# CR-479: WCL traffic counters false-success quarantine

Date: 2026-07-14

## Scope

This correction covers only Tahoe V2/Skywalk slot `[527]`,
`getWCL_TRAFFIC_COUNTERS`. It removes a fabricated seven-counter telemetry
snapshot. It does not change V1, ABI declarations, any traffic owner, or
another WCL getter.

## Recovered reference contract

The reference is the 25C56 x86_64
`com.apple.DriverKit-AppleBCMWLAN` DEXT:

```text
path: /System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN
SHA-256: 4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab
UUID: 149C0AD1-A92F-35BC-AA69-5C8815C5421E
```

The active Infra wrapper at `0x100017ca4` tail-dispatches through virtual
offset `+0x428` to `AppleBCMWLANCore::getWCL_TRAFFIC_COUNTERS` at
`0x100132b8c`. Core returns `0xe00002bc` when its carrier is null. With a
non-null carrier, it writes exactly seven qwords at caller offsets `+0`, `+8`,
`+0x10`, `+0x18`, `+0x20`, `+0x28`, and `+0x30`.

Those fields are not a fixed blank carrier: the code reads Core state
`+0x2c18` and `+0x2c20` through virtual offset `+0x368`, combines Core counter
fields at `+0x2a10`, `+0x2a20`, and `+0x2a28`, invokes
`getRealTimeNANTxPktCounter`, and obtains continuous nanoseconds. The direct
disassembly is captured in
`docs/reference/artifacts/wcl-traffic-counters-25c56/raw.txt`.

## Local divergence

Before this correction, the local V2/Skywalk getter kept the null guard but
then filled `7 * sizeof(uint64_t)` bytes with zero and returned success. That
method has no local WCL owner callback, Core-counter aggregation, NAN counter
reader, or continuous-time source backing this snapshot.

## Local correction

The local null guard remains unchanged. Every non-null request now returns
`kIOReturnUnsupported` before output mutation. No carrier layout, V1 path, or
traffic state is invented.

This is a no-backend quarantine, **not Apple valid-input return-code or value
parity**. The reference accepts a non-null carrier and produces a real
snapshot; individual absent owners yielding zero fields do not justify a
blanket local zero snapshot.

## Verification boundary

`scripts/wcl_traffic_counters_quarantine_report.py` verifies the reference
identity and raw anchors, active V2 slot declarations, retained null guard,
non-null fail-closed result, removal of the zero-fill, and absence of a local
counter source in this getter. No private carrier is constructed or invoked at
runtime; build/load and ordinary network gates are regression checks only.
