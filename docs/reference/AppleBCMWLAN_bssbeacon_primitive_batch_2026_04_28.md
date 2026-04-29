# AppleBCMWLAN — IO80211BSSBeacon primitive-only surface (CR-199)

- date: 2026-04-28
- source-of-truth: BootKC `IO80211Family.kc` symbol table
- justification class: REFERENCE_ALIGNMENT_FIX (NEW header file)
- supersedes: CR-198

## Scope

CR-199 introduces a new local header
`include/Airport/IO80211BSSBeacon.h` that carries one hundred and
two primitive-only direct-call IO80211BSSBeacon exports recovered
from the BootKC symbol table.

Helpers covered: vendor / network identification probes (Apple,
iOS, Orbi, Alpine BMW, NBT Evo BMW, CarPlay dongle, WAC), capability
and timing setters (capabilities, beacon period, RSSI/SNR/Noise/
non-transmitted BSSID index/interworking IE / internet-access flags),
rate-set computation and AKM/dump helpers, RSSI/SNR/probe/internet
accessibility const accessors, channel updater and current-BSS
mark/clear/predicate, SSID / OWE-trans-SSID / SSID-cstr / address /
ie-list-length getters, LQM / noise / noise-delta / channel / band /
chan-spec / DTIM-period / beacon-period / ATIM-window / listen-
interval / capabilities const accessors, HT-rx-rates / rx-rate /
rx-rate-percent / rx-rate / CCA mutators, short-SSID matcher, age
and timestamp accessors, encryption / WEP / WPA / HT / VHT / HE / AP
predicates, encryption-mode / AP-mode getters, blacklist / hidden
mutators and predicates, max/min rate getters, capability-tree
predicates (proxy-ARP, TIM-broadcast, DMS, BSS-trans mgmt, BSS-QoS-
mgmt MSCS, BSS-beacon-protection, BSS-SAEPK, BSS-SAEPK-pwd-excl,
BSS-OCV, fast-BSS transition, neighbor report, WiFi-fully-loaded,
score-computed, EHT, QoS-fastlane, NwAssurance-CCXIE, same-SSID
co-located AP, split-SSID co-located AP), R-and-R context, FT/MFP,
PMKSA expiration set/get and interworking-IE/wifi-info accessors,
non-transmitted BSSID index getter, FILS-discovery-frame and
beacon-at-HE-rate predicates, queue-chain accessor, current-BSS
AKMs, multi-BSSID offset, MLD address and predicates.

The class is declared with no base class and no data layout. The
local kext does not allocate, subclass, or take `sizeof` of
`IO80211BSSBeacon`; it is consumed strictly via opaque pointers
returned from IO80211Family.

## Recovered exports

| Address              | Symbol                                                         |
|----------------------|----------------------------------------------------------------|
| `0xffffff80022525b2` | `IO80211BSSBeacon::getRSSIAgeInSeconds() const`                |
| `0xffffff8002252fe0` | `IO80211BSSBeacon::isLikelyOrbiNetwork()`                      |
| `0xffffff8002253350` | `IO80211BSSBeacon::isAppleNetwork()`                           |
| `0xffffff8002253362` | `IO80211BSSBeacon::isIOSDevice()`                              |
| `0xffffff80022533ea` | `IO80211BSSBeacon::isLikelyAlpineBMWNetwork()`                 |
| `0xffffff80022535a6` | `IO80211BSSBeacon::isNBTEvoBMWNetwork()`                       |
| `0xffffff80022535c0` | `IO80211BSSBeacon::isCarPlayDongle()`                          |
| `0xffffff800225363c` | `IO80211BSSBeacon::isWACNetwork()`                             |
| `0xffffff8002253a54` | `IO80211BSSBeacon::setCapabilities(unsigned short)`            |
| `0xffffff8002253a66` | `IO80211BSSBeacon::setBeaconPeriod(unsigned short)`            |
| `0xffffff8002253a94` | `IO80211BSSBeacon::updateRSSI(int)`                            |
| `0xffffff8002253abc` | `IO80211BSSBeacon::updateSNR(short)`                           |
| `0xffffff8002253ad4` | `IO80211BSSBeacon::updateNoise(short)`                         |
| `0xffffff8002253b42` | `IO80211BSSBeacon::setNonTransmittedBssidIndex(unsigned char)` |
| `0xffffff8002253b54` | `IO80211BSSBeacon::setInterworkingIEPresent(bool)`             |
| `0xffffff8002253b66` | `IO80211BSSBeacon::setInternetAccess(bool)`                    |
| `0xffffff800225523e` | `IO80211BSSBeacon::calculateRates()`                           |
| `0xffffff8002255302` | `IO80211BSSBeacon::getAKMs()`                                  |
| `0xffffff8002255312` | `IO80211BSSBeacon::dumpBeacon()`                               |
| `0xffffff8002255582` | `IO80211BSSBeacon::getRSSI() const`                            |
| `0xffffff8002255592` | `IO80211BSSBeacon::getSNR() const`                             |
| `0xffffff80022555a4` | `IO80211BSSBeacon::isDirectedProbeNetwork() const`             |
| `0xffffff80022555b4` | `IO80211BSSBeacon::isInternetAccessible() const`               |
| `0xffffff80022555c8` | `IO80211BSSBeacon::updateChannel(unsigned char)`               |
| `0xffffff8002255998` | `IO80211BSSBeacon::setAsCurrent()`                             |
| `0xffffff80022559c6` | `IO80211BSSBeacon::removeAsCurrent()`                          |
| `0xffffff80022559d8` | `IO80211BSSBeacon::isCurrent() const`                          |
| `0xffffff8002255b1c` | `IO80211BSSBeacon::getSSIDCStr() const`                        |
| `0xffffff8002255b2c` | `IO80211BSSBeacon::getOWETransSSIDLength() const`              |
| `0xffffff8002255b3e` | `IO80211BSSBeacon::getSSIDLength() const`                      |
| `0xffffff8002255b90` | `IO80211BSSBeacon::isOWETrans() const`                         |
| `0xffffff8002255bfc` | `IO80211BSSBeacon::getAddress() const`                         |
| `0xffffff8002255c8e` | `IO80211BSSBeacon::getIeListLength() const`                    |
| `0xffffff8002255cc8` | `IO80211BSSBeacon::hasLQMResult() const`                       |
| `0xffffff8002255cda` | `IO80211BSSBeacon::getNoise() const`                           |
| `0xffffff8002255cec` | `IO80211BSSBeacon::getNoiseDeltaOverTwoCores() const`          |
| `0xffffff8002255cfe` | `IO80211BSSBeacon::updateNoiseDeltaOverTwoCores(signed char)`  |
| `0xffffff8002255d16` | `IO80211BSSBeacon::getChannel() const`                         |
| `0xffffff8002255d28` | `IO80211BSSBeacon::getBand() const`                            |
| `0xffffff8002255d3c` | `IO80211BSSBeacon::getChanSWSpec() const`                      |
| `0xffffff8002255d4e` | `IO80211BSSBeacon::getChanPrimarySWSpec() const`               |
| `0xffffff8002255d84` | `IO80211BSSBeacon::getDTIMPeriod() const`                      |
| `0xffffff8002255d96` | `IO80211BSSBeacon::getBeaconPeriod() const`                    |
| `0xffffff8002255da8` | `IO80211BSSBeacon::getATIMWindow() const`                      |
| `0xffffff8002255dba` | `IO80211BSSBeacon::getListenInterval() const`                  |
| `0xffffff8002255dc6` | `IO80211BSSBeacon::getCapabilities() const`                    |
| `0xffffff8002255e26` | `IO80211BSSBeacon::getHtRxRates()`                             |
| `0xffffff8002255e56` | `IO80211BSSBeacon::getRxRate() const`                          |
| `0xffffff8002255f8a` | `IO80211BSSBeacon::getRxRatePercent()`                         |
| `0xffffff8002255fcc` | `IO80211BSSBeacon::updateRxRate(int)`                          |
| `0xffffff8002255fdc` | `IO80211BSSBeacon::updateCCA(signed char)`                     |
| `0xffffff8002256204` | `IO80211BSSBeacon::shortSSIDMatches(unsigned int) const`       |
| `0xffffff8002256292` | `IO80211BSSBeacon::getAgeInSeconds() const`                    |
| `0xffffff8002256312` | `IO80211BSSBeacon::getAgeInMS() const`                         |
| `0xffffff800225638c` | `IO80211BSSBeacon::getTimestamp() const`                       |
| `0xffffff8002256416` | `IO80211BSSBeacon::isPrivacyEnabled() const`                   |
| `0xffffff800225642c` | `IO80211BSSBeacon::isWEPEnabled() const`                       |
| `0xffffff800225645c` | `IO80211BSSBeacon::isWPAEnabled() const`                       |
| `0xffffff800225648c` | `IO80211BSSBeacon::isHTEnabled() const`                        |
| `0xffffff800225649e` | `IO80211BSSBeacon::isVHTEnabled() const`                       |
| `0xffffff80022564b0` | `IO80211BSSBeacon::isHEEnabled() const`                        |
| `0xffffff80022564c2` | `IO80211BSSBeacon::isAP() const`                               |
| `0xffffff80022564dc` | `IO80211BSSBeacon::getEncryptionMode() const`                  |
| `0xffffff80022564ee` | `IO80211BSSBeacon::getAPMode() const`                          |
| `0xffffff800225650e` | `IO80211BSSBeacon::setBlacklisted(bool)`                       |
| `0xffffff8002256520` | `IO80211BSSBeacon::isBlacklisted() const`                      |
| `0xffffff8002256532` | `IO80211BSSBeacon::setHidden(bool)`                            |
| `0xffffff8002256544` | `IO80211BSSBeacon::isHidden() const`                           |
| `0xffffff800225763e` | `IO80211BSSBeacon::getMaxRate()`                               |
| `0xffffff8002257650` | `IO80211BSSBeacon::getMinRate()`                               |
| `0xffffff8002257662` | `IO80211BSSBeacon::isProxyARPSupported() const`                |
| `0xffffff8002257674` | `IO80211BSSBeacon::isTIMBroadcastSupported() const`            |
| `0xffffff8002257686` | `IO80211BSSBeacon::isDMSSupported() const`                     |
| `0xffffff8002257698` | `IO80211BSSBeacon::isBSSTransMgmtSupported() const`            |
| `0xffffff80022576aa` | `IO80211BSSBeacon::isBSSQoSMgmtMSCSSupported() const`          |
| `0xffffff80022576bc` | `IO80211BSSBeacon::isBSSBeaconProtectionCapable() const`       |
| `0xffffff80022576ce` | `IO80211BSSBeacon::isBSSSAEPKCapable() const`                  |
| `0xffffff80022576ee` | `IO80211BSSBeacon::isBSSSAEPKPwdExclsivelyUsed() const`        |
| `0xffffff8002257700` | `IO80211BSSBeacon::isBSSOCVCapable() const`                    |
| `0xffffff8002257712` | `IO80211BSSBeacon::isFastBSSTransitionSupported() const`       |
| `0xffffff8002257724` | `IO80211BSSBeacon::isNeighborReportSupported() const`          |
| `0xffffff8002257736` | `IO80211BSSBeacon::isWiFiNetworkFullyLoaded() const`           |
| `0xffffff8002257758` | `IO80211BSSBeacon::isScoreComputed() const`                    |
| `0xffffff80022577e0` | `IO80211BSSBeacon::isEhtEnabled() const`                       |
| `0xffffff80022577f2` | `IO80211BSSBeacon::isQosFastLaneEnabled() const`               |
| `0xffffff8002257816` | `IO80211BSSBeacon::isNwAssuranceEnabledInCCXIE() const`        |
| `0xffffff8002257828` | `IO80211BSSBeacon::isSameSSIDCoLocatedAP() const`              |
| `0xffffff8002257882` | `IO80211BSSBeacon::isSplitSSIDCoLocatedAP() const`             |
| `0xffffff80022579d8` | `IO80211BSSBeacon::isFtEnabled()`                              |
| `0xffffff8002257b14` | `IO80211BSSBeacon::isBssMfpCapable()`                          |
| `0xffffff8002257b28` | `IO80211BSSBeacon::getPMKSAExpiration() const`                 |
| `0xffffff8002257b38` | `IO80211BSSBeacon::isInterworkingIEPresent() const`            |
| `0xffffff8002257b4a` | `IO80211BSSBeacon::isWiFiNetworkInfoAvailable() const`         |
| `0xffffff8002257b6e` | `IO80211BSSBeacon::setPMKSAExpiration(unsigned int)`           |
| `0xffffff8002257b7e` | `IO80211BSSBeacon::getNonTransmittedBssidIndex()`              |
| `0xffffff8002257bc8` | `IO80211BSSBeacon::isFILSDiscoveryFrame() const`               |
| `0xffffff8002257bda` | `IO80211BSSBeacon::isBeaconAtHeRate() const`                   |
| `0xffffff8002257de2` | `IO80211BSSBeacon::getCurrentBSSAKMs()`                        |
| `0xffffff8002257e8c` | `IO80211BSSBeacon::getMultiBssidOffset()`                      |
| `0xffffff8002257e9c` | `IO80211BSSBeacon::getMldAddress() const`                      |
| `0xffffff8002257eca` | `IO80211BSSBeacon::isNonTransmittedBssid() const`              |
| `0xffffff8002257edc` | `IO80211BSSBeacon::isMld() const`                              |

## Non-claims

CR-199 declares these helpers only; it does not call them, does not
infer any vtable or data-layout assumption about `IO80211BSSBeacon`,
and does not change runtime behavior. The kext is bit-identical to
CR-189..CR-198 (sha256 / UUID unchanged), confirming the change is
purely structural reference alignment.

## Deferred from CR-198

CR-199 explicitly defers the five exports whose return types are
unrecovered kernel-internal pointers, to avoid `void *` type
erasure inside a 1:1 reference-alignment patch:

| Address              | Symbol                                                         |
|----------------------|----------------------------------------------------------------|
| `0xffffff80022525a4` | `IO80211BSSBeacon::getLogger() const`                          |
| `0xffffff80022559ea` | `IO80211BSSBeacon::getSSID() const`                            |
| `0xffffff80022559fa` | `IO80211BSSBeacon::getOWETransSSID() const`                    |
| `0xffffff800225798e` | `IO80211BSSBeacon::getRnRContext() const`                      |
| `0xffffff8002257bec` | `IO80211BSSBeacon::getQueueChain()`                            |

These will be re-introduced once the recovered return types
(`IO80211Logger *` or its actual decompiled type, `OSData *` /
`OSString *` or the actual SSID descriptor type,
`IO80211BSSBeaconRnRContext *`, and the queue-chain element type)
are documented from decomp evidence.
