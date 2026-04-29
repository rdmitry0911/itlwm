# AppleBCMWLAN TX completion producer

Date: 2026-04-27

## Reference Evidence

Primary decompilation anchors:

- `AppleBCMWLANPCIeSkywalkTxCompletionQueue::stagePacket(...) @ 0xffffff80014c91c0`
- `AppleBCMWLANPCIeSkywalkTxCompletionQueue::enqueuePackets(...) @ 0xffffff80014c8d62`
- `AppleBCMWLANPCIeSkywalkTxCompletionQueue::requestEnqueue(...) @ 0xffffff80014c920c`
- IOSkywalk TX completion enqueue body near `0xffffff8002a3fa9e`

Recovered semantics:

- TX completion packets are first staged into an owner-side list.
- `requestEnqueue(...)` delegates to the IOSkywalk queue producer boundary.
- The producer drains staged packets into the Skywalk-provided packet array and
  returns produced count.
- The IOSkywalk TX completion base path calls packet `completeWithQueue(queue,
  kIOSkywalkPacketDirectionTx, 0)` for produced packets and updates completion
  accounting.

## Local Divergence

Before CR-166, local `skywalkTxAction(...)` copied the packet payload into an
mbuf, called `outputPacket(...)`, returned the packet as consumed, and then left
the original `IOSkywalkPacket` without any TX completion producer path.

`skywalkTxCompletionAction(...)` returned `0` unconditionally, so even if the
completion queue was rung it could not return packet ownership to Skywalk.

## Restored Contract

CR-166 adds a local pending TX-completion producer:

- consumed TX packets are staged after they are accepted by the local TX action;
- the TX completion queue is rung with `requestEnqueue(nullptr, 0)`;
- `skywalkTxCompletionAction(...)` fills the provided packet array from the
  pending producer and returns produced count;
- teardown drains staged completion packets before queue/pool release.

This does not force TX success, link state, key install, RSN completion, DHCP,
internet reachability, retries, replay, polling, or deauth masking. It closes
the packet ownership contract for packets already consumed by the local TX
submission action.
