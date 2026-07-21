# Tahoe post-PLTI trace matrix contract

## Scope

This document records the verified, non-functional diagnostic scenarios that
ship with the Tahoe post-PLTI trace v2 layer.  The trace is IWN-only.  IWX,
including AX211, remains explicitly backend-unsupported for this trace and
cannot inherit an IWN verdict.

The public ABI contains only generation, episode, sequence, backend, and
categorical event identifiers.  It contains no network identity, channel,
signal, address, route, credential, key, packet, firmware-status, or pointer
field.

## Sealed capture rule

A control seal first blocks new episodes, then detaches the active token and
records one categorical capture-window-sealed terminal marker where an active
episode exists.  The control then disables admission.  A later off control
cannot append an abort to that sealed episode.  Two identical frozen reads
are consequently evidence of one stable capture window rather than a
teardown-induced partial trace.

A fixed non-sleeping trace gate serializes every recorder reservation with
reset, seal, snapshot, and invalidation.  A contended producer omits its
diagnostic event before reserving a sequence; it cannot leave a sequence hole
or create an episode after a seal.  Such an omitted required event is
fail-closed as an incomplete diagnostic classification, never a success.

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
- A selected BSS followed by seal identifies the missing join boundary, and a
  partial authentication prefix identifies its exact missing dequeue boundary.
- An EAPOL enqueue followed by seal identifies the remaining port-valid
  boundary instead of relabeling the observed EAPOL stage as absent.
- A post-terminal event, mixed episode/generation, sequence gap, drop, or
  unsupported backend is rejected or fail-closed.

The sole complete diagnostic trace classification is KernelChainObserved.  It
is ordered instrumentation evidence only, not proof of successful
association, reachability, SAE, PMF, or AX211 behavior.  Every sealed prefix
above is a completed diagnostic evidence run with an INCONCLUSIVE result,
never a successful association claim.  Its `first_missing_stage` is derived
from the parser's terminal phase, not reconstructed from a coarse verdict.

## 5 GHz scan selection

For IWN scan entry, the requested band now derives from the current net80211
scan channel.  A current 5 GHz channel requests the 5 GHz scan class; the
safe fallback is the 2.4 GHz class.  The trace records no band or channel
identity.  Static contract coverage forbids the prior hard-coded 2.4 GHz
request.

## Release handling

A semantic version owns one mutable prerelease asset.  A fully verified
candidate replaces that asset and target under the existing semantic tag;
intermediate commit markers are kept in Git history and sanitized evidence,
not published as adjacent releases.  A new tag is created only for a new
completed version-level layer.

## Non-claims

This layer does not implement or prove pure SAE, Algorithm 3 authentication,
PMF, IGTK, AX211 runtime behavior, generic reachability, traffic, or
physical-host validation.
