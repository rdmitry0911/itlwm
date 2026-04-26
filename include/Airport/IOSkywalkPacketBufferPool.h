//
//  IOSkywalkPacketBufferPool.h
//  itlwm
//
//  Created by qcwap on 2023/6/15.
//  Copyright © 2023 钟先耀. All rights reserved.
//

#ifndef IOSkywalkPacketBufferPool_h
#define IOSkywalkPacketBufferPool_h
#define _IOSKYWALKPACKETBUFFERPOOL_H

#include <IOKit/IOService.h>

class IOSkywalkMemorySegment;
class IOSkywalkMemorySegmentDescriptor;
class IOSkywalkPacket;
class IOSkywalkPacketBuffer;
class IOSkywalkPacketDescriptor;
class IOSkywalkPacketBufferDescriptor;

class IOSkywalkPacketBufferPool : public OSObject {
    OSDeclareDefaultStructors(IOSkywalkPacketBufferPool)
    
public:
    struct PoolOptions {
        uint32_t packetCount;
        uint32_t bufferCount;
        uint32_t bufferSize;
        uint32_t maxBuffersPerPacket;
        uint32_t memorySegmentSize;
        uint32_t poolFlags;
        uint64_t pad;
    };
    
public:
    virtual void free() APPLE_KEXT_OVERRIDE;
    virtual bool initWithName(char const*,void *,uint,IOSkywalkPacketBufferPool::PoolOptions const*);
    virtual bool initWithName(char const*,OSObject *,uint,IOSkywalkPacketBufferPool::PoolOptions const*);
    // Tahoe BootKC returns IOReturn for the pool operations below.  Keep the
    // legacy two-argument allocatePacket slot so the declared vtable order stays
    // aligned with the system class.
    virtual IOReturn allocatePacket(IOSkywalkPacket **,uint);
    virtual IOReturn allocatePacket(uint,IOSkywalkPacket **,uint);
    virtual IOReturn allocatePackets(uint,uint *,IOSkywalkPacket **,uint);
    virtual IOReturn deallocatePacket(IOSkywalkPacket *);
    virtual IOReturn deallocatePackets(IOSkywalkPacket **,uint);
    virtual IOReturn deallocatePacketList(IOSkywalkPacket *);
    virtual IOReturn deallocatePacketChain(unsigned long long);
    virtual IOReturn allocatePacketBuffer(IOSkywalkPacketBuffer **,uint);
    virtual IOReturn allocatePacketBuffers(uint *,IOSkywalkPacketBuffer **,uint);
    virtual IOReturn deallocatePacketBuffer(IOSkywalkPacketBuffer *);
    virtual IOReturn deallocatePacketBuffers(IOSkywalkPacketBuffer **,uint);
    virtual IOReturn newPacket(IOSkywalkPacketDescriptor *,IOSkywalkPacket **);
    virtual IOReturn newPacketBuffer(IOSkywalkPacketBufferDescriptor *,IOSkywalkPacketBuffer **);
    virtual IOReturn newMemorySegment(IOSkywalkMemorySegmentDescriptor *,IOSkywalkMemorySegment **);
    
public:
    static IOSkywalkPacketBufferPool *withName(char const*,OSObject *,uint,IOSkywalkPacketBufferPool::PoolOptions const*);
    
public:
    uint8_t filter[0xB8];
};

#endif /* IOSkywalkPacketBufferPool_h */
