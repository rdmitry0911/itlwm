//
//  AirportItlwmV2.hpp
//  AirportItlwm-Sonoma
//
//  Created by qcwap on 2023/6/27.
//  Copyright © 2023 钟先耀. All rights reserved.
//

#ifndef AirportItlwmV2_hpp
#define AirportItlwmV2_hpp

#include "Apple80211.h"
#include "TahoeCommanderV2.hpp"

#include "IOKit/network/IOGatedOutputQueue.h"
#include <libkern/c++/OSString.h>
#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOLib.h>
#include <libkern/OSKextLib.h>
#include <libkern/c++/OSMetaClass.h>
#include <IOKit/IOFilterInterruptEventSource.h>

#include "ItlIwm.hpp"
#include "ItlIwx.hpp"
#include "ItlIwn.hpp"

#if __IO80211_TARGET < __MAC_26_0
#include "AirportItlwmEthernetInterface.hpp"
#endif
#include "Airport/IO80211FaultReporter.h"
#include <skywalk/packet/os_packet.h>
#include <IOKit/skywalk/IOSkywalkTxSubmissionQueue.h>
#include <IOKit/skywalk/IOSkywalkRxCompletionQueue.h>

enum
{
    kPowerStateOff = 0,
    kPowerStateOn,
    kPowerStateCount
};

// WiFi radio power states (matches Apple's apple80211_power_state)
enum {
    kWiFiPowerOff     = 0,
    kWiFiPowerOn      = 1,
    kWiFiPowerStandby = 4,
};

#define kWatchDogTimerPeriod 1000

// ---------------------------------------------------------------
// Global runtime diagnostic — survives across all methods.
// Panic timers dump this so one crash report shows everything.
//
// rtMask bits (32-bit):
//  0  0x0000001  eventHandler called at least once
//  1  0x0000002  eventHandler: COUNTRY_CODE_UPDATE seen
//  2  0x0000004  eventHandler: ASSOC_DONE seen
//  3  0x0000008  eventHandler: DEAUTH seen
//  4  0x0000010  postMessageGated entered
//  5  0x0000020  postMessageGated succeeded
//  6  0x0000040  setLinkStatus called
//  7  0x0000080  setLinkStateGated called
//  8  0x0000100  watchdogAction called
//  9  0x0000200  enableAdapter called
// 10  0x0000400  disableAdapter called
// 11  0x0000800  first IOCTL processed
// 12  0x0001000  setPowerState called
// 13  0x0002000  fakeScanDone called
// 14  0x0004000  setLinkState returned OK
// 15  0x0008000  init() completed
// 16  0x0010000  createWorkQueue completed
// 17  0x0020000  configureInterface completed
// 18  0x0040000  start() completed (disarmed)
// 19  0x0080000  stop() entered
// 20  0x0100000  AirportItlwm::free() entered
// 21  0x0200000  AirportItlwm::free() super::free done
// 22  0x0400000  SkywalkInterface::free() entered
// 23  0x0800000  SkywalkInterface::free() super::free done
// 24  0x1000000  createInterface called
// 25  0x2000000  SCAN_DONE event received
// 26  0x4000000  ether_ifattach done
// 27  0x8000000  setInterfaceType(IFT_IEEE80211) called
//
// rtMask2 bits (BSD / framework / Skywalk lifecycle):
//  0  0x0001  fNetIf->getBSDInterface() returned non-NULL after start
//  1  0x0002  fNetIf->getBSDName() returned non-NULL after start
//  2  0x0004  first SCAN_REQ IOCTL from framework
//  3  0x0008  first ASSOCIATE IOCTL from framework
//  4  0x0010  first getSCAN_RESULT IOCTL from framework
//  5  0x0020  setSSID IOCTL from framework
//  6  0x0040  setPOWER IOCTL from framework
//  7  0x0080  DISASSOCIATE IOCTL from framework
//  8  0x0100  fVars (this+0xC0) non-NULL after init
//  9  0x0200  fVars->registrationInfo populated by us
// 10  0x0400  initBSDInterfaceParameters called by Skywalk
// 11  0x0800  prepareBSDInterface called by Skywalk
// 12  0x1000  setInterfaceEnable(true) called
// 13  0x2000  setRunningState called
// 14  0x4000  enable() called on interface
// 15  0x8000  postMessage called (first time, from Skywalk)
//
// rtMask3 bits (registration / BSD attach / interface bring-up):
//  0  0x0001  initRegistrationInfo completed
//  1  0x0002  mExpansionData populated
//  2  0x0004  Skywalk pools created
//  3  0x0008  Skywalk queues created
//  4  0x0010  registerEthernetInterface OK
//  5  0x0020  attachToDataLinkLayer entered
//  6  0x0040  attachToDataLinkLayer completed
//  7  0x0080  prepareBSDInterface entered (from attachToDataLinkLayer)
//  8  0x0100  prepareBSDInterface returned (from attachToDataLinkLayer)
//  9  0x0200  detachFromDataLinkLayer entered
// 10  0x0400  fNetIf->start() entered
// 11  0x0800  fNetIf->start() returned
// 12  0x1000  SkywalkInterface::init OK
// 13  0x2000  (reserved — SkywalkInterface has no start() override)
// 14  0x4000  (reserved — SkywalkInterface has no stop() override)
// 15  0x8000  IONetworkController::detachInterface entered
// ---------------------------------------------------------------
struct RuntimeDiag {
    volatile uint32_t rtMask;
    volatile uint32_t rtMask2;      // BSD/framework/Skywalk lifecycle bits
    volatile uint32_t rtMask3;      // registration/BSD attach/bring-up bits
    volatile int      ic_state;     // last seen ieee80211 state
    volatile uint32_t if_flags;     // last seen interface flags
    volatile uint32_t power_state;
    volatile uint32_t linkStatus;   // currentStatus
    volatile uint32_t evtCount;     // total eventHandler calls
    volatile uint32_t postMsgCount; // total postMessageGated calls
    volatile uint32_t wdCount;      // watchdog ticks
    volatile uint32_t ioctlCount;   // total IOCTL calls
    volatile int      lastIoctl;    // last IOCTL command
    volatile uint32_t lastPostMsg;  // last postMessage code
    volatile int      lastLinkState;// last link state (up=2/down=1)
    volatile uint32_t linkSetCount; // total setLinkStatus calls
    volatile int      lastEvtCode;  // last eventHandler msgCode
    volatile uint32_t scanCount;    // total fakeScanDone calls
    volatile uint32_t pmCount;      // total setPowerState calls
    volatile uint32_t scanDoneCount;// total SCAN_DONE events from firmware
    volatile uint32_t stopStep;     // last step reached in stop()
    volatile uint32_t freeStep;     // last step in AirportItlwm::free()
    volatile uint32_t skFreeStep;   // last step in SkywalkInterface::free()
    volatile uint32_t ifType;       // interface type set (0x47=WiFi, 0x06=Ether)
    volatile uint32_t scanReqCount; // total SCAN_REQ IOCTLs from framework
    volatile uint32_t assocCount;   // total ASSOCIATE IOCTLs from framework
    volatile uint32_t scanResCount; // total getSCAN_RESULT IOCTLs
    volatile uint32_t ic_flags;     // last seen ic->ic_flags (AUTO_JOIN, PSK, etc.)
    volatile uint32_t ic_des_esslen;// last seen ic->ic_des_esslen
    volatile uint32_t nodeCount;    // scan tree node count at last choose_bss
    volatile uint32_t matchFail;    // last match_bss fail bitmask
    volatile uint64_t fVarsPtr;     // raw fVars pointer (this+0xC0)
    volatile uint64_t bsdIfPtr;     // last seen BSD interface pointer
    volatile uint32_t enableCnt;    // enable() call count
    volatile uint32_t disableCnt;   // disable() call count
    volatile uint32_t startStep;    // fine-grained start() progress
    volatile uint32_t bsdIfFlags;   // last seen ifnet_flags on BSD interface
    volatile uint32_t bsdIfMtu;     // last seen ifnet_mtu on BSD interface
    volatile uint32_t lastEnableRet;// last enableAdapter return code
    volatile uint32_t lastPmReq;    // last setPowerState requested state
    // --- PM diagnostics (thread_call / power path) ---
    volatile uint64_t pmPolicyPtr;  // raw pmPolicyMaker pointer
    volatile uint32_t pmOffCancelRet;// thread_call_cancel result for powerOff
    volatile uint32_t pmOnCancelRet; // thread_call_cancel result for powerOn
    volatile uint32_t outputDropPwr; // packets dropped in outputPacket (power off)
    volatile uint32_t pmOffGateNull; // handleSetPowerStateOff gate==NULL count
    volatile uint32_t pmOnGateNull;  // handleSetPowerStateOn gate==NULL count
    volatile uint32_t pmAckOffCnt;   // acknowledgeSetPowerState calls from Off path
    volatile uint32_t pmAckOnCnt;    // acknowledgeSetPowerState calls from On path
    // --- Skywalk object pointers (for stop/free hang diagnosis) ---
    volatile uint64_t fNetIfPtr;     // raw fNetIf pointer
    volatile uint64_t fTxPoolPtr;    // raw fTxPool pointer
    volatile uint64_t fRxPoolPtr;    // raw fRxPool pointer
    volatile uint64_t fTxQueuePtr;   // raw fTxQueue pointer
    volatile uint64_t fRxQueuePtr;   // raw fRxQueue pointer
    // --- TX/RX Skywalk data path counters ---
    volatile uint32_t txCbCnt;       // skywalkTxAction invocations
    volatile uint32_t txPktSent;     // packets sent through outputPacket via TX
    volatile uint32_t txPktDrop;     // packets dropped in TX callback
    volatile uint32_t rxInputCnt;    // skywalkRxInput invocations
    volatile uint32_t rxPktOK;       // packets enqueued to fRxQueue
    volatile uint32_t rxAllocFail;   // fRxPool->allocatePacket failures
    volatile uint32_t rxEnqFail;     // fRxQueue->enqueuePackets failures
    volatile uint32_t rxCbCnt;       // skywalkRxAction invocations
    // --- nexusProvider diagnostics (raw fNetIf field offsets per YAML 91) ---
    volatile uint64_t nexusProvPtr;  // fNetIf+0xC8 (nexusProvider, must be non-NULL)
    volatile uint64_t nifCtxPtr;     // fNetIf+0xC0 (NIF_Context / ExpansionData)
    volatile uint64_t nexusArenaPtr; // fNetIf+0xD0 (nexus arena object)
    volatile uint64_t asyncSentinel; // fNetIf+0xB8 (0xe00002c7 = pending)
    volatile uint64_t regObj90;      // fNetIf+0x90 (queue-set object)
    volatile uint64_t regObj98;      // fNetIf+0x98 (queue-related object)
    volatile uint64_t regObjA0;      // fNetIf+0xA0 (queue manager object)
};
extern RuntimeDiag sRT;
#define RT_SET(bit)  do { sRT.rtMask  |= (1u << (bit)); } while(0)
#define RT2_SET(bit) do { sRT.rtMask2 |= (1u << (bit)); } while(0)
#define RT3_SET(bit) do { sRT.rtMask3 |= (1u << (bit)); } while(0)

extern "C" {
const char *convertApple80211IOCTLToString(signed int cmd);
}

class AirportItlwm : public IO80211Controller {
    OSDeclareDefaultStructors(AirportItlwm)
#define IOCTL(REQ_TYPE, REQ, DATA_TYPE) \
if (REQ_TYPE == SIOCGA80211) { \
ret = get##REQ(interface, (struct DATA_TYPE* )data); \
} else { \
ret = set##REQ(interface, (struct DATA_TYPE* )data); \
}
    
#define IOCTL_GET(REQ_TYPE, REQ, DATA_TYPE) \
if (REQ_TYPE == SIOCGA80211) { \
ret = get##REQ(interface, (struct DATA_TYPE* )data); \
}
#define IOCTL_SET(REQ_TYPE, REQ, DATA_TYPE) \
if (REQ_TYPE == SIOCSA80211) { \
ret = set##REQ(interface, (struct DATA_TYPE* )data); \
}
#define FUNC_IOCTL(REQ, DATA_TYPE) \
FUNC_IOCTL_GET(REQ, DATA_TYPE) \
FUNC_IOCTL_SET(REQ, DATA_TYPE)
#define FUNC_IOCTL_GET(REQ, DATA_TYPE) \
IOReturn get##REQ(OSObject *object, struct DATA_TYPE *data);
#define FUNC_IOCTL_SET(REQ, DATA_TYPE) \
IOReturn set##REQ(OSObject *object, struct DATA_TYPE *data);
    
public:
    virtual bool init(OSDictionary *properties) override;
    virtual void free() override;
    virtual IOService* probe(IOService* provider, SInt32* score) override;
    virtual bool start(IOService *provider) override;
    virtual void stop(IOService *provider) override;
#if __IO80211_TARGET < __MAC_26_0
    virtual IOReturn enable(IO80211SkywalkInterface *netif) override;
    virtual IOReturn disable(IO80211SkywalkInterface *netif) override;
#endif
    virtual IOReturn setHardwareAddress(const void *addr, UInt32 addrBytes) override;
    virtual IOReturn getHardwareAddress(IOEthernetAddress* addrP) override;
    virtual IOReturn setProperties(OSObject *properties) override;
    virtual IOReturn getPacketFilters(const OSSymbol *group, UInt32 *filters) const override;
    virtual IOReturn setPromiscuousMode(IOEnetPromiscuousMode mode) override;
    virtual IOReturn setMulticastMode(IOEnetMulticastMode mode) override;
    virtual IOReturn setMulticastList(IOEthernetAddress* addr, UInt32 len) override;
    virtual UInt32 getFeatures() const override;
    virtual const OSString * newVendorString() const override;
    virtual const OSString * newModelString() const override;
    virtual IOReturn selectMedium(const IONetworkMedium *medium) override;
    virtual bool createWorkQueue() override;
#if __IO80211_TARGET < __MAC_26_0
    virtual IONetworkInterface * createInterface() override;
#endif
    virtual bool configureInterface(IONetworkInterface *netif) override;
    virtual UInt32 outputPacket(mbuf_t, void * param) override;
#if defined(__PRIVATE_SPI__) && __IO80211_TARGET < __MAC_26_0
    virtual IOReturn outputStart(IONetworkInterface *interface, IOOptionBits options) override;
    virtual IOReturn networkInterfaceNotification(
                        IONetworkInterface * interface,
                        uint32_t              type,
                        void *                  argument ) override;
#endif
    virtual bool setLinkStatus(
                               UInt32                  status,
                               const IONetworkMedium * activeMedium = 0,
                               UInt64                  speed        = 0,
                               OSData *                data         = 0) override;
    static IOReturn setLinkStateGated(OSObject *target, void *arg0, void *arg1, void *arg2, void *arg3);
    static IOReturn postMessageGated(OSObject *target, void *arg0, void *arg1, void *arg2, void *arg3);
    static IOReturn postWclScanResultsGated(OSObject *target, void *arg0, void *arg1, void *arg2, void *arg3);

    static IOReturn tsleepHandler(OSObject* owner, void* arg0 = 0, void* arg1 = 0, void* arg2 = 0, void* arg3 = 0);
    static void eventHandler(struct ieee80211com *, int, void *);
    IOReturn enableAdapter(IONetworkInterface *netif);
    void disableAdapterCore(IONetworkInterface *netif);
    void disableAdapter(IONetworkInterface *netif);
    int handlePowerStateChange(uint32_t newState, IONetworkInterface *netif);
    void handleSystemPowerStateChange(bool powerOn, IONetworkInterface *netif);
    bool initCCLogs();

#if __IO80211_TARGET >= __MAC_26_0
    virtual IO80211WorkQueue *getWorkQueue() const override;
#else
    virtual IO80211WorkQueue *getWorkQueue() override;
#endif
    virtual bool requiresExplicitMBufRelease() override {
        return false;
    }
    virtual bool flowIdSupported() override {
        return false;
    }
    virtual SInt32 monitorModeSetEnabled(bool, UInt) override {
        return kIOReturnSuccess;
    }
    virtual IOReturn requestQueueSizeAndTimeout(unsigned short *queue, unsigned short *timeout) override {
        XYLog("%s\n", __FUNCTION__);
        return kIOReturnSuccess;
    }

    // Tahoe/26.x still uses controller slot +0xc80 for the primary Skywalk
    // interface cache, so keep exposing the bound infrastructure interface
    // there for family-side bootstrap consumers.
    virtual IO80211SkywalkInterface *getPrimarySkywalkInterface(void) override;
#if __IO80211_TARGET >= __MAC_26_0
    // dump[429] = releaseFlowQueue at vptr+0xD58.  Not called during start().
    virtual void *releaseFlowQueue(IO80211FlowQueue *) override;
    // dump[431] = getDriverLogStream (pure virtual) at vptr+0xD68.
    // IO80211Controller::start() calls this for setGlobalLogger(CCLogStream*).
    // Must return valid CCLogStream*.
    virtual void *getDriverLogStream() override;
#endif

    virtual bool getLogPipes(CCPipe**, CCPipe**, CCPipe**) override;

    virtual void *getFaultReporterFromDriver() override;
#if __IO80211_TARGET < __MAC_26_0
    virtual SInt32 apple80211_ioctl(IO80211SkywalkInterface *,unsigned long,void *, bool, bool) override;
#endif

    bool createMediumTables(const IONetworkMedium **primary);
    void releaseAll();
    void watchdogAction(IOTimerEventSource *timer);

    virtual SInt32 enableFeature(IO80211FeatureCode, void*) override;
    virtual bool isCommandProhibited(int command) override;
    virtual SInt32 handleCardSpecific(IO80211SkywalkInterface *,unsigned long,void *,bool) override;
    virtual IOReturn getDRIVER_VERSION(IO80211SkywalkInterface *interface,apple80211_version_data *data) override {
        XYLog("%s\n", __FUNCTION__);
        return getDRIVER_VERSION((OSObject *)interface, data);
    };
    virtual IOReturn getHARDWARE_VERSION(IO80211SkywalkInterface *interface,apple80211_version_data *data) override {
        XYLog("%s\n", __FUNCTION__);
        return getHARDWARE_VERSION((OSObject *)interface, data);
    };
    virtual IOReturn getCARD_CAPABILITIES(IO80211SkywalkInterface *interface,apple80211_capability_data *data) override {
        return getCARD_CAPABILITIES((OSObject *)interface, data);
    }
    virtual IOReturn getPOWER(IO80211SkywalkInterface *interface,apple80211_power_data *data) override {
        return getPOWER((OSObject *)interface, data);
    }
    virtual IOReturn setPOWER(IO80211SkywalkInterface *interface,apple80211_power_data *data) override {
        return setPOWER((OSObject *)interface, data);
    }
    virtual IOReturn getCOUNTRY_CODE(IO80211SkywalkInterface *interface,apple80211_country_code_data *data) override {
        return getCOUNTRY_CODE((OSObject *)interface, data);
    }
    virtual IOReturn setCOUNTRY_CODE(IO80211SkywalkInterface *interface,apple80211_country_code_data *data) override {
        return setCOUNTRY_CODE((OSObject *)interface, data);
    }
    virtual IOReturn getPLATFORM_CONFIG(IO80211SkywalkInterface *interface,apple80211_platform_config *data) override {
        return getPLATFORM_CONFIG((OSObject *)interface, data);
    }
    virtual IOReturn setGET_DEBUG_INFO(IO80211SkywalkInterface *interface,apple80211_debug_command *data) override {
        XYLog("%s\n", __FUNCTION__);
        return kIOReturnSuccess;
    }
#if __IO80211_TARGET >= __MAC_26_0
    // Tahoe adds several new control-plane virtuals.  Live 26.x logs showed
    // APPLE80211_IOC_PLATFORM_CONFIG still resolving to 0xe00002c7 even after
    // removing the explicit unsupported override, so the effective runtime
    // contract here is not "inherit and hope" but "provide a concrete producer".
    //
    // We therefore implement getPLATFORM_CONFIG explicitly and keep the other
    // Tahoe additions inherited until logs show they are on the critical path.
#endif
    
    //scan
    static void fakeScanDone(OSObject *owner, IOTimerEventSource *sender);
    
    //-----------------------------------------------------------------------
    // Power management support.
    //-----------------------------------------------------------------------
    virtual IOReturn registerWithPolicyMaker( IOService * policyMaker ) override;
    virtual IOReturn setPowerState( unsigned long powerStateOrdinal,
                                    IOService *   policyMaker) override;
    virtual unsigned long initialPowerStateForDomainState( IOPMPowerFlags domainState ) override;
    virtual IOReturn setWakeOnMagicPacket( bool active ) override;
    void setPowerStateOff(void);
    void setPowerStateOn(void);
    void unregistPM();
    bool initPCIPowerManagment(IOPCIDevice *provider);

    FUNC_IOCTL_GET(CARD_CAPABILITIES, apple80211_capability_data)
    FUNC_IOCTL(POWER, apple80211_power_data)
    FUNC_IOCTL_GET(DRIVER_VERSION, apple80211_version_data)
    FUNC_IOCTL_GET(HARDWARE_VERSION, apple80211_version_data)
    FUNC_IOCTL(COUNTRY_CODE, apple80211_country_code_data)
    IOReturn getSSID(OSObject *object, struct apple80211_ssid_data *data);
    IOReturn setSSID(OSObject *object, struct apple80211_ssid_data *data);
    IOReturn getBSSID(OSObject *object, struct apple80211_bssid_data *data);
    IOReturn setBSSID(OSObject *object, struct apple80211_bssid_data *data);
    IOReturn getCHANNEL(OSObject *object, struct apple80211_channel_data *data);
    IOReturn setCHANNEL(OSObject *object, struct apple80211_channel_data *data);
    IOReturn getPLATFORM_CONFIG(OSObject *object, struct apple80211_platform_config *data);
    
public:
    IOInterruptEventSource* fInterrupt;
    IOTimerEventSource *watchdogTimer;
    IOPCIDevice *pciNub;
    IONetworkStats *fpNetStats;
    // On Sequoia (26.x), BSD ifnet is created by IOSkywalkNetworkBSDClient
    // after deferBSDAttach(false). The legacy IOEthernetInterface shim is dead.
#if __IO80211_TARGET < __MAC_26_0
    AirportItlwmEthernetInterface *bsdInterface;
#endif
    IO80211SkywalkInterface *fNetIf;
    IOWorkLoop *fWatchdogWorkLoop;
    ItlHalService *fHalService;

    // Skywalk packet pools and queues for proper Sequoia registration
    IOSkywalkPacketBufferPool *fTxPool;
    IOSkywalkPacketBufferPool *fRxPool;
    IOSkywalkTxSubmissionQueue *fTxQueue;
    IOSkywalkRxCompletionQueue *fRxQueue;
    
    //IO80211
    uint8_t power_state;
    uint8_t tahoeRequestedPowerState;
    bool tahoeBootstrapPowerPending;
    bool tahoeBootstrapPowerWindowOpen;
    struct ieee80211_node *fNextNodeToSend;
    bool fScanResultWrapping;
    IOTimerEventSource *scanSource;
    
    u_int32_t current_authtype_lower;
    u_int32_t current_authtype_upper;
    UInt64 currentSpeed;
    UInt32 currentStatus;
    bool disassocIsVoluntary;
    char geo_location_cc[3];
    
    //pm
    thread_call_t powerOnThreadCall;
    thread_call_t powerOffThreadCall;
    thread_call_t tahoeBootThreadCall;
    UInt32 pmPowerState;
    IOService *pmPolicyMaker;
    UInt8 pmPCICapPtr;
    bool magicPacketEnabled;
    bool magicPacketSupported;
    
    //AWDL
    uint8_t *syncFrameTemplate;
    uint32_t syncFrameTemplateLength;
    uint8_t awdlBSSID[6];
    uint32_t awdlSyncState;
    uint32_t awdlElectionId;
    uint32_t awdlPresenceMode;
    uint16_t awdlMasterChannel;
    uint16_t awdlSecondaryMasterChannel;
    uint8_t *roamProfile;
    struct apple80211_btc_profiles_data *btcProfile;
    struct apple80211_btc_config_data btcConfig;
    uint32_t btcMode;
    uint32_t btcOptions;
    bool awdlSyncEnable;
    
    CCPipe *driverLogPipe;
    CCPipe *driverDataPathPipe;
    CCPipe *driverSnapshotsPipe;

    CCLogStream *driverLogStream;
    CCStream *driverFaultReporter;
    TahoeOwnerRegistry &getTahoeOwnerRegistry() { return tahoeOwnerRegistry; }
    TahoeCommanderV2 &getTahoeCommander() { return tahoeCommander; }

    IO80211FaultReporter *io80211FaultReporter;

    void performTahoeBootChipImage();

private:
    TahoeOwnerRegistry tahoeOwnerRegistry;
    TahoeCommanderV2 tahoeCommander{&tahoeOwnerRegistry};
};

// Boot nub — replicates Apple's AppleBCMWLANUserClient IOKit matching pattern.
// Apple's bootChipImage is triggered by an IOService (AppleBCMWLANUserClient)
// that matches against the controller via IOKit matching.  This nub does the same:
// IOKit starts it after AirportItlwm::registerService(), and its start() triggers
// the async boot via the pre-allocated thread_call.
class AirportItlwmBootNub : public IOService {
    OSDeclareDefaultStructors(AirportItlwmBootNub)
public:
    bool start(IOService *provider) override;
};

#endif /* AirportItlwmV2_hpp */
