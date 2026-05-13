# Analysis report — 2026-05-14 (WCL reassociation driver-reset invalidation edge terminal publication wiring)

This entry records the Stage 1 layer that wires the existing
`ieee80211_wcl_reassoc_post_failure` gated helper to the
driver-initiated reset edge in `ieee80211_newstate`, completing the
last deferred follow-up Stage 1 layer enumerated by the CR-446 Stage 2
decision after CR-468's response-RX edge and CR-469's
management-timer-expiration edge.

## ANOMALY

- id: A-WCL-REASSOC-DRIVER-RESET-INVALIDATION-TERMINAL-UNWIRED-20260514
- status: CONFIRMED_PRODUCER_EDGE_UNWIRED
- symptom: When the lower driver (HAL `iwx_stop`) initiates a hard
  reset by invoking `sc->sc_newstate(ic, IEEE80211_S_INIT, -1)` (which
  resolves to the upstream `ieee80211_newstate` after the
  `ic->ic_newstate` override is restored at attach time), the local
  net80211 state machine tears the interface down without publishing
  the recovered Apple WCL terminal failure selector (`0xcf` len 4) for
  any host-owned reassociation request still in the post-send leaf
  set (`REASSOC_REQ_SENT`, `REASSOC_REQ_SEND_FAIL`, or
  `REASSOC_REQ_TIMEOUT`). Apple userspace consumers observing
  `setWCL_REASSOC` therefore receive no terminal selector for a
  reassociation request that was cancelled by a driver-initiated
  reset.
- first visible manifestation: Static analysis of HEAD `31942ff`
  (`net80211: publish reassociation timeout failures`).
  `ieee80211_newstate` in
  `itl80211/openbsd/net80211/ieee80211_proto.c` contains no caller of
  `ieee80211_wcl_reassoc_post_failure`. The driver-reset entry path
  is exercised by `iwx_stop` in `itlwm/hal_iwx/ItlIwx.cpp` (line near
  the `sc->sc_newstate(ic, IEEE80211_S_INIT, -1)` call), which in
  turn is reached on HAL shutdown, firmware-error recovery, and any
  scheduled `iwx_stop` invocation that fires while an in-flight
  reassociation request remains active.

## DIVERGENCE POINTS

The recovered AppleBCMWLAN reassociation contract requires the driver
to publish exactly one terminal WCL selector per `setWCL_REASSOC`
request lifecycle. The auditor-verified
`AUDITOR_VERDICT_WCL_REASSOC_DECOMP_CLOSURE_20260509T161652_0300.md`
(sha256
`de12ec6fc61bd056e865079cd4405aedca3123bb0aa1cb3642b714fb3a824726`)
authorises publication on "real send failure, response failure/discard,
management timeout, or reset invalidation after the lower owner was
actually sent/opened" and explicitly identifies reset invalidation as
a deferred follow-up Stage 1 wiring item alongside response-RX and
timeout. The closing CR-446 Stage 2 decision enumerates "reset
invalidation after an open reassociation owner" as a deferred
follow-up that requires its own Stage 1 and Stage 2 gates.

At HEAD `31942ff`, the entry to `ieee80211_newstate` reads `ostate`,
optionally logs the transition under `IFF_DEBUG`, sets `ic_state` to
`nstate`, transitions `if_link_state` to `LINK_STATE_DOWN`, and
clears `IEEE80211_F_TX_MGMT_ONLY` before dispatching on `nstate`.
None of this code path invokes the host-owned WCL terminal
publication helper, so a driver-initiated reset that fires while a
host-owned reassociation request is still in the post-send window
silently drops the contract-required terminal failure event.

## CANDIDATE CAUSES

- confirmed: the reset-invalidation wiring of the WCL_REASSOC terminal
  helper was explicitly deferred in CR-446 to follow-up Stage 1
  layers. The sibling response-RX edge was wired by CR-468 and the
  sibling management-timeout edge was wired by CR-469; the
  reset-invalidation edge is the last enumerated deferred wiring
  item.
- rejected: a driver-reset edge that fires when no reassociation
  owner is active cannot publish a terminal selector. The gate inside
  `ieee80211_wcl_reassoc_post_failure`
  (`ic_wcl_reassoc_owner_active == 0` short-circuit and
  `ieee80211_wcl_reassoc_leaf_is_post_send(...)` predicate) silently
  no-ops in that case, so adding the helper call at the driver-reset
  entry cannot leak a spurious terminal selector when no real
  reassociation request is outstanding.
- rejected: synthesising a `RESET_INVALIDATED` leaf transition before
  the helper call would be observable inside the helper for one
  instruction, then immediately reset to `IDLE` by the helper. The
  minimal call relies on the helper's existing post-send gate
  accepting `REASSOC_REQ_SENT` (the canonical in-flight leaf), the
  synchronous-failure leaf `REASSOC_REQ_SEND_FAIL`, or the
  post-watchdog leaf `REASSOC_REQ_TIMEOUT` as a valid
  pre-publication state; no manual leaf transition or new leaf
  constant is needed for correct terminal-selector publication at the
  reset edge.
- rejected: publishing the terminal selector from the producer side
  (`setWCL_REASSOC`) when the producer observes a HAL reset would
  bypass the post-send-gate invariant: any pre-send-abandonment
  producer return would emit a terminal selector that the recovered
  Apple contract forbids. The synchronous-error branch in
  `setWCL_REASSOC` already publishes via the gated helper when
  `ieee80211_send_mgmt` fails inline, and the success branch
  transitions the leaf to `REASSOC_REQ_SENT` before returning; the
  remaining reset-invalidation window starts after the producer
  returns. The reset edge therefore belongs in net80211, not in the
  producer.

## CONFIRMED ROOT

`ieee80211_newstate` at HEAD `31942ff` handles driver-initiated reset
transitions (`nstate == IEEE80211_S_INIT && mgt == -1`) without
invoking the host-owned WCL reassociation publication helper. The
recovered Apple `0xcf` len 4 failure selector is therefore
unobservable for the driver-reset-invalidation outcome of an in-flight
reassociation request.

## FIX PLAN

The Stage 1 patch modifies one source file against HEAD `31942ff`:

- `itl80211/openbsd/net80211/ieee80211_proto.c::ieee80211_newstate`:
  inside the existing `switch (nstate)` block, at the head of
  `case IEEE80211_S_INIT:` and immediately after the existing
  "If mgt = -1, driver is already partway down" comment block, add a
  guarded comment and a single call to
  `ieee80211_wcl_reassoc_post_failure(ic, (u_int32_t)ECANCELED)`
  conditioned on `mgt == -1`. The call relies on the helper's
  existing post-send gate to publish the WCL terminal failure
  selector only when a real reassociation owner has reached the
  `REASSOC_REQ_SENT` post-send leaf set; otherwise the call is a
  no-op.

No other source files are modified. The helper definition in
`itl80211/openbsd/net80211/ieee80211.c`, the prototype in
`itl80211/openbsd/net80211/ieee80211_var.h`, the leaf constants and
post-send predicate in `itl80211/openbsd/net80211/ieee80211_var.h`,
and the `<sys/errno.h>` include in
`itl80211/openbsd/net80211/ieee80211_proto.c` are reused verbatim.
`ECANCELED` is already provided by the existing
`<sys/errno.h>` include in `ieee80211_proto.c`.

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
- functional end-to-end reassociation against a real AP (Stage 2
  work);
- coverage of every non-`iwx_stop` reset producer in the wider
  driver/HAL surface. Other HAL reset paths that do not pass through
  `sc->sc_newstate(ic, IEEE80211_S_INIT, -1)` remain out of scope
  and may need their own follow-up wiring if and when those producer
  sites are exercised on Tahoe and shown to invalidate an in-flight
  host-owned reassociation request without going through this edge.

## RESIDUAL UNCERTAINTY

- Whether additional HAL/driver reset producer sites (paths that
  invalidate an in-flight reassociation request without funnelling
  through `ieee80211_newstate(ic, IEEE80211_S_INIT, -1)`) exist in
  the current itlwm tree is not exhaustively enumerated by this
  layer. The dominant driver-reset path
  (`iwx_stop -> sc_newstate(IEEE80211_S_INIT, -1)`) is wired; any
  additional alternative path would require its own narrow follow-up
  Stage 1 if and when it is observed on Tahoe and shown to leave a
  post-send WCL_REASSOC owner unpublished.
- Functional end-to-end reassociation against a real AP (response
  success + DHCP + CONTROL_STA_NETWORK stability) is Stage 2 work and outside
  the present Stage 1 scope.

## PROVENANCE

- Basis commit: `31942fff4d03cfdea8f58971be557eb9ffe85daf` on
  `master` (commit message: "net80211: publish reassociation timeout
  failures"; this is the auditor's exact-diff commit of CR-469 v3).
- Predecessor basis commits:
  `f7e8c66450a3721b639947e15e513994ffc4d309` (CR-468 v2 response-RX
  wiring) and `13229b4a262fa6306e96ce1cc3306b4f48051d23` (Apple AP
  control-plane bounded decomp evidence).
- Decomp closure verdict (unchanged from CR-446/CR-468/CR-469):
  `commit-approval/status/AUDITOR_VERDICT_WCL_REASSOC_DECOMP_CLOSURE_20260509T161652_0300.md`,
  sha256
  `de12ec6fc61bd056e865079cd4405aedca3123bb0aa1cb3642b714fb3a824726`,
  verdict timestamp 2026-05-09T16:16:52+03:00.
- Foundation request:
  `commit-approval/requests/COMMIT_REQUEST_CR-446-wcl-reassoc-host-owner-contract.md`.
- Foundation Stage 1 decision:
  `commit-approval/decisions/COMMIT_DECISION_CR-446.md`.
- Foundation Stage 2 decision:
  `commit-approval/decisions/COMMIT_DECISION_CR-446_STAGE2.md`
  (enumerates response-RX, timeout, reset invalidation, etc. as
  deferred follow-up Stage 1 layers).
- Sibling response-RX wiring in HEAD history:
  `f7e8c66 net80211: publish reassociation response terminals`
  (CR-468 v2 Stage 2 APPROVED and committed by auditor).
- Sibling management-timeout wiring in HEAD history:
  `31942ff net80211: publish reassociation timeout failures`
  (CR-469 v3 Stage 2 APPROVED and committed by auditor; this is the
  current HEAD basis).
- Helper definition referenced at HEAD: function
  `ieee80211_wcl_reassoc_post_failure` is defined in
  `itl80211/openbsd/net80211/ieee80211.c` immediately after the
  sibling `ieee80211_wcl_reassoc_post_success`; both helpers carry
  the post-send gate.
- Helper post-send-gate predicate referenced at HEAD: inline
  `ieee80211_wcl_reassoc_leaf_is_post_send` accepts the post-send
  leaf set including `IEEE80211_WCL_REASSOC_OWNER_LEAF_REASSOC_REQ_SENT`,
  `IEEE80211_WCL_REASSOC_OWNER_LEAF_REASSOC_REQ_SEND_FAIL`, and
  `IEEE80211_WCL_REASSOC_OWNER_LEAF_REASSOC_REQ_TIMEOUT`; the leaf
  constants and the predicate live in
  `itl80211/openbsd/net80211/ieee80211_var.h`.
- Producer-side owner-open contract referenced at HEAD: the
  `setWCL_REASSOC` producer in
  `AirportItlwm/AirportItlwmSkywalkInterface.cpp` opens the host
  owner with `ic_wcl_reassoc_owner_active = 1` and leaf
  `IEEE80211_WCL_REASSOC_OWNER_LEAF_SETUP`, then invokes
  `ieee80211_send_mgmt`; on synchronous success the leaf transitions
  to `REASSOC_REQ_SENT`, on synchronous failure the leaf transitions
  to `REASSOC_REQ_SEND_FAIL` and the gated helper publishes
  immediately. The remaining reset-invalidation window therefore
  starts after `setWCL_REASSOC` returns with the owner in the
  post-send set, which is exactly the window the helper's post-send
  gate accepts.
- `ECANCELED` source: the constant is provided by `<sys/errno.h>`
  which is already included in
  `itl80211/openbsd/net80211/ieee80211_proto.c` ahead of
  `ieee80211_newstate`; the new helper-call line uses
  `(u_int32_t)ECANCELED` as the failure result, matching the
  semantic of an in-flight request cancelled by a HAL-initiated
  reset.
- Driver-reset producer reference at HEAD: `iwx_stop` in
  `itlwm/hal_iwx/ItlIwx.cpp` invokes
  `sc->sc_newstate(ic, IEEE80211_S_INIT, -1)` after stopping
  scheduled tasks and the device, before returning the interface to
  its idle state. The `sc->sc_newstate` callback is set in
  `iwx_attach` to the original `ic->ic_newstate` value
  (`ieee80211_newstate`) before `ic->ic_newstate` is overridden to
  the driver's wrapper, so the `iwx_stop` invocation reaches the
  upstream `ieee80211_newstate` directly.
