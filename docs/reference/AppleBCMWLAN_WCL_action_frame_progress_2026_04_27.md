# AppleBCMWLAN WCL Action-Frame Progress Contracts

Date: 2026-04-27

Binary: `/tmp/AppleBCMWLANCoreMac`

## Symbols

- `AppleBCMWLANCore::checkActionFrameCompleteOverdue()`
  - address: `0xffffff80015ba4d2`
- `AppleBCMWLANCore::setActionFrameProgress(bool)`
  - address: `0xffffff80016344aa`
- `AppleBCMWLANCore::getActionFrameProgress()`
  - address: `0xffffff80016344be`
- `AppleBCMWLANScanAdapter::startScan(apple80211ScanRequest*)`
  - address: `0xffffff80016ccc7a`

## Core progress state

`setActionFrameProgress(bool)` loads the Apple core-private pointer from
`this+0x128` and stores the bool byte at core-private `+0x4478`.

`getActionFrameProgress()` first calls
`checkActionFrameCompleteOverdue()`, then reloads core-private `+0x4478` and
returns bit 0 only.

`setupDriver()` clears the same byte at core-private `+0x4478` during recovered
driver state initialization, adjacent to other core-private state clears.

## Overdue contract

`checkActionFrameCompleteOverdue()`:

- returns immediately when bit 0 of core-private `+0x4478` is clear.
- reads the action-frame progress start timestamp from core-private `+0x4480`.
- converts monotonic time to milliseconds and compares elapsed time with
  threshold `0x12d` ms.
- handles timestamp wrap by comparing the unsigned elapsed interval.
- when overdue, clears core-private `+0x4478`.
- logs line `0x3b1d` with
  `Found action frame completion overdue start=%llu(ms) now=%llu(ms)`.
- reports status `0xe3ff852b` through the line `0x3b1e` logging path.

## Scan interaction

`AppleBCMWLANScanAdapter::startScan(...)`:

- calls `AppleBCMWLANCore::checkActionFrameCompleteOverdue()`.
- tests bit 0 of core-private `+0x4478`.
- if still set, returns `0xe00002d5`.
- logs line `0x00a5` using the message
  `Action frame in progress. Rejecting escan request!`.

## Local alignment in CR-156

The local Tahoe owner registry now records:

- progress flag witness for Apple core-private `+0x4478`.
- progress start-ms witness for Apple core-private `+0x4480`.
- overdue threshold `0x12d`.
- overdue status `0xe3ff852b`.
- scan reject status `0xe00002d5`.
- helper semantics matching Apple:
  `getActionFrameProgress(now)` first performs the overdue check, then returns
  the low bit of the progress flag.

CR-156 intentionally does not connect this state to local scan rejection yet.
That runtime behavior requires the recovered timestamp producer and completion
clear lifecycle, otherwise the scan gate could become a guessed blocking state.
