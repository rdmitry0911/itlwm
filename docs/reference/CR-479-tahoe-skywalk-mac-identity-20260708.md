# CR-479 Tahoe Skywalk MAC Identity

Date: 2026-07-08

Scope: keep the Tahoe Skywalk interface, BSD ifnet, and
`IOSkywalkLegacyEthernet` registry identity aligned across the private
`SET_MAC_ADDRESS` edge.

Reference evidence:

- `IO80211SkywalkInterface::setSET_MAC_ADDRESS` at `0xffffff8002252736`
  rejects null payloads, then calls the MacAddressAgent at
  `*(this + 0x120) + 0x50` with mode `2`.
- `IO80211SkywalkInterface::setHardwareAddress` at `0xffffff80022526ce`
  uses the same MacAddressAgent helper with mode `1`.
- MacAddressAgent apply helper `0xffffff8002238b6e` stores the new MAC,
  calls the inherited link-layer setter for mode `2` when the interface is
  enabled, posts `APPLE80211_M_LINK_ADDRESS_CHANGED` (`0x3b`), and updates
  the interface `IOMACAddress` property.
- `IOSkywalkEthernetInterface::setLinkLayerAddress` at
  `0xffffff8002a17cb0` updates `IOMACAddress` on the interface object and
  the BSD ifnet address.
- `IOSkywalkLegacyEthernet::getHardwareAddress` at `0xffffff8002a036d0`
  reads `IOMACAddress` through its referenced interface object, but the
  live `IOSkywalkLegacyEthernet` registry node also has its own materialized
  `IOMACAddress` property. That property is not redirected by the legacy
  `getProperty` path for normal registry enumeration.

Local conclusion:

The local Tahoe path materializes `IOSkywalkLegacyEthernet` before the WCL
private MAC update. Therefore the initial MAC must be seeded before Skywalk
registration, and the already-created legacy registry facade must be kept in
sync on the same `SET_MAC_ADDRESS` edge as the reference MacAddressAgent
state/property/message update.

Runtime proof on loaded UUID `1964CDB8-0C4E-316E-8FEE-CCE07F3AA943`,
CDHash `c8b99d1c67c5b9cf3ad7f1f2b8038bdd3ccfbf08`:

- Before join, interface and legacy facade both expose initial MAC
  `ca:f7:33:f4:97:4b`.
- After `SET_MAC_ADDRESS` during join, `networksetup -listallhardwareports`,
  `ifconfig en1`, parent `IOMACAddress`, and
  `IOSkywalkLegacyEthernet/IOMACAddress` all expose
  `c6:ef:66:c0:c0:ca`.
- `system_profiler SPAirPortDataType` reports `Status: Connected`, BSSID
  `80:e4:ba:20:ef:f9`, channel 6, and DHCP remains on `10.77.0.157`.

Remaining separate public API mismatch:

- `networksetup -getairportnetwork en1` still prints Tahoe's stale
  `You are not associated with an AirPort network.` This batch does not
  reintroduce the forbidden current-AP seed crutch; that output remains a
  separate Apple80211 current-network/status path discrepancy.
