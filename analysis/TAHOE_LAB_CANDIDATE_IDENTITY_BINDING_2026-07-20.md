# Tahoe lab exact-candidate identity gate — 2026-07-20

## Why this gate exists

The committed AP-visibility record proves only that the Tahoe laboratory
guest can perform a directed scan with an already loaded AirportItlwm.
It is intentionally not evidence for the source checkout or for a released
candidate. Crediting a later association result to that unknown loaded kext
would create a false positive.

scripts/capture_tahoe_lab_kext_identity.py is the precondition gate used
immediately before a candidate runtime batch. It accepts a local release
archive and compares its AirportItlwm.kext binary to the installed and
already loaded kext on the pinned Tahoe guest.

## Recorded successful quarantine scenario

At 2026-07-20T21:10:52Z the read-only gate completed against release
v2.4.0-alpha-a4d803c. The pinned guest query, Wi-Fi interface check, installed
bundle check, and installed-to-loaded UUID check all passed. The installed
AirportItlwm UUID was 3CD3F4A6-B781-32C3-A6DE-DC4B59A2D8F1, while the release
archive UUID was 63B2AF32-131A-3CB8-9C8B-A61E0DD04A45. Their binary SHA-256
values also differed.

Therefore the gate correctly produced candidate_kext_bound=false and prevented
an invalid runtime attribution. This is a successful identity-quarantine scenario,
not a failed candidate association: no association was attempted. The sanitized
evidence is evidence/runtime/tahoe_lab_kext_identity_a4d803c.json, whose SHA-256
is 05a6a0e986d4e053bccf53af2086685cdf43bf30efff37de6e18d712658cb697.

## Required equalities for a bound candidate

The gate returns candidate_kext_bound=true only when all of the following
are true:

1. Strict host-key-pinned SSH to the fixed lab guest succeeds.
2. The fixed Wi-Fi interface is present and the installed kext bundle has the
   expected bundle identifier.
3. SHA-256 and Mach-O LC_UUID of the installed kext binary equal those in
   the supplied release archive.
4. AirportItlwm is already reported loaded, and its observed loaded UUID
   equals both the installed and release UUID.

The resulting JSON records archive identity, safe installed/loaded identity
values, booleans for every equality, and named failure reasons. It does not
retain raw SSH output, a local archive path, hardware addresses, SSIDs,
BSSIDs, credentials, packets, or routes.

## Safety and scenario boundary

The capture is read-only: it does not install, load, unload, associate,
authenticate, configure addresses, obtain DHCP, change routes, or reboot.
Its non_claims and candidate_runtime_test_performed=false fields make that
boundary machine-checkable.

A candidate_kext_bound=true result is necessary before a four-epoch SAE/PMF
runtime attestation can be committed, but is still not an association or traffic PASS.
Conversely, candidate_kext_bound=false is a successful quarantine outcome for the
identity check: it prevents an unbound runtime claim and identifies exactly which
equality must be repaired before the scenario is retried.

## Reproducible checks

The executable has a synthetic archive and Mach-O parser self-test. The
adjacent shell contract verifies the host-key pinning, fixed guest identity,
exact archive members, equality predicates, non-retention policy, and absence
of install/load/network/reboot capabilities. The SAE/PMF lab contract runs
this check so the identity precondition cannot silently disappear from a
future large scenario batch.
