# CR-494 — DYNSAR_DETAIL no-producer quarantine

Date: 2026-07-15

## Scope

This correction covers only Tahoe V2/Skywalk slot `[518]`,
`getDYNSAR_DETAIL`. It removes the reset-only local DynSAR-detail cache and
its fabricated successful carrier. It retains the public virtual declaration,
the local raw input boundary, and the separate QoS/low-latency/tx-blanking
owner surfaces.

## Recovered 25C56 reference

The reference x86_64 DriverKit DEXT is
`com.apple.DriverKit-AppleBCMWLAN`:

~~~text
path: /System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN
SHA-256: 4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab
UUID: 149C0AD1-A92F-35BC-AA69-5C8815C5421E
~~~

Infra `getDYNSAR_DETAIL` wrapper `0x1000178e0` dispatches through virtual
offset `0x338` to Core `0x1001e11e4`. The selected Core body observes a null
pointer and caller `+0x08` range boundary, then writes `1` at caller `+0x00`.
For a valid request it reaches `AppleBCMWLANTxPowerManager` at
`(Core + 0x48) + 0x1598`, obtains values through
`getDynSARDetailCurId` (`0x1000b439a`) and
`getDynSARDetailCircled` (`0x1000b43b2`), and stores them at caller `+0x0c`
and `+0x10`. It passes caller bytes `+0x04/+0x08` to
`getDynSARDetailReportPerSlicePerAnt` (`0x1000b43ca`) and copies the returned
report to caller `+0x18` with a fixed `0x2d00` length before returning zero.

This establishes direct manager-backed output rather than a blank cache. It
does not recover every manager writer, full caller semantics, runtime state,
or an operational firmware lifecycle. The selected static capture is
`docs/reference/artifacts/dynsar-detail-25c56/raw.txt`.

## Local correction

AirportItlwm has no matching TxPowerManager detail producer or report source.
The prior three cache fields were only zeroed during the two init paths and
then replayed by this getter; they were never populated by DynSAR state.
Their declarations and resets are removed. The existing local `0x16` null or
out-of-range boundary is retained as a safety boundary. Every non-null,
in-range request now returns `kIOReturnUnsupported` before any caller write.

`TahoeQosDynsarContracts`, `TahoeOwnerRegistry::qosDynsar`, and the distinct
slow-wifi, low-latency, and tx-blanking getters are deliberately unchanged.

This is a no-producer quarantine, **not Apple null-input, valid-input
return-code, full carrier-layout, version, TxPowerManager/Core-state,
firmware, or runtime-selector parity**. It invokes no private selector,
IOVAR, firmware command, scan, radio transition, deployment, association, or
traffic path.

## Deterministic guard

`scripts/dynsar_detail_quarantine_report.py --check` verifies the reference
identity and raw static anchors, retained slot/ABI and local safety boundary,
non-null no-output failure, removal of the dead cache and matching local
detail producer, preservation of the separate QoS surfaces, and supersession
of the previous cache-success documentation.
