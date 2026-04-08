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

#define super IO80211Controller
OSDefineMetaClassAndStructors(AirportItlwm, IO80211Controller);
OSDefineMetaClassAndStructors(CTimeout, OSObject)

IO80211WorkQueue *_fWorkloop;
IOCommandGate *_fCommandGate;

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

void AirportItlwm::
eventHandler(struct ieee80211com *ic, int msgCode, void *data)
{
    AirportItlwm *that = OSDynamicCast(AirportItlwm, ic->ic_ac.ac_if.controller);
    IO80211SkywalkInterface *interface = that->fNetIf;
    XYLog("DEBUG %s msgCode=%d ic_state=%d if_flags=0x%x power_state=%u\n",
          __FUNCTION__, msgCode, ic->ic_state, ic->ic_ac.ac_if.if_flags, that->power_state);
    if (!interface)
        return;
    switch (msgCode) {
        case IEEE80211_EVT_COUNTRY_CODE_UPDATE:
            interface->postMessage(APPLE80211_M_COUNTRY_CODE_CHANGED, NULL, 0, 0);
            break;
        case IEEE80211_EVT_STA_ASSOC_DONE:
            interface->postMessage(APPLE80211_M_ASSOC_DONE, NULL, 0, 0);
            break;
        case IEEE80211_EVT_STA_DEAUTH:
            interface->postMessage(APPLE80211_M_DEAUTH_RECEIVED, NULL, 0, 0);
            break;
        default:
            break;
    }
}

void AirportItlwm::watchdogAction(IOTimerEventSource *timer)
{
    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
    struct ieee80211com *ic = fHalService->get80211Controller();
    static int wd_count = 0;
    if (wd_count++ % 10 == 0) // log every 10s
        XYLog("DEBUG %s [%d] ic_state=%d if_flags=0x%x power_state=%u pmPowerState=%u\n",
              __FUNCTION__, wd_count, ic->ic_state, ifp->if_flags, power_state, pmPowerState);
    (*ifp->if_watchdog)(ifp);
    watchdogTimer->setTimeoutMS(kWatchDogTimerPeriod);
}

void AirportItlwm::fakeScanDone(OSObject *owner, IOTimerEventSource *sender)
{
    UInt32 msg = 0;
    AirportItlwm *that = (AirportItlwm *)owner;
    that->fNetIf->postMessage(APPLE80211_M_SCAN_DONE, &msg, 4, 0);
    that->fNetIf->postMessage(APPLE80211_M_BGSCAN_CACHED_NETWORK_AVAILABLE, NULL, 0, 0);
}

bool AirportItlwm::init(OSDictionary *properties)
{
    bool ret = super::init(properties);
    awdlSyncEnable = true;
    power_state = 0;
    driverLogStream = nullptr;
    memset(geo_location_cc, 0, sizeof(geo_location_cc));
    XYLog("DEBUG %s power_state=%u ret=%d\n", __FUNCTION__, power_state, ret);
    return ret;
}

IOService* AirportItlwm::probe(IOService *provider, SInt32 *score)
{
    XYLog("DEBUG %s entry provider=%p\n", __PRETTY_FUNCTION__, provider);
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
    XYLog("DEBUG %s OK: pciNub=%p fHalService=%p\n", __FUNCTION__, pciNub, fHalService);
    return super::probe(provider, score);
}

#define LOWER32(x)  ((uint64_t)(x) & 0xffffffff)
#define HIGHER32(x) ((uint64_t)(x) >> 32)

bool AirportItlwm::
initCCLogs()
{
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
    driverLogPipe = CCPipe::withOwnerNameCapacity(this, "com.zxystd.AirportItlwm", "DriverLogs", &driverLogOptions);
    XYLog("%s driverLogPipeRet %d\n", __FUNCTION__, driverLogPipe != NULL);
    
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
    driverDataPathPipe = CCPipe::withOwnerNameCapacity(this, "com.zxystd.AirportItlwm", "DatapathEvents", &driverLogOptions);
    XYLog("%s driverDataPathPipeRet %d\n", __FUNCTION__, driverDataPathPipe != NULL);
    
    memset(&driverLogOptions, 0, sizeof(driverLogOptions));
    driverLogOptions.pipe_type = 0x200000001;
    driverLogOptions.log_data_type = 2;
    strlcpy(driverLogOptions.file_name, "StateSnapshots", sizeof(driverLogOptions.file_name));
    strlcpy(driverLogOptions.name, "0", sizeof(driverLogOptions.name));
    strlcpy(driverLogOptions.directory_name, "WiFi", sizeof(driverLogOptions.directory_name));
    driverLogOptions.pipe_size = 128;
    driverSnapshotsPipe = CCPipe::withOwnerNameCapacity(this, "com.zxystd.AirportItlwm", "StateSnapshots", &driverLogOptions);
    XYLog("%s driverSnapshotsPipeRet %d\n", __FUNCTION__, driverSnapshotsPipe != NULL);
    
    CCStreamOptions faultReportOptions = { 0 };
    faultReportOptions.stream_type = 1;
    faultReportOptions.console_level = 0xFFFFFFFFFFFFFFFF;
    driverFaultReporter = CCStream::withPipeAndName(driverSnapshotsPipe, "FaultReporter", &faultReportOptions);
    XYLog("%s driverFaultReporterRet %d\n", __FUNCTION__, driverFaultReporter != NULL);
    // NOTE: CCLogStream creation is deferred to after super::start() —
    // creating it before IO80211Controller::start() causes a deadlock because
    // the framework calls vtable[429] during start() and passes the CCLogStream
    // to CoreCapture cc_log, which needs IO80211 infrastructure not yet ready.
    return driverLogPipe && driverDataPathPipe && driverSnapshotsPipe && driverFaultReporter;
}

bool AirportItlwm::start(IOService *provider)
{
    XYLog("DEBUG %s [STEP 0] entry provider=%p\n", __PRETTY_FUNCTION__, provider);
    struct IOSkywalkEthernetInterface::RegistrationInfo registInfo;
    int boot_value = 0;

    UInt8 builtIn = 0;
    setProperty("built-in", OSData::withBytes(&builtIn, sizeof(builtIn)));
    setProperty("DriverKitDriver", kOSBooleanFalse);
#if __IO80211_TARGET >= __MAC_26_0
    XYLog("DEBUG %s [STEP 1] initCCLogs (Tahoe path)\n", __FUNCTION__);
    if (!initCCLogs()) {
        XYLog("DEBUG %s [STEP 1] FAIL: CCLog init\n", __FUNCTION__);
        return false;
    }
#endif
    // NOTE: driverLogStream is NULL here intentionally.  vtable[429] returns NULL
    // during super::start() — all 28+ framework call sites check for NULL and skip
    // logging.  Creating CCLogStream before start() causes a deadlock in CoreCapture.
    XYLog("DEBUG %s [STEP 2] super::start (driverLogStream=NULL — will create after start)\n", __FUNCTION__);
    if (!super::start(provider)) {
        XYLog("DEBUG %s [STEP 2] FAIL: super::start returned false\n", __FUNCTION__);
        return false;
    }
    // IO80211 infrastructure is now ready — safe to create CCLogStream
    {
        CCStreamOptions logStreamOptions = { 0 };
        logStreamOptions.stream_type = 0;
        logStreamOptions.console_level = 0xFFFFFFFFFFFFFFFF;
        CCStream *logStreamBase = CCStream::withPipeAndName(driverLogPipe, "DriverLogStream", &logStreamOptions);
        driverLogStream = OSDynamicCast(CCLogStream, logStreamBase);
        XYLog("DEBUG %s [STEP 2a] driverLogStream: base=%p cast=%p\n", __FUNCTION__, logStreamBase, driverLogStream);
        if (logStreamBase && !driverLogStream)
            logStreamBase->release();
    }
    XYLog("DEBUG %s [STEP 3] PCI setup\n", __FUNCTION__);
    pciNub->setBusMasterEnable(true);
    pciNub->setIOEnable(true);
    pciNub->setMemoryEnable(true);
    pciNub->configWrite8(0x41, 0);
    if (pciNub->requestPowerDomainState(kIOPMPowerOn,
                                        (IOPowerConnection *) getParentEntry(gIOPowerPlane), IOPMLowestState) != IOPMNoErr) {
        XYLog("DEBUG %s [STEP 3] FAIL: requestPowerDomainState\n", __FUNCTION__);
        super::stop(provider);
        return false;
    }
    if (initPCIPowerManagment(pciNub) == false) {
        XYLog("DEBUG %s [STEP 3] FAIL: initPCIPowerManagment\n", __FUNCTION__);
        super::stop(pciNub);
        return false;
    }
    XYLog("DEBUG %s [STEP 4] workloop & command gate\n", __FUNCTION__);
    if (_fWorkloop == NULL) {
        XYLog("DEBUG %s [STEP 4] FAIL: No _fWorkloop\n", __FUNCTION__);
        super::stop(pciNub);
        releaseAll();
        return false;
    }
    _fCommandGate = IOCommandGate::commandGate(this, (IOCommandGate::Action)AirportItlwm::tsleepHandler);
    if (_fCommandGate == 0) {
        XYLog("DEBUG %s [STEP 4] FAIL: No command gate\n", __FUNCTION__);
        super::stop(pciNub);
        releaseAll();
        return false;
    }
    _fWorkloop->addEventSource(_fCommandGate);
    const IONetworkMedium *primaryMedium;
    if (!createMediumTables(&primaryMedium) ||
        !setCurrentMedium(primaryMedium) || !setSelectedMedium(primaryMedium)) {
        XYLog("DEBUG %s [STEP 4] FAIL: setup medium\n", __FUNCTION__);
        releaseAll();
        return false;
    }
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
        return false;
    }
    XYLog("DEBUG %s [STEP 6] watchdog + scan timers\n", __FUNCTION__);
    fWatchdogWorkLoop = IOWorkLoop::workLoop();
    if (fWatchdogWorkLoop == NULL) {
        XYLog("DEBUG %s [STEP 6] FAIL: watchdog workloop\n", __FUNCTION__);
        fHalService->detach(pciNub);
        super::stop(pciNub);
        releaseAll();
        return false;
    }
    watchdogTimer = IOTimerEventSource::timerEventSource(this, OSMemberFunctionCast(IOTimerEventSource::Action, this, &AirportItlwm::watchdogAction));
    if (!watchdogTimer) {
        XYLog("DEBUG %s [STEP 6] FAIL: watchdog timer\n", __FUNCTION__);
        fHalService->detach(pciNub);
        super::stop(pciNub);
        releaseAll();
        return false;
    }
    fWatchdogWorkLoop->addEventSource(watchdogTimer);
    scanSource = IOTimerEventSource::timerEventSource(this, &fakeScanDone);
    _fWorkloop->addEventSource(scanSource);
    scanSource->enable();

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
        return false;
    }
    fNetIf->setInterfaceRole(1);
    fNetIf->setInterfaceId(1);

#if __IO80211_TARGET < __MAC_26_0
    if (!initCCLogs()) {
        XYLog("DEBUG %s [STEP 7] FAIL: CCLog init (pre-Tahoe)\n", __FUNCTION__);
        super::stop(provider);
        releaseAll();
        return false;
    }
#endif
    if (!fNetIf->attach(this)) {
        XYLog("DEBUG %s [STEP 7] FAIL: fNetIf attach\n", __FUNCTION__);
        super::stop(provider);
        releaseAll();
        return false;
    }
    XYLog("DEBUG %s [STEP 8] attachInterface + BSD interface\n", __FUNCTION__);
    if (!attachInterface(fNetIf, this)) {
        XYLog("DEBUG %s [STEP 8] FAIL: attachInterface\n", __FUNCTION__);
        super::stop(provider);
        releaseAll();
        return false;
    }
    if (!IONetworkController::attachInterface((IONetworkInterface **)&bsdInterface, true)) {
        XYLog("DEBUG %s [STEP 8] FAIL: IONetworkController attachInterface\n", __FUNCTION__);
        super::stop(provider);
        releaseAll();
        return false;
    }
    XYLog("DEBUG %s [STEP 8] bsdInterface=%p\n", __FUNCTION__, bsdInterface);
    memset(&registInfo, 0, sizeof(registInfo));
    if (!fNetIf->initRegistrationInfo(&registInfo, 1, sizeof(registInfo))) {
        XYLog("DEBUG %s [STEP 8] FAIL: initRegistrationInfo\n", __FUNCTION__);
        super::stop(provider);
        releaseAll();
        return false;
    }
    {
        // Dump first 64 bytes of RegistrationInfo to verify it's non-zero
        uint8_t *p = (uint8_t *)&registInfo;
        bool allZero = true;
        for (int i = 0; i < 64 && i < (int)sizeof(registInfo); i++)
            if (p[i]) { allZero = false; break; }
        XYLog("DEBUG %s [STEP 8] initRegistrationInfo OK, size=%lu, allZero=%d\n",
              __FUNCTION__, sizeof(registInfo), allZero);
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
    // fRegistrationInfo must be allocated on ALL targets (including Tahoe).
    // Before these fixes, skipping it on Tahoe caused fNetIf->start() to hang.
    fNetIf->mExpansionData->fRegistrationInfo = (struct IOSkywalkNetworkInterface::RegistrationInfo *)IOMalloc(sizeof(struct IOSkywalkNetworkInterface::RegistrationInfo));
    fNetIf->mExpansionData2->fRegistrationInfo = (struct IOSkywalkEthernetInterface::RegistrationInfo *)IOMalloc(sizeof(struct IOSkywalkEthernetInterface::RegistrationInfo));
    memcpy(fNetIf->mExpansionData->fRegistrationInfo, &registInfo, sizeof(registInfo));
    memcpy(fNetIf->mExpansionData2->fRegistrationInfo, &registInfo, sizeof(registInfo));
    XYLog("DEBUG %s [STEP 8b] fNetIf=%p role=%d, calling deferBSDAttach + start\n",
          __FUNCTION__, fNetIf, fNetIf->getInterfaceRole());
    if (fNetIf->getInterfaceRole() == 1)
        fNetIf->deferBSDAttach(true);
    XYLog("DEBUG %s [STEP 8c] calling fNetIf->start(this=%p)\n", __FUNCTION__, this);
    fNetIf->start(this);
    XYLog("DEBUG %s [STEP 8d] fNetIf->start returned OK\n", __FUNCTION__);

    XYLog("DEBUG %s [STEP 9] enableAdapter\n", __FUNCTION__);
    setLinkStatus(kIONetworkLinkValid);
    if (TAILQ_EMPTY(&fHalService->get80211Controller()->ic_ess))
        fHalService->get80211Controller()->ic_flags |= IEEE80211_F_AUTO_JOIN;
    _fCommandGate->enable();
    power_state = kWiFiPowerOn;
    XYLog("DEBUG %s [STEP 9] enabling adapter, power_state=%u bsdInterface=%p\n", __FUNCTION__, power_state, bsdInterface);
    enableAdapter(bsdInterface);
    {
        struct ieee80211com *ic_dbg = fHalService->get80211Controller();
        struct _ifnet *ifp_dbg = &ic_dbg->ic_ac.ac_if;
        XYLog("DEBUG %s [STEP 9a] post-enable: ic_state=%d if_flags=0x%x ic_caps=0x%x ic_opmode=%d\n",
              __FUNCTION__, ic_dbg->ic_state, ifp_dbg->if_flags, ic_dbg->ic_caps, ic_dbg->ic_opmode);
    }
    registerService();
    XYLog("DEBUG %s [STEP 10] start COMPLETE power_state=%u pmPowerState=%u\n", __FUNCTION__, power_state, pmPowerState);
    return true;
}

void AirportItlwm::stop(IOService *provider)
{
    XYLog("DEBUG %s [1] entry power_state=%u pmPowerState=%u provider=%p fHalService=%p\n",
          __PRETTY_FUNCTION__, power_state, pmPowerState, provider, fHalService);
    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
    XYLog("DEBUG %s [2] disableAdapter bsdInterface=%p\n", __FUNCTION__, bsdInterface);
    disableAdapter(bsdInterface);
    XYLog("DEBUG %s [3] setLinkStatus\n", __FUNCTION__);
    setLinkStatus(kIONetworkLinkValid);
    XYLog("DEBUG %s [4] fHalService->detach pciNub=%p\n", __FUNCTION__, pciNub);
    fHalService->detach(pciNub);
    XYLog("DEBUG %s [5] ether_ifdetach ifp=%p\n", __FUNCTION__, ifp);
    ether_ifdetach(ifp);
    XYLog("DEBUG %s [6] detachInterface fNetIf=%p\n", __FUNCTION__, fNetIf);
    detachInterface(fNetIf, true);
    XYLog("DEBUG %s [7] release fNetIf\n", __FUNCTION__);
    OSSafeReleaseNULL(fNetIf);
    XYLog("DEBUG %s [8] releaseAll\n", __FUNCTION__);
    releaseAll();
    XYLog("DEBUG %s [9] super::stop\n", __FUNCTION__);
    super::stop(provider);
    XYLog("DEBUG %s [10] DONE\n", __FUNCTION__);
}

void AirportItlwm::free()
{
    XYLog("DEBUG %s [1] entry fHalService=%p syncFrameTemplate=%p roamProfile=%p btcProfile=%p\n",
          __PRETTY_FUNCTION__, fHalService, syncFrameTemplate, roamProfile, btcProfile);
    if (fHalService != NULL) {
        XYLog("DEBUG %s [2] releasing fHalService\n", __FUNCTION__);
        fHalService->release();
        fHalService = NULL;
    }
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
    XYLog("DEBUG %s [3] super::free\n", __FUNCTION__);
    super::free();
}

bool AirportItlwm::createWorkQueue()
{
    XYLog("DEBUG %s _fWorkloop=%p → %d\n", __FUNCTION__, _fWorkloop, _fWorkloop != 0);
    return _fWorkloop != 0;
}

#if __IO80211_TARGET >= __MAC_26_0
IO80211WorkQueue *AirportItlwm::getWorkQueue() const
#else
IO80211WorkQueue *AirportItlwm::getWorkQueue()
#endif
{
    static int sGetWorkQueueCount = 0;
    if (++sGetWorkQueueCount <= 3)
        XYLog("DEBUG %s #%d returning %p\n", __FUNCTION__, sGetWorkQueueCount, _fWorkloop);
    return _fWorkloop;
}

void *AirportItlwm::getFaultReporterFromDriver()
{
    XYLog("DEBUG %s returning %p\n", __FUNCTION__, driverFaultReporter);
    return driverFaultReporter;
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
    super::disable(netif);
    setLinkStatus(kIONetworkLinkValid);
    return kIOReturnSuccess;
}
#endif

bool AirportItlwm::configureInterface(IONetworkInterface *netif)
{
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
    fpNetStats->collisions = 0;
#if defined(__PRIVATE_SPI__) && __IO80211_TARGET < __MAC_26_0
    netif->configureOutputPullModel(fHalService->getDriverInfo()->getTxQueueSize(), 0, 0, IOEthernetInterface::kOutputPacketSchedulingModelNormal, 0);
#else
    XYLog("DEBUG %s Tahoe: skipping configureOutputPullModel\n", __FUNCTION__);
#endif
    XYLog("DEBUG %s DONE\n", __FUNCTION__);
    return true;
}

IONetworkInterface *AirportItlwm::createInterface()
{
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
    AirportItlwm *that = OSDynamicCast(AirportItlwm, target);
    XYLog("DEBUG %s linkState=%llu deauthReason=%u power_state=%u\n",
          __FUNCTION__, (uint64_t)arg0, (unsigned int)(uint64_t)arg1, that->power_state);
#if __IO80211_TARGET >= __MAC_26_0
    XYLog("DEBUG %s Tahoe: calling IO80211InfraInterface::setLinkState fNetIf=%p\n", __FUNCTION__, that->fNetIf);
    IOReturn ret = ((IO80211InfraInterface *)that->fNetIf)->setLinkState((IO80211LinkState)(uint64_t)arg0, (unsigned int)(uint64_t)arg1, false, 0, 0);
#else
    IOReturn ret = that->fNetIf->setLinkState((IO80211LinkState)(uint64_t)arg0, (unsigned int)(uint64_t)arg1);
#endif
    XYLog("DEBUG %s setLinkState ret=0x%x\n", __FUNCTION__, ret);
    that->fNetIf->setRunningState((IO80211LinkState)(uint64_t)arg0 == kIO80211NetworkLinkUp);
    that->fNetIf->postMessage(APPLE80211_M_LINK_CHANGED, NULL, 0, false);
    that->fNetIf->postMessage(APPLE80211_M_BSSID_CHANGED, NULL, 0, false);
    that->fNetIf->postMessage(APPLE80211_M_SSID_CHANGED, NULL, 0, false);
    if ((IO80211LinkState)(uint64_t)arg0 == kIO80211NetworkLinkUp) {
        that->fNetIf->reportLinkStatus(3, 0x80);
    } else {
        that->fNetIf->reportLinkStatus(1, 0);
    }
    XYLog("DEBUG %s calling bsdInterface->setLinkState bsdInterface=%p\n", __FUNCTION__, that->bsdInterface);
    that->bsdInterface->setLinkState((IO80211LinkState)(uint64_t)arg0);
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
        fHalService->disable(bsdInterface);
        fHalService->enable(bsdInterface);
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
    if (code == kIO80211Feature80211n) {
        return 0;
    }
    return 102;
}

#if __IO80211_TARGET >= __MAC_26_0
// vtable[429] — IO80211Controller::start() calls offset 0xd68 and stores the return
// in a global logger used by 28+ IO80211Family call sites.  Apple drivers override
// this releaseFlowQueue slot to return their CCLogStream*.
static int sReleaseFlowQueueCallCount = 0;
void *AirportItlwm::releaseFlowQueue(IO80211FlowQueue *)
{
    int n = ++sReleaseFlowQueueCallCount;
    if (n <= 10)
        XYLog("DEBUG [vtable429] releaseFlowQueue #%d driverLogStream=%p rc=%d\n",
              n, driverLogStream, driverLogStream ? driverLogStream->getRetainCount() : -1);
    return driverLogStream;
}

static int sRestrictedModeCallCount = 0;
bool AirportItlwm::isCommandAllowedInRestrictedMode(int command)
{
    int n = ++sRestrictedModeCallCount;
    if (n <= 10)
        XYLog("DEBUG [vtable431] isCommandAllowedInRestrictedMode #%d cmd=%d → false\n", n, command);
    return false;
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
    cd->capabilities[2] = 0xFF; // BURST, WME, SHORT_GI_40MHZ, SHORT_GI_20MHZ, WOW, TSN, ?, ?
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
        fNetIf->postMessage(APPLE80211_M_COUNTRY_CODE_CHANGED, NULL, 0, 0);
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
    if (!pd)
        return kIOReturnError;
    XYLog("%s num_radios=%d req=%u cur=%u pmPowerState=%u\n",
          __FUNCTION__, pd->num_radios,
          pd->num_radios > 0 ? pd->power_state[0] : 0, power_state, pmPowerState);
    if (pd->num_radios > 0)
        handlePowerStateChange(pd->power_state[0], bsdInterface);
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
    return kIOReturnSuccess;
}

void AirportItlwm::disableAdapter(IONetworkInterface *netif)
{
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
        thread_call_free(powerOffThreadCall);
        powerOffThreadCall = NULL;
    }
    if (powerOnThreadCall) {
        thread_call_free(powerOnThreadCall);
        powerOnThreadCall = NULL;
    }
}

IOReturn AirportItlwm::setPowerState(unsigned long powerStateOrdinal, IOService *policyMaker)
{
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
    unsigned long ret;
    if ((domainState >> 9) & 1)
        ret = kPowerStateOff;
    else
        ret = (domainState >> 1) & 1;
    XYLog("DEBUG %s domainState=0x%lx (DeviceUsable=%d PowerOn=%d) -> %lu power_state=%u pmPowerState=%u\n",
          __FUNCTION__, (unsigned long)domainState,
          (int)((domainState >> 9) & 1), (int)((domainState >> 1) & 1),
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
    XYLog("DEBUG handleSetPowerStateOff param1=%p gate=%p\n", param1, self->getCommandGate());

    if (param1 == 0)
    {
        self->getCommandGate()->runAction((IOCommandGate::Action)
                                           handleSetPowerStateOff,
                                           (void *) 1);
    }
    else
    {
        self->setPowerStateOff();
        self->release();
    }
    XYLog("DEBUG handleSetPowerStateOff DONE\n");
}

static void handleSetPowerStateOn(thread_call_param_t param0,
                            thread_call_param_t param1)
{
    AirportItlwm *self = (AirportItlwm *) param0;
    XYLog("DEBUG handleSetPowerStateOn param1=%p gate=%p\n", param1, self->getCommandGate());

    if (param1 == 0)
    {
        self->getCommandGate()->runAction((IOCommandGate::Action)
                                           handleSetPowerStateOn,
                                           (void *) 1);
    }
    else
    {
        self->setPowerStateOn();
        self->release();
    }
    XYLog("DEBUG handleSetPowerStateOn DONE\n");
}

IOReturn AirportItlwm::registerWithPolicyMaker(IOService *policyMaker)
{
    IOReturn ret;

    XYLog("DEBUG %s entry policyMaker=%p\n", __FUNCTION__, policyMaker);
    pmPowerState = kPowerStateOn;
    pmPolicyMaker = policyMaker;

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
    disableAdapter(bsdInterface);
    pmPolicyMaker->acknowledgeSetPowerState();
}

void AirportItlwm::setPowerStateOn()
{
    XYLog("DEBUG %s power_state=%u pmPowerState=%u\n", __FUNCTION__, power_state, pmPowerState);
    pmPowerState = kPowerStateOn;
    if (power_state)
        enableAdapter(bsdInterface);
    else
        XYLog("DEBUG %s SKIPPED enableAdapter (power_state=0)\n", __FUNCTION__);
    pmPolicyMaker->acknowledgeSetPowerState();
}
