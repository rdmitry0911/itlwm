//
//  TahoeWclEnterpriseScanResumeContracts.hpp
//  AirportItlwm
//
//  Pure admission predicate for an 802.1X (WPA/WPA2 Enterprise) WCL
//  association.  It contains no IOKit, net80211, credential, or network
//  identity state so the exact fail-closed matrix can be tested on the host.
//

#ifndef TahoeWclEnterpriseScanResumeContracts_hpp
#define TahoeWclEnterpriseScanResumeContracts_hpp

namespace TahoeWclEnterpriseScanResumeContracts {

struct Facts {
    bool associationAccepted;
    bool infrastructureMode;
    bool openAuthLower;
    bool enterpriseAuthUpper;
    bool noCredential;
    bool hasRsnIe;
    bool stateIsScan;
    bool hasSelectedCandidate;
    bool selectedBssidRenderable;
};

/*
 * An 802.1X candidate carries no key: its PMK exists only after the EAP
 * conversation, which runs over the associated link and reaches the driver
 * later through APPLE80211_IOC_CIPHER_KEY.  The association itself therefore
 * starts as soon as the policy is in place, exactly as for an open candidate,
 * and needs the same WCL completion lease.  Resume only the normal net80211
 * SCAN edge for an exact infrastructure candidate with an Open-System lower
 * authentication, an enterprise-only upper selector and its RSN IE.  A key
 * carrier, PSK/SAE/unknown selector, absent candidate, malformed BSSID,
 * failed policy setup, or later state all fail closed.  This predicate never
 * selects a node, enters AUTH, or sends a management frame itself.
 */
constexpr bool shouldResumeScanAfterEnterpriseAssociation(const Facts &facts)
{
    return facts.associationAccepted &&
           facts.infrastructureMode &&
           facts.openAuthLower &&
           facts.enterpriseAuthUpper &&
           facts.noCredential &&
           facts.hasRsnIe &&
           facts.stateIsScan &&
           facts.hasSelectedCandidate &&
           facts.selectedBssidRenderable;
}

} // namespace TahoeWclEnterpriseScanResumeContracts

#endif /* TahoeWclEnterpriseScanResumeContracts_hpp */
