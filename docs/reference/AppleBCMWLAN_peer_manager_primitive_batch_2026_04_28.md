# AppleBCMWLAN — IO80211PeerManager primitive-only helper surface (CR-191)

- date: 2026-04-28
- source-of-truth: BootKC `IO80211Family.kc` symbol table
- justification class: REFERENCE_ALIGNMENT_FIX (header-only declarations)
- supersedes: CR-190

## Scope

Twenty additional public direct-call (non-vtable) methods on
`IO80211PeerManager` were recovered from the BootKC symbol table.
Each helper takes only primitive types (`bool`, `unsigned int`,
`unsigned long long`, `unsigned char`, `unsigned char *`) or the
already forward-declared `IO80211Peer *`.

The local kext does not call any of these helpers in CR-191; the
declarations are added inside the existing `class IO80211PeerManager`
body so future caller-wiring CRs can reference the BootKC symbols
without per-caller extern shims.

## Recovered exports

| Address              | Symbol                                                                              |
|----------------------|-------------------------------------------------------------------------------------|
| `0xffffff80021ce526` | `IO80211PeerManager::modifyChID(unsigned long long)`                                |
| `0xffffff80021d9728` | `IO80211PeerManager::printPeers(unsigned int, unsigned int)`                        |
| `0xffffff80021df230` | `IO80211PeerManager::getBlockMdns()`                                                |
| `0xffffff80021df242` | `IO80211PeerManager::setBlockMdns(bool)`                                            |
| `0xffffff80021df20c` | `IO80211PeerManager::getBlockMdnsTx()`                                              |
| `0xffffff80021df21e` | `IO80211PeerManager::setBlockMdnsTx(bool)`                                          |
| `0xffffff80021df2ba` | `IO80211PeerManager::setP2PLogging(bool)`                                           |
| `0xffffff80021d42a6` | `IO80211PeerManager::setPeersCount(unsigned long long)`                             |
| `0xffffff80021df5fe` | `IO80211PeerManager::setBeaconPeriod(unsigned int)`                                 |
| `0xffffff80021df60e` | `IO80211PeerManager::setDTIMPeriod(unsigned int)`                                   |
| `0xffffff80021df0e8` | `IO80211PeerManager::setDisplayState(bool)`                                         |
| `0xffffff80021d4e18` | `IO80211PeerManager::setScanOn2GOnly(bool)`                                         |
| `0xffffff80021d4c22` | `IO80211PeerManager::is24GOnlyScan()`                                               |
| `0xffffff80021df7e0` | `IO80211PeerManager::getTxQueueStamp()`                                             |
| `0xffffff80021df7f2` | `IO80211PeerManager::setTxQueueStamp(unsigned long long)`                           |
| `0xffffff80021e0078` | `IO80211PeerManager::updateCtlCount(unsigned long long)`                            |
| `0xffffff80021e008a` | `IO80211PeerManager::updateRxPackets(IO80211Peer*, unsigned long long, unsigned long long)` |
| `0xffffff80021c6482` | `IO80211PeerManager::macAddressEqual(IO80211Peer*, unsigned char*)`                 |
| `0xffffff80021db786` | `IO80211PeerManager::saveCountryCode(unsigned char*)`                               |
| `0xffffff80021deb10` | `IO80211PeerManager::reportP2PCCA(unsigned char, unsigned int, unsigned int, unsigned int, unsigned int)` |

## Non-claims

CR-191 declares these helpers only; it does not call them, does not
infer any vtable or data-layout assumption about
`IO80211PeerManager`, and does not change runtime behavior. The kext
is bit-identical to CR-190 (sha256 / UUID unchanged), confirming the
change is purely structural reference alignment.
