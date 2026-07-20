# Tahoe lab bound-candidate profile and SHA256-PSK baseline — 2026-07-20

## Purpose and candidate identity

This record preserves one successful prerequisite scenario and one bounded
runtime diagnostic scenario from the Tahoe laboratory.  It is deliberately
not a general Wi-Fi success claim.  The complete sanitized record is
`evidence/runtime/tahoe_lab_ff7b960_profile_and_sha256_baseline.json`, whose
SHA-256 is
`4cf626dde388124957f34d55e93c92bf332a22ddf5611faa715ac88d68aa4d59`.

Before either scenario, the read-only pinned identity gate bound release
`v2.4.0-alpha-ff7b960` to both the installed and already loaded AirportItlwm:
binary SHA-256 `8cc606410deb27d57ce9a0144eb894b5375c60563ff58deb69b4a7a31723d21a`
and LC_UUID `25D5548C-5DF3-3383-9544-967CD3AE0C6F` matched in all three
places (release archive, installed bundle, and loaded kext).  This is an
actual successful exact-candidate identity scenario.  Its separately
machine-checked evidence is
`evidence/runtime/tahoe_lab_kext_identity_ff7b960.json`, SHA-256
`7d3c7bf3389a2930f88d588b9333304450bb3c119e7758abbc9a0a82324a9552`.

## Saved-profile preflight: successful safety boundary, incomplete matrix

At 2026-07-20T21:32:06Z, the new `--preflight` mode observed the default
route and consulted only the macOS saved-profile list.  It did not request a
credential or call the join trigger.  The WPA2 profile hash was present;
the pure-SAE and SAE-transition profile hashes were absent.  The resulting
`PROFILE_READINESS_INCOMPLETE` is therefore a correct environment-precondition
result, not a failed association and not evidence that a stored credential is
missing.

The successful part of this scenario is the safety contract: profile names
were SHA-256-redacted, no raw network identifier or credential was committed,
and no route, address, DHCP, install, load, or reboot command was issued.
The runner's committed script hash is
`3a49252d9d0ece160e4de92f0ff3fd9b650229befe337ba47f068cc25d83c776`.

## SHA256-PSK diagnostic baseline: carrier accepted, handshake incomplete

At 2026-07-20T21:32:30Z, the same exact bound candidate received a saved
profile with the precise Tahoe SHA256-PSK carrier `auth=0x400`, `policy=0x6`,
and `pmf=0`.  This is distinct from the exact `wpa2-psk` carrier `auth=0x8`;
the latter expectation must not be broadened to hide an encoding difference.

The ten-record, non-overflowing RegDiag epoch showed two accepted auth-policy
events, two accepted PLTI publications, and two accepted PLTI deliveries.
The association ingress returned status zero and the route-preservation guard
passed.  Thus the candidate reached and accepted the tested carrier, and the
project PMK delivery path was not absent.

The strict `wpa2-sha256-psk` evaluator correctly still returned
`INCONCLUSIVE_OR_FAIL`: it observed no successful EAPOL TX/RX pair and no successful link-up publication.
A join-abort was observed.  This record does
not claim completed association, authentication, EAPOL exchange, link-up,
traffic, WPA2 baseline success, pure SAE, or PMF support.  The evaluator hash
is `63cc6b68ab96e0cbf4f390d23c419b08a2888f7cf4c02d12c82c46e0a85067a8`.

## Consequence for the next layer

The next diagnostic layer starts downstream of accepted PMK/PLTI delivery:
it must explain why the state progression produces join-abort without the
first successful EAPOL exchange or link-up.  It must not relabel this result
as a Keychain failure, a generic WPA3 failure, or a functional PSK success.
Raw traces, reports, route/interface snapshots, SSIDs, BSSIDs, and key
material remain private and are not part of this commit.
