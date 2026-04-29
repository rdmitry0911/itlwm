# AppleBCMWLAN APSTA reset/initSoftAPParameters reference notes, 2026-04-27

Source: Tahoe `AppleBCMWLANCoreMac` decompilation/disassembly.

## reset

Function: `AppleBCMWLANIO80211APSTAInterface::reset()`
Range: `0xffffff8001686cc6..0xffffff8001686e32`

Recovered contract:

- Clear `state+0x26c`.
- Clear byte `state+0x329`.
- If core expansion byte `+0x288d` bit `0x2` is clear, call the inherited link/update path with `0xffffffff`.
- Call `AppleBCMWLANCore::setConcurrencyState(4, false)` through owner/core at `state+0x218`.
- Zero `state+0xb8` for `0xf0` bytes.
- Clear `state+0x0` and qword `state+0xb0`.
- Call `setPowerSaveState(0, 0xa)`.
- Invoke timer sources `state+0x70` and `state+0x78` through vtable `+0x218`.
- Clear stats qwords `state+0x1b0..+0x200`.
- Clear runtime qwords `state+0x90`, `state+0x98`, and `state+0xa0`.

## initSoftAPParameters

Function: `AppleBCMWLANIO80211APSTAInterface::initSoftAPParameters()`
Range: `0xffffff8001687888..0xffffff8001687ade`

Recovered contract:

- Clear stats qwords `state+0x1b0..+0x200`.
- Clear qword `state+0x1a8`.
- Zero `state+0xb8` for `0xf0` bytes.
- Clear `state+0x0`.
- Write `state+0x16 = 1`.
- Write `state+0x18 = 0x0f` and `state+0x1c = 0x1e`.
- Write `state+0x20 = 0x708` and `state+0x24 = 0x0a`.
- Write `state+0x28 = 3`.
- Call `setBeaconInterval(state+0x14)`.
- If `state+0x16 != state+0x6a`, send or run IOCTL `0x4e` through commander `state+0x228`, then write `state+0x6a = state+0x16` on success.

Local use: CR-138 records constants/static asserts only; no runtime APSTA reset or SoftAP initialization is enabled by this note.
