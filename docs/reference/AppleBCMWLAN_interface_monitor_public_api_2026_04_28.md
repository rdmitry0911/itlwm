# IO80211InterfaceMonitor Public Direct-Call API Surface

Date: 2026-04-28

Scope: non-virtual exported helpers on `IO80211InterfaceMonitor`
selected by CR-180. These nineteen methods cover the controller
back-pointer, per-AC bytes/packets counters, RSSI/SNR/NF accessors, and
link-rate / channel mutators — the minimum public surface needed by
any future caller-wiring CR that wants to instrument the data path or
mirror the IORegistry-visible counters.

## Reference Evidence

`/srv/project/ghidra_additional/kc_target_symbols.txt` (BootKC,
IO80211Family, macOS Tahoe 26.x):

```
ffffff80022f7938  IO80211InterfaceMonitor::getController()
ffffff80022f7568  IO80211InterfaceMonitor::getInputBytes()
ffffff80022f7536  IO80211InterfaceMonitor::getInputPackets()
ffffff80022f7694  IO80211InterfaceMonitor::getOutputBEBytes()
ffffff80022f76c6  IO80211InterfaceMonitor::getOutputBKBytes()
ffffff80022f772a  IO80211InterfaceMonitor::getOutputVIBytes()
ffffff80022f76f8  IO80211InterfaceMonitor::getOutputVOBytes()
ffffff80022f759a  IO80211InterfaceMonitor::getOutputBEPackets()
ffffff80022f75cc  IO80211InterfaceMonitor::getOutputBKPackets()
ffffff80022f7662  IO80211InterfaceMonitor::getOutputVIPackets()
ffffff80022f75fe  IO80211InterfaceMonitor::getOutputVOPackets()
ffffff80022f54f4  IO80211InterfaceMonitor::getInterfaceRSSI()
ffffff80022f521e  IO80211InterfaceMonitor::setInterfaceRSSI(long long)
ffffff80022f54c2  IO80211InterfaceMonitor::hasInterfaceRSSI()
ffffff80022f5530  IO80211InterfaceMonitor::setInterfaceSNR(long long)
ffffff80022f57f2  IO80211InterfaceMonitor::setInterfaceNF(long long)
ffffff80022ef27c  IO80211InterfaceMonitor::getLinkRate()
ffffff80022ef20c  IO80211InterfaceMonitor::setLinkRate(unsigned long long)
ffffff80022ef182  IO80211InterfaceMonitor::modifyChID(unsigned long long)
```

The `IO80211InterfaceMonitor` symbol set holds 60+ exports total in
BootKC. CR-180 deliberately restricts scope to the controller
back-pointer + per-AC counter cluster + RSSI/SNR/NF + link-rate
accessors. Excluded for later batches: any helper whose signature
references kernel-internal types (`apple80211_leaky_ap_reason`,
`io80211BinCounters`, `__mbuf`, `IOReportChannelList`,
`apple80211_ampdu_stat_report`).

`getController()` returns the owning `IO80211Controller *`.
`getInputBytes`/`getInputPackets` are unconditional totals; the four
output AC-specific bytes/packets accessors mirror per-class queue
counters used by IORegistry data-path reporters.
`hasInterfaceRSSI` / `getInterfaceRSSI` / `setInterfaceRSSI` form the
RSSI cache; `setInterfaceSNR` / `setInterfaceNF` are the matched SNR
and noise-floor mutators. `getLinkRate` / `setLinkRate` expose the
cached infra link rate; `modifyChID` is the live channel-id mutator.

## CR-180 Header Alignment

`include/Airport/IO80211InterfaceMonitor.h` (new file):

```c++
class IO80211Controller;

class IO80211InterfaceMonitor {
public:
    IO80211Controller *getController(void);

    unsigned long long getInputBytes(void);
    unsigned long long getInputPackets(void);
    unsigned long long getOutputBEBytes(void);
    unsigned long long getOutputBKBytes(void);
    unsigned long long getOutputVIBytes(void);
    unsigned long long getOutputVOBytes(void);
    unsigned long long getOutputBEPackets(void);
    unsigned long long getOutputBKPackets(void);
    unsigned long long getOutputVIPackets(void);
    unsigned long long getOutputVOPackets(void);

    long long getInterfaceRSSI(void);
    void setInterfaceRSSI(long long);
    bool hasInterfaceRSSI(void);
    void setInterfaceSNR(long long);
    void setInterfaceNF(long long);

    unsigned long long getLinkRate(void);
    void setLinkRate(unsigned long long);
    void modifyChID(unsigned long long);
};
```

The class is declared opaque (no base class, no data layout, no vtable).
The local kext only ever holds an `IO80211InterfaceMonitor *` returned
by the kernel; it does not allocate, subclass, or take `sizeof` of the
class.

Generated kext is bit-identical to CR-179 (sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`,
UUID `BA3D771F-F079-33FF-94E5-C792E66237D8`).

## Deferred Work (out of scope for CR-180)

- Recover the definitions of `apple80211_leaky_ap_reason`,
  `io80211BinCounters`, and other types referenced by additional
  exported `IO80211InterfaceMonitor::*` helpers.
- Decompile the bodies of `setInterfaceRSSI` / `setLinkRate` to confirm
  whether callers are expected to hold a particular gate.
- Resolve the open caller-side gates for the peer/link-state path
  documented in CR-175 / CR-176 / CR-177 / CR-178 / CR-179 closures.
- Recover `IO80211InfraInterface::handleKeyDone(bool, bool)` body via
  raw `otool -tV` / `objdump -D` at `0xffffff80022e6f9c`.

The wired CR will be CR-181+ once these gates are decompile-confirmed.

## Non-claims

CR-180 does not:

- synthesize `AppleBCMWLANPCIeSkywalkPacket`;
- write `packet+0x78`;
- change packet allocation, queueing, input, or output behavior;
- force EAPOL TX, key install, RSN, DHCP, link, or data success;
- add retry, replay, delay, poll loop, packet synthesis, or deauth
  masking;
- call any of the nineteen interface-monitor methods from any local
  code path;
- declare `IO80211InterfaceMonitor` with any base class or data
  layout;
- include kernel-internal types whose definitions are not yet
  recovered.
