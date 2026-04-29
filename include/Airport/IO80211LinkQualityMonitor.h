//
//  IO80211LinkQualityMonitor.h
//  itlwm
//
//  Reference-aligned forward declaration of the public direct-call
//  IO80211LinkQualityMonitor surface exported by IO80211Family on macOS
//  Tahoe.
//
//  All declared methods are non-virtual direct-call BootKC exports, so
//  the local class declaration deliberately omits any vtable or data
//  layout. Callers only ever hold an `IO80211LinkQualityMonitor *`
//  returned by the kernel; the local kext does not allocate, subclass,
//  or take `sizeof` of this class.
//
//  CR-196 — primitive-only batch (BootKC IO80211Family.kc, recovered
//  2026-04-28). Each declaration is anchored to its BootKC address in
//  the comment block below. Helpers whose signatures reference kernel-
//  internal struct/enum types (apple80211_*, RxErrors *, TxErrors *,
//  Limits *, IO80211NetworkPacket *, packet_info_tag *,
//  io80211BinCounters *, apple80211_homechan_qual_data *,
//  apple80211_lqm_event_data *, apple80211_tvpm_event *,
//  apple80211_lqm_summary *, IO80211LinkRecovery *, UIEvent / UIState /
//  LimitType nested enums) are deferred until those types are
//  recovered.
//
//  BootKC anchors:
//    ffffff800230b852  IO80211LinkQualityMonitor::induceTxBadPhy()
//    ffffff800230b792  IO80211LinkQualityMonitor::induceTxLatency()
//    ffffff800230b7c0  IO80211LinkQualityMonitor::induceTxLowPhyRate()
//    ffffff800230b946  IO80211LinkQualityMonitor::induceRxBadPhy()
//    ffffff800230b902  IO80211LinkQualityMonitor::induceRxHighOverflow()
//    ffffff800230b99c  IO80211LinkQualityMonitor::induceRxHighPNReplay()
//    ffffff800230b9d6  IO80211LinkQualityMonitor::induceRxHighDecryptError()
//    ffffff800230ba10  IO80211LinkQualityMonitor::induceRxHighMCDecryptError()
//    ffffff800230ba4a  IO80211LinkQualityMonitor::induceRxAmpduDupErrors()
//    ffffff800230bb5a  IO80211LinkQualityMonitor::induceSlowWiFiIfDebugTriggered()
//    ffffff800230c1de  IO80211LinkQualityMonitor::resetLatencyCoP()
//    ffffff800230c0a2  IO80211LinkQualityMonitor::resetMeasurements()
//    ffffff800230c68c  IO80211LinkQualityMonitor::analyzeTxLatency()
//    ffffff800230be5c  IO80211LinkQualityMonitor::analyzeMeasurements(bool)
//    ffffff800230ce8e  IO80211LinkQualityMonitor::requestUserInput()
//    ffffff800230cf68  IO80211LinkQualityMonitor::triggerUserInput()
//    ffffff800230d056  IO80211LinkQualityMonitor::triggerLinkProbe(bool)
//    ffffff800230cee0  IO80211LinkQualityMonitor::triggerIPFailRecovery()
//    ffffff800230d5f6  IO80211LinkQualityMonitor::updateCcaExtCaps(bool)
//    ffffff800230d5e4  IO80211LinkQualityMonitor::updateRealTimeAWDLActiveState(bool)
//    ffffff800230cd9c  IO80211LinkQualityMonitor::recordPhyActivity(long long, bool)
//    ffffff800230c720  IO80211LinkQualityMonitor::recordPhyRate(unsigned int, bool)
//    ffffff800230c654  IO80211LinkQualityMonitor::recordTxLatency(unsigned long long)
//    ffffff800230c792  IO80211LinkQualityMonitor::recordAMPDUDensity(unsigned int)
//    ffffff800230d398  IO80211LinkQualityMonitor::recordSymptomsInput(unsigned long long)
//    ffffff800230c2f6  IO80211LinkQualityMonitor::recordRxPacket(unsigned long long, unsigned long long)
//    ffffff800230d114  IO80211LinkQualityMonitor::recordLinkProbeResult(int)
//    ffffff800230d25a  IO80211LinkQualityMonitor::recordUserInputResult(int)
//    ffffff800230d400  IO80211LinkQualityMonitor::recordAWDLInfraDutyCycle(unsigned int)
//    ffffff800230d2c4  IO80211LinkQualityMonitor::recordEscoTrafficIndication(unsigned int)
//    ffffff800230cb9a  IO80211LinkQualityMonitor::recordInterfaceConcurrencyState(bool)
//    ffffff8002306e10  IO80211LinkQualityMonitor::getCCAIndex(signed char)
//    ffffff8002306dd8  IO80211LinkQualityMonitor::getRSSIIndex(signed char)
//    ffffff800230702e  IO80211LinkQualityMonitor::getNSSIndex(unsigned int)
//    ffffff8002306e44  IO80211LinkQualityMonitor::getExpectedPhyRate(signed char, signed char)
//    ffffff800230c418  IO80211LinkQualityMonitor::getCcaExtCaps()
//    ffffff800230d45a  IO80211LinkQualityMonitor::getChannelWidth()
//    ffffff8002307e60  IO80211LinkQualityMonitor::getElapsedPeriodMS()
//    ffffff800230c1cc  IO80211LinkQualityMonitor::getWorstAvgLatencyCoP()
//    ffffff800230c11e  IO80211LinkQualityMonitor::getWorstMaxLatencyCop()
//    ffffff8002309128  IO80211LinkQualityMonitor::armMeasurementTimer()
//    ffffff80023090ac  IO80211LinkQualityMonitor::checkMeasurementTimeout()
//    ffffff800230c130  IO80211LinkQualityMonitor::getOffChannelDurationUS()
//    ffffff800230a27e  IO80211LinkQualityMonitor::getTVPMActiveDurationMS()
//    ffffff800230a128  IO80211LinkQualityMonitor::getMaxQueueFullDurationMS()
//    ffffff800230d61c  IO80211LinkQualityMonitor::getRealTimeAWDLActiveState()
//    ffffff800230a1e6  IO80211LinkQualityMonitor::getConcurrentInterfaceActiveDurationMS()
//    ffffff800230d4a4  IO80211LinkQualityMonitor::getPeerMacAddress()
//

#ifndef IO80211LinkQualityMonitor_h
#define IO80211LinkQualityMonitor_h

class IO80211LinkQualityMonitor {
public:
    void induceTxBadPhy(void);
    void induceTxLatency(void);
    void induceTxLowPhyRate(void);
    void induceRxBadPhy(void);
    void induceRxHighOverflow(void);
    void induceRxHighPNReplay(void);
    void induceRxHighDecryptError(void);
    void induceRxHighMCDecryptError(void);
    void induceRxAmpduDupErrors(void);
    void induceSlowWiFiIfDebugTriggered(void);

    void resetLatencyCoP(void);
    void resetMeasurements(void);

    void analyzeTxLatency(void);
    bool analyzeMeasurements(bool);

    void requestUserInput(void);
    void triggerUserInput(void);
    bool triggerLinkProbe(bool);
    void triggerIPFailRecovery(void);

    void updateCcaExtCaps(bool);
    void updateRealTimeAWDLActiveState(bool);

    void recordPhyActivity(long long, bool);
    void recordPhyRate(unsigned int, bool);
    void recordTxLatency(unsigned long long);
    void recordAMPDUDensity(unsigned int);
    void recordSymptomsInput(unsigned long long);
    void recordRxPacket(unsigned long long, unsigned long long);
    void recordLinkProbeResult(int);
    void recordUserInputResult(int);
    void recordAWDLInfraDutyCycle(unsigned int);
    void recordEscoTrafficIndication(unsigned int);
    void recordInterfaceConcurrencyState(bool);

    int getCCAIndex(signed char);
    int getRSSIIndex(signed char);
    int getNSSIndex(unsigned int);
    unsigned int getExpectedPhyRate(signed char, signed char);

    unsigned int getCcaExtCaps(void);
    unsigned int getChannelWidth(void);
    unsigned long long getElapsedPeriodMS(void);
    unsigned long long getWorstAvgLatencyCoP(void);
    unsigned long long getWorstMaxLatencyCop(void);

    void armMeasurementTimer(void);
    bool checkMeasurementTimeout(void);

    unsigned long long getOffChannelDurationUS(void);
    unsigned long long getTVPMActiveDurationMS(void);
    unsigned long long getMaxQueueFullDurationMS(void);
    bool getRealTimeAWDLActiveState(void);
    unsigned long long getConcurrentInterfaceActiveDurationMS(void);

    unsigned char *getPeerMacAddress(void);
};

#endif /* IO80211LinkQualityMonitor_h */
