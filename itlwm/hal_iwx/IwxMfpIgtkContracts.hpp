/*
 * Exact AX211 API-68 management-integrity-key carrier contract.
 *
 * This header intentionally models only the packed payload and the local
 * eligibility inputs.  The actual softc/config identity checks remain in
 * ItlIwx.cpp, where the two AX211 configuration objects are available.
 */

#ifndef IwxMfpIgtkContracts_hpp
#define IwxMfpIgtkContracts_hpp

#include <stddef.h>
#include <stdint.h>

namespace IwxMfpIgtkContracts {

static constexpr uint8_t kAx211FirmwareApi = 68;
static constexpr uint32_t kFirmwareMfpFlag = 1U << 2;
static constexpr uint32_t kMultiQueueRxCapability = 68;
static constexpr uint16_t kIgtkInstallCipher = 2U;
static constexpr uint16_t kIgtkDeleteFlag = 1U << 11;
static constexpr uint32_t kMgmtMcastKeyCommand = 0x1f;
static constexpr uint32_t kStationId = 0;
static constexpr uint32_t kFirstIgtkKeyId = 4;
static constexpr uint32_t kLastIgtkKeyId = 5;
static constexpr size_t kIgtkKeyLength = 16;

struct MgmtMcastKeyCommandV2 {
    uint32_t ctrl_flags;
    uint8_t igtk[32];
    uint32_t key_id;
    uint32_t sta_id;
    uint64_t receive_seq_cnt;
} __attribute__((packed));

static_assert(sizeof(MgmtMcastKeyCommandV2) == 0x34,
              "AX211 API-68 IGTK v2 command must be 0x34 bytes");
static_assert(offsetof(MgmtMcastKeyCommandV2, igtk) == 0x04,
              "AX211 API-68 IGTK bytes begin at +0x04");
static_assert(offsetof(MgmtMcastKeyCommandV2, key_id) == 0x24,
              "AX211 API-68 IGTK key id is at +0x24");
static_assert(offsetof(MgmtMcastKeyCommandV2, sta_id) == 0x28,
              "AX211 API-68 IGTK station id is at +0x28");
static_assert(offsetof(MgmtMcastKeyCommandV2, receive_seq_cnt) == 0x2c,
              "AX211 API-68 IGTK receive sequence is at +0x2c");

inline bool hasValidIgtkShape(uint32_t key_id, size_t key_len)
{
    return key_id >= kFirstIgtkKeyId && key_id <= kLastIgtkKeyId &&
           key_len == kIgtkKeyLength;
}

inline bool hasExactAbiPrerequisites(uint8_t firmware_api,
                                     uint32_t firmware_flags,
                                     bool has_multi_queue_rx)
{
    return firmware_api == kAx211FirmwareApi &&
           (firmware_flags & kFirmwareMfpFlag) != 0 &&
           has_multi_queue_rx;
}

} // namespace IwxMfpIgtkContracts

#endif /* IwxMfpIgtkContracts_hpp */
