# AppleBCMWLAN APSTA beacon/DTIM IOCTL reference notes, 2026-04-27

Source: Tahoe `AppleBCMWLANCoreMac` disassembly.

## setBeaconInterval

Function: `AppleBCMWLANIO80211APSTAInterface::setBeaconInterval(uint16_t)`
Range: `0xffffff8001687ae4..0xffffff8001687c7e`

Recovered contract:

- Compare requested interval with applied interval `state+0x68`.
- If equal, return without sending IOCTL.
- Build a command payload head with data pointer at `+0x0` and length word at `+0x8`.
- Payload length is `4`.
- Use commander from `state+0x228`.
- Async path stores `handleSetBcnIntervalAsyncCallBack` at callback context `+0x8` and cookie `0` at `+0x10`.
- Async path calls `sendVirtualIOCtlSet(..., 0x4c, payload, kNoRxExpected, callback, ...)`.
- Sync path calls `runVirtualIOCtlSet(..., 0x4c, payload, NULL, ...)`.
- On success, write requested interval to `state+0x68`.

## initSoftAPParameters DTIM apply

Function: `AppleBCMWLANIO80211APSTAInterface::initSoftAPParameters()`
Range: `0xffffff800168795f..0xffffff8001687ac2`

Recovered contract:

- Read DTIM period from `state+0x16`.
- If it matches applied DTIM `state+0x6a`, return without IOCTL.
- Build the same payload head with length `4`.
- Use commander from `state+0x228`.
- Async path stores `handleSetBcnDTIMPeriodAsyncCallBack` at callback context `+0x8` and cookie `0` at `+0x10`.
- Async path calls `sendVirtualIOCtlSet(..., 0x4e, payload, kNoRxExpected, callback, ...)`.
- Sync path calls `runVirtualIOCtlSet(..., 0x4e, payload, NULL, ...)`.
- On success, write DTIM period to `state+0x6a`.

## callbacks

Functions:

- `AppleBCMWLANIO80211APSTAInterface::handleSetBcnIntervalAsyncCallBack(...)`
  `0xffffff800169365a..0xffffff800169370b`
- `AppleBCMWLANIO80211APSTAInterface::handleSetBcnDTIMPeriodAsyncCallBack(...)`
  `0xffffff800169370e..0xffffff80016937bf`

Recovered contract:

- Status `0` returns immediately.
- Nonzero status logs through the interface logger when enabled.
- Both callbacks read rxPayload data pointer at `+0x0` and length at `+0x8`.
- Beacon interval callback emits bytestream telemetry through `state+0x210` with label `BCNPRD IOCTL rxPayload bytestream:`.
- DTIM callback emits bytestream telemetry through `state+0x210` with label `DTIMPRD IOCTL rxPayload bytestream:`.

Local use: CR-139 records constants/layout witnesses only; no APSTA beacon or DTIM command is sent by this note.
