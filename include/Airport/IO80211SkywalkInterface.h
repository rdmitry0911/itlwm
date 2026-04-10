//
//  IO80211SkywalkInterface.h
//  IO80211Family
//
//  Created by 钟先耀 on 2019/10/18.
//  Copyright © 2019 钟先耀. All rights reserved.
//

#ifndef _IO80211SKYWALK_H
#define _IO80211SKYWALK_H

#include <Availability.h>
#include "IOSkywalkEthernetInterface.h"

// This is necessary, because even the latest Xcode does not support properly targeting 11.0.
#ifndef __IO80211_TARGET
#error "Please define __IO80211_TARGET to the requested version"
#endif

class TxSubmissionDequeueStats;
class TxCompletionEnqueueStats;
class IO80211NetworkPacket;
class PacketSkywalkScratch;
typedef UInt64 IO80211FlowQueueHash;
class IO80211Peer;
class CCPipe;
class IO80211APIUserClient;
class IO80211PeerManager;
struct apple80211_wme_ac;
struct apple80211_interface_availability;
struct apple80211_cca_report;
struct apple80211_stat_report;
struct apple80211_chip_counters_tx;
struct apple80211_chip_counters_rx;
struct apple80211_chip_error_counters_tx;
struct apple80211_ManagementInformationBasedot11_counters;
struct apple80211_lteCoex_report;
struct apple80211_frame_counters;
struct userPrintCtx;
struct apple80211_lqm_summary;
struct apple80211_infra_specific_stats;

struct TxPacketRequest {
    uint16_t    unk1;       // 0
    uint16_t    t;       // 2
    uint16_t    mU;       // 4
    uint16_t    mM;       // 6
    uint16_t    pkt_cnt;
    uint16_t    unk2;
    uint16_t    unk3;
    uint16_t    unk4;
    uint32_t    pad;
    mbuf_t      bufs[8];    // 18
    uint32_t    reqTx;
};

static_assert(sizeof(struct TxPacketRequest) == 0x60, "TxPacketRequest size error");

class IO80211SkywalkInterface : public IOSkywalkEthernetInterface {
    OSDeclareAbstractStructors(IO80211SkywalkInterface)

public:

    virtual bool init() APPLE_KEXT_OVERRIDE;
    virtual void free() APPLE_KEXT_OVERRIDE;
    virtual IOReturn configureReport(IOReportChannelList *,UInt,void *,void *) APPLE_KEXT_OVERRIDE;
    virtual IOReturn updateReport(IOReportChannelList *,UInt,void *,void *) APPLE_KEXT_OVERRIDE;
    virtual bool start(IOService *) APPLE_KEXT_OVERRIDE;
    virtual void stop(IOService *) APPLE_KEXT_OVERRIDE;
    virtual IOReturn setPowerState(
        unsigned long powerStateOrdinal,
        IOService *   whatDevice ) APPLE_KEXT_OVERRIDE;
    virtual unsigned long maxCapabilityForDomainState( IOPMPowerFlags domainState ) APPLE_KEXT_OVERRIDE;
    virtual unsigned long initialPowerStateForDomainState( IOPMPowerFlags domainState ) APPLE_KEXT_OVERRIDE;
    virtual IOReturn enable(UInt) APPLE_KEXT_OVERRIDE;
    virtual IOReturn disable(UInt) APPLE_KEXT_OVERRIDE;
    virtual SInt32 initBSDInterfaceParameters(ifnet_init_eparams *,sockaddr_dl **) APPLE_KEXT_OVERRIDE;
    virtual bool prepareBSDInterface(ifnet_t, UInt) APPLE_KEXT_OVERRIDE;
    virtual IOReturn processBSDCommand(ifnet_t, UInt, void *) APPLE_KEXT_OVERRIDE;
    virtual SInt32 setInterfaceEnable(bool) APPLE_KEXT_OVERRIDE;
    virtual SInt32 setRunningState(bool) APPLE_KEXT_OVERRIDE;
    virtual IOReturn handleChosenMedia(UInt) APPLE_KEXT_OVERRIDE;
    virtual void *getSupportedMediaArray(UInt *,UInt *) APPLE_KEXT_OVERRIDE;
    virtual UInt32 getFeatureFlags(void) APPLE_KEXT_OVERRIDE;
    virtual const char *classNameOverride(void) APPLE_KEXT_OVERRIDE;
    virtual IOReturn setPromiscuousModeEnable(bool, UInt) APPLE_KEXT_OVERRIDE;

    // --- IO80211SkywalkInterface-specific virtuals ---
#if __IO80211_TARGET >= __MAC_26_0

    // Tahoe vtable: verified order against IO80211SkywalkInterface __ZTV
    // in BootKC 26.3 (25D125).  Absolute slot numbers below are from the
    // InfraProtocol secondary vtable base and may be ~2 off; the METHOD
    // ORDER is what the compiler uses and is confirmed correct.
    virtual void *createPeerManager(void);                                          // [355]
    virtual void *createPeer(unsigned char const*, IO80211PeerManager *);            // [356] NEW
    virtual void postMessage(UInt,void *,unsigned long,bool);                        // [357]
    virtual IOReturn reportDataPathEvents(UInt,void *,unsigned long,bool);           // [358]
    virtual IOReturn recordOutputPackets(TxSubmissionDequeueStats *,TxSubmissionDequeueStats *); // [359]
    virtual IOReturn recordOutputPacket(apple80211_wme_ac,int,int);                  // [360]
    virtual void logTxPacket(IO80211NetworkPacket *,PacketSkywalkScratch *,apple80211_wme_ac,bool); // [361]
    virtual void logTxCompletionPacket(IO80211NetworkPacket *,PacketSkywalkScratch *,unsigned char *,apple80211_wme_ac,int,UInt,bool,bool); // [362] extra bool
    virtual IOReturn recordCompletionPackets(TxCompletionEnqueueStats *,TxCompletionEnqueueStats *); // [363]
    virtual IOReturn inputPacket(IO80211NetworkPacket *,packet_info_tag *,ether_header *,bool *,bool); // [364] extra bool
    virtual IOReturn forwardInfraRelayPackets(IO80211NetworkPacket*, ether_header*);  // [365]
    virtual void logSkywalkTxReqPacket(IO80211NetworkPacket *,PacketSkywalkScratch *,unsigned char *,apple80211_wme_ac,bool); // [366]
    virtual SInt64 pendingPackets(unsigned char);                                     // [367]
    virtual SInt64 packetSpace(unsigned char);                                        // [368]
    virtual bool isChipInterfaceReady(void);                                          // [369]
    virtual bool isDebounceOnGoing(void);                                             // [370]
    // NOTE: setLinkState moved to IO80211InfraInterface [463]
    virtual IO80211LinkState linkState(void);                                         // [371]
    virtual void setScanningState(UInt,bool,apple80211_scan_data *,int);              // [372]
    virtual void setDataPathState(bool);                                              // [373]
    virtual void *getScanManager(void);                                               // [374]
    virtual void *getController(void);                                                // [375] NEW
    virtual void updateLinkParameters(apple80211_interface_availability *);            // [376]
    virtual void updateInterfaceCoexRiskPct(unsigned long long);                       // [377]
    virtual void setLQM(unsigned long long);                                           // [378]
    virtual void updateLinkStatus(void);                                               // [379]
    virtual void updateLinkStatusGated(void);                                          // [380]
    virtual void setInterfaceExtendedCCA(apple80211_channel,apple80211_cca_report *);  // [381]
    virtual void setInterfaceCCA(apple80211_channel,int);                              // [382]
    virtual void setInterfaceNF(apple80211_channel,long long);                         // [383]
    virtual void setInterfaceOFDMDesense(apple80211_channel,long long);                // [384]
    virtual void removePacketQueue(IO80211FlowQueueHash *);                            // [385]
    virtual void setDebugFlags(unsigned long long,UInt);                                // [386]
    virtual SInt64 debugFlags(void);                                                    // [387]
    virtual void setInterfaceChipCounters(apple80211_stat_report *,apple80211_chip_counters_tx *,apple80211_chip_error_counters_tx *,apple80211_chip_counters_rx *); // [388]
    virtual void setInterfaceMIBdot11(apple80211_stat_report *,apple80211_ManagementInformationBasedot11_counters *); // [389]
    virtual void setFrameStats(apple80211_stat_report *,apple80211_frame_counters *);  // [390]
    virtual void setInfraSpecificFrameStats(apple80211_stat_report *,apple80211_infra_specific_stats *); // [391]
    virtual void setRxDataStallStats(apple80211_stat_report *,void *);                 // [392] NEW
    virtual SInt64 getWmeTxCounters(unsigned long long *);                             // [393]
    // setEnabledBySystem, enabledBySystem, willRoam REMOVED
    virtual void setPeerManagerLogFlag(UInt,UInt,UInt);                                 // [394]
    virtual void setWoWEnabled(bool);                                                   // [395]
    virtual bool wowEnabled(void);                                                      // [396]
    virtual void printDataPath(userPrintCtx *);                                         // [397]
    virtual UInt64 getDataQueueDepth(void);                                             // [398] NEW (no param)
    virtual bool findOrCreateFlowQueue(IO80211FlowQueueHash);                           // [399]
    virtual UInt64 findOrCreateFlowQueueWithCache(IO80211FlowQueueHash,bool *);         // [400]
    virtual UInt64 findExistingFlowQueue(IO80211FlowQueueHash);                         // [401]
    virtual void removePacketQueue(IO80211FlowQueueHash const*);                        // [402]
    virtual void flushPacketQueues(void);                                                // [403]
    virtual void cachePeer(ether_addr *,UInt *);                                         // [404]
    virtual bool shouldLog(unsigned long long);                                           // [405]
    virtual void vlogDebug(unsigned long long,char const*,va_list);                       // [406]
    virtual void vlogDebugBPF(unsigned long long,char const*,va_list);                    // [407]
    virtual UInt64 createLinkQualityMonitor(IO80211Peer *,bool);                          // [408] changed: bool instead of IOService*
    virtual void releaseLinkQualityMonitor(IO80211Peer *);                                // [409]
    virtual void *getP2PSkywalkPeerMgr(void);                                             // [410]
    virtual bool isCommandProhibited(int);                                                 // [411]
    virtual void *findPeer(ether_addr &);                                                  // [412] NEW
    virtual void setNotificationProperty(OSSymbol const*,OSObject const*);                 // [413]
    virtual void *getWorkerMatchingDict(OSString *);                                       // [414]
    virtual bool init(IOService *, ether_addr *);                                          // [415] real secondary slot 413 (0xCE8)
    virtual bool isInterfaceEnabled(void);                                                  // [416] real secondary slot 414 (0xCF0)
    virtual ether_addr *getSelfMacAddr(void);                                               // [417]
    // setSelfMacAddr REMOVED
    virtual void *getPacketPool(OSString *);                                                // [418]
    virtual void *getLogger(void) const;                                                    // [419] NOW CONST
    virtual IOReturn handleSIOCSIFADDR(void);                                               // [420]
    virtual IOReturn debugHandler(apple80211_debug_command *);                               // [421]
    virtual void statsDump(void);                                                            // [422]
    virtual void powerOnNotification(void);                                                  // [423]
    virtual void powerOffNotification(void);                                                 // [424]
    virtual UInt64 getTxQueueDepth(void);                                                    // [425]
    virtual UInt64 getRxQueueCapacity(void);                                                 // [426]
    virtual void updateRxCounter(unsigned long long);                                        // [427]
    virtual void *getMultiCastQueue(void);                                                   // [428]
    // getCurrentBssid REMOVED
    virtual int getAssocState(void);                                                         // [429]
    virtual void notifyQueueState(apple80211_wme_ac,unsigned short);                         // [430]
    virtual int getTxHeadroom(void);                                                          // [431]
    virtual void *getRxCompQueue(void);                                                       // [432]
    virtual void *getTxCompQueue(void);                                                       // [433]
    virtual void *getTxSubQueue(apple80211_wme_ac);                                           // [434]
    virtual void *getTxPacketPool(void);                                                      // [435]
    virtual void *getRxPacketPool(void);                                                      // [436]
    virtual void enableDatapath(void);                                                        // [437]
    virtual void disableDatapath(void);                                                       // [438]
    virtual int getNumTxQueues(void);                                                         // [439]
    virtual void *getLQMSummary(apple80211_lqm_summary *);                                    // [440]
    virtual int getEventPipeSize(void);                                                       // [441]
    virtual UInt64 createEventPipe(IO80211APIUserClient *);                                   // [442]
    virtual void destroyEventPipe(IO80211APIUserClient *);                                    // [443]
    virtual void setUserBufferInfo(IOMemoryDescriptor *,unsigned long long);                   // [444] NEW
    virtual void postMessageIOUC(char const*,UInt,void *,unsigned long);                       // [445]
    virtual bool isIOUCPipeOpened(void);                                                       // [446]
    virtual void *getRingMD(IO80211APIUserClient *,unsigned long long);                        // [447]
    virtual void *attachPeer(ether_addr *);                                                    // [448] NEW
    virtual void *detachPeer(ether_addr *);                                                    // [449] NEW
    virtual void setDebugTrafficReport(bool);                                                  // [450] NEW
    virtual void *getDataPathInterfaceStats(void *);                                           // [451] NEW
    virtual void *getDataPathPeerStats(void *);                                                // [452] NEW
    virtual void *getLastQueuePacketTime(ether_addr *);                                        // [453] NEW
    virtual void *getLastRxUnicastLinkActivityTime(ether_addr *);                              // [454] NEW
    virtual void *updateInterfaceDataStats(void *);                                            // [455] NEW
    virtual void *updatePeerDataStats(void *);                                                 // [456] NEW
    virtual void logTxLatency(unsigned char *,UInt,unsigned long long);                         // [457] NEW
    virtual void logRxLatency(UInt,unsigned long long);                                         // [458] NEW
    virtual void *getNClearTxRxLatency(void *,void *);                                         // [459] NEW
    virtual void getLastTxTimeStamp(unsigned long long &);                                      // [460] NEW
    virtual void getLastRxTimeStamp(unsigned long long &);                                      // [461] NEW
    virtual void syncDPSStats(void *);                                                          // [462] NEW

#else /* Sonoma 14.4 and earlier */

    virtual void *createPeerManager(void);
    virtual void postMessage(UInt,void *,unsigned long,bool);
    virtual IOReturn reportDataPathEvents(UInt,void *,unsigned long,bool);
    virtual IOReturn recordOutputPackets(TxSubmissionDequeueStats *,TxSubmissionDequeueStats *);
    virtual IOReturn recordOutputPacket(apple80211_wme_ac,int,int);
    virtual void logTxPacket(IO80211NetworkPacket *,PacketSkywalkScratch *,apple80211_wme_ac,bool);
    virtual void logTxCompletionPacket(IO80211NetworkPacket *,PacketSkywalkScratch *,unsigned char *,apple80211_wme_ac,int,UInt,bool);
    virtual IOReturn recordCompletionPackets(TxCompletionEnqueueStats *,TxCompletionEnqueueStats *);
    virtual IOReturn inputPacket(IO80211NetworkPacket *,packet_info_tag *,ether_header *,bool *);
    virtual IOReturn forwardInfraRelayPackets(IO80211NetworkPacket*, ether_header*);
    virtual void logSkywalkTxReqPacket(IO80211NetworkPacket *,PacketSkywalkScratch *,unsigned char *,apple80211_wme_ac,bool);
    virtual SInt64 pendingPackets(unsigned char);
    virtual SInt64 packetSpace(unsigned char);
    virtual bool isChipInterfaceReady(void);
    virtual bool isDebounceOnGoing(void);
    virtual bool setLinkState(IO80211LinkState,UInt,bool debounceTimeout = 30,UInt code = 0);
    virtual IO80211LinkState linkState(void);
    virtual void setScanningState(UInt,bool,apple80211_scan_data *,int);
    virtual void setDataPathState(bool);
    virtual void *getScanManager(void);
    virtual void updateLinkParameters(apple80211_interface_availability *);
    virtual void updateInterfaceCoexRiskPct(unsigned long long);
    virtual void setLQM(unsigned long long);
    virtual void updateLinkStatus(void);
    virtual void updateLinkStatusGated(void);
    virtual void setInterfaceExtendedCCA(apple80211_channel,apple80211_cca_report *);
    virtual void setInterfaceCCA(apple80211_channel,int);
    virtual void setInterfaceNF(apple80211_channel,long long);
    virtual void setInterfaceOFDMDesense(apple80211_channel,long long);
    virtual void removePacketQueue(IO80211FlowQueueHash *);
    virtual void setDebugFlags(unsigned long long,UInt);
    virtual SInt64 debugFlags(void);
    virtual void setInterfaceChipCounters(apple80211_stat_report *,apple80211_chip_counters_tx *,apple80211_chip_error_counters_tx *,apple80211_chip_counters_rx *);
    virtual void setInterfaceMIBdot11(apple80211_stat_report *,apple80211_ManagementInformationBasedot11_counters *);
    virtual void setFrameStats(apple80211_stat_report *,apple80211_frame_counters *);
#if __IO80211_TARGET >= __MAC_14_4
    virtual void setInfraSpecificFrameStats(apple80211_stat_report *,apple80211_infra_specific_stats *);
#endif
    virtual SInt64 getWmeTxCounters(unsigned long long *);
    virtual void setEnabledBySystem(bool);
    virtual bool enabledBySystem(void);
    virtual bool willRoam(ether_addr *,UInt);
    virtual void setPeerManagerLogFlag(UInt,UInt,UInt);
    virtual void setWoWEnabled(bool);
    virtual bool wowEnabled(void);
    virtual void printDataPath(userPrintCtx *);
    virtual bool findOrCreateFlowQueue(IO80211FlowQueueHash);
    virtual UInt64 findOrCreateFlowQueueWithCache(IO80211FlowQueueHash,bool *);
    virtual UInt64 findExistingFlowQueue(IO80211FlowQueueHash);
    virtual void removePacketQueue(IO80211FlowQueueHash const*);
    virtual void flushPacketQueues(void);
    virtual void cachePeer(ether_addr *,UInt *);
    virtual bool shouldLog(unsigned long long);
    virtual void vlogDebug(unsigned long long,char const*,va_list);
    virtual void vlogDebugBPF(unsigned long long,char const*,va_list);
    virtual UInt64 createLinkQualityMonitor(IO80211Peer *,IOService *);
    virtual void releaseLinkQualityMonitor(IO80211Peer *);
    virtual void *getP2PSkywalkPeerMgr(void);
    virtual bool isCommandProhibited(int);
    virtual void setNotificationProperty(OSSymbol const*,OSObject const*);
    virtual void *getWorkerMatchingDict(OSString *);
    virtual bool init(IOService *);
    virtual bool isInterfaceEnabled(void);
    virtual ether_addr *getSelfMacAddr(void);
    virtual void setSelfMacAddr(ether_addr *);
    virtual void *getPacketPool(OSString *);
    virtual void *getLogger(void);
    virtual IOReturn handleSIOCSIFADDR(void);
    virtual IOReturn debugHandler(apple80211_debug_command *);
    virtual void statsDump(void);
    virtual void powerOnNotification(void);
    virtual void powerOffNotification(void);
    virtual UInt64 getTxQueueDepth(void);
    virtual UInt64 getRxQueueCapacity(void);
    virtual void updateRxCounter(unsigned long long);
    virtual void *getMultiCastQueue(void);
    virtual void *getCurrentBssid(void);
    virtual int getAssocState(void);
    virtual void notifyQueueState(apple80211_wme_ac,unsigned short);
    virtual int getTxHeadroom(void);
    virtual void *getRxCompQueue(void);
    virtual void *getTxCompQueue(void);
    virtual void *getTxSubQueue(apple80211_wme_ac);
    virtual void *getTxPacketPool(void);
    virtual void *getRxPacketPool(void);
    virtual void enableDatapath(void);
    virtual void disableDatapath(void);
    virtual int getNumTxQueues(void);
    virtual void *getLQMSummary(apple80211_lqm_summary *);
    virtual int getEventPipeSize(void);
    virtual UInt64 createEventPipe(IO80211APIUserClient *);
    virtual void destroyEventPipe(IO80211APIUserClient *);
    virtual void postMessageIOUC(char const*,UInt,void *,unsigned long);
    virtual bool isIOUCPipeOpened(void);
    virtual void *getRingMD(IO80211APIUserClient *,unsigned long long);

#endif /* __IO80211_TARGET >= __MAC_26_0 */

public:
    OSString *setInterfaceRole(UInt role);
    void *setInterfaceId(UInt id);
    int getInterfaceRole();

public:
    char _data[0x118];
};

#endif /* _IO80211SKYWALK_H */
