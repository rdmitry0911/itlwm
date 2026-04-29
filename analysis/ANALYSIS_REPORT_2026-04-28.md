# ANALYSIS REPORT - itlwm / macOS Tahoe - 2026-04-28

Continuation of `analysis/ANALYSIS_REPORT_2026-04-23.md`. Same protocol,
same evidence model.

## Context Inherited From Previous Cycle

- Active CR `CR-173` is `PENDING_STRUCTURAL_REVIEW` with no decision file.
- Active runtime blocker remains EAPOL TX / key install / RSN_DONE absent
  after RX EAPOL reaches IO80211 input.
- Direct local C++ subclass of `IO80211NetworkPacket` rejected by BootKC
  symbol verification (9 non-exported `IOSkywalkPacket::*` virtuals).
- Handoff explicitly authorizes batching adjacent confirmed 1:1 divergences
  while CR-173 awaits review.

## A-CR174-PACKET-SCRATCH-RX-TX-VLAN-FIELD-MAP

status: CONFIRMED_DEVIATION

symptom layer: structural reference alignment of `packet_info_tag` /
`PacketSkywalkScratch` field map. Adjacent to CR-173 layer.

reference behavior: Apple packet scratch carries additional decompile-proven
fields beyond the CR-173 set. Each field is read or written by exported
IO80211Family methods on the live RX/TX path that itlwm already participates
in.

### Confirmed Field Offsets (additional to CR-173)

Each is verified by direct read of the system decompile.

`+0x1c` (uint32_t) — TX VLAN tag (byteswapped)
- written by `IO80211InfraInterface::logTxPacket(IO80211NetworkPacket *,
  PacketSkywalkScratch *, apple80211_wme_ac, bool)` at
  `0xffffff80022e3896` in IO80211Family decompile.
- decompile excerpt:
  `*(uint *)(param_3 + 0x1c) = (uint)(ushort)(*(ushort *)(lVar4 + 0x40)
  << 8 | *(ushort *)(lVar4 + 0x40) >> 8);`

`+0x22` (uint16_t) — RX VLAN tag (byteswapped)
- written by `IO80211InfraInterface::inputPacket(IO80211NetworkPacket *,
  packet_info_tag *, ether_header *, bool *, bool)` at
  `0xffffff80022e3f20` and by
  `AppleBCMWLANLowLatencyInterface::inputPacket(...)` override before
  forwarding to base class.
- decompile excerpt:
  `uVar1 = *(ushort *)(lVar6 + 0x40 + (ulong)bVar4);
   *(ushort *)(param_3 + 0x22) = uVar1 << 8 | uVar1 >> 8;`
- read by `IO80211PeerManager::skywalkInputPacket(...)` at
  `0xffffff80021dd7b4` for RX log line construction:
  `local_80 = (uVar16 >> 7) << 0x18 |
   (uint)*(ushort *)((long)param_4 + 0x22);`

`+0x24` (uint32_t) — RxDrop log marker
- cleared by `IO80211PeerManager::inputPacket(...)` at
  `0xffffff80021d1424`:
  `*(undefined4 *)(param_3 + 0x24) = 0;`
- cleared again on RxDrop branch in
  `IO80211PeerManager::skywalkInputPacket(...)`:
  `*(undefined4 *)((long)param_4 + 0x24) = 0;`

`+0x28` (uint8_t) — TX accounting byte (per-AC meta byte mask)
- cleared then written by `IO80211InfraInterface::logTxPacket(...)`:
  `*(undefined1 *)(param_3 + 0x28) = 0;
   bVar3 = (byte)((ushort)*(undefined2 *)(lVar4 + 0x3e) >> 0xc) & 8;
   *(byte *)(param_3 + 0x28) = bVar3;`
- conditional override:
  `if (*pbVar1 == 0) { *(undefined1 *)(param_3 + 0x28) = 0x76; }`

### Implications

These four offsets sit inside the byte range that CR-173 still keeps as
`reserved19[0x10]` (covering `+0x19`..`+0x28`). CR-173's existing fields
end at `service_class +0x29`. The CR-174 expansion removes the trailing
portion of `reserved19` and replaces it with named fields plus narrower
inter-field padding.

CR-174 changes do not alter struct size (still `0x98`), do not affect
runtime ownership, do not synthesize packet-owned scratch, and do not
force any EAPOL/key/RSN/data behavior. They are structural restoration
of the field map already used by exported IO80211Family methods that
itlwm reaches through `AirportItlwmSkywalkInterface::inputPacket(...)`
forwarding to `IO80211InfraInterface::inputPacket(...)`.

### Additional Confirmation: Explicit `packet_info_tag *` Handoff Contract

Cross-decompile audit verifies the handoff is by explicit pointer
parameter, not packet-owned scratch:

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
contract is satisfied. Packet-owned scratch is not part of the
public IO80211 input contract.

### Justification Class

`REFERENCE_ALIGNMENT_FIX` — purely additive field naming + assertions.
No runtime ownership, no behavior change, no forced state. Continues the
exact CR-173 strategy with strict superset of named offsets.

### Fix Plan

- Edit `include/Airport/apple_private_spi.h` to expand `packet_info_tag`
  with the new named fields and narrower reserved-padding bands.
- Add `static_assert(__builtin_offsetof(packet_info_tag, ...) == ...)`
  for each new field.
- Preserve `static_assert(sizeof(packet_info_tag) == 0x98)`.
- Update `docs/reference/AppleBCMWLAN_packet_scratch_field_map_2026_04_27.md`
  with the new offsets.
- Update `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/114_packet_scratch_field_map_2026_04_27.yaml`
  with the new offsets.
- Update `docs/tahoe_signal_chain_audit.md` with a CR-174 closure section.
- Update `docs/tahoe_discrepancy_inventory.md` with item `#174`.
- No code path beyond the struct definition is touched.
- CR-174 SUPERSEDES CR-173.

## Carryover Audit Findings (NOT included in CR-174)

The following are documented for the next audit cycle. They are not
included in CR-174 because none of them is yet at `CONFIRMED_DEVIATION`
with a fully proven fix path.

### Peer / Link-State Contract Gap (status CORRELATED)

itlwm currently has no references to `IO80211Peer`, `addPeer`,
`setLinkState`, `setCurrentApAddress`, or `getInfraPeer` symbols.

The system path for RX delivery is:

- `IO80211InfraInterface::inputPacket(...)` →
  `IO80211PeerManager::skywalkInputPacket(...)` →
  per-peer accept (vtable slot `+0x168`) →
  upstream chain into supplicant socket.

`IO80211PeerManager::skywalkInputPacket(...)` checks `peer != 0`. If
peer is null, it logs `"Peer not found.. mpeer=%p"` and short-circuits
the per-peer accept. The runtime evidence `eapol_rx=8`, `eapol_tx=0`,
`IO80211RSNDone=No` is consistent with RX EAPOL counted at our driver
boundary but never reaching the supplicant socket because no peer is
registered.

Exported building blocks observed (verified in
`/srv/project/ghidra_additional/kc_target_symbols.txt`):

- `IO80211PeerManager::addPeer(unsigned char *)` at `0xffffff80021d3f58`.
- `IO80211PeerManager::addPeerOperation()` at `0xffffff80021d7ba0`.
- `IO80211InfraInterface::setLinkState(IO80211LinkState, unsigned int,
  bool, unsigned int, unsigned int)` at `0xffffff80022df28c`.
- `IO80211InfraInterface::setCurrentApAddress(ether_addr *)` at
  `0xffffff80022e5e40`.
- `IO80211InfraInterface::getInfraPeer()` at `0xffffff80022e1148`.
- `IO80211InfraPeer::withAddressAndPeerManager(...)` at
  `0xffffff80021bdc4c`.

Open gating questions (must be answered by decompile before any patch):

- exact ordering: `addPeer` before `setLinkState`, or after, or
  framework-internal during one of them.
- exact peer flavor: `IO80211InfraPeer` vs base `IO80211Peer`.
- exact lifecycle owner: BCM driver invokes through WCL bridge or
  directly through exported InfraInterface helpers.
- exact precondition: who computes `apple80211_infra_peer_address_data`
  and what fields are required.

Until those gates are decompile-confirmed, no peer/link-state patch is
allowed under protocol. Intermediate audit task captured for the next
cycle.

### Key / RSN Done Contract Gap (status OBSERVED)

`IO80211InfraInterface::handleKeyDone(bool, bool)` at
`0xffffff80022e6f9c` is the system-side handler that records
`IO80211RSNDone`. Ghidra fails on this address with
`"Bad instruction - Truncating control flow"`, so the body is not
directly readable from the existing `IO80211Family_decompiled.c`.

Recovery requires either:
- raw disassembly via `otool -tV` / `objdump -D` on the system kext, or
- a fresh Ghidra targeted decompile run on the address.

Until then, only the symbol existence is confirmed. No fix is allowed.

## Build / Submit Plan For CR-174

1. Edit `include/Airport/apple_private_spi.h` with expanded field map.
2. Edit reference doc.
3. Edit YAML 114.
4. Run `./scripts/build_tahoe.sh` (must pass BootKC symbol verification).
5. Run `./scripts/build_regdiag.sh`.
6. Generate `commit-approval/artifacts/CR-174-packet-scratch-rx-tx-vlan-batch.diff`.
7. Generate `commit-approval/build_evidence/...` with sha256.
8. Submit `commit-approval/requests/CR-174-packet-scratch-rx-tx-vlan-batch.md`
   superseding CR-173.
9. Stop and wait for reviewer decision.

## CARRYOVER PRE-RESEARCH (NOT IN CR-174)

This section captures decompile reads completed after CR-174 was packaged, to
accelerate the next cycle. None of these is yet at CONFIRMED_DEVIATION.

### IO80211PeerManager::addPeer body (FUN_ffffff80021d3f58)

Signature: `addPeer(IO80211PeerManager *param_1, unsigned char *addr) -> long`

Key behaviors:
- Returns `0x1502` if `addr == NULL`.
- If `param_1[3] + 0x530` (current single-peer slot) is non-NULL:
  - If `addr[0] & 1` is set, returns existing peer unchanged.
  - Otherwise overwrites the cached MAC at peer+0x58 (4 bytes) and peer+0x5c
    (2 bytes), then returns the existing peer.
- Else if `param_1[3] + 0x538` (peer pool) is non-NULL:
  - Validates pool count vs limits (pool object at `pool->_size_blob + 0x10`).
  - Calls `FUN_ffffff80021b9f7a(pool, &LAB_ffffff80021c64e1, manager, addr, 0, 0xffffffff)`.
  - Calls `FUN_ffffff80021bf64a(addr, manager)` to construct a new peer
    (likely `IO80211InfraPeer::withAddressAndPeerManager`).
  - On success: `FUN_ffffff80021c4cf4(peer, manager+0x518)` inserts peer
    into the manager pool, then `FUN_ffffff80021d42a6(manager, count)` posts
    via command-gate.
- Returns `0x150e` on most error paths.

### IO80211PeerManager::addPeerOperation body (FUN_ffffff80021d7ba0)

Thin command-gate wrapper:
- Reads `param_1[3] + 0xbf8` (the gate / scheduler context).
- If null, lazily initializes via `FUN_ffffff80021d73e0`.
- Calls `FUN_ffffff80021ce526(manager, 0x4966506565724164)` ("IfPeerAd" -
  cookie identifying the operation).
- Tail-calls `FUN_ffffff8000b00f40(gate, cookie, 1)` — a non-returning
  command-gate runAction-style dispatch.

Implication: `addPeerOperation()` runs `addPeer` (or its packaged subroutine)
on the InfraInterface command-gate. Ordering question: callers must be on or
off the gate consistently; needs caller-side audit before any local invocation.

### IO80211InfraInterface::setLinkState dispatch (FUN_ffffff80022df28c)

Vtable thunk: `(*(*param_1 + 0xe70))()`. Real body is at
FUN_ffffff80022df29e with signature
`(long *this, int state, undefined8 a3, char a4, uint a5, ulong a6)`.

Key behaviors of the body:
- Precondition check: `*(int *)(this[0x24] + 0x58) == 1` — gates entire
  function on an internal state.
- For `state == 1` (down): calls `FUN_ffffff80021cc798(peer_manager, 0)` then
  `FUN_ffffff80021d8338(peer_manager, 0)`.
- For `state == 2` (up): runs full link-up sequence:
  - Updates peer subsystem, calls `*(*peer + 0x980)(peer, 3, ...)` to set
    peer state to 3 (associated/run).
  - Sets `this[0x25] + 0xf15 = a6`, `this[0x25] + 0x6 = 2` (active state).
  - Calls `FUN_ffffff8002a383ae(this, 0x100003)` — almost certainly an event
    post (state-change notification).
  - Calls `FUN_ffffff80022df728(this)` and
    `FUN_ffffff8002a37bda(this, *(this[0x25]+0x20), *(this[0x25]+0x20), 0, 0)`.
  - Halts on Ghidra "Bad instruction" before completing — body partially
    readable; remainder requires raw disasm.

### Open Gates Before Peer/Link-State Patch

- Caller-side audit: which BCM driver path invokes `addPeerOperation()` vs
  `addPeer` directly, and on which thread / gate context.
- Identity of `withAddressAndPeerManager` constructor (FUN_ffffff80021bf64a)
  — verify it constructs `IO80211InfraPeer` (not base or TimeSync flavor).
- Precondition for `setLinkState(state=2)`: `this[0x24] + 0x58 == 1` — find
  who writes that field and when. Likely InfraInterface initialization or
  associate path.
- `setCurrentApAddress` body and ordering vs `setLinkState`.

Until all four are decompile-confirmed, the peer/link-state patch remains at
status CORRELATED. CR-176+ is the candidate id once confirmed.

## A-CR175-INFRAINTERFACE-TAHOE-API-HEADER-ALIGNMENT

status: CONFIRMED_DEVIATION

symptom layer: structural reference alignment of `IO80211InfraInterface`
public surface for the active runtime blocker layer. CR-175 supersedes
CR-174 by additively naming Tahoe-exported non-virtual helpers in
`IO80211InfraInterface.h` so that future caller wiring can link against
the documented BootKC symbols without per-caller extern shims.

### Confirmed Exported Symbols (BootKC, IO80211Family, 2026-04-28)

`ffffff80022e1148  IO80211InfraInterface::getInfraPeer()`

`ffffff80022e5ef8  IO80211InfraInterface::getCurrentApAddress()`

`ffffff80022e6f9c  IO80211InfraInterface::handleKeyDone(bool, bool)`

`ffffff80022e116e  IO80211InfraInterface::bssidChange(void*, unsigned long)`

These four symbols are direct-call exports (not vtable thunks). Adding
non-virtual header declarations does not shift the vtable layout and does
not introduce any new undefined symbols in the kext until a caller actually
references them. The build remains bit-identical to CR-174.

### Local Fix (REFERENCE_ALIGNMENT_FIX)

`include/Airport/IO80211InfraInterface.h`

- Add four non-virtual method declarations under `#if __IO80211_TARGET >=
  __MAC_26_0`:
  - `IO80211Peer *getInfraPeer(void);`
  - `ether_addr *getCurrentApAddress(void);`
  - `void handleKeyDone(bool, bool);`
  - `void bssidChange(void *, unsigned long);`
- Anchor each to its BootKC address in a comment block.
- No changes to vtable virtual list, no caller wiring.

### Binary Invariance

- kext sha256 pre  = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- kext sha256 post = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- UUID pre/post    = `BA3D771F-F079-33FF-94E5-C792E66237D8`
- regdiag sha256   = `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`

CR-175 is therefore a purely additive structural rename over CR-174.

### Non-claims (CR-175)

- Does not synthesize `AppleBCMWLANPCIeSkywalkPacket`.
- Does not write `packet+0x78`.
- Does not change packet allocation, queueing, input, or output behavior.
- Does not force EAPOL TX, key install, RSN, DHCP, link, or data success.
- Does not add retry, replay, delay, poll loop, packet synthesis, or
  deauth masking.
- Does not call `getInfraPeer`, `getCurrentApAddress`, `handleKeyDone`, or
  `bssidChange` from any local code path.

## A-CR176-PEERMANAGER-PUBLIC-API-HEADER-ALIGNMENT

status: CONFIRMED_DEVIATION

symptom layer: structural reference alignment of `IO80211PeerManager`
public surface. CR-176 supersedes CR-175 by adding a new local header that
declares the public direct-call peer-management surface exported by
IO80211Family. Until CR-176 the local include directory had no canonical
home for the IO80211PeerManager API at all.

### Confirmed Exported Symbols (BootKC, IO80211Family, 2026-04-28)

`ffffff80021d3f58  IO80211PeerManager::addPeer(unsigned char *)`

`ffffff80021d7ba0  IO80211PeerManager::addPeerOperation()`

`ffffff80021d4452  IO80211PeerManager::removePeer(IO80211Peer *)`

`ffffff80021d4806  IO80211PeerManager::removePeer(unsigned char *)`

`ffffff80021d7c7e  IO80211PeerManager::removePeerOperation()`

`ffffff80021df2fe  IO80211PeerManager::getPeerList()`

`ffffff80021d298e  IO80211PeerManager::getPeerStats(apple80211_peer_stats *)`

These seven symbols are the direct-call (non-vtable) public surface that any
future caller-wiring CR will need to invoke. Adding a local header that
declares them lets the kext use the documented C++ method-call syntax instead
of per-callsite mangled `extern` shims.

### Local Fix (REFERENCE_ALIGNMENT_FIX)

`include/Airport/IO80211PeerManager.h` (new file)

- Declares `class IO80211PeerManager` as an opaque non-data class with no
  vtable and no inheritance; the local kext only ever holds
  `IO80211PeerManager *` returned by IO80211Family and never allocates or
  takes `sizeof` of the class.
- Declares the seven non-virtual member functions listed above with
  signatures matching the BootKC mangled names.
- Forward-declares `class IO80211Peer` and
  `struct apple80211_peer_stats`.
- Anchors each declaration to its BootKC address in the header preamble.

### Binary Invariance

- kext sha256 pre  = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- kext sha256 post = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- UUID pre/post    = `BA3D771F-F079-33FF-94E5-C792E66237D8`
- regdiag sha256   = `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`

CR-176 is a purely additive header file with no callers, so the kext is
bit-identical to CR-175.

### Non-claims (CR-176)

- Does not synthesize `AppleBCMWLANPCIeSkywalkPacket`.
- Does not write `packet+0x78`.
- Does not change packet allocation, queueing, input, or output behavior.
- Does not force EAPOL TX, key install, RSN, DHCP, link, or data success.
- Does not add retry, replay, delay, poll loop, packet synthesis, or
  deauth masking.
- Does not call `addPeer`, `addPeerOperation`, `removePeer`,
  `removePeerOperation`, `getPeerList`, or `getPeerStats` from any local
  code path.
- Does not declare `IO80211PeerManager` with any base class or data layout;
  the declaration is intentionally opaque.

## A-CR177-PEER-PUBLIC-API-HEADER-ALIGNMENT

status: CONFIRMED_DEVIATION

symptom layer: structural reference alignment of `IO80211Peer` public
surface. CR-177 supersedes CR-176 by adding a new local header that
declares the public direct-call peer surface exported by IO80211Family.
Until CR-177 the local include directory had no canonical home for the
`IO80211Peer` API; only forward declarations existed.

### Confirmed Exported Symbols (BootKC, IO80211Family, 2026-04-28)

`ffffff80021bf64a  IO80211Peer::withAddressAndManager(unsigned char const *, IO80211PeerManager *)`

`ffffff80021bf6c0  IO80211Peer::init()`

`ffffff80021bff7a  IO80211Peer::getMacAddress()`

`ffffff80021c5df4  IO80211Peer::setMacAddress(ether_addr *)`

`ffffff80021c3558  IO80211Peer::getManager()`

`ffffff80021c60dc  IO80211Peer::getGeneration()`

These six symbols are the minimum public peer surface that any future
caller-wiring CR will need: a factory (`withAddressAndManager`), a base
init, MAC accessors, a back-pointer to the owning peer manager, and the
generation tag used by reference-counting paths.

The full IO80211Peer symbol table contains 230 entries; CR-177 deliberately
restricts scope to symbols whose C++ signatures contain no kernel-internal
enum or class types (e.g. `peerState`, `peerMonitoringCtx`,
`peerDataLogRequest`) so that the mangled names resolve unambiguously.
Additional methods can be added by future REFERENCE_ALIGNMENT_FIX CRs as
needed.

### Local Fix (REFERENCE_ALIGNMENT_FIX)

`include/Airport/IO80211Peer.h` (new file)

- Declares `class IO80211Peer` as opaque (no base class, no data, no vtable).
- Forward-declares `class IO80211PeerManager` and `struct ether_addr`.
- Declares the six member functions listed above with signatures matching
  the BootKC mangled names.
- `withAddressAndManager` is declared `static` (it is the OSObject-style
  factory that returns a freshly constructed peer).
- Anchors each declaration to its BootKC address in the header preamble.

### Binary Invariance

- kext sha256 pre  = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- kext sha256 post = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- UUID pre/post    = `BA3D771F-F079-33FF-94E5-C792E66237D8`
- regdiag sha256   = `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`

CR-177 is a purely additive header file with no callers; the kext is
bit-identical to CR-176.

### Non-claims (CR-177)

- Does not synthesize `AppleBCMWLANPCIeSkywalkPacket`.
- Does not write `packet+0x78`.
- Does not change packet allocation, queueing, input, or output behavior.
- Does not force EAPOL TX, key install, RSN, DHCP, link, or data success.
- Does not add retry, replay, delay, poll loop, packet synthesis, or
  deauth masking.
- Does not call any of the six peer methods from any local code path.
- Does not declare `IO80211Peer` with any base class or data layout.
- Does not include `peerState`, `peerMonitoringCtx`, or other
  kernel-internal types whose definitions are not yet recovered.

## A-CR178-PEERMANAGER-DATAPATH-LOOKUP-HEADER-ALIGNMENT

status: CONFIRMED_DEVIATION

symptom layer: structural reference alignment of `IO80211PeerManager`
data-path control + peer-lookup helper surface. CR-178 supersedes
CR-177 by extending the IO80211PeerManager header introduced in CR-176
with eleven additional non-virtual exported helpers needed by any
future caller-wiring CR that operates on the EAPOL TX / key install /
RSN_DONE blocker layer.

### Confirmed Exported Symbols (BootKC, IO80211Family, 2026-04-28)

`ffffff80021d1388  IO80211PeerManager::findPeer(unsigned char *)`

`ffffff80021d3f0c  IO80211PeerManager::findCachedPeer(unsigned char *)`

`ffffff80021df2a8  IO80211PeerManager::getUnicastPeer()`

`ffffff80021df296  IO80211PeerManager::getMulticastPeer()`

`ffffff80021df672  IO80211PeerManager::getEnabled()`

`ffffff80021cc798  IO80211PeerManager::setEnableState(bool)`

`ffffff80021df4f8  IO80211PeerManager::getDataPathOpen()`

`ffffff80021df50a  IO80211PeerManager::setDataPathOpen(bool)`

`ffffff80021cde60  IO80211PeerManager::setDataPathState(bool)`

`ffffff80021cded6  IO80211PeerManager::lockDataPath()`

`ffffff80021cdfca  IO80211PeerManager::unlockDataPath()`

These eleven symbols cover the lookup + data-path-control cluster of
the peer-manager surface. The full IO80211PeerManager export set is
220+ entries; CR-178 restricts scope to signatures that contain no
kernel-internal enum or class types (`scanSource`, `joinStatus`,
`ApplePeerManagerEvents`, `StateChangedSource`, `ccodeState`) so the
mangled names resolve unambiguously.

### Local Fix (REFERENCE_ALIGNMENT_FIX)

`include/Airport/IO80211PeerManager.h` (extended)

- Declares the four lookup helpers (`findPeer`, `findCachedPeer`,
  `getUnicastPeer`, `getMulticastPeer`).
- Declares the two enable-state accessors (`getEnabled`,
  `setEnableState`).
- Declares the data-path control quartet (`getDataPathOpen`,
  `setDataPathOpen`, `setDataPathState`, `lockDataPath`,
  `unlockDataPath`).
- Anchors each new declaration to its BootKC address in the header
  preamble alongside the CR-176 entries.

### Binary Invariance

- kext sha256 pre  = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- kext sha256 post = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- UUID pre/post    = `BA3D771F-F079-33FF-94E5-C792E66237D8`
- regdiag sha256   = `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`

CR-178 is a purely additive header extension with no callers; the kext
is bit-identical to CR-177.

### Non-claims (CR-178)

- Does not synthesize `AppleBCMWLANPCIeSkywalkPacket`.
- Does not write `packet+0x78`.
- Does not change packet allocation, queueing, input, or output behavior.
- Does not force EAPOL TX, key install, RSN, DHCP, link, or data success.
- Does not add retry, replay, delay, poll loop, packet synthesis, or
  deauth masking.
- Does not call any of the eleven peer-manager methods from any local
  code path.
- Does not declare `IO80211PeerManager` with any base class or data
  layout; the declaration is intentionally opaque.
- Does not include kernel-internal types whose definitions are not yet
  recovered.

## A-CR179-PEERMANAGER-INFRA-CONFIG-HEADER-ALIGNMENT

status: CONFIRMED_DEVIATION

symptom layer: structural reference alignment of `IO80211PeerManager`
infra BSSID/SSID/channel/RSSI surface. CR-179 supersedes CR-178 by
extending the IO80211PeerManager header (CR-176 / CR-178) with thirteen
additional non-virtual exported helpers covering the cached infra
association state.

### Confirmed Exported Symbols (BootKC, IO80211Family, 2026-04-28)

`ffffff80021df07a  IO80211PeerManager::getInfraBssid()`

`ffffff80021df2de  IO80211PeerManager::getInfraSsidLen()`

`ffffff80021df2ee  IO80211PeerManager::setInfraSsidLen(unsigned int)`

`ffffff80021df08a  IO80211PeerManager::getInfraSsidBytes()`

`ffffff80021df09a  IO80211PeerManager::setInfraSsidBytes(unsigned char *, unsigned int)`

`ffffff80021d4e36  IO80211PeerManager::setInfraTxState(bool)`

`ffffff80021d4eb0  IO80211PeerManager::setInfraChannel(apple80211_channel *)`

`ffffff80021d4e72  IO80211PeerManager::copyInfraChannel(apple80211_channel *)`

`ffffff80021d4e90  IO80211PeerManager::resetInfraChannel()`

`ffffff80021df04c  IO80211PeerManager::setInfraChannelInfo(apple80211_channel *)`

`ffffff80021df06a  IO80211PeerManager::setInfraChannelFlags(unsigned int)`

`ffffff80021df994  IO80211PeerManager::getInfraRSSI()`

`ffffff80021df984  IO80211PeerManager::setInfraRSSI(int)`

### Local Fix (REFERENCE_ALIGNMENT_FIX)

`include/Airport/IO80211PeerManager.h` (extended)

- Adds the BSSID accessor `getInfraBssid`.
- Adds the SSID byte/length accessor and mutator pair.
- Adds the TX-state mutator `setInfraTxState`.
- Adds the channel quartet `setInfraChannel`, `copyInfraChannel`,
  `resetInfraChannel`, `setInfraChannelInfo`, `setInfraChannelFlags`.
- Adds the RSSI accessor and mutator `getInfraRSSI` / `setInfraRSSI`.
- Forward-declares `struct apple80211_channel` and `struct ether_addr`.
- Anchors each new declaration to its BootKC address in the header
  preamble.

### Binary Invariance

- kext sha256 pre  = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- kext sha256 post = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- UUID pre/post    = `BA3D771F-F079-33FF-94E5-C792E66237D8`
- regdiag sha256   = `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`

CR-179 is a purely additive header extension; the kext is bit-identical
to CR-178.

### Non-claims (CR-179)

- Does not synthesize `AppleBCMWLANPCIeSkywalkPacket`.
- Does not write `packet+0x78`.
- Does not change packet allocation, queueing, input, or output behavior.
- Does not force EAPOL TX, key install, RSN, DHCP, link, or data success.
- Does not add retry, replay, delay, poll loop, packet synthesis, or
  deauth masking.
- Does not call any of the thirteen peer-manager methods from any local
  code path.
- Does not declare `IO80211PeerManager` with any base class or data
  layout; the declaration is intentionally opaque.
- Does not include kernel-internal types whose definitions are not yet
  recovered.

## A-CR180-INTERFACE-MONITOR-PUBLIC-API-HEADER-ALIGNMENT

status: CONFIRMED_DEVIATION

symptom layer: structural reference alignment of `IO80211InterfaceMonitor`
public direct-call surface. CR-180 supersedes CR-179 by adding a new
local header that declares the public direct-call interface-monitor
surface exported by IO80211Family. Until CR-180 the local include
directory had no canonical home for the `IO80211InterfaceMonitor` API;
only a forward declaration in IO80211Interface.h existed.

### Confirmed Exported Symbols (BootKC, IO80211Family, 2026-04-28)

`ffffff80022f7938  IO80211InterfaceMonitor::getController()`

`ffffff80022f7568  IO80211InterfaceMonitor::getInputBytes()`

`ffffff80022f7536  IO80211InterfaceMonitor::getInputPackets()`

`ffffff80022f7694  IO80211InterfaceMonitor::getOutputBEBytes()`

`ffffff80022f76c6  IO80211InterfaceMonitor::getOutputBKBytes()`

`ffffff80022f772a  IO80211InterfaceMonitor::getOutputVIBytes()`

`ffffff80022f76f8  IO80211InterfaceMonitor::getOutputVOBytes()`

`ffffff80022f759a  IO80211InterfaceMonitor::getOutputBEPackets()`

`ffffff80022f75cc  IO80211InterfaceMonitor::getOutputBKPackets()`

`ffffff80022f7662  IO80211InterfaceMonitor::getOutputVIPackets()`

`ffffff80022f75fe  IO80211InterfaceMonitor::getOutputVOPackets()`

`ffffff80022f54f4  IO80211InterfaceMonitor::getInterfaceRSSI()`

`ffffff80022f521e  IO80211InterfaceMonitor::setInterfaceRSSI(long long)`

`ffffff80022f54c2  IO80211InterfaceMonitor::hasInterfaceRSSI()`

`ffffff80022f5530  IO80211InterfaceMonitor::setInterfaceSNR(long long)`

`ffffff80022f57f2  IO80211InterfaceMonitor::setInterfaceNF(long long)`

`ffffff80022ef27c  IO80211InterfaceMonitor::getLinkRate()`

`ffffff80022ef20c  IO80211InterfaceMonitor::setLinkRate(unsigned long long)`

`ffffff80022ef182  IO80211InterfaceMonitor::modifyChID(unsigned long long)`

### Local Fix (REFERENCE_ALIGNMENT_FIX)

`include/Airport/IO80211InterfaceMonitor.h` (new file)

- Declares `class IO80211InterfaceMonitor` as opaque (no base class,
  no data, no vtable).
- Forward-declares `class IO80211Controller`.
- Declares the controller back-pointer accessor.
- Declares ten counter accessors (input bytes/packets and per-AC
  output bytes/packets).
- Declares RSSI/SNR/NF accessors and mutators.
- Declares link-rate accessor/mutator and `modifyChID` channel
  modifier.
- Anchors each declaration to its BootKC address in the header
  preamble.

### Binary Invariance

- kext sha256 pre  = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- kext sha256 post = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- UUID pre/post    = `BA3D771F-F079-33FF-94E5-C792E66237D8`
- regdiag sha256   = `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`

CR-180 is a purely additive header file with no callers; the kext is
bit-identical to CR-179.

### Non-claims (CR-180)

- Does not synthesize `AppleBCMWLANPCIeSkywalkPacket`.
- Does not write `packet+0x78`.
- Does not change packet allocation, queueing, input, or output behavior.
- Does not force EAPOL TX, key install, RSN, DHCP, link, or data success.
- Does not add retry, replay, delay, poll loop, packet synthesis, or
  deauth masking.
- Does not call any of the nineteen interface-monitor methods from any
  local code path.
- Does not declare `IO80211InterfaceMonitor` with any base class or
  data layout; the declaration is intentionally opaque.
- Does not include kernel-internal types whose definitions are not yet
  recovered.

## A-CR181-INFRAINTERFACE-PROPERTY-UPDATERS-HEADER-ALIGNMENT

status: CONFIRMED_DEVIATION

symptom layer: structural reference alignment of `IO80211InfraInterface`
IORegistry property updater + runtime helper surface. CR-181 supersedes
CR-180 by extending the IO80211InfraInterface header (CR-175) with
eleven additional non-virtual exported helpers covering IORegistry
property updates and dispatch-queue runtime checks.

### Confirmed Exported Symbols (BootKC, IO80211Family, 2026-04-28)

`ffffff80022e1a56  IO80211InfraInterface::updateSSIDProperty()`

`ffffff80022e2504  IO80211InfraInterface::updateLocaleProperty()`

`ffffff80022e1f98  IO80211InfraInterface::updateBSSIDProperty(ether_addr &, apple80211_channel &, bool, bool)`

`ffffff80022e2156  IO80211InfraInterface::updateChannelProperty(apple80211_channel &)`

`ffffff80022e1b90  IO80211InfraInterface::updateCountryCodeProperty(bool)`

`ffffff80022de8b2  IO80211InfraInterface::updateStaticProperties()`

`ffffff80022df728  IO80211InfraInterface::updateLinkSpeed()`

`ffffff80022e1782  IO80211InfraInterface::loadHwChannels()`

`ffffff80022e1848  IO80211InfraInterface::loadChannelInfo()`

`ffffff80022e61ea  IO80211InfraInterface::onDispatchQueue()`

`ffffff80022dfb9c  IO80211InfraInterface::cancelDebounceTimer()`

### Local Fix (REFERENCE_ALIGNMENT_FIX)

`include/Airport/IO80211InfraInterface.h` (extended)

- Adds the seven IORegistry property updaters (`updateSSIDProperty`,
  `updateLocaleProperty`, `updateBSSIDProperty`,
  `updateChannelProperty`, `updateCountryCodeProperty`,
  `updateStaticProperties`, `updateLinkSpeed`).
- Adds the two channel-table loaders (`loadHwChannels`,
  `loadChannelInfo`).
- Adds the runtime helpers `onDispatchQueue` and `cancelDebounceTimer`.
- Anchors each new declaration to its BootKC address in the header
  preamble next to the CR-175 entries.

### Binary Invariance

- kext sha256 pre  = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- kext sha256 post = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- UUID pre/post    = `BA3D771F-F079-33FF-94E5-C792E66237D8`
- regdiag sha256   = `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`

CR-181 is a purely additive header extension; the kext is bit-identical
to CR-180.

### Non-claims (CR-181)

- Does not synthesize `AppleBCMWLANPCIeSkywalkPacket`.
- Does not write `packet+0x78`.
- Does not change packet allocation, queueing, input, or output behavior.
- Does not force EAPOL TX, key install, RSN, DHCP, link, or data success.
- Does not add retry, replay, delay, poll loop, packet synthesis, or
  deauth masking.
- Does not call any of the eleven InfraInterface methods from any local
  code path.
- Does not declare `IO80211InfraInterface` with any base class or data
  layout beyond what was already present.
- Does not include kernel-internal types whose definitions are not yet
  recovered.

## A-CR182 — IO80211InterfaceMonitor leaky-AP/reporter/packet-record header alignment

CR-182 extends the `IO80211InterfaceMonitor` header (CR-180) with ten
additional public direct-call exports recovered from the IO80211Family
BootKC symbol table on 2026-04-28: three leaky-AP helpers
(`getLeakyApSsid`, `getLeakyApBssid`, `resetLeakyApStats`), four
per-packet record helpers (`setInputPacketRSSI`, `recordInputPacket`,
`recordOutputPacket`, `initFrameStats`), and three reporter-lifecycle
helpers (`initHeFrameStats`, `destroyReporters`, `updateAllReports`).

The exact symbols, addresses, and signatures are catalogued in
`docs/reference/AppleBCMWLAN_interface_monitor_leaky_ap_reporters_2026_04_28.md`
and YAML 122. No caller wiring or runtime behavior change is included;
the kext is bit-identical to CR-181.

### What CR-182 adds

- Adds three leaky-AP helpers (`getLeakyApSsid`, `getLeakyApBssid`,
  `resetLeakyApStats`).
- Adds four per-packet record helpers (`setInputPacketRSSI`,
  `recordInputPacket`, `recordOutputPacket`, `initFrameStats`).
- Adds three reporter-lifecycle helpers (`initHeFrameStats`,
  `destroyReporters`, `updateAllReports`).
- Adds forward declarations for `apple80211_ssid`, `ether_addr`, and
  `enum apple80211_wme_ac : unsigned int`.
- Anchors each new declaration to its BootKC address in the header
  preamble next to the CR-180 entries.

### Binary Invariance

- kext sha256 pre  = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- kext sha256 post = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- UUID pre/post    = `BA3D771F-F079-33FF-94E5-C792E66237D8`
- regdiag sha256   = `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`

CR-182 is a purely additive header extension; the kext is bit-identical
to CR-181.

### Non-claims (CR-182)

- Does not synthesize `AppleBCMWLANPCIeSkywalkPacket`.
- Does not write `packet+0x78`.
- Does not change packet allocation, queueing, input, or output behavior.
- Does not force EAPOL TX, key install, RSN, DHCP, link, or data success.
- Does not add retry, replay, delay, poll loop, packet synthesis, or
  deauth masking.
- Does not call any of the ten InterfaceMonitor methods from any local
  code path.
- Does not declare `IO80211InterfaceMonitor` with any base class or data
  layout beyond what was already present.
- Does not include kernel-internal types whose definitions are not yet
  recovered.

## A-CR183 — IO80211Peer capability/credit/counter accessor header alignment

CR-183 extends the `IO80211Peer` header (CR-177) with twenty-five
additional public direct-call exports recovered from the IO80211Family
BootKC symbol table on 2026-04-28: nine capability flag accessors
(HT/VHT/HE/6E getters/setters plus `hasHTorVHTCaps`), two transmit
predicates (`canTransmit`, `canTransmitReason`), four credit/counter
accessors (`getOpenCredits`, `getCloseCredits`, `getNumTxPacket`,
`getOutputSuccess`), four TX-quantum/sequence helpers (`getTxQuantum`,
`setTxQuantum`, `getNextTxSeq`, `setTransmitOk`), two RX-sequence
helpers (`getRxSequence`, `getRxSequenceMulticast`), and four
cache/SoftAP-peer flag accessors (`isCachedInFw`, `setCachedInFw`,
`isSoftAPPeer`, `setSoftAPPeer`).

The exact symbols, addresses, and signatures are catalogued in
`docs/reference/AppleBCMWLAN_peer_cap_credit_counter_2026_04_28.md`
and YAML 123. No caller wiring or runtime behavior change is
included; the kext is bit-identical to CR-182.

### What CR-183 adds

- Adds nine capability getters/setters (HT, VHT, HE, 6E,
  hasHTorVHTCaps).
- Adds two transmit predicates (`canTransmit`, `canTransmitReason`).
- Adds four credit/counter accessors (`getOpenCredits`,
  `getCloseCredits`, `getNumTxPacket`, `getOutputSuccess`).
- Adds four TX-quantum/sequence helpers (`getTxQuantum`,
  `setTxQuantum`, `getNextTxSeq`, `setTransmitOk`).
- Adds two RX-sequence helpers (`getRxSequence`,
  `getRxSequenceMulticast`).
- Adds four cache/SoftAP-peer flag accessors (`isCachedInFw`,
  `setCachedInFw`, `isSoftAPPeer`, `setSoftAPPeer`).
- Anchors each new declaration to its BootKC address in the header
  preamble next to the CR-177 entries.

### Binary Invariance

- kext sha256 pre  = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- kext sha256 post = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- UUID pre/post    = `BA3D771F-F079-33FF-94E5-C792E66237D8`
- regdiag sha256   = `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`

CR-183 is a purely additive header extension; the kext is bit-identical
to CR-182.

### Non-claims (CR-183)

- Does not synthesize `AppleBCMWLANPCIeSkywalkPacket`.
- Does not write `packet+0x78`.
- Does not change packet allocation, queueing, input, or output behavior.
- Does not force EAPOL TX, key install, RSN, DHCP, link, or data success.
- Does not add retry, replay, delay, poll loop, packet synthesis, or
  deauth masking.
- Does not call any of the twenty-five Peer methods from any local
  code path.
- Does not declare `IO80211Peer` with any base class or data layout
  beyond what was already present.
- Does not include kernel-internal types whose definitions are not yet
  recovered.

## A-CR184 — IO80211Peer RSSI/packet-stats/cache/queue header alignment

CR-184 extends the `IO80211Peer` header (CR-177 + CR-183) with
thirty-three additional public direct-call exports recovered from the
IO80211Family BootKC symbol table on 2026-04-28: two stats-id helpers,
two RSSI reporters, six per-band RSSI getters/setters, five
resource/queue helpers, three RX bit-field/count helpers, four
packet-stats accessors, three predicate helpers, four queue-state /
lifetime helpers, two cache-timestamp helpers, and two BSS-steering
predicates.

The exact symbols, addresses, and signatures are catalogued in
`docs/reference/AppleBCMWLAN_peer_rssi_stats_cache_2026_04_28.md` and
YAML 124. No caller wiring or runtime behavior change is included;
the kext is bit-identical to CR-183.

### What CR-184 adds

- Adds two stats-id helpers (`getStatsID`, `getStatsIDValid`).
- Adds two RSSI reporters (`reportRssi`, `reportChainRssi`).
- Adds six per-band RSSI getters/setters (24G, 5G, AcrossBands,
  ChainRssi5G, plus two setters).
- Adds five resource/queue helpers (`simulateDPS`, `freeResources`,
  `unpauseQueues`, `reclaimPackets`, `clearCacheState`).
- Adds three RX bit-field/count helpers (`getRxBitField`,
  `getRxBitFieldMulticast`, `incrementRxCount`).
- Adds four packet-stats accessors (`getPacketStats`,
  `getPacketStatsRealTimeRx`, `getPacketStatsRealTimeTx`,
  `getCumDataStats`).
- Adds three predicates (`hasRealTimeData`, `hasLowLatencyData`,
  `hasQueuedPackets`).
- Adds four queue-state/lifetime helpers (`getDataLinkCount`,
  `logPeerTxLatency`, `updateQueueState`, `setPacketLifetime`).
- Adds two cache-timestamp helpers (`getCacheTimeStamp`,
  `setCacheTimeStamp`).
- Adds two BSS-steering predicates (`isBssSteeringPeer`,
  `isBssSteeringPeerSyncState`).
- Adds forward declaration for `apple80211_channel`.
- Anchors each new declaration to its BootKC address in the header
  preamble next to the CR-177/CR-183 entries.

### Binary Invariance

- kext sha256 pre  = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- kext sha256 post = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- UUID pre/post    = `BA3D771F-F079-33FF-94E5-C792E66237D8`
- regdiag sha256   = `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`

CR-184 is a purely additive header extension; the kext is bit-identical
to CR-183.

### Non-claims (CR-184)

- Does not synthesize `AppleBCMWLANPCIeSkywalkPacket`.
- Does not write `packet+0x78`.
- Does not change packet allocation, queueing, input, or output behavior.
- Does not force EAPOL TX, key install, RSN, DHCP, link, or data success.
- Does not add retry, replay, delay, poll loop, packet synthesis, or
  deauth masking.
- Does not call any of the thirty-three Peer methods from any local
  code path.
- Does not declare `IO80211Peer` with any base class or data layout
  beyond what was already present.
- Does not include kernel-internal types whose definitions are not yet
  recovered.

## A-CR185 — IO80211PeerManager parameterless accessor batch (33)

### Anomaly

`IO80211PeerManager` exports a large parameterless / single-primitive
accessor surface (BSDName, provider/controller/interfaceId/commandGate,
interface monitor pointer, country code, DTIM/beacon period, enabling
state, capability flags HE/VHT/HT/RSDB, dispatch-queue check, peer cache
state, hash table dump, free/awdl/mDNS/mbuf helpers, reporter
init/destroy/update, scanning state, per-AC output byte counters) that
the local header
`include/Airport/IO80211PeerManager.h` does not yet declare. Each export
is a non-virtual direct-call BootKC symbol; refusing to declare them
prevents future caller-wiring CRs from referencing the surface even when
the kext linker can already resolve the symbols.

### Confirmed exported symbols (BootKC, IO80211Family, 2026-04-28)

| Address              | Symbol                                                         |
|----------------------|----------------------------------------------------------------|
| `0xffffff80021c90aa` | `IO80211PeerManager::getBSDName()`                             |
| `0xffffff80021df576` | `IO80211PeerManager::GetProvider()`                            |
| `0xffffff80021c6bd2` | `IO80211PeerManager::getController()`                          |
| `0xffffff80021c8648` | `IO80211PeerManager::getInterfaceId()`                         |
| `0xffffff80021df390` | `IO80211PeerManager::getCommandGate()`                         |
| `0xffffff80021cea00` | `IO80211PeerManager::interfaceMonitor()`                       |
| `0xffffff80021df8bc` | `IO80211PeerManager::getCountryCode()`                         |
| `0xffffff80021df5ee` | `IO80211PeerManager::getDTIMPeriod()`                          |
| `0xffffff80021df5de` | `IO80211PeerManager::getBeaconPeriod()`                        |
| `0xffffff80021ccbba` | `IO80211PeerManager::getEnabling()`                            |
| `0xffffff80021c9d2c` | `IO80211PeerManager::failToEnable()`                           |
| `0xffffff80021df650` | `IO80211PeerManager::getHeCapable()`                           |
| `0xffffff80021df63e` | `IO80211PeerManager::getVhtCapable()`                          |
| `0xffffff80021df89c` | `IO80211PeerManager::getMyHeCap()`                             |
| `0xffffff80021df88c` | `IO80211PeerManager::getMyVhtCap()`                            |
| `0xffffff80021df8ac` | `IO80211PeerManager::getRsdbCap()`                             |
| `0xffffff80021df87c` | `IO80211PeerManager::getHtCapabilities()`                      |
| `0xffffff80021df0ee` | `IO80211PeerManager::isRsdbSupported()`                        |
| `0xffffff80021dfb38` | `IO80211PeerManager::onDispatchQueue()`                        |
| `0xffffff80021d4c0c` | `IO80211PeerManager::isPeerCacheFull()`                        |
| `0xffffff80021d4f0c` | `IO80211PeerManager::printHashTable()`                         |
| `0xffffff80021d46a0` | `IO80211PeerManager::removeAllPeers()`                         |
| `0xffffff80021c93a4` | `IO80211PeerManager::freeResources()`                          |
| `0xffffff80021ccf5c` | `IO80211PeerManager::awdlChipReset()`                          |
| `0xffffff80021cc734` | `IO80211PeerManager::flushFreeMbufs()`                         |
| `0xffffff80021dba82` | `IO80211PeerManager::enablemDNSTx()`                           |
| `0xffffff80021c9a56` | `IO80211PeerManager::destroyReporters()`                       |
| `0xffffff80021d9338` | `IO80211PeerManager::updateAllReports()`                       |
| `0xffffff80021df8cc` | `IO80211PeerManager::getScanningState()`                       |
| `0xffffff80021dfc24` | `IO80211PeerManager::getOutputBEBytes()`                       |
| `0xffffff80021dfc36` | `IO80211PeerManager::getOutputBKBytes()`                       |
| `0xffffff80021dfc48` | `IO80211PeerManager::getOutputVIBytes()`                       |
| `0xffffff80021dfc5a` | `IO80211PeerManager::getOutputVOBytes()`                       |

### Local change

- `include/Airport/IO80211PeerManager.h` — extended `class
  IO80211PeerManager` body with thirty-three new public method
  declarations and anchored each new declaration to its BootKC address
  in the header preamble next to the CR-179 entries.

### Build evidence

- Tahoe build: `commit-approval/build_evidence/CR-185-build-tahoe-peermanager-param-less-accessors-20260428.txt` — `** BUILD SUCCEEDED **`, BootKC undef-symbol verification PASS (884 symbols resolve).
- regdiag build: `commit-approval/build_evidence/CR-185-build-regdiag-peermanager-param-less-accessors-20260428.txt` — succeeded.

### Binary Invariance

- kext sha256 pre  = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- kext sha256 post = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- UUID pre/post    = `BA3D771F-F079-33FF-94E5-C792E66237D8`
- regdiag sha256   = `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`

CR-185 is a purely additive header extension; the kext is bit-identical
to CR-184.

### Non-claims (CR-185)

- Does not synthesize `AppleBCMWLANPCIeSkywalkPacket`.
- Does not write `packet+0x78`.
- Does not change packet allocation, queueing, input, or output behavior.
- Does not force EAPOL TX, key install, RSN, DHCP, link, or data success.
- Does not add retry, replay, delay, poll loop, packet synthesis, or
  deauth masking.
- Does not call any of the thirty-three PeerManager methods from any
  local code path.
- Does not declare `IO80211PeerManager` with any base class or data
  layout beyond what was already present.
- Does not include kernel-internal types whose definitions are not yet
  recovered.

## A-CR186 — IO80211InfraInterface LQM/WMM/AVC/BT-coex/SIB/ULLA/AWDL/BPF/leaky-AP/supplicant/P2P helper batch (21)

### Anomaly

Beyond the CR-181 IORegistry/property-update surface, `IO80211InfraInterface`
also exports a wide non-virtual helper surface for LQM (link-quality
monitor) data/gate/static, WMM bandwidth reset, AVC advisory pointer,
BT-coex state, traffic-monitor pointer, SIB coex/turn-on metrics,
CoP TX-RTS fail count, ULLA durations, AWDL bandwidth/state,
bpfTapInternal, leaky-AP stats mode, supplicant event handler, and
P2P route helper. The local
`include/Airport/IO80211InfraInterface.h` did not declare these
direct-call symbols.

### Confirmed exported symbols (BootKC, IO80211Family, 2026-04-28)

| Address              | Symbol                                                                              |
|----------------------|-------------------------------------------------------------------------------------|
| `0xffffff80022e4446` | `IO80211InfraInterface::getLQMData()`                                              |
| `0xffffff80022e451c` | `IO80211InfraInterface::setLQMGated(unsigned long long)`                           |
| `0xffffff80022e44c4` | `IO80211InfraInterface::setLQMStatic(void*, void*)`                                |
| `0xffffff80022e5d1e` | `IO80211InfraInterface::getMonitorMode()`                                          |
| `0xffffff80022e5ca2` | `IO80211InfraInterface::getWMMBWReset()`                                           |
| `0xffffff80022e5cb8` | `IO80211InfraInterface::setWMMBWReset(bool)`                                       |
| `0xffffff80022e14cc` | `IO80211InfraInterface::getAVCAdvisory()`                                          |
| `0xffffff80022e66e0` | `IO80211InfraInterface::getBtCoexState()`                                          |
| `0xffffff80022e190e` | `IO80211InfraInterface::resetInterface(void*, unsigned long)`                      |
| `0xffffff80022e5dce` | `IO80211InfraInterface::getTrafficMonitor()`                                       |
| `0xffffff80022e1386` | `IO80211InfraInterface::finishSIBCoexTimer()`                                      |
| `0xffffff80022e3aca` | `IO80211InfraInterface::resetSIBTurnOnMetrics()`                                   |
| `0xffffff80022e3a8a` | `IO80211InfraInterface::getCoPTxRTSFailCount()`                                    |
| `0xffffff80022e3a9e` | `IO80211InfraInterface::getULLALiteDuration()`                                     |
| `0xffffff80022e39e6` | `IO80211InfraInterface::getAwdlMaxBandWidth()`                                     |
| `0xffffff80022e57fe` | `IO80211InfraInterface::notifyAWDLStateChange(bool)`                               |
| `0xffffff80022e58d6` | `IO80211InfraInterface::bpfTapInternal(unsigned int, unsigned int)`                |
| `0xffffff80022e3784` | `IO80211InfraInterface::setLeakyAPStatsMode(unsigned int)`                         |
| `0xffffff80022e12f2` | `IO80211InfraInterface::UpdateULLADuration(unsigned long long *)`                  |
| `0xffffff80022e1e0c` | `IO80211InfraInterface::handleSupplicantEvent(void*, unsigned long)`               |
| `0xffffff80022e1e3a` | `IO80211InfraInterface::routeToP2PInterface(unsigned int, void*, unsigned long)`   |

### Local change

- `include/Airport/IO80211InfraInterface.h` — extended `class
  IO80211InfraInterface` body with twenty-one new direct-call method
  declarations and anchored each new declaration to its BootKC
  address in the header preamble next to the CR-181 entries.

### Build evidence

- Tahoe build: `commit-approval/build_evidence/CR-186-build-tahoe-infra-lqm-wmm-avc-batch-20260428.txt` — `** BUILD SUCCEEDED **`, BootKC undef-symbol verification PASS (884 symbols resolve).
- regdiag build: `commit-approval/build_evidence/CR-186-build-regdiag-infra-lqm-wmm-avc-batch-20260428.txt` — succeeded.

### Binary Invariance

- kext sha256 pre  = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- kext sha256 post = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- UUID pre/post    = `BA3D771F-F079-33FF-94E5-C792E66237D8`
- regdiag sha256   = `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`

CR-186 is a purely additive header extension; the kext is bit-identical
to CR-185.

### Non-claims (CR-186)

- Does not synthesize `AppleBCMWLANPCIeSkywalkPacket`.
- Does not write `packet+0x78`.
- Does not change packet allocation, queueing, input, or output behavior.
- Does not force EAPOL TX, key install, RSN, DHCP, link, or data success.
- Does not add retry, replay, delay, poll loop, packet synthesis, or
  deauth masking.
- Does not call any of the twenty-one InfraInterface methods from any
  local code path.
- Does not declare `IO80211InfraInterface` with any base class or data
  layout beyond what was already present.
- Does not include kernel-internal types whose definitions are not yet
  recovered.

## A-CR187 — IO80211SkywalkInterface non-virtual helper batch (20)

### Anomaly

`IO80211SkywalkInterface` exports a large non-virtual helper surface
(pid-lock, work-queue, peer-manager/monitor, init-MAC, MAC-agent,
parent-interface, interface-monitor, interface-role-str, low-latency
predicate, postMessage internal/sync, ioctl-route, device-type,
medium-type, power-state, property-table, command-allowed) that the
local `include/Airport/IO80211SkywalkInterface.h` did not declare.

Five additional exports were identified during analysis but excluded
from this batch because they would implicitly override a parent-class
virtual and introduce new vtable references (`getBSDName`,
`getHardwareAddress`, `setHardwareAddress`, `stringFromReturn`,
`errnoFromReturn`). They are deferred to a future CR that explicitly
approves vtable references.

### Confirmed exported symbols (BootKC, IO80211Family, 2026-04-28)

| Address              | Symbol                                                                                         |
|----------------------|------------------------------------------------------------------------------------------------|
| `0xffffff800227849c` | `IO80211SkywalkInterface::pidLockPid()`                                                       |
| `0xffffff80022772e6` | `IO80211SkywalkInterface::setPidLock(bool)`                                                   |
| `0xffffff8002276fcc` | `IO80211SkywalkInterface::getWorkQueue()`                                                     |
| `0xffffff8002274c8e` | `IO80211SkywalkInterface::getInterfaceId()`                                                   |
| `0xffffff8002274c7c` | `IO80211SkywalkInterface::getPeerManager()`                                                   |
| `0xffffff8002276fde` | `IO80211SkywalkInterface::getPeerMonitor(IO80211Peer*)`                                       |
| `0xffffff80022770fa` | `IO80211SkywalkInterface::setInitMacAddress(ether_addr&)`                                     |
| `0xffffff8002278916` | `IO80211SkywalkInterface::getMacAddressAgent()`                                               |
| `0xffffff8002278466` | `IO80211SkywalkInterface::getParentInterface()`                                               |
| `0xffffff8002277cb4` | `IO80211SkywalkInterface::getInterfaceMonitor()`                                              |
| `0xffffff8002274bbc` | `IO80211SkywalkInterface::getInterfaceRoleStr()`                                              |
| `0xffffff800227848a` | `IO80211SkywalkInterface::isLowLatencyEnabled()`                                              |
| `0xffffff80022772b2` | `IO80211SkywalkInterface::postMessageInternal(unsigned int, void*, unsigned long, bool)`      |
| `0xffffff800227776e` | `IO80211SkywalkInterface::postMessageSync(unsigned int, void*, unsigned long)`                |
| `0xffffff80022788a4` | `IO80211SkywalkInterface::routeIoctlToWcl(unsigned int, unsigned int, void*, unsigned long)`  |
| `0xffffff8002278414` | `IO80211SkywalkInterface::getDeviceType()`                                                    |
| `0xffffff8002278428` | `IO80211SkywalkInterface::setDeviceType(unsigned int)`                                        |
| `0xffffff80022771f0` | `IO80211SkywalkInterface::getMediumType()`                                                    |
| `0xffffff80022774f2` | `IO80211SkywalkInterface::getPowerState()`                                                    |
| `0xffffff80022783cc` | `IO80211SkywalkInterface::getPropertyTable()`                                                 |
| `0xffffff8002276868` | `IO80211SkywalkInterface::isCommandAllowed()`                                                 |

### Local change

- `include/Airport/IO80211SkywalkInterface.h` — extended `class
  IO80211SkywalkInterface` body with twenty new direct-call method
  declarations and anchored each new declaration to its BootKC
  address; added a NOTE block documenting the five deferred exports
  whose declarations would override a parent-class virtual.

### Build evidence

- Tahoe build: `commit-approval/build_evidence/CR-187-build-tahoe-skywalk-helpers-20260428.txt` — `** BUILD SUCCEEDED **`, BootKC undef-symbol verification PASS (884 symbols resolve).
- regdiag build: `commit-approval/build_evidence/CR-187-build-regdiag-skywalk-helpers-20260428.txt` — succeeded.

### Binary Invariance

- kext sha256 pre  = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- kext sha256 post = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- UUID pre/post    = `BA3D771F-F079-33FF-94E5-C792E66237D8`
- regdiag sha256   = `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`

CR-187 is a purely additive header extension; the kext is bit-identical
to CR-186.

### Non-claims (CR-187)

- Does not synthesize `AppleBCMWLANPCIeSkywalkPacket`.
- Does not write `packet+0x78`.
- Does not change packet allocation, queueing, input, or output behavior.
- Does not force EAPOL TX, key install, RSN, DHCP, link, or data success.
- Does not add retry, replay, delay, poll loop, packet synthesis, or
  deauth masking.
- Does not call any of the twenty SkywalkInterface methods from any
  local code path.
- Does not declare `IO80211SkywalkInterface` with any new base class or
  data layout beyond what was already present.
- Does not include kernel-internal types whose definitions are not yet
  recovered.

## A-CR188 — IO80211InterfaceMonitor extended primitive helpers

### Anomaly

CR-180 / CR-182 declared an initial slice of `IO80211InterfaceMonitor`
public direct-call methods (counters, leaky-AP basics, RSSI/SNR/NF,
link-rate and packet-record helpers). The BootKC IO80211Family
symbol table exports a much larger primitive-only surface — effective
link / data-transfer rates, expected-peak latency, average-CCA
helpers, DPS counters, leaky-AP SSID/BSSID metrics validators and
resetters, leaky-AP status/network updaters, previous-interface-activity
setter, LQM setter, low-RX-rate range, effective Rx/Tx BW since-last-read
queries, aggregated-peers TX latency, and bssid-metrics-from-ssid-metrics
loader — none of which were declared.

### Confirmed exports

Twenty-seven symbols recovered from
`IO80211Family.kc` (BootKC, 2026-04-28); see
`docs/reference/AppleBCMWLAN_monitor_extension_batch_2026_04_28.md`
and
`docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/128_monitor_extension_batch_2026_04_28.yaml`.

### Fix

Header-only: extend `class IO80211InterfaceMonitor` body with twenty-seven
new public method declarations (primitives only). Verified against the
kernel SDK that none of these names match a parent-class virtual
(`IOService`, `IOReportingProvider`, `IOReporter`), so no implicit
override is introduced.

### Binary Invariance

- kext sha256 pre  = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- kext sha256 post = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- UUID pre/post    = `BA3D771F-F079-33FF-94E5-C792E66237D8`
- regdiag sha256   = `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`

CR-188 is a purely additive header extension; the kext is bit-identical
to CR-187.

### Non-claims (CR-188)

- Does not synthesize `AppleBCMWLANPCIeSkywalkPacket`.
- Does not write `packet+0x78`.
- Does not change packet allocation, queueing, input, or output behavior.
- Does not force EAPOL TX, key install, RSN, DHCP, link, or data success.
- Does not add retry, replay, delay, poll loop, packet synthesis, or
  deauth masking.
- Does not call any of the twenty-seven InterfaceMonitor methods from
  any local code path.
- Does not declare `IO80211InterfaceMonitor` with any new base class
  or data layout beyond what was already present.
- Does not include kernel-internal types whose definitions are not yet
  recovered.

## A-CR189 — IO80211InfraInterface additional primitive helpers

### Anomaly

CR-175 / CR-181 / CR-186 declared the bulk of `IO80211InfraInterface`
non-virtual public direct-call helpers. The BootKC IO80211Family
symbol table still exports an additional eighteen primitive-only
direct-call symbols not yet declared: 5G low/high band switch
counters, CoP SIB-coex turn-on count and duration, ULLA classic
duration, CoP TX-RTS reset, tx-path health-check reset, infra-peers
logging toggle, report data-transfer-rate dispatchers (sync/static/
timer), link-status update timer, leaky-AP stats reset timer,
multicast-state restore timer, link parameter and link status static
trampolines, tx/rx latency updater, and offload capability publisher.

### Confirmed exports

Eighteen symbols recovered from `IO80211Family.kc` (BootKC,
2026-04-28); see
`docs/reference/AppleBCMWLAN_infra_additional_primitives_2026_04_28.md`
and
`docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/129_infra_additional_primitives_2026_04_28.yaml`.

### Fix

Header-only: extend `class IO80211InfraInterface` body inside the
`__IO80211_TARGET >= __MAC_26_0` block with eighteen new public
method declarations. Adds a forward declaration for
`IO80211TimerSource` in the header preamble.

### Binary Invariance

- kext sha256 pre  = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- kext sha256 post = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- UUID pre/post    = `BA3D771F-F079-33FF-94E5-C792E66237D8`
- regdiag sha256   = `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`

CR-189 is a purely additive header extension; the kext is bit-identical
to CR-188.

### Non-claims (CR-189)

- Does not synthesize `AppleBCMWLANPCIeSkywalkPacket`.
- Does not write `packet+0x78`.
- Does not change packet allocation, queueing, input, or output behavior.
- Does not force EAPOL TX, key install, RSN, DHCP, link, or data success.
- Does not add retry, replay, delay, poll loop, packet synthesis, or
  deauth masking.
- Does not call any of the eighteen InfraInterface methods from any
  local code path.
- Does not declare `IO80211InfraInterface` with any new base class or
  data layout beyond what was already present.
- Does not include kernel-internal types whose definitions are not yet
  recovered.

## A-CR190 — IO80211SkywalkInterface companion-id / pid-lock / dispatch helpers

### Anomaly

CR-187 declared a primitive-only batch of `IO80211SkywalkInterface`
direct-call helpers. The BootKC IO80211Family symbol table exports
eight additional non-virtual primitive-only direct-call symbols not
yet declared in the local header: companion interface-id getter and
setter, pid-lock query, low-latency-enabled setter, time-sync MAC
address updater, dispatch-queue validator, controller work-queue
accessor, and process-name / ioctl-information storer.

A larger set of BootKC `IO80211SkywalkInterface` non-virtual exports
recovered on 2026-04-28 (`attachPeer`, `detachPeer`, `cachePeer`,
`findPeer`, `getSelfMacAddr`, `getFeatureFlags`, `getDataQueueDepth`,
`handleChosenMedia`, `flushPacketQueues`, `isChipInterfaceReady`,
`isCommandProhibited`, `isInterfaceEnabled`, `setRunningState`,
`getSupportedMediaArray`, `setPromiscuousModeEnable`, `shouldLog`,
`getLastQueuePacketTime`, `getLastRxUnicastLinkActivityTime`,
`logTxLatency`, `logRxLatency`, `getLastTxTimeStamp`,
`getLastRxTimeStamp`, `setDebugTrafficReport`) are intentionally
excluded — they are already declared as virtuals earlier in the
same class body and resolve through the inherited vtable;
redeclaring them as non-virtual would conflict with their existing
virtual signatures and break the build.

### Confirmed exports

Eight symbols recovered from `IO80211Family.kc` (BootKC, 2026-04-28);
see
`docs/reference/AppleBCMWLAN_skywalk_companion_batch_2026_04_28.md`
and
`docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/130_skywalk_companion_batch_2026_04_28.yaml`.

### Fix

Header-only: extend `class IO80211SkywalkInterface` body with eight
new public method declarations (primitives plus an opaque
`ether_addr &`). Verified against the existing local header that
none of these names already appear as virtuals or non-virtuals in
the same class body, so no in-class redeclaration conflict and no
implicit vtable override is introduced.

### Binary Invariance

- kext sha256 pre  = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- kext sha256 post = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- UUID pre/post    = `BA3D771F-F079-33FF-94E5-C792E66237D8`
- regdiag sha256   = `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`

CR-190 is a purely additive header extension; the kext is bit-identical
to CR-189.

### Non-claims (CR-190)

- Does not synthesize `AppleBCMWLANPCIeSkywalkPacket`.
- Does not write `packet+0x78`.
- Does not change packet allocation, queueing, input, or output behavior.
- Does not force EAPOL TX, key install, RSN, DHCP, link, or data success.
- Does not add retry, replay, delay, poll loop, packet synthesis, or
  deauth masking.
- Does not call any of the eight SkywalkInterface methods from any
  local code path.
- Does not declare `IO80211SkywalkInterface` with any new base class
  or data layout beyond what was already present.
- Does not include kernel-internal types whose definitions are not yet
  recovered.

## A-CR191 — IO80211PeerManager primitive-only helper batch

### Anomaly

CR-176 / CR-178 / CR-179 / CR-185 declared the bulk of
`IO80211PeerManager` non-virtual public direct-call helpers (peer
management, infra-config, provider/controller/capability
accessors). The BootKC IO80211Family symbol table still exports
twenty additional primitive-only direct-call symbols not yet
declared: channel-id modifier, peer printer, mDNS / mDNS-tx block
toggles, P2P logging toggle, peers-count setter, beacon-period
and DTIM-period setters, display-state setter, scan-2G-only
setter and matching getter, tx-queue stamp getter and setter,
ctl-count and rx-packets updaters, mac-equality predicate,
country-code saver, and P2P CCA reporter.

### Confirmed exports

Twenty symbols recovered from `IO80211Family.kc` (BootKC,
2026-04-28); see
`docs/reference/AppleBCMWLAN_peer_manager_primitive_batch_2026_04_28.md`
and
`docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/131_peer_manager_primitive_batch_2026_04_28.yaml`.

### Fix

Header-only: extend `class IO80211PeerManager` body with twenty new
public method declarations. Verified that none of these names are
already declared in the same class body (CR-176 through CR-185
entries) and none match a parent-class virtual (IO80211PeerManager
has its own MetaClass; standard OSObject/IOService virtuals do not
include any of these names), so no implicit override or in-class
redeclaration conflict is introduced.

### Binary Invariance

- kext sha256 pre  = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- kext sha256 post = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- UUID pre/post    = `BA3D771F-F079-33FF-94E5-C792E66237D8`
- regdiag sha256   = `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`

CR-191 is a purely additive header extension; the kext is bit-identical
to CR-190.

### Non-claims (CR-191)

- Does not synthesize `AppleBCMWLANPCIeSkywalkPacket`.
- Does not write `packet+0x78`.
- Does not change packet allocation, queueing, input, or output behavior.
- Does not force EAPOL TX, key install, RSN, DHCP, link, or data success.
- Does not add retry, replay, delay, poll loop, packet synthesis, or
  deauth masking.
- Does not call any of the twenty PeerManager methods from any local
  code path.
- Does not declare `IO80211PeerManager` with any new base class or
  data layout beyond what was already present.
- Does not include kernel-internal types whose definitions are not yet
  recovered.

## A-CR192 — IO80211Peer state-flag and counter accessor batch

### Anomaly

CR-177 / CR-183 / CR-184 declared the bulk of `IO80211Peer`
non-virtual public direct-call helpers (capability flags, credits,
RSSI, packet-stats, queue, cache). The BootKC IO80211Family symbol
table still exports thirty additional primitive-only direct-call
symbols not yet declared: HT/VHT operation-IE presence flags, peer
add/delete requested/in-progress state, BSS steering sync state
setter, unicast/multicast Bonjour Rx counters, beacon received
counter, total/per-link data-link counts, real-time and low-latency
data-session counts.

### Confirmed exports

Thirty symbols recovered from `IO80211Family.kc` (BootKC,
2026-04-28); see
`docs/reference/AppleBCMWLAN_peer_state_flag_batch_2026_04_28.md`
and
`docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/132_peer_state_flag_batch_2026_04_28.yaml`.

### Fix

Header-only: extend `class IO80211Peer` body with thirty new public
method declarations. Verified that none of these names are already
declared in the same class body (CR-177 / CR-183 / CR-184 entries).
The local IO80211Peer class has no declared base class, so all
declarations are non-virtual members and no implicit override is
introduced.

### Binary Invariance

- kext sha256 pre  = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- kext sha256 post = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- UUID pre/post    = `BA3D771F-F079-33FF-94E5-C792E66237D8`
- regdiag sha256   = `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`

CR-192 is a purely additive header extension; the kext is bit-identical
to CR-191.

### Non-claims (CR-192)

- Does not synthesize `AppleBCMWLANPCIeSkywalkPacket`.
- Does not write `packet+0x78`.
- Does not change packet allocation, queueing, input, or output behavior.
- Does not force EAPOL TX, key install, RSN, DHCP, link, or data success.
- Does not add retry, replay, delay, poll loop, packet synthesis, or
  deauth masking.
- Does not call any of the thirty Peer methods from any local code path.
- Does not declare `IO80211Peer` with any new base class or data layout
  beyond what was already present.
- Does not include kernel-internal types whose definitions are not yet
  recovered.

## A-CR193 — IO80211Peer timestamp / link-activity / cache-time batch

### Anomaly

CR-192 closed the IO80211Peer state-flag and counter accessor surface
(IE-present flags, peer add/delete request state, Bonjour Rx, beacon
counter, data-link / session counts). The BootKC IO80211Family symbol
table still exports twenty-four additional primitive-only timestamp
and counter symbols not yet declared: Rx unicast / multicast link-
activity timestamps, peer last-data-activity time and inactivity-
exceeded flag, peer-presence posted timestamp, peer-discovered time,
caching-denied timestamp, last-cache-add attempt, last data-log
timestamp, last output-success timestamp, time-of-first chain-RSSI
sample, waiting-to-be-uncached timestamp, last-queue-packet
timestamp, transmit-status-log count, TX status-mismatch count.

### Confirmed exports

Twenty-four symbols recovered from `IO80211Family.kc` (BootKC,
2026-04-28); see
`docs/reference/AppleBCMWLAN_peer_timestamp_batch_2026_04_28.md`
and
`docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/133_peer_timestamp_batch_2026_04_28.yaml`.

### Fix

Header-only: extend `class IO80211Peer` body with twenty-four new
public method declarations adjacent to the CR-192 entries. None of
the names match a method already declared in the local class body,
and the local IO80211Peer has no declared base class, so all
declarations are non-virtual and no implicit override is introduced.

### Binary Invariance

- kext sha256 pre  = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- kext sha256 post = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- UUID pre/post    = `BA3D771F-F079-33FF-94E5-C792E66237D8`
- regdiag sha256   = `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`

CR-193 is a purely additive header extension; the kext is bit-identical
to CR-192.

### Non-claims (CR-193)

- Does not synthesize `AppleBCMWLANPCIeSkywalkPacket`.
- Does not write `packet+0x78`.
- Does not change packet allocation, queueing, input, or output behavior.
- Does not force EAPOL TX, key install, RSN, DHCP, link, or data success.
- Does not add retry, replay, delay, poll loop, packet synthesis, or
  deauth masking.
- Does not call any of the twenty-four Peer methods from any local
  code path.
- Does not declare `IO80211Peer` with any new base class or data
  layout beyond what was already present.
- Does not include kernel-internal types whose definitions are not yet
  recovered.

## A-CR194 — IO80211Peer caching-state and tx-counter batch

### Anomaly

CR-193 closed the IO80211Peer timestamp / link-activity / cache-time
surface. The BootKC IO80211Family symbol table still exports thirty-
three additional primitive-only direct-call symbols not yet declared:
peer caching-state controls (state-for-cached, pre-caching / pre-
uncaching, reservation enable, sidecar request, low-latency link
idle, waiting-to-be-cached / uncached), update helpers (request
bit-field, num-host-packets, all-reports, cum / interval data stats,
tx-packet stats, tx-latency storage alloc/free), and tx counter
helpers (tx-OK / tx-queue / tx-fail counters, llw histogram, llw
consecutive-error count).

### Confirmed exports

Thirty-three symbols recovered from `IO80211Family.kc` (BootKC,
2026-04-28); see
`docs/reference/AppleBCMWLAN_peer_caching_state_batch_2026_04_28.md`
and
`docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/134_peer_caching_state_batch_2026_04_28.yaml`.

### Fix

Header-only: extend `class IO80211Peer` body with thirty-three new
public method declarations adjacent to the CR-193 entries. None of
the names match a method already declared in the local class body,
and the local IO80211Peer has no declared base class, so all
declarations are non-virtual.

### Binary Invariance

- kext sha256 pre  = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- kext sha256 post = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- UUID pre/post    = `BA3D771F-F079-33FF-94E5-C792E66237D8`
- regdiag sha256   = `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`

CR-194 is a purely additive header extension; the kext is bit-identical
to CR-193.

### Non-claims (CR-194)

- Does not synthesize `AppleBCMWLANPCIeSkywalkPacket`.
- Does not write `packet+0x78`.
- Does not change packet allocation, queueing, input, or output behavior.
- Does not force EAPOL TX, key install, RSN, DHCP, link, or data success.
- Does not add retry, replay, delay, poll loop, packet synthesis, or
  deauth masking.
- Does not call any of the thirty-three Peer methods from any local
  code path.
- Does not declare `IO80211Peer` with any new base class or data
  layout beyond what was already present.
- Does not include kernel-internal types whose definitions are not yet
  recovered.

## A-CR196 — IO80211LinkQualityMonitor primitive-only batch (NEW header)

### Anomaly

`IO80211LinkQualityMonitor` had no local header. The class has 230
public exports in BootKC IO80211Family.kc, of which 48 use only
primitive parameters (`bool`, `int`, `unsigned char`, `signed char`,
`unsigned int`, `unsigned long long`, `long long`) and zero kernel-
internal struct/enum types. None of these 48 helpers had any local
declaration prior to CR-196.

### Justification class

REFERENCE_ALIGNMENT_FIX. Header-only. Introduces the new local
header `include/Airport/IO80211LinkQualityMonitor.h` that declares
the class with no base class and adds the 48 primitive-only direct-
call exports. The local kext does not allocate, subclass, or take
`sizeof` of `IO80211LinkQualityMonitor`; consumers only ever hold
opaque pointers returned from IO80211Family.

### Local change (CR-196)

NEW file: `include/Airport/IO80211LinkQualityMonitor.h` — 48 public
non-virtual method declarations, each anchored to its BootKC
address in the file's leading comment block.

### Binary Invariance

- kext sha256 pre  = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- kext sha256 post = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- UUID pre/post    = `BA3D771F-F079-33FF-94E5-C792E66237D8`
- regdiag sha256   = `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`

CR-196 is a purely additive header introduction; the kext is bit-
identical to CR-189..CR-195.

### Non-claims (CR-196)

- Does not synthesize `AppleBCMWLANPCIeSkywalkPacket`.
- Does not write `packet+0x78`.
- Does not change packet allocation, queueing, input, or output behavior.
- Does not force EAPOL TX, key install, RSN, DHCP, link, or data success.
- Does not add retry, replay, delay, poll loop, packet synthesis, or
  deauth masking.
- Does not call any of the 48 LQM methods from any local code path.
- Does not declare `IO80211LinkQualityMonitor` with any base class or
  data layout.
- Does not include kernel-internal types whose definitions are not yet
  recovered.

## A-CR198 — IO80211BSSBeacon primitive-only batch (NEW header)

### Anomaly

`IO80211BSSBeacon` had no local header. The class has 200 public
exports in BootKC IO80211Family.kc, of which 102 use only primitive
parameter and primitive-pointer return types (`bool`, `int`,
`short`, `signed char`, `unsigned char`, `unsigned short`,
`unsigned int`, `unsigned long long`, `unsigned char *`,
`const char *`) with zero kernel-internal struct/enum types. None of
these 102 helpers had any local declaration prior to CR-198 / CR-199.
CR-199 supersedes CR-198 by deferring the five exports whose return
types are unrecovered kernel-internal pointers (`getLogger()`,
`getSSID()`, `getOWETransSSID()`, `getRnRContext()`,
`getQueueChain()`); CR-198 had declared those five with `void *`,
which the reviewer flagged as type erasure inside a 1:1 reference-
alignment patch.

### Justification class

REFERENCE_ALIGNMENT_FIX. Header-only. Introduces the new local
header `include/Airport/IO80211BSSBeacon.h` that declares the class
with no base class and adds the 102 primitive-only direct-call
exports. The local kext does not allocate, subclass, or take
`sizeof` of `IO80211BSSBeacon`; consumers only ever hold opaque
pointers returned from IO80211Family.

### Local change (CR-199)

NEW file: `include/Airport/IO80211BSSBeacon.h` — 102 public
non-virtual method declarations, each anchored to its BootKC
address in the file's leading comment block.

### Binary Invariance

- kext sha256 pre  = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- kext sha256 post = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- UUID pre/post    = `BA3D771F-F079-33FF-94E5-C792E66237D8`
- regdiag sha256   = `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`

CR-199 is a purely additive header introduction; the kext is bit-
identical to CR-189..CR-198.

### Non-claims (CR-199)

- Does not synthesize `AppleBCMWLANPCIeSkywalkPacket`.
- Does not write `packet+0x78`.
- Does not change packet allocation, queueing, input, or output behavior.
- Does not force EAPOL TX, key install, RSN, DHCP, link, or data success.
- Does not add retry, replay, delay, poll loop, packet synthesis, or
  deauth masking.
- Does not call any of the 102 BSSBeacon methods from any local code path.
- Does not declare `IO80211BSSBeacon` with any base class or
  data layout.
- Does not include kernel-internal types whose definitions are not yet
  recovered (in particular, the five exports `getLogger()`, `getSSID()`,
  `getOWETransSSID()`, `getRnRContext()`, `getQueueChain()` are deferred
  until their reference return types are documented from decomp evidence).

## A-CR200 — IO80211BssManager primitive-only batch (NEW header)

> **Status: SUPERSEDED_BY_CR201.** CR-200's 41-helper batch was
> rejected on 2026-04-28 because each declared return type was
> inferred from naming convention rather than backed by decompile
> evidence. CR-201 narrows the batch to fourteen helpers whose
> return type matches the verbatim Ghidra C decompile output;
> the remaining twenty-seven are deferred. See A-CR201 below.

### Anomaly

`IO80211BssManager` had no local header. The class has 110 public
exports in BootKC IO80211Family.kc. The CR-200 candidate set was
forty one helpers selected for primitive-only parameter signatures.

### Local change (CR-200)

NEW file: `include/Airport/IO80211BssManager.h` — 41 public
non-virtual method declarations. Rejected: return types declared by
naming-convention inference, not decompile evidence.

## A-CR201 — IO80211BssManager primitive-only batch (decomp-evidenced)

### Anomaly

Same source anomaly as A-CR200: `IO80211BssManager` had no local
header. CR-201 supersedes CR-200 with a decompile-evidenced subset of
the same forty-one-helper candidate set.

### Justification class

REFERENCE_ALIGNMENT_FIX. Header-only. Introduces the new local
header `include/Airport/IO80211BssManager.h` declaring the class with
no base class and adding fourteen primitive-only direct-call exports.
The local kext does not allocate, subclass, or take `sizeof` of
`IO80211BssManager`; consumers only ever hold opaque pointers
returned from IO80211Family. None of the 14 helpers is called by any
local code path — pre-existing kext mentions of `IO80211BssManager`
are documentation comments only.

### Local change (CR-201)

NEW file: `include/Airport/IO80211BssManager.h` — 14 public
non-virtual method declarations. Each declared return type is the
verbatim type emitted by Ghidra 12.2's C decompile of
`BootKernelExtensions.kc` at the corresponding address. The full
decompile output is captured in `analysis/cr201_bssmgr_decomp.c`.
No `void *` return is used as a substitute for any unrecovered
kernel-internal pointer; helpers whose decompile produced an opaque
placeholder return type (`undefined4` / `undefined8`) are deferred.

### Binary Invariance

- kext sha256 pre  = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- kext sha256 post = `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`
- UUID pre/post    = `BA3D771F-F079-33FF-94E5-C792E66237D8`
- regdiag sha256   = `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`

CR-201 is a purely additive header introduction; the kext is bit-
identical to CR-189..CR-200.

### Non-claims (CR-201)

- Does not synthesize `AppleBCMWLANPCIeSkywalkPacket`.
- Does not write `packet+0x78`.
- Does not change packet allocation, queueing, input, or output behavior.
- Does not force EAPOL TX, key install, RSN, DHCP, link, or data success.
- Does not add retry, replay, delay, poll loop, packet synthesis, or
  deauth masking.
- Does not call any of the 14 BssManager methods from any local code
  path.
- Does not declare `IO80211BssManager` with any base class or data
  layout.
- Does not promote any deferred helper's `undefined4`/`undefined8`
  decompile placeholder to a concrete C++ type.
- Does not substitute `void *` for any unrecovered kernel-internal
  return type.
- Does not claim coverage for the twenty-seven deferred helpers from
  the CR-200 candidate set; their decompile evidence is opaque or
  the decompile failed (`MISSING`) at the recorded address. They
  remain deferred along with the previously-noted kernel-internal-
  typed helpers.
