# AppleBCMWLAN — IO80211Peer state-flag and counter accessor surface (CR-192)

- date: 2026-04-28
- source-of-truth: BootKC `IO80211Family.kc` symbol table
- justification class: REFERENCE_ALIGNMENT_FIX (header-only declarations)
- supersedes: CR-191

## Scope

Thirty additional public direct-call methods on `IO80211Peer` were
recovered from the BootKC symbol table. Each helper takes only
primitive types (`bool`, `unsigned int`, `unsigned long long`).

The local kext does not call any of these helpers in CR-192; the
declarations are added inside the existing `class IO80211Peer` body
so future caller-wiring CRs can reference the BootKC symbols without
per-caller extern shims.

## Recovered exports

| Address              | Symbol                                                            |
|----------------------|-------------------------------------------------------------------|
| `0xffffff80021c37cc` | `IO80211Peer::getHtOperationIEPresent()`                          |
| `0xffffff80021c37de` | `IO80211Peer::setHtOperationIEPresent(bool)`                      |
| `0xffffff80021c37f0` | `IO80211Peer::getVhtOperationIEPresent()`                         |
| `0xffffff80021c3802` | `IO80211Peer::setVhtOperationIEPresent(bool)`                     |
| `0xffffff80021c6024` | `IO80211Peer::getPeerAddRequestedState()`                         |
| `0xffffff80021c6044` | `IO80211Peer::setPeerAddRequestedState(bool)`                     |
| `0xffffff80021c6034` | `IO80211Peer::getPeerDeleteRequestedState()`                      |
| `0xffffff80021c6052` | `IO80211Peer::setPeerDeleteRequestedState(bool)`                  |
| `0xffffff80021c607c` | `IO80211Peer::isPeerAddRequestInProgress()`                       |
| `0xffffff80021c6060` | `IO80211Peer::setPeerAddRequestInProgress(bool)`                  |
| `0xffffff80021c608c` | `IO80211Peer::isPeerDeleteRequesetInProgress()`                   |
| `0xffffff80021c606e` | `IO80211Peer::setPeerDeleteRequestInProgress(bool)`               |
| `0xffffff80021c5eba` | `IO80211Peer::setBssSteeringPeerSyncState(bool)`                  |
| `0xffffff80021c33f2` | `IO80211Peer::getUnicastBonjourRx()`                              |
| `0xffffff80021c3402` | `IO80211Peer::setUnicastBonjourRx(unsigned int)`                  |
| `0xffffff80021c3412` | `IO80211Peer::getMulticastBonjourRx()`                            |
| `0xffffff80021c3422` | `IO80211Peer::setMulticastBonjourRx(unsigned int)`                |
| `0xffffff80021c61b8` | `IO80211Peer::getBeaconReceivedCount()`                           |
| `0xffffff80021c61ca` | `IO80211Peer::incrementBeaconReceivedCount()`                     |
| `0xffffff80021c6132` | `IO80211Peer::getTotalDataLinkCount()`                            |
| `0xffffff80021c6140` | `IO80211Peer::incrementTotalDataLinks()`                          |
| `0xffffff80021c614e` | `IO80211Peer::decrementTotalDataLinks()`                          |
| `0xffffff80021c610e` | `IO80211Peer::incrementDataLinks()`                               |
| `0xffffff80021c611c` | `IO80211Peer::decrementDataLinks()`                               |
| `0xffffff80021c60ce` | `IO80211Peer::getRealTimeDataSessionCount()`                      |
| `0xffffff80021c59b6` | `IO80211Peer::incrementRealTimeDataSession()`                     |
| `0xffffff80021c59c4` | `IO80211Peer::decrementRealTimeDataSession()`                     |
| `0xffffff80021c60c0` | `IO80211Peer::getLowLatencyDataSessionCount()`                    |
| `0xffffff80021c59dc` | `IO80211Peer::incrementLowLatencyDataSession()`                   |
| `0xffffff80021c5a2a` | `IO80211Peer::decrementLowLatencyDataSession()`                   |

## Non-claims

CR-192 declares these helpers only; it does not call them, does not
infer any vtable or data-layout assumption about `IO80211Peer`, and
does not change runtime behavior. The kext is bit-identical to
CR-191 (sha256 / UUID unchanged), confirming the change is purely
structural reference alignment.
