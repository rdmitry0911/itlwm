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
