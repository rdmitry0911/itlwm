# Tahoe CopyValue compact current-link carriers

## Scope

This note records the Tahoe `Apple80211CopyValue` ABI for public
CoreWiFi/CoreWLAN current-link getters that are backed by BSD
`SIOCGA80211` requests.

## Reference Evidence

- `IO80211::_Apple80211CopyValue` maps selector `1` (`SSID`) to a
  mutable `CFData` and then calls `_Apple80211GetWithIOCTL`.
- `IO80211::_Apple80211GetWithIOCTL` case `1` zeroes a 32-byte local
  carrier and submits the ioctl with request length `0x20`, but the
  post-ioctl `CFDataAppendBytes(...)` length is the returned
  `apple80211req::req_len`.  The driver must therefore leave the 32-byte
  carrier as the destination capacity and write back the actual SSID byte
  length before returning success.
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
- `IO80211::_Apple80211CopyValue` maps selector `0xd` (`STATE`) to a dedicated
  compact branch: it initializes a four-byte local word, calls
  `_Apple80211GetWithIOCTL(handle, 0xd, 0, &word, 4)`, then creates the
  returned `CFNumber` from that word.
- The `_Apple80211GetWithIOCTL` selector `0xd` branch expands that compact
  word into an 8-byte `{ version, state }` request and submits it through the
  legacy Apple80211 GET ioctl number `0xc02869c9`; live `dtruss` showed this
  ioctl on the `CopyValue(0xd)` path, while the local raw BSD probe had been
  exercising the newer `0xc03069c9` form.
- Full-struct BSD callers of selector `0xd` still use the 8-byte
  `{ version, state }` layout.

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
- The compact SSID carrier is raw SSID bytes starting at offset zero, with
  the remaining destination capacity cleared.  On success, the bridge writes
  the actual SSID byte length back to `apple80211req::req_len` so
  `Apple80211CopyValue(1)` publishes a `CFData` of the real SSID length
  rather than the whole zero-padded 32-byte capacity.
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
- Selector `0xd` now treats `req_len == 4` as the Tahoe compact state carrier
  and writes the associated state word at offset zero for `Apple80211CopyValue`.
- Payload-less selector `0xd` GET still publishes the associated state through
  `apple80211req::req_val`.
- Full-length `apple80211_state_data` callers remain on the existing versioned
  state carrier.
- The Skywalk BSD bridge now accepts the legacy Apple80211 GET ioctl
  `0xc02869c9` and normalizes it to the same local `SIOCGA80211` dispatcher, so
  framework `Apple80211CopyValue` and raw BSD probes consume the same handlers.

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

Legacy-ioctl state closure build
`6209127B-42C6-352B-AF7B-44ACC924BA34`
(`AirportItlwm` binary SHA-256
`374688728451815cbd2b5e84838696c2ef53c5e582356ee602738b0e1d01a4c4`)
closed the remaining low-level state carrier: `Apple80211CopyValue(STATE)`
returned a `CFNumber` value `4`, raw legacy compact ioctl `0xc02869c9` returned
`req_val=4` and data word `4`, raw legacy full-state returned
`version=1 state=4`, and raw new compact ioctl `0xc03069c9` returned the same
state word. The same loaded kext passed the required 240-second concurrent
stress: `240/240` ping replies, `0%` packet loss, RTT
`1.702/761.184/1505.895/261.870 ms`, and `iperf3` `576 MBytes` sent and
received at `20.1 Mbits/sec`. Post-stress `en1` stayed active at
`10.77.0.157`; `system_profiler` still reported `Status: Connected`, channel
`1 (2GHz, 20MHz)`, country `US`, rate `104`, MCS `13`; the stress-window log
filter had no panic, NoCTL, IO80211QueueCall, missed-beacon, deauth, disassoc,
CoreCapture, or firmware-crash hits.

Returned-length closure build
`26CE31A5-2280-3F07-A233-CF27786488D7`
(`AirportItlwm` binary SHA-256
`69decdab356789dceb0ffec4eea268f8675be83f4b93d186d4781632d85dfdfd`)
closed the zero-padded SSID `CFData` mismatch: direct raw ioctl returned
`SSID len=16 ssid=ITLWM-Lab-3c95c7`, `CWFApple80211 SSID:` returned a
16-byte `NSData` containing only the joined SSID bytes, and
`CWFApple80211 currentNetwork:` returned the same 16-byte SSID, BSSID
`80:e4:ba:20:ef:f9`, channel `1`, WPA2 security, and RSSI. The same loaded
kext passed the required 240-second concurrent stress: `240/240` ping replies,
`0.0%` packet loss, RTT `59.122/818.467/1708.437/301.947 ms`, and `iperf3`
transferred `589 MBytes` at `20.6 Mbits/sec` sender with `20.5 Mbits/sec`
receiver. Post-stress `en1` stayed active at DHCP `10.77.0.157`; IORegistry
kept `IO80211SSID = "ITLWM-Lab-3c95c7"`, `IO80211BSSID =
<80e4ba20eff9>`, `CoreWiFiDriverReadyKey = "true"`, `IO80211RSNDone = Yes`,
and `IO80211CountryCode = "US"`; `system_profiler SPAirPortDataType` reported
`Status: Connected`.

Public CoreWLAN/`networksetup` remains open as a driver user-space surface, not
as a closed privacy/TCC-only finding. The captured `networksetup
-getairportnetwork en1` run did log
`CoreLocation CLInternalGetAuthorizationStatusForAppWithAuditToken` and returned
`err=1` before any `Apple80211GetWithIOCTL ... APPLE80211_IOC_SSID` reached the
driver, but that only classifies the observed entry path. Because the source of
truth for associated SSID/BSSID is still the driver's published state, any
public-client nil/not-associated result remains a non-identical driver surface
until the CoreWLAN/networksetup path consumes the same current-link state as the
low-level Apple80211 probes.
