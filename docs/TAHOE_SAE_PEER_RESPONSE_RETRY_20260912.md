# Missing same-exchange SAE peer-response retry

Status: the single-Commit omission reproduces failed native connection on the
published f170870d image; its unchanged pass-through control completes SAE,
DHCP and traffic. Driver-resident retry477ab0af passes actual core,
all-family worker and timer tests on Linux and Tahoe, including both complete
payload aggregates. Its exact842B image is now loaded: the single-Commit
omission, Confirm omission and forward-all controls complete SAE/DHCP
and20/20 traffic each direction. Bounded exhaustion retires the exact owner;
native fallback reaches saved LabAP. The planned STA/AP/S3/roam controls are
complete with retained WPA2 scan-admission and packet-loss failures; this is
a limited-alpha checkpoint, not full qualification. Public alpha now contains
477ab0af/842B, with exact download verification below. Baseline failures below
are retained, not relabelled.

## Controlled radio experiment

Guest IWN/6235 retains UUID68F5B0D9-4863-3627-B30C-CC0CC111BB58 and boot
B4FEB27A-EDDA-4A70-AA35-29B68EF3AE1A. Owned AX211 hostapd2.10 advertises the
fresh SSID AIAM-SAE-RETRY-0912, BSSID80:e4:ba:20:ef:f9, channel9, group19 SAE,
mandatory PMF and CCMP. The guest station is62:ad:f9:b9:cc:f4. Wired host
management and guest USB diagnostic SSH are separate from both radios.

The host's normal managed STA producer is closed for the fixture, preventing
its background scans from interrupting the same physical AP radio. Cleanup
restores the exact original NetworkManager profile. No physical10.90.10.22,
QEMU lifecycle, PCI bridge, base disk or kext activation is involved.

The fixture uses hostapd's existing external-management test interface. It
passes received management frames and actual TX statuses unchanged through
MGMT_RX_PROCESS/MGMT_TX_STATUS_PROCESS, except for **one** well-formed first
SAE Commit received for this AP. That frame is intentionally not processed
by hostapd. No password is changed and no Authentication frame is fabricated.
Radio TX has already occurred and the guest's firmware reports its real
successful completion. Public authentication-body digests identify the exact
omitted frame; crypto keys/passwords are not sampled or printed.

The first q1 request failed before driver entry: the reused native-client
wrapper whitelisted only the previous fixture SSIDs and returned2. There was
no SAE TX and no omitted frame. Its controller result1 and ordinary host
restoration are retained. The new task-local native wrapper admits only this
new SSID, with exact boot/image guards and password via stdin. No macOS privacy
grant or entitlement was changed.

## Negative control q2: one omission destroys the exchange

One ordinary networksetup request starts at22:11:11.605732179UTC.

- Target SAE epoch220/relay23 prepares Commit ticket9223372036854775853,
  transaction1/status0/body98, at1789164679080500713ns on the guest clock.
- Its actual TX terminal result0 arrives1789164679083732964ns and the engine
  consumes it successfully.
- Hostapd records that Commit at1789164679109746677ns on its own clock and
  omits only that frame. Independent monitor capture contains the same complete
  frame at1789164679.109339s, with identical public-body SHA256
  `663fe5197309df39b9d742d6492f1aacc4386a2c62338bdb113deb4fb31eac12`.
- No second target Commit, target peer response or same-epoch retry occurs.
  At1789164683776606979ns the owner retires with request_scan1 and destroys
  the engine: **4.692874015s after its successful TX terminal**.
- networksetup reports failed join/error-3912. Its process status is0; that
  is not a successful connection result. Native recovery later starts a new
  SAE exchange with saved LabAP02/epoch242, then moves to ca/epoch243.

At the90-second terminal, the guest has LabAP DHCP172.16.66.219, not the
requested fixture. The target link check returns1. The overall controller's0
means the diagnostic and cleanup completed, not that target connection passed.
Hostapd control records exactly one omitted frame,30 passed management frames
and five passed TX statuses. Monitor capture contains one Authentication frame
for the fixture with zero capture drops. The observer ends with errors0 and
encapsulation drops0.

Guest and host clock values are not subtracted from one another. The timeout
interval is entirely guest-clock, and the monitor/control identity comparison
uses the same host clock plus the exact frame digest. Generic tcpdump labels
Algorithm3 as Reserved/[|802.11]; a bounded radiotap/pcap decoder confirms full
capture lengths and decodes the actual SAE algorithm/transaction/status/body.
This is decoder support, not a truncated packet or an Open-System frame.

## Positive control q3: the same fixture succeeds when nothing is omitted

After q2's normal fixture teardown/restoration, the same AP configuration,
SSID, station identity, hostapd external-management path and exact guest image
are exercised with an explicit forward-all controller. There is one new native
request, no radio toggle and no guest restart.

Epoch251/relay26 sends Commit ticket9223372036854775858 and Confirm
ticket9223372036854775859. Both real TX terminals are successful; the engine
accepts peer Commit and validates peer Confirm, followed by RUN at
1789164883257132610ns. AP-side AP-STA-CONNECTED follows, and DHCP supplies
192.168.73.28.20/20 packets each direction pass. The monitor contains exactly
the four complete expected Authentication frames, zero capture drops, and
public-body digests matching the hostapd receive events. The observer has
errors0 and encapsulation drops0.

Two later native background roam scans fail to find a replacement while the
fixture remains connected. This is not a claim that all scanning/profile policy
is solved. The passing target data check is retained independently.

Normal fixture cleanup completes22:16:31UTC and restores the exact host
profile, normal power-save-on setting and wired route. Without a manual guest
selection/off-on, saved LabAP returns. A separate restored-link check passes
20/20 each direction on172.16.66.219. The same boot/image is retained and no
observer/capture remains. This control does not erase the failed first request.

## Reference and implementation boundary

Pinned [Linux v6.18 mac80211 authentication](https://raw.githubusercontent.com/torvalds/linux/v6.18/net/mac80211/mlme.c)
retains auth_data's current SAE transaction/status/body, allows three total
Authentication transmissions and schedules a two-second peer-response timeout
after an ACKed SAE frame. ieee80211_sta_work repeats ieee80211_auth for the
same owner rather than starting fresh password-derived state on each retry.
Source SHA256:
`db2115573e018bbca11ba6f430d803f6a8659f4396f313a1f011dad8c12d5aad`.
The hostapd2.10 delayed-Commit test and its MGMT_RX_PROCESS/TX status handlers
provide the test-interface contract. The available updated-Ghidra Apple
handleAuthEvent export forwards real status into JoinAdapter; it does not
expose Broadcom firmware's exact SAE retransmission timer. Linux timing must
not be presented as independently recovered Apple firmware timing.

Local ieee80211_sae_engine_tx_complete clears prepared_body immediately after
successful Commit/Confirm TX. The worker handles start, pre-doorbell private-
gate retry, terminal TX and received peer frames, but has no peer-response
retry event. Those existing private-gate retries are **not** protocol retries.
The common management timer eventually revokes the epoch and cancels the
exchange. The radio negative control executes precisely that missing edge.

FIX_CANDIDATE: adapt the bounded same-exchange Authentication retry to the
driver-resident SAE engine and IWN/IWM/IWX owners. Required complete boundary:

1. Retain only the current public serialized Commit/Confirm while awaiting its
   peer response; preserve anti-clogging token and Confirm counter correctly.
   No password reload, new scalar, association epoch or BSSID for a retry.
2. Distinguish a protocol retry from rollback of a never-submitted frame.
   Every actual retransmission obtains a new lifetime-monotonic TX ticket and
   uses the normal HAL descriptor/doorbell and real completion paths.
3. Arm from a real terminal receipt, retain a bounded attempt count and
   monotonic deadline, and handle no-ACK/reset/cancel distinctly. Never turn
   failed TX into success. Preserve the same upper JoinAdapter/reassoc owner.
4. Queue work outside leaves; only the serialized crypto worker mutates the
   opaque engine. Queued peer progress wins over a stale timeout. Stop, S3,
   detach, replacement and stale/duplicate callbacks cannot transmit later
   or consume a successor. Exhaustion must use the normal owned failure path.
5. Execute actual production core/worker tests on Linux and Tahoe for HnP/H2E,
   Commit and Confirm omissions, token/duplicate peer handling, deadline
   edges, cancellation, retry exhaustion and all three HAL admission paths.
   Existing body-string contracts are not sufficient evidence for this layer.
6. After source tests, commit/push the candidate, then build/load that exact
   image and repeat omission and forward-all controls, real S3, off/on, target
   switching and open/WPA2/WPA3 STA/AP regression. Publish the kext with
   accurately scoped runtime receipts, retaining any failed controls.

The omission experiment does not explain every earlier natural SAE timeout:
the exact missing over-the-air frame in depart-q5 was not captured. It proves
a concrete capability gap worth closing, not a universal retrospective cause.

## Evidence

Working root: `/dev/shm/aiam-sae-peer-retry-20260912.SgPpSi`.
Durable root: `/home/dima/Projects/itlwm/aiam-sae-peer-retry-runtime.aMvMK5`.
Both failed setup and all negative/positive/restored results are retained.
All68 entries of EVIDENCE.sha256 verify; manifest SHA256:
`e23bbb3ded4422d293944780007b19263531d1b98ebcf7cad00af2cea04edd0f`.
The durable root is frozen; do not append later implementation/runtime work.
Q2/Q3 trace SHA256:
`7a13c72e200f361b16a031a18e66966f997410557116d8a679f29ecd5551d7e5`,
`20197d022fee9f3184f660d77135697cd75d208fad8332115e86275406f84be2`.
Q2/Q3 monitor pcap SHA256:
`79a6269bb46b935cab71afa00b098c1ea0a819400b4549bd59f6433a20202cb2`,
`31cb0ec6a99e1b5f91e4e94335bb06be08f7a6f83a1f96ae293b61d17a2e169d`.

The driver and public release remain f170870d/UUID68F5. This diagnosis does
not warrant rebuilding the same code or publishing a new kext without a fix.

## Production candidate and executed source checks

The shared opaque core retains its current public serialized Commit/Confirm
after successful terminal TX. An exact last-terminal-ticket retry prepares
that same body, without reloading a password, regenerating scalar/element,
incrementing Confirm or changing the BSSID/epoch/relay. A new monotonic ticket
is required even after pre-doorbell rollback. The limit is three actual
successful transmissions per serialized body; one anti-clogging response can
replace that body and start its own bounded budget. Failure/completion/destroy
scrub the retained body. Nonzero TX terminals still fail closed: this change
does not reclassify no-ACK, reset or cancellation as success or retry them as
though a frame had been ACKed.

IWN, IWM and IWX record an absolute two-second peer deadline at the actual
terminal-event enqueue. Delayed crypto-worker execution does not renew it.
The worker signals a retained interrupt event source; only the main workloop
arms the timer. Timer callbacks only inspect the current public owner and
schedule the serialized worker, never execute crypto or wait for a private
gate. Queued peer progress wins before timeout claim; duplicate/dropped peer
frames do not restart the deadline. Timeout claims revalidate selected-BSS,
credential, epoch and peer-RX admission under the established leaf order.
Old timers re-read the successor deadline rather than transmit for the old
exchange. Stop/S3/detach flags close admission; detach drains worker scheduling
and workers before removing both event sources and destroying the engine leaf.

After exhaustion the exact current AUTH owner arms the existing native
watchdog for one tick. That preserves ordinary owned AUTH timeout handling
instead of inventing an AP status or directly issuing SCAN from the crypto
worker. Final failure publication can therefore follow up to one watchdog
tick later than the two-second peer deadline; exact Apple firmware timer
parity is not claimed. A genuine late peer response can still win before
native timeout consumes the AUTH epoch.

Executed checks on Linux and Tahoe:

- 30 scenarios compile and link the complete production core plus vendored
  hostap/mbedTLS: HnP/H2E, zero/one/two Commit and Confirm omissions, unchanged
  bodies, actual token-bearing crypto continuation, stale/duplicate completion,
  ticket reuse, pre-doorbell rollback, exhaustion, failed TX and destruction.
- 26 scenarios per IWN/IWM/IWX compile each complete actual worker, terminal
  receipt, identity, timeout claim and main-workloop timer-drain body. Only
  hardware, crypto results, scheduling and kernel leaves are explicit doubles.
  They check receipt-anchored timing, exact deadline, peer priority, cancellation,
  replacement, no obsolete-timeout busy loop and native timeout publication.
- 12 scenarios execute the complete timer helper with IOKit/clock doubles,
  including each allocation/add-source failure, rounded-up delay, invalid gate,
  retained objects, callback during removal and repeated shutdown.
- Existing 21 IWN failure/retirement scenarios and both full payload aggregates
  pass. This is source execution, not new on-air evidence.

The first core-test fixture used a four-byte token that the chosen hostap peer
parser cannot consume as an HnP token. The corrected peer uses its supported
32-byte token and additionally checks that the original scalar/element are
unchanged. This fixture failure is not a driver retry failure. The actual
driver serializer remains bounded by its existing variable-token contract.

The independent existing IWM/IWX peer-rejection retirement gates were rerun:
both still fail their original semantic assertion (exit134), retaining
generic_scan1, owned_cleanup0 and producer_ack0. This candidate does not claim
to fix that separate all-family failure-integration surface.

Fresh working evidence (not the frozen baseline root):
`/dev/shm/aiam-sae-peer-implementation-20260912.soPlBh`.
Tahoe tests run in a separate source tree
`/private/var/tmp/sae-peer-implementation-tests.vya2G9`; the build mirror and
loaded kext are unchanged at this checkpoint. Source-test archive SHA256:
`093149edc0b55d71d916d17dcdc9909fb12570b89990dc453385071c055ed815`.
Linux aggregate SHA256:
`8c23f5ffff6e1d48c814ae71fe20dadc048644f980a414f54fd0ab4a6ab71898`.
Tahoe selected/aggregate SHA256:
`c5a6ec1722298cc94cf27e344052c07261fd49da0da972637ef1f5f6e25962dd`,
`ab514a9b8ae319351aa0b9ee4d7d119f33b25317cc47247b3f596b4a7e2ebe98`.

At that source-only checkpoint, build/load and all new RF controls remained
next. The following sections record their actual execution; broad radio
qualification and replacement publication still require the remaining gates.

## Exact production build and ordinary guest boot

477ab0af139c96ff485d5f2a3c69152ee82aacb6 was pushed before build. The357-file
production manifest1135a508616b0b89620b114a067280fcc561bf5ee862f234e894053973f98eed
was verified before and after xcodebuild in the existing build mirror. All1088
imports resolve against the actual BootKC. The new Mach-O has UUID
842B08A6-AB1B-394A-9490-C10EB1F1D9D2 and SHA256
0e0aa25caca2f9ce9b630ad37aa9d9c53730ee939128d4fe30031b86927fcf80.
The previous68F5 bundle was preserved before copying source/building.

Private admission and activation-20260911T230116Z preserve the exact five
AuxKC members and timestamped rollback copies. Normal reboot of only the
owned guest began23:02:09UTC. Boot1FB8BDE2-E95D-4B56-8948-A4FCE549D6BD
loads842B; saved LabAP returns automatically with DHCP172.16.66.219 and
20/20 packets each direction. No physical22, unrelated QEMU or base disk
was changed. The public alpha asset is still the previousf170870d image.

## New-image single-Commit omission and forward-all controls

The same fixture configuration, SSID, BSSID, SAE group19/HnP and mandatory
PMF are retained. Each run requires an empty AP station table before exactly
one native networksetup request. A fresh bounded observer additionally records
the actual queue-terminal entry and retry API ticket/result. It reads no
private engine/node/credential offsets. Complete authentication capture is
decoded independently of tcpdump's unsupported algorithm label.

retry-commit-q1 (23:06:37UTC request) starts epoch18/relay3. Original Commit
ticket9223372036854775813 succeeds at the actual event enqueue. The retry
API consumes that exact ticket2000.539561ms later, prepares ticket5814
(same high-bit prefix), and the same epoch/relay/BSSID remains active.
Hostapd intentionally omits only the first Commit; the monitor records two
complete Commit bodies with identical SHA256
14a57e51b329d899e5bec3d0593843365daa863d1fb2f97c060456789ef55ff1.
The second reaches hostapd, followed by peer Commit, client Confirm5815,
validated peer Confirm and RUN. No replacement engine intervenes.
DHCP192.168.73.28 and20/20 packets each direction pass; maximum RTTs10.658
and10.683ms. Five authentication frames, no capture drops, observererrors0
and encap_drops0. Controller and exact host-profile restoration return0;
cleanup finishes23:08:34UTC. This closes this one previously failing control,
not every natural timeout or unsuccessful/no-ACK TX case.

retry-forward-q2 (23:14:30UTC request) forwards every management frame and
actual TX status. Epoch43/relay6 uses Commit5820 and Confirm5821, both real
TX successes, without a peer retry. Four complete authentication frames,
SAE/RUN/DHCP192.168.73.28 and20/20 packets each way pass. Observererrors0
and encap_drops0; maximum RTTs9.936/8.916ms. Native background replacement
scans still report unsuccessful roam searches while the target remains
connected; those are not counted as successful roaming. Controller and host
restoration return0; cleanup finishes23:16:26UTC.

Commit control trace/pcap SHA256:
c7f68e977048773f543ad7bc2fbef81d6224f75d07f3c8c5f695e8325743cb0e,
deff993f5dd3cd94c7649086d8bffbaf8eefe76134bc918b1e2d02787ab6600d.
Forward-all trace/pcap SHA256:
ea5b99430104a82ac7ec0bb07603d7205bea434a6cff786345fad2656b81e6e8,
d9eead2629b4167d53642fe06df2d83dfd847bd9d18764fe008e31c583091e8b.

## Confirm omission and bounded exhaustion on the same image

retry-confirm-q1 starts with one native request23:17:19UTC, epoch68/relay9.
Commit5826 passes; hostapd omits only Confirm5827. The driver invokes retry
2000.483760ms after that successful TX enqueue. Prepared ticket5828 is
rejected before hardware submission and explicitly rolled back. The existing
private-gate retry prepares5829, which actually transmits successfully; both
the original and repeated on-air Confirm have the exact public-body digest
3b6925611cf485a8d8cbd9b655de62d9a6767f9c50a239971cbfb61fc6d5aa68.
This is one protocol retransmission, not two:5828 has no on-air frame or
successful TX terminal. Peer Confirm validates in the original epoch, followed
by RUN and DHCP192.168.73.28.20/20 packets each direction pass, maxima9.199
and5.820ms. Five complete auth frames, capture drops0, observererrors0 and
encap_drops0. Controller and exact host restoration return0 at23:19:17UTC.

retry-exhaust-q1 makes one native request23:19:58UTC while its bounded
hostapd controller omits every received Commit. Epoch93/relay12 transmits
exactly three identical Commits, tickets5834--5836, each with an actual
successful terminal. Retry after the third returns-2,6006.382355ms after the
first receipt; the ordinary timeout retires the owner364.451307ms later.
No fourth same-epoch transmission occurs. Native policy starts another target
attempt, epoch113/relay13, with a new Commit body and tickets5837--5839.
It also stops at three and retires313.925129ms after its exhausted result.
The host capture contains exactly these six complete auth frames, two groups
of three identical bodies; no fixture AP_CONNECTED or fake success occurs.

The system client reports failed join/error-3912 even though its process
status is0. Later automatic policy connects to saved LabAP02 at epoch128,
then moves to LabAPca at129. These are separate exchanges, not success of the
requested silent fixture. Final DHCP is172.16.66.219; the diagnostic wrapper
and host restoration return0 at23:21:33UTC, observererrors0/encap_drops0.
The independent subsequent restored-link check passes20/20 each direction,
without explicit guest selection or off/on. This proves bounded retry and
native fallback for this omission control, not same-profile/seamless recovery.

Confirm trace/pcap SHA256:
2b9f7a95e938b7e418a04be5ea89e468d5ea4f40229edd8be000d9223fa6ae34,
39dca7f0916ecb0164c86efcb0d9bab85f1d9e77c77f999c7414f533611e1503.
Exhaustion trace/pcap SHA256:
d856ceb704b76cf85dc06f50547f8529ed92e321e06ce7b1d3380fe95cce3172,
b75c2ea4ae9e102503b18a603f477e1f457934865ddb4d004d82b4c832e09c8f.

The first generated Confirm runner failed bash syntax validation before
execution because a text-substitution replacement interpreted shell dollar
syntax. It was corrected with a literal callback replacement and all final
script snapshots parsed before execution. No live runner/shared fixture was
edited. The preserved controls above are the actual runs, not parser tests.

## Actual S3: native link returns, strict data gate fails

The independent pres3-q1 baseline passes20/20 each direction on restored
LabAP, with no new network selection or off/on. The controller removes the
exact diagnostic USB NIC and tablet, confirms their guest interfaces absent,
and arms one normal sleep. QEMU actually reaches suspended state23:23:41UTC,
is held five seconds and woken once23:23:46. macOS records Normal Sleep
23:23:38 ->23:23:47UTC. The arming/request delay is not counted as sleep.

The same boot1FB8 and image842B return on Wi-Fi alone, with WPA3 and DHCP
172.16.66.219. The strict20-packet test fails: guest-to-host19/20 (missing
sequence12), reverse20/20. Maximum RTTs255.184/198.859ms. Controller23567
returns1 and restores both exact USB devices in its failure cleanup; no repeat
replaces this result. The final guest power receipt is collected separately
over the restored diagnostic interface because the failed data gate precedes
that step. This proves actual wake/link recovery, not a zero-loss S3 pass.
No endpoint packet captures covered this specific loss, so its location is
not retrospectively assigned to the earlier firmware failure or bridge.

On this same post-S3 image, the first native Internet Sharing regression
(WPA3) completes at23:26:44UTC. External AX211 negotiates SAE/group19/HnP,
required PMF and BIP, gets DHCP192.168.2.2, passes20/20 forward and10/10
cold-ARP reverse1400-byte packets, and fetches the exact118-byte HTTP payload
through guest USB/NAT backhaul. Normal AP stop removes bridge100 and the
restored STA passes10/10 gateway packets. Host-profile restoration returns0;
wired management is unchanged. WPA2 completes at23:28:53UTC and open at
23:31:12UTC, with the same20/20 forward,10/10 cold-ARP reverse,118-byte
HTTP/NAT and10/10 restored-STA checks. AX211 negotiates WPA2-PSK and NONE
respectively and obtains192.168.2.2. All three wrappers and exact host-profile
restorations return0, without duplicates in their forward summaries. The
remaining STA/roam controls are still separate pending gates. This tests AP start
after sleep, not continuity of an already active AP through sleep or GUI clicks.

The ordinary packaging step archives the installed, tested bundle without
rebuilding. Full357-file source manifest, equality with the build bundle and
extracted-archive equality all verify. Prepared archive size15,693,832bytes,
SHA256f1fa87afb14519112deccb45e7db8122b1b3eefa99f707f2d4c101b4e51f749c;
its Mach-O retains UUID842B and SHA0e0aa25caca2f9ce9b630ad37aa9d9c53730ee939128d4fe30031b86927fcf80.
The copied host archive has the same digest. This is not publication: public
alpha still containsf170870d/68F5, and its prior archive is retained separately.

## Native client regression: open/WPA3 pass, WPA2 scan admission fails

Saved WPA3 off/on returns DHCP172.16.66.219 without an explicit selection;
the separate after-off-on-q1 check passes20/20 each direction. Native open
selection23:33:09UTC obtains192.168.73.26 and passes20/20 each direction,
maxima176.456/170.520ms. Native WPA3 selection23:36:22UTC obtains192.168.73.30
and passes20/20 each direction, maxima180.301/123.717ms. Both fixture wrappers
and host restoration return0. These are native system-client requests, not
mouse-driven GUI qualification or proof of every first-selection timing edge.

The intervening WPA2 request23:34:20UTC reports "Could not find network
AIAM-UIF3-WPA2" at23:34:23. Its process status0 does not mean success.
The target/security gate never passes; the last poll still shows saved
WPA3 LabAP. Wrapper43771 returns1 and restores the host23:35:35UTC. The AP
was ENABLED and shows no station authentication, association or pairwise
handshake. This is a retained failure before WPA2 authentication, not a
failed password/key exchange and not a WPA2 STA pass.

Read-only airportd logs for the exact request narrow this failure further.
At23:34:21.003, the first live networksetup scan of the2GHz subset (including
channel9) gets APPLE80211_IOC_SCAN_REQ return0xe00002d5/kIOReturnBusy within
1.479ms. The framework then proceeds to the24-channel5GHz subset; that scan
finishes2.755473s later with zero results. The successful second subset does
not replace the rejected2GHz census where this AP actually resides. Logging
explicitly permits SSID access for Apple-signed networksetup; BSSID privacy
redaction is not proof of the previous custom-helper entitlement failure.

The current setter can return Busy for source state, BGSCAN, management timer,
closed RSN port, controller reservation or lower admission. Those actual
ownership branches were not instrumented in this run, so no specific branch
is guessed and no fence is removed. Priority for the next functional cycle:
capture exact transient scan ownership and compare reference admission/queue
behavior, then preserve this requested2GHz census through the real terminal.
Changing retry limits or treating5GHz-only completion as a successful full
scan would not fix this observed first-selection failure.

WPA2 controller/airportd SHA256:
cacabf6408d6512cef3f08825ff70ed6e07a513df191fda42820c3d6e59a17b5,
5649590cdd8bdce87792895f1c566c15c017ececbeaa88d29396a57d756ee956.
WPA3 controller SHA256:
fa4ca172ccb38d69ed069a5baec18202de8a8485ce4bdf208e7c832a8a528568.

## Native two-BSS post-S3 roaming: target success with retained losses

The first planned02->ca control stops at its source guard before any observer,
traffic or native request: automatic policy has already reachedca. Its exit1
is not an on-air failure. The subsequent pair is separately labelled and
does not force a source selection or toggle the radio.

retry-roam-return-q1 requests ca/channel9 ->02/channel13 at23:38:12UTC.
Source departure gets actual TX success, then epoch282/relay29 sends Commit
5870 and Confirm5871, accepts both peer phases and reaches target RUN with
one reassociation-success event. No peer timeout/retry or replacement exchange
intervenes. The fixed traffic streams receive248/250 guest-to-gateway and
244/250 host-to-guest; reverse maximum RTT2100.055ms. There is one observed
encapsulation drop, errors0 and no endpoint capture drops.

retry-roam-depart-q2 requests02 ->ca at23:40:09UTC. Epoch283/relay30 uses
Commit5872 and Confirm5873, accepts both peer phases and reaches target RUN
with one reassociation-success event. Fixed traffic is247/250 each direction,
with two encapsulation drops, observererrors0 and no endpoint capture drops.
Both90-second controls finish their requested-target/security gate and return0.
Their wrapper explicitly says final-target success alone is not proof of
first-attempt success; the phase observations above are checked separately.
Neither controller is a zero-loss or seamless-roaming pass.

Return trace/guest pcap/host pcap SHA256:
5f53c32f9883ce3a5990a251dba3a5c737791937574d8b16b9a9535bacff9271,
1e203f00b11cf3476f4f8b1604181506c5f2cb746a3193eae505f388f6cbbf16,
c1b1e3b5925edede9b16932db421ac22211a567ecd2503b372cdf3a18e87b4da.
Departure trace/guest pcap/host pcap SHA256:
1b0d6435ba1496ecda3d328e94c6a0064b5c37bd1f733c7f3fc74b303f6d1ec1,
626ff46af94d477fa7482224912df474afee014871ade8401d447361796f9aab,
0d926eeed4e65da1687e78cd95a429b0ac6812c37af660da642666fe9e71c5f4.

## Final restored link and immutable evidence

The separately labelled final-restored-q1 check passes20/20 each direction
on saved WPA3 LabAP172.16.66.219. It does not erase the failed post-S3 test,
failed WPA2 first selection or either lossy roam. Host AX211 returns to its
exact original profile with power saving on; the wired management route is
unchanged. Both diagnostic USB devices are restored and no dtrace/tcpdump
process remains in the owned guest. Same boot1FB8 and UUID842B are verified
at23:44:39UTC. Physical22, other QEMU and backing disks were not changed.

Frozen complete implementation/runtime evidence, including unsuccessful
controls, observers, packet captures, hostapd/DHCP logs, source/build/admission
receipts and the exact packaged kext:
`/home/dima/Projects/itlwm/aiam-sae-peer-implementation-runtime.9PiKmZ`.
All370 entries verify both in the durable copy and against the RAM original.
EVIDENCE.sha256 SHA256:
`ac15d066b7ad16f51f778b85fa1af44bf57891a33efceb60ffe835073f2da64e`.
The durable directory is read-only and frozen; do not append publication or
next-cycle evidence. RAM originalsoPlBh is also now a read/copy-only record.

Publication work is separate:
`/home/dima/Projects/itlwm/aiam-sae-peer-release-20260912.aMuDtS`.
The previous68F5 archive and exact prepublication metadata are retained there.
The guarded publisher requires the frozen manifest, each claimed gate, both
known failures, a clean pushed documentation HEAD, unchanged remote release
metadata and exact new/old archive hashes. Publication/readback is not claimed
until that transaction actually completes. The next functional cycle is the
observed2GHz public scan Busy/ownership boundary; full autonomous parity work
remains active.

## Published limited alpha and independent download verification

Runtime documentation and the archived prior release history were committed
and pushed as4fed83662756666a4a9613aeae23923314436676. The guarded publication
then completed23:46:45--23:46:56UTC on2026-09-11 (September12 local time).
All370 frozen evidence entries reverified before any external mutation, and
remote release metadata matched the captured predecessor exactly.

Release357137705 is now titled AirportItlwm Tahoe v2.4.0-alpha (477ab0af).
Asset558223857 is AirportItlwm-Tahoe-v2.4.0-alpha.kext.zip,15,693,832bytes,
GitHub digestsha256:f1fa87afb14519112deccb45e7db8122b1b3eefa99f707f2d4c101b4e51f749c.
An independent gh download byte-compares equal to the frozen tested archive;
its SHA256 matches. Publisher17676 returns0. The replaced68F5 archive remains
in the separate publication root as previous-68F5.kext.zip for rollback.

The4262-character release body leads with the failed post-S3 data gate and
WPA2 first-selection scan failure, retains lossy roaming and IWM/IWX limits,
and links the complete previous history now archived in the repository.
The old roughly65K body was not silently truncated. No rebuild, guest reload,
physical22 mutation or extra qualification claim accompanied publication.
The immutable370-file evidence root was not modified with these later receipts.

Public release: https://github.com/rdmitry0911/itlwm/releases/tag/v2.4.0-alpha
The next-cycle working root is separate:
`/dev/shm/aiam-standard-scan-busy-20260912.YEu9MQ`.
