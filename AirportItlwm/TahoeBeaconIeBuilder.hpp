#ifndef TAHOE_BEACON_IE_BUILDER_HPP
#define TAHOE_BEACON_IE_BUILDER_HPP

#include <stdint.h>
#include <string.h>

namespace TahoeBeaconIeBuilder {

static constexpr uint8_t kElementIdSsid = 0;
static constexpr uint8_t kElementIdTim = 5;
static constexpr uint8_t kDefaultDtimPeriod = 1;

inline bool containsElement(const uint8_t *ies, uint32_t iesLen, uint8_t elementId)
{
    if (ies == nullptr)
        return false;

    for (uint32_t off = 0; off + 2U <= iesLen;) {
        const uint32_t elemLen = ies[off + 1U];
        const uint32_t next = off + 2U + elemLen;
        if (next > iesLen)
            break;
        if (ies[off] == elementId)
            return true;
        off = next;
    }

    return false;
}

inline uint32_t appendElement(uint8_t *dst,
                              uint32_t capacity,
                              uint32_t offset,
                              uint8_t elementId,
                              const uint8_t *payload,
                              uint8_t payloadLen)
{
    if (dst == nullptr || offset > capacity)
        return offset;

    const uint32_t totalLen = 2U + payloadLen;
    if (capacity - offset < totalLen)
        return offset;

    dst[offset++] = elementId;
    dst[offset++] = payloadLen;
    if (payloadLen != 0 && payload != nullptr) {
        memcpy(dst + offset, payload, payloadLen);
        offset += payloadLen;
    }

    return offset;
}

inline uint32_t appendTaggedIeTail(uint8_t *dst,
                                   uint32_t capacity,
                                   uint32_t offset,
                                   const uint8_t *ies,
                                   uint32_t iesLen)
{
    if (dst == nullptr || ies == nullptr || offset > capacity)
        return offset;

    for (uint32_t off = 0; off + 2U <= iesLen;) {
        const uint32_t elemLen = ies[off + 1U];
        const uint32_t totalLen = 2U + elemLen;
        const uint32_t next = off + totalLen;
        if (next > iesLen)
            break;
        if (capacity - offset < totalLen)
            break;

        memcpy(dst + offset, ies + off, totalLen);
        offset += totalLen;
        off = next;
    }

    return offset;
}

inline uint32_t buildCurrentBssIeStream(const uint8_t *ssid,
                                        uint8_t ssidLen,
                                        uint8_t dtimCount,
                                        uint8_t dtimPeriod,
                                        const uint8_t *rawTail,
                                        uint32_t rawTailLen,
                                        uint8_t *dst,
                                        uint32_t capacity)
{
    if (dst == nullptr)
        return 0;

    const bool rawHasSsid =
        containsElement(rawTail, rawTailLen, kElementIdSsid);
    const bool rawHasTim =
        containsElement(rawTail, rawTailLen, kElementIdTim);

    if (rawHasSsid && rawHasTim)
        return appendTaggedIeTail(dst, capacity, 0, rawTail, rawTailLen);

    uint32_t offset = 0;
    if (!rawHasSsid)
        offset = appendElement(dst, capacity, offset, kElementIdSsid,
                               ssid, ssidLen);

    if (!rawHasTim) {
        const uint8_t tim[] = {
            dtimCount,
            dtimPeriod != 0 ? dtimPeriod : kDefaultDtimPeriod,
            0,
            0,
        };
        offset = appendElement(dst, capacity, offset, kElementIdTim,
                               tim, static_cast<uint8_t>(sizeof(tim)));
    }

    return appendTaggedIeTail(dst, capacity, offset, rawTail, rawTailLen);
}

} // namespace TahoeBeaconIeBuilder

#endif // TAHOE_BEACON_IE_BUILDER_HPP
