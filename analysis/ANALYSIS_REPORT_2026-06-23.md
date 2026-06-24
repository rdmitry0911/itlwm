# Analysis report 2026-06-23 - AP/APSTA current-head role-7 fail-closed rework

## ANOMALY
- id: AP-APSTA-current-head-role7-failclosed-20260623
- status: FIX_IMPLEMENTED
- symptom: The Tahoe Skywalk `setVIRTUAL_IF_CREATE` role-7 path allocated the host APSTA owner and returned success even when the lower HAL AP/GO backend reported unsupported.
- first visible manifestation: Static current-HEAD review at `2b261c1567c1a96104cea691bba3fb095031fd44`; no runtime was authorized for this cycle.
- expected system behavior: Role 6/AWDL must stay on the recovered public create-failed path, and role 7/SoftAP must not report create success until a real AP/GO HAL backend advertises support and `startAPMode` succeeds.
- actual behavior: Role 7 called `AirportItlwmAPSTAStage1Owner::startLowerIfReady()` but discarded the return value and returned `kIOReturnSuccess`, leaving an externally successful create result while AP-up remained false.
- divergence point: `AirportItlwmSkywalkInterface::setVIRTUAL_IF_CREATE`, role `APPLE80211_VIF_SOFT_AP` case.
- evidence:
  - docs: `docs/reference/AppleBCMWLAN_APSTA_OWNER_SKELETON_2026_05_09.md` records the APSTA owner/lower-backend split and the rule that role-7 acquisition must fail closed while the lower backend is absent.
  - decomp: Existing AP/APSTA reference closure package documented in the APSTA owner reference doc remains the basis for role-7-only owner routing, AP-up gating, station table ownership, and callback lifetime.
  - runtime logs: Not applicable; this is a Stage 1 structural request with runtime/install forbidden.
- candidate causes: The role-7 create handler treated owner allocation as create success instead of treating lower HAL AP/GO unsupported as a fail-closed terminal result.
- rejected causes: Role 6/AWDL routing is not the cause and remains on the prior create-failed return. Selector forwarding for `setMIS_MAX_STA` and `setSOFTAP_EXTENDED_CAPABILITIES_IE` is not claimed to enable AP mode.
- confirmed deviation: Role 7 could return success without a successful lower AP/GO backend start.
- root cause: The lower HAL result was ignored in the role-7 create handler.
- fix: Rename the host owner from the workflow-labeled APSTAStage1 name to `AirportItlwmAPSTAOwner`; keep the Xcode Tahoe build graph wired to the owner source; have role-7 create call `startLowerIfReady()`, delete the owner on any non-success lower result, clear the net80211 APSTA callback through `deleteAPSTAOwner()` before releasing the owner, and return the HAL failure. The role-6/AWDL path is unchanged.
- verification: `./scripts/build_tahoe.sh /Volumes/macos/System/Library/KernelCollections/BootKernelExtensions.kc` completed successfully in the canonical guest repository and resolved all 932 undefined symbols against the Tahoe BootKC.
- notes: No install, reboot, kext load/unload, AP/client runtime, OpenCore mutation, or host-source checkout was used. Pre-existing unrelated CR479 dirty work was preserved and excluded from the reviewed AP/APSTA patch artifact.

## FIX_CANDIDATE

- anomaly_id: AP-APSTA-current-head-role7-failclosed-20260623
- symptom: Role-7 virtual interface create could report success while AP/GO remained unsupported by the lower HAL.
- expected system behavior: Role 7 is the only SoftAP/APSTA carrier routed through the host APSTA owner, but create remains fail-closed until a lower AP/GO backend succeeds. Role 6/AWDL stays separate.
- actual behavior: Role 7 allocated an owner, ignored `startLowerIfReady()` failure, and returned success.
- exact divergence point: `AirportItlwmSkywalkInterface::setVIRTUAL_IF_CREATE`, `case 7`.
- evidence from runtime: Not collected; Stage 1 structural scope only.
- evidence from decomp: AP/APSTA reference closure recorded in `docs/reference/AppleBCMWLAN_APSTA_OWNER_SKELETON_2026_05_09.md` and inherited AP owner reference artifacts establish role-7 owner routing and AP-up gating.
- exact semantic mismatch between reference and our code: Owner allocation was exposed as create success even though the lower AP/GO AP-up gate had not succeeded.
- fix justification path: SYSTEM_CONTRACT_FIX
- enumerated system-facing touchpoints: role-7 `APPLE80211_IOC_VIRTUAL_IF_CREATE` return value; role-6/AWDL return value; APSTA owner lifetime; net80211 `ic_apsta_event_cb` and `ic_apsta_event_arg`; HAL AP/GO capability and start/stop surface; `isHostApRunning()` AP-up gate.
- expected contract at each touchpoint: role 7 must not report success without successful HAL AP/GO start; role 6 must not enter APSTA owner routing; callback fields must be cleared before owner storage is released; HAL default and unsupported backend paths must fail closed; AP-up remains false unless owner running state is reached.
- why no relevant touchpoints are missing: The patch does not enable firmware AP mode, station publication, beacon/key/CSA commands, HostAP net80211 opt-out, DHCP, or traffic; those remain non-claims and are not touched.
- why proposed path adds no extra system-visible side effects: On unsupported AP/GO hardware, the create path returns the lower HAL failure and deletes the owner, so no new successful AP interface or AP-up state is published.
- why this is root cause and not just correlation: The handler had a direct unconditional success return immediately after a lower-start call whose failure was ignored.
- why proposed fix is 1:1 with reference architecture and semantics: It preserves the recovered owner/lower-backend split while making externally visible create success depend on the lower AP/GO start gate.
- files/functions to modify: `AirportItlwm/AirportItlwmSkywalkInterface.cpp`; `AirportItlwm/AirportItlwmV2.cpp`; `AirportItlwm/AirportItlwmV2.hpp`; `AirportItlwm/AirportItlwmAPSTAOwner.cpp`; `AirportItlwm/AirportItlwmAPSTAOwner.hpp`; `itlwm.xcodeproj/project.pbxproj`; tracked documentation.
- forbidden alternative fixes considered and rejected: forced create success was rejected; fake AP-up state was rejected; retry/poll/reorder/timing was rejected; suppressing role-7 routing entirely was rejected because the owner/lifetime groundwork is needed for the next AP/GO backend layer.
- verification plan: Stage 1 build and Tahoe BootKC symbol check only; after-fix runtime remains forbidden until auditor approval.

coder_decomp_completeness_self_check: YES
coder_payload_field_lifecycle_completeness_self_check: YES


## ANOMALY
- id: AP-APSTA-role7-delete-teardown-20260623
- status: FIX_IMPLEMENTED
- symptom: The Tahoe Skywalk Apple80211 IOCTL switch had role-7 `APPLE80211_IOC_VIRTUAL_IF_CREATE` owner dispatch but no matching `APPLE80211_IOC_VIRTUAL_IF_DELETE` dispatch into the host APSTA owner teardown path.
- first visible manifestation: Static current-HEAD review at `2b261c1567c1a96104cea691bba3fb095031fd44`; no runtime was authorized for this cycle.
- expected system behavior: Role-7 APSTA owner lifetime must have symmetric create/delete custody. Delete must release the existing APSTA owner and unregister the net80211 station-event callback before owner storage is reclaimed, while absent or non-matching owners must fail closed.
- actual behavior: The Tahoe Skywalk IOCTL switch routed create but not delete, so the only APSTA cleanup entry was controller release or the failed-create cleanup path.
- divergence point: `AirportItlwmSkywalkInterface::apple80211Request`, `APPLE80211_IOC_VIRTUAL_IF_DELETE` case was absent from the Tahoe/Skywalk switch.
- evidence:
  - docs: `docs/reference/AppleBCMWLAN_APSTA_OWNER_SKELETON_2026_05_09.md` records role-7 create/delete glue, owner lifetime, unregister-before-release, and fail-closed AP/GO semantics as local APSTA owner scope.
  - decomp: Existing AP/APSTA reference closure package documents role-7 owner lifetime, AP-up gating, station table ownership, and callback lifetime; no new decomp was required for this bounded switch-to-owner dispatch.
  - runtime logs: Not applicable; this is a Stage 1 structural request with runtime/install forbidden.
- candidate causes: The prior structural slice intentionally left the legacy `setVIRTUAL_IF_DELETE` migration as follow-up work.
- rejected causes: AP/GO firmware backend absence is not the cause of the missing delete dispatch; role-6/AWDL and NAN create failures remain separate and are not routed into APSTA delete.
- confirmed deviation: The owner had a delete implementation but the Tahoe Skywalk virtual-interface delete selector could not reach it.
- root cause: Missing `APPLE80211_IOC_VIRTUAL_IF_DELETE` dispatch in the Tahoe Skywalk IOCTL switch.
- fix: Add a switch-only Skywalk `setVIRTUAL_IF_DELETE` handler, route it to `AirportItlwm::deleteAPSTAOwnerForBSDName()`, require a non-empty delete carrier BSD name that matches the existing APSTA owner, call `deleteAPSTAOwner()` only on match, and return unsupported for null, empty, absent, or non-matching owners/names.
- verification: Pending current-cycle canonical guest Tahoe build and BootKC symbol check; no install, reboot, kext load/unload, AP/client runtime, OpenCore mutation, or role-7 success claim is authorized.
- notes: This is AP/APSTA structural lifetime symmetry only. It preserves the approved custody candidate and the unrelated CR479 dirty work outside the AP/APSTA artifact.

## FIX_CANDIDATE

- anomaly_id: AP-APSTA-role7-delete-teardown-20260623
- symptom: Role-7 APSTA owner teardown was implemented but not reachable from Tahoe/Skywalk `APPLE80211_IOC_VIRTUAL_IF_DELETE`.
- expected system behavior: The role-7 owner allocated by SoftAP create must be releasable by the matching virtual-interface delete carrier; unregister-before-release must clear the net80211 APSTA callback; delete must not allocate owner state or report AP/GO capability.
- actual behavior: The Tahoe Skywalk IOCTL switch had no delete case, so selector dispatch stopped before the APSTA owner teardown path.
- exact divergence point: `AirportItlwmSkywalkInterface::apple80211Request`, missing `APPLE80211_IOC_VIRTUAL_IF_DELETE` `SIOCSA80211` branch.
- evidence from runtime: Not collected; Stage 1 structural scope only.
- evidence from decomp: AP/APSTA reference closure recorded in `docs/reference/AppleBCMWLAN_APSTA_OWNER_SKELETON_2026_05_09.md` establishes role-7 owner lifetime, delete glue, callback lifetime, and AP-up gating.
- exact semantic mismatch between reference and our code: The local Tahoe/Skywalk selector graph exposed create-side owner custody without the matching delete-side teardown dispatch.
- fix justification path: SYSTEM_CONTRACT_FIX
- enumerated system-facing touchpoints: `APPLE80211_IOC_VIRTUAL_IF_DELETE` `SIOCSA80211` dispatch; delete carrier BSD name; APSTA owner lifetime; net80211 `ic_apsta_event_cb` and `ic_apsta_event_arg`; HAL AP/GO `stopAPMode` surface; `isHostApRunning()` AP-up gate.
- expected contract at each touchpoint: delete reaches only the existing role-7 APSTA owner when the delete carrier supplies a non-empty BSD name matching that owner; null, empty, non-matching, or absent owners/names fail closed; callback fields are unregistered before owner release; lower AP stop is called only through owner teardown when AP mode is supported; AP-up remains false on unsupported backends.
- why no relevant touchpoints are missing: The patch does not enable AP firmware operation, HostAP net80211 opt-out, beacon/key/CSA programming, station publication, DHCP, traffic, role-6/AWDL, NAN, or CR479 carrier behavior.
- why proposed path adds no extra system-visible side effects: The handler does not allocate, start, retry, publish, or report AP/GO success; it only releases an already existing APSTA owner for a non-empty matching carrier BSD name and otherwise returns unsupported.
- why this is root cause and not just correlation: The selector switch lacked the only branch that could call the existing APSTA delete implementation from Tahoe/Skywalk.
- why proposed fix is 1:1 with reference architecture and semantics: It preserves the recovered owner/lower-backend split and adds the missing owner lifetime edge while keeping AP success gated on lower AP/GO start.
- files/functions to modify: `AirportItlwm/AirportItlwmSkywalkInterface.cpp`; `AirportItlwm/AirportItlwmSkywalkInterface.hpp`; `AirportItlwm/AirportItlwmV2.cpp`; `AirportItlwm/AirportItlwmV2.hpp`; tracked analysis/reference/request artifacts.
- forbidden alternative fixes considered and rejected: forced delete success for absent owners was rejected; owner allocation from delete was rejected; AP-up or AP/GO success publication was rejected; retry/poll/reorder/timing was rejected; broad legacy virtual-interface teardown was rejected because this slice is role-7 APSTA owner custody only.
- verification plan: Stage 1 build and Tahoe BootKC symbol check only; install, reboot, kext load/unload, AP/client runtime, CR479 runtime, OpenCore mutation, commit, and role-7 success claims remain forbidden.

coder_decomp_completeness_self_check: YES
coder_payload_field_lifecycle_completeness_self_check: YES


## ANOMALY
- id: AP-APSTA-apgo-hal-method-surface-20260623
- status: FIX_IMPLEMENTED
- symptom: After role-7 APSTA owner create/delete custody, the Tahoe/Skywalk AP/GO selector surface still exposed only `SOFTAP_EXTENDED_CAPABILITIES_IE` and `MIS_MAX_STA`; recovered SAP setters for hidden mode, SoftAP params, CSA, and SoftAP Wi-Fi network-info IE had no controller-to-owner/HAL route.
- first visible manifestation: Static current-HEAD review at `2b261c1567c1a96104cea691bba3fb095031fd44`; no runtime was authorized for this cycle.
- expected system behavior: The APSTA owner owns the recovered SAP method surface while AP-up remains gated by lower AP/GO support. Selector calls that require AP-up must fail closed or return the recovered not-up code until `startAPMode()` succeeds, and the HAL must expose explicit backend hooks whose default implementations return unsupported.
- actual behavior: The owner already had lower start/stop, beacon/key/CSA placeholders and two SoftAP selector mirrors, but the Tahoe/Skywalk IOCTL switch did not dispatch `HOST_AP_MODE_HIDDEN`, `SOFTAP_PARAMS`, `SOFTAP_TRIGGER_CSA`, or `SOFTAP_WIFI_NETWORK_INFO_IE`, and the HAL lacked explicit methods for hidden, SoftAP params, and network-info IE.
- divergence point: `AirportItlwmSkywalkInterface::processApple80211Ioctl` missing AP/GO selector cases after the role-7 create/delete surface; `ItlHalService` missing backend method slots for the corresponding AP/GO commands.
- evidence:
  - docs: `include/Airport/IO80211SapProtocol.h` records recovered slots 526-529 for `setHOST_AP_MODE_HIDDEN`, `setSOFTAP_PARAMS`, `setSOFTAP_TRIGGER_CSA`, and `setSOFTAP_WIFI_NETWORK_INFO_IE`; `AirportItlwmAPSTAInterface.hpp` records their carrier layouts, AP-up state gates, and error constants.
  - decomp: Existing AP/APSTA reference closure package documents the SAP vtable/method surface, AP-up gate at state +0x26c, SoftAP private state block, and downstream iovar/firmware command ownership.
  - runtime logs: Not applicable; this is a Stage 1 structural request with runtime/install forbidden.
- candidate causes: The previous bounded slice intentionally stopped at role-7 delete/teardown and left the next AP/GO selector/backend method surface unrouted.
- rejected causes: AP/GO firmware backend absence is not treated as success; role-7 create success, station publication, beacon emission, key install, DHCP, and traffic are not claimed.
- confirmed deviation: The recovered AP/GO SAP setters had no local controller/owner/HAL surface beyond the two earlier SoftAP probes.
- root cause: Missing AP/GO selector dispatch and default HAL method slots for the next recovered SAP method group.
- fix: Add fail-closed `ItlHalService` default hooks for hidden, SoftAP params, and SoftAP Wi-Fi network-info IE; route Tahoe/Skywalk `HOST_AP_MODE_HIDDEN`, `SOFTAP_PARAMS`, `SOFTAP_TRIGGER_CSA`, and `SOFTAP_WIFI_NETWORK_INFO_IE` through controller methods into the APSTA owner; cache safe structural fields in the owner; call lower HAL hooks only when the AP-up gate is already true.
- verification: `./scripts/build_tahoe.sh /Volumes/macos/System/Library/KernelCollections/BootKernelExtensions.kc` completed successfully in the canonical guest repository and resolved all 932 undefined symbols against the Tahoe BootKC.
- notes: No install, reboot, kext load/unload, AP/client runtime, OpenCore mutation, CR479 runtime, after-fix runtime, commit, or role-7 success claim was performed. Pre-existing unrelated CR479 dirty work was preserved and excluded from the AP/APSTA patch artifact.

## FIX_CANDIDATE

- anomaly_id: AP-APSTA-apgo-hal-method-surface-20260623
- symptom: Recovered AP/GO SAP setters after role-7 lifetime were not routed to the host APSTA owner or lower HAL method surface.
- expected system behavior: SAP AP/GO setters have explicit owner/controller/HAL methods, but firmware-visible work remains gated on AP-up; unsupported backends return unsupported/not-ready rather than AP success.
- actual behavior: Only two SoftAP selectors were wired; hidden, SoftAP params, CSA, and Wi-Fi network-info IE had no Tahoe/Skywalk switch routing.
- exact divergence point: `AirportItlwmSkywalkInterface::processApple80211Ioctl` and `ItlHalService` AP/GO method declarations.
- evidence from runtime: Not collected; Stage 1 structural scope only.
- evidence from decomp: `include/Airport/IO80211SapProtocol.h` recovered slots 526-529 and `AirportItlwmAPSTAInterface.hpp` offset/state assertions for hidden, SoftAP params, CSA, and Wi-Fi network-info IE.
- exact semantic mismatch between reference and our code: The local APSTA owner did not expose the recovered method group needed between role-7 lifetime and future backend implementation.
- fix justification path: SYSTEM_CONTRACT_FIX
- enumerated system-facing touchpoints: `APPLE80211_IOC_HOST_AP_MODE_HIDDEN`, `APPLE80211_IOC_SOFTAP_PARAMS`, `APPLE80211_IOC_SOFTAP_TRIGGER_CSA`, `APPLE80211_IOC_SOFTAP_WIFI_NETWORK_INFO_IE`, APSTA owner private state block, `resetState26c` AP-up gate, and AP/GO HAL backend hooks.
- expected contract at each touchpoint: hidden accepts only 0/1 and returns not-up until AP is running; SoftAP params and Wi-Fi network-info IE update bounded owner state but return not-ready until AP is running; CSA reaches the HAL only when AP is running; HAL defaults return unsupported.
- why no relevant touchpoints are missing: This slice is limited to the recovered setter/method surface immediately after role-7 create/delete. It does not claim backend AP firmware operation, HostAP opt-out, station publication, beacon/key implementation, DHCP, traffic, role-6/AWDL, NAN, or CR479 behavior.
- why proposed path adds no extra system-visible side effects: The switch only routes existing selector carriers to owner methods. Lower HAL calls remain behind `isApRunning()` and the default HAL implementations return unsupported, so unsupported Intel backends still cannot publish AP-up or role-7 success.
- why this is root cause and not just correlation: The absence of switch/controller/HAL method entries is the direct reason these recovered selectors could not reach the APSTA owner surface.
- why proposed fix is 1:1 with reference architecture and semantics: It follows the recovered SAP slot group and keeps the reference AP-up gate as the system-visible boundary before firmware commands.
- files/functions to modify: `include/HAL/ItlHalService.hpp`; `AirportItlwm/AirportItlwmAPSTAOwner.cpp`; `AirportItlwm/AirportItlwmAPSTAOwner.hpp`; `AirportItlwm/AirportItlwmSkywalkInterface.cpp`; `AirportItlwm/AirportItlwmV2.cpp`; `AirportItlwm/AirportItlwmV2.hpp`; tracked analysis/reference/request artifacts.
- forbidden alternative fixes considered and rejected: forced AP-up was rejected; forced selector success for AP-up-gated methods was rejected; backend type guessing was rejected; retry/poll/reorder/timing was rejected; broad station/datapath implementation was rejected as outside this bounded method-surface layer.
- verification plan: Stage 1 build and Tahoe BootKC symbol check only; install, reboot, kext load/unload, AP/client runtime, CR479 runtime, OpenCore mutation, commit, and role-7 success claims remain forbidden.

coder_decomp_completeness_self_check: YES
coder_payload_field_lifecycle_completeness_self_check: YES


## ANOMALY
- id: AP-APSTA-apgo-delete-name-rework-20260623
- status: FIX_IMPLEMENTED
- symptom: Stage 1 review found that the exact AP/GO method-surface diff preserved a role-7 `VIRTUAL_IF_DELETE` custody edge where `AirportItlwmAPSTAOwner::matchesBSDName()` treated null or empty delete carrier names as matches.
- first visible manifestation: `commit-approval/decisions/COMMIT_DECISION_AP-APSTA-apgo-hal-method-surface-stage1-20260623.md` rejected the prior request because an empty-name delete carrier could tear down the APSTA owner without matching `bsdNameStorage`.
- expected system behavior: Delete custody succeeds only for a non-empty carrier BSD name matching the existing role-7 APSTA owner. Null, empty, absent, and non-matching carrier names fail closed and do not tear down APSTA owner state.
- actual behavior: `matchesBSDName()` returned true for null or empty carrier names.
- divergence point: `AirportItlwmAPSTAOwner::matchesBSDName()` null/empty guard.
- evidence:
  - docs: AP/APSTA reference documentation records that the delete carrier has a BSD name and the local system contract requires matching that name against the existing APSTA owner before teardown.
  - decomp: No new decompilation was required; the accepted AP/APSTA owner lifetime package and auditor review identified this as a submitted system-contract gap in the local fail-closed edge.
  - runtime logs: Not applicable; runtime/install are forbidden for this Stage 1 structural rework.
- candidate causes: The helper used null/empty as an owner wildcard instead of treating it as a non-matching carrier.
- rejected causes: The AP/GO HAL method-surface selectors and default unsupported HAL hooks remain bounded and build-verified; the rejection was isolated to delete-name matching in the exact submitted diff.
- confirmed deviation: Null or empty delete carrier names matched the APSTA owner.
- root cause: `matchesBSDName()` returned true before comparing to `bsdNameStorage` when the carrier name was null or empty.
- fix: `matchesBSDName()` now returns false for null or empty carrier names and compares only non-empty carrier names with the stored owner BSD name.
- verification: `./scripts/build_tahoe.sh /Volumes/macos/System/Library/KernelCollections/BootKernelExtensions.kc` completed successfully in the canonical guest repository and resolved all 932 undefined symbols against the Tahoe BootKC after the null/empty delete-name rework. The refreshed AP/APSTA patch artifact passed `git diff --check` and reverse-apply validation. No install, reboot, kext load/unload, AP/client runtime, CR479 runtime, after-fix runtime, OpenCore mutation, or commit was performed.
- notes: This rework preserves the AP/GO HAL method-surface diff and unrelated CR479 dirty work outside the AP/APSTA artifact.

## FIX_CANDIDATE

- anomaly_id: AP-APSTA-apgo-delete-name-rework-20260623
- symptom: Empty or null `VIRTUAL_IF_DELETE` carrier names could delete the APSTA owner.
- expected system behavior: Delete tears down only an existing role-7 APSTA owner whose stored BSD name matches a non-empty delete carrier BSD name.
- actual behavior: Null or empty names matched before the stored-name comparison.
- exact divergence point: `AirportItlwmAPSTAOwner::matchesBSDName()`.
- evidence from runtime: Not collected; Stage 1 structural scope only.
- evidence from decomp: Existing AP/APSTA owner lifetime package plus the accepted system contract for delete carrier name matching; no direct reference evidence was supplied that empty names intentionally match.
- exact semantic mismatch between reference and our code: The local system contract said delete succeeds only for a matching BSD-name carrier, but local code treated absence of a carrier name as a wildcard match.
- fix justification path: SYSTEM_CONTRACT_FIX
- enumerated system-facing touchpoints: `APPLE80211_IOC_VIRTUAL_IF_DELETE` carrier pointer, delete carrier BSD name, `AirportItlwmAPSTAOwner::matchesBSDName()`, APSTA owner lifetime, net80211 APSTA callback unregister, HAL AP/GO stop surface, AP-up gate.
- expected contract at each touchpoint: null carrier returns bad argument at the Skywalk interface; null or empty names fail in `matchesBSDName()`; only a non-empty exact stored-name match can reach `deleteAPSTAOwner()`; absent owners and non-matches return unsupported; teardown unregisters callback before owner release; AP-up remains false on unsupported backends.
- why no relevant touchpoints are missing: This rework changes only the reviewed delete-name edge and preserves the already bounded AP/GO method-surface selector/HAL work. AP firmware, HostAP opt-out, station publication, beacon/key/CSA backend implementation, DHCP, traffic, role-6/AWDL, NAN, and CR479 remain non-claims.
- why proposed path adds no extra system-visible side effects: The change removes a wildcard teardown condition. It does not allocate, start, retry, publish, force AP-up, force success, or add any new lower HAL call.
- why this is root cause and not just correlation: The reviewer-identified overbroad teardown condition was the direct boolean branch returning true for null or empty names.
- why proposed fix is 1:1 with reference architecture and semantics: It preserves owner/lower-backend separation and makes delete custody match the submitted fail-closed system contract.
- files/functions to modify: `AirportItlwm/AirportItlwmAPSTAOwner.cpp`; tracked AP/APSTA analysis/reference/request artifacts; regenerated AP/APSTA patch/build evidence.
- forbidden alternative fixes considered and rejected: accepting empty names as wildcard was rejected; forced delete success was rejected; owner allocation from delete was rejected; AP-up or AP/GO success publication was rejected; retry/poll/reorder/timing was rejected.
- verification plan: `git diff --check`, reverse-apply check of the refreshed AP/APSTA patch artifact, canonical guest Tahoe build, and Tahoe BootKC symbol check only. Install, reboot, kext load/unload, AP/client runtime, CR479 runtime, OpenCore mutation, after-fix runtime, and commit remain forbidden.

coder_decomp_completeness_self_check: YES
coder_payload_field_lifecycle_completeness_self_check: YES


## ANOMALY
- id: AP-APSTA-hostap-admission-staonly-station-event-20260623
- status: FIX_IMPLEMENTED
- symptom: After the AP/GO HAL method-surface review, the APSTA station-event bridge still used the broad `IEEE80211_OPT_OUT_STA_ONLY` build spelling as the only admission marker and the APSTA owner bound the net80211 station-event callback even in default Tahoe STA-only builds where the producer call sites are compiled out.
- first visible manifestation: Static current-HEAD review at `2b261c1567c1a96104cea691bba3fb095031fd44`; no runtime was authorized for this cycle.
- expected system behavior: Default Tahoe builds must remain STA-only and must not bind a dormant HostAP/net80211 APSTA station-event consumer. AP/APSTA exploration builds must opt into a named station-event admission gate before `IEEE80211_STA_ONLY` is suppressed, and callback binding must be compiled only in that admitted gate.
- actual behavior: The opt-out build selected only the legacy broad `IEEE80211_OPT_OUT_STA_ONLY` spelling, and `AirportItlwm::ensureAPSTAOwner()` / teardown paths called `ieee80211_apsta_event_register()` and `ieee80211_apsta_event_unregister()` unconditionally with respect to `IEEE80211_STA_ONLY`.
- divergence point: `itl80211/openbsd/net80211/ieee80211_var.h` STA-only opt-out gate, `scripts/build_tahoe.sh` opt-out preprocessor definitions, and `AirportItlwm::ensureAPSTAOwner()` / `releaseAll()` / `deleteAPSTAOwner()` net80211 station-event binding.
- evidence:
  - docs: `analysis/ANALYSIS_REPORT_2026-05-14b.md` records the net80211 AP station-event producer bridge and the original `IEEE80211_STA_ONLY` opt-out as a bounded AP exploration enabler; `analysis/ANALYSIS_REPORT_2026-05-14h.md` records the owner binding as structurally inert because default STA-only builds do not compile the publish call sites.
  - decomp: Existing AP/APSTA reference closure package documents the APSTA station-event owner contract, single-consumer callback, owner lifetime, and callback unregister-before-release invariant.
  - runtime logs: Not applicable; this is a Stage 1 structural request with runtime/install forbidden.
- candidate causes: The prior bridge and binding layers predated the current fail-closed AP/GO HAL method-surface review and did not carry a dedicated admission symbol for the station-event opt-out scope.
- rejected causes: AP/GO firmware backend absence is not treated as success; this patch does not enable AP profile creation, beacon/key/station producers, firmware AP mode, AP/client runtime, role-7 success, or CR479 behavior.
- confirmed deviation: A broad opt-out spelling was the only visible admission marker, and the owner binding compiled into default STA-only builds despite the absence of compiled producer call sites.
- root cause: Missing scoped APSTA station-event opt-out admission gate shared by the net80211 header, opt-out build envelope, and controller callback binding.
- fix: Add `IEEE80211_APSTA_STATION_EVENT_OPT_OUT` as the named admission gate, map the legacy `IEEE80211_OPT_OUT_STA_ONLY` spelling to that gate for existing callers, have `scripts/build_tahoe.sh --opt-out` define both macros, and compile APSTA net80211 station-event register/unregister calls only when the named gate is defined.
- verification: Pending current-cycle canonical guest Tahoe build and BootKC symbol check; no install, reboot, kext load/unload, AP/client runtime, OpenCore mutation, CR479 runtime, after-fix runtime, or commit is authorized.
- notes: This is a structural admission gate only. It preserves the AP/GO HAL method-surface work, fail-closed role-7 create/delete behavior, and unrelated CR479 dirty work outside the AP/APSTA artifact.

## FIX_CANDIDATE

- anomaly_id: AP-APSTA-hostap-admission-staonly-station-event-20260623
- symptom: The HostAP/net80211 APSTA station-event bridge had no dedicated admission marker after the AP/GO HAL method-surface review, and owner callback binding was compiled into default STA-only builds.
- expected system behavior: Default Tahoe builds keep `IEEE80211_STA_ONLY`, do not compile HostAP station-event producer call sites, and do not bind an APSTA net80211 callback. Only an explicit APSTA station-event opt-out build suppresses `IEEE80211_STA_ONLY` for the net80211 HostAP station-event exploration surface.
- actual behavior: The build opt-out defined only `IEEE80211_OPT_OUT_STA_ONLY`, and the controller registered/unregistered the APSTA net80211 callback without a compile-time admission guard.
- exact divergence point: `itl80211/openbsd/net80211/ieee80211_var.h` opt-out block; `scripts/build_tahoe.sh` `EXTRA_PP`; `AirportItlwm::ensureAPSTAOwner()`, `AirportItlwm::releaseAll()`, and `AirportItlwm::deleteAPSTAOwner()`.
- evidence from runtime: Not collected; Stage 1 structural scope only.
- evidence from decomp: The AP/APSTA closure and prior station-event bridge artifacts establish a single APSTA station-event consumer tied to APSTA owner lifetime; no reference evidence supports binding that consumer in a default STA-only build where the producer call sites are absent.
- exact semantic mismatch between reference and our code: The local build/control surface did not make station-event opt-out admission explicit and did not keep default STA-only callback binding out of the compiled controller path.
- fix justification path: SYSTEM_CONTRACT_FIX
- enumerated system-facing touchpoints: `IEEE80211_STA_ONLY`; `IEEE80211_OPT_OUT_STA_ONLY`; `IEEE80211_APSTA_STATION_EVENT_OPT_OUT`; `ieee80211_apsta_event_publish` producer call sites in `ieee80211_node_join` / `ieee80211_node_leave`; `ic_apsta_event_cb` / `ic_apsta_event_arg`; APSTA owner register/unregister lifetime; `scripts/build_tahoe.sh --opt-out`; role-7 create/delete fail-closed AP-up gate.
- expected contract at each touchpoint: default Tahoe builds keep `IEEE80211_STA_ONLY` and do not bind the APSTA callback; opt-out builds define the named station-event gate and may compile the producer bridge; legacy `IEEE80211_OPT_OUT_STA_ONLY` maps to the named gate for compatibility; callback register/unregister is paired only in the admitted gate; no callback registration changes AP-up, HAL support, AP profile, station producer, beacon/key, or runtime behavior.
- why no relevant touchpoints are missing: This slice is limited to admission for the existing net80211 station-event bridge after the AP/GO HAL method-surface layer. AP profile creation, beacon/key/station firmware producers, HostAP data path, DHCP, AP/client runtime, role-6/AWDL, NAN, and CR479 remain separate non-claims.
- why proposed path adds no extra system-visible side effects: Default builds remove the dormant callback binding rather than adding one. Opt-out builds only make the station-event admission explicit and preserve the existing register/unregister semantics; role-7 create still fails closed because the HAL backend does not advertise AP/GO support.
- why this is root cause and not just correlation: The absence of a named admission symbol and compile-time binding guard is the direct reason the station-event opt-out surface could not be distinguished from a broad HostAP opt-out in the exact submitted diff.
- why proposed fix is 1:1 with reference architecture and semantics: It keeps the recovered single-consumer station-event bridge tied to APSTA owner lifetime while preserving the local default STA-only build boundary until an explicit opt-out build admits that HostAP/net80211 station-event surface.
- files/functions to modify: `itl80211/openbsd/net80211/ieee80211_var.h`; `scripts/build_tahoe.sh`; `AirportItlwm/AirportItlwmV2.cpp`; tracked AP/APSTA analysis/reference/request artifacts.
- forbidden alternative fixes considered and rejected: broad AP runtime enablement was rejected; AP-up or role-7 success forcing was rejected; AP profile/beacon/key/station producer implementation was rejected as gated follow-up scope; callback binding in default STA-only builds was rejected; retry/poll/reorder/timing was rejected.
- verification plan: `git diff --check`, reverse-apply check of the refreshed AP/APSTA patch artifact, canonical guest Tahoe build, and Tahoe BootKC symbol check only. Install, reboot, kext load/unload, AP/client runtime, CR479 runtime, OpenCore mutation, after-fix runtime, and commit remain forbidden.

coder_decomp_completeness_self_check: YES
coder_payload_field_lifecycle_completeness_self_check: YES


## ANOMALY
- id: AP-APSTA-profile-beacon-key-station-producers-20260623
- status: FIX_IMPLEMENTED
- symptom: After scoped HostAP station-event admission, the current Tahoe/Skywalk APSTA structural surface still had no role-7 owner route for the recovered SAP profile/key/station producer slots: setSSID, setCHANNEL, setCIPHER_KEY, STA_AUTHORIZE, STA_DISASSOCIATE, and STA_DEAUTH. The APSTA owner had AP-up-gated beacon/key/station placeholders, but the public Apple80211 selector switch could not reach those owner/HAL producer paths.
- first visible manifestation: Static current-HEAD AP/APSTA review at `2b261c1567c1a96104cea691bba3fb095031fd44`; no runtime was authorized for this cycle.
- expected system behavior: The recovered SAP slots 517-523 must have APSTA owner/controller routes once a role-7 owner exists, with SSID/channel profile state mirrored into the APSTA state block and AP key/station commands reaching the HAL only after the AP-up gate succeeds. Default STA behavior must remain on the existing STA handlers when no APSTA owner exists, and unsupported Intel AP/GO backends must still fail closed.
- actual behavior: The Skywalk bridge routed SSID, CHANNEL, and CIPHER_KEY only through the STA path and did not route STA_AUTHORIZE, STA_DISASSOCIATE, or STA_DEAUTH. The APSTA owner did not persist SSID/channel profile input or translate the recovered station/key carriers into the HAL AP/GO command surface.
- divergence point: `AirportItlwmSkywalkInterface::processApple80211Ioctl` for `APPLE80211_IOC_SSID`, `APPLE80211_IOC_CHANNEL`, `APPLE80211_IOC_CIPHER_KEY`, `APPLE80211_IOC_STA_AUTHORIZE`, `APPLE80211_IOC_STA_DISASSOCIATE`, and `APPLE80211_IOC_STA_DEAUTH`; `AirportItlwmAPSTAOwner` profile/key/station producer methods.
- evidence:
  - docs: `include/Airport/IO80211SapProtocol.h` records recovered APSTA SAP slots 517-523 and concrete byte offsets 0x1028-0x1058; `AirportItlwmAPSTAInterface.hpp` records SSID state offsets +0x274/+0x278, channel carrier offsets, cipher-key AP-up gate, station-authorize/disassociate carrier offsets, station table layout, and AP event/station command constants.
  - decomp: Existing AP/APSTA closure package records the APSTA owner, AP-up gate at state +0x26c, SAP slot group, station table ownership, and HAL command ownership. No new runtime/instrumentation was authorized or needed for this structural routing slice.
  - runtime logs: Not applicable; this is Stage 1 structural only.
- candidate causes: The prior HostAP admission slice intentionally stopped at station-event callback admission and left the next SAP producer group unrouted.
- rejected causes: AP/GO firmware backend absence is not treated as success; no role-7 create success, AP client runtime, AP traffic, DHCP, OpenCore mutation, or CR479 behavior is claimed.
- confirmed deviation: Recovered SAP profile/key/station producer slots had no APSTA owner route in the Tahoe/Skywalk Apple80211 switch.
- root cause: Missing controller/owner/Skywalk routing for the next recovered APSTA SAP producer group.
- fix: Add APSTA owner profile methods for SSID and channel, AP key translation from `apple80211_key` into `ItlHalApKey`, station authorize/disassociate/deauth translation into `ItlHalApStationCommand`, and owner-backed station event HAL forwarding only when AP-up is true. Route Skywalk SSID/CHANNEL/CIPHER_KEY through the APSTA owner only when a role-7 owner exists, preserving existing STA handlers otherwise, and add fail-closed STA_AUTHORIZE/STA_DISASSOCIATE/STA_DEAUTH switch cases.
- verification: Default Tahoe build completed successfully in `/Users/devops/Projects/itlwm` and resolved all 932 undefined symbols against the Tahoe BootKC. Opt-out Tahoe build completed successfully and resolved all 934 undefined symbols against the Tahoe BootKC. No install, reboot, kext load/unload, OpenCore mutation, AP/client runtime, CR479 runtime, after-fix runtime, commit, or role-7 success claim was performed.
- notes: This is a structural AP/APSTA producer integration slice only. It preserves the prior fail-closed role-7 owner create/delete behavior and unrelated CR479 dirty work outside the AP/APSTA artifact.

## FIX_CANDIDATE

- anomaly_id: AP-APSTA-profile-beacon-key-station-producers-20260623
- symptom: Recovered APSTA profile/key/station SAP producer slots were not reachable from the Tahoe/Skywalk Apple80211 switch after HostAP station-event admission.
- expected system behavior: The APSTA owner owns role-7 AP profile state, AP key producer translation, station producer translation, beacon/profile interval state, and the AP-up gate. Lower HAL key/station/beacon commands run only after `startAPMode()` succeeds; default STA requests continue through STA handlers when no APSTA owner exists.
- actual behavior: The switch routed SSID/CHANNEL/CIPHER_KEY through STA handlers only and lacked STA_AUTHORIZE/STA_DISASSOCIATE/STA_DEAUTH APSTA dispatch.
- exact divergence point: `AirportItlwmSkywalkInterface::processApple80211Ioctl` selector cases and missing `AirportItlwmAPSTAOwner` profile/key/station producer methods.
- evidence from runtime: Not collected; runtime/install are forbidden for this structural request.
- evidence from decomp: `include/Airport/IO80211SapProtocol.h` slots 517-523; `AirportItlwmAPSTAInterface.hpp` APSTA SSID/channel/cipher/station carrier offsets, AP-up gate, and station table layout; existing AP/APSTA owner reference package.
- exact semantic mismatch between reference and our code: The local APSTA owner/controller path stopped before the recovered SAP producer group, so profile/key/station producers could not reach the owner/HAL AP-up-gated surface.
- fix justification path: SYSTEM_CONTRACT_FIX
- enumerated system-facing touchpoints: `APPLE80211_IOC_SSID` set path; `APPLE80211_IOC_CHANNEL` set path; `APPLE80211_IOC_CIPHER_KEY` set path; `APPLE80211_IOC_STA_AUTHORIZE`; `APPLE80211_IOC_STA_DISASSOCIATE`; `APPLE80211_IOC_STA_DEAUTH`; APSTA SSID state +0x274/+0x278; APSTA channel cache/start config; APSTA cipher-key AP-up gate; APSTA station table; net80211 station-event bridge; HAL `startAPMode`, `setAPKey`, `triggerAPCSA`, and `sendAPStationCommand` defaults.
- expected contract at each touchpoint: no owner means existing STA SSID/CHANNEL/CIPHER_KEY behavior remains unchanged or APSTA-only station producers return not-ready; owner-present SSID and channel update APSTA profile state; AP key and station HAL commands run only when `isApRunning()` is true; unsupported HAL defaults return unsupported/not-ready; station-event publication updates owner station state and forwards to HAL only after AP-up; role-7 create remains fail-closed on unsupported backends.
- why no relevant touchpoints are missing: This slice covers the recovered SAP producer group immediately after station-event admission. AP/GO firmware implementation, beacon template construction, HostAP data path, DHCP, AP traffic, AP client runtime, role-6/AWDL, NAN, and CR479 are explicit non-claims.
- why proposed path adds no extra system-visible side effects: Default STA paths are preserved when no APSTA owner exists. The current unsupported Intel AP/GO backends delete the owner on failed role-7 create, so the new producer routes cannot publish AP success today. HAL key/station/beacon-adjacent commands remain behind `isApRunning()`.
- why this is root cause and not just correlation: The absence of switch/controller/owner entries directly prevented the recovered SAP producer selectors from reaching the APSTA owner.
- why proposed fix is 1:1 with reference architecture and semantics: It follows the recovered SAP slot ordering and keeps AP firmware-visible work behind the same AP-up gate used by the reference APSTA owner.
- files/functions to modify: `AirportItlwm/AirportItlwmAPSTAOwner.cpp`; `AirportItlwm/AirportItlwmAPSTAOwner.hpp`; `AirportItlwm/AirportItlwmV2.cpp`; `AirportItlwm/AirportItlwmV2.hpp`; `AirportItlwm/AirportItlwmSkywalkInterface.cpp`; tracked AP/APSTA analysis/reference/request artifacts.
- forbidden alternative fixes considered and rejected: forced AP-up was rejected; forced role-7 success was rejected; routing all STA CIPHER_KEY requests to AP key installation without an APSTA owner was rejected; station command success without AP-up was rejected; retry/poll/reorder/timing was rejected; AP firmware backend implementation was rejected as gated follow-up scope.
- verification plan: `git diff --check`, reverse-apply check of the generated AP/APSTA patch artifact, default Tahoe build plus BootKC symbol check, and opt-out Tahoe build plus BootKC symbol check only. Install, reboot, kext load/unload, AP/client runtime, CR479 runtime, OpenCore mutation, after-fix runtime, and commit remain forbidden.

coder_decomp_completeness_self_check: YES
coder_payload_field_lifecycle_completeness_self_check: YES

## ANOMALY
- id: AP-APSTA-profile-producers-disassoc-deauth-carrier-rework-20260623
- status: FIX_IMPLEMENTED
- symptom: The rejected AP/APSTA profile/key/station producer Stage 1 package claimed STA_DISASSOCIATE and STA_DEAUTH producer translation, but the submitted HAL station command preserved only reason04 and dropped the recovered disassociate carrier value08/value0c plus payload value04/value08/sentinel leaves.
- first visible manifestation: `commit-approval/decisions/COMMIT_DECISION_AP-APSTA-profile-beacon-key-station-producers-stage1-20260623.md` rejected the Stage 1 package for incomplete STA_DISASSOCIATE/STA_DEAUTH carrier coverage.
- expected system behavior: The role-7 APSTA owner/controller station producer route must preserve the recovered disassociate/deauth carrier reason04/value08/value0c leaves and the derived firmware payload reason00/value04/value08/sentinel0a leaves across the AP-up-gated HAL station command boundary, or the request must prove those leaves non-observable.
- actual behavior: `AirportItlwmAPSTAOwner::setStationDisassociation()` forwarded only reason04 as `ItlHalApStationCommand::flags`; `ItlHalApStationCommand` had no fields for value08/value0c or the recovered payload leaves.
- divergence point: `AirportItlwmAPSTAOwner::setStationDisassociation()` and `ItlHalApStationCommand`.
- evidence:
  - docs: `AirportItlwm/AirportItlwmAPSTAInterface.hpp` records `AirportItlwmAPSTAStaDisassocInputLayout` reason04/value08/value0c offsets and `AirportItlwmAPSTAStaDisassocPayloadLayout` reason00/value04/value08/sentinel0a offsets and sentinel value 0xaaaa.
  - decomp: Existing AP/APSTA reference closure package records SAP slots 522/523 and the disassociate/deauth carrier/payload layouts as the station producer contract.
  - runtime logs: Not applicable; runtime/install remain forbidden for this Stage 1 structural rework.
- candidate causes: The HAL command shape was too narrow for the recovered station disassociate/deauth producer payload.
- rejected causes: No reference proof was available that value08/value0c or payload value04/value08/sentinel leaves are non-observable, so narrowing to reason-only was rejected.
- confirmed deviation: The local station command surface discarded recovered disassociate/deauth leaves inside a claimed station producer translation.
- root cause: `ItlHalApStationCommand` lacked disassociate/deauth carrier and payload fields, and the owner did not populate them.
- fix: Extend `ItlHalApStationCommand` with disassociate/deauth carrier fields and payload fields, and populate them in `setStationDisassociation()` as reason04 -> reason/payload reason00, value08 -> carrier value08/payload value04, value0c -> carrier value0c/payload value08, and sentinel0a -> 0xaaaa. Existing AP-up gating, unsupported HAL defaults, and no-owner fail-closed behavior are unchanged.
- verification: Pending regenerated AP/APSTA patch artifact, diff checks, default Tahoe build, opt-out Tahoe build, and BootKC symbol verification. No install, reboot, kext load/unload, AP/client runtime, CR479 runtime, after-fix runtime, OpenCore mutation, commit, firmware backend success, or role-7 success claim is authorized.
- notes: This rework preserves the broader AP/APSTA Stage 1 package and unrelated CR479 dirty work outside the AP/APSTA artifact.

## FIX_CANDIDATE

- anomaly_id: AP-APSTA-profile-producers-disassoc-deauth-carrier-rework-20260623
- symptom: STA_DISASSOCIATE/STA_DEAUTH station producer translation dropped recovered carrier and payload leaves.
- expected system behavior: Station disassociate/deauth translation preserves reason04/value08/value0c and the derived reason00/value04/value08/sentinel0a payload leaves through the AP-up-gated HAL station command surface.
- actual behavior: Only reason04 reached `ItlHalApStationCommand::flags`.
- exact divergence point: `AirportItlwmAPSTAOwner::setStationDisassociation()` and `ItlHalApStationCommand`.
- evidence from runtime: Not collected; Stage 1 structural scope only.
- evidence from decomp: `AirportItlwm/AirportItlwmAPSTAInterface.hpp` recovered disassociate/deauth carrier and payload layouts plus SAP slots 522/523 in `include/Airport/IO80211SapProtocol.h`.
- exact semantic mismatch between reference and our code: The local HAL station command boundary could not carry all recovered disassociate/deauth producer leaves included in the submitted system contract.
- fix justification path: SYSTEM_CONTRACT_FIX
- enumerated system-facing touchpoints: `APPLE80211_IOC_STA_DISASSOCIATE`; `APPLE80211_IOC_STA_DEAUTH`; APSTA owner AP-up gate; `AirportItlwmAPSTAStaDisassocInputLayout` reason04/value08/value0c; `AirportItlwmAPSTAStaDisassocPayloadLayout` reason00/value04/value08/sentinel0a; `ItlHalApStationCommand`; HAL `sendAPStationCommand` default unsupported path.
- expected contract at each touchpoint: absent owner or AP-down returns not-ready; AP-up owner translates every recovered carrier leaf into the HAL command; the derived payload leaves remain available to future backend firmware translation; default unsupported HAL still returns unsupported and does not claim AP/GO firmware success.
- why no relevant touchpoints are missing: This rework is limited to the auditor-identified disassociate/deauth station producer gap inside the already submitted profile/key/station producer package; SSID/channel/key/authorize/event routing, HostAP admission, AP/GO backend implementation, beacon emission, AP client runtime, DHCP, traffic, role-6/AWDL, NAN, and CR479 remain separate non-claims.
- why proposed path adds no extra system-visible side effects: The patch only adds fields to the borrowed HAL command object and populates them behind the existing AP-up gate. It does not allocate, start, retry, publish, force success, change return codes, or bypass unsupported HAL defaults.
- why this is root cause and not just correlation: The rejected Stage 1 diff directly showed a command object with no fields for the recovered leaves and an owner method that assigned only reason04.
- why proposed fix is 1:1 with reference architecture and semantics: It preserves the recovered carrier and payload layout leaves at the owner-to-HAL boundary without changing the owner/lower-backend split or AP-up gate.
- files/functions to modify: `include/HAL/ItlHalService.hpp`; `AirportItlwm/AirportItlwmAPSTAOwner.cpp`; tracked AP/APSTA analysis/reference/request artifacts; regenerated AP/APSTA patch/build evidence.
- forbidden alternative fixes considered and rejected: reason-only narrowing was rejected without direct reference proof; forced AP-up or role-7 success was rejected; fallback/retry/poll/reorder/timing was rejected; status-only artifact repair was rejected.
- verification plan: `git diff --check`, reverse-apply check of the refreshed AP/APSTA patch artifact, canonical guest default Tahoe build, opt-out Tahoe build, and Tahoe BootKC symbol checks only. Install, reboot, kext load/unload, AP/client runtime, CR479 runtime, OpenCore mutation, after-fix runtime, commit, firmware backend success, and role-7 success claims remain forbidden.

coder_decomp_completeness_self_check: YES
coder_payload_field_lifecycle_completeness_self_check: YES

## ANOMALY
- id: AP-APSTA-intel-apgo-firmware-backend-stage1-20260623
- status: FIX_IMPLEMENTED
- symptom: After APSTA profile/key/station producer routing, the Intel iwn HAL still exposed only the parent fail-closed AP/GO backend surface and had no local firmware RXON HOSTAP payload owner for a future `startAPMode()` backend.
- first visible manifestation: Static current-HEAD AP/APSTA review at `2b261c1567c1a96104cea691bba3fb095031fd44`; no runtime was authorized for this cycle.
- expected system behavior: The APSTA owner-to-HAL `startAPMode()` boundary must have an Intel backend owner that can validate `ItlHalApConfig`, compose the firmware AP RXON context from BSSID/channel/filter/rxchain fields, and remain fail-closed unless an explicit backend admission gate is enabled.
- actual behavior: `ItlIwn` inherited the default `ItlHalService` AP/GO methods, so the iwn backend had no owned payload builder or HAL override behind the APSTA producer routes.
- divergence point: `itlwm/hal_iwn/ItlIwn.hpp` and `itlwm/hal_iwn/ItlIwn.cpp` had no AP/GO HAL overrides and `iwn_config()` handled only STA and monitor RXON modes despite the local firmware register header defining `IWN_MODE_HOSTAP` and `IWN_FILTER_BEACON`.
- evidence:
  - docs: `docs/reference/AP_GO_HAL_SURFACE_2026_05_09.md` maps `startAPMode()` to AP MAC context add/configuration and states that backends must remain fail-closed until they advertise AP/GO support. `docs/reference/AppleBCMWLAN_APSTA_OWNER_SKELETON_2026_05_09.md` records the APSTA owner/lower-backend split and the prior producer integration layers.
  - decomp: Existing AP/APSTA reference closure package records AP-up gating, AP MAC context ownership, and station/key/beacon producer ownership. No new runtime/instrumentation was authorized or needed for this backend payload-owner slice.
  - source/donor: `itlwm/hal_iwn/if_iwnreg.h` defines `IWN_MODE_HOSTAP`, `IWN_FILTER_BSS`, `IWN_FILTER_BEACON`, and `IWN_CMD_RXON`; local `iwn_config()` already uses the same RXON field family for STA/monitor configuration.
  - runtime logs: Not applicable; this is Stage 1 structural only.
- candidate causes: The previous APSTA producer package intentionally stopped at owner/controller/HAL producer routing and left the Intel firmware backend owner absent.
- rejected causes: AP/GO runtime failure is not claimed or tested; role-7 create success is not claimed; beacon emission, client association, DHCP, AP traffic, and CR479 behavior are separate non-claims.
- confirmed deviation: The iwn backend had no AP/GO HAL override or AP RXON payload builder corresponding to the recovered `startAPMode()` backend contract.
- root cause: Missing iwn-local AP/GO HAL backend boundary for validating `ItlHalApConfig` and composing a firmware HOSTAP RXON context.
- fix: Add `ItlIwn::supportsAPMode()`, `startAPMode()`, `stopAPMode()`, and `iwn_build_ap_rxon()`. The helper validates channel and BSSID, finds the local channel, builds an `IWN_MODE_HOSTAP` RXON payload with AP BSSID/WLAP, BSS/beacon/multicast filters, rate masks, and rxchain fields. `supportsAPMode()` remains false in default and current opt-out builds because `IWN_APGO_FIRMWARE_BACKEND_OPT_IN` is not defined, so current role-7 create still fails closed and no AP-up state is exposed.
- verification: `git diff --check` passed for the path-limited AP/APSTA diff. Default Tahoe build completed successfully in the canonical guest repository and resolved all 932 undefined symbols against the Tahoe BootKC. Opt-out Tahoe build completed successfully and resolved all 934 undefined symbols against the Tahoe BootKC. No install, reboot, kext load/unload, OpenCore mutation, AP/client runtime, CR479 runtime, after-fix runtime, commit, or role-7 success claim was performed.
- notes: This backend slice advances the iwn firmware boundary only. It intentionally leaves backend admission off, does not define `IWN_APGO_FIRMWARE_BACKEND_OPT_IN`, and preserves the unsupported result for current paths.

## FIX_CANDIDATE

- anomaly_id: AP-APSTA-intel-apgo-firmware-backend-stage1-20260623
- symptom: The APSTA producer integration reached the HAL surface, but the Intel iwn backend had no AP/GO firmware payload owner behind `startAPMode()`.
- expected system behavior: AP/GO backend bring-up has a concrete Intel owner that validates the AP config and composes a firmware AP MAC/RXON context while refusing to advertise support until the backend admission prerequisites are explicitly enabled.
- actual behavior: `ItlIwn` inherited parent defaults and therefore could only return unsupported with no backend payload builder.
- exact divergence point: Missing `ItlIwn` AP/GO HAL overrides and missing iwn HOSTAP RXON builder.
- evidence from runtime: Not collected; runtime/install are forbidden for this Stage 1 structural request.
- evidence from decomp: Existing AP/APSTA reference closure package plus `AP_GO_HAL_SURFACE_2026_05_09.md` establish `startAPMode()` as the AP MAC context/backend boundary and require fail-closed backend capability gating.
- exact semantic mismatch between reference and our code: The local APSTA owner could route profile/key/station producers to a HAL AP/GO surface, but the Intel iwn backend did not own the first firmware context payload that the AP-up transition requires.
- fix justification path: SYSTEM_CONTRACT_FIX
- enumerated system-facing touchpoints: `ItlHalService::supportsAPMode()`; `ItlHalService::startAPMode()`; `ItlHalService::stopAPMode()`; `ItlIwn::supportsAPMode()`; `ItlIwn::startAPMode()`; `ItlIwn::stopAPMode()`; `ItlIwn::iwn_build_ap_rxon()`; `ItlHalApConfig` BSSID/channel/beacon/max-station carrier; `IWN_CMD_RXON`; `IWN_MODE_HOSTAP`; `IWN_FILTER_BSS`; `IWN_FILTER_BEACON`; APSTA role-7 create lower-start gate.
- expected contract at each touchpoint: current builds do not advertise AP/GO support; `startAPMode()` returns unsupported before any config validation or firmware command while admission is off; admitted future builds validate non-null config, nonzero valid channel, and unicast nonzero BSSID before composing the AP RXON payload; `stopAPMode()` remains re-entry-safe and unsupported while admission is off; role-7 create still deletes the owner and returns unsupported when the lower backend is not admitted.
- why no relevant touchpoints are missing: This slice is limited to the iwn AP RXON backend boundary after APSTA producer integration. It does not enable net80211 HostAP runtime, beacon template upload, AP key firmware install, station firmware add/remove, CSA, AP datapath queues, client association, DHCP, AP traffic, role-7 success, iwx/iwm backend support, or CR479 behavior.
- why proposed path adds no extra system-visible side effects: Default and opt-out builds leave `IWN_APGO_FIRMWARE_BACKEND_OPT_IN` undefined, so `supportsAPMode()` returns false and `startAPMode()` returns unsupported before touching `sc->rxon` or firmware. The new HOSTAP RXON builder is only reachable behind a future explicit admission gate.
- why this is root cause and not just correlation: The absence of an iwn AP/GO override is the direct reason the APSTA owner-to-HAL `startAPMode()` boundary had no Intel firmware payload owner after producer routing.
- why proposed fix is 1:1 with reference architecture and semantics: It preserves the recovered owner/lower-backend split, adds a backend AP MAC/RXON payload boundary corresponding to the `startAPMode()` contract, and keeps AP-up success gated on explicit backend capability rather than owner allocation.
- files/functions to modify: `itlwm/hal_iwn/ItlIwn.hpp`; `itlwm/hal_iwn/ItlIwn.cpp`; tracked AP/APSTA analysis/reference/request artifacts.
- forbidden alternative fixes considered and rejected: forced `supportsAPMode()` success was rejected; defining the backend opt-in macro in the current build was rejected; setting `ic_opmode` to HOSTAP globally was rejected; forcing role-7 create success was rejected; fake AP-up state was rejected; retry/poll/reorder/timing was rejected; beacon/key/station firmware implementation was rejected as a later layer.
- verification plan: `git diff --check`, reverse-apply check of the refreshed AP/APSTA patch artifact, canonical guest Tahoe build, and Tahoe BootKC symbol check only. Install, reboot, kext load/unload, AP/client runtime, CR479 runtime, OpenCore mutation, after-fix runtime, commit, role-7 success, and terminal project completion remain forbidden.

coder_decomp_completeness_self_check: YES
coder_payload_field_lifecycle_completeness_self_check: YES
