/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * HAL for the v-Airport virtual Wi-Fi device.  The device's firmware runs on
 * the VM host and owns scanning, association, encryption, rate control and
 * aggregation; vap_guest.c is the transport to it.  Capabilities, radio
 * power, scans and frames go through the firmware.  The points where it is
 * not driven yet are marked "VAP-SEAM:".
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

#include "VapPlatformIOKit.hpp"

#define VIRT_MAX_CHANNELS       64
#define VIRT_RSSI_OFFSET        100     /* net80211 RSSI = dBm + 100 */
#define VIRT_TX_MAX_SEGS        16      /* mbufs in one transmitted frame */
#define VIRT_REQUEST_MS         500     /* wait for the reply to a request */
#define VIRT_SCAN_WATCHDOG_MS   10000   /* wait for the end of a scan */

/* rxFrame() flags */
#define VIRT_RX_DECRYPTED   0x1     /* firmware removed IV and MIC and cleared Protected */

/* What the firmware reports about itself and the radio (HELLO, GET_CAPS). */
struct VirtCaps {
    uint8_t  macaddr[IEEE80211_ADDR_LEN];
    char     fw_version[32];
    char     fw_name[32];
    char     country[3];
    uint8_t  tx_chainmask, rx_chainmask, tx_nss, rx_nss;
    uint16_t nchans;
    struct {
        uint16_t freq;              /* MHz */
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
    virtual struct ieee80211com *get80211Controller() override { return &fIc; }
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
    virtual int16_t getBSSNoise() override { return -92; }
    virtual bool is5GBandSupport() override {
        return fIc.ic_sup_rates[IEEE80211_MODE_11A].rs_nrates != 0;
    }
    virtual int getTxNSS() override { return fCaps.tx_nss; }
    virtual uint8_t getTxChainMask() override { return fCaps.tx_chainmask; }
    virtual uint8_t getRxChainMask() override { return fCaps.rx_chainmask; }
    virtual uint32_t getLqmBeaconCount() override { return 0; }
    virtual const char *getFirmwareName() override { return fCaps.fw_name; }
    virtual UInt32 supportedFeatures() override { return kIONetworkFeatureMultiPages; }
    virtual const char *getFirmwareCountryCode() override { return fCaps.country; }
    virtual uint32_t getTxQueueSize() override { return VAP_G_TX_SLOTS; }

    //driver controller
    virtual void clearScanningFlags() override;
    // VAP-SEAM: transport sends the multicast filter to the firmware.
    virtual IOReturn setMulticastList(IOEthernetAddress *addr, int count) override {
        return kIOReturnSuccess;
    }

    void applyCaps(const VirtCaps &caps);

    int txFrame(mbuf_t m, struct ieee80211_node *ni);
    void rxFrame(mbuf_t m, uint16_t freqMHz, int8_t rssiDbm, uint32_t flags);
    int scanCommand(uint32_t id, bool background, uint64_t wclGeneration);
    void scanAbort(uint32_t id);
    void scanEnd(bool aborted, uint32_t id);

private:
    int request(uint16_t type, const void *payload, uint32_t len,
                void *rsp = NULL, uint32_t *rspLen = NULL);
    bool queryCaps(VirtCaps *caps);
    int radioStart();
    void radioStop();
    int scanStart(bool background, uint64_t reassocSerial);
    void scanReset();

    static IOReturn virt_request_gated(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3);
    static IOReturn virt_radio_gated(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3);
    static bool virt_intr_filter(OSObject *owner, IOFilterInterruptEventSource *sender);
    static void virt_intr(OSObject *owner, IOInterruptEventSource *sender, int count);
    static void virt_event(void *ctx, const struct vap_hdr *h, const void *payload, uint32_t len);
    static void virt_rx(void *ctx, const struct vap_rx_meta *meta, const uint8_t *frame, uint32_t len);
    static void virt_tx_ready(void *ctx);
    static void virt_init_task(void *arg);
    static void virt_scan_watchdog(void *arg);
    static IOReturn virt_start_task(OSObject *target, void *arg0, void *arg1, void *arg2, void *arg3);
    static void virt_start(struct _ifnet *ifp);
    static int virt_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg);
    static int virt_bgscan(struct ieee80211com *ic, uint64_t reassocSerial);
    static int virt_bgscan_abort(struct ieee80211com *ic, uint64_t reassocSerial);
    static int virt_set_key(struct ieee80211com *ic, struct ieee80211_node *ni,
                            struct ieee80211_key *k);
    static void virt_delete_key(struct ieee80211com *ic, struct ieee80211_node *ni,
                                struct ieee80211_key *k);
    static int virt_ampdu_start(struct ieee80211com *ic, struct ieee80211_node *ni,
                                uint8_t tid);
    static void virt_ampdu_stop(struct ieee80211com *ic, struct ieee80211_node *ni,
                                uint8_t tid);

private:
    struct ieee80211com fIc;
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

    IOFilterInterruptEventSource *fIntr;
    bool fIfAttached;
    bool fTaskqReady;

    struct task fInitTask;
    CTimeout *fScanWatchdog;
    IOSimpleLock *fScanLock;        /* guards fScan, fWcl, fScanId */
    VirtScan fScan;
    VirtWclTicket fWcl;
    uint32_t fScanId;
};

#endif /* ItlVirt_hpp */
