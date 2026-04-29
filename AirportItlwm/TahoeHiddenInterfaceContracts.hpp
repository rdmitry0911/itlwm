//
//  TahoeHiddenInterfaceContracts.hpp
//  AirportItlwm
//

#ifndef TahoeHiddenInterfaceContracts_hpp
#define TahoeHiddenInterfaceContracts_hpp

#include <stdint.h>

namespace TahoeHiddenInterfaceContracts {

static constexpr uint32_t kCoreHiddenInterfaceOffset = 0x1510;

static constexpr uint32_t kFlowIdSupportedVtableOffset = 0xa68;
static constexpr uint32_t kRequestFlowQueueVtableOffset = 0xa70;
static constexpr uint32_t kReleaseFlowQueueVtableOffset = 0xa78;
static constexpr uint32_t kBaseRequestFlowQueueVtableOffset = 0xd60;
static constexpr uint32_t kBaseReleaseFlowQueueVtableOffset = 0xd68;

static constexpr uint32_t kFlowMetadataAddressOffset = 0x06;
static constexpr uint32_t kFlowMetadataServiceClassOffset = 0x0c;
static constexpr uint32_t kFlowMetadataFlagsOffset = 0x10;

static constexpr uint32_t kTimestampBaseEnableVtableOffset = 0xd90;
static constexpr uint32_t kTimestampBaseDisableVtableOffset = 0xd98;
static constexpr uint32_t kTimestampHiddenEnableVtableOffset = 0xaa8;
static constexpr uint32_t kTimestampHiddenDisableVtableOffset = 0xab0;

static constexpr uint32_t kLogPipeOwnerOffset = 0x88;
static constexpr uint32_t kEventPipeOffset = 0x218;
static constexpr uint32_t kLogPipeOffset = 0x220;
static constexpr uint32_t kSnapshotPipeOffset = 0x230;

static constexpr uint32_t kCreateVirtualInterfaceBaseOffset = 0xe10;
static constexpr uint32_t kEnableVirtualInterfaceBaseOffset = 0xd40;
static constexpr uint32_t kDisableVirtualInterfaceBaseOffset = 0xd48;
static constexpr uint32_t kProximityOwnerOffset = 0x2c28;
static constexpr uint32_t kProximityRole = 0x06;
static constexpr uint32_t kProximityWakeFlag = 0x10000;
static constexpr uint32_t kVirtualInterfaceNullStatus = 0xe00002bc;

static_assert(kLogPipeOffset - kEventPipeOffset == 0x08,
              "Apple hidden interface pipe order is event/log/snapshot");
static_assert(kSnapshotPipeOffset - kLogPipeOffset == 0x10,
              "Apple hidden interface snapshot pipe offset mismatch");
static_assert(kReleaseFlowQueueVtableOffset -
                  kFlowIdSupportedVtableOffset == 0x10,
              "Apple hidden flow queue vtable slot spacing mismatch");

} // namespace TahoeHiddenInterfaceContracts

#endif /* TahoeHiddenInterfaceContracts_hpp */
