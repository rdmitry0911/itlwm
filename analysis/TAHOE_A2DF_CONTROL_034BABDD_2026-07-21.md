# Tahoe A2DF control baseline — 2026-07-21

## Exact control candidate and bounded result

The pinned disposable Tahoe QEMU guest ran the saved control candidate from
source commit `5e2f70a52eaa53b6c2633e95af76452dfe3e3774`:

- AirportItlwm version: `2.4.0`;
- Mach-O UUID: `034BABDD-BFDF-316F-B547-122C74246607`;
- candidate SHA-256:
  `d69bd5f028f46c8a76968d30d0a7a157f094136953a77dd9edc4a12027cbed6e`.

At `2026-07-21T19:11:50Z`, the lab runner completed its four bounded public
radio OFF/ON cycles.  Every cycle observed radio Off and On, a fresh AP
association epoch, stable authorization, and five successful permitted pings
over the pre-existing Wi-Fi address and direct lab route.  Aggregate result:
20/20 packets received and `four_cycle_result=PASS`.

The guest's management default route remained on `en0`; the Wi-Fi interface
was used only for the pre-existing direct route to the lab gateway.  The
runner issued no explicit join, address, route, or state-mutating DHCP
command.  It also finished with the radio observably On.

## Scope boundary

This is a restored-control A2DF regression baseline only.  It does not prove
the pending BIP/IGTK lifetime fix, PMF-required association, IGTK
installation, pure SAE, an Internet path, or behavior on the physical
`10.90.10.22` host.  No kext was installed, loaded, unloaded, activated, or
rebooted for this run.

The credential-free raw evidence remains local-only at
`/home/dima/Projects/aiam/runtime-captures/a2df-baseline-034babdd-20260721T191150Z/`.
Its `summary.log` SHA-256 is
`92ceb521bcb39813043a27e6d59a78c098a139a45dd356d802d6817d43fc1688`.
The committed record deliberately contains neither the raw capture nor
wireless identities.
