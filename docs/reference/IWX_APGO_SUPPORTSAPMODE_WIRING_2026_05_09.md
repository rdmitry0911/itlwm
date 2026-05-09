# iwx `supportsAPMode()` Wiring — Fail-Closed AP/GO HAL Capability Gate

## Purpose

This document describes the iwx-only `ItlIwx::supportsAPMode() const`
override that consults the project-owned
`iwx_firmware_family_supports_ap_go()` helper added in
`itlwm/hal_iwx/IwxApGoCapability.hpp`.

The override is the second concrete implementation slice in the route
plan recorded by
`docs/reference/ITLWM_APGO_BACKEND_CAPABILITY_CENSUS_2026_05_09.md`
(slice 1 introduced the helper at master
`659a5ff3ef0fb7ca42dfb639a06e2ec57332b1c1`). This slice routes the iwx
HAL through a single project-owned classification surface for AP/GO
capability without changing observable behaviour: the helper returns
`false` for every recognized device family, so the override returns
`false` for every iwx device, identical to the
`ItlHalService::supportsAPMode()` default it overrides.

## Override behaviour

`itlwm/hal_iwx/ItlIwx.hpp` declares the override in the public section
of `class ItlIwx`:

```
bool supportsAPMode() const override;
```

`itlwm/hal_iwx/ItlIwx.cpp` defines the body as a single statement that
delegates to the helper:

```
bool ItlIwx::
supportsAPMode() const
{
    return iwx_firmware_family_supports_ap_go(com.sc_device_family);
}
```

`itlwm/hal_iwx/ItlIwx.hpp` includes `itlwm/hal_iwx/IwxApGoCapability.hpp`
directly, in place of its previous direct `#include "if_iwxvar.h"`.
The helper transitively pulls `if_iwxvar.h` for the
`IWX_DEVICE_FAMILY_*` constants the switch needs. Routing
`if_iwxvar.h` through the helper keeps it included exactly once per
translation unit even when `IwxApGoCapability.hpp` is later wired
into additional consumers; `if_iwxvar.h` is a vendor-style header
without an include guard, so a second direct `#include "if_iwxvar.h"`
in any TU that already pulls the helper would redefine its
`struct iwx_*` types. The helper itself is guarded by
`IwxApGoCapability_hpp`, so any TU that includes both `ItlIwx.hpp`
and `IwxApGoCapability.hpp` resolves to a single inclusion.

## Capability gate semantics

`com.sc_device_family` is the local iwx device-family classification
recorded at PCI attach time, taking one of two values defined at
`itlwm/hal_iwx/if_iwxvar.h:647-648`:

```
#define IWX_DEVICE_FAMILY_22000  1
#define IWX_DEVICE_FAMILY_AX210  2
```

The helper's switch arms for both families return `false`, and the
`default` arm returns `false` so unknown future device families fail
closed. Therefore the override returns `false` for every iwx device
the driver can attach today.

## Observable-behaviour invariant

The override does not change any observable behaviour:

- `ItlHalService::supportsAPMode() const` (the parent default at
  `include/HAL/ItlHalService.hpp:119`, committed in CR-451 at
  `a768896bce57c66884f9bb738de18fb248776942`) returns `false`.
- `ItlIwx::supportsAPMode() const` (this override) returns `false` on
  every iwx device family known today.

Both return `false`. Code that reads `hal->supportsAPMode()` sees the
same answer before and after this slice.

## Wiring status

This slice wires the iwx HAL through the helper but does not promote
any AP/GO call site:

- The helper is now consulted by `ItlIwx::supportsAPMode()`. Slice 1's
  no-caller property is intentionally retired by this slice.
- No iwm override is added; `ItlIwm::supportsAPMode()` continues to
  inherit the parent default `false`. The iwm path remains
  fail-closed via the parent default plus its own preflight panic at
  `itlwm/hal_iwm/mac80211.cpp:2016`.
- `AirportItlwmAPSTAInterface::isLowerBackendReady()` is unchanged
  and still returns `false`.
- The V1 (`AirportSTAIOCTL.cpp`) and V2
  (`AirportItlwmSkywalkInterface.cpp`) `setVIRTUAL_IF_CREATE`
  dispatchers are unchanged.
- `ItlIwx` does not implement `startAPMode`, `stopAPMode`,
  `updateAPBeacon`, `setAPKey`, `triggerAPCSA`, or
  `sendAPStationCommand`. All five remain at the
  `ItlHalService` defaults (return `kIOReturnUnsupported`).

## Residual blockers

To promote any per-family entry of
`iwx_firmware_family_supports_ap_go()` from `false` to `true`:

1. Add AP/SoftAP firmware capability TLV constants to
   `itlwm/hal_iwx/if_iwxreg.h` (donor: upstream Linux iwlwifi
   `IWL_UCODE_TLV_CAPA_AP_LINK_PS` and related; upstream reference,
   not present in this tree). Without those constants, the
   firmware-image TLV walker has nothing to test.
2. Add a TLV-walker call site that loads the firmware image and sets
   capability bits on a local capability carrier. Today the iwx
   firmware loader does not record AP-related capability bits.
3. Land the AP-mode arm of `iwx_mac_ctxt_cmd_common`
   (`itlwm/hal_iwx/ItlIwx.cpp`, panic at the `IEEE80211_M_HOSTAP`
   preflight). Without this arm, promoting any helper entry to
   `true` would mislead `supportsAPMode()` into accepting AP-mode
   requests that would still fail at the panic guard.
4. Land HostAP enablement under a scoped opt-out of
   `IEEE80211_STA_ONLY`
   (`itl80211/openbsd/net80211/ieee80211_var.h:259-267`).

These are slices 3 and 4 in the route plan and remain residual.

## Out of scope for this slice

- No promotion of any helper switch arm from `false` to `true`.
- No `ItlIwm` override of `supportsAPMode()`.
- No implementation of `startAPMode`, `stopAPMode`, `updateAPBeacon`,
  `setAPKey`, `triggerAPCSA`, or `sendAPStationCommand` on `ItlIwx`
  or `ItlIwm`.
- No change to `AirportItlwmAPSTAInterface::isLowerBackendReady()`.
- No removal of `IEEE80211_STA_ONLY`.
- No alteration of the iwx HOSTAP panic or the iwm HOSTAP panic at
  `itlwm/hal_iwm/mac80211.cpp:2016`.
- No AP firmware command implementation, no station-event producer
  bridge, no APSTA owner promotion, no AP-up transition, no
  beaconing, no AP client association, no DHCP, no traffic, no
  peer-cache publication, no station-table mutation, no SoftAP stats
  publication, no role-7 success.
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
- iwx capability helper (slice 1):
  `itlwm/hal_iwx/IwxApGoCapability.hpp` (committed at
  `659a5ff3ef0fb7ca42dfb639a06e2ec57332b1c1`).
- Slice 1 doc (helper introduction):
  `docs/reference/IWX_APGO_FIRMWARE_CAPABILITY_2026_05_09.md`
  (committed at `659a5ff3ef0fb7ca42dfb639a06e2ec57332b1c1`).
- Host APSTA owner skeleton:
  `AirportItlwm/AirportItlwmAPSTAInterface.hpp` (committed at
  `e7a3b0547837fcad976e9e8ecc61ef09fc298ff5`).
- Local iwx device family enum:
  `itlwm/hal_iwx/if_iwxvar.h:647-648`.
- Parent default body:
  `include/HAL/ItlHalService.hpp:119`
  (`virtual bool supportsAPMode() const { return false; }`).
