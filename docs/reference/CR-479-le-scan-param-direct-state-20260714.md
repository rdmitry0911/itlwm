# CR-479 — LE_SCAN_PARAM direct-state correction

Date: 2026-07-14

## Scope

This correction covers only `AirportItlwmSkywalkInterface::setLE_SCAN_PARAM`.
It replaces an invented BTLE-owner/cache model with the narrow direct counters
the Tahoe reference actually updates. It introduces no BTLE transport, radio
action, firmware command, synthetic reporter, or private control path.

The port retains `NULL -> kIOReturnBadArgumentTahoe` as a local safety
boundary. Apple Core directly reads the carrier and has no null check, so this
does not claim Apple NULL or valid-input return-code parity.

## Tahoe 25C56 reference recovery

The live guest x86_64 DriverKit DEXT is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`,
SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

- Infra `setLE_SCAN_PARAM` at `0x100019414` loads Core and directly jumps to
  `AppleBCMWLANCore::setLE_SCAN_PARAM` at `0x100140d6c`; it is not a virtual
  hidden-owner dispatch.
- Core reads enable byte `+0`, peak dword `+0x4`, total dword `+0x8`, and
  duty dword `+0xc` before its branch. With a nonzero enable byte it increments
  its `+0x48` state block at `+0x78c4`, adds peak at `+0x78cc`, and adds total
  at `+0x78d0`. With a zero enable byte it increments only state `+0x78c8`.
- For every call, duty values through `6` increment the matching bucket at
  state `+0x78d4 + duty * 4`. Core then calls
  `AppleBCMWLANIOReportingPerSlice::reportBTLECnxStats` at `0x10002e00c`
  only if the optional reporting object at state `+0x1588` exists, and
  returns success.

## Local boundary and non-claims

The old local `TahoeLeScanContracts` carrier and owner cache claimed six
copied dwords and a BTLE reporting owner. Neither matches the reference. The
port now keeps enabled count, disabled count, peak sum, total sum, and seven
duty buckets directly. It intentionally does not implement the optional
reporting object because no corresponding local reporter exists.

No full opaque carrier layout, owner, reporter callback, firmware state,
association behavior, direct runtime setter invocation, or return-code parity
is claimed beyond the direct non-null local counter transition.

## Deterministic guard

`scripts/le_scan_param_direct_state_report.py --check` verifies the reference
anchors, local direct-counter branch, removal of the synthetic owner cache,
retained safety null gate, absent local reporter, and corrected historical
documentation. Runtime deployment remains independently blocked by the
guest's forced-off Wi-Fi lifecycle state.
