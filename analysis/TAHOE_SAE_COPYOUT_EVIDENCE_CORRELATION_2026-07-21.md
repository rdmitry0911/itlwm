# Tahoe SAE selected-BSS copyout and evidence-correlation test record

Date: 2026-07-21

## Scope

This commit records two intentionally non-functional SAE/PMF safety changes:

1. invalidating a selected-BSS epoch now scrubs its fixed BSSID, SSID, and
   scan-fact payload after publishing `epoch = 0`;
2. the capture evaluator now binds a PSK or audited transition verdict to one
   exact `auth-policy` window rather than allowing it to borrow PMK, EAPOL, or
   link events from another policy carrier.

Neither change enables pure SAE, Algorithm 3 authentication, PMF, IGTK, a new
UserClient selector, PMK ingress, an RSN output AKM, or a link-state path.
There is deliberately no production consumer of
`ieee80211_pae_selected_bss_copyout_current()` in this commit.  That API is a
fixed-value handoff foundation only: its future caller must already hold the
HAL/lifecycle claim that keeps `ieee80211com` and the leaf lock alive, and it
may not race terminal lock destruction.

## Kernel-space scenarios verified

The host unit and source-contract run records the following successful
synthetic scenarios:

- exact current-epoch copyout returns only the post-copy fixed identity,
  normalized scan flags, and strict-profile fact;
- a stale expected epoch returns a zeroed output value;
- an active replacement token returns a zeroed output value;
- cancellation/invalidation scrubs the source payload and makes the old epoch
  unreachable; and
- the fixed record layout contains no raw IE, credential, key, or pointer
  field, and copyout has no production caller.

The copyout implementation takes the leaf lock, checks STA mode, current
association epoch, completed replacement token, and published snapshot epoch,
then uses the existing fixed-field population helper and publishes the output
epoch last.  It rejects the live record as an output alias before clearing an
output buffer.  It performs no allocation, callback, gate entry, I/O, selector
dispatch, credential action, authentication transmission, or link action.

## Evidence-tool scenarios verified

The evaluator self-test now has positive fixtures for `wpa2-direct`,
`wpa2-plti`, `wpa2-plti-before-ingress`, `wpa2-sha256-plti`, `transition`, and the
`pure-sae-required-pmf-reject` quarantine verdict.  The latter is only a
successful fail-closed quarantine test, not a functional pure-SAE association.

It also has negative fixtures proving that a candidate cannot borrow progress
from a previous or later policy carrier:

- `wpa2-prior-policy-progress`;
- `wpa2-next-policy-progress`;
- `transition-next-policy-progress`;
- `wpa2-policy-result`;
- `wpa2-mismatched-direct-pmk-auth`; and
- `wpa2-mismatched-plti-auth`.

For positive PSK/transition evidence, a successful exact policy carrier starts
the window.  Successful ingress on the same path and PMK or matched PLTI with
the same auth mask must both occur in that window; their relative order is not
assumed because WCL may deliver PLTI before its outer ingress return.  EAPOL
TX/RX must follow the exact PMK completion, then link-up must follow EAPOL.  A
later `auth-policy` record terminates the window.  The pure-SAE quarantine
remains stricter: any PMK/PLTI, EAPOL, or link activity anywhere in its
isolated epoch invalidates the rejection verdict.

## Verification boundary

The committed checks are:

1. `scripts/test_tahoe_sae_product_foundation_contract.sh` — host C layout and
   scrub unit assertions plus inactive product constraints;
2. `scripts/test_net80211_pae_epoch_contract.sh` — source ownership, lifecycle
   fencing, zeroed-copyout model, and no-production-caller contract;
3. `scripts/test_tahoe_sae_quarantine_contract.sh` — aggregate WPA3/PMK/MFP
   quarantine and evaluator fixtures; and
4. an isolated Tahoe guest build-admission run — source/build evidence only.

For this staged batch, all four checks passed.  The isolated guest built the
kext, Agent, and RegDiag client and resolved its BootKC symbol set without
installing, loading, publishing, or releasing a bundle.

These are reproducible static, unit, fixture, and build results.  They do not
claim a working pure-SAE connection, PMF key lifecycle, association, traffic,
installation, release activation, or test on a physical host.  Raw captures,
profile identifiers, BSSIDs, SSIDs, routes, and credentials remain local-only.
