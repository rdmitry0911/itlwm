# IWN APSTA shared-channel and MFP-gate runtime evidence (25C56, 2026-08-04)

## User-visible layer

This layer closes three failures which appeared together when a Tahoe IWN
station and HostAP were active at the same time:

- retained HostAP replay could race the primary station's exact reset-recovery
  scan;
- a public AP start, WCL reassociation, CSA, or wake replay could leave DVM's
  BSS and PAN contexts on different channels even though the published Linux
  DVM interface combination permits only one different channel;
- an asynchronous PMF/PAE completion could miss the only `if_start` edge for
  EAPOL M4 by completing on a workloop other than the main driver command
  gate.

The common owner now defers retained PAN replay only for the exact primary-STA
recovery scan generation.  IWN constrains WCL admission, public AP start/CSA,
and wake replay to the live primary channel.  The physical scan remains a full
regulatory census; only the target admitted while APSTA is live is narrowed.
IWN/IWM/IWX deliver MFP/PAE completion through their main `IOCommandGate`, so
the sole output kick is serialized with the driver's ordinary TX owner.

IWN also now preserves DVM's association order (station/link quality, timing,
then associated RXON) and keeps primary data TX fenced until firmware accepts
the final two-associated-context PAN schedule.

## Reference boundary

The recovered Apple FullMAC behavior is recorded in:

- `docs/reference/AppleBCMWLAN_APSTA_channel_csa_sta_control_2026_04_27.md`;
- `docs/reference/IWN_AP_WPA3_SLEEP_RECOVERY_RUNTIME_25C56_20260801.md`;
- `docs/reference/TAHOE_WCL_POWER_TERMINAL_RUNTIME_25C56_20260804.md`.

Apple's FullMAC `setCHANNEL` translates and submits the requested chanspec.
That does not imply that Intel DVM can operate two physical channels.  The
published DVM STA+AP combination has `num_different_channels == 1`; its BSS
and PAN contexts are concurrent firmware roles sharing one radio channel.
The adaptation therefore belongs in the IWN backend and in the common APSTA
admission boundary, not in SAE itself.

The reference `hostAPPowerOff()` retains AP power-save state when a station is
associated, while an explicit HostAP NULL remains an authoritative stop.  The
local wake path retains exactly that distinction: it replays an active
associated profile after the destructive IWN radio epoch, but never overrides
an explicit public stop.

## Build and activation

The first clean proof artifact (W92) had:

- UUID `FA10A970-2057-3739-A1DF-607EF57D8DB6`;
- SHA-256
  `186cd04fffeb12787c8ec695d9d9ecb3db5709204ec01dbb269c9712a54f7fa2`;
- source id `01bfc634_apsta_shared_start_mfp_gate_w92`.

W92 proved the MFP completion gate and public-start channel alignment.  A
final defensive IWN guard was then added for the short interval in which
net80211 reports `SCAN` while firmware still carries an associated BSS RXON.
It accepts the RXON channel only when both `IWN_FILTER_BSS` and a nonzero AID
remain present, so an unassociated discovery RXON cannot pin AP admission to
a scan channel.

The resulting clean W93 artifact used for the final gate had:

- UUID `0ACAF486-3554-332C-A9C6-C715002A9DD9`;
- SHA-256
  `017e71fc5f5fa1e4f1c43d8d812f30a4f96d75ac819eec8dda0a5659094a6a12`;
- source id `01bfc634_apsta_shared_scan_guard_w93`;
- all 1075 undefined symbols resolved against the 25C56 BootKC;
- no `_thread_call_cancel_wait` dependency.

The candidate was installed transactionally in the disposable Tahoe guest.
The activation helper validated the exact five-member AuxKC, preserved
timestamped rollback copies, and reported `ACTIVATION_READY` before the guest
was rebooted.  The cold boot loaded the exact W93 UUID and SHA above.

## Cold-boot STA and public AP start

The passed-through Intel 6235 first reached a real WPA3 Personal station link
on channel 13 and received `172.16.66.214/24` by DHCP.  Source-bound traffic to
`172.16.66.1` passed 5/5 before AP start.

The public Apple80211 APSTA lifecycle then requested pure-SAE AP
`AIAM-APSTA-ALIGN-W92` on channel 9.  The request intentionally disagreed with
the live primary channel.  The driver recorded:

```text
APSTA public start shared channel follows primary profile=9 primary=13
AP PAN deactivation queued channel=13 ... auth_upper=0x1000 ... rsn_len=28
AP PAN timing source=retained-BSS
AP PAN context running after DVM RXON/beacon/EDCA transition
```

The host AX211 observed the resulting BSSID `ce:f7:33:f4:97:4b` on channel 13,
not the stale requested channel.  Its RSN information advertised SAE/CCMP,
MFP required and capable, and AES-128-CMAC as the group-management cipher.
The station completed SAE group 19, `pmf=2`, BIP, and the four-way handshake.
The driver recorded `AP WPA3 4-way complete ... authorized=1`.

With guest `ap1=192.168.3.1/24` and host `sta0=192.168.3.2/24`:

- host-to-guest ICMP passed 20/20;
- guest-to-host ICMP passed 12/12 after ordinary first-ARP convergence;
- SSH over the AP data path succeeded and reported the loaded W93 UUID;
- the simultaneous primary STA path passed 12/12 to its WPA3 gateway.

Calling CoreWLAN's convenience `startHostAPMode...` on the primary `en1`
interface was also observed, but it deliberately changed the primary role to
HOSTAP before the driver's APSTA lower call.  That is not a concurrency test
and is excluded from the positive result.  The accepted fixture reproduces
the real role-7 public sequence: VIRTUAL_IF_CREATE, AP-interface up, POWER,
CHANNEL, and HOST_AP_MODE on `ap1`.

## Real S3 and multi-AP wake replay

With both WPA3 links active and the external AP station authorized,
`pmset sleepnow` reached real ACPI S3.  The exact owned QEMU monitor alone
received `system_wakeup`.  The serial terminal recorded:

```text
PMRD: System Sleep
ACPI SLEEP
acpi_sleep_kernel hib=0, cpu=0
ACPI S3 WAKE
AppleACPIPlatformPower Wake reason: power-button (User)
APSTA radio-reset primary STA boundary state=4 wait_ticks=8
```

The multi-AP `LabAP` ESS then selected a different physical BSS on channel 9.
The retained guest AP profile was still channel 13, so the replay adapted to
the newly recovered primary instead of restoring an impossible split-channel
pair:

```text
APSTA radio-reset shared channel follows primary profile=13 primary=9
AP PAN deactivation queued channel=9 ... auth_upper=0x1000 ... rsn_len=28
AP PAN timing source=retained-BSS
AP PAN context running after DVM RXON/beacon/EDCA transition
AP WPA3 4-way complete PTK=0 authorized=1
```

After the host fixture admitted 2452 MHz, AX211 completed SAE group 19,
PMF2/BIP, and the four-way handshake again.  A fresh scan saw the same guest
BSSID on channel 9 with SAE, required/capable MFP, and AES-128-CMAC.  Post-wake
results were:

- host-to-guest ICMP 20/20;
- guest-to-host ICMP 12/12;
- primary STA-to-gateway ICMP 12/12;
- SSH through the guest AP succeeded;
- loaded kext UUID remained `0ACAF486-3554-332C-A9C6-C715002A9DD9`.

No driver panic, Debugger entry, firmware fatal, device timeout, or watchdog
appeared in the tested W93 S3 interval.  QEMU's separate virtio management
path is not part of this wireless result.

### Product-default role-7 APSTA replay (2026-09-09)

The published default artifact from `6eb51401` (UUID
`F9E599B0-3FA4-3A9E-886C-BC3A31294DE3`, Mach-O SHA-256
`91afd774a0fb51aeba21757423661e3147466cdce674ebc90dd7bbb2c9dcb11b`) was
replayed on the physical IWN/6235 guest after the default-build AP admission
fix.

The primary interface first held a real WPA3/required-PMF LabAP station on
channel 13.  A role-7 sequence then created `ap1`, brought it administratively
up, and applied POWER, CHANNEL and HOST_AP_MODE for a pure-SAE profile whose
requested channel was 9.  Each Apple80211 request succeeded.  The host AX211
observed and joined the resulting BSS on channel 13, confirming that the
current driver follows the live STA channel instead of admitting an impossible
split-channel DVM schedule.

With an isolated static AP test subnet, the AX211 and guest completed 20/20
client-to-AP ICMP plus SSH, 5/5 guest-to-client ICMP, and the primary station
simultaneously completed 5/5 to its external gateway.  System Profile still
reported the primary WPA3 station as Connected while `ap1` was active.

The guest then entered an ACPI sleep transition (`PMRD: System Sleep` and
`ACPI SLEEP` on the owned serial console).  This QEMU configuration kept the
VM monitor in `running`, so the result is recorded as a guest ACPI sleep/wake
cycle rather than a separate QEMU-suspended proof.  After the owned power-key
wake event, both `ap1` and the primary station were active again.  AX211
rejoined the restored role-7 AP and passed 10/10 client-to-AP, 5/5 AP-to-client
and 5/5 simultaneous STA-to-external-gateway traffic.

For clarity, Tahoe's ordinary Internet Sharing producer was separately
replayed with the same current artifact.  It created a working WPA3 AP, gave
the AX211 a DHCP lease, and passed AP traffic, but its primary
`startHostAPMode` route made the primary station Not Associated.  That
producer is therefore not evidence of STA+AP concurrency; the completed
concurrency claim is limited to the distinct role-7 lifecycle above.

## Regression gates and remaining scope

The focused source contracts pass:

- `scripts/test_iwn_iwm_iwx_apsta_reset_recovery_scan_order_contract.sh`;
- `scripts/test_iwn_iwm_iwx_mfp_pae_completion_gate_contract.sh`;
- `scripts/test_tahoe_iwn_apsta_shared_channel_public_start_contract.sh`;
- `scripts/test_tahoe_iwn_apsta_shared_channel_wake_replay_contract.sh`;
- `scripts/test_tahoe_iwn_apsta_shared_channel_wcl_contract.sh`;
- `scripts/test_tahoe_iwn_apsta_scan_scheduler_contract.sh`.

Physical on-air proof in this layer is IWN/6235.  IWM and IWX have clean-build
and source-contract coverage for the common replay/MFP gate but no matching
hardware runtime claim.  DVM remains intentionally single-channel for STA+AP;
this layer makes that limitation correct and recoverable rather than
pretending that the firmware has multi-channel concurrency.
