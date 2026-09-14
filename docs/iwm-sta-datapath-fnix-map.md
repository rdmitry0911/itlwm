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
| 9 | `iwm_ba_task` → `iwm_add_sta_cmd(1,MODIFY_QUEUES)` | `IWM_ADD_STA` modify, tfd_queue_msk=0x1e0 (add_sta #1) | ✅ identical (mask update after, per f-nix; earlier mask-crutch reverted) |
| 10 | `iwm_ba_task` → `iwm_sta_tx_agg` | `IWM_ADD_STA` MODIFY_QUEUES\|TID_DISABLE_TX, tfd_queue_msk=0x5e0 (add_sta #2) | ⛔→✅ **FIXED (330cfcc0)**: was routed via `iwm_sta_tx_ba_cmd`→`beginPrimaryBaCommand` whose EBUSY admission DROPPED the cmd → half-configured agg queue → no completion → `iwm_watchdog` device-timeout reconnect loop. Now sends directly like f-nix. |
| 11 | `iwm_ba_task` → `iwl_mvm_send_lq_cmd` | `IWM_LQ_CMD` (rate table, agg_frame_cnt_limit) | audit pending |
| — | RX: `iwm_rx_tx_cmd` / `iwm_rx_tx_ba_notif` | (completion) clears `sc_tx_timer[qid]` | ✅ STA path f-nix-clean |

## Lease-drop surface audit (functions whose firmware cmd can be silently dropped)

`beginPrimaryBaCommand()` returns EBUSY unless the station-use receipt exactly
matches lease generation/serial/identity/confirmed, then the caller returns
WITHOUT sending. f-nix never drops these. Callers:

- **`iwm_sta_tx_ba_cmd` (TX-BA)** — reached via `iwm_sta_tx_agg`. **FIXED** by
  bypassing it (direct send). ✅
- **`iwm_sta_rx_ba_cmd` (RX-BA)** — same gate; adds/removes immediate BA for RX
  reorder and returns the BAID. **CANDIDATE**, NOT yet fixed: the lease here is
  load-bearing (part of the tested immutable RX-BA lifecycle
  `runPrimaryRxBa`/`queuePrimaryRxBa`, `primary_rx_ba_teardown` 81 scenarios).
  Do not strip blindly. Verify at runtime whether it actually EBUSY-drops the RX
  session on the lab rig before restoring; if it does, fix while preserving BAID
  and lifecycle invariants.
- `iwm_add_sta_cmd` lease gate (`primaryStationContext.begin`) — proven benign on
  the datapath (diag: add_sta always `adm=Submit`, never dropped). ✅

## Verified NOT divergent (content identical to f-nix; refactor added only
bookkeeping/AP-mode branches)

`iwm_enable_txq`, `iwm_phy_ctxt_cmd_data` (rxchain), `iwm_mac_ctxt_cmd_*` content,
`iwm_add_sta_cmd` command bytes, `iwm_rx_tx_ba_notif` STA path.
