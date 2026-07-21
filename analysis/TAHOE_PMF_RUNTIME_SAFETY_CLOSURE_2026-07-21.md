# Tahoe PMF/BIP runtime safety closure — 2026-07-21

## Status

The implementation and build-admission layers needed before a controlled IWX
PMF/BIP runtime experiment are present and independently gated. No candidate
has been activated, no guest reboot has been performed for this checkpoint,
and no required-PMF AP transition has been attempted.

The current live boundary remains fail-closed. A repeated categorical AP
preflight rejects the optional and staged required configurations with
`ssid-pair-mismatch`. No configuration values, wireless identities, or
credentials were rendered or recorded. That result is a prerequisite failure,
not a PMF/BIP negative result and not permission to infer a compatible saved
profile.

## Closed safety layers

The branch contains these cumulative runtime-safety layers:

- IWX-specific categorical PMF/BIP trace evaluation, including initial-slot and
  cross-slot IGTK evidence;
- a bounded saved-profile-only runtime runner and a one-at-a-time AP helper
  with pre-armed rollback watchdog ownership;
- canonical hostapd `REKEY_GTK` control use rather than a packaged-client
  alias;
- host-side fresh qcow2 overlay preparation with one direct backing image,
  a sanitized local receipt, and a receipt validator;
- transactional AuxKC abort rollback on ordinary failed exit, HUP, INT, and
  TERM, with the collection restored before the bundle;
- runner cleanup that owns every allocated AP state directory even if an
  interrupt races the local required-AP flag;
- trace cleanup armed before reset and a rekey authorization fence that binds
  the live initial prefix to one nonzero active episode; and
- pre- and post-command hash-only host-network checks around the one bounded
  group rekey, with synthetic fixtures proving both drift cases fail closed.

The latest source-only commits for the final three items are `2cbe061`,
`4d43a9b`, and `6ec09ec`; their ancestors include the evaluator, runtime
runner, overlay, and activation-safety layers. These hashes identify source
history only and are not release names or runtime evidence.

## Verification

The aggregate SAE/PMF contract passed after each safety layer. Its coverage
includes the IWX trace fixtures, AP-helper synthetic lifecycle and drift
fixtures, overlay synthetic image fixture, candidate-identity self-tests, and
all existing quarantine contracts.

The pinned Tahoe isolated build gate also passed after each source layer. It
builds the kext, resolves all 959 undefined symbols against the pinned BootKC,
audits trace producer bridges, and builds Agent and RegDiag in a fresh guest
temporary directory. The gate neither installs nor loads a kext, changes an
AuxKC, publishes a release, reboots a guest, or contacts a physical validation
host.

## Required next external condition

Before any candidate activation or guest-only reboot, a separately reviewed
lab configuration/profile plan must make the optional and required PMF
configurations represent the same already-saved identity and credential. The
only acceptable immediate confirmation is a fresh categorical AP preflight
pass.

Only after that condition, the permitted sequence remains: fresh disposable
overlay receipt, private AuxKC admission, transactional activation, guest-only
reboot, exact loaded-identity binding, recovery baseline, bounded PMF-required
association/traffic/rekey gate, AP rollback, and recovery baseline again.

None of the completed source or fixture gates proves PMF-required association,
pure SAE, generic reachability, physical-host behavior, or a release claim.
