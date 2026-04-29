# AppleBCMWLAN IO80211NetworkPacket Virtual Surface

Date: 2026-04-27

Scope: `IO80211NetworkPacket` ABI layer between `IOSkywalkNetworkPacket` and
the Apple PCIe packet subclass.

## Reference Evidence

`kdk_symbols.txt` exports the Tahoe `IO80211NetworkPacket` class surface:

- metaclass constructors/destructors and allocator;
- `getPacketType()`;
- `getVirtualAddress()`;
- `setPTMMode(bool)` / `isPTMMode() const`;
- `setIngressEgressTimestamp(uint64_t)` /
  `getIngressEgressTimestamp() const`;
- `setPktEnqueueTime(uint64_t)` / `getPktEnqueueTime() const`;
- `firmwareToHostTxStatus(IO80211NetworkTXStatus)`;
- `setFirmwareTxStatus(IO80211NetworkTXStatus)`;
- `getFirmwareTxStatus()`;
- `getBufferSize()`;
- `prepareWithQueue(IOSkywalkPacketQueue*, uint32_t)`;
- `prepareWithQueue(IOSkywalkPacketQueue*, uint32_t, uint32_t)`.

`IO80211Family_decompiled.c` shows the implementation semantics:

- the constructor enters the `IOSkywalkNetworkPacket` constructor chain and
  installs the `IO80211NetworkPacket` vtable;
- the class deallocates with size `0x78`;
- `getPacketType()` parses the Ethernet payload and returns `2` for
  EtherType `0x888e`;
- several timestamp/PTM setters are explicit unsupported stubs;
- timestamp/PTM getters return zero/false;
- firmware TX status maps firmware status values through a small table;
- both `prepareWithQueue(...)` overloads tail-call parent packet preparation
  paths.

`AppleBCMWLANBusInterfacePCIeMac_decompiled.c` shows
`AppleBCMWLANPCIeSkywalkPacket` constructors calling the
`IO80211NetworkPacket` constructor chain before installing the Apple packet
vtable. That makes the IO80211 packet surface the immediate base ABI for any
local Apple packet subclass restoration.

## Local Divergence Before CR-172

The local `IO80211NetworkPacket` declaration was an empty subclass of
`IOSkywalkNetworkPacket`. That was sufficient for CR-170's system-object
allocation and cast, but it was not sufficient for 1:1 restoration of the
Apple packet subclass layer because the local compiler did not see the actual
intermediate virtual/method surface.

## Local Alignment

The tracked local header now declares only the decompile/export-proven
`IO80211NetworkPacket` method surface and asserts the class size remains
`0x78`. No local `IO80211NetworkPacket` object is instantiated and no Apple
PCIe packet subclass is synthesized in this batch.

This does not alter packet allocation, packet contents, scratch ownership,
EAPOL TX, key install, RSN state, DHCP, link state, retry, replay, delay,
polling, packet synthesis, or deauth handling.
