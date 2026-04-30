# COMMIT REQUEST CR-232 — DIAGNOSTIC_INSTRUMENTATION (end-to-end WCL connect-complete branch coverage, OOB-fix)

request_id: CR-232
request_stage: STAGE_1_STRUCTURAL
status: PENDING_STRUCTURAL_REVIEW
anomaly_id: post-CR-229 next-layer blocker: airportd shows isAssociated=0 despite our APPLE80211_M_WCL_CONNECT_COMPLETE_EVENT post; ni_port_valid stays 0; setCIPHER_KEY never issued
change_class: DIAGNOSTIC_INSTRUMENTATION
branch: master
head_commit: e1040cb9e929a8bf6816fde269a0f4d8a7b3742c

does_this_fix_proven_current_root_cause: NO
if NO, why this change is still correct and necessary:
  Diagnostic-only. Per `feedback_diagnostic_end_to_end_criterion`,
  every DIAGNOSTIC_INSTRUMENTATION CR must instrument ALL branches of
  the hypothesis to final points in one build. CR-232 preserves
  CR-231's end-to-end branch coverage and fixes the CR-231 reviewer's
  kernel-safety blocker (out-of-bounds payload reads on the final row
  of the hex dump).

prior_rejections:
  - commit-approval/decisions/COMMIT_DECISION_CR-230.md — separate
    earlier diagnostic with branches 1-3 only; rejected for partial
    coverage. Files stay in worktree (immutable).
  - commit-approval/decisions/COMMIT_DECISION_CR-231.md — first
    end-to-end attempt covering branches 1-7; rejected for kernel
    safety blocker `cr230LogPayloadHex()` reading past payload on
    the final row. Files stay in worktree (immutable).

CR-232 supersedes CR-231 as the corrected end-to-end diagnostic. The
ONLY substantive change vs CR-231 is the payload-hex-dumper safety
fix; all other instrumentation tags, sites, and rate-limits are
preserved verbatim.

reviewed_head_commit: e1040cb9e929a8bf6816fde269a0f4d8a7b3742c
artifact: commit-approval/artifacts/CR-232-end-to-end-wcl-diagnostic-oob-fix.diff
build_evidence: commit-approval/build_evidence/CR-232-build-end-to-end-wcl-diagnostic-oob-fix.txt
reference_decomp: analysis/cr230_applebcmwlan_sendConnectComplete_2026_04_29.txt
kext_sha256: 08f5edbeb5236d279db46e5907f241a5f31b848c5604ac88a9fb947501b96d71
kext_uuid: 5C34B0D9-5E82-353C-8678-EF6442DF1141
kext_size: 16304064
bootkc_undef: 886

## CHANGES_VS_CR-231

CR-232 source code is byte-different from CR-231 in exactly one
function. All other instrumentation is preserved verbatim.

The replaced function:

```c
// CR-232 safe replacement of CR-230's cr230LogPayloadHex().
// Every dereference bounded by `idx < sizeof(*payload)`. OOB indices
// pad with 0 in the format-arg list so the log line keeps a fixed
// 16-byte-per-row visual structure.
static void
cr232LogPayloadHex(const apple80211_wcl_connect_complete_event *payload)
{
    const uint8_t *p = reinterpret_cast<const uint8_t *>(payload);
    const size_t plen = sizeof(*payload);  // 0xa4
    for (size_t row = 0; row < plen; row += 16) {
        #define BYTE_AT(off) (((row + (off)) < plen) ? p[row + (off)] : (uint8_t)0)
        XYLog("DEBUG CR230_PAYLOAD off=0x%02zx %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n",
              row,
              BYTE_AT(0),  BYTE_AT(1),  BYTE_AT(2),  BYTE_AT(3),
              BYTE_AT(4),  BYTE_AT(5),  BYTE_AT(6),  BYTE_AT(7),
              BYTE_AT(8),  BYTE_AT(9),  BYTE_AT(10), BYTE_AT(11),
              BYTE_AT(12), BYTE_AT(13), BYTE_AT(14), BYTE_AT(15));
        #undef BYTE_AT
    }
}
```

The unsafe `cr230LogPayloadHex` symbol is no longer present in the
binary (replaced wholesale, not coexisting). The `CR230_PAYLOAD`
log tag is preserved so Stage 2 evidence aligns with prior
expectations.

## CLAIM_SCOPE

Pure diagnostic. End-to-end coverage of the WCL connect-complete
hypothesis at every checkpoint our kext can observe between (a) the
producer entry into `postTahoeWclConnectCompleteEvent` and (b) the
airportd-facing apple80211 IOCTL responses. No fix, no behavioral
change. Same as CR-231 except for the payload hex dumper safety fix.

The hypothesis is: "the driver's `APPLE80211_M_WCL_CONNECT_COMPLETE_EVENT`
post does not propagate into airportd `isAssociated=1`, so airportd
never starts the 4-way handshake, so `setCIPHER_KEY` is never issued,
so `ni_port_valid` stays 0, so non-EAPOL TX drops at the
`ENCAP_BR_PORT_NOT_VALID` gate (CR-229 Stage 2 evidence)."

| # | Branch | Tag | Site | Rate-limit |
|---|---|---|---|---|
| 1 | Producer entry state (ic_state, bss, flags, bssid) | `CR231_PRODUCER_STATE` | postTahoeWclConnectCompleteEvent entry | 16/run |
| 2 | Payload bytes on-the-wire (safe; OOB-fixed) | `CR230_PAYLOAD` | postTahoeWclConnectCompleteEvent | 11 rows × link-up rate |
| 3 | postMessage call markers | `CR230_POST_PRE`/`CR230_POST_DONE` | postTahoeWclConnectCompleteEvent | unbounded by link-up rate |
| 4 | Every IOCTL airportd issues | `CR231_IOCTL` | processApple80211Ioctl entry | 16/run |
| 5 | Every event posted via workloop | `CR231_POST_GATED` | postMessageGated | 16/run |
| 6 | (covered by 1) | — | — | — |
| 7 | IOREG state at LINK_UP | `CR231_IOREG_STATE` | postTahoeWclConnectCompleteEvent | 16/run |

End-to-end coverage criterion satisfied: all branches in one build,
bounded log volume, no behavioral side effects, payload reads bounded
by `sizeof(*payload)`.

## DIAGNOSTIC_NEUTRALITY

- All emitters are XYLog calls + reads of existing state.
- Every payload-byte access is guarded by `idx < sizeof(*payload)`;
  no kernel-context out-of-bounds reads.
- Existing control flow unchanged at every instrumentation site.
- No retry, fallback, packet substitution, queue manipulation, or
  state-machine modification introduced.
- Per-call rate-limit at 16 emissions for CR231_* tags.
- `CR230_PAYLOAD` rows are unbounded (intrinsic link-up rate; ~10/30s
  during stalled handshake retries).

## REFERENCE_DECOMP

`analysis/cr230_applebcmwlan_sendConnectComplete_2026_04_29.txt`
documents the AppleBCMWLAN connect-complete chain (sendConnectComplete
@ BootKC `0xffffff800158abce` → bulletinBoard subscriber
`connectCompleteEventHandler` @ `0xffffff800220a9a2` →
`updateConnectCompleteEvent` @ `0xffffff8002232ab0` reading payload
bytes 0..3 and 0xc..0x13 → `handleJoinConnectComplete` @
`0xffffff800220bfa4` validatePredicate → state-machine code 6 success
or 13 failure). Unchanged from CR-230/CR-231.

## EVIDENCE_FROM_RUNTIME (pre-fix; recap)

CR-229 Stage 2:
- `ENCAP_BR_PORT_NOT_VALID` fires 32 times (rate-capped); ic_state=0x4
  (RUN), ni_port_valid=0, ic_flags=0x32680801 (RSN active).
- airportd repeatedly logs `isAssociated=0` and continues scanning.
- No `setCIPHER_KEY` IOCTL ever observed.
- Driver-side `postTahoeWclConnectCompleteEvent` IS firing
  (`DEBUG postTahoeWclConnectCompleteEvent msg=0xd5 len=0xa4 status=0
  reason=0 bssid=50:4f:3b:cd:dd:67`) — proves the producer runs.

## CAUSALITY

- regression window: was always present; surfaced by CR-226+CR-229
  unblocking earlier layers.
- pinpointed divergence path: WCL connect-complete event posted by
  driver but not reflected in airportd's association state.
- why this is the right next diagnostic: covers the producer→framework
  →airportd chain that our kext can observe, with the kernel-safety
  blocker fixed so runtime collection is authorized.

## VERIFICATION_PERFORMED

- build: ** BUILD SUCCEEDED **
- targeted reproduction scenario: WPA2 connection attempt to CONTROL_STA_NETWORK AP.
- before reproduction result: CR-229 Stage 2 baseline showed BR_PORT_NOT_VALID.
- after reproduction result (Stage 2 expected): all 7 branches emit
  per-site rate-limited log lines; Stage 2 evidence will identify
  whether airportd polls expected IOCTLs and whether our responses
  match what AppleBCMWLAN returns.
- negative checks: no new BootKC undef symbols (886); whitespace check
  passes; kmutil load reaches user-approval gate.
- residual known issues not claimed fixed: the `isAssociated=0`
  symptom (CR-232 is diagnostic-only).

## VERIFICATION

- Build : ** BUILD SUCCEEDED **
- Kext sha256 : 08f5edbeb5236d279db46e5907f241a5f31b848c5604ac88a9fb947501b96d71
- Kext UUID   : 5C34B0D9-5E82-353C-8678-EF6442DF1141
- Kext size   : 16304064 bytes
- BootKC undef : 886 (all resolve, same as CR-229..CR-231)
- nm output: only `cr232LogPayloadHex` symbol present; no
  `cr230LogPayloadHex`. Verified safe: every byte read bounded by
  `sizeof(*payload)`.
- Compiled tags verified by `strings`:
  - `CR230_PAYLOAD`, `CR230_POST_PRE`, `CR230_POST_DONE`
  - `CR231_PRODUCER_STATE`, `CR231_IOREG_STATE`,
    `CR231_POST_GATED`, `CR231_IOCTL`
- kmutil load : passes vtable check.
- `git diff --cached --check`: passes.
- HEAD : `e1040cb9e929a8bf6816fde269a0f4d8a7b3742c` (post-CR-229).

## CHANGE_FOOTPRINT vs HEAD

- `AirportItlwm/AirportItlwmV2.cpp`:
  - Replace `cr230LogPayloadHex()` with safe `cr232LogPayloadHex()`
    using the bounded `BYTE_AT(off)` macro.
  - Update the call site to invoke `cr232LogPayloadHex()`.
  - Add `CR231_LOG_LIMIT` macro + `CR231_LOG()` rate-limited emitter
    macro at file scope.
  - Add `CR231_PRODUCER_STATE` + `CR231_IOREG_STATE` logs around the
    existing CR230 hex dump.
  - Add `CR231_POST_GATED` log inside `postMessageGated`.
- `AirportItlwm/AirportItlwmSkywalkInterface.cpp`:
  - Add `CR231_IOCTL_LOG_LIMIT` macro + `CR231_IOCTL_LOG()` emitter
    at file scope.
  - Add `CR231_IOCTL` log at processApple80211Ioctl entry.
- No header file changes.
- No build-script changes.

CR-230 and CR-231 request/build_evidence/artifact files remain in the
worktree unchanged (immutable per `feedback_no_edit_submitted_cr` and
`feedback_no_delete_submitted_requests`).

## RESIDUAL_UNCERTAINTY

- The 16-cap on CR231_* tags may hide low-rate or late-firing events.
  Stage 2 evidence will show which tags hit the cap; if any branch
  needs more observations, CR-233 can raise the limit selectively.
- BootKC-resident framework code (the bulletin board subscriber, the
  WCL state machine) cannot be instrumented from our kext. Branches
  4-5 cover the airportd-facing apple80211 IOCTL surface and the
  kext-side outbound events, which is the maximum visibility
  available without DTrace or a kernel debugger.

## FORBIDDEN_ALTERNATIVES_CONSIDERED_AND_REJECTED

- heuristic timing : NO
- fallback path : NO (no upstream return-value change)
- masking/suppression : NO
- forced callback / state / success : NO
- forced sync / flush / barrier : NO
- retry / reorder / poll loop : NO
- behavior-changing diagnostic : NO
- unsafe diagnostic memory read : NO (CR-231 reviewer's
  blocker — fixed in this CR; every payload byte read is now
  bounded by `sizeof(*payload)`)

## SUPERSEDES

supersedes:
- CR-231 (rejected for unsafe payload memory read on final row).
- CR-230 (rejected for partial branch coverage).

## STAGE_2_INTENT

If Stage 1 approves, Stage 2 will collect runtime evidence on a
WPA2 connection attempt. Expected publishable signals:

```
DEBUG CR231_PRODUCER_STATE fNetIf=1 ic=1 ic_state=4 ic_bss=1 ic_flags=0x32680801 bssid=50:4f:3b:cd:dd:67
DEBUG CR230_POST_PRE msg=0xd5 size=0xa4
DEBUG CR230_PAYLOAD off=0x00 ... (11 rows of payload bytes; final row has real bytes 0xa0..0xa3 then 12 zero pads)
DEBUG CR231_IOREG_STATE CoreWiFiDriverReadyKey=true fNetIf=1
DEBUG CR230_POST_DONE msg=0xd5
DEBUG CR231_POST_GATED msg=0xN len=0xN ic_state=4   (every event posted via workloop)
DEBUG CR231_IOCTL cmd=0x... req_type=N req_data=N    (every IOCTL airportd issues)
```

Decision matrix (same as CR-231):

| Outcome | Inference |
|---|---|
| CR231_IOCTL shows airportd polls only get*-handlers but never set*  | airportd doesn't think we're associated → check what get-handler return values differ from AppleBCMWLAN |
| CR231_POST_GATED shows correct sequence of LINK_CHANGED/BSSID/SSID after WCL_CONNECT_COMPLETE  | post sequence ok; gap is elsewhere |
| CR231_PRODUCER_STATE shows ic_bss=NULL or ic_state != RUN at the connect-complete post | producer skips the post |
| CR231_IOREG_STATE shows CoreWiFiDriverReadyKey=missing or false | IOREG state hasn't been published correctly |
| CR230_PAYLOAD shows non-zero status/reason fields | payload itself is malformed |
