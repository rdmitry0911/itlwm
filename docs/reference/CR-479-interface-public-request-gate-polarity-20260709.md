# CR-479 Tahoe interface public request-gate polarity

Date: 2026-07-09

## Scope

This batch restores only the decompile-proven Tahoe slot `[411]` request
subset that IO80211Family uses after the WCL-first public fallback path:

- `0x45`, `0x46` hidden association carriers;
- `1` `APPLE80211_IOC_SSID`;
- `4` `APPLE80211_IOC_CHANNEL`;
- `9` `APPLE80211_IOC_BSSID`;
- `0x67` `APPLE80211_IOC_CURRENT_NETWORK`;
- `0xd8` `APPLE80211_IOC_ROAM_PROFILE`.

It does not widen `isCommandProhibited(...)` to unrelated public request
numbers.

## Reference Evidence

The recovered IO80211Family fallback helpers call interface slot `+0xcc8`,
mapped in the 25C56 vtable to
`IO80211SkywalkInterface::isCommandProhibited(int)`.

The helpers for request numbers `1`, `9`, `0x67`, and `0xd8` all branch with
the same polarity: a non-zero slot result survives the fallback edge, while
zero takes the pre-helper abort path. Existing CR-479 inventory item 31 records
the exact helper names and request numbers.

## Runtime Trigger

The loaded Tahoe runtime still logged:

- `APPLE80211_IOC_ROAM_PROFILE -> 0xe0822403`;
- immediate `AUTO-JOIN ... error=(37 'driver not available')`.

The local dispatcher already routed selector `216` bidirectionally and
`setROAM_PROFILE(...)` already returned the Apple unsupported contract
`0xe00002c7`. Therefore the failure was before the local helper plane, at the
interface request-gate polarity.

## Local Closure

`AirportItlwmSkywalkInterface::isCommandProhibited(int)` now returns non-zero
directly for exactly the recovered hidden-association and public fallback
request subset. All other commands continue to use the inherited family
behavior.

## Runtime Validation

Validated on the Tahoe 25C56 guest after AuxKC install and reboot:

- loaded kext UUID `29EBEA3E-19C7-37AB-A1A2-0F21FEC7A5E3`, binary SHA-256
  `a17d9d8593023e25edc5c5e9b802789bf8d216c7e8a4f505a4bc9ca14b60de99`;
- raw BSD Apple80211 probe on associated `en1` returned:
  - `STATE state=4`;
  - `SSID len=16 value=ITLWM-Lab-3c95c7`;
  - `BSSID 80:e4:ba:20:ef:f9`;
  - `CARD_CAPABILITIES ef:e6:6f:27:00:40:0c:06:01:02:00:00:00:00:00:00:00:00:00:00:00:00:00:00`;
  - `CURRENT_NETWORK ssidLen=16 ssid=ITLWM-Lab-3c95c7 bssid=80:e4:ba:20:ef:f9 ieLen=168`;
  - `ROAM_PROFILE` SET returned normal BSD unsupported shape
    `rc=-1 errno=102`, with no same-window `0xe0822403` /
    `driver not available` log hit.
- `en1` remained active with DHCP `10.77.0.157`;
- `system_profiler SPAirPortDataType` reported connected infrastructure WPA2,
  country `US`, transmit rate `104`, and MCS `13` after stress;
- concurrent 240-second stress passed with `PING_RC=0` and `IPERF_RC=0`:
  `240 packets transmitted, 240 packets received, 0.0% packet loss`, RTT
  `3.916/784.199/1694.944/304.509 ms`, and iperf3 transferred `559 MBytes`
  at `19.5 Mbits/sec` sent/received;
- the post-stress log filter for panic, firmware crash, NoCTL, missed beacon,
  stack corruption, deauth, disassoc, `driver not available`, and
  `0xe0822403` signatures was empty.

Non-claims:

- this does not bypass CoreWLAN, LocationServices, TCC, or `networksetup`;
- this does not mark public `CWInterface.ssid` / `bssid` complete;
- the same runtime still had `networksetup -getairportnetwork en1` print
  `You are not associated with an AirPort network.`;
- this does not add logging or instrumentation.
