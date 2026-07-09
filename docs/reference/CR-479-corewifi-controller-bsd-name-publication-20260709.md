# CR-479 CoreWiFi Controller BSD Name Publication

## Scope

Close the CoreWiFi `___findWiFiController` publication gap without changing
Apple80211 IOCTL routing or the Tahoe public request gate.

## Reference Evidence

- CoreWiFi `-[CWFIO80211 IO80211InterfaceInfo:error:]`
  (`0x7ff81ee6acc0`) builds:
  - `propertyDictionary[IOInterfaceName] = interfaceName`
  - `matchingServiceDict[IOPropertyMatch] = propertyDictionary`
  - then calls `IOServiceGetMatchingService(...)`.
- A live selector/string decode of the same CoreWiFi image confirmed:
  - `0x7ff842efeb28` -> `IOInterfaceName`
  - `0x7ff842efea68` -> `IOPropertyMatch`
- CoreWiFi `___findWiFiController` (`0x7ff81ee6a980`) iterates
  `IO80211Controller` services and reads each controller's `BSD Name`
  property.
- CoreWiFi `___io80211ControllerInfo` (`0x7ff81ee6bf60`) then iterates
  `IOSkywalkEthernetInterface` services and recursively requires:
  - `BSD Name == controller BSD Name`
  - `IOClassNameOverride == IO80211Controller`

## Local Mismatch

The live Tahoe runtime before this patch showed:

- direct `CWFIO80211 IO80211InterfaceInfo:@"en1"` succeeded, proving the
  `IOInterfaceName` property-match path itself was not missing;
- the CoreWiFi `___findWiFiController` mimic failed because
  `AirportItlwm` had no controller-side `BSD Name`;
- the matching Skywalk descendant already exposed recursive
  `BSD Name = en1` and `IOClassNameOverride = IO80211Controller`.

## Fix

`AirportItlwmSkywalkInterface::setBSDName(char const *)` now preserves the
framework setter first, then publishes the same non-empty BSD name as an
OSString on the bound `AirportItlwm` controller under `BSD Name`.

This is not a fallback or replay path: it mirrors the already assigned
framework BSD name at the materialization point CoreWiFi consumes.

## Runtime Validation

- Tahoe 25C56 loaded kext UUID `13055703-139A-3837-98EF-23034DD8CDE5`;
  installed binary SHA-256
  `4264668a4a679dc664f7b05a2f4a639ac4b1efaa97595c15ae9010735a9ba09e`.
- Post-boot IORegistry exposed controller-side `"BSD Name" = "en1"` on
  `AirportItlwm`.
- The local CoreWiFi controller mimic changed from `ctrl.BSD Name=(nil)` to
  `ctrl.BSD Name=en1`, with recursive interface `BSD Name=en1`,
  `IOClassNameOverride=IO80211Controller`, and `match bsd=1 cls=1 both=1`.
- Direct `CWFIO80211 IO80211InterfaceInfo:@"en1"` returned the expected
  associated dictionary including `CoreWiFiDriverReadyKey`, `IO80211SSID`,
  `IO80211BSSID`, `IO80211Channel`, `IO80211CountryCode`, and
  `IO80211RSNDone`.
- Public Apple80211 probes still bound to `en1` and returned current-network,
  SSID, BSSID, CARD_CAPABILITIES, STATE, interface-name, and current-network
  values.
- A concurrent 240-second ping plus iperf3 stress pass completed with
  `PING_RC=0` and `IPERF_RC=0`: ping reported `240 packets transmitted`,
  `240 packets received`, `0.0% packet loss`, RTT
  `1.209/542.608/940.242/139.723 ms`; iperf3 transferred `778 MBytes` at
  `27.2 Mbits/sec` sender and `27.1 Mbits/sec` receiver.
- Post-stress `en1` remained active at DHCP `10.77.0.47`,
  `system_profiler SPAirPortDataType` reported `Status: Connected`, and
  IORegistry still exposed controller `"BSD Name" = "en1"` plus the associated
  `IO80211*` keys.
- The stress-window fault filter for `kernel` or `AirportItlwm` found no panic,
  crash, NoCTL, missed beacon, stack, deauth, disassoc, driver-not-available,
  `0xe0822403`, or `IO80211QueueCall` signature.

## Non-Claim

`networksetup -getairportnetwork en1` still prints
`You are not associated with an AirPort network.` on the same runtime.
The captured airportd path is a separate open layer: `networksetup` gets
`GET INTF CAPS err=0`, then `GET SSID err=1`.
