# Tahoe post-PLTI trace matrix contract

## Scope

This document records the verified, non-functional diagnostic scenarios that
ship with the Tahoe post-PLTI trace v3 layer.  The generic ordered association evaluator is IWN-only.
IWX, including AX211, remains backend-unsupported for that evaluator and
cannot inherit an IWN verdict.

v3 adds a dedicated IWX PMF/BIP evaluator with a distinct, sealed
categorical chain: validated PMF receive delivery, q0 doorbell, q0
completion observation, post-acknowledgement IGTK slot-4 or slot-5
publication, matching selected TX slot, port-valid, and an explicit capture
seal.  Its only positive outcomes are a completed initial PMF/BIP ownership
chain or a completed cross-slot rekey.  A port-valid record intentionally
keeps an IWX episode open until the explicit seal so the bounded rekey window
remains attributable to the same capture generation.

The companion active-prefix classifier is narrower still: it accepts only the
one live initial PMF/BIP chain through port-valid while the same episode remains
open.  It is a rekey authorization predicate for the bounded runner, never a sealed verdict or final success.
Any seal, abort, mixed identity, trace drop,
or extra post-port-valid PMF/BIP fact makes that progress result inconclusive.

This evaluator reports only implementation-local, ordered instrumentation
facts.  It does not prove PMF-required association, traffic, SAE, candidate
activation, external reachability, physical radio behavior, or a successful
application exchange.  It accepts neither a raw frame nor a firmware result,
and it exposes neither key material nor a status value.

The public ABI contains only generation, episode, sequence, backend, and
categorical event identifiers.  It contains no network identity, channel,
signal, address, route, credential, key, packet, firmware-status, or pointer
field.

The Tahoe bridge selects its real implementation from the Tahoe-only
`IO80211FAMILY_V3` target marker.  Shared IWN/IWX trace producer sources can include
the bridge before Tahoe compatibility headers declare availability markers, so
an availability predicate would silently select their local no-op fallback.
The post-build gate verifies every state, RX, TX, EAPOL, IWN, and IWX producer
object references its external bridge and rejects that fallback.  Candidate
builds use one fresh absolute DerivedData override for both the Tahoe build
and this linkage gate, so an incrementally stale object cannot be admitted as
evidence.  The gate drains each producer's symbol listing rather than using an
early-exit probe, so its `pipefail` policy cannot turn a successful match into
a false admission failure.

## Sealed capture rule

A control seal first blocks new episodes, then detaches the active token and
records one categorical capture-window-sealed terminal marker where an active
episode exists.  The control then disables admission.  A later off control
cannot append an abort to that sealed episode.  Two identical frozen reads
are consequently evidence of one stable capture window rather than a
teardown-induced partial trace.

A fixed non-sleeping trace gate serializes every recorder reservation with
snapshot publication.  Reset, seal, and invalidation first close an epoch and
wait for producers that had already entered it before changing capture state.
A contended producer in that established epoch increments the public dropped
counter before leaving; the client rejects every nonzero count as an integrity
failure rather than assigning a missing functional stage.  A producer that
loses the epoch race is discarded instead of being relabeled into the new
capture; it cannot create an episode after a seal.  Thus no omitted required
event can produce either an exact negative diagnosis or a success.

The passive state-request observation follows the null check but precedes the
STA-only security-epoch gate.  It therefore reports an actual state macro
request even where epoch bookkeeping intentionally has no STA work to do; it
does not alter the state transition or loosen that security gate.

## Versioned synthetic scenarios

The payload-builder unit and source contract successfully exercise these
scenarios in the same commit as the trace implementation:

- WCL resume followed by seal identifies a missing state self-request.
- State self-request followed by seal identifies a missing IWN callback.
- IWN callback followed by categorical scan-command rejection identifies a
  failed submission without exposing an error code.
- A scan start without completion identifies an incomplete scan stage.
- A no-candidate retry followed by a second coalesced scan remains one ordered
  episode and can end in selection-held.
- An IWN second-band command rejection can still be followed by the existing
  first-band completion and selection boundary; a rejection without that
  completion remains a sealed diagnostic failure.
- A selected BSS followed by seal identifies the missing join boundary, and a
  partial authentication prefix identifies its exact missing dequeue boundary.
- An EAPOL enqueue followed by seal identifies the remaining port-valid
  boundary instead of relabeling the observed EAPOL stage as absent.
- Two ordered inbound EAPOL rounds, including interleaved optional TX
  corroboration after a real enqueue, remain eligible for the port-valid
  terminal; those optional TX markers never establish success on their own.
- IWX PMF receive delivery is recorded only after the worker's stale
  epoch/current-BSS rejection fence; q0 submit is recorded after its
  doorbell and completion only after q0/sleep unlocks.
- An IWX initial PMF/BIP fixture requires PMF receive, q0 doorbell and
  completion, one post-acknowledgement slot publication, matching active TX
  selection, port-valid, and a seal.
- Slot 4 followed by slot 5, and slot 5 followed by slot 4, each exercise a
  distinct cross-slot rekey classification.  Repeated selection, same-slot
  replacement, publication without the PMF owner chain, or selection before
  publication is inconclusive.
- A missing q0 completion, active unsealed episode, cancellation, mixed
  generation or episode, post-terminal event, drop, or overflow is
  fail-closed.  The generic IWN evaluator remains BACKEND_UNSUPPORTED for
  every IWX trace, including one with all PMF/BIP observer facts.
- A post-terminal event, mixed episode/generation, sequence gap, drop, or
  unsupported backend is rejected or fail-closed.

The sole complete diagnostic trace classification is KernelChainObserved.  It
is ordered instrumentation evidence only, not proof of successful
association, reachability, SAE, PMF, or AX211 behavior.  Every sealed prefix
above is a completed diagnostic evidence run with an INCONCLUSIVE result,
never a successful association claim.  Its `first_missing_stage` is derived
from the parser's terminal phase, not reconstructed from a coarse verdict.

## Scan-policy preservation

The trace does not choose a scan band or alter IWN scan flags.  It preserves
the existing first-pass scan policy, including IWN's ordinary second-band
continuation.  The evaluator accepts the resulting repeated categorical scan
start markers before the one completion marker, without recording any band or
channel identity.  Static coverage forbids trace-driven scan-policy changes.

## Sanitized runtime record

The successful isolated runtime record for source commit
`ba4a2f0833da4ca02d654f929bac8e5d4e8a6412` is retained in
`docs/TAHOE_RUNTIME_BA4A2F0.md` and the paired aggregate-only evidence files
under `evidence/runtime/`.  It records one complete IWN categorical trace and
a separate four-cycle radio baseline without committing a wireless identity,
credential, address, route, packet, or raw client output.  The record uses
the single mutable `v2.4.0-alpha` semantic-release model; it does not itself
create or replace a release asset.

## Release handling

A semantic version owns one mutable prerelease asset.  A fully verified
candidate replaces that asset and target under the existing semantic tag;
intermediate commit markers are kept in Git history and sanitized evidence,
not published as adjacent releases.  A new tag is created only for a new
completed version-level layer.

## Non-claims

This layer does not implement or prove pure SAE, Algorithm 3 authentication,
PMF-required association, external traffic success, candidate activation,
WCL link publication, generic reachability, or physical-host validation.  The
IWX observer is categorical and fail-closed; its post-acknowledgement
publication and selected-slot facts do not upgrade those non-claims.
