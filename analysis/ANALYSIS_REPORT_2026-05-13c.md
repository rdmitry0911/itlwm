# Analysis report — 2026-05-13c (WCL reassociation management-timeout edge terminal publication wiring)

This entry records the Stage 1 layer that wires the existing
`ieee80211_wcl_reassoc_post_failure` gated helper to the management-frame
timer expiration edge in `ieee80211_watchdog`, completing the next
deferred wiring edge after CR-468 v2's response-RX edge.

## ANOMALY

- id: A-WCL-REASSOC-MGT-TIMEOUT-TERMINAL-UNWIRED-20260513
- status: CONFIRMED_PRODUCER_EDGE_UNWIRED
- symptom: When the management-frame retransmission timer
  (`ic->ic_mgt_timer`) expires while a host-owned reassociation request
  is in flight (owner active and last leaf in the post-send set, i.e.
  `IEEE80211_WCL_REASSOC_OWNER_LEAF_REASSOC_REQ_SENT`), the local driver
  tears the state machine down with `ieee80211_new_state(ic,
  IEEE80211_S_SCAN, -1)` without publishing the recovered Apple WCL
  terminal failure selector (`0xcf` len 4).
- first visible manifestation: Static analysis of HEAD `f7e8c66`.
  `ieee80211_watchdog` in `itl80211/openbsd/net80211/ieee80211.c` has no
  caller of `ieee80211_wcl_reassoc_post_failure` despite the helper
  being defined alongside `ieee80211_wcl_reassoc_post_success` in the
  same translation unit and declared in
  `itl80211/openbsd/net80211/ieee80211_var.h`. The
  `IEEE80211_WCL_REASSOC_OWNER_LEAF_REASSOC_REQ_TIMEOUT` constant exists
  in `itl80211/openbsd/net80211/ieee80211_var.h` and is accepted by the
  post-send predicate `ieee80211_wcl_reassoc_leaf_is_post_send`, but no
  producer transitions the owner to it.

## DIVERGENCE POINTS

The recovered AppleBCMWLAN reassociation contract requires the driver
to publish exactly one terminal WCL selector per `setWCL_REASSOC`
request lifecycle. The auditor-verified `AUDITOR_VERDICT_WCL_REASSOC_DECOMP_CLOSURE_20260509T161652_0300.md`
(sha256 `de12ec6fc61bd056e865079cd4405aedca3123bb0aa1cb3642b714fb3a824726`)
authorises publication on "real send failure, response failure/discard,
management timeout, or reset invalidation after the lower owner was
actually sent/opened" and explicitly identifies the management-timeout
edge as a deferred follow-up Stage 1 wiring item:
"Future response-RX, timeout, reset invalidation, functional
reassociation, CONTROL_STA_NETWORK stability, and AP/APSTA work require their own
Stage 1 and Stage 2 gates" (see
`commit-approval/decisions/COMMIT_DECISION_CR-446_STAGE2.md`).

At HEAD `f7e8c66`, the management-frame timer expiration path in
`ieee80211_watchdog` decrements `ic->ic_mgt_timer` to zero, optionally
logs a debug message under `IFF_DEBUG`, and transitions the state
machine to `IEEE80211_S_SCAN`. None of this code path invokes the
host-owned WCL terminal publication helper.

## CANDIDATE CAUSES

- confirmed: the management-timeout wiring of the WCL_REASSOC terminal
  helper was explicitly deferred in CR-446 to follow-up Stage 1 layers.
  The sibling response-RX edge was wired by CR-468 v2 (committed at
  `f7e8c66 net80211: publish reassociation response terminals`); the
  management-timeout edge is the next follow-up.
- rejected: a management-timer expiration that fires when no
  reassociation owner is active cannot publish a terminal selector. The
  gate inside `ieee80211_wcl_reassoc_post_failure`
  (`ic_wcl_reassoc_owner_active == 0` short-circuit and
  `ieee80211_wcl_reassoc_leaf_is_post_send(...)` predicate) silently
  no-ops in that case, so adding the helper call at the watchdog edge
  cannot leak a spurious terminal selector when no real reassociation
  request is in flight.
- rejected: synthesising a TIMEOUT-leaf transition before the helper
  call would be observable inside the helper for one instruction, then
  immediately reset to `IDLE` by the helper. The minimal call relies on
  the helper's existing post-send gate accepting `REASSOC_REQ_SENT` as
  a valid pre-publication state; no manual leaf transition is needed
  for correct terminal-selector publication.

## CONFIRMED ROOT

`ieee80211_watchdog` at HEAD `f7e8c66` handles management-frame timer
expirations by tearing down the state machine to SCAN but never invokes
the host-owned WCL reassociation publication helper. The recovered
Apple `0xcf` len 4 failure selector is therefore unobservable for the
management-timeout outcome of an in-flight reassociation request.

## FIX PLAN

The Stage 1 patch at
`commit-approval/artifacts/CR-469-stage1-wcl-reassoc-mgt-timeout-terminal-publication-v3.diff`
modifies one source file against HEAD `f7e8c66`:

- `itl80211/openbsd/net80211/ieee80211.c::ieee80211_watchdog`: inside
  the existing `if (ic->ic_mgt_timer && --ic->ic_mgt_timer == 0) { ... }`
  block, before the existing STA-mode AUTH/ASSOC timeout handling, add
  a guarded comment and a single call to
  `ieee80211_wcl_reassoc_post_failure(ic, (u_int32_t)ETIMEDOUT)`. The
  call relies on the helper's existing post-send gate to publish the
  WCL terminal failure selector only when a real reassociation owner
  has reached the `REASSOC_REQ_SENT` post-send leaf set; otherwise the
  call is a no-op.

No other source files are modified. The helper definition in
`itl80211/openbsd/net80211/ieee80211.c`, the prototype in
`itl80211/openbsd/net80211/ieee80211_var.h`, the leaf constants and
post-send predicate in `itl80211/openbsd/net80211/ieee80211_var.h`,
and the `<sys/errno.h>` include in
`itl80211/openbsd/net80211/ieee80211.c` are reused verbatim.

## NON-CLAIMS

This Stage 1 layer does not claim:

- AP-mode, hostap, beaconing, AP client association, AP DHCP, or AP
  traffic success;
- CONTROL_STA_NETWORK client control success or lab AP control success;
- successful runtime reassociation against any AP;
- closure of any STA WPA2 4-way PSK / EAPOL lineage gap;
- closure of the CR-467 Stage 2 lab AP STA scan/join/DHCP/IP gate;
- runtime stability evidence (Stage 2 work);
- selector publication from any forbidden source enumerated in the
  CR-446 verdict (bgscan, same-AP scan completion, `WCL_LEAVE`,
  linkdown, reportDown, AP/APSTA, userspace shims, generic
  DriverReset);
- wiring of the driver-reset invalidation edge (separate follow-up
  Stage 1);
- functional end-to-end reassociation against a real AP (Stage 2 work).

## RESIDUAL UNCERTAINTY

- The driver-reset invalidation edge remains the last deferred
  follow-up Stage 1 layer per the CR-446 Stage 2 decision. It is not
  wired by this patch.
- Functional end-to-end reassociation against a real AP (response
  success + DHCP + CONTROL_STA_NETWORK stability) is Stage 2 work and outside the
  present Stage 1 scope.

## PROVENANCE

- Basis commit: `f7e8c66450a3721b639947e15e513994ffc4d309` on `master`
  (commit message: "net80211: publish reassociation response
  terminals"; this is the auditor's exact-diff commit of CR-468 v2).
- Predecessor basis commit:
  `13229b4a262fa6306e96ce1cc3306b4f48051d23` ("docs/itlwm: record
  bounded Apple AP control-plane decomp evidence").
- Decomp closure verdict (unchanged from CR-446/CR-468):
  `commit-approval/status/AUDITOR_VERDICT_WCL_REASSOC_DECOMP_CLOSURE_20260509T161652_0300.md`,
  sha256 `de12ec6fc61bd056e865079cd4405aedca3123bb0aa1cb3642b714fb3a824726`,
  verdict timestamp 2026-05-09T16:16:52+03:00.
- Foundation request:
  `commit-approval/requests/COMMIT_REQUEST_CR-446-wcl-reassoc-host-owner-contract.md`.
- Foundation Stage 1 decision:
  `commit-approval/decisions/COMMIT_DECISION_CR-446.md`.
- Foundation Stage 2 decision:
  `commit-approval/decisions/COMMIT_DECISION_CR-446_STAGE2.md`
  (enumerates response-RX, timeout, reset invalidation, etc. as
  deferred follow-up Stage 1 layers).
- Foundation commit in HEAD history:
  `1086f64 net80211/AirportItlwm: introduce host-owned WCL
  reassociation owner contract`.
- Sibling follow-up commit in HEAD history (response-RX wiring):
  `f7e8c66 net80211: publish reassociation response terminals`
  (CR-468 v2 Stage 2 APPROVED and committed by auditor before this
  Stage 1 was submitted).
- Helper definition referenced at HEAD: function
  `ieee80211_wcl_reassoc_post_failure` is defined in
  `itl80211/openbsd/net80211/ieee80211.c` immediately after the
  sibling `ieee80211_wcl_reassoc_post_success`; both helpers carry
  the post-send gate.
- Helper post-send-gate predicate referenced at HEAD: inline
  `ieee80211_wcl_reassoc_leaf_is_post_send` accepts the post-send
  leaf set including the value
  `IEEE80211_WCL_REASSOC_OWNER_LEAF_REASSOC_REQ_TIMEOUT`; the leaf
  constants and the predicate live in
  `itl80211/openbsd/net80211/ieee80211_var.h`.
- `ETIMEDOUT` source: the constant is provided by `<sys/errno.h>`
  which is already included in
  `itl80211/openbsd/net80211/ieee80211.c` ahead of
  `ieee80211_watchdog`; the new helper-call line uses
  `(u_int32_t)ETIMEDOUT` as the failure result.
