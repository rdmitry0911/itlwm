//
//  TahoeAssociationAuthContracts.hpp
//  AirportItlwm
//

#ifndef TahoeAssociationAuthContracts_hpp
#define TahoeAssociationAuthContracts_hpp

#include <stdint.h>

namespace TahoeAssociationAuthContracts {

static constexpr uint32_t kAuthWpa = 1U << 0;
static constexpr uint32_t kAuthWpaPsk = 1U << 1;
static constexpr uint32_t kAuthWpa2 = 1U << 2;
static constexpr uint32_t kAuthWpa2Psk = 1U << 3;
static constexpr uint32_t kAuthSha256Psk = 1U << 10;
static constexpr uint32_t kAuthSha2568021x = 1U << 11;
static constexpr uint32_t kAuthWpa3Sae = 1U << 12;
static constexpr uint32_t kAuthWpa3FtSae = 1U << 13;
static constexpr uint32_t kAuthWpa3Enterprise = 1U << 14;
static constexpr uint32_t kAuthWpa3FtEnterprise = 1U << 15;

static constexpr uint32_t kWpaProtocolAuthMask =
    kAuthWpa |
    kAuthWpaPsk |
    kAuthWpa2 |
    kAuthWpa2Psk |
    kAuthSha256Psk |
    kAuthSha2568021x;

static constexpr uint32_t kPskAuthMask =
    kAuthWpaPsk |
    kAuthWpa2Psk |
    kAuthSha256Psk;

static constexpr uint32_t kLegacyPskAuthMask =
    kAuthWpaPsk |
    kAuthWpa2Psk;

static constexpr uint32_t kEnterpriseAuthMask =
    kAuthWpa |
    kAuthWpa2 |
    kAuthSha2568021x;

static constexpr uint32_t kWpa3OnlyAuthMask =
    kAuthWpa3Sae |
    kAuthWpa3FtSae |
    kAuthWpa3Enterprise |
    kAuthWpa3FtEnterprise;

/* The sole explicitly permitted WPA3 transition representation for fallback. */
static constexpr uint32_t kAuditedWpa3PskTransitionAuth =
    kAuthWpa3Sae | kAuthWpa2Psk;

inline bool usesLocalWpaProtocol(uint32_t authtypeUpper)
{
    return (authtypeUpper & kWpaProtocolAuthMask) != 0;
}

inline bool usesLocalPskAkm(uint32_t authtypeUpper)
{
    return (authtypeUpper & kPskAuthMask) != 0;
}

inline bool usesLocalLegacyPskAkm(uint32_t authtypeUpper)
{
    return (authtypeUpper & kLegacyPskAuthMask) != 0;
}

inline bool usesLocalSha256PskAkm(uint32_t authtypeUpper)
{
    return (authtypeUpper & kAuthSha256Psk) != 0;
}

inline bool usesLocalEnterpriseAkm(uint32_t authtypeUpper)
{
    return (authtypeUpper & kEnterpriseAuthMask) != 0;
}

inline bool isWpa3OnlyAuth(uint32_t authtypeUpper)
{
    /*
     * Strict identity-only diagnostic predicate. Association ingress uses
     * requiresUnsupportedWpa3Auth() below, which is deliberately stricter:
     * it also rejects WPA3 vectors with extra unrecognised bits.
     */
    return authtypeUpper != 0 &&
           (authtypeUpper & ~kWpa3OnlyAuthMask) == 0 &&
           (authtypeUpper & kWpa3OnlyAuthMask) != 0;
}

inline bool isAuditedWpa3PskTransition(uint32_t authtypeUpper)
{
    return authtypeUpper == kAuditedWpa3PskTransitionAuth;
}

inline bool requiresUnsupportedWpa3Auth(uint32_t authtypeUpper)
{
    /*
     * Preserve only the explicitly permitted WPA3-SAE|WPA2-PSK transition
     * carrier. Arbitrary additional bits would allow an unsupported
     * SAE/FT/enterprise carrier to enter the legacy Open-System path or the
     * WPA2 PMK helper. SAE and WPA3 enterprise both require
     * capabilities this driver has quarantined (SAE and PMF/IGTK).
     */
    return (authtypeUpper & kWpa3OnlyAuthMask) != 0 &&
           !isAuditedWpa3PskTransition(authtypeUpper);
}

inline bool mayUseLocalPskPmk(uint32_t authtypeUpper)
{
    /*
     * Version-1 PLTI transports a WPA/WPA2 PBKDF2 PMK.  It may preserve the
     * one explicitly permitted SAE transition carrier above, but must reject any other
     * WPA3-containing vector rather than silently treating one PSK bit as an
     * authorization to derive a WPA2 PMK.
     */
    if ((authtypeUpper & kWpa3OnlyAuthMask) != 0)
        return isAuditedWpa3PskTransition(authtypeUpper);
    return usesLocalPskAkm(authtypeUpper);
}

inline uint32_t localAuthMaskWithoutFallbackRewrite(uint32_t authtypeUpper)
{
    return authtypeUpper & kWpaProtocolAuthMask;
}

} // namespace TahoeAssociationAuthContracts

#endif /* TahoeAssociationAuthContracts_hpp */
