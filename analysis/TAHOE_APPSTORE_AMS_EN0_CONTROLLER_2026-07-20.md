# Tahoe App Store / AppleMediaServices en0 Controller Crash

## Scope

This is a Tahoe-only, source-level mitigation for an observed App Store and
`appstoreagent` crash on a Tahoe Hackintosh. It changes one property on the
`NetworkController` personality in
`AirportItlwm/AirportItlwm-Tahoe-Info.plist`. It does not alter association,
credentials, SAE, routing, MAC addresses, or interface creation.

## Observed failure and local anchors

The supplied crash investigation identifies
`+[AMSDevice macAddress] -> GetFirstEthernetAddress -> CFGetTypeID(NULL)`.
AppleMediaServices obtains the first IORegistry object matched by `BSD
Name=en0`. Tahoe AirportItlwm deliberately mirrors the Skywalk interface's
BSD Name onto the `AirportItlwm` controller in
`AirportItlwmSkywalkInterface::setBSDName()`, while the controller's provider
is an `IOPCIEDeviceWrapper`, not an Ethernet interface. The supplied diagnosis
is therefore compatible with an AMS implementation that follows that provider
after finding the controller.

This distinction matters. A later read-only, aggregate-only lab topology
probe found that the controller itself has a six-byte `IOMACAddress`, while
its immediate `IOPCIEDeviceWrapper` provider does not. The source and probe do
not prove which object closed-source AMS dereferences, so this document does
not claim that the controller lacks a MAC property or that the causal path is
fully established.

The controller-side BSD Name publication is separately required by CoreWiFi
and is retained. Removing it would widen the behavior change and regress an
already evidenced controller identity surface.

## Fix candidate — `A-TAHOE-AMS-EN0-CONTROLLER-IOBUILTIN-001`

- status: source-level mitigation implemented. An exact released candidate
  has completed a read-only topology check and Wi-Fi regression gate in the
  lab; App Store crash reproduction remains pending on the affected OS build.
- divergence: the `NetworkController` personality classifies the
  `AirportItlwm` controller as built-in while it can also expose `BSD Name=en0`
  and has an `IOPCIEDeviceWrapper` provider without an Ethernet MAC property.
- change: add `IOBuiltin = false` only to the Tahoe `NetworkController`
  personality (`IOClass = AirportItlwm`, `IOProviderClass =
  IOPCIEDeviceWrapper`). It is not applied to the `itlwm` PCI wrapper,
  `AirportItlwmBootNub`, the real Skywalk interface, Sonoma, or legacy
  personalities.
- expected effect: the supplied AMS disassembly is consistent with AMS
  skipping `IOBuiltin=false` nodes during its en0 enumeration. If the supplied
  provider-dereference hypothesis is correct, this lets enumeration continue
  to the real interface instead of reaching the wrapper's absent property.
  This is a bounded hypothesis, not a claim that this source change has
  reproduced or eliminated the closed-source crash.
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
## Read-only lab observation and remaining validation

The release-bound QEMU overlay scenario recorded in
`analysis/TAHOE_LAB_BB7366B_OVERLAY_RUNTIME_2026-07-21.md` verifies only these
properties: one AirportItlwm controller has typed `IOBuiltin=false` and a
mirrored BSD Name; that controller has a six-byte MAC; its direct wrapper
provider lacks one; and one AirportItlwm Skywalk interface has a six-byte MAC.
The same candidate passed the four-cycle radio/data-plane regression gate.

That scenario does not launch App Store, log in, make purchases, clear
Keychain state, observe `appstoreagent` over its background interval, or
reproduce the crash on the affected OS build. Those are still required before
claiming an App Store fix. This local source change does not authorize
physical-host installation, reboot, or interaction.
