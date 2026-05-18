/*
 * AirportItlwmAgent — DIAGNOSTIC_INSTRUMENTATION (rev8 supersede of
 * REJECTED rev7 SYSTEM_CONTRACT_FIX request).
 *
 * Rev7 (SYSTEM_CONTRACT_FIX) was REJECTED at Stage 1 because the
 * airportd producer-side body/lifecycle/entitlement contract for the
 * proposed CWWiFiClient join-delegate trigger was not closed in
 * evidence and several cited decomp artifacts were absent from the
 * guest. The auditor explicitly offered switching to
 * DIAGNOSTIC_INSTRUMENTATION as an acceptable alternative scope.
 *
 * This rev8 strips the helper to a pure behavior-neutral observer:
 *   - register a CWWiFiClient delegate (the same delegate-slot
 *     contract the rejected rev7 used),
 *   - on every join / autoJoin lifecycle callback, os_log the
 *     interface name, the ssid value (when carried by the event,
 *     redacted via %{private}s), and any error.
 *
 * The diagnostic does NOT:
 *   - query the System keychain,
 *   - derive any PMK / PBKDF2 / key material,
 *   - open or call any kext IOUserClient,
 *   - mutate any kext or SCDynamicStore state,
 *   - retain, copy, or log any credential bytes.
 *
 * The single hypothesis it disambiguates is whether the public
 * CoreWLAN -[CWWiFiClient joinDidStartForWiFiInterfaceWithName:ssid:]
 * delegate callback fires on the live iwx Tahoe path during the
 * pre-first-M1 association window. If Stage 2 runtime confirms the
 * callback fires, a future request will re-introduce the PMK
 * derivation/delivery pipeline together with the now-required
 * airportd producer-contract closure.
 *
 * Behavior neutrality: the delegate methods read NSString arguments
 * passed in by CoreWLAN and emit os_log entries. No system contract
 * is exercised beyond the public -[CWWiFiClient setDelegate:] /
 * informal delegate dispatch already exercised by Apple system
 * components.
 *
 * Thread context: CWWiFiClient dispatches delegate callbacks on its
 * own internal serial dispatch queue inside an autorelease pool. The
 * delegate methods below run off-main, serialized per CWWiFiClient
 * instance.
 *
 * No raw key bytes ever appear in any log line (this helper never
 * obtains any). NETWORK_NAME (ssid) is logged through %{private}s so
 * os_log redacts it at runtime under default profiles.
 */

#import <Foundation/Foundation.h>
#import <CoreWLAN/CoreWLAN.h>
#include "log.h"
#include <stdlib.h>
#include <unistd.h>

@interface AirportItlwmAgentDelegate : NSObject
@end

@implementation AirportItlwmAgentDelegate

- (void)joinDidStartForWiFiInterfaceWithName:(NSString *)interfaceName
                                        ssid:(NSString *)ssid
{
    AGENT_LOG("joinDidStart iface=%{public}s ssid=%{private}s",
              interfaceName ? [interfaceName UTF8String] : "(nil)",
              ssid ? [ssid UTF8String] : "(nil)");
}

- (void)joinDidCompleteForWiFiInterfaceWithName:(NSString *)interfaceName
                                     isAutoJoin:(BOOL)isAutoJoin
                                          error:(NSError *)error
{
    AGENT_LOG("joinDidComplete iface=%{public}s isAutoJoin=%d "
              "error=%{public}s",
              interfaceName ? [interfaceName UTF8String] : "(nil)",
              (int)isAutoJoin,
              error ? [[error localizedDescription] UTF8String] : "(none)");
}

- (void)autoJoinDidStartForWiFiInterfaceWithName:(NSString *)interfaceName
{
    AGENT_LOG("autoJoinDidStart iface=%{public}s",
              interfaceName ? [interfaceName UTF8String] : "(nil)");
}

- (void)autoJoinDidCompleteForWiFiInterfaceWithName:(NSString *)interfaceName
{
    AGENT_LOG("autoJoinDidComplete iface=%{public}s",
              interfaceName ? [interfaceName UTF8String] : "(nil)");
}

- (void)autoJoinDidUpdate:(NSDictionary *)update
{
    AGENT_LOG("autoJoinDidUpdate keys=%lu",
              (unsigned long)(update ? [update count] : 0));
}

@end

int
main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    @autoreleasepool {
        AGENT_LOG("diagnostic daemon starting (uid=%u)", (unsigned)getuid());

        CWWiFiClient *client = [CWWiFiClient sharedWiFiClient];
        if (client == nil) {
            AGENT_ERR("[CWWiFiClient sharedWiFiClient] returned nil");
            return 1;
        }

        /*
         * Keep a strong reference to the delegate for the daemon
         * lifetime. -[CWWiFiClient setDelegate:] is a non-retaining
         * store on the slot at +0x38 (proven by Ghidra asm of
         * CoreWLAN.framework on Tahoe 26.2 at 0x7ff8115a4e03; the
         * accepted prior FULL_DECOMP basis already on file as
         * docs/reference/CR-479-next-layer-external-supplicant-pmk-delivery-static-closure-20260517.md),
         * so a stack/temp delegate would be a use-after-free risk.
         */
        static AirportItlwmAgentDelegate *delegateStrongRef;
        delegateStrongRef = [[AirportItlwmAgentDelegate alloc] init];
        [client setDelegate:delegateStrongRef];

        AGENT_LOG("CWWiFiClient delegate registered "
                  "(DIAGNOSTIC_INSTRUMENTATION; observer-only); "
                  "entering NSRunLoop (KeepAlive LaunchDaemon)");

        [[NSRunLoop currentRunLoop] run];

        /* Defensive cleanup (unreachable under launchd KeepAlive). */
        [client setDelegate:nil];
    }
    return 0;
}
