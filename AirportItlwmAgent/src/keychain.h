/*
 * AirportItlwmAgent — project-owned keychain credential lookup.
 *
 * Reads the WPA2 passphrase from a dedicated project-owned
 * system-domain keychain at /Library/Keychains/AirportItlwm.keychain.
 * The lookup matches on service (svce) = "AirportItlwm WiFi PSK"
 * and account (acct) = the SSID byte window the kext published in
 * the AirportItlwmAssociationTarget. Items are populated by the
 * operator at install time with
 * `security add-generic-password -A`, which sets a permissive
 * per-item access list so the ad-hoc-codesigned helper can
 * decrypt the password under root.
 *
 * Before SecKeychainFindGenericPassword can run, the keychain
 * must be unlocked. On macOS 26 Tahoe, `security create-keychain
 * -p ""` produces a keychain whose actual unlock password does
 * NOT equal the literal empty byte string, so the install script
 * generates a per-install random unlock password and stores it
 * at /etc/airportitlwm/keychain-password (mode 0600, root:wheel).
 * The helper reads that file on every relaunch, passes the bytes
 * to SecKeychainUnlock, and immediately scrubs the local buffer.
 * The security trust boundary is filesystem permissions on BOTH
 * the keychain file and the unlock-password file (both root-only);
 * the unlock password itself is not an independent secret.
 *
 * Runs as root so it has read access to both the keychain file
 * and the unlock-password file.
 *
 * AgentPrimeProjectKeychain opens and unlocks the project keychain
 * without reading any SSID credential. The LaunchDaemon calls it
 * before the first association target can arrive so Tahoe's cold
 * securityd / keychain unlock latency is paid before the kext's
 * pre-M1 PMK wait window begins. Lookup still unlocks defensively
 * on every association edge, so keychain auto-lock remains safe.
 *
 * Returns the password bytes through caller-allocated output
 * buffers; the caller is responsible for zeroing the buffer when
 * done. No password bytes are logged here or in any downstream
 * call. Only the SSID alias (the SSID is also visible in any
 * beacon scan, so it is not a credential), the result code, and
 * a structural marker (password length, found/not-found) are
 * surfaced.
 */
#ifndef AIRPORTITLWMAGENT_KEYCHAIN_H
#define AIRPORTITLWMAGENT_KEYCHAIN_H

#include <stddef.h>
#include <stdint.h>

/*
 * Open and unlock /Library/Keychains/AirportItlwm.keychain using
 * the root-only per-install unlock password file, then immediately
 * release the keychain reference. Returns 0 on success, -1 on
 * keychain open / password-file / unlock failure. No credential
 * item is read and no secret bytes are logged.
 */
int AgentPrimeProjectKeychain(void);

/*
 * AgentLookupProjectPSK: find the generic-password entry in the
 * project-owned /Library/Keychains/AirportItlwm.keychain whose
 * service (svce) equals "AirportItlwm WiFi PSK" and whose
 * account (acct) equals the SSID byte window
 * [ssid, ssid+ssid_len).
 *
 * On success returns 0 and writes the password bytes into
 * out_password (no NUL terminator) and the byte length into
 * *inout_password_len (caller passes the capacity in; the call
 * shrinks it to the actual length).
 *
 * Returns:
 *   0                 — found and copied
 *  -1                 — keychain open / unlock / search failed
 *                      (errSec*)
 *  -2                 — entry not found for this SSID (the
 *                      operator has not added a
 *                      `security add-generic-password` item for
 *                      this network)
 *  -3                 — buffer too small (*inout_password_len holds
 *                      the required size on return; no bytes copied)
 */
int AgentLookupProjectPSK(const uint8_t *ssid, size_t ssid_len,
                          uint8_t *out_password,
                          size_t *inout_password_len);

#endif /* AIRPORTITLWMAGENT_KEYCHAIN_H */
