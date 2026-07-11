# CR-479: LQM card-capability producer closure

Date: 2026-07-11

## Scope

`IO80211InfraInterface::createLinkQualityMonitor` first requires public
`CARD_CAPABILITIES[10] bit 0x08`. The 25C56 implementation is not a generic
"modern firmware" or chain-count predicate. This note recovers the exact
Apple producer of that bit and records why the legacy Intel bridge must leave
it clear.

## Reference producer

The analyzed macOS 26.2 build 25C56 guest `BootKernelExtensions.kc` has
SHA-256 `eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d`.

Raw 25C56 `AppleBCMWLANCore::setupFirmware` starts at
`0xffffff800157617e`. The KDK semantic label is
`AppleBCMWLANCore::setupFirmware(AppleBCMWLANChipImage const*)` at
`0xffffff8001596346`; the known `-0x201c8` image drift is used only for the
name, while the instructions below are from the running image.

`setupFirmware` requests the controller IOVAR `wlc_ver`. On success it reads
the 16-bit response field at stack `RBP-0x74` and writes it to core-private
`+0x30c`:

```text
0xffffff800157638a  LEA RSI, "wlc_ver"
0xffffff800157639b  CALL controller IOVAR request
0xffffff80015763c5  MOVZX ECX, word ptr [RBP-0x74]
0xffffff80015763c9  MOV RAX, qword ptr [RBX+0x128]
0xffffff80015763d0  MOV dword ptr [RAX+0x30c], ECX
```

Only the expected unavailable-I/O error `0xe3ff8117` takes the local fallback:

```text
0xffffff80015763a7  CMP EAX, 0xe3ff8117
0xffffff80015763b9  MOV dword ptr [RAX+0x30c], 0x3
```

Other `wlc_ver` errors leave `setupFirmware` through its failure path. Thus
`+0x30c` is a Broadcom controller firmware-generation result, not a PCI device
ID, radio-chain count, Intel ucode API, or a firmware file-name version.

The current raw `AppleBCMWLANCore::getCARD_CAPABILITIES` entry is
`0xffffff80015c4a9e`. Its output carrier starts with a four-byte version, so
the store at carrier `+0x0e` is public `capabilities[10]`:

```text
0xffffff80015c4ab6  MOV R15D, dword ptr [core+0x30c]
0xffffff80015c4f01  CMP R15D, 0x5
0xffffff80015c4f05  JBE 0xffffff80015c4f0d
0xffffff80015c4f07  OR byte ptr [carrier+0x0e], 0x08
0xffffff80015c4f0d  JNZ 0xffffff80015c4f2d
0xffffff80015c4f20  CALL qword ptr [controller-vtable+0xa30]
0xffffff80015c4f26  CMP EAX, 0x110c
0xffffff80015c4f2b  JZ 0xffffff80015c4f07
```

The flag result from the comparison makes the predicate exact:

- set `capabilities[10] bit 0x08` when `wlc_ver` generation is greater than
  `5`;
- set it when generation equals `5` and the controller virtual returns
  `0x110c` (BCM4364);
- leave it clear below generation `5`, or at generation `5` for every other
  controller.

The retained raw instruction artifacts are:

- `10.7.6.112:~/Projects/ghidra_output/aiam_core_setup_firmware_prefix_25C56_20260711.txt`
- `10.7.6.112:~/Projects/ghidra_output/aiam_core_cardcap_current_25C56_20260711.txt`
- `10.7.6.112:~/Projects/ghidra_output/aiam_core_30c_scalar_refs_25C56_20260711.txt`

## Local disposition

The active guest adapter is the legacy Intel path (`8086:4060`, `iwn-6030`).
`ItlIwn::getFirmwareVersion()` exposes only the opaque firmware-name string;
the legacy image reader validates its own Intel firmware format but retains no
Apple-compatible `wlc_ver` generation owner. No local controller implements
the Apple virtual chip-ID query used for the BCM4364 exception.

More importantly, forcing this bit was already falsified: enabling
`cap[10] = 0x08` together with slow-WiFi reached
`IO80211PeerMonitor::createLinkQualityMonitor(3511)`, then failed to reach
DHCP and later produced the `NoCTL` failure. The later 25C56 FBT run with the
bit absent proves the current early return is the intended framework gate, not
a substitute local LQM implementation.

Therefore `TahoeCapabilityContracts::applyAppleConsistentCardCapabilityCluster`
continues to publish only `capabilities[0..9]`. The public 24-byte carrier is
zeroed before the helper runs, which leaves `capabilities[10] bit 0x08` clear.
There is deliberately no conversion from Intel firmware name, legacy firmware
header API, PCI ID, hardware revision, NSS, or chain masks to this Apple
predicate.

## Non-claims

- This does not claim Intel hardware is equivalent to a Broadcom
  `wlc_ver` generation.
- This does not enable slow-WiFi or LQM creation.
- This does not recover the PeerMonitor owner, controller chip-ID virtual, or
  LinkQualityMonitor initialization contract.
