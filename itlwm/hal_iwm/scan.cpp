/*
* Copyright (C) 2020  钟先耀
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
/*    $OpenBSD: if_iwm.c,v 1.316 2020/12/07 20:09:24 tobhe Exp $    */

/*
 * Copyright (c) 2014, 2016 genua gmbh <info@genua.de>
 *   Author: Stefan Sperling <stsp@openbsd.org>
 * Copyright (c) 2014 Fixup Software Ltd.
 * Copyright (c) 2017 Stefan Sperling <stsp@openbsd.org>
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

/*-
 * Based on BSD-licensed source modules in the Linux iwlwifi driver,
 * which were used as the reference documentation for this implementation.
 *
 ***********************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2013 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 Intel Deutschland GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2013 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 Intel Deutschland GmbH
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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

#include "ItlIwm.hpp"
#include "rs.h"
#include <ClientKit/AirportItlwmScanHomeAwayBridge.h>
#include <HAL/ItlScanCommandPolicy.hpp>

uint16_t ItlIwm::
iwm_scan_rx_chain(struct iwm_softc *sc)
{
    uint16_t rx_chain;
    uint8_t rx_ant;
    
    rx_ant = iwm_fw_valid_rx_ant(sc);
    rx_chain = rx_ant << IWM_PHY_RX_CHAIN_VALID_POS;
    rx_chain |= rx_ant << IWM_PHY_RX_CHAIN_FORCE_MIMO_SEL_POS;
    rx_chain |= rx_ant << IWM_PHY_RX_CHAIN_FORCE_SEL_POS;
    rx_chain |= 0x1 << IWM_PHY_RX_CHAIN_DRIVER_FORCE_POS;
    return htole16(rx_chain);
}

uint32_t ItlIwm::
iwm_scan_rate_n_flags(struct iwm_softc *sc, int flags, int no_cck)
{
    uint32_t tx_ant;
    int i, ind;
    
    for (i = 0, ind = sc->sc_scan_last_antenna;
         i < IWM_RATE_MCS_ANT_NUM; i++) {
        ind = (ind + 1) % IWM_RATE_MCS_ANT_NUM;
        if (iwm_fw_valid_tx_ant(sc) & (1 << ind)) {
            sc->sc_scan_last_antenna = ind;
            break;
        }
    }
    tx_ant = (1 << sc->sc_scan_last_antenna) << IWM_RATE_MCS_ANT_POS;
    
    if ((flags & IEEE80211_CHAN_2GHZ) && !no_cck)
        return htole32(IWL_RATE_1M_PLCP | RATE_MCS_CCK_MSK |
                       tx_ant);
    else
        return htole32(IWL_RATE_6M_PLCP | tx_ant);
}

uint8_t ItlIwm::
iwm_lmac_scan_fill_channels(struct iwm_softc *sc,
    struct iwm_scan_channel_cfg_lmac *chan, int n_ssids, int bgscan,
    const struct ieee80211_wcl_scan_plan *plan)
{
    struct ieee80211com *ic = &sc->sc_ic;
    struct ieee80211_channel *c;
    const bool exactWclPlan = plan != NULL && plan->active != 0;
    const bool activeProbe = exactWclPlan ?
        plan->scan_type != IEEE80211_WCL_SCAN_TYPE_PASSIVE :
        (n_ssids != 0 && !bgscan);
    uint8_t nchan;
    
    for (nchan = 0, c = &ic->ic_channels[1];
         c <= &ic->ic_channels[IEEE80211_CHAN_MAX] &&
         nchan < sc->sc_capa_n_scan_channels;
         c++) {
        if (c->ic_flags == 0)
            continue;
        if (exactWclPlan &&
            !ieee80211_wcl_scan_plan_channel_allowed(ic, plan, c))
            continue;
        
        chan->channel_num = htole16(ieee80211_mhz2ieee(c->ic_freq, 0));
        chan->iter_count = htole16(1);
        chan->iter_interval = 0;
        chan->flags = htole32(IWM_UNIFIED_SCAN_CHANNEL_PARTIAL);
        if (activeProbe)
            chan->flags |= htole32(1 << 1); /* select SSID 0 */
        chan++;
        nchan++;
    }
    
    return nchan;
}

uint8_t ItlIwm::
iwm_umac_scan_fill_channels(struct iwm_softc *sc,
    struct iwm_scan_channel_cfg_umac *chan, int n_ssids, int bgscan,
    const struct ieee80211_wcl_scan_plan *plan)
{
    struct ieee80211com *ic = &sc->sc_ic;
    struct ieee80211_channel *c;
    const bool exactWclPlan = plan != NULL && plan->active != 0;
    const bool activeProbe = exactWclPlan ?
        plan->scan_type != IEEE80211_WCL_SCAN_TYPE_PASSIVE :
        (n_ssids != 0 && !bgscan);
    uint8_t nchan;
    
    for (nchan = 0, c = &ic->ic_channels[1];
         c <= &ic->ic_channels[IEEE80211_CHAN_MAX] &&
         nchan < sc->sc_capa_n_scan_channels;
         c++) {
        if (c->ic_flags == 0)
            continue;
        if (exactWclPlan &&
            !ieee80211_wcl_scan_plan_channel_allowed(ic, plan, c))
            continue;
        
        chan->channel_num = ieee80211_mhz2ieee(c->ic_freq, 0);
        chan->iter_count = 1;
        chan->iter_interval = htole16(0);
        if (activeProbe)
            chan->flags = htole32(1 << 0); /* select SSID 0 */
        chan++;
        nchan++;
    }
    
    return nchan;
}

int ItlIwm::
iwm_fill_probe_req_v1(struct iwm_softc *sc, struct iwm_scan_probe_req_v1 *preq1)
{
    struct iwm_scan_probe_req preq2;
    int err, i;
    
    err = iwm_fill_probe_req(sc, &preq2);
    if (err)
        return err;
    
    preq1->mac_header = preq2.mac_header;
    for (i = 0; i < nitems(preq1->band_data); i++)
        preq1->band_data[i] = preq2.band_data[i];
    preq1->common_data = preq2.common_data;
    memcpy(preq1->buf, preq2.buf, sizeof(preq1->buf));
    return 0;
}

int ItlIwm::
iwm_fill_probe_req(struct iwm_softc *sc, struct iwm_scan_probe_req *preq)
{
    struct ieee80211com *ic = &sc->sc_ic;
    struct _ifnet *ifp = IC2IFP(ic);
    struct ieee80211_frame *wh = (struct ieee80211_frame *)preq->buf;
    struct ieee80211_rateset *rs;
    size_t remain = sizeof(preq->buf);
    uint8_t *frm, *pos;
    
    memset(preq, 0, sizeof(*preq));
    
    if (remain < sizeof(*wh) + 2)
        return ENOBUFS;
    
    /*
     * Build a probe request frame.  Most of the following code is a
     * copy & paste of what is done in net80211.
     */
    wh->i_fc[0] = IEEE80211_FC0_VERSION_0 | IEEE80211_FC0_TYPE_MGT |
    IEEE80211_FC0_SUBTYPE_PROBE_REQ;
    wh->i_fc[1] = IEEE80211_FC1_DIR_NODS;
    //        IEEE80211_ADDR_COPY(ic->ic_myaddr, LLADDR(ifp->if_sadl));
    IEEE80211_ADDR_COPY(wh->i_addr1, etherbroadcastaddr);
    IEEE80211_ADDR_COPY(wh->i_addr2, ic->ic_myaddr);
    IEEE80211_ADDR_COPY(wh->i_addr3, etherbroadcastaddr);
    *(uint16_t *)&wh->i_dur[0] = 0;    /* filled by HW */
    *(uint16_t *)&wh->i_seq[0] = 0;    /* filled by HW */
    
    frm = (uint8_t *)(wh + 1);
    *frm++ = IEEE80211_ELEMID_SSID;
    *frm++ = 0;
    /* hardware inserts SSID */
    
    /* Tell firmware where the MAC header and SSID IE are. */
    preq->mac_header.offset = 0;
    preq->mac_header.len = htole16(frm - (uint8_t *)wh);
    remain -= frm - (uint8_t *)wh;
    
    /* Fill in 2GHz IEs and tell firmware where they are. */
    rs = &ic->ic_sup_rates[IEEE80211_MODE_11G];
    if (rs->rs_nrates > IEEE80211_RATE_SIZE) {
        if (remain < 4 + rs->rs_nrates)
            return ENOBUFS;
    } else if (remain < 2 + rs->rs_nrates)
        return ENOBUFS;
    preq->band_data[0].offset = htole16(frm - (uint8_t *)wh);
    pos = frm;
    frm = ieee80211_add_rates(frm, rs);
    if (rs->rs_nrates > IEEE80211_RATE_SIZE)
        frm = ieee80211_add_xrates(frm, rs);
    remain -= frm - pos;
    
    if (isset(sc->sc_enabled_capa,
              IWM_UCODE_TLV_CAPA_DS_PARAM_SET_IE_SUPPORT)) {
        if (remain < 3)
            return ENOBUFS;
        *frm++ = IEEE80211_ELEMID_DSPARMS;
        *frm++ = 1;
        *frm++ = 0;
        remain -= 3;
    }
    preq->band_data[0].len = htole16(frm - pos);
    
    if (sc->sc_nvm.sku_cap_band_52GHz_enable) {
        /* Fill in 5GHz IEs. */
        rs = &ic->ic_sup_rates[IEEE80211_MODE_11A];
        if (rs->rs_nrates > IEEE80211_RATE_SIZE) {
            if (remain < 4 + rs->rs_nrates)
                return ENOBUFS;
        } else if (remain < 2 + rs->rs_nrates)
            return ENOBUFS;
        preq->band_data[1].offset = htole16(frm - (uint8_t *)wh);
        pos = frm;
        frm = ieee80211_add_rates(frm, rs);
        if (rs->rs_nrates > IEEE80211_RATE_SIZE)
            frm = ieee80211_add_xrates(frm, rs);
        preq->band_data[1].len = htole16(frm - pos);
        remain -= frm - pos;
        if (ic->ic_flags & IEEE80211_F_VHTON) {
            if (remain < sizeof(struct ieee80211_ie_vhtcap))
                return ENOBUFS;
            frm = ieee80211_add_vhtcaps(frm, ic);
            remain -= frm - pos;
        }
    }
    
    /* Send 11n IEs on both 2GHz and 5GHz bands. */
    preq->common_data.offset = htole16(frm - (uint8_t *)wh);
    pos = frm;
    if (ic->ic_flags & IEEE80211_F_HTON) {
        if (remain < sizeof(struct ieee80211_ie_htcap))
            return ENOBUFS;
        frm = ieee80211_add_htcaps(frm, ic);
        /* XXX add WME info? */
    }

    preq->common_data.len = htole16(frm - pos);
    
    return 0;
}

int ItlIwm::
iwm_lmac_scan(struct iwm_softc *sc, int bgscan, uint64_t scan_serial)
{
    ItlScanCommandPolicy policy = {};
    if (!copyScanCommandPolicy(scan_serial, &policy))
        return ECANCELED;
    const struct ieee80211_wcl_scan_plan &wclPlan = policy.plan;
    const bool exactWclPlan = wclPlan.active != 0;
    const uint8_t scanSsidLength = wclPlan.ssid_len;
    const uint8_t *scanSsid = wclPlan.ssid;
    /* A Tahoe type-1 scan with no SSID is an active wildcard scan.  Keep
     * that transmission mode separate from the optional directed-SSID and
     * pre-connection hints consumed by Intel firmware. */
    const bool activeScan = exactWclPlan ?
        wclPlan.scan_type != IEEE80211_WCL_SCAN_TYPE_PASSIVE :
        scanSsidLength != 0;
    const bool directedSsid = activeScan && scanSsidLength != 0;
    const uint8_t activeDwell = static_cast<uint8_t>(
        ieee80211_wcl_scan_time_or_default(
            exactWclPlan ? wclPlan.active_dwell_ms : 0, 10));
    const uint8_t passiveDwell = static_cast<uint8_t>(
        ieee80211_wcl_scan_time_or_default(
            exactWclPlan ? wclPlan.passive_dwell_ms : 0, 110));
    const uint32_t homeAwayMs = policy.homeAwayMs;
    const uint32_t suspendMs = ieee80211_wcl_scan_time_or_default(
        exactWclPlan ? wclPlan.home_dwell_ms : 0, homeAwayMs);
    struct iwm_host_cmd hcmd = {
        .id = IWM_SCAN_OFFLOAD_REQUEST_CMD,
        .len = { 0, },
        .data = { NULL, },
        .flags = 0,
    };
    struct iwm_scan_req_lmac *req;
    struct iwm_scan_probe_req_v1 *preq;
    size_t req_len;
    int err, async = bgscan;
    
    req_len = sizeof(struct iwm_scan_req_lmac) +
    (sizeof(struct iwm_scan_channel_cfg_lmac) *
     sc->sc_capa_n_scan_channels) + sizeof(struct iwm_scan_probe_req_v1);
    if (req_len > IWM_MAX_CMD_PAYLOAD_SIZE)
        return ENOMEM;
    req = (struct iwm_scan_req_lmac*)malloc(req_len, M_DEVBUF, M_NOWAIT);
    if (req == NULL)
        return ENOMEM;
    bzero(req, req_len);
    
    hcmd.len[0] = (uint16_t)req_len;
    hcmd.data[0] = (void *)req;
    hcmd.flags |= async ? IWM_CMD_ASYNC : 0;
    
    /* These timings correspond to iwlwifi's UNASSOC scan. */
    req->active_dwell = activeDwell;
    req->passive_dwell = passiveDwell;
    req->fragmented_dwell = 44;
    req->extended_dwell = 90;
    if (bgscan) {
        req->max_out_time = htole32(homeAwayMs);
        req->suspend_time = htole32(suspendMs);
    } else {
        req->max_out_time = htole32(0);
        req->suspend_time = htole32(0);
    }
    req->scan_prio = htole32(IWM_SCAN_PRIORITY_HIGH);
    req->rx_chain_select = iwm_scan_rx_chain(sc);
    req->iter_num = htole32(1);
    req->delay = 0;
    
    req->scan_flags = htole32(IWM_LMAC_SCAN_FLAG_PASS_ALL |
                              IWM_LMAC_SCAN_FLAG_ITER_COMPLETE |
                              IWM_LMAC_SCAN_FLAG_EXTENDED_DWELL);
    if (!activeScan)
        req->scan_flags |= htole32(IWM_LMAC_SCAN_FLAG_PASSIVE);
    else if (directedSsid)
        req->scan_flags |=
        htole32(IWM_LMAC_SCAN_FLAG_PRE_CONNECTION);
    if (isset(sc->sc_enabled_capa,
              IWM_UCODE_TLV_CAPA_DS_PARAM_SET_IE_SUPPORT) &&
              isset(sc->sc_enabled_capa,
              IWM_UCODE_TLV_CAPA_WFA_TPC_REP_IE_SUPPORT))
        req->scan_flags |= htole32(IWM_LMAC_SCAN_FLAGS_RRM_ENABLED);
    
    req->flags = htole32(IWM_PHY_BAND_24);
    if (sc->sc_nvm.sku_cap_band_52GHz_enable)
        req->flags |= htole32(IWM_PHY_BAND_5);
    req->filter_flags =
    htole32(IWM_MAC_FILTER_ACCEPT_GRP | IWM_MAC_FILTER_IN_BEACON);
    
    /* Tx flags 2 GHz. */
    req->tx_cmd[0].tx_flags = htole32(IWM_TX_CMD_FLG_SEQ_CTL |
                                      IWM_TX_CMD_FLG_BT_DIS);
    req->tx_cmd[0].rate_n_flags =
    iwm_scan_rate_n_flags(sc, IEEE80211_CHAN_2GHZ, 1/*XXX*/);
    req->tx_cmd[0].sta_id = IWM_AUX_STA_ID;
    
    /* Tx flags 5 GHz. */
    req->tx_cmd[1].tx_flags = htole32(IWM_TX_CMD_FLG_SEQ_CTL |
                                      IWM_TX_CMD_FLG_BT_DIS);
    req->tx_cmd[1].rate_n_flags =
    iwm_scan_rate_n_flags(sc, IEEE80211_CHAN_5GHZ, 1/*XXX*/);
    req->tx_cmd[1].sta_id = IWM_AUX_STA_ID;
    
    /* One selected zero-length SSID is Intel firmware's active wildcard
     * representation; an empty selector list is passive. */
    if (activeScan) {
        req->direct_scan[0].id = IEEE80211_ELEMID_SSID;
        req->direct_scan[0].len = scanSsidLength;
        if (scanSsidLength != 0)
            memcpy(req->direct_scan[0].ssid, scanSsid,
                   scanSsidLength);
    }
    
    req->n_channels = iwm_lmac_scan_fill_channels(sc,
                                                  (struct iwm_scan_channel_cfg_lmac *)req->data,
                                                  activeScan ? 1 : 0, bgscan, &wclPlan);
    if (req->n_channels == 0) {
        ::free(req);
        return EINVAL;
    }
    
    preq = (struct iwm_scan_probe_req_v1 *)(req->data +
                                            (sizeof(struct iwm_scan_channel_cfg_lmac) *
                                             sc->sc_capa_n_scan_channels));
    err = iwm_fill_probe_req_v1(sc, preq);
    if (err) {
        ::free(req);
        return err;
    }
    
    /* Specify the scan plan: We'll do one iteration. */
    req->schedule[0].iterations = 1;
    req->schedule[0].full_scan_mul = 1;
    
    /* Disable EBS. */
    req->channel_opt[0].non_ebs_ratio = 1;
    req->channel_opt[1].non_ebs_ratio = 1;
    
    hcmd.scan_serial = scan_serial;
    err = iwm_send_cmd(sc, &hcmd);
    ::free(req);
    return err;
}

int ItlIwm::
iwm_config_umac_scan(struct iwm_softc *sc)
{
    struct ieee80211com *ic = &sc->sc_ic;
    struct iwm_scan_config *scan_config;
    int err, nchan;
    size_t cmd_size;
    struct ieee80211_channel *c;
    struct iwm_host_cmd hcmd = {
        .id = iwm_cmd_id(IWM_SCAN_CFG_CMD, IWM_LONG_GROUP, 0),
        .flags = 0,
    };
    static const uint32_t rates = (IWM_SCAN_CONFIG_RATE_1M |
                                   IWM_SCAN_CONFIG_RATE_2M | IWM_SCAN_CONFIG_RATE_5M |
                                   IWM_SCAN_CONFIG_RATE_11M | IWM_SCAN_CONFIG_RATE_6M |
                                   IWM_SCAN_CONFIG_RATE_9M | IWM_SCAN_CONFIG_RATE_12M |
                                   IWM_SCAN_CONFIG_RATE_18M | IWM_SCAN_CONFIG_RATE_24M |
                                   IWM_SCAN_CONFIG_RATE_36M | IWM_SCAN_CONFIG_RATE_48M |
                                   IWM_SCAN_CONFIG_RATE_54M);
    
    cmd_size = sizeof(*scan_config) + sc->sc_capa_n_scan_channels;
    
    scan_config = (struct iwm_scan_config*)malloc(cmd_size, M_DEVBUF, M_WAIT);
    if (scan_config == NULL)
        return ENOMEM;
    bzero(scan_config, cmd_size);
    
    scan_config->tx_chains = htole32(iwm_fw_valid_tx_ant(sc));
    scan_config->rx_chains = htole32(iwm_fw_valid_rx_ant(sc));
    scan_config->legacy_rates = htole32(rates |
                                        IWM_SCAN_CONFIG_SUPPORTED_RATE(rates));
    
    /* These timings correspond to iwlwifi's UNASSOC scan. */
    scan_config->dwell_active = 10;
    scan_config->dwell_passive = 110;
    scan_config->dwell_fragmented = 44;
    scan_config->dwell_extended = 90;
    scan_config->out_of_channel_time = htole32(0);
    scan_config->suspend_time = htole32(0);
    
    IEEE80211_ADDR_COPY(scan_config->mac_addr, sc->sc_ic.ic_myaddr);
    
    scan_config->bcast_sta_id = IWM_AUX_STA_ID;
    scan_config->channel_flags = 0;
    
    for (c = &ic->ic_channels[1], nchan = 0;
         c <= &ic->ic_channels[IEEE80211_CHAN_MAX] &&
         nchan < sc->sc_capa_n_scan_channels; c++) {
        if (c->ic_flags == 0)
            continue;
        scan_config->channel_array[nchan++] =
        ieee80211_mhz2ieee(c->ic_freq, 0);
    }
    
    scan_config->flags = htole32(IWM_SCAN_CONFIG_FLAG_ACTIVATE |
                                 IWM_SCAN_CONFIG_FLAG_ALLOW_CHUB_REQS |
                                 IWM_SCAN_CONFIG_FLAG_SET_TX_CHAINS |
                                 IWM_SCAN_CONFIG_FLAG_SET_RX_CHAINS |
                                 IWM_SCAN_CONFIG_FLAG_SET_AUX_STA_ID |
                                 IWM_SCAN_CONFIG_FLAG_SET_ALL_TIMES |
                                 IWM_SCAN_CONFIG_FLAG_SET_LEGACY_RATES |
                                 IWM_SCAN_CONFIG_FLAG_SET_MAC_ADDR |
                                 IWM_SCAN_CONFIG_FLAG_SET_CHANNEL_FLAGS|
                                 IWM_SCAN_CONFIG_N_CHANNELS(nchan) |
                                 IWM_SCAN_CONFIG_FLAG_CLEAR_FRAGMENTED);
    
    hcmd.data[0] = scan_config;
    hcmd.len[0] = cmd_size;
    
    err = iwm_send_cmd(sc, &hcmd);
    ::free(scan_config);
    return err;
}

int ItlIwm::
iwm_umac_scan_size(struct iwm_softc *sc)
{
    int base_size = IWM_SCAN_REQ_UMAC_SIZE_V1;
    int tail_size;
    
    if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_ADAPTIVE_DWELL_V2))
        base_size = IWM_SCAN_REQ_UMAC_SIZE_V8;
    else if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_ADAPTIVE_DWELL))
        base_size = IWM_SCAN_REQ_UMAC_SIZE_V7;
    if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_SCAN_EXT_CHAN_VER))
        tail_size = sizeof(struct iwm_scan_req_umac_tail_v2);
    else
        tail_size = sizeof(struct iwm_scan_req_umac_tail_v1);
    
    return base_size + sizeof(struct iwm_scan_channel_cfg_umac) *
    sc->sc_capa_n_scan_channels + tail_size;
}

struct iwm_scan_umac_chan_param *ItlIwm::
iwm_get_scan_req_umac_chan_param(struct iwm_softc *sc,
                                 struct iwm_scan_req_umac *req)
{
    if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_ADAPTIVE_DWELL_V2))
        return &req->v8.channel;
    
    if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_ADAPTIVE_DWELL))
        return &req->v7.channel;
    return &req->v1.channel;
}

void *ItlIwm::
iwm_get_scan_req_umac_data(struct iwm_softc *sc, struct iwm_scan_req_umac *req)
{
    if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_ADAPTIVE_DWELL_V2))
        return (void *)&req->v8.data;
    
    if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_ADAPTIVE_DWELL))
        return (void *)&req->v7.data;
    return (void *)&req->v1.data;
    
}

/* adaptive dwell max budget time [TU] for full scan */
#define IWM_SCAN_ADWELL_MAX_BUDGET_FULL_SCAN 300
/* adaptive dwell max budget time [TU] for directed scan */
#define IWM_SCAN_ADWELL_MAX_BUDGET_DIRECTED_SCAN 100
/* adaptive dwell default high band APs number */
#define IWM_SCAN_ADWELL_DEFAULT_HB_N_APS 8
/* adaptive dwell default low band APs number */
#define IWM_SCAN_ADWELL_DEFAULT_LB_N_APS 2
/* adaptive dwell default APs number in social channels (1, 6, 11) */
#define IWM_SCAN_ADWELL_DEFAULT_N_APS_SOCIAL 10

int ItlIwm::
iwm_umac_scan(struct iwm_softc *sc, int bgscan, uint64_t scan_serial)
{
    ItlScanCommandPolicy policy = {};
    if (!copyScanCommandPolicy(scan_serial, &policy))
        return ECANCELED;
    const struct ieee80211_wcl_scan_plan &wclPlan = policy.plan;
    const bool exactWclPlan = wclPlan.active != 0;
    const uint8_t scanSsidLength = wclPlan.ssid_len;
    const uint8_t *scanSsid = wclPlan.ssid;
    const bool activeScan = exactWclPlan ?
        wclPlan.scan_type != IEEE80211_WCL_SCAN_TYPE_PASSIVE :
        scanSsidLength != 0;
    const bool directedSsid = activeScan && scanSsidLength != 0;
    const uint8_t activeDwell = static_cast<uint8_t>(
        ieee80211_wcl_scan_time_or_default(
            exactWclPlan ? wclPlan.active_dwell_ms : 0, 10));
    const uint8_t passiveDwell = static_cast<uint8_t>(
        ieee80211_wcl_scan_time_or_default(
            exactWclPlan ? wclPlan.passive_dwell_ms : 0, 110));
    const uint32_t homeAwayMs = policy.homeAwayMs;
    const uint32_t suspendMs = ieee80211_wcl_scan_time_or_default(
        exactWclPlan ? wclPlan.home_dwell_ms : 0, homeAwayMs);
    struct iwm_host_cmd hcmd = {
        .id = iwm_cmd_id(IWM_SCAN_REQ_UMAC, IWM_LONG_GROUP, 0),
        .len = { 0, },
        .data = { NULL, },
        .flags = 0,
    };
    struct iwm_scan_req_umac *req;
    void *cmd_data, *tail_data;
    struct iwm_scan_req_umac_tail_v2 *tail;
    struct iwm_scan_req_umac_tail_v1 *tailv1;
    struct iwm_scan_umac_chan_param *chanparam;
    size_t req_len;
    int err, async = bgscan;
    
    req_len = iwm_umac_scan_size(sc);
    if ((req_len < IWM_SCAN_REQ_UMAC_SIZE_V1 +
         sizeof(struct iwm_scan_req_umac_tail_v1)) ||
        req_len > IWM_MAX_CMD_PAYLOAD_SIZE)
        return ERANGE;
    req = (struct iwm_scan_req_umac*)malloc(req_len, M_DEVBUF, M_NOWAIT);
    if (req == NULL)
        return ENOMEM;
    bzero(req, req_len);
    
    hcmd.len[0] = (uint16_t)req_len;
    hcmd.data[0] = (void *)req;
    hcmd.flags |= async ? IWM_CMD_ASYNC : 0;
    
    if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_ADAPTIVE_DWELL)) {
        req->v7.adwell_default_n_aps_social =
        IWM_SCAN_ADWELL_DEFAULT_N_APS_SOCIAL;
        req->v7.adwell_default_n_aps =
        IWM_SCAN_ADWELL_DEFAULT_LB_N_APS;
        
        if (directedSsid)
            req->v7.adwell_max_budget =
            htole16(IWM_SCAN_ADWELL_MAX_BUDGET_DIRECTED_SCAN);
        else
            req->v7.adwell_max_budget =
            htole16(IWM_SCAN_ADWELL_MAX_BUDGET_FULL_SCAN);
        
        req->v7.scan_priority = htole32(IWM_SCAN_PRIORITY_HIGH);
        req->v7.max_out_time[IWM_SCAN_LB_LMAC_IDX] = 0;
        req->v7.suspend_time[IWM_SCAN_LB_LMAC_IDX] = 0;
        
        if (isset(sc->sc_ucode_api,
                  IWM_UCODE_TLV_API_ADAPTIVE_DWELL_V2)) {
            req->v8.active_dwell[IWM_SCAN_LB_LMAC_IDX] = activeDwell;
            req->v8.passive_dwell[IWM_SCAN_LB_LMAC_IDX] = passiveDwell;
        } else {
            req->v7.active_dwell = activeDwell;
            req->v7.passive_dwell = passiveDwell;
            req->v7.fragmented_dwell = 44;
        }
    } else {
        /* These timings correspond to iwlwifi's UNASSOC scan. */
        req->v1.active_dwell = activeDwell;
        req->v1.passive_dwell = passiveDwell;
        req->v1.fragmented_dwell = 44;
        req->v1.extended_dwell = 90;

        req->v1.scan_priority = htole32(IWM_SCAN_PRIORITY_HIGH);
    }
    
    if (bgscan) {
        const uint32_t maxOutTime = htole32(homeAwayMs);
        const uint32_t suspendTime = htole32(suspendMs);
        if (isset(sc->sc_ucode_api,
                  IWM_UCODE_TLV_API_ADAPTIVE_DWELL_V2)) {
            req->v8.max_out_time[IWM_SCAN_LB_LMAC_IDX] = maxOutTime;
            req->v8.suspend_time[IWM_SCAN_LB_LMAC_IDX] = suspendTime;
        } else if (isset(sc->sc_ucode_api,
                         IWM_UCODE_TLV_API_ADAPTIVE_DWELL)) {
            req->v7.max_out_time[IWM_SCAN_LB_LMAC_IDX] = maxOutTime;
            req->v7.suspend_time[IWM_SCAN_LB_LMAC_IDX] = suspendTime;
        } else {
            req->v1.max_out_time = maxOutTime;
            req->v1.suspend_time = suspendTime;
        }
    }

    req->ooc_priority = htole32(IWM_SCAN_PRIORITY_HIGH);
    
    cmd_data = iwm_get_scan_req_umac_data(sc, req);
    chanparam = iwm_get_scan_req_umac_chan_param(sc, req);
    chanparam->count = iwm_umac_scan_fill_channels(sc,
                                                   (struct iwm_scan_channel_cfg_umac *)cmd_data,
                                                   activeScan ? 1 : 0, bgscan, &wclPlan);
    if (chanparam->count == 0) {
        ::free(req);
        return EINVAL;
    }
    chanparam->flags = 0;
    
    tail_data = (uint8_t*)cmd_data + sizeof(struct iwm_scan_channel_cfg_umac) *
    sc->sc_capa_n_scan_channels;
    tail = (struct iwm_scan_req_umac_tail_v2*)((uint8_t*)tail_data);
    /* tail v1 layout differs in preq and direct_scan member fields. */
    tailv1 = (struct iwm_scan_req_umac_tail_v1*)((uint8_t*)tail_data);
    
    req->general_flags = htole32(IWM_UMAC_SCAN_GEN_FLAGS_PASS_ALL |
                                 IWM_UMAC_SCAN_GEN_FLAGS_ITER_COMPLETE);
    if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_ADAPTIVE_DWELL_V2)) {
        req->v8.general_flags2 =
        IWM_UMAC_SCAN_GEN_FLAGS2_ALLOW_CHNL_REORDER;
    }
    
    /* Select SSID zero for both directed and wildcard active scans. */
    if (activeScan) {
        if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_SCAN_EXT_CHAN_VER)) {
            tail->direct_scan[0].id = IEEE80211_ELEMID_SSID;
            tail->direct_scan[0].len = scanSsidLength;
            if (scanSsidLength != 0)
                memcpy(tail->direct_scan[0].ssid, scanSsid,
                       scanSsidLength);
        } else {
            tailv1->direct_scan[0].id = IEEE80211_ELEMID_SSID;
            tailv1->direct_scan[0].len = scanSsidLength;
            if (scanSsidLength != 0)
                memcpy(tailv1->direct_scan[0].ssid, scanSsid,
                       scanSsidLength);
        }
    }
    if (directedSsid)
        req->general_flags |=
            htole32(IWM_UMAC_SCAN_GEN_FLAGS_PRE_CONNECT);
    if (!activeScan)
        req->general_flags |= htole32(IWM_UMAC_SCAN_GEN_FLAGS_PASSIVE);
    
    if (isset(sc->sc_enabled_capa,
              IWM_UCODE_TLV_CAPA_DS_PARAM_SET_IE_SUPPORT) &&
              isset(sc->sc_enabled_capa,
              IWM_UCODE_TLV_CAPA_WFA_TPC_REP_IE_SUPPORT))
        req->general_flags |=
        htole32(IWM_UMAC_SCAN_GEN_FLAGS_RRM_ENABLED);
    
    if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_ADAPTIVE_DWELL)) {
        req->general_flags |=
        htole32(IWM_UMAC_SCAN_GEN_FLAGS_ADAPTIVE_DWELL);
    } else {
        req->general_flags |=
        htole32(IWM_UMAC_SCAN_GEN_FLAGS_EXTENDED_DWELL);
    }
    
    if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_SCAN_EXT_CHAN_VER))
        err = iwm_fill_probe_req(sc, &tail->preq);
    else
        err = iwm_fill_probe_req_v1(sc, &tailv1->preq);
    if (err) {
        ::free(req);
        return err;
    }
    
    /* Specify the scan plan: We'll do one iteration. */
    tail->schedule[0].interval = 0;
    tail->schedule[0].iter_count = 1;
    
    hcmd.scan_serial = scan_serial;
    err = iwm_send_cmd(sc, &hcmd);
    ::free(req);
    return err;
}

void ItlIwm::
iwm_mcc_update(struct iwm_softc *sc, struct iwm_mcc_chub_notif *notif)
{
    struct ieee80211com *ic = &sc->sc_ic;
    
    snprintf(sc->sc_fw_mcc, sizeof(sc->sc_fw_mcc), "%c%c",
             (le16toh(notif->mcc) & 0xff00) >> 8, le16toh(notif->mcc) & 0xff);
    if (sc->sc_fw_mcc_int != notif->mcc && sc->sc_ic.ic_event_handler) {
        (*sc->sc_ic.ic_event_handler)(&sc->sc_ic, IEEE80211_EVT_COUNTRY_CODE_UPDATE, NULL);
    }
    sc->sc_fw_mcc_int = notif->mcc;
    
    /* TODO: Schedule a task to send MCC_UPDATE_CMD? */
}

uint8_t ItlIwm::
iwm_ridx2rate(struct ieee80211_rateset *rs, int ridx)
{
    int i;
    uint8_t rval;
    
    for (i = 0; i < rs->rs_nrates; i++) {
        rval = (rs->rs_rates[i] & IEEE80211_RATE_VAL);
        if (rval == ieee80211_std_rateset_11g.rs_rates[ridx])
            return rs->rs_rates[i];
    }
    
    return 0;
}

void ItlIwm::
iwm_ack_rates(struct iwm_softc *sc, struct iwm_node *in, int *cck_rates,
              int *ofdm_rates)
{
    struct ieee80211_node *ni = &in->in_ni;
    struct ieee80211_rateset *rs = &ni->ni_rates;
    int lowest_present_ofdm = -1;
    int lowest_present_cck = -1;
    uint8_t cck = 0;
    uint8_t ofdm = 0;
    int i;
    
    if (ni->ni_chan == IEEE80211_CHAN_ANYC ||
        IEEE80211_IS_CHAN_2GHZ(ni->ni_chan)) {
        for (i = IWL_FIRST_CCK_RATE; i < IWL_FIRST_OFDM_RATE; i++) {
            if ((iwm_ridx2rate(rs, i) & IEEE80211_RATE_BASIC) == 0)
                continue;
            cck |= (1 << i);
            if (lowest_present_cck == -1 || lowest_present_cck > i)
                lowest_present_cck = i;
        }
    }
    for (i = IWL_FIRST_OFDM_RATE; i <= IWL_LAST_NON_HT_RATE; i++) {
        if ((iwm_ridx2rate(rs, i) & IEEE80211_RATE_BASIC) == 0)
            continue;
        ofdm |= (1 << (i - IWL_FIRST_OFDM_RATE));
        if (lowest_present_ofdm == -1 || lowest_present_ofdm > i)
            lowest_present_ofdm = i;
    }
    
    /*
     * Now we've got the basic rates as bitmaps in the ofdm and cck
     * variables. This isn't sufficient though, as there might not
     * be all the right rates in the bitmap. E.g. if the only basic
     * rates are 5.5 Mbps and 11 Mbps, we still need to add 1 Mbps
     * and 6 Mbps because the 802.11-2007 standard says in 9.6:
     *
     *    [...] a STA responding to a received frame shall transmit
     *    its Control Response frame [...] at the highest rate in the
     *    BSSBasicRateSet parameter that is less than or equal to the
     *    rate of the immediately previous frame in the frame exchange
     *    sequence ([...]) and that is of the same modulation class
     *    ([...]) as the received frame. If no rate contained in the
     *    BSSBasicRateSet parameter meets these conditions, then the
     *    control frame sent in response to a received frame shall be
     *    transmitted at the highest mandatory rate of the PHY that is
     *    less than or equal to the rate of the received frame, and
     *    that is of the same modulation class as the received frame.
     *
     * As a consequence, we need to add all mandatory rates that are
     * lower than all of the basic rates to these bitmaps.
     */
    
    if (IWL_RATE_24M_INDEX < lowest_present_ofdm)
        ofdm |= IWL_RATE_BIT_MSK(24) >> IWL_FIRST_OFDM_RATE;
    if (IWL_RATE_12M_INDEX < lowest_present_ofdm)
        ofdm |= IWL_RATE_BIT_MSK(12) >> IWL_FIRST_OFDM_RATE;
    /* 6M already there or needed so always add */
    ofdm |= IWL_RATE_BIT_MSK(6) >> IWL_FIRST_OFDM_RATE;
    
    /*
     * CCK is a bit more complex with DSSS vs. HR/DSSS vs. ERP.
     * Note, however:
     *  - if no CCK rates are basic, it must be ERP since there must
     *    be some basic rates at all, so they're OFDM => ERP PHY
     *    (or we're in 5 GHz, and the cck bitmap will never be used)
     *  - if 11M is a basic rate, it must be ERP as well, so add 5.5M
     *  - if 5.5M is basic, 1M and 2M are mandatory
     *  - if 2M is basic, 1M is mandatory
     *  - if 1M is basic, that's the only valid ACK rate.
     * As a consequence, it's not as complicated as it sounds, just add
     * any lower rates to the ACK rate bitmap.
     */
    if (IWL_RATE_11M_INDEX < lowest_present_cck)
        cck |= IWL_RATE_BIT_MSK(11) >> IWL_FIRST_CCK_RATE;
    if (IWL_RATE_5M_INDEX < lowest_present_cck)
        cck |= IWL_RATE_BIT_MSK(5) >> IWL_FIRST_CCK_RATE;
    if (IWL_RATE_2M_INDEX < lowest_present_cck)
        cck |= IWL_RATE_BIT_MSK(2) >> IWL_FIRST_CCK_RATE;
    /* 1M already there or needed so always add */
    cck |= IWL_RATE_BIT_MSK(1) >> IWL_FIRST_CCK_RATE;
    
    *cck_rates = cck;
    *ofdm_rates = ofdm;
}

int ItlIwm::
iwm_scan(struct iwm_softc *sc, const ItlStateTransitionRequest &request)
{
    struct ieee80211com *ic = &sc->sc_ic;
    int err;

    if (!stateTransitionCurrent(request))
        return ECANCELED;

    /* Match the IWX AP-lifetime scan fence.  The foreground scan displaced
     * by HostAP remains an upper policy owner, but no firmware scan command
     * may overlap the live GO/link-station resource epoch. */
    if (isAPScanFenceActive() && deferScanCommand(request)) {
        XYLog("%s: IWM STA scan deferred by live AP radio fence state=%u "
              "flags=0x%x\n", DEVNAME(sc),
              static_cast<unsigned>(ic->ic_state),
              static_cast<unsigned>(sc->sc_flags));
        return 0;
    }
    
    if (!stateTransitionCurrent(request))
        return ECANCELED;
    if (scanCommandBackgroundPending()) {
        /* Pair firmware STOPPING with the host WCL reassociation terminal
         * before admitting the replacement foreground command. */
        if (ic->ic_wcl_reassoc_owner_active)
            err = ieee80211_cancel_wcl_reassoc_bgscan(ic, ECANCELED);
        else
            err = iwm_scan_abort(sc, true);
        if (err) {
            if (err == EBUSY && deferScanCommand(request))
                return 0;
            XYLog("%s: could not abort background scan\n",
                  DEVNAME(sc));
            return err;
        }
    }
    
    uint64_t scanSerial = 0;
    const bool umac = isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_UMAC_SCAN);
    err = reserveScanCommand(false, umac, &scanSerial, &request);
    if (err != 0) {
        if (err == EBUSY && deferScanCommand(request))
            return 0;
        noteWclInitialScanCommandRejected(0, request.scanGeneration);
        return err;
    }
    if (umac)
        err = iwm_umac_scan(sc, 0, scanSerial);
    else
        err = iwm_lmac_scan(sc, 0, scanSerial);
    if (err) {
        XYLog("%s: %d could not initiate scan, err=%d\n", DEVNAME(sc), __LINE__, err);
        noteWclInitialScanCommandRejected(scanSerial);
        rejectScanCommand(scanSerial);
        return err;
    }
    
    /*
     * The current mode might have been fixed during association.
     * Ensure all channels get scanned.
     */
    if (IFM_MODE(ic->ic_media.ifm_cur->ifm_media) == IFM_AUTO)
        ieee80211_setmode(ic, IEEE80211_MODE_AUTO);
    
    if (!activateScanCommand(scanSerial, false))
        return 0;
    noteWclInitialScanCommandStarted(scanSerial);
    if (!scanCommandCurrent(scanSerial))
        return 0;
    if ((sc->sc_flags & IWM_FLAG_BGSCAN) == 0) {
        ieee80211_set_link_state(ic, LINK_STATE_DOWN);
        if (!scanCommandCurrent(scanSerial))
            return 0;
        ieee80211_node_cleanup(ic, ic->ic_bss);
    }
    if (!scanCommandCurrent(scanSerial))
        return 0;
    ic->ic_state = IEEE80211_S_SCAN;
    /* Availability consumers may submit immediately after this event.  Keep
     * the reference powerOn boundary below both command acceptance and the
     * committed lower SCAN state. */
    noteWclScanRadioReady(scanSerial);
    if (!scanCommandCurrent(scanSerial))
        return 0;
    iwm_led_blink_start(sc);
    wakeupOn(&ic->ic_state); /* wake iwm_init() */
    (void)readyScanCommand(scanSerial);
    
    return 0;
}

int ItlIwm::
iwm_bgscan(struct ieee80211com *ic)
{
    struct iwm_softc *sc = (struct iwm_softc *)IC2IFP(ic)->if_softc;
    ItlIwm *that = container_of(sc, ItlIwm, com);
    int err;

    if (that->isAPScanFenceActive()) {
        XYLog("%s: IWM STA background scan rejected by live AP radio "
              "fence\n", DEVNAME(sc));
        return EBUSY;
    }
    
    if (sc->sc_flags & IWM_FLAG_SCANNING)
        return 0;
    
    uint64_t scanSerial = 0;
    const bool umac = isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_UMAC_SCAN);
    err = that->reserveScanCommand(true, umac, &scanSerial);
    if (err != 0) {
        return err;
    }
    if (umac)
        err = that->iwm_umac_scan(sc, 1, scanSerial);
    else
        err = that->iwm_lmac_scan(sc, 1, scanSerial);
    if (err) {
        XYLog("%s: could not initiate scan\n", DEVNAME(sc));
        that->rejectScanCommand(scanSerial);
        return err;
    }
    
    if (!that->activateScanCommand(scanSerial, true))
        return ENXIO;
    that->noteWclBackgroundScanCommandStarted(scanSerial);
    (void)that->readyScanCommand(scanSerial);
    return 0;
}

int ItlIwm::
iwm_bgscan_abort(struct ieee80211com *ic)
{
    struct iwm_softc *sc;
    ItlIwm *that;

    if (ic == NULL || (sc = (struct iwm_softc *)ic->ic_softc) == NULL)
        return EINVAL;
    that = container_of(sc, ItlIwm, com);
    /* Claim the physical kind and abort together; a replacement foreground
     * command is never part of this cancelled background scan. */
    return that->iwm_scan_abort(sc, true);
}

int ItlIwm::
iwm_umac_scan_abort_status(struct iwm_softc *sc, uint32_t *status,
    uint64_t serial)
{
    struct iwm_umac_scan_abort cmd = { 0 };
    struct iwm_host_cmd hcmd = {
        .scan_serial = serial,
        .id = IWM_WIDE_ID(IWM_LONG_GROUP, IWM_SCAN_ABORT_UMAC),
        .len = { sizeof(cmd), },
        .data = { &cmd, },
    };
    if (status == NULL)
        return EINVAL;
    *status = IWM_UMAC_SCAN_ABORT_STATUS_NOT_FOUND;
    if (serial == 0)
        return 0;
    const int error = iwm_send_cmd_status(sc, &hcmd, status);
    /* The exact physical notification can win before abort publication.
     * NOT_FOUND still needs host terminal reconciliation, never UID reuse. */
    return error == EALREADY ? 0 : error;
}

int ItlIwm::
iwm_umac_scan_abort(struct iwm_softc *sc)
{
    const uint32_t generation = sc->sc_generation;
    uint64_t serial = 0;
    int error = reserveScanCommandAbort(false, &serial);
    if (error != 0 || serial == 0)
        return error;
    uint32_t status;
    error = iwm_umac_scan_abort_status(sc, &status, serial);
    if (error == 0 &&
        status != IWM_UMAC_SCAN_ABORT_STATUS_SUCCESS &&
        status != IWM_UMAC_SCAN_ABORT_STATUS_IN_PROGRESS &&
        status != IWM_UMAC_SCAN_ABORT_STATUS_NOT_FOUND)
        error = EIO;
    if (error != 0) {
        if (!scanCommandCurrent(serial) &&
            generation == static_cast<uint32_t>(sc->sc_generation))
            return 0;
        rejectScanCommand(serial);
    }
    return error;
}

int ItlIwm::
iwm_lmac_scan_abort(struct iwm_softc *sc, uint64_t serial)
{
    const uint32_t generation = sc->sc_generation;
    if (serial == 0) {
        const int error = reserveScanCommandAbort(false, &serial);
        if (error != 0 || serial == 0)
            return error;
    }
    struct iwm_host_cmd cmd = {
        .scan_serial = serial,
        .id = IWM_SCAN_OFFLOAD_ABORT_CMD,
    };
    uint32_t status = 0;
    const int error = iwm_send_cmd_status(sc, &cmd, &status);
    if (error == EALREADY)
        return 0;
    if (error != 0) {
        if (!scanCommandCurrent(serial) &&
            generation == static_cast<uint32_t>(sc->sc_generation))
            return 0;
        rejectScanCommand(serial);
        return error;
    }
    /* Firmware status 2 is NOT_FOUND, not a physical host terminal.
     * The blocking owner still waits; AP handoff still needs RX completion. */
    if (status != IWM_CAN_ABORT_STATUS && status != 2) {
        rejectScanCommand(serial);
        return EIO;
    }
    return 0;
}

int ItlIwm::
iwm_scan_abort(struct iwm_softc *sc, bool backgroundOnly)
{
    const uint32_t generation = sc->sc_generation;
    uint64_t serial = 0;
    int error = reserveScanCommandAbort(true, &serial, backgroundOnly);
    if (error != 0 || serial == 0)
        return error;
    uint32_t status = IWM_UMAC_SCAN_ABORT_STATUS_NOT_FOUND;
    const bool umac = isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_UMAC_SCAN);
    if (umac)
        error = iwm_umac_scan_abort_status(sc, &status, serial);
    else
        error = iwm_lmac_scan_abort(sc, serial);
    if (error == 0 && umac &&
        status != IWM_UMAC_SCAN_ABORT_STATUS_SUCCESS &&
        status != IWM_UMAC_SCAN_ABORT_STATUS_IN_PROGRESS &&
        status != IWM_UMAC_SCAN_ABORT_STATUS_NOT_FOUND)
        error = EIO;
    if (error != 0) {
        if (!scanCommandCurrent(serial) &&
            generation == static_cast<uint32_t>(sc->sc_generation))
            return 0;
        rejectScanCommand(serial);
        return error;
    }
    return waitScanCommandAbort(serial, generation);
}
