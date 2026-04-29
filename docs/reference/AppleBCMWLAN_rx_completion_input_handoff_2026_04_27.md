# AppleBCMWLAN RX Completion Input Handoff

Date: 2026-04-27

Scope: active primary-STA Skywalk RX completion to IO80211 input edge.

## Reference Evidence

`AppleBCMWLANPCIeSkywalkRxCompletionQueue::enqueuePackets(...)` is the
producer-side callback that drains staged RX packets. For each normal
infrastructure-role packet it:

- obtains packet data virtual address through the packet vtable;
- obtains packet data offset through the packet vtable;
- reads the packet scratch/tag pointer;
- maps scratch/tag TID at `+0x18` into service class at `+0x29`;
- calls the bound interface input slot with packet, scratch/tag,
  `dataVirtualAddress + dataOffset`, `nullptr` accepted pointer, and `false`.

The downstream `IO80211InfraInterface::inputPacket(...)` implementation is
synchronous at this boundary: it logs the RX completion packet through the
interface monitor when present, resolves the peer, and calls
`IO80211PeerManager::skywalkInputPacket(...)`.

## Local Divergence Before CR-163

The local `skywalkRxInput(...)` copied mbuf data into a Skywalk packet and
enqueued it to `fRxQueue`, but the registered RX completion action only
incremented a diagnostic counter and returned `count`.

That meant the exact reference producer-side edge after `enqueuePackets(...)`
was absent locally. Runtime evidence matched this: EAPOL RX enqueue was logged,
but the `AirportItlwmSkywalkInterface::inputPacket(...)` probe was never
reached.

## CR-164 Runtime Correction

After-fix runtime on 2026-04-27 disproved the local assumption that calling
`IOSkywalkRxCompletionQueue::enqueuePackets(...)` would invoke the registered
completion action.

Observed local runtime:

- `ITLWM_EAPOL path=rx stage=enqueue-ok` appears for repeated RX EAPOL frames.
- `skywalkRxAction(...)` is not invoked.
- `ITLWM_IO80211_INPUT` remains absent.
- WCL later reports BSS update failure and leaves the network.

The adjacent IOSkywalkFamily decompile explains the mismatch:

- `IOSkywalkRxCompletionQueue::requestEnqueue(...)` is the path that calls the
  registered action.
- the registered action fills the Skywalk-provided packet array and returns the
  produced packet count.
- base `IOSkywalkRxCompletionQueue::enqueuePackets(...)` publishes packets to
  the networking side, but does not call the registered action.

Therefore the Apple `AppleBCMWLANPCIeSkywalkRxCompletionQueue::enqueuePackets`
symbol is the producer action body: it drains Apple's owner-side pending RX
list, performs the IO80211 input handoff, writes produced packets into the
array passed by IOSkywalk, and returns the produced count.

## Local Alignment

`skywalkRxAction(...)` now performs the IO80211 input handoff for packets that
arrive at the RX completion boundary:

- validates packet data pointer, offset, and length;
- builds the ethernet header pointer from `dataVirtualAddress + dataOffset`;
- uses the recovered `packet_info_tag` staged with the pending RX packet;
- calls `AirportItlwmSkywalkInterface::inputPacket(...)` with a null accepted
  pointer and `false`, matching the reference call shape.

The corrected local alignment is:

- `skywalkRxInput(...)` only prepares a packet from the mbuf and stages it in a
  fixed-capacity local pending RX producer queue.
- `skywalkRxInput(...)` rings `fRxQueue->requestEnqueue(nullptr, 0)`.
- `skywalkRxAction(...)` is the producer action: it pops staged packets, derives
  `ether_header` from `dataVirtualAddress + dataOffset`, uses the staged tag,
  calls `AirportItlwmSkywalkInterface::inputPacket(...)`, stores the produced
  packet into the Skywalk-provided output array, updates IO80211 RX accounting,
  and returns the produced count.

This intentionally does not call input after base `enqueuePackets(...)`, does
not replay or duplicate packets, does not force accepted success, and does not
synthesize EAPOL TX, key install, RSN completion, DHCP, or link success.
