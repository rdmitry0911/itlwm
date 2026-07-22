# Tahoe pure-SAE product discovery and relay foundation

Date: 2026-07-20

## Scope

This layer moves only the product prerequisites that can be made truthful
without permitting a WPA3/SAE connection:

1. net80211 recognizes RSN AKM suite type 8 as SAE and classifies it as a
   SHA-256 KDF AKM for the future four-way handshake owner;
2. beacon/probe-response scan input normalizes RSNXE into fixed node facts;
3. a naturally aligned, C/C++-checked product relay ABI reserves the future
   semantic SAE records without exposing a UserClient selector yet.

The layer is deliberately not an authentication, PMF, IGTK, or association
implementation. It is safe to merge only as a quarantine-preserving product
foundation.

## RSN and RSNXE facts

`IEEE80211_AKM_SAE` is an internal bit for the standard RSN OUI suite type 8.
It is parsed only when the RSN OUI is present; WPA1 vendor selectors remain
unable to claim SAE. The active device configuration remains
`IEEE80211_AKM_PSK`, and the existing raw ioctl carrier has no SAE option.
Consequently scan discovery cannot make a candidate selectable or make an
association request advertise SAE.

RSNXE is EID 244, not an EID-255 extension. The scan parser retains no raw
RSNXE bytes. `ni_sae_scan_flags` contains only:

- `RSNXE_PRESENT`;
- `H2E` when RSNXE bit 5 is set;
- `UNMODELED` for any non-length/non-H2E capability bit or later nonzero byte;
- `MALFORMED` for zero/oversized/mismatched capability-field length, duplicate
  RSN/RSNXE, a rejected RSN parser result, or a truncated beacon/probe IE
  stream;
- `AKM_AMBIGUOUS` when a profile contains SAE plus a second, duplicate, or
  unknown AKM suite.

The length is strict: low nibble plus one must equal the entire RSNXE payload.
That is intentionally more conservative than accepting a partially modeled
extension. A future pure-SAE admission predicate must reject both `MALFORMED`
and `UNMODELED`, as well as `AKM_AMBIGUOUS`; absent H2E is a valid HnP fact,
not an implicit downgrade. The RSN parser also keeps a count of known and
unknown AKM suites while parsing, so an SAE bitset cannot erase an SAE-PK or
unknown companion suite before that predicate is made.

The beacon/probe scanner also treats a dangling one-byte IE header as
malformed and validates that an EID-255 Extension IE has at least the required
Extension-ID payload byte before reading it. The same zero-length Extension-IE
guard exists in association-response parsing. These are input safety repairs;
they do not enable SAE or change association selection.

## Relay ABI boundary

`include/ClientKit/AirportItlwmSaeRelayV1.h` fixes the layouts for a selected
BSS target, a kext-validated peer event, a semantic Agent reply, a verified
completion, and an abort. Every non-wait record binds controller nonce,
per-UserClient cookie, generation, association epoch, and event sequence. The
exact BSSID and station MAC are repeated in every record, so a reply is not
implicitly bound to a stale target. `rsnxe_capabilities` is a canonical
product bitset (`H2E = bit 0`), not raw RSNXE bit positions; the future kext
owner maps only a fully modeled scan profile into it. The ABI has no packed
records and no password, PWE, KCK, raw RSNXE, or generic frame-injection field.
`AirportItlwmSaeCompletionV1` reserves fixed PMK/PMKID completion fields for a
future verified-Confirm handoff, but this declaration creates no live PMK
ingress or selector.

Within the inactive relay model, a validated peer Commit or Confirm is not
available to an Agent merely because its bound event sequence exists. The
model must first copy that event through `TakeEvent()`; pending peer input
blocks the subsequent Agent reply or completion. This is a delivery-order
fence only and does not enable a selector, cryptographic handoff, or SAE
association.

This header does **not** change the live PLTI table. Existing selectors 0
(`DeliverPMK`) and 1 (`WaitAssociationTarget`) remain the v1 PSK carrier;
selectors 2–6 are declarations only until an Algorithm-3 state machine can
own lifecycle cancellation and TX-completion fences.

## Explicit runtime non-claims

The following conditions intentionally remain unchanged:

- Tahoe association ingress rejects pure WPA3/SAE before PSK/Keychain PMK
  lookup; the audited `SAE|WPA2-PSK` transition path remains WPA2 fallback;
- net80211 accepts and emits only Open-System authentication, not Algorithm 3;
- `IEEE80211_C_MFP` is clear, MFP callbacks remain null, and
  `iwx_set_key_wait()` returns `EOPNOTSUPP`;
- the candidate was built only in an isolated Tahoe guest; it was not copied
  to an EFI or host, installed, loaded, released, or exercised on
  `10.90.10.22`, and no reboot occurred.

The MFP/IGTK quarantine is necessary. Current PAE Msg3 sends Msg4 before all
key installations are complete, software BIP state can change before firmware
acknowledgement, and no PAE-owned transaction/completion owner exists yet.
The separate association-epoch fence now invalidates stale work at BSS
replacement, teardown, and same-BSSID reassociation, but does not by itself
make the API-68 IGTK command path safe to enable.

## One-pass verification

The tracked candidate gates passed:

1. `scripts/test_tahoe_sae_product_foundation_contract.sh` for passive
   RSN/RSNXE facts, ABI layout, and inactive selectors;
2. `scripts/test_net80211_pae_epoch_contract.sh` for every fenced lifecycle
   boundary;
3. `scripts/test_tahoe_sae_quarantine_contract.sh` and
   `scripts/run_tahoe_sae_quarantine_layer.sh --static-only` for the existing
   WPA3, PMK, MFP, and Tahoe build-admission boundaries;
4. C and C++ syntax-only inclusion of `AirportItlwmSaeRelayV1.h`.

An isolated Tahoe guest build also passed with all BootKC symbol references
resolved. This is source/build evidence only, not evidence of a working
pure-SAE association, PMF key lifecycle, traffic, stability, installation,
or a release.

## Required next layers

1. Add a kext-owned Algorithm-3 FSM and exact selected-BSS admission
   predicate. It must validate the normalized RSN/RSNXE/PMF facts, require one
   known SAE AKM, and reject `MALFORMED`, `UNMODELED`, and
   `AKM_AMBIGUOUS` profiles plus unsupported SAE-PK/password-id/extension
   profiles.
2. Wire the semantic relay under the controller command gate, with one
   UserClient owner, nonce/cookie/epoch/BSSID/STA/event binding, and a matching
   TX-completion fence before every phase transition.
3. Replace the MFP quarantine with a PAE-owned, epoch-tagged transaction and
   completion path: key installation/removal, Msg4/GroupMsg2, port validity,
   link-up, deauth, same-BSSID reassociation, q0 reuse, reset, stop, and
   detach must have one terminal commit/cancel owner.
4. Only after those layers have local and bounded lab evidence may the
   WPA3 ingress guard and PMF capability be reconsidered.
