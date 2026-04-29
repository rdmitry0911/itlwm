//
//  IO80211PeerManager.h
//  itlwm
//
//  Reference-aligned forward declaration of the public direct-call
//  IO80211PeerManager surface exported by IO80211Family on macOS Tahoe.
//
//  All declared methods are non-virtual direct-call BootKC exports, so
//  the local class declaration deliberately omits any vtable or data
//  layout. Callers only ever hold an `IO80211PeerManager *` returned by
//  the kernel; the local kext does not allocate, subclass, or take
//  `sizeof` of this class.
//
//  Symbol addresses (BootKC, IO80211Family.kc, recovered 2026-04-28):
//    ffffff80021d3f58  IO80211PeerManager::addPeer(unsigned char*)
//    ffffff80021d7ba0  IO80211PeerManager::addPeerOperation()
//    ffffff80021d4452  IO80211PeerManager::removePeer(IO80211Peer*)
//    ffffff80021d4806  IO80211PeerManager::removePeer(unsigned char*)
//    ffffff80021d7c7e  IO80211PeerManager::removePeerOperation()
//    ffffff80021df2fe  IO80211PeerManager::getPeerList()
//    ffffff80021d298e  IO80211PeerManager::getPeerStats(
//                          apple80211_peer_stats*)
//
//  CR-178 additions (data-path + peer-lookup helpers, BootKC IO80211Family.kc,
//  recovered 2026-04-28):
//    ffffff80021d1388  IO80211PeerManager::findPeer(unsigned char*)
//    ffffff80021d3f0c  IO80211PeerManager::findCachedPeer(unsigned char*)
//    ffffff80021df2a8  IO80211PeerManager::getUnicastPeer()
//    ffffff80021df296  IO80211PeerManager::getMulticastPeer()
//    ffffff80021df672  IO80211PeerManager::getEnabled()
//    ffffff80021cc798  IO80211PeerManager::setEnableState(bool)
//    ffffff80021df4f8  IO80211PeerManager::getDataPathOpen()
//    ffffff80021df50a  IO80211PeerManager::setDataPathOpen(bool)
//    ffffff80021cde60  IO80211PeerManager::setDataPathState(bool)
//    ffffff80021cded6  IO80211PeerManager::lockDataPath()
//    ffffff80021cdfca  IO80211PeerManager::unlockDataPath()
//
//  CR-179 additions (infra BSSID/SSID/channel/state helpers, BootKC
//  IO80211Family.kc, recovered 2026-04-28):
//    ffffff80021df07a  IO80211PeerManager::getInfraBssid()
//    ffffff80021df2de  IO80211PeerManager::getInfraSsidLen()
//    ffffff80021df2ee  IO80211PeerManager::setInfraSsidLen(unsigned int)
//    ffffff80021df08a  IO80211PeerManager::getInfraSsidBytes()
//    ffffff80021df09a  IO80211PeerManager::setInfraSsidBytes(unsigned char*,
//                                                            unsigned int)
//    ffffff80021d4e36  IO80211PeerManager::setInfraTxState(bool)
//    ffffff80021d4eb0  IO80211PeerManager::setInfraChannel(apple80211_channel*)
//    ffffff80021d4e72  IO80211PeerManager::copyInfraChannel(apple80211_channel*)
//    ffffff80021d4e90  IO80211PeerManager::resetInfraChannel()
//    ffffff80021df04c  IO80211PeerManager::setInfraChannelInfo(
//                                              apple80211_channel*)
//    ffffff80021df06a  IO80211PeerManager::setInfraChannelFlags(unsigned int)
//    ffffff80021df994  IO80211PeerManager::getInfraRSSI()
//    ffffff80021df984  IO80211PeerManager::setInfraRSSI(int)
//

#ifndef IO80211PeerManager_h
#define IO80211PeerManager_h

#include <IOKit/IOReturn.h>

class IO80211Peer;
struct apple80211_peer_stats;
struct apple80211_channel;
struct ether_addr;

class IO80211PeerManager {
public:
    // CR-205: addPeer return type corrected from IOReturn to IO80211Peer*.
    // Raw disasm at 0xffffff80021d3f58 shows the result is computed in
    // callee-saved R14 (loaded from `*(void **)([RAX+0x18]+0x530)` on the
    // hot path; cleared via `XOR R14D, R14D` on the error/null path) and
    // moved to RAX with a full 64-bit `MOV RAX, R14` immediately before
    // `ADD RSP,0x48; POP {RBX,R12,R13,R14,R15,RBP}; RET`. There is no
    // MOVZX EAX, AL/AX, no 32-bit MOV EAX, ..., and no truncation to a
    // 32-bit width — that is the macOS x86_64 SysV ABI for a pointer
    // return, not for IOReturn (a 32-bit int). See
    //   analysis/cr205_addpeer_disasm.txt
    // for the verbatim instruction listing.
    IO80211Peer *addPeer(unsigned char *addr);
    IOReturn addPeerOperation(void);
    void removePeer(IO80211Peer *peer);
    IOReturn removePeer(unsigned char *addr);
    IOReturn removePeerOperation(void);
    void *getPeerList(void);
    IOReturn getPeerStats(apple80211_peer_stats *stats);

    IO80211Peer *findPeer(unsigned char *addr);
    IO80211Peer *findCachedPeer(unsigned char *addr);
    IO80211Peer *getUnicastPeer(void);
    IO80211Peer *getMulticastPeer(void);

    bool getEnabled(void);
    void setEnableState(bool);
    bool getDataPathOpen(void);
    void setDataPathOpen(bool);
    void setDataPathState(bool);
    void lockDataPath(void);
    void unlockDataPath(void);

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

    // CR-185 additions: provider / controller / capability / scanning /
    // counter / reporter parameterless accessors. Recovered from
    // IO80211Family BootKernelExtensions.kc on 2026-04-28:
    //   ffffff80021c90aa  IO80211PeerManager::getBSDName()
    //   ffffff80021df576  IO80211PeerManager::GetProvider()
    //   ffffff80021c6bd2  IO80211PeerManager::getController()
    //   ffffff80021c8648  IO80211PeerManager::getInterfaceId()
    //   ffffff80021df390  IO80211PeerManager::getCommandGate()
    //   ffffff80021cea00  IO80211PeerManager::interfaceMonitor()
    //   ffffff80021df8bc  IO80211PeerManager::getCountryCode()
    //   ffffff80021df5ee  IO80211PeerManager::getDTIMPeriod()
    //   ffffff80021df5de  IO80211PeerManager::getBeaconPeriod()
    //   ffffff80021ccbba  IO80211PeerManager::getEnabling()
    //   ffffff80021c9d2c  IO80211PeerManager::failToEnable()
    //   ffffff80021df650  IO80211PeerManager::getHeCapable()
    //   ffffff80021df63e  IO80211PeerManager::getVhtCapable()
    //   ffffff80021df89c  IO80211PeerManager::getMyHeCap()
    //   ffffff80021df88c  IO80211PeerManager::getMyVhtCap()
    //   ffffff80021df8ac  IO80211PeerManager::getRsdbCap()
    //   ffffff80021df87c  IO80211PeerManager::getHtCapabilities()
    //   ffffff80021df0ee  IO80211PeerManager::isRsdbSupported()
    //   ffffff80021dfb38  IO80211PeerManager::onDispatchQueue()
    //   ffffff80021d4c0c  IO80211PeerManager::isPeerCacheFull()
    //   ffffff80021d4f0c  IO80211PeerManager::printHashTable()
    //   ffffff80021d46a0  IO80211PeerManager::removeAllPeers()
    //   ffffff80021c93a4  IO80211PeerManager::freeResources()
    //   ffffff80021ccf5c  IO80211PeerManager::awdlChipReset()
    //   ffffff80021cc734  IO80211PeerManager::flushFreeMbufs()
    //   ffffff80021dba82  IO80211PeerManager::enablemDNSTx()
    //   ffffff80021c9a56  IO80211PeerManager::destroyReporters()
    //   ffffff80021d9338  IO80211PeerManager::updateAllReports()
    //   ffffff80021df8cc  IO80211PeerManager::getScanningState()
    //   ffffff80021dfc24  IO80211PeerManager::getOutputBEBytes()
    //   ffffff80021dfc36  IO80211PeerManager::getOutputBKBytes()
    //   ffffff80021dfc48  IO80211PeerManager::getOutputVIBytes()
    //   ffffff80021dfc5a  IO80211PeerManager::getOutputVOBytes()
    char const *getBSDName(void);
    void *GetProvider(void);
    void *getController(void);
    unsigned int getInterfaceId(void);
    void *getCommandGate(void);
    void *interfaceMonitor(void);
    unsigned int getCountryCode(void);
    unsigned int getDTIMPeriod(void);
    unsigned int getBeaconPeriod(void);
    bool getEnabling(void);
    bool failToEnable(void);
    bool getHeCapable(void);
    bool getVhtCapable(void);
    unsigned int getMyHeCap(void);
    unsigned int getMyVhtCap(void);
    unsigned int getRsdbCap(void);
    unsigned int getHtCapabilities(void);
    bool isRsdbSupported(void);
    bool onDispatchQueue(void);
    bool isPeerCacheFull(void);
    void printHashTable(void);
    void removeAllPeers(void);
    void freeResources(void);
    void awdlChipReset(void);
    void flushFreeMbufs(void);
    void enablemDNSTx(void);
    void destroyReporters(void);
    void updateAllReports(void);
    unsigned int getScanningState(void);
    unsigned long long getOutputBEBytes(void);
    unsigned long long getOutputBKBytes(void);
    unsigned long long getOutputVIBytes(void);
    unsigned long long getOutputVOBytes(void);

    // CR-191 additions: peer-manager primitive-only direct-call helpers
    // recovered from IO80211Family BootKernelExtensions.kc on 2026-04-28.
    // None of these names match a parent-class virtual (verified) and none
    // are already declared as virtuals or non-virtuals earlier in this
    // class body.
    //   ffffff80021ce526  IO80211PeerManager::modifyChID(unsigned long long)
    //   ffffff80021d9728  IO80211PeerManager::printPeers(unsigned int,
    //                                                    unsigned int)
    //   ffffff80021df230  IO80211PeerManager::getBlockMdns()
    //   ffffff80021df242  IO80211PeerManager::setBlockMdns(bool)
    //   ffffff80021df20c  IO80211PeerManager::getBlockMdnsTx()
    //   ffffff80021df21e  IO80211PeerManager::setBlockMdnsTx(bool)
    //   ffffff80021df2ba  IO80211PeerManager::setP2PLogging(bool)
    //   ffffff80021d42a6  IO80211PeerManager::setPeersCount(unsigned long
    //                                                       long)
    //   ffffff80021df5fe  IO80211PeerManager::setBeaconPeriod(unsigned int)
    //   ffffff80021df60e  IO80211PeerManager::setDTIMPeriod(unsigned int)
    //   ffffff80021df0e8  IO80211PeerManager::setDisplayState(bool)
    //   ffffff80021d4e18  IO80211PeerManager::setScanOn2GOnly(bool)
    //   ffffff80021d4c22  IO80211PeerManager::is24GOnlyScan()
    //   ffffff80021df7e0  IO80211PeerManager::getTxQueueStamp()
    //   ffffff80021df7f2  IO80211PeerManager::setTxQueueStamp(unsigned
    //                                                         long long)
    //   ffffff80021e0078  IO80211PeerManager::updateCtlCount(unsigned long
    //                                                        long)
    //   ffffff80021e008a  IO80211PeerManager::updateRxPackets(IO80211Peer*,
    //                                                         unsigned long
    //                                                         long,
    //                                                         unsigned long
    //                                                         long)
    //   ffffff80021c6482  IO80211PeerManager::macAddressEqual(IO80211Peer*,
    //                                                         unsigned
    //                                                         char*)
    //   ffffff80021db786  IO80211PeerManager::saveCountryCode(unsigned
    //                                                         char*)
    //   ffffff80021deb10  IO80211PeerManager::reportP2PCCA(unsigned char,
    //                                                      unsigned int,
    //                                                      unsigned int,
    //                                                      unsigned int,
    //                                                      unsigned int)
    void modifyChID(unsigned long long);
    void printPeers(unsigned int, unsigned int);
    bool getBlockMdns(void);
    void setBlockMdns(bool);
    bool getBlockMdnsTx(void);
    void setBlockMdnsTx(bool);
    void setP2PLogging(bool);
    void setPeersCount(unsigned long long);
    void setBeaconPeriod(unsigned int);
    void setDTIMPeriod(unsigned int);
    void setDisplayState(bool);
    void setScanOn2GOnly(bool);
    bool is24GOnlyScan(void);
    unsigned long long getTxQueueStamp(void);
    void setTxQueueStamp(unsigned long long);
    void updateCtlCount(unsigned long long);
    void updateRxPackets(IO80211Peer *, unsigned long long, unsigned long long);
    bool macAddressEqual(IO80211Peer *, unsigned char *);
    void saveCountryCode(unsigned char *);
    void reportP2PCCA(unsigned char, unsigned int, unsigned int, unsigned int, unsigned int);
};

#endif /* IO80211PeerManager_h */
