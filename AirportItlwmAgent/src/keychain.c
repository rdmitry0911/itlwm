/*
 * AirportItlwmAgent — project-owned keychain credential lookup.
 *
 * The helper does NOT read airportd-populated entries in
 * /Library/Keychains/System.keychain: those entries are created
 * with an ACL that authorizes decrypt-without-user-interaction
 * only to /usr/libexec/airportd and to apps belonging to the
 * Apple-anchored AirPort application group. An ad-hoc-codesigned
 * project helper is not on that allow list and therefore receives
 * errSecInteractionNotAllowed (OSStatus -25308) from
 * SecKeychainFindGenericPassword against the airportd-created
 * item. Adding a parallel non-canonical item to the same keychain
 * to bypass that ACL would mask the real authorization contract
 * and is rejected.
 *
 * Instead the helper reads from a dedicated project-owned
 * system-domain keychain at /Library/Keychains/AirportItlwm.keychain
 * that the install script creates with a per-install random
 * unlock password stored at /etc/airportitlwm/keychain-password
 * (mode 0600, root:wheel). The helper
 * reads the unlock password from that file on every relaunch,
 * passes it to SecKeychainUnlock, and immediately scrubs the
 * local buffer via the agent_zero() volatile-pointer memset
 * pattern. Operator-supplied PSK items are added with
 * `security add-generic-password -s "AirportItlwm WiFi PSK"
 * -a <SSID> -w <passphrase> -A /Library/Keychains/AirportItlwm.keychain`
 * (the -A flag makes the per-item access list permissive so the
 * helper can decrypt the password under root). The security trust
 * boundary is the root-only filesystem permissions on both the
 * keychain file and the unlock-password file; the unlock password
 * itself is NOT a separate secret -- anyone who can read the
 * keychain-password file can also read the keychain file, so the
 * password's only role is to drive the macOS Security framework
 * APIs that require a non-empty unlock password on macOS 26 Tahoe.
 *
 * Both APIs (Sec{Keychain,Item}Copy*) are still functional on
 * macOS 26 Tahoe; the Sec{Keychain,Item}* deprecation warnings
 * are silenced locally so the build stays clean.
 *
 * Password bytes (PSK passphrase AND keychain unlock password)
 * are NEVER printed. Only the SSID alias (visible in any beacon
 * scan), the lookup return code, and length markers are logged.
 */
#include "keychain.h"
#include "log.h"

#include <Security/Security.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

/*
 * Maximum project-keychain unlock-password length we accept on
 * read. The install script writes a base64-encoded 32-byte
 * random value (44 base64 chars + trailing newline = 45 bytes),
 * so 256 bytes is a generous upper bound.
 */
#define kAgentKeychainPasswordMaxLen 256u

/*
 * Read the project keychain unlock password from the install-time
 * file. Returns 0 on success, -1 on failure. On success
 * *inout_len is the trimmed password length (trailing \n / \r
 * stripped); pw_buf MUST be zeroed by the caller after use.
 */
static int
agent_read_keychain_password(const char *path,
                             uint8_t *pw_buf, size_t pw_buf_cap,
                             size_t *out_len)
{
    if (path == NULL || pw_buf == NULL || pw_buf_cap == 0 ||
        out_len == NULL) {
        return -1;
    }

    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        AGENT_ERR("open(%s) errno=%d", path, errno);
        return -1;
    }

    /*
     * Reject world-readable or group-readable files defensively;
     * the install script writes the file with mode 0600 root:wheel
     * and the helper runs under launchd as UserName=root.
     */
    struct stat st;
    if (fstat(fd, &st) != 0) {
        AGENT_ERR("fstat(%s) errno=%d", path, errno);
        close(fd);
        return -1;
    }
    if (!S_ISREG(st.st_mode)) {
        AGENT_ERR("refusing keychain-password file: not a regular "
                  "file (mode 0%o)", st.st_mode & 0777);
        close(fd);
        return -1;
    }
    if (st.st_uid != 0) {
        AGENT_ERR("refusing keychain-password file owned by uid=%u "
                  "(want uid 0 / root)", (unsigned)st.st_uid);
        close(fd);
        return -1;
    }
    if ((st.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
        AGENT_ERR("refusing keychain-password file with permissive "
                  "mode 0%o (want 0600)", st.st_mode & 0777);
        close(fd);
        return -1;
    }

    ssize_t n = read(fd, pw_buf, pw_buf_cap);
    int read_errno = errno;
    close(fd);
    if (n < 0) {
        AGENT_ERR("read(%s) errno=%d", path, read_errno);
        return -1;
    }
    size_t len = (size_t)n;
    /* Strip trailing newlines / CRs. */
    while (len > 0 &&
           (pw_buf[len - 1] == '\n' || pw_buf[len - 1] == '\r')) {
        pw_buf[--len] = 0;
    }
    if (len == 0) {
        AGENT_ERR("keychain-password file %s is empty after trim",
                  path);
        return -1;
    }
    *out_len = len;
    return 0;
}

/*
 * Volatile-pointer memset for local secret-bearing buffers. Mirrors
 * the agent_zero() pattern in main.m so the unlock password does
 * not linger in the helper's stack after the unlock call returns.
 */
static void
agent_zero_local(void *p, size_t n)
{
    volatile uint8_t *v = (volatile uint8_t *)p;
    while (n--)
        *v++ = 0;
}

int
AgentLookupProjectPSK(const uint8_t *ssid, size_t ssid_len,
                     uint8_t *out_password,
                     size_t *inout_password_len)
{
    if (ssid == NULL || ssid_len == 0 ||
        out_password == NULL || inout_password_len == NULL ||
        *inout_password_len == 0) {
        return -1;
    }

    static const char kProjectKeychainPath[] =
        "/Library/Keychains/AirportItlwm.keychain";
    static const char kProjectKeychainPasswordPath[] =
        "/etc/airportitlwm/keychain-password";

    SecKeychainRef proj_kc = NULL;
    OSStatus st = SecKeychainOpen(kProjectKeychainPath, &proj_kc);
    if (st != errSecSuccess || proj_kc == NULL) {
        AGENT_ERR("SecKeychainOpen(project) OSStatus=%d", (int)st);
        return -1;
    }

    /*
     * Read the install-time unlock password from the project's
     * root-only password file and pass it to SecKeychainUnlock. On
     * macOS 26 Tahoe, `security create-keychain -p ""` produces a
     * keychain whose actual unlock password does NOT equal the
     * literal empty byte string, so an empty-password unlock attempt
     * fails deterministically with OSStatus -25293 / errSecAuth
     * Failed and underlying CSSMERR_DL_OPERATION_AUTH_DENIED. A
     * non-empty unlock password (per-install random bytes written
     * by the install script) avoids this Tahoe behavior. The
     * password file is mode 0600 root:wheel; the helper reads it
     * on every relaunch and scrubs the local stack buffer before
     * returning from this function.
     */
    uint8_t pw_buf[kAgentKeychainPasswordMaxLen];
    size_t  pw_len = 0;
    if (agent_read_keychain_password(kProjectKeychainPasswordPath,
                                     pw_buf, sizeof(pw_buf),
                                     &pw_len) != 0) {
        agent_zero_local(pw_buf, sizeof(pw_buf));
        CFRelease(proj_kc);
        return -1;
    }

    st = SecKeychainUnlock(proj_kc, (UInt32)pw_len, pw_buf, TRUE);
    agent_zero_local(pw_buf, sizeof(pw_buf));
    if (st != errSecSuccess) {
        AGENT_ERR("SecKeychainUnlock(project) OSStatus=%d", (int)st);
        CFRelease(proj_kc);
        return -1;
    }

    /*
     * Project-owned generic-password schema:
     *   service (svce) = "AirportItlwm WiFi PSK"
     *   account (acct) = SSID byte window
     *   data           = WPA2 passphrase bytes
     * The service value is project-specific so it cannot collide
     * with airportd-populated System.keychain entries (which use
     * svce = "AirPort").
     */
    static const char kServiceTag[] = "AirportItlwm WiFi PSK";

    UInt32 pass_len_u = 0;
    void *pass_bytes  = NULL;
    SecKeychainItemRef item = NULL;

    st = SecKeychainFindGenericPassword(
        proj_kc,
        (UInt32)sizeof(kServiceTag) - 1, kServiceTag,
        (UInt32)ssid_len, (const char *)ssid,
        &pass_len_u, &pass_bytes, &item);

    CFRelease(proj_kc);

    if (st == errSecItemNotFound) {
        AGENT_LOG("AgentLookupProjectPSK NOT_FOUND ssid_len=%zu",
                  ssid_len);
        return -2;
    }
    if (st != errSecSuccess || pass_bytes == NULL) {
        AGENT_ERR("SecKeychainFindGenericPassword OSStatus=%d "
                  "ssid_len=%zu", (int)st, ssid_len);
        if (item)
            CFRelease(item);
        return -1;
    }

    if ((size_t)pass_len_u > *inout_password_len) {
        AGENT_ERR("AgentLookupProjectPSK BUFFER_TOO_SMALL "
                  "have=%zu need=%u",
                  *inout_password_len, pass_len_u);
        *inout_password_len = pass_len_u;
        SecKeychainItemFreeContent(NULL, pass_bytes);
        if (item)
            CFRelease(item);
        return -3;
    }

    memcpy(out_password, pass_bytes, pass_len_u);
    *inout_password_len = pass_len_u;

    /*
     * SecKeychainItemFreeContent zeroes the framework-owned buffer
     * before freeing it (per SecKeychain.h documentation: "The
     * function uses memset() to overwrite the password before
     * freeing it"). Combined with the caller's responsibility to
     * zero out_password when done, the cleartext PSK has no
     * permanent footprint in helper memory.
     */
    SecKeychainItemFreeContent(NULL, pass_bytes);
    if (item)
        CFRelease(item);

    AGENT_LOG("AgentLookupProjectPSK FOUND ssid_len=%zu password_len=%u",
              ssid_len, pass_len_u);
    return 0;
}

#pragma clang diagnostic pop
