# Failed roaming target and WCL association teardown — 2026-09-10

## Reproduced boundary

The loaded `c19ab0db` image correctly withdrew BSD carrier and IPv4 on the
controlled on-air SAE Confirm mismatch, but airportd still considered the
old network associated. Its immediate auto-join attempts were rejected by
that predicate. Full timing, independent AP rejection, eventual automatic
other-profile recovery and packet checks are recorded in
[the carrier qualification](TAHOE_ROAM_CARRIER_CONTINUITY_20260910.md).

No new runtime instrumentation was needed to identify this boundary: the
existing lower scan/SAE logs, external hostapd and current-boot native network
events expose both the actual failure and the inconsistent consumer state.

## Reference contract and remaining reconstruction

Route: REUSE_REFERENCE_DECOMP. Exact 25C56
`AppleBCMWLANNetAdapter::handleLink` at `0xffffff800152590a` publishes an
independent 16-byte WCL `0xd8` link indication. It carries the event BSSID,
link-state byte, interface-type byte and normalized reason. A failed roam
is not represented merely by the reassociation-result bulletin.

`WCLRoamManager::setReassocFail` at `0xffffff8002104928` clears its request
bookkeeping and consumes the bulletin; it does not run WCLNetManager's
connection teardown. `WCLNetManager::linkDownInd` at `0xffffff80020edee8`
has an explicit reason-5 branch for authentication/association failure when
roaming to a new target. That path delegates to `leaveNetworkCommand` at
`0xffffff80020ed43e`. Its terminal `linkDownComplete` publishes the
20-byte `WCL_LINK_STATE_UPDATE` IOC and retires the framework association
state. The independent roam-manager link-down event also retires its timer,
locks and temporary policy state.

The local watchdog sends the existing reassociation-failure event and enters
SCAN. The generic carrier path publishes parent/BSD link-down, while the
separate WCL link-down producer is currently reached by explicit deauth,
beacon-loss, disassociate and power-off paths. It is not reached by the failed
roaming-target path. Clearing the driver's BssManager is not a replacement
for WCLNetManager's own teardown.

One saved decompilation incorrectly treated the exact kernel `_memcmp`
symbol at `0xffffff8000104450` as no-return, truncating ordinary branches.
The symbol was independently verified against this guest's own BootKC.
A new read-only batch covers all 308 exact symbol-bounded functions of
WCLNetManager, WCLRoamManager and their FSM bases, with that metadata corrected
and 40 actual parallel decompiler interfaces. All 308 jobs completed; two
country-code extension functions retain bad-instruction warnings and are not
claimed complete. The active link-loss, leave, completion and FSM functions
were checked separately. Manifest SHA-256:
`9acf702e497267127a4e160de3e12e3ea20e3c55a99f302c99038e2edf344a0c`.

The first configuration-table reader failed because the config at
`0xffffff80023e2c20` is BSS storage initialized at module load, not populated
file data. The exact initializer `0xffffff80020f2b66..0xffffff80020f2be8`
assigns the static context at `0xffffff80023db620` and dimensions 7 states,
11 events and 22 actions. Decoding the matching level-zero BootKC chained
pointers recovered all 77 cells and 56 subscriptions. The `0xd8` subscription
reaches `linkStatusInd`, whose link-state byte selects LINK_UP or LINK_DOWN_IND.
From WAITING_FOR_CONNECT_COMPLETE, WAITING_FOR_IP, LINK_UP and SLEEP, the latter
invokes `linkDownInd`. LEAVE_NETWORK then enters DEAUTH/SLEEP_DEAUTH and calls
`leaveNetwork`; LINK_DOWN_COMPLETE runs the terminal cleanup. A repeated
LINK_DOWN_IND in DEAUTH, SLEEP_DEAUTH or LINK_DOWN is ignored. Local producer
one-shot ownership is still required; consumer tolerance is not permission to
publish events for a newer association. Table SHA-256:
`a566a2f25cace5dd33e4516d86e10cdd34f44dfcaead15235b8ba8451b24760b`.

## FIX_CANDIDATE

The required correction is a one-shot, epoch-owned failed-replacement
notification to WCL, in addition to ordinary BSD carrier retirement. It must
be sent only after the old association has actually been replaced/lost;
no-target or superseded scans that retain the source must not be converted
into disconnections. A late backend callback must not detach a newer join.
Existing deauth/beacon-loss publications must not gain a duplicate terminal.
The selected failed target identity and the event's lifetime across the
controller gate must be explicit; a generic reason-9 internal disassociate
or a shortened timeout is not a substitute for the reference event.

Source correction, actual-production-function regression with an unchanged
negative control, full adjacent three-family tests, build, exact-image load
and repeated successful/failed on-air roaming remain required. There is no
new production change or qualified release at this checkpoint.

## Implemented candidate and source checks

The common association cancellation now consumes the exact same-ESS carrier
reservation while holding the selected-BSS leaf lock, before invalidating that
selected value. It copies only BSSID and the post-cancellation publication
epoch. After the lock and existing credential-revocation callbacks have been
released, the controller revalidates epoch and BSSID inside its command gate
and queues the independent 16-byte WCL link-down indication with normalized
reason 5. No new timer, synthetic reassociation result, forced framework state
or credential-preservation exception was introduced.

Direct lower/preflight failures and explicit common carrier retirement consume
the same one-shot reservation. A stale backend epoch cannot retire a newer
lease. These direct paths finish their ordinary BSD publication before the
new event's potentially yielding delivery, and cannot apply another DOWN after
that callback. Cancellation fences retain their pre-existing lower state
transition owners. Source-retaining scans have no replacement reservation and
produce no new link-loss indication. An authorized successful RUN retires the
reservation without a failure event; early IWX port authorization still waits
for its committed RUN.

Public disassociate, WCL leave/join-abort, radio off, RUN deauthentication and
firmware beacon loss retire the replacement-only notification lease before
their existing terminal path. A fresh public/WCL association also disarms it.
Pre-RUN deauthentication has no eligible RUN-only link indication, so an actual
lost replacement there retains the new common failure terminal.

The expanded ASan/UBSan regression compiles the production cancellation body,
snapshot/claim helpers, BSS replacement, carrier bridge and gated WCL producer.
Its 98 cases include duplicate failures, cancellation before invalidation,
reentrant epoch/BSSID replacement, same-BSSID newer joins, explicit cancellation,
successful completion, missing lock/invalid identity rejection and the exact
zero-initialized WCL payload. Dependency fixtures model state and callback
boundaries, not hardware or crypto. The unchanged `a9918b4f` cancellation body
compiles with the same fixture and fails the reservation-retirement assertion
(exit 134); the corrected code passes. The full payload suite and adjacent
SAE, association-comeback, initial-BSSID, WCL reassociation, three-family beacon
loss, link-context and public-disassociate contracts pass.

Build, exact-image load and repeated successful/failed on-air roaming are still
required before this candidate can replace the published `964a90b3` image.

## Loaded candidate and first failed-target recovery

Source `b5c6cfd8` built with all 1085 external symbols resolved. Private
five-member AuxKC admission and transactional activation preserved the four
companion members. A normal disposable-guest reboot at 12:25:29 UTC returned
SSH with the new loaded UUID by 12:26:26, within 57 seconds. Boot session
`9C8DDC74-0D6C-49D7-9200-4DE18FBC3C76` loaded UUID
`9D5A9332-924A-3D77-8615-7E1CF1C73CB1`, matching Mach-O SHA-256
`4037898627c66b449c6c6688378910954c65839b62a0bdf5c702792e356cb7ec`.
The initial saved pure-WPA3/required-PMF connection obtained its ordinary DHCP
address and passed 5/5 1400-byte packets. An automatic completed transition
to the other ordinary BSS preceded the negative test.

The isolated same-SSID wrong-password AP became ready at 12:27:39 UTC.
The public directed request at 12:28:15 selected its actual BSSID at -33 dBm.
External hostapd received SAE Commit and Confirm, rejected the Confirm
mismatch and did not authorize the station. This repeats the real failed
authentication boundary, not a no-target scan or request-only check.

Exact-current-boot native logs show BSD link inactive at 12:28:24.886176,
BSSID_CHANGED at .899159, the autojoin manager's associated network becoming
null at .900909, and SSID_CHANGED at .917234. IPv4 was removed at
12:28:25.007886. The associated-network cleanup therefore followed the first
link-off observation by about 15 ms, not the prior approximately 54 seconds.
There is no Already-associated autojoin refusal between that loss and the
next actual successful association; later refusals while connected are not
misclassified as the reproduced defect.

Automatic recovery attempted the saved WPA3 profile at 12:28:28.693932,
about 3.8 seconds after loss. That attempt still failed: hostapd recorded a
second Confirm mismatch, and airportd excluded the profile at 12:28:39.246264.
It then selected the saved WPA2 profile, obtained carrier at 12:28:45.847620
and DHCP BOUND at 12:28:46.973350, approximately 22.1 seconds after loss.
Thus this is prompt retirement of stale association ownership, not complete
candidate-selection equivalence or recovery to the original WPA3 BSS.
In particular, Apple's fresh-join candidate record named a strong channel-13
BSS while the wrong-password channel-9 AP saw another exchange; that target
selection boundary remains a separate question to resolve.

Actual recovered-address forward traffic passed 20/20 while the bad AP was
still active. Normal termination of only the fixture controller at 12:30:04
restored the host's ordinary managed profile by 12:30:09 and preserved its
wired default route. No guest off/on, join command, daemon restart or reboot
intervened after the directed request. The separately awaited reverse check
then passed 20/20 through the restored host Wi-Fi link. The 85-sample observer
ended normally; no matched driver panic, firmware fatal, device timeout or
unset-key diagnostic appeared in the complete candidate serial interval.
Successful-roam, GUI, sleep and AP
regressions on this exact candidate remain required before publication.

## Successful roaming after the failed-target test

The real System Settings pane selected the saved ordinary WPA3 profile at
12:32:30 UTC, without a password prompt or radio toggle. Its actual channel-13
BSSID and DHCP address returned, followed by a 3/3 packet precondition.
The first persistent-flow run overlapped four unrestricted framework requests
while the pane remained open. All four lower scans were superseded by WCL
requests and none produced a completed roam. The source address and traffic
survived these cancellations, but this is not a successful-roam qualification.
Its final TCP echo was 1000 bytes short with exactly 1000 bytes still pending
in the server when the client closed; no byte-exact claim is made for that run.

After the pane was closed, a directed request at 12:35:43 completed on the
other BSS at 12:35:52.534154, after the first flow had ended. It is likewise
not counted as a transition inside that earlier flow. A separate 150-second
run from 12:37:23.874377 used one TCP socket and one UDP socket, neither
reopened. Four further requests produced three actual ROAMED/BSSID_CHANGED
pairs at 12:37:52.556416, 12:38:17.713813 and 12:39:07.721408. Lower BSSID
readback and Apple's current-network channel updates agreed on 13/9/13.
One intervening request did not complete a BSS transition.

The exact-current-boot native interval contains no link-inactive event, IPv4
withdrawal or DHCP BOUND across those three transitions. TCP sent and received
exactly 6,301,000 bytes; the peer accepted once, echoed the same total and had
zero pending bytes at normal completion. UDP sent 6,340,000 and received
6,012,000 echo bytes, about 5.17% missing; 55 EAGAIN TCP sends and 16 ENOBUFS
UDP sends remain visible. There was no EADDRNOTAVAIL. This qualifies logical
address and established-TCP continuity, not lossless or low-latency roaming.
The competing foreground-scan behavior and full candidate-selection policy
remain independent open observations. GUI off/on, actual sleep and AP
regressions still gate publication of this image.

## GUI radio lifecycle and open-network pre-sleep control

System Settings radio-off at 12:41:18 UTC produced native inactive carrier at
12:41:18.411828 and removed IPv4 at .595171. The independent power query
reported Off and no address. One GUI on at 12:41:44 restored active carrier
at 12:41:53.900596 and DHCP BOUND at 12:41:56.664811. Separate 1400-byte
forward and reverse checks passed 20/20 each. No second toggle was used.

The isolated open AP became ready at 12:43:19. Its saved profile was selected
through the visible System Settings pane at 12:44:00. External DHCP and the
actual guest lease agreed on 192.168.73.26; the external station state reported
open security with no MFP. Both independently awaited 1400-byte checks passed
20/20 by 12:44:50, with individual reverse RTTs up to 556 ms. These results
are connectivity, not latency qualification. Actual S3 and post-wake service
checks are the next gate; pre-sleep traffic alone does not satisfy it.

The exact-image sleep guard verified hibernatemode zero, the current open
address and a 2/2 precondition. USB management and the tablet were removed
while awake at 12:45:23; the delayed guard independently required both
Ethernet interfaces and the tablet absent before requesting sleep at 12:45:42.
Serial ACPI SLEEP and the owned VM's suspended state confirmed real S3 by
12:46:30. The ordinary wake request at 12:47:37 produced ACPI S3 WAKE in the
complete current-boot serial interval. A short tail query missed that line
after newer console traffic; the complete interval, not that negative tail,
establishes wake. USB management/tablet were reattached at 12:47:59.

Native logs show automatic return to the same open network, carrier active
at 12:47:39.556343 and DHCP BOUND at 12:47:40.623303, before USB reattachment.
The unchanged boot session and loaded UUID were independently checked. Both
post-wake 1400-byte checks passed 20/20 by 12:49:01. No manual selection,
off/on or reboot was used. The first USB SSH probe timed out during interface
enumeration; the next completed. The framebuffer still displayed its 15:46
pre-sleep image at 12:49 UTC, so this proves service recovery, not a usable
post-S3 GUI.

The controlled AP was normally stopped at 12:49:33; its fixture restored the
host's ordinary managed profile and wired route by 12:49:36. The guest
automatically regained its ordinary WPA3 address and passed 10/10 without a
join command. The fresh USB upstream separately passed HTTP. Native AP
open/WPA2/WPA3 service remains the last candidate release regression gate.

## Completed same-image post-S3 AP matrix

Native system Internet Sharing ran WPA3, WPA2 and open sequentially on the
same `b5c6cfd8` boot after the real S3 above. Every external AX211 client
profile used automatic IPv4 configuration, obtained the actual 192.168.2.2
DHCP lease, and independently reported the intended negotiated security.
WPA3 used SAE with required PMF; WPA2 used WPA2-PSK; open used no pairwise or
group cipher. Every mode passed 20/20 1400-byte client-to-gateway packets,
10/10 reverse packets after deleting only the bridge-scoped client ARP entry,
and routed HTTP through the guest's independent upstream. Each bounded
lower TX observer completed with empty diagnostic stderr. No manual bridge
rewrite, daemon restart, radio toggle or reboot separated the three modes.

Normal disable at 12:52:16, 13:02:21 and 13:05:04 UTC respectively was followed
by a separate 15-second dwell. In each case `bridge100` was absent, its retired
object had zero I/O references and a cleared detach byte, and the actual
returned STA address passed 10/10 packets. WPA2/open retired objects still
had flags 0x2 and ordinary reference counts; zero I/O ownership is not a claim
that every ordinary object reference was destroyed. The host's normal managed
WPA3 profile and 172.16.66.226 address were restored; its wired default route
was preserved and the exact temporary HTTP destination route was removed.
The bounded HTTP fixture was normally terminated after the matrix.

The complete 13,684-line current-boot serial interval audited after final
disable contains no matched driver panic, firmware fatal/assert, device
timeout, unset-key or TX-gate diagnostic. Successful ADDBA responses with
`error=0` were independently distinguished from failures. This qualifies
the reproduced successful-roam address continuity and failed-roam WCL
retirement changes with GUI, actual S3 and native AP service regressions.
It does not close the separate candidate-selection ambiguity, measured UDP
loss, prior post-roam ARP/DHCP blackhole, stalled post-S3 GUI, active-AP
off-channel scans, automatic external-client continuity or ad-hoc. Common
IWM/IWX source coverage is not equivalent recent hardware qualification.

The archive was packaged from the same frozen, installed and loaded image.
ZIP SHA-256 is
`c2940702f7de42e797659bc59a36a0908756c35c219b9196eea19bd123815b44`;
its extracted Mach-O SHA-256 is
`4037898627c66b449c6c6688378910954c65839b62a0bdf5c702792e356cb7ec`.
Guest extraction, canonical/frozen comparisons, host transfer and ZIP integrity
checks passed. The previous public archive and notes are retained privately.
Publication and independent public-download verification are the next step.

## Published artifact verification

The Tahoe `v2.4.0-alpha` asset was replaced at 13:08:35 UTC with source
`b5c6cfd8`, asset ID `555040315`, size 15,613,137 bytes. The loaded boot,
kext UUID and installed Mach-O hash were independently rechecked immediately
before publication. A fresh public download is byte-identical to the qualified
archive, with both ZIP and extracted Mach-O hashes matching the values above.
The raw GitHub release-body string also matches the staged notes byte for byte.
An initial CLI text-rendering comparison had an extra output newline; comparison
of the raw JSON body resolved that formatting difference without rewriting the
published notes. The previous archive and notes remain available for rollback.
No physical user host was installed or rebooted.
