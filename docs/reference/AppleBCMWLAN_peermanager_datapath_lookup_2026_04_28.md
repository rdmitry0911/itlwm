# IO80211PeerManager Data-Path + Peer-Lookup Helpers

Date: 2026-04-28

Scope: non-virtual exported peer-manager helpers on `IO80211PeerManager`
selected by CR-178. These eleven methods extend the seven member-set
helpers declared in CR-176 with the lookup and data-path-control
surface needed by any future caller-wiring CR that operates on the
EAPOL TX / key install / RSN_DONE blocker layer.

## Reference Evidence

`/srv/project/ghidra_additional/kc_target_symbols.txt` (BootKC,
IO80211Family, macOS Tahoe 26.x):

```
ffffff80021d1388  IO80211PeerManager::findPeer(unsigned char *)
ffffff80021d3f0c  IO80211PeerManager::findCachedPeer(unsigned char *)
ffffff80021df2a8  IO80211PeerManager::getUnicastPeer()
ffffff80021df296  IO80211PeerManager::getMulticastPeer()
ffffff80021df672  IO80211PeerManager::getEnabled()
ffffff80021cc798  IO80211PeerManager::setEnableState(bool)
ffffff80021df4f8  IO80211PeerManager::getDataPathOpen()
ffffff80021df50a  IO80211PeerManager::setDataPathOpen(bool)
ffffff80021cde60  IO80211PeerManager::setDataPathState(bool)
ffffff80021cded6  IO80211PeerManager::lockDataPath()
ffffff80021cdfca  IO80211PeerManager::unlockDataPath()
```

The `IO80211PeerManager` symbol set holds 220+ exports total in BootKC.
CR-178 deliberately restricts scope to the lookup + data-path-control
cluster whose C++ signatures contain no kernel-internal enum or class
types (e.g. `scanSource`, `joinStatus`, `ApplePeerManagerEvents`,
`StateChangedSource`, `ccodeState`) so that the mangled names resolve
unambiguously without forcing recovery of those types.

`findPeer(unsigned char *addr)` is the public hash-table lookup used by
every consumer that needs an `IO80211Peer *` from a six-byte address.
`findCachedPeer(...)` is the variant that consults the post-flush peer
cache. `getUnicastPeer()` / `getMulticastPeer()` return the
well-known per-interface peers without touching the hash table.

`setEnableState`/`getEnabled` and `setDataPathOpen`/`getDataPathOpen`
are the two paired flag accessors that gate, respectively, peer-manager
enablement and data-path admission. `setDataPathState(bool)` is the
broader latch that the kernel toggles when transitioning peers in/out
of the open data-path state. `lockDataPath()`/`unlockDataPath()` form
the matched mutex pair that protects every data-path mutation.

## CR-178 Header Alignment

`include/Airport/IO80211PeerManager.h` (extended):

```c++
class IO80211Peer;
struct apple80211_peer_stats;

class IO80211PeerManager {
public:
    // CR-176 helpers omitted here for brevity.

    IO80211Peer *findPeer(unsigned char *addr);
    IO80211Peer *findCachedPeer(unsigned char *addr);
    IO80211Peer *getUnicastPeer(void);
    IO80211Peer *getMulticastPeer(void);

    bool getEnabled(void);
    void setEnableState(bool);
    bool getDataPathOpen(void);
    void setDataPathOpen(bool);
    void setDataPathState(bool);
    void lockDataPath(void);
    void unlockDataPath(void);
};
```

The class remains opaque (no base class, no data layout, no vtable).
The local kext only ever holds an `IO80211PeerManager *` returned by
the kernel; it does not allocate, subclass, or take `sizeof` of the
class.

Generated kext is bit-identical to CR-177 (sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`,
UUID `BA3D771F-F079-33FF-94E5-C792E66237D8`).

## Deferred Work (out of scope for CR-178)

- Recover the definitions of `scanSource`, `joinStatus`,
  `ApplePeerManagerEvents`, `StateChangedSource`, and `ccodeState`
  enums so that the next batch of `IO80211PeerManager::set*State(...)`
  helpers can be declared in a future CR.
- Decompile the body of `IO80211PeerManager::setDataPathState(bool)`
  to confirm whether it acquires `lockDataPath` internally or expects
  the caller to hold the lock.
- Resolve the open caller-side gates for the peer/link-state path
  documented in CR-175 / CR-176 / CR-177 closures.
- Recover `IO80211InfraInterface::handleKeyDone(bool, bool)` body via
  raw `otool -tV` / `objdump -D` at `0xffffff80022e6f9c` (Ghidra "Bad
  instruction" truncation).

The wired CR will be CR-179+ once these gates are decompile-confirmed.

## Non-claims

CR-178 does not:

- synthesize `AppleBCMWLANPCIeSkywalkPacket`;
- write `packet+0x78`;
- change packet allocation, queueing, input, or output behavior;
- force EAPOL TX, key install, RSN, DHCP, link, or data success;
- add retry, replay, delay, poll loop, packet synthesis, or deauth
  masking;
- call any of the eleven peer-manager methods from any local code
  path;
- declare `IO80211PeerManager` with any base class or data layout;
- include kernel-internal types whose definitions are not yet
  recovered.
