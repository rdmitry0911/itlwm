/*
 * Copyright (c) 2005 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * The contents of this file constitute Original Code as defined in and
 * are subject to the Apple Public Source License Version 1.1 (the
 * "License").  You may not use this file except in compliance with the
 * License.  Please obtain a copy of the License at
 * http://www.apple.com/publicsource and read it before using this file.
 *
 * This Original Code and all software distributed under the License are
 * distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 */
#ifndef _APPLE80211_IOCTL_H_
#define _APPLE80211_IOCTL_H_

#include <Availability.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/ethernet.h>
#include <sys/param.h>
#include <sys/ioctl.h>

// This is necessary, because even the latest Xcode does not support properly targeting 11.0.
#ifndef __IO80211_TARGET
#error "Please define __IO80211_TARGET to the requested version"
#endif

#include "apple80211_var.h"

struct apple80211req
{
    char        req_if_name[IFNAMSIZ];    // 16 bytes
    int         req_type;                 // 4 bytes
    int         req_val;                  // 4 bytes
    u_int32_t   req_len;                  // 4 bytes
    void        *req_data;                // 4 bytes
};

#if __IO80211_TARGET >= __MAC_10_15
#define SIOCSA80211 2150656456
#define SIOCGA80211 3224398281
#else
#define SIOCSA80211 2150132168
#define SIOCGA80211 3223873993
#endif

#define APPLE80211_AWDL_CAP_CCA_STATS   2
#define APPLE80211_AWDL_CAP_SEC_PAYLOAD 0x100000000

// req_type

#define APPLE80211_IOC_SSID                     1    // req_type

#define APPLE80211_IOC_AUTH_TYPE                2    // req_type
#define     APPLE80211_AUTH_TYPE_UNICAST        1    // req_val, SIOCGA80211 only
#define     APPLE80211_AUTH_TYPE_MULTICAST      2    // req_val, SIOCGA80211 only

#define APPLE80211_IOC_CIPHER_KEY                3   // req_type
#define     APPLE80211_CIPHER_KEY_UNICAST        1   // req_val
#define     APPLE80211_CIPHER_KEY_MULICAST       2   // req_val

// Tahoe Skywalk current-PMK setter delivered through the alternate
// apple80211setCUR_PMK selector. Carries struct apple80211_pmk; the
// key bytes live at struct offset 0x10, and the validated key length
// at offset 0x04. Used by Apple userspace to publish or refresh the
// active host-supplicant PMK on a PSK association edge.
#define APPLE80211_IOC_CUR_PMK                 360   // req_type

#define APPLE80211_IOC_CHANNEL                   4   // req_type

#define APPLE80211_IOC_POWERSAVE                 5   // req_type

#define APPLE80211_IOC_PROTMODE                  6   // req_type

#define APPLE80211_IOC_TXPOWER                   7   // req_type
#define APPLE80211_IOC_RATE                      8   // req_type
#define APPLE80211_IOC_BSSID                     9   // req_type

#define APPLE80211_IOC_SCAN_REQ                 10   // req_type

#define APPLE80211_IOC_SCAN_RESULT              11   // req_type

#define APPLE80211_IOC_CARD_CAPABILITIES        12   // req_type

#define APPLE80211_IOC_STATE                    13   // req_type (apple80211_state)
#define APPLE80211_IOC_PHY_MODE                 14   // req_type (apple80211_phymode)

#define APPLE80211_IOC_OP_MODE                  15   // req_type (apple80211_opmode)
#define APPLE80211_IOC_RSSI                     16   // req_type
#define APPLE80211_IOC_NOISE                    17   // req_type

#define APPLE80211_IOC_INT_MIT                  18
#define APPLE80211_IOC_INT_MIT_OFF               1   // req_val
#define APPLE80211_IOC_INT_MIT_ON                2   // req_val

// card power
#define APPLE80211_IOC_POWER                    19   // req_type

#define APPLE80211_IOC_ASSOCIATE                20   // req_type
#define APPLE80211_IOC_ASSOCIATE_RESULT         21   // req_type
#define APPLE80211_IOC_DISASSOCIATE             22   // req_type
#define APPLE80211_IOC_STATUS_DEV_NAME          23   // req_type

#define APPLE80211_IOC_IBSS_MODE                24   // req_type
#define APPLE80211_IOC_IBSS_MODE_START           1   // req_val
#define APPLE80211_IOC_IBSS_MODE_STOP            2   // req_val

#define APPLE80211_IOC_HOST_AP_MODE             25   // req_type
#define APPLE80211_IOC_HOST_AP_MODE_START        1   // req_val
#define APPLE80211_IOC_HOST_AP_MODE_STOP         2   // req_val

#define APPLE80211_IOC_AP_MODE                   26  // req_type (apple80211_apmode)
#define APPLE80211_IOC_SUPPORTED_CHANNELS        27  // req_type
#define APPLE80211_IOC_LOCALE                    28  // req_type
#define APPLE80211_IOC_DEAUTH                    29  // req_type
#define APPLE80211_IOC_COUNTERMEASURES           30  // req_type
#define APPLE80211_IOC_FRAG_THRESHOLD            31  // req_type
#define APPLE80211_IOC_RATE_SET                  32  // req_type
#define APPLE80211_IOC_SHORT_SLOT                33  // req_type
#define APPLE80211_IOC_MULTICAST_RATE            34  // req_type
#define APPLE80211_IOC_SHORT_RETRY_LIMIT         35  // req_type
#define APPLE80211_IOC_LONG_RETRY_LIMIT          36  // req_type
#define APPLE80211_IOC_TX_ANTENNA                37  // req_type
#define APPLE80211_IOC_RX_ANTENNA                38  // req_type
#define APPLE80211_IOC_ANTENNA_DIVERSITY         39  // req_type
#define APPLE80211_IOC_ROM                       40  // req_type
#define APPLE80211_IOC_DTIM_INT                  41  // req_type
#define APPLE80211_IOC_STATION_LIST              42  // req_type
#define APPLE80211_IOC_DRIVER_VERSION            43  // req_type
#define APPLE80211_IOC_HARDWARE_VERSION          44  // req_type
#define APPLE80211_IOC_RAND                      45  // req_type
#define APPLE80211_IOC_RSN_IE                    46  // req_type
#define APPLE80211_IOC_BACKGROUND_SCAN           47  // req_type
#define APPLE80211_IOC_AP_IE_LIST                48  // req_type
#define APPLE80211_IOC_STATS                     49  // req_type
#define APPLE80211_IOC_ASSOCIATION_STATUS        50  // req_type
#define APPLE80211_IOC_COUNTRY_CODE              51  // req_type
#define APPLE80211_IOC_DEBUG_FLAGS               52  // req_type
#define APPLE80211_IOC_LAST_RX_PKT_DATA          53  // req_type
#define APPLE80211_IOC_RADIO_INFO                54  // req_type
#define APPLE80211_IOC_GUARD_INTERVAL            55  // req_type
#define APPLE80211_IOC_MIMO_POWERSAVE            56  // req_type
#define APPLE80211_IOC_MCS                       57  // req_type
#define APPLE80211_IOC_RIFS                      58  // req_type
#define APPLE80211_IOC_LDPC                      59  // req_type
#define APPLE80211_IOC_MSDU                      60  // req_type
#define APPLE80211_IOC_MPDU                      61  // req_type
#define APPLE80211_IOC_BLOCK_ACK                 62  // req_type
#define APPLE80211_IOC_PLS                       63  // req_type
#define APPLE80211_IOC_PSMP                      64  // req_type
#define APPLE80211_IOC_PHY_SUB_MODE              65  // req_type
#define APPLE80211_IOC_MCS_INDEX_SET             66  // req_type
#define APPLE80211_IOC_CACHE_THRESH_BCAST        67  // req_type
#define APPLE80211_IOC_CACHE_THRESH_DIRECT       68  // req_type
#define APPLE80211_IOC_WOW_PARAMETERS            69  // req_type
#define APPLE80211_IOC_WOW_ENABLED               70  // req_type
#define APPLE80211_IOC_40MHZ_INTOLERANT          71  // req_type
#define APPLE80211_IOC_PID_LOCK                  72
#define APPLE80211_IOC_STA_IE_LIST               73
#define APPLE80211_IOC_STA_AUTHORIZE             74
#define APPLE80211_IOC_STA_DISASSOCIATE          75
#define APPLE80211_IOC_STA_DEAUTH                76
#define APPLE80211_IOC_RSN_CONF                  77
#define APPLE80211_IOC_KEY_RSC                   78
#define APPLE80211_IOC_STA_STATS                 79
#define APPLE80211_IOC_ROAM_THRESH               80
#define APPLE80211_IOC_VENDOR_DBG_FLAGS          81
#define APPLE80211_IOC_CACHE_AGE_THRESH          82
#define APPLE80211_IOC_PMK_CACHE                 83
#define APPLE80211_IOC_LINK_QUAL_EVENT_PARAMS    84
#define APPLE80211_IOC_IE                        85
#define APPLE80211_IOC_SCAN_REQ_MULTIPLE         86
#define APPLE80211_IOC_BTCOEX_MODE               87
#define APPLE80211_IOC_WOW_TEST                  88
#define APPLE80211_IOC_CLEAR_PMK_CACHE           89
#define APPLE80211_IOC_SCANCACHE_CLEAR           90
#define APPLE80211_IOC_P2P_ENABLE                91
#define APPLE80211_IOC_P2P_LISTEN                92
#define APPLE80211_IOC_P2P_SCAN                  93
#define APPLE80211_IOC_VIRTUAL_IF_CREATE         94
#define APPLE80211_IOC_VIRTUAL_IF_DELETE         95
#define APPLE80211_IOC_VIRTUAL_IF_ROLE           96
#define APPLE80211_IOC_VIRTUAL_IF_PARENT         97
#define APPLE80211_IOC_P2P_GO_CONF               98
#define APPLE80211_IOC_P2P_NOA_LIST              99
#define APPLE80211_IOC_P2P_OPP_PS                100
#define APPLE80211_IOC_P2P_CT_WINDOW             101
#define APPLE80211_IOC_BT_COEX_FLAGS             102
#define APPLE80211_IOC_CURRENT_NETWORK           103
#define APPLE80211_IOC_BT_POWER                  104
#define APPLE80211_IOC_AVAILABILITY              105
#define APPLE80211_IOC_RSSI_BOUNDS               106
#define APPLE80211_IOC_ROAM                      107
#define APPLE80211_IOC_TX_CHAIN_POWER            108
#define APPLE80211_IOC_CDD_MODE                  109
#define APPLE80211_IOC_LAST_BCAST_SCAN_TIME      110
#define APPLE80211_IOC_THERMAL_THROTTLING        111
#define APPLE80211_IOC_FACTORY_MODE              112
#define APPLE80211_IOC_REASSOCIATE               113

#define APPLE80211_IOC_POWER_DEBUG_INFO 115
#define APPLE80211_IOC_AWDL_SYNC_PARAMS 116
#define APPLE80211_IOC_AWDL_SYNC_ENABLED 117
#define APPLE80211_IOC_AWDL_EXTENSION_STATE_MACHINE_PARAMETERS 118
#define APPLE80211_IOC_AWDL_SERVICE_PARAMS 119
#define APPLE80211_IOC_AWDL_PEER_SERVICE_REQUEST 120
#define APPLE80211_IOC_AWDL_ELECTION_ALGORITHM_ENABLED 121
#define APPLE80211_IOC_AWDL_ELECTION_ID 122
#define APPLE80211_IOC_AWDL_MAX_TREE_DEPTH 123
#define APPLE80211_IOC_AWDL_GUARD_TIME 124
#define APPLE80211_IOC_AWDL_BSSID 125
#define APPLE80211_IOC_AWDL_ELECTION_METRIC 126
#define APPLE80211_IOC_AWDL_AVAILABILITY_WINDOW_AP_ALIGNMENT 127
#define APPLE80211_IOC_AWDL_SYNC_FRAME_AP_BEACON_ALIGNMENT 128
#define APPLE80211_IOC_AWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE 129
#define APPLE80211_IOC_PEER_CACHE_MAXIMUM_SIZE 130
#define APPLE80211_IOC_AWDL_OUI 131
#define APPLE80211_IOC_AWDL_MASTER_CHANNEL 132
#define APPLE80211_IOC_AWDL_TOP_MASTER 133
#define APPLE80211_IOC_AWDL_SYNC_STATE 134
#define APPLE80211_IOC_AWDL_ELECTION_RSSI_THRESHOLDS 135
#define APPLE80211_IOC_AWDL_PRESENCE_MODE 136
#define APPLE80211_IOC_AWDL_ELECTION_MASTER_COUNTS 137
#define APPLE80211_IOC_AWDL_PERIODIC_SYNC_FRAME_PACKET_LIFETIME 138
#define APPLE80211_IOC_AWDL_MASTER_MODE_SYNC_FRAME_PERIOD 139
#define APPLE80211_IOC_AWDL_NON_ELECTION_MASTER_MODE_SYNC_FRAME_PERIOD 140
#define APPLE80211_IOC_AWDL_EXPLICIT_AVAILABILITY_WINDOW_EXTENSION_OPT_OUT 141
#define APPLE80211_IOC_AWDL_GET_AWDL_MASTER_DATABASE 142
#define APPLE80211_IOC_PEER_CACHE_CONTROL 143
#define APPLE80211_IOC_AWDL_BATTERY_LEVEL 144
#define APPLE80211_IOC_AWDL_BT_COEX_AW_PROTECTED_PERIOD_LENGTH 145
#define APPLE80211_IOC_AWDL_BT_COEX_AGREEMENT 146
#define APPLE80211_IOC_AWDL_BT_COEX_AGREEMENT_ENABLED 147
#define APPLE80211_IOC_AWDL_STRATEGY 148
#define APPLE80211_IOC_AWDL_OOB_REQUEST 149
#define APPLE80211_IOC_AWDL_MAX_NO_MASTER_PERIODS 150
#define APPLE80211_IOC_AWDL_SYNC_FRAME_TEMPLATE 151
#define APPLE80211_IOC_LOG_FLAGS 152
#define APPLE80211_IOC_PEER_STATS 153
#define APPLE80211_IOC_HT_CAPABILITY 154
#define APPLE80211_IOC_AWDL_ELECTION_PARAMS 155
#define APPLE80211_IOC_LINK_CHANGED_EVENT_DATA 156
#define APPLE80211_IOC_GET_DEBUG_INFO 157
#define APPLE80211_IOC_AWDL_DEVICE_CAPABILITIES 158
#define APPLE80211_IOC_AWDL_RSSI_MEASUREMENT_REQUEST 159
#define APPLE80211_IOC_AWDL_AES_KEY 160
#define APPLE80211_IOC_AWDL_SCAN_RESERVED_TIME 161
#define APPLE80211_IOC_AWDL_CTL 162
#define APPLE80211_IOC_AWDL_SOCIAL_TIME_SLOTS 163
#define APPLE80211_IOC_AWDL_PEER_TRAFFIC_REGISTRATION 164
#define APPLE80211_IOC_EXTENDED_STATS 165
#define APPLE80211_IOC_BEACON_PERIOD 166
#define APPLE80211_IOC_AWDL_FORCED_ROAM_CONFIG 167
#define APPLE80211_IOC_AWDL_QUIET 168
#define APPLE80211_IOC_ACL_POLICY 169
#define APPLE80211_IOC_ACL_ADD 170
#define APPLE80211_IOC_ACL_REMOVE 171
#define APPLE80211_IOC_ACL_FLUSH 172
#define APPLE80211_IOC_ACL_LIST 173
#define APPLE80211_IOC_CHAIN_ACK 174
#define APPLE80211_IOC_DESENSE 175
#define APPLE80211_IOC_OFFLOAD_SCANNING 176
#define APPLE80211_IOC_OFFLOAD_RSN 177
#define APPLE80211_IOC_OFFLOAD_COUNTRY_CODE 178
#define APPLE80211_IOC_OFFLOAD_KEEPALIVE_L2 179
#define APPLE80211_IOC_OFFLOAD_ARP_NDP 180
#define APPLE80211_IOC_VHT_MCS_INDEX_SET 181
#define APPLE80211_IOC_DWDS 182
#define APPLE80211_IOC_INTERRUPT_STATS 183
#define APPLE80211_IOC_INTERRUPT_STATS_RESET 184
#define APPLE80211_IOC_TIMER_STATS 185
#define APPLE80211_IOC_TIMER_STATS_RESET 186
#define APPLE80211_IOC_OFFLOAD_STATS 187
#define APPLE80211_IOC_OFFLOAD_STATS_RESET 188
#define APPLE80211_IOC_OFFLOAD_BEACONS 189
#define APPLE80211_IOC_ROAMING 190
#define APPLE80211_IOC_OFFLOAD_ARP 191
#define APPLE80211_IOC_OFFLOAD_NDP 192
#define APPLE80211_IOC_OFFLOAD_SCAN 193
#define APPLE80211_IOC_DESENSE_LEVEL 194
#define APPLE80211_IOC_MCS_VHT 195
#define APPLE80211_IOC_TX_NSS 196
#define APPLE80211_IOC_GAS_REQ 197
#define APPLE80211_IOC_GAS_START 198
#define APPLE80211_IOC_GAS_SET_PEER 199
#define APPLE80211_IOC_GAS_RESULTS 200
#define APPLE80211_IOC_AWDL_BTLE_PEER_INDICATION 201
#define APPLE80211_IOC_AWDL_BTLE_STATE_PARAMS 202
#define APPLE80211_IOC_AWDL_PEER_DATABASE 203
#define APPLE80211_IOC_AWDL_BTLE_ENABLE_SYNC_WITH_PARAMS 204
#define APPLE80211_IOC_AWDL_SECONDARY_MASTER_CHANNEL 205
#define APPLE80211_IOC_PHY_STATS 206
#define APPLE80211_IOC_CHANNELS_INFO 207
#define APPLE80211_IOC_AWDL_AF_TX_MODE 208
#define APPLE80211_IOC_ERROR_STRING 209
#define APPLE80211_IOC_ERROR_NO 210
#define APPLE80211_IOC_AWDL_PIGGYBACK_SCAN_REQ 211
#define APPLE80211_IOC_AWDL_PRIVATE_ELECTION_ID 212
#define APPLE80211_IOC_AWDL_MIN_RATE 213
#define APPLE80211_IOC_VHT_CAPABILITY 214
#define APPLE80211_IOC_BGSCAN_CACHE_RESULTS 215
#define APPLE80211_IOC_ROAM_PROFILE 216
#define APPLE80211_IOC_AWDL_OPER_MODE 217
#define APPLE80211_IOC_RESTORE_DEFAULTS 218
#define APPLE80211_IOC_AWDL_ENCRYPTION_KEYS 219
#define APPLE80211_IOC_AWDL_ENCRYPTION_TYPE 220
#define APPLE80211_IOC_BTCOEX_PROFILES 221
#define APPLE80211_IOC_BTCOEX_CONFIG 222
#define APPLE80211_IOC_AWDL_STATISTICS 223
#define APPLE80211_IOC_AWDL_ENABLE_ROAMING 224
#define APPLE80211_IOC_AWDL_OOB_AUTO_REQUEST 225
#define APPLE80211_IOC_AWDL_TXCAL_PERIOD 226
#define APPLE80211_IOC_CHIP_COUNTER_STATS 227
#define APPLE80211_IOC_DBG_GUARD_TIME_PARAMS 228
#define APPLE80211_IOC_AWDL_AWDL_ADVERTISERS 229
#define APPLE80211_IOC_LEAKY_AP_STATS_MODE 230
#define APPLE80211_IOC_CAPTURE 231
#define APPLE80211_IOC_LEAKY_AP_STATS 232
#define APPLE80211_IOC_AWDL_BLOCK_SET_COMMANDS 233
#define APPLE80211_IOC_LEAKY_AP_AWD_MODE 234
#define APPLE80211_IOC_BTCOEX_OPTIONS 235
#define APPLE80211_IOC_FORCE_SYNC_TO_PEER 236
#define APPLE80211_IOC_COUNTRY_CHANNELS 237
#define APPLE80211_IOC_PRIVATE_MAC 238
#define APPLE80211_IOC_RESET_CHIP 239
#define APPLE80211_IOC_CRASH 240
#define APPLE80211_IOC_RANGING_ENABLE 241
#define APPLE80211_IOC_RANGING_START 242
#define APPLE80211_IOC_RANGING_AUTHENTICATE 243
#define APPLE80211_IOC_AWDL_PREFERRED_CHANNELS 244
#define APPLE80211_IOC_LEAKY_AP_SSID_STATS 245
#define APPLE80211_IOC_AWDL_RSDB_CAPS 246
#define APPLE80211_IOC_AWDL_DEV_STATS 247
#define APPLE80211_IOC_LAST_ASSOC_HISTORY 248
#define APPLE80211_IOC_AWDL_COMMON_CHANNEL 249
#define APPLE80211_IOC_AWDL_PEERS_INFO 250
#define APPLE80211_IOC_TKO_PARAMS 251
#define APPLE80211_IOC_TKO_DUMP 252
#define APPLE80211_IOC_AWDL_NEARBY_LOG_TRIGGER 253
#define APPLE80211_IOC_HW_SUPPORTED_CHANNELS 254
#define APPLE80211_IOC_BTCOEX_PROFILE 255
#define APPLE80211_IOC_BTCOEX_PROFILE_ACTIVE 256
#define APPLE80211_IOC_TRAP_INFO 257
#define APPLE80211_IOC_THERMAL_INDEX 258
#define APPLE80211_IOC_MAX_NSS_FOR_AP 259
#define APPLE80211_IOC_BTCOEX_2G_CHAIN_DISABLE 260
#define APPLE80211_IOC_POWER_BUDGET 261
#define APPLE80211_IOC_AWDL_DFSP_CONFIG 262
#define APPLE80211_IOC_AWDL_DFSP_UCSA_CONFIG 263
#define APPLE80211_IOC_SCAN_BACKOFF_REPORT 264
#define APPLE80211_IOC_OFFLOAD_TCPKA_ENABLE 265
#define APPLE80211_IOC_RANGING_CAPS 266
#define APPLE80211_IOC_SUPPRESS_SCANS 267
#define APPLE80211_IOC_HOST_AP_MODE_HIDDEN 336
#define APPLE80211_IOC_LQM_CONFIG 337
#define APPLE80211_IOC_AWDL_CCA 338
#define APPLE80211_IOC_TRAP_CRASHTRACER_MINI_DUMP 339
#define APPLE80211_IOC_AWDL_SIDECAR_STATISTICS 340
#define APPLE80211_IOC_AWDL_CAPABILITIES    341
#define APPLE80211_IOC_LLW_PARAMS 344
#define APPLE80211_IOC_HE_CAPABILITY        345
#define APPLE80211_IOC_SOFTAP_PARAMS        347
#define APPLE80211_IOC_SOFTAP_TRIGGER_CSA   349
#define APPLE80211_IOC_SOFTAP_STATS         350
#define APPLE80211_IOC_AWDL_SIDECAR_DIAGNOSTICS 351
#define APPLE80211_IOC_SOFTAP_WIFI_NETWORK_INFO_IE  352
#define APPLE80211_IOC_NSS  353
#define APPLE80211_IOC_SET_MAC_ADDRESS 368
#define APPLE80211_IOC_SOFTAP_EXTENDED_CAPABILITIES_IE 403
#define APPLE80211_IOC_MIS_MAX_STA                     508

#define APPLE80211_IOC_CARD_SPECIFIC            0xffffffff    // req_type

// Kernel interface

// Bump this value when structures change
#define APPLE80211_VERSION    1

struct apple80211_ssid_data
{
    u_int32_t    version;
    u_int32_t    ssid_len;
    u_int8_t     ssid_bytes[APPLE80211_MAX_SSID_LEN];
};

struct apple80211_virt_if_create_data {
    uint32_t    version;
    uint8_t     mac[APPLE80211_ADDR_LEN];
    uint16_t    unk1;
    uint32_t    role;
    uint8_t     bsd_name[IFNAMSIZ];
} __attribute__((packed));

struct apple80211_virt_if_delete_data {
    uint32_t    version;
    uint8_t     bsd_name[IFNAMSIZ];
} __attribute__((packed));

struct apple80211_ht_capability {
    uint32_t    version;
    uint8_t     hc_id;              /* element ID */
    uint8_t     hc_len;             /* length in bytes */
    uint16_t    hc_cap;             /* HT caps (see below) */
    uint8_t     hc_param;           /* HT params (see below) */
    uint8_t     hc_mcsset[16];      /* supported MCS set */
    uint16_t    hc_extcap;          /* extended HT capabilities */
    uint32_t    hc_txbf;            /* txbf capabilities */
    uint8_t     hc_antenna;         /* antenna capabilities */
} __attribute__((packed));

struct apple80211_vht_capability {
    uint32_t    version;
    uint8_t     ie;
    uint8_t     len;
    uint32_t    vht_cap_info;
    uint16_t    rx_mcs_map;
    uint16_t    rx_highest;
    uint16_t    tx_mcs_map;
    uint16_t    tx_highest;
} __attribute__((packed));

struct apple80211_he_capability {
    uint32_t    version;
    uint8_t     reserved04[8];
    // AppleBCMWLANCore::getHE_CAPABILITY(...) touches only +0xc, +0xe and
    // +0x1a in the returned carrier. IO80211PeerManager later snapshots 0x24
    // bytes of that payload, so keep the ABI offset-accurate and opaque
    // instead of inventing semantic field names for the untouched gaps.
    uint16_t    capability_word;
    uint8_t     capability_byte;
    uint8_t     reserved0f[0x0b];
    uint64_t    capability_tail;
    uint16_t    reserved22;
} __attribute__((packed));
static_assert(sizeof(struct apple80211_he_capability) == 0x24,
              "apple80211_he_capability must preserve the recovered Tahoe layout");

struct apple80211_p2p_device_capability {
    // AppleBCMWLANCore::getP2P_DEVICE_CAPABILITY(...) zeroes the first byte and
    // only defers to the NAN owner when one exists. The Tahoe ABI here is a
    // one-byte carrier, not a versioned struct.
    uint8_t     capability;
} __attribute__((packed));
static_assert(sizeof(struct apple80211_p2p_device_capability) == 0x1,
              "apple80211_p2p_device_capability must remain a 1-byte carrier");

struct apple80211_private_mac_data {
    uint32_t    version;
    uint32_t    enabled;
    // AppleBCMWLANCore::getPRIVATE_MAC(...) writes the next 24 bytes as an
    // opaque packed carrier returned from the "scanmac" command path. The
    // exact semantic names of these fields are not fully recovered yet, so keep
    // the ABI offset-accurate instead of inventing labels Apple never used.
    uint32_t    scanmac_state;
    uint32_t    timeout_seconds;
    uint8_t     primary_mac[6];
    uint8_t     secondary_mac[6];
} __attribute__((packed));

static_assert(sizeof(struct apple80211_private_mac_data) == 0x1c,
              "apple80211_private_mac_data must match Apple 0x1c carrier ABI");

struct apple80211_set_mac_address_data {
    uint8_t     mac[6];
    uint8_t     reserved[3];
} __attribute__((packed));

static_assert(sizeof(struct apple80211_set_mac_address_data) == 0x09,
              "apple80211_set_mac_address_data must match observed Tahoe 9-byte carrier ABI");

struct apple80211_offload_tcpka_enable_t {
    uint32_t    version;
    uint32_t    enabled;
} __attribute__((packed));

static_assert(sizeof(struct apple80211_offload_tcpka_enable_t) == 0x08,
              "apple80211_offload_tcpka_enable_t must match Apple version + u32 ABI");

struct apple80211_lqm_config_t {
    uint32_t    version;
    uint32_t    sample_period_ms;
    uint32_t    tx_per_interval_ms;
    uint32_t    rx_loss_interval_ms;
    // Apple writes the rest of this carrier through unaligned offsets
    // (+0x11/+0x15/+0x17/+0x18/+0x19), so keep the ABI offset-accurate and
    // avoid inventing semantic field names that are not yet fully recovered.
    uint8_t     reserved10;
    uint32_t    opaque_word_11;
    uint16_t    opaque_word_15;
    uint8_t     opaque_byte_17;
    uint8_t     enabled;
    uint8_t     opaque_tail_19[8];
    uint8_t     reserved21[3];
} __attribute__((packed));

static_assert(sizeof(struct apple80211_lqm_config_t) == 0x24,
              "apple80211_lqm_config_t must preserve the recovered 0x24 Tahoe ABI");

struct apple80211_lqm_summary {
    // IO80211LQMData::getLQM_SUMMARY zeroes a fixed 0x15a0-byte caller blob.
    // The internal field map is not required for the consumer contract yet,
    // but the ABI size must stay exact for Tahoe callers.
    uint8_t     opaque[0x15a0];
} __attribute__((packed));

static_assert(sizeof(struct apple80211_lqm_summary) == 0x15a0,
              "apple80211_lqm_summary must match the fixed Tahoe summary blob");

struct apple80211_channel_data
{
    u_int32_t                    version;
    struct apple80211_channel    channel;
};

struct apple80211_bssid_data
{
    u_int32_t            version;
    struct ether_addr    bssid;
};

#if __IO80211_TARGET >= __MAC_26_0
struct apple80211_capability_data
{
    u_int32_t    version;
    // Tahoe AppleBCMWLANCore::getCARD_CAPABILITIES() writes through byte
    // offset +0x17, so the public carrier is 0x1c bytes total.
    u_int8_t     capabilities[24];
};
#elif __IO80211_TARGET >= __MAC_14_0
struct apple80211_capability_data
{
    u_int32_t    version;
    u_int8_t     capabilities[14];
};
#else
struct apple80211_capability_data
{
    u_int32_t    version;
    u_int8_t     capabilities[11];
};
#endif

struct apple80211_state_data
{
    u_int32_t    version;
    u_int32_t    state;
};

struct apple80211_rssi_data
{
    u_int32_t    version;
    u_int32_t    num_radios;
    u_int32_t    rssi_unit;
    int32_t      rssi[APPLE80211_MAX_RADIO];        // control channel
    int32_t      aggregate_rssi;                    // aggregate control channel rssi
    int32_t      rssi_ext[APPLE80211_MAX_RADIO];    // extension channel rssi
    int32_t      aggregate_rssi_ext;                // aggregate extension channel rssi
};

struct apple80211_power_data
{
    u_int32_t    version;
    u_int32_t    num_radios;
    u_int32_t    power_state[APPLE80211_MAX_RADIO];
};

struct apple80211_assoc_result_data
{
    u_int32_t    version;
    u_int32_t    result;
};

struct apple80211_assoc_status_data
{
    u_int32_t    version;
    u_int32_t    status;
};

struct apple80211_rate_data
{
    u_int32_t    version;
    u_int32_t    num_radios;
    u_int32_t    rate[APPLE80211_MAX_RADIO];
};

struct apple80211_status_dev_data
{
    u_int32_t    version;
    u_int8_t     dev_name[MAXPATHLEN];
};

struct apple80211_powersave_data
{
    u_int32_t    version;
    u_int32_t    powersave_level;
};

struct apple80211_protmode_data
{
    u_int32_t    version;
    u_int32_t    protmode;
    u_int32_t    threshold;        // bytes
};

struct apple80211_txpower_data
{
    u_int32_t    version;
    u_int32_t    txpower_unit;
    int32_t      txpower;
};

struct apple80211_phymode_data
{
    u_int32_t    version;
    u_int32_t    phy_mode;            // vector of supported phy modes
    u_int32_t    active_phy_mode;     // current active phy mode
};

struct apple80211_opmode_data
{
    u_int32_t    version;
    u_int32_t    op_mode;
};

struct apple80211_noise_data
{
    u_int32_t    version;
    u_int32_t    num_radios;
    u_int32_t    noise_unit;
    int32_t      noise[APPLE80211_MAX_RADIO];        // control channel
    int32_t      aggregate_noise;                    // aggregate control channel noise
    int32_t      noise_ext[APPLE80211_MAX_RADIO];    // extension channel noise
    int32_t      aggregate_noise_ext;                // aggregate extension channel noise
};

struct apple80211_intmit_data
{
    u_int32_t    version;
    u_int32_t    int_mit;
};

struct apple80211_authtype_data
{
    u_int32_t    version;
    u_int32_t    authtype_lower;    //    apple80211_authtype_lower
    u_int32_t    authtype_upper;    //    apple80211_authtype_upper
};

struct apple80211_sup_channel_data
{
    u_int32_t                    version;
    u_int32_t                    num_channels;
    struct apple80211_channel    supported_channels[APPLE80211_MAX_CHANNELS];
};


struct apple80211_roam_threshold_data
{
    u_int32_t threshold;
    u_int32_t count;
};

struct apple80211_locale_data
{
    u_int32_t    version;
    u_int32_t    locale;
};

struct apple80211_scan_data
{
    u_int32_t                    version;
    u_int32_t                    bss_type;                            // apple80211_apmode
    struct ether_addr            bssid;                               // target BSSID
    u_int32_t                    ssid_len;                            // length of the SSID
    u_int8_t                     ssid[APPLE80211_MAX_SSID_LEN];       // direct scan ssid or AirDrop scan ssid like "Air-xxxx"
    u_int32_t                    scan_type;                           // apple80211_scan_type
    u_int32_t                    phy_mode;                            // apple80211_phymode vector
    u_int16_t                    dwell_time;                          // time to spend on each channel (ms)
    u_int32_t                    rest_time;                           // time between scanning each channel (ms)
    u_int32_t                    num_channels;                        // 0 if not passing in channels
    struct apple80211_channel    channels[APPLE80211_MAX_CHANNELS];   // channel list
};

struct apple80211_scan_multiple_data
{
    uint32_t                  version;
    uint32_t                  ap_mode; // apple80211_apmode
    uint32_t                  ssid_count;
    apple80211_ssid_data      ssids[10];
    uint32_t                  bssid_count;
    ether_addr                bssids[16];
    uint32_t                  scan_type;
    uint32_t                  phy_mode;
    uint32_t                  dwell_time;
    uint32_t                  rest_time;
    uint32_t                  num_channels;
    struct apple80211_channel channels[APPLE80211_MAX_CHANNELS];
    uint16_t                  unk_2;
};

static_assert(__offsetof(struct apple80211_scan_multiple_data, bssid_count) == 0x19c, "zxystd: BSSID offset invalid");

/*
 * apple80211_link_changed_event_data - Tahoe IOCTL ABI.
 *
 * The recovered Apple reference uses a 32-byte response on the
 * APPLE80211_IOC_LINK_CHANGED_EVENT_DATA IOCTL: the userspace
 * consumer allocates 32 bytes for the response buffer and the
 * kernel publisher writes voluntary at +0x1c (link-down) /
 * +0x1d (link-up), packs the link reason and instantaneous RSSI
 * into a union at +0x04, exposes SNR / NF / CCA at +0x08 / +0x0a /
 * +0x0c, and copies up to twelve bytes of last-association payload
 * at +0x10..+0x1b on link-down. Fields not produced by this driver
 * remain zero per the bzero entry contract.
 */
struct apple80211_link_changed_event_data
{
    uint8_t    isLinkDown;        // +0x00
    uint8_t    _pad_01[3];        // +0x01..+0x03
    union {                       // +0x04..+0x07
        uint32_t rssi;            //   when isLinkDown == 0 (link-up RSSI)
        uint32_t reason;          //   when isLinkDown == 1 (apple80211_link_down_reason)
    };
    uint16_t   snr;               // +0x08
    uint16_t   nf;                // +0x0a
    int8_t     cca;               // +0x0c
    uint8_t    _pad_0d[3];        // +0x0d..+0x0f
    uint8_t    last_assoc[12];    // +0x10..+0x1b (filled on link-down only)
    uint8_t    voluntary_down;    // +0x1c
    uint8_t    voluntary_up;      // +0x1d
    uint8_t    _pad_1e[2];        // +0x1e..+0x1f
};

static_assert(sizeof(struct apple80211_link_changed_event_data) == 0x20,
              "apple80211_link_changed_event_data must be 32 bytes (Tahoe IOCTL ABI)");
static_assert(__offsetof(struct apple80211_link_changed_event_data, voluntary_down) == 0x1c,
              "voluntary_down must live at +0x1c per Tahoe IOCTL ABI");
static_assert(__offsetof(struct apple80211_link_changed_event_data, voluntary_up) == 0x1d,
              "voluntary_up must live at +0x1d per Tahoe IOCTL ABI");

/*
 * Tahoe event payload ABIs - three distinct carriers.
 *
 * The recovered Apple/Tahoe reference event-publication path distinguishes
 * three event-payload ABIs that share the postMessage / IO80211Glue
 * pending-queue routing infrastructure but must never be conflated at the
 * payload-shape level:
 *
 *   1. 32-byte link-changed snapshot/getter payload
 *      (APPLE80211_M_LINK_CHANGED = 4) carried by struct
 *      apple80211_link_changed_event_data above. The struct is the
 *      length-checked inline payload for APPLE80211_M_LINK_CHANGED
 *      publication and the on-demand response for the SIOCGA80211 ioctl
 *      path APPLE80211_IOC_LINK_CHANGED_EVENT_DATA = 156.
 *   2. 24-byte BSSID-changed compact carrier (APPLE80211_M_BSSID_CHANGED
 *      = 3) carried by struct apple80211_bssid_changed_event_data below.
 *      The carrier was recovered with length 0x18, BSSID at offset +0x00,
 *      apple80211_channel at offset +0x08, and reason at offset +0x14.
 *      IO80211InfraInterface::bssidChange(data, 0x18) consumes the embedded
 *      channel before normal event delivery, calling
 *      IO80211Controller::setInfraChannel(data + 0x08) and updating the
 *      framework-visible channel properties. The WCL writer reached through
 *      IOUC selector 0x1b1 still owns the current-BSS validation,
 *      WCLBSSBeacon validation, zero-BSSID rejection, and same-BSS reason-1
 *      suppression rule: a publication whose reason field equals 1 and whose
 *      bssid matches the last published bssid is suppressed; any other
 *      reason or bssid transition publishes the populated 24-byte payload
 *      through the standard IO80211Controller::postMessage / IO80211Glue
 *      pending-queue routing. Tahoe userspace length-checks this carrier and
 *      rejects zero-length publications, so the populated 24-byte payload is
 *      the only valid Tahoe shape for this event.
 *   3. 16-byte WCL link-state update carrier (WCL event code 0xd8) carried
 *      by struct TahoeWclLinkChangedPayload defined in AirportItlwmV2.cpp.
 *      This is a direct WCL link-state update path produced by
 *      AppleBCMWLAN link-state handlers; it is not byte-synthesised from
 *      the 32-byte link-changed payload, and the 24-byte BSSID-changed and
 *      32-byte link-changed payloads must not be byte-synthesised from the
 *      16-byte 0xd8 payload either. The three ABIs are independent.
 *
 * The shared IO80211Controller::postMessage / IO80211SkywalkInterface
 * dispatch path routes these events through the IO80211Glue pending-queue
 * pipeline (queued copy on enqueue, filter decision via isMsgNeeded, async
 * versus sync routing through IO80211PostOffice::sendMail /
 * sendMailSync, pending-list drain through processPendingEventQueue,
 * free-after-delivery ownership in the postMessage layer). The local kext
 * does not manage these lifecycle steps directly; it passes the populated
 * payload and the asynchronous-delivery flag through postMessage and the
 * framework handles copy/filter/route/drain/free.
 */

#define APPLE80211_BSSID_CHANGE_REASON_INITIAL  0
#define APPLE80211_BSSID_CHANGE_REASON_SAME_BSS 1

struct apple80211_bssid_changed_event_data
{
    uint8_t  bssid[6];        // +0x00..+0x05
    uint8_t  _pad_06[0x02];   // +0x06..+0x07
    struct apple80211_channel channel; // +0x08..+0x13
    uint32_t reason;          // +0x14..+0x17 (Apple BSSID change reason;
                              //                value 1 is the suppression
                              //                marker when bssid is
                              //                unchanged)
};

static_assert(sizeof(struct apple80211_bssid_changed_event_data) == 0x18,
              "apple80211_bssid_changed_event_data must be 24 bytes "
              "(Tahoe compact carrier ABI)");
static_assert(__offsetof(struct apple80211_bssid_changed_event_data, bssid) == 0x00,
              "bssid must live at +0x00 per Tahoe IOCTL ABI");
static_assert(__offsetof(struct apple80211_bssid_changed_event_data, channel) == 0x08,
              "channel must live at +0x08 per Tahoe IOCTL ABI");
static_assert(__offsetof(struct apple80211_bssid_changed_event_data, reason) == 0x14,
              "reason must live at +0x14 per Tahoe IOCTL ABI");

struct apple80211_apmode_data
{
    u_int32_t    version;
    u_int32_t    apmode;
};

struct apple80211_assoc_data
{
    u_int32_t                version;
    u_int16_t                ad_mode;          // apple80211_apmode
    u_int16_t                ad_auth_lower;    // apple80211_authtype_lower
    u_int16_t                ad_auth_upper;    // apple80211_authtype_upper
    u_int32_t                ad_ssid_len;
    u_int8_t                 ad_ssid[ APPLE80211_MAX_SSID_LEN ];
    struct ether_addr        ad_bssid;         // prefer over ssid if not zeroed
    struct apple80211_key    ad_key;
    uint16_t                 ad_rsn_ie_len;
    u_int8_t                 ad_rsn_ie[ APPLE80211_MAX_RSN_IE_LEN + 1 ];
    u_int32_t                ad_flags;         // apple80211_assoc_flags
};

static_assert(offsetof(apple80211_assoc_data, ad_key) == 0x38, "aaaa");

static_assert(offsetof(apple80211_assoc_data, ad_rsn_ie) == 206, "offsetof(apple80211_assoc_data, ad_rsn_ie)");
static_assert(offsetof(apple80211_assoc_data, ad_flags) == 464, "ad_flags offset error");

struct apple80211_deauth_data
{
    u_int32_t            version;
    u_int32_t            deauth_reason;    // reason code
    struct ether_addr    deauth_ea;        // BSSID of AP
};

struct apple80211_countermeasures_data
{
    u_int32_t    version;
    u_int32_t    enabled;
};

struct apple80211_frag_threshold_data
{
    u_int32_t    version;
    u_int32_t    threshold;    // bytes
};

struct apple80211_rate_set_data
{
    u_int32_t                version;
    u_int16_t                num_rates;
    struct apple80211_rate   rates[APPLE80211_MAX_RATES];
};

struct apple80211_short_slot_data
{
    u_int32_t    version;
    u_int8_t     mode;
};

struct apple80211_retry_limit_data
{
    u_int32_t    version;
    u_int32_t    limit;
};

struct apple80211_antenna_data
{
    u_int32_t    version;
    u_int32_t    num_radios;
    int32_t      antenna_index[APPLE80211_MAX_RADIO];
};

struct apple80211_dtim_int_data
{
    u_int32_t    version;
    u_int32_t    interval;
};

struct apple80211_sta_data
{
    u_int32_t                    version;
    u_int32_t                    num_stations;
    struct apple80211_station    station_list[APPLE80211_MAX_STATIONS];
};

struct apple80211_version_data
{
    u_int32_t    version;
    u_int16_t    string_len;
    char         string[APPLE80211_MAX_VERSION_LEN];
};

struct apple80211_rom_data
{
    u_int32_t    version;
    u_int32_t    rom_len;
    u_int8_t     rom[1];    // variable length
};

struct apple80211_rand_data
{
    u_int32_t    version;
    u_int32_t    rand;
};

struct apple80211_rsn_ie_data
{
    u_int32_t    version;
    u_int16_t    len;
    u_int8_t     ie[ APPLE80211_MAX_RSN_IE_LEN ];
};

struct apple80211_ap_ie_data
{
    u_int32_t    version;
    u_int32_t    len;
#if __IO80211_TARGET < __MAC_13_0
    u_int8_t     *ie_data;
#else
    u_int8_t     ie_data[APPLE80211_NETWORK_DATA_MAX_IE_LEN];
#endif
};

struct apple80211_stats_data
{
    u_int32_t    version;
    u_int32_t    tx_frame_count;
    u_int32_t    tx_errors;
    u_int32_t    rx_frame_count;
    u_int32_t    rx_errors;
};

struct apple80211_country_code_data
{
    u_int32_t    version;
    u_int8_t     cc[APPLE80211_MAX_CC_LEN];
};

struct apple80211_hw_mac_address
{
    u_int32_t    version;
    u_int8_t     hw_addr[6];
} __attribute__((packed));

static_assert(sizeof(apple80211_hw_mac_address) == 0x0A,
              "apple80211_hw_mac_address must match Apple version + 6-byte MAC ABI");

struct apple80211_chip_power_limit
{
    u_int32_t version;
    u_int64_t wlan_pwr_duty_cycle[6];
} __attribute__((packed));

static_assert(sizeof(apple80211_chip_power_limit) == 0x34,
              "apple80211_chip_power_limit must match Apple packed 6x u64 duty-cycle ABI");

struct apple80211_last_rx_pkt_data
{
    u_int32_t         version;
    u_int32_t         rate;
    int32_t           rssi;
    u_int32_t         num_streams;    // number of spatial streams
    struct ether_addr sa;             // source address
};

struct apple80211_radio_info_data
{
    u_int32_t    version;
    u_int32_t    count;        // number of rf chains
};

struct apple80211_guard_interval_data
{
    u_int32_t    version;
    u_int32_t    interval;    // apple80211_guard_interval
};

struct apple80211_thermal_index_t
{
    u_int32_t    version;
    u_int32_t    thermal_index;
};
static_assert(sizeof(apple80211_thermal_index_t) == 0x08,
              "apple80211_thermal_index_t must match Apple version + u32 carrier ABI");

struct apple80211_power_budget_t
{
    u_int32_t    version;
    u_int32_t    power_budget;
};
static_assert(sizeof(apple80211_power_budget_t) == 0x08,
              "apple80211_power_budget_t must match Apple version + u32 carrier ABI");

struct apple80211_mcs_data
{
    u_int32_t    version;
    u_int32_t    index;        // 0 to APPLE80211_MAX_MCS_INDEX
};

struct apple80211_rifs_data
{
    u_int32_t    version;
    u_int32_t    enabled;
};

struct apple80211_ldpc_data
{
    u_int32_t    version;
    u_int32_t    enabled;
};

struct apple80211_msdu_data
{
    u_int32_t    version;
    u_int32_t    max_length;        // 3839 or 7935 bytes
};

struct apple80211_mpdu_data
{
    u_int32_t    version;
    u_int32_t    max_factor;        // 0 - APPLE80211_MAX_MPDU_FACTOR
    u_int32_t    max_density;       // 0 - APPLE80211_MAX_MPDU_DENSITY
};

struct apple80211_block_ack_data
{
    u_int32_t   version;
    u_int8_t    ba_enabled;             // block ack enabled
    u_int8_t    immediate_ba_enabled;   // immediate block ack enabled
    u_int8_t    cbba_enabled;           // compressed bitmap block ack enabled
    u_int8_t    implicit_ba_enabled;    // implicit block ack enabled
};

struct apple80211_pls_data
{
    u_int32_t    version;
    u_int32_t    enabled;    // phy level spoofing enabled
};

struct apple80211_psmp_data
{
    u_int32_t    version;
    u_int32_t    enabled;
};

struct apple80211_physubmode_data
{
    u_int32_t    version;
    u_int32_t    phy_mode;       // one apple80211_phymode
    u_int32_t    phy_submode;    // one apple80211_physubmode
    u_int32_t    flags;          // apple80211_channel_flag vector
};

struct apple80211_mcs_index_set_data
{
    u_int32_t   version;
    u_int8_t    mcs_set_map[APPLE80211_MAP_SIZE( APPLE80211_MAX_MCS_INDEX + 1 )];
};

struct apple80211_vht_mcs_index_set_data
{
    u_int32_t   version;
    u_int16_t   mcs_map;
    u_int16_t   reserved;
};

struct apple80211_he_mcs_index_set_data
{
    u_int32_t   version;
    u_int16_t   mcs_map;
    u_int16_t   reserved;
};

#ifdef __cplusplus
static_assert(sizeof(apple80211_vht_mcs_index_set_data) == 0x08,
              "Tahoe VHT MCS index carrier is copied as one qword");
static_assert(sizeof(apple80211_he_mcs_index_set_data) == 0x08,
              "Tahoe HE MCS index carrier is copied as one qword");
#endif

struct apple80211_mcs_vht_data 
{
    u_int32_t   version;
    u_int32_t   index;
    u_int32_t   nss;
    u_int32_t   bw;
    u_int32_t   guard_interval;
} __attribute__((packed));

struct apple80211_wow_parameter_data
{
    u_int32_t                     version;
    u_int8_t                      wake_cond_map[APPLE80211_MAP_SIZE( APPLE80211_MAX_WAKE_COND + 1 )];
    u_int32_t                     beacon_loss_time;
    u_int32_t                     pattern_count;
    struct apple80211_wow_pattern patterns[APPLE80211_MAX_WOW_PATTERNS];
};

struct apple80211_40mhz_intolerant_data
{
    u_int32_t    version;
    u_int32_t    enabled;    // bit enabled or not
};

struct apple80211_tx_nss_data
{
    uint32_t    version;
    uint8_t     nss;
};

struct apple80211_nss_data
{
    uint32_t    version;
    uint8_t     nss;
};

struct apple80211_awdl_peer_traffic_registration
{
    uint32_t    version;
    void        *addr;
    uint32_t    name_len;
    char        name[152];
    uint32_t    active;
} __attribute__((packed));

struct apple80211_awdl_election_metric
{
    uint32_t    version;
    uint32_t    metric;
} __attribute__((packed));

struct apple80211_awdl_sync_enabled
{
    uint32_t    version;
    uint32_t    unk1;
    uint32_t    enabled;
} __attribute__((packed));

struct apple80211_awdl_sync_frame_template
{
    uint32_t    version;
    uint32_t    payload_len;
    void        *payload;
} __attribute__((packed));

struct apple80211_awdl_bssid {
    uint32_t    version;
    uint8_t     bssid[APPLE80211_ADDR_LEN];
    uint8_t     unk_mac[APPLE80211_ADDR_LEN];
} __attribute__((packed));

struct apple80211_awdl_channel {
    uint16_t    chan_spec;
    uint8_t     chan_num;
    uint8_t     indoor_restric;
    uint8_t     radar_dfs;
    uint8_t     passive;
    uint8_t     support_40Mhz;
    uint8_t     support_80Mhz;
    uint8_t     z;
    uint32_t    per_chan;
    uint32_t    chan_bitmap;
} __attribute__((packed));

struct apple80211_channels_info {
    uint32_t    version;
    uint32_t    unk1;
    uint16_t    num_chan_specs;
    uint16_t    chan_spec[APPLE80211_MAX_CHANNELS];
    uint8_t     chan_num[APPLE80211_MAX_CHANNELS];
    uint8_t     indoor_restric[APPLE80211_MAX_CHANNELS];
    uint8_t     radar_dfs[APPLE80211_MAX_CHANNELS];
    uint8_t     passive[APPLE80211_MAX_CHANNELS];
    uint8_t     support_40Mhz[APPLE80211_MAX_CHANNELS];
    uint8_t     support_80Mhz[APPLE80211_MAX_CHANNELS];
    uint8_t     z[APPLE80211_MAX_CHANNELS];
    uint8_t     pad[386];
    uint32_t    per_chan[APPLE80211_MAX_CHANNELS];
    uint32_t    chan_bitmap[APPLE80211_MAX_CHANNELS];
} __attribute__((packed));

static_assert(__offsetof(struct apple80211_channels_info, chan_num) == 0x10A, "invalid offset");   //wf_chspec_ctlchan
static_assert(__offsetof(struct apple80211_channels_info, indoor_restric) == 0x18A, "invalid offset"); //wlc_restricted_chanspec
static_assert(__offsetof(struct apple80211_channels_info, radar_dfs) == 0x20A, "invalid offset");  //wlc_radar_chanspec
static_assert(__offsetof(struct apple80211_channels_info, passive) == 0x28A, "invalid offset");    //wlc_quiet_chanspec
static_assert(__offsetof(struct apple80211_channels_info, support_40Mhz) == 0x30A, "invalid offset");
static_assert(__offsetof(struct apple80211_channels_info, support_80Mhz) == 0x38A, "invalid offset");
static_assert(__offsetof(struct apple80211_channels_info, per_chan) == 0x60C, "invalid offset");

struct apple80211_peer_cache_maximum_size {
    uint32_t    version;
    uint32_t    max_peers;
} __attribute__((packed));

struct apple80211_awdl_election_id {
    uint32_t    version;
    uint32_t    election_id;
} __attribute__((packed));

struct apple80211_awdl_master_channel {
    uint32_t    version;
    uint32_t    master_channel;
} __attribute__((packed));

struct apple80211_awdl_secondary_master_channel {
    uint32_t    version;
    uint32_t    secondary_master_channel;
} __attribute__((packed));

struct apple80211_awdl_min_rate {
    uint32_t    version;
    uint8_t    min_rate;
} __attribute__((packed));

struct apple80211_awdl_election_rssi_thresholds {
    uint32_t    version;
    uint32_t    unk1;
    uint32_t    unk2;
    uint32_t    unk3;
} __attribute__((packed));

struct apple80211_channel_sequence {
    uint16_t    flags;
    uint8_t     pad;
} __attribute__((packed));

struct apple80211_awdl_sync_channel_sequence {
    uint32_t    version;
    uint8_t     pad1;
    uint8_t     length;             // 5
    uint8_t     encoding;           // 6
    uint8_t     step_count;         // 7
    uint8_t     duplicate_count;    // 8
    uint8_t     fill_channel;       // 9
    uint8_t     pad2[6];
    struct apple80211_channel_sequence seqs[APPLE80211_MAX_CHANNELS];
} __attribute__((packed));

static_assert(__offsetof(apple80211_awdl_sync_channel_sequence, seqs) == 16, "seqs offset error");

static_assert(sizeof(struct apple80211_awdl_sync_channel_sequence) == 0x190, "apple80211_awdl_sync_channel_sequence struct corrupt");

struct apple80211_awdl_presence_mode {
    uint32_t    version;
    uint32_t    mode;
} __attribute__((packed));

struct apple80211_awdl_extension_state_machine_parameter {
    uint32_t    version;
    uint32_t    unk1;
    uint32_t    unk2;
    uint32_t    unk3;
    uint32_t    unk4;
} __attribute__((packed));

struct apple80211_awdl_sync_state {
    uint32_t    version;
    uint32_t    state;
} __attribute__((packed));

struct apple80211_awdl_sync_params {
    uint32_t    version;
    uint32_t    availability_window_length;
    uint32_t    availability_window_period;
    uint32_t    extension_length;
    uint32_t    synchronization_frame_period;
} __attribute__((packed));

struct apple80211_awdl_cap {
    uint32_t    version;
    uint8_t     cap;
} __attribute__((packed));

struct apple80211_awdl_af_tx_mode {
    uint32_t    version;
    uint64_t    mode;
} __attribute__((packed));

#define AWDL_OOB_AF_PARAMS_SIZE 38

struct apple80211_awdl_oob_request {
    uint32_t    version;
    uint32_t    unk1;               // 4
    uint32_t    unk2;               // 8
    uint32_t    unk3;               // 12
    uint32_t    unk4;               // 16
    uint16_t    pad1;
    uint32_t    unk5;               // 22
    uint16_t    unk6;               // 26
    uint32_t    pad2;
    uint32_t    unk7;               // 32
    uint32_t    pad3;
    uint16_t    data_len;           // 40
    uint32_t    pad4;
    uint16_t    unk9;               // 44
    uint8_t     data[1782];         // 48
} __attribute__((packed));

//Roam: <airport[565]> DISABLED, 2.4GHz on awdl0 => {
//    "ROAM_PROF" =     (
//                {
//            "ROAM_PROF_BACKOFF_MULTIPLIER" = 10;
//            "ROAM_PROF_FULLSCAN_PERIOD" = 3000;
//            "ROAM_PROF_INIT_SCAN_PERIOD" = 600;
//            "ROAM_PROF_MAX_SCAN_PERIOD" = 60000;
//            "ROAM_PROF_NFSCAN" = 1;
//            "ROAM_PROF_ROAM_DELTA" = 50;
//            "ROAM_PROF_ROAM_FLAGS" = 0;
//            "ROAM_PROF_ROAM_TRIGGER" = "-120";
//            "ROAM_PROF_RSSI_BOOST_DELTA" = 55;
//            "ROAM_PROF_RSSI_BOOST_THRESH" = "-68";
//            "ROAM_PROF_RSSI_LOWER" = "-128";
//        }
//    );
//    "ROAM_PROF_BAND" = 4;
//    "ROAM_PROF_NUM" = 1;
//}
//Roam: <airport[565]> SINGLE-BAND, SINGLE-AP, 2.4GHz on awdl0 => {
//    "ROAM_PROF" =     (
//                {
//            "ROAM_PROF_BACKOFF_MULTIPLIER" = 2;
//            "ROAM_PROF_FULLSCAN_PERIOD" = 3600;
//            "ROAM_PROF_INIT_SCAN_PERIOD" = 60;
//            "ROAM_PROF_MAX_SCAN_PERIOD" = 1200;
//            "ROAM_PROF_NFSCAN" = 1;
//            "ROAM_PROF_ROAM_DELTA" = 12;
//            "ROAM_PROF_ROAM_FLAGS" = 0;
//            "ROAM_PROF_ROAM_TRIGGER" = "-80";
//            "ROAM_PROF_RSSI_BOOST_DELTA" = 55;
//            "ROAM_PROF_RSSI_BOOST_THRESH" = "-68";
//            "ROAM_PROF_RSSI_LOWER" = "-128";
//        }
//    );
//    "ROAM_PROF_BAND" = 4;
//    "ROAM_PROF_NUM" = 1;
//}
//Roam: <airport[565]> DUAL-BAND, SINGLE-AP, 2.4GHz on awdl0 => {
//    "ROAM_PROF" =     (
//                {
//            "ROAM_PROF_BACKOFF_MULTIPLIER" = 2;
//            "ROAM_PROF_FULLSCAN_PERIOD" = 300;
//            "ROAM_PROF_INIT_SCAN_PERIOD" = 180;
//            "ROAM_PROF_MAX_SCAN_PERIOD" = 180;
//            "ROAM_PROF_NFSCAN" = 2;
//            "ROAM_PROF_ROAM_DELTA" = 16;
//            "ROAM_PROF_ROAM_FLAGS" = 1;
//            "ROAM_PROF_ROAM_TRIGGER" = "-10";
//            "ROAM_PROF_RSSI_BOOST_DELTA" = 55;
//            "ROAM_PROF_RSSI_BOOST_THRESH" = "-68";
//            "ROAM_PROF_RSSI_LOWER" = "-75";
//        },
//                {
//            "ROAM_PROF_BACKOFF_MULTIPLIER" = 2;
//            "ROAM_PROF_FULLSCAN_PERIOD" = 120;
//            "ROAM_PROF_INIT_SCAN_PERIOD" = 20;
//            "ROAM_PROF_MAX_SCAN_PERIOD" = 90;
//            "ROAM_PROF_NFSCAN" = 2;
//            "ROAM_PROF_ROAM_DELTA" = 12;
//            "ROAM_PROF_ROAM_FLAGS" = 0;
//            "ROAM_PROF_ROAM_TRIGGER" = "-75";
//            "ROAM_PROF_RSSI_BOOST_DELTA" = 55;
//            "ROAM_PROF_RSSI_BOOST_THRESH" = "-68";
//            "ROAM_PROF_RSSI_LOWER" = "-128";
//        }
//    );
//    "ROAM_PROF_BAND" = 4;
//    "ROAM_PROF_NUM" = 2;
//}
//Roam: <airport[565]> MULTI-AP, 2.4GHz on awdl0 => {
//    "ROAM_PROF" =     (
//                {
//            "ROAM_PROF_BACKOFF_MULTIPLIER" = 2;
//            "ROAM_PROF_FULLSCAN_PERIOD" = 1200;
//            "ROAM_PROF_INIT_SCAN_PERIOD" = 180;
//            "ROAM_PROF_MAX_SCAN_PERIOD" = 600;
//            "ROAM_PROF_NFSCAN" = 2;
//            "ROAM_PROF_ROAM_DELTA" = 20;
//            "ROAM_PROF_ROAM_FLAGS" = 1;
//            "ROAM_PROF_ROAM_TRIGGER" = "-10";
//            "ROAM_PROF_RSSI_BOOST_DELTA" = 55;
//            "ROAM_PROF_RSSI_BOOST_THRESH" = "-68";
//            "ROAM_PROF_RSSI_LOWER" = "-50";
//        },
//                {
//            "ROAM_PROF_BACKOFF_MULTIPLIER" = 2;
//            "ROAM_PROF_FULLSCAN_PERIOD" = 600;
//            "ROAM_PROF_INIT_SCAN_PERIOD" = 180;
//            "ROAM_PROF_MAX_SCAN_PERIOD" = 180;
//            "ROAM_PROF_NFSCAN" = 2;
//            "ROAM_PROF_ROAM_DELTA" = 16;
//            "ROAM_PROF_ROAM_FLAGS" = 1;
//            "ROAM_PROF_ROAM_TRIGGER" = "-50";
//            "ROAM_PROF_RSSI_BOOST_DELTA" = 55;
//            "ROAM_PROF_RSSI_BOOST_THRESH" = "-68";
//            "ROAM_PROF_RSSI_LOWER" = "-75";
//        },
//                {
//            "ROAM_PROF_BACKOFF_MULTIPLIER" = 1;
//            "ROAM_PROF_FULLSCAN_PERIOD" = 120;
//            "ROAM_PROF_INIT_SCAN_PERIOD" = 20;
//            "ROAM_PROF_MAX_SCAN_PERIOD" = 90;
//            "ROAM_PROF_NFSCAN" = 2;
//            "ROAM_PROF_ROAM_DELTA" = 12;
//            "ROAM_PROF_ROAM_FLAGS" = 0;
//            "ROAM_PROF_ROAM_TRIGGER" = "-75";
//            "ROAM_PROF_RSSI_BOOST_DELTA" = 55;
//            "ROAM_PROF_RSSI_BOOST_THRESH" = "-68";
//            "ROAM_PROF_RSSI_LOWER" = "-128";
//        }
//    );
//    "ROAM_PROF_BAND" = 4;
//    "ROAM_PROF_NUM" = 3;
//}
//Roam: <airport[565]> AC POWER, 2.4GHz on awdl0 => {
//    "ROAM_PROF" =     (
//                {
//            "ROAM_PROF_BACKOFF_MULTIPLIER" = 2;
//            "ROAM_PROF_FULLSCAN_PERIOD" = 120;
//            "ROAM_PROF_INIT_SCAN_PERIOD" = 20;
//            "ROAM_PROF_MAX_SCAN_PERIOD" = 90;
//            "ROAM_PROF_NFSCAN" = 2;
//            "ROAM_PROF_ROAM_DELTA" = 16;
//            "ROAM_PROF_ROAM_FLAGS" = 1;
//            "ROAM_PROF_ROAM_TRIGGER" = "-10";
//            "ROAM_PROF_RSSI_BOOST_DELTA" = 55;
//            "ROAM_PROF_RSSI_BOOST_THRESH" = "-68";
//            "ROAM_PROF_RSSI_LOWER" = "-75";
//        },
//                {
//            "ROAM_PROF_BACKOFF_MULTIPLIER" = 2;
//            "ROAM_PROF_FULLSCAN_PERIOD" = 120;
//            "ROAM_PROF_INIT_SCAN_PERIOD" = 20;
//            "ROAM_PROF_MAX_SCAN_PERIOD" = 90;
//            "ROAM_PROF_NFSCAN" = 2;
//            "ROAM_PROF_ROAM_DELTA" = 12;
//            "ROAM_PROF_ROAM_FLAGS" = 0;
//            "ROAM_PROF_ROAM_TRIGGER" = "-75";
//            "ROAM_PROF_RSSI_BOOST_DELTA" = 55;
//            "ROAM_PROF_RSSI_BOOST_THRESH" = "-68";
//            "ROAM_PROF_RSSI_LOWER" = "-128";
//        }
//    );
//    "ROAM_PROF_BAND" = 4;
//    "ROAM_PROF_NUM" = 2;
//}
//Roam: <airport[565]> DISABLED, 5GHz on awdl0 => {
//    "ROAM_PROF" =     (
//                {
//            "ROAM_PROF_BACKOFF_MULTIPLIER" = 10;
//            "ROAM_PROF_FULLSCAN_PERIOD" = 3000;
//            "ROAM_PROF_INIT_SCAN_PERIOD" = 600;
//            "ROAM_PROF_MAX_SCAN_PERIOD" = 60000;
//            "ROAM_PROF_NFSCAN" = 1;
//            "ROAM_PROF_ROAM_DELTA" = 50;
//            "ROAM_PROF_ROAM_FLAGS" = 0;
//            "ROAM_PROF_ROAM_TRIGGER" = "-120";
//            "ROAM_PROF_RSSI_BOOST_DELTA" = 0;
//            "ROAM_PROF_RSSI_BOOST_THRESH" = 0;
//            "ROAM_PROF_RSSI_LOWER" = "-128";
//        }
//    );
//    "ROAM_PROF_BAND" = 2;
//    "ROAM_PROF_NUM" = 1;
//}
//Roam: <airport[565]> SINGLE-BAND, SINGLE-AP, 5GHz on awdl0 => {
//    "ROAM_PROF" =     (
//                {
//            "ROAM_PROF_BACKOFF_MULTIPLIER" = 2;
//            "ROAM_PROF_FULLSCAN_PERIOD" = 3600;
//            "ROAM_PROF_INIT_SCAN_PERIOD" = 60;
//            "ROAM_PROF_MAX_SCAN_PERIOD" = 1200;
//            "ROAM_PROF_NFSCAN" = 1;
//            "ROAM_PROF_ROAM_DELTA" = 12;
//            "ROAM_PROF_ROAM_FLAGS" = 0;
//            "ROAM_PROF_ROAM_TRIGGER" = "-80";
//            "ROAM_PROF_RSSI_BOOST_DELTA" = 0;
//            "ROAM_PROF_RSSI_BOOST_THRESH" = 0;
//            "ROAM_PROF_RSSI_LOWER" = "-128";
//        }
//    );
//    "ROAM_PROF_BAND" = 2;
//    "ROAM_PROF_NUM" = 1;
//}
//Roam: <airport[565]> DUAL-BAND, SINGLE-AP, 5GHz on awdl0 => {
//    "ROAM_PROF" =     (
//                {
//            "ROAM_PROF_BACKOFF_MULTIPLIER" = 2;
//            "ROAM_PROF_FULLSCAN_PERIOD" = 120;
//            "ROAM_PROF_INIT_SCAN_PERIOD" = 20;
//            "ROAM_PROF_MAX_SCAN_PERIOD" = 90;
//            "ROAM_PROF_NFSCAN" = 2;
//            "ROAM_PROF_ROAM_DELTA" = 12;
//            "ROAM_PROF_ROAM_FLAGS" = 0;
//            "ROAM_PROF_ROAM_TRIGGER" = "-75";
//            "ROAM_PROF_RSSI_BOOST_DELTA" = 0;
//            "ROAM_PROF_RSSI_BOOST_THRESH" = 0;
//            "ROAM_PROF_RSSI_LOWER" = "-128";
//        }
//    );
//    "ROAM_PROF_BAND" = 2;
//    "ROAM_PROF_NUM" = 1;
//}
//Roam: <airport[565]> MULTI-AP, 5GHz on awdl0 => {
//    "ROAM_PROF" =     (
//                {
//            "ROAM_PROF_BACKOFF_MULTIPLIER" = 2;
//            "ROAM_PROF_FULLSCAN_PERIOD" = 120;
//            "ROAM_PROF_INIT_SCAN_PERIOD" = 20;
//            "ROAM_PROF_MAX_SCAN_PERIOD" = 90;
//            "ROAM_PROF_NFSCAN" = 2;
//            "ROAM_PROF_ROAM_DELTA" = 12;
//            "ROAM_PROF_ROAM_FLAGS" = 0;
//            "ROAM_PROF_ROAM_TRIGGER" = "-75";
//            "ROAM_PROF_RSSI_BOOST_DELTA" = 0;
//            "ROAM_PROF_RSSI_BOOST_THRESH" = 0;
//            "ROAM_PROF_RSSI_LOWER" = "-128";
//        }
//    );
//    "ROAM_PROF_BAND" = 2;
//    "ROAM_PROF_NUM" = 1;
//}
//Roam: <airport[565]> AC POWER, 5GHz on awdl0 => {
//    "ROAM_PROF" =     (
//                {
//            "ROAM_PROF_BACKOFF_MULTIPLIER" = 2;
//            "ROAM_PROF_FULLSCAN_PERIOD" = 120;
//            "ROAM_PROF_INIT_SCAN_PERIOD" = 20;
//            "ROAM_PROF_MAX_SCAN_PERIOD" = 90;
//            "ROAM_PROF_NFSCAN" = 2;
//            "ROAM_PROF_ROAM_DELTA" = 12;
//            "ROAM_PROF_ROAM_FLAGS" = 0;
//            "ROAM_PROF_ROAM_TRIGGER" = "-75";
//            "ROAM_PROF_RSSI_BOOST_DELTA" = 0;
//            "ROAM_PROF_RSSI_BOOST_THRESH" = 0;
//            "ROAM_PROF_RSSI_LOWER" = "-128";
//        }
//    );
//    "ROAM_PROF_BAND" = 2;
//    "ROAM_PROF_NUM" = 1;
//}

struct apple80211_roam_profile {
    int8_t      flags;
    int8_t      trigger;
    int8_t      rssi_lower;
    int8_t      rssi_boost_delta;
    int8_t      rssi_boost_thresh;
    int8_t      delta;
    uint16_t    backoff_multiplier;
    uint16_t    full_scan_period;
    uint16_t    init_scan_period;
    uint16_t    nfscan;
    uint16_t    max_scan_period;
} __attribute__((packed));

struct apple80211_roam_profile_band_data {
    uint32_t    version;
    uint32_t    flags;          // 4 (0x2, 0x4)
    uint32_t    profile_cnt;    // 8
    struct apple80211_roam_profile profiles[4];
} __attribute__((packed));

static_assert(sizeof(struct apple80211_roam_profile_band_data) == 76, "roam data size error");

struct apple80211_ie_data {
    uint32_t    version;
    uint32_t    frame_type_flags;   // 4
    uint32_t    add;                // 8
    uint32_t    signature_len;      // 12
    uint32_t    ie_len;             // 16
    uint32_t    pad1;               // 20
    uint8_t     ie[2048];
} __attribute__((packed));

struct apple80211_p2p_listen_data {
    uint32_t    version;
    uint32_t    pad1;
    uint32_t    channel;        // 8
    uint32_t    flags;          // 12
    uint32_t    duration;       // 16
} __attribute__((packed));

struct apple80211_p2p_go_conf_data {
    uint32_t    version;
    uint32_t    auth_upper;     // 4 should equal to 1
    uint32_t    auth_lower;     // 6 should non zero
    void        *dynbcn;        // 8
    uint32_t    channel;        // 12
    uint32_t    bcn_len;        // 16
    uint32_t    ssid_len;       // 20
    uint8_t     ssid[32];       // 24
    uint32_t    suppress_beacon;// 56 security:1,4
} __attribute__((packed));

struct apple80211_sta_roam_data {
    uint32_t    version;
    uint8_t     rcc_channels;
    uint8_t     unk1;
    uint8_t     taget_channel;
    uint8_t     target_bssid[APPLE80211_ADDR_LEN];
} __attribute__((packed));

struct apple80211_btc_profiles_data {
    uint32_t    version;
    uint32_t    profile_cnt;
    uint8_t     profiles[141][4];
} __attribute__((packed));

struct apple80211_btc_config_data {
    uint32_t version;
    uint32_t enable_2G;
    uint32_t profile_2g;
    uint32_t enable_5G;
    uint32_t profile_5G;
} __attribute__((packed));

struct apple80211_btc_mode_data {
    uint32_t    version;
    uint32_t    btc_mode;
} __attribute__((packed));

struct apple80211_btc_options_data {
    uint32_t    version;
    uint32_t    btc_options;
} __attribute__((packed));

struct apple80211_driver_available_data {
    uint64_t event;
    uint64_t avaliable;
    uint32_t reason;
    uint32_t sub_reason;
    // Tahoe/26.x IO80211Family validates APPLE80211_M_DRIVER_AVAILABLE payload
    // length against 0xf8 before marking the controller available.  Our older
    // local header stopped at 0xb8, which matched pre-Tahoe observations but no
    // longer matches the 26.x family-side ABI.
    char pad[224];
} __attribute__((packed));

static_assert(sizeof(struct apple80211_driver_available_data) == 0xF8, "invalid struct apple80211_driver_available_data");

struct apple80211_platform_config {
    // Tahoe WCL bring-up now calls APPLE80211_IOC_PLATFORM_CONFIG before the
    // interface is considered driver-available.  IO80211Family 26.x stores only
    // the first 7 bytes of this payload in WCLDeviceConfiguration
    // (u32 @ +0, u16 @ +4, u8 @ +6; see setPlatformConfig in the decompile),
    // so the ABI we must satisfy is a tightly packed 7-byte blob.
    //
    // The local headers previously had only a forward declaration, which left
    // AirportItlwm with no producer-side contract to implement and the live
    // path fell through to kIOReturnUnsupported (0xe00002c7) during boot.
    uint32_t flags;
    uint16_t value_4;
    uint8_t value_6;
} __attribute__((packed));

static_assert(sizeof(struct apple80211_platform_config) == 0x7, "invalid struct apple80211_platform_config");

// Recovered from public-binary single-pass byte-copy contract observed in the
// Tahoe SoftAP capability path. The producer first clears three independent
// regions of driver-private APSTA state (qword at relative offset 0x50,
// qword at relative offset 0x58, word at relative offset 0x60 — covering
// bytes 0x50..0x61), and then writes the request payload into that same
// region at relative offsets 0x50 (one byte from input +0x00), 0x51
// (one qword from input +0x01) and 0x59 (one qword from input +0x09). The
// qword writes at +0x51 and +0x59 land on unaligned addresses inside the
// APSTA region, so the local mirror must be a tightly packed 17-byte blob
// — there is no native or Apple-side alignment padding in the byte-copy
// program. The selector ID (403) was recovered from a chained-pointer
// rebase on selector 352 in the AppleBCMWLAN SoftAP capability table.
struct apple80211_softap_extended_capabilities_info {
    uint8_t  flag00;     // input +0x00 -> APSTA state +0x50 (1 byte)
    uint64_t value01;    // input +0x01 -> APSTA state +0x51 (8 bytes, unaligned)
    uint64_t value09;    // input +0x09 -> APSTA state +0x59 (8 bytes, unaligned)
} __attribute__((packed));

static_assert(__offsetof(struct apple80211_softap_extended_capabilities_info, flag00) == 0x00,
              "softap_extended_capabilities_info.flag00 offset must be 0x00");
static_assert(__offsetof(struct apple80211_softap_extended_capabilities_info, value01) == 0x01,
              "softap_extended_capabilities_info.value01 offset must be 0x01");
static_assert(__offsetof(struct apple80211_softap_extended_capabilities_info, value09) == 0x09,
              "softap_extended_capabilities_info.value09 offset must be 0x09");
static_assert(sizeof(struct apple80211_softap_extended_capabilities_info) == 17,
              "softap_extended_capabilities_info packed payload must be 17 bytes");

// Recovered from public-binary AP-up-gated maxassoc selector observed in the
// Tahoe MIS configuration path: the producer reads a single uint32 maxassoc
// value at relative offset 0x00 and forwards it to the per-radio max-assoc
// admission limit. The recovered request struct in the wire layout is 0xC
// bytes; the trailing 8 bytes are reserved/padding and not consumed by the
// AP-up branch. The selector ID (508) was recovered from a chained-pointer
// rebase on the MIS selector table adjacent to existing public selectors.
struct apple80211_mis_max_sta {
    uint32_t value00;    // requested maxassoc (clamped to [1, IEEE80211_AID_DEF])
    uint32_t reserved04; // unused by the AP-up branch
    uint32_t reserved08; // unused by the AP-up branch
} __attribute__((packed));

static_assert(__offsetof(struct apple80211_mis_max_sta, value00) == 0x00,
              "mis_max_sta.value00 offset must be 0x00");
static_assert(sizeof(struct apple80211_mis_max_sta) == 0xC,
              "mis_max_sta packed payload must be 0xC bytes");

#endif // _APPLE80211_IOCTL_H_
