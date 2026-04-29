# AppleBCMWLAN — IO80211SkywalkInterface companion-id / pid-lock / dispatch helper surface (CR-190)

- date: 2026-04-28
- source-of-truth: BootKC `IO80211Family.kc` symbol table
- justification class: REFERENCE_ALIGNMENT_FIX (header-only declarations)
- supersedes: CR-189

## Scope

Eight additional public direct-call (non-vtable) methods on
`IO80211SkywalkInterface` were recovered from the BootKC symbol
table. Each helper takes only primitive types (`bool`,
`unsigned int`, `unsigned long`, `void *`) or an
already-known opaque type (`ether_addr &`).

The local kext does not call any of these helpers in CR-190; the
declarations are added inside the existing
`class IO80211SkywalkInterface` body so future caller-wiring CRs can
reference the BootKC symbols without per-caller extern shims.

## Recovered exports

| Address              | Symbol                                                                          |
|----------------------|---------------------------------------------------------------------------------|
| `0xffffff80022781cc` | `IO80211SkywalkInterface::getCompanionInterfaceId()`                            |
| `0xffffff80022781dc` | `IO80211SkywalkInterface::setCompanionInterfaceId(unsigned int)`                |
| `0xffffff800227739a` | `IO80211SkywalkInterface::pidLocked()`                                          |
| `0xffffff8002278478` | `IO80211SkywalkInterface::setLowLatencyEnabled(bool)`                           |
| `0xffffff80022771ea` | `IO80211SkywalkInterface::updateTimeSyncMacAddress(ether_addr&)`                |
| `0xffffff8002275220` | `IO80211SkywalkInterface::validateDispatchQueue()`                              |
| `0xffffff8002276fba` | `IO80211SkywalkInterface::getControllerWorkQueue()`                             |
| `0xffffff80022765a2` | `IO80211SkywalkInterface::storeProcessNameAndIoctlInformation(unsigned long)`   |

## Excluded names (already declared as virtuals in the same class body)

The following BootKC `IO80211SkywalkInterface` non-virtual exports
recovered on 2026-04-28 are **already declared as virtual methods**
earlier in the local `class IO80211SkywalkInterface` body and resolve
through the inherited vtable. Redeclaring them here as non-virtual
would conflict with their existing virtual signatures and break the
build, so they are intentionally NOT added in CR-190:

`attachPeer`, `detachPeer`, `cachePeer`, `findPeer`, `getSelfMacAddr`,
`getFeatureFlags`, `getDataQueueDepth`, `handleChosenMedia`,
`flushPacketQueues`, `isChipInterfaceReady`, `isCommandProhibited`,
`isInterfaceEnabled`, `setRunningState`, `getSupportedMediaArray`,
`setPromiscuousModeEnable`, `shouldLog`, `getLastQueuePacketTime`,
`getLastRxUnicastLinkActivityTime`, `logTxLatency`, `logRxLatency`,
`getLastTxTimeStamp`, `getLastRxTimeStamp`, `setDebugTrafficReport`.

## Non-claims

CR-190 declares these helpers only; it does not call them, does not
infer any vtable or data-layout assumption about
`IO80211SkywalkInterface`, and does not change runtime behavior. The
kext is bit-identical to CR-189 (sha256/UUID unchanged), confirming
the change is purely structural reference alignment.
