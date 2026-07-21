//
//  TahoeExternalPmkScanResumeContracts.hpp
//  AirportItlwm
//
//  A pure policy predicate for the WCL external-PMK handoff.  Keeping this
//  decision independent of IOKit/net80211 makes every acceptance condition
//  unit-testable without serialising credentials or network identities.
//

#ifndef TahoeExternalPmkScanResumeContracts_hpp
#define TahoeExternalPmkScanResumeContracts_hpp

namespace TahoeExternalPmkScanResumeContracts {

struct Facts {
    bool pskPmkPolicyAllowed;
    bool associationAccepted;
    bool observedExternalPmkReady;
    bool stateIsScan;
    bool pskFlagSet;
    bool externalPmkOwner;
};

/*
 * A WCL association may arrive after the scan that populated the BSS tree has
 * already completed.  Resume only that normal SCAN state machine edge after
 * the paired PLTI wait observes PMK readiness.  This deliberately does not
 * select a BSS, enter AUTH, enqueue a management frame, or broaden the PSK
 * policy (and therefore cannot enable pure SAE).
 */
constexpr bool shouldResumeScanAfterExternalPmk(const Facts &facts)
{
    return facts.pskPmkPolicyAllowed &&
           facts.associationAccepted &&
           facts.observedExternalPmkReady &&
           facts.stateIsScan &&
           facts.pskFlagSet &&
           !facts.externalPmkOwner;
}

} // namespace TahoeExternalPmkScanResumeContracts

#endif /* TahoeExternalPmkScanResumeContracts_hpp */
