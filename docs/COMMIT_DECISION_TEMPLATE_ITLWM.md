# COMMIT DECISION TEMPLATE — itlwm / macOS Tahoe

request_id:
review_stage: STAGE_1_STRUCTURAL | STAGE_2_AFTER_FIX_RUNTIME
status: APPROVED_FOR_AFTER_FIX_RUNTIME | APPROVED | REJECTED | SUPERSEDED
reviewed_head_commit:
reviewed_diff_scope:
reviewed_anomaly_id:
reviewed_change_class: ROOT_CAUSE_FIX | REFERENCE_ALIGNMENT_FIX | SYSTEM_CONTRACT_FIX | DIAGNOSTIC_INSTRUMENTATION
reviewed_claim_scope:
reviewed_justification_path: REFERENCE_ALIGNMENT_FIX | SYSTEM_CONTRACT_FIX | DIAGNOSTIC_INSTRUMENTATION

verdict_summary:
allow_after_fix_runtime: YES | NO
allow_commit_now: YES | NO

## CHECKS
- completeness: PASS | FAIL
- subject_specificity: PASS | FAIL
- decomp_evidence: PASS | FAIL
- runtime_evidence: PASS | FAIL
- causality: PASS | FAIL | N/A
- claim_scope: PASS | FAIL
- workaround_hunt: PASS | FAIL
- reference_1_to_1: PASS | FAIL | N/A
- system_contract_coverage: PASS | FAIL | N/A
- diagnostic_neutrality: PASS | FAIL | N/A
- verification: PASS | FAIL
- diff_scope: PASS | FAIL

## DETAILED_FINDINGS
- completeness:
- subject_specificity:
- decomp_evidence:
- runtime_evidence:
- causality:
- claim_scope:
- reference_1_to_1:
- system_contract_coverage:
- diagnostic_neutrality:
- verification:
- diff_scope:

## WORKAROUND_HUNT
- heuristic timing found: YES | NO
- fallback behavior found: YES | NO
- masking/suppression found: YES | NO
- forced callback found: YES | NO
- forced state transition found: YES | NO
- forced success / fake success found: YES | NO
- forced sync / flush / barrier / invalidate without reference basis: YES | NO
- guessed state correction found: YES | NO
- retry / reorder / poll loop found: YES | NO
- temporary stabilization logic found: YES | NO
- best effort behavior found: YES | NO

## REVIEWED_EVIDENCE
### decomp evidence reviewed
- component / binary:
- function / symbol:
- address / offset / source anchor:
- exact lines / snippet reviewed:
- reviewer conclusion:

### runtime evidence reviewed
- panic logs:
- driver / kext logs:
- ioreg / state evidence:
- packet / firmware / transport trace:
- before/after evidence:
- reviewer conclusion:

## CLAIM SCOPE REVIEW
- claimed:
- actually supported:
- unsupported / overclaim found:
- final reviewer scope:

## SYSTEM CONTRACT REVIEW
- enumerated touchpoints reviewed:
- contracts confirmed:
- missing / weakly supported touchpoints:
- extra system-visible side effects found:
- reviewer conclusion:

## DIAGNOSTIC REVIEW
- hypotheses being disambiguated:
- probe points reviewed:
- behavior-neutrality assessment:
- extra side effects found:
- reviewer conclusion:

## REJECTION_REASONS
-

## REQUIRED_CHANGES_BEFORE_RESUBMISSION
-

## APPROVAL_CONSTRAINTS
- approved_exact_diff_only: YES
- approval_invalid_if_head_changes: YES
- approval_invalid_if_diff_changes: YES
- approval_invalid_if_request_changes: YES

## FINAL_DECLARATION
- Stage 1 success is `status: APPROVED_FOR_AFTER_FIX_RUNTIME` with `allow_after_fix_runtime: YES` and `allow_commit_now: NO`.
- Commit is allowed only if `review_stage: STAGE_2_AFTER_FIX_RUNTIME`, `status: APPROVED`, and `allow_commit_now: YES`.
- Approval applies only to the exact reviewed diff on the exact reviewed HEAD.
- Any change to HEAD, diff, or request text invalidates this decision.
