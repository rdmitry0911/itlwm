# IO80211InfraInterface IORegistry Property Updater Helpers

Date: 2026-04-28

Scope: non-virtual exported helpers on `IO80211InfraInterface` selected
by CR-181. These eleven methods extend the four CR-175 helpers with
the IORegistry property updaters that any future caller-wiring CR will
need to drive when association state changes — i.e. the path that
keeps the visible `IO80211*` IORegistry properties (including
`IO80211RSNDone`) in sync with the kext-internal cache.

## Reference Evidence

`/srv/project/ghidra_additional/kc_target_symbols.txt` (BootKC,
IO80211Family, macOS Tahoe 26.x):

```
ffffff80022e1a56  IO80211InfraInterface::updateSSIDProperty()
ffffff80022e2504  IO80211InfraInterface::updateLocaleProperty()
ffffff80022e1f98  IO80211InfraInterface::updateBSSIDProperty(
                     ether_addr &, apple80211_channel &, bool, bool)
ffffff80022e2156  IO80211InfraInterface::updateChannelProperty(
                     apple80211_channel &)
ffffff80022e1b90  IO80211InfraInterface::updateCountryCodeProperty(bool)
ffffff80022de8b2  IO80211InfraInterface::updateStaticProperties()
ffffff80022df728  IO80211InfraInterface::updateLinkSpeed()
ffffff80022e1782  IO80211InfraInterface::loadHwChannels()
ffffff80022e1848  IO80211InfraInterface::loadChannelInfo()
ffffff80022e61ea  IO80211InfraInterface::onDispatchQueue()
ffffff80022dfb9c  IO80211InfraInterface::cancelDebounceTimer()
```

The IORegistry property updater cluster is the second batch of public
InfraInterface exports the local kext aligns with the BootKC. Together
with the four direct-call helpers from CR-175, this brings the locally
declared InfraInterface direct-call surface to fifteen exported methods
— still well below the 100+ total but covering the slice most relevant
to the IORegistry state-progression layer of the EAPOL TX / key install
/ RSN_DONE blocker.

`updateSSIDProperty()` / `updateLocaleProperty()` /
`updateCountryCodeProperty(bool)` / `updateStaticProperties()` are
parameterless updaters that read kext-internal state and re-publish
the corresponding `IO80211*` property to IORegistry.
`updateBSSIDProperty(ether_addr &, apple80211_channel &, bool, bool)`
and `updateChannelProperty(apple80211_channel &)` take the new BSSID
and channel by reference. `updateLinkSpeed()` re-publishes the cached
link-rate; `loadHwChannels()` / `loadChannelInfo()` populate the
hardware channel table; `onDispatchQueue()` returns whether the caller
is currently executing on the InfraInterface dispatch queue;
`cancelDebounceTimer()` cancels the debounce timer used by the link
state path.

## CR-181 Header Alignment

`include/Airport/IO80211InfraInterface.h` (extended, under the
`__IO80211_TARGET >= __MAC_26_0` block):

```c++
void updateSSIDProperty(void);
void updateLocaleProperty(void);
void updateBSSIDProperty(ether_addr &, apple80211_channel &, bool, bool);
void updateChannelProperty(apple80211_channel &);
void updateCountryCodeProperty(bool);
void updateStaticProperties(void);
void updateLinkSpeed(void);
void loadHwChannels(void);
void loadChannelInfo(void);
bool onDispatchQueue(void);
void cancelDebounceTimer(void);
```

Generated kext is bit-identical to CR-180 (sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`,
UUID `BA3D771F-F079-33FF-94E5-C792E66237D8`).

## Deferred Work (out of scope for CR-181)

- Decompile the bodies of the property updaters to confirm whether
  callers are expected to be on the dispatch queue (the
  `onDispatchQueue()` predicate is published, but the contract is not
  yet read out of decomp).
- Recover the definition of `apple80211_infra_scan_start_event_data`
  so that the additional `updateChanOutageTime` helper can be declared
  in a future CR.
- Resolve the open caller-side gates for the peer/link-state path
  documented in CR-175 / CR-176 / CR-177 / CR-178 / CR-179 / CR-180
  closures.
- Recover `IO80211InfraInterface::handleKeyDone(bool, bool)` body via
  raw `otool -tV` / `objdump -D` at `0xffffff80022e6f9c`.

The wired CR will be CR-182+ once these gates are decompile-confirmed.

## Non-claims

CR-181 does not:

- synthesize `AppleBCMWLANPCIeSkywalkPacket`;
- write `packet+0x78`;
- change packet allocation, queueing, input, or output behavior;
- force EAPOL TX, key install, RSN, DHCP, link, or data success;
- add retry, replay, delay, poll loop, packet synthesis, or deauth
  masking;
- call any of the eleven new InfraInterface methods from any local
  code path;
- declare `IO80211InfraInterface` with any base class or data layout
  beyond what was already in the local header;
- include kernel-internal types whose definitions are not yet
  recovered.
