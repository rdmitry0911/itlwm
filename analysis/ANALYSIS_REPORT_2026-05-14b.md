# Analysis report — 2026-05-14b (net80211 IEEE80211_STA_ONLY opt-out and AP station-event producer bridge)

This entry records the Stage 1 layer that introduces a build-time
opt-out for `IEEE80211_STA_ONLY` and the minimal net80211 AP
station-event producer bridge in
`itl80211/openbsd/net80211/ieee80211_var.h` and
`itl80211/openbsd/net80211/ieee80211_node.c`. The bridge is the
producer-side seam through which the host-owned APSTA owner consumes
station associate, reassociate, and leave events when an AP-mode
exploration build is configured.

## ANOMALY

- id: A-NET80211-APSTA-EVENT-BRIDGE-MISSING-20260514
- status: CONFIRMED_PRODUCER_INTERFACE_MISSING
- symptom: At HEAD `c00415d`, the recovered Apple APSTA owner
  contract requires a per-controller, single-consumer station-event
  callback through which `ieee80211_node_join` and
  `ieee80211_node_leave` publish associate, reassociate, and leave
  transitions to the local APSTA owner. The local net80211 sources
  expose no callback fields, no register/unregister helpers, and no
  publication call sites. As a separate safety blocker, the project
  Tahoe Xcode configurations inject `IEEE80211_STA_ONLY` through
  `GCC_PREPROCESSOR_DEFINITIONS`, which disables every
  `#ifndef IEEE80211_STA_ONLY` AP path including
  `ieee80211_node_join` and `ieee80211_node_leave` themselves. There
  is no header-level mechanism to suppress the injected define for
  an AP-mode exploration build without editing the project file.
- first visible manifestation: Static analysis of HEAD `c00415d`.
  Grep over `itl80211/openbsd/net80211/ieee80211_var.h` and
  `itl80211/openbsd/net80211/ieee80211_node.c` shows no
  `ic_apsta_event_*` field, no `ieee80211_apsta_event_*` symbol, and
  no AP station-event publication call. Grep over
  `itlwm.xcodeproj/project.pbxproj` confirms `IEEE80211_STA_ONLY` is
  a default `GCC_PREPROCESSOR_DEFINITIONS` entry on the Tahoe
  configurations.

## DIVERGENCE POINTS

The auditor-verified B12 closure package
(`AP_CONTROL_PLANE_CLOSURE_STATUS: FULL_LAYER_CLOSED_CODER_READY`,
exported as `itlwm-b13-export-b12-artifacts-20260513T0102` with
result archive sha256
`d94b7939e432f680beb943a3ff56be57d000c1ca28ff26a8ba35ed1a118fb9f1`)
records the recovered APSTA owner FSM in
`reviewed_paths/FSM_LIFECYCLE.md`:

```
net80211 association success
  -> ieee80211_node_join(...)
  -> ic_apsta_event_cb(event=8 or 10, node)
  -> APSTA owner allocate/update station table entry

net80211 leave/deauth/disassoc
  -> ieee80211_node_leave(...)
  -> ic_apsta_event_cb(event=5, node)
  -> APSTA owner removes matching entry and recomputes count
```

`reviewed_paths/CONTRACT_RECOVERY.md` requires that the APSTA
station-event callback check that `ic_apsta_event_arg` still points
to the current owner before teardown clears it, so the callback is
not cleared by a stale unregister after a new owner has been
registered.

`reviewed_paths/IMPLEMENTATION_ROUTE.md` separates the safety
enabler ("`IEEE80211_STA_ONLY` opt-out") from the bridge merge
("station-event callback bridge once owner lifetime and callback
clearing are verified") but requires both for the AP-mode
exploration build to compile and run with truthful no-op behavior
when no consumer is registered.

At HEAD `c00415d`, none of these surfaces exist locally. AP role-7
work that already landed (CR-467 Stage 1 host-owner type
introduction) cannot reach a runtime consumer surface, and the
default Tahoe build cannot compile the AP path even when an
exploration build wants to.

## CANDIDATE CAUSES

- confirmed: the `IEEE80211_STA_ONLY` opt-out and the net80211
  station-event producer bridge are an explicitly deferred follow-up
  Stage 1 layer in
  `docs/reference/IWX_APGO_AP_CONTROL_PLANE_STAGE1_LAYER_CLOSURE_2026_05_13.md`
  and have not yet been wired by any in-HEAD commit.
- rejected: introducing a behavior-changing client-side STA path
  through this bridge. The publication call sites added in this
  patch live inside the existing `#ifndef IEEE80211_STA_ONLY` blocks
  that already gate `ieee80211_node_join` and `ieee80211_node_leave`,
  so default STA-only builds do not compile the call sites at all.
  In AP-opt-out builds where the call sites compile, the bridge is
  dormant when no consumer is registered, so client/STA-only
  behavior is unchanged. No retry, polling, fallback, masking, or
  synthetic event is introduced.
- rejected: making the bridge non-dormant by default or installing a
  placeholder consumer to prove the wiring runs. A placeholder
  consumer would publish synthetic success or claim AP-up runtime
  without a real owner. The producer is dormant on purpose so the
  layer can be merged before the role-7 owner allocation is wired,
  per the closure-doc merge order.

## CONFIRMED ROOT

The producer-side interface for AP station-event publication does
not exist in `itl80211/openbsd/net80211/`, and there is no
header-level opt-out for the `IEEE80211_STA_ONLY` define injected by
the Tahoe Xcode configurations. Both gaps are implementation-only:
they have no behavior consequence in default STA builds and no
external runtime dependency.

## FIX PLAN

The Stage 1 patch at
`commit-approval/artifacts/CR-471-stage1-net80211-station-event-producer-bridge-clean-head.diff`
modifies two source files plus one tracked documentation file
against HEAD `c00415d`:

- `itl80211/openbsd/net80211/ieee80211_var.h`:
  - After the existing `SMALL_KERNEL`-gated definition of
    `IEEE80211_STA_ONLY`, add a guarded `#undef IEEE80211_STA_ONLY`
    triggered by the new `IEEE80211_OPT_OUT_STA_ONLY` flag. The undef
    is no-op when the flag is unset, so default STA builds keep the
    injected define and behave exactly as before.
  - In `struct ieee80211com`, alongside the existing
    `ic_wcl_reassoc_owner_*` fields, add the
    `ic_apsta_event_cb` function pointer field and the
    `ic_apsta_event_arg` opaque cookie field. Both default to NULL
    on zero-initialised allocation so the bridge is dormant from
    boot.
  - After the `ieee80211_wcl_reassoc_*` declarations, add the
    `IEEE80211_APSTA_EVENT_LEAVE/ASSOC/REASSOC` constants (matching
    the recovered Apple APSTA event identifiers 5, 8, 10), the
    `ieee80211_apsta_event_cb_t` typedef, and the public prototypes
    `ieee80211_apsta_event_register`,
    `ieee80211_apsta_event_unregister`, and
    `ieee80211_apsta_event_publish`.
- `itl80211/openbsd/net80211/ieee80211_node.c`:
  - Inside the existing `#ifndef IEEE80211_STA_ONLY` block, add a
    call to `ieee80211_apsta_event_publish` at the end of
    `ieee80211_node_join` after the RSN port setup, dispatching
    `IEEE80211_APSTA_EVENT_ASSOC` for a fresh association and
    `IEEE80211_APSTA_EVENT_REASSOC` for a reassociation against an
    already-associated station. The fresh-versus-reassociation
    decision reuses the existing `newassoc` flag computed at the
    top of `ieee80211_node_join`.
  - Inside the existing `#ifndef IEEE80211_STA_ONLY` block, add a
    call to `ieee80211_apsta_event_publish` near the end of
    `ieee80211_node_leave` immediately before the
    `IEEE80211_STA_COLLECT` newstate transition, dispatching
    `IEEE80211_APSTA_EVENT_LEAVE`. Publishing before the COLLECT
    transition lets the consumer observe the still-present
    association id and per-station state that identify the entry to
    remove.
  - At the end of the file, outside the `#ifndef IEEE80211_STA_ONLY`
    block, add the unconditional definitions of
    `ieee80211_apsta_event_register`,
    `ieee80211_apsta_event_unregister`, and
    `ieee80211_apsta_event_publish`. The register helper rejects a
    NULL callback, accepts idempotent re-registration of the same
    consumer with the same cookie, and rejects a different consumer
    or cookie while a registration exists. The unregister helper
    enforces the recovered identity check by clearing the callback
    only when the supplied cookie matches the cached one. The
    publish helper snapshots the callback and cookie before
    invoking the consumer so a concurrent unregister cannot tear
    them out from under the in-flight publication.
- `analysis/ANALYSIS_REPORT_2026-05-14b.md` (this file): records the
  anomaly, the recovered contract, the implementation, the
  non-claims, and the residual uncertainty for this exact diff.

No other source files are modified. The Xcode project file is not
modified. The Tahoe build keeps `IEEE80211_STA_ONLY` enabled by
default; no production build path changes behavior unless an
explicit AP-mode exploration build sets
`IEEE80211_OPT_OUT_STA_ONLY` and a host-owned APSTA owner registers
through `ieee80211_apsta_event_register`.

## NON-CLAIMS

This Stage 1 layer does not claim:

- AP-mode, hostap, beaconing, AP client association, AP DHCP, or AP
  traffic success;
- CONTROL_STA_NETWORK client control success or lab AP control success;
- successful runtime reassociation against any AP;
- closure of any STA WPA2 4-way PSK / EAPOL lineage gap;
- closure of the CR-467 Stage 2 lab AP STA scan/join/DHCP/IP gate;
- runtime stability evidence (Stage 2 work);
- any change to the existing `wcl_reassoc` terminal selector
  publication contract or to client-mode STA behavior;
- runtime allocation of the host-owned APSTA owner or registration
  of any consumer callback (those belong to follow-up controller and
  HAL Stage 1 layers, items 1, 2, 4, 5-V2, and 8 of the AP
  control-plane closure document);
- functional iwx/iwm AP/GO firmware backend (Stage 2 scope);
- any guest runtime evidence; this is a static implementation layer
  whose runtime exercise depends on the deferred owner allocation
  and HAL backend layers.

## RESIDUAL UNCERTAINTY

- The bridge cannot be exercised at runtime until the controller
  layer allocates the host-owned APSTA owner and registers it
  through `ieee80211_apsta_event_register`; that wiring is the
  separately deferred items 1, 2, 4, and 5-V2 of the closure
  document and is outside this layer.
- The publish call sites are inside the existing
  `#ifndef IEEE80211_STA_ONLY` blocks. In default Tahoe builds the
  block is excluded by the project-injected define, so the call
  sites do not compile and the bridge symbols are reachable only as
  exported declarations. This is intentional for this layer and
  does not affect the default STA build.
- The `IEEE80211_OPT_OUT_STA_ONLY` flag must be set per-target by an
  AP-mode exploration configuration before the
  AP-mode call sites compile. No project-file change is included in
  this layer; that change is part of the follow-up controller-layer
  AP-up gate work.

## PROVENANCE

- Basis commit: `c00415de0898054b0d40acfca6c7cb28db90616d` on
  `master` (commit message: "net80211: publish reassociation
  reset-invalidation failures").
- Decomp closure verdict: `AP_CONTROL_PLANE_CLOSURE_STATUS:
  FULL_LAYER_CLOSED_CODER_READY` recorded by the auditor in
  `sig_20260513T011130_0300_5c504835`. B13 export of B12 artifacts
  is `itlwm-b13-export-b12-artifacts-20260513T0102` with result
  archive sha256
  `d94b7939e432f680beb943a3ff56be57d000c1ca28ff26a8ba35ed1a118fb9f1`.
- Recovered FSM and contract:
  `reviewed_paths/FSM_LIFECYCLE.md`,
  `reviewed_paths/CONTRACT_RECOVERY.md`,
  `reviewed_paths/IMPLEMENTATION_ROUTE.md` inside the B13 archive.
- Closure-document anchor:
  `docs/reference/IWX_APGO_AP_CONTROL_PLANE_STAGE1_LAYER_CLOSURE_2026_05_13.md`
  enumerates this layer as item 10
  ("`IEEE80211_STA_ONLY` opt-out and net80211 station-event producer
  bridge in `itl80211/openbsd/net80211/ieee80211_var.h` and
  `ieee80211_node.c`") of the deferred follow-up Stage 1 set.
- Project-file evidence for the injected define:
  `itlwm.xcodeproj/project.pbxproj` lines that list
  `IEEE80211_STA_ONLY` inside `GCC_PREPROCESSOR_DEFINITIONS` for the
  Tahoe configurations.
- Helper definition references at HEAD: `ieee80211_node_join` and
  `ieee80211_node_leave` are defined inside the existing
  `#ifndef IEEE80211_STA_ONLY` block in
  `itl80211/openbsd/net80211/ieee80211_node.c`.
