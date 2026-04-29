# IO80211InterfaceMonitor Leaky-AP, Reporter, and Packet-Record Helpers

Date: 2026-04-28

Scope: non-virtual exported helpers on `IO80211InterfaceMonitor` selected
by CR-182. These ten methods extend the nineteen CR-180 helpers with
the leaky-AP cache, IOReporter lifecycle, and per-packet record helpers
that any future caller-wiring CR will need to drive when the data path
records EAPOL/Ethernet TX/RX events into the InterfaceMonitor.

## Reference Evidence

`/srv/project/ghidra_additional/kc_target_symbols.txt` (BootKC,
IO80211Family, macOS Tahoe 26.x):

```
ffffff80022ef6c8  IO80211InterfaceMonitor::getLeakyApSsid(apple80211_ssid*)
ffffff80022ef714  IO80211InterfaceMonitor::getLeakyApBssid(ether_addr*)
ffffff80022eefa4  IO80211InterfaceMonitor::resetLeakyApStats()
ffffff80022f4f7c  IO80211InterfaceMonitor::setInputPacketRSSI(long long)
ffffff80022f784c  IO80211InterfaceMonitor::recordInputPacket(int, int)
ffffff80022f775c  IO80211InterfaceMonitor::recordOutputPacket(
                     apple80211_wme_ac, int, int)
ffffff80022f1e2c  IO80211InterfaceMonitor::initFrameStats()
ffffff80022f281a  IO80211InterfaceMonitor::initHeFrameStats()
ffffff80022ef0fc  IO80211InterfaceMonitor::destroyReporters()
ffffff80022f752e  IO80211InterfaceMonitor::updateAllReports()
```

Together with the nineteen direct-call helpers from CR-180, this
brings the locally declared InterfaceMonitor direct-call surface to
twenty-nine exported methods — the slice most relevant to recording
data-path activity into the IORegistry/IOReporter visible surface that
the CoreWLAN agent and the system stat collector consume.

`getLeakyApSsid(apple80211_ssid*)` / `getLeakyApBssid(ether_addr*)` /
`resetLeakyApStats()` operate on the InterfaceMonitor's leaky-AP cache.
`setInputPacketRSSI(long long)` records the per-input-packet RSSI used
for windowed averaging. `recordInputPacket(int, int)` and
`recordOutputPacket(apple80211_wme_ac, int, int)` are the per-packet
counter updaters; the latter records on a per-AC basis matching the
CR-180 `getOutput{BE,BK,VI,VO}{Bytes,Packets}` accessors.
`initFrameStats()` / `initHeFrameStats()` allocate the IOReporter
backing storage; `destroyReporters()` is the matching teardown;
`updateAllReports()` re-publishes the entire reporter set.

## CR-182 Header Alignment

`include/Airport/IO80211InterfaceMonitor.h` (extended):

```c++
void getLeakyApSsid(apple80211_ssid *);
void getLeakyApBssid(ether_addr *);
void resetLeakyApStats(void);
void setInputPacketRSSI(long long);
void recordInputPacket(int, int);
void recordOutputPacket(apple80211_wme_ac, int, int);
void initFrameStats(void);
void initHeFrameStats(void);
void destroyReporters(void);
void updateAllReports(void);
```

Forward declarations added at file scope: `struct apple80211_ssid;`,
`struct ether_addr;`, and `enum apple80211_wme_ac : unsigned int;`.

Generated kext is bit-identical to CR-181 (sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`,
UUID `BA3D771F-F079-33FF-94E5-C792E66237D8`).

## Deferred Work (out of scope for CR-182)

- Decompile the bodies of `initFrameStats` / `initHeFrameStats` /
  `destroyReporters` to confirm reporter-channel layout and ordering
  vs `getController()` / `IOReporter::createLegend`.
- Identify the leaky-AP cache writer path (which IO80211Peer transition
  populates the SSID/BSSID slots queried here).
- Resolve the open caller-side gates carried over from CR-181 (peer/
  link-state preconditions, `handleKeyDone` body recovery).

The wired CR will be CR-183+ once these gates are decompile-confirmed.

## Non-claims

CR-182 does not:

- synthesize `AppleBCMWLANPCIeSkywalkPacket`;
- write `packet+0x78`;
- change packet allocation, queueing, input, or output behavior;
- force EAPOL TX, key install, RSN, DHCP, link, or data success;
- add retry, replay, delay, poll loop, packet synthesis, or deauth
  masking;
- call any of the ten new InterfaceMonitor methods from any local
  code path;
- declare `IO80211InterfaceMonitor` with any base class or data layout
  beyond what was already in the local header;
- include kernel-internal types whose definitions are not yet
  recovered.
