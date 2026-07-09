# CR-479 Tahoe scan-result SSID offset

Date: 2026-07-09

## Scope

This note records the Tahoe `apple80211_scan_result` ABI correction that fixed
malformed current-network SSID publication through
`Apple80211CopyCurrentNetwork` and `CWFApple80211 currentNetwork:`.

## Reference Evidence

Primary BootKC consumer:

- `IO80211BssManager::getCurrentNet(...)`
  `0xffffff80022669ae`

Recovered offset evidence:

- the BSSID field is copied from the current-BSS object at carrier offset
  `+0x1c`;
- `MOV byte ptr [RBX + 0x60], AL` writes the SSID length to carrier offset
  `+0x60`;
- `MOVZX EDX, byte ptr [RBX + 0x60]` reloads that SSID length for the copy;
- `LEA RDI, [RBX + 0x61]` copies SSID bytes into carrier offset `+0x61`;
- Tahoe still consumes IE length at `+0x8a` and IE bytes at `+0x8c`.

The local Tahoe layout had artificially reduced the scan-result rate array to
14 entries and added a four-byte reserved hole before `asr_ie_len`. That kept
the IE offsets stable, but moved `asr_ssid_len` to `+0x5c` and
`asr_ssid` to `+0x5d`, which is not the Apple BssManager layout.

## Local Closure

Tahoe `apple80211_scan_result` now uses the same 15-rate prefix as the legacy
carrier:

- `asr_bssid` remains at `+0x1c`;
- `asr_rates[15]` occupies `+0x24..+0x5f`;
- `asr_ssid_len` is at `+0x60`;
- `asr_ssid` starts at `+0x61`;
- `asr_ie_len` remains at `+0x8a`;
- `asr_ie_data` remains at `+0x8c`;
- total carrier size remains `0x8d4`.

Compile-time Tahoe asserts and `tests/tahoe_payload_builders_test.cpp` now lock
those offsets and the total size.

## Runtime Classification

Before this fix, `Apple80211CopyCurrentNetwork` and
`CWFApple80211 currentNetwork:` returned a malformed SSID object with length
`87`; the bytes started with the tail of the joined SSID and then included IE
payload bytes.

After the fix, the loaded Tahoe build
`1F012C39-0D8D-3254-9C76-3CA7037033D1`
(`AirportItlwm` SHA-256
`05cdf28f080ccdbae94bfaa52532a3df4a208e4ed020802cd344fda403a9af69`)
returned the correct current-network carrier:

- raw `CURRENT_NETWORK`: SSID length `16`, SSID `ITLWM-Lab-3c95c7`, BSSID
  `80:e4:ba:20:ef:f9`, channel `1`, IE length `168`;
- `Apple80211CopyCurrentNetwork`: `SSID` CFData length `16`, `SSID_STR`
  `ITLWM-Lab-3c95c7`, BSSID `80:e4:ba:20:ef:f9`;
- `CWFApple80211 currentNetwork:`: `CWFScanResult.SSID` length `16`, matching
  SSID bytes and BSSID.

The same loaded kext passed the required 240-second concurrent data-path check:
ping reported `240 packets transmitted, 240 packets received, 0.0% packet
loss`, RTT `4.127/740.237/1473.066/256.531 ms`; iperf3 transferred
`590 MBytes` sent at `20.6 Mbits/sec` and `589 MBytes` received at
`20.6 Mbits/sec`. Post-stress `en1` remained `active` at DHCP `10.77.0.157`,
`system_profiler SPAirPortDataType` reported `Status: Connected`, channel
`1 (2GHz, 20MHz)`, country `US`, rate `104`, and MCS `13`, and the
stress-window log filter had no panic, stack-corruption, NoCTL,
IO80211QueueCall, missed-beacon, deauth, disassoc, CoreCapture, or
firmware-crash hits.

Public CoreWLAN still reported `CWInterface.ssid == nil` and
`CWInterface.bssid == nil`, and `networksetup -getairportnetwork en1` still
printed `You are not associated with an AirPort network.` This batch therefore
closes the low-level scan-result ABI corruption, not the remaining public
CoreWLAN/current-profile gate.
