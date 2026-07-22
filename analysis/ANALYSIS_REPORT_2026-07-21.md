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

- id: `LAB-IWX-PMF-BIP-EXACT-REKEY-CARDINALITY-20260721`
- status: `FIX_VERIFIED`
- scope: safe categorical IWX PMF/BIP trace evaluation and its future runtime
  evidence gate; no physical AP, guest, candidate activation, or driver
  functionality result is claimed by this source-level finding.
- symptom: the sealed evaluator reports `CROSS_SLOT_REKEY_OBSERVED` whenever
  its internal `rekey_count` is nonzero.  It does not reject a second or later
  cross-slot transition in the same single capture episode.
- expected system behavior: the bounded runtime protocol permits exactly one
  post-initial AP stimulus: one canonical `REKEY_GTK` request.  A successful
  PMF/BIP evidence verdict must therefore contain the initial slot selection
  and exactly one subsequent opposite-slot publication/selection chain.  Any
  extra cross-slot transition is an unbounded or ambiguous event and must be
  inconclusive rather than attributed to the one permitted stimulus.
- actual behavior: after initial slot 4 and port-valid, the evaluator accepts
  a valid slot-5 chain, increments `rekey_count` to one, then accepts a second
  valid slot-4 chain, increments it to two, and returns
  `CROSS_SLOT_REKEY_OBSERVED` because its final predicate is
  `rekey_count != 0`.
- divergence point:
  - `include/ClientKit/AirportItlwmIwxPmfBipTraceContracts.h` increments
    `rekey_count` for every valid post-port-valid opposite-slot selection;
  - the final result maps every positive cardinality to the same success
    verdict;
  - neither the C fixture nor the C++ payload fixture covers a second valid
    cross-slot chain.
- evidence:
  - local source: the only final cardinality predicate is
    `return rekey_count != 0 ? ...CrossSlotRekeyObserved ...`.
  - deterministic local C probe, compiled only from the checked-in evaluator
    header and run without a kext/AP/guest, supplied the sealed sequence
    initial slot 4, valid slot 5 rekey, valid slot 4 rekey.  The current code
    returned `verdict=10 stage=0`, i.e. categorical cross-slot success with
    no missing stage.
  - `docs/TAHOE_IWX_PMF_BIP_RUNTIME_PROTOCOL.md` defines the sole
    post-initial stimulus as hostapd's canonical `REKEY_GTK`; the safety
    closure records hash-only fences around the one bounded group rekey.
  - decomp: not applicable.  This is a repository-owned evidence evaluator,
    not an assertion about an Apple binary path.
- candidate causes:
  - confirmed: the evaluator models existence of a cross-slot transition but
    not the cardinality of the bounded transaction it is meant to attest.
  - confirmed: the runner's one request and the trace's one episode do not by
    themselves prevent a timer-driven or otherwise independent extra rekey
    from being folded into the success verdict.
- rejected interpretations:
  - this does not prove that the lab AP ever emits a second rekey;
  - it does not justify issuing an additional AP command, widening the wait,
    or running a live PMF experiment;
  - it does not imply a driver BIP failure.  It is an evidence attribution
    boundary that must fail closed before runtime.
- confirmed deviation: the evaluator's accepted language is ``initial + one
  or more cross-slot chains'', while the documented bounded gate authorizes
  ``initial + exactly one cross-slot chain''.
- root cause: `rekey_count` is used only as a boolean instead of enforcing the
  exact cardinality of the one authorised rekey stimulus.

## FIX_CANDIDATE

- anomaly_id: `LAB-IWX-PMF-BIP-EXACT-REKEY-CARDINALITY-20260721`
- symptom: a trace containing two cross-slot IGTK transitions can be promoted
  to the same verdict as the one bounded rekey transaction.
- expected system behavior: a sealed IWX PMF/BIP success contains exactly one
  cross-slot transition after the initial active slot and port-valid boundary;
  a second transition returns `INTEGRITY_INCONCLUSIVE` with the categorical
  first-missing stage `cross-slot-rekey`.
- actual behavior: the second transition increments `rekey_count` and leaves
  the final `rekey_count != 0` success predicate true.
- exact divergence point: the selected-slot branch and final verdict mapping
  in `airport_itlwm_iwx_pmf_bip_trace_classify_entries_with_stage()`.
- evidence from runtime: the local C evaluator probe above deterministically
  returns a successful cross-slot verdict for two post-initial transitions;
  it performs no guest, hostapd, AP, network, firmware, or kext action.
- evidence from decomp: N/A.  The candidate is a safe trace-contract
  correction and changes no reverse-engineered implementation claim.
- exact semantic mismatch: a one-stimulus runtime contract is represented by
  an existential rather than exact-cardinality trace predicate.
- fix justification path: `DIAGNOSTIC_INSTRUMENTATION` with an evidence
  admission correction.  The evaluator only narrows a future PASS condition;
  it does not change association, key installation, radio, AP, or traffic
  behavior.
- exact hypotheses being disambiguated:
  - exactly the runner-authorised group-rekey chain occurred after initial
    PMF/BIP activation;
  - an additional AP/driver/environment transition occurred during the same
    capture and must not be attributed to that request.
- exact probe points: the existing categorical IGTK publication and selected
  slot events in one sealed, generation/episode-consistent trace; no new
  producer, trace field, raw packet, key material, timer, or runtime command
  is needed.
- why these probe points are sufficient: a second opposite-slot selection is
  the complete observable indication of a second cross-slot rekey within this
  deliberately categorical ABI.  Rejecting it preserves all single-rekey
  positive evidence and fails closed for an unbound extra transition.
- why instrumentation is behavior-neutral: the patch changes only local
  evaluator classification of already recorded fixed event IDs.  It adds no
  allocation, logging, event producer, AP command, guest operation, route,
  address, DHCP, radio, kext, firmware, or packet action.
- files/functions to modify:
  - `include/ClientKit/AirportItlwmIwxPmfBipTraceContracts.h`:
    reject `rekey_count > 1` at the second selected-slot transition before a
    final success can be reached;
  - `tests/iwx_pmf_bip_trace_contract_test.c`: add a sealed two-rekey negative
    fixture that currently demonstrates the false success and after the fix
    requires `INTEGRITY_INCONCLUSIVE/cross-slot-rekey`;
  - `tests/tahoe_payload_builders_test.cpp`: mirror that cardinality boundary
    through the C++ wrapper;
  - `docs/TAHOE_IWX_PMF_BIP_RUNTIME_PROTOCOL.md`: state the exact-one
    cross-slot admission rule;
  - `analysis/ANALYSIS_REPORT_2026-07-21.md`: record deterministic source
    evidence and verification.
- forbidden alternative fixes considered and rejected:
  - accepting multiple transitions because the runner made only one request;
  - adding a second live AP observation or a retry to determine which
    transition belongs to the request;
  - emitting key identifiers, timestamps, command cookies, or raw trace data
    to correlate the transition;
  - weakening the final verdict to accept a mixed/extra event sequence.
- verification plan:
  1. add the two-cross-slot fixture and confirm it fails on the pre-fix
     evaluator with the current success verdict;
  2. make the minimal evaluator cardinality check and require the fixture to
     return `INTEGRITY_INCONCLUSIVE/cross-slot-rekey`, while both single
     4-to-5 and 5-to-4 positives remain successful;
  3. run the trace contracts, full PMF static/evidence contracts, and the
     isolated Tahoe build-only gate; do not activate a candidate, reboot a
     guest, or touch the live AP.

## IMPLEMENTATION AND LOCAL VERIFICATION

- implementation:
  - the sealed evaluator now rejects the second otherwise-valid cross-slot
    selected-slot event once `rekey_count` is already nonzero, with
    `INTEGRITY_INCONCLUSIVE/cross-slot-rekey`;
  - the final mapping explicitly requires `rekey_count == 1` for
    `CROSS_SLOT_REKEY_OBSERVED`;
  - the C fixture and C++ wrapper fixture both cover initial slot 4, one
    valid slot-5 rekey, a second valid slot-4 rekey, and seal; both require
    the new fail-closed verdict while retaining the existing one-rekey 4-to-5
    and 5-to-4 positives;
  - the static trace contract and runtime protocol now make exact-one
    cardinality explicit.
- deterministic verification:
  - before implementation, the standalone local C probe returned
    `verdict=10 stage=0` for the sealed two-rekey sequence, demonstrating
    that the pre-fix evaluator emitted cross-slot success;
  - after implementation,
    `bash scripts/test_tahoe_post_plti_trace_contract.sh`: PASS, including
    the C and C++ IWX PMF/BIP trace matrices;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_evidence_contract.sh
    --self-test`: PASS;
  - `bash scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh`: PASS;
  - `bash scripts/test_tahoe_sae_quarantine_contract.sh`: PASS;
  - `bash scripts/run_tahoe_sae_quarantine_layer.sh`: PASS in the pinned
    isolated Tahoe guest.  The kext, trace client, Agent, and RegDiag built,
    and all 959 undefined symbols resolved against BootKC.  The gate made no
    kext install/load/publish/release operation.
- verification boundary: this narrows only the future safe-trace evidence
  admission predicate.  It is not a PMF-required association, group-rekey
  runtime observation, driver-functionality result, candidate activation,
  guest reboot, or live AP operation.
- external blocker unchanged: the optional/required saved-profile identity
  preflight remains categorically mismatched.  No live configuration was
  read, changed, or bypassed.

## ANOMALY: `LAB-PMF-AP-POSTSTART-NETWORK-ATTESTATION-20260721`

### Observation

`do_activate()` records its hash-only host route/address/forwarding signature
before it establishes the rollback owner and correctly rechecks it directly
before the first optional-PMF process mutation.  After that mutation,
however, it starts the required-PMF daemon, reattests only the required PID,
pinned AP shape, and staged configuration pair, and then calls
`mark_required_active()`.  There is no corresponding host-network equality
test at the state-promotion edge.  A route/address/forwarding change caused
while the required daemon starts can therefore be published as
`PMF_AP_SWITCHOVER=REQUIRED_ACTIVE`, even though the transaction baseline no
longer holds.

- scope: repository-owned disposable PMF-required AP helper and its local
  fake-hostapd fixture; no kext, firmware, Apple80211, candidate, guest, or
  physical-AP behavior claim.
- expected system behavior: a required-active state must mean that the exact
  required process, pinned AP shape, admitted configuration pair, and original
  hash-only host-network invariant all held at the promotion edge.  If the
  invariant is unreadable or changed after the optional-to-required process
  transition, required-active publication must be refused.  The established
  post-transition recovery must restore optional PMF and release ownership
  only if it can also reprove the original baseline; otherwise its independent
  rollback watchdog and marker remain armed.
- actual behavior: the successful path following
  `start_configured_hostapd("$REQUIRED_CONFIG", ...)` reaches required
  process/AP and configuration checks and then `mark_required_active()` with
  no `host_network_signature()` comparison against `network_signature`.
  `finish_post_transition_rollback()` contains the needed baseline comparison,
  but it is currently reached only after another failure.
- exact divergence point:
  `scripts/tahoe_pmf_required_ap_switchover.sh::do_activate()` between the
  successful required-process/AP post-start attestation and
  `mark_required_active()`.
- source evidence: the fake hostapd already has a fixture-only
  `FAKE_MUTATE_NETWORK_ON_REQUIRED_START=1` hook that writes a different
  fake-network state immediately when it is invoked for the required config.
  The existing failure case uses it together with `FAKE_FAIL_REQUIRED=1`, so
  it covers rollback after a failed required start but cannot prove the
  successful-promotion branch.

## FIX_CANDIDATE

- anomaly_id: `LAB-PMF-AP-POSTSTART-NETWORK-ATTESTATION-20260721`
- status: `FIX_VERIFIED`
- proposed change:
  1. extend the fixture with a fresh-state successful required-start case that
     enables only `FAKE_MUTATE_NETWORK_ON_REQUIRED_START=1`; before a fix the
     assertion must stop because the helper accepts activation and emits the
     required-active result;
  2. after the existing required-process/AP post-start reattestation, sample
     `host_network_signature()` and require equality with the transaction's
     `network_signature` before configuration/state promotion;
  3. on unreadable or changed post-start network state, use the existing
     `finish_post_transition_rollback()` result convention: report optional
     rollback verified only when optional process/AP and the original network
     baseline are all restored; otherwise retain the marker-bound watchdog;
  4. extend the static runtime contract and PMF runtime protocol to require
     this post-start network predicate before required-active publication.
- safety and side effects: the fix adds only read-only signature sampling to
  a local helper transaction.  It introduces no AP config rewrite, retry,
  restart loop, kext action, guest reboot, live access-point action, network
  mutation, or credential/identity output.  The fake state mutation exists
  only under the test fixture's generated environment.
- forbidden alternatives considered and rejected:
  - accepting the changed invariant and relying on a later runner-side
    assertion would still publish a false required-active helper state;
  - clearing marker/watchdog unconditionally after optional restart would
    contradict the established post-transition rollback proof;
  - changing the saved AP configuration or live route to make the signature
    match would expand scope and bypass the external preflight blocker.
- deterministic verification plan:
  - pre-fix, the fixture must fail at an assertion that successful required
    startup with injected network drift was accepted;
  - post-fix, the same local event must produce a categorical post-start
    network diagnostic, no `REQUIRED_ACTIVE`, no live required child, a live
    restored optional child, and retained marker/watchdog while fake network
    output remains drifted;
  - restoring only generated fake network output and invoking the existing
    explicit rollback must clear the retained owner and report normal optional
    restoration;
  - run fixture, static contracts, trace/evidence self-tests, and the pinned
    isolated Tahoe build-only gate.  No live AP/candidate/guest operation is
    authorized.

## IMPLEMENTATION AND LOCAL VERIFICATION: `LAB-PMF-AP-POSTSTART-NETWORK-ATTESTATION-20260721`

- implementation:
  - `do_activate()` now retains a local `post_start_failure` reason after the
    exact required PID/AP-shape reattestation and samples the existing
    hash-only `host_network_signature()` once more before configuration/state
    promotion;
  - an unreadable or changed value relative to the admitted
    `network_signature` takes `finish_post_transition_rollback()`.  It emits
    `optional rollback verified` only if the already-existing recovery helper
    restores optional PMF and re-proves the saved baseline; otherwise it
    retains marker/watchdog ownership and reports the categorical armed result;
  - the fixture now covers a successful required-hostapd launch that changes
    only its generated fake-network source.  It requires no required-active
    result, no required child, restored live optional child, and retained
    marker/watchdog until a stable explicit rollback;
  - the static contract requires the new post-start signature comparison after
    required process/AP attestation and before configuration/state promotion,
    and the runtime protocol records the required-active invariant.
- deterministic fixture evidence:
  - before implementation, the new fixture stopped at
    `activation accepted host-network drift during successful required start`,
    directly proving that the old helper published a successful required state
    after its fake required launch had changed the signature;
  - after implementation, the same source-local event reports
    `required-PMF host-network invariants changed before state promotion;
    rollback watchdog remains armed`, publishes no `REQUIRED_ACTIVE`, quiesces
    the generated required child, and restores the generated optional child;
  - returning only the generated fake-network state to its baseline lets the
    following explicit rollback report `PMF_AP_ROLLBACK=OPTIONAL_RESTORED` and
    remove the retained marker/watchdog.
- verification:
  - `bash scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh`: PASS;
  - `bash scripts/test_tahoe_post_plti_trace_contract.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_evidence_contract.sh
    --self-test`: PASS;
  - `bash scripts/test_tahoe_sae_quarantine_contract.sh`: PASS;
  - `bash scripts/run_tahoe_sae_quarantine_layer.sh`: PASS in the pinned,
    isolated Tahoe build directory (source identity `dirty5e8364e5fafd`).
    The kext, trace producer, Agent, and RegDiag built; all 959 undefined
    symbols resolved against BootKC; no kext was installed, loaded, published,
    or released.
- verification boundary: this closes only a host-side required-active
  promotion fence.  It is not a live hostapd result, PMF-required association,
  IGTK observation, candidate activation, guest reboot, or driver-functionality
  result.
- external blocker unchanged: the optional/required saved-profile identity
  preflight remains categorically mismatched.  No live configuration was
  read, changed, or bypassed.

## ANOMALY: `LAB-PMF-AP-WATCHDOG-PREPROMOTION-ATTESTATION-20260721`

### Observation

The helper now proves its independent marker-bound watchdog immediately before
the first optional-PMF stop, but after the AP process transition it reattests
only required hostapd, AP shape, host-network, and configuration inputs.  It
can write `state=required` and publish `PMF_AP_SWITCHOVER=REQUIRED_ACTIVE`
without re-reading `watchdog_owner_is_current()`.  A watchdog that exits while
required hostapd starts leaves required PMF active with no autonomous
restoration owner if the caller subsequently disappears.

- scope: repository-owned PMF-required AP transition helper and its generated
  local fixture only; no kext, firmware, Apple80211, candidate, guest, or
  physical-AP behavior claim.
- expected system behavior: `REQUIRED_ACTIVE` must be committed only while an
  exact marker/state-bound independent watchdog still owns a bounded recovery
  path.  A missing, dead, replaced, or mismatched receipt before promotion must
  refuse required-active publication and use the existing post-transition
  recovery.  If synchronous recovery restores optional PMF and all existing
  postconditions hold, it may report verified optional rollback; otherwise it
  must leave recovery ownership intact.
- actual behavior: `do_activate()` invokes `watchdog_owner_is_current()` only
  in its pre-stop admission chain.  Its successful post-start sequence reaches
  `mark_required_active()` after process/AP, network, and configuration checks
  without any final watchdog identity predicate.
- exact divergence point:
  `scripts/tahoe_pmf_required_ap_switchover.sh::do_activate()` between the
  post-start configuration check and `mark_required_active()`.
- source-local proof mechanism: fake `ip` already counts route probes and can
  kill/remove only a generated watchdog receipt at a selected route call.  On
  a normal activation the first two probes are baseline/pre-stop; the third is
  the post-start network reattestation.  Killing the watchdog at that third
  probe leaves the fake network stable and lets the unmodified helper publish
  required-active, proving the stale-owner promotion path without a real AP,
  guest, route, or profile action.

## FIX_CANDIDATE

- anomaly_id: `LAB-PMF-AP-WATCHDOG-PREPROMOTION-ATTESTATION-20260721`
- status: `FIX_VERIFIED`
- proposed change:
  1. add a fresh-state fixture case that terminates only its generated watchdog
     during the third activation route probe, after required startup begins;
     before a fix it must stop because activation was accepted;
  2. reattest `watchdog_owner_is_current()` after all required process/AP,
     host-network, and staged-configuration promotion predicates and directly
     before `mark_required_active()`;
  3. on failure, take the established `finish_post_transition_rollback()`
     path and use its existing verified-vs-armed diagnostic convention;
  4. require that final ordering in the static runtime contract and document
     the independent-owner predicate for required-active publication.
- safety and side effects: this adds no process start/retry, configuration
  write, network mutation, kext operation, guest action, or live AP action.
  The only synthetic kill occurs in the generated local fixture and targets
  its own temporary watchdog PID.
- forbidden alternatives considered and rejected:
  - treating a pre-stop owner proof as permanent ignores the interval when the
    optional AP has already been stopped and recovery ownership matters most;
  - publishing required state and trusting later runner cleanup cannot protect
    against a caller exit before cleanup;
  - adding a replacement watchdog after loss would introduce an unbounded
    retry/recovery policy rather than refusing the invalid promotion.
- deterministic verification plan:
  - pre-fix, the isolated fixture must fail at its assertion that required
    activation accepted a watchdog killed during required startup;
  - post-fix, it must emit no `REQUIRED_ACTIVE`, restore a live generated
    optional process, leave no generated required process/marker/watchdog
    receipt, and report verified optional rollback under its unchanged fake
    network signature;
  - run local fixture, static contracts, trace/evidence tests, and the pinned
    isolated Tahoe build-only gate.  Do not touch any live AP, candidate, or
    guest.

## IMPLEMENTATION AND LOCAL VERIFICATION: `LAB-PMF-AP-WATCHDOG-PREPROMOTION-ATTESTATION-20260721`

- implementation:
  - `do_activate()` now invokes the existing exact
    `watchdog_owner_is_current()` predicate after its required process/AP,
    host-network, and configuration promotion predicates and immediately
    before `mark_required_active()`;
  - a failed final owner proof uses `finish_post_transition_rollback()`.
    It therefore restores optional PMF and releases state only under the
    existing optional-process/AP/network recovery proof; no replacement
    watchdog, retry, or live-network operation was added;
  - the local fixture uses the existing generated route-call discriminator to
    kill/remove only its temporary watchdog receipt at the third activation
    route probe, after required hostapd has started; the static contract now
    enforces this final owner predicate before state promotion, and the
    runtime protocol makes it part of required-active semantics.
- deterministic fixture evidence:
  - before implementation, the isolated test stopped at
    `activation accepted a watchdog that died during required-PMF startup`,
    directly proving that the old source emitted required-active without its
    independent restoration owner;
  - after implementation, the same fake-only event reports
    `rollback watchdog is not exact before required-PMF state promotion;
    optional rollback verified`, emits no `REQUIRED_ACTIVE`, restores a live
    generated optional process, quiesces the generated required process, and
    leaves no marker or stale watchdog receipt;
  - the static activation ordering requires process/AP -> host network ->
    staged configuration -> exact watchdog owner -> `mark_required_active`,
    and counts all seven post-transition failure paths that verify recovery.
- verification:
  - `bash scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh`: PASS;
  - `bash scripts/test_tahoe_post_plti_trace_contract.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_evidence_contract.sh
    --self-test`: PASS;
  - `bash scripts/test_tahoe_sae_quarantine_contract.sh`: PASS;
  - `bash scripts/run_tahoe_sae_quarantine_layer.sh`: PASS in the pinned,
    isolated Tahoe build directory (source identity `dirty0483a17fa681`).
    The kext, trace producer, Agent, and RegDiag built; all 959 undefined
    symbols resolved against BootKC; no kext was installed, loaded, published,
    or released.
- verification boundary: this closes only a host-side rollback-owner liveness
  fence.  It is not a live hostapd result, PMF-required association, IGTK
  observation, candidate activation, guest reboot, or driver-functionality
  result.
- external blocker unchanged: the optional/required saved-profile identity
  preflight remains categorically mismatched.  No live configuration was
  read, changed, or bypassed.

## ANOMALY: `LAB-PMF-AP-FINAL-REQUIRED-PROMOTION-ATTESTATION-20260721`

### Observation

`do_activate()` initially reattests the required hostapd PID and pinned AP
shape after startup, then performs post-start host-network, configuration, and
watchdog-owner admission work before it writes `state=required`.  That work
creates a second interval in which the previously attested required process
can exit or be replaced.  The final watchdog check does not prove required
hostapd ownership, so the current source can still publish
`PMF_AP_SWITCHOVER=REQUIRED_ACTIVE` from stale process evidence.

- scope: repository-owned PMF-required AP helper and generated local fixture;
  no kext, firmware, Apple80211, candidate, guest, or physical-AP behavior
  claim.
- expected system behavior: required-active publication must commit only after
  the exact required PID/configuration and pinned AP shape are observed at the
  final promotion edge, after all network/configuration/rollback-owner gates.
  A process loss during those later gates must refuse success and use the
  existing verified-or-armed post-transition recovery convention.
- actual behavior: `configured_hostapd_active("$REQUIRED_CONFIG", ...)` and
  `runtime_ap_is_pinned()` occur before the later
  `host_network_signature()`, `config_pair_signature()`, and final
  `watchdog_owner_is_current()` checks; `mark_required_active()` follows
  without a second required-process/AP predicate.
- exact divergence point:
  `scripts/tahoe_pmf_required_ap_switchover.sh::do_activate()` between the
  final watchdog-owner check and `mark_required_active()`.
- source-local proof mechanism: the generated fake `ip` supports an exact
  route-call hook that kills/removes only the temporary required PID receipt.
  Targeting activation's third route probe kills required hostapd during the
  post-start host-network read, after its first process/AP reattestation but
  before configuration/watchdog/state promotion.  The unmodified source still
  emits required-active; no real AP, guest, route, identity, or credential is
  read or changed.

## FIX_CANDIDATE

- anomaly_id: `LAB-PMF-AP-FINAL-REQUIRED-PROMOTION-ATTESTATION-20260721`
- status: `FIX_VERIFIED`
- proposed change:
  1. add the fresh-state third-route required-process-loss fixture; before a
     fix it must fail at an assertion that activation was accepted;
  2. repeat the existing exact required PID/configuration and pinned AP-shape
     predicate after final network/configuration/watchdog admission and
     immediately before `mark_required_active()`;
  3. use `finish_post_transition_rollback()` on that final process/AP failure
     and preserve its existing categorical outcome semantics;
  4. extend static ordering and the PMF runtime protocol with the final
     required-process predicate.
- safety and side effects: this is a read-only process/AP reattestation plus a
  generated fixture kill.  It creates no retry, daemon replacement policy,
  configuration mutation, network mutation, kext action, guest reboot, or
  real AP operation.
- forbidden alternatives considered and rejected:
  - treating the earlier startup attestation as current across later blocking
    probes falsely conflates an observation with promotion ownership;
  - marking required state first and relying on later rollback can leave a
    caller crash with a false required receipt;
  - adding a timer or automatic restart to see whether required comes back
    would add unbounded behavior outside the bounded transaction.
- deterministic verification plan:
  - pre-fix, the isolated fixture reports that activation accepted a required
    process killed on its post-start route probe;
  - post-fix, it emits no required-active success, stops no extra real process,
    restores its generated optional process, removes the marker and stops its
    watchdog under the stable fake baseline, and produces the final
    required-process diagnostic;
  - run fixture, static contracts, trace/evidence self-tests, and the pinned
    isolated Tahoe build-only gate, without live AP/candidate/guest action.

## IMPLEMENTATION AND LOCAL VERIFICATION: `LAB-PMF-AP-FINAL-REQUIRED-PROMOTION-ATTESTATION-20260721`

- implementation:
  - after final host-network, configuration, and exact-watchdog admission,
    `do_activate()` now repeats the existing required PID/configuration and
    pinned AP-shape predicates immediately before `mark_required_active()`;
  - a failed final predicate uses `finish_post_transition_rollback()`, so its
    recovery behavior remains bounded by the pre-existing optional-process/AP/
    network proof and does not add a restart, retry, kext, guest, AP-config, or
    host-network action;
  - the generated fixture now kills only its temporary required process and
    PID receipt on the third route probe.  The static contract requires the
    final required PID/AP predicate after the watchdog check, and the protocol
    describes the repeated actual-commit attestation.
- deterministic fixture evidence:
  - before implementation, the new test stopped at
    `activation accepted a required hostapd that died before final state
    promotion`, directly demonstrating a stale-required-process
    `REQUIRED_ACTIVE` result;
  - after implementation, the same fake-only process loss emits
    `required-PMF hostapd is not exact before final state promotion; optional
    rollback verified`, emits no required-active result, restores a live
    generated optional process, leaves no generated required PID or marker,
    and terminates its generated watchdog;
  - the static promotion order is now required process/AP -> host network ->
    staged configuration -> exact watchdog -> required process/AP -> state
    promotion, with all eight post-transition recovery paths accounted for.
- verification:
  - `bash scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh`: PASS;
  - `bash scripts/test_tahoe_post_plti_trace_contract.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_evidence_contract.sh
    --self-test`: PASS;
  - `bash scripts/test_tahoe_sae_quarantine_contract.sh`: PASS;
  - `bash scripts/run_tahoe_sae_quarantine_layer.sh`: PASS in the pinned,
    isolated Tahoe build directory (source identity `dirty5e16c147eeec`).
    The kext, trace producer, Agent, and RegDiag built; all 959 undefined
    symbols resolved against BootKC; no kext was installed, loaded, published,
    or released.
- verification boundary: this closes only a host-side required-process
  freshness fence.  It is not a live hostapd result, PMF-required association,
  IGTK observation, candidate activation, guest reboot, or driver-functionality
  result.
- external blocker unchanged: the optional/required saved-profile identity
  preflight remains categorically mismatched.  No live configuration was
  read, changed, or bypassed.

## ANOMALY: `LAB-PMF-AP-REKEY-WATCHDOG-COMMAND-ATTESTATION-20260721`

### Observation

The bounded rekey path verifies marker/state, staged configuration, required
hostapd, AP shape, and host-network baseline, and it repeats required-process
proof immediately before it records the sole raw `REKEY_GTK` request.  It does
not prove that the independent marker-bound watchdog remains alive at that
command edge.  A watchdog that dies after activation can therefore allow the
only permitted AP control side effect and a rekey success witness while there
is no autonomous bounded rollback owner.

- scope: repository-owned PMF-required rekey helper and generated local
  fixture only; no kext, firmware, Apple80211, candidate, guest, or physical
  AP behavior claim.
- expected system behavior: raw `REKEY_GTK` must be admitted only under the
  exact required process/AP, unmodified configuration/network state, and a
  current independent marker/state-bound watchdog.  A lost owner before the
  raw command must be categorical, issue no raw request, consume no one-shot
  request receipt, and leave the existing rollback path available.
- actual behavior: `do_rekey()` goes from its final
  `configured_hostapd_active("$REQUIRED_CONFIG", ...)` directly to
  `record_rekey_request()` and raw control without `watchdog_owner_is_current`.
- exact divergence point:
  `scripts/tahoe_pmf_required_ap_switchover.sh::do_rekey()` between the final
  required-process check and `record_rekey_request()`.
- source-local proof mechanism: fake `ip` can kill/remove only the temporary
  watchdog receipt at the next route probe.  The first rekey signature probe
  occurs after required process/AP admission and before the final command-edge
  process check.  With unchanged fake network output, the unmodified helper
  still records/sends raw `REKEY_GTK` and publishes success, without any live
  AP, guest, route, profile, or credential operation.

## FIX_CANDIDATE

- anomaly_id: `LAB-PMF-AP-REKEY-WATCHDOG-COMMAND-ATTESTATION-20260721`
- status: `FIX_VERIFIED`
- proposed change:
  1. add a fresh generated required transaction whose watchdog is killed only
     at its first rekey route probe; before a fix the fixture must fail because
     a raw CLI request was accepted;
  2. invoke `watchdog_owner_is_current()` after final required-process
     reattestation and immediately before `record_rekey_request()`;
  3. reject a missing/dead/replaced owner before receipt write and raw control,
     preserving the existing explicit rollback cleanup path;
  4. require this ordering in the static contract and describe command-edge
     watchdog ownership in the runtime protocol.
- safety and side effects: the change is an existing read-only PID/argv/state
  predicate and a fixture-only temporary PID kill.  It adds no retry, AP
  restart, configuration/network mutation, kext operation, guest action, or
  live AP action.
- forbidden alternatives considered and rejected:
  - relying on activation-time watchdog proof ignores subsequent lease/process
    loss before a later raw AP control side effect;
  - recording the one-shot request before owner proof would consume the only
    permissible stimulus even though no command could safely be authorized;
  - adding a new watchdog at rekey time would alter transaction ownership and
    create a second recovery policy.
- deterministic verification plan:
  - pre-fix, the local fixture must stop at an assertion that rekey accepted a
    watchdog killed before its raw command edge;
  - post-fix, it must reach no fake CLI request and create neither request nor
    success receipt, retain the required state for one normal explicit rollback,
    and report the categorical owner diagnostic;
  - run fixture, static contracts, trace/evidence self-tests, and the pinned
    isolated Tahoe build-only gate without live AP/candidate/guest action.

## IMPLEMENTATION AND LOCAL VERIFICATION: `LAB-PMF-AP-REKEY-WATCHDOG-COMMAND-ATTESTATION-20260721`

- implementation:
  - `do_rekey()` now invokes the pre-existing exact
    `watchdog_owner_is_current()` predicate after its final required-process
    command-edge reattestation and immediately before it can write
    `rekey.requested` or send raw `REKEY_GTK`;
  - a failed owner predicate is categorical and leaves the one-shot request
    receipt untouched, so it authorizes neither a raw command nor a retry;
    existing explicit rollback remains the only cleanup path for the generated
    required state;
  - the local fixture kills/removes only its temporary watchdog on the first
    rekey route probe.  Static ordering now requires final required-process ->
    exact watchdog -> request receipt -> raw command, and the protocol records
    that command-edge ownership predicate.
- deterministic fixture evidence:
  - before implementation, the isolated test stopped at
    `group rekey accepted a watchdog that died before the raw command edge`,
    proving that the old helper sent its fake raw command and published a
    success path without an independent owner;
  - after implementation, the same fake-only event reports
    `rollback watchdog is not exact before bounded group-rekey`, reaches no
    fake CLI request, writes neither `rekey.requested` nor `rekey.status`, and
    retains the generated required state/marker for the next explicit rollback;
  - that explicit rollback restores generated optional PMF and clears the
    marker even though the fixture-only watchdog receipt was removed.
- verification:
  - `bash scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh`: PASS;
  - `bash scripts/test_tahoe_post_plti_trace_contract.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_evidence_contract.sh
    --self-test`: PASS;
  - `bash scripts/test_tahoe_sae_quarantine_contract.sh`: PASS;
  - `bash scripts/run_tahoe_sae_quarantine_layer.sh`: PASS in the pinned,
    isolated Tahoe build directory (source identity `dirty54306ea56392`).
    The kext, trace producer, Agent, and RegDiag built; all 959 undefined
    symbols resolved against BootKC; no kext was installed, loaded, published,
    or released.
- verification boundary: this closes only a host-side raw-rekey admission
  fence.  It is not a live hostapd result, PMF-required association, IGTK
  observation, candidate activation, guest reboot, or driver-functionality
  result.
- external blocker unchanged: the optional/required saved-profile identity
  preflight remains categorically mismatched.  No live configuration was
  read, changed, or bypassed.

## ANOMALY: `LAB-PMF-AP-REKEY-FINAL-REQUIRED-ATTESTATION-20260721`

### Observation

After raw `REKEY_GTK` returns `OK`, `do_rekey()` reattests required hostapd
and AP shape, then reads the post-command host-network signature before it
writes `rekey.status` and emits success.  The network probe creates an interval
in which the previously attested required PID can exit or be replaced.  The
current source does not repeat its required PID/AP predicate after that probe,
so it can publish a categorical rekey success witness from stale process
evidence.

- scope: repository-owned bounded rekey helper and generated fixture only; no
  kext, firmware, Apple80211, candidate, guest, or physical-AP behavior claim.
- expected system behavior: `PMF_AP_REKEY=REQUESTED` and its success receipt
  require the exact required process/configuration and pinned AP shape at the
  actual final success edge, after both post-ack network comparison and the
  raw command's one-shot receipt.  A post-ack process loss must remain
  inconclusive and leave no success witness.
- actual behavior: `configured_hostapd_active("$REQUIRED_CONFIG", ...)` and
  `runtime_ap_is_pinned()` precede the post-command
  `host_network_signature()`; after the comparison the helper writes
  `rekey.status` without a final required process/AP reattestation.
- exact divergence point:
  `scripts/tahoe_pmf_required_ap_switchover.sh::do_rekey()` between the
  post-command host-network comparison and `rekey_requested=true`.
- source-local proof mechanism: the fake route helper can kill/remove only
  its generated required hostapd PID receipt on the second rekey route probe
  (the post-ack network read).  It keeps fake network output stable.  The
  unmodified helper nevertheless writes its rekey success witness and emits
  success, without any real AP, guest, route, identity, or credential action.

## FIX_CANDIDATE

- anomaly_id: `LAB-PMF-AP-REKEY-FINAL-REQUIRED-ATTESTATION-20260721`
- status: `FIX_VERIFIED`
- proposed change:
  1. add a fresh generated transaction that kills required hostapd only on the
     second rekey route probe; before a fix it must fail because rekey success
     was accepted;
  2. repeat the existing exact required PID/configuration and pinned AP-shape
     predicate after post-command network comparison and directly before
     `rekey.status` success publication;
  3. on loss, fail categorically after the already-consumed one-shot request
     while withholding `rekey.status`; leave existing explicit rollback for
     cleanup;
  4. require final required process/AP ordering in the static contract and
     state the final-success boundary in the runtime protocol.
- safety and side effects: this adds only read-only reattestation and a
  temporary fixture-only child kill.  No retry, additional raw command,
  restart, configuration/network mutation, kext operation, guest action, or
  live AP action is introduced.
- forbidden alternatives considered and rejected:
  - treating the first post-ack process observation as proof through later
    network reads can publish a false success witness;
  - retrying the raw command after a lost process breaks the exact-one command
    invariant;
  - restarting required hostapd to regain evidence changes AP behavior outside
    the bounded, observational runtime contract.
- deterministic verification plan:
  - pre-fix, the local fixture must stop at its assertion that rekey accepted a
    required process killed before final success publication;
  - post-fix, it must issue exactly the one already-authorized fake raw command
    but publish no success witness, retain its one-shot receipt, and clean up
    only via a subsequent explicit rollback;
  - run fixture, static contracts, trace/evidence self-tests, and the pinned
    isolated Tahoe build-only gate without live AP/candidate/guest action.

## IMPLEMENTATION AND LOCAL VERIFICATION: `LAB-PMF-AP-REKEY-FINAL-REQUIRED-ATTESTATION-20260721`

- implementation:
  - after its post-ack host-network equality check, `do_rekey()` now repeats
    the existing exact required PID/configuration and pinned AP-shape
    predicates immediately before it writes `rekey.status` or reports
    `PMF_AP_REKEY=REQUESTED`;
  - a failed final process/AP predicate withholds the success witness while
    retaining the already-written one-shot request receipt.  It introduces no
    second raw command, retry, restart, configuration/network mutation, kext,
    guest, or live AP operation;
  - the fixture's generated fake route probe now covers a required child loss
    on the second rekey route call.  Static ordering requires post-ack network
    comparison -> final required PID/AP -> success receipt, and the protocol
    documents that final success edge.
- deterministic fixture evidence:
  - before implementation, the test stopped at
    `group rekey accepted a required hostapd that died before final success
    publication`, proving a false success result after the one permitted raw
    control command;
  - after implementation, that same fixture-only loss reaches exactly one fake
    raw CLI request and preserves `rekey.requested`, but reports
    `required-PMF hostapd process is not exact before rekey success
    publication`, writes no `rekey.status`, and emits no success result;
  - the following explicit rollback restores generated optional PMF, removes
    its marker, and terminates the still-live generated watchdog.
- verification:
  - `bash scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh`: PASS;
  - `bash scripts/test_tahoe_post_plti_trace_contract.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_evidence_contract.sh
    --self-test`: PASS;
  - `bash scripts/test_tahoe_sae_quarantine_contract.sh`: PASS;
  - `bash scripts/run_tahoe_sae_quarantine_layer.sh`: PASS in the pinned,
    isolated Tahoe build directory (source identity `dirty5cf0dac63169`).
    The kext, trace producer, Agent, and RegDiag built; all 959 undefined
    symbols resolved against BootKC; no kext was installed, loaded, published,
    or released.
- verification boundary: this closes only a host-side bounded-rekey success
  freshness fence.  It is not a live hostapd result, PMF-required association,
  IGTK observation, candidate activation, guest reboot, or driver-functionality
  result.
- external blocker unchanged: the optional/required saved-profile identity
  preflight remains categorically mismatched.  No live configuration was
  read, changed, or bypassed.

## ANOMALY: `LAB-PMF-AP-REKEY-FINAL-WATCHDOG-ATTESTATION-20260721`

### Observation

The rekey command edge now requires an exact independent watchdog before raw
`REKEY_GTK`, and the success edge reattests required hostapd/AP after the final
network read.  It still does not prove that the watchdog survived the raw
command and post-ack network probe before `rekey.status` is written.  A
watchdog loss during that interval can therefore produce a categorical rekey
success witness despite no longer having an autonomous rollback owner.

- scope: repository-owned bounded rekey helper and generated fixture only; no
  kext, firmware, Apple80211, candidate, guest, or physical-AP behavior claim.
- expected system behavior: a rekey success witness must be emitted only while
  the required process/AP, network/configuration input, and exact
  marker/state-bound watchdog all remain current at its final publication edge.
  A post-command owner loss is inconclusive: the already-issued raw request
  remains consumed, but no success witness is permitted.
- actual behavior: after post-command network and final process/AP checks,
  `do_rekey()` writes `rekey.status` without a second
  `watchdog_owner_is_current()` check.
- exact divergence point:
  `scripts/tahoe_pmf_required_ap_switchover.sh::do_rekey()` between final
  required process/AP attestation and `rekey_requested=true`.
- source-local proof mechanism: the generated fake `ip` can kill/remove only
  its temporary watchdog receipt on rekey's second route probe (the post-ack
  network read) while preserving the fake baseline.  The unmodified source
  still publishes success, without contacting a real AP/guest or changing a
  real route, identity, profile, or credential.

## FIX_CANDIDATE

- anomaly_id: `LAB-PMF-AP-REKEY-FINAL-WATCHDOG-ATTESTATION-20260721`
- status: `FIX_VERIFIED`
- proposed change:
  1. add a fresh generated rekey transaction that kills its watchdog only on
     the second route probe; before a fix the fixture must fail because success
     was accepted;
  2. invoke the existing exact watchdog predicate after final required/AP
     reattestation and immediately before rekey success receipt publication;
  3. withhold `rekey.status` and output success on loss while retaining the
     already-consumed one-shot receipt and existing explicit rollback cleanup;
  4. extend static ordering and protocol wording for final-success owner proof.
- safety and side effects: this adds only a read-only PID/argv/state predicate
  and a local generated watchdog kill.  It creates no extra raw command,
  retry, AP restart, configuration/network mutation, kext action, guest action,
  or live AP operation.
- forbidden alternatives considered and rejected:
  - treating pre-command owner proof as proof at final result publication
    ignores post-command watchdog loss;
  - issuing another rekey after loss violates the exact-one raw command rule;
  - silently writing success and letting later cleanup discover loss makes a
    categorical runtime witness unreliable.
- deterministic verification plan:
  - pre-fix, the local fixture must stop at its assertion that rekey accepted a
    watchdog killed before final success publication;
  - post-fix, it must send exactly one already-authorized fake raw command,
    retain only the request receipt, emit no success witness, and then restore
    optional PMF only through its explicit rollback;
  - run fixture, static contracts, trace/evidence self-tests, and the pinned
    isolated Tahoe build-only gate without live AP/candidate/guest action.

## IMPLEMENTATION AND LOCAL VERIFICATION: `LAB-PMF-AP-REKEY-FINAL-WATCHDOG-ATTESTATION-20260721`

- implementation:
  - after final required PID/AP reattestation, `do_rekey()` now repeats the
    existing exact `watchdog_owner_is_current()` predicate immediately before
    it can write `rekey.status` or emit its categorical success result;
  - a final owner loss leaves the previously authorized one-shot raw request
    consumed but withholds the success witness.  It adds no extra command,
    retry, restart, configuration/network mutation, kext, guest, or live AP
    action;
  - the fixture kills/removes only its generated watchdog at the second rekey
    route probe.  Static ordering now requires final required PID/AP -> exact
    watchdog -> success receipt, and the protocol describes that final owner
    proof.
- deterministic fixture evidence:
  - before implementation, the isolated fixture stopped at
    `group rekey accepted a watchdog that died before final success
    publication`, proving false success after an otherwise valid fake raw
    command;
  - after implementation, the same event reaches exactly one fake raw request
    and retains `rekey.requested`, but reports
    `rollback watchdog is not exact before rekey success publication`, writes
    no `rekey.status`, and emits no success result;
  - explicit rollback then restores generated optional PMF and clears its
    marker despite the fixture-only missing watchdog receipt.
- verification:
  - `bash scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh`: PASS;
  - `bash scripts/test_tahoe_post_plti_trace_contract.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_evidence_contract.sh
    --self-test`: PASS;
  - `bash scripts/test_tahoe_sae_quarantine_contract.sh`: PASS;
  - `bash scripts/run_tahoe_sae_quarantine_layer.sh`: PASS in the pinned,
    isolated Tahoe build directory (source identity `dirty6c56245c6a9b`).
    The kext, trace producer, Agent, and RegDiag built; all 959 undefined
    symbols resolved against BootKC; no kext was installed, loaded, published,
    or released.
- verification boundary: this closes only a host-side final rekey-owner
  freshness fence.  It is not a live hostapd result, PMF-required association,
  IGTK observation, candidate activation, guest reboot, or driver-functionality
  result.
- external blocker unchanged: the optional/required saved-profile identity
  preflight remains categorically mismatched.  No live configuration was
  read, changed, or bypassed.

## ANOMALY: `LAB-PMF-AP-REKEY-FINAL-CONFIG-ATTESTATION-20260721`

### Observation

`do_rekey()` binds the staged optional/required configuration pair to the
transaction before it issues raw `REKEY_GTK`, but its post-ack success path
checks process/AP, network, and watchdog only.  A staged file can change while
the raw command or final network probe runs; the helper can then write
`rekey.status` even though the rollback configuration baseline has become
unresolved and a later optional-PMF restart must be refused.

- scope: repository-owned bounded rekey helper and generated local fixture;
  no kext, firmware, Apple80211, candidate, guest, or physical-AP behavior
  claim.
- expected system behavior: the rekey success witness must attest the same
  staged configuration pair recorded in restricted state.  A changed or
  unreadable pair after the raw command makes its outcome inconclusive, holds
  no success receipt, and requires restoring the generated baseline before an
  explicit rollback may restart optional PMF.
- actual behavior: `config_pair_matches_state()` runs only before raw control.
  After post-command network/process/AP/watchdog checks, `do_rekey()` writes
  `rekey.status` without checking that the state-bound configuration pair is
  still current.
- exact divergence point:
  `scripts/tahoe_pmf_required_ap_switchover.sh::do_rekey()` between final
  post-command attestation and `rekey_requested=true`.
- source-local proof mechanism: fake `ip` can append only a syntactically
  acceptable generated directive to its temporary required config on rekey's
  second route probe, after raw ACK and before success publication.  The
  unmodified helper accepts success.  Restoring only the generated fixture file
  then allows its normal explicit rollback; no real AP/configuration/identity
  is touched.

## FIX_CANDIDATE

- anomaly_id: `LAB-PMF-AP-REKEY-FINAL-CONFIG-ATTESTATION-20260721`
- status: `FIX_VERIFIED`
- proposed change:
  1. add a fresh generated transaction that mutates only its temporary staged
     required config on the second rekey route probe; before a fix the fixture
     must fail because success was accepted;
  2. repeat `config_pair_matches_state()` after post-command network/process/
     AP/watchdog checks and immediately before success receipt publication;
  3. withhold `rekey.status` while retaining the already-consumed raw-command
     receipt and require fixture baseline restoration before explicit rollback;
  4. require this ordering in static contract and protocol language.
- safety and side effects: this is a read-only digest comparison and a local
  fixture-only temp-file append.  It adds no raw retry, daemon restart,
  configuration rewrite in production, network mutation, kext action, guest
  action, or live AP operation.
- forbidden alternatives considered and rejected:
  - assuming the pre-command configuration read remains true across the raw
    command leaves state/rollback authority stale;
  - restarting optional from a changed pair violates existing rollback guards;
  - treating the rekey as successful because hostapd was already running hides
    an unresolved staged input needed by later recovery.
- deterministic verification plan:
  - pre-fix, the fixture must stop at an assertion that rekey accepted a
    configuration changed before final success publication;
  - post-fix, it must send exactly one fake raw command, retain only the
    one-shot request receipt, emit no success witness, and reject rollback
    until the generated baseline is restored;
  - run fixture, static contracts, trace/evidence self-tests, and the pinned
    isolated Tahoe build-only gate without live AP/candidate/guest action.

## IMPLEMENTATION AND LOCAL VERIFICATION: `LAB-PMF-AP-REKEY-FINAL-CONFIG-ATTESTATION-20260721`

- implementation:
  - after post-ack network equality and before final required PID/AP/watchdog
    success checks, `do_rekey()` now requires `config_pair_matches_state()`;
  - a changed pair after the raw command retains the already-consumed
    `rekey.requested` receipt but withholds `rekey.status` and categorical
    success.  It adds no second raw command, retry, restart, production config
    write, network mutation, kext action, guest action, or live AP operation;
  - the generated fixture mutates only its required config on rekey's second
    route probe, verifies that immediate rollback refuses the stale pair, then
    restores only the generated baseline before the normal explicit rollback.
    Static ordering and runtime protocol now require that final pair proof.
- deterministic fixture evidence:
  - before implementation, the fixture stopped at
    `group rekey accepted a configuration changed before final success
    publication`, proving a false success witness despite an unresolved staged
    rollback input;
  - after implementation, the same fake-only mutation issues exactly one raw
    request and retains `rekey.requested`, but reports
    `staged PMF configuration pair changed before rekey success publication`,
    writes no `rekey.status`, and emits no success result;
  - explicit rollback first rejects the changed generated pair without a
    rollback witness.  Restoring the generated config baseline then restores
    optional PMF and clears the marker normally.
- verification:
  - `bash scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh`: PASS;
  - `bash scripts/test_tahoe_post_plti_trace_contract.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_evidence_contract.sh
    --self-test`: PASS;
  - `bash scripts/test_tahoe_sae_quarantine_contract.sh`: PASS;
  - `bash scripts/run_tahoe_sae_quarantine_layer.sh`: PASS in the pinned,
    isolated Tahoe build directory (source identity `dirty2662fc2b10a0`).
    The kext, trace producer, Agent, and RegDiag built; all 959 undefined
    symbols resolved against BootKC; no kext was installed, loaded, published,
    or released.
- verification boundary: this closes only a host-side final rekey-config
  freshness fence.  It is not a live hostapd result, PMF-required association,
  IGTK observation, candidate activation, guest reboot, or driver-functionality
  result.
- external blocker unchanged: the optional/required saved-profile identity
  preflight remains categorically mismatched.  No live configuration was
  read, changed, or bypassed.

## ANOMALY: `LAB-PMF-AP-ROLLBACK-FINAL-CONFIG-ATTESTATION-20260721`

### Observation

`do_rollback()` validates the state-bound configuration pair before it starts
optional PMF, then checks AP shape, host-network baseline, and optional process
before it cancels watchdog/clears marker/writes `rollback_verified=true`.  It
does not recheck the staged pair after optional restart.  A pair changed while
the later network probe runs can therefore receive a verified rollback receipt
even though the restricted transaction baseline no longer authorizes the
result and a subsequent rollback would correctly reject that pair.

- scope: repository-owned PMF-required rollback helper and generated fixture
  only; no kext, firmware, Apple80211, candidate, guest, or physical-AP
  behavior claim.
- expected system behavior: a rollback witness must assert the same staged
  configuration pair recorded at transaction admission.  A changed or
  unreadable pair after optional restart must withhold the witness and retain
  marker/watchdog ownership until the generated baseline is restored and a
  later explicit rollback can complete.
- actual behavior: the only `config_pair_matches_state()` call in
  `do_rollback()` is before optional start.  After the host-network comparison
  and final optional PID/AP check, the helper directly releases ownership and
  writes the receipt.
- exact divergence point:
  `scripts/tahoe_pmf_required_ap_switchover.sh::do_rollback()` between final
  optional process/AP attestation and watchdog/marker release.
- source-local proof mechanism: fake `ip` can append only a generated
  syntactically acceptable directive to its temporary required config on the
  one rollback route probe, after optional restart but before receipt.  The
  unmodified helper reports verified success; restoring only the generated file
  permits the expected later explicit rollback.  No real AP, configuration,
  route, identity, or credential is touched.

## FIX_CANDIDATE

- anomaly_id: `LAB-PMF-AP-ROLLBACK-FINAL-CONFIG-ATTESTATION-20260721`
- status: `FIX_VERIFIED`
- proposed change:
  1. add a fresh rollback fixture that mutates only its generated required
     config on the rollback route probe; before a fix it must fail because
     rollback success was accepted;
  2. repeat `config_pair_matches_state()` after host-network/optional-process
     verification and immediately before watchdog/marker release;
  3. refuse receipt publication on a changed pair and let only a baseline
     restoration followed by explicit rollback complete cleanup;
  4. require the final configuration predicate in static ordering and protocol
     wording.
- safety and side effects: this is a read-only digest comparison and a
  fixture-only temporary config append.  It adds no AP restart beyond the
  existing rollback start, retry, production configuration write, network
  mutation, kext action, guest action, or live AP operation.
- forbidden alternatives considered and rejected:
  - treating a pre-restart config check as permanent across later probes makes
    the receipt outlive its authorization input;
  - accepting the receipt and expecting a future caller to notice mismatch
    turns a verified rollback into an ambiguous state;
  - restarting optional from changed files defeats the existing strict
    rollback configuration guard.
- deterministic verification plan:
  - pre-fix, the fixture must stop at an assertion that rollback accepted a
    configuration changed before verification;
  - post-fix, it must create no rollback receipt, retain marker/watchdog under
    the changed generated pair, and finish only after restoring that baseline;
  - run fixture, static contracts, trace/evidence self-tests, and the pinned
    isolated Tahoe build-only gate without live AP/candidate/guest action.

## IMPLEMENTATION AND LOCAL VERIFICATION: `LAB-PMF-AP-ROLLBACK-FINAL-CONFIG-ATTESTATION-20260721`

- implementation:
  - after final optional PID/AP and host-network verification, `do_rollback()`
    now repeats `config_pair_matches_state()` before it can dispose of the
    watchdog, clear its marker, or write `rollback_verified=true`;
  - a changed post-restart pair withholds the receipt and retains recovery
    ownership.  No extra restart/retry, production config write, network
    mutation, kext action, guest action, or live AP operation was introduced;
  - the generated fixture changes only its temporary required config on the
    rollback route probe, proves that the first receipt is withheld, then
    restores that generated baseline and completes ordinary explicit rollback.
    Static ordering and runtime protocol now require the final pair proof.
- deterministic fixture evidence:
  - before implementation, the fixture stopped at
    `rollback accepted a configuration changed before verification`, proving
    that the old helper wrote a verified receipt from stale rollback input;
  - after implementation, the same fake-only mutation reports
    `staged PMF configuration pair changed before rollback verification`,
    writes no `rollback.status`, restores a live generated optional process,
    and retains marker/watchdog ownership;
  - the next rollback correctly rejects the changed pair; restoring only the
    generated config baseline then yields `PMF_AP_ROLLBACK=OPTIONAL_RESTORED`
    and the normal `rollback_verified=true` completion witness.
- verification:
  - `bash scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh`: PASS;
  - `bash scripts/test_tahoe_post_plti_trace_contract.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_evidence_contract.sh
    --self-test`: PASS;
  - `bash scripts/test_tahoe_sae_quarantine_contract.sh`: PASS;
  - `bash scripts/run_tahoe_sae_quarantine_layer.sh`: PASS in the pinned,
    isolated Tahoe build directory (source identity `dirtyaf2683b317ca`).
    The kext, trace producer, Agent, and RegDiag built; all 959 undefined
    symbols resolved against BootKC; no kext was installed, loaded, published,
    or released.
- verification boundary: this closes only a host-side rollback-input freshness
  fence.  It is not a live hostapd result, PMF-required association, IGTK
  observation, candidate activation, guest reboot, or driver-functionality
  result.
- external blocker unchanged: the optional/required saved-profile identity
  preflight remains categorically mismatched.  No live configuration was
  read, changed, or bypassed.

## ANOMALY: `LAB-PMF-AP-ROLLBACK-FINAL-NETWORK-ATTESTATION-20260721`

### Observation

`do_rollback()` compares its host route/address/forwarding baseline after
optional restart, then performs final optional PID/AP and configuration-pair
checks before it releases watchdog/marker ownership and writes the rollback
receipt.  The AP-shape probe is itself a later operation.  If the host-network
invariant changes during that final AP probe, the old network comparison is
stale and the helper can publish `rollback_verified=true` despite the recorded
baseline no longer holding.

- scope: repository-owned rollback helper and generated fixture only; no kext,
  firmware, Apple80211, candidate, guest, or physical-AP behavior claim.
- expected system behavior: verified rollback requires the saved hash-only
  host-network invariant at the actual ownership-release/receipt edge, after
  final optional process/AP and staged-configuration checks.  A later drift is
  inconclusive and must retain marker/watchdog ownership until a stable
  explicit rollback can complete.
- actual behavior: `after_signature="$(host_network_signature)"` precedes
  `optional_hostapd_exact_and_pinned()` and final configuration comparison;
  there is no second signature comparison before `cancel_watchdog()` and
  `rollback_verified=true`.
- exact divergence point:
  `scripts/tahoe_pmf_required_ap_switchover.sh::do_rollback()` between final
  optional/configuration attestation and ownership/receipt release.
- source-local proof mechanism: the generated fake `iw` counts AP-shape calls
  and can change only its fake-network state at a selected call.  A rollback
  uses one AP-shape check during optional start, one before the existing network
  read, and one in final optional attestation; changing fake network state at
  that third call leaves all AP output valid but makes the existing signature
  stale.  The unmodified helper accepts verified rollback without real AP,
  route, profile, identity, or credential action.

## FIX_CANDIDATE

- anomaly_id: `LAB-PMF-AP-ROLLBACK-FINAL-NETWORK-ATTESTATION-20260721`
- status: `FIX_VERIFIED`
- proposed change:
  1. add a fresh generated rollback fixture that changes only fake network
     state at its final optional AP-shape probe; before a fix it must fail
     because rollback success was accepted;
  2. repeat the existing host-network signature comparison after final
     optional/configuration verification and immediately before ownership
     release/receipt write;
  3. withhold the receipt and retain marker/watchdog on drift, then permit a
     later explicit rollback after fake baseline restoration;
  4. require final network ordering in the static contract and runtime
     protocol.
- safety and side effects: the fix adds a read-only signature sample and a
  fixture-only fake-state write.  It introduces no network mutation in the
  helper, retry, AP restart, config rewrite, kext action, guest action, or live
  AP operation.
- forbidden alternatives considered and rejected:
  - treating the earlier network comparison as permanent across final AP
    attestation can publish false verified recovery;
  - clearing ownership despite drift removes the only bounded recovery path;
  - attempting to restore host network would violate the helper's no-network-
    mutation contract.
- deterministic verification plan:
  - pre-fix, the fixture must stop at an assertion that rollback accepted a
    host-network drift before verification;
  - post-fix, it must write no rollback receipt, retain marker/watchdog with a
    live generated optional process, and complete only after fake baseline
    restoration and explicit rollback;
  - run fixture, static contracts, trace/evidence self-tests, and the pinned
    isolated Tahoe build-only gate without live AP/candidate/guest action.

## IMPLEMENTATION AND LOCAL VERIFICATION: `LAB-PMF-AP-ROLLBACK-FINAL-NETWORK-ATTESTATION-20260721`

- implementation:
  - after final optional PID/AP and configuration-pair checks, `do_rollback()`
    now re-samples `host_network_signature()` and compares it to the saved
    baseline before watchdog disposition, marker release, or receipt write;
  - unreadable or changed final network state withholds `rollback.status` and
    retains recovery ownership.  The helper gained no route/address/NAT/
    forwarding mutation, retry, AP restart, config write, kext action, guest
    action, or live AP operation;
  - the generated fake `iw` can now change only fake network state at a
    selected AP-shape call.  Its third rollback call proves the late drift;
    static ordering and the runtime protocol require the second signature
    comparison at the receipt edge.
- deterministic fixture evidence:
  - before implementation, the fixture stopped at
    `rollback accepted a host-network drift before verification`, proving a
    false verified receipt after final AP attestation altered the baseline;
  - after implementation, the same fake-only drift reports
    `host network invariants changed before rollback verification`, writes no
    receipt, keeps the generated optional process live, and retains its
    marker/watchdog;
  - restoring only generated fake network output allows the following explicit
    rollback to report `PMF_AP_ROLLBACK=OPTIONAL_RESTORED` and commit its normal
    completion witness.
- verification:
  - `bash scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh`: PASS;
  - `bash scripts/test_tahoe_post_plti_trace_contract.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_evidence_contract.sh
    --self-test`: PASS;
  - `bash scripts/test_tahoe_sae_quarantine_contract.sh`: PASS;
  - `bash scripts/run_tahoe_sae_quarantine_layer.sh`: PASS in the pinned,
    isolated Tahoe build directory (source identity `dirtyd7aa7a701499`).
    The kext, trace producer, Agent, and RegDiag built; all 959 undefined
    symbols resolved against BootKC; no kext was installed, loaded, published,
    or released.
- verification boundary: this closes only a host-side final rollback-network
  freshness fence.  It is not a live hostapd result, PMF-required association,
  IGTK observation, candidate activation, guest reboot, or driver-functionality
  result.
- external blocker unchanged: the optional/required saved-profile identity
  preflight remains categorically mismatched.  No live configuration was
  read, changed, or bypassed.

## ANOMALY: `LAB-PMF-AP-ROLLBACK-POSTNETWORK-CONFIG-ATTESTATION-20260721`

### Observation

The final rollback network fence added at the receipt edge follows the final
staged-configuration comparison.  The network signature necessarily performs
another route/address/forwarding read.  A staged configuration file can change
after that earlier pair comparison but while this new final network read is in
progress.  The old helper then sees an unchanged network signature and releases
watchdog/marker ownership with `rollback_verified=true`, even though the
state-bound optional/required configuration pair no longer matches the content
it certified earlier in the same rollback.

- scope: repository-owned rollback helper and generated fixture only; no kext,
  firmware, Apple80211, candidate, guest, physical AP, live profile, or
  credential behavior claim.
- expected system behavior: the staged pair must match the state-bound hashes
  at the actual rollback receipt edge, after every later read which can overlap
  a configuration change.  A changed pair is inconclusive: it must retain the
  marker/watchdog and withhold the completion receipt until an explicit stable
  rollback can finish.
- actual behavior: `config_pair_matches_state` occurs before the final
  `host_network_signature` call and no configuration predicate separates that
  call from `cancel_watchdog`, `clear_marker`, and `rollback_verified=true`.
- exact divergence point:
  `scripts/tahoe_pmf_required_ap_switchover.sh::do_rollback()` between the
  final host-network comparison and ownership/receipt release.
- source-local proof mechanism: the generated fake `ip` counts only its route
  reads and can append a harmless generated directive to the generated
  required-PMF config at a selected call.  One rollback route read occurs
  before the existing final configuration predicate; the new final network
  fence makes a second route read after it.  Mutating only at that second read
  leaves all fake AP/network output valid, but the unmodified helper accepts a
  verified receipt.  No real AP, route, profile, identity, credential, or
  network action is used.

## FIX_CANDIDATE

- anomaly_id: `LAB-PMF-AP-ROLLBACK-POSTNETWORK-CONFIG-ATTESTATION-20260721`
- status: `FIX_VERIFIED`
- proposed change:
  1. add a generated rollback fixture that mutates only its private required
     config on the second rollback route read; before a fix it must fail because
     a changed pair was accepted at receipt publication;
  2. repeat `config_pair_matches_state` after the final host-network signature
     comparison and immediately before watchdog/marker/receipt release;
  3. retain recovery ownership and withhold `rollback.status` on this late
     mismatch, then prove an explicit cleanup after restoring the generated
     config;
  4. add static ordering and runtime-protocol coverage for the post-network
     pair attestation.
- safety and side effects: the proposed helper change is a read-only local
  staged-file comparison.  The mutation exists only in the generated fixture;
  no live config write, network mutation, retry, AP restart, kext action,
  guest action, or live AP operation is introduced.
- forbidden alternatives considered and rejected:
  - treating the pre-network pair comparison as permanently current permits a
    false verified rollback after a later file mutation;
  - clearing marker/watchdog on mismatch discards the bounded recovery path;
  - rewriting or restoring the staged file from the helper exceeds its
    validation-only scope and could overwrite an operator change.
- deterministic verification plan:
  - pre-fix, the fixture must stop at an assertion that a post-network config
    change was accepted before receipt publication;
  - post-fix, it must emit the categorical late-pair diagnostic, write no
    receipt, retain generated marker/watchdog/optional state, and succeed only
    after generated config restoration and explicit rollback;
  - run fixture, static contracts, trace/evidence self-tests, and the pinned
    isolated Tahoe build-only gate without live AP/candidate/guest action.

## IMPLEMENTATION AND LOCAL VERIFICATION: `LAB-PMF-AP-ROLLBACK-POSTNETWORK-CONFIG-ATTESTATION-20260721`

- implementation:
  - `do_rollback()` now repeats `config_pair_matches_state` after its final
    host-network baseline comparison and before it cancels the watchdog, clears
    the marker, or writes the rollback completion receipt;
  - a late file mismatch emits `staged PMF configuration pair changed before
    rollback receipt`, retains recovery ownership, and writes no receipt.  The
    helper gained no config/network mutation, retry, AP restart, kext action,
    guest action, or live AP operation;
  - the runtime protocol and static contract bind the second pair comparison to
    the post-network receipt edge.
- deterministic fixture evidence:
  - before implementation, its second rollback route read appended only to the
    generated required config and stopped at `rollback accepted a configuration
    changed after final network verification`, proving that the old source
    accepted the stale pair;
  - after implementation, the same generated-only mutation produces the
    categorical late-pair diagnostic, preserves the generated optional process,
    marker, and watchdog, and leaves `rollback.status` absent;
  - after restoring only the generated required config, an explicit rollback
    emits `PMF_AP_ROLLBACK=OPTIONAL_RESTORED`, writes
    `rollback_verified=true`, clears the marker, and cancels that watchdog.
- verification:
  - `bash scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh`: PASS;
  - `bash scripts/test_tahoe_post_plti_trace_contract.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_evidence_contract.sh
    --self-test`: PASS;
  - `bash scripts/test_tahoe_sae_quarantine_contract.sh`: PASS;
  - `bash scripts/run_tahoe_sae_quarantine_layer.sh`: PASS in the pinned,
    isolated Tahoe build directory (source identity `dirtydad6879b1d59`).
    The kext, trace producer, Agent, and RegDiag built; all 959 undefined
    symbols resolved against BootKC; no kext was installed, loaded, published,
    or released.
- verification boundary: this closes only a host-side rollback staged-file
  freshness fence.  It is not a live hostapd result, PMF-required association,
  IGTK observation, candidate activation, guest reboot, or driver-functionality
  result.
- external blocker unchanged: the optional/required saved-profile identity
  preflight remains categorically mismatched.  No live configuration was read,
  changed, or bypassed.

## ANOMALY: `LAB-PMF-AP-ROLLBACK-POSTNETWORK-OPTIONAL-ATTESTATION-20260721`

### Observation

The rollback receipt edge now repeats configuration after its final network
signature, but the last exact optional hostapd PID/AP-shape attestation still
precedes both that network read and the new final configuration comparison.  An
optional process can disappear during the final route/address/forwarding read.
Neither the later configuration predicate nor ownership release proves the
optional process is still current, so the old helper can publish
`rollback_verified=true` after it has restored no live optional-PMF process.

- scope: repository-owned rollback helper and generated fixture only; no kext,
  firmware, Apple80211, candidate, guest, physical AP, live profile, or
  credential behavior claim.
- expected system behavior: the exact optional-PMF PID and pinned AP shape
  must be current at the actual receipt edge after the final network/config
  reads.  A late disappearance is inconclusive: it must retain marker/watchdog
  ownership and withhold the completion receipt until explicit stable recovery.
- actual behavior: `optional_hostapd_exact_and_pinned` is before the final
  `host_network_signature`; `config_pair_matches_state` after that read does
  not prove optional process liveness, and `cancel_watchdog`, `clear_marker`,
  and `rollback_verified=true` follow without another optional predicate.
- exact divergence point:
  `scripts/tahoe_pmf_required_ap_switchover.sh::do_rollback()` between the
  final host-network/configuration fences and ownership/receipt release.
- source-local proof mechanism: the generated fake `ip` already counts route
  reads.  A fixture-only hook can terminate only its generated optional child
  and remove only its generated PID receipt on the second rollback route read,
  which is the final network signature after the existing optional check.  All
  fake AP/network/config output remains valid, while the unmodified helper
  accepts a receipt without optional ownership.  No real AP, route, profile,
  identity, credential, or network action is used.

## FIX_CANDIDATE

- anomaly_id: `LAB-PMF-AP-ROLLBACK-POSTNETWORK-OPTIONAL-ATTESTATION-20260721`
- status: `FIX_VERIFIED`
- proposed change:
  1. add the fixture-only selected-route optional-child termination hook and a
     fresh rollback case targeting its second network-signature route read;
     before a fix it must fail because receipt publication accepted no optional
     process;
  2. repeat `optional_hostapd_exact_and_pinned` after final network and
     configuration verification, immediately before watchdog/marker/receipt
     release;
  3. retain recovery ownership and withhold `rollback.status` on late optional
     loss, then prove explicit cleanup after generated optional restoration;
  4. add static ordering and runtime-protocol coverage for the final optional
     attestation.
- safety and side effects: the helper correction is read-only PID/AP
  observation; process termination is confined to an ephemeral fixture child.
  It introduces no live process action, config/network mutation, retry, AP
  restart, kext action, guest action, or live AP operation.
- forbidden alternatives considered and rejected:
  - treating the preceding optional predicate as current across network/file
    reads permits a false verified rollback without an AP owner;
  - clearing marker/watchdog after optional loss discards the bounded recovery
    path;
  - restarting optional automatically would add a second AP transition and
    could mask a process identity failure rather than report it.
- deterministic verification plan:
  - pre-fix, the fixture must stop at an assertion that late optional loss was
    accepted before receipt publication;
  - post-fix, it must produce the categorical optional-process diagnostic,
    write no receipt, retain marker/watchdog, and succeed only after generated
    optional restoration and explicit rollback;
  - run fixture, static contracts, trace/evidence self-tests, and the pinned
    isolated Tahoe build-only gate without live AP/candidate/guest action.

## IMPLEMENTATION AND LOCAL VERIFICATION: `LAB-PMF-AP-ROLLBACK-POSTNETWORK-OPTIONAL-ATTESTATION-20260721`

- implementation:
  - `do_rollback()` now repeats `optional_hostapd_exact_and_pinned` after its
    final network and staged-pair checks, immediately before it can cancel the
    watchdog, clear the marker, or write the rollback receipt;
  - a missing, replaced, or unpinned optional process emits
    `optional-PMF hostapd process or AP shape is not exact before rollback
    receipt`, preserves recovery ownership, and writes no receipt.  The helper
    gained no process/config/network mutation, retry, AP restart, kext action,
    guest action, or live AP operation;
  - the generated fake `ip` gained a route-call-scoped optional-child kill hook
    confined to its ephemeral child/PID receipt; static ordering and the
    runtime protocol bind the final optional attestation to the receipt edge.
- deterministic fixture evidence:
  - before implementation, terminating only that generated optional child at
    the second rollback route read stopped at `rollback accepted an optional
    hostapd lost after final network verification`, proving a false receipt;
  - after implementation, the same generated-only loss emits the categorical
    diagnostic, leaves `rollback.status` absent, and retains marker/watchdog
    ownership while the optional PID receipt remains absent;
  - the following explicit rollback starts a fresh generated optional process,
    emits `PMF_AP_ROLLBACK=OPTIONAL_RESTORED`, writes
    `rollback_verified=true`, clears the marker, and cancels the watchdog.
- verification:
  - `bash scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh`: PASS;
  - `bash scripts/test_tahoe_post_plti_trace_contract.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_evidence_contract.sh
    --self-test`: PASS;
  - `bash scripts/test_tahoe_sae_quarantine_contract.sh`: PASS;
  - `bash scripts/run_tahoe_sae_quarantine_layer.sh`: PASS in the pinned,
    isolated Tahoe build directory (source identity `dirtyc463b04b0103`).
    The kext, trace producer, Agent, and RegDiag built; all 959 undefined
    symbols resolved against BootKC; no kext was installed, loaded, published,
    or released.
- verification boundary: this closes only a host-side rollback optional-process
  freshness fence.  It is not a live hostapd result, PMF-required association,
  IGTK observation, candidate activation, guest reboot, or driver-functionality
  result.
- external blocker unchanged: the optional/required saved-profile identity
  preflight remains categorically mismatched.  No live configuration was read,
  changed, or bypassed.

## ANOMALY: `LAB-PMF-AP-ROLLBACK-RECEIPT-TARGET-ADMISSION-20260721`

### Observation

`do_rollback()` creates `rollback.status` only after it has stopped required
PMF, restored/attested optional PMF, cancelled the watchdog, and cleared the
active marker.  Although the parent state directory is restricted at admission,
the helper never verifies that this mandatory completion-witness pathname is
fresh and non-symlinked before it begins those lifecycle mutations.  A
pre-existing generated directory at `rollback.status` makes the final shell
redirection fail after ownership has already been released.  The caller gets a
failure and no completion receipt, but neither marker nor watchdog remains to
authorize a normal retry.

- scope: repository-owned restricted state and generated fixture only; no kext,
  firmware, Apple80211, candidate, guest, physical AP, live profile, or
  credential behavior claim.
- expected system behavior: a rollback must reject a non-fresh completion
  receipt target before it stops required hostapd or releases any recovery
  ownership.  The resulting categorical failure must retain the required
  transaction, marker, and watchdog; removing only the private obstruction must
  permit one later ordinary rollback.
- actual behavior: no predicate checks `$STATE_DIR/rollback.status` until the
  final `printf ... >"$STATE_DIR/rollback.status"`, which is after
  `cancel_watchdog()` and `clear_marker()`.
- exact divergence point:
  `scripts/tahoe_pmf_required_ap_switchover.sh::do_rollback()` between state
  admission and the first required-hostapd stop, relative to the later receipt
  redirection.
- source-local proof mechanism: a fixture makes only a directory named
  `rollback.status` in its own mode-0700 generated state directory after a
  generated required activation.  The unmodified helper performs its full
  generated rollback, then cannot redirect the receipt and has already dropped
  marker/watchdog ownership.  No real AP, route, profile, identity, credential,
  or network action is used.

## FIX_CANDIDATE

- anomaly_id: `LAB-PMF-AP-ROLLBACK-RECEIPT-TARGET-ADMISSION-20260721`
- status: `FIX_VERIFIED`
- proposed change:
  1. add a read-only `rollback_receipt_is_fresh` predicate requiring an absent,
     non-symlinked `$STATE_DIR/rollback.status` at rollback admission;
  2. reject a blocked target before required hostapd can be stopped, retaining
     marker/watchdog ownership and emitting a categorical diagnostic;
  3. add a generated directory-obstruction fixture that fails on the old late
     redirection, then removes only its private obstruction and completes one
     normal rollback after the correction;
  4. bind the receipt-target admission to the static runtime contract and
     protocol.
- safety and side effects: the helper change is a read-only pathname predicate.
  The obstructing directory exists only in the fixture's restricted temporary
  state.  It adds no live process/config/network mutation, retry, AP restart,
  kext action, guest action, or live AP operation.
- forbidden alternatives considered and rejected:
  - allow the receipt redirection to be the first target validation after
    ownership release;
  - overwrite an existing receipt/path or follow a symlink;
  - infer success from restored AP shape after the no-receipt failure;
  - restart AP/watchdog or alter host networking to conceal the broken
    transaction.
- deterministic verification plan:
  - pre-fix, the fixture must stop at an assertion that a blocked receipt target
    released ownership before receipt publication;
  - post-fix, it must report the categorical target-admission diagnostic,
    preserve required hostapd plus marker/watchdog, and succeed only after the
    generated obstruction is removed and explicit rollback is invoked;
  - run fixture, static contracts, trace/evidence self-tests, and the pinned
    isolated Tahoe build-only gate without live AP/candidate/guest action.

## IMPLEMENTATION AND LOCAL VERIFICATION: `LAB-PMF-AP-ROLLBACK-RECEIPT-TARGET-ADMISSION-20260721`

- implementation:
  - added `rollback_receipt_is_fresh()`, a read-only absent/non-symlinked
    predicate for the transaction's `rollback.status` target;
  - `do_rollback()` now applies it immediately after restricted state-directory
    admission and before it can stop required hostapd.  A blocked target emits
    `rollback completion receipt target is not fresh`, while marker/watchdog
    and required PMF remain current;
  - the protocol and static contract make completion-witness target admission a
    first rollback step, rather than treating final shell redirection as a
    best-effort post-release action.  No AP restart, retry, config/network
    mutation, kext action, guest action, or live AP operation was added.
- deterministic fixture evidence:
  - before implementation, a directory at only the generated
    `rollback.status` path caused the final redirection to fail after ownership
    release; the fixture stopped at `rollback receipt obstruction released
    ownership before receipt publication`;
  - after implementation, that same private directory produces the categorical
    admission diagnostic before required hostapd is stopped, retaining the live
    required process, active marker, and watchdog with no optional restart;
  - removing only the generated empty directory permits the following explicit
    rollback to report `PMF_AP_ROLLBACK=OPTIONAL_RESTORED`, write
    `rollback_verified=true`, clear the marker, and cancel the watchdog.
- verification:
  - `bash scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh`: PASS;
  - `bash scripts/test_tahoe_post_plti_trace_contract.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_evidence_contract.sh
    --self-test`: PASS;
  - `bash scripts/test_tahoe_sae_quarantine_contract.sh`: PASS;
  - `bash scripts/run_tahoe_sae_quarantine_layer.sh`: PASS in the pinned,
    isolated Tahoe build directory (source identity `dirtyda57a0518d58`).
    The kext, trace producer, Agent, and RegDiag built; all 959 undefined
    symbols resolved against BootKC; no kext was installed, loaded, published,
    or released.
- verification boundary: this closes only a host-side rollback receipt-target
  admission gap.  It is not a live hostapd result, PMF-required association,
  IGTK observation, candidate activation, guest reboot, or driver-functionality
  result.
- external blocker unchanged: the optional/required saved-profile identity
  preflight remains categorically mismatched.  No live configuration was read,
  changed, or bypassed.

## ANOMALY: `LAB-PMF-AP-POSTTRANSITION-ROLLBACK-CONFIG-ATTESTATION-20260721`

### Observation

`finish_post_transition_rollback()` is the automatic recovery path used after
optional hostapd has been stopped and required hostapd cannot be started or
promoted.  It restores/attests optional PMF, reads the stored host-network
baseline, then re-attests optional PID/AP before it cancels the watchdog and
clears the marker.  Unlike explicit rollback, it has no staged-pair comparison
after that network read.  A required configuration file can change during the
final recovery network read; the old source still returns success, so
`do_activate()` reports `optional rollback verified` and releases ownership
even though the state-bound optional/required pair no longer matches the
transaction baseline.

- scope: repository-owned automatic recovery helper and generated fixture only;
  no kext, firmware, Apple80211, candidate, guest, physical AP, live profile,
  or credential behavior claim.
- expected system behavior: automatic post-transition recovery can describe
  optional rollback as verified only if the state-bound staged pair remains
  exact through its final network/process observations.  A late pair drift
  must retain marker/watchdog and report the recovery as armed/inconclusive
  until an explicit rollback after generated baseline restoration.
- actual behavior: `config_pair_matches_state` is consulted only inside
  `restore_optional_after_activation_failure()` before optional restart.  The
  later `host_network_signature` can overlap a file change, and no pair
  predicate follows it before `cancel_watchdog()` and `clear_marker()`.
- exact divergence point:
  `scripts/tahoe_pmf_required_ap_switchover.sh::finish_post_transition_rollback()`
  between final host-network equality and final optional/ownership release.
- source-local proof mechanism: the generated fake `ip` counts route reads and
  can append a harmless directive only to the generated required config.  On a
  generated required-start failure, its third activation route read is the
  post-transition recovery network signature, after optional restart's first
  pair check.  The unmodified helper reports verified optional rollback and
  drops ownership.  No real AP, route, profile, identity, credential, or
  network action is used.

## FIX_CANDIDATE

- anomaly_id: `LAB-PMF-AP-POSTTRANSITION-ROLLBACK-CONFIG-ATTESTATION-20260721`
- status: `FIX_VERIFIED`
- proposed change:
  1. add a generated required-start-failure fixture that mutates only its
     private required config at `finish_post_transition_rollback`'s final
     network route read; before a fix it must prove false optional rollback
     verification;
  2. repeat `config_pair_matches_state` after the recovery network baseline
     comparison and before final optional/ownership release;
  3. retain marker/watchdog and withhold the verified-recovery branch on late
     drift, then permit an explicit normal rollback only after restoring the
     generated config;
  4. add static ordering and runtime-protocol coverage for this automatic
     recovery pair attestation.
- safety and side effects: the helper correction is a read-only staged-file
  comparison.  Its file mutation is generated-fixture-only; no live config
  write, network mutation, retry, AP restart, kext action, guest action, or
  live AP operation is introduced.
- forbidden alternatives considered and rejected:
  - trust the earlier restart-time pair check across the later network read;
  - describe an inconclusive recovery as verified merely because optional PID/
    AP shape remained valid;
  - clear marker/watchdog after the drift or rewrite the operator's config;
  - repeat/start AP processes or alter the host network to conceal the drift.
- deterministic verification plan:
  - pre-fix, the fixture must stop at an assertion that post-transition
    recovery accepted a configuration drift after its final network check;
  - post-fix, it must retain generated optional state plus marker/watchdog,
    report the armed-recovery branch, and succeed only after generated config
    restoration and explicit rollback;
  - run fixture, static contracts, trace/evidence self-tests, and the pinned
    isolated Tahoe build-only gate without live AP/candidate/guest action.

## IMPLEMENTATION AND LOCAL VERIFICATION: `LAB-PMF-AP-POSTTRANSITION-ROLLBACK-CONFIG-ATTESTATION-20260721`

- implementation:
  - `finish_post_transition_rollback()` now repeats
    `config_pair_matches_state` after it verifies the stored host-network
    baseline and before final optional PID/AP attestation or ownership release;
  - a late pair mismatch returns an inconclusive automatic recovery, so the
    caller emits the existing `rollback watchdog remains armed` branch instead
    of `optional rollback verified`.  The correction adds no config/network
    mutation, retry, AP restart, kext action, guest action, or live AP
    operation;
  - static ordering and the runtime protocol bind automatic post-transition
    recovery to the same state-bound configuration freshness rule as explicit
    rollback.
- deterministic fixture evidence:
  - before implementation, a generated required-start failure combined with a
    generated required-config append at the third activation route read stopped
    at `post-transition recovery accepted configuration drift after final
    network verification`; the old source falsely reported verified optional
    rollback and released its owner;
  - after implementation, the same generated-only drift keeps optional hostapd
    live but retains the marker/watchdog and emits the armed-recovery branch;
  - restoring only the generated required config permits the following explicit
    rollback to emit `PMF_AP_ROLLBACK=OPTIONAL_RESTORED` and cleanly release
    both owner receipts.
- verification:
  - `bash scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh`: PASS;
  - `bash scripts/test_tahoe_post_plti_trace_contract.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_evidence_contract.sh
    --self-test`: PASS;
  - `bash scripts/test_tahoe_sae_quarantine_contract.sh`: PASS;
  - `bash scripts/run_tahoe_sae_quarantine_layer.sh`: PASS in the pinned,
    isolated Tahoe build directory (source identity `dirtye59334a39c10`).
    The kext, trace producer, Agent, and RegDiag built; all 959 undefined
    symbols resolved against BootKC; no kext was installed, loaded, published,
    or released.
- verification boundary: this closes only an automatic host-side recovery
  configuration-freshness fence.  It is not a live hostapd result,
  PMF-required association, IGTK observation, candidate activation, guest
  reboot, or driver-functionality result.
- external blocker unchanged: the optional/required saved-profile identity
  preflight remains categorically mismatched.  No live configuration was read,
  changed, or bypassed.

## ANOMALY: `LAB-PMF-AP-WATCHDOG-PRETRANSITION-ATTESTATION-20260721`

### Observation

`start_watchdog()` validates the ready watchdog process before it writes the
restricted PID receipt, but `do_activate()` never rechecks that independent
recovery owner after its later host-network/configuration fences and before it
stops optional hostapd.  A watchdog that exits in that interval leaves an
otherwise valid marker/state transaction with no autonomous restoration owner;
the current source can still transition to required PMF and publish
`REQUIRED_ACTIVE`.

This is deterministically reproducible only in the local fixture.  Its fake
route command counts calls; on activation's second route read, after watchdog
readiness and before optional stop, it kills the generated watchdog and removes
only the generated PID receipt while preserving normal route output.  The
unmodified helper then accepts required activation.  No real AP, network,
guest, profile, credential, route, or address is read or changed.

### Root Cause

Watchdog liveness is proved at setup time but not at the actual first AP
mutation edge.  The pre-stop network/configuration admission predicates do not
imply that the separate session-bound watchdog remains alive.

### FIX_CANDIDATE: `LAB-PMF-AP-WATCHDOG-PRETRANSITION-ATTESTATION-20260721`

- status: `FIX_VERIFIED`; the fixture demonstrated current required-active
  publication without an owner and now rejects it before optional stop.
- system contract to preserve:
  1. optional hostapd is never stopped unless the exact marker/state-bound
     watchdog process is current at the final pre-transition edge;
  2. a dead/missing/replaced watchdog after setup is a categorical pre-stop
     failure that retains optional PMF and performs only the lighter rollback
     cleanup;
  3. the separate watchdog remains an independent owner; no caller-local
     sleep, retry, restart, or ownership substitution is introduced;
  4. all host-network/configuration and process checks retain their existing
     ordering and semantics.
- proposed minimal correction:
  - add one read-only `watchdog_owner_is_current()` predicate using the
    existing restricted PID receipt and exact watchdog argv/state matcher;
  - recheck it after `write_watchdog_pid()` in setup and after the final
    network/configuration pre-stop fences immediately before optional stop;
  - extend fixture/static ordering/protocol coverage for the generated
    second-route watchdog death.
- scope and touchpoints:
  - `scripts/tahoe_pmf_required_ap_switchover.sh::start_watchdog`,
    `watchdog_owner_is_current`, and `do_activate`;
  - `scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh`;
  - `scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh`;
  - `docs/TAHOE_IWX_PMF_BIP_RUNTIME_PROTOCOL.md`;
  - this analysis record.
- rejected alternatives:
  - rely on the earlier readiness acknowledgement indefinitely;
  - transition first and let the caller discover missing rollback ownership;
  - replace the watchdog with a retry/timer/local process, or restart hostapd;
  - make any live AP, guest, profile, route/address, firewall, or DHCP change.
- verification plan:
  1. demonstrate that the current generated second-route death publishes
     required-active despite a missing watchdog receipt;
  2. require the correction to report an exact watchdog pre-stop failure,
     retain the original live optional hostapd, start no required hostapd, and
     clear the temporary marker through the pre-stop cleanup path;
  3. run AP/runtime/trace/SAE contracts and the isolated Tahoe build-only gate.
- verification boundary: this is a host-side lifecycle ownership fence only;
  it is not evidence of a live PMF association, IGTK behavior, or driver
  functionality.

## ANOMALY: `LAB-PMF-AP-EXACT-ONE-RAW-REKEY-REQUEST-20260721`

### Observation

The runtime protocol permits exactly one post-initial `REKEY_GTK` stimulus,
and the trace evaluator now rejects more than one observed cross-slot
transition.  The AP helper itself does not bind that cardinality to its
marker-bound transaction: `do_rekey()` writes its success-only `rekey.status`
after post-command checks but has no pre-command request receipt or admission
check.  A second direct `--rekey` against the same required state therefore
issues another raw command.  More subtly, a raw command that receives `OK` but
then fails a process/AP/network postcondition leaves no success status, so a
retry can issue a second real command even though the first command already
reached hostapd.

The local fixture can demonstrate this safely.  Its fake CLI logs commands;
after a generated post-command network drift, restoring only the local fake
network state lets the current helper issue a second raw command and write
success.  A separately stable generated transaction also accepts a second
success command.  Neither case reads or changes a real AP, guest, saved
profile, route/address, or credential.

### Root Cause

The code treats `rekey.status` as both a success witness and an implicit
one-shot guard, but commits it too late to prove whether the raw side effect
has occurred.  The protocol's exact-one semantic therefore lives only in the
consumer/evaluator, not in the producer that can create the side effect.

### FIX_CANDIDATE: `LAB-PMF-AP-EXACT-ONE-RAW-REKEY-REQUEST-20260721`

- status: `FIX_VERIFIED`; the fixture demonstrated the second raw request on
  the old ordering and now proves both retry and duplicate rejection.
- system contract to preserve:
  1. every marker-bound required-PMF transaction may issue at most one raw
     `REKEY_GTK` command, whether its eventual outcome is success or
     inconclusive;
  2. a read-only/pre-command rejection does not consume the sole request;
  3. a durable request receipt is committed only after all pre-command
     predicates including the exact required-process fence and immediately
     before the one raw command;
  4. `rekey.status` remains a success-only witness and a request receipt alone
     never promotes PMF/BIP evidence;
  5. a post-command failure remains inconclusive, leaves rollback ownership
     intact, and forbids a second stimulus rather than retrying it.
- proposed minimal correction:
  - add a restricted `rekey.requested` receipt helper that rejects an existing
    file or symlink, writes `rekey_attempted=true` mode `0600`, and is invoked
    after the final pre-command process attestation and before `REKEY_GTK`;
  - reject `--rekey` when that receipt already exists before any AP/control
    operation;
  - retain the existing `rekey.status` only after all command postconditions;
  - extend the local fake-CLI fixture for both post-command-failure retry and
    stable duplicate-command rejection; extend static ordering and protocol
    wording.
- scope and touchpoints:
  - `scripts/tahoe_pmf_required_ap_switchover.sh::do_rekey` plus a small
    restricted-state receipt helper;
  - `scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh`;
  - `scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh`;
  - `docs/TAHOE_IWX_PMF_BIP_RUNTIME_PROTOCOL.md`;
  - this analysis record.
- rejected alternatives:
  - rely solely on the trace evaluator to detect an extra AP command;
  - write the existing success witness before hostapd acknowledgement;
  - retry after a failed acknowledgement/postcondition, add timers, or
    restart/reload hostapd;
  - make a live AP, guest, profile, address/route, firewall, or DHCP change.
- verification plan:
  1. make the current fixture stop when it accepts a retry after an
     acknowledged-but-inconclusive command;
  2. add the pre-command request receipt and require no second fake CLI line,
     no success witness for the failed attempt, and normal rollback;
  3. require a separately successful transaction to reject a duplicate
     command while retaining its original success witness;
  4. run AP/runtime/trace/SAE contracts and the isolated Tahoe build-only
     gate without a candidate activation or live AP operation.
- verification boundary: this is a repository-owned raw-command cardinality
  fence only.  It cannot establish a real hostapd result, PMF-required
  association, IGTK transition, or driver functionality.

## ANOMALY: `LAB-PMF-AP-ROLLBACK-WITNESS-COMMIT-ORDER-20260721`

### Observation

`do_rollback()` currently writes the categorical
`rollback_verified=true` receipt before it attempts to cancel the watchdog or
clear the active marker.  Either trailing operation can fail.  The runtime
runner's cleanup deliberately treats the presence of that receipt as a
watchdog-written success and skips another rollback attempt, so an interrupted
or failed teardown can be promoted to verified recovery merely because the AP
shape/network predicates were earlier true.

This is directly reproducible without a real AP: after a generated required
transaction, the fixture replaces only its private `watchdog.pid` receipt with
the PID of a generated unrelated child.  The existing `cancel_watchdog()`
correctly rejects that process identity, but the current source has already
written `rollback.status` when it emits its categorical cancellation failure.
The original generated watchdog and marker remain present; no live hostapd,
network, guest, profile, or physical AP is touched.

### Root Cause

The rollback witness is committed before the transaction's ownership-release
postconditions.  It is therefore an intermediate AP-restoration observation,
not proof that the marker-bound recovery transaction completed.  The runner's
intentional receipt-first cleanup behavior makes that ordering system-visible.

### FIX_CANDIDATE: `LAB-PMF-AP-ROLLBACK-WITNESS-COMMIT-ORDER-20260721`

- status: `FIX_VERIFIED`; the local fixture demonstrated the false receipt on
  the old ordering and now requires its absence until teardown completes.
- system contract to preserve:
  1. `rollback_verified=true` exists only after optional process/AP/network
     predicates, watchdog disposition, and marker release have all succeeded;
  2. a failed watchdog identity or marker release leaves no success receipt
     and preserves the recovery authority for a later safe retry;
  3. the watchdog-owned rollback keeps its existing `FROM_WATCHDOG` behavior:
     it clears the marker, writes the receipt, and then exits;
  4. no rollback retry, AP restart, network change, or live action is added.
- proposed minimal correction:
  - retain all current rollback admission/restore/attestation checks;
  - move the `rollback.status` write and its permission mode after successful
    `cancel_watchdog` (when applicable) and successful `clear_marker`;
  - make the static contract require that commit order and add fixture cases
    for a rejected watchdog receipt followed by a normal local recovery.
- scope and touchpoints:
  - `scripts/tahoe_pmf_required_ap_switchover.sh::do_rollback`;
  - `scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh` with a
    generated unrelated PID written only to its own restricted state file;
  - `scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh` ordering assertion;
  - `docs/TAHOE_IWX_PMF_BIP_RUNTIME_PROTOCOL.md` witness semantics;
  - this analysis report.
- rejected alternatives:
  - trust the receipt despite a failed owner release;
  - make the runner infer success from optional AP shape alone;
  - suppress watchdog identity checks, kill an unrelated process, or add a
    restart/retry loop;
  - touch a real AP, saved profile, guest, address/route, firewall, or DHCP
    state.
- verification plan:
  1. show the unmodified ordering leaves `rollback.status` after a generated
     foreign watchdog receipt makes cancellation fail;
  2. after reordering, require that failure to retain marker/original watchdog
     and write no receipt, then restore the original fixture receipt and
     complete one normal rollback;
  3. run the AP fixture, runtime/trace/SAE contracts, and the isolated
     build-only Tahoe gate.  Do not activate a candidate or operate live AP
     infrastructure.
- verification boundary: this is a host-side transaction-receipt ordering fix,
  not evidence of PMF-required association, IGTK behavior, or driver
  functionality.

## ANOMALY: `LAB-PMF-AP-ROLLBACK-OPTIONAL-PROCESS-ATTESTATION-20260721`

### Observation

`do_rollback()` can publish `rollback_verified=true`, cancel the independent
rollback watchdog, and clear the marker after an optional-PMF process has
already disappeared.  Its post-start evidence is currently only
`runtime_ap_is_pinned()` and the host-network signature.  Both can remain true
after the child validated by `wait_hostapd_active()` exits: AP shape is a radio
observation, and the network signature intentionally excludes the hostapd
process identity.

The same stale-observation boundary appears in the two automatic recovery
paths.  `restore_optional_after_activation_failure()` calls
`start_configured_hostapd()` (whose successful wait is an earlier observation)
and ends with only `runtime_ap_is_pinned()`.  `finish_armed_rollback()` then
releases marker/watchdog ownership immediately; `finish_post_transition_rollback()`
first reads the network signature, then releases ownership, without a final
optional-PMF exact-process observation.

This is source-local and deterministic: the fixture's fake `iw` can kill only
the generated optional child after the start helper's PID check while continuing
to emit the pinned AP shape.  On the current source an explicit rollback then
reaches `rollback_verified=true`; a failed required start can likewise report
`optional rollback verified`.  No real AP, configuration, interface, route,
or guest is involved.

### Root Cause

The rollback success witness is bound to a historical optional-hostapd start
observation instead of an exact optional process at the final ownership-release
edge.  `runtime_ap_is_pinned()` is correctly a separate topology predicate; it
is not and must not become a proxy for PID/configuration ownership.

### FIX_CANDIDATE: `LAB-PMF-AP-ROLLBACK-OPTIONAL-PROCESS-ATTESTATION-20260721`

- status: `FIX_VERIFIED`; the local fixture demonstrated the false success
  before the correction and now rejects it while retaining recovery ownership.
- system contract to preserve:
  1. a rollback witness and cancellation/clear of the marker-bound watchdog
     mean the exact optional-PMF hostapd process for the admitted config is
     still live and the AP remains pinned at that final edge;
  2. a process loss during recovery withholds the witness and retains the
     marker/watchdog owner for a later explicit rollback;
  3. host-network signature equality remains an independent required recovery
     predicate, not a substitute for process identity;
  4. recovery must not retry, restart, reload, or otherwise add new AP
     lifecycle side effects after a failed final attestation.
- proposed minimal correction:
  - introduce one read-only helper that requires both
    `configured_hostapd_active "$OPTIONAL_CONFIG" "$OPTIONAL_PID"` and
    `runtime_ap_is_pinned`;
  - use it as the terminal predicate of
    `restore_optional_after_activation_failure()`;
  - reapply it after the post-transition network-signature comparison and
    before marker/watchdog release;
  - use it after the explicit rollback network comparison and before
    `rollback_verified=true`.
- scope and touchpoints:
  - `scripts/tahoe_pmf_required_ap_switchover.sh` only in optional recovery
    completion predicates;
  - `scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh` only for
    generated optional-child death injection and recovery assertions;
  - `scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh` only for static
    ordering/coverage assertions;
  - `docs/TAHOE_IWX_PMF_BIP_RUNTIME_PROTOCOL.md` only for the ownership
    boundary description;
  - this analysis report for before/after evidence.
- excluded alternatives:
  - treat a channel/width response, a host-network hash, or a PID file alone
    as proof of optional hostapd ownership;
  - add sleeps, retries, a hostapd restart, or a second AP switchover;
  - weaken the existing exact argv/config PID predicate;
  - touch a live AP, guest, saved profile, route/address, firewall, or DHCP
    state.
- expected fixture proof before the fix:
  - fake `iw` removes the generated optional PID at the start-observation edge
    while it reports the existing pinned shape;
  - current explicit rollback falsely succeeds and writes `rollback_verified`;
  - current failed-required activation falsely reports verified optional
    rollback under the same generated death condition.
- expected proof after the fix:
  - both paths fail categorically without a rollback witness and retain the
    marker/watchdog; a subsequent fixture rollback with the injection removed
    restores optional PMF normally;
  - stable rollback remains a single restoration with no added helper retry;
  - AP/runtime/trace/SAE contracts and the isolated build-only gate still pass.
- verification boundary: this candidate closes a host-side recovery ownership
  race only.  It cannot demonstrate a real hostapd result, PMF-required
  association, IGTK observation, or driver functionality.

## ANOMALY

- id: `LAB-PMF-AP-RESTRICTED-STATE-INTEGRITY-20260721`
- status: `FIX_VERIFIED`
- scope: repository-owned AP transaction state-directory admission; no kext,
  firmware, Apple80211, candidate, guest, or physical-AP claim.
- symptom: `require_state_dir()` checks only prefix, existence, non-symlink,
  and canonical spelling.  It accepts a group/other-writable state directory
  even though state, watchdog receipt, and later rollback authority are placed
  inside it.
- expected system behavior: the marker-bound state directory must be owned by
  the invoking user and mode `0700` before any state write, watchdog launch, or
  AP process transition.  A writable-by-others directory must reject activation
  while optional PMF remains untouched.
- actual behavior: a canonical `0777` directory under the accepted temporary
  prefix reaches `write_state()`, `write_marker()`, watchdog start, and the
  optional/required hostapd transition without an ownership or permission test.
- divergence point: `scripts/tahoe_pmf_required_ap_switchover.sh::require_state_dir`.
- evidence:
  - local source establishes the missing UID/mode predicates while later code
    treats state file, watchdog PID, and marker contents as rollback authority.
  - the runtime protocol calls that state restricted and marker-bound; a
    directory writable by other local principals cannot satisfy that ownership
    contract.
  - deterministic no-AP runtime reproduction: the fixture gave only its
    generated state directory mode `0777`.  The current helper accepted
    synthetic activation, and the fixture stopped with `activation accepted an
    other-writable rollback state directory` (exit 1).  No physical AP, guest,
    route, or real configuration was used.
  - decomp: not applicable; this is external laboratory transaction control.
- candidate causes:
  - confirmed: directory path validation was implemented without access-mode
    or owner validation.
- rejected causes:
  - state-file `0600`: it is written only after the unsafe directory is
    admitted, and cannot protect marker/FIFO/PID names from directory changes.
  - control-directory lock mode: it protects the lock/marker parent but does
    not secure the caller-selected transaction state directory.
- confirmed deviation: untrusted writable namespace is accepted as trusted
  rollback ownership.
- root cause: `require_state_dir()` has no current-UID or exact-mode check.

## FIX_CANDIDATE

- anomaly_id: `LAB-PMF-AP-RESTRICTED-STATE-INTEGRITY-20260721`
- symptom: other-writable temporary state can authorize a PMF AP transaction.
- expected system behavior: only a canonical, invoking-user-owned `0700`
  directory beneath the restricted prefix can enter any helper mode with state.
- actual behavior: prefix/canonical checks alone admit `0777` state.
- exact divergence point: missing `stat` UID and mode equality checks in
  `require_state_dir()` before `state_file()` is used.
- evidence from runtime: the fixture-only `0777` state directory let the
  unmodified helper report synthetic required activation, and the fixture
  deterministically rejected that acceptance (exit 1); no physical AP, route,
  guest, or real configuration was involved.
- evidence from decomp: not applicable; no Apple component owns the external
  temporary-directory transaction protocol.
- exact semantic mismatch between reference and our code: no reference-driver
  path applies.  The mismatch is against the SYSTEM_CONTRACT that rollback
  authority is restricted and marker-bound, not a shared temporary namespace.
- fix justification path: `SYSTEM_CONTRACT_FIX`.
- enumerated system-facing touchpoints:
  1. state/marker/watchdog PID/FIFO names: must live in private ownership;
  2. optional/required hostapd: no transition begins when that ownership fails;
  3. control lock: unchanged and remains a separate one-at-a-time guard;
  4. host networking, staged configs, rekey, guest, and candidate: untouched.
- expected contract at each touchpoint:
  1. only caller UID with `0700` directory can create/read transaction state;
  2. optional PID remains live and required PID never appears on rejection;
  3. no state format or lock semantics change;
  4. added checks are metadata reads only.
- why no relevant touchpoints are missing: the candidate gates the sole common
  `require_state_dir()` entrance used by activation, rekey, rollback, and
  watchdog.  It changes no downstream process or network behavior.
- why proposed path adds no extra system-visible side effects: it performs two
  local metadata reads before existing work.  Failure prevents an AP operation;
  it adds no retry, delay, file permission change, configuration write, or
  guest action.
- why this is root cause and not just correlation: all state-owning modes call
  this exact function, and no other path validates the directory's owner/mode.
- why proposed fix is 1:1 with reference architecture and semantics: no Apple
  implementation applies.  The system-contract fix makes the existing
  restricted-state premise explicit without altering state contents or owners.
- files/functions to modify:
  - `scripts/tahoe_pmf_required_ap_switchover.sh::require_state_dir`;
  - `scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh` unsafe-state
    rejection case;
  - `scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh` static predicate;
  - `docs/TAHOE_IWX_PMF_BIP_RUNTIME_PROTOCOL.md` restricted-state wording.
- forbidden alternative fixes considered and rejected:
  - chmod a caller-selected directory inside the helper;
  - trust state-file mode after opening an unsafe directory;
  - loosen marker ownership or rely solely on `flock`;
  - move/rename state after activation begins.
- verification plan:
  1. Completed: the fixture confirmed its generated `0777` state directory was
     incorrectly accepted for synthetic required activation by the pre-fix
     helper.
  2. Completed: UID/`0700` admission categorically rejects before optional PID
     changes, required PID, marker, state, or watchdog appear.
  3. Completed: PMF static/evidence contracts and the isolated Tahoe build-only
     gate passed.  No candidate activation, guest reboot, or live AP operation
     occurred.

## IMPLEMENTATION AND LOCAL VERIFICATION

- implementation:
  - `require_state_dir()` now requires the canonical state directory to be
    owned by the invoking UID and exactly mode `0700` before it permits any
    state-owning helper mode.
  - the helper does not chmod, move, or repair caller-provided directories; it
    simply rejects an unsafe namespace before state, marker, FIFO, watchdog, or
    hostapd work begins.
  - the local fixture changes only its generated directory to mode `0777` and
    confirms that no transaction artifact is created there.
- deterministic verification:
  - Before implementation, the AP-helper fixture failed exactly at `activation
    accepted an other-writable rollback state directory`; this is the
    controlled reproduction recorded above.
  - After implementation, the same fixture: PASS.  The `0777` directory gets
    a categorical permission diagnostic while the exact optional PID remains
    live; required PID, marker, state file, and watchdog receipt are absent.
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh`: PASS.
  - `bash scripts/test_tahoe_sae_quarantine_contract.sh`: PASS.
  - `bash scripts/run_tahoe_sae_quarantine_layer.sh`: PASS on the pinned
    isolated guest.  The Tahoe kext built successfully, all 959 undefined
    symbols resolved against BootKC, trace producers/Agent/RegDiag built, and
    the gate made no kext install/load/publish/release operation.
- verification boundary: this closes only state-directory admission integrity.
  It is not a live AP result, candidate activation, guest reboot, PMF/BIP
  association, or a substitute for operating-system filesystem isolation.
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

## ANOMALY

- id: `LAB-PMF-AP-REKEY-PROCESS-EDGE-20260721`
- status: `FIX_VERIFIED`
- scope: repository-owned required-PMF hostapd transaction evidence around the
  single bounded group-rekey command; no physical AP, guest, candidate, or
  driver-functionality result is claimed.
- symptom: `do_rekey()` verifies the exact required hostapd process only at
  the beginning of the function.  It then performs AP-shape and host-network
  reads, sends `REKEY_GTK`, accepts `OK`, and writes the rekey success witness
  without a process-identity check at either command boundary.
- expected system behavior: `PMF_AP_REKEY=REQUESTED` and
  `rekey_requested=true` must attest that the sole request was admitted by the
  exact marker-bound required-PMF hostapd process and that the same process
  remains current when that success witness is published.  A process exit or
  replacement during the intervening probes or the raw-control exchange is
  inconclusive and must not produce the witness.
- actual behavior: `configured_hostapd_active()` precedes
  `runtime_ap_is_pinned()` and `host_network_signature()`.  Neither probe
  validates the configured hostapd PID.  After the raw CLI `OK`, the code
  checks only AP radio shape and the host-network signature before writing
  `rekey.status` and success output.
- divergence point: `scripts/tahoe_pmf_required_ap_switchover.sh::do_rekey`,
  between its first `configured_hostapd_active()` call and the sole
  `REKEY_GTK` command, and again between the CLI acknowledgement and the
  `rekey_requested=true` witness.
- evidence:
  - local source directly orders the one initial process check before several
    non-PID probes and contains no later `configured_hostapd_active()` call;
  - the existing no-AP fixture already provides a generated required PID and
    a fake-`iw` termination hook, demonstrating that the AP-shape probe can
    remain syntactically pinned after the exact process has been removed;
  - the runner treats the helper's categorical output as admission for the
    final cross-slot trace verdict, so a stale process observation cannot
    support that causal attribution;
  - decomp: not applicable.  This is host-side experiment ownership, not a
    claim about an Apple or firmware implementation.
- candidate causes:
  - confirmed: rekey ownership is modeled as a precondition only, rather than
    an edge predicate that remains true across the command acknowledgement.
  - confirmed: AP channel/width and host route/address/forwarding signatures
    do not establish the identity or liveness of the hostapd process that
    accepted the control request.
- rejected interpretations:
  - this does not prove that a live hostapd daemon will spontaneously exit;
  - a successful later guest authorization does not retroactively bind the
    helper's one raw control acknowledgement to the original process;
  - the fix must not add another group-rekey, a retry, a process restart, or
    a live AP action.
- confirmed deviation: the success witness currently means only that a
  process was exact at an earlier observation and that the raw control client
  printed `OK`; the declared bounded transaction requires exact required-AP
  ownership at the request and witness edges.
- root cause: the lifecycle check is not repeated after the non-PID admission
  probes or after the asynchronous raw-control acknowledgement.

## FIX_CANDIDATE

- anomaly_id: `LAB-PMF-AP-REKEY-PROCESS-EDGE-20260721`
- symptom: a dead or replaced required AP can be represented as the owner of
  the bounded rekey request.
- expected system behavior: re-attest the exact required hostapd process
  immediately before the raw `REKEY_GTK` command and immediately after a
  positive acknowledgement, before AP/network postconditions or success
  state/output are recorded.
- actual behavior: the only exact-process test occurs before the AP-shape and
  network admission probes; the success path after `OK` contains no process
  identity test.
- exact divergence point: `do_rekey()` pre-command and post-ack command
  boundaries.
- evidence from runtime: the deterministic local fixture will kill only its
  generated fake required child either during the existing AP-shape probe or
  immediately after its fake CLI records `OK`.  On the unmodified helper this
  reaches the rekey success path; no host network, real hostapd, AP, guest, or
  credential is involved.
- evidence from decomp: N/A; no reverse-engineered driver path is changed.
- exact semantic mismatch: a stale liveness snapshot is accepted where the
  categorical transaction witness claims current process ownership.
- fix justification path: `SYSTEM_CONTRACT_FIX`.
- enumerated system-facing touchpoints:
  1. required hostapd lifecycle: exact PID/argument/config identity is
     required at both raw-control command edges;
  2. raw control transport: the original one `REKEY_GTK` request remains the
     only stimulus and is not retried;
  3. AP pin/network invariants: existing read-only postconditions remain in
     their current order after the new process fences;
  4. marker/watchdog/rollback: a failed rekey leaves the existing rollback
     owner intact for the caller/watchdog rather than fabricating recovery;
  5. guest, routes, addresses, DHCP, candidate, kext, and physical host: no
     new operation is introduced.
- expected contract at each touchpoint:
  1. no rekey witness is written when the required PID vanishes before or
     after the acknowledged request;
  2. exactly one raw-control command is issued only after a current pre-edge
     required-process observation;
  3. process/AP/network postconditions all remain mandatory for success;
  4. failure is categorical and leaves cleanup ownership unchanged;
  5. test injections are local fake-process operations only.
- why no relevant touchpoints are missing: the candidate closes the two
  unguarded process-identity edges around the sole existing stimulus.  It
  neither changes configuration/state formats nor adds an AP lifecycle path.
- why proposed path adds no extra system-visible side effects: the normal path
  adds two read-only `ps`/PID-file observations.  The failure path withholds a
  witness; it does not launch, stop, reload, retry, or reconfigure hostapd.
- why this is root cause and not just correlation: the helper's direct source
  order permits the synthetic child death after its only PID check, and its
  remaining success predicates make no reference to that process.
- why proposed path is 1:1 with reference architecture and semantics: no
  Apple reference implementation applies.  The correction reuses the helper's
  established exact-process predicate at the actual command boundaries.
- files/functions to modify:
  - `scripts/tahoe_pmf_required_ap_switchover.sh::do_rekey`;
  - `scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh`: local
    before-command and post-ack required-child death injections;
  - `scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh`: static ordering
    contract for both rekey process fences;
  - `docs/TAHOE_IWX_PMF_BIP_RUNTIME_PROTOCOL.md`: rekey ownership wording;
  - `analysis/ANALYSIS_REPORT_2026-07-21.md`: source evidence and results.
- forbidden alternative fixes considered and rejected:
  - trust `hostapd_cli` output alone;
  - retry `REKEY_GTK`, restart hostapd, or use a timer to infer liveness;
  - weaken the AP process identity predicate to channel/width alone;
  - add a guest join, scan, route/address/DHCP mutation, or touch the live AP.
- verification plan:
  1. add local generated-child-death cases and show the pre-fix helper emits
     rekey success despite process loss;
  2. add the two exact-process fences and require both cases to fail without a
     `rekey.status` witness, while the stable one-request fixture still
     passes;
  3. run AP/runtime/SAE contracts and the isolated Tahoe build-only gate.  Do
     not activate a candidate, reboot a guest, or operate the live AP.

## IMPLEMENTATION AND LOCAL VERIFICATION

- implementation:
  - `do_rekey()` now reuses `configured_hostapd_active()` immediately after
    the existing host-network precondition and immediately after a positive
    raw-control acknowledgement;
  - only after the post-ack exact-process fence do the existing AP-shape and
    host-network postconditions permit `rekey_requested=true` and
    `PMF_AP_REKEY=REQUESTED`;
  - the local fixture's pre-command fake-`iw` termination proves that a stale
    earlier PID observation cannot reach `hostapd_cli`; its post-ack fake CLI
    termination proves that one acknowledged command cannot write a success
    witness after the required child disappears;
  - each failure is recovered through the pre-existing explicit rollback and
    a fresh generated required transaction is used for the stable one-request
    positive case.  No process restart is added to the helper itself.
- deterministic verification:
  - before implementation, the new fixture stopped exactly at `group rekey
    accepted a required hostapd that died before the command edge`; this was a
    generated local child only and made no real AP or network operation;
  - after implementation,
    `bash scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh`: PASS.
    The pre-command injection makes zero fake CLI calls and no witness; the
    post-ack injection makes exactly one fake CLI call and no witness; both
    restore generated optional PMF, and the stable single request passes;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh`: PASS;
  - `bash scripts/test_tahoe_post_plti_trace_contract.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_evidence_contract.sh
    --self-test`: PASS;
  - `bash scripts/test_tahoe_sae_quarantine_contract.sh`: PASS;
  - `bash scripts/run_tahoe_sae_quarantine_layer.sh`: PASS in the pinned
    isolated Tahoe guest.  The kext, trace client, Agent, and RegDiag built,
    all 959 undefined symbols resolved against BootKC, and no kext was
    installed, loaded, published, or released.
- verification boundary: this closes only a host-side rekey ownership race.
  It is not a live hostapd result, candidate activation, guest reboot,
  PMF-required association, IGTK runtime observation, or driver-functionality
  result.
- external blocker unchanged: the optional/required saved-profile identity
  preflight remains categorically mismatched.  No live configuration was
  read, changed, or bypassed.

## IMPLEMENTATION AND LOCAL VERIFICATION: `LAB-PMF-AP-WATCHDOG-PRETRANSITION-ATTESTATION-20260721`

- implementation:
  - added `watchdog_owner_is_current()`, which rereads the restricted
    watchdog PID receipt through the existing PID validator and requires the
    exact session/state-bound watchdog argv match;
  - `start_watchdog()` now repeats that predicate after it writes the PID
    receipt, so setup itself cannot return a stale owner observation;
  - `do_activate()` repeats it after all final host-network/configuration
    pre-stop fences and immediately before optional hostapd can be stopped;
  - a failed final predicate follows the existing pre-stop failure path:
    optional PMF remains active, the lighter cleanup clears marker state, and
    no required process is started.  No watchdog replacement, timer/retry, AP
    restart, or live-network action was introduced.
- deterministic fixture evidence:
  - before implementation, fake route call number two killed only the
    generated watchdog and removed its fixture receipt while returning the
    normal route text.  The old helper still returned required-active, and the
    fixture stopped at `activation accepted a watchdog that died before
    optional-PMF stop`;
  - after implementation, that same local event produces
    `rollback watchdog is not exact before optional-PMF stop; optional-PMF
    state retained`, preserves the original live optional PID, starts no
    required child, clears the marker, and leaves no stale fake receipt;
  - static ordering now requires watchdog re-attestation after PID receipt
    persistence and after final configuration/network fences before optional
    stop.
- verification:
  - `bash scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh`: PASS;
  - `bash scripts/test_tahoe_post_plti_trace_contract.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_evidence_contract.sh
    --self-test`: PASS;
  - `bash scripts/test_tahoe_sae_quarantine_contract.sh`: PASS;
  - `bash scripts/run_tahoe_sae_quarantine_layer.sh`: PASS in the pinned,
    isolated Tahoe build directory (source identity `dirty110c9d102b8c`).
    The kext, trace producer, Agent, and RegDiag built; all 959 undefined
    symbols resolved against BootKC; no kext was installed, loaded, published,
    or released.
- verification boundary: this closes only a host-side rollback-owner freshness
  gap.  It is not a live hostapd result, PMF-required association, IGTK
  observation, candidate activation, guest reboot, or driver-functionality
  result.
- external blocker unchanged: the optional/required saved-profile identity
  preflight remains categorically mismatched.  No live configuration was
  read, changed, or bypassed.

## IMPLEMENTATION AND LOCAL VERIFICATION: `LAB-PMF-AP-EXACT-ONE-RAW-REKEY-REQUEST-20260721`

- implementation:
  - added a restricted `rekey.requested` receipt containing only
    `rekey_attempted=true`; its freshness predicate also treats an existing
    legacy/success `rekey.status` as a consumed transaction;
  - `do_rekey()` rejects a consumed request before AP/control admission,
    then records the request after its final exact required-process
    pre-command fence and immediately before the sole raw `REKEY_GTK`;
  - the pre-existing `rekey.status` remains success-only and is still written
    only after acknowledgement plus process/AP/network postconditions;
  - a failed receipt write or any post-command failure is inconclusive and
    leaves the AP rollback owner intact; no retry, restart, reload, or extra
    network/live action was introduced.
- deterministic fixture evidence:
  - before implementation, a generated post-command host-network drift made
    the first raw request inconclusive with no success witness; after restoring
    only fake network output, a second helper invocation reached fake CLI and
    stopped at `group rekey accepted a retry after an acknowledged
    inconclusive request`;
  - after implementation, that first raw request persists
    `rekey.requested`; the retry produces the categorical already-recorded
    diagnostic, adds no fake CLI line, writes no success witness, and then
    rolls back normally;
  - a fresh stable transaction writes both the request receipt and its normal
    success witness.  Its duplicate invocation is rejected before fake CLI
    and preserves the original witness;
  - existing pre-command drift/death cases still reach no raw command and do
    not consume the request before the final command edge.
- verification:
  - `bash scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh`: PASS;
  - `bash scripts/test_tahoe_post_plti_trace_contract.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_evidence_contract.sh
    --self-test`: PASS;
  - `bash scripts/test_tahoe_sae_quarantine_contract.sh`: PASS;
  - `bash scripts/run_tahoe_sae_quarantine_layer.sh`: PASS in the pinned,
    isolated Tahoe build directory (source identity `dirty79108b9e6293`).
    The kext, trace producer, Agent, and RegDiag built; all 959 undefined
    symbols resolved against BootKC; no kext was installed, loaded, published,
    or released.
- verification boundary: this closes only a host-side bounded-rekey command
  cardinality gap.  It is not a live hostapd result, PMF-required association,
  IGTK observation, candidate activation, guest reboot, or driver
  functionality result.
- external blocker unchanged: the optional/required saved-profile identity
  preflight remains categorically mismatched.  No live configuration was
  read, changed, or bypassed.

## IMPLEMENTATION AND LOCAL VERIFICATION: `LAB-PMF-AP-ROLLBACK-WITNESS-COMMIT-ORDER-20260721`

- implementation:
  - `do_rollback()` now keeps all existing optional-process, AP-shape, and
    host-network checks in place; it then cancels the non-watchdog caller's
    exact watchdog, clears the marker, and only then creates the restricted
    `rollback.status` witness;
  - watchdog-owned rollback retains its existing no-self-cancel behavior but
    likewise clears the marker before it writes the receipt;
  - the runner remains receipt-first during cleanup, but the receipt now
    denotes completed recovery ownership release rather than an intermediate
    restoration observation;
  - no AP restart/retry, configuration mutation, network operation, or live
    infrastructure action was added.
- deterministic fixture evidence:
  - before implementation, a fixture-only unrelated local `sleep` PID replaced
    the private watchdog receipt after a generated required activation.  The
    helper correctly failed with `rollback could not safely cancel its
    watchdog`, retained its original watchdog and marker, but had already
    written `rollback.status`; the fixture stopped at
    `rollback verification was published before watchdog ownership released`;
  - after implementation, the same failure creates no receipt and retains the
    original generated watchdog/marker.  Restoring the original private PID
    receipt then permits one normal rollback, commits the witness, clears the
    marker, and terminates the original watchdog;
  - the static contract requires the final ordering
    optional-process/AP/network attestation -> watchdog disposition -> marker
    release -> `rollback_verified=true` and requires the foreign-watchdog
    fixture discriminator.
- verification:
  - `bash scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh`: PASS;
  - `bash scripts/test_tahoe_post_plti_trace_contract.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_evidence_contract.sh
    --self-test`: PASS;
  - `bash scripts/test_tahoe_sae_quarantine_contract.sh`: PASS;
  - `bash scripts/run_tahoe_sae_quarantine_layer.sh`: PASS in the pinned,
    isolated Tahoe build directory (source identity `dirty989cc7e4bc4f`).
    The kext, trace producer, Agent, and RegDiag built; all 959 undefined
    symbols resolved against BootKC; no kext was installed, loaded, published,
    or released.
- verification boundary: this closes only a host-side rollback receipt-order
  race.  It is not a live hostapd result, PMF-required association, IGTK
  observation, candidate activation, guest reboot, or driver-functionality
  result.
- external blocker unchanged: the optional/required saved-profile identity
  preflight remains categorically mismatched.  No live configuration was
  read, changed, or bypassed.

## IMPLEMENTATION AND LOCAL VERIFICATION: `LAB-PMF-AP-ROLLBACK-OPTIONAL-PROCESS-ATTESTATION-20260721`

- implementation:
  - added `optional_hostapd_exact_and_pinned()`, a read-only conjunction of
    the existing exact PID/argv/configuration predicate and the existing
    pinned AP-shape predicate;
  - `restore_optional_after_activation_failure()` now ends with that
    conjunction rather than AP shape alone;
  - `finish_armed_rollback()` repeats the conjunction immediately before it
    cancels the watchdog, and `finish_post_transition_rollback()` repeats it
    after its host-network comparison and before releasing ownership;
  - `do_rollback()` retains its existing start/AP/network sequence and adds
    the same final conjunction immediately before it writes
    `rollback_verified=true`;
  - no restart, retry, reload, configuration write, network mutation, or new
    live operation was added to the helper.
- deterministic fixture evidence:
  - before implementation, the new local fake-`iw` injection removed only the
    generated optional child after `wait_hostapd_active()` had observed it,
    continued reporting the pinned shape, and stopped at
    `rollback accepted an optional hostapd that died before verification`;
    this proves that the old helper wrote a false success witness without
    contacting a real AP or guest;
  - after implementation, the explicit rollback injection fails with
    `optional-PMF hostapd process or AP shape is not exact before rollback
    verification`, leaves no `rollback.status`, and retains the generated
    marker/watchdog; the subsequent injection-free explicit rollback restores
    optional PMF and clears both owner receipts;
  - the failed-required-start injection likewise reports
    `rollback watchdog remains armed`, does not claim verified recovery, and
    leaves its marker/watchdog for the following stable explicit rollback;
  - static ordering now requires the optional exact-process/AP fence in the
    restore, armed recovery, post-transition recovery, and explicit rollback
    release paths.
- verification:
  - `bash scripts/test_tahoe_pmf_required_ap_switchover_fixture.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_contract.sh`: PASS;
  - `bash scripts/test_tahoe_post_plti_trace_contract.sh`: PASS;
  - `bash scripts/test_tahoe_iwx_pmf_bip_runtime_evidence_contract.sh
    --self-test`: PASS;
  - `bash scripts/test_tahoe_sae_quarantine_contract.sh`: PASS;
  - `bash scripts/run_tahoe_sae_quarantine_layer.sh`: PASS in the pinned,
    isolated Tahoe build directory.  The kext, trace producer, Agent, and
    RegDiag built; all 959 undefined symbols resolved against BootKC; no kext
    was installed, loaded, published, or released (source identity
    `dirtyec8a262ec8c6`).
- verification boundary: this fixes only a host-side AP recovery ownership
  race.  It is not a live hostapd result, PMF-required association, IGTK
  observation, guest reboot, candidate activation, or driver-functionality
  result.
- external blocker unchanged: the optional/required saved-profile identity
  preflight remains categorically mismatched.  No live configuration was
  read, changed, or bypassed.
