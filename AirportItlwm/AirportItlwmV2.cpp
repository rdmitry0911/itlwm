//
//  AirportItlwmV2.cpp
//  AirportItlwm-Sonoma
//
//  Created by qcwap on 2023/6/27.
//  Copyright © 2023 钟先耀. All rights reserved.
//

#include "AirportItlwmV2.hpp"
#include <sys/_netstat.h>
#include <crypto/sha1.h>
#include <net80211/ieee80211_priv.h>
#include <net80211/ieee80211_var.h>

#include "AirportItlwmSkywalkInterface.hpp"
#include "IOPCIEDeviceWrapper.hpp"
#include <IOKit/skywalk/IOSkywalkPacketBuffer.h>

#define super IO80211Controller
OSDefineMetaClassAndStructors(AirportItlwm, IO80211Controller);
OSDefineMetaClassAndStructors(CTimeout, OSObject)

#include "Airport/CCDataStream.h"
#include "Airport/CCFaultReporter.h"


IO80211WorkQueue *_fWorkloop;
IOCommandGate *_fCommandGate;

// RuntimeDiag struct defined in AirportItlwmV2.hpp
RuntimeDiag sRT = {};

// Skywalk TX submission callback — called when BSD stack has packets to transmit.
// The Skywalk nexus delivers packets from the BSD ifnet TX path as
// IOSkywalkPacket objects. We extract the frame data from each packet's
// buflet, copy it into an mbuf, and send it through the existing
// outputPacket path which enqueues to the hardware via if_snd.
//
// Return type is unsigned int (not IOReturn/int) to match kernel ABI.
// Kernel symbol uses mangled 'j' (unsigned int), not 'i' (int).
// Packet param is IOSkywalkPacket * const * (PKP mangling) — the array
// entries are const, but the packets themselves are mutable.
static unsigned int
skywalkTxAction(OSObject *owner, IOSkywalkTxSubmissionQueue *queue,
                IOSkywalkPacket * const *packets, UInt32 count, void *refCon)
{
    AirportItlwm *that = OSDynamicCast(AirportItlwm, owner);
    if (!that) return 0;

    sRT.txCbCnt++;
    UInt32 sent = 0;
    for (UInt32 i = 0; i < count; i++) {
        IOSkywalkPacket *pkt = packets[i];

        // Get the first packet buffer to access the underlying buflet
        IOSkywalkPacketBuffer *bufs[1] = { NULL };
        UInt32 nBufs = pkt->getPacketBuffers(bufs, 1);
        if (nBufs == 0 || !bufs[0]) {
            sRT.txPktDrop++;
            if (sRT.txCbCnt <= 3)
                XYLog("skywalkTxAction: pkt %u/%u no buffers\n", i, count);
            continue;
        }

        kern_buflet_t buflet = bufs[0]->mBufletHandle;
        if (!buflet) { sRT.txPktDrop++; continue; }

        void *objAddr = kern_buflet_get_object_address(buflet);
        uint16_t dataOff = kern_buflet_get_data_offset(buflet);
        uint16_t dataLen = kern_buflet_get_data_length(buflet);

        if (!objAddr || dataLen == 0) { sRT.txPktDrop++; continue; }

        // Allocate an mbuf with packet header and copy the Ethernet frame
        mbuf_t m = NULL;
        if (mbuf_allocpacket(MBUF_DONTWAIT, dataLen, NULL, &m) != 0) {
            sRT.txPktDrop++;
            continue;
        }

        if (mbuf_copyback(m, 0, dataLen, (uint8_t *)objAddr + dataOff, MBUF_DONTWAIT) != 0) {
            mbuf_freem(m);
            sRT.txPktDrop++;
            continue;
        }

        that->outputPacket(m, NULL);
        sent++;
    }

    sRT.txPktSent += sent;
    if (sRT.txCbCnt <= 3 && count > 0)
        XYLog("skywalkTxAction: count=%u sent=%u (total tx=%u)\n", count, sent, sRT.txPktSent);
    return sent;
}

// Skywalk RX completion callback — notification from the Skywalk subsystem
// after it has consumed packets that the driver enqueued to the RX queue.
// This is a completion notification, not a receive path — the actual RX
// injection happens in skywalkRxInput() called from _if_input().
// Return type is unsigned int to match kernel ABI (see TX callback comment).
static unsigned int
skywalkRxAction(OSObject *owner, IOSkywalkRxCompletionQueue *queue,
                IOSkywalkPacket **packets, UInt32 count, void *refCon)
{
    sRT.rxCbCnt++;
    return count;
}

#if __IO80211_TARGET >= __MAC_26_0
// Skywalk RX input handler — called from _if_input() on the Sequoia path.
// Converts an mbuf (received from the 802.11 stack) into an IOSkywalkPacket
// and enqueues it to the RX completion queue for delivery to the BSD stack
// through the Skywalk nexus.
static int
skywalkRxInput(struct _ifnet *ifp, mbuf_t m)
{
    AirportItlwm *that = OSDynamicCast(AirportItlwm, ifp->controller);
    if (!that || !that->fRxPool || !that->fRxQueue) {
        mbuf_freem(m);
        return ENXIO;
    }

    sRT.rxInputCnt++;

    size_t len = mbuf_pkthdr_len(m);
    if (len == 0) {
        mbuf_freem(m);
        return EINVAL;
    }

    // Allocate an IOSkywalkPacket from the RX pool
    IOSkywalkPacket *rxPkt = NULL;
    if (that->fRxPool->allocatePacket(1, &rxPkt, 0) != kIOReturnSuccess || !rxPkt) {
        sRT.rxAllocFail++;
        if (sRT.rxAllocFail <= 5)
            XYLog("skywalkRxInput: allocatePacket failed (drop #%u)\n", sRT.rxAllocFail);
        mbuf_freem(m);
        return ENOMEM;
    }

    // Get the packet buffer to access the underlying buflet
    IOSkywalkPacketBuffer *bufs[1] = { NULL };
    UInt32 nBufs = rxPkt->getPacketBuffers(bufs, 1);
    if (nBufs == 0 || !bufs[0]) {
        that->fRxPool->deallocatePacket(rxPkt);
        mbuf_freem(m);
        return ENOMEM;
    }

    kern_buflet_t buflet = bufs[0]->mBufletHandle;
    void *objAddr = kern_buflet_get_object_address(buflet);
    uint32_t bufSize = kern_buflet_get_object_limit(buflet);
    if (!objAddr || len > bufSize) {
        that->fRxPool->deallocatePacket(rxPkt);
        mbuf_freem(m);
        return EMSGSIZE;
    }

    // Copy the mbuf data into the Skywalk packet buffer
    mbuf_copydata(m, 0, len, objAddr);
    kern_buflet_set_data_offset(buflet, 0);
    kern_buflet_set_data_length(buflet, (uint16_t)len);
    rxPkt->setDataLength((UInt32)len);

    // Enqueue the packet to the RX completion queue
    const IOSkywalkPacket *pktArray[] = { rxPkt };
    IOReturn ret = that->fRxQueue->enqueuePackets(pktArray, 1, 0);

    mbuf_freem(m);

    if (ret != kIOReturnSuccess) {
        sRT.rxEnqFail++;
        if (sRT.rxEnqFail <= 5)
            XYLog("skywalkRxInput: enqueuePackets failed 0x%x (drop #%u)\n", ret, sRT.rxEnqFail);
        return EIO;
    }

    sRT.rxPktOK++;

    return 0;
}
#endif /* __IO80211_TARGET >= __MAC_26_0 */

void AirportItlwm::releaseAll()
{
    XYLog("DEBUG %s [1] logStream=%p(rc=%d) logPipe=%p dataPath=%p snapshots=%p faultReporter=%p\n",
          __FUNCTION__, driverLogStream, driverLogStream ? driverLogStream->getRetainCount() : -1,
          driverLogPipe, driverDataPathPipe, driverSnapshotsPipe, driverFaultReporter);
    OSSafeReleaseNULL(driverLogStream);
    OSSafeReleaseNULL(driverLogPipe);
    OSSafeReleaseNULL(driverDataPathPipe);
    OSSafeReleaseNULL(driverSnapshotsPipe);
    OSSafeReleaseNULL(driverFaultReporter);
    if (io80211FaultReporter) {
        io80211FaultReporter->release();
        io80211FaultReporter = NULL;
    }
    if (fHalService) {
        XYLog("DEBUG %s [2] releasing fHalService=%p\n", __FUNCTION__, fHalService);
        fHalService->release();
        fHalService = NULL;
    }
    if (_fWorkloop) {
        if (_fCommandGate) {
            XYLog("DEBUG %s [3] removing _fCommandGate=%p from _fWorkloop=%p\n", __FUNCTION__, _fCommandGate, _fWorkloop);
            _fWorkloop->removeEventSource(_fCommandGate);
            _fCommandGate->release();
            _fCommandGate = NULL;
        }
        if (scanSource) {
            XYLog("DEBUG %s [4] removing scanSource=%p\n", __FUNCTION__, scanSource);
            scanSource->cancelTimeout();
            scanSource->disable();
            _fWorkloop->removeEventSource(scanSource);
            scanSource->release();
            scanSource = NULL;
        }
        if (fWatchdogWorkLoop && watchdogTimer) {
            XYLog("DEBUG %s [5] removing watchdogTimer=%p from fWatchdogWorkLoop=%p\n", __FUNCTION__, watchdogTimer, fWatchdogWorkLoop);
            watchdogTimer->cancelTimeout();
            fWatchdogWorkLoop->removeEventSource(watchdogTimer);
            watchdogTimer->release();
            watchdogTimer = NULL;
            fWatchdogWorkLoop->release();
            fWatchdogWorkLoop = NULL;
        }
        XYLog("DEBUG %s [6] releasing _fWorkloop=%p\n", __FUNCTION__, _fWorkloop);
        _fWorkloop->release();
        _fWorkloop = NULL;
    }
    XYLog("DEBUG %s [7] unregistPM\n", __FUNCTION__);
    unregistPM();
    XYLog("DEBUG %s DONE\n", __FUNCTION__);
}

IOReturn AirportItlwm::
postMessageGated(OSObject *target, void *arg0, void *arg1, void *arg2, void *arg3)
{
    RT_SET(4);
    RT2_SET(15);
    sRT.postMsgCount++;
    AirportItlwm *that = OSDynamicCast(AirportItlwm, target);
    if (!that || !that->fNetIf) return kIOReturnNotReady;
    UInt32 msg = (UInt32)(uintptr_t)arg0;
    sRT.lastPostMsg = msg;
    // Use controller-level postMessage — routes through IO80211Controller
    // dispatch (like Apple's postMessageInfra), NOT through
    // IO80211InfraInterface::postMessage which calls
    // updateCountryCodeProperty → sendIOUCToWcl (panics on workloop).
    // arg1 = optional data pointer, arg2 = optional data length
    // (reference: scanComplete passes &status/4, others pass NULL/0).
    that->postMessage(that->fNetIf, msg, arg1, (unsigned int)(uintptr_t)arg2, true);
    RT_SET(5);
    return kIOReturnSuccess;
}

void AirportItlwm::
eventHandler(struct ieee80211com *ic, int msgCode, void *data)
{
    AirportItlwm *that = OSDynamicCast(AirportItlwm, ic->ic_ac.ac_if.controller);
    RT_SET(0);
    sRT.evtCount++;
    sRT.lastEvtCode = msgCode;
    sRT.ic_state = ic->ic_state;
    sRT.if_flags = ic->ic_ac.ac_if.if_flags;
    XYLog("DEBUG %s msgCode=%d ic_state=%d if_flags=0x%x power_state=%u\n",
          __FUNCTION__, msgCode, ic->ic_state, ic->ic_ac.ac_if.if_flags, that->power_state);
    if (!that || !that->fNetIf) {
        XYLog("DEBUG %s SKIP: interface=NULL\n", __FUNCTION__);
        return;
    }
    UInt32 apple80211Msg;
    void *msgData = NULL;
    unsigned int msgDataLen = 0;
    static UInt32 scanStatus;  // static — must survive until postMessageGated runs
    switch (msgCode) {
        case IEEE80211_EVT_COUNTRY_CODE_UPDATE:
            RT_SET(1);
            apple80211Msg = APPLE80211_M_COUNTRY_CODE_CHANGED;
            break;
        case IEEE80211_EVT_STA_ASSOC_DONE:
            RT_SET(2);
            apple80211Msg = APPLE80211_M_ASSOC_DONE;
            break;
        case IEEE80211_EVT_STA_DEAUTH:
            RT_SET(3);
            apple80211Msg = APPLE80211_M_DEAUTH_RECEIVED;
            break;
        case IEEE80211_EVT_SCAN_DONE:
            // Reference: AppleBCMWLANCore::scanComplete calls
            // postMessage(infra, 10, &status, 4, 1) directly — no timer.
            RT_SET(25);
            sRT.scanDoneCount++;
            apple80211Msg = APPLE80211_M_SCAN_DONE;
            scanStatus = data ? *(UInt32 *)data : 0;
            msgData = &scanStatus;
            msgDataLen = sizeof(scanStatus);
            break;
        default:
            XYLog("DEBUG %s UNHANDLED msgCode=%d\n", __FUNCTION__, msgCode);
            return;
    }
    // Defer postMessage to workloop context — cannot call from interrupt thread.
    that->getCommandGate()->runAction(postMessageGated,
        (void *)(uintptr_t)apple80211Msg, msgData, (void *)(uintptr_t)msgDataLen);
}

void AirportItlwm::watchdogAction(IOTimerEventSource *timer)
{
    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
    struct ieee80211com *ic = fHalService->get80211Controller();
    RT_SET(8);
    sRT.wdCount++;
    sRT.ic_state = ic->ic_state;
    sRT.if_flags = ifp->if_flags;
    sRT.power_state = power_state;
    sRT.linkStatus = currentStatus;
    sRT.ic_flags = ic->ic_flags;
    sRT.ic_des_esslen = ic->ic_des_esslen;
    if (fNetIf) {
        ifnet_t bif = fNetIf->getBSDInterface();
        sRT.bsdIfPtr = (uint64_t)(uintptr_t)bif;
        if (bif) {
            sRT.bsdIfFlags = ifnet_flags(bif);
            sRT.bsdIfMtu = ifnet_mtu(bif);
        }
    }
    // Count scan tree nodes — useful for diagnosing association failures
    {
        uint32_t cnt = 0;
        struct ieee80211_node *ni;
        RB_FOREACH(ni, ieee80211_tree, &ic->ic_tree) cnt++;
        sRT.nodeCount = cnt;
    }
    static int wd_count = 0;
    static int wd_last_state = -1;
    wd_count++;
    if (wd_count <= 30 || wd_count % 10 == 0 || ic->ic_state != wd_last_state) {
        XYLog("DEBUG %s [%d] ic_state=%d if_flags=0x%x power_state=%u pmPowerState=%u link=0x%x\n",
              __FUNCTION__, wd_count, ic->ic_state, ifp->if_flags, power_state, pmPowerState, currentStatus);
        wd_last_state = ic->ic_state;
    }
    (*ifp->if_watchdog)(ifp);
    watchdogTimer->setTimeoutMS(kWatchDogTimerPeriod);
}

void AirportItlwm::fakeScanDone(OSObject *owner, IOTimerEventSource *sender)
{
    RT_SET(13);
    sRT.scanCount++;
    UInt32 msg = 0;
    AirportItlwm *that = (AirportItlwm *)owner;
    struct ieee80211com *ic = that->fHalService->get80211Controller();
    XYLog("DEBUG %s ic_state=%d posting SCAN_DONE + BGSCAN_CACHED_NETWORK_AVAILABLE\n", __FUNCTION__, ic->ic_state);
    that->postMessage(that->fNetIf, APPLE80211_M_SCAN_DONE, &msg, 4, true);
    that->postMessage(that->fNetIf, APPLE80211_M_BGSCAN_CACHED_NETWORK_AVAILABLE, NULL, 0, true);
}

bool AirportItlwm::isCommandProhibited(int command)
{
    RT_SET(11);
    sRT.ioctlCount++;
    sRT.lastIoctl = command;
    return false;
}

bool AirportItlwm::init(OSDictionary *properties)
{
    bool ret = super::init(properties);
    awdlSyncEnable = true;
    power_state = 0;
    driverLogStream = nullptr;
    fTxPool = NULL;
    fRxPool = NULL;
    fTxQueue = NULL;
    fRxQueue = NULL;
    memset(geo_location_cc, 0, sizeof(geo_location_cc));
    RT_SET(15);
    XYLog("DEBUG %s power_state=%u ret=%d\n", __FUNCTION__, power_state, ret);
    return ret;
}

IOService* AirportItlwm::probe(IOService *provider, SInt32 *score)
{
    XYLog("DEBUG %s entry provider=%p\n", __PRETTY_FUNCTION__, provider);

    int delay_secs = 0;
    if (PE_parse_boot_argn("itlwm_delay", &delay_secs, sizeof(delay_secs)) && delay_secs > 0) {
        XYLog("DEBUG %s itlwm_delay=%d — pausing before probe...\n", __FUNCTION__, delay_secs);
        IOSleep(delay_secs * 1000);
    }
    IOPCIEDeviceWrapper *wrapper = OSDynamicCast(IOPCIEDeviceWrapper, provider);
    if (!wrapper) {
        XYLog("DEBUG %s FAIL: Not a IOPCIEDeviceWrapper instance\n", __FUNCTION__);
        return NULL;
    }
    pciNub = wrapper->pciNub;
    fHalService = wrapper->fHalService;
    if (!pciNub || !fHalService) {
        XYLog("DEBUG %s FAIL: pciNub=%p fHalService=%p\n", __FUNCTION__, pciNub, fHalService);
        return NULL;
    }
    XYLog("DEBUG %s OK: pciNub=%p fHalService=%p — calling super::probe (IO80211Controller)\n", __FUNCTION__, pciNub, fHalService);

    // Panic timer: catch hangs inside IO80211Controller::probe()
    // Captures global rtMask + key pointers so one crash report is enough.
    struct ProbeDiag {
        volatile bool     returned;
        void             *self;
        void             *provider;
        void             *pciNub;
        void             *halService;
    };
    static ProbeDiag sPD = {};
    sPD = { false, this, provider, pciNub, fHalService };

    thread_call_t probeTimer = thread_call_allocate(
        [](thread_call_param_t ctx, thread_call_param_t) {
            ProbeDiag *d = (ProbeDiag *)ctx;
            if (!d->returned)
                panic("AirportItlwm::probe hung  "
                      "rtMask=0x%05x | self=%p prov=%p pci=%p hal=%p | "
                      "ic=%d fl=0x%x pwr=%u link=0x%x "
                      "evt=%u(last=%d) pm=%u wd=%u scan=%u",
                      sRT.rtMask, d->self, d->provider,
                      d->pciNub, d->halService,
                      sRT.ic_state, sRT.if_flags, sRT.power_state,
                      sRT.linkStatus,
                      sRT.evtCount, sRT.lastEvtCode,
                      sRT.postMsgCount, sRT.wdCount, sRT.scanCount);
        }, &sPD);
    uint64_t probeDeadline;
    clock_interval_to_deadline(60, kSecondScale, &probeDeadline);
    thread_call_enter_delayed(probeTimer, probeDeadline);

    IOService *result = super::probe(provider, score);

    sPD.returned = true;
    thread_call_cancel(probeTimer);
    thread_call_free(probeTimer);
    XYLog("DEBUG %s super::probe returned %p\n", __FUNCTION__, result);
    return result;
}

#define LOWER32(x)  ((uint64_t)(x) & 0xffffffff)
#define HIGHER32(x) ((uint64_t)(x) >> 32)

bool AirportItlwm::
initCCLogs()
{
    // ---------------------------------------------------------------
    // CoreCapture lifecycle diagnostic — bitmask tracks each factory
    // call so a single panic report pinpoints the exact failure.
    //
    // Bit  Hex   Phase
    //  0   0x001  initCCLogs entered
    //  1   0x002  driverLogPipe created
    //  2   0x004  driverDataPathPipe created
    //  3   0x008  driverSnapshotsPipe created
    //  4   0x010  driverFaultReporter (CCStream) created
    //  5   0x020  OSDynamicCast(CCDataStream) OK
    //  6   0x040  frWorkloop created
    //  7   0x080  CCFaultReporter::withStreamWorkloop OK
    //  8   0x100  IO80211FaultReporter::allocWithParams OK
    //  9   0x200  driverLogStream (CCLogStream) created
    // 10   0x400  initCCLogs returning true
    // ---------------------------------------------------------------
    struct CCDiag {
        volatile uint32_t mask;
        void *pciNub;
        void *logPipe;
        void *dataPathPipe;
        void *snapshotsPipe;
        void *faultStream;     // CCStream* from withPipeAndName
        void *dataStream;      // CCDataStream* after OSDynamicCast
        void *frWorkloop;
        void *ccFaultReporter;
        void *io80211FR;
        void *logStream;
    };
    static CCDiag sCCDiag = {};
    sCCDiag = {};
    sCCDiag.pciNub = pciNub;
#define CC_SET(bit) do { sCCDiag.mask |= (1u << (bit)); } while(0)

    CC_SET(0);

    CCPipeOptions driverLogOptions = { 0 };
    driverLogOptions.pipe_type = 0;
    driverLogOptions.log_data_type = 1;
    driverLogOptions.pipe_size = 0x200000;
    driverLogOptions.min_log_size_notify = 0xccccc;
    driverLogOptions.notify_threshold = 1000;
    strlcpy(driverLogOptions.file_name, "Itlwm_Logs", sizeof(driverLogOptions.file_name));
    snprintf(driverLogOptions.name, sizeof(driverLogOptions.name), "wlan%d", 0);
    strlcpy(driverLogOptions.directory_name, "WiFi", sizeof(driverLogOptions.directory_name));
    driverLogOptions.pad9 = 0x1000000;
    driverLogOptions.pad10 = 2;
    driverLogOptions.file_options = 0;
    driverLogOptions.log_policy = 0;
    // Use pciNub as owner (not this) — Apple's BCM driver uses the bus interface as CCPipe owner.
    // Using the IO80211Controller as owner conflicts with CoreCapture registration in
    // IO80211Controller::setupControlPathLogging(), causing a deadlock during super::start().
    driverLogPipe = CCPipe::withOwnerNameCapacity(pciNub, "com.zxystd.AirportItlwm", "DriverLogs", &driverLogOptions);
    sCCDiag.logPipe = driverLogPipe;
    if (driverLogPipe) CC_SET(1);

    memset(&driverLogOptions, 0, sizeof(driverLogOptions));
    driverLogOptions.pipe_type = 0;
    driverLogOptions.log_data_type = 0;
    driverLogOptions.pipe_size = 0x200000;
    driverLogOptions.min_log_size_notify = 0xccccc;
    driverLogOptions.notify_threshold = 1000;
    strlcpy(driverLogOptions.file_name, "AppleBCMWLAN_Datapath", sizeof(driverLogOptions.file_name));
    strlcpy(driverLogOptions.directory_name, "WiFi", sizeof(driverLogOptions.directory_name));
    driverLogOptions.pad9 = HIGHER32(0x202800000);
    driverLogOptions.pad10 = LOWER32(0x202800000);
    driverLogOptions.file_options = 0;
    driverLogOptions.log_policy = 0;
    driverDataPathPipe = CCPipe::withOwnerNameCapacity(pciNub, "com.zxystd.AirportItlwm", "DatapathEvents", &driverLogOptions);
    sCCDiag.dataPathPipe = driverDataPathPipe;
    if (driverDataPathPipe) CC_SET(2);

    memset(&driverLogOptions, 0, sizeof(driverLogOptions));
    driverLogOptions.pipe_type = 0x200000001;
    driverLogOptions.log_data_type = 2;
    strlcpy(driverLogOptions.file_name, "StateSnapshots", sizeof(driverLogOptions.file_name));
    strlcpy(driverLogOptions.name, "0", sizeof(driverLogOptions.name));
    strlcpy(driverLogOptions.directory_name, "WiFi", sizeof(driverLogOptions.directory_name));
    driverLogOptions.pipe_size = 128;
    driverSnapshotsPipe = CCPipe::withOwnerNameCapacity(pciNub, "com.zxystd.AirportItlwm", "StateSnapshots", &driverLogOptions);
    sCCDiag.snapshotsPipe = driverSnapshotsPipe;
    if (driverSnapshotsPipe) CC_SET(3);

    CCStreamOptions faultReportOptions = { 0 };
    faultReportOptions.stream_type = 1;
    faultReportOptions.console_level = 0xFFFFFFFFFFFFFFFF;
    driverFaultReporter = CCStream::withPipeAndName(driverSnapshotsPipe, "FaultReporter", &faultReportOptions);
    sCCDiag.faultStream = driverFaultReporter;
    if (driverFaultReporter) CC_SET(4);

    // CCLogStream MUST be created before super::start() — IO80211Controller::start()
    // calls vtable[431] (getDriverLogStream) to get CCLogStream* for setGlobalLogger().
    {
        CCStreamOptions logStreamOptions = { 0 };
        logStreamOptions.stream_type = 0;
        logStreamOptions.console_level = 0xFFFFFFFFFFFFFFFF;
        CCStream *logStreamBase = CCStream::withPipeAndName(driverLogPipe, "DriverLogStream", &logStreamOptions);
        driverLogStream = OSDynamicCast(CCLogStream, logStreamBase);
        if (logStreamBase && !driverLogStream)
            logStreamBase->release();
    }
    sCCDiag.logStream = driverLogStream;
    if (driverLogStream) CC_SET(9);

    // Create IO80211FaultReporter from the fault-reporter data stream.
    // Reference: AppleBCMWLANLogger::init() creates CCDataStream via
    // CCStream::withPipeAndName(pipe, name, {stream_type=1}), casts with
    // safeMetaCast, then CCFaultReporter::withStreamWorkloop →
    // IO80211FaultReporter::allocWithParams.
    if (driverFaultReporter) {
        CCDataStream *dataStream = OSDynamicCast(CCDataStream, driverFaultReporter);
        sCCDiag.dataStream = dataStream;
        if (dataStream) {
            CC_SET(5);
            IOWorkLoop *frWorkloop = IOWorkLoop::workLoop();
            sCCDiag.frWorkloop = frWorkloop;
            if (frWorkloop) {
                CC_SET(6);
                CCFaultReporter *ccfr = CCFaultReporter::withStreamWorkloop(
                    dataStream, frWorkloop);
                sCCDiag.ccFaultReporter = ccfr;
                if (ccfr) {
                    CC_SET(7);
                    io80211FaultReporter = IO80211FaultReporter::allocWithParams(ccfr);
                    sCCDiag.io80211FR = io80211FaultReporter;
                    if (io80211FaultReporter) CC_SET(8);
                }
                frWorkloop->release();
            }
        }
    }

    bool ok = driverLogPipe && driverDataPathPipe && driverSnapshotsPipe
        && driverFaultReporter && io80211FaultReporter;
    if (ok) CC_SET(10);

    XYLog("%s mask=0x%03x | logPipe=%p dataPath=%p snap=%p "
          "faultStream=%p ds=%p wl=%p ccfr=%p io80211fr=%p logStream=%p\n",
          __FUNCTION__, sCCDiag.mask,
          sCCDiag.logPipe, sCCDiag.dataPathPipe, sCCDiag.snapshotsPipe,
          sCCDiag.faultStream, sCCDiag.dataStream, sCCDiag.frWorkloop,
          sCCDiag.ccFaultReporter, sCCDiag.io80211FR, sCCDiag.logStream);

    if (!ok) {
        panic("AirportItlwm::initCCLogs FAILED  ccMask=0x%03x rtMask=0x%07x rt2=0x%04x | "
              "pci=%p logPipe=%p dataPath=%p snap=%p "
              "faultStream=%p dataStream=%p wl=%p ccfr=%p io80211fr=%p logStream=%p | "
              "ic=%d fl=0x%x pwr=%u evt=%u pm=%u",
              sCCDiag.mask, sRT.rtMask, sRT.rtMask2, sCCDiag.pciNub,
              sCCDiag.logPipe, sCCDiag.dataPathPipe, sCCDiag.snapshotsPipe,
              sCCDiag.faultStream, sCCDiag.dataStream, sCCDiag.frWorkloop,
              sCCDiag.ccFaultReporter, sCCDiag.io80211FR, sCCDiag.logStream,
              sRT.ic_state, sRT.if_flags, sRT.power_state,
              sRT.evtCount, sRT.postMsgCount);
    }

    return ok;
#undef CC_SET
}

bool AirportItlwm::start(IOService *provider)
{
    XYLog("DEBUG %s [STEP 0] entry provider=%p\n", __PRETTY_FUNCTION__, provider);

    // boot-arg "itlwm_delay=N" — pause N seconds so verbose boot output
    // stays on screen long enough to read/photograph.
    int delay_secs = 0;
    if (PE_parse_boot_argn("itlwm_delay", &delay_secs, sizeof(delay_secs)) && delay_secs > 0) {
        XYLog("DEBUG %s itlwm_delay=%d — pausing to let you read the screen...\n", __FUNCTION__, delay_secs);
        IOSleep(delay_secs * 1000);
        XYLog("DEBUG %s delay done, continuing\n", __FUNCTION__);
    }

    struct IOSkywalkEthernetInterface::RegistrationInfo registInfo;
    int boot_value = 0;

    // ---------------------------------------------------------------
    // Diagnostic panic timer — bitmask tracks every lifecycle phase.
    // On timeout the panic message prints the mask + key pointers,
    // so a single crash report is enough to pinpoint the hang.
    //
    // Bit  Hex      Phase
    //  0   0x00001  initCCLogs entered
    //  1   0x00002  CCPipes created
    //  2   0x00004  driverLogStream OK
    //  3   0x00008  io80211FaultReporter OK
    //  4   0x00010  super::start entered
    //  5   0x00020  super::start returned OK
    //  6   0x00040  PCI configured
    //  7   0x00080  _fWorkloop OK
    //  8   0x00100  _fCommandGate OK
    //  9   0x00200  HAL attached
    // 10   0x00400  watchdog/scan timers OK
    // 11   0x00800  fNetIf init OK
    // 12   0x01000  fNetIf attach OK
    // 13   0x02000  attachInterface OK
    // 14   0x04000  bsdInterface OK
    // 15   0x08000  fNetIf->start OK
    // 16   0x10000  enableAdapter OK
    // 17   0x20000  registerService done (start complete)
    // ---------------------------------------------------------------
    struct StartDiag {
        volatile uint32_t mask;
        volatile int      step;          // legacy step counter
        void             *self;          // AirportItlwm*
        void             *logStream;
        void             *faultReporter;
        void             *workloop;
        void             *netIf;
        void             *bsdIf;
    };
    static StartDiag sDiag = {};
    sDiag = {};
    sDiag.self = this;

    thread_call_t panicTimer = thread_call_allocate(
        [](thread_call_param_t ctx, thread_call_param_t) {
            StartDiag *d = (StartDiag *)ctx;
            if (!(d->mask & 0x20000))
                panic("AirportItlwm::start hung  "
                      "sMask=0x%05x step=%d ss=%u "
                      "rtMask=0x%07x rt2=0x%04x rt3=0x%04x | "
                      "self=%p log=%p fault=%p wl=%p nif=%p bsd=%p | "
                      "ic=%d fl=0x%x pwr=%u link=0x%x "
                      "evt=%u(last=%d) pm=%u(req=%u) wd=%u | "
                      "ioctl=%u lastIo=%d lastPM=0x%x "
                      "ls=%d lsCnt=%u scan=%u pmCnt=%u | "
                      "scanReq=%u assoc=%u scanRes=%u "
                      "icfl=0x%x esslen=%u nodes=%u mfail=0x%x | "
                      "fVars=%p bsdIf=%p enCnt=%u disCnt=%u enRet=0x%x | "
                      "bsdFl=0x%x bsdMtu=%u | "
                      "pmPol=%p pmOffC=%u pmOnC=%u txDrop=%u "
                      "gateNullOff=%u gateNullOn=%u ackOff=%u ackOn=%u | "
                      "skNif=%p txP=%p rxP=%p txQ=%p rxQ=%p | "
                      "txCb=%u txS=%u txD=%u rxIn=%u rxOK=%u rxAF=%u rxEF=%u rxCb=%u | "
                      "nxProv=%p nifCtx=%p nxArena=%p async=%p "
                      "r90=%p r98=%p rA0=%p",
                      d->mask, d->step, sRT.startStep,
                      sRT.rtMask, sRT.rtMask2, sRT.rtMask3, d->self,
                      d->logStream, d->faultReporter,
                      d->workloop, d->netIf, d->bsdIf,
                      sRT.ic_state, sRT.if_flags, sRT.power_state,
                      sRT.linkStatus,
                      sRT.evtCount, sRT.lastEvtCode,
                      sRT.postMsgCount, sRT.lastPmReq, sRT.wdCount,
                      sRT.ioctlCount, sRT.lastIoctl,
                      sRT.lastPostMsg, sRT.lastLinkState,
                      sRT.linkSetCount, sRT.scanCount,
                      sRT.pmCount,
                      sRT.scanReqCount, sRT.assocCount, sRT.scanResCount,
                      sRT.ic_flags, sRT.ic_des_esslen,
                      sRT.nodeCount, sRT.matchFail,
                      (void *)(uintptr_t)sRT.fVarsPtr,
                      (void *)(uintptr_t)sRT.bsdIfPtr,
                      sRT.enableCnt, sRT.disableCnt, sRT.lastEnableRet,
                      sRT.bsdIfFlags, sRT.bsdIfMtu,
                      (void *)(uintptr_t)sRT.pmPolicyPtr,
                      sRT.pmOffCancelRet, sRT.pmOnCancelRet,
                      sRT.outputDropPwr,
                      sRT.pmOffGateNull, sRT.pmOnGateNull,
                      sRT.pmAckOffCnt, sRT.pmAckOnCnt,
                      (void *)(uintptr_t)sRT.fNetIfPtr,
                      (void *)(uintptr_t)sRT.fTxPoolPtr,
                      (void *)(uintptr_t)sRT.fRxPoolPtr,
                      (void *)(uintptr_t)sRT.fTxQueuePtr,
                      (void *)(uintptr_t)sRT.fRxQueuePtr,
                      sRT.txCbCnt, sRT.txPktSent, sRT.txPktDrop,
                      sRT.rxInputCnt, sRT.rxPktOK, sRT.rxAllocFail,
                      sRT.rxEnqFail, sRT.rxCbCnt,
                      (void *)(uintptr_t)sRT.nexusProvPtr,
                      (void *)(uintptr_t)sRT.nifCtxPtr,
                      (void *)(uintptr_t)sRT.nexusArenaPtr,
                      (void *)(uintptr_t)sRT.asyncSentinel,
                      (void *)(uintptr_t)sRT.regObj90,
                      (void *)(uintptr_t)sRT.regObj98,
                      (void *)(uintptr_t)sRT.regObjA0);
        }, &sDiag);
    uint64_t panicDeadline;
    clock_interval_to_deadline(60, kSecondScale, &panicDeadline);
    thread_call_enter_delayed(panicTimer, panicDeadline);
    XYLog("DEBUG %s panic timer armed (60s)\n", __FUNCTION__);
#define SD_SET(bit) do { sDiag.mask |= (1u << (bit)); } while(0)
#define DISARM_PANIC_TIMER() do { SD_SET(17); thread_call_cancel(panicTimer); thread_call_free(panicTimer); } while(0)

    UInt8 builtIn = 0;
    setProperty("built-in", OSData::withBytes(&builtIn, sizeof(builtIn)));
    setProperty("DriverKitDriver", kOSBooleanFalse);
#if __IO80211_TARGET >= __MAC_26_0
    XYLog("DEBUG %s [STEP 1] initCCLogs (Tahoe path)\n", __FUNCTION__);
    SD_SET(0); // initCCLogs entered
    if (!initCCLogs()) {
        XYLog("DEBUG %s [STEP 1] FAIL: CCLog init\n", __FUNCTION__);
        DISARM_PANIC_TIMER();
        return false;
    }
    SD_SET(1); // CCPipes created
    sDiag.logStream = driverLogStream;
    if (driverLogStream) SD_SET(2);
    sDiag.faultReporter = io80211FaultReporter;
    if (io80211FaultReporter) SD_SET(3);
    sDiag.step = 1;
#endif
    XYLog("DEBUG %s [STEP 2] super::start (logStream=%p faultRep=%p)\n",
          __FUNCTION__, driverLogStream, io80211FaultReporter);
    SD_SET(4); // super::start entered
    bool superResult = super::start(provider);
    if (!superResult) {
        XYLog("DEBUG %s [STEP 2] FAIL: super::start returned false\n", __FUNCTION__);
        DISARM_PANIC_TIMER();
        return false;
    }
    SD_SET(5); // super::start OK
    sDiag.step = 2;
    XYLog("DEBUG %s [STEP 3] PCI setup\n", __FUNCTION__);
    pciNub->setBusMasterEnable(true);
    pciNub->setIOEnable(true);
    pciNub->setMemoryEnable(true);
    pciNub->configWrite8(0x41, 0);
    if (pciNub->requestPowerDomainState(kIOPMPowerOn,
                                        (IOPowerConnection *) getParentEntry(gIOPowerPlane), IOPMLowestState) != IOPMNoErr) {
        XYLog("DEBUG %s [STEP 3] FAIL: requestPowerDomainState\n", __FUNCTION__);
        super::stop(provider);
        DISARM_PANIC_TIMER();
        return false;
    }
    if (initPCIPowerManagment(pciNub) == false) {
        XYLog("DEBUG %s [STEP 3] FAIL: initPCIPowerManagment\n", __FUNCTION__);
        super::stop(pciNub);
        DISARM_PANIC_TIMER();
        return false;
    }
    SD_SET(6); // PCI configured
    sDiag.step = 3;
    XYLog("DEBUG %s [STEP 4] workloop & command gate\n", __FUNCTION__);
    sDiag.workloop = _fWorkloop;
    if (_fWorkloop == NULL) {
        XYLog("DEBUG %s [STEP 4] FAIL: No _fWorkloop\n", __FUNCTION__);
        super::stop(pciNub);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    SD_SET(7); // _fWorkloop OK
    _fCommandGate = IOCommandGate::commandGate(this, (IOCommandGate::Action)AirportItlwm::tsleepHandler);
    if (_fCommandGate == 0) {
        XYLog("DEBUG %s [STEP 4] FAIL: No command gate\n", __FUNCTION__);
        super::stop(pciNub);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    SD_SET(8); // _fCommandGate OK
    _fWorkloop->addEventSource(_fCommandGate);
    const IONetworkMedium *primaryMedium;
    if (!createMediumTables(&primaryMedium) ||
        !setCurrentMedium(primaryMedium) || !setSelectedMedium(primaryMedium)) {
        XYLog("DEBUG %s [STEP 4] FAIL: setup medium\n", __FUNCTION__);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    sDiag.step = 4;
    XYLog("DEBUG %s [STEP 5] HAL initWithController + attach\n", __FUNCTION__);
    fHalService->initWithController(this, _fWorkloop, _fCommandGate);
    fHalService->get80211Controller()->ic_event_handler = eventHandler;

    if (PE_parse_boot_argn("-novht", &boot_value, sizeof(boot_value)))
        fHalService->get80211Controller()->ic_userflags |= IEEE80211_F_NOVHT;
    if (PE_parse_boot_argn("-noht40", &boot_value, sizeof(boot_value)))
        fHalService->get80211Controller()->ic_userflags |= IEEE80211_F_NOHT40;

    if (!fHalService->attach(pciNub)) {
        XYLog("DEBUG %s [STEP 5] FAIL: HAL attach\n", __FUNCTION__);
        super::stop(pciNub);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    SD_SET(9); // HAL attached
    sDiag.step = 6;
    XYLog("DEBUG %s [STEP 6] watchdog + scan timers\n", __FUNCTION__);
    fWatchdogWorkLoop = IOWorkLoop::workLoop();
    if (fWatchdogWorkLoop == NULL) {
        XYLog("DEBUG %s [STEP 6] FAIL: watchdog workloop\n", __FUNCTION__);
        fHalService->detach(pciNub);
        super::stop(pciNub);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    watchdogTimer = IOTimerEventSource::timerEventSource(this, OSMemberFunctionCast(IOTimerEventSource::Action, this, &AirportItlwm::watchdogAction));
    if (!watchdogTimer) {
        XYLog("DEBUG %s [STEP 6] FAIL: watchdog timer\n", __FUNCTION__);
        fHalService->detach(pciNub);
        super::stop(pciNub);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    fWatchdogWorkLoop->addEventSource(watchdogTimer);
    scanSource = IOTimerEventSource::timerEventSource(this, &fakeScanDone);
    _fWorkloop->addEventSource(scanSource);
    scanSource->enable();

    SD_SET(10); // watchdog/scan timers OK
    sDiag.step = 7;
    XYLog("DEBUG %s [STEP 7] Skywalk interface init + attach\n", __FUNCTION__);
    fNetIf = new AirportItlwmSkywalkInterface;
#if __IO80211_TARGET >= __MAC_26_0
    IOEthernetAddress addr;
    getHardwareAddress(&addr);
    if (!fNetIf->init(this, (ether_addr *)&addr)) {
#else
    if (!fNetIf->init(this)) {
#endif
        XYLog("DEBUG %s [STEP 7] FAIL: Skywalk interface init\n", __FUNCTION__);
        super::stop(provider);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    SD_SET(11); // fNetIf init OK
    sDiag.netIf = fNetIf;
    sRT.fNetIfPtr = (uint64_t)(uintptr_t)fNetIf;
    fNetIf->setInterfaceRole(1);
    fNetIf->setInterfaceId(1);

#if __IO80211_TARGET < __MAC_26_0
    if (!initCCLogs()) {
        XYLog("DEBUG %s [STEP 7] FAIL: CCLog init (pre-Tahoe)\n", __FUNCTION__);
        super::stop(provider);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
#endif
    if (!fNetIf->attach(this)) {
        XYLog("DEBUG %s [STEP 7] FAIL: fNetIf attach\n", __FUNCTION__);
        super::stop(provider);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    SD_SET(12); // fNetIf attach OK
    sDiag.step = 8;
    XYLog("DEBUG %s [STEP 8] attachInterface + BSD interface\n", __FUNCTION__);
    if (!attachInterface(fNetIf, this)) {
        XYLog("DEBUG %s [STEP 8] FAIL: attachInterface\n", __FUNCTION__);
        super::stop(provider);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    SD_SET(13); // attachInterface OK

    // --- Skywalk registration: proper Sequoia path ---
    //
    // On macOS Sequoia (26.x), IONetworkStack no longer matches IOEthernetInterface
    // for WiFi controllers.  BSD ifnet creation goes through:
    //   registerEthernetInterface → creates LogicalLink + nexus provider
    //   deferBSDAttach(false) → registerService on interface
    //   IOSkywalkNetworkBSDClient matches → creates BSD ifnet via nexus
    //
    // Reference: Apple BCM WiFi driver calls registerInfraEthernetInterface
    // (IO80211InfraInterface non-virtual) which internally calls
    // registerEthernetInterface on IOSkywalkEthernetInterface.

    sRT.startStep = 80;
    memset(&registInfo, 0, sizeof(registInfo));
    if (!fNetIf->initRegistrationInfo(&registInfo, 1, sizeof(registInfo))) {
        XYLog("DEBUG %s [STEP 8] FAIL: initRegistrationInfo\n", __FUNCTION__);
        super::stop(provider);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    RT3_SET(0); // initRegistrationInfo OK
    {
        uint8_t *p = (uint8_t *)&registInfo;
        XYLog("DEBUG %s [STEP 8] initRegistrationInfo OK, size=%lu\n",
              __FUNCTION__, sizeof(registInfo));
        XYLog("DEBUG %s [STEP 8] regInfo[0-31]: %02x %02x %02x %02x %02x %02x %02x %02x "
              "%02x %02x %02x %02x %02x %02x %02x %02x "
              "%02x %02x %02x %02x %02x %02x %02x %02x "
              "%02x %02x %02x %02x %02x %02x %02x %02x\n",
              __FUNCTION__,
              p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7],
              p[8],p[9],p[10],p[11],p[12],p[13],p[14],p[15],
              p[16],p[17],p[18],p[19],p[20],p[21],p[22],p[23],
              p[24],p[25],p[26],p[27],p[28],p[29],p[30],p[31]);
    }

    // mExpansionData / mExpansionData2 are managed by the framework's
    // initRegistrationInfo (called above at STEP 8).  Manual allocation
    // was removed because it wrote to the WRONG offsets when our class
    // size was 0x10 too small (0xD0 vs real 0xE0), corrupting framework state.
    // See plan: "Fix Skywalk Class Layout Mismatch (S1-S4 Architecture)".
    XYLog("DEBUG %s [STEP 8-post] mExpansionData=%p mExpansionData2=%p "
          "(should be non-NULL if framework initialized them)\n",
          __FUNCTION__, fNetIf->mExpansionData, fNetIf->mExpansionData2);
    RT3_SET(1); // post-initRegistrationInfo check

    // Create Skywalk packet buffer pools for TX and RX
    sRT.startStep = 81;
    {
        IOSkywalkPacketBufferPool::PoolOptions poolOpts = {};
        poolOpts.packetCount = 256;
        poolOpts.bufferCount = 256;
        poolOpts.bufferSize  = 2048;
        poolOpts.maxBuffersPerPacket = 1;

        fTxPool = IOSkywalkPacketBufferPool::withName("AirportItlwm-TX", fNetIf, 0, &poolOpts);
        fRxPool = IOSkywalkPacketBufferPool::withName("AirportItlwm-RX", fNetIf, 0, &poolOpts);
        XYLog("DEBUG %s [STEP 8b] pools: TX=%p RX=%p\n", __FUNCTION__, fTxPool, fRxPool);
        if (!fTxPool || !fRxPool) {
            XYLog("DEBUG %s [STEP 8b] FAIL: pool creation (TX=%p RX=%p)\n",
                  __FUNCTION__, fTxPool, fRxPool);
            super::stop(provider);
            releaseAll();
            DISARM_PANIC_TIMER();
            return false;
        }
    }
    RT3_SET(2); // pools created
    sRT.fTxPoolPtr = (uint64_t)(uintptr_t)fTxPool;
    sRT.fRxPoolPtr = (uint64_t)(uintptr_t)fRxPool;

    // Create Skywalk TX submission and RX completion queues
    fTxQueue = IOSkywalkTxSubmissionQueue::withPool(fTxPool, 256, 0, this,
                                                    skywalkTxAction, NULL, 0);
    fRxQueue = IOSkywalkRxCompletionQueue::withPool(fRxPool, 256, 0, this,
                                                    skywalkRxAction, NULL, 0);
    XYLog("DEBUG %s [STEP 8c] queues: TX=%p RX=%p\n", __FUNCTION__, fTxQueue, fRxQueue);
    if (!fTxQueue || !fRxQueue) {
        XYLog("DEBUG %s [STEP 8c] FAIL: queue creation (TX=%p RX=%p)\n",
              __FUNCTION__, fTxQueue, fRxQueue);
        super::stop(provider);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    RT3_SET(3); // queues created
    sRT.fTxQueuePtr = (uint64_t)(uintptr_t)fTxQueue;
    sRT.fRxQueuePtr = (uint64_t)(uintptr_t)fRxQueue;

    // Wire up Skywalk RX input handler on the internal ifnet before
    // registration, so received frames go through the Skywalk path
    // instead of the legacy IOEthernetInterface::inputPacket path.
#if __IO80211_TARGET >= __MAC_26_0
    {
        struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
        ifp->if_skywalk_rx = skywalkRxInput;
        XYLog("DEBUG %s [STEP 8c-rx] wired if_skywalk_rx=%p on ifp=%p\n",
              __FUNCTION__, (void *)(uintptr_t)skywalkRxInput, ifp);
    }
#endif

    // Register the interface through the proper Skywalk path.
    // registerEthernetInterface internally creates a LogicalLink from the queues
    // and calls registerNetworkInterfaceWithLogicalLink, populating fVars[0] and
    // setting up the nexus provider for IOSkywalkNetworkBSDClient matching.
    sRT.startStep = 82;

    // Pre-registration diagnostics: dump critical kernel-side fields at the
    // offsets the framework expects.  These offsets come from Ghidra analysis
    // of IOSkywalkFamily.kext 26.3:
    //   +0xC0 = NIF_Context (ExpansionData for IOSkywalkNetworkInterface)
    //   +0xC8 = nexusProvider (must be non-NULL for core registration at FUN_0xa37640)
    //   +0xD0 = nexus arena
    //   +0x118 = mExpansionData2 (EthernetRegistrationContext, read by registerEthernetInterface)
    // Dump nexusProvider and surrounding fields (per YAML 91).
    // These offsets are critical for understanding registration success/failure.
    {
        uint8_t *raw = (uint8_t *)fNetIf;
        void *nifCtx      = *(void **)(raw + 0xC0);
        void *nexusProv   = *(void **)(raw + 0xC8);
        void *nexusArena  = *(void **)(raw + 0xD0);
        void *ethRegCtx   = *(void **)(raw + 0x118);
        void *asyncSent   = *(void **)(raw + 0xB8);
        void *regObj90    = *(void **)(raw + 0x90);
        void *regObj98    = *(void **)(raw + 0x98);
        void *regObjA0    = *(void **)(raw + 0xA0);

        // Persist in RuntimeDiag for panic timer visibility
        sRT.nifCtxPtr     = (uint64_t)(uintptr_t)nifCtx;
        sRT.nexusProvPtr  = (uint64_t)(uintptr_t)nexusProv;
        sRT.nexusArenaPtr = (uint64_t)(uintptr_t)nexusArena;
        sRT.asyncSentinel = (uint64_t)(uintptr_t)asyncSent;
        sRT.regObj90      = (uint64_t)(uintptr_t)regObj90;
        sRT.regObj98      = (uint64_t)(uintptr_t)regObj98;
        sRT.regObjA0      = (uint64_t)(uintptr_t)regObjA0;

        XYLog("DEBUG %s [PRE-REG] fNetIf=%p raw offsets:\n"
              "  +0x90 (queueSet)=%p  +0x98 (queueObj)=%p  +0xA0 (queueMgr)=%p\n"
              "  +0xB8 (asyncSentinel)=%p\n"
              "  +0xC0 (NIF_Context)=%p\n"
              "  +0xC8 (nexusProvider)=%p\n"
              "  +0xD0 (nexusArena)=%p\n"
              "  +0x118 (mExpansionData2/EthRegCtx)=%p\n",
              __FUNCTION__, fNetIf,
              regObj90, regObj98, regObjA0,
              asyncSent, nifCtx, nexusProv, nexusArena, ethRegCtx);
        if (ethRegCtx) {
            void *regInfoPtr = *(void **)ethRegCtx;
            XYLog("DEBUG %s [PRE-REG] *(+0x118)->fRegistrationInfo=%p\n",
                  __FUNCTION__, regInfoPtr);
        }
    }

    {
        IOSkywalkPacketQueue *queues[] = {
            (IOSkywalkPacketQueue *)fTxQueue,
            (IOSkywalkPacketQueue *)fRxQueue
        };
        bool regOK = fNetIf->registerEthernetInterface(
            (const IOSkywalkEthernetInterface::RegistrationInfo *)&registInfo,
            queues, 2, fTxPool, fRxPool, 0);
        XYLog("DEBUG %s [STEP 8d] registerEthernetInterface=%d\n", __FUNCTION__, regOK);
        if (!regOK) {
            XYLog("DEBUG %s [STEP 8d] FAIL: registerEthernetInterface\n", __FUNCTION__);
            super::stop(provider);
            releaseAll();
            DISARM_PANIC_TIMER();
            return false;
        }
    }
    RT3_SET(4); // registerEthernetInterface OK
    SD_SET(14);

    // Start the Skywalk interface
    sDiag.step = 81;
    RT3_SET(10); // entering fNetIf->start
    XYLog("DEBUG %s [STEP 8e] calling fNetIf->start(this=%p)\n", __FUNCTION__, this);
    fNetIf->start(this);
    RT3_SET(11); // fNetIf->start returned
    SD_SET(15); // fNetIf->start OK

    // Trigger IOSkywalkNetworkBSDClient matching.
    // deferBSDAttach(false) removes IODeferBSDAttach property and calls
    // registerService() on fNetIf, causing IOKit to match BSDClient.
    // BSDClient::start creates the nexus channel and BSD ifnet.
    XYLog("DEBUG %s [STEP 8f] calling deferBSDAttach(false)\n", __FUNCTION__);
    fNetIf->deferBSDAttach(false);
    {
        const char *bsdName = fNetIf->getBSDName();
        ifnet_t bsdIf = fNetIf->getBSDInterface();
        sRT.bsdIfPtr = (uint64_t)(uintptr_t)bsdIf;
        if (bsdIf) RT2_SET(0);
        if (bsdName && bsdName[0]) RT2_SET(1);
        XYLog("DEBUG %s [STEP 8f] deferBSDAttach done, bsdName=%s bsdIf=%p rt2=0x%04x rt3=0x%04x\n",
              __FUNCTION__, bsdName ? bsdName : "(null)", bsdIf, sRT.rtMask2, sRT.rtMask3);
    }

    sDiag.step = 9;
    XYLog("DEBUG %s [STEP 9] enableAdapter\n", __FUNCTION__);
    setLinkStatus(kIONetworkLinkValid);
    if (TAILQ_EMPTY(&fHalService->get80211Controller()->ic_ess))
        fHalService->get80211Controller()->ic_flags |= IEEE80211_F_AUTO_JOIN;
    _fCommandGate->enable();
    power_state = kWiFiPowerOn;
    XYLog("DEBUG %s [STEP 9] enabling adapter, power_state=%u\n", __FUNCTION__, power_state);
    enableAdapter(NULL);
    {
        struct ieee80211com *ic_dbg = fHalService->get80211Controller();
        struct _ifnet *ifp_dbg = &ic_dbg->ic_ac.ac_if;
        XYLog("DEBUG %s [STEP 9a] post-enable: ic_state=%d if_flags=0x%x ic_caps=0x%x ic_opmode=%d\n",
              __FUNCTION__, ic_dbg->ic_state, ifp_dbg->if_flags, ic_dbg->ic_caps, ic_dbg->ic_opmode);
    }
    SD_SET(16); // enableAdapter OK
    sDiag.step = 10;
    registerService();
    RT_SET(18);
    XYLog("DEBUG %s start COMPLETE mask=0x%05x\n", __FUNCTION__, sDiag.mask | 0x20000);
    DISARM_PANIC_TIMER();
#undef DISARM_PANIC_TIMER
#undef SD_SET
    return true;
}

void AirportItlwm::stop(IOService *provider)
{
    RT_SET(19);
    sRT.stopStep = 1;
    XYLog("DEBUG %s [1] entry power_state=%u pmPowerState=%u provider=%p fHalService=%p "
          "rtMask=0x%07x\n",
          __PRETTY_FUNCTION__, power_state, pmPowerState, provider, fHalService, sRT.rtMask);

    // Panic timer — catches hangs in the teardown path.
    thread_call_t stopTimer = thread_call_allocate(
        [](thread_call_param_t, thread_call_param_t) {
            panic("AirportItlwm::stop hung  "
                  "stopStep=%u rtMask=0x%07x rt2=0x%04x rt3=0x%04x | "
                  "ic=%d fl=0x%x pwr=%u link=0x%x | "
                  "evt=%u(last=%d) pm=%u(req=%u) wd=%u ioctl=%u(last=%d) "
                  "scanDone=%u scan=%u pmCnt=%u ls=%d lsCnt=%u "
                  "ifType=0x%x skFree=%u free=%u ss=%u | "
                  "scanReq=%u assoc=%u scanRes=%u "
                  "icfl=0x%x esslen=%u nodes=%u mfail=0x%x | "
                  "fVars=%p bsdIf=%p enCnt=%u disCnt=%u enRet=0x%x | "
                  "bsdFl=0x%x bsdMtu=%u | "
                  "pmPol=%p pmOffC=%u pmOnC=%u txDrop=%u "
                  "gateNullOff=%u gateNullOn=%u ackOff=%u ackOn=%u | "
                  "skNif=%p txP=%p rxP=%p txQ=%p rxQ=%p | "
                  "txCb=%u txS=%u txD=%u rxIn=%u rxOK=%u rxAF=%u rxEF=%u rxCb=%u | "
                  "nxProv=%p nifCtx=%p nxArena=%p async=%p "
                  "r90=%p r98=%p rA0=%p",
                  sRT.stopStep, sRT.rtMask, sRT.rtMask2, sRT.rtMask3,
                  sRT.ic_state, sRT.if_flags, sRT.power_state,
                  sRT.linkStatus,
                  sRT.evtCount, sRT.lastEvtCode,
                  sRT.postMsgCount, sRT.lastPmReq, sRT.wdCount,
                  sRT.ioctlCount, sRT.lastIoctl,
                  sRT.scanDoneCount, sRT.scanCount, sRT.pmCount,
                  sRT.lastLinkState, sRT.linkSetCount,
                  sRT.ifType, sRT.skFreeStep, sRT.freeStep,
                  sRT.startStep,
                  sRT.scanReqCount, sRT.assocCount, sRT.scanResCount,
                  sRT.ic_flags, sRT.ic_des_esslen,
                  sRT.nodeCount, sRT.matchFail,
                  (void *)(uintptr_t)sRT.fVarsPtr,
                  (void *)(uintptr_t)sRT.bsdIfPtr,
                  sRT.enableCnt, sRT.disableCnt, sRT.lastEnableRet,
                  sRT.bsdIfFlags, sRT.bsdIfMtu,
                  (void *)(uintptr_t)sRT.pmPolicyPtr,
                  sRT.pmOffCancelRet, sRT.pmOnCancelRet,
                  sRT.outputDropPwr,
                  sRT.pmOffGateNull, sRT.pmOnGateNull,
                  sRT.pmAckOffCnt, sRT.pmAckOnCnt,
                  (void *)(uintptr_t)sRT.fNetIfPtr,
                  (void *)(uintptr_t)sRT.fTxPoolPtr,
                  (void *)(uintptr_t)sRT.fRxPoolPtr,
                  (void *)(uintptr_t)sRT.fTxQueuePtr,
                  (void *)(uintptr_t)sRT.fRxQueuePtr,
                  sRT.txCbCnt, sRT.txPktSent, sRT.txPktDrop,
                  sRT.rxInputCnt, sRT.rxPktOK, sRT.rxAllocFail,
                  sRT.rxEnqFail, sRT.rxCbCnt,
                  (void *)(uintptr_t)sRT.nexusProvPtr,
                  (void *)(uintptr_t)sRT.nifCtxPtr,
                  (void *)(uintptr_t)sRT.nexusArenaPtr,
                  (void *)(uintptr_t)sRT.asyncSentinel,
                  (void *)(uintptr_t)sRT.regObj90,
                  (void *)(uintptr_t)sRT.regObj98,
                  (void *)(uintptr_t)sRT.regObjA0);
        }, NULL);
    uint64_t stopDeadline;
    clock_interval_to_deadline(60, kSecondScale, &stopDeadline);
    thread_call_enter_delayed(stopTimer, stopDeadline);

    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
    sRT.stopStep = 2;
    XYLog("DEBUG %s [2] disableAdapter\n", __FUNCTION__);
    disableAdapter(NULL);
    sRT.stopStep = 3;
    XYLog("DEBUG %s [3] setLinkStatus\n", __FUNCTION__);
    setLinkStatus(kIONetworkLinkValid);
    sRT.stopStep = 4;
    XYLog("DEBUG %s [4] fHalService->detach pciNub=%p\n", __FUNCTION__, pciNub);
    fHalService->detach(pciNub);
    sRT.stopStep = 5;
    XYLog("DEBUG %s [5] ether_ifdetach ifp=%p\n", __FUNCTION__, ifp);
#if __IO80211_TARGET >= __MAC_26_0
    ifp->if_skywalk_rx = NULL;
#endif
    ether_ifdetach(ifp);
    sRT.stopStep = 6;
    // Release Skywalk queues and pools
    XYLog("DEBUG %s [6] releasing Skywalk queues/pools\n", __FUNCTION__);
    OSSafeReleaseNULL(fTxQueue);  sRT.fTxQueuePtr = 0;
    OSSafeReleaseNULL(fRxQueue);  sRT.fRxQueuePtr = 0;
    OSSafeReleaseNULL(fTxPool);   sRT.fTxPoolPtr = 0;
    OSSafeReleaseNULL(fRxPool);   sRT.fRxPoolPtr = 0;
    sRT.stopStep = 61;
    RT3_SET(15); // detachInterface entered
    XYLog("DEBUG %s [6b] detachInterface fNetIf=%p\n", __FUNCTION__, fNetIf);
    detachInterface(fNetIf, true);
    sRT.stopStep = 7;
    XYLog("DEBUG %s [7] release fNetIf\n", __FUNCTION__);
    OSSafeReleaseNULL(fNetIf);    sRT.fNetIfPtr = 0;
    sRT.stopStep = 8;
    XYLog("DEBUG %s [8] releaseAll\n", __FUNCTION__);
    releaseAll();
    sRT.stopStep = 9;
    XYLog("DEBUG %s [9] super::stop\n", __FUNCTION__);
    super::stop(provider);
    sRT.stopStep = 10;
    XYLog("DEBUG %s [10] DONE\n", __FUNCTION__);
    thread_call_cancel(stopTimer);
    thread_call_free(stopTimer);
}

void AirportItlwm::free()
{
    RT_SET(20);
    sRT.freeStep = 1;
    XYLog("DEBUG %s [1] entry fHalService=%p syncFrameTemplate=%p roamProfile=%p btcProfile=%p "
          "rtMask=0x%07x\n",
          __PRETTY_FUNCTION__, fHalService, syncFrameTemplate, roamProfile, btcProfile, sRT.rtMask);

    // Panic timer — catches hangs in IO80211Controller::free() chain
    thread_call_t freeTimer = thread_call_allocate(
        [](thread_call_param_t, thread_call_param_t) {
            if (!(sRT.rtMask & 0x200000))
                panic("AirportItlwm::free hung  "
                      "freeStep=%u rtMask=0x%07x rt2=0x%04x rt3=0x%04x | "
                      "stopStep=%u skFree=%u ss=%u | "
                      "ic=%d fl=0x%x pwr=%u link=0x%x | "
                      "evt=%u pm=%u(req=%u) wd=%u ioctl=%u(last=%d) "
                      "scanDone=%u ifType=0x%x | "
                      "scanReq=%u assoc=%u scanRes=%u "
                      "icfl=0x%x esslen=%u nodes=%u mfail=0x%x | "
                      "fVars=%p bsdIf=%p enCnt=%u disCnt=%u enRet=0x%x | "
                      "bsdFl=0x%x bsdMtu=%u | "
                      "pmPol=%p pmOffC=%u pmOnC=%u txDrop=%u "
                      "gateNullOff=%u gateNullOn=%u ackOff=%u ackOn=%u | "
                      "skNif=%p txP=%p rxP=%p txQ=%p rxQ=%p",
                      sRT.freeStep, sRT.rtMask, sRT.rtMask2, sRT.rtMask3,
                      sRT.stopStep, sRT.skFreeStep, sRT.startStep,
                      sRT.ic_state, sRT.if_flags, sRT.power_state,
                      sRT.linkStatus,
                      sRT.evtCount, sRT.postMsgCount, sRT.lastPmReq,
                      sRT.wdCount,
                      sRT.ioctlCount, sRT.lastIoctl,
                      sRT.scanDoneCount, sRT.ifType,
                      sRT.scanReqCount, sRT.assocCount, sRT.scanResCount,
                      sRT.ic_flags, sRT.ic_des_esslen,
                      sRT.nodeCount, sRT.matchFail,
                      (void *)(uintptr_t)sRT.fVarsPtr,
                      (void *)(uintptr_t)sRT.bsdIfPtr,
                      sRT.enableCnt, sRT.disableCnt, sRT.lastEnableRet,
                      sRT.bsdIfFlags, sRT.bsdIfMtu,
                      (void *)(uintptr_t)sRT.pmPolicyPtr,
                      sRT.pmOffCancelRet, sRT.pmOnCancelRet,
                      sRT.outputDropPwr,
                      sRT.pmOffGateNull, sRT.pmOnGateNull,
                      sRT.pmAckOffCnt, sRT.pmAckOnCnt,
                      (void *)(uintptr_t)sRT.fNetIfPtr,
                      (void *)(uintptr_t)sRT.fTxPoolPtr,
                      (void *)(uintptr_t)sRT.fRxPoolPtr,
                      (void *)(uintptr_t)sRT.fTxQueuePtr,
                      (void *)(uintptr_t)sRT.fRxQueuePtr);
        }, NULL);
    uint64_t freeDeadline;
    clock_interval_to_deadline(60, kSecondScale, &freeDeadline);
    thread_call_enter_delayed(freeTimer, freeDeadline);

    sRT.freeStep = 2;
    if (fHalService != NULL) {
        XYLog("DEBUG %s [2] releasing fHalService\n", __FUNCTION__);
        fHalService->release();
        fHalService = NULL;
    }
    sRT.freeStep = 3;
    if (syncFrameTemplate != NULL && syncFrameTemplateLength > 0) {
        IOFree(syncFrameTemplate, syncFrameTemplateLength);
        syncFrameTemplateLength = 0;
        syncFrameTemplate = NULL;
    }
    if (roamProfile != NULL) {
        IOFree(roamProfile, sizeof(struct apple80211_roam_profile_band_data));
        roamProfile = NULL;
    }
    if (btcProfile != NULL) {
        IOFree(btcProfile, sizeof(struct apple80211_btc_profiles_data));
        btcProfile = NULL;
    }
    sRT.freeStep = 4;
    XYLog("DEBUG %s [3] super::free\n", __FUNCTION__);
    super::free();
    RT_SET(21);
    thread_call_cancel(freeTimer);
    thread_call_free(freeTimer);
}

bool AirportItlwm::createWorkQueue()
{
    _fWorkloop = IO80211WorkQueue::workQueue();
    RT_SET(16);
    XYLog("DEBUG %s created IO80211WorkQueue _fWorkloop=%p\n", __FUNCTION__, _fWorkloop);
    return _fWorkloop != 0;
}

#if __IO80211_TARGET >= __MAC_26_0
IO80211WorkQueue *AirportItlwm::getWorkQueue() const
#else
IO80211WorkQueue *AirportItlwm::getWorkQueue()
#endif
{
    // getWorkQueue is called multiple times during IO80211Controller::start():
    //  #1 = createWorkQueue phase, #2 = CommandGate alloc (after RangingManager),
    //  #3 = inside CreatePostOffice, #4 = TimerSource alloc, #5 = TimerFactory alloc
    static int sGetWorkQueueCount = 0;
    if (++sGetWorkQueueCount <= 20)
        XYLog("DEBUG %s #%d returning %p\n", __FUNCTION__, sGetWorkQueueCount, _fWorkloop);
    return _fWorkloop;
}

void *AirportItlwm::getFaultReporterFromDriver()
{
    return io80211FaultReporter;
}

#if __IO80211_TARGET < __MAC_26_0
IOReturn AirportItlwm::enable(IO80211SkywalkInterface *netif)
{
    XYLog("DEBUG %s power_state=%u netif=%p bsdInterface=%p\n", __PRETTY_FUNCTION__, power_state, netif, bsdInterface);
    super::enable(netif);
    _fCommandGate->enable();
    if (power_state)
        enableAdapter(bsdInterface);
    else
        XYLog("DEBUG %s SKIPPED enableAdapter (power_state=0)\n", __FUNCTION__);
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::disable(IO80211SkywalkInterface *netif)
{
    XYLog("DEBUG %s power_state=%u\n", __PRETTY_FUNCTION__, power_state);
    disableAdapter(bsdInterface);
    super::disable(netif);
    setLinkStatus(kIONetworkLinkValid);
    return kIOReturnSuccess;
}
#endif

bool AirportItlwm::configureInterface(IONetworkInterface *netif)
{
    RT_SET(17);
    XYLog("DEBUG %s entry netif=%p power_state=%u\n", __FUNCTION__, netif, power_state);
    IONetworkData *nd;
    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;

    if (super::configureInterface(netif) == false) {
        XYLog("super failed\n");
        return false;
    }
    
    nd = netif->getParameter(kIONetworkStatsKey);
    if (!nd || !(fpNetStats = (IONetworkStats *)nd->getBuffer())) {
        XYLog("network statistics buffer unavailable?\n");
        return false;
    }
    ifp->netStat = fpNetStats;
    XYLog("DEBUG %s ether_ifattach ifp=%p netif=%p\n", __FUNCTION__, ifp, netif);
    ether_ifattach(ifp, OSDynamicCast(IOEthernetInterface, netif));
    RT_SET(26);
    fpNetStats->collisions = 0;
#if defined(__PRIVATE_SPI__) && __IO80211_TARGET < __MAC_26_0
    netif->configureOutputPullModel(fHalService->getDriverInfo()->getTxQueueSize(), 0, 0, IOEthernetInterface::kOutputPacketSchedulingModelNormal, 0);
#else
    XYLog("DEBUG %s Tahoe: skipping configureOutputPullModel\n", __FUNCTION__);
#endif
    XYLog("DEBUG %s DONE\n", __FUNCTION__);
    return true;
}

// On Sequoia (26.x), createInterface is not called by the Skywalk
// attachInterface(IOSkywalkInterface*, IOService*) path. BSD ifnet is
// created by IOSkywalkNetworkBSDClient after deferBSDAttach(false).
#if __IO80211_TARGET < __MAC_26_0
IONetworkInterface *AirportItlwm::createInterface()
{
    RT_SET(24);
    XYLog("DEBUG %s entry fNetIf=%p\n", __FUNCTION__, fNetIf);
    AirportItlwmEthernetInterface *netif = new AirportItlwmEthernetInterface;
    if (!netif) {
        XYLog("DEBUG %s FAIL: alloc AirportItlwmEthernetInterface\n", __FUNCTION__);
        return NULL;
    }
    if (!netif->initWithSkywalkInterfaceAndProvider(this, fNetIf)) {
        XYLog("DEBUG %s FAIL: initWithSkywalkInterfaceAndProvider\n", __FUNCTION__);
        netif->release();
        return NULL;
    }
    XYLog("DEBUG %s OK: netif=%p\n", __FUNCTION__, netif);
    return netif;
}
#endif

bool AirportItlwm::createMediumTables(const IONetworkMedium **primary)
{
    IONetworkMedium    *medium;

    OSDictionary *mediumDict = OSDictionary::withCapacity(2);
    if (mediumDict == NULL) {
        XYLog("Cannot allocate OSDictionary\n");
        return false;
    }
    
    medium = IONetworkMedium::medium(kIOMediumIEEE80211, 54000000);
    IONetworkMedium::addMedium(mediumDict, medium);
    medium->release();
    if (primary) {
        *primary = medium;
    }
    medium = IONetworkMedium::medium(kIOMediumIEEE80211None, 0);
    IONetworkMedium::addMedium(mediumDict, medium);
    medium->release();
    
    bool result = publishMediumDictionary(mediumDict);
    if (!result) {
        XYLog("Cannot publish medium dictionary!\n");
    }

    mediumDict->release();
    return result;
}

IOReturn AirportItlwm::selectMedium(const IONetworkMedium *medium) {
    setSelectedMedium(medium);
    return kIOReturnSuccess;
}

bool AirportItlwm::
setLinkStatus(UInt32 status, const IONetworkMedium * activeMedium, UInt64 speed, OSData * data)
{
    RT_SET(6);
    struct _ifnet *ifq = &fHalService->get80211Controller()->ic_ac.ac_if;
    XYLog("DEBUG %s status=0x%x (prev=0x%x) active=%d speed=%llu if_flags=0x%x ic_state=%d power_state=%u\n",
          __FUNCTION__, status, currentStatus, (status & kIONetworkLinkActive) != 0, speed,
          ifq->if_flags, fHalService->get80211Controller()->ic_state, power_state);
    if (status == currentStatus) {
        return true;
    }
    bool ret = super::setLinkStatus(status, activeMedium, speed, data);
    currentStatus = status;
    if (fNetIf) {
        if (status & kIONetworkLinkActive) {
#if defined(__PRIVATE_SPI__) && __IO80211_TARGET < __MAC_26_0
            bsdInterface->startOutputThread();
#endif
            getCommandGate()->runAction(setLinkStateGated, (void *)kIO80211NetworkLinkUp, (void *)0);
        } else if (!(status & kIONetworkLinkNoNetworkChange)) {
#if defined(__PRIVATE_SPI__) && __IO80211_TARGET < __MAC_26_0
            bsdInterface->stopOutputThread();
            bsdInterface->flushOutputQueue();
#endif
            ifq_flush(&ifq->if_snd);
            mq_purge(&fHalService->get80211Controller()->ic_mgtq);
            getCommandGate()->runAction(setLinkStateGated, (void *)kIO80211NetworkLinkDown, (void *)fHalService->get80211Controller()->ic_deauth_reason);
        }
    }
    return ret;
}

IOReturn AirportItlwm::
setLinkStateGated(OSObject *target, void *arg0, void *arg1, void *arg2, void *arg3)
{
    RT_SET(7);
    sRT.lastLinkState = (int)(uint64_t)arg0;
    sRT.linkSetCount++;
    AirportItlwm *that = OSDynamicCast(AirportItlwm, target);
    XYLog("DEBUG %s linkState=%llu deauthReason=%u power_state=%u\n",
          __FUNCTION__, (uint64_t)arg0, (unsigned int)(uint64_t)arg1, that->power_state);
#if __IO80211_TARGET >= __MAC_26_0
    XYLog("DEBUG %s Tahoe: calling IO80211InfraInterface::setLinkState fNetIf=%p\n", __FUNCTION__, that->fNetIf);
    IOReturn ret = ((IO80211InfraInterface *)that->fNetIf)->setLinkState((IO80211LinkState)(uint64_t)arg0, (unsigned int)(uint64_t)arg1, false, 0, 0);
#else
    IOReturn ret = that->fNetIf->setLinkState((IO80211LinkState)(uint64_t)arg0, (unsigned int)(uint64_t)arg1);
#endif
    if (ret == kIOReturnSuccess) RT_SET(14);
    XYLog("DEBUG %s setLinkState ret=0x%x\n", __FUNCTION__, ret);
    RT2_SET(13);
    that->fNetIf->setRunningState((IO80211LinkState)(uint64_t)arg0 == kIO80211NetworkLinkUp);
    that->postMessage(that->fNetIf, APPLE80211_M_LINK_CHANGED, NULL, 0, true);
    that->postMessage(that->fNetIf, APPLE80211_M_BSSID_CHANGED, NULL, 0, true);
    that->postMessage(that->fNetIf, APPLE80211_M_SSID_CHANGED, NULL, 0, true);
    if ((IO80211LinkState)(uint64_t)arg0 == kIO80211NetworkLinkUp) {
        that->fNetIf->reportLinkStatus(3, 0x80);
    } else {
        that->fNetIf->reportLinkStatus(1, 0);
    }
#if __IO80211_TARGET < __MAC_26_0
    if (that->bsdInterface) {
        XYLog("DEBUG %s calling bsdInterface->setLinkState bsdInterface=%p\n", __FUNCTION__, that->bsdInterface);
        that->bsdInterface->setLinkState((IO80211LinkState)(uint64_t)arg0);
    }
#endif
    XYLog("DEBUG %s DONE ret=0x%x\n", __FUNCTION__, ret);
    return ret;
}

#if defined(__PRIVATE_SPI__) && __IO80211_TARGET < __MAC_26_0
IOReturn AirportItlwm::outputStart(IONetworkInterface *interface, IOOptionBits options)
{
    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
    mbuf_t m = NULL;
    if (ifq_is_oactive(&ifp->if_snd))
        return kIOReturnNoResources;
    while (kIOReturnSuccess == interface->dequeueOutputPackets(1, &m)) {
        if (outputPacket(m, NULL)!= kIOReturnOutputSuccess ||
            ifq_is_oactive(&ifp->if_snd))
            return kIOReturnNoResources;
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::networkInterfaceNotification(
                    IONetworkInterface * interface,
                    uint32_t              type,
                    void *                  argument )
{
    XYLog("%s\n", __FUNCTION__);
    return kIOReturnSuccess;
}
#endif

extern const char* hexdump(uint8_t *buf, size_t len);

UInt32 AirportItlwm::outputPacket(mbuf_t m, void *param)
{
    static int sOutputCount = 0;
    if (++sOutputCount <= 5)
        XYLog("DEBUG %s #%d m=%p param=%p ic_state=%d\n", __FUNCTION__, sOutputCount, m, param,
              fHalService->get80211Controller()->ic_state);
    IOReturn ret = kIOReturnOutputSuccess;

    if (pmPowerState != kPowerStateOn) {
        sRT.outputDropPwr++;
        if (m && mbuf_type(m) != MBUF_TYPE_FREE)
            freePacket(m);
        return kIOReturnOutputDropped;
    }

    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;

    if (fHalService->get80211Controller()->ic_state != IEEE80211_S_RUN || ifp->if_snd.queue == NULL) {
        if (m && mbuf_type(m) != MBUF_TYPE_FREE)
            freePacket(m);
        return kIOReturnOutputDropped;
    }
    if (m == NULL) {
        XYLog("%s m==NULL!!\n", __FUNCTION__);
        ifp->netStat->outputErrors++;
        ret = kIOReturnOutputDropped;
    }
    if (!(mbuf_flags(m) & MBUF_PKTHDR) ){
        XYLog("%s pkthdr is NULL!!\n", __FUNCTION__);
        ifp->netStat->outputErrors++;
        freePacket(m);
        ret = kIOReturnOutputDropped;
    }
    if (mbuf_type(m) == MBUF_TYPE_FREE) {
        XYLog("%s mbuf is FREE!!\n", __FUNCTION__);
        ifp->netStat->outputErrors++;
        ret = kIOReturnOutputDropped;
    }
    size_t len = mbuf_len(m);
    ether_header_t *eh = (ether_header_t *)mbuf_data(m);
    if (len >= sizeof(ether_header_t) && eh->ether_type == htons(ETHERTYPE_PAE)) { // EAPOL packet
        const char* dump = hexdump((uint8_t*)mbuf_data(m), len);
        XYLog("output EAPOL packet, len: %zu, data: %s\n", len, dump ? dump : "Failed to allocate memory");
        if (dump)
            IOFree((void*)dump, 3 * len + 1);
    }
    if (!ifp->if_snd.queue->lockEnqueue(m)) {
        freePacket(m);
        ret = kIOReturnOutputDropped;
    }
    (*ifp->if_start)(ifp);
    return ret;
}

const OSString * AirportItlwm::newVendorString() const
{
    return OSString::withCString("Apple");
}

const OSString * AirportItlwm::newModelString() const
{
    return OSString::withCString(fHalService->getDriverInfo()->getFirmwareName());
}

IOReturn AirportItlwm::getHardwareAddress(IOEthernetAddress *addrP)
{
    if (IEEE80211_ADDR_EQ(etheranyaddr, fHalService->get80211Controller()->ic_myaddr))
        return kIOReturnError;
    else {
        IEEE80211_ADDR_COPY(addrP, fHalService->get80211Controller()->ic_myaddr);
        return kIOReturnSuccess;
    }
}

IOReturn AirportItlwm::setHardwareAddress(const void *addrP, UInt32 addrBytes)
{
    if (!fNetIf || !addrP)
        return kIOReturnError;
    if_setlladdr(&fHalService->get80211Controller()->ic_ac.ac_if, (const UInt8 *)addrP);
    if (fHalService->get80211Controller()->ic_state > IEEE80211_S_INIT) {
#if __IO80211_TARGET >= __MAC_26_0
        fHalService->disable(NULL);
        fHalService->enable(NULL);
#else
        fHalService->disable(bsdInterface);
        fHalService->enable(bsdInterface);
#endif
    }
    return kIOReturnSuccess;
}

UInt32 AirportItlwm::getFeatures() const
{
    return fHalService->getDriverInfo()->supportedFeatures();
}

IOReturn AirportItlwm::setPromiscuousMode(IOEnetPromiscuousMode mode)
{
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::setMulticastMode(IOEnetMulticastMode mode)
{
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::setMulticastList(IOEthernetAddress* addr, UInt32 len)
{
    return fHalService->getDriverController()->setMulticastList(addr, len);
}

IOReturn AirportItlwm::getPacketFilters(const OSSymbol *group, UInt32 *filters) const
{
    IOReturn    rtn = kIOReturnSuccess;
    if (group == gIOEthernetWakeOnLANFilterGroup && magicPacketSupported)
        *filters = kIOEthernetWakeOnMagicPacket;
    else if (group == gIONetworkFilterGroup)
        *filters = kIOPacketFilterMulticast | kIOPacketFilterPromiscuous;
    else
        rtn = IOEthernetController::getPacketFilters(group, filters);
    return rtn;
}

SInt32 AirportItlwm::
enableFeature(IO80211FeatureCode code, void *data)
{
    XYLog("DEBUG %s code=%d\n", __FUNCTION__, code);
    if (code == kIO80211Feature80211n) {
        return 0;
    }
    return 102;
}

#if __IO80211_TARGET >= __MAC_26_0
// vtable dump[429] = releaseFlowQueue at vptr+0xD58.  Not called during start().
static int sReleaseFlowQueueCallCount = 0;
void *AirportItlwm::releaseFlowQueue(IO80211FlowQueue *)
{
    int n = ++sReleaseFlowQueueCallCount;
    if (n <= 50)
        XYLog("DEBUG [vtable429] releaseFlowQueue #%d\n", n);
    return nullptr;
}

// vtable dump[431] = getDriverLogStream (pure virtual) at vptr+0xD68.
// IO80211Controller::start() calls this to get CCLogStream* for setGlobalLogger(),
// then createIOReporters() uses it.  Must return valid CCLogStream*.
// (Dump indexes include 2 RTTI entries, so vptr offset = (431-2)*8 = 0xD68.)
static int sGetDriverLogStreamCallCount = 0;
void *AirportItlwm::getDriverLogStream()
{
    int n = ++sGetDriverLogStreamCallCount;
    if (n <= 50)
        XYLog("DEBUG [vtable431] getDriverLogStream #%d driverLogStream=%p rc=%d\n",
              n, driverLogStream, driverLogStream ? driverLogStream->getRetainCount() : -1);
    return driverLogStream;
}
#endif

bool AirportItlwm::getLogPipes(CCPipe**logPipe, CCPipe**eventPipe, CCPipe**snapshotsPipe)
{
    XYLog("DEBUG %s logPipe=%p dataPipe=%p snapPipe=%p\n", __FUNCTION__,
          driverLogPipe, driverDataPathPipe, driverSnapshotsPipe);
    bool ret = false;
    if (logPipe) {
        *logPipe = driverLogPipe;
        ret = true;
    }
    if (eventPipe) {
        *eventPipe = driverDataPathPipe;
        ret = true;
    }
    if (snapshotsPipe) {
        *snapshotsPipe = driverSnapshotsPipe;
        ret = true;
    }
    return ret;
}

#define APPLE80211_CAPA_AWDL_FEATURE_AUTO_UNLOCK    0x00000004
#define APPLE80211_CAPA_AWDL_FEATURE_WOW            0x00000080

IOReturn AirportItlwm::
getCARD_CAPABILITIES(OSObject *object,
                                     struct apple80211_capability_data *cd)
{
    uint32_t caps = fHalService->get80211Controller()->ic_caps;
    memset(cd, 0, sizeof(struct apple80211_capability_data));
    
    if (caps & IEEE80211_C_WEP)
        cd->capabilities[0] |= 1 << APPLE80211_CAP_WEP;
    if (caps & IEEE80211_C_RSN)
        cd->capabilities[0] |= 1 << APPLE80211_CAP_TKIP | 1 << APPLE80211_CAP_AES_CCM;
    // Disable not implemented capabilities
    // if (caps & IEEE80211_C_PMGT)
    //     cd->capabilities[0] |= 1 << APPLE80211_CAP_PMGT;
    // if (caps & IEEE80211_C_IBSS)
    //     cd->capabilities[0] |= 1 << APPLE80211_CAP_IBSS;
    // if (caps & IEEE80211_C_HOSTAP)
    //     cd->capabilities[0] |= 1 << APPLE80211_CAP_HOSTAP;
    // AES not enabled, like on Apple cards
    
    if (caps & IEEE80211_C_SHSLOT)
        cd->capabilities[1] |= 1 << (APPLE80211_CAP_SHSLOT - 8);
    if (caps & IEEE80211_C_SHPREAMBLE)
        cd->capabilities[1] |= 1 << (APPLE80211_CAP_SHPREAMBLE - 8);
    if (caps & IEEE80211_C_RSN)
        cd->capabilities[1] |= 1 << (APPLE80211_CAP_WPA1 - 8) | 1 << (APPLE80211_CAP_WPA2 - 8) | 1 << (APPLE80211_CAP_TKIPMIC - 8);
    // Disable not implemented capabilities
    // if (caps & IEEE80211_C_TXPMGT)
    //     cd->capabilities[1] |= 1 << (APPLE80211_CAP_TXPMGT - 8);
    // if (caps & IEEE80211_C_MONITOR)
    //     cd->capabilities[1] |= 1 << (APPLE80211_CAP_MONITOR - 8);
    // WPA not enabled, like on Apple cards

    cd->version = APPLE80211_VERSION;
    cd->capabilities[2] = 0xEF; // BURST, WME, SHORT_GI_40MHZ, SHORT_GI_20MHZ, TSN (WOW bit cleared — not implemented)
    cd->capabilities[3] = 0x2B;
    cd->capabilities[5] = 0x40;
    cd->capabilities[6] = (
//                           1 |    //MFP capable
                           0x8 |
                           0x4 |
                           0x80
                           );
    *(uint16_t *)&cd->capabilities[8] = 0x201;
//
//    cd->capabilities[2] |= 0x10;
//    cd->capabilities[5] |= 0x1;
//
//    cd->capabilities[2] |= 0x2;
//
//    cd->capabilities[3] |= 0x20;
//
//    cd->capabilities[0] |= 0x80;
//
//    cd->capabilities[3] |= 0x80;
//    cd->capabilities[4] |= 0x4;
//
//    cd->capabilities[4] |= 0x1;
//    cd->capabilities[3] |= 0x1;
//    cd->capabilities[6] |= 0x8;
//
//    cd->capabilities[3] |= 3;
//    cd->capabilities[4] |= 2;
//    cd->capabilities[6] |= 0x10;
//    cd->capabilities[5] |= 0x20;
//    cd->capabilities[5] |= 0x80;
//
//    if (cd->capabilities[6] & 0x20) {
//        cd->capabilities[2] |= 8;
//    }
//    cd->capabilities[5] |= 8;
//    cd->capabilities[8] |= 2;
//
//    cd->capabilities[11] |= (2 | 4 | 8 | 0x10 | 0x20 | 0x40 | 0x80);
    
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getDRIVER_VERSION(OSObject *object,
                                  struct apple80211_version_data *hv)
{
    if (!hv)
        return kIOReturnError;
    hv->version = APPLE80211_VERSION;
    snprintf(hv->string, sizeof(hv->string), "itlwm: %s%s fw: %s", ITLWM_VERSION, GIT_COMMIT, fHalService->getDriverInfo()->getFirmwareVersion());
    hv->string_len = strlen(hv->string);
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getHARDWARE_VERSION(OSObject *object,
                                    struct apple80211_version_data *hv)
{
    if (!hv)
        return kIOReturnError;
    hv->version = APPLE80211_VERSION;
    strncpy(hv->string, fHalService->getDriverInfo()->getFirmwareVersion(), sizeof(hv->string));
    hv->string_len = strlen(fHalService->getDriverInfo()->getFirmwareVersion());
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getCOUNTRY_CODE(OSObject *object,
                                struct apple80211_country_code_data *cd)
{
    char user_override_cc[3];
    const char *cc_fw = fHalService->getDriverInfo()->getFirmwareCountryCode();
    
    if (!cd)
        return kIOReturnError;
    cd->version = APPLE80211_VERSION;
    memset(user_override_cc, 0, sizeof(user_override_cc));
    PE_parse_boot_argn("itlwm_cc", user_override_cc, 3);
    /* user_override_cc > firmware_cc > geo_location_cc */
    strncpy((char*)cd->cc, user_override_cc[0] ? user_override_cc : ((cc_fw[0] == 'Z' && cc_fw[1] == 'Z' && geo_location_cc[0]) ? geo_location_cc : cc_fw), sizeof(cd->cc));
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setCOUNTRY_CODE(OSObject *object, struct apple80211_country_code_data *data)
{
    XYLog("%s cc=%s\n", __FUNCTION__, data->cc);
    if (data && data->cc[0] != 120 && data->cc[0] != 88) {
        memcpy(geo_location_cc, data->cc, sizeof(geo_location_cc));
        postMessage(fNetIf, APPLE80211_M_COUNTRY_CODE_CHANGED, NULL, 0, true);
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getPOWER(OSObject *object,
                         struct apple80211_power_data *pd)
{
    if (!pd)
        return kIOReturnError;
    pd->version = APPLE80211_VERSION;
    pd->num_radios = 4;
    pd->power_state[0] = power_state;
    pd->power_state[1] = power_state;
    pd->power_state[2] = power_state;
    pd->power_state[3] = power_state;
    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
    XYLog("DEBUG %s returning power_state=%u if_flags=0x%x ic_state=%d pmPowerState=%u\n",
          __FUNCTION__, power_state, ifp->if_flags, fHalService->get80211Controller()->ic_state, pmPowerState);
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setPOWER(OSObject *object,
                         struct apple80211_power_data *pd)
{
    RT2_SET(6);
    if (!pd)
        return kIOReturnError;
    XYLog("%s num_radios=%d req=%u cur=%u pmPowerState=%u\n",
          __FUNCTION__, pd->num_radios,
          pd->num_radios > 0 ? pd->power_state[0] : 0, power_state, pmPowerState);
    if (pd->num_radios > 0)
#if __IO80211_TARGET >= __MAC_26_0
        handlePowerStateChange(pd->power_state[0], NULL);
#else
        handlePowerStateChange(pd->power_state[0], bsdInterface);
#endif
    return kIOReturnSuccess;
}

#if __IO80211_TARGET < __MAC_26_0
SInt32 AirportItlwm::apple80211_ioctl(IO80211SkywalkInterface *interface,unsigned long cmd,void *data, bool b1, bool b2)
{
    if (!ml_at_interrupt_context())
        XYLog("%s cmd: %s b1: %d b2: %d\n", __FUNCTION__, convertApple80211IOCTLToString((unsigned int)cmd), b1, b2);
    return super::apple80211_ioctl(interface, cmd, data, b1, b2);
}

SInt32 AirportItlwm::apple80211SkywalkRequest(UInt request,int cmd,IO80211SkywalkInterface *interface,void *data)
{
    if (!ml_at_interrupt_context())
        XYLog("%s 1 cmd: %s request: %d\n", __FUNCTION__, convertApple80211IOCTLToString(cmd), request);
    return kIOReturnUnsupported;
}

SInt32 AirportItlwm::apple80211SkywalkRequest(UInt request,int cmd,IO80211SkywalkInterface *interface,void *data,void *)
{
    if (!ml_at_interrupt_context())
        XYLog("%s 2 cmd: %s request: %d\n", __FUNCTION__, convertApple80211IOCTLToString(cmd), request);
    return kIOReturnUnsupported;
}
#endif

IOReturn AirportItlwm::enableAdapter(IONetworkInterface *netif)
{
    RT_SET(9);
    sRT.enableCnt++;
    struct ieee80211com *ic = fHalService ? fHalService->get80211Controller() : NULL;
    XYLog("DEBUG %s netif=%p power_state=%u pmPowerState=%u ic_state=%d\n",
          __FUNCTION__, netif, power_state, pmPowerState, ic ? ic->ic_state : -1);
    if (!fHalService) {
        XYLog("DEBUG %s ABORT: fHalService is NULL\n", __FUNCTION__);
        return kIOReturnNotReady;
    }
    fHalService->enable(netif);
    struct _ifnet *ifp = &ic->ic_ac.ac_if;
    XYLog("DEBUG %s post-enable: ic_state=%d if_flags=0x%x\n",
          __FUNCTION__, ic->ic_state, ifp->if_flags);
    if (watchdogTimer) {
        watchdogTimer->setTimeoutMS(kWatchDogTimerPeriod);
        watchdogTimer->enable();
    }
    sRT.lastEnableRet = kIOReturnSuccess;
    return kIOReturnSuccess;
}

void AirportItlwm::disableAdapter(IONetworkInterface *netif)
{
    RT_SET(10);
    sRT.disableCnt++;
    XYLog("DEBUG %s netif=%p power_state=%u pmPowerState=%u\n", __FUNCTION__, netif, power_state, pmPowerState);
    if (watchdogTimer) {
        watchdogTimer->cancelTimeout();
        watchdogTimer->disable();
    }
    if (fHalService)
        fHalService->disable(netif);
}

//
// State machine matching Apple's AppleBCMWLANCore::handlePowerStateChange.
// States: 0=OFF, 1=ON, 4=STANDBY
// Transitions:
//   cur=1→req=0: powerOff        cur=0→req=1: powerOn
//   cur=4→req=0: powerOff        cur=4→req=1: powerOn
//   cur=0→req=4: powerOn         cur=1→req=4: powerOff
//   other: error (-1)
// On powerOn/powerOff failure, state is rolled back.
//
int AirportItlwm::handlePowerStateChange(uint32_t newState, IONetworkInterface *netif)
{
    uint8_t prevState = power_state;
    int err = 0;

    XYLog("DEBUG %s cur=%u req=%u\n", __FUNCTION__, prevState, newState);

    if ((newState == kWiFiPowerOff && prevState == kWiFiPowerOn) ||
        (newState == kWiFiPowerOff && prevState == kWiFiPowerStandby)) {
        // ON→OFF or STANDBY→OFF: power off
        power_state = kWiFiPowerOff;
        net80211_ifstats(fHalService->get80211Controller());
        disableAdapter(netif);
    }
    else if (newState == kWiFiPowerOn && (prevState == kWiFiPowerOff || prevState == kWiFiPowerStandby)) {
        // OFF→ON or STANDBY→ON: power on
        power_state = kWiFiPowerOn;
        enableAdapter(netif);
    }
    else if (newState == kWiFiPowerStandby && prevState == kWiFiPowerOff) {
        // OFF→STANDBY: power on (into standby mode)
        power_state = kWiFiPowerStandby;
        enableAdapter(netif);
    }
    else if (newState == kWiFiPowerStandby && prevState == kWiFiPowerOn) {
        // ON→STANDBY: power off (into standby)
        power_state = kWiFiPowerStandby;
        net80211_ifstats(fHalService->get80211Controller());
        disableAdapter(netif);
    }
    else if (newState == prevState) {
        // Same state — no-op
        XYLog("DEBUG %s already in state %u, no-op\n", __FUNCTION__, prevState);
    }
    else {
        // Invalid transition
        XYLog("DEBUG %s INVALID transition %u → %u\n", __FUNCTION__, prevState, newState);
        err = -1;
    }

    if (err) {
        XYLog("DEBUG %s FAILED, rollback %u → %u\n", __FUNCTION__, power_state, prevState);
        power_state = prevState;
    }

    return err;
}

IOReturn AirportItlwm::
tsleepHandler(OSObject* owner, void* arg0, void* arg1, void* arg2, void* arg3)
{
    AirportItlwm* dev = OSDynamicCast(AirportItlwm, owner);
    if (dev == 0)
        return kIOReturnError;
    
    if (arg1 == 0) {
        if (_fCommandGate->commandSleep(arg0, THREAD_INTERRUPTIBLE) == THREAD_AWAKENED)
            return kIOReturnSuccess;
        else
            return kIOReturnTimeout;
    } else {
        AbsoluteTime deadline;
        clock_interval_to_deadline((*(int*)arg1), kNanosecondScale, reinterpret_cast<uint64_t*> (&deadline));
        if (_fCommandGate->commandSleep(arg0, deadline, THREAD_INTERRUPTIBLE) == THREAD_AWAKENED)
            return kIOReturnSuccess;
        else
            return kIOReturnTimeout;
    }
}

bool AirportItlwm::initPCIPowerManagment(IOPCIDevice *provider)
{
    UInt16 reg16;

    reg16 = provider->configRead16(kIOPCIConfigCommand);

    reg16 |= ( kIOPCICommandBusMaster       |
               kIOPCICommandMemorySpace     |
               kIOPCICommandMemWrInvalidate );

    reg16 &= ~kIOPCICommandIOSpace;  // disable I/O space

    provider->configWrite16( kIOPCIConfigCommand, reg16 );
    provider->findPCICapability(kIOPCIPowerManagementCapability,
                                &pmPCICapPtr);
    if (pmPCICapPtr) {
        UInt16 pciPMCReg = provider->configRead32( pmPCICapPtr ) >> 16;
        if (pciPMCReg & kPCIPMCPMESupportFromD3Cold)
            magicPacketSupported = true;
        provider->configWrite16((pmPCICapPtr + 4), 0x8000 );
        IOSleep(10);
    }
    return true;
}

static IOPMPowerState powerStateArray[kPowerStateCount] =
{
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, kIOPMDeviceUsable, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0}
};

void AirportItlwm::unregistPM()
{
    if (powerOffThreadCall) {
        sRT.pmOffCancelRet = thread_call_cancel(powerOffThreadCall);
        thread_call_free(powerOffThreadCall);
        powerOffThreadCall = NULL;
    }
    if (powerOnThreadCall) {
        sRT.pmOnCancelRet = thread_call_cancel(powerOnThreadCall);
        thread_call_free(powerOnThreadCall);
        powerOnThreadCall = NULL;
    }
}

IOReturn AirportItlwm::setPowerState(unsigned long powerStateOrdinal, IOService *policyMaker)
{
    RT_SET(12);
    sRT.pmCount++;
    sRT.lastPmReq = (uint32_t)powerStateOrdinal;
    IOReturn result = IOPMAckImplied;
    XYLog("DEBUG %s ordinal=%lu pmPowerState=%u power_state=%u\n", __FUNCTION__, powerStateOrdinal, pmPowerState, power_state);
    if (pmPowerState == powerStateOrdinal) {
        XYLog("DEBUG %s SKIPPED (already in state %lu)\n", __FUNCTION__, powerStateOrdinal);
        return result;
    }
    switch (powerStateOrdinal) {
        case kPowerStateOff:
            if (powerOffThreadCall) {
                retain();
                if (thread_call_enter(powerOffThreadCall))
                    release();
                result = 5000000;
            }
            break;
        case kPowerStateOn:
            if (powerOnThreadCall) {
                retain();
                if (thread_call_enter(powerOnThreadCall))
                    release();
                result = 5000000;
            }
            break;
            
        default:
            break;
    }
    return result;
}

unsigned long AirportItlwm::initialPowerStateForDomainState(IOPMPowerFlags domainState)
{
    // kIOPMDeviceUsable (bit 9) = parent can supply usable power → ON
    // kIOPMPowerOn (bit 1) = parent has power → ON
    // Neither → OFF
    unsigned long ret;
    if (domainState & kIOPMDeviceUsable)
        ret = kPowerStateOn;
    else if (domainState & kIOPMPowerOn)
        ret = kPowerStateOn;
    else
        ret = kPowerStateOff;
    XYLog("DEBUG %s domainState=0x%lx (DeviceUsable=%d PowerOn=%d) -> %lu power_state=%u pmPowerState=%u\n",
          __FUNCTION__, (unsigned long)domainState,
          (int)((domainState & kIOPMDeviceUsable) != 0), (int)((domainState & kIOPMPowerOn) != 0),
          ret, power_state, pmPowerState);
    return ret;
}

IOReturn AirportItlwm::setWakeOnMagicPacket(bool active)
{
    magicPacketEnabled = active;
    return kIOReturnSuccess;
}

static void handleSetPowerStateOff(thread_call_param_t param0,
                             thread_call_param_t param1)
{
    AirportItlwm *self = (AirportItlwm *)param0;
    if (!self) return;

    if (param1 == 0)
    {
        IOCommandGate *gate = self->getCommandGate();
        if (gate) {
            gate->runAction((IOCommandGate::Action)
                            handleSetPowerStateOff,
                            (void *) 1);
        } else {
            sRT.pmOffGateNull++;
            XYLog("DEBUG handleSetPowerStateOff: gate=NULL, calling directly\n");
            self->setPowerStateOff();
            self->release();
        }
    }
    else
    {
        self->setPowerStateOff();
        self->release();
    }
}

static void handleSetPowerStateOn(thread_call_param_t param0,
                            thread_call_param_t param1)
{
    AirportItlwm *self = (AirportItlwm *)param0;
    if (!self) return;

    if (param1 == 0)
    {
        IOCommandGate *gate = self->getCommandGate();
        if (gate) {
            gate->runAction((IOCommandGate::Action)
                            handleSetPowerStateOn,
                            (void *) 1);
        } else {
            sRT.pmOnGateNull++;
            XYLog("DEBUG handleSetPowerStateOn: gate=NULL, calling directly\n");
            self->setPowerStateOn();
            self->release();
        }
    }
    else
    {
        self->setPowerStateOn();
        self->release();
    }
}

IOReturn AirportItlwm::registerWithPolicyMaker(IOService *policyMaker)
{
    IOReturn ret;

    XYLog("DEBUG %s entry policyMaker=%p\n", __FUNCTION__, policyMaker);
    pmPowerState = kPowerStateOn;
    pmPolicyMaker = policyMaker;
    sRT.pmPolicyPtr = (uint64_t)(uintptr_t)policyMaker;

    powerOffThreadCall = thread_call_allocate(
                                            (thread_call_func_t)handleSetPowerStateOff,
                                            (thread_call_param_t)this);
    powerOnThreadCall  = thread_call_allocate(
                                            (thread_call_func_t)handleSetPowerStateOn,
                                              (thread_call_param_t)this);
    ret = pmPolicyMaker->registerPowerDriver(this,
                                             powerStateArray,
                                             kPowerStateCount);
    XYLog("DEBUG %s registerPowerDriver ret=%d pmPowerState=%u\n", __FUNCTION__, ret, pmPowerState);
    if (ret == kIOReturnSuccess) {
        // Match Apple's pattern: assert device desires ON, external starts at OFF.
        // Without this, PM framework may call setPowerState(0) after registration,
        // disabling the adapter that start() just enabled.
        changePowerStateToPriv(kPowerStateOn);
        changePowerStateTo(kPowerStateOff);
        XYLog("DEBUG %s changePowerStateToPriv(ON) + changePowerStateTo(OFF) done\n", __FUNCTION__);
    }
    return ret;
}

void AirportItlwm::setPowerStateOff()
{
    XYLog("DEBUG %s power_state=%u pmPowerState=%u\n", __FUNCTION__, power_state, pmPowerState);
    pmPowerState = kPowerStateOff;
#if __IO80211_TARGET >= __MAC_26_0
    disableAdapter(NULL);
#else
    disableAdapter(bsdInterface);
#endif
    if (pmPolicyMaker) {
        sRT.pmAckOffCnt++;
        pmPolicyMaker->acknowledgeSetPowerState();
    }
}

void AirportItlwm::setPowerStateOn()
{
    XYLog("DEBUG %s power_state=%u pmPowerState=%u\n", __FUNCTION__, power_state, pmPowerState);
    pmPowerState = kPowerStateOn;
    if (power_state)
#if __IO80211_TARGET >= __MAC_26_0
        enableAdapter(NULL);
#else
        enableAdapter(bsdInterface);
#endif
    else
        XYLog("DEBUG %s SKIPPED enableAdapter (power_state=0)\n", __FUNCTION__);
    if (pmPolicyMaker) {
        sRT.pmAckOnCnt++;
        pmPolicyMaker->acknowledgeSetPowerState();
    }
}
