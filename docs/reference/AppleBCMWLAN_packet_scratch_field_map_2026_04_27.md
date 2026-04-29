# AppleBCMWLAN Packet Scratch Field Map

Date: 2026-04-27

Scope: packet scratch/tag ABI fields used by AppleBCMWLAN PCIe packet methods.

## Reference Evidence

`AppleBCMWLANBusInterfacePCIeMac_decompiled.c` shows
`AppleBCMWLANPCIeSkywalkPacket` storing a scratch pointer at packet offset
`+0x78`. The scratch allocation size is `0x98`.

Recovered scratch field uses:

- `+0x18`: RX TID, later mapped by the RX completion producer.
- `+0x1c`: TX VLAN tag (uint32_t, byteswapped). Written by
  `IO80211InfraInterface::logTxPacket(...)` at `0xffffff80022e3896`:
  `*(uint *)(param_3 + 0x1c) = (uint)(ushort)(*(ushort *)(lVar4 + 0x40)
   << 8 | *(ushort *)(lVar4 + 0x40) >> 8);`
- `+0x22`: RX VLAN tag (uint16_t, byteswapped). Written by
  `IO80211InfraInterface::inputPacket(...)` at `0xffffff80022e3f20` and
  by `AppleBCMWLANLowLatencyInterface::inputPacket(...)` override before
  forwarding to base. Read by
  `IO80211PeerManager::skywalkInputPacket(...)` at `0xffffff80021dd7b4`
  for RX log line construction.
- `+0x24`: RX drop log marker (uint32_t). Cleared by
  `IO80211PeerManager::inputPacket(...)` at `0xffffff80021d1424` and on
  the RxDrop branch of `IO80211PeerManager::skywalkInputPacket(...)`.
- `+0x28`: TX accounting byte / per-AC meta byte mask (uint8_t).
  Cleared then conditionally written by
  `IO80211InfraInterface::logTxPacket(...)`:
  `bVar3 = (byte)((ushort)*(undefined2 *)(lVar4 + 0x3e) >> 0xc) & 8;
   *(byte *)(param_3 + 0x28) = bVar3;` with override
  `if (*pbVar1 == 0) *(undefined1 *)(param_3 + 0x28) = 0x76;`.
- `+0x29`: service class written by the RX completion producer.
- `+0x48`: bus address, cleared by packet completion.
- `+0x50`: virtual address, cleared by packet completion and returned by
  `getVirtualAddress()`.
- `+0x74`: packet signature; new packets set this to `0xdeadbeef`.
- `+0x80`: TX status.
- `+0x8a`: flow queue index.
- `+0x90`: low nibble WME AC and high bit duplicate-packet marker.

`IO80211InterfaceMonitor::logRxCompletionPacket(...)` also consumes the
`packet_info_tag` pointer passed to IO80211 input and reads the monitor marker
at `+0x14`, TID at `+0x18`, and service class path using `+0x29`.

## CR-174 Field Expansion

CR-174 expands the named field map by promoting four offsets out of
`reserved19[0x10]` (range `+0x19`..`+0x28`):

- `tx_vlan_tag` at `+0x1c` (uint32_t)
- `rx_vlan_tag` at `+0x22` (uint16_t)
- `rx_drop_marker` at `+0x24` (uint32_t)
- `ac_meta` at `+0x28` (uint8_t)

The struct size remains `0x98`. No runtime code path changes — only the
header struct names and `static_assert` checks are added. Generated
binary is bit-identical to CR-173 (sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`,
UUID `BA3D771F-F079-33FF-94E5-C792E66237D8`).

## Explicit `packet_info_tag *` Handoff Contract

Cross-decompile audit confirms the IO80211 input chain takes
`packet_info_tag *` as an explicit pointer parameter, not as
packet-owned scratch:

- `IO80211InfraInterface::inputPacket(IO80211NetworkPacket *,
  packet_info_tag *, ether_header *, bool *, bool)`
  (`0xffffff80022e3f20`) — `packet_info_tag *` is parameter 3.
- `IO80211PeerManager::inputPacket(IO80211NetworkPacket *,
  packet_info_tag *, ether_header *, bool *)`
  (`0xffffff80021d1424`) — `packet_info_tag *` is parameter 3.
- `IO80211PeerManager::skywalkInputPacket(IO80211NetworkPacket *,
  IO80211Peer *, packet_info_tag *, ether_header *, bool, bool, bool *,
  bool)` (`0xffffff80021dd7b4`) — `packet_info_tag *` is parameter 4.
- `AppleBCMWLANLowLatencyInterface::inputPacket(...)`
  (`0xffffff8001563fac`) — overrides base, enriches `tag+0x22` (RX VLAN)
  before forwarding to base.

Local code already passes `packet_info_tag *tag` through
`AirportItlwmSkywalkInterface::inputPacket(...)` to
`IO80211InfraInterface::inputPacket(...)`. The explicit-pointer handoff
contract is satisfied without packet-owned scratch.

## Local Alignment

The local `packet_info_tag` declaration now names the proven scratch fields
while preserving the recovered total size `0x98` and all previously asserted
offsets. This does not change packet allocation, packet ownership, RX/TX
behavior, or IO80211 input ordering.

## Rejected Direct Subclass Path

A direct local C++ subclass of `IO80211NetworkPacket` was tested and rejected
before submission. The kext linked, but BootKC symbol verification failed
because the generated subclass vtable referenced non-exported
`IOSkywalkPacket::*` virtual method symbols. That means the Apple packet
subclass cannot be restored by naive C++ inheritance in this kext without a
different proven construction strategy.

This batch therefore records only the proven scratch field map. It does not
synthesize the Apple packet subclass, write `packet+0x78`, force EAPOL/key/RSN
success, retry, replay, delay, poll, synthesize packets, or mask deauth.
