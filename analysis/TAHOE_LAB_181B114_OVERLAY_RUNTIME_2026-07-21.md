# Tahoe 181b114 overlay runtime gates — 2026-07-21

## Successful, bounded scenarios

The published `v2.4.0-alpha-181b114` archive was materialized privately,
admitted into a private AuxKC, then installed and rebooted only in a powered-off
QEMU external qcow2 overlay. Both pre- and post-materialization collection
checks had the exact approved five-member set. The post-boot read-only identity
capture proved that the archive, installed bundle, and loaded AirportItlwm
Mach-O have the same SHA-256 and UUID. This is a real loaded-candidate result,
not an inference from a build directory.

The overlay guest then completed the strict A2DF baseline-control scenario:

- four observed radio OFF/ON cycles;
- four stable re-associations and authorization observations;
- five bounded laboratory data-plane packets in each cycle, all received;
- preserved management/default and pinned direct Wi-Fi route invariants; and
- complete read-only DHCP textual observations.

The runner used saved-profile autojoin only. It issued no explicit address,
route, or state-mutating DHCP command. Raw station/AP identities, routes, DHCP
renderings, and packet output remain local-only.

## LinkContext result: useful negative observation, not a functional claim

An exact-source RegDiag client was built privately from the three required
`181b114` source inputs, then enabled with mode `0x51` and block mask `0x0`.
An initial four-cycle trace correctly failed closed because its 128-entry ring
overflowed. The runner now supports a one-to-four-cycle bounded trace window;
four remains the default and only four cycles produce the A2DF four-cycle
verdict. Its static contract prevents a one-cycle window from being reported as
the baseline.

The one-cycle window completed its own bounded data-plane and invariant checks
and yielded a trace with `dropped=0`. The owner-context evaluator returned
`OWNER_CONTEXT_GATE_HELD`; the parent evaluator returned
`TAHOE_PARENT_LINK_UP_NOT_OBSERVED`. These are intentionally retained as safe
negative observations, not normalized into a pass. The trace shows the
existing event-source route reaches the workloop with its gate held, so the
existing precondition guard correctly prevents publication before the parent
link-state path. No direct parent call, retry, gate bypass, forged trace, or
alternate publication route was introduced.

The diagnostic mode was then disabled. Its remaining control-only bit is inert
because the global enabled bit is clear; no association/data/context/intervention
mode remained enabled.

## Scope and limits

The base disk was used only as the backing file of the external overlay. The
physical host was not rebooted, and the remote physical validation host was not
touched. This record does not claim general Internet traffic, pure SAE/PMF
functionality, parent link-up acceptance, or completion of the off-gate
publication discrepancy.

Machine-checkable evidence is
`evidence/runtime/tahoe_lab_kext_identity_181b114.json` and
`evidence/runtime/tahoe_lab_181b114_overlay_runtime.json`. They retain only
aggregate verdicts and hashes of local raw artifacts; no credentials, SSIDs,
BSSIDs, addresses, routes, packets, or DHCP text are committed.
