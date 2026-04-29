# AppleBCMWLAN APSTA teardown reference notes, 2026-04-27

Source: Tahoe `AppleBCMWLANCoreMac` decompilation/disassembly.

## freeResources

Function: `AppleBCMWLANIO80211APSTAInterface::freeResources()`
Range: `0xffffff8001685d64..0xffffff8001685e92`

Recovered contract:

- `state+0x70`: call vtable `+0x158`, call vtable `+0x28`, clear field.
- `state+0x78`: call vtable `+0x158`, call vtable `+0x28`, clear field.
- `state+0x240`: call vtable `+0x28` when present, clear field.
- `state+0x248`: call vtable `+0x28` when present, clear field.
- `state+0x250`: call vtable `+0x28` when present, clear field.
- `state+0x260`: call vtable `+0x28` when present, clear field.
- `state+0x258`: call vtable `+0x28` when present, clear field.

## stop(IOService*)

Function: `AppleBCMWLANIO80211APSTAInterface::stop(IOService*)`
Range: `0xffffff8001686a7e..0xffffff8001686c91`

Recovered contract:

- Fetch work queue using the inherited `getWorkQueue` path and validate the dispatch queue.
- Iterate TX queues from `state+0x300 + i*8` while `i < state+0x2a4`.
- For each non-null TX queue: call queue vtable `+0x158`, call work queue vtable `+0x148`, call queue vtable `+0x28` if still present, clear the slot.
- Apply the same stop/remove/release/clear sequence to `state+0x2e8` TX completion queue.
- Apply the same stop/remove/release/clear sequence to `state+0x2f0` RX completion queue.
- For `state+0x320` multicast queue: stop through vtable `+0x158`, remove through work queue vtable `+0x148`, call direct `IO80211WorkQueue::removeWorkSource`, release through vtable `+0x28`, clear the field.
- Tailcall super stop through vtable offset `+0x5d8`.

Local use: CR-137 records constants/static asserts only; no runtime APSTA teardown is enabled by this note.
