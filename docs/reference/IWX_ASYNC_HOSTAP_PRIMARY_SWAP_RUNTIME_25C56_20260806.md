# IWX asynchronous HostAP primary-SWAP publication (25C56, 2026-08-06)

## Reference boundary

Tahoe `airportd` routes the standard HostAP lifecycle through the primary
interface mode.  Its recovered start path rejects a primary interface already
in SWAP mode, while its stop path rejects any primary interface which is not in
SWAP mode:

`/home/dima/Projects/ghidra_output/airportd_internet_sharing_25C56_20260802T0720Z/airportd_hostap.c`

The exact 25C56 BootKC implementation of
`AppleBCMWLANCore::getOP_MODE` checks only the APSTA owner word at state
`+0x26c` before calling the APSTA `getOP_MODE` vtable member and merging that
mode into the primary carrier:

`/home/dima/Projects/ghidra_output/itlwm_full_sta_parity_decomp_20260514T160154/07_xrefs/BootKC_full_STA/0982_0xffffff80015e564a___ZN16AppleBCMWLANCore10getOP_MODEEP22apple80211_opmode_data.asm.txt`

The recovered APSTA HostAP success tail sets that same word to one.  There is
no second-HostAP-selector confirmation gate in the reference owner:

`docs/reference/AppleBCMWLAN_APSTA_hostap_success_tail_2026_04_27.md`

## Fixed divergence

The local IWX lower AP start is deliberately asynchronous because its firmware
commands cannot be executed under the upper Apple command gate.  The previous
primary-mode policy nevertheless kept SWAP hidden until CoreWLAN repeated an
identical HostAP selector.  A direct standard `startHostAPMode:` has no such
required repetition.  The BSS could therefore be on air while `airportd`
believed that the primary interface was not in SWAP; its later stop returned
Not Supported and left a stale lower AP owner.

Primary OP_MODE now publishes SWAP as soon as the asynchronous lower start has
actually reached `Running` with state `+0x26c != 0`.  That edge also consumes
both pending public-start admission flags.  An optional repeated start remains
idempotent but is no longer part of the public AP-up contract.

## Candidate identity

The disposable Tahoe 26.2 guest loaded the dirty-diff candidate identified by
source string `755e32a1373f`:

- Mach-O UUID: `0D1105CD-43FA-397A-BB48-73E592ECCC8B`;
- binary SHA-256:
  `cb7779f705a61802b000bb5868ac92f5e88ac67dc211feee21f45238a9c6ad25`;
- Tahoe Debug/OptOut build succeeded;
- all 1074 undefined symbols resolved against the exact guest 25C56 BootKC.

The candidate was live-loaded from the canonical five-member AuxKC after a
clean disposable-guest reset.  This run proves the driver behavior below; it
does not claim that this lab snapshot automatically loaded the third-party
kext at boot.

## Direct CoreWLAN lifecycle

Two consecutive public `startHostAPMode:` calls were run without a Wi-Fi
off/on transition between them.  For each call:

- CoreWLAN returned success;
- IWX completed PHY, beacon, MAC, binding, multicast/broadcast TVQM and quota
  programming;
- the driver logged `APSTA asynchronous public HostAP start reached lower
  running` only after the lower worker returned success;
- the host AX211 saw `AIAM-WIP125-Open` on channel 11 with BSSID
  `86:e4:ba:20:ef:f9`.

Each matching public `stopHostAPMode` reached the driver and produced this
ordered terminal path:

```text
APSTA datapath disabled
IWX AP lower stop queued outside upper command gate
APSTA accepted asynchronous HostAP stop pending lower terminal
IWX AP lower stop worker complete error=0
APSTA lower stop reached terminal
```

The BSS disappeared from a fresh physical scan after standard sharing was
disabled.  No panic or fatal driver event occurred in the test interval.

## Standard Internet Sharing, traffic and sleep policy

The exact Tahoe producer configuration was then used: CoreWLAN
`InternetSharing` preferences selected the same open SSID/channel and numeric
`com.apple.nat.plist` state enabled sharing from guest virtio `en0` to Wi-Fi
`en1`.  The external AX211 client:

- associated in open mode (`key_mgmt=NONE`);
- renewed a real DHCP lease, `192.168.2.5/24`, from guest gateway
  `192.168.2.1`;
- passed 4/4 ICMP client-to-guest and 4/4 guest-to-client;
- received HTTP 200 through the guest NAT path.

While sharing was active, the reference
`InternetSharingPreferencePlugin` held `DenySystemSleep`.  A real
`pmset sleepnow` request consequently entered DarkWake rather than S3.  The
guest remained reachable on all 25 one-second probes, the AX211 association
remained `COMPLETED`, post-request gateway ICMP passed 4/4, and NAT again
returned HTTP 200.  Disabling sharing subsequently reached lower terminal
stop in four seconds and removed the BSS.

## Contract verification

- `scripts/test_tahoe_primary_apsta_op_mode_contract.sh`
- `scripts/test_iwm_iwx_apsta_bounded_recovery_handoff_contract.sh`
- `scripts/test_tahoe_iwx_hostap_lower_epoch_retry_contract.sh`
- `scripts/test_iwx_ap_authoritative_stop_terminal_contract.sh`
