# IO80211PeerManager Public Direct-Call API Surface

Date: 2026-04-28

Scope: non-virtual exported peer-management helpers on
`IO80211PeerManager` that the local include directory had no canonical
home for before CR-176.

## Reference Evidence

`/srv/project/ghidra_additional/kc_target_symbols.txt` (BootKC,
IO80211Family, macOS Tahoe 26.x):

```
ffffff80021d3f58  IO80211PeerManager::addPeer(unsigned char *)
ffffff80021d7ba0  IO80211PeerManager::addPeerOperation()
ffffff80021d4452  IO80211PeerManager::removePeer(IO80211Peer *)
ffffff80021d4806  IO80211PeerManager::removePeer(unsigned char *)
ffffff80021d7c7e  IO80211PeerManager::removePeerOperation()
ffffff80021df2fe  IO80211PeerManager::getPeerList()
ffffff80021d298e  IO80211PeerManager::getPeerStats(
                      apple80211_peer_stats *)
```

These are direct-call symbols (not vtable entries). They form the public
peer-lifecycle surface for any kext that registers a station peer or
queries the active peer list.

`addPeer(unsigned char *addr)` is the entry-point that any caller would
use to register a new peer with a given MAC. Its decompiled body
(`FUN_ffffff80021d3f58`):

- returns `0x1502` if `addr == NULL`;
- checks the manager's single-peer slot at `manager[3] + 0x530`;
- otherwise calls
  `FUN_ffffff80021b9f7a(pool, &LAB_ffffff80021c64e1, manager, addr, 0,
   0xffffffff)` to allocate from the peer pool at `manager[3] + 0x538`;
- calls `FUN_ffffff80021bf64a(addr, manager)`
  (`IO80211InfraPeer::withAddressAndPeerManager`) to construct the peer;
- inserts via `FUN_ffffff80021c4cf4(peer, manager + 0x518)`;
- posts a command-gate notification via `FUN_ffffff80021d42a6(manager,
   count)`.

`addPeerOperation()` is a thin command-gate wrapper that ultimately
tail-calls `FUN_ffffff8000b00f40(gate, cookie, 1)` after labelling the
operation with the magic cookie `0x4966506565724164` (`"IfPeerAd"`).

`getPeerList()` and `getPeerStats(apple80211_peer_stats *)` are accessors
exposed for status-query paths.

## CR-176 Header Alignment

`include/Airport/IO80211PeerManager.h` (new file):

```c++
class IO80211Peer;
struct apple80211_peer_stats;

class IO80211PeerManager {
public:
    // CR-205 correction: addPeer return type changed from IOReturn to
    // IO80211Peer * based on raw x86_64 disasm at 0xffffff80021d3f58
    // (full 64-bit `MOV RAX, R14; ...; RET` epilogue with R14 carrying
    // the peer pointer; null/error paths zero R14D). See
    // `analysis/cr205_addpeer_disasm.txt`.
    IO80211Peer *addPeer(unsigned char *addr);
    IOReturn addPeerOperation(void);
    void removePeer(IO80211Peer *peer);
    IOReturn removePeer(unsigned char *addr);
    IOReturn removePeerOperation(void);
    void *getPeerList(void);
    IOReturn getPeerStats(apple80211_peer_stats *stats);
};
```

The class is declared opaque (no base class, no data layout, no vtable).
The local kext only ever holds an `IO80211PeerManager *` returned by the
kernel; it does not allocate, subclass, or take `sizeof` of the class.

Generated kext is bit-identical to CR-175 (sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`,
UUID `BA3D771F-F079-33FF-94E5-C792E66237D8`).

## Deferred Work (out of scope for CR-176)

- Resolve open caller-side gates for the peer/link-state path:
  - thread/gate context for `addPeer` / `addPeerOperation`;
  - writer of the `IO80211InfraInterface this[0x24] + 0x58 == 1`
    precondition for `setLinkState(state=2)`;
  - ordering of `setCurrentApAddress` vs `setLinkState`;
- Recover `IO80211InfraInterface::handleKeyDone(bool, bool)` body via raw
  `otool -tV` / `objdump -D` at `0xffffff80022e6f9c` (Ghidra "Bad
  instruction" truncation).

The wired CR will be CR-177+ once these gates are decompile-confirmed.

## Non-claims

CR-176 does not:

- synthesize `AppleBCMWLANPCIeSkywalkPacket`;
- write `packet+0x78`;
- change packet allocation, queueing, input, or output behavior;
- force EAPOL TX, key install, RSN, DHCP, link, or data success;
- add retry, replay, delay, poll loop, packet synthesis, or deauth
  masking;
- call `addPeer`, `addPeerOperation`, `removePeer`, `removePeerOperation`,
  `getPeerList`, or `getPeerStats` from any local code path;
- declare `IO80211PeerManager` with any base class or data layout.
