# AppleBCMWLAN Skywalk Packet Pool Network Type

Date: 2026-04-27

Scope: active primary-STA Skywalk RX/TX packet pool layer.

## Reference Evidence

- `AppleBCMWLANSkywalkPacketPool::initWithName(char const*, OSObject*, AppleBCMWLANSkywalkPoolOptions const*, CCLogStream*, CCFaultReporter*)`
- Address: `0xffffff80016e033c` in `/tmp/AppleBCMWLANCoreMac`.
- The function copies the caller pool options into a local `IOSkywalkPacketBufferPool::PoolOptions` shape, then calls the parent init slot with `ecx = 1`.
- In the Tahoe Skywalk headers, `1` is `kIOSkywalkPacketTypeNetwork`.

Relevant disassembly excerpt:

```asm
ffffff80016e0388  movq 0x80e59(%rip), %rax
ffffff80016e038f  movl $0x1, %ecx
ffffff80016e0394  callq *0x130(%rax)
```

BootKC symbol evidence for the downstream contract:

- `IO80211InfraInterface::inputPacket(IO80211NetworkPacket*, packet_info_tag*, ether_header*, bool*, bool)`
- `IO80211PeerManager::skywalkInputPacket(IO80211NetworkPacket*, IO80211Peer*, packet_info_tag*, ether_header*, bool, bool, bool*, bool)`
- `IOSkywalkNetworkPacket::withPool(IOSkywalkPacketBufferPool*, IOSkywalkPacketDescriptor*, unsigned int)`

## Local Divergence Before CR-161

`AirportItlwm` created both TX and RX pools with packet type `0`, which is
`kIOSkywalkPacketTypeGeneric`.

```cpp
IOSkywalkPacketBufferPool::withName("AirportItlwm-TX", fNetIf, 0, &poolOpts);
IOSkywalkPacketBufferPool::withName("AirportItlwm-RX", fNetIf, 0, &poolOpts);
```

Runtime evidence from CR-112/CR-113 showed local RX EAPOL packet enqueue
success, but no `ITLWM_IO80211_INPUT` marker, no EAPOL TX, and no key install.
The first missing boundary after local RX enqueue is therefore the IO80211
network-packet input path.

## Local Alignment

Use `kIOSkywalkPacketTypeNetwork` for both local TX and RX packet pools. This
restores the reference packet-pool class contract without adding manual
callbacks, replay, retries, forced state, or a guessed custom
`IO80211NetworkPacket` subclass.
