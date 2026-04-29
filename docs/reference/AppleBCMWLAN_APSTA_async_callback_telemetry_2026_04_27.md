# AppleBCMWLAN APSTA Async Callback And Telemetry Reference

Source: Tahoe `AppleBCMWLANCoreMac` disassembly.

## IPv4 Packet Filter Delete

Producer: `AppleBCMWLANIO80211APSTAInterface::setHostApModeInternal(...)`
around `0xffffff8001688d12..0xffffff8001688d84`.

Callback:
`AppleBCMWLANIO80211APSTAInterface::deleteIPv4PktFiltersAsyncCallBack(...)`
at `0xffffff8001692b52`.

Recovered contract:

- Before ARP offload setup, the HostAP path writes dword `0x6c` into a local
  4-byte payload and sends virtual IOVAR `pkt_filter_delete`.
- The command source is `state+0x228`.
- The command uses no RX expected, completion cookie `0`, and callback
  `deleteIPv4PktFiltersAsyncCallBack`.
- The immediate send failure path logs at level `1`; the async callback logs
  nonzero status at level `2` and otherwise returns.
- The callback uses APSTA logger slot `+0xd08` and error-string conversion
  through owner `state+0x218` vtable `+0x780`.

## Beacon Interval Producer

Function: `AppleBCMWLANIO80211APSTAInterface::setBeaconInterval(uint16_t)`
at `0xffffff8001687ae4`.

Recovered contract:

- If requested interval equals applied interval `state+0x68`, return without
  sending.
- The payload is 4 bytes, sourced from the requested interval local.
- Async mode uses virtual IOCTL set selector `0x4c`, no RX expected, completion
  cookie `0`, and callback `handleSetBcnIntervalAsyncCallBack`.
- Sync mode uses virtual IOCTL set selector `0x4c` with no RX payload.
- On successful send or sync set, `state+0x68` is written with the requested
  interval.
- Sync failure logs at line `0x106b` and level `1`.

## Beacon DTIM Producer

Producer: `AppleBCMWLANIO80211APSTAInterface::initSoftAPParameters()`
at `0xffffff8001687888`.

Recovered contract:

- The DTIM period source is `state+0x16`; applied DTIM is `state+0x6a`.
- If source equals applied value, return without sending.
- The payload is 4 bytes.
- Async mode uses virtual IOCTL set selector `0x4e`, no RX expected, completion
  cookie `0`, and callback `handleSetBcnDTIMPeriodAsyncCallBack`.
- Sync mode uses virtual IOCTL set selector `0x4e` with no RX payload.
- On successful send or sync set, `state+0x6a` is written with `state+0x16`.
- Sync failure logs at line `0x1091` and level `1`.

## Beacon Async Callbacks

Functions:

- `handleSetBcnIntervalAsyncCallBack(...)` at `0xffffff800169365a`
- `handleSetBcnDTIMPeriodAsyncCallBack(...)` at `0xffffff800169370e`

Recovered contract:

- Status `0` returns immediately.
- Nonzero status first logs at level `1` when the logger allows it.
- After the optional log, both callbacks emit the RX payload through telemetry
  resource `state+0x210`.
- The `CommandRxPayload` data pointer is read from offset `+0x00`, and length
  is read from offset `+0x08`.
- The telemetry flag is `1`.
- Beacon interval uses label `BCNPRD IOCTL rxPayload bytestream: `.
- Beacon DTIM uses label `DTIMPRD IOCTL rxPayload bytestream: `.
- Callback log lines are `0x1079` for beacon interval and `0x109f` for DTIM.

## Local Scope

The local scaffold records constants, strings, offsets, and static asserts
only. It does not execute APSTA runtime paths, does not send IOVARs, and does
not alter primary STA behavior.
