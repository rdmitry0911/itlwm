# AppleBCMWLAN APSTA Public Simple Body Reference

Source binary: `/tmp/AppleBCMWLANCoreMac`.

This note records the APSTA public method bodies whose behavior is a direct
state/output copy, fixed return, or single helper dispatch. It intentionally
excludes station/key methods with command buffers and IOVAR/IOCTL datapaths.

## Getter Bodies

- `getSSID(...) @ 0xffffff8001687c84`
  - read private state from `self+0x130`
  - read SSID length from `state+0x274`
  - return raw `0x16` when length is greater than `0x20`
  - write length to output `+0x04`
  - copy SSID bytes from `state+0x278` to output `+0x08`
  - return `0`
- `getSTATE(...) @ 0xffffff8001687dfe`
  - write value `4` to output `+0x04`
  - return `0`
- `getOP_MODE(...) @ 0xffffff8001687e0e`
  - return raw `0x16` for null input
  - write type value `1` to output `+0x00`
  - read AP-up state from `state+0x26c`
  - write mode `8` to output `+0x04` when AP-up state is nonzero
  - write mode `0` otherwise
  - return `0`
- `getPEER_CACHE_MAXIMUM_SIZE(...) @ 0xffffff80016882da`
  - write value `8` to output `+0x04`
  - return `0`
- `getHOST_AP_MODE_HIDDEN(...) @ 0xffffff80016882ea`
  - return raw `0x16` for null input
  - write value `1` at output base
  - return `0`
- `getSOFTAP_PARAMS(...) @ 0xffffff800168e7f4`
  - copy APSTA state fields `+0x18/+0x1c/+0x20/+0x24` to output
    `+0x04/+0x08/+0x0c/+0x10`
  - copy applied beacon interval `state+0x68` to output `+0x14`
  - copy mode byte `state+0x10` to output `+0x16`
  - copy `state+0x0e & 1` to output `+0x17`
  - copy byte `state+0x28` to output `+0x18`
  - return `0`
- `getSOFTAP_STATS(...) @ 0xffffff800168e838`
  - copy `0x58` bytes from `state+0x1b0` to the output
  - return `0`

## Setter Bodies

- `setSSID(...) @ 0xffffff800168dc92`
  - performs optional logging only
  - does not read or write SSID input/state
  - return `0`
- `setPEER_CACHE_CONTROL(...) @ 0xffffff8001688490`
  - read core/owner from `state+0x218`
  - call `AppleBCMWLANCore::completePeerCacheControl(input, self)`
  - ignore helper result
  - return `0`
- `setSOFTAP_PARAMS(...) @ 0xffffff800168e536`
  - read input fields directly; no null guard is present
  - compute power-hold path from input byte `+0x17` and `state+0x0e & 1`
  - when hold path is false and AP-up state `state+0x26c` is nonzero, call
    `setPowerSaveState(0, 0)` and clear `state+0x0e`
  - if input beacon interval `+0x14` is not `0xffff` and differs from
    `state+0x68`, call `setBeaconInterval(value)`
  - copy input `+0x04/+0x08/+0x0c/+0x10` to state `+0x18/+0x1c/+0x20/+0x24`
    when changed
  - copy zero-extended input byte `+0x18` to state dword `+0x28` when changed
  - when hold path is true, call `setPowerSaveState(1, 0)`
  - return `0`
- `setSOFTAP_EXTENDED_CAPABILITIES_IE(...) @ 0xffffff800168e7b8`
  - clear state qwords `+0x50` and `+0x58`
  - clear state word `+0x60`
  - copy input byte `+0x00` to state `+0x50`
  - copy input qword `+0x01` to state `+0x51`
  - copy input qword `+0x09` to state `+0x59`
  - return `0`
- `setMIS_MAX_STA(...) @ 0xffffff8001693a80`
  - read AP-up state from `state+0x26c`
  - if AP-up state is nonzero, read input dword `+0x00` and call
    `setMaxAssoc(value)`
  - ignore helper result
  - return `0`

## Local Scope

The local APSTA scaffold records these offsets, carriers, and fixed returns as
compiled witnesses only. It still does not define the final APSTA owner class,
does not route public SAP calls through APSTA methods, and does not enable
HostAP/APSTA runtime.

## Local Selector Wiring (AP-mode HostAP, Stage 1 structural)

The two recovered selectors above are now mapped to project-owned bodies on the
local AirportItlwm (V1 + V2/Tahoe) class hierarchy with the following bounded
scope. AP firmware enablement and AP runtime evidence remain residual scope.

- Selector IDs (numeric values recovered from chained-pointer rebase against
  the AppleBCMWLAN selector tables):
  - `APPLE80211_IOC_SOFTAP_EXTENDED_CAPABILITIES_IE = 403`
  - `APPLE80211_IOC_MIS_MAX_STA = 508`

- Packed request carriers in `include/Airport/apple80211_ioctl.h`:
  - `apple80211_softap_extended_capabilities_info`: 17-byte tightly packed
    blob `{ uint8_t flag00 @0x00; uint64_t value01 @0x01; uint64_t value09 @0x09 }`
    with `static_assert` for each offset and the total size.
  - `apple80211_mis_max_sta`: 0xC-byte packed blob `{ uint32_t value00 @0x00;
    uint32_t reserved04; uint32_t reserved08 }` with `static_assert` for
    `value00` offset and the total size.

- `setSOFTAP_EXTENDED_CAPABILITIES_IE(input)`: copies the three named input
  fields into project-owned APSTA state slots. The recovered Apple body
  performs two distinct steps in order: it first clears state qword `+0x50`,
  qword `+0x58` and word `+0x60` (bytes `0x50..0x61`), and then it writes
  input byte `+0x00` to state `+0x50`, input qword `+0x01` to state `+0x51`
  and input qword `+0x09` to state `+0x59`. The qword writes at state
  `+0x51` and `+0x59` are unaligned inside the cleared region. The local
  driver-private mirror reuses the same packed wire-carrier type
  `apple80211_softap_extended_capabilities_info` defined in
  `include/Airport/apple80211_ioctl.h`. That type is `__attribute__((packed))`
  and carries compile-time `static_assert` checks pinning `flag00` to
  mirror `+0x00`, `value01` to mirror `+0x01`, `value09` to mirror `+0x09`
  and the total size to 17 bytes; reusing the same type therefore makes
  the mirror layout claim mechanically true. Each scalar assignment in
  the local body fully overwrites the prior value at its mirror offset,
  which subsumes the recovered clear+write sequence for that field. The
  byte at state `+0x61` is cleared by the recovered body but is not
  written by either the recovered body or this mirror; it lies past the
  end of the 17-byte packed mirror, so the local representation does not
  need to track it. Returns `kIOReturnSuccess` per the recovered contract.

- `setMIS_MAX_STA(input)`: AP-up gate; when the gate is true, forwards
  `input.value00` to a local `setMaxAssoc()` helper and ignores the helper
  result; otherwise the body is silently a no-op. Returns `kIOReturnSuccess`
  unconditionally per the recovered contract.

- Local AP-up gate `isHostApRunning()` returns `false` structurally on this
  driver. The build defines `IEEE80211_STA_ONLY`, which hides
  `IEEE80211_M_HOSTAP` from the `ieee80211_opmode` enumeration in
  `itl80211/openbsd/net80211/ieee80211_var.h:259-268`, and the iwx/iwm
  firmware MAC-context command paths panic on any opmode that is not STA or
  MONITOR (`itlwm/hal_iwx/ItlIwx.cpp:8428`,
  `itlwm/hal_iwm/mac80211.cpp:2019`). The truthful AP-up state is therefore
  always `false`, and the recovered Apple gate falls through to the
  no-backend-call return-success branch.

- Local maxassoc backend `setMaxAssoc(value)`: writes
  `ic->ic_max_aid = clamp(value, 1, IEEE80211_AID_DEF)`. The OpenBSD
  net80211 `ieee80211_node_join()` admission loop consumes `ic_max_aid` and
  rejects beyond-limit attempts with `IEEE80211_REASON_ASSOC_TOOMANY` (17).

- AID/TIM bitmap capacity invariant. `ic_aid_bitmap` and `ic_tim_bitmap` are
  sized at attach against `ic_max_aid` using `IEEE80211_AID_DEF` when the
  field is unset (`ieee80211_node_attach` in
  `itl80211/openbsd/net80211/ieee80211_node.c`). The local driver does not
  customize `ic_max_aid` before attach, so the allocated bitmap covers AIDs
  `[1, IEEE80211_AID_DEF)`. Reducing the limit below the allocated capacity
  is always safe because the admission loop only inspects bitmap indices
  `< ic_max_aid`. Raising the limit above the allocated capacity is rejected
  by the clamp to preserve the bitmap-size invariant; resizing
  `ic_aid_bitmap`/`ic_tim_bitmap` is a separate AP-enablement concern.

- Selector dispatch wiring:
  - V1 path: `AirportSTAIOCTL.cpp` adds two cases that forward through the
    existing `IOCTL_SET(...)` macro to `AirportItlwm::setSOFTAP_EXTENDED_CAPABILITIES_IE`
    and `AirportItlwm::setMIS_MAX_STA`. Matching `FUNC_IOCTL_SET` declarations
    are added in `AirportItlwm/AirportItlwm.hpp`.
  - V2/Tahoe path: `shouldRouteTahoeSkywalkIoctlReq()` in
    `AirportItlwmV2.cpp` returns `isSet` for the two selectors so the
    Skywalk dispatcher routes them to the local interface owner;
    `AirportItlwmSkywalkInterface::processApple80211Ioctl()` adds matching
    `case` arms that call the methods on the back-pointer to the
    `AirportItlwm` instance. V2 method declarations and the project-owned
    APSTA state struct are mirrored in `AirportItlwm/AirportItlwmV2.hpp`.

- Residual scope. AP firmware enablement (iwx/iwm MAC-context HOSTAP branch,
  removal of the `IEEE80211_STA_ONLY` build flag, beacon/AP firmware command
  table, AID/TIM bitmap resize plumbing) is NOT in this Stage 1 claim. The
  selector dispatch and APSTA state mirror are wired so that future AP
  enablement work has a stable, project-owned receiver instead of a
  fall-through to `kIOReturnUnsupported (0xe00002c7)` during boot probes.
