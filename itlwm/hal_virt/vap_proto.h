/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * v-Airport guest <-> firmware protocol, payload layer (draft v0).
 *
 * Single source of truth for both sides: the guest HAL (kext, C++) and the
 * qdev-host plugin (C). Registers and queues are in vap_regs.h; this file
 * describes what travels in the queues.
 *
 * All multi-byte fields are little-endian. Structures are naturally aligned
 * with explicit padding; nothing here is packed. Every message is padded to
 * a multiple of 8 bytes.
 */
#ifndef VAP_PROTO_H
#define VAP_PROTO_H

#include <stdint.h>

#define VAP_PROTO_VERSION 0u

/* ---- message header ---------------------------------------------------- */

/*
 * Control and event queues carry a vap_hdr followed by the payload. The frame
 * queues carry no header: a buffer starts with vap_tx_meta or vap_rx_meta,
 * followed by the frame.
 */
#define VAP_KIND_REQ   1u   /* guest -> firmware, answered by one RSP with the same seq */
#define VAP_KIND_RSP   2u   /* firmware -> guest */
#define VAP_KIND_EVT   3u   /* firmware -> guest, unsolicited, seq == 0 */

struct vap_hdr {
    uint16_t type;      /* VAP_CMD_* or VAP_EVT_* */
    uint8_t  kind;      /* VAP_KIND_* */
    uint8_t  rsvd;
    uint32_t len;       /* payload bytes after this header, before padding */
    uint32_t seq;
    int32_t  status;    /* RSP only: 0 or negative VAP_E_* */
};

#define VAP_E_UNSUPPORTED  (-1)
#define VAP_E_INVAL        (-2)
#define VAP_E_BUSY         (-3)
#define VAP_E_NOT_READY    (-4)   /* radio off, or no BSS selected */
#define VAP_E_HOST         (-5)   /* host driver refused; detail in the log */
#define VAP_E_NO_BSS       (-6)   /* target BSS unknown to the host and not found by a rescan */

/* ---- feature bits (HELLO, both directions) ------------------------------ */

#define VAP_FEAT_BAND_5G      (1ull << 0)
#define VAP_FEAT_BAND_6G      (1ull << 1)
#define VAP_FEAT_AP           (1ull << 2)   /* host can run an AP interface next to the station */
#define VAP_FEAT_MONITOR      (1ull << 3)
#define VAP_FEAT_LIVE_MAC     (1ull << 4)   /* MAC address may change while the radio is on */
#define VAP_FEAT_SAE_FRAMES   (1ull << 5)   /* guest-run SAE: commit/confirm relayed as auth frames */
#define VAP_FEAT_SME_OFFLOAD  (1ull << 6)   /* firmware runs SAE / 4-way itself from a credential */
#define VAP_FEAT_PMF          (1ull << 7)
#define VAP_FEAT_CQM          (1ull << 8)
#define VAP_FEAT_POWERSAVE    (1ull << 9)
#define VAP_FEAT_DATA_8023    (1ull << 10)  /* reserved: 802.3 data frames instead of 802.11 */
#define VAP_FEAT_AWDL         (1ull << 11)  /* the firmware runs AWDL beside the station (VAP_CMD_AWDL_*) */

/* ---- commands ----------------------------------------------------------- */

enum {
    VAP_CMD_HELLO        = 0x01,
    VAP_CMD_GET_CAPS     = 0x02,
    VAP_CMD_RADIO        = 0x03,
    VAP_CMD_SET_MAC      = 0x04,
    VAP_CMD_SCAN_START   = 0x10,
    VAP_CMD_SCAN_ABORT   = 0x11,
    VAP_CMD_SET_BSS      = 0x20,
    VAP_CMD_CLEAR_BSS    = 0x21,
    VAP_CMD_PORT         = 0x22,
    VAP_CMD_KEY_ADD      = 0x30,
    VAP_CMD_KEY_DEL      = 0x31,
    VAP_CMD_GET_STATS    = 0x40,
    VAP_CMD_SET_CQM      = 0x41,
    VAP_CMD_SET_POWERSAVE = 0x42,
    VAP_CMD_AP_START     = 0x50,
    VAP_CMD_AP_STOP      = 0x51,
    VAP_CMD_AP_STA_DEAUTH = 0x52,
    VAP_CMD_AP_STA_INFO  = 0x53,
    VAP_CMD_AWDL_CONFIG  = 0x60,
    VAP_CMD_AWDL_TEMPLATE = 0x61,
};

enum {
    VAP_EVT_FW_ERROR       = 0x80,
    VAP_EVT_RFKILL         = 0x81,
    VAP_EVT_SCAN_DONE      = 0x90,
    VAP_EVT_MLME_TIMEOUT   = 0xa0,
    VAP_EVT_DISCONNECTED   = 0xa1,
    VAP_EVT_BEACON_LOSS    = 0xa2,
    VAP_EVT_CQM            = 0xa3,
    VAP_EVT_CHANNEL_SWITCH = 0xa4,
    VAP_EVT_TX_STATUS      = 0xb0,
    VAP_EVT_REGDOM         = 0xc0,
    VAP_EVT_AP_STA         = 0xc8,
    VAP_EVT_AP_STOPPED     = 0xc9,
    VAP_EVT_AP_STARTED     = 0xca,
    VAP_EVT_AWDL_STATE     = 0xd0,
    VAP_EVT_AWDL_PEER      = 0xd1,
};

struct vap_hello_req {
    uint32_t proto_version;
    uint32_t rsvd;
    uint64_t guest_features;
};

struct vap_hello_rsp {
    uint32_t proto_version;
    uint32_t max_frame;         /* largest frame the firmware accepts or delivers */
    uint64_t fw_features;
    uint8_t  mac[6];            /* permanent address of the host interface */
    uint8_t  rsvd[2];
    char     fw_version[32];    /* NUL-terminated */
};

/*
 * GET_CAPS response: a sequence of TLVs. Radio capabilities are carried as
 * the bodies of the corresponding 802.11 elements exactly as the host driver
 * reports them, so a new Wi-Fi generation adds a TLV type and nothing else.
 */
struct vap_tlv {
    uint16_t type;
    uint16_t len;               /* value bytes; the next TLV starts at the next multiple of 4 */
};

enum {
    VAP_BAND_2G = 0, VAP_BAND_5G = 1, VAP_BAND_6G = 2,
};

enum {
    VAP_TLV_CHANNELS = 1,       /* u8 band, u8 rsvd[3], struct vap_chan[] */
    VAP_TLV_RATES    = 2,       /* u8 band, u8 rsvd[3], u8 rates[] in 500 kbit/s units */
    VAP_TLV_HT_CAP   = 3,       /* u8 band, u8 rsvd[3], HT Capabilities element body */
    VAP_TLV_VHT_CAP  = 4,       /* u8 band, u8 rsvd[3], VHT Capabilities element body */
    VAP_TLV_HE_CAP   = 5,       /* u8 band, u8 rsvd[3], HE Capabilities element body (station) */
    VAP_TLV_EHT_CAP  = 6,       /* u8 band, u8 rsvd[3], EHT Capabilities element body (station) */
    VAP_TLV_CIPHERS  = 7,       /* u32 cipher suite selectors */
    VAP_TLV_AKMS     = 8,       /* u32 AKM suite selectors */
    VAP_TLV_REGDOM   = 9,       /* char alpha2[2], u8 dfs_region, u8 rsvd */
    VAP_TLV_ANTENNAS = 10,      /* u8 tx_chainmask, rx_chainmask, tx_nss, rx_nss */
    VAP_TLV_FW_NAME  = 11,      /* host driver and device, NUL-terminated */
};

#define VAP_CHAN_DISABLED   (1u << 0)
#define VAP_CHAN_NO_IR      (1u << 1)   /* passive only */
#define VAP_CHAN_RADAR      (1u << 2)
#define VAP_CHAN_NO_HT40P   (1u << 3)
#define VAP_CHAN_NO_HT40M   (1u << 4)
#define VAP_CHAN_NO_80      (1u << 5)
#define VAP_CHAN_NO_160     (1u << 6)

struct vap_chan {
    uint16_t freq_mhz;
    uint16_t flags;             /* VAP_CHAN_* */
    int8_t   max_dbm;
    uint8_t  rsvd[3];
};

struct vap_radio { uint32_t on; uint32_t rsvd; };

struct vap_set_mac { uint8_t mac[6]; uint8_t rsvd[2]; };

#define VAP_SCAN_PASSIVE    (1u << 0)
#define VAP_SCAN_BACKGROUND (1u << 1)   /* associated: keep the link, low priority */
#define VAP_SCAN_FLUSH      (1u << 2)   /* drop cached results first */

/* Followed by n_ssids * struct vap_ssid, n_freqs * u16 (0 freqs = all), then ie_len bytes. */
struct vap_scan_start {
    uint32_t scan_id;           /* echoed in SCAN_DONE and in every result frame */
    uint32_t flags;
    uint16_t n_ssids;
    uint16_t n_freqs;
    uint16_t ie_len;            /* extra elements for probe requests */
    uint16_t dwell_ms;          /* hint; 0 = host default */
};

struct vap_ssid { uint8_t len; uint8_t ssid[32]; uint8_t rsvd[3]; };

struct vap_scan_abort { uint32_t scan_id; uint32_t rsvd; };

struct vap_evt_scan_done { uint32_t scan_id; uint32_t aborted; uint32_t n_results; uint32_t rsvd; };

/*
 * SET_BSS binds the firmware to the BSS the guest is about to authenticate
 * with. It carries what a management frame alone does not: the channel and
 * the SSID of a hidden network. Followed by ie_len bytes: the RSN / RSNX
 * elements the guest will put in its association request.
 */
struct vap_set_bss {
    uint8_t  bssid[6];
    uint16_t freq_mhz;
    struct vap_ssid ssid;
    uint16_t beacon_int;
    uint16_t capinfo;
    uint32_t flags;             /* VAP_BSS_* */
    uint16_t ie_len;
    uint16_t rsvd;
};

#define VAP_BSS_PRIVACY       (1u << 0)
#define VAP_BSS_MFP_CAPABLE   (1u << 1)
#define VAP_BSS_MFP_REQUIRED  (1u << 2)
#define VAP_BSS_REASSOC       (1u << 3)   /* roaming inside the same ESS */

struct vap_port { uint32_t authorized; uint32_t rsvd; };

#define VAP_KEY_PAIRWISE  (1u << 0)
#define VAP_KEY_GROUP     (1u << 1)
#define VAP_KEY_IGTK      (1u << 2)
#define VAP_KEY_BIGTK     (1u << 3)
#define VAP_KEY_TX        (1u << 4)       /* default transmit key */

struct vap_key {
    uint32_t cipher;            /* 802.11 cipher suite selector */
    uint16_t flags;             /* VAP_KEY_* */
    uint8_t  idx;
    uint8_t  len;
    uint8_t  peer[6];           /* pairwise: the AP; group keys: zero */
    uint8_t  rsvd[2];
    uint8_t  rsc[8];            /* receive sequence counter, little-endian */
    uint8_t  key[32];
};

#define VAP_RATE_LEGACY 0
#define VAP_RATE_HT     1
#define VAP_RATE_VHT    2
#define VAP_RATE_HE     3
#define VAP_RATE_EHT    4

struct vap_rate {
    uint32_t kbps;
    uint8_t  mode;              /* VAP_RATE_* */
    uint8_t  mcs;
    uint8_t  nss;
    uint8_t  bw_mhz_log2;       /* 20 MHz << n: 0 = 20, 1 = 40, 2 = 80, 3 = 160, 4 = 320 */
    uint8_t  gi;                /* 0 = long/0.8 us, 1 = short/0.4, 2 = 1.6, 3 = 3.2 */
    uint8_t  rsvd[3];
};

struct vap_stats {
    int8_t   rssi_dbm;
    int8_t   rssi_avg_dbm;
    int8_t   noise_dbm;
    uint8_t  channel_load;      /* 0..255, busy share of the operating channel; 255 = unknown */
    uint16_t freq_mhz;
    uint16_t center_freq1_mhz;
    struct vap_rate tx_rate;
    struct vap_rate rx_rate;
    uint32_t beacons_rx;
    uint32_t beacons_lost;
    uint64_t tx_packets, tx_bytes, tx_retries, tx_failed;
    uint64_t rx_packets, rx_bytes;
};

struct vap_set_cqm { int32_t rssi_thold_dbm; uint32_t hysteresis_db; };

struct vap_evt_rfkill { uint32_t hard; uint32_t soft; };

#define VAP_MLME_AUTH  1u
#define VAP_MLME_ASSOC 2u
struct vap_evt_mlme_timeout { uint32_t what; uint8_t bssid[6]; uint8_t rsvd[2]; };

struct vap_evt_disconnected { uint16_t reason; uint8_t by_ap; uint8_t rsvd; uint8_t bssid[6]; uint8_t rsvd2[2]; };

struct vap_evt_cqm { int32_t rssi_dbm; uint32_t below; };

struct vap_evt_channel_switch { uint16_t freq_mhz; uint16_t center_freq1_mhz; uint16_t bw_mhz; uint16_t rsvd; };

struct vap_evt_tx_status { uint32_t cookie; uint32_t acked; };

/* ---- access point ------------------------------------------------------- */

/*
 * The firmware runs the access point itself: beacons, probe responses,
 * authentication, association and the authenticator side of the 4-way
 * handshake. The guest says what to run and gets the stations' comings and
 * goings; data for the AP crosses as 802.3 frames with vif VAP_VIF_AP.
 *
 * AP_START answers at once with the BSSID and channel it will use (status 0:
 * accepted); VAP_EVT_AP_STARTED follows when the AP beacons, or
 * VAP_EVT_AP_STOPPED if it could not start or stops later.
 */
#define VAP_AP_AKM_OPEN 0x0u
#define VAP_AP_AKM_PSK  (1u << 0)
#define VAP_AP_AKM_SAE  (1u << 1)

#define VAP_AP_HIDDEN   (1u << 0)

/* Followed by ie_len bytes: elements to add to beacons and probe responses. */
struct vap_ap_start {
    uint8_t  bssid[6];          /* all zero: the firmware picks one */
    uint16_t freq_mhz;          /* 0: the station's channel, or the firmware's choice */
    uint16_t beacon_int;
    uint8_t  dtim;
    uint8_t  max_sta;           /* 0: the firmware's limit */
    uint32_t akm;               /* VAP_AP_AKM_*; PSK|SAE is WPA3 transition */
    uint32_t pairwise_cipher;   /* suite selector, e.g. CCMP 0x000fac04 */
    uint32_t group_cipher;
    uint8_t  mfp;               /* 0 off, 1 optional, 2 required */
    uint8_t  flags;             /* VAP_AP_* */
    uint8_t  ssid_len;
    uint8_t  cred_len;          /* passphrase 8..63, or 64 hexadecimal digits of the PSK */
    uint8_t  ssid[32];
    uint8_t  cred[64];
    uint16_t ie_len;
    uint16_t rsvd;
};

/* AP_START reply and VAP_EVT_AP_STARTED payload. */
struct vap_ap_started { uint8_t bssid[6]; uint16_t freq_mhz; };

struct vap_ap_sta_deauth { uint8_t sta[6]; uint16_t reason; };

struct vap_ap_sta_query { uint8_t sta[6]; uint8_t rsvd[2]; };

struct vap_ap_sta_info {
    uint8_t  sta[6];
    uint16_t aid;
    int8_t   rssi_dbm;
    uint8_t  rsvd[3];
    uint32_t connected_s;
    uint64_t tx_packets, rx_packets, tx_bytes, rx_bytes;
    struct vap_rate tx_rate;
    struct vap_rate rx_rate;
};

/* Followed by ie_len bytes: the station's association request elements, when known. */
struct vap_evt_ap_sta {
    uint8_t  sta[6];
    uint8_t  connected;
    uint8_t  rsvd;
    uint16_t reason;            /* when it left */
    uint16_t ie_len;
};

struct vap_evt_ap_stopped { int32_t reason; uint32_t rsvd; };

/* ---- AWDL --------------------------------------------------------------- */

/*
 * The firmware runs AWDL beside the station, as Apple's own firmware does: it
 * holds a channel context on the AWDL channel, keeps the availability windows
 * and the channel schedule, synchronises to the elected master and takes part
 * in the election, sends the periodic sync frames (PSF) and the master
 * indication frames (MI) from the guest's templates, and carries data. The
 * guest's AWDL stack decides the rest: which peers and services, what
 * schedule it wants, what goes in the frames.
 *
 * AWDL_CONFIG always carries the whole configuration; enabled == 0 stops AWDL
 * and frees its channel context. AWDL_TEMPLATE replaces one template: the
 * guest's TLVs, to which the firmware adds those it owns (sync parameters,
 * election parameters v1 and v2, channel sequence; any of these in the
 * template are replaced).
 *
 * Frames with vif VAP_VIF_AWDL: from the firmware, the peers' AWDL action
 * frames as 802.11 frames without FCS (VAP_RXF_AWDL_ACTION, with the RSSI and
 * frequency they were received at) and data as 802.3 frames; to the firmware,
 * data as 802.3 frames (AWDL carries IPv6 only).
 */
#define VAP_AWDL_SEQ_LEN 16

struct vap_awdl_config {
    uint8_t  addr[6];               /* the guest's AWDL interface address */
    uint8_t  enabled;               /* the stack's "sync enabled" */
    uint8_t  presence_mode;         /* availability windows per extended window, usually 4 */
    uint8_t  master_channel;        /* social channel: 6, 44 or 149 */
    uint8_t  secondary_master_channel;
    uint8_t  seq_step;              /* availability windows per slot of seq[], minus one */
    uint8_t  rsvd;
    uint16_t aw_period_tu;          /* length of an availability window, usually 16 */
    uint16_t aw_length_tu;
    uint16_t ext_length_tu;
    uint16_t sync_frame_period_tu;  /* PSF period, usually 110 */
    uint32_t election_metric;
    uint32_t election_id;
    uint8_t  seq[VAP_AWDL_SEQ_LEN]; /* channel of each slot, 0: away (on the station's channel) */
    uint8_t  bssid[6];              /* 00:25:00:ff:94:73 */
    uint8_t  rsvd2[2];
};

#define VAP_AWDL_TMPL_SYNC  0u      /* TLVs of the periodic sync frame */
#define VAP_AWDL_TMPL_MI    1u      /* TLVs of the master indication frame (the stack's OOB auto request) */
#define VAP_AWDL_TMPL_ONCE  2u      /* one action frame now, to dst (the stack's OOB request) */

/* Followed by len bytes of TLVs. */
struct vap_awdl_template {
    uint16_t kind;                  /* VAP_AWDL_TMPL_* */
    uint16_t len;
    uint8_t  dst[6];                /* ONCE; ff:ff:ff:ff:ff:ff is every peer */
    uint8_t  rsvd[2];
};

#define VAP_AWDL_STATE_OFF    0u
#define VAP_AWDL_STATE_MASTER 1u    /* this card is the master */
#define VAP_AWDL_STATE_SLAVE  2u    /* synchronised to another master */

/* Sent when AWDL starts or stops and when the election tree changes. */
struct vap_evt_awdl_state {
    uint8_t  state;                 /* VAP_AWDL_STATE_* */
    uint8_t  channel;               /* the channel the firmware's AWDL context is on */
    uint16_t n_peers;
    uint8_t  master[6];
    uint8_t  sync[6];               /* the peer we synchronise to; our own address as master */
    uint32_t master_metric;
    uint32_t height;                /* hops to the master */
};

/* A peer became valid (its master indication was received) or timed out. */
struct vap_evt_awdl_peer {
    uint8_t  addr[6];
    uint8_t  present;
    int8_t   rssi_dbm;
    char     name[32];              /* from the peer's ARPA TLV, NUL-terminated, may be empty */
};

/* ---- frames ------------------------------------------------------------- */

/*
 * Station frames (vif VAP_VIF_STA) cross the boundary as 802.11 frames
 * without FCS, management and data alike, in the clear: the firmware
 * encrypts and decrypts with the keys from KEY_ADD. The firmware owns rate
 * control, aggregation, retries and power save. Access point frames (vif
 * VAP_VIF_AP) are 802.3 data frames; AWDL frames (vif VAP_VIF_AWDL) are
 * described in the AWDL section above.
 */
#define VAP_VIF_STA  0u
#define VAP_VIF_AP   1u
#define VAP_VIF_AWDL 2u

#define VAP_TXF_NO_ENCRYPT  (1u << 0)   /* EAPOL before keys are installed */
#define VAP_TXF_WANT_STATUS (1u << 1)   /* report with VAP_EVT_TX_STATUS(cookie) */

struct vap_tx_meta {
    uint32_t flags;
    uint32_t cookie;
    uint16_t frame_len;
    uint8_t  tid;
    uint8_t  vif;               /* VAP_VIF_* */
    uint8_t  rsvd[4];
};

#define VAP_RXF_SCAN_RESULT (1u << 0)   /* beacon or probe response reported by a scan */
#define VAP_RXF_BEACON_DIGEST (1u << 1) /* periodic copy of the serving AP's beacon */
#define VAP_RXF_SYNTHESIZED (1u << 2)   /* built by the firmware, was not on the air in this form */
#define VAP_RXF_DECRYPTED   (1u << 3)   /* was protected on the air: delivered in the clear,
                                           Protected bit cleared, IV and MIC removed */
#define VAP_RXF_AWDL_ACTION (1u << 4)   /* vif AWDL: a peer's AWDL action frame (otherwise 802.3 data) */

struct vap_rx_meta {
    uint32_t flags;
    uint32_t scan_id;           /* for VAP_RXF_SCAN_RESULT */
    uint64_t tsf_us;
    uint16_t frame_len;
    uint16_t freq_mhz;
    int8_t   rssi_dbm;
    int8_t   noise_dbm;
    uint8_t  vif;               /* VAP_VIF_* */
    uint8_t  rsvd;
    struct vap_rate rate;
};

#endif /* VAP_PROTO_H */
