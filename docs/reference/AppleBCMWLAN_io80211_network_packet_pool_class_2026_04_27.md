# AppleBCMWLAN IO80211 Network Packet Pool Class

Date: 2026-04-27

## Runtime Trigger

The current CONTROL_STA_NETWORK runtime moved past scan visibility and reaches the
IO80211 input boundary:

- `desired_ssid=CONTROL_STA_NETWORK`, `current_ssid=CONTROL_STA_NETWORK`
- `rx=8`, `eapol_rx=8`
- `tx=28`, `eapol_tx=0`
- `hidden_assoc result=0x0`
- `IO80211InfraInterface::inputPacket(...)` returns success for RX EAPOL
- no `setCIPHER_KEY`, no `IO80211RSNDone`
- AP deauth follows with reason 15

This proves that the next blocker is below the driver RX producer and inside
the object/class contract consumed by IO80211.

## Reference Evidence

`AppleBCMWLANPCIeSkywalkPacketPool::newPacketWithDescriptor(...) @
0xffffff80014cb250` does not return a base Skywalk packet. It calls
`AppleBCMWLANPCIeSkywalkPacket::withPool(...)` and then initializes the packet
scratch signature at `packet->scratch + 0x74`.

`AppleBCMWLANPCIeSkywalkPacket::withPool(...) @ 0xffffff80014cb280` allocates a
packet object of size `0x80`. Its constructor chain calls the
`IO80211NetworkPacket` constructor before installing the AppleBCMWLAN packet
vtable.

`AppleBCMWLANPCIeSkywalkPacketPool::allocatePacket(...) @ 0xffffff80014cb8ae`
calls the parent allocation path and then validates the returned object against
the AppleBCMWLAN PCIe packet metaclass. This confirms that the packet pool is an
owner object whose allocation hook is part of the reference contract.

`IO80211NetworkPacket::getPacketType(...) @ 0xffffff80022cf000` parses the
Ethernet frame from the packet virtual address, data offset, and data length:

- EtherType `0x88b4` returns packet type `3`
- EtherType `0x888e` returns packet type `2` for EAPOL
- IPv4 UDP DHCP/bootp conditions return packet type `1`
- other frames return `0`

Downstream IO80211 consumers are typed as `IO80211NetworkPacket*`, including:

- `IO80211InfraInterface::inputPacket(...) @ 0xffffff80022e3838`
- `IO80211PeerManager::inputPacket(...) @ 0xffffff80021d11fc`
- `IO80211PeerManager::skywalkInputPacket(...) @ 0xffffff80021dd58c`

## Local Divergence

Before this batch the local code created base
`IOSkywalkPacketBufferPool::withName(..., kIOSkywalkPacketTypeNetwork, ...)`
pools. That restores the generic network packet type but still does not prove
that allocation returns an `IO80211NetworkPacket`-family object.

The local RX producer then passed the packet into IO80211 through:

`reinterpret_cast<IO80211NetworkPacket *>(pkt)`

That satisfies the C++ signature syntactically, but not the reference object
contract. In the failing runtime, RX EAPOL reaches `inputPacket(...)`, yet no
EAPOL TX/key/RSN progression follows.

## Local Alignment

The local Tahoe packet pool is now an `IOSkywalkPacketBufferPool` subclass whose
`newPacket(...)` allocation hook creates a real system `IO80211NetworkPacket`
via `OSMetaClass::allocClassWithName("IO80211NetworkPacket")`, then calls the
inherited `initWithPool(...)`.

The fix preserves the existing pool options, queue topology, producer actions,
packet buffers, and RX tag carrier. It does not synthesize Apple PCIe scratch,
does not write `packet+0x78`, and does not force EAPOL TX, key install,
`IO80211RSNDone`, DHCP, link, retry, replay, or deauth masking.
