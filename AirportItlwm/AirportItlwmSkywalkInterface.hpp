//
//  AirportItlwmSkywalkInterface.hpp
//  AirportItlwm-Sonoma
//
//  Created by qcwap on 2023/6/27.
//  Copyright © 2023 钟先耀. All rights reserved.
//

#ifndef AirportItlwmSkywalkInterface_hpp
#define AirportItlwmSkywalkInterface_hpp

#include <Airport/Apple80211.h>

// Struct used by non-virtual IOCTL methods (removed from InfraProtocol in Tahoe)
struct apple80211_colocated_network_scope_id
{
    uint32_t version;
    uint32_t id1;
    uint32_t id2;
    uint8_t  reserved[0x24];
} __attribute__((packed));
static_assert(sizeof(apple80211_colocated_network_scope_id) == 0x30,
              "apple80211_colocated_network_scope_id must match Tahoe WCL ABI");

class AirportItlwm;

class AirportItlwmSkywalkInterface : public IO80211InfraProtocol {
    OSDeclareDefaultStructors(AirportItlwmSkywalkInterface)

public:
#if __IO80211_TARGET >= __MAC_26_0
    virtual bool init() override;
    virtual bool init(IOService *, ether_addr *) override;
    bool bindController(AirportItlwm *);
#else
    virtual bool init(IOService *) override;
#endif
    virtual void free() override;
    virtual IOReturn processBSDCommand(ifnet_t, UInt, void *) override;
    virtual UInt64 createEventPipe(IO80211APIUserClient *) override;

    // Override getInterfaceSubFamily — returns IFNET_SUBFAMILY_WIFI (3).
    //
    // This is the correct Apple mechanism for setting if_subfamily.
    // IOSkywalkFamily has a separate function (vmaddr 0xffffff8002a3dae8,
    // missed by Ghidra decompiler) that calls getInterfaceSubFamily() via
    // vtable slot 0x9A8 and stores the result at eparams+0x140 (subfamily).
    // Verified by opcode search in BootKernelExtensions.kc 25D125:
    //   0xffffff8002a3dc02: callq *0x9a0(%rax)  → getInterfaceFamily()
    //   0xffffff8002a3dc11: callq *0x9a8(%rax)  → getInterfaceSubFamily()
    //   0xffffff8002a3dc17: mov [%rbx+0x140],%eax → stores subfamily
    //
    // Apple's AppleBCMWLANIO80211APSTAInterface::getInterfaceSubFamily()
    // (0xffffff8001686a5e) does exactly this: validateDispatchQueue(), return 3.
    //
    // Note: initBSDInterfaceParameters itself does NOT call getInterfaceSubFamily
    // (confirmed — only calls getHardwareAssists 0x990, getFeatureFlags 0x9B8,
    // getTSOOptions 0xA10). The subfamily is set by the separate IOSkywalkFamily
    // function above, which fills eparams callbacks (demux/proto/framer) AND
    // identity fields (family/subfamily).
    //
    // Without subfamily=3, airportd's _getIfListCopy Path B (ioctl 0xC020699F)
    // does not recognize the interface as Wi-Fi → no IOCTLs → no ASSOCIATE.
    //
    // Future bring-up note: verify that the BSD name being inspected really
    // belongs to AirportItlwm before reasoning from its ioctl results.  en*
    // numbering is not stable and may point at unrelated USB Ethernet devices.
    virtual void *getInterfaceSubFamily(void) override {
        XYLog("getInterfaceSubFamily: returning 3 (IFNET_SUBFAMILY_WIFI)\n");
        return (void *)3;  // IFNET_SUBFAMILY_WIFI
    }

    void associateSSID(uint8_t *ssid, uint32_t ssid_len, const struct ether_addr &bssid, uint32_t authtype_lower, uint32_t authtype_upper, uint8_t *key, uint32_t key_len, int key_index);
    void setPTK(const u_int8_t *key, size_t key_len);
    void setGTK(const u_int8_t *key, size_t key_len, u_int8_t kid, u_int8_t *rsc);

    // Non-virtual methods: IOCTLs removed from InfraProtocol vtable in Tahoe.
    // These are handled by IO80211Controller/WCL internally on Tahoe, but
    // our driver still needs them for internal state management.
    IOReturn getSSID(apple80211_ssid_data *);
    IOReturn getAUTH_TYPE(apple80211_authtype_data *);
    IOReturn setAUTH_TYPE(apple80211_authtype_data *);
    IOReturn getBSSID(apple80211_bssid_data *);
    IOReturn getSCAN_RESULT(apple80211_scan_result *);
    IOReturn getSTATE(apple80211_state_data *);
    IOReturn getPHY_MODE(apple80211_phymode_data *);
    IOReturn getNOISE(apple80211_noise_data *);
    IOReturn getLOCALE(apple80211_locale_data *);
    IOReturn getDEAUTH(apple80211_deauth_data *);
    IOReturn getRATE_SET(apple80211_rate_set_data *);
    IOReturn getRSN_IE(apple80211_rsn_ie_data *);
    IOReturn setRSN_IE(apple80211_rsn_ie_data *);
    IOReturn getAP_IE_LIST(apple80211_ap_ie_data *);
    IOReturn getASSOCIATION_STATUS(apple80211_assoc_status_data *);
    IOReturn getMCS_INDEX_SET(apple80211_mcs_index_set_data *);
    IOReturn getVHT_MCS_INDEX_SET(apple80211_vht_mcs_index_set_data *);
    IOReturn getLINK_CHANGED_EVENT_DATA(apple80211_link_changed_event_data *);
    IOReturn setASSOCIATE(apple80211_assoc_data *);
    IOReturn setDISASSOCIATE(void *);
    IOReturn setDEAUTH(apple80211_deauth_data *);
    IOReturn setSCAN_REQ(apple80211_scan_data *);
    IOReturn getCURRENT_NETWORK(apple80211_scan_result *);
    IOReturn getCOLOCATED_NETWORK_SCOPE_ID(apple80211_colocated_network_scope_id *);
    IOReturn processApple80211Ioctl(UInt, apple80211req *);

public:
    //
    // GET methods — vtable slots [470]-[544] (75 methods)
    // Order MUST match IO80211InfraProtocol.h exactly.
    //
    // [470] AWDL internal
    virtual IOReturn getAWDL_PEER_TRAFFIC_STATS(void *, unsigned int) override { XYLog("DEBUG VTABLE [470] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [471]
    virtual IOReturn getCHANNEL(apple80211_channel_data *) override;
    // [472]
    virtual IOReturn getPOWERSAVE(apple80211_powersave_data *) override;
    // [473]
    virtual IOReturn getTXPOWER(apple80211_txpower_data *) override;
    // [474]
    virtual IOReturn getRATE(apple80211_rate_data *) override;
    // [475]
    virtual IOReturn getOP_MODE(apple80211_opmode_data *) override;
    // [476]
    virtual IOReturn getRSSI(apple80211_rssi_data *) override;
    // [477]
    virtual IOReturn getSUPPORTED_CHANNELS(apple80211_sup_channel_data *) override;
    // [478]
    virtual IOReturn getGUARD_INTERVAL(apple80211_guard_interval_data *) override;
    // [479]
    virtual IOReturn getMCS(apple80211_mcs_data *) override;
    // [480]
    virtual IOReturn getPOWER_DEBUG_INFO(apple80211_power_debug_info *) override { XYLog("DEBUG VTABLE [480] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [481]
    virtual IOReturn getHT_CAPABILITY(apple80211_ht_capability *) override;
    // [482]
    virtual IOReturn getMCS_VHT(apple80211_mcs_vht_data *) override;
    // [483]
    // Tahoe/26.x still issues legacy APPLE80211_IOC_CHANNELS_INFO from the UI
    // scan/status path.  Leaving slot [483] unsupported causes repeated
    // 0xe00002c7 failures in live IOC DEBUG logs even though the older
    // AirportItlwm controller already has a working CHANNELS_INFO producer.
    virtual IOReturn getCHANNELS_INFO(apple80211_channels_info *) override;
    // [484]
    virtual IOReturn getVHT_CAPABILITY(apple80211_vht_capability *) override;
    // [485]
    virtual IOReturn getROAM_PROFILE(apple80211_roam_profile_all_bands *) override { XYLog("DEBUG VTABLE [485] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [486]
    virtual IOReturn getCHIP_COUNTER_STATS(apple80211_chip_stats *) override { XYLog("DEBUG VTABLE [486] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [487]
    virtual IOReturn getDBG_GUARD_TIME_PARAMS(apple80211_dbg_guard_time_params *) override { XYLog("DEBUG VTABLE [487] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [488]
    virtual IOReturn getLEAKY_AP_STATS_MODE(apple80211_leaky_ap_setting *) override { XYLog("DEBUG VTABLE [488] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [489]
    virtual IOReturn getCOUNTRY_CHANNELS(apple80211_country_channel_data *) override { XYLog("DEBUG VTABLE [489] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [490]
    virtual IOReturn getPRIVATE_MAC(apple80211_private_mac_data *) override;
    // [491]
    // [491] — AppleBCMWLANInfraProtocol is a direct `return 0xe00002c7;`
    // stub on Tahoe. Keep it explicitly unsupported until a different family
    // path is recovered.
    virtual IOReturn getRANGING_ENABLE(apple80211_ranging_enable_request_t *) override { XYLog("DEBUG VTABLE [491] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [492] — AppleBCMWLANInfraProtocol is a direct `return 0xe00002c7;`
    // stub on Tahoe.
    virtual IOReturn getRANGING_START(apple80211_ranging_start_request_t *) override { XYLog("DEBUG VTABLE [492] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [493]
    virtual IOReturn getAWDL_RSDB_CAPS(apple80211_rsdb_capability *) override { XYLog("DEBUG VTABLE [493] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [494]
    virtual IOReturn getTKO_PARAMS(apple80211_tko_params *) override { XYLog("DEBUG VTABLE [494] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [495]
    virtual IOReturn getTKO_DUMP(apple80211_tko_dump *) override { XYLog("DEBUG VTABLE [495] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [496]
    virtual IOReturn getHW_SUPPORTED_CHANNELS(apple80211_sup_channel_data *) override { XYLog("DEBUG VTABLE [496] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [497]
    virtual IOReturn getBTCOEX_PROFILE(apple80211_btcoex_profile *) override { XYLog("DEBUG VTABLE [497] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [498]
    virtual IOReturn getBTCOEX_PROFILE_ACTIVE(apple80211_btcoex_profile_active_data *) override { XYLog("DEBUG VTABLE [498] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [499]
    virtual IOReturn getTRAP_INFO(apple80211_trap_info_data *) override { XYLog("DEBUG VTABLE [499] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [500]
    virtual IOReturn getTHERMAL_INDEX(apple80211_thermal_index_t *) override;
    // [501]
    virtual IOReturn getMAX_NSS_FOR_AP(apple80211_btcoex_max_nss_for_ap_data *) override { XYLog("DEBUG VTABLE [501] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [502]
    virtual IOReturn getBTCOEX_2G_CHAIN_DISABLE(apple80211_btcoex_2g_chain_disable *) override { XYLog("DEBUG VTABLE [502] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [503]
    virtual IOReturn getPOWER_BUDGET(apple80211_power_budget_t *) override;
    // [504]
    virtual IOReturn getOFFLOAD_TCPKA_ENABLE(apple80211_offload_tcpka_enable_t *) override;
    // [505]
    // [505] — AppleBCMWLANInfraProtocol is a direct `return 0xe00002c7;`
    // stub on Tahoe.
    virtual IOReturn getRANGING_CAPS(apple80211_ranging_capabilities_t *) override { XYLog("DEBUG VTABLE [505] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [506]
    virtual IOReturn getLQM_CONFIG(apple80211_lqm_config_t *) override { XYLog("DEBUG VTABLE [506] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [507]
    virtual IOReturn getTRAP_CRASHTRACER_MINI_DUMP(apple80211_trap_mini_dump_data *) override { XYLog("DEBUG VTABLE [507] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [508]
    virtual IOReturn getBEACON_INFO(apple80211_beacon_info_t *) override { XYLog("DEBUG VTABLE [508] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [509]
    virtual IOReturn getCHIP_POWER_RANGE(apple80211_chip_power_limit *) override { XYLog("DEBUG VTABLE [509] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [510]
    virtual IOReturn getNSS(apple80211_nss_data *) override;
    // [511]
    virtual IOReturn getHW_ADDR(apple80211_hw_mac_address *) override;
    // [512]
    virtual IOReturn getCHIP_DIAGS(appl80211_chip_diags_data *) override { XYLog("DEBUG VTABLE [512] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [513]
    virtual IOReturn getHP2P_CTRL(apple80211_hp2p_ctrl *) override { XYLog("DEBUG VTABLE [513] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [514]
    virtual IOReturn getBSS_BLACKLIST(bss_blacklist *) override { XYLog("DEBUG VTABLE [514] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [515]
    virtual IOReturn getTXRX_CHAIN_INFO(apple80211_txrx_chain_info *) override { XYLog("DEBUG VTABLE [515] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [516]
    virtual IOReturn getMIMO_STATUS(apple80211_mimo_status *) override { XYLog("DEBUG VTABLE [516] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [517]
    virtual IOReturn getCUR_PMK(apple80211_pmk *) override { XYLog("DEBUG VTABLE [517] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [518]
    virtual IOReturn getDYNSAR_DETAIL(apple80211_dynsar_detail *) override { XYLog("DEBUG VTABLE [518] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [519]
    // [519] — AppleBCMWLANInfraProtocol returns `0xe00002c7` directly for this
    // selector on Tahoe.
    virtual IOReturn getCOUNTRY_CHANNELS_INFO(apple80211_channels_info *) override { XYLog("DEBUG VTABLE [519] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [520]
    virtual IOReturn getLQM_SUMMARY(apple80211_lqm_summary *) override { XYLog("DEBUG VTABLE [520] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [521]
    virtual IOReturn getSLOW_WIFI_FEATURE_ENABLED(apple80211_slow_wifi_feature_enabled *) override { XYLog("DEBUG VTABLE [521] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [522]
    virtual IOReturn getTIMESYNC_INFO(apple80211_timesync_info *) override;
    // [523]
    virtual IOReturn getSENSING_DATA(apple80211_sensing_data_t *) override { XYLog("DEBUG VTABLE [523] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [524]
    virtual IOReturn getWCL_FW_HOT_CHANNELS(apple80211_fw_hot_channels *) override { XYLog("DEBUG VTABLE [524] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [525]
    virtual IOReturn getWCL_LOW_LATENCY_INFO(apple80211_low_latency_info *) override { XYLog("DEBUG VTABLE [525] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [526]
    virtual IOReturn getWCL_BSS_INFO(apple80211_beacon_msg *) override;
    // [527]
    virtual IOReturn getWCL_TRAFFIC_COUNTERS(apple80211_wcl_traffic_counters *) override { XYLog("DEBUG VTABLE [527] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [528]
    virtual IOReturn getWCL_GET_TX_BLANKING_STATUS(uint *) override { XYLog("DEBUG VTABLE [528] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [529]
    virtual IOReturn getHE_COUNTERS(apple80211_he_counters_ctl *) override { XYLog("DEBUG VTABLE [529] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [530]
    virtual IOReturn getWCL_CHANNELS_INFO(apple80211ChannelInfo *) override;
    // [531]
    virtual IOReturn getRSN_XE(apple80211_rsn_xe_data *) override { XYLog("DEBUG VTABLE [531] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [532]
    virtual IOReturn getSIB_COEX_STATUS(apple80211_sib_coex_status *) override { XYLog("DEBUG VTABLE [532] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [533]
    virtual IOReturn getWCL_EXTENDED_BSS_INFO(apple80211_extended_bss_info *) override { XYLog("DEBUG VTABLE [533] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [534]
    virtual IOReturn getWCL_LOW_LATENCY_INFO_STATS(apple80211_wcl_low_latency_stats *) override { XYLog("DEBUG VTABLE [534] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [535]
    virtual IOReturn getWCL_BGSCAN_CACHE_RESULT(apple80211_bgscan_cached_network_data_list *) override;
    // [536]
    virtual IOReturn getWCL_WNM_OFFLOAD(apple80211_wcl_wnm_offload_t *) override { XYLog("DEBUG VTABLE [536] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [537]
    virtual IOReturn getWIFI_NOISE_PER_ANT(apple80211_noise_per_ant_t *) override { XYLog("DEBUG VTABLE [537] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [538]
    // [538] — AppleBCMWLANInfraProtocol is a direct `return 0xe00002c7;`
    // stub on Tahoe.
    virtual IOReturn getFW_CLOCK_INFO(apple80211_fw_clock_info *) override { XYLog("DEBUG VTABLE [538] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [539] — AppleBCMWLANInfraProtocol is a direct `return 0xe00002c7;`
    // stub on Tahoe.
    virtual IOReturn getTIMESYNC_STATS(apple80211_timesync_stats *) override { XYLog("DEBUG VTABLE [539] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [540]
    virtual IOReturn getSYSTEM_SLEEP_CONFIG(apple80211_system_sleep_config *) override { XYLog("DEBUG VTABLE [540] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [541]
    virtual IOReturn getSMARTCCA_OPMODE(apple80211_smartcca_opmode *) override { XYLog("DEBUG VTABLE [541] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [542]
    virtual IOReturn getLQM_STATISTICS(apple80211_lqm_statistics *) override { XYLog("DEBUG VTABLE [542] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [543]
    virtual IOReturn getHE_CAPABILITY(apple80211_he_capability *) override;
    // [544]
    virtual IOReturn getP2P_DEVICE_CAPABILITY(apple80211_p2p_device_capability *) override;

    //
    // SET methods — vtable slots [545]-[663] (119 methods)
    // Order MUST match IO80211InfraProtocol.h exactly.
    //
    // [545]
    virtual IOReturn setCIPHER_KEY(apple80211_key *) override;
    // [546]
    virtual IOReturn setCHANNEL(apple80211_channel_data *) override { XYLog("DEBUG VTABLE [546] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [547]
    virtual IOReturn setPOWERSAVE(apple80211_powersave_data *) override;
    // [548]
    virtual IOReturn setTXPOWER(apple80211_txpower_data *) override { XYLog("DEBUG VTABLE [548] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [549]
    virtual IOReturn setRATE(apple80211_rate_data *) override { XYLog("DEBUG VTABLE [549] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [550]
    virtual IOReturn setIBSS_MODE(apple80211_network_data *) override { XYLog("DEBUG VTABLE [550] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [551]
    virtual IOReturn setAP_MODE(apple80211_apmode_data *) override { XYLog("DEBUG VTABLE [551] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [552]
    virtual IOReturn setIE(apple80211_ie_data *) override { XYLog("DEBUG VTABLE [552] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [553]
    virtual IOReturn setWOW_TEST(apple80211_wow_test_data *) override { XYLog("DEBUG VTABLE [553] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [554]
    virtual IOReturn setCLEAR_PMKSA_CACHE(void *) override;
    // [555]
    virtual IOReturn setVIRTUAL_IF_CREATE(apple80211_virt_if_create_data *) override { XYLog("DEBUG VTABLE [555] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [556]
    virtual IOReturn setHT_CAPABILITY(apple80211_ht_capability *) override { XYLog("DEBUG VTABLE [556] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [557]
    virtual IOReturn setOFFLOAD_ARP(apple80211_offload_arp_data *) override;
    // [558]
    virtual IOReturn setOFFLOAD_NDP(apple80211_offload_ndp_data *) override { XYLog("DEBUG VTABLE [558] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [559]
    virtual IOReturn setGAS_REQ(apple80211_gas_query_t *) override { XYLog("DEBUG VTABLE [559] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [560]
    virtual IOReturn setVHT_CAPABILITY(apple80211_vht_capability *) override { XYLog("DEBUG VTABLE [560] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [561]
    virtual IOReturn setROAM_PROFILE(apple80211_roam_profile_all_bands *) override { XYLog("DEBUG VTABLE [561] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [562]
    virtual IOReturn setDBG_GUARD_TIME_PARAMS(apple80211_dbg_guard_time_params *) override { XYLog("DEBUG VTABLE [562] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [563]
    virtual IOReturn setLEAKY_AP_STATS_MODE(apple80211_leaky_ap_setting *) override { XYLog("DEBUG VTABLE [563] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [564]
    virtual IOReturn setPRIVATE_MAC(apple80211_private_mac_data *) override { XYLog("DEBUG VTABLE [564] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [565]
    virtual IOReturn setRESET_CHIP(apple80211_reset_command *) override { XYLog("DEBUG VTABLE [565] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [566]
    virtual IOReturn setCRASH(apple80211_crash_command *) override { XYLog("DEBUG VTABLE [566] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [567]
    virtual IOReturn setRANGING_ENABLE(apple80211_ranging_enable_request_t *) override { XYLog("DEBUG VTABLE [567] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [568]
    virtual IOReturn setRANGING_START(apple80211_ranging_start_request_t *) override { XYLog("DEBUG VTABLE [568] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [569]
    virtual IOReturn setRANGING_AUTHENTICATE(apple80211_ranging_authenticate_request_t *) override { XYLog("DEBUG VTABLE [569] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [570]
    virtual IOReturn setTKO_PARAMS(apple80211_tko_params *) override { XYLog("DEBUG VTABLE [570] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [571]
    virtual IOReturn setBTCOEX_PROFILE(apple80211_btcoex_profile *) override { XYLog("DEBUG VTABLE [571] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [572]
    virtual IOReturn setBTCOEX_PROFILE_ACTIVE(apple80211_btcoex_profile_active_data *) override { XYLog("DEBUG VTABLE [572] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [573]
    virtual IOReturn setTHERMAL_INDEX(apple80211_thermal_index_t *) override { XYLog("DEBUG VTABLE [573] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [574]
    virtual IOReturn setBTCOEX_2G_CHAIN_DISABLE(apple80211_btcoex_2g_chain_disable *) override { XYLog("DEBUG VTABLE [574] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [575]
    virtual IOReturn setPOWER_BUDGET(apple80211_power_budget_t *) override { XYLog("DEBUG VTABLE [575] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [576]
    virtual IOReturn setOFFLOAD_TCPKA_ENABLE(apple80211_offload_tcpka_enable_t *) override;
    // [577]
    virtual IOReturn setLQM_CONFIG(apple80211_lqm_config_t *) override { XYLog("DEBUG VTABLE [577] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [578]
    virtual IOReturn setDYNAMIC_RSSI_WINDOW_CONFIG(apple80211_dynamic_rssi_window_config *) override { XYLog("DEBUG VTABLE [578] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [579]
    virtual IOReturn setUSB_HOST_NOTIFICATION(apple80211_usb_host_notification_data *) override { XYLog("DEBUG VTABLE [579] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [580]
    virtual IOReturn setHP2P_CTRL(apple80211_hp2p_ctrl *) override { XYLog("DEBUG VTABLE [580] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [581]
    virtual IOReturn setBSS_BLACKLIST(bss_blacklist *) override { XYLog("DEBUG VTABLE [581] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [582]
    virtual IOReturn setSET_PROPERTY(apple80211_set_property_unserialized_data *) override { XYLog("DEBUG VTABLE [582] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [583]
    virtual IOReturn setROAM_CACHE_UPDATE(apple80211_roam_cache_data *) override { XYLog("DEBUG VTABLE [583] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [584]
    virtual IOReturn setPM_MODE(apple80211_pm_mode *) override;
    // [585]
    virtual IOReturn setSET_WIFI_ASSERTION_STATE(apple80211_wifi_assertion_data *) override { XYLog("DEBUG VTABLE [585] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [586]
    virtual IOReturn setREALTIME_QOS_MSCS(apple80211_state_data *) override { XYLog("DEBUG VTABLE [586] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [587]
    virtual IOReturn setSENSING_ENABLE(apple80211_sensing_enable_t *) override { XYLog("DEBUG VTABLE [587] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [588]
    virtual IOReturn setSENSING_DISABLE(apple80211_sensing_disable_t *) override { XYLog("DEBUG VTABLE [588] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [589] — AppleBCMWLAN: validates param, delegates to network leave handler
    virtual IOReturn setWCL_LEAVE_NETWORK(apple80211_leave_network *) override;
    // [590] — AppleBCMWLAN: validates param, snapshots reassoc parameters,
    // and delegates into NetAdapter reassoc send path.
    virtual IOReturn setWCL_REASSOC(apple80211_reassoc *data) override;
    // [591] — No AppleBCMWLAN / IO80211Family producer was found for this slot
    // in the current Tahoe decompile corpus. Returning success here would
    // advertise a non-existent producer path; keep it explicitly unsupported
    // until a real Apple implementation is recovered.
    virtual IOReturn setWCL_SET_ROAM_LOCK(apple80211_set_roam_lock *data) override { XYLog("DEBUG VTABLE [591] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [592] — AppleBCMWLAN: delegates to RoamAdapter legacy profile path.
    virtual IOReturn setWCL_LEGACY_ROAM_PROFILE_CONFIG(apple80211_legacy_roam_profile_config *data) override;
    // [593] — AppleBCMWLAN: delegates to RoamAdapter modern profile path.
    virtual IOReturn setWCL_ROAM_PROFILE_CONFIG(apple80211_roam_profile_config *data) override;
    // [594]
    virtual IOReturn setWCL_ROAM_USER_CACHE(apple80211_user_roam_cache *data) override;
    // [595] — AppleBCMWLAN: aborts ongoing scan
    virtual IOReturn setWCL_SCAN_ABORT(void *) override;
    // [596] — AppleBCMWLAN: sets real-time vs default mode
    virtual IOReturn setWCL_REAL_TIME_MODE(apple80211_wcl_real_time_mode *data) override;
    // [597] — AppleBCMWLAN: configures ARP keepalive/GARP mode.
    virtual IOReturn setWCL_ARP_MODE(apple80211_wcl_arp_mode *data) override;
    // [598]
    virtual IOReturn setWCL_JOIN_ABORT(apple80211_wcl_abort_join *data) override;
    // [599]
    virtual IOReturn setWCL_TRIGGER_CC(triggerCC *) override;
    // [600]
    virtual IOReturn setWCL_SCAN_REQ(apple80211ScanRequest *) override;
    // [601]
    virtual IOReturn setWCL_ASSOCIATE(apple80211AssocCandidates *) override;
    // [602] — AppleBCMWLAN: delegates to NetAdapter::setQosParams
    virtual IOReturn setWCL_QOS_PARAMS(apple80211_wcl_qos_params *data) override;
    // [603] — AppleBCMWLAN: calls PowerManager::handleLinkUpConfiguration
    virtual IOReturn setWCL_LINK_UP_DONE(void *) override;
    // [604]
    virtual IOReturn setWCL_SET_SCAN_HOME_AWAY_TIME(scanHomeAndAwayTime *data) override;
    // [605] — AppleBCMWLANInfraProtocol::setVOICE_IND_STATE is a direct
    // `return 0xe00002c7;` stub. Our old validate+ack body was a real
    // semantic mismatch because it advertised a producer path Apple does not
    // expose on Tahoe.
    virtual IOReturn setVOICE_IND_STATE(apple80211_voice_ind_state *data) override { XYLog("DEBUG VTABLE [605] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [606]
    virtual IOReturn setRSN_XE(apple80211_rsn_xe_data *) override { XYLog("DEBUG VTABLE [606] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [607]
    virtual IOReturn setMWS_ACCESSORY_POWER_LIMIT_WIFI_ENH(apple80211_mws_accessory_power_limit *) override { XYLog("DEBUG VTABLE [607] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [608]
    virtual IOReturn setWCL_ULOFDMA_STATE(apple80211_wcl_ulofdma_state *) override;
    // [609]
    virtual IOReturn setWCL_ACTION_FRAME(apple80211_wcl_action_frame *) override { XYLog("DEBUG VTABLE [609] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [610]
    virtual IOReturn setGAS_ABORT(void *) override { XYLog("DEBUG VTABLE [610] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [611] — AppleBCMWLAN: stores flags, applies DynSAR/KVR/6G feature configuration
    virtual IOReturn setOS_FEATURE_FLAGS(apple80211_feature_flags *data) override;
    // [612]
    virtual IOReturn setDHCP_RENEWAL_DATA(apple80211_dhcp_renewal_data *data) override;
    // [613]
    virtual IOReturn setBATTERY_POWERSAVE_CONFIG(apple80211_battery_ps_config *data) override;
    // [614]
    virtual IOReturn setMIMO_CONFIG(apple80211_mimo_config *) override;
    // [615] — AppleBCMWLAN: delegates to BGScanAdapter.
    virtual IOReturn setWCL_CONFIG_BG_MOTIONPROFILE(apple80211_bg_motion_profile *data) override;
    // [616] — AppleBCMWLAN: delegates to BGScanAdapter.
    virtual IOReturn setWCL_CONFIG_BG_NETWORK(apple80211_bg_network *data) override;
    // [617] — AppleBCMWLAN: handles enable/disable/periodic scan config.
    virtual IOReturn setWCL_CONFIG_BGSCAN(apple80211_bg_scan *data) override;
    // [618] — AppleBCMWLAN: delegates to BGScanAdapter.
    virtual IOReturn setWCL_CONFIG_BG_PARAMS(apple80211_bg_params *data) override;
    // [619] — AppleBCMWLAN: stores profile at offset, calls power config vtable
    virtual IOReturn setPOWER_PROFILE(apple80211_power_profile *data) override;
    // [620] — No Apple producer was recovered for this selector on Tahoe.
    virtual IOReturn setHEARTBEAT(void *) override { XYLog("DEBUG VTABLE [620] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [621] — No Apple producer was recovered for this selector on Tahoe.
    virtual IOReturn setINTERFACE_SETTING(apple80211_interface_setting *data) override { XYLog("DEBUG VTABLE [621] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [622]
    virtual IOReturn setBYPASS_TX_POWER_CAP(apple80211_bypass_tx_power_cap *) override { XYLog("DEBUG VTABLE [622] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [623]
    virtual IOReturn setFACETIME_WIFICALLING_PARAMS(apple80211_facetime_wificalling_params *) override;
    // [624] — AppleBCMWLAN: stores IPv4 addr/mask/gw, notifies InfraInterface
    virtual IOReturn setIPV4_PARAMS(apple80211_ipv4_params *data) override;
    // [625] — AppleBCMWLAN: delegates to WnmAdapter.
    virtual IOReturn setWCL_WNM_OPS(apple80211_wcl_wnm_config_t *) override;
    // [626] — AppleBCMWLAN: delegates to WnmAdapter.
    virtual IOReturn setWCL_WNM_OFFLOAD(apple80211_wcl_wnm_offload_t *) override;
    // [627]
    virtual IOReturn setWCL_LIMITED_AGGREGATION(apple80211_limited_aggregation_config *) override { XYLog("DEBUG VTABLE [627] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [628]
    virtual IOReturn setWCL_BCN_MUTE_CONFIG(apple80211_bcn_mute_config *) override { XYLog("DEBUG VTABLE [628] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [629]
    virtual IOReturn setEAP_FILTER_CONFIG(apple80211_eap_filter_config *) override { XYLog("DEBUG VTABLE [629] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [630]
    virtual IOReturn setWOW_LOW_POWER_MODE(apple80211_wow_low_power_mode *) override { XYLog("DEBUG VTABLE [630] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [631]
    virtual IOReturn setDUAL_POWER_MODE(apple80211_dual_power_mode_params *) override;
    // [632]
    virtual IOReturn setWCL_UPDATE_FAST_LANE(apple80211_fastlane *) override { XYLog("DEBUG VTABLE [632] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [633]
    virtual IOReturn setWCL_ASSOCIATED_SLEEP(apple80211_associated_sleep_config *) override { XYLog("DEBUG VTABLE [633] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [634]
    virtual IOReturn setCONGESTION_CTRL_IND(apple80211_congestion_control_indication *) override;
    // [635]
    virtual IOReturn setSTAND_ALONE_MODE_STATE(apple80211_standalone_state *) override { XYLog("DEBUG VTABLE [635] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [636] — AppleBCMWLAN: stores up to 10 IPv6 addresses, notifies InfraInterface
    virtual IOReturn setIPV6_PARAMS(apple80211_ipv6_params *data) override;
    // [637] — AppleBCMWLAN: validates param, returns success (minimal stub)
    virtual IOReturn setINFRA_ENUMERATED(apple80211_infra_enumerated *data) override;
    // [638]
    virtual IOReturn setLMTPC_CONFIG(apple80211_lmtpc_config *) override;
    // [639]
    virtual IOReturn setTRAFFIC_ENG_PARAMS(apple80211_traffic_eng_params *) override { XYLog("DEBUG VTABLE [639] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [640]
    virtual IOReturn setLE_SCAN_PARAM(apple80211_le_scan_params *) override;
    // [641]
    virtual IOReturn setTIMESYNC_GPIO(apple80211_timesync_gpio *) override { XYLog("DEBUG VTABLE [641] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [642]
    virtual IOReturn setHOST_CLOCK_INFO(apple80211_host_clock_info *) override { XYLog("DEBUG VTABLE [642] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [643]
    virtual IOReturn setFW_CLOCK_SOURCE(apple80211_fw_clock_source *) override { XYLog("DEBUG VTABLE [643] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [644]
    virtual IOReturn setTIMESYNC_TX_POLICY(apple80211_timesync_tx_policy *) override { XYLog("DEBUG VTABLE [644] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [645]
    virtual IOReturn setTIMESYNC_RX_POLICY(apple80211_timesync_rx_policy *) override { XYLog("DEBUG VTABLE [645] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [646]
    virtual IOReturn setTIMESTAMPING_EN(apple80211_timestamping_en *) override { XYLog("DEBUG VTABLE [646] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [647]
    virtual IOReturn setWCL_SOI_CONFIG(appl80211_sleep_on_inactivity_config *) override { XYLog("DEBUG VTABLE [647] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [648]
    virtual IOReturn setMWS_TIME_SHARING_WIFI_ENH(apple80211_mws_time_sharing *) override { XYLog("DEBUG VTABLE [648] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [649]
    virtual IOReturn setMWS_WIFI_TYPE_7_BITMAP_WIFI_ENH(apple80211_mws_wifi_channel_bitmap *) override { XYLog("DEBUG VTABLE [649] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [650]
    virtual IOReturn setMWS_COEX_BITMAP_WIFI_ENH(apple80211_mws_wifi_channel_bitmap *) override { XYLog("DEBUG VTABLE [650] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [651]
    virtual IOReturn setMWS_DISABLE_OCL_BITMAP_WIFI_ENH(apple80211_mws_wifi_channel_bitmap *) override { XYLog("DEBUG VTABLE [651] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [652]
    virtual IOReturn setMWS_RFEM_CONFIG_WIFI_ENH(apple80211_mws_rfem_config *) override { XYLog("DEBUG VTABLE [652] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [653]
    virtual IOReturn setMWS_ASSOC_PROTECTION_BITMAP_WIFI_ENH(apple80211_mws_wifi_channel_bitmap *) override { XYLog("DEBUG VTABLE [653] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [654]
    virtual IOReturn setMWS_SCAN_FREQ_WIFI_ENH(apple80211_mws_scan_freq *) override { XYLog("DEBUG VTABLE [654] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [655]
    virtual IOReturn setMWS_SCAN_FREQ_MODE_WIFI_ENH(apple80211_mws_scan_freq_mode *) override { XYLog("DEBUG VTABLE [655] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [656]
    virtual IOReturn setMWS_CONDITION_ID_BITMAP_WIFI_ENH(apple80211_mws_condition_id_config *) override { XYLog("DEBUG VTABLE [656] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [657]
    virtual IOReturn setMWS_ANTENNA_SELECTION_WIFI_ENH(apple80211_mws_antenna_selection *) override { XYLog("DEBUG VTABLE [657] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [658]
    virtual IOReturn setNDD_REQ(apple80211_ndd_data *) override { XYLog("DEBUG VTABLE [658] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [659]
    virtual IOReturn setDBRG_ENTROPY(apple80211_drbg_entropy *) override { XYLog("DEBUG VTABLE [659] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [660]
    virtual IOReturn setSDB_ENABLE(apple80211_sdb_enable *) override { XYLog("DEBUG VTABLE [660] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [661]
    virtual IOReturn setBTCOEX_EXT_PROFILE(apple80211_btcoex_ext_profile *) override { XYLog("DEBUG VTABLE [661] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [662]
    virtual IOReturn setOS_ELIGIBILITY(apple80211_os_eligibility *) override { XYLog("DEBUG VTABLE [662] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [663]
    virtual IOReturn setTX_MODE_CONFIG(apple80211_tx_mode_config *) override { XYLog("DEBUG VTABLE [663] %s\n", __FUNCTION__); return kIOReturnUnsupported; }

private:
    AirportItlwm *instance;
    ItlHalService *fHalService;

    //IO80211
    struct ieee80211_node *fNextNodeToSend;
    IOTimerEventSource *scanSource;
    bool fScanResultWrapping;
    uint32_t cachedPowersaveLevel;
    uint32_t cachedThermalIndex;
    uint32_t cachedPowerBudget;
    uint32_t cachedPrivateMacState;
    uint32_t cachedPrivateMacTimeoutSeconds;
    uint8_t cachedPrivateMacPrimary[6];
    uint8_t cachedPrivateMacSecondary[6];
    bool cachedTcpkaOffloadSupported;
    bool cachedTcpkaOffloadEnabled;
    uint64_t cachedOSFeatureFlags;
    bool cachedDhcpRenewalData;
    uint32_t cachedBatteryPowerSaveMode;
    uint32_t cachedPowerProfile;
    uint32_t cachedCurrentMcs;
    uint32_t cachedUlofdmaState;
    uint32_t cachedMimoConfig;
    uint32_t cachedFaceTimeWiFiCallingStatus;
    int32_t cachedDualPowerModePrimary;
    int32_t cachedDualPowerModeSecondary;
    bool cachedCongestionControlEnabled;
    uint32_t cachedLmtpcValue;
    uint8_t cachedLeScanParams[0x10];
    bool hasCachedLeScanParams;
    bool cachedRealTimeMode;
    uint32_t cachedQosLongRetryLimit;
    uint32_t cachedQosRtsThreshold;
    uint32_t cachedQosLifetimeAc3;
    uint32_t cachedQosLifetimeAc2;
    uint8_t cachedQosFlags;
    uint32_t cachedIPv4Address;
    uint32_t cachedIPv4Netmask;
    uint32_t cachedIPv4Reserved;
    uint32_t cachedIPv4Gateway;
    uint16_t cachedIPv4GatewayTail;
    uint32_t cachedIPv6Count;
    uint8_t cachedIPv6Addresses[10][16];
    uint8_t cachedIPv6LinkLocalAddress[16];
    bool cachedInfraEnumerated;
    uint8_t cachedUserRoamCache[0x7c];
    bool hasCachedUserRoamCache;
    uint32_t cachedPmMode;
    uint32_t cachedScanHomeAwayTime;
    uint8_t cachedWnmConfig[0x338];
    bool hasCachedWnmConfig;
    uint8_t cachedWnmOffload[0x30];
    bool hasCachedWnmOffload;
    uint8_t cachedReassocRequest[0x9c];
    bool hasCachedReassocRequest;
    uint8_t cachedLegacyRoamProfileConfig[0x60];
    bool hasCachedLegacyRoamProfileConfig;
    uint8_t cachedRoamProfileConfig[0x23c];
    bool hasCachedRoamProfileConfig;
    uint8_t cachedWclArpMode[0x14];
    bool hasCachedWclArpMode;
    uint8_t cachedBgMotionProfile[0x40];
    bool hasCachedBgMotionProfile;
    uint8_t cachedBgNetwork[0x12c0];
    bool hasCachedBgNetwork;
    uint8_t cachedBgScanConfig[8];
    bool hasCachedBgScanConfig;
    uint8_t cachedBgParams[0x20];
    bool hasCachedBgParams;
    uint8_t cachedTriggerCC[0x20];
    uint32_t cachedTriggerCCMode;
    bool hasCachedTriggerCC;

    u_int32_t current_authtype_lower;
    u_int32_t current_authtype_upper;
    bool disassocIsVoluntary;
};


#endif /* AirportItlwmSkywalkInterface_hpp */
