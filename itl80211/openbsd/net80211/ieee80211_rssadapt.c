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
/*	$OpenBSD: ieee80211_rssadapt.c,v 1.11 2014/12/23 03:24:08 tedu Exp $	*/
/*	$NetBSD: ieee80211_rssadapt.c,v 1.7 2004/05/25 04:33:59 dyoung Exp $	*/

/*-
 * Copyright (c) 2003, 2004 David Young.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY David Young ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL David
 * Young BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_rssadapt.h>

#ifdef interpolate
#undef interpolate
#endif
#define interpolate(parm, old, new)				\
	((parm##_old * (old) +					\
	(parm##_denom - parm##_old) * (new)) / parm##_denom)

#ifdef IEEE80211_DEBUG
#define RSSADAPT_DO_PRINT() (0)
#define	RSSADAPT_PRINTF(X) \
	do { ; } while (0)

int ieee80211_rssadapt_debug = 0;

#else
#define	RSSADAPT_DO_PRINT() (0)
#define	RSSADAPT_PRINTF(X)
#endif

static const struct ieee80211_rssadapt_expavgctl master_expavgctl = {
	.rc_decay_denom		= 16,
	.rc_decay_old		= 15,
	.rc_thresh_denom	= 8,
	.rc_thresh_old		= 4,
	.rc_avgrssi_denom	= 8,
	.rc_avgrssi_old		= 4
};

int
ieee80211_rssadapt_choose(struct ieee80211_rssadapt *ra,
    const struct ieee80211_rateset *rs, const struct ieee80211_frame *wh,
    u_int len, int fixed_rate, const char *dvname, int do_not_adapt)
{
	u_int16_t (*thrs)[IEEE80211_RATE_SIZE];
	int flags = 0, i, rateidx = 0, thridx, top;

	(void)dvname;

	if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) == IEEE80211_FC0_TYPE_CTL)
		flags |= IEEE80211_RATE_BASIC;

	for (i = 0, top = IEEE80211_RSSADAPT_BKT0;
	     i < IEEE80211_RSSADAPT_BKTS;
	     i++, top <<= IEEE80211_RSSADAPT_BKTPOWER) {
		thridx = i;
		if (len <= top)
			break;
	}

	thrs = &ra->ra_rate_thresh[thridx];

	if (fixed_rate != -1) {
		if ((rs->rs_rates[fixed_rate] & flags) == flags) {
			rateidx = fixed_rate;
			goto out;
		}
		flags |= IEEE80211_RATE_BASIC;
		i = fixed_rate;
	} else
		i = rs->rs_nrates;

	while (--i >= 0) {
		rateidx = i;
		if ((rs->rs_rates[i] & flags) != flags)
			continue;
		if (do_not_adapt)
			break;
		if ((*thrs)[i] < ra->ra_avg_rssi)
			break;
	}

out:
	return rateidx;
}

void
ieee80211_rssadapt_updatestats(struct ieee80211_rssadapt *ra)
{
	long interval;

	ra->ra_pktrate =
	    (ra->ra_pktrate + 10 * (ra->ra_nfail + ra->ra_nok)) / 2;
	ra->ra_nfail = ra->ra_nok = 0;

	/* a node is eligible for its rate to be raised every 1/10 to 10
	 * seconds, more eligible in proportion to recent packet rates.
	 */
	interval = MAX(100000, 10000000 / MAX(1, 10 * ra->ra_pktrate));
	ra->ra_raise_interval.tv_sec = interval / (1000 * 1000);
	ra->ra_raise_interval.tv_usec = interval % (1000 * 1000);
}

void
ieee80211_rssadapt_input(struct ieee80211com *ic,
    const struct ieee80211_node *ni, struct ieee80211_rssadapt *ra, int rssi)
{
	(void)ic;
	(void)ni;

	ra->ra_avg_rssi = interpolate(master_expavgctl.rc_avgrssi,
	    ra->ra_avg_rssi, (rssi << 8));
}

/*
 * Adapt the data rate to suit the conditions.  When a transmitted
 * packet is dropped after IEEE80211_RSSADAPT_RETRY_LIMIT retransmissions,
 * raise the RSS threshold for transmitting packets of similar length at
 * the same data rate.
 */
void
ieee80211_rssadapt_lower_rate(struct ieee80211com *ic,
    const struct ieee80211_node *ni, struct ieee80211_rssadapt *ra,
    const struct ieee80211_rssdesc *id)
{
	const struct ieee80211_rateset *rs = &ni->ni_rates;
	u_int16_t last_thr;
	u_int i, thridx, top;

	(void)ic;

	ra->ra_nfail++;

	if (id->id_rateidx >= rs->rs_nrates) {
		return;
	}

	for (i = 0, top = IEEE80211_RSSADAPT_BKT0;
	     i < IEEE80211_RSSADAPT_BKTS;
	     i++, top <<= IEEE80211_RSSADAPT_BKTPOWER) {
		thridx = i;
		if (id->id_len <= top)
			break;
	}

	last_thr = ra->ra_rate_thresh[thridx][id->id_rateidx];
	ra->ra_rate_thresh[thridx][id->id_rateidx] =
	    interpolate(master_expavgctl.rc_thresh, last_thr,
	    (id->id_rssi << 8));
}

void
ieee80211_rssadapt_raise_rate(struct ieee80211com *ic,
    struct ieee80211_rssadapt *ra, const struct ieee80211_rssdesc *id)
{
	u_int16_t (*thrs)[IEEE80211_RATE_SIZE], newthr, oldthr;
	const struct ieee80211_node *ni = id->id_node;
	const struct ieee80211_rateset *rs = &ni->ni_rates;
	int i, top;

	(void)ic;

	ra->ra_nok++;

	if (!ratecheck(&ra->ra_last_raise, &ra->ra_raise_interval))
		return;

	for (i = 0, top = IEEE80211_RSSADAPT_BKT0;
	     i < IEEE80211_RSSADAPT_BKTS;
	     i++, top <<= IEEE80211_RSSADAPT_BKTPOWER) {
		thrs = &ra->ra_rate_thresh[i];
		if (id->id_len <= top)
			break;
	}

	if (id->id_rateidx + 1 < rs->rs_nrates &&
	    (*thrs)[id->id_rateidx + 1] > (*thrs)[id->id_rateidx]) {
		oldthr = (*thrs)[id->id_rateidx + 1];
		if ((*thrs)[id->id_rateidx] == 0)
			newthr = ra->ra_avg_rssi;
		else
			newthr = (*thrs)[id->id_rateidx];
		(*thrs)[id->id_rateidx + 1] =
		    interpolate(master_expavgctl.rc_decay, oldthr, newthr);
	}
}
