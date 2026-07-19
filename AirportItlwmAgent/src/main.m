/*
 * AirportItlwmAgent — project-owned PLTI PMK producer
 * (root LaunchDaemon).
 *
 * The userland half of the project-owned WPA2 PSK PMK delivery
 * pipeline. The Apple producer path that would normally feed an
 * external supplicant on Tahoe is unreachable for project
 * helpers:
 *
 *   - the CoreLocationd CWLocationClient join-started event path
 *     gates on the `com.apple.wifi.events.private` HARD
 *     entitlement at airportd's __verifyEntitlementForEventType:
 *     (eventType 0x6d for autoJoinDidStart);
 *   - an ad-hoc-codesigned project helper cannot claim that
 *     private entitlement;
 *   - no replay/late-subscriber path exists in any of the 22
 *     CWXPCSubsystem helper-class init bodies, so a missed
 *     pre-first-M1 subscribe edge cannot be recovered by
 *     waiting.
 *
 * The helper opens the project-owned 'PLTI' user client on
 * AirportItlwm and runs a strict producer pipeline driven
 * entirely by the kext-side PSK association-start edge:
 *
 *     loop {
 *       WaitAssociationTarget(conn, last_acked) -> tgt
 *           (blocks under the kext command gate until a new PSK
 *           association edge publishes a target);
 *       open /Library/Keychains/AirportItlwm.keychain and unlock
 *           it with the install-time random password read from
 *           /etc/airportitlwm/keychain-password (mode 0600,
 *           root:wheel; bytes scrubbed from the helper stack
 *           immediately after SecKeychainUnlock returns);
 *       lookup by service "AirportItlwm WiFi PSK" and
 *           account = SSID -> passphrase (zeroed immediately
 *           after PMK derivation);
 *       PBKDF2-HMAC-SHA1(passphrase, ssid, 4096) -> 32-byte PMK;
 *       DeliverPMK(conn, generation=tgt.generation, pmk32)
 *           (kext validates generation_echo == pending generation
 *           before writing ic_psk; mismatch = kIOReturnNotPermitted);
 *       zero pmk32;
 *       last_acked = tgt.generation;
 *     }
 *
 * No CWWiFiClient delegate, CoreWLAN, airportd, eventType 0x6d,
 * private entitlement, or codesign-bypass path is used.
 *
 * Secret-handling discipline:
 *   - passphrase and pmk32 are stack buffers, explicitly
 *     memset(0)'d as soon as DeliverPMK returns or the path
 *     errors out;
 *   - no PSK/PMK/passphrase bytes are ever logged;
 *   - SSID lengths are logged but SSID bytes themselves are not
 *     printed (SSIDs are visible in any beacon scan, but
 *     omitting them keeps the helper log free of CONTROL_STA_
 *     NETWORK aliases on the lab path).
 */

#import <Foundation/Foundation.h>
#include "assoc_target.h"
#include "userclient.h"
#include "keychain.h"
#include "wpa_key.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Maximum reasonable PSK/Passphrase length. WPA2 PSK passphrase
 * is 8..63 ASCII bytes per IEEE 802.11-2016; some networks store
 * a 64-hex-byte pre-derived PSK string, so a 128-byte cap is a
 * generous upper bound that still fits comfortably on the stack
 * and bounds the buffer-too-small failure path. */
#define kAgentPassphraseMaxLen 128u
#define kAgentPLTIRetrySleepSeconds 1u
#define kAgentPLTIRetryLogEvery 10u

static bool
agent_target_uses_psk_pmk(const struct AirportItlwmAssociationTarget *tgt)
{
    if (tgt == NULL)
        return false;
    return AirportItlwmAgentTargetUsesPskPmk(tgt->authtype_upper);
}

static void
agent_zero(void *p, size_t n)
{
    /*
     * memset_s is not available in macOS userspace headers; a
     * volatile-pointer memset hand-roll is the canonical
     * "compiler must not optimize this away" pattern for
     * scrubbing secret-bearing memory before the buffer goes out
     * of scope.
     */
    volatile uint8_t *v = (volatile uint8_t *)p;
    while (n--)
        *v++ = 0;
}

static int
agent_handle_target(io_connect_t conn,
                    const struct AirportItlwmAssociationTarget *tgt)
{
    uint8_t passphrase[kAgentPassphraseMaxLen];
    size_t  passphrase_len = sizeof(passphrase);
    uint8_t pmk32[32];
    int     rc;

    AGENT_LOG("handle_target ENTRY generation=%llu ssid_len=%u "
              "authtype_lower=0x%x authtype_upper=0x%x",
              (unsigned long long)tgt->generation, tgt->ssid_len,
              tgt->authtype_lower, tgt->authtype_upper);

    /*
     * Do not turn a WPA3-only password into a WPA2 PBKDF2 PMK.  A future SAE
     * producer needs an explicitly versioned password ingress and its own
     * authenticated exchange; version-1 PLTI must fail closed instead.
     */
    if (!agent_target_uses_psk_pmk(tgt)) {
        AGENT_ERR("handle_target REJECT_NON_PSK generation=%llu "
                  "authtype_upper=0x%x",
                  (unsigned long long)tgt->generation,
                  tgt->authtype_upper);
        return -1;
    }

    int kc = AgentLookupProjectPSK(tgt->ssid, tgt->ssid_len,
                                  passphrase, &passphrase_len);
    if (kc != 0) {
        AGENT_LOG("handle_target NO_CREDENTIAL generation=%llu "
                  "ssid_len=%u rc=%d",
                  (unsigned long long)tgt->generation,
                  tgt->ssid_len, kc);
        agent_zero(passphrase, sizeof(passphrase));
        return kc;
    }

    rc = AgentDerivePMK_PBKDF2(passphrase, passphrase_len,
                               tgt->ssid, tgt->ssid_len,
                               pmk32);
    agent_zero(passphrase, sizeof(passphrase));
    if (rc != 0) {
        AGENT_ERR("handle_target derivation FAILED generation=%llu rc=%d",
                  (unsigned long long)tgt->generation, rc);
        agent_zero(pmk32, sizeof(pmk32));
        return rc;
    }

    kern_return_t kr = AgentDeliverPMK(conn, tgt->generation, pmk32);
    agent_zero(pmk32, sizeof(pmk32));
    if (kr != kIOReturnSuccess) {
        AGENT_ERR("handle_target DeliverPMK FAILED generation=%llu kr=0x%x",
                  (unsigned long long)tgt->generation, (unsigned)kr);
        return -1;
    }
    AGENT_LOG("handle_target DONE generation=%llu",
              (unsigned long long)tgt->generation);
    return 0;
}

static void
agent_prime_keychain_for_first_target(void)
{
    int rc = AgentPrimeProjectKeychain();
    if (rc != 0) {
        AGENT_ERR("AgentPrimeProjectKeychain failed rc=%d; "
                  "association lookup will retry", rc);
    }
}

static kern_return_t
agent_open_plti_blocking(io_connect_t *out_conn)
{
    if (out_conn == NULL)
        return kIOReturnBadArgument;
    *out_conn = MACH_PORT_NULL;

    uint32_t attempt = 0;
    for (;;) {
        kern_return_t kr = AgentOpenPLTIQuiet(out_conn);
        if (kr == kIOReturnSuccess) {
            if (attempt != 0) {
                AGENT_LOG("PLTI user client opened after %u retries",
                          attempt);
            }
            return kr;
        }

        attempt++;
        if (attempt == 1 || (attempt % kAgentPLTIRetryLogEvery) == 0) {
            AGENT_LOG("AgentOpenPLTI unavailable kr=0x%x; retrying "
                      "in %u s", (unsigned)kr,
                      (unsigned)kAgentPLTIRetrySleepSeconds);
        }
        sleep(kAgentPLTIRetrySleepSeconds);
    }
}

int
main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    @autoreleasepool {
        AGENT_LOG("project-owned PLTI PMK producer starting "
                  "(uid=%u)", (unsigned)getuid());

        /*
         * Pay Tahoe's cold securityd / keychain unlock cost before
         * the first PSK association target can start the kext's
         * pre-M1 PMK wait window. Lookup still unlocks defensively
         * on each association edge, so this is a latency warmup, not
         * a credential cache.
         */
        agent_prime_keychain_for_first_target();

        for (;;) {
            io_connect_t conn = MACH_PORT_NULL;
            kern_return_t kr = agent_open_plti_blocking(&conn);
            if (kr != kIOReturnSuccess) {
                AGENT_ERR("agent_open_plti_blocking failed kr=0x%x",
                          (unsigned)kr);
                sleep(kAgentPLTIRetrySleepSeconds);
                continue;
            }

            uint64_t last_acked = 0;
            for (;;) {
                struct AirportItlwmAssociationTarget tgt;
                kr = AgentWaitAssociationTarget(conn, last_acked, &tgt);
                if (kr == kIOReturnAborted) {
                    AGENT_LOG("WaitAssociationTarget aborted; closing "
                              "connection and reopening PLTI in-process");
                    break;
                }
                if (kr != kIOReturnSuccess) {
                    AGENT_ERR("WaitAssociationTarget kr=0x%x; closing "
                              "connection and reopening PLTI in-process",
                              (unsigned)kr);
                    break;
                }
                (void)agent_handle_target(conn, &tgt);
                /*
                 * Always advance last_acked even if the credential
                 * lookup or PMK delivery failed. Otherwise the helper
                 * would re-block on the same generation indefinitely
                 * and never observe the next association edge. A
                 * missing credential is not the kext's problem to
                 * solve; the helper logs it and moves on. The kext-
                 * side replay guard guarantees that an unrelated
                 * later DeliverPMK cannot retroactively land for the
                 * skipped generation.
                 */
                last_acked = tgt.generation;
            }

            if (conn != MACH_PORT_NULL)
                IOServiceClose(conn);
            sleep(kAgentPLTIRetrySleepSeconds);
        }
    }
    return 0;
}
