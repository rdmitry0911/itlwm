//
//  IO80211PeerMonitor.h
//  itlwm
//
//  Opaque direct-call surface used by Tahoe's real Skywalk TX producer.
//  IO80211Family owns the object and all of its storage; the Intel driver only
//  obtains a live pointer through IO80211SkywalkInterface::getPeerMonitor().
//
//  Tahoe 25C56 machine-code anchors (recovered 2026-08-07):
//    0xffffff80022d6062  incrementTxInput(unsigned int)
//    0xffffff80022d3c98  incrementTxStatusForDps(int, unsigned int,
//                                               unsigned long long,
//                                               long long)
//    0xffffff80022d64a2  txLatency(unsigned int, unsigned long long)
//
//  The first two routines return the boolean reporter-update result.  Raw
//  disassembly returns the result in AL/EAX.  txLatency is an accounting
//  producer: its input is a real completion latency in nanoseconds.
//

#ifndef IO80211PeerMonitor_h
#define IO80211PeerMonitor_h

class IO80211PeerMonitor {
public:
    bool incrementTxInput(unsigned int accessCategory);
    bool incrementTxStatusForDps(int status,
                                 unsigned int accessCategory,
                                 unsigned long long submitTime,
                                 long long packetCount);
    void txLatency(unsigned int accessCategory,
                   unsigned long long latencyNanoseconds);
};

#endif /* IO80211PeerMonitor_h */
