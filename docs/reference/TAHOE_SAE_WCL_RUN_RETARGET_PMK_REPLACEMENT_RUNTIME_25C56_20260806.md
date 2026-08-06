# Tahoe SAE WCL RUN-retarget PMK replacement (25C56, 2026-08-06)

## User-visible failure

An AX211 could cold-join the saved pure-SAE/MFP `LabAP`, but a real system WCL
roam to another BSS of the same ESS destroyed the working source link without
bringing up the target.  The target SAE Commit and Confirm both completed on
air.  The failure was later: the target continuation could not claim its newly
derived PMK because the completed source association still owned
`ic_sae_wcl_pmk_claim`.

The diagnostic terminal was exact:

```text
REAL_SCAN_STARTED
TARGET_SELECTED
RUN_RETARGET_PREPARED
LOWER_RETARGET_ACCEPTED
DRIVER_RESIDENT_WCL_STARTED
SAE Commit accepted
SAE Confirm accepted
PMK continuation: claimed=0 assoc_started=0
```

The earlier implementation also queued `RUN -> SCAN` before asking the private
IWN/IWM/IWX credential owner to accept the target.  IWX state work is
asynchronous, so a synchronous lower rejection could not restore the source
association.  This exposed a second mismatch at the same transaction boundary.

## Exact 25C56 reference boundary

The implementation was checked against the exact reference decompiles on
`10.7.6.112`:

- `~/Projects/ghidra_output/aiam_applebcm_reassoc_25C56_20260801/reassoc.txt`,
  SHA-256
  `e6575b5b96210c17e073e656a0bc9a7775ca1d2286f22e3e013ab86b86cfe669`;
- `~/Projects/ghidra_output/cr333_wcl_reassoc_owner_research_20260508_0003_boundary_decomp.txt`,
  SHA-256
  `696c580076b5059d5ae48b1aa55949c3df21f6e881aa484d8af86b3ac140acd8`.

`AppleBCMWLANCore::setWCL_REASSOC` at `0xffffff80016369ac` first calls
`AppleBCMWLANRoamAdapter::setReassocParams`, then submits
`AppleBCMWLANNetAdapter::sendReassocCommand`.  Only a synchronous submission
failure calls `restoreReassocParams`; an accepted command transfers completion
to the asynchronous reassociation owner.  `AppleBCMWLANCore::handleReassocEvent`
at `0xffffff80015bfeec` later publishes the reassociation result through event
selector `0x49`.

Therefore the source association must remain intact through lower submission,
but its completed security material stops being a rollback owner once the
accepted target replacement begins.

## Local correction

The shared net80211 owner now has an explicit
`RUN_RETARGET_ISSUED` transaction:

1. it accepts only an exact port-valid direct-SAE source generation and an
   already owned WCL or confirmed-BTM target;
2. it changes the public target identity without queuing `RUN -> SCAN`;
3. IWN, IWM, and IWX stage the new private credential generation while the
   source remains usable;
4. a lower staging failure restores the original bound generation and BSSID;
5. a successful stage enters `node_join_bss()`, whose selected-BSS replacement
   retires the source PMK/PMKID, controller PSK and external-PMK owner before
   advancing the association epoch;
6. the new target binds exactly once and may then claim its independently
   derived PMK.

Fresh request generations may also replace a fully drained cancelled SAE
engine tombstone in all three Intel owners.  This is limited to a strictly
newer request and epoch with no engine, in-flight frame, queued peer,
completion, association TX or pending producer.

The target link is not published while its driver-resident SAE port is closed.
The normal open/Apple-supplicant path keeps its historical behavior; only an
exact bound direct-SAE request waits for the four-way terminal to set
port-valid and release link-up.

## Clean candidate and runtime proof

All detailed diagnosis logs were removed before the clean build.  The Tahoe
Debug candidate was admitted against the exact guest BootKC with all 1074
undefined symbols resolved and passed the private exact-five-member AuxKC
preflight.  The loaded identity was:

- Mach-O UUID `CDB329A9-8DE6-3ECD-B5AC-AF9D86C36A75`;
- binary SHA-256
  `5a135af0a48c32230979650ac161532841aa0c4f2dd7a648a4d2014aedcb3931`.

On the physical passthrough AX211:

- cold saved-profile SAE joined channel 153/80, obtained
  `172.16.66.213/24`, and passed 100/100 local packets;
- a real system `ROAM_PROFILE` request produced
  `REAL_SCAN_STARTED -> TARGET_SELECTED -> RUN_RETARGET_PREPARED ->
  LOWER_RETARGET_ACCEPTED -> DRIVER_RESIDENT_WCL_STARTED ->
  TARGET_PORT_VALID` and moved from channel 153 to channel 13;
- DHCP remained `172.16.66.213`; post-roam host-to-guest and source-bound
  guest-to-router ICMP each passed 100/100;
- a user Wi-Fi off/on cycle cold-rejoined pure SAE on channel 153 with the same
  address and again passed 100/100 in both directions;
- real S3 recorded `PMRD: System Sleep`, `ACPI S3 WAKE`, and
  `PMRD: System Wake`.  Saved SAE, DHCP and the same loaded UUID returned;
  direct SSH over Wi-Fi worked and both post-wake directions passed 100/100.

No panic, kernel-extension backtrace or firmware fatal occurred in the clean
roam, off/on or sleep/wake cycle.  `networksetup -getairportnetwork` can still
report no association, and a CoreWLAN helper launched without a console login
can redact SSID/BSSID even while `wdutil`, DHCP and physical traffic prove the
WPA3 link.  That presentation/TCC mismatch is separate from this completed
reassociation data-path layer.

## Contract verification

- `scripts/test_net80211_pae_epoch_contract.sh`
- `scripts/test_tahoe_wcl_reassoc_roam_scan_contract.sh`
- `scripts/test_tahoe_wcl_physical_scan_lifecycle_contract.sh`
- `scripts/test_tahoe_iwx_driver_resident_sae_owner_contract.sh`
- `scripts/test_tahoe_iwm_driver_resident_sae_owner_contract.sh`
- `scripts/test_tahoe_iwn_wnm_bss_transition_contract.sh`
