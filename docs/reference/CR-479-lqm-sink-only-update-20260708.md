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

## Current Closure

That direct C++ terminal call was a stable diagnostic closure, but not the final
Apple producer boundary. It is now replaced by the recovered
`WCLGlue::receiveMessageInternal` shape:

- `msgWord0 = (APPLE80211_M_RSSI_CHANGED << 16) | 2`;
- sender manager id `13` (`WCLBulletinBoardManagerDriver`);
- delivery through `WCLBulletinBoard::sendMessage(...)`, preserving the
  bulletin-board bitmap/subscription gate before WCLNetManager dispatch.

## Verification

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

## Non-Claims

This does not restore the framework `IO80211LQMData::postLQMEvent` timer
producer and does not re-enable the unsafe LQM QueueCall prerequisites. The
CARD_CAPABILITIES and slow-wifi LQM gates remain on the stable policy recorded
in `CR-479-lqm-create-prerequisites-20260707.md`.
