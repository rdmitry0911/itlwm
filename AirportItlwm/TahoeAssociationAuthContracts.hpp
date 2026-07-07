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

static constexpr uint32_t kEnterpriseAuthMask =
    kAuthWpa |
    kAuthWpa2 |
    kAuthSha2568021x;

static constexpr uint32_t kWpa3OnlyAuthMask =
    kAuthWpa3Sae |
    kAuthWpa3FtSae |
    kAuthWpa3Enterprise |
    kAuthWpa3FtEnterprise;

inline bool usesLocalWpaProtocol(uint32_t authtypeUpper)
{
    return (authtypeUpper & kWpaProtocolAuthMask) != 0;
}

inline bool usesLocalPskAkm(uint32_t authtypeUpper)
{
    return (authtypeUpper & kPskAuthMask) != 0;
}

inline bool usesLocalEnterpriseAkm(uint32_t authtypeUpper)
{
    return (authtypeUpper & kEnterpriseAuthMask) != 0;
}

inline bool isWpa3OnlyAuth(uint32_t authtypeUpper)
{
    return authtypeUpper != 0 &&
           (authtypeUpper & ~kWpa3OnlyAuthMask) == 0 &&
           (authtypeUpper & kWpa3OnlyAuthMask) != 0;
}

inline uint32_t localAuthMaskWithoutFallbackRewrite(uint32_t authtypeUpper)
{
    return authtypeUpper & kWpaProtocolAuthMask;
}

} // namespace TahoeAssociationAuthContracts

#endif /* TahoeAssociationAuthContracts_hpp */
