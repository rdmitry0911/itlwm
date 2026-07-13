# CR-479: Dynamic RSSI Window false-success quarantine

Date: 2026-07-13

## Reference contract

The Tahoe 25C56 `com.apple.DriverKit-AppleBCMWLAN` image has SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.
The recovered Infra wrapper symbol is at `0x100019530`.  Separately, the Core
implementation `AppleBCMWLANCore::setDYNAMIC_RSSI_WINDOW_CONFIG` is at
`0x10014365e`.  It
returns `0xe00002bc` for a null pointer and passes the carrier dword to
`configureDynamicRssiWindow` at `0x100140672`.

Core accepts values 2 through 16 inclusive and otherwise returns
`0xe00002c2`.  For an accepted value it selects ConfigManager through Core
`+0x1558` and reaches `AppleBCMWLANConfigManager::configureDynamicRssiWindow`
at `0x10008c6a6`.  That manager requires its feature byte, otherwise returns
`0xe00002c7`; it sends a four-byte `rssi_win` value ORed with `0x200`, then a
four-byte `snr_win` value through `AppleBCMWLANCommander::runIOVarSet`.  The
recovered routine handles both command results, including a special
`0xe3ff8117` branch.  This is real configuration and firmware work, not an
inert Core cache.

The reference details do not establish a complete public carrier allocation.
The local correction below also does not claim Apple null-input status,
accepted-range/error, feature-gate, or transport-status parity.

## Local divergence

`AirportItlwmSkywalkInterface::setDYNAMIC_RSSI_WINDOW_CONFIG` previously
accepted a non-null pointer, copied its dword into unconsumed local
`cachedDynamicRssiWindowConfig`, and returned success.  Scoped local source
has no matching Dynamic-RSSI configurator or `rssi_win`/`snr_win` path.

## Local correction

The existing local null guard remains.  A non-null request now returns
`kIOReturnUnsupported` before cache or configurator mutation, and the dead cache
member plus its initialization/reset sites are removed.  This is a local
no-backend quarantine, not Apple valid-input return-code parity.

## Deterministic guard

`scripts/dynamic_rssi_window_quarantine_report.py --check` requires the
25C56 wrapper/Core/ConfigManager/IOVAR anchors, preserved local null guard, a
non-null unsupported result, removal of pseudo-state, absence of the matching
local Dynamic-RSSI anchor literals, and the corrected signal-chain wording.
