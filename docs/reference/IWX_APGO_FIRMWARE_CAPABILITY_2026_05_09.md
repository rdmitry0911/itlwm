# iwx Firmware AP/GO Capability — Donor Semantics, Local Inventory, and Fail-Closed Helper

## Purpose

This document describes the project-owned `iwx_firmware_family_supports_ap_go()`
helper added in `itlwm/hal_iwx/IwxApGoCapability.hpp` and the upstream
Linux iwlwifi semantics that the helper will eventually mirror once a
TLV-driven runtime check and the AP-mode arm of
`iwx_mac_ctxt_cmd_common` are landed.

The helper is the first concrete implementation slice in the route
plan recorded by
`docs/reference/ITLWM_APGO_BACKEND_CAPABILITY_CENSUS_2026_05_09.md`.
This slice introduces only the helper and its tracked documentation.
No caller exists yet; `ItlHalService::supportsAPMode()`,
`AirportItlwmAPSTAInterface::isLowerBackendReady()`, and the V1 / V2
`setVIRTUAL_IF_CREATE` dispatchers are unchanged.

## Linux iwlwifi donor semantics (upstream reference; not present in this tree)

In the upstream Linux iwlwifi driver, AP/GO firmware support is
advertised through two layered checks:

1. NIC-family configuration table. Each iwlwifi-supported NIC has a
   `struct iwl_cfg` entry (e.g., `iwl9560_2ac_cfg`,
   `iwl_ax200_cfg_cc`) that includes operating-mode capabilities and
   firmware-image filename strings. The configuration table is
   consulted at PCI probe time; AP/GO operation is only attempted on
   NICs whose cfg entry matches a known AP/GO-capable family.
2. Firmware capability TLVs. Once a firmware image is loaded, the
   iwlwifi loader walks the TLV section and sets bits in
   `mvm->fw->ucode_capa.capa[]`. AP-mode power-save handling is
   gated by `IWL_UCODE_TLV_CAPA_AP_LINK_PS` (declared in upstream
   `iwl-fw-file.h`); related capabilities include
   `IWL_UCODE_TLV_CAPA_BEACON_STORING`,
   `IWL_UCODE_TLV_CAPA_TKIP_MIC_KEYS`, and the API-version flags that
   bound the AP MAC context command shape.

Both checks must succeed for the upstream driver to expose AP/GO
operation. AP/SoftAP MAC-context bring-up itself reuses the same
firmware command (`IWX_MAC_CONTEXT_CMD = 0x28`, locally at
`itlwm/hal_iwx/if_iwxreg.h:1896`) that STA mode already uses, but
with the `IWX_FW_MAC_TYPE_GO` MAC type
(`itlwm/hal_iwx/if_iwxreg.h:4530`).

These names are upstream Linux iwlwifi reference names and are not
present in this tree; the local `if_iwxreg.h` declares neither
`IWX_UCODE_TLV_CAPA_AP_LINK_PS` nor a per-NIC cfg table.

## Local fw/iwx firmware inventory

The local `itlwm/firmware/` tree contains the following iwx firmware
images at master `f4008e7a`:

| File | sha256 |
| --- | --- |
| `iwlwifi-cc-a0-68.ucode` | `4847c55420863d20d111b6a72f1fe2cf9ecd143081c57353020099be6e2af40c` |
| `iwlwifi-Qu-b0-hr-b0-68.ucode` | `09cd336771bad4fc41184131c805abc3e136061f9e3fb5f92bcac687a3f3976d` |
| `iwlwifi-Qu-b0-jf-b0-68.ucode` | `15167f513e2596e82b34a99aa73783e5dfe5a713c289baf3adbe9809c90131f5` |
| `iwlwifi-Qu-c0-hr-b0-68.ucode` | `80a25e1c86872f811a52fb6022f3f1182e2b4ec641954183aa20f492ab4055fb` |
| `iwlwifi-Qu-c0-jf-b0-68.ucode` | `08ecd3782784deb0c2d397d169b444adc93257f984b4c0133a9058cda53570bd` |
| `iwlwifi-QuZ-a0-hr-b0-68.ucode` | `b2ecffd017e337712a37d7b54ab73c1296dd9752df58ed3258aa9b4ed79cec31` |
| `iwlwifi-QuZ-a0-jf-b0-68.ucode` | `76bcb0a3d5e744b75833c3b1d5512231c37f52f56aee5d3f0830c554bfb3825b` |
| `iwlwifi-so-a0-gf-a0-68.ucode` | `08f78a57bd7052e07c2f597a0f22f5cd214eaf9f215912a467cdc2c1a1ae127d` |
| `iwlwifi-so-a0-gf4-a0-68.ucode` | `665483f7f4d79235dc0c7cb281a32e49c7a7b8154b30e7891f45965698ba875c` |
| `iwlwifi-so-a0-hr-b0-68.ucode` | `1550c08c54ae779ae3d3a6d5e99a4239af2ef99c8404463a0869cd242a806ba0` |
| `iwlwifi-so-a0-jf-b0-68.ucode` | `d152ddb16a12582216efa70375764eabc506146422c27e08bf082f66a3e8cab0` |
| `iwlwifi-ty-a0-gf-a0-68.ucode` | `6cbc5c1d375e901c7cf57b2151cdb661b39157112884bb7d12ded69c210375eb` |

The filename suffix `-68` corresponds to the iwlwifi firmware API
version 68. The leading family token (`cc`, `Qu`, `QuZ`, `so`, `ty`)
maps to a iwlwifi NIC family but the local port has no centralised
table that turns that family token into an `IWX_DEVICE_FAMILY_*`
value or into a per-firmware-image AP/GO capability classification.

The per-image AP/GO TLV bits in these firmware blobs have not been
parsed in this slice. A future slice that promotes any entry of
`iwx_firmware_family_supports_ap_go()` from `false` to `true` must
include a TLV walker driven by the AP/SoftAP capability constants
that have to be added to `if_iwxreg.h` first.

## Local device family enum

The local port classifies iwx silicon into two device families today:

```
itlwm/hal_iwx/if_iwxvar.h:647: #define IWX_DEVICE_FAMILY_22000  1
itlwm/hal_iwx/if_iwxvar.h:648: #define IWX_DEVICE_FAMILY_AX210  2
```

Both are 802.11ax NIC generations. The 9000-series and earlier
devices live on the iwm path (`itlwm/hal_iwm/`), which has its own
HOSTAP preflight panic guard at
`itlwm/hal_iwm/mac80211.cpp:2016` and is out of scope for this slice.

## Helper behavior

`itlwm/hal_iwx/IwxApGoCapability.hpp` declares one inline helper:

```
static inline bool iwx_firmware_family_supports_ap_go(int device_family);
```

The helper takes a device family value of the form
`IWX_DEVICE_FAMILY_*` and returns whether iwx firmware loaded for
that family is known to support AP/GO MAC contexts and the AP/GO
firmware command set. Implementation is a per-family `switch`:

| device_family | helper returns |
| --- | --- |
| `IWX_DEVICE_FAMILY_22000` | `false` |
| `IWX_DEVICE_FAMILY_AX210` | `false` |
| any other (`default`) | `false` |

Every entry returns `false` because no firmware image in the local
inventory has been proven to support AP/GO commands and the local
port lacks both the TLV constants and the AP-mode arm of
`iwx_mac_ctxt_cmd_common` that would be required to issue an
AP-context command.

Returning `false` for every family is the truthful state of the
driver, not a fallback. The helper is fail-closed by design.

## Wiring status

This slice does **not** wire the helper into any caller:

- `ItlHalService::supportsAPMode()` (declared in
  `include/HAL/ItlHalService.hpp` at the AP/GO HAL surface added by
  CR-451, commit `a768896bce57c66884f9bb738de18fb248776942`) is
  unchanged. Its default body still returns `false`.
- `ItlIwx` does not override `supportsAPMode()`.
- `ItlIwm` does not override `supportsAPMode()`.
- `AirportItlwmAPSTAInterface::isLowerBackendReady()` is unchanged
  and still returns `false`.
- The V1 (`AirportSTAIOCTL.cpp`) and V2
  (`AirportItlwmSkywalkInterface.cpp`) `setVIRTUAL_IF_CREATE`
  dispatchers are unchanged.

The helper has zero callers in this diff. The next implementation
slice in the route plan (slice 2) will add an `ItlIwx::supportsAPMode()`
override that consults the helper; with every family entry currently
`false`, that override will continue to fail closed even after
slice 2 lands.

## Residual blockers

To promote any per-family entry of
`iwx_firmware_family_supports_ap_go()` from `false` to `true`:

1. Add the AP/SoftAP firmware capability TLV constants to
   `itlwm/hal_iwx/if_iwxreg.h` (donor: upstream Linux iwlwifi
   `IWL_UCODE_TLV_CAPA_AP_LINK_PS` and related). Without those
   constants, the firmware-image TLV walker has nothing to test.
2. Add a TLV-walker call site that loads the firmware image and sets
   capability bits on `sc_fw.ucode_capa[]` (or equivalent local
   carrier). Today the iwx firmware loader does not record
   AP-related capability bits.
3. Land the AP-mode arm of `iwx_mac_ctxt_cmd_common`
   (`itlwm/hal_iwx/ItlIwx.cpp:8325-8346`, panic at line `8346`) so
   that an actual AP-context command can be issued. Without this
   arm, `iwx_firmware_family_supports_ap_go()` returning `true`
   would mislead `supportsAPMode()` into accepting AP-mode requests
   that would still fail at the panic guard.
4. Land HostAP enablement under a scoped opt-out of
   `IEEE80211_STA_ONLY` (`itl80211/openbsd/net80211/ieee80211_var.h:259-267`).

These are slices 3 and 4 in the route plan and remain residual.

## Out of scope for this slice

- No `ItlIwx::supportsAPMode()` override.
- No caller of `iwx_firmware_family_supports_ap_go()`.
- No change to `AirportItlwmAPSTAInterface::isLowerBackendReady()`.
- No removal of `IEEE80211_STA_ONLY`.
- No alteration of the iwx HOSTAP panic at
  `itlwm/hal_iwx/ItlIwx.cpp:8346` or the iwm HOSTAP panic at
  `itlwm/hal_iwm/mac80211.cpp:2016`.
- No AP firmware command implementation, no station-event producer
  bridge, no APSTA owner promotion, no AP-up transition, no
  beaconing, no AP client association, no DHCP, no traffic, no
  peer-cache publication, no station-table mutation.
- No merging of AP station lifecycle into the WCL_REASSOC
  publication path (commit `1086f64eefb3c8f53d7625f1973113b06f838830`
  remains separate).

## Self-check anchors

- AP/APSTA parity verdict:
  `commit-approval/status/AUDITOR_VERDICT_AP_LAYER_PARITY_CLOSURE_20260509T173840_0300.md`
  sha256 `2ae7a49d627fb2b328ff1280478a273e07316bcf9c28466ea4e0ba3979838e56`.
- Capability census (slice plan):
  `docs/reference/ITLWM_APGO_BACKEND_CAPABILITY_CENSUS_2026_05_09.md`
  (committed at `f4008e7a357e21ac214fdf0696abe01809fef4f5`).
- AP/GO HAL surface:
  `include/HAL/ItlHalService.hpp` (committed at
  `a768896bce57c66884f9bb738de18fb248776942`).
- Host APSTA owner skeleton:
  `AirportItlwm/AirportItlwmAPSTAInterface.hpp` (committed at
  `e7a3b0547837fcad976e9e8ecc61ef09fc298ff5`).
- Local iwx device family enum:
  `itlwm/hal_iwx/if_iwxvar.h:647-648`.
- Local iwx firmware MAC type defines:
  `itlwm/hal_iwx/if_iwxreg.h:4509-4532` (`IWX_FW_MAC_TYPE_GO` at
  line 4530).
- Local iwx MAC context command id:
  `itlwm/hal_iwx/if_iwxreg.h:1896` (`IWX_MAC_CONTEXT_CMD = 0x28`).
- Local iwx HOSTAP preflight panic:
  `itlwm/hal_iwx/ItlIwx.cpp:8346` (function `iwx_mac_ctxt_cmd_common`
  at lines 8325-8346).
- Local iwm HOSTAP preflight panic:
  `itlwm/hal_iwm/mac80211.cpp:2016` (function `iwm_mac_ctxt_cmd_common`
  at lines 1998-2016).
- `IEEE80211_STA_ONLY` mask of `IEEE80211_M_HOSTAP`:
  `itl80211/openbsd/net80211/ieee80211_var.h:259-267` (mask itself at
  lines 261-265).
