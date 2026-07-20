# Tahoe SAE/PMF laboratory scenario matrix

## Scope

This is a versioned test contract for the current Tahoe SAE/PMF quarantine.
It does not claim that SAE, Algorithm 3 authentication, PMF, IGTK, or a pure
SAE association is implemented.  The pure-SAE scenario below has a successful
**quarantine** verdict only when the driver rejects the carrier before any
PMK, PLTI, EAPOL, or link-up activity.

`scripts/run_tahoe_sae_lab_profiles.sh` runs one four-epoch batch.  Each epoch
uses `capture_tahoe_sae_layer.sh`, whose `sae-on` control creates a new
diagnostic epoch, and whose raw trace and snapshot remain local-only.  The
batch records a committable attestation containing scenario names, outcome,
route-preservation result, SHA-256 profile identifiers, and SHA-256 hashes of
the capture files.  It never records an SSID, BSSID, password, key material,
or raw capture content.

## Required profile matrix

| Order | Scenario ID | Exact carrier and expected outcome | What a PASS proves |
| --- | --- | --- | --- |
| 1 | `wpa2-psk-baseline` | `auth=0x8`, `policy=0x6`; successful association ingress, direct PMK or matched PLTI handoff, EAPOL TX/RX, and link-up. | The known WPA2-PSK baseline works for this installed candidate and lab profile. |
| 2 | `pure-sae-required-pmf-reject` | Hidden-WCL `auth=0x1000`, `pmf=1`, `policy=0x1`; nonzero hidden association result and no PMK, PLTI, EAPOL, or link-up in the isolated epoch. | The unsupported pure-SAE + required-PMF carrier is fail-closed without contaminating the PSK path. It is not an SAE or PMF success claim. |
| 3 | `sae-transition-psk` | `auth=0x1008`, `policy=0xe`; successful association ingress, PMK/PLTI, EAPOL TX/RX, and link-up. | Only the explicitly audited SAE-transition + WPA2-PSK representation worked. A generic mixed-mode join that emits another carrier is inconclusive, not a transition PASS. |
| 4 | `wpa2-psk-recovery` | The same exact `auth=0x8`, `policy=0x6` success criteria after the pure-SAE attempt. | The pure-SAE rejection did not leave the baseline PSK path unusable. |

The runner requires three pre-existing saved macOS profiles: exact WPA2-PSK,
pure SAE with required PMF, and SAE-transition + PSK.  Credentials must come
only from Keychain/known-network state; there is no password option.

## Evidence and safety rules

The runner invokes no explicit route, address, DHCP, install, load, or reboot
command.  It observes the default route before the batch and after every
epoch; an absent or changed default-route signature makes the scenario and
overall batch `INCONCLUSIVE_OR_FAIL`.  That is an observation gate, not a
claim that macOS performed no transient network work of its own.

The evaluator rejects a trace with an ABI/control mismatch, ring overflow,
counter mismatch, malformed ordering, wrong exact carrier, missing PMF
carrier for pure SAE, or cross-stage contamination.  Its local fixture matrix
contains direct-PMK and PLTI WPA2 success, exact transition success, pure-SAE
quarantine success, and negative cases for trace overflow, wrong transition
auth/policy, missing pure-SAE PMF, PMK contamination, EAPOL contamination, and
link-up contamination.

Only a sanitized attestation may be copied into a committed test record, and
only after all four scenarios report `PASS`, the candidate kext identity has
been independently bound to the source/release being tested, and the recorded
default-route gate passed.  Raw `trace.txt`, snapshots, reports, route dumps,
and interface dumps are not committable evidence because they can contain
identifiers or pointers.  A scan-only readiness result is likewise a
precondition, never a candidate functional verdict.
