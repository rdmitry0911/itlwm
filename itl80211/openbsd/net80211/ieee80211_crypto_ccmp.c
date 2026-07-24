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
/*	$OpenBSD: ieee80211_crypto_ccmp.c,v 1.21 2018/11/09 14:14:31 claudio Exp $	*/

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
 * This code implements the CTR with CBC-MAC protocol (CCMP) defined in
 * IEEE Std 802.11-2007 section 8.3.3.
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

#include <crypto/aes.h>

/* CCMP software crypto context */
struct ieee80211_ccmp_ctx {
	AES_CTX		aesctx;
	u_int64_t	publication_generation;
	int		retired;
	TAILQ_ENTRY(ieee80211_ccmp_ctx) retired_entry;
};

static void
ieee80211_ccmp_ctx_free(struct ieee80211_ccmp_ctx *ctx)
{
	if (ctx == NULL)
		return;
	explicit_bzero(ctx, sizeof(*ctx));
	free(ctx);
}

/* Caller holds ic_pae_selected_bss_lock. */
static int
ieee80211_ccmp_key_live_locked(struct ieee80211com *ic,
    const struct ieee80211_key *key, struct ieee80211_ccmp_ctx **out)
{
	struct ieee80211_ccmp_ctx *ctx;

	if (ic == NULL || key == NULL || out == NULL ||
	    (key->k_flags & (IEEE80211_KEY_PAE_MFP_LIVE |
	    IEEE80211_KEY_SWCRYPTO)) !=
	    (IEEE80211_KEY_PAE_MFP_LIVE | IEEE80211_KEY_SWCRYPTO) ||
	    key->k_cipher != IEEE80211_CIPHER_CCMP || key->k_priv == NULL)
		return 0;
	ctx = (struct ieee80211_ccmp_ctx *)key->k_priv;
	if (ctx->retired || ctx->publication_generation == 0)
		return 0;
	*out = ctx;
	return 1;
}

void
ieee80211_ccmp_crypto_attach(struct ieee80211com *ic)
{
	if (ic != NULL) {
		TAILQ_INIT(&ic->ic_ccmp_retired);
		ic->ic_ccmp_next_generation = 0;
	}
}

void
ieee80211_ccmp_crypto_detach(struct ieee80211com *ic)
{
	/* The BSS node survives crypto_detach() and owns a possible live PTK.
	 * Reap only contexts already removed from a descriptor; final ownership is
	 * checked after node teardown by ieee80211_ccmp_lifetime_drain(). */
	if (ic != NULL)
		(void)ieee80211_ccmp_reap(ic);
}

int
ieee80211_ccmp_key_publishable_locked(struct ieee80211com *ic,
    const struct ieee80211_key *slot, const struct ieee80211_key *new_key)
{
	struct ieee80211_ccmp_ctx *ctx;

	if (ic == NULL || slot == NULL || new_key == NULL ||
	    new_key->k_cipher != IEEE80211_CIPHER_CCMP ||
	    (new_key->k_flags & IEEE80211_KEY_SWCRYPTO) == 0 ||
	    new_key->k_priv == NULL)
		return EINVAL;
	ctx = (struct ieee80211_ccmp_ctx *)new_key->k_priv;
	if (ctx->retired || ctx->publication_generation != 0 ||
	    (new_key->k_flags & IEEE80211_KEY_PAE_MFP_LIVE) != 0)
		return EBUSY;
	/* A hardware descriptor has no local context and can be replaced by the
	 * negotiated software owner.  A pre-existing unmanaged software context
	 * cannot: freeing or overwriting it would have no reader-lifetime proof. */
	if (slot->k_priv != NULL &&
	    !ieee80211_ccmp_key_live_locked(ic, slot, &ctx))
		return EBUSY;
	return 0;
}

int
ieee80211_ccmp_key_publish_retire_locked(struct ieee80211com *ic,
    struct ieee80211_key *slot, struct ieee80211_key *new_key)
{
	struct ieee80211_ccmp_ctx *newctx, *oldctx = NULL;
	int error;

	error = ieee80211_ccmp_key_publishable_locked(ic, slot, new_key);
	if (error != 0)
		return error;
	if (slot->k_priv != NULL) {
		if (!ieee80211_ccmp_key_live_locked(ic, slot, &oldctx))
			return EBUSY;
		if (oldctx == new_key->k_priv)
			return EBUSY;
	}
	newctx = (struct ieee80211_ccmp_ctx *)new_key->k_priv;
	do {
		newctx->publication_generation = ++ic->ic_ccmp_next_generation;
	} while (newctx->publication_generation == 0);
	*slot = *new_key;
	slot->k_flags |= IEEE80211_KEY_PAE_MFP_LIVE |
	    IEEE80211_KEY_SWCRYPTO;
	new_key->k_priv = NULL;
	new_key->k_flags &= ~IEEE80211_KEY_PAE_MFP_LIVE;
	if (oldctx != NULL) {
		oldctx->retired = 1;
		TAILQ_INSERT_TAIL(&ic->ic_ccmp_retired, oldctx, retired_entry);
	}
	return 0;
}

int
ieee80211_ccmp_key_unpublish_retire(struct ieee80211com *ic,
    struct ieee80211_key *key, struct ieee80211_key *out)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_ccmp_ctx *ctx;
	int error = 0;

	if (ic == NULL || key == NULL || out == NULL || key == out)
		return EINVAL;
	explicit_bzero(out, sizeof(*out));
	/* No selected-BSS leaf means no PAE-live descriptor could have crossed
	 * publication.  Let the historical generic delete path own this key. */
	if ((lock = ic->ic_pae_selected_bss_lock) == NULL)
		return EOPNOTSUPP;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	/* This negative decision must be made under the same lock as publication:
	 * an unlocked LIVE test can race the multi-field value copy in finish().
	 * Claim a non-PAE value too: the caller must never dereference the source
	 * descriptor after a concurrent publisher is allowed to reuse it. */
	if ((key->k_flags & IEEE80211_KEY_PAE_MFP_LIVE) == 0) {
		*out = *key;
		explicit_bzero(key, sizeof(*key));
		error = ENOENT;
	} else if (!ieee80211_ccmp_key_live_locked(ic, key, &ctx)) {
		error = EBUSY;
	} else {
		ctx->retired = 1;
		TAILQ_INSERT_TAIL(&ic->ic_ccmp_retired, ctx, retired_entry);
		explicit_bzero(key, sizeof(*key));
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	if (error == 0)
		(void)ieee80211_ccmp_reap(ic);
	return error;
}

int
ieee80211_ccmp_reap(struct ieee80211com *ic)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_ccmp_retired_head retired;
	struct ieee80211_ccmp_ctx *ctx;

	if (ic == NULL)
		return EINVAL;
	if ((lock = ic->ic_pae_selected_bss_lock) == NULL)
		return 0;
	TAILQ_INIT(&retired);
	/* Every live PAE CCMP reader copies aesctx and the required counter while
	 * holding this same leaf lock.  Thus no reader retains/dereferences ctx
	 * after the following unlock, unlike BIP's stack-CMAC reader protocol. */
	irq = IOSimpleLockLockDisableInterrupt(lock);
	while ((ctx = TAILQ_FIRST(&ic->ic_ccmp_retired)) != NULL) {
		TAILQ_REMOVE(&ic->ic_ccmp_retired, ctx, retired_entry);
		TAILQ_INSERT_TAIL(&retired, ctx, retired_entry);
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	while ((ctx = TAILQ_FIRST(&retired)) != NULL) {
		TAILQ_REMOVE(&retired, ctx, retired_entry);
		ieee80211_ccmp_ctx_free(ctx);
	}
	return 0;
}

int
ieee80211_ccmp_lifetime_drain(struct ieee80211com *ic)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	int i, error;

	if (ic == NULL)
		return EINVAL;
	if ((lock = ic->ic_pae_selected_bss_lock) == NULL)
		return 0;
	if ((error = ieee80211_ccmp_reap(ic)) != 0)
		return error;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	error = TAILQ_FIRST(&ic->ic_ccmp_retired) != NULL ? EBUSY : 0;
	if (error == 0 && ic->ic_bss != NULL &&
	    (ic->ic_bss->ni_pairwise_key.k_flags &
	    IEEE80211_KEY_PAE_MFP_LIVE) != 0)
		error = EBUSY;
	for (i = 0; error == 0 && i < IEEE80211_WEP_NKID; i++) {
		if (ic->ic_nw_keys[i].k_flags & IEEE80211_KEY_PAE_MFP_LIVE)
			error = EBUSY;
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return error;
}

/* Return 1 after a PAE-live key snapshot, 0 for the historical direct path,
 * and -1 for a malformed/unpublished PAE descriptor. */
static int
ieee80211_ccmp_pae_tx_snapshot(struct ieee80211com *ic,
    struct ieee80211_key *key, struct ieee80211_key *snapshot,
    struct ieee80211_ccmp_ctx *ctx_snapshot)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_ccmp_ctx *ctx;
	int result = 0;

	if (ic == NULL || key == NULL || snapshot == NULL ||
	    ctx_snapshot == NULL)
		return -1;
	/*
	 * Do not decide that this is the legacy path from an unlocked marker
	 * read.  Teardown clears a PAE descriptor under this same lock before it
	 * reaps its context; on a weakly ordered CPU an unlocked reader could
	 * otherwise observe the cleared bit while retaining a stale k_priv.  The
	 * lock makes the no-marker result an ordered observation too.  A missing
	 * lock is the one attach-unwind state in which no PAE-live descriptor can
	 * have been published.
	 */
	if ((lock = ic->ic_pae_selected_bss_lock) == NULL)
		return 0;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	if ((key->k_flags & IEEE80211_KEY_PAE_MFP_LIVE) == 0) {
		result = 0;
	} else if (ieee80211_ccmp_key_live_locked(ic, key, &ctx)) {
		*snapshot = *key;
		snapshot->k_tsc = ++key->k_tsc;
		*ctx_snapshot = *ctx;
		result = 1;
	} else
		result = -1;
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return result;
}

static int
ieee80211_ccmp_pae_rx_snapshot(struct ieee80211com *ic,
    struct ieee80211_key *key, struct ieee80211_key *snapshot,
    struct ieee80211_ccmp_ctx *ctx_snapshot,
    u_int64_t *generation)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_ccmp_ctx *ctx;
	int result = 0;

	if (ic == NULL || key == NULL || snapshot == NULL ||
	    ctx_snapshot == NULL || generation == NULL)
		return -1;
	*generation = 0;
	/* See the matching TX helper: an unlocked cleared marker is not enough
	 * to route a formerly PAE-live descriptor to the historical raw-k_priv
	 * path.  The selected-BSS lock orders deletion/publication and this
	 * negative observation alike. */
	if ((lock = ic->ic_pae_selected_bss_lock) == NULL)
		return 0;
	irq = IOSimpleLockLockDisableInterrupt(lock);
	if ((key->k_flags & IEEE80211_KEY_PAE_MFP_LIVE) == 0) {
		result = 0;
	} else if (ieee80211_ccmp_key_live_locked(ic, key, &ctx)) {
		*snapshot = *key;
		*ctx_snapshot = *ctx;
		*generation = ctx->publication_generation;
		result = 1;
	} else
		result = -1;
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return result;
}

/* The publication generation is copied under the leaf lock.  It cannot be
 * reused after an old context is immediately reaped, unlike its allocator
 * address, so a fast double rekey cannot pass an ABA identity comparison. */
static int
ieee80211_ccmp_pae_rx_commit(struct ieee80211com *ic,
    struct ieee80211_key *key, u_int64_t generation,
    mbuf_t m, u_int64_t pn)
{
	IOSimpleLock *lock;
	IOInterruptState irq;
	struct ieee80211_ccmp_ctx *ctx;
	struct ieee80211_frame *wh;
	u_int64_t *rsc;
	int accept = 0;

	if (ic == NULL || key == NULL || generation == 0 || m == NULL ||
	    (lock = ic->ic_pae_selected_bss_lock) == NULL)
		return 0;
	wh = mtod(m, struct ieee80211_frame *);
	irq = IOSimpleLockLockDisableInterrupt(lock);
	if (ieee80211_ccmp_key_live_locked(ic, key, &ctx) &&
	    ctx->publication_generation == generation) {
		if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) ==
		    IEEE80211_FC0_TYPE_DATA) {
			u_int8_t tid = ieee80211_has_qos(wh) ?
			    ieee80211_get_qos(wh) & IEEE80211_QOS_TID : 0;
			rsc = &key->k_rsc[tid];
		} else
			rsc = &key->k_mgmt_rsc;
		if (pn > *rsc) {
			*rsc = pn;
			accept = 1;
		}
	}
	IOSimpleLockUnlockEnableInterrupt(lock, irq);
	return accept;
}

/*
 * Initialize software crypto context.  This function can be overridden
 * by drivers doing hardware crypto.
 */
int
ieee80211_ccmp_set_key(struct ieee80211com *ic, struct ieee80211_key *k)
{
	struct ieee80211_ccmp_ctx *ctx;

	ctx = (struct ieee80211_ccmp_ctx *)malloc(sizeof(*ctx), 0, 0);
	if (ctx == NULL)
		return ENOMEM;
	bzero(ctx, sizeof(*ctx));
	AES_Setkey(&ctx->aesctx, k->k_key, 16);
	k->k_priv = ctx;
	return 0;
}

void
ieee80211_ccmp_delete_key(struct ieee80211com *ic, struct ieee80211_key *k)
{
	struct ieee80211_ccmp_ctx *ctx;

	if (k == NULL)
		return;
	ctx = (struct ieee80211_ccmp_ctx *)k->k_priv;
	/* A retired context belongs to the out-of-lock reaper. */
	if (ctx != NULL && !ctx->retired)
		ieee80211_ccmp_ctx_free(ctx);
	k->k_priv = NULL;
}

/*-
 * Counter with CBC-MAC (CCM) - see RFC3610.
 * CCMP uses the following CCM parameters: M = 8, L = 2
 */
static void
ieee80211_ccmp_phase1(AES_CTX *ctx, const struct ieee80211_frame *wh,
    u_int64_t pn, int lm, u_int8_t b[16], u_int8_t a[16], u_int8_t s0[16])
{
	u_int8_t auth[32], nonce[13];
	u_int8_t *aad;
	u_int8_t tid = 0;
	int la, i;

	/* construct AAD (additional authenticated data) */
	aad = &auth[2];	/* skip l(a), will be filled later */
	*aad = wh->i_fc[0];
	/* 11w: conditionally mask subtype field */
	if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) ==
	    IEEE80211_FC0_TYPE_DATA)
		*aad &= ~IEEE80211_FC0_SUBTYPE_MASK |
		   IEEE80211_FC0_SUBTYPE_QOS;
	aad++;
	/* protected bit is already set in wh */
	*aad = wh->i_fc[1];
	*aad &= ~(IEEE80211_FC1_RETRY | IEEE80211_FC1_PWR_MGT |
	    IEEE80211_FC1_MORE_DATA);
	/* 11n: conditionally mask order bit */
	if (ieee80211_has_qos(wh))
		*aad &= ~IEEE80211_FC1_ORDER;
	aad++;
	IEEE80211_ADDR_COPY(aad, wh->i_addr1); aad += IEEE80211_ADDR_LEN;
	IEEE80211_ADDR_COPY(aad, wh->i_addr2); aad += IEEE80211_ADDR_LEN;
	IEEE80211_ADDR_COPY(aad, wh->i_addr3); aad += IEEE80211_ADDR_LEN;
	*aad++ = wh->i_seq[0] & ~0xf0;
	*aad++ = 0;
	if (ieee80211_has_addr4(wh)) {
		IEEE80211_ADDR_COPY(aad,
		    ((const struct ieee80211_frame_addr4 *)wh)->i_addr4);
		aad += IEEE80211_ADDR_LEN;
	}
	if (ieee80211_has_qos(wh)) {
		/* 
		 * XXX 802.11-2012 11.4.3.3.3 g says the A-MSDU present bit
		 * must be set here if both STAs are SPP A-MSDU capable.
		 */
		*aad++ = tid = ieee80211_get_qos(wh) & IEEE80211_QOS_TID;
		*aad++ = 0;
	}

	/* construct CCM nonce */
	nonce[ 0] = tid;
	if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) ==
	    IEEE80211_FC0_TYPE_MGT)
		nonce[0] |= 1 << 4;	/* 11w: set management bit */
	IEEE80211_ADDR_COPY(&nonce[1], wh->i_addr2);
	nonce[ 7] = pn >> 40;	/* PN5 */
	nonce[ 8] = pn >> 32;	/* PN4 */
	nonce[ 9] = pn >> 24;	/* PN3 */
	nonce[10] = pn >> 16;	/* PN2 */
	nonce[11] = pn >> 8;	/* PN1 */
	nonce[12] = pn;		/* PN0 */

	/* add 2 authentication blocks (including l(a) and padded AAD) */
	la = aad - &auth[2];		/* fill l(a) */
	auth[0] = la >> 8;
	auth[1] = la & 0xff;
	memset(aad, 0, 30 - la);	/* pad AAD with zeros */

	/* construct first block B_0 */
	b[ 0] = 89;	/* Flags = 64*Adata + 8*((M-2)/2) + (L-1) */
	memcpy(&b[1], nonce, 13);
	b[14] = lm >> 8;
	b[15] = lm & 0xff;
	AES_Encrypt(ctx, b, b);

	for (i = 0; i < 16; i++)
		b[i] ^= auth[i];
	AES_Encrypt(ctx, b, b);
	for (i = 0; i < 16; i++)
		b[i] ^= auth[16 + i];
	AES_Encrypt(ctx, b, b);

	/* construct S_0 */
	a[ 0] = 1;	/* Flags = L' = (L-1) */
	memcpy(&a[1], nonce, 13);
	a[14] = a[15] = 0;
	AES_Encrypt(ctx, a, s0);
}

mbuf_t
ieee80211_ccmp_encrypt(struct ieee80211com *ic, mbuf_t m0,
    struct ieee80211_key *k)
{
	struct ieee80211_ccmp_ctx *ctx;
	struct ieee80211_ccmp_ctx ctx_snapshot;
	struct ieee80211_key key_snapshot;
	struct ieee80211_key *txkey;
	const struct ieee80211_frame *wh;
	const u_int8_t *src;
	u_int8_t *ivp, *mic, *dst;
	u_int8_t a[16], b[16], s0[16], s[16];
	mbuf_t n0 = NULL, m, n;
	int hdrlen, left, moff, noff, len;
	u_int16_t ctr;
	int i, j, managed;
    mbuf_t temp;
    unsigned int max_chunks = 1;

	bzero(&ctx_snapshot, sizeof(ctx_snapshot));
	bzero(&key_snapshot, sizeof(key_snapshot));

    mbuf_get(MBUF_DONTWAIT, mbuf_type(m0), &n0);
	if (n0 == NULL)
		goto nospace;
	if (m_dup_pkthdr(n0, m0, MBUF_DONTWAIT))
		goto nospace;
    mbuf_pkthdr_setlen(n0, mbuf_pkthdr_len(n0) + IEEE80211_CCMP_HDRLEN);
    mbuf_setlen(n0, mbuf_get_mhlen());
	if (mbuf_pkthdr_len(n0) >= mbuf_get_minclsize() - IEEE80211_CCMP_MICLEN) {
        mbuf_mclget(MBUF_DONTWAIT, mbuf_type(n0), &n0);
		if (mbuf_flags(n0) & MBUF_EXT)
            mbuf_setlen(n0, MCLBYTES);
	}
	if (mbuf_len(n0) > mbuf_pkthdr_len(n0))
        mbuf_setlen(n0, mbuf_pkthdr_len(n0));

	/* copy 802.11 header */
	wh = mtod(m0, struct ieee80211_frame *);
	hdrlen = ieee80211_get_hdrlen(wh);
	memcpy(mtod(n0, caddr_t), wh, hdrlen);

	/* A live PAE software key is immutable to this reader after the snapshot:
	 * reserve its PN and copy the AES schedule while the publisher uses the
	 * same selected-BSS lock.  Legacy software-CCMP retains its old path. */
	managed = ieee80211_ccmp_pae_tx_snapshot(ic, k, &key_snapshot,
	    &ctx_snapshot);
	if (managed < 0)
		goto drop;
	if (managed != 0) {
		txkey = &key_snapshot;
		ctx = &ctx_snapshot;
	} else {
		txkey = k;
		ctx = (struct ieee80211_ccmp_ctx *)txkey->k_priv;
		if (ctx == NULL)
			goto drop;
		txkey->k_tsc++;	/* increment the legacy 48-bit PN */
	}

	/* construct CCMP header */
	ivp = mtod(n0, u_int8_t *) + hdrlen;
	ivp[0] = txkey->k_tsc;		/* PN0 */
	ivp[1] = txkey->k_tsc >> 8;		/* PN1 */
	ivp[2] = 0;			/* Rsvd */
	ivp[3] = txkey->k_id << 6 | IEEE80211_WEP_EXTIV;	/* KeyID | ExtIV */
	ivp[4] = txkey->k_tsc >> 16;	/* PN2 */
	ivp[5] = txkey->k_tsc >> 24;	/* PN3 */
	ivp[6] = txkey->k_tsc >> 32;	/* PN4 */
	ivp[7] = txkey->k_tsc >> 40;	/* PN5 */

	/* construct initial B, A and S_0 blocks */
	ieee80211_ccmp_phase1(&ctx->aesctx, wh, txkey->k_tsc,
	    mbuf_pkthdr_len(m0) - hdrlen, b, a, s0);

	/* construct S_1 */
	ctr = 1;
	a[14] = ctr >> 8;
	a[15] = ctr & 0xff;
	AES_Encrypt(&ctx->aesctx, a, s);

	/* encrypt frame body and compute MIC */
	j = 0;
	m = m0;
	n = n0;
	moff = hdrlen;
	noff = hdrlen + IEEE80211_CCMP_HDRLEN;
	left = mbuf_pkthdr_len(m0) - moff;
	while (left > 0) {
		if (moff == mbuf_len(m)) {
			/* nothing left to copy from m */
			m = mbuf_next(m);
			moff = 0;
		}
		if (noff == mbuf_len(n)) {
			/* n is full and there's more data to copy */
            temp = NULL;
            mbuf_get(MBUF_DONTWAIT, mbuf_type(n), &temp);
			if (temp == NULL)
				goto nospace;
            mbuf_setnext(n, temp);
			n = mbuf_next(n);
            mbuf_setlen(n, mbuf_get_mlen());
			if (left >= mbuf_get_minclsize() - IEEE80211_CCMP_MICLEN) {
                mbuf_mclget(MBUF_DONTWAIT, mbuf_type(n), &n);
				if (mbuf_flags(n) & MBUF_EXT)
                    mbuf_setlen(n, MCLBYTES);
			}
			if (mbuf_len(n) > left)
                mbuf_setlen(n, left);
			noff = 0;
		}
		len = min(mbuf_len(m) - moff, mbuf_len(n) - noff);

		src = mtod(m, u_int8_t *) + moff;
		dst = mtod(n, u_int8_t *) + noff;
		for (i = 0; i < len; i++) {
			/* update MIC with clear text */
			b[j] ^= src[i];
			/* encrypt message */
			dst[i] = src[i] ^ s[j];
			if (++j < 16)
				continue;
			/* we have a full block, encrypt MIC */
			AES_Encrypt(&ctx->aesctx, b, b);
			/* construct a new S_ctr block */
			ctr++;
			a[14] = ctr >> 8;
			a[15] = ctr & 0xff;
			AES_Encrypt(&ctx->aesctx, a, s);
			j = 0;
		}

		moff += len;
		noff += len;
		left -= len;
	}
	if (j != 0)	/* partial block, encrypt MIC */
		AES_Encrypt(&ctx->aesctx, b, b);

	/* reserve trailing space for MIC */
	if (mbuf_trailingspace(n) < IEEE80211_CCMP_MICLEN) {
        temp = NULL;
        mbuf_get(MBUF_DONTWAIT, mbuf_type(n), &temp);
		if (temp == NULL)
			goto nospace;
        mbuf_setnext(n, temp);
		n = mbuf_next(n);
        mbuf_setlen(n, 0);
	}
	/* finalize MIC, U := T XOR first-M-bytes( S_0 ) */
	mic = mtod(n, u_int8_t *) + mbuf_len(n);
	for (i = 0; i < IEEE80211_CCMP_MICLEN; i++)
		mic[i] = b[i] ^ s0[i];
    mbuf_setlen(n, mbuf_len(n) + IEEE80211_CCMP_MICLEN);
	mbuf_pkthdr_setlen(n0, mbuf_pkthdr_len(n0) + IEEE80211_CCMP_MICLEN);

	mbuf_freem(m0);
	explicit_bzero(&key_snapshot, sizeof(key_snapshot));
	explicit_bzero(&ctx_snapshot, sizeof(ctx_snapshot));
	return n0;
 drop:
	mbuf_freem(m0);
	mbuf_freem(n0);
	explicit_bzero(&key_snapshot, sizeof(key_snapshot));
	explicit_bzero(&ctx_snapshot, sizeof(ctx_snapshot));
	return NULL;
 nospace:
	ic->ic_stats.is_tx_nombuf++;
	mbuf_freem(m0);
	mbuf_freem(n0);
	explicit_bzero(&key_snapshot, sizeof(key_snapshot));
	explicit_bzero(&ctx_snapshot, sizeof(ctx_snapshot));
	return NULL;
}

int
ieee80211_ccmp_get_pn(uint64_t *pn, uint64_t **prsc, mbuf_t m,
    struct ieee80211_key *k)
{
   struct ieee80211_frame *wh;
   int hdrlen;
   const u_int8_t *ivp;

   wh = mtod(m, struct ieee80211_frame *);
   hdrlen = ieee80211_get_hdrlen(wh);
   if (mbuf_pkthdr_len(m) < hdrlen + IEEE80211_CCMP_HDRLEN)
       return EINVAL;

   ivp = (u_int8_t *)wh + hdrlen;

   /* check that ExtIV bit is set */
   if (!(ivp[3] & IEEE80211_WEP_EXTIV))
       return EINVAL;

   /* retrieve last seen packet number for this frame type/priority */
   if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) ==
       IEEE80211_FC0_TYPE_DATA) {
       u_int8_t tid = ieee80211_has_qos(wh) ?
           ieee80211_get_qos(wh) & IEEE80211_QOS_TID : 0;
       *prsc = &k->k_rsc[tid];
   } else    /* 11w: management frames have their own counters */
       *prsc = &k->k_mgmt_rsc;

   /* extract the 48-bit PN from the CCMP header */
   *pn = (u_int64_t)ivp[0]      |
        (u_int64_t)ivp[1] <<  8 |
        (u_int64_t)ivp[4] << 16 |
        (u_int64_t)ivp[5] << 24 |
        (u_int64_t)ivp[6] << 32 |
        (u_int64_t)ivp[7] << 40;

   return 0;
}

mbuf_t
ieee80211_ccmp_decrypt(struct ieee80211com *ic, mbuf_t m0,
    struct ieee80211_key *k)
{
	struct ieee80211_ccmp_ctx *ctx;
	struct ieee80211_ccmp_ctx ctx_snapshot;
	struct ieee80211_key key_snapshot;
	struct ieee80211_key *rxkey;
	struct ieee80211_frame *wh;
	u_int64_t generation = 0, pn, *prsc;
    const u_int8_t *src;
	u_int8_t *dst;
	u_int8_t mic0[IEEE80211_CCMP_MICLEN];
	u_int8_t a[16], b[16], s0[16], s[16];
	mbuf_t n0 = NULL, m, n;
	int hdrlen, left, moff, noff, len;
	u_int16_t ctr;
	int i, j, managed;
    mbuf_t temp;
    unsigned int max_chunks = 1;

	bzero(&ctx_snapshot, sizeof(ctx_snapshot));
	bzero(&key_snapshot, sizeof(key_snapshot));

	wh = mtod(m0, struct ieee80211_frame *);
    hdrlen = ieee80211_get_hdrlen(wh);
    if (mbuf_pkthdr_len(m0) < hdrlen + IEEE80211_CCMP_HDRLEN +
        IEEE80211_CCMP_MICLEN) {
	        goto drop;
	    }
	/* The PAE software-CCMP path never dereferences a live k_priv after the
	 * selected-BSS lock drops.  Its copied AES schedule remains valid even if
	 * a concurrent rekey immediately retires the old descriptor. */
	managed = ieee80211_ccmp_pae_rx_snapshot(ic, k, &key_snapshot,
	    &ctx_snapshot, &generation);
	if (managed < 0)
		goto drop;
	if (managed != 0) {
		rxkey = &key_snapshot;
		ctx = &ctx_snapshot;
	} else {
		rxkey = k;
		ctx = (struct ieee80211_ccmp_ctx *)rxkey->k_priv;
		if (ctx == NULL)
			goto drop;
	}
    
	    /*
     * Get the frame's Packet Number (PN) and a pointer to our last-seen
     * Receive Sequence Counter (RSC) which we can use to detect replays.
     */
	    if (ieee80211_ccmp_get_pn(&pn, &prsc, m0, rxkey) != 0)
	        goto drop;

	if (pn <= *prsc) {
		goto replay;
	}

    mbuf_get(MBUF_DONTWAIT, mbuf_type(m0), &n0);
	if (n0 == NULL)
		goto nospace;
	if (m_dup_pkthdr(n0, m0, MBUF_DONTWAIT))
		goto nospace;
    
    mbuf_pkthdr_setlen(n0, mbuf_pkthdr_len(n0) - (IEEE80211_CCMP_HDRLEN + IEEE80211_CCMP_MICLEN));
    mbuf_setlen(n0, mbuf_get_mhlen());
	if (mbuf_pkthdr_len(n0) >= mbuf_get_minclsize()) {
        mbuf_mclget(MBUF_DONTWAIT, mbuf_type(n0), &n0);
		if (mbuf_flags(n0) & MBUF_EXT)
            mbuf_setlen(n0, MCLBYTES);
	}
	if (mbuf_len(n0) > mbuf_pkthdr_len(n0))
        mbuf_setlen(n0, mbuf_pkthdr_len(n0));

	/* construct initial B, A and S_0 blocks */
	ieee80211_ccmp_phase1(&ctx->aesctx, wh, pn,
	    mbuf_pkthdr_len(n0) - hdrlen, b, a, s0);

	/* copy 802.11 header and clear protected bit */
	memcpy(mtod(n0, caddr_t), wh, hdrlen);
	wh = mtod(n0, struct ieee80211_frame *);
	wh->i_fc[1] &= ~IEEE80211_FC1_PROTECTED;

	/* construct S_1 */
	ctr = 1;
	a[14] = ctr >> 8;
	a[15] = ctr & 0xff;
	AES_Encrypt(&ctx->aesctx, a, s);

	/* decrypt frame body and compute MIC */
	j = 0;
	m = m0;
	n = n0;
	moff = hdrlen + IEEE80211_CCMP_HDRLEN;
	noff = hdrlen;
	left = mbuf_pkthdr_len(n0) - noff;
	while (left > 0) {
		if (moff == mbuf_len(m)) {
			/* nothing left to copy from m */
			m = mbuf_next(m);
			moff = 0;
		}
		if (noff == mbuf_len(n)) {
			/* n is full and there's more data to copy */
            temp = NULL;
            mbuf_get(MBUF_DONTWAIT, mbuf_type(n), &temp);
			if (temp == NULL)
				goto nospace;
            mbuf_setnext(n, temp);
			n = mbuf_next(n);
            mbuf_setlen(n, mbuf_get_mlen());
			if (left >= mbuf_get_minclsize()) {
                mbuf_mclget(MBUF_DONTWAIT, mbuf_type(n), &n);
				if (mbuf_flags(n) & MBUF_EXT)
                    mbuf_setlen(n, MCLBYTES);
			}
			if (mbuf_len(n) > left)
                mbuf_setlen(n, left);
			noff = 0;
		}
		len = min(mbuf_len(m) - moff, mbuf_len(n) - noff);

		src = mtod(m, u_int8_t *) + moff;
		dst = mtod(n, u_int8_t *) + noff;
		for (i = 0; i < len; i++) {
			/* decrypt message */
			dst[i] = src[i] ^ s[j];
			/* update MIC with clear text */
			b[j] ^= dst[i];
			if (++j < 16)
				continue;
			/* we have a full block, encrypt MIC */
			AES_Encrypt(&ctx->aesctx, b, b);
			/* construct a new S_ctr block */
			ctr++;
			a[14] = ctr >> 8;
			a[15] = ctr & 0xff;
			AES_Encrypt(&ctx->aesctx, a, s);
			j = 0;
		}

		moff += len;
		noff += len;
		left -= len;
	}
	if (j != 0)	/* partial block, encrypt MIC */
		AES_Encrypt(&ctx->aesctx, b, b);

	/* finalize MIC, U := T XOR first-M-bytes( S_0 ) */
	for (i = 0; i < IEEE80211_CCMP_MICLEN; i++)
		b[i] ^= s0[i];

	/* check that it matches the MIC in received frame */
	mbuf_copydata(m, moff, IEEE80211_CCMP_MICLEN, mic0);
	if (timingsafe_bcmp(mic0, b, IEEE80211_CCMP_MICLEN) != 0) {
		goto badmic;
	}

	/* Commit a validated RSC only if the same descriptor still owns it.  A
	 * rekey that won the interval intentionally drops this old-key frame. */
	if (managed != 0) {
		if (!ieee80211_ccmp_pae_rx_commit(ic, k, generation, m0, pn))
			goto replay;
	} else
		*prsc = pn;

	mbuf_freem(m0);
	explicit_bzero(&key_snapshot, sizeof(key_snapshot));
	explicit_bzero(&ctx_snapshot, sizeof(ctx_snapshot));
	return n0;
 badmic:
	ic->ic_stats.is_ccmp_dec_errs++;
	goto drop;
 replay:
	/* A rekey race is fail-closed just like a stale replay counter. */
	ic->ic_stats.is_ccmp_replays++;
	goto drop;
 drop:
	mbuf_freem(m0);
	mbuf_freem(n0);
	explicit_bzero(&key_snapshot, sizeof(key_snapshot));
	explicit_bzero(&ctx_snapshot, sizeof(ctx_snapshot));
	return NULL;
 nospace:
	ic->ic_stats.is_rx_nombuf++;
	mbuf_freem(m0);
	mbuf_freem(n0);
	explicit_bzero(&key_snapshot, sizeof(key_snapshot));
	explicit_bzero(&ctx_snapshot, sizeof(ctx_snapshot));
	return NULL;
}
