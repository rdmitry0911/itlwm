//
//  IO80211AWDLProtocol.h
//  itlwm
//
//  Tahoe AWDL (role 6) protocol class of IO80211Family. Apple's class is
//  abstract: its own slots are pure virtuals that the reference Broadcom
//  driver implements in AppleBCMWLANProximityInterface. A third-party awdl0
//  object must therefore match the table slot for slot.
//
//  The method list and its order follow ForbiddenItlwm's Tahoe declarations
//  (GPL-2.0, github.com/retropineaple/ForbiddenItlwm, include/Airport/macOS26),
//  checked against the 25C56 tables (see docs/AWDL_FEASIBILITY.md in v-Airport).
//  The base is this tree's IO80211VirtualInterface, which ends at slot 504 on
//  25C56 (IO80211SapProtocol starts its own slots at 505 as well).
//

#ifndef IO80211AWDLProtocol_h
#define IO80211AWDLProtocol_h

#include "IO80211VirtualInterface.h"
#include "apple80211_var.h"

#if __IO80211_TARGET >= __MAC_26_0

struct apple80211_awdl_af_sec_payload_request;
struct apple80211_awdl_af_tx_mode;
struct apple80211_awdl_bssid;
struct apple80211_awdl_cap;
struct apple80211_awdl_cca;
struct apple80211_awdl_config_request;
struct apple80211_awdl_dev_stats;
struct apple80211_awdl_device_capabilities;
struct apple80211_awdl_dfsp_config;
struct apple80211_awdl_dfsp_ucsa_config;
struct apple80211_awdl_election_id;
struct apple80211_awdl_election_metric;
struct apple80211_awdl_election_params;
struct apple80211_awdl_election_rssi_thresholds;
struct apple80211_awdl_extension_state_machine_parameter;
struct apple80211_awdl_master_channel;
struct apple80211_awdl_min_rate;
struct apple80211_awdl_oob_request;
struct apple80211_awdl_peer_traffic_registration;
struct apple80211_awdl_presence_mode;
struct apple80211_awdl_psf_dwell_params;
struct apple80211_awdl_secondary_master_channel;
struct apple80211_awdl_sync_channel_sequence;
struct apple80211_awdl_sync_enabled;
struct apple80211_awdl_sync_frame_template;
struct apple80211_awdl_sync_params;
struct apple80211_awdl_sync_state;
struct apple80211_awdl_txcal_period;
struct apple80211_bssid_data;
struct apple80211_channel_data;
struct apple80211_companion_skywalk_if_create;
struct apple80211_encrypt;
struct apple80211_key;
struct apple80211_llw_params;
struct apple80211_p2p_coex_protection;
struct apple80211_peer_cache_control;
struct apple80211_peer_cache_maximum_size;
struct apple80211_rtg_peer_stats_cfg;
struct apple80211_ssid_data;
struct apple80211_state_data;
struct apple80211_tx_rate;
struct awdl_peer_advert_add;

/*
 * IO80211AWDLProtocol -> IO80211VirtualInterface -> IO80211SkywalkInterface.
 * Slots 505..567: 63 pure methods, in this order.
 */
class IO80211AWDLProtocol : public IO80211VirtualInterface {
    OSDeclareAbstractStructors(IO80211AWDLProtocol)

public:
    virtual IOReturn getSSID(struct apple80211_ssid_data *) = 0;                                   // 505
    virtual IOReturn getCHANNEL(struct apple80211_channel_data *) = 0;
    virtual IOReturn getBSSID(struct apple80211_bssid_data *) = 0;
    virtual IOReturn getSTATE(struct apple80211_state_data *) = 0;
    virtual IOReturn getAWDL_SYNC_PARAMS(struct apple80211_awdl_sync_params *) = 0;
    virtual IOReturn getAWDL_SYNC_ENABLED(struct apple80211_awdl_sync_enabled *) = 0;            // 510
    virtual IOReturn getAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(
        struct apple80211_awdl_extension_state_machine_parameter *) = 0;
    virtual IOReturn getAWDL_ELECTION_ID(struct apple80211_awdl_election_id *) = 0;
    virtual IOReturn getAWDL_ELECTION_METRIC(struct apple80211_awdl_election_metric *) = 0;
    virtual IOReturn getAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(
        struct apple80211_awdl_sync_channel_sequence *) = 0;
    virtual IOReturn getPEER_CACHE_MAXIMUM_SIZE(struct apple80211_peer_cache_maximum_size *) = 0; // 515
    virtual IOReturn getAWDL_MASTER_CHANNEL(struct apple80211_awdl_master_channel *) = 0;
    virtual IOReturn getAWDL_SYNC_STATE(struct apple80211_awdl_sync_state *) = 0;
    virtual IOReturn getAWDL_ELECTION_RSSI_THRESHOLDS(
        struct apple80211_awdl_election_rssi_thresholds *) = 0;
    virtual IOReturn getAWDL_PRESENCE_MODE(struct apple80211_awdl_presence_mode *) = 0;
    virtual IOReturn getAWDL_ELECTION_PARAMS(struct apple80211_awdl_election_params *) = 0;      // 520
    virtual IOReturn getAWDL_DEVICE_CAPABILITIES(struct apple80211_awdl_device_capabilities *) = 0;
    virtual IOReturn getAWDL_SECONDARY_MASTER_CHANNEL(
        struct apple80211_awdl_secondary_master_channel *) = 0;
    virtual IOReturn getAWDL_AF_TX_MODE(struct apple80211_awdl_af_tx_mode *) = 0;
    virtual IOReturn getAWDL_MIN_RATE(struct apple80211_awdl_min_rate *) = 0;
    virtual IOReturn getAWDL_ENCRYPTION_TYPE(struct apple80211_encrypt *) = 0;                   // 525
    virtual IOReturn getAWDL_TXCAL_PERIOD(struct apple80211_awdl_txcal_period *) = 0;
    virtual IOReturn getAWDL_DEV_STATS(struct apple80211_awdl_dev_stats *) = 0;
    virtual IOReturn getAWDL_DFSP_CONFIG(struct apple80211_awdl_dfsp_config *) = 0;
    virtual IOReturn getAWDL_CCA(struct apple80211_awdl_cca *) = 0;
    virtual IOReturn getAWDL_AF_SECONDARY_PAYLOAD_CAPABILITIES(struct apple80211_awdl_cap *) = 0; // 530
    virtual IOReturn getLLW_PARAMS(struct apple80211_llw_params *) = 0;
    virtual IOReturn setSSID(struct apple80211_ssid_data *) = 0;
    virtual IOReturn setCHANNEL(struct apple80211_channel_data *) = 0;
    virtual IOReturn setAWDL_SYNC_PARAMS(struct apple80211_awdl_sync_params *) = 0;
    virtual IOReturn setAWDL_SYNC_ENABLED(struct apple80211_awdl_sync_enabled *) = 0;            // 535
    virtual IOReturn setAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(
        struct apple80211_awdl_extension_state_machine_parameter *) = 0;
    virtual IOReturn setAWDL_ELECTION_ID(struct apple80211_awdl_election_id *) = 0;
    virtual IOReturn setAWDL_BSSID(struct apple80211_awdl_bssid *) = 0;
    virtual IOReturn setAWDL_ELECTION_METRIC(struct apple80211_awdl_election_metric *) = 0;
    virtual IOReturn setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(
        struct apple80211_awdl_sync_channel_sequence *) = 0;                                     // 540
    virtual IOReturn setAWDL_MASTER_CHANNEL(struct apple80211_awdl_master_channel *) = 0;
    virtual IOReturn setAWDL_SYNC_STATE(struct apple80211_awdl_sync_state *) = 0;
    virtual IOReturn setAWDL_ELECTION_RSSI_THRESHOLDS(
        struct apple80211_awdl_election_rssi_thresholds *) = 0;
    virtual IOReturn setAWDL_PRESENCE_MODE(struct apple80211_awdl_presence_mode *) = 0;
    virtual IOReturn setPEER_CACHE_CONTROL(struct apple80211_peer_cache_control *) = 0;          // 545
    virtual IOReturn setAWDL_OOB_REQUEST(struct apple80211_awdl_oob_request *) = 0;
    virtual IOReturn setAWDL_SYNC_FRAME_TEMPLATE(struct apple80211_awdl_sync_frame_template *) = 0;
    virtual IOReturn setAWDL_ELECTION_PARAMS(struct apple80211_awdl_election_params *) = 0;
    virtual IOReturn setAWDL_PEER_TRAFFIC_REGISTRATION(
        struct apple80211_awdl_peer_traffic_registration *) = 0;
    virtual IOReturn setAWDL_SECONDARY_MASTER_CHANNEL(
        struct apple80211_awdl_secondary_master_channel *) = 0;                                  // 550
    virtual IOReturn setAWDL_AF_TX_MODE(struct apple80211_awdl_af_tx_mode *) = 0;
    virtual IOReturn setAWDL_MIN_RATE(struct apple80211_awdl_min_rate *) = 0;
    virtual IOReturn setAWDL_ENCRYPTION_KEYS(struct apple80211_key *) = 0;
    virtual IOReturn setAWDL_ENCRYPTION_TYPE(struct apple80211_encrypt *) = 0;
    virtual IOReturn setAWDL_OOB_AUTO_REQUEST(struct apple80211_awdl_oob_request *) = 0;         // 555
    virtual IOReturn setAWDL_TXCAL_PERIOD(struct apple80211_awdl_txcal_period *) = 0;
    virtual IOReturn setAWDL_AWDL_ADVERTISERS(struct awdl_peer_advert_add *) = 0;
    virtual IOReturn setAWDL_DFSP_CONFIG(struct apple80211_awdl_dfsp_config *) = 0;
    virtual IOReturn setAWDL_DFSP_UCSA_CONFIG(struct apple80211_awdl_dfsp_ucsa_config *) = 0;
    virtual IOReturn setCOMPANION_SKYWALK_IF_CREATE(
        struct apple80211_companion_skywalk_if_create *) = 0;                                   // 560
    virtual IOReturn setAWDL_OOB_SEC_PAYLOAD_REQUEST(
        struct apple80211_awdl_af_sec_payload_request *) = 0;
    virtual IOReturn setLLW_PARAMS(struct apple80211_llw_params *) = 0;
    virtual IOReturn setTX_RATE(struct apple80211_tx_rate *) = 0;
    virtual IOReturn setP2P_COEX_PROTECTION(struct apple80211_p2p_coex_protection *) = 0;
    virtual IOReturn setPSF_DWELL(struct apple80211_awdl_psf_dwell_params *) = 0;               // 565
    virtual IOReturn setAWDL_CONFIG(struct apple80211_awdl_config_request *) = 0;
    virtual IOReturn setAWDL_RTG_PEER_STATS_CONFIG(struct apple80211_rtg_peer_stats_cfg *) = 0; // 567
};

enum {
    kIO80211AWDLProtocolVtableSlotFirst = 505,
    kIO80211AWDLProtocolVtableSlotLast = 567,
    kIO80211AWDLProtocolMethodCount = 63,
};

#endif /* __IO80211_TARGET >= __MAC_26_0 */

#endif /* IO80211AWDLProtocol_h */
