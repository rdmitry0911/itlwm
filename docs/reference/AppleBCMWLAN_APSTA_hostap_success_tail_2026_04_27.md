# AppleBCMWLAN APSTA HostAP success-tail reference notes, 2026-04-27

Source: Tahoe `AppleBCMWLANCoreMac` disassembly.

Function: `AppleBCMWLANIO80211APSTAInterface::setHostApModeInternal(...)`
Range: `0xffffff800168c138..0xffffff800168c296`

Recovered success-tail contract:

- Write `state+0x26c = 1`.
- Write `state+0x20c = 0`.
- Write `state+0x88 = 0`.
- Call `handleAPStatsUpdates(state+0x70)`.
- Schedule monitor timer `state+0x78` through vtable `+0x1d0` with interval `0x3e8`.
- Read network-data flags from input `+0x4`.
- If flags bit `8` is set, write beacon interval `0x64` to `state+0x14`; otherwise write `0x12c`.
- If flags bit `9` is clear, skip closednet IOVAR and continue to `initSoftAPParameters()`.
- If flags bit `9` is set, build a 4-byte payload value `1`, use commander `state+0x228`, and call `runVirtualIOVarSet(..., "closednet", payload, NULL, ...)`.
- On closednet error, log and continue to the common path. On success, optionally log and continue to the common path.
- Common path calls `initSoftAPParameters()`.

Local use: CR-140 records constants only; no HostAP success state, timer schedule, or closednet command is executed by this note.
