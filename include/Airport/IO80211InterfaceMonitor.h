//
//  IO80211InterfaceMonitor.h
//  itlwm
//
//  Reference-aligned forward declaration of the public direct-call
//  IO80211InterfaceMonitor surface exported by IO80211Family on macOS
//  Tahoe.
//
//  All declared methods are non-virtual direct-call BootKC exports, so
//  the local class declaration deliberately omits any vtable or data
//  layout. Callers only ever hold an `IO80211InterfaceMonitor *` returned
//  by the kernel; the local kext does not allocate, subclass, or take
//  `sizeof` of this class.
//
//  Symbol addresses (BootKC, IO80211Family.kc, recovered 2026-04-28):
//    ffffff80022f7938  IO80211InterfaceMonitor::getController()
//    ffffff80022f7568  IO80211InterfaceMonitor::getInputBytes()
//    ffffff80022f7536  IO80211InterfaceMonitor::getInputPackets()
//    ffffff80022f7694  IO80211InterfaceMonitor::getOutputBEBytes()
//    ffffff80022f76c6  IO80211InterfaceMonitor::getOutputBKBytes()
//    ffffff80022f772a  IO80211InterfaceMonitor::getOutputVIBytes()
//    ffffff80022f76f8  IO80211InterfaceMonitor::getOutputVOBytes()
//    ffffff80022f759a  IO80211InterfaceMonitor::getOutputBEPackets()
//    ffffff80022f75cc  IO80211InterfaceMonitor::getOutputBKPackets()
//    ffffff80022f7662  IO80211InterfaceMonitor::getOutputVIPackets()
//    ffffff80022f75fe  IO80211InterfaceMonitor::getOutputVOPackets()
//    ffffff80022f54f4  IO80211InterfaceMonitor::getInterfaceRSSI()
//    ffffff80022f521e  IO80211InterfaceMonitor::setInterfaceRSSI(long long)
//    ffffff80022f54c2  IO80211InterfaceMonitor::hasInterfaceRSSI()
//    ffffff80022f5530  IO80211InterfaceMonitor::setInterfaceSNR(long long)
//    ffffff80022f57f2  IO80211InterfaceMonitor::setInterfaceNF(long long)
//    ffffff80022ef27c  IO80211InterfaceMonitor::getLinkRate()
//    ffffff80022ef20c  IO80211InterfaceMonitor::setLinkRate(unsigned long long)
//    ffffff80022ef182  IO80211InterfaceMonitor::modifyChID(unsigned long long)
//
//  CR-182 additions (leaky-AP + reporter + packet-record helpers, BootKC
//  IO80211Family.kc, recovered 2026-04-28):
//    ffffff80022ef6c8  IO80211InterfaceMonitor::getLeakyApSsid(apple80211_ssid*)
//    ffffff80022ef714  IO80211InterfaceMonitor::getLeakyApBssid(ether_addr*)
//    ffffff80022eefa4  IO80211InterfaceMonitor::resetLeakyApStats()
//    ffffff80022f4f7c  IO80211InterfaceMonitor::setInputPacketRSSI(long long)
//    ffffff80022f784c  IO80211InterfaceMonitor::recordInputPacket(int, int)
//    ffffff80022f775c  IO80211InterfaceMonitor::recordOutputPacket(
//                          apple80211_wme_ac, int, int)
//    ffffff80022f1e2c  IO80211InterfaceMonitor::initFrameStats()
//    ffffff80022f281a  IO80211InterfaceMonitor::initHeFrameStats()
//    ffffff80022ef0fc  IO80211InterfaceMonitor::destroyReporters()
//    ffffff80022f752e  IO80211InterfaceMonitor::updateAllReports()
//

#ifndef IO80211InterfaceMonitor_h
#define IO80211InterfaceMonitor_h

class IO80211Controller;
struct apple80211_ssid;
struct ether_addr;
enum apple80211_wme_ac : unsigned int;

class IO80211InterfaceMonitor {
public:
    IO80211Controller *getController(void);

    unsigned long long getInputBytes(void);
    unsigned long long getInputPackets(void);
    unsigned long long getOutputBEBytes(void);
    unsigned long long getOutputBKBytes(void);
    unsigned long long getOutputVIBytes(void);
    unsigned long long getOutputVOBytes(void);
    unsigned long long getOutputBEPackets(void);
    unsigned long long getOutputBKPackets(void);
    unsigned long long getOutputVIPackets(void);
    unsigned long long getOutputVOPackets(void);

    long long getInterfaceRSSI(void);
    void setInterfaceRSSI(long long);
    bool hasInterfaceRSSI(void);
    void setInterfaceSNR(long long);
    void setInterfaceNF(long long);

    unsigned long long getLinkRate(void);
    void setLinkRate(unsigned long long);
    void modifyChID(unsigned long long);

    // CR-182 additions: leaky-AP, reporter, and packet-record helpers.
    // Recovered from IO80211Family BootKernelExtensions.kc on 2026-04-28:
    //   ffffff80022ef6c8 IO80211InterfaceMonitor::getLeakyApSsid(apple80211_ssid*)
    //   ffffff80022ef714 IO80211InterfaceMonitor::getLeakyApBssid(ether_addr*)
    //   ffffff80022eefa4 IO80211InterfaceMonitor::resetLeakyApStats()
    //   ffffff80022f4f7c IO80211InterfaceMonitor::setInputPacketRSSI(long long)
    //   ffffff80022f784c IO80211InterfaceMonitor::recordInputPacket(int, int)
    //   ffffff80022f775c IO80211InterfaceMonitor::recordOutputPacket(
    //                       apple80211_wme_ac, int, int)
    //   ffffff80022f1e2c IO80211InterfaceMonitor::initFrameStats()
    //   ffffff80022f281a IO80211InterfaceMonitor::initHeFrameStats()
    //   ffffff80022ef0fc IO80211InterfaceMonitor::destroyReporters()
    //   ffffff80022f752e IO80211InterfaceMonitor::updateAllReports()
    void getLeakyApSsid(apple80211_ssid *);
    void getLeakyApBssid(ether_addr *);
    void resetLeakyApStats(void);
    void setInputPacketRSSI(long long);
    void recordInputPacket(int, int);
    void recordOutputPacket(apple80211_wme_ac, int, int);
    void initFrameStats(void);
    void initHeFrameStats(void);
    void destroyReporters(void);
    void updateAllReports(void);

    // CR-188 additions: primitive-only effective-rate / leaky-AP / CCA /
    // DPS / activity / latency helpers recovered from BootKC IO80211Family
    // on 2026-04-28. None override IOService or IOReportingProvider
    // virtuals (verified by header grep against the kernel SDK).
    //   ffffff80022efe74 IO80211InterfaceMonitor::getEffectiveLinkRate()
    //   ffffff80022efe16 IO80211InterfaceMonitor::setEffectiveLinkRate(unsigned long long)
    //   ffffff80022eff00 IO80211InterfaceMonitor::getEffectiveDataTransferRate()
    //   ffffff80022efea2 IO80211InterfaceMonitor::setEffectiveDataTransferRate(unsigned long long)
    //   ffffff80022ef2aa IO80211InterfaceMonitor::setDataTransferRates(unsigned long long)
    //   ffffff80022eff2e IO80211InterfaceMonitor::setExpectedPeakLatency(unsigned long long)
    //   ffffff80022f0458 IO80211InterfaceMonitor::getInterfaceAverageCCA()
    //   ffffff80022f0424 IO80211InterfaceMonitor::hasInterfaceAverageCCA()
    //   ffffff80022f0122 IO80211InterfaceMonitor::setInterfaceAverageCCA(unsigned long long)
    //   ffffff80022f0044 IO80211InterfaceMonitor::setInterfaceOpenPercent(unsigned long long)
    //   ffffff80022f5abe IO80211InterfaceMonitor::setInterfaceOFDMDesense(long long)
    //   ffffff80022f7266 IO80211InterfaceMonitor::incrementDPSDetected()
    //   ffffff80022f72a0 IO80211InterfaceMonitor::incrementConsecutiveDPS()
    //   ffffff80022ef414 IO80211InterfaceMonitor::isBssidMetricsLoaded()
    //   ffffff80022ef73e IO80211InterfaceMonitor::isLeakyApSsidBssidValid()
    //   ffffff80022ef39e IO80211InterfaceMonitor::isLeakyApSsidMatchesSsidMetrics()
    //   ffffff80022ef370 IO80211InterfaceMonitor::resetLeakyApSsidMetrics()
    //   ffffff80022ef31a IO80211InterfaceMonitor::resetLeakyApBssidMetrics()
    //   ffffff80022efc82 IO80211InterfaceMonitor::updateLeakyApStatus()
    //   ffffff80022ef4c2 IO80211InterfaceMonitor::updateLeakyApNetwork()
    //   ffffff80022f7fb4 IO80211InterfaceMonitor::setPreviousInterfaceActivity()
    //   ffffff80022f715c IO80211InterfaceMonitor::setLQM(int)
    //   ffffff80022efc26 IO80211InterfaceMonitor::getLowRxRatePeriodRange(unsigned int)
    //   ffffff80022f73f2 IO80211InterfaceMonitor::getEffectiveRxBWSinceLastRead(unsigned int*)
    //   ffffff80022f72da IO80211InterfaceMonitor::getEffectiveTxBWSinceLastRead(unsigned int*)
    //   ffffff80022f65d2 IO80211InterfaceMonitor::aggregatedPeersTxLatency(unsigned int, unsigned long long)
    //   ffffff80022ef7fe IO80211InterfaceMonitor::loadLeakyApBssidMetricsFromSsidMetrics()
    unsigned long long getEffectiveLinkRate(void);
    void setEffectiveLinkRate(unsigned long long);
    unsigned long long getEffectiveDataTransferRate(void);
    void setEffectiveDataTransferRate(unsigned long long);
    void setDataTransferRates(unsigned long long);
    void setExpectedPeakLatency(unsigned long long);
    unsigned long long getInterfaceAverageCCA(void);
    bool hasInterfaceAverageCCA(void);
    void setInterfaceAverageCCA(unsigned long long);
    void setInterfaceOpenPercent(unsigned long long);
    void setInterfaceOFDMDesense(long long);
    void incrementDPSDetected(void);
    void incrementConsecutiveDPS(void);
    bool isBssidMetricsLoaded(void);
    bool isLeakyApSsidBssidValid(void);
    bool isLeakyApSsidMatchesSsidMetrics(void);
    void resetLeakyApSsidMetrics(void);
    void resetLeakyApBssidMetrics(void);
    void updateLeakyApStatus(void);
    void updateLeakyApNetwork(void);
    void setPreviousInterfaceActivity(void);
    void setLQM(int);
    unsigned int getLowRxRatePeriodRange(unsigned int);
    void getEffectiveRxBWSinceLastRead(unsigned int *);
    void getEffectiveTxBWSinceLastRead(unsigned int *);
    void aggregatedPeersTxLatency(unsigned int, unsigned long long);
    void loadLeakyApBssidMetricsFromSsidMetrics(void);
};

#endif /* IO80211InterfaceMonitor_h */
