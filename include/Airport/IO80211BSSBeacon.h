//
//  IO80211BSSBeacon.h
//  itlwm
//
//  Reference-aligned forward declaration of the public direct-call
//  IO80211BSSBeacon surface exported by IO80211Family on macOS Tahoe.
//
//  All declared methods are non-virtual direct-call BootKC exports, so
//  the local class declaration deliberately omits any vtable or data
//  layout. Tahoe constructs genuine framework objects through the exported
//  class allocator and constructor, then keeps only opaque
//  `IO80211BSSBeacon *` pointers; the local kext does not subclass this class
//  or take `sizeof` of its incomplete declaration.
//
//  CR-199 — primitive-only batch (BootKC IO80211Family.kc, recovered
//  2026-04-28). Each declaration is anchored to its BootKC address in
//  the comment block below. Helpers whose signatures reference kernel-
//  internal struct/enum types (apple80211_*, IE *, OSData *, OSArray *,
//  OSDictionary *, IO80211ScanResult *, beacon_ie_*, ApRankingScoreCtx,
//  rate-set descriptors, akm/rsn cipher enums, multi-BSSID descriptors,
//  IO80211BSSBeaconRnRContext *, IO80211BSSBeaconQueueChain *,
//  IO80211Logger *, OSData/OSString-backed SSID descriptor, etc.) are
//  deferred until those types are recovered. In particular, the
//  following five exports return unrecovered kernel-internal pointers
//  and are deferred from this batch:
//    getLogger() const, getSSID() const, getOWETransSSID() const,
//    getRnRContext() const, getQueueChain().
//
//  BootKC anchors:
//    ffffff80022525b2  IO80211BSSBeacon::getRSSIAgeInSeconds() const
//    ffffff8002252fe0  IO80211BSSBeacon::isLikelyOrbiNetwork()
//    ffffff8002253350  IO80211BSSBeacon::isAppleNetwork()
//    ffffff8002253362  IO80211BSSBeacon::isIOSDevice()
//    ffffff80022533ea  IO80211BSSBeacon::isLikelyAlpineBMWNetwork()
//    ffffff80022535a6  IO80211BSSBeacon::isNBTEvoBMWNetwork()
//    ffffff80022535c0  IO80211BSSBeacon::isCarPlayDongle()
//    ffffff800225363c  IO80211BSSBeacon::isWACNetwork()
//    ffffff8002253a54  IO80211BSSBeacon::setCapabilities(unsigned short)
//    ffffff8002253a66  IO80211BSSBeacon::setBeaconPeriod(unsigned short)
//    ffffff8002253a94  IO80211BSSBeacon::updateRSSI(int)
//    ffffff8002253abc  IO80211BSSBeacon::updateSNR(short)
//    ffffff8002253ad4  IO80211BSSBeacon::updateNoise(short)
//    ffffff8002253b42  IO80211BSSBeacon::setNonTransmittedBssidIndex(unsigned char)
//    ffffff8002253b54  IO80211BSSBeacon::setInterworkingIEPresent(bool)
//    ffffff8002253b66  IO80211BSSBeacon::setInternetAccess(bool)
//    ffffff800225523e  IO80211BSSBeacon::calculateRates()
//    ffffff8002255302  IO80211BSSBeacon::getAKMs()
//    ffffff8002255312  IO80211BSSBeacon::dumpBeacon()
//    ffffff8002255582  IO80211BSSBeacon::getRSSI() const
//    ffffff8002255592  IO80211BSSBeacon::getSNR() const
//    ffffff80022555a4  IO80211BSSBeacon::isDirectedProbeNetwork() const
//    ffffff80022555b4  IO80211BSSBeacon::isInternetAccessible() const
//    ffffff80022555c8  IO80211BSSBeacon::updateChannel(unsigned char)
//    ffffff8002255998  IO80211BSSBeacon::setAsCurrent()
//    ffffff80022559c6  IO80211BSSBeacon::removeAsCurrent()
//    ffffff80022559d8  IO80211BSSBeacon::isCurrent() const
//    ffffff8002255b1c  IO80211BSSBeacon::getSSIDCStr() const
//    ffffff8002255b2c  IO80211BSSBeacon::getOWETransSSIDLength() const
//    ffffff8002255b3e  IO80211BSSBeacon::getSSIDLength() const
//    ffffff8002255b90  IO80211BSSBeacon::isOWETrans() const
//    ffffff8002255bfc  IO80211BSSBeacon::getAddress() const
//    ffffff8002255c8e  IO80211BSSBeacon::getIeListLength() const
//    ffffff8002255cc8  IO80211BSSBeacon::hasLQMResult() const
//    ffffff8002255cda  IO80211BSSBeacon::getNoise() const
//    ffffff8002255cec  IO80211BSSBeacon::getNoiseDeltaOverTwoCores() const
//    ffffff8002255cfe  IO80211BSSBeacon::updateNoiseDeltaOverTwoCores(signed char)
//    ffffff8002255d16  IO80211BSSBeacon::getChannel() const
//    ffffff8002255d28  IO80211BSSBeacon::getBand() const
//    ffffff8002255d3c  IO80211BSSBeacon::getChanSWSpec() const
//    ffffff8002255d4e  IO80211BSSBeacon::getChanPrimarySWSpec() const
//    ffffff8002255d84  IO80211BSSBeacon::getDTIMPeriod() const
//    ffffff8002255d96  IO80211BSSBeacon::getBeaconPeriod() const
//    ffffff8002255da8  IO80211BSSBeacon::getATIMWindow() const
//    ffffff8002255dba  IO80211BSSBeacon::getListenInterval() const
//    ffffff8002255dc6  IO80211BSSBeacon::getCapabilities() const
//    ffffff8002255e26  IO80211BSSBeacon::getHtRxRates()
//    ffffff8002255e56  IO80211BSSBeacon::getRxRate() const
//    ffffff8002255f8a  IO80211BSSBeacon::getRxRatePercent()
//    ffffff8002255fcc  IO80211BSSBeacon::updateRxRate(int)
//    ffffff8002255fdc  IO80211BSSBeacon::updateCCA(signed char)
//    ffffff8002256204  IO80211BSSBeacon::shortSSIDMatches(unsigned int) const
//    ffffff8002256292  IO80211BSSBeacon::getAgeInSeconds() const
//    ffffff8002256312  IO80211BSSBeacon::getAgeInMS() const
//    ffffff800225638c  IO80211BSSBeacon::getTimestamp() const
//    ffffff8002256416  IO80211BSSBeacon::isPrivacyEnabled() const
//    ffffff800225642c  IO80211BSSBeacon::isWEPEnabled() const
//    ffffff800225645c  IO80211BSSBeacon::isWPAEnabled() const
//    ffffff800225648c  IO80211BSSBeacon::isHTEnabled() const
//    ffffff800225649e  IO80211BSSBeacon::isVHTEnabled() const
//    ffffff80022564b0  IO80211BSSBeacon::isHEEnabled() const
//    ffffff80022564c2  IO80211BSSBeacon::isAP() const
//    ffffff80022564dc  IO80211BSSBeacon::getEncryptionMode() const
//    ffffff80022564ee  IO80211BSSBeacon::getAPMode() const
//    ffffff800225650e  IO80211BSSBeacon::setBlacklisted(bool)
//    ffffff8002256520  IO80211BSSBeacon::isBlacklisted() const
//    ffffff8002256532  IO80211BSSBeacon::setHidden(bool)
//    ffffff8002256544  IO80211BSSBeacon::isHidden() const
//    ffffff800225763e  IO80211BSSBeacon::getMaxRate()
//    ffffff8002257650  IO80211BSSBeacon::getMinRate()
//    ffffff8002257662  IO80211BSSBeacon::isProxyARPSupported() const
//    ffffff8002257674  IO80211BSSBeacon::isTIMBroadcastSupported() const
//    ffffff8002257686  IO80211BSSBeacon::isDMSSupported() const
//    ffffff8002257698  IO80211BSSBeacon::isBSSTransMgmtSupported() const
//    ffffff80022576aa  IO80211BSSBeacon::isBSSQoSMgmtMSCSSupported() const
//    ffffff80022576bc  IO80211BSSBeacon::isBSSBeaconProtectionCapable() const
//    ffffff80022576ce  IO80211BSSBeacon::isBSSSAEPKCapable() const
//    ffffff80022576ee  IO80211BSSBeacon::isBSSSAEPKPwdExclsivelyUsed() const
//    ffffff8002257700  IO80211BSSBeacon::isBSSOCVCapable() const
//    ffffff8002257712  IO80211BSSBeacon::isFastBSSTransitionSupported() const
//    ffffff8002257724  IO80211BSSBeacon::isNeighborReportSupported() const
//    ffffff8002257736  IO80211BSSBeacon::isWiFiNetworkFullyLoaded() const
//    ffffff8002257758  IO80211BSSBeacon::isScoreComputed() const
//    ffffff80022577e0  IO80211BSSBeacon::isEhtEnabled() const
//    ffffff80022577f2  IO80211BSSBeacon::isQosFastLaneEnabled() const
//    ffffff8002257816  IO80211BSSBeacon::isNwAssuranceEnabledInCCXIE() const
//    ffffff8002257828  IO80211BSSBeacon::isSameSSIDCoLocatedAP() const
//    ffffff8002257882  IO80211BSSBeacon::isSplitSSIDCoLocatedAP() const
//    ffffff80022579d8  IO80211BSSBeacon::isFtEnabled()
//    ffffff8002257b14  IO80211BSSBeacon::isBssMfpCapable()
//    ffffff8002257b28  IO80211BSSBeacon::getPMKSAExpiration() const
//    ffffff8002257b38  IO80211BSSBeacon::isInterworkingIEPresent() const
//    ffffff8002257b4a  IO80211BSSBeacon::isWiFiNetworkInfoAvailable() const
//    ffffff8002257b6e  IO80211BSSBeacon::setPMKSAExpiration(unsigned int)
//    ffffff8002257b7e  IO80211BSSBeacon::getNonTransmittedBssidIndex()
//    ffffff8002257bc8  IO80211BSSBeacon::isFILSDiscoveryFrame() const
//    ffffff8002257bda  IO80211BSSBeacon::isBeaconAtHeRate() const
//    ffffff8002257de2  IO80211BSSBeacon::getCurrentBSSAKMs()
//    ffffff8002257e8c  IO80211BSSBeacon::getMultiBssidOffset()
//    ffffff8002257e9c  IO80211BSSBeacon::getMldAddress() const
//    ffffff8002257eca  IO80211BSSBeacon::isNonTransmittedBssid() const
//    ffffff8002257edc  IO80211BSSBeacon::isMld() const
//

#ifndef IO80211BSSBeacon_h
#define IO80211BSSBeacon_h

class IO80211BSSBeacon {
public:
    unsigned int getRSSIAgeInSeconds() const;

    bool isLikelyOrbiNetwork(void);
    bool isAppleNetwork(void);
    bool isIOSDevice(void);
    bool isLikelyAlpineBMWNetwork(void);
    bool isNBTEvoBMWNetwork(void);
    bool isCarPlayDongle(void);
    bool isWACNetwork(void);

    void setCapabilities(unsigned short);
    void setBeaconPeriod(unsigned short);
    void updateRSSI(int);
    void updateSNR(short);
    void updateNoise(short);
    void setNonTransmittedBssidIndex(unsigned char);
    void setInterworkingIEPresent(bool);
    void setInternetAccess(bool);

    void calculateRates(void);
    unsigned int getAKMs(void);
    void dumpBeacon(void);

    int getRSSI() const;
    short getSNR() const;
    bool isDirectedProbeNetwork() const;
    bool isInternetAccessible() const;

    void updateChannel(unsigned char);
    void setAsCurrent(void);
    void removeAsCurrent(void);
    bool isCurrent() const;

    const char *getSSIDCStr() const;
    unsigned int getOWETransSSIDLength() const;
    unsigned int getSSIDLength() const;
    bool isOWETrans() const;
    unsigned char *getAddress() const;
    unsigned int getIeListLength() const;

    bool hasLQMResult() const;
    short getNoise() const;
    signed char getNoiseDeltaOverTwoCores() const;
    void updateNoiseDeltaOverTwoCores(signed char);

    unsigned int getChannel() const;
    unsigned int getBand() const;
    unsigned int getChanSWSpec() const;
    unsigned int getChanPrimarySWSpec() const;
    unsigned int getDTIMPeriod() const;
    unsigned short getBeaconPeriod() const;
    unsigned short getATIMWindow() const;
    unsigned short getListenInterval() const;
    unsigned short getCapabilities() const;

    unsigned int getHtRxRates(void);
    unsigned int getRxRate() const;
    unsigned int getRxRatePercent(void);
    void updateRxRate(int);
    void updateCCA(signed char);

    bool shortSSIDMatches(unsigned int) const;
    unsigned int getAgeInSeconds() const;
    unsigned int getAgeInMS() const;
    unsigned long long getTimestamp() const;

    bool isPrivacyEnabled() const;
    bool isWEPEnabled() const;
    bool isWPAEnabled() const;
    bool isHTEnabled() const;
    bool isVHTEnabled() const;
    bool isHEEnabled() const;
    bool isAP() const;
    unsigned int getEncryptionMode() const;
    unsigned int getAPMode() const;

    void setBlacklisted(bool);
    bool isBlacklisted() const;
    void setHidden(bool);
    bool isHidden() const;

    unsigned int getMaxRate(void);
    unsigned int getMinRate(void);

    bool isProxyARPSupported() const;
    bool isTIMBroadcastSupported() const;
    bool isDMSSupported() const;
    bool isBSSTransMgmtSupported() const;
    bool isBSSQoSMgmtMSCSSupported() const;
    bool isBSSBeaconProtectionCapable() const;
    bool isBSSSAEPKCapable() const;
    bool isBSSSAEPKPwdExclsivelyUsed() const;
    bool isBSSOCVCapable() const;
    bool isFastBSSTransitionSupported() const;
    bool isNeighborReportSupported() const;
    bool isWiFiNetworkFullyLoaded() const;
    bool isScoreComputed() const;
    bool isEhtEnabled() const;
    bool isQosFastLaneEnabled() const;
    bool isNwAssuranceEnabledInCCXIE() const;
    bool isSameSSIDCoLocatedAP() const;
    bool isSplitSSIDCoLocatedAP() const;

    bool isFtEnabled(void);
    bool isBssMfpCapable(void);
    unsigned int getPMKSAExpiration() const;
    bool isInterworkingIEPresent() const;
    bool isWiFiNetworkInfoAvailable() const;
    void setPMKSAExpiration(unsigned int);
    unsigned char getNonTransmittedBssidIndex(void);

    bool isFILSDiscoveryFrame() const;
    bool isBeaconAtHeRate() const;

    unsigned int getCurrentBSSAKMs(void);
    unsigned int getMultiBssidOffset(void);
    unsigned char *getMldAddress() const;
    bool isNonTransmittedBssid() const;
    bool isMld() const;
};

#endif /* IO80211BSSBeacon_h */
