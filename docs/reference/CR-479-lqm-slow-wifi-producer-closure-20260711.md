# CR-479: LQM slow-WiFi producer quarantine

Date: 2026-07-11

## Scope

`IO80211InfraInterface::createLinkQualityMonitor` obtains selector `0x187`
(`SLOW_WIFI_FEATURE_ENABLED`) and uses carrier dword `+0x04` as options bit
zero. This note closes the investigation of the missing local producer edge:
the reference update source is recovered exactly, but reproducing it exposes
the already-known incomplete local LQM consumer. The local driver must not
acknowledge or cache a bit-2-bearing raw feature word, nor route bit 2 into the
public getter, until that consumer is recovered. It retains the raw carrier
only for requests whose bit 2 is clear.

## Reference evidence

The analyzed macOS 26.2 build 25C56 guest `BootKernelExtensions.kc` has
SHA-256 `eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d`.

Two raw 25C56 instruction captures establish the Apple owner lifecycle:

- Core initialization at `0xffffff80015638e1` executes
  `MOV dword ptr [RAX + 0x7569],0x1010001`; slow-WiFi's low byte is initially
  `1`.
- The raw function beginning `0xffffff8001616a46` accepts a non-null
  eight-byte `apple80211_feature_flags` word, stores it at core `+0x4470`,
  then executes `SHR DL,0x2`, `AND DL,0x1`, and
  `MOV byte ptr [RAX + 0x7569],DL` at `0xffffff8001616ad0`.

The only direct caller is the current InfraProtocol thunk
`0xffffff8001522d90 -> 0xffffff8001616a46`; its corresponding 25C56 KDK
slot is `AppleBCMWLANInfraProtocol::setOS_FEATURE_FLAGS`. The KDK label is
semantic support only because this image has known address drift; the raw
caller, input dereference, and bit extraction prove the write. The retained
Ghidra captures are:

- `10.7.6.112:~/Projects/ghidra_output/aiam_slow_wifi_7569_runtime_writer_25C56_20260711.txt`
- `10.7.6.112:~/Projects/ghidra_output/aiam_slow_wifi_writer_caller_25C56_20260711.txt`
- `10.7.6.112:~/Projects/ghidra_output/aiam_slow_wifi_policy_owner_25C56_20260711.c`

The established getter proof is unchanged: selector `0x187` returns
`core[0x7569] & 1` at carrier dword `+0x04` and preserves caller dword `+0x00`.

## Runtime falsification

The direct producer-to-getter implementation was built and tested twice on
the 25C56 guest. It is rejected, not committed:

- Candidate `EACC3D4B-4773-30D1-83FF-CB21BE2A7DF8` copied the Apple startup
  low byte. Its runtime capture contains `IO80211QueueCall::handleEntry` type
  3 with `0xe00002c7`; concurrent 240-second ping/iperf produced `239/240`
  ping.
- Candidate `17DA2F0A-6A4D-33C6-BAD4-3E368575F3C8` left startup at zero but
  mapped OS feature word bit 2 exactly. Its runtime capture again contains the
  type-3 `0xe00002c7` path and produced `239/240` ping.

Both candidates completed iperf3, but neither outcome passes the required
zero-loss runtime gate. The raw artifacts are retained outside the source
tree at `runtime-captures/itlwm-lqm-slow-wifi-osflags-20260711T193821Z/` and
`runtime-captures/itlwm-lqm-slow-wifi-osflags-nullowner-20260711T195333Z/`.

A focused FBT run on rejected UUID `17DA2F0A-6A4D-33C6-BAD4-3E368575F3C8`
then recorded `IO80211InfraInterface::createLinkQualityMonitor` itself:

```text
requested=1 -> return=3758097095 (0xe00002c7)
```

No `IO80211PeerMonitor::createLinkQualityMonitor` or
`IO80211LinkQualityMonitor::initWithProviderAndOptions` probe fired. This
matches the direct 25C56 `cap+0xb36 & 0x08` branch, proving that the first
local consumer gate is the intentionally absent `CARD_CAPABILITIES[10]` bit.
The line-3511 PeerMonitor prerequisite is the next gate only after that
capability is correctly owned. The FBT artifact is retained at
`runtime-captures/itlwm-lqm-peer-prereq-trace-20260711T200800Z/`.

## Local disposition

`setOS_FEATURE_FLAGS` retains the native 64-bit word in
`cachedOSFeatureFlags` only when slow-WiFi bit 2 is clear. If bit 2 is set,
it returns `kIOReturnUnsupported` before the cache write. `TahoeOwnerRegistry`
remains the actual local null-owner for the slow-WiFi getter, initialized to
`0`; there is deliberately no local
`OS_FEATURE_FLAGS bit 2 -> SLOW_WIFI_FEATURE_ENABLED` transition and no
startup copy of the private Apple core byte.

The bit-2 rejection is a quarantine, not a substitute value or compatibility
gate. Apple accepts the word only because it owns the dependent LQM capability
and PeerMonitor state. This driver does not, so cache-only success would make
a false externally visible claim. The quarantine does not claim to suppress
other LQM or QueueCall work in the radio lifecycle.

`scripts/lqm_slow_wifi_producer_report.py` checks the reference anchors,
the retained raw feature-word carrier for bit-2-clear requests, the bit-2
quarantine, and the absence of the unsafe local mapping deterministically. Its
checked-in output is
`evidence/state/lqm_slow_wifi_producer_report.json`.

## Current candidate runtime validation

Candidate `18696FED-D0E0-3E57-BAC7-B09E499527B1` was loaded after the host
reboot; the active bundle and AuxiliaryKC inspection both matched that UUID.
During a normal radio OFF/ON lifecycle, a passive FBT probe recorded the
system-managed `setOS_FEATURE_FLAGS` producer passing `0x408967c` twice. Bit
2 is set in that word, and both local returns were `0xe00002c7`
(`kIOReturnUnsupported`). The probe is retained at
`/Users/devops/runtime/slowwifi-bit2-system-producer-20260717T165823Z/fbt.log`;
the matching external IOC results are in
`/home/dima/Projects/itlwm/vm-boot/20260717T164704Z-5g153-vht80-slowwifi-resume/serial.log`.
This proves the narrow behavior of the candidate: it truthfully rejects a
real bit-2 carrier before `cachedOSFeatureFlags` is written.

That same timestamp-less FBT capture contains `IO80211QueueCall` and
`IO80211InfraInterface::createLinkQualityMonitor` entries, including one
QueueCall before the first captured OS-feature entry. It therefore neither
establishes a causal ordering between OS feature flags and QueueCall nor
proves that this quarantine suppresses LQM work. Those LQM paths remain an
open surface. Separately, the post-reboot candidate passed a clean 240-second
traffic gate; its artifacts are
`/Users/devops/runtime/slowwifi-18696-post-host-reboot-20260717T164918Z/ping-240.log`
and `iperf3-240.json` in the same directory. It also passed four strict radio
OFF/ON cycles on the 5 GHz channel-153, VHT80 lab AP; the four-cycle log is

## Non-claims

- This does not set `CARD_CAPABILITIES[10] & 0x08` or enable LQM creation.
- This does not implement the other Apple `setOS_FEATURE_FLAGS` fan-out
  branches (DynSAR, 6G, KVR, and link-loss configuration).
- This does not change the `CHIP_DIAGS` no-write unsupported behavior.
- This does not claim that OS feature flags cause, or that this guard suppresses,
  any observed `IO80211QueueCall` work.
