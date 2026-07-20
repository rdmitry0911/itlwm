# Tahoe App Store / AppleMediaServices en0 Controller Crash

## Scope

This is a Tahoe-only, source-level mitigation for the observed App Store and
`appstoreagent` crash on the 10.90.10.22 Hackintosh. It changes one property
on the `NetworkController` personality in
`AirportItlwm/AirportItlwm-Tahoe-Info.plist`. It does not alter association,
credentials, SAE, routing, MAC addresses, or interface creation.

## Observed failure and local anchors

The supplied crash investigation identifies
`+[AMSDevice macAddress] -> GetFirstEthernetAddress -> CFGetTypeID(NULL)`.
AppleMediaServices obtains the first IORegistry object matched by `BSD
Name=en0`. Tahoe AirportItlwm deliberately mirrors the Skywalk interface's
BSD Name onto the `AirportItlwm` controller in
`AirportItlwmSkywalkInterface::setBSDName()`, while the controller's provider
is an `IOPCIEDeviceWrapper`, not an Ethernet interface. The controller path
therefore lacks the interface's `IOMACAddress` property.

The controller-side BSD Name publication is separately required by CoreWiFi
and is retained. Removing it would widen the behavior change and regress an
already evidenced controller identity surface.

## Fix candidate — `A-TAHOE-AMS-EN0-CONTROLLER-IOBUILTIN-001`

- status: source-level mitigation implemented; runtime validation remains
  pending a deliberately installed Tahoe candidate and reboot.
- divergence: the `NetworkController` personality classifies the
  `AirportItlwm` controller as built-in while it can also expose `BSD Name=en0`
  without owning an Ethernet MAC property.
- change: add `IOBuiltin = false` only to the Tahoe `NetworkController`
  personality (`IOClass = AirportItlwm`, `IOProviderClass =
  IOPCIEDeviceWrapper`). It is not applied to the `itlwm` PCI wrapper,
  `AirportItlwmBootNub`, the real Skywalk interface, Sonoma, or legacy
  personalities.
- expected effect: the supplied AMS disassembly indicates that AMS skips
  `IOBuiltin=false` nodes during its en0 enumeration, proceeds to the real
  interface, and reads its `IOMACAddress` rather than passing `NULL` to
  `CFGetTypeID`.
- source verification:
  `scripts/test_tahoe_appstore_iobuiltin_contract.sh` parses the plist as
  typed data, proves the exact controller scope and Tahoe target mapping, and
  proves the controller-side BSD Name publication remains present. It also
  requires the release workflow to call the project-owned Tahoe build wrapper
  and package a `AirportItlwm-Tahoe-*.zip` asset with the kext at its root;
  CI parses the built kext plist and the plist after zip extraction to require
  a typed `IOBuiltin=false` on the same controller personality. The legacy
  all-target scheme stops at Sonoma and cannot provide this asset.
  The workflow pins GitHub's explicit `macos-26-intel` runner and asserts
  `uname -m = x86_64`, because the released Tahoe kext is x86_64 and the
  moving `macos-latest` label is Arm. The hosted CI wrapper validates
  source/build output and x86_64 architecture, but has no Tahoe
  BootKernelExtensions collection, so it does not claim a BootKC/AuxKC
  symbol-admission result.
- runtime verification still required after a released candidate is manually
  installed: capture the loaded kext Info.plist/IORegistry properties, verify
  the controller has `IOBuiltin=false` and the real `en0` interface retains a
  six-byte `IOMACAddress`, then exercise App Store and observe
  `appstoreagent` across its normal background interval. This local source
  change does not authorize installation, reboot, or host interaction.
