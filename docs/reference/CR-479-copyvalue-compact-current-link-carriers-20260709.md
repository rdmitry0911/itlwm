# Tahoe CopyValue compact current-link carriers

## Scope

This note records the Tahoe `Apple80211CopyValue` ABI for public
CoreWiFi/CoreWLAN current-link getters that are backed by BSD
`SIOCGA80211` requests.

## Reference Evidence

- `IO80211::_Apple80211CopyValue` maps selector `1` (`SSID`) to a
  mutable `CFData` and then calls `_Apple80211GetWithIOCTL`.
- `IO80211::_Apple80211GetWithIOCTL` case `1` zeroes a 32-byte local
  carrier, sets the ioctl request length to `0x20`, and appends exactly
  that byte count to the returned `CFData`.
- `CoreWiFi::-[CWFApple80211 SSID:]` calls
  `_Apple80211CopyValue(handle, 1, 0, &value)` and converts the returned
  object through the same Objective-C selector used by the BSSID path.
- `IO80211::_Apple80211CopyValue` maps selector `9` (`BSSID`) to a
  mutable `CFString`.
- `IO80211::_Apple80211GetWithIOCTL` case `9` sets the ioctl request
  length to `6`, then either copies those six octets directly or formats
  them with `ether_ntoa(...)`.
- `CoreWiFi::-[CWFApple80211 BSSID:]` calls
  `_Apple80211CopyValue(handle, 9, 0, &value)`, converts the returned
  string, and normalizes it with `CWFCorrectEthernetAddressString(...)`.
- `IO80211::_Apple80211CopyValue` maps selector `0xd` (`STATE`) to a
  `CFNumber`, but the lower `_Apple80211GetWithIOCTL` carrier remains the
  8-byte `{ version, state }` layout and copies the second dword into the
  caller's 4-byte number slot.

## Local Delta

The Skywalk BSD bridge previously ignored `apple80211req::req_len` and
always wrote legacy versioned structs for `SSID` and `BSSID`:

- `apple80211_ssid_data` is 40 bytes and starts with `{ version, ssid_len }`;
- `apple80211_bssid_data` is 12 bytes on the local ABI and starts with
  `version`.

That shape is correct for legacy struct callers, but it is not the Tahoe
`Apple80211CopyValue` compact carrier. When userspace asks for 32 SSID
bytes or six BSSID octets, writing the versioned struct both corrupts the
compact payload and writes past the length requested by the framework.

## Local Closure

- `AirportItlwmSkywalkInterface::processApple80211Ioctl(...)` now treats
  `req_len == 0x20` on selector `1` as the Tahoe compact SSID carrier.
- The compact SSID carrier is zero-padded raw SSID bytes starting at
  offset zero.
- `req_len == 6` on selector `9` is treated as the Tahoe compact BSSID
  carrier.
- The compact BSSID carrier is the raw six-octet current BSSID starting at
  offset zero.
- Legacy versioned struct callers remain on the existing
  `getSSID(...)`/`getBSSID(...)` path.
- The Tahoe card-specific virtual no longer owns separate SSID/BSSID raw
  writers. Its get-side selector 1/9 ingress only supplies the recovered
  compact `req_len` values before entering the same Skywalk BSD bridge, which
  prevents no-length fallback into the legacy versioned structs.
- Selector `0xd` is intentionally unchanged because the reference lower
  ioctl carrier is still the 8-byte versioned state struct.

## Validation Status

Tahoe runtime build `C79923D0-EA57-3121-8439-6124496AC777`
(`AirportItlwm` binary SHA-256
`458df8e577497acb8790e3f66c3e1f6a61f937aab351eccd8e799936ec6d5b32`)
kept the low-level current-link path valid: controlled rejoin returned success,
DHCP stayed active at `10.77.0.157`, IORegistry exposed `IO80211SSID`,
`IO80211BSSID`, and `IO80211RSNDone = Yes`, and
`CWFApple80211 currentNetwork:` returned the real SSID data, BSSID, channel,
security, and RSSI.

The current tree rebuilt successfully with the same binary SHA-256 as the loaded
kext, then passed the required 240-second concurrent data-path check: `240/240`
ping replies, `0%` packet loss, RTT `1.680/660.536/968.354/129.385 ms`, and
`iperf3` `606 MBytes` sent at `21.2 Mbits/sec` with `605 MBytes` received at
`21.1 Mbits/sec`. Post-stress IORegistry still carried the real SSID/BSSID,
`CoreWiFiDriverReadyKey = true`, and `IO80211RSNDone = Yes`.

Follow-up cleanup build
`05555661-F35D-3449-A801-1B647CC9577B`
(`AirportItlwm` binary SHA-256
`cb85ca345aa60f6f32c4559d7c3c5374b34b4583ea6f101417048730005c04a7`)
removed the duplicate `handleCardSpecific(...)` SSID/BSSID raw writers while
keeping the compact bridge valid: direct `Apple80211CopyValue` probes returned
selector `1` as `CFData` length `16` with the joined SSID bytes, selector `9`
as `CFString` BSSID `80:e4:ba:20:ef:f9`, and selectors `13` and `103` with
`err=0`. The same loaded kext passed the required 240-second concurrent stress:
`240/240` ping replies, `0%` packet loss, RTT
`1.603/730.552/1550.276/272.483 ms`, and `iperf3` `581 MBytes` sent at
`20.3 Mbits/sec` with `580 MBytes` received at `20.2 Mbits/sec`. Post-stress
`en1` remained `active` at DHCP `10.77.0.157`, `system_profiler` reported
`Status: Connected`, channel `1 (2GHz, 20MHz)`, rate `104`, MCS `13`, and the
stress-window log filter had no panic, stack-corruption, NoCTL,
IO80211QueueCall, missed-beacon, deauth, disassoc, or CoreCapture hits.

Public external clients remain a separate Tahoe privacy surface. In the same
runtime window, `networksetup -getairportnetwork en1` logged
`CoreLocation CLInternalGetAuthorizationStatusForAppWithAuditToken` and returned
`err=1` before any `Apple80211GetWithIOCTL ... APPLE80211_IOC_SSID` reached the
driver. That result does not validate or invalidate the compact ioctl carriers;
it proves that this external client is stopped by Location/TCC before the BSD
Apple80211 path is consulted.
