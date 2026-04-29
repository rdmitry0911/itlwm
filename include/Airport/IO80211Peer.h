//
//  IO80211Peer.h
//  itlwm
//
//  Reference-aligned forward declaration of the public direct-call
//  IO80211Peer surface exported by IO80211Family on macOS Tahoe.
//
//  All declared methods are non-virtual direct-call BootKC exports.
//  The local class declaration deliberately omits any base class,
//  vtable, or data layout. Callers only ever hold an `IO80211Peer *`
//  returned by the kernel; the local kext does not allocate, subclass,
//  or take `sizeof` of this class.
//
//  Symbol addresses (BootKC, IO80211Family.kc, recovered 2026-04-28):
//    ffffff80021bf64a  IO80211Peer::withAddressAndManager(
//                          unsigned char const*, IO80211PeerManager*)
//    ffffff80021bf6c0  IO80211Peer::init()
//    ffffff80021bff7a  IO80211Peer::getMacAddress()
//    ffffff80021c5df4  IO80211Peer::setMacAddress(ether_addr*)
//    ffffff80021c3558  IO80211Peer::getManager()
//    ffffff80021c60dc  IO80211Peer::getGeneration()
//
//  CR-183 additions (capability / credit / TX-RX counter accessors,
//  BootKC IO80211Family.kc, recovered 2026-04-28):
//    ffffff80021c3376  IO80211Peer::getHtCapable()
//    ffffff80021c3388  IO80211Peer::setHtCapable(bool)
//    ffffff80021c339a  IO80211Peer::getVhtCapable()
//    ffffff80021c33ac  IO80211Peer::setVhtCapable(bool)
//    ffffff80021c33ce  IO80211Peer::isHeSupported()
//    ffffff80021c33e0  IO80211Peer::setHeSupported(bool)
//    ffffff80021c3836  IO80211Peer::is6ECapable()
//    ffffff80021c3824  IO80211Peer::set6ECapable(bool)
//    ffffff80021c5d86  IO80211Peer::hasHTorVHTCaps()
//    ffffff80021c3af0  IO80211Peer::canTransmit(unsigned int, unsigned int)
//    ffffff80021c3a1c  IO80211Peer::canTransmitReason(unsigned int, unsigned int)
//    ffffff80021c5f02  IO80211Peer::getOpenCredits()
//    ffffff80021c5ef4  IO80211Peer::getCloseCredits()
//    ffffff80021c5fcc  IO80211Peer::getNumTxPacket()
//    ffffff80021c3814  IO80211Peer::getOutputSuccess()
//    ffffff80021c370a  IO80211Peer::getTxQuantum()
//    ffffff80021c371a  IO80211Peer::setTxQuantum(unsigned int)
//    ffffff80021c5dd6  IO80211Peer::getNextTxSeq(unsigned char)
//    ffffff80021c3682  IO80211Peer::getRxSequence()
//    ffffff80021c3672  IO80211Peer::getRxSequenceMulticast()
//    ffffff80021c372a  IO80211Peer::setTransmitOk(bool)
//    ffffff80021c5da0  IO80211Peer::isCachedInFw()
//    ffffff80021c5db2  IO80211Peer::setCachedInFw(bool)
//    ffffff80021c5ec8  IO80211Peer::isSoftAPPeer()
//    ffffff80021c5ed8  IO80211Peer::setSoftAPPeer(bool)
//
//  CR-184 additions (RSSI / packet-stats / cache / queue helpers,
//  BootKC IO80211Family.kc, recovered 2026-04-28):
//    ffffff80021bfcb8  IO80211Peer::getStatsID()
//    ffffff80021bfca6  IO80211Peer::getStatsIDValid()
//    ffffff80021c2fb0  IO80211Peer::reportRssi(int, apple80211_channel)
//    ffffff80021c30b6  IO80211Peer::reportChainRssi(signed char const*, int)
//    ffffff80021c5e2a  IO80211Peer::getAvgRssi24G()
//    ffffff80021c5e3a  IO80211Peer::getAvgRssi5G()
//    ffffff80021c323e  IO80211Peer::getAvgRssiAcrossBands()
//    ffffff80021c36d6  IO80211Peer::getAvgChainRssi5G()
//    ffffff80021c5e6e  IO80211Peer::setPeerAvgRssi24G(signed char)
//    ffffff80021c5e7e  IO80211Peer::setPeerAvgRssi5G(signed char)
//    ffffff80021c2f94  IO80211Peer::simulateDPS()
//    ffffff80021bfbb2  IO80211Peer::freeResources()
//    ffffff80021c2ce8  IO80211Peer::unpauseQueues()
//    ffffff80021bfa80  IO80211Peer::reclaimPackets()
//    ffffff80021c29e6  IO80211Peer::clearCacheState()
//    ffffff80021c3432  IO80211Peer::getRxBitField(unsigned int)
//    ffffff80021c344c  IO80211Peer::getRxBitFieldMulticast(unsigned int)
//    ffffff80021c511e  IO80211Peer::incrementRxCount(unsigned int)
//    ffffff80021c32e0  IO80211Peer::getPacketStats()
//    ffffff80021c3348  IO80211Peer::getPacketStatsRealTimeRx()
//    ffffff80021c331a  IO80211Peer::getPacketStatsRealTimeTx()
//    ffffff80021c348a  IO80211Peer::getCumDataStats()
//    ffffff80021c60ae  IO80211Peer::hasRealTimeData()
//    ffffff80021c609c  IO80211Peer::hasLowLatencyData()
//    ffffff80021bfdcc  IO80211Peer::hasQueuedPackets()
//    ffffff80021c57e4  IO80211Peer::getDataLinkCount()
//    ffffff80021c5bbc  IO80211Peer::logPeerTxLatency(unsigned long long)
//    ffffff80021c3266  IO80211Peer::updateQueueState(int)
//    ffffff80021c37b0  IO80211Peer::getCacheTimeStamp()
//    ffffff80021c37be  IO80211Peer::setCacheTimeStamp(unsigned long long)
//    ffffff80021c373c  IO80211Peer::setPacketLifetime(unsigned int)
//    ffffff80021c5e8e  IO80211Peer::isBssSteeringPeer()
//    ffffff80021c5eaa  IO80211Peer::isBssSteeringPeerSyncState()
//

#ifndef IO80211Peer_h
#define IO80211Peer_h

class IO80211PeerManager;
struct ether_addr;
struct apple80211_channel;

class IO80211Peer {
public:
    static IO80211Peer *withAddressAndManager(unsigned char const *addr,
                                              IO80211PeerManager *manager);
    bool init(void);
    ether_addr *getMacAddress(void);
    void setMacAddress(ether_addr *addr);
    IO80211PeerManager *getManager(void);
    unsigned int getGeneration(void);

    // CR-183: capability / credit / TX-RX counter accessors.
    bool getHtCapable(void);
    void setHtCapable(bool);
    bool getVhtCapable(void);
    void setVhtCapable(bool);
    bool isHeSupported(void);
    void setHeSupported(bool);
    bool is6ECapable(void);
    void set6ECapable(bool);
    bool hasHTorVHTCaps(void);
    bool canTransmit(unsigned int, unsigned int);
    unsigned int canTransmitReason(unsigned int, unsigned int);
    unsigned int getOpenCredits(void);
    unsigned int getCloseCredits(void);
    unsigned long long getNumTxPacket(void);
    bool getOutputSuccess(void);
    unsigned int getTxQuantum(void);
    void setTxQuantum(unsigned int);
    unsigned char getNextTxSeq(unsigned char);
    unsigned char getRxSequence(void);
    unsigned char getRxSequenceMulticast(void);
    void setTransmitOk(bool);
    bool isCachedInFw(void);
    void setCachedInFw(bool);
    bool isSoftAPPeer(void);
    void setSoftAPPeer(bool);

    // CR-184: RSSI / packet-stats / cache / queue helpers.
    void *getStatsID(void);
    bool getStatsIDValid(void);
    void reportRssi(int, apple80211_channel);
    void reportChainRssi(signed char const *, int);
    signed char getAvgRssi24G(void);
    signed char getAvgRssi5G(void);
    signed char getAvgRssiAcrossBands(void);
    signed char getAvgChainRssi5G(void);
    void setPeerAvgRssi24G(signed char);
    void setPeerAvgRssi5G(signed char);
    void simulateDPS(void);
    void freeResources(void);
    void unpauseQueues(void);
    unsigned int reclaimPackets(void);
    void clearCacheState(void);
    unsigned long long getRxBitField(unsigned int);
    unsigned long long getRxBitFieldMulticast(unsigned int);
    void incrementRxCount(unsigned int);
    void *getPacketStats(void);
    void *getPacketStatsRealTimeRx(void);
    void *getPacketStatsRealTimeTx(void);
    void *getCumDataStats(void);
    bool hasRealTimeData(void);
    bool hasLowLatencyData(void);
    bool hasQueuedPackets(void);
    unsigned int getDataLinkCount(void);
    void logPeerTxLatency(unsigned long long);
    void updateQueueState(int);
    unsigned long long getCacheTimeStamp(void);
    void setCacheTimeStamp(unsigned long long);
    void setPacketLifetime(unsigned int);
    bool isBssSteeringPeer(void);
    bool isBssSteeringPeerSyncState(void);

    // CR-192 additions: peer state-flag and counter accessor helpers
    // recovered from IO80211Family BootKernelExtensions.kc on 2026-04-28.
    // None of these names match a virtual already declared in this
    // class body (the local IO80211Peer has no declared base class,
    // so all declarations are non-virtual).
    //   ffffff80021c37cc  IO80211Peer::getHtOperationIEPresent()
    //   ffffff80021c37de  IO80211Peer::setHtOperationIEPresent(bool)
    //   ffffff80021c37f0  IO80211Peer::getVhtOperationIEPresent()
    //   ffffff80021c3802  IO80211Peer::setVhtOperationIEPresent(bool)
    //   ffffff80021c6024  IO80211Peer::getPeerAddRequestedState()
    //   ffffff80021c6044  IO80211Peer::setPeerAddRequestedState(bool)
    //   ffffff80021c6034  IO80211Peer::getPeerDeleteRequestedState()
    //   ffffff80021c6052  IO80211Peer::setPeerDeleteRequestedState(bool)
    //   ffffff80021c607c  IO80211Peer::isPeerAddRequestInProgress()
    //   ffffff80021c6060  IO80211Peer::setPeerAddRequestInProgress(bool)
    //   ffffff80021c608c  IO80211Peer::isPeerDeleteRequesetInProgress()
    //   ffffff80021c606e  IO80211Peer::setPeerDeleteRequestInProgress(bool)
    //   ffffff80021c5eba  IO80211Peer::setBssSteeringPeerSyncState(bool)
    //   ffffff80021c33f2  IO80211Peer::getUnicastBonjourRx()
    //   ffffff80021c3402  IO80211Peer::setUnicastBonjourRx(unsigned int)
    //   ffffff80021c3412  IO80211Peer::getMulticastBonjourRx()
    //   ffffff80021c3422  IO80211Peer::setMulticastBonjourRx(unsigned int)
    //   ffffff80021c61b8  IO80211Peer::getBeaconReceivedCount()
    //   ffffff80021c61ca  IO80211Peer::incrementBeaconReceivedCount()
    //   ffffff80021c6132  IO80211Peer::getTotalDataLinkCount()
    //   ffffff80021c6140  IO80211Peer::incrementTotalDataLinks()
    //   ffffff80021c614e  IO80211Peer::decrementTotalDataLinks()
    //   ffffff80021c610e  IO80211Peer::incrementDataLinks()
    //   ffffff80021c611c  IO80211Peer::decrementDataLinks()
    //   ffffff80021c60ce  IO80211Peer::getRealTimeDataSessionCount()
    //   ffffff80021c59b6  IO80211Peer::incrementRealTimeDataSession()
    //   ffffff80021c59c4  IO80211Peer::decrementRealTimeDataSession()
    //   ffffff80021c60c0  IO80211Peer::getLowLatencyDataSessionCount()
    //   ffffff80021c59dc  IO80211Peer::incrementLowLatencyDataSession()
    //   ffffff80021c5a2a  IO80211Peer::decrementLowLatencyDataSession()
    bool getHtOperationIEPresent(void);
    void setHtOperationIEPresent(bool);
    bool getVhtOperationIEPresent(void);
    void setVhtOperationIEPresent(bool);
    bool getPeerAddRequestedState(void);
    void setPeerAddRequestedState(bool);
    bool getPeerDeleteRequestedState(void);
    void setPeerDeleteRequestedState(bool);
    bool isPeerAddRequestInProgress(void);
    void setPeerAddRequestInProgress(bool);
    bool isPeerDeleteRequesetInProgress(void);
    void setPeerDeleteRequestInProgress(bool);
    void setBssSteeringPeerSyncState(bool);
    unsigned int getUnicastBonjourRx(void);
    void setUnicastBonjourRx(unsigned int);
    unsigned int getMulticastBonjourRx(void);
    void setMulticastBonjourRx(unsigned int);
    unsigned long long getBeaconReceivedCount(void);
    void incrementBeaconReceivedCount(void);
    unsigned int getTotalDataLinkCount(void);
    void incrementTotalDataLinks(void);
    void decrementTotalDataLinks(void);
    void incrementDataLinks(void);
    void decrementDataLinks(void);
    unsigned int getRealTimeDataSessionCount(void);
    void incrementRealTimeDataSession(void);
    void decrementRealTimeDataSession(void);
    unsigned int getLowLatencyDataSessionCount(void);
    void incrementLowLatencyDataSession(void);
    void decrementLowLatencyDataSession(void);

    // CR-193 additions: peer timestamp / link-activity / cache-time
    // accessors recovered from IO80211Family BootKernelExtensions.kc on
    // 2026-04-28. All primitive (`unsigned long long`/`bool`) and not
    // already declared in this class body.
    //   ffffff80021c592e  IO80211Peer::getLastRxUnicastLinkActivity()
    //   ffffff80021c29d4  IO80211Peer::setLastRxUnicastLinkActivity(unsigned long long)
    //   ffffff80021c5fdc  IO80211Peer::getLastRxMulticastLinkActivity()
    //   ffffff80021c5fee  IO80211Peer::setLastRxMulticastLinkActivity(unsigned long long)
    //   ffffff80021c57f2  IO80211Peer::getPeerLastDataActivityTimeMsec()
    //   ffffff80021c5ade  IO80211Peer::getPeerDataInActivityExceededThreshold()
    //   ffffff80021c6000  IO80211Peer::getLastPeerPresencePosted()
    //   ffffff80021c6012  IO80211Peer::setLastPeerPresencePosted(unsigned long long)
    //   ffffff80021c5e4a  IO80211Peer::setPeerDiscoveredTime(unsigned long long)
    //   ffffff80021c5d8e  IO80211Peer::getCachingDeniedTimeStamp()
    //   ffffff80021c5d50  IO80211Peer::setCachingDeniedTimeStamp(unsigned long long)
    //   ffffff80021c5f10  IO80211Peer::getLastCacheAddAttempt()
    //   ffffff80021c378c  IO80211Peer::getLastDataLogTimeStamp()
    //   ffffff80021c379e  IO80211Peer::setLastDataLogTimeStamp(unsigned long long)
    //   ffffff80021c36e6  IO80211Peer::getLastOutputSuccess()
    //   ffffff80021c36f8  IO80211Peer::setLastOutputSuccess(unsigned long long)
    //   ffffff80021c36b2  IO80211Peer::getTimeOfFirstChainRssiSample()
    //   ffffff80021c36c4  IO80211Peer::setTimeOfFirstChainRssiSample(unsigned long long)
    //   ffffff80021c5d1a  IO80211Peer::getWaitingToBeUnCachedTimeStamp()
    //   ffffff80021c5f22  IO80211Peer::getLastQueuePacket()
    //   ffffff80021c5faa  IO80211Peer::setLastQueuePacket(unsigned long long)
    //   ffffff80021c5fbc  IO80211Peer::getNumTransmitStatusLog()
    //   ffffff80021c3692  IO80211Peer::getNumTxStatusMismatch()
    //   ffffff80021c36a2  IO80211Peer::setNumTxStatusMismatch(unsigned int)
    unsigned long long getLastRxUnicastLinkActivity(void);
    void setLastRxUnicastLinkActivity(unsigned long long);
    unsigned long long getLastRxMulticastLinkActivity(void);
    void setLastRxMulticastLinkActivity(unsigned long long);
    unsigned long long getPeerLastDataActivityTimeMsec(void);
    bool getPeerDataInActivityExceededThreshold(void);
    unsigned long long getLastPeerPresencePosted(void);
    void setLastPeerPresencePosted(unsigned long long);
    void setPeerDiscoveredTime(unsigned long long);
    unsigned long long getCachingDeniedTimeStamp(void);
    void setCachingDeniedTimeStamp(unsigned long long);
    unsigned long long getLastCacheAddAttempt(void);
    unsigned long long getLastDataLogTimeStamp(void);
    void setLastDataLogTimeStamp(unsigned long long);
    unsigned long long getLastOutputSuccess(void);
    void setLastOutputSuccess(unsigned long long);
    unsigned long long getTimeOfFirstChainRssiSample(void);
    void setTimeOfFirstChainRssiSample(unsigned long long);
    unsigned long long getWaitingToBeUnCachedTimeStamp(void);
    unsigned long long getLastQueuePacket(void);
    void setLastQueuePacket(unsigned long long);
    unsigned int getNumTransmitStatusLog(void);
    unsigned int getNumTxStatusMismatch(void);
    void setNumTxStatusMismatch(unsigned int);

    // CR-194 additions: peer caching-state and tx counter controls
    // recovered from IO80211Family BootKernelExtensions.kc on
    // 2026-04-28. All primitive (`bool`, `unsigned int`,
    // `unsigned long long`) and not already declared in this class
    // body.
    //   ffffff80021c2b92  IO80211Peer::setStateForCachedPeer()
    //   ffffff80021c2b7a  IO80211Peer::setPreCachingStateForPeer(unsigned long long)
    //   ffffff80021c5b46  IO80211Peer::setPreUnCachingStateForPeer()
    //   ffffff80021c2ccc  IO80211Peer::clearPreUnCachingStateForPeer()
    //   ffffff80021c5d3e  IO80211Peer::getPreUnCachingStateForPeer()
    //   ffffff80021c6164  IO80211Peer::setReservationEnabled()
    //   ffffff80021c6176  IO80211Peer::clearReservationEnabled()
    //   ffffff80021c6188  IO80211Peer::ifCacheReservationEnabled()
    //   ffffff80021c5d74  IO80211Peer::isPeerDeniedCachingForThisSession()
    //   ffffff80021c5d62  IO80211Peer::setPeerDeniedCachingForThisSession()
    //   ffffff80021c60fc  IO80211Peer::getReceivedSidecarRequest()
    //   ffffff80021c60ea  IO80211Peer::setReceivedSidecarRequest(bool)
    //   ffffff80021c5b38  IO80211Peer::setLowLatencyLinkIdle()
    //   ffffff80021c5b32  IO80211Peer::clearLowLatencyLinkIdle()
    //   ffffff80021c5b2a  IO80211Peer::isLowLatencyLinkIdle()
    //   ffffff80021c619a  IO80211Peer::isWaitingToBeCached()
    //   ffffff80021c5dc4  IO80211Peer::setWaitingToBeCached(bool)
    //   ffffff80021c5d2c  IO80211Peer::isWaitingToBeUnCached()
    //   ffffff80021c0de2  IO80211Peer::updateRequestBitField()
    //   ffffff80021c3848  IO80211Peer::updateNumHostPackets(unsigned int, int)
    //   ffffff80021c4ff2  IO80211Peer::updateAllReports()
    //   ffffff80021c3500  IO80211Peer::updateCumDataStats()
    //   ffffff80021c356e  IO80211Peer::updateIntervalDataStats()
    //   ffffff80021c35c2  IO80211Peer::clearIntervalDataStats()
    //   ffffff80021c5036  IO80211Peer::updateTxPacketStats(int, unsigned int)
    //   ffffff80021c5b6e  IO80211Peer::allocTxLatencyStorage()
    //   ffffff80021bfc60  IO80211Peer::freeTxLatencyStorage()
    //   ffffff80021c5094  IO80211Peer::incrementTxOkCount(unsigned int)
    //   ffffff80021c5064  IO80211Peer::incrementTxQueueCount(unsigned int)
    //   ffffff80021c50c2  IO80211Peer::incrementTxFailNoAckCount()
    //   ffffff80021c50f0  IO80211Peer::incrementTxFailOtherCount()
    //   ffffff80021c61b2  IO80211Peer::llwLoadPacketLifetimeHistogram(unsigned char)
    //   ffffff80021c61ac  IO80211Peer::llwComputeTxConsecutiveErrorCount(int)
    void setStateForCachedPeer(void);
    void setPreCachingStateForPeer(unsigned long long);
    void setPreUnCachingStateForPeer(void);
    void clearPreUnCachingStateForPeer(void);
    bool getPreUnCachingStateForPeer(void);
    void setReservationEnabled(void);
    void clearReservationEnabled(void);
    bool ifCacheReservationEnabled(void);
    bool isPeerDeniedCachingForThisSession(void);
    void setPeerDeniedCachingForThisSession(void);
    bool getReceivedSidecarRequest(void);
    void setReceivedSidecarRequest(bool);
    void setLowLatencyLinkIdle(void);
    void clearLowLatencyLinkIdle(void);
    bool isLowLatencyLinkIdle(void);
    bool isWaitingToBeCached(void);
    void setWaitingToBeCached(bool);
    bool isWaitingToBeUnCached(void);
    void updateRequestBitField(void);
    void updateNumHostPackets(unsigned int, int);
    void updateAllReports(void);
    void updateCumDataStats(void);
    void updateIntervalDataStats(void);
    void clearIntervalDataStats(void);
    void updateTxPacketStats(int, unsigned int);
    void allocTxLatencyStorage(void);
    void freeTxLatencyStorage(void);
    void incrementTxOkCount(unsigned int);
    void incrementTxQueueCount(unsigned int);
    void incrementTxFailNoAckCount(void);
    void incrementTxFailOtherCount(void);
    void llwLoadPacketLifetimeHistogram(unsigned char);
    int llwComputeTxConsecutiveErrorCount(int);
};

#endif /* IO80211Peer_h */
