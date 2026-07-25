/*
 * Read-only, identity-free WCL multi-BSS observation for Tahoe 25C56.
 *
 * This program neither initiates nor alters a scan, association, radio state,
 * saved profile, Keychain item, AP, route, or driver property.  It observes
 * the recovered WCL join-manager and final driver-carrier edges.  The first
 * candidate identity is retained only as two in-kernel comparison words for
 * the lifetime of one join ordinal; it is never formatted, copied out, or
 * persisted.  Output is bounded to categorical lifecycle counters and the
 * boolean fact that two final carriers in one WCL join selected different
 * identities.
 */

#pragma D option quiet
#pragma D option bufsize=4m

inline int WCL_CANDIDATE_COUNT_OFF = 0x218;
inline int WCL_FIRST_CANDIDATE_OFF = 0x220;

dtrace:::BEGIN
{
    join_ordinal = 0;
    driver_carriers = 0;
    orphan_carriers = 0;
    candidate_zero = 0;
    candidate_one = 0;
    candidate_multiple = 0;
    selected_carriers = 0;
    same_join_multiple_carriers = 0;
    same_join_distinct_selected = 0;
    get_candidates = 0;
    send_candidate = 0;
    join_abort_req = 0;
    join_abort_complete = 0;
    join_timeout = 0;
    join_assoc_complete = 0;
    join_connect_complete = 0;
    trace_errors = 0;
}

/* A new join bounds the only interval in which two final carriers can be
 * compared.  A later join is a fresh upper-owner request, not a replacement
 * claim for this trace. */
fbt:com.apple.iokit.IO80211Family:_ZN14WCLJoinManager17handleJoinRequestEPv:entry
{
    join_ordinal++;
    active_join_ordinal = join_ordinal;
    active_carrier_count = 0;
    active_first_valid = 0;
}

fbt:com.apple.iokit.IO80211Family:_ZN24WCLJoinCandidateSelector21getJoinCandidatesListEP14WCLJoinRequestR27apple80211_low_latency_info:entry
{
    get_candidates++;
}

fbt:com.apple.iokit.IO80211Family:_ZN14WCLJoinManager27handleSendCandidateToDriverEPv:entry
{
    send_candidate++;
}

fbt:com.apple.iokit.IO80211Family:_ZN14WCLJoinManager18handleJoinAbortReqEPv:entry
{
    join_abort_req++;
}

fbt:com.apple.iokit.IO80211Family:_ZN14WCLJoinManager23handleJoinAbortCompleteEPv:entry
{
    join_abort_complete++;
}

fbt:com.apple.iokit.IO80211Family:_ZN14WCLJoinManager13handleTimeoutEPv:entry
{
    join_timeout++;
}

fbt:com.apple.iokit.IO80211Family:_ZN14WCLJoinManager11timeoutJoinEP18IO80211TimerSource:entry
{
    join_timeout++;
}

fbt:com.apple.iokit.IO80211Family:_ZN14WCLJoinManager23handleJoinAssocCompleteEPv:entry
{
    join_assoc_complete++;
}

fbt:com.apple.iokit.IO80211Family:_ZN14WCLJoinManager25handleJoinConnectCompleteEPv:entry
{
    join_connect_complete++;
}

/* Candidate count remains an admission category only.  The observed first
 * record is never treated as proof of a list stride, cursor, retry, or roam
 * policy. */
fbt:com.zxystd.AirportItlwm:_ZN28AirportItlwmSkywalkInterface16setWCL_ASSOCIATEEP25apple80211AssocCandidates:entry
/arg1 != 0/
{
    this->carrier = (uintptr_t)arg1;
    this->count = *(uint32_t *)(this->carrier + WCL_CANDIDATE_COUNT_OFF);
    driver_carriers++;

    if (active_join_ordinal == 0) {
        orphan_carriers++;
    } else if (this->count == 0) {
        candidate_zero++;
    } else {
        if (this->count == 1)
            candidate_one++;
        else
            candidate_multiple++;

        /* Keep exactly the recovered first candidate in probe-local state.
         * These values are compared only, never emitted. */
        this->first_word = *(uint32_t *)(
            this->carrier + WCL_FIRST_CANDIDATE_OFF);
        this->second_word = *(uint16_t *)(
            this->carrier + WCL_FIRST_CANDIDATE_OFF + 4);
        selected_carriers++;
        if (active_first_valid == 0) {
            active_first_word = this->first_word;
            active_second_word = this->second_word;
            active_first_valid = 1;
        } else {
            if (active_carrier_count == 1)
                same_join_multiple_carriers++;
            if (active_first_word != this->first_word ||
                active_second_word != this->second_word)
                same_join_distinct_selected = 1;
        }
        active_carrier_count++;
    }
}

dtrace:::ERROR
{
    trace_errors++;
}

dtrace:::END
{
    printf("WCL_MULTIBSSID_AGGREGATE v=1 joins=%llu driver_carriers=%llu orphan_carriers=%llu candidate_zero=%llu candidate_one=%llu candidate_multiple=%llu selected_carriers=%llu same_join_multiple_carriers=%llu same_join_distinct_selected=%u get_candidates=%llu send_candidate=%llu join_abort_req=%llu join_abort_complete=%llu join_timeout=%llu join_assoc_complete=%llu join_connect_complete=%llu trace_errors=%llu\n",
           (uint64_t)join_ordinal, (uint64_t)driver_carriers,
           (uint64_t)orphan_carriers, (uint64_t)candidate_zero,
           (uint64_t)candidate_one, (uint64_t)candidate_multiple,
           (uint64_t)selected_carriers, (uint64_t)same_join_multiple_carriers,
           same_join_distinct_selected != 0 ? 1U : 0U,
           (uint64_t)get_candidates, (uint64_t)send_candidate,
           (uint64_t)join_abort_req, (uint64_t)join_abort_complete,
           (uint64_t)join_timeout, (uint64_t)join_assoc_complete,
           (uint64_t)join_connect_complete, (uint64_t)trace_errors);
}
