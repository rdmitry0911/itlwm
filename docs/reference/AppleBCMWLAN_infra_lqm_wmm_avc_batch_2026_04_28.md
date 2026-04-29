# AppleBCMWLAN — IO80211InfraInterface LQM/WMM/AVC/BT-coex/SIB/ULLA/AWDL/BPF/leaky-AP/supplicant/P2P helper surface (CR-186)

- date: 2026-04-28
- source-of-truth: BootKC `IO80211Family.kc` symbol table
- justification class: REFERENCE_ALIGNMENT_FIX (header-only declarations)
- supersedes: CR-185

## Scope

Twenty-one additional public direct-call methods on
`IO80211InfraInterface` were recovered from the BootKC symbol table.
Each helper takes only primitive arguments (`bool`, `unsigned int`,
`unsigned long`, `unsigned long long`, `void *`) and returns a
primitive type or an opaque `void *`. No kernel-internal struct or
enum types are referenced.

The local kext does not call any of these helpers in CR-186; the
declarations are added inside the existing
`class IO80211InfraInterface` body so that future caller-wiring CRs
can reference the BootKC symbols without per-caller extern shims.

## Recovered exports

| Address              | Symbol                                                                              |
|----------------------|-------------------------------------------------------------------------------------|
| `0xffffff80022e4446` | `IO80211InfraInterface::getLQMData()`                                              |
| `0xffffff80022e451c` | `IO80211InfraInterface::setLQMGated(unsigned long long)`                           |
| `0xffffff80022e44c4` | `IO80211InfraInterface::setLQMStatic(void*, void*)`                                |
| `0xffffff80022e5d1e` | `IO80211InfraInterface::getMonitorMode()`                                          |
| `0xffffff80022e5ca2` | `IO80211InfraInterface::getWMMBWReset()`                                           |
| `0xffffff80022e5cb8` | `IO80211InfraInterface::setWMMBWReset(bool)`                                       |
| `0xffffff80022e14cc` | `IO80211InfraInterface::getAVCAdvisory()`                                          |
| `0xffffff80022e66e0` | `IO80211InfraInterface::getBtCoexState()`                                          |
| `0xffffff80022e190e` | `IO80211InfraInterface::resetInterface(void*, unsigned long)`                      |
| `0xffffff80022e5dce` | `IO80211InfraInterface::getTrafficMonitor()`                                       |
| `0xffffff80022e1386` | `IO80211InfraInterface::finishSIBCoexTimer()`                                      |
| `0xffffff80022e3aca` | `IO80211InfraInterface::resetSIBTurnOnMetrics()`                                   |
| `0xffffff80022e3a8a` | `IO80211InfraInterface::getCoPTxRTSFailCount()`                                    |
| `0xffffff80022e3a9e` | `IO80211InfraInterface::getULLALiteDuration()`                                     |
| `0xffffff80022e39e6` | `IO80211InfraInterface::getAwdlMaxBandWidth()`                                     |
| `0xffffff80022e57fe` | `IO80211InfraInterface::notifyAWDLStateChange(bool)`                               |
| `0xffffff80022e58d6` | `IO80211InfraInterface::bpfTapInternal(unsigned int, unsigned int)`                |
| `0xffffff80022e3784` | `IO80211InfraInterface::setLeakyAPStatsMode(unsigned int)`                         |
| `0xffffff80022e12f2` | `IO80211InfraInterface::UpdateULLADuration(unsigned long long *)`                  |
| `0xffffff80022e1e0c` | `IO80211InfraInterface::handleSupplicantEvent(void*, unsigned long)`               |
| `0xffffff80022e1e3a` | `IO80211InfraInterface::routeToP2PInterface(unsigned int, void*, unsigned long)`   |

## Non-claims

CR-186 declares these helpers only; it does not call them, does not
infer any vtable or data-layout assumption about
`IO80211InfraInterface`, and does not change runtime behavior. The
kext is bit-identical to CR-185 (sha256/UUID unchanged), confirming
the change is purely structural reference alignment.
