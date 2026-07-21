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
/*	$OpenBSD: ieee80211_proto.c,v 1.95 2019/09/02 12:54:21 stsp Exp $	*/
/*	$NetBSD: ieee80211_proto.c,v 1.8 2004/04/30 23:58:20 dyoung Exp $	*/

/*-
 * Copyright (c) 2001 Atsushi Onoe
 * Copyright (c) 2002, 2003 Sam Leffler, Errno Consulting
 * Copyright (c) 2008, 2009 Damien Bergamini
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * IEEE 802.11 protocol support.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_llc.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_priv.h>
#include <ClientKit/AirportItlwmPostPltiTraceBridge.h>

#if defined(__IO80211_TARGET) && __IO80211_TARGET >= __MAC_26_0
#include <ClientKit/AirportItlwmRegDiagBridge.h>
#endif

const char * const ieee80211_mgt_subtype_name[] = {
	"assoc_req",	"assoc_resp",	"reassoc_req",	"reassoc_resp",
	"probe_req",	"probe_resp",	"reserved#6",	"reserved#7",
	"beacon",	"atim",		"disassoc",	"auth",
	"deauth",	"action",	"action_noack",	"reserved#15"
};
const char * const ieee80211_state_name[IEEE80211_S_MAX] = {
	"INIT",		/* IEEE80211_S_INIT */
	"SCAN",		/* IEEE80211_S_SCAN */
	"AUTH",		/* IEEE80211_S_AUTH */
	"ASSOC",	/* IEEE80211_S_ASSOC */
	"RUN"		/* IEEE80211_S_RUN */
};
const char * const ieee80211_phymode_name[] = {
	"auto",		/* IEEE80211_MODE_AUTO */
	"11a",		/* IEEE80211_MODE_11A */
	"11b",		/* IEEE80211_MODE_11B */
	"11g",		/* IEEE80211_MODE_11G */
	"11n",		/* IEEE80211_MODE_11N */
    "11ac",     /* IEEE80211_MODE_11AC */
    "11ax",     /* IEEE80211_MODE_11AX */
};

void ieee80211_set_beacon_miss_threshold(struct ieee80211com *);
int ieee80211_newstate(struct ieee80211com *, enum ieee80211_state, int);

/*
 * Future asynchronous owners must snapshot through this acquire load rather
 * than reading the volatile field directly.  The field is written from reset
 * and driver-task contexts, so volatile alone is not a synchronization rule.
 */
u_int64_t
ieee80211_pae_assoc_epoch_current(const struct ieee80211com *ic)
{
	if (ic == NULL || ic->ic_opmode != IEEE80211_M_STA)
		return 0;
	return __atomic_load_n(&ic->ic_pae_assoc_epoch, __ATOMIC_ACQUIRE);
}

/*
 * The selected-BSS leaf lock also serializes the compact PMF transaction
 * record.  It deliberately protects values only: driver callbacks, crypto,
 * EAPOL output, node release, and state changes all happen after it is
 * dropped.  This keeps epoch cancellation usable from deauth/roam/stop
 * paths without creating an inversion with q0 or the driver task gate.
 */
static u_int64_t
ieee80211_pae_mfp_txn_cancel_locked(struct ieee80211com *ic)
{
	struct ieee80211_pae_mfp_txn *txn = &ic->ic_pae_mfp_txn;
	u_int64_t id;

	if (!txn->active)
		return 0;
	id = txn->id;
	explicit_bzero(txn, sizeof(*txn));
	return id;
}

static int
ieee80211_pae_mfp_txn_live_locked(struct ieee80211com *ic,
	const struct ieee80211_pae_mfp_txn *txn, u_int64_t id,
	u_int64_t epoch, const struct ieee80211_node *ni)
{
	return txn->active && txn->id == id && txn->assoc_epoch == epoch &&
	txn->ni == ni && ic->ic_bss == ni &&
	__atomic_load_n(&ic->ic_pae_assoc_epoch, __ATOMIC_ACQUIRE) == epoch;
}

static u_int8_t
ieee80211_pae_mfp_txn_next_stage(const struct ieee80211_pae_mfp_txn *txn,
	u_int8_t stage)
{
	if (stage == IEEE80211_PAE_MFP_STAGE_PTK && txn->have_gtk)
		return IEEE80211_PAE_MFP_STAGE_GTK;
	if ((stage == IEEE80211_PAE_MFP_STAGE_PTK ||
	     stage == IEEE80211_PAE_MFP_STAGE_GTK) && txn->have_igtk)
		return IEEE80211_PAE_MFP_STAGE_IGTK;
	return IEEE80211_PAE_MFP_STAGE_NONE;
}

static const struct ieee80211_key *
ieee80211_pae_mfp_txn_key(const struct ieee80211_pae_mfp_txn *txn,
	u_int8_t stage)
{
	switch (stage) {
	case IEEE80211_PAE_MFP_STAGE_PTK:
		return &txn->ptk_key;
	case IEEE80211_PAE_MFP_STAGE_GTK:
		return &txn->gtk_key;
	case IEEE80211_PAE_MFP_STAGE_IGTK:
		return &txn->igtk_key;
	default:
		return NULL;
	}
}

static int
ieee80211_pae_mfp_txn_live(struct ieee80211com *ic, u_int64_t id,
	u_int64_t epoch, const struct ieee80211_node *ni)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	int live;

	if (ic == NULL || (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return 0;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	live = ieee80211_pae_mfp_txn_live_locked(ic, &ic->ic_pae_mfp_txn,
	    id, epoch, ni);
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return live;
}

/*
 * Firmware may have accepted a data or management key before software BIP
 * setup rejects the final commit.  Remove every staged key best-effort while
 * still in the serial PAE worker; the caller then deauthenticates rather than
 * leaving an accepted key behind for a failed association.
 */
static void
ieee80211_pae_mfp_txn_rollback_firmware(struct ieee80211com *ic,
	const struct ieee80211_pae_mfp_txn *txn)
{
	struct ieee80211_key key;

	if (ic == NULL || txn == NULL || txn->ni == NULL ||
	    ic->ic_delete_key == NULL)
		return;
	if (txn->have_igtk) {
		key = txn->igtk_key;
		key.k_priv = NULL;
		(*ic->ic_delete_key)(ic, txn->ni, &key);
		explicit_bzero(&key, sizeof(key));
	}
	if (txn->have_gtk) {
		key = txn->gtk_key;
		key.k_priv = NULL;
		(*ic->ic_delete_key)(ic, txn->ni, &key);
		explicit_bzero(&key, sizeof(key));
	}
	if (txn->have_ptk) {
		key = txn->ptk_key;
		key.k_priv = NULL;
		(*ic->ic_delete_key)(ic, txn->ni, &key);
		explicit_bzero(&key, sizeof(key));
	}
}

/* Clear the one WCL PMF request only for the still-current failed BSS. */
static int
ieee80211_pae_mfp_txn_terminal_current(struct ieee80211com *ic,
	struct ieee80211_node *ni, u_int64_t epoch)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	int current = 0;

	if (ic == NULL || ni == NULL ||
	    (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return 0;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	if (ic->ic_bss == ni &&
	    __atomic_load_n(&ic->ic_pae_assoc_epoch, __ATOMIC_ACQUIRE) ==
	    epoch) {
		ic->ic_pae_mfp_requested = 0;
		current = 1;
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return current;
}

void
ieee80211_pae_mfp_txn_abort(struct ieee80211com *ic)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	void (*cancel)(struct ieee80211com *, u_int64_t);
	u_int64_t id;

	if (ic == NULL || (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	id = ieee80211_pae_mfp_txn_cancel_locked(ic);
	cancel = ic->ic_pae_mfp_txn_cancel;
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	if (id != 0 && cancel != NULL)
		(*cancel)(ic, id);
}

int
ieee80211_pae_mfp_txn_begin(struct ieee80211com *ic,
	struct ieee80211_node *ni, const struct ieee80211_ptk *ptk,
	const struct ieee80211_key *ptk_key, int have_ptk,
	const struct ieee80211_key *gtk_key, int have_gtk,
	const struct ieee80211_key *igtk_key, int have_igtk,
	u_int64_t replaycnt, u_int16_t key_info, u_int8_t reply)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_pae_mfp_txn *txn;
	const struct ieee80211_key *key;
	struct ieee80211_key key_copy;
	int (*submit)(struct ieee80211com *, u_int64_t, u_int64_t,
	    struct ieee80211_node *, const struct ieee80211_key *, u_int8_t);
	u_int64_t id, epoch;
	u_int8_t stage;
	int error;

	if (ic == NULL || ni == NULL || ptk == NULL ||
	    (reply != IEEE80211_PAE_MFP_REPLY_4WAY_MSG4 &&
	     reply != IEEE80211_PAE_MFP_REPLY_GROUP_MSG2) ||
	    (!have_ptk && !have_gtk && !have_igtk) ||
	    (have_ptk && ptk_key == NULL) ||
	    (have_gtk && gtk_key == NULL) ||
	    (have_igtk && igtk_key == NULL))
		return EINVAL;
	if ((have_ptk && ptk_key->k_priv != NULL) ||
	    (have_gtk && gtk_key->k_priv != NULL) ||
	    (have_igtk && igtk_key->k_priv != NULL))
		return EINVAL;
	lock = ic->ic_pae_selected_bss_lock;
	submit = ic->ic_pae_mfp_txn_submit;
	if (lock == NULL || submit == NULL)
		return EOPNOTSUPP;

	irq = IOSimpleLockLockDisableInterrupt(lock);
	epoch = __atomic_load_n(&ic->ic_pae_assoc_epoch, __ATOMIC_ACQUIRE);
	if (epoch == 0 || ic->ic_bss != ni || ic->ic_pae_mfp_txn.active) {
		IOSimpleLockUnlockEnableInterrupt(lock, irq);
		return EBUSY;
	}
	do {
		id = ++ic->ic_pae_mfp_next_txn;
	} while (id == 0);
	txn = &ic->ic_pae_mfp_txn;
	explicit_bzero(txn, sizeof(*txn));
	txn->active = 1;
	txn->id = id;
	txn->assoc_epoch = epoch;
	txn->ni = ni;
	txn->ptk = *ptk;
	txn->replaycnt = replaycnt;
	txn->key_info = key_info;
	txn->reply = reply;
	txn->have_ptk = !!have_ptk;
	txn->have_gtk = !!have_gtk;
	txn->have_igtk = !!have_igtk;
	if (have_ptk) {
		txn->ptk_key = *ptk_key;
		txn->ptk_key.k_priv = NULL;
	}
	if (have_gtk) {
		txn->gtk_key = *gtk_key;
		txn->gtk_key.k_priv = NULL;
	}
	if (have_igtk) {
		txn->igtk_key = *igtk_key;
		txn->igtk_key.k_priv = NULL;
	}
	stage = have_ptk ? IEEE80211_PAE_MFP_STAGE_PTK :
	    (have_gtk ? IEEE80211_PAE_MFP_STAGE_GTK :
	     IEEE80211_PAE_MFP_STAGE_IGTK);
	txn->phase = stage;
	key = ieee80211_pae_mfp_txn_key(txn, stage);
	key_copy = *key;
	key_copy.k_priv = NULL;
	IOSimpleLockUnlockEnableInterrupt(lock, irq);

	error = (*submit)(ic, id, epoch, ni, &key_copy, stage);
	explicit_bzero(&key_copy, sizeof(key_copy));
	if (error != 0)
		ieee80211_pae_mfp_txn_abort(ic);
	return error;
}

static int
ieee80211_pae_mfp_txn_commit(struct ieee80211com *ic,
	const struct ieee80211_pae_mfp_txn *txn)
{
	struct ieee80211_node *ni = txn->ni;
	struct ieee80211_key bip_key;
	int bip_installed = 0;
	int was_port_valid;

	if (!ieee80211_pae_mfp_txn_live(ic, txn->id, txn->assoc_epoch, ni))
		return ECANCELED;

	/* Firmware has ACKed data keys. BIP remains software-owned. */
	if (txn->have_igtk) {
		bip_key = txn->igtk_key;
		bip_key.k_priv = NULL;
		if (ieee80211_set_key(ic, ni, &bip_key) != 0) {
			/* FW has ACKed staged keys; remove them before failing closed. */
			ieee80211_pae_mfp_txn_rollback_firmware(ic, txn);
			explicit_bzero(&bip_key, sizeof(bip_key));
			return EIO;
		}
		bip_installed = 1;
		if (!ieee80211_pae_mfp_txn_live(ic, txn->id,
		    txn->assoc_epoch, ni)) {
			ieee80211_delete_key(ic, ni, &bip_key);
			ieee80211_pae_mfp_txn_rollback_firmware(ic, txn);
			explicit_bzero(&bip_key, sizeof(bip_key));
			return ECANCELED;
		}
	}
	if (!ieee80211_pae_mfp_txn_live(ic, txn->id, txn->assoc_epoch, ni)) {
		if (bip_installed)
			ieee80211_delete_key(ic, ni, &bip_key);
		ieee80211_pae_mfp_txn_rollback_firmware(ic, txn);
		if (txn->have_igtk)
			explicit_bzero(&bip_key, sizeof(bip_key));
		return ECANCELED;
	}

	if (txn->have_ptk) {
		ni->ni_ptk = txn->ptk;
		ni->ni_pairwise_key = txn->ptk_key;
	}
	if (txn->have_gtk)
		ic->ic_nw_keys[txn->gtk_key.k_id] = txn->gtk_key;
	if (txn->have_igtk) {
		ic->ic_nw_keys[bip_key.k_id] = bip_key;
		ic->ic_igtk_kid = bip_key.k_id;
		ni->ni_flags |= IEEE80211_NODE_TXMGMTPROT |
		    IEEE80211_NODE_RXMGMTPROT;
	}
	if (txn->have_ptk) {
		ni->ni_flags &= ~IEEE80211_NODE_RSN_NEW_PTK;
		ni->ni_flags &= ~IEEE80211_NODE_TXRXPROT;
		ni->ni_flags |= IEEE80211_NODE_RXPROT;
	}
	if (txn->key_info & EAPOL_KEY_INSTALL)
		ni->ni_flags |= IEEE80211_NODE_TXRXPROT;
	ni->ni_replaycnt = txn->replaycnt;
	ni->ni_replaycnt_ok = 1;

	if (!ieee80211_pae_mfp_txn_live(ic, txn->id, txn->assoc_epoch, ni))
		goto rollback_cancelled;
	if (txn->reply == IEEE80211_PAE_MFP_REPLY_4WAY_MSG4) {
		if (ieee80211_send_4way_msg4(ic, ni) != 0)
			goto rollback_error;
	} else {
		(void)ieee80211_send_group_msg2(ic, ni, NULL);
	}

	if (txn->key_info & EAPOL_KEY_SECURE) {
		if (!ieee80211_pae_mfp_txn_live(ic, txn->id,
		    txn->assoc_epoch, ni))
			goto rollback_cancelled;
		ni->ni_flags |= IEEE80211_NODE_TXRXPROT;
		was_port_valid = ni->ni_port_valid;
		ni->ni_port_valid = 1;
		if (!was_port_valid) {
			AirportItlwmPostPltiTraceCompleteEpisode(ic);
			ieee80211_set_link_state(ic, LINK_STATE_UP);
			if (ic->ic_event_handler != NULL)
				(*ic->ic_event_handler)(ic,
				    IEEE80211_EVT_STA_RSN_HANDSHAKE_DONE, NULL);
		}
		ni->ni_assoc_fail = 0;
		if (ic->ic_opmode == IEEE80211_M_STA)
			ic->ic_rsngroupcipher = ni->ni_rsngroupcipher;
	}
	if (txn->have_igtk)
		explicit_bzero(&bip_key, sizeof(bip_key));
	return 0;

rollback_error:
	if (bip_installed)
		ieee80211_delete_key(ic, ni, &bip_key);
	ieee80211_pae_mfp_txn_rollback_firmware(ic, txn);
	if (txn->have_igtk)
		explicit_bzero(&bip_key, sizeof(bip_key));
	return EIO;

rollback_cancelled:
	if (bip_installed)
		ieee80211_delete_key(ic, ni, &bip_key);
	ieee80211_pae_mfp_txn_rollback_firmware(ic, txn);
	if (txn->have_igtk)
		explicit_bzero(&bip_key, sizeof(bip_key));
	return ECANCELED;
}

void
ieee80211_pae_mfp_txn_complete(struct ieee80211com *ic, u_int64_t id,
	u_int8_t stage, int error)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_pae_mfp_txn *txn;
	struct ieee80211_pae_mfp_txn snapshot;
	const struct ieee80211_key *key;
	struct ieee80211_key key_copy;
	struct ieee80211_node *ni;
	int (*submit)(struct ieee80211com *, u_int64_t, u_int64_t,
	    struct ieee80211_node *, const struct ieee80211_key *, u_int8_t);
	void (*finish)(struct ieee80211com *, u_int64_t) = NULL;
	void (*cancel)(struct ieee80211com *, u_int64_t) = NULL;
	u_int64_t epoch;
	u_int8_t next;
	int final_commit = 0;

	if (ic == NULL || (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	txn = &ic->ic_pae_mfp_txn;
	if (!txn->active || txn->id != id || txn->phase != stage) {
		IOSimpleLockUnlockEnableInterrupt(lock, irq);
		return;
	}
	epoch = txn->assoc_epoch;
	ni = txn->ni;
	if (error != 0 || !ieee80211_pae_mfp_txn_live_locked(ic, txn, id,
	    epoch, ni)) {
		cancel = ic->ic_pae_mfp_txn_cancel;
		(void)ieee80211_pae_mfp_txn_cancel_locked(ic);
		IOSimpleLockUnlockEnableInterrupt(lock, irq);
		if (cancel != NULL)
			(*cancel)(ic, id);
		/* A stale epoch belongs to a replacement BSS and must not deauth it. */
		if (error != ECANCELED &&
		    ieee80211_pae_mfp_txn_terminal_current(ic, ni, epoch)) {
			IEEE80211_SEND_MGMT(ic, ni,
			    IEEE80211_FC0_SUBTYPE_DEAUTH, IEEE80211_REASON_AUTH_LEAVE);
			ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
		}
		return;
	}
	next = ieee80211_pae_mfp_txn_next_stage(txn, stage);
	if (next != IEEE80211_PAE_MFP_STAGE_NONE) {
		txn->phase = next;
		key = ieee80211_pae_mfp_txn_key(txn, next);
		key_copy = *key;
		key_copy.k_priv = NULL;
		submit = ic->ic_pae_mfp_txn_submit;
		IOSimpleLockUnlockEnableInterrupt(lock, irq);
		if (submit == NULL || (*submit)(ic, id, epoch, ni, &key_copy,
		    next) != 0) {
			explicit_bzero(&key_copy, sizeof(key_copy));
			ieee80211_pae_mfp_txn_complete(ic, id, next, EIO);
		} else
			explicit_bzero(&key_copy, sizeof(key_copy));
		return;
	}
	snapshot = *txn;
	txn->phase = IEEE80211_PAE_MFP_STAGE_NONE;
	IOSimpleLockUnlockEnableInterrupt(lock, irq);

	final_commit = ieee80211_pae_mfp_txn_commit(ic, &snapshot);
	irq = IOSimpleLockLockDisableInterrupt(lock);
	txn = &ic->ic_pae_mfp_txn;
	if (!ieee80211_pae_mfp_txn_live_locked(ic, txn, id, epoch,
	    snapshot.ni)) {
		IOSimpleLockUnlockEnableInterrupt(lock, irq);
		explicit_bzero(&snapshot, sizeof(snapshot));
		return;
	}
	if (final_commit == 0)
		finish = ic->ic_pae_mfp_txn_finish;
	else
		cancel = ic->ic_pae_mfp_txn_cancel;
	(void)ieee80211_pae_mfp_txn_cancel_locked(ic);
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	if (final_commit == 0) {
		if (finish != NULL)
			(*finish)(ic, id);
	} else {
		if (cancel != NULL)
			(*cancel)(ic, id);
		if (final_commit != ECANCELED &&
		    ieee80211_pae_mfp_txn_terminal_current(ic, snapshot.ni,
		    snapshot.assoc_epoch)) {
			IEEE80211_SEND_MGMT(ic, snapshot.ni,
			    IEEE80211_FC0_SUBTYPE_DEAUTH, IEEE80211_REASON_AUTH_LEAVE);
			ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
		}
	}
	explicit_bzero(&snapshot, sizeof(snapshot));
}

/* All callers hold ic_pae_selected_bss_lock when it is available. */
static void
ieee80211_pae_selected_bss_invalidate(struct ieee80211com *ic)
{
	if (ic == NULL)
		return;
	__atomic_store_n(&ic->ic_pae_selected_bss.epoch, 0,
	    __ATOMIC_RELEASE);
	ieee80211_pae_selected_bss_clear_payload(&ic->ic_pae_selected_bss);
}

/* Advance a nonzero association epoch while its leaf writer lock is held. */
static u_int64_t
ieee80211_pae_assoc_epoch_advance_locked(struct ieee80211com *ic)
{
	u_int64_t epoch;

	do {
		epoch = __atomic_add_fetch(&ic->ic_pae_assoc_epoch, 1,
		    __ATOMIC_ACQ_REL);
	} while (epoch == 0);
	return epoch;
}

/*
 * Capture only the BSS net80211 selected and copied into ic_bss.  The caller
 * reaches this after node replacement; request-side WCL/ASSOCIATE carriers,
 * scan candidates, raw IE pointers, and credentials remain out of scope.
 */
void
ieee80211_pae_selected_bss_capture(struct ieee80211com *ic,
    const struct ieee80211_node *ni, int strict_pure_sae_profile,
    u_int64_t expected_epoch)
{
	IOSimpleLock *lock;
	IOInterruptState irq;

	if (ic == NULL)
		return;
	lock = ic->ic_pae_selected_bss_lock;
	/* Publication is optional until its leaf lock has been allocated. */
	if (lock == NULL)
		return;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	if (ic->ic_opmode != IEEE80211_M_STA || ni == NULL ||
	    ni != ic->ic_bss || expected_epoch == 0 ||
	    __atomic_load_n(&ic->ic_pae_assoc_epoch, __ATOMIC_ACQUIRE) !=
		expected_epoch ||
	    __atomic_load_n(&ic->ic_pae_assoc_replace_epoch,
		__ATOMIC_ACQUIRE) != expected_epoch)
		goto out;
	ieee80211_pae_selected_bss_invalidate(ic);
	if (!ieee80211_pae_selected_bss_populate(
	    &ic->ic_pae_selected_bss, ni->ni_bssid, ni->ni_essid,
	    ni->ni_esslen, ni->ni_sae_scan_flags, strict_pure_sae_profile))
		goto out;
	__atomic_store_n(&ic->ic_pae_selected_bss.epoch, expected_epoch,
	    __ATOMIC_RELEASE);
out:
	/* Only the replacement owner that installed this marker may clear it. */
	if (__atomic_load_n(&ic->ic_pae_assoc_replace_epoch,
	    __ATOMIC_ACQUIRE) == expected_epoch)
		__atomic_store_n(&ic->ic_pae_assoc_replace_epoch, 0,
		    __ATOMIC_RELEASE);
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
}

/*
 * Copy only the fixed selected-BSS value for an exact, fully published epoch.
 * The caller already holds the HAL/lifecycle claim that keeps ic and this leaf
 * lock alive through the call and supplies a kernel-resident, caller-owned,
 * non-aliasing output value.  The leaf lock serializes fields but does not
 * create that object-lifetime claim, and this API may not race final lock
 * destruction.  There are deliberately no production consumers until a
 * future owner can satisfy that precondition.  This is not association
 * admission, credential delivery, or authentication permission.
 */
int
ieee80211_pae_selected_bss_copyout_current(struct ieee80211com *ic,
    u_int64_t expected_epoch, struct ieee80211_pae_selected_bss *out)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	int copied = 0;

	if (out == NULL)
		return 0;
	/* The live record is never a valid external output destination. */
	if (ic != NULL && out == &ic->ic_pae_selected_bss)
		return 0;
	memset(out, 0, sizeof(*out));
	if (ic == NULL || expected_epoch == 0 ||
	    ic->ic_opmode != IEEE80211_M_STA)
		return 0;
	lock = ic->ic_pae_selected_bss_lock;
	if (lock == NULL)
		return 0;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	if (ic->ic_opmode == IEEE80211_M_STA && expected_epoch != 0 &&
	    __atomic_load_n(&ic->ic_pae_assoc_epoch, __ATOMIC_ACQUIRE) ==
		expected_epoch &&
	    __atomic_load_n(&ic->ic_pae_assoc_replace_epoch,
		__ATOMIC_ACQUIRE) == 0 &&
	    __atomic_load_n(&ic->ic_pae_selected_bss.epoch,
		__ATOMIC_ACQUIRE) == expected_epoch) {
		if (ieee80211_pae_selected_bss_populate(out,
		    ic->ic_pae_selected_bss.bssid,
		    ic->ic_pae_selected_bss.ssid,
		    ic->ic_pae_selected_bss.ssid_len,
		    ic->ic_pae_selected_bss.sae_scan_flags,
		    ic->ic_pae_selected_bss.strict_pure_sae_profile)) {
			out->epoch = expected_epoch;
			copied = 1;
		}
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return copied;
}

/*
 * Advance a transaction fence before a new STA association owner replaces
 * node/RSN state.  The future SAE relay and PAE continuation queues may read
 * this from a different execution context, hence an atomic increment.  Zero
 * is reserved as the uninitialized/no-attempt value and is skipped on wrap.
 */
u_int64_t
ieee80211_pae_assoc_epoch_begin(struct ieee80211com *ic)
{
	u_int64_t epoch;
	u_int64_t txn_id = 0;
	IOSimpleLock *lock;
	IOInterruptState irq;
	void (*cancel)(struct ieee80211com *, u_int64_t) = NULL;

	if (ic == NULL || ic->ic_opmode != IEEE80211_M_STA)
		return 0;
	lock = ic->ic_pae_selected_bss_lock;
	if (lock != NULL)
		irq = IOSimpleLockLockDisableInterrupt(lock);
	epoch = ieee80211_pae_assoc_epoch_advance_locked(ic);
	__atomic_store_n(&ic->ic_pae_assoc_replace_epoch, 0,
	    __ATOMIC_RELEASE);
	ieee80211_pae_selected_bss_invalidate(ic);
	if (lock != NULL) {
		txn_id = ieee80211_pae_mfp_txn_cancel_locked(ic);
		cancel = ic->ic_pae_mfp_txn_cancel;
		IOSimpleLockUnlockEnableInterrupt(lock, irq);
	}
	/* Epoch cancellation only marks and notifies after the leaf lock. */
	if (txn_id != 0 && cancel != NULL)
		(*cancel)(ic, txn_id);
	return epoch;
}

/* Begin the one controlled current-BSS replacement owner token. */
u_int64_t
ieee80211_pae_assoc_epoch_begin_replacement(struct ieee80211com *ic)
{
	u_int64_t epoch;
	u_int64_t txn_id;
	IOSimpleLock *lock;
	IOInterruptState irq;
	void (*cancel)(struct ieee80211com *, u_int64_t);

	if (ic == NULL || ic->ic_opmode != IEEE80211_M_STA)
		return 0;
	lock = ic->ic_pae_selected_bss_lock;
	if (lock == NULL) {
		(void)ieee80211_pae_assoc_epoch_begin(ic);
		return 0;
	}
	irq = IOSimpleLockLockDisableInterrupt(lock);
	epoch = ieee80211_pae_assoc_epoch_advance_locked(ic);
	__atomic_store_n(&ic->ic_pae_assoc_replace_epoch, epoch,
	    __ATOMIC_RELEASE);
	ieee80211_pae_selected_bss_invalidate(ic);
	txn_id = ieee80211_pae_mfp_txn_cancel_locked(ic);
	cancel = ic->ic_pae_mfp_txn_cancel;
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	if (txn_id != 0 && cancel != NULL)
		(*cancel)(ic, txn_id);
	return epoch;
}

/*
 * The final HAL owner calls this only after every controller, IRQ, and task
 * producer has been drained.  ifdetach() intentionally does not free this
 * lock because closed driver queues may still reject late cancellation work.
 */
void
ieee80211_pae_selected_bss_lock_destroy(struct ieee80211com *ic)
{
	IOSimpleLock *lock;
	IOInterruptState irq;

	if (ic == NULL)
		return;
	lock = ic->ic_pae_selected_bss_lock;
	if (lock == NULL)
		return;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	ieee80211_pae_selected_bss_invalidate(ic);
	__atomic_store_n(&ic->ic_pae_assoc_replace_epoch, 0,
	    __ATOMIC_RELEASE);
	ic->ic_pae_selected_bss_lock = NULL;
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	IOSimpleLockFree(lock);
}

/*
 * Preserve only the forward SCAN -> AUTH -> ASSOC -> RUN chain as one
 * attempt. Every other STA state request is a cancellation/retry boundary and
 * must invalidate pending asynchronous work before the driver receives its
 * potentially deferred newstate callback.
 */
void
ieee80211_pae_assoc_epoch_note_newstate(struct ieee80211com *ic,
    enum ieee80211_state nstate)
{
	if (ic == NULL)
		return;
	/* Passive trace ownership follows the same pre-callback state boundary. */
	AirportItlwmPostPltiTraceNoteStateRequest(ic, (uint32_t)ic->ic_state,
	    (uint32_t)nstate);
	if (ic->ic_opmode != IEEE80211_M_STA)
		return;
	if ((ic->ic_state == IEEE80211_S_SCAN &&
	     nstate == IEEE80211_S_AUTH) ||
	    (ic->ic_state == IEEE80211_S_AUTH &&
	     nstate == IEEE80211_S_ASSOC) ||
	    (ic->ic_state == IEEE80211_S_ASSOC &&
	     nstate == IEEE80211_S_RUN))
		return;
	(void)ieee80211_pae_assoc_epoch_begin(ic);
}

void
ieee80211_proto_attach(struct _ifnet *ifp)
{
	struct ieee80211com *ic = (struct ieee80211com *)ifp;

	mq_init(&ic->ic_mgtq, IFQ_MAXLEN, IPL_NET);
	mq_init(&ic->ic_pwrsaveq, IFQ_MAXLEN, IPL_NET);

	ifp->if_hdrlen = sizeof(struct ieee80211_frame);

	ic->ic_rtsthreshold = IEEE80211_RTS_MAX;
	ic->ic_fragthreshold = 2346;		/* XXX not used yet */
	ic->ic_fixed_rate = -1;			/* no fixed rate */
	ic->ic_fixed_mcs = -1;			/* no fixed mcs */
	ic->ic_protmode = IEEE80211_PROT_CTSONLY;

	/* protocol state change handler */
	ic->ic_newstate = ieee80211_newstate;

	/* initialize management frame handlers */
	ic->ic_recv_mgmt = ieee80211_recv_mgmt;
	ic->ic_send_mgmt = ieee80211_send_mgmt;
}

void
ieee80211_proto_detach(struct _ifnet *ifp)
{
	struct ieee80211com *ic = (struct ieee80211com *)ifp;

	mq_purge(&ic->ic_mgtq);
	mq_purge(&ic->ic_pwrsaveq);
}

void
ieee80211_print_essid(const u_int8_t *essid, int len)
{
	(void)essid;
	(void)len;
}

#ifdef IEEE80211_DEBUG
void
ieee80211_dump_pkt(const u_int8_t *buf, int len, int rate, int rssi)
{
	(void)buf;
	(void)len;
	(void)rate;
	(void)rssi;
}
#endif

int
ieee80211_fix_rate(struct ieee80211com *ic, struct ieee80211_node *ni,
    int flags)
{
#define	RV(v)	((v) & IEEE80211_RATE_VAL)
	int i, j, ignore, error;
	int okrate, badrate, fixedrate;
	const struct ieee80211_rateset *srs;
	struct ieee80211_rateset *nrs;
	u_int8_t r;

	/*
	 * If the fixed rate check was requested but no fixed rate has been
	 * defined then just remove the check.
	 */
	if ((flags & IEEE80211_F_DOFRATE) && ic->ic_fixed_rate == -1)
		flags &= ~IEEE80211_F_DOFRATE;

	error = 0;
	okrate = badrate = fixedrate = 0;
	srs = &ic->ic_sup_rates[ieee80211_chan2mode(ic, ni->ni_chan)];
	nrs = &ni->ni_rates;
	for (i = 0; i < nrs->rs_nrates; ) {
		ignore = 0;
		if (flags & IEEE80211_F_DOSORT) {
			/*
			 * Sort rates.
			 */
			for (j = i + 1; j < nrs->rs_nrates; j++) {
				if (RV(nrs->rs_rates[i]) >
				    RV(nrs->rs_rates[j])) {
					r = nrs->rs_rates[i];
					nrs->rs_rates[i] = nrs->rs_rates[j];
					nrs->rs_rates[j] = r;
				}
			}
		}
		r = nrs->rs_rates[i] & IEEE80211_RATE_VAL;
		badrate = r;
		if (flags & IEEE80211_F_DOFRATE) {
			/*
			 * Check fixed rate is included.
			 */
			if (r == RV(srs->rs_rates[ic->ic_fixed_rate]))
				fixedrate = r;
		}
		if (flags & IEEE80211_F_DONEGO) {
			/*
			 * Check against supported rates.
			 */
			for (j = 0; j < srs->rs_nrates; j++) {
				if (r == RV(srs->rs_rates[j])) {
					/*
					 * Overwrite with the supported rate
					 * value so any basic rate bit is set.
					 * This insures that response we send
					 * to stations have the necessary basic
					 * rate bit set.
					 */
					nrs->rs_rates[i] = srs->rs_rates[j];
					break;
				}
			}
			if (j == srs->rs_nrates) {
				/*
				 * A rate in the node's rate set is not
				 * supported.  If this is a basic rate and we
				 * are operating as an AP then this is an error.
				 * Otherwise we just discard/ignore the rate.
				 * Note that this is important for 11b stations
				 * when they want to associate with an 11g AP.
				 */
#ifndef IEEE80211_STA_ONLY
				if (ic->ic_opmode == IEEE80211_M_HOSTAP &&
				    (nrs->rs_rates[i] & IEEE80211_RATE_BASIC))
					error++;
#endif
				ignore++;
			}
		}
		if (flags & IEEE80211_F_DODEL) {
			/*
			 * Delete unacceptable rates.
			 */
			if (ignore) {
				nrs->rs_nrates--;
				for (j = i; j < nrs->rs_nrates; j++)
					nrs->rs_rates[j] = nrs->rs_rates[j + 1];
				nrs->rs_rates[j] = 0;
				continue;
			}
		}
		if (!ignore)
			okrate = nrs->rs_rates[i];
		i++;
	}
	if (okrate == 0 || error != 0 ||
	    ((flags & IEEE80211_F_DOFRATE) && fixedrate == 0))
		return badrate | IEEE80211_RATE_BASIC;
	else
		return RV(okrate);
#undef RV
}

/*
 * Reset 11g-related state.
 */
void
ieee80211_reset_erp(struct ieee80211com *ic)
{
	ic->ic_flags &= ~IEEE80211_F_USEPROT;

	ieee80211_set_shortslottime(ic,
	    ic->ic_curmode == IEEE80211_MODE_11A ||
	    (ic->ic_curmode == IEEE80211_MODE_11N &&
	    IEEE80211_IS_CHAN_5GHZ(ic->ic_ibss_chan))
#ifndef IEEE80211_STA_ONLY
	    ||
	    ((ic->ic_curmode == IEEE80211_MODE_11G ||
	    (ic->ic_curmode == IEEE80211_MODE_11N &&
	    IEEE80211_IS_CHAN_2GHZ(ic->ic_ibss_chan))) &&
	     ic->ic_opmode == IEEE80211_M_HOSTAP &&
	     (ic->ic_caps & IEEE80211_C_SHSLOT))
#endif
	);

	if (ic->ic_curmode == IEEE80211_MODE_11A ||
	    (ic->ic_curmode == IEEE80211_MODE_11N &&
	    IEEE80211_IS_CHAN_5GHZ(ic->ic_ibss_chan)) ||
	    (ic->ic_caps & IEEE80211_C_SHPREAMBLE))
		ic->ic_flags |= IEEE80211_F_SHPREAMBLE;
	else
		ic->ic_flags &= ~IEEE80211_F_SHPREAMBLE;
}

/*
 * Set the short slot time state and notify the driver.
 */
void
ieee80211_set_shortslottime(struct ieee80211com *ic, int on)
{
	if (on)
		ic->ic_flags |= IEEE80211_F_SHSLOT;
	else
		ic->ic_flags &= ~IEEE80211_F_SHSLOT;

	/* notify the driver */
	if (ic->ic_updateslot != NULL)
		ic->ic_updateslot(ic);
}

/*
 * This function is called by the 802.1X PACP machine (via an ioctl) when
 * the transmit key machine (4-Way Handshake for 802.11) should run.
 */
int
ieee80211_keyrun(struct ieee80211com *ic, u_int8_t *macaddr)
{
	struct ieee80211_node *ni = ic->ic_bss;
#ifndef IEEE80211_STA_ONLY
	struct ieee80211_pmk *pmk;
#endif

	/* STA must be associated or AP must be ready */
	if (ic->ic_state != IEEE80211_S_RUN ||
	    !(ic->ic_flags & IEEE80211_F_RSNON))
		return ENETDOWN;

	ni->ni_rsn_supp_state = RSNA_SUPP_PTKSTART;
#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode == IEEE80211_M_STA)
#endif
		return 0;	/* supplicant only, do nothing */

#ifndef IEEE80211_STA_ONLY
	/* find the STA with which we must start the key exchange */
	if ((ni = ieee80211_find_node(ic, macaddr)) == NULL) {
		DPRINTF(("no node found for %s\n", ether_sprintf(macaddr)));
		return EINVAL;
	}
	/* check that the STA is in the correct state */
	if (ni->ni_state != IEEE80211_STA_ASSOC ||
	    ni->ni_rsn_state != RSNA_AUTHENTICATION_2) {
		DPRINTF(("unexpected in state %d\n", ni->ni_rsn_state));
		return EINVAL;
	}
	ni->ni_rsn_state = RSNA_INITPMK;

	/* make sure a PMK is available for this STA, otherwise deauth it */
	if ((pmk = ieee80211_pmksa_find(ic, ni, NULL)) == NULL) {
		DPRINTF(("no PMK available for %s\n", ether_sprintf(macaddr)));
		IEEE80211_SEND_MGMT(ic, ni, IEEE80211_FC0_SUBTYPE_DEAUTH,
		    IEEE80211_REASON_AUTH_LEAVE);
		ieee80211_node_leave(ic, ni);
		return EINVAL;
	}
	memcpy(ni->ni_pmk, pmk->pmk_key, IEEE80211_PMK_LEN);
	memcpy(ni->ni_pmkid, pmk->pmk_pmkid, IEEE80211_PMKID_LEN);
	ni->ni_flags |= IEEE80211_NODE_PMK;

	/* initiate key exchange (4-Way Handshake) with STA */
	return ieee80211_send_4way_msg1(ic, ni);
#endif	/* IEEE80211_STA_ONLY */
}

#ifndef IEEE80211_STA_ONLY
/*
 * Initiate a group key handshake with a node.
 */
static void
ieee80211_node_gtk_rekey(void *arg, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = (struct ieee80211com *)arg;

	if (ni->ni_state != IEEE80211_STA_ASSOC ||
	    ni->ni_rsn_gstate != RSNA_IDLE)
		return;

	/* initiate a group key handshake with STA */
	ni->ni_flags |= IEEE80211_NODE_REKEY;
	if (ieee80211_send_group_msg1(ic, ni) != 0)
		ni->ni_flags &= ~IEEE80211_NODE_REKEY;
}

/*
 * This function is called in HostAP mode when the group key needs to be
 * changed.
 */
void
ieee80211_setkeys(struct ieee80211com *ic)
{
	struct ieee80211_key *k;
	u_int8_t kid;
    int rekeysta = 0;

	/* Swap(GM, GN) */
	kid = (ic->ic_def_txkey == 1) ? 2 : 1;
	k = &ic->ic_nw_keys[kid];
	memset(k, 0, sizeof(*k));
	k->k_id = kid;
	k->k_cipher = ic->ic_bss->ni_rsngroupcipher;
	k->k_flags = IEEE80211_KEY_GROUP | IEEE80211_KEY_TX;
	k->k_len = ieee80211_cipher_keylen(k->k_cipher);
	arc4random_buf(k->k_key, k->k_len);

	if (ic->ic_caps & IEEE80211_C_MFP) {
		/* Swap(GM_igtk, GN_igtk) */
		kid = (ic->ic_igtk_kid == 4) ? 5 : 4;
		k = &ic->ic_nw_keys[kid];
		memset(k, 0, sizeof(*k));
		k->k_id = kid;
		k->k_cipher = ic->ic_bss->ni_rsngroupmgmtcipher;
		k->k_flags = IEEE80211_KEY_IGTK | IEEE80211_KEY_TX;
		k->k_len = 16;
		arc4random_buf(k->k_key, k->k_len);
	}

	ieee80211_iterate_nodes(ic, ieee80211_node_gtk_rekey, ic);
    ieee80211_iterate_nodes(ic, ieee80211_count_rekeysta, &rekeysta);
    if (rekeysta == 0)
        ieee80211_setkeysdone(ic);
}

/*
 * The group key handshake has been completed with all associated stations.
 */
void
ieee80211_setkeysdone(struct ieee80211com *ic)
{
	u_int8_t kid;

	/* install GTK */
	kid = (ic->ic_def_txkey == 1) ? 2 : 1;
    switch ((*ic->ic_set_key)(ic, ic->ic_bss, &ic->ic_nw_keys[kid])) {
        case 0:
        case EBUSY:
            ic->ic_def_txkey = kid;
            break;
        default:
            break;
    }

	if (ic->ic_caps & IEEE80211_C_MFP) {
		/* install IGTK */
		kid = (ic->ic_igtk_kid == 4) ? 5 : 4;
        switch ((*ic->ic_set_key)(ic, ic->ic_bss, &ic->ic_nw_keys[kid])) {
            case 0:
            case EBUSY:
                ic->ic_igtk_kid = kid;
                break;
            default:
                break;
        }
	}
}

/*
 * Group key lifetime has expired, update it.
 */
void
ieee80211_gtk_rekey_timeout(void *arg)
{
	struct ieee80211com *ic = (struct ieee80211com *)arg;
	int s;

	s = splnet();
	ieee80211_setkeys(ic);
	splx(s);

	/* re-schedule a GTK rekeying after 3600s */
	timeout_add_sec(&ic->ic_rsn_timeout, 3600);
}

void
ieee80211_sa_query_timeout(void *arg)
{
	struct ieee80211_node *ni = (struct ieee80211_node *)arg;
	struct ieee80211com *ic = ni->ni_ic;
	int s;

	s = splnet();
	if (++ni->ni_sa_query_count >= 3) {
		ni->ni_flags &= ~IEEE80211_NODE_SA_QUERY;
		ni->ni_flags |= IEEE80211_NODE_SA_QUERY_FAILED;
	} else	/* retry SA Query Request */
		ieee80211_sa_query_request(ic, ni);
	splx(s);
}

/*
 * Request that a SA Query Request frame be sent to a specified peer STA
 * to which the STA is associated.
 */
void
ieee80211_sa_query_request(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	/* MLME-SAQuery.request */

	if (!(ni->ni_flags & IEEE80211_NODE_SA_QUERY)) {
		ni->ni_flags |= IEEE80211_NODE_SA_QUERY;
		ni->ni_flags &= ~IEEE80211_NODE_SA_QUERY_FAILED;
		ni->ni_sa_query_count = 0;
	}
	/* generate new Transaction Identifier */
	ni->ni_sa_query_trid++;

	/* send SA Query Request */
	IEEE80211_SEND_ACTION(ic, ni, IEEE80211_CATEG_SA_QUERY,
	    IEEE80211_ACTION_SA_QUERY_REQ, 0);
	timeout_add_msec(&ni->ni_sa_query_to, 10);
}
#endif	/* IEEE80211_STA_ONLY */

/*
 * Negotiated channel width comes from the peer's HT/VHT/HE operation
 * information.  It still has to be representable by the local channel map:
 * the map carries the firmware/NVM regulatory limits for each primary
 * channel.  Do this validation before a HAL turns ni_chw into a PHY or TLC
 * firmware command.
 */
static void
ieee80211_sanitize_negotiated_chw(struct ieee80211_node *ni)
{
    struct ieee80211_channel *chan = ni->ni_chan;
    int offset;
    int valid = 0;

    if (chan == NULL)
        return;

    offset = chan->ic_freq - chan->ic_center_freq1;
    switch (ni->ni_chw) {
    case IEEE80211_CHAN_WIDTH_20_NOHT:
    case IEEE80211_CHAN_WIDTH_20:
        valid = 1;
        break;
    case IEEE80211_CHAN_WIDTH_40:
        if (offset == -10)
            valid = IEEE80211_IS_CHAN_HT40U(chan);
        else if (offset == 10)
            valid = IEEE80211_IS_CHAN_HT40D(chan);
        break;
    case IEEE80211_CHAN_WIDTH_80:
        valid = IEEE80211_IS_CHAN_VHT80(chan) &&
            (offset == -30 || offset == -10 ||
             offset == 10 || offset == 30);
        break;
    case IEEE80211_CHAN_WIDTH_160:
        valid = IEEE80211_IS_CHAN_VHT160(chan) &&
            (offset == -70 || offset == -50 || offset == -30 ||
             offset == -10 || offset == 10 || offset == 30 ||
             offset == 50 || offset == 70);
        break;
    default:
        break;
    }

    if (valid)
        return;

    /* 80+80 and malformed or NVM-disallowed wide channels are not safe
     * firmware inputs.  Preserve the association at its common 20 MHz
     * width instead of advertising a PHY geometry we cannot program. */
    ni->ni_chw = IEEE80211_CHAN_WIDTH_20;
    chan->ic_center_freq1 = chan->ic_freq;
    chan->ic_center_freq2 = 0;
}

void
ieee80211_ht_negotiate_chw(struct ieee80211com *ic, struct ieee80211_node *ni)
{
    int ht_param;

    if (!ni || !ni->ni_chan)
        return;
    
    ni->ni_chw = IEEE80211_CHAN_WIDTH_20;
    ni->ni_chan->ic_center_freq1 = ni->ni_chan->ic_freq;

    if (((ic->ic_htcaps & IEEE80211_HTCAP_40INTOLERANT) || (ni->ni_htcaps & IEEE80211_HTCAP_40INTOLERANT) || (ic->ic_userflags & IEEE80211_F_NOHT40))
        && IEEE80211_IS_CHAN_2GHZ(ni->ni_chan)) {
        ni->ni_chw = IEEE80211_CHAN_WIDTH_20;
    } else if ((ni->ni_htcaps & IEEE80211_HTCAP_CBW20_40) && IEEE80211_IS_CHAN_HT40(ni->ni_chan) && (ic->ic_htcaps & IEEE80211_HTCAP_CBW20_40)) {
        ht_param = ni->ni_htop0 & IEEE80211_HTOP0_SCO_MASK;
        if ((ht_param == IEEE80211_HTOP0_SCO_SCA) ||
            (ht_param == IEEE80211_HTOP0_SCO_SCB))
            ni->ni_chw = IEEE80211_CHAN_WIDTH_40;
    }
    
    if (ni->ni_chw == IEEE80211_CHAN_WIDTH_40) {
        if ((ni->ni_htop0 & IEEE80211_HTOP0_SCO_MASK) == IEEE80211_HTOP0_SCO_SCA)
            ni->ni_chan->ic_center_freq1 = ni->ni_chan->ic_freq + 10;
        else
            ni->ni_chan->ic_center_freq1 = ni->ni_chan->ic_freq - 10;
    }

    ieee80211_sanitize_negotiated_chw(ni);
}

void
ieee80211_ht_negotiate(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	int i;

	ni->ni_flags &= ~(IEEE80211_NODE_HT | IEEE80211_NODE_HT_SGI20 |
	    IEEE80211_NODE_HT_SGI40);
    ni->ni_chw = IEEE80211_CHAN_WIDTH_20;
    ni->ni_chan->ic_center_freq1 = ni->ni_chan->ic_freq;

	/* Check if we support HT. */
	if ((ic->ic_modecaps & (1 << IEEE80211_MODE_11N)) == 0)
		return;

	/* Check if HT support has been explicitly disabled. */
	if ((ic->ic_flags & IEEE80211_F_HTON) == 0)
		return;

	/*
	 * Check if the peer supports HT.
	 * Require at least one of the mandatory MCS.
	 * MCS 0-7 are mandatory but some APs have particular MCS disabled.
	 */
	if (!ieee80211_node_supports_ht(ni)) {
		ic->ic_stats.is_ht_nego_no_mandatory_mcs++;
		return;
	}

	if (ic->ic_opmode == IEEE80211_M_STA) {
		/* We must support the AP's basic MCS set. */
		for (i = 0; i < IEEE80211_HT_NUM_MCS; i++) {
			if (isset(ni->ni_basic_mcs, i) &&
			    !isset(ic->ic_sup_mcs, i)) {
				ic->ic_stats.is_ht_nego_no_basic_mcs++;
				return;
			}
		}
	}

	/*
	 * Don't allow group cipher (includes WEP) or TKIP
	 * for pairwise encryption (see 802.11-2012 11.1.6).
	 */
	if (ic->ic_flags & IEEE80211_F_WEPON) {
		ic->ic_stats.is_ht_nego_bad_crypto++;
		return;
	}
	if ((ic->ic_flags & IEEE80211_F_RSNON) &&
	    (ni->ni_rsnciphers & IEEE80211_CIPHER_USEGROUP ||
	    ni->ni_rsnciphers & IEEE80211_CIPHER_TKIP)) {
		ic->ic_stats.is_ht_nego_bad_crypto++;
		return;
	}

	ni->ni_flags |= IEEE80211_NODE_HT;
    
    if (ieee80211_node_supports_ht_sgi20(ni))
        ni->ni_flags |= IEEE80211_NODE_HT_SGI20;
    
    ieee80211_ht_negotiate_chw(ic, ni);
    
    if (ni->ni_chw == IEEE80211_CHAN_WIDTH_40 && ieee80211_node_supports_ht_sgi40(ni))
        ni->ni_flags |= IEEE80211_NODE_HT_SGI40;
    
}

void
ieee80211_vht_negotiate(struct ieee80211com *ic, struct ieee80211_node *ni)
{
    uint8_t ext_nss_bw_supp, supp_chwidth;
    uint16_t cf0, cf1;
    int ccfs0, ccfs1, ccfs2;
    int ccf0, ccf1;
    bool support_80_80 = false;
    bool support_160 = false;
    
    ni->ni_flags &= ~(IEEE80211_NODE_VHT | IEEE80211_NODE_VHT_SGI80 |
                      IEEE80211_NODE_VHT_SGI160);
    /* Check if we support VHT. */
    if ((ic->ic_modecaps & (1 << IEEE80211_MODE_11AC)) == 0)
        return;

    if (ic->ic_userflags & IEEE80211_F_NOVHT)
        return;

    /* Check if VHT support has been explicitly disabled. */
    if ((ic->ic_flags & IEEE80211_F_VHTON) == 0)
        return;
    
    if (!IEEE80211_IS_CHAN_5GHZ(ni->ni_chan))
        return;
    
    if (!ieee80211_node_supports_vht(ni)) {
        ic->ic_stats.is_vht_nego_no_mandatory_mcs++;
        return;
    }
    
    /*
     * Don't allow group cipher (includes WEP) or TKIP
     * for pairwise encryption (see 802.11-2012 11.1.6).
     */
    if (ic->ic_flags & IEEE80211_F_WEPON) {
        ic->ic_stats.is_vht_nego_bad_crypto++;
        return;
    }
    if ((ic->ic_flags & IEEE80211_F_RSNON) &&
        (ni->ni_rsnciphers & IEEE80211_CIPHER_USEGROUP ||
        ni->ni_rsnciphers & IEEE80211_CIPHER_TKIP)) {
        ic->ic_stats.is_vht_nego_bad_crypto++;
        return;
    }
    
    support_160 = (ni->ni_vhtcaps & (IEEE80211_VHTCAP_SUPP_CHAN_WIDTH_MASK |
                  IEEE80211_VHTCAP_EXT_NSS_BW_MASK));
    support_80_80 = ((ni->ni_vhtcaps &
             IEEE80211_VHTCAP_SUPP_CHAN_WIDTH_160_80P80MHZ) ||
            (ni->ni_vhtcaps & IEEE80211_VHTCAP_SUPP_CHAN_WIDTH_160MHZ &&
             ni->ni_vhtcaps & IEEE80211_VHTCAP_EXT_NSS_BW_MASK) ||
            ((ni->ni_vhtcaps & IEEE80211_VHTCAP_EXT_NSS_BW_MASK) >>
                    IEEE80211_VHTCAP_EXT_NSS_BW_SHIFT > 1));
    
    ext_nss_bw_supp = u32_get_bits(ni->ni_vhtcaps,
                      IEEE80211_VHTCAP_EXT_NSS_BW_MASK);
    supp_chwidth = u32_get_bits(ni->ni_vhtcaps,
                       IEEE80211_VHTCAP_SUPP_CHAN_WIDTH_MASK);
    
    ccfs0 = ni->ni_vht_chan1;
    ccfs1 = ni->ni_vht_chan2;
    ccfs2 = (le16toh(ni->ni_htop1) &
                IEEE80211_HT_OP_MODE_CCFS2_MASK)
            >> IEEE80211_HT_OP_MODE_CCFS2_SHIFT;
    
    ccf0 = ccfs0;
    
    if ((ic->ic_caps & IEEE80211_C_SUPPORTS_VHT_EXT_NSS_BW) == 0)
        ext_nss_bw_supp = 0;
    
    /*
     * Cf. IEEE 802.11 Table 9-250
     *
     * We really just consider that because it's inefficient to connect
     * at a higher bandwidth than we'll actually be able to use.
     */
    switch ((supp_chwidth << 4) | ext_nss_bw_supp) {
    default:
    case 0x00:
        ccf1 = 0;
        support_160 = false;
        support_80_80 = false;
        break;
    case 0x01:
        support_80_80 = false;
    case 0x02:
    case 0x03:
        ccf1 = ccfs2;
        break;
    case 0x10:
        ccf1 = ccfs1;
        break;
    case 0x11:
    case 0x12:
        if (!ccfs1)
            ccf1 = ccfs2;
        else
            ccf1 = ccfs1;
        break;
    case 0x13:
    case 0x20:
    case 0x23:
        ccf1 = ccfs1;
        break;
    }
    
    cf0 = ieee80211_ieee2mhz(ccf0, ni->ni_chan->ic_flags);
    cf1 = ieee80211_ieee2mhz(ccf1, ni->ni_chan->ic_flags);
    
    switch (ni->ni_vht_chanwidth) {
        case IEEE80211_VHT_CHANWIDTH_80P80MHZ:
            ni->ni_chw = IEEE80211_CHAN_WIDTH_80P80;
            ni->ni_chan->ic_center_freq1 = cf0;
            ni->ni_chan->ic_center_freq2 = cf1;
            break;
        case IEEE80211_VHT_CHANWIDTH_160MHZ:
            ni->ni_chw = IEEE80211_CHAN_WIDTH_160;
            ni->ni_chan->ic_center_freq1 = cf0;
            break;
        case IEEE80211_VHT_CHANWIDTH_80MHZ:
            ni->ni_chw = IEEE80211_CHAN_WIDTH_80;
            ni->ni_chan->ic_center_freq1 = cf0;
            /* If needed, adjust based on the newer interop workaround. */
            if (ccf1) {
                unsigned int diff = abs(ccf1 - ccf0);
                if ((diff == 8) && support_160) {
                    ni->ni_chw = IEEE80211_CHAN_WIDTH_160;
                    ni->ni_chan->ic_center_freq1 = cf1;
                } else if ((diff > 8) && support_80_80) {
                    ni->ni_chw = IEEE80211_CHAN_WIDTH_80P80;
                    ni->ni_chan->ic_center_freq2 = cf1;
                }
            }
            break;
        case IEEE80211_VHT_CHANWIDTH_USE_HT:
            /* Use HT negotiate information */
            break;
            
        default:
            ieee80211_sanitize_negotiated_chw(ni);
            return;
    }
    
    ni->ni_flags |= IEEE80211_NODE_VHT;
    
    if (ieee80211_node_supports_vht_sgi80(ni))
        ni->ni_flags |= IEEE80211_NODE_VHT_SGI80;
    if (ieee80211_node_supports_vht_sgi160(ni))
        ni->ni_flags |= IEEE80211_NODE_VHT_SGI160;

    ieee80211_sanitize_negotiated_chw(ni);
}

void
ieee80211_he_negotiate(struct ieee80211com *ic, struct ieee80211_node *ni)
{
    uint8_t ext_nss_bw_supp, supp_chwidth;
    uint16_t cf0, cf1;
    int ccfs0, ccfs1, ccfs2;
    int ccf0, ccf1;
    bool support_80_80 = false;
    bool support_160 = false;
    struct ieee80211_vht_operation *he_oper_vht = (struct ieee80211_vht_operation *)ni->ni_he_optional;
    
    ni->ni_flags &= ~IEEE80211_NODE_HE;
    
    /* Check if we support HE. */
    if ((ic->ic_modecaps & (1 << IEEE80211_MODE_11AX)) == 0)
        return;

    /* Check if HE support has been explicitly disabled. */
    if ((ic->ic_flags & IEEE80211_F_HEON) == 0)
        return;
    
    ni->ni_flags |= IEEE80211_NODE_HE;
    
    if (!(htole32(ni->ni_he_oper_params) & IEEE80211_HE_OPERATION_VHT_OPER_INFO))
        return;
    
    support_160 = (ni->ni_vhtcaps & (IEEE80211_VHTCAP_SUPP_CHAN_WIDTH_MASK |
                  IEEE80211_VHTCAP_EXT_NSS_BW_MASK));
    support_80_80 = ((ni->ni_vhtcaps &
             IEEE80211_VHTCAP_SUPP_CHAN_WIDTH_160_80P80MHZ) ||
            (ni->ni_vhtcaps & IEEE80211_VHTCAP_SUPP_CHAN_WIDTH_160MHZ &&
             ni->ni_vhtcaps & IEEE80211_VHTCAP_EXT_NSS_BW_MASK) ||
            ((ni->ni_vhtcaps & IEEE80211_VHTCAP_EXT_NSS_BW_MASK) >>
                    IEEE80211_VHTCAP_EXT_NSS_BW_SHIFT > 1));
    
    ext_nss_bw_supp = u32_get_bits(ni->ni_vhtcaps,
                      IEEE80211_VHTCAP_EXT_NSS_BW_MASK);
    supp_chwidth = u32_get_bits(ni->ni_vhtcaps,
                       IEEE80211_VHTCAP_SUPP_CHAN_WIDTH_MASK);
    
    ccfs0 = he_oper_vht->center_freq_seg0_idx;
    ccfs1 = he_oper_vht->center_freq_seg1_idx;
    ccfs2 = (le16toh(ni->ni_htop1) &
                IEEE80211_HT_OP_MODE_CCFS2_MASK)
            >> IEEE80211_HT_OP_MODE_CCFS2_SHIFT;
    
    ccf0 = ccfs0;
    
    if ((ic->ic_caps & IEEE80211_C_SUPPORTS_VHT_EXT_NSS_BW) == 0)
        ext_nss_bw_supp = 0;
    
    /*
     * Cf. IEEE 802.11 Table 9-250
     *
     * We really just consider that because it's inefficient to connect
     * at a higher bandwidth than we'll actually be able to use.
     */
    switch ((supp_chwidth << 4) | ext_nss_bw_supp) {
    default:
    case 0x00:
        ccf1 = 0;
        support_160 = false;
        support_80_80 = false;
        break;
    case 0x01:
        support_80_80 = false;
    case 0x02:
    case 0x03:
        ccf1 = ccfs2;
        break;
    case 0x10:
        ccf1 = ccfs1;
        break;
    case 0x11:
    case 0x12:
        if (!ccfs1)
            ccf1 = ccfs2;
        else
            ccf1 = ccfs1;
        break;
    case 0x13:
    case 0x20:
    case 0x23:
        ccf1 = ccfs1;
        break;
    }
    
    cf0 = ieee80211_ieee2mhz(ccf0, ni->ni_chan->ic_flags);
    cf1 = ieee80211_ieee2mhz(ccf1, ni->ni_chan->ic_flags);
    
    switch (he_oper_vht->chan_width) {
        case IEEE80211_VHT_CHANWIDTH_80P80MHZ:
            ni->ni_chw = IEEE80211_CHAN_WIDTH_80P80;
            ni->ni_chan->ic_center_freq1 = cf0;
            ni->ni_chan->ic_center_freq2 = cf1;
            break;
        case IEEE80211_VHT_CHANWIDTH_160MHZ:
            ni->ni_chw = IEEE80211_CHAN_WIDTH_160;
            ni->ni_chan->ic_center_freq1 = cf0;
            break;
        case IEEE80211_VHT_CHANWIDTH_80MHZ:
            ni->ni_chw = IEEE80211_CHAN_WIDTH_80;
            ni->ni_chan->ic_center_freq1 = cf0;
            /* If needed, adjust based on the newer interop workaround. */
            if (ccf1) {
                unsigned int diff = abs(ccf1 - ccf0);
                if ((diff == 8) && support_160) {
                    ni->ni_chw = IEEE80211_CHAN_WIDTH_160;
                    ni->ni_chan->ic_center_freq1 = cf1;
                } else if ((diff > 8) && support_80_80) {
                    ni->ni_chw = IEEE80211_CHAN_WIDTH_80P80;
                    ni->ni_chan->ic_center_freq2 = cf1;
                }
            }
            break;
        case IEEE80211_VHT_CHANWIDTH_USE_HT:
            /* Use HT negotiate information */
            break;
            
        default:
            ieee80211_sanitize_negotiated_chw(ni);
            return;
    }

    ieee80211_sanitize_negotiated_chw(ni);
}

void
ieee80211_sta_set_rx_nss(struct ieee80211com *ic, struct ieee80211_node *ni)
{
    uint8_t ht_rx_nss = 0, vht_rx_nss = 0, he_rx_nss = 0, rx_nss;
    bool support_160;

    if (ni->ni_flags & IEEE80211_NODE_HE) {
        int i;
        uint8_t rx_mcs_80 = 0, rx_mcs_160 = 0;
        uint16_t mcs_160_map =
            le16toh(ni->ni_he_mcs_nss_supp.rx_mcs_160);
        uint16_t mcs_80_map = le16toh(ni->ni_he_mcs_nss_supp.rx_mcs_80);

        for (i = 7; i >= 0; i--) {
            uint8_t mcs_160 = (mcs_160_map >> (2 * i)) & 3;

            if (mcs_160 != IEEE80211_VHT_MCS_NOT_SUPPORTED) {
                rx_mcs_160 = i + 1;
                break;
            }
        }
        for (i = 7; i >= 0; i--) {
            uint8_t mcs_80 = (mcs_80_map >> (2 * i)) & 3;

            if (mcs_80 != IEEE80211_VHT_MCS_NOT_SUPPORTED) {
                rx_mcs_80 = i + 1;
                break;
            }
        }

        support_160 = ni->ni_he_cap_elem.phy_cap_info[0] &
                  IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G;

        if (support_160)
            he_rx_nss = min(rx_mcs_80, rx_mcs_160);
        else
            he_rx_nss = rx_mcs_80;
    }

    if (ni->ni_flags & IEEE80211_NODE_HT) {
        if (ni->ni_rxmcs[0])
            ht_rx_nss++;
        if (ni->ni_rxmcs[1])
            ht_rx_nss++;
        if (ni->ni_rxmcs[2])
            ht_rx_nss++;
        if (ni->ni_rxmcs[3])
            ht_rx_nss++;
        /* FIXME: consider rx_highest? */
    }

    if (ni->ni_flags & IEEE80211_NODE_VHT) {
        int i;
        uint16_t rx_mcs_map;

        rx_mcs_map = le16toh(ni->ni_vht_mcsinfo.rx_mcs_map);

        for (i = 7; i >= 0; i--) {
            uint8_t mcs = (rx_mcs_map >> (2 * i)) & 3;

            if (mcs != IEEE80211_VHT_MCS_NOT_SUPPORTED) {
                vht_rx_nss = i + 1;
                break;
            }
        }
        /* FIXME: consider rx_highest? */
    }

    rx_nss = max(vht_rx_nss, ht_rx_nss);
    rx_nss = max(he_rx_nss, rx_nss);
    ni->ni_rx_nss = max_t(u8, 1, rx_nss);
}

void
ieee80211_tx_ba_timeout(void *arg)
{
	struct ieee80211_tx_ba *ba = (struct ieee80211_tx_ba *)arg;
	struct ieee80211_node *ni = ba->ba_ni;
	struct ieee80211com *ic = ni->ni_ic;
	u_int8_t tid;
	int s;

	s = splnet();
	tid = ((caddr_t)ba - (caddr_t)ni->ni_tx_ba) / sizeof(*ba);
	if (ba->ba_state == IEEE80211_BA_REQUESTED) {
		/* MLME-ADDBA.confirm(TIMEOUT) */
		ba->ba_state = IEEE80211_BA_INIT;
		if (ni->ni_addba_req_intval[tid] <
		    IEEE80211_ADDBA_REQ_INTVAL_MAX)
			ni->ni_addba_req_intval[tid]++;
		/*
		 * In case the peer believes there is an existing
		 * block ack agreement with us, try to delete it.
		 */
		IEEE80211_SEND_ACTION(ic, ni, IEEE80211_CATEG_BA,
		    IEEE80211_ACTION_DELBA,
		    IEEE80211_REASON_SETUP_REQUIRED << 16 | 1 << 8 | tid);
	} else if (ba->ba_state == IEEE80211_BA_AGREED) {
		/* Block Ack inactivity timeout */
		ic->ic_stats.is_ht_tx_ba_timeout++;
		ieee80211_delba_request(ic, ni, IEEE80211_REASON_TIMEOUT,
		    1, tid);
	}
	splx(s);
}

void
ieee80211_rx_ba_timeout(void *arg)
{
	struct ieee80211_rx_ba *ba = (struct ieee80211_rx_ba *)arg;
	struct ieee80211_node *ni = ba->ba_ni;
	struct ieee80211com *ic = ni->ni_ic;
	u_int8_t tid;
	int s;

	ic->ic_stats.is_ht_rx_ba_timeout++;

	s = splnet();

	/* Block Ack inactivity timeout */
	tid = ((caddr_t)ba - (caddr_t)ni->ni_rx_ba) / sizeof(*ba);
	ieee80211_delba_request(ic, ni, IEEE80211_REASON_TIMEOUT, 0, tid);

	splx(s);
}

/*
 * Request initiation of Block Ack with the specified peer.
 */
int
ieee80211_addba_request(struct ieee80211com *ic, struct ieee80211_node *ni,
    u_int16_t ssn, u_int8_t tid)
{
	struct ieee80211_tx_ba *ba = &ni->ni_tx_ba[tid];

	if (ba->ba_state != IEEE80211_BA_INIT)
		return EBUSY;

	/* MLME-ADDBA.request */

	/* setup Block Ack */
	ba->ba_ni = ni;
	ba->ba_state = IEEE80211_BA_REQUESTED;
	ba->ba_token = ic->ic_dialog_token++;
	ba->ba_timeout_val = 0;
	timeout_set(&ba->ba_to, ieee80211_tx_ba_timeout, ba);
	ba->ba_winsize = IEEE80211_BA_MAX_WINSZ;
	ba->ba_winstart = ssn;
	ba->ba_winend = (ba->ba_winstart + ba->ba_winsize - 1) & 0xfff;
	ba->ba_params =
	    (ba->ba_winsize << IEEE80211_ADDBA_BUFSZ_SHIFT) |
	    (tid << IEEE80211_ADDBA_TID_SHIFT);
    if (ic->ic_caps & IEEE80211_C_AMSDU_IN_AMPDU) {
        ba->ba_params |= IEEE80211_ADDBA_AMSDU;
    }
	if ((ic->ic_htcaps & IEEE80211_HTCAP_DELAYEDBA) == 0)
		/* immediate BA */
		ba->ba_params |= IEEE80211_ADDBA_BA_POLICY;
    
    if ((ic->ic_caps & IEEE80211_C_TX_AMPDU_SETUP_IN_HW) &&
        ic->ic_ampdu_tx_start != NULL) {
        int err = ic->ic_ampdu_tx_start(ic, ni, tid);
        if (err && err != EBUSY) {
            /* driver failed to setup, rollback */
            ieee80211_addba_resp_refuse(ic, ni, tid,
                                        IEEE80211_STATUS_UNSPECIFIED);
        } else if (err == 0)
            ieee80211_addba_resp_accept(ic, ni, tid);
        return err; /* The device will send an ADDBA frame. */
    }

    timeout_add_sec(&ba->ba_to, 1);    /* dot11ADDBAResponseTimeout */
    IEEE80211_SEND_ACTION(ic, ni, IEEE80211_CATEG_BA,
                          IEEE80211_ACTION_ADDBA_REQ, tid);
	return 0;
}

/*
 * Request the deletion of Block Ack with a peer and notify driver.
 */
void
ieee80211_delba_request(struct ieee80211com *ic, struct ieee80211_node *ni,
    u_int16_t reason, u_int8_t dir, u_int8_t tid)
{
	/* MLME-DELBA.request */

	if (reason) {
		/* transmit a DELBA frame */
		IEEE80211_SEND_ACTION(ic, ni, IEEE80211_CATEG_BA,
		    IEEE80211_ACTION_DELBA, reason << 16 | dir << 8 | tid);
	}
	if (dir) {
		/* MLME-DELBA.confirm(Originator) */
		struct ieee80211_tx_ba *ba = &ni->ni_tx_ba[tid];

		if (ic->ic_ampdu_tx_stop != NULL)
			ic->ic_ampdu_tx_stop(ic, ni, tid);

		ba->ba_state = IEEE80211_BA_INIT;
		/* stop Block Ack inactivity timer */
		timeout_del(&ba->ba_to);
	} else {
		/* MLME-DELBA.confirm(Recipient) */
		struct ieee80211_rx_ba *ba = &ni->ni_rx_ba[tid];
		int i;

		if (ic->ic_ampdu_rx_stop != NULL)
			ic->ic_ampdu_rx_stop(ic, ni, tid);

		ba->ba_state = IEEE80211_BA_INIT;
		/* stop Block Ack inactivity timer */
		timeout_del(&ba->ba_to);
		timeout_del(&ba->ba_gap_to);

		if (ba->ba_buf != NULL) {
			/* free all MSDUs stored in reordering buffer */
			for (i = 0; i < IEEE80211_BA_MAX_WINSZ; i++)
				mbuf_freem(ba->ba_buf[i].m);
			/* free reordering buffer */
			free(ba->ba_buf);
			ba->ba_buf = NULL;
		}
	}
}

#ifndef IEEE80211_STA_ONLY
void
ieee80211_auth_open_confirm(struct ieee80211com *ic,
    struct ieee80211_node *ni, uint16_t seq)
{
	struct _ifnet *ifp = &ic->ic_if;

	IEEE80211_SEND_MGMT(ic, ni, IEEE80211_FC0_SUBTYPE_AUTH, seq + 1);
	if (ifp->if_flags & IFF_DEBUG)
		XYLog("%s: station %s %s authenticated (open)\n",
		    ifp->if_xname,
		    ether_sprintf((u_int8_t *)ni->ni_macaddr),
		    ni->ni_state != IEEE80211_STA_CACHE ?
		    "newly" : "already");
	ieee80211_node_newstate(ni, IEEE80211_STA_AUTH);
}
#endif

void
ieee80211_try_another_bss(struct ieee80211com *ic)
{
	struct ieee80211_node *curbs, *selbs;
	struct _ifnet *ifp = &ic->ic_if;

	/* Don't select our current AP again. */
	curbs = ieee80211_find_node(ic, ic->ic_bss->ni_macaddr);
	if (curbs) {
		curbs->ni_fails++;
		ieee80211_node_newstate(curbs, IEEE80211_STA_CACHE);
	}

	/* Try a different AP from the same ESS if available. */
	if (ic->ic_caps & IEEE80211_C_SCANALLBAND) {
		/*
		 * Make sure we will consider APs on all bands during
		 * access point selection in ieee80211_node_choose_bss().
		 * During multi-band scans, our previous AP may be trying
		 * to steer us onto another band by denying authentication.
		 */
		ieee80211_setmode(ic, IEEE80211_MODE_AUTO);
	}
	selbs = ieee80211_node_choose_bss(ic, 0, NULL);
	if (selbs == NULL)
		return;

	/* Should not happen but seriously, don't try the same AP again. */
	if (memcmp(selbs->ni_macaddr, ic->ic_bss->ni_macaddr,
	    IEEE80211_NWID_LEN) == 0)
		return;

	/* Triggers an AUTH->AUTH transition, avoiding another SCAN. */
	ieee80211_node_join_bss(ic, selbs);
}

void
ieee80211_auth_open(struct ieee80211com *ic, const struct ieee80211_frame *wh,
    struct ieee80211_node *ni, struct ieee80211_rxinfo *rxi, u_int16_t seq,
    u_int16_t status)
{
	struct _ifnet *ifp = &ic->ic_if;
	switch (ic->ic_opmode) {
#ifndef IEEE80211_STA_ONLY
	case IEEE80211_M_IBSS:
		if (ic->ic_state != IEEE80211_S_RUN ||
		    seq != IEEE80211_AUTH_OPEN_REQUEST) {
			DPRINTF(("discard auth from %s; state %u, seq %u\n",
			    ether_sprintf((u_int8_t *)wh->i_addr2),
			    ic->ic_state, seq));
			ic->ic_stats.is_rx_bad_auth++;
			return;
		}
		ieee80211_new_state(ic, IEEE80211_S_AUTH,
		    wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK);

		/* In IBSS mode no (re)association frames are sent. */
		if (ic->ic_flags & IEEE80211_F_RSNON)
			ni->ni_rsn_supp_state = RSNA_SUPP_PTKSTART;
		break;

	case IEEE80211_M_AHDEMO:
		/* should not come here */
		break;

	case IEEE80211_M_HOSTAP:
		if (ic->ic_state != IEEE80211_S_RUN ||
		    seq != IEEE80211_AUTH_OPEN_REQUEST) {
			DPRINTF(("discard auth from %s; state %u, seq %u\n",
			    ether_sprintf((u_int8_t *)wh->i_addr2),
			    ic->ic_state, seq));
			ic->ic_stats.is_rx_bad_auth++;
			return;
		}
		if (ni == ic->ic_bss) {
			ni = ieee80211_find_node(ic, wh->i_addr2);
			if (ni == NULL)
				ni = ieee80211_alloc_node(ic, wh->i_addr2);
			if (ni == NULL) {
				return;
			}
			IEEE80211_ADDR_COPY(ni->ni_bssid, ic->ic_bss->ni_bssid);
			ni->ni_rssi = rxi->rxi_rssi;
			ni->ni_rstamp = rxi->rxi_tstamp;
			ni->ni_chan = ic->ic_bss->ni_chan;
		}

		/*
		 * Drivers may want to set up state before confirming.
		 * In which case this returns EBUSY and the driver will
		 * later call ieee80211_auth_open_confirm() by itself.
		 */
		if (ic->ic_newauth && ic->ic_newauth(ic, ni,
		    ni->ni_state != IEEE80211_STA_CACHE, seq) != 0)
			break;
		ieee80211_auth_open_confirm(ic, ni, seq);
		break;
#endif	/* IEEE80211_STA_ONLY */

	case IEEE80211_M_STA:
		if (ic->ic_state != IEEE80211_S_AUTH ||
		    seq != IEEE80211_AUTH_OPEN_RESPONSE) {
			ic->ic_stats.is_rx_bad_auth++;
			DPRINTF(("discard auth from %s; state %u, seq %u\n",
			    ether_sprintf((u_int8_t *)wh->i_addr2),
			    ic->ic_state, seq));
			return;
		}
		/* A rejected current-BSS auth response ends the active attempt. */
		if (status != 0 && ni == ic->ic_bss)
			(void)ieee80211_pae_assoc_epoch_begin(ic);
		if (ic->ic_flags & IEEE80211_F_RSNON) {
			/* XXX not here! */
			ic->ic_bss->ni_flags &= ~IEEE80211_NODE_TXRXPROT;
			ic->ic_bss->ni_port_valid = 0;
			ic->ic_bss->ni_replaycnt_ok = 0;
			(*ic->ic_delete_key)(ic, ic->ic_bss,
			    &ic->ic_bss->ni_pairwise_key);
		}
		if (status != 0) {
			if (ifp->if_flags & IFF_DEBUG)
				XYLog("%s: open authentication failed "
				    "(status %d) for %s\n", ifp->if_xname,
				    status,
				    ether_sprintf((u_int8_t *)wh->i_addr3));
			if (ni != ic->ic_bss)
				ni->ni_fails++;
			else
				ieee80211_try_another_bss(ic);
			ic->ic_stats.is_rx_auth_fail++;
			return;
		}
		ieee80211_new_state(ic, IEEE80211_S_ASSOC,
		    wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK);
		break;
	default:
		break;
	}
}

void
ieee80211_set_beacon_miss_threshold(struct ieee80211com *ic)
{
	/*
	 * Scale the missed beacon counter threshold to the AP's actual
     * beacon interval.
	 */
	int btimeout = MIN(IEEE80211_BEACON_MISS_THRES * ic->ic_bss->ni_intval,
         IEEE80211_BEACON_MISS_THRES * (IEEE80211_DUR_TU / 10));
    /* Ensure that at least one beacon may be missed. */
	btimeout = MAX(btimeout, 2 * ic->ic_bss->ni_intval);
	if (ic->ic_bss->ni_intval > 0) /* don't crash if interval is bogus */
		ic->ic_bmissthres = btimeout / ic->ic_bss->ni_intval;

}

/* Tell our peer, and the driver, to stop A-MPDU Tx for all TIDs. */
void
ieee80211_stop_ampdu_tx(struct ieee80211com *ic, struct ieee80211_node *ni,
    int mgt)
{
	int tid;

	for (tid = 0; tid < nitems(ni->ni_tx_ba); tid++) {
		struct ieee80211_tx_ba *ba = &ni->ni_tx_ba[tid];
		if (ba->ba_state != IEEE80211_BA_AGREED)
			continue;
        ieee80211_delba_request(ic, ni,
                                ((ic->ic_caps & IEEE80211_C_TX_AMPDU_SETUP_IN_HW) || mgt == -1) ? 0 : IEEE80211_REASON_AUTH_LEAVE, 1, tid);
	}
}

void
ieee80211_check_wpa_supplicant_failure(struct ieee80211com *ic,
    struct ieee80211_node *ni)
{
	struct ieee80211_node *ni2;

	if (ic->ic_opmode != IEEE80211_M_STA
#ifndef IEEE80211_STA_ONLY
	    && ic->ic_opmode != IEEE80211_M_IBSS
#endif
	    )
		return;

	if (ni->ni_rsn_supp_state != RSNA_SUPP_PTKNEGOTIATING)
		return;

	ni->ni_assoc_fail |= IEEE80211_NODE_ASSOCFAIL_WPA_KEY;

	if (ni != ic->ic_bss)
		return;

	/* Also update the copy of our AP's node in the node cache. */
	ni2 = ieee80211_find_node(ic, ic->ic_bss->ni_macaddr);
	if (ni2)
		ni2->ni_assoc_fail |= ic->ic_bss->ni_assoc_fail;
}

int
ieee80211_newstate(struct ieee80211com *ic, enum ieee80211_state nstate,
    int mgt)
{
	struct _ifnet *ifp = &ic->ic_if;
	struct ieee80211_node *ni;
	enum ieee80211_state ostate;
#ifndef IEEE80211_STA_ONLY
	int s;
#endif

	ostate = ic->ic_state;
	ic->ic_state = nstate;			/* state transition */
	ni = ic->ic_bss;			/* NB: no reference held */
	ieee80211_set_link_state(ic, LINK_STATE_DOWN);
	ic->ic_xflags &= ~IEEE80211_F_TX_MGMT_ONLY;
	switch (nstate) {
	case IEEE80211_S_INIT:
		/*
		 * If mgt = -1, driver is already partway down, so do
		 * not send management frames.
		 */
		/*
		 * Publish the WCL reassociation terminal failure
		 * selector when an in-flight host-owned reassociation
		 * owner is invalidated by a driver-initiated reset
		 * (signalled by mgt == -1 entering INIT). The helper's
		 * post-send gate filters non-active and pre-send
		 * owners, so unrelated INIT transitions remain silent.
		 */
		if (mgt == -1)
			ieee80211_wcl_reassoc_post_failure(ic,
			    (u_int32_t)ECANCELED);
		switch (ostate) {
		case IEEE80211_S_INIT:
			break;
		case IEEE80211_S_RUN:
			if (mgt == -1)
				goto justcleanup;
			ieee80211_stop_ampdu_tx(ic, ni, mgt);
			ieee80211_ba_del(ni);
			switch (ic->ic_opmode) {
			case IEEE80211_M_STA:
				IEEE80211_SEND_MGMT(ic, ni,
				    IEEE80211_FC0_SUBTYPE_DISASSOC,
				    IEEE80211_REASON_ASSOC_LEAVE);
				break;
#ifndef IEEE80211_STA_ONLY
			case IEEE80211_M_HOSTAP:
				s = splnet();
				RB_FOREACH(ni, ieee80211_tree, &ic->ic_tree) {
					if (ni->ni_state != IEEE80211_STA_ASSOC)
						continue;
					IEEE80211_SEND_MGMT(ic, ni,
					    IEEE80211_FC0_SUBTYPE_DISASSOC,
					    IEEE80211_REASON_ASSOC_LEAVE);
				}
				splx(s);
				break;
#endif
			default:
				break;
			}
			/* FALLTHROUGH */
		case IEEE80211_S_ASSOC:
			if (mgt == -1)
				goto justcleanup;
			switch (ic->ic_opmode) {
			case IEEE80211_M_STA:
				IEEE80211_SEND_MGMT(ic, ni,
				    IEEE80211_FC0_SUBTYPE_DEAUTH,
				    IEEE80211_REASON_AUTH_LEAVE);
				break;
#ifndef IEEE80211_STA_ONLY
			case IEEE80211_M_HOSTAP:
				s = splnet();
				RB_FOREACH(ni, ieee80211_tree, &ic->ic_tree) {
					IEEE80211_SEND_MGMT(ic, ni,
					    IEEE80211_FC0_SUBTYPE_DEAUTH,
					    IEEE80211_REASON_AUTH_LEAVE);
				}
				splx(s);
				break;
#endif
			default:
				break;
			}
			/* FALLTHROUGH */
		case IEEE80211_S_AUTH:
		case IEEE80211_S_SCAN:
justcleanup:
#ifndef IEEE80211_STA_ONLY
			if (ic->ic_opmode == IEEE80211_M_HOSTAP)
				timeout_del(&ic->ic_rsn_timeout);
#endif
			ieee80211_ba_del(ni);
			timeout_del(&ic->ic_bgscan_timeout);
			ic->ic_bgscan_fail = 0;
			ic->ic_mgt_timer = 0;
			mq_purge(&ic->ic_mgtq);
			mq_purge(&ic->ic_pwrsaveq);
			ieee80211_free_allnodes(ic, 1);
			break;
		}
		ni->ni_rsn_supp_state = RSNA_SUPP_INITIALIZE;
		ni->ni_assoc_fail = 0;
		if (ic->ic_flags & IEEE80211_F_RSNON)
			ieee80211_crypto_clear_groupkeys(ic);
		break;
	case IEEE80211_S_SCAN:
		ic->ic_flags &= ~IEEE80211_F_SIBSS;
		/* initialize bss for probe request */
		IEEE80211_ADDR_COPY(ni->ni_macaddr, etherbroadcastaddr);
		IEEE80211_ADDR_COPY(ni->ni_bssid, etherbroadcastaddr);
		ni->ni_rates = ic->ic_sup_rates[
			ieee80211_chan2mode(ic, ni->ni_chan)];
		ni->ni_associd = 0;
		ni->ni_rstamp = 0;
		ni->ni_rsn_supp_state = RSNA_SUPP_INITIALIZE;
		if (ic->ic_flags & IEEE80211_F_RSNON)
			ieee80211_crypto_clear_groupkeys(ic);
		switch (ostate) {
		case IEEE80211_S_INIT:
#ifndef IEEE80211_STA_ONLY
			if (ic->ic_opmode == IEEE80211_M_HOSTAP &&
			    ic->ic_des_chan != IEEE80211_CHAN_ANYC) {
				/*
				 * AP operation and we already have a channel;
				 * bypass the scan and startup immediately.
				 */
				ieee80211_create_ibss(ic, ic->ic_des_chan);
			} else
#endif
				ieee80211_begin_scan(ifp);
			break;
		case IEEE80211_S_SCAN:
			/* scan next */
			if (ic->ic_flags & IEEE80211_F_ASCAN) {
				IEEE80211_SEND_MGMT(ic, ni,
				    IEEE80211_FC0_SUBTYPE_PROBE_REQ, 0);
			}
			break;
		case IEEE80211_S_RUN:
			/* beacon miss */
			if (ifp->if_flags & IFF_DEBUG) {
				/* XXX bssid clobbered above */
				XYLog("%s: no recent beacons from %s;"
				    " rescanning\n", ifp->if_xname,
				    ether_sprintf(ic->ic_bss->ni_bssid));
			}
			timeout_del(&ic->ic_bgscan_timeout);
			ic->ic_bgscan_fail = 0;
			ieee80211_stop_ampdu_tx(ic, ni, mgt);
			ieee80211_free_allnodes(ic, 1);
			/* FALLTHROUGH */
		case IEEE80211_S_AUTH:
		case IEEE80211_S_ASSOC:
			/* timeout restart scan */
			ni = ieee80211_find_node(ic, ic->ic_bss->ni_macaddr);
			if (ni != NULL)
				ni->ni_fails++;
			ieee80211_begin_scan(ifp);
			break;
		}
		break;
	case IEEE80211_S_AUTH:
        ieee80211_clean_sta_bss_node(ic);
		if (ostate == IEEE80211_S_RUN)
			ieee80211_check_wpa_supplicant_failure(ic, ni);
		ni->ni_rsn_supp_state = RSNA_SUPP_INITIALIZE;
		if (ic->ic_flags & IEEE80211_F_RSNON)
			ieee80211_crypto_clear_groupkeys(ic);
		switch (ostate) {
		case IEEE80211_S_INIT:
			if (ifp->if_flags & IFF_DEBUG)
				XYLog("%s: invalid transition %s -> %s\n",
				    ifp->if_xname, ieee80211_state_name[ostate],
				    ieee80211_state_name[nstate]);
			break;
		case IEEE80211_S_SCAN:
			IEEE80211_SEND_MGMT(ic, ni,
			    IEEE80211_FC0_SUBTYPE_AUTH, 1);
			break;
		case IEEE80211_S_AUTH:
		case IEEE80211_S_ASSOC:
			switch (mgt) {
			case IEEE80211_FC0_SUBTYPE_AUTH:
				if (ic->ic_opmode == IEEE80211_M_STA) {
					IEEE80211_SEND_MGMT(ic, ni,
					    IEEE80211_FC0_SUBTYPE_AUTH,
					    IEEE80211_AUTH_OPEN_REQUEST);
				}
				break;
			case IEEE80211_FC0_SUBTYPE_DEAUTH:
				/* ignore and retry scan on timeout */
				break;
			}
			break;
		case IEEE80211_S_RUN:
			timeout_del(&ic->ic_bgscan_timeout);
			ic->ic_bgscan_fail = 0;
			ieee80211_stop_ampdu_tx(ic, ni, mgt);
			ieee80211_ba_del(ni);
			switch (mgt) {
			case IEEE80211_FC0_SUBTYPE_AUTH:
				IEEE80211_SEND_MGMT(ic, ni,
				    IEEE80211_FC0_SUBTYPE_AUTH, 2);
				ic->ic_state = ostate;	/* stay RUN */
				break;
			case IEEE80211_FC0_SUBTYPE_DEAUTH:
				/* try to reauth */
				IEEE80211_SEND_MGMT(ic, ni,
				    IEEE80211_FC0_SUBTYPE_AUTH, 1);
				break;
			}
			break;
		}
		break;
	case IEEE80211_S_ASSOC:
		switch (ostate) {
		case IEEE80211_S_INIT:
		case IEEE80211_S_SCAN:
		case IEEE80211_S_ASSOC:
			if (ifp->if_flags & IFF_DEBUG)
				XYLog("%s: invalid transition %s -> %s\n",
				    ifp->if_xname, ieee80211_state_name[ostate],
				    ieee80211_state_name[nstate]);
			break;
		case IEEE80211_S_AUTH:
			IEEE80211_SEND_MGMT(ic, ni,
			    IEEE80211_FC0_SUBTYPE_ASSOC_REQ, 0);
			break;
		case IEEE80211_S_RUN:
			ieee80211_stop_ampdu_tx(ic, ni, mgt);
			ieee80211_ba_del(ni);
			IEEE80211_SEND_MGMT(ic, ni,
			    IEEE80211_FC0_SUBTYPE_ASSOC_REQ, 1);
			break;
		}
		break;
	case IEEE80211_S_RUN:
		switch (ostate) {
		case IEEE80211_S_INIT:
			if (ic->ic_opmode == IEEE80211_M_MONITOR)
				break;
		case IEEE80211_S_AUTH:
		case IEEE80211_S_RUN:
			if (ifp->if_flags & IFF_DEBUG)
				XYLog("%s: invalid transition %s -> %s\n",
				    ifp->if_xname, ieee80211_state_name[ostate],
				    ieee80211_state_name[nstate]);
			break;
		case IEEE80211_S_SCAN:		/* adhoc/hostap mode */
		case IEEE80211_S_ASSOC:		/* infra mode */
			if (ni->ni_txrate >= ni->ni_rates.rs_nrates)
				panic("%s: bogus xmit rate %u setup",
				    __FUNCTION__, ni->ni_txrate);
#ifdef USE_APPLE_SUPPLICANT
            {
#elif (defined IO80211FAMILY_V2)
            if (ieee80211_is_8021x_akm((enum ieee80211_akm)ni->ni_rsnakms) ||
                !(ic->ic_flags & IEEE80211_F_RSNON)) {
#else
			if (!(ic->ic_flags & IEEE80211_F_RSNON)) {
#endif
				/*
				 * NB: When RSN is enabled, we defer setting
				 * the link up until the port is valid.
				 */
				ieee80211_set_link_state(ic, LINK_STATE_UP);
				ni->ni_assoc_fail = 0;
			}
            ni->ni_fails = 0;
            ni = ieee80211_find_node(ic, ni->ni_macaddr);
            if (ni)
                ni->ni_fails = 0;
			ic->ic_mgt_timer = 0;
			ieee80211_set_beacon_miss_threshold(ic);
			(*ifp->if_start)(ifp);
			break;
		}
		break;
	}
	return 0;
}

void
ieee80211_set_link_state(struct ieee80211com *ic, int nstate)
{
	struct _ifnet *ifp = &ic->ic_if;
    int link_state;
    
	switch (ic->ic_opmode) {
#ifndef IEEE80211_STA_ONLY
	case IEEE80211_M_IBSS:
	case IEEE80211_M_HOSTAP:
		nstate = LINK_STATE_UNKNOWN;
		break;
#endif
	case IEEE80211_M_MONITOR:
		nstate = LINK_STATE_DOWN;
		break;
	default:
		break;
	}
    link_state = nstate;
    if (link_state != ifp->if_link_state) {
        ifp->if_link_state = link_state;
#if defined(__IO80211_TARGET) && __IO80211_TARGET >= __MAC_26_0
        /*
         * Passive Tahoe census marker for the actual net80211 bridge edge.
         * Take the existing atomic association epoch before the unchanged
         * controller call below; the bridge cannot publish, defer, retry, or
         * otherwise alter this link-state transition.
         */
        AirportItlwmRegDiagNet80211LinkContext(
            ic, (uint32_t)link_state, ieee80211_pae_assoc_epoch_current(ic));
#endif
        if (link_state == LINK_STATE_UP) {
            ifp->controller->setLinkStatus(kIONetworkLinkValid | kIONetworkLinkActive, ifp->controller->getCurrentMedium());
        } else {
            ifp->controller->setLinkStatus(kIONetworkLinkValid);
        }
    }
//	if (nstate != ifp->if_link_state) {
//		ifp->if_link_state = nstate;
//		if (LINK_STATE_IS_UP(nstate)) {
//			struct if_ieee80211_data ifie;
//			memset(&ifie, 0, sizeof(ifie));
//			ifie.ifie_nwid_len = ic->ic_bss->ni_esslen;
//			memcpy(ifie.ifie_nwid, ic->ic_bss->ni_essid,
//			    sizeof(ifie.ifie_nwid));
//			memcpy(ifie.ifie_addr, ic->ic_bss->ni_bssid,
//			    sizeof(ifie.ifie_addr));
//			ifie.ifie_channel = ieee80211_chan2ieee(ic,
//			    ic->ic_bss->ni_chan);
//			ifie.ifie_flags = ic->ic_flags;
//			ifie.ifie_xflags = ic->ic_xflags;
//			rtm_80211info(&ic->ic_if, &ifie);
//		}
//		if_link_state_change(ifp);
//	}
}
