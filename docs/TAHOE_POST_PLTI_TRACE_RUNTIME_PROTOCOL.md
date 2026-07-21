# Tahoe post-PLTI trace runtime protocol

This protocol is for one release-bound diagnostic run after the trace layer has
passed its static and Tahoe build gates.  It is intentionally narrower than
the A2DF four-cycle baseline: it uses one radio OFF/ON transition to create a
single saved-profile autojoin opportunity and records only the safe categorical
post-PLTI trace.

## Preconditions

1. Build the exact source candidate and the `airport_itlwm_post_plti_trace`
   client with the Tahoe gate.
2. Complete private AuxKC admission and transactional activation for the
   release archive.  Reboot only a fresh, disposable external-overlay guest.
3. Capture the existing read-only exact-candidate identity attestation for
   that guest and archive.  Every candidate-binding check and its
   `ready_for_exact_candidate_runtime_experiment` verdict must be true.  The
   release tag must end in the seven-character prefix of `--source-commit`.
4. Copy the built client before the run to a restricted guest-local path such
   as `/private/tmp/aiam-post-plti-trace-CANDIDATE/airport_itlwm_post_plti_trace`.
   This copy is not a KEXT install/load operation.
5. Ensure an already authorized saved profile exists and the guest radio is
   already On.  Do not pass a wireless name, password, BSSID, address, route,
   or credential to the runner.

The runner itself rechecks only the pinned local QEMU SSH host key, guest OS
build, and the supplied local identity attestation.  It does not enumerate a
network identity or profile, so the saved-profile requirement remains an
external precondition rather than an unproved runner claim.

## Invocation

From the checkout that produced the candidate, run:

```bash
scripts/test_tahoe_post_plti_trace_runtime_contract.sh
scripts/run_tahoe_post_plti_trace_runtime.sh \
  --trace-tool /private/tmp/aiam-post-plti-trace-CANDIDATE/airport_itlwm_post_plti_trace \
  --identity-evidence /local/safe/tahoe_lab_kext_identity.json \
  --source-commit FULL_GIT_SHA \
  --out runtime-captures/post-plti-CANDIDATE
scripts/test_tahoe_post_plti_trace_runtime_evidence_contract.sh \
  --evidence runtime-captures/post-plti-CANDIDATE/runtime-attestation.json
```

`runtime-captures/` is local-only.  Do not commit the client stdout/stderr
files.  If the safe aggregate passes its contract, create a separate sanitized
runtime document and evidence JSON that describe only the successful scenario
and its explicit non-claims.

## Required sequence and interpretation

The runner performs, in order:

1. reset the trace and wait for an acknowledged nonzero generation on the IWN
   backend;
2. wait until snapshot and buffer reads agree with that generation and show an
   empty, undropped trace;
3. observe the radio On, execute exactly one public OFF/ON transition, and
   let macOS use saved-profile autojoin without an explicit join command;
4. collect two identical safe trace reads after a bounded settle period; and
5. turn tracing off and wait for its acknowledgement.

`PASS` is intentionally strict: both radio states, generation synchronization,
zero dropped entries, stable double-read, final trace shutdown, and the shared
`KERNEL_CHAIN_OBSERVED` verdict are required.  A missing branch,
unsupported backend, incomplete ordering, post-reset race, or unstable read is
`INCONCLUSIVE`, not a functional success.

The verdict requires the ordered categorical chain through port-valid after
EAPOL enqueue.  Firmware submit/TX completion may arrive asynchronously and is
only corroborating evidence; this shell runner does not independently require
an EAPOL TX-done marker.

This does not prove pure SAE or PMF functionality, generic Internet
reachability, or physical-host validation.  It makes no claim about network
identity, credentials, addresses, routes, DHCP state, packets, or raw driver
logs.
