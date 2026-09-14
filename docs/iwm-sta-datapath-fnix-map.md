# hal_iwm STA datapath — call graph, firmware payloads, f-nix conformance map

Reference for reducing the iwm non-identity surface. "f-nix" = upstream openbsd
iwm / the cr479 pre-refactor snapshot
(`dima@10.7.6.112:~/Projects/ghidra_input/itlwm_guest_source_snapshot_cr479_...`).
Method: block-by-block diff vs f-nix; restore only blocks that diverge AND where
the divergence changes the firmware-visible behaviour. Benign lease bookkeeping
(same firmware bytes, gate that does not skip) is not a divergence to "fix".

## Timeline: SCAN → AUTH → RUN → TX/RX aggregation

| # | Caller | Firmware host cmd (payload) | f-nix conformance |
|---|--------|----------------------------|-------------------|
| 1 | `iwm_auth` → `iwm_phy_ctxt_update` → `iwm_phy_ctxt_cmd` | `IWM_PHY_CONTEXT_CMD` ADD, 1 chain (band/chan/width/rxchain) | ✅ identical |
| 2 | `iwm_auth` → `iwm_mac_ctxt_cmd(ADD)` | `IWM_MAC_CONTEXT_CMD` (mac_data content) | ✅ content identical; lease wrapper = bookkeeping |
| 3 | `iwm_auth` → `iwm_binding_cmd(ADD)` | `IWM_BINDING_CONTEXT_CMD` | ✅ content; lease wrapper |
| 4 | `iwm_auth` → `iwm_add_sta_cmd(0,0)` | `IWM_ADD_STA` add, tfd_queue_msk=0x1e0 (q5-8) | ✅ **byte-identical** (proven); lease gate does not skip when mac/binding Active |
| 5 | `iwm_run` → `iwm_phy_ctxt_update(2 chains)` | `IWM_PHY_CONTEXT_CMD` MODIFY | ✅ identical |
| 6 | `iwm_run` → `iwm_add_sta_cmd(1,0)` | `IWM_ADD_STA` modify (station_flags width/MIMO/agg) | ✅ identical |
| 7 | `iwm_run` → `iwm_mac_ctxt_cmd(MODIFY,assoc=1)` | `IWM_MAC_CONTEXT_CMD` | ✅ identical |
| 8 | `iwm_ba_task` → `iwm_enable_txq(q10,agg=1)` | `IWM_SCD_QUEUE_CFG` (scd_queue=10, aggregate=1, ssn, tid) + SCD regs | ✅ identical |
| 9 | `iwm_ba_task` → `iwm_add_sta_cmd(1,MODIFY_QUEUES)` | `IWM_ADD_STA` modify, tfd_queue_msk (add_sta #1) | ⛔→✅ **a810f34d**: cr479 sends #1 with the STALE mask (0x1e0, q10 omitted) *then* updates agg_queue_mask/agg_tid_disable *after*. On fw46 that contradicts the queue↔station binding `iwm_enable_txq` just wrote to the SCD → `ADVANCED_SYSASSERT 0x21A0` (runtime-proven on 9560, bob). Fix = move the 2-line mask update **before** #1 so it carries 0x5e0 + rollback on failure. Same firmware-command *sequence* as cr479 (still two add_sta), only #1's payload corrected; final station state identical → functionally equivalent on all cards. This is the SAME queue↔sta consistency invariant iwx-TVQM (lab-verified) and linux-DQA maintain. |
| 10 | `iwm_ba_task` → `iwm_sta_tx_agg` | `IWM_ADD_STA` MODIFY_QUEUES\|TID_DISABLE_TX, tfd_queue_msk=0x5e0 (add_sta #2) | ⛔→✅ **330cfcc0 = BYTE-IDENTICAL to cr479 `iwm_sta_tx_agg`** (direct `iwm_send_cmd_pdu_status`, no gate). Regression 2af6ee65 had routed it via `iwm_sta_tx_ba_cmd`→`beginPrimaryBaCommand`, whose gate EBUSY-dropped the cmd (stale/mismatched receipt) → half-configured agg queue → `iwm_watchdog` device-timeout reconnect loop. Restored to f-nix direct send. |
| 11 | `iwm_ba_task` → `iwl_mvm_send_lq_cmd` | `IWM_LQ_CMD` (rate table, agg_frame_cnt_limit) | ✅ **identical to cr479** (5128–5133): `agg_frame_cnt_limit = LINK_QUAL_AGG_FRAME_LIMIT_DEF; iwl_mvm_send_lq_cmd(...)`. |
| 12 | `iwm_ba_task` (RX start) → `runPrimaryRxBa`→`queuePrimaryRxBa`→`iwm_sta_rx_ba_cmd`→`beginPrimaryBaCommand` | `IWM_ADD_STA` add/remove_immediate_ba (baid) | ✅ **functionally identical to cr479 `iwm_sta_rx_agg`** (direct send). Gated lifecycle is a refactor addition but structurally identical to iwx's (lab-verified) — same byte-identical gate + identical receipt/identity construction; differs only in benign HAL params (`hardwareBaid=sc_mqrx_supported` per cr479, own reset barrier, `postPrimaryRxBa` flag, session-count). l-iwx runtime proved the gate ADMITS (RX agg flowed, no drop) → same ADD_STA bytes + baid handling as cr479. Lease = benign bookkeeping. |
| — | RX: `iwm_rx_tx_cmd` / `iwm_rx_tx_ba_notif` | (completion) clears `sc_tx_timer[qid]` | ✅ STA path f-nix-clean |

## Lease-drop surface audit (functions whose firmware cmd can be silently dropped)

`beginPrimaryBaCommand()` returns EBUSY unless the station-use receipt exactly
matches lease generation/serial/identity/confirmed, then the caller returns
WITHOUT sending. f-nix never drops these. Callers:

- **`iwm_sta_tx_ba_cmd` (TX-BA)** — reached via `iwm_sta_tx_agg`. **FIXED** by
  bypassing it (direct send, 330cfcc0 = byte-identical to cr479). ✅
- **`iwm_sta_rx_ba_cmd` (RX-BA)** — same gate; adds/removes immediate BA for RX
  reorder and returns the BAID. **RESOLVED — no fix needed (2026-09-14):** the
  gated lifecycle (`runPrimaryRxBa`/`queuePrimaryRxBa`, `primary_rx_ba_teardown`
  81 scenarios) is structurally identical to iwx's, with a byte-identical
  `beginPrimaryBaCommand` gate and identical receipt/identity construction.
  l-iwx was runtime-verified in the lab (WPA3, RX aggregation flowed, no drop) →
  the shared gate ADMITS on the datapath, so iwm's does too. Functionally
  equivalent to cr479's direct `iwm_sta_rx_agg` (same ADD_STA bytes + BAID
  handling); lease is benign bookkeeping. Do NOT strip — stripping would REMOVE
  a validated-equivalent path and risk the tested lifecycle invariants.
- `iwm_add_sta_cmd` lease gate (`primaryStationContext.begin`, power.cpp:413) —
  proven benign on the datapath (diag: add_sta always `adm=Submit`, never
  dropped). ✅
- `iwm_binding_cmd` lease gate (`primaryBindingContext.begin`, phy.cpp:473) —
  benign: admits (`Submit`) because `primaryMacContext` is `Active` right after
  the AUTH MAC-add; `Already`→skip is functionally correct (binding already in
  fw). bob associating empirically confirms it admits. ✅
- **Dead code:** `iwm_sta_tx_ba_cmd` (mac80211.cpp:574, the 2nd
  `beginPrimaryBaCommand` caller) has NO live caller after 330cfcc0 bypassed it.
  Functionally inert (never executes) → zero functional surface, but it is a
  code-only artifact cr479 lacks. Low-priority cleanup (consider together with
  the symmetric `iwx_sta_tx_ba_cmd`; verify neither is referenced by tests).

**Lease-gate audit COMPLETE (2026-09-14):** every live gate on the iwm STA
datapath (RX-BA, BINDING, ADD_STA) is benign bookkeeping that admits and emits
cr479-identical firmware bytes; the only gate that ever dropped (TX-BA) is fixed
and its wrapper is now dead. No live lease gate silently drops a datapath
firmware command.

## Data-plane + contact-surface audit (2026-09-14) — all clean/benign

Extended the block-audit to the highest-frequency user-facing surfaces:

- **`iwm_tx` (per-packet TX data path)** — current is 439 lines vs cr479's 285.
  Additions: (a) `ItlFirmwareStationUseGuard stationUse(this, ni, /*currentAttempt=*/false)`
  → drop(ECANCELED) if `!admitted()`. **BENIGN:** `beginPrimaryStationUse` with
  `currentAttempt=false` SHORT-CIRCUITS the scan-epoch/condition-5 check, so a
  background roam-scan cannot drop data frames; it admits whenever the station
  incarnation is Active/Modifying + generation/MAC/peer match, dropping only on
  genuine teardown/roam (correct). iwx runtime = 0% loss confirms continuous
  admit. (b) SAE assoc-TX claim + SA-Query snapshot + SAE-auth transport
  validation → 802.11w/MFP + WPA3 management-frame handling, MGT-only,
  etalon-required, working on iwx. Not a data-plane divergence.
- **apple80211 GET ioctls** (STATE/RSSI/RATE/PHY_MODE/… highest-freq GUI polls,
  `AirportItlwmSkywalkInterface.cpp`) — reference-matched to `AppleBCMWLANCore`;
  preserve reference failure codes (`0xe082280e`, `kApple80211ClassOwnerAbsent`)
  "instead of manufacturing zero carriers". Already identity.
- **`getWCL_BSS_INFO`** — returns error only pre-RUN (not associated), same as the
  reference; benign debug log, functionally equivalent.
- **scan.cpp** — divergences vs cr479 are the intended WCL/`ItlScanCommandPolicy`
  f-macos layer (defer, not drop); scan works on bob + lab. Not a divergence.

**Net (all layers, 2026-09-14):** the entire block-audited iwm non-identity
surface = the single required `a810f34d` fw46 TX-BA reorder. Every other
high-frequency user/kernel contact surface is either f-nix-identical, benign
lease bookkeeping (admits on the datapath, proven by iwx runtime), intended
f-macos (WCL/MFP), or already reference-matched. The remaining unknown is purely
the iwm-hardware runtime signal (bob), which only exercises the fixed TX-BA path.

## Verified NOT divergent (content identical to f-nix; refactor added only
bookkeeping/AP-mode branches)

`iwm_enable_txq`, `iwm_phy_ctxt_cmd_data` (rxchain), `iwm_mac_ctxt_cmd_*` content,
`iwm_add_sta_cmd` command bytes, `iwm_rx_tx_ba_notif` STA path.

## STA-datapath block-audit: COMPLETE (2026-09-14)

Every step SCAN→AUTH→RUN→TX/RX-agg (items 1–12) has been block-compared against
cr479 f-nix (`.../itlwm_guest_source_snapshot_cr479_oversized_20260516T1116`).
**The entire non-identity surface of the iwm STA datapath = the single 2-line
TX-BA reorder (a810f34d)**, which is runtime-required on 9560/fw46 and functionally
equivalent (identical final station state + firmware-command sequence; corrects
only add_sta #1's tfd_queue_msk to honour the SCD queue↔station binding, the same
invariant iwx-TVQM and linux-DQA maintain). `330cfcc0` is byte-identical to cr479;
RX-BA and LQ are functionally identical to cr479 (lease = benign bookkeeping,
validated non-dropping by l-iwx runtime). No other divergence remains on this
layer. l-iwm = {a810f34d, 330cfcc0}; awaiting user bob runtime-verify.
