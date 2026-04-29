//
//  IO80211SapProtocol.h
//  itlwm
//
//  Tahoe SAP/APSTA vtable contract recovered from IO80211Family and
//  AppleBCMWLANCore. This header intentionally records the contract without
//  defining a C++ base class until the full SAP slot map is implemented.
//

#ifndef IO80211SapProtocol_h
#define IO80211SapProtocol_h

#include "IO80211SkywalkInterface.h"
#include "apple80211_var.h"

class IO80211SapProtocol;

struct apple80211_host_ap_mode_hidden_t;
struct apple80211_mis_max_sta;
struct apple80211_peer_cache_control;
struct apple80211_rsn_conf_data;
struct apple80211_softap_csa_params;
struct apple80211_softap_extended_capabilities_info;
struct apple80211_softap_params;
struct apple80211_softap_stats;
struct apple80211_softap_wifi_network_info;
struct apple80211_sta_authorize_data;
struct apple80211_sta_disassoc_data;
struct apple80211_sta_ie_data;
struct apple80211_sta_stats_data;

#if __IO80211_TARGET >= __MAC_26_0

enum {
    kIO80211VtableSlotByteStride = 8,

    kIO80211SapProtocolBaseVtableSlotFirst = 280,
    kIO80211SapProtocolBaseVtableSlotLast = 519,
    kIO80211SapProtocolVtableSlotFirst = 481,
    kIO80211SapProtocolVtableSlotReset = 481,
    kIO80211SapProtocolVtableSlotVirtualForwardPacket = 483,
    kIO80211SapProtocolVtableSlotSkywalkForwardPacket = 465,
    kIO80211SapProtocolVtableSlotForwardPacket = 465,
    kIO80211SapProtocolVtableSlotSetMacAddress = 488,

    kIO80211SapProtocolVtableSlotGetSSID = 505,
    kIO80211SapProtocolVtableSlotGetCHANNEL = 506,
    kIO80211SapProtocolVtableSlotGetSTATE = 507,
    kIO80211SapProtocolVtableSlotGetOP_MODE = 508,
    kIO80211SapProtocolVtableSlotGetSTATION_LIST = 509,
    kIO80211SapProtocolVtableSlotGetSTA_IE_LIST = 510,
    kIO80211SapProtocolVtableSlotGetKEY_RSC = 511,
    kIO80211SapProtocolVtableSlotGetSTA_STATS = 512,
    kIO80211SapProtocolVtableSlotGetPEER_CACHE_MAXIMUM_SIZE = 513,
    kIO80211SapProtocolVtableSlotGetHOST_AP_MODE_HIDDEN = 514,
    kIO80211SapProtocolVtableSlotGetSOFTAP_PARAMS = 515,
    kIO80211SapProtocolVtableSlotGetSOFTAP_STATS = 516,

    kIO80211SapProtocolVtableSlotSetSSID = 517,
    kIO80211SapProtocolVtableSlotSetCIPHER_KEY = 518,
    kIO80211SapProtocolVtableSlotSetCHANNEL = 519,
    kIO80211SapProtocolVtableSlotSetHOST_AP_MODE = 520,
    kIO80211SapProtocolVtableSlotSetSTA_AUTHORIZE = 521,
    kIO80211SapProtocolVtableSlotSetSTA_DISASSOCIATE = 522,
    kIO80211SapProtocolVtableSlotSetSTA_DEAUTH = 523,
    kIO80211SapProtocolVtableSlotSetRSN_CONF = 524,
    kIO80211SapProtocolVtableSlotSetPEER_CACHE_CONTROL = 525,
    kIO80211SapProtocolVtableSlotSetHOST_AP_MODE_HIDDEN = 526,
    kIO80211SapProtocolVtableSlotSetSOFTAP_PARAMS = 527,
    kIO80211SapProtocolVtableSlotSetSOFTAP_TRIGGER_CSA = 528,
    kIO80211SapProtocolVtableSlotSetSOFTAP_WIFI_NETWORK_INFO_IE = 529,
    kIO80211SapProtocolVtableSlotSetSOFTAP_EXTENDED_CAPABILITIES_IE = 530,
    kIO80211SapProtocolVtableSlotSetMIS_MAX_STA = 531,

    kAppleBCMWLANAPSTAVtableSlotFirst = 280,
    kAppleBCMWLANAPSTAVtableSlotLastRecovered = 531,
    kAppleBCMWLANAPSTAVtableSlotForwardPacket = 465,
    kAppleBCMWLANAPSTAVtableSlotReset = 481,
    kAppleBCMWLANAPSTAVtableSlotSetMacAddress = 488,
    kAppleBCMWLANAPSTAVtableSlotGetSSID = 505,
    kAppleBCMWLANAPSTAVtableSlotGetCHANNEL = 506,
    kAppleBCMWLANAPSTAVtableSlotGetSTATE = 507,
    kAppleBCMWLANAPSTAVtableSlotGetOP_MODE = 508,
    kAppleBCMWLANAPSTAVtableSlotGetSTATION_LIST = 509,
    kAppleBCMWLANAPSTAVtableSlotGetSTA_IE_LIST = 510,
    kAppleBCMWLANAPSTAVtableSlotGetKEY_RSC = 511,
    kAppleBCMWLANAPSTAVtableSlotGetSTA_STATS = 512,
    kAppleBCMWLANAPSTAVtableSlotGetPEER_CACHE_MAXIMUM_SIZE = 513,
    kAppleBCMWLANAPSTAVtableSlotGetHOST_AP_MODE_HIDDEN = 514,
    kAppleBCMWLANAPSTAVtableSlotGetSOFTAP_PARAMS = 515,
    kAppleBCMWLANAPSTAVtableSlotGetSOFTAP_STATS = 516,
    kAppleBCMWLANAPSTAVtableSlotSetSSID = 517,
    kAppleBCMWLANAPSTAVtableSlotSetCIPHER_KEY = 518,
    kAppleBCMWLANAPSTAVtableSlotSetCHANNEL = 519,
    kAppleBCMWLANAPSTAVtableSlotSetHOST_AP_MODE = 520,
    kAppleBCMWLANAPSTAVtableSlotSetSTA_AUTHORIZE = 521,
    kAppleBCMWLANAPSTAVtableSlotSetSTA_DISASSOCIATE = 522,
    kAppleBCMWLANAPSTAVtableSlotSetSTA_DEAUTH = 523,
    kAppleBCMWLANAPSTAVtableSlotSetRSN_CONF = 524,
    kAppleBCMWLANAPSTAVtableSlotSetPEER_CACHE_CONTROL = 525,
    kAppleBCMWLANAPSTAVtableSlotSetHOST_AP_MODE_HIDDEN = 526,
    kAppleBCMWLANAPSTAVtableSlotSetSOFTAP_PARAMS = 527,
    kAppleBCMWLANAPSTAVtableSlotSetSOFTAP_TRIGGER_CSA = 528,
    kAppleBCMWLANAPSTAVtableSlotSetSOFTAP_WIFI_NETWORK_INFO_IE = 529,
    kAppleBCMWLANAPSTAVtableSlotSetSOFTAP_EXTENDED_CAPABILITIES_IE = 530,
    kAppleBCMWLANAPSTAVtableSlotSetMIS_MAX_STA = 531,

    kIO80211SapProtocolVtableByteOffsetFirst = 0x0f08,
    kIO80211SapProtocolVtableByteOffsetVirtualForwardPacket = 0x0f18,
    kAppleBCMWLANAPSTAVtableByteOffsetForwardPacket = 0x0e88,
    kAppleBCMWLANAPSTAVtableByteOffsetReset = 0x0f08,
    kAppleBCMWLANAPSTAVtableByteOffsetSetMacAddress = 0x0f40,
    kAppleBCMWLANAPSTAVtableByteOffsetGetSSID = 0x0fc8,
    kAppleBCMWLANAPSTAVtableByteOffsetGetCHANNEL = 0x0fd0,
    kAppleBCMWLANAPSTAVtableByteOffsetGetSTATE = 0x0fd8,
    kAppleBCMWLANAPSTAVtableByteOffsetGetOP_MODE = 0x0fe0,
    kAppleBCMWLANAPSTAVtableByteOffsetGetSTATION_LIST = 0x0fe8,
    kAppleBCMWLANAPSTAVtableByteOffsetGetSTA_IE_LIST = 0x0ff0,
    kAppleBCMWLANAPSTAVtableByteOffsetGetKEY_RSC = 0x0ff8,
    kAppleBCMWLANAPSTAVtableByteOffsetGetSTA_STATS = 0x1000,
    kAppleBCMWLANAPSTAVtableByteOffsetGetPEER_CACHE_MAXIMUM_SIZE = 0x1008,
    kAppleBCMWLANAPSTAVtableByteOffsetGetHOST_AP_MODE_HIDDEN = 0x1010,
    kAppleBCMWLANAPSTAVtableByteOffsetGetSOFTAP_PARAMS = 0x1018,
    kAppleBCMWLANAPSTAVtableByteOffsetGetSOFTAP_STATS = 0x1020,
    kAppleBCMWLANAPSTAVtableByteOffsetSetSSID = 0x1028,
    kAppleBCMWLANAPSTAVtableByteOffsetSetCIPHER_KEY = 0x1030,
    kAppleBCMWLANAPSTAVtableByteOffsetSetCHANNEL = 0x1038,
    kAppleBCMWLANAPSTAVtableByteOffsetSetHOST_AP_MODE = 0x1040,
    kAppleBCMWLANAPSTAVtableByteOffsetSetSTA_AUTHORIZE = 0x1048,
    kAppleBCMWLANAPSTAVtableByteOffsetSetSTA_DISASSOCIATE = 0x1050,
    kAppleBCMWLANAPSTAVtableByteOffsetSetSTA_DEAUTH = 0x1058,
    kAppleBCMWLANAPSTAVtableByteOffsetSetRSN_CONF = 0x1060,
    kAppleBCMWLANAPSTAVtableByteOffsetSetPEER_CACHE_CONTROL = 0x1068,
    kAppleBCMWLANAPSTAVtableByteOffsetSetHOST_AP_MODE_HIDDEN = 0x1070,
    kAppleBCMWLANAPSTAVtableByteOffsetSetSOFTAP_PARAMS = 0x1078,
    kAppleBCMWLANAPSTAVtableByteOffsetSetSOFTAP_TRIGGER_CSA = 0x1080,
    kAppleBCMWLANAPSTAVtableByteOffsetSetSOFTAP_WIFI_NETWORK_INFO_IE = 0x1088,
    kAppleBCMWLANAPSTAVtableByteOffsetSetSOFTAP_EXTENDED_CAPABILITIES_IE = 0x1090,
    kAppleBCMWLANAPSTAVtableByteOffsetSetMIS_MAX_STA = 0x1098,
};

typedef void (*IO80211SapResetSlot)(IO80211SapProtocol *);
typedef void (*IO80211SapForwardPacketSlot)(IO80211SapProtocol *, IO80211NetworkPacket *);
typedef void (*IO80211SapSetMacAddressSlot)(IO80211SapProtocol *, ether_addr &);

typedef IOReturn (*IO80211SapGetSSIDSlot)(IO80211SapProtocol *, struct apple80211_ssid_data *);
typedef IOReturn (*IO80211SapGetCHANNELSlot)(IO80211SapProtocol *, struct apple80211_channel_data *);
typedef IOReturn (*IO80211SapGetSTATESlot)(IO80211SapProtocol *, struct apple80211_state_data *);
typedef IOReturn (*IO80211SapGetOP_MODESlot)(IO80211SapProtocol *, struct apple80211_opmode_data *);
typedef IOReturn (*IO80211SapGetSTATION_LISTSlot)(IO80211SapProtocol *, struct apple80211_sta_data *);
typedef IOReturn (*IO80211SapGetSTA_IE_LISTSlot)(IO80211SapProtocol *, struct apple80211_sta_ie_data *);
typedef IOReturn (*IO80211SapGetKEY_RSCSlot)(IO80211SapProtocol *, struct apple80211_key *);
typedef IOReturn (*IO80211SapGetSTA_STATSSlot)(IO80211SapProtocol *, struct apple80211_sta_stats_data *);
typedef IOReturn (*IO80211SapGetPEER_CACHE_MAXIMUM_SIZESlot)(IO80211SapProtocol *, struct apple80211_peer_cache_maximum_size *);
typedef IOReturn (*IO80211SapGetHOST_AP_MODE_HIDDENSlot)(IO80211SapProtocol *, struct apple80211_host_ap_mode_hidden_t *);
typedef IOReturn (*IO80211SapGetSOFTAP_PARAMSSlot)(IO80211SapProtocol *, struct apple80211_softap_params *);
typedef IOReturn (*IO80211SapGetSOFTAP_STATSSlot)(IO80211SapProtocol *, struct apple80211_softap_stats *);

typedef IOReturn (*IO80211SapSetSSIDSlot)(IO80211SapProtocol *, struct apple80211_ssid_data *);
typedef IOReturn (*IO80211SapSetCIPHER_KEYSlot)(IO80211SapProtocol *, struct apple80211_key *);
typedef IOReturn (*IO80211SapSetCHANNELSlot)(IO80211SapProtocol *, struct apple80211_channel_data *);
typedef IOReturn (*IO80211SapSetHOST_AP_MODESlot)(IO80211SapProtocol *, struct apple80211_network_data *);
typedef IOReturn (*IO80211SapSetSTA_AUTHORIZESlot)(IO80211SapProtocol *, struct apple80211_sta_authorize_data *);
typedef IOReturn (*IO80211SapSetSTA_DISASSOCIATESlot)(IO80211SapProtocol *, struct apple80211_sta_disassoc_data *);
typedef IOReturn (*IO80211SapSetSTA_DEAUTHSlot)(IO80211SapProtocol *, struct apple80211_sta_disassoc_data *);
typedef IOReturn (*IO80211SapSetRSN_CONFSlot)(IO80211SapProtocol *, struct apple80211_rsn_conf_data *);
typedef IOReturn (*IO80211SapSetPEER_CACHE_CONTROLSlot)(IO80211SapProtocol *, struct apple80211_peer_cache_control *);
typedef IOReturn (*IO80211SapSetHOST_AP_MODE_HIDDENSlot)(IO80211SapProtocol *, struct apple80211_host_ap_mode_hidden_t *);
typedef IOReturn (*IO80211SapSetSOFTAP_PARAMSSlot)(IO80211SapProtocol *, struct apple80211_softap_params *);
typedef IOReturn (*IO80211SapSetSOFTAP_TRIGGER_CSASlot)(IO80211SapProtocol *, struct apple80211_softap_csa_params *);
typedef IOReturn (*IO80211SapSetSOFTAP_WIFI_NETWORK_INFO_IESlot)(IO80211SapProtocol *, struct apple80211_softap_wifi_network_info *);
typedef IOReturn (*IO80211SapSetSOFTAP_EXTENDED_CAPABILITIES_IESlot)(IO80211SapProtocol *, struct apple80211_softap_extended_capabilities_info *);
typedef IOReturn (*IO80211SapSetMIS_MAX_STASlot)(IO80211SapProtocol *, struct apple80211_mis_max_sta *);

static_assert(kIO80211SapProtocolVtableSlotGetSSID == 505,
              "APSTA getSSID slot must match recovered SAP contract");
static_assert(kIO80211SapProtocolBaseVtableSlotLast == 519,
              "IO80211SapProtocol base vtable must stop at recovered slot 519");
static_assert(kAppleBCMWLANAPSTAVtableSlotLastRecovered == 531,
              "APSTA recovered concrete vtable surface must extend through slot 531");
static_assert(kIO80211SapProtocolVtableSlotVirtualForwardPacket == 483,
              "IO80211SapProtocol virtual-interface forwardPacket slot mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotForwardPacket ==
              kIO80211SapProtocolVtableSlotSkywalkForwardPacket,
              "APSTA concrete forwardPacket must use the recovered slot 465 alias");
static_assert(kAppleBCMWLANAPSTAVtableSlotForwardPacket !=
              kIO80211SapProtocolVtableSlotVirtualForwardPacket,
              "APSTA forwardPacket must not be placed at the base virtual-interface slot");
static_assert(kAppleBCMWLANAPSTAVtableSlotSetMacAddress ==
              kIO80211SapProtocolVtableSlotSetMacAddress,
              "APSTA setMacAddress slot mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotGetCHANNEL ==
              kIO80211SapProtocolVtableSlotGetCHANNEL,
              "APSTA getCHANNEL slot mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotGetOP_MODE ==
              kIO80211SapProtocolVtableSlotGetOP_MODE,
              "APSTA getOP_MODE slot mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotGetSTATION_LIST ==
              kIO80211SapProtocolVtableSlotGetSTATION_LIST,
              "APSTA getSTATION_LIST slot mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotGetSTA_IE_LIST ==
              kIO80211SapProtocolVtableSlotGetSTA_IE_LIST,
              "APSTA getSTA_IE_LIST slot mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotGetKEY_RSC ==
              kIO80211SapProtocolVtableSlotGetKEY_RSC,
              "APSTA getKEY_RSC slot mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotGetSTA_STATS ==
              kIO80211SapProtocolVtableSlotGetSTA_STATS,
              "APSTA getSTA_STATS slot mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotGetPEER_CACHE_MAXIMUM_SIZE ==
              kIO80211SapProtocolVtableSlotGetPEER_CACHE_MAXIMUM_SIZE,
              "APSTA getPEER_CACHE_MAXIMUM_SIZE slot mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotGetHOST_AP_MODE_HIDDEN ==
              kIO80211SapProtocolVtableSlotGetHOST_AP_MODE_HIDDEN,
              "APSTA getHOST_AP_MODE_HIDDEN slot mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotGetSOFTAP_PARAMS ==
              kIO80211SapProtocolVtableSlotGetSOFTAP_PARAMS,
              "APSTA getSOFTAP_PARAMS slot mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotGetSOFTAP_STATS ==
              kIO80211SapProtocolVtableSlotGetSOFTAP_STATS,
              "APSTA getSOFTAP_STATS slot mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotSetSSID ==
              kIO80211SapProtocolVtableSlotSetSSID,
              "APSTA setSSID slot mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotSetCIPHER_KEY ==
              kIO80211SapProtocolVtableSlotSetCIPHER_KEY,
              "APSTA setCIPHER_KEY slot mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotSetCHANNEL ==
              kIO80211SapProtocolVtableSlotSetCHANNEL,
              "APSTA setCHANNEL slot mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotSetSTA_AUTHORIZE ==
              kIO80211SapProtocolVtableSlotSetSTA_AUTHORIZE,
              "APSTA setSTA_AUTHORIZE slot mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotSetSTA_DISASSOCIATE ==
              kIO80211SapProtocolVtableSlotSetSTA_DISASSOCIATE,
              "APSTA setSTA_DISASSOCIATE slot mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotSetSTA_DEAUTH ==
              kIO80211SapProtocolVtableSlotSetSTA_DEAUTH,
              "APSTA setSTA_DEAUTH slot mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotSetRSN_CONF ==
              kIO80211SapProtocolVtableSlotSetRSN_CONF,
              "APSTA setRSN_CONF slot mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotSetPEER_CACHE_CONTROL ==
              kIO80211SapProtocolVtableSlotSetPEER_CACHE_CONTROL,
              "APSTA setPEER_CACHE_CONTROL slot mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotSetHOST_AP_MODE_HIDDEN ==
              kIO80211SapProtocolVtableSlotSetHOST_AP_MODE_HIDDEN,
              "APSTA setHOST_AP_MODE_HIDDEN slot mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotSetSOFTAP_PARAMS ==
              kIO80211SapProtocolVtableSlotSetSOFTAP_PARAMS,
              "APSTA setSOFTAP_PARAMS slot mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotSetSOFTAP_TRIGGER_CSA ==
              kIO80211SapProtocolVtableSlotSetSOFTAP_TRIGGER_CSA,
              "APSTA setSOFTAP_TRIGGER_CSA slot mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotSetSOFTAP_WIFI_NETWORK_INFO_IE ==
              kIO80211SapProtocolVtableSlotSetSOFTAP_WIFI_NETWORK_INFO_IE,
              "APSTA setSOFTAP_WIFI_NETWORK_INFO_IE slot mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotSetSOFTAP_EXTENDED_CAPABILITIES_IE ==
              kIO80211SapProtocolVtableSlotSetSOFTAP_EXTENDED_CAPABILITIES_IE,
              "APSTA setSOFTAP_EXTENDED_CAPABILITIES_IE slot mismatch");
static_assert(kIO80211SapProtocolVtableSlotSetMIS_MAX_STA == 531,
              "APSTA setMIS_MAX_STA slot must match recovered SAP contract");
static_assert(kAppleBCMWLANAPSTAVtableSlotSetMIS_MAX_STA ==
              kIO80211SapProtocolVtableSlotSetMIS_MAX_STA,
              "APSTA concrete setMIS_MAX_STA slot mismatch");
static_assert(kIO80211SapProtocolVtableSlotSetHOST_AP_MODE ==
              kIO80211SapProtocolVtableSlotSetCHANNEL + 1,
              "APSTA setHOST_AP_MODE must immediately follow setCHANNEL");
static_assert(kIO80211SapProtocolVtableSlotSetSOFTAP_WIFI_NETWORK_INFO_IE ==
              kIO80211SapProtocolVtableSlotSetSOFTAP_TRIGGER_CSA + 1,
              "APSTA SoftAP Wi-Fi network-info slot order mismatch");
static_assert(kIO80211SapProtocolVtableSlotFirst * kIO80211VtableSlotByteStride ==
              kIO80211SapProtocolVtableByteOffsetFirst,
              "IO80211SapProtocol extension byte offset mismatch");
static_assert(kIO80211SapProtocolVtableSlotVirtualForwardPacket *
              kIO80211VtableSlotByteStride ==
              kIO80211SapProtocolVtableByteOffsetVirtualForwardPacket,
              "IO80211SapProtocol virtual forwardPacket byte offset mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotForwardPacket * kIO80211VtableSlotByteStride ==
              kAppleBCMWLANAPSTAVtableByteOffsetForwardPacket,
              "APSTA concrete forwardPacket byte offset mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotReset * kIO80211VtableSlotByteStride ==
              kAppleBCMWLANAPSTAVtableByteOffsetReset,
              "APSTA concrete reset byte offset mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotSetMacAddress * kIO80211VtableSlotByteStride ==
              kAppleBCMWLANAPSTAVtableByteOffsetSetMacAddress,
              "APSTA concrete setMacAddress byte offset mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotGetSSID * kIO80211VtableSlotByteStride ==
              kAppleBCMWLANAPSTAVtableByteOffsetGetSSID,
              "APSTA concrete getSSID byte offset mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotGetCHANNEL * kIO80211VtableSlotByteStride ==
              kAppleBCMWLANAPSTAVtableByteOffsetGetCHANNEL,
              "APSTA concrete getCHANNEL byte offset mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotGetSTATE * kIO80211VtableSlotByteStride ==
              kAppleBCMWLANAPSTAVtableByteOffsetGetSTATE,
              "APSTA concrete getSTATE byte offset mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotGetOP_MODE * kIO80211VtableSlotByteStride ==
              kAppleBCMWLANAPSTAVtableByteOffsetGetOP_MODE,
              "APSTA concrete getOP_MODE byte offset mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotGetSTATION_LIST *
              kIO80211VtableSlotByteStride ==
              kAppleBCMWLANAPSTAVtableByteOffsetGetSTATION_LIST,
              "APSTA concrete getSTATION_LIST byte offset mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotGetSTA_IE_LIST *
              kIO80211VtableSlotByteStride ==
              kAppleBCMWLANAPSTAVtableByteOffsetGetSTA_IE_LIST,
              "APSTA concrete getSTA_IE_LIST byte offset mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotGetKEY_RSC * kIO80211VtableSlotByteStride ==
              kAppleBCMWLANAPSTAVtableByteOffsetGetKEY_RSC,
              "APSTA concrete getKEY_RSC byte offset mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotGetSTA_STATS *
              kIO80211VtableSlotByteStride ==
              kAppleBCMWLANAPSTAVtableByteOffsetGetSTA_STATS,
              "APSTA concrete getSTA_STATS byte offset mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotGetPEER_CACHE_MAXIMUM_SIZE *
              kIO80211VtableSlotByteStride ==
              kAppleBCMWLANAPSTAVtableByteOffsetGetPEER_CACHE_MAXIMUM_SIZE,
              "APSTA concrete getPEER_CACHE_MAXIMUM_SIZE byte offset mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotGetHOST_AP_MODE_HIDDEN *
              kIO80211VtableSlotByteStride ==
              kAppleBCMWLANAPSTAVtableByteOffsetGetHOST_AP_MODE_HIDDEN,
              "APSTA concrete getHOST_AP_MODE_HIDDEN byte offset mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotGetSOFTAP_PARAMS *
              kIO80211VtableSlotByteStride ==
              kAppleBCMWLANAPSTAVtableByteOffsetGetSOFTAP_PARAMS,
              "APSTA concrete getSOFTAP_PARAMS byte offset mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotGetSOFTAP_STATS *
              kIO80211VtableSlotByteStride ==
              kAppleBCMWLANAPSTAVtableByteOffsetGetSOFTAP_STATS,
              "APSTA concrete getSOFTAP_STATS byte offset mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotSetSSID * kIO80211VtableSlotByteStride ==
              kAppleBCMWLANAPSTAVtableByteOffsetSetSSID,
              "APSTA concrete setSSID byte offset mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotSetCIPHER_KEY *
              kIO80211VtableSlotByteStride ==
              kAppleBCMWLANAPSTAVtableByteOffsetSetCIPHER_KEY,
              "APSTA concrete setCIPHER_KEY byte offset mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotSetCHANNEL * kIO80211VtableSlotByteStride ==
              kAppleBCMWLANAPSTAVtableByteOffsetSetCHANNEL,
              "APSTA concrete setCHANNEL byte offset mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotSetHOST_AP_MODE * kIO80211VtableSlotByteStride ==
              kAppleBCMWLANAPSTAVtableByteOffsetSetHOST_AP_MODE,
              "APSTA concrete setHOST_AP_MODE byte offset mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotSetSTA_AUTHORIZE * kIO80211VtableSlotByteStride ==
              kAppleBCMWLANAPSTAVtableByteOffsetSetSTA_AUTHORIZE,
              "APSTA concrete setSTA_AUTHORIZE byte offset mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotSetSTA_DISASSOCIATE *
              kIO80211VtableSlotByteStride ==
              kAppleBCMWLANAPSTAVtableByteOffsetSetSTA_DISASSOCIATE,
              "APSTA concrete setSTA_DISASSOCIATE byte offset mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotSetSTA_DEAUTH * kIO80211VtableSlotByteStride ==
              kAppleBCMWLANAPSTAVtableByteOffsetSetSTA_DEAUTH,
              "APSTA concrete setSTA_DEAUTH byte offset mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotSetRSN_CONF * kIO80211VtableSlotByteStride ==
              kAppleBCMWLANAPSTAVtableByteOffsetSetRSN_CONF,
              "APSTA concrete setRSN_CONF byte offset mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotSetPEER_CACHE_CONTROL *
              kIO80211VtableSlotByteStride ==
              kAppleBCMWLANAPSTAVtableByteOffsetSetPEER_CACHE_CONTROL,
              "APSTA concrete setPEER_CACHE_CONTROL byte offset mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotSetHOST_AP_MODE_HIDDEN *
              kIO80211VtableSlotByteStride ==
              kAppleBCMWLANAPSTAVtableByteOffsetSetHOST_AP_MODE_HIDDEN,
              "APSTA concrete setHOST_AP_MODE_HIDDEN byte offset mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotSetSOFTAP_PARAMS *
              kIO80211VtableSlotByteStride ==
              kAppleBCMWLANAPSTAVtableByteOffsetSetSOFTAP_PARAMS,
              "APSTA concrete setSOFTAP_PARAMS byte offset mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotSetSOFTAP_TRIGGER_CSA *
              kIO80211VtableSlotByteStride ==
              kAppleBCMWLANAPSTAVtableByteOffsetSetSOFTAP_TRIGGER_CSA,
              "APSTA concrete setSOFTAP_TRIGGER_CSA byte offset mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotSetSOFTAP_WIFI_NETWORK_INFO_IE *
              kIO80211VtableSlotByteStride ==
              kAppleBCMWLANAPSTAVtableByteOffsetSetSOFTAP_WIFI_NETWORK_INFO_IE,
              "APSTA concrete setSOFTAP_WIFI_NETWORK_INFO_IE byte offset mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotSetSOFTAP_EXTENDED_CAPABILITIES_IE *
              kIO80211VtableSlotByteStride ==
              kAppleBCMWLANAPSTAVtableByteOffsetSetSOFTAP_EXTENDED_CAPABILITIES_IE,
              "APSTA concrete setSOFTAP_EXTENDED_CAPABILITIES_IE byte offset mismatch");
static_assert(kAppleBCMWLANAPSTAVtableSlotSetMIS_MAX_STA * kIO80211VtableSlotByteStride ==
              kAppleBCMWLANAPSTAVtableByteOffsetSetMIS_MAX_STA,
              "APSTA concrete setMIS_MAX_STA byte offset mismatch");

#endif /* __IO80211_TARGET >= __MAC_26_0 */

#endif /* IO80211SapProtocol_h */
