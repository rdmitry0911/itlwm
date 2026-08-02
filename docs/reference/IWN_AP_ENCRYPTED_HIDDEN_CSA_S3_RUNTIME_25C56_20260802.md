# IWN encrypted hidden AP, CSA, and S3 runtime evidence (25C56, 2026-08-02)

## Scope

This observation combines four user-visible HostAP contracts that had already
been tested separately: protected association, a hidden SSID, a live channel
switch, and recovery from real S3.  The purpose is to catch state that passes
isolated tests but is lost when the AP profile, pairwise/group keys, committed
channel, and hidden-beacon policy must be restored together.

The disposable Tahoe guest ran macOS 26.2 build `25C56` with IWN/Intel 6235.
The external station was the host Intel AX211.  The loaded AirportItlwm had:

- source commit `78a81947017f6b2222b9b630a743f97081eb1b87`;
- Mach-O UUID `85DFC435-7460-3F49-ACAB-8C2140A58FBE`;
- binary SHA-256
  `0f82790cc3261862074da53717c3bbad7f82f0a9efd3fb0eda90ec98352ddd96`.

The AP used BSSID `ce:f7:33:f4:97:4b`, started on channel 11, and performed a
live CSA to channel 6.  Guest `ap1` used `192.168.3.1/24`; host `sta0` used
`192.168.3.2/24`.  The public Apple80211 HostAP/hidden/CSA selectors were used;
there was no synthetic userspace authenticator in the guest.

## Reference contract

The implementation basis remains the exact Tahoe artifacts recovered on
`10.7.6.112`:

- `AppleBCMWLANIO80211APSTAInterface::setSOFTAP_TRIGGER_CSA`:
  `/home/dima/Projects/ghidra_output/cr464_layer_a/04_setSOFTAP_TRIGGER_CSA.c`;
- `AppleBCMWLANIO80211APSTAInterface::hostAPPowerOff`:
  `/home/dima/Projects/ghidra_output/aiam_apsta_reference_lifecycle_kdk_26_3_20260801/ffffff8001692772_APSTA_hostAPPowerOff.c`.

The reference `getCHANNEL` reads the lower FullMAC committed channel rather
than the original profile value.  The local IWN DVM path therefore snapshots
its lower committed channel before the destructive radio reset and uses that
channel for replay.  Explicit HostAP stop remains authoritative and does not
retain the profile.

## WPA2 result

The hidden `AIAMWPA2` BSS was absent from a wildcard scan.  A directed scan
found it on channel 11 and exposed RSN/CCMP/PSK.  AX211 associated with
`key_mgmt=WPA2-PSK`; bidirectional ICMP passed 8/8 before the sleep cycle.

Selector 349 completed the live switch to channel 6 without changing the
BSSID.  The station remained `COMPLETED` at `2437 MHz`.  A real
`pmset sleepnow` reached `PMRD: System Sleep` and ACPI S3.  After the exact
QEMU `system_wakeup`, the AP replayed on channel 6, the station completed a
fresh WPA2 four-way handshake, and bidirectional ICMP passed 8/8 again.

The relevant serial sequence was:

```text
AP WPA2 4-way complete PTK=0 authorized=1
APSTA radio-reset channel snapshot profile=11 committed=6
PMRD: System Sleep
ACPI S3 WAKE
AP PAN deactivation queued channel=6 ... auth_upper=0x8
APSTA replaying retained hidden AP profile after radio reset
AP post-deactivation timing queued channel=6
AP PAN context running after DVM RXON/beacon/EDCA transition
AP association request from 80:e4:ba:20:ef:fb ... rsn=1
AP WPA2 4-way complete PTK=0 authorized=1
```

## WPA3 result

The hidden `AIAMWPA3` BSS was likewise absent from a wildcard scan and present
in a directed scan on channel 11.  Its RSN element advertised SAE, CCMP,
MFP-required/MFP-capable, and AES-128-CMAC.  AX211 completed SAE group 19 with
`key_mgmt=SAE`, `pmf=2`, `mgmt_group_cipher=BIP`, and bidirectional ICMP 8/8.

After live CSA, the same BSSID remained `COMPLETED` on `2437 MHz`, with SAE
and PMF state intact, and traffic again passed 8/8 in both directions.  A real
S3/wake cycle then replayed the hidden WPA3 profile on committed channel 6.
The station reauthorized and post-wake traffic passed 8/8 in both directions.

The relevant serial sequence was:

```text
AP SAE Commit accepted peer=80:e4:ba:20:ef:fb group=19
AP SAE Confirm response=0 authenticated=1
AP WPA3 4-way complete PTK=0 authorized=1
APSTA radio-reset channel snapshot profile=11 committed=6
PMRD: System Sleep
ACPI S3 WAKE
AP PAN deactivation queued channel=6 ... auth_upper=0x1000
APSTA replaying retained hidden AP profile after radio reset
AP post-deactivation timing queued channel=6
AP PAN context running after DVM RXON/beacon/EDCA transition
AP association request from 80:e4:ba:20:ef:fb ... rsn=1
AP WPA3 4-way complete PTK=0 authorized=1
```

## Result and remaining boundary

No new defect was found in this composite layer.  IWN now has direct on-air
evidence that encrypted hidden HostAP state, the committed CSA channel,
WPA2/WPA3 authorization, PMF, and bidirectional data recover together across
real S3.  This result does not claim simultaneous STA+AP operation, multiple
simultaneous external clients, or physical IWM/IWX runtime coverage; those
remain separate boundaries.
