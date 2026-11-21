//
//  AirportItlwmAWDLInterface.hpp
//  AirportItlwm (v-Airport)
//
//  awdl0 for Tahoe: a concrete IO80211AWDLProtocol. Ported in shape from
//  ForbiddenItlwm's ItlAWDLInterface (GPL-2.0). The "firmware" of AWDL runs
//  on the v-Airport host (vap_proto.h, "AWDL"; v-Airport docs/AWDL_FEASIBILITY.md):
//  this interface is thin. It turns the stack's setAWDL_* calls into the
//  firmware's configuration and templates, hands the peers' action frames to
//  Apple's peer manager and carries awdl0's data. Every request is also
//  recorded in the I/O Registry. Enabled only by the boot-arg -vapawdl.
//

#ifndef AirportItlwmAWDLInterface_hpp
#define AirportItlwmAWDLInterface_hpp

#include "Airport/Apple80211.h"

#if __IO80211_TARGET >= __MAC_26_0

#include "Airport/IO80211AWDLProtocol.h"
#include <IOKit/IOLocks.h>
#include <skywalk/packet/os_packet.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/skywalk/IOSkywalkPacket.h>
#include <IOKit/skywalk/IOSkywalkPacketBufferPool.h>
#include <IOKit/skywalk/IOSkywalkTxSubmissionQueue.h>
#include <IOKit/skywalk/IOSkywalkTxCompletionQueue.h>
#include <IOKit/skywalk/IOSkywalkRxCompletionQueue.h>

#include "../itlwm/hal_virt/vap_proto.h"

class AirportItlwmAWDLInterface : public IO80211AWDLProtocol {
    OSDeclareDefaultStructors(AirportItlwmAWDLInterface)

public:
    bool initWithController(IO80211Controller *controller, ether_addr *address);
    void free() override;

    // Base-class hooks, recorded and passed on.
    IOReturn processBSDCommand(ifnet_t, UInt, void *) override;
    IOReturn enable(UInt) override;
    void setMacAddress(ether_addr &) override;

    /*
     * Slots where Apple's IO80211VirtualInterface, IO80211SkywalkInterface or
     * IOSkywalkNetworkInterface override a method that this tree's base
     * headers do not declare (25C56 tables).  Bound by name to Apple's own
     * implementations, so awdl0 has exactly Apple's base behaviour there
     * (user clients, hardware address, multicast filter, peer cache).
     */
    bool willTerminate(IOService *, IOOptionBits) override
        __asm__("__ZN25IOSkywalkNetworkInterface13willTerminateEP9IOServicej");
    IOReturn newUserClient(task_t, void *, UInt32, OSDictionary *, IOUserClient **) override
        __asm__("__ZN23IO80211VirtualInterface13newUserClientEP4taskPvjP12OSDictionaryPP12IOUserClient");
    const char *stringFromReturn(IOReturn) override
        __asm__("__ZN23IO80211VirtualInterface16stringFromReturnEi");
    int errnoFromReturn(IOReturn) override
        __asm__("__ZN23IO80211SkywalkInterface15errnoFromReturnEi");
    bool bpfTap(UInt, UInt) override
        __asm__("__ZN23IO80211VirtualInterface6bpfTapEjj");
    void getHardwareAddress(ether_addr *) override
        __asm__("__ZN23IO80211SkywalkInterface18getHardwareAddressEP10ether_addr");
    void setHardwareAddress(ether_addr *) override
        __asm__("__ZN23IO80211SkywalkInterface18setHardwareAddressEP10ether_addr");
    bool setMulticastAddresses(ether_addr const *, UInt) override
        __asm__("__ZN23IO80211VirtualInterface21setMulticastAddressesEPK10ether_addrj");
    void logTxPacket(IO80211NetworkPacket *, PacketSkywalkScratch *, apple80211_wme_ac, bool) override
        __asm__("__ZN23IO80211VirtualInterface11logTxPacketEP20IO80211NetworkPacketP20PacketSkywalkScratch17apple80211_wme_acb");
    void logTxCompletionPacket(IO80211NetworkPacket *, PacketSkywalkScratch *, unsigned char *,
                               apple80211_wme_ac, int, UInt, bool, bool) override
        __asm__("__ZN23IO80211VirtualInterface21logTxCompletionPacketEP20IO80211NetworkPacketP20PacketSkywalkScratchPh17apple80211_wme_acijbb");
    void logSkywalkTxReqPacket(IO80211NetworkPacket *, PacketSkywalkScratch *, unsigned char *,
                               apple80211_wme_ac, bool) override
        __asm__("__ZN23IO80211VirtualInterface21logSkywalkTxReqPacketEP20IO80211NetworkPacketP20PacketSkywalkScratchPh17apple80211_wme_acb");
    void setInterfaceNF(apple80211_channel, long long) override
        __asm__("__ZN23IO80211VirtualInterface14setInterfaceNFE18apple80211_channelx");
    void setInterfaceChipCounters(apple80211_stat_report *, apple80211_chip_counters_tx *,
                                  apple80211_chip_error_counters_tx *, apple80211_chip_counters_rx *) override
        __asm__("__ZN23IO80211VirtualInterface24setInterfaceChipCountersEP22apple80211_stat_reportP27apple80211_chip_counters_txP33apple80211_chip_error_counters_txP27apple80211_chip_counters_rx");
    void setInterfaceMIBdot11(apple80211_stat_report *,
                              apple80211_ManagementInformationBasedot11_counters *) override
        __asm__("__ZN23IO80211VirtualInterface20setInterfaceMIBdot11EP22apple80211_stat_reportP50apple80211_ManagementInformationBasedot11_counters");
    void setFrameStats(apple80211_stat_report *, apple80211_frame_counters *) override
        __asm__("__ZN23IO80211VirtualInterface13setFrameStatsEP22apple80211_stat_reportP25apple80211_frame_counters");
    void setInfraSpecificFrameStats(apple80211_stat_report *, apple80211_infra_specific_stats *) override
        __asm__("__ZN23IO80211VirtualInterface26setInfraSpecificFrameStatsEP22apple80211_stat_reportP31apple80211_infra_specific_stats");
    void cachePeer(ether_addr *, UInt *) override
        __asm__("__ZN23IO80211VirtualInterface9cachePeerEP10ether_addrPj");
    bool shouldLog(unsigned long long) override
        __asm__("__ZN23IO80211VirtualInterface9shouldLogEy");
    void vlogDebug(unsigned long long, char const *, va_list) override
        __asm__("__ZN23IO80211VirtualInterface9vlogDebugEyPKcP13__va_list_tag");
    void vlogDebugBPF(unsigned long long, char const *, va_list) override
        __asm__("__ZN23IO80211VirtualInterface12vlogDebugBPFEyPKcP13__va_list_tag");

    // The transmit queue's action: frames macOS wants on the air.
    static UInt32 transmitAction(OSObject *, IOSkywalkTxSubmissionQueue *,
                                 IOSkywalkPacket *const *, UInt32, void *);

    // IO80211AWDLProtocol, slots 505..567.
    IOReturn getSSID(struct apple80211_ssid_data *) override;
    IOReturn getCHANNEL(struct apple80211_channel_data *) override;
    IOReturn getBSSID(struct apple80211_bssid_data *) override;
    IOReturn getSTATE(struct apple80211_state_data *) override;
    IOReturn getAWDL_SYNC_PARAMS(struct apple80211_awdl_sync_params *) override;
    IOReturn getAWDL_SYNC_ENABLED(struct apple80211_awdl_sync_enabled *) override;
    IOReturn getAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(
        struct apple80211_awdl_extension_state_machine_parameter *) override;
    IOReturn getAWDL_ELECTION_ID(struct apple80211_awdl_election_id *) override;
    IOReturn getAWDL_ELECTION_METRIC(struct apple80211_awdl_election_metric *) override;
    IOReturn getAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(
        struct apple80211_awdl_sync_channel_sequence *) override;
    IOReturn getPEER_CACHE_MAXIMUM_SIZE(struct apple80211_peer_cache_maximum_size *) override;
    IOReturn getAWDL_MASTER_CHANNEL(struct apple80211_awdl_master_channel *) override;
    IOReturn getAWDL_SYNC_STATE(struct apple80211_awdl_sync_state *) override;
    IOReturn getAWDL_ELECTION_RSSI_THRESHOLDS(
        struct apple80211_awdl_election_rssi_thresholds *) override;
    IOReturn getAWDL_PRESENCE_MODE(struct apple80211_awdl_presence_mode *) override;
    IOReturn getAWDL_ELECTION_PARAMS(struct apple80211_awdl_election_params *) override;
    IOReturn getAWDL_DEVICE_CAPABILITIES(struct apple80211_awdl_device_capabilities *) override;
    IOReturn getAWDL_SECONDARY_MASTER_CHANNEL(
        struct apple80211_awdl_secondary_master_channel *) override;
    IOReturn getAWDL_AF_TX_MODE(struct apple80211_awdl_af_tx_mode *) override;
    IOReturn getAWDL_MIN_RATE(struct apple80211_awdl_min_rate *) override;
    IOReturn getAWDL_ENCRYPTION_TYPE(struct apple80211_encrypt *) override;
    IOReturn getAWDL_TXCAL_PERIOD(struct apple80211_awdl_txcal_period *) override;
    IOReturn getAWDL_DEV_STATS(struct apple80211_awdl_dev_stats *) override;
    IOReturn getAWDL_DFSP_CONFIG(struct apple80211_awdl_dfsp_config *) override;
    IOReturn getAWDL_CCA(struct apple80211_awdl_cca *) override;
    IOReturn getAWDL_AF_SECONDARY_PAYLOAD_CAPABILITIES(struct apple80211_awdl_cap *) override;
    IOReturn getLLW_PARAMS(struct apple80211_llw_params *) override;
    IOReturn setSSID(struct apple80211_ssid_data *) override;
    IOReturn setCHANNEL(struct apple80211_channel_data *) override;
    IOReturn setAWDL_SYNC_PARAMS(struct apple80211_awdl_sync_params *) override;
    IOReturn setAWDL_SYNC_ENABLED(struct apple80211_awdl_sync_enabled *) override;
    IOReturn setAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(
        struct apple80211_awdl_extension_state_machine_parameter *) override;
    IOReturn setAWDL_ELECTION_ID(struct apple80211_awdl_election_id *) override;
    IOReturn setAWDL_BSSID(struct apple80211_awdl_bssid *) override;
    IOReturn setAWDL_ELECTION_METRIC(struct apple80211_awdl_election_metric *) override;
    IOReturn setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(
        struct apple80211_awdl_sync_channel_sequence *) override;
    IOReturn setAWDL_MASTER_CHANNEL(struct apple80211_awdl_master_channel *) override;
    IOReturn setAWDL_SYNC_STATE(struct apple80211_awdl_sync_state *) override;
    IOReturn setAWDL_ELECTION_RSSI_THRESHOLDS(
        struct apple80211_awdl_election_rssi_thresholds *) override;
    IOReturn setAWDL_PRESENCE_MODE(struct apple80211_awdl_presence_mode *) override;
    IOReturn setPEER_CACHE_CONTROL(struct apple80211_peer_cache_control *) override;
    IOReturn setAWDL_OOB_REQUEST(struct apple80211_awdl_oob_request *) override;
    IOReturn setAWDL_SYNC_FRAME_TEMPLATE(struct apple80211_awdl_sync_frame_template *) override;
    IOReturn setAWDL_ELECTION_PARAMS(struct apple80211_awdl_election_params *) override;
    IOReturn setAWDL_PEER_TRAFFIC_REGISTRATION(
        struct apple80211_awdl_peer_traffic_registration *) override;
    IOReturn setAWDL_SECONDARY_MASTER_CHANNEL(
        struct apple80211_awdl_secondary_master_channel *) override;
    IOReturn setAWDL_AF_TX_MODE(struct apple80211_awdl_af_tx_mode *) override;
    IOReturn setAWDL_MIN_RATE(struct apple80211_awdl_min_rate *) override;
    IOReturn setAWDL_ENCRYPTION_KEYS(struct apple80211_key *) override;
    IOReturn setAWDL_ENCRYPTION_TYPE(struct apple80211_encrypt *) override;
    IOReturn setAWDL_OOB_AUTO_REQUEST(struct apple80211_awdl_oob_request *) override;
    IOReturn setAWDL_TXCAL_PERIOD(struct apple80211_awdl_txcal_period *) override;
    IOReturn setAWDL_AWDL_ADVERTISERS(struct awdl_peer_advert_add *) override;
    IOReturn setAWDL_DFSP_CONFIG(struct apple80211_awdl_dfsp_config *) override;
    IOReturn setAWDL_DFSP_UCSA_CONFIG(struct apple80211_awdl_dfsp_ucsa_config *) override;
    IOReturn setCOMPANION_SKYWALK_IF_CREATE(
        struct apple80211_companion_skywalk_if_create *) override;
    IOReturn setAWDL_OOB_SEC_PAYLOAD_REQUEST(
        struct apple80211_awdl_af_sec_payload_request *) override;
    IOReturn setLLW_PARAMS(struct apple80211_llw_params *) override;
    IOReturn setTX_RATE(struct apple80211_tx_rate *) override;
    IOReturn setP2P_COEX_PROTECTION(struct apple80211_p2p_coex_protection *) override;
    IOReturn setPSF_DWELL(struct apple80211_awdl_psf_dwell_params *) override;
    IOReturn setAWDL_CONFIG(struct apple80211_awdl_config_request *) override;
    IOReturn setAWDL_RTG_PEER_STATS_CONFIG(struct apple80211_rtg_peer_stats_cfg *) override;

    // Recording, also used by the controller for its own AWDL steps.
    void note(const char *what, const void *bytes, size_t length);

    // The data path, set up by the controller around its pools and queues.
    void attachDataPath(IOWorkLoop *workLoop, IOSkywalkTxCompletionQueue *txCompletion,
                        IOSkywalkRxCompletionQueue *rxCompletion, IOSkywalkPacketBufferPool *rxPool);
    void detachDataPath();
    UInt32 popTransmitted(IOSkywalkPacket **packets, UInt32 count);
    UInt32 popReceived(IOSkywalkPacket **packets, UInt32 count);

    // From the firmware (vif VAP_VIF_AWDL), in the controller's work loop.
    void inputAction(const uint8_t *frame, uint32_t length, uint16_t freq, int8_t rssi);
    void inputData(const uint8_t *frame, uint32_t length);
    void firmwareEvent(uint16_t type, const void *payload, uint32_t length);

private:
    // Requests whose size this tree knows are echoed back by their getter.
    IOReturn noteSet(const char *name, const void *data, size_t knownLength);
    IOReturn noteGet(const char *name, void *data, size_t knownLength);
    void noteTransmit(IOSkywalkPacket *const *packets, UInt32 count);
    void publish();

    // The firmware side: configuration and templates, sent from the work loop.
    void configChanged();
    void templateChanged(unsigned kind, const uint8_t *tlvs, size_t length);
    void kick();
    static void flushAction(OSObject *owner, IOTimerEventSource *sender);
    void flush();
    UInt32 transmitPackets(IOSkywalkPacket *const *packets, UInt32 count);
    void publishCounters();

    static constexpr unsigned kTemplateMax = 1500;
    static constexpr unsigned kRing = 512;
    static constexpr unsigned kBufferSize = 2048;   // the awdl0 pools' buffers (SKYWALK_BUF_SIZE)

    IOLock *fLock;
    OSDictionary *fCounts;      // name -> calls
    OSDictionary *fLastSet;     // name -> bytes of the last set
    OSArray *fTrace;            // ordered "ms name len bytes" lines, bounded
    uint32_t fSequence;
    uint32_t fTransmitCallbacks;
    uint32_t fTransmitOffered;
    ether_addr fAddress;

    IO80211Controller *fController;
    IOWorkLoop *fWorkLoop;
    IOTimerEventSource *fFlushTimer;
    bool fFlushPending;
    struct vap_awdl_config fConfig;     // under fLock
    bool fConfigDirty, fFirmwareRunning;
    IOReturn fConfigResult;
    uint8_t fTemplate[3][kTemplateMax];         // VAP_AWDL_TMPL_SYNC, _MI, _ONCE
    uint16_t fTemplateLength[3];
    bool fTemplateDirty[3];
    uint8_t fSend[sizeof(struct vap_awdl_template) + kTemplateMax];   // the work loop's request buffer

    IOSimpleLock *fRingLock;
    IOSkywalkTxCompletionQueue *fTxCompletion;
    IOSkywalkRxCompletionQueue *fRxCompletion;
    IOSkywalkPacketBufferPool *fRxPool;
    IOSkywalkPacket *fTxDone[kRing];
    unsigned fTxDoneHead, fTxDoneCount;
    IOSkywalkPacket *fRxReady[kRing];
    unsigned fRxHead, fRxCount;
    uint32_t fTxFrames, fTxDropped, fRxFrames, fRxDropped, fActionFrames, fActionDropped;
    uint32_t fPeersAdded, fPeersRemoved, fStateEvents;
};

#endif /* __IO80211_TARGET >= __MAC_26_0 */

#endif /* AirportItlwmAWDLInterface_hpp */
