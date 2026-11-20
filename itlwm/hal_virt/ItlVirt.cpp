/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "ItlVirt.hpp"

#include <libkern/OSByteOrder.h>
#include <IOKit/network/IONetworkController.h>
#include <net/ethernet.h>
#include <sys/_if_media.h>
#include <sys/_ifq.h>

#define super ItlHalService
OSDefineMetaClassAndStructors(ItlVirt, ItlHalService)

/*
 * How long the stand-in for the firmware "scans" before finding nothing.  Not
 * shorter: net80211 rescans at once after an empty foreground scan.
 */
#define VIRT_STUB_SCAN_MS   250

static void
virt_builtin_caps(VirtCaps *caps)
{
    static const uint8_t macaddr[IEEE80211_ADDR_LEN] =
        { 0x02, 0x00, 0x00, 0x80, 0x21, 0x01 };
    static const uint8_t chans5[] = { 36, 40, 44, 48 };
    int n = 0;

    bzero(caps, sizeof(*caps));
    IEEE80211_ADDR_COPY(caps->macaddr, macaddr);
    caps->ic_caps = IEEE80211_C_RSN;
    caps->max_rssi = VIRT_RSSI_OFFSET - 33;     /* -33 dBm, as the Intel HALs */
    for (int chan = 1; chan <= 11; chan++, n++) {
        caps->chans[n].freq = ieee80211_ieee2mhz(chan, IEEE80211_CHAN_2GHZ);
        caps->chans[n].flags = IEEE80211_CHAN_CCK | IEEE80211_CHAN_OFDM |
            IEEE80211_CHAN_DYN | IEEE80211_CHAN_2GHZ | IEEE80211_CHAN_HT20;
    }
    for (size_t i = 0; i < nitems(chans5); i++, n++) {
        caps->chans[n].freq = ieee80211_ieee2mhz(chans5[i], IEEE80211_CHAN_5GHZ);
        caps->chans[n].flags = IEEE80211_CHAN_A | IEEE80211_CHAN_HT20;
    }
    caps->nchans = n;
    caps->htcaps = IEEE80211_HTCAP_SMPS_DIS << IEEE80211_HTCAP_SMPS_SHIFT;
    caps->ampdu_params = IEEE80211_AMPDU_PARAM_SS_4 | 0x3 /* 64k */;
    caps->sup_mcs[0] = 0xff;                    /* MCS 0-7: one spatial stream */
}

bool ItlVirt::
virt_match(IOPCIDevice *device)
{
    return device->configRead16(kIOPCIConfigVendorID) == VAP_PCI_VENDOR &&
        device->configRead16(kIOPCIConfigDeviceID) == VAP_PCI_DEVICE;
}

uint32_t ItlVirt::
regRead(uint32_t reg)
{
    return OSReadLittleInt32((const volatile void *)fRegMap->getVirtualAddress(), reg);
}

bool ItlVirt::
virt_intr_filter(OSObject *owner, IOFilterInterruptEventSource *sender)
{
    return true;
}

// VAP-SEAM: transport drains the used rings of all four queues here; the one
// MSI vector has no status register to tell them apart.
void ItlVirt::
virt_intr(OSObject *owner, IOInterruptEventSource *sender, int count)
{
}

bool ItlVirt::
attach(IOPCIDevice *device)
{
    struct ieee80211com *ic = &fIc;
    struct _ifnet *ifp = &ic->ic_if;
    VirtCaps caps;
    uint32_t magic;
    int msiIndex = -1;

    device->setMemoryEnable(true);
    device->setBusMasterEnable(true);
    fRegMap = device->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0);
    if (fRegMap == NULL || fRegMap->getLength() < VAP_BAR0_SIZE) {
        XYLog("%s: can't map BAR0\n", __FUNCTION__);
        goto fail;
    }
    magic = regRead(VAP_REG_MAGIC);
    if (magic != VAP_MAGIC) {
        XYLog("%s: bad magic 0x%08x, expected 0x%08x\n", __FUNCTION__,
              magic, VAP_MAGIC);
        goto fail;
    }
    XYLog("%s: v-Airport device, transport version %u\n", __FUNCTION__,
          regRead(VAP_REG_VERSION));

    for (int index = 0; ; index++) {
        int interruptType;
        if (device->getInterruptType(index, &interruptType) != kIOReturnSuccess)
            break;
        if (interruptType & kIOInterruptTypePCIMessaged) {
            msiIndex = index;
            break;
        }
    }
    if (msiIndex == -1) {
        XYLog("%s: can't find MSI interrupt\n", __FUNCTION__);
        goto fail;
    }
    fIntr = IOFilterInterruptEventSource::filterInterruptEventSource(this,
        &ItlVirt::virt_intr, &ItlVirt::virt_intr_filter, device, msiIndex);
    if (fIntr == NULL) {
        XYLog("%s: can't establish interrupt\n", __FUNCTION__);
        goto fail;
    }
    if (getMainWorkLoop()->addEventSource(fIntr) != kIOReturnSuccess) {
        XYLog("%s: can't establish interrupt\n", __FUNCTION__);
        fIntr->release();
        fIntr = NULL;
        goto fail;
    }
    fIntr->enable();

    fScanLock = IOSimpleLockAlloc();
    if (fScanLock == NULL)
        goto fail;
    taskq_init();
    fTaskqReady = true;
    task_set(&fInitTask, virt_init_task, this, "virt_init_task");
    timeout_set(&fScanTimer, virt_scan_timeout, this);

    ic->ic_phytype = IEEE80211_T_OFDM;  /* not only, but not used */
    ic->ic_opmode = IEEE80211_M_STA;    /* default to BSS mode */
    ic->ic_state = IEEE80211_S_INIT;

    // VAP-SEAM: transport sets up the queues and fetches the capabilities
    // from the firmware (GET_CAPS) here, in place of the built-in set.
    virt_builtin_caps(&caps);
    applyCaps(caps);

    ifp->if_softc = this;
    ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
    ifp->if_ioctl = ieee80211_ioctl;
    ifp->if_start = virt_start;
    ifp->if_watchdog = ieee80211_watchdog;
    strlcpy(ifp->if_xname, "vap", sizeof(ifp->if_xname));

    if_attach(ifp);
    ieee80211_ifattach(ifp, getController());
    ieee80211_media_init(ifp);
    fIfAttached = true;

    ic->ic_bgscan_start = virt_bgscan;
    ic->ic_bgscan_abort = virt_bgscan_abort;
    ic->ic_set_key = virt_set_key;
    ic->ic_delete_key = virt_delete_key;
    ic->ic_ampdu_rx_start = virt_ampdu_start;
    ic->ic_ampdu_rx_stop = virt_ampdu_stop;
    ic->ic_ampdu_tx_start = virt_ampdu_start;
    ic->ic_ampdu_tx_stop = virt_ampdu_stop;
    /* Override 802.11 state transition machine. */
    fNewstate = ic->ic_newstate;
    ic->ic_newstate = virt_newstate;
    return true;

fail:
    detach(device);
    return false;
}

void ItlVirt::
detach(IOPCIDevice *device)
{
    struct _ifnet *ifp = &fIc.ic_if;

    ifp->if_flags &= ~IFF_UP;
    if (fTaskqReady) {
        task_del(systq, &fInitTask);
        /* task_del() cannot see a callback which is already running. */
        taskq_barrier(systq);
    }
    if (ifp->if_flags & IFF_RUNNING)
        radioStop();
    if (fIntr != NULL) {
        fIntr->disable();
        getMainWorkLoop()->removeEventSource(fIntr);
        fIntr->release();
        fIntr = NULL;
    }
    if (fScanTimer != NULL) {
        timeout_del(&fScanTimer);
        timeout_free(&fScanTimer);
    }
    if (fIfAttached) {
        ieee80211_ifdetach(ifp);
        fIfAttached = false;
    }
    if (fTaskqReady) {
        taskq_destroy(systq);
        fTaskqReady = false;
    }
    if (fScanLock != NULL) {
        IOSimpleLockFree(fScanLock);
        fScanLock = NULL;
    }
    if (fRegMap != NULL) {
        fRegMap->release();
        fRegMap = NULL;
    }
}

void ItlVirt::
free()
{
    if (ieee80211_bip_lifetime_drain(&fIc) != 0)
        panic("ItlVirt::free BIP lifetime");
    /* ieee80211_ifdetach() leaves this lock to the HAL's final release. */
    ieee80211_pae_selected_bss_lock_destroy(&fIc);
    super::free();
}

/*
 * Must run before ieee80211_ifattach(): it and ieee80211_media_init() derive
 * the interface address, the channel set and the PHY modes from these fields.
 */
void ItlVirt::
applyCaps(const VirtCaps &caps)
{
    struct ieee80211com *ic = &fIc;
    bool has2ghz = false, has5ghz = false;

    IEEE80211_ADDR_COPY(ic->ic_myaddr, caps.macaddr);
    /*
     * Not the radio's choice but this HAL's design: the firmware scans all
     * channels of all bands in one request and owns Block Ack setup, which
     * net80211 only uses together with QoS.
     */
    ic->ic_caps = caps.ic_caps | IEEE80211_C_SCANALL | IEEE80211_C_SCANALLBAND |
        IEEE80211_C_QOS | IEEE80211_C_TX_AMPDU | IEEE80211_C_TX_AMPDU_SETUP_IN_HW;
    ic->ic_max_rssi = caps.max_rssi;

    memset(ic->ic_channels, 0, sizeof(ic->ic_channels));
    for (int i = 0; i < caps.nchans && i < VIRT_MAX_CHANNELS; i++) {
        const u_int chan = ieee80211_mhz2ieee(caps.chans[i].freq,
                                              caps.chans[i].flags);
        if (chan == 0 || chan > IEEE80211_CHAN_MAX)
            continue;
        ic->ic_channels[chan].ic_freq = caps.chans[i].freq;
        ic->ic_channels[chan].ic_flags = caps.chans[i].flags;
        if (caps.chans[i].flags & IEEE80211_CHAN_2GHZ)
            has2ghz = true;
        if (caps.chans[i].flags & IEEE80211_CHAN_5GHZ)
            has5ghz = true;
    }
    memset(ic->ic_sup_rates, 0, sizeof(ic->ic_sup_rates));
    if (has2ghz) {
        ic->ic_sup_rates[IEEE80211_MODE_11B] = ieee80211_std_rateset_11b;
        ic->ic_sup_rates[IEEE80211_MODE_11G] = ieee80211_std_rateset_11g;
    }
    if (has5ghz)
        ic->ic_sup_rates[IEEE80211_MODE_11A] = ieee80211_std_rateset_11a;

    ic->ic_htcaps = caps.htcaps;
    ic->ic_htxcaps = 0;
    ic->ic_txbfcaps = 0;
    ic->ic_aselcaps = 0;
    ic->ic_ampdu_params = caps.ampdu_params;
    memcpy(ic->ic_sup_mcs, caps.sup_mcs, sizeof(ic->ic_sup_mcs));
    /* TX is supported with the same MCS as RX. */
    ic->ic_tx_mcs_set = IEEE80211_TX_MCS_SET_DEFINED;
}

IOReturn ItlVirt::
enable(IONetworkInterface *netif)
{
    struct _ifnet *ifp = &fIc.ic_if;

    if (!fIfAttached)
        return kIOReturnNotReady;
    if (ifp->if_flags & IFF_UP)
        return kIOReturnSuccess;
    ifp->if_flags |= IFF_UP;
    task_add(systq, &fInitTask);
    return kIOReturnSuccess;
}

IOReturn ItlVirt::
disable(IONetworkInterface *netif)
{
    struct _ifnet *ifp = &fIc.ic_if;

    if (!fIfAttached)
        return kIOReturnSuccess;
    ifp->if_flags &= ~IFF_UP;
    task_del(systq, &fInitTask);
    if (ifp->if_flags & IFF_RUNNING)
        radioStop();
    return kIOReturnSuccess;
}

void ItlVirt::
virt_init_task(void *arg)
{
    ItlVirt *that = (ItlVirt *)arg;
    struct _ifnet *ifp = &that->fIc.ic_if;

    if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) == IFF_UP)
        that->radioStart();
}

int ItlVirt::
radioStart()
{
    struct ieee80211com *ic = &fIc;
    struct _ifnet *ifp = &ic->ic_if;
    int err;

    // VAP-SEAM: transport resets the device, negotiates features, enables the
    // queues, posts RX and EVENT buffers and sets DRIVER_OK here.
    ifq_clr_oactive(&ifp->if_snd);
    ifq_flush(&ifp->if_snd);
    ifp->if_flags |= IFF_RUNNING;

    /* The power-on scan is a census: it must not select and join a BSS. */
    __atomic_store_n(&ic->ic_initial_scan_census_only, 1, __ATOMIC_RELEASE);
    err = ieee80211_begin_scan_with_result(ifp);
    if (err) {
        XYLog("%s: initial scan rejected (%d)\n", __FUNCTION__, err);
        radioStop();
        return err;
    }

    /*
     * The radio-ready edge on which the Airport layer publishes driver
     * availability.  It follows the first committed SCAN state so that a
     * consumer of that availability never submits into INIT.
     */
    if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING) &&
        ic->ic_event_handler != NULL)
        (*ic->ic_event_handler)(ic, IEEE80211_EVT_WCL_SCAN_REOPENED, NULL);
    return 0;
}

void ItlVirt::
radioStop()
{
    struct ieee80211com *ic = &fIc;
    struct _ifnet *ifp = &ic->ic_if;

    ifp->if_flags &= ~IFF_RUNNING;
    ifq_clr_oactive(&ifp->if_snd);
    ifq_flush(&ifp->if_snd);
    __atomic_store_n(&ic->ic_initial_scan_census_only, 0, __ATOMIC_RELEASE);
    scanReset();
    // VAP-SEAM: transport resets the device (VAP_REG_STATUS = 0) and takes
    // back every buffer still on the rings here.
    ieee80211_new_state(ic, IEEE80211_S_INIT, -1);
}

IOReturn ItlVirt::
virt_start_task(OSObject *target, void *arg0, void *arg1, void *arg2, void *arg3)
{
    struct _ifnet *ifp = (struct _ifnet *)arg0;
    ItlVirt *that = (ItlVirt *)ifp->if_softc;
    struct ieee80211com *ic = &that->fIc;
    struct ieee80211_node *ni;
    struct ether_header *eh;
    mbuf_t m;
    bool mgmt;

    if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
        return kIOReturnError;

    for (;;) {
        /* need to send management frames even if we're not RUNning */
        m = mq_dequeue(&ic->ic_mgtq);
        if (m) {
            ni = (struct ieee80211_node *)mbuf_pkthdr_rcvif(m);
            mgmt = true;
            goto sendit;
        }

        if (
#ifndef AIRPORT
            ic->ic_state != IEEE80211_S_RUN ||
#endif
            (ic->ic_xflags & IEEE80211_F_TX_MGMT_ONLY))
            break;

        m = ifq_dequeue(&ifp->if_snd);
        if (!m)
            break;
        if (mbuf_len(m) < sizeof (*eh) &&
            mbuf_pullup(&m, sizeof (*eh)) != 0) {
            ifp->netStat->outputErrors++;
            continue;
        }
        if ((m = ieee80211_encap(ifp, m, &ni)) == NULL) {
            ifp->netStat->outputErrors++;
            continue;
        }
        mgmt = false;

    sendit:
        if (that->txFrame(m, ni, mgmt) != 0) {
            ifp->netStat->outputErrors++;
            continue;
        }
        ifp->netStat->outputPackets++;
    }

    return kIOReturnSuccess;
}

void ItlVirt::
virt_start(struct _ifnet *ifp)
{
    ItlVirt *that = (ItlVirt *)ifp->if_softc;

    that->getMainCommandGate()->attemptAction(virt_start_task, ifp);
}

// VAP-SEAM: transport puts the 802.11 frame on VAP_Q_TX and frees it, with
// the node reference, on the firmware's completion; until then it is dropped.
/*
 * Consumes m and the node reference on every return.  A set Protected bit
 * marks plaintext which the firmware is to encrypt.
 */
int ItlVirt::
txFrame(mbuf_t m, struct ieee80211_node *ni, bool mgmt)
{
    mbuf_freem(m);
    ieee80211_release_node(&fIc, ni);
    return ENXIO;
}

// VAP-SEAM: transport calls this from virt_intr() for every frame taken off
// VAP_Q_RX: an 802.11 frame without FCS whose header is contiguous in m.
void ItlVirt::
rxFrame(mbuf_t m, uint16_t freqMHz, int8_t rssiDbm, uint32_t flags)
{
    struct ieee80211com *ic = &fIc;
    struct _ifnet *ifp = &ic->ic_if;
    struct mbuf_list ml = MBUF_LIST_INITIALIZER();
    struct ieee80211_frame *wh = mtod(m, struct ieee80211_frame *);
    struct ieee80211_rxinfo rxi;
    struct ieee80211_node *ni;
    u_int chan;

    /* The node lookup reads the addresses before net80211 checks the length. */
    if (!(ifp->if_flags & IFF_RUNNING) ||
        mbuf_len(m) < sizeof(struct ieee80211_frame_min) ||
        ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) != IEEE80211_FC0_TYPE_CTL &&
         mbuf_len(m) < sizeof(*wh))) {
        mbuf_freem(m);
        return;
    }

    bzero(&rxi, sizeof(rxi));
    /*
     * With the Protected bit already clear net80211 takes the payload as it
     * is and trusts the firmware's replay check.  Without this flag it would
     * drop such a frame as unencrypted.
     */
    if (flags & VIRT_RX_DECRYPTED)
        rxi.rxi_flags |= IEEE80211_RXI_HWDEC;
    /* Skips net80211's own reorder buffer. */
    if (flags & VIRT_RX_AMPDU_DONE)
        rxi.rxi_flags |= IEEE80211_RXI_AMPDU_DONE;
    rxi.rxi_rssi = MAX(0, MIN(rssiDbm + VIRT_RSSI_OFFSET, ic->ic_max_rssi));
    /* Zero means unknown: net80211 then assumes the current channel. */
    chan = ieee80211_mhz2ieee(freqMHz, 0);
    if (chan <= IEEE80211_CHAN_MAX && ic->ic_channels[chan].ic_freq == freqMHz)
        rxi.rxi_chan = chan;

    ni = ieee80211_find_rxnode(ic, wh);
    ieee80211_inputm(ifp, m, ni, &rxi, &ml);
    ieee80211_release_node(ic, ni);
    if_input(ifp, &ml);
}

int ItlVirt::
virt_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
    ItlVirt *that = (ItlVirt *)ic->ic_softc;
    int err;

    // VAP-SEAM: transport sends the firmware command of this transition
    // (authenticate, associate, disconnect) and commits it on the answer.
    if (nstate != IEEE80211_S_SCAN) {
        that->scanAbort(0);
        return that->fNewstate(ic, nstate, arg);
    }

    /*
     * The firmware scans all channels by itself, while the default handler
     * would run net80211's own channel-by-channel scan.  Commit SCAN here
     * instead, the way iwx does.
     */
    if (ic->ic_state == IEEE80211_S_RUN) {
        ieee80211_stop_ampdu_tx(ic, ic->ic_bss, -1);
        ieee80211_ba_del(ic->ic_bss);
    }
    err = that->scanStart(false, 0);
    if (err)
        return err == EALREADY ? 0 : err;
    /*
     * The current mode might have been fixed during association.
     * Ensure all channels get scanned.
     */
    if (IFM_MODE(ic->ic_media.ifm_cur->ifm_media) == IFM_AUTO)
        ieee80211_setmode(ic, IEEE80211_MODE_AUTO);
    ieee80211_set_link_state(ic, LINK_STATE_DOWN);
    ieee80211_node_cleanup(ic, ic->ic_bss);
    ic->ic_state = IEEE80211_S_SCAN;
    return 0;
}

int ItlVirt::
virt_bgscan(struct ieee80211com *ic, uint64_t reassocSerial)
{
    ItlVirt *that = (ItlVirt *)ic->ic_softc;

    return that->scanStart(true, reassocSerial);
}

int ItlVirt::
virt_bgscan_abort(struct ieee80211com *ic, uint64_t reassocSerial)
{
    ItlVirt *that = (ItlVirt *)ic->ic_softc;
    uint32_t id = 0;
    int err = 0;

    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(that->fScanLock);
    if (that->fScan.state == VIRT_SCAN_BACKGROUND) {
        if (reassocSerial != 0 && that->fScan.reassocSerial != reassocSerial)
            err = ECANCELED;
        else
            id = that->fScan.id;
    }
    IOSimpleLockUnlockEnableInterrupt(that->fScanLock, irq);
    if (id != 0)
        that->scanAbort(id);
    return err;
}

// VAP-SEAM: transport hands the key to the firmware, which then encrypts and
// decrypts with it; until then net80211's software crypto keeps it.
int ItlVirt::
virt_set_key(struct ieee80211com *ic, struct ieee80211_node *ni,
             struct ieee80211_key *k)
{
    return ieee80211_set_key(ic, ni, k);
}

// VAP-SEAM: transport removes the key from the firmware.
void ItlVirt::
virt_delete_key(struct ieee80211com *ic, struct ieee80211_node *ni,
                struct ieee80211_key *k)
{
    ieee80211_delete_key(ic, ni, k);
}

/* The firmware negotiates and runs Block Ack sessions on its own. */
int ItlVirt::
virt_ampdu_start(struct ieee80211com *ic, struct ieee80211_node *ni, uint8_t tid)
{
    return 0;
}

void ItlVirt::
virt_ampdu_stop(struct ieee80211com *ic, struct ieee80211_node *ni, uint8_t tid)
{
}

/*
 * Admission of every scan: newstate(SCAN) for foreground scans, ic_bgscan_start
 * for background ones.  A WCL request reaches it the same way, through
 * net80211, and is recognised by its waiting ticket.
 */
int ItlVirt::
scanStart(bool background, uint64_t reassocSerial)
{
    struct ieee80211com *ic = &fIc;
    struct ieee80211_wcl_scan_started started;
    const uint64_t joinGeneration =
        background ? 0 : ieee80211_wcl_join_scan_generation(ic);
    IOInterruptState irq;
    uint32_t id, stale = 0;
    bool wcl;
    int err;

    if (!(ic->ic_if.if_flags & IFF_RUNNING))
        return ENETDOWN;

    /*
     * Only a foreground request displaces a running scan: a background scan
     * when the association is being left, the generic census when WCL starts
     * its own initial scan.
     */
    irq = IOSimpleLockLockDisableInterrupt(fScanLock);
    wcl = fWcl.phase == VIRT_WCL_STARTING && fWcl.background == background &&
        reassocSerial == 0;
    if (!background && fScan.state != VIRT_SCAN_IDLE &&
        (fScan.state == VIRT_SCAN_BACKGROUND || wcl))
        stale = fScan.id;
    IOSimpleLockUnlockEnableInterrupt(fScanLock, irq);
    if (stale != 0)
        scanAbort(stale);

    bzero(&started, sizeof(started));
    irq = IOSimpleLockLockDisableInterrupt(fScanLock);
    if (fScan.state != VIRT_SCAN_IDLE) {
        err = !background && fScan.state == VIRT_SCAN_FOREGROUND ?
            EALREADY : EBUSY;
        IOSimpleLockUnlockEnableInterrupt(fScanLock, irq);
        return err;
    }
    if (++fScanId == 0)
        ++fScanId;
    id = fScanId;
    wcl = fWcl.phase == VIRT_WCL_STARTING && fWcl.background == background &&
        reassocSerial == 0;
    fScan.state = background ? VIRT_SCAN_BACKGROUND : VIRT_SCAN_FOREGROUND;
    fScan.wcl = wcl;
    fScan.id = id;
    fScan.joinGeneration = joinGeneration;
    fScan.reassocSerial = reassocSerial;
    if (wcl) {
        fWcl.phase = VIRT_WCL_ACTIVE;
        fWcl.backend = id;
        if (!background && !fWcl.silenced) {
            started.generation = fWcl.generation;
            started.backend_generation = id;
        }
    }
    IOSimpleLockUnlockEnableInterrupt(fScanLock, irq);
    if (wcl && background)
        __atomic_store_n(&ic->ic_wcl_scan_active, 1, __ATOMIC_RELEASE);

    err = scanCommand(id, background, wcl);
    if (err) {
        /* The ticket goes back to STARTING; its begin call reports the failure. */
        irq = IOSimpleLockLockDisableInterrupt(fScanLock);
        if (fScan.id == id) {
            bzero(&fScan, sizeof(fScan));
            if (wcl) {
                fWcl.phase = VIRT_WCL_STARTING;
                fWcl.backend = 0;
            }
        }
        IOSimpleLockUnlockEnableInterrupt(fScanLock, irq);
        if (wcl && background)
            __atomic_store_n(&ic->ic_wcl_scan_active, 0, __ATOMIC_RELEASE);
        return err;
    }

    /* A WCL initial scan becomes active above only by this event. */
    if (started.generation != 0 && ic->ic_event_handler != NULL)
        (*ic->ic_event_handler)(ic, IEEE80211_EVT_WCL_SCAN_STARTED, &started);
    return 0;
}

// VAP-SEAM: transport sends the scan request (for a WCL scan as described by
// ieee80211_wcl_scan_plan_snapshot()) and returns the firmware's verdict.
/* The stand-in arms a timer in place of the firmware's scan-complete event. */
int ItlVirt::
scanCommand(uint32_t id, bool background, bool wcl)
{
    return timeout_add_msec(&fScanTimer, VIRT_STUB_SCAN_MS) ? 0 : EIO;
}

void ItlVirt::
virt_scan_timeout(void *arg)
{
    ((ItlVirt *)arg)->scanEnd(false, 0);
}

// VAP-SEAM: transport tells the firmware to stop scan `id` (0: whichever is
// running) and waits for its answer before the scan is retired.
void ItlVirt::
scanAbort(uint32_t id)
{
    scanEnd(true, id);
}

// VAP-SEAM: transport calls this from the firmware's scan-complete event; the
// beacons and probe responses of the scan have gone through rxFrame() before.
void ItlVirt::
scanEnd(bool aborted, uint32_t id)
{
    struct ieee80211com *ic = &fIc;
    struct _ifnet *ifp = &ic->ic_if;
    struct ieee80211_wcl_scan_terminal terminal;
    VirtScan done;
    bool background;

    bzero(&terminal, sizeof(terminal));
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(fScanLock);
    if (fScan.state == VIRT_SCAN_IDLE || (id != 0 && fScan.id != id)) {
        IOSimpleLockUnlockEnableInterrupt(fScanLock, irq);
        return;
    }
    /* Retire the scan before any callback can admit its successor. */
    done = fScan;
    bzero(&fScan, sizeof(fScan));
    if (done.wcl) {
        if (!fWcl.silenced) {
            terminal.generation = fWcl.generation;
            terminal.backend_generation = fWcl.backend;
            terminal.status = aborted ?
                IEEE80211_WCL_SCAN_TERMINAL_STATUS_ABORTED :
                IEEE80211_WCL_SCAN_TERMINAL_STATUS_COMPLETE;
        }
        bzero(&fWcl, sizeof(fWcl));
    }
    IOSimpleLockUnlockEnableInterrupt(fScanLock, irq);
    background = done.state == VIRT_SCAN_BACKGROUND;

    if (aborted || (done.wcl && !background)) {
        /*
         * Scan cleanup only: no generic SCAN_DONE, no BSS selection and no
         * rescan.  An aborted census proves nothing about the candidates,
         * and a WCL foreground scan is reported by its tagged terminal alone.
         */
        if (background)
            ic->ic_flags &= ~(IEEE80211_F_BGSCAN |
                              IEEE80211_F_DISABLE_BG_AUTO_CONNECT);
        ieee80211_end_scan_controlled(ifp, aborted ?
            IEEE80211_SCAN_COMPLETION_WCL_HANDOFF :
            IEEE80211_SCAN_COMPLETION_WCL_FOREGROUND);
    } else {
        /* A WCL background scan ends like a generic one, minus SCAN_DONE. */
        if (done.wcl)
            __atomic_store_n(&ic->ic_wcl_scan_suppress_scan_done_once, 1,
                             __ATOMIC_RELEASE);
        ieee80211_end_scan_owned(ifp, IEEE80211_SCAN_COMPLETION_GENERIC,
                                 done.joinGeneration, done.reassocSerial);
    }
    if (done.wcl && background)
        __atomic_store_n(&ic->ic_wcl_scan_active, 0, __ATOMIC_RELEASE);
    if (terminal.generation != 0 && ic->ic_event_handler != NULL)
        (*ic->ic_event_handler)(ic, IEEE80211_EVT_WCL_SCAN_TERMINAL, &terminal);
}

/*
 * The radio is being reset and the firmware forgets its scan.  That is not a
 * scan terminal: a WCL request is told that it will never get one.
 */
void ItlVirt::
scanReset()
{
    struct ieee80211com *ic = &fIc;
    VirtWclTicket ticket;

    timeout_del(&fScanTimer);
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(fScanLock);
    ticket = fWcl;
    bzero(&fScan, sizeof(fScan));
    bzero(&fWcl, sizeof(fWcl));
    IOSimpleLockUnlockEnableInterrupt(fScanLock, irq);
    __atomic_store_n(&ic->ic_wcl_scan_active, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&ic->ic_wcl_scan_suppress_scan_done_once, 0,
                     __ATOMIC_RELEASE);
    ic->ic_flags &= ~(IEEE80211_F_BGSCAN | IEEE80211_F_DISABLE_BG_AUTO_CONNECT);

    if (ic->ic_event_handler == NULL)
        return;
    if (ticket.phase == VIRT_WCL_ACTIVE) {
        struct ieee80211_wcl_scan_invalidation invalidation;
        bzero(&invalidation, sizeof(invalidation));
        invalidation.generation = ticket.generation;
        invalidation.backend_generation = ticket.backend;
        (*ic->ic_event_handler)(ic, IEEE80211_EVT_WCL_SCAN_INVALIDATED,
                                &invalidation);
    } else if (ticket.phase == VIRT_WCL_STARTING) {
        struct ieee80211_wcl_scan_start_rejected rejected;
        bzero(&rejected, sizeof(rejected));
        rejected.generation = ticket.generation;
        (*ic->ic_event_handler)(ic, IEEE80211_EVT_WCL_SCAN_START_REJECTED,
                                &rejected);
    }
}

IOReturn ItlVirt::
beginWclInitialScan(uint64_t generation, uint32_t *outBackendGeneration)
{
    struct ieee80211com *ic = &fIc;
    IOInterruptState irq;
    bool started;

    if (generation == 0 || outBackendGeneration == NULL)
        return kIOReturnBadArgument;
    /* Stays zero: the STARTED event publishes the backend generation. */
    *outBackendGeneration = 0;
    if (ic->ic_event_handler == NULL)
        return kIOReturnNotReady;
    if (ic->ic_state != IEEE80211_S_SCAN ||
        ic->ic_opmode != IEEE80211_M_STA ||
        (ic->ic_if.if_flags & IFF_RUNNING) == 0 ||
        ic->ic_mgt_timer != 0 || ic->ic_des_esslen != 0)
        return kIOReturnBusy;

    irq = IOSimpleLockLockDisableInterrupt(fScanLock);
    if (fWcl.phase != VIRT_WCL_IDLE) {
        IOSimpleLockUnlockEnableInterrupt(fScanLock, irq);
        return kIOReturnBusy;
    }
    fWcl.phase = VIRT_WCL_STARTING;
    fWcl.background = false;
    fWcl.generation = generation;
    IOSimpleLockUnlockEnableInterrupt(fScanLock, irq);

    ieee80211_begin_scan(&ic->ic_if);

    /* A ticket still STARTING never reached the firmware. */
    irq = IOSimpleLockLockDisableInterrupt(fScanLock);
    started = !(fWcl.phase == VIRT_WCL_STARTING && fWcl.generation == generation);
    if (!started)
        bzero(&fWcl, sizeof(fWcl));
    IOSimpleLockUnlockEnableInterrupt(fScanLock, irq);
    return started ? kIOReturnSuccess : kIOReturnBusy;
}

IOReturn ItlVirt::
beginWclBackgroundScan(uint64_t generation, uint32_t *outBackendGeneration)
{
    struct ieee80211com *ic = &fIc;
    IOInterruptState irq;
    IOReturn ret = kIOReturnSuccess;

    if (generation == 0 || outBackendGeneration == NULL)
        return kIOReturnBadArgument;
    *outBackendGeneration = 0;
    if (ic->ic_state != IEEE80211_S_RUN || ic->ic_bss == NULL ||
        ic->ic_mgt_timer != 0 || (ic->ic_flags & IEEE80211_F_BGSCAN) != 0 ||
        ((ic->ic_flags & IEEE80211_F_RSNON) != 0 &&
         !ic->ic_bss->ni_port_valid))
        return kIOReturnBusy;

    irq = IOSimpleLockLockDisableInterrupt(fScanLock);
    if (fWcl.phase != VIRT_WCL_IDLE || fScan.state != VIRT_SCAN_IDLE) {
        IOSimpleLockUnlockEnableInterrupt(fScanLock, irq);
        return kIOReturnBusy;
    }
    fWcl.phase = VIRT_WCL_STARTING;
    fWcl.background = true;
    fWcl.generation = generation;
    IOSimpleLockUnlockEnableInterrupt(fScanLock, irq);

    ieee80211_begin_cache_bgscan(&ic->ic_if);

    irq = IOSimpleLockLockDisableInterrupt(fScanLock);
    if (fWcl.generation == generation && fWcl.phase == VIRT_WCL_ACTIVE) {
        *outBackendGeneration = fWcl.backend;
    } else if (fWcl.generation == generation) {
        bzero(&fWcl, sizeof(fWcl));
        ret = kIOReturnBusy;
    }
    /* Otherwise the scan has already ended and published its terminal. */
    IOSimpleLockUnlockEnableInterrupt(fScanLock, irq);
    if (ret != kIOReturnSuccess)
        ic->ic_flags &= ~(IEEE80211_F_BGSCAN |
                          IEEE80211_F_DISABLE_BG_AUTO_CONNECT);
    return ret;
}

IOReturn ItlVirt::
abortWclBackgroundScan(uint64_t generation)
{
    uint32_t id = 0;

    if (generation == 0 || fScanLock == NULL)
        return kIOReturnBadArgument;
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(fScanLock);
    if (fWcl.phase == VIRT_WCL_ACTIVE && fWcl.generation == generation)
        id = fWcl.backend;
    IOSimpleLockUnlockEnableInterrupt(fScanLock, irq);
    if (id == 0)
        return kIOReturnNotReady;
    /* Publishes the ticket's ABORTED terminal. */
    scanAbort(id);
    return kIOReturnSuccess;
}

/* The controller may call this before attach and after detach. */
void ItlVirt::
invalidateWclBackgroundScan()
{
    if (fScanLock == NULL)
        return;
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(fScanLock);
    if (fWcl.phase != VIRT_WCL_IDLE)
        fWcl.silenced = true;
    IOSimpleLockUnlockEnableInterrupt(fScanLock, irq);
}

void ItlVirt::
clearScanningFlags()
{
    if (fScanLock != NULL)
        scanAbort(0);
}
