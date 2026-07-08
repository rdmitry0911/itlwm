# CR-479 BssManager band/RSSI writer seeding

Date: 2026-07-08

## Scope

This batch extends the existing framework-owned `IO80211BssManager` seed burst
with the next reference-proven current-BSS cache writers: band-info bitmap and
last-BSS RSSI. It does not add authentication context, network flags, associated
auth type, or synthetic association state.

## Reference Evidence

BootKC anchors from `10.7.6.112:~/Projects/ghidra_additional/kc_all_symbols.txt`:

- `0xffffff800226682e` `IO80211BssManager::setLastBSSRssi()`
- `0xffffff8002266884` `IO80211BssManager::getCurrentBand(Bands&)`
- `0xffffff8002266fee` `IO80211BssManager::setBandInfoBitmap(unsigned int)`
- `0xffffff800211cfb3` `IO80211GetBandInfoBitmap(unsigned int)`

The mangled export for `getCurrentBand` is
`__ZN17IO80211BssManager14getCurrentBandER5Bands`, so the local declaration
uses an opaque `Bands : unsigned int` enum instead of replacing the parameter
with `unsigned int&`.

Recovered behavior:

- `getCurrentBand(Bands&)` returns `0xe0822403` when the BssManager current-BSS
  object is missing. On success it calls the current BSS band accessor and
  writes the result into the caller's `Bands` slot.
- `IO80211GetBandInfoBitmap(unsigned int)` decrements the one-based band value,
  rejects values outside 1..4, and indexes a four-dword table:
  `{ 0x1, 0x8, 0x2, 0x9 }`.
- `setBandInfoBitmap(unsigned int)` stores the caller dword into the BssManager
  ivars.
- `setLastBSSRssi()` reads RSSI from the BssManager current-BSS object when it
  exists, stores zero otherwise, and therefore does not require a local RSSI
  guess.

The recovered WCL link-state-update caller builds auth context from WCL payload
offsets `+0x10`, `+0x14`, `+0x18`, and `+0x214`, then calls
`setAuthContext`, computes `IO80211GetBandInfoBitmap(payload +0x3d4)`, calls
`setBandInfoBitmap`, and finally calls `setAssocSSID`. Only the band/RSSI part
is closed here because the exact local producer values for the four auth-context
fields, `setNetworkFlags(...)`, and `setAssociatedAuthType(...)` are not yet
proven.

## Local Mapping

`AirportItlwmSkywalkInterface::seedBssManagerRateAndMcs()` continues to use the
verified WCLConfigManager pointer route to recover the framework-owned
`IO80211BssManager *`. The seed burst now:

- asks the recovered BssManager for the current `Bands` value;
- maps that one-based value through the recovered `IO80211GetBandInfoBitmap`
  table and calls `setBandInfoBitmap`;
- calls `setLastBSSRssi()`, letting IO80211Family derive the stored RSSI from
  its own current-BSS object.

No SSID, BSSID, auth, network-flag, or association-state value is fabricated by
this layer.

## Runtime Verification

Installed build:

- SHA-256:
  `66f8b5f9f879e41a6987574c3d1209310d5142665ff0258e40ba05b052a9bffa`
- UUID: `010A5D59-357A-361B-8AD6-81D181C4C87C`

Post-reboot verification on 2026-07-08:

- `kmutil showloaded` reported `com.zxystd.AirportItlwm (2.4.0)` with UUID
  `010A5D59-357A-361B-8AD6-81D181C4C87C`.
- Manual join to `AIAMlab6235` succeeded, `en1` became active with
  `10.77.0.47`.
- Host-to-guest 240-second ping during concurrent guest-to-host TCP stress was
  `240/240` with 0% packet loss. RTT rose under saturation as expected:
  min/avg/max/mdev `0.711/400.311/707.843/216.916 ms`.
- Concurrent guest-to-host `iperf3` ran for 200 seconds and transferred
  658 MiB at about 27.5 Mbit/s receiver rate.
- Post-stress 20-packet ping had 0% loss with min/avg/max/mdev
  `0.687/1.195/4.989/1.059 ms`.
- Unified log and serial checks over the test window found no panic,
  `Kernel stack memory corruption`, `NoCTL`, or `IO80211QueueCall` failure.
  The serial tail still contained the known LQM zero-counter chatter and BT ACPI
  `0xe00002c7` messages.

The known Tahoe public CoreWLAN/TCC symptom remains unchanged:
`networksetup -getairportnetwork en1` still reports
`You are not associated with an AirPort network.` while IP traffic is active.
This batch intentionally does not synthesize that public userland association
surface.
