# STA PMF liveness and native reconnect

Status: implementation loaded; healthy protected-query and timeout/native
recovery edges measured on RF. **Full radio qualification remains open**.
This is recovery from a lost protected association, not a claim that the
underlying AP/STA state divergence or all SAE roaming failures are fixed.

## Measured baseline

Published `ee501d78`, UUID `DCCB5E44-25AE-30BE-930F-8975BFC10A6D`, boot
`24A7472D-52A0-4F57-89F0-4EBFE68D38E6` reproduced a persistent data outage.
The exact native LabAP roam02/channel13 -> ca/channel9 completed both SAE
peer phases and Association/RUN (epoch126, relay22, source-departure ticket15).
Commit5851 and Confirm5852, with the direct-ticket high bit, were ACKed;
their peer responses were queued and accepted. This is not an SAE timeout.

The traffic result was only68/250 guest->gateway and70/250 AX211->guest.
DHCP went to INIT/no-server and169.254.122.152 while native WiFi still reported
WPA3/active. At19:17:54UTC the same boot remained in that state, DHCP elapsed
2316seconds. No off/on, new roam, reboot or kernel write was used to hide it.
Wired and USB management stayed available. The public alpha release notes
were amended18:44:42UTC to retain this failure; its asset bytes were unchanged.

Three bounded read-only observers saw successful TX descriptors but no data
RX/decryption. The last saw292 target-addressed beacons and66 unprotected
Deauths, all reason7, in30seconds. No STA SA Query or state transition occurred.
An unprotected source MAC does **not** authenticate the sender as the AP.
The shared RXMGMTPROT branch correctly rejected those frames but had no STA
challenge/recovery path. Existing STA support only answered peer queries;
the existing AP query timer/transaction owner is not a STA procedure.

## Reference and policy

The exact25C56 Apple core deauth event and WCLDeauthDisassoc exports consume
firmware-qualified events; they do not justify trusting raw unprotected
Deauth. Ready join-core-events-v2 manifest SHA256:
`4fd72ee9d171dad17de8ceba1e38e5a7c4a4230992dc563cf6772302778373e0`.

Updated-Ghidra `5995e24caa` exports in
`/tmp/aiam-roam-policy.Kj1vgE/reassoc-terminal-5995e24caa-20260911` show:
`WCLNetManager::linkDownInd` at`ffffff80020edee8` routes its default local-loss
case through `leaveNetworkCommand` at`ffffff80020ed43e`; `leaveNetwork` at
`ffffff80020ef1c0` issues the ordinary native leave carrier. No fabricated
received-deauth or beacon-loss event is needed after a local liveness timeout.

[hostap's STA SME](https://chromium.googlesource.com/external/w1.fi/cgit/hostap/+/refs/heads/main/wpa_supplicant/sme.c)
uses a completed PMF/current-BSSID/reason6-or7 trigger, protected requests,
201TU retry,1000TU maximum, ten-second procedure rate limit and acceptance of
any outstanding random transaction. The reviewed local mutable-main snapshot
SHA256 is`62ec977c2463d5730179e75ee49498b72ce41e2a5f9bf496f23f2e276a7640e9`.
This driver does not advertise OCV; no OCV support is inferred from that code.

## Implementation

- Shared STA value owner, separate from the AP query and incoming AP request
  IDs; five bounded random transaction IDs, monotonic deadlines and rate limit.
- Only established current STA, valid port, negotiated MFP and active TX/RX
  management protection can start. Unprotected Deauth/Disassoc is still dropped.
- Response consumption is attached directly to inputm's successful CCMP/PN
  verification edge, not to an untrusted raw action dispatcher. Exact peer,
  destination, current epoch and any actually submitted ID must match.
- Native [mbuf tags](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/bsd/sys/kpi_mbuf.h)
  carry the immutable query identity through the management queue. IWN/IWM/IWX
  validate it before crypto and again under the selected-BSS leaf across the
  real TX doorbell. Old queued packets cannot become a new association's query.
- Every association-epoch advance cancels the value without timer/gate calls
  under the leaf. Timers retain no node pointer; stale callbacks observe the
  current deadline, and detach closes admission before timer removal.
- A query with zero actual submissions cannot manufacture an AP timeout.
  A timer-allocation/admission failure cancels only its own generation.
- After an unanswered submitted challenge, the Tahoe controller claims the
  exact expired value in its gate and posts one copied0xd8 link-loss carrier
  with an unmapped local reason. WCL owns normal leave/reconnect. No unowned
  lower SCAN is issued after a yielding callback. This route is enabled only
  by the Tahoe controller; AP query behavior is unchanged.

## Checks and next radio gate

The actual policy and kernel-binding bodies pass ASan/UBSan/Werror, all65536
ID seeds (including duplicate/wraparound IDs), all five pending responses,
deadlines/rate limits, stale TX/epoch/timer, RX identity, detach and local
allocation failures. The same test passes on the Tahoe build guest. Existing
management-queue tests and the payload aggregate also pass.

Build source manifest (356files) SHA256:
`302cf0827ea29c6fe4e372a9a14805514eed5d83a24225ab1fe2e37abf21552f`.
Candidate UUID:`59448436-3947-3A8D-8738-402DF8FED85C`;
Mach-O SHA256:`43f24ef98f6384a62800012bad7dd5ba384e9c7290f23c0bb8dc9a0eadbaa213`.
All1088 imports resolve against the actual guest BootKC. Installed through
the five-member AuxKC transaction `activation-20260911T192210Z`; loaded in
boot `6886D4CD-BC3A-45BE-8D73-4ACC65414EF7` after a normal guest reboot.
Private AuxKC admission passes with the exact five-member set and no canonical
mutation. Aggregate log SHA256:
`cfeb296c2e80ad18da99e2cd31799d5f5c9b0a252d1d6d57f751266fbd51ae61`;
build receipt SHA256:
`4cb512fa8d2a87832c38d57b04ddb645f3d071fe1fe098ba455854a211193ae3`.

Radio plan: on an owned hostapd WPA3 fixture, inject one unprotected reason7
without removing the station and require a protected query/response with no
reconnect; then remove only the guest station using `DEAUTHENTICATE ... tx=0`
while keeping AP beacons, and require bounded query timeout, native leave,
fresh SAE/keys, DHCP and bidirectional traffic without on/off. These exact
hostap_2_10 test/tx flags were read in src/ap/ctrl_iface_ap.c. Repeat bounded
controls after real S3 and check normal WPA2/open paths; retain every failure.

## First candidate radio receipts (19:26–19:41UTC)

Owned AX211 AP `AIAM-UIF3-WPA3`, BSSID `80:e4:ba:20:ef:f9`, channel9,
SAE/group19/CCMP/PMF-required. CoreWLAN joined the real guest driver; hostapd
reported AUTH/ASSOC/AUTHORIZED/MFP, AKM8. This profile uses guest private MAC
`7a:b8:96:a9:d2:25` and DHCP `192.168.73.30`. Baseline five packets each way
passed. No GUI click is claimed for this helper-driven selection.

Fixture mistakes are retained separately: the initial forward-q1 used a
guessed `.20` address and sent no valid probe. Healthy-q1 observed idle state
only. Healthy-q2 passed `reason=7 test=0` as separate CLI arguments; this
hostapd_cli discarded the third argument, and AP log/monitor confirmed an
ordinary protected Deauth with station removal. This is not an SA Query
failure or pass. After its native recovery to LabAP, the helper explicitly
rejoined the fixture for a corrected independent control.

Healthy-q3 used the exact raw control command
`DEAUTHENTICATE 7a:b8:96:a9:d2:25 reason=7 test=0`.
The AP log confirmed both flags. At19:35:54, one plaintext reason7 triggered
generation1/epoch43/transaction27531. Snapshot and actual HAL doorbell
accepted it, and the verified CCMP/PN response edge consumed the matching
transaction about60ms later. No timeout or new association followed the
stimulus through the observer end. Traffic was10/10 each way. The trace
also contains the earlier, explicitly requested fixture join; do not count
those pre-stimulus transitions as query-driven reconnects. DTrace errors0.

Forgot-q1 used the exact raw command
`DEAUTHENTICATE 7a:b8:96:a9:d2:25 reason=7 tx=0` at19:36:57.
The AP removed only that station, without sending the command's Deauth.
Subsequent guest data elicited plaintext reason7. Generation2/epoch43 sent
five protected queries and committed all five doorbells; no verified response
arrived. Failure was claimed once about1.026s after the first submission,
and the controller posted its local-loss carrier. WCL immediately issued its
ordinary leave, then fresh SAE peer phases1/2, association and RUN about3.73s
after timeout. No radio toggle, forced join or reboot was used during recovery.

Native policy selected the already-saved **LabAP**, not the original fixture.
DHCP returned to `172.16.66.219`; recovered gateway traffic was20/20.
The original fixture-bound streams were0/35 forward and0/44 reverse after
the profile/address changed: they must not be presented as successful traffic
continuity or same-AP recovery. The first later host->guest check was20/29;
steady bidirectional1400-byte checks were20/20 forward and20/21 reverse.
Thus the persistent outage cleared in this control, but lossless recovery,
same-profile choice and the original multi-AP reproduction remain open.
Linux ping with both `-c` and `-w` can transmit beyond the requested count
while awaiting replies; subsequent harness uses an external hard deadline
and `-c` alone. Actual transmitted counts above are preserved, not normalized.

Forgot-q1's trace ended with errors0. The AP monitor capture ended when the
720-second fixture removed its own monitor, with zero capture drops; it is
not a complete independent beacon census or an untouched ciphertext capture.
The fixture restored the exact host NetworkManager profile at19:38:11UTC;
wired management and the guest boot identity were unchanged. Public release
is still `ee501d78`/DCCB with its persistent-outage warning, **not** this image.

## Sleep qualification in progress

The first real Normal Sleep lasted75s (19:43:08–19:44:23UTC), same boot and
loaded image. Its controller looked for `VM status: suspended`, but this QEMU
reports `VM status: paused (suspended)`. The bounded observer failed and
restored USB before manual `system_wakeup`; this is **not** WiFi-only wake
qualification. Immediate SSH probes failed; later DHCP/WiFi returned.

The corrected second controller exhausted its55s observation window while
QEMU still reported running; macOS logged sleep entry and then DarkWake when
USB was restored. That DarkWake later re-entered sleep. The third controller's
background AND-list kept the SSH channel open through that sleep; cleanup
performed a full wake and restored USB. Its delayed helper then saw `en2`
and exited **before requesting a new sleep**, as intended by its safety gate.
These are preserved fixture/control failures, not three successful S3 tests.
The fourth controller used a simple fully redirected background command and
a120s observation window. At19:52:58UTC it observed actual QEMU suspended
state, held it for5s and woke once at19:53:03. Before USB restoration, the
same boot and UUID returned through WiFi, DHCP was `.219`, WPA3_SAE, and
1400-byte traffic was **20/20 each way**. Its controller and WiFi-only gates
passed; only then were the exact diagnostic USB NIC and tablet restored.
This proves one strict post-S3 data-path recovery, not all sleep combinations.
The roughly105s from arming to actual S3 is retained as a separate latency
investigation; the earlier failed controls and lossy baseline are not erased.

Healthy-q3 trace SHA256:
`a4204234565f647b078186e441f935ccf22ad27c0fb977aa6be78eb1b818a71d`.
Forgot-q1 trace SHA256:
`f125aee89aafdf7ed76df906c33c82f3292fb1435726ccc56c3fd2e77976e3af`.
Updated-Ghidra exact WCL recovery subset is retained in
`reference-wcl-loss-ready/`; its source export manifest SHA256 is
`c112fed29f2a4d49fd07862be556a7e0f139b2c5e550deaa833f719a5be4b8da`.

## Same-SSID replay after S3

Three bounded80s native-WCL traces retained250-packet streams each way:

| Control | Intended transition | Actual traffic received | First attempt |
| --- | --- | --- | --- |
| sa-query-depart-q1 | LabAP02/ch13 -> ca/ch9 |224/250 F,222/250 R| SAE1/2 and target RUN; about4.9s from AUTH entry to engine start |
| sa-query-return-q1 | ca/ch9 ->02/ch13 |249/250 F,247/250 R| Target RUN, no persistent outage |
| sa-query-depart-q2 |02/ch13 ->ca/ch9|206/250 F,206/250 R| **Failed** at5s before target SAE started; native recovery first joined02, then a later automatic roam reachedca |

All observers ended with errors0. Final ca/ch9/WPA3/DHCP does not turn the
failed first attempt into a pass. The added worker probes in depart-q2 show
that its initial SAE worker was only scheduled during cancellation; the
nearly5s wait was **before** crypto-worker activation, not a measured slow
SAE calculation. Subsequent fresh-join SAE completed normally. No SA Query
trigger occurred in these three runs; they do not reproduce or close the
earlier post-RUN plaintext-reason7 persistent outage. New remaining focus:
IWN AUTH command/beacon readiness, followed by the existing SAE peer-response
loss/retry gap. The current image is not asserted to have complete WPA3 roaming.

The collected q4 power log also identifies sleep-notification delays:
`loginwindow timed out(30000 ms)` and `powerd is slow(28001 ms)`.
Thus the long S3 entry cannot currently be attributed to IWN; userland power
coordination and the laboratory login state must be distinguished from RF
recovery. The measured WiFi-only wake pass remains valid.

Working evidence:
`/dev/shm/aiam-sae-peer-response-20260911.X7eeBs`, mirrored to
`/home/dima/Projects/itlwm/aiam-sae-peer-response-runtime.djZ0wV` (not frozen).
The earlier passive-discovery frozen573-file evidence remains untouched.
