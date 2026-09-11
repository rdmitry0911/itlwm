# STA PMF liveness and native reconnect

Status: implementation built; **new-image radio qualification is pending**.
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
All1088 imports resolve against the actual guest BootKC. Not yet installed.
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

Working evidence:
`/dev/shm/aiam-sae-peer-response-20260911.X7eeBs`, mirrored to
`/home/dima/Projects/itlwm/aiam-sae-peer-response-runtime.djZ0wV` (not frozen).
The earlier passive-discovery frozen573-file evidence remains untouched.
