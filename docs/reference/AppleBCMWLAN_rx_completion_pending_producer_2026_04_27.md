# AppleBCMWLAN RX Completion Pending Producer

Date: 2026-04-27

Scope: active primary-STA RX completion producer boundary after CR-164 runtime.

## Runtime Correction

CR-164 after-fix runtime showed that local RX EAPOL frames reached the local
RX path, but the registered RX action still did not run:

- `ITLWM_EAPOL path=rx stage=enqueue-ok` repeated.
- `skywalkRxAction(...)` did not appear.
- `ITLWM_IO80211_INPUT` did not appear.
- WCL later left the network after `Update Bss fail`.

This proves that local direct calls to base
`IOSkywalkRxCompletionQueue::enqueuePackets(...)` are not the producer-action
boundary.

## Reference Evidence

`IOSkywalkRxCompletionQueue::requestEnqueue(...) @ 0xffffff8002a59c4c`
calls the registered action. The action receives the owner, queue, a
Skywalk-provided packet array, available packet count, and refcon. The returned
value is the produced packet count.

`IOSkywalkRxCompletionQueue::enqueuePackets(...) @ 0xffffff8002a59cda /
0xffffff8002a59d84` publishes packets through the base networking/KPipe path
and does not invoke the registered action.

`AppleBCMWLANPCIeSkywalkRxCompletionQueue::enqueuePackets(...) @
0xffffff80014ca8e4` is the Apple producer action body. It drains the
Apple owner-side pending RX list, obtains packet data address and offset, maps
tag TID `+0x18` to service class `+0x29`, calls the interface input slot with
packet/tag/header/null-accepted/false, writes produced packet pointers into the
provided array, and returns produced count.

## Local Alignment

The local Tahoe path now mirrors that architecture:

- `skywalkRxInput(...)` prepares a packet and stages it in a fixed-capacity
  pending RX producer queue.
- `skywalkRxInput(...)` rings `fRxQueue->requestEnqueue(nullptr, 0)`.
- `skywalkRxAction(...)` pops pending packets, performs the IO80211 input
  handoff, fills the provided packet array, and returns produced count.
- teardown drains pending prepared packets before releasing the RX pool.

The change does not force accepted success, EAPOL TX, key install, RSN done,
DHCP/IP/data/link success, retry, replay, duplicate notify, or deauth masking.
