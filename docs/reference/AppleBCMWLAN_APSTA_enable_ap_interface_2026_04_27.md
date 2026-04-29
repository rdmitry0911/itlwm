# AppleBCMWLAN APSTA Enable AP Interface Reference

Source: `/tmp/itlwm-re/AppleBCMWLANCoreMac` disassembly.

## `enableAPInterface()`

Range: `0xffffff800168d310..0xffffff800168d858`.

Recovered sequence:

- read APSTA state block at object `+0x130`
- read core owner at `state+0x218`
- if feature flag `0x15` is set and config path
  `core+0x128 -> +0x1558 -> +0x10 -> +0xe2` allows it, send:
  - `rrm_bcn_req_thrtl_win`
  - 4-byte payload value `0`
  - `rrm_bcn_req_max_off_chan_time`
  - 4-byte payload value `0`
- if feature flag `0x19` is set and config path
  `core+0x128 -> +0x1558 -> +0x10 -> +0xe3` allows it, send:
  - `wnm`
  - 4-byte payload value `0`
- read boot arg `wlan.ap.maxmpdu` with size `4`
- if boot-arg read fails, call `configureMPDUSize(0xffffffff)`
- if boot-arg read succeeds with nonzero value, call
  `configureMPDUSize(value)`
- if boot-arg read succeeds with zero value, skip MPDU override
- OR `0x10000` into core private field `core+0x128 -> +0x2890`
- call APSTA vtable `+0xe70` with arguments `(2, 1)`
- prepare `scb_probe` payload:
  - qword `+0x00 = 0xf0000001e`
  - dword `+0x08 = 5`
  - payload size `0x0c`
- if the commander/transport path reports async support, send virtual IOVAR
  `scb_probe` with `kNoRxExpected` and completion context:
  - owner at `+0x0`
  - callback `handleSetScbProbeAsyncCallBack` at `+0x8`
  - cookie `0` at `+0x10`
- otherwise run sync virtual IOVAR set `scb_probe` with the same payload
- notify the core path through `0xffffff8002219ffe` with:
  - event id `0x1e`
  - optional interface name pointer and length
  - interface name is discarded when length is `>= 0x11`
  - flag `1`
- call APSTA vtable `+0xb18` with selector `4` and zero payload arguments
- call `AppleBCMWLANCore::addEventBit(5)`
- tailcall `AppleBCMWLANCore::writeEventBitField()`

## Local Scope

The local scaffold records constants and layout witnesses only. It does not
call `enableAPInterface`, send RRM/WNM/MPDU/scb_probe commands, publish AP
link-up, or write event bits at runtime.
