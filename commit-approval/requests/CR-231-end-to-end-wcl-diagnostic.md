# COMMIT REQUEST CR-231 — DIAGNOSTIC_INSTRUMENTATION (end-to-end WCL connect-complete branch coverage)

request_id: CR-231
request_stage: STAGE_1_STRUCTURAL
status: PENDING_STRUCTURAL_REVIEW
anomaly_id: post-CR-229 next-layer blocker: airportd shows isAssociated=0 despite our APPLE80211_M_WCL_CONNECT_COMPLETE_EVENT post; ni_port_valid stays 0; setCIPHER_KEY never issued
change_class: DIAGNOSTIC_INSTRUMENTATION
branch: master
head_commit: e1040cb9e929a8bf6816fde269a0f4d8a7b3742c

does_this_fix_proven_current_root_cause: NO
if NO, why this change is still correct and necessary:
  Diagnostic-only. Per `feedback_diagnostic_end_to_end_criterion`, every
  DIAGNOSTIC_INSTRUMENTATION CR must instrument ALL branches of the
  hypothesis to final points in one build. CR-230 covered only branches
  1-3 of the WCL connect-complete hypothesis (producer state, payload
  bytes, post markers); CR-231 expands to branches 4-7 in the same
  build, satisfying the end-to-end criterion.

prior_related:
  - commit-approval/requests/CR-230-diagnostic-wcl-payload-hexdump.md —
    earlier submission with branches 1-3 only. CR-230 stays in worktree
    per `feedback_no_delete_submitted_requests` and is immutable per
    `feedback_no_edit_submitted_cr`. CR-231 supersedes it as a fully
    end-to-end diagnostic.

reviewed_head_commit: e1040cb9e929a8bf6816fde269a0f4d8a7b3742c
artifact: commit-approval/artifacts/CR-231-end-to-end-wcl-diagnostic.diff
build_evidence: commit-approval/build_evidence/CR-231-build-end-to-end-wcl-diagnostic.txt
reference_decomp: analysis/cr230_applebcmwlan_sendConnectComplete_2026_04_29.txt
kext_sha256: f887b6c261c7dc2a13f03ae55a4c2f22a0cdc86cad42c2f9cc8a3d9fa953723b
kext_uuid: 8223B864-BE80-3B4A-A3B2-A84365F5CE4B
kext_size: 16304064
bootkc_undef: 886

## CLAIM_SCOPE

Pure diagnostic. End-to-end coverage of the WCL connect-complete
hypothesis at every checkpoint our kext can observe between (a) the
producer entry into `postTahoeWclConnectCompleteEvent` and (b) the
airportd-facing apple80211 IOCTL responses. No fix, no behavioral
change.

The hypothesis is: "the driver's `APPLE80211_M_WCL_CONNECT_COMPLETE_EVENT`
post does not propagate into airportd `isAssociated=1`, so airportd
never starts the 4-way handshake, so `setCIPHER_KEY` is never issued,
so `ni_port_valid` stays 0, so non-EAPOL TX drops at the
`ENCAP_BR_PORT_NOT_VALID` gate (CR-229 Stage 2 evidence)."

The hypothesis has 7 branches that produce distinct attribution if
they fire or stay silent:

| # | Branch | Tag | Site | Rate-limit |
|---|---|---|---|---|
| 1 | Producer entry state (ic_state, bss, flags, bssid)        | `CR231_PRODUCER_STATE` | postTahoeWclConnectCompleteEvent entry | 16/run |
| 2 | Payload bytes on-the-wire                                  | `CR230_PAYLOAD` | postTahoeWclConnectCompleteEvent | 11 rows × link-up rate |
| 3 | postMessage call markers                                   | `CR230_POST_PRE`/`CR230_POST_DONE` | postTahoeWclConnectCompleteEvent | unbounded by link-up rate |
| 4 | Every IOCTL airportd issues to the driver                  | `CR231_IOCTL` | processApple80211Ioctl entry | 16/run |
| 5 | Every event posted via the central workloop                | `CR231_POST_GATED` | postMessageGated | 16/run |
| 6 | Producer entry state (carry from branch 1)                 | (covered by 1) | — | — |
| 7 | IOREG state at LINK_UP post (CoreWiFiDriverReadyKey, fNetIf) | `CR231_IOREG_STATE` | postTahoeWclConnectCompleteEvent | 16/run |

End-to-end coverage criterion (`feedback_diagnostic_end_to_end_criterion`)
satisfied: all branches in one build, with bounded log volume, no
behavioral side effects.

## DIAGNOSTIC_NEUTRALITY

- No new state mutation. All emitters are XYLog calls + reads of
  existing state (ic_state, ic_bss, fNetIf, payload bytes, IOCTL
  request fields, IOREG property values).
- Existing control flow unchanged at every instrumentation site.
- No retry, fallback, packet substitution, queue manipulation, or
  state-machine modification introduced.
- Per-call rate-limit at 16 emissions via `CR231_LOG()` (V2.cpp) and
  `CR231_IOCTL_LOG()` (AirportItlwmSkywalkInterface.cpp). After 16th
  emission per call site, the counter silently increments; CR-232+
  can raise the limit if finer attribution is needed.
- CR-230's `CR230_PAYLOAD` rows + `POST_PRE/DONE` markers are
  unbounded (intrinsic link-up rate; ~10/30s during stalled
  handshake retries per CR-229 Stage 2 evidence).

## REFERENCE_DECOMP

`analysis/cr230_applebcmwlan_sendConnectComplete_2026_04_29.txt`
documents the AppleBCMWLAN connect-complete chain (sendConnectComplete
@ BootKC `0xffffff800158abce` → bulletinBoard subscriber
`connectCompleteEventHandler` @ `0xffffff800220a9a2` →
`updateConnectCompleteEvent` @ `0xffffff8002232ab0` reading payload
bytes 0..3 and 0xc..0x13 → `handleJoinConnectComplete` @
`0xffffff800220bfa4` validatePredicate → state-machine code 6 success
or 13 failure).

CR-231 instruments the kext-side checkpoints surrounding this chain:
producer construct/post (branches 1-3), framework dispatch entry
points we don't control (BootKC; not directly instrumentable from a
kext, but we can see what airportd polls afterwards via branches
4-5), airportd response loop (branch 4), IOREG observable state
(branch 7).

## EVIDENCE_FROM_RUNTIME (pre-fix; recap)

CR-229 Stage 2:
- `ENCAP_BR_PORT_NOT_VALID` fires 32 times (rate-capped); ic_state=0x4
  (RUN), ni_port_valid=0, ic_flags=0x32680801 (RSN active).
- airportd repeatedly logs `isAssociated=0` and continues scanning.
- No `setCIPHER_KEY` IOCTL ever observed.
- Driver-side `postTahoeWclConnectCompleteEvent` IS firing
  (`DEBUG postTahoeWclConnectCompleteEvent msg=0xd5 len=0xa4 status=0
  reason=0 bssid=50:4f:3b:cd:dd:67`) — proves the producer runs.

The gap: framework receives msg=0xd5 (or it doesn't reach the WCL
subscriber), but airportd never transitions to "associated".

## CAUSALITY

- regression window: was always present; surfaced by CR-226+CR-229
  unblocking earlier layers.
- pinpointed divergence path: WCL connect-complete event posted by
  driver but not reflected in airportd's association state.
- why this is the right next diagnostic: instruments every branch of
  the producer→framework→airportd chain that our kext can observe,
  giving Stage 2 evidence the data needed to attribute the gap to a
  specific layer (payload contents, post routing, framework dispatch,
  airportd polling, or IOREG state).

## VERIFICATION_PERFORMED

- build: ** BUILD SUCCEEDED **
- targeted reproduction scenario: WPA2 connection attempt to CONTROL_STA_NETWORK AP.
- before reproduction result: CR-230 captures branches 1-3; gaps in 4-7.
- after reproduction result (Stage 2 expected): all 7 branches emit
  per-site rate-limited log lines; Stage 2 evidence will identify
  whether airportd polls expected IOCTLs and whether our responses
  match what AppleBCMWLAN returns.
- negative checks: no new BootKC undef symbols (886); whitespace check
  passes; kmutil load reaches user-approval gate.
- residual known issues not claimed fixed: the `isAssociated=0`
  symptom (CR-231 is diagnostic-only).

## VERIFICATION

- Build : ** BUILD SUCCEEDED **
- Kext sha256 : f887b6c261c7dc2a13f03ae55a4c2f22a0cdc86cad42c2f9cc8a3d9fa953723b
- Kext UUID   : 8223B864-BE80-3B4A-A3B2-A84365F5CE4B
- Kext size   : 16304064 bytes
- BootKC undef : 886 (all resolve, same as CR-229/CR-230)
- Compiled tags verified by `strings`:
  - `CR230_PAYLOAD`, `CR230_POST_PRE`, `CR230_POST_DONE` (carryover)
  - `CR231_PRODUCER_STATE`, `CR231_IOREG_STATE`,
    `CR231_POST_GATED`, `CR231_IOCTL` (new)
- kmutil load : passes vtable check.
- `git diff --cached --check`: passes.
- HEAD : `e1040cb9e929a8bf6816fde269a0f4d8a7b3742c` (post-CR-229).

## CHANGE_FOOTPRINT

- `AirportItlwm/AirportItlwmV2.cpp`:
  - Add `CR231_LOG_LIMIT` macro + `CR231_LOG()` rate-limited emitter
    macro at file scope (just above postTahoeWclConnectCompleteEvent).
  - Add producer-state log + IOREG-state log inside
    postTahoeWclConnectCompleteEvent (around the existing CR-230
    payload hex dump).
  - Add `CR231_POST_GATED` log inside `postMessageGated` after `msg`
    is computed.
- `AirportItlwm/AirportItlwmSkywalkInterface.cpp`:
  - Add `CR231_IOCTL_LOG_LIMIT` macro + `CR231_IOCTL_LOG()`
    rate-limited emitter at file scope (just above
    processApple80211Ioctl).
  - Add `CR231_IOCTL` log at processApple80211Ioctl entry, before the
    existing `switch (req->req_type)` dispatch.
- No header file changes.
- No build-script changes.
- No artifacts/decomp file changes (CR-230's
  `analysis/cr230_applebcmwlan_sendConnectComplete_2026_04_29.txt`
  and request/build_evidence files are unchanged and not re-staged).

## RESIDUAL_UNCERTAINTY

- The 16-cap on CR231_* tags may hide low-rate or late-firing events.
  Stage 2 evidence will show which tags hit the cap; if any branch
  needs more observations, CR-232 can raise the limit selectively.
- BootKC-resident framework code (the bulletin board subscriber, the
  WCL state machine) cannot be instrumented from our kext.
  Branches 4-5 cover the airportd-facing apple80211 IOCTL surface
  and the kext-side outbound events, which is the maximum visibility
  available without DTrace or a kernel debugger.

## FORBIDDEN_ALTERNATIVES_CONSIDERED_AND_REJECTED

- heuristic timing : NO
- fallback path : NO (no upstream return-value change)
- masking/suppression : NO
- forced callback / state / success : NO
- forced sync / flush / barrier : NO
- retry / reorder / poll loop : NO
- behavior-changing diagnostic : NO

## STAGE_2_INTENT

If Stage 1 approves, Stage 2 will collect runtime evidence on a
WPA2 connection attempt. Expected publishable signals:

```
DEBUG CR231_PRODUCER_STATE fNetIf=1 ic=1 ic_state=4 ic_bss=1 ic_flags=0x32680801 bssid=50:4f:3b:cd:dd:67
DEBUG CR230_POST_PRE msg=0xd5 size=0xa4
DEBUG CR230_PAYLOAD off=0x00 ... (11 rows of payload bytes)
DEBUG CR231_IOREG_STATE CoreWiFiDriverReadyKey=true fNetIf=1
DEBUG CR230_POST_DONE msg=0xd5
DEBUG CR231_POST_GATED msg=0xN len=0xN ic_state=4   (every event posted via workloop)
DEBUG CR231_IOCTL cmd=0x... req_type=N req_data=N    (every IOCTL airportd issues)
```

Decision matrix for the data:

| Outcome | Inference |
|---|---|
| CR231_IOCTL shows airportd polls only get*-handlers but never set*  | airportd doesn't think we're associated → check what get-handler return values differ from AppleBCMWLAN |
| CR231_POST_GATED shows correct sequence of LINK_CHANGED/BSSID/SSID after WCL_CONNECT_COMPLETE  | post sequence ok; gap is elsewhere |
| CR231_PRODUCER_STATE shows ic_bss=NULL or ic_state != RUN at the connect-complete post | producer skips the post |
| CR231_IOREG_STATE shows CoreWiFiDriverReadyKey=missing or false | IOREG state hasn't been published correctly |
| CR230_PAYLOAD shows non-zero status/reason fields | payload itself is malformed |

Decision in any case feeds CR-232+ as a targeted fix.
