# CR-479 CURRENT_NETWORK not-associated status

Date: 2026-07-07

## Scope

This batch closes the public `CURRENT_NETWORK` failure-code mismatch for the
Tahoe Skywalk bridge. It does not claim to solve current-BSS ownership or the
remaining user-visible `networksetup` association-state discrepancy.

## Reference evidence

BootKC anchor:

- `0xffffff80015e6384`
  `AppleBCMWLANCore::getCURRENT_NETWORK(apple80211_scan_result*)`

Static slice:

- `10.7.6.112:~/Projects/ghidra_output/cr479_bootkc_memory_safe_checkpoint_smoke_20260516T1248/09_static_slices/BootKC_memory_safe/01060_ffffff80015e6384___ZN16AppleBCMWLANCore18getCURRENT_NETWORKEP22apple80211_scan_result.asm.txt`

Recovered control flow:

- load BssManager from `core+0x128+0x1538`;
- call `IO80211BssManager::isAssociated()` at `0xffffff8002266710`;
- if not associated, return `0xe0822403`;
- if associated, tail-call the BssManager current-network copier at
  `0xffffff80022669ae`.

## Local mapping

`AirportItlwmSkywalkInterface::getCURRENT_NETWORK(...)` already refuses to
publish a current scan result unless net80211 is in `IEEE80211_S_RUN` with a
current BSS. That negative branch now returns the same Apple status
(`0xe0822403`) instead of generic `kIOReturnError`.

The success branch remains the existing local current-node
`apple80211_scan_result` producer. This change does not synthesize association,
does not bypass WCL/BssManager current-BSS ownership, and does not change the
post-association datapath.

## Runtime verification

Installed build:

- SHA-256:
  `09aba89b257bc626d39982674e9751e8bedc08bdb36a7b114d64acf8974eb3e7`
- UUID: `ADEACE61-12FC-3424-A612-AF7AB1E30FAB`

Post-reboot verification on 2026-07-07:

- loaded UUID matched the installed build;
- manual join to `AIAMlab6235` returned success, `en1` became active with
  `10.77.0.47`, and a 10-packet baseline ping had 0% loss;
- bidirectional 120-second TCP stress plus concurrent 150-packet ping held:
  host-to-guest TCP was about 6.2 Mbps, guest-to-host TCP was about 40 Mbps,
  and ping was 150/150 with 0% loss;
- post-stress 10-packet ping had 0% loss with about 0.84 ms average latency;
- serial tail had no new panic or kernel stack corruption entries in the
  checked tail.

As expected for this narrow status-code layer, `networksetup -getairportnetwork
en1` still reported `You are not associated with an AirPort network.` while the
IP datapath was active.
