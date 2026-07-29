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
#include <net80211/ieee80211_sae_admission.h>
#include <HAL/ItlSaePmkContinuationV1.h>
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

/* Value moved out of a cancelled generic transaction while the selected-BSS
 * leaf lock is held, then destroyed only after that lock is dropped. */
struct ieee80211_pae_mfp_prepared {
	struct ieee80211_key bip_key;
	int bip_installed;
};

static void ieee80211_pae_mfp_txn_dispose_prepared(struct ieee80211com *,
	struct ieee80211_pae_mfp_prepared *);

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
ieee80211_pae_mfp_txn_cancel_locked(struct ieee80211com *ic,
	struct ieee80211_pae_mfp_prepared *prepared)
{
	struct ieee80211_pae_mfp_txn *txn = &ic->ic_pae_mfp_txn;
	u_int64_t id;

	_KASSERT(prepared != NULL);
	bzero(prepared, sizeof(*prepared));
	if (!txn->active)
		return 0;
	id = txn->id;
	if (txn->prepared_bip_installed) {
		prepared->bip_key = txn->prepared_bip_key;
		prepared->bip_installed = 1;
	}
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

/* The reply path needs a temporary MIC view of this node before the final
 * commit.  Keep that short-lived mutation under the same selected-BSS lock
 * as the transaction record so an epoch replacement cannot restore state
 * into a different BSS. */
struct ieee80211_pae_mfp_mic_state {
	struct ieee80211_ptk ptk;
	u_int64_t replaycnt;
	u_int8_t replaycnt_ok;
	u_int rsn_supp_state;
};

/* The BIP context is deliberately local until the backend has accepted the
 * successful key handoff.  It therefore has no RX/TX reader and can be
 * synchronously destroyed on a rejected handoff without touching a live
 * group-key slot. */
static int
ieee80211_pae_mfp_txn_stage_mic_state(struct ieee80211com *ic,
	const struct ieee80211_pae_mfp_txn *txn,
	struct ieee80211_pae_mfp_mic_state *old)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_node *ni;
	int live = 0;

	if (ic == NULL || txn == NULL || old == NULL ||
	    (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return 0;
	ni = txn->ni;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	if (ieee80211_pae_mfp_txn_live_locked(ic, &ic->ic_pae_mfp_txn,
	    txn->id, txn->assoc_epoch, ni)) {
		old->ptk = ni->ni_ptk;
		old->replaycnt = ni->ni_replaycnt;
		old->replaycnt_ok = ni->ni_replaycnt_ok;
		old->rsn_supp_state = ni->ni_rsn_supp_state;
		if (txn->have_ptk)
			ni->ni_ptk = txn->ptk;
		ni->ni_replaycnt = txn->replaycnt;
		ni->ni_replaycnt_ok = 1;
		live = 1;
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return live;
}

static void
ieee80211_pae_mfp_txn_restore_mic_state(struct ieee80211com *ic,
	const struct ieee80211_pae_mfp_txn *txn,
	const struct ieee80211_pae_mfp_mic_state *old)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_node *ni;

	if (ic == NULL || txn == NULL || old == NULL ||
	    (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return;
	ni = txn->ni;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	if (ieee80211_pae_mfp_txn_live_locked(ic, &ic->ic_pae_mfp_txn,
	    txn->id, txn->assoc_epoch, ni)) {
		if (txn->have_ptk)
			ni->ni_ptk = old->ptk;
		ni->ni_replaycnt = old->replaycnt;
		ni->ni_replaycnt_ok = old->replaycnt_ok;
		ni->ni_rsn_supp_state = old->rsn_supp_state;
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
}

static int
ieee80211_pae_mfp_igtk_shape_valid(const struct ieee80211_key *key)
{
	return key != NULL && key->k_id >= IEEE80211_WEP_NKID &&
	    key->k_id < IEEE80211_GROUP_NKID &&
	    key->k_cipher == IEEE80211_CIPHER_BIP &&
	    (key->k_flags & IEEE80211_KEY_IGTK) != 0 && key->k_len == 16;
}

/* Read-only current-BSS/epoch fence for post-handoff notifications.  Unlike
 * terminal_current(), it must not mutate the WCL request on a successful
 * association merely to decide whether LinkUp/RSN_DONE is still current. */
static int
ieee80211_pae_mfp_txn_current(struct ieee80211com *ic,
	struct ieee80211_node *ni, u_int64_t epoch)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	int current = 0;

	if (ic == NULL || ni == NULL ||
	    (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return 0;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	current = ic->ic_bss == ni &&
	    __atomic_load_n(&ic->ic_pae_assoc_epoch, __ATOMIC_ACQUIRE) == epoch;
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return current;
}

/* Caller holds ic_pae_selected_bss_lock.  This is the one atomic transition
 * from an accepted backend PAE owner to normal associated-key lifetime: the
 * BIP helper only moves/queues contexts and does not allocate, reap, call
 * back, or drop this leaf lock. */
int
ieee80211_pae_mfp_txn_finish_publish_locked(struct ieee80211com *ic,
	u_int64_t id)
{
	struct ieee80211_pae_mfp_txn *txn;
	struct ieee80211_node *ni;
	int error, ptk_software = 0, gtk_software = 0;

	if (ic == NULL)
		return EINVAL;
	txn = &ic->ic_pae_mfp_txn;
	ni = txn->ni;
	if (!ieee80211_pae_mfp_txn_live_locked(ic, txn, id,
	    txn->assoc_epoch, ni) || txn->phase != IEEE80211_PAE_MFP_STAGE_NONE ||
	    txn->finish_published)
		return ECANCELED;
	if (txn->have_igtk &&
	    (!txn->prepared_bip_installed ||
	     !ieee80211_pae_mfp_igtk_shape_valid(&txn->prepared_bip_key) ||
	     txn->prepared_bip_key.k_priv == NULL))
		return EIO;
	/* Validate every software-CCMP replacement before the BIP transfer below.
	 * The subsequent CCMP moves cannot fail under this same leaf lock, so the
	 * final handoff remains all-or-nothing rather than publishing an IGTK next
	 * to a half-replaced PTK/GTK lifetime. */
	if (txn->have_ptk && txn->ptk_key.k_priv != NULL) {
		error = ieee80211_ccmp_key_publishable_locked(ic,
		    &ni->ni_pairwise_key, &txn->ptk_key);
		if (error != 0)
			return error;
		ptk_software = 1;
	} else if (txn->have_ptk &&
	    (ni->ni_pairwise_key.k_flags & IEEE80211_KEY_PAE_MFP_LIVE))
		return EBUSY;
	if (txn->have_gtk && txn->gtk_key.k_id >= IEEE80211_WEP_NKID)
		return EINVAL;
	if (txn->have_gtk && txn->gtk_key.k_priv != NULL) {
		error = ieee80211_ccmp_key_publishable_locked(ic,
		    &ic->ic_nw_keys[txn->gtk_key.k_id], &txn->gtk_key);
		if (error != 0)
			return error;
		gtk_software = 1;
	} else if (txn->have_gtk &&
	    (ic->ic_nw_keys[txn->gtk_key.k_id].k_flags &
	    IEEE80211_KEY_PAE_MFP_LIVE))
		return EBUSY;

	/* Do the only fallible transfer first: every remaining publication below
	 * is a value copy under this lock.  On error the helper leaves both slots
	 * untouched and the generic cancellation path still owns local BIP. */
	if (txn->have_igtk) {
		error = ieee80211_bip_key_publish_retire_locked(ic,
		    &ic->ic_nw_keys[txn->prepared_bip_key.k_id],
		    &txn->prepared_bip_key);
		if (error != 0)
			return error;
		txn->prepared_bip_installed = 0;
		explicit_bzero(&txn->prepared_bip_key,
		    sizeof(txn->prepared_bip_key));
	}
	if (txn->have_ptk) {
		ni->ni_ptk = txn->ptk;
		if (ptk_software) {
			error = ieee80211_ccmp_key_publish_retire_locked(ic,
			    &ni->ni_pairwise_key, &txn->ptk_key);
			_KASSERT(error == 0);
		} else
			ni->ni_pairwise_key = txn->ptk_key;
		ni->ni_flags &= ~IEEE80211_NODE_RSN_NEW_PTK;
		ni->ni_flags &= ~IEEE80211_NODE_TXRXPROT;
		ni->ni_flags |= IEEE80211_NODE_RXPROT;
	}
	if (txn->have_gtk) {
		if (gtk_software) {
			error = ieee80211_ccmp_key_publish_retire_locked(ic,
			    &ic->ic_nw_keys[txn->gtk_key.k_id], &txn->gtk_key);
			_KASSERT(error == 0);
		} else
			ic->ic_nw_keys[txn->gtk_key.k_id] = txn->gtk_key;
	}
	if (txn->have_igtk)
		ni->ni_flags |= IEEE80211_NODE_TXMGMTPROT |
		    IEEE80211_NODE_RXMGMTPROT;
	if (txn->key_info & EAPOL_KEY_INSTALL)
		ni->ni_flags |= IEEE80211_NODE_TXRXPROT;
	ni->ni_replaycnt = txn->replaycnt;
	ni->ni_replaycnt_ok = 1;
	if (txn->key_info & EAPOL_KEY_SECURE) {
		ni->ni_flags |= IEEE80211_NODE_TXRXPROT;
		txn->finish_port_became_valid = !ni->ni_port_valid;
		ni->ni_port_valid = 1;
		ni->ni_assoc_fail = 0;
		if (ic->ic_opmode == IEEE80211_M_STA)
			ic->ic_rsngroupcipher = ni->ni_rsngroupcipher;
	}
	/* prepare_reply() temporarily enters PTKDONE to build Msg4, then restores
	 * its MIC view before the backend handoff.  Make that state durable only
	 * after this transaction's key publication has been accepted. */
	if (txn->reply == IEEE80211_PAE_MFP_REPLY_4WAY_MSG4)
		ni->ni_rsn_supp_state = RNSA_SUPP_PTKDONE;
	txn->finish_published = 1;
	return 0;
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
	struct ieee80211_pae_mfp_prepared prepared;
	u_int64_t id;

	if (ic == NULL || (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	id = ieee80211_pae_mfp_txn_cancel_locked(ic, &prepared);
	cancel = ic->ic_pae_mfp_txn_cancel;
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	ieee80211_pae_mfp_txn_dispose_prepared(ic, &prepared);
	if (id != 0 && cancel != NULL)
		(*cancel)(ic, id);
}

/*
 * A submit callback runs after the selected-BSS leaf has dropped.  A roam can
 * therefore replace the generic value record before the old callback reports
 * failure.  Never let that old failure abort whichever transaction happened
 * to become current in the meantime: remove only the exact id/epoch/BSS
 * record that this caller created.  A false result is an expected stale
 * completion, not a reason to tear down the replacement association.
 */
static int
ieee80211_pae_mfp_txn_abort_exact(struct ieee80211com *ic, u_int64_t id,
	u_int64_t epoch, struct ieee80211_node *ni)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	void (*cancel)(struct ieee80211com *, u_int64_t);
	struct ieee80211_pae_mfp_prepared prepared;
	u_int64_t cancel_id = 0;
	int current = 0;

	if (ic == NULL || ni == NULL || id == 0 || epoch == 0 ||
	    (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return 0;
	bzero(&prepared, sizeof(prepared));
	irq = IOSimpleLockLockDisableInterrupt(lock);
	if (ic->ic_pae_mfp_txn.active && ic->ic_pae_mfp_txn.id == id &&
	    ic->ic_pae_mfp_txn.assoc_epoch == epoch &&
	    ic->ic_pae_mfp_txn.ni == ni) {
		current = ic->ic_bss == ni &&
		    __atomic_load_n(&ic->ic_pae_assoc_epoch,
		    __ATOMIC_ACQUIRE) == epoch;
		cancel = ic->ic_pae_mfp_txn_cancel;
		cancel_id = ieee80211_pae_mfp_txn_cancel_locked(ic, &prepared);
	} else
		cancel = NULL;
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	ieee80211_pae_mfp_txn_dispose_prepared(ic, &prepared);
	if (cancel_id != 0 && cancel != NULL)
		(*cancel)(ic, cancel_id);
	return cancel_id != 0 && current;
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
	void (*cancel)(struct ieee80211com *, u_int64_t);
	int (*finish)(struct ieee80211com *, u_int64_t);
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
	if (have_igtk && !ieee80211_pae_mfp_igtk_shape_valid(igtk_key))
		return EINVAL;
	lock = ic->ic_pae_selected_bss_lock;
	if (lock == NULL)
		return EOPNOTSUPP;

	irq = IOSimpleLockLockDisableInterrupt(lock);
	submit = ic->ic_pae_mfp_txn_submit;
	cancel = ic->ic_pae_mfp_txn_cancel;
	finish = ic->ic_pae_mfp_txn_finish;
	if (submit == NULL || cancel == NULL || finish == NULL) {
		IOSimpleLockUnlockEnableInterrupt(lock, irq);
		return EOPNOTSUPP;
	}
	epoch = __atomic_load_n(&ic->ic_pae_assoc_epoch, __ATOMIC_ACQUIRE);
	if (epoch == 0 || ic->ic_bss != ni) {
		IOSimpleLockUnlockEnableInterrupt(lock, irq);
		return ECANCELED;
	}
	if (ic->ic_pae_mfp_txn.active) {
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
	if (error != 0) {
		if (!ieee80211_pae_mfp_txn_abort_exact(ic, id, epoch, ni))
			return ECANCELED;
		/* EBUSY is reserved for an already-live generic record above.  A
		 * backend that rejected this newly-created record did not accept an
		 * owner, so make the current-BSS failure explicit instead of letting
		 * ingress mistake it for an accepted asynchronous handoff. */
		if (error == EBUSY)
			return EIO;
	}
	return error;
}

static void
ieee80211_pae_mfp_txn_dispose_prepared(struct ieee80211com *ic,
	struct ieee80211_pae_mfp_prepared *prepared)
{
	if (prepared == NULL)
		return;
	/* This is a local, never-published BIP context.  It has no readers and
	 * must not go through the live-slot retirement path. */
	if (prepared->bip_installed)
		ieee80211_delete_key(ic, NULL, &prepared->bip_key);
	explicit_bzero(prepared, sizeof(*prepared));
}

/* Prepare only the fallible reply side of the transaction.  In particular,
 * BIP stays in `prepared` and the temporary MIC view is restored before this
 * returns: no PTK/GTK/IGTK/protection/port state is visible until the driver
 * has explicitly accepted the generic-to-backend finish handoff. */
static int
ieee80211_pae_mfp_txn_prepare_reply(struct ieee80211com *ic,
	const struct ieee80211_pae_mfp_txn *txn,
	struct ieee80211_pae_mfp_prepared *prepared)
{
	struct ieee80211_node *ni = txn->ni;
	struct ieee80211_pae_mfp_mic_state old_mic;
	int state_staged = 0;
	int error = 0;

	if (prepared == NULL)
		return EINVAL;
	bzero(prepared, sizeof(*prepared));
	bzero(&old_mic, sizeof(old_mic));
	if (!ieee80211_pae_mfp_txn_live(ic, txn->id, txn->assoc_epoch, ni))
		return ECANCELED;

	/* Firmware has ACKed data keys. BIP remains local software state until
	 * both the reply and backend ownership handoff have succeeded. */
	if (txn->have_igtk) {
		prepared->bip_key = txn->igtk_key;
		prepared->bip_key.k_priv = NULL;
		if (ieee80211_set_key(ic, ni, &prepared->bip_key) != 0) {
			explicit_bzero(prepared, sizeof(*prepared));
			return EIO;
		}
		prepared->bip_installed = 1;
		if (!ieee80211_pae_mfp_txn_live(ic, txn->id,
		    txn->assoc_epoch, ni)) {
			error = ECANCELED;
			goto rollback;
		}
	}
	if (!ieee80211_pae_mfp_txn_live(ic, txn->id, txn->assoc_epoch, ni)) {
		error = ECANCELED;
		goto rollback;
	}

	/* Message 4 / group message 2 needs a temporary PTK/replay MIC view. */
	if (!ieee80211_pae_mfp_txn_stage_mic_state(ic, txn, &old_mic)) {
		error = ECANCELED;
		goto rollback;
	}
	state_staged = 1;
	if (!ieee80211_pae_mfp_txn_live(ic, txn->id, txn->assoc_epoch, ni))
		goto rollback_cancelled;
	if (txn->reply == IEEE80211_PAE_MFP_REPLY_4WAY_MSG4) {
		if (ieee80211_send_4way_msg4(ic, ni) != 0)
			goto rollback_error;
	} else if (ieee80211_send_group_msg2(ic, ni, NULL) != 0)
		goto rollback_error;
	if (!ieee80211_pae_mfp_txn_live(ic, txn->id, txn->assoc_epoch, ni))
		goto rollback_cancelled;

	/* The reply is on the wire; remove its temporary node mutation before the
	 * backend handoff so a reject leaves no software key/protection state. */
	ieee80211_pae_mfp_txn_restore_mic_state(ic, txn, &old_mic);
	state_staged = 0;
	explicit_bzero(&old_mic, sizeof(old_mic));
	return 0;

rollback_error:
	error = EIO;
	goto rollback;
rollback_cancelled:
	error = ECANCELED;
rollback:
	if (state_staged)
		ieee80211_pae_mfp_txn_restore_mic_state(ic, txn, &old_mic);
	ieee80211_pae_mfp_txn_dispose_prepared(ic, prepared);
	explicit_bzero(&old_mic, sizeof(old_mic));
	return error;
}

void
ieee80211_pae_mfp_txn_complete(struct ieee80211com *ic, u_int64_t id,
	u_int8_t stage, int error)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_pae_mfp_txn *txn;
	struct ieee80211_pae_mfp_txn snapshot;
	struct ieee80211_pae_mfp_prepared prepared;
	struct ieee80211_pae_mfp_prepared cancelled_prepared;
	const struct ieee80211_key *key;
	struct ieee80211_key key_copy;
	struct ieee80211_node *ni;
	int (*submit)(struct ieee80211com *, u_int64_t, u_int64_t,
	    struct ieee80211_node *, const struct ieee80211_key *, u_int8_t);
	int (*finish)(struct ieee80211com *, u_int64_t) = NULL;
	void (*cancel)(struct ieee80211com *, u_int64_t) = NULL;
	u_int64_t epoch, cancel_id;
	u_int8_t next;
	int final_commit = 0, finish_error = 0;
	int port_became_valid = 0, published = 0;

	bzero(&prepared, sizeof(prepared));
	bzero(&cancelled_prepared, sizeof(cancelled_prepared));

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
		(void)ieee80211_pae_mfp_txn_cancel_locked(ic,
		    &cancelled_prepared);
		IOSimpleLockUnlockEnableInterrupt(lock, irq);
		ieee80211_pae_mfp_txn_dispose_prepared(ic, &cancelled_prepared);
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

	final_commit = ieee80211_pae_mfp_txn_prepare_reply(ic, &snapshot,
	    &prepared);
	irq = IOSimpleLockLockDisableInterrupt(lock);
	txn = &ic->ic_pae_mfp_txn;
	if (!ieee80211_pae_mfp_txn_live_locked(ic, txn, id, epoch,
	    snapshot.ni)) {
		IOSimpleLockUnlockEnableInterrupt(lock, irq);
		ieee80211_pae_mfp_txn_dispose_prepared(ic, &prepared);
		explicit_bzero(&snapshot, sizeof(snapshot));
		return;
	}
	if (final_commit == 0 && prepared.bip_installed) {
		/* Move the unpublished context into the generic value record while
		 * the epoch fence is held.  A competing cancellation will extract it
		 * and free it only after dropping this lock. */
		if (txn->prepared_bip_installed) {
			final_commit = EIO;
		} else {
			txn->prepared_bip_key = prepared.bip_key;
			txn->prepared_bip_installed = 1;
			bzero(&prepared.bip_key, sizeof(prepared.bip_key));
			prepared.bip_installed = 0;
		}
	}
	if (final_commit == 0) {
		/*
		 * The backend takes selected-BSS first, then its owner lock, and calls
		 * finish_publish_locked() before it accepts the handoff.  Publication,
		 * backend ownership transfer, and an epoch replacement are therefore
		 * one atomic choice; a nonzero result retains generic cancellation.
		 */
		finish = ic->ic_pae_mfp_txn_finish;
		IOSimpleLockUnlockEnableInterrupt(lock, irq);
		if (finish != NULL)
			finish_error = (*finish)(ic, id);
		else
			finish_error = EOPNOTSUPP;
		/* finish_publish_locked() may have retired the destination slot's old
		 * context.  Queue its out-of-lock reap before re-entering the PAE
		 * leaf lock; reader exits themselves never free/reap. */
		if (finish_error == 0) {
			ieee80211_bip_reap_schedule(ic);
			(void)ieee80211_ccmp_reap(ic);
		}

		irq = IOSimpleLockLockDisableInterrupt(lock);
		txn = &ic->ic_pae_mfp_txn;
		cancel_id = 0;
		if (ieee80211_pae_mfp_txn_live_locked(ic, txn, id, epoch,
		    snapshot.ni)) {
			if (finish_error == 0 && txn->finish_published) {
				published = 1;
				port_became_valid = txn->finish_port_became_valid;
			} else {
				/* A zero callback result without atomic publication would be
				 * an ownership hole; fail closed while local BIP is still in
				 * the value record. */
				if (finish_error == 0)
					finish_error = EIO;
				cancel = ic->ic_pae_mfp_txn_cancel;
			}
			cancel_id = ieee80211_pae_mfp_txn_cancel_locked(ic,
			    &cancelled_prepared);
		}
		IOSimpleLockUnlockEnableInterrupt(lock, irq);
		ieee80211_pae_mfp_txn_dispose_prepared(ic, &cancelled_prepared);
		if (finish_error != 0 && cancel_id != 0 && cancel != NULL)
			(*cancel)(ic, cancel_id);
		if (finish_error != 0 && finish_error != ECANCELED &&
		    cancel_id != 0 && ieee80211_pae_mfp_txn_terminal_current(ic,
		    snapshot.ni, snapshot.assoc_epoch)) {
			IEEE80211_SEND_MGMT(ic, snapshot.ni,
			    IEEE80211_FC0_SUBTYPE_DEAUTH, IEEE80211_REASON_AUTH_LEAVE);
			ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
		}
		if (finish_error == 0 && published && port_became_valid &&
		    ieee80211_pae_mfp_txn_current(ic, snapshot.ni,
		    snapshot.assoc_epoch)) {
			ieee80211_public_initial_bssid_pin_port_valid(ic,
			    snapshot.ni);
			AirportItlwmPostPltiTraceCompleteEpisode(ic);
			ieee80211_set_link_state(ic, LINK_STATE_UP);
			if (ic->ic_event_handler != NULL)
				(*ic->ic_event_handler)(ic,
				    IEEE80211_EVT_STA_RSN_HANDSHAKE_DONE, NULL);
		}
	} else {
		cancel = ic->ic_pae_mfp_txn_cancel;
		(void)ieee80211_pae_mfp_txn_cancel_locked(ic,
		    &cancelled_prepared);
		IOSimpleLockUnlockEnableInterrupt(lock, irq);
		ieee80211_pae_mfp_txn_dispose_prepared(ic, &cancelled_prepared);
		ieee80211_pae_mfp_txn_dispose_prepared(ic, &prepared);
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

/* Caller holds ic_pae_selected_bss_lock whenever that lock exists. */
static void
ieee80211_sae_peer_rx_admission_clear_locked(struct ieee80211com *ic)
{
	if (ic == NULL)
		return;
	explicit_bzero(&ic->ic_sae_peer_rx_admission,
	    sizeof(ic->ic_sae_peer_rx_admission));
}

/* Caller holds ic_pae_selected_bss_lock whenever that lock exists. */
static void
ieee80211_public_initial_bssid_pin_clear_locked(struct ieee80211com *ic)
{
	if (ic == NULL)
		return;
	explicit_bzero(&ic->ic_public_initial_bssid_pin,
	    sizeof(ic->ic_public_initial_bssid_pin));
}

/* Caller holds ic_pae_selected_bss_lock whenever that lock exists. */
static void
ieee80211_wnm_bss_transition_clear_locked(struct ieee80211com *ic)
{
	if (ic == NULL)
		return;
	explicit_bzero(&ic->ic_wnm_bss_transition,
	    sizeof(ic->ic_wnm_bss_transition));
}

static int
ieee80211_bssid_is_unicast_nonzero(
    const u_int8_t bssid[IEEE80211_ADDR_LEN])
{
	size_t index;
	int nonzero = 0;

	if (bssid == NULL || (bssid[0] & 0x01) != 0)
		return 0;
	for (index = 0; index < IEEE80211_ADDR_LEN; index++) {
		if (bssid[index] != 0) {
			nonzero = 1;
			break;
		}
	}
	return nonzero;
}

static int
ieee80211_sae_wcl_request_bssid_is_unicast_nonzero(
    const u_int8_t bssid[IEEE80211_ADDR_LEN])
{
	return ieee80211_bssid_is_unicast_nonzero(bssid);
}

/*
 * The public IOC_ASSOCIATE carrier can name a BSS selected by CoreWLAN, but
 * does not express a durable user BSSID lock.  Record that provenance only
 * after the caller has fully rebuilt its local association policy.  The
 * marker carries no SSID, RSN IE, key, or node reference.
 */
void
ieee80211_public_initial_bssid_pin_arm(struct ieee80211com *ic,
    const u_int8_t bssid[IEEE80211_ADDR_LEN])
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	u_int64_t epoch;

	if (ic == NULL || bssid == NULL || ic->ic_opmode != IEEE80211_M_STA ||
	    !ieee80211_bssid_is_unicast_nonzero(bssid) ||
	    (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	epoch = __atomic_load_n(&ic->ic_pae_assoc_epoch, __ATOMIC_ACQUIRE);
	if (ic->ic_opmode == IEEE80211_M_STA && epoch != 0 &&
	    (ic->ic_flags & IEEE80211_F_DESBSSID) != 0 &&
	    IEEE80211_ADDR_EQ(ic->ic_des_bssid, bssid)) {
		ieee80211_public_initial_bssid_pin_clear_locked(ic);
		ic->ic_public_initial_bssid_pin.configuration_epoch = epoch;
		IEEE80211_ADDR_COPY(ic->ic_public_initial_bssid_pin.bssid, bssid);
		ic->ic_public_initial_bssid_pin.active = 1;
	} else {
		ieee80211_public_initial_bssid_pin_clear_locked(ic);
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
}

void
ieee80211_public_initial_bssid_pin_disarm(struct ieee80211com *ic)
{
	IOSimpleLock *lock;
	IOInterruptState irq;

	if (ic == NULL || (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	ieee80211_public_initial_bssid_pin_clear_locked(ic);
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
}

int
ieee80211_wnm_bss_transition_arm(struct ieee80211com *ic,
    const u_int8_t source_bssid[IEEE80211_ADDR_LEN],
    const u_int8_t target_bssid[IEEE80211_ADDR_LEN],
    const u_int8_t *ssid, u_int8_t ssid_len, u_int8_t dialog_token,
    u_int8_t target_channel)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_wnm_bss_transition *transition;
	int armed = 0;

	if (ic == NULL || source_bssid == NULL || target_bssid == NULL ||
	    ssid == NULL || ssid_len == 0 || ssid_len > IEEE80211_NWID_LEN ||
	    ic->ic_opmode != IEEE80211_M_STA ||
	    !ieee80211_bssid_is_unicast_nonzero(source_bssid) ||
	    !ieee80211_bssid_is_unicast_nonzero(target_bssid) ||
	    IEEE80211_ADDR_EQ(source_bssid, target_bssid) ||
	    (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return 0;

	irq = IOSimpleLockLockDisableInterrupt(lock);
	if (ic->ic_state == IEEE80211_S_RUN && ic->ic_bss != NULL &&
	    IEEE80211_ADDR_EQ(ic->ic_bss->ni_bssid, source_bssid) &&
	    ic->ic_bss->ni_esslen == ssid_len &&
	    memcmp(ic->ic_bss->ni_essid, ssid, ssid_len) == 0) {
		transition = &ic->ic_wnm_bss_transition;
		ieee80211_wnm_bss_transition_clear_locked(ic);
		IEEE80211_ADDR_COPY(transition->source_bssid, source_bssid);
		IEEE80211_ADDR_COPY(transition->target_bssid, target_bssid);
		memcpy(transition->ssid, ssid, ssid_len);
		transition->ssid_len = ssid_len;
		transition->dialog_token = dialog_token;
		transition->target_channel = target_channel;
		transition->active = 1;
		armed = 1;
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return armed;
}

void
ieee80211_wnm_bss_transition_clear(struct ieee80211com *ic)
{
	IOSimpleLock *lock;
	IOInterruptState irq;

	if (ic == NULL || (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	ieee80211_wnm_bss_transition_clear_locked(ic);
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
}

/*
 * A BTM request can arrive while a periodic or WCL background census owns
 * the firmware scan command.  Mark that completion as ineligible to satisfy
 * this request; its terminal path will schedule one fresh driver-owned scan.
 */
int
ieee80211_wnm_bss_transition_defer_fresh_scan(struct ieee80211com *ic)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_wnm_bss_transition *transition;
	int deferred = 0;

	if (ic == NULL || (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return 0;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	transition = &ic->ic_wnm_bss_transition;
	if (transition->active != 0 &&
	    transition->candidate_confirmed == 0) {
		transition->fresh_scan_pending = 1;
		transition->scan_retry_count = 0;
		deferred = 1;
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return deferred;
}

int
ieee80211_wnm_bss_transition_fresh_scan_pending(struct ieee80211com *ic)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	const struct ieee80211_wnm_bss_transition *transition;
	int pending = 0;

	if (ic == NULL || (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return 0;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	transition = &ic->ic_wnm_bss_transition;
	pending = transition->active != 0 &&
	    transition->candidate_confirmed == 0 &&
	    transition->fresh_scan_pending != 0;
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return pending;
}

int
ieee80211_wnm_bss_transition_retry_fresh_scan(struct ieee80211com *ic)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_wnm_bss_transition *transition;
	int retry = 0;

	if (ic == NULL || (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return 0;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	transition = &ic->ic_wnm_bss_transition;
	if (transition->active != 0 &&
	    transition->candidate_confirmed == 0 &&
	    transition->fresh_scan_pending != 0 &&
	    transition->scan_retry_count < 50) {
		transition->scan_retry_count++;
		retry = 1;
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return retry;
}

void
ieee80211_wnm_bss_transition_fresh_scan_started(struct ieee80211com *ic)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_wnm_bss_transition *transition;

	if (ic == NULL || (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	transition = &ic->ic_wnm_bss_transition;
	if (transition->active != 0) {
		transition->fresh_scan_pending = 0;
		transition->scan_retry_count = 0;
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
}

int
ieee80211_wnm_bss_transition_candidate_disposition(
    struct ieee80211com *ic, const struct ieee80211_node *ni)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	const struct ieee80211_wnm_bss_transition *transition;
	int disposition = 0;

	if (ic == NULL || ni == NULL ||
	    (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return 0;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	transition = &ic->ic_wnm_bss_transition;
	if (transition->active != 0) {
		disposition =
		    IEEE80211_ADDR_EQ(transition->target_bssid, ni->ni_bssid) &&
		    transition->ssid_len == ni->ni_esslen &&
		    memcmp(transition->ssid, ni->ni_essid,
		    transition->ssid_len) == 0 &&
		    (transition->target_channel == 0 ||
		    transition->target_channel ==
		    ieee80211_chan2ieee(ic, ni->ni_chan)) ? 1 : -1;
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return disposition;
}

int
ieee80211_wnm_bss_transition_active(struct ieee80211com *ic,
    u_int8_t *dialog_token)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	const struct ieee80211_wnm_bss_transition *transition;
	int active = 0;

	if (dialog_token != NULL)
		*dialog_token = 0;
	if (ic == NULL || (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return 0;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	transition = &ic->ic_wnm_bss_transition;
	if (transition->active != 0) {
		active = 1;
		if (dialog_token != NULL)
			*dialog_token = transition->dialog_token;
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return active;
}

int
ieee80211_wnm_bss_transition_confirm_candidate(
    struct ieee80211com *ic, const struct ieee80211_node *ni,
    u_int8_t *dialog_token, u_int8_t target_bssid[IEEE80211_ADDR_LEN])
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_wnm_bss_transition *transition;
	int confirmed = 0;

	if (dialog_token != NULL)
		*dialog_token = 0;
	if (target_bssid != NULL)
		explicit_bzero(target_bssid, IEEE80211_ADDR_LEN);
	if (ic == NULL || ni == NULL || dialog_token == NULL ||
	    target_bssid == NULL ||
	    (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return 0;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	transition = &ic->ic_wnm_bss_transition;
	if (transition->active != 0 &&
	    IEEE80211_ADDR_EQ(transition->target_bssid, ni->ni_bssid) &&
	    transition->ssid_len == ni->ni_esslen &&
	    memcmp(transition->ssid, ni->ni_essid, transition->ssid_len) == 0 &&
	    (transition->target_channel == 0 ||
	    transition->target_channel == ieee80211_chan2ieee(ic, ni->ni_chan))) {
		transition->candidate_confirmed = 1;
		*dialog_token = transition->dialog_token;
		IEEE80211_ADDR_COPY(target_bssid, transition->target_bssid);
		confirmed = 1;
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return confirmed;
}

int
ieee80211_wnm_bss_transition_copy_retarget(struct ieee80211com *ic,
    const u_int8_t *ssid, u_int8_t ssid_len,
    u_int8_t target_bssid[IEEE80211_ADDR_LEN])
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_wnm_bss_transition *transition;
	int retarget = 0;

	if (target_bssid != NULL)
		explicit_bzero(target_bssid, IEEE80211_ADDR_LEN);
	if (ic == NULL || ssid == NULL || ssid_len == 0 ||
	    ssid_len > IEEE80211_NWID_LEN || target_bssid == NULL ||
	    (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return 0;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	transition = &ic->ic_wnm_bss_transition;
	if (transition->active != 0 && transition->candidate_confirmed != 0 &&
	    transition->ssid_len == ssid_len &&
	    memcmp(transition->ssid, ssid, ssid_len) == 0) {
		IEEE80211_ADDR_COPY(target_bssid, transition->target_bssid);
		retarget = 1;
	} else if (transition->active != 0 &&
	    transition->candidate_confirmed != 0) {
		/* A different explicit join supersedes the completed transition. */
		ieee80211_wnm_bss_transition_clear_locked(ic);
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return retarget;
}

void
ieee80211_wnm_bss_transition_consume(struct ieee80211com *ic,
    const u_int8_t *ssid, u_int8_t ssid_len,
    const u_int8_t target_bssid[IEEE80211_ADDR_LEN])
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_wnm_bss_transition *transition;

	if (ic == NULL || ssid == NULL || target_bssid == NULL ||
	    (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	transition = &ic->ic_wnm_bss_transition;
	if (transition->active != 0 && transition->candidate_confirmed != 0 &&
	    transition->ssid_len == ssid_len &&
	    memcmp(transition->ssid, ssid, ssid_len) == 0 &&
	    IEEE80211_ADDR_EQ(transition->target_bssid, target_bssid))
		ieee80211_wnm_bss_transition_clear_locked(ic);
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
}

/* Caller holds ic_pae_selected_bss_lock. */
static void
ieee80211_public_initial_bssid_pin_bind_selected_bss_locked(
    struct ieee80211com *ic, const struct ieee80211_node *ni,
    u_int64_t expected_epoch)
{
	struct ieee80211_public_initial_bssid_pin *pin;

	if (ic == NULL)
		return;
	pin = &ic->ic_public_initial_bssid_pin;
	if (pin->active == 0)
		return;
	if (ni == NULL || expected_epoch == 0 ||
	    pin->binding_pending == 0 || pin->association_epoch != 0 ||
	    __atomic_load_n(&ic->ic_pae_assoc_epoch, __ATOMIC_ACQUIRE) !=
		expected_epoch ||
	    __atomic_load_n(&ic->ic_pae_selected_bss.epoch,
		__ATOMIC_ACQUIRE) != expected_epoch ||
	    (ic->ic_flags & IEEE80211_F_DESBSSID) == 0 ||
	    !IEEE80211_ADDR_EQ(pin->bssid, ic->ic_des_bssid) ||
	    !IEEE80211_ADDR_EQ(pin->bssid, ni->ni_bssid) ||
	    !IEEE80211_ADDR_EQ(ic->ic_pae_selected_bss.bssid, ni->ni_bssid)) {
		ieee80211_public_initial_bssid_pin_clear_locked(ic);
		return;
	}
	pin->association_epoch = expected_epoch;
	pin->binding_pending = 0;
}

/*
 * Retire only an exact public initial-BSS pin after its first successful RSN
 * port-valid publication.  This is deliberately later than AUTH/ASSOC and
 * earlier than the driver event callback: a failed initial 4-way handshake
 * keeps the historical BSSID restriction, while a live RUN association may
 * recover to another compatible BSS of the same ESS after a later loss.
 */
void
ieee80211_public_initial_bssid_pin_port_valid(struct ieee80211com *ic,
    struct ieee80211_node *ni)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_public_initial_bssid_pin *pin;
	u_int64_t epoch;

	if (ic == NULL || ni == NULL ||
	    (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	pin = &ic->ic_public_initial_bssid_pin;
	epoch = __atomic_load_n(&ic->ic_pae_assoc_epoch, __ATOMIC_ACQUIRE);
	if (pin->active != 0 && pin->association_epoch != 0 &&
	    pin->association_epoch == epoch && ni == ic->ic_bss &&
	    ni->ni_port_valid != 0) {
		if (ic->ic_opmode == IEEE80211_M_STA &&
		    ic->ic_state == IEEE80211_S_RUN &&
		    __atomic_load_n(&ic->ic_pae_assoc_replace_epoch,
		    __ATOMIC_ACQUIRE) == 0 &&
		    __atomic_load_n(&ic->ic_pae_selected_bss.epoch,
		    __ATOMIC_ACQUIRE) == epoch &&
		    (ic->ic_flags & IEEE80211_F_DESBSSID) != 0 &&
		    IEEE80211_ADDR_EQ(pin->bssid, ic->ic_des_bssid) &&
		    IEEE80211_ADDR_EQ(pin->bssid, ni->ni_bssid) &&
		    IEEE80211_ADDR_EQ(ic->ic_pae_selected_bss.bssid,
		    ni->ni_bssid)) {
			ic->ic_flags &= ~IEEE80211_F_DESBSSID;
			explicit_bzero(ic->ic_des_bssid, sizeof(ic->ic_des_bssid));
		}
		/* A port-valid edge cannot leave a stale initial-public marker. */
		ieee80211_public_initial_bssid_pin_clear_locked(ic);
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
}

/*
 * Tahoe's generic S_RUN handler historically publishes LINK_UP under
 * USE_APPLE_SUPPLICANT before a local RSN PAE has opened its port.  Keep that
 * historical timing for every non-public path, but make the exact public
 * initial-BSS owner wait for its existing port-valid release helper below.
 * This only observes fixed identity/epoch state; it never owns a node, key,
 * credential, or callback.
 */
int
ieee80211_public_initial_bssid_pin_should_defer_link_up(
    struct ieee80211com *ic, struct ieee80211_node *ni)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_public_initial_bssid_pin *pin;
	u_int64_t epoch;
	int defer = 0;

	if (ic == NULL || ni == NULL ||
	    (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return 0;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	pin = &ic->ic_public_initial_bssid_pin;
	epoch = __atomic_load_n(&ic->ic_pae_assoc_epoch, __ATOMIC_ACQUIRE);
	if (pin->active != 0 && pin->association_epoch != 0 &&
	    pin->association_epoch == epoch && ic->ic_opmode == IEEE80211_M_STA &&
	    ic->ic_state == IEEE80211_S_RUN && ni == ic->ic_bss &&
	    ni->ni_port_valid == 0 &&
	    __atomic_load_n(&ic->ic_pae_selected_bss.epoch,
	    __ATOMIC_ACQUIRE) == epoch &&
	    (ic->ic_flags & IEEE80211_F_DESBSSID) != 0 &&
	    IEEE80211_ADDR_EQ(pin->bssid, ic->ic_des_bssid) &&
	    IEEE80211_ADDR_EQ(pin->bssid, ni->ni_bssid) &&
	    IEEE80211_ADDR_EQ(pin->bssid, ic->ic_pae_selected_bss.bssid))
		defer = 1;
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return defer;
}

static int
ieee80211_sae_wcl_request_phase_is_active(u_int8_t phase)
{
	return phase == IEEE80211_SAE_WCL_REQUEST_PENDING ||
	    phase == IEEE80211_SAE_WCL_REQUEST_SCAN_STARTING ||
	    phase == IEEE80211_SAE_WCL_REQUEST_SCAN_ISSUED ||
	    phase == IEEE80211_SAE_WCL_REQUEST_BOUND;
}

/*
 * Hook publication shares ic_pae_selected_bss_lock with the exact WCL/BSS
 * owner.  Take the callback values under that same leaf, then invoke only
 * the local values after unlocking: IWN's callback lease owns the remaining
 * close/drain lifetime.  In particular, never turn a NULL check plus a
 * second direct field load into a stop/detach NULL-call race.
 */
void
ieee80211_sae_driver_hook_snapshot_copyout(struct ieee80211com *ic,
    struct ieee80211_sae_driver_hook_snapshot *out)
{
	IOSimpleLock *lock;
	IOInterruptState irq;

	if (out == NULL)
		return;
	explicit_bzero(out, sizeof(*out));
	if (ic == NULL || (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	out->auth_hold = ic->ic_sae_auth_hold;
	out->auth_owned = ic->ic_sae_auth_owned;
	out->engine_peer_event = ic->ic_sae_engine_peer_event;
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
}

/*
 * A leaf-lock clear may revoke a separately-owned driver credential or SAE
 * engine.  The generic record has no private material, so it copies only the
 * driver's nonblocking callback and the public generation.  Delivery is
 * always deferred until after the selected-BSS leaf lock is dropped.
 */
struct ieee80211_sae_wcl_request_revocation {
	void		(*callback)(struct ieee80211com *, u_int64_t);
	u_int64_t	generation;
};

static void
ieee80211_sae_wcl_request_revocation_deliver(struct ieee80211com *ic,
    struct ieee80211_sae_wcl_request_revocation *revocation)
{
	void (*callback)(struct ieee80211com *, u_int64_t);
	u_int64_t generation;

	if (revocation == NULL)
		return;
	callback = revocation->callback;
	generation = revocation->generation;
	explicit_bzero(revocation, sizeof(*revocation));
	if (ic != NULL && generation != 0 && callback != NULL)
		(*callback)(ic, generation);
}

/* Caller holds ic_pae_selected_bss_lock whenever that lock exists.  This is
 * the only direct-WCL policy owner: it has already invalidated the preceding
 * RSN/PMK attempt before publication, so clearing it cannot restore a
 * credential or a broader AKM.  Keep the teardown value-only and bounded so
 * ordinary epoch cancellation may perform it under the selected-BSS leaf. */
static void
ieee80211_sae_wcl_request_policy_clear_locked(struct ieee80211com *ic)
{
	struct ieee80211_sae_wcl_request *request;
	struct ieee80211_node *ni;

	if (ic == NULL)
		return;
	request = &ic->ic_sae_wcl_request;
	ni = ic->ic_bss;
	/* A completion may have copied the derived PMK into the ordinary local
	 * PAE store so its first M1 path can remain untouched.  It belongs only to
	 * this exact direct request.  Tear it down before the public request value
	 * disappears; a later association must never inherit either a PMK or its
	 * SAE PMKID. */
	if (ic->ic_sae_wcl_pmk_claim.active != 0 && ni != NULL &&
	    IEEE80211_ADDR_EQ(ni->ni_bssid,
	    ic->ic_sae_wcl_pmk_claim.bssid) &&
	    IEEE80211_ADDR_EQ(ic->ic_myaddr,
	    ic->ic_sae_wcl_pmk_claim.sta) &&
	    request->ssid_len == ni->ni_esslen &&
	    memcmp(request->ssid, ni->ni_essid, request->ssid_len) == 0) {
		explicit_bzero(ni->ni_pmk, sizeof(ni->ni_pmk));
		explicit_bzero(ni->ni_pmkid, sizeof(ni->ni_pmkid));
		ni->ni_flags &= ~(IEEE80211_NODE_PMK | IEEE80211_NODE_PMKID);
	}
	explicit_bzero(&ic->ic_sae_wcl_pmk_claim,
	    sizeof(ic->ic_sae_wcl_pmk_claim));
	ic->ic_sae_wcl_policy_generation = 0;
	ic->ic_pae_mfp_requested = 0;
	/* This direct/WCL policy owns a different explicit BSSID lifetime.  It
	 * must never inherit the transient public-CoreWLAN provenance marker. */
	ieee80211_public_initial_bssid_pin_clear_locked(ic);
	ic->ic_flags &= ~(IEEE80211_F_PSK | IEEE80211_F_RSNON |
	    IEEE80211_F_MFPR | IEEE80211_F_DESBSSID);
	explicit_bzero(ic->ic_psk, sizeof(ic->ic_psk));
	ic->ic_external_pmk_owner = 0;
	ic->ic_rsnprotos = 0;
	ic->ic_rsnakms = 0;
	ic->ic_rsnciphers = 0;
	ic->ic_rsngroupcipher = (enum ieee80211_cipher)0;
	ic->ic_rsngroupmgmtcipher = (enum ieee80211_cipher)0;
	ic->ic_des_esslen = 0;
	explicit_bzero(ic->ic_des_essid, sizeof(ic->ic_des_essid));
	explicit_bzero(ic->ic_des_bssid, sizeof(ic->ic_des_bssid));
#ifdef USE_APPLE_SUPPLICANT
	explicit_bzero(ic->ic_rsn_ie_override,
	    sizeof(ic->ic_rsn_ie_override));
#endif
}

/* Caller holds ic_pae_selected_bss_lock whenever that lock exists. */
static void
ieee80211_sae_wcl_request_clear_locked(struct ieee80211com *ic,
    struct ieee80211_sae_wcl_request_revocation *revocation)
{
	u_int64_t generation;

	if (ic == NULL)
		return;
	generation = ic->ic_sae_wcl_request.generation;
	if (generation != 0 && revocation != NULL &&
	    revocation->generation == 0) {
		revocation->callback = ic->ic_sae_wcl_request_revoke;
		revocation->generation = generation;
	}
	if (generation != 0 &&
	    ic->ic_sae_wcl_policy_generation == generation)
		ieee80211_sae_wcl_request_policy_clear_locked(ic);
	explicit_bzero(&ic->ic_sae_wcl_request,
	    sizeof(ic->ic_sae_wcl_request));
}

/* Caller holds ic_pae_selected_bss_lock whenever that lock exists. */
static int
ieee80211_sae_wcl_request_owner_hooks_ready_locked(
    const struct ieee80211com *ic)
{
	/* A direct request must own TX initiation, late Open suppression, RX, and
	 * private-state revocation as one unit.  Partial hook registration must
	 * never turn a selected SAE BSS into the historical Open-System path. */
	return ic != NULL && ic->ic_sae_auth_hold != NULL &&
	    ic->ic_sae_auth_owned != NULL &&
	    ic->ic_sae_engine_peer_event != NULL &&
	    ic->ic_sae_wcl_request_revoke != NULL;
}

/* Caller holds ic_pae_selected_bss_lock whenever that lock exists. */
static int
ieee80211_sae_wcl_request_identity_is_valid_locked(
    const struct ieee80211_sae_wcl_request *request)
{
	return request != NULL && request->generation != 0 &&
	    request->ssid_len != 0 && request->ssid_len <= IEEE80211_NWID_LEN &&
	    ieee80211_sae_wcl_request_bssid_is_unicast_nonzero(request->bssid);
}

/* Caller holds ic_pae_selected_bss_lock whenever that lock exists. */
static int
ieee80211_sae_wcl_request_scan_issued_locked(struct ieee80211com *ic,
    u_int64_t generation)
{
	const struct ieee80211_sae_wcl_request *request;

	if (ic == NULL || generation == 0)
		return 0;
	request = &ic->ic_sae_wcl_request;
	return request->phase == IEEE80211_SAE_WCL_REQUEST_SCAN_ISSUED &&
	    request->generation == generation &&
	    request->association_epoch == 0 &&
	    ieee80211_sae_wcl_request_identity_is_valid_locked(request);
}

/* Caller holds ic_pae_selected_bss_lock.  STARTING is deliberately not
 * selection-owned: it is the narrow interval between WCL's public request
 * and IWN's confirmed new scan submission. */
static int
ieee80211_sae_wcl_request_scan_starting_locked(struct ieee80211com *ic,
    u_int64_t generation)
{
	const struct ieee80211_sae_wcl_request *request;

	if (ic == NULL || generation == 0)
		return 0;
	request = &ic->ic_sae_wcl_request;
	return request->phase == IEEE80211_SAE_WCL_REQUEST_SCAN_STARTING &&
	    request->generation == generation &&
	    request->association_epoch == 0 &&
	    ieee80211_sae_wcl_request_identity_is_valid_locked(request);
}

/* Caller holds ic_pae_selected_bss_lock.  node_join_bss() sets its explicit
 * fence before the BSS copy and leaves it active through the synchronous
 * driver's S_AUTH request.  The selected-BSS check is a defensive second
 * fence for the S_SCAN tail if a caller ever reaches it without the marker. */
static int
ieee80211_sae_wcl_request_join_active_locked(struct ieee80211com *ic)
{
	return ic != NULL && (ic->ic_sae_wcl_request_join_active != 0 ||
	    (ic->ic_state == IEEE80211_S_SCAN &&
	    __atomic_load_n(&ic->ic_pae_selected_bss.epoch,
	    __ATOMIC_ACQUIRE) != 0));
}

/* Caller holds ic_pae_selected_bss_lock.  A direct reconnect may originate
 * from a stable RUN association, but not in the short macro window after an
 * ordinary RUN -> AUTH transition invalidated its selected-BSS epoch and
 * before the driver receives that transition.  This is a publication fence,
 * not an SAE admission predicate. */
static int
ieee80211_sae_wcl_request_run_is_stable_locked(struct ieee80211com *ic)
{
	u_int64_t epoch;

	if (ic == NULL || ic->ic_state != IEEE80211_S_RUN)
		return 0;
	epoch = __atomic_load_n(&ic->ic_pae_assoc_epoch, __ATOMIC_ACQUIRE);
	return epoch != 0 &&
	    __atomic_load_n(&ic->ic_pae_assoc_replace_epoch,
	    __ATOMIC_ACQUIRE) == 0 &&
	    __atomic_load_n(&ic->ic_pae_selected_bss.epoch,
	    __ATOMIC_ACQUIRE) == epoch;
}

static int
ieee80211_sae_peer_rx_mac_is_unicast_nonzero(const u_int8_t *mac)
{
	size_t index;
	int nonzero = 0;

	if (mac == NULL || (mac[0] & 0x01) != 0)
		return 0;
	for (index = 0; index < IEEE80211_ADDR_LEN; index++) {
		if (mac[index] != 0) {
			nonzero = 1;
			break;
		}
	}
	return nonzero;
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
	/* A public initial-BSS hint becomes eligible only after this exact
	 * post-copy selected-BSS identity has been published for the replacement
	 * epoch.  It never binds a scan candidate or a request-side BSSID. */
	ieee80211_public_initial_bssid_pin_bind_selected_bss_locked(ic, ni,
	    expected_epoch);
out:
	/* A failed/short selected-BSS publication cannot leave a pending public
	 * hint for a later replacement to inherit. */
	if (ic->ic_public_initial_bssid_pin.active != 0 &&
	    ic->ic_public_initial_bssid_pin.binding_pending != 0)
		ieee80211_public_initial_bssid_pin_clear_locked(ic);
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
 * destruction.  The Tahoe WCL join-completion bridge is one such serialized
 * consumer: it holds the controller lifecycle claim and revalidates this
 * exact epoch before publication.  This is not association admission,
 * credential delivery, or authentication permission.
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
 * Publish one exact, selected-BSS-bound peer-RX admission.  The controller
 * owns its generation; net80211 merely preserves that value in a copied
 * public peer event after this leaf-lock validation.  No callback, allocation
 * or node retention is permitted here.  This intentionally pre-arms the
 * exact selected identity without requiring S_AUTH: the selected-BSS join
 * owner will publish immediately before ieee80211_new_state(..., S_AUTH).
 * snapshot_admission() is the sole RX gate and still requires S_AUTH, so this
 * publication cannot accept an Algorithm-3 frame before that transition.
 */
int
ieee80211_sae_peer_rx_admit(struct ieee80211com *ic, u_int64_t expected_epoch,
    u_int64_t relay_generation, const u_int8_t bssid[IEEE80211_ADDR_LEN],
    const u_int8_t sta[IEEE80211_ADDR_LEN])
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_sae_admission profile;
	int admitted = 0;

	if (ic == NULL || expected_epoch == 0 || relay_generation == 0 ||
	    bssid == NULL || sta == NULL ||
	    !ieee80211_sae_peer_rx_mac_is_unicast_nonzero(bssid) ||
	    !ieee80211_sae_peer_rx_mac_is_unicast_nonzero(sta) ||
	    ic->ic_opmode != IEEE80211_M_STA)
		return 0;
	lock = ic->ic_pae_selected_bss_lock;
	if (lock == NULL)
		return 0;
	bzero(&profile, sizeof(profile));
	irq = IOSimpleLockLockDisableInterrupt(lock);
	/* Never leave an old generation admitted after a failed replacement. */
	ieee80211_sae_peer_rx_admission_clear_locked(ic);
	if (ic->ic_opmode == IEEE80211_M_STA && ic->ic_bss != NULL &&
	    __atomic_load_n(&ic->ic_pae_assoc_epoch, __ATOMIC_ACQUIRE) ==
		expected_epoch &&
	    __atomic_load_n(&ic->ic_pae_assoc_replace_epoch,
		__ATOMIC_ACQUIRE) == 0 &&
	    __atomic_load_n(&ic->ic_pae_selected_bss.epoch,
		__ATOMIC_ACQUIRE) == expected_epoch &&
	    IEEE80211_ADDR_EQ(ic->ic_pae_selected_bss.bssid, bssid) &&
	    IEEE80211_ADDR_EQ(ic->ic_bss->ni_bssid, bssid) &&
	    IEEE80211_ADDR_EQ(ic->ic_myaddr, sta) &&
	    ieee80211_sae_admission_group19_hnp(&ic->ic_pae_selected_bss,
		&profile)) {
		ic->ic_sae_peer_rx_admission.association_epoch = expected_epoch;
		ic->ic_sae_peer_rx_admission.relay_generation = relay_generation;
		IEEE80211_ADDR_COPY(ic->ic_sae_peer_rx_admission.bssid, bssid);
		IEEE80211_ADDR_COPY(ic->ic_sae_peer_rx_admission.sta, sta);
		ic->ic_sae_peer_rx_admission.active = 1;
		admitted = 1;
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	explicit_bzero(&profile, sizeof(profile));
	return admitted;
}

void
ieee80211_sae_peer_rx_revoke(struct ieee80211com *ic,
    u_int64_t expected_epoch, u_int64_t relay_generation)
{
	IOSimpleLock *lock;
	IOInterruptState irq;

	if (ic == NULL || expected_epoch == 0 || relay_generation == 0)
		return;
	lock = ic->ic_pae_selected_bss_lock;
	if (lock == NULL)
		return;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	if (ic->ic_sae_peer_rx_admission.active != 0 &&
	    ic->ic_sae_peer_rx_admission.association_epoch == expected_epoch &&
	    ic->ic_sae_peer_rx_admission.relay_generation == relay_generation)
		ieee80211_sae_peer_rx_admission_clear_locked(ic);
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
}

/*
 * RX obtains a value snapshot only after all current-BSS/epoch/admission
 * identities agree under the same leaf lock.  It drops that lock before it
 * touches an mbuf or invokes the controller event handler.
 */
int
ieee80211_sae_peer_rx_snapshot_admission(struct ieee80211com *ic,
    const u_int8_t bssid[IEEE80211_ADDR_LEN],
    const u_int8_t sta[IEEE80211_ADDR_LEN], u_int64_t *association_epoch,
    u_int64_t *relay_generation)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	u_int64_t epoch = 0;
	u_int64_t generation = 0;
	int copied = 0;

	if (association_epoch != NULL)
		*association_epoch = 0;
	if (relay_generation != NULL)
		*relay_generation = 0;
	if (ic == NULL || bssid == NULL || sta == NULL ||
	    association_epoch == NULL || relay_generation == NULL ||
	    ic->ic_opmode != IEEE80211_M_STA)
		return 0;
	lock = ic->ic_pae_selected_bss_lock;
	if (lock == NULL)
		return 0;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	epoch = __atomic_load_n(&ic->ic_pae_assoc_epoch, __ATOMIC_ACQUIRE);
	if (epoch != 0 && ic->ic_state == IEEE80211_S_AUTH &&
	    ic->ic_sae_peer_rx_admission.active != 0 &&
	    ic->ic_sae_peer_rx_admission.association_epoch == epoch &&
	    ic->ic_sae_peer_rx_admission.relay_generation != 0 &&
	    __atomic_load_n(&ic->ic_pae_assoc_replace_epoch,
		__ATOMIC_ACQUIRE) == 0 &&
	    __atomic_load_n(&ic->ic_pae_selected_bss.epoch,
		__ATOMIC_ACQUIRE) == epoch &&
	    ic->ic_bss != NULL &&
	    IEEE80211_ADDR_EQ(ic->ic_pae_selected_bss.bssid, bssid) &&
	    IEEE80211_ADDR_EQ(ic->ic_bss->ni_bssid, bssid) &&
	    IEEE80211_ADDR_EQ(ic->ic_myaddr, sta) &&
	    IEEE80211_ADDR_EQ(ic->ic_sae_peer_rx_admission.bssid, bssid) &&
	    IEEE80211_ADDR_EQ(ic->ic_sae_peer_rx_admission.sta, sta)) {
		generation = ic->ic_sae_peer_rx_admission.relay_generation;
		copied = 1;
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	if (copied) {
		*association_epoch = epoch;
		*relay_generation = generation;
	}
	return copied;
}

/*
 * Advance a transaction fence before a new STA association owner replaces
 * node/RSN state.  The future SAE relay and PAE continuation queues may read
 * this from a different execution context, hence an atomic increment.  Zero
 * is reserved as the uninitialized/no-attempt value and is skipped on wrap.
 *
 * Only ieee80211_next_scan() may request preservation, and only while it is
 * making an intra-scan channel hop.  It may carry one exact, unbound public
 * initial-BSSID marker to the next epoch; every other asynchronous owner is
 * invalidated exactly as it is for an ordinary cancellation.
 */
static u_int64_t
ieee80211_pae_assoc_epoch_begin_internal(struct ieee80211com *ic,
    int preserve_unbound_public_initial_bssid_pin)
{
	u_int64_t epoch;
	u_int64_t prior_epoch;
	u_int64_t txn_id = 0;
	IOSimpleLock *lock;
	IOInterruptState irq;
	void (*cancel)(struct ieee80211com *, u_int64_t) = NULL;
	struct ieee80211_pae_mfp_prepared prepared;
	struct ieee80211_sae_wcl_request_revocation revocation;

	if (ic == NULL || ic->ic_opmode != IEEE80211_M_STA)
		return 0;
	bzero(&prepared, sizeof(prepared));
	bzero(&revocation, sizeof(revocation));
	lock = ic->ic_pae_selected_bss_lock;
	if (lock != NULL)
		irq = IOSimpleLockLockDisableInterrupt(lock);
	prior_epoch = __atomic_load_n(&ic->ic_pae_assoc_epoch,
	    __ATOMIC_ACQUIRE);
	epoch = ieee80211_pae_assoc_epoch_advance_locked(ic);
	__atomic_store_n(&ic->ic_pae_assoc_replace_epoch, 0,
	    __ATOMIC_RELEASE);
	ieee80211_pae_selected_bss_invalidate(ic);
	ieee80211_sae_peer_rx_admission_clear_locked(ic);
	/* An ordinary retry/reset never transfers public initial-BSS provenance
	 * into a later attempt.  The scanner's own channel hop is the sole narrow
	 * exception: it retains only an exact unbound public marker and rebases
	 * its configuration epoch.  It deliberately leaves the actual DESBSSID
	 * configuration untouched so legacy/raw recovery semantics remain intact. */
	if (preserve_unbound_public_initial_bssid_pin != 0 &&
	    ic->ic_public_initial_bssid_pin.active != 0 &&
	    ic->ic_public_initial_bssid_pin.association_epoch == 0 &&
	    ic->ic_public_initial_bssid_pin.binding_pending == 0 &&
	    ic->ic_public_initial_bssid_pin.configuration_epoch == prior_epoch &&
	    (ic->ic_flags & IEEE80211_F_DESBSSID) != 0 &&
	    ieee80211_bssid_is_unicast_nonzero(
	    ic->ic_public_initial_bssid_pin.bssid) &&
	    IEEE80211_ADDR_EQ(ic->ic_public_initial_bssid_pin.bssid,
	    ic->ic_des_bssid)) {
		ic->ic_public_initial_bssid_pin.configuration_epoch = epoch;
	} else {
		ieee80211_public_initial_bssid_pin_clear_locked(ic);
	}
	/* A real cancellation wins over the brief pre-policy reservation too.
	 * begin() rechecks this value after its out-of-lock WEP teardown before
	 * it can publish any new direct request. */
	ic->ic_sae_wcl_request_policy_starting = 0;
	/* Every ordinary association cancellation invalidates a WCL SAE request.
	 * The one SCAN_ISSUED exception lives solely in the controlled replacement
	 * helper below, never in this general retry/reset path. */
	ieee80211_sae_wcl_request_clear_locked(ic, &revocation);
	if (lock != NULL) {
		txn_id = ieee80211_pae_mfp_txn_cancel_locked(ic, &prepared);
		cancel = ic->ic_pae_mfp_txn_cancel;
		IOSimpleLockUnlockEnableInterrupt(lock, irq);
	}
	/* Epoch cancellation only marks and notifies after the leaf lock. */
	ieee80211_sae_wcl_request_revocation_deliver(ic, &revocation);
	ieee80211_pae_mfp_txn_dispose_prepared(ic, &prepared);
	if (txn_id != 0 && cancel != NULL)
		(*cancel)(ic, txn_id);
	return epoch;
}

u_int64_t
ieee80211_pae_assoc_epoch_begin(struct ieee80211com *ic)
{
	return ieee80211_pae_assoc_epoch_begin_internal(ic, 0);
}

/* Begin the one controlled current-BSS replacement owner token. */
u_int64_t
ieee80211_pae_assoc_epoch_begin_replacement(struct ieee80211com *ic)
{
	u_int64_t epoch;
	u_int64_t prior_epoch;
	u_int64_t txn_id;
	IOSimpleLock *lock;
	IOInterruptState irq;
	void (*cancel)(struct ieee80211com *, u_int64_t);
	struct ieee80211_pae_mfp_prepared prepared;
	struct ieee80211_sae_wcl_request_revocation revocation;

	if (ic == NULL || ic->ic_opmode != IEEE80211_M_STA)
		return 0;
	lock = ic->ic_pae_selected_bss_lock;
	if (lock == NULL) {
		(void)ieee80211_pae_assoc_epoch_begin(ic);
		return 0;
	}
	bzero(&prepared, sizeof(prepared));
	bzero(&revocation, sizeof(revocation));
	irq = IOSimpleLockLockDisableInterrupt(lock);
	prior_epoch = __atomic_load_n(&ic->ic_pae_assoc_epoch,
	    __ATOMIC_ACQUIRE);
	epoch = ieee80211_pae_assoc_epoch_advance_locked(ic);
	__atomic_store_n(&ic->ic_pae_assoc_replace_epoch, epoch,
	    __ATOMIC_RELEASE);
	ieee80211_pae_selected_bss_invalidate(ic);
	ieee80211_sae_peer_rx_admission_clear_locked(ic);
	/* The initial public configuration is followed by exactly one selected
	 * BSS replacement.  Carry its marker only across that one replacement;
	 * capture() below will bind it to the post-copy BSS or erase it. */
	if (ic->ic_public_initial_bssid_pin.active != 0 &&
	    ic->ic_public_initial_bssid_pin.association_epoch == 0 &&
	    ic->ic_public_initial_bssid_pin.configuration_epoch == prior_epoch)
	{
			ic->ic_public_initial_bssid_pin.binding_pending = 1;
	}
	else
		ieee80211_public_initial_bssid_pin_clear_locked(ic);
	ic->ic_sae_wcl_request_policy_starting = 0;
	/* The direct WCL resume has exactly one permitted bind handoff: its
	 * SCAN_ISSUED request survives this post-scan replacement long enough to
	 * bind the BSS copied below.  A PENDING request is retained only through
	 * this replacement so bind_selected_bss() can reject the old scan result
	 * before it reaches the historical Open/PSK path; it is never bindable.
	 * STARTING is never retained here: end_scan() holds it before this point,
	 * while every lifecycle replacement must fail it closed.  BOUND, malformed,
	 * and every other phase are ordinary cancellation state. */
	if (ieee80211_sae_wcl_request_scan_issued_locked(ic,
	    ic->ic_sae_wcl_request.generation) ||
	    ic->ic_sae_wcl_request.phase == IEEE80211_SAE_WCL_REQUEST_PENDING)
		ic->ic_sae_wcl_request.association_epoch = 0;
	else
		ieee80211_sae_wcl_request_clear_locked(ic, &revocation);
	txn_id = ieee80211_pae_mfp_txn_cancel_locked(ic, &prepared);
	cancel = ic->ic_pae_mfp_txn_cancel;
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	ieee80211_sae_wcl_request_revocation_deliver(ic, &revocation);
	ieee80211_pae_mfp_txn_dispose_prepared(ic, &prepared);
	if (txn_id != 0 && cancel != NULL)
		(*cancel)(ic, txn_id);
	return epoch;
}

/*
 * A direct WCL resume may begin while a prior association is in RUN.  It must
 * retire that old epoch before invoking the driver's raw SCAN callback, but
 * the STARTING request itself must survive long enough for that driver to
 * decide whether it can submit a fresh scan.  This stays callback-free under
 * the leaf lock; any PMF backend cancellation is delivered only after
 * dropping it.
 */
static int
ieee80211_sae_wcl_request_fence_run_resume(struct ieee80211com *ic,
    u_int64_t generation)
{
	u_int64_t txn_id;
	IOSimpleLock *lock;
	IOInterruptState irq;
	void (*cancel)(struct ieee80211com *, u_int64_t);
	struct ieee80211_pae_mfp_prepared prepared;

	if (ic == NULL || generation == 0 ||
	    ic->ic_opmode != IEEE80211_M_STA)
		return 0;
	lock = ic->ic_pae_selected_bss_lock;
	if (lock == NULL)
		return 0;
	bzero(&prepared, sizeof(prepared));
	irq = IOSimpleLockLockDisableInterrupt(lock);
	if (ic->ic_opmode != IEEE80211_M_STA ||
	    ic->ic_state != IEEE80211_S_RUN ||
	    !ieee80211_sae_wcl_request_scan_starting_locked(ic, generation)) {
		IOSimpleLockUnlockEnableInterrupt(lock, irq);
		return 0;
	}
	(void)ieee80211_pae_assoc_epoch_advance_locked(ic);
	__atomic_store_n(&ic->ic_pae_assoc_replace_epoch, 0,
	    __ATOMIC_RELEASE);
	ieee80211_pae_selected_bss_invalidate(ic);
	ieee80211_sae_peer_rx_admission_clear_locked(ic);
	/* A direct WCL RUN-resume owns a new association epoch and cannot inherit
	 * public initial-BSS provenance from the prior RUN owner. */
	ieee80211_public_initial_bssid_pin_clear_locked(ic);
	/* Do not bind an old RUN epoch to the resumed request. */
	ic->ic_sae_wcl_request.association_epoch = 0;
	txn_id = ieee80211_pae_mfp_txn_cancel_locked(ic, &prepared);
	cancel = ic->ic_pae_mfp_txn_cancel;
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	ieee80211_pae_mfp_txn_dispose_prepared(ic, &prepared);
	if (txn_id != 0 && cancel != NULL)
		(*cancel)(ic, txn_id);
	return 1;
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
	struct ieee80211_sae_wcl_request_revocation revocation;

	if (ic == NULL)
		return;
	lock = ic->ic_pae_selected_bss_lock;
	if (lock == NULL)
		return;
	/* Node teardown is complete and all owners have drained before this
	 * terminal lock destruction, so no reader can retain a CCMP snapshot. */
	if (ieee80211_ccmp_lifetime_drain(ic) != 0)
		panic("ieee80211_pae_selected_bss_lock_destroy CCMP lifetime");

	bzero(&revocation, sizeof(revocation));
	irq = IOSimpleLockLockDisableInterrupt(lock);
	ieee80211_pae_selected_bss_invalidate(ic);
	ieee80211_sae_peer_rx_admission_clear_locked(ic);
	ieee80211_public_initial_bssid_pin_clear_locked(ic);
	ieee80211_sae_wcl_request_clear_locked(ic, &revocation);
	ic->ic_sae_wcl_request_policy_starting = 0;
	ic->ic_sae_wcl_request_join_active = 0;
	__atomic_store_n(&ic->ic_pae_assoc_replace_epoch, 0,
	    __ATOMIC_RELEASE);
	ic->ic_pae_selected_bss_lock = NULL;
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	ieee80211_sae_wcl_request_revocation_deliver(ic, &revocation);
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
    enum ieee80211_state nstate, int arg)
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
	/* Two exact scanner-owned edges may carry an unbound public marker.  A
	 * channel hop preserves it within one scan.  A public association restart
	 * preserves the marker while IWN aborts a command built before the new
	 * ESS/BSSID policy and replays a fresh directed scan.  Every untagged
	 * request remains a hard cancellation. */
	if ((ic->ic_state == IEEE80211_S_SCAN && nstate == IEEE80211_S_SCAN &&
	     arg == IEEE80211_NEWSTATE_ARG_SCAN_HOP) ||
	    (nstate == IEEE80211_S_SCAN &&
	     arg == IEEE80211_NEWSTATE_ARG_PUBLIC_ASSOCIATE)) {
		(void)ieee80211_pae_assoc_epoch_begin_internal(ic, 1);
		return;
	}
	(void)ieee80211_pae_assoc_epoch_begin(ic);
}

/*
 * Begin the one pure-SAE WCL route.  Unlike publish(), this owns the local
 * RSN policy too: the generic WPA parameter carrier cannot express SAE and
 * must not silently fall back to PSK or 802.1X.  The caller has already
 * invalidated any host-owned PMK before entering here; this helper adds no
 * credential, no raw RSN IE and no state transition.
 *
 * The request is deliberately S_SCAN-only for now.  Replacing a live RUN
 * owner needs a separate request-preserving PMK reset path; using the normal
 * epoch reset after publication would erase the just-created generation.
 */
u_int64_t
ieee80211_sae_wcl_request_begin(struct ieee80211com *ic,
    const u_int8_t bssid[IEEE80211_ADDR_LEN], const u_int8_t *ssid,
    u_int ssid_len)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_sae_wcl_request *request;
	struct ieee80211_sae_wcl_request_revocation revocation;
	u_int64_t generation = 0;

	if (ic == NULL || bssid == NULL || ssid == NULL || ssid_len == 0 ||
	    ssid_len > IEEE80211_NWID_LEN ||
	    !ieee80211_sae_wcl_request_bssid_is_unicast_nonzero(bssid) ||
	    ic->ic_opmode != IEEE80211_M_STA ||
	    ic->ic_state != IEEE80211_S_SCAN ||
	    (ic->ic_caps & IEEE80211_C_RSN) == 0)
		return 0;
	lock = ic->ic_pae_selected_bss_lock;
	if (lock == NULL)
		return 0;

	bzero(&revocation, sizeof(revocation));
	irq = IOSimpleLockLockDisableInterrupt(lock);
	if (ic->ic_opmode != IEEE80211_M_STA ||
	    ic->ic_state != IEEE80211_S_SCAN ||
	    ic->ic_sae_wcl_request_policy_starting != 0 ||
	    ieee80211_sae_wcl_request_join_active_locked(ic) ||
	    ic->ic_sae_wcl_request_next_generation == (u_int64_t)-1 ||
	    (ic->ic_sae_wcl_request.phase != IEEE80211_SAE_WCL_REQUEST_NONE &&
	     ic->ic_sae_wcl_request.phase != IEEE80211_SAE_WCL_REQUEST_PENDING &&
	     ic->ic_sae_wcl_request.phase !=
	     IEEE80211_SAE_WCL_REQUEST_SCAN_ISSUED))
		goto out_unlock;
	/* Pure-SAE WCL bypasses generic associateSSID(); retire any old public
	 * initial-BSS provenance before this non-public policy writes DESBSSID. */
	ieee80211_public_initial_bssid_pin_clear_locked(ic);
	/* The Skywalk ingress has already called clearExternalPmkEligibilityLocked,
	 * including its epoch reset.  Own this narrow interval before deleting a
	 * stale WEP key so a concurrent legacy node_join_bss() cannot begin after
	 * the non-destructive precheck and then lose its RSN policy beneath it. */
	ic->ic_sae_wcl_request_policy_starting = 1;
	IOSimpleLockUnlockEnableInterrupt(lock, irq);

	/* WEP teardown invokes the ordinary driver key callback, so never hold the
	 * leaf spin lock across it.  It does not advance the association epoch;
	 * any genuine lifecycle edge clears policy_starting and makes the recheck
	 * below fail closed. */
	ieee80211_disable_wep(ic);

	irq = IOSimpleLockLockDisableInterrupt(lock);
	if (ic->ic_opmode != IEEE80211_M_STA ||
	    ic->ic_state != IEEE80211_S_SCAN ||
	    ic->ic_sae_wcl_request_policy_starting == 0 ||
	    ieee80211_sae_wcl_request_join_active_locked(ic) ||
	    ic->ic_sae_wcl_request_next_generation == (u_int64_t)-1 ||
	    (ic->ic_sae_wcl_request.phase != IEEE80211_SAE_WCL_REQUEST_NONE &&
	     ic->ic_sae_wcl_request.phase != IEEE80211_SAE_WCL_REQUEST_PENDING &&
	     ic->ic_sae_wcl_request.phase !=
	     IEEE80211_SAE_WCL_REQUEST_SCAN_ISSUED))
		goto out_clear_reservation;
	if (ic->ic_sae_wcl_request.phase != IEEE80211_SAE_WCL_REQUEST_NONE)
		ieee80211_sae_wcl_request_clear_locked(ic, &revocation);
	generation = ++ic->ic_sae_wcl_request_next_generation;
	_KASSERT(generation != 0);

	/* RSN from the selected scan BSS remains authoritative.  In particular,
	 * no opaque WCL RSN override or PSK/PLTI owner is carried into this path.
	 * MFP is a required association policy, but no IEEE80211_C_MFP test is an
	 * SAE admission gate here: the separately opted-in backend owns whatever
	 * protected-management transport is available after authentication. */
	ic->ic_rsnprotos = IEEE80211_PROTO_RSN;
	ic->ic_rsnakms = IEEE80211_AKM_SAE;
	ic->ic_rsnciphers = IEEE80211_CIPHER_CCMP;
	ic->ic_rsngroupcipher = IEEE80211_CIPHER_CCMP;
	ic->ic_rsngroupmgmtcipher = IEEE80211_CIPHER_BIP;
	ic->ic_flags &= ~(IEEE80211_F_PSK | IEEE80211_F_WEPON);
	ic->ic_flags |= IEEE80211_F_RSNON | IEEE80211_F_MFPR |
	    IEEE80211_F_DESBSSID;
	ic->ic_pae_mfp_requested = 1;
	ic->ic_external_pmk_owner = 0;
	explicit_bzero(ic->ic_psk, sizeof(ic->ic_psk));
	ic->ic_des_esslen = (u_int8_t)ssid_len;
	explicit_bzero(ic->ic_des_essid, sizeof(ic->ic_des_essid));
	memcpy(ic->ic_des_essid, ssid, ssid_len);
	IEEE80211_ADDR_COPY(ic->ic_des_bssid, bssid);
#ifdef USE_APPLE_SUPPLICANT
	explicit_bzero(ic->ic_rsn_ie_override,
	    sizeof(ic->ic_rsn_ie_override));
#endif
	ic->ic_sae_wcl_policy_generation = generation;

	request = &ic->ic_sae_wcl_request;
	explicit_bzero(request, sizeof(*request));
	request->generation = generation;
	IEEE80211_ADDR_COPY(request->bssid, bssid);
	request->ssid_len = (u_int8_t)ssid_len;
	memcpy(request->ssid, ssid, ssid_len);
	request->phase = IEEE80211_SAE_WCL_REQUEST_PENDING;

out_clear_reservation:
	ic->ic_sae_wcl_request_policy_starting = 0;
	out_unlock:
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	ieee80211_sae_wcl_request_revocation_deliver(ic, &revocation);
	return generation;
}

/*
 * Publish public, exact WCL target identity before the separately-owned HAL
 * copies a CIPHER_PWD credential.  A newer request may atomically supersede
 * only PENDING or SCAN_ISSUED public state; the private driver slot performs
 * its matching newer-generation replacement independently.  Once BOUND, an
 * attempt is in flight and a second request fails closed rather than changing
 * the BSS beneath it.
 */
u_int64_t
ieee80211_sae_wcl_request_publish(struct ieee80211com *ic,
    const u_int8_t bssid[IEEE80211_ADDR_LEN], const u_int8_t *ssid,
    u_int ssid_len)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_sae_wcl_request *request;
	struct ieee80211_sae_wcl_request_revocation revocation;
	u_int64_t generation = 0;

	if (ic == NULL || bssid == NULL || ssid == NULL || ssid_len == 0 ||
	    ssid_len > IEEE80211_NWID_LEN ||
	    !ieee80211_sae_wcl_request_bssid_is_unicast_nonzero(bssid) ||
	    ic->ic_opmode != IEEE80211_M_STA)
		return 0;
	lock = ic->ic_pae_selected_bss_lock;
	if (lock == NULL)
		return 0;
	bzero(&revocation, sizeof(revocation));
	irq = IOSimpleLockLockDisableInterrupt(lock);
	if (ic->ic_opmode != IEEE80211_M_STA ||
	    (ic->ic_state != IEEE80211_S_SCAN &&
	     ic->ic_state != IEEE80211_S_RUN) ||
	    (ic->ic_state == IEEE80211_S_RUN &&
	    !ieee80211_sae_wcl_request_run_is_stable_locked(ic)) ||
	    ic->ic_sae_wcl_request_policy_starting != 0 ||
	    ieee80211_sae_wcl_request_join_active_locked(ic) ||
	    ic->ic_sae_wcl_request_next_generation == (u_int64_t)-1 ||
	    (ic->ic_sae_wcl_request.phase != IEEE80211_SAE_WCL_REQUEST_NONE &&
	     ic->ic_sae_wcl_request.phase != IEEE80211_SAE_WCL_REQUEST_PENDING &&
	     ic->ic_sae_wcl_request.phase !=
	     IEEE80211_SAE_WCL_REQUEST_SCAN_ISSUED))
		goto out;
	if (ic->ic_sae_wcl_request.phase != IEEE80211_SAE_WCL_REQUEST_NONE)
		ieee80211_sae_wcl_request_clear_locked(ic, &revocation);
	generation = ++ic->ic_sae_wcl_request_next_generation;
	/* A nonzero counter cannot become zero because the overflow edge above
	 * refuses publication rather than wrapping and reusing a backend fence. */
	_KASSERT(generation != 0);
	request = &ic->ic_sae_wcl_request;
	explicit_bzero(request, sizeof(*request));
	request->generation = generation;
	IEEE80211_ADDR_COPY(request->bssid, bssid);
	request->ssid_len = (u_int8_t)ssid_len;
	memcpy(request->ssid, ssid, ssid_len);
	request->phase = IEEE80211_SAE_WCL_REQUEST_PENDING;
out:
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	ieee80211_sae_wcl_request_revocation_deliver(ic, &revocation);
	return generation;
}

/* Bracket the full node-copy/RSN-select/S_AUTH handoff.  The marker carries
 * no association identity and never authorizes SAE; it solely makes a late
 * WCL publication fail busy while a legacy join is already committed. */
int
ieee80211_sae_wcl_request_join_begin(struct ieee80211com *ic)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	int begun = 0;

	if (ic == NULL)
		return 0;
	/* This fence has no meaning for legacy non-STA joins or before the leaf
	 * lock exists.  Preserve their historical no-op/allow behavior; zero is
	 * reserved for a live pure-SAE policy reservation that actually won. */
	if (ic->ic_opmode != IEEE80211_M_STA)
		return 1;
	lock = ic->ic_pae_selected_bss_lock;
	if (lock == NULL)
		return 1;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	if (ic->ic_opmode == IEEE80211_M_STA &&
	    ic->ic_sae_wcl_request_policy_starting == 0) {
		ic->ic_sae_wcl_request_join_active = 1;
		begun = 1;
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return begun;
}

void
ieee80211_sae_wcl_request_join_end(struct ieee80211com *ic)
{
	IOSimpleLock *lock;
	IOInterruptState irq;

	if (ic == NULL)
		return;
	lock = ic->ic_pae_selected_bss_lock;
	if (lock == NULL)
		return;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	ic->ic_sae_wcl_request_join_active = 0;
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
}

/*
 * Clear one public request only when its generation still matches.  This is
 * intentionally a leaf/value operation: it captures only the driver's
 * generation-only revocation callback under the leaf lock, then tells that
 * separately-owned credential slot to cancel after unlock.  No password,
 * PMK, node, or other private state crosses this generic boundary.
 */
int
ieee80211_sae_wcl_request_clear_if_generation(struct ieee80211com *ic,
    u_int64_t generation)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_sae_wcl_request_revocation revocation;
	int cleared = 0;

	if (ic == NULL || generation == 0)
		return 0;
	lock = ic->ic_pae_selected_bss_lock;
	if (lock == NULL)
		return 0;
	bzero(&revocation, sizeof(revocation));
	irq = IOSimpleLockLockDisableInterrupt(lock);
	if (ic->ic_sae_wcl_request.generation == generation &&
	    ieee80211_sae_wcl_request_phase_is_active(
	    ic->ic_sae_wcl_request.phase)) {
		ieee80211_sae_wcl_request_clear_locked(ic, &revocation);
		cleared = 1;
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	ieee80211_sae_wcl_request_revocation_deliver(ic, &revocation);
	return cleared;
}

/*
 * Resume exactly one normal scan selection after WCL published an explicit
 * target.  Do not use the ordinary state-transition macro: its SCAN -> SCAN
 * fence correctly erases arbitrary stale attempts.  This request remains
 * SCAN_STARTING until IWN accepts a fresh scan, then becomes SCAN_ISSUED
 * through one controlled node replacement so it can bind the exact selected
 * BSS.  A RUN origin first fences the old association with the dedicated
 * preserve-only helper above.
 */
int
ieee80211_sae_wcl_request_resume_scan(struct ieee80211com *ic,
    u_int64_t generation)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	enum ieee80211_state origin;
	int result = IEEE80211_SAE_WCL_REQUEST_RESUME_FAILED;
	int scan_error;

	if (ic == NULL || generation == 0 ||
	    ic->ic_opmode != IEEE80211_M_STA)
		return IEEE80211_SAE_WCL_REQUEST_RESUME_FAILED;
	lock = ic->ic_pae_selected_bss_lock;
	if (lock == NULL)
		return IEEE80211_SAE_WCL_REQUEST_RESUME_FAILED;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	if (ic->ic_opmode != IEEE80211_M_STA || ic->ic_newstate == NULL ||
	    !ieee80211_sae_wcl_request_owner_hooks_ready_locked(ic) ||
	    ic->ic_sae_wcl_request_policy_starting != 0 ||
	    ieee80211_sae_wcl_request_join_active_locked(ic) ||
	    ic->ic_sae_wcl_request.generation != generation ||
	    ic->ic_sae_wcl_request.phase != IEEE80211_SAE_WCL_REQUEST_PENDING ||
	    !ieee80211_sae_wcl_request_identity_is_valid_locked(
	    &ic->ic_sae_wcl_request) ||
	    (ic->ic_state != IEEE80211_S_SCAN &&
	     ic->ic_state != IEEE80211_S_RUN)) {
		IOSimpleLockUnlockEnableInterrupt(lock, irq);
		(void)ieee80211_sae_wcl_request_clear_if_generation(ic,
		    generation);
		return IEEE80211_SAE_WCL_REQUEST_RESUME_FAILED;
	}
	origin = ic->ic_state;
	ic->ic_sae_wcl_request.association_epoch = 0;
	ic->ic_sae_wcl_request.phase = IEEE80211_SAE_WCL_REQUEST_SCAN_STARTING;
	IOSimpleLockUnlockEnableInterrupt(lock, irq);

	if (origin == IEEE80211_S_RUN &&
	    !ieee80211_sae_wcl_request_fence_run_resume(ic, generation))
		goto out;

	/* Recheck after the out-of-lock RUN cancellation before calling the raw
	 * state method.  A competing ordinary transition wins by clearing this
	 * generation instead of receiving an unsolicited second scan request. */
	irq = IOSimpleLockLockDisableInterrupt(lock);
	if (!ieee80211_sae_wcl_request_scan_starting_locked(ic, generation) ||
	    !ieee80211_sae_wcl_request_owner_hooks_ready_locked(ic) ||
	    ic->ic_newstate == NULL || ic->ic_state != origin) {
		IOSimpleLockUnlockEnableInterrupt(lock, irq);
		goto out;
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);

	/* This is deliberately the raw driver state method, not the epoch macro.
	 * A newer WCL publication may supersede this request after the final
	 * pre-call check.  IWN may return EAGAIN when another scan is already in
	 * flight; that is an explicit retry, never permission to consume its old
	 * census.  Success is acknowledged only if IWN promoted this exact
	 * generation after accepting a fresh scan. */
	scan_error = (*ic->ic_newstate)(ic, IEEE80211_S_SCAN, -1);
	if (scan_error == EAGAIN) {
		result = IEEE80211_SAE_WCL_REQUEST_RESUME_RETRY;
	} else if (scan_error == 0) {
		irq = IOSimpleLockLockDisableInterrupt(lock);
		if (ieee80211_sae_wcl_request_owner_hooks_ready_locked(ic) &&
		    (ieee80211_sae_wcl_request_scan_issued_locked(ic, generation) ||
		    (ic->ic_sae_wcl_request.phase ==
		    IEEE80211_SAE_WCL_REQUEST_BOUND &&
		    ic->ic_sae_wcl_request.generation == generation &&
		    ieee80211_sae_wcl_request_identity_is_valid_locked(
		    &ic->ic_sae_wcl_request))))
			result = IEEE80211_SAE_WCL_REQUEST_RESUME_STARTED;
		IOSimpleLockUnlockEnableInterrupt(lock, irq);
	}
out:
	if (result != IEEE80211_SAE_WCL_REQUEST_RESUME_STARTED)
		(void)ieee80211_sae_wcl_request_clear_if_generation(ic,
		    generation);
	return result;
}

/* Caller holds ic_pae_selected_bss_lock.  This recognizes only begin()'s
 * complete, exact public policy; it does not infer ownership from a generic
 * WCL request or from a credential-bearing driver slot. */
static int
ieee80211_sae_wcl_request_scan_policy_matches_locked(
    const struct ieee80211com *ic,
    const struct ieee80211_sae_wcl_request *request)
{
	return ic != NULL && request != NULL && request->generation != 0 &&
	    request->generation == ic->ic_sae_wcl_policy_generation &&
	    ieee80211_sae_wcl_request_identity_is_valid_locked(request) &&
	    (ic->ic_flags & (IEEE80211_F_RSNON | IEEE80211_F_MFPR)) ==
	    (IEEE80211_F_RSNON | IEEE80211_F_MFPR) &&
	    (ic->ic_flags & IEEE80211_F_PSK) == 0 &&
	    ic->ic_pae_mfp_requested != 0 &&
	    ic->ic_rsnprotos == IEEE80211_PROTO_RSN &&
	    ic->ic_rsnakms == IEEE80211_AKM_SAE &&
	    ic->ic_rsnciphers == IEEE80211_CIPHER_CCMP &&
	    ic->ic_rsngroupcipher == IEEE80211_CIPHER_CCMP &&
	    ic->ic_rsngroupmgmtcipher == IEEE80211_CIPHER_BIP;
}

/* Return only the generation of an exact direct request that is waiting for
 * this driver's raw S_SCAN call to submit a new scan.  No state is changed;
 * the returned value is a public cancellation fence, not an SAE admission
 * token. */
int
ieee80211_sae_wcl_request_scan_starting(struct ieee80211com *ic,
    u_int64_t *generation)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	int starting = 0;

	if (generation != NULL)
		*generation = 0;
	if (ic == NULL || generation == NULL ||
	    ic->ic_opmode != IEEE80211_M_STA)
		return 0;
	lock = ic->ic_pae_selected_bss_lock;
	if (lock == NULL)
		return 0;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	if (ieee80211_sae_wcl_request_owner_hooks_ready_locked(ic) &&
	    ieee80211_sae_wcl_request_scan_starting_locked(ic,
	    ic->ic_sae_wcl_request.generation) &&
	    ieee80211_sae_wcl_request_scan_policy_matches_locked(ic,
	    &ic->ic_sae_wcl_request)) {
		*generation = ic->ic_sae_wcl_request.generation;
		starting = 1;
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return starting;
}

/* Promote only an exact STARTING request after the driver has accepted a
 * fresh scan command.  A coalesced historical scan never reaches this path,
 * and a lifecycle cancellation that wins before promotion leaves the request
 * unowned and therefore unselectable. */
int
ieee80211_sae_wcl_request_scan_started(struct ieee80211com *ic,
    u_int64_t generation)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	int started = 0;

	if (ic == NULL || generation == 0 ||
	    ic->ic_opmode != IEEE80211_M_STA ||
	    ic->ic_state != IEEE80211_S_SCAN)
		return 0;
	lock = ic->ic_pae_selected_bss_lock;
	if (lock == NULL)
		return 0;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	if (ieee80211_sae_wcl_request_owner_hooks_ready_locked(ic) &&
	    ieee80211_sae_wcl_request_scan_starting_locked(ic, generation) &&
	    ieee80211_sae_wcl_request_scan_policy_matches_locked(ic,
	    &ic->ic_sae_wcl_request)) {
		ic->ic_sae_wcl_request.phase =
		    IEEE80211_SAE_WCL_REQUEST_SCAN_ISSUED;
		started = 1;
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return started;
}

/*
 * A scan can finish between begin() reserving its short WEP-teardown window
 * and resume_scan() issuing the replacement scan.  Neither that reservation
 * nor a PENDING request may let end_scan() choose a historical BSS: the
 * result belongs to the old scan and switch_ess() would also erase the new
 * RSN/SAE policy.  HOLD merely returns from end_scan(); resume_scan() starts
 * the sole new scan after the private credential has staged.
 */
int
ieee80211_sae_wcl_request_scan_selection_held(struct ieee80211com *ic)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	const struct ieee80211_sae_wcl_request *request;
	int held = 0;

	if (ic == NULL || ic->ic_opmode != IEEE80211_M_STA ||
	    ic->ic_state != IEEE80211_S_SCAN)
		return 0;
	lock = ic->ic_pae_selected_bss_lock;
	if (lock == NULL)
		return 0;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	request = &ic->ic_sae_wcl_request;
	if (ic->ic_sae_wcl_request_policy_starting != 0 ||
	    ((request->phase == IEEE80211_SAE_WCL_REQUEST_PENDING ||
	    request->phase == IEEE80211_SAE_WCL_REQUEST_SCAN_STARTING) &&
	    ieee80211_sae_wcl_request_scan_policy_matches_locked(ic, request)))
		held = 1;
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return held;
}

/*
 * The old OpenBSD ESS list predates WCL's exact preselection policy.  Its
 * switch_ess() helper calls ieee80211_set_ess(), which in turn disables RSN
 * and starts a fresh association epoch.  After resume_scan() issued the one
 * exact direct policy, suppress only that ESS overwrite while allowing the
 * replacement scan to select and bind its BSS.  PENDING is intentionally not
 * selection-owned: scan_selection_held() handles it without a false join.
 */
int
ieee80211_sae_wcl_request_scan_selection_owned(struct ieee80211com *ic)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	const struct ieee80211_sae_wcl_request *request;
	int owned = 0;

	if (ic == NULL || ic->ic_opmode != IEEE80211_M_STA ||
	    ic->ic_state != IEEE80211_S_SCAN)
		return 0;
	lock = ic->ic_pae_selected_bss_lock;
	if (lock == NULL)
		return 0;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	request = &ic->ic_sae_wcl_request;
	if (request->phase == IEEE80211_SAE_WCL_REQUEST_SCAN_ISSUED &&
	    ieee80211_sae_wcl_request_scan_policy_matches_locked(ic, request))
		owned = 1;
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return owned;
}

/* Caller holds ic_pae_selected_bss_lock whenever that lock exists. */
static int
ieee80211_sae_wcl_request_matches_current_locked(struct ieee80211com *ic,
    const struct ieee80211_sae_wcl_request *request,
    const struct ieee80211_node *ni, u_int64_t expected_epoch)
{
	const struct ieee80211_pae_selected_bss *selected;
	u_int8_t profile;

	if (ic == NULL || request == NULL || ni == NULL ||
	    expected_epoch == 0 || ic->ic_opmode != IEEE80211_M_STA ||
	    !ieee80211_sae_wcl_request_identity_is_valid_locked(request) ||
	    ic->ic_bss != ni ||
	    __atomic_load_n(&ic->ic_pae_assoc_epoch, __ATOMIC_ACQUIRE) !=
		expected_epoch ||
	    __atomic_load_n(&ic->ic_pae_assoc_replace_epoch,
		__ATOMIC_ACQUIRE) != 0 ||
	    !IEEE80211_ADDR_EQ(request->bssid, ni->ni_bssid) ||
	    request->ssid_len != ni->ni_esslen ||
	    memcmp(request->ssid, ni->ni_essid, request->ssid_len) != 0)
		return 0;
	selected = &ic->ic_pae_selected_bss;
	profile = selected->strict_pure_sae_profile;
	return ieee80211_pae_selected_bss_identity_matches(selected,
	    expected_epoch, request->bssid, request->ssid, request->ssid_len) &&
	    (profile == IEEE80211_SAE_SELECTED_BSS_PROFILE_PURE ||
	     profile == IEEE80211_SAE_SELECTED_BSS_PROFILE_TRANSITION);
}

/*
 * Consume the sole controlled SCAN_ISSUED handoff after node_join_bss copied
 * its choice and selected-BSS capture published the same epoch.  Anything
 * other than an exact selected pure/transition profile clears the live
 * request and makes the caller return to SCAN before generic Open auth.
 */
int
ieee80211_sae_wcl_request_bind_selected_bss(struct ieee80211com *ic,
    const struct ieee80211_node *ni, u_int64_t expected_epoch)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_sae_wcl_request_revocation revocation;
	int result = IEEE80211_SAE_WCL_REQUEST_BIND_NONE;

	if (ic == NULL)
		return IEEE80211_SAE_WCL_REQUEST_BIND_NONE;
	lock = ic->ic_pae_selected_bss_lock;
	if (lock == NULL)
		return IEEE80211_SAE_WCL_REQUEST_BIND_NONE;
	bzero(&revocation, sizeof(revocation));
	irq = IOSimpleLockLockDisableInterrupt(lock);
	if (ic->ic_sae_wcl_request.phase == IEEE80211_SAE_WCL_REQUEST_NONE) {
		if (ic->ic_sae_wcl_request.generation != 0 ||
		    ic->ic_sae_wcl_request.association_epoch != 0) {
			ieee80211_sae_wcl_request_clear_locked(ic, &revocation);
			result = IEEE80211_SAE_WCL_REQUEST_BIND_REJECTED;
		}
		goto out;
	}
	if (ic->ic_state == IEEE80211_S_SCAN &&
	    ieee80211_sae_wcl_request_owner_hooks_ready_locked(ic) &&
	    ieee80211_sae_wcl_request_scan_issued_locked(ic,
	    ic->ic_sae_wcl_request.generation) &&
	    ieee80211_sae_wcl_request_matches_current_locked(ic,
	    &ic->ic_sae_wcl_request, ni, expected_epoch)) {
		ic->ic_sae_wcl_request.association_epoch = expected_epoch;
		ic->ic_sae_wcl_request.phase = IEEE80211_SAE_WCL_REQUEST_BOUND;
		result = IEEE80211_SAE_WCL_REQUEST_BIND_BOUND;
	} else {
		ieee80211_sae_wcl_request_clear_locked(ic, &revocation);
		result = IEEE80211_SAE_WCL_REQUEST_BIND_REJECTED;
	}
out:
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	ieee80211_sae_wcl_request_revocation_deliver(ic, &revocation);
	return result;
}

int
ieee80211_sae_wcl_request_bound_current(struct ieee80211com *ic,
    const struct ieee80211_node *ni)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	u_int64_t epoch;
	int bound = 0;

	if (ic == NULL || ni == NULL || ic->ic_opmode != IEEE80211_M_STA)
		return 0;
	lock = ic->ic_pae_selected_bss_lock;
	if (lock == NULL)
		return 0;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	epoch = __atomic_load_n(&ic->ic_pae_assoc_epoch, __ATOMIC_ACQUIRE);
	if (epoch != 0 &&
	    ic->ic_sae_wcl_request.phase == IEEE80211_SAE_WCL_REQUEST_BOUND &&
	    ic->ic_sae_wcl_request.association_epoch == epoch &&
	    ieee80211_sae_wcl_request_owner_hooks_ready_locked(ic) &&
	    ieee80211_sae_wcl_request_matches_current_locked(ic,
	    &ic->ic_sae_wcl_request, ni, epoch))
		bound = 1;
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return bound;
}

/*
 * Copy a BOUND direct-WCL request only after the same exact current-BSS and
 * owner-hook checks that gate RSN output.  expected_generation == 0 lets the
 * first driver owner atomically capture the sole current BOUND request;
 * nonzero is an exact stale-worker fence.  The caller owns the surrounding
 * HAL/lifecycle claim that keeps ic and its leaf lock alive; this function
 * serializes fields but never creates a node lifetime claim.  The group and
 * method remain deliberately absent: pure SAE may use the existing narrow
 * admission helper, while transition SAE needs a later explicit helper.
 */
int
ieee80211_sae_wcl_request_copyout_bound_current(struct ieee80211com *ic,
    u_int64_t expected_generation,
    struct ieee80211_sae_wcl_bound_request *out)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	const struct ieee80211_sae_wcl_request *request;
	const struct ieee80211_pae_selected_bss *selected;
	u_int64_t epoch;
	int copied = 0;

	if (out == NULL)
		return 0;
	explicit_bzero(out, sizeof(*out));
	if (ic == NULL || ic->ic_opmode != IEEE80211_M_STA)
		return 0;
	lock = ic->ic_pae_selected_bss_lock;
	if (lock == NULL)
		return 0;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	epoch = __atomic_load_n(&ic->ic_pae_assoc_epoch, __ATOMIC_ACQUIRE);
	request = &ic->ic_sae_wcl_request;
	selected = &ic->ic_pae_selected_bss;
	if (epoch != 0 && request->generation != 0 &&
	    (expected_generation == 0 ||
	    request->generation == expected_generation) &&
	    request->phase == IEEE80211_SAE_WCL_REQUEST_BOUND &&
	    request->association_epoch == epoch &&
	    ieee80211_sae_wcl_request_owner_hooks_ready_locked(ic) &&
	    ic->ic_bss != NULL &&
	    ieee80211_sae_peer_rx_mac_is_unicast_nonzero(ic->ic_myaddr) &&
	    ieee80211_sae_wcl_request_matches_current_locked(ic, request,
	    ic->ic_bss, epoch)) {
		out->generation = request->generation;
		out->association_epoch = epoch;
		out->sae_scan_flags = selected->sae_scan_flags;
		IEEE80211_ADDR_COPY(out->bssid, request->bssid);
		IEEE80211_ADDR_COPY(out->sta, ic->ic_myaddr);
		out->ssid_len = request->ssid_len;
		memcpy(out->ssid, request->ssid, request->ssid_len);
		out->sae_profile = selected->strict_pure_sae_profile;
		copied = 1;
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return copied;
}

/*
 * Direct WCL keeps the controller-facing peer-RX admission pure-SAE-only.
 * A transition BSS reaches this companion predicate only after an exact WCL
 * request bound it to the selected BSS.  It preserves the current HnP-only
 * aperture: transition adds the exact SAE|PSK census fact, not H2E, SAE-PK,
 * password identifiers, or any other unmodeled scan capability.
 *
 * Caller holds ic_pae_selected_bss_lock.  selected is the live fixed-byte
 * record and out is a stack-owned value; neither may retain a node, IE, or
 * credential.
 */
static int
ieee80211_sae_wcl_peer_rx_admission_group19_hnp_locked(
    const struct ieee80211_pae_selected_bss *selected,
    struct ieee80211_sae_admission *out)
{
	u_int32_t flags;

	if (out != NULL)
		explicit_bzero(out, sizeof(*out));
	if (selected == NULL || out == NULL || selected->epoch == 0)
		return 0;
	if (selected->strict_pure_sae_profile ==
	    IEEE80211_SAE_SELECTED_BSS_PROFILE_PURE)
		return ieee80211_sae_admission_group19_hnp(selected, out);
	if (selected->strict_pure_sae_profile !=
	    IEEE80211_SAE_SELECTED_BSS_PROFILE_TRANSITION)
		return 0;

	flags = selected->sae_scan_flags;
	if ((flags & IEEE80211_SAE_SCAN_CENSUS_COMPLETE) == 0 ||
	    !ieee80211_sae_scan_transition_akm_census_is_supported(flags) ||
	    (flags & ~(IEEE80211_SAE_ADMISSION_GROUP19_HNP_ALLOWED_FLAGS |
	    IEEE80211_SAE_SCAN_TRANSITION_AKM_MASK)) != 0)
		return 0;

	out->group = IEEE80211_SAE_ADMISSION_GROUP_19;
	out->method = IEEE80211_SAE_ADMISSION_METHOD_HNP;
	return 1;
}

/*
 * Admit one exact driver-owned direct-WCL SAE peer-RX path after S_AUTH has
 * begun.  The driver supplies only the value copied by
 * ieee80211_sae_wcl_request_copyout_bound_current(); this leaf rechecks it
 * against the live BOUND request and selected BSS before publishing the
 * generic RX admission.  It does not retain the caller's value, a node, an
 * IE, or any private SAE material.
 */
int
ieee80211_sae_wcl_peer_rx_admit(struct ieee80211com *ic,
    const struct ieee80211_sae_wcl_bound_request *bound,
    u_int64_t relay_generation)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	const struct ieee80211_sae_wcl_request *request;
	const struct ieee80211_pae_selected_bss *selected;
	struct ieee80211_sae_admission profile;
	u_int64_t epoch;
	int admitted = 0;

	if (ic == NULL || bound == NULL || relay_generation == 0 ||
	    bound->generation == 0 || bound->association_epoch == 0 ||
	    bound->ssid_len == 0 || bound->ssid_len > IEEE80211_NWID_LEN ||
	    !ieee80211_sae_peer_rx_mac_is_unicast_nonzero(bound->bssid) ||
	    !ieee80211_sae_peer_rx_mac_is_unicast_nonzero(bound->sta) ||
	    ic->ic_opmode != IEEE80211_M_STA)
		return 0;
	lock = ic->ic_pae_selected_bss_lock;
	if (lock == NULL)
		return 0;
	explicit_bzero(&profile, sizeof(profile));
	irq = IOSimpleLockLockDisableInterrupt(lock);
	/* A failed exact replacement must never leave a former direct owner RX-live. */
	ieee80211_sae_peer_rx_admission_clear_locked(ic);
	epoch = __atomic_load_n(&ic->ic_pae_assoc_epoch, __ATOMIC_ACQUIRE);
	request = &ic->ic_sae_wcl_request;
	selected = &ic->ic_pae_selected_bss;
	if (ic->ic_opmode == IEEE80211_M_STA &&
	    ic->ic_state == IEEE80211_S_AUTH && ic->ic_bss != NULL &&
	    epoch == bound->association_epoch &&
	    request->generation == bound->generation &&
	    request->phase == IEEE80211_SAE_WCL_REQUEST_BOUND &&
	    request->association_epoch == epoch &&
	    ieee80211_sae_wcl_request_owner_hooks_ready_locked(ic) &&
	    IEEE80211_ADDR_EQ(request->bssid, bound->bssid) &&
	    request->ssid_len == bound->ssid_len &&
	    memcmp(request->ssid, bound->ssid, sizeof(request->ssid)) == 0 &&
	    IEEE80211_ADDR_EQ(ic->ic_myaddr, bound->sta) &&
	    selected->sae_scan_flags == bound->sae_scan_flags &&
	    selected->strict_pure_sae_profile == bound->sae_profile &&
	    ieee80211_sae_wcl_request_matches_current_locked(ic, request,
	    ic->ic_bss, epoch) &&
	    ieee80211_sae_wcl_peer_rx_admission_group19_hnp_locked(selected,
	    &profile)) {
		ic->ic_sae_peer_rx_admission.association_epoch = epoch;
		ic->ic_sae_peer_rx_admission.relay_generation = relay_generation;
		IEEE80211_ADDR_COPY(ic->ic_sae_peer_rx_admission.bssid,
		    bound->bssid);
		IEEE80211_ADDR_COPY(ic->ic_sae_peer_rx_admission.sta,
		    bound->sta);
		ic->ic_sae_peer_rx_admission.active = 1;
		admitted = 1;
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	explicit_bzero(&profile, sizeof(profile));
	return admitted;
}

/* Caller holds ic_pae_selected_bss_lock.  This checks the public part of a
 * completed direct-SAE continuation before any PMK bytes can enter the local
 * PAE.  The raw Algorithm-3 admission deliberately did not require a PMF
 * backend; this later RSN/4-way continuation does, because MFPR cannot
 * complete an association without the selected software-PMF owner. */
static int
ieee80211_sae_wcl_request_pmk_base_current_locked(
    struct ieee80211com *ic, const struct ieee80211_node *ni,
    const struct ItlSaePmkContinuationIdentityV1 *identity,
    enum ieee80211_state expected_state)
{
	const struct ieee80211_sae_wcl_request *request;
	u_int64_t epoch;

	if (ic == NULL || ni == NULL || identity == NULL ||
	    !itl_sae_pmk_continuation_identity_is_well_formed(identity) ||
	    ic->ic_opmode != IEEE80211_M_STA ||
	    ic->ic_state != expected_state || ic->ic_bss != ni)
		return 0;
	epoch = __atomic_load_n(&ic->ic_pae_assoc_epoch, __ATOMIC_ACQUIRE);
	request = &ic->ic_sae_wcl_request;
	return epoch != 0 && identity->association_epoch == epoch &&
	    identity->request_generation != 0 &&
	    request->generation == identity->request_generation &&
	    request->phase == IEEE80211_SAE_WCL_REQUEST_BOUND &&
	    request->association_epoch == epoch &&
	    ieee80211_sae_wcl_request_owner_hooks_ready_locked(ic) &&
	    ieee80211_sae_wcl_request_scan_policy_matches_locked(ic, request) &&
	    ieee80211_sae_wcl_request_matches_current_locked(ic, request, ni,
	    epoch) &&
	    __atomic_load_n(&ic->ic_pae_assoc_replace_epoch,
	    __ATOMIC_ACQUIRE) == 0 &&
	    IEEE80211_ADDR_EQ(identity->bssid, request->bssid) &&
	    IEEE80211_ADDR_EQ(identity->bssid, ni->ni_bssid) &&
	    IEEE80211_ADDR_EQ(identity->sta, ic->ic_myaddr) &&
	    ni->ni_rsnprotos == IEEE80211_PROTO_RSN &&
	    ni->ni_rsnakms == IEEE80211_AKM_SAE &&
	    ni->ni_rsncipher == IEEE80211_CIPHER_CCMP &&
	    ni->ni_rsngroupcipher == IEEE80211_CIPHER_CCMP &&
	    (ic->ic_flags & (IEEE80211_F_RSNON | IEEE80211_F_MFPR)) ==
	    (IEEE80211_F_RSNON | IEEE80211_F_MFPR) &&
	    (ic->ic_flags & IEEE80211_F_PSK) == 0 &&
	    (ic->ic_caps & IEEE80211_C_MFP) != 0 &&
	    ic->ic_pae_mfp_requested != 0 &&
	    (ni->ni_flags & IEEE80211_NODE_MFP) != 0 &&
	    ic->ic_pae_mfp_txn_submit != NULL &&
	    ic->ic_pae_mfp_txn_cancel != NULL &&
	    ic->ic_pae_mfp_txn_finish != NULL;
}

/* Caller holds ic_pae_selected_bss_lock. */
static int
ieee80211_sae_wcl_request_pmk_claim_matches_locked(
    struct ieee80211com *ic, const struct ieee80211_node *ni,
    const struct ItlSaePmkContinuationIdentityV1 *identity,
    enum ieee80211_state expected_state)
{
	const struct ieee80211_sae_wcl_pmk_claim *claim;

	if (!ieee80211_sae_wcl_request_pmk_base_current_locked(ic, ni, identity,
	    expected_state))
		return 0;
	claim = &ic->ic_sae_wcl_pmk_claim;
	return claim->active != 0 &&
	    itl_sae_pmk_continuation_bytes_all_zero(claim->reserved,
	    sizeof(claim->reserved)) &&
	    claim->generation == identity->request_generation &&
	    claim->association_epoch == identity->association_epoch &&
	    claim->relay_generation == identity->relay_generation &&
	    claim->event_sequence == identity->event_sequence &&
	    IEEE80211_ADDR_EQ(claim->bssid, identity->bssid) &&
	    IEEE80211_ADDR_EQ(claim->sta, identity->sta) &&
	    !itl_sae_pmk_continuation_bytes_all_zero(ic->ic_psk,
	    sizeof(ic->ic_psk)) &&
	    (ni->ni_flags & IEEE80211_NODE_PMKID) != 0;
}

/*
 * Claim one verified direct-SAE PMK and its scalar-derived SAE PMKID under
 * the selected-BSS leaf.  Both values came from the same accepted in-kext
 * SAE exchange.  It intentionally leaves IEEE80211_F_PSK clear: SAE is an
 * AKM, not a legacy PSK selection policy.
 */
int
ieee80211_sae_wcl_request_pmk_claim_locked(struct ieee80211com *ic,
    const struct ItlSaePmkContinuationV1 *continuation)
{
	const struct ieee80211_sae_peer_rx_admission *admission;
	struct ieee80211_sae_wcl_pmk_claim *claim;
	struct ieee80211_node *ni;

	if (ic == NULL || continuation == NULL ||
	    !itl_sae_pmk_continuation_is_well_formed(continuation))
		return 0;
	ni = ic->ic_bss;
	if (!ieee80211_sae_wcl_request_pmk_base_current_locked(ic, ni,
	    &continuation->identity, IEEE80211_S_AUTH))
		return 0;
	admission = &ic->ic_sae_peer_rx_admission;
	claim = &ic->ic_sae_wcl_pmk_claim;
	if (claim->active != 0 || admission->active == 0 ||
	    admission->association_epoch != continuation->identity.association_epoch ||
	    admission->relay_generation != continuation->identity.relay_generation ||
	    !IEEE80211_ADDR_EQ(admission->bssid, continuation->identity.bssid) ||
	    !IEEE80211_ADDR_EQ(admission->sta, continuation->identity.sta))
		return 0;

	/* The only retained PMK copy is the pre-existing local PAE store.  Its
	 * matching WCL policy clear erases it before any new association can use
	 * it.  Do not route this through WCL/PLTI or a controller PMK installer. */
	explicit_bzero(ic->ic_psk, sizeof(ic->ic_psk));
	memcpy(ic->ic_psk, continuation->pmk, sizeof(ic->ic_psk));
	ic->ic_flags &= ~IEEE80211_F_PSK;
	ic->ic_external_pmk_owner = 0;
	explicit_bzero(ni->ni_pmk, sizeof(ni->ni_pmk));
	explicit_bzero(ni->ni_pmkid, sizeof(ni->ni_pmkid));
	ni->ni_flags &= ~(IEEE80211_NODE_PMK | IEEE80211_NODE_PMKID);
	memcpy(ni->ni_pmkid, continuation->pmkid, sizeof(ni->ni_pmkid));
	ni->ni_flags |= IEEE80211_NODE_PMKID;
	explicit_bzero(claim, sizeof(*claim));
	claim->generation = continuation->identity.request_generation;
	claim->association_epoch = continuation->identity.association_epoch;
	claim->relay_generation = continuation->identity.relay_generation;
	claim->event_sequence = continuation->identity.event_sequence;
	IEEE80211_ADDR_COPY(claim->bssid, continuation->identity.bssid);
	IEEE80211_ADDR_COPY(claim->sta, continuation->identity.sta);
	claim->active = 1;
	return 1;
}

int
ieee80211_sae_wcl_request_pmk_claim_assoc_current_locked(
    struct ieee80211com *ic, const struct ieee80211_node *ni,
    const struct ItlSaePmkContinuationIdentityV1 *identity)
{
	return ieee80211_sae_wcl_request_pmk_claim_matches_locked(ic, ni,
	    identity, IEEE80211_S_ASSOC);
}

int
ieee80211_sae_wcl_request_pmk_claim_assoc_current(struct ieee80211com *ic,
    const struct ieee80211_node *ni,
    const struct ItlSaePmkContinuationIdentityV1 *identity)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	int current = 0;

	if (ic == NULL || ni == NULL || identity == NULL)
		return 0;
	lock = ic->ic_pae_selected_bss_lock;
	if (lock == NULL)
		return 0;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	current = ieee80211_sae_wcl_request_pmk_claim_assoc_current_locked(ic,
	    ni, identity);
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return current;
}

/* ieee80211_newstate() carries only its private continuation sentinel, never
 * a secret or an IWN owner pointer.  Reconstruct the public identity from the
 * one-shot claim while holding the selected-BSS leaf, then reuse the same
 * exact current-BSS predicate as IWN's descriptor fence. */
static int
ieee80211_sae_wcl_request_pmk_claim_assoc_sentinel_current(
    struct ieee80211com *ic, const struct ieee80211_node *ni)
{
	struct ItlSaePmkContinuationIdentityV1 identity;
	const struct ieee80211_sae_wcl_pmk_claim *claim;
	IOSimpleLock *lock;
	IOInterruptState irq;
	int current = 0;

	if (ic == NULL || ni == NULL)
		return 0;
	lock = ic->ic_pae_selected_bss_lock;
	if (lock == NULL)
		return 0;
	explicit_bzero(&identity, sizeof(identity));
	irq = IOSimpleLockLockDisableInterrupt(lock);
	claim = &ic->ic_sae_wcl_pmk_claim;
	if (claim->active != 0) {
		identity.version = kItlSaePmkContinuationV1Version;
		identity.size = sizeof(identity);
		identity.request_generation = claim->generation;
		identity.association_epoch = claim->association_epoch;
		identity.relay_generation = claim->relay_generation;
		identity.event_sequence = claim->event_sequence;
		IEEE80211_ADDR_COPY(identity.bssid, claim->bssid);
		IEEE80211_ADDR_COPY(identity.sta, claim->sta);
		current = ieee80211_sae_wcl_request_pmk_claim_assoc_current_locked(
		    ic, ni, &identity);
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	explicit_bzero(&identity, sizeof(identity));
	return current;
}

int
ieee80211_sae_wcl_request_pmk_continue_assoc(struct ieee80211com *ic,
    const struct ItlSaePmkContinuationIdentityV1 *identity)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_node *ni;
	int claimed = 0;
	int error;

	if (ic == NULL || identity == NULL ||
	    !itl_sae_pmk_continuation_identity_is_well_formed(identity) ||
	    ic->ic_opmode != IEEE80211_M_STA)
		return 0;
	lock = ic->ic_pae_selected_bss_lock;
	if (lock == NULL)
		return 0;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	ni = ic->ic_bss;
	claimed = ieee80211_sae_wcl_request_pmk_claim_matches_locked(ic, ni,
	    identity, IEEE80211_S_AUTH);
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	if (!claimed || ic->ic_newstate == NULL)
		return 0;

	/* A successful direct-SAE Confirm/PMK claim is the real authentication
	 * success edge for this selected BSS.  Record it before entering ASSOC,
	 * exactly as the Open-System response path does below. */
	if (ic->ic_event_handler != NULL)
		(*ic->ic_event_handler)(ic, IEEE80211_EVT_STA_AUTH_DONE, NULL);

	/* The standard forward AUTH -> ASSOC edge preserves this epoch.  The
	 * private argument makes ieee80211_newstate() repeat the exact claim check
	 * after it has committed S_ASSOC and immediately before it queues the
	 * ordinary Association Request. */
	ieee80211_pae_assoc_epoch_note_newstate(ic, IEEE80211_S_ASSOC, -1);
	error = (*ic->ic_newstate)(ic, IEEE80211_S_ASSOC,
	    IEEE80211_SAE_WCL_MGMT_PMK_CONTINUE);
	return error == 0;
}

enum ieee80211_sae_wcl_request_auth_owner_state {
	IEEE80211_SAE_WCL_AUTH_OWNER_NONE = 0,
	IEEE80211_SAE_WCL_AUTH_OWNER_READY = 1,
	IEEE80211_SAE_WCL_AUTH_OWNER_REJECTED = -1,
};

/*
 * S_AUTH is the final generic boundary before historic Open-System handling.
 * A live direct-WCL request must be exact and fully driver-owned here.  A
 * missing hook, stale identity, or incomplete handoff is revoked and forces
 * the caller back to SCAN; it cannot fall through to an Open AUTH frame.
 */
static int
ieee80211_sae_wcl_request_auth_owner_state(struct ieee80211com *ic,
    const struct ieee80211_node *ni)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_sae_wcl_request_revocation revocation;
	u_int64_t epoch;
	int state = IEEE80211_SAE_WCL_AUTH_OWNER_NONE;

	if (ic == NULL || ni == NULL || ic->ic_opmode != IEEE80211_M_STA)
		return IEEE80211_SAE_WCL_AUTH_OWNER_NONE;
	lock = ic->ic_pae_selected_bss_lock;
	if (lock == NULL)
		return IEEE80211_SAE_WCL_AUTH_OWNER_NONE;
	bzero(&revocation, sizeof(revocation));
	irq = IOSimpleLockLockDisableInterrupt(lock);
	epoch = __atomic_load_n(&ic->ic_pae_assoc_epoch, __ATOMIC_ACQUIRE);
	if (ic->ic_sae_wcl_request.phase == IEEE80211_SAE_WCL_REQUEST_NONE) {
		if (ic->ic_sae_wcl_request.generation != 0 ||
		    ic->ic_sae_wcl_request.association_epoch != 0) {
			ieee80211_sae_wcl_request_clear_locked(ic, &revocation);
			state = IEEE80211_SAE_WCL_AUTH_OWNER_REJECTED;
		}
	} else if (epoch != 0 &&
	    ic->ic_sae_wcl_request.phase == IEEE80211_SAE_WCL_REQUEST_BOUND &&
	    ic->ic_sae_wcl_request.association_epoch == epoch &&
	    ieee80211_sae_wcl_request_owner_hooks_ready_locked(ic) &&
	    ieee80211_sae_wcl_request_matches_current_locked(ic,
	    &ic->ic_sae_wcl_request, ni, epoch)) {
		state = IEEE80211_SAE_WCL_AUTH_OWNER_READY;
	} else {
		ieee80211_sae_wcl_request_clear_locked(ic, &revocation);
		state = IEEE80211_SAE_WCL_AUTH_OWNER_REJECTED;
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	ieee80211_sae_wcl_request_revocation_deliver(ic, &revocation);
	return state;
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
	struct ieee80211_key bip_key;
	u_int8_t kid;
	u_int16_t bip_kid;
    int error, rekeysta = 0;

	/* Keep the next IGTK off-table until every station has received its KDE.
	 * A table slot can still be validating MMIEs from the previous epoch. */
	if (ic->ic_caps & IEEE80211_C_MFP) {
		bzero(&bip_key, sizeof(bip_key));
		error = ieee80211_bip_next_kid(ic, &bip_kid);
		if (error == 0) {
			bip_key.k_id = bip_kid;
			bip_key.k_cipher = ic->ic_bss->ni_rsngroupmgmtcipher;
			bip_key.k_flags = IEEE80211_KEY_IGTK | IEEE80211_KEY_TX;
			bip_key.k_len = IEEE80211_BIP_KEYLEN;
			arc4random_buf(bip_key.k_key, bip_key.k_len);
			error = ieee80211_bip_pending_stage(ic, &bip_key);
		}
		explicit_bzero(&bip_key, sizeof(bip_key));
		if (error != 0)
			return;
	}

	/* Swap(GM, GN) */
	kid = (ic->ic_def_txkey == 1) ? 2 : 1;
	k = &ic->ic_nw_keys[kid];
	memset(k, 0, sizeof(*k));
	k->k_id = kid;
	k->k_cipher = ic->ic_bss->ni_rsngroupcipher;
	k->k_flags = IEEE80211_KEY_GROUP | IEEE80211_KEY_TX;
	k->k_len = ieee80211_cipher_keylen(k->k_cipher);
	arc4random_buf(k->k_key, k->k_len);

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
	struct ieee80211_key bip_key, retry_key;
	int error;

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
		/* Install the value that was actually advertised in Group Msg1.  The
		 * local BIP context is created outside the leaf lock and only then
		 * atomically replaces its destination slot. */
		bzero(&bip_key, sizeof(bip_key));
		bzero(&retry_key, sizeof(retry_key));
		error = ieee80211_bip_pending_take(ic, &bip_key);
		if (error == 0) {
			retry_key = bip_key;
			retry_key.k_priv = NULL;
			retry_key.k_flags &= ~IEEE80211_KEY_BIP_LOCAL;
			error = ieee80211_bip_key_install_publish(ic, ic->ic_bss,
			    &bip_key);
			if (error != 0)
				(void)ieee80211_bip_pending_restore(ic, &retry_key);
		}
		explicit_bzero(&retry_key, sizeof(retry_key));
		explicit_bzero(&bip_key, sizeof(bip_key));
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
		if (ic->ic_event_handler != NULL)
			(*ic->ic_event_handler)(ic,
			    IEEE80211_EVT_STA_AUTH_DONE, NULL);
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
	struct ieee80211_sae_driver_hook_snapshot sae_hooks;
	enum ieee80211_state ostate;
	int sae_auth_hold;
	int sae_wcl_owner;
#ifndef IEEE80211_STA_ONLY
	int s;
#endif

	ostate = ic->ic_state;
	explicit_bzero(&sae_hooks, sizeof(sae_hooks));
	/* A real state-machine transition supersedes any delayed status-30
	 * association request.  The watchdog retry deliberately sends directly
	 * and therefore retains its retry count until success or failure. */
	ic->ic_assoc_comeback_tu = 0;
	ic->ic_assoc_comeback_pending = 0;
	ic->ic_assoc_comeback_reassoc = 0;
	ic->ic_assoc_comeback_retries = 0;
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
		/*
		 * A selected driver-owned SAE attempt may enter S_AUTH only after
		 * its lower HAL has prepared pre-association RX/TX context.  Give
		 * that owner the first decision after the state has committed and
		 * before any generic Open-System AUTH branch can send.  The normal
		 * management watchdog remains armed; a failed owner must return to
		 * SCAN rather than allowing an Open-System downgrade.
		 */
		sae_wcl_owner = IEEE80211_SAE_WCL_AUTH_OWNER_NONE;
		if (ic->ic_opmode == IEEE80211_M_STA && ni != NULL)
			sae_wcl_owner = ieee80211_sae_wcl_request_auth_owner_state(
			    ic, ni);
		if (sae_wcl_owner == IEEE80211_SAE_WCL_AUTH_OWNER_REJECTED) {
			/* The generic leaf gate already revoked any private driver state
			 * after dropping its lock.  Enter the ordinary cancellation path
			 * rather than sending a historic Open-System AUTH frame. */
			ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
			break;
		}
		sae_auth_hold = 0;
		if (ic->ic_opmode == IEEE80211_M_STA && ni != NULL)
			ieee80211_sae_driver_hook_snapshot_copyout(ic, &sae_hooks);
		if (sae_wcl_owner == IEEE80211_SAE_WCL_AUTH_OWNER_READY) {
			/* A direct-WCL request must be claimed by its prepared driver
			 * owner.  If a late lifecycle change makes hold unavailable, or
			 * that owner declines it, fail closed before the legacy branch. */
			if (sae_hooks.auth_hold != NULL)
				sae_auth_hold = sae_hooks.auth_hold(ic, ni, ostate, mgt);
			if (sae_auth_hold == 0) {
				ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
				break;
			}
		} else if (ic->ic_opmode == IEEE80211_M_STA && ni != NULL &&
		    sae_hooks.auth_hold != NULL) {
			sae_auth_hold = sae_hooks.auth_hold(ic, ni, ostate, mgt);
		}
		explicit_bzero(&sae_hooks, sizeof(sae_hooks));
		if (sae_auth_hold != 0) {
			ic->ic_mgt_timer = IEEE80211_TRANS_WAIT;
			break;
		}
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
		/* Direct SAE reaches Association only through its private PMK
		 * continuation sentinel.  Recheck the exact public claim after the
		 * state has committed and immediately before generic code can enqueue
		 * an Association Request.  In particular, a late S_AUTH replacement
		 * cannot turn a stale Confirm into an on-air association. */
		if (mgt == IEEE80211_SAE_WCL_MGMT_PMK_CONTINUE &&
		    (ostate != IEEE80211_S_AUTH ||
		    ic->ic_opmode != IEEE80211_M_STA || ni == NULL ||
		    !ieee80211_sae_wcl_request_pmk_claim_assoc_sentinel_current(ic,
		    ni))) {
			ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
			break;
		}
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
            /* Tahoe's non-public paths retain their historical S_RUN link
             * publication.  Only the exact public initial-BSS marker waits
             * for port-valid, so it cannot expose DESBSSID before release. */
			if ((ic->ic_flags & IEEE80211_F_RSNON) == 0 ||
			    !ieee80211_public_initial_bssid_pin_should_defer_link_up(
			    ic, ni)) {
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
				if (ic->ic_opmode == IEEE80211_M_STA &&
				    (ic->ic_flags & IEEE80211_F_RSNON) == 0 &&
				    ic->ic_event_handler != NULL)
					(*ic->ic_event_handler)(
					    ic, IEEE80211_EVT_STA_OPEN_RUN_DONE, NULL);
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
