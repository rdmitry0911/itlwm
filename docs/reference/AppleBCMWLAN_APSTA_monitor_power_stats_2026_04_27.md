# AppleBCMWLAN APSTA Monitor, Power-State, And Stats Reference

Source: Tahoe `AppleBCMWLANCoreMac` disassembly.

## Stats Update Timer

Function: `AppleBCMWLANIO80211APSTAInterface::handleAPStatsUpdates(IO80211TimerSource*)`
at `0xffffff8001685a36`.

Recovered contract:

- The timer argument must match `state+0x70`; if AP state `state+0x26c` is
  down, the timer action path goes through vtable `+0x218`.
- The active path allocates `0x808` bytes, calls APSTA vtable `+0xfd8`, accepts
  `0xe00002d8` as async-submit failure, and calls
  `checkForStationListMismatch` on successful output.
- Activity uses baseline `state+0x88`. The optional firmware activity source
  reads core-private `+0x4d59` and, when NAN/IR counter bits and private
  `+0x757e` bit `1` are set, sums four qwords from `+0x2a78`.
- Unchanged activity adds `0x1388` to `state+0x20c`. At threshold
  `0x16e361`, it posts STA message id `0x0d` with a `0x0c` byte payload
  `{ qword 0, dword 0xffffffff }`. At `0x170a71`, it clears the age counter.
- Changed activity updates `state+0x88` and clears `state+0x20c`.
- The timer is rescheduled through vtable `+0x1d0` at interval `0x1388`.

## Monitor Timer

Function: `AppleBCMWLANIO80211APSTAInterface::monitorAPInterface(IO80211TimerSource*)`
at `0xffffff8001685e94`.

Recovered contract:

- The timer argument must match `state+0x78`; if AP state is down, the timer
  action path goes through vtable `+0x218`.
- When timestamp `state+0x1a8` is present, the method accumulates elapsed
  power-state duration for current state `state+0x10`.
- Core-private byte `+0x4d59` bit `0` is mirrored into `state+0x208`. When it
  changes, or when `state+0x62 & 1` is set, the method programs Apple vendor IE
  and clears `state+0x62`.
- The method walks active station entries at `state+0xb8`, stride `0x30`, and
  tracks minimum active time for associated stations.
- Firmware RX activity is derived from the same optional four-counter source as
  the stats timer, then APSTA vtable `+0xc38` contributes four RX counters.
- Firmware and interface RX deltas compare against baselines `state+0x90` and
  `state+0x98`; deltas are accumulated into `state+0x1b8` and `state+0x1c0`.
- If SoftAP concurrency is disabled and the RX delta is below `state+0x24`, the
  low-traffic counter at `state+0x64` increments; otherwise it is cleared.
- Power-save transitions are enabled by `state+0x0e & 1`: state `1` can move to
  `2`, state `2` can return to `1` or move to `3`, and state `3` can return to
  `1`, all using reason `4`.
- The timer is rescheduled through vtable `+0x1d0` at interval `0x3e8`.

## Power-State Machine

Function: `AppleBCMWLANIO80211APSTAInterface::setPowerSaveState(...)`
at `0xffffff8001686e62`.

Recovered contract:

- The method is gated by `state+0x0e & 1`.
- Reason `7` (Infra scan) is logged and ignored.
- On actual state change, the previous duration is accumulated, transition
  count at `state+0x1c8 + new_state * 0x10` is incremented, and timestamp
  `state+0x1a8` is refreshed.
- State `0` releases power assertion for reset/power-off reasons `0x0a` and
  `0x0c`, clears `state+0x64`, exits low-power mode when AP is up, and disables
  beacon duty cycle.
- State `1` enables beacon duty cycle, configures duty-cycle params from
  `state+0x28`, and releases the power assertion.
- State `2` enables beacon duty cycle and, when `state+0xb4 != 1`, programs
  `modesw_bcns_wait` payload `0x0a` and then `lphs_mode` payload `1`.
- State `3` holds the SoftAP power assertion, exits low-power mode, disables
  beacon duty cycle, and clears `state+0x64`.
- After side effects, current power state is written to `state+0x10`.

## Assoc List Callback And Conversion

Functions:

- `getAssocListAsyncCallback(...)` at `0xffffff80016880fe`
- `convertBCMAssocListToAppleAssocList(...)` at `0xffffff80016881f6`

Recovered contract:

- The async callback returns on nonzero status. On successful RX payload, it
  allocates `0x808`, converts the BCM maclist, calls
  `checkForStationListMismatch`, and frees the buffer.
- BCM maclist count is at `+0x00`; MAC addresses start at `+0x04` with stride
  `6`.
- Apple output is version `1`, count at `+0x04`, entries at `+0x08`, and entry
  stride `0x10`.
- Counts `>= 0x81` are clamped to `0x80`.
- Each output entry writes valid dword `1`, four MAC bytes at entry `+0x04`,
  MAC tail word at entry `+0x08`, and clears the reserved dword at `+0x0c`.

## Management Frame Protection

Function:
`AppleBCMWLANIO80211APSTAInterface::configureManagementFrameProtectionForSoftAP(unsigned int)`
at `0xffffff800168c4fe`.

Recovered contract:

- The method is feature-gated by feature `0x26`.
- Unsupported feature returns success `0`.
- Supported feature sends virtual IOVAR `mfp` with 4-byte payload.
- IOVAR failure is returned by the method, but the log explicitly says AP
  initialization should continue.

## Datapath Print And RX Counter

Functions:

- `printDataPath(userPrintCtx*)` at `0xffffff8001694176`
- `updateRxCounter(uint64_t)` at `0xffffff8001694450`

Recovered contract:

- `printDataPath` uses `userPrintCtx` offsets `+0x18`, `+0x20`, `+0x24`, and
  `+0x28`.
- It prints interface role/BSD name, then prints each TX subqueue through
  vtable `+0x338`, TX completion through `+0x320`, RX completion through
  `+0x328`, and finally the superclass path through `+0xc68`.
- `updateRxCounter` adds the supplied value to `state+0xa0`.

## Local Scope

The local scaffold records constants, offsets, carriers, and static asserts
only. It does not enable APSTA runtime, does not send APSTA IOVARs, and does
not change primary STA behavior.
