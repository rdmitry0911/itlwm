# CR-479 pre-M1 CWWiFiClient join-delegate Stage 1 diagnostic (rev8)

request_id: CR-479-stage1-cwwificlient-join-delegate-diagnostic-rev8-20260518
supersedes:
  - CR-479-stage1-local-pmk-ingress-producer-cwwificlient-rev7-20260517
    (REJECTED at Stage 1; missing producer-side contract closure
    and missing cited decomp/runtime evidence; auditor explicitly
    offered DIAGNOSTIC_INSTRUMENTATION as an acceptable alternative
    scope)
  - CR-479-stage1-local-pmk-ingress-producer-cwwificlient-rev6-20260517
  - CR-479-stage1-local-pmk-ingress-producer-cwwificlient-rev5-20260517
correlation_id: CR-479-stage1-local-pmk-ingress-producer-cwwificlient-rev7-20260517
stage1_review_class: DIAGNOSTIC_INSTRUMENTATION (behavior-neutral)
allow_after_fix_runtime_target: STAGE_2 (pending Stage 1 acceptance)
commit_now_target: NO (diagnostic only; semantic delivery is a
  later request once joinDidStart firing is empirically confirmed)

## Why this revision exists

Rev7 was REJECTED at Stage 1 (see
commit-approval/decisions/COMMIT_DECISION_CR-479-stage1-local-pmk-ingress-producer-cwwificlient-rev7-20260517.md)
on three independent grounds:

1. Several decomp/runtime artifacts that the rev7 request cited as
   evidence were absent from the canonical guest checkout:
   - analysis/CR-479-pre-m1-target-trigger-reference-coverage-20260517.md
   - analysis/CR-479-pre-m1-trigger-airportd-producer-20260517.md
   - analysis/CR-479-pre-m1-trigger-cwwificlient-joindid-bodies-20260517.md
   - commit-approval/runtime_evidence/CR-479-pre-m1-trigger-rev5-stage1-prep-20260517/helper_build_rev5.log
2. The airportd producer body / XPC publication / restart-replay /
   entitlement surface for -[CWWiFiClient setJoinStartedEvent:withReason:deviceName:]
   was deferred to Stage 2 follow-up research, but the request asked
   for SYSTEM_CONTRACT_FIX approval. The auditor noted producer-side
   contract is current scope for that change class.
3. The cited rev4 helper-runtime/build evidence directory was not
   accessible in the current guest tree, so the build-and-trigger
   claim could not be tied to the exact reviewed artifact.

The auditor explicitly noted in REQUIRED_CHANGES_BEFORE_RESUBMISSION:

> If the intent is only to collect runtime facts about whether
> CWWiFiClient callbacks arrive, submit a DIAGNOSTIC_INSTRUMENTATION
> request or a bounded decomp/research request instead of a semantic
> PMK-delivery producer patch.

This rev8 accepts that suggestion. It narrows the patch to a
behavior-neutral observer and abandons the semantic PMK delivery
claim until Stage 2 runtime confirms (or disproves) that
-[CWWiFiClient joinDidStartForWiFiInterfaceWithName:ssid:] fires on
the live iwx Tahoe path before the kext receives first M1.

## Single hypothesis being disambiguated

> H_DIAG: -[CWWiFiClient joinDidStartForWiFiInterfaceWithName:ssid:]
> is invoked on a registered delegate during the live macOS Tahoe
> 26.2 iwx Wi-Fi association window, carrying a non-empty ssid
> NSString, before the kext receives the first M1 EAPOL frame.

Outcomes:

- If Stage 2 runtime captures a joinDidStart os_log entry BEFORE the
  kext logs first-M1 reception, H_DIAG is confirmed and a follow-on
  SYSTEM_CONTRACT_FIX request will reintroduce the PMK derivation/
  delivery pipeline together with the airportd producer-contract
  closure the rev7 review demanded.
- If Stage 2 runtime captures NO joinDidStart os_log entry during
  the pre-first-M1 window despite a normal WPA2 connection attempt,
  H_DIAG is empirically disproved on this build, and the CWWiFiClient
  join-delegate path joins the rev4 SCDynamicStore SSID_STR path as a
  rejected trigger source. A new pre-M1 trigger hypothesis must be
  raised.
- If joinDidStart fires but always with a nil/empty ssid, the helper
  must be redesigned to obtain the ssid out of band.

## Probe points

The diagnostic registers a CWWiFiClient delegate via the public
-[CWWiFiClient sharedWiFiClient]/-[CWWiFiClient setDelegate:] surface
and implements:

- -joinDidStartForWiFiInterfaceWithName:ssid:
- -joinDidCompleteForWiFiInterfaceWithName:isAutoJoin:error:
- -autoJoinDidStartForWiFiInterfaceWithName:
- -autoJoinDidCompleteForWiFiInterfaceWithName:
- -autoJoinDidUpdate:

Each handler emits a single os_log line through the
AirportItlwmAgent: subsystem prefix:

- joinDidStart: iface=%{public}s ssid=%{private}s
- joinDidComplete: iface=%{public}s isAutoJoin=%d error=%{public}s
- autoJoinDidStart: iface=%{public}s
- autoJoinDidComplete: iface=%{public}s
- autoJoinDidUpdate: keys=%lu

These probe points are sufficient because the rev7 design depended
on exactly one of these callbacks (-joinDidStartForWiFiInterfaceWithName:ssid:)
carrying the ssid before the kext receives first M1. The four
adjacent join lifecycle callbacks are observed too so the timing of
joinDidStart can be cross-correlated against autoJoin* lifecycle and
joinDidComplete, and so that a missing joinDidStart is unambiguously
distinguishable from a delegate registration failure (which would
also suppress autoJoinDidUpdate).

The setMonitoringEventWithType: subscription gate, the airportd XPC
producer body, and the entitlement/restart-replay path are NOT probed
in this diagnostic. The rev7 reviewer-cited summary (selectors at
addresses 0x7ff8115a4e03 / 0x7ff8115a3446 / 0x7ff8115a34bd /
0x7ff8115a3518) shows that CoreWLAN dispatches these join-delegate
methods via objc_opt_respondsToSelector + objc_msgSend on every
internal callback without requiring an explicit event-type
subscription, so the bare delegate slot is the minimal sufficient
hook for H_DIAG.

## Why this is behavior-neutral

The helper:

- never reads or writes the System keychain (no SecItemCopyMatching,
  no SecItemAdd, no SecAccessRef construction);
- never derives any key material (no PBKDF2, no HMAC-SHA1, no
  SecKeyDerive);
- never opens an IOServiceClient on the kext or any other driver
  (no IOServiceMatching, no IOServiceOpen, no IOConnectCallMethod);
- never publishes any SCDynamicStore key, no NSXPCConnection,
  no DistributedNotificationCenter post, no Mach port registration
  beyond the implicit os_log + launchd handshake every LaunchDaemon
  has;
- never reads ssid bytes off the wire (the diagnostic only observes
  the NSString argument CoreWLAN passes into the delegate method).

The only system-visible side effects are:

- launchd records a com.zxystd.airportitlwmagent service registration
  on first install (same boundary as any LaunchDaemon);
- CoreWLAN holds a non-retaining +0x38 delegate-slot pointer to a
  process-owned NSObject (this is the public delegate API of
  CWWiFiClient and is the exact contract Apple uses internally);
- log entries are emitted to the unified logging system under the
  AirportItlwmAgent: prefix.

There is no path by which the helper can mutate any kext state, any
80211 association state, any credential storage, any user setting,
or any networking decision. The CWWiFiClient delegate dispatch is
read-only: CoreWLAN invokes the delegate methods on its own queue;
the methods do not return any value that CoreWLAN consults.

## Stage 1 evidence the diagnostic relies on (durable, in-tree)

These are evidence files that currently exist in the canonical guest
checkout (verified at the rev7 reviewed HEAD
09d43e2f3ed742fab6cc2d52d9cf7adf103dd8c5):

- docs/reference/CR-479-next-layer-external-supplicant-pmk-delivery-static-closure-20260517.md
  (the accepted FULL_DECOMP closure on the PMK ingress carriers and
  the public CoreWLAN setDelegate slot)
- docs/reference/CR-479-event-payload-three-distinct-abis-closure-20260517.md
  (related FULL_DECOMP closure on the supplicant event ABIs)
- analysis/CR-479-wcl-owner-aware-first-m1-routing-20260517.md
  (the M1 owner-routing layer the prior d43f4d9d commit already
  landed; not modified or claimed by this diagnostic)

This diagnostic does NOT re-cite the rev7 missing-file paths. It does
NOT claim the airportd setJoinStartedEvent producer body, the XPC
publication ABI, or the entitlement surface; it only claims that the
public CoreWLAN delegate slot is the documented receive side for
join-state changes, which is already on file in the FULL_DECOMP
closure above. The producer-side contract closure remains an open
follow-up; it is NOT a precondition for a behavior-neutral
DIAGNOSTIC_INSTRUMENTATION request, because the diagnostic does not
exercise SYSTEM_CONTRACT_FIX semantics.

## Runtime evidence expected from this instrumentation (Stage 2)

Stage 2 after-fix runtime, run after Stage 1 acceptance, should:

1. Build the helper from the exact reviewed source.
2. Install + bootstrap the LaunchDaemon on the Tahoe guest.
3. Trigger a fresh WPA2 association attempt against the controlled
   test AP (CONTROL_STA_NETWORK; redacted SSID/password from
   policy.connection_test profile).
4. Capture the unified log around the association attempt:
     sudo log show --last 5m \
       --predicate 'process == "AirportItlwmAgent" OR process == "airportd" OR subsystem == "com.apple.network.eapol" OR senderImagePath CONTAINS "itl80211"'
5. Build a timeline of:
     - LaunchDaemon start
     - delegate-registered log line
     - airportd ASSOC request / Will associate to ...
     - joinDidStart os_log entry (or its absence)
     - kext ASSOC IOCTL receipt
     - first M1 reception in the kext
     - joinDidComplete os_log entry
6. Compute the relative ordering of the joinDidStart event vs. the
   kext first-M1 timestamp.

The structural review of this rev8 request does NOT require the
Stage 2 timeline; that is the after-fix evidence package the next
request stage will deliver.

## Forbidden alternatives considered and rejected for this rev8

- heuristic timing: not used; the diagnostic is event-driven by the
  CoreWLAN delegate dispatch.
- fallback path: not used; if [CWWiFiClient sharedWiFiClient] returns
  nil the daemon logs an error and exits non-zero rather than
  fabricating a delegate.
- masking / suppression: not used; the diagnostic adds log lines
  only.
- force callback / state / success: not used; the diagnostic never
  calls into CoreWLAN beyond setDelegate:.
- forced sync / flush / barrier: not used; the diagnostic does not
  participate in any synchronization beyond the autoreleasepool
  scope around the run loop.
- retry / reorder / poll loop: not used; setDelegate: is called once
  at daemon start.
- raw key bytes in logs: not possible; the diagnostic never obtains
  any key material.

## Scope summary

- changed files (8): AirportItlwmAgent/.gitignore,
  AirportItlwmAgent/Makefile,
  AirportItlwmAgent/com.zxystd.airportitlwmagent.plist,
  AirportItlwmAgent/scripts/install.sh,
  AirportItlwmAgent/scripts/uninstall.sh,
  AirportItlwmAgent/src/log.h,
  AirportItlwmAgent/src/main.m,
  analysis/CR-479-pre-m1-trigger-cwwificlient-joindid-stage1-diagnostic-20260518.md
- changed files (in docs/reference/ mirror, 1):
  docs/reference/CR-479-pre-m1-trigger-cwwificlient-joindid-stage1-diagnostic-20260518.md
- total: 9 add-only files; no modifications to existing kext or
  userland code.

## Residual uncertainty

- It remains untested whether [CWWiFiClient sharedWiFiClient] is
  reliably non-nil during the LaunchDaemon early-boot window before
  the WindowServer/loginwindow stack initializes the CoreWLAN
  framework. Stage 2 will record whether the daemon logs the
  "delegate registered" line at all and at what relative timestamp
  vs. airportd ASSOC.
- The CoreWLAN delegate-dispatch path on this build has only been
  characterized by the prior FULL_DECOMP closure summary; the
  airportd-side producer body is intentionally NOT covered by this
  diagnostic and remains an open item for a later
  SYSTEM_CONTRACT_FIX request.
- If joinDidStart fires only after the kext sees first M1
  (auto-reconnect path), the diagnostic still records this and the
  next request must reclassify the trigger window.

## Stage 2 runtime result (recorded 2026-05-18)

This diagnostic was approved at Stage 1 on 2026-05-18 (decision
commit-approval/decisions/COMMIT_DECISION_CR-479-stage1-cwwificlient-join-delegate-diagnostic-rev9-20260518.md,
status APPROVED_FOR_AFTER_FIX_RUNTIME, allow_after_fix_runtime: YES)
and run on the canonical guest at HEAD
09d43e2f3ed742fab6cc2d52d9cf7adf103dd8c5. The full Stage 2 runtime
evidence package is preserved under
commit-approval/runtime_evidence/CR-479-stage2-after-fix-runtime-cwwificlient-join-delegate-diagnostic-rev9-20260518/
(MANIFEST sha256 c82b15f01bd4fbcd22edcb3ec0dae5f3cf02e0a66f3c052d73c9fd168cda6eed,
25 files).

### Result: H_DIAG EMPIRICALLY DISPROVED

In one fresh WPA2 association attempt against the controlled
diagnostic lab AP (alias <FAST_LAB_AP_SSID>, WPA2-PSK CCMP,
channel 6 on 2.4 GHz, channel-bandwidth 20 MHz):

- AirportItlwmAgent started at 2026-05-18T08:33:01.136+0300 as
  PID 7227 under launchd; the helper attached its delegate via
  -[CWWiFiClient setDelegate:] at 2026-05-18T08:33:01.137+0300.
  Both lines were captured through os_log under the
  AirportItlwmAgent: subsystem prefix.
- airportd entered the documented association path on the iwx
  Tahoe target:
  - 2026-05-18T08:33:28.692+0300: "Assoc: Will associate to
    [ssid=<FAST_LAB_AP_SSID>, bssid=(null), channel=(channel=6,
    width=20), ibss=no, cc=(null), rssi=0, rsn=(mcast=aes_ccm,
    ucast={ aes_ccm }, auths={ psk }, caps=0xc), wpa=(null),
    wep=no], hasPassword=1"
  - 2026-05-18T08:33:38.865+0300: "Join timed out and not the
    last attempt, stop EAPOL and disassoc"
- networksetup returned Error -3912 (operation could not be
  completed) ~21 s after issue.
- During the 120 s unified-log capture window covering the
  association attempt:
  - joinDidStartForWiFiInterfaceWithName:ssid:,
    joinDidCompleteForWiFiInterfaceWithName:isAutoJoin:error:,
    autoJoinDidStartForWiFiInterfaceWithName:,
    autoJoinDidCompleteForWiFiInterfaceWithName:, and
    autoJoinDidUpdate: were NEVER observed on the registered
    helper delegate. The helper emitted exactly the two startup
    lines and nothing further.
  - The kext-side first-M1 EAPOL marker NEVER appeared (no msg1,
    no "M1", no "4-way", no fourway string).
  - The unified-log scan also found ZERO occurrences of the
    strings "joinDidStart" / "setJoinStartedEvent" /
    "setJoinComplete" from any process in the capture window.

The relative ordering against the auditor-required reference
points is therefore:

  point                                       timestamp                 status
  ------------------------------------------- ------------------------- --------------
  helper start                                2026-05-18T08:33:01.136   OBSERVED
  helper delegate registered                  2026-05-18T08:33:01.137   OBSERVED
  airportd Will associate (pre-ASSOC IOCTL)   2026-05-18T08:33:28.692   OBSERVED
  joinDidStart on helper delegate             n/a                       NEVER OBSERVED
  kext first M1 EAPOL reception               n/a                       NEVER OBSERVED
  airportd Join timed out                     2026-05-18T08:33:38.865   OBSERVED
  joinDidComplete on helper delegate          n/a                       NEVER OBSERVED

### Implication

The rev7 SYSTEM_CONTRACT_FIX trigger hypothesis (the public
CWWiFiClient join-delegate as the viable replacement for the
already-empirically-disproved rev4 SCDynamicStore SSID_STR trigger)
is now also empirically disproved on this build/path.

Candidate explanations the next research/decomp cycle should
consider (carried forward from the rev9 design doc; reinforced by
the Stage 2 auditor decision
commit-approval/decisions/COMMIT_DECISION_CR-479-stage2-after-fix-runtime-cwwificlient-join-delegate-diagnostic-rev9-rev2-20260518.md):

1. CoreWLAN may gate join-delegate dispatch behind an explicit
   -[CWWiFiClient startMonitoringEventWithType:error:] event-type
   subscription that the rev7 design assumed unnecessary.
2. The airportd producer selector
   -[airportd setJoinStartedEvent:withReason:deviceName:] may
   only fire post-association (after a successful 4-way), not at
   the pre-ASSOC-IOCTL boundary the diagnostic was watching.
3. The dispatch may be guarded by an Apple-private entitlement
   that an ad-hoc-codesigned LaunchDaemon does not possess.
4. CoreWLAN on macOS Tahoe 26.2 may dispatch via a different /
   renamed / renumbered selector than the rev7 design assumed.

Any future SYSTEM_CONTRACT_FIX request that proposes a userland
producer of pre-first-M1 PMK MUST first close the airportd
producer body / XPC publication / restart-replay / entitlement
contract before submitting a semantic patch, per both the rev7
Stage 1 reviewer rejection and the rev9-rev2 Stage 2 reviewer
commentary.

### Why this diagnostic remains in-tree

The helper is preserved under AirportItlwmAgent/ as a reusable
observer-only LaunchDaemon. If Apple updates airportd / CoreWLAN
in a future macOS build, the same diagnostic can be re-deployed
without re-coding to retest H_DIAG against the updated framework
surface. The negative finding above is the durable result of the
current (Tahoe 26.2 build 25C56, itlwm 2.4.0 at 09d43e2f3...c5)
run; subsequent runs would be filed as new Stage 2 evidence
packages under their own runtime_evidence/ subdirectories.
