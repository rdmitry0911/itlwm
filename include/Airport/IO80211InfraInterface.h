//
//  IO80211InfraInterface.h
//  itlwm
//
//  Created by qcwap on 2023/6/12.
//  Copyright © 2023 钟先耀. All rights reserved.
//

#ifndef IO80211InfraInterface_h
#define IO80211InfraInterface_h

struct apple80211_wcl_advisory_info;
struct apple80211_wcl_tx_rx_latency;
struct apple80211_wcl_update_link_state;
class IO80211TimerSource;

class IO80211InfraInterface : public IO80211SkywalkInterface {
    OSDeclareAbstractStructors(IO80211InfraInterface)
    
public:
    virtual bool init() APPLE_KEXT_OVERRIDE;
#if __IO80211_TARGET >= __MAC_26_0
    // Tahoe bring-up path: allocates self+0x120 block and sets infra-specific
    // state; must be called instead of the SkywalkInterface overload so that
    // BSD attach, PostOffice/Glue event delivery and WCL scan completion work.
    virtual bool init(IOService *, ether_addr *) APPLE_KEXT_OVERRIDE;
    IOReturn registerInfraEthernetInterface(
        IOSkywalkEthernetInterface::RegistrationInfo *,
        IOSkywalkPacketQueue **,
        unsigned int,
        IOSkywalkPacketBufferPool *,
        IOSkywalkPacketBufferPool *);
#endif
    virtual void free() APPLE_KEXT_OVERRIDE;
    virtual IOReturn configureReport(IOReportChannelList *,UInt,void *,void *) APPLE_KEXT_OVERRIDE;
    virtual IOReturn updateReport(IOReportChannelList *,UInt,void *,void *) APPLE_KEXT_OVERRIDE;
    virtual bool start(IOService *) APPLE_KEXT_OVERRIDE;
    virtual SInt32 initBSDInterfaceParameters(ifnet_init_eparams *,sockaddr_dl **) APPLE_KEXT_OVERRIDE;
    virtual bool prepareBSDInterface(ifnet_t, UInt) APPLE_KEXT_OVERRIDE;
    virtual IOReturn processBSDCommand(ifnet_t, UInt, void *) APPLE_KEXT_OVERRIDE;
    virtual SInt32 setInterfaceEnable(bool) APPLE_KEXT_OVERRIDE;
    virtual UInt getHardwareAssists(void) APPLE_KEXT_OVERRIDE;
    virtual bool bpfTap(UInt,UInt) APPLE_KEXT_OVERRIDE;
    // getHardwareAddress / setHardwareAddress NOT overridden here:
    // kernel exports these on IO80211SkywalkInterface, not IO80211InfraInterface.
    // Declaring overrides here generates unresolvable symbols.
    virtual void postMessage(UInt,void *,unsigned long,bool) APPLE_KEXT_OVERRIDE;
    virtual IOReturn recordOutputPackets(TxSubmissionDequeueStats *,TxSubmissionDequeueStats *) APPLE_KEXT_OVERRIDE;
    virtual void logTxPacket(IO80211NetworkPacket *,PacketSkywalkScratch *,apple80211_wme_ac,bool) APPLE_KEXT_OVERRIDE;
#if __IO80211_TARGET >= __MAC_26_0
    virtual void logTxCompletionPacket(IO80211NetworkPacket *,PacketSkywalkScratch *,unsigned char *,apple80211_wme_ac,int,UInt,bool,bool) APPLE_KEXT_OVERRIDE;
#else
    virtual void logTxCompletionPacket(IO80211NetworkPacket *,PacketSkywalkScratch *,unsigned char *,apple80211_wme_ac,int,UInt,bool) APPLE_KEXT_OVERRIDE;
#endif
    virtual IOReturn recordCompletionPackets(TxCompletionEnqueueStats *,TxCompletionEnqueueStats *) APPLE_KEXT_OVERRIDE;
#if __IO80211_TARGET >= __MAC_26_0
    virtual IOReturn inputPacket(IO80211NetworkPacket *,packet_info_tag *,ether_header *,bool *,bool) APPLE_KEXT_OVERRIDE;
#else
    virtual IOReturn inputPacket(IO80211NetworkPacket *,packet_info_tag *,ether_header *,bool *) APPLE_KEXT_OVERRIDE;
#endif
    virtual SInt64 pendingPackets(unsigned char) APPLE_KEXT_OVERRIDE;
    virtual SInt64 packetSpace(unsigned char) APPLE_KEXT_OVERRIDE;
    virtual bool isDebounceOnGoing(void) APPLE_KEXT_OVERRIDE;
#if __IO80211_TARGET < __MAC_26_0
    virtual bool setLinkState(IO80211LinkState,UInt,bool debounceTimeout = 30,UInt code = 0) APPLE_KEXT_OVERRIDE;
#endif
    virtual IO80211LinkState linkState(void) APPLE_KEXT_OVERRIDE;
    virtual void setScanningState(UInt,bool,apple80211_scan_data *,int) APPLE_KEXT_OVERRIDE;
    virtual void setDataPathState(bool) APPLE_KEXT_OVERRIDE;
    virtual void *getScanManager(void) APPLE_KEXT_OVERRIDE;
    virtual void updateLinkParameters(apple80211_interface_availability *) APPLE_KEXT_OVERRIDE;
    virtual void updateInterfaceCoexRiskPct(unsigned long long) APPLE_KEXT_OVERRIDE;
    virtual void setLQM(unsigned long long) APPLE_KEXT_OVERRIDE;
    virtual void updateLinkStatus(void) APPLE_KEXT_OVERRIDE;
    virtual void updateLinkStatusGated(void) APPLE_KEXT_OVERRIDE;
    virtual void setInterfaceExtendedCCA(apple80211_channel,apple80211_cca_report *) APPLE_KEXT_OVERRIDE;
    virtual void setInterfaceCCA(apple80211_channel,int) APPLE_KEXT_OVERRIDE;
    virtual void setInterfaceNF(apple80211_channel,long long) APPLE_KEXT_OVERRIDE;
    virtual void setInterfaceOFDMDesense(apple80211_channel,long long) APPLE_KEXT_OVERRIDE;
    virtual void setDebugFlags(unsigned long long,UInt) APPLE_KEXT_OVERRIDE;
    virtual SInt64 debugFlags(void) APPLE_KEXT_OVERRIDE;
    virtual void setInterfaceChipCounters(apple80211_stat_report *,apple80211_chip_counters_tx *,apple80211_chip_error_counters_tx *,apple80211_chip_counters_rx *) APPLE_KEXT_OVERRIDE;
    virtual void setInterfaceMIBdot11(apple80211_stat_report *,apple80211_ManagementInformationBasedot11_counters *) APPLE_KEXT_OVERRIDE;
    virtual void setFrameStats(apple80211_stat_report *,apple80211_frame_counters *) APPLE_KEXT_OVERRIDE;
#if __IO80211_TARGET >= __MAC_14_4
    virtual void setInfraSpecificFrameStats(apple80211_stat_report *,apple80211_infra_specific_stats *) APPLE_KEXT_OVERRIDE;
#endif
    virtual SInt64 getWmeTxCounters(unsigned long long *) APPLE_KEXT_OVERRIDE;
#if __IO80211_TARGET < __MAC_26_0
    virtual void setEnabledBySystem(bool) APPLE_KEXT_OVERRIDE;
    virtual bool enabledBySystem(void) APPLE_KEXT_OVERRIDE;
    virtual bool willRoam(ether_addr *,UInt) APPLE_KEXT_OVERRIDE;
#endif
    virtual void setPeerManagerLogFlag(UInt,UInt,UInt) APPLE_KEXT_OVERRIDE;
    virtual void setWoWEnabled(bool) APPLE_KEXT_OVERRIDE;
    virtual bool wowEnabled(void) APPLE_KEXT_OVERRIDE;
#if __IO80211_TARGET >= __MAC_26_0
    virtual UInt64 createLinkQualityMonitor(IO80211Peer *,bool) APPLE_KEXT_OVERRIDE;
#else
    virtual UInt64 createLinkQualityMonitor(IO80211Peer *,IOService *) APPLE_KEXT_OVERRIDE;
#endif
    virtual void releaseLinkQualityMonitor(IO80211Peer *) APPLE_KEXT_OVERRIDE;
    virtual int getAssocState(void) APPLE_KEXT_OVERRIDE;
    virtual void *getLQMSummary(apple80211_lqm_summary *) APPLE_KEXT_OVERRIDE;

#if __IO80211_TARGET >= __MAC_26_0
    // Tahoe: setLinkState moved here from IO80211SkywalkInterface [463]
    virtual bool setLinkState(IO80211LinkState,UInt,bool,UInt,UInt);
    // [464]
    virtual bool setLinkStateInternal(IO80211LinkState,uint,bool,uint,uint);
    // [465]
    virtual void setCurrentApAddress(ether_addr *);
    // [466]
    virtual void setWCL_ADVISORTY_INFO(apple80211_wcl_advisory_info *);
    // [467]
    virtual void *getWCL_TX_RX_LATENCY(apple80211_wcl_tx_rx_latency *);
    // [468]
    virtual IOReturn setWCL_LINK_STATE_UPDATE(apple80211_wcl_update_link_state *);
    // [469]
    virtual void createLQMData(void);

    // Non-virtual exported Tahoe helpers. Recovered from
    // IO80211Family BootKernelExtensions.kc symbol table on 2026-04-28:
    //   ffffff80022e1148 IO80211InfraInterface::getInfraPeer()
    //   ffffff80022e5ef8 IO80211InfraInterface::getCurrentApAddress()
    //   ffffff80022e6f9c IO80211InfraInterface::handleKeyDone(bool, bool)
    //   ffffff80022e116e IO80211InfraInterface::bssidChange(void*, unsigned long)
    // These are direct-call symbols, not vtable entries, so they do not
    // shift the vtable layout. Declared here so callers can link against
    // the exported symbols without forcing per-caller extern shims.
    IO80211Peer *getInfraPeer(void);
    ether_addr *getCurrentApAddress(void);
    void handleKeyDone(bool, bool);
    void bssidChange(void *, unsigned long);

    // CR-181 additions: IORegistry property updaters and runtime helpers.
    // Recovered from IO80211Family BootKernelExtensions.kc on 2026-04-28:
    //   ffffff80022e1a56 IO80211InfraInterface::updateSSIDProperty()
    //   ffffff80022e2504 IO80211InfraInterface::updateLocaleProperty()
    //   ffffff80022e1f98 IO80211InfraInterface::updateBSSIDProperty(
    //                       ether_addr&, apple80211_channel&, bool, bool)
    //   ffffff80022e2156 IO80211InfraInterface::updateChannelProperty(
    //                       apple80211_channel&)
    //   ffffff80022e1b90 IO80211InfraInterface::updateCountryCodeProperty(bool)
    //   ffffff80022de8b2 IO80211InfraInterface::updateStaticProperties()
    //   ffffff80022df728 IO80211InfraInterface::updateLinkSpeed()
    //   ffffff80022e1782 IO80211InfraInterface::loadHwChannels()
    //   ffffff80022e1848 IO80211InfraInterface::loadChannelInfo()
    //   ffffff80022e61ea IO80211InfraInterface::onDispatchQueue()
    //   ffffff80022dfb9c IO80211InfraInterface::cancelDebounceTimer()
    void updateSSIDProperty(void);
    void updateLocaleProperty(void);
    void updateBSSIDProperty(ether_addr &, apple80211_channel &, bool, bool);
    void updateChannelProperty(apple80211_channel &);
    void updateCountryCodeProperty(bool);
    void updateStaticProperties(void);
    void updateLinkSpeed(void);
    void loadHwChannels(void);
    void loadChannelInfo(void);
    bool onDispatchQueue(void);
    void cancelDebounceTimer(void);

    // CR-186 additions: LQM / WMM / AVC / BT-coex / traffic-monitor /
    // SIB / ULLA / AWDL / BPF / leaky-AP / supplicant / P2P helpers.
    // Recovered from IO80211Family BootKernelExtensions.kc on 2026-04-28:
    //   ffffff80022e4446 IO80211InfraInterface::getLQMData()
    //   ffffff80022e451c IO80211InfraInterface::setLQMGated(unsigned long long)
    //   ffffff80022e44c4 IO80211InfraInterface::setLQMStatic(void*, void*)
    //   ffffff80022e5d1e IO80211InfraInterface::getMonitorMode()
    //   ffffff80022e5ca2 IO80211InfraInterface::getWMMBWReset()
    //   ffffff80022e5cb8 IO80211InfraInterface::setWMMBWReset(bool)
    //   ffffff80022e14cc IO80211InfraInterface::getAVCAdvisory()
    //   ffffff80022e66e0 IO80211InfraInterface::getBtCoexState()
    //   ffffff80022e190e IO80211InfraInterface::resetInterface(void*, unsigned long)
    //   ffffff80022e5dce IO80211InfraInterface::getTrafficMonitor()
    //   ffffff80022e1386 IO80211InfraInterface::finishSIBCoexTimer()
    //   ffffff80022e3aca IO80211InfraInterface::resetSIBTurnOnMetrics()
    //   ffffff80022e3a8a IO80211InfraInterface::getCoPTxRTSFailCount()
    //   ffffff80022e3a9e IO80211InfraInterface::getULLALiteDuration()
    //   ffffff80022e39e6 IO80211InfraInterface::getAwdlMaxBandWidth()
    //   ffffff80022e57fe IO80211InfraInterface::notifyAWDLStateChange(bool)
    //   ffffff80022e58d6 IO80211InfraInterface::bpfTapInternal(unsigned int,
    //                                                          unsigned int)
    //   ffffff80022e3784 IO80211InfraInterface::setLeakyAPStatsMode(unsigned int)
    //   ffffff80022e12f2 IO80211InfraInterface::UpdateULLADuration(
    //                                                          unsigned long long*)
    //   ffffff80022e1e0c IO80211InfraInterface::handleSupplicantEvent(void*,
    //                                                                unsigned long)
    //   ffffff80022e1e3a IO80211InfraInterface::routeToP2PInterface(unsigned int,
    //                                                              void*,
    //                                                              unsigned long)
    void *getLQMData(void);
    void setLQMGated(unsigned long long);
    void setLQMStatic(void *, void *);
    unsigned int getMonitorMode(void);
    bool getWMMBWReset(void);
    void setWMMBWReset(bool);
    void *getAVCAdvisory(void);
    unsigned int getBtCoexState(void);
    void resetInterface(void *, unsigned long);
    void *getTrafficMonitor(void);
    void finishSIBCoexTimer(void);
    void resetSIBTurnOnMetrics(void);
    unsigned long long getCoPTxRTSFailCount(void);
    unsigned long long getULLALiteDuration(void);
    unsigned int getAwdlMaxBandWidth(void);
    void notifyAWDLStateChange(bool);
    bool bpfTapInternal(unsigned int, unsigned int);
    void setLeakyAPStatsMode(unsigned int);
    void UpdateULLADuration(unsigned long long *);
    void handleSupplicantEvent(void *, unsigned long);
    void routeToP2PInterface(unsigned int, void *, unsigned long);

    // CR-189 additions: 5G low/high band switch counters, CoP SIB coex
    // turn-on metrics, ULLA classic duration, CoP TX-RTS reset,
    // tx-path health-check reset, infra-peers-logging toggle, report
    // dispatchers (sync/static/timer) and link/multicast/timer helpers
    // recovered from BootKC IO80211Family on 2026-04-28. All are
    // direct-call non-vtable exports; they take only primitives or an
    // opaque IO80211TimerSource * (forward-declared above).
    //   ffffff80022e3a38 IO80211InfraInterface::get5GLowHighBandSwitchCounter()
    //   ffffff80022e3a4c IO80211InfraInterface::get5GLowHighBandSwitchSuccessPerc()
    //   ffffff80022e3be6 IO80211InfraInterface::getCoPSIBCoexTurnOnCount()
    //   ffffff80022e3bfa IO80211InfraInterface::getCoPSIBCoexTurnOnDuration()
    //   ffffff80022e3a76 IO80211InfraInterface::getULLAClassicDuration()
    //   ffffff80022e3ab2 IO80211InfraInterface::resetCoPTxRTSFailCount()
    //   ffffff80022e4b6a IO80211InfraInterface::resetTxPathHealthCheck()
    //   ffffff80022e3ccc IO80211InfraInterface::setInfraPeersLoggingEnabled(bool)
    //   ffffff80022e42fa IO80211InfraInterface::reportDataTransferRates()
    //   ffffff80022e440c IO80211InfraInterface::reportDataTransferRatesStatic(void*)
    //   ffffff80022ddfd0 IO80211InfraInterface::reportDataTransferRatesTimer(IO80211TimerSource*)
    //   ffffff80022dded2 IO80211InfraInterface::triggerLinkStatusUpdate(IO80211TimerSource*)
    //   ffffff80022e29ea IO80211InfraInterface::handleLeakyApStatsResetTimer(IO80211TimerSource*)
    //   ffffff80022ddf66 IO80211InfraInterface::restoreMulticastStateTimer(IO80211TimerSource*)
    //   ffffff80022e4084 IO80211InfraInterface::updateLinkParametersStatic(void*, void*)
    //   ffffff80022e45e2 IO80211InfraInterface::updateLinkStatusStatic(void*)
    //   ffffff80022e62cc IO80211InfraInterface::updateTxRxLatency()
    //   ffffff80022de4b2 IO80211InfraInterface::publishOffloadCapability()
    unsigned int get5GLowHighBandSwitchCounter(void);
    unsigned int get5GLowHighBandSwitchSuccessPerc(void);
    unsigned long long getCoPSIBCoexTurnOnCount(void);
    unsigned long long getCoPSIBCoexTurnOnDuration(void);
    unsigned long long getULLAClassicDuration(void);
    void resetCoPTxRTSFailCount(void);
    void resetTxPathHealthCheck(void);
    void setInfraPeersLoggingEnabled(bool);
    void reportDataTransferRates(void);
    void reportDataTransferRatesStatic(void *);
    void reportDataTransferRatesTimer(IO80211TimerSource *);
    void triggerLinkStatusUpdate(IO80211TimerSource *);
    void handleLeakyApStatsResetTimer(IO80211TimerSource *);
    void restoreMulticastStateTimer(IO80211TimerSource *);
    void updateLinkParametersStatic(void *, void *);
    void updateLinkStatusStatic(void *);
    void updateTxRxLatency(void);
    void publishOffloadCapability(void);
#else
    virtual IOReturn setLinkStateInternal(IO80211LinkState,uint,bool,uint,apple80211_link_changed_event_data &);
    virtual void setPoweredOnByUser(bool);
    virtual void setCurrentBssid(ether_addr *);
    virtual void setWCL_ADVISORTY_INFO(apple80211_wcl_advisory_info *);
    virtual void *getWCL_TX_RX_LATENCY(apple80211_wcl_tx_rx_latency *);
#endif

public:
    char _data[0x120];
};

#endif /* IO80211InfraInterface_h */
