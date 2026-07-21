# Analysis report — 2026-07-21

## ANOMALY

- id: `LAB-PMF-AP-WATCHDOG-READINESS-20260721`
- status: `FIX_VERIFIED`
- scope: external, repository-owned PMF-required AP safety helper; not a kext,
  firmware, Apple80211, or physical-AP behavior claim.
- symptom: `do_activate()` can accept a numeric PID returned by a background
  `setsid ... --watchdog` launch and then stop the optional-PMF hostapd process
  without proving that an independent rollback process has reached its
  restoration-owning state.
- first visible manifestation: source audit of
  `scripts/tahoe_pmf_required_ap_switchover.sh::start_watchdog()` before any
  live AP operation.  It records `$!` after a background launch and returns
  success without consuming an exec/readiness result from the watchdog.
- expected system behavior: the documented PMF experiment contract requires a
  separately executing rollback watchdog *before the first hostapd process
  transition*.  Therefore optional PMF must remain running when watchdog
  startup cannot be established.
- actual behavior: a successful background fork is treated as successful
  watchdog startup.  POSIX shell semantics do not make the parent background
  PID proof that the child execed the helper, passed `require_state_dir()`,
  observed marker-bound rollback authority, or remains alive.
- divergence point:
  - `start_watchdog()` launches `setsid "$SELF" --watchdog ... &`, checks only
    that `$!` is numeric, and writes it to `watchdog.pid`.
  - `do_activate()` treats that return as sufficient and immediately enters
    `stop_configured_hostapd("$OPTIONAL_CONFIG", ...)`.
  - `do_watchdog()` has no parent-visible initialization acknowledgement.
- evidence:
  - local source: lines 387–394 contain the PID-only `start_watchdog()` path;
    lines 453–464 establish state/marker, accept that path, and then may stop
    optional PMF.
  - local deterministic process probe (no AP, guest, or network touched):
    `setsid /bin/false &` returned a valid background PID while the child
    subsequently exited nonzero.  This demonstrates the exact fork-vs-exec
    distinction the helper presently collapses.
  - contract documentation:
    `docs/TAHOE_IWX_PMF_BIP_RUNTIME_PROTOCOL.md` requires a rollback watchdog
    before the first process transition; its cleanup contract relies on that
    independent owner when the runner disappears.
  - decomp: not applicable.  This is host-side laboratory transaction control,
    not a system-driver semantic path.
- candidate causes:
  - confirmed: `start_watchdog()` has no readiness handshake or liveness/
    identity validation of the executed watchdog.
  - not claimed: spontaneous later watchdog termination.  No local launcher
    can make an independently killed process immortal; the scope is strictly
    the false positive at startup before an AP mutation.
- rejected causes:
  - AP configuration/profile mismatch: unrelated; it currently blocks live
    preflight before this code is reachable.
  - runner cleanup ownership: it is a secondary recovery path and cannot
    replace the independent watchdog if the runner is interrupted.
- confirmed deviation: the code's observable acceptance condition is
  `numeric($!)`, whereas the declared safety contract is an independently
  ready rollback owner bound to the restricted state directory.
- root cause: a background-launch PID denotes only a launcher process.  It
  does not attest that `--watchdog` reached its rollback-authorized state.
- notes: no live candidate activation, guest reboot, hostapd change, AP
  configuration read, or credential handling was performed for this analysis.

## FIX_CANDIDATE

- anomaly_id: `LAB-PMF-AP-WATCHDOG-READINESS-20260721`
- symptom: optional-PMF hostapd can be stopped after a false-positive watchdog
  launch result.
- expected system behavior: no required-PMF process transition starts until a
  separate watchdog has validated the exact state directory and marker-bound
  rollback authority, identified its real PID, and acknowledged readiness to
  the activating process.
- actual behavior: the activating process writes `$!` directly to
  `watchdog.pid`, never receives a watchdog acknowledgement, and permits the
  first hostapd stop.
- exact divergence point: `start_watchdog()` / `do_watchdog()` in
  `scripts/tahoe_pmf_required_ap_switchover.sh`.
- evidence from runtime: the local `setsid /bin/false` probe above produced a
  usable background PID followed by nonzero child exit.  This is a direct,
  deterministic reproduction of the assumed-success mechanism; it did not
  contact the AP.
- evidence from decomp: not applicable; no Apple system component owns this
  repository-local rollback transaction.
- exact semantic mismatch between reference and our code: there is no
  reference-driver path.  The mismatch is against the explicit repository
  SYSTEM_CONTRACT: an independent rollback *owner* is stronger than a PID
  allocated for an attempted launch.
- fix justification path: `SYSTEM_CONTRACT_FIX`.
- enumerated system-facing touchpoints:
  1. Optional-PMF hostapd lifecycle: it must not be stopped before the
     recovery owner is proved ready.
  2. Restricted state-directory/active-marker ownership: the watchdog must
     validate that same marker-bound state before it acknowledges readiness.
  3. Watchdog cancellation: `watchdog.pid` must identify the actual watchdog
     process and keep the existing command-line identity check effective.
  4. Host networking and hostapd configuration: neither may be changed by the
     readiness exchange.
- expected contract at each touchpoint:
  1. A failed or absent ready acknowledgement leaves optional hostapd active.
  2. Readiness is emitted only after `require_state_dir()`, marker ownership,
     and rollback-authorized state validation.
  3. The parent persists only the watchdog's self-reported PID after checking
     that the live process is the expected watchdog invocation.
  4. The exchange uses only a private descriptor/FIFO under the already
     restricted state directory; it does not run hostapd, `ip`, `sysctl`,
     route, address, NAT, DHCP, or AP configuration commands.
- why no relevant touchpoints are missing: the candidate changes only the
  admission edge between existing `write_marker()` and existing optional-PMF
  stop.  Required-PMF start, rekey, rollback, config parsing, and guest
  traffic remain byte-for-byte outside the new handshake's semantic scope.
- why proposed path adds no extra system-visible side effects: the successful
  path consumes an internal one-line readiness message and records the real
  watchdog PID; neither is surfaced to hostapd, the guest, the driver, or
  committed evidence.  The failed path stops *before* any AP mutation.
- why this is root cause and not just correlation: the exact code path maps a
  background launch result directly to authorization for the first mutation,
  and the deterministic process probe proves that this result can exist when
  the launched program did not successfully run.
- why proposed fix is 1:1 with reference architecture and semantics: not
  applicable to the Apple driver.  For this host-side system contract, the
  fix preserves the existing architecture (separate `setsid` watchdog,
  marker-bound rollback, bounded lease) and makes the existing ownership
  predicate explicit; it neither changes driver behavior nor adds a fallback,
  retry, AP state publication, or synthetic success.
- proposed fix:
  - establish a one-shot private readiness descriptor before spawning the
    watchdog;
  - have `do_watchdog()` validate its restricted state/marker authorization
    and write exactly `PMF_AP_WATCHDOG_READY:<self-pid>` before its lease sleep;
  - make `start_watchdog()` accept activation only after it consumes that
    one-shot acknowledgement and verifies the reported PID belongs to the
    expected watchdog command; otherwise close the descriptor, clear the
    marker, and fail before any hostapd stop;
  - add a fixture-only early-exit injection proving that a watchdog which
    exits before acknowledgement cannot reach the optional-PMF stop.
- files/functions to modify:
  - `scripts/tahoe_pmf_required_ap_switchover.sh`:
    `start_watchdog()`, `do_watchdog()`, watchdog argument validation, and
    bounded internal readiness helpers.
  - `scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh`: successful
    readiness assertion and early-exit rejection case.
  - `scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh`: static ordering and
    readiness-bound ownership assertions.
  - `docs/TAHOE_IWX_PMF_BIP_RUNTIME_PROTOCOL.md`: narrow wording correction
    from "started" to "ready and identity-bound" rollback owner.
- forbidden alternative fixes considered and rejected:
  - merely sleep after `$!`: time is not proof of exec, state validation, or
    liveness.
  - retry or poll hostapd/watchdog startup: it broadens the mutation window
    and is not needed for a one-shot ownership handshake.
  - let runner cleanup substitute for the watchdog: it cannot recover an
    interrupted runner.
  - relax the AP/profile preflight or modify any staged AP configuration:
    outside this anomaly and not authorized.
- verification plan:
  1. Run shell syntax and the AP local fixture; assert normal activation still
     yields a live, identity-bound watchdog and verified rollback.
  2. Exercise the fixture-only pre-ack watchdog exit; assert activation fails
     before optional hostapd is stopped, required hostapd never appears, and
     the active marker is removed.
  3. Run the aggregate PMF static/evidence contracts and the pinned Tahoe
     build-admission gate.  No candidate activation, guest reboot, or live AP
     switchover is part of verification.

## IMPLEMENTATION AND LOCAL VERIFICATION

- implementation:
  - `start_watchdog()` now opens a private FIFO inside the already restricted
    state directory, passes its write descriptor only to the detached
    watchdog, consumes one readiness line, and persists only the watchdog's
    self-reported PID after command-line identity/liveness validation.
  - `do_watchdog()` emits that one line only after `require_state_dir()`,
    marker ownership, and rollback-authorized state validation; it then keeps
    the pre-existing bounded lease and rollback path unchanged.
  - an acknowledgement failure kills the just-launched owner where possible,
    closes/unlinks the private FIFO, and makes `do_activate()` clear its marker
    before any optional-PMF stop is reached.
- deterministic verification:
  - `bash scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh`: PASS.
    Its new injected pre-ack watchdog exit proves that activation fails, the
    exact optional hostapd PID remains alive, required hostapd is absent, no
    false `watchdog.pid` is persisted, and the marker is removed.
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh`: PASS.
  - `bash scripts/test_tahoe_sae_quarantine_contract.sh`: PASS.
  - `bash scripts/run_tahoe_sae_quarantine_layer.sh`: PASS on the pinned
    isolated guest.  The Tahoe kext built successfully, all 959 undefined
    symbols resolved against BootKC, trace producers/Agent/RegDiag built, and
    the gate made no kext install/load/publish/release operation.
- verification boundary: this closes only the launcher false-positive safety
  surface.  It is not a live AP result, a candidate activation, a guest reboot,
  a PMF/BIP association, or evidence that a later independently running
  watchdog cannot be externally terminated.
- next blocker for the PMF experiment remains external and unchanged: the
  optional/required configuration pair still fails the categorical saved-
  profile identity preflight.  No configuration was read into this report,
  changed, or bypassed.

## ANOMALY

- id: `LAB-PMF-AP-PRESTOP-NETWORK-FENCE-20260721`
- status: `FIX_VERIFIED`
- scope: repository-owned AP transaction admission; no kext, firmware,
  Apple80211, candidate, guest, or physical-AP claim.
- symptom: the helper captures the hash-only host-network baseline before it
  creates the state/marker and waits for the independent watchdog, but it does
  not compare the current signature again immediately before it stops the
  optional-PMF process.
- expected system behavior: the host route/address/forwarding invariant must
  still equal the captured baseline at the first AP process mutation.  A drift
  during watchdog readiness must reject the transaction while optional PMF is
  still running.
- actual behavior: `do_activate()` calls `host_network_signature()` at its
  initial preflight edge, then `write_state()`, `write_marker()`, and
  `start_watchdog()`, and immediately reaches
  `stop_configured_hostapd("$OPTIONAL_CONFIG", ...)`.  Its next signature
  comparison is only in the later rollback path.
- divergence point: `scripts/tahoe_pmf_required_ap_switchover.sh::do_activate`.
- evidence:
  - local source establishes the exact ordering above; the rekey path already
    demonstrates the intended no-stimulus-on-drift contract by comparing its
    hash immediately before and after `REKEY_GTK`.
  - `docs/TAHOE_IWX_PMF_BIP_RUNTIME_PROTOCOL.md` states that the host helper
    validates the hash-only invariant before stopping optional PMF, and the
    runner promises that host networking is not mutated by the experiment.
  - decomp: not applicable; this is external transaction control.
- candidate causes:
  - confirmed: no pre-stop signature comparison exists after the new bounded
    watchdog-readiness interval.
- rejected causes:
  - required-PMF profile mismatch: it safely stops the whole experiment earlier
    and is unrelated to a drift that occurs after activation admission began.
  - rekey fencing: it protects the later stimulus, not the first hostapd
    process transition.
- confirmed deviation: a precondition sampled before an asynchronous
  readiness handshake is treated as if it described the later mutation edge.
- root cause: the transaction has no invariant freshness fence between
  successful watchdog ownership and `stop_configured_hostapd`.

## FIX_CANDIDATE

- anomaly_id: `LAB-PMF-AP-PRESTOP-NETWORK-FENCE-20260721`
- symptom: a host-network drift during watchdog readiness can permit the
  optional-PMF process transition before the helper reports the transaction
  inconclusive.
- expected system behavior: a changed signature rejects activation before any
  hostapd process is stopped.
- actual behavior: only the early baseline is checked at this point.
- exact divergence point: the missing
  `host_network_signature() == network_signature` comparison between
  `start_watchdog()` and `stop_configured_hostapd()`.
- evidence from runtime: the local fixture already models route-signature
  drift categorically for rekey.  It can deterministically make the second
  signature probe return a different value, proving the new pre-stop fence
  without contacting a real AP.
- evidence from decomp: not applicable; no Apple component is involved.
- exact semantic mismatch between reference and our code: no reference-driver
  path applies.  The mismatch is against the explicit SYSTEM_CONTRACT that an
  AP transition may begin only with current host-network invariants.
- fix justification path: `SYSTEM_CONTRACT_FIX`.
- enumerated system-facing touchpoints:
  1. optional hostapd process: must remain untouched when the signature drifts;
  2. watchdog/state/marker: must be cancelled and cleared through the existing
     rollback-owner cleanup while optional PMF is still active;
  3. host routing/address/forwarding: read/hash only, never modified;
  4. required hostapd/rekey/guest: not reached on this rejection path.
- expected contract at each touchpoint:
  1. the original optional PID remains live and required PID never appears;
  2. no active marker or live watchdog survives a pre-stop rejection; the
     existing local PID receipt may remain only as non-authorizing audit state;
  3. the changed signature produces a categorical failure only;
  4. no AP/guest/candidate side effect occurs.
- why no relevant touchpoints are missing: the candidate inserts one
  read-only comparison at the exact last pre-mutation edge and reuses the
  established `finish_armed_rollback()` cleanup.  It does not alter any
  post-transition, rekey, or driver path.
- why proposed path adds no extra system-visible side effects: on an unchanged
  network it adds only a read/hash; on a changed network it prevents, rather
  than compensates for, an AP transition.  It introduces no retry, delay,
  fallback, configuration write, network command, or state publication.
- why this is root cause and not just correlation: the code order directly
  connects the stale baseline to authorization for the first process stop;
  no other branch rechecks the predicate in that interval.
- why proposed fix is 1:1 with reference architecture and semantics: no Apple
  implementation applies.  It is a narrow system-contract admission fence,
  structurally identical to the existing pre/post rekey signature fencing and
  uses no new state owner.
- files/functions to modify:
  - `scripts/tahoe_pmf_required_ap_switchover.sh::do_activate()`;
  - `scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh` fake signature
    source and pre-stop-drift case;
  - `scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh` ordering assertion;
  - `docs/TAHOE_IWX_PMF_BIP_RUNTIME_PROTOCOL.md` invariant wording.
- forbidden alternative fixes considered and rejected:
  - rely on rollback to detect the drift after stopping optional PMF;
  - extend or poll the watchdog lease;
  - alter route/address/forwarding or staged AP configuration;
  - treat a later rekey signature check as coverage for activation.
- verification plan:
  1. The fixture's second signature probe returns drift immediately after a
     normal watchdog readiness acknowledgement; assert no optional PID change,
     no required PID, no marker, and no live watchdog process.
  2. Re-run PMF static/evidence contracts and the isolated Tahoe build-only
     gate; do not activate a candidate, reboot a guest, or touch the live AP.

## IMPLEMENTATION AND LOCAL VERIFICATION

- implementation:
  - `do_activate()` now samples the existing hash-only host-network signature
    once more after watchdog readiness and directly before
    `stop_configured_hostapd()`.
  - An unreadable or changed signature takes the established
    `finish_armed_rollback()` path while optional PMF is still active.  It
    reports a categorical inconclusive result and never reaches required-PMF
    start, rekey, or guest work.
  - The local fixture can inject drift precisely on that second route probe;
    the static contract pins the resulting source ordering.
- deterministic verification:
  - `bash scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh`: PASS.
    The injected pre-stop drift keeps the exact optional hostapd PID live,
    never creates the required PID, clears the active marker, and leaves no
    live watchdog process.
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh`: PASS.
  - `bash scripts/test_tahoe_sae_quarantine_contract.sh`: PASS.
  - `bash scripts/run_tahoe_sae_quarantine_layer.sh`: PASS on the pinned
    isolated guest.  The Tahoe kext built successfully, all 959 undefined
    symbols resolved against BootKC, trace producers/Agent/RegDiag built, and
    the gate made no kext install/load/publish/release operation.
- verification boundary: this closes only the repository-local AP admission
  freshness gap.  It is not a live AP result, candidate activation, guest
  reboot, PMF/BIP association, or proof about a host-network change occurring
  after the first process transition has already begun.
- external blocker unchanged: the optional/required saved-profile identity
  preflight remains categorically mismatched.  No live configuration was read,
  changed, or bypassed.

## ANOMALY

- id: `LAB-PMF-AP-TRANSITION-CONFIG-OWNERSHIP-20260721`
- status: `FIX_VERIFIED`
- scope: repository-owned staged configuration ownership across required-PMF
  transition and rollback; no kext, firmware, Apple80211, candidate, guest,
  or physical-AP claim.
- symptom: `do_activate()` keeps the admitted configuration-pair digest only
  in local variables.  A staged file can change after the final pre-stop fence
  and while required hostapd is launched; the helper neither checks the digest
  before state promotion nor records it for rollback.
- expected system behavior: required-active publication and any optional-PMF
  restart after a transition must be authorized only by the same staged pair
  admitted at activation.  If either file changes after optional PMF stops,
  required PMF must be quiesced and marker/watchdog retained until the original
  pair is restored and verified.
- actual behavior: `write_state()` stores only the host-network digest.
  `start_configured_hostapd("$REQUIRED_CONFIG", ...)` consumes the path after
  the last local config digest check, and the next operation is process/AP
  attestation then `mark_required_active()`.  `do_rollback()` may subsequently
  start optional hostapd from a changed file without a configuration baseline
  comparison.
- divergence point:
  `scripts/tahoe_pmf_required_ap_switchover.sh::{write_state,mark_required_active,
  do_activate,restore_optional_after_activation_failure,do_rollback}`.
- evidence:
  - local source establishes that the config digest is not state-owned and is
    absent from the post-start promotion and rollback paths.
  - the protocol requires validated configuration shape before the AP process
    transition; that contract applies to the required input and to a restart
    claimed as optional recovery, not merely to an earlier file read.
  - deterministic no-AP runtime reproduction: only the fixture generated
    required config acquired the valid but unadmitted `wpa_group_rekey`
    directive inside fake required-hostapd startup.  The current helper
    published the synthetic transition, and the fixture stopped with
    `activation accepted a required config changed during daemon start`
    (exit 1).  No physical AP, guest, route, or real configuration was used.
  - decomp: not applicable; this is external laboratory transaction control.
- candidate causes:
  - confirmed: configuration ownership is local to admission rather than
    durable transaction state, and recovery does not distinguish an already
    running optional process from a restart that would consume changed bytes.
- rejected causes:
  - pre-stop configuration fence: it closes only the readiness interval before
    optional stop, not the later stop/start interval.
  - network recovery verification: it proves route/address/forwarding state,
    not hostapd file identity.
- confirmed deviation: an unadmitted staged config can become a required
  hostapd input or optional rollback input after the last configuration check.
- root cause: no config-pair baseline is preserved and enforced across the
  transition/rollback owner boundary.

## FIX_CANDIDATE

- anomaly_id: `LAB-PMF-AP-TRANSITION-CONFIG-OWNERSHIP-20260721`
- symptom: post-fence staged-file changes can be consumed by hostapd or treated
  as verified optional recovery.
- expected system behavior: state carries a hash-only pair baseline; a changed
  pair blocks required promotion, blocks rekey, and allows optional restart
  only once the baseline pair is restored.  A currently running optional
  process may be retained without rereading the changed file.
- actual behavior: no config digest is in state, and restart paths use paths
  without comparing their current contents to an activation baseline.
- exact divergence point: missing
  `config_pair_signature_before` state value and missing equality checks after
  required start and before any optional hostapd restart.
- evidence from runtime: a fixture-only fake required-hostapd start changed
  only its generated required file.  The unmodified helper reported required
  active and the fixture deterministically rejected it (exit 1), without an
  AP, route, guest, or real configuration operation.
- evidence from decomp: not applicable; no Apple component owns hostapd staged
  configuration transaction ownership.
- exact semantic mismatch between reference and our code: no reference-driver
  path applies.  The mismatch is against the SYSTEM_CONTRACT that a categorical
  AP transition and verified restoration consume only the admitted staged
  configuration pair.
- fix justification path: `SYSTEM_CONTRACT_FIX`.
- enumerated system-facing touchpoints:
  1. required hostapd input: a changed pair cannot be promoted as active;
  2. required process on a detected change: it is stopped without consuming a
     new configuration file;
  3. optional restart: it occurs only after the state pair baseline matches;
  4. state/marker/watchdog: retain ownership while staged-file identity is
     unresolved, and permit explicit rollback after baseline restoration;
  5. rekey: it is blocked while the pair differs;
  6. network, guest, candidate, and physical AP configuration: no new action.
- expected contract at each touchpoint:
  1. no `required` state or success line follows a changed pair;
  2. a process launched from unverified bytes is quiesced;
  3. no optional hostapd starts from changed bytes;
  4. only a verified later rollback may clear marker/watchdog;
  5. no group rekey reaches hostapd after a pair mismatch;
  6. digest reads are local and values are never rendered.
- why no relevant touchpoints are missing: the candidate covers every path that
  can consume a staged config after activation: required promotion, rekey, the
  shared activation-failure restore, normal rollback, and watchdog rollback.
  Existing optional processes remain usable without a file reread.
- why proposed path adds no extra system-visible side effects: normal paths add
  hash-only reads and one state digest.  Mismatch paths stop only an already
  required process, decline an untrusted optional restart, and retain the
  existing recovery owner.  No retry, delay, config write, credential output,
  route mutation, or guest operation is introduced.
- why this is root cause and not just correlation: the exact source paths pass
  config file names directly to hostapd after the last digest check, while no
  state value permits recovery to prove the same bytes later.
- why proposed fix is 1:1 with reference architecture and semantics: no Apple
  implementation applies.  This narrow system-contract fix extends the
  existing hash-only config admission predicate through the existing state and
  rollback owner; it adds no alternate configuration source or process owner.
- files/functions to modify:
  - `scripts/tahoe_pmf_required_ap_switchover.sh` config state fields,
    transition checks, rekey/restore/rollback guards;
  - `scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh` fake required
    start mutation and baseline-restoration case;
  - `scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh` state/ordering guard;
  - `docs/TAHOE_IWX_PMF_BIP_RUNTIME_PROTOCOL.md` durable configuration wording.
- forbidden alternative fixes considered and rejected:
  - copy staged configuration/credentials into a new snapshot file;
  - restart optional hostapd from an unverified changed path;
  - ignore the post-start mismatch because required hostapd has a live PID;
  - modify the staged files, AP settings, or host networking to make hashes
    match.
- verification plan:
  1. Completed: the fixture confirmed that a fake required startup mutation was
     incorrectly published as required active by the pre-fix helper.
  2. Completed: state-owned digest guards quiesce required, refuse an optional
     restart from changed bytes, and retain marker/watchdog until fixture
     baseline restoration permits normal rollback.
  3. Completed: PMF static/evidence contracts and the isolated Tahoe build-only
     gate passed.  No candidate activation, guest reboot, or live AP operation
     occurred.

## IMPLEMENTATION AND LOCAL VERIFICATION

- implementation:
  - the activation state now carries `config_pair_signature_before` alongside
    the existing host-network digest, and required-state promotion preserves
    that same value.
  - `config_pair_matches_state()` gates post-start promotion, bounded rekey,
    and every optional-hostapd restart path.  An already-running optional
    process may remain in place without rereading a changed staged file.
  - on a changed pair after optional stop, recovery stops any required process
    but does not start optional from untrusted bytes; marker/watchdog remain
    until a later normal rollback sees the original pair again.
  - the fixture changes only its generated required file during fake daemon
    startup, then restores that generated baseline before its explicit rollback.
- deterministic verification:
  - Before implementation, the AP-helper fixture failed exactly at `activation
    accepted a required config changed during daemon start`; this is the
    controlled reproduction recorded above.
  - After implementation, the same fixture: PASS.  The changed pair produces
    no required-active line, no required/optional PID from changed input, and
    retains marker/live watchdog.  Restoring its generated pair allows normal
    optional rollback and removes ownership.
  - The fixture also changes the pair during an active required session and
    proves `--rekey` fails before any fake hostapd CLI request, then restores
    the generated pair for baseline cases.
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh`: PASS.
  - `bash scripts/test_tahoe_sae_quarantine_contract.sh`: PASS.
  - `bash scripts/run_tahoe_sae_quarantine_layer.sh`: PASS on the pinned
    isolated guest.  The Tahoe kext built successfully, all 959 undefined
    symbols resolved against BootKC, trace producers/Agent/RegDiag built, and
    the gate made no kext install/load/publish/release operation.
- verification boundary: this closes only repository-owned staged-file
  ownership across AP transition/recovery.  It is not a live AP result,
  candidate activation, guest reboot, PMF/BIP association, or a replacement for
  a separately reviewed real staged-profile configuration plan.
- external blocker unchanged: the optional/required saved-profile identity
  preflight remains categorically mismatched.  No live configuration was read,
  changed, or bypassed.

## ANOMALY

- id: `LAB-PMF-RUNNER-CLEANUP-AP-BEFORE-RADIO-20260721`
- status: `FIX_VERIFIED`
- scope: repository-owned runtime-runner interruption cleanup ordering; no
  kext, firmware, Apple80211, candidate, guest, or physical-AP claim.
- symptom: when the runner has powered the guest radio off and then successfully
  activated the required-PMF AP, its `cleanup()` turns the guest radio on before
  it attempts marker-bound AP rollback.
- expected system behavior: after an interrupted required-PMF activation, AP
  rollback ownership must be attempted before the radio is restored.  This
  prevents the saved-profile autojoin edge from observing required PMF outside
  the bounded trace/authorization sequence.
- actual behavior: `cleanup()` executes the `RADIO_OFF_PENDING` recovery block
  first, then the fresh-`AP_STATE_DIR` rollback block.  The normal main path
  leaves `RADIO_OFF_PENDING=1` from radio-off through required activation until
  radio-on is both requested and observed, so a failure in that interval takes
  the unsafe order.
- divergence point: `scripts/run_tahoe_iwx_pmf_bip_runtime.sh::cleanup`.
- evidence:
  - local source establishes the exact two cleanup blocks and main-path flag
    lifecycle above.
  - the runner protocol defines required-PMF AP activation before saved-profile
    radio-on/authorization and requires verified optional restoration before a
    result; turning radio on first reverses that safety boundary under failure.
  - deterministic local runtime reproduction: the contract fixture executed
    the extracted real `cleanup()` body with a fake AP helper and fake radio
    functions.  It observed `radio-on` before `ap-rollback`, and rejected the
    unmodified runner with `runner cleanup restores radio before AP rollback
    ownership` (exit 1), without SSH, guest, hostapd, or network operations.
  - decomp: not applicable; this is external laboratory transaction control.
- candidate causes:
  - confirmed: cleanup prioritizes guest radio recovery over the already
    allocated AP rollback ownership boundary.
- rejected causes:
  - advisory `AP_REQUIRED_ACTIVE`: cleanup correctly does not trust this flag;
    the issue is the order of two mandatory cleanup actions.
  - normal successful path: it performs explicit AP rollback before return and
    does not exercise `RADIO_OFF_PENDING` cleanup recovery.
- confirmed deviation: a failure path can re-enable saved-profile autojoin
  while required PMF is still the active AP configuration.
- root cause: the radio-recovery block is positioned before AP rollback in
  `cleanup()`.

## FIX_CANDIDATE

- anomaly_id: `LAB-PMF-RUNNER-CLEANUP-AP-BEFORE-RADIO-20260721`
- symptom: interrupted execution can restore radio before optional PMF.
- expected system behavior: attempt AP rollback from every allocated state
  directory first, then restore a pending guest radio state.
- actual behavior: the order is radio recovery, AP rollback, trace cleanup,
  identity capture.
- exact divergence point: the two leading `cleanup()` blocks are reversed from
  the transaction ownership order.
- evidence from runtime: an extracted-body local mock supplied no-op fake radio
  and AP-helper functions with the real cleanup control flow.  The unmodified
  source recorded `radio-on` before `ap-rollback` and the fixture failed
  deterministically; no guest, AP, SSH, route, or hostapd command was invoked.
- evidence from decomp: not applicable; no Apple component owns the external
  runtime-runner cleanup order.
- exact semantic mismatch between reference and our code: no reference-driver
  path applies.  The mismatch is against the SYSTEM_CONTRACT that the AP state
  directory is the recovery owner after activation and required PMF must not
  be exposed to saved-profile autojoin outside the bounded gate.
- fix justification path: `SYSTEM_CONTRACT_FIX`.
- enumerated system-facing touchpoints:
  1. host AP lifecycle: optional-PMF rollback is attempted first;
  2. guest radio/autojoin: it remains off until that attempt completes;
  3. AP state/marker/watchdog: the existing fresh-directory cleanup semantics
     and valid watchdog witness handling are preserved;
  4. trace/identity/evidence: their existing later cleanup order is unchanged;
  5. route/address, kext, reboot, and physical host: no new command exists.
- expected contract at each touchpoint:
  1. no cleanup radio-on precedes an available AP rollback attempt;
  2. a failure after activation cannot create an unbounded required-PMF
     autojoin interval through runner ordering;
  3. failed rollback still produces inconclusive evidence and keeps existing
     helper ownership behavior;
  4. trace is still disabled and identity captured after recovery attempts;
  5. mutation scope remains unchanged.
- why no relevant touchpoints are missing: only the first two cleanup actions
  are swapped.  No action is removed, duplicated, or newly introduced, and
  the AP helper retains all process/rollback authority.
- why proposed path adds no extra system-visible side effects: it changes only
  the order of already-required recovery calls.  It adds no retry, delay,
  fallback, guest command, AP command, configuration write, or state format.
- why this is root cause and not just correlation: the local mock invokes the
  actual cleanup body and observes the exact reversed call order, which maps
  directly to the unsafe source order.
- why proposed fix is 1:1 with reference architecture and semantics: no Apple
  implementation applies.  The system-contract fix respects the runner's
  declared AP ownership boundary rather than introducing any new recovery
  path.
- files/functions to modify:
  - `scripts/run_tahoe_iwx_pmf_bip_runtime.sh::cleanup`;
  - `scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh` extracted-body
    ordering fixture;
  - `docs/TAHOE_IWX_PMF_BIP_RUNTIME_PROTOCOL.md` interruption wording.
- forbidden alternative fixes considered and rejected:
  - leave the order and rely on trace capture to make autojoin harmless;
  - condition rollback on advisory `AP_REQUIRED_ACTIVE`;
  - keep radio off indefinitely instead of attempting normal recovery;
  - add guest joins, scans, route commands, or AP configuration mutations.
- verification plan:
  1. Completed: the extracted-body fixture confirmed radio-on before AP
     rollback without external operations.
  2. Completed: swapping only the cleanup actions makes the same extracted-body
     mock record AP rollback before radio-on, with existing ownership/witness
     behavior unchanged.
  3. Completed: PMF static/evidence contracts and the isolated Tahoe build-only
     gate passed.  No candidate activation, guest reboot, or live AP operation
     occurred.

## IMPLEMENTATION AND LOCAL VERIFICATION

- implementation:
  - `cleanup()` now processes the existing fresh-`AP_STATE_DIR` rollback
    ownership/witness path before the existing `RADIO_OFF_PENDING` recovery
    block.  Trace and identity cleanup retain their former relative order.
  - no cleanup action was added or removed: the fix is solely the order of two
    pre-existing recovery operations.
  - the contract fixture extracts the actual cleanup function at test time,
    executes it in a child shell with local mock radio/AP-helper functions, and
    checks the observable sequence without executing any remote or AP command.
- deterministic verification:
  - Before implementation, the extracted-body fixture failed exactly at
    `runner cleanup restores radio before AP rollback ownership`; this is the
    controlled reproduction recorded above.
  - After implementation, the same fixture: PASS.  It records `ap-rollback`
    before `radio-on`, writes the same synthetic rollback witness, and invokes
    no SSH, guest, hostapd, route, or network operation.
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh`: PASS.
  - `bash scripts/test_tahoe_sae_quarantine_contract.sh`: PASS.
  - `bash scripts/run_tahoe_sae_quarantine_layer.sh`: PASS on the pinned
    isolated guest.  The Tahoe kext built successfully, all 959 undefined
    symbols resolved against BootKC, trace producers/Agent/RegDiag built, and
    the gate made no kext install/load/publish/release operation.
- verification boundary: this closes only an interruption cleanup ordering
  gap.  It is not a live AP result, candidate activation, guest reboot,
  PMF/BIP association, or a proof that an external AP rollback itself succeeds.
- external blocker unchanged: the optional/required saved-profile identity
  preflight remains categorically mismatched.  No live configuration was read,
  changed, or bypassed.

## ANOMALY

- id: `LAB-PMF-AP-POSTTRANSITION-ROLLBACK-NETWORK-VERIFY-20260721`
- status: `FIX_VERIFIED`
- scope: repository-owned activation-failure recovery after the first AP
  process transition; no kext, firmware, Apple80211, candidate, guest, or
  physical-AP claim.
- symptom: activation branches that can run after optional hostapd has been
  stopped call `finish_armed_rollback()`.  That helper restores optional PMF
  and clears marker/watchdog without comparing the saved host-network baseline
  to the post-restore network state.
- expected system behavior: once a hostapd process transition has begun, an
  immediate recovery may clear its rollback owner only if optional PMF is
  restored *and* the hash-only host route/address/forwarding invariant still
  equals the recorded baseline.  Otherwise the activation is inconclusive and
  the marker-bound watchdog remains armed.
- actual behavior: the ordinary `do_rollback()` path reads
  `host_network_signature_before` and rejects a mismatched post-restore
  signature before it cancels its watchdog.  In contrast,
  `finish_armed_rollback()` calls only
  `restore_optional_after_activation_failure()`, `cancel_watchdog()`, and
  `clear_marker()`.  The start-failure, post-start-attestation, and
  state-promotion-failure branches invoke this weaker helper after a possible
  optional-process stop.
- divergence point:
  `scripts/tahoe_pmf_required_ap_switchover.sh::finish_armed_rollback` and its
  post-transition callers in `do_activate()`.
- evidence:
  - local source directly shows the stronger normal rollback invariant check
    and its absence from the activation-failure helper.
  - the documented runtime boundary requires verified recovery and makes every
    route/address deviation inconclusive; clearing the only recovery owner
    after a changed invariant contradicts that contract.
  - deterministic no-AP runtime reproduction: the fixture fake required-hostapd
    start set only its generated network signature source to drift and then
    failed.  The current helper restored fake optional hostapd but emitted the
    verified-rollback branch instead of retaining its armed watchdog; the
    fixture stopped at `post-transition drift did not retain its
    armed-watchdog diagnostic` (exit 1).  No physical AP, route, or real
    configuration was touched.
  - decomp: not applicable; this is external laboratory transaction control.
- candidate causes:
  - confirmed: one cleanup helper is reused on both pre-stop rejections (where
    no AP mutation occurred) and after-transition recovery (where a baseline
    verification is required), despite their different contracts.
- rejected causes:
  - normal `--rollback`: it already has the required signature comparison and
    does not explain the weaker activation-failure branches.
  - pre-stop network/config fences: they prevent known stale admission but
    cannot detect a drift introduced during stop/start recovery.
- confirmed deviation: a recovery result labelled verified omits the same
  host-network postcondition that the normal recovery path enforces.
- root cause: `finish_armed_rollback()` lacks a transition-aware host-network
  verification mode.

## FIX_CANDIDATE

- anomaly_id: `LAB-PMF-AP-POSTTRANSITION-ROLLBACK-NETWORK-VERIFY-20260721`
- symptom: a network drift during a failed AP transition can remove the last
  rollback owner and claim optional recovery verified.
- expected system behavior: all branches after the first optional-PMF stop use
  a recovery helper that compares the current hash-only network signature to
  the state baseline before canceling watchdog/marker ownership.
- actual behavior: those branches use `finish_armed_rollback()` with no such
  comparison.
- exact divergence point: missing `host_network_signature()` equality test
  between `restore_optional_after_activation_failure()` and
  `cancel_watchdog()` for post-transition activation failures.
- evidence from runtime: the fixture-only required-start path changed its fake
  network signature immediately before it returned failure.  The unmodified
  helper nevertheless took its optional-rollback-verified branch; the fixture
  deterministically rejected the missing armed-watchdog diagnostic (exit 1),
  with no AP, route, or real configuration touched.
- evidence from decomp: not applicable; no Apple component owns this host-side
  transaction recovery.
- exact semantic mismatch between reference and our code: no reference-driver
  path applies.  The mismatch is against the explicit SYSTEM_CONTRACT shared
  by `do_rollback()` and the runner: verified optional recovery includes the
  preserved host-network invariant.
- fix justification path: `SYSTEM_CONTRACT_FIX`.
- enumerated system-facing touchpoints:
  1. optional/required hostapd lifecycle: optional must be restored before an
     invariant can be evaluated;
  2. host route/address/forwarding: read/hash only, and a mismatch must block
     verified cleanup;
  3. state/marker/watchdog: remain owned when recovery cannot be verified;
  4. pre-stop rejections: retain their existing lightweight cleanup because no
     hostapd transition has occurred;
  5. staged files, rekey, candidate, and guest: not reached or changed.
- expected contract at each touchpoint:
  1. required PMF is absent and optional PMF is exact before a verified result;
  2. unchanged signature is mandatory after a transition recovery;
  3. only verified recovery cancels watchdog and clears marker;
  4. a pre-stop drift still leaves optional untouched and needs no transition
     recovery assertion;
  5. no new network/configuration/guest operation occurs.
- why no relevant touchpoints are missing: the candidate adds a dedicated
  recovery wrapper only to the three post-transition failure branches.  It
  reuses existing process restoration, signature hashing, and watchdog logic;
  it changes neither normal rollback nor pre-stop rejection behavior.
- why proposed path adds no extra system-visible side effects: success adds
  one read-only signature hash after existing restoration.  Failure preserves
  an existing watchdog rather than clearing it.  There is no retry, delay,
  fallback, configuration write, or guest action.
- why this is root cause and not just correlation: the same fixture drift is
  rejected by `do_rollback()`'s existing postcondition but accepted by the
  activation-failure helper solely because that comparison is absent.
- why proposed fix is 1:1 with reference architecture and semantics: no Apple
  implementation applies.  The system-contract fix makes the existing
  activation-failure path use the same recovery invariant as normal rollback,
  without creating a new owner or recovery mechanism.
- files/functions to modify:
  - `scripts/tahoe_pmf_required_ap_switchover.sh` dedicated post-transition
    recovery helper and `do_activate` callers;
  - `scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh` synthetic
    drift-on-required-start case and bounded cleanup;
  - `scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh` ordering assertion;
  - `docs/TAHOE_IWX_PMF_BIP_RUNTIME_PROTOCOL.md` recovery wording.
- forbidden alternative fixes considered and rejected:
  - weaken `do_rollback()` to match the existing activation helper;
  - clear marker/watchdog after an unverified recovery and rely on later
    runner checks;
  - modify host route/address/forwarding to force the hash to match;
  - apply the stricter post-transition condition to pre-stop rejections where
    no AP process changed.
- verification plan:
  1. Completed: the fixture confirmed that the pre-fix helper incorrectly
     reported its synthetic network drift as verified optional rollback.
  2. Completed: the transition-aware recovery helper restores optional PMF but
     preserves marker/watchdog until a subsequent stable normal rollback.
  3. Completed: PMF static/evidence contracts and the isolated Tahoe build-only
     gate passed.  No candidate activation, guest reboot, or live AP operation
     occurred.

## IMPLEMENTATION AND LOCAL VERIFICATION

- implementation:
  - `finish_post_transition_rollback()` reads the saved network baseline,
    performs the existing optional-PMF restoration, and compares the current
    hash-only network signature before it may cancel watchdog or clear marker.
  - only the three `do_activate()` branches after a possible optional-process
    stop use that helper: required start failure, post-start attestation
    failure, and state-promotion failure.  Pre-stop rejections retain their
    prior cleanup because no AP process transition occurred.
  - when the post-restore signature differs, the helper reports an armed
    rollback watchdog; the existing watchdog/normal rollback owner can later
    complete cleanup only after the invariant becomes true.
- deterministic verification:
  - Before implementation, the AP-helper fixture failed exactly at
    `post-transition drift did not retain its armed-watchdog diagnostic`; this
    is the controlled reproduction recorded above.
  - After implementation, the same fixture: PASS.  Its drifted start failure
    restores a live optional PID but keeps marker/live watchdog; after the
    fixture restores its synthetic network baseline, an explicit normal
    rollback reports optional restoration and removes both ownership records.
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh`: PASS.
  - `bash scripts/test_tahoe_sae_quarantine_contract.sh`: PASS.
  - `bash scripts/run_tahoe_sae_quarantine_layer.sh`: PASS on the pinned
    isolated guest.  The Tahoe kext built successfully, all 959 undefined
    symbols resolved against BootKC, trace producers/Agent/RegDiag built, and
    the gate made no kext install/load/publish/release operation.
- verification boundary: this closes only an activation-failure recovery
  invariant gap.  It is not a live AP result, candidate activation, guest
  reboot, PMF/BIP association, or proof that an external network change will
  itself return to baseline.
- external blocker unchanged: the optional/required saved-profile identity
  preflight remains categorically mismatched.  No live configuration was read,
  changed, or bypassed.

## ANOMALY

- id: `LAB-PMF-AP-PREPROMOTION-REQUIRED-ATTESTATION-20260721`
- status: `FIX_VERIFIED`
- scope: repository-owned required-PMF state promotion; no kext, firmware,
  Apple80211, candidate, guest, or physical-AP claim.
- symptom: `do_activate()` lets `start_configured_hostapd()` return and then
  marks rollback state as `required` without reattesting that the exact
  required hostapd PID is still live and that the AP remains pinned.
- expected system behavior: `PMF_AP_SWITCHOVER=REQUIRED_ACTIVE` must be
  emitted only if the exact required-PMF process and pinned channel/width are
  still present at the state-promotion edge.  A post-start disappearance must
  restore optional PMF and report an inconclusive activation instead.
- actual behavior: `wait_hostapd_active()` checks process identity and channel
  state inside `start_configured_hostapd()`, then returns.  The caller's next
  operation is `mark_required_active()`; no required-process or channel
  attestation occurs between that return and the state/public success claim.
- divergence point: `scripts/tahoe_pmf_required_ap_switchover.sh::do_activate`
  after `start_configured_hostapd("$REQUIRED_CONFIG", ...)` and before
  `mark_required_active()`.
- evidence:
  - local source establishes the missing final predicate at the sole state
    promotion edge.
  - the helper's state file and `PMF_AP_SWITCHOVER=REQUIRED_ACTIVE` are
    consumed as authorization for the later bounded runner sequence, so a
    stale process snapshot cannot be treated as current required-PMF state.
  - deterministic no-AP runtime reproduction: the fixture fake-IW helper
    terminated its generated required child after `wait_hostapd_active()` had
    read it but immediately before that function returned.  The current helper
    accepted the transition, and the fixture stopped with `activation accepted
    a required hostapd that died before state promotion` (exit 1).  No physical
    AP, host network, or real hostapd was involved.
  - decomp: not applicable; this is external laboratory transaction control.
- candidate causes:
  - confirmed: promotion relies on a liveness observation made by a different
    function rather than an explicit predicate at the promotion edge.
- rejected causes:
  - ordinary required-hostapd start failure: the existing failure branch
    handles a failed `start_configured_hostapd()` but not a process that exits
    immediately after that function's successful observation.
  - watchdog readiness: it proves rollback ownership before transition, not
    that the required process remains alive when success is claimed.
- confirmed deviation: an asynchronous process is represented as current
  required state after an earlier, now-stale liveness observation.
- root cause: the `mark_required_active()` branch lacks a final required PID
  and AP-pinned attestation.

## FIX_CANDIDATE

- anomaly_id: `LAB-PMF-AP-PREPROMOTION-REQUIRED-ATTESTATION-20260721`
- symptom: a dead or unpinned required AP can be promoted as active.
- expected system behavior: the promotion edge rechecks exact required process
  identity and pinned radio state; failure follows the existing full rollback
  path rather than emitting required-active success.
- actual behavior: no check separates the successful start helper return from
  `mark_required_active()`.
- exact divergence point: the missing
  `configured_hostapd_active("$REQUIRED_CONFIG", "$REQUIRED_PID")` and
  `runtime_ap_is_pinned()` predicates immediately before promotion.
- evidence from runtime: a fixture-only fake-IW termination injection ran
  after the successful child identity observation inside
  `wait_hostapd_active()`.  The unmodified helper accepted that synthetic
  activation and the fixture deterministically reported it (exit 1); no
  physical AP, host network, or real hostapd was involved.
- evidence from decomp: not applicable; no Apple component owns the external
  AP lifecycle transaction.
- exact semantic mismatch between reference and our code: no reference-driver
  path applies.  The mismatch is against the SYSTEM_CONTRACT that a categorical
  active-state publication represents a live, pinned required AP at that exact
  publication boundary.
- fix justification path: `SYSTEM_CONTRACT_FIX`.
- enumerated system-facing touchpoints:
  1. required hostapd PID: must remain the exact configured process at promote;
  2. AP channel/width: must remain pinned at promote;
  3. optional hostapd/rollback ownership: a failed promotion check must use
     the existing marker-bound full rollback path;
  4. state/marker/watchdog: success is withheld until attestation, and a
     failed rollback retains watchdog ownership if restoration cannot prove its
     invariant;
  5. network, staged configuration, rekey, candidate, and guest: no new
     operation is performed by this check.
- expected contract at each touchpoint:
  1. no `required` state is written for a dead/replaced process;
  2. no required-active claim occurs for an unpinned AP;
  3. optional PMF is restored when the full rollback can verify it;
  4. marker/watchdog are cleared only through verified rollback;
  5. the added checks are read-only and do not broaden capabilities.
- why no relevant touchpoints are missing: the candidate acts only in the
  narrow gap after the existing start function and before existing state
  promotion.  It reuses `finish_armed_rollback()` because that existing
  activation-failure path validates optional restoration and preserves watchdog
  ownership if restoration cannot be proved.
- why proposed path adds no extra system-visible side effects: it adds one
  process identity observation and one `iw` read in the success path.  The
  failure path restores existing optional PMF rather than publishing a false
  required state; it adds no retry, delay, configuration write, or guest work.
- why this is root cause and not just correlation: the fake child dies at the
  exact only unguarded boundary, while the source directly maps the stale
  return to `mark_required_active()`.
- why proposed fix is 1:1 with reference architecture and semantics: no Apple
  implementation applies.  The narrow system-contract fence uses the existing
  exact-process and pin predicates and existing rollback owner, without new
  process ownership or state format.
- files/functions to modify:
  - `scripts/tahoe_pmf_required_ap_switchover.sh::do_activate`;
  - `scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh` fake-IW
    termination source and pre-promotion case;
  - `scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh` ordering assertion;
  - `docs/TAHOE_IWX_PMF_BIP_RUNTIME_PROTOCOL.md` promotion wording.
- forbidden alternative fixes considered and rejected:
  - sleep, poll, or retry after start;
  - mark required state first and let later rekey detect the missing process;
  - rely only on watchdog expiry instead of immediate verified rollback;
  - alter hostapd configuration, routing, AP settings, or guest state.
- verification plan:
  1. Completed: the fixture confirmed that the pre-fix helper incorrectly
     reported the controlled post-start child death as required active.
  2. Completed: final attestation makes the same injection fail, restores
     optional PMF, leaves no required PID or marker, and no live watchdog.
  3. Completed: PMF static/evidence contracts and the isolated Tahoe build-only
     gate passed.  No candidate activation, guest reboot, or live AP operation
     occurred.

## IMPLEMENTATION AND LOCAL VERIFICATION

- implementation:
  - after `start_configured_hostapd()` returns, `do_activate()` now immediately
    rechecks the exact required PID and pinned AP channel/width before calling
    `mark_required_active()`.
  - a failed post-start attestation takes the existing
    `finish_armed_rollback()` path.  It restores optional PMF when verifiable,
    otherwise preserves marker/watchdog ownership; it never writes `state=required`
    or prints `PMF_AP_SWITCHOVER=REQUIRED_ACTIVE`.
  - the fake-IW fixture hook is confined to test mode and kills only its
    generated fake required child at the exact stale-observation boundary.
- deterministic verification:
  - Before implementation, the AP-helper fixture failed exactly at `activation
    accepted a required hostapd that died before state promotion`; this is the
    controlled reproduction recorded above.
  - After implementation, the same fixture: PASS.  The injected child death
    yields a categorical failure, restores optional hostapd, leaves no required
    PID/marker/live watchdog, and emits no required-active success line.
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh`: PASS.
  - `bash scripts/test_tahoe_sae_quarantine_contract.sh`: PASS.
  - `bash scripts/run_tahoe_sae_quarantine_layer.sh`: PASS on the pinned
    isolated guest.  The Tahoe kext built successfully, all 959 undefined
    symbols resolved against BootKC, trace producers/Agent/RegDiag built, and
    the gate made no kext install/load/publish/release operation.
- verification boundary: this closes only a repository-local required-state
  publication race.  It is not a live AP result, candidate activation, guest
  reboot, PMF/BIP association, or proof that a process cannot die after the
  final immediate attestation.
- external blocker unchanged: the optional/required saved-profile identity
  preflight remains categorically mismatched.  No live configuration was read,
  changed, or bypassed.

## ANOMALY

- id: `LAB-PMF-AP-PRESTOP-CONFIG-FRESHNESS-20260721`
- status: `FIX_VERIFIED`
- scope: repository-owned staged-configuration admission for the AP helper;
  no kext, firmware, Apple80211, candidate, guest, or physical-AP claim.
- symptom: `do_activate()` validates the optional/required configuration pair
  before it writes rollback state and waits for watchdog readiness, but it does
  not bind those exact file contents to the later first hostapd transition.
- expected system behavior: the configuration shape admitted for a switchover
  must be the same configuration pair consumed at the first process mutation.
  Any unreadable, symlinked, or content-changed staged file during the bounded
  readiness interval must retain the already-running optional PMF process.
- actual behavior: `validate_config_pair()` runs before `write_state()`,
  `write_marker()`, and `start_watchdog()`.  After the asynchronous readiness
  interval, `stop_configured_hostapd()` and
  `start_configured_hostapd("$REQUIRED_CONFIG", ...)` use the paths without a
  current content equality check.  A changed required file can therefore be
  passed to hostapd despite having been absent from the original admission.
- divergence point: `scripts/tahoe_pmf_required_ap_switchover.sh::do_activate`.
- evidence:
  - local source establishes the exact pre-watchdog validation and later
    path-based hostapd launch order.
  - the documented AP boundary promises that configuration shape is validated
    before optional PMF is stopped; an earlier sample does not enforce that
    promise across the asynchronous owner-readiness interval.
  - deterministic no-AP runtime reproduction: the local fixture appended the
    valid but previously unadmitted `wpa_group_rekey` directive to its staged
    required file during the second network probe.  The current helper
    accepted the synthetic transition, and the fixture stopped with
    `activation accepted a staged configuration changed before
    optional-PMF stop` (exit 1).  No physical AP or real configuration was
    contacted.
  - decomp: not applicable; this is external laboratory transaction control.
- candidate causes:
  - confirmed: no hash-only configuration-pair baseline is compared at the
    final pre-stop admission edge.
- rejected causes:
  - saved-profile identity mismatch: it remains an earlier, categorical
    blocker and does not cover a mutation after its initial validation.
  - rekey host-network fencing: it occurs only after a required AP has already
    been started and cannot authorize the first process transition.
- confirmed deviation: a pre-watchdog configuration read is treated as if it
  authorized the later file bytes supplied to hostapd.
- root cause: `do_activate()` has no configuration freshness predicate between
  successful watchdog readiness and `stop_configured_hostapd()`.

## FIX_CANDIDATE

- anomaly_id: `LAB-PMF-AP-PRESTOP-CONFIG-FRESHNESS-20260721`
- symptom: a staged configuration can change after validation and still become
  the required-PMF hostapd input.
- expected system behavior: either staged file changing during readiness
  rejects the activation before optional PMF is stopped.
- actual behavior: validation checks selected directives and pair equality
  only before the bounded readiness interval; the later hostapd invocation is
  path-bound rather than content-bound.
- exact divergence point: the missing hash-only
  `config_pair_signature() == baseline` comparison immediately after the
  existing post-watchdog network fence and before the first hostapd stop.
- evidence from runtime: the local fake-IP route source altered only the
  fixture required configuration on activation's second signature probe.  The
  unmodified helper accepted its synthetic hostapd transition; the fixture
  deterministically reported that acceptance and exited 1, with no AP, route,
  or real configuration operation.
- evidence from decomp: not applicable; no Apple component owns staged
  hostapd file admission.
- exact semantic mismatch between reference and our code: no reference-driver
  path applies.  The mismatch is against the explicit SYSTEM_CONTRACT that the
  configuration shape checked before stopping optional PMF is the one used for
  the transition.
- fix justification path: `SYSTEM_CONTRACT_FIX`.
- enumerated system-facing touchpoints:
  1. optional hostapd lifecycle: no stop is permitted for stale configuration;
  2. required hostapd input: it must be exactly the validated file pair;
  3. watchdog/state/marker: a pre-stop rejection must reuse existing rollback
     owner cleanup while optional PMF remains active;
  4. staged files and credentials: they are read only into a hash pipeline and
     never rendered, copied, or changed;
  5. host networking, rekey, candidate, and guest: none is reached on this
     rejection path.
- expected contract at each touchpoint:
  1. the original optional PID remains live and no required PID appears;
  2. a changed or unreadable pair produces a categorical inconclusive result;
  3. no marker or live watchdog survives proven pre-stop cleanup;
  4. only SHA-256 digests are compared in-process, with no value emitted;
  5. no AP configuration command, network command, or guest operation occurs.
- why no relevant touchpoints are missing: the candidate adds read-only
  fingerprints around initial semantic validation and at the exact final
  boundary.  It does not alter hostapd command construction, config semantics,
  rollback, rekey, or any driver path.
- why proposed path adds no extra system-visible side effects: success adds
  only local content hashes around validation and at the final boundary;
  failure prevents an AP process transition.
  It adds no retry, delay, fallback, configuration write, state publication,
  or credential output.
- why this is root cause and not just correlation: the unbound file paths are
  passed directly to the first mutation after the stale validation interval;
  no intervening code compares their contents.
- why proposed fix is 1:1 with reference architecture and semantics: no Apple
  implementation applies.  This narrow host-side transaction admission fence
  mirrors the existing hash-only network freshness contract and retains the
  existing one-owner rollback architecture.
- files/functions to modify:
  - `scripts/tahoe_pmf_required_ap_switchover.sh::config_pair_signature` and
    `do_activate`;
  - `scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh` fake-IP
    mutation source and configuration-drift case;
  - `scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh` source-ordering
    assertion;
  - `docs/TAHOE_IWX_PMF_BIP_RUNTIME_PROTOCOL.md` freshness wording.
- forbidden alternative fixes considered and rejected:
  - re-run only directive validation, which cannot detect a changed but still
    syntactically valid unadmitted hostapd directive;
  - rely on later rollback after optional PMF has been stopped;
  - pin or rewrite staged configuration files, alter their permissions, or
    modify the physical AP;
  - treat the host-network hash as a substitute for configuration identity.
- verification plan:
  1. Completed: the new fixture confirmed that the pre-fix helper incorrectly
     accepted its controlled changed-file case without a real AP.
  2. Completed: the pair fingerprint fence made the same injection preserve
     the exact optional PID, create no required PID, clear the marker, and
     leave no live watchdog.
  3. Completed: PMF static/evidence contracts and the isolated Tahoe build-only
     gate passed.  No candidate activation, guest reboot, or live AP operation
     occurred.

## IMPLEMENTATION AND LOCAL VERIFICATION

- implementation:
  - `config_pair_signature()` verifies that both staged files are regular,
    non-symlink files and reduces their ordered contents to one in-process
    SHA-256 digest.  Neither file content nor digest is rendered by the
    helper.
  - `do_activate()` takes that baseline before directive validation, confirms
    the pair did not change while it was being validated, and compares it once
    more after watchdog readiness and the existing network freshness fence.
  - A changed or unreadable pair takes `finish_armed_rollback()` while optional
    PMF is still active, so required hostapd, rekey, and guest work are not
    reached.
  - The fixture changes only its generated required file by adding the valid
    but previously unadmitted `wpa_group_rekey` directive on the exact second
    network probe, then restores the generated file for later cases.
- deterministic verification:
  - Before implementation, the AP-helper fixture failed exactly at `activation
    accepted a staged configuration changed before optional-PMF stop`; this is
    the controlled reproduction recorded above.
  - After implementation, the same fixture: PASS.  Its configuration-drift
    case preserves the exact optional hostapd PID, creates no required PID,
    clears the marker, and leaves no live watchdog process.
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh`: PASS.
  - `bash scripts/test_tahoe_sae_quarantine_contract.sh`: PASS.
  - `bash scripts/run_tahoe_sae_quarantine_layer.sh`: PASS on the pinned
    isolated guest.  The Tahoe kext built successfully, all 959 undefined
    symbols resolved against BootKC, trace producers/Agent/RegDiag built, and
    the gate made no kext install/load/publish/release operation.
- verification boundary: this closes only the host-side staged-file admission
  freshness surface.  It is not a live AP result, candidate activation, guest
  reboot, PMF/BIP association, or a proof against a file change occurring after
  the first hostapd process transition has already begun.
- external blocker unchanged: the optional/required saved-profile identity
  preflight remains categorically mismatched.  No live configuration was read,
  changed, or bypassed.
