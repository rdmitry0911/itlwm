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

struct iwm_phy_db_entry * ItlIwm::
iwm_phy_db_get_section(struct iwm_softc *sc, uint16_t type, uint16_t chg_id)
{
    struct iwm_phy_db *phy_db = &sc->sc_phy_db;
    
    if (type >= IWM_PHY_DB_MAX)
        return NULL;
    
    switch (type) {
        case IWM_PHY_DB_CFG:
            return &phy_db->cfg;
        case IWM_PHY_DB_CALIB_NCH:
            return &phy_db->calib_nch;
        case IWM_PHY_DB_CALIB_CHG_PAPD:
            if (chg_id >= IWM_NUM_PAPD_CH_GROUPS)
                return NULL;
            return &phy_db->calib_ch_group_papd[chg_id];
        case IWM_PHY_DB_CALIB_CHG_TXP:
            if (chg_id >= IWM_NUM_TXP_CH_GROUPS)
                return NULL;
            return &phy_db->calib_ch_group_txp[chg_id];
        default:
            return NULL;
    }
    return NULL;
}

int ItlIwm::
iwm_phy_db_set_section(struct iwm_softc *sc,
                       struct iwm_calib_res_notif_phy_db *phy_db_notif)
{
    uint16_t type = le16toh(phy_db_notif->type);
    uint16_t size  = le16toh(phy_db_notif->length);
    struct iwm_phy_db_entry *entry;
    uint16_t chg_id = 0;
    
    if (type == IWM_PHY_DB_CALIB_CHG_PAPD ||
        type == IWM_PHY_DB_CALIB_CHG_TXP)
        chg_id = le16toh(*(uint16_t *)phy_db_notif->data);
    
    entry = iwm_phy_db_get_section(sc, type, chg_id);
    if (!entry)
        return EINVAL;
    
    if (entry->data)
        ::free(entry->data);
    entry->data = (uint8_t*)malloc(size, M_DEVBUF, M_NOWAIT);
    if (!entry->data) {
        entry->size = 0;
        return ENOMEM;
    }
    memcpy(entry->data, phy_db_notif->data, size);
    entry->size = size;
    
    return 0;
}

int ItlIwm::
iwm_phy_db_get_section_data(struct iwm_softc *sc, uint32_t type, uint8_t **data,
                            uint16_t *size, uint16_t ch_id)
{
    struct iwm_phy_db_entry *entry;
    uint16_t ch_group_id = 0;
    
    if (type == IWM_PHY_DB_CALIB_CHG_PAPD)
        ch_group_id = iwm_channel_id_to_papd(ch_id);
    else if (type == IWM_PHY_DB_CALIB_CHG_TXP)
        ch_group_id = iwm_channel_id_to_txp(sc, ch_id);
    
    entry = iwm_phy_db_get_section(sc, type, ch_group_id);
    if (!entry)
        return EINVAL;
    
    *data = entry->data;
    *size = entry->size;
    
    return 0;
}

int ItlIwm::
iwm_send_phy_db_cmd(struct iwm_softc *sc, uint16_t type, uint16_t length,
                    void *data)
{
    struct iwm_phy_db_cmd phy_db_cmd;
    struct iwm_host_cmd cmd = {
        .id = IWM_PHY_DB_CMD,
        .flags = IWM_CMD_ASYNC,
    };
    
    phy_db_cmd.type = le16toh(type);
    phy_db_cmd.length = le16toh(length);
    
    cmd.data[0] = &phy_db_cmd;
    cmd.len[0] = sizeof(struct iwm_phy_db_cmd);
    cmd.data[1] = data;
    cmd.len[1] = length;
    
    return iwm_send_cmd(sc, &cmd);
}

int ItlIwm::
iwm_phy_db_send_all_channel_groups(struct iwm_softc *sc, uint16_t type,
                                   uint8_t max_ch_groups)
{
    uint16_t i;
    int err;
    struct iwm_phy_db_entry *entry;
    
    for (i = 0; i < max_ch_groups; i++) {
        entry = iwm_phy_db_get_section(sc, type, i);
        if (!entry)
            return EINVAL;
        
        if (!entry->size)
            continue;
        
        err = iwm_send_phy_db_cmd(sc, type, entry->size, entry->data);
        if (err)
            return err;
        
        DELAY(1000);
    }
    
    return 0;
}

int ItlIwm::
iwm_send_phy_db_data(struct iwm_softc *sc)
{
    uint8_t *data = NULL;
    uint16_t size = 0;
    int err;
    
    err = iwm_phy_db_get_section_data(sc, IWM_PHY_DB_CFG, &data, &size, 0);
    if (err)
        return err;
    
    err = iwm_send_phy_db_cmd(sc, IWM_PHY_DB_CFG, size, data);
    if (err)
        return err;
    
    err = iwm_phy_db_get_section_data(sc, IWM_PHY_DB_CALIB_NCH,
                                      &data, &size, 0);
    if (err)
        return err;
    
    err = iwm_send_phy_db_cmd(sc, IWM_PHY_DB_CALIB_NCH, size, data);
    if (err)
        return err;
    
    err = iwm_phy_db_send_all_channel_groups(sc,
                                             IWM_PHY_DB_CALIB_CHG_PAPD, IWM_NUM_PAPD_CH_GROUPS);
    if (err)
        return err;
    
    err = iwm_phy_db_send_all_channel_groups(sc,
                                             IWM_PHY_DB_CALIB_CHG_TXP, IWM_NUM_TXP_CH_GROUPS);
    if (err)
        return err;
    
    return 0;
}

/*
 * For the high priority TE use a time event type that has similar priority to
 * the FW's action scan priority.
 */
#define IWM_ROC_TE_TYPE_NORMAL IWM_TE_P2P_DEVICE_DISCOVERABLE
#define IWM_ROC_TE_TYPE_MGMT_TX IWM_TE_P2P_CLIENT_ASSOC

int ItlIwm::
iwm_send_time_event_cmd(struct iwm_softc *sc,
                        const struct iwm_time_event_cmd *cmd)
{
    struct iwm_rx_packet *pkt;
    struct iwm_time_event_resp *resp;
    struct iwm_host_cmd hcmd = {
        .id = IWM_TIME_EVENT_CMD,
        .flags = IWM_CMD_WANT_RESP,
        .resp_pkt_len = sizeof(*pkt) + sizeof(*resp),
    };
    uint32_t resp_len;
    int err;
    
    hcmd.data[0] = cmd;
    hcmd.len[0] = sizeof(*cmd);
    err = iwm_send_cmd(sc, &hcmd);
    if (err)
        return err;
    
    pkt = hcmd.resp_pkt;
    if (!pkt || (pkt->hdr.flags & IWM_CMD_FAILED_MSK)) {
        err = EIO;
        goto out;
    }
    
    resp_len = iwm_rx_packet_payload_len(pkt);
    if (resp_len != sizeof(*resp)) {
        err = EIO;
        goto out;
    }
    
    resp = (struct iwm_time_event_resp *)pkt->data;
    if (le32toh(resp->status) == 0)
        sc->sc_time_event_uid = le32toh(resp->unique_id);
    else
        err = EIO;
out:
    iwm_free_resp(sc, &hcmd);
    return err;
}

void ItlIwm::
iwm_protect_session(struct iwm_softc *sc, struct iwm_node *in,
                    uint32_t duration, uint32_t max_delay)
{
    struct iwm_time_event_cmd time_cmd;
    
    /* Do nothing if a time event is already scheduled. */
    if (sc->sc_flags & IWM_FLAG_TE_ACTIVE)
        return;
    
    memset(&time_cmd, 0, sizeof(time_cmd));
    
    time_cmd.action = htole32(IWM_FW_CTXT_ACTION_ADD);
    time_cmd.id_and_color =
    htole32(IWM_FW_CMD_ID_AND_COLOR(in->in_id, in->in_color));
    time_cmd.id = htole32(IWM_TE_BSS_STA_AGGRESSIVE_ASSOC);
    
    time_cmd.apply_time = htole32(0);
    
    time_cmd.max_frags = IWM_TE_V2_FRAG_NONE;
    time_cmd.max_delay = htole32(max_delay);
    /* TODO: why do we need to interval = bi if it is not periodic? */
    time_cmd.interval = htole32(1);
    time_cmd.duration = htole32(duration);
    time_cmd.repeat = 1;
    time_cmd.policy
    = htole16(IWM_TE_V2_NOTIF_HOST_EVENT_START |
              IWM_TE_V2_NOTIF_HOST_EVENT_END |
              IWM_T2_V2_START_IMMEDIATELY);
    
    if (iwm_send_time_event_cmd(sc, &time_cmd) == 0)
        sc->sc_flags |= IWM_FLAG_TE_ACTIVE;
    
    DELAY(100);
}

void ItlIwm::
iwm_unprotect_session(struct iwm_softc *sc, struct iwm_node *in)
{
    struct iwm_time_event_cmd time_cmd;
    
    /* Do nothing if the time event has already ended. */
    if ((sc->sc_flags & IWM_FLAG_TE_ACTIVE) == 0)
        return;
    
    memset(&time_cmd, 0, sizeof(time_cmd));
    
    time_cmd.action = htole32(IWM_FW_CTXT_ACTION_REMOVE);
    time_cmd.id_and_color =
    htole32(IWM_FW_CMD_ID_AND_COLOR(in->in_id, in->in_color));
    time_cmd.id = htole32(sc->sc_time_event_uid);
    
    if (iwm_send_time_event_cmd(sc, &time_cmd) == 0)
        sc->sc_flags &= ~IWM_FLAG_TE_ACTIVE;
    
    DELAY(100);
}

int ItlIwm::
iwm_send_phy_cfg_cmd(struct iwm_softc *sc)
{
    struct iwm_phy_cfg_cmd phy_cfg_cmd;
    enum iwm_ucode_type ucode_type = sc->sc_uc_current;
    
    phy_cfg_cmd.phy_cfg = htole32(sc->sc_fw_phy_config);
    phy_cfg_cmd.calib_control.event_trigger =
    sc->sc_default_calib[ucode_type].event_trigger;
    phy_cfg_cmd.calib_control.flow_trigger =
    sc->sc_default_calib[ucode_type].flow_trigger;
    
    return iwm_send_cmd_pdu(sc, IWM_PHY_CONFIGURATION_CMD, 0,
                            sizeof(phy_cfg_cmd), &phy_cfg_cmd);
}

int ItlIwm::
iwm_send_dqa_cmd(struct iwm_softc *sc)
{
    struct iwm_dqa_enable_cmd dqa_cmd = {
        .cmd_queue = htole32(IWM_DQA_CMD_QUEUE),
    };
    uint32_t cmd_id;
    
    cmd_id = iwm_cmd_id(IWM_DQA_ENABLE_CMD, IWM_DATA_PATH_GROUP, 0);
    return iwm_send_cmd_pdu(sc, cmd_id, 0, sizeof(dqa_cmd), &dqa_cmd);
}

int ItlIwm::
iwm_binding_cmd(struct iwm_softc *sc, struct iwm_node *in, uint32_t action)
{
    using Lease = ItlFirmwareContextLease;
    const bool remove = action == IWM_FW_CTXT_ACTION_REMOVE;
    if (action != IWM_FW_CTXT_ACTION_ADD && !remove)
        return EINVAL;
    struct ieee80211com *ic = &sc->sc_ic;
    IOSimpleLock *ownerLock = ic->ic_pae_selected_bss_lock;
    if (wclScanLock == NULL || (!remove && ownerLock == NULL))
        return ENXIO;
    IOInterruptState ownerIrq = 0;
    if (!remove)
        ownerIrq = IOSimpleLockLockDisableInterrupt(ownerLock);
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    const uint32_t generation = sc->sc_generation;
    ItlFirmwareContextIdentity identity = {};
    ItlFirmwareContextReceipt receipt = {};
    int error = 0;
    if (sc->sc_flags & IWM_FLAG_SHUTDOWN)
        error = ENXIO;
    else if (remove) {
        identity = primaryBindingContext.owner.identity;
        if (primaryStationContext.occupied() || (sc->sc_flags & IWM_FLAG_STA_ACTIVE))
            error = EBUSY;
    } else if (in == NULL || in->in_phyctxt == NULL ||
               in->in_phyctxt->channel == NULL) {
        error = EINVAL;
    } else {
        identity.attempt = ItlScanCommandPolicy::identityLocked(ic);
        identity.mac = IWM_FW_CMD_ID_AND_COLOR(in->in_id, in->in_color);
        identity.mode = ic->ic_opmode;
        identity.commandLength = sizeof(struct iwm_mac_ctx_cmd);
        IEEE80211_ADDR_COPY(identity.peer, in->in_macaddr);
        if (primaryMacContext.stage != Lease::Stage::Active ||
            primaryMacContext.owner.generation != generation ||
            !primaryMacContext.owner.identity.equals(identity)) {
            error = EBUSY;
        } else {
            identity.phy = IWM_FW_CMD_ID_AND_COLOR(
                in->in_phyctxt->id, in->in_phyctxt->color);
            identity.lmac = IEEE80211_IS_CHAN_2GHZ(in->in_phyctxt->channel) ||
                !isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_CDB_SUPPORT) ?
                IWM_LMAC_24G_INDEX : IWM_LMAC_5G_INDEX;
            identity.commandLength = isset(sc->sc_enabled_capa,
                IWM_UCODE_TLV_CAPA_BINDING_CDB_SUPPORT) ?
                sizeof(struct iwm_binding_cmd) : sizeof(struct iwm_binding_cmd_v1);
        }
    }
    Lease::Admission admission = Lease::Admission::Missing;
    if (error == 0) {
        admission = primaryBindingContext.begin(remove ? Lease::Operation::Remove :
            Lease::Operation::Add, generation, identity, &receipt);
        if (admission == Lease::Admission::Busy)
            error = EBUSY;
        else if (admission == Lease::Admission::Missing)
            error = ENOENT;
        else if (admission == Lease::Admission::Exhausted)
            error = EOVERFLOW;
    }
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    if (!remove)
        IOSimpleLockUnlockEnableInterrupt(ownerLock, ownerIrq);
    if (error != 0 || admission == Lease::Admission::Already)
        return error;

    /* REMOVE names the old PHY even if the current node has disappeared.
     * Its membership list is empty: this binding has no remaining MAC. */
    struct iwm_binding_cmd cmd = {};
    cmd.id_and_color = htole32(receipt.identity.phy);
    cmd.action = htole32(action);
    cmd.phy = htole32(receipt.identity.phy);
    cmd.lmac_id = htole32(receipt.identity.lmac);
    for (unsigned i = 0; i < IWM_MAX_MACS_IN_BINDING; ++i)
        cmd.macs[i] = htole32(IWM_FW_CTXT_INVALID);
    if (!remove)
        cmd.macs[0] = htole32(receipt.identity.mac);
    uint32_t status = 0;
    ItlFirmwareContextCommand context = {
        receipt, ItlFirmwareContextCommand::Kind::Binding, remove, false
    };
    struct iwm_host_cmd hcmd = {};
    hcmd.context_command = &context;
    hcmd.id = IWM_BINDING_CONTEXT_CMD;
    hcmd.len[0] = static_cast<uint16_t>(receipt.identity.commandLength);
    hcmd.data[0] = &cmd;
    error = iwm_send_cmd_status(sc, &hcmd, &status);
    const Lease::Completion completion = error != 0 ?
        (context.submitted ? Lease::Completion::Uncertain : Lease::Completion::Rejected) :
        status != 0 ? Lease::Completion::Rejected : Lease::Completion::Success;
    if (error == 0 && status != 0)
        error = EIO;
    irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    if (!primaryBindingContext.finish(receipt, sc->sc_generation, completion))
        error = ENXIO;
    else if (primaryBindingContext.confirmed)
        sc->sc_flags |= IWM_FLAG_BINDING_ACTIVE;
    else
        sc->sc_flags &= ~IWM_FLAG_BINDING_ACTIVE;
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    return error;
}

/* The mutable ring is inspected only under the command leaf. */
static bool
iwm_cmdq_ring_valid(const struct iwm_softc *sc)
{
    return sc->cmdqid >= 0 && sc->cmdqid < (int)nitems(sc->txq) &&
        sc->txq[sc->cmdqid].desc != NULL &&
        sc->txq[sc->cmdqid].cmd != NULL &&
        sc->txq[sc->cmdqid].qid == sc->cmdqid;
}

/* Caller holds the radio lifecycle lock. Attach's NVM bootstrap has no init
 * reference and no UP/RUNNING interface; runtime requires the actual owner. */
static bool
iwm_cmdq_epoch_owner(const struct iwm_softc *sc, int generation)
{
    return !sc->sc_sae_tx_detaching &&
        (sc->sc_flags & IWM_FLAG_SHUTDOWN) == 0 &&
        sc->sc_radio_stop_refs == 0 && sc->sc_generation == generation &&
        (sc->sc_radio_init_refs == 1 ||
         (sc->sc_radio_init_refs == 0 &&
          (sc->sc_ic.ic_if.if_flags & (IFF_UP | IFF_RUNNING)) == 0));
}

int ItlIwm::
iwm_cmdq_init(struct iwm_softc *sc)
{
    sc->sc_cmdq_lock = IOSimpleLockAlloc();
    if (sc->sc_cmdq_lock == NULL)
        return ENOMEM;
    sc->sc_cmdq_next_serial = 0;
    sc->sc_cmdq_epoch = 1;
    sc->sc_cmdq_senders = sc->sc_cmdq_stoppers = 0;
    sc->sc_cmdq_generation = 0;
    sc->sc_cmdq_stopping = true;
    sc->sc_cmdq_detaching = false;
    memset(sc->sc_cmdq_slots, 0, sizeof(sc->sc_cmdq_slots));
    return 0;
}

bool ItlIwm::
iwm_cmdq_select(struct iwm_softc *sc, int qid, int generation)
{
    if (sc->sc_cmdq_lock == NULL || sc->sc_sae_tx_lifecycle_lock == NULL ||
        qid < 0 || qid >= (int)nitems(sc->txq))
        return false;
    IOLockLock(sc->sc_sae_tx_lifecycle_lock);
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(sc->sc_cmdq_lock);
    const bool selected = iwm_cmdq_epoch_owner(sc, generation) &&
        !sc->sc_cmdq_detaching && sc->sc_cmdq_stopping &&
        sc->sc_cmdq_senders == 0 && sc->sc_cmdq_stoppers == 0;
    if (selected)
        sc->cmdqid = qid;
    IOSimpleLockUnlockEnableInterrupt(sc->sc_cmdq_lock, irq);
    IOLockUnlock(sc->sc_sae_tx_lifecycle_lock);
    return selected;
}

bool ItlIwm::
iwm_cmdq_start(struct iwm_softc *sc, int generation)
{
    if (sc->sc_cmdq_lock == NULL || sc->sc_sae_tx_lifecycle_lock == NULL)
        return false;
    IOLockLock(sc->sc_sae_tx_lifecycle_lock);
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(sc->sc_cmdq_lock);
    bool started = iwm_cmdq_epoch_owner(sc, generation) &&
        !sc->sc_cmdq_detaching && sc->sc_cmdq_stopping &&
        sc->sc_cmdq_senders == 0 && sc->sc_cmdq_stoppers == 0 &&
        iwm_cmdq_ring_valid(sc) && sc->txq[sc->cmdqid].queued == 0;
    for (unsigned i = 0; started && i < nitems(sc->sc_cmdq_slots); i++)
        started = sc->sc_cmd_resp_pkt[i] == NULL &&
            sc->txq[sc->cmdqid].data[i].m == NULL;
    if (started) {
        if (++sc->sc_cmdq_epoch == 0)
            ++sc->sc_cmdq_epoch;
        memset(sc->sc_cmdq_slots, 0, sizeof(sc->sc_cmdq_slots));
        sc->sc_cmdq_generation = generation;
        sc->sc_cmdq_stopping = false;
    }
    IOSimpleLockUnlockEnableInterrupt(sc->sc_cmdq_lock, irq);
    IOLockUnlock(sc->sc_sae_tx_lifecycle_lock);
    return started;
}

bool ItlIwm::
iwm_cmdq_enter(struct iwm_softc *sc)
{
    if (sc->sc_cmdq_lock == NULL)
        return false;
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(sc->sc_cmdq_lock);
    const bool entered = !sc->sc_cmdq_stopping && !sc->sc_cmdq_detaching &&
        (sc->sc_flags & IWM_FLAG_SHUTDOWN) == 0 &&
        sc->sc_cmdq_generation == sc->sc_generation;
    if (entered)
        sc->sc_cmdq_senders++;
    IOSimpleLockUnlockEnableInterrupt(sc->sc_cmdq_lock, irq);
    return entered;
}

void ItlIwm::
iwm_cmdq_leave(struct iwm_softc *sc)
{
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(sc->sc_cmdq_lock);
    KASSERT(sc->sc_cmdq_senders != 0, "sc->sc_cmdq_senders != 0");
    sc->sc_cmdq_senders--;
    IOSimpleLockUnlockEnableInterrupt(sc->sc_cmdq_lock, irq);
    if (getMainCommandGate() != NULL)
        getMainCommandGate()->commandWakeup(sc, false);
}

void ItlIwm::
iwm_cmdq_stop(struct iwm_softc *sc)
{
    if (sc->sc_cmdq_lock == NULL)
        return;
    struct iwm_tfd *wake_desc = NULL;
    lockTsleep();
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(sc->sc_cmdq_lock);
    sc->sc_cmdq_stoppers++;
    if (!sc->sc_cmdq_stopping) {
        if (iwm_cmdq_ring_valid(sc))
            wake_desc = sc->txq[sc->cmdqid].desc;
        sc->sc_cmdq_stopping = true;
        if (++sc->sc_cmdq_epoch == 0)
            ++sc->sc_cmdq_epoch;
        for (unsigned i = 0; i < nitems(sc->sc_cmdq_slots); i++)
            if (sc->sc_cmdq_slots[i].state != IWM_CMD_SLOT_FREE)
                sc->sc_cmdq_slots[i].state = IWM_CMD_SLOT_ABORTED;
    }
    IOSimpleLockUnlockEnableInterrupt(sc->sc_cmdq_lock, irq);
    for (unsigned i = 0; wake_desc != NULL && i < IWM_TX_RING_COUNT; i++) {
        wakeupOn(&wake_desc[i]);
        if (getMainCommandGate() != NULL)
            getMainCommandGate()->commandWakeup(&wake_desc[i], false);
    }
    unlockTsleep();

    /* No descriptor, response or DMA may be reclaimed while a sender still
     * owns it. Release a caller's entire recursive controller gate while
     * draining; a sleeping sender must reacquire that gate before leaving. */
    for (;;) {
        irq = IOSimpleLockLockDisableInterrupt(sc->sc_cmdq_lock);
        const uint32_t senders = sc->sc_cmdq_senders;
        IOSimpleLockUnlockEnableInterrupt(sc->sc_cmdq_lock, irq);
        if (senders == 0)
            break;
        if (getMainCommandGate() != NULL && getMainWorkLoop() != NULL &&
            getMainWorkLoop()->inGate()) {
            AbsoluteTime deadline;
            clock_interval_to_deadline(10, kMillisecondScale,
                reinterpret_cast<uint64_t *>(&deadline));
            (void)getMainCommandGate()->commandSleep(sc, deadline, THREAD_UNINT);
        } else {
            IOSleep(1);
        }
    }
    for (unsigned i = 0; i < nitems(sc->sc_cmdq_slots); i++) {
        irq = IOSimpleLockLockDisableInterrupt(sc->sc_cmdq_lock);
        uint8_t *response = sc->sc_cmd_resp_pkt[i];
        sc->sc_cmd_resp_pkt[i] = NULL;
        sc->sc_cmd_resp_len[i] = 0;
        IOSimpleLockUnlockEnableInterrupt(sc->sc_cmdq_lock, irq);
        ::free(response);
    }
    irq = IOSimpleLockLockDisableInterrupt(sc->sc_cmdq_lock);
    KASSERT(sc->sc_cmdq_stoppers != 0, "sc->sc_cmdq_stoppers != 0");
    sc->sc_cmdq_stoppers--;
    IOSimpleLockUnlockEnableInterrupt(sc->sc_cmdq_lock, irq);
}

void ItlIwm::
iwm_cmdq_detach_begin(struct iwm_softc *sc)
{
    if (sc->sc_cmdq_lock != NULL) {
        IOInterruptState irq = IOSimpleLockLockDisableInterrupt(sc->sc_cmdq_lock);
        sc->sc_cmdq_detaching = true;
        IOSimpleLockUnlockEnableInterrupt(sc->sc_cmdq_lock, irq);
    }
    iwm_cmdq_stop(sc);
}

void ItlIwm::
iwm_cmdq_destroy(struct iwm_softc *sc)
{
    if (sc->sc_cmdq_lock == NULL)
        return;
    KASSERT(sc->sc_cmdq_stopping && sc->sc_cmdq_senders == 0 &&
        sc->sc_cmdq_stoppers == 0, "command owners drained");
    IOSimpleLockFree(sc->sc_cmdq_lock);
    sc->sc_cmdq_lock = NULL;
}

void ItlIwm::
iwm_cmdq_store_response(struct iwm_softc *sc, int qid, int idx, int code,
                         const struct iwm_rx_packet *pkt, size_t pkt_len)
{
    if (sc->sc_cmdq_lock == NULL)
        return;
    uint8_t *discard = NULL;
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(sc->sc_cmdq_lock);
    if (!sc->sc_cmdq_stopping && !sc->sc_cmdq_detaching &&
        iwm_cmdq_ring_valid(sc) && qid == sc->cmdqid &&
        idx >= 0 && idx < IWM_TX_RING_COUNT) {
        const struct iwm_cmd_slot *slot = &sc->sc_cmdq_slots[idx];
        if ((slot->state == IWM_CMD_SLOT_SUBMITTED ||
             slot->state == IWM_CMD_SLOT_TIMED_OUT) &&
            (slot->code & 0xffff) == (uint32_t)code &&
            sc->sc_cmd_resp_pkt[idx] != NULL) {
            if (slot->state == IWM_CMD_SLOT_TIMED_OUT ||
                (pkt->hdr.flags & IWM_CMD_FAILED_MSK) ||
                pkt_len < sizeof(*pkt) || pkt_len > sc->sc_cmd_resp_len[idx]) {
                discard = sc->sc_cmd_resp_pkt[idx];
                sc->sc_cmd_resp_pkt[idx] = NULL;
                sc->sc_cmd_resp_len[idx] = 0;
            } else {
                memcpy(sc->sc_cmd_resp_pkt[idx], pkt, pkt_len);
            }
        }
    }
    IOSimpleLockUnlockEnableInterrupt(sc->sc_cmdq_lock, irq);
    ::free(discard);
}

int ItlIwm::
iwm_send_cmd(struct iwm_softc *sc, struct iwm_host_cmd *hcmd)
{
    struct iwm_tx_ring *ring = NULL;
    struct iwm_tfd *desc;
    struct iwm_tx_data *txdata;
    struct iwm_device_cmd *cmd;
    mbuf_t m = NULL;
    bus_addr_t paddr;
    uint32_t addr_lo;
    int err = 0, i, paylen, off, s = 0;
    int idx = -1, code, async, group_id;
    size_t hdrlen, datasz;
    uint8_t *data;
    int generation = sc->sc_generation;
    unsigned int max_chunks = 1;
    IOPhysicalSegment seg;
    IOMbufNaturalMemoryCursor *cursor = NULL;
    uint8_t *resp_buf = NULL;
    uint64_t serial = 0;
    uint32_t epoch = 0;
    bool command_locked = false;
    IOInterruptState command_irq = 0;
    bool command_submitted = false;
    bool scan_locked = false;
    bool owner_locked = false;
    bool nic_wake_acquired = false;
    bool wait_locked = false;
    IOInterruptState scan_irq = 0;
    IOInterruptState owner_irq = 0;
    IOSimpleLock *owner_lock = sc->sc_ic.ic_pae_selected_bss_lock;
    ItlFirmwareContextCommand *context_command = hcmd->context_command;
    const bool context_live = context_command != NULL && !context_command->cleanup;
    const bool scan_request =
        hcmd->id == IWM_SCAN_OFFLOAD_REQUEST_CMD ||
        hcmd->id == iwm_cmd_id(IWM_SCAN_REQ_UMAC, IWM_LONG_GROUP, 0);
    const bool scan_abort = hcmd->id == IWM_SCAN_OFFLOAD_ABORT_CMD ||
        hcmd->id == IWM_WIDE_ID(IWM_LONG_GROUP, IWM_SCAN_ABORT_UMAC);

    if ((scan_request || scan_abort) &&
        (wclScanLock == NULL || hcmd->scan_serial == 0))
        return ENXIO;
    if ((scan_request || context_live) && owner_lock == NULL)
        return ENXIO;
    if (context_command != NULL) {
        if (scan_request || scan_abort || wclScanLock == NULL)
            return EINVAL;
        if (context_command->submitted)
            return EALREADY;
        if (context_command->receipt.generation !=
            static_cast<uint32_t>(sc->sc_generation))
            return ENXIO;
    }
    
    if (!iwm_cmdq_enter(sc))
        return ENXIO;
    s = splnet();
    code = hcmd->id;
    async = hcmd->flags & IWM_CMD_ASYNC;
    
    for (i = 0, paylen = 0; i < nitems(hcmd->len); i++) {
        paylen += hcmd->len[i];
    }
    
    /* If this command waits for a response, allocate response buffer. */
    hcmd->resp_pkt = NULL;
    if (hcmd->flags & IWM_CMD_WANT_RESP) {
        _KASSERT(!async);
        _KASSERT(hcmd->resp_pkt_len >= sizeof(struct iwm_rx_packet));
        _KASSERT(hcmd->resp_pkt_len <= IWM_CMD_RESP_MAX);
        resp_buf = (uint8_t *)malloc(hcmd->resp_pkt_len, M_DEVBUF,
                                     M_NOWAIT | M_ZERO);
        if (resp_buf == NULL) {
            err = ENOMEM;
            goto out;
        }
    }
    
    group_id = iwm_cmd_groupid(code);
    if (group_id != 0) {
        hdrlen = sizeof(cmd->hdr_wide);
        datasz = sizeof(cmd->data_wide);
    } else {
        hdrlen = sizeof(cmd->hdr);
        datasz = sizeof(cmd->data);
    }
    
    if (paylen > datasz) {
        /* Command is too large to fit in pre-allocated space. */
        size_t totlen = hdrlen + paylen;
        if (paylen > IWM_MAX_CMD_PAYLOAD_SIZE) {
            XYLog("%s: firmware command too long (%zd bytes)\n",
                  DEVNAME(sc), totlen);
            err = EINVAL;
            goto out;
        }
        mbuf_allocpacket(MBUF_WAITOK, totlen, &max_chunks, &m);
        if (m == NULL) {
            XYLog("%s: could not get fw cmd mbuf (%zd bytes)\n",
                  DEVNAME(sc), totlen);
            err = ENOMEM;
            goto out;
        }
        mbuf_setlen(m, totlen);
        mbuf_pkthdr_setlen(m, totlen);
        cmd = mtod(m, struct iwm_device_cmd *);
        /* Prepare a local mapping before reserving any ring storage. A
         * permanent slot map may already belong to a concurrent producer. */
        cursor = IOMbufNaturalMemoryCursor::withSpecification(
            hdrlen + IWM_MAX_CMD_PAYLOAD_SIZE, 1);
        if (cursor == NULL) {
            err = ENOMEM;
            goto out;
        }
        if (cursor->getPhysicalSegmentsWithCoalesce(m, &seg, 1) == 0) {
            XYLog("%s: could not load fw cmd mbuf (%zd bytes)\n",
                  DEVNAME(sc), totlen);
            err = ENOMEM;
            goto out;
        }
        cursor->release();
        cursor = NULL;
        paddr = seg.location;
    }

    if (generation != sc->sc_generation ||
        (sc->sc_flags & IWM_FLAG_SHUTDOWN) != 0) {
        err = ENXIO;
        goto out;
    }

    /* The wait mutex serializes the 7000 queue's NIC wake transition against
     * ACK. The command leaf serializes storage and the physical tail. All
     * allocation and mapping above remain local and outside both leaves. */
    lockTsleep();
    wait_locked = true;
    command_irq = IOSimpleLockLockDisableInterrupt(sc->sc_cmdq_lock);
    command_locked = true;
    if (sc->sc_cmdq_stopping || sc->sc_cmdq_detaching ||
        sc->sc_cmdq_generation != generation || !iwm_cmdq_ring_valid(sc) ||
        generation != sc->sc_generation || (sc->sc_flags & IWM_FLAG_SHUTDOWN)) {
        err = ENXIO;
        goto out;
    }
    ring = &sc->txq[sc->cmdqid];
    idx = ring->cur;
    if (idx < 0 || idx >= IWM_TX_RING_COUNT ||
        ring->queued >= IWM_TX_RING_COUNT ||
        sc->sc_cmdq_slots[idx].state != IWM_CMD_SLOT_FREE ||
        sc->sc_cmd_resp_pkt[idx] != NULL || ring->data[idx].m != NULL) {
        err = ENOSPC;
        goto out;
    }
    if (sc->sc_device_family == IWM_DEVICE_FAMILY_7000 && ring->queued == 0) {
        IOSimpleLockUnlockEnableInterrupt(sc->sc_cmdq_lock, command_irq);
        command_locked = false;
        if (!iwm_nic_lock(sc)) {
            err = EBUSY;
            goto out;
        }
        nic_wake_acquired = true;
        command_irq = IOSimpleLockLockDisableInterrupt(sc->sc_cmdq_lock);
        command_locked = true;
        if (generation != sc->sc_generation || (sc->sc_flags & IWM_FLAG_SHUTDOWN)) {
            err = ENXIO;
            goto out;
        }
    }
    desc = &ring->desc[idx];
    txdata = &ring->data[idx];
    if (m == NULL) {
        cmd = &ring->cmd[idx];
        paddr = txdata->cmd_paddr;
    }
    
    if (group_id != 0) {
        cmd->hdr_wide.opcode = iwm_cmd_opcode(code);
        cmd->hdr_wide.group_id = group_id;
        cmd->hdr_wide.qid = ring->qid;
        cmd->hdr_wide.idx = idx;
        cmd->hdr_wide.length = htole16(paylen);
        cmd->hdr_wide.version = iwm_cmd_version(code);
        data = cmd->data_wide;
    } else {
        cmd->hdr.code = code;
        cmd->hdr.flags = 0;
        cmd->hdr.qid = ring->qid;
        cmd->hdr.idx = idx;
        data = cmd->data;
    }
    
    for (i = 0, off = 0; i < nitems(hcmd->data); i++) {
        if (hcmd->len[i] == 0)
            continue;
        memcpy(data + off, hcmd->data[i], hcmd->len[i]);
        off += hcmd->len[i];
    }
    KASSERT(off == paylen, "off == paylen");
    
    /* lo field is not aligned */
    addr_lo = htole32((uint32_t)paddr);
    memcpy(&desc->tbs[0].lo, &addr_lo, sizeof(uint32_t));
    desc->tbs[0].hi_n_len  = htole16(iwm_get_dma_hi_addr(paddr)
                                     | ((hdrlen + paylen) << 4));
    desc->num_tbs = 1;
    
    //    if (paylen > datasz) {
    //        bus_dmamap_sync(sc->sc_dmat, txdata->map, 0,
    //            hdrlen + paylen, BUS_DMASYNC_PREWRITE);
    //    } else {
    //        bus_dmamap_sync(sc->sc_dmat, ring->cmd_dma.map,
    //            (char *)(void *)cmd - (char *)(void *)ring->cmd_dma.vaddr,
    //            hdrlen + paylen, BUS_DMASYNC_PREWRITE);
    //    }
    //    bus_dmamap_sync(sc->sc_dmat, ring->desc_dma.map,
    //        (char *)(void *)desc - (char *)(void *)ring->desc_dma.vaddr,
    //        sizeof (*desc), BUS_DMASYNC_PREWRITE);
    
    /* The scan leaf covers both the host receipt and the actual doorbell.
     * An init/stop boundary during command preparation cannot publish an
     * old scan into the new firmware epoch. No allocation or wait is inside
     * this leaf; command acknowledgement remains a separate lifetime. */
    if (scan_request || scan_abort || context_command != NULL) {
        if (scan_request || context_live) {
            owner_irq = IOSimpleLockLockDisableInterrupt(owner_lock);
            owner_locked = true;
        }
        scan_irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
        scan_locked = true;
        if (generation != sc->sc_generation ||
            (sc->sc_flags & IWM_FLAG_SHUTDOWN) != 0 ||
            (context_command != NULL &&
             !firmwareContextCommandCurrentLocked(*context_command)) ||
            (scan_request && !scanCommandOwnerCurrentLocked(hcmd->scan_serial, generation)) ||
            ((scan_request || scan_abort) &&
             !(scan_abort ? scanCommand.submitAbort(hcmd->scan_serial, generation) :
               scanCommand.submit(hcmd->scan_serial, generation)))) {
            err = scan_abort && scanCommand.current(hcmd->scan_serial, generation) &&
                scanCommand.terminalSeen ? EALREADY : ENXIO;
            goto out;
        }
    }

    if (m != NULL) {
        txdata->m = m; /* completion/reset now owns this DMA allocation */
        m = NULL;
    }

    if (++sc->sc_cmdq_next_serial == 0)
        ++sc->sc_cmdq_next_serial;
    serial = sc->sc_cmdq_next_serial;
    epoch = sc->sc_cmdq_epoch;
    sc->sc_cmdq_slots[idx] = {serial, epoch, static_cast<uint32_t>(code),
        IWM_CMD_SLOT_SUBMITTED, async != 0};
    sc->sc_cmd_resp_pkt[idx] = resp_buf;
    sc->sc_cmd_resp_len[idx] = resp_buf != NULL ? hcmd->resp_pkt_len : 0;
    resp_buf = NULL;
    
    iwm_update_sched(sc, ring->qid, ring->cur, 0, 0);
    /* Kick command ring. */
    ring->queued++;
    ring->cur = (ring->cur + 1) % IWM_TX_RING_COUNT;
    IWM_WRITE(sc, IWM_HBUS_TARG_WRPTR, ring->qid << 8 | ring->cur);
    if (context_command != NULL)
        context_command->submitted = true;
    command_submitted = true;
    if (scan_locked) {
        IOSimpleLockUnlockEnableInterrupt(wclScanLock, scan_irq);
        scan_locked = false;
    }
    if (owner_locked) {
        IOSimpleLockUnlockEnableInterrupt(owner_lock, owner_irq);
        owner_locked = false;
    }
    IOSimpleLockUnlockEnableInterrupt(sc->sc_cmdq_lock, command_irq);
    command_locked = false;
    unlockTsleep();
    wait_locked = false;
    
    if (!async) {
        uint64_t hard_deadline;
        clock_interval_to_deadline(2, kSecondScale, &hard_deadline);
        lockTsleep();
        wait_locked = true;
        for (;;) {
            command_irq = IOSimpleLockLockDisableInterrupt(sc->sc_cmdq_lock);
            command_locked = true;
            struct iwm_cmd_slot *slot = &sc->sc_cmdq_slots[idx];
            if (generation != sc->sc_generation || (sc->sc_flags & IWM_FLAG_SHUTDOWN) ||
                sc->sc_cmdq_stopping || sc->sc_cmdq_detaching ||
                slot->serial != serial || slot->epoch != epoch ||
                slot->state == IWM_CMD_SLOT_ABORTED) {
                err = ENXIO;
                goto out;
            }
            if (slot->state == IWM_CMD_SLOT_COMPLETED) {
                /* Actual ACK is retained even if it preceded registration or
                 * raced the timeout. This slot cannot be reused before here. */
                hcmd->resp_pkt = (struct iwm_rx_packet *)sc->sc_cmd_resp_pkt[idx];
                sc->sc_cmd_resp_pkt[idx] = NULL;
                sc->sc_cmd_resp_len[idx] = 0;
                slot->state = IWM_CMD_SLOT_FREE;
                err = 0;
                goto out;
            }
            if (slot->state != IWM_CMD_SLOT_SUBMITTED) {
                err = EIO;
                goto out;
            }
            uint64_t now;
            clock_get_uptime(&now);
            if (err != 0 || now >= hard_deadline) {
                if (err == 0)
                    err = ETIMEDOUT;
                slot->state = IWM_CMD_SLOT_TIMED_OUT;
                /* DMA and response stay quarantined until ACK or reset. */
                goto out;
            }
            uint64_t remaining;
            absolutetime_to_nanoseconds(hard_deadline - now, &remaining);
            IOSimpleLockUnlockEnableInterrupt(sc->sc_cmdq_lock, command_irq);
            command_locked = false;
            if (getMainCommandGate() != NULL && getMainWorkLoop() != NULL &&
                getMainWorkLoop()->inGate()) {
                AbsoluteTime deadline;
                clock_interval_to_deadline(10, kMillisecondScale,
                    reinterpret_cast<uint64_t *>(&deadline));
                if (*reinterpret_cast<uint64_t *>(&deadline) > hard_deadline)
                    *reinterpret_cast<uint64_t *>(&deadline) = hard_deadline;
                /* Never hold the wait mutex across recursive gate release or
                 * reacquisition. A missed gate wake is bounded to 10ms; only
                 * the retained slot state, never a wake, confirms success. */
                unlockTsleep();
                wait_locked = false;
                (void)getMainCommandGate()->commandSleep(desc, deadline, THREAD_UNINT);
                lockTsleep();
                wait_locked = true;
            } else {
                err = tsleep_nsec_locked(desc, PCATCH, "iwmcmd", remaining);
            }
        }
    }
out:
    if (scan_locked)
        IOSimpleLockUnlockEnableInterrupt(wclScanLock, scan_irq);
    if (owner_locked)
        IOSimpleLockUnlockEnableInterrupt(owner_lock, owner_irq);
    if (command_locked)
        IOSimpleLockUnlockEnableInterrupt(sc->sc_cmdq_lock, command_irq);
    if (!command_submitted && nic_wake_acquired)
        iwm_nic_unlock(sc);
    if (wait_locked)
        unlockTsleep();
    if (cursor != NULL)
        cursor->release();
    ::free(resp_buf);
    if (m != NULL)
        mbuf_freem(m);
    splx(s);
    iwm_cmdq_leave(sc);
    
    return err;
}

void ItlIwm::
iwm_radio_abort_command_waits(struct iwm_softc *sc)
{
    if (sc->cmdqid < 0 || sc->cmdqid >= (int)nitems(sc->txq))
        return;
    struct iwm_tx_ring *ring = &sc->txq[sc->cmdqid];
    if (ring->desc == NULL)
        return;
    /* Stop has already closed SHUTDOWN and advanced generation. Keep the
     * wait mutex through wakeup; descriptor/response reclaim occurs only
     * after the init and state bodies have left. No firmware leaf is held. */
    lockTsleep();
    wakeupOn(&sc->sc_scan_abort_pending);
    for (int idx = 0; idx < IWM_TX_RING_COUNT; idx++)
        wakeupOn(&ring->desc[idx]);
    unlockTsleep();
}

int ItlIwm::
iwm_send_cmd_pdu(struct iwm_softc *sc, uint32_t id, uint32_t flags,
                 uint16_t len, const void *data)
{
    struct iwm_host_cmd cmd = {
        .id = id,
        .len = { len, },
        .data = { data, },
        .flags = flags,
    };
    
    return iwm_send_cmd(sc, &cmd);
}

int ItlIwm::
iwm_send_cmd_status(struct iwm_softc *sc, struct iwm_host_cmd *cmd,
                    uint32_t *status)
{
    struct iwm_rx_packet *pkt;
    struct iwm_cmd_response *resp;
    int err, resp_len;
    
    KASSERT((cmd->flags & IWM_CMD_WANT_RESP) == 0, "(cmd->flags & IWM_CMD_WANT_RESP) == 0");
    cmd->flags |= IWM_CMD_WANT_RESP;
    cmd->resp_pkt_len = sizeof(*pkt) + sizeof(*resp);
    
    err = iwm_send_cmd(sc, cmd);
    if (err)
        return err;
    
    pkt = cmd->resp_pkt;
    if (pkt == NULL || (pkt->hdr.flags & IWM_CMD_FAILED_MSK)) {
        iwm_free_resp(sc, cmd);
        return EIO;
    }
    
    resp_len = iwm_rx_packet_payload_len(pkt);
    if (resp_len != sizeof(*resp)) {
        iwm_free_resp(sc, cmd);
        return EIO;
    }
    
    resp = (struct iwm_cmd_response *)pkt->data;
    *status = le32toh(resp->status);
    iwm_free_resp(sc, cmd);
    return err;
}

int ItlIwm::
iwm_send_cmd_pdu_status(struct iwm_softc *sc, uint32_t id, uint16_t len,
                        const void *data, uint32_t *status)
{
    struct iwm_host_cmd cmd = {
        .id = id,
        .len = { len, },
        .data = { data, },
    };
    
    return iwm_send_cmd_status(sc, &cmd, status);
}

void ItlIwm::
iwm_free_resp(struct iwm_softc *sc, struct iwm_host_cmd *hcmd)
{
    _KASSERT((hcmd->flags & (IWM_CMD_WANT_RESP)) == IWM_CMD_WANT_RESP);
    ::free(hcmd->resp_pkt);
    hcmd->resp_pkt = NULL;
}

void ItlIwm::
iwm_cmd_done(struct iwm_softc *sc, int qid, int idx, int code)
{
    if (sc->sc_cmdq_lock == NULL)
        return;
    mbuf_t retired = NULL;
    uint8_t *discard = NULL;
    void *wake = NULL;
    bool unlock_nic = false;
    lockTsleep();
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(sc->sc_cmdq_lock);
    if (!sc->sc_cmdq_stopping && !sc->sc_cmdq_detaching &&
        iwm_cmdq_ring_valid(sc) && qid == sc->cmdqid &&
        idx >= 0 && idx < IWM_TX_RING_COUNT) {
        struct iwm_tx_ring *ring = &sc->txq[qid];
        struct iwm_cmd_slot *slot = &sc->sc_cmdq_slots[idx];
        if ((slot->state == IWM_CMD_SLOT_SUBMITTED ||
             slot->state == IWM_CMD_SLOT_TIMED_OUT) &&
            slot->epoch == sc->sc_cmdq_epoch &&
            (slot->code & 0xffff) == (uint32_t)code) {
            retired = ring->data[idx].m;
            ring->data[idx].m = NULL;
            KASSERT(ring->queued > 0, "ACK owns a submitted command");
            unlock_nic = --ring->queued == 0 &&
                sc->sc_device_family == IWM_DEVICE_FAMILY_7000;
            if (slot->async || slot->state == IWM_CMD_SLOT_TIMED_OUT) {
                discard = sc->sc_cmd_resp_pkt[idx];
                sc->sc_cmd_resp_pkt[idx] = NULL;
                sc->sc_cmd_resp_len[idx] = 0;
                slot->state = IWM_CMD_SLOT_FREE;
            } else {
                slot->state = IWM_CMD_SLOT_COMPLETED;
            }
            wake = &ring->desc[idx];
        }
    }
    IOSimpleLockUnlockEnableInterrupt(sc->sc_cmdq_lock, irq);
    if (unlock_nic)
        iwm_nic_unlock(sc);
    if (wake != NULL) {
        wakeupOn(wake);
        if (getMainCommandGate() != NULL)
            getMainCommandGate()->commandWakeup(wake, false);
    }
    unlockTsleep();
    if (retired != NULL)
        mbuf_freem(retired);
    ::free(discard);
}

int ItlIwm::
iwm_phy_ctxt_update(struct iwm_softc *sc, struct iwm_phy_ctxt *phyctxt,
                    struct ieee80211_channel *chan, uint8_t chains_static,
                    uint8_t chains_dynamic, uint32_t apply_time)
{
    uint16_t band_flags = (IEEE80211_CHAN_2GHZ | IEEE80211_CHAN_5GHZ);
    int err;
    
    if (isset(sc->sc_enabled_capa,
              IWM_UCODE_TLV_CAPA_BINDING_CDB_SUPPORT) &&
        (phyctxt->channel->ic_flags & band_flags) !=
        (chan->ic_flags & band_flags)) {
        err = iwm_phy_ctxt_cmd(sc, phyctxt, chains_static,
                               chains_dynamic, IWM_FW_CTXT_ACTION_REMOVE, apply_time);
        if (err) {
            printf("%s: could not remove PHY context "
                   "(error %d)\n", DEVNAME(sc), err);
            return err;
        }
        phyctxt->channel = chan;
        err = iwm_phy_ctxt_cmd(sc, phyctxt, chains_static,
                               chains_dynamic, IWM_FW_CTXT_ACTION_ADD, apply_time);
        if (err) {
            printf("%s: could not remove PHY context "
                   "(error %d)\n", DEVNAME(sc), err);
            return err;
        }
    } else {
        phyctxt->channel = chan;
        err = iwm_phy_ctxt_cmd(sc, phyctxt, chains_static,
                               chains_dynamic, IWM_FW_CTXT_ACTION_MODIFY, apply_time);
        if (err) {
            printf("%s: could not update PHY context (error %d)\n",
                   DEVNAME(sc), err);
            return err;
        }
    }
    
    return 0;
}
