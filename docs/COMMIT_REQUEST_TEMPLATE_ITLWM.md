# COMMIT REQUEST TEMPLATE — itlwm / macOS Tahoe

request_id:
request_stage: STAGE_1_STRUCTURAL | STAGE_2_AFTER_FIX_RUNTIME
anomaly_id:
change_class: ROOT_CAUSE_FIX | REFERENCE_ALIGNMENT_FIX | SYSTEM_CONTRACT_FIX | DIAGNOSTIC_INSTRUMENTATION
branch: main
head_commit:
status: PENDING_STRUCTURAL_REVIEW | PENDING_AFTER_FIX_RUNTIME_REVIEW

does_this_fix_proven_current_root_cause: YES | NO
if NO, why this change is still correct and necessary:

## SYMPTOM
symptom:
expected system behavior:
actual behavior:
first visible manifestation:

## DIVERGENCE
exact divergence point:
confirmed deviation:
confirmed root cause:
exact confirmed deviation removed:
exact semantic mismatch removed:

## CLAIM SCOPE
exact claim scope:
non_claims:
- This request does NOT claim:
- This request does NOT claim:

## JUSTIFICATION PATH
justification path: REFERENCE_ALIGNMENT_FIX | SYSTEM_CONTRACT_FIX | DIAGNOSTIC_INSTRUMENTATION
- if REFERENCE_ALIGNMENT_FIX:
  - exact reference path proven by:
  - exact lifecycle boundary proven by:
  - exact side effects proven by:
- if SYSTEM_CONTRACT_FIX:
  - enumerated system-facing touchpoints:
  - exact expected contract at each touchpoint:
  - why no relevant touchpoints are missing:
  - why proposed path adds no extra system-visible side effects:
  - lifecycle scenarios covered by verification:
- if DIAGNOSTIC_INSTRUMENTATION:
  - exact hypotheses being disambiguated:
  - exact probe points:
  - why these probe points are sufficient:
  - why instrumentation is behavior-neutral:
  - exact runtime evidence expected from this instrumentation:

## CHANGED FILES
changed files:
-

## DIFF SUMMARY
diff summary:

## EVIDENCE FROM DECOMP
- component / binary:
- function / symbol:
- address / offset / source anchor:
- exact lines / snippet:
- semantic meaning:
- how this proves reference behavior:

## EVIDENCE FROM RUNTIME
- panic logs:
- driver / kext logs:
- ioreg / state evidence:
- packet / firmware / transport trace:
- before evidence:
- after evidence:
- why this runtime evidence is semantically significant:
- why this is not trace-order / object-id noise:

## CAUSALITY
- regression window:
- pinpointed divergence path:
- why this is root cause and not just correlation:

## VERIFICATION PERFORMED
- build:
- targeted reproduction scenario:
- before reproduction result:
- after reproduction result:
- negative checks:
- residual known issues not claimed fixed:
- scenario coverage:
  - initial boot:
  - reconnect / re-open:
  - sleep / wake:
  - power transitions:
  - multi-client / repeated open if relevant:

## STAGE RULES
- if request_stage = STAGE_1_STRUCTURAL:
  - after evidence may be pending
  - after reproduction result may be pending
  - request goal is only reviewer structural approval for after-fix runtime
- if request_stage = STAGE_2_AFTER_FIX_RUNTIME:
  - HEAD must equal Stage 1 reviewed HEAD
  - diff must equal Stage 1 reviewed diff
  - request text / claim scope must be unchanged except for adding after-fix runtime evidence
  - after evidence must be concrete, complete, and reviewable

## RESIDUAL UNCERTAINTY
residual uncertainty:

## FORBIDDEN ALTERNATIVES CONSIDERED AND REJECTED
- heuristic timing:
- fallback path:
- masking/suppression:
- force callback / state / success:
- forced sync / flush / barrier:
- retry / reorder / poll loop:
- why rejected:

## PROPOSED COMMIT MESSAGE
proposed commit message:

## PATCH ARTIFACT
exact patch artifact:

## SUPERSEDES
supersedes:
