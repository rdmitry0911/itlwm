/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "ItlVirt.hpp"

#include <libkern/OSByteOrder.h>
#include <IOKit/network/IONetworkController.h>
#include <net/ethernet.h>
#include <sys/_if_media.h>
#include <sys/_ifq.h>
#include <net80211/ieee80211_priv.h>
#include <net80211/ieee80211_sae_engine.h>
#include <net80211/ieee80211_sae_policy.h>

#if __IO80211_TARGET >= __MAC_26_0
extern "C" void airportItlwmRequestAPTxDequeue(IOEthernetController *controller);
#endif

#define super ItlHalService
OSDefineMetaClassAndStructors(ItlVirt, ItlHalService)

/*
 * The HAL object is found from its softc with container_of(), here and in
 * the SAE glue included at the end, the way the Intel HALs do it.  clang
 * objects to offsetof() on a class with virtual functions.
 */
#pragma clang diagnostic ignored "-Winvalid-offsetof"

/* ic_softc and if_softc are the softc: the SAE glue reads them as that. */
static inline ItlVirt *
virt_hal(void *softc)
{
    return container_of((struct virt_softc *)softc, ItlVirt, com);
}

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
    caps->fw_features = hs.fw_features;
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
        struct ieee80211com *ic = &that->com.sc_ic;

        XYLog("vap: disconnected from %s, reason %u%s\n", ether_sprintf((u_char *)e->bssid),
              e->reason, e->by_ap ? ", by the AP" : "");
        /* Normally the deauthentication frame came first and did this already. */
        if (ic->ic_state == IEEE80211_S_RUN && ic->ic_bss != NULL &&
            IEEE80211_ADDR_EQ(ic->ic_bss->ni_bssid, e->bssid))
            ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
        return;
    }
    if (h->type == VAP_EVT_AP_STARTED && len >= sizeof(struct vap_ap_started)) {
        that->apStarted((const struct vap_ap_started *)payload);
        return;
    }
    if (h->type == VAP_EVT_AP_STOPPED && len >= sizeof(struct vap_evt_ap_stopped)) {
        const struct vap_evt_ap_stopped *e = (const struct vap_evt_ap_stopped *)payload;

        /*
         * The firmware lost the access point, or could not start it.  The
         * Airport layer sees the channel go to zero and decides what to do.
         */
        XYLog("vap: AP stopped by the firmware (%d)\n", e->reason);
        that->apReset(true);
        return;
    }
    if (h->type == VAP_EVT_AP_STA && len >= sizeof(struct vap_evt_ap_sta)) {
        const struct vap_evt_ap_sta *e = (const struct vap_evt_ap_sta *)payload;
        const uint16_t ieLen = len >= sizeof(*e) + e->ie_len ? e->ie_len : 0;

        that->apStation(e->sta, e->connected != 0, (const uint8_t *)&e[1], ieLen);
        return;
    }
    XYLog("vap: event 0x%x, %u bytes\n", h->type, len);
}

void ItlVirt::
virt_rx(void *ctx, const struct vap_rx_meta *meta, const uint8_t *frame, uint32_t len)
{
    ItlVirt *that = (ItlVirt *)ctx;
    mbuf_t m;

    if (meta->vif == VAP_VIF_AP) {
        that->apInput(frame, len);
        return;
    }
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
    struct _ifnet *ifp = &that->com.sc_ic.ic_if;

    ifq_clr_oactive(&ifp->if_snd);
    virt_start_task(NULL, ifp, NULL, NULL, NULL);
    /* The access point's queue was held back by the same lack of room. */
    that->apTxKick();
}

/* ---- access point --------------------------------------------------------- */

static uint16_t
virt_ap_chan2freq(uint16_t chan)
{
    if (chan == 14)
        return 2484;
    if (chan >= 1 && chan <= 13)
        return 2407 + 5 * chan;
    if (chan >= 32 && chan <= 177)
        return 5000 + 5 * chan;
    return 0;
}

static uint16_t
virt_ap_freq2chan(uint16_t freq)
{
    if (freq == 2484)
        return 14;
    if (freq >= 2412 && freq <= 2472)
        return (freq - 2407) / 5;
    if (freq >= 5160 && freq <= 5885)
        return (freq - 5000) / 5;
    return 0;
}

/*
 * What the access point's RSN element asks for: the group cipher, the first
 * pairwise cipher (suite selectors, OUI first) and management frame
 * protection (0 off, 1 optional, 2 required).  Leaves alone what the
 * element does not say.
 */
static void
virt_ap_parse_rsn(const uint8_t *ie, size_t len, uint32_t *group, uint32_t *pairwise, uint8_t *mfp)
{
    size_t off, n;

    if (ie == NULL || len < 8 || ie[0] != IEEE80211_ELEMID_RSN || (size_t)ie[1] + 2 > len)
        return;
    len = (size_t)ie[1] + 2;
    *group = (uint32_t)ie[4] << 24 | (uint32_t)ie[5] << 16 | (uint32_t)ie[6] << 8 | ie[7];
    off = 8;
    if (off + 2 > len)
        return;
    n = ie[off] | (size_t)ie[off + 1] << 8;
    off += 2;
    if (n == 0 || off + 4 * n > len)
        return;
    *pairwise = (uint32_t)ie[off] << 24 | (uint32_t)ie[off + 1] << 16 |
        (uint32_t)ie[off + 2] << 8 | ie[off + 3];
    off += 4 * n;
    if (off + 2 > len)
        return;
    n = ie[off] | (size_t)ie[off + 1] << 8;        /* AKM suites: startAPMode() has them from authUpper */
    off += 2 + 4 * n;
    if (off + 2 > len)
        return;
    /* RSN capabilities: bit 6 "required", bit 7 "capable". */
    *mfp = (ie[off] & 0x40) ? 2 : (ie[off] & 0x80) ? 1 : 0;
}

/*
 * The vendor elements of the beacon the Airport layer built, for the
 * firmware to add to its own beacons and probe responses.  Microsoft's (WPA,
 * WMM, WPS) are the firmware's business.
 */
static uint16_t
virt_ap_vendor_ies(const uint8_t *beacon, size_t len, uint8_t *out, size_t cap)
{
    static const uint8_t microsoft[3] = { 0x00, 0x50, 0xf2 };
    const size_t fixed = sizeof(struct ieee80211_frame) + 12;  /* timestamp, interval, capabilities */
    size_t off, used = 0;

    if (beacon == NULL || len <= fixed)
        return 0;
    for (off = fixed; off + 2 <= len && off + 2 + beacon[off + 1] <= len; off += 2 + beacon[off + 1]) {
        const size_t el = 2 + (size_t)beacon[off + 1];

        if (beacon[off] != IEEE80211_ELEMID_VENDOR || el < 5 ||
            memcmp(&beacon[off + 2], microsoft, sizeof(microsoft)) == 0)
            continue;
        if (used + el > cap)
            break;
        memcpy(out + used, &beacon[off], el);
        used += el;
    }
    return (uint16_t)used;
}

/* Sends the AP_START kept in fApStart.  Returns the firmware's status or VAP_G_*. */
int ItlVirt::
apStartCommand()
{
    const struct vap_ap_start *a = (const struct vap_ap_start *)fApStart;
    struct vap_ap_started st;
    uint32_t n = sizeof(st);
    int rc;

    bzero(&st, sizeof(st));
    rc = request(VAP_CMD_AP_START, fApStart, fApStartLen, &st, &n);
    if (rc == 0 && n < sizeof(st))
        rc = VAP_G_EIO;
    if (rc != 0) {
        XYLog("vap: AP start refused (%d)\n", rc);
        fApState = VIRT_AP_OFF;
        fApChannel = 0;
        return rc;
    }
    /*
     * Accepted: the firmware answers with the address and the channel it
     * will use (next to an associated station that is the station's
     * channel) and reports later that it beacons, or that it could not.
     */
    fApState = VIRT_AP_STARTING;
    fApChannel = virt_ap_freq2chan(st.freq_mhz);
    memcpy(fApBssid, st.bssid, sizeof(fApBssid));
    XYLog("vap: AP start accepted: %s, %u MHz (asked for %u), akm 0x%x, mfp %u, %u vendor IE bytes%s\n",
          ether_sprintf(fApBssid), st.freq_mhz, a->freq_mhz, a->akm, a->mfp, a->ie_len,
          (a->flags & VAP_AP_HIDDEN) ? ", hidden" : "");
    return 0;
}

IOReturn ItlVirt::
startAPMode(const struct ItlHalApConfig *config)
{
    struct vap_ap_start *a = (struct vap_ap_start *)fApStart;
    int rc;

    if (!supportsAPMode())
        return kIOReturnUnsupported;
    if (config == NULL || config->ssid == NULL || config->ssidLength == 0 ||
        config->ssidLength > sizeof(a->ssid))
        return kIOReturnBadArgument;
    /* authUpper as the Airport layer hands it over: open, WPA2-PSK, WPA3-SAE, both. */
    if (config->authUpper != 0 && config->authUpper != 0x8 && config->authUpper != 0x1000 &&
        config->authUpper != 0x1008)
        return kIOReturnUnsupported;
    if (config->authUpper != 0 &&
        (config->credential == NULL || config->credentialLength < 8 ||
         config->credentialLength > sizeof(a->cred)))
        return kIOReturnBadArgument;
    if (virt_ap_chan2freq(config->channel) == 0)
        return kIOReturnBadArgument;
    /* A start over an access point which is up replaces it. */
    if (fApState != VIRT_AP_OFF)
        (void)stopAPMode();

    explicit_bzero(fApStart, sizeof(fApStart));
    memcpy(a->bssid, config->bssid, sizeof(a->bssid));
    a->freq_mhz = virt_ap_chan2freq(config->channel);
    a->beacon_int = config->beaconInterval;
    a->dtim = config->dtimPeriod;
    a->max_sta = fApMaxSta != 0 ? fApMaxSta :
        (uint8_t)(config->maxStations > 255 ? 255 : config->maxStations);
    a->pairwise_cipher = a->group_cipher = 0x000fac04;      /* CCMP */
    if (config->authUpper != 0) {
        /* WPA2 Personal 0x8, WPA3 Personal 0x1000, transition both */
        a->akm = (config->authUpper & 0x1000) ? VAP_AP_AKM_SAE : 0;
        if ((config->authUpper & 0x8) || a->akm == 0)
            a->akm |= VAP_AP_AKM_PSK;
        a->mfp = a->akm == VAP_AP_AKM_SAE ? 2 : 0;
        virt_ap_parse_rsn(config->rsnIE, config->rsnIELength, &a->group_cipher,
                          &a->pairwise_cipher, &a->mfp);
        a->cred_len = (uint8_t)config->credentialLength;
        memcpy(a->cred, config->credential, config->credentialLength);
    }
    a->flags = fApHidden ? VAP_AP_HIDDEN : 0;
    a->ssid_len = (uint8_t)config->ssidLength;
    memcpy(a->ssid, config->ssid, config->ssidLength);
    a->ie_len = virt_ap_vendor_ies((const uint8_t *)config->beaconTemplate,
                                   config->beaconTemplateLength,
                                   fApStart + sizeof(*a), VIRT_AP_IE_MAX);
    fApStartLen = sizeof(*a) + a->ie_len;

    rc = apStartCommand();
    if (rc != 0) {
        explicit_bzero(fApStart, sizeof(fApStart));
        fApStartLen = 0;
    }
    return rc == 0 ? kIOReturnSuccess :
        rc == VAP_E_UNSUPPORTED ? kIOReturnUnsupported :
        rc == VAP_E_BUSY ? kIOReturnBusy :
        rc == VAP_E_INVAL ? kIOReturnBadArgument : kIOReturnError;
}

IOReturn ItlVirt::
stopAPMode()
{
    int rc;

    if (fApState == VIRT_AP_OFF)
        return kIOReturnSuccess;
    rc = request(VAP_CMD_AP_STOP, NULL, 0);
    XYLog("vap: AP stopped (%d)\n", rc);
    apReset(false);
    explicit_bzero(fApStart, sizeof(fApStart));
    fApStartLen = 0;
    return kIOReturnSuccess;
}

/* The access point is gone.  tellStations: its stations were not told to leave. */
void ItlVirt::
apReset(bool tellStations)
{
    static const uint8_t none[IEEE80211_ADDR_LEN] = { 0 };
    unsigned i;

    fApState = VIRT_AP_OFF;
    fApChannel = 0;
    for (i = 0; i < VIRT_AP_MAX_STA; i++) {
        if (memcmp(fApSta[i], none, sizeof(none)) == 0)
            continue;
        if (tellStations) {
            struct ieee80211_node witness;

            bzero(&witness, sizeof(witness));
            IEEE80211_ADDR_COPY(witness.ni_macaddr, fApSta[i]);
            ieee80211_apsta_event_publish(&com.sc_ic, &witness, IEEE80211_APSTA_EVENT_LEAVE);
        }
        bzero(fApSta[i], sizeof(fApSta[i]));
    }
}

void ItlVirt::
apStarted(const struct vap_ap_started *st)
{
    if (fApState == VIRT_AP_OFF)
        return;
    fApState = VIRT_AP_ON;
    fApChannel = virt_ap_freq2chan(st->freq_mhz);
    memcpy(fApBssid, st->bssid, sizeof(fApBssid));
    XYLog("vap: AP up: %s, %u MHz\n", ether_sprintf(fApBssid), st->freq_mhz);
    apTxKick();
}

/*
 * A station has completed the key handshake with the firmware's
 * authenticator, or has left.  The Airport layer hears of it the way it does
 * from the other HALs: a witness node with the address and, when the
 * firmware has them, the elements of the association request.
 */
void ItlVirt::
apStation(const uint8_t *sta, bool connected, const uint8_t *ies, uint16_t ieLen)
{
    static const uint8_t none[IEEE80211_ADDR_LEN] = { 0 };
    struct ieee80211_node witness;
    int slot = -1, vacant = -1;
    unsigned i;

    if (fApState == VIRT_AP_OFF || memcmp(sta, none, sizeof(none)) == 0)
        return;
    for (i = 0; i < VIRT_AP_MAX_STA; i++) {
        if (memcmp(fApSta[i], sta, IEEE80211_ADDR_LEN) == 0)
            slot = (int)i;
        else if (vacant < 0 && memcmp(fApSta[i], none, sizeof(none)) == 0)
            vacant = (int)i;
    }
    XYLog("vap: AP station %s %s\n", ether_sprintf((u_char *)sta),
          connected ? (slot >= 0 ? "joined again" : "joined") : "left");
    if (!connected && slot < 0)
        return;
    bzero(&witness, sizeof(witness));
    IEEE80211_ADDR_COPY(witness.ni_macaddr, sta);
    if (connected) {
        if (slot < 0 && vacant >= 0)
            memcpy(fApSta[vacant], sta, IEEE80211_ADDR_LEN);
        if (ieLen != 0) {
            witness.ni_rsnie_tlv = const_cast<uint8_t *>(ies);
            witness.ni_rsnie_tlv_len = ieLen;
        }
        ieee80211_apsta_event_publish(&com.sc_ic, &witness,
            slot >= 0 ? IEEE80211_APSTA_EVENT_REASSOC : IEEE80211_APSTA_EVENT_ASSOC);
    } else {
        bzero(fApSta[slot], sizeof(fApSta[slot]));
        ieee80211_apsta_event_publish(&com.sc_ic, &witness, IEEE80211_APSTA_EVENT_LEAVE);
    }
}

/* One 802.3 frame from a station of the access point. */
void ItlVirt::
apInput(const uint8_t *frame, uint32_t len)
{
    struct mbuf_list ml = MBUF_LIST_INITIALIZER();
    mbuf_t m;

    if (fApState != VIRT_AP_ON || len < sizeof(struct ether_header) ||
        (m = getController()->allocatePacket(len)) == NULL)
        return;
    if (mbuf_copyback(m, 0, len, frame, MBUF_DONTWAIT) != 0) {
        mbuf_freem(m);
        return;
    }
    ml_enqueue(&ml, m);
    if_input_ap(&com.sc_ic.ic_if, &ml);
}

/*
 * The access point's outgoing packets come with the stack's doorbell: the
 * queue set notification calls IOSkywalkTxSubmissionQueue::packetSubmission
 * (true) on each submission queue (25C56 nxp_tx_qset_notify
 * 0xffffff8002a06449), which runs the Airport layer's action synchronously
 * (legacyDequeue 0xffffff8002a32c26).  A queue stops taking doorbells only
 * when the action took nothing (no room here: legacyDequeue sets the stall
 * bit, 0xffffff8002a32c7f) or was asked before the access point was up, and
 * only checkForWork, through requestDequeue, clears that.  The reference
 * restarts on its transmit completions (AppleBCMWLAN txCompRingDrain); this
 * transport says when it has room again (virt_tx_ready), and the access
 * point's start clears what was refused before it.
 */
void ItlVirt::
apTxKick()
{
#if __IO80211_TARGET >= __MAC_26_0
    if (fApState == VIRT_AP_ON)
        airportItlwmRequestAPTxDequeue(getController());
#endif
}

struct VirtApTx {
    mbuf_t  m;
    int     rc;
};

IOReturn ItlVirt::
virt_ap_tx_gated(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3)
{
    ItlVirt *that = (ItlVirt *)arg0;
    VirtApTx *tx = (VirtApTx *)arg1;
    struct vap_iov iov[VIRT_TX_MAX_SEGS];
    struct vap_tx_meta meta;
    unsigned n = 0;
    mbuf_t seg;

    tx->rc = VAP_G_EINVAL;
    if (that->fApState != VIRT_AP_ON) {
        tx->rc = VAP_E_NOT_READY;
        return kIOReturnSuccess;
    }
    bzero(&meta, sizeof(meta));
    meta.vif = VAP_VIF_AP;
    for (seg = tx->m; seg != NULL && n < nitems(iov); seg = mbuf_next(seg)) {
        if (mbuf_len(seg) == 0)
            continue;
        iov[n].p = mbuf_data(seg);
        iov[n].len = (uint32_t)mbuf_len(seg);
        n++;
    }
    /* vap_tx() copies the frame. */
    if (seg == NULL && n != 0)
        tx->rc = vap_tx(that->fDev, &meta, iov, n);
    return kIOReturnSuccess;
}

/* The mbuf is ours only when this returns success. */
IOReturn ItlVirt::
transmitAPData(mbuf_t packet)
{
    VirtApTx tx = { packet, VAP_G_EINVAL };

    if (packet == NULL || mbuf_pkthdr_len(packet) < sizeof(struct ether_header))
        return kIOReturnBadArgument;
    getMainCommandGate()->runAction(virt_ap_tx_gated, this, &tx);
    if (tx.rc == VAP_G_OK) {
        mbuf_freem(packet);
        return kIOReturnSuccess;
    }
    /* No room: virt_tx_ready() asks for the queue again. */
    return tx.rc == VAP_G_BUSY ? kIOReturnNoResources :
        tx.rc == VAP_E_NOT_READY ? kIOReturnNotReady : kIOReturnError;
}

/*
 * How many packets the Airport layer may take off the access point's queue
 * at once.  The transport counts its free slots lazily (it collects finished
 * ones when it runs out), so that count is no measure: a batch is offered
 * and vap_tx() says when there really is no room, with tx_ready() to follow.
 */
uint32_t ItlVirt::
getAPTxFreeSpace() const
{
    return fApState == VIRT_AP_ON ? VAP_G_TX_SLOTS / 2 : 0;
}

IOReturn ItlVirt::
setAPMaxStations(uint32_t maxStations)
{
    /* Part of the access point's configuration: taken at its next start. */
    fApMaxSta = (uint8_t)(maxStations > 255 ? 255 : maxStations);
    return kIOReturnSuccess;
}

IOReturn ItlVirt::
setAPHidden(bool hidden)
{
    struct vap_ap_start *a = (struct vap_ap_start *)fApStart;

    if (fApHidden == hidden)
        return kIOReturnSuccess;
    fApHidden = hidden;
    if (fApState == VIRT_AP_OFF || fApStartLen == 0)
        return kIOReturnSuccess;
    /* Hidden or not is how the firmware's access point is configured: start it again. */
    (void)request(VAP_CMD_AP_STOP, NULL, 0);
    apReset(true);
    a->flags = hidden ? (a->flags | VAP_AP_HIDDEN) : (a->flags & ~VAP_AP_HIDDEN);
    return apStartCommand() == 0 ? kIOReturnSuccess : kIOReturnError;
}

uint16_t ItlVirt::
getAPSTARequiredSharedChannel() const
{
    struct ieee80211com *ic = const_cast<struct ieee80211com *>(&com.sc_ic);

    /* Asked before the access point starts as well: the station's channel first. */
    if (ic->ic_opmode == IEEE80211_M_STA && ic->ic_state == IEEE80211_S_RUN &&
        ic->ic_bss != NULL && ic->ic_bss->ni_chan != NULL &&
        ic->ic_bss->ni_chan != IEEE80211_CHAN_ANYC)
        return (uint16_t)ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);
    return fApChannel;
}

IOReturn ItlVirt::
sendAPStationCommand(const struct ItlHalApStationCommand *command)
{
    struct vap_ap_sta_deauth d;
    int rc;

    if (fApState != VIRT_AP_ON)
        return kIOReturnNotReady;
    if (command == NULL || command->station == NULL)
        return kIOReturnBadArgument;
    switch (command->command) {
    case kItlHalApStationAuthorize:
    case kItlHalApStationUnauthorize:
        /* The firmware's authenticator opens and closes the port itself. */
        return kIOReturnSuccess;
    case kItlHalApStationDisassociate:
        bzero(&d, sizeof(d));
        memcpy(d.sta, command->station, sizeof(d.sta));
        d.reason = command->disassocReason != 0 && command->disassocReason <= 0xffff ?
            (uint16_t)command->disassocReason : IEEE80211_REASON_AUTH_LEAVE;
        rc = request(VAP_CMD_AP_STA_DEAUTH, &d, sizeof(d));
        XYLog("vap: AP station %s told to leave, reason %u (%d)\n",
              ether_sprintf((u_char *)d.sta), d.reason, rc);
        return rc == 0 ? kIOReturnSuccess : kIOReturnError;
    default:
        return kIOReturnUnsupported;
    }
}

/* The Airport layer limits what it takes off the access point's queue by this. */
extern "C" bool
airportItlwmQueryVirtAPTxFreeSpace(ItlHalService *service, uint32_t *freeSpace)
{
    ItlVirt *that = OSDynamicCast(ItlVirt, service);

    if (that == NULL || freeSpace == NULL)
        return false;
    *freeSpace = that->getAPTxFreeSpace();
    return true;
}

bool ItlVirt::
attach(IOPCIDevice *device)
{
    struct ieee80211com *ic = &com.sc_ic;
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

    ifp->if_softc = &com;
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
    if (ic->ic_caps & IEEE80211_C_MFP) {
        ic->ic_pae_mfp_txn_submit = virt_pae_txn_submit;
        ic->ic_pae_mfp_txn_cancel = virt_pae_txn_cancel;
        ic->ic_pae_mfp_txn_finish = virt_pae_txn_finish;
    }
    ic->ic_ampdu_rx_start = virt_ampdu_start;
    ic->ic_ampdu_rx_stop = virt_ampdu_stop;
    ic->ic_ampdu_tx_start = virt_ampdu_start;
    ic->ic_ampdu_tx_stop = virt_ampdu_stop;
    /* Override 802.11 state transition machine. */
    fNewstate = ic->ic_newstate;
    ic->ic_newstate = virt_newstate;
    if (!saeAttach()) {
        XYLog("vap: can't set up SAE\n");
        goto fail;
    }
    XYLog("vap: attached\n");
    return true;

fail:
    detach(device);
    return false;
}

void ItlVirt::
detach(IOPCIDevice *device)
{
    struct _ifnet *ifp = &com.sc_ic.ic_if;

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
    /* No frame arrives any more: the SAE worker can be waited for. */
    saeDetach();
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
    if (ieee80211_bip_lifetime_drain(&com.sc_ic) != 0)
        panic("ItlVirt::free BIP lifetime");
    /* ieee80211_ifdetach() leaves this lock to the HAL's final release. */
    ieee80211_pae_selected_bss_lock_destroy(&com.sc_ic);
    /* Kept until here like fDev: a late SAE call finds a closed gate, not a freed lock. */
    if (com.sc_sae_wcl_credential_lock != NULL)
        IOSimpleLockFree(com.sc_sae_wcl_credential_lock);
    if (com.sc_sae_engine_lock != NULL)
        IOSimpleLockFree(com.sc_sae_engine_lock);
    if (com.sc_sae_tx_lock != NULL)
        IOSimpleLockFree(com.sc_sae_tx_lock);
    if (com.sc_task_gate_lock != NULL)
        IOLockFree(com.sc_task_gate_lock);
    super::free();
}

/*
 * Must run before ieee80211_ifattach(): it and ieee80211_media_init() derive
 * the interface address, the channel set and the PHY modes from these fields.
 */
void ItlVirt::
applyCaps(const VirtCaps &caps)
{
    struct ieee80211com *ic = &com.sc_ic;
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
    /*
     * Management frame protection is the firmware's work once it has the
     * keys.  net80211 negotiates it only for an association which asks for
     * it, which today is a WPA3-SAE one.
     */
    if (caps.fw_features & VAP_FEAT_PMF)
        ic->ic_caps |= IEEE80211_C_MFP;
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
    struct _ifnet *ifp = &com.sc_ic.ic_if;

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
    struct _ifnet *ifp = &com.sc_ic.ic_if;

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
    struct _ifnet *ifp = &that->com.sc_ic.ic_if;

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
    struct ieee80211com *ic = &com.sc_ic;
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
    struct ieee80211com *ic = &that->com.sc_ic;
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
    const struct ieee80211com *ic = &com.sc_ic;
    const struct ieee80211_channel *c =
        ic->ic_state == IEEE80211_S_RUN && ic->ic_bss != NULL ? ic->ic_bss->ni_chan : NULL;
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
    struct ieee80211com *ic = &com.sc_ic;
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
    /* Publishes the SAE hooks in net80211; radioStop() withdraws them. */
    iwx_sae_engine_reopen(&com);

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
    struct ieee80211com *ic = &com.sc_ic;
    struct _ifnet *ifp = &ic->ic_if;
    struct vap_radio off = { 0, 0 };

    ifp->if_flags &= ~IFF_RUNNING;
    /*
     * An SAE exchange in progress ends here and a password not yet used is
     * dropped.  Neither call waits: the exchange is retired by its worker.
     */
    iwx_sae_engine_stop_begin(&com);
    iwx_sae_wcl_stop_begin(&com);
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
    /* The firmware's access point goes with the radio; its owner starts it again. */
    if (fApState != VIRT_AP_OFF) {
        (void)request(VAP_CMD_AP_STOP, NULL, 0);
        apReset(true);
    }
    /* The firmware leaves the BSS and forgets its keys with the radio. */
    fKeys = 0;
    fPaeTxn = 0;
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
    ItlVirt *that = virt_hal(ifp->if_softc);
    struct ieee80211com *ic = &that->com.sc_ic;
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
            if (!that->saeAdmitAssocReq(m, ni)) {
                mbuf_freem(m);
                ieee80211_release_node(ic, ni);
                ifp->netStat->outputErrors++;
                continue;
            }
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
    ItlVirt *that = virt_hal(ifp->if_softc);

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
        ifq_set_oactive(&com.sc_ic.ic_if.if_snd);
        return EBUSY;
    }
    if (rc != VAP_G_OK)
        XYLog("vap: frame of %u bytes in %u+ mbufs not sent (%d)\n",
              (unsigned)mbuf_pkthdr_len(m), n, rc);
    mbuf_freem(m);
    ieee80211_release_node(&com.sc_ic, ni);
    return rc == VAP_G_OK ? 0 : EIO;
}

/* The management frames 802.11w protects: deauthentication, disassociation, action. */
static bool
virt_is_robust_mgmt(const struct ieee80211_frame *wh)
{
    const uint8_t subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;

    return (wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) == IEEE80211_FC0_TYPE_MGT &&
        (subtype == IEEE80211_FC0_SUBTYPE_DEAUTH || subtype == IEEE80211_FC0_SUBTYPE_DISASSOC ||
         subtype == IEEE80211_FC0_SUBTYPE_ACTION);
}

/* Delivers one received 802.11 frame (no FCS, header contiguous in m) to net80211. */
void ItlVirt::
rxFrame(mbuf_t m, uint16_t freqMHz, int8_t rssiDbm, uint32_t flags)
{
    struct ieee80211com *ic = &com.sc_ic;
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
    /*
     * The same for management frame protection.  The host kernel checks the
     * frames which must be protected and the firmware relays only those it
     * accepted, in the clear and unmarked.  A unicast one is told to
     * net80211 as verified by the firmware: it has nothing left to strip.
     * A group-addressed one cannot be: net80211 wants the MIC element on
     * it, which the host has removed, and drops it; the firmware reports
     * that disconnection with VAP_EVT_DISCONNECTED as well.
     */
    if ((ni->ni_flags & IEEE80211_NODE_RXMGMTPROT) && virt_is_robust_mgmt(wh) &&
        !IEEE80211_IS_MULTICAST(wh->i_addr1))
        rxi.rxi_flags |= IEEE80211_RXI_HWDEC;
    ieee80211_inputm(ifp, m, ni, &rxi, &ml);
    /* The frame brought keys: see virt_pae_txn_submit(). */
    while (fPaeTxn != 0) {
        const uint64_t id = fPaeTxn;

        fPaeTxn = 0;
        ieee80211_pae_mfp_txn_complete(ic, id, fPaeStage, 0);
    }
    ieee80211_release_node(ic, ni);
    if_input(ifp, &ml);
}

struct VirtNewstate {
    enum ieee80211_state nstate;
    int arg;
    int err;
};

IOReturn ItlVirt::
virt_newstate_gated(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3)
{
    VirtNewstate *ns = (VirtNewstate *)arg1;

    ns->err = virt_newstate((struct ieee80211com *)arg0, ns->nstate, ns->arg);
    return kIOReturnSuccess;
}

int ItlVirt::
virt_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
    ItlVirt *that = virt_hal(ic->ic_softc);
    int err;

    /*
     * The SAE worker gives up a failed exchange from its own thread, and the
     * watchdog of the Airport layer times a join out from its own workloop.
     * State changes, like the frames, are handled on the main workloop gate.
     */
    if (!that->getMainWorkLoop()->inGate()) {
        VirtNewstate ns = { nstate, arg, EIO };

        that->getMainCommandGate()->runAction(virt_newstate_gated, ic, &ns);
        return ns.err;
    }

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
    ItlVirt *that = virt_hal(ic->ic_softc);

    return that->scanStart(true, reassocSerial);
}

int ItlVirt::
virt_bgscan_abort(struct ieee80211com *ic, uint64_t reassocSerial)
{
    ItlVirt *that = virt_hal(ic->ic_softc);
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

/*
 * The bit of a key in fKeys: the group keys by index, the two integrity
 * group keys (4 and 5) above them, then the pairwise key.
 */
static uint8_t
virt_key_bit(const struct ieee80211_key *k)
{
    if (k->k_flags & IEEE80211_KEY_IGTK)
        return (uint8_t)(0x10 << (k->k_id & 1));
    return (k->k_flags & IEEE80211_KEY_GROUP) ? (uint8_t)(1 << (k->k_id & 3)) : 0x80;
}

/* The data keys go to the firmware; a key it holds is never used in software. */
static bool
virt_key_is_firmware(const struct ieee80211_key *k)
{
    return (k->k_cipher == IEEE80211_CIPHER_CCMP || k->k_cipher == IEEE80211_CIPHER_TKIP) &&
        !(k->k_flags & (IEEE80211_KEY_IGTK | IEEE80211_KEY_SWCRYPTO));
}

/* The integrity group key of an association with management frame protection. */
static bool
virt_key_is_igtk(const struct ieee80211_key *k)
{
    return k->k_cipher == IEEE80211_CIPHER_BIP && (k->k_flags & IEEE80211_KEY_IGTK) &&
        (k->k_id == 4 || k->k_id == 5) && k->k_len == 16;
}

/*
 * Hands a key to the firmware: a data key, pairwise (ni is the AP) or group,
 * or an integrity group key.
 */
int ItlVirt::
keyAdd(struct ieee80211_node *ni, const struct ieee80211_key *k)
{
    const bool group = (k->k_flags & IEEE80211_KEY_GROUP) != 0;
    const bool igtk = virt_key_is_igtk(k);
    struct vap_key vk;
    int rc;

    if (k->k_len > sizeof(vk.key) || (!group && !igtk && ni == NULL))
        return EINVAL;
    /* The key bytes are in the order nl80211 has them too, TKIP included. */
    bzero(&vk, sizeof(vk));
    vk.cipher = igtk ? 0x000fac06 : k->k_cipher == IEEE80211_CIPHER_CCMP ? 0x000fac04 : 0x000fac02;
    vk.idx = k->k_id;
    vk.len = (uint8_t)k->k_len;
    memcpy(vk.key, k->k_key, k->k_len);
    if (igtk) {
        vk.flags = VAP_KEY_IGTK;
        /* The packet number the AP's protected frames continue from. */
        for (int i = 0; i < 6; i++)
            vk.rsc[i] = (uint8_t)(k->k_mgmt_rsc >> (8 * i));
    } else if (group) {
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
        getMainCommandGate()->runAction(virt_start_task, &com.sc_ic.ic_if);
    }
    rc = request(VAP_CMD_KEY_ADD, &vk, sizeof(vk));
    explicit_bzero(&vk, sizeof(vk));
    XYLog("vap: %s key %u %s (%d)\n", igtk ? "integrity group" : group ? "group" : "pairwise",
          k->k_id, rc == 0 ? "installed" : "refused", rc);
    if (rc != 0)
        return EIO;
    fKeys |= virt_key_bit(k);
    /* The group key is the last data key of a handshake: the link is fully keyed. */
    if (group)
        setPort(true);
    return 0;
}

/* Takes a key back from the firmware, if it holds it. */
void ItlVirt::
keyDel(const struct ieee80211_key *k)
{
    struct vap_key vk;

    /* net80211 deletes more often than it sets: only what the firmware holds. */
    if (!(fKeys & virt_key_bit(k)))
        return;
    bzero(&vk, sizeof(vk));
    vk.idx = k->k_id;
    vk.flags = (k->k_flags & IEEE80211_KEY_IGTK) ? VAP_KEY_IGTK :
        (k->k_flags & IEEE80211_KEY_GROUP) ? VAP_KEY_GROUP : VAP_KEY_PAIRWISE;
    /* Fails harmlessly when the firmware has left the BSS already. */
    (void)request(VAP_CMD_KEY_DEL, &vk, sizeof(vk));
    fKeys &= ~virt_key_bit(k);
}

int ItlVirt::
virt_set_key(struct ieee80211com *ic, struct ieee80211_node *ni,
             struct ieee80211_key *k)
{
    /* A live BIP slot belongs to its publisher in net80211, as in iwx. */
    if (k == NULL || ieee80211_bip_key_is_slot(ic, k))
        return EINVAL;
    /*
     * The integrity group key does not come this way: with management frame
     * protection the keys of a handshake go through virt_pae_txn_finish().
     */
    if (!virt_key_is_firmware(k) || k->k_len > sizeof(vap_key::key) ||
        (!(k->k_flags & IEEE80211_KEY_GROUP) && ni == NULL))
        return ieee80211_set_key(ic, ni, k);
    return virt_hal(ic->ic_softc)->keyAdd(ni, k);
}

void ItlVirt::
virt_delete_key(struct ieee80211com *ic, struct ieee80211_node *ni,
                struct ieee80211_key *k)
{
    const bool igtk = k != NULL && virt_key_is_igtk(k);

    if (k == NULL || ieee80211_bip_key_is_slot(ic, k))
        return;
    if (igtk || virt_key_is_firmware(k))
        virt_hal(ic->ic_softc)->keyDel(k);
    /* The software side of an integrity group key is net80211's, like any software key. */
    if (igtk || !virt_key_is_firmware(k))
        ieee80211_delete_key(ic, ni, k);
    else
        explicit_bzero(k, sizeof(*k));
}

/*
 * The keys of a handshake with management frame protection.
 *
 * net80211 does not install those itself.  It offers them one by one to a
 * transaction owner in the HAL and, when the owner has reported each one
 * done, sends its reply to the AP (message 4, or message 2 of a group
 * handshake) and asks the owner to finish: to make the keys live together.
 * Without such an owner it refuses to complete an SAE authentication at all.
 *
 * That shape is for firmware whose key commands complete later.  Here they
 * complete at once, and nothing may be installed before the reply has left:
 * the firmware encrypts whatever it sends once it has the pairwise key.  So
 * a key offered is only noted, rxFrame() reports it done as soon as
 * net80211 has returned from the frame which brought it, and the finish
 * step installs all of them.
 */
int ItlVirt::
virt_pae_txn_submit(struct ieee80211com *ic, u_int64_t id, u_int64_t epoch,
                    struct ieee80211_node *ni, const struct ieee80211_key *k, u_int8_t stage)
{
    ItlVirt *that = virt_hal(ic->ic_softc);

    that->fPaeTxn = id;
    that->fPaeStage = stage;
    return 0;
}

/* Nothing of a transaction is in the firmware before it has finished. */
void ItlVirt::
virt_pae_txn_cancel(struct ieee80211com *ic, u_int64_t id)
{
    ItlVirt *that = virt_hal(ic->ic_softc);

    if (that->fPaeTxn == id)
        that->fPaeTxn = 0;
}

int ItlVirt::
virt_pae_txn_finish(struct ieee80211com *ic, u_int64_t id)
{
    ItlVirt *that = virt_hal(ic->ic_softc);
    IOSimpleLock *lock = ic->ic_pae_selected_bss_lock;
    const struct ieee80211_pae_mfp_txn *txn = &ic->ic_pae_mfp_txn;
    struct ieee80211_key key[3];    /* pairwise, group, integrity group */
    struct ieee80211_node *ni = NULL;
    IOInterruptState irq;
    unsigned have = 0, done = 0;
    int err = 0;

    if (lock == NULL)
        return ECANCELED;
    /* The keys are in net80211's record of the transaction, under its lock. */
    irq = IOSimpleLockLockDisableInterrupt(lock);
    if (txn->active && txn->id == id) {
        ni = txn->ni;
        key[0] = txn->ptk_key;
        key[1] = txn->gtk_key;
        key[2] = txn->igtk_key;
        have = (txn->have_ptk ? 1 : 0) | (txn->have_gtk ? 2 : 0) | (txn->have_igtk ? 4 : 0);
    }
    IOSimpleLockUnlockEnableInterrupt(lock, irq);
    if (ni == NULL)
        return ECANCELED;

    for (unsigned i = 0; i < 3 && err == 0; i++) {
        if (!(have & (1 << i)))
            continue;
        err = that->keyAdd(ni, &key[i]);
        if (err == 0)
            done |= 1 << i;
    }
    if (err == 0) {
        /* Makes the keys net80211's: from here it deletes them like any others. */
        irq = IOSimpleLockLockDisableInterrupt(lock);
        err = ieee80211_pae_mfp_txn_finish_publish_locked(ic, id);
        IOSimpleLockUnlockEnableInterrupt(lock, irq);
    }
    /* Refused: the firmware must not keep keys net80211 does not know of. */
    if (err != 0) {
        for (unsigned i = 0; i < 3; i++)
            if (done & (1 << i))
                that->keyDel(&key[i]);
        that->setPort(false);
    }
    explicit_bzero(key, sizeof(key));
    return err;
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
    struct ieee80211com *ic = &com.sc_ic;
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
    if (wclGeneration != 0 && ieee80211_wcl_scan_plan_snapshot(&com.sc_ic, &plan) &&
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
    struct ieee80211com *ic = &com.sc_ic;
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
    struct ieee80211com *ic = &com.sc_ic;
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
    struct ieee80211com *ic = &com.sc_ic;
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
    /* A reserved SAE join has the radio until it starts or is released. */
    if (fWcl.phase != VIRT_WCL_IDLE || wclSaeAdmissionReserved) {
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
    struct ieee80211com *ic = &com.sc_ic;
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
    if (fWcl.phase != VIRT_WCL_IDLE || fScan.state != VIRT_SCAN_IDLE ||
        wclSaeAdmissionReserved) {
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

/*
 * Driver-resident SAE.
 *
 * net80211 has the SAE engine; the glue between it and a HAL exists as the
 * text of hal_iwx/IwxSaeEngine.inc: the password macOS stages, the one
 * attempt in progress with its worker, the net80211 hooks, the HAL methods
 * of the Airport layer.  That text is included here unchanged and built
 * for this HAL by the names below: the class, the softc and the few iwx
 * names it uses for what is a HAL's own.  The rest of this file is what it
 * needs around it: its lifecycle lease, the transport of authentication
 * frames, and the calls at attach, radio on and off, detach and on the way
 * out of an association request.
 */
#define ItlIwx                      ItlVirt
#define iwx_softc                   virt_softc
#define iwx_sae_engine_owner        virt_sae_engine_owner
#define IWX_SAE_ENGINE_PEERQ_LEN    VIRT_SAE_PEERQ_LEN
#define IWX_FLAG_SHUTDOWN           VIRT_FLAG_SHUTDOWN
#define IWX_FLAG_SCANNING           VIRT_FLAG_SCANNING
#define IC2IFP(_ic)                 (&(_ic)->ic_if)
/* The scan state the text reads under its scan lock. */
#define wclScanLock                 fScanLock
#define wclScanPhase                scanBusy()
#define ItlIwxWclScanPhase          VirtScanBusy
/* The worker's tickets have the top bit, the Airport layer's relay has not. */
#define IWX_SAE_ENGINE_TICKET_DIRECT_BIT    ((u_int64_t)kItlSaeAuthTransportV1DriverTicketBit)
#define IWX_SAE_ENGINE_TICKET_COUNTER_MASK  (~IWX_SAE_ENGINE_TICKET_DIRECT_BIT)
/* The steps of an exchange iwx records for its trace tool: nobody listens here. */
#define IWX_DIRECT_SAE_TRACE(_ic, _event)   do { (void)(_ic); } while (0)
/*
 * The text lets an association request go by moving a transmit ring on.
 * Here that step only takes the claim; saeAdmitAssocReq() sends the frame.
 */
struct virt_sae_ring {
    int ring_count, cur, qid;
};
#define iwx_tx_ring                 virt_sae_ring
#define IWX_HBUS_TARG_WRPTR         0
#define IWX_WRITE(_sc, _reg, _val)  do { (void)(_sc); (void)(_val); } while (0)

namespace {

/*
 * A cancelled ticket is not sent.  Tickets only grow, so the highest one
 * cancelled stands for all before it.
 */
static bool
iwx_sae_tx_ticket_cancelled_locked(const struct virt_softc *sc, u_int64_t ticket)
{
    return sc == NULL || !(ticket & IWX_SAE_ENGINE_TICKET_DIRECT_BIT) ||
        (ticket & IWX_SAE_ENGINE_TICKET_COUNTER_MASK) <= sc->sc_sae_tx_cancel_through;
}

static void
iwx_sae_tx_cancel_ticket_locked(struct virt_softc *sc, u_int64_t ticket)
{
    const u_int64_t counter = ticket & IWX_SAE_ENGINE_TICKET_COUNTER_MASK;

    if (sc != NULL && (ticket & IWX_SAE_ENGINE_TICKET_DIRECT_BIT) &&
        counter > sc->sc_sae_tx_cancel_through)
        sc->sc_sae_tx_cancel_through = counter;
}

/* SAE comes with management frame protection, which is on when the firmware has it. */
static bool
iwx_mfp_runtime_enabled(const struct virt_softc *sc)
{
    return sc != NULL && (sc->sc_ic.ic_caps & IEEE80211_C_MFP) != 0;
}

static bool
iwx_sae_wcl_credential_runtime_opted_in(void)
{
    return ITL_SAE_DRIVER_CRYPTO_AVAILABLE != 0;
}

} // namespace

/* A warning of the text itself: an interrupt state saved and restored under one condition. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconditional-uninitialized"
#include "../hal_iwx/IwxSaeEngine.inc"
#pragma clang diagnostic pop

#undef ItlIwx
#undef iwx_softc
#undef iwx_sae_engine_owner
#undef IWX_SAE_ENGINE_PEERQ_LEN
#undef IWX_FLAG_SHUTDOWN
#undef IWX_FLAG_SCANNING
#undef IC2IFP
#undef wclScanLock
#undef wclScanPhase
#undef ItlIwxWclScanPhase
#undef IWX_DIRECT_SAE_TRACE
#undef iwx_tx_ring
#undef IWX_HBUS_TARG_WRPTR
#undef IWX_WRITE

/*
 * The lease.  Its holders are the worker and the credential calls of the
 * Airport layer.  Nothing waits for them when the radio goes off (the
 * worker itself needs the workloop gate, which radioStop() is called on),
 * so the lease is refused only from detach on.
 */
bool ItlVirt::
iwx_task_gate_enter(struct virt_softc *sc, bool bootstrap_init)
{
    bool entered = false;

    if (sc->sc_task_gate_lock == NULL)
        return false;
    IOLockLock(sc->sc_task_gate_lock);
    if (!sc->sc_task_gate_closed) {
        sc->sc_task_gate_active++;
        entered = true;
    }
    IOLockUnlock(sc->sc_task_gate_lock);
    return entered;
}

void ItlVirt::
iwx_task_gate_leave(struct virt_softc *sc)
{
    if (sc->sc_task_gate_lock == NULL)
        return;
    IOLockLock(sc->sc_task_gate_lock);
    sc->sc_task_gate_active--;
    IOLockWakeup(sc->sc_task_gate_lock, sc, false);
    IOLockUnlock(sc->sc_task_gate_lock);
}

void ItlVirt::
iwx_add_task(struct virt_softc *sc, struct taskq *taskq, struct task *task)
{
    if (sc->sc_task_gate_lock == NULL || !sc->sc_task_callbacks_ready || taskq == NULL)
        return;
    /* Under the lock which closes the gate: detach cannot miss a task. */
    IOLockLock(sc->sc_task_gate_lock);
    if (!sc->sc_task_gate_closed)
        (void)task_add(taskq, task);
    IOLockUnlock(sc->sc_task_gate_lock);
}

/*
 * In iwx this task delivers the completions of sent frames, which the
 * transport here has none of, and ends a failed join when the engine has no
 * lock.  The text queues it for the second only, which cannot happen here:
 * attach fails without that lock.  It exists so that path of the text is
 * whole rather than a task without a function.
 */
void ItlVirt::
virt_sae_tx_task(void *arg)
{
    struct virt_softc *sc = (struct virt_softc *)arg;
    ItlVirt *that = virt_hal(sc);

    if (!that->iwx_task_gate_enter(sc, false))
        return;
    iwx_sae_tx_finish_join_retirement(sc);
    iwx_sae_engine_wake_join_retirement(sc);
    that->iwx_task_gate_leave(sc);
}

struct VirtSaeTx {
    const struct ItlSaeAuthTxRequestV1 *request;
    IOReturn rc;
};

/*
 * One authentication frame of the SAE worker, commit or confirm.
 * kIOReturnNotReady makes the worker try again shortly, any other failure
 * ends the attempt.  The firmware reports nothing about a frame it took,
 * so taking it is the completion the engine waits for; if the frame is
 * lost after that, the engine's wait for the AP's reply runs out and it
 * sends again.
 */
IOReturn ItlVirt::
submitSaeAuthFrame(const struct ItlSaeAuthTxRequestV1 *request)
{
    VirtSaeTx tx = { request, kIOReturnNotReady };

    if (!itl_sae_auth_transport_request_is_well_formed(request))
        return kIOReturnBadArgument;
    /* The Airport layer's relay of an exchange run in user space is not served. */
    if (!(request->ticket & IWX_SAE_ENGINE_TICKET_DIRECT_BIT))
        return kIOReturnUnsupported;
    if (!iwx_task_gate_enter(&com, false))
        return kIOReturnNotReady;
    getMainCommandGate()->runAction(virt_sae_tx_gated, this, &tx);
    iwx_task_gate_leave(&com);
    return tx.rc;
}

IOReturn ItlVirt::
virt_sae_tx_gated(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3)
{
    ItlVirt *that = (ItlVirt *)arg0;
    VirtSaeTx *tx = (VirtSaeTx *)arg1;
    const struct ItlSaeAuthTxRequestV1 *request = tx->request;
    struct virt_softc *sc = &that->com;
    struct ieee80211com *ic = &sc->sc_ic;
    struct ItlSaeAuthTransportEventV1 done;
    struct vap_tx_meta meta;
    struct vap_iov iov;
    bool cancelled;
    int rc = VAP_G_BUSY;
    mbuf_t m = NULL;

    /*
     * The frame is built by net80211, which refuses a request that is not
     * for the exchange it is in: state, BSS, association epoch, addresses.
     */
    if ((ic->ic_if.if_flags & IFF_RUNNING) && ic->ic_bss != NULL)
        m = ieee80211_sae_auth_frame_build(ic, ic->ic_bss, request);
    if (m != NULL) {
        bzero(&meta, sizeof(meta));
        iov.p = mbuf_data(m);
        iov.len = (uint32_t)mbuf_len(m);
        /*
         * A cancellation after this check is too late for the frame, exactly
         * as one after a transmission would be; the lock is not held across
         * the device access to close that window.  A frame the device had no
         * room for goes first.
         */
        IOSimpleLockLock(sc->sc_sae_tx_lock);
        cancelled = iwx_sae_tx_ticket_cancelled_locked(sc, request->ticket);
        IOSimpleLockUnlock(sc->sc_sae_tx_lock);
        if (!cancelled && that->fTxHeld == NULL)
            rc = vap_tx(that->fDev, &meta, &iov, 1);
        /* vap_tx() copied the frame; it carries no node reference. */
        mbuf_freem(m);
        tx->rc = cancelled ? kIOReturnAborted : rc == VAP_G_OK ? kIOReturnSuccess :
            rc == VAP_G_BUSY ? kIOReturnNotReady : kIOReturnError;
    } else if (ic->ic_if.if_flags & IFF_RUNNING) {
        tx->rc = kIOReturnError;
    }
    if (tx->rc == kIOReturnSuccess) {
        bzero(&done, sizeof(done));
        done.version = kItlSaeAuthTransportV1Version;
        done.size = sizeof(done);
        done.kind = kItlSaeAuthTransportEventTxComplete;
        done.association_epoch = request->association_epoch;
        done.relay_generation = request->relay_generation;
        done.ticket = request->ticket;
        done.phase = request->phase;
        done.auth_status = request->auth_status;
        done.wire_transaction = request->wire_transaction;
        memcpy(done.bssid, request->bssid, sizeof(done.bssid));
        memcpy(done.sta, request->sta, sizeof(done.sta));
        /* Closed hooks: the radio went off and the attempt is over anyway. */
        if (iwx_sae_engine_callback_enter(sc)) {
            (void)iwx_sae_engine_queue_terminal(sc, &done);
            iwx_sae_engine_callback_leave(sc);
        }
    }
    XYLog("vap: SAE %s to %s %s (0x%x)\n",
          request->phase == kItlSaeAuthTransportPhaseCommit ? "commit" : "confirm",
          ether_sprintf((u_char *)request->bssid),
          tx->rc == kIOReturnSuccess ? "sent" : "not sent", (unsigned)tx->rc);
    return kIOReturnSuccess;
}

void ItlVirt::
cancelSaeAuthFrame(uint64_t ticket)
{
    if (ticket == 0 || com.sc_sae_tx_lock == NULL)
        return;
    IOSimpleLockLock(com.sc_sae_tx_lock);
    iwx_sae_tx_cancel_ticket_locked(&com, ticket);
    IOSimpleLockUnlock(com.sc_sae_tx_lock);
}

/*
 * After an SAE exchange run here, net80211 queues the association request
 * like any other, and the frame may leave only while the exchange it
 * follows is still the current one: the glue decides, and takes the frame's
 * departure as the end of its attempt.  False: the frame must not be sent.
 */
bool ItlVirt::
saeAdmitAssocReq(mbuf_t m, struct ieee80211_node *ni)
{
    const struct ieee80211_frame *wh = mtod(m, struct ieee80211_frame *);
    struct virt_sae_ring ring = { 1, 0, 0 };
    struct IwxSaeAssocTxClaim claim;
    bool admitted = false;

    if ((wh->i_fc[0] & (IEEE80211_FC0_TYPE_MASK | IEEE80211_FC0_SUBTYPE_MASK)) !=
        (IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_ASSOC_REQ))
        return true;
    switch (iwx_sae_engine_assoc_tx_preflight(&com, ni, wh, &claim)) {
    case IWX_SAE_ASSOC_TX_NOT_DIRECT:
        return true;
    case IWX_SAE_ASSOC_TX_ADMITTED:
        admitted = iwx_sae_engine_assoc_tx_commit(&com, &ring, ring.cur, 0, 0, ni, &claim);
        break;
    default:
        break;
    }
    XYLog("vap: association request after SAE %s\n", admitted ? "released" : "held back");
    /*
     * The worker retires its attempt when it finds the frame gone.  It looks
     * once after queueing it, which is too early for a frame that waited
     * here for room in the device.
     */
    if (admitted)
        iwx_sae_engine_schedule_task(&com);
    return admitted;
}

/*
 * The AP refused the exchange (a wrong password, mostly).  The worker has
 * failed the join for the Airport layer, which is told once three parties
 * have cleaned up; this is the HAL's part.  The BSS is left for SCAN as in
 * virt_newstate(), minus the scan: nothing is to be looked for.  Each step
 * may end the join through a callback, hence the checks.
 */
void ItlVirt::
iwx_wcl_join_failure_scan(struct ieee80211com *ic, uint64_t generation)
{
    if (!ieee80211_wcl_join_failure_pending(ic, generation))
        return;
    virt_hal(ic->ic_softc)->getMainCommandGate()->runAction(virt_join_failure_gated,
                                                           ic, &generation);
}

IOReturn ItlVirt::
virt_join_failure_gated(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3)
{
    struct ieee80211com *ic = (struct ieee80211com *)arg0;
    const uint64_t generation = *(const uint64_t *)arg1;

    XYLog("vap: SAE refused by %s, join failed\n", ether_sprintf(ic->ic_bss->ni_bssid));
    ieee80211_pae_assoc_epoch_note_newstate(ic, IEEE80211_S_SCAN, -1);
    if (!ieee80211_wcl_join_failure_pending(ic, generation))
        return kIOReturnAborted;
    ieee80211_set_link_state(ic, LINK_STATE_DOWN);
    if (!ieee80211_wcl_join_failure_pending(ic, generation))
        return kIOReturnAborted;
    ieee80211_node_cleanup(ic, ic->ic_bss);
    if (!ieee80211_wcl_join_failure_pending(ic, generation))
        return kIOReturnAborted;
    ic->ic_mgt_timer = 0;
    ic->ic_state = IEEE80211_S_SCAN;
    iwx_sae_engine_request_join_retirement((struct virt_softc *)ic->ic_softc, generation);
    ieee80211_wcl_join_cleanup_done(ic, generation, IEEE80211_JOIN_CLEANUP_LOWER);
    return kIOReturnSuccess;
}

/*
 * After ieee80211_ifattach(): the glue shares net80211's selected-BSS lock.
 * The hooks are not published here but by radioStart().
 */
bool ItlVirt::
saeAttach()
{
    struct virt_softc *sc = &com;
    const uint64_t needed = VAP_FEAT_SAE_FRAMES | VAP_FEAT_PMF;

    sc->sc_task_gate_lock = IOLockAlloc();
    sc->sc_sae_tx_lock = IOSimpleLockAlloc();
    sc->sc_sae_engine_lock = IOSimpleLockAlloc();
    sc->sc_sae_wcl_credential_lock = IOSimpleLockAlloc();
    if (sc->sc_task_gate_lock == NULL || sc->sc_sae_tx_lock == NULL ||
        sc->sc_sae_engine_lock == NULL || sc->sc_sae_wcl_credential_lock == NULL)
        return false;
    /* What iwx_attach() starts the glue's state from. */
    sc->sc_sae_engine_lifecycle_generation = 1;
    sc->sc_sae_engine_callback_state = IWX_SAE_ENGINE_CALLBACK_CLOSED;
    sc->sc_sae_engine_stopping = true;
    sc->sc_nswq = systq;
    task_set(&sc->sae_tx_task, virt_sae_tx_task, sc, "virt_sae_tx_task");
    task_set(&sc->sae_engine_task, iwx_sae_engine_task, sc, "virt_sae_engine_task");
    sc->sc_taskq_initialized = sc->sc_task_callbacks_ready = true;
    if (!saePeerTimer.init(this, getMainWorkLoop(), &ItlVirt::iwx_sae_peer_timer_event,
                           &ItlVirt::iwx_sae_peer_timer_timeout))
        return false;
    sc->sc_sae_engine_task_ready = true;
    /*
     * The firmware must take the authentication frames of an exchange run
     * here and protect management frames, which WPA3 requires.  With this
     * set supportsDriverResidentSae() is true and macOS offers WPA3.
     */
    sc->sc_sae_engine_runtime_enabled = (fCaps.fw_features & needed) == needed;
    XYLog("vap: WPA3-SAE %s (firmware: SAE frames %d, management frame protection %d)\n",
          supportsDriverResidentSae() ? "available" : "not available",
          (fCaps.fw_features & VAP_FEAT_SAE_FRAMES) != 0, (fCaps.fw_features & VAP_FEAT_PMF) != 0);
    return true;
}

/*
 * Not on the workloop gate: the worker is waited for, and it may be waiting
 * for the gate.  Safe on a HAL whose attach failed anywhere.
 */
void ItlVirt::
saeDetach()
{
    struct virt_softc *sc = &com;

    /* Ends the attempt, withdraws the hooks, waits for the worker, drops the password. */
    iwx_sae_engine_detach_begin(sc);
    iwx_sae_wcl_detach_begin(sc);
    sc->sc_sae_engine_runtime_enabled = false;
    if (sc->sc_task_gate_lock != NULL) {
        IOLockLock(sc->sc_task_gate_lock);
        sc->sc_task_gate_closed = sc->sc_task_gate_detaching = true;
        sc->sc_flags |= VIRT_FLAG_SHUTDOWN;
        while (sc->sc_task_gate_active != 0)
            IOLockSleep(sc->sc_task_gate_lock, sc, THREAD_UNINT);
        IOLockUnlock(sc->sc_task_gate_lock);
    }
    if (fTaskqReady && sc->sc_task_callbacks_ready) {
        task_del(systq, &sc->sae_tx_task);
        taskq_barrier(systq);
    }
    /* The hook kept for an attempt in AUTH until now goes too, before ieee80211_ifdetach(). */
    (void)iwx_sae_engine_publish_hooks(sc, false, false, 0);
}
