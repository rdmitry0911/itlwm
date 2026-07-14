# CR-479: WCL trigger CC false-success quarantine

Date: 2026-07-14

## Scope

This correction covers only Tahoe V2/Skywalk slot `[599]`,
`setWCL_TRIGGER_CC`. It removes the local unread request cache and the valid
mode success acknowledgement. It does not implement ScanAdapter, JoinAdapter,
or change V1, scan state, or association state.

## Recovered reference contract

The reference is the 25C56 x86_64
`com.apple.DriverKit-AppleBCMWLAN` DEXT:

```text
path: /System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN
SHA-256: 4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab
UUID: 149C0AD1-A92F-35BC-AA69-5C8815C5421E
```

The active Infra wrapper at `0x100018c30` tail-dispatches to
`AppleBCMWLANCore::setWCL_TRIGGER_CC` at `0x10013a800`. Core reads the mode
at request `+0x8`: mode `0` dispatches to
`AppleBCMWLANScanAdapter::triggerCC` at `0x10018fda2`, mode `1` dispatches to
`AppleBCMWLANJoinAdapter::triggerCC` at `0x100040e28`, and any other mode
returns `0xe00002bc`.

Both adapters copy 0x20 bytes into adapter-owned state. The ScanAdapter path
also reports scan-side data; the JoinAdapter path performs event-field work,
collects join timeout/CCA metrics, and changes join-adapter state. These are
real producer paths, not a shared Core cache. The captured disassembly is in
`docs/reference/artifacts/wcl-trigger-cc-25c56/raw.txt`.

## Local divergence

Before this correction, the local V2/Skywalk setter copied 0x20 bytes into
`cachedTriggerCC`, marked it present, and returned success for modes 0/1. The
fields had no local reader and no Scan/Join adapter, fault-reporting, metric,
or state pipeline behind them.

## Local correction

The local null guard remains unchanged. The local invalid-mode boundary remains
`kIOReturnBadArgumentTahoe`. Valid modes now return `kIOReturnUnsupported`
before local state mutation. The dead snapshot type, cache fields, and their
two initialization/reset sites are removed.

This is a no-backend quarantine, **not Apple null, valid-mode return-code,
input-size, or adapter-side parity**. It does not establish the complete
opaque request layout or replace either Apple adapter.

## Verification boundary

`scripts/wcl_trigger_cc_quarantine_report.py` verifies the reference identity
and raw anchors, the active V2 slot, local retained boundaries, valid-mode
fail-closed result, dead-cache removal, and absence of local adapter work. No
private carrier is constructed or invoked at runtime; build/load and ordinary
network gates are regression checks only.
