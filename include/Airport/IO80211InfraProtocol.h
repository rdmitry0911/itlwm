//
//  IO80211InfraProtocol.h
//  itlwm
//
//  Created by qcwap on 2023/6/14.
//  Copyright © 2023 钟先耀. All rights reserved.
//
//  Corrected vtable layout extracted from macOS 26.3 (25D125)
//  BootKernelExtensions.kc on 2026-04-07.
//  Total: 194 pure virtual methods (75 GET + 119 SET)
//  Vtable slots [470]-[663] of IO80211InfraProtocol.
//

#ifndef IO80211InfraProtocol_h
#define IO80211InfraProtocol_h

#include <IOKit/IOService.h>
#include <Airport/apple80211_var.h>
#include <Airport/apple80211_ioctl.h>

// Forward declarations for GET parameter types
struct apple80211_power_debug_info;
struct apple80211_roam_profile_all_bands;
struct apple80211_dbg_guard_time_params;
struct apple80211_country_channel_data;
struct apple80211_private_mac_data;
struct apple80211_ranging_enable_request_t;
struct apple80211_ranging_start_request_t;
struct apple80211_rsdb_capability;
struct apple80211_tko_params;
struct apple80211_tko_dump;
struct apple80211_btcoex_profile;
struct apple80211_btcoex_profile_active_data;
struct apple80211_trap_info_data;
struct apple80211_thermal_index_t;
struct apple80211_btcoex_max_nss_for_ap_data;
struct apple80211_btcoex_2g_chain_disable;
struct apple80211_power_budget_t;
struct apple80211_ranging_capabilities_t;
struct apple80211_lqm_config_t;
struct apple80211_trap_mini_dump_data;
struct apple80211_beacon_info_t;
struct apple80211_chip_power_limit;
struct apple80211_nss_data;
struct apple80211_hw_mac_address;
struct appl80211_chip_diags_data;
struct apple80211_hp2p_ctrl;
struct bss_blacklist;
struct apple80211_txrx_chain_info;
struct apple80211_mimo_status;
struct apple80211_pmk;
struct apple80211_dynsar_detail;
struct apple80211_slow_wifi_feature_enabled;
struct apple80211_timesync_info;
struct apple80211_sensing_data_t;
struct apple80211_fw_hot_channels;
struct apple80211_low_latency_info;
struct apple80211_beacon_msg;
struct apple80211_wcl_traffic_counters;
struct apple80211_he_counters_ctl;
struct apple80211ChannelInfo;
struct apple80211_rsn_xe_data;
struct apple80211_sib_coex_status;
struct apple80211_extended_bss_info;
struct apple80211_wcl_low_latency_stats;
struct apple80211_bgscan_cached_network_data_list;
struct apple80211_wcl_wnm_offload_t;
struct apple80211_noise_per_ant_t;
struct apple80211_fw_clock_info;
struct apple80211_timesync_stats;
struct apple80211_system_sleep_config;
struct apple80211_smartcca_opmode;
struct apple80211_lqm_statistics;
struct apple80211_he_capability;
struct apple80211_p2p_device_capability;

// Forward declarations for SET parameter types
struct apple80211_ie_data;
struct apple80211_wow_test_data;
struct apple80211_offload_arp_data;
struct apple80211_offload_ndp_data;
struct apple80211_gas_query_t;
struct apple80211_leaky_ap_setting;
struct apple80211_reset_command;
struct apple80211_crash_command;
struct apple80211_ranging_authenticate_request_t;
struct apple80211_dynamic_rssi_window_config;
struct apple80211_usb_host_notification_data;
struct apple80211_set_property_unserialized_data;
struct apple80211_roam_cache_data;
struct apple80211_pm_mode;
struct apple80211_wifi_assertion_data;
struct apple80211_sensing_enable_t;
struct apple80211_sensing_disable_t;
struct apple80211_leave_network;
struct apple80211_reassoc;
struct apple80211_set_roam_lock;
struct apple80211_legacy_roam_profile_config;
struct apple80211_roam_profile_config;
struct apple80211_user_roam_cache;
struct apple80211_wcl_real_time_mode;
struct apple80211_wcl_arp_mode;
struct apple80211_wcl_abort_join;
struct triggerCC;
struct apple80211ScanRequest;
struct apple80211AssocCandidates;
struct apple80211_wcl_qos_params;
struct scanHomeAndAwayTime;
struct apple80211_voice_ind_state;
struct apple80211_mws_accessory_power_limit;
struct apple80211_wcl_ulofdma_state;
struct apple80211_wcl_action_frame;
struct apple80211_feature_flags;
struct apple80211_dhcp_renewal_data;
struct apple80211_battery_ps_config;
struct apple80211_mimo_config;
struct apple80211_bg_motion_profile;
struct apple80211_bg_network;
struct apple80211_bg_scan;
struct apple80211_bg_params;
struct apple80211_power_profile;
struct apple80211_interface_setting;
struct apple80211_bypass_tx_power_cap;
struct apple80211_facetime_wificalling_params;
struct apple80211_ipv4_params;
struct apple80211_wcl_wnm_config_t;
struct apple80211_limited_aggregation_config;
struct apple80211_bcn_mute_config;
struct apple80211_eap_filter_config;
struct apple80211_wow_low_power_mode;
struct apple80211_dual_power_mode_params;
struct apple80211_fastlane;
struct apple80211_associated_sleep_config;
struct apple80211_congestion_control_indication;
struct apple80211_standalone_state;
struct apple80211_ipv6_params;
struct apple80211_infra_enumerated;
struct apple80211_lmtpc_config;
struct apple80211_traffic_eng_params;
struct apple80211_le_scan_params;
struct apple80211_timesync_gpio;
struct apple80211_host_clock_info;
struct apple80211_fw_clock_source;
struct apple80211_timesync_tx_policy;
struct apple80211_timesync_rx_policy;
struct apple80211_timestamping_en;
struct appl80211_sleep_on_inactivity_config;
struct apple80211_mws_time_sharing;
struct apple80211_mws_wifi_channel_bitmap;
struct apple80211_mws_rfem_config;
struct apple80211_mws_scan_freq;
struct apple80211_mws_scan_freq_mode;
struct apple80211_mws_condition_id_config;
struct apple80211_mws_antenna_selection;
struct apple80211_ndd_data;
struct apple80211_drbg_entropy;
struct apple80211_sdb_enable;
struct apple80211_btcoex_ext_profile;
struct apple80211_os_eligibility;
struct apple80211_tx_mode_config;

class IO80211InfraProtocol : public IO80211InfraInterface {
    OSDeclareAbstractStructors(IO80211InfraProtocol)

public:
    //
    // GET methods — vtable slots [470]-[544] (75 methods)
    //
    // [470] AWDL internal method — pure_virtual even in AppleBCMWLAN.
    // Called from IO80211AWDLPeer for data stats. Not a standard IOCTL.
    virtual IOReturn getAWDL_PEER_TRAFFIC_STATS(void *, unsigned int) = 0;
    // [471]
    virtual IOReturn getCHANNEL(apple80211_channel_data *) = 0;
    // [472]
    virtual IOReturn getPOWERSAVE(apple80211_powersave_data *) = 0;
    // [473]
    virtual IOReturn getTXPOWER(apple80211_txpower_data *) = 0;
    // [474]
    virtual IOReturn getRATE(apple80211_rate_data *) = 0;
    // [475]
    virtual IOReturn getOP_MODE(apple80211_opmode_data *) = 0;
    // [476]
    virtual IOReturn getRSSI(apple80211_rssi_data *) = 0;
    // [477]
    virtual IOReturn getSUPPORTED_CHANNELS(apple80211_sup_channel_data *) = 0;
    // [478]
    virtual IOReturn getGUARD_INTERVAL(apple80211_guard_interval_data *) = 0;
    // [479]
    virtual IOReturn getMCS(apple80211_mcs_data *) = 0;
    // [480]
    virtual IOReturn getPOWER_DEBUG_INFO(apple80211_power_debug_info *) = 0;
    // [481]
    virtual IOReturn getHT_CAPABILITY(apple80211_ht_capability *) = 0;
    // [482]
    virtual IOReturn getMCS_VHT(apple80211_mcs_vht_data *) = 0;
    // [483]
    virtual IOReturn getCHANNELS_INFO(apple80211_channels_info *) = 0;
    // [484]
    virtual IOReturn getVHT_CAPABILITY(apple80211_vht_capability *) = 0;
    // [485]
    virtual IOReturn getROAM_PROFILE(apple80211_roam_profile_all_bands *) = 0;
    // [486]
    virtual IOReturn getCHIP_COUNTER_STATS(apple80211_chip_stats *) = 0;
    // [487]
    virtual IOReturn getDBG_GUARD_TIME_PARAMS(apple80211_dbg_guard_time_params *) = 0;
    // [488]
    virtual IOReturn getLEAKY_AP_STATS_MODE(apple80211_leaky_ap_setting *) = 0;
    // [489]
    virtual IOReturn getCOUNTRY_CHANNELS(apple80211_country_channel_data *) = 0;
    // [490]
    virtual IOReturn getPRIVATE_MAC(apple80211_private_mac_data *) = 0;
    // [491]
    virtual IOReturn getRANGING_ENABLE(apple80211_ranging_enable_request_t *) = 0;
    // [492]
    virtual IOReturn getRANGING_START(apple80211_ranging_start_request_t *) = 0;
    // [493]
    virtual IOReturn getAWDL_RSDB_CAPS(apple80211_rsdb_capability *) = 0;
    // [494]
    virtual IOReturn getTKO_PARAMS(apple80211_tko_params *) = 0;
    // [495]
    virtual IOReturn getTKO_DUMP(apple80211_tko_dump *) = 0;
    // [496]
    virtual IOReturn getHW_SUPPORTED_CHANNELS(apple80211_sup_channel_data *) = 0;
    // [497]
    virtual IOReturn getBTCOEX_PROFILE(apple80211_btcoex_profile *) = 0;
    // [498]
    virtual IOReturn getBTCOEX_PROFILE_ACTIVE(apple80211_btcoex_profile_active_data *) = 0;
    // [499]
    virtual IOReturn getTRAP_INFO(apple80211_trap_info_data *) = 0;
    // [500]
    virtual IOReturn getTHERMAL_INDEX(apple80211_thermal_index_t *) = 0;
    // [501]
    virtual IOReturn getMAX_NSS_FOR_AP(apple80211_btcoex_max_nss_for_ap_data *) = 0;
    // [502]
    virtual IOReturn getBTCOEX_2G_CHAIN_DISABLE(apple80211_btcoex_2g_chain_disable *) = 0;
    // [503]
    virtual IOReturn getPOWER_BUDGET(apple80211_power_budget_t *) = 0;
    // [504]
    virtual IOReturn getOFFLOAD_TCPKA_ENABLE(apple80211_offload_tcpka_enable_t *) = 0;
    // [505]
    virtual IOReturn getRANGING_CAPS(apple80211_ranging_capabilities_t *) = 0;
    // [506]
    virtual IOReturn getLQM_CONFIG(apple80211_lqm_config_t *) = 0;
    // [507]
    virtual IOReturn getTRAP_CRASHTRACER_MINI_DUMP(apple80211_trap_mini_dump_data *) = 0;
    // [508]
    virtual IOReturn getBEACON_INFO(apple80211_beacon_info_t *) = 0;
    // [509]
    virtual IOReturn getCHIP_POWER_RANGE(apple80211_chip_power_limit *) = 0;
    // [510]
    virtual IOReturn getNSS(apple80211_nss_data *) = 0;
    // [511]
    virtual IOReturn getHW_ADDR(apple80211_hw_mac_address *) = 0;
    // [512]
    virtual IOReturn getCHIP_DIAGS(appl80211_chip_diags_data *) = 0;
    // [513]
    virtual IOReturn getHP2P_CTRL(apple80211_hp2p_ctrl *) = 0;
    // [514]
    virtual IOReturn getBSS_BLACKLIST(bss_blacklist *) = 0;
    // [515]
    virtual IOReturn getTXRX_CHAIN_INFO(apple80211_txrx_chain_info *) = 0;
    // [516]
    virtual IOReturn getMIMO_STATUS(apple80211_mimo_status *) = 0;
    // [517]
    virtual IOReturn getCUR_PMK(apple80211_pmk *) = 0;
    // [518]
    virtual IOReturn getDYNSAR_DETAIL(apple80211_dynsar_detail *) = 0;
    // [519]
    virtual IOReturn getCOUNTRY_CHANNELS_INFO(apple80211_channels_info *) = 0;
    // [520]
    virtual IOReturn getLQM_SUMMARY(apple80211_lqm_summary *) = 0;
    // [521]
    virtual IOReturn getSLOW_WIFI_FEATURE_ENABLED(apple80211_slow_wifi_feature_enabled *) = 0;
    // [522]
    virtual IOReturn getTIMESYNC_INFO(apple80211_timesync_info *) = 0;
    // [523]
    virtual IOReturn getSENSING_DATA(apple80211_sensing_data_t *) = 0;
    // [524]
    virtual IOReturn getWCL_FW_HOT_CHANNELS(apple80211_fw_hot_channels *) = 0;
    // [525]
    virtual IOReturn getWCL_LOW_LATENCY_INFO(apple80211_low_latency_info *) = 0;
    // [526]
    virtual IOReturn getWCL_BSS_INFO(apple80211_beacon_msg *) = 0;
    // [527]
    virtual IOReturn getWCL_TRAFFIC_COUNTERS(apple80211_wcl_traffic_counters *) = 0;
    // [528]
    virtual IOReturn getWCL_GET_TX_BLANKING_STATUS(uint *) = 0;
    // [529]
    virtual IOReturn getHE_COUNTERS(apple80211_he_counters_ctl *) = 0;
    // [530]
    virtual IOReturn getWCL_CHANNELS_INFO(apple80211ChannelInfo *) = 0;
    // [531]
    virtual IOReturn getRSN_XE(apple80211_rsn_xe_data *) = 0;
    // [532]
    virtual IOReturn getSIB_COEX_STATUS(apple80211_sib_coex_status *) = 0;
    // [533]
    virtual IOReturn getWCL_EXTENDED_BSS_INFO(apple80211_extended_bss_info *) = 0;
    // [534]
    virtual IOReturn getWCL_LOW_LATENCY_INFO_STATS(apple80211_wcl_low_latency_stats *) = 0;
    // [535]
    virtual IOReturn getWCL_BGSCAN_CACHE_RESULT(apple80211_bgscan_cached_network_data_list *) = 0;
    // [536]
    virtual IOReturn getWCL_WNM_OFFLOAD(apple80211_wcl_wnm_offload_t *) = 0;
    // [537]
    virtual IOReturn getWIFI_NOISE_PER_ANT(apple80211_noise_per_ant_t *) = 0;
    // [538]
    virtual IOReturn getFW_CLOCK_INFO(apple80211_fw_clock_info *) = 0;
    // [539]
    virtual IOReturn getTIMESYNC_STATS(apple80211_timesync_stats *) = 0;
    // [540]
    virtual IOReturn getSYSTEM_SLEEP_CONFIG(apple80211_system_sleep_config *) = 0;
    // [541]
    virtual IOReturn getSMARTCCA_OPMODE(apple80211_smartcca_opmode *) = 0;
    // [542]
    virtual IOReturn getLQM_STATISTICS(apple80211_lqm_statistics *) = 0;
    // [543]
    virtual IOReturn getHE_CAPABILITY(apple80211_he_capability *) = 0;
    // [544]
    virtual IOReturn getP2P_DEVICE_CAPABILITY(apple80211_p2p_device_capability *) = 0;

    //
    // SET methods — vtable slots [545]-[663] (119 methods)
    //
    // [545]
    virtual IOReturn setCIPHER_KEY(apple80211_key *) = 0;
    // [546]
    virtual IOReturn setCHANNEL(apple80211_channel_data *) = 0;
    // [547]
    virtual IOReturn setPOWERSAVE(apple80211_powersave_data *) = 0;
    // [548]
    virtual IOReturn setTXPOWER(apple80211_txpower_data *) = 0;
    // [549]
    virtual IOReturn setRATE(apple80211_rate_data *) = 0;
    // [550]
    virtual IOReturn setIBSS_MODE(apple80211_network_data *) = 0;
    // [551]
    virtual IOReturn setAP_MODE(apple80211_apmode_data *) = 0;
    // [552]
    virtual IOReturn setIE(apple80211_ie_data *) = 0;
    // [553]
    virtual IOReturn setWOW_TEST(apple80211_wow_test_data *) = 0;
    // [554]
    virtual IOReturn setCLEAR_PMKSA_CACHE(void *) = 0;
    // [555]
    virtual IOReturn setVIRTUAL_IF_CREATE(apple80211_virt_if_create_data *) = 0;
    // [556]
    virtual IOReturn setHT_CAPABILITY(apple80211_ht_capability *) = 0;
    // [557]
    virtual IOReturn setOFFLOAD_ARP(apple80211_offload_arp_data *) = 0;
    // [558]
    virtual IOReturn setOFFLOAD_NDP(apple80211_offload_ndp_data *) = 0;
    // [559]
    virtual IOReturn setGAS_REQ(apple80211_gas_query_t *) = 0;
    // [560]
    virtual IOReturn setVHT_CAPABILITY(apple80211_vht_capability *) = 0;
    // [561]
    virtual IOReturn setROAM_PROFILE(apple80211_roam_profile_all_bands *) = 0;
    // [562]
    virtual IOReturn setDBG_GUARD_TIME_PARAMS(apple80211_dbg_guard_time_params *) = 0;
    // [563]
    virtual IOReturn setLEAKY_AP_STATS_MODE(apple80211_leaky_ap_setting *) = 0;
    // [564]
    virtual IOReturn setPRIVATE_MAC(apple80211_private_mac_data *) = 0;
    // [565]
    virtual IOReturn setRESET_CHIP(apple80211_reset_command *) = 0;
    // [566]
    virtual IOReturn setCRASH(apple80211_crash_command *) = 0;
    // [567]
    virtual IOReturn setRANGING_ENABLE(apple80211_ranging_enable_request_t *) = 0;
    // [568]
    virtual IOReturn setRANGING_START(apple80211_ranging_start_request_t *) = 0;
    // [569]
    virtual IOReturn setRANGING_AUTHENTICATE(apple80211_ranging_authenticate_request_t *) = 0;
    // [570]
    virtual IOReturn setTKO_PARAMS(apple80211_tko_params *) = 0;
    // [571]
    virtual IOReturn setBTCOEX_PROFILE(apple80211_btcoex_profile *) = 0;
    // [572]
    virtual IOReturn setBTCOEX_PROFILE_ACTIVE(apple80211_btcoex_profile_active_data *) = 0;
    // [573]
    virtual IOReturn setTHERMAL_INDEX(apple80211_thermal_index_t *) = 0;
    // [574]
    virtual IOReturn setBTCOEX_2G_CHAIN_DISABLE(apple80211_btcoex_2g_chain_disable *) = 0;
    // [575]
    virtual IOReturn setPOWER_BUDGET(apple80211_power_budget_t *) = 0;
    // [576]
    virtual IOReturn setOFFLOAD_TCPKA_ENABLE(apple80211_offload_tcpka_enable_t *) = 0;
    // [577]
    virtual IOReturn setLQM_CONFIG(apple80211_lqm_config_t *) = 0;
    // [578]
    virtual IOReturn setDYNAMIC_RSSI_WINDOW_CONFIG(apple80211_dynamic_rssi_window_config *) = 0;
    // [579]
    virtual IOReturn setUSB_HOST_NOTIFICATION(apple80211_usb_host_notification_data *) = 0;
    // [580]
    virtual IOReturn setHP2P_CTRL(apple80211_hp2p_ctrl *) = 0;
    // [581]
    virtual IOReturn setBSS_BLACKLIST(bss_blacklist *) = 0;
    // [582]
    virtual IOReturn setSET_PROPERTY(apple80211_set_property_unserialized_data *) = 0;
    // [583]
    virtual IOReturn setROAM_CACHE_UPDATE(apple80211_roam_cache_data *) = 0;
    // [584]
    virtual IOReturn setPM_MODE(apple80211_pm_mode *) = 0;
    // [585]
    virtual IOReturn setSET_WIFI_ASSERTION_STATE(apple80211_wifi_assertion_data *) = 0;
    // [586]
    virtual IOReturn setREALTIME_QOS_MSCS(apple80211_state_data *) = 0;
    // [587]
    virtual IOReturn setSENSING_ENABLE(apple80211_sensing_enable_t *) = 0;
    // [588]
    virtual IOReturn setSENSING_DISABLE(apple80211_sensing_disable_t *) = 0;
    // [589]
    virtual IOReturn setWCL_LEAVE_NETWORK(apple80211_leave_network *) = 0;
    // [590]
    virtual IOReturn setWCL_REASSOC(apple80211_reassoc *) = 0;
    // [591]
    virtual IOReturn setWCL_SET_ROAM_LOCK(apple80211_set_roam_lock *) = 0;
    // [592]
    virtual IOReturn setWCL_LEGACY_ROAM_PROFILE_CONFIG(apple80211_legacy_roam_profile_config *) = 0;
    // [593]
    virtual IOReturn setWCL_ROAM_PROFILE_CONFIG(apple80211_roam_profile_config *) = 0;
    // [594]
    virtual IOReturn setWCL_ROAM_USER_CACHE(apple80211_user_roam_cache *) = 0;
    // [595]
    virtual IOReturn setWCL_SCAN_ABORT(void *) = 0;
    // [596]
    virtual IOReturn setWCL_REAL_TIME_MODE(apple80211_wcl_real_time_mode *) = 0;
    // [597]
    virtual IOReturn setWCL_ARP_MODE(apple80211_wcl_arp_mode *) = 0;
    // [598]
    virtual IOReturn setWCL_JOIN_ABORT(apple80211_wcl_abort_join *) = 0;
    // [599]
    virtual IOReturn setWCL_TRIGGER_CC(triggerCC *) = 0;
    // [600]
    virtual IOReturn setWCL_SCAN_REQ(apple80211ScanRequest *) = 0;
    // [601]
    virtual IOReturn setWCL_ASSOCIATE(apple80211AssocCandidates *) = 0;
    // [602]
    virtual IOReturn setWCL_QOS_PARAMS(apple80211_wcl_qos_params *) = 0;
    // [603]
    virtual IOReturn setWCL_LINK_UP_DONE(void *) = 0;
    // [604]
    virtual IOReturn setWCL_SET_SCAN_HOME_AWAY_TIME(scanHomeAndAwayTime *) = 0;
    // [605]
    virtual IOReturn setVOICE_IND_STATE(apple80211_voice_ind_state *) = 0;
    // [606]
    virtual IOReturn setRSN_XE(apple80211_rsn_xe_data *) = 0;
    // [607]
    virtual IOReturn setMWS_ACCESSORY_POWER_LIMIT_WIFI_ENH(apple80211_mws_accessory_power_limit *) = 0;
    // [608]
    virtual IOReturn setWCL_ULOFDMA_STATE(apple80211_wcl_ulofdma_state *) = 0;
    // [609]
    virtual IOReturn setWCL_ACTION_FRAME(apple80211_wcl_action_frame *) = 0;
    // [610]
    virtual IOReturn setGAS_ABORT(void *) = 0;
    // [611]
    virtual IOReturn setOS_FEATURE_FLAGS(apple80211_feature_flags *) = 0;
    // [612]
    virtual IOReturn setDHCP_RENEWAL_DATA(apple80211_dhcp_renewal_data *) = 0;
    // [613]
    virtual IOReturn setBATTERY_POWERSAVE_CONFIG(apple80211_battery_ps_config *) = 0;
    // [614]
    virtual IOReturn setMIMO_CONFIG(apple80211_mimo_config *) = 0;
    // [615]
    virtual IOReturn setWCL_CONFIG_BG_MOTIONPROFILE(apple80211_bg_motion_profile *) = 0;
    // [616]
    virtual IOReturn setWCL_CONFIG_BG_NETWORK(apple80211_bg_network *) = 0;
    // [617]
    virtual IOReturn setWCL_CONFIG_BGSCAN(apple80211_bg_scan *) = 0;
    // [618]
    virtual IOReturn setWCL_CONFIG_BG_PARAMS(apple80211_bg_params *) = 0;
    // [619]
    virtual IOReturn setPOWER_PROFILE(apple80211_power_profile *) = 0;
    // [620]
    virtual IOReturn setHEARTBEAT(void *) = 0;
    // [621]
    virtual IOReturn setINTERFACE_SETTING(apple80211_interface_setting *) = 0;
    // [622]
    virtual IOReturn setBYPASS_TX_POWER_CAP(apple80211_bypass_tx_power_cap *) = 0;
    // [623]
    virtual IOReturn setFACETIME_WIFICALLING_PARAMS(apple80211_facetime_wificalling_params *) = 0;
    // [624]
    virtual IOReturn setIPV4_PARAMS(apple80211_ipv4_params *) = 0;
    // [625]
    virtual IOReturn setWCL_WNM_OPS(apple80211_wcl_wnm_config_t *) = 0;
    // [626]
    virtual IOReturn setWCL_WNM_OFFLOAD(apple80211_wcl_wnm_offload_t *) = 0;
    // [627]
    virtual IOReturn setWCL_LIMITED_AGGREGATION(apple80211_limited_aggregation_config *) = 0;
    // [628]
    virtual IOReturn setWCL_BCN_MUTE_CONFIG(apple80211_bcn_mute_config *) = 0;
    // [629]
    virtual IOReturn setEAP_FILTER_CONFIG(apple80211_eap_filter_config *) = 0;
    // [630]
    virtual IOReturn setWOW_LOW_POWER_MODE(apple80211_wow_low_power_mode *) = 0;
    // [631]
    virtual IOReturn setDUAL_POWER_MODE(apple80211_dual_power_mode_params *) = 0;
    // [632]
    virtual IOReturn setWCL_UPDATE_FAST_LANE(apple80211_fastlane *) = 0;
    // [633]
    virtual IOReturn setWCL_ASSOCIATED_SLEEP(apple80211_associated_sleep_config *) = 0;
    // [634]
    virtual IOReturn setCONGESTION_CTRL_IND(apple80211_congestion_control_indication *) = 0;
    // [635]
    virtual IOReturn setSTAND_ALONE_MODE_STATE(apple80211_standalone_state *) = 0;
    // [636]
    virtual IOReturn setIPV6_PARAMS(apple80211_ipv6_params *) = 0;
    // [637]
    virtual IOReturn setINFRA_ENUMERATED(apple80211_infra_enumerated *) = 0;
    // [638]
    virtual IOReturn setLMTPC_CONFIG(apple80211_lmtpc_config *) = 0;
    // [639]
    virtual IOReturn setTRAFFIC_ENG_PARAMS(apple80211_traffic_eng_params *) = 0;
    // [640]
    virtual IOReturn setLE_SCAN_PARAM(apple80211_le_scan_params *) = 0;
    // [641]
    virtual IOReturn setTIMESYNC_GPIO(apple80211_timesync_gpio *) = 0;
    // [642]
    virtual IOReturn setHOST_CLOCK_INFO(apple80211_host_clock_info *) = 0;
    // [643]
    virtual IOReturn setFW_CLOCK_SOURCE(apple80211_fw_clock_source *) = 0;
    // [644]
    virtual IOReturn setTIMESYNC_TX_POLICY(apple80211_timesync_tx_policy *) = 0;
    // [645]
    virtual IOReturn setTIMESYNC_RX_POLICY(apple80211_timesync_rx_policy *) = 0;
    // [646]
    virtual IOReturn setTIMESTAMPING_EN(apple80211_timestamping_en *) = 0;
    // [647]
    virtual IOReturn setWCL_SOI_CONFIG(appl80211_sleep_on_inactivity_config *) = 0;
    // [648]
    virtual IOReturn setMWS_TIME_SHARING_WIFI_ENH(apple80211_mws_time_sharing *) = 0;
    // [649]
    virtual IOReturn setMWS_WIFI_TYPE_7_BITMAP_WIFI_ENH(apple80211_mws_wifi_channel_bitmap *) = 0;
    // [650]
    virtual IOReturn setMWS_COEX_BITMAP_WIFI_ENH(apple80211_mws_wifi_channel_bitmap *) = 0;
    // [651]
    virtual IOReturn setMWS_DISABLE_OCL_BITMAP_WIFI_ENH(apple80211_mws_wifi_channel_bitmap *) = 0;
    // [652]
    virtual IOReturn setMWS_RFEM_CONFIG_WIFI_ENH(apple80211_mws_rfem_config *) = 0;
    // [653]
    virtual IOReturn setMWS_ASSOC_PROTECTION_BITMAP_WIFI_ENH(apple80211_mws_wifi_channel_bitmap *) = 0;
    // [654]
    virtual IOReturn setMWS_SCAN_FREQ_WIFI_ENH(apple80211_mws_scan_freq *) = 0;
    // [655]
    virtual IOReturn setMWS_SCAN_FREQ_MODE_WIFI_ENH(apple80211_mws_scan_freq_mode *) = 0;
    // [656]
    virtual IOReturn setMWS_CONDITION_ID_BITMAP_WIFI_ENH(apple80211_mws_condition_id_config *) = 0;
    // [657]
    virtual IOReturn setMWS_ANTENNA_SELECTION_WIFI_ENH(apple80211_mws_antenna_selection *) = 0;
    // [658]
    virtual IOReturn setNDD_REQ(apple80211_ndd_data *) = 0;
    // [659]
    virtual IOReturn setDBRG_ENTROPY(apple80211_drbg_entropy *) = 0;
    // [660]
    virtual IOReturn setSDB_ENABLE(apple80211_sdb_enable *) = 0;
    // [661]
    virtual IOReturn setBTCOEX_EXT_PROFILE(apple80211_btcoex_ext_profile *) = 0;
    // [662]
    virtual IOReturn setOS_ELIGIBILITY(apple80211_os_eligibility *) = 0;
    // [663]
    virtual IOReturn setTX_MODE_CONFIG(apple80211_tx_mode_config *) = 0;

public:
    uint8_t filler[0x120];
};

#endif /* IO80211InfraProtocol_h */
