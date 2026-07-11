# CR-479 LQM Sink-Only Update Boundary

Date: 2026-07-08

## Scope

`AirportItlwmSkywalkInterface::postLqmUpdateBulletin()` originally used two
paths after walking the Tahoe WCL object graph:

- normal path: call `WCLNetManager::handleLqmUpdate(...)` when the NetManager
  state sink is present;
- fallback path: write timestamps directly to private NetManager state offsets
  `+0x150/+0x158` when the sink pointer was absent.

The direct private-offset write is not an Apple producer contract. Runtime
DTrace on the stable associated system showed the real sink path is present and
active (`WCLNetManager::handleLqmUpdate` entries during association), so the
local bridge should fail closed when that sink is absent instead of fabricating
private state.

## Diagnostic Closure

The intermediate LQM update bridge required `nm`, `S`, and `sink` to be kernel
pointers. When the sink was absent it returned without mutation. When the sink
was present it built the recovered LQM payload and called
`WCLNetManager::handleLqmUpdate(...)`.

## Historical Intermediate Closure

That direct C++ terminal call was a stable diagnostic closure, but not the final
Apple producer boundary. It is now replaced by the recovered
`WCLGlue::receiveMessageInternal` shape:

- `msgWord0 = (APPLE80211_M_RSSI_CHANGED << 16) | 2`;
- sender manager id `13` (`WCLBulletinBoardManagerDriver`);
- delivery through `WCLBulletinBoard::sendMessage(...)`, preserving the
  bulletin-board bitmap/subscription gate before WCLNetManager dispatch.

This transport shape was still not an Apple producer contract. The local code
walked undocumented private pointers, constructed a `0x1dc` event with
fabricated nonzero counters, and published it from link-up, WCL link-state, and
the one-second driver watchdog. Neither the payload nor its cadence came from
the framework LQM measurement owner.

## 2026-07-11 Final Correction

Current 25C56 decompilation identifies the actual ownership boundary:

- `AppleBCMWLANCore::postLQMEvent(...)` at `0xffffff80015614fa` reads the
  core-owned Infra message endpoint and, only when it exists, forwards event
  `0x27` with the caller's `0x1dc` payload and flag `1`;
- `IO80211LQMData::postLQMEvent(...)` maps to the LQM data owner's private
  state, not to a public bulletin-board object graph;
- `IO80211LinkQualityMonitor::measurementTimeoutCallback(...)` at
  `0xffffff80022e263a` performs the monitor-owned measurement/update sequence,
  checks its accumulated validity fields, publishes through owner virtuals,
  and only then resets the measurement window.

The exact current artifact is
`~/Projects/ghidra_output/aiam_lqm_real_producers_25C56_20260711.c` on
`10.7.6.112`. The Apple core entry was recovered by matching the complete KDK
function signature; the unique event/length tail is at the same `+0x1a`
offset. The resulting match list is
`~/Projects/ghidra_output/aiam_lqm_signature_matches_25C56_20260711.txt`.

The local synthetic producer is therefore removed in full:

- delete `postLqmUpdateBulletin()` and its link-up, WCL update, and watchdog
  call sites;
- delete the private WCL pointer walk, fabricated payload/counters, and direct
  `WCLBulletinBoard::sendMessage(...)` declaration;
- retain only independently recovered LQM config and real link-changed signal
  helpers.

At this removal stage no substitute producer was added. Restoring LQM updates
required the reference driver-owned timer, real measurements, and Infra
endpoint rather than another terminal transport shortcut. That owner was
subsequently recovered and is documented in
`CR-479-driver-owned-lqm-statistics-producer-20260711.md`.

## Historical Runtime Evidence

- Clean baseline `069a57f` was re-materialized first and held a 240 second
  ping plus TCP iperf3 run: 240/240 ping, 0% loss; iperf3 1.34 GB at about
  48.0 Mbit/s.
- This change was then materialized as kext UUID
  `F37F4D56-BE40-3B1C-BE56-A6CB5099B0B9`, CDHash
  `c397e6b08b5749b4d34f2829899355f7e2d07f9f`.
- DTrace saw `WCLNetManager::handleLqmUpdate` fire 30 times in a 30 second
  associated window.
- The changed build held the same 240 second ping plus TCP iperf3 run:
  240/240 ping, 0% loss; iperf3 1.34 GB at about 48.0 Mbit/s.
- The driver-sender bulletin-board replacement was materialized as CDHash
  `dc8e89f3f26ec823ef10f374315654aa33928b46`. It associated, held a 240
  second ping plus TCP iperf3 run without CoreCapture/missed-beacon/panic
  faults, and completed iperf3 with 821 MB received at 28.6 Mbit/s. The
  concurrent ping process completed with 235/240 replies under saturated TCP
  load; a follow-up unloaded ping immediately after the run completed 10/10 at
  sub-millisecond average latency.

Those historical runs show only that the synthetic traffic did not immediately
break the associated data path. They do not validate payload provenance,
producer cadence, or parity with the framework LQM owner.

The final owner implementation is separate from that historical evidence.
Clean build UUID `09663B25-365D-3D90-BE59-D50490351847` removed every
private-WCL producer and delivered 50 real event `0x27` updates to
`WCLNetManager::handleLqmUpdate` over a 250-second DTrace window at the
recovered 5000 ms cadence. Its concurrent 240-second ping and iperf3 gate
completed with 240/240 replies, zero loss, and 572 MB at 20.0 Mbit/s without
CoreCapture, missed-beacon, panic, host AER, or IOMMU faults.

## Non-Claims

The synthetic-producer removal itself does not force CARD_CAPABILITIES or the
slow-wifi policy bit. Those gates remain on the policy recorded in
`CR-479-lqm-create-prerequisites-20260707.md`. The subsequent exact owner
producer does not change public CoreWLAN, `networksetup`, Dynamic Store, or
AP/GO behavior.
