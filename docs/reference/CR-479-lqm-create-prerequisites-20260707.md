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

## Runtime falsification of the first attempt

The reference prerequisite analysis above is still useful, but the first local
closure that forced those carriers was incomplete for the current bridged
driver.
Runtime on 2026-07-07 loaded builds with:

- `cap[10] = 0x08`
- `getSLOW_WIFI_FEATURE_ENABLED() -> enabled = 1`

Those builds passed association and started the IO80211 LQM queue path, but
then repeatedly panicked in:

- `IO80211QueueCall::handleEntry`
- `___stack_chk_fail`
- `Kernel stack memory corruption detected`

The later session notes corrected the root cause of that stack-smash: the
selector queried from the LQM QueueCall is `APPLE80211_IOC_CHIP_DIAGS`
(`0x16a` in the Tahoe IOC table), not `CUR_PMK`. The framework probes it with
a 4-byte stack buffer. The local `getCHIP_DIAGS` implementation was returning
success and zeroing the public 0x14-byte carrier, which overwrote the canary.
The Apple-compatible behavior without the private diagnostic owner is a miss:
return `0xe00002c7` and write nothing.

## 2026-07-08 corrected probe result

A new runtime check enabled the two LQM gates together with the corrected
`getCHIP_DIAGS()` no-write miss. That proved the stack-smash root above:
the QueueCall no longer hit `___stack_chk_fail`. It also proved the create
prerequisites are still incomplete for the bridged driver:

- `IO80211PeerMonitor::createLinkQualityMonitor(3511) failed`;
- `IO80211LinkQualityMonitor::free: slow-wifi Destroying IO80211LinkQualityMonitor`;
- association did not reach DHCP and CoreCapture later panicked on
  `[FG] IP timed out (NoCTL)`.

Therefore the stable bridge policy remains:

- CARD_CAPABILITIES keeps the recovered Apple-consistent request bitmap but
  does not advertise local `cap[10] = 0x08`.
- `AirportItlwmSkywalkInterface::getSLOW_WIFI_FEATURE_ENABLED()` returns the
  compact `version + enabled` carrier, but `enabled` comes from the local
  cached policy state instead of being forced to `1`.
- `AirportItlwmSkywalkInterface::getCHIP_DIAGS()` returns `0xe00002c7` and
  performs no buffer write; this part is closed and should stay.

The LQM create gates should only be re-enabled after the exact line-3511
PeerMonitor prerequisite is recovered from the 25C56 decompile/runtime logs.
