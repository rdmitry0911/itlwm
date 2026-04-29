#ifndef IO80211NetworkPacket_h
#define IO80211NetworkPacket_h

#include "IOSkywalkNetworkPacket.h"

enum IO80211NetworkTXStatus : UInt32;

// Tahoe IO80211Family exports IO80211NetworkPacket as the Wi-Fi packet class
// consumed by IO80211InfraInterface/PeerManager.  The implementation and
// storage live in IO80211Family; local code mirrors the exported packet
// surface so later Apple packet subclasses can be declared against the same
// base ABI.
class IO80211NetworkPacket : public IOSkywalkNetworkPacket
{
    OSDeclareDefaultStructors(IO80211NetworkPacket)

public:
    // BootKC exports IO80211NetworkPacket::getPacketType as the
    // NON-const-mangled `__ZN20IO80211NetworkPacket13getPacketTypeEv`
    // (the parent IOSkywalkNetworkPacket::getPacketType is const at a
    // SEPARATE vtable slot). Keep the non-const declaration to match
    // BootKC layout — this is a new virtual, not an override.
    virtual UInt32 getPacketType();
    virtual void *getVirtualAddress();
    virtual void setPTMMode(bool enabled);
    virtual void setIngressEgressTimestamp(UInt64 timestamp);
    virtual UInt64 getIngressEgressTimestamp() const;
    virtual bool isPTMMode() const;
    virtual void setPktEnqueueTime(UInt64 timestamp);
    virtual UInt64 getPktEnqueueTime() const;
    virtual IO80211NetworkTXStatus
    firmwareToHostTxStatus(IO80211NetworkTXStatus status);
    virtual void setFirmwareTxStatus(IO80211NetworkTXStatus status);
    virtual IO80211NetworkTXStatus getFirmwareTxStatus();
    virtual UInt32 getBufferSize();
    virtual IOReturn prepareWithQueue(IOSkywalkPacketQueue *queue,
                                      UInt32 options);
    virtual IOReturn prepareWithQueue(IOSkywalkPacketQueue *queue,
                                      IOSkywalkPacketDirection direction,
                                      IOOptionBits options) APPLE_KEXT_OVERRIDE;
};

static_assert(sizeof(IO80211NetworkPacket) == 0x78,
              "IO80211NetworkPacket must match Tahoe metaclass size 0x78");

#endif /* IO80211NetworkPacket_h */
