#ifndef TahoeBssManagerContracts_hpp
#define TahoeBssManagerContracts_hpp

#include <stddef.h>
#include <stdint.h>

namespace TahoeBssManagerContracts {

static constexpr size_t kBssManagerObjectSize = 0x18;
static constexpr size_t kBssBeaconObjectSize = 0x18;
static constexpr size_t kBeaconMetaDataSize = 0x44;
static constexpr size_t kBeaconIeCapacity = 0x800;
static constexpr size_t kBeaconPayloadSize =
    kBeaconMetaDataSize + kBeaconIeCapacity;
static constexpr uint32_t kBaseCurrentBssStateFeatureGate = 0x60;
static constexpr uint32_t kCurrentBssPrivateStateMask = 0x01;
static constexpr uint32_t kCurrentBssChannelMessage = 0x52;
static constexpr uint32_t kSsidBytesPresentFlags = 0x06;

// BeaconMetaData consumed by IO80211BSSBeacon::setBeaconDataFromMsg().
// The same carrier is returned by the reference getWCL_BSS_INFO path.
struct BeaconMetaData {
    uint32_t ieLength;             // 0x00
    uint16_t channelSpec;          // 0x04
    uint8_t ssid[32];              // 0x06
    uint8_t ssidLength;            // 0x26
    uint8_t primaryChannel;        // 0x27
    uint8_t reserved28;            // 0x28
    uint8_t bssid[6];              // 0x29
    uint8_t reserved2f;            // 0x2f
    int32_t rssi;                  // 0x30
    int16_t noise;                 // 0x34
    int16_t snr;                   // 0x36
    uint16_t beaconInterval;       // 0x38
    uint16_t capability;           // 0x3a
    uint32_t reserved3c;           // 0x3c
    uint32_t flags;                // 0x40
} __attribute__((packed));

struct BeaconPayload {
    BeaconMetaData meta;
    uint8_t ie[kBeaconIeCapacity];
} __attribute__((packed));

static_assert(sizeof(BeaconMetaData) == kBeaconMetaDataSize,
              "BeaconMetaData must match the Tahoe 0x44 carrier");
static_assert(kBssManagerObjectSize == 0x18 &&
                  kBssBeaconObjectSize == 0x18,
              "framework BSS object sizes must match their metaclasses");
static_assert(kBaseCurrentBssStateFeatureGate < 0x80,
              "BSS current-state feature gate must fit the core bitmap");
static_assert(offsetof(BeaconMetaData, ssidLength) == 0x26,
              "BeaconMetaData SSID length offset mismatch");
static_assert(offsetof(BeaconMetaData, bssid) == 0x29,
              "BeaconMetaData BSSID offset mismatch");
static_assert(offsetof(BeaconMetaData, rssi) == 0x30,
              "BeaconMetaData RSSI offset mismatch");
static_assert(offsetof(BeaconMetaData, flags) == 0x40,
              "BeaconMetaData flags offset mismatch");
static_assert(sizeof(BeaconPayload) == kBeaconPayloadSize,
              "BeaconPayload must match the Tahoe 0x844 carrier");

} // namespace TahoeBssManagerContracts

#endif /* TahoeBssManagerContracts_hpp */
