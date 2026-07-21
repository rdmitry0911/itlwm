# Tahoe bb7366b overlay runtime gates — 2026-07-21

## Successful, recorded scenarios

Published prerelease `v2.4.0-alpha-bb7366b` was downloaded as a Tahoe kext
archive, structurally verified, and activated only in a fresh external QEMU
qcow2 overlay. The base disk remained a read-only backing file. Private AuxKC
admission passed without canonical mutation; transactional activation accepted
the exact approved five-member collection, and only the guest was rebooted.
The subsequent read-only identity capture bound the release archive, installed
bundle, and loaded Mach-O to the same SHA-256 and UUID.

The successful public-radio recovery scenario is also recorded: one OFF/ON
recovery reached the saved-profile address-ready state without an explicit join,
address, route, or state-mutating DHCP command.

The candidate then completed the strict A2DF baseline-control scenario:

- four observed radio OFF/ON cycles;
- four stable authorization and fresh re-association observations;
- five bounded data-plane packets in each cycle, all received;
- preserved management/default and direct Wi-Fi-route invariants; and
- complete read-only DHCP textual observations.

The runner used saved-profile autojoin only. Its raw station, access-point,
address, route, DHCP, and packet observations remain local-only.

## Corrected App Store topology premise

The Tahoe-only `IOBuiltin=false` controller mitigation was already in this
candidate. A read-only aggregate IORegistry probe confirms its intended narrow
scope: one controller has `IOBuiltin=false` and the mirrored BSD Name. It also
corrects an earlier overstatement: that controller has a six-byte MAC, whereas
its direct wrapper/provider does not; one AirportItlwm Skywalk interface also
has a six-byte MAC.

This is a topology/property result, not an App Store functional result. The
scenario did not launch App Store, log in, purchase, alter Keychain state, or
observe background agents over time. It also uses a different guest build from
the reported crash, so it neither reproduces nor claims to fix that crash.

## LinkContext bridge correction: runtime observation

An exact-source RegDiag client built privately from `bb7366b` enabled passive
LinkContext with mode `0x51` and block mask `0x0`. Its one-cycle bounded window
passed the data-plane, invariant, and complete read-only DHCP checks. The fresh
trace had 38 records and no dropped entries.

The corrected net80211 bridge recorded two bridge events, both with a nonzero
association epoch and successful result. This confirms the argument-order
correction is observable at runtime, rather than being only a static build
claim. It does not publish link state or bypass any gate.

The owner-context evaluator correctly returned `OWNER_CONTEXT_GATE_HELD`; the
parent evaluator returned `TAHOE_PARENT_LINK_UP_NOT_OBSERVED`. These are safe
negative results: the existing event-source route reaches the workloop while
its gate is held, so no parent publication was synthesized or claimed. The
diagnostic was then disabled; its global enabled bit is clear, leaving no
association, data, context, or intervention mode active.

## Scope and limits

The physical host was not rebooted and the remote physical validation host was
not touched. This record does not claim pure SAE/PMF functionality, general
Internet traffic, parent link-up acceptance, or resolution of the off-gate
publication discrepancy.

Machine-checkable, sanitized evidence is
`evidence/runtime/tahoe_lab_kext_identity_bb7366b.json` and
`evidence/runtime/tahoe_lab_bb7366b_overlay_runtime.json`. They include only
aggregate verdicts and hashes; credentials, wireless identities, addresses,
routes, packets, and DHCP renderings are not versioned.
