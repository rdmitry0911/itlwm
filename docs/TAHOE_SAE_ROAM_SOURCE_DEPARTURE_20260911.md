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

No production change, new build or new release is claimed by this report.

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
