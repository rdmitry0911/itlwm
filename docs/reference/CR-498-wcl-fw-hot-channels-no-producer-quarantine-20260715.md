# CR-498 — WCL FW hot channels NetAdapter no-producer quarantine

Date: 2026-07-15

## Scope

This correction covers only Tahoe V2/Skywalk protocol slot `[524]`
`getWCL_FW_HOT_CHANNELS`. It removes the local four-byte zero-fill and
successful acknowledgement for a non-null carrier. It preserves the public
virtual slot, opaque carrier declaration, method spelling used by neighboring
report delimiters, and adjacent `[525]` low-latency and `[527]` traffic
surfaces. The tree has no numeric `APPLE80211_IOC_WCL_FW_HOT_CHANNELS`
selector or request-switch route; this correction does not add one.

## Recovered 25C56 reference

The reference x86_64 DriverKit DEXT is
`com.apple.DriverKit-AppleBCMWLAN`:

~~~text
path: /System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN
SHA-256: 4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab
UUID: 149C0AD1-A92F-35BC-AA69-5C8815C5421E
~~~

Infra `getWCL_FW_HOT_CHANNELS` wrapper `0x100017b74` dispatches through
virtual offset `0x770`; its Core vtable cell `0x1003A1858` rebases to
`0x100140c84`. Core reaches NetAdapter at `(Core + 0x48) + 0x15e0` and
tail-calls `NetAdapter::getFWHotChannels`.

The selected NetAdapter body issues
`runIOVarGet("roam_channels_in_hotlist")`, retains that transport status, and
branches to the output path only when it is zero. On that observed branch it
clamps the reply count to seven, writes the count at caller `+0x10`, then
converts each observed reply chanspec through `getAppleChannelSpec` into
caller words beginning at `+0x0`. The selected static recovery does not
establish a complete caller layout, null-input behavior, every reply field,
or a local replacement lifecycle. The capture is
`docs/reference/artifacts/wcl-fw-hot-channels-25c56/raw.txt`.

## Local correction

AirportItlwm had no matching NetAdapter hot-channel owner,
`roam_channels_in_hotlist` query, transport status, reply validation, or
channel-spec conversion path. Its local getter had only a null guard followed
by `memset(data, 0, sizeof(uint32_t))` and `kIOReturnSuccess`. The local null
guard remains a safety boundary. Every non-null request now returns
`kIOReturnUnsupported` before caller mutation.

This is a no-producer quarantine, **not Apple null-input, valid-input
return-code, full-carrier, channel-value, NetAdapter-owner, firmware, or
runtime-selector parity**. It invokes no private selector, IOVAR, firmware
command, scan, radio transition, deployment, association, or traffic path.

## Deterministic guard

`scripts/wcl_fw_hot_channels_quarantine_report.py --check` verifies reference
identity/raw routing, the transport-status and bounded output branches,
retained protocol ABI without a newly invented numeric IOC, local null
boundary, non-null no-output failure, absence of a local NetAdapter/IOVAR
backend, method delimiter preservation, and supersession of the former
state-backed telemetry classification.
