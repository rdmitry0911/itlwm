# IWX standard Internet Sharing HostAP transaction (25C56, 2026-08-06)

## Reference and failure boundary

Tahoe `configd` issues two closely spaced standard Internet Sharing HostAP
starts.  The recovered `airportd` path rejects a start while the primary
interface already reports SWAP.  With the previous candidate, a fast IWX
lower start published SWAP between those two requests.  The second request
therefore failed in userspace with:

```text
Not supported, Wi-Fi interface is in SWAP mode
SWAP interface start failed
```

The exact consumer was recovered from:

`/home/dima/Projects/ghidra_output/airportd_internet_sharing_25C56_20260802T0720Z/airportd_hostap.c`

The exact 25C56 Broadcom producer completes `setHostApModeInternal`
synchronously.  It keeps transition word `+0x270` set while it creates the
firmware interface and sets AP-up word `+0x26c` only in the synchronous
success tail:

`/home/dima/Projects/ghidra_output/cr467_remaining_layer_supplement_20260513T105035Z/90_full_archival_material/full_layer_20260510/03_decomp_bootkc/sel025_setHostApModeInternal.c`

`/home/dima/Projects/ghidra_output/cr467_remaining_layer_supplement_20260513T105035Z/90_full_archival_material/full_layer_20260510/11_warning_evidence/sel025_setHostApModeInternal.focus_windows.md`

IWX cannot reproduce that implementation detail literally: its firmware
completion is delivered on the upper workloop, so synchronously waiting in
the Apple command path starves the completion which must finish the start.

## Event-delimited bridge

Standard Internet Sharing has a real lifecycle edge which direct CoreWLAN
HostAP does not: after the first accepted asynchronous start it disables and
then enables the role-7 APSTA interface before issuing its second standard
start.  The owner now records that successful interface-enable event only
while the first public IWX start is pending.  Primary OP_MODE publication is
held at STA for that event-delimited transaction even if the lower AP reaches
RUNNING.  The second standard request can consequently enter `airportd`, send
its replacement stop/non-NULL carrier sequence to the driver, and only the
final lower RUNNING owner publishes SWAP.

There is no timeout.  A direct CoreWLAN start never records the interface
event, so it still publishes SWAP immediately at the real lower RUNNING edge
and its matching direct stop remains routable.

## Candidate identity

The disposable Tahoe 26.2 guest loaded the dirty-diff candidate identified by
source string `95154e3859c5`:

- Mach-O UUID: `74751D1D-3C38-3B62-9713-ED320EC460BC`;
- binary SHA-256:
  `1caa60c7e47224e0be5a29a6fb9a7bb0120bf8b98f55ee3c76468ef5ec1235fe`;
- Debug/OptOut build succeeded against the guest 25C56 BootKC;
- all 1074 undefined symbols resolved;
- a private exact five-member AuxKC admission passed before the candidate was
  installed into this disposable snapshot.

This is an unsigned laboratory candidate, not a signed release artifact.

## Direct CoreWLAN regression

Two consecutive `startHostAPMode:` / `stopHostAPMode` cycles ran without a
Wi-Fi power toggle.  In both cycles the external AX211 saw
`AIAM-WIP127-DIRECT` on channel 11 with BSSID `86:e4:ba:20:ef:f9`.  Each stop
crossed the asynchronous IWX removal worker and logged `APSTA lower stop
reached terminal`; a fresh scan no longer found the BSS.  The new
interface-driven publication hold did not arm in either direct cycle.

## Standard WPA2 Internet Sharing

The exact Tahoe preference producer configured `AIAM-WIP127-WPA2`, WPA2-PSK,
channel 11, and enabled Internet Sharing from guest virtio `en0` to Wi-Fi
`en1`.  Runtime ordering showed:

```text
APSTA interface-driven HostAP confirmation pending
APSTA asynchronous public HostAP start reached lower running
START HOSTAP USING CONFIG
APSTA accepted asynchronous HostAP stop pending lower terminal
APSTA queued confirmed HostAP replacement behind lower stop
APSTA confirmed HostAP replacement crossed lower stop terminal
APSTA asynchronous public HostAP start reached lower running
```

`airportd` accepted both standard requests, `configd` logged `AP started` for
both, and the interval contained neither `SWAP interface start failed` nor
`Not supported, Wi-Fi interface is in SWAP mode` for HostAP start.

The external AX211 then completed WPA2-PSK/CCMP.  A fresh DHCP DISCOVER and
REQUEST received `192.168.2.5/24` from guest gateway `192.168.2.1` for 3600
seconds.  Client-to-guest and guest-to-client ICMP each passed 4/4, and an
HTTP request bound to the wireless client returned 200 through guest NAT.
Disabling standard sharing reached the authoritative lower stop terminal and
removed the BSS.

## Standard WPA3, PMF, and sleep policy

The same producer then configured `AIAM-WIP127-WPA3`, WPA3-SAE, channel 11.
The physical RSN advertisement contained SAE, MFPR and MFPC.  The AX211
reported `key_mgmt=SAE`, `pmf=2`, CCMP and `wpa_state=COMPLETED`; the driver
logged `IWX AP WPA3 4-way complete authorized=1`.  Fresh DHCP, bidirectional
ICMP 4/4 in each direction, and NAT HTTP 200 all passed.

While sharing was active, `InternetSharingPreferencePlugin` held the normal
`DenySystemSleep` assertion.  `pmset sleepnow` entered DarkWake.  Independent
virtio SSH and AP gateway traffic both remained reachable for 25/25
one-second probes; SAE/PMF remained completed, post-request ICMP passed 4/4,
and NAT again returned HTTP 200.

After standard sharing was disabled, the already requested sleep later
entered full S3.  QEMU `system_wakeup` restored the macOS/IWX power path and
the driver republished its lower-ready APSTA capability, but this lab's
emulated AHCI/virtio path did not restore SSH.  An exact reset of only this
disposable QEMU was required.  There was no driver panic.  This is the same
known VM full-S3 transport boundary and is not counted as an on-air AP pass.

## Wi-Fi off/on observation and residual

After the clean guest reset, Wi-Fi off and on completed while virtio SSH
remained available.  Standard WPA3 Internet Sharing again produced a physical
BSS; the AX211 completed SAE/PMF, DHCP returned `192.168.2.5`, gateway ICMP
passed 4/4, and NAT returned HTTP 200.

The first lower AP attempt after that rapid power cycle emitted one IWX
firmware fatal.  Existing firmware recovery reinitialized the device and the
subsequent standard AP start completed successfully, but a no-fatal off/on
claim is deliberately not made.  Eliminating that recovered power-cycle fatal
is the next independent high-use runtime layer.

## Contract verification

All 50 AP/HostAP contract scripts passed, including:

- `scripts/test_tahoe_primary_apsta_op_mode_contract.sh`;
- `scripts/test_tahoe_apsta_interface_enable_lifecycle_contract.sh`;
- `scripts/test_iwm_iwx_apsta_bounded_recovery_handoff_contract.sh`;
- `scripts/test_tahoe_standard_internet_sharing_ap_contract.sh`;
- paired IWM/IWX open, WPA2, WPA3/SAE/PMF, stop, reset and sleep contracts.
