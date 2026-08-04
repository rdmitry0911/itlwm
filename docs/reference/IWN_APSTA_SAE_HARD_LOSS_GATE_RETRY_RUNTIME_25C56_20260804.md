# IWN APSTA SAE hard-loss and gate-retry runtime evidence (25C56, 2026-08-04)

## Scope

This layer closes two user-visible failures which shared the same multi-BSS
reconnect path:

- loss of the current WPA3/SAE BSS did not transfer the retained active-ESS
  credential into a fresh driver-owned scan and join;
- with a concurrent HostAP role, transient ownership of the private workloop
  gate could exhaust the old two 1 ms SAE submission retries before either a
  Commit or Confirm reached its descriptor doorbell.

The implementation remains driver-resident.  Password consumption, SAE
Commit/Confirm, PMK continuation and native ASSOC TX are all inside the kext;
the upper WCL path supplies only the ordinary credential and selected-BSS
request.

## Implementation boundary

The generic net80211 beacon-loss path now snapshots one exact active-ESS
credential generation and invokes the HAL hard-loss hook.  IWN admits that
generation into a foreground scan, keeps it fenced through the physical scan
terminal, and hands the selected candidate directly to the existing
driver-owned SAE engine.  The APSTA scheduler clears its logical primary-BSS
association carrier before scan submission and gives the unassociated scan or
authentication owner the 280/20 PAN split.

SAE frame submission still uses `IOCommandGate::attemptAction()`.  Replacing
it with a blocking gate call would create a stop/detach deadlock.  Instead,
IWN, IWM and IWX now give each individual Commit or Confirm at most six
deferred attempts with delays of 2, 4, 8, 16, 32 and 32 ms.  A successful
doorbell resets the counter, so Confirm receives an independent bounded
budget.  The complete contention window is 94 ms per frame and remains well
inside the management authentication timer.

## Exact builds

The diagnostic build which isolated the contention branch was:

- source ID `79f60bbb-apsta-sae-diag-wip66`;
- UUID `B311D1FF-0090-31F1-9B85-F7CB82147C95`;
- Mach-O SHA-256
  `bc19f642a10e8ca0abf130d1c660d7ca34110f3ad3c3ac2c36b0ea599fa18453`.

It recorded a generation-2 initial submit result of retry, followed by a
successful deferred submit, on-air Commit/Confirm, ASSOC and
`ACTIVE_ESS_CREDENTIAL`.  The same older retry policy also had an independent
run where the tiny retry window was exhausted and the management timer
expired.  This distinguishes private-gate contention from SAE cryptography,
candidate selection and PAN scheduling.

The bounded-retry diagnostic build was:

- source ID `79f60bbb-apsta-sae-gate-retry-wip67`;
- UUID `A953340C-843E-325C-BFFC-2AFB76CBBBB8`;
- Mach-O SHA-256
  `611ff3a4dc58e7f92d1615714198459264eed82d4cf1d01a3a46c6ed4c6b2f80`.

The final log-clean runtime build was:

- source ID `79f60bbb-apsta-sae-reconnect-gate-wip68`;
- UUID `4A2F1DA7-6B1E-3C17-A5E2-E17EED7D1E74`;
- Mach-O SHA-256
  `2e816048e84f22d7a110528ffbf06572c012ef821d18fcc55296f57a009c1d86`.

The installed AuxKC contained exactly five expected members, and the loaded
AirportItlwm UUID matched the clean bundle.  Temporary per-frame, per-channel
and SAE-worker diagnostics are absent from this binary.

## On-air results

The controlled pure-SAE/required-PMF source was BSSID
`80:e4:ba:20:ef:f9` on channel 153.  Other physical `LabAP` BSSes with the
same credential were visible on channel 13.

With the clean build, terminating only the controlled hostapd removed the
current BSS from the air.  Serial evidence then recorded:

```
iwn_sae_reconnect BSS_LOSS_ARMED
wcl_assoc CACHED_CANDIDATE_DIRECT_JOIN
wcl_auth SUCCESS_LEDGER captured=1 result=0x00000000
wcl_assoc VALIDATED_COMPLETION captured=1 result=0x00000000 armed=1 published=1
iwn_sae_roam ACTIVE_ESS_CREDENTIAL
```

`wdutil` reported WPA3 Personal on channel 13 with RSSI -44 dBm and DHCP
address `172.16.66.187`; ICMP to `172.16.66.1` passed 5/5.  No controller SAE
relay or userspace crypto process participated.

The concurrent open HostAP role remained usable during the next roam cycle.
The Linux client completed Open-System association at
`ce:f7:33:f4:97:4b`, retained `192.168.3.2`, and reached guest `ap1` at
`192.168.3.1`.  The first direction delivered 7/10 while association warmed
up, the reverse direction delivered 10/10, and a subsequent host-to-guest
check delivered 5/5.

## Real S3

The bounded-retry diagnostic build entered real ACPI S3 from a live STA+AP
state.  Serial recorded `AP stop requested`, `PMRD: System Sleep`,
`ACPI SLEEP` and `acpi_sleep_kernel`.  This AP stop matches Tahoe's explicit
upper-layer policy and the reference HostAP power contract.

After the exact owned QEMU received `system_wakeup`, the IWN path performed a
fresh cached-candidate SAE join and recorded successful Commit/Confirm,
`VALIDATED_COMPLETION` and `ACTIVE_ESS_CREDENTIAL`, with no panic.  The QEMU
virtio management NIC did not resume its SSH banner until a later guest reset;
that management-plane defect did not affect the passthrough IWN radio trace.

## Static contracts

The following contracts pass on the final source:

- `test_tahoe_sae_gate_contention_retry_contract.sh`;
- `test_tahoe_iwn_apsta_scan_scheduler_contract.sh`;
- `test_tahoe_iwn_beacon_loss_reconnect_contract.sh`;
- `test_tahoe_iwn_sae_auth_transport_contract.sh`;
- the IWM and IWX driver-resident SAE owner contracts.

The cross-family retry contract also models the exact delay series and its
94 ms upper bound.

## Remaining surface

This result does not claim complete repeated APSTA recovery.  A later clean
cycle selected the restored channel-153 target and reached
`TARGET_PORT_VALID`, but a subsequent beacon-loss scan coincided with a DVM
command-queue `qid=0` timeout.  The unexpected-lower-reset path replayed the
HostAP profile and its client data path, while the primary STA remained
inactive.  Recovery of the primary STA after that command-queue reset is the
next functional layer.

Starting HostAP before the initial STA association was established also left
the STA inactive in one deliberate ordering test.  Physical IWM/IWX runtime
validation remains separate from the source-symmetric retry implementation.
