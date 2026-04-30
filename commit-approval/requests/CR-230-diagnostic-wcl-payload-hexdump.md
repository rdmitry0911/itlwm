# COMMIT REQUEST CR-230 — DIAGNOSTIC_INSTRUMENTATION (WCL connect-complete payload hex dump)

request_id: CR-230
request_stage: STAGE_1_STRUCTURAL
status: PENDING_STRUCTURAL_REVIEW
anomaly_id: post-CR-229 next-layer blocker: airportd shows isAssociated=0 despite our APPLE80211_M_WCL_CONNECT_COMPLETE_EVENT post; ni_port_valid stays 0; setCIPHER_KEY never issued
change_class: DIAGNOSTIC_INSTRUMENTATION
branch: master
head_commit: e1040cb9e929a8bf6816fde269a0f4d8a7b3742c

does_this_fix_proven_current_root_cause: NO
if NO, why this change is still correct and necessary:
  Diagnostic-only. CR-229 Stage 2 proved BR_PORT_NOT_VALID is the encap
  drop branch and that ni_port_valid never flips to 1 because the
  4-way handshake never completes. The 4-way handshake never completes
  because airportd never issues setCIPHER_KEY (no Set-key-request log).
  airportd doesn't issue setCIPHER_KEY because it sees isAssociated=0,
  even though the driver IS posting APPLE80211_M_WCL_CONNECT_COMPLETE_EVENT
  (msg=0xd5, len=0xa4) on every LINK_UP. CR-230 captures the actual
  payload bytes posted by our kext so Stage 2 evidence can compare
  against the AppleBCMWLAN reference and identify whether the issue
  is wrong payload bytes, missing framework wiring, or earlier
  state-machine gating.

reviewed_head_commit: e1040cb9e929a8bf6816fde269a0f4d8a7b3742c
artifact: commit-approval/artifacts/CR-230-diagnostic-wcl-payload-hexdump.diff
build_evidence: commit-approval/build_evidence/CR-230-build-wcl-payload-diagnostic.txt
reference_decomp: analysis/cr230_applebcmwlan_sendConnectComplete_2026_04_29.txt
kext_sha256: d672775d76ba294a4b13f0d738f953485ba4221bbb95844034966106bf0d676f
kext_uuid: 9FBB8378-12A3-3A2C-9290-8744CEC88A25
kext_size: 16299448
bootkc_undef: 886

## CLAIM_SCOPE

Pure diagnostic. Adds a hex-dump emitter and post-pre/post-done log
markers around the existing
`postTahoeWclConnectCompleteEvent()` call in
`AirportItlwm/AirportItlwmV2.cpp`. No fix, no behavioral change.

Diagnostic emits 11 hex-dump lines (16 bytes per row × 11 rows for the
0xa4-byte payload) plus 2 marker lines per LINK_UP event. Total
~13 lines per event. Producer rate is bounded by link-state
transitions (CR-229 Stage 2 shows ~10 fires per 30 seconds during
auto-join retries), so worst-case adds ~130 log lines per minute.

Three branches in one build per `feedback_diagnostic_end_to_end_criterion`:

| Branch tag        | What it captures |
|---|---|
| `CR230_POST_PRE`  | producer entry — msg code + size |
| `CR230_PAYLOAD`   | per-byte hex dump of the 0xa4-byte payload (11 rows) |
| `CR230_POST_DONE` | producer exit (after framework `postMessage` returns) |

Together these prove (a) the function executes, (b) what bytes are
on the wire, (c) the framework call returned without panic.

## DIAGNOSTIC_NEUTRALITY

- No new state mutation. The only writes are XYLog calls and the
  on-stack payload reads for hex dumping.
- Existing `postMessage` call is unchanged.
- No retry, fallback, packet substitution, queue manipulation, or
  state-machine modification introduced.
- Bounded log volume (linked to LINK_UP event rate, which is itself
  bounded by association attempts).

## REFERENCE_DECOMP

`analysis/cr230_applebcmwlan_sendConnectComplete_2026_04_29.txt`
contains the full AppleBCMWLAN connect-complete chain decompiled
from BootKC:

1. `AppleBCMWLANJoinAdapter::sendConnectComplete` @ 0xffffff800158abce
   — builds the 0xa4-byte payload from `[r15 + 0x2ac + i*0x44]`
   per-record source data (10 records of 0x44 bytes each).
2. `WCLJoinManager::connectCompleteEventHandler` @ 0xffffff800220a9a2
   — bulletin-board subscriber that validates payload != NULL and
   size == 0xa4, then tail-calls state-machine 0xffffff800211e974
   with state code 5.
3. `WCLJoinRequest::updateConnectCompleteEvent` @ 0xffffff8002232ab0
   — reads payload[0..3] (event-level status/reason) and payload[0xc..0x13]
   (records[0].status + records[0].reason as 64-bit). Bssid NOT
   read here.
4. `WCLJoinManager::handleJoinConnectComplete` @ 0xffffff800220bfa4
   — calls updateConnectCompleteEvent then validatePredicate
   at 0xffffff800220a5bc. PASS → state-machine code 6 (success);
   FAIL → sendWCLJoinDone() + state-machine code 13.

Per local struct `apple80211_wcl_connect_complete_event` at
`include/Airport/apple80211_var.h:549-554` (10 records × 0x10 bytes
each = 0xa0 + 4-byte header = 0xa4 bytes), our local
`buildTahoeWclConnectCompletePayload` zeroes the buffer and only
sets `records[0].bssid` to the AP MAC. All other fields are zero,
which corresponds to `status=0`/`reason=0` (success) in both the
event-level header and records[0]. AppleBCMWLAN's reference also
emits zero status/reason for a successful join, so the byte values
should align.

If CR-230 Stage 2 evidence shows the payload IS all-zeros except
records[0].bssid, the bytes-on-wire are correct and the issue is
upstream (post-message routing or state-machine gating). If the
hex dump shows unexpected non-zero bytes, the build's payload
construction itself is broken.

## EVIDENCE_FROM_RUNTIME (pre-fix; recap)

CR-229 Stage 2:
- BR_PORT_NOT_VALID fires 32 times (rate-capped); ic_state=0x4 RUN,
  ni_port_valid=0, ic_flags=0x32680801 RSN active.
- Upstream `_iwm_start_task ... ieee80211_encap OUTPUT_ERROR` fires
  345 times.
- airportd repeatedly logs `isAssociated=0` and continues scanning.
- No `setCIPHER_KEY` IOCTL ever observed.
- Driver-side `postTahoeWclConnectCompleteEvent` IS firing
  (`DEBUG postTahoeWclConnectCompleteEvent msg=0xd5 len=0xa4 status=0
  reason=0 bssid=50:4f:3b:cd:dd:67`) — proves the producer runs.

The gap: the framework receives msg=0xd5 (or it doesn't reach the
WCL subscriber), but airportd never transitions to "associated".
CR-230 captures the producer-side bytes; CR-231+ will instrument
the consumer side or fix the routing.

## CAUSALITY

- regression window: was always present; surfaced by CR-226+CR-229
  unblocking earlier layers.
- pinpointed divergence path: WCL connect-complete event posted by
  driver but not reflected in airportd's association state.
- why this is the right next diagnostic: confirms producer-side
  payload bytes match AppleBCMWLAN reference, narrowing the bug
  to consumer-side routing/gating.

## VERIFICATION_PERFORMED

- build: ** BUILD SUCCEEDED **
- targeted reproduction scenario: WPA2 connection attempt.
- before reproduction result: `postTahoeWclConnectCompleteEvent
  msg=0xd5 len=0xa4 status=0 reason=0 bssid=...` fires; no
  per-byte payload visibility; airportd shows `isAssociated=0`.
- after reproduction result (Stage 2 expected): same `msg=0xd5
  status=0 reason=0` plus the `CR230_PAYLOAD` hex dump showing
  the 0xa4 bytes that left our kext.
- negative checks: no new BootKC undef symbols (886, same as CR-229);
  whitespace check passes; kmutil load reaches user-approval gate.
- residual known issues not claimed fixed: the airportd
  `isAssociated=0` symptom itself (CR-230 is diagnostic-only).

## VERIFICATION

- Build : ** BUILD SUCCEEDED **
- Kext sha256 : d672775d76ba294a4b13f0d738f953485ba4221bbb95844034966106bf0d676f
- Kext UUID   : 9FBB8378-12A3-3A2C-9290-8744CEC88A25
- Kext size   : 16299448 bytes
- BootKC undef : 886 (all resolve, same as CR-229)
- Compiled tags verified by `strings`:
    `%s: DEBUG CR230_PAYLOAD off=0x%02zx ...`
    `%s: DEBUG CR230_POST_PRE msg=0x%x size=0x%zx`
    `%s: DEBUG CR230_POST_DONE msg=0x%x`
- kmutil load : passes vtable check.
- `git diff --cached --check`: passes.
- HEAD : `e1040cb9e929a8bf6816fde269a0f4d8a7b3742c` (post-CR-229 commit).

## CHANGE_FOOTPRINT

- `AirportItlwm/AirportItlwmV2.cpp`:
  - Add `cr230LogPayloadHex()` helper (file-scope static, 11 XYLog
    calls in a fixed-bound loop).
  - Insert `cr230LogPayloadHex(&payload)` + `CR230_POST_PRE`/
    `CR230_POST_DONE` markers in
    `postTahoeWclConnectCompleteEvent()`.
  - No control-flow changes.
- No header file changes.
- No build-script changes.
- `analysis/cr230_applebcmwlan_sendConnectComplete_2026_04_29.txt`:
  NEW — reference decomp documentation.

## RESIDUAL_UNCERTAINTY

- The next-layer fix path (CR-231+) is unknown until Stage 2 evidence
  arrives. Possibilities:
  - payload bytes are correct → routing issue in IO80211Family or
    PostOffice → instrument framework path or use different
    notification mechanism.
  - state-machine gating earlier in the WCL flow → instrument
    `WCLJoinAdapter` or `WCLJoinRequest` initialization.
  - airportd-side gating → check apple80211 IOCTL surface for
    something we haven't implemented yet.

## FORBIDDEN_ALTERNATIVES_CONSIDERED_AND_REJECTED

- heuristic timing : NO
- fallback path : NO (no upstream return-value change)
- masking/suppression : NO
- forced callback / state / success : NO
- forced sync / flush / barrier : NO
- retry / reorder / poll loop : NO
- behavior-changing diagnostic : NO

## STAGE_2_INTENT

If Stage 1 approves, Stage 2 will collect runtime evidence on the
same WPA2 connection attempt:

```
DEBUG CR230_POST_PRE msg=0xd5 size=0xa4
DEBUG CR230_PAYLOAD off=0x00 NN NN NN NN ...
DEBUG CR230_PAYLOAD off=0x10 NN NN NN NN ...
... (11 rows, total 0xa4 bytes)
DEBUG CR230_POST_DONE msg=0xd5
```

The `CR230_PAYLOAD off=0x00 ...` row will show:
- bytes 0..1 (event status, 16-bit LE)
- bytes 2..3 (event reason, 16-bit LE)
- bytes 4..9 (records[0].bssid)
- bytes 10..11 (records[0].reserved)
- bytes 12..15 (records[0].status, 32-bit LE)

If status/reason fields are zero and bssid matches `ic_bss->ni_bssid`,
the producer payload is correct and CR-231 must focus on routing/gating.
