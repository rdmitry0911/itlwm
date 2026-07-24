# Tahoe disposable-overlay preparation protocol

## Purpose and scope

`tahoe_prepare_disposable_overlay.sh` prepares the host-side storage boundary
for a future Tahoe candidate experiment. It creates one new qcow2 overlay over
one direct backing image, treats that base as read-only, and writes a
local-only sanitized attestation. With the explicit OVMF variables option, it
also creates one private variables copy that is paired with that overlay. It
does not boot QEMU or reboot a guest. It does not activate a candidate. It also
does not alter an AuxKC, control an AP, or change host networking.

The helper is intentionally not a runtime runner and its `PASS` receipt is not
candidate, association, PMF/BIP, traffic, or physical-host evidence.

## Required inputs

The caller supplies three required explicit values:

1. an absolute qcow2 root image with no backing image;
2. the absolute pinned VM root already used by the local Tahoe launcher; and
3. a fresh single-component output-directory name below that VM root.

The caller can additionally supply an absolute `--ovmf-vars-template` for the
future OVMF variables store. This option is opt-in: when it is absent, the
helper retains the exact v1 disk-only receipt and produces no OVMF variables
file. When it is present, the template must be a nonempty regular file, not a
symlink, and not open by another process. Duplicate input flags are rejected.

The helper refuses a symlinked base, a base with an existing backing chain, an
existing output directory, or a base currently open by another process. It
creates the result in a restricted staging directory and renames that directory
only after checking that the new top layer has one direct backing image and no
top-level guest-data allocation. A failed preparation removes only its own
staging directory; it never removes a caller path or a base image.

The disk-only published directory contains `tahoe-pmf-runtime.qcow2` and
`overlay-attestation.json`. The v1 attestation contains categorical storage
facts and metadata digests only. Image paths, wireless identities, credentials,
addresses, routes, packets, and raw QEMU output stay local-only.

When the OVMF option is present, the published directory is a disposable pair:
the qcow2 overlay plus `OVMF_VARS-1920x1080.fd`. The helper makes that file in
its private staging directory as a mode-0600, distinct inode copy, never a
link to the template. It rejects a template already in use, hashes the opened
template before and after copying, hashes the new copy, and repeats the stable
template/copy hash check immediately before atomic publication. The v2
attestation records only the fixed file name, categorical copy facts, the two
matching SHA-256 values, and the positive copy size; it retains no template or
image path.

The pair is disposable rather than a recovery envelope. If a later experiment
fails, discard the pair and prepare a new overlay and, when needed, a new OVMF
variables copy from the last known working baseline. This helper does not
construct a second VM, a fallback candidate, or a recovery boot path.

Validate the receipt before the later guest sequence with
`test_tahoe_disposable_overlay_evidence_contract.sh --attestation` against its
local path. The validator reads only the aggregate; it does not inspect either
image or start a VM.

## Launcher boundary

The existing Tahoe launcher selects only a disk through `ITLWM_DISK` below its
pinned VM root. That path can consume a v1 disk-only overlay, but it must not
be used for a v2 pair: it hard-wires the shared OVMF variables file and would
defeat the pair boundary. A v2 pair requires the later dedicated launcher to
bind the pair-local variables file explicitly. Starting either kind of guest
remains a separate, later action.

For a v2 pair, the receipt additionally names `ITLWM_OVMF_VARS` as the future
variables selector and binds it to the fixed file name and SHA-256 before a
first boot. This helper neither exports that variable nor starts QEMU; it only
prepares and attests the local storage boundary. A later launcher integration
must verify the recorded digest before it consumes the selector.

`tahoe_launch_disposable_pair.sh` is that v2-only later launcher. It accepts
the prepared pair, its pinned VM root, an explicit QEMU binary and OVMF code
file, and exactly one bounded VFIO device address. `--check-only` validates the
v2 receipt, the pair-local variables file mode and digest, the fresh direct
qcow2 relation, and the absence of a prior consumption marker. It does not
start QEMU or a user service. A successful `--launch` repeats those checks,
refuses an existing QEMU or fixed local management-port conflict, atomically
marks the pair consumed, and submits one deterministic pair-derived transient
systemd user unit with pair-local monitor, serial, disk, and private variables
paths.
It is not a recovery envelope: it does not use the old shared-variables
launcher, does not stop an existing guest, does not construct a second VM, and
does not provide a recovery boot path. A failed, exited, or
panicking attempt remains a consumed pair: discard it and prepare one new pair
from the last known working base.

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
