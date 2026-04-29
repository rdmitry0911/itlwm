# IO80211Peer RSSI / Packet-Stats / Cache / Queue Helpers

Date: 2026-04-28

Scope: non-virtual exported helpers on `IO80211Peer` selected by
CR-184. These thirty-three helpers extend the thirty-one already
declared (CR-177 plus CR-183) with the per-band RSSI accounting,
real-time and cumulative packet-stats accessors, cache-state queries,
and queue/lifetime helpers.

## Reference Evidence

`/srv/project/ghidra_additional/kc_target_symbols.txt` (BootKC,
IO80211Family, macOS Tahoe 26.x):

```
ffffff80021bfcb8  IO80211Peer::getStatsID()
ffffff80021bfca6  IO80211Peer::getStatsIDValid()
ffffff80021c2fb0  IO80211Peer::reportRssi(int, apple80211_channel)
ffffff80021c30b6  IO80211Peer::reportChainRssi(signed char const*, int)
ffffff80021c5e2a  IO80211Peer::getAvgRssi24G()
ffffff80021c5e3a  IO80211Peer::getAvgRssi5G()
ffffff80021c323e  IO80211Peer::getAvgRssiAcrossBands()
ffffff80021c36d6  IO80211Peer::getAvgChainRssi5G()
ffffff80021c5e6e  IO80211Peer::setPeerAvgRssi24G(signed char)
ffffff80021c5e7e  IO80211Peer::setPeerAvgRssi5G(signed char)
ffffff80021c2f94  IO80211Peer::simulateDPS()
ffffff80021bfbb2  IO80211Peer::freeResources()
ffffff80021c2ce8  IO80211Peer::unpauseQueues()
ffffff80021bfa80  IO80211Peer::reclaimPackets()
ffffff80021c29e6  IO80211Peer::clearCacheState()
ffffff80021c3432  IO80211Peer::getRxBitField(unsigned int)
ffffff80021c344c  IO80211Peer::getRxBitFieldMulticast(unsigned int)
ffffff80021c511e  IO80211Peer::incrementRxCount(unsigned int)
ffffff80021c32e0  IO80211Peer::getPacketStats()
ffffff80021c3348  IO80211Peer::getPacketStatsRealTimeRx()
ffffff80021c331a  IO80211Peer::getPacketStatsRealTimeTx()
ffffff80021c348a  IO80211Peer::getCumDataStats()
ffffff80021c60ae  IO80211Peer::hasRealTimeData()
ffffff80021c609c  IO80211Peer::hasLowLatencyData()
ffffff80021bfdcc  IO80211Peer::hasQueuedPackets()
ffffff80021c57e4  IO80211Peer::getDataLinkCount()
ffffff80021c5bbc  IO80211Peer::logPeerTxLatency(unsigned long long)
ffffff80021c3266  IO80211Peer::updateQueueState(int)
ffffff80021c37b0  IO80211Peer::getCacheTimeStamp()
ffffff80021c37be  IO80211Peer::setCacheTimeStamp(unsigned long long)
ffffff80021c373c  IO80211Peer::setPacketLifetime(unsigned int)
ffffff80021c5e8e  IO80211Peer::isBssSteeringPeer()
ffffff80021c5eaa  IO80211Peer::isBssSteeringPeerSyncState()
```

This is the fourth batch of public Peer exports the local kext aligns
with the BootKC. Together with the thirty-one direct-call helpers
already declared, this brings the locally declared Peer direct-call
surface to sixty-four exported methods — the slice most relevant to
the per-Peer RSSI accounting, real-time/cumulative packet-stats query
path, cache-state management, and queue/lifetime control.

## CR-184 Header Alignment

`include/Airport/IO80211Peer.h` (extended):

```c++
void *getStatsID(void);
bool getStatsIDValid(void);
void reportRssi(int, apple80211_channel);
void reportChainRssi(signed char const *, int);
signed char getAvgRssi24G(void);
signed char getAvgRssi5G(void);
signed char getAvgRssiAcrossBands(void);
signed char getAvgChainRssi5G(void);
void setPeerAvgRssi24G(signed char);
void setPeerAvgRssi5G(signed char);
void simulateDPS(void);
void freeResources(void);
void unpauseQueues(void);
unsigned int reclaimPackets(void);
void clearCacheState(void);
unsigned long long getRxBitField(unsigned int);
unsigned long long getRxBitFieldMulticast(unsigned int);
void incrementRxCount(unsigned int);
void *getPacketStats(void);
void *getPacketStatsRealTimeRx(void);
void *getPacketStatsRealTimeTx(void);
void *getCumDataStats(void);
bool hasRealTimeData(void);
bool hasLowLatencyData(void);
bool hasQueuedPackets(void);
unsigned int getDataLinkCount(void);
void logPeerTxLatency(unsigned long long);
void updateQueueState(int);
unsigned long long getCacheTimeStamp(void);
void setCacheTimeStamp(unsigned long long);
void setPacketLifetime(unsigned int);
bool isBssSteeringPeer(void);
bool isBssSteeringPeerSyncState(void);
```

Forward declaration added at file scope: `struct apple80211_channel;`.
`reportRssi(int, apple80211_channel)` takes the channel struct by
value in the BootKC mangled name; this header forward-declares the
struct so the declaration parses on its own. Any TU that actually
calls `reportRssi` will need to include `apple80211_var.h` to obtain
the full struct definition; CR-184 introduces no such caller, so the
build does not exercise that requirement.

Generated kext is bit-identical to CR-183 (sha256
`c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`,
UUID `BA3D771F-F079-33FF-94E5-C792E66237D8`).

## Deferred Work (out of scope for CR-184)

- Recover the `IO80211Peer` constructor / `init` body to identify
  which fields the RSSI/packet-stats accessors back onto.
- Confirm thread/gate context for the per-band RSSI setters and
  `clearCacheState`.
- Establish the ordering of `reportRssi` / `reportChainRssi`
  relative to the RX path that reads them.
- Resolve the open caller-side gates carried over from earlier CRs
  (peer/link-state preconditions, `handleKeyDone` body recovery).

The wired CR will be CR-185+ once these gates are decompile-confirmed.

## Non-claims

CR-184 does not:

- synthesize `AppleBCMWLANPCIeSkywalkPacket`;
- write `packet+0x78`;
- change packet allocation, queueing, input, or output behavior;
- force EAPOL TX, key install, RSN, DHCP, link, or data success;
- add retry, replay, delay, poll loop, packet synthesis, or deauth
  masking;
- call any of the thirty-three new Peer methods from any local code
  path;
- declare `IO80211Peer` with any base class or data layout beyond
  what was already in the local header;
- include kernel-internal types whose definitions are not yet
  recovered.
