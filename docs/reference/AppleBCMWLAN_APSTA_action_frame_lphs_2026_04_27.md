# AppleBCMWLAN APSTA Action-Frame / LPHS Contracts

Source: Tahoe `AppleBCMWLANCoreMac`, APSTA `handleEvent` action-frame branch
and adjacent LPHS power-state helpers.

Tahoe 25C56 reconfirmation on 2026-07-10 uses `handleEvent` at
`0xffffff800166f880`, action branch `0xffffff800167029f`, and
`isSoftAPConcurrencyEnabled` at `0xffffff8001672676`. Raw evidence is stored on
`10.7.6.112` as
`~/Projects/ghidra_output/aiam_apsta_action_current_25C56_20260710.range.tsv`,
`aiam_apsta_action_tail_25C56_20260710.range.tsv`, and
`aiam_apsta_helpers_25C56_20260710.c`.

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

`AirportItlwmAPSTAOwner::publishStationEventFromNet80211` now parses the
recovered v1/v2 payload forms, writes accepted LPHS action values to the
matching station entry, checks all five entries, and invokes the existing
power-state transition only when feature `0x46` plus private byte
`+0x4d59 & 0x1b` report concurrency disabled. It does not synthesize action
frames, enable AP/GO runtime, or alter the primary STA association/data path.
