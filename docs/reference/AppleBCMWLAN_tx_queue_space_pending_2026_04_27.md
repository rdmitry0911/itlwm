# AppleBCMWLAN TX Queue Space And Pending Contract

Date: 2026-04-27

Scope: active primary-STA Skywalk TX admission visibility after RX/TX
producer restoration.

## Reference Evidence

`AppleBCMWLANSkywalkInterface::getTxSubQueue(apple80211_wme_ac)` at
`0xffffff800155fb5a` loads the owner ivars, maps WME AC through the queue-index
table, checks the queue count, and returns a TX queue pointer from the queue
vector.

`AppleBCMWLANIO80211APSTAInterface::getTxSubQueue(apple80211_wme_ac)` at
`0xffffff80016940b4` performs the same owner-vector lookup for the APSTA
interface layout.

`AppleBCMWLANSkywalkInterface::getTxQueueDepth()` at `0xffffff800155fbfe`
reads the first TX queue object and returns its capacity field. The APSTA
variant at `0xffffff8001694126` does the same through its owner layout.

IO80211Family `IO80211SkywalkInterface::pendingPackets(unsigned char)` at
`0xffffff80022780ac` maps the interface flow queue database and calls the
matched queue pending-count virtual. `packetSpace(unsigned char)` at
`0xffffff8002278134` maps the same database and calls the matched queue
packet-space virtual.

## Local Divergence

The local Tahoe interface already exposes a live single TX queue through
`getTxSubQueue(...)`, but `pendingPackets(...)` and `packetSpace(...)` returned
`0` unconditionally. That advertises no TX admission capacity to the system
even when `fTxQueue` exists.

## Local Alignment In This Batch

The local single-queue mapping is now consistent:

- local TX queue lookup returns `fTxQueue` for local TX queue queries;
- `pendingPackets(...)` returns `fTxQueue->getPacketCount()`;
- `packetSpace(...)` returns `fTxQueue->getFreeSpace()`.

The returned values are owned by the live IOSkywalk queue object. This does not
fabricate capacity, force TX success, force EAPOL/key/RSN/DHCP/link state, add
retries, replay packets, or synthesize packets.
