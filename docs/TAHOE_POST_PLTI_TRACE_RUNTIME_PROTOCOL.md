# Tahoe post-PLTI trace runtime protocol

This protocol is for one release-bound diagnostic run after the trace layer has
passed its static and Tahoe build gates.  It is intentionally narrower than
the A2DF four-cycle baseline: it uses one radio OFF/ON transition to create a
single saved-profile autojoin opportunity and records only the safe categorical
post-PLTI trace.

The current release-bound runner remains IWN-only.  It is not the runtime
protocol for the IWX PMF q0 categorical observer.

If reset control identifies `backend=iwx`, the IWN-only runner records a
local `INCONCLUSIVE` aggregate with `verdict=BACKEND_UNSUPPORTED` and
`failure_phase=trace-backend-iwx-ordered-unsupported`, then stops before the
radio OFF/ON transition.  This is an explicit protocol-boundary result, not
an IWX PMF-observer experiment or evidence of PMF, SAE, association, or data
plane behavior.

## Preconditions

1. Build and package the exact source candidate and the
   `airport_itlwm_post_plti_trace` client with the Tahoe gate.  From the clean
   committed checkout that produced the archive, create a local
   `create_tahoe_candidate_provenance.py` manifest.  It binds the full source
   commit and source identity to the archive digest, binary digest, UUID, and
   semantic release tag.
2. Complete private AuxKC admission and transactional activation for the
   release archive.  Reboot only a fresh, disposable external-overlay guest.
3. Capture a fresh read-only v2 exact-candidate identity attestation for that
   guest, archive, and provenance manifest.  Every candidate-binding check and its
   `ready_for_exact_candidate_runtime_experiment` verdict must be true.  The
   release tag is a semantic version such as v2.4.0-alpha.  It is the single
   mutable prerelease for that semantic version, not a new tag for every
   commit.  The source commit, archive digest, binary digest, and Mach-O UUID
   remain identity-bound fields in the sanitized attestation; the runner does
   not accept a free source-commit label.
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
scripts/create_tahoe_candidate_provenance.py \
  --release-zip /local/safe/AirportItlwm-Tahoe.kext.zip \
  --release-tag v2.4.0-alpha \
  --output /local/safe/tahoe_candidate_provenance.json
scripts/capture_tahoe_lab_kext_identity.py \
  --expected-release-zip /local/safe/AirportItlwm-Tahoe.kext.zip \
  --candidate-provenance /local/safe/tahoe_candidate_provenance.json \
  --output /local/safe/tahoe_lab_kext_identity.json
scripts/run_tahoe_post_plti_trace_runtime.sh \
  --trace-tool /private/tmp/aiam-post-plti-trace-CANDIDATE/airport_itlwm_post_plti_trace \
  --identity-evidence /local/safe/tahoe_lab_kext_identity.json \
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
4. make at most five bounded local report reads after the settle period, solely
   to allow an already ordered success chain to finish before capture is
   frozen;
5. seal the capture: block new episode admission, detach the active episode
   token, append the categorical sealed terminal marker where applicable, and
   wait for the acknowledged disabled control state;
6. collect two identical frozen safe trace reads; and
7. send a final off control and wait for its acknowledgement.

`PASS` is intentionally strict: both radio states, generation synchronization,
zero dropped entries, an acknowledged seal, a stable sealed double-read, final
trace shutdown, `first_missing_stage=none`, and the shared
`KERNEL_CHAIN_OBSERVED` verdict are required.  A missing branch,
unsupported backend, incomplete ordering, post-reset race, or unstable read is
`INCONCLUSIVE`, not a functional success.

A structurally valid sealed diagnostic prefix is retained locally as an
`INCONCLUSIVE` evidence result rather than being discarded as a runner failure.
It is useful for locating the first categorical missing stage, but it is never
an association-success claim.

The runner exits successfully for such a valid diagnostic prefix so that its
sanitized evidence is retained.  Release eligibility must instead require the
evidence contract to report `result=PASS`; a zero process exit alone is not
release authorization.

The verdict requires the ordered categorical chain through port-valid after
EAPOL enqueue.  Firmware submit/TX completion may arrive asynchronously and is
only corroborating evidence; this shell runner does not independently require
an EAPOL TX-done marker.

This does not prove pure SAE or PMF functionality, generic Internet
reachability, or physical-host validation.  It makes no claim about network
identity, credentials, addresses, routes, DHCP state, packets, or raw driver
logs.
