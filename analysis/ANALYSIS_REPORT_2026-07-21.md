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
