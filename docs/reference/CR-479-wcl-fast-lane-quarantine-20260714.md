# CR-479 — WCL Fast Lane false-success quarantine

Date: 2026-07-14

## Scope

This correction covers only
`AirportItlwmSkywalkInterface::setWCL_UPDATE_FAST_LANE`. The local direct null
guard remains `0xe00002bc`. Every non-null carrier now returns
`kIOReturnUnsupported` before it can be advertised as a completed Fast Lane
policy request.

The recovered code observes byte `+0` and, conditionally, byte `+1`; that is
not a recovered complete public carrier ABI. No private carrier is constructed
and no firmware request is synthesized.

## Tahoe 25C56 reference recovery

The live guest x86_64 DriverKit DEXT is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`,
SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

- Infra slot `[632]`,
  `AppleBCMWLANInfraProtocol::setWCL_UPDATE_FAST_LANE`, is at `0x100019916`.
  It returns `0xe00002bc` only for a null carrier.
- For a non-null carrier, it converts byte `+0` to a Boolean and invokes its
  vtable entry `+0x1a8`. Binding cell `0x1003964b0` resolves that dispatch to
  `IOUserNetworkWLAN::setFastlaneCapable(bool)`, identifying the capability
  boundary.
- When both observed bytes `+0` and `+1` are non-zero, it follows Infra `+0x88`
  to Core, calls `AppleBCMWLANCore::getNetAdapter` at `0x1000c7dd6`, obtains
  the NetAdapter through Core state `+0x48`, then `+0x15e0`, and calls
  `AppleBCMWLANNetAdapter::overrideACMConfiguration` at `0x10019e2c6`.
- `overrideACMConfiguration` reads WME AC `3`, clears its ACM bit `0x10`,
  installs `configureACMOverrideForFastLaneAsyncCallback` at `0x1000168c4`,
  and calls `configureWmeParamsAsync` at `0x100013664`.
  `configureWmeParamsAsync` submits the `wme_ac_sta` IOVAR through
  `AppleBCMWLANCommander::sendIOVarSet`; the owner path has a real enqueue
  result and asynchronous completion lifecycle.

The wrapper initializes its ordinary non-null return to success and does not
propagate the WME submission result. That return shape does not authorize the
port to report success without performing the preceding capability and WME/ACM
work.

## Local boundary and non-claims

The Intel port contains none of `setFastlaneCapable`,
`overrideACMConfiguration`, `configureWmeParamsAsync`, or the Fast Lane ACM
completion owner. It has no equivalent `wme_ac_sta` transport. The former
non-null success was therefore a false acknowledgement of a traffic-policy
operation that did not occur.

This change does not alter association, roaming, scans, radio power, QoS
paths, or normal public rejoin. It does not claim Apple valid-input return-code
parity and it does not add a guessed IOVAR, callback, firmware payload, or
direct runtime setter invocation.

## Deterministic guard

`scripts/wcl_fast_lane_quarantine_report.py --check` verifies the live source
boundary, the absence of the scoped Fast Lane/WME owner names, the exact
reference anchors above, and the corrected historical documentation. Runtime
deployment remains gated independently by the current forced-off guest state.
