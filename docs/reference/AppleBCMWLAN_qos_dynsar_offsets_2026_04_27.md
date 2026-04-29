# AppleBCMWLAN QoS / DynSAR / congestion-control offset contracts

Date: 2026-04-27

Scope: small Tahoe AppleBCMWLANCore helpers adjacent to the hidden-interface and
controller layers. This note records core-private offsets and simple return
contracts only. It does not enable runtime QoS IOVARs or DynSAR policy locally.

## DynSAR fail-safe window

Function:

- `AppleBCMWLANCore::wasDynSARInFailSafeMode()`
- Address: `0xffffff8001632052`

Observed behavior:

- Reads a start-tick value from core-private `+0x74e0`.
- Computes elapsed ticks from a time source.
- Shifts elapsed right by `0x0a`.
- Returns true while the shifted elapsed value is below `0x9502f9`.
- Debug logging, when enabled, uses line `0xdea9`.

## Congestion-control feature gate

Functions:

- `AppleBCMWLANCore::configureCongestionControlMechanisms(unsigned int)`
- Address: `0xffffff8001632144`
- `AppleBCMWLANCore::configureAggregationCongestionControlMechanism(unsigned int)`
- Address: `0xffffff8001632162`

Observed behavior:

- Both functions test core-private byte `+0x7584` bit `0`.
- If the bit is set, return `0`.
- If the bit is clear, return `0xe00002c7`.

## AWDL AMPDU and feature flag accessors

Functions:

- `AppleBCMWLANCore::forceAwdlAmpdu()`
- Address: `0xffffff800163428c`
- `AppleBCMWLANCore::setForceAwdlAmpdu(unsigned int)`
- Address: `0xffffff80016342a0`
- `AppleBCMWLANCore::forceDisableAwdlAmpdu()`
- Address: `0xffffff80016342b4`
- `AppleBCMWLANCore::setForceDisableAwdlAmpdu(unsigned int)`
- Address: `0xffffff80016342c8`
- `AppleBCMWLANCore::getHwFeatureFlags() const`
- Address: `0xffffff80016342dc`

Observed behavior:

- `forceAwdlAmpdu()` reads core-private `+0x3768`.
- `setForceAwdlAmpdu(...)` writes core-private `+0x3768`.
- `forceDisableAwdlAmpdu()` reads core-private `+0x3764`.
- `setForceDisableAwdlAmpdu(...)` writes core-private `+0x3764`.
- `getHwFeatureFlags()` reads core-private `+0x458c`.

## Split TX status and address resolution counters

Functions:

- `AppleBCMWLANCore::isSplitTxStatusEnabled()`
- Address: `0xffffff800163434a`
- `AppleBCMWLANCore::getTxAddrResolveReqV4()`
- Address: `0xffffff8001634360`
- `AppleBCMWLANCore::getTxAddrResolveReqV6()`
- Address: `0xffffff8001634374`

Observed behavior:

- `isSplitTxStatusEnabled()` returns bit 0 of core-private `+0x00dc`.
- `getTxAddrResolveReqV4()` reads core-private `+0x2aa4`.
- `getTxAddrResolveReqV6()` reads core-private `+0x2aa8`.

## Local alignment

- Adds `TahoeQosDynsarContracts.hpp` with recovered offsets, bit masks, status,
  threshold, and helper semantics.
- Adds `TahoeOwnerRegistry::QosDynsarOwner` as a local owner-state witness for
  the recovered fields.
- Does not call QoS IOVARs locally.
- Does not enable DynSAR policy or congestion-control runtime paths.

## Non-claims

- This batch does not claim final primary STA association or data success.
- This batch does not enable AP/SoftAP runtime.
- This batch does not implement QoS IOVAR dispatch.
- This batch does not force DynSAR, congestion, AMPDU, split-TX, or address
  resolution state.
