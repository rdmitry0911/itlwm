# CR-479 LQM create prerequisites

Date: 2026-07-07

## Scope

The post-association Tahoe path was still logging:

- `IO80211QueueCall::handleEntry - called type 3, error 0xe00002c7`

Type 3 is the asynchronous `IO80211InfraInterface::createLinkQualityMonitor`
queue entry. Two prerequisite fields were present in the earlier working
session but were lost during branch cleanup:

- CARD_CAPABILITIES capability index `0x53`
- `getSLOW_WIFI_FEATURE_ENABLED` returning `enabled = 1`

## Reference evidence

Recovered IO80211Family flow:

- `IO80211InfraInterface::createLinkQualityMonitor(IO80211Peer *, bool)`
  checks the loaded card-capability bitmap at `cap+0xb36` bit `0x08`.
- `IO80211Controller::loadCardCapabilities` copies
  `apple80211_capability_data.capabilities[0]` to the framework bitmap base
  at `cap+0xb2c`; therefore `cap+0xb36` is local `capabilities[10]`.
- `IO80211PeerMonitor::createLinkQualityMonitor(IOService *, Options const *)`
  has an early `opts[0] & 1` requirement. Live dtrace in session
  `34da2167-3af4-4a61-8925-c015c3ba857c` proved `opts[0]` is built from the
  slow-wifi selector success and the non-zero dword at payload offset `+4`.

The older CARD_CAPABILITIES shadow fix remains valid for clearing Apple-
impossible AKM bits. This layer adds the separate LQM capability bit instead of
reintroducing any of the rejected `cap[2]`, `cap[3]`, or `cap[6]` bits.

## Local closure

`TahoeCapabilityContracts::applyAppleConsistentCardCapabilityCluster()` now
also sets:

- `cap[10] = 0x08`

Both Tahoe and legacy `getCARD_CAPABILITIES` producers use that helper, so the
framework's loaded capability bitmap satisfies the LQM gate consistently.

`AirportItlwmSkywalkInterface::getSLOW_WIFI_FEATURE_ENABLED()` returns the
Apple-compatible compact carrier:

- `version = APPLE80211_VERSION`
- `enabled = 1`

This restores the two prerequisite selector answers required before the next
layer, where the LQM monitor can be created and fed with real link statistics.
