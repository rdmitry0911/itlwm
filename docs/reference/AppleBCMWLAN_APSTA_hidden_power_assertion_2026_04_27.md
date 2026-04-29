# AppleBCMWLAN APSTA Hidden Mode And Power Assertion Reference

Source: `/tmp/itlwm-re/AppleBCMWLANCoreMac` disassembly.

## `setHOST_AP_MODE_HIDDEN(...)`

Range: `0xffffff800168d970..0xffffff800168dbbc`.

Recovered sequence:

- read APSTA state block at object `+0x130`
- require AP-up state `state+0x26c != 0`
- return `6` when AP is not up
- return raw invalid argument `0x16` for null input
- read hidden value from input `+0x4`
- reject values greater than `1`
- send virtual IOVAR `closednet` through commander `state+0x228`
- TX payload points to the hidden value and has size `4`
- on IOVAR success, write `state+0x0d = (hidden != 0)`
- if hidden value is zero and AP remains up:
  - call `setPowerSaveState(0, 9)`
  - clear `state+0x0e`
  - call `holdSoftAPPowerAssertion()`

## `holdSoftAPPowerAssertion()`

Range: `0xffffff800168dbc2..0xffffff800168dc8c`.

Recovered sequence:

- build 4-byte payload value `1`
- write `state+0x0c = 1`
- read core owner from `state+0x218`
- notify core path `0xffffff8002219ffe` with:
  - resource `core+0x128 -> +0x2c20`
  - event id `0x8d`
  - payload pointer to the 4-byte value `1`
  - payload size `4`
  - flag `1`

## Local Scope

The local scaffold records constants only. It does not call hidden-mode setters,
send `closednet`, change power-save state, or hold power assertions at runtime.
