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

## Runtime falsification

The reference prerequisite analysis above is still useful, but the local
closure that forced those carriers was wrong for the current bridged driver.
Runtime on 2026-07-07 loaded builds with:

- `cap[10] = 0x08`
- `getSLOW_WIFI_FEATURE_ENABLED() -> enabled = 1`

Those builds passed association and started the IO80211 LQM queue path, but
then repeatedly panicked in:

- `IO80211QueueCall::handleEntry`
- `___stack_chk_fail`
- `Kernel stack memory corruption detected`

The panic occurred after the framework's own LQM queue work ran, so satisfying
the two public gates is not sufficient. The missing layer is the exact Apple
provider/work-queue/link-quality-monitor wiring behind that QueueCall path.

## Corrective closure

This layer is reverted for the current Tahoe bridge:

- CARD_CAPABILITIES keeps the recovered Apple-consistent cluster through
  `cap[8..9] = 0x0201`; it does not advertise local `cap[10] = 0x08`.
- `AirportItlwmSkywalkInterface::getSLOW_WIFI_FEATURE_ENABLED()` returns the
  compact `version + enabled` carrier, but `enabled` comes from the local
  cached policy state instead of being forced to `1`.

Validation after the corrective revert:

- build succeeded and all 936 undefined symbols resolved against BootKC;
- 120/120 ping to `10.77.0.1`, 0% loss, avg 1.432 ms;
- bidirectional TCP stress for 120 seconds while associated:
  guest->host 580,862,008 bytes at 38.683 Mbit/s sender /
  37.471 Mbit/s receiver; host->guest 106,299,392 bytes at
  7.086 Mbit/s sender / 6.913 Mbit/s receiver;
- concurrent stress ping 150/150, 0% loss;
- post-stress ping 10/10, 0% loss, avg 1.944 ms;
- serial panic count did not increase during the corrected run.

The LQM create prerequisite gates should only be re-enabled with a batch that
also restores the exact safe Apple QueueCall/provider wiring.
