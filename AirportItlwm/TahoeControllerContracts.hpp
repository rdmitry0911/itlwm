//
//  TahoeControllerContracts.hpp
//  AirportItlwm
//

#ifndef TahoeControllerContracts_hpp
#define TahoeControllerContracts_hpp

#include <stdint.h>

namespace TahoeControllerContracts {

static constexpr uint32_t kActionFramePoolCapacity = 0x100;
static constexpr uint32_t kIO80211DefaultDataQueueDepth = 0x400;
static constexpr uint16_t kAppleDataQueueDepthDefault = 0x200;
static constexpr uint32_t kDataQueueDepthOffset = 0x1154;

static constexpr uint32_t kPromiscuousModeOffset = 0x4778;
static constexpr uint32_t kMulticastRejectGateOffset = 0x2891;
static constexpr uint8_t kMulticastRejectGateBit = 0x80;
static constexpr uint32_t kMulticastCountOffset = 0x234;
static constexpr uint32_t kMulticastListOffset = 0x238;
static constexpr uint32_t kMulticastAddressLength = 6;
static constexpr uint32_t kMulticastMaxEntries = 0x20;
static constexpr uint32_t kMulticastPayloadBaseLength = 4;
static constexpr uint32_t kMulticastPayloadCapacity = 0xca;
static constexpr uint8_t kMulticastPayloadFill = 0xaa;
static constexpr const char *kMulticastIovar = "mcast_list";

static constexpr uint32_t kUnsupportedStatus = 0xe00002c7;
static constexpr uint32_t kErrorStatus = 0xe00002bc;
static constexpr uint32_t kCommandRejectStatus = 0xe0823804;

static constexpr const char *kIoServicePlane = "IOService";
static constexpr const char *kCoalesceQSizeProperty = "wlan.coalesce.qsize";
static constexpr const char *kCoalesceTimeoutProperty = "wlan.coalesce.timeout";

static_assert(kMulticastMaxEntries * kMulticastAddressLength == 0xc0,
              "Apple multicast list stores 0x20 6-byte addresses");
static_assert(kMulticastPayloadBaseLength +
                  kMulticastMaxEntries * kMulticastAddressLength <=
              kMulticastPayloadCapacity,
              "Apple multicast IOVAR payload fits in 0xca bytes");

} // namespace TahoeControllerContracts

#endif /* TahoeControllerContracts_hpp */
