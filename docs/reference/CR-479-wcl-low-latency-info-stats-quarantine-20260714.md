# CR-479: WCL low-latency stats false-success quarantine

Date: 2026-07-14

## Scope

This correction covers only Tahoe V2/Skywalk slot `[534]`,
`getWCL_LOW_LATENCY_INFO_STATS`. It removes a fabricated telemetry snapshot;
it does not implement Apple's hidden low-latency or traffic-monitor owners,
and does not change the separate `getWCL_LOW_LATENCY_INFO` configuration
carrier.

## Recovered reference contract

The reference is the 25C56 x86_64 slice of
`com.apple.DriverKit-AppleBCMWLAN`:

```text
path: /System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN
SHA-256: 4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab
UUID: 149C0AD1-A92F-35BC-AA69-5C8815C5421E
```

Its Infra slot-[534] wrapper at `0x100017d60` tail-dispatches to
`AppleBCMWLANCore::getWCL_LOW_LATENCY_INFO_STATS` at `0x100141f06`.
The Core body has this visible boundary:

- `NULL -> 0xe00002bc`;
- for a valid carrier, it reads the low-latency/traffic owner at Core state
  `+0x2c18` through virtual offset `+0x288`, and writes four resulting dwords
  starting at caller `+0x10`;
- it also copies current Core state from `+0x4798`, aggregates an indexed
  `+0x76c4` region, and copies fields at `+0x7500/+0x7504`,
  `+0x77b8/+0x77bc/+0x78c0`, and `+0x78fc` through `+0x793b`.

The final vector stores end at caller `+0x7b`, establishing a real 0x7c-byte
snapshot rather than a zero-fill fast path. The captured disassembly is in
`docs/reference/artifacts/wcl-low-latency-info-stats-25c56/raw.txt`.

## Local divergence

Before this correction, the sole local stats producer rejected null but then
`memset(..., 0, 0x7c)` and returned success. The local registry's three
low-latency configuration scalars (`enabled`, `powerSave`, `window`) belong
only to the distinct `getWCL_LOW_LATENCY_INFO` carrier. There is no matching
local low-latency stats owner, per-counter reader, or traffic-monitor snapshot
producer.

## Local correction

The local method preserves the Tahoe null guard and returns
`kIOReturnUnsupported` for every non-null carrier before data mutation. No
registry field is removed because none backed the synthetic stats result.

This is a no-backend quarantine, **not Apple valid-input return-code parity**:
the reference accepts valid input and performs the real snapshot reads.

## Verification boundary

`scripts/wcl_low_latency_info_stats_quarantine_report.py` checks the reference
identity, raw disassembly anchors, active slot declarations, retained null
guard, non-null fail-closed result, removal of the zero-fill, and isolation
from the distinct configuration carrier. Build/load and normal network gates
are regression checks only. No private carrier is constructed or invoked at
runtime, so this correction does not claim direct selector execution.
