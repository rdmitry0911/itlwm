# CR-479 BssManager current-BSS identity writer seeding

> **2026-07-11 ownership correction:** The runtime evidence below remains
> historical, but its WCLConfigManager ownership claim and local pointer walk
> are superseded by
> `CR-479-driver-owned-bssmanager-lifecycle-20260711.md`. Apple owns a separate
> driver BssManager; current code never traverses or mutates WCL private state.

Date: 2026-07-07

## Scope

This batch closes the framework-visible current-BSS identity cache writer gap.
The local Tahoe bridge already recovers the framework-owned
`IO80211BssManager *` through the verified WCLConfigManager route and seeds
rate/MCS fields there. The same seed burst now publishes associated SSID and
RSN IE through the direct writers exported by IO80211Family.

## Reference evidence

BootKC anchors from
`10.7.6.112:~/Projects/ghidra_additional/kc_all_symbols.txt`:

- `0xffffff800226713c`
  `IO80211BssManager::setAssocSSID(unsigned char const*, unsigned long)`
- `0xffffff8002267afa`
  `IO80211BssManager::setAssocRSNIE(unsigned char const*, unsigned long)`

Static slices on the decompile host:

- `cr479_bootkc_memory_safe_checkpoint_smoke_20260516T1248/09_static_slices/BootKC_memory_safe/04246_ffffff800226713c_FUN_ffffff800226713c.asm.txt`
- `cr479_bootkc_memory_safe_checkpoint_smoke_20260516T1248/09_static_slices/BootKC_memory_safe/04254_ffffff8002267afa_FUN_ffffff8002267afa.asm.txt`

Recovered behavior:

- `setAssocSSID` initializes the return status to `0xe00002db`, rejects
  length greater than `0x20`, clears the associated-SSID storage, writes the
  length, copies the caller buffer only when length is nonzero, and returns
  success on accepted lengths.
- `setAssocRSNIE` initializes the return status to `0xe00002db`, rejects
  length greater than `0x101`, writes the accepted length, copies the caller
  buffer on nonzero length, and takes a separate zero-length path that clears
  the associated RSN IE storage.

## Local mapping

`AirportItlwmSkywalkInterface::seedBssManagerRateAndMcs()` keeps the existing
WCLConfigManager pointer chain used by the prior BssManager rate/MCS batches.
After the framework-owned `IO80211BssManager *` is recovered, it now calls:

- `setAssocSSID(ni->ni_essid, ni->ni_esslen)` when the current node SSID
  length is within the Apple writer's `0x20` byte limit.
- `setAssocRSNIE(ni->ni_rsnie_tlv, ni->ni_rsnie_tlv_len)` when the current
  node has a nonempty RSN IE within the Apple writer's `0x101` byte limit.
- `setAssocRSNIE(nullptr, 0)` when the current node has no usable RSN IE, using
  the writer's recovered clear path instead of retaining stale framework state.

The patch does not synthesize SSIDs, RSN IEs, auth types, association state, or
current-BSS objects. It only forwards local current-node data through recovered
IO80211Family writer exports with the same length boundaries as the reference.

## Runtime verification

Installed build:

- SHA-256:
  `b662abedf37bce9ea3fb898fbb59a1627ffd272167fbaf785ff57978b891b0b3`
- UUID: `4BAF07A4-7CA4-38FD-9EAF-0BD5D1915F4A`

Post-reboot verification on 2026-07-07:

- loaded UUID matched the installed build;
- manual join to `AIAMlab6235` returned success, `en1` became active with
  `10.77.0.47`, and a 10-packet baseline ping had 0% loss;
- bidirectional 120-second TCP stress plus concurrent 150-packet ping held:
  host-to-guest TCP was about 5.8 Mbps, guest-to-host TCP was about 40 Mbps,
  and ping was 150/150 with 0% loss;
- post-stress 10-packet ping had 0% loss with about 1.1 ms average latency;
- serial tail had the known BT ACPI and LQM QueueCall errors, with no new
  panic or kernel stack corruption entries in the checked tail.

This layer did not close the user-visible association-state discrepancy by
itself: `networksetup -getairportnetwork en1` still reported
`You are not associated with an AirPort network.` while datapath and IP traffic
were active. The remaining state gap is therefore beyond SSID/RSN writer
seeding alone.
