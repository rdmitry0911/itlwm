# IO80211PeerManager Infra BSSID/SSID/Channel/RSSI Helpers

Date: 2026-04-28

Scope: non-virtual exported infra-config helpers on `IO80211PeerManager`
selected by CR-179. These thirteen methods extend the eleven CR-178
data-path / lookup helpers with the BSSID, SSID, channel, TX-state, and
RSSI accessors that any future caller-wiring CR will need when reading
or programming the cached infra association state.

## Reference Evidence

`/srv/project/ghidra_additional/kc_target_symbols.txt` (BootKC,
IO80211Family, macOS Tahoe 26.x):

```
ffffff80021df07a  IO80211PeerManager::getInfraBssid()
ffffff80021df2de  IO80211PeerManager::getInfraSsidLen()
ffffff80021df2ee  IO80211PeerManager::setInfraSsidLen(unsigned int)
ffffff80021df08a  IO80211PeerManager::getInfraSsidBytes()
ffffff80021df09a  IO80211PeerManager::setInfraSsidBytes(unsigned char *,
                                                        unsigned int)
ffffff80021d4e36  IO80211PeerManager::setInfraTxState(bool)
ffffff80021d4eb0  IO80211PeerManager::setInfraChannel(apple80211_channel *)
ffffff80021d4e72  IO80211PeerManager::copyInfraChannel(apple80211_channel *)
ffffff80021d4e90  IO80211PeerManager::resetInfraChannel()
ffffff80021df04c  IO80211PeerManager::setInfraChannelInfo(
                          apple80211_channel *)
ffffff80021df06a  IO80211PeerManager::setInfraChannelFlags(unsigned int)
ffffff80021df994  IO80211PeerManager::getInfraRSSI()
ffffff80021df984  IO80211PeerManager::setInfraRSSI(int)
```

The infra-config cluster is the third batch of public peer-manager
exports the local kext aligns with the BootKC. Together with the seven
membership helpers from CR-176 and the eleven data-path / lookup
helpers from CR-178, this brings the locally declared peer-manager
surface to thirty-one exported methods — still well below the 220+
total but covering the slice most relevant to the EAPOL TX / key
install / RSN_DONE blocker layer.

`getInfraBssid()` returns the cached BSSID as an `ether_addr *`.
`getInfraSsidLen()` / `setInfraSsidLen(unsigned int)` and
`getInfraSsidBytes()` / `setInfraSsidBytes(unsigned char *, unsigned
int)` form the SSID accessor pair: byte buffer + length, with the
caller responsible for the byte storage. `setInfraTxState(bool)` is
the per-interface TX gate exposed for the data-path open/close path.
`setInfraChannel`, `copyInfraChannel`, `resetInfraChannel`,
`setInfraChannelInfo`, and `setInfraChannelFlags` together form the
channel-config quartet. `getInfraRSSI` / `setInfraRSSI` expose the
integer RSSI cache used by IORegistry reporting.

## CR-179 Header Alignment

`include/Airport/IO80211PeerManager.h` (extended):

```c++
class IO80211PeerManager {
public:
    // ... CR-176 + CR-178 helpers omitted for brevity ...

    ether_addr *getInfraBssid(void);
    unsigned int getInfraSsidLen(void);
    void setInfraSsidLen(unsigned int);
    unsigned char *getInfraSsidBytes(void);
    void setInfraSsidBytes(unsigned char *, unsigned int);
    void setInfraTxState(bool);
    void setInfraChannel(apple80211_channel *);
    void copyInfraChannel(apple80211_channel *);
    void resetInfraChannel(void);
    void setInfraChannelInfo(apple80211_channel *);
    void setInfraChannelFlags(unsigned int);
    int getInfraRSSI(void);
    void setInfraRSSI(int);
};
```

The class remains opaque (no base class, no data layout, no vtable).
Generated kext is bit-identical to CR-178 (sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`,
UUID `BA3D771F-F079-33FF-94E5-C792E66237D8`).

## Deferred Work (out of scope for CR-179)

- Recover the definitions of `scanSource`, `joinStatus`,
  `ApplePeerManagerEvents`, `StateChangedSource`, `ccodeState`, and
  the country-code state-machine enum so further `IO80211PeerManager`
  state-control helpers can be declared.
- Decompile the bodies of the channel mutators to confirm whether
  callers are expected to hold `lockDataPath` first.
- Resolve the open caller-side gates for the peer/link-state path
  documented in CR-175 / CR-176 / CR-177 / CR-178 closures.
- Recover `IO80211InfraInterface::handleKeyDone(bool, bool)` body via
  raw `otool -tV` / `objdump -D` at `0xffffff80022e6f9c`.

The wired CR will be CR-180+ once these gates are decompile-confirmed.

## Non-claims

CR-179 does not:

- synthesize `AppleBCMWLANPCIeSkywalkPacket`;
- write `packet+0x78`;
- change packet allocation, queueing, input, or output behavior;
- force EAPOL TX, key install, RSN, DHCP, link, or data success;
- add retry, replay, delay, poll loop, packet synthesis, or deauth
  masking;
- call any of the thirteen new peer-manager methods from any local
  code path;
- declare `IO80211PeerManager` with any base class or data layout;
- include kernel-internal types whose definitions are not yet
  recovered.
