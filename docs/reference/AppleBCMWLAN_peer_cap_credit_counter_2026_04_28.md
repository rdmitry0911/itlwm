# IO80211Peer Capability / Credit / Counter Accessor Helpers

Date: 2026-04-28

Scope: non-virtual exported helpers on `IO80211Peer` selected by
CR-183. These twenty-five accessors extend the six CR-177 helpers
with the capability flags, TX-credit accounting, RX/TX sequence
counters, and cache/SoftAP-peer state queries that any future
caller-wiring CR will need to drive when the data path enqueues
EAPOL/Ethernet frames or evaluates per-peer transmit gating.

## Reference Evidence

`/srv/project/ghidra_additional/kc_target_symbols.txt` (BootKC,
IO80211Family, macOS Tahoe 26.x):

```
ffffff80021c3376  IO80211Peer::getHtCapable()
ffffff80021c3388  IO80211Peer::setHtCapable(bool)
ffffff80021c339a  IO80211Peer::getVhtCapable()
ffffff80021c33ac  IO80211Peer::setVhtCapable(bool)
ffffff80021c33ce  IO80211Peer::isHeSupported()
ffffff80021c33e0  IO80211Peer::setHeSupported(bool)
ffffff80021c3836  IO80211Peer::is6ECapable()
ffffff80021c3824  IO80211Peer::set6ECapable(bool)
ffffff80021c5d86  IO80211Peer::hasHTorVHTCaps()
ffffff80021c3af0  IO80211Peer::canTransmit(unsigned int, unsigned int)
ffffff80021c3a1c  IO80211Peer::canTransmitReason(unsigned int, unsigned int)
ffffff80021c5f02  IO80211Peer::getOpenCredits()
ffffff80021c5ef4  IO80211Peer::getCloseCredits()
ffffff80021c5fcc  IO80211Peer::getNumTxPacket()
ffffff80021c3814  IO80211Peer::getOutputSuccess()
ffffff80021c370a  IO80211Peer::getTxQuantum()
ffffff80021c371a  IO80211Peer::setTxQuantum(unsigned int)
ffffff80021c5dd6  IO80211Peer::getNextTxSeq(unsigned char)
ffffff80021c3682  IO80211Peer::getRxSequence()
ffffff80021c3672  IO80211Peer::getRxSequenceMulticast()
ffffff80021c372a  IO80211Peer::setTransmitOk(bool)
ffffff80021c5da0  IO80211Peer::isCachedInFw()
ffffff80021c5db2  IO80211Peer::setCachedInFw(bool)
ffffff80021c5ec8  IO80211Peer::isSoftAPPeer()
ffffff80021c5ed8  IO80211Peer::setSoftAPPeer(bool)
```

This is the third batch of public Peer exports the local kext aligns
with the BootKC. Together with the six direct-call helpers from
CR-177, this brings the locally declared Peer direct-call surface to
thirty-one exported methods — the slice most relevant to the per-Peer
TX gating, capability-bitmap, and FW-cache-state side of the EAPOL TX
/ key install / RSN_DONE blocker.

`getXxxCapable` / `setXxxCapable` (HT, VHT, HE, 6E) are the per-Peer
capability flag accessors. `hasHTorVHTCaps()` is the OR predicate.
`canTransmit(unsigned int, unsigned int)` and `canTransmitReason(...)`
are the per-Peer TX gates evaluated by the data-path TX submitter.
`getOpenCredits()` / `getCloseCredits()` / `getNumTxPacket()` /
`getOutputSuccess()` expose the per-Peer credit/counter state that the
flow-control layer queries. `getTxQuantum()` / `setTxQuantum(...)` /
`getNextTxSeq(...)` / `getRxSequence()` / `getRxSequenceMulticast()`
expose the TX/RX sequencing state. `setTransmitOk(...)` flips the
per-Peer TX permission. `isCachedInFw()` / `setCachedInFw(...)` /
`isSoftAPPeer()` / `setSoftAPPeer(...)` are the FW-cache and
SoftAP-peer flags.

## CR-183 Header Alignment

`include/Airport/IO80211Peer.h` (extended):

```c++
bool getHtCapable(void);
void setHtCapable(bool);
bool getVhtCapable(void);
void setVhtCapable(bool);
bool isHeSupported(void);
void setHeSupported(bool);
bool is6ECapable(void);
void set6ECapable(bool);
bool hasHTorVHTCaps(void);
bool canTransmit(unsigned int, unsigned int);
unsigned int canTransmitReason(unsigned int, unsigned int);
unsigned int getOpenCredits(void);
unsigned int getCloseCredits(void);
unsigned long long getNumTxPacket(void);
bool getOutputSuccess(void);
unsigned int getTxQuantum(void);
void setTxQuantum(unsigned int);
unsigned char getNextTxSeq(unsigned char);
unsigned char getRxSequence(void);
unsigned char getRxSequenceMulticast(void);
void setTransmitOk(bool);
bool isCachedInFw(void);
void setCachedInFw(bool);
bool isSoftAPPeer(void);
void setSoftAPPeer(bool);
```

Generated kext is bit-identical to CR-182 (sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`,
UUID `BA3D771F-F079-33FF-94E5-C792E66237D8`).

Note: return types listed above are conventional defaults (capability
predicates → `bool`, credit accumulators → `unsigned int` /
`unsigned long long`, sequence counters → `unsigned char`). The
Itanium C++ ABI mangled-name resolution used by BootKC undef-symbol
verification does not depend on return type, so these declarations
match the exported symbols by mangled name regardless. The bodies
will be cross-checked against the decompile when the wired CR
introduces live callers.

## Deferred Work (out of scope for CR-183)

- Recover the `IO80211Peer` constructor / `init` body to identify
  which fields the capability/credit accessors back onto.
- Confirm thread/gate context for the capability setters (typically
  dispatch-queue gated).
- Establish the ordering of `canTransmit` / `setTransmitOk` relative
  to the data-path TX submitter.
- Resolve the open caller-side gates carried over from CR-181/CR-182
  (peer/link-state preconditions, `handleKeyDone` body recovery).

The wired CR will be CR-184+ once these gates are decompile-confirmed.

## Non-claims

CR-183 does not:

- synthesize `AppleBCMWLANPCIeSkywalkPacket`;
- write `packet+0x78`;
- change packet allocation, queueing, input, or output behavior;
- force EAPOL TX, key install, RSN, DHCP, link, or data success;
- add retry, replay, delay, poll loop, packet synthesis, or deauth
  masking;
- call any of the twenty-five new Peer methods from any local code
  path;
- declare `IO80211Peer` with any base class or data layout beyond
  what was already in the local header;
- include kernel-internal types whose definitions are not yet
  recovered.
