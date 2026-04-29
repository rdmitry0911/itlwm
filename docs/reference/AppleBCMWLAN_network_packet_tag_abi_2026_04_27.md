# AppleBCMWLAN Network Packet And RX Tag ABI

Date: 2026-04-27

Scope: active primary-STA Skywalk RX/input ABI layer after CR-161 packet-pool
network-type restoration.

## Reference Evidence

`AppleBCMWLANPCIeSkywalkRxCompletionQueue::enqueuePackets(...)` is the producer
side for the RX completion batch. The recovered PCIe implementation removes
packets from its staged list, obtains the packet data virtual address and data
offset, and then calls the bound interface input slot.

For normal infrastructure roles the call shape is:

```text
interface_input(interface, packet, packet_scratch, ether_header, false, false)
```

Before the call the producer reads `packet_scratch+0x18`, maps it through a
service-class table, and writes the result to `packet_scratch+0x29`.

`IO80211InterfaceMonitor::logRxCompletionPacket(...)` is a downstream consumer
on the `IO80211InfraInterface::inputPacket(...)` path. It reads:

- `packet_info_tag+0x18` as TID and rejects values above `7`.
- `packet_info_tag+0x14` as the monitor gate / completion marker.

Apple's PCIe packet object stores its scratch pointer at packet `+0x78`.
`AppleBCMWLANPCIeSkywalkPacket::free()` frees that scratch with size `0x98`,
and `AppleBCMWLANPCIeSkywalkPacket::prepare()` clears the first `0x30` bytes.

## Local Divergence Before CR-162

The local `IOSkywalkNetworkPacket` declaration inherited directly from
`IOService` and re-declared generic packet methods in the wrong class. Tahoe's
Skywalk header defines `IOSkywalkNetworkPacket` as an `IOSkywalkPacket`
subclass with only the network-packet extensions.

The local `packet_info_tag` declaration was empty. Passing such a tag into the
reference IO80211 input path would be ABI-invalid because proven consumers
dereference at least offsets `+0x14`, `+0x18`, and `+0x29`.

## Local Alignment

`IOSkywalkNetworkPacket` now matches the Tahoe Skywalk header shape:

- inherits `IOSkywalkPacket`;
- exposes `withPool(...)`;
- overrides only `getPacketType()`;
- declares the network metadata accessors used by the IO80211/Skywalk
  contract.

`packet_info_tag` now records the recovered partial scratch-compatible ABI:

- offset `+0x14`: monitor/completion marker field;
- offset `+0x18`: TID byte;
- offset `+0x29`: service-class byte populated by the RX completion producer;
- total size `0x98`.

The field names are local documentation names. The offsets, size, and consumer
uses are decompile-proven. This batch intentionally does not add a manual
`inputPacket(...)` callback or synthesize RX completion delivery; it only
restores the ABI layer needed before that path can be safely aligned.
