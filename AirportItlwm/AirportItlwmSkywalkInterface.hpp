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
    // [470] — the recovered Tahoe vtable matrix marks this as an AWDL internal
    // stub, not a shared Apple80211 producer contract.
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
    virtual IOReturn getPOWER_DEBUG_INFO(apple80211_power_debug_info *) override;
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
    virtual IOReturn getROAM_PROFILE(apple80211_roam_profile_all_bands *) override;
    // [486] — AppleBCMWLANCore does not expose a normal producer on Tahoe.
    // Newer chips trap through a private stats path, while the visible public
    // contract returns the fixed Apple failure 0xe00002e6 rather than generic
    // unsupported. Keep the slot explicit so the mismatch does not regress.
    virtual IOReturn getCHIP_COUNTER_STATS(apple80211_chip_stats *) override;
    // [487] — AppleBCMWLANCore exposes a compact 8-byte public carrier for the
    // forced_pm/guard-time path instead of leaving the selector unsupported.
    virtual IOReturn getDBG_GUARD_TIME_PARAMS(apple80211_dbg_guard_time_params *) override;
    // [488] — Broadcom-private leaky-AP diagnostics surface. This is no
    // longer carried as Q13 system-contract debt; it stays on the internal
    // diagnostics queue instead of pretending to be a missing Apple80211
    // public producer.
    virtual IOReturn getLEAKY_AP_STATS_MODE(apple80211_leaky_ap_setting *) override { XYLog("DEBUG VTABLE [488] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [489]
    virtual IOReturn getCOUNTRY_CHANNELS(apple80211_country_channel_data *) override;
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
    // [493] — AppleBCMWLANCore copies an 8-byte RSDB capability carrier from
    // core state at +0x436.
    virtual IOReturn getAWDL_RSDB_CAPS(apple80211_rsdb_capability *) override;
    // [494] — Tahoe public contract is owner-backed: missing keepalive owner ->
    // 0xe00002bc, otherwise six u32 fields at +0x4..+0x18.
    virtual IOReturn getTKO_PARAMS(apple80211_tko_params *) override;
    // [495] — missing keepalive owner -> 0xe00002bc.
    virtual IOReturn getTKO_DUMP(apple80211_tko_dump *) override;
    // [496]
    virtual IOReturn getHW_SUPPORTED_CHANNELS(apple80211_sup_channel_data *) override;
    // [497] — AppleBCMWLANCore exposes the fixed Tahoe fail 0xe00002c2 here,
    // not generic unsupported.
    virtual IOReturn getBTCOEX_PROFILE(apple80211_btcoex_profile *) override;
    // [498] — Tahoe public contract is `NULL -> 0xe00002c2`, else a single
    // dword activity carrier at +0x4.
    virtual IOReturn getBTCOEX_PROFILE_ACTIVE(apple80211_btcoex_profile_active_data *) override;
    // [499] — trap/debug diagnostics surface, not a shared Apple80211 runtime
    // producer contract. Keep it classified as internal-only instead of open
    // Q13 debt.
    virtual IOReturn getTRAP_INFO(apple80211_trap_info_data *) override { XYLog("DEBUG VTABLE [499] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [500]
    virtual IOReturn getTHERMAL_INDEX(apple80211_thermal_index_t *) override;
    // [501] — Tahoe public contract is `NULL -> 0xe00002c2`, else one dword
    // carrier at +0x4.
    virtual IOReturn getMAX_NSS_FOR_AP(apple80211_btcoex_max_nss_for_ap_data *) override;
    // [502] — Tahoe public contract is `NULL -> 0xe00002c2`, else writes
    // version=1 at +0x4.
    virtual IOReturn getBTCOEX_2G_CHAIN_DISABLE(apple80211_btcoex_2g_chain_disable *) override;
    // [503]
    virtual IOReturn getPOWER_BUDGET(apple80211_power_budget_t *) override;
    // [504]
    virtual IOReturn getOFFLOAD_TCPKA_ENABLE(apple80211_offload_tcpka_enable_t *) override;
    // [505]
    // [505] — AppleBCMWLANInfraProtocol is a direct `return 0xe00002c7;`
    // stub on Tahoe.
    virtual IOReturn getRANGING_CAPS(apple80211_ranging_capabilities_t *) override { XYLog("DEBUG VTABLE [505] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [506]
    virtual IOReturn getLQM_CONFIG(apple80211_lqm_config_t *) override;
    // [507] — AppleBCMWLANCore zero-fills the mini-dump body from +0x4 rather
    // than using a generic unsupported stub, so this remains open producer
    // work and should not be misclassified as Apple-unsupported.
    virtual IOReturn getTRAP_CRASHTRACER_MINI_DUMP(apple80211_trap_mini_dump_data *) override;
    // [508]
    virtual IOReturn getBEACON_INFO(apple80211_beacon_info_t *) override;
    // [509]
    virtual IOReturn getCHIP_POWER_RANGE(apple80211_chip_power_limit *) override { XYLog("DEBUG VTABLE [509] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [510]
    virtual IOReturn getNSS(apple80211_nss_data *) override;
    // [511]
    virtual IOReturn getHW_ADDR(apple80211_hw_mac_address *) override;
    // [512]
    virtual IOReturn getCHIP_DIAGS(appl80211_chip_diags_data *) override;
    // [513] — HP2P adjunct state belongs to the hidden proximity/datapath
    // owner path. The former Q11-C umbrella queue is closed; this slot now
    // lives in the narrower Q11-C1 HP2P/DynSAR helper subqueue.
    virtual IOReturn getHP2P_CTRL(apple80211_hp2p_ctrl *) override;
    // [514] — AppleBCMWLANCore delegates to an async blacklist getter rather
    // than a direct unsupported stub. Preserve the caller-visible raw blob.
    virtual IOReturn getBSS_BLACKLIST(bss_blacklist *) override;
    // [515] — Tahoe public contract is `NULL -> 0xe00002c2`, else four one-byte
    // chain masks.
    virtual IOReturn getTXRX_CHAIN_INFO(apple80211_txrx_chain_info *) override;
    // [516] — Tahoe public contract is a compact 10-byte MIMO status carrier
    // gated by the MIMO owner.
    virtual IOReturn getMIMO_STATUS(apple80211_mimo_status *) override;
    // [517]
    virtual IOReturn getCUR_PMK(apple80211_pmk *) override;
    // [518] — DynSAR detail is a hidden RF/policy owner surface. The former
    // Q11-C umbrella queue is closed; this slot now lives in Q11-C1 with
    // HP2P helper ownership.
    virtual IOReturn getDYNSAR_DETAIL(apple80211_dynsar_detail *) override;
    // [519]
    // [519] — AppleBCMWLANInfraProtocol returns `0xe00002c7` directly for this
    // selector on Tahoe.
    virtual IOReturn getCOUNTRY_CHANNELS_INFO(apple80211_channels_info *) override;
    // [520]
    virtual IOReturn getLQM_SUMMARY(apple80211_lqm_summary *) override;
    // [521] — hidden slow-wifi policy surface. The former Q11-C umbrella
    // queue is closed; this slot now lives in Q11-C2 low-latency / slow-wifi
    // policy ownership.
    virtual IOReturn getSLOW_WIFI_FEATURE_ENABLED(apple80211_slow_wifi_feature_enabled *) override;
    // [522]
    virtual IOReturn getTIMESYNC_INFO(apple80211_timesync_info *) override;
    // [523]
    virtual IOReturn getSENSING_DATA(apple80211_sensing_data_t *) override;
    // [524] — AppleBCMWLANCore delegates to the net adapter instead of leaving
    // the slot unsupported.
    virtual IOReturn getWCL_FW_HOT_CHANNELS(apple80211_fw_hot_channels *) override;
    // [525] — low-latency runtime state belongs to the dedicated Q11-C2
    // low-latency / slow-wifi owner path, not the former umbrella queue.
    virtual IOReturn getWCL_LOW_LATENCY_INFO(apple80211_low_latency_info *) override;
    // [526]
    virtual IOReturn getWCL_BSS_INFO(apple80211_beacon_msg *) override;
    // [527] — Tahoe public contract is `NULL -> 0xe00002bc`, else six u64
    // counters.
    virtual IOReturn getWCL_TRAFFIC_COUNTERS(apple80211_wcl_traffic_counters *) override;
    // [528] — tx-blanking status belongs to the dedicated Q11-C2
    // low-latency/tx-blanking owner path, not the former umbrella queue.
    virtual IOReturn getWCL_GET_TX_BLANKING_STATUS(uint *) override;
    // [529] — AppleBCMWLANInfraProtocol is a direct `return 0xe00002c7;`
    // stub on Tahoe.
    virtual IOReturn getHE_COUNTERS(apple80211_he_counters_ctl *) override { XYLog("DEBUG VTABLE [529] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [530]
    virtual IOReturn getWCL_CHANNELS_INFO(apple80211ChannelInfo *) override;
    // [531] — AppleBCMWLANCore copies an opaque XE blob with length at +0x4 and
    // payload at +0x6.
    virtual IOReturn getRSN_XE(apple80211_rsn_xe_data *) override;
    // [532] — Tahoe public contract is `NULL -> 0xe00002c2`, else two dwords
    // from core state.
    virtual IOReturn getSIB_COEX_STATUS(apple80211_sib_coex_status *) override;
    // [533]
    virtual IOReturn getWCL_EXTENDED_BSS_INFO(apple80211_extended_bss_info *) override;
    // [534] — Tahoe public contract is `NULL -> 0xe00002bc`, else a fixed
    // low-latency stats carrier.
    virtual IOReturn getWCL_LOW_LATENCY_INFO_STATS(apple80211_wcl_low_latency_stats *) override;
    // [535]
    virtual IOReturn getWCL_BGSCAN_CACHE_RESULT(apple80211_bgscan_cached_network_data_list *) override;
    // [536] — AppleBCMWLANInfraProtocol is a direct `return 0xe00002c7;`
    // stub on Tahoe.
    virtual IOReturn getWCL_WNM_OFFLOAD(apple80211_wcl_wnm_offload_t *) override { XYLog("DEBUG VTABLE [536] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [537] — AppleBCMWLANCore::getWIFI_NOISE_PER_ANT is a direct
    // `return 0xe00002c7;` stub on Tahoe.
    virtual IOReturn getWIFI_NOISE_PER_ANT(apple80211_noise_per_ant_t *) override { XYLog("DEBUG VTABLE [537] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [538]
    // [538] — AppleBCMWLANInfraProtocol is a direct `return 0xe00002c7;`
    // stub on Tahoe.
    virtual IOReturn getFW_CLOCK_INFO(apple80211_fw_clock_info *) override { XYLog("DEBUG VTABLE [538] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [539] — AppleBCMWLANInfraProtocol is a direct `return 0xe00002c7;`
    // stub on Tahoe.
    virtual IOReturn getTIMESYNC_STATS(apple80211_timesync_stats *) override { XYLog("DEBUG VTABLE [539] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [540] — system sleep config is owned by the Q12 sleep/wake queue, not
    // Q13 unsupported-surface debt.
    virtual IOReturn getSYSTEM_SLEEP_CONFIG(apple80211_system_sleep_config *) override;
    // [541] — AppleBCMWLANInfraProtocol is a direct `return 0xe00002c7;`
    // stub on Tahoe.
    virtual IOReturn getSMARTCCA_OPMODE(apple80211_smartcca_opmode *) override { XYLog("DEBUG VTABLE [541] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [542]
    // [542] — AppleBCMWLANInfraProtocol::getLQM_STATISTICS is a direct
    // `return 0xe00002c7;` stub on Tahoe. Keep it explicitly unsupported
    // instead of advertising a producer path that the reference driver does
    // not expose.
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
    // [546] — AppleBCMWLANCore preserves the caller-visible channel carrier,
    // rejects invalid channel ids with raw 0x16, and only then enters the
    // hidden chanspec/property path.
    virtual IOReturn setCHANNEL(apple80211_channel_data *) override;
    // [547]
    virtual IOReturn setPOWERSAVE(apple80211_powersave_data *) override;
    // [548] — AppleBCMWLANCore writes the public one-byte qtxpower transport;
    // Tahoe does not treat this as a generic unsupported setter.
    virtual IOReturn setTXPOWER(apple80211_txpower_data *) override;
    // [549] — AppleBCMWLANCore updates the public bg_rate property path.
    virtual IOReturn setRATE(apple80211_rate_data *) override;
    // [550] — AppleBCMWLANCore exposes a visible success contract here before
    // delegating into private proximity/NAN owner work.
    virtual IOReturn setIBSS_MODE(apple80211_network_data *) override;
    // [551] — Tahoe exposes a visible public contract here, but the common
    // non-AP path still resolves to `0xe00002c7`. Keep the fixed fail shape
    // instead of generic unsupported.
    virtual IOReturn setAP_MODE(apple80211_apmode_data *) override;
    // [552] — management-IE injection now sits in Q11-A1: the legacy owner
    // lives on the non-Tahoe controller branch (`AirportItlwm.hpp`), so the
    // former Q11-A umbrella queue is closed but this exact owner mismatch
    // remains isolated here.
    virtual IOReturn setIE(apple80211_ie_data *) override;
    // [553] — wake-on-wireless test/debug surface. Classified to Q12/internal
    // test coverage, not Q13.
    virtual IOReturn setWOW_TEST(apple80211_wow_test_data *) override;
    // [554]
    virtual IOReturn setCLEAR_PMKSA_CACHE(void *) override;
    // [555]
    // [555] — Tahoe exposes role-dependent public failures here before the
    // private proximity/AWDL/NAN owner path takes over.
    virtual IOReturn setVIRTUAL_IF_CREATE(apple80211_virt_if_create_data *) override;
    // [556] — capability programming surface now sits in Q11-B1 capability
    // programming ownership after closing the broader Q11-B umbrella queue.
    virtual IOReturn setHT_CAPABILITY(apple80211_ht_capability *) override;
    // [557]
    virtual IOReturn setOFFLOAD_ARP(apple80211_offload_arp_data *) override;
    // [558] — NDP offload setup now sits in Q11-C3 nearby/NDP ownership after
    // closing the broader Q11-C umbrella queue.
    virtual IOReturn setOFFLOAD_NDP(apple80211_offload_ndp_data *) override;
    // [559] — AppleBCMWLANCore rejects NULL with 0xe00002c2 and otherwise
    // delegates into the GAS owner path.
    virtual IOReturn setGAS_REQ(apple80211_gas_query_t *) override;
    // [560] — capability programming surface now sits in Q11-B1 capability
    // programming ownership after closing the broader Q11-B umbrella queue.
    virtual IOReturn setVHT_CAPABILITY(apple80211_vht_capability *) override;
    // [561] — AppleBCMWLANInfraProtocol is a direct `return 0xe00002c7;`
    // stub on Tahoe. This slot was already counted out of the open mismatch
    // queue in the audit; keep the header on the same explicit unsupported
    // contract instead of treating it like a missing producer.
    virtual IOReturn setROAM_PROFILE(apple80211_roam_profile_all_bands *) override { XYLog("DEBUG VTABLE [561] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [562] — AppleBCMWLANCore consumes a compact 8-byte carrier and writes it
    // through the "forced_pm" property path.
    virtual IOReturn setDBG_GUARD_TIME_PARAMS(apple80211_dbg_guard_time_params *) override;
    // [563] — Broadcom-private leaky-AP diagnostics setter. Reclassified to
    // internal diagnostics coverage rather than Q13.
    virtual IOReturn setLEAKY_AP_STATS_MODE(apple80211_leaky_ap_setting *) override { XYLog("DEBUG VTABLE [563] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [564] — AppleBCMWLANCore preserves timeout/MAC state and returns the
    // raw Tahoe code `0x16`, not generic unsupported.
    virtual IOReturn setPRIVATE_MAC(apple80211_private_mac_data *) override;
    // [565] — Apple routes this selector into a trap/debug path and the only
    // caller-visible non-trap contract is the raw Tahoe fail 0x16.
    virtual IOReturn setRESET_CHIP(apple80211_reset_command *) override;
    // [566] — AppleBCMWLANInfraProtocol exposes 0x16 / 0x13 / owner-result,
    // not generic unsupported.
    virtual IOReturn setCRASH(apple80211_crash_command *) override;
    // [567] — Tahoe exposes a bad-argument gate before switching into a hidden
    // ranging owner path.
    virtual IOReturn setRANGING_ENABLE(apple80211_ranging_enable_request_t *) override;
    // [568] — Tahoe exposes a bad-argument gate before switching into a hidden
    // ranging start owner path.
    virtual IOReturn setRANGING_START(apple80211_ranging_start_request_t *) override;
    // [569] — feature-gated ranging/authentication path now sits in Q11-C3
    // nearby/ranging ownership after closing the broader Q11-C umbrella queue.
    virtual IOReturn setRANGING_AUTHENTICATE(apple80211_ranging_authenticate_request_t *) override;
    // [570] — AppleBCMWLANCore copies six public dwords into the keepalive
    // owner when it exists, otherwise returns 0xe00002bc.
    virtual IOReturn setTKO_PARAMS(apple80211_tko_params *) override;
    // [571] — RF coexistence programming now sits in Q11-B2 coexistence owner
    // work after closing the broader Q11-B umbrella queue.
    virtual IOReturn setBTCOEX_PROFILE(apple80211_btcoex_profile *) override;
    // [572] — RF coexistence programming now sits in Q11-B2 coexistence owner
    // work after closing the broader Q11-B umbrella queue.
    virtual IOReturn setBTCOEX_PROFILE_ACTIVE(apple80211_btcoex_profile_active_data *) override;
    // [573] — Tahoe validates the public carrier and returns the fixed fail
    // `0xe00002bc` from the visible path rather than generic unsupported.
    virtual IOReturn setTHERMAL_INDEX(apple80211_thermal_index_t *) override;
    // [574] — RF coexistence/radio programming now sits in Q11-B2 after
    // closing the broader Q11-B umbrella queue.
    virtual IOReturn setBTCOEX_2G_CHAIN_DISABLE(apple80211_btcoex_2g_chain_disable *) override;
    // [575] — power-budget policy belongs to Q12 power/sleep work, not Q13.
    virtual IOReturn setPOWER_BUDGET(apple80211_power_budget_t *) override;
    // [576]
    virtual IOReturn setOFFLOAD_TCPKA_ENABLE(apple80211_offload_tcpka_enable_t *) override;
    // [577]
    virtual IOReturn setLQM_CONFIG(apple80211_lqm_config_t *) override;
    // [578] — AppleBCMWLANCore consumes the first dword as the public carrier.
    virtual IOReturn setDYNAMIC_RSSI_WINDOW_CONFIG(apple80211_dynamic_rssi_window_config *) override;
    // [579] — host USB power-state notification belongs to Q12 sleep/power
    // orchestration, not Q13.
    virtual IOReturn setUSB_HOST_NOTIFICATION(apple80211_usb_host_notification_data *) override;
    // [580] — decompile shows an internal trap-only selector, not a normal
    // public producer.
    virtual IOReturn setHP2P_CTRL(apple80211_hp2p_ctrl *) override;
    // [581]
    // Tahoe AppleBCMWLANCore::setBSS_BLACKLIST consumes an opaque public blob and
    // preserves it in core state before dispatching helper work; it is not a
    // direct unsupported slot.
    virtual IOReturn setBSS_BLACKLIST(bss_blacklist *) override;
    // [582] — AppleBCMWLANCore dispatches this selector through a gated
    // setPropertyIoctl callback path.
    virtual IOReturn setSET_PROPERTY(apple80211_set_property_unserialized_data *) override;
    // [583]
    // [583] — AppleBCMWLANInfraProtocol is a direct `return 0xe00002c7;`
    // stub on Tahoe.
    virtual IOReturn setROAM_CACHE_UPDATE(apple80211_roam_cache_data *) override { XYLog("DEBUG VTABLE [583] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [584]
    virtual IOReturn setPM_MODE(apple80211_pm_mode *) override;
    // [585]
    // [585] — AppleBCMWLANInfraProtocol is a direct `return 0xe00002c7;`
    // stub on Tahoe.
    virtual IOReturn setSET_WIFI_ASSERTION_STATE(apple80211_wifi_assertion_data *) override { XYLog("DEBUG VTABLE [585] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [586] — AppleBCMWLANCore consumes the first dword as an enable/disable
    // state carrier before QoS-owner gating.
    virtual IOReturn setREALTIME_QOS_MSCS(apple80211_state_data *) override;
    // [587] — AppleBCMWLANSensingAdapter routes this selector into an internal
    // trap-only control path.
    virtual IOReturn setSENSING_ENABLE(apple80211_sensing_enable_t *) override;
    // [588] — AppleBCMWLANSensingAdapter is feature-gated here and does not
    // expose a generic unsupported contract.
    virtual IOReturn setSENSING_DISABLE(apple80211_sensing_disable_t *) override;
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
    // [606] — AppleBCMWLANCore forwards the opaque XE blob starting at +0x6
    // with the public length at +0x4.
    virtual IOReturn setRSN_XE(apple80211_rsn_xe_data *) override;
    // [607]
    // [607] — AppleBCMWLANInfraProtocol is a direct `return 0xe00002c7;`
    // stub on Tahoe.
    virtual IOReturn setMWS_ACCESSORY_POWER_LIMIT_WIFI_ENH(apple80211_mws_accessory_power_limit *) override { XYLog("DEBUG VTABLE [607] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [608]
    virtual IOReturn setWCL_ULOFDMA_STATE(apple80211_wcl_ulofdma_state *) override;
    // [609] — action-frame injection now sits in Q11-A2 net-adapter frame
    // injection ownership after closing the broader Q11-A umbrella queue.
    virtual IOReturn setWCL_ACTION_FRAME(apple80211_wcl_action_frame *) override;
    // [610]
    // [610] — AppleBCMWLANCore delegates to GASAdapter with no payload.
    virtual IOReturn setGAS_ABORT(void *) override;
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
    // [622] — tx-power-cap bypass now sits in Q11-B3 tx-power policy
    // ownership after closing the broader Q11-B umbrella queue.
    virtual IOReturn setBYPASS_TX_POWER_CAP(apple80211_bypass_tx_power_cap *) override;
    // [623]
    virtual IOReturn setFACETIME_WIFICALLING_PARAMS(apple80211_facetime_wificalling_params *) override;
    // [624] — AppleBCMWLAN: stores IPv4 addr/mask/gw, notifies InfraInterface
    virtual IOReturn setIPV4_PARAMS(apple80211_ipv4_params *data) override;
    // [625] — AppleBCMWLAN: delegates to WnmAdapter.
    virtual IOReturn setWCL_WNM_OPS(apple80211_wcl_wnm_config_t *) override;
    // [626] — AppleBCMWLAN: delegates to WnmAdapter.
    virtual IOReturn setWCL_WNM_OFFLOAD(apple80211_wcl_wnm_offload_t *) override;
    // [627] — Tahoe public contract is `NULL -> 0xe00002bc`, else success.
    virtual IOReturn setWCL_LIMITED_AGGREGATION(apple80211_limited_aggregation_config *) override;
    // [628] — Tahoe public contract is `NULL -> 0xe00002bc`, else preserve the
    // compact 4-byte caller-visible carrier.
    virtual IOReturn setWCL_BCN_MUTE_CONFIG(apple80211_bcn_mute_config *) override;
    // [629] — Tahoe public contract is `NULL -> 0xe00002bc`, else preserve the
    // first dword caller-visible carrier.
    virtual IOReturn setEAP_FILTER_CONFIG(apple80211_eap_filter_config *) override;
    // [630]
    // [630] — AppleBCMWLANInfraProtocol is a direct `return 0xe00002c7;`
    // stub on Tahoe.
    virtual IOReturn setWOW_LOW_POWER_MODE(apple80211_wow_low_power_mode *) override { XYLog("DEBUG VTABLE [630] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [631]
    virtual IOReturn setDUAL_POWER_MODE(apple80211_dual_power_mode_params *) override;
    // [632] — fast-lane steering now sits in Q11-C3 traffic-policy ownership
    // after closing the broader Q11-C umbrella queue.
    virtual IOReturn setWCL_UPDATE_FAST_LANE(apple80211_fastlane *) override { XYLog("DEBUG VTABLE [632] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [633] — AppleBCMWLANCore consumes a larger opaque sleep-management blob
    // and feeds it into the power-state adapter.
    virtual IOReturn setWCL_ASSOCIATED_SLEEP(apple80211_associated_sleep_config *) override;
    // [634]
    virtual IOReturn setCONGESTION_CTRL_IND(apple80211_congestion_control_indication *) override;
    // [635]
    // [635] — AppleBCMWLANInfraProtocol is a direct `return 0xe00002c7;`
    // stub on Tahoe.
    virtual IOReturn setSTAND_ALONE_MODE_STATE(apple80211_standalone_state *) override { XYLog("DEBUG VTABLE [635] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [636] — AppleBCMWLAN: stores up to 10 IPv6 addresses, notifies InfraInterface
    virtual IOReturn setIPV6_PARAMS(apple80211_ipv6_params *data) override;
    // [637] — AppleBCMWLAN: validates param, returns success (minimal stub)
    virtual IOReturn setINFRA_ENUMERATED(apple80211_infra_enumerated *data) override;
    // [638]
    virtual IOReturn setLMTPC_CONFIG(apple80211_lmtpc_config *) override;
    // [639] — traffic-engine parameters now sit in Q11-C3 traffic-policy
    // ownership after closing the broader Q11-C umbrella queue.
    virtual IOReturn setTRAFFIC_ENG_PARAMS(apple80211_traffic_eng_params *) override;
    // [640]
    virtual IOReturn setLE_SCAN_PARAM(apple80211_le_scan_params *) override;
    // [641]
    // [641] — AppleBCMWLANInfraProtocol is a direct `return 0xe00002c7;`
    // stub on Tahoe.
    virtual IOReturn setTIMESYNC_GPIO(apple80211_timesync_gpio *) override { XYLog("DEBUG VTABLE [641] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [642] — AppleBCMWLANInfraProtocol::setHOST_CLOCK_INFO is a direct
    // `return 0xe00002c7;` stub on Tahoe. Q12 therefore closes on the same
    // explicit fixed fail-contract instead of treating this as a missing owner.
    virtual IOReturn setHOST_CLOCK_INFO(apple80211_host_clock_info *) override { XYLog("DEBUG VTABLE [642] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [643] — AppleBCMWLANInfraProtocol is a direct `return 0xe00002c7;`
    // stub on Tahoe.
    virtual IOReturn setFW_CLOCK_SOURCE(apple80211_fw_clock_source *) override { XYLog("DEBUG VTABLE [643] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [644] — AppleBCMWLANInfraProtocol is a direct `return 0xe00002c7;`
    // stub on Tahoe.
    virtual IOReturn setTIMESYNC_TX_POLICY(apple80211_timesync_tx_policy *) override { XYLog("DEBUG VTABLE [644] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [645] — AppleBCMWLANInfraProtocol is a direct `return 0xe00002c7;`
    // stub on Tahoe.
    virtual IOReturn setTIMESYNC_RX_POLICY(apple80211_timesync_rx_policy *) override { XYLog("DEBUG VTABLE [645] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [646] — AppleBCMWLANInfraProtocol is a direct `return 0xe00002c7;`
    // stub on Tahoe.
    virtual IOReturn setTIMESTAMPING_EN(apple80211_timestamping_en *) override { XYLog("DEBUG VTABLE [646] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [647] — AppleBCMWLANCore forwards the SOI sub-blob starting at +0x1c
    // into the power-state adapter and returns success.
    virtual IOReturn setWCL_SOI_CONFIG(appl80211_sleep_on_inactivity_config *) override;
    // [648]
    // [648] — AppleBCMWLANInfraProtocol is a direct `return 0xe00002c7;`
    // stub on Tahoe.
    virtual IOReturn setMWS_TIME_SHARING_WIFI_ENH(apple80211_mws_time_sharing *) override { XYLog("DEBUG VTABLE [648] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [649] — AppleBCMWLANCore copies a 9-dword bitmap into cached core state
    // and then fans out to a Broadcom-private notifier.
    virtual IOReturn setMWS_WIFI_TYPE_7_BITMAP_WIFI_ENH(apple80211_mws_wifi_channel_bitmap *) override;
    // [650] — AppleBCMWLANCore copies a 9-dword bitmap into cached core state
    // and then fans out to a Broadcom-private notifier.
    virtual IOReturn setMWS_COEX_BITMAP_WIFI_ENH(apple80211_mws_wifi_channel_bitmap *) override;
    // [651] — AppleBCMWLANCore copies a 9-dword bitmap into cached core state
    // and then fans out to a Broadcom-private notifier.
    virtual IOReturn setMWS_DISABLE_OCL_BITMAP_WIFI_ENH(apple80211_mws_wifi_channel_bitmap *) override;
    // [652] — AppleBCMWLANCore copies a 10-dword RFEM carrier into cached
    // core state and then fans out to a Broadcom-private notifier.
    virtual IOReturn setMWS_RFEM_CONFIG_WIFI_ENH(apple80211_mws_rfem_config *) override;
    // [653] — AppleBCMWLANCore copies a 9-dword bitmap into cached core state
    // and then fans out to a Broadcom-private notifier.
    virtual IOReturn setMWS_ASSOC_PROTECTION_BITMAP_WIFI_ENH(apple80211_mws_wifi_channel_bitmap *) override;
    // [654] — AppleBCMWLANCore copies a 10-dword scan-frequency carrier into
    // cached core state and then fans out to a Broadcom-private notifier.
    virtual IOReturn setMWS_SCAN_FREQ_WIFI_ENH(apple80211_mws_scan_freq *) override;
    // [655] — AppleBCMWLANCore reorders four dwords from the public carrier
    // into cached core state before invoking a Broadcom-private notifier.
    virtual IOReturn setMWS_SCAN_FREQ_MODE_WIFI_ENH(apple80211_mws_scan_freq_mode *) override;
    // [656] — AppleBCMWLANCore iterates an opaque 0x28-byte condition record
    // list and replays the same notifier for each entry.
    virtual IOReturn setMWS_CONDITION_ID_BITMAP_WIFI_ENH(apple80211_mws_condition_id_config *) override;
    // [657] — AppleBCMWLANCore copies nine 16-bit antenna selectors into
    // cached core state and then fans out to a Broadcom-private notifier.
    virtual IOReturn setMWS_ANTENNA_SELECTION_WIFI_ENH(apple80211_mws_antenna_selection *) override;
    // [658] — AppleBCMWLANCore only succeeds when a NearbyDeviceDiscoveryAdapter
    // owner exists at +0x7c90; otherwise the public Tahoe contract is the
    // fixed feature-gated fail `0xe00002c7`.
    virtual IOReturn setNDD_REQ(apple80211_ndd_data *) override;
    // [659] — decompile shows an internal trap-only selector, not a public
    // carrier producer.
    virtual IOReturn setDBRG_ENTROPY(apple80211_drbg_entropy *) override;
    // [660]
    // [660] — AppleBCMWLANInfraProtocol is a direct `return 0xe00002c7;`
    // stub on Tahoe.
    virtual IOReturn setSDB_ENABLE(apple80211_sdb_enable *) override { XYLog("DEBUG VTABLE [660] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [661] — AppleBCMWLANInfraProtocol is a direct `return 0xe00002c7;`
    // stub on Tahoe.
    virtual IOReturn setBTCOEX_EXT_PROFILE(apple80211_btcoex_ext_profile *) override { XYLog("DEBUG VTABLE [661] %s\n", __FUNCTION__); return kIOReturnUnsupported; }
    // [662]
    // [662] — AppleBCMWLANCore consumes a single dword eligibility carrier and
    // then optionally reconfigures EDCA.
    virtual IOReturn setOS_ELIGIBILITY(apple80211_os_eligibility *) override;
    // [663] — AppleBCMWLANInfraProtocol is a direct `return 0xe00002c7;`
    // stub on Tahoe.
    virtual IOReturn setTX_MODE_CONFIG(apple80211_tx_mode_config *) override { XYLog("DEBUG VTABLE [663] %s\n", __FUNCTION__); return kIOReturnUnsupported; }

private:
    AirportItlwm *instance;
    ItlHalService *fHalService;

    //IO80211
    struct ieee80211_node *fNextNodeToSend;
    IOTimerEventSource *scanSource;
    bool fScanResultWrapping;
    uint32_t cachedPowersaveLevel;
    apple80211_channel_data cachedRequestedChannel;
    bool hasCachedRequestedChannel;
    uint32_t cachedBgRate;
    bool hasCachedBgRate;
    uint32_t cachedThermalIndex;
    uint32_t cachedPowerBudget;
    uint32_t cachedDynsarHeader0[4];
    uint32_t cachedDynsarHeader1[4];
    uint8_t cachedDynsarPayload[4][0x2d00];
    bool cachedSlowWifiFeatureEnabled;
    uint8_t cachedLowLatencyEnabled;
    uint8_t cachedLowLatencyPowerSave;
    uint16_t cachedLowLatencyWindow;
    bool cachedTxBlankingStatus;
    uint32_t cachedPrivateMacState;
    uint32_t cachedPrivateMacTimeoutSeconds;
    uint8_t cachedPrivateMacPrimary[6];
    uint8_t cachedPrivateMacSecondary[6];
    bool cachedTcpkaOffloadSupported;
    bool cachedTcpkaOffloadEnabled;
    uint32_t cachedWowTestMode;
    uint64_t cachedOSFeatureFlags;
    bool cachedDhcpRenewalData;
    uint32_t cachedBatteryPowerSaveMode;
    uint32_t cachedPowerProfile;
    apple80211_ht_capability cachedHtCapability;
    bool hasCachedHtCapability;
    uint32_t cachedCurrentMcs;
    uint16_t cachedIbssMode;
    uint16_t cachedIbssAuthLower;
    uint16_t cachedIbssAuthUpper;
    apple80211_channel cachedIbssChannel;
    uint32_t cachedIbssSsidLen;
    uint8_t cachedIbssSsid[APPLE80211_MAX_SSID_LEN];
    bool hasCachedIbssNetwork;
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
    apple80211_lqm_config_t cachedLqmConfig;
    bool hasCachedLqmConfig;
    apple80211_vht_capability cachedVhtCapability;
    bool hasCachedVhtCapability;
    uint32_t cachedScanHomeAwayTime;
    bool cachedGasQueryIssued;
    bool cachedSetPropertyIoctlSeen;
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
    uint32_t cachedUsbHostNotificationSeq;
    uint32_t cachedUsbHostNotificationChange;
    uint32_t cachedUsbHostNotificationPresent;
    uint32_t cachedApMode;
    uint8_t cachedAssocIe[2048];
    uint32_t cachedAssocIeLen;
    bool hasCachedAssocIe;
    uint8_t cachedVendorIe[2048];
    uint32_t cachedVendorIeLen;
    uint32_t cachedVendorIeFlags;
    bool hasCachedVendorIe;
    uint8_t cachedBtcoexProfile[0x38];
    bool hasCachedBtcoexProfile;
    uint32_t cachedBtcoexProfileActive;
    uint16_t cachedBtcoex2GChainDisable;
    uint8_t cachedLastActionFrame[0x200];
    uint16_t cachedLastActionFrameLen;
    uint32_t cachedLastActionFrameChannel;
    uint8_t cachedLastActionFrameCategory;
    bool hasCachedLastActionFrame;
    uint8_t cachedDbgGuardTimeParams[8];
    bool hasCachedDbgGuardTimeParams;
    uint32_t cachedDynamicRssiWindowConfig;
    uint32_t cachedRealTimeQosMscs;
    uint8_t cachedBcnMuteConfig[4];
    bool hasCachedBcnMuteConfig;
    uint32_t cachedEapFilterConfig;
    bool cachedBypassTxPowerCapEnabled;
    uint8_t cachedAssociatedSleepConfig[0x58];
    bool hasCachedAssociatedSleepConfig;
    uint8_t cachedSoiConfig[0x40];
    bool hasCachedSoiConfig;
    uint32_t cachedOsEligibility;
    uint8_t cachedBssBlacklist[0x2b];
    bool hasCachedBssBlacklist;
    uint16_t cachedRsnXeLength;
    uint8_t cachedRsnXe[0x100];
    bool hasCachedRsnXe;
    uint64_t cachedAwdlRsdbCaps;
    uint32_t cachedTkoParams[6];
    bool hasCachedTkoParams;
    uint32_t cachedMwsWifiType7Bitmap[9];
    uint32_t cachedMwsCoexBitmap[9];
    uint32_t cachedMwsDisableOclBitmap[9];
    uint32_t cachedMwsRfemConfig[10];
    uint32_t cachedMwsAssocProtectionBitmap[9];
    uint32_t cachedMwsScanFreq[10];
    uint32_t cachedMwsScanFreqMode[4];
    uint8_t cachedMwsConditionIdConfig[0x168];
    uint8_t cachedMwsConditionIdCount;
    bool hasCachedMwsConditionIdConfig;
    uint16_t cachedMwsAntennaSelection[9];

    u_int32_t current_authtype_lower;
    u_int32_t current_authtype_upper;
    bool disassocIsVoluntary;
};


#endif /* AirportItlwmSkywalkInterface_hpp */
