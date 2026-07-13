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
