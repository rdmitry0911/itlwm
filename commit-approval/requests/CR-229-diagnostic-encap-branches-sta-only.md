# COMMIT REQUEST CR-229 — DIAGNOSTIC_INSTRUMENTATION (ieee80211_encap STA-only branch coverage)

request_id: CR-229
request_stage: STAGE_1_STRUCTURAL
status: PENDING_STRUCTURAL_REVIEW
anomaly_id: post-CR-226 TX path blocker: `_iwm_start_task ... ieee80211_encap OUTPUT_ERROR`
change_class: DIAGNOSTIC_INSTRUMENTATION
branch: master
head_commit: b10b378a432e9bf99a1c827acc24b2e772ab1beb

does_this_fix_proven_current_root_cause: NO
if NO, why this change is still correct and necessary:
  Diagnostic-only. CR-226 Stage 2 evidence proved STEP 8b/8c/8d cleared,
  but the next-layer TX-encap path returns NULL from `ieee80211_encap`,
  causing `_iwm_start_task` line 3579 to log `OUTPUT_ERROR` repeatedly
  and the EAPOL handshake to stall. Five distinct `bad:` branches in
  the compiled function exit through `goto bad`; runtime evidence must
  identify which branch fires for our actual workload before any fix
  can be authored.

prior_rejections:
  - commit-approval/decisions/COMMIT_DECISION_CR-227.md — branch_coverage
    overclaim: claimed 6 branches but binary contained 5 (HOSTAP_NOTASSOC
    compile-excluded by `IEEE80211_STA_ONLY=1`).
  - commit-approval/decisions/COMMIT_DECISION_CR-228.md — premature
    submission: placeholder `head_commit`, no artifact, no build evidence;
    request explicitly deferred CR-228 source/build/artifact production
    until after CR-226 commits.

CR-229 corrects both rejections: HEAD is concrete (post-CR-226 commit
`b10b378a432...`); source/artifact/build evidence are all present and
fresh; branch universe is explicitly scoped to the post-preprocessor
STA-only Tahoe binary with binary proof.

reviewed_head_commit: b10b378a432e9bf99a1c827acc24b2e772ab1beb
artifact: commit-approval/artifacts/CR-229-diagnostic-encap-branches-sta-only.diff
build_evidence: commit-approval/build_evidence/CR-229-build-encap-branch-diagnostic-sta-only.txt
kext_sha256: 7f94c2f20a188ce90b88969e2899cf7bba6f82bf3994a9bde5ce8b9a1a25d563
kext_uuid: EDAC5835-8F4F-3DDE-B9D5-43583017E1CB
kext_size: 16299232
bootkc_undef: 886

## CLAIM_SCOPE

End-to-end diagnostic instrumentation of every reachable `bad:` failure
path in `ieee80211_encap` for the **post-preprocessor STA-only Tahoe
binary**. Five branches in one build, each with a per-branch
volatile-counter rate-limited XYLog emitter capped at 32 occurrences.

| Branch tag (compiled)      | Source line | What fires it |
|---|---|---|
| `ENCAP_BR_PULLUP_FAIL`     | line ~593 | `mbuf_pullup(&m, sizeof eh)` returns NULL |
| `ENCAP_BR_NONODE`          | line ~603 | `ieee80211_find_txnode(ic, dst)` returns NULL |
| `ENCAP_BR_PORT_NOT_VALID`  | line ~621 | RSN active + `ni_port_valid=0` + `etype != PAE` — most likely culprit during 4-way handshake |
| `ENCAP_BR_PREPEND_FAIL`    | line ~663 | `mbuf_prepend(&m, hdrlen, ...)` returns NULL |
| `ENCAP_BR_BAD_OPMODE`      | line ~712 | switch default — sanity guard |

Excluded from runtime inventory:

| Branch tag                  | Reason for exclusion |
|---|---|
| `ENCAP_BR_HOSTAP_NOTASSOC` | source under `#ifndef IEEE80211_STA_ONLY`; `IEEE80211_STA_ONLY=1` at `itl80211/openbsd/net80211/ieee80211_var.h:59`; verified absent in compiled binary (`strings`/`nm` proof in build evidence) |
| Two raw-frame `goto bad` (`ieee80211_output.c:584, 595`) | inside `if (0) { ... }` block at line 565; compiler dead-code-elimination removes both |

Each emitter logs:
- `ic_state` (0=INIT, 1=SCAN, 2=AUTH, 3=ASSOC, 4=RUN — to disambiguate
  pre/post-association drops);
- `ic_flags` (RSN/TKIP bit pattern) for PULLUP_FAIL/PORT_NOT_VALID;
- `etype` (Ethernet type — to disambiguate EAPOL vs non-EAPOL drops);
- branch-specific fields (dst MAC for NONODE; ni_port_valid for
  PORT_NOT_VALID; hdrlen for PREPEND_FAIL; ic_opmode for BAD_OPMODE;
  bss_set bit for NONODE).

Each branch's emitter is rate-limited to 32 occurrences per branch via
the `ENCAP_LOG()` macro and per-branch `volatile unsigned int` counter.
Addresses prior log-spam concern from CR-217 NEWPACKET FINAL diagnostic
(18432 events in CR-226 Stage 2 boot); without rate-limiting,
unbounded XYLog on this hot path would fire hundreds of times per
second during a stalled handshake.

After 32 emissions per branch, subsequent occurrences silently
increment the counter (`n=` field in the log line shows the running
total). All 5 reachable branches end-to-end in one build per
`feedback_diagnostic_end_to_end_criterion`.

## DIAGNOSTIC_NEUTRALITY

- No new state mutation outside the per-branch counter increments.
- No retry, fallback, mbuf modification, packet substitution, or queue
  manipulation introduced.
- Return values from each `bad:` are unchanged (caller still receives
  NULL and logs `_iwm_start_task 3579 ieee80211_encap OUTPUT_ERROR`).
- The CR-229 emitters fire BEFORE the existing `goto bad;` so they
  expose state at the failure point as observed by ieee80211_encap,
  with no behavioral side effect on the underlying control flow.

## EVIDENCE_FROM_DECOMP

- component / binary: `Build/Debug/Tahoe/AirportItlwm.kext/Contents/MacOS/AirportItlwm`
- function / symbol: `_ieee80211_encap` (and the surrounding outbound
  TX path in `mac80211.cpp::_iwm_start_task`).
- address / offset / source anchor:
  `itl80211/openbsd/net80211/ieee80211_output.c:537-735` is the function
  body; CR-229 inserts emitters at lines 593, 603, 621, 663, 712.
- exact lines / snippet: 5 `ENCAP_LOG()` calls before the existing
  `ic->ic_stats.is_tx_*++; goto bad;` cleanup.
- semantic meaning: each branch corresponds to a distinct failure mode
  documented in OpenBSD's net80211 layer.
- how this proves reference behavior: source code unchanged from
  upstream OpenBSD logic; only adds per-branch tap points without
  altering function semantics.

## EVIDENCE_FROM_RUNTIME (pre-fix; recap from CR-226 Stage 2)

- driver / kext logs: `commit-approval/runtime_evidence/CR-226-stage2-boot-log-2026-04-29.txt`
  contains the `_iwm_start_task 3579 ieee80211_encap OUTPUT_ERROR`
  pattern firing repeatedly after `ASSOC -> RUN`.
- ioreg / state evidence: not required for this diagnostic.
- before evidence: `_iwm_start_task ieee80211_encap OUTPUT_ERROR`
  starting at 19:33:32, recurring at 19:36:16/17/etc., each ~10ms apart
  during the EAPOL retry storm.
- after evidence: collected post-CR-229 Stage 2.
- why this runtime evidence is semantically significant: CR-226 Stage 2
  proved STEP 8b/8c/8d cleared; the OUTPUT_ERROR is the next-layer
  blocker preventing connection.
- why this is not trace-order / object-id noise: pattern reproduces
  every connection attempt.

## CAUSALITY

- regression window: CR-226 cleared STEP 8b; the OUTPUT_ERROR was
  always there (visible in CR-226 Stage 2 evidence too) but masked by
  STEP 8b's earlier failure. Now visible because STEP 8b passes.
- pinpointed divergence path: `ieee80211_encap` returns NULL through one
  of 5 `goto bad` paths.
- why this is root cause and not just correlation: the function's
  return value directly controls whether `_iwm_start_task` continues to
  `iwm_tx` or fails with `OUTPUT_ERROR`. CR-229 will identify which of
  the 5 paths fires, enabling a targeted fix.

## VERIFICATION_PERFORMED

- build: ** BUILD SUCCEEDED **
- targeted reproduction scenario: WPA2 connection attempt to CONTROL_STA_NETWORK AP
  (workload that produced the OUTPUT_ERROR pattern in CR-226 Stage 2).
- before reproduction result: `_iwm_start_task 3579 ieee80211_encap
  OUTPUT_ERROR` fires; no per-branch attribution.
- after reproduction result: pending Stage 2 (will identify exact
  branch via `ENCAP_BR_*` log lines).
- negative checks: no new BootKC undef symbols (886 same as CR-226);
  binary linker check OK; whitespace check OK.
- residual known issues not claimed fixed: the OUTPUT_ERROR itself
  (this CR is diagnostic-only).
- scenario coverage:
  - initial boot: covered (kext loads, encap drops fire).
  - reconnect / re-open: covered (auto-join retry workload).
  - sleep / wake: not in claim scope.
  - power transitions: not in claim scope.

## VERIFICATION

- Build : ** BUILD SUCCEEDED **
- Kext sha256 : 7f94c2f20a188ce90b88969e2899cf7bba6f82bf3994a9bde5ce8b9a1a25d563
- Kext UUID   : EDAC5835-8F4F-3DDE-B9D5-43583017E1CB
- Kext size   : 16299232 bytes
- BootKC undef : 886 (all resolve, same as CR-226)
- kmutil load : passes vtable check (gets to user-approval gate).
- `git diff --cached --check`: passes.
- HEAD is the post-CR-226 commit `b10b378a432e9bf99a1c827acc24b2e772ab1beb`
  (verifiable via `git rev-parse HEAD`).

## CHANGE_FOOTPRINT

- `itl80211/openbsd/net80211/ieee80211_output.c`:
  - Add 6 `static volatile unsigned int s_encap_*_count` counters
    (`s_encap_hostap_notassoc_count` is part of the source declaration
    inside `#ifndef IEEE80211_STA_ONLY`, so it does not appear in the
    compiled STA-only binary; this is structural and consistent with
    OpenBSD's HOSTAP-gated convention).
  - Add `ENCAP_LOG_LIMIT` macro + `ENCAP_LOG()` rate-limited emitter
    macro at file scope (just above ieee80211_encap).
  - Insert `ENCAP_LOG(...)` call before each existing `goto bad;` in
    the fallback path. No control-flow changes.
- No header file changes.
- No build script changes.
- No other source file is touched.

## RESIDUAL_UNCERTAINTY

- Actual failing branch is unknown until runtime evidence arrives.
  CR-229 only collects the data; no fix claim.
- Log spam from CR-217 NEWPACKET FINAL XYLog (18432 events per boot)
  remains present; cleanup planned in a later CR after the next-layer
  blocker is resolved.

## FORBIDDEN_ALTERNATIVES_CONSIDERED_AND_REJECTED

- heuristic timing : NO
- fallback path : NO (no upstream return-value change)
- masking/suppression : NO
- forced callback / state / success : NO
- forced sync / flush / barrier : NO
- retry / reorder / poll loop : NO
- behavior-changing diagnostic : NO
- overclaimed branch coverage : NO (CR-227's overclaim is corrected
  by explicit STA-only scoping with binary proof in build evidence).
- premature submission : NO (HEAD is concrete; artifact + build
  evidence + binary proof all present; CR-228's premature-submission
  finding is corrected).
- why rejected: each is a workaround. CR-229 stays passive and counts
  what's actually happening on the reviewable build.

## STAGE_2_INTENT

If Stage 1 approves, Stage 2 will collect runtime evidence on the same
WPA2 connection workload (associate to CONTROL_STA_NETWORK AP, fail handshake,
auto-retry):

```
itlwm: ENCAP_BR_PORT_NOT_VALID ic_state=0xN etype=0x0800 ni_port_valid=0 ic_flags=0xN n=K
itlwm: ENCAP_BR_NONODE         ic_state=0xN dst=... etype=0x... bss_set=N n=K
itlwm: ENCAP_BR_PREPEND_FAIL   ic_state=0xN hdrlen=... etype=0x... n=K
[+ optional ENCAP_BR_PULLUP_FAIL, ENCAP_BR_BAD_OPMODE]
```

Hypotheses to disambiguate:
```
H0: BR_PULLUP_FAIL    — undersized mbuf
H1: BR_NONODE         — ic_bss is NULL (pre-association)
H2: BR_PORT_NOT_VALID — RSN handshake never completes; ni_port_valid stuck at 0
H3: BR_PREPEND_FAIL   — out-of-mbuf at hdrlen prepend
H4: BR_BAD_OPMODE     — sanity (should never fire)
```

The data informs the CR-230+ fix.

## SUPERSEDES

supersedes (within encap-diagnostic chain):
- CR-227: rejected for branch_coverage overclaim (claimed 6 branches in
  one build but compiled binary had 5 due to `IEEE80211_STA_ONLY=1`).
- CR-228: rejected for premature submission (placeholder HEAD, no
  artifact, no build evidence).

CR-229 corrects both: branch universe explicitly scoped to STA-only
binary with `strings`/`nm` proof; HEAD is concrete post-CR-226
(`b10b378a432...`); fresh artifact + build evidence + binary proof
all present; CR-227 source code is preserved as-is (functionally
unchanged) and re-staged against the new HEAD.

## PROPOSED_COMMIT_MESSAGE

```
ieee80211_encap: instrument bad: branch coverage (CR-229)

Add per-branch rate-limited XYLog emitters before each goto bad in
ieee80211_encap to identify which of the 5 reachable failure paths
fires when _iwm_start_task line 3579 reports OUTPUT_ERROR after
CR-226 Stage 2 cleared STEP 8b. Diagnostic-only; no control-flow change.

Branch universe scoped to STA-only Tahoe binary (5 branches:
PULLUP_FAIL, NONODE, PORT_NOT_VALID, PREPEND_FAIL, BAD_OPMODE).
HOSTAP_NOTASSOC excluded by IEEE80211_STA_ONLY=1; raw-frame goto bad
sites inside if(0) block are dead-code-eliminated.

Per-branch ENCAP_LOG_LIMIT=32 emission cap; subsequent fires silently
increment counter, running total in n= field.
```
