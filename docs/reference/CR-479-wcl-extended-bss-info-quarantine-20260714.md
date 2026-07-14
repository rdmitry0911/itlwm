# CR-479: WCL extended BSS info false-success quarantine

Date: 2026-07-14

## Scope

This correction covers only Tahoe V2/Skywalk slot `[533]`,
`getWCL_EXTENDED_BSS_INFO`. It removes a non-null success that had neither an
output writer nor a matching local owner. It does not change V1, the ABI slot,
or any association/rate/MCS implementation.

## Recovered reference contract

The reference is the 25C56 x86_64
`com.apple.DriverKit-AppleBCMWLAN` DEXT:

```text
path: /System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN
SHA-256: 4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab
UUID: 149C0AD1-A92F-35BC-AA69-5C8815C5421E
```

The active Infra wrapper at `0x100017c58` tail-dispatches through virtual
offset `+0x420` to `AppleBCMWLANCore::getWCL_EXTENDED_BSS_INFO` at
`0x100132df6`. Core returns `0xe00002bc` for a null carrier; otherwise it
loads its NetAdapter at Core state `+0x15e0` and tail-calls
`AppleBCMWLANNetAdapter::getExtendedBssInfo` at `0x10019de64`.

That adapter is a real output pipeline, not a no-op success: it invokes
`updateRateSetSync`, `updateMCSSetSyc`, and `getAssociatedWPARSNIESync`,
placing sub-carriers around caller offsets `+0xbc`, `+0xcc/+0xd4`, and `+0x113`
respectively. Rate/MCS failures are returned. Feature `0x73` with an 11be
adapter additionally obtains current BSSID and calls `getMloContext` at
caller `+0xdc`. The direct disassembly is captured in
`docs/reference/artifacts/wcl-extended-bss-info-25c56/raw.txt`.

## Local divergence

Before this correction, the local V2/Skywalk getter retained a null guard but
returned success for every non-null carrier without writing any field or
calling an Extended-BSS, NetAdapter, rate/MCS, RSN, or MLO producer.

## Local correction

The local null guard remains unchanged. Every non-null request now returns
`kIOReturnUnsupported` before output mutation. No cache, opaque carrier,
NetAdapter substitute, or synthetic association state is introduced.

This is a no-backend quarantine, **not Apple valid-input return-code or
output-layout parity**. The reference valid path is a real producer and may
return its own synchronization errors.

## Verification boundary

`scripts/wcl_extended_bss_info_quarantine_report.py` checks the reference
identity/raw anchors, active V2 slot declarations, retained local null guard,
non-null fail-closed result, and absence of the reference producer calls in
the local getter. No private carrier is constructed or invoked at runtime;
build/load and ordinary network gates are regression checks only.
