# AP/GO HAL Surface on `ItlHalService` — Recovered Contracts and Donor Mapping

## Purpose

This document describes the fail-closed AP/GO HAL surface added to
`include/HAL/ItlHalService.hpp` and the recovered Apple
AppleBCMWLAN / Intel iwlwifi / OpenBSD net80211 HostAP contracts each
HAL method represents. It is the local implementation reference for
the host APSTA owner skeleton (`AirportItlwmAPSTAInterface`) and the
future net80211 HostAP enablement work, both of which need a single,
stable HAL contract surface to talk to the lower backend.

This bounded slice introduces only the contract surface. No HAL
backend (`ItlIwx`, `ItlIwm`) overrides any of the new methods; the
default fail-closed return values stay in effect, so observable AP
behaviour is unchanged.

## Recovered Apple owner contract

`AppleBCMWLANIO80211APSTAInterface` (the recovered Apple APSTA owner
that the host `AirportItlwmAPSTAInterface` skeleton mirrors) talks to
its lower backend through a small set of firmware-command edges. Each
edge has a clear ownership boundary: the APSTA owner builds the
parameter block from net80211 / SoftAP state, calls into the lower
backend, and waits for the firmware response (or the lower-backend
async completion cookie).

The recovered owner needs the following lower-backend capabilities to
transition from `OWNER_ALLOCATED` through `AP_STARTING_LOWER` to
`AP_UP`:

- AP MAC context add/update with BSSID, channel, beacon interval, DTIM
  period, and station count limits.
- Beacon and probe-response template upload and live update.
- AP station add/remove with auth/assoc state, power-save flags, and
  cipher suite.
- AP pairwise/group key install with key index, cipher, key bytes, and
  RSC.
- Channel switch announcement (CSA) trigger and channel context
  update.
- AP firmware event conversion back into APSTA owner state and
  station-table mutations.

These are the same edges the recovered Apple FSM
(`commit-approval/offline_results/itlwm_ap_layer_remaining_debt_closure_result_2026_05_09/FSM_LIFECYCLE.md`)
documents and the same edges the AP/APSTA parity verdict
(`commit-approval/status/AUDITOR_VERDICT_AP_LAYER_PARITY_CLOSURE_20260509T173840_0300.md`)
authorises for local implementation.

## Local HAL surface

The HAL methods added to `ItlHalService` map 1:1 to those Apple
edges, expressed as project-owned local types. Each method has a
default fail-closed implementation that returns
`false` / `kIOReturnUnsupported`.

| HAL method | Recovered Apple owner edge | Linux iwlwifi / mac80211 donor | OpenBSD net80211 HostAP donor |
| --- | --- | --- | --- |
| `bool supportsAPMode() const` | Capability gate before any AP/GO firmware command is allowed | NIC family / firmware capability bit (`mvm->fw->capa` / `IWL_UCODE_TLV_CAPA_AP_MODE` and family tables) | `ic->ic_caps & IEEE80211_C_HOSTAP` capability bit on the controller |
| `IOReturn startAPMode(const ItlHalApConfig *)` | AP MAC context add + binding + queue programming + BSSID/channel/beacon-interval/DTIM-period/maxStations install | `iwl_mvm_mac_ctxt_add` / `iwl_mvm_binding_add_vif` / `iwl_mvm_send_add_bcast_sta` / `iwl_mvm_start_ap_ibss` | `ieee80211_create_ibss` AP path, `ieee80211_chan2mode`, `ieee80211_setup_phy` |
| `IOReturn stopAPMode()` | AP firmware teardown, queue release, MAC context delete | `iwl_mvm_stop_ap_ibss` / `iwl_mvm_mac_ctxt_remove` / `iwl_mvm_send_rm_bcast_sta` | `ieee80211_dot11hdr_quiesce`, `ieee80211_node_cleanup` |
| `IOReturn updateAPBeacon(const void *, size_t, uint16_t, uint8_t)` | Beacon and probe-response template upload, beacon-interval / DTIM-period update | `iwl_mvm_mac_ctxt_send_beacon` / `iwl_mvm_mac_beacon_loaded` / `iwl_mvm_beacon_update_template` | `ieee80211_beacon_alloc`, `ieee80211_beacon_update`, `ieee80211_iv_to_seq` |
| `IOReturn setAPKey(const ItlHalApKey *)` | AP pairwise/group key install, RSC delivery, AP-side rekey | `iwl_mvm_mac_ctxt_send_aux_cmd` key install path / `iwl_mvm_send_aux_sta` / `iwl_mvm_set_sta_key` | `ieee80211_crypto_setkey`, `ieee80211_crypto_decap`, `ieee80211_keymap` |
| `IOReturn triggerAPCSA(const ItlHalApCSA *)` | CSA programming, channel context switch, beacon CSA IE update | `iwl_mvm_mac_ctxt_send_chan_switch` / `iwl_mvm_csa_count_down` / `iwl_mvm_chsw_via_isr` | `ieee80211_send_csa_action`, `ieee80211_chan_switch_apr`, `ieee80211_csa_check_complete` |
| `IOReturn sendAPStationCommand(const ItlHalApStationCommand *)` | AP station auth/assoc/deauth/disassoc/power-save commands | `iwl_mvm_send_add_sta` / `iwl_mvm_send_rm_sta` / `iwl_mvm_send_sta_modify` / `iwl_mvm_set_sta_pm` | `ieee80211_node_join`, `ieee80211_node_leave`, `ieee80211_node_sup_negotiate`, `ieee80211_node_pwrsave` |

The above mapping is the route plan for the future Intel AP/GO
backend implementation. Each row is a separate implementation slice
that requires its own decomp, recovered-contract, and Stage 1
review. The donor sources are the operator-prechecked AP local
implementation-gap package
(`commit-approval/offline_results/itlwm_ap_local_impl_gap_closure_stage1_2026_05_09/IMPLEMENTATION_ROUTE.md`,
archive sha256 `1b72d3fc249d2bcef20e9557fc6ac8665fc7cf2cea9d692adf1951e7bf83f670`) and the
auditor-verified AP/APSTA parity bundle
(`commit-approval/offline_results/itlwm_ap_layer_remaining_debt_closure_result_2026_05_09.tar.zst`).

## Parameter shapes

Each parameter struct is the canonical local representation of the
recovered Apple AP/GO firmware command parameters and matches the
shape that net80211 / OpenBSD HostAP code already uses internally.

### `ItlHalApConfig`

The AP MAC context. Owned by the APSTA owner; passed by const pointer
into `startAPMode`. The HAL borrows the pointer for the duration of
the call only.

- `bssid[IEEE80211_ADDR_LEN]`: AP BSSID.
- `channel`: operating channel number.
- `beaconInterval`: beacon interval in TU (per Apple recovered constants
  `kAirportItlwmAPSTAHostApBeaconIntervalNormal=0x12c` and
  `kAirportItlwmAPSTAHostApBeaconIntervalShort=0x64`).
- `dtimPeriod`: DTIM period (per Apple recovered constant
  `kAirportItlwmAPSTAInitSoftAPDefaultDtimPeriod=1`).
- `maxStations`: maximum allowed associations (per Apple recovered
  selector 508 `MIS_MAX_STA`).
- `beaconTemplate` / `beaconTemplateLength`: borrowed beacon template
  bytes for upload to firmware.

### `ItlHalApKey`

AP pairwise/group key. Owned by the APSTA owner / net80211 keymap;
passed by const pointer into `setAPKey`.

- `station`: station MAC (NULL for group key).
- `keyIndex`: WEP-style key index 0..3 or AP-side group key index.
- `cipher`: net80211 cipher suite (`IEEE80211_CIPHER_*`).
- `keyData` / `keyLength`: key bytes.
- `rsc` / `rscLength`: replay sequence counter (group key only).

### `ItlHalApCSA`

Channel switch announcement. Owned by the APSTA owner; passed by
const pointer into `triggerAPCSA`.

- `channel`: target channel number.
- `count`: CSA beacon countdown.

### `ItlHalApStationCommand`

Station-table command. Owned by the APSTA owner; passed by const
pointer into `sendAPStationCommand`.

- `command`: station command opcode (auth / assoc / deauth / disassoc
  / power-save). Matches the recovered Apple owner station-event
  publication contract (separate from the WCL_REASSOC owner contract
  introduced by CR-446).
- `station`: station MAC.
- `flags`: command-specific flags (e.g., reason code, capability bits).

## Lifetime invariants

- All parameter pointers are borrowed for the duration of the call.
  Backends must not retain pointers, must not free them, and must
  complete any necessary copy before returning.
- A backend that returns `true` from `supportsAPMode()` must also
  implement `startAPMode` / `stopAPMode` and at least the command
  methods needed by the AP/GO bring-up flow it advertises (beacon
  update, AP key install, station add/remove). `triggerAPCSA` may
  return `kIOReturnUnsupported` for backends that do not advertise
  CSA capability; CSA is not required for the basic AP-up edge.
- `stopAPMode()` must be re-entry-safe: calling it while AP mode is
  not started must succeed without side effects.

## Out of scope for this slice

- No `ItlIwx` or `ItlIwm` HAL backend overrides any of the new
  methods. The default fail-closed return values stay in effect.
- The `AirportItlwmAPSTAInterface::isLowerBackendReady()` gate is
  unchanged and still returns `false`. Wiring it to consult
  `supportsAPMode()` requires a HAL service pointer reachable from
  the APSTA owner; that wiring is a separate slice.
- No net80211 HostAP enablement, no `IEEE80211_STA_ONLY` change, no
  HOSTAP panic surface change, no AP firmware command
  implementation, no station-event producer bridge, no AP-up
  transition, no beaconing, no AP client association, no DHCP, no
  traffic, no peer-cache publication, no station-table mutation, no
  WCL_REASSOC merge.

## Self-check anchors

- Recovered Apple owner FSM, AP-up transition, station-table layout,
  station-event publication: auditor-verified offline bundle
  `commit-approval/offline_results/itlwm_ap_layer_remaining_debt_closure_result_2026_05_09.tar.zst`.
- Auditor verdict closing Apple AP/APSTA reference debt:
  `commit-approval/status/AUDITOR_VERDICT_AP_LAYER_PARITY_CLOSURE_20260509T173840_0300.md`
  sha256 `2ae7a49d627fb2b328ff1280478a273e07316bcf9c28466ea4e0ba3979838e56`.
- AP local implementation-gap donor package:
  `commit-approval/offline_results/itlwm_ap_local_impl_gap_closure_stage1_2026_05_09/`,
  archive sha256
  `1b72d3fc249d2bcef20e9557fc6ac8665fc7cf2cea9d692adf1951e7bf83f670`.
- iwx HOSTAP preflight panic at `itlwm/hal_iwx/ItlIwx.cpp:8428`
  (untouched).
- iwm HOSTAP preflight panic at `itlwm/hal_iwm/mac80211.cpp:2019`
  (untouched).
- `IEEE80211_STA_ONLY` mask of `IEEE80211_M_HOSTAP` at
  `itl80211/openbsd/net80211/ieee80211_var.h:259-268` (untouched).
- Host APSTA owner skeleton committed at
  `e7a3b0547837fcad976e9e8ecc61ef09fc298ff5`
  (`AirportItlwm/AirportItlwmAPSTAInterface.hpp`).
