# AppleBCMWLAN IOSkywalkNetworkPacket Size ABI

Date: 2026-04-27

Scope: packet-class ABI foundation for the active STA EAPOL/RSN blocker and
for the next Apple PCIe packet scratch restoration layer.

## Reference Evidence

`IOSkywalkFamily_decompiled.c` registers `IOSkywalkNetworkPacket` with instance
size `0x78` at multiple metaclass construction sites:

- `FUN_ffffff8002a33e56`: `IOSkywalkNetworkPacket`, size `0x78`
- `FUN_ffffff8002a33f34`: `IOSkywalkNetworkPacket`, size `0x78`
- `FUN_ffffff8002a346fe`: `IOSkywalkNetworkPacket`, size `0x78`

`IO80211Family_decompiled.c` shows `IO80211NetworkPacket` constructors entering
the `IOSkywalkNetworkPacket` constructor chain and then installing the
IO80211 packet vtable. The same file deallocates `IO80211NetworkPacket` with
size `0x78`.

`AppleBCMWLANBusInterfacePCIeMac_decompiled.c` shows the first subclass layer
above `IO80211NetworkPacket`:

- `AppleBCMWLANPCIeSkywalkPacket` metaclass size is `0x80`;
- its constructor calls the `IO80211NetworkPacket` constructor chain before
  installing the Apple packet vtable;
- Apple packet methods read/write a scratch pointer at packet offset `+0x78`;
- the scratch object size is `0x98`.

This proves that `+0x78` is subclass-owned storage in the Apple PCIe packet
object. It is not storage owned by `IOSkywalkNetworkPacket` or
`IO80211NetworkPacket`.

## Local Divergence Before CR-171

The local `IOSkywalkNetworkPacket` declaration contained an extra protected
`void *mReserved` member. Since the base `IOSkywalkPacket` layout is already
`0x78`, that extra member made the local network-packet declaration `0x80`.

That mismatch blocks exact restoration of the Apple packet subclass ABI. If a
local subclass is built while the base declaration is `0x80`, the subclass
scratch pointer would move to `+0x80` instead of the reference `+0x78`.

## Local Alignment

The tracked local `IOSkywalkNetworkPacket` declaration now contains no packet
storage beyond the `IOSkywalkPacket` base. A compile-time assertion checks that
`sizeof(IOSkywalkNetworkPacket) == 0x78`. The ignored local `MacKernelSDK`
mirror was inspected only as a source/header witness and is not part of the
tracked CR diff.

This batch does not synthesize the Apple PCIe packet subclass, scratch pointer,
scratch-dependent log methods, EAPOL TX, key install, RSN success, DHCP, link,
retry, replay, delay, poll loop, packet synthesis, or deauth masking. It only
restores the proven base-class size contract required before those upper layers
can be restored safely.
