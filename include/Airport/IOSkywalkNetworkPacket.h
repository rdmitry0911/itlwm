#ifndef _IOSKYWALKNETWORKPACKET_H
#define _IOSKYWALKNETWORKPACKET_H

#include <IOKit/skywalk/IOSkywalkPacket.h>

class IOSkywalkNetworkPacket : public IOSkywalkPacket
{
    OSDeclareDefaultStructors(IOSkywalkNetworkPacket)

public:
    static IOSkywalkNetworkPacket *withPool(IOSkywalkPacketBufferPool *pool,
                                            IOSkywalkPacketDescriptor *desc,
                                            IOOptionBits options);
    virtual UInt32 getPacketType() const APPLE_KEXT_OVERRIDE;

    IOReturn setHeadroom(uint8_t headroom);
    uint8_t getHeadroom();
    IOReturn setLinkHeaderLength(uint8_t length);
    uint8_t getLinkHeaderLength();
    IOReturn setLinkHeaderOffset(uint32_t offset);
    IOReturn getLinkHeaderOffset(uint32_t *offset);
    IOReturn setNetworkHeaderOffset(uint32_t offset);
    IOReturn getNetworkHeaderOffset(uint32_t *offset);
    IOReturn setDataContainsFCS(bool contain);
    bool getDataContainsFCS();
    kern_packet_svc_class_t getServiceClass();

    IOReturn setTimestamp(AbsoluteTime timestamp);
    IOReturn getTimestamp(AbsoluteTime *timestamp);
    IOReturn clearTimestamp();
    bool isTimestampRequested();
    IOReturn setCompletionStatus(int status);
    IOReturn getExpiryTime(AbsoluteTime *time);

    IOReturn getTokenData(void *data, uint16_t *size);
    IOReturn getPacketID(packet_id_t *packetID);

    bool isPacketGroupStart();
    bool isPacketGroupEnd();
    bool isHighPriority();
    bool isTransportNewFlow();
    bool isTransportLastPacket();
    IOReturn setIsLinkBroadcast(bool broadcast);
    bool isLinkBroadcast();
    IOReturn setIsLinkMulticast(bool multicast);
    bool isLinkMulticast();
};

static_assert(sizeof(IOSkywalkNetworkPacket) == 0x78,
              "IOSkywalkNetworkPacket must match Tahoe metaclass size 0x78");

#endif
