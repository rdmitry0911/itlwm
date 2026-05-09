# Intel iwx / iwm AP/GO Backend Capability Census

## Purpose

This document is a bounded research result. It maps what the local
Intel iwx / iwm HALs already provide, what the OpenBSD net80211 layer
already provides under `IEEE80211_STA_ONLY`, and what the recovered
Apple AP/APSTA owner contract plus the auditor-approved AP/GO HAL
surface (`include/HAL/ItlHalService.hpp` after commit
`a768896bce57c66884f9bb738de18fb248776942`) require for a non-trivial
backend implementation. The output is a per-method blocker list and a
recommended next code route.

No code is added or modified by this slice; the only diff is this
documentation file under `docs/reference/`. The host APSTA owner
skeleton (`AirportItlwmAPSTAInterface`) and the fail-closed HAL surface
(`ItlHalService`) keep their default behaviour: `isLowerBackendReady()`
returns `false`, `supportsAPMode()` returns `false`, the command
methods return `kIOReturnUnsupported`, role-7 / SoftAP acquisition
still fails closed at `0xe00002bd`, and role-6 / AWDL still fails
closed at `0xe00002bd`.

## What the local iwx firmware command interface already supports

The Intel iwx firmware command interface (`itlwm/hal_iwx/if_iwxreg.h`)
already exposes the building blocks needed for AP / GO operation. The
local C++ wrapper does not call any of them in AP / GO context.

### Firmware MAC types (`if_iwxreg.h:4509-4532` — doc comment 4509-4521; defines 4522-4532; `IWX_FW_MAC_TYPE_GO` at line 4530)

```
#define IWX_FW_MAC_TYPE_FIRST        1
#define IWX_FW_MAC_TYPE_AUX          IWX_FW_MAC_TYPE_FIRST
#define IWX_FW_MAC_TYPE_LISTENER     2
#define IWX_FW_MAC_TYPE_PIBSS        3
#define IWX_FW_MAC_TYPE_IBSS         4
#define IWX_FW_MAC_TYPE_BSS_STA      5
#define IWX_FW_MAC_TYPE_P2P_DEVICE   6
#define IWX_FW_MAC_TYPE_P2P_STA      7
#define IWX_FW_MAC_TYPE_GO           8
#define IWX_FW_MAC_TYPE_TEST         9
#define IWX_FW_MAC_TYPE_MAX          IWX_FW_MAC_TYPE_TEST
```

`IWX_FW_MAC_TYPE_GO` (value `8`) is the firmware-level MAC context type
for P2P GO and is reused by mac80211/iwlwifi for the SoftAP / GO bring-up
path. The local wrapper at `itlwm/hal_iwx/ItlIwx.cpp:8325-8346`
(`iwx_mac_ctxt_cmd_common`) only maps two opmodes:

```
if (ic->ic_opmode == IEEE80211_M_MONITOR)
    cmd->mac_type = htole32(IWX_FW_MAC_TYPE_LISTENER);
else if (ic->ic_opmode == IEEE80211_M_STA)
    cmd->mac_type = htole32(IWX_FW_MAC_TYPE_BSS_STA);
else
    panic("unsupported operating mode %d\n", ic->ic_opmode);
```

The panic at `:8346` is the iwx HOSTAP preflight guard that the
auditor verdict explicitly preserves until a full replacement path is
landed. To make the firmware accept AP / GO MAC contexts, this guard
must be replaced with an AP-mode arm that maps `IEEE80211_M_HOSTAP` to
`IWX_FW_MAC_TYPE_GO` and sets the AP-specific command fields. That
replacement is its own implementation slice and is not in scope here.

### Firmware command IDs related to AP / GO bring-up

- `IWX_MAC_CONTEXT_CMD = 0x28` (`if_iwxreg.h:1896`) — already used by
  `iwx_mac_ctxt_cmd_common` for STA / monitor MAC context. AP context
  reuses the same command ID with the AP / GO MAC type.
- `IWX_BEACON_TEMPLATE_CMD = 0x91` (`if_iwxreg.h:1924`) — beacon and
  probe-response template upload. **Not used anywhere in the local
  code.**
- `IWX_BEACON_NOTIFICATION = 0x90` (`if_iwxreg.h:1923`) — async beacon
  status notification from firmware. Not subscribed by the local code.
- `IWX_TIME_EVENT_CMD = 0x29` (`if_iwxreg.h:1897`) — time-event scheduler
  used for STA roaming today; CSA would reuse it on the AP side.
- `IWX_ADD_STA = 0x18` and `IWX_REMOVE_STA = 0x19` — already used for
  STA-side single-peer add (the AP we connect to). For AP / GO, the
  same commands add client stations with `STA_AUTH` / `STA_ASSOC_AP`
  flags and the AP-side STA flags pattern from iwlwifi.

### Firmware-level AP / GO TX support

`IWX_TX_CMD = 0x1c` (`if_iwxreg.h`) already supports the TX descriptor
flags and the rate scaling that AP / GO transmit needs. The driver TX
path on iwx is already used for STA traffic and TX of EAPOL during
WPA2 4-way handshake. AP-side TX of the broadcast / multicast queue
and the per-STA queues would reuse the same descriptor format with
different `lookup_assoc_id` / `bssid_idx` values from the AP MAC
context.

## What the iwm firmware command interface supports

The Intel iwm firmware (older 7000 / 8000 family) has a similar shape
in `itlwm/hal_iwm/if_iwmreg.h`:

- `IWM_FW_MAC_TYPE_GO` exists in the firmware command interface.
- `iwm_mac_ctxt_cmd_common` at `itlwm/hal_iwm/mac80211.cpp:1998-2016`
  has the same two-opmode if-else and the same panic guard at line
  2016.

The iwm 7000 / 8000 family is the historical baseline for Apple's
in-tree AppleBCMWLAN equivalents; vendor / family advertisement is
required to know which iwm devices actually support AP / GO firmware
commands. iwlwifi in Linux marks AP support per-NIC family in
`iwl_cfg`. The local iwm has no such per-family table and would need
one before exposing `supportsAPMode() == true` on iwm.

## What OpenBSD net80211 already provides (gated behind STA_ONLY)

The local OpenBSD net80211 build defines `IEEE80211_STA_ONLY`
unconditionally at the top of `itl80211/openbsd/net80211/ieee80211_var.h`.
The `IEEE80211_STA_ONLY` mask at `ieee80211_var.h:259-267` (mask
itself at lines 261-265) removes
`IEEE80211_M_IBSS`, `IEEE80211_M_AHDEMO`, and `IEEE80211_M_HOSTAP` from
the `ieee80211_opmode` enum and gates a large body of HostAP / IBSS
code in:

- `itl80211/openbsd/net80211/ieee80211_node.c` — node alloc / free for
  HostAP-managed clients (`ieee80211_node_join`, `ieee80211_node_leave`,
  per-AID slot management, TIM bitmap update).
- `itl80211/openbsd/net80211/ieee80211_input.c` — auth / assoc / reassoc
  / disassoc / deauth frame processing on the AP side.
- `itl80211/openbsd/net80211/ieee80211_output.c` — beacon / probe-
  response / auth response / assoc response frame composition.
  `ieee80211_beacon_alloc` exists locally at
  `itl80211/openbsd/net80211/ieee80211_output.c:2267`. The local port
  has no `ieee80211_beacon_update` helper; live beacon updates would
  need to be ported from upstream OpenBSD or implemented as a
  handwritten beacon-template builder.
- `itl80211/openbsd/net80211/ieee80211_proto.c` — HostAP state machine,
  `IEEE80211_S_RUN` AP path, `ieee80211_new_state` AP transitions.
- `itl80211/openbsd/net80211/ieee80211_crypto.c` — group key install,
  pairwise rekey, WPA2 4-way handshake from the authenticator side.

Removing `IEEE80211_STA_ONLY` is a non-local change: every translation
unit that depends on the `ieee80211_opmode` enum and the gated
prototypes recompiles, and the project's STA-only invariants must be
re-validated. The auditor verdict explicitly preserves `STA_ONLY` until
a complete replacement path is landed.

## What the recovered Apple AP/APSTA owner contract requires

The auditor-verified AP/APSTA parity bundle
(`commit-approval/offline_results/itlwm_ap_layer_remaining_debt_closure_result_2026_05_09.tar.zst`,
internal MANIFEST.sha256 verified) and the AP/APSTA parity verdict
(`commit-approval/status/AUDITOR_VERDICT_AP_LAYER_PARITY_CLOSURE_20260509T173840_0300.md`,
sha256 `2ae7a49d627fb2b328ff1280478a273e07316bcf9c28466ea4e0ba3979838e56`)
document the recovered Apple `AppleBCMWLANIO80211APSTAInterface` AP / GO
firmware command edges and the lifecycle from `OWNER_ALLOCATED`
through `AP_STARTING_LOWER` to `AP_UP`. Each edge has a clear
ownership boundary the AP/GO HAL surface (added in CR-451 commit
`a768896b`) is intended to cover.

## Per-method blocker list

This section is the actionable output of the census. Each entry names
the auditor-approved HAL method on `ItlHalService`, the recovered
Apple owner edge it represents, the concrete local blockers that
prevent a non-trivial implementation, and the **smallest next code
slice** that would unblock it. None of those slices is requested here;
this Stage 1 only documents the route.

### `bool supportsAPMode() const`

- **Recovered Apple edge**: capability gate before any AP / GO
  firmware command is allowed.
- **Linux iwlwifi donor**: per-NIC `iwl_cfg` table flag and firmware
  capability TLV (e.g., `IWL_UCODE_TLV_CAPA_AP_MODE` / NIC family bit).
- **OpenBSD net80211 donor**: `ic->ic_caps & IEEE80211_C_HOSTAP`.
- **Local blockers**:
  - `ItlIwx::ic->ic_caps` does not advertise `IEEE80211_C_HOSTAP`
    (the bit definition lives in `ieee80211_var.h` but the iwx HAL
    never sets it).
  - There is no per-family / per-firmware capability table for iwx
    that lists which devices have a firmware image that supports AP /
    GO MAC contexts. The closest analogue is the iwlwifi `iwl_cfg`
    family enumeration, which has no port in this project.
  - The iwm path has the same shape and the same gap.
- **Smallest next code slice (not in CR-452)**: a per-family iwx
  capability table seeded from the firmware versions actually present
  in the local checkout (`fw/iwx/*.ucode`), gated by a strict
  whitelist. `supportsAPMode()` returns `true` only on listed families
  and only after the loaded firmware advertises the AP / GO TLV.

### `IOReturn startAPMode(const ItlHalApConfig *config)`

- **Recovered Apple edge**: AP MAC context add + binding + queue
  programming + BSSID / channel / beacon-interval / DTIM-period /
  maxStations install.
- **Linux iwlwifi donor (upstream reference; not present in this tree)**:
  `iwl_mvm_mac_ctxt_add`, `iwl_mvm_binding_add_vif`,
  `iwl_mvm_send_add_bcast_sta`, `iwl_mvm_start_ap_ibss` — names from
  the upstream Linux iwlwifi `mac_ctxt.c` / `mvm.c` modules.
- **OpenBSD net80211 donor**: `ieee80211_create_ibss` exists locally
  at `itl80211/openbsd/net80211/ieee80211_node.c:935`;
  `ieee80211_chan2mode` exists locally at
  `itl80211/openbsd/net80211/ieee80211.c:1319`. The AP path of
  `ieee80211_create_ibss` is gated behind `IEEE80211_STA_ONLY` and
  cannot be exercised on the local tree without the gate change in
  slice 4.
- **Local blockers**:
  - `iwx_mac_ctxt_cmd_common` panics for `IEEE80211_M_HOSTAP`
    (`itlwm/hal_iwx/ItlIwx.cpp:8346`) and the same shape on iwm at
    `itlwm/hal_iwm/mac80211.cpp:2016`. The panic guard must be
    replaced with an AP-mode arm that emits
    `IWX_FW_MAC_TYPE_GO` / `IWM_FW_MAC_TYPE_GO` and the AP-specific
    fields (channel, AP queues, `tsf_id`, broadcast / multicast queue
    indices).
  - There is no broadcast / multicast queue allocation path on
    `ItlIwx` / `ItlIwm`. Linux iwlwifi has
    `iwl_mvm_send_add_bcast_sta` and a queue allocator (`iwl_mvm_tvqm_enable_txq`);
    no port exists.
  - There is no AP-side time-event programming
    (`iwl_mvm_start_ap_ibss` schedules an indefinite time-event for AP
    operation).
- **Smallest next code slice**: a `startAPMode` skeleton that builds
  the AP-mode `IWX_MAC_CONTEXT_CMD` payload from `ItlHalApConfig`,
  sends it, and returns the firmware status without touching the
  panic guards (the panic guards are still hit because
  `ic->ic_opmode` is not yet set to HOSTAP). This unblocks the
  payload composition while keeping the rest of the bring-up flow
  fail-closed.

### `IOReturn stopAPMode()`

- **Recovered Apple edge**: AP firmware teardown, queue release, MAC
  context delete.
- **Linux iwlwifi donor (upstream reference; not present in this tree)**:
  `iwl_mvm_stop_ap_ibss`, `iwl_mvm_mac_ctxt_remove`,
  `iwl_mvm_send_rm_bcast_sta` — names from the upstream Linux iwlwifi
  `mac_ctxt.c` / `mvm.c` modules.
- **OpenBSD net80211 donor**: `ieee80211_node_cleanup` is declared
  locally at `itl80211/openbsd/net80211/ieee80211_node.h:669` and
  called from `itl80211/openbsd/net80211/ieee80211_node.c:880`. The
  AP-side teardown sequencing (queue release, MAC context delete) is
  not provided by net80211 and must be implemented locally.
- **Local blockers**: same as `startAPMode` — `iwx_mac_ctxt_cmd_common`
  with `IWX_FW_CTXT_ACTION_REMOVE` already returns early at
  `itlwm/hal_iwx/ItlIwx.cpp:8338-8339` for any opmode (`if (action ==
  IWX_FW_CTXT_ACTION_REMOVE) return;`), so the firmware teardown path
  itself is reachable; the missing piece is the broadcast / multicast
  queue release and the AP-mode bookkeeping in
  `ItlIwx::iwx_disable_interrupts` / `iwx_stop_device`.
- **Smallest next code slice**: re-entry-safe `stopAPMode` that calls
  the existing `iwx_disable_interrupts` / `iwx_stop_device` path with
  AP-mode bookkeeping cleared. Required after `startAPMode` lands.

### `IOReturn updateAPBeacon(const void *, size_t, uint16_t, uint8_t)`

- **Recovered Apple edge**: beacon / probe-response template upload,
  beacon-interval / DTIM-period update.
- **Linux iwlwifi donor (upstream reference; not present in this tree)**:
  `iwl_mvm_mac_ctxt_send_beacon`, `iwl_mvm_mac_beacon_loaded`,
  `iwl_mvm_beacon_update_template` — names from the upstream Linux
  iwlwifi `mac_ctxt.c` / `tx.c` modules.
- **OpenBSD net80211 donor**: `ieee80211_beacon_alloc` exists locally
  at `itl80211/openbsd/net80211/ieee80211_output.c:2267`. The local
  port has no live beacon-update helper; that responsibility falls to
  the AP-mode iwx wrapper.
- **Local blockers**:
  - `IWX_BEACON_TEMPLATE_CMD = 0x91` is defined but the local code
    has no caller. A new `iwx_send_beacon_template` helper would have
    to be written; the donor mapping uses
    `iwl_mvm_mac_ctxt_send_beacon`.
  - `ieee80211_beacon_alloc` is gated behind `IEEE80211_STA_ONLY`. A
    handwritten beacon-template builder for iwx (using
    `kAirportItlwmAPSTABeaconPayloadSize=4` for the recovered
    selector + the recovered `kAirportItlwmAPSTAHostApBeaconIntervalNormal=0x12c`
    / `kAirportItlwmAPSTAInitSoftAPDefaultDtimPeriod=1`) is one route;
    enabling `ieee80211_beacon_alloc` is the other.
- **Smallest next code slice**: a beacon-template upload helper that
  takes the `ItlHalApConfig::beaconTemplate` bytes verbatim, wraps
  them in `IWX_BEACON_TEMPLATE_CMD`, and dispatches via
  `iwx_send_cmd_pdu`. Composition of the beacon payload is the
  caller's responsibility (the host APSTA owner / future net80211
  HostAP layer).

### `IOReturn setAPKey(const ItlHalApKey *key)`

- **Recovered Apple edge**: AP pairwise / group key install, RSC
  delivery.
- **Linux iwlwifi donor (upstream reference; not present in this tree)**:
  `iwl_mvm_set_sta_key`, `iwl_mvm_send_aux_sta` — names from the
  upstream Linux iwlwifi `sta.c` module.
- **OpenBSD net80211 donor**: the local port exposes
  `ieee80211_keyrun` at `itl80211/openbsd/net80211/ieee80211_proto.c:376`
  and `ieee80211_crypto_clear_groupkeys` (used in
  `ieee80211_proto.c:1503`/`1516`/`1567`); group-key install for the
  authenticator side is not present locally and would be ported from
  upstream OpenBSD.
- **Local blockers**:
  - The local key install path is `iwx_set_key` at
    `itlwm/hal_iwx/ItlIwx.cpp:9580` and `iwx_delete_key` at
    `itlwm/hal_iwx/ItlIwx.cpp:9614`, both built around
    `IWX_ADD_STA_KEY` (`itlwm/hal_iwx/ItlIwx.cpp:9609` and `:9640`).
    They are wired for STA-side single-peer keys; AP-side group keys
    and per-STA pairwise keys reuse the same firmware command with
    different RSC and key-flags.
  - The local OpenBSD `ieee80211_crypto.c` is compiled with
    `IEEE80211_STA_ONLY` and has the AP-side group-key and rekey
    paths gated.
- **Smallest next code slice**: an AP-mode `setAPKey` arm in the
  existing iwx `iwx_set_key` / `iwx_delete_key` path that selects the
  AP-mode RSC bytes and the group-key flags before calling
  `IWX_ADD_STA_KEY`. Pre-requires the per-STA queue allocator from
  `startAPMode`.

### `IOReturn triggerAPCSA(const ItlHalApCSA *csa)`

- **Recovered Apple edge**: CSA programming, channel context switch,
  beacon CSA IE update.
- **Linux iwlwifi donor (upstream reference; not present in this tree)**:
  `iwl_mvm_mac_ctxt_send_chan_switch`, `iwl_mvm_csa_count_down`,
  `iwl_mvm_chsw_via_isr` — names from the upstream Linux iwlwifi
  `mac_ctxt.c` / `time-event.c` modules.
- **OpenBSD net80211 donor**: the local port has the CSA IE structure
  `struct ieee80211_csa_ie` declared at
  `itl80211/openbsd/net80211/ieee80211.h:1522`, but no AP-side CSA
  action helper, channel-switch announcement state machine, or
  CSA-complete callback. The full AP-side CSA path would be ported
  from upstream OpenBSD.
- **Local blockers**:
  - No AP-side time-event programming, no AP-side channel context
    switch in the iwx HAL today.
  - net80211 CSA helpers are gated.
- **Smallest next code slice**: `triggerAPCSA` may legitimately stay
  `kIOReturnUnsupported` for the first AP-mode iwx slice; CSA is
  optional for basic AP-up. Defer until after `startAPMode` /
  `updateAPBeacon` / `sendAPStationCommand` land.

### `IOReturn sendAPStationCommand(const ItlHalApStationCommand *cmd)`

- **Recovered Apple edge**: AP station auth / assoc / deauth /
  disassoc / power-save commands.
- **Linux iwlwifi donor (upstream reference; not present in this tree)**:
  `iwl_mvm_send_add_sta`, `iwl_mvm_send_rm_sta`,
  `iwl_mvm_send_sta_modify`, `iwl_mvm_set_sta_pm` — names from the
  upstream Linux iwlwifi `sta.c` / `power.c` modules.
- **OpenBSD net80211 donor**: `ieee80211_node_join` exists locally at
  `itl80211/openbsd/net80211/ieee80211_node.c:3167`;
  `ieee80211_node_leave` exists locally at
  `itl80211/openbsd/net80211/ieee80211_node.c:3344`;
  `ieee80211_pwrsave` exists locally at
  `itl80211/openbsd/net80211/ieee80211_output.c:2355`. All three are
  reachable only when `IEEE80211_STA_ONLY` is removed (slice 4) and
  `ic->ic_opmode == IEEE80211_M_HOSTAP`.
- **Local blockers**:
  - `IWX_ADD_STA` / `IWX_REMOVE_STA` are already used for STA-side
    single-peer add / remove, but the AP-side flags (`STA_ASSOC_AP`,
    `STA_AUTH`, `STA_TYPE` per-mac80211 mapping) are not set
    anywhere.
  - Power-save commands and per-STA queue setup (`iwl_mvm_set_sta_pm`,
    `iwl_mvm_tvqm_enable_txq`) are unported.
- **Smallest next code slice**: an AP-mode arm of the existing iwx
  `iwx_add_sta_cmd` family that uses the AP-side STA flags from the
  `ItlHalApStationCommand::flags` field. Pre-requires the
  broadcast / multicast queue allocator from `startAPMode`.

## Recommended next code route

The census above identifies a natural ordering for the future
implementation slices:

1. **iwx per-family AP-capability table.** Read-only data: which iwx
   firmware images in `fw/iwx/*.ucode` advertise AP / GO support, and
   under what TLVs. Exposes a host-side helper
   `iwx_firmware_supports_ap_mode()` that returns `false` on every
   family until proven otherwise. Build evidence: build clean, no
   change to STA path. Auditor route: `IMPLEMENT_LOCAL` (table) +
   `REUSE_LINUX_BSD_LINUX_IWLWIFI` (TLV semantics).
2. **Wire `ItlIwx::supportsAPMode()`** to consult the per-family
   table from slice 1. Same for iwm. Still returns `false` on every
   currently-shipped firmware image. Bounded structural slice.
3. **AP-mode arm of `iwx_mac_ctxt_cmd_common`.** Replace the panic
   guard at `itlwm/hal_iwx/ItlIwx.cpp:8346` with an AP-mode arm that
   maps `IEEE80211_M_HOSTAP` to `IWX_FW_MAC_TYPE_GO` and sets the AP
   command fields. Same for iwm at `mac80211.cpp:2016`. Requires
   `IEEE80211_STA_ONLY` to be replaced — see slice 4.
4. **HostAP enablement under a scoped opt-out of `IEEE80211_STA_ONLY`.**
   Either (a) replace `IEEE80211_STA_ONLY` with a runtime check that
   defaults to STA-only and only enables HOSTAP / IBSS when the host
   APSTA owner is active, or (b) compile the HOSTAP code paths
   unconditionally and gate them with `ic->ic_opmode == IEEE80211_M_HOSTAP`
   at runtime. Either route is a separate non-trivial slice.
5. **Beacon-template upload.** New `iwx_send_beacon_template` /
   `iwm_send_beacon_template` helper using
   `IWX_BEACON_TEMPLATE_CMD` / `IWM_BEACON_TEMPLATE_CMD`. Wired into
   `ItlHalService::updateAPBeacon`.
6. **AP station add / remove.** AP-side flag arm of `iwx_add_sta_cmd`
   / `iwm_add_sta_cmd`. Wired into `sendAPStationCommand`.
7. **AP key install.** AP-side RSC arm of `iwx_set_key` /
   `iwx_delete_key` (`itlwm/hal_iwx/ItlIwx.cpp:9580` / `:9614`),
   reusing the existing `IWX_ADD_STA_KEY` firmware command path.
   Wired into `setAPKey`.
8. **CSA.** Optional; can remain `kIOReturnUnsupported` until later.
9. **APSTA owner gate wiring.** `AirportItlwmAPSTAInterface::isLowerBackendReady()`
   becomes a member function that consults the `ItlHalService::supportsAPMode()`
   of the active backend; only returns `true` when every required
   command method has a working override.

## Build evidence

This slice is documentation-only. The full Tahoe build (`./scripts/build_tahoe.sh
/System/Library/KernelCollections/BootKernelExtensions.kc`) at the
candidate worktree on master `a768896bce57c66884f9bb738de18fb248776942`
reports `BUILD SUCCEEDED` with all `927` undefined symbols resolved
against BootKC. No source file is touched in the diff; the only diff
hunk is this documentation file under `docs/reference/`, so no new
symbols are introduced and the build-graph state is functionally
unchanged.

## Self-check anchors

- Auditor-verified AP/APSTA parity bundle:
  `commit-approval/offline_results/itlwm_ap_layer_remaining_debt_closure_result_2026_05_09.tar.zst`,
  archive sha256 (per bundle MANIFEST) verified by the parity verdict.
- AP/APSTA parity verdict:
  `commit-approval/status/AUDITOR_VERDICT_AP_LAYER_PARITY_CLOSURE_20260509T173840_0300.md`,
  sha256 `2ae7a49d627fb2b328ff1280478a273e07316bcf9c28466ea4e0ba3979838e56`.
- AP local implementation-gap donor package:
  `commit-approval/offline_results/itlwm_ap_local_impl_gap_closure_stage1_2026_05_09/`,
  archive sha256
  `1b72d3fc249d2bcef20e9557fc6ac8665fc7cf2cea9d692adf1951e7bf83f670`.
- Host APSTA owner skeleton commit:
  `e7a3b0547837fcad976e9e8ecc61ef09fc298ff5`
  (`AirportItlwm/AirportItlwmAPSTAInterface.hpp`).
- AP/GO HAL surface commit:
  `a768896bce57c66884f9bb738de18fb248776942`
  (`include/HAL/ItlHalService.hpp`).
- iwx firmware MAC types: `itlwm/hal_iwx/if_iwxreg.h:4509-4532` (doc comment 4509-4521; defines 4522-4532; `IWX_FW_MAC_TYPE_GO` define at line 4530).
- iwx beacon template command: `itlwm/hal_iwx/if_iwxreg.h:1924`.
- iwx HOSTAP preflight panic: `itlwm/hal_iwx/ItlIwx.cpp:8346` (function `iwx_mac_ctxt_cmd_common` at `itlwm/hal_iwx/ItlIwx.cpp:8325-8346`).
- iwm HOSTAP preflight panic: `itlwm/hal_iwm/mac80211.cpp:2016` (function `iwm_mac_ctxt_cmd_common` at `itlwm/hal_iwm/mac80211.cpp:1998-2016`).
- `IEEE80211_STA_ONLY` mask of `IEEE80211_M_HOSTAP`:
  `itl80211/openbsd/net80211/ieee80211_var.h:259-267` (mask itself at lines 261-265).
- AP/GO HAL surface and donor mapping doc:
  `docs/reference/AP_GO_HAL_SURFACE_2026_05_09.md`.
