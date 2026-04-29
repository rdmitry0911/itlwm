# AppleBCMWLAN WCL Action-Frame Send Contracts

Source: Tahoe `AppleBCMWLANCoreMac`, `setWCL_ACTION_FRAME` and
`AppleBCMWLANNetAdapter` action-frame send helpers.

## Recovered Entry Points

- `AppleBCMWLANCore::setWCL_ACTION_FRAME(apple80211_wcl_action_frame*)`:
  `0xffffff8001636ab4`
- `AppleBCMWLANNetAdapter::sendActionFrame(...)`:
  `0xffffff8001549050`
- `AppleBCMWLANNetAdapter::sendActionFrameV2(...)`:
  `0xffffff8001549322`

## Core Dispatch Contract

- `NULL` input returns `0xe00002bc`.
- Apple reads the net-adapter owner from core private `+0x15e0`.
- Apple selects V2 when core private `+0x30c > 0x14`, equivalent to firmware
  generation threshold `0x15`.
- Apple reads the caller carrier as:
  - category byte at input `+0x00`
  - channel dword at input `+0x04`
  - address at input `+0x08`
  - frame length word at input `+0x0e`
  - frame bytes at input `+0x10`

## V1 Send Contract

- `sendActionFrame` rejects `pData == NULL` with `0xe00002bc`.
- Total action-frame bytes must be at most `0x707`; larger requests trap/log
  the "ActionFrame size is too big!" path.
- Apple zeroes a `0x718` stack buffer and sends a fixed CommandTxPayload length
  of `0x724` through the IOVAR path.
- The no-RX expected path and callback metadata are set before dispatch.

## V2 Send Contract

- `sendActionFrameV2` also rejects total action-frame bytes `>= 0x708` with
  `0xe00002bc`.
- It allocates a dynamic buffer of `total + 0x34`, fills a version-2 action
  frame envelope, and sends the dynamic issue-command path.
- With `setWCL_ACTION_FRAME`'s current arguments the prefix length is zero, so
  the dynamic request length equals the caller frame length.

## Local Reconstruction Scope

The local Tahoe commander now keeps the same `0x708` payload capacity and
`0x15` V2 threshold, preserves the V1 fixed payload length `0x724`, and keeps
the oversized fail shape `0xe00002bc`. It does not implement real Broadcom
adapter injection or change primary association behavior.
