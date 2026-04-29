# AppleBCMWLAN APSTA HostAP Control And Power Reference

Source: `/srv/project/ghidra_output/AppleBCMWLAN_Core_decompiled.c`.

## `setHOST_AP_MODE(apple80211_network_data *)`

Symbol: `AppleBCMWLANIO80211APSTAInterface::setHOST_AP_MODE(...)`.
Address: `0xffffff80016884ae`.

Recovered sequence:

- read APSTA state block at object `+0x130`
- read core private block through `state+0x218 -> +0x128`
- read neighbouring owners:
  - proximity owner at core-private `+0x2c28`
  - NAN owner at core-private `+0x74f0`
  - NAN data owner at core-private `+0x74f8`
- if input is non-null and input dword `+0x1c` is nonzero:
  - when feature gate `0x46` is disabled, bring down proximity/NAN owners
    before `setHostApModeInternal(input)`
  - return the result of `setHostApModeInternal(input)`
- if input is null or input dword `+0x1c` is zero:
  - call `setHostApModeInternal(input)`
  - when feature gate `0x46` is disabled, bring up neighbouring owners only
    when core-private byte `+0x2890 & 1` is set and core-private dword
    `+0x4d8c` is either `4` or `1`
  - return the internal result, or the first nonzero bring-up result that
    replaces it

## `hostAPPowerOff()`

Symbol: `AppleBCMWLANIO80211APSTAInterface::hostAPPowerOff()`.
Address: `0xffffff8001692772`.

Recovered sequence:

- default return is `0`
- if AP-up state `state+0x26c` is zero, return `0`
- if associated station count `state+0x00` is zero:
  - call `setPowerSaveState(0, 0x0c)`
  - clear `state+0x0e`
  - call `setHostApModeInternal(NULL)`
  - notify core path `0xffffff8002219ffe` with event id `1`, null payload,
    payload size `0`, and flag `1`
  - return the internal call result
- otherwise, if `isSoftAPConcurrencyEnabled()` is false:
  - call `setPowerSaveState(3, 3)`
  - return `0`

## `isSoftAPConcurrencyEnabled()`

Symbol: `AppleBCMWLANIO80211APSTAInterface::isSoftAPConcurrencyEnabled()`.
Address: `0xffffff8001692896`.

Recovered sequence:

- feature gate `0x46` must be enabled
- core private byte `+0x4d59` masked by `0x1b` must be nonzero
- otherwise return false

## `configureLowPowerModeExit()`

Symbol: `AppleBCMWLANIO80211APSTAInterface::configureLowPowerModeExit()`.
Address: `0xffffff80016928e4`.

Recovered sequence:

- if low-power state `state+0xb4` is zero, return immediately
- otherwise dispatch low-power exit work through the interface work queue
- the work-queue gate uses vtable offset `+0x130`
- low-power disable helpers pass a 4-byte command payload
- successful low-power exit clears `state+0xb4`

## Local Scope

The local scaffold records constants, offsets, and layout witnesses only. It
does not call HostAP mode, power-off, concurrency, or low-power exit paths at
runtime.
