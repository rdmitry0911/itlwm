# CR-479 — POWER_PROFILE false-success quarantine

Date: 2026-07-14

## Scope

This correction covers only
`AirportItlwmSkywalkInterface::setPOWER_PROFILE`. The former local path read a
single inferred dword into an otherwise unread cache and returned success.
Scoped source finds that cache only in its declaration, two reset sites, and
the setter; it finds no local ConfigManager, power-profile owner, or
power-profile dispatch corresponding to Tahoe.

The direct null guard remains `kIOReturnBadArgumentTahoe` (`0xe00002bc`). A
non-null request now returns `kIOReturnUnsupported` before reading the carrier.
The dead pseudo-cache and reset sites are removed. No power-profile ABI,
ConfigManager, firmware request, synthetic completion, or policy mutation is
introduced.

## Tahoe 25C56 reference recovery

The recovered DEXT is
`/System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN`,
SHA-256
`4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab`.

- `AppleBCMWLANInfraProtocol::setPOWER_PROFILE` at `0x100018fb4` forwards to
  `AppleBCMWLANCore::setPOWER_PROFILE` at `0x100142686`.
- Null returns `0xe00002bc`. A non-null carrier supplies one dword, which Core
  stores at `+0x29e8` before an indirect Core virtual call at `+0x560`.
- `AppleBCMWLANCore` vtable `0x1003a10d8` has the relevant address-point entry
  at `0x1003a1648`, resolving to `AppleBCMWLANCore::setPowerProfile` at
  `0x100124398`.
- That helper selects ConfigManager at Core `+0x1558` and tail-jumps to
  `AppleBCMWLANConfigManager::setPowerProfile` at `0x10008b53e`, which owns
  the downstream configuration state.

This is a ConfigManager/power-profile lifecycle, not an Intel cache-and-success
substitute. The recovered code does not establish a complete public carrier
ABI or authorize a guessed owner/transport implementation.

## Local boundary and non-claims

No unrelated power-save, battery-save, MIMO, FaceTime/Wi-Fi-calling, IPv4,
DHCP, scan, roaming, coexistence, or raw BPF path changes. No direct runtime
invocation of this setter is claimed. Runtime deployment remains gated by the
separately recorded guest `Wi-Fi Power (en1): Off (forced)` lifecycle state;
this static correction does not bypass that state.
