# Roaming policy and post-transition data path — 2026-09-10

## Open failure, unchanged production image

The published `964a90b3` IWN/6235 image previously associated on a strong
2.4-GHz BSS, obtained DHCP, then accepted an airportd best-connected roam to
a weaker BSS. ARP/DHCP stopped receiving responses after that transition.
The exact old-boot sequence remains in
[the protected-leave qualification](TAHOE_PMF_LEAVE_TX_KEY_20260910.md).
That failure is not closed by the passing controls below. No production
source correction or new release is claimed in this investigation.

## Exact reference recovery

The guest's own 25C56 BootKC SHA-256 is
`eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d`.
It matches the saved Ghidra input, independently checked against the running
guest's system kernel collection. The companion 25C56 Broadcom DEXT input
SHA-256 is
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

The DEXT batch recovered 192 functions. The final kernel batch submitted
118 named functions with 40 actual decompiler interfaces, not merely a
40-CPU wrapper setting. Kernel function boundaries were reconstructed from
the exact guest symbol table in a read-only analysis session. Previously
overmerged function bodies and false no-return metadata for allocation,
logging, time, `safeMetaCast` and `bzero` had truncated otherwise successful
C outputs. The original binary and saved project were not modified.

The final kernel manifest hash is
`eacf2467af48cacdd7ee967584c76bd30e262f1cf3d2348b58f6bab75c14a1b6`;
DEXT manifest hash is
`33abe5ad584c4b03fc2e22b36f20939ba5d5f93e2cfdf3a8a7cfce8781b4e2da`.
Two adjacent kernel functions still have an unresolved instruction warning
in a logging helper. A decompiler's completed flag is therefore not treated
as proof that every recovered function is semantically complete.

Directly inspected contracts include:

- `setROAM` at `0xffffff80021e4272` requires a 20-byte public carrier.
  `WCLNetManager::setROAM` at `0xffffff80021e158e` reads channel-presence,
  channel-band, band-presence and channel at offsets 4/5/6/7, and BSSID at 8.
  The old local 13-byte header is not this public ABI. The userspace producer
  independently constructs a 20-byte request.
- `sendReassocCommand` at `0xffffff80021dec28` may resolve an explicit BSSID
  to one cached channel, then normalizes its channel specifications. An
  unrestricted BSSID is not equivalent to a credentialed fresh join.
- `Core::setWCL_REASSOC` at `0xffffff8001616794` applies temporary pruning
  and, when request flag bit 2 is set, clears candidate boosts before issuing
  reassociation. Immediate failure restores the prior parameters.
- `setReassocParams` / `restoreReassocParams` at `0xffffff800152f014` /
  `0xffffff800152f228` maintain that temporary override lifetime; restoration
  reapplies the stored three-band boosts and default -80 dBm pruning.
- The matching kernel and DEXT roam-profile producers stop at the first zero
  trigger, skip a bracket with its enable/control word at bracket offset
  `0x1c` clear, and preserve separate global 5/6-GHz candidate boosts from
  public offsets `0x22a` / `0x22e`.

The Intel translator currently continues past a zero trigger, does not apply
that disabled-bracket gate, and does not implement those independent global
boosts. WCL candidate selection excludes the source BSSID and ranks eligible
other BSSs by RSSI, separately from its autonomous roam-profile comparator.
These are identified contract gaps, not established causes of the observed
ARP/DHCP blackhole. Broadcom firmware candidate scoring and every policy flag
are not claimed completely recovered or implemented.

## Reboot control and public request route

Only the owned disposable guest rebooted at 09:09:28 UTC, retaining the
published Mach-O SHA-256
`a5a808d8b7e644401b56a44554f2c52ca2232c5de33fbd5c4b2c1c2b4a685752`
and loaded UUID `EDEEE35F-92DE-3EE3-8F25-3EBA042D1275`. New boot UUID:
`318BB068-B3F6-4BA3-87AD-08AE626DFC12`. SSH returned within 120 seconds.
The read-only TX/crypto observer started at 09:10:26, before the automatic
best-connected request at 09:10:49. That real physical scan ended with no
eligible target, so no BSS transition occurred. DHCP and a later 10/10
1400-byte source-bound check passed. The observer ended at 09:13:27 with
zero diagnostic errors and no recorded encryption/decryption failure.

A direct BSD ioctl using the verified 20-byte carrier returned errno 102
before any observed retarget. It is not counted as a roaming attempt reaching
the hardware. The current Apple80211 framework subsequently opened and bound
the same interface successfully; its `Apple80211Set` call with a BSSID
dictionary reached the real WCL request and physical scan. No entitlements,
kernel objects, radio admission tables or firmware state were patched.
The first framework probe used a non-exported implementation symbol and
exited before opening the interface; it is also excluded from driver results.

## GUI off/on and directed transition control

After ordinary login, the GUI was usable. System Settings switched Wi-Fi off
at approximately 09:22:34; independent readback at 09:22:36 showed power Off,
inactive carrier and absent IPv4. GUI on at approximately 09:23:14 completed
SAE on the strong BSS at 09:23:22; DHCP reached BOUND at 09:23:25.

A framework request naming the weaker BSSID was accepted at 09:23:29. WCL
derived one channel from its cache; the real scan selected that BSS at about
-69 dBm, driver-resident SAE retarget ran at 09:23:35, and DHCP reached BOUND
at 09:23:36. At 09:23:55 the gateway had no ARP-cache entry, but this alone
was not a blackhole: the subsequent source-bound 1400-byte check passed
10/10 and populated the gateway entry. No additional off/on was used.

The complete 09:22:10–09:25:11 observer ended with zero diagnostic errors.
Its post-retarget phase recorded 95 successful TX submissions, all observed
single-frame firmware statuses successful, 90 successful encryption calls
and 365 successful CCMP decryptions. Some frames required ACK retries. These
are working-path controls, not proof that the earlier failure is fixed or
that every aggregate frame succeeded.

## Unrestricted requests

An unrestricted framework request at 09:24:59 was superseded by a new WCL
foreground request while the Wi-Fi settings pane remained open. No completed
transition is claimed for it. System Settings was then quit normally; no
wireless daemon was restarted and no scanning policy was disabled.

The next unrestricted request at 09:25:50 selected the strong BSS, completed
SAE at 09:25:58, reached DHCP BOUND at 09:26:00 and passed 10/10. Another
unrestricted request at 09:26:44 selected the weaker BSS, completed SAE at
09:26:51, reached DHCP BOUND at 09:26:53 and passed a separate 10/10 check.
The complete observer ended at 09:28:50 with zero diagnostic errors and
empty stderr. Both retarget phases recorded only successful TX submissions
and single-frame firmware status. Their 75/100 encryption calls and 776/419
CCMP decryptions succeeded. ACK retries and aggregate replies remain visible;
these counters do not claim lossless aggregate transmission.

These controls establish that neither low RSSI, an unrestricted request nor
the direct SAE retarget alone inevitably produces the original failure.
They occurred after a real GUI off/on. The original failure, complete policy
parity, post-sleep GUI matrix and IWM/IWX hardware qualification remain open.

## Fresh-boot automatic transition, without GUI off/on

A second normal reboot of only the disposable guest at 09:30:18 UTC retained
the published image. The new boot UUID was
`F77C8891-B6C9-4EC1-B2E2-21BF9289C253`; SSH returned at 09:30:56. The observer
started at 09:30:58, before initial authentication. Initial association on
the strong channel-13 BSS reached RUN at 09:31:09 and DHCP BOUND at 09:31:10.

Airportd independently started best-connected roaming at 09:31:33. The real
scan selected the weaker channel-9 BSS, retarget started at 09:31:38, RUN
followed at 09:31:40 and DHCP reached BOUND at 09:31:41. The manual framework
request at 09:31:43 returned EBUSY during that already-running transaction;
it is not counted as another transition. The preceding three-packet check
also occurred after automatic retarget, not before it.

Later independently awaited 1400-byte checks passed 10/10 in each direction.
No GUI login, radio toggle or explicit join intervened on this boot. The
complete observer ended at 09:33:59 with zero diagnostic errors and empty
stderr. Its post-retarget phase recorded 13,869 successful TX submissions,
13,863 successful encryption calls, 541 successful CCMP decryptions and only
successful observed single-frame firmware status; this phase also includes
the start of the subsequent UDP control. Aggregate notifications and ACK
retries are not interpreted as lossless per-frame delivery.

Thus fresh-boot automatic retarget can succeed without off/on. That narrows
the reproduction conditions; it does not erase the earlier failed boot.

## Load controls and limits of the loss observation

The original 90-second UDP control started at 09:33:38 on the weaker BSS,
after its successful roam and packet checks. The sender offered 20 Mbit/s;
the receiver reported 5.99 Mbit/s and 128,960 lost of 187,360 datagrams
(about 69%). No additional requested transition was issued during this
stream. The first approximately 20 seconds overlap the observer above;
the later unobserved intervals retain substantial loss.

A separate 20-second control at 09:41:15 offered 2 Mbit/s on that same BSS
and delivered all 4,167 datagrams. An addressed framework request at 09:41:53
then selected the strong channel-13 BSS. Retarget and RUN completed at
09:41:59 without off/on; the ordinary STA address was present at 09:42:20.
The next 20-second, 20-Mbit/s control started at 09:42:39. Its receiver
reported 10.8 Mbit/s and 16,951 lost of 41,664 datagrams (about 41%).

The accompanying 09:41:53–09:44:54 observer ended without diagnostic errors
or stderr, recording 24,865 successful TX submissions, 24,860 successful
encryption calls and 2,460 successful CCMP decryptions. Its observed
single-frame firmware statuses were successful, with ACK retries. Neither
these counters nor a submitted-packet count cover packets dropped before
the lower TX function, or every aggregate completion.

Both throughput controls traverse the guest radio, an external AP and the
host's other Wi-Fi link. The observed loss is not yet attributed to one
driver, radio, queue or network segment. A proposed wired-receiver control
at 09:44:56 did not establish an iperf control connection; the bounded
capture received no matching UDP packets and the server expired normally.
It is excluded from throughput results. No management route, AP setting or
host interface was changed for that attempt.

## Four consecutive transitions and address withdrawal

On the same published image and boot, four unrestricted framework requests
at 09:47:31, 09:47:57, 09:48:24 and 09:48:50 UTC alternated the two BSSs.
Each reached target RUN and passed its independently awaited three-packet
check, without radio toggling. The complete 09:46:34–09:49:35 observer ended
without diagnostic errors. This does not erase the earlier failed boot.

Exact-current-boot IPConfiguration logs show media inactive, IPv4 removal,
media active and restoration of the same address at **every** transition.
The inactive edges were 09:47:37.149, 09:48:03.242, 09:48:29.969 and
09:48:56.286 UTC. The corresponding address removals were .260, .359,
09:48:30.077 and .398; DHCP returned to BOUND at 09:47:38.382,
09:48:06.982, 09:48:32.613 and 09:49:00.177. The public link-down/up sequence
is therefore a reproducible intra-ESS interruption even when service returns.

One continuous 170-second, 2-Mbit/s iperf UDP run overlapped these requests.
It offered normal traffic until the first transition, then reported no new
offered datagrams for the remaining 100 seconds. Both processes eventually
exited successfully, but this is **not** a low-loss continuity pass: the
offered stream stopped. A later process-sampling attempt found that iperf
had already exited and provides no stack evidence for its stopping condition.

## Persistent-socket control

A separate bounded Python probe opened one connected TCP socket and one UDP
socket, each bound to the actual STA address. Neither socket was reopened or
reconnected during the test. A loopback control first passed without errors.
The host echo peer then ran from 09:57:15 UTC and the guest client from
09:57:17 for 75 seconds. A real unrestricted roam was accepted at 09:57:40.

At approximately 09:57:46, UDP send observed seven ENOBUFS and five
EADDRNOTAVAIL results; TCP observed seven EAGAIN results. Both streams
subsequently resumed on their original sockets. TCP sent and received exactly
3,207,000 bytes, with no pending output at the normal terminal. UDP sent
3,202,000 and received 3,178,000 bytes. The peer accepted TCP only once and
saw EOF only after the client's normal completion.

Thus the iperf observation must not be generalized to destruction of all
established flows. The proven user-visible defect is transient address
withdrawal and socket errors during a successful same-network roam. The
separate earlier persistent ARP/DHCP failure remains unclosed.

## Reference carrier boundary and next correction

The additional exact 25C56 batch recovered 117 link/roam functions with 40
actual decompiler interfaces. Its manifest SHA-256 is
`f01a9b790d86c90616732a11ac7d14cd3e2b4b754f94a84a4818fb9f97308292`.
Returning time and memmove helpers required metadata correction to recover
the complete `Core::handleRoamEvent` body. Previously truncated C is not
treated as a complete contract merely because decompilation reported success.

`Core::handleRoamPrepEvent` / `handleRoamEvent`, `NetAdapter::handleRoam`,
and `WCLRoamManager::roamStart` / `roamDone` are separate from genuine
`NetAdapter::setLinkDown` / `WCLRoamManager::linkDown`. The latter retire
association ownership; a successful roam does not invoke that ordinary
link-down path. `WCLNetManager::updateLinkState` and the Infra
`setWCL_LINK_STATE_UPDATE` consumer independently encode current-BSS refresh
and link-state change. Pending-packet retrieval only reads two counters; it
is not a recovered queue-flush/replay implementation.

The local `ieee80211_newstate` unconditionally publishes LINK_STATE_DOWN,
including controlled same-ESS AUTH/ASSOC/RUN progression. The next correction
must distinguish this admitted BSS replacement from a real link loss,
preserve target security admission and current-BSS publication, and retire
any continuity ownership on failure, reset, cancellation or another epoch.
No production correction is included in this investigation checkpoint.
