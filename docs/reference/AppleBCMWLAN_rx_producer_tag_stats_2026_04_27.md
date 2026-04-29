# AppleBCMWLAN RX Producer Tag And Accounting Closure

Date: 2026-04-27

Scope: active primary-STA Skywalk RX completion producer after RX pending
producer restoration.

## Reference Evidence

`AppleBCMWLANPCIeSkywalkRxCompletionQueue::enqueuePackets(...)`
at `0xffffff80014ca8e4` is the RX completion producer action. For each
produced normal infrastructure packet it:

- removes a packet from the owner-side staged list;
- reads packet data virtual address and data offset;
- reads the packet scratch/tag pointer from the packet object;
- maps the TID byte at scratch offset `+0x18` into service class byte
  `+0x29`;
- calls the interface input slot with packet, scratch/tag, ethernet header,
  null accepted pointer, and `false`;
- stores the produced packet in the Skywalk-provided output array.

After the batch it calls:

- `IO80211SkywalkInterface::recordInputPacket(int, int)`
  at `0xffffff8002277c96` with produced packet count and byte count;
- the interface RX-counter virtual slot, recovered locally as
  `updateRxCounter(unsigned long long)`, with produced packet count.

## Local Constraint

The local RX pool now uses `kIOSkywalkPacketTypeNetwork`, which yields the
system `IOSkywalkNetworkPacket` class. The Tahoe class size is `0x78`, so a
raw write to `packet+0x78` would write past the generic packet object. Apple's
`packet+0x78` scratch pointer belongs to the Apple PCIe packet subclass, not to
the generic system packet object. Local code must therefore not synthesize the
Apple subclass field by raw offset.

## Local Alignment In This Batch

The local RX pending producer now carries a recovered `packet_info_tag` and
packet length alongside each staged RX packet. `skywalkRxAction(...)` pops the
metadata with the packet and passes the recovered tag to
`inputPacket(...)`.

The producer now also mirrors the reference post-batch accounting edge:

- sums produced RX bytes;
- calls `recordInputPacket(produced, producedBytes)`;
- calls `updateRxCounter(produced)`.

This is limited to the RX completion producer boundary. It does not force
accepted success, EAPOL TX, key install, RSN completion, DHCP, link state,
retries, replay, duplicate notification, or deauth masking.
