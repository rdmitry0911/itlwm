# Tahoe AX211/API-68 PMF transaction-owner evidence

Date: 2026-07-21

Scope: audited PSK+PMF association plumbing on the AX211/API-68 path. This
record is a source/build and deterministic-contract result; it is not a claim
that pure SAE, arbitrary WPA3, or a physical AP association has passed.

## Candidate gates

- Firmware/device gate: the two supported AX211 API-68 configurations only,
  exact IGTK-v2 ABI prerequisites, q0, PMF-owner, task-gate, task queue, and
  selected-BSS lock all present.
- Request gate: WCL publishes PMF only for an exact audited PSK PMK carrier
  with its PMF capability bit. Public associate, WCL leave, disassociate, and
  terminal current-BSS PMF failure clear the request. Pure SAE stays rejected
  before this gate.
- Transaction gate: PTK -> GTK -> IGTK firmware acknowledgements complete
  before Msg4, protection flags, port-valid, or link-up. A group rekey may
  omit IGTK only with a live existing BIP key.

## Executed guest checks

The Tahoe guest executed these commands against the staged source tree:

```text
./scripts/build_tahoe.sh
bash scripts/test_tahoe_ax211_api68_pmf_transaction_owner_contract.sh
bash scripts/test_iwx_q0_serialization_contract.sh
bash scripts/test_net80211_pae_epoch_contract.sh
bash scripts/test_tahoe_sae_quarantine_contract.sh
```

Results:

- Tahoe Debug `AirportItlwm.kext` build: PASS.
- PMF transaction-owner static and deterministic matrix: PASS.
- q0 serialization and association-epoch contracts: PASS.
- Aggregate SAE/PMF admission suite: PASS.

The build also confirmed no `_thread_call_cancel_wait` dependency. BootKC
symbol verification was skipped because the guest did not have the configured
BootKernelExtensions collection path; this is explicitly not treated as a
runtime-load verdict.

## Deterministic failure matrix covered

- Initial PMF transaction success: PTK, GTK, IGTK, then Msg4 and port/link
  publication.
- Group rekey with optional IGTK, including rejection when no prior live BIP
  IGTK exists.
- q0 full/send failure, ADD_STA NACK, failed-header bit, short response,
  opcode mismatch, and unknown direct response.
- Timeout, late acknowledgement, duplicate acknowledgement, and q0 token
  release without a stale PAE continuation.
- Cancellation during every stage: deauth, disassociate, roam, BSS
  replacement, reassociation, stop, and detach.
- Stale RX epoch/completion and the rollback path after software BIP setup
  fails following firmware key acknowledgement.

No SSID, BSSID, MAC address, IP address, PMK, passphrase, or routing change
is included in this record. A physical PMF-required PSK association and data
plane run remain the next separate laboratory evidence step.
