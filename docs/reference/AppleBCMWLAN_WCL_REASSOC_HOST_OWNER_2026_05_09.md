# AppleBCMWLAN WCL Reassociation Host Owner — Recovered Contract

Source binary: `/tmp/AppleBCMWLANCoreMac` (Apple BCM Wi-Fi reference) and
the `task1`–`task6` decomposition packages produced for the
`WCL_REASSOC_DECOMP_CLOSURE_2026_05_09` workstream.

## Selectors and lifecycle

The reference path publishes three WCL selectors around a reassociation
attempt:

- `0x49` length 8 — terminal reassociation result. First dword zero is
  success/completion; nonzero is a mapped failure status. Published by
  the lower owner only after a real reassociation event is observed.
- `0xcf` length 4 — terminal reassociation failure. Published by the
  lower owner only after a real send failure, response failure/discard,
  management timeout, or reset invalidation that follows a real lower
  owner request send/attempt.
- `0x89` length `0xc` — non-terminal roam-scan-start progress marker.
  Does not close the owner.

The reference Apple body
(`AppleBCMWLANCore::setWCL_REASSOC` →
`AppleBCMWLANNetAdapter::sendReassocCommand` → firmware command id
`WLC_REASSOC` `0x440006`) opens the firmware-owned lower owner. Local
itlwm has no equivalent firmware reassociation command on the
Intel iwm/iwx HALs (see `task3` absence proof), so the local lower owner
is the existing OpenBSD net80211 management-frame send/recv/timeout
machinery operating on `IEEE80211_FC0_SUBTYPE_REASSOC_REQ`.

## Local mapping

`AirportItlwmSkywalkInterface::setWCL_REASSOC` is the producer entry
point. It validates the request payload, snapshots the recovered
reassociation request, and delegates to
`ieee80211_send_mgmt(ic, ic_bss, IEEE80211_FC0_SUBTYPE_REASSOC_REQ, …)`
as the lower owner. The producer never publishes a terminal selector
itself; pre-send abandonment is reported through the producer's
synchronous `IOReturn` only.

Driver-private state in `struct ieee80211com` carries:

- `ic_wcl_reassoc_owner_active` — set to 1 by the producer when the
  owner is opened; cleared back to 0 by the gated terminal helpers.
- `ic_wcl_reassoc_owner_last_leaf` — tracks producer/lower-owner
  progression (`SETUP`, `REASSOC_REQ_SENT`, `REASSOC_REQ_SEND_FAIL`,
  `REASSOC_REQ_TIMEOUT`).

## Post-send gate

`ieee80211_wcl_reassoc_leaf_is_post_send(leaf)` returns true only for
leaves that prove the lower owner has reached at least the
`REASSOC_REQ_SENT` stage (or one of the post-send-attempt failure
leaves `REASSOC_REQ_SEND_FAIL` / `REASSOC_REQ_TIMEOUT`). The gate is
consulted by the two terminal-publication helpers:

- `ieee80211_wcl_reassoc_post_success(ic)` — emits
  `IEEE80211_EVT_WCL_REASSOC_DONE`, which the controller-side
  `eventHandler` translates into selector `0x49` length 8 with
  `{status=0, reason=0}` payload.
- `ieee80211_wcl_reassoc_post_failure(ic, code)` — emits
  `IEEE80211_EVT_WCL_REASSOC_FAIL`, which the controller-side
  `eventHandler` translates into selector `0xcf` length 4 with the
  caller-supplied nonzero failure code (zero is rewritten to `1` to
  preserve the reference invariant that the failure carrier is never
  empty).

Pre-send abandonment (bgscan candidate loss, switch-prep
allocation/deauth failure, leave-while-not-yet-sent, generic
DriverReset records, AP/APSTA, or userspace shims) cannot reach either
helper because the gate suppresses the publish call. The owner state
is closed by clearing `ic_wcl_reassoc_owner_active` directly when the
producer aborts; no terminal selector is fired in that case.

## Forbidden synthesis paths

The recovered Apple contract forbids terminal selector publication from:

- bgscan completion or same-AP scan completion;
- `setWCL_LEAVE_NETWORK`, linkdown, reportDown, or any STA-leave path;
- AP/APSTA producer paths (`task5` carve-out: AP/APSTA remains future
  work and must not be used to publish STA reassociation terminal
  events);
- userspace shims, Apple80211 compatibility/export layers, CoreWLAN /
  CoreWiFi consumer layers (`task4` boundary);
- generic DriverReset / table_base records (`task6` carve-out:
  DriverReset may invalidate an outstanding lower owner that already
  reached `REASSOC_REQ_SENT`, but it cannot substitute terminal
  publication for never-sent producers).

The local code does not name or model an Intel firmware
`WLC_REASSOC 0x440006` command; `task3` records the absence proof for
that surface across scan, STA, MLME, RX, time-event, watchdog, and
MAC-context command families.

## Residual scope

Stage 1 establishes the contract surface and wires it at the producer
edge (`setWCL_REASSOC` opens the owner; on synchronous send the leaf
becomes `REASSOC_REQ_SENT`; on synchronous send failure the leaf
becomes `REASSOC_REQ_SEND_FAIL` and `ieee80211_wcl_reassoc_post_failure`
is called).

Wiring of the remaining post-send publication edges remains separate
work and is intentionally not in this Stage 1 claim:

- reassoc-response RX (`ieee80211_recv_assoc_resp` for `reassoc=1`):
  call `ieee80211_wcl_reassoc_post_success` on
  `IEEE80211_STATUS_SUCCESS`; call
  `ieee80211_wcl_reassoc_post_failure(status)` on non-success status
  or response discard;
- management-frame timeout (`ieee80211_watchdog`): call
  `ieee80211_wcl_reassoc_post_failure(ETIMEDOUT)` only when
  `ic_wcl_reassoc_owner_last_leaf == REASSOC_REQ_SENT`;
- reset invalidation that follows `REASSOC_REQ_SENT`: call
  `ieee80211_wcl_reassoc_post_failure(ECANCELED)` from the reset path
  with the same gate.

Each of those wiring steps targets a single recovered contract edge
and can be reviewed independently; the post-send gate already in place
mechanically prevents synthesis from the forbidden sources during the
incremental work.
