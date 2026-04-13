//
//  AirportItlwmSkywalkInterface.cpp
//  AirportItlwm-Sonoma
//
//  Created by qcwap on 2023/6/27.
//  Copyright © 2023 钟先耀. All rights reserved.
//
#include "AirportItlwmV2.hpp"
#include "AirportItlwmSkywalkInterface.hpp"
#include <sys/CTimeout.hpp>
#include <libkern/c++/OSMetaClass.h>
#include <crypto/sha1.h>
#include <net80211/ieee80211_node.h>
#include <net80211/ieee80211_ioctl.h>
#include <net80211/ieee80211_priv.h>

#define super IO80211InfraProtocol
OSDefineMetaClassAndStructors(AirportItlwmSkywalkInterface, IO80211InfraProtocol);

const char* hexdump(uint8_t *buf, size_t len) {
    ssize_t str_len = len * 3 + 1;
    char *str = (char*)IOMalloc(str_len);
    if (!str)
        return nullptr;
    for (size_t i = 0; i < len; i++)
    snprintf(str + 3 * i, (len - i) * 3, "%02x ", buf[i]);
    str[MAX(str_len - 2, 0)] = 0;
    return str;
}

static int ieeeChanFlag2appleScanFlagVentura(int flags)
{
    int ret = 0;
    if (flags & IEEE80211_CHAN_2GHZ)
        ret |= APPLE80211_C_FLAG_2GHZ;
    if (flags & IEEE80211_CHAN_5GHZ)
        ret |= APPLE80211_C_FLAG_5GHZ;
    ret |= (APPLE80211_C_FLAG_ACTIVE | APPLE80211_C_FLAG_20MHZ);
    return ret;
}

static constexpr IOReturn kApple80211ErrDriverNotAvailable = 0xe0822403;
static constexpr IOReturn kApple80211ErrNoCachedValue = 0xe00002f0;
static constexpr IOReturn kApple80211ErrInvalidArgumentRaw = 0x16;

static bool isInvalidTahoeLqmThresholdByte(uint8_t value)
{
    // AppleBCMWLANCore::setLQM_CONFIG rejects any byte in the inclusive range
    // [0x0b, 0x9b] for the seven-byte threshold subfield starting at +0x11.
    return static_cast<uint8_t>(value - 0x0bU) < 0x91U;
}

static void initializeTahoeLqmConfig(apple80211_lqm_config_t *config)
{
    memset(config, 0, sizeof(*config));
    config->version = APPLE80211_VERSION;
    // IO80211LQMData mirrors one interval across the first three dwords of the
    // public carrier. Tahoe family-side validation only accepts 1000 or 5000
    // in that role; initialize the local cache to the lower valid interval
    // instead of inventing a non-Apple default.
    config->sample_period_ms = 1000;
    config->tx_per_interval_ms = 1000;
    config->rx_loss_interval_ms = 1000;
}

static int ieeeChanFlag2apple(int flags, int bw)
{
    int ret = 0;
    if (flags & IEEE80211_CHAN_2GHZ)
        ret |= APPLE80211_C_FLAG_2GHZ;
    if (flags & IEEE80211_CHAN_5GHZ)
        ret |= APPLE80211_C_FLAG_5GHZ;
    if (!(flags & IEEE80211_CHAN_PASSIVE))
        ret |= APPLE80211_C_FLAG_ACTIVE;
    if (flags & IEEE80211_CHAN_DFS)
        ret |= APPLE80211_C_FLAG_DFS;
    if (bw == -1) {
        if (flags & IEEE80211_CHAN_VHT) {
            if ((flags & IEEE80211_CHAN_VHT160) || (flags & IEEE80211_CHAN_VHT80_80))
                ret |= APPLE80211_C_FLAG_160MHZ;
            if (flags & IEEE80211_CHAN_VHT80)
                ret |= APPLE80211_C_FLAG_80MHZ;
        } else if ((flags & IEEE80211_CHAN_HT40) && (flags & IEEE80211_CHAN_HT)) {
            ret |= APPLE80211_C_FLAG_40MHZ;
            if (flags & IEEE80211_CHAN_HT40U)
                ret |= APPLE80211_C_FLAG_EXT_ABV;
        } else if (flags & IEEE80211_CHAN_HT20)
            ret |= APPLE80211_C_FLAG_20MHZ;
        else if ((flags & IEEE80211_CHAN_CCK) || (flags & IEEE80211_CHAN_OFDM))
            ret |= APPLE80211_C_FLAG_10MHZ;
    } else {
        switch (bw) {
            case IEEE80211_CHAN_WIDTH_80P80:
            case IEEE80211_CHAN_WIDTH_160:
                ret |= APPLE80211_C_FLAG_160MHZ;
                break;
            case IEEE80211_CHAN_WIDTH_80:
                ret |= APPLE80211_C_FLAG_80MHZ;
                break;
            case IEEE80211_CHAN_WIDTH_40:
                ret |= APPLE80211_C_FLAG_40MHZ;
                if (flags & IEEE80211_CHAN_HT40U)
                    ret |= APPLE80211_C_FLAG_EXT_ABV;
                break;
            case IEEE80211_CHAN_WIDTH_20:
                ret |= APPLE80211_C_FLAG_20MHZ;
                break;
            default:
                if (flags & IEEE80211_CHAN_HT20)
                    ret |= APPLE80211_C_FLAG_20MHZ;
                else if ((flags & IEEE80211_CHAN_CCK) || (flags & IEEE80211_CHAN_OFDM))
                    ret |= APPLE80211_C_FLAG_10MHZ;
                break;
        }
    }
    return ret;
}

struct triggerCCSnapshot
{
    uint64_t qword0;
    uint64_t qword1;
    uint64_t qword2;
    uint64_t qword3;
} __attribute__((packed));
static_assert(sizeof(triggerCCSnapshot) == 0x20,
              "triggerCCSnapshot must match the first four qwords cached by Apple");

struct tahoeIPv4ParamsContract
{
    uint32_t address;
    uint32_t netmask;
    uint32_t gateway;
    uint16_t gatewayTail;
} __attribute__((packed));
static_assert(sizeof(tahoeIPv4ParamsContract) == 0x0E,
              "tahoeIPv4ParamsContract must match recovered Apple field coverage");

struct tahoeIPv6ParamsHeader
{
    uint32_t count;
    uint8_t addresses[10][16];
} __attribute__((packed));
static_assert(sizeof(tahoeIPv6ParamsHeader) == 0xA4,
              "tahoeIPv6ParamsHeader must match recovered Apple count+address coverage");

struct apple80211_offload_arp_data
{
    uint32_t version;
    uint32_t has_ipv4_address;
    uint32_t ipv4_address;
    uint32_t keepalive_enabled;
    uint32_t gateway;
    uint16_t gateway_tail;
    uint16_t reserved;
} __attribute__((packed));
static_assert(sizeof(apple80211_offload_arp_data) == 0x18,
              "apple80211_offload_arp_data must preserve the recovered 0x18 Apple offsets");

struct tahoeWclRealTimeMode
{
    uint8_t enabled;
} __attribute__((packed));

struct tahoeWclUlofdmaState
{
    uint32_t mode;
} __attribute__((packed));
static_assert(sizeof(tahoeWclUlofdmaState) == 0x4,
              "tahoeWclUlofdmaState must match the Apple dword carrier");

struct tahoeWclQosParams
{
    uint32_t long_retry_limit;
    uint32_t rts_threshold;
    uint32_t lifetime_ac3;
    uint32_t lifetime_ac2;
    uint32_t powersave_mode;
    uint8_t  low_latency_policy;
    uint8_t  realtime_mlo;
    uint8_t  reserved16;
    uint8_t  flags;
} __attribute__((packed));
static_assert(sizeof(tahoeWclQosParams) == 0x18,
              "tahoeWclQosParams must match Apple dword fields + tail bytes");

struct tahoeFaceTimeWiFiCallingParams
{
    uint32_t status;
} __attribute__((packed));
static_assert(sizeof(tahoeFaceTimeWiFiCallingParams) == 0x4,
              "tahoeFaceTimeWiFiCallingParams must match Apple status dword");

struct tahoeDualPowerModeParams
{
    int32_t primary;
    int32_t secondary;
} __attribute__((packed));
static_assert(sizeof(tahoeDualPowerModeParams) == 0x8,
              "tahoeDualPowerModeParams must match two Apple mode dwords");

struct tahoeCongestionControlIndication
{
    uint8_t enabled;
} __attribute__((packed));
static_assert(sizeof(tahoeCongestionControlIndication) == 0x1,
              "tahoeCongestionControlIndication must match Apple bool carrier");

struct tahoeLmtpcConfig
{
    uint8_t value;
} __attribute__((packed));
static_assert(sizeof(tahoeLmtpcConfig) == 0x1,
              "tahoeLmtpcConfig must match Apple byte carrier");

struct tahoeLeScanParams
{
    uint8_t disconnected;
    uint8_t reserved01[3];
    uint32_t connectedEvents;
    uint32_t disconnectedEvents;
    uint32_t bucket;
} __attribute__((packed));
static_assert(sizeof(tahoeLeScanParams) == 0x10,
              "tahoeLeScanParams must match the recovered Apple 0x10 payload");

struct apple80211_reassoc
{
    uint8_t reserved00[0x68];
    uint16_t channels[7];
    uint32_t channel_scores[7];
    uint32_t channel_count;
    uint32_t score_count;
    uint8_t feature_flags;
    int8_t roam_reason;
} __attribute__((packed));
static_assert(sizeof(apple80211_reassoc) == 0x9c,
              "apple80211_reassoc must cover the Apple offsets used by sendReassocCommand");

struct apple80211_legacy_roam_profile_config
{
    uint8_t raw[0x60];
} __attribute__((packed));
static_assert(sizeof(apple80211_legacy_roam_profile_config) == 0x60,
              "legacy roam profile config must match WCLRoamProfile payload size");

struct apple80211_roam_profile_config
{
    uint8_t raw[0x23c];
} __attribute__((packed));
static_assert(sizeof(apple80211_roam_profile_config) == 0x23c,
              "roam profile config must match WCLRoamProfile payload size");

struct apple80211_wcl_arp_mode
{
    uint16_t wnm_interval_a;
    uint16_t wnm_interval_b;
    uint8_t wnm_enabled_a;
    uint8_t wnm_enabled_b;
    uint16_t reserved06;
    uint32_t mode;
    uint32_t interval;
    uint8_t enabled;
    uint8_t reserved11[3];
} __attribute__((packed));
static_assert(offsetof(apple80211_wcl_arp_mode, mode) == 0x08,
              "apple80211_wcl_arp_mode::mode must match Apple offset +0x8");
static_assert(offsetof(apple80211_wcl_arp_mode, interval) == 0x0c,
              "apple80211_wcl_arp_mode::interval must match Apple offset +0xc");
static_assert(offsetof(apple80211_wcl_arp_mode, enabled) == 0x10,
              "apple80211_wcl_arp_mode::enabled must match Apple offset +0x10");
static_assert(sizeof(apple80211_wcl_arp_mode) == 0x14,
              "apple80211_wcl_arp_mode must preserve the recovered Tahoe offsets");

struct apple80211_bg_motion_profile
{
    uint8_t raw[0x40];
} __attribute__((packed));

struct apple80211_bg_network
{
    uint8_t raw[0x12c0];
} __attribute__((packed));
static_assert(sizeof(apple80211_bg_network) == 0x12c0,
              "apple80211_bg_network must preserve the full Apple copy range");

struct apple80211_bg_scan
{
    uint8_t raw[8];
} __attribute__((packed));

struct apple80211_bg_params
{
    uint8_t raw[0x20];
} __attribute__((packed));
static_assert(sizeof(apple80211_bg_params) == 0x20,
              "apple80211_bg_params must preserve the recovered 0x20 payload");

struct apple80211_pm_mode
{
    uint32_t version;
    uint32_t mode;
} __attribute__((packed));
static_assert(sizeof(apple80211_pm_mode) == 0x8,
              "apple80211_pm_mode must preserve the recovered version+mode layout");

struct apple80211_user_roam_cache
{
    uint8_t raw[0x7c];
} __attribute__((packed));
static_assert(sizeof(apple80211_user_roam_cache) == 0x7c,
              "apple80211_user_roam_cache must cover the recovered count/override tail");

struct scanHomeAndAwayTime
{
    uint32_t milliseconds;
} __attribute__((packed));
static_assert(sizeof(scanHomeAndAwayTime) == 0x4,
              "scanHomeAndAwayTime must match the single dword consumed by Apple");

struct apple80211_timesync_info
{
    char text[0x100];
} __attribute__((packed));
static_assert(sizeof(apple80211_timesync_info) == 0x100,
              "apple80211_timesync_info must preserve the recovered 0x100 text buffer");

struct apple80211_wcl_wnm_config_t
{
    uint8_t raw[0x338];
} __attribute__((packed));
static_assert(sizeof(apple80211_wcl_wnm_config_t) == 0x338,
              "apple80211_wcl_wnm_config_t must cover the recovered WnmAgent field range");

struct apple80211_wcl_wnm_offload_t
{
    uint8_t raw[0x30];
} __attribute__((packed));
static_assert(sizeof(apple80211_wcl_wnm_offload_t) == 0x30,
              "apple80211_wcl_wnm_offload_t must cover the recovered DMS offload field range");

static constexpr IOReturn kIOReturnBadArgumentTahoe = static_cast<IOReturn>(0xe00002bc);

void AirportItlwmSkywalkInterface::associateSSID(uint8_t *ssid, uint32_t ssid_len, const struct ether_addr &bssid, uint32_t authtype_lower, uint32_t authtype_upper, uint8_t *key, uint32_t key_len, int key_index)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    XYLog("DEBUG %s ssid_len=%u auth_lower=%u auth_upper=%u key_len=%u bssid=%02x:%02x:%02x:%02x:%02x:%02x ic_state=%d\n",
          __FUNCTION__, ssid_len, authtype_lower, authtype_upper, key_len,
          bssid.octet[0], bssid.octet[1], bssid.octet[2],
          bssid.octet[3], bssid.octet[4], bssid.octet[5], ic->ic_state);

    ieee80211_disable_rsn(ic);
    ieee80211_disable_wep(ic);
    
    struct ieee80211_wpaparams     wpa;
    struct ieee80211_nwkey         nwkey;
    bzero(&wpa, sizeof(wpa));
    bzero(&nwkey, sizeof(nwkey));
    
    memset(ic->ic_des_essid, 0, IEEE80211_NWID_LEN);
    memcpy(ic->ic_des_essid, ssid, ssid_len);
    ic->ic_des_esslen = ssid_len;
    
    bool is_zero = true;
    for (int i = 0; i < IEEE80211_ADDR_LEN; i++)
    is_zero &= bssid.octet[i] == 0;
    
    if (!is_zero) {
        IEEE80211_ADDR_COPY(ic->ic_des_bssid, bssid.octet);
        ic->ic_flags |= IEEE80211_F_DESBSSID;
    }
    else {
        memset(ic->ic_des_bssid, 0, IEEE80211_ADDR_LEN);
        ic->ic_flags &= ~IEEE80211_F_DESBSSID;
    }

    // AUTHTYPE_WPA3_SAE AUTHTYPE_WPA3_FT_SAE
    // we don't really support WPA3, but we have announced we support WPA3 in card capability function. so we fake it as WPA2 to support some WPA2/WPA3 mix wifi connection.
    if (authtype_upper == APPLE80211_AUTHTYPE_WPA3_SAE || authtype_upper == APPLE80211_AUTHTYPE_WPA3_FT_SAE) {
        wpa.i_protos |= IEEE80211_WPA_PROTO_WPA2;
        authtype_upper |= APPLE80211_AUTHTYPE_WPA2_PSK;// hack
    }
    // AUTHTYPE_WPA3_ENTERPRISE AUTHTYPE_WPA3_FT_ENTERPRISE
    if (authtype_upper == APPLE80211_AUTHTYPE_WPA3_ENTERPRISE || authtype_upper == APPLE80211_AUTHTYPE_WPA3_FT_ENTERPRISE) {
        wpa.i_protos |= IEEE80211_WPA_PROTO_WPA2;
        authtype_upper |= APPLE80211_AUTHTYPE_WPA2;// hack
    }
    
    if (authtype_upper & (APPLE80211_AUTHTYPE_WPA | APPLE80211_AUTHTYPE_WPA_PSK | APPLE80211_AUTHTYPE_WPA2 | APPLE80211_AUTHTYPE_WPA2_PSK | APPLE80211_AUTHTYPE_SHA256_PSK | APPLE80211_AUTHTYPE_SHA256_8021X)) {
        XYLog("%s %d\n", __FUNCTION__, __LINE__);
        wpa.i_protos = IEEE80211_WPA_PROTO_WPA1 | IEEE80211_WPA_PROTO_WPA2;
    }
    
    if (authtype_upper & (APPLE80211_AUTHTYPE_WPA_PSK | APPLE80211_AUTHTYPE_WPA2_PSK | APPLE80211_AUTHTYPE_SHA256_PSK)) {
        XYLog("%s %d\n", __FUNCTION__, __LINE__);
        wpa.i_akms |= IEEE80211_WPA_AKM_PSK | IEEE80211_WPA_AKM_SHA256_PSK;
        wpa.i_enabled = 1;
        memcpy(ic->ic_psk, key, sizeof(ic->ic_psk));
        ic->ic_flags |= IEEE80211_F_PSK;
        ieee80211_ioctl_setwpaparms(ic, &wpa);
    }
    if (authtype_upper & (APPLE80211_AUTHTYPE_WPA | APPLE80211_AUTHTYPE_WPA2 | APPLE80211_AUTHTYPE_SHA256_8021X)) {
        XYLog("%s %d\n", __FUNCTION__, __LINE__);
        wpa.i_akms |= IEEE80211_WPA_AKM_8021X | IEEE80211_WPA_AKM_SHA256_8021X;
        wpa.i_enabled = 1;
        ieee80211_ioctl_setwpaparms(ic, &wpa);
    }
    
    if (authtype_lower == APPLE80211_AUTHTYPE_SHARED) {
        XYLog("shared key authentication is not supported!\n");
        return;
    }
    
    if (authtype_upper == APPLE80211_AUTHTYPE_NONE && authtype_lower == APPLE80211_AUTHTYPE_OPEN) { // Open or WEP Open System
        if (key_len > 0) {
            XYLog("%s %d\n", __FUNCTION__, __LINE__);
            nwkey.i_wepon = IEEE80211_NWKEY_WEP;
            nwkey.i_defkid = key_index + 1;
            nwkey.i_key[key_index].i_keylen = (int)key_len;
            nwkey.i_key[key_index].i_keydat = key;
            ieee80211_ioctl_setnwkeys(ic, &nwkey);
        }
    }
}

void AirportItlwmSkywalkInterface::setPTK(const u_int8_t *key, size_t key_len) {
    struct ieee80211com *ic = fHalService->get80211Controller();
    struct ieee80211_node    * ni = ic->ic_bss;
    struct ieee80211_key *k;
    int keylen;
    
    ni->ni_rsn_supp_state = RNSA_SUPP_PTKDONE;
    
    if (ni->ni_rsncipher != IEEE80211_CIPHER_USEGROUP) {
        u_int64_t prsc;
        
        /* check that key length matches that of pairwise cipher */
        keylen = ieee80211_cipher_keylen(ni->ni_rsncipher);
        if (key_len != keylen) {
            XYLog("PTK length mismatch. expected %d, got %zu\n", keylen, key_len);
            return;
        }
        prsc = /*(gtk == NULL) ? LE_READ_6(key->rsc) :*/ 0;
        
        /* map PTK to 802.11 key */
        k = &ni->ni_pairwise_key;
        memset(k, 0, sizeof(*k));
        k->k_cipher = ni->ni_rsncipher;
        k->k_rsc[0] = prsc;
        k->k_len = keylen;
        memcpy(k->k_key, key, k->k_len);
        /* install the PTK */
        if ((*ic->ic_set_key)(ic, ni, k) != 0) {
            XYLog("setting PTK failed\n");
            return;
        }
        else
            XYLog("setting PTK successfully\n");
        ni->ni_flags &= ~IEEE80211_NODE_RSN_NEW_PTK;
        ni->ni_flags &= ~IEEE80211_NODE_TXRXPROT;
        ni->ni_flags |= IEEE80211_NODE_RXPROT;
    } else if (ni->ni_rsncipher != IEEE80211_CIPHER_USEGROUP)
        XYLog("%s: unexpected pairwise key update received from %s\n",
              ic->ic_if.if_xname, ether_sprintf(ni->ni_macaddr));
}

void AirportItlwmSkywalkInterface::setGTK(const u_int8_t *gtk, size_t key_len, u_int8_t kid, u_int8_t *rsc) {
    struct ieee80211com *ic = fHalService->get80211Controller();
    struct ieee80211_node    * ni = ic->ic_bss;
    struct ieee80211_key *k;
    int keylen;
    
    if (gtk != NULL) {
        /* check that key length matches that of group cipher */
        keylen = ieee80211_cipher_keylen(ni->ni_rsngroupcipher);
        if (key_len != keylen) {
            XYLog("GTK length mismatch. expected %d, got %zu\n", keylen, key_len);
            return;
        }
        /* map GTK to 802.11 key */
        k = &ic->ic_nw_keys[kid];
        if (k->k_cipher == IEEE80211_CIPHER_NONE || k->k_len != keylen || memcmp(k->k_key, gtk, keylen) != 0) {
            memset(k, 0, sizeof(*k));
            k->k_id = kid;    /* 0-3 */
            k->k_cipher = ni->ni_rsngroupcipher;
            k->k_flags = IEEE80211_KEY_GROUP;
            //if (gtk[6] & (1 << 2))
            //  k->k_flags |= IEEE80211_KEY_TX;
            k->k_rsc[0] = LE_READ_6(rsc);
            k->k_len = keylen;
            memcpy(k->k_key, gtk, k->k_len);
            /* install the GTK */
            if ((*ic->ic_set_key)(ic, ni, k) != 0) {
                XYLog("setting GTK failed\n");
                return;
            }
            else
                XYLog("setting GTK successfully\n");
        }
    }
    
    if (true) {
        ni->ni_flags |= IEEE80211_NODE_TXRXPROT;
#ifndef IEEE80211_STA_ONLY
        if (ic->ic_opmode != IEEE80211_M_IBSS ||
            ++ni->ni_key_count == 2)
#endif
        {
            XYLog("marking port %s valid\n",
                  ether_sprintf(ni->ni_macaddr));
            ni->ni_port_valid = 1;
            ieee80211_set_link_state(ic, LINK_STATE_UP);
            ni->ni_assoc_fail = 0;
            if (ic->ic_opmode == IEEE80211_M_STA)
                ic->ic_rsngroupcipher = ni->ni_rsngroupcipher;
        }
    }
}

void AirportItlwmSkywalkInterface::
free()
{
    RT_SET(22);
    sRT.skFreeStep = 1;
    XYLog("DEBUG %s entry mExpansionData=%p mExpansionData2=%p "
          "instance=%p fHalService=%p rtMask=0x%07x\n",
          __FUNCTION__, mExpansionData, mExpansionData2,
          instance, fHalService, sRT.rtMask);

    // Panic timer — the previous crash (panic8) was in the destructor chain
    // triggered by super::free() → IO80211SkywalkInterface::free() → D0 → D1.
    // If it hangs again, dump full lifecycle state.
    thread_call_t skFreeTimer = thread_call_allocate(
        [](thread_call_param_t, thread_call_param_t) {
            if (!(sRT.rtMask & 0x800000))
                panic("SkywalkInterface::free hung  "
                      "skFreeStep=%u rtMask=0x%07x rt2=0x%04x rt3=0x%04x | "
                      "stopStep=%u freeStep=%u ss=%u | "
                      "ic=%d fl=0x%x pwr=%u link=0x%x | "
                      "evt=%u pm=%u(req=%u) wd=%u ioctl=%u(last=%d) "
                      "scanDone=%u ifType=0x%x "
                      "ls=%d lsCnt=%u scan=%u pmCnt=%u | "
                      "scanReq=%u assoc=%u scanRes=%u "
                      "icfl=0x%x esslen=%u nodes=%u mfail=0x%x | "
                      "fVars=%p bsdIf=%p enCnt=%u disCnt=%u enRet=0x%x | "
                      "bsdFl=0x%x bsdMtu=%u",
                      sRT.skFreeStep, sRT.rtMask, sRT.rtMask2, sRT.rtMask3,
                      sRT.stopStep, sRT.freeStep, sRT.startStep,
                      sRT.ic_state, sRT.if_flags, sRT.power_state,
                      sRT.linkStatus,
                      sRT.evtCount, sRT.postMsgCount, sRT.lastPmReq,
                      sRT.wdCount,
                      sRT.ioctlCount, sRT.lastIoctl,
                      sRT.scanDoneCount, sRT.ifType,
                      sRT.lastLinkState, sRT.linkSetCount,
                      sRT.scanCount, sRT.pmCount,
                      sRT.scanReqCount, sRT.assocCount, sRT.scanResCount,
                      sRT.ic_flags, sRT.ic_des_esslen,
                      sRT.nodeCount, sRT.matchFail,
                      (void *)(uintptr_t)sRT.fVarsPtr,
                      (void *)(uintptr_t)sRT.bsdIfPtr,
                      sRT.enableCnt, sRT.disableCnt, sRT.lastEnableRet,
                      sRT.bsdIfFlags, sRT.bsdIfMtu);
        }, NULL);
    uint64_t skFreeDeadline;
    clock_interval_to_deadline(30, kSecondScale, &skFreeDeadline);
    thread_call_enter_delayed(skFreeTimer, skFreeDeadline);

    // mExpansionData, mExpansionData2, and fVars->registrationInfo are
    // managed by the framework (allocated in initRegistrationInfo, freed
    // in IOSkywalkNetworkInterface::free / IOSkywalkEthernetInterface::free).
    // Manual IOFree was removed — it was freeing at wrong offsets when our
    // class size was 0x10 too small.  super::free() handles cleanup.
    sRT.skFreeStep = 2;
    instance = NULL;
    fHalService = NULL;
    scanSource = NULL;
    fNextNodeToSend = NULL;
    sRT.skFreeStep = 5;
    XYLog("DEBUG %s calling super::free (IO80211InfraProtocol)\n", __FUNCTION__);
    super::free();
    RT_SET(23);
    thread_call_cancel(skFreeTimer);
    thread_call_free(skFreeTimer);
}

IOReturn AirportItlwmSkywalkInterface::
getCHANNELS_INFO(apple80211_channels_info *data)
{
    if (!data)
        return kIOReturnBadArgument;

    // Tahoe's UI path still queries legacy APPLE80211_IOC_CHANNELS_INFO.
    // Live 26.x IOC DEBUG traces showed slot [483] returning 0xe00002c7 from
    // this Skywalk vtable.  The older AirportVirtualIOCTL path already proved
    // that a simple channel inventory satisfies the family contract here, so
    // keep the Skywalk slot on that same ABI instead of shadowing it with
    // unsupported.
    struct ieee80211com *ic = fHalService ? fHalService->get80211Controller() : nullptr;
    if (!ic)
        return kIOReturnNotReady;

    memset(data, 0, sizeof(*data));
    data->version = APPLE80211_VERSION;
    for (int i = 0; i < IEEE80211_CHAN_MAX; i++) {
        struct ieee80211_channel *channel = &ic->ic_channels[i];
        if (channel->ic_freq == 0)
            continue;

        int slot = data->num_chan_specs;
        int chanNum = ieee80211_chan2ieee(ic, channel);
        data->chan_num[slot] = chanNum;
        data->support_80Mhz[slot] = IEEE80211_IS_CHAN_VHT80(channel);
        data->support_40Mhz[slot] =
            IEEE80211_IS_CHAN_HT40(channel) || IEEE80211_IS_CHAN_VHT40(channel);
        data->num_chan_specs++;
        if (data->num_chan_specs >= APPLE80211_MAX_CHANNELS)
            break;
    }
    return kIOReturnSuccess;
}

UInt64 AirportItlwmSkywalkInterface::createEventPipe(IO80211APIUserClient *client)
{
    return super::createEventPipe(client);
}

IOReturn AirportItlwmSkywalkInterface::
processBSDCommand(ifnet_t interface, UInt cmd, void *data)
{
    // Tahoe removed a large block of legacy Apple80211 GET/SET methods from the
    // IO80211InfraProtocol vtable. Our driver still implements those semantics
    // as helper methods below, but without an explicit BSD-command bridge they
    // never get called on 26.x.
    //
    // Evidence kept here so the reason for this bridge is not lost:
    // - airportd repeatedly queried APPLE80211_IOC_SSID and received
    //   0xe0822403 / "driver not available", aborting auto-join.
    // - docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/89_remaining_gaps_closed_checked.yaml
    //   shows Tahoe first tries WCL and then expects driver fallback behavior
    //   matching IO80211Controller cache semantics (success + zeroed data).
    // - Our Tahoe target does not build AirportSTAIOCTL.cpp, so the old ioctl
    //   dispatcher never runs here; the BSD path must forward these requests.
    if ((cmd == SIOCGA80211 || cmd == SIOCSA80211) && data != NULL) {
        IOReturn ret = processApple80211Ioctl(cmd, (apple80211req *)data);
        if (ret != kIOReturnUnsupported)
            return ret;
    }
    return super::processBSDCommand(interface, cmd, data);
}

IOReturn AirportItlwmSkywalkInterface::
processApple80211Ioctl(UInt cmd, apple80211req *req)
{
    // Tahoe's non-virtual interface path still queries VIRTUAL_IF_ROLE/PARENT
    // during airportd/CoreWiFi _initInterface.  Reverse docs record Apple's
    // expected behavior here: non-virtual interfaces do NOT return raw POSIX
    // ENXIO/6, they return the Apple80211-specific -3903 (0xe082280e) code.
    //
    // Important Tahoe-specific detail from the live 853f68e logs:
    // our earlier source already had these switch cases, yet the framework
    // still logged raw `6` for both IOCTLs.  The reason is that these Apple
    // wrappers are payload-less safe-to-fail requests, while our bridge used
    // to reject *all* req_data == NULL requests before the switch.  That sent
    // VIRTUAL_IF_ROLE/PARENT straight back into the framework fallback path,
    // which is exactly where the raw POSIX-style ENXIO/6 surfaced.
    //
    // So the real 1:1 fix is not "add cases somewhere in the switch".  The
    // fix is to intercept these payload-less Tahoe IOCTLs *before* the generic
    // req_data validation and return the Apple-specific not-a-virtual-interface
    // contract directly.
    static const IOReturn kApple80211NotVirtualInterface =
        static_cast<IOReturn>(0xe082280e);

    if (req == NULL)
        return kIOReturnUnsupported;

    switch (req->req_type) {
        case APPLE80211_IOC_VIRTUAL_IF_ROLE:
        case APPLE80211_IOC_VIRTUAL_IF_PARENT:
            return kApple80211NotVirtualInterface;
        default:
            break;
    }

    if (req->req_data == NULL)
        return kIOReturnUnsupported;

    // Tahoe architectural gap fixed here:
    // the Skywalk-only BSD bridge originally forwarded just a small hand-picked
    // subset of Apple80211 IOCTLs.  That diverged from the legacy STA IOCTL
    // plane, where APPLE80211_IOC_POWERSAVE and several other state-carrier /
    // bring-up requests were already wired to real handlers.
    //
    // Live proof on 4973c4d:
    // - WCL logged `setPOWERSAVE ... not supported` during early init even
    //   though this class already implemented setPOWERSAVE/getPOWERSAVE.
    // - The failure was not inside the handler.  The BSD bridge simply never
    //   routed APPLE80211_IOC_POWERSAVE to it, so Tahoe fell back to
    //   unsupported before the recovered handler could run.
    //
    // Restore those routes here instead of papering over each symptom higher
    // up.  This keeps the Tahoe Skywalk path architecturally aligned with the
    // legacy IOCTL plane: if a real handler exists in AirportItlwm /
    // AirportItlwmSkywalkInterface, the BSD bridge must make it reachable.
    switch (req->req_type) {
        case APPLE80211_IOC_SSID:
            return (cmd == SIOCGA80211) ? getSSID((apple80211_ssid_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_AUTH_TYPE:
            if (cmd == SIOCGA80211)
                return getAUTH_TYPE((apple80211_authtype_data *)req->req_data);
            if (cmd == SIOCSA80211)
                return setAUTH_TYPE((apple80211_authtype_data *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_CHANNEL:
            if (cmd == SIOCGA80211)
                return getCHANNEL((apple80211_channel_data *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_POWER:
            if (instance == NULL)
                return kIOReturnNotReady;
            if (cmd == SIOCGA80211)
                return instance->getPOWER(this, (apple80211_power_data *)req->req_data);
            if (cmd == SIOCSA80211)
                return instance->setPOWER(this, (apple80211_power_data *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_POWERSAVE:
            if (cmd == SIOCGA80211)
                return getPOWERSAVE((apple80211_powersave_data *)req->req_data);
            if (cmd == SIOCSA80211)
                return setPOWERSAVE((apple80211_powersave_data *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_BSSID:
            return (cmd == SIOCGA80211) ? getBSSID((apple80211_bssid_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_SCAN_RESULT:
            return (cmd == SIOCGA80211) ? getSCAN_RESULT((apple80211_scan_result *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_STATE:
            return (cmd == SIOCGA80211) ? getSTATE((apple80211_state_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_PHY_MODE:
            return (cmd == SIOCGA80211) ? getPHY_MODE((apple80211_phymode_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_NOISE:
            return (cmd == SIOCGA80211) ? getNOISE((apple80211_noise_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_RSSI:
            return (cmd == SIOCGA80211) ? getRSSI((apple80211_rssi_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_ASSOCIATE:
            return (cmd == SIOCSA80211) ? setASSOCIATE((apple80211_assoc_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_DISASSOCIATE:
            return (cmd == SIOCSA80211) ? setDISASSOCIATE(req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_RATE:
            return (cmd == SIOCGA80211) ? getRATE((apple80211_rate_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_GUARD_INTERVAL:
            return (cmd == SIOCGA80211) ? getGUARD_INTERVAL((apple80211_guard_interval_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_HT_CAPABILITY:
            return (cmd == SIOCGA80211) ? getHT_CAPABILITY((apple80211_ht_capability *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_HE_CAPABILITY:
            return (cmd == SIOCGA80211) ? getHE_CAPABILITY((apple80211_he_capability *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_TXPOWER:
            return (cmd == SIOCGA80211) ? getTXPOWER((apple80211_txpower_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_THERMAL_INDEX:
            return (cmd == SIOCGA80211) ? getTHERMAL_INDEX((apple80211_thermal_index_t *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_POWER_BUDGET:
            return (cmd == SIOCGA80211) ? getPOWER_BUDGET((apple80211_power_budget_t *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_LQM_CONFIG:
            if (cmd == SIOCGA80211)
                return getLQM_CONFIG((apple80211_lqm_config_t *)req->req_data);
            if (cmd == SIOCSA80211)
                return setLQM_CONFIG((apple80211_lqm_config_t *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_PRIVATE_MAC:
            return (cmd == SIOCGA80211) ? getPRIVATE_MAC((apple80211_private_mac_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_OFFLOAD_TCPKA_ENABLE:
            return (cmd == SIOCGA80211) ? getOFFLOAD_TCPKA_ENABLE((apple80211_offload_tcpka_enable_t *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_OP_MODE:
            return (cmd == SIOCGA80211) ? getOP_MODE((apple80211_opmode_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_SUPPORTED_CHANNELS:
        case APPLE80211_IOC_HW_SUPPORTED_CHANNELS:
            return (cmd == SIOCGA80211) ? getSUPPORTED_CHANNELS((apple80211_sup_channel_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_LOCALE:
            return (cmd == SIOCGA80211) ? getLOCALE((apple80211_locale_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_DEAUTH:
            if (cmd == SIOCGA80211)
                return getDEAUTH((apple80211_deauth_data *)req->req_data);
            if (cmd == SIOCSA80211)
                return setDEAUTH((apple80211_deauth_data *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_RATE_SET:
            return (cmd == SIOCGA80211) ? getRATE_SET((apple80211_rate_set_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_RSN_IE:
            if (cmd == SIOCGA80211)
                return getRSN_IE((apple80211_rsn_ie_data *)req->req_data);
            if (cmd == SIOCSA80211)
                return setRSN_IE((apple80211_rsn_ie_data *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_AP_IE_LIST:
            return (cmd == SIOCGA80211) ? getAP_IE_LIST((apple80211_ap_ie_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_CIPHER_KEY:
            return (cmd == SIOCSA80211) ? setCIPHER_KEY((apple80211_key *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_ASSOCIATION_STATUS:
            return (cmd == SIOCGA80211) ? getASSOCIATION_STATUS((apple80211_assoc_status_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_COUNTRY_CODE:
            if (instance == NULL)
                return kIOReturnNotReady;
            if (cmd == SIOCGA80211)
                return instance->getCOUNTRY_CODE(this, (apple80211_country_code_data *)req->req_data);
            if (cmd == SIOCSA80211)
                return instance->setCOUNTRY_CODE(this, (apple80211_country_code_data *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_DRIVER_VERSION:
            if (instance == NULL || cmd != SIOCGA80211)
                return (instance == NULL) ? kIOReturnNotReady : kIOReturnUnsupported;
            return instance->getDRIVER_VERSION(this, (apple80211_version_data *)req->req_data);
        case APPLE80211_IOC_HARDWARE_VERSION:
            if (instance == NULL || cmd != SIOCGA80211)
                return (instance == NULL) ? kIOReturnNotReady : kIOReturnUnsupported;
            return instance->getHARDWARE_VERSION(this, (apple80211_version_data *)req->req_data);
        case APPLE80211_IOC_MCS:
            return (cmd == SIOCGA80211) ? getMCS((apple80211_mcs_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_MCS_INDEX_SET:
            return (cmd == SIOCGA80211) ? getMCS_INDEX_SET((apple80211_mcs_index_set_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_MCS_VHT:
            if (cmd == SIOCGA80211)
                return getMCS_VHT((apple80211_mcs_vht_data *)req->req_data);
            return kIOReturnUnsupported;
        case APPLE80211_IOC_SCAN_REQ:
            return (cmd == SIOCSA80211) ? setSCAN_REQ((apple80211_scan_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_NSS:
            return (cmd == SIOCGA80211) ? getNSS((apple80211_nss_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_CURRENT_NETWORK:
            return (cmd == SIOCGA80211) ? getCURRENT_NETWORK((apple80211_scan_result *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_LINK_CHANGED_EVENT_DATA:
            return (cmd == SIOCGA80211) ? getLINK_CHANGED_EVENT_DATA((apple80211_link_changed_event_data *)req->req_data)
                                        : kIOReturnUnsupported;
        case APPLE80211_IOC_VHT_MCS_INDEX_SET:
            return (cmd == SIOCGA80211) ? getVHT_MCS_INDEX_SET((apple80211_vht_mcs_index_set_data *)req->req_data)
                                        : kIOReturnUnsupported;
        default:
            return kIOReturnUnsupported;
    }
}

#if __IO80211_TARGET >= __MAC_26_0
bool AirportItlwmSkywalkInterface::
init()
{
    XYLog("DEBUG %s entry\n", __PRETTY_FUNCTION__);
    // Apple's Tahoe APSTA surface enters construction through subclass
    // no-arg init(), then performs provider/role/address binding on a
    // separate follow-up path. Advertising a local 2-arg init override here
    // was misleading: the port ignored those arguments anyway and the true
    // Apple-recovered crash-free constructor contract is still the no-arg
    // IO80211InfraInterface::init() path.
    if (!IO80211InfraInterface::init()) {
        XYLog("%s IO80211InfraInterface::init failed\n", __PRETTY_FUNCTION__);
        return false;
    }
    XYLog("DEBUG %s IO80211InfraInterface::init OK\n", __FUNCTION__);
    instance = NULL;
    fHalService = NULL;
    scanSource = NULL;
    cachedPowersaveLevel = APPLE80211_POWERSAVE_MODE_DISABLED;
    cachedThermalIndex = 0;
    cachedPowerBudget = 0;
    cachedPrivateMacState = 0;
    cachedPrivateMacTimeoutSeconds = 0;
    memset(cachedPrivateMacPrimary, 0, sizeof(cachedPrivateMacPrimary));
    memset(cachedPrivateMacSecondary, 0, sizeof(cachedPrivateMacSecondary));
    cachedTcpkaOffloadSupported = false;
    cachedTcpkaOffloadEnabled = false;
    cachedOSFeatureFlags = 0;
    cachedDhcpRenewalData = false;
    cachedBatteryPowerSaveMode = 0;
    cachedPowerProfile = 0;
    cachedCurrentMcs = 0;
    cachedUlofdmaState = 0;
    cachedMimoConfig = 0;
    cachedFaceTimeWiFiCallingStatus = 0;
    cachedDualPowerModePrimary = -1;
    cachedDualPowerModeSecondary = -1;
    cachedCongestionControlEnabled = false;
    cachedLmtpcValue = 0;
    memset(cachedLeScanParams, 0, sizeof(cachedLeScanParams));
    hasCachedLeScanParams = false;
    cachedRealTimeMode = false;
    cachedQosLongRetryLimit = 0;
    cachedQosRtsThreshold = 0;
    cachedQosLifetimeAc3 = 0;
    cachedQosLifetimeAc2 = 0;
    cachedQosFlags = 0;
    cachedIPv4Address = 0;
    cachedIPv4Netmask = 0;
    cachedIPv4Reserved = 0;
    cachedIPv4Gateway = 0;
    cachedIPv4GatewayTail = 0;
    cachedIPv6Count = 0;
    memset(cachedIPv6Addresses, 0, sizeof(cachedIPv6Addresses));
    memset(cachedIPv6LinkLocalAddress, 0, sizeof(cachedIPv6LinkLocalAddress));
    cachedInfraEnumerated = false;
    memset(cachedUserRoamCache, 0, sizeof(cachedUserRoamCache));
    hasCachedUserRoamCache = false;
    cachedPmMode = 0;
    initializeTahoeLqmConfig(&cachedLqmConfig);
    hasCachedLqmConfig = false;
    cachedScanHomeAwayTime = 0;
    memset(cachedWnmConfig, 0, sizeof(cachedWnmConfig));
    hasCachedWnmConfig = false;
    memset(cachedWnmOffload, 0, sizeof(cachedWnmOffload));
    hasCachedWnmOffload = false;
    memset(cachedReassocRequest, 0, sizeof(cachedReassocRequest));
    hasCachedReassocRequest = false;
    memset(cachedLegacyRoamProfileConfig, 0, sizeof(cachedLegacyRoamProfileConfig));
    hasCachedLegacyRoamProfileConfig = false;
    memset(cachedRoamProfileConfig, 0, sizeof(cachedRoamProfileConfig));
    hasCachedRoamProfileConfig = false;
    memset(cachedWclArpMode, 0, sizeof(cachedWclArpMode));
    hasCachedWclArpMode = false;
    memset(cachedBgMotionProfile, 0, sizeof(cachedBgMotionProfile));
    hasCachedBgMotionProfile = false;
    memset(cachedBgNetwork, 0, sizeof(cachedBgNetwork));
    hasCachedBgNetwork = false;
    memset(cachedBgScanConfig, 0, sizeof(cachedBgScanConfig));
    hasCachedBgScanConfig = false;
    memset(cachedBgParams, 0, sizeof(cachedBgParams));
    hasCachedBgParams = false;
    memset(cachedTriggerCC, 0, sizeof(cachedTriggerCC));
    cachedTriggerCCMode = 0;
    hasCachedTriggerCC = false;
    RT3_SET(12); // SkywalkInterface::init OK
    return true;
}

bool AirportItlwmSkywalkInterface::
init(IOService *provider, ether_addr *addr)
{
    XYLog("DEBUG %s entry provider=%p addr=%p\n", __PRETTY_FUNCTION__, provider, addr);
    // Tahoe still carries the secondary 2-arg vtable slot, but the kernel does
    // not export IO80211InfraInterface::init(provider, addr), so dropping the
    // local override makes the kext bind against a non-exported symbol and fail
    // BootKC verification. Keep this as a local shadow only; the real
    // constructor path is the no-arg init() + bindController() sequence above.
    return init();
}

bool AirportItlwmSkywalkInterface::
bindController(AirportItlwm *provider)
{
    XYLog("DEBUG %s entry provider=%p\n", __FUNCTION__, provider);
    // Recovered Apple APSTA construction uses a split contract: subclass
    // no-arg init first, then a separate parameter-binding path wires the
    // interface to controller-owned state before attach/start. Keep the local
    // port on the same shape instead of pretending the constructor itself is
    // the provider-binding entry.
    instance = provider;
    if (!instance) {
        XYLog("DEBUG %s FAIL: provider is NULL\n", __FUNCTION__);
        return false;
    }
    fHalService = instance->fHalService;
    scanSource = instance->scanSource;
    setInterfaceRole(1);
    setInterfaceId(1);
    XYLog("DEBUG %s OK: instance=%p fHalService=%p scanSource=%p\n",
          __FUNCTION__, instance, fHalService, scanSource);
    return true;
#else
bool AirportItlwmSkywalkInterface::
init(IOService *provider)
{
    XYLog("DEBUG %s entry provider=%p\n", __PRETTY_FUNCTION__, provider);
    bool ret = super::init(provider);
    if (!ret) {
        XYLog("%s super::init failed\n", __PRETTY_FUNCTION__);
        return false;
    }
    XYLog("DEBUG %s super::init OK\n", __FUNCTION__);
    instance = OSDynamicCast(AirportItlwm, provider);
    if (!instance) {
        XYLog("DEBUG %s FAIL: provider is not AirportItlwm\n", __FUNCTION__);
        return false;
    }
    this->fHalService = instance->fHalService;
    this->scanSource = instance->scanSource;
    this->cachedPowersaveLevel = APPLE80211_POWERSAVE_MODE_DISABLED;
    this->cachedThermalIndex = 0;
    this->cachedPowerBudget = 0;
    this->cachedPrivateMacState = 0;
    this->cachedPrivateMacTimeoutSeconds = 0;
    memset(this->cachedPrivateMacPrimary, 0, sizeof(this->cachedPrivateMacPrimary));
    memset(this->cachedPrivateMacSecondary, 0, sizeof(this->cachedPrivateMacSecondary));
    this->cachedTcpkaOffloadSupported = false;
    this->cachedTcpkaOffloadEnabled = false;
    this->cachedOSFeatureFlags = 0;
    this->cachedDhcpRenewalData = false;
    this->cachedBatteryPowerSaveMode = 0;
    this->cachedPowerProfile = 0;
    this->cachedCurrentMcs = 0;
    this->cachedUlofdmaState = 0;
    this->cachedMimoConfig = 0;
    this->cachedFaceTimeWiFiCallingStatus = 0;
    this->cachedDualPowerModePrimary = -1;
    this->cachedDualPowerModeSecondary = -1;
    this->cachedCongestionControlEnabled = false;
    this->cachedLmtpcValue = 0;
    memset(this->cachedLeScanParams, 0, sizeof(this->cachedLeScanParams));
    this->hasCachedLeScanParams = false;
    this->cachedRealTimeMode = false;
    this->cachedQosLongRetryLimit = 0;
    this->cachedQosRtsThreshold = 0;
    this->cachedQosLifetimeAc3 = 0;
    this->cachedQosLifetimeAc2 = 0;
    this->cachedQosFlags = 0;
    this->cachedIPv4Address = 0;
    this->cachedIPv4Netmask = 0;
    this->cachedIPv4Reserved = 0;
    this->cachedIPv4Gateway = 0;
    this->cachedIPv4GatewayTail = 0;
    this->cachedIPv6Count = 0;
    memset(this->cachedIPv6Addresses, 0, sizeof(this->cachedIPv6Addresses));
    memset(this->cachedIPv6LinkLocalAddress, 0, sizeof(this->cachedIPv6LinkLocalAddress));
    this->cachedInfraEnumerated = false;
    memset(this->cachedUserRoamCache, 0, sizeof(this->cachedUserRoamCache));
    this->hasCachedUserRoamCache = false;
    this->cachedPmMode = 0;
    initializeTahoeLqmConfig(&this->cachedLqmConfig);
    this->hasCachedLqmConfig = false;
    this->cachedScanHomeAwayTime = 0;
    memset(this->cachedWnmConfig, 0, sizeof(this->cachedWnmConfig));
    this->hasCachedWnmConfig = false;
    memset(this->cachedWnmOffload, 0, sizeof(this->cachedWnmOffload));
    this->hasCachedWnmOffload = false;
    memset(this->cachedReassocRequest, 0, sizeof(this->cachedReassocRequest));
    this->hasCachedReassocRequest = false;
    memset(this->cachedLegacyRoamProfileConfig, 0, sizeof(this->cachedLegacyRoamProfileConfig));
    this->hasCachedLegacyRoamProfileConfig = false;
    memset(this->cachedRoamProfileConfig, 0, sizeof(this->cachedRoamProfileConfig));
    this->hasCachedRoamProfileConfig = false;
    memset(this->cachedWclArpMode, 0, sizeof(this->cachedWclArpMode));
    this->hasCachedWclArpMode = false;
    memset(this->cachedBgMotionProfile, 0, sizeof(this->cachedBgMotionProfile));
    this->hasCachedBgMotionProfile = false;
    memset(this->cachedBgNetwork, 0, sizeof(this->cachedBgNetwork));
    this->hasCachedBgNetwork = false;
    memset(this->cachedBgScanConfig, 0, sizeof(this->cachedBgScanConfig));
    this->hasCachedBgScanConfig = false;
    memset(this->cachedBgParams, 0, sizeof(this->cachedBgParams));
    this->hasCachedBgParams = false;
    memset(this->cachedTriggerCC, 0, sizeof(this->cachedTriggerCC));
    this->cachedTriggerCCMode = 0;
    this->hasCachedTriggerCC = false;
    RT3_SET(12); // SkywalkInterface::init OK
    XYLog("DEBUG %s OK: instance=%p fHalService=%p scanSource=%p\n",
          __FUNCTION__, instance, fHalService, scanSource);
    return true;
#endif
}

//ifnet_t AirportItlwmSkywalkInterface::
//getBSDInterface()
//{
//    if (instance->bsdInterface)
//        return instance->bsdInterface->getIfnet();
//    return NULL;
//}

IOReturn AirportItlwmSkywalkInterface::
getSSID(struct apple80211_ssid_data *sd)
{
    struct ieee80211com * ic = fHalService->get80211Controller();
    // Apple's IO80211Controller::getSSIDData always pre-zeroes and returns success.
    // When not associated, the cached SSID data is zeroed (ssid_len=0).
    // Returning error here causes airportd to abort auto-join with "driver not available".
    memset(sd, 0, sizeof(*sd));
    sd->version = APPLE80211_VERSION;
    if (ic->ic_state == IEEE80211_S_RUN) {
        memcpy(sd->ssid_bytes, ic->ic_des_essid, strlen((const char*)ic->ic_des_essid));
        sd->ssid_len = (uint32_t)strlen((const char*)ic->ic_des_essid);
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getAUTH_TYPE(struct apple80211_authtype_data *ad)
{
    ad->version = APPLE80211_VERSION;
    ad->authtype_lower = current_authtype_lower;
    ad->authtype_upper = current_authtype_upper;
    XYLog("DEBUG %s lower=%u upper=%u\n", __FUNCTION__, current_authtype_lower, current_authtype_upper);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setAUTH_TYPE(struct apple80211_authtype_data *ad)
{
    XYLog("DEBUG %s lower=%u upper=%u\n", __FUNCTION__, ad->authtype_lower, ad->authtype_upper);
    current_authtype_lower = ad->authtype_lower;
    current_authtype_upper = ad->authtype_upper;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setCIPHER_KEY(struct apple80211_key *key)
{
    XYLog("%s\n", __FUNCTION__);
    const char* keydump = hexdump(key->key, key->key_len);
    const char* rscdump = hexdump(key->key_rsc, key->key_rsc_len);
    const char* eadump = hexdump(key->key_ea.octet, APPLE80211_ADDR_LEN);
    static_assert(__offsetof(struct apple80211_key, key_ea) == 92, "struct corrupted");
    static_assert(__offsetof(struct apple80211_key, key_rsc_len) == 80, "struct corrupted");
    static_assert(__offsetof(struct apple80211_key, wowl_kck_len) == 100, "struct corrupted");
    static_assert(__offsetof(struct apple80211_key, wowl_kek_len) == 120, "struct corrupted");
    static_assert(__offsetof(struct apple80211_key, wowl_kck_key) == 104, "struct corrupted");
    if (keydump && rscdump && eadump)
        XYLog("Set key request: len=%d cipher_type=%d flags=%d index=%d key=%s rsc_len=%d rsc=%s ea=%s\n",
              key->key_len, key->key_cipher_type, key->key_flags, key->key_index, keydump, key->key_rsc_len, rscdump, eadump);
    else
        XYLog("Set key request, but failed to allocate memory for hexdump\n");
    
    if (keydump)
        IOFree((void*)keydump, 3 * key->key_len + 1);
    if (rscdump)
        IOFree((void*)rscdump, 3 * key->key_rsc_len + 1);
    if (eadump)
        IOFree((void*)eadump, 3 * APPLE80211_ADDR_LEN + 1);
    
    switch (key->key_cipher_type) {
        case APPLE80211_CIPHER_NONE:
            // clear existing key
//            XYLog("Setting NONE key is not supported\n");
            break;
        case APPLE80211_CIPHER_WEP_40:
        case APPLE80211_CIPHER_WEP_104:
            XYLog("Setting WEP key %d is not supported\n", key->key_index);
            break;
        case APPLE80211_CIPHER_TKIP:
        case APPLE80211_CIPHER_AES_OCB:
        case APPLE80211_CIPHER_AES_CCM:
            switch (key->key_flags) {
                case 4: // PTK
                    setPTK(key->key, key->key_len);
                    break;
                case 0: // GTK
                    setGTK(key->key, key->key_len, key->key_index, key->key_rsc);
                    break;
            }
            break;
        case APPLE80211_CIPHER_PMK:
            XYLog("Setting WPA PMK is not supported\n");
            break;
        case APPLE80211_CIPHER_MSK:
            XYLog("Setting MSK\n");
            ieee80211_pmksa_add(fHalService->get80211Controller(), IEEE80211_AKM_8021X,
                                fHalService->get80211Controller()->ic_bss->ni_macaddr, key->key, 0);
            break;
        case APPLE80211_CIPHER_PMKSA:
            XYLog("Setting WPA PMKSA\n");
            ieee80211_pmksa_add(fHalService->get80211Controller(), IEEE80211_AKM_8021X,
                                fHalService->get80211Controller()->ic_bss->ni_macaddr, key->key, 0);
            break;
    }
    //fInterface->postMessage(APPLE80211_M_CIPHER_KEY_CHANGED);
    if (key->key_cipher_type == APPLE80211_CIPHER_TKIP ||
        key->key_cipher_type == APPLE80211_CIPHER_AES_OCB ||
        key->key_cipher_type == APPLE80211_CIPHER_AES_CCM) {
        if (key->key_flags == 0) {
            // GTK installed — 4-way handshake complete, notify Apple supplicant
            XYLog("%s posting RSN_HANDSHAKE_DONE after GTK install\n", __FUNCTION__);
            postMessage(APPLE80211_M_RSN_HANDSHAKE_DONE, NULL, 0, false);
        }
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getPHY_MODE(struct apple80211_phymode_data *pd)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    XYLog("DEBUG %s ic_curmode=%d ic_flags=0x%x ic_state=%d\n",
          __FUNCTION__, ic->ic_curmode, ic->ic_flags, ic->ic_state);
    pd->version = APPLE80211_VERSION;
    pd->phy_mode = APPLE80211_MODE_11A
    | APPLE80211_MODE_11B
    | APPLE80211_MODE_11G
    | APPLE80211_MODE_11N;
    
    if (ic->ic_flags & IEEE80211_F_VHTON)
        pd->phy_mode |= APPLE80211_MODE_11AC;
    
    if (ic->ic_flags & IEEE80211_F_HEON)
        pd->phy_mode |= APPLE80211_MODE_11AX;
    
    switch (fHalService->get80211Controller()->ic_curmode) {
        case IEEE80211_MODE_AUTO:
            pd->active_phy_mode = APPLE80211_MODE_AUTO;
            break;
        case IEEE80211_MODE_11A:
            pd->active_phy_mode = APPLE80211_MODE_11A;
            break;
        case IEEE80211_MODE_11B:
            pd->active_phy_mode = APPLE80211_MODE_11B;
            break;
        case IEEE80211_MODE_11G:
            pd->active_phy_mode = APPLE80211_MODE_11G;
            break;
        case IEEE80211_MODE_11N:
            pd->active_phy_mode = APPLE80211_MODE_11N;
            break;
        case IEEE80211_MODE_11AC:
            pd->active_phy_mode = APPLE80211_MODE_11AC;
            break;
        case IEEE80211_MODE_11AX:
            pd->active_phy_mode = APPLE80211_MODE_11AX;
            break;
            
        default:
            pd->active_phy_mode = APPLE80211_MODE_AUTO;
            break;
    }
    
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getCHANNEL(struct apple80211_channel_data *cd)
{
    struct ieee80211com * ic = fHalService->get80211Controller();
    // Apple's IO80211 framework pre-zeroes channel data and returns success.
    // When not associated, channel=0 / flags=0.
    // Returning error here causes airportd getCHANNEL queries to fail during init.
    memset(cd, 0, sizeof(apple80211_channel_data));
    cd->version = APPLE80211_VERSION;
    cd->channel.version = APPLE80211_VERSION;
    if (ic->ic_state == IEEE80211_S_RUN) {
        cd->channel.channel = ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);
        cd->channel.flags = ieeeChanFlag2apple(ic->ic_bss->ni_chan->ic_flags, ic->ic_bss->ni_chw);
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getSTATE(struct apple80211_state_data *sd)
{
    memset(sd, 0, sizeof(*sd));
    sd->version = APPLE80211_VERSION;
    sd->state = fHalService->get80211Controller()->ic_state;
    XYLog("DEBUG %s ic_state=%d\n", __FUNCTION__, sd->state);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getMCS_INDEX_SET(struct apple80211_mcs_index_set_data *ad)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (ad == NULL)
        return kIOReturnBadArgument;
    if (ic->ic_bss == NULL)
        return kApple80211ErrDriverNotAvailable;

    memset(ad, 0, sizeof(*ad));
    ad->version = APPLE80211_VERSION;
    size_t size = min(ARRAY_SIZE(ic->ic_bss->ni_rxmcs), ARRAY_SIZE(ad->mcs_set_map));
    bool hasAnyMcsBit = false;
    for (size_t i = 0; i < size; i++) {
        ad->mcs_set_map[i] = ic->ic_bss->ni_rxmcs[i];
        hasAnyMcsBit |= ad->mcs_set_map[i] != 0;
    }

    // AppleBCMWLANCore::getMCS_INDEX_SET delegates to
    // IO80211BssManager::getCurrentMCSSet(). That helper returns 0xe0822403
    // when there is no current BSS and 0xe00002f0 when the cached MCS set is
    // not marked valid. The port does not expose Apple's dedicated validity
    // bit, so the closest trustworthy local carrier is "current BSS exists but
    // the copied MCS map is still empty" -> no cached value.
    if (!hasAnyMcsBit)
        return kApple80211ErrNoCachedValue;

    XYLog("DEBUG %s OK\n", __FUNCTION__);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getVHT_MCS_INDEX_SET(struct apple80211_vht_mcs_index_set_data *data)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (ic->ic_bss == NULL || ic->ic_curmode < IEEE80211_MODE_11AC) {
        XYLog("DEBUG %s ic_bss=%p ic_curmode=%d → error\n", __FUNCTION__, ic->ic_bss, ic->ic_curmode);
        return kIOReturnError;
    }
    memset(data, 0, sizeof(struct apple80211_vht_mcs_index_set_data));
    data->version = APPLE80211_VERSION;
    data->mcs_map = ic->ic_bss->ni_vht_mcsinfo.tx_mcs_map;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getMCS_VHT(struct apple80211_mcs_vht_data *data)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (ic->ic_bss == NULL || ic->ic_curmode < IEEE80211_MODE_11AC) {
        XYLog("DEBUG %s ic_bss=%p ic_curmode=%d → error\n", __FUNCTION__, ic->ic_bss, ic->ic_curmode);
        return kIOReturnError;
    }
    memset(data, 0, sizeof(struct apple80211_mcs_vht_data));
    data->version = APPLE80211_VERSION;
    data->guard_interval = (ieee80211_node_supports_vht_sgi80(ic->ic_bss) || ieee80211_node_supports_vht_sgi160(ic->ic_bss)) ? APPLE80211_GI_SHORT : APPLE80211_GI_LONG;
    data->index = ic->ic_bss->ni_txmcs;
    data->nss = fHalService->getDriverInfo()->getTxNSS();
    switch (ic->ic_bss->ni_chw) {
        case IEEE80211_CHAN_WIDTH_40:
            data->bw = 40;
            break;
        case IEEE80211_CHAN_WIDTH_80:
            data->bw = 80;
            break;
        case IEEE80211_CHAN_WIDTH_80P80:
        case IEEE80211_CHAN_WIDTH_160:
            data->bw = 160;
            break;
            
        default:
            data->bw = 20;
            break;
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getRATE_SET(struct apple80211_rate_set_data *ad)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (ic->ic_bss == NULL) {
        // AppleBCMWLANCore::getRATE_SET delegates to
        // IO80211BssManager::getCurrentRateSet(), which returns 0xe0822403
        // when there is no current BSS. Tahoe previously leaked raw POSIX 6
        // here, which is not architecturally equivalent to the reference
        // producer/consumer contract.
        return kApple80211ErrDriverNotAvailable;
    }
    if (ic->ic_bss->ni_rates.rs_nrates == 0) {
        // The Apple helper splits "current BSS exists" from "cached rate-set
        // exists": an empty cached set returns 0xe00002f0 rather than success.
        return kApple80211ErrNoCachedValue;
    }
    memset(ad, 0, sizeof(*ad));
    ad->version = APPLE80211_VERSION;
    ad->num_rates = ic->ic_bss->ni_rates.rs_nrates;
    size_t size = min(ic->ic_bss->ni_rates.rs_nrates, ARRAY_SIZE(ad->rates));
    for (size_t i = 0; i < size; i++) {
        ad->rates[i].version = APPLE80211_VERSION;
        ad->rates[i].rate = ic->ic_bss->ni_rates.rs_rates[i];
        ad->rates[i].flags = 0;
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getOP_MODE(struct apple80211_opmode_data *od)
{
    XYLog("DEBUG VTABLE [475] %s\n", __FUNCTION__);
    od->version = APPLE80211_VERSION;
    od->op_mode = APPLE80211_M_STA;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getTXPOWER(struct apple80211_txpower_data *txd)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (txd == NULL)
        return kIOReturnBadArgument;
    memset(txd, 0, sizeof(*txd));
    txd->version = APPLE80211_VERSION;
    txd->txpower = ic->ic_txpower;
    txd->txpower_unit = APPLE80211_UNIT_PERCENT;
    // AppleBCMWLANCore::getTXPOWER runs through the config-backed "qtxpower"
    // transport, not through an association-gated raw state check. Until the
    // exact config query route is lifted, stop leaking raw POSIX 6 to Tahoe
    // consumers and expose the current cached txpower scalar instead. The
    // remaining producer-source mismatch stays tracked separately in Q13.
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getRATE(struct apple80211_rate_data *rd)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (ic->ic_bss == NULL || ic->ic_state != IEEE80211_S_RUN) {
        // AppleBCMWLANCore::getRATE() returns 0xe0822403 until the BSS manager
        // reports an associated current network. Returning raw 6 here was a
        // local shortcut and confused Tahoe consumers that key off the Apple
        // error code.
        XYLog("DEBUG %s ic_bss=%p ic_state=%d → 0xe0822403\n",
              __FUNCTION__, ic->ic_bss, ic->ic_state);
        return kApple80211ErrDriverNotAvailable;
    }
    int nss;
    int sgi;
    int index = 0;
    memset(rd, 0, sizeof(*rd));
    rd->version = APPLE80211_VERSION;
    rd->num_radios = 1;
    sgi = ieee80211_node_supports_sgi(ic->ic_bss);
    if (ic->ic_curmode == IEEE80211_MODE_11AC) {
        if (sgi)
            index += 1;
        nss = fHalService->getDriverInfo()->getTxNSS();
        switch (ic->ic_bss->ni_chw) {
            case IEEE80211_CHAN_WIDTH_40:
                index += 4;
                break;
            case IEEE80211_CHAN_WIDTH_80:
                index += 8;
                break;
            case IEEE80211_CHAN_WIDTH_80P80:
            case IEEE80211_CHAN_WIDTH_160:
                index += 12;
                break;

            default:
                break;
        }
        index += 2 * (nss - 1);
        const struct ieee80211_vht_rateset *rs = &ieee80211_std_ratesets_11ac[index];
        rd->rate[0] = rs->rates[ic->ic_bss->ni_txmcs % rs->nrates] / 2;
    } else if (ic->ic_curmode == IEEE80211_MODE_11N) {
        int is_40mhz = ic->ic_bss->ni_chw == IEEE80211_CHAN_WIDTH_40;
        if (sgi)
            index += 1;
        if (is_40mhz)
            index += (IEEE80211_HT_RATESET_MIMO4_SGI + 1);
        index += (ic->ic_bss->ni_txmcs / 16);
        nss = ic->ic_bss->ni_txmcs / 8 + 1;
        index += 2 * (nss - 1);
        rd->rate[0] = ieee80211_std_ratesets_11n[index].rates[ic->ic_bss->ni_txmcs % 8] / 2;
    } else {
        rd->rate[0] = ic->ic_bss->ni_rates.rs_rates[ic->ic_bss->ni_txrate];
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getBSSID(struct apple80211_bssid_data *bd)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    // Apple's IO80211 framework (FUN_ffffff8002215524) pre-zeroes BSSID and returns success.
    // When not associated, BSSID is all-zero.
    // Returning error here causes airportd to fail during init and abort auto-join.
    memset(bd, 0, sizeof(*bd));
    bd->version = APPLE80211_VERSION;
    if (ic->ic_state == IEEE80211_S_RUN) {
        memcpy(bd->bssid.octet, ic->ic_bss->ni_bssid, APPLE80211_ADDR_LEN);
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getHW_ADDR(struct apple80211_hw_mac_address *data)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    // AppleBCMWLANCore::getHW_ADDR writes version=1 followed by the six-byte
    // hardware address, and IO80211Family's
    // WCLDeviceConfiguration::setHwMacAddr(...) consumes the same packed
    // `u32 version + u8[6]` layout. Leaving slot [511] unsupported was
    // therefore an architectural mismatch, not an optional capability.
    memset(data, 0, sizeof(*data));
    data->version = APPLE80211_VERSION;
    IEEE80211_ADDR_COPY(data->hw_addr, ic->ic_myaddr);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getVHT_CAPABILITY(struct apple80211_vht_capability *data)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    // AppleBCMWLANCore::getVHT_CAPABILITY does not use a generic unsupported
    // path. It returns 0x2d when the PHY gate rejects VHT, and otherwise
    // copies a 14-byte VHT capability IE body into the packed
    // apple80211_vht_capability payload after `version`. Our local ABI now
    // mirrors that recovered contract, so Tahoe should expose the same IE
    // shape instead of leaving slot [484] on kIOReturnUnsupported.
    if (data == nullptr)
        return kIOReturnBadArgument;
    if ((ic->ic_flags & IEEE80211_F_VHTON) == 0 || ic->ic_vhtcaps == 0)
        return 45;

    memset(data, 0, sizeof(*data));
    data->version = APPLE80211_VERSION;
    data->ie = IEEE80211_ELEMID_VHT_CAP;
    data->len = sizeof(struct ieee80211_ie_vhtcap) - 2;
    data->vht_cap_info = ic->ic_vhtcaps;
    data->rx_mcs_map = ic->ic_vht_rx_mcs_map;
    data->rx_highest = ic->ic_vht_rx_highest;
    data->tx_mcs_map = ic->ic_vht_tx_mcs_map;
    data->tx_highest = ic->ic_vht_tx_highest;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getHT_CAPABILITY(struct apple80211_ht_capability *data)
{
    struct ieee80211com *ic = fHalService->get80211Controller();

    // Remote otool on AppleBCMWLANCoreMac shows getHT_CAPABILITY is a real
    // producer: it first refreshes cached HT/VHT capability state and then
    // copies a contiguous 0x1c-byte HT capability IE body into caller
    // offsets +0x4..+0x1f. Our 802.11 stack already carries the same source
    // fields in `ieee80211com`, and the local `ieee80211_add_htcaps()`
    // generator writes them in the same order, so slot [481] must expose that
    // packed carrier instead of generic unsupported.
    if (data == nullptr)
        return kApple80211ErrInvalidArgumentRaw;

    memset(data, 0, sizeof(*data));
    data->version = APPLE80211_VERSION;
    data->hc_id = IEEE80211_ELEMID_HTCAPS;
    data->hc_len = sizeof(struct ieee80211_ie_htcap) - 2;
    data->hc_cap = ic->ic_htcaps;
    data->hc_param = ic->ic_ampdu_params;
    memcpy(data->hc_mcsset, ic->ic_sup_mcs, sizeof(ic->ic_sup_mcs));
    LE_WRITE_2(data->hc_mcsset + 10, ic->ic_max_rxrate & IEEE80211_MCS_RX_RATE_HIGH);
    data->hc_mcsset[12] = ic->ic_tx_mcs_set;
    data->hc_extcap = ic->ic_htxcaps;
    data->hc_txbf = ic->ic_txbfcaps;
    data->hc_antenna = ic->ic_aselcaps;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getHE_CAPABILITY(struct apple80211_he_capability *data)
{
    struct ieee80211com *ic = fHalService->get80211Controller();

    // AppleBCMWLANCore::getHE_CAPABILITY is not an unsupported slot. It
    // returns 0x2d when the PHY/capability gate rejects HE and otherwise
    // writes only three discontiguous fields inside a 0x24-byte opaque
    // carrier. Keep that shape intact instead of inventing a semantic HE IE
    // schema that the local headers never recovered.
    if (data == nullptr)
        return kIOReturnBadArgument;
    if ((ic->ic_flags & IEEE80211_F_HEON) == 0)
        return 45;

    memset(data, 0, sizeof(*data));
    data->version = APPLE80211_VERSION;
    data->capability_word = 0x0b00;
    data->capability_byte = 0x26;
    data->capability_tail = 0xfffafffafffafffaULL;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getGUARD_INTERVAL(apple80211_guard_interval_data *data)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    struct ieee80211_node *ni = ic->ic_bss;

    // AppleBCMWLANCore::getGUARD_INTERVAL is a real producer, not a generic
    // unsupported slot: NULL returns 0xe00002c2, and the normal path derives
    // the interval from cached "nrate" state with a deterministic fallback to
    // long GI (800 ns) when no recognized short-GI encoding is available.
    if (data == nullptr)
        return static_cast<IOReturn>(0xe00002c2);

    memset(data, 0, sizeof(*data));
    data->version = APPLE80211_VERSION;
    data->interval = APPLE80211_GI_LONG;

    if (ni == nullptr)
        return kIOReturnSuccess;

    switch (ni->ni_chw) {
        case IEEE80211_CHAN_WIDTH_40:
            if (ieee80211_node_supports_ht_sgi40(ni))
                data->interval = APPLE80211_GI_SHORT;
            break;
        case IEEE80211_CHAN_WIDTH_80:
            if (ieee80211_node_supports_vht_sgi80(ni))
                data->interval = APPLE80211_GI_SHORT;
            break;
        case IEEE80211_CHAN_WIDTH_80P80:
        case IEEE80211_CHAN_WIDTH_160:
            if (ieee80211_node_supports_vht_sgi160(ni))
                data->interval = APPLE80211_GI_SHORT;
            break;
        case IEEE80211_CHAN_WIDTH_20:
        case IEEE80211_CHAN_WIDTH_20_NOHT:
        default:
            if (ieee80211_node_supports_ht_sgi20(ni))
                data->interval = APPLE80211_GI_SHORT;
            break;
    }

    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getP2P_DEVICE_CAPABILITY(apple80211_p2p_device_capability *data)
{
    // AppleBCMWLANCore::getP2P_DEVICE_CAPABILITY zeroes the one-byte carrier
    // and only defers into AppleBCMWLANNANInterface when a NAN owner exists.
    // This port currently has no NAN object at all, so the Apple-shaped path
    // is the zeroed fast-path, not generic unsupported.
    if (data == nullptr)
        return kIOReturnBadArgument;

    data->capability = 0;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getPRIVATE_MAC(apple80211_private_mac_data *data)
{
    // AppleBCMWLANCore::getPRIVATE_MAC does not use kIOReturnUnsupported. It
    // rejects NULL with raw 0x16 and otherwise writes a packed 0x1c carrier
    // covering offsets +0x4..+0x1b. The exact semantic names of the trailing
    // "scanmac" fields are still only partially recovered, so keep the ABI
    // offset-accurate and state-backed instead of inventing names or collapsing
    // the slot into a generic unsupported path.
    if (data == nullptr)
        return kApple80211ErrInvalidArgumentRaw;

    memset(data, 0, sizeof(*data));
    data->version = APPLE80211_VERSION;
    data->enabled = 0;
    data->scanmac_state = cachedPrivateMacState;
    data->timeout_seconds = cachedPrivateMacTimeoutSeconds;
    memcpy(data->primary_mac, cachedPrivateMacPrimary, sizeof(data->primary_mac));
    memcpy(data->secondary_mac, cachedPrivateMacSecondary, sizeof(data->secondary_mac));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getTHERMAL_INDEX(apple80211_thermal_index_t *data)
{
    // AppleBCMWLANCore::getTHERMAL_INDEX is a plain core-state carrier getter:
    // it writes a 32-bit scalar at caller offset +4 from core-state base +0x0.
    // Tahoe should therefore expose a real `version + u32` payload here rather
    // than leaving slot [500] on kIOReturnUnsupported.
    if (data == nullptr)
        return kIOReturnBadArgument;

    memset(data, 0, sizeof(*data));
    data->version = APPLE80211_VERSION;
    data->thermal_index = cachedThermalIndex;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getPOWER_BUDGET(apple80211_power_budget_t *data)
{
    // AppleBCMWLANCore::getPOWER_BUDGET is the neighboring scalar carrier:
    // it writes a 32-bit value at caller offset +4 from core-state base +0x4.
    // The important Tahoe mismatch was architectural reachability/ABI, not a
    // complex helper path, so slot [503] must return the same 8-byte carrier
    // shape instead of generic unsupported.
    if (data == nullptr)
        return kIOReturnBadArgument;

    memset(data, 0, sizeof(*data));
    data->version = APPLE80211_VERSION;
    data->power_budget = cachedPowerBudget;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getOFFLOAD_TCPKA_ENABLE(apple80211_offload_tcpka_enable_t *data)
{
    // Remote otool recovered the full getter body:
    // - NULL -> 0xe00002c2
    // - feature/config gate failure -> 0xe00002c7
    // - keepalive object present -> write enabled flag at caller +0x4
    //
    // Tahoe therefore needs the exact carrier ABI here even before the deeper
    // keepalive object path is fully lifted. Returning the Apple unsupported
    // code when the local keepalive producer is absent is still materially more
    // correct than leaving slot [504] on generic kIOReturnUnsupported with the
    // wrong payload type.
    if (data == nullptr)
        return static_cast<IOReturn>(0xe00002c2);
    if (!cachedTcpkaOffloadSupported)
        return static_cast<IOReturn>(0xe00002c7);

    memset(data, 0, sizeof(*data));
    data->version = APPLE80211_VERSION;
    data->enabled = cachedTcpkaOffloadEnabled ? 1U : 0U;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getLQM_CONFIG(apple80211_lqm_config_t *data)
{
    // The Tahoe LQM getter is a real producer, not an unsupported slot.
    // IO80211LQMData exposes a stable 0x24-byte carrier and AppleBCMWLANCore
    // forwards the same public ABI from its own owner state. The local port
    // does not have the hidden Broadcom LQM owner, but it can still preserve
    // the exact caller-visible carrier and validation contract.
    if (data == nullptr)
        return kIOReturnBadArgument;

    if (!hasCachedLqmConfig)
        initializeTahoeLqmConfig(&cachedLqmConfig);

    memcpy(data, &cachedLqmConfig, sizeof(*data));
    data->version = APPLE80211_VERSION;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getLQM_SUMMARY(apple80211_lqm_summary *data)
{
    // IO80211LQMData::getLQM_SUMMARY simply zeroes a fixed 0x15a0-byte caller
    // buffer. Tahoe should therefore export the same summary blob ABI instead
    // of leaving slot [520] on generic unsupported.
    if (data == nullptr)
        return kIOReturnBadArgument;

    memset(data, 0, sizeof(*data));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getRSSI(struct apple80211_rssi_data *rd)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    XYLog("DEBUG VTABLE [476] %s ic_state=%d ic_bss=%p\n",
          __FUNCTION__, ic->ic_state, ic->ic_bss);
    if (ic->ic_bss == NULL) {
        // Apple delegates RSSI reads to IO80211BssManager::getCurrentRSSI(),
        // which returns 0xe0822403 when there is no current BSS. Tahoe should
        // not collapse that state into raw POSIX 6.
        return kApple80211ErrDriverNotAvailable;
    }
    memset(rd, 0, sizeof(*rd));
    rd->version = APPLE80211_VERSION;
    rd->num_radios = 1;
    rd->rssi_unit = APPLE80211_UNIT_DBM;
    rd->rssi[0] = rd->aggregate_rssi
    = rd->rssi_ext[0]
    = rd->aggregate_rssi_ext
    = -(0 - IWM_MIN_DBM - ic->ic_bss->ni_rssi);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getRSN_IE(struct apple80211_rsn_ie_data *data)
{
#ifdef USE_APPLE_SUPPLICANT
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (ic->ic_bss == NULL || ic->ic_bss->ni_rsnie == NULL) {
        return kIOReturnError;
    }
    data->version = APPLE80211_VERSION;
    if (ic->ic_rsn_ie_override[1] > 0) {
        data->len = 2 + ic->ic_rsn_ie_override[1];
        memcpy(data->ie, ic->ic_rsn_ie_override, data->len);
    }
    else {
        data->len = 2 + ic->ic_bss->ni_rsnie[1];
        memcpy(data->ie, ic->ic_bss->ni_rsnie, data->len);
    }
    return kIOReturnSuccess;
#else
    return kIOReturnUnsupported;
#endif
}

IOReturn AirportItlwmSkywalkInterface::
setRSN_IE(struct apple80211_rsn_ie_data *data)
{
#ifdef USE_APPLE_SUPPLICANT
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (!data)
        return kIOReturnError;
    static_assert(sizeof(ic->ic_rsn_ie_override) == APPLE80211_MAX_RSN_IE_LEN, "Max RSN IE length mismatch");
    memcpy(ic->ic_rsn_ie_override, data->ie, APPLE80211_MAX_RSN_IE_LEN);
    if (ic->ic_state == IEEE80211_S_RUN && ic->ic_bss != nullptr)
        ieee80211_save_ie(data->ie, &ic->ic_bss->ni_rsnie);
    return kIOReturnSuccess;
#else
    return kIOReturnUnsupported;
#endif
}

IOReturn AirportItlwmSkywalkInterface::
getAP_IE_LIST(struct apple80211_ap_ie_data *data)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (!data)
        return kIOReturnError;
    if (ic->ic_bss == NULL || ic->ic_bss->ni_rsnie_tlv == NULL || ic->ic_bss->ni_rsnie_tlv_len == 0 || ic->ic_bss->ni_rsnie_tlv_len > data->len || ic->ic_bss->ni_rsnie_tlv_len > 1024)
        return kIOReturnError;
    data->version = APPLE80211_VERSION;
    data->len = ic->ic_bss->ni_rsnie_tlv_len;
    memcpy(data->ie_data, ic->ic_bss->ni_rsnie_tlv, data->len);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getNOISE(struct apple80211_noise_data *nd)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (nd == NULL)
        return kIOReturnBadArgument;
    if (ic->ic_bss == NULL)
        return kApple80211ErrDriverNotAvailable;

    int32_t noise = -fHalService->getDriverInfo()->getBSSNoise();
    if (noise == 0)
        return 0x66;

    memset(nd, 0, sizeof(*nd));
    nd->version = APPLE80211_VERSION;
    nd->num_radios = 1;
    nd->noise[0] = nd->aggregate_noise = noise;
    nd->noise_unit = APPLE80211_UNIT_DBM;
    XYLog("DEBUG %s noise=%d\n", __FUNCTION__, nd->noise[0]);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getPOWERSAVE(struct apple80211_powersave_data *pd)
{
    XYLog("DEBUG VTABLE [472] %s\n", __FUNCTION__);
    pd->version = APPLE80211_VERSION;
    // AppleBCMWLANCore::getPOWERSAVE on Tahoe reads back a cached power-save
    // mode from core state instead of reporting the IOC as unsupported.  Live
    // 26.3 logs showed WCL sending APPLE80211_IOC_POWERSAVE with level 7 very
    // early during bring-up; our old kIOReturnUnsupported path made WCL log
    // "arg->powersave_level = 7 not supported" and diverged from the reference
    // contract before scan-complete delivery even mattered.
    pd->powersave_level = cachedPowersaveLevel;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setPOWERSAVE(struct apple80211_powersave_data *pd)
{
    XYLog("DEBUG VTABLE [547] %s level=%u\n", __FUNCTION__, pd ? pd->powersave_level : 0);
    if (pd == NULL)
        return kIOReturnBadArgument;

    // Recovered Apple contract:
    // - Tahoe WCL expects APPLE80211_IOC_POWERSAVE to succeed during startup.
    // - AppleBCMWLANCore::getPOWERSAVE returns the last accepted 32-bit level.
    // Mirroring that 1:1 externally means accepting the requested policy here
    // and returning the same cached value from getPOWERSAVE later.
    cachedPowersaveLevel = pd->powersave_level;
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getNSS(struct apple80211_nss_data *data)
{
    XYLog("DEBUG VTABLE [510] %s nss=%d\n", __FUNCTION__, fHalService->getDriverInfo()->getTxNSS());
    memset(data, 0, sizeof(*data));
    data->version = APPLE80211_VERSION;
    data->nss = fHalService->getDriverInfo()->getTxNSS();
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setASSOCIATE(struct apple80211_assoc_data *ad)
{
    RT2_SET(3); sRT.assocCount++;
    XYLog("%s [%s] mode=%d ad_auth_lower=%d ad_auth_upper=%d rsn_ie_len=%d%s%s%s%s%s%s%s\n", __FUNCTION__, ad->ad_ssid, ad->ad_mode, ad->ad_auth_lower, ad->ad_auth_upper, ad->ad_rsn_ie_len,
          (ad->ad_flags & 2) ? ", Instant Hotspot" : "",
          (ad->ad_flags & 4) ? ", Auto Instant Hotspot" : "",
          (ad->ad_rsn_ie[APPLE80211_MAX_RSN_IE_LEN] & 1) ? ", don't disassociate" : "",
          (ad->ad_rsn_ie[APPLE80211_MAX_RSN_IE_LEN] & 2) ? ", don't blacklist" : "",
          (ad->ad_rsn_ie[APPLE80211_MAX_RSN_IE_LEN] & 4) ? ", closed Network" : "",
          (ad->ad_rsn_ie[APPLE80211_MAX_RSN_IE_LEN] & 8) ? ", 802.1X" : "",
          (ad->ad_rsn_ie[APPLE80211_MAX_RSN_IE_LEN] & 0x20) ? ", force BSSID" : "");
    
    struct apple80211_rsn_ie_data rsn_ie_data;
    struct apple80211_authtype_data auth_type_data;
    struct ieee80211com *ic = fHalService->get80211Controller();

    if (!ad)
        return kIOReturnError;
    
    XYLog("DEBUG %s ic_state=%d ic_opmode=%d\n", __FUNCTION__, ic->ic_state, ic->ic_opmode);
    if (ic->ic_state < IEEE80211_S_SCAN) {
        XYLog("DEBUG %s SKIP: ic_state=%d < SCAN\n", __FUNCTION__, ic->ic_state);
        return kIOReturnSuccess;
    }

    if (ic->ic_state == IEEE80211_S_ASSOC || ic->ic_state == IEEE80211_S_AUTH) {
        XYLog("DEBUG %s SKIP: ic_state=%d (already in ASSOC/AUTH)\n", __FUNCTION__, ic->ic_state);
        return kIOReturnSuccess;
    }

    if (ad->ad_mode != APPLE80211_AP_MODE_IBSS) {
        disassocIsVoluntary = false;
        auth_type_data.version = APPLE80211_VERSION;
        auth_type_data.authtype_upper = ad->ad_auth_upper;
        auth_type_data.authtype_lower = ad->ad_auth_lower;
        setAUTH_TYPE(&auth_type_data);
        rsn_ie_data.version = APPLE80211_VERSION;
        rsn_ie_data.len = ad->ad_rsn_ie[1] + 2;
        memcpy(rsn_ie_data.ie, ad->ad_rsn_ie, rsn_ie_data.len);
        setRSN_IE(&rsn_ie_data);

        associateSSID(ad->ad_ssid, ad->ad_ssid_len, ad->ad_bssid, ad->ad_auth_lower, ad->ad_auth_upper, ad->ad_key.key, ad->ad_key.key_len, ad->ad_key.key_index);
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setDISASSOCIATE(void *ad)
{
    RT2_SET(7);
    struct ieee80211com *ic = fHalService->get80211Controller();
    XYLog("DEBUG %s ic_state=%d\n", __FUNCTION__, ic->ic_state);

    if (ic->ic_state < IEEE80211_S_SCAN) {
        XYLog("DEBUG %s SKIP: ic_state=%d < SCAN\n", __FUNCTION__, ic->ic_state);
        return kIOReturnSuccess;
    }

    if (ic->ic_state > IEEE80211_S_AUTH && ic->ic_bss != NULL)
        IEEE80211_SEND_MGMT(ic, ic->ic_bss, IEEE80211_FC0_SUBTYPE_DEAUTH, IEEE80211_REASON_AUTH_LEAVE);

    if (ic->ic_state == IEEE80211_S_ASSOC || ic->ic_state == IEEE80211_S_AUTH) {
        XYLog("DEBUG %s SKIP: ic_state=%d (ASSOC/AUTH)\n", __FUNCTION__, ic->ic_state);
        return kIOReturnSuccess;
    }
    
    disassocIsVoluntary = true;

    ieee80211_del_ess(ic, nullptr, 0, 1);
    ieee80211_deselect_ess(ic);
#ifdef USE_APPLE_SUPPLICANT
    ic->ic_rsn_ie_override[1] = 0;
#endif
    ic->ic_assoc_status = APPLE80211_STATUS_UNAVAILABLE;
    ic->ic_deauth_reason = APPLE80211_REASON_ASSOC_LEAVING;
    ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getSUPPORTED_CHANNELS(struct apple80211_sup_channel_data *ad)
{
    XYLog("DEBUG VTABLE [477] %s\n", __FUNCTION__);
    if (!ad)
        return kIOReturnError;
    ad->version = APPLE80211_VERSION;
    ad->num_channels = 0;
    struct ieee80211com *ic = fHalService->get80211Controller();
    for (int i = 0; i < IEEE80211_CHAN_MAX; i++) {
        if (ic->ic_channels[i].ic_freq != 0) {
            ad->supported_channels[ad->num_channels].channel = ieee80211_chan2ieee(ic, &ic->ic_channels[i]);
            ad->supported_channels[ad->num_channels].flags = ieeeChanFlag2appleScanFlagVentura(ic->ic_channels[i].ic_flags);
            ad->num_channels++;
        }
    }
    XYLog("DEBUG %s num_channels=%d ic_state=%d\n",
          __FUNCTION__, ad->num_channels, ic->ic_state);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getLOCALE(struct apple80211_locale_data *ld)
{
    if (!ld)
        return kIOReturnError;
    ld->version = APPLE80211_VERSION;
    ld->locale  = APPLE80211_LOCALE_FCC;
    
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getDEAUTH(struct apple80211_deauth_data *da)
{
    if (!da)
        return kIOReturnError;
    da->version = APPLE80211_VERSION;
    struct ieee80211com *ic = fHalService->get80211Controller();
    da->deauth_reason = ic->ic_deauth_reason;
    XYLog("DEBUG %s deauth_reason=%d ic_state=%d\n", __FUNCTION__, da->deauth_reason, ic->ic_state);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getASSOCIATION_STATUS(struct apple80211_assoc_status_data *hv)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    
    if (!hv)
        return kIOReturnError;
    memset(hv, 0, sizeof(*hv));
    hv->version = APPLE80211_VERSION;
    if (ic->ic_state == IEEE80211_S_RUN)
        hv->status = APPLE80211_STATUS_SUCCESS;
    else
        hv->status = APPLE80211_STATUS_UNAVAILABLE;
    XYLog("DEBUG %s status=%d ic_state=%d\n", __FUNCTION__, hv->status, ic->ic_state);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setCLEAR_PMKSA_CACHE(void *req)
{
    XYLog("%s\n", __FUNCTION__);
    struct ieee80211com *ic = fHalService->get80211Controller();
    //if doing background or active scan, don't free nodes.
    if ((ic->ic_flags & IEEE80211_F_BGSCAN) || (ic->ic_flags & IEEE80211_F_ASCAN))
        return kIOReturnSuccess;
    ieee80211_free_allnodes(ic, 0);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setDEAUTH(struct apple80211_deauth_data *da)
{
    XYLog("DEBUG %s reason=%d\n", __FUNCTION__, da ? da->deauth_reason : -1);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getMCS(struct apple80211_mcs_data* md)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (md == NULL)
        return kIOReturnBadArgument;
    if (ic->ic_state == IEEE80211_S_RUN && ic->ic_bss != NULL)
        cachedCurrentMcs = static_cast<uint32_t>(ic->ic_bss->ni_txmcs);

    // The recovered Apple getMCS producer is a cached scalar carrier, not the
    // old "associated or raw 6" branch that Tahoe was accidentally exposing.
    md->version = APPLE80211_VERSION;
    md->index = cachedCurrentMcs;
    XYLog("DEBUG %s mcs=%u ic_state=%d ic_bss=%p\n",
          __FUNCTION__, md->index, ic->ic_state, ic->ic_bss);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getLINK_CHANGED_EVENT_DATA(struct apple80211_link_changed_event_data *ed)
{
    if (ed == nullptr)
        return 16;
    
    struct ieee80211com *ic = fHalService->get80211Controller();
    
    bzero(ed, sizeof(apple80211_link_changed_event_data));
    ed->isLinkDown = !(instance->currentStatus & kIONetworkLinkActive);
    if (ed->isLinkDown) {
        ed->voluntary = disassocIsVoluntary;
        ed->reason = APPLE80211_LINK_DOWN_REASON_DEAUTH;
    } else
        ed->rssi = -(0 - IWM_MIN_DBM - ic->ic_bss->ni_rssi);
    XYLog("Link %s, reason: %d, voluntary: %d\n", ed->isLinkDown ? "down" : "up", ed->reason, ed->voluntary);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setSCAN_REQ(struct apple80211_scan_data *sd)
{
    RT2_SET(2); sRT.scanReqCount++;
    struct ieee80211com *ic = fHalService->get80211Controller();
    XYLog("DEBUG %s Type: %u BSS Type: %u PHY Mode: %u Dwell: %u Rest: %u Channels: %u SSID: %s ic_state=%d\n",
          __FUNCTION__,
          sd->scan_type,
          sd->bss_type,
          sd->phy_mode,
          sd->dwell_time,
          sd->rest_time,
          sd->num_channels,
          sd->ssid,
          ic->ic_state);
    if (fScanResultWrapping)
        return 22;
    if (ic->ic_state <= IEEE80211_S_INIT)
        return 22;

    /*
     * Reset SCAN_RESULT iterator — airportd/IO80211 framework reads
     * SCAN_RESULT immediately after SCAN_REQ returns success.  If
     * ic_tree already has nodes from the internal scan, the framework
     * can iterate them right away.  Without this reset, a stale
     * fScanResultWrapping=true from a previous cycle causes SCAN_RESULT
     * to return 5 ("end") before returning any nodes.
     */
    fNextNodeToSend = NULL;
    fScanResultWrapping = false;

    if (sd->scan_type == APPLE80211_SCAN_TYPE_FAST || sd->scan_type == APPLE80211_SCAN_TYPE_PASSIVE) {
        if (scanSource) {
            scanSource->setTimeoutMS(100);
            scanSource->enable();
        }
        return kIOReturnSuccess;
    }
    ieee80211_begin_cache_bgscan(&ic->ic_ac.ac_if);
    if (scanSource) {
        scanSource->setTimeoutMS(100);
        scanSource->enable();
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_TRIGGER_CC(triggerCC *data)
{
    if (!data)
        return kIOReturnBadArgument;

    const auto *snapshot = reinterpret_cast<const triggerCCSnapshot *>(data);
    const uint8_t *raw = reinterpret_cast<const uint8_t *>(data);
    const uint32_t mode = *reinterpret_cast<const uint32_t *>(raw + 0x8);
    const uint32_t trigger = *reinterpret_cast<const uint32_t *>(raw + 0x4);
    const uint32_t channelMetric = *reinterpret_cast<const uint32_t *>(raw + 0x10);

    // Tahoe 26.x calls APPLE80211_IOC_WCL_TRIGGER_CC during the scan manager
    // bring-up path.  The Apple producer does not treat it as optional:
    // AppleBCMWLANCore::setWCL_TRIGGER_CC first copies the first four qwords of
    // the request into adapter-owned state, then accepts mode 0/1 and returns
    // 0xe00002bc only for any other mode.  Returning unsupported here is what
    // produced the live INTERNAL WCL_TRIGGER_CC -> 0xe00002c7 failure.
    memcpy(cachedTriggerCC, snapshot, sizeof(*snapshot));
    cachedTriggerCCMode = mode;
    hasCachedTriggerCC = true;

    XYLog("DEBUG %s mode=%u trigger=%u channel_metric=%u\n",
          __FUNCTION__, mode, trigger, channelMetric);

    if (mode == 0 || mode == 1)
        return kIOReturnSuccess;
    return kIOReturnBadArgumentTahoe;
}

IOReturn AirportItlwmSkywalkInterface::
setOS_FEATURE_FLAGS(apple80211_feature_flags *data)
{
    if (!data)
        return kIOReturnBadArgumentTahoe;

    const uint64_t flags = *reinterpret_cast<const uint64_t *>(data);

    // AppleBCMWLANCore::setOS_FEATURE_FLAGS is not an ack-only slot. It first
    // persists the incoming 64-bit word, then derives multiple cached booleans
    // and fans out follow-up configuration (DynSAR, 6G, KVR, AOP scan-forward,
    // adaptive 11r). We do not collapse that producer path back into an inline
    // success stub; the raw feature word must remain driver-owned state.
    cachedOSFeatureFlags = flags;

    XYLog("WCL [611] %s flags=0x%llx dynsar=%u six_ghz=%u adaptive11r=%u\n",
          __FUNCTION__,
          static_cast<unsigned long long>(flags),
          static_cast<unsigned int>((flags >> 6) & 1ULL),
          static_cast<unsigned int>((flags >> 7) & 1ULL),
          static_cast<unsigned int>((flags >> 28) & 1ULL));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setDHCP_RENEWAL_DATA(apple80211_dhcp_renewal_data *data)
{
    if (!data)
        return kIOReturnBadArgumentTahoe;

    // Apple caches the first byte as a persistent bool instead of treating the
    // IOC as disposable. Carry the same state locally so later keepalive / PM
    // work does not lose the DHCP-renewal edge.
    cachedDhcpRenewalData = *reinterpret_cast<const uint8_t *>(data) != 0;
    XYLog("WCL [612] %s enabled=%u\n", __FUNCTION__,
          static_cast<unsigned int>(cachedDhcpRenewalData));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setBATTERY_POWERSAVE_CONFIG(apple80211_battery_ps_config *data)
{
    if (!data)
        return kIOReturnBadArgumentTahoe;

    // Apple stores the first dword and hands it into the battery-save path.
    // Even before the deeper power manager parity is finished, this IOC must
    // remain a state carrier rather than a blind success stub.
    cachedBatteryPowerSaveMode = *reinterpret_cast<const uint32_t *>(data);
    XYLog("WCL [613] %s mode=%u\n", __FUNCTION__, cachedBatteryPowerSaveMode);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setPOWER_PROFILE(apple80211_power_profile *data)
{
    if (!data)
        return kIOReturnBadArgumentTahoe;

    // AppleBCMWLANCore caches the first power-profile dword at +0x29f0 before
    // dispatching into its power-policy vtable. Preserve the same cached owner
    // state here instead of dropping the profile on the floor.
    cachedPowerProfile = *reinterpret_cast<const uint32_t *>(data);
    XYLog("WCL [619] %s profile=%u\n", __FUNCTION__, cachedPowerProfile);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setIPV4_PARAMS(apple80211_ipv4_params *data)
{
    if (!data)
        return kIOReturnBadArgumentTahoe;

    const auto *ipv4 = reinterpret_cast<const tahoeIPv4ParamsContract *>(data);

    // The Apple producer persists IPv4/mask/router fields in core state,
    // triggers IPv4 notifications, and only then decides whether keepalive data
    // needs refreshing. Returning success without carrying the values breaks the
    // producer/consumer contract even if user-visible network behavior has not
    // reached that path yet.
    cachedIPv4Address = ipv4->address;
    cachedIPv4Netmask = ipv4->netmask;
    cachedIPv4Reserved = 0;
    cachedIPv4Gateway = ipv4->gateway;
    cachedIPv4GatewayTail = ipv4->gatewayTail;

    XYLog("WCL [624] %s addr=0x%08x mask=0x%08x gw=0x%08x tail=0x%04x keepalive=%u\n",
          __FUNCTION__, cachedIPv4Address, cachedIPv4Netmask, cachedIPv4Gateway,
          cachedIPv4GatewayTail,
          static_cast<unsigned int>(cachedIPv4Address != 0 && cachedIPv4Netmask != 0));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setIPV6_PARAMS(apple80211_ipv6_params *data)
{
    if (!data)
        return kIOReturnBadArgumentTahoe;

    const auto *ipv6 = reinterpret_cast<const tahoeIPv6ParamsHeader *>(data);
    cachedIPv6Count = MIN(ipv6->count, static_cast<uint32_t>(10));
    memset(cachedIPv6Addresses, 0, sizeof(cachedIPv6Addresses));
    memset(cachedIPv6LinkLocalAddress, 0, sizeof(cachedIPv6LinkLocalAddress));
    if (cachedIPv6Count != 0)
        memcpy(cachedIPv6Addresses, ipv6->addresses, cachedIPv6Count * sizeof(cachedIPv6Addresses[0]));

    // Apple seeds a dedicated link-local fe80:: prefix after refreshing the
    // cached IPv6 table. Keep that exact state edge so later consumers do not
    // observe "success" with no link-local cache behind it.
    cachedIPv6LinkLocalAddress[0] = 0xfe;
    cachedIPv6LinkLocalAddress[1] = 0x80;
    if (cachedIPv6Count != 0)
        memcpy(&cachedIPv6LinkLocalAddress[8], &cachedIPv6Addresses[0][8], 8);

    XYLog("WCL [636] %s count=%u first=%02x:%02x:%02x:%02x ll=%02x%02x::\n",
          __FUNCTION__, cachedIPv6Count,
          cachedIPv6Count ? cachedIPv6Addresses[0][0] : 0,
          cachedIPv6Count ? cachedIPv6Addresses[0][1] : 0,
          cachedIPv6Count ? cachedIPv6Addresses[0][2] : 0,
          cachedIPv6Count ? cachedIPv6Addresses[0][3] : 0,
          cachedIPv6LinkLocalAddress[0], cachedIPv6LinkLocalAddress[1]);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setINFRA_ENUMERATED(apple80211_infra_enumerated *data)
{
    if (!data)
        return kIOReturnBadArgumentTahoe;

    // Apple's contract here really is minimal: validate non-null and succeed.
    // Our old kIOReturnError path still diverged from that producer shape.
    cachedInfraEnumerated = true;
    XYLog("WCL [637] %s enumerated=1\n", __FUNCTION__);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_SCAN_REQ(apple80211ScanRequest *req)
{
    RT2_SET(2); sRT.scanReqCount++;
    struct ieee80211com *ic = fHalService->get80211Controller();
    const uint8_t *raw = reinterpret_cast<const uint8_t *>(req);

    // Dump key fields from apple80211ScanRequest (total 0x1598 bytes)
    // Offsets from decompiled AppleBCMWLAN/IO80211Family
    uint32_t num_channels = raw ? *reinterpret_cast<const uint32_t *>(raw + 0x54) : 0;
    uint32_t scan_type = raw ? *reinterpret_cast<const uint32_t *>(raw + 0x44) : 0;
    uint32_t bss_type = raw ? *reinterpret_cast<const uint32_t *>(raw + 0x14) : 0;
    uint32_t ssid_len = raw ? *reinterpret_cast<const uint32_t *>(raw + 0x20) : 0;
    XYLog("DEBUG %s ic_state=%d req=%p scan_type=%u bss_type=%u ssid_len=%u num_channels=%u\n",
          __FUNCTION__, ic->ic_state, req, scan_type, bss_type, ssid_len, num_channels);

    // Dump first 96 bytes of struct for offset verification
    if (req) {
        XYLog("DEBUG %s hex[0x00-0x2F]: %s\n", __FUNCTION__, hexdump((uint8_t*)raw, 48));
        XYLog("DEBUG %s hex[0x30-0x5F]: %s\n", __FUNCTION__, hexdump((uint8_t*)raw + 0x30, 48));
    }

    if (!req)
        return kIOReturnBadArgument;
    if (fScanResultWrapping)
        return 22;
    if (ic->ic_state <= IEEE80211_S_INIT)
        return 22;

    ieee80211_begin_cache_bgscan(&ic->ic_ac.ac_if);
    if (scanSource) {
        scanSource->setTimeoutMS(100);
        scanSource->enable();
    }
    XYLog("DEBUG %s → scan triggered OK\n", __FUNCTION__);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_ASSOCIATE(apple80211AssocCandidates *candidates)
{
    RT2_SET(3); sRT.assocCount++;
    if (!candidates)
        return kIOReturnBadArgument;

    struct ieee80211com *ic = fHalService->get80211Controller();
    const uint8_t *raw = reinterpret_cast<const uint8_t *>(candidates);

    // Extract fields from apple80211AssocCandidates struct (offsets from decompiled AppleBCMWLAN)
    uint16_t ap_mode = *reinterpret_cast<const uint16_t *>(raw + 0x0C);
    uint32_t auth_lower = *reinterpret_cast<const uint32_t *>(raw + 0x10);
    uint32_t auth_upper = *reinterpret_cast<const uint32_t *>(raw + 0x14);
    uint32_t ssid_len = *reinterpret_cast<const uint32_t *>(raw + 0x1C);
    const uint8_t *ssid = raw + 0x20;
    uint16_t rsn_ie_len = *reinterpret_cast<const uint16_t *>(raw + 0xD4);
    const uint8_t *rsn_ie = raw + 0xD6;
    const struct ether_addr *bssid = reinterpret_cast<const struct ether_addr *>(raw + 0x1F4);

    if (ssid_len > APPLE80211_MAX_SSID_LEN)
        ssid_len = APPLE80211_MAX_SSID_LEN;

    char ssid_str[APPLE80211_MAX_SSID_LEN + 1];
    memcpy(ssid_str, ssid, ssid_len);
    ssid_str[ssid_len] = '\0';

    XYLog("DEBUG %s [%s] mode=%d auth_lower=%d auth_upper=%d rsn_ie_len=%d ic_state=%d\n",
          __FUNCTION__, ssid_str, ap_mode, auth_lower, auth_upper, rsn_ie_len, ic->ic_state);
    XYLog("DEBUG %s BSSID=%02x:%02x:%02x:%02x:%02x:%02x\n", __FUNCTION__,
          bssid->octet[0], bssid->octet[1], bssid->octet[2],
          bssid->octet[3], bssid->octet[4], bssid->octet[5]);

    // Dump first 64 bytes and BSSID region for offset verification
    XYLog("DEBUG %s hex[0x00-0x2F]: %s\n", __FUNCTION__, hexdump((uint8_t*)raw, 48));
    XYLog("DEBUG %s hex[0x1F0-0x21F]: %s\n", __FUNCTION__, hexdump((uint8_t*)raw + 0x1F0, 48));

    if (ic->ic_state < IEEE80211_S_SCAN) {
        XYLog("DEBUG %s SKIP: ic_state=%d < SCAN\n", __FUNCTION__, ic->ic_state);
        return kIOReturnSuccess;
    }

    if (ic->ic_state == IEEE80211_S_ASSOC || ic->ic_state == IEEE80211_S_AUTH) {
        XYLog("DEBUG %s SKIP: already in ASSOC/AUTH ic_state=%d\n", __FUNCTION__, ic->ic_state);
        return kIOReturnSuccess;
    }

    if (ap_mode != APPLE80211_AP_MODE_IBSS) {
        disassocIsVoluntary = false;

        struct apple80211_authtype_data auth_type_data;
        auth_type_data.version = APPLE80211_VERSION;
        auth_type_data.authtype_upper = auth_upper;
        auth_type_data.authtype_lower = auth_lower;
        setAUTH_TYPE(&auth_type_data);

        if (rsn_ie_len > 0 && rsn_ie_len <= APPLE80211_MAX_RSN_IE_LEN) {
            struct apple80211_rsn_ie_data rsn_ie_data;
            rsn_ie_data.version = APPLE80211_VERSION;
            rsn_ie_data.len = rsn_ie[1] + 2;
            memcpy(rsn_ie_data.ie, rsn_ie, MIN(rsn_ie_data.len, (uint16_t)APPLE80211_MAX_RSN_IE_LEN));
            setRSN_IE(&rsn_ie_data);
        }

        associateSSID(const_cast<uint8_t *>(ssid), ssid_len, *bssid,
                      auth_lower, auth_upper, NULL, 0, 0);
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_LEAVE_NETWORK(apple80211_leave_network *data)
{
    if (!data)
        return kIOReturnError;

    struct ieee80211com *ic = fHalService->get80211Controller();
    XYLog("%s ic_state=%d\n", __FUNCTION__, ic->ic_state);

    if (ic->ic_state < IEEE80211_S_SCAN)
        return kIOReturnSuccess;

    if (ic->ic_state > IEEE80211_S_AUTH && ic->ic_bss != NULL)
        IEEE80211_SEND_MGMT(ic, ic->ic_bss, IEEE80211_FC0_SUBTYPE_DEAUTH, IEEE80211_REASON_AUTH_LEAVE);

    if (ic->ic_state == IEEE80211_S_ASSOC || ic->ic_state == IEEE80211_S_AUTH)
        return kIOReturnSuccess;

    disassocIsVoluntary = true;

    ieee80211_del_ess(ic, nullptr, 0, 1);
    ieee80211_deselect_ess(ic);
#ifdef USE_APPLE_SUPPLICANT
    ic->ic_rsn_ie_override[1] = 0;
#endif
    ic->ic_assoc_status = APPLE80211_STATUS_UNAVAILABLE;
    ic->ic_deauth_reason = APPLE80211_REASON_ASSOC_LEAVING;
    ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_SCAN_ABORT(void *data)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    XYLog("%s ic_state=%d\n", __FUNCTION__, ic->ic_state);

    // AppleBCMWLANCore::setWCL_SCAN_ABORT is not a no-op: it dispatches into
    // scan-adapter-owned abort work.  The recovered WCLScanManager FSM shows
    // why that matters: SCAN_ABORT_REQ moves IN_PROGRESS -> ABORTED, and the
    // owner only returns ABORTED -> IDLE when it later receives SCAN_COMPLETE.
    //
    // Our old Tahoe path only cleared net80211 flags and returned success, so
    // WCL stayed stuck in SCAN_MANAGER_STATE_ABORTED / IN_PROGRESS and every
    // later external SCAN_REQ hit the family ignore path (0xe00002bc / 16).
    // Cancel the local fake-scan timer and synthesize the single SCAN_DONE edge
    // that our backend otherwise never emits for abort completion.
    if (scanSource) {
        scanSource->cancelTimeout();
        scanSource->disable();
    }

    if (ic->ic_flags & IEEE80211_F_BGSCAN)
        ic->ic_flags &= ~IEEE80211_F_BGSCAN;
    if (ic->ic_flags & IEEE80211_F_ASCAN)
        ic->ic_flags &= ~IEEE80211_F_ASCAN;

    fNextNodeToSend = NULL;
    fScanResultWrapping = false;

    if (instance && instance->fNetIf) {
        static UInt32 abortScanStatus = 0;
        instance->postMessage(instance->fNetIf, APPLE80211_M_SCAN_DONE,
                              &abortScanStatus, sizeof(abortScanStatus), true);
    }

    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setOFFLOAD_TCPKA_ENABLE(apple80211_offload_tcpka_enable_t *data)
{
    // AppleBCMWLANCore::setOFFLOAD_TCPKA_ENABLE does not use a bad-argument
    // code here. The slot defaults to 0xe00002c7 and only flips to success
    // once both the feature gate and keepalive owner object exist.
    if (data == nullptr || !cachedTcpkaOffloadSupported)
        return kIOReturnUnsupported;

    cachedTcpkaOffloadEnabled = data->enabled != 0;
    XYLog("DEBUG [576] %s enabled=%u\n", __FUNCTION__,
          static_cast<unsigned int>(cachedTcpkaOffloadEnabled));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setOFFLOAD_ARP(apple80211_offload_arp_data *data)
{
    // AppleBCMWLANCore::setOFFLOAD_ARP rejects NULL and "no Infra owner" with
    // raw 0x16, then copies the IPv4 / keepalive carrier into core-owned state
    // before running the hidden IPv4 / keepalive notification hooks. We do not
    // have those hidden gated callbacks yet, but the same cached IPv4 fields
    // already back our lifted `setIPV4_PARAMS(...)` path, so keep the exact
    // field coverage instead of dropping this producer on the floor.
    if (data == nullptr || instance == nullptr || instance->fNetIf == nullptr)
        return kApple80211ErrInvalidArgumentRaw;

    cachedDhcpRenewalData = data->keepalive_enabled != 0;
    if (data->has_ipv4_address != 0) {
        cachedIPv4Address = data->ipv4_address;
        cachedIPv4Reserved = 0;
    }
    cachedIPv4Gateway = data->gateway;
    cachedIPv4GatewayTail = data->gateway_tail;

    XYLog("DEBUG [557] %s has_ipv4=%u addr=0x%08x keepalive=%u gw=0x%08x tail=0x%04x\n",
          __FUNCTION__, data->has_ipv4_address, data->ipv4_address,
          data->keepalive_enabled, data->gateway, data->gateway_tail);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setPM_MODE(apple80211_pm_mode *data)
{
    // AppleBCMWLANCore::setPM_MODE is a thin producer: it forwards the dword at
    // caller +0x4 into NetAdapter::configurePM(...). The recovered helper maps
    // any non-zero mode onto the same PM request bit family-side consumers use
    // when they later query powersave state. Re-enter the lifted POWERSAVE path
    // instead of leaving slot [584] unsupported.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    cachedPmMode = data->mode;

    apple80211_powersave_data pd{};
    pd.version = APPLE80211_VERSION;
    pd.powersave_level = data->mode;
    const IOReturn rc = setPOWERSAVE(&pd);
    XYLog("DEBUG [584] %s mode=%u rc=0x%x\n", __FUNCTION__, data->mode, rc);
    return rc;
}

IOReturn AirportItlwmSkywalkInterface::
setLQM_CONFIG(apple80211_lqm_config_t *data)
{
    // AppleBCMWLANCore::setLQM_CONFIG is not a blind setter. Tahoe validates
    // the public 0x24-byte carrier before forwarding it into the LQM owner.
    // Reproduce those exact caller-visible checks so the port no longer
    // advertises slot [577] as unsupported.
    if (data == nullptr)
        return kIOReturnBadArgument;
    if (data->sample_period_ms < 1000 || data->tx_per_interval_ms < 1000 ||
        data->rx_loss_interval_ms < 1000)
        return static_cast<IOReturn>(0x2d);

    const uint8_t *thresholdBytes =
        reinterpret_cast<const uint8_t *>(data) + 0x11;
    for (size_t i = 0; i < 7; i++) {
        if (isInvalidTahoeLqmThresholdByte(thresholdBytes[i]))
            return static_cast<IOReturn>(0x16);
    }
    for (size_t i = 0; i < sizeof(data->opaque_tail_19); i++) {
        if (data->opaque_tail_19[i] > 99)
            return static_cast<IOReturn>(0x16);
    }

    memcpy(&cachedLqmConfig, data, sizeof(cachedLqmConfig));
    cachedLqmConfig.version = APPLE80211_VERSION;
    hasCachedLqmConfig = true;
    XYLog("DEBUG [577] %s sample=%u tx_per=%u rx_loss=%u enabled=%u\n",
          __FUNCTION__, cachedLqmConfig.sample_period_ms,
          cachedLqmConfig.tx_per_interval_ms,
          cachedLqmConfig.rx_loss_interval_ms,
          static_cast<unsigned int>(cachedLqmConfig.enabled));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_REAL_TIME_MODE(apple80211_wcl_real_time_mode *data)
{
    const auto *mode = reinterpret_cast<const tahoeWclRealTimeMode *>(data);

    // AppleBCMWLANCore::setWCL_REAL_TIME_MODE is a real producer with two
    // branches only: NULL -> 0xe00002bc, nonzero first byte -> NetAdapter
    // real-time mode, zero -> NetAdapter default mode. Keep the same gate and
    // preserve the selected mode in driver-owned state rather than leaving the
    // slot as inline success.
    if (mode == nullptr)
        return kIOReturnBadArgumentTahoe;

    cachedRealTimeMode = mode->enabled != 0;
    XYLog("WCL [596] %s realtime=%u\n", __FUNCTION__,
          static_cast<unsigned int>(cachedRealTimeMode));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_ROAM_USER_CACHE(apple80211_user_roam_cache *data)
{
    // AppleBCMWLANCore::setWCL_ROAM_USER_CACHE delegates into the roam adapter
    // `cmdROAM_USER_CACHE(...)`. The recovered helper family shows that the
    // caller-visible payload carries channel entries from offset 0x0 in 0x0c
    // strides, a channel count at +0x78, and an override byte at +0x7a.
    // Persist that exact 0x7c blob so later roam-owner lifts retain the same
    // request state instead of losing it behind an inline success stub.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    memcpy(cachedUserRoamCache, data, sizeof(*data));
    hasCachedUserRoamCache = true;
    const auto count = *reinterpret_cast<const uint16_t *>(data->raw + 0x78);
    const auto overrideState = data->raw[0x7a];
    XYLog("WCL [594] %s count=%u override=%u cached=%u\n",
          __FUNCTION__, count, overrideState,
          static_cast<unsigned int>(hasCachedUserRoamCache));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_REASSOC(apple80211_reassoc *data)
{
    struct ieee80211com *ic = fHalService->get80211Controller();

    // AppleBCMWLANCore::setWCL_REASSOC is not an ack-only stub: it snapshots
    // the recovered reassoc request, refuses NULL with 0xe00002bc, and bails
    // out with the same code when the interface is not associated. The actual
    // producer then delegates to NetAdapter::sendReassocCommand(...).
    //
    // The local port does not carry Apple's firmware command owner, but the
    // net80211 STA stack already owns reassociation frame generation via
    // `ieee80211_send_mgmt(..., REASSOC_REQ, ...)`. Preserve the same request
    // coverage and association gate instead of leaving slot [590] as inline
    // success.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    memcpy(cachedReassocRequest, data, sizeof(*data));
    hasCachedReassocRequest = true;

    if (ic->ic_state != IEEE80211_S_RUN || ic->ic_bss == nullptr)
        return kIOReturnBadArgumentTahoe;

    const int rc = ieee80211_send_mgmt(ic, ic->ic_bss,
                                       IEEE80211_FC0_SUBTYPE_REASSOC_REQ,
                                       0, 0);
    XYLog("WCL [590] %s channels=%u scores=%u reason=%d flags=0x%02x rc=%d\n",
          __FUNCTION__, data->channel_count, data->score_count,
          static_cast<int>(data->roam_reason), data->feature_flags, rc);
    return rc == 0 ? kIOReturnSuccess : static_cast<IOReturn>(rc);
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_LEGACY_ROAM_PROFILE_CONFIG(apple80211_legacy_roam_profile_config *data)
{
    // WCLRoamProfile::setRoamingProfile(legacy) consumes exactly 0x60 bytes,
    // and AppleBCMWLANRoamAdapter stores that profile before it reconfigures
    // join preferences / Multi-AP state. The local port does not have the
    // hidden roam owner object yet, but dropping the payload on the floor was
    // still an architectural mismatch because later roam decisions had no
    // stable source of truth at all.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    memcpy(cachedLegacyRoamProfileConfig, data, sizeof(*data));
    hasCachedLegacyRoamProfileConfig = true;
    XYLog("WCL [592] %s cached=%u size=0x%zx\n",
          __FUNCTION__, static_cast<unsigned int>(hasCachedLegacyRoamProfileConfig),
          sizeof(*data));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_ROAM_PROFILE_CONFIG(apple80211_roam_profile_config *data)
{
    // WCLRoamProfile::setRoamingProfile(modern) ships a 0x23c payload into the
    // roam adapter. Apple's downstream helper fans this out into join
    // preference and per-band policy programming. Persist the exact carrier so
    // the local port no longer acknowledges this producer and immediately loses
    // the only recovered roam configuration state.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    memcpy(cachedRoamProfileConfig, data, sizeof(*data));
    hasCachedRoamProfileConfig = true;
    XYLog("WCL [593] %s cached=%u size=0x%zx\n",
          __FUNCTION__, static_cast<unsigned int>(hasCachedRoamProfileConfig),
          sizeof(*data));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_ARP_MODE(apple80211_wcl_arp_mode *data)
{
    // AppleBCMWLANCore::setWCL_ARP_MODE has three distinct pieces:
    // - NULL -> 0xe00002bc
    // - mode 0/1 choose the keepalive/GARP owner path, anything else -> 0xe00002bc
    // - optional WNM sideband update consumes the two u16s at offsets +0/+2
    //
    // We still lack Apple's hidden keepalive and WNM owners, but the local
    // port now preserves the exact recovered carrier and reuses the lifted
    // OFFLOAD_ARP path so the same IPv4/keepalive state is available to later
    // consumer paths instead of vanishing in an inline success stub.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;
    if (data->mode > 1)
        return kIOReturnBadArgumentTahoe;

    memcpy(cachedWclArpMode, data, sizeof(*data));
    hasCachedWclArpMode = true;

    apple80211_offload_arp_data arp{};
    arp.version = APPLE80211_VERSION;
    arp.has_ipv4_address = cachedIPv4Address != 0 ? 1U : 0U;
    arp.ipv4_address = cachedIPv4Address;
    arp.keepalive_enabled = data->enabled != 0 ? 1U : 0U;
    arp.gateway = cachedIPv4Gateway;
    arp.gateway_tail = cachedIPv4GatewayTail;

    const IOReturn carrierRc = setOFFLOAD_ARP(&arp);
    if (carrierRc != kIOReturnSuccess && carrierRc != kApple80211ErrInvalidArgumentRaw)
        return carrierRc;

    XYLog("WCL [597] %s mode=%u interval=%u enabled=%u wnm=%u/%u\n",
          __FUNCTION__, data->mode, data->interval, data->enabled,
          data->wnm_enabled_a, data->wnm_enabled_b);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_CONFIG_BG_MOTIONPROFILE(apple80211_bg_motion_profile *data)
{
    // AppleBGScanAdapter first validates its internal motion-profile mapping,
    // then programs PNO/EPNO from the incoming blob. The recovered helper
    // rejects a zero PNO-count byte with a generic error, so keep that gate
    // instead of advertising unconditional success.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;
    if (data->raw[1] == 0)
        return kIOReturnError;

    memcpy(cachedBgMotionProfile, data, sizeof(cachedBgMotionProfile));
    hasCachedBgMotionProfile = true;
    XYLog("WCL [615] %s pno_count=%u epno_count=%u multi=%u\n",
          __FUNCTION__, data->raw[1], data->raw[0x1a], data->raw[0]);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_CONFIG_BG_NETWORK(apple80211_bg_network *data)
{
    struct ieee80211com *ic = fHalService->get80211Controller();

    // Apple clears PFN state, resets its internal "cached network available"
    // flags, and only then copies the full 0x12c0 request into adapter-owned
    // storage. The local port has no PFN engine, but it does have the same
    // bgscan cache owner in net80211. Preserve the full request and clear the
    // current cache iterator so later BGSCAN_CACHE_RESULT consumers observe the
    // new network set instead of stale cached nodes.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    memcpy(cachedBgNetwork, data, sizeof(*data));
    hasCachedBgNetwork = true;
    fNextNodeToSend = nullptr;
    fScanResultWrapping = false;
    if (ic->ic_state == IEEE80211_S_RUN)
        ieee80211_begin_cache_bgscan(&ic->ic_ac.ac_if);

    const uint32_t whitelistCount = *reinterpret_cast<const uint32_t *>(&data->raw[0x18]);
    const uint32_t epnoCount = *reinterpret_cast<const uint32_t *>(&data->raw[0x39c]);
    const uint8_t anyConfig = data->raw[0];
    XYLog("WCL [616] %s any=%u list=%u epno=%u marker=0x%08x\n",
          __FUNCTION__, anyConfig, whitelistCount, epnoCount,
          *reinterpret_cast<const uint32_t *>(&data->raw[0x88c]));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_CONFIG_BGSCAN(apple80211_bg_scan *data)
{
    struct ieee80211com *ic = fHalService->get80211Controller();

    // AppleBCMWLANCore::setWCL_CONFIG_BGSCAN is a tiny command multiplexer:
    // byte 0 resets PFN mode, byte 1 rewrites scan_nprobes using byte 2, and
    // byte 3 toggles suspend/resume using byte 4. Mirror that split locally so
    // the request is preserved and the available bgscan owner is exercised.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    memcpy(cachedBgScanConfig, data, sizeof(*data));
    hasCachedBgScanConfig = true;

    if (data->raw[0] != 0) {
        if (ic->ic_flags & IEEE80211_F_BGSCAN)
            ic->ic_flags &= ~IEEE80211_F_BGSCAN;
        if (ic->ic_flags & IEEE80211_F_ASCAN)
            ic->ic_flags &= ~IEEE80211_F_ASCAN;
    }

    IOReturn rc = kIOReturnSuccess;
    if (data->raw[3] != 0) {
        if (data->raw[4] != 0) {
            if (ic->ic_bgscan_start != nullptr && ic->ic_state == IEEE80211_S_RUN) {
                const int bgscanRc = ic->ic_bgscan_start(ic);
                if (bgscanRc != 0)
                    rc = static_cast<IOReturn>(bgscanRc);
            }
        } else {
            if (ic->ic_flags & IEEE80211_F_BGSCAN)
                ic->ic_flags &= ~IEEE80211_F_BGSCAN;
        }
    }

    XYLog("WCL [617] %s reset=%u nprobes=%u/%u suspend=%u/%u rc=0x%x\n",
          __FUNCTION__, data->raw[0], data->raw[1], data->raw[2],
          data->raw[3], data->raw[4], rc);
    return rc;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_CONFIG_BG_PARAMS(apple80211_bg_params *data)
{
    struct ieee80211com *ic = fHalService->get80211Controller();

    // AppleBGScanAdapter::setWCL_CONFIG_BG_PARAMS carries two independent
    // sub-commands out of a 0x20 blob. The local bgscan engine does not expose
    // those hidden helper entrypoints, but preserving the exact payload and
    // re-arming cache bgscan when the request is non-empty keeps the owner-side
    // state reachable instead of acknowledging and discarding it.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    memcpy(cachedBgParams, data, sizeof(*data));
    hasCachedBgParams = true;
    if ((data->raw[0] != 0 || data->raw[0x18] != 0) && ic->ic_state == IEEE80211_S_RUN)
        ieee80211_begin_cache_bgscan(&ic->ic_ac.ac_if);

    XYLog("WCL [618] %s pno=%u mode=%u epno=%u action=%u dwell=%u\n",
          __FUNCTION__, data->raw[0], data->raw[1], data->raw[0x18],
          data->raw[0x19], *reinterpret_cast<const uint32_t *>(&data->raw[0x1c]));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_JOIN_ABORT(apple80211_wcl_abort_join *data)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    const bool requestCompletion = data != nullptr &&
                                   *reinterpret_cast<const uint32_t *>(data) != 0;

    XYLog("WCL [598] %s ic_state=%d completion=%u\n",
          __FUNCTION__, ic->ic_state,
          static_cast<unsigned int>(requestCompletion));

    // AppleBCMWLANCore::setWCL_JOIN_ABORT does not reject NULL. It maps NULL to
    // `false`, delegates into JoinAdapter::abortFirmwareJoinSync(bool), and
    // when the incoming bool is true it publishes APPLE80211_M_WCL_JOIN_ABORT_COMPLETE
    // (0xD6). The WCLJoinManager symbolic FSM confirms why this matters:
    // JOIN_ABORT_REQ moves active join states into ABORTED and the manager only
    // returns ABORTED -> IDLE after the matching JOIN_ABORT_COMPLETE edge.
    //
    // Our local architecture does not have a separate JoinAdapter object, so
    // the closest 1:1 owner action is to tear down any in-flight auth/assoc
    // state in the same net80211 controller that `setASSOCIATE(...)` drives,
    // then emit the same completion bulletin. Leaving slot [598] as inline
    // success kept the consumer side permanently missing that completion edge.
    if (ic->ic_state >= IEEE80211_S_SCAN) {
        if (ic->ic_state > IEEE80211_S_AUTH && ic->ic_bss != NULL)
            IEEE80211_SEND_MGMT(ic, ic->ic_bss, IEEE80211_FC0_SUBTYPE_DEAUTH,
                                IEEE80211_REASON_AUTH_LEAVE);

        disassocIsVoluntary = true;
        ieee80211_del_ess(ic, nullptr, 0, 1);
        ieee80211_deselect_ess(ic);
#ifdef USE_APPLE_SUPPLICANT
        ic->ic_rsn_ie_override[1] = 0;
#endif
        ic->ic_assoc_status = APPLE80211_STATUS_UNAVAILABLE;
        ic->ic_deauth_reason = APPLE80211_REASON_ASSOC_LEAVING;
        ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
    }

    if (requestCompletion && instance && instance->fNetIf)
        instance->postMessage(instance->fNetIf,
                              APPLE80211_M_WCL_JOIN_ABORT_COMPLETE,
                              nullptr, 0, true);

    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_QOS_PARAMS(apple80211_wcl_qos_params *data)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    const auto *qos = reinterpret_cast<const tahoeWclQosParams *>(data);

    // AppleBCMWLANCore::setWCL_QOS_PARAMS is not one monolithic blob. The
    // recovered NetAdapter helper applies independent knobs under a flag byte:
    // long retry limit, RTS threshold, powersave mode, and two "lifetime"
    // buckets. We only claim parity where the local port has a real owner:
    // RTS threshold lives in `ieee80211com`, powersave already has a lifted
    // Tahoe IOC path, and the remaining Apple-configured words are preserved
    // as driver-owned state instead of being acknowledged and discarded.
    if (qos == nullptr)
        return kIOReturnBadArgumentTahoe;

    cachedQosFlags = qos->flags;
    if ((qos->flags & 0x01) != 0)
        cachedQosLongRetryLimit = qos->long_retry_limit;
    if ((qos->flags & 0x02) != 0)
        cachedQosRtsThreshold = qos->rts_threshold;
    if ((qos->flags & 0x04) != 0)
        cachedQosLifetimeAc3 = qos->lifetime_ac3;
    if ((qos->flags & 0x08) != 0)
        cachedQosLifetimeAc2 = qos->lifetime_ac2;

    if ((qos->flags & 0x02) != 0) {
        ic->ic_rtsthreshold = MIN(static_cast<uint32_t>(IEEE80211_RTS_MAX),
                                  cachedQosRtsThreshold);
    }

    if ((qos->flags & 0x10) != 0) {
        apple80211_powersave_data pd{};
        pd.version = APPLE80211_VERSION;
        pd.powersave_level = qos->powersave_mode;
        setPOWERSAVE(&pd);
    }

    XYLog("WCL [602] %s flags=0x%02x retry=%u rts=%u life3=%u life2=%u ps=%u\n",
          __FUNCTION__, qos->flags, cachedQosLongRetryLimit, cachedQosRtsThreshold,
          cachedQosLifetimeAc3, cachedQosLifetimeAc2, cachedPowersaveLevel);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_LINK_UP_DONE(void *data)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    (void)data;

    // Apple routes LINK_UP_DONE into PowerManager::handleLinkUpConfiguration().
    // The local port has no separate Apple power-manager object, but it does
    // have the same two owner-side knobs that materially change post-link
    // runtime behavior: re-applying the current powersave mode and pushing a
    // MAC-context refresh through `ic_updateedca`. Leaving slot [603] as a
    // blind success stub meant neither happened on the Tahoe WCL path.
    if (cachedPowersaveLevel != APPLE80211_POWERSAVE_MODE_DISABLED) {
        apple80211_powersave_data pd{};
        pd.version = APPLE80211_VERSION;
        pd.powersave_level = cachedPowersaveLevel;
        setPOWERSAVE(&pd);
    }
    if (ic->ic_updateedca != nullptr)
        ic->ic_updateedca(ic);

    XYLog("WCL [603] %s link_active=%u assoc=%u realtime=%u ps=%u\n",
          __FUNCTION__,
          static_cast<unsigned int>((instance->currentStatus & kIONetworkLinkActive) != 0),
          static_cast<unsigned int>(ic->ic_state == IEEE80211_S_RUN && ic->ic_bss != nullptr),
          static_cast<unsigned int>(cachedRealTimeMode),
          cachedPowersaveLevel);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_SET_SCAN_HOME_AWAY_TIME(scanHomeAndAwayTime *data)
{
    // AppleBCMWLANCore::setWCL_SET_SCAN_HOME_AWAY_TIME consumes a single dword
    // and forwards it into the scan-adapter owner. Preserve the same carrier
    // instead of acknowledging slot [604] and discarding the timing request.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    cachedScanHomeAwayTime = data->milliseconds;
    XYLog("WCL [604] %s ms=%u\n", __FUNCTION__, cachedScanHomeAwayTime);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_ULOFDMA_STATE(apple80211_wcl_ulofdma_state *data)
{
    const auto *state = reinterpret_cast<const tahoeWclUlofdmaState *>(data);

    // AppleBCMWLANCore::setWCL_ULOFDMA_STATE is a plain 11ax-adapter producer:
    // NULL -> 0xe00002bc, otherwise forward the first dword to the owner at
    // core +0x15c8. Even before that hidden owner is lifted 1:1, slot [608]
    // must preserve the exact dword carrier instead of sitting on generic
    // kIOReturnUnsupported.
    if (state == nullptr)
        return kIOReturnBadArgumentTahoe;

    cachedUlofdmaState = state->mode;
    XYLog("WCL [608] %s mode=%u\n", __FUNCTION__, cachedUlofdmaState);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setMIMO_CONFIG(apple80211_mimo_config *data)
{
    const auto *config = reinterpret_cast<const uint32_t *>(data);

    // AppleBCMWLANCore::setMIMO_CONFIG rejects NULL with 0xe00002bc, reads the
    // first dword as the caller-visible mode, and then pushes that mode into
    // the MIMO power-save owner. The hidden owner is still outside this batch,
    // but acknowledging slot [614] without caching the selected mode was still
    // a producer mismatch.
    if (config == nullptr)
        return kIOReturnBadArgumentTahoe;

    cachedMimoConfig = *config;
    XYLog("WCL [614] %s mimo_ps=%u\n", __FUNCTION__, cachedMimoConfig);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setFACETIME_WIFICALLING_PARAMS(apple80211_facetime_wificalling_params *data)
{
    const auto *params = reinterpret_cast<const tahoeFaceTimeWiFiCallingParams *>(data);

    // Apple stores a single FaceTime/WiFi-calling status dword and hands it to
    // setWiFiCallPolicies(...). Preserve that status verbatim so slot [623] no
    // longer drops an Apple-owned carrier behind kIOReturnUnsupported.
    if (params == nullptr)
        return kIOReturnBadArgumentTahoe;

    cachedFaceTimeWiFiCallingStatus = params->status;
    XYLog("WCL [623] %s status=%u\n", __FUNCTION__, cachedFaceTimeWiFiCallingStatus);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setDUAL_POWER_MODE(apple80211_dual_power_mode_params *data)
{
    const auto *params = reinterpret_cast<const tahoeDualPowerModeParams *>(data);

    // AppleBCMWLANCore::setDUAL_POWER_MODE persists two signed dwords at core
    // offsets +0x4d3c/+0x4d40, conditionally arms a one-byte "type-2 present"
    // flag at +0x4d44, and always re-enters tx-power-cap state handling. The
    // local port still lacks that config-owner choreography, but it must stop
    // advertising slot [631] as entirely unsupported.
    if (params == nullptr)
        return kIOReturnBadArgumentTahoe;

    cachedDualPowerModePrimary = params->primary;
    cachedDualPowerModeSecondary = params->secondary;
    XYLog("WCL [631] %s primary=%d secondary=%d\n",
          __FUNCTION__, cachedDualPowerModePrimary, cachedDualPowerModeSecondary);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setCONGESTION_CTRL_IND(apple80211_congestion_control_indication *data)
{
    const auto *ind = reinterpret_cast<const tahoeCongestionControlIndication *>(data);

    // AppleBCMWLANCore::setCONGESTION_CTRL_IND is a tiny core-state write:
    // log gate, then a bool at core +0x79d2. There is no extra hidden helper
    // to wait for, so slot [634] should behave as a real bool carrier here.
    if (ind == nullptr)
        return kIOReturnBadArgumentTahoe;

    cachedCongestionControlEnabled = ind->enabled != 0;
    XYLog("WCL [634] %s enabled=%u\n", __FUNCTION__,
          static_cast<unsigned int>(cachedCongestionControlEnabled));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setLMTPC_CONFIG(apple80211_lmtpc_config *data)
{
    const auto *config = reinterpret_cast<const tahoeLmtpcConfig *>(data);

    // AppleBCMWLANCore::setLMTPC_CONFIG rejects NULL, copies one byte into the
    // core-owned cache at +0x4594, and then re-enters setLMTPC(). Preserve the
    // same byte carrier so slot [638] no longer diverges at the producer ABI.
    if (config == nullptr)
        return kIOReturnBadArgumentTahoe;

    cachedLmtpcValue = config->value;
    XYLog("WCL [638] %s value=%u\n", __FUNCTION__, cachedLmtpcValue);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setLE_SCAN_PARAM(apple80211_le_scan_params *data)
{
    const auto *params = reinterpret_cast<const tahoeLeScanParams *>(data);

    // AppleBCMWLANCore::setLE_SCAN_PARAM consumes a fixed 0x10 carrier:
    // byte 0 selects disconnect-vs-connect accounting, dwords +4/+8 feed the
    // per-event counters, and dword +0xc indexes one of seven histogram
    // buckets. The local port has no BTLE reporting owner yet, but it still
    // needs to retain the exact request blob instead of leaving slot [640]
    // unreachable.
    if (params == nullptr)
        return kIOReturnBadArgumentTahoe;

    memcpy(cachedLeScanParams, params, sizeof(*params));
    hasCachedLeScanParams = true;
    XYLog("WCL [640] %s disconnected=%u connect=%u disconnect=%u bucket=%u\n",
          __FUNCTION__, params->disconnected, params->connectedEvents,
          params->disconnectedEvents, params->bucket);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_WNM_OPS(apple80211_wcl_wnm_config_t *data)
{
    // AppleBCMWLANCore::setWCL_WNM_OPS is a real producer with only one gate
    // visible at the core layer: NULL -> 0xe00002bc, otherwise delegate into
    // WnmAdapter::configureWnmFeatures(...). The WCL-side consumer mutates a
    // large opaque blob up to offsets 0x334+, so preserve the recovered full
    // carrier instead of keeping slot [625] unsupported.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    memcpy(cachedWnmConfig, data, sizeof(*data));
    hasCachedWnmConfig = true;
    XYLog("DEBUG [625] %s flags0=0x%02x flags8=0x%02x beacon=0x%02x cached=%u\n",
          __FUNCTION__, data->raw[0], data->raw[8], data->raw[0x32c],
          static_cast<unsigned int>(hasCachedWnmConfig));
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
setWCL_WNM_OFFLOAD(apple80211_wcl_wnm_offload_t *data)
{
    // AppleBCMWLANCore::setWCL_WNM_OFFLOAD has the same core-layer contract:
    // NULL -> 0xe00002bc, otherwise delegate into WnmAdapter
    // configureWnmOffloadFeatures(...). Recovered WCLWnmAgent helpers mutate
    // the caller blob up to offset 0x2c, so keep the full opaque carrier.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    memcpy(cachedWnmOffload, data, sizeof(*data));
    hasCachedWnmOffload = true;
    XYLog("DEBUG [626] %s flags0=0x%02x flags4=0x%02x cached=%u\n",
          __FUNCTION__, data->raw[0], data->raw[4],
          static_cast<unsigned int>(hasCachedWnmOffload));
    return kIOReturnSuccess;
}

extern OSDictionary *convertScanToDictionary(apple80211_scan_result *a1);

static int convertNodeToScanResult(ItlHalService *fHalService, struct ieee80211_node *fNextNodeToSend, apple80211_scan_result *result)
{
    bzero(result, sizeof(*result));
    result->version = APPLE80211_VERSION;
    if (fNextNodeToSend->ni_rsnie_tlv && fNextNodeToSend->ni_rsnie_tlv_len > 0) {
        result->asr_ie_len = fNextNodeToSend->ni_rsnie_tlv_len;
        memcpy(result->asr_ie_data, fNextNodeToSend->ni_rsnie_tlv, MIN(result->asr_ie_len, sizeof(result->asr_ie_data)));
    } else {
        result->asr_ie_len = 0;
    }
    result->asr_beacon_int = fNextNodeToSend->ni_intval;
    // Tahoe airportd candidate ingestion is sensitive to scan-result shape.
    // V16 left asr_rates empty because the loop iterated result->asr_nrates
    // immediately after bzero(), so nrates stayed 0 until after the copy.
    // That produces a malformed candidate even when the BSS is present in ic_tree.
    result->asr_nrates = MIN((uint8_t)fNextNodeToSend->ni_rates.rs_nrates,
                             (uint8_t)APPLE80211_MAX_RATES);
    for (uint8_t i = 0; i < result->asr_nrates; i++)
        result->asr_rates[i] = fNextNodeToSend->ni_rates.rs_rates[i];
    result->asr_age = (uint32_t)(airport_up_time() - fNextNodeToSend->ni_age_ts);
    result->asr_cap = fNextNodeToSend->ni_capinfo;
    result->asr_channel.version = APPLE80211_VERSION;
    result->asr_channel.channel = ieee80211_chan2ieee(fHalService->get80211Controller(), fNextNodeToSend->ni_chan);
    result->asr_channel.flags = ieeeChanFlag2appleScanFlagVentura(fNextNodeToSend->ni_chan->ic_flags);
    result->asr_noise = -fHalService->getDriverInfo()->getBSSNoise();
    result->asr_rssi = -(0 - IWM_MIN_DBM - fNextNodeToSend->ni_rssi);
    result->asr_snr = result->asr_rssi - result->asr_noise;
    memcpy(result->asr_bssid, fNextNodeToSend->ni_bssid, IEEE80211_ADDR_LEN);
    result->asr_ssid_len = fNextNodeToSend->ni_esslen;
    if (result->asr_ssid_len != 0)
        memcpy(&result->asr_ssid, fNextNodeToSend->ni_essid, result->asr_ssid_len);
    return 0;
}

IOReturn AirportItlwmSkywalkInterface::
getCURRENT_NETWORK(apple80211_scan_result *sr)
{
    if (fHalService->get80211Controller()->ic_state != IEEE80211_S_RUN || fHalService->get80211Controller()->ic_bss == NULL)
        return kIOReturnError;
    convertNodeToScanResult(fHalService, fHalService->get80211Controller()->ic_bss, sr);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getTIMESYNC_INFO(apple80211_timesync_info *data)
{
    // AppleBCMWLANCore::getTIMESYNC_INFO routes through the hidden +0x1510
    // object into a 0x100-byte text producer. The recovered bus/time-sync
    // engine path always appends capability text first and then either:
    // - calls TimeSyncEngine::getTimeSyncInfo(...)
    // - or prints "TimeSync Engine not-instantiated"
    //
    // The local port has no timesync engine object, so the exact Apple-shaped
    // behavior for the "engine missing" case is a deterministic text report,
    // not generic unsupported.
    if (data == nullptr)
        return kIOReturnBadArgumentTahoe;

    bzero(data->text, sizeof(data->text));
    strlcat(data->text,
            "TimeSync Capability:\n\tFirmware: HW Timestamping Not capable\n\tHost: Timestamping NOT capable\n",
            sizeof(data->text));
    strlcat(data->text, "TimeSync Engine not-instantiated\n", sizeof(data->text));
    XYLog("DEBUG [522] %s produced timesync report without engine\n", __FUNCTION__);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getCOLOCATED_NETWORK_SCOPE_ID(apple80211_colocated_network_scope_id *as)
{
    if (!as)
        return kIOReturnBadArgument;

    // Apple/WCL expects a 0x30-byte buffer here, not the 12-byte stub we had
    // before.  WCLConfigManager validates bulletinBoardMessage length == 0x30
    // before calling into the driver.  The old declaration kept the trailing
    // bytes uninitialized and made our Tahoe ABI diverge from Apple.
    //
    // AppleBCMWLAN sets version=1 (not APPLE80211_VERSION), then fills two
    // IDs via a lower helper when colocated-scope state exists.  We do not
    // have that lower scope provider yet, so the correct non-fabricated state
    // is a zeroed payload with version=1 and zero IDs.
    bzero(as, sizeof(*as));
    as->version = 1;
    XYLog("%s version=%u id1=%u id2=%u\n", __FUNCTION__, as->version, as->id1, as->id2);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getSCAN_RESULT(struct apple80211_scan_result *sr)
{
    RT2_SET(4); sRT.scanResCount++;
    if (fNextNodeToSend == NULL) {
        if (fScanResultWrapping) {
            fScanResultWrapping = false;
            XYLog("DEBUG %s wrapping done → 5\n", __FUNCTION__);
            return 5;
        } else {
            fNextNodeToSend = RB_MIN(ieee80211_tree, &fHalService->get80211Controller()->ic_tree);
            if (fNextNodeToSend == NULL) {
                XYLog("DEBUG %s no nodes → 5\n", __FUNCTION__);
                return 5;
            }
        }
    }
    XYLog("DEBUG %s ssid=%s rssi=%d ch=%d\n", __FUNCTION__,
          fNextNodeToSend->ni_essid, fNextNodeToSend->ni_rssi,
          fNextNodeToSend->ni_chan ? ieee80211_chan2ieee(fHalService->get80211Controller(), fNextNodeToSend->ni_chan) : -1);
    convertNodeToScanResult(fHalService, fNextNodeToSend, sr);
    
    fNextNodeToSend = RB_NEXT(ieee80211_tree, &HalService->get80211Controller()->ic_tree, fNextNodeToSend);
    if (fNextNodeToSend == NULL)
        fScanResultWrapping = true;

    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getWCL_BGSCAN_CACHE_RESULT(apple80211_bgscan_cached_network_data_list *data)
{
    if (!data)
        return kIOReturnError;

    struct ieee80211com *ic = fHalService->get80211Controller();
    bzero(data, sizeof(*data));

    uint64_t now = airport_up_time();
    uint32_t count = 0;
    struct ieee80211_node *ni;

    RB_FOREACH(ni, ieee80211_tree, &ic->ic_tree) {
        if (count >= APPLE80211_BGSCAN_MAX_NETWORKS)
            break;
        if (ni->ni_chan == NULL || ni->ni_chan == IEEE80211_CHAN_ANYC)
            continue;

        struct apple80211_bgscan_cached_network_entry *entry = &data->entries[count];
        memcpy(entry->bssid, ni->ni_bssid, 6);
        entry->channel = ieee80211_chan2ieee(ic, ni->ni_chan);
        entry->rssi = -(0 - IWM_MIN_DBM - ni->ni_rssi);
        entry->capability = ni->ni_capinfo;
        entry->ssid_crc = ether_crc32_le_update(0xFFFFFFFF,
                            (const u_int8_t *)ni->ni_essid, ni->ni_esslen);
        entry->age_ms = (uint32_t)(now - ni->ni_age_ts);
        count++;
    }

    data->count = count;
    data->timestamp = now;

    XYLog("%s count=%u timestamp=%llu\n", __FUNCTION__, count, now);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getWCL_CHANNELS_INFO(apple80211ChannelInfo *data)
{
    if (!data)
        return kIOReturnError;

    struct ieee80211com *ic = fHalService->get80211Controller();
    bzero(data, sizeof(*data));

    uint16_t count = 0;
    for (int i = 0; i < IEEE80211_CHAN_MAX && count < APPLE80211_WCL_MAX_CHANNELS; i++) {
        struct ieee80211_channel *ch = &ic->ic_channels[i];
        if (ch->ic_freq == 0)
            continue;

        struct apple80211_wcl_channel_entry *entry = &data->entries[count];
        entry->chan_spec = ieee80211_chan2ieee(ic, ch);
        entry->max_tx_power = 0;
        entry->min_tx_power = 0;
        entry->field4 = 0;
        entry->field5 = 0;
        entry->indoor = 0;

        uint8_t flags = 0;
        if (ch->ic_flags & IEEE80211_CHAN_PASSIVE)
            flags |= (1 << 2);  // bit2: passive
        if (ch->ic_flags & IEEE80211_CHAN_DFS)
            flags |= (1 << 1);  // bit1: radar/DFS
        if (IEEE80211_IS_CHAN_HT40(ch) || IEEE80211_IS_CHAN_VHT40(ch))
            flags |= (1 << 4);  // bit4: 40MHz
        if (IEEE80211_IS_CHAN_VHT80(ch))
            flags |= (1 << 5);  // bit5: 80MHz
        entry->flags = flags;
        count++;
    }

    data->num_channels = count;
    data->support_6e = 0;

    XYLog("WCL [530] %s num_channels=%u\n", __FUNCTION__, count);
    return kIOReturnSuccess;
}

IOReturn AirportItlwmSkywalkInterface::
getWCL_BSS_INFO(apple80211_beacon_msg *data)
{
    if (!data)
        return kIOReturnError;

    struct ieee80211com *ic = fHalService->get80211Controller();
    if (ic->ic_state != IEEE80211_S_RUN || ic->ic_bss == NULL) {
        XYLog("WCL [526] %s not associated ic_state=%d\n", __FUNCTION__, ic->ic_state);
        return kIOReturnError;
    }

    struct ieee80211_node *ni = ic->ic_bss;
    bzero(data, sizeof(*data));

    // Populate at known offsets — exact layout is reverse-engineered,
    // fields may shift. Log raw bytes for verification.
    uint8_t *buf = data->data;

    // +0x00: BSSID (6 bytes) — most beacon structs start with BSSID
    memcpy(buf + 0x00, ni->ni_bssid, 6);
    // +0x06: beacon interval (2 bytes LE)
    *(uint16_t *)(buf + 0x06) = ni->ni_intval;
    // +0x08: capability info (2 bytes LE)
    *(uint16_t *)(buf + 0x08) = ni->ni_capinfo;
    // +0x0A: channel number (2 bytes LE)
    *(uint16_t *)(buf + 0x0A) = ieee80211_chan2ieee(ic, ni->ni_chan);
    // +0x0C: RSSI (2 bytes LE, signed)
    *(int16_t *)(buf + 0x0C) = -(0 - IWM_MIN_DBM - ni->ni_rssi);
    // +0x0E: noise (2 bytes LE, signed)
    *(int16_t *)(buf + 0x0E) = -fHalService->getDriverInfo()->getBSSNoise();
    // +0x10: SSID length (1 byte)
    buf[0x10] = ni->ni_esslen;
    // +0x11: SSID (up to 32 bytes)
    if (ni->ni_esslen > 0)
        memcpy(buf + 0x11, ni->ni_essid, MIN(ni->ni_esslen, 32));
    // +0x7C: IE length (2 bytes LE)
    uint16_t ie_len = 0;
    if (ni->ni_rsnie_tlv && ni->ni_rsnie_tlv_len > 0)
        ie_len = MIN(ni->ni_rsnie_tlv_len, (uint16_t)(0x84 - 0x7E));
    *(uint16_t *)(buf + 0x7C) = ie_len;
    // +0x7E: IE data (remaining bytes)
    if (ie_len > 0)
        memcpy(buf + 0x7E, ni->ni_rsnie_tlv, ie_len);

    XYLog("WCL [526] %s bssid=%02x:%02x:%02x:%02x:%02x:%02x ch=%u rssi=%d ssid_len=%u ie_len=%u\n",
          __FUNCTION__,
          ni->ni_bssid[0], ni->ni_bssid[1], ni->ni_bssid[2],
          ni->ni_bssid[3], ni->ni_bssid[4], ni->ni_bssid[5],
          ieee80211_chan2ieee(ic, ni->ni_chan),
          -(0 - IWM_MIN_DBM - ni->ni_rssi), ni->ni_esslen, ie_len);
    // Dump first 32 bytes for offset verification
    XYLog("WCL [526] hex[0x00-0x1F]: %s\n", hexdump(buf, 32));
    return kIOReturnSuccess;
}
