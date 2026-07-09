//
//  AirportItlwmCountryCode.hpp
//  itlwm
//

#ifndef AirportItlwmCountryCode_hpp
#define AirportItlwmCountryCode_hpp

#include "HAL/ItlHalService.hpp"
#include "Airport/apple80211_var.h"

namespace AirportItlwmCountryCode {

inline bool hasAlpha2(const char *cc)
{
    return cc != nullptr && cc[0] != '\0' && cc[1] != '\0';
}

inline bool isFallbackCountry(const char *cc)
{
    if (!hasAlpha2(cc))
        return true;

    return (cc[0] == 'Z' && cc[1] == 'Z') ||
           cc[0] == 'X' || cc[0] == 'x';
}

inline bool isValid80211dAlpha2(const uint8_t *cc)
{
    return cc != nullptr &&
           cc[0] >= 'A' && cc[0] <= 'Z' &&
           cc[1] >= 'A' && cc[1] <= 'Z' &&
           !(cc[0] == 'Z' && cc[1] == 'Z') &&
           cc[0] != 'X';
}

inline void copyAlpha2(uint8_t out[APPLE80211_MAX_CC_LEN], const char *cc)
{
    if (out == nullptr)
        return;

    out[0] = 'Z';
    out[1] = 'Z';
    out[2] = '\0';

    if (!hasAlpha2(cc))
        return;

    out[0] = static_cast<uint8_t>(cc[0]);
    out[1] = static_cast<uint8_t>(cc[1]);
}

inline const char *localePropertyString(uint32_t locale)
{
    switch (locale) {
        case APPLE80211_LOCALE_FCC:
            return "FCC";
        case APPLE80211_LOCALE_ETSI:
            return "ETSI";
        case APPLE80211_LOCALE_JAPAN:
            return "Japan";
        case APPLE80211_LOCALE_KOREA:
            return "Korea";
        case APPLE80211_LOCALE_APAC:
            return "APAC";
        case APPLE80211_LOCALE_ROW:
            return "RoW";
        case APPLE80211_LOCALE_INDONESIA:
            return "Indonesia";
        default:
            return "Unknown";
    }
}

inline bool copyCurrentBss80211dCountry(
    ItlHalService *halService,
    uint8_t out[APPLE80211_MAX_CC_LEN])
{
    if (out == nullptr)
        return false;

    out[0] = '\0';
    out[1] = '\0';
    out[2] = '\0';

    if (halService == nullptr)
        return false;

    struct ieee80211com *ic = halService->get80211Controller();
    if (ic == nullptr || ic->ic_state != IEEE80211_S_RUN ||
        ic->ic_bss == nullptr)
        return false;

    const struct ieee80211_node *ni = ic->ic_bss;
    const uint8_t *ies = ni->ni_rsnie_tlv;
    uint32_t iesLen = ni->ni_rsnie_tlv_len;
    if (ies == nullptr || iesLen < 5)
        return false;

    for (uint32_t off = 0; off + 2 <= iesLen;) {
        const uint8_t elemId = ies[off];
        const uint32_t elemLen = ies[off + 1];
        const uint32_t next = off + 2 + elemLen;
        if (next > iesLen)
            break;

        if (elemId == IEEE80211_ELEMID_COUNTRY && elemLen >= 3) {
            const uint8_t *alpha2 = ies + off + 2;
            if (isValid80211dAlpha2(alpha2)) {
                out[0] = alpha2[0];
                out[1] = alpha2[1];
                out[2] = '\0';
                return true;
            }
        }

        off = next;
    }

    return false;
}

inline void selectCountryCode(
    ItlHalService *halService,
    const char *userOverrideCc,
    const char *firmwareCc,
    const char *geoLocationCc,
    uint8_t out[APPLE80211_MAX_CC_LEN])
{
    uint8_t currentBssCc[APPLE80211_MAX_CC_LEN];
    const char *selectedCc = "ZZ";
    const bool hasCurrentBssCountry =
        copyCurrentBss80211dCountry(halService, currentBssCc);

    if (hasAlpha2(userOverrideCc)) {
        selectedCc = userOverrideCc;
    } else if (!isFallbackCountry(firmwareCc)) {
        selectedCc = firmwareCc;
    } else if (hasCurrentBssCountry) {
        selectedCc = reinterpret_cast<const char *>(currentBssCc);
    } else if (hasAlpha2(geoLocationCc)) {
        selectedCc = geoLocationCc;
    } else if (hasAlpha2(firmwareCc)) {
        selectedCc = firmwareCc;
    }

    copyAlpha2(out, selectedCc);
}

} // namespace AirportItlwmCountryCode

#endif /* AirportItlwmCountryCode_hpp */
