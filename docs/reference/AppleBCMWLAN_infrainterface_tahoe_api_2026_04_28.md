# IO80211InfraInterface Tahoe Direct-Call API Surface

Date: 2026-04-28

Scope: non-virtual exported helpers on `IO80211InfraInterface` that are
relevant to the active EAPOL/key/RSN_DONE blocker layer but were not
declared by the local header before CR-175.

## Reference Evidence

`/srv/project/ghidra_additional/kc_target_symbols.txt` (BootKC,
IO80211Family, macOS Tahoe 26.x):

```
ffffff80022e1148  IO80211InfraInterface::getInfraPeer()
ffffff80022e5ef8  IO80211InfraInterface::getCurrentApAddress()
ffffff80022e6f9c  IO80211InfraInterface::handleKeyDone(bool, bool)
ffffff80022e116e  IO80211InfraInterface::bssidChange(void *, unsigned long)
```

These are direct-call symbols (not vtable entries). They can be linked
against directly from kext code without affecting the vtable layout.

`getInfraPeer()` and `getCurrentApAddress()` are obviously read-only
accessors that surface the internally-tracked peer / current AP MAC.
`bssidChange(void *, unsigned long)` is invoked when the peer's BSSID
changes during association.

`handleKeyDone(bool, bool)` is the EAPOL/RSN bookkeeping hook. Its body
at `0xffffff80022e6f9c` currently reports a Ghidra "Bad instruction"
truncation, so the exact internal sequence is not yet readable. Until
the body is recovered, CR-175 does not wire any caller — it only
declares the symbol so that future caller patches do not have to add a
per-callsite mangled extern.

## CR-175 Header Alignment

`include/Airport/IO80211InfraInterface.h` adds non-virtual declarations
under `#if __IO80211_TARGET >= __MAC_26_0`:

```c++
IO80211Peer *getInfraPeer(void);
ether_addr *getCurrentApAddress(void);
void handleKeyDone(bool, bool);
void bssidChange(void *, unsigned long);
```

Each declaration is anchored to its BootKC address in a comment block.

The vtable layout is unaffected — these are not virtual methods.
Generated kext is bit-identical to CR-174 (sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`,
UUID `BA3D771F-F079-33FF-94E5-C792E66237D8`).

## Deferred Work (out of scope for CR-175)

- Recover `handleKeyDone(bool, bool)` body via raw `otool -tV` /
  `objdump -D` at `0xffffff80022e6f9c` so the actual internal sequence
  is known before any caller wiring.
- Resolve open caller-side gates for the peer/link-state path:
  - thread/gate context for `addPeer` / `addPeerOperation`,
  - writer of the `IO80211InfraInterface this[0x24]+0x58 == 1`
    precondition for `setLinkState(state=2)`,
  - ordering of `setCurrentApAddress` vs `setLinkState`.

The wired CR will be CR-176+ after the above are decompile-confirmed.

## Non-claims

CR-175 does not:

- synthesize `AppleBCMWLANPCIeSkywalkPacket`;
- write `packet+0x78`;
- change packet allocation, queueing, input, or output behavior;
- force EAPOL TX, key install, RSN, DHCP, link, or data success;
- add retry, replay, delay, poll loop, packet synthesis, or deauth
  masking;
- call `getInfraPeer`, `getCurrentApAddress`, `handleKeyDone`, or
  `bssidChange` from any local code path.
