# CR-479 Tahoe low-latency enable carrier

## Reference evidence

- 25C56 `IOSkywalkLegacyEthernet::start` keeps the
  `IOSkywalkEthernetInterface` provider at `this+0x120` and creates the
  `IOLinkStatus` symbol used by its property override.
- 25C56 `IOSkywalkLegacyEthernet::getProperty("IOLinkStatus")` does not trust a
  locally mirrored legacy-node property. It reads the provider context at
  `*(provider+0xc0)+0x38`, wraps that dword in an `OSNumber`, sets the legacy
  property, and then falls through to the superclass getter.
- 25C56 `IOSkywalkNetworkInterface::reportLinkStatus(unsigned int,unsigned int)`
  is the writer for that provider context: it stores non-zero speed at
  context `+0x30`, stores `status & 7` at context `+0x38`, and emits the
  corresponding link event.
- Existing Tahoe signal-chain notes recover
  `AppleBCMWLANLowLatencyInterface::setInterfaceEnable(true)` as:
  base `IO80211InfraInterface::setInterfaceEnable(true)`, then
  `reportLinkStatus(3, 0x80)`, then
  `setLinkState(kIO80211NetworkLinkUp, 1, false, 0, 0)`.

## Local mismatch

The local modeled low-latency object stopped after the base enable because an
old diagnostic comment claimed the recovered side effects poisoned the main
infra object. Live Tahoe 25C56 contradicted that as a final shape: associated
`en1` stayed active and transported traffic, while the legacy child still
reported `IOLinkStatus = 0` and `IOLinkSpeed = 0`.

## Closure

Restore the recovered low-latency enable body on
`AirportItlwmSkywalkInterface::setInterfaceEnable(true)`. Do not mirror
`IOLinkStatus` or `IOLinkSpeed` directly onto `IOSkywalkLegacyEthernet`; the
reference contract makes the provider carrier context the source of truth.

## Validation

- The loaded Tahoe guest reports `IOLinkStatus = 3` through the keyed
  `IOSkywalkLegacyEthernet` property lookup, proving the provider carrier
  context is visible through the reference getter path.
- The same boot keeps association and transport live: `Apple80211CopyCurrentNetwork`
  and `CWFApple80211 currentNetwork:` return the real SSID/BSSID/channel/RSN
  scan record, and `system_profiler SPAirPortDataType` reports Wi-Fi
  `Status: Connected`.
- A 240-second ping plus `iperf3` stress run completed on the loaded build.

This closure intentionally does not claim the public CoreWiFi
`networksetup -getairportnetwork en1` surface: the captured Tahoe log for that
tool shows `airportd` denying the public `GET SSID` XPC request in
`CWFXPCConnection::__allowXPCRequestWithType:error:` before a visible driver
ioctl, while the lower Apple80211 current-network path succeeds. That remains a
separate open layer.
