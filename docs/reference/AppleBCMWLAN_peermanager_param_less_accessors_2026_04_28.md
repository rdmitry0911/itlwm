# AppleBCMWLAN — IO80211PeerManager parameterless accessor surface (CR-185)

- date: 2026-04-28
- source-of-truth: BootKC `IO80211Family.kc` symbol table
- justification class: REFERENCE_ALIGNMENT_FIX (header-only declarations)
- supersedes: CR-184

## Scope

Thirty-three additional public direct-call methods on `IO80211PeerManager`
were recovered from the BootKC symbol table of `IO80211Family.kc` shipped
in the macOS Tahoe SDK image. Each accessor takes either no arguments or a
single primitive parameter and returns either a primitive type or an
already-known opaque pointer (`void *`, `char const *`).

The local kext does not subclass, allocate, or take `sizeof` of
`IO80211PeerManager`; the existing opaque declaration in
`include/Airport/IO80211PeerManager.h` is sufficient to declare the new
non-virtual methods, and no caller wiring is added in CR-185.

## Recovered exports (kext-side declarations are anchored to these)

| Address              | Symbol                                                         |
|----------------------|----------------------------------------------------------------|
| `0xffffff80021c90aa` | `IO80211PeerManager::getBSDName()`                             |
| `0xffffff80021df576` | `IO80211PeerManager::GetProvider()`                            |
| `0xffffff80021c6bd2` | `IO80211PeerManager::getController()`                          |
| `0xffffff80021c8648` | `IO80211PeerManager::getInterfaceId()`                         |
| `0xffffff80021df390` | `IO80211PeerManager::getCommandGate()`                         |
| `0xffffff80021cea00` | `IO80211PeerManager::interfaceMonitor()`                       |
| `0xffffff80021df8bc` | `IO80211PeerManager::getCountryCode()`                         |
| `0xffffff80021df5ee` | `IO80211PeerManager::getDTIMPeriod()`                          |
| `0xffffff80021df5de` | `IO80211PeerManager::getBeaconPeriod()`                        |
| `0xffffff80021ccbba` | `IO80211PeerManager::getEnabling()`                            |
| `0xffffff80021c9d2c` | `IO80211PeerManager::failToEnable()`                           |
| `0xffffff80021df650` | `IO80211PeerManager::getHeCapable()`                           |
| `0xffffff80021df63e` | `IO80211PeerManager::getVhtCapable()`                          |
| `0xffffff80021df89c` | `IO80211PeerManager::getMyHeCap()`                             |
| `0xffffff80021df88c` | `IO80211PeerManager::getMyVhtCap()`                            |
| `0xffffff80021df8ac` | `IO80211PeerManager::getRsdbCap()`                             |
| `0xffffff80021df87c` | `IO80211PeerManager::getHtCapabilities()`                      |
| `0xffffff80021df0ee` | `IO80211PeerManager::isRsdbSupported()`                        |
| `0xffffff80021dfb38` | `IO80211PeerManager::onDispatchQueue()`                        |
| `0xffffff80021d4c0c` | `IO80211PeerManager::isPeerCacheFull()`                        |
| `0xffffff80021d4f0c` | `IO80211PeerManager::printHashTable()`                         |
| `0xffffff80021d46a0` | `IO80211PeerManager::removeAllPeers()`                         |
| `0xffffff80021c93a4` | `IO80211PeerManager::freeResources()`                          |
| `0xffffff80021ccf5c` | `IO80211PeerManager::awdlChipReset()`                          |
| `0xffffff80021cc734` | `IO80211PeerManager::flushFreeMbufs()`                         |
| `0xffffff80021dba82` | `IO80211PeerManager::enablemDNSTx()`                           |
| `0xffffff80021c9a56` | `IO80211PeerManager::destroyReporters()`                       |
| `0xffffff80021d9338` | `IO80211PeerManager::updateAllReports()`                       |
| `0xffffff80021df8cc` | `IO80211PeerManager::getScanningState()`                       |
| `0xffffff80021dfc24` | `IO80211PeerManager::getOutputBEBytes()`                       |
| `0xffffff80021dfc36` | `IO80211PeerManager::getOutputBKBytes()`                       |
| `0xffffff80021dfc48` | `IO80211PeerManager::getOutputVIBytes()`                       |
| `0xffffff80021dfc5a` | `IO80211PeerManager::getOutputVOBytes()`                       |

## Non-claims

CR-185 declares these accessors only; it does not call them, does not
infer any vtable or data-layout assumption about `IO80211PeerManager`,
and does not change runtime behavior. The kext is bit-identical to
CR-184 (sha256/UUID unchanged), confirming the change is purely
structural reference alignment.
