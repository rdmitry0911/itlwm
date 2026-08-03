# IWN HostAP unexpected lower-reset recovery (25C56, 2026-08-03)

## Scope and reference boundary

Tahoe's recovered
`AppleBCMWLANIO80211APSTAInterface::hostAPPowerOff()` has two deliberate
system-power branches.  With no associated station it stops HostAP; with a
live station it retains the AP owner in power-save state 3 for restoration on
power-on.  The authoritative 25C56 decompile remains:

`/home/dima/Projects/ghidra_output/aiam_apsta_reference_lifecycle_kdk_26_3_20260801/ffffff8001692772_APSTA_hostAPPowerOff.c`

An Intel firmware fatal or TX watchdog is a different boundary.  It destroys
the lower PAN/GO context without a public `HOST_AP_MODE(NULL)` request, so the
configured Apple role-7 owner must remain durable.  Before this change,
IWN/IWM/IWX stopped their device queues while leaving the lower AP software
census in `Running`.  The common APSTA watchdog therefore saw a nonzero stale
channel and could not distinguish the vanished BSS from a healthy AP.

## Change

- IWN retires its AP runtime whenever `iwn_stop()` destroys the firmware
  epoch.  IWM and IWX likewise reset their GO runtime after their respective
  device-stop boundary.
- The common APSTA watchdog compares a still-running upper owner with the
  lower committed channel.  Upper `Running` plus lower channel zero closes the
  stale datapath and station census, retains the HostAP profile, and enters the
  existing bounded radio-reset replay path.
- While healthy, the same census continuously snapshots a completed lower
  CSA channel, so an unexpected reset replays the actual on-air channel.
- `prepareForRadioReset()` keeps the recovered reference behavior: a normal
  system-power callback with no station still stops HostAP.  Unexpected
  firmware loss retains the profile even with no client because it is not a
  user or system HostAP stop.

The cross-family contract is locked by
`scripts/test_iwn_iwm_iwx_ap_unexpected_reset_replay_contract.sh`.  Physical
on-air reset evidence in this cycle is IWN/6235; IWM/IWX are compile- and
source-contract coverage only.

## Candidate identity and build

The disposable Tahoe 26.2 guest loaded the opt-out AP/STA candidate:

- Mach-O UUID: `B33E1520-3748-3169-A0DE-4C1725CAC87D`;
- binary SHA-256:
  `da5f55f3b79996e5aafff68da6f681468c6471d4b9e7e4dc1b0099f49a2ec29d`;
- Tahoe Debug/OptOut build succeeded;
- all 1075 undefined symbols resolved against the guest 25C56 BootKC;
- the private and canonical AuxKC inspections each contained exactly five
  expected members before the guest-only reset.

The physical fixture was the guest Intel 6235 and host AX211.  Standard
Internet Sharing produced `AIAM-Native-WPA3` on channel 11 with BSSID
`ce:f7:33:f4:97:4b`.  AX211 completed SAE group 19 with CCMP, PMF required,
and BIP; DHCP assigned `192.168.2.4` from guest `192.168.2.1`.

## Real unexpected-reset result

A bounded reverse `iperf3` run exercised guest-to-station AP TX.  The real IWN
watchdog observed aggregate queue 12 at `queued=255`, stopped the lower radio,
and scheduled a fresh firmware epoch.  The decisive serial order was:

```text
device timeout AP aggregate qid=12 queued=255 ...
device timeout pending qid=12 queued=255 ...
APSTA unexpected lower reset detected; retaining HostAP profile
AP PAN context running after DVM RXON/beacon/EDCA transition
IWN AP TX BA started tid=0 ...
```

No Internet Sharing toggle, HostAP setter, guest reboot, or QEMU reset was
issued between the watchdog terminal and the recovered BSS.  The external
station automatically completed a fresh WPA3 authorization and reported
`key_mgmt=SAE`, `pmf=2`, `mgmt_group_cipher=BIP`, and
`wpa_state=COMPLETED`.  Its existing DHCP address remained valid.

After recovery:

- host-to-guest ICMP passed 20/20;
- guest-to-host ICMP passed 20/20;
- source-policy-routed HTTP through guest Internet Sharing returned 200;
- the temporary host policy rule and table were removed after the check.

## Sleep recovery

Standard Internet Sharing intentionally held the reference
`DenySystemSleep` assertion.  `pmset sleepnow` therefore entered DarkWake,
not S3, and the SAE/PMF association plus gateway traffic remained live.  This
matches Tahoe's standard producer policy and is not reported as an S3 test.

For the driver-level power boundary, Internet Sharing was disabled and the
same WPA3 profile was started through the public Apple80211 HostAP selectors.
The external station used static `192.168.3.2`; guest `ap1` used
`192.168.3.1`.  With the client associated, a second `pmset sleepnow` reached
real ACPI S3.  The serial sequence was:

```text
APSTA datapath disabled link=1 RX=0 TX=0 TXC=0
PMRD: System Sleep
ACPI SLEEP
ACPI S3 WAKE
PMRD: System Wake
AP WPA3 SAE authenticator prepared SSID length=16
APSTA datapath enabled link=1 RX=1 TX=1 TXC=1
AP PAN context running after DVM RXON/beacon/EDCA transition
AP WPA3 4-way complete PTK=0 authorized=1
```

AX211 returned to `COMPLETED` seven seconds after wake with SAE/PMF/BIP and
the same BSSID/channel.  Post-wake ICMP passed 20/20 in both directions.

## Remaining high-use gap

The recovery is now transparent, but the stimulus also exposes the next
user-visible layer: IWN AP TX aggregation can fill an entire ring and reverse
throughput can collapse below 1 Mbit/s before watchdog recovery.  Fixing the
aggregate completion/window ownership without regressing WPA2/WPA3, PMF,
reconnect, or sleep is the next priority cycle.
