# Tahoe scan RSSI publication — 2026-09-09

## Reproduced boundary

The released IWN image from `549114c2` was loaded with UUID
`13B4F696-F1E4-3FCC-82E4-3834DE4B4E76`. A bounded, read-only FBT observation
followed scan results from the Intel/node boundary to Apple's BSS consumer
and final `WCLScanManager::serveScanResult` return. The node layout was
obtained from this build's `ItlIwn.o` DWARF, not an older kext's offsets.

- The target 5-GHz BSS was intermittently received and serialized by the
  driver with a measured RSSI around -78 dBm. This is not a permanently
  absent RX path and does not prove all directed scans discover that BSS.
- A working 2.4-GHz BSS reached `IO80211BSSBeacon::setBeaconDataFromMsg`
  with -44 dBm and metadata flags `0x6`, but both `toScanResult` and the
  final WCL scan-result return contained RSSI zero. Other cached BSSs showed
  the same loss; the currently connected BSS could retain a nonzero value.
- The CoreWLAN object's backing scan dictionary explicitly contained
  `RSSI = 0`; this was not merely a getter applied to a missing RSSI key.
- The daemon separately logged Location Services denial for BSSID access.
  Kernel scan-result serialization still contained the BSSID. Missing BSSID
  in this diagnostic client is therefore not evidence of a driver BSSID
  serialization defect. No privacy settings or entitlements were changed.

An earlier raw-signal observer attempted an unaligned 32-bit read from a
packed PHY record. DTrace rejected that diagnostic action without modifying
kernel memory. That unused read was removed. The subsequent Apple-consumer
and final-return trace completed with an empty diagnostic-error log.

## Reference evidence

Six bounded functions were decompiled from the saved BootKC project in
read-only mode with 40 configured parallel workers. The existing project's
incorrect no-return annotation on the clock routine truncated some C output
despite a successful decompiler status. Those truncated bodies are not used
as complete evidence; the corresponding raw instruction listings resolve
the missing fallthrough.

The saved reference `setBeaconDataFromMsg` at `0xffffff8002253652` first
applies `isNewBssBetter(rssi, isRssiOnChannel)`. Its bit-14 branch at
`0xffffff8002253736` gates the write of metadata RSSI (+0x30) to the BSS
RSSI field (+0x280), together with the RSSI timestamp (+0x2c8). With bit
`0x4000` clear, that write is skipped. The scan-result serializer copies
that stored value to the public result at +0x16.

`isNewBssBetter` independently consumes bit `0x40` as the on-channel
measurement property and protects recent on-channel samples from
off-channel replacement. Neither property is a reason to fabricate a
measurement or remove channel validation.

The current scan builder supplies a real RSSI scalar but only the base/SSID
flags and optional noise flag. This explains the observed missing RSSI
update at the Apple consumer. It is separate from the physical scan-band
handoff fixed and released in `549114c2`.

## Next correction and claim boundary

The next functional correction must carry measured-RSSI validity through
the metadata producer and distinguish a newly received sample from replay
of an older cache entry. Blindly setting the bit on every node in every
census would refresh Apple's RSSI timestamp for stale observations.
The node's monotonic beacon/probe timestamp is available, but background
scans do not all reset `ni_inact`; that counter alone is not a sufficient
freshness witness. Snapshot/publication ownership must remain intact if
a queued result is cancelled before delivery.

IWN, IWM's two receive paths and IWX supply the physical RX channel to the
shared net80211 parser, which rejects advertised/received-channel mismatch.
The shared producer correction must preserve that evidence and include
the current-BSS metadata path, not just the one IWN test call.

No RSSI production correction is implemented or claimed qualified yet.
The missing 5-GHz candidate and public `-3912` issues remain open; this
observation alone does not establish that fixing metadata resolves them.
