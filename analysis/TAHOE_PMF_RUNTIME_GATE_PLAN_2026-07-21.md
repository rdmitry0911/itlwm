# Tahoe IWX PMF/BIP runtime-gate plan

## Persistent layer-selection criterion (2026-07-22)

For every next surface-reduction cycle, select the eligible discrepancy that
affects the most frequently used user-facing function first. A smaller
diagnostic/evidence discrepancy is not a higher priority solely because it is
easy to test. It may run first only if it is a hard prerequisite for the
higher-frequency path or prevents a false claim about that path; record that
dependency in the corresponding `FIX_CANDIDATE`.

Date: 2026-07-21

## Status

The committed BIP/PMF ownership layer (`459ef19`) has passed source contracts,
model tests, a clean Tahoe kext build, BootKC symbol resolution, and a
private-only AuxKC admission.  It has not passed a physical PMF-required PSK
association, group rekey, or traffic run.

That distinction is deliberate.  The existing post-PLTI trace is an IWN
ordered association evaluator.  IWX contributes only three categorical PMF
owner observations:

1. PMF EAPOL RX delivered;
2. q0 command doorbelled;
3. q0 completion observed.

Its generic evaluator correctly returns `BACKEND_UNSUPPORTED` for IWX.  These
three markers alone cannot prove an IGTK publication, active-slot transition,
or BIP rekey lifetime.

## Required new trace surface

Extend the safe-only trace ABI with an IWX-specific PMF/BIP classifier rather
than loosening the existing IWN evaluator.  The new surface must retain all
current privacy constraints:

- fixed categorical event IDs only;
- no key bytes, key hashes, PN/IPN values, status codes, firmware values,
  pointers, MAC/BSSID/SSID, addresses, channel, packet data, or timestamps;
- no allocation, logging, property publication, object retention, or frame
  inspection in a producer;
- one controller-bound capture generation and one sealed episode per verdict;
- dropped/overflow/mixed-generation/mixed-episode observations are always
  inconclusive.

The minimum positive evidence must distinguish both slot categories and their
active transition.  A suitable append-only vocabulary is:

| Categorical fact | Purpose |
| --- | --- |
| IGTK slot 4 published | Initial/rekey value reached normal BIP lifetime. |
| IGTK slot 5 published | The alternate slot reached normal BIP lifetime. |
| slot 4 selected for TX | Active PMF transmitter selection is slot 4. |
| slot 5 selected for TX | Active PMF transmitter selection is slot 5. |

The exact names/IDs must be append-only and versioned with the trace ABI.  The
event sources must be limited to post-success BIP publication under the
selected-BSS lock, after the corresponding firmware acknowledgement is owned.
Do not emit an event for a local/prepared descriptor, a failed backend command,
or a raw table access.  Any generic/BIP fallback path that can publish an IGTK
must use the same narrow helper so the evaluator cannot miss a valid source.

## Required evaluator matrix

Implement a dedicated C-compatible evaluator and C/C++ unit fixtures.  It may
consume the existing sanitized `AirportItlwmPostPltiTraceEntry` ring, but must
only accept the IWX backend and must not modify the IWN success semantics.

Positive fixtures:

1. Initial PMF transaction: PMF RX, q0 submit/completion, one slot publication
   and matching active-slot event, then normal port-valid/episode completion.
2. Slot 4 initial publication followed by slot 5 rekey and active-slot change.
3. Slot 5 initial publication followed by slot 4 rekey and active-slot change.
4. A valid traffic/recovery attestation is represented separately from the
   trace; neither a ping nor trace alone establishes the full verdict.

Negative/inconclusive fixtures at minimum:

- missing q0 completion;
- publication without the correct PMF owner sequence;
- active-slot event before its publication;
- repeated active selection without an intervening rekey;
- same-slot replacement mistaken for cross-slot rekey;
- slot 4/5 publication with a stale/mixed episode or generation;
- event after terminal seal, abort, overflow, or drop;
- cancellation/detach before final publication;
- generic IWN or unknown backend supplied to the IWX evaluator.

The evaluator must report the first categorical missing/invalid stage without
serialising raw trace data into committed evidence.

## Runtime runner requirements

Create a new bounded runner; do not repurpose the SAE profile runner as PMF
proof.  It must:

1. require a pre-existing Keychain/saved profile and never accept a password;
2. use only the pinned QEMU guest and its fixed management transport;
3. capture candidate identity before and after the test;
4. reset, arm, seal, and read only the safe trace properties;
5. require the IWX PMF/BIP evaluator verdict plus bounded traffic success;
6. preserve the default-route signature on `en0`, a direct lab route on `en1`,
   and the expected lab address invariant at every phase;
7. retain raw traces, interface output, and route dumps only locally; commit a
   sanitized hash/count/verdict attestation only;
8. fail closed if the trace is not sealed, has a drop, has a mismatched
   candidate identity, or the AP/route/address guard changes.

The AP procedure must be a separate one-at-a-time hostapd switchover helper:

- validate current AP process/config and channel-width invariants first;
- switch only the lab AP to the staged required-PMF PSK configuration;
- guarantee rollback to the current optional-PMF configuration on every exit;
- never alter host IP, NAT, forwarding, or default routes;
- restore the original AP before reporting a result;
- use a bounded short group-rekey only after the initial PMF test is proven.

## Candidate activation sequence

Once the runner and static/build gates exist, use a **fresh disposable guest
overlay**.  The base image is not a test target.  The only permitted sequence
is:

1. build the clean committed candidate in the pinned guest;
2. private-only AuxKC preflight;
3. transactional `tahoe_auxkc_activate_release.sh` activation;
4. guest-only reboot;
5. `capture_tahoe_lab_kext_identity.py` exact archive/installed/loaded binding;
6. four-cycle A2DF recovery baseline;
7. controlled PMF-required initial association, traffic, and rekey test;
8. AP rollback and another four-cycle A2DF recovery baseline.

Direct kext load/unload, physical-host actions, host reboot, arbitrary join
commands, or a PMF claim based on ordinary WPA2 traffic are out of scope.
