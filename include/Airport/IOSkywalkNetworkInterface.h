//
//  IOSkywalkNetworkInterface.h
//  itlwm
//
//  Created by qcwap on 2023/6/7.
//  Copyright © 2023 钟先耀. All rights reserved.
//

#ifndef IOSkywalkNetworkInterface_h
#define IOSkywalkNetworkInterface_h

#include <net/if.h>

#include "IOSkywalkInterface.h"

// if_link_status is a struct in the kernel (mangled as "14if_link_status"),
// not a typedef.  Using typedef UInt produces mangled "j" (unsigned int).
struct if_link_status {
    UInt status;
};
struct ifnet_traffic_descriptor_common;
class IOSkywalkPacketQueue;
class IOSkywalkLogicalLink;
class IOSkywalkPacketBufferPool;

class IOSkywalkNetworkInterface : public IOSkywalkInterface {
    OSDeclareAbstractStructors( IOSkywalkNetworkInterface )
    
public:
    struct RegistrationInfo {
        uint8_t pad[304];
    } __attribute__((packed));
    struct IOSkywalkTSOOptions;
    
public:
    virtual void free() APPLE_KEXT_OVERRIDE;
    virtual bool init(OSDictionary *) APPLE_KEXT_OVERRIDE;
    virtual void stop(IOService *) APPLE_KEXT_OVERRIDE;
    virtual void joinPMtree( IOService * driver ) APPLE_KEXT_OVERRIDE;
    virtual IOReturn setAggressiveness(
                                       unsigned long type,
                                       unsigned long newLevel ) APPLE_KEXT_OVERRIDE;
    virtual IOReturn enable(UInt) APPLE_KEXT_OVERRIDE;
    virtual IOReturn disable(UInt) APPLE_KEXT_OVERRIDE;
    // ---------------------------------------------------------------
    // Vtable verified 1:1 against IOSkywalkFamily.kext 26.3 (25D125)
    // by parsing __DATA,__const vtable at vmaddr 0x3e2b8 and resolving
    // LC_SYMTAB + LC_DYSYMTAB relocations.  Total: 334 virtual entries,
    // of which 41 methods + 10 reserved belong to IOSkywalkNetworkInterface.
    //
    // IOSkywalkNetworkInterface vtable offsets (dispatch, from vptr):
    //   0x8D0  registerNetworkInterfaceWithLogicalLink
    //   0x8D8  deregisterLogicalLink
    //   0x8E0  initBSDInterfaceParameters     (pure virtual)
    //   0x8E8  prepareBSDInterface
    //   0x8F0  finalizeBSDInterface
    //   0x8F8  getBSDInterface
    //   0x900  setBSDName
    //   0x908  getBSDName
    //   0x910  processBSDCommand
    //   0x918  processInterfaceCommand
    //   0x920  interfaceAdvisoryEnable
    //   0x928  setRxFlowSteering              (new in 26.3)
    //   0x930  setInterfaceEnable
    //   0x938  setRunningState
    //   0x940  handleChosenMedia
    //   … (see full table in commit)
    //   0xA00  deferBSDAttach
    //   0xA08  reportDetailedLinkStatus
    //   0xA10  getTSOOptions
    //   0xA18–0xA60  Reserved 0–9
    //
    // Previous header had two bugs:
    //   1) registerNetworkInterfaceWithLogicalLink + deregisterLogicalLink were
    //      placed AFTER deferBSDAttach/reportDetailedLinkStatus, shifting every
    //      slot from initBSDInterfaceParameters onward by –2.  Result:
    //      getBSDInterface() dispatched to prepareBSDInterface() → panic at
    //      ifnet_set_mtu(0x2ca0, 1500), CR2=0x2d88.
    //   2) setRxFlowSteering was missing, shifting setInterfaceEnable and
    //      everything below by –1.
    // ---------------------------------------------------------------
    virtual IOReturn registerNetworkInterfaceWithLogicalLink(IOSkywalkNetworkInterface::RegistrationInfo const*,IOSkywalkLogicalLink *,IOSkywalkPacketBufferPool *,IOSkywalkPacketBufferPool *,UInt);
    virtual IOReturn deregisterLogicalLink(void);
    virtual SInt32 initBSDInterfaceParameters(ifnet_init_eparams *,sockaddr_dl **) = 0;
    virtual bool prepareBSDInterface(ifnet_t,UInt);
    virtual void finalizeBSDInterface(ifnet_t,UInt);
    virtual ifnet_t getBSDInterface(void) const;
    virtual void setBSDName(char const*);
    virtual const char *getBSDName(void) const;
    virtual IOReturn processBSDCommand(ifnet_t,UInt,void *);
    virtual IOReturn processInterfaceCommand(ifdrv *);
    virtual IOReturn interfaceAdvisoryEnable(bool);
    virtual SInt32 setRxFlowSteering(UInt, ifnet_traffic_descriptor_common *, UInt);
    virtual SInt32 setInterfaceEnable(bool);
    virtual SInt32 setRunningState(bool);
    virtual IOReturn handleChosenMedia(UInt);
    virtual void *getSupportedMediaArray(UInt *,UInt *);
    virtual void *getPacketTapInfo(UInt *,UInt *);
    virtual UInt getUnsentDataByteCount(UInt *,UInt *,UInt) const;
    virtual UInt32 getSupportedWakeFlags(UInt *);
    virtual void enableNetworkWake(UInt);
    virtual void calculateRingSizeForQueue(IOSkywalkPacketQueue const*,UInt *) const;
    virtual UInt getMaxTransferUnit(void);
    virtual void setMaxTransferUnit(UInt);
    virtual UInt getMinPacketSize(void);
    virtual UInt getHardwareAssists(void);
    virtual void setHardwareAssists(UInt,UInt);
    virtual void *getInterfaceFamily(void);
    virtual void *getInterfaceSubFamily(void);
    virtual UInt getInitialMedia(void);
    virtual UInt getFeatureFlags(void);
    virtual UInt getTxDataOffset(void);
    virtual UInt captureInterfaceState(UInt);
    virtual void restoreInterfaceState(UInt);
    virtual void setMTU(UInt);
    virtual bool bpfTap(UInt,UInt);
    virtual const char *getBSDNamePrefix(void);
    virtual UInt getBSDUnitNumber(void);
    virtual const char *classNameOverride(void);
    virtual void deferBSDAttach(bool);
    virtual void reportDetailedLinkStatus(if_link_status const*);
    virtual UInt getTSOOptions(IOSkywalkNetworkInterface::IOSkywalkTSOOptions *);
    OSMetaClassDeclareReservedUnused( IOSkywalkNetworkInterface,  0);
    OSMetaClassDeclareReservedUnused( IOSkywalkNetworkInterface,  1);
    OSMetaClassDeclareReservedUnused( IOSkywalkNetworkInterface,  2);
    OSMetaClassDeclareReservedUnused( IOSkywalkNetworkInterface,  3);
    OSMetaClassDeclareReservedUnused( IOSkywalkNetworkInterface,  4);
    OSMetaClassDeclareReservedUnused( IOSkywalkNetworkInterface,  5);
    OSMetaClassDeclareReservedUnused( IOSkywalkNetworkInterface,  6);
    OSMetaClassDeclareReservedUnused( IOSkywalkNetworkInterface,  7);
    OSMetaClassDeclareReservedUnused( IOSkywalkNetworkInterface,  8);
    OSMetaClassDeclareReservedUnused( IOSkywalkNetworkInterface,  9);
    
public:
    void reportLinkStatus(unsigned int, unsigned int);
    
public:
    void *vptr;
    struct ExpansionData
    {
        RegistrationInfo *fRegistrationInfo;
        ifnet_t fBSDInterface;
    };
    ExpansionData *mExpansionData;
    // Ghidra metaclass constructor at FUN_0xa36bc2 passes 0xE0 as instance size.
    // Real kernel fields at +0xC0..+0xDF include NIF_Context (+0xC0),
    // nexusProvider (+0xC8), and nexus arena (+0xD0).
    // Previous pad was 2*8=16 bytes (class=0xD0), but real class is 0xE0.
    uint8_t pad[4 * 8];
};

static_assert(sizeof(IOSkywalkNetworkInterface) == 0xE0, "IOSkywalkNetworkInterface must match kernel metaclass size 0xE0");

#endif /* IOSkywalkNetworkInterface_h */
