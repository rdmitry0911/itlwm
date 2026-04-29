# WORKFLOW — itlwm / macOS Tahoe

## ROLES
- Executor: diagnoses, proves, patches, verifies, submits request
- Reviewer: independently reviews, approves or rejects
- Committer: commits only after approval

## REQUIRED FILES
- docs/AGENT_EXECUTION_PROTOCOL_ITLWM.md
- docs/REVIEWER_PROTOCOL_ITLWM.md
- docs/COMMIT_REQUEST_TEMPLATE_ITLWM.md
- docs/COMMIT_DECISION_TEMPLATE_ITLWM.md
- analysis/ANALYSIS_REPORT_YYYY-MM-DD.md
- commit-approval/requests/
- commit-approval/decisions/

## EXECUTOR LOOP
1. Register anomaly
2. Advance status only with evidence
3. Produce FIX_CANDIDATE before code changes
4. Patch only exact confirmed mismatch
5. Verify with build + targeted runtime evidence
6. Submit COMMIT_REQUEST + PATCH artifact
7. Stop and wait for review
8. Do not commit

## REVIEWER LOOP
1. Take oldest pending request
2. Review request completeness
3. Review decomp evidence
4. Review runtime evidence
5. Review causality if ROOT_CAUSE_FIX
6. Hunt for workaround logic
7. Review 1:1 reference alignment
8. Approve or reject by written decision

## COMMIT GATE
Commit is allowed only when all are true:
- decision file exists
- status = APPROVED
- allow_commit_now = YES
- HEAD unchanged
- diff unchanged
- request text unchanged

## HARD RULE
No hacks.
No heuristics.
No fallback behavior.
No guessed state correction.
No commit without approval.
