# AppleBCMWLAN — IO80211InterfaceMonitor extended primitive helper surface (CR-188)

- date: 2026-04-28
- source-of-truth: BootKC `IO80211Family.kc` symbol table
- justification class: REFERENCE_ALIGNMENT_FIX (header-only declarations)
- supersedes: CR-187

## Scope

Twenty-seven additional public direct-call methods on
`IO80211InterfaceMonitor` were recovered from the BootKC symbol table.
Each helper takes only primitive arguments (`bool`, `int`,
`unsigned int`, `long long`, `unsigned long long`, `unsigned int *`).

The local kext does not call any of these helpers in CR-188; the
declarations are added inside the existing
`class IO80211InterfaceMonitor` body so future caller-wiring CRs can
reference the BootKC symbols without per-caller extern shims.

Verified against the kernel SDK
(`Kernel.framework/Versions/A/Headers/IOKit/...`): no parent-class
virtual matches any of these names, so no implicit override is
introduced and the kext remains bit-identical.

## Recovered exports

| Address              | Symbol                                                                       |
|----------------------|------------------------------------------------------------------------------|
| `0xffffff80022efe74` | `IO80211InterfaceMonitor::getEffectiveLinkRate()`                            |
| `0xffffff80022efe16` | `IO80211InterfaceMonitor::setEffectiveLinkRate(unsigned long long)`          |
| `0xffffff80022eff00` | `IO80211InterfaceMonitor::getEffectiveDataTransferRate()`                    |
| `0xffffff80022efea2` | `IO80211InterfaceMonitor::setEffectiveDataTransferRate(unsigned long long)`  |
| `0xffffff80022ef2aa` | `IO80211InterfaceMonitor::setDataTransferRates(unsigned long long)`          |
| `0xffffff80022eff2e` | `IO80211InterfaceMonitor::setExpectedPeakLatency(unsigned long long)`        |
| `0xffffff80022f0458` | `IO80211InterfaceMonitor::getInterfaceAverageCCA()`                          |
| `0xffffff80022f0424` | `IO80211InterfaceMonitor::hasInterfaceAverageCCA()`                          |
| `0xffffff80022f0122` | `IO80211InterfaceMonitor::setInterfaceAverageCCA(unsigned long long)`        |
| `0xffffff80022f0044` | `IO80211InterfaceMonitor::setInterfaceOpenPercent(unsigned long long)`       |
| `0xffffff80022f5abe` | `IO80211InterfaceMonitor::setInterfaceOFDMDesense(long long)`                |
| `0xffffff80022f7266` | `IO80211InterfaceMonitor::incrementDPSDetected()`                            |
| `0xffffff80022f72a0` | `IO80211InterfaceMonitor::incrementConsecutiveDPS()`                         |
| `0xffffff80022ef414` | `IO80211InterfaceMonitor::isBssidMetricsLoaded()`                            |
| `0xffffff80022ef73e` | `IO80211InterfaceMonitor::isLeakyApSsidBssidValid()`                         |
| `0xffffff80022ef39e` | `IO80211InterfaceMonitor::isLeakyApSsidMatchesSsidMetrics()`                 |
| `0xffffff80022ef370` | `IO80211InterfaceMonitor::resetLeakyApSsidMetrics()`                         |
| `0xffffff80022ef31a` | `IO80211InterfaceMonitor::resetLeakyApBssidMetrics()`                        |
| `0xffffff80022efc82` | `IO80211InterfaceMonitor::updateLeakyApStatus()`                             |
| `0xffffff80022ef4c2` | `IO80211InterfaceMonitor::updateLeakyApNetwork()`                            |
| `0xffffff80022f6fb4` | `IO80211InterfaceMonitor::setPreviousInterfaceActivity()`                    |
| `0xffffff80022f715c` | `IO80211InterfaceMonitor::setLQM(int)`                                       |
| `0xffffff80022efc26` | `IO80211InterfaceMonitor::getLowRxRatePeriodRange(unsigned int)`             |
| `0xffffff80022f73f2` | `IO80211InterfaceMonitor::getEffectiveRxBWSinceLastRead(unsigned int*)`      |
| `0xffffff80022f72da` | `IO80211InterfaceMonitor::getEffectiveTxBWSinceLastRead(unsigned int*)`      |
| `0xffffff80022f65d2` | `IO80211InterfaceMonitor::aggregatedPeersTxLatency(unsigned int, unsigned long long)` |
| `0xffffff80022ef7fe` | `IO80211InterfaceMonitor::loadLeakyApBssidMetricsFromSsidMetrics()`          |

## Non-claims

CR-188 declares these helpers only; it does not call them, does not
infer any vtable or data-layout assumption about
`IO80211InterfaceMonitor`, and does not change runtime behavior. The
kext is bit-identical to CR-187 (sha256/UUID unchanged), confirming
the change is purely structural reference alignment.
