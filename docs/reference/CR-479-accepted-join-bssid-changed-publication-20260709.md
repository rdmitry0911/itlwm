# CR-479 accepted join BSSID_CHANGED publication

## Reference evidence

- Tahoe `CWXPCInterfaceContext::ssidChanged` schedules `__associatedNetwork`
  and forwards that result through `setAssociatedNetwork:`.
- `__associatedNetwork` reads the current interface object through
  `wifiClient -> interfaceWithName:`, requires non-null current `ssidData`,
  then compares that value to the internal current/associated network object
  before returning a CoreWiFi scan result.
- Tahoe `CWXPCInterfaceContext::bssidChanged` is the event-side path that
  refreshes current-BSS state before the `ssidChanged` publication gate. Its
  block reads `wifiClient -> interfaceWithName:`, queries BSSID for the
  interface, copies `ssidData`, `wlanChannel`, RSSI, and BSSID material into
  the current transition objects, and then schedules the associated-network
  refresh.
- The recovered kernel BSSID event ABI remains the existing 24-byte compact
  carrier: BSSID at `+0x00`, embedded `apple80211_channel` at `+0x08`, and
  reason at `+0x14`.

## Local closure

The previous Tahoe implementation only published the populated event-3 carrier
from passive framework entry points: `setCurrentApAddress(...)` and the
`setLinkStateInternal(...)` parent-success gate. Runtime showed that the parent
gate stayed closed on Tahoe, and the current public symptom persisted after
`SSID_CHANGED` was delivered and low-level `CWFApple80211 currentNetwork:`
returned a valid `CWFScanResult`.

The local accepted-join path now publishes the same populated 24-byte
`APPLE80211_M_BSSID_CHANGED` carrier from the current associated BSS after WCL
connect-complete and before zero-length `APPLE80211_M_SSID_CHANGED`. The new
publisher uses the same `AirportItlwmSkywalkInterface` writer, embedded-channel
fill, last-published BSSID tracker, zero-BSSID rejection, and same-BSS
reason-1 suppression as the passive `setCurrentApAddress(...)` hook. It does
not call `setCurrentApAddress(...)`, does not seed any current-AP cache, and
does not restore the rejected zero-length BSSID event.

## Non-claims

- This does not change the raw Apple80211 SSID/BSSID/current-network ioctl
  producers.
- This does not add a userland `networksetup`, TCC, LocationServices, or
  CoreWLAN workaround.
- This does not claim `IO80211BssManager::setNetworkFlags(...)`; its writer ABI
  remains separate until a producer mask and polarity are proven.

## Runtime validation

Tahoe runtime build `C79923D0-EA57-3121-8439-6124496AC777`
(`AirportItlwm` binary SHA-256
`458df8e577497acb8790e3f66c3e1f6a61f937aab351eccd8e799936ec6d5b32`)
completed a controlled rejoin with DHCP `10.77.0.157`, en1 `status: active`,
`IO80211SSID`, `IO80211BSSID`, and `IO80211RSNDone = Yes` present in IORegistry.
`system_profiler SPAirPortDataType` reported the interface connected with
current-network information.

The same binary rebuilt from the current tree with
`./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`;
the built binary SHA-256 matched the loaded kext. A post-build runtime stress
run kept the link up for a 240-second ping while `iperf3` was running in
parallel: `240/240` ping replies, `0%` packet loss, RTT
`1.680/660.536/968.354/129.385 ms`, and `iperf3` transferred `606 MBytes` sent
at `21.2 Mbits/sec` with `605 MBytes` received at `21.1 Mbits/sec`.
Post-stress IORegistry still exposed the real SSID/BSSID and
`IO80211RSNDone = Yes`.

The same run showed the important Tahoe split:

- airportd's own `GET SSID` request (`codesignID=com.apple.airport.airportd`)
  completed with `err=0`, followed by
  `AUTO-JOIN: Updated associated network ...`;
- external `networksetup -getairportnetwork en1` failed with `err=1` after
  `CoreLocation CLInternalGetAuthorizationStatusForAppWithAuditToken` and before
  any `Apple80211GetWithIOCTL ... APPLE80211_IOC_SSID` request reached the
  driver.

Therefore the old public `networksetup` "not associated" output is classified
as a Tahoe Location/TCC pre-IOCTL redaction path for that external client, not
as evidence that the accepted join BSSID/current-BSS publication failed.
