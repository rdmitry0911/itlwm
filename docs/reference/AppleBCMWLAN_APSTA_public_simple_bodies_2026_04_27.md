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
