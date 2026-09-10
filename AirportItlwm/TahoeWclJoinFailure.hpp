#ifndef TahoeWclJoinFailure_hpp
#define TahoeWclJoinFailure_hpp

#include <stdint.h>
#include <string.h>
#include <net80211/ieee80211_join_attempt.h>

/* The including controller supplies the real apple80211 carrier declarations.
 * These conversions describe public results, never a Broadcom command or an
 * Intel errno masquerading as a received IEEE status. */
namespace TahoeWclJoinFailure {

struct Payloads {
    apple80211_wcl_auth_assoc_complete_event authAssoc;
    apple80211_wcl_first_beacon_event firstBeacon;
    apple80211_wcl_connect_complete_event connect;
    bool publishAuthAssoc;
    bool publishFirstBeacon;
};

inline uint32_t rawStatus(const ieee80211_join_phase_result &result)
{
    if (!result.seen || result.cause == IEEE80211_JOIN_FAILURE_NONE)
        return 0;
    switch (result.cause) {
    case IEEE80211_JOIN_FAILURE_TIMEOUT: return 2;
    case IEEE80211_JOIN_FAILURE_NO_NETWORKS: return 3;
    case IEEE80211_JOIN_FAILURE_NO_ACK: return 5;
    default: return 1;
    }
}

inline uint32_t mappedStatus(const ieee80211_join_phase_result &result)
{
    const uint32_t status = rawStatus(result);
    return status != 0 ? 0xe0820400U | status : 0;
}

inline uint32_t mappedPeerStatus(const ieee80211_join_phase_result &result)
{
    if (!result.seen || result.cause != IEEE80211_JOIN_FAILURE_PEER_STATUS)
        return 0;
    return result.peer_status <= 0x44 ?
        0xe0821000U | result.peer_status : 0xe3ff8100U;
}

inline bool build(const ieee80211_join_failure &failure, Payloads *out)
{
    if (out == nullptr || failure.generation == 0 ||
        !ieee80211_join_attempt_bssid_valid(failure.bssid) ||
        failure.ssid_len == 0 || failure.ssid_len > sizeof(failure.ssid) ||
        failure.phase < IEEE80211_JOIN_DISCOVERY ||
        failure.phase > IEEE80211_JOIN_KEYS || !failure.terminal.seen ||
        failure.terminal.cause == IEEE80211_JOIN_FAILURE_NONE ||
        failure.terminal.cause > IEEE80211_JOIN_FAILURE_PEER_REASON)
        return false;

    uint16_t status;
    uint16_t secondary = 0xffff;
    const bool peerLeft = failure.terminal.cause == IEEE80211_JOIN_FAILURE_PEER_REASON;
    const uint32_t raw = rawStatus(failure.terminal);
    if (peerLeft) {
        /* Core sends its separate received-deauth/disassoc bulletin, then
         * terminates JoinAdapter with the received reason (12 if zero).
         * It does not synthesize AUTH/ASSOC or SET_SSID phase events. */
        status = failure.terminal.peer_reason != 0 ? failure.terminal.peer_reason : 12;
    } else if (failure.phase == IEEE80211_JOIN_KEYS) {
        /* Supplicant terminal is independent of AUTH/ASSOC/SET_SSID.
         * JoinAdapter maps failed key completion to 1014; a real handshake
         * timeout additionally carries secondary 1005. It sends only 0xd5,
         * retaining the preceding successful ASSOC record. */
        status = 1014;
        if (failure.terminal.cause == IEEE80211_JOIN_FAILURE_TIMEOUT)
            secondary = 1005;
    } else {
        status = raw == 1 || raw == 2 ? 16 : 1;
        if (failure.terminal.cause == IEEE80211_JOIN_FAILURE_PEER_STATUS)
            status = failure.terminal.peer_status;
        if (failure.phase == IEEE80211_JOIN_DISCOVERY) {
            if (raw == 3)
                secondary = 1000;
        } else if (raw == 2) {
            secondary = failure.phase == IEEE80211_JOIN_AUTH ? 1002 : 1004;
        } else if (raw == 5) {
            secondary = failure.phase == IEEE80211_JOIN_AUTH ? 1001 : 1003;
        }
    }
    if (status == 0)
        return false;

    memset(out, 0, sizeof(*out));
    out->publishAuthAssoc = !peerLeft && (failure.phase == IEEE80211_JOIN_AUTH ||
        failure.phase == IEEE80211_JOIN_ASSOC);
    out->publishFirstBeacon = !peerLeft && failure.phase != IEEE80211_JOIN_KEYS;
    auto &auth = out->authAssoc;
    auth.status = status;
    auth.secondary_state = secondary;
    auth.auth_seen = failure.auth.seen;
    memcpy(auth.bssid, failure.bssid, sizeof(auth.bssid));
    auth.auth_status = mappedStatus(failure.auth);
    auth.auth_reason = mappedPeerStatus(failure.auth);
    auth.assoc_status = mappedStatus(failure.assoc);
    auth.assoc_reason = mappedPeerStatus(failure.assoc);

    auto &beacon = out->firstBeacon;
    beacon.status = status;
    beacon.secondary_state = secondary;
    memcpy(beacon.bssid, failure.bssid, sizeof(beacon.bssid));
    /* The completed lower join reports failure after a prior AUTH/ASSOC
     * error. Preserve that earlier specific overall result; do not claim
     * another received authentication/association response. */
    beacon.event_status = failure.phase == IEEE80211_JOIN_DISCOVERY ?
        mappedStatus(failure.terminal) : 0xe0820401U;

    auto &connect = out->connect;
    connect.status = status;
    connect.reason = secondary;
    memcpy(connect.records[0].bssid, failure.bssid,
        sizeof(connect.records[0].bssid));
    connect.records[0].status = auth.assoc_status;
    connect.records[0].reason = auth.assoc_reason;
    return true;
}

} // namespace TahoeWclJoinFailure
#endif
