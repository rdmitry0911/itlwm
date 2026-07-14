# CR-479 — PRIVATE_MAC owner/state quarantine

Date: 2026-07-14

## Scope

This correction covers only the direct virtual `setPRIVATE_MAC` / public
`getPRIVATE_MAC` pair in `AirportItlwmSkywalkInterface`. The prior local
setter copied timeout and MAC bytes into a cache even though it returned raw
`0x16`; the getter then exposed that synthetic state. The correction removes
that rejected-state cache and preserves the packed getter ABI with a zero
local baseline.

No private setter invocation, IOVAR, private ioctl, scan-MAC change, radio
transition, deployment, association, or traffic is introduced.

## Tahoe 25C56 reference recovery

The live guest x86_64 DriverKit DEXT is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`,
SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

- Infra `setPRIVATE_MAC` at `0x100018528` dispatches through Core virtual
  `+0x6f0` to `AppleBCMWLANCore::setPRIVATE_MAC` at `0x10011ee12`.
- `NULL` returns raw `0x16`. A non-null carrier can pass timeout at `+0xc` to
  `AppleBCMWLANBGScanAdapter::configureBGScanPrivateMacTimeout`, passes a
  nonzero MAC carrier at `+0x10` to
  `AppleBCMWLANBGScanAdapter::configureBGScanPrivateMac`, then uses carrier
  `+0x4` to call `enablePrivateMACForScans` or `disablePrivateMACForScans`.
  The valid path returns zero.
- Infra `getPRIVATE_MAC` at `0x1000172f4` dispatches through Core virtual
  `+0x6e8` to `AppleBCMWLANCore::getPRIVATE_MAC` at `0x100119538`.
  `NULL` returns raw `0x16`; otherwise Core reads
  `isPrivateMacEnabled`, `getPrivateMacTimeout`, and uses Commander
  `runIOVarGet("scanmac")` for the opaque trailing carrier state. That is a
  live owner/transport producer, not a cache-only getter.

## Local boundary and non-claims

AirportItlwm has no `AppleBCMWLANBGScanAdapter`, private-MAC configuration
methods, or `scanmac` command backend. It retains the raw `0x16` NULL
boundary. A non-null direct vtable carrier now returns
`kIOReturnUnsupported` before any read or cache mutation. All
`cachedPrivateMac*` state is removed.

The normal BSD dispatcher exposes only `getPRIVATE_MAC`; non-get commands
return `kIOReturnUnsupported`. The getter retains the established versioned
`0x1c` packed carrier and zero local baseline, but does not claim live Tahoe
BGScan state or valid-input return-code parity. The virtual setter remains for
ABI completeness.

## Deterministic guard

`scripts/private_mac_rejected_state_report.py --check` verifies the canonical
reference anchors, null/error boundary, absence of carrier reads and synthetic
state, retained getter ABI and dispatcher boundary, scoped owner absence, and
the corrected historical classification. Runtime deployment remains
independently blocked by the guest's forced-off Wi-Fi lifecycle state.
