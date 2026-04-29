# AppleBCMWLAN — IO80211InfraInterface additional primitive helper surface (CR-189)

- date: 2026-04-28
- source-of-truth: BootKC `IO80211Family.kc` symbol table
- justification class: REFERENCE_ALIGNMENT_FIX (header-only declarations)
- supersedes: CR-188

## Scope

Eighteen additional public direct-call methods on
`IO80211InfraInterface` were recovered from the BootKC symbol table.
Each helper takes only primitives (`bool`, `unsigned int`,
`unsigned long long`, `void *`) or an opaque `IO80211TimerSource *`
forward-declared in the same header.

The local kext does not call any of these helpers in CR-189; the
declarations are added inside the existing
`class IO80211InfraInterface` body so future caller-wiring CRs can
reference the BootKC symbols without per-caller extern shims.

## Recovered exports

| Address              | Symbol                                                                       |
|----------------------|------------------------------------------------------------------------------|
| `0xffffff80022e3a38` | `IO80211InfraInterface::get5GLowHighBandSwitchCounter()`                     |
| `0xffffff80022e3a4c` | `IO80211InfraInterface::get5GLowHighBandSwitchSuccessPerc()`                 |
| `0xffffff80022e3be6` | `IO80211InfraInterface::getCoPSIBCoexTurnOnCount()`                          |
| `0xffffff80022e3bfa` | `IO80211InfraInterface::getCoPSIBCoexTurnOnDuration()`                       |
| `0xffffff80022e3a76` | `IO80211InfraInterface::getULLAClassicDuration()`                            |
| `0xffffff80022e3ab2` | `IO80211InfraInterface::resetCoPTxRTSFailCount()`                            |
| `0xffffff80022e4b6a` | `IO80211InfraInterface::resetTxPathHealthCheck()`                            |
| `0xffffff80022e3ccc` | `IO80211InfraInterface::setInfraPeersLoggingEnabled(bool)`                   |
| `0xffffff80022e42fa` | `IO80211InfraInterface::reportDataTransferRates()`                           |
| `0xffffff80022e440c` | `IO80211InfraInterface::reportDataTransferRatesStatic(void*)`                |
| `0xffffff80022ddfd0` | `IO80211InfraInterface::reportDataTransferRatesTimer(IO80211TimerSource*)`   |
| `0xffffff80022dded2` | `IO80211InfraInterface::triggerLinkStatusUpdate(IO80211TimerSource*)`        |
| `0xffffff80022e29ea` | `IO80211InfraInterface::handleLeakyApStatsResetTimer(IO80211TimerSource*)`   |
| `0xffffff80022ddf66` | `IO80211InfraInterface::restoreMulticastStateTimer(IO80211TimerSource*)`     |
| `0xffffff80022e4084` | `IO80211InfraInterface::updateLinkParametersStatic(void*, void*)`            |
| `0xffffff80022e45e2` | `IO80211InfraInterface::updateLinkStatusStatic(void*)`                       |
| `0xffffff80022e62cc` | `IO80211InfraInterface::updateTxRxLatency()`                                 |
| `0xffffff80022de4b2` | `IO80211InfraInterface::publishOffloadCapability()`                          |

## Non-claims

CR-189 declares these helpers only; it does not call them, does not
infer any vtable or data-layout assumption about
`IO80211InfraInterface`, and does not change runtime behavior. The
kext is bit-identical to CR-188 (sha256/UUID unchanged), confirming
the change is purely structural reference alignment.
