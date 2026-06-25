# Analysis report 2026-06-25 - AP/APSTA role-7 admission materialization recovery

## ANOMALY
- id: AP-APSTA-role7-admission-materialization-recovery-20260625
- status: REFERENCE_PACKAGE_READY
- symptom: The Stage 2 runtime for the periodic iwn AP beacon producer candidate failed before the AP backend or beacon producer could be exercised: role-7 `APPLE80211_IOC_VIRTUAL_IF_CREATE` and cleanup `APPLE80211_IOC_VIRTUAL_IF_DELETE` both returned user `errno=102`, and no AP virtual interface materialized.
- first visible manifestation: `commit-approval/decisions/COMMIT_DECISION_AP-APSTA-iwn-periodic-beacon-producer-stage1-20260625.md` rejected Stage 2 while accepting indexed evidence sha256 `522862395212e355de5bc35a0db54edacfc313e0c244def057fda7bb464ea32f` only as negative role-7 boundary custody.
- expected system behavior: A successful role-7 create must parse the Apple80211 create carrier, validate role 7 and BSD-name input, allocate/store the APSTA owner, assemble APSTA `RegistrationInfo`, start and publish an IO80211/Skywalk APSTA child interface with AP BSD name/role, bind lower AP state, and roll back all owner/publication state on failure. Delete must consume the BSD-name carrier, match only an existing role-7 owner/interface, and clean up without allocating or forcing AP success.
- actual behavior: The local create path accepts role 7 into `AirportItlwmAPSTAOwner`, attempts lower AP start, deletes the owner on lower failure, and returns the lower failure. The latest runtime proves no public `apsta0` or AP role appeared. Local delete is name-only and fails closed after failed create because no matching owner remains.
- divergence point: The active root is now the role-7 admission/materialization boundary joining `AirportItlwmSkywalkInterface::setVIRTUAL_IF_CREATE`, `AirportItlwm::ensureAPSTAOwner`, `AirportItlwmAPSTAOwner::startLowerIfReady`, IO80211/Skywalk child-interface publication, role/name publication, and `setVIRTUAL_IF_DELETE` cleanup.
- evidence:
  - panic logs: N/A; no panic is claimed.
  - runtime logs: Existing Stage 2 evidence is accepted only as negative role-7 create/delete custody with `errno=102`; no new runtime was run in this cycle.
  - ioreg: Existing evidence recorded no `apsta0`, no `bridge0`, and parent Wi-Fi still `Infrastructure`; not reused as AP success.
  - packet traces: N/A; runtime never reached AP beacon/client/DHCP/traffic.
  - firmware traces: N/A; runtime stopped before AP backend proof.
  - decomp: `analysis/ANALYSIS_REPORT_2026-04-23.md` for `AppleBCMWLANCore::setVIRTUAL_IF_CREATE(...) @ 0xffffff80015fc280`, `docs/reference/AppleBCMWLAN_APSTA_enable_ap_interface_2026_04_27.md`, `docs/reference/AppleBCMWLAN_APSTA_teardown_2026_04_27.md`, and `docs/reference/AppleBCMWLAN_APSTA_OWNER_SKELETON_2026_05_09.md`.
  - docs: `docs/reference/AP_APSTA_ROLE7_ADMISSION_RECOVERY_PACKAGE_2026_06_25.md`.
- candidate causes: Missing local IO80211/Skywalk APSTA child-interface materialization, BSD name/role publication, or exact IOReturn-to-errno/failure-edge mapping between lower AP start and public Apple80211 role-7 status.
- rejected causes: This is not proof that the iwn periodic beacon producer failed; runtime did not reach beacon programming. It is not permission to rerun AP runtime, relabel errno 102 as AP success, edit evidence bundles, force AP support, or request commit.
- confirmed deviation: Current local role-7 create can own host APSTA state and lower AP start but does not yet prove or implement a distinct IO80211/Skywalk APSTA child interface materialization/publication path matching the Apple reference contract.
- root cause: Not yet confirmed as a code root cause. The confirmed active boundary is incomplete role-7 APSTA admission/materialization coverage.
- fix: Added/updated the bounded reference package and this analysis report. The package covers role-7 create/delete parsing, BSD-name handling, role validation, success/failure predicates, IOReturn/errno ledger, APSTA RegistrationInfo fields, IO80211/Skywalk publication requirement, role/name publication, owner transitions, rollback/delete cleanup, producers, consumers, state machines, object lifecycles, local iwn/HAL/Apple80211/IO80211-Skywalk mapping, and route recommendation.
- verification: Documentation-only validation pending/requested in the matching Stage 1 package. No source semantic patch, build, install, reboot, kext load/unload, AP/client runtime, evidence-index mutation, OpenCore mutation, validator mutation, or commit was performed.
- notes: The recommended auditor route is `IMPLEMENT_LOCAL` for the missing APSTA materialization glue. A bounded `RESEARCH_FIRST` prerequisite remains only to recover exact local API/failure-map details before semantic patching; no direct donor currently proves `REUSE_LINUX_BSD` or `REUSE_REFERENCE_DECOMP`.

## FIX_CANDIDATE

- anomaly_id: AP-APSTA-role7-admission-materialization-recovery-20260625
- symptom: Negative role-7 create/delete custody blocks AP/APSTA runtime proof before AP-up or beacon/backend code can be exercised.
- expected system behavior: Role-7 create succeeds only after complete APSTA owner, lower AP state, APSTA registration/publication, BSD name/role publication, and rollback ownership are in place; delete succeeds only for a matching materialized APSTA owner/interface.
- actual behavior: Local create deletes the host owner on lower/materialization failure and returns a public failure observed as `errno=102`; local delete after failed create has no matching owner and also fails closed.
- exact divergence point: Missing or unproven IO80211/Skywalk APSTA child-interface materialization and exact public status mapping after role-7 carrier acceptance.
- evidence from runtime: Existing indexed Stage 2 evidence sha256 `522862395212e355de5bc35a0db54edacfc313e0c244def057fda7bb464ea32f` is real external-world negative custody only; no new runtime was run.
- evidence from decomp: Apple role-7 create branch, APSTA factory/storage, APSTA RegistrationInfo assembly, `enableAPInterface()` side effects, and teardown references are cited in the package.
- exact semantic mismatch between reference and our code: The reference success path materializes and publishes an APSTA interface; local code currently has host owner/lower HAL ownership but lacks proven child-interface materialization/name-role publication.
- fix justification path: SYSTEM_CONTRACT_FIX
- enumerated system-facing touchpoints: `APPLE80211_IOC_VIRTUAL_IF_CREATE`; `APPLE80211_IOC_VIRTUAL_IF_DELETE`; role field at create carrier offset `0x0c`; MAC carrier offset `0x04`; BSD-name carrier offset `0x10`; APSTA owner storage; `ItlHalService::supportsAPMode`; `ItlHalService::startAPMode`; APSTA `RegistrationInfo`; IO80211/Skywalk child-interface publication; BSD name `ap`/unit publication; IORegistry interface role; net80211 HostAP state; failed-create rollback; delete cleanup.
- expected contract at each touchpoint: Role 7 is the only SoftAP create role; create must fail closed for bad/missing carriers and duplicate/create failures, must not report success until APSTA child interface publication is complete, and must roll back owner/lower/publication state on failure. Delete must be name-only, must not allocate AP state, and must return success only for an existing matching APSTA owner/interface.
- why no relevant touchpoints are missing: The package enumerates create/delete parsing, carrier fields, validation, owner lifecycle, lower AP owner, APSTA registration/publication, role/name publication, rollback, delete cleanup, producer/consumer ownership, and local iwn/HAL/Apple80211/IO80211-Skywalk boundaries for the submitted role-7 scope.
- why proposed path adds no extra system-visible side effects: The diff is documentation/request only. It changes no compiled source, no build scripts, no kext, no runtime selectors, no firmware commands, no evidence indexes, no validators, no OpenCore state, and no AP capability behavior.
- why this is root cause and not just correlation: The package does not claim final root cause. It narrows the active proven boundary after runtime stopped before beacon/backend code and defines the missing contract that must be recovered before semantic patching.
- why proposed fix is 1:1 with reference architecture and semantics: It uses the recovered Apple role-7 APSTA creation, registration, enable, and teardown contracts as the observable target and rejects AP success until local publication matches that target.
- files/functions to modify: `docs/reference/AP_APSTA_ROLE7_ADMISSION_RECOVERY_PACKAGE_2026_06_25.md`; `analysis/ANALYSIS_REPORT_2026-06-25.md`; matching Stage 1 request and patch artifact.
- forbidden alternative fixes considered and rejected: same runtime rerun; AP-success relabeling; forced role-7 success; fake `apsta0` publication; validator/evidence edits; source semantic patch before materialization contract recovery; commit request; broad AP/beacon/client overclaim.
- verification plan: path-limited documentation whitespace check, reverse-apply check for the documentation/analysis patch artifact, hash custody, and no build/runtime because compiled source did not change.

coder_decomp_completeness_self_check: YES
coder_payload_field_lifecycle_completeness_self_check: YES

## NO-WORKAROUND PROOF

This package is not a shortcut, backfill, renamed surrogate, status-only proof, schema-only proof, post-factum artifact repair, semantic degradation of previously accepted behavior, or any no-workaround violation. It preserves the indexed real external-world role-7 `errno=102` evidence only as negative boundary custody, refuses AP-success relabeling, performs no runtime/source/evidence/validator mutation, and advances the blocked parent by replacing an invalid same-root runtime rerun with a bounded missing-contract recovery package and `IMPLEMENT_LOCAL` route recommendation.

## 2026-06-25 - Role-7 local materialization API and status recovery

## ANOMALY
- id: AP-APSTA-role7-local-materialization-api-status-20260625
- status: LOCAL_REFERENCE_PACKAGE_READY
- symptom: Before a role-7 APSTA implementation can be reviewed, the workflow still needed direct local/reference evidence for the IO80211/Skywalk child-interface publication API, BSD name/role publication, successful-materialization rollback/delete cleanup, and the role-7 IOReturn-to-user-status ledger.
- first visible manifestation: `COMMIT_DECISION_AP-APSTA-role7-admission-materialization-recovery-20260625.md` approved the documentation route but required bounded research for exact local IO80211/Skywalk publication API/lifecycle and status mapping before any semantic APSTA materialization patch.
- expected system behavior: Role-7 create cannot return public success until APSTA owner state, lower AP state, APSTA registration-info identity, Skywalk child-interface registration, BSD name and role publication, and rollback/delete cleanup are all in place. Public status must preserve the fail-closed `errno=102` custody as negative evidence unless a direct reference bridge proves a different mapping.
- actual behavior: Current local code owns the role-7 create/delete carrier parsing, APSTA owner lifetime, lower-start gate, and fail-closed delete, but it does not allocate/register/start/defer-BSD-attach a distinct APSTA Skywalk child interface and does not publish an APSTA BSD name or role. The observed create/delete `errno=102` remains negative fail-closed custody.
- divergence point: Missing local APSTA materialization layer after `AirportItlwmAPSTAOwner::startLowerIfReady()` and before any public role-7 success return.
- evidence:
  - panic logs: N/A; no panic is claimed.
  - runtime logs: Existing accepted Stage 2 evidence remains negative-only role-7 create/delete custody with `ioctl_ret=-1` and `errno=102`; no new runtime was run.
  - ioreg: Existing evidence recorded no `apsta0`, no `bridge0`, and parent Wi-Fi still Infrastructure; not reused as AP success.
  - decomp: `analysis/ANALYSIS_REPORT_2026-04-23.md` role-7 create and RegistrationInfo ranges; `docs/reference/AppleBCMWLAN_APSTA_enable_ap_interface_2026_04_27.md`; `docs/reference/AppleBCMWLAN_APSTA_teardown_2026_04_27.md`; `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/96_apsta_owner_layer_reconstruction_2026_04_26.yaml`.
  - local source: `AirportItlwm/AirportItlwmSkywalkInterface.cpp:1943`, `:5472`, `:5540`; `AirportItlwm/AirportItlwmV2.cpp:3763`, `:3800`, `:3823`, `:4111`, `:4172`, `:4220`, `:6710`, `:6769`, `:6793`; `AirportItlwm/AirportItlwmAPSTAOwner.cpp:28`, `:99`, `:132`, `:152`; IO80211/Skywalk headers named in the package.
- candidate causes: The current role-7 owner is only a host state/lower-backend owner, not a published IO80211/Skywalk APSTA child interface.
- rejected causes: The accepted `errno=102` evidence is not AP success, not proof that beacon/backend producers failed, and not permission for runtime rerun, AP success relabeling, fake interface publication, validator/evidence edits, source semantic patching, or commit.
- confirmed deviation: Local code has no distinct APSTA child-interface registration/publication path matching the recovered Apple APSTA `RegistrationInfo`/start/enable contract.
- root cause: Not yet final code root cause for AP success. The confirmed implementation prerequisite is missing local APSTA materialization and publication.
- fix: Added `docs/reference/AP_APSTA_ROLE7_LOCAL_MATERIALIZATION_API_STATUS_PACKAGE_2026_06_25.md` and this analysis update. The package records direct local APIs for Skywalk registration, BSD attach, role/name publication surfaces, owner/delete lifecycle, rollback requirements, and the bounded status ledger.
- verification: Documentation-only validation planned in the matching Stage 1 request. No source semantic patch, build, install, reboot, kext load/unload, AP/client runtime, evidence-index mutation, OpenCore mutation, validator mutation, or commit was performed.
- notes: The IMPLEMENT_LOCAL route remains appropriate, but the first semantic patch must implement a coherent APSTA materialization layer. If it changes public failure mapping beyond the existing fail-closed unsupported path, it must first decompile the inherited `IOService::errnoFromReturn` bridge or caller path.

coder_decomp_completeness_self_check: YES
coder_payload_field_lifecycle_completeness_self_check: YES

## NO-WORKAROUND PROOF

This local materialization/status package is not a shortcut, backfill, renamed surrogate, status-only proof, schema-only proof, post-factum artifact repair, semantic degradation of previously accepted behavior, or any no-workaround violation. It preserves the accepted real external-world role-7 `errno=102` create/delete evidence as negative-only custody, does not relabel AP failure as AP success, does not mutate source/runtime/evidence/validator/OpenCore state, and advances the route by replacing a vague IMPLEMENT_LOCAL prerequisite with direct local/reference API and lifecycle boundaries.
