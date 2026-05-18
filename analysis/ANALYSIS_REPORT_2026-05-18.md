# ANALYSIS_REPORT 2026-05-18 — CR-479 project-owned PLTI PMK producer (Stage 1)

This report supplements
`analysis/CR-479-follow-up-cwlocationclient-eventtype-entitlement-publish-payload-replay-decomp-20260518.md`
(rev2) and records the IMPLEMENT_LOCAL functional source change
landed on basis commit
`5213e180d019fb5c46cae7343db5ba5b6bcd2fa4` for Stage 1 structural
review.

## ANOMALY
- id: CR-479-stage1-project-owned-plti-pmk-producer-kext-trigger-20260518
- status: FIX_IMPLEMENTED
- symptom:
  - The Tahoe iwx PSK association edge never receives the
    32-byte WPA2 PMK before the first 4-way M1, because no
    Apple/userland actor delivers one on this build:
    - airportd's own setJoinStartedEvent producer never reaches
      our ad-hoc-signed daemon (HARD entitlement gate at
      `__verifyEntitlementForEventType:` for eventType 0x6d on
      `com.apple.wifi.events.private`, per rev2 decomp);
    - no replay/late-subscriber path exists in the 22 named
      CWXPCSubsystem helper-class init bodies (rev2
      `initWithScheduler_slots.tsv` + `airportd_classref_targets.tsv`);
    - the PLTI DeliverPMK ingress carrier already exists
      (CR-239..CR-245) but nothing was driving it.
  - Net effect: `ic_psk` stays zero, the OpenBSD net80211 host
    PAE consumes the first M1 with no PMK, and the AP eventually
    deauths the STA.
- first visible manifestation:
  - 2026-05-11 .. 2026-05-17 runtime evidence cycles on
    CONTROL_STA_NETWORK: repeated M1 arrival followed by
    deauth/reassociation loops with `install_external_pmk` never
    firing.
- expected system behavior:
  - Before the first M1, some root-privileged actor delivers the
    PSK-derived PMK to the kext PMK sink (any of the documented
    Apple carriers: CIPHER_KEY(PMK), CUR_PMK, or our project-owned
    PLTI DeliverPMK).
- actual behavior (pre-fix):
  - No producer reaches the PLTI sink. CIPHER_KEY(PMK) /
    CUR_PMK are not driven on Tahoe iwx because airportd never
    derives a PMK for this driver. The ic_psk store stays empty.
- divergence point:
  - Producer-side: absent. The CR-479 rev2 evidence proved the
    Apple producer path through CoreLocationd/airportd is HARD-
    gated to entitled-only consumers, so no Apple producer will
    ever reach our helper. The project must own the producer.
- evidence:
  - decomp:
    - `analysis/CR-479-follow-up-cwlocationclient-eventtype-entitlement-publish-payload-replay-decomp-20260518.md`
      (rev2) — 22-slot CWXPCSubsystem helper class identity map,
      bitmap-decoded entitlement gate at airportd 0x1000c607f,
      producer-arg tuple (event-int, reason-NSString,
      deviceName-NSString), consumer-delegate-arg shape, and the
      inferred-absent replay finding;
    - decomp-host evidence under
      `commit-approval/runtime_evidence/CR-479-follow-up-cwlocationclient-eventtype-entitlement-publish-payload-replay-decomp-20260518/`
      including `initWithScheduler_slots.tsv`,
      `airportd_classref_targets.tsv`,
      `corewlan_setclasses_targets.tsv`.
  - runtime logs:
    - prior rev9/rev10 CWWiFiClient diagnostic Stage 2 cycles
      confirmed the delegate callback never fires on the live
      iwx Tahoe path (negative result, fully captured under
      `commit-approval/runtime_evidence/CR-479-stage1-cwwificlient-join-delegate-diagnostic-rev8-20260518/`).
  - docs:
    - `docs/AGENT_EXECUTION_PROTOCOL_ITLWM.md` — SYSTEM_CONTRACT_FIX
      requires every relevant touchpoint be covered without
      adding system-visible side effects;
    - `docs/COMMIT_REQUEST_TEMPLATE_ITLWM.md` — Stage 1 / Stage 2
      flow used for this request.
- candidate causes:
  - CONFIRMED_ROOT_CAUSE: no project-side producer drives the
    PLTI PMK sink, and the Apple producer path is unreachable
    for project helpers. CR-479 rev2 decomp closed the producer
    side; the missing piece is functional.
- rejected causes:
  - CWWiFiClient join delegate trigger (rejected: HARD
    entitlement gate at airportd, decisive rev1+rev2 evidence);
  - replay/late-subscriber catch-up on subscribe (rejected:
    inferred-absent finding strengthened by rev2 full 22-slot
    class-init inventory);
  - codesign tampering / fixup_chains rewrites / amfid bypass
    to claim `com.apple.wifi.events.private` (rejected: out of
    scope and contrary to the project's no-heuristics rule).
- confirmed deviation:
  - The project shipped half of the PLTI carrier (CR-239..CR-245)
    without a producer. With the producer absent, the carrier's
    DeliverPMK method has no input and the kext's external-PMK
    sink branch is unreachable.
- root cause:
  - Missing project-owned producer between the kext PSK
    association-start edge and the existing DeliverPMK sink.
- fix:
  - Add a project-owned producer/consumer pipeline that uses
    ONLY project-owned surfaces:
    - kext PSK association-start edge publishes a
      `(generation, ssid, bssid, authtype)` target via a new
      command-gate-protected slot;
    - new PLTI external method
      `kAirportItlwmUserClientMethod_WaitAssociationTarget`
      blocks the AirportItlwmAgent helper until a new target
      arrives;
    - helper looks up the matching credential in the System
      keychain by SSID, derives the 32-byte WPA2 PMK via
      PBKDF2-HMAC-SHA1 (4096 iterations, ssid as salt), and
      delivers it back through the existing PLTI DeliverPMK
      external method, with the generation echoed as the
      scalar argument;
    - the kext PMK sink (`AirportItlwm::deliverExternalPMK`)
      gains a generation-echo target-identity replay guard:
      mismatched generations are rejected with
      `kIOReturnNotPermitted` and `ic_psk` is left untouched.
      The sink also explicitly clears
      `ic_external_pmk_owner = 0` to mirror the
      `installExternalPmkLocked` first-M1 owner=local routing
      already used by the CIPHER_KEY(PMK) / CUR_PMK carriers;
    - lifecycle reset edges
      (`clearExternalPmkEligibilityLocked`,
      `AirportItlwm::releaseAll`) cancel the pending generation
      so any in-flight DeliverPMK from a prior session is
      rejected.
- verification (Stage 1):
  - guest build: `./scripts/build_tahoe.sh
    /Volumes/macos/System/Library/KernelCollections/BootKernelExtensions.kc`
    — BUILD SUCCEEDED, all 927 undefined symbols resolve
    against the Tahoe BootKC;
  - helper build/codesign:
    `cd AirportItlwmAgent && make clean && make` —
    OK, AirportItlwmAgent linked against Foundation/IOKit/
    Security/CoreFoundation and codesigned with ad-hoc identity;
  - carrier ABI lock:
    `static_assert(sizeof(struct apple80211_key) == 148)` in
    both the kext dispatch table and the helper's self-contained
    apple80211_key mirror;
  - gate-atomic replay guard encoded in deliverExternalPMK:
    a single IOCommandGate::runAction action performs
    `(generation_echo != 0 && !fAssocTargetCanceled &&
      fAssocTarget.generation != 0 && echo == pending)`
    AND the subsequent ic_psk memcpy + IEEE80211_F_PSK set +
    ic_external_pmk_owner=0 + setwpaparms; mismatch returns
    `kIOReturnNotPermitted` without touching ic state.
  - gate-atomic cancel: cancelPendingAssocTarget's gated action
    zeros the pending generation AND zeros ic_psk + drops
    IEEE80211_F_PSK + clears ic_external_pmk_owner in the SAME
    command-gate hold, mutually exclusive with the
    deliverExternalPMK gated action. clearExternalPmkEligibilityLocked
    delegates its ic_psk reset to cancelPendingAssocTarget for
    every lifecycle reset edge (disassociate, leave, PMKSA
    clear, RSN disable, JOIN_ABORT, REASSOC).
  - Stage 2 after-fix runtime is OUT OF SCOPE for this Stage 1
    request and is described in the request's verification plan.
  - Stage 1 rev5 supersession context (recorded with the same
    anomaly id): rev1..rev4 of this Stage 1 request were
    rejected. rev1 was returned at orchestrator preflight for
    missing `coder_decomp_completeness_self_check: YES`; rev2
    was returned at orchestrator preflight because the
    request/patch artifacts were on the guest only and the
    preflight reads the host root; rev2 was then auditor-
    rejected because deliverExternalPMK validated the pending
    generation under the command gate but wrote ic_psk after
    releasing it, allowing a concurrent reset to interleave;
    rev3 closed the race by folding the generation check, the
    ic_psk install, and the ic_psk zero into gated actions but
    placed publishPendingAssocTarget unconditionally inside the
    PSK branch, so a helper-derived PMK could land on the
    public ad_key sub-branch; rev4 narrowed
    publishPendingAssocTarget to the external-owner / no-key
    sub-branch only but used the DESTRUCTIVE
    cancelPendingAssocTarget on the localImportHasKey
    sub-branch AFTER the caller PMK install, which wiped the
    just-installed caller PMK from ic_psk. rev5 splits the
    helper-lockout primitive into two variants:
    cancelPendingAssocTarget remains destructive (zeros
    pending generation AND ic_psk + IEEE80211_F_PSK +
    ic_external_pmk_owner) and is used only by
    clearExternalPmkEligibilityLocked and releaseAll, where
    wiping the PMK is the intended lifecycle semantic; the new
    non-destructive invalidatePendingAssocTargetOnly zeros
    only the pending PLTI target (generation + canceled flag)
    and is called from the localImportHasKey and owner=none
    PSK sub-branches BEFORE any caller-side PMK install, so a
    concurrent gated DeliverPMK that managed to land before
    the invalidate is overwritten by the caller memcpy, and
    any DeliverPMK arriving after the invalidate is rejected
    on the gated check. The locally-installed caller PMK
    survives the helper lockout, restoring the legacy public
    ad_key contract. rev5 also removes the
    commit-approval/runtime_evidence reference from
    AirportItlwmAgent/src/main.m per the rev4 source-comment
    hygiene requirement.
    Stage 1 rev6 supersession context (recorded with the same
    anomaly id): rev5 was Stage 1 APPROVED and ran through Stage 2
    after-fix runtime, which the auditor REJECTED for two
    SYSTEM_CONTRACT_FIX completeness reasons. The blocking item is
    that the helper hard-codes the SecKeychainFindGenericPassword
    service tag to "AirPort network password", while airportd-
    populated System.keychain entries on macOS 26.2 Tahoe use
    service "AirPort" (with description "AirPort network password"
    in the desc attribute, not the svce attribute that
    SecKeychainFindGenericPassword matches on). Verified at
    runtime via `sudo security dump-keychain /Library/Keychains/
    System.keychain` against an airportd-created entry on the iwx
    Tahoe Stage 2 guest. The rev5 Stage 2 run only succeeded after
    a parallel keychain item with svce="AirPort network password"
    was manually added, which masked the helper lookup defect. The
    auditor required (a) fixing the helper to match airportd's
    canonical svce or implementing a documented ordered lookup
    contract without masking the error, and (b) re-collecting
    Stage 2 evidence on the superseding diff. rev6 closes (a) by
    changing AirportItlwmAgent/src/keychain.c kServiceTag from
    "AirPort network password" to "AirPort" (the canonical Tahoe
    airportd value), with the accompanying comment block updated
    to record the verification source. No other source files
    change in rev6: the kext PSK association-start edge, the
    publish/wait/deliver/reset gated actions, the generation
    replay guard, the cancel/invalidate primitive split, and the
    helper PBKDF2 / DeliverPMK pipeline all remain bit-identical
    to rev5.
    Stage 1 rev7 supersession context (recorded with the same
    anomaly id): rev6 was Stage 1 REJECTED because the new
    AirportItlwmAgent/src/keychain.c source comment referenced
    review-step history ("CR-479 Stage 2 review"), violating the
    project rule that code comments must describe stable
    behaviour without citing CR/review/cycle ids or local
    evidence paths; the rev6 request body and proposed commit
    message also still named the helper service tag as
    "AirPort network password" (the legacy desc string), which
    contradicted the actual rev6 source constant "AirPort". rev7
    rewrites the keychain.c comment block to describe the
    Tahoe airportd System.keychain contract in API-canonical
    terms (svce "AirPort" is the lookup key,
    "AirPort network password" lives in desc and is not the
    lookup key) without any CR/review/cycle id, evidence path,
    or AIAM history; updates the request's
    SYSTEM_CONTRACT_FIX touchpoint enumeration and expected-
    contract item 5 and the proposed commit message to name
    ServiceName == "AirPort"; and adds a clearly labelled
    rev7 exact-diff helper build record. No other source or
    documentation changes between rev6 and rev7: the
    keychain.c constant value, kext source, helper main
    loop, and analysis content (other than this rev7
    supersession paragraph) are all bit-identical.
    Follow-up Stage 1 (separately filed; new request_id and
    new anomaly_id, NOT a rev8 of this anomaly): the rev7
    Stage 2 after-fix runtime exposed a downstream credential-
    acquisition ACL gap. With the rev7 svce fix in place
    (kServiceTag "AirPort") and no parallel masking entry,
    SecKeychainFindGenericPassword now correctly MATCHES the
    airportd-populated System.keychain entry, but returns
    OSStatus -25308 (errSecInteractionNotAllowed) because the
    airportd-created entry's ACL authorizes decrypt only to
    /usr/libexec/airportd (com.apple.airport.airportd Apple-
    anchored) and to apps in the Apple-anchored AirPort
    application group; an ad-hoc-codesigned project helper is
    not on that allow list. A parallel non-canonical entry in
    System.keychain was rejected as masking. The follow-up
    Stage 1 (CR-479-stage1-credential-acquisition-project-
    keychain-contract-rev1-20260518) re-points
    AirportItlwmAgent/src/keychain.c at a dedicated project-
    owned system-domain keychain at /Library/Keychains/
    AirportItlwm.keychain that the install script creates with
    an empty unlock password and auto-lock disabled; the helper
    queries that keychain with service "AirportItlwm WiFi PSK"
    and account = SSID. PSK items are populated by the operator
    via `security add-generic-password -A` so the per-item access
    list is permissive enough for the ad-hoc helper to decrypt.
    No source file in the original CR-479 producer pipeline
    (kext, PLTI carrier, gated DeliverPMK, replay guard,
    invalidate/cancel split, helper main loop, PBKDF2, install
    scripts other than the keychain-bootstrap step) changes
    Follow-up Stage 1 rev4 supersession context: the rev3
    Stage 2 after-fix runtime exposed a local rev3
    implementation bug. The rev3 design assumed
    `security create-keychain -p ""` would produce a project
    keychain with an empty unlock password, and that
    SecKeychainUnlock(proj_kc, 0, "", TRUE) would unlock it.
    On macOS 26.2 Tahoe, both halves of that assumption
    fail deterministically with OSStatus -25293
    (errSecAuthFailed) / underlying
    CSSMERR_DL_OPERATION_AUTH_DENIED -- the keychain file
    is created but its actual stored unlock password is not
    the empty byte string, so every subsequent operation
    that requires the keychain to be unlocked (security
    set-keychain-settings, security unlock-keychain -p "",
    and the helper's SecKeychainUnlock with empty password
    bytes) is rejected. The credential-acquisition
    CONTRACT itself -- project-owned keychain at
    /Library/Keychains/AirportItlwm.keychain, svce
    "AirportItlwm WiFi PSK", acct = SSID, no read of
    airportd-managed System.keychain entries, no parallel
    masking item, no fallback, no fake PMK, no Apple
    entitlement -- was proven correct at runtime in rev3
    Stage 2 evidence; only the unlock-password mechanism
    needs to be redesigned. rev4 closes the
    unlock-mechanism gap by generating a per-install random
    unlock password at install time and storing it at
    /etc/airportitlwm/keychain-password (mode 0600,
    root:wheel; written by install.sh from 32 bytes of
    /dev/urandom, base64-encoded; reused on reinstall so
    existing operator PSK items in the project keychain
    survive). The install script passes this password to
    security create-keychain -p, security unlock-keychain
    -p, and security set-keychain-settings -lut 0. The
    helper reads /etc/airportitlwm/keychain-password on
    every relaunch, passes the bytes to SecKeychainUnlock,
    and immediately scrubs the local stack buffer via the
    same volatile-pointer memset pattern already used for
    the PSK passphrase and PMK buffers. The security trust
    boundary is filesystem permissions on BOTH the keychain
    file and the unlock-password file (both root-only); the
    unlock password itself is not an independent secret
    (anyone who can read the password file can also read
    the keychain file), and the request does NOT claim
    secret-protection for the unlock password. The
    credential-acquisition contract is otherwise
    bit-identical to rev3: no kext source change, no PLTI
    publish/wait/deliver/reset behavior change, no
    additional file outside /Library/Keychains/,
    /Library/LaunchDaemons/, /usr/local/libexec/,
    /etc/airportitlwm/.
    Follow-up Stage 1 rev5 supersession context (recorded
    with the same anomaly id as rev4): rev4 was Stage 1
    REJECTED because (a) the rev4 request and install.sh
    file-level comment still described an empty-password
    create/unlock contract in their current-claim text
    (inherited from rev3 wording), even though the rev4
    implementation in fact used a per-install random
    password; and (b) the rev4 install.sh did not actually
    enforce the "root-only mode 0600" trust boundary on
    the project keychain file -- it relied on a supposed
    macOS system-domain default which rev3 install
    evidence showed to be mode 0644 instead, contradicting
    the request's claim. rev5 closes (a) by rewriting the
    install.sh file-level paragraph that mentioned an
    empty unlock password, by extending the install.sh
    body comment to describe the brief argv-visible
    install-time password exposure and its mitigations,
    and by rewriting the rev4 request's exact claim
    scope, diff summary, and proposed commit message in
    the corresponding rev5 request artifact. rev5 closes
    (b) by adding an explicit chmod 0600 + chown
    root:wheel loop in install.sh that runs immediately
    after `security create-keychain` (covering both
    /Library/Keychains/AirportItlwm.keychain and the
    -db sibling that some macOS versions produce) and
    that also re-asserts the mode/owner on every
    reinstall over an existing keychain file. rev5 also
    strengthens the AirportItlwmAgent/src/keychain.c
    agent_read_keychain_password() helper with two
    additional defensive checks before SecKeychainUnlock:
    the file must be a regular file (S_ISREG) and must
    be owned by uid 0 (root); the existing permissive
    group/world mode rejection is retained. No other
    source/behaviour change between rev4 and rev5: the
    credential-acquisition contract, the kext,
    publish/wait/deliver/reset, replay guard,
    cancel/invalidate split, helper main loop, and
    Follow-up Stage 1 rev6 supersession context: rev5 Stage 2
    after-fix runtime PROVED the helper-side credential-
    acquisition pipeline end-to-end on the live iwx Tahoe
    path -- two consecutive generations (gen=15 and gen=16)
    in a single airportd retry window both went
    WaitAssociationTarget OK -> SecKeychainOpen ->
    SecKeychainUnlock -> SecKeychainFindGenericPassword ->
    AgentLookupProjectPSK FOUND -> AgentDerivePMK_PBKDF2 OK
    -> DeliverPMK OK with matching generation echoes, and
    no read of airportd-managed System.keychain entries
    and no parallel non-canonical masking item appeared in
    the Security framework activity log. However, the
    rev5 install.sh aborted at the `security
    set-keychain-settings -lut 0` invocation immediately
    after a successful create+unlock pair: on macOS 26.2
    Tahoe that call returns "passphrase incorrect" /
    OSStatus -25293 / underlying
    CSSMERR_DL_OPERATION_AUTH_DENIED independently of the
    password value (verified for the rev3 empty-password
    design, an early rev4 fixed-string test, and the rev5
    per-install random base64 password) and independently
    of the flag set tried (`-lut 0`, `-t 0`, no flags).
    Because `set -euo pipefail` terminated the script at
    that line, the rev5 install.sh did not complete the
    search-list update or LaunchDaemon kickstart; the
    rev5 Stage 2 evidence was collected after a manual
    operator workaround that re-created the keychain
    while skipping the failing set-keychain-settings
    call. rev6 closes the install-script blocker by
    dropping the `security set-keychain-settings -lut 0`
    invocation from install.sh entirely. The helper
    unlocks the project keychain defensively on every
    relaunch (via SecKeychainUnlock with the per-install
    password bytes from /etc/airportitlwm/
    keychain-password), so disabling auto-lock at
    install time is not actually required for helper
    correctness -- it was a convenience that turns out
    to be unimplementable on macOS 26.2 Tahoe without an
    Authorization Services workflow that is outside the
    project's scope. The file-level comment in install.sh
    and keychain.c and the install.sh body block both
    record the intentional non-invocation with the
    reason. No other source/behavior change between
    rev5 and rev6: the credential-acquisition contract,
    the per-install random password file, the filesystem
    trust-boundary enforcement (chmod 0600 + chown
    root:wheel on the project keychain and on the
    password file), the helper's
    agent_read_keychain_password() defensive checks
    (S_ISREG + uid==0 + 0600), the kext, the PLTI
    publish/wait/deliver/reset chain, the gated
    generation replay guard, the cancel/invalidate
    primitive split, the helper main loop, and PBKDF2
    derivation are all bit-identical.
- notes:
  - No `CWWiFiClient`, `CoreWLAN`, `airportd`, eventType `0x6d`,
    `com.apple.wifi.events.private` entitlement, or codesign-
    bypass surface is used. The producer trigger is the kext's
    OWN PSK association-start edge, and the helper consumes a
    project-owned PLTI external method.
  - Sensitive credential and PMK material has zero log
    footprint: only lengths, generations, return codes, and
    structural markers appear in `os_log` or `XYLog` lines.
    The helper's stack-resident passphrase and PMK buffers are
    explicitly scrubbed with a volatile-pointer memset before
    going out of scope.
  - The rejected rev2 paper-only follow-up request
    (`COMMIT_REQUEST_CR-479-stage2-follow-up-cwlocationclient-eventtype-entitlement-publish-payload-replay-decomp-rev2-20260518.md`)
    and its add-only analysis/docs files are NOT part of this
    patch per the auditor's `stale_worktree_file_policy`
    direction.

- delta vs rev6/rev7/rev8/rev9/rev10 (rev11 supersession):
    The rev6..rev10 install.sh `[5/5] Loading LaunchDaemon...`
    block ran `launchctl bootstrap ... 2>/dev/null || true`,
    then the same `|| true` pattern for `enable` and `kickstart
    -k`. Three behaviors made that block unfit for Stage 2
    proof: (a) the prior `uninstall.sh launchctl disable
    system/...` records `disabled` in launchd's persistent
    override store, so the very next `launchctl bootstrap`
    against the same label returns `Bootstrap failed: 5:
    Input/output error` until an explicit `launchctl enable`
    clears the override; (b) the `|| true` mask silently
    swallowed that error, so install.sh returned exit 0 even
    though the service was not loaded; (c) no verification
    poll after the sequence ever proved the service was
    actually registered, so a fresh `launchctl print
    system/com.zxystd.airportitlwmagent` immediately after
    install.sh could (and did, during rev10 Stage 2 evidence
    collection) return `Bad request. Could not find service
    "com.zxystd.airportitlwmagent" in domain for system` while
    the install script reported success. Separately, the
    `Done.` operator workflow printed only the
    `security add-generic-password -s ... -A` command for
    seeding a target SSID and did not include the
    `security unlock-keychain -p ...` step that the project
    keychain requires on Tahoe 26.2 because install.sh
    deliberately leaves auto-lock at the macOS default; the
    documented add command therefore returned OSStatus
    -25293 / CSSMERR_DL_OPERATION_AUTH_DENIED on a freshly
    installed keychain (verified during rev10 Stage 2
    evidence collection at 20_seed/00_seed_psk.txt).
    rev11 closes both gaps with an install.sh source delta
    only (no helper code change; helper sha is
    byte-equivalent to rev5..rev10). The `[5/5]` step now
    runs `launchctl enable` BEFORE `launchctl bootstrap`
    (clearing any persistent disable override left by
    uninstall.sh), executes each launchctl command with
    explicit stderr capture into a mktemp file (no
    `|| true` mask), classifies a bootstrap failure of
    `service already loaded` / `Bootstrap failed: 17` as
    recoverable via a bootout-then-rebootstrap retry, treats
    every other launchctl failure as a hard install error
    that exits non-zero with the captured stderr message,
    and verifies with a bounded 5-second poll that
    `launchctl print system/com.zxystd.airportitlwmagent`
    returns a printable service before declaring step [5/5]
    complete. The `Done.` operator workflow now prints both
    the `security unlock-keychain` step (using the
    per-install password from
    /etc/airportitlwm/keychain-password) and the
    `security add-generic-password -A` step, in order, with
    an explicit note about why the unlock step is required
    and what OSStatus the add returns without it. The
    file-level operator-workflow comment block at the top
    of install.sh is updated in lockstep to match the new
    two-step printed workflow. The credential-acquisition
    contract itself (project keychain at
    /Library/Keychains/AirportItlwm.keychain, service
    "AirportItlwm WiFi PSK", per-install random unlock
    password at /etc/airportitlwm/keychain-password,
    helper-side SecKeychainUnlock-on-every-relaunch, no
    System.keychain read, no fake PMK, no Apple
    entitlement claim, unchanged PLTI publish/wait/deliver/
    reset path) is unchanged from rev6.
