//
//  IO80211ControllerV3.h
//  IO80211Family - macOS 26 Tahoe
//
//  Reverse-engineered vtable from BootKernelExtensions.kc (26.2 25C56)
//  vtable for IO80211Controller: 471 entries, IO80211Controller-specific: [396]-[469]
//

#ifndef _IO80211CONTROLLERV3_H
#define _IO80211CONTROLLERV3_H

#if defined(KERNEL) && defined(__cplusplus)

#include <Availability.h>
#include <libkern/version.h>

#ifndef __IO80211_TARGET
#error "Please define __IO80211_TARGET to the requested version"
#endif

#if VERSION_MAJOR > 8
#define _MODERN_BPF
#endif

#include <sys/kpi_mbuf.h>

#include <IOKit/network/IOEthernetController.h>

#include <sys/param.h>
#include <net/bpf.h>

#include "apple80211_ioctl.h"
#include "IO80211SkywalkInterface.h"
#include "IO80211WorkLoop.h"
#include "IO80211WorkQueue.h"
#include "CCStream.h"
#include "CCDataPipe.h"
#include "CCLogPipe.h"
#include "CCLogStream.h"

#define AUTH_TIMEOUT            15    // seconds

enum {
    LINK_SPEED_80211A    = 54000000ul,        // 54 Mbps
    LINK_SPEED_80211B    = 11000000ul,        // 11 Mbps
    LINK_SPEED_80211G    = 54000000ul,        // 54 Mbps
    LINK_SPEED_80211N    = 300000000ul,        // 300 Mbps (MCS index 15, 400ns GI, 40 MHz channel)
};

enum IO80211CountryCodeOp
{
    kIO80211CountryCodeReset,
};
typedef enum IO80211CountryCodeOp IO80211CountryCodeOp;

enum IO80211SystemPowerState
{
    kIO80211SystemPowerStateUnknown,
    kIO80211SystemPowerStateAwake,
    kIO80211SystemPowerStateSleeping,
};
typedef enum IO80211SystemPowerState IO80211SystemPowerState;

enum IO80211FeatureCode
{
    kIO80211Feature80211n = 1,
};
typedef enum IO80211FeatureCode IO80211FeatureCode;


class IOSkywalkInterface;
class IO80211ScanManager;

enum scanSource
{
    SOURCE_1,
};

enum joinStatus
{
    STATUS_1,
};

class IO80211Controller;
class IO80211Interface;
class IO82110WorkLoop;
class IO80211VirtualInterface;
class IO80211ControllerMonitor;
class CCLogPipe;
class CCIOReporterLogStream;
class CCLogStream;
class IO80211RangingManager;
class IO80211FlowQueue;
class IO80211FlowQueueLegacy;
class FlowIdMetadata;
class IOReporter;
class IO80211InfraInterface;
class IO80211PostOffice;
extern void IO80211VirtualInterfaceNamerRetain();


struct apple80211_hostap_state;
struct apple80211_awdl_sync_channel_sequence;
struct ieee80211_ht_capability_ie;
struct apple80211_channel_switch_announcement;
struct apple80211_beacon_period_data;
struct apple80211_power_debug_sub_info;
struct apple80211_stat_report;
struct apple80211_frame_counters;
struct apple80211_leaky_ap_event;
struct apple80211_chip_stats;
struct apple80211_extended_stats;
struct apple80211_ampdu_stat_report;
struct apple80211_btCoex_report;
struct apple80211_cca_report;
class CCPipe;
struct apple80211_lteCoex_report;

// New structs in Tahoe
struct apple80211_platform_config;
struct apple80211_device_orientation;
struct apple80211_device_accessory_info;
struct apple80211_powertable_version_data;

typedef IOReturn (*IOCTL_FUNC)(IO80211Controller*, IO80211Interface*, IO80211VirtualInterface*, apple80211req*, bool);
extern IOCTL_FUNC gGetHandlerTable[];
extern IOCTL_FUNC gSetHandlerTable[];

class IO80211InterfaceAVCAdvisory;

//
// IO80211Controller for macOS 26 Tahoe
//
// Virtual method order MUST match the vtable extracted from BootKernelExtensions.kc.
// Slots [0]-[395] are inherited from parent classes (OSObject -> IORegistryEntry ->
// IOService -> IONetworkController -> IOEthernetController) and are stable.
// Slots [396]-[469] are IO80211Controller's own virtual methods listed below.
//
class IO80211Controller : public IOEthernetController {
    OSDeclareAbstractStructors(IO80211Controller)

public:
    // --- Parent class overrides (matches vtable [0]-[395]) ---
    virtual void free() APPLE_KEXT_OVERRIDE;
    virtual bool init(OSDictionary *) APPLE_KEXT_OVERRIDE;
    virtual IOReturn configureReport(IOReportChannelList *,UInt,void *,void *) APPLE_KEXT_OVERRIDE;
    virtual IOReturn updateReport(IOReportChannelList *,UInt,void *,void *) APPLE_KEXT_OVERRIDE;
    virtual bool start(IOService *) APPLE_KEXT_OVERRIDE;
    virtual void stop(IOService *) APPLE_KEXT_OVERRIDE;
    virtual IOWorkLoop* getWorkLoop(void) const APPLE_KEXT_OVERRIDE;
    virtual const char* stringFromReturn(int) APPLE_KEXT_OVERRIDE;
    virtual int errnoFromReturn(int) APPLE_KEXT_OVERRIDE;
    virtual UInt32 getFeatures() const APPLE_KEXT_OVERRIDE;
    virtual const OSString * newVendorString() const APPLE_KEXT_OVERRIDE;
    virtual const OSString * newModelString() const APPLE_KEXT_OVERRIDE;
    virtual bool createWorkLoop() APPLE_KEXT_OVERRIDE;
    virtual IOReturn getHardwareAddress(IOEthernetAddress *) APPLE_KEXT_OVERRIDE;
    virtual IOReturn setHardwareAddress(const IOEthernetAddress * addrP) APPLE_KEXT_OVERRIDE;
    virtual IOReturn setMulticastMode(bool active) APPLE_KEXT_OVERRIDE;
    virtual IOReturn setPromiscuousMode(bool active) APPLE_KEXT_OVERRIDE;

    // --- IO80211Controller own virtuals: vtable [396]-[469] ---

    // [396] pure virtual
    virtual bool isCommandProhibited(int) = 0;
    // [397]
    virtual bool createWorkQueue();
    // [398] NEW in Tahoe
    virtual void debugStateInit();
    // [399] NOW CONST
    virtual IO80211WorkQueue *getWorkQueue() const;
    // [400]
    virtual void requestPacketTx(void*, UInt);
    // [401] NOW CONST
    virtual IOCommandGate *getIO80211CommandGate() const;
    // [402]
    virtual IO80211SkywalkInterface* getPrimarySkywalkInterface(void);
    // [403]
    virtual int bpfOutputPacket(OSObject *,UInt,mbuf_t m);
    // [404]
    virtual SInt32 monitorModeSetEnabled(bool, UInt);
    // [405] pure virtual - handleCardSpecific
    virtual SInt32 handleCardSpecific(IO80211SkywalkInterface *,unsigned long,void *,bool) = 0;
    // [406]
    virtual UInt32 hardwareOutputQueueDepth();
    // [407]
    virtual SInt32 performCountryCodeOperation(IO80211CountryCodeOp);
    // [408]
    virtual void dataLinkLayerAttachComplete();
    // [409]
    virtual SInt32 enableFeature(IO80211FeatureCode, void*) = 0;

    // [410]-[417] pure virtual: driver-specific IOCTL handlers
    virtual IOReturn getDRIVER_VERSION(IO80211SkywalkInterface *,apple80211_version_data *) = 0;
    virtual IOReturn getHARDWARE_VERSION(IO80211SkywalkInterface *,apple80211_version_data *) = 0;
    virtual IOReturn getCARD_CAPABILITIES(IO80211SkywalkInterface *,apple80211_capability_data *) = 0;
    virtual IOReturn getPOWER(IO80211SkywalkInterface *,apple80211_power_data *) = 0;
    virtual IOReturn setPOWER(IO80211SkywalkInterface *,apple80211_power_data *) = 0;
    virtual IOReturn getCOUNTRY_CODE(IO80211SkywalkInterface *,apple80211_country_code_data *) = 0;
    virtual IOReturn setCOUNTRY_CODE(IO80211SkywalkInterface *,apple80211_country_code_data *) = 0;
    virtual IOReturn setGET_DEBUG_INFO(IO80211SkywalkInterface *,apple80211_debug_command *) = 0;

    // [418] NEW in Tahoe - has base class implementation
    virtual IOReturn getPLATFORM_CONFIG(IO80211SkywalkInterface *, apple80211_platform_config *);
    // [419] NEW in Tahoe
    virtual IOReturn getDEVICE_ORIENTATION(IO80211SkywalkInterface *, apple80211_device_orientation *);
    // [420] NEW in Tahoe
    virtual IOReturn setDEVICE_ORIENTATION(IO80211SkywalkInterface *, apple80211_device_orientation *);
    // [421] NEW in Tahoe
    virtual IOReturn getACCESSORY_STATE(IO80211SkywalkInterface *, apple80211_device_accessory_info *);
    // [422] NEW in Tahoe
    virtual IOReturn setACCESSORY_STATE(IO80211SkywalkInterface *, apple80211_device_accessory_info *);
    // [423] NEW in Tahoe
    virtual IOReturn getPOWERTABLE_VERSION(IO80211SkywalkInterface *, apple80211_powertable_version_data *);

    // [424]
    virtual SInt32 enableVirtualInterface(IO80211VirtualInterface *);
    // [425]
    virtual SInt32 disableVirtualInterface(IO80211VirtualInterface *);
    // [426]
    virtual bool requiresExplicitMBufRelease();
    // [427]
    virtual bool flowIdSupported() {
        return false;
    }
    // [428]
    virtual IO80211FlowQueueLegacy* requestFlowQueue(FlowIdMetadata const*);
    // [429] — IO80211Controller::start() calls this at vtable offset 0xd68 to get the
    // driver's CCLogStream* for the global logger.  The base-class symbol name is
    // releaseFlowQueue, but Apple drivers override this slot to return CCLogStream*.
    // IO80211Family uses the returned pointer in 28+ call sites for logging.
    virtual void *releaseFlowQueue(IO80211FlowQueue *);
    // [430]
    virtual bool getLogPipes(CCPipe**, CCPipe**, CCPipe**);
    // [431] pure virtual - NEW in Tahoe
    // Called from IO80211ControllerMonitor::initWithControllerAndProvider() during
    // createIOReporters.  Must return non-null CCLogStream* or createIOReporters fails.
    virtual void *getDriverLogStream() = 0;
    // [432]
    virtual void enableFeatureForLoggingFlags(unsigned long long) {};
    // [433]
    virtual IOReturn requestQueueSizeAndTimeout(unsigned short *, unsigned short *) { return kIOReturnIOError; };
    // [434]
    virtual IOReturn enablePacketTimestamping(void) {
        return kIOReturnUnsupported;
    }
    // [435]
    virtual IOReturn disablePacketTimestamping(void) {
        return kIOReturnUnsupported;
    }
    // [436]
    virtual UInt getPacketTSCounter();
    // [437]
    virtual void *getDriverTextLog();
    // [438]
    virtual UInt32 selfDiagnosticsReport(int,char const*,UInt);
    // [439] pure virtual
    virtual void *getFaultReporterFromDriver() = 0;
    // [440] NEW in Tahoe
    virtual void *allocIO80211RecursiveLock();
    // [441]
    virtual UInt32 getDataQueueDepth(OSObject *);
    // [442]
    virtual bool wasDynSARInFailSafeMode(void) { return false; }
    // [443]
    virtual void updateAdvisoryScoresIfNeed(void);
    // [444]
    virtual UInt64 getAVCAdvisoryInfo(IO80211InterfaceAVCAdvisory *);
    // [445] NEW in Tahoe
    virtual UInt32 getActionFramePoolCapacity();
    // [446] NEW in Tahoe
    virtual void *getPostOffice();
    // [447] NEW in Tahoe
    virtual void *CreatePostOffice();
    // [448]
    virtual bool attachInterface(OSObject *,IOService *);
    // [449]
    virtual void detachInterface(OSObject *,bool);
    // [450]
    virtual IO80211VirtualInterface* createVirtualInterface(ether_addr *,UInt);
    // [451]
    virtual bool attachVirtualInterface(IO80211VirtualInterface **,ether_addr *,UInt,bool);
    // [452]
    virtual bool detachVirtualInterface(IO80211VirtualInterface *,bool);

    // [453]-[468] Reserved slots
    OSMetaClassDeclareReservedUnused( IO80211Controller,  0);
    OSMetaClassDeclareReservedUnused( IO80211Controller,  1);
    OSMetaClassDeclareReservedUnused( IO80211Controller,  2);
    OSMetaClassDeclareReservedUnused( IO80211Controller,  3);
    OSMetaClassDeclareReservedUnused( IO80211Controller,  4);
    OSMetaClassDeclareReservedUnused( IO80211Controller,  5);
    OSMetaClassDeclareReservedUnused( IO80211Controller,  6);
    OSMetaClassDeclareReservedUnused( IO80211Controller,  7);
    OSMetaClassDeclareReservedUnused( IO80211Controller,  8);
    OSMetaClassDeclareReservedUnused( IO80211Controller,  9);
    OSMetaClassDeclareReservedUnused( IO80211Controller, 10);
    OSMetaClassDeclareReservedUnused( IO80211Controller, 11);
    OSMetaClassDeclareReservedUnused( IO80211Controller, 12);
    OSMetaClassDeclareReservedUnused( IO80211Controller, 13);
    OSMetaClassDeclareReservedUnused( IO80211Controller, 14);
    OSMetaClassDeclareReservedUnused( IO80211Controller, 15);

    // [469]
    virtual IOReturn setMulticastList(ether_addr const*, UInt);

public:
    // Non-virtual public methods (resolved at link time, not in vtable)
    IOReturn addReporterLegend(IOService *,IOReporter *,char const*,char const*);
    IOReturn removeReporterFromLegend(IOService *,IOReporter *,char const*,char const*);
    void setLeakyAPStats(apple80211_leaky_ap_event *);
    bool setChipCounterStats(apple80211_stat_report *,apple80211_chip_stats *,apple80211_channel *);
    bool setFrameStats(apple80211_stat_report *,apple80211_frame_counters *,apple80211_channel *);
    bool setPowerStats(apple80211_stat_report *,apple80211_power_debug_sub_info *);
    bool setAMPDUstat(apple80211_stat_report *,apple80211_ampdu_stat_report *,apple80211_channel *);
    bool setBTCoexstat(apple80211_stat_report *,apple80211_btCoex_report *);
    bool setLTECoexstat(apple80211_stat_report *,apple80211_lteCoex_report *);
    IOReturn setChanCCA(apple80211_stat_report *,int);
    IOReturn setChanExtendedCCA(apple80211_stat_report *,apple80211_cca_report *);
    IOReturn setChanNoiseFloor(apple80211_stat_report *,int);
    IOReturn setChanNoiseFloorLTE(apple80211_stat_report *,int);
    bool setExtendedChipCounterStats(apple80211_stat_report *,void *);
    void postMessage(IO80211SkywalkInterface*, UInt, void *, unsigned long, bool);
    void postMessageSync(IO80211SkywalkInterface*, UInt, void *, unsigned long, bool);
    void updateWoWReasonToIoReg(UInt, char*, unsigned long, UInt);
    void getProcessName(char*, unsigned long);
    UInt getPid();
    IOService *getReporterProvider();
    IOService *GetProvider() const;

protected:
    uint8_t  filler[0x128];
};

#endif /* defined(KERNEL) && defined(__cplusplus) */

#endif /* !_IO80211CONTROLLERV3_H */
