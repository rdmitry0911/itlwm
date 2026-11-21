/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * HAL for the v-Airport virtual Wi-Fi device.  The device's firmware runs on
 * the VM host and owns scanning, association, encryption, rate control and
 * aggregation; vap_guest.c is the transport to it.  Capabilities, radio
 * power, scans and frames go through the firmware, and so does the access
 * point, which the firmware runs whole.  The points where it is not driven
 * yet are marked "VAP-SEAM:".
 */

#ifndef ItlVirt_hpp
#define ItlVirt_hpp
#include <compat.h>
#include <linux/kernel.h>

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/kpi_mbuf.h>
#include <sys/_task.h>
#include <sys/timeout.h>

#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOFilterInterruptEventSource.h>
#include <IOKit/pci/IOPCIDevice.h>

#include <HAL/ItlHalService.hpp>
#include <HAL/ItlDriverInfo.hpp>
#include <HAL/ItlDriverController.hpp>
#include <HAL/ItlSaePeerTimer.hpp>
#include <HAL/ItlSaePmkContinuationV1.h>

#include "VapPlatformIOKit.hpp"

#define VIRT_MAX_CHANNELS       128     /* 2.4, 5 and 6 GHz */
#define VIRT_RSSI_OFFSET        100     /* net80211 RSSI = dBm + 100 */
#define VIRT_TX_MAX_SEGS        16      /* mbufs in one transmitted frame */
#define VIRT_REQUEST_MS         500     /* wait for the reply to a request */
#define VIRT_SCAN_WATCHDOG_MS   10000   /* wait for the end of a scan */
#define VIRT_SCAN_CHANNEL_MS    250     /* ... or this per channel scanned, if longer */
#define VIRT_SCAN_RETRY_MS      250     /* between attempts to start a scan on a busy firmware */
#define VIRT_STATS_MS           1000    /* between link statistics requests while associated */
#define VIRT_AP_MAX_STA         32      /* stations of the access point remembered here */
#define VIRT_AP_IE_MAX          512     /* vendor elements handed to the access point */

/* rxFrame() flags */
#define VIRT_RX_DECRYPTED   0x1     /* firmware removed IV and MIC and cleared Protected */

/* What the firmware reports about itself and the radio (HELLO, GET_CAPS). */
struct VirtCaps {
    uint8_t  macaddr[IEEE80211_ADDR_LEN];
    uint64_t fw_features;           /* VAP_FEAT_* */
    char     fw_version[32];
    char     fw_name[32];
    char     country[3];
    uint8_t  tx_chainmask, rx_chainmask, tx_nss, rx_nss;
    uint16_t nchans;
    struct {
        uint16_t freq;              /* MHz */
        int8_t   max_dbm;           /* regulatory transmit power limit */
        uint32_t flags;             /* IEEE80211_CHAN_* */
    } chans[VIRT_MAX_CHANNELS];
    struct ieee80211_rateset rates_2g, rates_5g;
    /*
     * Bodies of the 802.11 capability elements.  net80211 keeps one set for
     * all bands: the first one reported is taken.
     */
    bool     ht, vht;
    uint8_t  htcap[26];
    uint8_t  vhtcap[12];
    uint8_t  hecap[64];             /* HE: of the 5 GHz band if it has one */
    uint8_t  hecap_len;             /* 0: no HE */
};

/* The access point the firmware runs for us (vap_proto.h, "access point"). */
enum {
    VIRT_AP_OFF,
    VIRT_AP_STARTING,               /* AP_START accepted, not beaconing yet */
    VIRT_AP_ON,
};

enum {
    VIRT_SCAN_IDLE,
    VIRT_SCAN_FOREGROUND,
    VIRT_SCAN_BACKGROUND,
};

/* The one scan the firmware is running. */
struct VirtScan {
    uint8_t  state;                 /* VIRT_SCAN_* */
    bool     wcl;                   /* belongs to the WCL ticket below */
    bool     deferred;              /* the firmware was busy: not started on it yet */
    uint8_t  tries;
    uint32_t id;                    /* nonzero, unique per scan; the firmware's scan_id */
    uint64_t joinGeneration;
    uint64_t reassocSerial;
};

enum {
    VIRT_WCL_IDLE,
    VIRT_WCL_STARTING,              /* accepted, firmware scan not started yet */
    VIRT_WCL_ACTIVE,
};

/* The Airport layer's WCL scan request, at most one at a time. */
struct VirtWclTicket {
    uint8_t  phase;                 /* VIRT_WCL_* */
    bool     background;
    bool     silenced;              /* finish without publishing events */
    uint64_t generation;            /* the Airport layer's identity */
    uint32_t backend;               /* ours: the scan id */
};

/*
 * Driver-resident SAE is the glue text of hal_iwx/IwxSaeEngine.inc, which
 * ItlVirt.cpp builds for this HAL: the engine in net80211 runs the exchange,
 * the glue owns one attempt at a time.  The two structures below are that
 * text's state and keep its field names.
 */
#define VIRT_SAE_PEERQ_LEN      4

#define VIRT_FLAG_SHUTDOWN      0x01    /* detaching: the task gate stays closed */
#define VIRT_FLAG_SCANNING      0x02    /* never set: scanBusy() has the scan state */

struct ieee80211_sae_engine;

/* The attempt the glue's worker owns.  Public identity only, no key material. */
struct virt_sae_engine_owner {
    bool                              active;
    bool                              start_pending;
    bool                              cancelled;
    bool                              suppress_scan;
    bool                              completion_claimed;
    bool                              assoc_tx_pending;
    bool                              assoc_tx_accepted;
    bool                              terminal_valid;
    bool                              peer_overflow;
    bool                              submit_retry_pending;
    u_int8_t                          submit_retry_count;
    u_int64_t                         request_generation;
    u_int64_t                         association_epoch;
    u_int64_t                         relay_generation;
    u_int64_t                         join_attempt_generation;
    u_int64_t                         join_failure_generation;
    u_int64_t                         in_flight_ticket;
    u_int64_t                         terminal_peer_deadline;
    u_int64_t                         peer_reply_deadline;
    u_int64_t                         peer_reply_ticket;
    struct ItlSaeSelectedJoinEventV1  selected;
    struct ItlSaeAuthActivatedEventV1 activated;
    struct ItlSaePmkContinuationIdentityV1 completion;
    struct ItlSaeAuthTransportEventV1 terminal;
    struct ItlSaeAuthPeerEventV1      peerq[VIRT_SAE_PEERQ_LEN];
    u_int8_t                          peer_head;
    u_int8_t                          peer_tail;
    u_int8_t                          peer_count;
};

struct virt_softc {
    struct ieee80211com sc_ic;
    int sc_flags;                   /* VIRT_FLAG_* */

    struct task sae_tx_task;
    struct task sae_engine_task;

    /*
     * The glue's lifecycle lease: its worker and the credential calls hold
     * it while they run.  It closes for detach only; a radio that is off is
     * refused by the checks of each path.
     */
    IOLock *sc_task_gate_lock;
    int sc_task_gate_active;
    bool sc_task_gate_closed;
    bool sc_task_gate_detaching;
    bool sc_taskq_initialized;
    bool sc_task_callbacks_ready;
    struct taskq *sc_nswq;          /* the queue of sae_tx_task: systq */

    /*
     * The transport.  A frame is with the firmware or refused by the time
     * submitSaeAuthFrame() returns, so all it keeps is the highest ticket
     * cancelled so far.  The glue also looks at a frame in flight and at
     * queued completions: this transport has neither and they stay empty.
     */
    IOSimpleLock *sc_sae_tx_lock;
    uint64_t sc_sae_tx_cancel_through;
    bool sc_sae_tx_active;
    bool sc_sae_tx_doorbelled;
    uint64_t sc_sae_tx_active_ticket;
    struct ItlSaeAuthTransportEventV1 sc_sae_tx_active_event;
    bool sc_sae_tx_last_event_valid;
    struct ItlSaeAuthTransportEventV1 sc_sae_tx_last_event;
    uint8_t sc_sae_tx_event_count;
    uint64_t sc_sae_tx_join_failure_generation;

    IOSimpleLock *sc_sae_engine_lock;
    struct virt_sae_engine_owner sc_sae_engine_owner;
    struct ieee80211_sae_engine *sc_sae_engine;
    u_int64_t sc_sae_engine_wcl_cancel_generation;
    u_int64_t sc_sae_engine_join_failure_generation;
    volatile u_int32_t sc_sae_engine_lifecycle_generation;
    u_int64_t sc_sae_engine_next_ticket;
    u_int64_t sc_sae_engine_next_relay_generation;
    volatile u_int32_t sc_sae_engine_callback_state;
    volatile u_int32_t sc_sae_engine_task_admission_state;
    bool sc_sae_engine_task_ready;
    bool sc_sae_engine_stopping;
    bool sc_sae_engine_detaching;
    bool sc_sae_engine_runtime_enabled;

    /* The password macOS handed over for one network, until it is used up. */
    IOSimpleLock *sc_sae_wcl_credential_lock;
    bool sc_sae_wcl_credential_staged;
    bool sc_sae_wcl_credential_pending;
    bool sc_sae_wcl_credential_active;
    bool sc_sae_bss_loss_recovery_armed;
    bool sc_sae_driver_reset_recovery_pending;
    bool sc_sae_wcl_credential_cancel_valid;
    uint64_t sc_sae_wcl_credential_cancel_through_generation;
    uint64_t sc_sae_bss_loss_recovery_generation;
    struct ItlSaeWclCredentialV1 sc_sae_wcl_credential;
};

/* What the SAE glue asks of the scan state: is the radio free for a join. */
enum class VirtScanBusy : uint8_t {
    Idle,
    Busy,
};

class ItlVirt : public ItlHalService, ItlDriverInfo, ItlDriverController {
    OSDeclareDefaultStructors(ItlVirt)

public:
    static bool virt_match(IOPCIDevice *device);

    //kext
    void free() override;
    virtual bool attach(IOPCIDevice *device) override;
    virtual void detach(IOPCIDevice *device) override;
    IOReturn enable(IONetworkInterface *netif) override;
    IOReturn disable(IONetworkInterface *netif) override;
    virtual struct ieee80211com *get80211Controller() override { return &com.sc_ic; }
    virtual ItlDriverInfo *getDriverInfo() override { return this; }
    virtual ItlDriverController *getDriverController() override { return this; }

    IOReturn beginWclBackgroundScan(uint64_t generation,
                                    uint32_t *outBackendGeneration) override;
    IOReturn beginWclInitialScan(uint64_t generation,
                                 uint32_t *outBackendGeneration) override;
    IOReturn abortWclBackgroundScan(uint64_t generation) override;
    void invalidateWclBackgroundScan() override;

    //driver info
    virtual const char *getFirmwareVersion() override { return fCaps.fw_version; }
    virtual int16_t getBSSNoise() override {
        return fStatsValid && fStats.noise_dbm != 0 ? fStats.noise_dbm : -92;
    }
    virtual int16_t getChannelLoad() override {
        return fStatsValid && fStats.channel_load != 255 ?
            (int16_t)(fStats.channel_load * 100 / 254) : -1;
    }
    virtual bool is5GBandSupport() override {
        return com.sc_ic.ic_sup_rates[IEEE80211_MODE_11A].rs_nrates != 0;
    }
    virtual int getTxNSS() override { return fCaps.tx_nss; }
    virtual uint8_t getTxChainMask() override { return fCaps.tx_chainmask; }
    virtual uint8_t getRxChainMask() override { return fCaps.rx_chainmask; }
    virtual uint32_t getLqmBeaconCount() override { return fStatsValid ? fStats.beacons_rx : 0; }
    virtual const char *getFirmwareName() override { return fCaps.fw_name; }
    virtual UInt32 supportedFeatures() override { return kIONetworkFeatureMultiPages; }
    virtual const char *getFirmwareCountryCode() override { return fCaps.country; }
    virtual uint32_t getTxQueueSize() override { return VAP_G_TX_SLOTS; }
    virtual bool getTxRate(struct ItlTxRate *rate) override;
    virtual bool getMaxTxPowerHalfDbm(uint8_t *halfDbm) override;
    virtual bool publishesHeCapability() override {
        return (com.sc_ic.ic_flags & IEEE80211_F_HEON) != 0;
    }

    //driver controller
    virtual void clearScanningFlags() override;
    // VAP-SEAM: transport sends the multicast filter to the firmware.
    virtual IOReturn setMulticastList(IOEthernetAddress *addr, int count) override {
        return kIOReturnSuccess;
    }

    /*
     * Access point.  The firmware runs it whole: beacons, authentication,
     * association and the authenticator's key handshake.  This side says
     * what to run, passes 802.3 data both ways and reports the stations.
     */
    bool supportsAPMode() const override {
        return (fCaps.fw_features & VAP_FEAT_AP) != 0;
    }
    IOReturn startAPMode(const struct ItlHalApConfig *config) override;
    IOReturn stopAPMode() override;
    IOReturn transmitAPData(mbuf_t packet) override;
    /* The keys of the access point are the firmware's. */
    IOReturn setAPKey(const struct ItlHalApKey *key) override { return kIOReturnSuccess; }
    IOReturn setAPMaxStations(uint32_t maxStations) override;
    IOReturn setAPHidden(bool hidden) override;
    uint16_t getAPCurrentChannel() const override { return fApChannel; }
    /* One radio, one channel: next to a station the access point is on its channel. */
    bool requiresAPSTASharedChannel() const override { return true; }
    uint16_t getAPSTARequiredSharedChannel() const override;
    IOReturn sendAPStationCommand(const struct ItlHalApStationCommand *command) override;
    uint32_t getAPTxFreeSpace() const;

    /* AWDL: the firmware runs it (vap_proto.h); the bridges in ItlVirt.cpp call these. */
    bool awdlSupported() const;
    IOReturn awdlRequest(uint16_t type, const void *payload, uint32_t len);
    IOReturn awdlTransmit(const void *frame, uint32_t len);

    void applyCaps(const VirtCaps &caps);

    int txFrame(mbuf_t m, struct ieee80211_node *ni);
    void rxFrame(mbuf_t m, uint16_t freqMHz, int8_t rssiDbm, uint32_t flags);
    int scanCommand(uint32_t id, bool background, uint64_t wclGeneration);
    void scanAbort(uint32_t id);
    void scanEnd(bool aborted, uint32_t id);

    /*
     * Driver-resident SAE.  The glue text defines the methods below and
     * everything named iwx_, except what it calls and ItlVirt.cpp
     * implements: the frame transport (submitSaeAuthFrame,
     * cancelSaeAuthFrame) and the four services at the end.  All of it is
     * public because part of that text is not class members.
     */
    struct virt_softc com;          /* ic_softc; holds the ieee80211com */
    ItlSaePeerTimer saePeerTimer;

    IOReturn submitSaeAuthFrame(const struct ItlSaeAuthTxRequestV1 *request) override;
    void cancelSaeAuthFrame(uint64_t ticket) override;
    IOReturn stageSaeWclCredential(const struct ItlSaeWclCredentialV1 *credential) override;
    void cancelSaeWclCredential(uint64_t request_generation) override;
    void purgeSaeWclCredentialStage() override;
    bool isSaeWclCredentialAdmissionReady() override;
    bool reserveSaeWclCredentialAdmission() override;
    void releaseSaeWclCredentialAdmission() override;
    bool supportsDriverResidentSae() override;
    bool supportsSaeH2e() override { return supportsDriverResidentSae(); }

    static int iwx_sae_auth_hold(struct ieee80211com *, struct ieee80211_node *,
                                 enum ieee80211_state, int);
    static int iwx_sae_auth_owned(struct ieee80211com *, const struct ieee80211_node *);
    static int iwx_sae_engine_peer_event(struct ieee80211com *,
                                         const struct ItlSaeAuthPeerEventV1 *);
    static void iwx_sae_wcl_request_revoke(struct ieee80211com *, u_int64_t);
    static void iwx_sae_roam_port_valid(struct ieee80211com *, const struct ieee80211_node *);
    static int iwx_sae_wnm_roam_start(struct ieee80211com *, const struct ieee80211_node *,
                                      u_int64_t, u_int64_t);
    static int iwx_sae_wcl_roam_start(struct ieee80211com *, const struct ieee80211_node *,
                                      const u_int8_t [IEEE80211_ADDR_LEN]);
    static bool iwx_sae_bss_loss_arm(struct ieee80211com *, const struct ieee80211_node *);
    static int iwx_sae_bss_loss_recover(struct ieee80211com *);
    void iwx_sae_driver_reset_recovery_prepare(struct virt_softc *);
    bool iwx_sae_driver_reset_recovery_pending(struct virt_softc *, bool);
    static int iwx_sae_targeted_roam_start(struct ieee80211com *, const struct ieee80211_node *,
                                           const u_int8_t [IEEE80211_ADDR_LEN], bool, u_int64_t);
    static void iwx_sae_engine_task(void *);
    void iwx_sae_peer_timer_drain();
    static void iwx_sae_peer_timer_event(OSObject *, IOInterruptEventSource *, int);
    static void iwx_sae_peer_timer_timeout(OSObject *, IOTimerEventSource *);
    void iwx_sae_engine_stop_begin(struct virt_softc *);
    void iwx_sae_engine_reopen(struct virt_softc *);
    void iwx_sae_engine_detach_begin(struct virt_softc *);
    void iwx_sae_wcl_stop_begin(struct virt_softc *);
    void iwx_sae_wcl_detach_begin(struct virt_softc *);
    static IOReturn iwx_sae_continue_assoc_action(OSObject *, void *, void *, void *, void *);
    bool iwx_sae_continue_assoc_gated(struct ieee80211com *,
                                      const struct ItlSaePmkContinuationIdentityV1 *);

    bool iwx_task_gate_enter(struct virt_softc *, bool);
    void iwx_task_gate_leave(struct virt_softc *);
    void iwx_add_task(struct virt_softc *, struct taskq *, struct task *);
    static void iwx_wcl_join_failure_scan(struct ieee80211com *, uint64_t);

private:
    int request(uint16_t type, const void *payload, uint32_t len,
                void *rsp = NULL, uint32_t *rspLen = NULL);
    bool queryCaps(VirtCaps *caps);
    bool transportRecover();
    void syncMac();
    void setPort(bool authorized);
    int radioStart();
    void radioStop();
    int scanStart(bool background, uint64_t reassocSerial);
    void scanRetry();
    void scanReset();
    /* Caller holds fScanLock. */
    VirtScanBusy scanBusy() const {
        return fScan.state == VIRT_SCAN_IDLE && fWcl.phase == VIRT_WCL_IDLE ?
            VirtScanBusy::Idle : VirtScanBusy::Busy;
    }
    int keyAdd(struct ieee80211_node *ni, const struct ieee80211_key *k);
    void keyDel(const struct ieee80211_key *k);
    int apStartCommand();
    void apReset(bool tellStations);
    void apStarted(const struct vap_ap_started *st);
    void apStation(const uint8_t *sta, bool connected, const uint8_t *ies, uint16_t ieLen);
    void apInput(const uint8_t *frame, uint32_t len);
    void apTxKick();
    static IOReturn virt_ap_tx_gated(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3);
    static IOReturn virt_awdl_tx_gated(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3);
    bool saeAttach();
    void saeDetach();
    bool saeAdmitAssocReq(mbuf_t m, struct ieee80211_node *ni);

    static IOReturn virt_request_gated(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3);
    static IOReturn virt_radio_gated(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3);
    static bool virt_intr_filter(OSObject *owner, IOFilterInterruptEventSource *sender);
    static void virt_intr(OSObject *owner, IOInterruptEventSource *sender, int count);
    static void virt_event(void *ctx, const struct vap_hdr *h, const void *payload, uint32_t len);
    static void virt_rx(void *ctx, const struct vap_rx_meta *meta, const uint8_t *frame, uint32_t len);
    static void virt_tx_ready(void *ctx);
    static void virt_init_task(void *arg);
    static void virt_scan_watchdog(void *arg);
    static void virt_stats_timer(void *arg);
    static IOReturn virt_start_task(OSObject *target, void *arg0, void *arg1, void *arg2, void *arg3);
    static void virt_start(struct _ifnet *ifp);
    static int virt_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg);
    static int virt_bgscan(struct ieee80211com *ic, uint64_t reassocSerial);
    static int virt_bgscan_abort(struct ieee80211com *ic, uint64_t reassocSerial);
    static int virt_set_key(struct ieee80211com *ic, struct ieee80211_node *ni,
                            struct ieee80211_key *k);
    static void virt_delete_key(struct ieee80211com *ic, struct ieee80211_node *ni,
                                struct ieee80211_key *k);
    static int virt_pae_txn_submit(struct ieee80211com *ic, u_int64_t id, u_int64_t epoch,
                                   struct ieee80211_node *ni, const struct ieee80211_key *k,
                                   u_int8_t stage);
    static void virt_pae_txn_cancel(struct ieee80211com *ic, u_int64_t id);
    static int virt_pae_txn_finish(struct ieee80211com *ic, u_int64_t id);
    static int virt_ampdu_start(struct ieee80211com *ic, struct ieee80211_node *ni,
                                uint8_t tid);
    static void virt_ampdu_stop(struct ieee80211com *ic, struct ieee80211_node *ni,
                                uint8_t tid);
    static IOReturn virt_newstate_gated(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3);
    static IOReturn virt_sae_tx_gated(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3);
    static IOReturn virt_join_failure_gated(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3);
    static void virt_sae_tx_task(void *arg);

private:
    int (*fNewstate)(struct ieee80211com *, enum ieee80211_state, int);
    VirtCaps fCaps;

    /*
     * The transport is not thread-safe.  Every call into it is made with the
     * driver workloop gate held: from the interrupt action, from request()
     * and from the transmit path.
     */
    VapPlatformIOKit fPlatform;
    bool fMapped;                   /* BAR0 is mapped: the device can be reached */
    struct vap_dev *fDev;
    uint8_t fCtrlLost;              /* request slots given up on, by bit */
    bool fResetLogged;
    uint64_t fResetCheckAt;
    mbuf_t fTxHeld;                 /* the frame the device had no room for */
    struct ieee80211_node *fTxHeldNi;
    uint8_t fFwMac[IEEE80211_ADDR_LEN];     /* the address the firmware is using */
    uint8_t fKeys;                  /* keys the firmware holds: see virt_key_bit() */
    uint64_t fPaeTxn;               /* key transaction with a stage to report done, or 0 */
    uint8_t fPaeStage;
    bool fPortAuthorized;

    IOFilterInterruptEventSource *fIntr;
    bool fIfAttached;
    bool fTaskqReady;

    struct task fInitTask;
    CTimeout *fStatsTimer;
    struct vap_stats fStats;        /* the last answer to GET_STATS; valid while associated */
    bool fStatsValid;
    CTimeout *fScanWatchdog;
    IOSimpleLock *fScanLock;        /* guards fScan, fWcl, fScanId */
    VirtScan fScan;
    VirtWclTicket fWcl;
    uint32_t fScanId;
    bool wclSaeAdmissionReserved;   /* guarded by fScanLock; see reserveSaeWclCredentialAdmission() */

    /* The access point; touched with the workloop gate held. */
    uint8_t fApState;               /* VIRT_AP_* */
    uint16_t fApChannel;            /* accepted or running; 0 without an access point */
    bool fApHidden;
    uint8_t fApMaxSta;              /* 0: as the start request says */
    uint8_t fApBssid[IEEE80211_ADDR_LEN];
    uint8_t fApSta[VIRT_AP_MAX_STA][IEEE80211_ADDR_LEN];   /* all zero: free */
    /* The last AP_START, to run it again with another setting.  Holds the passphrase. */
    uint32_t fApStartLen;
    uint8_t fApStart[sizeof(struct vap_ap_start) + VIRT_AP_IE_MAX];
};

#endif /* ItlVirt_hpp */
