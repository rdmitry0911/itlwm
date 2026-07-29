//
//  TahoeWclOpenScanResumeContracts.hpp
//  AirportItlwm
//
//  Pure admission policy for resuming an ordinary WCL open-network join.
//  Open networks have no PMK handoff, so they cannot use the external-PMK
//  scan-resume predicate.  Keeping this decision separate makes the exact
//  Open/None/no-key/no-RSN boundary testable without any network identity.
//

#ifndef TahoeWclOpenScanResumeContracts_hpp
#define TahoeWclOpenScanResumeContracts_hpp

namespace TahoeWclOpenScanResumeContracts {

struct Facts {
    bool associationAccepted;
    bool infrastructureMode;
    bool openAuthLower;
    bool noUpperAuth;
    bool noCredential;
    bool noRsnIe;
    bool stateIsScan;
    bool hasSelectedCandidate;
    bool selectedBssidRenderable;
};

/*
 * Resume only the normal net80211 SCAN edge for an exact infrastructure
 * open-network candidate.  A WEP key, RSN IE, upper authentication selector,
 * absent candidate, malformed BSSID, failed policy setup, or later state all
 * fail closed.  This predicate never selects a node, enters AUTH, or sends a
 * management frame itself.
 */
constexpr bool shouldResumeScanAfterOpenAssociation(const Facts &facts)
{
    return facts.associationAccepted &&
           facts.infrastructureMode &&
           facts.openAuthLower &&
           facts.noUpperAuth &&
           facts.noCredential &&
           facts.noRsnIe &&
           facts.stateIsScan &&
           facts.hasSelectedCandidate &&
           facts.selectedBssidRenderable;
}

} // namespace TahoeWclOpenScanResumeContracts

#endif /* TahoeWclOpenScanResumeContracts_hpp */
