/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * HAL for the v-Airport virtual Wi-Fi device (vap_regs.h).  The device's
 * firmware runs on the VM host and owns scanning, association, encryption,
 * rate control and aggregation.  This is the skeleton without a transport:
 * every point where the transport plugs in is marked "VAP-SEAM:".
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

#include "vap_regs.h"

#define VIRT_MAX_CHANNELS   64
#define VIRT_RSSI_OFFSET    100     /* net80211 RSSI = dBm + 100 */

/* rxFrame() flags */
#define VIRT_RX_DECRYPTED   0x1     /* firmware removed IV and MIC and cleared Protected */
#define VIRT_RX_AMPDU_DONE  0x2     /* firmware already reordered this MPDU */

/*
 * What the firmware reports about the radio, in net80211 terms.  VHT and HE
 * are not carried yet.
 */
struct VirtCaps {
    uint8_t  macaddr[IEEE80211_ADDR_LEN];
    uint32_t ic_caps;               /* IEEE80211_C_* */
    uint8_t  max_rssi;              /* ceiling of dBm + VIRT_RSSI_OFFSET */
    uint8_t  nchans;
    struct {
        uint16_t freq;              /* MHz */
        uint32_t flags;             /* IEEE80211_CHAN_* */
    } chans[VIRT_MAX_CHANNELS];
    uint16_t htcaps;                /* IEEE80211_HTCAP_* */
    uint8_t  ampdu_params;
    uint8_t  sup_mcs[howmany(80, NBBY)];    /* all zero: no 11n */
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
    uint32_t id;                    /* nonzero, unique per scan */
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
    virtual const char *getFirmwareVersion() override { return "0"; }
    virtual int16_t getBSSNoise() override { return -92; }
    virtual bool is5GBandSupport() override {
        return fIc.ic_sup_rates[IEEE80211_MODE_11A].rs_nrates != 0;
    }
    virtual int getTxNSS() override { return 1; }
    virtual uint8_t getTxChainMask() override { return 0x1; }
    virtual uint8_t getRxChainMask() override { return 0x1; }
    virtual uint32_t getLqmBeaconCount() override { return 0; }
    virtual const char *getFirmwareName() override { return "v-airport"; }
    virtual UInt32 supportedFeatures() override { return kIONetworkFeatureMultiPages; }
    virtual const char *getFirmwareCountryCode() override { return "ZZ"; }
    virtual uint32_t getTxQueueSize() override { return VAP_QUEUE_MAX_SIZE; }

    //driver controller
    virtual void clearScanningFlags() override;
    // VAP-SEAM: transport sends the multicast filter to the firmware.
    virtual IOReturn setMulticastList(IOEthernetAddress *addr, int count) override {
        return kIOReturnSuccess;
    }

    void applyCaps(const VirtCaps &caps);

    //transport seams
    int txFrame(mbuf_t m, struct ieee80211_node *ni, bool mgmt);
    void rxFrame(mbuf_t m, uint16_t freqMHz, int8_t rssiDbm, uint32_t flags);
    int scanCommand(uint32_t id, bool background, bool wcl);
    void scanAbort(uint32_t id);
    void scanEnd(bool aborted, uint32_t id);

private:
    uint32_t regRead(uint32_t reg);
    int radioStart();
    void radioStop();
    int scanStart(bool background, uint64_t reassocSerial);
    void scanReset();

    static bool virt_intr_filter(OSObject *owner, IOFilterInterruptEventSource *sender);
    static void virt_intr(OSObject *owner, IOInterruptEventSource *sender, int count);
    static void virt_init_task(void *arg);
    static void virt_scan_timeout(void *arg);
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

    IOMemoryMap *fRegMap;
    IOFilterInterruptEventSource *fIntr;
    bool fIfAttached;
    bool fTaskqReady;

    struct task fInitTask;
    CTimeout *fScanTimer;           /* stands in for the firmware's scan-complete event */
    IOSimpleLock *fScanLock;        /* guards fScan, fWcl, fScanId */
    VirtScan fScan;
    VirtWclTicket fWcl;
    uint32_t fScanId;
};

#endif /* ItlVirt_hpp */
