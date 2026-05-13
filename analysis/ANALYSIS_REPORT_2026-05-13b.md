# Analysis report — 2026-05-13b (WCL reassociation response-RX terminal publication wiring)

This entry records the Stage 1 layer that wires the existing
`ieee80211_wcl_reassoc_post_success` and `ieee80211_wcl_reassoc_post_failure`
gated helpers to the reassociation response RX edge in
`ieee80211_recv_assoc_resp`, completing one of the deferred wiring edges
named in the CR-446 Stage 2 decision.

## ANOMALY

- id: A-WCL-REASSOC-RESPONSE-RX-TERMINAL-UNWIRED-20260513
- status: CONFIRMED_PRODUCER_EDGE_UNWIRED
- symptom: After a userspace WCL reassociation request, the local driver
  receives a reassociation response frame but never publishes the
  recovered Apple WCL terminal selector for the success or failure outcome
  of that response. Apple userspace consumers (CoreWLAN / CoreWiFi /
  airportd) only observe the legacy `IEEE80211_EVT_STA_ASSOC_DONE` event
  and the synchronous send return; they do not observe selector `0x49`
  len 8 on success or selector `0xcf` len 4 on response failure.
- first visible manifestation: Static analysis of the local source at
  HEAD `13229b4`. The host-owned WCL reassociation owner helpers
  `ieee80211_wcl_reassoc_post_success` and
  `ieee80211_wcl_reassoc_post_failure` exist (defined in
  `itl80211/openbsd/net80211/ieee80211.c` and declared in
  `itl80211/openbsd/net80211/ieee80211_var.h`); they carry the
  post-send gate required by the recovered Apple contract. However, no
  call site in `ieee80211_recv_assoc_resp` invokes either helper, so
  the response-RX edge never publishes the WCL terminal selector even
  when the lower owner has reached the `REASSOC_REQ_SENT` post-send
  leaf and the response decode succeeds or fails.

## DIVERGENCE POINTS

The recovered AppleBCMWLAN reassociation contract requires the driver
to publish exactly one terminal WCL selector per `setWCL_REASSOC`
request lifecycle: selector `0x49` len 8 `{status=0, reason=0}` on
response success, or selector `0xcf` len 4 `{nonzero failure code}` on
response failure, send failure, response discard, management timeout,
or reset invalidation. The auditor-verified `AUDITOR_VERDICT_WCL_REASSOC_DECOMP_CLOSURE_20260509T161652_0300.md`
(sha256 `de12ec6fc61bd056e865079cd4405aedca3123bb0aa1cb3642b714fb3a824726`)
records this contract and identifies the response-RX edge as a
follow-up Stage 1 wiring item: "Future response-RX, timeout, reset
invalidation, functional reassociation, CONTROL_STA_NETWORK stability, and
AP/APSTA work require their own Stage 1 and Stage 2 gates" (see
`commit-approval/decisions/COMMIT_DECISION_CR-446_STAGE2.md`).

At HEAD `13229b4`, the response-RX edge in
`itl80211/openbsd/net80211/ieee80211_input.c::ieee80211_recv_assoc_resp`
distinguishes the reassociation path with a local `reassoc` parameter
and parses the 802.11 status field, then fires
`IEEE80211_EVT_STA_ASSOC_DONE` on success and returns early on failure
with a `XYLog` debug message and an `is_rx_auth_fail` statistic bump.
Neither path invokes the WCL terminal publication helpers.

## CANDIDATE CAUSES

- confirmed: the response-RX wiring of the WCL_REASSOC terminal helpers
  was explicitly deferred in CR-446 to follow-up Stage 1 layers. CR-446
  Stage 2 was APPROVED with the explicit non-claim listed above. No
  follow-up Stage 1 request has wired it.
- rejected: a pre-send / before-owner-active reassoc-resp RX cannot
  publish a terminal selector. The gate inside the helpers
  (`ic_wcl_reassoc_owner_active == 0` short-circuit and
  `ieee80211_wcl_reassoc_leaf_is_post_send(...)` predicate) silently
  no-ops in that case, so adding the helper call at the response-RX
  edge cannot leak a spurious terminal selector when no real owner is
  in flight.
- rejected: the `reassoc` flag inside `ieee80211_recv_assoc_resp` is
  the carrier that distinguishes initial associate from reassociate.
  Wiring the helpers only under `if (reassoc)` matches the recovered
  Apple contract that reserves WCL terminal selectors for the
  reassociation lifecycle.

## CONFIRMED ROOT

`ieee80211_recv_assoc_resp` at HEAD `13229b4` decodes the 802.11
status field and updates `ic->ic_assoc_status` and the legacy
`IEEE80211_EVT_STA_ASSOC_DONE` event, but never invokes the host-owned
WCL reassociation publication helpers, leaving the recovered Apple
selector contract unobservable from this code path.

## FIX PLAN

The Stage 1 patch at
`commit-approval/artifacts/CR-468-stage1-wcl-reassoc-response-rx-terminal-publication.diff`
modifies one source file against HEAD `13229b4`:

- `itl80211/openbsd/net80211/ieee80211_input.c`
  - On success (`status == IEEE80211_STATUS_SUCCESS`) and when
    `reassoc != 0`, call `ieee80211_wcl_reassoc_post_success(ic)` so
    the existing gated helper publishes selector `0x49`.
  - On failure (`status != IEEE80211_STATUS_SUCCESS`) and when
    `reassoc != 0`, call
    `ieee80211_wcl_reassoc_post_failure(ic, (u_int32_t)status)` so the
    existing gated helper publishes selector `0xcf` with the 802.11
    status code as the failure result.

No other source files are modified. The existing helpers in
`ieee80211.c` and the prototypes in `ieee80211_var.h` are reused
verbatim; no new functions, types, or fields are introduced.

## NON-CLAIMS

This Stage 1 layer does not claim:

- AP-mode, hostap, beaconing, AP client association, AP DHCP, or AP
  traffic success;
- CONTROL_STA_NETWORK control success or lab AP control success;
- successful runtime reassociation against any AP;
- closure of any STA WPA2 4-way PSK / EAPOL lineage gap;
- closure of the CR-467 Stage 2 lab AP STA scan/join/DHCP/IP gate
  (which remains blocked under Route A per the rev7 v4 rejection);
- runtime stability evidence (Stage 2 work);
- selector publication from any forbidden source enumerated in the
  CR-446 verdict (bgscan, same-AP scan completion, `WCL_LEAVE`,
  linkdown, reportDown, AP/APSTA, userspace shims, generic
  DriverReset).

## RESIDUAL UNCERTAINTY

- The management-frame timeout edge in `ieee80211_watchdog` and the
  driver-reset invalidation edge remain follow-up Stage 1 layers per
  the CR-446 Stage 2 decision. They are not wired by this patch.
- Functional end-to-end reassociation against a real AP (response
  success + DHCP keep-alive + CONTROL_STA_NETWORK stability) is Stage 2 work and
  is outside the present Stage 1 scope.

## PROVENANCE

- Basis commit: `13229b4a262fa6306e96ce1cc3306b4f48051d23` on `master`.
- Decomp closure verdict:
  `commit-approval/status/AUDITOR_VERDICT_WCL_REASSOC_DECOMP_CLOSURE_20260509T161652_0300.md`,
  sha256 `de12ec6fc61bd056e865079cd4405aedca3123bb0aa1cb3642b714fb3a824726`,
  verdict timestamp 2026-05-09T16:16:52+03:00.
- Foundation request:
  `commit-approval/requests/COMMIT_REQUEST_CR-446-wcl-reassoc-host-owner-contract.md`.
- Foundation Stage 1 decision:
  `commit-approval/decisions/COMMIT_DECISION_CR-446.md`
  (status `APPROVED_FOR_AFTER_FIX_RUNTIME`,
  `allow_after_fix_runtime: YES`,
  `allow_commit_now: NO`).
- Foundation Stage 2 decision:
  `commit-approval/decisions/COMMIT_DECISION_CR-446_STAGE2.md`
  (status `APPROVED`,
  `allow_after_fix_runtime: NO`,
  `allow_commit_now: YES`,
  enumerates deferred follow-up Stage 1 layers including response-RX).
- Foundation commit in HEAD history:
  `1086f64 net80211/AirportItlwm: introduce host-owned WCL reassociation owner contract`.
- Helper definitions referenced at HEAD:
  - `ieee80211_wcl_reassoc_post_success` at
    `itl80211/openbsd/net80211/ieee80211.c:1606-1623`.
  - `ieee80211_wcl_reassoc_post_failure` at
    `itl80211/openbsd/net80211/ieee80211.c:1626-1640`.
  - prototypes at
    `itl80211/openbsd/net80211/ieee80211_var.h:724-725`.
- Helper post-send-gate predicate referenced at HEAD:
  `ieee80211_wcl_reassoc_leaf_is_post_send(...)` in
  `itl80211/openbsd/net80211/ieee80211.c`.
