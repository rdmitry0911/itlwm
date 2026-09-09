# Direct SAE BSS replacement retires old TX agreements — 2026-09-09

## Proven runtime sequence

On loaded IWN source `05b6ac3f`, a normal saved-network selection was followed
by the system's real background WCL reassociation. With independent primary
traffic running, matching-build FBT captured direct SAE retarget, old-BSS
copy with 11 outstanding queue-10 descriptors, and a new aggregate start
with those same outstanding descriptors. No old aggregate stop ran between
them. The next watchdog retained a count of 11 with equal read/write cursors.
Exact times and loaded artifact identities are in the STA aggregate-stop note.

The ordinary WCL leave path does call the driver's stop callback, so changing
only that callback cannot repair this direct RUN-to-AUTH replacement.
`ieee80211_node_copy` calls cleanup, which clears the old BA agreements and
timeouts before copying the new node. The later state transition sees the
new node, not the old hardware agreement. IWN also resets its allocation mask
for the new RXON, allowing its next ADDBA to rebase the still-owned queue.

## Correction and reference boundary

After join admission, but before replacement-epoch/key retirement and node
copy, a RUN STA now invokes the existing local TX-agreement teardown on the
old BSS. The `-1` argument avoids a new on-air DELBA or speculative leave.
The callback still belongs to each hardware backend. The source-preserving
SAE retarget preparation/rollback remains unchanged; this runs only after
the common BSS replacement has actually been admitted.

The existing legacy net80211 roaming path already stops aggregation before
BSS replacement. Intel's
[DVM stop path](https://raw.githubusercontent.com/torvalds/linux/master/drivers/net/wireless/intel/iwlwifi/dvm/tx.c)
and [transport queue disable](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/net/wireless/intel/iwlwifi/pcie/tx.c)
likewise preserve outstanding queue ownership through teardown. The matching
Apple WCL reassociation carrier and lower-acceptance evidence remain as
documented in `TAHOE_WCL_REASSOC_BSSID_ABI_20260909.md`; Apple's different
firmware is not evidence for Intel-specific descriptor offsets or counts.

This common replacement edge serves IWN/IWM/IWX callers. It is not equivalent
hardware qualification of all three families, and does not disable aggregation,
clear a live queued count, fabricate a completion or relax PMF ownership.

## Verification boundary

The ASan/UBSan test compiles the actual complete `ieee80211_node_join_bss`
method with observed substitutes for external subsystems. It checks old
aggregate teardown before epoch retirement and copy, same/different BSSID,
admitted/rejected join, RUN versus SCAN/AUTH, STA versus non-STA, and rejected
post-copy binding. A rejected join does not stop the source link. The old
`05b6ac3f` method fails the pending-old-queue assertion; the corrected method
passes. The actual IWN stop-backend regression independently verifies physical
descriptor release before cursor reset.

PAE epoch, direct-WCL SAE, three-family WCL roaming and IWN BTM contract tests
pass. Loaded-image on-air roaming under traffic, repeated APSTA start/stop,
actual S3 recovery and the system-sharing security/DHCP matrix remain required
before promoting the held release.
