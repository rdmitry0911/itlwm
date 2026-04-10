#ifndef IOSkywalkEthernetInterface_h
#define IOSkywalkEthernetInterface_h

#include "IOSkywalkNetworkInterface.h"

struct nicproxy_limits_info_s;
struct nicproxy_info_s;

class IOSkywalkEthernetInterface : public IOSkywalkNetworkInterface {
    OSDeclareAbstractStructors( IOSkywalkEthernetInterface )
    
public:
    struct RegistrationInfo {
        uint8_t pad[304];
    } __attribute__((packed));
    
public:
    virtual void free() APPLE_KEXT_OVERRIDE;
    virtual bool init(OSDictionary *) APPLE_KEXT_OVERRIDE;
    virtual IOReturn newUserClient( task_t owningTask, void * securityID,
                                   UInt32 type, OSDictionary * properties,
                                   LIBKERN_RETURNS_RETAINED IOUserClient ** handler ) APPLE_KEXT_OVERRIDE;
    virtual IOReturn setPowerState(
                                   unsigned long powerStateOrdinal,
                                   IOService *   whatDevice ) APPLE_KEXT_OVERRIDE;
    virtual IOReturn enable(UInt) APPLE_KEXT_OVERRIDE;
    virtual SInt32 initBSDInterfaceParameters(ifnet_init_eparams *,sockaddr_dl **) APPLE_KEXT_OVERRIDE;
    virtual bool prepareBSDInterface(ifnet_t,UInt) APPLE_KEXT_OVERRIDE;
    virtual IOReturn processBSDCommand(ifnet_t,UInt,void *) APPLE_KEXT_OVERRIDE;
    virtual void *getPacketTapInfo(UInt *,UInt *) APPLE_KEXT_OVERRIDE;
    virtual void enableNetworkWake(UInt) APPLE_KEXT_OVERRIDE;
    virtual UInt getMaxTransferUnit(void) APPLE_KEXT_OVERRIDE;
    virtual UInt getMinPacketSize(void) APPLE_KEXT_OVERRIDE;
    virtual void *getInterfaceFamily(void) APPLE_KEXT_OVERRIDE;
    virtual void *getInterfaceSubFamily(void) APPLE_KEXT_OVERRIDE;
    virtual UInt getInitialMedia(void) APPLE_KEXT_OVERRIDE;
    virtual const char *getBSDNamePrefix(void) APPLE_KEXT_OVERRIDE;
    // registerNetworkInterfaceWithLogicalLink is NOT declared here:
    // IOSkywalkEthernetInterface in the real kernel OVERRIDES the parent
    // IOSkywalkNetworkInterface::registerNetworkInterfaceWithLogicalLink
    // (same vtable slot).  Declaring it here with a different RegistrationInfo
    // type makes the C++ compiler treat it as a NEW virtual method, adding
    // an extra vtable slot that shifts every subsequent slot by +1.
    // Verified against IOSkywalkFamily.kext 26.3 (25D125):
    //   - IO80211SkywalkInterface vtable at __ZTV23IO80211SkywalkInterface
    //   - init(IOService*,ether_addr*) is at secondary slot 413 (offset 0xCE8)
    //   - isInterfaceEnabled() is at secondary slot 414 (offset 0xCF0)
    //   With the extra slot, our init lands at slot 414 → framework's call
    //   to isInterfaceEnabled (0xCF0) dispatches to init with wrong args → panic.
    virtual void getHardwareAddress(ether_addr *);
    virtual void setHardwareAddress(ether_addr *);
    virtual void setLinkLayerAddress(ether_addr *);
    virtual bool configureMulticastFilter(UInt,ether_addr const*,UInt);
    virtual bool setMulticastAddresses(ether_addr const*,UInt);
    virtual void setAllMulticastModeEnable(bool);
    virtual IOReturn setPromiscuousModeEnable(bool, UInt);
    virtual void reportNicProxyLimits(nicproxy_limits_info_s);
    virtual void hwConfigNicProxyData(nicproxy_info_s *);
    OSMetaClassDeclareReservedUnused( IOSkywalkEthernetInterface,  0 );
    OSMetaClassDeclareReservedUnused( IOSkywalkEthernetInterface,  1 );
    OSMetaClassDeclareReservedUnused( IOSkywalkEthernetInterface,  2 );
    OSMetaClassDeclareReservedUnused( IOSkywalkEthernetInterface,  3 );
    OSMetaClassDeclareReservedUnused( IOSkywalkEthernetInterface,  4 );
    OSMetaClassDeclareReservedUnused( IOSkywalkEthernetInterface,  5 );
    OSMetaClassDeclareReservedUnused( IOSkywalkEthernetInterface,  6 );
    OSMetaClassDeclareReservedUnused( IOSkywalkEthernetInterface,  7 );
    OSMetaClassDeclareReservedUnused( IOSkywalkEthernetInterface,  8 );
    OSMetaClassDeclareReservedUnused( IOSkywalkEthernetInterface,  9 );
    OSMetaClassDeclareReservedUnused( IOSkywalkEthernetInterface, 10 );
    
public:
    bool initRegistrationInfo(IOSkywalkEthernetInterface::RegistrationInfo*, unsigned int, unsigned long);
    // Return type is IOReturn (0 = kIOReturnSuccess), NOT bool.
    // Ghidra FUN_0xa3d994 returns ulong: 0 on success, IOReturn error code on failure.
    // C++ name mangling (Itanium ABI) does not include return type for
    // non-template functions, so the linker resolves to the same kernel symbol.
    IOReturn registerEthernetInterface(IOSkywalkEthernetInterface::RegistrationInfo const*, IOSkywalkPacketQueue**, unsigned int, IOSkywalkPacketBufferPool*, IOSkywalkPacketBufferPool*, unsigned int);
    
public:
    void *vptr;
    uint8_t pad1[0x30];
    struct ExpansionData
    {
        RegistrationInfo *fRegistrationInfo;
        ifnet_t fBSDInterface;
    };
    ExpansionData *mExpansionData2;
};

// mExpansionData2 must be at absolute offset 0x118: the kernel's
// registerEthernetInterface reads **(self+0x118) to get the
// EthernetRegistrationContext.  Ghidra cleanup at FUN_0xa3d76e
// clears *(self+0x118).  Was 0x108 when base was 0x10 too small.
static_assert(__offsetof(IOSkywalkEthernetInterface, mExpansionData2) == 0x118, "mExpansionData2 must be at kernel offset 0x118");

// Ghidra metaclass constructor at FUN_0xa3d540 passes 0x120 as instance size.
// IOSkywalkEthernetInterface-specific data is 0x40 bytes (unchanged),
// but base IOSkywalkNetworkInterface grew from 0xD0 to 0xE0.
static_assert(sizeof(IOSkywalkEthernetInterface) == 0x120, "IOSkywalkEthernetInterface must match kernel metaclass size 0x120");

#endif /* IOSkywalkEthernetInterface_h */
