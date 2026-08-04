/*
 * Copyright (C) 2020  pigworlds
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
/*    $OpenBSD: if_iwn.c,v 1.243 2020/11/12 15:16:18 krw Exp $    */

/*-
 * Copyright (c) 2007-2010 Damien Bergamini <damien.bergamini@free.fr>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Driver for Intel WiFi Link 4965 and 1000/5000/6000 Series 802.11 network
 * adapters.
 */

#ifndef ItlIwn_hpp
#define ItlIwn_hpp
#include <compat.h>
#include <linux/kernel.h>

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/systm.h>
#include <sys/endian.h>
#include <sys/kpi_mbuf.h>
#include <HAL/ItlApBlockAckRuntime.hpp>
#include <HAL/ItlApFirmwareRuntime.hpp>

#include "if_iwnreg.h"
#include "if_iwnvar.h"
#include <sys/pcireg.h>

#include <IOKit/network/IOEthernetController.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/network/IOGatedOutputQueue.h>
#include <libkern/c++/OSString.h>
#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOLib.h>
#include <libkern/OSKextLib.h>
#include <libkern/c++/OSMetaClass.h>
#include <IOKit/IOFilterInterruptEventSource.h>

#include <HAL/ItlHalService.hpp>
#include <HAL/ItlDriverInfo.hpp>
#include <HAL/ItlDriverController.hpp>

struct ieee80211_sae_ap;

enum {
    IWN_AP_PS_QUEUE_LEN = 16,
    IWN_AP_RATE_MCS_COUNT = 16,
    IWN_AP_RATE_TID_COUNT = 8,
    IWN_AP_RATE_WINDOW_SIZE = 62
};

/* Intel DVM keeps one 62-attempt success bitmap per rate.  AP clients do not
 * have a net80211 station node, so retain the same feedback ownership next to
 * the firmware station ID instead of fabricating a node solely for rate
 * control. */
struct IwnApRateWindow {
    uint64_t successHistory;
    uint8_t attempts;
    uint8_t successes;
    uint16_t successRatio;
    int32_t averageThroughput;
};

/* A compressed BA does not repeat the initial firmware rate.  DVM therefore
 * pairs it with the immediately preceding aggregate TX response for this
 * RA/TID. */
struct IwnApAggregateRateFeedback {
    bool valid;
    uint8_t mcs;
    uint8_t rflags;
    uint32_t generation;
};

struct IwnApRateControlRuntime {
    bool initialized;
    bool feedbackModeInitialized;
    bool aggregateFeedback;
    bool linkQualityPending;
    uint8_t selectedMcs;
    uint8_t missedRateCount;
    uint32_t generation;
    struct IwnApRateWindow windows[IWN_AP_RATE_MCS_COUNT];
    struct IwnApAggregateRateFeedback
        pendingAggregate[IWN_AP_RATE_TID_COUNT];
};

/*
 * DVM has twelve dynamic station IDs (2..13), while Tahoe exposes five
 * APSTA station entries.  Keep every host and firmware datum that is owned
 * by one peer in the same slot.  The apClientContext pointer below is only
 * the serialized operation context; asynchronous completions reselect a slot by
 * firmware station ID before touching it.
 */
struct IwnApClientRuntime {
    bool inUse;
    uint8_t stationId;
    bool commandPending;
    uint8_t mac[IEEE80211_ADDR_LEN];
    bool nodeInstalled;
    uint8_t materializationStage;
    bool authenticated;
    bool reassociationPending;
    uint16_t legacyRateMask;
    bool qos;
    bool ht;
    uint8_t htNss;
    uint16_t htCapabilities;
    uint8_t htAmpduParams;
    uint8_t htMcs[2];
    struct IwnApRateControlRuntime rateControl;
    uint16_t rxBaMask;
    struct ItlApRxBaRuntime rxBa[kItlApRxBaTidCount];
    uint16_t txBaMask;
    uint16_t disableTid;
    bool txBaEnablePending;
    uint8_t txBaPendingTid;
    uint8_t txBaPendingQueue;
    uint16_t txBaPendingSsn;
    uint16_t txBaPendingOldDisableTid;
    uint8_t txDialogToken;
    uint8_t txBaQueue[kItlApRxBaTidCount];
    uint16_t txSequence[kItlApRxBaTidCount];
    struct ItlApTxBaRuntime txBa[kItlApRxBaTidCount];
    bool associated;
    bool authorized;
    bool powerSave;
    uint16_t aid;
    uint8_t rsnState;
    uint8_t rsnIE[64];
    uint8_t pmk[IEEE80211_PMK_LEN];
    uint8_t anonce[EAPOL_KEY_NONCE_LEN];
    struct ieee80211_ptk ptk;
    struct ieee80211_key pairwiseSoftwareKey;
    bool softwareCcmpRxObserved;
    uint64_t replayCounter;
    uint64_t pairwiseTxPn;
    uint64_t pairwiseRxPn[16];
    size_t rsnIELength;
    struct ieee80211_sae_ap *sae;
    uint8_t saePmksaPmk[IEEE80211_PMK_LEN];
    uint8_t saePmksaPmkid[IEEE80211_PMKID_LEN];
    uint8_t saePmksaSta[IEEE80211_ADDR_LEN];
    uint8_t saePmksaBssid[IEEE80211_ADDR_LEN];
    bool saePmksaValid;
    bool openAuthenticated;
    mbuf_t psQueue[IWN_AP_PS_QUEUE_LEN];
    uint8_t psQueueHead;
    uint8_t psQueueTail;
    uint8_t psQueueCount;
    bool psQueueReady;
    bool timSet;
};

class ItlIwn : public ItlHalService, ItlDriverInfo, ItlDriverController {
    OSDeclareDefaultStructors(ItlIwn)
    
public:
    
    //kext
    void free() override;
    virtual bool attach(IOPCIDevice *device) override;
    virtual void detach(IOPCIDevice *device) override;
    IOReturn enable(IONetworkInterface *netif) override;
    IOReturn disable(IONetworkInterface *netif) override;
    virtual struct ieee80211com *get80211Controller() override;
    bool supportsAPMode() const override;
    IOReturn startAPMode(const struct ItlHalApConfig *config) override;
    IOReturn stopAPMode() override;
    IOReturn transmitAPData(mbuf_t packet) override;
    IOReturn setAPMaxStations(uint32_t maxStations) override;
    IOReturn setAPHidden(bool hidden) override;
    IOReturn triggerAPCSA(const struct ItlHalApCSA *csa) override;
    uint16_t getAPCurrentChannel() const override;
    uint32_t getAPTxFreeSpace() const;
    struct IwnApClientRuntime *iwn_find_ap_client(const uint8_t *);
    struct IwnApClientRuntime *iwn_find_ap_client_by_id(uint8_t);
    struct IwnApClientRuntime *iwn_allocate_ap_client(const uint8_t *);
    struct IwnApClientRuntime *iwn_first_ap_client(bool requireAssociated);
    void iwn_select_ap_client(struct IwnApClientRuntime *);
    void iwn_reset_ap_client(struct IwnApClientRuntime *, bool releaseSlot,
        bool preserveSaePmksa = false);
    int iwn_submit_next_ap_client_materialization();
    void iwn_reset_ap_runtime_state();
    void iwn_set_ap_scan_transition_blocked(bool);
    void iwn_set_ap_primary_tx_quiesced(bool, bool);
    bool iwn_ap_primary_tx_pending() const;
    IOReturn iwn_quiesce_scan_for_ap_transition();
    int iwn_build_ap_rxon(struct iwn_rxon *, const struct ItlHalApConfig *);
    int iwn_send_ap_pan_params(const struct ItlHalApConfig *);
    int iwn_set_ap_sta_scan_priority(bool);
    int iwn_set_ap_sta_auth_priority(bool);
    int iwn_clear_ap_sta_pan_priority();
    int iwn_send_ap_stop_pan_params();
    int iwn_add_ap_broadcast_node();
    int iwn_send_ap_broadcast_link_quality(int);
    int iwn_add_ap_client_node(const uint8_t *);
    int iwn_update_ap_client_node();
    int iwn_remove_ap_client_node(const uint8_t *);
    int iwn_wake_ap_client_node();
    int iwn_allow_ap_client_sleep_tx();
    void iwn_reset_ap_client_rate_control(struct IwnApClientRuntime *);
    bool iwn_ap_rate_feedback_matches(struct IwnApClientRuntime *,
        uint8_t, uint8_t);
    int iwn_ap_rate_control_feedback(struct IwnApClientRuntime *,
        uint8_t, uint8_t, uint16_t, uint16_t, bool, uint32_t);
    int iwn_send_ap_client_link_quality();
    int iwn_send_ap_assoc_success();
    int iwn_send_ap_sensitivity();
    int iwn_send_ap_timing(const struct ItlHalApConfig *);
    int iwn_send_ap_edca(bool accessPointValues);
    int iwn_send_ap_beacon(const struct ItlHalApConfig *);
    int iwn_send_ap_rxon_assoc();
    int iwn_send_ap_raw_frame(const void *, size_t);
    int iwn_send_ap_mgmt_frame(const void *, size_t);
    int iwn_send_ap_compressed_bar(uint8_t, uint16_t);
    int iwn_send_ap_data_frame(mbuf_t, bool moreData = false,
        bool psDelivery = false);
    int iwn_install_ap_ccmp_key(bool, uint8_t, const uint8_t *);
    int iwn_send_ap_eapol_key(const void *, size_t);
    int iwn_send_ap_4way_msg1();
    int iwn_send_ap_4way_msg3();
    void iwn_begin_ap_4way();
    bool iwn_handle_ap_eapol_key(const uint8_t *, size_t);
    bool iwn_ap_uses_sae() const;
    void iwn_reset_ap_sae();
    void iwn_clear_ap_sae_pmksa();
    bool iwn_ap_sae_pmksa_matches(
        const uint8_t *, const uint8_t *) const;
    int iwn_prepare_ap_client_reauthentication(bool preserveSaePmksa);
    int iwn_send_ap_sae_auth(const uint8_t *, uint16_t, uint16_t,
        const void *, size_t);
    bool iwn_handle_ap_sae_auth(const struct ieee80211_frame *, size_t);
    int iwn_update_ap_tim(bool);
    static void iwn_ap_csa_timeout(void *);
    int iwn_finish_ap_csa();
    void iwn_complete_ap_csa_rebind();
    void iwn_finish_ap_csa_client_restore(int);
    void iwn_clear_ap_client_for_csa(struct IwnApClientRuntime *);
    int iwn_queue_ap_ps_packet(mbuf_t, bool atFront = false);
    void iwn_purge_ap_ps_queue();
    void iwn_drain_ap_ps_queue();
    static IOReturn iwn_ap_data_tx_action(OSObject *, void *, void *,
        void *, void *);
    bool iwn_handle_ap_probe_req(const struct ieee80211_frame *, size_t);
    bool iwn_handle_ap_open_auth(const struct ieee80211_frame *, size_t);
    bool iwn_handle_ap_assoc_req(const struct ieee80211_frame *, size_t);
    int iwn_set_ap_client_rx_ba(uint8_t, uint16_t, uint16_t, bool);
    void iwn_stop_all_ap_client_rx_ba();
    int iwn_set_ap_client_tx_ba(uint8_t, uint16_t, bool);
    void iwn_stop_all_ap_client_tx_ba();
    void iwn_ap_ampdu_tx_start(int, uint8_t, uint16_t, uint8_t);
    void iwn_ap_ampdu_tx_stop(int, uint8_t, uint16_t);
    static void iwn_ap_rx_ba_deliver(void *, struct ItlApRxBaReady *);
    bool iwn_handle_ap_block_ack(const struct ieee80211_frame *, size_t,
        bool);
    bool iwn_handle_ap_disconnect(const struct ieee80211_frame *, size_t);
    void iwn_publish_ap_station_event(const uint8_t *, const uint8_t *,
        size_t, int);
    bool iwn_handle_ap_ps_poll(
        const struct ieee80211_frame_pspoll *, size_t);
    bool iwn_handle_ap_data(mbuf_t, size_t, struct mbuf_list *,
        uint32_t, uint8_t);
    void iwn_note_ap_firmware_event(int, int, int, int, int);
    void iwn_continue_ap_after_deactivation();

    /* One-ticket, real IWN management-TX path for the SAE relay. */
    IOReturn submitSaeAuthFrame(
        const struct ItlSaeAuthTxRequestV1 *request) override;
    void cancelSaeAuthFrame(uint64_t ticket) override;

    /* One bounded private CIPHER_PWD staging slot; never an Agent path. */
    IOReturn stageSaeWclCredential(
        const struct ItlSaeWclCredentialV1 *credential) override;
    void cancelSaeWclCredential(uint64_t request_generation) override;
    void purgeSaeWclCredentialStage() override;
    bool isSaeWclCredentialAdmissionReady() override;
    bool reserveSaeWclCredentialAdmission() override;
    void releaseSaeWclCredentialAdmission() override;
    bool supportsDriverResidentSae() override;

    IOReturn beginWclBackgroundScan(uint64_t generation,
                                    uint32_t *outBackendGeneration) override;
    IOReturn beginWclInitialScan(uint64_t generation,
                                 uint32_t *outBackendGeneration) override;
    IOReturn abortWclBackgroundScan(uint64_t generation) override;
    void invalidateWclBackgroundScan() override;
    IOReturn beginStandardScan(uint64_t generation, bool background,
                               uint32_t *outBackendGeneration) override;
    
    static bool intrFilter(OSObject *object, IOFilterInterruptEventSource *src);
    static IOReturn _iwn_start_task(OSObject *target, void *arg0, void *arg1, void *arg2, void *arg3);
    
    virtual ItlDriverInfo *getDriverInfo() override;
    
    virtual ItlDriverController *getDriverController() override;
    
    //driver info
    virtual const char *getFirmwareVersion() override;
    
    virtual int16_t getBSSNoise() override;
    
    virtual bool is5GBandSupport() override;
    
    virtual int getTxNSS() override;

    virtual uint8_t getTxChainMask() override;

    virtual uint8_t getRxChainMask() override;

    virtual uint32_t getLqmBeaconCount() override;
    
    virtual const char *getFirmwareName() override;
    
    virtual UInt32 supportedFeatures() override;

    virtual const char *getFirmwareCountryCode() override;
    
    virtual uint32_t getTxQueueSize() override;
    
    //driver controller
    virtual void clearScanningFlags() override;
    
    virtual IOReturn setMulticastList(IOEthernetAddress *addr, int count) override;
    
    void releaseAll();
    void joinSSID(const char *ssid, const char *pwd);
    
    //utils
    static void *mallocarray(size_t, size_t, int, int);
    
    static int        iwn_match(struct IOPCIDevice *device);
    bool       iwn_attach(struct iwn_softc *sc, struct pci_attach_args *pa);
    int        iwn4965_attach(struct iwn_softc *, pci_product_id_t);
    int        iwn5000_attach(struct iwn_softc *, pci_product_id_t);
    #if NBPFILTER > 0
    void        iwn_radiotap_attach(struct iwn_softc *);
    #endif
    int        iwn_activate(struct iwn_softc *sc, int);
    void       iwn_wakeup(struct iwn_softc *);
    static void        iwn_init_task(void *);
    int        iwn_eeprom_lock(struct iwn_softc *);
    int        iwn_init_otprom(struct iwn_softc *);
    int        iwn_read_prom_data(struct iwn_softc *, uint32_t, void *, int);
    int        iwn_dma_contig_alloc(bus_dma_tag_t, struct iwn_dma_info *,
                void **, bus_size_t, bus_size_t);
    void        iwn_dma_contig_free(struct iwn_dma_info *);
    int        iwn_alloc_sched(struct iwn_softc *);
    void        iwn_free_sched(struct iwn_softc *);
    int        iwn_alloc_kw(struct iwn_softc *);
    void        iwn_free_kw(struct iwn_softc *);
    int        iwn_alloc_ict(struct iwn_softc *);
    void        iwn_free_ict(struct iwn_softc *);
    int        iwn_alloc_fwmem(struct iwn_softc *);
    void        iwn_free_fwmem(struct iwn_softc *);
    int        iwn_alloc_rx_ring(struct iwn_softc *, struct iwn_rx_ring *);
    void        iwn_reset_rx_ring(struct iwn_softc *, struct iwn_rx_ring *);
    void        iwn_free_rx_ring(struct iwn_softc *, struct iwn_rx_ring *);
    int        iwn_alloc_tx_ring(struct iwn_softc *, struct iwn_tx_ring *,
                int);
    void        iwn_reset_tx_ring(struct iwn_softc *, struct iwn_tx_ring *);
    void        iwn_free_tx_ring(struct iwn_softc *, struct iwn_tx_ring *);
    void        iwn5000_ict_reset(struct iwn_softc *);
    int        iwn_read_eeprom(struct iwn_softc *);
    static void        iwn4965_read_eeprom(struct iwn_softc *);
    static void        iwn5000_read_eeprom(struct iwn_softc *);
    void        iwn_read_eeprom_channels(struct iwn_softc *, int, uint32_t);
    void        iwn_read_eeprom_enhinfo(struct iwn_softc *);
    static struct        ieee80211_node *iwn_node_alloc(struct ieee80211com *);
    static void        iwn_newassoc(struct ieee80211com *, struct ieee80211_node *,
                int);
    int        iwn_media_change(struct _ifnet *);
    static int        iwn_newstate(struct ieee80211com *, enum ieee80211_state, int);
    static int        iwn_newstate_preflight(struct ieee80211com *,
                    enum ieee80211_state, int);
    static void       iwn_scan_lease_replay_task(void *);
    static void        iwn_iter_func(void *, struct ieee80211_node *);
    static void        iwn_calib_timeout(void *);
    int        iwn_ccmp_decap(struct iwn_softc *, mbuf_t,
                struct ieee80211_node *);
    void        iwn_rx_phy(struct iwn_softc *, struct iwn_rx_desc *,
                struct iwn_rx_data *);
    void        iwn_rx_done(struct iwn_softc *, struct iwn_rx_desc *,
                struct iwn_rx_data *, struct mbuf_list *,
                struct mbuf_list *);
    void        iwn_ra_choose(struct iwn_softc *, struct ieee80211_node *);
    void        iwn_ampdu_rate_control(struct iwn_softc *, struct ieee80211_node *,
                struct iwn_tx_ring *, uint16_t, uint16_t);
    void        iwn_ht_single_rate_control(struct iwn_softc *,
                struct ieee80211_node *, uint8_t, uint8_t, uint8_t, int);
    void        iwn_rx_compressed_ba(struct iwn_softc *, struct iwn_rx_desc *,
                struct iwn_rx_data *);
    void        iwn5000_rx_calib_results(struct iwn_softc *,
                struct iwn_rx_desc *, struct iwn_rx_data *);
    void        iwn_rx_statistics(struct iwn_softc *, struct iwn_rx_desc *,
                struct iwn_rx_data *);
    bool        iwn_ampdu_txq_can_advance(const struct iwn_tx_ring *, int) const;
    bool        iwn_ampdu_txq_advance(struct iwn_softc *, struct iwn_tx_ring *,
                    int, int);
    void        iwn_ampdu_tx_done(struct iwn_softc *, struct iwn_tx_ring *,
                struct iwn_rx_desc *, uint16_t, uint8_t, uint8_t, uint8_t,
                int, uint32_t, struct iwn_txagg_status *);
    static void        iwn4965_tx_done(struct iwn_softc *, struct iwn_rx_desc *,
                struct iwn_rx_data *);
    static void        iwn5000_tx_done(struct iwn_softc *, struct iwn_rx_desc *,
                struct iwn_rx_data *);
    void        iwn_tx_done_free_txdata(struct iwn_softc *,
                struct iwn_tx_data *);
    void        iwn_clear_oactive(struct iwn_softc *, struct iwn_tx_ring *);
    bool        iwn_tx_pending(struct iwn_softc *);
    void        iwn_refresh_tx_timer(struct iwn_softc *);
    void        iwn_tx_done(struct iwn_softc *, struct iwn_rx_desc *,
                uint8_t, uint8_t, uint8_t, int, int, uint16_t);
    void        iwn_cmd_done(struct iwn_softc *, struct iwn_rx_desc *);
    void        iwn_notif_intr(struct iwn_softc *);
    void        iwn_wakeup_intr(struct iwn_softc *);
    void        iwn_fatal_intr(struct iwn_softc *);
    static int        iwn_intr(OSObject *object, IOInterruptEventSource* sender, int count);
    static void        iwn4965_update_sched(struct iwn_softc *, int, int, uint8_t,
                uint16_t);
    static void        iwn4965_reset_sched(struct iwn_softc *, int, int);
    static void        iwn5000_update_sched(struct iwn_softc *, int, int, uint8_t,
                uint16_t);
    static void        iwn5000_reset_sched(struct iwn_softc *, int, int);
    bool       iwn_sae_tx_commit_doorbell(struct iwn_softc *, uint64_t,
                int, int, int, uint8_t, uint16_t);
    int        iwn_tx(struct iwn_softc *, mbuf_t,
                struct ieee80211_node *,
                const struct ItlSaeAuthTxRequestV1 * = nullptr);
    int        iwn_rval2ridx(int);
    static void        iwn_start(struct _ifnet *);
    static void        iwn_watchdog(struct _ifnet *);
    static int        iwn_ioctl(struct _ifnet *, u_long, caddr_t);
    int        iwn_cmd(struct iwn_softc *, int, const void *, int, int);
    int        iwn_set_cmd_in_flight(struct iwn_softc *);
    void       iwn_clear_cmd_in_flight(struct iwn_softc *);
    static int        iwn4965_add_node(struct iwn_softc *, struct iwn_node_info *,
                int);
    static int        iwn5000_add_node(struct iwn_softc *, struct iwn_node_info *,
                int);
    int        iwn_add_bss_node(struct iwn_softc *,
                struct ieee80211_node *);
    int        iwn_set_link_quality(struct iwn_softc *,
                struct ieee80211_node *);
    int        iwn_add_broadcast_node(struct iwn_softc *, int, int);
    static void        iwn_updateedca(struct ieee80211com *);
    void        iwn_set_led(struct iwn_softc *, uint8_t, uint8_t, uint8_t);
    int        iwn_set_critical_temp(struct iwn_softc *);
    int        iwn_set_timing(struct iwn_softc *, struct ieee80211_node *);
    static void        iwn4965_power_calibration(struct iwn_softc *, int);
    static int        iwn4965_set_txpower(struct iwn_softc *, int);
    static int        iwn5000_set_txpower(struct iwn_softc *, int);
    static int        iwn4965_get_rssi(const struct iwn_rx_stat *);
    static int        iwn5000_get_rssi(const struct iwn_rx_stat *);
    int        iwn_get_noise(const struct iwn_rx_general_stats *);
    static int        iwn4965_get_temperature(struct iwn_softc *);
    static int        iwn5000_get_temperature(struct iwn_softc *);
    int        iwn_init_sensitivity(struct iwn_softc *);
    void        iwn_collect_noise(struct iwn_softc *,
                const struct iwn_rx_general_stats *);
    static int        iwn4965_init_gains(struct iwn_softc *);
    static int        iwn5000_init_gains(struct iwn_softc *);
    static int        iwn4965_set_gains(struct iwn_softc *);
    static int        iwn5000_set_gains(struct iwn_softc *);
    void        iwn_tune_sensitivity(struct iwn_softc *,
                const struct iwn_rx_stats *);
    int        iwn_send_sensitivity(struct iwn_softc *);
    int        iwn_set_pslevel(struct iwn_softc *, int, int, int);
    int        iwn_send_temperature_offset(struct iwn_softc *);
    int        iwn_send_btcoex(struct iwn_softc *);
    int        iwn_send_advanced_btcoex(struct iwn_softc *);
    int        iwn5000_runtime_calib(struct iwn_softc *);
    int        iwn_config(struct iwn_softc *);
    uint16_t    iwn_get_active_dwell_time(struct iwn_softc *, uint16_t, uint8_t);
    uint16_t    iwn_limit_dwell(struct iwn_softc *, uint16_t);
    uint16_t    iwn_get_passive_dwell_time(struct iwn_softc *, uint16_t);
    int        iwn_scan(struct iwn_softc *, uint16_t, int, u_int64_t);
    int        iwn_scan_start(struct iwn_softc *, uint16_t, int,
                enum iwn_scan_lease_owner, u_int64_t, u_int64_t,
                u_int32_t *, u_int64_t);
    int        iwn_scan_continue(struct iwn_softc *, uint16_t, int);
    int        iwn_scan_submit(struct iwn_softc *, uint16_t, int, u_int64_t,
                               bool, bool, bool, u_int64_t, u_int32_t,
                               bool *, bool *);
    int        iwn_cmd_with_doorbell_hook(
                struct iwn_softc *, int, const void *, int, int,
                bool (*)(struct iwn_softc *, void *),
                void (*)(struct iwn_softc *, void *), void *);
    void        iwn_scan_abort(struct iwn_softc *);
    static int        iwn_bgscan(struct ieee80211com *);
    static int        iwn_wnm_bgscan_abort(struct ieee80211com *);
    void       iwn_rxon_configure_ht40(struct ieee80211com *,
                                        struct ieee80211_node *);
    int        iwn_rxon_ht40_enabled(struct iwn_softc *);
    int        iwn_auth(struct iwn_softc *, int);
    int        iwn_run(struct iwn_softc *);
    static IOReturn iwn_sae_tx_gate_action(OSObject *, void *, void *,
                void *, void *);
    IOReturn   iwn_sae_tx_submit_on_gate(struct iwn_softc *,
                const struct ItlSaeAuthTxRequestV1 *);
    static void iwn_sae_tx_task(void *);
    bool       iwn_sae_tx_queue_terminal(struct iwn_softc *,
                const struct ItlSaeAuthTransportEventV1 *, uint32_t);
    void       iwn_sae_tx_report_terminal(struct iwn_softc *,
                struct iwn_tx_data *, int32_t);
    void       iwn_sae_tx_retire_unsubmitted(struct iwn_softc *, uint64_t);
    void       iwn_sae_tx_cancel_all(struct iwn_softc *);
    void       iwn_sae_tx_stop_begin(struct iwn_softc *);
    void       iwn_sae_tx_reopen(struct iwn_softc *);
    void       iwn_sae_tx_detach_begin(struct iwn_softc *);
    bool       iwn_sae_tx_snapshot_reset(struct iwn_softc *,
                struct ItlSaeAuthTransportEventV1 *);
    void       iwn_sae_tx_emit_reset_event(struct iwn_softc *,
                const struct ItlSaeAuthTransportEventV1 *);
    void       iwn_sae_tx_purge(struct iwn_softc *);
    static int iwn_sae_auth_hold(struct ieee80211com *,
                struct ieee80211_node *, enum ieee80211_state, int);
    static int iwn_sae_auth_owned(struct ieee80211com *,
                const struct ieee80211_node *);
    static int iwn_sae_engine_peer_event(struct ieee80211com *,
        const struct ItlSaeAuthPeerEventV1 *);
    static void iwn_sae_wcl_request_revoke(struct ieee80211com *,
        u_int64_t);
    static void iwn_sae_roam_port_valid(struct ieee80211com *,
        const struct ieee80211_node *);
    static int iwn_sae_wnm_roam_start(struct ieee80211com *,
        const struct ieee80211_node *);
    static int iwn_sae_wcl_roam_start(struct ieee80211com *,
        const struct ieee80211_node *,
        const u_int8_t [IEEE80211_ADDR_LEN]);
    static bool iwn_sae_bss_loss_arm(struct ieee80211com *,
        const struct ieee80211_node *);
    static int iwn_sae_bss_loss_recover(struct ieee80211com *);
    static int iwn_sae_targeted_roam_start(struct ieee80211com *,
        const struct ieee80211_node *,
        const u_int8_t [IEEE80211_ADDR_LEN], bool);
    static void iwn_sae_engine_task(void *);
    void       iwn_sae_engine_stop_begin(struct iwn_softc *);
    void       iwn_sae_engine_reopen(struct iwn_softc *);
    void       iwn_sae_engine_detach_begin(struct iwn_softc *);
    void       iwn_sae_wcl_stop_begin(struct iwn_softc *);
    void       iwn_sae_wcl_detach_begin(struct iwn_softc *);
    static void        iwn_mfp_pae_task(void *);
    static int         iwn_pae_mfp_txn_submit(struct ieee80211com *,
                u_int64_t, u_int64_t, struct ieee80211_node *,
                const struct ieee80211_key *, u_int8_t);
    static void        iwn_pae_mfp_txn_cancel(struct ieee80211com *,
                u_int64_t);
    static int         iwn_pae_mfp_txn_finish(struct ieee80211com *,
                u_int64_t);
    void        iwn_mfp_pae_abort_all(struct iwn_softc *);
    void        iwn_mfp_pae_detach_begin(struct iwn_softc *);
    void        iwn_interrupt_teardown(struct iwn_softc *);
    static int        iwn_set_key(struct ieee80211com *, struct ieee80211_node *,
                struct ieee80211_key *);
    static void        iwn_delete_key(struct ieee80211com *, struct ieee80211_node *,
                struct ieee80211_key *);
    static void        iwn_updateprot(struct ieee80211com *);
    static void        iwn_updateslot(struct ieee80211com *);
    void        iwn_update_rxon_restore_power(struct iwn_softc *);
    static void        iwn5000_update_rxon(struct iwn_softc *);
    static void        iwn4965_update_rxon(struct iwn_softc *);
    static int        iwn_ampdu_rx_start(struct ieee80211com *,
                struct ieee80211_node *, uint8_t);
    static void        iwn_ampdu_rx_stop(struct ieee80211com *,
                struct ieee80211_node *, uint8_t);
    static int        iwn_ampdu_tx_start(struct ieee80211com *,
                struct ieee80211_node *, uint8_t);
    static void        iwn_ampdu_tx_stop(struct ieee80211com *,
                struct ieee80211_node *, uint8_t);
    static void        iwn4965_ampdu_tx_start(struct iwn_softc *,
                struct ieee80211_node *, uint8_t, uint16_t);
    static void        iwn4965_ampdu_tx_stop(struct iwn_softc *,
                uint8_t, uint16_t);
    static void        iwn5000_ampdu_tx_start(struct iwn_softc *,
                struct ieee80211_node *, uint8_t, uint16_t);
    static void        iwn5000_ampdu_tx_stop(struct iwn_softc *,
                uint8_t, uint16_t);
    static void        iwn_update_chw(struct ieee80211com *);
    static int        iwn5000_query_calibration(struct iwn_softc *);
    static int        iwn5000_send_calibration(struct iwn_softc *);
    static int        iwn5000_send_wimax_coex(struct iwn_softc *);
    static int        iwn5000_crystal_calib(struct iwn_softc *);
    static int        iwn6000_temp_offset_calib(struct iwn_softc *);
    static int        iwn2000_temp_offset_calib(struct iwn_softc *);
    static int        iwn4965_post_alive(struct iwn_softc *);
    static int        iwn5000_post_alive(struct iwn_softc *);
    static int        iwn4965_load_bootcode(struct iwn_softc *, const uint8_t *,
                int);
    static int        iwn4965_load_firmware(struct iwn_softc *);
    static int        iwn5000_load_firmware_section(struct iwn_softc *, uint32_t,
                const uint8_t *, int);
    static int        iwn5000_load_firmware(struct iwn_softc *);
    int        iwn_read_firmware_leg(struct iwn_softc *,
                struct iwn_fw_info *);
    int        iwn_read_firmware_tlv(struct iwn_softc *,
                struct iwn_fw_info *, uint16_t);
    int        iwn_read_firmware(struct iwn_softc *);
    int        iwn_clock_wait(struct iwn_softc *);
    int        iwn_apm_init(struct iwn_softc *);
    void        iwn_apm_stop_master(struct iwn_softc *);
    void        iwn_apm_stop(struct iwn_softc *);
    static int        iwn4965_nic_config(struct iwn_softc *);
    static int        iwn5000_nic_config(struct iwn_softc *);
    int        iwn_hw_prepare(struct iwn_softc *);
    int        iwn_hw_init(struct iwn_softc *);
    void        iwn_hw_stop(struct iwn_softc *);
    int        iwn_init(struct _ifnet *);
    void        iwn_stop(struct _ifnet *);
    
public:
    IOInterruptEventSource* fInterrupt;
    /* Private driver workloop gate; never AirportItlwm's policy gate. */
    IOCommandGate *fSaeTxGate;
    bool apFirmwareTransitionActive;
    bool apFirmwareDeactivationReplySeen;
    bool apFirmwareDeactivationNotificationSeen;
    bool apFirmwarePostDeactivateQueued;
    bool apFirmwareUnassociatedReplySeen;
    bool apFirmwareUnassociatedNotificationSeen;
    bool apStaScanPriorityActive;
    bool apStaAuthPriorityActive;
    bool apStaBssAssociated;
    bool apPrimaryTxQuiesced;
    uint8_t apFirmwareStage;
    struct ItlHalApConfig apFirmwareConfig;
    struct iwn_rxon apFirmwareRxon;
    uint8_t apFirmwareSsid[IEEE80211_NWID_LEN];
    uint8_t apFirmwareCredential[0x40];
    uint8_t apFirmwareRsnIE[64];
    uint8_t apFirmwareBeacon[MCLBYTES];
    struct IwnApClientRuntime apClients[kItlApFirmwareMaxClients];
    struct IwnApClientRuntime *apClientContext;
    uint32_t apMaxStations;
    uint8_t apProfilePmk[IEEE80211_PMK_LEN];
    uint8_t apGtk[16];
    uint8_t apIgtk[16];
    uint64_t apGroupTxPn;
    uint8_t apGtkKid;
    uint8_t apIgtkKid;
    bool apHidden;
    CTimeout *apCsaTimeout;
    bool apCsaTimerInitialized;
    bool apCsaPending;
    uint16_t apCsaTargetChannel;
    uint8_t apCsaMode;
    uint8_t apCsaCount;
    uint8_t apCsaClientRestoreStage;
    uint8_t apCsaRestoreIndex;
    bool apCsaGroupKeyRestored;
    struct pci_attach_args pci;
    struct iwn_softc com;
};

#endif
