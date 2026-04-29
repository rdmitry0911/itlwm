# IO80211Peer Public Direct-Call API Surface

Date: 2026-04-28

Scope: non-virtual exported peer helpers on `IO80211Peer` selected by
CR-177. These six are the minimum public surface needed by any future
caller-wiring CR that constructs, identifies, or queries a peer object.

## Reference Evidence

`/srv/project/ghidra_additional/kc_target_symbols.txt` (BootKC,
IO80211Family, macOS Tahoe 26.x):

```
ffffff80021bf64a  IO80211Peer::withAddressAndManager(
                      unsigned char const *, IO80211PeerManager *)
ffffff80021bf6c0  IO80211Peer::init()
ffffff80021bff7a  IO80211Peer::getMacAddress()
ffffff80021c5df4  IO80211Peer::setMacAddress(ether_addr *)
ffffff80021c3558  IO80211Peer::getManager()
ffffff80021c60dc  IO80211Peer::getGeneration()
```

The symbol table holds 230 `IO80211Peer::*` exports total. CR-177
deliberately restricts scope to symbols whose C++ signatures contain no
kernel-internal enum or class types (e.g. `peerState`,
`peerMonitoringCtx`, `peerDataLogRequest`) so that the mangled names
resolve unambiguously even before those types are recovered.

`withAddressAndManager(...)` is the OSObject-style factory that returns a
freshly constructed peer. Decompiled body
(`FUN_ffffff80021bf64a`) shows it allocates from
`DAT_ffffff800240e480` with a 0x20-byte block and tail-calls
`IO80211Peer::initWithAddressAndManager(addr, manager)`.

`initWithAddressAndManager(...)` (`FUN_ffffff80021bf6f4`) stores the
address at `this[3]+0x58/0x5c`, the manager at `this[3]+0x60`, retains
the manager via vtable slot `+0x20`, reads `manager->something+0xc38`
into `this[3]+0x500`, and computes priority from low-nibble of
`this[3]+0x5d` into `this[3]+0x104`.

## CR-177 Header Alignment

`include/Airport/IO80211Peer.h` (new file):

```c++
class IO80211PeerManager;
struct ether_addr;

class IO80211Peer {
public:
    static IO80211Peer *withAddressAndManager(unsigned char const *addr,
                                              IO80211PeerManager *manager);
    bool init(void);
    ether_addr *getMacAddress(void);
    void setMacAddress(ether_addr *addr);
    IO80211PeerManager *getManager(void);
    unsigned int getGeneration(void);
};
```

The class is declared opaque (no base class, no data layout, no vtable).
The local kext only ever holds an `IO80211Peer *` returned by the kernel;
it does not allocate, subclass, or take `sizeof` of the class.

Generated kext is bit-identical to CR-176 (sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`,
UUID `BA3D771F-F079-33FF-94E5-C792E66237D8`).

## Deferred Work (out of scope for CR-177)

- Recover the definition of the `peerState` enum so that
  `IO80211Peer::getState()` and `IO80211Peer::setState(peerState)` can be
  declared in a future CR.
- Recover the definition of `peerMonitoringCtx`, `peerDataLogRequest`,
  and other kernel-internal peer types referenced by additional
  exported helpers.
- Resolve the open caller-side gates for the peer/link-state path
  documented in CR-175 / CR-176 closures.
- Recover `IO80211InfraInterface::handleKeyDone(bool, bool)` body via raw
  `otool -tV` / `objdump -D` at `0xffffff80022e6f9c` (Ghidra "Bad
  instruction" truncation).

The wired CR will be CR-178+ once these gates are decompile-confirmed.

## Non-claims

CR-177 does not:

- synthesize `AppleBCMWLANPCIeSkywalkPacket`;
- write `packet+0x78`;
- change packet allocation, queueing, input, or output behavior;
- force EAPOL TX, key install, RSN, DHCP, link, or data success;
- add retry, replay, delay, poll loop, packet synthesis, or deauth
  masking;
- call any of the six peer methods from any local code path;
- declare `IO80211Peer` with any base class or data layout;
- include kernel-internal types whose definitions are not yet recovered.
