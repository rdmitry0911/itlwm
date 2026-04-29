# AppleBCMWLAN TX Output Accounting - 2026-04-27

## Reference Evidence

`AppleBCMWLANPCIeSkywalkTxSubmissionQueue::dequeuePackets(...)` in
`AppleBCMWLANBusInterfacePCIeMac` starts at `0xffffff80014c611c`.

The recovered TX submission producer accumulates per-batch packet and byte
state while draining Skywalk TX packets. Near `0xffffff80014c7944` it loads the
interface pointer, queue AC, packet counters, and byte count, then calls the
interface virtual slot at `+0xb30`. The IO80211Family vtable map identifies
that slot as:

`IO80211SkywalkInterface::recordOutputPacket(apple80211_wme_ac,int,int)`
at `0xffffff8002277cc6`.

The IO80211 implementation is a pure monitor/accounting edge: it looks up the
interface monitor from interface private storage and delegates to
`IO80211InterfaceMonitor::recordOutputPacket(...)` when present.

## Local Divergence

Before CR-169, local `skywalkTxAction(...)` copied accepted Skywalk TX packets
into mbufs and called `outputPacket(...)`, but it only updated local
`sRT.txPktSent`. It did not call the IO80211 output-accounting method after
the TX batch.

## Restored Local Semantics

The local TX action now records packet and byte totals for frames accepted by
the existing `outputPacket(...)` path. After the batch it calls
`recordOutputPacket({ APPLE80211_WME_AC_BE }, delivered, deliveredBytes)` for
the local single-queue BE mapping.

This change does not call Apple scratch-dependent TX log methods, does not
synthesize `PacketSkywalkScratch`, does not replay packets, and does not force
TX, EAPOL, key, RSN, DHCP, link, or data success.
