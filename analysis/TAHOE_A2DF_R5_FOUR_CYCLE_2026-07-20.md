# Tahoe A2DF R5 four-cycle baseline — 2026-07-20

## Successful, bounded scenario

At `2026-07-20T23:28:00Z` through `2026-07-20T23:34:01Z`, the pinned QEMU
laboratory guest completed the A2DF R5 baseline-control run from source commit
`70e09469152f065b8f4986077906fa6a8a25cc6d`.

All four public Wi-Fi OFF/ON cycles reached an observed radio-Off state,
observed radio-On state, stable AP authorization, and a fresh association
epoch.  Each cycle then sent exactly five permitted pings over the preexisting
Wi-Fi address and its pinned direct lab route: all 20 transmitted packets were
received.  The management/default route and the direct Wi-Fi route were
preserved at every hard invariant check.  All `ipconfig getpacket` stdout
observations were complete; those are retained only as local textual evidence.

The runner used only saved-profile autojoin after radio power restoration.  It
issued no explicit route, address, or state-mutating DHCP command.  Its SSH
transport was pinned to a literal host key, ignored ambient SSH configuration,
and used a per-run mode-0600 known-hosts file.

## Scope and limits

Radio power was intentionally changed on the QEMU guest.  OS-managed
association/DHCP/address/route activity is not claimed absent; the record says
only that the runner issued none of those explicit state-mutating commands and
that the named route/address invariants were observed afterwards.

No kext was installed, loaded, unloaded, activated, or rebooted.  The physical
host and `10.90.10.22` were untouched.  The run is a radio recovery and
bounded lab data-plane baseline, not a candidate-kext, SAE, PMF, general
Internet, or release-functional claim.

The machine-checkable, credential-free result is
`evidence/runtime/tahoe_a2df_r5_70e0946_four_cycle.json`.  Raw AP identity,
station identity, route snapshots, DHCP renderings, and ping text remain
local-only; the record retains only their aggregate result and raw summary
SHA-256.
