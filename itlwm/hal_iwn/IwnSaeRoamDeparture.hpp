#ifndef IWN_SAE_ROAM_DEPARTURE_HPP
#define IWN_SAE_ROAM_DEPARTURE_HPP

#include <stdint.h>

/* One physical source-leave descriptor, not a second credential owner.
 * The HAL serializes this value state with the selected-BSS leaf and checks
 * its current source/roam lifecycle before each transition. */
struct IwnSaeRoamDepartureIdentity {
    uint64_t ticket;
    uint64_t association_epoch;
    uint64_t reassoc_serial;
    uint64_t source_generation;
    uint64_t join_sequence;
    uint64_t reassoc_sequence;
    uint8_t source_bssid[6];
    uint8_t target_bssid[6];
    uint8_t sta[6];
};

enum IwnSaeRoamDeparturePhase {
    IWN_SAE_ROAM_DEPARTURE_IDLE = 0,
    IWN_SAE_ROAM_DEPARTURE_ARMED,
    IWN_SAE_ROAM_DEPARTURE_DOORBELLED
};

struct IwnSaeRoamDepartureState {
    uint64_t next_ticket;
    IwnSaeRoamDepartureIdentity identity;
    IwnSaeRoamDeparturePhase phase;
};

static inline bool
iwn_sae_roam_departure_identity_equal(const IwnSaeRoamDepartureIdentity &a,
    const IwnSaeRoamDepartureIdentity &b)
{
    if (a.ticket == 0 || a.ticket != b.ticket ||
        a.association_epoch != b.association_epoch ||
        a.reassoc_serial != b.reassoc_serial ||
        a.source_generation != b.source_generation ||
        a.join_sequence != b.join_sequence ||
        a.reassoc_sequence != b.reassoc_sequence)
        return false;
    for (unsigned i = 0; i < 6; ++i) {
        if (a.source_bssid[i] != b.source_bssid[i] ||
            a.target_bssid[i] != b.target_bssid[i] || a.sta[i] != b.sta[i])
            return false;
    }
    return true;
}

static inline bool
iwn_sae_roam_departure_arm(IwnSaeRoamDepartureState &state,
    IwnSaeRoamDepartureIdentity &identity)
{
    bool source_nonzero = false, target_nonzero = false, sta_nonzero = false;
    bool different_bss = false;
    if (state.phase != IWN_SAE_ROAM_DEPARTURE_IDLE ||
        state.next_ticket == UINT64_MAX || identity.ticket != 0 ||
        identity.association_epoch == 0 || identity.reassoc_serial == 0 ||
        identity.source_generation == 0 || (identity.source_bssid[0] & 1) ||
        (identity.target_bssid[0] & 1) || (identity.sta[0] & 1))
        return false;
    for (unsigned i = 0; i < 6; ++i) {
        source_nonzero |= identity.source_bssid[i] != 0;
        target_nonzero |= identity.target_bssid[i] != 0;
        sta_nonzero |= identity.sta[i] != 0;
        different_bss |= identity.source_bssid[i] != identity.target_bssid[i];
    }
    if (!source_nonzero || !target_nonzero || !sta_nonzero || !different_bss)
        return false;
    identity.ticket = ++state.next_ticket;
    state.identity = identity;
    state.phase = IWN_SAE_ROAM_DEPARTURE_ARMED;
    return true;
}

static inline bool
iwn_sae_roam_departure_publish(IwnSaeRoamDepartureState &state,
    const IwnSaeRoamDepartureIdentity &identity)
{
    if (state.phase != IWN_SAE_ROAM_DEPARTURE_ARMED ||
        !iwn_sae_roam_departure_identity_equal(state.identity, identity))
        return false;
    state.phase = IWN_SAE_ROAM_DEPARTURE_DOORBELLED;
    return true;
}

static inline bool
iwn_sae_roam_departure_cancel(IwnSaeRoamDepartureState &state,
    const IwnSaeRoamDepartureIdentity &identity)
{
    if (state.phase == IWN_SAE_ROAM_DEPARTURE_IDLE ||
        !iwn_sae_roam_departure_identity_equal(state.identity, identity))
        return false;
    state.identity = IwnSaeRoamDepartureIdentity{};
    state.phase = IWN_SAE_ROAM_DEPARTURE_IDLE;
    return true;
}

static inline bool
iwn_sae_roam_departure_complete(IwnSaeRoamDepartureState &state,
    const IwnSaeRoamDepartureIdentity &identity)
{
    return state.phase == IWN_SAE_ROAM_DEPARTURE_DOORBELLED &&
        iwn_sae_roam_departure_cancel(state, identity);
}

#endif
