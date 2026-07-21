# Tahoe SAE Algorithm-3 taxonomy foundation

This foundation introduces one self-contained classifier for the fixed
Authentication fields defined by SAE: Algorithm 3 and the Commit and Confirm
transaction values.  Its input is already-decoded host-order integers.  The
classifier has no production caller; it cannot send, receive, parse, store,
or authorize an SAE exchange.

Run the checked scenario with:

```sh
bash scripts/test_tahoe_sae_product_foundation_contract.sh
```

The gate compiles the taxonomy unit test as both C11 and C++14, verifies the
valid Commit and Confirm classifications, rejects non-SAE and unclassified
transaction values, and confirms the active net80211 path remains
Open-System-only.  It also reruns the existing discovery, selected-BSS,
relay-ABI, and quarantine contracts.

## Scope boundary

This is intentionally a taxonomy, not an SAE state machine.  SAE permits
retries, anti-clogging tokens, group negotiation, and exchange details which
cannot safely be reduced to a strict Commit-to-Confirm ordering predicate.
The classifier therefore interprets neither status nor body data and accepts
no pointer, credential, group, password, PWE, key, epoch, or authorization
input.

No production RX/TX path includes or invokes this classifier.  The active
sender still constructs Open-System Authentication, the receiver still
rejects non-Open-System Authentication, the active RSN configuration remains
PSK-only, the UserClient exposes no SAE selector, and the Agent has no SAE
credential implementation.

## Successful static scenario and non-claims

A PASS records only the C/C++ unit and source-contract scenario described
above.  It contains no wireless identity, credential, address, route, packet,
or raw trace.  It is not evidence of pure SAE, WPA3 association, PMF, IGTK,
EAPOL, link publication, AX211 execution, or physical-host behavior.  Those
claims remain blocked until a complete algorithm, semantic relay, PMF/IGTK
owner, and controlled AX211 runtime experiment are implemented and verified.
