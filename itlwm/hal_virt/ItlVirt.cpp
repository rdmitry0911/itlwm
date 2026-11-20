/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "ItlVirt.hpp"

#include <libkern/OSByteOrder.h>
#include <IOKit/network/IONetworkController.h>
#include <net/ethernet.h>
#include <sys/_if_media.h>
#include <sys/_ifq.h>

#define super ItlHalService
OSDefineMetaClassAndStructors(ItlVirt, ItlHalService)

struct VirtRequest {
    uint16_t    type;
    const void *payload;
    uint32_t    len;
    void       *rsp;
    uint32_t   *rspLen;
    int         rc;
};

bool ItlVirt::
virt_match(IOPCIDevice *device)
{
    return device->configRead16(kIOPCIConfigVendorID) == VAP_PCI_VENDOR &&
        device->configRead16(kIOPCIConfigDeviceID) == VAP_PCI_DEVICE;
}

/*
 * One request to the firmware, waited for.  Returns the firmware's status
 * (0 or VAP_E_*) or VAP_G_*.  On entry *rspLen is the room at rsp, on return
 * the size of the reply.
 */
int ItlVirt::
request(uint16_t type, const void *payload, uint32_t len, void *rsp, uint32_t *rspLen)
{
    VirtRequest rq = { type, payload, len, rsp, rspLen, VAP_G_EIO };

    getMainCommandGate()->runAction(virt_request_gated, this, &rq);
    return rq.rc;
}

IOReturn ItlVirt::
virt_request_gated(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3)
{
    ItlVirt *that = (ItlVirt *)arg0;
    VirtRequest *rq = (VirtRequest *)arg1;
    struct vap_dev *d = that->fDev;
    const void *p = NULL;
    uint32_t n = 0, waited;
    int slot;

    /* A reply that arrived after its request had been given up frees the slot. */
    for (slot = 0; slot < (int)VAP_G_CTRL_SLOTS; slot++) {
        if ((that->fCtrlLost & (1 << slot)) && vap_ctrl_done(d, slot)) {
            vap_ctrl_release(d, slot);
            that->fCtrlLost &= ~(1 << slot);
        }
    }

    slot = vap_ctrl_send(d, rq->type, rq->payload, rq->len);
    if (slot < 0) {
        rq->rc = slot;
        return kIOReturnSuccess;
    }
    /*
     * The interrupt action cannot run while this thread holds the gate, so
     * the reply is polled for; only control replies, which keeps events and
     * frames from being delivered into the middle of the caller.
     */
    for (waited = 0; ; waited += 20) {
        vap_dev_poll_ctrl(d);
        if (vap_ctrl_done(d, slot) || waited >= VIRT_REQUEST_MS * 1000)
            break;
        IODelay(20);
    }
    if (!vap_ctrl_done(d, slot)) {
        that->fCtrlLost |= 1 << slot;
        XYLog("vap: request 0x%x: no reply in %u ms%s\n", rq->type, VIRT_REQUEST_MS,
              vap_dev_needs_reset(d) ? ", the device needs a reset" : "");
        return kIOReturnSuccess;
    }
    rq->rc = vap_ctrl_result(d, slot, &p, &n);
    if (rq->rsp != NULL && rq->rspLen != NULL) {
        if (rq->rc != VAP_G_EIO && n != 0)
            memcpy(rq->rsp, p, MIN(n, *rq->rspLen));
        *rq->rspLen = n;
    }
    vap_ctrl_release(d, slot);
    return kIOReturnSuccess;
}

/* The next TLV of a GET_CAPS reply; false at the end or at a TLV which overruns it. */
static bool
virt_tlv_next(const uint8_t *buf, uint32_t len, uint32_t *off, struct vap_tlv *t,
              const uint8_t **val)
{
    if (*off + sizeof(*t) > len)
        return false;
    memcpy(t, buf + *off, sizeof(*t));
    if (t->len > len - *off - sizeof(*t))
        return false;
    *val = buf + *off + sizeof(*t);
    *off += (uint32_t)sizeof(*t) + ((t->len + 3u) & ~3u);
    return true;
}

static bool
virt_parse_caps(const uint8_t *buf, uint32_t len, VirtCaps *caps)
{
    /* Per band: what decides the widths its channels can be used with. */
    bool ht[2] = { false, false }, vht[2] = { false, false };
    uint16_t htinfo[2] = { 0, 0 };
    uint32_t vhtinfo[2] = { 0, 0 };
    struct vap_tlv t;
    const uint8_t *v;
    uint32_t off;

    for (off = 0; virt_tlv_next(buf, len, &off, &t, &v);) {
        /* The per-band TLVs start with the band and three reserved bytes. */
        const uint8_t band = (t.len >= 4 && v[0] <= VAP_BAND_5G) ? v[0] : 0xff;
        const uint8_t *body = v + 4;
        const uint32_t n = t.len >= 4 ? t.len - 4u : 0;

        switch (t.type) {
        case VAP_TLV_RATES:
            if (band != 0xff) {
                struct ieee80211_rateset *rs =
                    band == VAP_BAND_2G ? &caps->rates_2g : &caps->rates_5g;
                rs->rs_nrates = (uint8_t)MIN(n, (uint32_t)IEEE80211_RATE_MAXSIZE);
                memcpy(rs->rs_rates, body, rs->rs_nrates);
            }
            break;
        case VAP_TLV_HT_CAP:
            if (band != 0xff && n == sizeof(caps->htcap)) {
                ht[band] = true;
                htinfo[band] = OSReadLittleInt16(body, 0);
                if (!caps->ht)
                    memcpy(caps->htcap, body, n);
                caps->ht = true;
            }
            break;
        case VAP_TLV_VHT_CAP:
            if (band != 0xff && n == sizeof(caps->vhtcap)) {
                vht[band] = true;
                vhtinfo[band] = OSReadLittleInt32(body, 0);
                if (!caps->vht)
                    memcpy(caps->vhtcap, body, n);
                caps->vht = true;
            }
            break;
        case VAP_TLV_HE_CAP:
            /* MAC 6, PHY 11, at least the MCS maps up to 80 MHz; 5 GHz says more. */
            if (band != 0xff && n >= 21 && n <= sizeof(caps->hecap) &&
                (caps->hecap_len == 0 || band == VAP_BAND_5G)) {
                memcpy(caps->hecap, body, n);
                caps->hecap_len = (uint8_t)n;
            }
            break;
        case VAP_TLV_REGDOM:
            /* Anything else, like the world domain "00", stays unknown. */
            if (t.len >= 2 && v[0] >= 'A' && v[0] <= 'Z' && v[1] >= 'A' && v[1] <= 'Z') {
                caps->country[0] = (char)v[0];
                caps->country[1] = (char)v[1];
            }
            break;
        case VAP_TLV_ANTENNAS:
            if (t.len >= 4) {
                caps->tx_chainmask = v[0];
                caps->rx_chainmask = v[1];
                caps->tx_nss = MAX(v[2], 1);
                caps->rx_nss = MAX(v[3], 1);
            }
            break;
        case VAP_TLV_FW_NAME:
            if (t.len != 0) {
                const size_t k = MIN((size_t)t.len, sizeof(caps->fw_name) - 1);
                memcpy(caps->fw_name, v, k);
                caps->fw_name[k] = '\0';
            }
            break;
        default:
            break;
        }
    }
    /* Stopped before the end: a TLV overruns the reply. */
    if (off + sizeof(t) <= len)
        return false;

    /* The channels come last: their flags depend on the elements of their band. */
    for (off = 0; virt_tlv_next(buf, len, &off, &t, &v);) {
        uint8_t band;

        if (t.type != VAP_TLV_CHANNELS || t.len < 4 || v[0] > VAP_BAND_5G)
            continue;
        band = v[0];
        for (uint32_t i = 4; i + sizeof(struct vap_chan) <= t.len &&
             caps->nchans < VIRT_MAX_CHANNELS; i += (uint32_t)sizeof(struct vap_chan)) {
            struct vap_chan c;
            uint32_t flags;

            memcpy(&c, v + i, sizeof(c));
            if (c.flags & VAP_CHAN_DISABLED)
                continue;
            flags = band == VAP_BAND_2G ? (IEEE80211_CHAN_CCK | IEEE80211_CHAN_OFDM |
                IEEE80211_CHAN_DYN | IEEE80211_CHAN_2GHZ) : IEEE80211_CHAN_A;
            if (c.flags & VAP_CHAN_NO_IR)
                flags |= IEEE80211_CHAN_PASSIVE;
            if (c.flags & VAP_CHAN_RADAR)
                flags |= IEEE80211_CHAN_DFS;
            if (ht[band]) {
                flags |= IEEE80211_CHAN_HT20;
                if (htinfo[band] & IEEE80211_HTCAP_CBW20_40) {
                    if (!(c.flags & VAP_CHAN_NO_HT40P))
                        flags |= IEEE80211_CHAN_HT40U;
                    if (!(c.flags & VAP_CHAN_NO_HT40M))
                        flags |= IEEE80211_CHAN_HT40D;
                }
            }
            if (vht[band]) {
                if (!(c.flags & VAP_CHAN_NO_80))
                    flags |= IEEE80211_CHAN_VHT80;
                if (!(c.flags & VAP_CHAN_NO_160) &&
                    (vhtinfo[band] & IEEE80211_VHTCAP_SUPP_CHAN_WIDTH_MASK))
                    flags |= IEEE80211_CHAN_VHT160;
            }
            caps->chans[caps->nchans].freq = c.freq_mhz;
            caps->chans[caps->nchans].flags = flags;
            caps->chans[caps->nchans].max_dbm = c.max_dbm;
            caps->nchans++;
        }
    }
    return caps->nchans != 0;
}

/* HELLO and GET_CAPS: who the firmware is and what its radio can do. */
bool ItlVirt::
queryCaps(VirtCaps *caps)
{
    struct vap_hello_req hq;
    struct vap_hello_rsp hs;
    uint32_t len = sizeof(hs);
    uint8_t *tlv;
    bool ok;
    int rc;

    bzero(&hq, sizeof(hq));
    bzero(&hs, sizeof(hs));
    hq.proto_version = VAP_PROTO_VERSION;
    rc = request(VAP_CMD_HELLO, &hq, sizeof(hq), &hs, &len);
    if (rc != 0 || len != sizeof(hs) || hs.proto_version != VAP_PROTO_VERSION) {
        XYLog("vap: HELLO failed: status %d, %u bytes, protocol %u (ours %u)\n",
              rc, len, hs.proto_version, VAP_PROTO_VERSION);
        return false;
    }
    if (IEEE80211_IS_MULTICAST(hs.mac) || IEEE80211_ADDR_EQ(hs.mac, etheranyaddr)) {
        XYLog("vap: HELLO: unusable MAC address %s\n", ether_sprintf(hs.mac));
        return false;
    }

    bzero(caps, sizeof(*caps));
    IEEE80211_ADDR_COPY(caps->macaddr, hs.mac);
    hs.fw_version[sizeof(hs.fw_version) - 1] = '\0';
    strlcpy(caps->fw_version, hs.fw_version, sizeof(caps->fw_version));
    strlcpy(caps->fw_name, "v-airport", sizeof(caps->fw_name));
    strlcpy(caps->country, "ZZ", sizeof(caps->country));
    caps->tx_chainmask = caps->rx_chainmask = 0x1;
    caps->tx_nss = caps->rx_nss = 1;

    tlv = (uint8_t *)IOMalloc(VAP_G_CTRL_MAX);
    if (tlv == NULL)
        return false;
    len = VAP_G_CTRL_MAX;
    rc = request(VAP_CMD_GET_CAPS, NULL, 0, tlv, &len);
    ok = rc == 0 && len <= VAP_G_CTRL_MAX && virt_parse_caps(tlv, len, caps);
    IOFree(tlv, VAP_G_CTRL_MAX);
    if (!ok) {
        XYLog("vap: GET_CAPS failed: status %d, %u bytes\n", rc, len);
        return false;
    }

    XYLog("vap: firmware \"%s\" (%s), MAC %s, features 0x%llx, max frame %u\n",
          caps->fw_version, caps->fw_name, ether_sprintf(caps->macaddr),
          (unsigned long long)hs.fw_features, hs.max_frame);
    XYLog("vap: %u channels, HT %d, VHT %d, HE %d, country %s, chains tx 0x%x rx 0x%x, nss %u/%u\n",
          caps->nchans, caps->ht, caps->vht, caps->hecap_len != 0, caps->country,
          caps->tx_chainmask, caps->rx_chainmask, caps->tx_nss, caps->rx_nss);
    return true;
}

bool ItlVirt::
virt_intr_filter(OSObject *owner, IOFilterInterruptEventSource *sender)
{
    return true;
}

void ItlVirt::
virt_intr(OSObject *owner, IOInterruptEventSource *sender, int count)
{
    ItlVirt *that = (ItlVirt *)owner;
    uint64_t now;

    vap_dev_poll(that->fDev);

    /* Reading the status is a round trip to the host: not on every interrupt. */
    if (that->fResetLogged)
        return;
    clock_get_uptime(&now);
    if (now < that->fResetCheckAt)
        return;
    clock_interval_to_deadline(1, kSecondScale, &that->fResetCheckAt);
    if (vap_dev_needs_reset(that->fDev)) {
        that->fResetLogged = true;
        // VAP-SEAM: transport brings the device up again with vap_dev_start().
        XYLog("vap: the device needs a reset (the firmware restarted); not handled yet\n");
    }
}

void ItlVirt::
virt_event(void *ctx, const struct vap_hdr *h, const void *payload, uint32_t len)
{
    ItlVirt *that = (ItlVirt *)ctx;

    if (h->type == VAP_EVT_SCAN_DONE && len >= sizeof(struct vap_evt_scan_done)) {
        const struct vap_evt_scan_done *e = (const struct vap_evt_scan_done *)payload;

        XYLog("vap: scan %u done, %u results%s\n", e->scan_id, e->n_results,
              e->aborted ? ", aborted" : "");
        /* Only the scan which is still running; the id filters out the rest. */
        if (e->scan_id != 0)
            that->scanEnd(e->aborted != 0, e->scan_id);
        /* The firmware is free again for a scan it had no room for. */
        that->scanRetry();
        return;
    }
    if (h->type == VAP_EVT_DISCONNECTED && len >= sizeof(struct vap_evt_disconnected)) {
        const struct vap_evt_disconnected *e = (const struct vap_evt_disconnected *)payload;
        struct ieee80211com *ic = &that->fIc;

        XYLog("vap: disconnected from %s, reason %u%s\n", ether_sprintf((u_char *)e->bssid),
              e->reason, e->by_ap ? ", by the AP" : "");
        /* Normally the deauthentication frame came first and did this already. */
        if (ic->ic_state == IEEE80211_S_RUN && ic->ic_bss != NULL &&
            IEEE80211_ADDR_EQ(ic->ic_bss->ni_bssid, e->bssid))
            ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
        return;
    }
    XYLog("vap: event 0x%x, %u bytes\n", h->type, len);
}

void ItlVirt::
virt_rx(void *ctx, const struct vap_rx_meta *meta, const uint8_t *frame, uint32_t len)
{
    ItlVirt *that = (ItlVirt *)ctx;
    mbuf_t m;

    if (len == 0 || (m = that->getController()->allocatePacket(len)) == NULL)
        return;
    /* A packet of more than one cluster may come as a chain. */
    if (mbuf_copyback(m, 0, len, frame, MBUF_DONTWAIT) != 0) {
        mbuf_freem(m);
        return;
    }
    that->rxFrame(m, meta->freq_mhz, meta->rssi_dbm,
                  (meta->flags & VAP_RXF_DECRYPTED) ? VIRT_RX_DECRYPTED : 0);
}

/* The device has room again for the frame it refused. */
void ItlVirt::
virt_tx_ready(void *ctx)
{
    ItlVirt *that = (ItlVirt *)ctx;
    struct _ifnet *ifp = &that->fIc.ic_if;

    ifq_clr_oactive(&ifp->if_snd);
    virt_start_task(NULL, ifp, NULL, NULL, NULL);
}

bool ItlVirt::
attach(IOPCIDevice *device)
{
    struct ieee80211com *ic = &fIc;
    struct _ifnet *ifp = &ic->ic_if;
    struct vap_callbacks cb = { this, NULL, virt_event, virt_rx, virt_tx_ready };
    struct vap_platform pf;
    VirtCaps caps;
    int msiIndex = -1;
    int rc;

    device->setMemoryEnable(true);
    device->setBusMasterEnable(true);
    if (!fPlatform.init(device)) {
        XYLog("vap: can't map BAR0\n");
        goto fail;
    }
    fMapped = true;
    pf = fPlatform.platform();
    XYLog("vap: magic 0x%08x, transport version %u\n",
          pf.rd32(pf.ctx, VAP_REG_MAGIC), pf.rd32(pf.ctx, VAP_REG_VERSION));
    if (fDev == NULL)
        fDev = (struct vap_dev *)IOMallocZero(sizeof(*fDev));
    if (fDev == NULL)
        goto fail;
    /* Nothing else can call into the transport yet: no interrupt, no interface. */
    rc = vap_dev_init(fDev, &pf, &cb);
    if (rc != VAP_G_OK) {
        XYLog("vap: device bring-up failed (%d)\n", rc);
        goto fail;
    }

    fScanLock = IOSimpleLockAlloc();
    if (fScanLock == NULL)
        goto fail;
    taskq_init();
    fTaskqReady = true;
    task_set(&fInitTask, virt_init_task, this, "virt_init_task");
    timeout_set(&fScanWatchdog, virt_scan_watchdog, this);
    timeout_set(&fStatsTimer, virt_stats_timer, this);

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
        XYLog("vap: can't find MSI interrupt\n");
        goto fail;
    }
    fIntr = IOFilterInterruptEventSource::filterInterruptEventSource(this,
        &ItlVirt::virt_intr, &ItlVirt::virt_intr_filter, device, msiIndex);
    if (fIntr == NULL) {
        XYLog("vap: can't establish interrupt\n");
        goto fail;
    }
    if (getMainWorkLoop()->addEventSource(fIntr) != kIOReturnSuccess) {
        XYLog("vap: can't establish interrupt\n");
        fIntr->release();
        fIntr = NULL;
        goto fail;
    }
    fIntr->enable();

    ic->ic_phytype = IEEE80211_T_OFDM;  /* not only, but not used */
    ic->ic_opmode = IEEE80211_M_STA;    /* default to BSS mode */
    ic->ic_state = IEEE80211_S_INIT;

    if (!queryCaps(&caps))
        goto fail;
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
    XYLog("vap: attached\n");
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
    if (fIfAttached)
        getMainCommandGate()->runAction(virt_radio_gated, this, NULL);
    if (fIntr != NULL) {
        fIntr->disable();
        getMainWorkLoop()->removeEventSource(fIntr);
        fIntr->release();
        fIntr = NULL;
    }
    if (fStatsTimer != NULL) {
        timeout_del(&fStatsTimer);
        timeout_free(&fStatsTimer);
    }
    if (fScanWatchdog != NULL) {
        timeout_del(&fScanWatchdog);
        timeout_free(&fScanWatchdog);
    }
    if (fIfAttached) {
        ieee80211_ifdetach(ifp);
        fIfAttached = false;
    }
    /* With the interrupt source gone nothing else calls into the transport. */
    if (fMapped) {
        if (fDev != NULL)
            vap_dev_fini(fDev);
        fPlatform.free();
        fMapped = false;
    }
    if (fTaskqReady) {
        taskq_destroy(systq);
        fTaskqReady = false;
    }
    if (fScanLock != NULL) {
        IOSimpleLockFree(fScanLock);
        fScanLock = NULL;
    }
}

void ItlVirt::
free()
{
    /* Kept until here: a late caller finds a stopped device, not a freed one. */
    if (fDev != NULL) {
        IOFree(fDev, sizeof(*fDev));
        fDev = NULL;
    }
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

    fCaps = caps;
    IEEE80211_ADDR_COPY(ic->ic_myaddr, caps.macaddr);
    IEEE80211_ADDR_COPY(fFwMac, caps.macaddr);
    /*
     * Not the radio's choice but this HAL's design: the firmware scans all
     * channels of all bands in one request and owns Block Ack setup, which
     * net80211 only uses together with QoS.  The cipher list of the firmware
     * is not consulted until keys are handed to it.
     */
    ic->ic_caps = IEEE80211_C_RSN | IEEE80211_C_SCANALL | IEEE80211_C_SCANALLBAND |
        IEEE80211_C_QOS | IEEE80211_C_TX_AMPDU | IEEE80211_C_TX_AMPDU_SETUP_IN_HW;
    ic->ic_max_rssi = VIRT_RSSI_OFFSET - 33;    /* 100 % is -33 dBm, as in the Intel HALs */

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
        const struct ieee80211_rateset *g = caps.rates_2g.rs_nrates != 0 ?
            &caps.rates_2g : &ieee80211_std_rateset_11g;
        struct ieee80211_rateset *b = &ic->ic_sup_rates[IEEE80211_MODE_11B];

        ic->ic_sup_rates[IEEE80211_MODE_11G] = *g;
        /* 11b is the CCK part: 1, 2, 5.5 and 11 Mbit/s. */
        for (int i = 0; i < g->rs_nrates; i++) {
            const uint8_t rate = g->rs_rates[i] & IEEE80211_RATE_VAL;
            if (rate == 2 || rate == 4 || rate == 11 || rate == 22)
                b->rs_rates[b->rs_nrates++] = g->rs_rates[i];
        }
    }
    if (has5ghz)
        ic->ic_sup_rates[IEEE80211_MODE_11A] = caps.rates_5g.rs_nrates != 0 ?
            caps.rates_5g : ieee80211_std_rateset_11a;

    if (caps.ht) {
        const uint8_t *h = caps.htcap;

        ic->ic_htcaps = OSReadLittleInt16(h, 0);
        ic->ic_ampdu_params = h[2];
        /* The MCS set: 10 bytes of receive bitmap, the transmit byte at 12. */
        memcpy(ic->ic_sup_mcs, h + 3, sizeof(ic->ic_sup_mcs));
        ic->ic_tx_mcs_set = h[3 + 12];
        ic->ic_htxcaps = OSReadLittleInt16(h, 19);
        ic->ic_txbfcaps = OSReadLittleInt32(h, 21);
        ic->ic_aselcaps = h[25];
    }
    if (caps.vht && !(ic->ic_userflags & IEEE80211_F_NOVHT)) {
        /* Per stream: MCS 0-7, 0-8, 0-9, or none. */
        static const uint16_t mcs[4] = { 0x00ff, 0x01ff, 0x03ff, 0 };
        const uint8_t *v = caps.vhtcap;

        ic->ic_flags |= IEEE80211_F_VHTON;
        ic->ic_vhtcaps = OSReadLittleInt32(v, 0);
        ic->ic_vht_rx_mcs_map = OSReadLittleInt16(v, 4);
        ic->ic_vht_rx_highest = OSReadLittleInt16(v, 6);
        ic->ic_vht_tx_mcs_map = OSReadLittleInt16(v, 8);
        ic->ic_vht_tx_highest = OSReadLittleInt16(v, 10);
        for (int i = 0; i < 8; i++)
            ic->ic_vht_sup_mcs[i] = mcs[(ic->ic_vht_rx_mcs_map >> (2 * i)) & 3];
    }
    if (caps.hecap_len != 0 && (ic->ic_flags & IEEE80211_F_VHTON)) {
        /*
         * The element body: fixed capabilities, then one pair of MCS maps
         * per supported width class, then the PPE thresholds.
         */
        const uint8_t *h = caps.hecap;
        uint32_t off = sizeof(ic->ic_he_cap_elem);

        ic->ic_flags |= IEEE80211_F_HEON;
        memcpy(&ic->ic_he_cap_elem, h, sizeof(ic->ic_he_cap_elem));
        memset(&ic->ic_he_mcs_nss_supp, 0xff, sizeof(ic->ic_he_mcs_nss_supp));
        memcpy(&ic->ic_he_mcs_nss_supp.rx_mcs_80, h + off, 4);
        off += 4;
        if ((ic->ic_he_cap_elem.phy_cap_info[0] &
             IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G) && off + 4 <= caps.hecap_len) {
            memcpy(&ic->ic_he_mcs_nss_supp.rx_mcs_160, h + off, 4);
            off += 4;
        }
        if ((ic->ic_he_cap_elem.phy_cap_info[0] &
             IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G) && off + 4 <= caps.hecap_len) {
            memcpy(&ic->ic_he_mcs_nss_supp.rx_mcs_80p80, h + off, 4);
            off += 4;
        }
        memset(ic->ic_ppe_thres, 0, sizeof(ic->ic_ppe_thres));
        if (ic->ic_he_cap_elem.phy_cap_info[6] & IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT)
            memcpy(ic->ic_ppe_thres, h + off,
                   MIN((size_t)(caps.hecap_len - off), sizeof(ic->ic_ppe_thres)));
    }
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
    getMainCommandGate()->runAction(virt_radio_gated, this, NULL);
    return kIOReturnSuccess;
}

void ItlVirt::
virt_init_task(void *arg)
{
    ItlVirt *that = (ItlVirt *)arg;

    that->getMainCommandGate()->runAction(virt_radio_gated, that, (void *)1);
}

/* Radio on (arg1 set) or off, on the workloop gate. */
IOReturn ItlVirt::
virt_radio_gated(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3)
{
    ItlVirt *that = (ItlVirt *)arg0;
    struct _ifnet *ifp = &that->fIc.ic_if;

    if (arg1 != NULL) {
        if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) == IFF_UP)
            that->radioStart();
    } else if (ifp->if_flags & IFF_RUNNING)
        that->radioStop();
    return kIOReturnSuccess;
}

/*
 * The Airport layer changes the interface address at will: macOS uses a
 * private one per network.  Every path leaves it in ac_enaddr; not all of them
 * update ic_myaddr.  The firmware has to be on the air with the same address
 * and takes a new one only while not associated, so this runs before every
 * foreground scan and before authentication.
 */
void ItlVirt::
syncMac()
{
    struct ieee80211com *ic = &fIc;
    const uint8_t *want = ic->ic_ac.ac_enaddr;
    struct vap_set_mac sm;
    int rc;

    if (IEEE80211_ADDR_EQ(want, etheranyaddr))
        want = ic->ic_myaddr;
    if (!IEEE80211_ADDR_EQ(want, fFwMac)) {
        bzero(&sm, sizeof(sm));
        IEEE80211_ADDR_COPY(sm.mac, want);
        rc = request(VAP_CMD_SET_MAC, &sm, sizeof(sm));
        if (rc == 0)
            IEEE80211_ADDR_COPY(fFwMac, sm.mac);
        XYLog("vap: MAC %s %s (%d)\n", ether_sprintf(sm.mac), rc == 0 ? "set" : "refused", rc);
    }
    if (!IEEE80211_ADDR_EQ(ic->ic_myaddr, want))
        IEEE80211_ADDR_COPY(ic->ic_myaddr, want);
}

void ItlVirt::
setPort(bool authorized)
{
    struct vap_port pt = { authorized ? 1u : 0u, 0 };

    if (fPortAuthorized == authorized)
        return;
    fPortAuthorized = authorized;
    (void)request(VAP_CMD_PORT, &pt, sizeof(pt));
    XYLog("vap: port %s\n", authorized ? "authorized" : "closed");
}

/* Once a second while associated: what the Wi-Fi menu shows of the link. */
void ItlVirt::
virt_stats_timer(void *arg)
{
    ItlVirt *that = (ItlVirt *)arg;
    struct ieee80211com *ic = &that->fIc;
    struct vap_stats st;
    uint32_t n = sizeof(st);

    if (ic->ic_state != IEEE80211_S_RUN || !(ic->ic_if.if_flags & IFF_RUNNING))
        return;
    if (that->request(VAP_CMD_GET_STATS, NULL, 0, &st, &n) == 0 && n >= sizeof(st)) {
        that->fStats = st;
        that->fStatsValid = true;
        /* net80211 has the signal of the last frame; this is the link's own. */
        if (st.rssi_dbm != 0 && ic->ic_bss != NULL)
            ic->ic_bss->ni_rssi =
                (uint8_t)MAX(0, MIN(st.rssi_dbm + VIRT_RSSI_OFFSET, VIRT_RSSI_OFFSET));
    }
    timeout_add_msec(&that->fStatsTimer, VIRT_STATS_MS);
}

bool ItlVirt::
getTxRate(struct ItlTxRate *rate)
{
    const struct vap_rate r = fStats.tx_rate;

    if (!fStatsValid || r.kbps == 0)
        return false;
    rate->kbps = r.kbps;
    rate->mode = r.mode == VAP_RATE_LEGACY ? ITL_TX_RATE_LEGACY :
        r.mode == VAP_RATE_HT ? ITL_TX_RATE_HT :
        r.mode == VAP_RATE_VHT ? ITL_TX_RATE_VHT : ITL_TX_RATE_HE;
    rate->mcs = r.mcs;
    rate->nss = r.nss;
    rate->bw = MIN(r.bw_mhz_log2, 3);
    rate->sgi = r.gi == 1;
    return true;
}

/* The regulatory ceiling of the operating channel, else of the whole radio. */
bool ItlVirt::
getMaxTxPowerHalfDbm(uint8_t *halfDbm)
{
    const struct ieee80211_channel *c =
        fIc.ic_state == IEEE80211_S_RUN && fIc.ic_bss != NULL ? fIc.ic_bss->ni_chan : NULL;
    int dbm = 0;

    for (int i = 0; i < fCaps.nchans; i++) {
        if (c != NULL && c != IEEE80211_CHAN_ANYC && fCaps.chans[i].freq == c->ic_freq) {
            dbm = fCaps.chans[i].max_dbm;
            break;
        }
        dbm = MAX(dbm, (int)fCaps.chans[i].max_dbm);
    }
    if (dbm <= 0)
        return false;
    *halfDbm = (uint8_t)(dbm * 2);
    return true;
}

int ItlVirt::
radioStart()
{
    struct ieee80211com *ic = &fIc;
    struct _ifnet *ifp = &ic->ic_if;
    struct vap_radio on = { 1, 0 };
    int err;

    syncMac();
    err = request(VAP_CMD_RADIO, &on, sizeof(on));
    if (err) {
        XYLog("vap: radio on refused (%d)\n", err);
        return EIO;
    }
    XYLog("vap: radio on\n");
    ifq_clr_oactive(&ifp->if_snd);
    ifq_flush(&ifp->if_snd);
    ifp->if_flags |= IFF_RUNNING;

    /* The power-on scan is a census: it must not select and join a BSS. */
    __atomic_store_n(&ic->ic_initial_scan_census_only, 1, __ATOMIC_RELEASE);
    err = ieee80211_begin_scan_with_result(ifp);
    if (err) {
        XYLog("vap: initial scan rejected (%d)\n", err);
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
    struct vap_radio off = { 0, 0 };

    ifp->if_flags &= ~IFF_RUNNING;
    if (fTxHeld != NULL) {
        mbuf_freem(fTxHeld);
        ieee80211_release_node(ic, fTxHeldNi);
        fTxHeld = NULL;
        fTxHeldNi = NULL;
    }
    ifq_clr_oactive(&ifp->if_snd);
    ifq_flush(&ifp->if_snd);
    __atomic_store_n(&ic->ic_initial_scan_census_only, 0, __ATOMIC_RELEASE);
    scanReset();
    /* The firmware leaves the BSS and forgets its keys with the radio. */
    fKeys = 0;
    fPortAuthorized = false;
    timeout_del(&fStatsTimer);
    fStatsValid = false;
    /* Also ends whatever the firmware was scanning. */
    (void)request(VAP_CMD_RADIO, &off, sizeof(off));
    XYLog("vap: radio off\n");
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
    int err;

    if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
        return kIOReturnError;

    for (;;) {
        /* The frame the device had no room for goes first. */
        if ((m = that->fTxHeld) != NULL) {
            ni = that->fTxHeldNi;
            that->fTxHeld = NULL;
            goto sendit;
        }

        /* need to send management frames even if we're not RUNning */
        m = mq_dequeue(&ic->ic_mgtq);
        if (m) {
            ni = (struct ieee80211_node *)mbuf_pkthdr_rcvif(m);
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

    sendit:
        err = that->txFrame(m, ni);
        if (err == EBUSY)
            break;
        if (err) {
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

/*
 * Hands one 802.11 frame to the firmware.  Consumes m and the node reference,
 * except on EBUSY: then the frame is held and sent again from
 * virt_tx_ready().
 */
int ItlVirt::
txFrame(mbuf_t m, struct ieee80211_node *ni)
{
    struct ieee80211_frame *wh = mtod(m, struct ieee80211_frame *);
    struct vap_iov iov[VIRT_TX_MAX_SEGS];
    struct vap_tx_meta meta;
    unsigned n = 0;
    int rc = VAP_G_EINVAL;
    mbuf_t seg;

    bzero(&meta, sizeof(meta));
    if (ieee80211_has_qos(wh))
        meta.tid = ieee80211_get_qos(wh) & IEEE80211_QOS_TID;
    /* Frames cross in the clear: the firmware encrypts with the keys it holds. */
    wh->i_fc[1] &= ~IEEE80211_FC1_PROTECTED;
    for (seg = m; seg != NULL && n < nitems(iov); seg = mbuf_next(seg)) {
        if (mbuf_len(seg) == 0)
            continue;
        iov[n].p = mbuf_data(seg);
        iov[n].len = (uint32_t)mbuf_len(seg);
        n++;
    }
    /* vap_tx() copies the frame, so the mbuf is not needed after it. */
    if (seg == NULL)
        rc = vap_tx(fDev, &meta, iov, n);
    if (rc == VAP_G_BUSY) {
        fTxHeld = m;
        fTxHeldNi = ni;
        ifq_set_oactive(&fIc.ic_if.if_snd);
        return EBUSY;
    }
    if (rc != VAP_G_OK)
        XYLog("vap: frame of %u bytes in %u+ mbufs not sent (%d)\n",
              (unsigned)mbuf_pkthdr_len(m), n, rc);
    mbuf_freem(m);
    ieee80211_release_node(&fIc, ni);
    return rc == VAP_G_OK ? 0 : EIO;
}

/* Delivers one received 802.11 frame (no FCS, header contiguous in m) to net80211. */
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
    /* The firmware reorders aggregates itself: no reorder buffer here. */
    rxi.rxi_flags = IEEE80211_RXI_AMPDU_DONE;
    /*
     * Not clipped to ic_max_rssi: that is where net80211 puts its "good
     * signal" marks, and two strong access points must still compare by
     * their real levels when a roam target is picked.
     */
    rxi.rxi_rssi = MAX(0, MIN(rssiDbm + VIRT_RSSI_OFFSET, VIRT_RSSI_OFFSET));
    /* Zero means unknown: net80211 then assumes the current channel. */
    chan = ieee80211_mhz2ieee(freqMHz, 0);
    if (chan <= IEEE80211_CHAN_MAX && ic->ic_channels[chan].ic_freq == freqMHz)
        rxi.rxi_chan = chan;

    ni = ieee80211_find_rxnode(ic, wh);
    /*
     * The firmware delivers every frame in the clear, Protected bit off.
     * Once net80211 expects protection from this peer it drops such a frame
     * unless told that the firmware decrypted it (and checked the replay
     * counter).  Before that it would drop a frame so marked, and the
     * firmware has the key a moment earlier than net80211 switches over.
     */
    if ((flags & VIRT_RX_DECRYPTED) && (ni->ni_flags & IEEE80211_NODE_RXPROT))
        rxi.rxi_flags |= IEEE80211_RXI_HWDEC;
    ieee80211_inputm(ifp, m, ni, &rxi, &ml);
    ieee80211_release_node(ic, ni);
    if_input(ifp, &ml);
}

int ItlVirt::
virt_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
    ItlVirt *that = (ItlVirt *)ic->ic_softc;
    int err;

    /* Leaving the BSS closes the port; the keys go with the node. */
    if (ic->ic_state == IEEE80211_S_RUN && nstate != IEEE80211_S_RUN) {
        that->setPort(false);
        timeout_del(&that->fStatsTimer);
        that->fStatsValid = false;
    }

    /*
     * The firmware follows the association from the management frames it
     * relays, so no state needs a command of its own.
     */
    if (nstate != IEEE80211_S_SCAN) {
        that->scanAbort(0);
        /* The authentication frame must already carry the current address. */
        if (nstate == IEEE80211_S_AUTH)
            that->syncMac();
        err = that->fNewstate(ic, nstate, arg);
        if (err == 0 && nstate == IEEE80211_S_RUN && ic->ic_state == IEEE80211_S_RUN) {
            /* Without RSN nothing else opens the port. */
            if (!(ic->ic_flags & IEEE80211_F_RSNON))
                that->setPort(true);
            timeout_add_msec(&that->fStatsTimer, VIRT_STATS_MS);
        }
        return err;
    }

    /*
     * The firmware scans all channels by itself, while the default handler
     * would run net80211's own channel-by-channel scan.  Commit SCAN here
     * instead, the way iwx does.
     */
    if (ic->ic_state == IEEE80211_S_RUN) {
        ieee80211_stop_ampdu_tx(ic, ic->ic_bss, -1);
        ieee80211_ba_del(ic->ic_bss);
        /*
         * The firmware stays associated until a deauthentication frame
         * passes through it; net80211 sends none on this transition.
         */
        IEEE80211_SEND_MGMT(ic, ic->ic_bss, IEEE80211_FC0_SUBTYPE_DEAUTH,
                            IEEE80211_REASON_AUTH_LEAVE);
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

/* The bit of a data key in fKeys: the group keys by index, then the pairwise key. */
static uint8_t
virt_key_bit(const struct ieee80211_key *k)
{
    return (k->k_flags & IEEE80211_KEY_GROUP) ? (uint8_t)(1 << (k->k_id & 3)) : 0x80;
}

/* The data keys go to the firmware; a key it holds is never used in software. */
static bool
virt_key_is_firmware(const struct ieee80211_key *k)
{
    return (k->k_cipher == IEEE80211_CIPHER_CCMP || k->k_cipher == IEEE80211_CIPHER_TKIP) &&
        !(k->k_flags & (IEEE80211_KEY_IGTK | IEEE80211_KEY_SWCRYPTO));
}

int ItlVirt::
virt_set_key(struct ieee80211com *ic, struct ieee80211_node *ni,
             struct ieee80211_key *k)
{
    ItlVirt *that = (ItlVirt *)ic->ic_softc;
    struct vap_key vk;
    bool group;
    int rc;

    /* A live BIP slot belongs to its publisher in net80211, as in iwx. */
    if (k == NULL || ieee80211_bip_key_is_slot(ic, k))
        return EINVAL;
    group = (k->k_flags & IEEE80211_KEY_GROUP) != 0;
    // VAP-SEAM: transport hands the integrity group key to the firmware once
    // management frame protection is offered; BIP stays in net80211 until then.
    if (!virt_key_is_firmware(k) || k->k_len > sizeof(vk.key) || (!group && ni == NULL))
        return ieee80211_set_key(ic, ni, k);

    /* The key bytes are in the order nl80211 has them too, TKIP included. */
    bzero(&vk, sizeof(vk));
    vk.cipher = k->k_cipher == IEEE80211_CIPHER_CCMP ? 0x000fac04 : 0x000fac02;
    vk.idx = k->k_id;
    vk.len = (uint8_t)k->k_len;
    memcpy(vk.key, k->k_key, k->k_len);
    if (group) {
        vk.flags = VAP_KEY_GROUP | ((k->k_flags & IEEE80211_KEY_TX) ? VAP_KEY_TX : 0);
        for (int i = 0; i < 6; i++)
            vk.rsc[i] = (uint8_t)(k->k_rsc[0] >> (8 * i));
    } else {
        vk.flags = VAP_KEY_PAIRWISE;
        IEEE80211_ADDR_COPY(vk.peer, ni->ni_macaddr);
        /*
         * Whatever net80211 has queued, message 4 of the handshake above
         * all, must reach the firmware before the key does: a frame sent
         * after it goes out encrypted and the AP drops it.
         */
        that->getMainCommandGate()->runAction(virt_start_task, &ic->ic_if);
    }
    rc = that->request(VAP_CMD_KEY_ADD, &vk, sizeof(vk));
    explicit_bzero(&vk, sizeof(vk));
    XYLog("vap: %s key %u %s (%d)\n", group ? "group" : "pairwise", k->k_id,
          rc == 0 ? "installed" : "refused", rc);
    if (rc != 0)
        return EIO;
    that->fKeys |= virt_key_bit(k);
    /* The group key is the last one of a handshake: the link is fully keyed. */
    if (group)
        that->setPort(true);
    return 0;
}

void ItlVirt::
virt_delete_key(struct ieee80211com *ic, struct ieee80211_node *ni,
                struct ieee80211_key *k)
{
    ItlVirt *that = (ItlVirt *)ic->ic_softc;
    struct vap_key vk;

    if (k == NULL || ieee80211_bip_key_is_slot(ic, k))
        return;
    if (!virt_key_is_firmware(k)) {
        ieee80211_delete_key(ic, ni, k);
        return;
    }
    /* net80211 deletes more often than it sets: only what the firmware holds. */
    if (that->fKeys & virt_key_bit(k)) {
        bzero(&vk, sizeof(vk));
        vk.idx = k->k_id;
        vk.flags = (k->k_flags & IEEE80211_KEY_GROUP) ? VAP_KEY_GROUP : VAP_KEY_PAIRWISE;
        /* Fails harmlessly when the firmware has left the BSS already. */
        (void)that->request(VAP_CMD_KEY_DEL, &vk, sizeof(vk));
        that->fKeys &= ~virt_key_bit(k);
    }
    explicit_bzero(k, sizeof(*k));
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
    uint64_t wclGeneration = 0;
    IOInterruptState irq;
    uint32_t id, stale = 0;
    bool wcl;
    int err;

    if (!(ic->ic_if.if_flags & IFF_RUNNING))
        return ENETDOWN;
    /* Probe requests should carry the current address too. */
    if (!background)
        syncMac();

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
        wclGeneration = fWcl.generation;
        if (!background && !fWcl.silenced) {
            started.generation = fWcl.generation;
            started.backend_generation = id;
        }
    }
    IOSimpleLockUnlockEnableInterrupt(fScanLock, irq);
    if (wcl && background)
        __atomic_store_n(&ic->ic_wcl_scan_active, 1, __ATOMIC_RELEASE);

    err = scanCommand(id, background, wclGeneration);
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

/*
 * Starts scan `id` on the firmware: all frequencies, with the SSID and the
 * scan type of the WCL request if it is one.  The results arrive as received
 * frames, then VAP_EVT_SCAN_DONE ends the scan.
 */
int ItlVirt::
scanCommand(uint32_t id, bool background, uint64_t wclGeneration)
{
    struct {
        struct vap_scan_start sc;
        struct vap_ssid ssid;
        uint8_t pad[4];             /* messages are padded to 8 bytes */
    } req;
    struct ieee80211_wcl_scan_plan plan;
    uint32_t len = sizeof(req.sc);
    IOInterruptState irq;
    bool waited = false;
    int rc;

    bzero(&req, sizeof(req));
    req.sc.scan_id = id;
    if (background)
        req.sc.flags |= VAP_SCAN_BACKGROUND;
    if (wclGeneration != 0 && ieee80211_wcl_scan_plan_snapshot(&fIc, &plan) &&
        plan.generation == wclGeneration) {
        if (plan.scan_type == IEEE80211_WCL_SCAN_TYPE_PASSIVE)
            req.sc.flags |= VAP_SCAN_PASSIVE;
        if (plan.ssid_len != 0 && plan.ssid_len <= sizeof(req.ssid.ssid)) {
            req.sc.n_ssids = 1;
            req.ssid.len = plan.ssid_len;
            memcpy(req.ssid.ssid, plan.ssid, plan.ssid_len);
            len = sizeof(req);
        }
    }

    rc = request(VAP_CMD_SCAN_START, &req, len);
    /*
     * Busy: the firmware is still ending the previous scan or runs one of
     * its own.  The scan stays accepted and is started when that one is over.
     */
    irq = IOSimpleLockLockDisableInterrupt(fScanLock);
    if (fScan.id == id) {
        waited = fScan.deferred;
        fScan.deferred = rc == VAP_E_BUSY;
    }
    IOSimpleLockUnlockEnableInterrupt(fScanLock, irq);
    if (rc == VAP_E_BUSY) {
        if (!waited)
            XYLog("vap: scan %u waits for the firmware to end another\n", id);
        timeout_add_msec(&fScanWatchdog, VIRT_SCAN_RETRY_MS);
        return 0;
    }
    if (rc != 0) {
        XYLog("vap: scan %u refused (%d)\n", id, rc);
        return rc == VAP_E_NOT_READY ? ENETDOWN : EIO;
    }
    XYLog("vap: scan %u started%s%s\n", id, background ? ", background" : "",
          wclGeneration != 0 ? ", WCL" : "");
    timeout_add_msec(&fScanWatchdog, VIRT_SCAN_WATCHDOG_MS);
    return 0;
}

/* Starts the scan the firmware was too busy for; gives up after VIRT_SCAN_WATCHDOG_MS. */
void ItlVirt::
scanRetry()
{
    uint64_t wclGeneration = 0;
    uint32_t id = 0, tries = 0;
    bool background = false;

    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(fScanLock);
    if (fScan.state != VIRT_SCAN_IDLE && fScan.deferred) {
        id = fScan.id;
        tries = ++fScan.tries;
        background = fScan.state == VIRT_SCAN_BACKGROUND;
        wclGeneration = fScan.wcl ? fWcl.generation : 0;
    }
    IOSimpleLockUnlockEnableInterrupt(fScanLock, irq);
    if (id == 0)
        return;
    if (tries > VIRT_SCAN_WATCHDOG_MS / VIRT_SCAN_RETRY_MS ||
        scanCommand(id, background, wclGeneration) != 0) {
        XYLog("vap: scan %u never started, ending it\n", id);
        scanEnd(false, id);
    }
}

/* No SCAN_DONE came: end the scan with what has arrived instead of hanging on it. */
void ItlVirt::
virt_scan_watchdog(void *arg)
{
    ItlVirt *that = (ItlVirt *)arg;
    struct vap_scan_abort ab = { 0, 0 };
    bool deferred;

    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(that->fScanLock);
    ab.scan_id = that->fScan.id;
    deferred = that->fScan.deferred;
    IOSimpleLockUnlockEnableInterrupt(that->fScanLock, irq);
    if (ab.scan_id == 0)
        return;
    if (deferred) {
        that->scanRetry();
        return;
    }
    XYLog("vap: scan %u: no SCAN_DONE in %u ms, ending it\n", ab.scan_id,
          VIRT_SCAN_WATCHDOG_MS);
    (void)that->request(VAP_CMD_SCAN_ABORT, &ab, sizeof(ab));
    that->scanEnd(false, ab.scan_id);
}

/* Stops scan `id` (0: whichever is running) and retires it as aborted. */
void ItlVirt::
scanAbort(uint32_t id)
{
    struct vap_scan_abort ab = { 0, 0 };
    bool deferred = false;

    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(fScanLock);
    if (fScan.state != VIRT_SCAN_IDLE && (id == 0 || fScan.id == id)) {
        ab.scan_id = fScan.id;
        deferred = fScan.deferred;
    }
    IOSimpleLockUnlockEnableInterrupt(fScanLock, irq);
    if (ab.scan_id == 0)
        return;
    /*
     * The firmware's own SCAN_DONE for it finds the scan retired and is
     * ignored.  A scan the firmware has not started yet is not its to abort.
     */
    if (!deferred)
        (void)request(VAP_CMD_SCAN_ABORT, &ab, sizeof(ab));
    scanEnd(true, ab.scan_id);
}

/*
 * The end of scan `id`: from the firmware's SCAN_DONE (the scan's beacons and
 * probe responses have gone through rxFrame() before), from an abort, or
 * from the watchdog.
 */
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
    timeout_del(&fScanWatchdog);
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
 * The radio is being switched off and the firmware drops its scan.  That is
 * not a scan terminal: a WCL request is told that it will never get one.
 */
void ItlVirt::
scanReset()
{
    struct ieee80211com *ic = &fIc;
    VirtWclTicket ticket;

    timeout_del(&fScanWatchdog);
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
