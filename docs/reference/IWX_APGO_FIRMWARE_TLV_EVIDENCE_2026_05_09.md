# iwx Firmware AP/GO TLV Capability Evidence — Static Image Scan and Fail-Closed Support-Check Surface

## Purpose

This document records the static evidence collected from the local
`itlwm/firmware/` iwlwifi firmware images regarding the AP/GO-relevant
firmware capability TLV bits, and describes the fail-closed
`iwx_softc_supports_ap_go()` support-check surface added to
`itlwm/hal_iwx/IwxApGoCapability.hpp` that consumes that evidence
defense-in-depth alongside the existing per-family helper.

The surface is the third concrete implementation slice in the AP/GO
route plan committed by
`docs/reference/ITLWM_APGO_BACKEND_CAPABILITY_CENSUS_2026_05_09.md`
(slice 1 = helper, slice 2 = `ItlIwx::supportsAPMode()` wiring).

## Local TLV walker (already present)

The local iwx HAL already parses the firmware TLV stream during
`iwx_read_firmware()` in `itlwm/hal_iwx/ItlIwx.cpp:1401-1640`. The
walker initializes `sc->sc_capaflags = 0` and zeroes
`sc->sc_enabled_capa[]` and `sc->sc_ucode_api[]` at
`itlwm/hal_iwx/ItlIwx.cpp:1455-1458`, then iterates the post-header
TLV stream and populates the bitmaps from the standard TLV types:

- TLV type `IWX_UCODE_TLV_FLAGS = 18` writes the 32-bit
  `sc->sc_capaflags` from the first matching TLV
  (`itlwm/hal_iwx/ItlIwx.cpp:1518-1537`).
- TLV type `IWX_UCODE_TLV_API_CHANGES_SET = 29` walks
  `struct iwx_ucode_api { uint32_t api_index; uint32_t api_flags; }`
  entries and sets bits in `sc->sc_ucode_api[]`
  (`itlwm/hal_iwx/ItlIwx.cpp:1593-1612`).
- TLV type `IWX_UCODE_TLV_ENABLED_CAPABILITIES = 30` walks
  `struct iwx_ucode_capa { uint32_t api_index; uint32_t api_capa; }`
  entries and sets bits in `sc->sc_enabled_capa[]`
  (`itlwm/hal_iwx/ItlIwx.cpp:1613-1630`).

The TLV header struct is `struct iwx_tlv_ucode_header`
(`itlwm/hal_iwx/if_iwxreg.h:1612-1633`) with magic
`IWX_TLV_UCODE_MAGIC = 0x0a4c5749` (`itlwm/hal_iwx/if_iwxreg.h:1610`).
Each TLV element is `struct iwx_ucode_tlv { uint32_t type; uint32_t length; uint8_t data[]; }`
(`itlwm/hal_iwx/if_iwxreg.h:1594-1598`); each TLV body is padded to a
4-byte boundary in the stream.

`sc_capaflags` and `sc_enabled_capa` are members of
`struct iwx_softc` declared at `itlwm/hal_iwx/if_iwxvar.h:666` and
`itlwm/hal_iwx/if_iwxvar.h:671` (the latter sized
`howmany(IWX_NUM_UCODE_TLV_CAPA, NBBY)` where
`IWX_NUM_UCODE_TLV_CAPA = 128` at `itlwm/hal_iwx/if_iwxreg.h:1402`).

The walker is reachable on every firmware load and runs before any
HAL caller can reach the AP/GO HAL surface; the `sc_capaflags` /
`sc_enabled_capa` state is therefore authoritative once
`iwx_attach()` returns success.

## Local TLV constant inventory (already named)

`itlwm/hal_iwx/if_iwxreg.h` declares 18 `IWX_UCODE_TLV_FLAGS_*`
flag bits at lines 1228-1246 and 53 `IWX_UCODE_TLV_CAPA_*`
capability bits at lines 1340-1417. The relevant AP/GO-touching
constants already locally named are:

| Macro | Bit | Local line | Meaning |
| --- | --- | --- | --- |
| `IWX_UCODE_TLV_FLAGS_P2P` | flags 3 | `if_iwxreg.h:1231` | uCode supports P2P |
| `IWX_UCODE_TLV_FLAGS_UAPSD_SUPPORT` | flags 24 | `if_iwxreg.h:1241` | uCode supports uAPSD |
| `IWX_UCODE_TLV_FLAGS_BCAST_FILTERING` | flags 29 | `if_iwxreg.h:1244` | broadcast filtering |
| `IWX_UCODE_TLV_FLAGS_GO_UAPSD` | flags 30 | `if_iwxreg.h:1245` | AP/GO interfaces support uAPSD clients |
| `IWX_UCODE_TLV_CAPA_RADIO_BEACON_STATS` | capa 22 | `if_iwxreg.h:1379` | radio and beacon statistics |
| `IWX_UCODE_TLV_CAPA_BEACON_ANT_SELECTION` | capa 71 | `if_iwxreg.h:1404` | firmware decides beacon antenna |
| `IWX_UCODE_TLV_CAPA_BEACON_STORING` | capa 72 | `if_iwxreg.h:1405` | firmware stores latest beacon |

`IWX_UCODE_TLV_FLAGS_GO_UAPSD` and `IWX_UCODE_TLV_CAPA_BEACON_STORING`
are the two locally-named bits that most directly admit AP-mode
firmware behaviour. The combination is used by the new
`iwx_softc_supports_ap_go()` support-check surface as a
defense-in-depth firmware-evidence requirement on top of the per-family
classification.

## Upstream Linux iwlwifi TLV reference (current master)

The upstream Linux kernel header that declares the donor capability
enum is `drivers/net/wireless/intel/iwlwifi/fw/file.h`. The current
upstream `enum iwl_ucode_tlv_capa` retains the same bit-numbering for
the AP-relevant entries already locally named:

| Upstream macro | Upstream value | Local correspondence |
| --- | --- | --- |
| `IWL_UCODE_TLV_CAPA_RADIO_BEACON_STATS` | 22 | matches local `IWX_UCODE_TLV_CAPA_RADIO_BEACON_STATS` |
| `IWL_UCODE_TLV_CAPA_BEACON_ANT_SELECTION` | 71 | matches local `IWX_UCODE_TLV_CAPA_BEACON_ANT_SELECTION` |
| `IWL_UCODE_TLV_CAPA_BEACON_STORING` | 72 | matches local `IWX_UCODE_TLV_CAPA_BEACON_STORING` |
| `IWL_UCODE_TLV_CAPA_AP_LINK_PS` | absent in current upstream | absent locally |
| `IWL_UCODE_TLV_CAPA_TKIP_MIC_KEYS` | absent in current upstream | absent locally |
| `IWL_UCODE_TLV_CAPA_DC2DC_CONFIG_SUPPORT` | absent in current upstream | local has `IWX_UCODE_TLV_CAPA_DC2DC_CONFIG_SUPPORT` at bit 19 |

`IWL_UCODE_TLV_CAPA_AP_LINK_PS` and `IWL_UCODE_TLV_CAPA_TKIP_MIC_KEYS`
were removed from the current upstream `enum iwl_ucode_tlv_capa`. They
are mentioned in older kernel revisions and in
`docs/reference/IWX_APGO_FIRMWARE_CAPABILITY_2026_05_09.md` as
historical donor names; without a stable current-upstream donor name
this slice does **not** add new local TLV constants. Promoting any
unnamed bit to a local `IWX_UCODE_TLV_CAPA_*` macro requires a
pinned upstream donor commit identifier and a fresh route decision.

## Static iwx firmware image scan (per-image evidence)

A static offline scanner walks each `itlwm/firmware/iwlwifi-*-68.ucode`
file (uncompressed on-disk TLV stream) and records:

- 32-bit `IWX_UCODE_TLV_FLAGS` value (the first such TLV's payload).
- 128-bit `sc_enabled_capa` bitmap accumulated from every
  `IWX_UCODE_TLV_ENABLED_CAPABILITIES` TLV.

The scanner reproduces exactly what `iwx_read_firmware()` populates
into `sc_capaflags` / `sc_enabled_capa[]` at runtime; running it
offline does not require firmware execution. The scan output for the
12 iwlwifi firmware images at master `03ce0397` is summarized below
(`GO_UAPSD` = `IWX_UCODE_TLV_FLAGS_GO_UAPSD`, bit 30 of FLAGS;
`BEACON_STORING` = `IWX_UCODE_TLV_CAPA_BEACON_STORING`, bit 72 of CAPA;
`bit27` = unnamed CAPA bit 27 which historical upstream once named
`IWL_UCODE_TLV_CAPA_AP_LINK_PS` but the current upstream master does
not retain):

| Firmware image | `GO_UAPSD` | `BEACON_STORING` | unnamed CAPA `bit27` |
| --- | --- | --- | --- |
| `iwlwifi-Qu-b0-hr-b0-68.ucode` | 1 | 0 | 1 |
| `iwlwifi-Qu-b0-jf-b0-68.ucode` | 1 | 0 | 1 |
| `iwlwifi-Qu-c0-hr-b0-68.ucode` | 1 | 0 | 1 |
| `iwlwifi-Qu-c0-jf-b0-68.ucode` | 1 | 0 | 1 |
| `iwlwifi-QuZ-a0-hr-b0-68.ucode` | 1 | 0 | 1 |
| `iwlwifi-QuZ-a0-jf-b0-68.ucode` | 1 | 0 | 1 |
| `iwlwifi-cc-a0-68.ucode` | 1 | 0 | 1 |
| `iwlwifi-so-a0-gf-a0-68.ucode` | 1 | 0 | 1 |
| `iwlwifi-so-a0-gf4-a0-68.ucode` | 1 | 0 | 1 |
| `iwlwifi-so-a0-hr-b0-68.ucode` | 1 | 0 | 1 |
| `iwlwifi-so-a0-jf-b0-68.ucode` | 1 | 0 | 1 |
| `iwlwifi-ty-a0-gf-a0-68.ucode` | 1 | 0 | 1 |

Concrete interpretation:
- Every iwx firmware image in the local fleet advertises
  `IWX_UCODE_TLV_FLAGS_GO_UAPSD`. This is the only locally-named
  AP/GO firmware-capability flag and it is universally set; the
  flag alone is therefore not a discriminating gate.
- No iwx firmware image in the local fleet advertises
  `IWX_UCODE_TLV_CAPA_BEACON_STORING`. Requiring this capability
  enforces a fail-closed answer for every supported NIC today.
- Unnamed CAPA bit 27 is set on every image but the current upstream
  iwlwifi enum does not retain a name for that bit. The historical
  name `IWL_UCODE_TLV_CAPA_AP_LINK_PS` is recorded for context;
  this slice does not add a local macro for bit 27.

## Fail-closed support-check surface

`itlwm/hal_iwx/IwxApGoCapability.hpp` adds a new inline helper:

```
static inline bool iwx_softc_supports_ap_go(const struct iwx_softc *sc);
```

The helper composes three checks in order, returning `false` from the
first one that fails:

1. Per-family classification via the existing
   `iwx_firmware_family_supports_ap_go(sc->sc_device_family)`. The
   classification returns `false` for every known family today, so
   this gate alone keeps the function fail-closed across the entire
   fleet.
2. `sc->sc_capaflags & IWX_UCODE_TLV_FLAGS_GO_UAPSD`. Universally set
   in current iwx firmware; required to keep the answer contingent on
   firmware-side AP/GO uAPSD evidence rather than family alone.
3. `isset(sc->sc_enabled_capa, IWX_UCODE_TLV_CAPA_BEACON_STORING)`.
   Not set on any current iwx firmware image; this gate forces a
   `false` answer across the entire current fleet even if Gate 1 is
   later promoted to `true` for some family.

`itlwm/hal_iwx/ItlIwx.cpp` updates the previously-committed
`ItlIwx::supportsAPMode() const` override to call
`iwx_softc_supports_ap_go(&com)` instead of the family-only helper.
The override therefore now consults firmware TLV evidence in addition
to the family classification. Both helpers remain fail-closed and the
override still returns `false` on every iwx device the driver can
attach today.

## Observable-behaviour invariant

The override returns `false` before and after this slice on every iwx
device that the driver can attach today:

| State | Family check | `GO_UAPSD` | `BEACON_STORING` | Override result |
| --- | --- | --- | --- | --- |
| Before this slice (CR-457) | `false` | (not consulted) | (not consulted) | `false` |
| After this slice, current fleet | `false` | `true` | `false` | `false` (gate 1 short-circuits) |
| Hypothetical: family promoted, image keeps current TLVs | `true` | `true` | `false` | `false` (gate 3 short-circuits) |
| Hypothetical: family promoted, image adds `BEACON_STORING` | `true` | `true` | `true` | `true` (would require future slices to be safe) |

Code that reads `hal->supportsAPMode()` therefore sees the same answer
before and after this slice on every supported iwx device.

## Scanner provenance

The static scanner used to produce the per-image table above is
durably saved at `commit-approval/runtime_evidence/CR-458-iwx-capa-scan.py`
(sha256 recorded in the Stage 1 evidence index). Its raw output
across the 12 iwlwifi-*-68.ucode images is saved at
`commit-approval/runtime_evidence/CR-458-iwx-capa-scan.txt`. The
scanner reproduces the local TLV walker semantics deterministically
from the on-disk firmware bytes; running it requires only a Python 3
interpreter and produces the same per-image bitmaps that
`iwx_read_firmware()` would populate at runtime.

## Out of scope for this slice

- No new `IWX_UCODE_TLV_CAPA_*` or `IWX_UCODE_TLV_FLAGS_*` macro is
  added. Naming unnamed CAPA bits requires a pinned upstream donor
  commit and a fresh route decision.
- No `iwx_firmware_family_supports_ap_go()` switch arm is promoted
  from `false` to `true`. Every recognised family and the `default`
  arm still return `false`.
- No `ItlIwm` / `ItlIwn` override of `supportsAPMode()` is added.
- No change to `AirportItlwmAPSTAInterface::isLowerBackendReady()`,
  the V1 / V2 `setVIRTUAL_IF_CREATE` dispatchers,
  `IEEE80211_STA_ONLY`, the iwx HOSTAP preflight panic, the iwm
  HOSTAP preflight panic, or any AP firmware command path.
- No AP-up transition, beacon emission, AP probe-response template
  upload, AP station add / remove, AP key install, CSA / beacon
  update, AP firmware event conversion, AP client association, DHCP,
  traffic, peer-cache publication, station-table mutation, or
  SoftAP stats publication is implemented or claimed.
- No promotion of the host APSTA owner to a registered IOService /
  IO80211 interface.
- No installation, no reboot, no runtime evidence collection.
- No merging of AP station lifecycle into the WCL_REASSOC publication
  path; commit `1086f64eefb3c8f53d7625f1973113b06f838830` remains
  separate.

## Self-check anchors

- AP/APSTA parity verdict:
  `commit-approval/status/AUDITOR_VERDICT_AP_LAYER_PARITY_CLOSURE_20260509T173840_0300.md`
  sha256 `2ae7a49d627fb2b328ff1280478a273e07316bcf9c28466ea4e0ba3979838e56`.
- Capability census (route plan):
  `docs/reference/ITLWM_APGO_BACKEND_CAPABILITY_CENSUS_2026_05_09.md`
  (committed at `f4008e7a357e21ac214fdf0696abe01809fef4f5`).
- Slice 1 helper doc:
  `docs/reference/IWX_APGO_FIRMWARE_CAPABILITY_2026_05_09.md`
  (committed at `659a5ff3ef0fb7ca42dfb639a06e2ec57332b1c1`).
- Slice 2 wiring doc:
  `docs/reference/IWX_APGO_SUPPORTSAPMODE_WIRING_2026_05_09.md`
  (committed at `03ce03977b7e02be811ed9fca7556ca4a8e768da`).
- Local TLV walker:
  `itlwm/hal_iwx/ItlIwx.cpp:1401-1640` (`iwx_read_firmware`).
- Local TLV constants used by the new helper:
  `itlwm/hal_iwx/if_iwxreg.h:1245` (`IWX_UCODE_TLV_FLAGS_GO_UAPSD`)
  and `itlwm/hal_iwx/if_iwxreg.h:1405`
  (`IWX_UCODE_TLV_CAPA_BEACON_STORING`).
- Local softc fields consumed:
  `itlwm/hal_iwx/if_iwxvar.h:666` (`int sc_capaflags`) and
  `itlwm/hal_iwx/if_iwxvar.h:671` (`uint8_t sc_enabled_capa[]`).
- Local device family enum:
  `itlwm/hal_iwx/if_iwxvar.h:647-648`.
- Parent default body:
  `include/HAL/ItlHalService.hpp:119`
  (`virtual bool supportsAPMode() const { return false; }`).
- Iwx HOSTAP preflight panic (unchanged):
  `itlwm/hal_iwx/ItlIwx.cpp` at the `IEEE80211_M_HOSTAP` arm of
  `iwx_mac_ctxt_cmd_common`.
- Iwm HOSTAP preflight panic (unchanged):
  `itlwm/hal_iwm/mac80211.cpp:2016`.
- `IEEE80211_STA_ONLY` mask of `IEEE80211_M_HOSTAP` (unchanged):
  `itl80211/openbsd/net80211/ieee80211_var.h:259-267`.
