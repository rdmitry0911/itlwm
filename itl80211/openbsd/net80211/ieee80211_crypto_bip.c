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
/*	$OpenBSD: ieee80211_crypto_bip.c,v 1.10 2018/11/09 14:14:31 claudio Exp $	*/

/*-
 * Copyright (c) 2008 Damien Bergamini <damien.bergamini@free.fr>
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
 * This code implements the Broadcast/Multicast Integrity Protocol (BIP)
 * defined in IEEE P802.11w/D7.0 section 8.3.4.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/endian.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_crypto.h>
#include <net80211/ieee80211_priv.h>

#include <ClientKit/AirportItlwmPostPltiTraceBridge.h>

#include <crypto/aes.h>
#include <crypto/cmac.h>

/*
 * The AES schedule is immutable after set-key.  AES_CMAC_CTX is deliberately
 * not stored here: its X/M_last/M_n fields are scratch state and two PMF
 * readers must never share them.  Readers copy aesctx while holding the
 * selected-BSS leaf lock and build their own CMAC state on the stack.
 */
struct ieee80211_bip_ctx {
	AES_CTX		aesctx;
	int		published;
	int		retired;
	TAILQ_ENTRY(ieee80211_bip_ctx) retired_entry;
};

static int
ieee80211_bip_key_shape_valid(const struct ieee80211_key *k)
{
	return k != NULL && (k->k_id == 4 || k->k_id == 5) &&
	    k->k_cipher == IEEE80211_CIPHER_BIP &&
	    (k->k_flags & IEEE80211_KEY_IGTK) != 0 &&
	    k->k_len == IEEE80211_BIP_KEYLEN;
}

int
ieee80211_bip_key_is_slot(const struct ieee80211com *ic,
    const struct ieee80211_key *k)
{
	return ic != NULL && k != NULL &&
	    (k == &ic->ic_nw_keys[4] || k == &ic->ic_nw_keys[5]);
}

static void
ieee80211_bip_ctx_free(struct ieee80211_bip_ctx *ctx)
{
	if (ctx == NULL)
		return;
	explicit_bzero(ctx, sizeof(*ctx));
	free(ctx);
}

/* Caller holds ic_pae_selected_bss_lock. */
static int
ieee80211_bip_ctx_live_locked(struct ieee80211com *ic,
    struct ieee80211_key *k, struct ieee80211_bip_ctx **out)
{
	struct ieee80211_bip_ctx *ctx;

	if (!ieee80211_bip_key_is_slot(ic, k) ||
	    !ieee80211_bip_key_shape_valid(k))
		return 0;
	ctx = (struct ieee80211_bip_ctx *)k->k_priv;
	if (ctx == NULL || !ctx->published || ctx->retired)
		return 0;
	*out = ctx;
	return 1;
}

/*
 * Caller holds ic_pae_selected_bss_lock after a successful backend-owned
 * handoff.  This intentionally has no public/raw-descriptor entry point:
 * publication and the TX selector must already be coherent before it emits
 * the two categorical facts.  In particular, prepared contexts, failed
 * backend commands, and table probes never reach this helper.
 */
static void
ieee80211_bip_trace_publish_locked(struct ieee80211com *ic,
    struct ieee80211_key *slot)
{
	struct ieee80211_bip_ctx *ctx;

	if (ic == NULL || slot == NULL ||
	    !ieee80211_bip_ctx_live_locked(ic, slot, &ctx) ||
	    ic->ic_igtk_kid != slot->k_id)
		return;
	if (slot->k_id != 4 && slot->k_id != 5)
		return;

	/* The bridge uses one fixed slot category to choose the armed backend's
	 * pair under one recorder admission.  `ctx` is used solely for the live
	 * slot ownership check above. */
	(void)ctx;
	AirportItlwmPostPltiTraceRecordIgtkPublicationSelection(ic, slot->k_id);
}

/* Caller holds ic_pae_selected_bss_lock.  No allocation or free is allowed. */
static void
ieee80211_bip_ctx_retire_locked(struct ieee80211com *ic,
    struct ieee80211_bip_ctx *ctx)
{
	_KASSERT(ctx != NULL);
	_KASSERT(ctx->published);
	_KASSERT(!ctx->retired);
	ctx->published = 0;
	ctx->retired = 1;
	TAILQ_INSERT_TAIL(&ic->ic_bip_retired, ctx, retired_entry);
}

static void
ieee80211_bip_reap_timeout(void *arg)
{
	/* Net80211 is compiled as C++ by the Tahoe target; keep the timeout
	 * callback boundary explicit rather than relying on C's void * conversion. */
	struct ieee80211com *ic = (struct ieee80211com *)arg;

	if (ic == NULL || ic->ic_bip_reap_to == NULL)
		return;
	/* This runs from the timeout work context, never from a PMF reader exit.
	 * A reader may still own an old context, in which case retry shortly. */
	if (ieee80211_bip_reap(ic) == EBUSY && ic->ic_bip_reap_to != NULL)
		timeout_add_msec(&ic->ic_bip_reap_to, 10);
}

/* Call only after dropping ic_pae_selected_bss_lock. */
void
ieee80211_bip_reap_schedule(struct ieee80211com *ic)
{
	if (ic != NULL && ic->ic_bip_reap_to != NULL)
		timeout_add_msec(&ic->ic_bip_reap_to, 10);
}

void
ieee80211_bip_crypto_attach(struct ieee80211com *ic)
{
	if (ic == NULL)
		return;
	TAILQ_INIT(&ic->ic_bip_retired);
	__atomic_store_n(&ic->ic_bip_readers, 0, __ATOMIC_RELEASE);
	bzero(&ic->ic_bip_pending_key, sizeof(ic->ic_bip_pending_key));
	ic->ic_bip_pending_valid = 0;
	timeout_set(&ic->ic_bip_reap_to, ieee80211_bip_reap_timeout, ic);
}

void
ieee80211_bip_crypto_detach(struct ieee80211com *ic)
{
	IOSimpleLock *lock;
	IOInterruptState irq;

	if (ic == NULL)
		return;
	/* No timeout can race final driver free.  Terminal drain remains
	 * fail-closed if a reader has not left by then. */
	if (ic->ic_bip_reap_to != NULL) {
		timeout_del(&ic->ic_bip_reap_to);
		timeout_free(&ic->ic_bip_reap_to);
	}
	/* An in-flight reader is a terminal-lifetime error, not a reason to free
	 * from its exit path.  The final HAL owner checks it before lock destroy. */
	(void)ieee80211_bip_reap(ic);
	/* Pending HostAP key material never owns a BIP context, but it is still
	 * secret and is serialized with the live descriptor state when possible. */
	if ((lock = ic->ic_pae_selected_bss_lock) != NULL) {
		irq = IOSimpleLockLockDisableInterrupt(lock);
		explicit_bzero(&ic->ic_bip_pending_key,
		    sizeof(ic->ic_bip_pending_key));
		ic->ic_bip_pending_valid = 0;
		IOSimpleLockUnlockEnableInterrupt(lock, irq);
	} else {
		explicit_bzero(&ic->ic_bip_pending_key,
		    sizeof(ic->ic_bip_pending_key));
		ic->ic_bip_pending_valid = 0;
	}
}

/*
 * Initialize a private BIP context.  The local-owner marker makes generic
 * delete safe for transaction cancellation: published slots never carry it,
 * and a value copy handed to a driver callback never owns k_priv.
 */
int
ieee80211_bip_set_key(struct ieee80211com *ic, struct ieee80211_key *k)
{
	struct ieee80211_bip_ctx *ctx;

	/* A table slot is never a staging object: publishing it would make a
	 * local-owner context reachable before the descriptor is coherent. */
	if (ieee80211_bip_key_is_slot(ic, k) ||
	    !ieee80211_bip_key_shape_valid(k) || k->k_priv != NULL)
		return EINVAL;
	ctx = (struct ieee80211_bip_ctx *)malloc(sizeof(*ctx), 0, 0);
	if (ctx == NULL)
		return ENOMEM;
	bzero(ctx, sizeof(*ctx));
	AES_Setkey(&ctx->aesctx, k->k_key, IEEE80211_BIP_KEYLEN);
	k->k_priv = ctx;
	k->k_flags |= IEEE80211_KEY_BIP_LOCAL;
	return 0;
}

void
ieee80211_bip_delete_key(struct ieee80211com *ic, struct ieee80211_key *k)
{
	struct ieee80211_bip_ctx *ctx;

	if (k == NULL)
		return;
	/* Generic delete must unpublish a live table descriptor first.  A direct
	 * BIP delete here would lose its retired context, so fail closed. */
	if (ieee80211_bip_key_is_slot(ic, k))
		panic("ieee80211_bip_delete_key live slot");
	/* A table slot and a value-only callback copy do not own the context. */
	if ((k->k_flags & IEEE80211_KEY_BIP_LOCAL) == 0) {
		k->k_priv = NULL;
		return;
	}
	ctx = (struct ieee80211_bip_ctx *)k->k_priv;
	k->k_priv = NULL;
	k->k_flags &= ~IEEE80211_KEY_BIP_LOCAL;
	if (ctx != NULL && !ctx->published && !ctx->retired)
		ieee80211_bip_ctx_free(ctx);
}

/* Caller holds ic_pae_selected_bss_lock. */
int
ieee80211_bip_key_publish_retire_locked(struct ieee80211com *ic,
    struct ieee80211_key *slot, struct ieee80211_key *new_key)
{
	struct ieee80211_bip_ctx *newctx, *oldctx = NULL;

	if (ic == NULL || slot == NULL || new_key == NULL ||
	    !ieee80211_bip_key_shape_valid(new_key) ||
	    slot != &ic->ic_nw_keys[new_key->k_id])
		return EINVAL;
	newctx = (struct ieee80211_bip_ctx *)new_key->k_priv;
	if (newctx == NULL || (new_key->k_flags & IEEE80211_KEY_BIP_LOCAL) == 0 ||
	    newctx->published || newctx->retired)
		return EBUSY;
	if (slot->k_cipher != IEEE80211_CIPHER_NONE) {
		if (!ieee80211_bip_key_shape_valid(slot))
			return EBUSY;
		oldctx = (struct ieee80211_bip_ctx *)slot->k_priv;
		if (oldctx == NULL || !oldctx->published || oldctx->retired ||
		    oldctx == newctx)
			return EBUSY;
	}

	/* This is the only descriptor publication.  The leaf lock makes every
	 * following assignment visible together; readers may only acquire after it
	 * and either see newctx as live or reject the slot. */
	*slot = *new_key;
	slot->k_flags &= ~IEEE80211_KEY_BIP_LOCAL;
	newctx->published = 1;
	newctx->retired = 0;
	new_key->k_priv = NULL;
	new_key->k_flags &= ~IEEE80211_KEY_BIP_LOCAL;
	ic->ic_igtk_kid = slot->k_id;
	ieee80211_bip_trace_publish_locked(ic, slot);
	if (oldctx != NULL)
		ieee80211_bip_ctx_retire_locked(ic, oldctx);
	return 0;
}

static void
ieee80211_bip_key_value_copy(struct ieee80211_key *out,
    const struct ieee80211_key *in)
{
	*out = *in;
	out->k_priv = NULL;
	out->k_flags &= ~IEEE80211_KEY_BIP_LOCAL;
}

int
ieee80211_bip_key_publish_retire(struct ieee80211com *ic,
    struct ieee80211_key *slot, struct ieee80211_key *new_key)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	int error;

	if (ic == NULL || slot == NULL || new_key == NULL ||
	    (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return EINVAL;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	error = ieee80211_bip_key_publish_retire_locked(ic, slot, new_key);
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	if (error == 0)
		ieee80211_bip_reap_schedule(ic);
	return error;
}

int
ieee80211_bip_key_install_publish(struct ieee80211com *ic,
    struct ieee80211_node *ni, struct ieee80211_key *new_key)
{
	int error;

	if (ic == NULL || new_key == NULL ||
	    !ieee80211_bip_key_shape_valid(new_key) ||
	    ieee80211_bip_key_is_slot(ic, new_key) || ic->ic_set_key == NULL)
		return EINVAL;
	error = (*ic->ic_set_key)(ic, ni, new_key);
	if (error != 0)
		return error;
	if (new_key->k_priv == NULL ||
	    (new_key->k_flags & IEEE80211_KEY_BIP_LOCAL) == 0) {
		if (ic->ic_delete_key != NULL)
			(*ic->ic_delete_key)(ic, ni, new_key);
		else
			ieee80211_delete_key(ic, ni, new_key);
		return EIO;
	}
	error = ieee80211_bip_key_publish_retire(ic,
	    &ic->ic_nw_keys[new_key->k_id], new_key);
	if (error != 0) {
		/* The driver received only a local descriptor.  Undo it rather than
		 * making a stale or half-installed context reachable from the table. */
		if (ic->ic_delete_key != NULL)
			(*ic->ic_delete_key)(ic, ni, new_key);
		else
			ieee80211_delete_key(ic, ni, new_key);
	}
	return error;
}

int
ieee80211_bip_key_snapshot(struct ieee80211com *ic,
    const struct ieee80211_key *slot, struct ieee80211_key *out)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_bip_ctx *ctx;
	int error = EINVAL;

	if (ic == NULL || slot == NULL || out == NULL ||
	    !ieee80211_bip_key_is_slot(ic, slot) ||
	    (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return EINVAL;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	if (ieee80211_bip_ctx_live_locked(ic, (struct ieee80211_key *)slot,
	    &ctx)) {
		ieee80211_bip_key_value_copy(out, slot);
		error = 0;
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return error;
}

int
ieee80211_bip_key_slot_live(struct ieee80211com *ic, u_int16_t kid)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_bip_ctx *ctx;
	int live = 0;

	if (ic == NULL || (kid != 4 && kid != 5) ||
	    (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return 0;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	live = ieee80211_bip_ctx_live_locked(ic, &ic->ic_nw_keys[kid], &ctx);
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return live;
}

int
ieee80211_bip_active_key_snapshot(struct ieee80211com *ic,
    struct ieee80211_key *out)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_key *slot;
	struct ieee80211_bip_ctx *ctx;
	int error = EINVAL;

	if (ic == NULL || out == NULL ||
	    (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return EINVAL;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	if ((ic->ic_igtk_kid == 4 || ic->ic_igtk_kid == 5)) {
		slot = &ic->ic_nw_keys[ic->ic_igtk_kid];
		if (ieee80211_bip_ctx_live_locked(ic, slot, &ctx)) {
			ieee80211_bip_key_value_copy(out, slot);
			error = 0;
		}
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return error;
}

struct ieee80211_key *
ieee80211_bip_active_slot(struct ieee80211com *ic)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	u_int16_t kid = 4;

	if (ic == NULL)
		return NULL;
	if ((lock = ic->ic_pae_selected_bss_lock) == NULL)
		return &ic->ic_nw_keys[kid];
	irq = IOSimpleLockLockDisableInterrupt(lock);
	if (ic->ic_igtk_kid == 4 || ic->ic_igtk_kid == 5)
		kid = ic->ic_igtk_kid;
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return &ic->ic_nw_keys[kid];
}

int
ieee80211_bip_key_needs_update(struct ieee80211com *ic, u_int16_t kid,
    const u_int8_t *key, u_int len)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_key *slot;
	struct ieee80211_bip_ctx *ctx;
	int update = 1;

	if (ic == NULL || key == NULL || len != IEEE80211_BIP_KEYLEN ||
	    (kid != 4 && kid != 5) ||
	    (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return -1;
	slot = &ic->ic_nw_keys[kid];
	irq = IOSimpleLockLockDisableInterrupt(lock);
	if (ieee80211_bip_ctx_live_locked(ic, slot, &ctx))
		update = slot->k_len != len ||
		    memcmp(slot->k_key, key, len) != 0;
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return update;
}

int
ieee80211_bip_next_kid(struct ieee80211com *ic, u_int16_t *kidp)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	u_int16_t kid = 4;

	if (ic == NULL || kidp == NULL ||
	    (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return EINVAL;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	if (ic->ic_igtk_kid == 4)
		kid = 5;
	else if (ic->ic_igtk_kid == 5)
		kid = 4;
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	*kidp = kid;
	return 0;
}

int
ieee80211_bip_pending_stage(struct ieee80211com *ic,
    const struct ieee80211_key *key)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	int error = 0;

	if (ic == NULL || !ieee80211_bip_key_shape_valid(key) ||
	    key->k_priv != NULL || (key->k_flags & IEEE80211_KEY_BIP_LOCAL) != 0 ||
	    (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return EINVAL;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	if (ic->ic_bip_pending_valid)
		error = EBUSY;
	else {
		ieee80211_bip_key_value_copy(&ic->ic_bip_pending_key, key);
		ic->ic_bip_pending_valid = 1;
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return error;
}

int
ieee80211_bip_pending_snapshot(struct ieee80211com *ic,
    struct ieee80211_key *out)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	int error = ENOENT;

	if (ic == NULL || out == NULL ||
	    (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return EINVAL;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	if (ic->ic_bip_pending_valid &&
	    ieee80211_bip_key_shape_valid(&ic->ic_bip_pending_key)) {
		ieee80211_bip_key_value_copy(out, &ic->ic_bip_pending_key);
		error = 0;
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return error;
}

int
ieee80211_bip_pending_take(struct ieee80211com *ic,
    struct ieee80211_key *out)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	int error = ENOENT;

	if (ic == NULL || out == NULL ||
	    (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return EINVAL;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	if (ic->ic_bip_pending_valid &&
	    ieee80211_bip_key_shape_valid(&ic->ic_bip_pending_key)) {
		ieee80211_bip_key_value_copy(out, &ic->ic_bip_pending_key);
		explicit_bzero(&ic->ic_bip_pending_key,
		    sizeof(ic->ic_bip_pending_key));
		ic->ic_bip_pending_valid = 0;
		error = 0;
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return error;
}

int
ieee80211_bip_pending_restore(struct ieee80211com *ic,
    const struct ieee80211_key *key)
{
	return ieee80211_bip_pending_stage(ic, key);
}

int
ieee80211_bip_key_unpublish_retire(struct ieee80211com *ic,
    struct ieee80211_key *slot, struct ieee80211_key *out)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_bip_ctx *ctx, *otherctx, *local = NULL;
	struct ieee80211_key *other;
	u_int16_t oldkid;
	int error = 0;

	if (ic == NULL || slot == NULL || out == NULL ||
	    !ieee80211_bip_key_is_slot(ic, slot) ||
	    (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return EINVAL;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	if (slot->k_cipher != IEEE80211_CIPHER_NONE &&
	    !ieee80211_bip_key_shape_valid(slot)) {
		error = EBUSY;
		goto out;
	}
	ctx = (struct ieee80211_bip_ctx *)slot->k_priv;
	if (ctx != NULL) {
		if (ctx->retired) {
			error = EBUSY;
			goto out;
		}
		if (ctx->published)
			ieee80211_bip_ctx_retire_locked(ic, ctx);
		else if (slot->k_flags & IEEE80211_KEY_BIP_LOCAL)
			local = ctx;
		else {
			error = EBUSY;
			goto out;
		}
	}
	oldkid = slot->k_id;
	ieee80211_bip_key_value_copy(out, slot);
	bzero(slot, sizeof(*slot));
	/* TX has one selected IGTK, while RX may retain the other slot through a
	 * rekey overlap.  If this unpublishes TX's slot, select the still-live
	 * opposite slot or a safe inactive id that the BIP reader will reject. */
	if (ic->ic_igtk_kid == oldkid) {
		other = &ic->ic_nw_keys[(oldkid == 4) ? 5 : 4];
		if (ieee80211_bip_ctx_live_locked(ic, other, &otherctx))
			ic->ic_igtk_kid = other->k_id;
		else
			ic->ic_igtk_kid = 4;
	} else if (ic->ic_igtk_kid != 4 && ic->ic_igtk_kid != 5) {
		ic->ic_igtk_kid = 4;
	}
out:
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	if (local != NULL)
		ieee80211_bip_ctx_free(local);
	if (error == 0)
		ieee80211_bip_reap_schedule(ic);
	return error;
}

/*
 * A claim starts before k_priv is read.  The returned pointer is an opaque
 * identity only: after leaving the leaf lock no reader dereferences it.  The
 * copied AES schedule is independent of mutable AES_CMAC_CTX scratch state.
 */
static struct ieee80211_bip_ctx *
ieee80211_bip_ctx_hold_tx(struct ieee80211com *ic, struct ieee80211_key *k,
    AES_CTX *aesctx, u_int16_t *kid, u_int64_t *tsc)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_bip_ctx *ctx = NULL;

	if (ic == NULL || k == NULL || aesctx == NULL || kid == NULL ||
	    tsc == NULL)
		return NULL;
	__atomic_fetch_add(&ic->ic_bip_readers, 1, __ATOMIC_ACQUIRE);
	lock = ic->ic_pae_selected_bss_lock;
	if (lock != NULL) {
		irq = IOSimpleLockLockDisableInterrupt(lock);
		if (ieee80211_bip_ctx_live_locked(ic, k, &ctx) &&
		    ic->ic_igtk_kid == k->k_id) {
			*aesctx = ctx->aesctx;
			*kid = k->k_id;
			*tsc = k->k_tsc++;
		} else
			/* A preserved RX-only slot is never a TX claim. */
			ctx = NULL;
		IOSimpleLockUnlockEnableInterrupt(lock, irq);
	}
	if (ctx == NULL)
		__atomic_fetch_sub(&ic->ic_bip_readers, 1, __ATOMIC_RELEASE);
	return ctx;
}

static struct ieee80211_bip_ctx *
ieee80211_bip_ctx_hold_rx(struct ieee80211com *ic, struct ieee80211_key *k,
    AES_CTX *aesctx, u_int64_t *mgmt_rsc)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_bip_ctx *ctx = NULL;

	if (ic == NULL || k == NULL || aesctx == NULL || mgmt_rsc == NULL)
		return NULL;
	__atomic_fetch_add(&ic->ic_bip_readers, 1, __ATOMIC_ACQUIRE);
	lock = ic->ic_pae_selected_bss_lock;
	if (lock != NULL) {
		irq = IOSimpleLockLockDisableInterrupt(lock);
		if (ieee80211_bip_ctx_live_locked(ic, k, &ctx)) {
			*aesctx = ctx->aesctx;
			*mgmt_rsc = k->k_mgmt_rsc;
		}
		IOSimpleLockUnlockEnableInterrupt(lock, irq);
	}
	if (ctx == NULL)
		__atomic_fetch_sub(&ic->ic_bip_readers, 1, __ATOMIC_RELEASE);
	return ctx;
}

static void
ieee80211_bip_ctx_put(struct ieee80211com *ic, struct ieee80211_bip_ctx *ctx)
{
	u_int32_t old;

	if (ic == NULL || ctx == NULL)
		return;
	old = __atomic_fetch_sub(&ic->ic_bip_readers, 1, __ATOMIC_RELEASE);
	_KASSERT(old != 0);
}

/* Caller still holds a reader claim. */
static int
ieee80211_bip_tx_commit(struct ieee80211com *ic, struct ieee80211_key *k,
    struct ieee80211_bip_ctx *ctx, u_int16_t kid)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_bip_ctx *current = NULL;
	int live = 0;

	if (ic == NULL || k == NULL || ctx == NULL ||
	    (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return 0;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	/* RX may continue to use an old slot during a 4<->5 handover, but TX
	 * cannot emit an MMIE from a claim whose selected key changed mid-CMAC. */
	live = ieee80211_bip_ctx_live_locked(ic, k, &current) && current == ctx &&
	    ic->ic_igtk_kid == kid;
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return live;
}

/* Caller still holds a reader claim. */
static int
ieee80211_bip_rx_commit(struct ieee80211com *ic, struct ieee80211_key *k,
    struct ieee80211_bip_ctx *ctx, u_int64_t ipn)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_bip_ctx *current = NULL;
	int accept = 0;

	if (ic == NULL || k == NULL || ctx == NULL ||
	    (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return 0;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	if (ieee80211_bip_ctx_live_locked(ic, k, &current) && current == ctx &&
	    ipn > k->k_mgmt_rsc) {
		k->k_mgmt_rsc = ipn;
		accept = 1;
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return accept;
}

int
ieee80211_bip_reap(struct ieee80211com *ic)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_bip_retired_head retired;
	struct ieee80211_bip_ctx *ctx;

	if (ic == NULL)
		return EINVAL;
	/* An attach failure can final-release the driver before ifattach() ever
	 * allocated the leaf lock.  No BIP reader or retirement list could then
	 * have become reachable, so this is an intentional no-op. */
	if ((lock = ic->ic_pae_selected_bss_lock) == NULL)
		return 0;
	TAILQ_INIT(&retired);
	irq = IOSimpleLockLockDisableInterrupt(lock);
	if (__atomic_load_n(&ic->ic_bip_readers, __ATOMIC_ACQUIRE) != 0) {
		IOSimpleLockUnlockEnableInterrupt(lock, irq);
		return EBUSY;
	}
	while ((ctx = TAILQ_FIRST(&ic->ic_bip_retired)) != NULL) {
		TAILQ_REMOVE(&ic->ic_bip_retired, ctx, retired_entry);
		TAILQ_INSERT_TAIL(&retired, ctx, retired_entry);
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	while ((ctx = TAILQ_FIRST(&retired)) != NULL) {
		TAILQ_REMOVE(&retired, ctx, retired_entry);
		ieee80211_bip_ctx_free(ctx);
	}
	return 0;
}

int
ieee80211_bip_lifetime_drain(struct ieee80211com *ic)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_key *slot4, *slot5;
	int error;

	if (ic == NULL)
		return EINVAL;
	/* An early attach unwind has no selected-BSS lock and therefore cannot
	 * have published a BIP reader/context.  Final free is a no-op in that
	 * one state; once the lock exists, any remaining reader stays fatal. */
	if ((lock = ic->ic_pae_selected_bss_lock) == NULL)
		return 0;
	error = ieee80211_bip_reap(ic);
	if (error != 0)
		return error;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	slot4 = &ic->ic_nw_keys[4];
	slot5 = &ic->ic_nw_keys[5];
	error = __atomic_load_n(&ic->ic_bip_readers, __ATOMIC_ACQUIRE) != 0 ||
	    TAILQ_FIRST(&ic->ic_bip_retired) != NULL ||
	    slot4->k_cipher != IEEE80211_CIPHER_NONE || slot4->k_priv != NULL ||
	    slot5->k_cipher != IEEE80211_CIPHER_NONE || slot5->k_priv != NULL ||
	    ic->ic_bip_pending_valid ? EBUSY : 0;
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return error;
}

/* pseudo-header used for BIP MIC computation */
struct ieee80211_bip_frame {
	u_int8_t	i_fc[2];
	u_int8_t	i_addr1[IEEE80211_ADDR_LEN];
	u_int8_t	i_addr2[IEEE80211_ADDR_LEN];
	u_int8_t	i_addr3[IEEE80211_ADDR_LEN];
} __packed;

mbuf_t
ieee80211_bip_encap(struct ieee80211com *ic, mbuf_t m0,
    struct ieee80211_key *k)
{
	struct ieee80211_bip_frame aad;
	struct ieee80211_frame *wh;
	struct ieee80211_bip_ctx *ctx;
	AES_CTX aesctx;
	AES_CMAC_CTX cmac;
	u_int8_t *mmie, mic[AES_CMAC_DIGEST_LENGTH];
	u_int16_t kid;
	u_int64_t tsc;
	mbuf_t m;
    mbuf_t temp;

	bzero(&aesctx, sizeof(aesctx));
	bzero(&cmac, sizeof(cmac));
	if (m0 == NULL || k == NULL)
		return NULL;
	if (mbuf_len(m0) < sizeof(*wh))
		goto drop;
	wh = mtod(m0, struct ieee80211_frame *);
	/* BIP is exclusively a multicast management protection primitive.  A
	 * selector bug must discard rather than trip a kernel assertion. */
	if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) !=
	    IEEE80211_FC0_TYPE_MGT || !IEEE80211_IS_MULTICAST(wh->i_addr1))
		goto drop;
	/* Reserve before claiming a key so an allocation failure cannot consume an
	 * IGTK packet number. */
	m = m0;
	if (mbuf_trailingspace(m) < IEEE80211_MMIE_LEN) {
        temp = NULL;
        mbuf_mclget(MBUF_DONTWAIT, mbuf_type(m), &temp);
		if (temp == NULL)
			goto nospace;
        mbuf_setnext(m, temp);
		m = temp;
        mbuf_setlen(m, 0);
	}

	/* clear Protected bit from group management frames */
	wh->i_fc[1] &= ~IEEE80211_FC1_PROTECTED;

	/* construct AAD (additional authenticated data) */
	aad.i_fc[0] = wh->i_fc[0];
	aad.i_fc[1] = wh->i_fc[1] & ~(IEEE80211_FC1_RETRY |
	    IEEE80211_FC1_PWR_MGT | IEEE80211_FC1_MORE_DATA);
	/* XXX 11n may require clearing the Order bit too */
	IEEE80211_ADDR_COPY(aad.i_addr1, wh->i_addr1);
	IEEE80211_ADDR_COPY(aad.i_addr2, wh->i_addr2);
	IEEE80211_ADDR_COPY(aad.i_addr3, wh->i_addr3);

	ctx = ieee80211_bip_ctx_hold_tx(ic, k, &aesctx, &kid, &tsc);
	if (ctx == NULL)
		goto drop;

	/* construct Management MIC IE */
	mmie = mtod(m, u_int8_t *) + mbuf_len(m);
	mmie[0] = IEEE80211_ELEMID_MMIE;
	mmie[1] = 16;
	LE_WRITE_2(&mmie[2], kid);
	LE_WRITE_6(&mmie[4], tsc);
	memset(&mmie[10], 0, 8);	/* MMIE MIC field set to 0 */

	cmac.aesctx = aesctx;
	AES_CMAC_Init(&cmac);
	AES_CMAC_Update(&cmac, (u_int8_t *)&aad, sizeof aad);
	AES_CMAC_Update(&cmac, (u_int8_t *)&wh[1],
	    (u_int)(mbuf_len(m0) - sizeof(*wh)));
	AES_CMAC_Update(&cmac, mmie, IEEE80211_MMIE_LEN);
	AES_CMAC_Final(mic, &cmac);
	/* truncate AES-128-CMAC to 64-bit */
	memcpy(&mmie[10], mic, 8);

    mbuf_setlen(m, mbuf_len(m) + IEEE80211_MMIE_LEN);
    mbuf_pkthdr_setlen(m0, mbuf_pkthdr_len(m0) + IEEE80211_MMIE_LEN);

	/* Do not emit a frame signed with an IGTK that was replaced while CMAC
	 * ran.  Its reserved PN may be skipped safely. */
	if (!ieee80211_bip_tx_commit(ic, k, ctx, kid))
		goto drop_claim;
	ieee80211_bip_ctx_put(ic, ctx);
	explicit_bzero(&cmac, sizeof(cmac));
	explicit_bzero(&aesctx, sizeof(aesctx));
	return m0;
drop_claim:
	ieee80211_bip_ctx_put(ic, ctx);
drop:
	explicit_bzero(&cmac, sizeof(cmac));
	explicit_bzero(&aesctx, sizeof(aesctx));
	mbuf_freem(m0);
	return NULL;
nospace:
	ic->ic_stats.is_tx_nombuf++;
	mbuf_freem(m0);
	return NULL;
}

mbuf_t
ieee80211_bip_decap(struct ieee80211com *ic, mbuf_t m0,
    struct ieee80211_key *k)
{
	struct ieee80211_frame *wh;
	struct ieee80211_bip_frame aad;
	struct ieee80211_bip_ctx *ctx;
	AES_CTX aesctx;
	AES_CMAC_CTX cmac;
	u_int8_t *mmie, mic0[8], mic[AES_CMAC_DIGEST_LENGTH];
	u_int64_t ipn, mgmt_rsc;

	bzero(&aesctx, sizeof(aesctx));
	bzero(&cmac, sizeof(cmac));
	if (m0 == NULL || k == NULL)
		return NULL;
	if (mbuf_len(m0) < sizeof(*wh) + IEEE80211_MMIE_LEN)
		goto drop;
	wh = mtod(m0, struct ieee80211_frame *);
	if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) !=
	    IEEE80211_FC0_TYPE_MGT || !IEEE80211_IS_MULTICAST(wh->i_addr1))
		goto drop;

	/*
	 * It is assumed that management frames are contiguous and that
	 * the mbuf length has already been checked to contain at least
	 * a header and a MMIE (checked in ieee80211_decrypt()).
	 */
	mmie = mtod(m0, u_int8_t *) + mbuf_len(m0) - IEEE80211_MMIE_LEN;

	ipn = LE_READ_6(&mmie[4]);
	ctx = ieee80211_bip_ctx_hold_rx(ic, k, &aesctx, &mgmt_rsc);
	if (ctx == NULL)
		goto drop;
	if (ipn <= mgmt_rsc) {
		/* replayed frame, discard */
		ic->ic_stats.is_cmac_replays++;
		goto drop_claim;
	}

	/* save and mask MMIE MIC field to 0 */
	memcpy(mic0, &mmie[10], 8);
	memset(&mmie[10], 0, 8);

	/* construct AAD (additional authenticated data) */
	aad.i_fc[0] = wh->i_fc[0];
	aad.i_fc[1] = wh->i_fc[1] & ~(IEEE80211_FC1_RETRY |
	    IEEE80211_FC1_PWR_MGT | IEEE80211_FC1_MORE_DATA);
	/* XXX 11n may require clearing the Order bit too */
	IEEE80211_ADDR_COPY(aad.i_addr1, wh->i_addr1);
	IEEE80211_ADDR_COPY(aad.i_addr2, wh->i_addr2);
	IEEE80211_ADDR_COPY(aad.i_addr3, wh->i_addr3);

	/* compute MIC */
	cmac.aesctx = aesctx;
	AES_CMAC_Init(&cmac);
	AES_CMAC_Update(&cmac, (u_int8_t *)&aad, sizeof aad);
	AES_CMAC_Update(&cmac, (u_int8_t *)&wh[1],
	    (u_int)(mbuf_len(m0) - sizeof(*wh)));
	AES_CMAC_Final(mic, &cmac);

	/* check that MIC matches the one in MMIE */
	if (timingsafe_bcmp(mic, mic0, 8) != 0) {
		ic->ic_stats.is_cmac_icv_errs++;
		goto drop_claim;
	}
	/* The replay check is repeated with the same context under the leaf lock;
	 * a concurrent RX or a rekey therefore cannot roll the counter back. */
	if (!ieee80211_bip_rx_commit(ic, k, ctx, ipn)) {
		ic->ic_stats.is_cmac_replays++;
		goto drop_claim;
	}
	/*
	 * There is no need to trim the MMIE from the mbuf since it is
	 * an information element and will be ignored by upper layers.
	 * We do it anyway as it is cheap to do it here and because it
	 * may be confused with fixed fields by upper layers.
	 */
	mbuf_adj(m0, -IEEE80211_MMIE_LEN);
	ieee80211_bip_ctx_put(ic, ctx);
	explicit_bzero(&cmac, sizeof(cmac));
	explicit_bzero(&aesctx, sizeof(aesctx));
	return m0;
drop_claim:
	ieee80211_bip_ctx_put(ic, ctx);
drop:
	explicit_bzero(&cmac, sizeof(cmac));
	explicit_bzero(&aesctx, sizeof(aesctx));
	mbuf_freem(m0);
	return NULL;
}
