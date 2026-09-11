# SAE roam: source departure and return-to-BSS comeback

## Reproduction on the published image

After the separate [scan/peer-PS controls](TAHOE_UI_SCAN_LATENCY_ATTRIBUTION_20260911.md),
two ordinary framework roam requests ran on the same disposable guest boot
`A2F0698F-FF16-449D-AC40-E3E4117D4CF9`, loaded UUID
`BE0C8CE1-4924-39F2-BE2B-5929C1039113`. No driver or firmware policy change
was made. The first request at 15:00:40 UTC moved from `9a:fb:5d:97:a9:02`
(channel 13) to `82:c3:97:84:51:ca` (channel 9). At 15:02:18 the second request
returned to the first BSS. Both final snapshots show the requested BSS and
WPA3 SAE, with the same guest address `172.16.66.219`.

Traffic is 250 ICMP packets at 200-ms spacing with 1400-byte payload per
direction. Forward is guest-to-**gateway** `172.16.66.1`; reverse is external
AX211 `172.16.66.226` to guest. It is not the peer-to-peer forward path from
the preceding latency controls. Host power save remains at its ordinary on
setting throughout this pair.

| Run | Received F/R | Peer comeback | Actual comeback retry | Encap drops |
| --- | --- | --- | --- | --- |
| departure q1, 02 → ca | 247/250, 247/250 | none observed | not exercised | 3 |
| return q2, ca → 02 | 242/250, 242/250 | status 30, 1000 TU | 1,093,481,121 ns | 0 |

Return q2 preserves the published minimum-deadline fix: actual retry is later
than the AP's 1,024,000,000-ns minimum. No early retry occurs. The difference
between the two packet counts is an observation, not a proof that every extra
lost packet is caused by comeback.

Both 70-second observers terminate with zero errors. The compiled probes
observe common management send (including deauth/disassoc), public
DISASSOCIATE, WCL_REASSOC, SA Query receive/build, and packed single-frame
management TX status. Neither run contains a public DISASSOCIATE entry, a
deauth/disassoc send, or a deauth/disassoc firmware TX completion. Both contain
successful SAE authentication TX and association-request TX. Return q2 has no
observed SA Query request at the net80211 receive entry; the status-30 response
itself is observed. No monitor-mode RF capture or AP-side station-table proof
is claimed.

The exact unchanged `framework-roam` helper SHA-256 is
`729509f1b5c809f24201eec897d5122bd59b13964e08a9459c3a021798daf8ca`.
The helper's successful synchronous return is not used as proof of connection;
the real state/management events, final identity and traffic are retained.

## Source localization

The explicit WCL same-ESS selection in `ieee80211_node.c` handles pure SAE
before its generic `ieee80211_node_defer_bss_switch` branch. It calls
`ic_sae_wcl_roam_start`, whose current IWN implementation immediately enters
`iwn_sae_targeted_roam_start(..., false)`.

That helper validates and copies the active driver-resident credential,
prepares the public RUN retarget, stages the target credential, and invokes
`ieee80211_node_join_bss`. It emits no source deauthentication. Node join
retires hardware TX agreements, starts replacement of the association epoch,
copies the new BSS, and enters AUTH. Its existing comment correctly calls the
AMPDU teardown local rather than an on-air leave. Thus the missing departure
in these traces is consistent with the executed production branch, not merely
a search for an absent log string.

The BTM path calls the same targeted helper with `consume_wnm=true`, but only
after its separate source BTM-response/deauth descriptor fence. It must not
gain a second departure. The legacy generic switch sends deauth but relies on
the historical node-unreference callback and arms it after send; its own
comment leaves full TX drain/terminal-before-arm handling open. It is not a
safe exact-descriptor fence to copy into the SAE branch.

## Reference and causal limit

The updated 5995e24caa reference exports used earlier in this work contain
Apple `sendReassocCommandLegacy` at `ffffff8001524108` and V3 at
`ffffff800152445c`: the lower reassociation command is submitted while
associated. These upper host-driver functions do not expose Broadcom firmware's
complete departure or key-cache behavior; they do not prove an Apple on-air
deauth sequence.

The available upstream hostapd 2.11 `check_sa_query()` rejects temporarily
when a station remains associated, MFP-enabled and authorized, and its SA
Query has not timed out (with the FT reassociation exception). This makes
retained old-AP state a concrete explanation to test for the observed return
comeback. The live AP's exact station state has not yet been read. A read-only
SSH attempt to `172.16.66.100` stopped at strict unknown-host-key verification;
no verification bypass or AP mutation was attempted.

## Next implementation boundary

The next candidate should serialize the non-BTM pure-SAE source leave behind
one **exact protected management descriptor**, before destroying old keys or
changing channel. It must not merely insert `SEND_MGMT` immediately before
node replacement and assume the queued frame has left the hardware.

Required boundaries before that candidate can qualify:

- Capture source association epoch, WCL reassociation serial and exact target
  as values; stale/duplicate completion cannot advance a successor.
- Arm before submission and correlate the actual descriptor, including an
  immediate completion, rejection before doorbell, failed TX, cancellation,
  radio off and sleep. Avoid a global node-refcount completion condition.
- Preserve the private credential transaction and old protected key context
  until the intended leave reaches its terminal. Do not add a secret-bearing
  second public owner or a direct unprotected deauth shortcut.
- Keep data gated during the actual departure/replacement interval; avoid
  holding a leaf lock while waiting or invoking reentrant HAL/controller work.
- Keep the already-fenced BTM path unchanged. IWM/IWX capability/fallback
  behavior must remain explicit; this IWN RF pair does not qualify them.
- Validate the production primitive with stale/early/failure tests, then build,
  activate and repeat actual A→B→A. Observe source departure completion,
  AP comeback response, DHCP/traffic and off/on plus real-S3 recovery before
  calling this functional surface closed or replacing the release.

The reproduction above predates the production candidate and qualification
sections below; it makes no claim for their newer artifact.

## Evidence

Root: `/home/dima/Projects/itlwm/aiam-roam-departure-runtime.GDGeMA`.
The corrected 34-file manifest `evidence-pair-q3.sha256` verifies completely:
`cf026e65500bc106210d50f9f4e09b15a2c36bdb3dc6ee7a85f7175439e5fe7e`.
The first q2 manifest accidentally included itself; its failed receipt is
retained and included unchanged in q3, not presented as a valid manifest.

Guest/host capture counts are 1005/507 for departure and 984/500 for return;
the filters include ARP, so these are not counts of successful echo replies.
All four report zero kernel drops. Controllers and observers are terminal;
the guest is back on BSS 02/channel 13 with WPA3 and its original address.
No AP fixture, physical `.22`, QEMU lifecycle or disk-image operation was used.

## Candidate: exact protected source departure

The following candidate was committed as `986e030be8801e6d2aad74c5bb7b3eb4c1b517d2`.
The non-BTM IWN hook now builds one ordinary protected deauth using
the shared management header policy and the normal software-PMF TX path. It
does not replace driver-resident SAE with a userspace exchange. The existing
BTM response/deauth fence and IWM/IWX implementations are unchanged.

A value-only ticket carries source epoch, credential generation, WCL owner
serial, join/reassoc sequence counters, source/target BSSID and STA address.
It is armed before submission, attached to the exact descriptor before its
doorbell, and consumed only after that descriptor and its node reference
retire. Scheduler publication takes lifecycle -> credential -> selected-BSS
locks, repeats the current ACTIVE source/public identity checks, then releases
all locks before any callback. No second password owner is introduced.

The direct producer explicitly checks queue pressure and slot ownership. Data
is held during source departure; ordinary hardware stop cancels the pending
value without resetting its monotonic ticket or erasing the existing
sleep/reconnect ACTIVE credential. Stale/duplicate completions cannot advance
a successor. A terminal TX failure may still permit roaming to an available
target; it is not recorded as successful delivery to the source AP. A failed
target start after departure publishes an owned failure and requests recovery
only if no reentrant successor was admitted.

Local candidate checks on 2026-09-11:

- 34 complete management-queue/header/builder cases, including the production
  protected deauth body, invalid PMF state and allocation/prepend failures.
- 27 cases execute all six new departure orchestration functions with explicit
  hardware/frame-allocation/target-join boundaries. They cover pre-doorbell
  cancellation, immediate completion, TX failure, stale source/owner/credential,
  stop and reentrant recovery; the value primitive also rejects mismatched
  identities, duplicate completion and ticket wrap.
- The existing complete IWN TX-completion fixture now checks exact departure
  identity capture before clear and callback after packet/node/slot retirement.
- `test_payload_builders.sh` passes, including existing AP, PMF key lifetime,
  deadline, IWM/IWX retirement, state-transition and firmware-owner fixtures.
  The first candidate run failed because its IWN descriptor double lacked the
  new field/callback; this was repaired and a real retirement assertion added.
- SAE transport, software PMF, WCL credential, WCL roam-scan and queue-topology
  contracts pass. The standalone DVM AMPDU contract had a pre-existing stale
  three-argument stop signature (HEAD already passes `&retired`); its expected
  signature is updated without changing production AMPDU behavior.

These are sanitizer/source gates, not simulated RF success. The separate
actual-image observations follow.

## Actual Tahoe image and source-departure observations

The complete AP-capable Tahoe build passed and resolved all 1085 external
symbols against BootKC. The 354-file production manifest was checked before
and after the build. The macOS production-function fixtures also passed.

- Source manifest SHA-256:
  `65b657d797b571541bc1445039b67a2df111183a7a7f8cf879e9a8b7ca9ce6ff`.
- Build log SHA-256:
  `6cb7bb4c682d498980ec9399b04bd956e65b03f38df20fc565c7291efc246b10`.
- Mach-O UUID: `ADABFCB9-0EE6-3FE0-AD81-01CDF616226B`.
- Mach-O SHA-256:
  `043c6e748cebe4ce2390df82e3428cb48121add50b4eddc0cf7d866640a21768`.
- Guest boot: `4CBD3236-33BE-48B8-8051-05DC34A550BB`.

Private preflight and the existing transactional AuxKC activation completed.
The first reboot wrapper stopped on a non-root checksum permission error
before reboot; the corrected sudo-guarded wrapper performed one normal lab
guest reboot. No live kext unload or physical `.22` operation was used.

The loaded image automatically joined WPA3 and later moved from BSS 02 to ca
through its own WCL path, with departure ticket 1. The first requested test
therefore stopped at its source-BSSID guard, before observers or another roam
request. This is not a failed RF exchange or a controlled traffic result.

| Actual run | Target result | Departure receipt | Traffic F/R | Comeback |
| --- | --- | --- | --- | --- |
| return q2, ca -> 02 | NO_ELIGIBLE_TARGET; stayed ca | not submitted | 250/250, 250/250 | target not attempted |
| return q3, ca -> 02 | NO_ELIGIBLE_TARGET; stayed ca | not submitted | 250/250, 250/250 | target not attempted |
| selection q1, ca -> 02 | target RUN | ticket 2, status 1, ACK retries 0 | not sampled as a 250-packet run | none observed |
| depart q4, 02 -> ca | target RUN | ticket 3, status 1, ACK retries 0 | 249/250, 248/250 | none observed |
| return q5, ca -> 02 | NO_ELIGIBLE_TARGET; stayed ca | not submitted | 250/250, 250/250 | target not attempted |
| selection q2, ca -> 02 | target RUN | ticket 4, status 1, ACK retries 1 | not sampled as a 250-packet run | none observed |

F/R here retains the reproduction's guest-to-gateway / host-to-guest paths,
250 packets at 200 ms with 1400-byte payloads. The diagnostic selection runs
last 35 seconds; the traffic runs use 70-second observers. Every observer
reported zero errors. Depart q4 recorded **one encap drop**, not zero.

For each observed successful transition, the actual protected-deauth builder
and exact descriptor publication precede firmware deauth TX status and the
departure terminal. Only after that terminal does targeted SAE retarget begin,
followed by real authentication/association and target RUN. No status-30
comeback appears in these successful samples. This closes the **missing
non-BTM IWN protected source-departure fence**, not all return-to-BSS behavior,
AP station-table cleanup or seamless roaming.

Two observer/harness defects are retained explicitly. Return q2 originally
returned zero because it lacked a final target-identity assertion, despite
not roaming; the assertion was added before q3. The two diagnostic selection
logs printed a bool return as a full int, including unspecified upper bits
(`196134401` / `196115969`); the low byte is 1. Later observers cast to uint8_t.
Neither defect converts a NO_ELIGIBLE_TARGET run into success.

Fresh host scanning observed target 02 on channel 13 at -34 dBm before q3.
The failed driver censuses logged 24-28 nodes and zero match-BSS rejections;
that alone does not distinguish missing target discovery, the ni_fails gate,
or an earlier WNM/WCL filter. Exact-image live-node probes are the next
diagnostic boundary. Do not infer that the AP vanished or reset failure
counters speculatively. Updated reference `WCLRoamManager::roamScanEnd`
(`ffffff8002106052`) exposes candidate BSSID/RSSI/channel/age/load and current
RSSI publication; it does not itself establish an automatic retry policy.

## Off/on and real sleep: recovery, not zero-loss qualification

Native `networksetup` WPA3 off/on withdrew carrier and IPv4, recovered the
saved profile and passed 20/20 1400-byte packets in each direction. This was
not a mouse-driven GUI test.

Real Normal Sleep ran from 16:06:11 to 16:09:03 UTC, 172 seconds, with QEMU
independently observed suspended. Diagnostic USB Ethernet and tablet were
absent. The same boot recovered WPA3 DHCP at 16:09:11, eight seconds after
wake. Direct Wi-Fi SSH and traffic were tested before USB management returned.
The two strict zero-loss checks **failed**: q1 received 20/20 forward and
19/20 reverse (missing reverse sequence 8); q2 received 19/20 forward and
20/20 reverse. Peak RTT was 251.179 ms. These show a working recovered path,
not lossless recovery or proof that all loss originated in the driver.

The existing WindowServer 30-second sleep-acknowledgement timeout was recorded.
Post-S3 GUI operation and already-active AP client continuity through sleep
are not qualified by this test. No radio toggle was needed after wake.

## Native AP regression after S3

Before packaging/publication, native Internet Sharing was exercised after
that same real sleep. An external AX211 associated and obtained DHCP from
the guest in WPA3, WPA2 and open modes. WPA3 independently reported SAE,
mandatory PMF (`pmf=2`) and BIP. WPA3 q2, WPA2 q1 and open q2 each passed
20/20 client-to-AP packets, 10/10 AP-to-client after cold ARP, and an exact
118-byte HTTP response routed through the guest's USB/NAT upstream. This is
not proof of concurrent STA Wi-Fi backhaul. Each ordinary sharing stop
retired its bridge and restored 10/10 STA-to-gateway packets; no reboot,
radio toggle or daemon restart separated these mode checks.

The first WPA3 AP run completed security, DHCP and both traffic checks but
failed HTTP because the host loopback test server was absent. After starting
a bounded server, q2 completed the whole path. Open q1 received all 20 replies
plus one duplicate (sequence 10); the historical exact-string parser stopped
before HTTP on ping's additional duplicate field. A task-local parser now
requires all unique replies and reports duplicates separately. Open q2 had
20/20 without duplicates and completed HTTP/stop. The q1 duplicate is retained
as an unresolved observation, not dismissed as a parser problem. Both local
servers were bounded; the second was stopped by its exact owned PID when the
AP matrix finished. Host management remained on wired Ethernet.

These native-producer AP checks are not a new mouse-driven GUI matrix or
already-active AP/client sleep-continuity test.

## Packaging identity

The frozen preflight bundle, installed bundle and full extracted ZIP bundle
compare equal; packaging did not rebuild the binary.

- ZIP SHA-256:
  `fddbebf1e662ffea7f98bf27e08c481648f7109f681034d27681ed5bf1109fac`.
- ZIP size: 15,684,441 bytes.

Published at 16:38 UTC to
[`v2.4.0-alpha`](https://github.com/rdmitry0911/itlwm/releases/tag/v2.4.0-alpha),
title `AirportItlwm Tahoe v2.4.0-alpha (986e030b)`, asset ID `557611652`.
API digest/size and a complete independent download were verified; the
download byte-compares equal to the tested archive. Prior release notes were
preserved as explicitly historical sections, including their limitations.
Publication receipts are separate from the frozen qualification manifest:
`/home/dima/Projects/itlwm/aiam-sae-departure-release-20260911.GNXEkK`.

Runtime evidence is frozen under
`/home/dima/Projects/itlwm/aiam-roam-departure-candidate-runtime.VktxXM`.
All 280 entries of `evidence-qualified-q1.sha256` verify; manifest SHA-256:
`f3106c820bffd438911c0d0db25b192f31dc8bda5a4eec7b881923a942e04d8d`.
This includes the packaged image, failed runs/harness receipts, detailed
post-S3 candidate probes and all original AP regression captures.
The exact source-departure correction is IWN-specific. Common header-builder
and IWM/IWX build/fixture coverage is not new IWM/IWX RF qualification.

## Next-layer localization on the same post-S3 image

After AP regression, the first planned 02 -> ca runner stopped before its
observer/request because the guest was already on ca. Post-S3 return q7 then
requested ca -> 02 at 16:28:19 UTC and reproduced NO_ELIGIBLE_TARGET. A bounded
read-only observer used node offsets derived from this exact ADAB binary:
`node_cmp` proves MAC at 0x35, adjacent BSSID is 0x3b, and `choose_bss` loads
RSSI at 0x34 and ni_fails at 0xc18. Its actual RB_NEXT entry visits each live
node before any selection-time free; candidate returns carry address values,
not a dereferenced retired pointer.

This failed census contained 23 nodes, **all ni_fails=0**, and no target
`9a:fb:5d:97:a9:02`. The observed source ca was correctly excluded by the WCL
filter. Both physical band submissions and the exact terminal were observed;
there were zero observer errors/encap drops. The guest remained on ca with
249/250 forward and 250/250 reverse traffic. After that run finished, a fresh
host scan reported target 02 at 2472 MHz / NM signal 85. This establishes a
missing target in that guest census, not a ni_fails rejection, and does not
prove that every earlier missing-target run has the same cause. No target
SAE exchange or protected source departure was attempted in q7.

Post-S3 q8 found the target and reached RUN after departure ticket 12, with
248/250 traffic in both directions and no comeback. Its new packed command
observer incorrectly read 32-bit channel flags at a two-byte-aligned array
position, producing 13 DTrace invalid-alignment faults; the full runner
correctly failed the observer gate. These are observer faults, not a kernel
panic or a valid complete channel-plan measurement. The corrected q5 probe
decodes channel entries byte by byte (SHA-256
`3e5b1b290bb00999452df18c4e18680a1194933b009640a52c9d7026ba9c38f5`).

With that corrected observer, post-S3 q9 moved 02 -> ca using departure
ticket 13, status 1 / ACK retries 0, then real SAE/RUN. It received 249/250
forward and 247/250 reverse; zero observer errors, two encap drops, no comeback.
The actual first firmware command contains 13 2.4-GHz channels: channel 13
remains passive (flags 0x2), active dwell 36 ms, passive dwell 85 ms; channel 9
is active (flags 0x3). The following 24-channel 5-GHz command has passive
dwell 85 ms. Both retain max_out 112640 us and pause encoding 0x402800.
Thus channel 13 is not omitted from the physical plan. A passive 85-ms listen
is shorter than a 100-TU beacon period; this is a concrete timing hypothesis,
not yet an RF proof or authorization to exceed the serving-BSS budget.

Post-S3 q10 repeated ca -> 02 with this corrected command/census observer.
It recorded channel 13 in the physical plan with the same passive flag and
85-ms dwell, but the 27-node fresh census again lacked target 02 (all observed
ni_fails were zero). No source departure was submitted. It remained on ca
and passed 250/250 in both traffic directions, with zero observer errors and
zero encap drops. This is a successful preservation of the source link during
a **failed roam**, not a successful target connection.
