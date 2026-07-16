# CR-479 — WCL Fast Lane boundary and runtime recovery

Date: 2026-07-14

## Original quarantine scope

The original correction covered only
`AirportItlwmSkywalkInterface::setWCL_UPDATE_FAST_LANE`. The local direct null
guard remains `0xe00002bc`. At that point every non-null carrier returned
`kIOReturnUnsupported`, because no runtime invocation or native EDCA mapping
had yet been established.

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

## Original boundary and non-claims

The Intel port contains none of `setFastlaneCapable`,
`overrideACMConfiguration`, `configureWmeParamsAsync`, or the Fast Lane ACM
completion owner. It has no equivalent `wme_ac_sta` transport. The former
non-null success was therefore a false acknowledgement of a traffic-policy
operation that did not occur.

The original change did not alter association, roaming, scans, radio power,
QoS paths, or normal public rejoin. It did not claim Apple valid-input
return-code parity and did not add a guessed IOVAR, callback, firmware payload,
or direct runtime setter invocation.

## Runtime recovery — 2026-07-16

An exact Tahoe runtime bisect established that this is not an optional policy
for the local join path:

- `1044083fc36b4ab4cb2f4c7a401fe150ac88d2b1` (the direct parent) completes
  association, DHCP, and a gateway ping on the channel-6 control AP;
- `3a8a5a32c243bdab9591e8a7357894f9b61790e3` changes only this non-null
  setter to `kIOReturnUnsupported` and reproduces the post-WPA disconnect;
- the live trace records that setter's `0xe00002c7` result immediately before
  the packet-queue reset.

The recovered carrier ABI reads only byte `+0`, and byte `+1` when byte `+0`
is nonzero. The local recovery preserves the null gate, applies the equivalent
native host admission-control operation for an enabled pair by clearing
`ic_edca_ac[EDCA_AC_VO].ac_acm`, and asks the existing `ic_updateedca` backend
to refresh native EDCA once the BSS is running. Clearing the local ACM flag is
not claimed to serialize that bit into IWN/IWM/IWX firmware; its direct local
effect is to stop the native VO-to-VI admission-control downgrade. The EDCA
refresh itself uses each Intel backend's established firmware/MAC-context
route.

The port still does not implement Apple's `wme_ac_sta` IOVAR or its async
completion callback, and does not claim full Apple valid-input return-code or
transport parity. The public wrapper likewise ignores its async completion;
the local implementation returns success only after applying its real native
counterpart or returns not-ready/unsupported when that counterpart is absent.

## Deterministic guard

`scripts/wcl_fast_lane_quarantine_report.py --check` verifies the live native
Fast Lane/EDCA bridge, the exact reference anchors above, and the recorded
runtime boundary. Runtime deployment remains independently gated.
