# AppleBCMWLAN — IO80211Peer caching-state and tx-counter surface (CR-194)

- date: 2026-04-28
- source-of-truth: BootKC `IO80211Family.kc` symbol table
- justification class: REFERENCE_ALIGNMENT_FIX (header-only declarations)
- supersedes: CR-193

## Scope

Thirty-three additional public direct-call methods on `IO80211Peer`
were recovered from the BootKC symbol table. Each helper takes only
primitive types (`bool`, `int`, `unsigned char`, `unsigned int`,
`unsigned long long`).

These cover peer caching-state controls (state-for-cached-peer, pre-
caching / pre-uncaching state, reservation enable, sidecar request,
low-latency link idle, waiting-to-be-cached / uncached), update
helpers (request bit-field, num-host-packets, all-reports, cum data
stats, interval data stats, tx-packet stats, tx-latency storage
alloc/free), and tx counter helpers (tx-OK / tx-queue counts, tx-
fail no-ack / other counts, llw packet-lifetime histogram, llw
consecutive-error count).

## Recovered exports

| Address              | Symbol                                                        |
|----------------------|---------------------------------------------------------------|
| `0xffffff80021c2b92` | `IO80211Peer::setStateForCachedPeer()`                        |
| `0xffffff80021c2b7a` | `IO80211Peer::setPreCachingStateForPeer(unsigned long long)`  |
| `0xffffff80021c5b46` | `IO80211Peer::setPreUnCachingStateForPeer()`                  |
| `0xffffff80021c2ccc` | `IO80211Peer::clearPreUnCachingStateForPeer()`                |
| `0xffffff80021c5d3e` | `IO80211Peer::getPreUnCachingStateForPeer()`                  |
| `0xffffff80021c6164` | `IO80211Peer::setReservationEnabled()`                        |
| `0xffffff80021c6176` | `IO80211Peer::clearReservationEnabled()`                      |
| `0xffffff80021c6188` | `IO80211Peer::ifCacheReservationEnabled()`                    |
| `0xffffff80021c5d74` | `IO80211Peer::isPeerDeniedCachingForThisSession()`            |
| `0xffffff80021c5d62` | `IO80211Peer::setPeerDeniedCachingForThisSession()`           |
| `0xffffff80021c60fc` | `IO80211Peer::getReceivedSidecarRequest()`                    |
| `0xffffff80021c60ea` | `IO80211Peer::setReceivedSidecarRequest(bool)`                |
| `0xffffff80021c5b38` | `IO80211Peer::setLowLatencyLinkIdle()`                        |
| `0xffffff80021c5b32` | `IO80211Peer::clearLowLatencyLinkIdle()`                      |
| `0xffffff80021c5b2a` | `IO80211Peer::isLowLatencyLinkIdle()`                         |
| `0xffffff80021c619a` | `IO80211Peer::isWaitingToBeCached()`                          |
| `0xffffff80021c5dc4` | `IO80211Peer::setWaitingToBeCached(bool)`                     |
| `0xffffff80021c5d2c` | `IO80211Peer::isWaitingToBeUnCached()`                        |
| `0xffffff80021c0de2` | `IO80211Peer::updateRequestBitField()`                        |
| `0xffffff80021c3848` | `IO80211Peer::updateNumHostPackets(unsigned int, int)`        |
| `0xffffff80021c4ff2` | `IO80211Peer::updateAllReports()`                             |
| `0xffffff80021c3500` | `IO80211Peer::updateCumDataStats()`                           |
| `0xffffff80021c356e` | `IO80211Peer::updateIntervalDataStats()`                      |
| `0xffffff80021c35c2` | `IO80211Peer::clearIntervalDataStats()`                       |
| `0xffffff80021c5036` | `IO80211Peer::updateTxPacketStats(int, unsigned int)`         |
| `0xffffff80021c5b6e` | `IO80211Peer::allocTxLatencyStorage()`                        |
| `0xffffff80021bfc60` | `IO80211Peer::freeTxLatencyStorage()`                         |
| `0xffffff80021c5094` | `IO80211Peer::incrementTxOkCount(unsigned int)`               |
| `0xffffff80021c5064` | `IO80211Peer::incrementTxQueueCount(unsigned int)`            |
| `0xffffff80021c50c2` | `IO80211Peer::incrementTxFailNoAckCount()`                    |
| `0xffffff80021c50f0` | `IO80211Peer::incrementTxFailOtherCount()`                    |
| `0xffffff80021c61b2` | `IO80211Peer::llwLoadPacketLifetimeHistogram(unsigned char)`  |
| `0xffffff80021c61ac` | `IO80211Peer::llwComputeTxConsecutiveErrorCount(int)`         |

## Non-claims

CR-194 declares these helpers only; it does not call them, does not
infer any vtable or data-layout assumption about `IO80211Peer`, and
does not change runtime behavior. The kext is bit-identical to
CR-193 (sha256 / UUID unchanged), confirming the change is purely
structural reference alignment.
