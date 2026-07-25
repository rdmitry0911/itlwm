#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#import <CoreWLAN/CoreWLAN.h>
#import <CommonCrypto/CommonDigest.h>
#import <Foundation/Foundation.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * This deliberately narrow laboratory client performs exactly one public,
 * undirected CoreWLAN scan on the BSD endpoint owned by AirportItlwm.  The
 * target is accepted only as a SHA-256 hexadecimal digest and is compared
 * against candidate names in process.  It never renders or persists a
 * network name, BSSID, RSSI, channel number, IE, error text, credential, or
 * the target digest.  Its only output is an identity-free aggregate suitable
 * for checking whether a same-SSID multi-BSS fixture is receivable.
 */

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
    if (service == IO_OBJECT_NULL || name == NULL || capacity < 4)
        return 0;

    memset(name, 0, capacity);
    CFStringRef key = cfstr("BSD Name");
    if (key == NULL)
        return 0;
    CFTypeRef value = IORegistryEntryCreateCFProperty(service, key,
                                                       kCFAllocatorDefault, 0);
    CFRelease(key);
    if (value == NULL)
        return 0;
    const int copied = CFGetTypeID(value) == CFStringGetTypeID() &&
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

/* Do not accept a plaintext target in argv or the environment.  The input is
 * only a fixed-width digest and remains absent from all output. */
static int
decode_target_digest(uint8_t digest[CC_SHA256_DIGEST_LENGTH])
{
    const char *input = getenv("AIRPORT_ITLWM_LAB_TARGET_SSID_SHA256");
    if (digest == NULL || input == NULL)
        return 0;

    const size_t input_length = strlen(input);
    if (input_length != CC_SHA256_DIGEST_LENGTH * 2U)
        return 0;

    for (size_t index = 0; index < CC_SHA256_DIGEST_LENGTH; index++) {
        const int high = hex_value(input[index * 2U]);
        const int low = hex_value(input[index * 2U + 1U]);
        if (high < 0 || low < 0) {
            memset(digest, 0, CC_SHA256_DIGEST_LENGTH);
            return 0;
        }
        digest[index] = (uint8_t)((high << 4) | low);
    }
    return 1;
}

static int
ssid_matches_digest(NSString *ssid,
                    const uint8_t expected[CC_SHA256_DIGEST_LENGTH])
{
    if (ssid == nil || expected == NULL)
        return 0;
    NSData *data = [ssid dataUsingEncoding:NSUTF8StringEncoding];
    if (data == nil || data.length == 0 || data.length > UINT32_MAX)
        return 0;

    uint8_t digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(data.bytes, (CC_LONG)data.length, digest);
    const int matched = memcmp(digest, expected, sizeof(digest)) == 0;
    memset(digest, 0, sizeof(digest));
    return matched;
}

static void
increment(uint32_t *value, int *overflow)
{
    if (*value == UINT32_MAX) {
        *overflow = 1;
        return;
    }
    (*value)++;
}

int
main(void)
{
    uint8_t target_digest[CC_SHA256_DIGEST_LENGTH];
    char endpoint_name[16];
    uint32_t returned_records = 0;
    uint32_t returned_ssid_records = 0;
    uint32_t returned_bssid_records = 0;
    uint32_t returned_channel_records = 0;
    uint32_t target_records = 0;
    uint32_t distinct_bss = 0;
    uint32_t incomplete_bss_records = 0;
    uint32_t band_2ghz = 0;
    uint32_t band_5ghz = 0;
    uint32_t band_6ghz = 0;
    uint32_t band_other = 0;
    int scan_error_present = 0;
    int overflow = 0;
    const char *outcome = "scan-failed";
    const char *endpoint_binding = "unresolved";

    memset(target_digest, 0, sizeof(target_digest));
    memset(endpoint_name, 0, sizeof(endpoint_name));
    if (!decode_target_digest(target_digest)) {
        outcome = "target-input-invalid";
    } else {
        io_service_t service = find_service();
        if (service == IO_OBJECT_NULL) {
            outcome = "airport-itlwm-service-unavailable";
        } else if (!copy_airport_itlwm_bsd_name(service, endpoint_name,
                                                 sizeof(endpoint_name))) {
            outcome = "airport-itlwm-bsd-unresolved";
            IOObjectRelease(service);
        } else {
            endpoint_binding = "airport-itlwm-bsd";
            @autoreleasepool {
                NSString *endpoint = [NSString stringWithUTF8String:endpoint_name];
                memset(endpoint_name, 0, sizeof(endpoint_name));
                CWWiFiClient *client = [CWWiFiClient sharedWiFiClient];
                if (endpoint == nil || client == nil) {
                    outcome = "corewlan-input-unavailable";
                } else {
                    CWInterface *interface = [client interfaceWithName:endpoint];
                    if (interface == nil) {
                        outcome = "interface-unavailable";
                    } else {
                        NSError *scan_error = nil;
                        NSSet<CWNetwork *> *networks =
                            [interface scanForNetworksWithName:nil error:&scan_error];
                        scan_error_present = scan_error != nil ? 1 : 0;
                        if (networks != nil) {
                            NSMutableSet<NSString *> *seen_bss =
                                [[NSMutableSet alloc] init];
                            outcome = "ok";
                            for (CWNetwork *network in networks) {
                                NSString *network_name = [network ssid];
                                NSString *network_bss = [network bssid];
                                CWChannel *channel = [network wlanChannel];
                                increment(&returned_records, &overflow);
                                if (network_name != nil &&
                                    network_name.length != 0)
                                    increment(&returned_ssid_records,
                                              &overflow);
                                if (network_bss != nil &&
                                    network_bss.length != 0)
                                    increment(&returned_bssid_records,
                                              &overflow);
                                if (channel != nil)
                                    increment(&returned_channel_records,
                                              &overflow);
                                if (!ssid_matches_digest(network_name,
                                                         target_digest))
                                    continue;

                                increment(&target_records, &overflow);
                                if (network_bss == nil || network_bss.length == 0) {
                                    increment(&incomplete_bss_records, &overflow);
                                    continue;
                                }
                                if ([seen_bss containsObject:network_bss])
                                    continue;
                                [seen_bss addObject:network_bss];
                                increment(&distinct_bss, &overflow);
                                switch (channel != nil ? [channel channelBand] :
                                        kCWChannelBandUnknown) {
                                case kCWChannelBand2GHz:
                                    increment(&band_2ghz, &overflow);
                                    break;
                                case kCWChannelBand5GHz:
                                    increment(&band_5ghz, &overflow);
                                    break;
                                case kCWChannelBand6GHz:
                                    increment(&band_6ghz, &overflow);
                                    break;
                                default:
                                    increment(&band_other, &overflow);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            IOObjectRelease(service);
        }
    }
    memset(target_digest, 0, sizeof(target_digest));
    memset(endpoint_name, 0, sizeof(endpoint_name));

    if (overflow)
        outcome = "count-overflow";
    const unsigned target_present = target_records != 0 ? 1U : 0U;
    const unsigned multi_ap_visible = distinct_bss >= 2 ? 1U : 0U;
    const unsigned represented_bands = (band_2ghz != 0 ? 1U : 0U) +
        (band_5ghz != 0 ? 1U : 0U) + (band_6ghz != 0 ? 1U : 0U) +
        (band_other != 0 ? 1U : 0U);
    const unsigned multi_band_visible = represented_bands >= 2 ? 1U : 0U;
    printf("lab_target_visibility=%s endpoint_binding=%s returned_records=%u "
           "returned_ssid_records=%u returned_bssid_records=%u "
           "returned_channel_records=%u target_present=%u "
           "target_records=%u distinct_bss=%u incomplete_bss_records=%u "
           "band_2ghz=%u band_5ghz=%u band_6ghz=%u band_other=%u "
           "multi_ap_visible=%u multi_band_visible=%u scan_error_present=%u\n",
           outcome, endpoint_binding, returned_records, returned_ssid_records,
           returned_bssid_records, returned_channel_records, target_present,
           target_records,
           distinct_bss, incomplete_bss_records, band_2ghz, band_5ghz,
           band_6ghz, band_other, multi_ap_visible, multi_band_visible,
           scan_error_present != 0 ? 1U : 0U);
    return strcmp(outcome, "ok") == 0 ? 0 : 1;
}
