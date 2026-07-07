/*
 * AirportItlwmAgent — PLTI user client wrapper.
 *
 * Pure C implementation; no Foundation or CoreWLAN dependency. The
 * apple80211_key carrier is the existing PLTI DeliverPMK contract
 * (key_len=32, key_cipher_type=APPLE80211_CIPHER_PMK,
 * key_flags=APPLE80211_KEY_FLAG_UNICAST). The generation echo is
 * passed as the scalar argument so the kext-side target-identity
 * replay guard can match it against the currently published
 * pending generation.
 */
#include "userclient.h"
#include "log.h"

#include <IOKit/IOKitLib.h>
#include <stdbool.h>
#include <string.h>

/*
 * Self-contained mirror of struct apple80211_key from
 * MacKernelSDK/Headers/IOKit/80211/apple80211_var.h. The full SDK
 * header is written for C++/Objective-C++ consumers (bare
 * apple80211_channel type names without a struct tag); the helper
 * is plain C, so we inline only the carrier fields actually used
 * by the PLTI DeliverPMK sink, with a build-time _Static_assert
 * that pins the byte size to the value the kext sees at its
 * checkStructureInputSize == sizeof(struct apple80211_key)
 * dispatch check. The kext-side layout is published in
 * MacKernelSDK and unchanged by this patch; this mirror's size MUST
 * stay equal to that header's sizeof.
 */
#define APPLE80211_KEY_BUFF_LEN        32
#define APPLE80211_RSC_LEN             8
#define APPLE80211_KEY_FLAG_UNICAST    0x1
#define APPLE80211_CIPHER_PMK          6

struct apple80211_key {
    uint32_t  version;
    uint32_t  key_len;
    uint32_t  key_cipher_type;
    uint16_t  key_flags;
    uint16_t  key_index;
    uint8_t   key[APPLE80211_KEY_BUFF_LEN];
    uint8_t   pad[30];
    uint32_t  key_rsc_len;
    uint8_t   key_rsc[APPLE80211_RSC_LEN];
    uint8_t   key_ea[6];            /* mirrors struct ether_addr */
    uint32_t  wowl_kck_len;         /* SDK uses 'uint' (== unsigned int == uint32_t) */
    uint8_t   wowl_kck_key[16];
    uint32_t  wowl_kek_len;
    uint8_t   wowl_kek_key[24];
};

_Static_assert(sizeof(struct apple80211_key) == 148,
               "apple80211_key carrier size must match the kext "
               "MacKernelSDK layout (148 bytes on x86_64)");

/*
 * APPLE80211_CIPHER_PMK is the cipher-type tag the kext-side sink
 * validates before writing ic_psk. The helper does not pull the
 * MacKernelSDK headers, so the constant value is carried locally;
 * the integer is fixed by Apple's apple80211_cipher_type enum.
 */
#ifndef APPLE80211_CIPHER_PMK
#define APPLE80211_CIPHER_PMK 6
#endif

static kern_return_t
agent_open_plti_impl(io_connect_t *out_conn, bool log_failures)
{
    if (out_conn == NULL)
        return kIOReturnBadArgument;
    *out_conn = MACH_PORT_NULL;

    io_iterator_t iter = IO_OBJECT_NULL;
    kern_return_t kr = IOServiceGetMatchingServices(
        kIOMainPortDefault,
        IOServiceMatching("AirportItlwm"),
        &iter);
    if (kr != kIOReturnSuccess || iter == IO_OBJECT_NULL) {
        if (log_failures)
            AGENT_ERR("AgentOpenPLTI IOServiceGetMatchingServices kr=0x%x",
                      kr);
        return (kr != kIOReturnSuccess) ? kr : kIOReturnNotFound;
    }

    io_service_t svc = IOIteratorNext(iter);
    IOObjectRelease(iter);
    if (svc == IO_OBJECT_NULL) {
        if (log_failures)
            AGENT_ERR("AgentOpenPLTI no AirportItlwm service registered");
        return kIOReturnNotFound;
    }

    io_connect_t conn = MACH_PORT_NULL;
    kr = IOServiceOpen(svc, mach_task_self(),
                       kAirportItlwmUserClientType, &conn);
    IOObjectRelease(svc);
    if (kr != kIOReturnSuccess) {
        if (log_failures)
            AGENT_ERR("AgentOpenPLTI IOServiceOpen('PLTI') kr=0x%x", kr);
        return kr;
    }

    *out_conn = conn;
    AGENT_LOG("PLTI user client opened conn=0x%x", conn);
    return kIOReturnSuccess;
}

kern_return_t
AgentOpenPLTI(io_connect_t *out_conn)
{
    return agent_open_plti_impl(out_conn, true);
}

kern_return_t
AgentOpenPLTIQuiet(io_connect_t *out_conn)
{
    return agent_open_plti_impl(out_conn, false);
}

kern_return_t
AgentWaitAssociationTarget(io_connect_t conn,
                           uint64_t last_acked,
                           struct AirportItlwmAssociationTarget *out_target)
{
    if (out_target == NULL || conn == MACH_PORT_NULL)
        return kIOReturnBadArgument;
    memset(out_target, 0, sizeof(*out_target));

    uint64_t scalar_in[1] = { last_acked };
    size_t   struct_out_sz = sizeof(*out_target);

    kern_return_t kr = IOConnectCallMethod(
        conn,
        kAirportItlwmUserClientMethod_WaitAssociationTarget,
        scalar_in, 1,
        NULL, 0,
        NULL, NULL,
        out_target, &struct_out_sz);
    if (kr != kIOReturnSuccess) {
        AGENT_LOG("WaitAssociationTarget kr=0x%x (likely aborted on "
                  "kext teardown)", kr);
        return kr;
    }
    if (struct_out_sz != sizeof(*out_target) ||
        out_target->version != kAirportItlwmAssocTargetVersion) {
        AGENT_ERR("WaitAssociationTarget malformed reply size=%zu "
                  "version=%u expected size=%zu version=%u",
                  struct_out_sz, out_target->version,
                  sizeof(*out_target),
                  (unsigned)kAirportItlwmAssocTargetVersion);
        return kIOReturnInternalError;
    }
    AGENT_LOG("WaitAssociationTarget OK generation=%llu ssid_len=%u "
              "authtype_upper=0x%x",
              (unsigned long long)out_target->generation,
              out_target->ssid_len, out_target->authtype_upper);
    return kIOReturnSuccess;
}

kern_return_t
AgentDeliverPMK(io_connect_t conn,
                uint64_t generation,
                const uint8_t pmk32[32])
{
    if (conn == MACH_PORT_NULL || pmk32 == NULL)
        return kIOReturnBadArgument;

    struct apple80211_key key;
    memset(&key, 0, sizeof(key));
    key.version          = 1;
    key.key_len          = 32;
    key.key_cipher_type  = APPLE80211_CIPHER_PMK;
    key.key_flags        = APPLE80211_KEY_FLAG_UNICAST;
    /*
     * key_index is the 16-bit per-PTK index in Apple's standard
     * usage. The 64-bit generation echo travels through the
     * scalar argument; key_index is left zero to avoid colliding
     * with that field's documented meaning.
     */
    key.key_index = 0;
    memcpy(key.key, pmk32, 32);

    uint64_t scalar_in[1] = { generation };
    kern_return_t kr = IOConnectCallMethod(
        conn,
        kAirportItlwmUserClientMethod_DeliverPMK,
        scalar_in, 1,
        &key, sizeof(key),
        NULL, NULL,
        NULL, NULL);

    /*
     * Zero the local key struct as soon as the call returns so the
     * 32-byte PMK does not sit in the helper's stack longer than
     * necessary. The caller owns pmk32 zeroing.
     */
    memset(&key, 0, sizeof(key));

    if (kr != kIOReturnSuccess) {
        AGENT_ERR("DeliverPMK generation=%llu kr=0x%x",
                  (unsigned long long)generation, kr);
        return kr;
    }
    AGENT_LOG("DeliverPMK OK generation=%llu",
              (unsigned long long)generation);
    return kIOReturnSuccess;
}
