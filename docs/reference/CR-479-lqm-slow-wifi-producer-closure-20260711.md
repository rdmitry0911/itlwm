# CR-479: LQM slow-WiFi producer quarantine

Date: 2026-07-11

## Scope

`IO80211InfraInterface::createLinkQualityMonitor` obtains selector `0x187`
(`SLOW_WIFI_FEATURE_ENABLED`) and uses carrier dword `+0x04` as options bit
zero. This note closes the investigation of the missing local producer edge:
the reference update source is recovered exactly, but reproducing it exposes
the already-known incomplete local LQM consumer. The local driver must retain
the raw feature word without routing this bit into the public getter until that
consumer is recovered.

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
  low byte. It reached `IO80211QueueCall::handleEntry` type 3 with
  `0xe00002c7` and concurrent 240-second ping/iperf produced `239/240` ping.
- Candidate `17DA2F0A-6A4D-33C6-BAD4-3E368575F3C8` left startup at zero but
  mapped OS feature word bit 2 exactly. A real userspace feature update again
  reached the same type-3 `0xe00002c7` path and produced `239/240` ping.

Both candidates completed iperf3, but neither outcome passes the required
zero-loss runtime gate. The raw artifacts are retained outside the source
tree at `runtime-captures/itlwm-lqm-slow-wifi-osflags-20260711T193821Z/` and
`runtime-captures/itlwm-lqm-slow-wifi-osflags-nullowner-20260711T195333Z/`.
The known line-3511 `IO80211PeerMonitor::createLinkQualityMonitor`
prerequisite, not a selector ABI error, is the next kernel-facing layer.

## Local disposition

`setOS_FEATURE_FLAGS` continues to retain the native 64-bit word in
`cachedOSFeatureFlags`. `TahoeOwnerRegistry` remains the actual local
null-owner for the slow-WiFi getter, initialized to `0`. There is deliberately
no local `OS_FEATURE_FLAGS bit 2 -> SLOW_WIFI_FEATURE_ENABLED` transition and
no startup copy of the private Apple core byte.

This is not a substitute value or a compatibility gate: it is the reference
null-owner branch for a driver that does not yet own the dependent LQM
PeerMonitor state. Adding the recovered producer before recovering that owner
would turn a known queue failure into an externally visible regression.

`scripts/lqm_slow_wifi_producer_report.py` checks the reference anchors,
the retained raw feature-word carrier, and the absence of the unsafe local
mapping deterministically. Its checked-in output is
`evidence/state/lqm_slow_wifi_producer_report.json`.

## Non-claims

- This does not set `CARD_CAPABILITIES[10] & 0x08` or enable LQM creation.
- This does not implement the other Apple `setOS_FEATURE_FLAGS` fan-out
  branches (DynSAR, 6G, KVR, and link-loss configuration).
- This does not change the `CHIP_DIAGS` no-write unsupported behavior.
