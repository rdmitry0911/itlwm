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

## Loaded-image queue ownership and repeated APSTA qualification

Source `94b39c4b` built with all 1083 BootKC symbols resolved. Private
five-member AuxKC admission and transactional activation passed. The guest
boot at 20:27:23 UTC loaded UUID `4DB60C90-6ACF-3DA5-8C35-0700E97EE3B7`,
matching frozen Mach-O SHA-256
`e99f5017ae79df7ec66954c340043a1aef9796d4cfeb1e7fbd8b859ca86ba254`.
The read-only queue observer used this build's DWARF, not earlier offsets.

With primary traffic running, ordinary selections completed in nine and ten
seconds without printed errors. The actual background WCL/SAE retarget at
20:28:42 stopped TID 0 with three outstanding descriptors before node copy;
the node-copy edge saw zero, and the successor aggregate started empty.
At 20:29:38 the second retarget repeated this ordering with ten outstanding
descriptors. A third retarget at 20:32:42 also stopped the old agreement
before copy, this time with an already empty queue. All three returned 1.
The five-minute observer reached its terminal at 20:33:03 with an empty
diagnostic-error file. No device-timeout watchdog or driver reset appeared
in this boot's qualification interval.

The encompassing stress run sent 14,000 1400-byte packets and received
12,662 (9.6% loss), including foreground reconnects and background roaming.
This is ownership/reset regression evidence, not lossless-roaming or a
steady-state performance qualification. A separate post-transition primary
check passed 10/10 packets.

Two separate role-7 SAE/required-PMF APSTA starts then admitted the external
AX211 with power save enabled. Each passed 20/20 client-to-AP packets, an
isolated cold-neighbor 10/10 in the reverse direction, and concurrent primary
traffic at 5/5. External capture records each cold ARP request and reply;
matching lower probes record queue-8 completion without diagnostic errors.
The first normal AP stop retained primary traffic at 10/10, followed by
another ordinary saved-profile selection and 5/5 traffic. Dependent ping,
cold-neighbor and stop phases were awaited separately.

## Actual S3 on the corrected image

The 20:38:56 UTC sleep request, with both roles active, reached serial
`ACPI SLEEP` and independently observed QEMU `paused (suspended)`. The owned
monitor received wake at 20:39:42, followed by serial `ACPI S3 WAKE`. Physical
STA SSH confirmed the unchanged boot epoch and loaded UUID; primary traffic
passed 5/5 without a Wi-Fi toggle or selection. The driver replayed the AP
on the recovered primary's shared channel.

The external client's non-autoconnect test profile had left for its ordinary
network during sleep. Explicit reselection completed SAE/PMF on the restored
AP without calling AP start again. Client-to-AP traffic passed 20/20 and the
primary passed 5/5. At 20:44:37 a separate cold AP run passed 10/10 with
power save enabled, an externally captured ARP request/reply and successful
queue-8 completion. This test was detached seven seconds before its ARP
removal; no management SSH or client-originated ping crossed the measured
AP path during the cold window. The observer finished without errors.

The independent virtio Ethernet management path did not recover: its DHCP
summary initially retained a bound lease while its IPv4 address was absent.
A DHCP retry, temporary restoration of its prior address, virtual link toggle
and interface down/up did not restore reachability. Physical Wi-Fi remained
available. This is a separately observed management-transport limitation,
not a demonstrated cause in the Intel driver, and no guest reboot or USB
network device was used to obtain the Wi-Fi recovery result.

These checks establish recovery of the tested APSTA service, not automatic
client continuity, every GUI/saved-profile sequence or equivalent IWM/IWX
hardware coverage. The post-S3 system-sharing/DHCP matrix remains a separate
gate before release promotion.

The normal post-wake role-7 stop retained primary traffic at 10/10. The
following attempted standard-sharing matrix is not qualified: its original
Ethernet upstream was unavailable, no InternetSharing daemon/bridge was
started, and the client found no AP. The earlier attempted `ifconfig en0 up`
remained in `ioctl`; a one-second process sample also found IPConfiguration's
serial worker in `ioctl`. Fresh DHCP inspection requests waited behind it.
These userspace stacks do not identify the kernel lock owner or establish
that the two waits have an identical cause.

A temporary USB Ethernet added after wake first failed enumeration under the
S3-retained emulated hub. Direct-root-port attachment created its interface,
but did not restore the already blocked configuration service. It was removed
while awake; it was never carried through another S3. Diagnostic callers were
stopped by their exact PIDs. This attempt neither reproduces the previous
USB-detacher `EBUSY` cause nor proves that boundary repaired. A fresh repeat
must avoid the post-wake VirtIO down/up operation and establish a working
upstream before counting any sharing/DHCP result.
