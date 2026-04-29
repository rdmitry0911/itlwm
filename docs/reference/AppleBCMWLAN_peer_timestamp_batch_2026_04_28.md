# AppleBCMWLAN — IO80211Peer timestamp / link-activity / cache-time surface (CR-193)

- date: 2026-04-28
- source-of-truth: BootKC `IO80211Family.kc` symbol table
- justification class: REFERENCE_ALIGNMENT_FIX (header-only declarations)
- supersedes: CR-192

## Scope

Twenty-four additional public direct-call methods on `IO80211Peer`
were recovered from the BootKC symbol table. Each helper takes only
primitive types (`bool`, `unsigned int`, `unsigned long long`).

These cover Rx unicast/multicast link-activity timestamps, peer
last-data-activity time and inactivity-threshold flag, peer-presence
posted timestamp, peer-discovered time, caching-denied timestamp,
last cache-add attempt, last data-log timestamp, last output-success
timestamp, time-of-first chain-RSSI sample, waiting-to-be-uncached
timestamp, last-queue-packet timestamp, transmit-status-log count,
and TX status mismatch count.

## Recovered exports

| Address              | Symbol                                                            |
|----------------------|-------------------------------------------------------------------|
| `0xffffff80021c592e` | `IO80211Peer::getLastRxUnicastLinkActivity()`                     |
| `0xffffff80021c29d4` | `IO80211Peer::setLastRxUnicastLinkActivity(unsigned long long)`   |
| `0xffffff80021c5fdc` | `IO80211Peer::getLastRxMulticastLinkActivity()`                   |
| `0xffffff80021c5fee` | `IO80211Peer::setLastRxMulticastLinkActivity(unsigned long long)` |
| `0xffffff80021c57f2` | `IO80211Peer::getPeerLastDataActivityTimeMsec()`                  |
| `0xffffff80021c5ade` | `IO80211Peer::getPeerDataInActivityExceededThreshold()`           |
| `0xffffff80021c6000` | `IO80211Peer::getLastPeerPresencePosted()`                        |
| `0xffffff80021c6012` | `IO80211Peer::setLastPeerPresencePosted(unsigned long long)`      |
| `0xffffff80021c5e4a` | `IO80211Peer::setPeerDiscoveredTime(unsigned long long)`          |
| `0xffffff80021c5d8e` | `IO80211Peer::getCachingDeniedTimeStamp()`                        |
| `0xffffff80021c5d50` | `IO80211Peer::setCachingDeniedTimeStamp(unsigned long long)`      |
| `0xffffff80021c5f10` | `IO80211Peer::getLastCacheAddAttempt()`                           |
| `0xffffff80021c378c` | `IO80211Peer::getLastDataLogTimeStamp()`                          |
| `0xffffff80021c379e` | `IO80211Peer::setLastDataLogTimeStamp(unsigned long long)`        |
| `0xffffff80021c36e6` | `IO80211Peer::getLastOutputSuccess()`                             |
| `0xffffff80021c36f8` | `IO80211Peer::setLastOutputSuccess(unsigned long long)`           |
| `0xffffff80021c36b2` | `IO80211Peer::getTimeOfFirstChainRssiSample()`                    |
| `0xffffff80021c36c4` | `IO80211Peer::setTimeOfFirstChainRssiSample(unsigned long long)`  |
| `0xffffff80021c5d1a` | `IO80211Peer::getWaitingToBeUnCachedTimeStamp()`                  |
| `0xffffff80021c5f22` | `IO80211Peer::getLastQueuePacket()`                               |
| `0xffffff80021c5faa` | `IO80211Peer::setLastQueuePacket(unsigned long long)`             |
| `0xffffff80021c5fbc` | `IO80211Peer::getNumTransmitStatusLog()`                          |
| `0xffffff80021c3692` | `IO80211Peer::getNumTxStatusMismatch()`                           |
| `0xffffff80021c36a2` | `IO80211Peer::setNumTxStatusMismatch(unsigned int)`               |

## Non-claims

CR-193 declares these helpers only; it does not call them, does not
infer any vtable or data-layout assumption about `IO80211Peer`, and
does not change runtime behavior. The kext is bit-identical to
CR-192 (sha256 / UUID unchanged), confirming the change is purely
structural reference alignment.
