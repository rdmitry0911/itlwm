# IWN WCL scan-to-SAE handoff runtime (25C56, 2026-08-03)

## Reference behavior

Tahoe's `AppleBCMWLANCore::setWCL_REASSOC` delegates the request to one
asynchronous lower owner.  The legacy path in the 25C56 decompile submits
`WLC_REASSOC` (`0x440006`) with both scan channels and candidate preferences;
it does not split scan completion and association into competing operations.
The recovered carrier and command lifecycle are documented in
`AppleBCMWLAN_WCL_REASSOC_ROAM_SCAN_25C56_20260801.md`.

## Regression and cause

Commit `f773e21e` correctly prevented an unrelated live scan from overlapping
a driver-resident SAE join, but also rejected the intended WCL handoff.  IWN's
`IWN_STOP_SCAN` keeps the scan lease in `DRAINING` while
`ieee80211_end_scan()` selects the target.  The pure-SAE target reaches
`iwn_sae_auth_hold()` inside that callback, before the terminal scan publisher
can make the lease idle.  Treating every live lease as unrelated therefore
left AUTH held until timeout.

## Narrow handoff

`iwn_sae_join_scan_block_promote()` now accepts only the completed background
lease that owns this WCL roam:

- owner is generic-background or WCL-background;
- phase is `DRAINING`, with command submitted and terminal already claimed;
- the command was not aborted and neither hardware nor publication was
  invalidated;
- hardware scanning is clear, no AP transition or initial handoff is pending;
- the WCL reassociation owner is active at `ROAM_STARTED`.

That lease is transferred to the selected SAE generation.  Every other live
scan remains a conflict, preserving the protected-association fence.

## Verification

- Tahoe 25C56 clean candidate source ID:
  `sae-wcl-scan-handoff-clean-wip40-20260803`.
- Loaded kext UUID: `ED1ACDDA-7C5A-3409-BDA6-02EF5AE305FD`; binary SHA-256:
  `25a1b27f412939b041b4c28eb04f0896b6a351615f900b38e7062cb13f37bdda`.
- On-air pure-SAE WCL roam moved from the channel-13 source to BSSID
  `82:c3:97:84:51:ca` on channel 9.  Serial order was
  `REAL_SCAN_STARTED`, `TARGET_SELECTED`, `DRIVER_RESIDENT_WCL_STARTED`,
  `TARGET_PORT_VALID`, with no authentication timeout in the clean cycle.
- The link retained `172.16.66.187`, passed 100/100 source-bound ICMP packets,
  and returned HTTP 204 through `en1`.
- A real S3 cycle recorded `PMRD: System Sleep`, `ACPI S3 WAKE`, and
  `PMRD: System Wake`.  SAE reconnected on channel 13 with the same DHCP
  address; direct Wi-Fi SSH worked, followed by 100/100 ICMP and HTTP 204
  through `en1`.  QEMU's separate virtio user-NAT DNS did not resume, so the
  HTTP check pinned the already resolved endpoint and did not depend on that
  unrelated management device.
- Tahoe Debug build succeeded with all 1075 undefined symbols resolved.
- Contract checks:
  `scripts/test_tahoe_iwn_lab_direct_sae_userclient_contract.sh`,
  `scripts/test_tahoe_iwn_apsta_scan_scheduler_contract.sh`, and
  `scripts/test_tahoe_wcl_reassoc_roam_scan_contract.sh`.
