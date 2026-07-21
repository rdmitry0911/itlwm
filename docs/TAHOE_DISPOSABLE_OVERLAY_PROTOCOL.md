# Tahoe disposable-overlay preparation protocol

## Purpose and scope

`tahoe_prepare_disposable_overlay.sh` prepares the host-side storage boundary
for a future Tahoe candidate experiment. It creates one new qcow2 overlay over
one direct backing image, treats that base as read-only, and writes a
local-only sanitized attestation. It does not boot QEMU or reboot a guest. It
does not activate a candidate. It also does not alter an AuxKC, control an AP,
or change host networking.

The helper is intentionally not a runtime runner and its `PASS` receipt is not
candidate, association, PMF/BIP, traffic, or physical-host evidence.

## Required inputs

The caller supplies three explicit values:

1. an absolute qcow2 root image with no backing image;
2. the absolute pinned VM root already used by the local Tahoe launcher; and
3. a fresh single-component output-directory name below that VM root.

The helper refuses a symlinked base, a base with an existing backing chain, an
existing output directory, or a base currently open by another process. It
creates the result in a restricted staging directory and renames that directory
only after checking that the new top layer has one direct backing image and no
top-level guest-data allocation. A failed preparation removes only its own
staging directory; it never removes a caller path or a base image.

The published directory contains `tahoe-pmf-runtime.qcow2` and
`overlay-attestation.json`. The attestation contains categorical storage
facts and metadata digests only. Image paths, wireless identities, credentials,
addresses, routes, packets, and raw QEMU output stay local-only.

Validate the receipt before the later guest sequence with
`test_tahoe_disposable_overlay_evidence_contract.sh --attestation` against its
local path. The validator reads only the aggregate; it does not inspect either
image or start a VM.

## Launcher boundary

The existing Tahoe launcher already selects a disk through `ITLWM_DISK` below
its pinned VM root. Use the newly created relative disk path with that existing
launcher; do not edit the launcher, replace the base image, or attach the
overlay to any other VM. Starting the guest is a separate, later action.

Before a guest boot, retain the fresh attestation and ensure the directory has
not been reused. After a guest uses the overlay, its top-level data allocation
is expected to change; the preparation receipt still records the pre-boot
freshness boundary and must not be treated as a post-runtime disk-integrity
claim.

## PMF/BIP sequence boundary

Preparing an overlay does not lift any PMF/BIP gate. The candidate sequence
remains: clean source/build gate, private AuxKC admission, transactional
activation, guest-only reboot, exact loaded-identity binding, A2DF control,
then the bounded PMF-required experiment and recovery check.

The activation helper arms its rollback before the first canonical move and
attempts to restore the collection before the bundle on ordinary failed exit,
HUP, INT, or TERM. A forced untrappable process kill is not evidence of a
rollback and remains a failed experiment boundary.

The controlled AP preflight remains a hard precondition. At this checkpoint it
reports a categorical identity mismatch, so no candidate activation, guest
boot for this experiment, or PMF-required AP transition is authorized. The
overlay helper exists to make the later approved sequence reproducible without
guessing storage topology; it must not be invoked as a way to bypass the AP
preflight.
