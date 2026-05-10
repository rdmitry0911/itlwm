# iwx AP/GO MAC-Context Command Owner and HAL Boundary Layer

## Purpose

This document describes the project-owned iwx AP/GO MAC-context
command owner / dispatcher path and the corresponding HAL boundary
added in this slice. The layer wires three components into a single
fail-closed sequence:

1. `iwx_mac_ctxt_cmd_fill_ap()` / `iwx_mac_ctxt_cmd_fill_go()`
   primitive-parameter wire-field mapping helpers.
2. `iwx_mac_ctxt_cmd_ap_send()` MAC-context command owner that
   builds and ships an `IWX_MAC_CONTEXT_CMD` with mac_type
   `IWX_FW_MAC_TYPE_GO`.
3. `iwx_start_ap_mode()` / `iwx_stop_ap_mode()` lifecycle helpers and
   the `ItlIwx::startAPMode()` / `ItlIwx::stopAPMode()` HAL boundary
   overrides that delegate to them.

Every layer is gated on the existing fail-closed
`iwx_softc_supports_ap_go(sc)` capability surface (CR-458). On every
iwx device the driver can attach today, the gate returns `false`, so
the HAL boundary returns `kIOReturnUnsupported` for `startAPMode()`
and `kIOReturnSuccess` (idempotent) for `stopAPMode()` without
reaching the firmware command path. The layer establishes a
reachable system-visible AP/GO contract while the existing iwx
HOSTAP preflight panic at `itlwm/hal_iwx/ItlIwx.cpp:8389`,
`IEEE80211_STA_ONLY` mask at
`itl80211/openbsd/net80211/ieee80211_var.h:259-267`, and parent HAL
defaults remain byte-identical.

## Local iwx MAC-context substrate (verified at master `bc782bc`)

### Top-level entry points

| Symbol | File:line (post-patch) | Note |
| --- | --- | --- |
| `iwx_mac_ctxt_cmd_common` | `itlwm/hal_iwx/ItlIwx.cpp:8368` | function body byte-identical to master `bc782bc` (line 8331 pre-patch) |
| iwx HOSTAP preflight `panic` | `itlwm/hal_iwx/ItlIwx.cpp:8389` | panic statement byte-identical to master bc782bc (line 8352 pre-patch) |
| `iwx_mac_ctxt_cmd_fill_sta` | `itlwm/hal_iwx/ItlIwx.cpp:8462` | body byte-identical to master `bc782bc` (line 8425 pre-patch) |
| `iwx_mac_ctxt_cmd` dispatcher | `itlwm/hal_iwx/ItlIwx.cpp:8621` | body byte-identical to master `bc782bc` (line 8449 pre-patch) |

The new helpers and the new HAL overrides are inserted **before** the
dispatcher and **after** `iwx_mac_ctxt_cmd_fill_sta`. Neither the
panic nor the existing dispatcher body is modified at the byte level.

### Unchanged STA / monitor call sites

The six existing `iwx_mac_ctxt_cmd` call sites at
`itlwm/hal_iwx/ItlIwx.cpp:4295`, `:9251`, `:9326`, `:9364`, `:9429`,
and `:9559` continue to use the STA / monitor opmode dispatch via
`iwx_mac_ctxt_cmd_common`'s opmode-switch. The new AP/GO command
owner `iwx_mac_ctxt_cmd_ap_send` is a separate code path that does
not reuse `iwx_mac_ctxt_cmd_common`, so the panic-guarded opmode
dispatch is not touched.

### Capability gate (committed prior)

| Symbol | File:line | Slice |
| --- | --- | --- |
| `iwx_firmware_family_supports_ap_go` | `itlwm/hal_iwx/IwxApGoCapability.hpp:66` | CR-456 |
| `iwx_softc_supports_ap_go` | `itlwm/hal_iwx/IwxApGoCapability.hpp:113` | CR-458 |
| `ItlIwx::supportsAPMode() const` | `itlwm/hal_iwx/ItlIwx.cpp:349` | CR-457 → CR-458 |

Every gate returns `false` on the current iwx fleet:
`iwx_firmware_family_supports_ap_go()` returns `false` for every
recognised family and the `default` arm; no `iwlwifi-*-68.ucode`
image advertises `IWX_UCODE_TLV_CAPA_BEACON_STORING`.

### AP HAL surface (committed prior, CR-451)

| Symbol | File:line |
| --- | --- |
| `struct ItlHalApConfig` | `include/HAL/ItlHalService.hpp:45-53` |
| `ItlHalService::supportsAPMode() const` (parent default) | `include/HAL/ItlHalService.hpp:119` |
| `ItlHalService::startAPMode(...)` (parent default) | `include/HAL/ItlHalService.hpp:120` |
| `ItlHalService::stopAPMode()` (parent default) | `include/HAL/ItlHalService.hpp:124` |

Parent defaults return `kIOReturnUnsupported`; this layer adds
iwx-only overrides that compute the same answer through the
project-owned classification and lifecycle path.

### Wire structures (already declared)

| Struct | File:line | Role |
| --- | --- | --- |
| `struct iwx_mac_ctx_cmd` | `itlwm/hal_iwx/if_iwxreg.h:4742-4773` | top-level `IWX_MAC_CONTEXT_CMD = 0x28` payload |
| `struct iwx_mac_data_ap` | `itlwm/hal_iwx/if_iwxreg.h:4562-4571` | AP arm |
| `struct iwx_mac_data_go` | `itlwm/hal_iwx/if_iwxreg.h:4621-4625` | GO arm (embeds AP arm) |
| `IWX_FW_MAC_TYPE_GO = 8` | `itlwm/hal_iwx/if_iwxreg.h:4530` | GO MAC-type |
| `IWX_FW_CTXT_ACTION_*` | `itlwm/hal_iwx/if_iwxreg.h:2969-2972` | ADD / MODIFY / REMOVE / STUB |
| `IWX_MAC_FILTER_ACCEPT_GRP` | `itlwm/hal_iwx/if_iwxreg.h:4676` | accept multicast |
| `IWX_MAC_FILTER_IN_PROBE_REQUEST` | `itlwm/hal_iwx/if_iwxreg.h:4682` | deliver probe-requests to host |

### Beacon allocator (in OpenBSD-derived net80211)

| Symbol | File:line |
| --- | --- |
| `ieee80211_beacon_alloc` declaration | `itl80211/openbsd/net80211/ieee80211_proto.h:105` |
| `ieee80211_beacon_alloc` implementation | `itl80211/openbsd/net80211/ieee80211_output.c:2267` |

The local beacon allocator is implementation-complete in the
OpenBSD-derived net80211 stack. The MAC-context command owner added
in this slice ships a `beacon_template` id of `0`; an actual beacon
template upload via `IWX_BEACON_TEMPLATE_CMD = 0x91`
(`itlwm/hal_iwx/if_iwxreg.h:1924`) is not part of this layer and is
not reachable today because the gate is closed. The
`IEEE80211_STA_ONLY` mask at
`itl80211/openbsd/net80211/ieee80211_var.h:259-267` continues to
suppress the OpenBSD HOSTAP enumerator in legacy STA-only build
configurations.

## Upstream Linux iwlwifi donor reference (pinned to v6.13)

Donor reference pinned to **Linux v6.13 release commit
`ffd294d346d185b70e28b1a28abe367bbfe53c04`**. Files cited:

- `drivers/net/wireless/intel/iwlwifi/mvm/mac-ctxt.c` at v6.13.
- `drivers/net/wireless/intel/iwlwifi/mvm/mvm.h` at v6.13.
- `drivers/net/wireless/intel/iwlwifi/fw/file.h` at v6.13.

Field-by-field semantics for the AP arm follow upstream
`iwl_mvm_mac_ctxt_cmd_fill_ap`:

| Local helper field | Upstream source |
| --- | --- |
| `ap->bi` | `vif->bss_conf.beacon_int` |
| `ap->dtim_interval` | `bi × dtim_period` |
| `ap->mcast_qid` | `mvmvif->deflink.cab_queue` |
| `ap->beacon_template` | `mvmvif->id` |
| `ap->beacon_time` | `mvmvif->ap_beacon_time` (computed at AP-up) |
| `ap->beacon_tsf` | `0` (unused on AP path) |

Names below are upstream Linux iwlwifi reference; not present
locally. Their semantics are mirrored by the local helpers above:
`iwl_mvm_mac_ctxt_cmd_fill_ap`, `iwl_mvm_mac_ctxt_cmd_ap`,
`iwl_mvm_mac_ctxt_cmd_go`, `iwl_mvm_mac_ctxt_cmd_send_beacon`,
`iwl_mvm_protect_assoc`, `mvmvif->ap_beacon_time`,
`mvmvif->deflink.cab_queue`, `mvmvif->id`,
`vif->bss_conf.beacon_int`, `struct ieee80211_vif`,
`struct ieee80211_bss_conf`, `iwl_mvm_get_systime()`.

## Layer contract

### Wire-field mapping helpers

```
void ItlIwx::iwx_mac_ctxt_cmd_fill_ap(struct iwx_softc *sc,
                                       struct iwx_mac_data_ap *ap,
                                       uint32_t beacon_time,
                                       uint32_t bi_tu,
                                       uint32_t dtim_period,
                                       uint32_t mcast_qid,
                                       uint32_t beacon_template_id);

void ItlIwx::iwx_mac_ctxt_cmd_fill_go(struct iwx_softc *sc,
                                       struct iwx_mac_data_go *go,
                                       uint32_t beacon_time,
                                       uint32_t bi_tu,
                                       uint32_t dtim_period,
                                       uint32_t mcast_qid,
                                       uint32_t beacon_template_id,
                                       uint32_t ctwin,
                                       uint32_t opp_ps_enabled);
```

Pure mapping helpers. The AP filler writes eight `htole32`/`htole64`
fields of `struct iwx_mac_data_ap`. The GO filler delegates the
embedded AP arm to the AP filler and then writes `ctwin` and
`opp_ps_enabled`.

### MAC-context command owner

```
int ItlIwx::iwx_mac_ctxt_cmd_ap_send(struct iwx_softc *sc,
                                      const struct ItlHalApConfig *config,
                                      uint32_t action);
```

Builds and ships an `IWX_MAC_CONTEXT_CMD` with
`mac_type = IWX_FW_MAC_TYPE_GO`. The function:

- **Gate 0** — calls `iwx_softc_supports_ap_go(sc)` and returns
  `EOPNOTSUPP` if the gate is closed; this is the entry-point
  short-circuit and prevents any subsequent firmware interaction
  when the device or firmware image lacks AP/GO capability.
- For `IWX_FW_CTXT_ACTION_REMOVE`: ships a minimal command
  containing only `id_and_color`, `action`, `mac_type`, `tsf_id`.
- For `IWX_FW_CTXT_ACTION_ADD` / `IWX_FW_CTXT_ACTION_MODIFY`:
  validates `config != NULL` (returns `EINVAL` otherwise), copies
  `ic->ic_myaddr` into `cmd.node_addr`, copies `config->bssid`
  into `cmd.bssid_addr`, sets
  `filter_flags = IWX_MAC_FILTER_ACCEPT_GRP | IWX_MAC_FILTER_IN_PROBE_REQUEST`,
  and calls `iwx_mac_ctxt_cmd_fill_ap` with `bi_tu = config->beaconInterval`,
  `dtim_period = max(config->dtimPeriod, 1)`, `mcast_qid = 0`,
  `beacon_template_id = 0`, `beacon_time = 0`.

`mcast_qid = 0` and `beacon_template_id = 0` are placeholder values:
the current iwx device set keeps the gate closed, so the command is
never actually shipped and the placeholder values are never observed
by firmware. Promoting any helper switch arm to `true` requires a
follow-on slice that allocates a real multicast queue and a real
beacon template id before issuing the ADD command.

### Lifecycle helpers

```
int ItlIwx::iwx_start_ap_mode(struct iwx_softc *sc,
                               const struct ItlHalApConfig *config);

int ItlIwx::iwx_stop_ap_mode(struct iwx_softc *sc);
```

`iwx_start_ap_mode()` checks `iwx_softc_supports_ap_go(sc)` and
returns `EOPNOTSUPP` if the gate rejects AP/GO operation; otherwise
forwards to `iwx_mac_ctxt_cmd_ap_send(sc, config, IWX_FW_CTXT_ACTION_ADD)`.
`config == NULL` returns `EINVAL`.

`iwx_stop_ap_mode()` returns `0` (idempotent success) when the gate
is closed, so the host APSTA owner's tear-down path tolerates
re-entry without producing spurious failures. When the gate is
open, it forwards to `iwx_mac_ctxt_cmd_ap_send(sc, NULL, IWX_FW_CTXT_ACTION_REMOVE)`.

### HAL boundary

```
IOReturn ItlIwx::startAPMode(const struct ItlHalApConfig *config) override;
IOReturn ItlIwx::stopAPMode() override;
```

`startAPMode` translates `iwx_start_ap_mode` errno-style returns:
- `EOPNOTSUPP` → `kIOReturnUnsupported`
- `EINVAL` → `kIOReturnBadArgument`
- non-zero → `kIOReturnError`
- `0` → `kIOReturnSuccess`

`stopAPMode` translates similarly:
- `EOPNOTSUPP` → `kIOReturnUnsupported`
- non-zero → `kIOReturnError`
- `0` → `kIOReturnSuccess`

The HAL boundary is reachable from any code that holds an
`ItlHalService *` pointer. On every iwx device the driver can
attach today, `startAPMode` returns `kIOReturnUnsupported` because
`iwx_softc_supports_ap_go(&com)` is `false`, and `stopAPMode`
returns `kIOReturnSuccess`.

## Observable-behaviour invariant

The HAL boundary returns the same answer the parent default would:

| Configuration | Parent default | iwx override |
| --- | --- | --- |
| Current iwx fleet (gate closed) | `startAPMode → kIOReturnUnsupported`, `stopAPMode → kIOReturnUnsupported` | `startAPMode → kIOReturnUnsupported`, `stopAPMode → kIOReturnSuccess` |

The `stopAPMode` value differs (`kIOReturnUnsupported` vs.
`kIOReturnSuccess`) because the iwx override treats the closed gate
as "no AP context to remove" rather than "operation unsupported".
This is the only observable behaviour change introduced by the
override and is the documented fail-closed contract: idempotent
tear-down is required by the AP/GO HAL surface (see
`include/HAL/ItlHalService.hpp:108-114`: "It must remain
re-entry-safe: calling stopAPMode() while AP mode is not started
must succeed without side effects.")

`hal->supportsAPMode()` returns the same value before and after this
slice on every supported iwx device. Every existing STA / monitor
path through `iwx_mac_ctxt_cmd_common` and `iwx_mac_ctxt_cmd` is
byte-identical.

## Attach-time AP/GO HAL boundary self-test

`ItlIwx::attach()` runs a single one-shot AP/GO HAL probe after the
underlying `iwx_attach()` returns success. The probe casts `this`
to `ItlHalService *` and invokes the abstract HAL boundary entries
`startAPMode(NULL)` and `stopAPMode()` exactly once per attach. It
records the resulting `IOReturn` values together with the current
state of `iwx_softc_supports_ap_go(&com)` to the kernel log via the
existing `XYLog` macro, in the format:

```
itlwm: ap-hal-probe gate=<0|1> startAPMode=0x<hex> stopAPMode=0x<hex>
```

Expected values on every iwx device today (gate is closed):

- `gate=0`
- `startAPMode=0xe00002c7` (`kIOReturnUnsupported`)
- `stopAPMode=0x00000000` (`kIOReturnSuccess`)

Internal call chain:

- `ItlHalService *hal = static_cast<ItlHalService *>(this)`
- `hal->startAPMode(NULL)` resolves through C++ virtual dispatch
  to `ItlIwx::startAPMode(NULL)`, which forwards to
  `iwx_start_ap_mode(&com, NULL)`. The lifecycle helper short-
  circuits at `iwx_softc_supports_ap_go(&com) == false` with
  `EOPNOTSUPP`; the override translates that to
  `kIOReturnUnsupported`.
- `hal->stopAPMode()` resolves through C++ virtual dispatch to
  `ItlIwx::stopAPMode()`, which forwards to
  `iwx_stop_ap_mode(&com)`. The lifecycle helper short-circuits at
  the same gate with `0` (idempotent); the override translates
  that to `kIOReturnSuccess`.

The probe runs unconditionally on every attach but its observable
effect is exactly one log line. It does not allocate, mutate the
softc, change interface state, send any firmware command, or
alter any STA / monitor / APSTA owner code path; gate closure
guarantees `iwx_mac_ctxt_cmd_ap_send` is never reached.

If a future per-family helper arm is promoted to `true`, the same
probe will instead see `gate=1`, `startAPMode=0xe00002c2`
(`kIOReturnBadArgument`, because `config == NULL` reaches the
lifecycle helper after the gate admits the call), and `stopAPMode`
will issue `IWX_FW_CTXT_ACTION_REMOVE`. That future change is out
of scope for this slice and forbidden by the unchanged fail-closed
gate.

## Stage 2 runtime plan (concrete, after Stage 1 approval)

The HAL boundary is reachable; the gate is closed; the firmware
command path is unreachable. Stage 2 directly observes the AP/GO
HAL boundary contract by reading the attach-time probe log line
from kernel logs and confirms no regression in the existing STA
path.

1. Copy the new kext to `/Library/Extensions/AirportItlwm.kext`,
   set ownership `root:wheel`, approve the load prompt via VNC
   `127.0.0.1:5901` if it appears.
2. Reboot the guest. SSH on `127.0.0.1:3322` must return within
   120 seconds; if not, follow the no-Wi-Fi recovery path.
3. Capture initial driver evidence: `kextstat | grep -i AirportItlwm`,
   `ioreg -p IOService -w 0 | grep -i AirportItlwm`, `dmesg | head`.
4. **AP/GO HAL boundary observation (primary Stage 2 evidence)**:
   capture the probe line emitted at attach. Use both
   `dmesg | grep ap-hal-probe` and
   `log show --last 5m --predicate 'process == "kernel" AND eventMessage CONTAINS "ap-hal-probe"'`.
   The line must show `gate=0`, `startAPMode=0xe00002c7`
   (`kIOReturnUnsupported`), and `stopAPMode=0x00000000`
   (`kIOReturnSuccess`). Save raw command output to
   `commit-approval/runtime_evidence/CR-462-stage2-ap-hal-probe.log`.
5. **Symbol presence cross-check (secondary)**: confirm the
   override symbols are linked into the loaded kext binary via
   `nm /Library/Extensions/AirportItlwm.kext/Contents/MacOS/AirportItlwm | grep -E 'startAPMode|stopAPMode'`.
   The presence of `_ZN6ItlIwx11startAPModeEPK15ItlHalApConfig`
   and `_ZN6ItlIwx10stopAPModeEv` together with the parent
   `ItlHalService` defaults provides the durable static evidence
   complementing the runtime IOReturn observation.
6. **STA regression test against FAST_LAB_AP**: start the host lab
   AP (`./start-fast_lab_ap-ap.sh`, `SSID=FAST_LAB_AP`,
   `password=<REDACTED:WIFI_PSK>`, gateway/DHCP `10.77.0.1/24`); scan,
   join, obtain DHCP lease, hold the link 60 seconds; capture
   `ifconfig`, station dump, `dmesg`, hostapd / dnsmasq logs,
   `./status-fast_lab_ap-ap.sh`. Verify no panic, no kernel error,
   no link drop. Stop the host lab AP afterwards
   (`./stop-fast_lab_ap-ap.sh`).
7. **CONTROL_STA_NETWORK control test**: scan, associate to `SSID=CONTROL_STA_NETWORK`,
   `password=<REDACTED:WIFI_PSK>`; obtain DHCP lease; hold link 60 seconds;
   capture `dmesg`, panic check.
8. Archive runtime evidence under
   `commit-approval/runtime_evidence/CR-462-stage2-...`.

The runtime plan does not exercise actual AP-mode firmware
command issuance because the gate keeps every command path
unreachable on the current iwx fleet. This is the documented
fail-closed contract. Stage 2 evidence therefore consists of:

- the attach-time probe log line proving the AP/GO HAL boundary
  returns `kIOReturnUnsupported` for `startAPMode(NULL)` and
  `kIOReturnSuccess` for `stopAPMode()` under the closed gate;
- HAL boundary symbol presence in the loaded kext binary;
- STA-mode association + DHCP + stability against FAST_LAB_AP and
  CONTROL_STA_NETWORK;
- panic-regression negative check.

## Concrete safety blocker — actual AP bring-up requires future slices

Issuing a real AP-mode bring-up via this layer requires every helper
switch arm of `iwx_firmware_family_supports_ap_go()` and every
firmware image's `IWX_UCODE_TLV_CAPA_BEACON_STORING` to be true.
Even if those gates were promoted today, the following decomp /
implementation gaps would have to be closed first to avoid firmware
state corruption:

1. **AP-state carrier**: there is no project-owned softc field that
   tracks AP-mode lifecycle (AP-up boolean, MAC ID, beacon template
   id, broadcast / multicast station ids). `iwx_mac_ctxt_cmd_ap_send`
   currently sends `id_and_color = 0` and `beacon_template_id = 0`
   as placeholders.
2. **Broadcast / multicast station setup**: the upstream iwlwifi
   AP path allocates two internal stations (`bcast_sta` and
   `mcast_sta`) per VIF before the MAC-context ADD. The local
   driver has no equivalent allocator.
3. **Beacon-template upload**: the firmware AP MAC-context expects
   a beacon template uploaded via `IWX_BEACON_TEMPLATE_CMD = 0x91`
   before AP-up. The local driver has no `iwx_beacon_template()`
   helper that ships the OpenBSD-derived `ieee80211_beacon_alloc`
   output via that command.
4. **AP-mode time-event / session-protection**: upstream
   `iwl_mvm_protect_assoc()` requests a session-protection window
   around the AP-up TBTT. The local STA-side `iwx_protect_session`
   is wired for association only.
5. **HOSTAP enablement under scoped `IEEE80211_STA_ONLY` opt-out**:
   the OpenBSD net80211 stack masks `IEEE80211_M_HOSTAP` from
   `enum ieee80211_opmode` when `IEEE80211_STA_ONLY` is defined
   (`itl80211/openbsd/net80211/ieee80211_var.h:259-267`). Without a
   scoped opt-out, the OpenBSD beacon allocator and AP node-state
   transitions cannot be reached from the iwx HAL.

Each of these gaps is a discrete decomp / implementation surface
that warrants its own Stage 1 review. Bundling them into a single
Stage 1 alongside the layer in this slice would create a multi-file
multi-thousand-line patch that conflates several distinct
Apple-observable contract boundaries; the auditor's review surface
would be effectively unbounded. The current layer therefore
deliberately stops at the HAL boundary and the gated MAC-context
command owner so each downstream gap can be filled and reviewed
independently.

## Out of scope for this slice

- No `iwx_firmware_family_supports_ap_go()` switch arm is promoted
  from `false` to `true`. Every recognised device family and the
  `default` arm still return `false`.
- No new `IWX_UCODE_TLV_*` macro is added.
- No new firmware-command wire struct is added (the layer reuses
  the existing `IWX_MAC_CONTEXT_CMD = 0x28` ABI).
- No alteration of the iwx HOSTAP preflight panic at
  `itlwm/hal_iwx/ItlIwx.cpp:8389`. The new AP-mode command owner
  bypasses `iwx_mac_ctxt_cmd_common`'s opmode dispatch entirely.
- No alteration of the iwm HOSTAP preflight panic at
  `itlwm/hal_iwm/mac80211.cpp:2016`.
- No removal or scoped opt-out of `IEEE80211_STA_ONLY` at
  `itl80211/openbsd/net80211/ieee80211_var.h:259-267`.
- No `ItlIwm` / `ItlIwn` override of `supportsAPMode` /
  `startAPMode` / `stopAPMode`.
- No `updateAPBeacon`, `setAPKey`, `triggerAPCSA`, or
  `sendAPStationCommand` override on `ItlIwx`. These remain at the
  `ItlHalService` defaults that return `kIOReturnUnsupported`.
- No change to `AirportItlwmAPSTAInterface::isLowerBackendReady()`
  at `AirportItlwm/AirportItlwmAPSTAInterface.hpp:2562`; the gate
  still returns `false`.
- No change to the V1 (`AirportSTAIOCTL.cpp`) or V2
  (`AirportItlwmSkywalkInterface.cpp`) `setVIRTUAL_IF_CREATE`
  dispatchers.
- No AP-up transition, beacon emission, AP probe-response template
  upload, AP station add / remove, AP key install, CSA / beacon
  update, AP firmware event conversion, AP client association,
  DHCP, traffic, peer-cache publication, station-table mutation,
  SoftAP stats publication, role-7 success, or project-completion
  claim is requested by Stage 1 or implied by the Stage 2 runtime
  plan above.
- No merging of AP station lifecycle into the WCL_REASSOC
  publication path; commit `1086f64eefb3c8f53d7625f1973113b06f838830`
  remains separate.

## Self-check anchors

- AP/APSTA parity verdict:
  `commit-approval/status/AUDITOR_VERDICT_AP_LAYER_PARITY_CLOSURE_20260509T173840_0300.md`
  sha256 `2ae7a49d627fb2b328ff1280478a273e07316bcf9c28466ea4e0ba3979838e56`.
- Capability census (route plan):
  `docs/reference/ITLWM_APGO_BACKEND_CAPABILITY_CENSUS_2026_05_09.md`
  (committed at `f4008e7a357e21ac214fdf0696abe01809fef4f5`).
- AP/GO HAL surface (parent class declaration):
  `include/HAL/ItlHalService.hpp` (committed at `a768896bce57c66884f9bb738de18fb248776942`).
- Slice 1 helper doc:
  `docs/reference/IWX_APGO_FIRMWARE_CAPABILITY_2026_05_09.md`
  (committed at `659a5ff3ef0fb7ca42dfb639a06e2ec57332b1c1`).
- Slice 2 wiring doc:
  `docs/reference/IWX_APGO_SUPPORTSAPMODE_WIRING_2026_05_09.md`
  (committed at `03ce03977b7e02be811ed9fca7556ca4a8e768da`).
- Slice 3 TLV evidence doc:
  `docs/reference/IWX_APGO_FIRMWARE_TLV_EVIDENCE_2026_05_09.md`
  (committed at `bc782bcf48fb69251c41d8f3566c361b23c6774e`).
- Iwm HOSTAP preflight panic (untouched):
  `itlwm/hal_iwm/mac80211.cpp:2016`.
- `IEEE80211_STA_ONLY` mask (untouched):
  `itl80211/openbsd/net80211/ieee80211_var.h:259-267`.
- Parent default body (untouched):
  `include/HAL/ItlHalService.hpp:119`
  (`virtual bool supportsAPMode() const { return false; }`).
- Upstream Linux iwlwifi pin: v6.13 release commit
  `ffd294d346d185b70e28b1a28abe367bbfe53c04`. Donor files:
  `drivers/net/wireless/intel/iwlwifi/mvm/mac-ctxt.c`,
  `drivers/net/wireless/intel/iwlwifi/mvm/mvm.h`,
  `drivers/net/wireless/intel/iwlwifi/fw/file.h`.
