/*
 * Bounded public-CoreWLAN recovery laboratory client.
 *
 * This is deliberately a user-facing association probe, not a private
 * driver/UserClient shortcut.  It receives only SHA-256 target identities in
 * the environment, reads one WPA/WPA2 credential record and one later
 * withdrawal acknowledgement from a pipe, and never prints or persists an
 * SSID, BSSID, credential, channel, NSError description, profile, or scan
 * record.  It does not call any CoreWLAN configuration-commit API.
 *
 * Before the host withdraws its temporary first BSS it must first request an
 * arm acknowledgement.  The client verifies it is still on that exact BSS,
 * publishes the arm acknowledgement, and only then accepts a withdrawal
 * acknowledgement.  After that acknowledgement this client performs no
 * second association: success means the same target SSID is associated on a
 * BSSID different from the exact first BSS.
 */

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#import <CoreWLAN/CoreWLAN.h>
#import <CommonCrypto/CommonDigest.h>
#import <Foundation/Foundation.h>

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum {
    kCredentialMinimumLength = 8u,
    kCredentialMaximumLength = 64u,
    kCredentialInputDeadlineMilliseconds = 15000u,
    /* The host's verified one-shot withdrawal can itself take up to twenty
     * seconds to stop hostapd.  Keep the post-association control window
     * comfortably above that bounded operation without leaving a client
     * indefinitely associated if the controller disappears. */
    kControlInputDeadlineMilliseconds = 60000u,
    kDiscoveryAttempts = 80u,
    kInitialIdentityAttempts = 40u,
    kRecoveryAttempts = 240u,
    kPollDelayMilliseconds = 500u,
};

static void
secure_bzero(void *bytes, size_t length)
{
    volatile uint8_t *volatile cursor = bytes;

    while (length-- != 0)
        *cursor++ = 0;
}

static CFStringRef
cfstr(const char *text)
{
    return CFStringCreateWithCString(kCFAllocatorDefault, text,
                                    kCFStringEncodingUTF8);
}

static io_service_t
find_service(void)
{
    CFMutableDictionaryRef match = IOServiceMatching("AirportItlwm");

    if (match == NULL)
        return IO_OBJECT_NULL;
    return IOServiceGetMatchingService(kIOMainPortDefault, match);
}

static int
copy_airport_itlwm_bsd_name(io_service_t service, char *name, size_t capacity)
{
    CFStringRef key;
    CFTypeRef value;
    int copied;

    if (service == IO_OBJECT_NULL || name == NULL || capacity < 4)
        return 0;
    memset(name, 0, capacity);
    key = cfstr("BSD Name");
    if (key == NULL)
        return 0;
    value = IORegistryEntryCreateCFProperty(service, key,
                                             kCFAllocatorDefault, 0);
    CFRelease(key);
    if (value == NULL)
        return 0;
    copied = CFGetTypeID(value) == CFStringGetTypeID() &&
        CFStringGetCString((CFStringRef)value, name, capacity,
                           kCFStringEncodingUTF8);
    CFRelease(value);
    if (!copied || name[0] != 'e' || name[1] != 'n' || name[2] == '\0') {
        memset(name, 0, capacity);
        return 0;
    }
    for (size_t index = 2; name[index] != '\0'; index++) {
        if (name[index] < '0' || name[index] > '9') {
            memset(name, 0, capacity);
            return 0;
        }
    }
    return 1;
}

static int
hex_value(char character)
{
    if (character >= '0' && character <= '9')
        return character - '0';
    if (character >= 'a' && character <= 'f')
        return character - 'a' + 10;
    if (character >= 'A' && character <= 'F')
        return character - 'A' + 10;
    return -1;
}

static int
decode_digest_environment(const char *name,
                          uint8_t digest[CC_SHA256_DIGEST_LENGTH])
{
    const char *input;

    if (name == NULL || digest == NULL || (input = getenv(name)) == NULL)
        return 0;
    if (strlen(input) != CC_SHA256_DIGEST_LENGTH * 2u)
        return 0;
    for (size_t index = 0; index < CC_SHA256_DIGEST_LENGTH; index++) {
        const int high = hex_value(input[index * 2u]);
        const int low = hex_value(input[index * 2u + 1u]);

        if (high < 0 || low < 0) {
            secure_bzero(digest, CC_SHA256_DIGEST_LENGTH);
            return 0;
        }
        digest[index] = (uint8_t)((high << 4) | low);
    }
    return 1;
}

static int
string_matches_digest(NSString *value,
                      const uint8_t expected[CC_SHA256_DIGEST_LENGTH])
{
    NSData *data;
    uint8_t digest[CC_SHA256_DIGEST_LENGTH];
    int matched;

    if (value == nil || expected == NULL)
        return 0;
    data = [value dataUsingEncoding:NSUTF8StringEncoding];
    if (data == nil || data.length == 0 || data.length > UINT32_MAX)
        return 0;
    CC_SHA256(data.bytes, (CC_LONG)data.length, digest);
    matched = memcmp(digest, expected, sizeof(digest)) == 0;
    secure_bzero(digest, sizeof(digest));
    return matched;
}

/* `iw` reports its BSSID in a lower-case textual form while CoreWLAN does not
 * promise a particular case.  The controller and this public client therefore
 * bind a MAC address through a fixed lower-case ASCII representation, not
 * through a framework-specific rendering. */
static int
bssid_matches_digest(NSString *value,
                     const uint8_t expected[CC_SHA256_DIGEST_LENGTH])
{
    NSData *data;
    const uint8_t *input;
    uint8_t canonical[17];
    uint8_t digest[CC_SHA256_DIGEST_LENGTH];
    int matched = 0;

    if (value == nil || expected == NULL)
        return 0;
    data = [value dataUsingEncoding:NSUTF8StringEncoding];
    if (data == nil || data.length != sizeof(canonical) || data.bytes == NULL)
        return 0;
    input = data.bytes;
    for (size_t index = 0; index < sizeof(canonical); index++) {
        uint8_t byte = input[index];

        if (index % 3u == 2u) {
            if (byte != ':')
                goto out;
        } else if (hex_value((char)byte) < 0) {
            goto out;
        } else if (byte >= 'A' && byte <= 'F') {
            byte = (uint8_t)(byte - 'A' + 'a');
        }
        canonical[index] = byte;
    }
    CC_SHA256(canonical, (CC_LONG)sizeof(canonical), digest);
    matched = memcmp(digest, expected, sizeof(digest)) == 0;

out:
    secure_bzero(canonical, sizeof(canonical));
    secure_bzero(digest, sizeof(digest));
    return matched;
}

static int64_t
monotonic_milliseconds(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 ||
        now.tv_sec > INT64_MAX / 1000)
        return -1;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int
wait_for_stdin_until(int64_t deadline)
{
    struct pollfd descriptor;
    int64_t now;
    int remaining;
    int result;

    now = monotonic_milliseconds();
    if (now < 0 || now >= deadline)
        return 0;
    remaining = deadline - now > INT_MAX ? INT_MAX : (int)(deadline - now);
    descriptor.fd = STDIN_FILENO;
    descriptor.events = POLLIN | POLLHUP;
    descriptor.revents = 0;
    do {
        result = poll(&descriptor, 1, remaining);
    } while (result < 0 && errno == EINTR);
    secure_bzero(&descriptor, sizeof(descriptor));
    return result > 0;
}

/* Read exactly one newline-terminated ASCII record without copying it to an
 * argv/environment/file.  The caller decides whether EOF is required next. */
static int
read_bounded_line(uint8_t *out, size_t capacity, size_t *out_length,
                  int64_t deadline)
{
    size_t length = 0;
    uint8_t byte = 0;

    if (out == NULL || out_length == NULL || capacity == 0)
        return 0;
    secure_bzero(out, capacity);
    *out_length = 0;
    for (;;) {
        ssize_t count;

        if (!wait_for_stdin_until(deadline))
            goto fail;
        do {
            count = read(STDIN_FILENO, &byte, sizeof(byte));
        } while (count < 0 && errno == EINTR);
        if (count != 1)
            goto fail;
        if (byte == '\n') {
            if (length == 0)
                goto fail;
            *out_length = length;
            secure_bzero(&byte, sizeof(byte));
            return 1;
        }
        if (byte == '\0' || byte == '\r' || length == capacity)
            goto fail;
        out[length++] = byte;
    }

fail:
    secure_bzero(&byte, sizeof(byte));
    secure_bzero(out, capacity);
    return 0;
}

static int
credential_is_valid(const uint8_t *credential, size_t length)
{
    int all_hex = 1;

    if (credential == NULL || length < kCredentialMinimumLength ||
        length > kCredentialMaximumLength)
        return 0;
    for (size_t index = 0; index < length; index++) {
        if (credential[index] < 0x20 || credential[index] > 0x7e)
            return 0;
        if (hex_value((char)credential[index]) < 0)
            all_hex = 0;
    }
    return length != kCredentialMaximumLength || all_hex;
}

static int
read_credential(uint8_t credential[kCredentialMaximumLength],
                size_t *credential_length)
{
    struct stat input_status;
    int64_t started;
    int64_t deadline;

    if (credential == NULL || credential_length == NULL ||
        isatty(STDIN_FILENO) != 0 ||
        fstat(STDIN_FILENO, &input_status) != 0 ||
        !S_ISFIFO(input_status.st_mode))
        return 0;
    started = monotonic_milliseconds();
    if (started < 0 ||
        started > INT64_MAX - kCredentialInputDeadlineMilliseconds)
        return 0;
    deadline = started + kCredentialInputDeadlineMilliseconds;
    if (!read_bounded_line(credential, kCredentialMaximumLength,
                           credential_length, deadline) ||
        !credential_is_valid(credential, *credential_length)) {
        secure_bzero(credential, kCredentialMaximumLength);
        *credential_length = 0;
        return 0;
    }
    return 1;
}

static int
read_control_token(const char *expected, int require_eof)
{
    uint8_t control[32];
    uint8_t extra = 0;
    size_t control_length = 0;
    size_t expected_length;
    int64_t started;
    int64_t deadline;
    ssize_t count;
    int accepted = 0;

    secure_bzero(control, sizeof(control));
    if (expected == NULL || (expected_length = strlen(expected)) == 0 ||
        expected_length >= sizeof(control))
        goto out;
    started = monotonic_milliseconds();
    if (started < 0 ||
        started > INT64_MAX - kControlInputDeadlineMilliseconds)
        goto out;
    deadline = started + kControlInputDeadlineMilliseconds;
    if (!read_bounded_line(control, sizeof(control), &control_length,
                           deadline) ||
        control_length != expected_length ||
        memcmp(control, expected, expected_length) != 0)
        goto out;
    if (!require_eof) {
        accepted = 1;
        goto out;
    }
    if (!wait_for_stdin_until(deadline))
        goto out;
    do {
        count = read(STDIN_FILENO, &extra, sizeof(extra));
    } while (count < 0 && errno == EINTR);
    accepted = count == 0;

out:
    secure_bzero(control, sizeof(control));
    secure_bzero(&extra, sizeof(extra));
    return accepted;
}

static int
read_withdrawal_arm_control(void)
{
    return read_control_token("arm-withdraw", 0);
}

static int
read_withdrawal_control(void)
{
    return read_control_token("withdraw", 1);
}

static void
delay_milliseconds(uint32_t milliseconds)
{
    struct timespec delay;

    delay.tv_sec = milliseconds / 1000u;
    delay.tv_nsec = (long)(milliseconds % 1000u) * 1000000L;
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR)
        ;
    secure_bzero(&delay, sizeof(delay));
}

static CWNetwork *
scan_for_exact_target(CWInterface *interface,
                      const uint8_t ssid_digest[CC_SHA256_DIGEST_LENGTH],
                      const uint8_t bssid_digest[CC_SHA256_DIGEST_LENGTH],
                      uint32_t *matching_records,
                      int *alternate_bss_visible,
                      int *scan_error_present)
{
    NSError *scan_error = nil;
    NSSet<CWNetwork *> *networks;
    CWNetwork *target = nil;
    uint32_t matches = 0;

    if (matching_records != NULL)
        *matching_records = 0;
    if (alternate_bss_visible != NULL)
        *alternate_bss_visible = 0;
    if (interface == nil || ssid_digest == NULL || bssid_digest == NULL)
        return nil;
    networks = [interface scanForNetworksWithName:nil error:&scan_error];
    if (scan_error != nil && scan_error_present != NULL)
        *scan_error_present = 1;
    for (CWNetwork *network in networks) {
        NSString *network_bssid;

        if (!string_matches_digest([network ssid], ssid_digest))
            continue;
        network_bssid = [network bssid];
        if (!bssid_matches_digest(network_bssid, bssid_digest)) {
            if (network_bssid != nil && network_bssid.length != 0 &&
                alternate_bss_visible != NULL)
                *alternate_bss_visible = 1;
            continue;
        }
        if (matches == UINT32_MAX) {
            matches = 0;
            target = nil;
            break;
        }
        matches++;
        target = network;
    }
    if (matching_records != NULL)
        *matching_records = matches;
    return matches == 1u ? target : nil;
}

static CWNetwork *
wait_for_exact_target(CWInterface *interface,
                      const uint8_t ssid_digest[CC_SHA256_DIGEST_LENGTH],
                      const uint8_t bssid_digest[CC_SHA256_DIGEST_LENGTH],
                      uint32_t *attempts, uint32_t *matching_records,
                      int *alternate_bss_visible,
                      int *scan_error_present)
{
    CWNetwork *target = nil;

    if (attempts != NULL)
        *attempts = 0;
    if (matching_records != NULL)
        *matching_records = 0;
    if (alternate_bss_visible != NULL)
        *alternate_bss_visible = 0;
    for (uint32_t attempt = 1; attempt <= kDiscoveryAttempts; attempt++) {
        uint32_t matches = 0;
        int alternate = 0;

        target = scan_for_exact_target(interface, ssid_digest, bssid_digest,
                                       &matches, &alternate,
                                       scan_error_present);
        if (attempts != NULL)
            *attempts = attempt;
        if (matching_records != NULL)
            *matching_records = matches;
        if (alternate_bss_visible != NULL)
            *alternate_bss_visible = alternate;
        if (target != nil && alternate)
            return target;
        if (attempt != kDiscoveryAttempts)
            delay_milliseconds(kPollDelayMilliseconds);
    }
    return nil;
}

static int
interface_has_exact_initial_identity(
    CWInterface *interface,
    const uint8_t ssid_digest[CC_SHA256_DIGEST_LENGTH],
    const uint8_t bssid_digest[CC_SHA256_DIGEST_LENGTH])
{
    return interface != nil &&
        string_matches_digest([interface ssid], ssid_digest) &&
        bssid_matches_digest([interface bssid], bssid_digest);
}

static int
wait_for_exact_initial_identity(
    CWInterface *interface,
    const uint8_t ssid_digest[CC_SHA256_DIGEST_LENGTH],
    const uint8_t bssid_digest[CC_SHA256_DIGEST_LENGTH])
{
    for (uint32_t attempt = 1; attempt <= kInitialIdentityAttempts; attempt++) {
        if (interface_has_exact_initial_identity(interface, ssid_digest,
                                                 bssid_digest))
            return 1;
        if (attempt != kInitialIdentityAttempts)
            delay_milliseconds(kPollDelayMilliseconds);
    }
    return 0;
}

static int
wait_for_same_ssid_different_bss(
    CWInterface *interface,
    const uint8_t ssid_digest[CC_SHA256_DIGEST_LENGTH],
    const uint8_t initial_bssid_digest[CC_SHA256_DIGEST_LENGTH])
{
    for (uint32_t attempt = 1; attempt <= kRecoveryAttempts; attempt++) {
        NSString *current_bssid = interface != nil ? [interface bssid] : nil;

        if (interface != nil &&
            string_matches_digest([interface ssid], ssid_digest) &&
            current_bssid != nil &&
            !bssid_matches_digest(current_bssid, initial_bssid_digest))
            return 1;
        if (attempt != kRecoveryAttempts)
            delay_milliseconds(kPollDelayMilliseconds);
    }
    return 0;
}

static void
emit_result(const char *result, const char *endpoint_binding,
            uint32_t discovery_attempts, uint32_t matching_records,
            int alternate_bss_visible, int scan_error_present,
            int association_error_present,
            int initial_identity_exact, int withdrawal_arm_accepted,
            int pre_withdrawal_identity_exact,
            int withdrawal_control_accepted,
            int recovery_same_ssid, int recovery_different_bss,
            int cleanup_disassociate_attempted)
{
    printf("public_corewlan_recovery=%s endpoint_binding=%s "
           "discovery_attempts=%u matching_records=%u "
           "alternate_bss_visible=%u scan_error_present=%u "
           "association_error_present=%u "
           "initial_identity_exact=%u withdrawal_arm_accepted=%u "
           "pre_withdrawal_identity_exact=%u "
           "withdrawal_control_accepted=%u "
           "recovery_same_ssid=%u recovery_different_bss=%u "
           "cleanup_disassociate_attempted=%u\n",
           result, endpoint_binding, discovery_attempts, matching_records,
           alternate_bss_visible != 0 ? 1u : 0u,
           scan_error_present != 0 ? 1u : 0u,
           association_error_present != 0 ? 1u : 0u,
           initial_identity_exact != 0 ? 1u : 0u,
           withdrawal_arm_accepted != 0 ? 1u : 0u,
           pre_withdrawal_identity_exact != 0 ? 1u : 0u,
           withdrawal_control_accepted != 0 ? 1u : 0u,
           recovery_same_ssid != 0 ? 1u : 0u,
           recovery_different_bss != 0 ? 1u : 0u,
           cleanup_disassociate_attempted != 0 ? 1u : 0u);
    fflush(stdout);
}

int
main(int argc, char **argv)
{
    static const char ssid_digest_name[] =
        "AIRPORT_ITLWM_LAB_TARGET_SSID_SHA256";
    static const char bssid_digest_name[] =
        "AIRPORT_ITLWM_LAB_TARGET_BSSID_SHA256";
    uint8_t ssid_digest[CC_SHA256_DIGEST_LENGTH];
    uint8_t bssid_digest[CC_SHA256_DIGEST_LENGTH];
    uint8_t credential[kCredentialMaximumLength];
    size_t credential_length = 0;
    char endpoint_name[16];
    io_service_t service = IO_OBJECT_NULL;
    CWInterface *interface = nil;
    CWNetwork *target = nil;
    const char *endpoint_binding = "unresolved";
    const char *result = "not-started";
    uint32_t discovery_attempts = 0;
    uint32_t matching_records = 0;
    int alternate_bss_visible = 0;
    int scan_error_present = 0;
    int association_error_present = 0;
    int initial_identity_exact = 0;
    int withdrawal_arm_accepted = 0;
    int pre_withdrawal_identity_exact = 0;
    int withdrawal_control_accepted = 0;
    int recovery_same_ssid = 0;
    int recovery_different_bss = 0;
    int associated = 0;
    int cleanup_disassociate_attempted = 0;
    int exit_code = 1;

    secure_bzero(ssid_digest, sizeof(ssid_digest));
    secure_bzero(bssid_digest, sizeof(bssid_digest));
    secure_bzero(credential, sizeof(credential));
    secure_bzero(endpoint_name, sizeof(endpoint_name));
    (void)argv;
    if (argc != 1) {
        result = "usage";
        goto out;
    }
    if (!decode_digest_environment(ssid_digest_name, ssid_digest) ||
        !decode_digest_environment(bssid_digest_name, bssid_digest)) {
        result = "target-input-invalid";
        goto out;
    }
    if (!read_credential(credential, &credential_length)) {
        result = "credential-input-invalid";
        goto out;
    }
    service = find_service();
    if (service == IO_OBJECT_NULL) {
        result = "airport-itlwm-service-unavailable";
        goto out;
    }
    if (!copy_airport_itlwm_bsd_name(service, endpoint_name,
                                     sizeof(endpoint_name))) {
        result = "airport-itlwm-bsd-unresolved";
        goto out;
    }
    endpoint_binding = "airport-itlwm-bsd";
    @autoreleasepool {
        NSString *endpoint = [NSString stringWithUTF8String:endpoint_name];
        CWWiFiClient *client = [CWWiFiClient sharedWiFiClient];

        secure_bzero(endpoint_name, sizeof(endpoint_name));
        if (endpoint == nil || client == nil) {
            result = "corewlan-input-unavailable";
            goto out;
        }
        interface = [client interfaceWithName:endpoint];
        if (interface == nil) {
            result = "interface-unavailable";
            goto out;
        }
        target = wait_for_exact_target(interface, ssid_digest, bssid_digest,
                                       &discovery_attempts,
                                       &matching_records,
                                       &alternate_bss_visible,
                                       &scan_error_present);
        if (target == nil) {
            result = "initial-or-alternate-target-unavailable";
            goto out;
        }
        @autoreleasepool {
            NSString *password = [[NSString alloc]
                initWithBytes:credential length:credential_length
                encoding:NSUTF8StringEncoding];
            NSError *association_error = nil;
            int associated_now = 0;

            secure_bzero(credential, sizeof(credential));
            credential_length = 0;
            if (password != nil)
                associated_now = [interface associateToNetwork:target
                                                       password:password
                                                          error:&association_error];
            association_error_present = association_error != nil;
            password = nil;
            if (!associated_now) {
                result = "public-association-failed";
                goto out;
            }
        }
        associated = 1;
        initial_identity_exact = wait_for_exact_initial_identity(
            interface, ssid_digest, bssid_digest);
        if (!initial_identity_exact) {
            result = "initial-identity-timeout";
            goto out;
        }
        emit_result("initial-ready", endpoint_binding, discovery_attempts,
                    matching_records, alternate_bss_visible, scan_error_present,
                    association_error_present, initial_identity_exact, 0, 0,
                    0, 0, 0, 0);
        withdrawal_arm_accepted = read_withdrawal_arm_control();
        if (!withdrawal_arm_accepted) {
            result = "withdrawal-arm-rejected";
            goto out;
        }
        pre_withdrawal_identity_exact = interface_has_exact_initial_identity(
            interface, ssid_digest, bssid_digest);
        if (!pre_withdrawal_identity_exact) {
            result = "pre-withdrawal-identity-lost";
            goto out;
        }
        emit_result("withdraw-armed", endpoint_binding, discovery_attempts,
                    matching_records, alternate_bss_visible, scan_error_present,
                    association_error_present, initial_identity_exact,
                    withdrawal_arm_accepted, pre_withdrawal_identity_exact, 0,
                    0, 0, 0);
        withdrawal_control_accepted = read_withdrawal_control();
        if (!withdrawal_control_accepted) {
            result = "withdrawal-control-rejected";
            goto out;
        }
        recovery_same_ssid = wait_for_same_ssid_different_bss(
            interface, ssid_digest, bssid_digest);
        recovery_different_bss = recovery_same_ssid;
        if (!recovery_same_ssid) {
            result = "recovery-timeout";
            goto out;
        }
        result = "recovered";
        exit_code = 0;
    }

out:
    secure_bzero(credential, sizeof(credential));
    credential_length = 0;
    if (associated && interface != nil) {
        [interface disassociate];
        cleanup_disassociate_attempted = 1;
    }
    if (service != IO_OBJECT_NULL)
        IOObjectRelease(service);
    secure_bzero(ssid_digest, sizeof(ssid_digest));
    secure_bzero(bssid_digest, sizeof(bssid_digest));
    secure_bzero(endpoint_name, sizeof(endpoint_name));
    emit_result(result, endpoint_binding, discovery_attempts, matching_records,
                alternate_bss_visible, scan_error_present,
                association_error_present,
                initial_identity_exact, withdrawal_arm_accepted,
                pre_withdrawal_identity_exact, withdrawal_control_accepted,
                recovery_same_ssid, recovery_different_bss,
                cleanup_disassociate_attempted);
    return exit_code;
}
