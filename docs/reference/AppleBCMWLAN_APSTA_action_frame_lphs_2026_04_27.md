# AppleBCMWLAN APSTA Action-Frame / LPHS Contracts

Source: Tahoe `AppleBCMWLANCoreMac`, APSTA `handleEvent` action-frame branch
and adjacent LPHS power-state helpers.

## Recovered Entry Points

- `AppleBCMWLANIO80211APSTAInterface::handleEvent(wl_event_msg_t*)`
  action-frame branch: `0xffffff80016904bf..0xffffff8001690f70`
- `AppleBCMWLANIO80211APSTAInterface::checkIfAllStaAreInLPM()`: inlined in
  the same branch, loop body `0xffffff8001690d10..0xffffff8001690f24`
- `AppleBCMWLANIO80211APSTAInterface::setPowerSaveState(...)`:
  `0xffffff8001686e62`
- `AppleBCMWLANIO80211APSTAInterface::runPowerSaveStateMachine()` log site:
  `0xffffff8001686214..0xffffff8001686234`

## Event Parse Contract

- APSTA action frames are dispatched from `handleEvent` event type `0x4b`.
- The event payload base is `wl_event_msg_t + 0x30`.
- The minimum accepted payload length is `0x12`.
- The version word is read at payload `+0x00`. Apple byte-swaps it for the
  reject test and rejects swapped versions `>= 3`.
- Raw version `0x0100` uses category/action at payload `+0x10/+0x11`, which
  are absolute event offsets `+0x40/+0x41`.
- Raw version `0x0200` requires payload length greater than `0x19` and uses
  category/action at payload `+0x18/+0x19`, absolute event offsets
  `+0x48/+0x49`.
- Unknown version leaves category/action at sentinel `0xaa`.

## LPHS State Contract

- LPHS category is `0x7f`.
- Accepted LPHS actions are `1` and `2`.
- Apple writes the accepted action value directly to station-table entry
  `state+0xb8 + index*0x30 + 0x10`.
- New station entries are initialized with sleep-state value `2`.
- `checkIfAllStaAreInLPM()` treats active entries with sleep-state `2` as the
  blocking awake/default state.
- Therefore LPHS action `1` is the low-power/sleep state, and action `2` is the
  awake/default state.
- If no active station remains in blocking state `2`, and SoftAP concurrency is
  disabled, Apple transitions to `setPowerSaveState(3, 0x0b)`.

## Log / Telemetry Constants

- `handleActionFrame` received log line: `0x10b2`
- empty payload log line: `0x10b4`
- invalid minimum length log line: `0x10b6`
- invalid version log line: `0x10bf`
- category/action contents log line: `0x10c9`
- concurrency diagnostic line: `0x10da`
- `checkIfAllStaAreInLPM` per-entry log line: `0x11cf`
- `runPowerSaveStateMachine` low-traffic log line: `0x1422`

## Local Reconstruction Scope

The local scaffold records these constants, aliases, and static asserts only.
It does not enable APSTA runtime ownership, does not synthesize action frames,
does not force a power-save state, and does not alter the primary STA
association or data path.
