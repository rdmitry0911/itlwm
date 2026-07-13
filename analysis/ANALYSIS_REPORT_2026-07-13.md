# Analysis report — 2026-07-13

## ANOMALY

- id: `CR-479-BSS-BLACKLIST-PUBLIC-ROUTE-P0`
- status: `FIX_VERIFIED`
- symptom: the in-progress Tahoe `APPLE80211_IOC_BSS_BLACKLIST` (`0x174`)
  owner layer did not preserve the recovered public route's short-circuit
  error and admission semantics.
- first visible manifestation: source and BootKC artifact comparison during
  the async-owner recovery found that the local BSD bridge forwarded selector
  `0x174` without an interface-presence return, bypassed its existing
  `isCommandProhibited` vtable gate, and mapped a missing controller owner to
  `kIOReturnNotReady`.
- expected system behavior: in 25C56, the selector-specific GET and SET
  routes return raw `0x66` for a null interface before inspecting the request
  carrier. With an interface present, a null or non-`0x2b` carrier returns raw
  `0x16`. The public wrapper then invokes vtable `+0xcc8(this, 0x174)`, returns
  a non-zero result unchanged, and returns `0xe082280e` if its required class
  owner cast is absent.
- actual behavior: `processBSDCommand` had no selector-specific null-interface
  branch; the BSS switch dispatched before admission; and both BSS owner entry
  points returned `kIOReturnNotReady` when `instance == nullptr`.
- divergence point:
  - local `AirportItlwmSkywalkInterface::processBSDCommand`,
    `processApple80211Ioctl`, `getBSS_BLACKLIST`, and `setBSS_BLACKLIST`;
  - reference GET route `0xffffff80021dbe61`, SET route
    `0xffffff80021e8899`, GET wrapper `0xffffff80021c1cfb`, and SET wrapper
    `0xffffff80021c6f95`.
- evidence:
  - runtime: the earlier targeted valid-carrier trace captures BSS getter
    ingress at
    `/home/dima/Projects/aiam/runtime-captures/itlwm-bss-blacklist-quarantine-20260712/runtime/bss-probe-list.raw`.
    It confirms the normal selector path is live; it cannot safely construct
    a kernel-null `ifnet_t`, so it is not used to infer the error branches.
  - decomp: deterministic 25C56 route/wrapper artifacts under
    `docs/reference/artifacts/bss-blacklist-25C56/` show `TEST RSI` followed
    by raw `0x66` before carrier checks, and the `+0xcc8` admission/cast
    sequence with raw `0xe082280e`.
  - docs: `docs/reference/CR-479-bss-blacklist-async-owner-20260713.md`.
- candidate causes: this is a direct ABI/return-semantic deviation; no claim
  is made that it causes the separate WCL radio-cycle panic shared by the
  baseline and candidate.
- rejected causes: no retry, replay, scan filtering, MACMODE emulation, or
  WCL lifecycle modification is implied by these public wrapper branches.
- confirmed deviation: route precedence, admission propagation, and the
  missing-owner status differ from the current 25C56 reference.
- root cause: not established for a broader runtime symptom; the patch scope
  is the directly confirmed public ABI deviation only.
- fix: the BSS-only BSD ingress now performs the recovered preflight; the
  dispatch performs carrier validation, boolean admission, and required-owner
  status ordering; direct owner entry points map only `instance == nullptr` to
  `0xe082280e`.
- verification:
  - standalone branch-matrix assertions and BSS evidence report passed with
    all 11 deterministic 25C56 artifact hashes;
  - a clean Tahoe build resolved `959/959` undefined symbols and produced
    UUID `F011FAD1-0DE3-32BE-B42B-C6368DBF0504`, binary SHA-256
    `e73f9f4c85f0fa26032ac76dfbe3f9de27b6085b11ac69b451d5739051d3cb6a`;
  - the rebuilt AuxKC and loaded kext carry that same UUID after guest reboot;
  - the installed BSS probe returned `EINVAL` for both 42-byte directions,
    preserved GET caller bytes, emitted the expected four valid `0xa3`
    snapshots, and ended `RESULT PASS events=4 overflow=0`;
  - explicit saved-profile rejoin restored `en1` `10.77.0.47`; 240 seconds
    of concurrent ping and iperf3 completed with `240/240`, 0% loss, and
    `1.95 GBytes` at `69.9 Mbits/sec`; hostapd kept the station authorized
    with zero TX failures, QEMU remained running, and no new guest panic or
    host AER/VFIO/DMAR/IOMMU fault was recorded.
- evidence path:
  `/home/dima/Projects/aiam/runtime-captures/itlwm-bss-blacklist-route-p0-20260713/`.
- notes: the generic BSD bridge cannot identify selector `0x174` when its
  outer `apple80211req *data` itself is null, so this patch deliberately does
  not alter unrelated generic-null routing. The recovered selector-bearing
  route path is covered before dispatch.

## FIX_CANDIDATE

- anomaly_id: `CR-479-BSS-BLACKLIST-PUBLIC-ROUTE-P0`
- symptom: the BSS blacklist public route exposes non-reference raw error,
  admission, and owner-absent behavior.
- expected system behavior: `0x66` is returned before carrier validation when
  the interface is absent; valid-interface malformed carriers return `0x16`;
  the `+0xcc8` gate precedes the required cast and returns its non-zero value
  unchanged; a failed cast returns `0xe082280e`.
- actual behavior: no local `0x66` branch, no `0x174` gate, and
  `kIOReturnNotReady` for an absent `instance`.
- exact divergence point: the four local functions and four 25C56 functions
  named in the anomaly above.
- evidence from runtime: the existing FBT probe proves valid BSS getter
  dispatch; null-interface and failed-cast branches are not safely exercisable
  from an unprivileged client and remain directly proven by the reference
  route/wrapper instructions and deterministic pure branch tests.
- evidence from decomp: the tracked raw GET/SET route and wrapper artifacts
  establish the order and all three raw status values.
- exact semantic mismatch between reference and our code: the local bridge
  validated/forwarded without the reference interface short-circuit, skipped
  a virtual admission call, and translated an owner absence to a different
  IOReturn.
- fix justification path: `REFERENCE_ALIGNMENT_FIX`.
- why this is root cause and not just correlation: it is not asserted as the
  root cause of the WCL panic. It is a complete, independently confirmed
  public-route deviation with direct reference instruction evidence.
- why proposed fix is 1:1 with reference architecture and semantics:
  - add a BSS-only preflight before generic forwarding, preserving `0x66`
    ahead of inner carrier validation;
  - use the existing ABI-stable `bool isCommandProhibited(int)` vtable slot,
    preserving its only possible local raw values `0` and `1` rather than
    changing the vtable signature;
  - preserve an arbitrary non-zero wrapper status in the pure contract helper;
  - map only `instance == nullptr` to `0xe082280e`, leaving lower HAL, command
    gate, and net80211 statuses untouched.
- files/functions to modify:
  - `AirportItlwm/TahoeBssBlacklistContracts.hpp` — scalar route/wrapper
    contract helpers;
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp` — BSS-only ingress and
    dispatch wiring;
  - `tests/tahoe_payload_builders_test.cpp` — deterministic precedence and
    status matrix;
  - BSS reference note, discrepancy/signal-chain records, and evidence report.
- forbidden alternative fixes considered and rejected:
  - adding selector `0x174` to hidden-association prohibition (would reject
    the working selector);
  - changing the `bool` vtable ABI to synthesize arbitrary IOReturns;
  - mapping all `kIOReturnNotReady` exits to `0xe082280e`;
  - adding lower MACMODE/MACLIST programming, selection policy, retries,
    replay, or WCL changes to this route-only batch.
- verification plan: run the standalone test matrix and BSS evidence report,
  build a clean Tahoe product with undefined-symbol check, install/reboot,
  run the valid BSS GET/SET probe plus steady-state traffic, and compare the
  build/load identities before committing this bounded layer.

## VERIFIED RESULT

The verification plan completed on the exact compiled source-code delta
(`git diff --cached --binary HEAD -- AirportItlwm include itl80211 itlwm
itlwm.xcodeproj scripts/build_tahoe.sh scripts/tahoe_source_identity.py`
SHA-256 `17b6dec75d110c10f4be52410af4ed2bae9e39e7f967470c149449faddcdeaea`).
The BSS layer is verified only for its declared public-route and local
owner-model scope. The reference lower MACLIST callback, MACMODE programming,
and WCL candidate selection remain separate open surfaces. The public
`networksetup -getairportnetwork en1` false-negative remains a known Tahoe
framework reporting behavior: hostapd, IP, probe, and traffic evidence prove
the actual association in this run.

## FIX_VERIFIED — TX-power-cap false-success quarantine

- anomaly_id: `CR-479-TX-POWER-CAP-FALSE-SUCCESS-P0`
- status: `FIX_VERIFIED`
- symptom: Tahoe advertises successful `BYPASS_TX_POWER_CAP` and
  `DUAL_POWER_MODE` requests even though the Intel backend has no equivalent
  firmware TX-power-cap owner.
- expected system behavior: 25C56 routes `DUAL_POWER_MODE` from the public
  bridge at `0xffffff8001522f42` into
  `AppleBCMWLANCore::setDUAL_POWER_MODE` at `0xffffff80016176e2`. A null
  carrier returns raw `0xe00002bc`; a valid carrier stores the two signed
  dwords and immediately enters the `0xffffff800160b3e0` sender, which issues
  firmware `txcapstate`. The same Core TX-power-cap owner services the bypass
  policy instead of completing it as a local cache-only request.
- actual behavior: non-null bypass reaches
  `TahoeCommanderV2::runSetBypassTxPowerCap`, which updates synthetic registry
  state and calls a transport helper that only records/completes status zero.
  Non-null dual-power requests update two interface caches and the same
  synthetic registry, then return success. The current Intel source contains
  no `txcapstate` backend or TX-power-cap firmware request.
- exact divergence point:
  - local `AirportItlwmSkywalkInterface::setBYPASS_TX_POWER_CAP` and
    `setDUAL_POWER_MODE`;
  - local `TahoeCommanderV2::dispatchTransport` and
    `TahoeTxPowerCapOwner::apply`;
  - reference public/Core/sender chain named above.
- evidence from runtime: the previous isolated candidate built and loaded,
  then passed bidirectional 240-second traffic, 480 ping samples, AP
  authorization, and serial fault filters. Its first radio OFF/ON panic is
  non-attributable because the restored bit-identical A2DF baseline immediately
  reproduced the same WCL chain. See
  `/home/dima/Projects/aiam/runtime-captures/itlwm-tx-power-cap-quarantine-20260712/`.
  Consequently radio OFF/ON is explicitly excluded from this layer's gate.
- evidence from static recovery: the preserved 25C56 chain and prior candidate
  record are in `ITLWM_CODEX_MEMORY.md` and the rejection record above;
  current source inspection proves the synthetic success path and absence of a
  backend. The retained reference note records the exact bridge/Core/sender
  anchors; this correction does not infer a valid-input Apple return value
  from the quarantine.
- confirmed deviation: a public caller observes success even though no Intel
  radio policy is changed and no firmware completion can occur.
- fix justification path: `REFERENCE_ALIGNMENT_SAFETY_QUARANTINE`.
- why this is root cause and not just correlation: it is not a claim about the
  separate WCL lifecycle panic. It is a direct false-capability violation:
  local completion succeeds after only local bookkeeping while reference
  completion owns real firmware `txcapstate` work.
- proposed fix: retain the reference null error, then fail closed with
  `kIOReturnUnsupported` for every non-null bypass or dual-power request before
  any pseudo-state or commander mutation. Remove only their three now-dead
  interface cache fields and retire the stale payload-parity claim that called
  this a firmware-send surface.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp` and `.hpp`;
  - `AirportItlwm/TahoePayloadParity.hpp` and
    `scripts/payload_parity_report.py`;
  - a dedicated TX-power-cap quarantine report, reference note, and this
    analysis record.
- forbidden alternative fixes considered and rejected:
  - fabricating `txcapstate` through the generic TahoeCommander transport;
  - changing PM, radio state, `0x37`, WCL, association, or the generic
    commander implementation;
  - claiming `kIOReturnUnsupported` is Apple's valid-input result;
  - using the baseline-shared radio OFF/ON fault as a candidate gate.
- verification plan: deterministic source guard plus existing payload checks,
  clean Tahoe build and symbol resolution, AuxKC install/load identity,
  bounded setter-ingress observation if callable, and saved-profile rejoin
  followed by bounded bidirectional traffic/ping with guest and host fault
  filters. The runtime report will distinguish regression coverage from direct
  selector invocation if the private setters are not externally callable.

## VERIFIED RESULT — TX-power-cap false-success quarantine

The declared verification plan completed.  The exact compiled source-code
delta (build inputs only) has SHA-256
`2c8ecf517e6593ed2d8b9b33b749b5057e60a33bfc7c91f4193b5526f543af1c`.
`git diff --cached --check`, the 31-contract Tahoe payload-builder test,
the dedicated six-invariant quarantine report, and the payload-parity report
all passed.  A clean Tahoe build resolved all 959 undefined symbols against
BootKC.

The installed candidate loaded as UUID
`26FA0214-149C-3125-848E-7CB17E0042F9` with signed executable SHA-256
`c0c9859281aa3ba3be5339906e0dec1e7c6580590b1809ddf83d1e5950b5e1ac`
and AuxKC SHA-256
`bf818ef47ead9755d099e1e7d75ff08fe60f12b9bc82ffbf201381fa7b9f44e0`.
After saved-profile rejoin, capped uplink and reverse 240-second gates each
transferred 572 MiB at 20.0 Mbit/s with 240/240 concurrent ping replies and
0.0% loss (mean RTT 4.805 ms and 6.146 ms respectively; reverse sender had
one retransmit).  Hostapd kept the station authorized and QEMU remained
running.

The public root-only ioctl probe stops at an outer unsupported socket gate
before a private setter; it is explicitly **not** used to claim direct setter
runtime invocation or Apple valid-input return-code parity.  Radio OFF/ON is
excluded because the restored bit-identical A2DF baseline reproduces the same
separate WCL lifecycle panic.  Guest boot `DumpPanic` zero-file bookkeeping,
codeless `ApplePVPanic` warnings, and one older host correctable AER record
are not misreported as candidate faults.  Full immutable runtime evidence is
under `/home/dima/Projects/aiam/runtime-captures/itlwm-tx-power-cap-quarantine-20260713/`.

## FIX_VERIFIED — LMTPC configuration false-success quarantine

- anomaly_id: `CR-479-LMTPC-CONFIG-FALSE-SUCCESS-P0`
- status: `FIX_VERIFIED`
- symptom: a non-null `LMTPC_CONFIG` request reports success although the
  Intel port does not have an LMTPC owner or an `lpc` firmware transport.
- expected system behavior: the recovered 25C56 Core setter rejects null with
  `0xe00002bc`, persists the input byte at Core `+0x4594`, then calls an LMTPC
  owner. The owner sends a four-byte firmware `lpc` iovar through Commander
  under the `>= 0x1123` firmware gate.
- actual behavior: local
  `AirportItlwmSkywalkInterface::setLMTPC_CONFIG` preserves the null guard,
  then writes only `cachedLmtpcValue` and returns success. The cache has no
  consumer; scoped local inventory finds no LMTPC owner, `lpc` iovar,
  `runSetLMTPC`, or equivalent Intel transport.
- exact divergence point: local cache-only setter versus
  `AppleBCMWLANCore::setLMTPC_CONFIG` at `0x100142c22` and its real LMTPC
  owner at `0x1000fe4c0`; see
  `docs/reference/CR-479-lmtpc-config-quarantine-20260713.md`.
- evidence from static recovery: the exact 25C56 reference image has SHA-256
  `4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`;
  raw recovery establishes null/carrier/owner/firmware behavior. Scoped local
  inspection proves the cache-only success path and backend absence.
- confirmed deviation: callers are told an LMTPC policy was accepted while no
  Intel firmware policy can be applied.
- fix justification path: `REFERENCE_ALIGNMENT_SAFETY_QUARANTINE`.
- why this is root cause and not just correlation: this does not explain the
  independent WCL lifecycle panic. It is a direct false-capability boundary:
  local success follows a dead cache write while the reference owner performs
  real firmware work.
- proposed fix: retain the recovered null error, reject all non-null requests
  with `kIOReturnUnsupported` before mutation, and remove only the dead LMTPC
  cache, its initializers, and its now-unused byte carrier declaration.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp` and `.hpp`;
  - dedicated LMTPC quarantine report, reference note, and this analysis
    record.
- forbidden alternative fixes considered and rejected:
  - fabricating `lpc` through the generic TahoeCommander transport;
  - changing PM, radio state, `0x37`, WCL, association, or generic commander
    semantics;
  - claiming `kIOReturnUnsupported` is Apple's valid-input result;
  - using the baseline-shared radio OFF/ON fault as a candidate gate.
- verification plan: deterministic source guard plus existing payload checks,
  clean Tahoe build and symbol resolution, AuxKC install/load identity,
  bounded ingress observation if callable, saved-profile rejoin, bounded
  bidirectional traffic/ping, and guest/host fault filters. The runtime report
  will distinguish regression coverage from direct private-setter execution.

## VERIFIED RESULT — LMTPC configuration false-success quarantine

The declared verification plan completed.  The compiled source-code delta
(build inputs only) has SHA-256
`cdf819057b42f4a05ef7cc02695aa0ee91c90beb1f12c12ee9c8d8d028377f4e`.
`git diff --cached --check`, the 31-contract Tahoe payload-builder test, the
five-invariant LMTPC quarantine report, the retained TX-power-cap report, and
the payload-parity report all passed.  A clean Tahoe build resolved all 959
undefined symbols against BootKC.

The installed candidate loaded as UUID
`DA95BC10-2BB8-3CCD-B5A9-D81A374DB12D` with signed executable SHA-256
`14f8e154e5715521ec1eb02ee24a216046cfe5e9842ec2ca524add541206cb5c`
and AuxKC SHA-256
`e6058b390ec54310ca9713ebf1ec81240f8018da34213419be3726e95ab7cc09`.
After explicit saved-profile rejoin, capped uplink and reverse 240-second
gates each transferred 572 MiB at 20.0 Mbit/s with 240/240 concurrent ping
replies and 0.0% loss (mean RTT 4.579 ms and 6.385 ms respectively; reverse
sender had one retransmit).  Hostapd retained an authorized station with zero
TX failures, QEMU remained running, the bounded guest fault filter had no
matching panic/WCL/AirportItlwm failure marker, and the bounded host filter had
no fatal VFIO/IOMMU/AER match.

The recovered reference consumes effective input byte `+0`, but does not prove
the complete public-carrier allocation size.  No guessed private-setter ioctl
was issued, so this is explicitly not a claim of direct setter runtime
invocation or Apple valid-input return-code parity.  Radio OFF/ON remains
excluded because the restored bit-identical A2DF baseline reproduces the same
separate WCL lifecycle panic.  Full immutable runtime evidence is under
`/home/dima/Projects/aiam/runtime-captures/itlwm-lmtpc-config-quarantine-20260713/`.

## FIX_VERIFIED — Battery powersave configuration false-success quarantine

- anomaly_id: `CR-479-BATTERY-POWERSAVE-CONFIG-FALSE-SUCCESS-P0`
- status: `FIX_VERIFIED`
- symptom: a non-null `BATTERY_POWERSAVE_CONFIG` request reports success even
  though the Intel port has no matching MIMO-power-save/MRC-threshold owner or
  `mrc_rssi_threshold` firmware transport.
- expected system behavior: recovered 25C56 wrapper/Core code rejects null
  with `0xe00002bc`, consumes effective dword `+0`, and enters the battery-save
  owner chain. Under MIMO-PS and association gates, the owner sends a
  four-byte signed `mrc_rssi_threshold` firmware iovar.
- actual behavior: local
  `AirportItlwmSkywalkInterface::setBATTERY_POWERSAVE_CONFIG` preserves the
  null guard, then writes only `cachedBatteryPowerSaveMode` and returns
  success. The cache has no consumer; scoped local inventory finds no
  BatterySaveMode owner, MRC threshold configuration, iovar, or mapped Intel
  transport.
- exact divergence point: local cache-only setter versus Infra wrapper
  `0x100018f44`, Core `0x100142544`, and conditional MRC owner chain described
  in `docs/reference/CR-479-battery-powersave-config-quarantine-20260713.md`.
- evidence from static recovery: the exact reference image has SHA-256
  `4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`;
  raw recovery establishes null/effective-dword/state-gate/firmware behavior.
  Scoped local inspection proves the cache-only success path and backend
  absence.
- confirmed deviation: callers are told a battery powersave policy was
  accepted while the local port cannot reproduce the conditional MRC firmware
  work.
- fix justification path: `REFERENCE_ALIGNMENT_SAFETY_QUARANTINE`.
- why this is root cause and not just correlation: this does not explain the
  independent WCL lifecycle panic. It is a direct false-capability boundary:
  local success follows a dead cache write while the reference can issue real
  firmware policy work.
- proposed fix: retain the recovered null error, reject all non-null requests
  with `kIOReturnUnsupported` before mutation, and remove only the dead cache
  plus its initializers.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp` and `.hpp`;
  - dedicated battery-powersave quarantine report, reference note, and this
    analysis record.
- forbidden alternative fixes considered and rejected:
  - fabricating `mrc_rssi_threshold` through the generic TahoeCommander;
  - changing MIMO, PM, radio state, `0x37`, WCL, association, or generic
    commander semantics;
  - claiming `kIOReturnUnsupported` is Apple's valid-input result;
  - using the baseline-shared radio OFF/ON fault as a candidate gate.
- verification plan: deterministic source guard plus existing payload checks,
  clean Tahoe build and symbol resolution, AuxKC install/load identity,
  bounded ingress observation if callable, saved-profile rejoin, bounded
  bidirectional traffic/ping, and guest/host fault filters. The runtime report
  will distinguish regression coverage from direct private-setter execution.

## VERIFIED RESULT — Battery powersave configuration false-success quarantine

The declared verification plan completed. The compiled source-code delta
(build inputs only) has SHA-256
`f8b7e63a670d7f579ac221970d85115d14fbc4cf0cc52d022cb8924b4c1fb826`.
`git diff --cached --check`, the 31-contract Tahoe payload-builder test, the
five-invariant battery-powersave quarantine report, retained LMTPC and
TX-power-cap reports, and the payload-parity report all passed. A clean Tahoe
build resolved all 959 undefined symbols against BootKC.

The installed candidate loaded as UUID
`20FBB20C-B016-3787-BDDC-9C9FB9D44872` with signed executable SHA-256
`fa7c29fca4ddaa6c5d58a1257f5410aec890f789af7faa182834eb7d3f20dfdf`
and AuxKC SHA-256
`fef87fd3f455724045bf658e2bdfd5c28b9f37c2d35e4c03be45c5a5dc4be445`.
After explicit saved-profile rejoin, capped uplink and reverse 240-second
gates each transferred 572 MiB at 20.0 Mbit/s with 240/240 concurrent ping
replies and 0.0% loss (mean RTT 4.131 ms and 6.912 ms respectively; reverse
sender had two retransmits). Hostapd retained an authorized station with zero
TX failures, QEMU remained running, the bounded guest fault filter had no
matching panic/WCL/AirportItlwm failure marker, and the bounded host filter had
no fatal VFIO/IOMMU/AER match.

The recovered reference consumes effective input dword `+0`, but does not
prove the complete public-carrier allocation size. No guessed private-setter
ioctl was issued, so this is explicitly not a claim of direct setter runtime
invocation or Apple valid-input return-code parity. Radio OFF/ON remains
excluded because the restored bit-identical A2DF baseline reproduces the same
separate WCL lifecycle panic. Full immutable runtime evidence is under
`/home/dima/Projects/aiam/runtime-captures/itlwm-battery-powersave-config-quarantine-20260713/`.

## FIX_CANDIDATE — MWS WiFi Type-7 bitmap false-success quarantine

- anomaly_id: `CR-479-MWS-WIFI-TYPE7-BITMAP-FALSE-SUCCESS-P0`
- status: `FIX_CANDIDATE`
- symptom: a non-null `MWS_WIFI_TYPE_7_BITMAP_WIFI_ENH` request reports
  success although the Intel port has no MWS WiFiType7 firmware-policy owner
  or transport.
- expected system behavior: recovered 25C56 Infra slot-[649] at
  `0x1000195ec` calls Core `0x100140e68`; the Core setter consumes nine
  dwords at `+0..+0x20`, stores them at `+0x2978..+0x2998`, and dispatches
  `+0x620`. Its proven vtable target `0x100122580` creates the 36-byte `mws`
  opcode-6 bitmap payload and sends it through Commander IOVAR work.
- actual behavior: local
  `AirportItlwmSkywalkInterface::setMWS_WIFI_TYPE_7_BITMAP_WIFI_ENH` preserves
  the null guard, then writes only `cachedMwsWifiType7Bitmap` and returns
  success. The cache has no consumer; scoped local inventory finds no MWS
  iovar, WiFiType7 owner, callback, or equivalent Commander transport.
- exact divergence point: local cache-only setter versus the recovered
  wrapper/Core/vtable/terminal chain recorded in
  `docs/reference/CR-479-mws-wifi-type7-bitmap-quarantine-20260713.md`.
- evidence from static recovery: the exact reference image has SHA-256
  `4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`;
  raw recovery establishes the null/effective-nine-dword/vtable/firmware
  behavior. Scoped local inspection proves the cache-only success path and
  backend absence.
- confirmed deviation: callers are told a Type-7 coexistence policy was
  accepted while the local port cannot reproduce the corresponding firmware
  work.
- fix justification path: `REFERENCE_ALIGNMENT_SAFETY_QUARANTINE`.
- why this is root cause and not just correlation: this does not explain the
  independent WCL lifecycle panic. It is a direct false-capability boundary:
  local success follows a dead cache write while the reference sends real MWS
  IOVAR policy work.
- proposed fix: retain the recovered null error, reject all non-null requests
  with `kIOReturnUnsupported` before mutation, and remove only the dead cache
  plus its two initializers.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp` and `.hpp`;
  - dedicated MWS WiFi Type-7 quarantine report, reference note, and this
    analysis record.
- forbidden alternative fixes considered and rejected:
  - fabricating the opaque 36-byte `mws` payload or issuing a guessed IOVAR;
  - treating generic Intel coexistence code as Apple's MWS implementation;
  - changing PM, radio state, `0x37`, WCL, association, or generic commander
    semantics;
  - claiming `kIOReturnUnsupported` is Apple's valid-input result;
  - changing adjacent MWS selectors without their own terminal trace;
  - using the baseline-shared radio OFF/ON fault as a candidate gate.
- verification plan: deterministic source guard plus existing payload checks,
  clean Tahoe build and symbol resolution, AuxKC install/load identity,
  saved-profile rejoin, bounded bidirectional traffic/ping, and guest/host
  fault filters. The runtime report will distinguish regression coverage from
  direct private-setter execution.

## VERIFIED RESULT — MWS COEX bitmap false-success quarantine

The declared verification plan completed. The compiled source-code delta
(build inputs only) has SHA-256
`dab239a2fb5648b8dc62195ea019cd56f550e32637d7b0dcda21a93517c7222`.
`git diff --cached --check`, the 31-contract Tahoe payload-builder test, the
four-invariant MWS COEX quarantine report, retained RFEM/disable-OCL/Type-7/
battery/LMTPC/TX-power-cap reports, and the payload-parity report all passed.
A clean Tahoe build resolved all 959 undefined symbols against BootKC.

The installed candidate loaded as UUID
`4B935CB8-7C94-387E-9BFD-A8C1BDA75654` with signed executable SHA-256
`46b5bfd06d2560bdc514751afdc676c3fba40bb5d3e8bde3146ce47198a645c6`
and AuxKC SHA-256
`619cbc9b4d9453dc754f2f70752add1736a77f826661e4b5bdc45e2c80c30568`.
After explicit saved-profile rejoin, capped uplink and reverse 240-second
gates each transferred 572 MiB at 20.0 Mbit/s with 240/240 concurrent ping
replies and 0.0% loss (mean RTT 4.344 ms and 6.591 ms respectively; reverse
sender had two retransmits). Hostapd retained an authorized, authenticated,
associated station with zero TX failures, QEMU remained running, the bounded
guest fault filter had no matching panic/WCL/AirportItlwm marker, and the
bounded host filter had no fatal VFIO/IOMMU/AER match.

The recovered reference consumes nine effective dwords, but does not prove the
complete public-carrier allocation size. No guessed opaque carrier or private
setter ioctl was issued, so this is explicitly not a claim of direct setter
runtime invocation or Apple valid-input return-code parity. The known
`networksetup` association string remains a false negative here; the actual AP
station state, IPv4 address, route, ping, and traffic gates are the connection
evidence. Radio OFF/ON remains excluded because the restored bit-identical
A2DF baseline reproduces the same separate WCL lifecycle panic. Full immutable
runtime evidence is under
`/home/dima/Projects/aiam/runtime-captures/itlwm-mws-coex-bitmap-quarantine-20260713/`.

## VERIFIED RESULT — MWS RFEM configuration false-success quarantine

The declared verification plan completed. The compiled source-code delta
(build inputs only) has SHA-256
`13212b03d59d3cf307c3ea4ce23ffe06ede49e7b5e92cf811532ba0fb61dfe4c`.
`git diff --cached --check`, the 31-contract Tahoe payload-builder test, the
four-invariant MWS RFEM quarantine report, retained disable-OCL/Type-7/battery/
LMTPC/TX-power-cap reports, and the payload-parity report all passed. A clean
Tahoe build resolved all 959 undefined symbols against BootKC.

The installed candidate loaded as UUID
`7CAD3F3E-618A-3569-AC1F-D40D03E42DFB` with signed executable SHA-256
`5bf92283b84d73183cb2ef66764cd5656ed4f9ea9d3e400b57ce599bf0164dad`
and AuxKC SHA-256
`86e818380bc6b7028810b0085a1577f253da4fb660909bb926214ac377adcd3b`.
After explicit saved-profile rejoin, capped uplink and reverse 240-second
gates each transferred 572 MiB at 20.0 Mbit/s with 240/240 concurrent ping
replies and 0.0% loss (mean RTT 4.330 ms and 5.954 ms respectively; reverse
sender had one retransmit). Hostapd retained an authorized, authenticated,
associated station with zero TX failures, QEMU remained running, the bounded
guest fault filter had no matching panic/WCL/AirportItlwm marker, and the
bounded host filter had no fatal VFIO/IOMMU/AER match.

The recovered reference consumes ten effective dwords, but does not prove the
complete public-carrier allocation size. No guessed opaque carrier or private
setter ioctl was issued, so this is explicitly not a claim of direct setter
runtime invocation or Apple valid-input return-code parity. The known
`networksetup` association string remains a false negative here; the actual AP
station state, IPv4 address, route, ping, and traffic gates are the connection
evidence. Radio OFF/ON remains excluded because the restored bit-identical
A2DF baseline reproduces the same separate WCL lifecycle panic. Full immutable
runtime evidence is under
`/home/dima/Projects/aiam/runtime-captures/itlwm-mws-rfem-config-quarantine-20260713/`.

## FIX_CANDIDATE — MWS COEX bitmap false-success quarantine

- anomaly_id: `CR-479-MWS-COEX-BITMAP-FALSE-SUCCESS-P0`
- status: `FIX_CANDIDATE`
- symptom: a non-null `MWS_COEX_BITMAP_WIFI_ENH` request reports success
  although the Intel port has no corresponding MWS firmware-policy owner or
  transport.
- expected system behavior: recovered 25C56 Infra slot-[650] at
  `0x100019624` calls Core `0x100140f5a`; the Core setter consumes nine
  dwords at `+0..+0x20`, stores them at `+0x292c..+0x294c`, and dispatches
  `+0x610`. Its proven vtable target `0x100122074` creates the 36-byte MWS
  command-2 bitmap payload and returns real Commander IOVAR status.
- actual behavior: local
  `AirportItlwmSkywalkInterface::setMWS_COEX_BITMAP_WIFI_ENH` preserves the
  null guard, then writes only `cachedMwsCoexBitmap` and returns success. The
  cache has no consumer; scoped local inventory finds no MWS iovar, COEX bitmap
  owner, callback, or equivalent Commander transport.
- exact divergence point: local cache-only setter versus the recovered
  wrapper/Core/vtable/terminal chain recorded in
  `docs/reference/CR-479-mws-coex-bitmap-quarantine-20260713.md`.
- evidence from static recovery: the exact reference image has SHA-256
  `4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`;
  raw recovery establishes the null/effective-nine-dword/vtable/firmware
  behavior. Scoped local inspection proves the cache-only success path and
  backend absence.
- confirmed deviation: callers are told a COEX bitmap policy was accepted
  while the local port cannot reproduce the corresponding firmware work.
- fix justification path: `REFERENCE_ALIGNMENT_SAFETY_QUARANTINE`.
- why this is root cause and not just correlation: this does not explain the
  independent WCL lifecycle panic. It is a direct false-capability boundary:
  local success follows a dead cache write while the reference sends real MWS
  IOVAR policy work.
- proposed fix: retain the recovered null error, reject all non-null requests
  with `kIOReturnUnsupported` before mutation, and remove only the dead cache
  plus its two initializers.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp` and `.hpp`;
  - dedicated COEX bitmap quarantine report, reference note, and this analysis
    record.
- forbidden alternative fixes considered and rejected:
  - fabricating the opaque MWS command-2 payload or issuing a guessed IOVAR;
  - treating generic Intel coexistence code as Apple's MWS implementation;
  - changing PM, radio state, `0x37`, WCL, association, or generic commander
    semantics;
  - claiming `kIOReturnUnsupported` is Apple's valid-input result;
  - changing adjacent MWS selectors without their own terminal trace;
  - using the baseline-shared radio OFF/ON fault as a candidate gate.
- verification plan: deterministic source guard plus existing payload checks,
  clean Tahoe build and symbol resolution, AuxKC install/load identity,
  saved-profile rejoin, bounded bidirectional traffic/ping, and guest/host
  fault filters. The runtime report will distinguish regression coverage from
  direct private-setter execution.

## VERIFIED RESULT — MWS disable-OCL bitmap false-success quarantine

The declared verification plan completed. The compiled source-code delta
(build inputs only) has SHA-256
`1d8725e0702f0e9d7938282828a753e8ce2038faab82241b93195c7ea77c2308`.
`git diff --cached --check`, the 31-contract Tahoe payload-builder test, the
four-invariant MWS disable-OCL quarantine report, retained Type-7/battery/LMTPC/
TX-power-cap reports, and the payload-parity report all passed. A clean Tahoe
build resolved all 959 undefined symbols against BootKC.

The installed candidate loaded as UUID
`EBC16885-101F-3FD8-BC2E-649D266186F0` with signed executable SHA-256
`1f225cbc6f0aa80abb17d21c57f094df172afe87ab7949dd81e7197803d0386c`
and AuxKC SHA-256
`4f7e65b895ff4f60354d1db28dc25fe664feb862d6186af0b494ad7c84e9814d`.
After explicit saved-profile rejoin, capped uplink and reverse 240-second
gates each transferred 572 MiB at 20.0 Mbit/s with 240/240 concurrent ping
replies and 0.0% loss (mean RTT 4.031 ms and 6.173 ms respectively; reverse
sender had three retransmits). Hostapd retained an authorized, authenticated,
associated station with zero TX failures, QEMU remained running, the bounded
guest fault filter had no matching panic/WCL/AirportItlwm marker, and the
bounded host filter had no fatal VFIO/IOMMU/AER match.

The recovered reference consumes nine effective dwords, but does not prove the
complete public-carrier allocation size. No guessed opaque carrier or private
setter ioctl was issued, so this is explicitly not a claim of direct setter
runtime invocation or Apple valid-input return-code parity. The known
`networksetup` association string remains a false negative here; the actual AP
station state, IPv4 address, route, ping, and traffic gates are the connection
evidence. Radio OFF/ON remains excluded because the restored bit-identical
A2DF baseline reproduces the same separate WCL lifecycle panic. Full immutable
runtime evidence is under
`/home/dima/Projects/aiam/runtime-captures/itlwm-mws-disable-ocl-bitmap-quarantine-20260713/`.

## FIX_CANDIDATE — MWS RFEM configuration false-success quarantine

- anomaly_id: `CR-479-MWS-RFEM-CONFIG-FALSE-SUCCESS-P0`
- status: `FIX_CANDIDATE`
- symptom: a non-null `MWS_RFEM_CONFIG_WIFI_ENH` request reports success
  although the Intel port has no corresponding MWS RFEM firmware-policy owner
  or transport.
- expected system behavior: recovered 25C56 Infra slot-[652] at
  `0x100019694` calls Core `0x10014131e`; the Core setter consumes ten dwords
  at `+0..+0x24`, stores the RF-band bitmap first and the channel bitmaps at
  `+0x292c..+0x2950`, then dispatches `+0x640`. Its proven vtable target
  `0x100122ce6` creates the 36-byte MWS command-8 bitmap payload and sends it
  through Commander IOVAR work.
- actual behavior: local
  `AirportItlwmSkywalkInterface::setMWS_RFEM_CONFIG_WIFI_ENH` preserves the
  null guard, then writes only `cachedMwsRfemConfig` and returns success. The
  cache has no consumer; scoped local inventory finds no MWS iovar, RFEM owner,
  callback, or equivalent Commander transport.
- exact divergence point: local cache-only setter versus the recovered
  wrapper/Core/vtable/terminal chain recorded in
  `docs/reference/CR-479-mws-rfem-config-quarantine-20260713.md`.
- evidence from static recovery: the exact reference image has SHA-256
  `4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`;
  raw recovery establishes the null/effective-ten-dword/vtable/firmware
  behavior. Scoped local inspection proves the cache-only success path and
  backend absence.
- confirmed deviation: callers are told an RFEM coexistence policy was
  accepted while the local port cannot reproduce the corresponding firmware
  work.
- fix justification path: `REFERENCE_ALIGNMENT_SAFETY_QUARANTINE`.
- why this is root cause and not just correlation: this does not explain the
  independent WCL lifecycle panic. It is a direct false-capability boundary:
  local success follows a dead cache write while the reference sends real MWS
  IOVAR policy work.
- proposed fix: retain the recovered null error, reject all non-null requests
  with `kIOReturnUnsupported` before mutation, and remove only the dead cache
  plus its two initializers.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp` and `.hpp`;
  - dedicated RFEM quarantine report, reference note, and this analysis record.
- forbidden alternative fixes considered and rejected:
  - fabricating the opaque MWS command-8 payload or issuing a guessed IOVAR;
  - treating generic Intel coexistence code as Apple's MWS implementation;
  - changing PM, radio state, `0x37`, WCL, association, or generic commander
    semantics;
  - claiming `kIOReturnUnsupported` is Apple's valid-input result;
  - changing adjacent MWS selectors without their own terminal trace;
  - using the baseline-shared radio OFF/ON fault as a candidate gate.
- verification plan: deterministic source guard plus existing payload checks,
  clean Tahoe build and symbol resolution, AuxKC install/load identity,
  saved-profile rejoin, bounded bidirectional traffic/ping, and guest/host
  fault filters. The runtime report will distinguish regression coverage from
  direct private-setter execution.

## VERIFIED RESULT — MWS WiFi Type-7 bitmap false-success quarantine

The declared verification plan completed. The compiled source-code delta
(build inputs only) has SHA-256
`2ab3b122fc4829f42717b3d57f93ff68fda84cfd83a15f1ea01530f99d302ccd`.
`git diff --cached --check`, the 31-contract Tahoe payload-builder test, the
four-invariant MWS WiFi Type-7 quarantine report, retained battery/LMTPC/TX
power-cap reports, and the payload-parity report all passed. A clean Tahoe
build resolved all 959 undefined symbols against BootKC.

The installed candidate loaded as UUID
`26D0587F-7B95-3CEE-8E8A-EFA5CBFD6A28` with signed executable SHA-256
`d4a1cbac9f04a1093536102db7edb7b536a75f6b29f3e910b1874930c9417f8e`
and AuxKC SHA-256
`3cac91b4e347c74f892078fcb38e6922c37af22f4a3d3ba90e65415679e53007`.
After explicit saved-profile rejoin, capped uplink and reverse 240-second
gates each transferred 572 MiB at 20.0 Mbit/s with 240/240 concurrent ping
replies and 0.0% loss (mean RTT 4.308 ms and 6.404 ms respectively; reverse
sender had three retransmits). Hostapd retained an authorized, authenticated,
associated station with zero TX failures, QEMU remained running, the bounded
guest fault filter had no matching panic/WCL/AirportItlwm marker, and the
bounded host filter had no fatal VFIO/IOMMU/AER match.

The recovered reference consumes nine effective dwords, but does not prove the
complete public-carrier allocation size. No guessed opaque carrier or private
setter ioctl was issued, so this is explicitly not a claim of direct setter
runtime invocation or Apple valid-input return-code parity. The known
`networksetup` association string remains a false negative here; the actual AP
station state, IPv4 address, route, ping, and traffic gates are the connection
evidence. Radio OFF/ON remains excluded because the restored bit-identical
A2DF baseline reproduces the same separate WCL lifecycle panic. Full immutable
runtime evidence is under
`/home/dima/Projects/aiam/runtime-captures/itlwm-mws-wifi-type7-bitmap-quarantine-20260713/`.

## FIX_CANDIDATE — MWS disable-OCL bitmap false-success quarantine

- anomaly_id: `CR-479-MWS-DISABLE-OCL-BITMAP-FALSE-SUCCESS-P0`
- status: `FIX_CANDIDATE`
- symptom: a non-null `MWS_DISABLE_OCL_BITMAP_WIFI_ENH` request reports
  success although the Intel port has no corresponding MWS firmware-policy
  owner or transport.
- expected system behavior: recovered 25C56 Infra slot-[651] at
  `0x10001965c` calls Core `0x10014113c`; the Core setter consumes nine
  dwords at `+0..+0x20`, stores them at `+0x2954..+0x2974`, and dispatches
  `+0x618`. Its proven vtable target `0x1001222fa` creates the MWS
  subcommand-3 bitmap payload and sends it through Commander IOVAR work.
- actual behavior: local
  `AirportItlwmSkywalkInterface::setMWS_DISABLE_OCL_BITMAP_WIFI_ENH` preserves
  the null guard, then writes only `cachedMwsDisableOclBitmap` and returns
  success. The cache has no consumer; scoped local inventory finds no MWS
  iovar, OCL coexistence owner, callback, or equivalent Commander transport.
- exact divergence point: local cache-only setter versus the recovered
  wrapper/Core/vtable/terminal chain recorded in
  `docs/reference/CR-479-mws-disable-ocl-bitmap-quarantine-20260713.md`.
- evidence from static recovery: the exact reference image has SHA-256
  `4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`;
  raw recovery establishes the null/effective-nine-dword/vtable/firmware
  behavior. Scoped local inspection proves the cache-only success path and
  backend absence.
- confirmed deviation: callers are told an OCL coexistence policy was
  accepted while the local port cannot reproduce the corresponding firmware
  work.
- fix justification path: `REFERENCE_ALIGNMENT_SAFETY_QUARANTINE`.
- why this is root cause and not just correlation: this does not explain the
  independent WCL lifecycle panic. It is a direct false-capability boundary:
  local success follows a dead cache write while the reference sends real MWS
  IOVAR policy work.
- proposed fix: retain the recovered null error, reject all non-null requests
  with `kIOReturnUnsupported` before mutation, and remove only the dead cache
  plus its two initializers.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp` and `.hpp`;
  - dedicated disable-OCL quarantine report, reference note, and this analysis
    record.
- forbidden alternative fixes considered and rejected:
  - fabricating the opaque MWS subcommand-3 payload or issuing a guessed IOVAR;
  - treating generic Intel coexistence code as Apple's MWS implementation;
  - changing PM, radio state, `0x37`, WCL, association, or generic commander
    semantics;
  - claiming `kIOReturnUnsupported` is Apple's valid-input result;
  - changing adjacent MWS selectors without their own terminal trace;
  - using the baseline-shared radio OFF/ON fault as a candidate gate.
- verification plan: deterministic source guard plus existing payload checks,
  clean Tahoe build and symbol resolution, AuxKC install/load identity,
  saved-profile rejoin, bounded bidirectional traffic/ping, and guest/host
  fault filters. The runtime report will distinguish regression coverage from
  direct private-setter execution.

## FIX_CANDIDATE — MWS association-protection bitmap false-success quarantine

- anomaly_id: `CR-479-MWS-ASSOC-PROTECTION-BITMAP-FALSE-SUCCESS-P0`
- status: `FIX_CANDIDATE`
- symptom: a non-null `MWS_ASSOC_PROTECTION_BITMAP_WIFI_ENH` request reports
  success although the Intel port has no corresponding MWS firmware-policy
  owner or transport.
- expected system behavior: recovered 25C56 Infra slot-[653] at
  `0x1000196cc` calls Core `0x100141526`; the Core setter consumes nine
  dwords at `+0..+0x20`, stores them at `+0x292c..+0x294c`, and dispatches
  `+0x648`. With the Core vptr address point, its current raw vtable entry is
  `0x1003a1730`, image-local `0x100122fa6`,
  `setWiFiAssocProtectionConfigBitmapWiFiEnh`, which creates a 36-byte MWS
  command-13 payload with nine low-16-bit bitmap fields, sends it through
  Commander IOVAR work, and preserves status.
- actual behavior: local
  `AirportItlwmSkywalkInterface::setMWS_ASSOC_PROTECTION_BITMAP_WIFI_ENH`
  preserves the null guard, then writes only
  `cachedMwsAssocProtectionBitmap` and returns success. The cache has no
  consumer; scoped local inventory finds no MWS iovar, association-protection
  terminal owner, callback, or equivalent Commander transport.
- exact divergence point: local cache-only setter versus the recovered
  wrapper/Core/vtable/terminal chain recorded in
  `docs/reference/CR-479-mws-assoc-protection-bitmap-quarantine-20260713.md`.
- evidence from static recovery: the exact reference image has SHA-256
  `4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`;
  raw recovery establishes the current null/effective-nine-dword/vtable/
  firmware behavior. Scoped local inspection proves the cache-only success
  path and backend absence.
- confirmed deviation: callers are told an association-protection policy was
  accepted while the local port cannot reproduce the corresponding firmware
  work.
- fix justification path: `REFERENCE_ALIGNMENT_SAFETY_QUARANTINE`.
- why this is root cause and not just correlation: this does not explain the
  independent WCL lifecycle panic. It is a direct false-capability boundary:
  local success follows a dead cache write while the reference sends real MWS
  IOVAR policy work.
- proposed fix: retain the recovered null error, reject all non-null requests
  with `kIOReturnUnsupported` before mutation, and remove only the dead cache
  plus its two initializers.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp` and `.hpp`;
  - dedicated association-protection quarantine report, reference note, and
    this analysis record.
- forbidden alternative fixes considered and rejected:
  - fabricating the opaque MWS command-13 payload or issuing a guessed IOVAR;
  - treating the adjacent Condition-ID terminal as the current vtable target;
  - treating generic Intel coexistence code as Apple's MWS implementation;
  - changing PM, radio state, `0x37`, WCL, association, or generic commander
    semantics;
  - claiming `kIOReturnUnsupported` is Apple's valid-input result;
  - changing adjacent MWS selectors without their own terminal trace;
  - using the baseline-shared radio OFF/ON fault as a candidate gate.
- verification plan: deterministic source guard plus existing payload checks,
  clean Tahoe build and symbol resolution, AuxKC install/load identity,
  saved-profile rejoin, bounded bidirectional traffic/ping, and guest/host
  fault filters. The runtime report will distinguish regression coverage from
  direct private-setter execution.

## VERIFIED RESULT — MWS association-protection bitmap false-success quarantine

The declared verification plan completed. The compiled source-code delta
(build inputs only) has SHA-256
`d0aa508312365711aaf8f113d28486fe27c0ff961f33672c8aae1ccaebf17bd5`.
`git diff --cached --check`, the 31-contract Tahoe payload-builder test, the
four-invariant MWS association-protection quarantine report, retained COEX/
RFEM/disable-OCL/Type-7/battery/LMTPC/TX-power-cap reports, and the
payload-parity report all passed. A clean Tahoe build resolved all 959
undefined symbols against BootKC.

The installed candidate loaded as UUID
`42EA39AB-2082-39DC-8431-BE6928524AA1` with signed executable SHA-256
`be44da47f3ebcaee02c790ac9cd360a6e2e5a81c6aa640b9c56d6297762dbf73`
and AuxKC SHA-256
`e299646e25f9f5a265b239282db8fd93b572de4a7d3884e2106cb905b988f26b`.
After explicit saved-profile rejoin, capped uplink and reverse 240-second
gates each transferred 572 MiB at 20.0 Mbit/s with 240/240 concurrent ping
replies and 0.0% loss (mean RTT 5.244 ms and 6.198 ms respectively; reverse
sender had one retransmit). Hostapd retained an authorized, authenticated,
associated station with zero TX failures, QEMU remained running, the bounded
guest fault filter had no matching panic/WCL/AirportItlwm marker, and the
bounded host filter had no fatal VFIO/IOMMU/AER match.

The recovered reference consumes nine effective dwords, but does not prove the
complete public-carrier allocation size. No guessed opaque carrier or private
setter ioctl was issued, so this is explicitly not a claim of direct setter
runtime invocation or Apple valid-input return-code parity. The known
`networksetup` association string remains a false negative here; the actual AP
station state, IPv4 address, route, ping, and traffic gates are the connection
evidence. Radio OFF/ON remains excluded because the restored bit-identical
A2DF baseline reproduces the same separate WCL lifecycle panic. Full immutable
runtime evidence is under
`/home/dima/Projects/aiam/runtime-captures/itlwm-mws-assoc-protection-bitmap-quarantine-20260713/`.

## FIX_CANDIDATE — MWS scan-frequency false-success quarantine

- anomaly_id: `CR-479-MWS-SCAN-FREQ-FALSE-SUCCESS-P0`
- status: `FIX_CANDIDATE`
- symptom: a non-null `MWS_SCAN_FREQ_WIFI_ENH` request reports success although
  the Intel port has no corresponding MWS firmware-policy owner or transport.
- expected system behavior: recovered 25C56 Infra slot-[654] at
  `0x100019704` calls Core `0x100141708`; the Core setter reads ten dwords in
  reference order (`+0x24`, then `+0..+0x20`), stores them at
  `+0x299c..+0x29c0`, and dispatches `+0x628`. With the Core vptr address
  point, raw entry `0x1003a1710` selects image-local `0x100122806`,
  `setWiFiType4BlankingBitmapsWiFiEnh`, which creates a 40-byte MWS
  command-7 payload with ten low-16-bit fields, sends it through Commander
  IOVAR work, and preserves status.
- actual behavior: local
  `AirportItlwmSkywalkInterface::setMWS_SCAN_FREQ_WIFI_ENH` preserves the null
  guard, then writes only `cachedMwsScanFreq` and returns success. The cache
  has no consumer; scoped local inventory finds no MWS iovar, Type-4 blanking
  terminal owner, callback, or equivalent Commander transport.
- exact divergence point: local cache-only setter versus the recovered
  wrapper/Core/vtable/terminal chain recorded in
  `docs/reference/CR-479-mws-scan-freq-quarantine-20260713.md`.
- evidence from static recovery: the exact reference image has SHA-256
  `4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`;
  raw recovery establishes the current null/reordered-ten-dword/vtable/
  firmware behavior. Scoped local inspection proves the cache-only success
  path and backend absence.
- confirmed deviation: callers are told a scan-frequency coexistence policy
  was accepted while the local port cannot reproduce the corresponding
  firmware work.
- fix justification path: `REFERENCE_ALIGNMENT_SAFETY_QUARANTINE`.
- why this is root cause and not just correlation: this does not explain the
  independent WCL lifecycle panic. It is a direct false-capability boundary:
  local success follows a dead cache write while the reference sends real MWS
  IOVAR policy work.
- proposed fix: retain the recovered null error, reject all non-null requests
  with `kIOReturnUnsupported` before mutation, and remove only the dead cache
  plus its two initializers.
- files/functions to modify:
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp` and `.hpp`;
  - dedicated scan-frequency quarantine report, reference note, and this
    analysis record.
- forbidden alternative fixes considered and rejected:
  - fabricating the opaque MWS command-7 payload or issuing a guessed IOVAR;
  - treating generic Intel coexistence code as Apple's MWS implementation;
  - changing scan mode, Condition-ID, antenna, PM, radio state, `0x37`, WCL,
    association, or generic commander semantics;
  - claiming `kIOReturnUnsupported` is Apple's valid-input result;
  - changing adjacent MWS selectors without their own terminal trace;
  - using the baseline-shared radio OFF/ON fault as a candidate gate.
- verification plan: deterministic source guard plus existing payload checks,
  clean Tahoe build and symbol resolution, AuxKC install/load identity,
  saved-profile rejoin, bounded bidirectional traffic/ping, and guest/host
  fault filters. The runtime report will distinguish regression coverage from
  direct private-setter execution.

## VERIFIED RESULT — MWS scan-frequency false-success quarantine

The declared verification plan completed. The compiled source-code delta
(build inputs only) has SHA-256
`cd537e7c0a7cea426be010a503ed27b5ba8de0e29b845d8f37690134c4785cf1`.
`git diff --cached --check`, the 31-contract Tahoe payload-builder test, the
four-invariant MWS scan-frequency quarantine report, retained association-
protection/COEX/RFEM/disable-OCL/Type-7/battery/LMTPC/TX-power-cap reports,
and the payload-parity report all passed. A clean Tahoe build resolved all 959
undefined symbols against BootKC.

The installed candidate loaded as UUID
`A953ED22-9781-3096-8C81-BA8B9EEBB915` with signed executable SHA-256
`8294ef32f8381a388c12d3b6176b66a10040d4a0c8d29e9b5309426869f795c1`
and AuxKC SHA-256
`4b582fc12c3096f9b9a4ff6f4dbf3bf5e50d7dec412e238f3fa871fb21e5756a`.
After explicit saved-profile rejoin, capped uplink and reverse 240-second
gates each transferred 572 MiB at 20.0 Mbit/s with 240/240 concurrent ping
replies and 0.0% loss (mean RTT 4.229 ms and 6.150 ms respectively; reverse
sender had one retransmit). Hostapd retained an authorized, authenticated,
associated station with zero TX failures, QEMU remained running, the bounded
guest fault filter had no matching panic/WCL/AirportItlwm marker, and the
bounded host filter had no fatal VFIO/IOMMU/AER match.

The recovered reference consumes ten effective dwords in a specific reorder,
but does not prove the complete public-carrier allocation size. No guessed
opaque carrier or private setter ioctl was issued, so this is explicitly not a
claim of direct setter runtime invocation or Apple valid-input return-code
parity. The known `networksetup` association string remains a false negative
here; the actual AP station state, IPv4 address, route, ping, and traffic gates
are the connection evidence. Radio OFF/ON remains excluded because the
restored bit-identical A2DF baseline reproduces the same separate WCL lifecycle
panic. Full immutable runtime evidence is under
`/home/dima/Projects/aiam/runtime-captures/itlwm-mws-scan-freq-quarantine-20260713/`.

## FIX_CANDIDATE — MWS scan-frequency-mode false-success quarantine

- anomaly ID: public `MWS_SCAN_FREQ_MODE_WIFI_ENH` acknowledged an opaque
  carrier after only caching four reordered dwords despite no Intel-side MWS
  owner or transport.
- expected reference path: slot-[655] wrapper `0x10001973c` tail-dispatches
  to Core `0x100141910`, which reads raw `+0x24/+0x04/+0x08/+0x0c`, then
  calls vtable offset `+0x630`. Accounting for the Itanium vptr address
  point, raw entry `0x1003a1718` resolves to terminal `0x100122a9c`,
  which constructs the 40-byte `mws` command-`0x1018000`, subcommand-12
  policy and preserves its enqueue or synchronous transport status.
- actual local behavior: retain only pseudo-state
  `cachedMwsScanFreqMode`, report success, and provide neither the MWS
  iovar nor an equivalent owner, callback, or transport.
- proposed correction: preserve null-to-`kIOReturnBadArgumentTahoe`, reject
  non-null input with `kIOReturnUnsupported` before mutation, and delete
  only the dead cache plus its two reset sites.
- scope boundary: no inferred full opaque-carrier size, no direct setter
  invocation, no Apple valid-input return-code parity claim, and no mutation
  of scan-frequency, Condition-ID, antenna, PM, radio/WCL, association, or
  generic command semantics.
- verification plan: source report plus retained payload contracts, clean
  Tahoe build/load identity, saved-profile rejoin, bounded bidirectional
  traffic/ping, and bounded guest/host fault filters. Radio OFF/ON stays
  excluded due to the independently reproduced baseline WCL lifecycle panic.

## VERIFIED RESULT — MWS Condition-ID bitmap false-success quarantine

The declared verification plan completed. The compiled source-code delta
(build inputs only) has SHA-256
`78165d97457c2c653446141f2b8a5bcda30934a574a8a8bbe8937d0f0f3e959d`.
`git diff --cached --check`, the 31-contract Tahoe payload-builder test, the
four-invariant MWS Condition-ID bitmap quarantine report, retained
scan-frequency-mode/scan-frequency/association-protection/COEX/RFEM/
disable-OCL/Type-7/battery/LMTPC/TX-power-cap reports, and payload parity all
passed. A clean Tahoe build resolved all 959 undefined symbols against BootKC.

The installed candidate loaded as UUID
`F0DACC1F-0F16-3345-B609-D538C26D5F51` with signed executable SHA-256
`4483f2d87f8e951e31780cc908061553ddc7c3e6b84a49b2ea0a57f7f4f0a2aa`
and AuxKC SHA-256
`160083977c31f2334c8a6eb1aa7a1464aca58e303bfd2462f75666d07da9a2a3`.
After explicit saved-profile rejoin, capped uplink and reverse 240-second
gates each transferred 572 MiB at 20.0 Mbit/s with 240/240 concurrent ping
replies and 0.0% loss (mean RTT 3.447 ms and 7.389 ms respectively; reverse
sender had four retransmits). Hostapd retained an authorized, authenticated,
associated station with zero TX failures, QEMU remained running, the bounded
guest fault filter had no matching panic/WCL/AirportItlwm marker, and the
bounded host filter had no fatal VFIO/IOMMU/AER match.

The recovered reference establishes its count loop, record geometry, and
terminal transport, but does not prove the complete public-carrier allocation
size. No guessed opaque carrier or private setter ioctl was issued, so this is
explicitly not a claim of direct setter runtime invocation or Apple valid-input
return-code parity. The known `networksetup` association string remains a
false negative here; the actual AP station state, IPv4 address, route, ping,
and traffic gates are the connection evidence. Radio OFF/ON remains excluded
because the restored bit-identical A2DF baseline reproduces the same separate
WCL lifecycle panic. Full immutable runtime evidence is under
`/home/dima/Projects/aiam/runtime-captures/itlwm-mws-condition-id-bitmap-quarantine-20260713/`.

## FIX_CANDIDATE — MWS antenna-selection false-success quarantine

- anomaly ID: public `MWS_ANTENNA_SELECTION_WIFI_ENH` acknowledged an opaque
  carrier after only caching nine u16 fields despite no Intel-side MWS owner
  or transport.
- expected reference path: slot-[657] wrapper `0x1000197ac` follows the
  Core pointer to `0x100141cbc`, which reads wifiBand at raw `+0x10` then
  eight selectors at raw `+0x00..+0x0e`, and tail-dispatches vtable offset
  `+0x588`. Accounting for the Itanium vptr address point, raw entry
  `0x1003a1670` resolves to terminal `0x10012351c`, which constructs the
  band-dependent `mws` command-`0x1018000`, subcommand-4 policy and
  preserves enqueue or synchronous transport status.
- actual local behavior: retain only pseudo-state
  `cachedMwsAntennaSelection`, report success, and provide neither the MWS
  iovar nor an equivalent owner, callback, or transport.
- proposed correction: preserve null-to-`kIOReturnBadArgumentTahoe`, reject
  non-null input with `kIOReturnUnsupported` before mutation, and delete
  only the dead cache plus its two reset sites.
- scope boundary: no inferred full opaque-carrier size, no direct setter
  invocation, no Apple valid-input return-code parity claim, and no mutation
  of Condition-ID, scan frequency/mode, PM, radio/WCL, association, or
  generic command semantics.
- verification plan: source report plus retained payload contracts, clean
  Tahoe build/load identity, saved-profile rejoin, bounded bidirectional
  traffic/ping, and bounded guest/host fault filters. Radio OFF/ON stays
  excluded due to the independently reproduced baseline WCL lifecycle panic.

## VERIFIED RESULT — MWS antenna-selection false-success quarantine

The declared verification plan completed. The compiled source-code delta
(build inputs only) has SHA-256
f4139e49384077739584129f9df192b191de6290c404e30bcb0178104ea0db5c.
The antenna-selection report, retained quarantine reports, Tahoe
payload-builder suite (31 contracts), payload parity, and staged whitespace
check passed. A clean Tahoe build resolved all 959 undefined symbols against
BootKC.

The installed candidate loaded as UUID
9FFA073C-79D0-323E-AEDC-B6F82936B284 with signed executable SHA-256
4c0ce478c86bdbfc84b1f7dadb8ed5126e6a70c1e8a844274a4fe4a1e9af4bee and
AuxKC SHA-256
e63b5e40797f85c64ad91cd8c9a9c9993e8e47959bcb85e9b096d3568478134f.
After explicit saved-profile rejoin, capped uplink and reverse 240-second
gates each transferred 572 MiB at 20.0 Mbit/s with 240/240 concurrent ping
replies and 0.0% loss (mean RTT 5.205 ms and 6.110 ms respectively; reverse
sender had two retransmits). Hostapd retained an authorized, authenticated,
associated station with zero TX failures, QEMU remained running, the bounded
guest fault filter had no matching panic/WCL/AirportItlwm marker, and the
bounded host filter had no fatal VFIO/IOMMU/AER match.

The recovered reference establishes nine effective u16 fields and
band-dependent terminal transport, but does not prove the complete
public-carrier allocation size. No guessed opaque carrier or private setter
ioctl was issued, so this is explicitly not a claim of direct setter runtime
invocation or Apple valid-input return-code parity. The known networksetup
association string remains a false negative; AP station state, IPv4, ping, and
traffic gates are the connection evidence. Radio OFF/ON remains excluded
because the restored bit-identical A2DF baseline reproduces the same separate
WCL lifecycle panic. Full immutable runtime evidence is under
/home/dima/Projects/aiam/runtime-captures/itlwm-mws-antenna-selection-quarantine-20260713/.

## VERIFIED RESULT — MWS scan-frequency-mode false-success quarantine

The declared verification plan completed. The compiled source-code delta
(build inputs only) has SHA-256
`1f6d408a8b571af626c700f56dd277798d3c191dbaf6a9074ca2ca93b47ad045`.
`git diff --cached --check`, the 31-contract Tahoe payload-builder test, the
four-invariant MWS scan-frequency-mode quarantine report, retained
scan-frequency/association-protection/COEX/RFEM/disable-OCL/Type-7/battery/
LMTPC/TX-power-cap reports, and payload parity all passed. A clean Tahoe
build resolved all 959 undefined symbols against BootKC.

The installed candidate loaded as UUID
`536F221F-4868-3B13-9B0A-E65B8055CCFF` with signed executable SHA-256
`a2d87cb79ccecbb41f4f9f295c1a6207d05e1af6eb8c2c356219fa35cec14dda`
and AuxKC SHA-256
`45575e0d64d15e47579d7a10864d874da665f783e59439253b311705a4167219`.
After explicit saved-profile rejoin, capped uplink and reverse 240-second
gates each transferred 572 MiB at 20.0 Mbit/s with 240/240 concurrent ping
replies and 0.0% loss (mean RTT 4.306 ms and 5.736 ms respectively; reverse
sender had three retransmits). Hostapd retained an authorized, authenticated,
associated station with zero TX failures, QEMU remained running, the bounded
guest fault filter had no matching panic/WCL/AirportItlwm marker, and the
bounded host filter had no fatal VFIO/IOMMU/AER match.

The recovered reference establishes the four effective reordered fields and
terminal transport, but does not prove the complete public-carrier allocation
size. No guessed opaque carrier or private setter ioctl was issued, so this is
explicitly not a claim of direct setter runtime invocation or Apple valid-input
return-code parity. The known `networksetup` association string remains a
false negative here; the actual AP station state, IPv4 address, route, ping,
and traffic gates are the connection evidence. Radio OFF/ON remains excluded
because the restored bit-identical A2DF baseline reproduces the same separate
WCL lifecycle panic. Full immutable runtime evidence is under
`/home/dima/Projects/aiam/runtime-captures/itlwm-mws-scan-freq-mode-quarantine-20260713/`.

## FIX_CANDIDATE — MWS Condition-ID bitmap false-success quarantine

- anomaly ID: public `MWS_CONDITION_ID_BITMAP_WIFI_ENH` retained only a
  clamped local cache and reported success despite no Intel-side MWS owner or
  transport.
- expected reference path: slot-[656] wrapper `0x100019774` dispatches to
  Core `0x100141a2e`, preserving null/count-zero rejection and looping each
  `0x28` condition record. It stages the condition ID and nine bitmap words,
  invokes vtable offset `+0x638`, and returns immediately on terminal error.
  Accounting for the Itanium vptr address point, raw entry `0x1003a1720`
  resolves to terminal `0x100123df8`, which constructs the `mws`
  command-`0x1018000`, subcommand-10 policy and preserves enqueue or
  synchronous transport status.
- actual local behavior: retain only
  `cachedMwsConditionIdConfig/cachedMwsConditionIdCount`, report success,
  and provide neither the MWS iovar nor an equivalent owner, callback, or
  transport.
- proposed correction: retain both
  `kIOReturnBadArgumentTahoe` guards, reject count-nonzero input with
  `kIOReturnUnsupported` before mutation, and remove only the three dead
  cache members plus their two reset sites.
- scope boundary: no inferred full opaque-carrier size, no direct setter
  invocation, no Apple valid-input return-code parity claim, and no mutation
  of antenna, scan frequency/mode, PM, radio/WCL, association, or generic
  command semantics.
- verification plan: source report plus retained payload contracts, clean
  Tahoe build/load identity, saved-profile rejoin, bounded bidirectional
  traffic/ping, and bounded guest/host fault filters. Radio OFF/ON stays
  excluded due to the independently reproduced baseline WCL lifecycle panic.

## FIX_CANDIDATE — IBSS mode false-success quarantine

- anomaly ID: public APPLE80211_IOC_IBSS_MODE selector 24 accepted a non-null
  network carrier after copying it only to dead local pseudo-state; the local
  STA-only port did not create the requested IBSS/ad-hoc network.
- expected reference path: on Tahoe 25C56, Infra wrapper 0x10001814c dispatches
  to AppleBCMWLANCore::setIBSS_MODE at 0x10011c94c. Its non-null path
  coordinates Proximity/NAN/NAN-data links and calls
  AppleBCMWLANJoinAdapter::createAdhocNetwork at 0x10003d7ea, preserving
  lifecycle status. The recovered BssManager lifecycle ties positive
  ad-hoc-created state to actual successful creator work.
- actual local behavior: copies mode, auth bounds, channel and SSID into seven
  cachedIbss fields, returns success, and has no consumer, creator, transport,
  IBSS net80211 mode, or Intel HAL backend.
- proposed correction: leave the existing local null rejection untouched,
  reject every non-null carrier with kIOReturnUnsupported before mutation, and
  delete only the seven dead cache members plus their two reset sites.
- scope boundary: no claim of Apple null-input or valid-input return-code
  parity, no full carrier-size inference, no direct carrier invocation, and no
  mutation of BssManager teardown, getOP_MODE, STA_ONLY/HAL, WCL, radio, PM,
  association, or generic command behavior.
- verification plan: deterministic source report, retained payload contracts,
  clean Tahoe build/load identity, saved-profile rejoin, bounded bidirectional
  traffic/ping, and bounded guest/host fault filters. Radio OFF/ON remains
  excluded due to the independently reproduced baseline WCL lifecycle panic.

## VERIFIED RESULT — IBSS mode false-success quarantine

The declared verification plan completed. The compiled source-code delta
(build inputs only) has SHA-256
f232dae4299d01ef298795e4d4b34a97a9311f8ccd3cee7259ebdc5f450515d3.
The IBSS-mode report, retained quarantine reports, Tahoe payload-builder suite
(31 contracts), payload parity, and staged whitespace check passed. A clean
Tahoe build resolved all 959 undefined symbols against BootKC.

The installed candidate loaded as UUID
9625D5EB-24E1-3C4F-BB34-8848100A0667 with executable SHA-256
528606c2f3862250a0d3448672d283740ed0bd6a7f43c778a61f1f4a4e6286f4 and
AuxKC SHA-256
a1171f3d24912a5b170f15cf1438a39712ca322857fd0ff6e29501a955386712.
After an explicit normal credentialed rejoin, capped uplink and reverse
240-second gates each transferred 572 MiB at 20.0 Mbit/s with 240/240
concurrent ping replies and 0.0% loss (mean RTT 5.036 ms and 5.939 ms
respectively; reverse sender had one retransmit). Hostapd retained an
authorized, authenticated, associated station with zero TX failures, QEMU
remained running, the bounded guest fault filter had no matching
panic/WCL/AirportItlwm marker, and the bounded host filter had no fatal
VFIO/IOMMU/AER match.

The recovered reference proves real ad-hoc/proximity/NAN lifecycle work and
createAdhocNetwork ownership, but does not establish a complete public-carrier
allocation or Apple valid-input return-code parity. No guessed carrier or
private setter ioctl was issued, so this is explicitly not a claim of direct
setter runtime invocation. The known networksetup association string remains a
false negative; AP station state, IPv4, gateway route, ping, and traffic gates
are the connection evidence. Radio OFF/ON remains excluded because the
restored bit-identical A2DF baseline reproduces the same separate WCL lifecycle
panic. Full immutable runtime evidence is under
/home/dima/Projects/aiam/runtime-captures/itlwm-ibss-mode-quarantine-20260713/.

## FIX_CANDIDATE — USB host notification false-success quarantine

- anomaly ID: public `USB_HOST_NOTIFICATION` accepted a non-null carrier,
  updated local owner/cache state, and reported success through a synthetic
  commander despite no Intel USB/coexistence hardware backend.
- expected reference path: Tahoe 25C56 Infra wrapper `0x100018890` dispatches
  virtual slot `+0x720` to Core `0x100120ae0`. The Core invokes hidden owner
  `+0x1510`/virtual `+0x170`, sends carrier `+0x0c` as the four-byte
  `asym_mit_ext_usb` IOVAR, conditionally sends carrier `+0x08` when it is at
  most one as `asym_mit_ext_usb_chg`, and preserves commander status.
- actual local behavior: `runSetUSBHostNotification` only updates a local
  registry and calls `dispatchTransport`, which immediately completes status
  zero. The public setter then mirrors three values into dead Skywalk cache;
  no local `asym_mit_ext_usb`, `runIOVarSet`, or Intel-equivalent owner exists.
- proposed correction: preserve the existing local null rejection, reject each
  non-null carrier with `kIOReturnUnsupported` before the synthetic path, and
  remove only the three dead Skywalk cache members and their two reset sites.
- scope boundary: no generic commander mutation, no guessed carrier or private
  setter invocation, no Apple null/valid-input return parity claim, and no
  MWS, BTCOEX, WCL, radio, power, or association change.
- verification plan: deterministic source report, retained payload contracts,
  clean Tahoe build/load identity, saved-profile rejoin, bounded bidirectional
  traffic/ping, and bounded guest/host fault filters. Radio OFF/ON remains
  excluded because the restored bit-identical A2DF baseline reproduces the
  separate WCL lifecycle panic.

## FIX_CANDIDATE — WCL associated-sleep false-success quarantine

- anomaly ID: public `setWCL_ASSOCIATED_SLEEP` accepted a non-null opaque
  carrier, copied a local `0x58`-byte cache, and returned success although no
  Intel power-management backend consumes that state.
- expected reference path: Tahoe 25C56 image SHA-256
  `4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`
  dispatches Infra wrapper `0x100019498` through virtual `+0x778` to Core
  `0x100142fce`.  The Core updates power-management state, reads the carrier
  byte at `+0x36`, and calls four `PowerStateAdapter` configurators through
  Core `+0x8c88`: beacon SOI, data SOI, excess-PM alert, and associated-sleep
  roam scanning.
- actual local behavior: the setter only copied the cache and set a flag; no
  local `PowerStateAdapter` or recovered configurator exists, and no consumer
  observed that pseudo-state.
- proposed correction: preserve the existing local null safety guard; return
  `kIOReturnUnsupported` for a non-null carrier before mutation; remove only
  the two dead cache members and their two reset groups.
- scope boundary: no Core power-management lift, no inferred complete carrier
  allocation, no guessed carrier or private setter invocation, and no Apple
  null or valid-input return-code parity claim.
- verification plan: deterministic source report, retained payload contracts,
  clean Tahoe build/load identity, saved-profile rejoin, bounded bidirectional
  traffic/ping, and focused bounded guest/host fault filters. Radio OFF/ON
  remains excluded because the restored bit-identical A2DF baseline reproduces
  the separate WCL lifecycle panic.

## VERIFIED RESULT — WCL associated-sleep false-success quarantine

The declared verification plan completed.  The compiled source-code delta
(`AirportItlwm/` and `include/` build inputs) has SHA-256
`3d6aaf0d98324506e79ffa5d1925b720e0dbaa9e7256281dc3100a5cf5ccfd37`.
The associated-sleep quarantine report, retained BCN-mute/IE/USB/BTCOEX
reports, payload parity, 31 payload-builder contracts, Python syntax check,
and staged whitespace check passed.  A clean Tahoe build resolved all
959 undefined symbols against BootKC.

The installed candidate loaded as UUID
`F854B410-0B11-3AA8-9E64-F61C22A75762` with executable SHA-256
`09c6b7f2bd5c479d0782d0a88167a2c397d7aff784e59b5e77858653513c5174` and
AuxKC SHA-256
`880d80783d609636ea522bfef3c8b7c7612747592205db5d0bf1c9e580139809`.
The informational codesign check reported an unsigned code object; loaded
identity is established by kmutil, UUID, and executable hash, not a signing
claim.  After an explicit normal credentialed rejoin, capped uplink and
reverse 240-second gates each transferred 572 MiB at 20.0 Mbit/s with
240/240 concurrent ping replies and 0.0% loss (mean RTT 3.323 ms and
5.815 ms; reverse sender had one retransmit).  Hostapd kept an
authorized/authenticated/associated station with zero TX failures and QEMU
remained running.  The focused bounded guest failure filter produced
`no_matching_guest_panic_wcl_airportitlwm_marker`; the bounded host filter
produced `no_recent_fatal_vfio_iommu_aer_match`.

The recovered reference proves PowerStateAdapter backend work, but does not
establish a complete public carrier allocation or Apple null/valid-input
return-code parity.  No guessed carrier or private setter ioctl was issued,
so this is explicitly not a claim of direct setter runtime invocation.  The
known networksetup association string remains a false negative; AP station
state, IPv4/gateway route, ping, and traffic gates are the connection
evidence.  Radio OFF/ON remains excluded because the restored bit-identical
A2DF baseline reproduces the separate WCL lifecycle panic.  Full immutable
runtime evidence is under
`/home/dima/Projects/aiam/runtime-captures/itlwm-wcl-associated-sleep-quarantine-20260713/`.

## FIX_CANDIDATE — WCL SOI false-success quarantine

- anomaly ID: public `setWCL_SOI_CONFIG` accepted a non-null opaque carrier,
  copied it into a local `0x40`-byte cache, and returned success although no
  Intel sleep-on-inactivity configuration backend consumes that state.
- expected reference path: Tahoe 25C56 image SHA-256
  `4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`
  dispatches Infra wrapper `0x1000194e4` through virtual `+0x780` to Core
  `0x100143182`.  The Core passes the base carrier and `+0x1c` portion to the
  `PowerStateAdapter` at `+0x8c88` for beacon-SOI and data-SOI configuration;
  observed descendant paths reach `bcn_li_bcn` and `pm2_sleep_ret` commander
  IOVAR operations.
- actual local behavior: the setter only copied a cache and set a flag; no
  local sleep adapter, matching configurator, or recovered IOVAR backend
  exists, and no consumer observed that pseudo-state.
- proposed correction: preserve the existing local null safety guard; return
  `kIOReturnUnsupported` for a non-null carrier before mutation; remove only
  the two dead cache members and their two reset groups.
- scope boundary: no power-management lift, no inferred complete carrier
  allocation, no guessed carrier or private setter invocation, and no Apple
  null or valid-input return-code parity claim.
- verification plan: deterministic source report, retained payload contracts,
  clean Tahoe build/load identity, saved-profile rejoin, bounded bidirectional
  traffic/ping, and focused bounded guest/host fault filters. Radio OFF/ON
  remains excluded because the restored bit-identical A2DF baseline reproduces
  the separate WCL lifecycle panic.

## VERIFIED RESULT — WCL SOI false-success quarantine

The declared verification plan completed.  The compiled source-code delta
(`AirportItlwm/` and `include/` build inputs) has SHA-256
`553b50681a52ed3d4a754460c11d9183c23664538bd472e2dfce947278c5a13e`.
The SOI quarantine report, retained associated-sleep/BCN-mute/IE/USB/BTCOEX
reports, payload parity, 31 payload-builder contracts, Python syntax check,
and staged whitespace check passed.  A clean Tahoe build resolved all
959 undefined symbols against BootKC.

The installed candidate loaded as UUID
`20F07E14-8B39-3453-A1CA-2B8283E4873A` with executable SHA-256
`07ca4d502df5c08ecd82b999d83ffbd66a1f05a3668884e7087b3af34ccf0443` and
AuxKC SHA-256
`c392c4cf7046f0484cf7cc4ebc1bfab3685ed8201336a092bab71ec67c71f595`.
The informational codesign check reported an unsigned code object; loaded
identity is established by kmutil, UUID, and executable hash, not a signing
claim.  After an explicit normal credentialed rejoin, capped uplink and
reverse 240-second gates each transferred 572 MiB at 20.0 Mbit/s with
240/240 concurrent ping replies and 0.0% loss (mean RTT 5.143 ms and
6.015 ms; reverse sender had zero retransmits).  Hostapd kept an
authorized/authenticated/associated station with zero TX failures and QEMU
remained running.  The focused bounded guest failure filter produced
`no_matching_guest_panic_wcl_airportitlwm_marker`; the bounded host filter
produced `no_recent_fatal_vfio_iommu_aer_match`.

The recovered reference proves PowerStateAdapter and commander work, but does
not establish a complete public carrier allocation or Apple null/valid-input
return-code parity.  No guessed carrier or private setter ioctl was issued,
so this is explicitly not a claim of direct setter runtime invocation.  The
known networksetup association string remains a false negative; AP station
state, IPv4/gateway route, ping, and traffic gates are the connection
evidence.  Radio OFF/ON remains excluded because the restored bit-identical
A2DF baseline reproduces the separate WCL lifecycle panic.  Full immutable
runtime evidence is under
`/home/dima/Projects/aiam/runtime-captures/itlwm-wcl-soi-quarantine-20260713/`.

## FIX_CANDIDATE — OS eligibility false-success quarantine

- anomaly ID: public `setOS_ELIGIBILITY` accepted a non-null carrier, copied a
  dword into a local cache, and returned success although no Intel
  aggressive-EDCA policy backend consumes that state.
- expected reference path: Tahoe 25C56 image SHA-256
  `4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`
  dispatches Infra wrapper `0x100019830` through virtual `+0x7d0` to Core
  `0x100143ed6`.  When eligibility bit 0 changes while the commander is awake,
  Core invokes NetAdapter `+0x15e0` / `0x100014cc8`
  `configureAggressiveEDCA`; its recovered path sends `wme_ac_sta` and updates
  the short retry limit before Core stores the carrier word.
- actual local behavior: the setter only copied a dword cache; no local
  aggressive-EDCA configurator, `wme_ac_sta` commander path, or retry-limit
  backend exists, and no consumer observed that pseudo-state.
- proposed correction: preserve the existing local null safety guard; return
  `kIOReturnUnsupported` for a non-null carrier before mutation; remove only
  the dead cache member and its two reset sites.
- scope boundary: no NetAdapter or power-management lift, no inferred complete
  carrier allocation, no guessed carrier or private setter invocation, and no
  Apple null or valid-input return-code parity claim.
- verification plan: deterministic source report, retained payload contracts,
  clean Tahoe build/load identity, saved-profile rejoin, bounded bidirectional
  traffic/ping, and focused bounded guest/host fault filters. Radio OFF/ON
  remains excluded because the restored bit-identical A2DF baseline reproduces
  the separate WCL lifecycle panic.

## VERIFIED RESULT — OS eligibility false-success quarantine

The declared verification plan completed. The compiled source-code delta
(build-input `AirportItlwm/` and `include/` files) has SHA-256
`e95ce30d4305a687a80d4dd9076b77b8202019e4de55db9cfa4418bf6eb38f17`.
The OS-eligibility report, retained SOI, associated-sleep, BCN-mute, IE, USB,
and BTCOEX reports, Tahoe payload parity, 31 payload-builder contracts,
`py_compile`, and staged whitespace check passed. A clean Tahoe build resolved
all 959 undefined symbols against BootKC.

The installed candidate loaded as UUID
`E6F4DE9C-47BF-37A5-959F-64B50ADB4BC4` with executable SHA-256
`ef69b9037d7b2948fb065c091ad533e93ab9bc8d3112cde4b0f9e397bb493ac9` and
AuxKC SHA-256
`52781a9010b8a2201ab49e82d087e2374af9e2d12ac8c10c97ea6fa3abf234fa`.
The deployment's informational codesign check reported an unsigned code
object; loaded identity is established by kmutil, UUID, and executable hash,
not a signing claim. After an explicit normal credentialed rejoin, capped
uplink and reverse 240-second gates each transferred 572 MiB at 20.0 Mbit/s
with 240/240 concurrent ping replies and 0.0% loss (mean RTT 3.235 ms and
6.108 ms; reverse sender had three retransmits). Hostapd retained an
authorized, authenticated, associated station with zero TX failures, QEMU
remained running, the focused bounded guest failure filter produced
`no_matching_guest_panic_wcl_airportitlwm_marker`, and the bounded host filter
produced `no_recent_fatal_vfio_iommu_aer_match`.

The recovered reference proves NetAdapter/commander EDCA work, but does not
establish a complete public carrier allocation or Apple null/valid-input
return-code parity. No guessed carrier or private setter ioctl was issued, so
this is explicitly not a claim of direct setter runtime invocation. The known
networksetup association string remains a false negative; AP station state,
IPv4/gateway route, ping, and traffic gates are the connection evidence.
Radio OFF/ON remains excluded because the restored bit-identical A2DF baseline
reproduces the separate WCL lifecycle panic. Full immutable runtime evidence
is under
`/home/dima/Projects/aiam/runtime-captures/itlwm-os-eligibility-quarantine-20260713/`.

## FIX_CANDIDATE — Dynamic RSSI Window false-success quarantine

- anomaly ID: public `setDYNAMIC_RSSI_WINDOW_CONFIG` accepted a non-null
  carrier, copied its dword into a local cache, and returned success although
  no Intel dynamic-RSSI configuration backend consumes that state.
- recovered reference components: Tahoe 25C56 image SHA-256
  `4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`
  identifies the Infra wrapper symbol at `0x100019530` and, separately, Core
  implementation `0x10014365e`. Core passes the carrier to `0x100140672`,
  which validates the reference range then selects ConfigManager `+0x1558` /
  `0x10008c6a6`; its recovered path sends four-byte `rssi_win` and `snr_win`
  commander IOVARs.
- actual local behavior: the setter only copied a dword cache; scoped local
  source contains no matching Dynamic-RSSI configurator or `rssi_win`/
  `snr_win` path, and no consumer observed that pseudo-state.
- proposed correction: preserve the existing local null safety guard; return
  `kIOReturnUnsupported` for every non-null carrier before mutation; remove
  only the dead cache member and its two reset sites.
- scope boundary: no ConfigManager/commander lift, no inferred complete carrier
  allocation, no emulated reference 2..16 range or status path, no guessed
  carrier or private setter invocation, and no Apple null or valid-input
  return-code parity claim.
- verification plan: deterministic source report, retained payload contracts,
  clean Tahoe build/load identity, saved-profile rejoin, bounded bidirectional
  traffic/ping, and focused bounded guest/host fault filters. Radio OFF/ON
  remains excluded because the restored bit-identical A2DF baseline reproduces
  the separate WCL lifecycle panic.

## VERIFIED RESULT — Dynamic RSSI Window false-success quarantine

The declared verification plan completed. The compiled source-code delta
(build-input `AirportItlwm/` and `include/` files) has SHA-256
`8042f72fce8467eeaca669819ee061be636bdcd73c7878d78143262425a3d9cc`.
The Dynamic-RSSI report, retained OS-eligibility, SOI, associated-sleep,
BCN-mute, IE, USB, and BTCOEX reports, Tahoe payload parity, 31
payload-builder contracts, `py_compile`, and staged whitespace check passed.
A clean Tahoe build resolved all 959 undefined symbols against BootKC.

The installed candidate loaded as UUID
`79A76107-B7AE-3070-ACAC-51F05B8C3039` with executable SHA-256
`1e8f690c8e81e65c7ba86affb5e8d92c1728524ab108e722e86a59aa9ac367c5` and
AuxKC SHA-256
`e4cb1c0c8e2b0acc6340efb314cedeab14739c8022fa4d084f0e6396c3bb339e`.
An initial AuxKC invocation without a base collection failed before an AuxKC
move or reboot; the recorded retry used explicit BootKC/SystemKC/bundle paths
and produced the identity above. The deployment's informational codesign check
reported an unsigned code object; loaded identity is established by kmutil,
UUID, and executable hash, not a signing claim. The saved-profile join attempt
returned `-3900`; a normal explicit credentialed rejoin then succeeded.
Capped uplink and reverse 240-second gates each transferred 572 MiB at
20.0 Mbit/s with 240/240 concurrent ping replies and 0.0% loss (mean RTT
4.309 ms and 6.141 ms; reverse sender had two retransmits). Hostapd retained
an authorized, authenticated, associated station with zero TX failures, QEMU
remained running, the focused bounded guest failure filter produced
`no_matching_guest_panic_wcl_airportitlwm_marker`, and the bounded host filter
produced `no_recent_fatal_vfio_iommu_aer_match`.

The capture independently identifies the Dynamic-RSSI Infra wrapper symbol
and Core implementation, but does not record a wrapper-to-Core edge. The Core
recovery proves ConfigManager/commander work, while the scoped local source
absence check is limited to the matching Dynamic-RSSI configurator and
`rssi_win`/`snr_win` anchors; it is not a claim that generic local IOVAR
transport is absent. No complete carrier allocation or Apple null/range/error,
feature-gate, or transport-status parity is claimed. No guessed carrier or
private setter ioctl was issued, so this is explicitly not a claim of direct
setter runtime invocation. The known networksetup association string remains a
false negative; AP station state, IPv4/gateway route, ping, and traffic gates
are the connection evidence. Radio OFF/ON remains excluded because the
restored bit-identical A2DF baseline reproduces the separate WCL lifecycle
panic. Full immutable runtime evidence is under
`/home/dima/Projects/aiam/runtime-captures/itlwm-dynamic-rssi-window-quarantine-20260713/`.

## VERIFIED RESULT — WCL WNM Offload false-success quarantine

The public `setWCL_WNM_OFFLOAD` cache/flag acknowledgement has been removed.
The local null guard remains, while every non-null carrier now returns
`kIOReturnUnsupported` before pseudo-state mutation; only its dead `0x30`
cache, flag, and two reset pairs were removed. Sibling `setWCL_WNM_OPS` and
`getWCL_WNM_OFFLOAD` remain outside this batch.

The retained Tahoe 25C56 recovery proves the direct
`0x100019af6` -> `0x1001429d2` -> `+0x15b0` -> `0x1000a99e0` WnmAdapter
path, including its offload configure/unconfigure work and descendants that
reach `tclas_add`, `wnm_dms_set`, and `wnm_dms_dependency`. It does not infer
a complete public carrier allocation or Apple null, valid-input, feature-gate,
or transport-status parity. The local absence check is correspondingly scoped
to matching WNM-offload anchors, not generic local IOVAR transport.

The deterministic WNM-offload report plus retained reports, payload contracts,
whitespace check, and clean Tahoe build all passed; the build resolved
959/959 symbols. The compiled code delta is
`595a8b9c5f9fd0b895654beb61798024789b5dbbaa25c9cf20d2790a5b761600`.
The loaded candidate UUID is `69F07E16-7EF4-39C0-BDC6-F9ECB116BB3C`, its
executable SHA-256 is
`2d823655c20ffcd31ccd3972b4618c9b5b3a85010d5dcb9ea9ec280cd9d6cb52`, and
the rebuilt AuxKC SHA-256 is
`cba8cfc51c9ffa10f5fa1ef13394d448dba2255edc8499e887b62b0f344a3721`.

After normal credentialed rejoin, `en1` recovered `10.77.0.47` and its route
to `10.77.0.1`. The bounded 240-second uplink and reverse runs each completed
572 MiB at 20.0 Mbit/s with concurrent 240/240 ping delivery (3.528 ms and
6.398 ms mean RTT; reverse sender had one retransmit). Hostapd retained an
authorized, authenticated, associated station with zero TX failures, QEMU
remained running, the focused guest filter produced
`no_matching_guest_panic_wcl_airportitlwm_marker`, and the bounded host filter
produced `no_recent_fatal_vfio_iommu_aer_match`. These are narrowly scoped
filters, not a generic no-log claim. No direct setter invocation, guessed
carrier, private IOCTL, or radio OFF/ON occurred. The guest rebooted to load
the AuxKC; the host did not reboot. Immutable runtime evidence is under
`/home/dima/Projects/aiam/runtime-captures/itlwm-wcl-wnm-offload-quarantine-20260713/`.

## VERIFIED RESULT — WCL WNM OPS false-success quarantine

The declared verification plan completed. `setWCL_WNM_OPS` preserves its local
null safety guard and returns `kIOReturnUnsupported` for every non-null carrier
before mutation. The change removes only the dead WNM OPS `0x338` cache, its
acknowledgement flag, and their two reset pairs. `setWCL_WNM_OFFLOAD`,
`getWCL_WNM_OFFLOAD`, `setWCL_LIMITED_AGGREGATION`, and APSTA behavior are
unchanged.

Tahoe 25C56 reference recovery (DEXT SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`) proves
the direct Infra-wrapper `0x100019abe` -> Core `0x1001429b0` -> Core `+0x15b0`
-> WnmAdapter `0x1000a7ff0` path. The adapter branches include enterprise,
product-information, and beacon-reporting work; recovered WNM support handling
reaches Commander `runIOVarSet` `0x10017b6e6` for `wnm`. This is evidence of
real backend work, not a lift: it does not infer a complete public carrier,
Apple null/valid-input/feature-gate/transport-status return parity, or a
generic absence of local IOVAR support. No guessed carrier or private setter
invocation was made.

The deterministic WNM OPS report, retained affected reports and payload
contracts, `py_compile`, and staged whitespace check passed. The compiled
build-input code delta SHA-256 is
`09c36fe84437d52682318cbdaad7166a0936723b248019c946750943fb5cd02f`; a clean
Tahoe build resolved all 959 undefined symbols. The rebuilt AuxKC loaded UUID
`B9828396-276B-323E-ABAD-CC8A7D517D74` with executable SHA-256
`d3c3dbdb16ef7e8d7c391efc08ee7d8fb0dae3ec66834a0d853060cac9593508` and AuxKC
SHA-256 `26c08de0bd83eeb4bd9465bf36fd4f16c0ef7fd536c01c5269c56cdcfdfdf735`.

After a normal explicit credentialed rejoin, `en1` held `10.77.0.47` and the
route to `10.77.0.1` used `en1`. Capped 240-second uplink and reverse gates
each transferred 572 MiB at 20.0 Mbit/s with 240/240 concurrent pings and
0.0% loss (mean RTT 4.775 ms uplink, 5.959 ms reverse; three reverse sender
retransmits). Hostapd retained an authenticated, associated, authorized
station with final `tx failed: 0`, and QEMU remained running. The exact
bounded markers were `no_matching_guest_panic_wcl_airportitlwm_marker` and
`no_recent_fatal_vfio_iommu_aer_match`; they are narrow focused checks, not a
generic log-cleanliness claim. The known `networksetup` association string is
a false negative, so AP state, IPv4/route, ping, and traffic are the
association evidence. The guest rebooted only to load the rebuilt AuxKC; the
host was not rebooted. No direct setter, private IOCTL, guessed carrier, or
radio OFF/ON cycle was used. Full immutable evidence is under
`/home/dima/Projects/aiam/runtime-captures/itlwm-wcl-wnm-ops-quarantine-20260713/`.

## VERIFIED RESULT — REALTIME_QOS_MSCS false-success quarantine

The public `setREALTIME_QOS_MSCS` had accepted a non-null
`apple80211_state_data`, copied its `+0x4` state dword into a local cache, and
returned success although that cache had no consumer or QoS/MSCS firmware and
completion path.  The correction preserves the existing defensive local null
guard, returns `kIOReturnUnsupported` for every non-null request before
mutation, and removes only `cachedRealTimeQosMscs` plus its two initialization
resets.  Scoped source confirms no matching local QoS/MSCS sender, terminal,
or event handler.

Tahoe 25C56 DEXT SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab` dispatches
from Infra wrapper `0x1000189ac` through Core virtual `+0x7b0` to
`0x1001e81a4`.  Core gates feature bit `0x5f`, config `+0x7579`, and the
current-BSS QoS/MSCS virtual `+0x290`; only then it inspects `data + 4`, updates
`+0x757b`, and calls `sendQoSMgmtMSCSReq` `0x10013d028`.  Its sender reaches
`confiQoSMgmtMSCS` `0x10013cda6`, the `WL_QOS_CMD_RAV_MSCS`/`qosSetIOVar`
terminal, and the reference has `handleMSCSEvent` `0x1001de8dc`.  The reference
raw `0x16` null branch is after those QoS/BSS gates; this remains neither an
Apple null-input nor valid-input return-code, full-carrier-layout, or firmware
completion parity claim.

The deterministic report, retained contracts, payload parity, 31 payload
builder contracts, `py_compile`, prebuild audit, and staged whitespace check
passed.  A clean Tahoe build resolved all 959 symbols and produced UUID
`91F1DF9E-04F2-38FB-8834-627B03916E56` with executable SHA-256
`eed60a9acc5cc9dfc89ff98cd399d2490728d30cf8e522b20ee36a86fa7a61e8`.
After explicit guest-only AuxKC rebuild and a normal secret-hidden credentialed
rejoin, both 240-second traffic directions transferred 572 MiB at 20.0 Mbit/s;
their concurrent pings were 240/240 with 0.0% loss (mean 3.562 ms uplink,
5.985 ms reverse), and reverse iperf had zero sender retransmits.  AP evidence
remained authenticated/associated/authorized with zero TX failures, QEMU was
running, and focused bounded guest/host filters found no matching WCL/
AirportItlwm panic or fatal vfio/IOMMU/DMAR/AER marker.  The guest rebooted only
to load the AuxKC; the host was not rebooted.  No direct setter, private IOCTL,
guessed carrier, or radio OFF/ON was used.  Full immutable evidence is under
`/home/dima/Projects/aiam/runtime-captures/itlwm-realtime-qos-mscs-quarantine-20260713/`.

## VERIFIED RESULT — IE public setter and carrier-ABI false-success quarantine

The declared verification plan completed. The compiled source-code delta
(build-input `AirportItlwm/` and `include/` files) has SHA-256
`96aed4fc7e37dbb270832eb6fd53ef9d32cbae304c2a47cf45ca3bf30d1903a8`.
The IE public-quarantine report, retained USB/BTCOEX reports, Tahoe payload
parity, 31 payload-builder contracts, and staged whitespace check passed. A
clean Tahoe build resolved all 959 undefined symbols against BootKC.

The installed candidate loaded as UUID
`7E962C85-D85B-33C7-B66C-CB3AC8F052A3` with executable SHA-256
`e721c7e74edc9a5fdba76d80fe62455312dce02a4cea1014370e742cd24ef99c` and
AuxKC SHA-256
`c4c6333f1ac612e85e89a052bb7ecbaafff588be1895f06dc8cb1d0efcc7c338`.
The deployment's informational codesign check reported an unsigned code
object; loaded identity is established by kmutil, UUID, and executable hash,
not a signing claim. After an explicit normal credentialed rejoin, capped
uplink and reverse 240-second gates each transferred 572 MiB at 20.0 Mbit/s
with 240/240 concurrent ping replies and 0.0% loss (mean RTT 3.521 ms and
6.695 ms; reverse sender had one retransmit). Hostapd retained an authorized,
authenticated, associated station with zero TX failures, QEMU remained
running, the focused bounded guest failure filter had no matching
panic/WCL/AirportItlwm marker, and the bounded host filter had no fatal
VFIO/IOMMU/AER match.

The recovered reference proves real WAPI and `vndr_ie` backend work and exact
carrier/range semantics, but does not establish Apple valid-input return-code
parity outside the backend result. No guessed IE carrier or private setter
ioctl was issued, so this is explicitly not a claim of direct setter runtime
invocation. The known networksetup association string remains a false
negative; AP station state, IPv4/gateway route, ping, and traffic gates are
the connection evidence. Radio OFF/ON remains excluded because the restored
bit-identical A2DF baseline reproduces the separate WCL lifecycle panic. Full
immutable runtime evidence is under
`/home/dima/Projects/aiam/runtime-captures/itlwm-ie-public-quarantine-20260713/`.

## VERIFIED RESULT — USB host notification false-success quarantine

The declared verification plan completed. The compiled source-code delta
(build inputs only) has SHA-256
`e0bf5fc4c69d0e6166e17758e78af86b51bfbc4c4e43e669503b240ac092fc09`.
The USB host-notification report, retained affected quarantine reports, Tahoe
payload-builder suite (31 contracts), payload parity, and staged whitespace
check passed. A clean Tahoe build resolved all 959 undefined symbols against
BootKC.

The installed candidate loaded as UUID
`E65EA5A4-7787-3BD9-B86E-7A153B329F8B` with executable SHA-256
`e3c8c3e69ef2f5f2adf96aa5667bf1f3cdae61e25f319c809e96aa9efb14162a` and
AuxKC SHA-256
`48e0bb48ddf7b084fe2163640cbb0c69fedbf28e95c69c5e27c4aef6de206985`.
The deployment's informational codesign check reported an unsigned code
object; the loaded identity is established by kmutil, UUID, and executable
hash, not a signing claim. After an explicit normal credentialed rejoin,
capped uplink and reverse 240-second gates each transferred 572 MiB at
20.0 Mbit/s with 240/240 concurrent ping replies and 0.0% loss (mean RTT
5.378 ms and 6.158 ms; reverse sender had zero retransmits). Hostapd retained
an authorized, authenticated, associated station with zero TX failures, QEMU
remained running, the bounded guest fault filter had no matching
panic/WCL/AirportItlwm marker, and the bounded host filter had no fatal
VFIO/IOMMU/AER match.

The recovered reference proves hidden-owner and real commander work but does
not establish a complete public-carrier allocation or Apple null/valid-input
return-code parity. No guessed carrier or private setter ioctl was issued, so
this is explicitly not a claim of direct setter runtime invocation. The known
networksetup association string remains a false negative; AP station state,
IPv4/gateway route, ping, and traffic gates are the connection evidence.
Radio OFF/ON remains excluded because the restored bit-identical A2DF baseline
reproduces the same separate WCL lifecycle panic. Full immutable runtime
evidence is under
`/home/dima/Projects/aiam/runtime-captures/itlwm-usb-host-notification-quarantine-20260713/`.

## FIX_CANDIDATE — BTCOEX public setter false-success quarantine

- anomaly ID: public `BTCOEX_PROFILE`, `BTCOEX_PROFILE_ACTIVE`, and
  `BTCOEX_2G_CHAIN_DISABLE` valid carriers reach a synthetic commander that
  updates local registry/cache state, completes status zero, and reports
  success without an Intel-equivalent coexistence backend.
- expected reference path: Tahoe 25C56 wrappers `0x1000186c8`/`+0x670`,
  `0x100018714`/`+0x698`, and `0x1000187ac`/`+0x690` lead to Core
  `0x100124656`, `0x1001e393a`, and `0x1001e3a3e`. They retain raw invalid
  `0xe00002c2` branches and perform real `btc_profile`, `btc_profile_active`,
  and `btc_2g_shchain_disable` commander IOVAR work with transport status.
- actual local behavior: `TahoeCommanderV2` changes a registry and
  `dispatchTransport` completes status zero. The setters copy profile state or
  update active/chain getter caches, but no Intel firmware transport consumes
  these Tahoe carriers.
- proposed correction: preserve null/absent-instance raw errors and profile
  band/mode/index validation; reject valid non-null carriers with
  `kIOReturnUnsupported` before commander/cache mutation; remove only dead
  profile cache fields while retaining paired getter caches.
- scope boundary: no generic commander/registry, getter, MWS, boot-time Intel
  coex, WCL, radio, power, or association change; no guessed carrier or private
  setter invocation and no Apple valid-input return-code parity claim.
- verification plan: deterministic source report, retained payload contracts,
  clean Tahoe build/load identity, saved-profile rejoin, bounded bidirectional
  traffic/ping, and bounded guest/host fault filters. Radio OFF/ON remains
  excluded because the restored bit-identical A2DF baseline reproduces the
  separate WCL lifecycle panic.

## VERIFIED RESULT — BTCOEX public setter false-success quarantine

The declared verification plan completed. The compiled source-code delta
(build inputs only) has SHA-256
`24dcf7bda7ce62d3f3a119244e3ff413ca595d51316427f0e896f4903477da3c`.
The BTCOEX public quarantine report, retained affected quarantine reports,
Tahoe payload-builder suite (31 contracts), payload parity, and staged
whitespace check passed. A clean Tahoe build resolved all 959 undefined
symbols against BootKC.

The installed candidate loaded as UUID
`ECBEC631-783C-31FA-97B7-F2439F381810` with executable SHA-256
`f5db668f7fb739452c762c3dc370c626b000fcf60d49dad9f4c2108c0ed9ac6a` and
AuxKC SHA-256
`2ad4a23a8f3c6e313e4dad745aeef8c403f81a3ab0b306f47e573818eb71a6e8`.
The deployment's informational codesign check reported an unsigned code
object; the loaded identity is established by kmutil, UUID, and executable
hash, not a signing claim. After an explicit normal credentialed rejoin,
capped uplink and reverse 240-second gates each transferred 572 MiB at
20.0 Mbit/s with 240/240 concurrent ping replies and 0.0% loss (mean RTT
7.129 ms and 8.122 ms; reverse sender had two retransmits). Hostapd retained
an authorized, authenticated, associated station with zero TX failures, QEMU
remained running, the bounded guest fault filter had no matching
panic/WCL/AirportItlwm marker, and the bounded host filter had no fatal
VFIO/IOMMU/AER match.

The recovered reference proves concrete coexistence firmware work and visible
invalid branches but does not establish complete public carrier allocations or
Apple valid-input return-code parity. No guessed carrier or private setter
ioctl was issued, so this is explicitly not a claim of direct setter runtime
invocation. The known networksetup association string remains a false
negative; AP station state, IPv4/gateway route, ping, and traffic gates are
the connection evidence. Radio OFF/ON remains excluded because the restored
bit-identical A2DF baseline reproduces the same separate WCL lifecycle panic.
Full immutable runtime evidence is under
`/home/dima/Projects/aiam/runtime-captures/itlwm-btcoex-public-quarantine-20260713/`.

## FIX_CANDIDATE — IE public setter and carrier-ABI false-success quarantine

- anomaly ID: public `APPLE80211_IOC_IE` / selector `552` accepted a
  zero-length or valid carrier, passed it into synthetic local owner/commander
  bookkeeping, copied it into dead Skywalk caches, and reported success. The
  legacy controller `setIE` entry also returned success unconditionally. The
  shared local carrier declaration additionally inserted an extra dword that
  moved IE bytes from the recovered `+0x14` to `+0x18`.
- expected reference path: Tahoe 25C56 Infra wrapper `0x100018230` dispatches
  virtual `+0x528` to Core `0x100121826`. It returns raw `0x16` for null,
  zero, and `>0x800` lengths; it accepts only `1..0x800` in a `0x814` carrier
  with bytes at `+0x14`, then calls either JoinAdapter `0x10003eeac` / `wapiie`
  or Core `0x10012109c` / `vndr_ie` commander work.
- actual local behavior: no Intel `wapiie` or `vndr_ie` backend exists. The
  local generic commander only updated registry/cache state and completed
  status zero; the ABI declaration contradicted the existing `0x814` APSTA
  layout witness.
- proposed correction: restore the `0x814` / `+0x14` declaration and validate
  only `1..0x800`; preserve raw invalid status in both public entry points;
  return `kIOReturnUnsupported` for a valid carrier before owner, async,
  cache, or transport mutation; delete only seven dead Skywalk IE cache fields
  and their two reset sites.
- scope boundary: no generic commander/owner, association, WCL, radio, MWS,
  power, or Intel firmware change; no guessed carrier or private setter
  invocation, and no Apple valid-input return-code parity claim.
- verification plan: deterministic source report, retained payload contracts,
  clean Tahoe build/load identity, saved-profile rejoin, bounded bidirectional
  traffic/ping, and bounded guest/host fault filters. Radio OFF/ON remains
  excluded because the restored bit-identical A2DF baseline reproduces the
  separate WCL lifecycle panic.
