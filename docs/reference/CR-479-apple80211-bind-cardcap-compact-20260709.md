# CR-479 Apple80211 bind CARD_CAPABILITIES compact ABI

Date: 2026-07-09

## Scope

Tahoe `Apple80211BindToInterfaceWithService` falls back to the legacy IOCTL
path when DriverKit user-client access is unavailable. That fallback issues
`APPLE80211_IOC_CARD_CAPABILITIES` with `req_len = 0x15`, not the full Tahoe
public carrier length `0x1c`.

## Evidence

- IO80211 decompile:
  `06_decomp/IO80211/0079_0x7ffc12d5001c__Apple80211BindToInterfaceWithService.c`
  stores selector `0x0c`, length `0x15`, and `req_data = handle + 0x58` before
  calling ioctl `0xc02869c9`.
- Pre-fix live probe on the associated Tahoe guest:
  `CARD_CAPABILITIES len=0x1c` returned success with the recovered capability
  bytes, while `len=0x15` returned `0xe00002c2`.
- The same pre-fix runtime showed `Apple80211BindToInterface(en1)` falling back
  after DriverKit entitlement denial, then failing to bind.

## Local Closure

`AirportItlwmSkywalkInterface::processApple80211Ioctl(...)` now treats
`0x15` as the Apple80211 bind compact carrier for selector `0x0c`. It builds the
same full recovered `apple80211_capability_data` payload and copies only the
caller-requested compact prefix, preserving the existing full `0x1c` response
for full-size callers and retaining rejection for shorter nonzero lengths.

`AirportItlwm::handleCardSpecific(...)` seeds the same compact length for the
selector `0x0c` card-specific ingress that lacks an `apple80211req::req_len`
field.

## Runtime Validation

Validated on Tahoe 25C56 with loaded kext UUID
`04FF242B-3AE8-3A29-AC0D-E82C554FD5AB`:

- raw BSD Apple80211 `CARD_CAPABILITIES len=0x15` returned success for both
  Tahoe and legacy ioctl command numbers;
- `len=0x14` continued to return `0xe00002c2`;
- `Apple80211BindToInterface(en1)` returned `0`;
- `Apple80211CopyCurrentNetwork`, SSID, BSSID, CARD_CAPABILITIES, STATE, and
  CURRENT_NETWORK returned real associated-network payloads.
- concurrent 240-second ping plus iperf3 stress passed with `PING_RC=0` and
  `IPERF_RC=0`: `240 packets transmitted, 240 packets received, 0.0% packet
  loss`; iperf3 transferred `824 MBytes` at `28.8 Mbits/sec`;
- post-stress `en1` remained active at DHCP `10.77.0.47`, and
  `system_profiler SPAirPortDataType` reported connected WPA2 infrastructure
  on channel `6`, country `US`, transmit rate `104`, and MCS `13`;
- the stress-window fault filter found no panic, firmware crash, NoCTL, missed
  beacon, stack corruption, deauth, disassoc, `driver not available`,
  `0xe0822403`, or `IO80211QueueCall` signature.

Non-claim: `networksetup -getairportnetwork en1` still prints
`You are not associated with an AirPort network.` on this Tahoe runtime because
airportd rejects its CoreWLAN `GET SSID` request with
`APP NOT AUTHORIZED FOR LOCATION SERVICES` before any Apple80211 payload query.
