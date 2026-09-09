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

## Production correction and verification boundary

The shared beacon/probe-response parser now records the actual received
RSSI and physical channel with a host-monotonic microsecond sample stamp.
This is separate from the historical `ni_rssi` 5-GHz selection peak, which
is not changed by this fix. A matching current STA node receives the same
measurement; another BSSID/channel or a local AP owner does not inherit it.
Creating a local IBSS/AP clears these received-measurement fields.

The WCL metadata builder publishes measured RSSI with bit 14 and its verified
on-channel provenance with bit 6. Missing, invalid and channel-mismatched
measurements remain absent. The value-only terminal snapshot also carries
an exact sample witness. Immediately before publication, a locked node lookup
revalidates it; after the publication call, the still-matching sample is
marked issued. Snapshot collection and a cancelled/suppressed terminal do
not consume it. A duplicate or old snapshot cannot refresh the RSSI timestamp
after its sample was issued or replaced by a newer RX. No node pointer is
retained across the publication gate. As elsewhere, the void postMessage
return proves only that publication was issued, not that the consumer accepted it.

The current-BSS builder initializes a separate Apple beacon object and therefore
includes its actual measured value independently of the scan-cache receipt.
It does not fabricate a measurement from an uninitialized selection scalar.

`scripts/test_scan_rssi_publication.sh` compiles the production RX recorder,
locked publication witness, both metadata producers and terminal collector
under ASan/UBSan. Cases cover fresh and repeated samples, cache replays,
cancelled and duplicate snapshots, newer RX between collection/publication,
removed/replaced identities, invalid RSSI, physical-channel mismatch, 2.4/5-GHz
values and current-BSS/AP-owner isolation. The negative control compiles the
old metadata producers and fails their missing measured-RSSI assertion.
Existing physical-scan lifecycle, exact-plan and SSID-refresh checks also pass.

## Matching-image runtime qualification

Source `150d7e73` built with all 1083 BootKC symbols resolved. Private AuxKC
admission passed; transactional activation retained the four companion
members. The disposable IWN/6235 guest loaded UUID
`431DFEA8-4B59-3A52-B4B9-91677A7619B1`, matching frozen Mach-O SHA-256
`7bb7dbd33dadd02303891a7a11e3791670851a020c292bf4e86d45bf232f3c67`.
Saved WPA3/DHCP recovered and passed 5/5 packets.

At 17:07 UTC the actual Apple consumer received measured metadata with
flags `0x4046`, and final `WCLScanManager::serveScanResult` results contained
negative measured RSSI across both bands. The CoreWLAN dictionary/getter
returned -44 dBm for a 2.4-GHz target rather than zero. The first directed
request still returned no target, and 5-GHz target discovery remained
intermittent; those are not claimed fixed.

The first observer's raw inner-object RSSI/timestamp reads used the 26.3
reference layout on the 25C56 guest. Those raw reads are invalid evidence;
the fixed public carrier offsets and CoreWLAN result are independent of them.
The matching guest BootKC symbols were then resolved and its saved 25C56
project inspected read-only with a 40-CPU headless configuration. Its actual
RSSI getter at `0xffffff8002230b6e` reads inner +0x27c; the bit-14 branch in
`setBeaconDataFromMsg` at `0xffffff800222ed22` writes RSSI +0x27c and time
+0x2c0. These differ from the 26.3 private offsets, not from the metadata ABI.

The corrected 17:14 UTC observer completed without diagnostic errors. A fresh
5-GHz sample with `0x4046` initialized RSSI to -81 dBm and advanced its time
from zero. Cached `0x46` messages retained already stored RSSI and exactly
the same timestamp across the call. Current-BSS refreshes also carried the
measured -44 dBm value. This verifies actual consumer admission and the
non-refreshing cache-replay property, not just the producer's populated scalar.

Three credentialed public network selections completed in 10, 9 and 9
seconds without printed join errors. Each immediately following five-packet
check lost its first packet, then passed four. This is recovery, not seamless
reassociation or proof that the historical `-3912` surface is eliminated.

Concurrent WPA3 STA plus role-7 SAE/required-PMF AP passed 20/20 client-to-AP,
5/5 AP-to-client and 5/5 primary-to-gateway packets. `pmset sleepnow` at
17:11:28 UTC reached actual S3 (QEMU suspended and serial `ACPI SLEEP`). The
owned monitor woke it at 17:12:33; `ACPI S3 WAKE` followed, with unchanged
boot epoch and loaded UUID. Explicit client reselection completed SAE group
19, PMF and BIP; all three traffic checks passed again. The AP used static
addresses: this does not requalify DHCP or automatic AP-client continuity.
AP stop reached the real lower zero-result terminal at 17:13:41 and primary
traffic passed 10/10. The temporary AP address and client profile were removed,
and the ordinary host profile restored; wired management was unchanged.

Frozen release ZIP SHA-256:
`a8cf54d2499cd6f423876b3f8f3195466f5c8a3b1a3315d96bbea27017fb2443`.
Its extracted Mach-O matches the loaded candidate.

## Remaining adjacent surface

The fresh measured-RSSI admission defect is corrected. Some older cache-only
entries can still produce zero RSSI when an Apple BSS object has no previous
measurement: replay correctly does not relabel an already-issued sample as
fresh, but the broader terminal census still includes those cached identities.
That producer/cache-lifetime boundary needs separate correction; this result
does not claim every cached result now has valid signal data. Missing 5-GHz
candidates, initial open discovery, public/UI reconnect and equivalent recent
IWM/IWX hardware qualification also remain open.
