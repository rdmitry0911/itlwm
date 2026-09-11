#ifndef ItlScanCommandPolicy_hpp
#define ItlScanCommandPolicy_hpp

#include <ClientKit/AirportItlwmScanHomeAwayBridge.h>
#include "ItlStateTransitionLease.hpp"

/* Include after net80211's complete declarations. This is host-only request
 * storage, never a firmware ABI. One value survives version dispatch and
 * command allocation; no nested builder may reread mutable WCL policy. */
struct ItlScanCommandPolicy {
    struct ieee80211_wcl_scan_plan plan;
    uint32_t homeAwayMs;
    ItlStateTransitionIdentity identity;
    uint64_t stateSerial;
    uint64_t joinGeneration;
    uint64_t reassocSerial;

    /* All *Locked methods require the selected-BSS leaf. Callers which
     * also hold the HAL scan leaf always acquire it second. No callback,
     * allocation or firmware operation occurs in these value helpers. */
    static ItlStateTransitionIdentity identityLocked(const struct ieee80211com *ic)
    {
        ItlStateTransitionIdentity result = {};
        if (ic->ic_opmode == IEEE80211_M_STA) {
            result.joinSequence = ic->ic_wcl_join_attempt.next_generation;
            if (ic->ic_wcl_join_attempt.phase != IEEE80211_JOIN_IDLE)
                result.joinGeneration = ic->ic_wcl_join_attempt.result.generation;
            result.associationEpoch = ic->ic_pae_assoc_epoch;
        }
        return result;
    }

    static uint32_t homeAwayTime()
    {
        uint32_t value = 0;
        return airportItlwmGetScanHomeAwayTime(&value) ? value : 120U;
    }

    static bool captureIngressLocked(const struct ieee80211com *ic,
                                    uint64_t wclGeneration,
                                    ItlStateTransitionRequest *request)
    {
        request->scanGeneration = wclGeneration;
        if (wclGeneration != 0)
            return true; // Exact WCL value is copied by physical admission.
        const struct ieee80211_join_attempt &attempt = ic->ic_wcl_join_attempt;
        const bool freshJoin =
            !__atomic_load_n(&ic->ic_initial_scan_census_only, __ATOMIC_ACQUIRE) &&
            attempt.phase == IEEE80211_JOIN_DISCOVERY &&
            attempt.result.association_epoch == 0 &&
            attempt.result.generation == request->identity.joinGeneration;
        request->scanJoinGeneration = freshJoin ? request->identity.joinGeneration : 0;
        const int length = freshJoin ? attempt.result.ssid_len : ic->ic_des_esslen;
        if (length < 0 || static_cast<unsigned>(length) > sizeof(request->scanSsid))
            return false;
        request->scanSsidLength = static_cast<uint8_t>(length);
        if (length != 0)
            memcpy(request->scanSsid,
                freshJoin ? attempt.result.ssid : ic->ic_des_essid, length);
        return true;
    }

    static int captureOwnedLocked(struct ieee80211com *ic,
        uint64_t wclGeneration, const ItlStateTransitionRequest *request,
        ItlScanCommandPolicy *policy)
    {
        *policy = ItlScanCommandPolicy{};
        policy->identity = identityLocked(ic);
        if (request != NULL && !request->identity.equals(policy->identity))
            return ECANCELED;
        if (request != NULL && request->scanGeneration != wclGeneration)
            return ECANCELED;
        if (wclGeneration != 0) {
            const struct ieee80211_wcl_scan_plan &plan = ic->ic_wcl_scan_plan;
            if (plan.active == 0 || plan.generation != wclGeneration)
                return ECANCELED;
            policy->plan = plan;
        } else if (request != NULL) {
            if (request->scanSsidLength > sizeof(policy->plan.ssid))
                return EINVAL;
            const struct ieee80211_join_attempt &attempt = ic->ic_wcl_join_attempt;
            if (request->scanJoinGeneration != 0 &&
                (request->scanJoinGeneration != policy->identity.joinGeneration ||
                 __atomic_load_n(&ic->ic_initial_scan_census_only, __ATOMIC_ACQUIRE) ||
                 attempt.phase != IEEE80211_JOIN_DISCOVERY ||
                 attempt.result.association_epoch != 0))
                return ECANCELED;
            policy->plan.ssid_len = request->scanSsidLength;
            memcpy(policy->plan.ssid, request->scanSsid, sizeof(policy->plan.ssid));
            policy->joinGeneration = request->scanJoinGeneration;
        } else {
            if (ic->ic_des_esslen < 0 ||
                static_cast<unsigned>(ic->ic_des_esslen) > sizeof(policy->plan.ssid))
                return EINVAL;
            policy->plan.ssid_len = ic->ic_des_esslen;
            memcpy(policy->plan.ssid, ic->ic_des_essid, policy->plan.ssid_len);
        }
        policy->stateSerial = request != NULL ? request->serial : 0;
        return 0;
    }

    bool currentLocked(const struct ieee80211com *ic) const
    {
        if (reassocSerial != 0 && (!ic->ic_wcl_reassoc_owner_active ||
            ic->ic_wcl_reassoc_owner_serial != reassocSerial))
            return false;
        if (!identity.equals(identityLocked(ic)))
            return false;
        if (plan.active != 0 && (ic->ic_wcl_scan_plan.active == 0 ||
            ic->ic_wcl_scan_plan.generation != plan.generation))
            return false;
        if (joinGeneration != 0 &&
            (__atomic_load_n(&ic->ic_initial_scan_census_only, __ATOMIC_ACQUIRE) ||
             ic->ic_wcl_join_attempt.phase != IEEE80211_JOIN_DISCOVERY ||
             ic->ic_wcl_join_attempt.result.association_epoch != 0))
            return false;
        return true;
    }

};

#endif
