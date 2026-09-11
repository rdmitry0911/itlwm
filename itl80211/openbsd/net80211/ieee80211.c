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
/*    $OpenBSD: ieee80211.c,v 1.83 2020/04/08 09:34:29 stsp Exp $    */
/*    $NetBSD: ieee80211.c,v 1.19 2004/06/06 05:45:29 dyoung Exp $    */

/*-
 * Copyright (c) 2001 Atsushi Onoe
 * Copyright (c) 2002, 2003 Sam Leffler, Errno Consulting
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
 * IEEE 802.11 generic handler
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
#include <kern/clock.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <sys/_if_media.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_priv.h>
#include <net80211/ieee80211_assoc_comeback.h>
#include <ClientKit/AirportItlwmRoamLockBridge.h>

#ifdef IEEE80211_DEBUG
int	ieee80211_debug = 0;
#endif

///compat for undefined symbols
int _stop(struct kmod_info*, void*) {
    return 0;
};
int _start(struct kmod_info*, void*) {
    return 0;
};
///

int ieee80211_cache_size = IEEE80211_CACHE_SIZE;
static volatile u_int32_t airport_itlwm_roam_locked = 0;

void
airportItlwmSetRoamLocked(bool locked)
{
    __atomic_store_n(&airport_itlwm_roam_locked, locked ? 1U : 0U,
        __ATOMIC_RELEASE);
}

bool
airportItlwmIsRoamLocked(void)
{
    return __atomic_load_n(&airport_itlwm_roam_locked,
        __ATOMIC_ACQUIRE) != 0;
}

int
ieee80211_wcl_scan_plan_stage(struct ieee80211com *ic,
    const struct ieee80211_wcl_scan_plan *source)
{
	struct ieee80211_wcl_scan_plan *plan;
	IOInterruptState irq;
	int error = 0;

	if (ic == NULL || source == NULL || source->generation == 0 ||
	    source->ssid_len > IEEE80211_NWID_LEN ||
	    source->requested_channel_count >
	    IEEE80211_WCL_SCAN_REQUEST_MAX_CHANNELS)
		return EINVAL;
	if (ic->ic_pae_selected_bss_lock == NULL)
		return ENXIO;
	plan = &ic->ic_wcl_scan_plan;
	irq = IOSimpleLockLockDisableInterrupt(ic->ic_pae_selected_bss_lock);
	if (__atomic_load_n(&plan->active, __ATOMIC_ACQUIRE) != 0)
		error = EBUSY;
	else {
		/* The leaf also excludes readers and clear/restage. An atomic
		 * active flag alone cannot protect the plain payload memcpy. */
		memcpy(plan, source, sizeof(*plan));
		plan->active = 0;
		__atomic_thread_fence(__ATOMIC_RELEASE);
		__atomic_store_n(&plan->active, 1, __ATOMIC_RELEASE);
	}
	IOSimpleLockUnlockEnableInterrupt(ic->ic_pae_selected_bss_lock, irq);
	return error;
}

int
ieee80211_wcl_scan_plan_snapshot(struct ieee80211com *ic,
    struct ieee80211_wcl_scan_plan *snapshot)
{
	struct ieee80211_wcl_scan_plan *plan;
	IOInterruptState irq;
	int copied = 0;

	if (snapshot == NULL)
		return 0;
	explicit_bzero(snapshot, sizeof(*snapshot));
	if (ic == NULL || ic->ic_pae_selected_bss_lock == NULL)
		return 0;
	plan = &ic->ic_wcl_scan_plan;
	irq = IOSimpleLockLockDisableInterrupt(ic->ic_pae_selected_bss_lock);
	if (__atomic_load_n(&plan->active, __ATOMIC_ACQUIRE) != 0 &&
	    plan->generation != 0) {
		memcpy(snapshot, plan, sizeof(*snapshot));
		snapshot->active = 1;
		copied = 1;
	}
	IOSimpleLockUnlockEnableInterrupt(ic->ic_pae_selected_bss_lock, irq);
	return copied;
}

int
ieee80211_wcl_scan_plan_channel_allowed(struct ieee80211com *ic,
    const struct ieee80211_wcl_scan_plan *plan,
    const struct ieee80211_channel *channel)
{
	u_int channel_number;

	if (ic == NULL || plan == NULL || channel == NULL ||
	    plan->active == 0 || plan->channel_filter == 0)
		return 1;
	channel_number = ieee80211_chan2ieee(ic, channel);
	if (channel_number > IEEE80211_CHAN_MAX)
		return 0;
	if (isset(plan->channel_any, channel_number))
		return 1;
	if (IEEE80211_IS_CHAN_2GHZ(channel))
		return isset(plan->channel_2ghz, channel_number) != 0;
	if (IEEE80211_IS_CHAN_5GHZ(channel))
		return isset(plan->channel_5ghz, channel_number) != 0;
	return 0;
}

void
ieee80211_wcl_scan_plan_clear(struct ieee80211com *ic,
    u_int64_t generation)
{
	struct ieee80211_wcl_scan_plan *plan;
	IOInterruptState irq;

	if (ic == NULL || ic->ic_pae_selected_bss_lock == NULL)
		return;
	plan = &ic->ic_wcl_scan_plan;
	irq = IOSimpleLockLockDisableInterrupt(ic->ic_pae_selected_bss_lock);
	if (__atomic_load_n(&plan->active, __ATOMIC_ACQUIRE) == 0 ||
	    (generation != 0 && plan->generation != generation)) {
		IOSimpleLockUnlockEnableInterrupt(ic->ic_pae_selected_bss_lock, irq);
		return;
	}
	__atomic_store_n(&plan->active, 0, __ATOMIC_RELEASE);
	IOSimpleLockUnlockEnableInterrupt(ic->ic_pae_selected_bss_lock, irq);
}

void
ieee80211_set_roam_profile_policy(struct ieee80211com *ic,
    const struct ieee80211_roam_profile_policy *policy)
{
    u_int32_t generation;

    if (ic == NULL || policy == NULL)
        return;

    /* WCL setters are serialized by the interface command gate. */
    generation = __atomic_load_n(&ic->ic_roam_profile_generation,
        __ATOMIC_RELAXED);
    if (generation & 1)
        generation++;
    __atomic_store_n(&ic->ic_roam_profile_generation, generation + 1,
        __ATOMIC_RELEASE);
    memcpy(&ic->ic_roam_profile, policy, sizeof(*policy));
    __atomic_store_n(&ic->ic_roam_profile_generation, generation + 2,
        __ATOMIC_RELEASE);

    /* A pending legacy threshold timer belongs to the preceding policy. */
    if (ic->ic_bgscan_timeout != NULL)
        timeout_del(&ic->ic_bgscan_timeout);
}

static int
ieee80211_roam_profile_snapshot(struct ieee80211com *ic,
    struct ieee80211_roam_profile_policy *policy)
{
    u_int32_t before, after;
    int attempt;

    if (ic == NULL || policy == NULL)
        return 0;
    for (attempt = 0; attempt != 4; attempt++) {
        before = __atomic_load_n(&ic->ic_roam_profile_generation,
            __ATOMIC_ACQUIRE);
        if (before & 1)
            continue;
        memcpy(policy, &ic->ic_roam_profile, sizeof(*policy));
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        after = __atomic_load_n(&ic->ic_roam_profile_generation,
            __ATOMIC_RELAXED);
        if (before == after)
            return policy->valid_mask != 0;
    }
    return 0;
}

static int
ieee80211_roam_profile_band(const struct ieee80211_channel *chan)
{
    if (chan == NULL || chan == IEEE80211_CHAN_ANYC)
        return -1;
    if (chan->ic_freq >= 5925)
        return IEEE80211_ROAM_PROFILE_BAND_6GHZ;
    if (IEEE80211_IS_CHAN_2GHZ(chan))
        return IEEE80211_ROAM_PROFILE_BAND_2GHZ;
    if (IEEE80211_IS_CHAN_5GHZ(chan))
        return IEEE80211_ROAM_PROFILE_BAND_5GHZ;
    return -1;
}

static const struct ieee80211_roam_profile_bracket *
ieee80211_roam_profile_active(
    const struct ieee80211_roam_profile_policy *policy,
    const struct ieee80211_node *ni, int *band_out)
{
    const struct ieee80211_roam_profile_bracket *bracket;
    int band, rssi_dbm, i;

    band = ieee80211_roam_profile_band(ni != NULL ? ni->ni_chan : NULL);
    if (band_out != NULL)
        *band_out = band;
    if (band < 0 || (policy->valid_mask & (1U << band)) == 0)
        return NULL;

    rssi_dbm = (int)ni->ni_rssi - (int)policy->rssi_bias_db;
    for (i = 0; i < policy->count[band]; i++) {
        bracket = &policy->bracket[band][i];
        /* Adjacent ranges share one boundary; the lower range owns it. */
        if (rssi_dbm <= bracket->trigger_dbm &&
            rssi_dbm > bracket->lower_dbm)
            return bracket;
    }
    return NULL;
}

int
ieee80211_roam_profile_scan_delay(struct ieee80211com *ic,
    const struct ieee80211_node *ni, u_int32_t *delay_ms)
{
    struct ieee80211_roam_profile_policy policy;
    const struct ieee80211_roam_profile_bracket *bracket;
    u_int32_t delay_s, maximum_s, multiplier;
    int i;

    if (delay_ms == NULL || ni == NULL)
        return 0;
    if (!ieee80211_roam_profile_snapshot(ic, &policy))
        return 0;               /* preserve the legacy RSSI policy */

    bracket = ieee80211_roam_profile_active(&policy, ni, NULL);
    if (bracket == NULL)
        return -1;              /* profile configured, range is dormant */

    delay_s = bracket->initial_scan_period_s;
    maximum_s = bracket->max_scan_period_s;
    multiplier = bracket->backoff_multiplier;
    if (delay_s == 0)
        delay_s = bracket->full_scan_period_s;
    if (multiplier == 0)
        multiplier = 1;
    for (i = 0; i < ic->ic_bgscan_fail; i++) {
        if (maximum_s != 0 && delay_s >= maximum_s) {
            delay_s = maximum_s;
            break;
        }
        if (delay_s > UINT32_MAX / multiplier) {
            delay_s = maximum_s != 0 ? maximum_s : UINT32_MAX / 1000;
            break;
        }
        delay_s *= multiplier;
    }
    if (maximum_s != 0 && delay_s > maximum_s)
        delay_s = maximum_s;
    if (delay_s > UINT32_MAX / 1000)
        delay_s = UINT32_MAX / 1000;
    *delay_ms = delay_s * 1000;
    return 1;
}

int
ieee80211_roam_profile_candidate_allowed(struct ieee80211com *ic,
    const struct ieee80211_node *current,
    const struct ieee80211_node *candidate)
{
    struct ieee80211_roam_profile_policy policy;
    const struct ieee80211_roam_profile_bracket *bracket;
    int current_dbm, candidate_dbm, candidate_score, candidate_band;
    int boost_threshold, boost_delta;

    if (current == NULL || candidate == NULL)
        return 1;
    if (!ieee80211_roam_profile_snapshot(ic, &policy))
        return 1;
    bracket = ieee80211_roam_profile_active(&policy, current, NULL);
    if (bracket == NULL)
        return 1;

    candidate_band = ieee80211_roam_profile_band(candidate->ni_chan);
    current_dbm = (int)current->ni_rssi - (int)policy.rssi_bias_db;
    candidate_dbm = (int)candidate->ni_rssi - (int)policy.rssi_bias_db;
    candidate_score = candidate_dbm;
    if (candidate_band >= 0) {
        boost_threshold = bracket->boost_threshold_dbm[candidate_band];
        boost_delta = bracket->boost_delta_db[candidate_band];
        if (boost_threshold > -128 && boost_delta > 0 &&
            candidate_dbm >= boost_threshold)
            candidate_score += boost_delta;
    }
    return candidate_score >= current_dbm + bracket->roam_delta_db;
}

void ieee80211_setbasicrates(struct ieee80211com *);
int ieee80211_findrate(struct ieee80211com *, enum ieee80211_phymode, int);
void ieee80211_configure_ampdu_tx(struct ieee80211com *, int);

void
ieee80211_begin_bgscan(struct _ifnet *ifp)
{
    struct ieee80211com *ic = (struct ieee80211com *)ifp;

    /* roam_off suppresses only net80211's autonomous RSSI roam owner. */
    if (airportItlwmIsRoamLocked())
        return;
    
    if (ic->ic_state != IEEE80211_S_RUN || ic->ic_mgt_timer != 0)
        return;
    
    if ((ic->ic_flags & IEEE80211_F_RSNON) && !ic->ic_bss->ni_port_valid)
        return;
    
    if ((ic->ic_flags & IEEE80211_F_BGSCAN)) {
        /* A controller-owned cache scan collects candidates for WCL; do not
         * let the periodic timer turn it into an autonomous roam. */
        if (__atomic_load_n(&ic->ic_wcl_scan_active, __ATOMIC_ACQUIRE) == 0)
            ic->ic_flags &= ~IEEE80211_F_DISABLE_BG_AUTO_CONNECT;
        return;
    }
    
    if (ic->ic_bgscan_start != NULL && ic->ic_bgscan_start(ic, 0) == 0) {
        /*
         * Free the nodes table to ensure we get an up-to-date view
         * of APs around us. In particular, we need to kick out the
         * AP we are associated to. Otherwise, our current AP might
         * stay cached if it is turned off while we are scanning, and
         * we could end up picking a now non-existent AP over and over.
         */
        ieee80211_free_allnodes(ic, 0 /* keep ic->ic_bss */);
        
        ic->ic_flags |= IEEE80211_F_BGSCAN;
        ic->ic_flags &= ~IEEE80211_F_DISABLE_BG_AUTO_CONNECT;
        /* Driver calls ieee80211_end_scan() when done. */
    }
}

static u_int8_t
ieee80211_wcl_reassoc_primary_channel(u_int16_t channel_spec)
{
	/* Tahoe AppleChannelSpec keeps the primary 20 MHz channel in byte 0.
	 * Bits 15:14 describe the band and the remaining width/sideband bits do
	 * not change candidate identity for this net80211 scan. */
	return (u_int8_t)(channel_spec & 0xff);
}

int
ieee80211_wcl_reassoc_candidate_disposition(struct ieee80211com *ic,
    const struct ieee80211_node *ni)
{
	const struct ieee80211_wcl_reassoc_request *request;
	u_int8_t channel;
	int channel_allowed = 0;
	static const u_int8_t unspecified[IEEE80211_ADDR_LEN] = { 0 };
	static const u_int8_t broadcast[IEEE80211_ADDR_LEN] =
	    { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	u_int i;

	if (ic == NULL || ni == NULL || !ic->ic_wcl_reassoc_owner_active ||
	    (ic->ic_wcl_reassoc_owner_last_leaf !=
	    IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_STARTED &&
	    ic->ic_wcl_reassoc_owner_last_leaf !=
	    IEEE80211_WCL_REASSOC_OWNER_LEAF_ROAM_STARTED))
		return 0;

	/* A WCL roam request must demonstrate a different over-the-air BSS. */
	if (IEEE80211_ADDR_EQ(ni->ni_bssid,
	    ic->ic_wcl_reassoc_source_bssid))
		return -1;
	/* WLC_REASSOC roams within the associated ESS. A wildcard BSSID does
	 * not authorize a switch to an unrelated SSID when the discovery-side
	 * desired-SSID selector is empty. */
	if (ic->ic_bss == NULL || ni->ni_esslen != ic->ic_bss->ni_esslen ||
	    memcmp(ni->ni_essid, ic->ic_bss->ni_essid, ni->ni_esslen) != 0)
		return -1;
	channel = (u_int8_t)ieee80211_chan2ieee(ic, ni->ni_chan);
	request = &ic->ic_wcl_reassoc_request;

	if (request->channel_count == 0) {
		channel_allowed = 1;
	} else {
		for (i = 0; i < request->channel_count; i++) {
			if (ieee80211_wcl_reassoc_primary_channel(
			    request->channel_spec[i]) == channel) {
				channel_allowed = 1;
				break;
			}
		}
	}
	if (!channel_allowed)
		return -1;

	if (request->prune_rssi_dbm != 0 &&
	    (int)ni->ni_rssi - 100 < (int)request->prune_rssi_dbm)
		return -1;

	/* WCL setROAMWithBssid uses a broadcast BSSID for an unrestricted roam.
	 * The firmware ABI also admits an all-zero unspecified BSSID. Neither
	 * contains a score or a channel; exact addresses form an allowlist. */
	if (request->candidate_count == 0)
		return 1;
	for (i = 0; i < request->candidate_count; i++) {
		const u_int8_t *bssid = request->candidate[i].bssid;
		if (IEEE80211_ADDR_EQ(bssid, unspecified) ||
		    IEEE80211_ADDR_EQ(bssid, broadcast) ||
		    IEEE80211_ADDR_EQ(bssid, ni->ni_bssid))
			return 1;
	}
	return -1;
}

static void ieee80211_wcl_reassoc_clear_locked(struct ieee80211com *);

int
ieee80211_begin_wcl_reassoc_bgscan(struct _ifnet *ifp,
    const struct ieee80211_wcl_reassoc_request *request)
{
	struct ieee80211com *ic = (struct ieee80211com *)ifp;
	int error;
	u_int64_t serial, source_epoch;
	IOInterruptState irq;

	if (ic == NULL || request == NULL || ic->ic_pae_selected_bss_lock == NULL)
		return EINVAL;
	irq = IOSimpleLockLockDisableInterrupt(ic->ic_pae_selected_bss_lock);
	if (ic->ic_opmode != IEEE80211_M_STA ||
	    ic->ic_state != IEEE80211_S_RUN || ic->ic_bss == NULL ||
	    ic->ic_mgt_timer != 0 || (ic->ic_flags & IEEE80211_F_BGSCAN) != 0 ||
	    ic->ic_bgscan_start == NULL || ic->ic_wcl_reassoc_owner_active ||
	    request->channel_count > IEEE80211_WCL_REASSOC_MAX_CHANSPECS ||
	    request->candidate_count > IEEE80211_WCL_REASSOC_MAX_CANDIDATES) {
		IOSimpleLockUnlockEnableInterrupt(ic->ic_pae_selected_bss_lock, irq);
		return EBUSY;
	}
	if ((ic->ic_flags & IEEE80211_F_RSNON) != 0 &&
	    !ic->ic_bss->ni_port_valid) {
		IOSimpleLockUnlockEnableInterrupt(ic->ic_pae_selected_bss_lock, irq);
		return EBUSY;
	}
	if (ic->ic_wcl_reassoc_next_serial == ~(u_int64_t)0) {
		IOSimpleLockUnlockEnableInterrupt(ic->ic_pae_selected_bss_lock, irq);
		return EOVERFLOW;
	}

	serial = ++ic->ic_wcl_reassoc_next_serial;
	source_epoch = ic->ic_pae_assoc_epoch;
	ic->ic_wcl_reassoc_owner_serial = serial;
	ic->ic_wcl_reassoc_source_epoch = source_epoch;
	ic->ic_wcl_reassoc_terminal_serial = 0;
	ic->ic_wcl_reassoc_scan_accepted_serial = 0;
	ic->ic_wcl_reassoc_request = *request;
	IEEE80211_ADDR_COPY(ic->ic_wcl_reassoc_source_bssid,
	    ic->ic_bss->ni_bssid);
	explicit_bzero(ic->ic_wcl_reassoc_target_bssid,
	    sizeof(ic->ic_wcl_reassoc_target_bssid));
	ic->ic_wcl_reassoc_owner_active = 1;
	ic->ic_wcl_reassoc_owner_last_leaf =
	    IEEE80211_WCL_REASSOC_OWNER_LEAF_SETUP;
	IOSimpleLockUnlockEnableInterrupt(ic->ic_pae_selected_bss_lock, irq);

	/* Discard the old census before the command can observe fresh candidates.
	 * Node-release callbacks may replace this admission, so check afterward. */
	ieee80211_free_allnodes(ic, 0);
	irq = IOSimpleLockLockDisableInterrupt(ic->ic_pae_selected_bss_lock);
	if (!ic->ic_wcl_reassoc_owner_active ||
	    ic->ic_wcl_reassoc_owner_serial != serial ||
	    ic->ic_pae_assoc_epoch != source_epoch) {
		if (ic->ic_wcl_reassoc_owner_serial == serial)
			ieee80211_wcl_reassoc_clear_locked(ic);
		IOSimpleLockUnlockEnableInterrupt(ic->ic_pae_selected_bss_lock, irq);
		return ECANCELED;
	}
	IOSimpleLockUnlockEnableInterrupt(ic->ic_pae_selected_bss_lock, irq);

	/* Explicit WCL reassociation is user/airportd intent and is therefore not
	 * suppressed by the autonomous-roam preference.  Every HAL already owns
	 * a real associated background scan through this callback. */
	error = (*ic->ic_bgscan_start)(ic, serial);
	irq = IOSimpleLockLockDisableInterrupt(ic->ic_pae_selected_bss_lock);
	/* A real tagged physical terminal may already have selected the target,
	 * retired the request and advanced the association epoch. Do not rearm it
	 * when the submitting callback eventually returns. */
	if (ic->ic_wcl_reassoc_next_serial == serial &&
	    ic->ic_wcl_reassoc_scan_accepted_serial == serial) {
		IOSimpleLockUnlockEnableInterrupt(ic->ic_pae_selected_bss_lock, irq);
		return 0;
	}
	if (!ic->ic_wcl_reassoc_owner_active ||
	    ic->ic_wcl_reassoc_owner_serial != serial ||
	    ic->ic_pae_assoc_epoch != source_epoch) {
		/* A returned lower result cannot commit or erase another admission. */
		if (ic->ic_wcl_reassoc_owner_serial == serial)
			ieee80211_wcl_reassoc_clear_locked(ic);
		IOSimpleLockUnlockEnableInterrupt(ic->ic_pae_selected_bss_lock, irq);
		return ECANCELED;
	}
	if (error != 0) {
		ieee80211_wcl_reassoc_clear_locked(ic);
		IOSimpleLockUnlockEnableInterrupt(ic->ic_pae_selected_bss_lock, irq);
		return error;
	}
	ic->ic_flags |= IEEE80211_F_BGSCAN;
	ic->ic_flags &= ~IEEE80211_F_DISABLE_BG_AUTO_CONNECT;
	ic->ic_wcl_reassoc_owner_last_leaf =
	    IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_STARTED;
	ic->ic_wcl_reassoc_scan_accepted_serial = serial;
	IOSimpleLockUnlockEnableInterrupt(ic->ic_pae_selected_bss_lock, irq);
	XYLog("wcl_reassoc REAL_SCAN_STARTED channels=%u candidates=%u flags=0x%x prune=%d\n",
	    request->channel_count, request->candidate_count,
	    request->feature_flags, request->prune_rssi_dbm);
	return 0;
}

/*
 * A new foreground WCL transaction supersedes an accepted firmware roam
 * scan.  Broadcom sends JoinAdapter and ScanAdapter commands to firmware
 * without a host-side WLC_REASSOC busy fence.  Intel's reassociation census
 * is host-owned, so perform the equivalent replacement explicitly: retire
 * the lower scan before the foreground request claims the radio, then close
 * the already-sent reassociation owner exactly once.
 *
 * Do not cancel a roam after target switching or an OTA reassociation has
 * started.  At that point the source association is no longer a stable join
 * cache and the caller must retry after its ordinary terminal event.
 */
int
ieee80211_cancel_wcl_reassoc_bgscan(struct ieee80211com *ic,
    u_int32_t result)
{
	int error;
	u_int64_t serial;
	IOInterruptState irq;
	int background;

	if (ic == NULL || ic->ic_pae_selected_bss_lock == NULL)
		return EINVAL;
	irq = IOSimpleLockLockDisableInterrupt(ic->ic_pae_selected_bss_lock);
	if (!ic->ic_wcl_reassoc_owner_active) {
		IOSimpleLockUnlockEnableInterrupt(ic->ic_pae_selected_bss_lock, irq);
		return 0;
	}
	if (ic->ic_wcl_reassoc_owner_last_leaf !=
	    IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_STARTED &&
	    ic->ic_wcl_reassoc_owner_last_leaf !=
	    IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_FAILED) {
		IOSimpleLockUnlockEnableInterrupt(ic->ic_pae_selected_bss_lock, irq);
		return EBUSY;
	}
	serial = ic->ic_wcl_reassoc_owner_serial;
	background = (ic->ic_flags & IEEE80211_F_BGSCAN) != 0;
	IOSimpleLockUnlockEnableInterrupt(ic->ic_pae_selected_bss_lock, irq);

	if (background) {
		if (ic->ic_bgscan_abort == NULL)
			return EOPNOTSUPP;
		error = (*ic->ic_bgscan_abort)(ic, serial);
		if (error != 0)
			return error;
	}
	irq = IOSimpleLockLockDisableInterrupt(ic->ic_pae_selected_bss_lock);
	/* An abort can complete the old scan and admit a same-phase successor.
	 * Its flags and request belong to that successor even on the same BSSID. */
	if (!ic->ic_wcl_reassoc_owner_active ||
	    ic->ic_wcl_reassoc_owner_serial != serial) {
		const int replaced = ic->ic_wcl_reassoc_owner_active != 0;
		IOSimpleLockUnlockEnableInterrupt(ic->ic_pae_selected_bss_lock, irq);
		return replaced ? EBUSY : 0;
	}
	if (ic->ic_wcl_reassoc_owner_last_leaf !=
	    IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_STARTED &&
	    ic->ic_wcl_reassoc_owner_last_leaf !=
	    IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_FAILED) {
		IOSimpleLockUnlockEnableInterrupt(ic->ic_pae_selected_bss_lock, irq);
		return EBUSY;
	}
	ic->ic_flags &= ~(IEEE80211_F_BGSCAN |
	    IEEE80211_F_DISABLE_BG_AUTO_CONNECT);
	ic->ic_wcl_reassoc_owner_last_leaf =
	    IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_FAILED;
	IOSimpleLockUnlockEnableInterrupt(ic->ic_pae_selected_bss_lock, irq);
	XYLog("wcl_reassoc SUPERSEDED_BY_WCL_REQUEST\n");
	ieee80211_wcl_reassoc_post_failure_owned(ic, serial,
	    result != 0 ? result : (u_int32_t)ECANCELED);
	return 0;
}

void
ieee80211_begin_cache_bgscan(struct _ifnet *ifp)
{
    struct ieee80211com *ic = (struct ieee80211com *)ifp;
    struct timeval tv;
    
    if ((ic->ic_flags & IEEE80211_F_BGSCAN) ||
        ic->ic_state != IEEE80211_S_RUN || ic->ic_mgt_timer != 0)
        return;
    
    if ((ic->ic_flags & IEEE80211_F_RSNON) && !ic->ic_bss->ni_port_valid)
        return;
    
    ic->ic_flags |= IEEE80211_F_DISABLE_BG_AUTO_CONNECT;
    
    //if last cache scan is 5 minutes ago, clear the nodes and rescan.
    microtime(&tv);
    if (ic->ic_last_cache_scan_ts > 0 && tv.tv_sec - ic->ic_last_cache_scan_ts > 5 * 60) {
        ieee80211_free_allnodes(ic, 0);
    }
    ic->ic_last_cache_scan_ts = tv.tv_sec;
    
    if (ic->ic_bgscan_start != NULL && ic->ic_bgscan_start(ic, 0) == 0) {
        ic->ic_flags |= IEEE80211_F_BGSCAN;
    }
}

/*
 * Start one driver-owned BSS Transition candidate census while preserving
 * the live source association.  Unlike a cache-only WCL scan, completion is
 * allowed to steer the connection, but node selection remains constrained
 * by the protected WNM target record.
 */
int
ieee80211_begin_wnm_bgscan(struct _ifnet *ifp)
{
    struct ieee80211com *ic = (struct ieee80211com *)ifp;
    int error;

    if (ic == NULL || ic->ic_state != IEEE80211_S_RUN ||
        ic->ic_mgt_timer != 0 || (ic->ic_flags & IEEE80211_F_BGSCAN) != 0 ||
        ic->ic_bgscan_start == NULL)
        return EBUSY;
	if ((ic->ic_flags & IEEE80211_F_RSNON) != 0 &&
	    (ic->ic_bss == NULL || !ic->ic_bss->ni_port_valid))
		return EBUSY;

	if (!ieee80211_wnm_bss_transition_scan_start(ic))
		return EBUSY;
	error = ic->ic_bgscan_start(ic, 0);
	ieee80211_wnm_bss_transition_scan_end(ic);
	if (error != 0)
		return error;

    /* Keep only the live ic_bss; every target must be observed afresh. */
    ieee80211_free_allnodes(ic, 0);
    ic->ic_flags |= IEEE80211_F_BGSCAN;
    ic->ic_flags &= ~IEEE80211_F_DISABLE_BG_AUTO_CONNECT;
    return 0;
}

void
ieee80211_wnm_bgscan_retry_timeout(void *arg)
{
    struct _ifnet *ifp = (struct _ifnet *)arg;
    struct ieee80211com *ic = (struct ieee80211com *)ifp;
    u_int8_t dialog_token = 0;
    int error;

    if (ic == NULL ||
        !ieee80211_wnm_bss_transition_fresh_scan_pending(ic))
        return;

    error = ieee80211_begin_wnm_bgscan(ifp);
    if (error == 0) {
        ieee80211_wnm_bss_transition_fresh_scan_started(ic);
        return;
    }
    if (error == EBUSY &&
        ieee80211_wnm_bss_transition_retry_fresh_scan(ic)) {
        timeout_add_msec(&ic->ic_wnm_bgscan_retry_timeout, 100);
        return;
    }

    if (ieee80211_wnm_bss_transition_active(ic, &dialog_token) &&
        ic->ic_state == IEEE80211_S_RUN && ic->ic_bss != NULL)
        (void)ieee80211_send_bss_transition_response(ic, ic->ic_bss,
            dialog_token, IEEE80211_WNM_BSS_TM_REJECT_NO_SUITABLE, NULL);
    ieee80211_wnm_bss_transition_clear(ic);
}

void
ieee80211_bgscan_timeout(void *arg)
{
    struct _ifnet *ifp = (struct _ifnet *)arg;
    
    ieee80211_begin_bgscan(ifp);
}

void
ieee80211_channel_init(struct _ifnet *ifp)
{
    struct ieee80211com *ic = (struct ieee80211com *)ifp;
    struct ieee80211_channel *c;
    int i;
    
    /*
     * Fill in 802.11 available channel set, mark
     * all available channels as active, and pick
     * a default channel if not already specified.
     */
    memset(ic->ic_chan_avail, 0, sizeof(ic->ic_chan_avail));
    ic->ic_modecaps |= 1<<IEEE80211_MODE_AUTO;
    for (i = 0; i <= IEEE80211_CHAN_MAX; i++) {
        c = &ic->ic_channels[i];
        if (c->ic_flags) {
            /*
             * Verify driver passed us valid data.
             */
            if (i != ieee80211_chan2ieee(ic, c)) {
                XYLog("%s: bad channel ignored; "
                      "freq %u flags %x number %u\n",
                      ifp->if_xname, c->ic_freq, c->ic_flags,
                      i);
                c->ic_flags = 0;    /* NB: remove */
                continue;
            }
            setbit(ic->ic_chan_avail, i);
            /*
             * Identify mode capabilities.
             */
            if (IEEE80211_IS_CHAN_A(c))
                ic->ic_modecaps |= 1<<IEEE80211_MODE_11A;
            if (IEEE80211_IS_CHAN_B(c))
                ic->ic_modecaps |= 1<<IEEE80211_MODE_11B;
            if (IEEE80211_IS_CHAN_PUREG(c))
                ic->ic_modecaps |= 1<<IEEE80211_MODE_11G;
            if (IEEE80211_IS_CHAN_N(c))
                ic->ic_modecaps |= 1<<IEEE80211_MODE_11N;
            if (IEEE80211_IS_CHAN_AC(c))
                ic->ic_modecaps |= 1<<IEEE80211_MODE_11AC | 1<<IEEE80211_MODE_11AX;
        }
    }
    /* validate ic->ic_curmode */
    if ((ic->ic_modecaps & (1<<ic->ic_curmode)) == 0)
        ic->ic_curmode = IEEE80211_MODE_AUTO;
    ic->ic_des_chan = IEEE80211_CHAN_ANYC;    /* any channel is ok */
}

void
ieee80211_ifattach(struct _ifnet *ifp, IOEthernetController *controller)
{
    struct ieee80211com *ic = (struct ieee80211com *)ifp;
    
    ifp->controller = controller;
    ic->ic_newstate_preflight = NULL;
    ic->ic_wcl_scan_suppress_scan_done_once = 0;
    ic->ic_wcl_scan_active = 0;
    memset(&ic->ic_wcl_scan_plan, 0, sizeof(ic->ic_wcl_scan_plan));
    ic->ic_initial_scan_census_only = 0;
    ic->ic_wcl_reassoc_next_serial = 0;
    ic->ic_wcl_reassoc_owner_serial = 0;
    ic->ic_wcl_reassoc_source_epoch = 0;
    ic->ic_wcl_reassoc_terminal_serial = 0;
    ic->ic_wcl_reassoc_scan_accepted_serial = 0;
    ic->ic_wcl_reassoc_owner_active = 0;
    ic->ic_wcl_reassoc_owner_last_leaf =
        IEEE80211_WCL_REASSOC_OWNER_LEAF_IDLE;
    memset(&ic->ic_wcl_reassoc_request, 0,
           sizeof(ic->ic_wcl_reassoc_request));
    memset(ic->ic_wcl_reassoc_source_bssid, 0,
           sizeof(ic->ic_wcl_reassoc_source_bssid));
    memset(ic->ic_wcl_reassoc_target_bssid, 0,
           sizeof(ic->ic_wcl_reassoc_target_bssid));
    /* A missing leaf lock leaves the dormant snapshot unpublishable. */
	if (ic->ic_pae_selected_bss_lock == NULL)
		ic->ic_pae_selected_bss_lock = IOSimpleLockAlloc();
    __atomic_store_n(&ic->ic_pae_assoc_epoch, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&ic->ic_pae_assoc_replace_epoch, 0,
	    __ATOMIC_RELAXED);
    memset(&ic->ic_pae_selected_bss, 0, sizeof(ic->ic_pae_selected_bss));
    memset(&ic->ic_sae_peer_rx_admission, 0,
           sizeof(ic->ic_sae_peer_rx_admission));
    memset(&ic->ic_public_initial_bssid_pin, 0,
           sizeof(ic->ic_public_initial_bssid_pin));
    memset(&ic->ic_wnm_bss_transition, 0,
           sizeof(ic->ic_wnm_bss_transition));
    ic->ic_sae_wcl_request_next_generation = 0;
    ic->ic_sae_wcl_policy_generation = 0;
    memset(&ic->ic_sae_wcl_request, 0,
           sizeof(ic->ic_sae_wcl_request));
    memset(&ic->ic_sae_wcl_pmk_claim, 0,
           sizeof(ic->ic_sae_wcl_pmk_claim));
    ic->ic_sae_wcl_fresh_carrier_required = 0;
    ic->ic_sae_wcl_request_policy_starting = 0;
    ic->ic_sae_wcl_request_join_active = 0;
    memset(ic->ic_bss_blacklist_requested, 0,
           sizeof(ic->ic_bss_blacklist_requested));
    ic->ic_bss_blacklist_count = 0;
    memset(ic->ic_bss_blacklist_bssid, 0,
           sizeof(ic->ic_bss_blacklist_bssid));
    ic->ic_bss_blacklist_event_count = 0;
    memset(ic->ic_bss_blacklist_event_body, 0,
           sizeof(ic->ic_bss_blacklist_event_body));
    ifp->if_skywalk_rx = NULL;
    ifp->if_skywalk_rx_ap = NULL;
    ifq_init(&ifp->if_snd, ifp, 2048);
    memcpy(((struct arpcom *)ifp)->ac_enaddr, ic->ic_myaddr,
           ETHER_ADDR_LEN);
    if (ifp->if_sadl) {
        ::free(ifp->if_sadl);
    }
    ifp->if_sadl = (struct sockaddr_dl *)::malloc(sizeof(struct sockaddr_dl), 0, 0);
    memcpy(LLADDR(ifp->if_sadl), ic->ic_myaddr, ETHER_ADDR_LEN);
    
    ifp->if_output = ieee80211_output;
    
#if NBPFILTER > 0
    bpfattach(&ic->ic_rawbpf, ifp, DLT_IEEE802_11,
              sizeof(struct ieee80211_frame_addr4));
#endif
    ieee80211_crypto_attach(ifp);
    
    ieee80211_channel_init(ifp);
    
    /* IEEE 802.11 defines a MTU >= 2290 */
    //	ifp->if_capabilities |= IFCAP_VLAN_MTU;
    
    ieee80211_setbasicrates(ic);
    (void)ieee80211_setmode(ic, (enum ieee80211_phymode)ic->ic_curmode);
    
    if (ic->ic_lintval == 0)
        ic->ic_lintval = 100;		/* default sleep */
    ic->ic_bmissthres = IEEE80211_BEACON_MISS_THRES;
    ic->ic_dtim_period = 1;	/* all TIMs are DTIMs */
    
    ieee80211_node_attach(ifp);
    ieee80211_proto_attach(ifp);
    
    //	if_addgroup(ifp, "wlan");
    //	ifp->if_priority = IF_WIRELESS_DEFAULT_PRIORITY;
    
    ieee80211_set_link_state(ic, LINK_STATE_DOWN);
    
    timeout_set(&ic->ic_bgscan_timeout, ieee80211_bgscan_timeout, ifp);
    timeout_set(&ic->ic_wnm_bgscan_retry_timeout,
                ieee80211_wnm_bgscan_retry_timeout, ifp);
}

void
ieee80211_ifdetach(struct _ifnet *ifp)
{
    struct ieee80211com *ic = (struct ieee80211com *)ifp;
    
    /* Close future async STA owners before queues, crypto, and nodes vanish. */
    ieee80211_public_initial_bssid_pin_disarm(ic);
    ieee80211_wnm_bss_transition_clear(ic);
    ieee80211_wcl_scan_plan_clear(ic, 0);
    ic->ic_wcl_reassoc_owner_serial = 0;
    ic->ic_wcl_reassoc_source_epoch = 0;
    ic->ic_wcl_reassoc_terminal_serial = 0;
    ic->ic_wcl_reassoc_scan_accepted_serial = 0;
    ic->ic_wcl_reassoc_owner_active = 0;
    ic->ic_wcl_reassoc_owner_last_leaf =
        IEEE80211_WCL_REASSOC_OWNER_LEAF_IDLE;
    explicit_bzero(&ic->ic_wcl_reassoc_request,
                   sizeof(ic->ic_wcl_reassoc_request));
    explicit_bzero(ic->ic_wcl_reassoc_source_bssid,
                   sizeof(ic->ic_wcl_reassoc_source_bssid));
    explicit_bzero(ic->ic_wcl_reassoc_target_bssid,
                   sizeof(ic->ic_wcl_reassoc_target_bssid));
    ieee80211_roam_link_cancel(ic);
    (void)ieee80211_pae_assoc_epoch_begin(ic);
    timeout_del(&ic->ic_wnm_bgscan_retry_timeout);
    timeout_free(&ic->ic_wnm_bgscan_retry_timeout);
    timeout_del(&ic->ic_bgscan_timeout);
    timeout_free(&ic->ic_bgscan_timeout);
    ieee80211_proto_detach(ifp);
    ieee80211_crypto_detach(ifp);
    ieee80211_node_detach(ifp);
    ifmedia_delete_instance(&ic->ic_media, IFM_INST_ANY);
    ifq_destroy(&ifp->if_snd);
    if (ifp->if_slowtimo) {
        ifp->if_slowtimo->release();
        ifp->if_slowtimo = NULL;
    }
    if (ifp->if_sadl) {
        ::free(ifp->if_sadl);
        ifp->if_sadl = NULL;
    }
    ifp->netStat = NULL;
    ifp->controller = NULL;
    ifp->iface = NULL;
    /* HAL queues can still reject work after ifdetach; terminal owner frees it. */
}

/*
 * Convert MHz frequency to IEEE channel number.
 */
u_int
ieee80211_mhz2ieee(u_int freq, u_int flags)
{
    if (flags & IEEE80211_CHAN_2GHZ) {    /* 2GHz band */
        if (freq == 2484)
            return 14;
        if (freq < 2484)
            return (freq - 2407) / 5;
        else
            return 15 + ((freq - 2512) / 20);
    } else if (flags & IEEE80211_CHAN_5GHZ) {    /* 5GHz band */
        return (freq - 5000) / 5;
    } else {                /* either, guess */
        if (freq == 2484)
            return 14;
        if (freq < 2484)
            return (freq - 2407) / 5;
        if (freq < 5000)
            return 15 + ((freq - 2512) / 20);
        return (freq - 5000) / 5;
    }
}

/*
 * Convert channel to IEEE channel number.
 */
u_int
ieee80211_chan2ieee(struct ieee80211com *ic, const struct ieee80211_channel *c)
{
    struct _ifnet *ifp = &ic->ic_if;
    if (ic->ic_channels <= c && c <= &ic->ic_channels[IEEE80211_CHAN_MAX])
        return c - ic->ic_channels;
    else if (c == IEEE80211_CHAN_ANYC)
        return IEEE80211_CHAN_ANY;
    
    XYLog("严重%s: bogus channel pointer", ifp->if_xname);
    return 1;
}

/*
 * Convert IEEE channel number to MHz frequency.
 */
u_int
ieee80211_ieee2mhz(u_int chan, u_int flags)
{
    if (flags & IEEE80211_CHAN_2GHZ) {    /* 2GHz band */
        if (chan == 14)
            return 2484;
        if (chan < 14)
            return 2407 + chan*5;
        else
            return 2512 + ((chan-15)*20);
    } else if (flags & IEEE80211_CHAN_5GHZ) {/* 5GHz band */
        return 5000 + (chan*5);
    } else {                /* either, guess */
        if (chan == 14)
            return 2484;
        if (chan < 14)            /* 0-13 */
            return 2407 + chan*5;
        if (chan < 27)            /* 15-26 */
            return 2512 + ((chan-15)*20);
        return 5000 + (chan*5);
    }
}

void
ieee80211_configure_ampdu_tx(struct ieee80211com *ic, int enable)
{
    if ((ic->ic_caps & IEEE80211_C_TX_AMPDU) == 0)
        return;
    
    /* Sending AMPDUs requires QoS support. */
    if ((ic->ic_caps & IEEE80211_C_QOS) == 0)
        return;
    
    if (enable)
        ic->ic_flags |= IEEE80211_F_QOS;
    else
        ic->ic_flags &= ~IEEE80211_F_QOS;
}

/*
 * Setup the media data structures according to the channel and
 * rate tables.  This must be called by the driver after
 * ieee80211_attach and before most anything else.
 */
void
ieee80211_media_init(struct _ifnet *ifp)
{
#define    ADD(_ic, _s, _o) \
ifmedia_add(&(_ic)->ic_media, \
IFM_MAKEWORD(IFM_IEEE80211, (_s), (_o), 0), 0, NULL)
    struct ieee80211com *ic = (struct ieee80211com *)ifp;
    struct ifmediareq imr;
    int i, j, mode, rate, maxrate, r;
    uint64_t mword, mopt;
    const struct ieee80211_rateset *rs;
    struct ieee80211_rateset allrates;
    
    /*
     * Do late attach work that must wait for any subclass
     * (i.e. driver) work such as overriding methods.
     */
    ieee80211_node_lateattach(ifp);
    
    /*
     * Fill in media characteristics.
     */
    ifmedia_init(&ic->ic_media, 0);
    maxrate = 0;
    memset(&allrates, 0, sizeof(allrates));
    for (mode = IEEE80211_MODE_AUTO; mode <= IEEE80211_MODE_11G; mode++) {
        static const uint64_t mopts[] = {
            IFM_AUTO,
            IFM_IEEE80211_11A,
            IFM_IEEE80211_11B,
            IFM_IEEE80211_11G,
        };
        if ((ic->ic_modecaps & (1<<mode)) == 0)
            continue;
        mopt = mopts[mode];
        ADD(ic, IFM_AUTO, mopt);    /* e.g. 11a auto */
#ifndef IEEE80211_STA_ONLY
        if (ic->ic_caps & IEEE80211_C_IBSS)
            ADD(ic, IFM_AUTO, mopt | IFM_IEEE80211_IBSS);
        if (ic->ic_caps & IEEE80211_C_HOSTAP)
            ADD(ic, IFM_AUTO, mopt | IFM_IEEE80211_HOSTAP);
        if (ic->ic_caps & IEEE80211_C_AHDEMO)
            ADD(ic, IFM_AUTO, mopt | IFM_IEEE80211_ADHOC);
#endif
        if (ic->ic_caps & IEEE80211_C_MONITOR)
            ADD(ic, IFM_AUTO, mopt | IFM_IEEE80211_MONITOR);
        if (mode == IEEE80211_MODE_AUTO)
            continue;
        rs = &ic->ic_sup_rates[mode];
        for (i = 0; i < rs->rs_nrates; i++) {
            rate = rs->rs_rates[i];
            mword = ieee80211_rate2media(ic, rate, (enum ieee80211_phymode)mode);
            if (mword == 0)
                continue;
            ADD(ic, mword, mopt);
#ifndef IEEE80211_STA_ONLY
            if (ic->ic_caps & IEEE80211_C_IBSS)
                ADD(ic, mword, mopt | IFM_IEEE80211_IBSS);
            if (ic->ic_caps & IEEE80211_C_HOSTAP)
                ADD(ic, mword, mopt | IFM_IEEE80211_HOSTAP);
            if (ic->ic_caps & IEEE80211_C_AHDEMO)
                ADD(ic, mword, mopt | IFM_IEEE80211_ADHOC);
#endif
            if (ic->ic_caps & IEEE80211_C_MONITOR)
                ADD(ic, mword, mopt | IFM_IEEE80211_MONITOR);
            /*
             * Add rate to the collection of all rates.
             */
            r = rate & IEEE80211_RATE_VAL;
            for (j = 0; j < allrates.rs_nrates; j++)
                if (allrates.rs_rates[j] == r)
                    break;
            if (j == allrates.rs_nrates) {
                /* unique, add to the set */
                allrates.rs_rates[j] = r;
                allrates.rs_nrates++;
            }
            rate = (rate & IEEE80211_RATE_VAL) / 2;
            if (rate > maxrate)
                maxrate = rate;
        }
    }
    for (i = 0; i < allrates.rs_nrates; i++) {
        mword = ieee80211_rate2media(ic, allrates.rs_rates[i],
                                     IEEE80211_MODE_AUTO);
        if (mword == 0)
            continue;
        mword = IFM_SUBTYPE(mword);    /* remove media options */
        ADD(ic, mword, 0);
#ifndef IEEE80211_STA_ONLY
        if (ic->ic_caps & IEEE80211_C_IBSS)
            ADD(ic, mword, IFM_IEEE80211_IBSS);
        if (ic->ic_caps & IEEE80211_C_HOSTAP)
            ADD(ic, mword, IFM_IEEE80211_HOSTAP);
        if (ic->ic_caps & IEEE80211_C_AHDEMO)
            ADD(ic, mword, IFM_IEEE80211_ADHOC);
#endif
        if (ic->ic_caps & IEEE80211_C_MONITOR)
            ADD(ic, mword, IFM_IEEE80211_MONITOR);
    }
    
    if (ic->ic_modecaps & (1 << IEEE80211_MODE_11N)) {
        mopt = IFM_IEEE80211_11N;
        ADD(ic, IFM_AUTO, mopt);
#ifndef IEEE80211_STA_ONLY
        if (ic->ic_caps & IEEE80211_C_IBSS)
            ADD(ic, IFM_AUTO, mopt | IFM_IEEE80211_IBSS);
        if (ic->ic_caps & IEEE80211_C_HOSTAP)
            ADD(ic, IFM_AUTO, mopt | IFM_IEEE80211_HOSTAP);
#endif
        if (ic->ic_caps & IEEE80211_C_MONITOR)
            ADD(ic, IFM_AUTO, mopt | IFM_IEEE80211_MONITOR);
        for (i = 0; i < IEEE80211_HT_NUM_MCS; i++) {
            if (!isset(ic->ic_sup_mcs, i))
                continue;
            ADD(ic, IFM_IEEE80211_HT_MCS0 + i, mopt);
#ifndef IEEE80211_STA_ONLY
            if (ic->ic_caps & IEEE80211_C_IBSS)
                ADD(ic, IFM_IEEE80211_HT_MCS0 + i,
                    mopt | IFM_IEEE80211_IBSS);
            if (ic->ic_caps & IEEE80211_C_HOSTAP)
                ADD(ic, IFM_IEEE80211_HT_MCS0 + i,
                    mopt | IFM_IEEE80211_HOSTAP);
#endif
            if (ic->ic_caps & IEEE80211_C_MONITOR)
                ADD(ic, IFM_IEEE80211_HT_MCS0 + i,
                    mopt | IFM_IEEE80211_MONITOR);
        }
        ic->ic_flags |= IEEE80211_F_HTON; /* enable 11n by default */
        ieee80211_configure_ampdu_tx(ic, 1);
    }
    
    if (ic->ic_modecaps & (1 << IEEE80211_MODE_11AC)) {
        mopt = IFM_IEEE80211_11AC;
        ADD(ic, IFM_AUTO, mopt);
#ifndef IEEE80211_STA_ONLY
        if (ic->ic_caps & IEEE80211_C_IBSS)
            ADD(ic, IFM_AUTO, mopt | IFM_IEEE80211_IBSS);
        if (ic->ic_caps & IEEE80211_C_HOSTAP)
            ADD(ic, IFM_AUTO, mopt | IFM_IEEE80211_HOSTAP);
#endif
        if (ic->ic_caps & IEEE80211_C_MONITOR)
            ADD(ic, IFM_AUTO, mopt | IFM_IEEE80211_MONITOR);
        for (i = 0; i < IEEE80211_VHT_NUM_MCS; i++) {
#if 0
            /* TODO: Obtain VHT MCS information from VHT CAP IE. */
            if (!vht_mcs_supported)
                continue;
#endif
            ADD(ic, IFM_IEEE80211_VHT_MCS0 + i, mopt);
#ifndef IEEE80211_STA_ONLY
            if (ic->ic_caps & IEEE80211_C_IBSS)
                ADD(ic, IFM_IEEE80211_VHT_MCS0 + i,
                    mopt | IFM_IEEE80211_IBSS);
            if (ic->ic_caps & IEEE80211_C_HOSTAP)
                ADD(ic, IFM_IEEE80211_VHT_MCS0 + i,
                    mopt | IFM_IEEE80211_HOSTAP);
#endif
            if (ic->ic_caps & IEEE80211_C_MONITOR)
                ADD(ic, IFM_IEEE80211_VHT_MCS0 + i,
                    mopt | IFM_IEEE80211_MONITOR);
        }
        ieee80211_configure_ampdu_tx(ic, 1);
    }
    if (ic->ic_modecaps & (1 << IEEE80211_MODE_11AX)) {
        mopt = IFM_IEEE80211_11AX;
        ADD(ic, IFM_AUTO, mopt);
        for (i = 0; i < IEEE80211_VHT_NUM_MCS; i++) {
#if 0
            /* TODO: Obtain VHT MCS information from VHT CAP IE. */
            if (!vht_mcs_supported)
                continue;
#endif
            ADD(ic, IFM_IEEE80211_VHT_MCS0 + i, mopt);
#ifndef IEEE80211_STA_ONLY
            if (ic->ic_caps & IEEE80211_C_IBSS)
                ADD(ic, IFM_IEEE80211_VHT_MCS0 + i,
                    mopt | IFM_IEEE80211_IBSS);
            if (ic->ic_caps & IEEE80211_C_HOSTAP)
                ADD(ic, IFM_IEEE80211_VHT_MCS0 + i,
                    mopt | IFM_IEEE80211_HOSTAP);
#endif
            if (ic->ic_caps & IEEE80211_C_MONITOR)
                ADD(ic, IFM_IEEE80211_VHT_MCS0 + i,
                    mopt | IFM_IEEE80211_MONITOR);
        }
        ieee80211_configure_ampdu_tx(ic, 1);
    }
    
    ieee80211_media_status(ifp, &imr);
    ifmedia_set(&ic->ic_media, imr.ifm_active);
    
    //	if (maxrate)
    //		ifp->if_baudrate = IF_Mbps(maxrate);
    
    
#undef ADD
}

int
ieee80211_findrate(struct ieee80211com *ic, enum ieee80211_phymode mode,
                   int rate)
{
#define    IEEERATE(_ic,_m,_i) \
((_ic)->ic_sup_rates[_m].rs_rates[_i] & IEEE80211_RATE_VAL)
    int i, nrates = ic->ic_sup_rates[mode].rs_nrates;
    for (i = 0; i < nrates; i++)
        if (IEEERATE(ic, mode, i) == rate)
            return i;
    return -1;
#undef IEEERATE
}

/*
 * Handle a media change request.
 */
int
ieee80211_media_change(struct _ifnet *ifp)
{
    struct ieee80211com *ic = (struct ieee80211com *)ifp;
    struct ifmedia_entry *ime;
    enum ieee80211_opmode newopmode;
    enum ieee80211_phymode newphymode;
    int i, j, newrate, error = 0;
    
    ime = ic->ic_media.ifm_cur;
    /*
     * First, identify the phy mode.
     */
    switch (IFM_MODE(ime->ifm_media)) {
        case IFM_IEEE80211_11A:
            newphymode = IEEE80211_MODE_11A;
            break;
        case IFM_IEEE80211_11B:
            newphymode = IEEE80211_MODE_11B;
            break;
        case IFM_IEEE80211_11G:
            newphymode = IEEE80211_MODE_11G;
            break;
        case IFM_IEEE80211_11N:
            newphymode = IEEE80211_MODE_11N;
            break;
        case IFM_IEEE80211_11AC:
            newphymode = IEEE80211_MODE_11AC;
            break;
        case IFM_IEEE80211_11AX:
            newphymode = IEEE80211_MODE_11AX;
            break;
        case IFM_AUTO:
            newphymode = IEEE80211_MODE_AUTO;
            break;
        default:
            return EINVAL;
    }
    
    /*
     * Validate requested mode is available.
     */
    if ((ic->ic_modecaps & (1<<newphymode)) == 0)
        return EINVAL;
    
    /*
     * Next, the fixed/variable rate.
     */
    i = -1;
    if (IFM_SUBTYPE(ime->ifm_media) >= IFM_IEEE80211_VHT_MCS0 &&
        IFM_SUBTYPE(ime->ifm_media) <= IFM_IEEE80211_VHT_MCS9) {
        if (((ic->ic_modecaps & (1 << IEEE80211_MODE_11AC)) == 0) && ((ic->ic_modecaps & (1 << IEEE80211_MODE_11AX)) == 0))
            return EINVAL;
        if (newphymode != IEEE80211_MODE_AUTO &&
            newphymode != IEEE80211_MODE_11AC &&
            newphymode != IEEE80211_MODE_11AX)
            return EINVAL;
        i = ieee80211_media2mcs(ime->ifm_media);
        /* TODO: Obtain VHT MCS information from VHT CAP IE. */
        if (i == -1 /* || !vht_mcs_supported */)
            return EINVAL;
    } else if (IFM_SUBTYPE(ime->ifm_media) >= IFM_IEEE80211_HT_MCS0 &&
               IFM_SUBTYPE(ime->ifm_media) <= IFM_IEEE80211_HT_MCS76) {
        if ((ic->ic_modecaps & (1 << IEEE80211_MODE_11N)) == 0)
            return EINVAL;
        if (newphymode != IEEE80211_MODE_AUTO &&
            newphymode != IEEE80211_MODE_11N)
            return EINVAL;
        i = ieee80211_media2mcs(ime->ifm_media);
        if (i == -1 || isclr(ic->ic_sup_mcs, i))
            return EINVAL;
    } else if (IFM_SUBTYPE(ime->ifm_media) != IFM_AUTO) {
        /*
         * Convert media subtype to rate.
         */
        newrate = ieee80211_media2rate(ime->ifm_media);
        if (newrate == 0)
            return EINVAL;
        /*
         * Check the rate table for the specified/current phy.
         */
        if (newphymode == IEEE80211_MODE_AUTO) {
            /*
             * In autoselect mode search for the rate.
             */
            for (j = IEEE80211_MODE_11A;
                 j < IEEE80211_MODE_MAX; j++) {
                if ((ic->ic_modecaps & (1<<j)) == 0)
                    continue;
                i = ieee80211_findrate(ic, (enum ieee80211_phymode)j, newrate);
                if (i != -1) {
                    /* lock mode too */
                    newphymode = (enum ieee80211_phymode)j;
                    break;
                }
            }
        } else {
            i = ieee80211_findrate(ic, newphymode, newrate);
        }
        if (i == -1)            /* mode/rate mismatch */
            return EINVAL;
    }
    /* NB: defer rate setting to later */
    
    /*
     * Deduce new operating mode but don't install it just yet.
     */
#ifndef IEEE80211_STA_ONLY
    if (ime->ifm_media & IFM_IEEE80211_ADHOC)
        newopmode = IEEE80211_M_AHDEMO;
    else if (ime->ifm_media & IFM_IEEE80211_HOSTAP)
        newopmode = IEEE80211_M_HOSTAP;
    else if (ime->ifm_media & IFM_IEEE80211_IBSS)
        newopmode = IEEE80211_M_IBSS;
    else
#endif
        if (ime->ifm_media & IFM_IEEE80211_MONITOR)
            newopmode = IEEE80211_M_MONITOR;
        else
            newopmode = IEEE80211_M_STA;
    
#ifndef IEEE80211_STA_ONLY
    /*
     * Autoselect doesn't make sense when operating as an AP.
     * If no phy mode has been selected, pick one and lock it
     * down so rate tables can be used in forming beacon frames
     * and the like.
     */
    if (newopmode == IEEE80211_M_HOSTAP &&
        newphymode == IEEE80211_MODE_AUTO) {
        if (ic->ic_modecaps & (1 << IEEE80211_MODE_11AX))
            newphymode = IEEE80211_MODE_11AX;
        if (ic->ic_modecaps & (1 << IEEE80211_MODE_11AC))
            newphymode = IEEE80211_MODE_11AC;
        else if (ic->ic_modecaps & (1 << IEEE80211_MODE_11N))
            newphymode = IEEE80211_MODE_11N;
        else if (ic->ic_modecaps & (1 << IEEE80211_MODE_11A))
            newphymode = IEEE80211_MODE_11A;
        else if (ic->ic_modecaps & (1 << IEEE80211_MODE_11G))
            newphymode = IEEE80211_MODE_11G;
        else
            newphymode = IEEE80211_MODE_11B;
    }
#endif
    
    /*
     * Handle phy mode change.
     */
    if (ic->ic_curmode != newphymode) {        /* change phy mode */
        error = ieee80211_setmode(ic, newphymode);
        if (error != 0)
            return error;
        error = ENETRESET;
    }
    
    /*
     * Committed to changes, install the MCS/rate setting.
     */
    ic->ic_flags &= ~(IEEE80211_F_HTON | IEEE80211_F_VHTON);
    ieee80211_configure_ampdu_tx(ic, 0);
    if ((ic->ic_modecaps & (1 << IEEE80211_MODE_11AX)) &&
        (newphymode == IEEE80211_MODE_AUTO ||
         newphymode == IEEE80211_MODE_11AX)) {
        ic->ic_flags |= IEEE80211_F_HEON;
        ieee80211_configure_ampdu_tx(ic, 1);
    } else if ((ic->ic_modecaps & (1 << IEEE80211_MODE_11AC)) &&
        (newphymode == IEEE80211_MODE_AUTO ||
         newphymode == IEEE80211_MODE_11AC)) {
        ic->ic_flags |= IEEE80211_F_VHTON;
        ieee80211_configure_ampdu_tx(ic, 1);
    } else if ((ic->ic_modecaps & (1 << IEEE80211_MODE_11N)) &&
               (newphymode == IEEE80211_MODE_AUTO ||
                newphymode == IEEE80211_MODE_11N)) {
        ic->ic_flags |= IEEE80211_F_HTON;
        ieee80211_configure_ampdu_tx(ic, 1);
    }
    if ((ic->ic_flags & (IEEE80211_F_HTON | IEEE80211_F_VHTON)) == 0) {
        ic->ic_fixed_mcs = -1;
        if (ic->ic_fixed_rate != i) {
            ic->ic_fixed_rate = i;        /* set fixed tx rate */
            error = ENETRESET;
        }
    } else {
        ic->ic_fixed_rate = -1;
        if (ic->ic_fixed_mcs != i) {
            ic->ic_fixed_mcs = i;        /* set fixed mcs */
            error = ENETRESET;
        }
    }
    
    /*
     * Handle operating mode change.
     */
    if (ic->ic_opmode != newopmode) {
        /* The STA guard must run while the old owner is still visible. */
        if (ic->ic_opmode == IEEE80211_M_STA &&
            newopmode != IEEE80211_M_STA) {
            ieee80211_roam_link_cancel(ic);
            (void)ieee80211_pae_assoc_epoch_begin(ic);
        }
        ic->ic_opmode = newopmode;
#ifndef IEEE80211_STA_ONLY
        switch (newopmode) {
            case IEEE80211_M_AHDEMO:
            case IEEE80211_M_HOSTAP:
            case IEEE80211_M_STA:
            case IEEE80211_M_MONITOR:
                ic->ic_flags &= ~IEEE80211_F_IBSSON;
                break;
            case IEEE80211_M_IBSS:
                ic->ic_flags |= IEEE80211_F_IBSSON;
                break;
        }
#endif
        /*
         * Yech, slot time may change depending on the
         * operating mode so reset it to be sure everything
         * is setup appropriately.
         */
        ieee80211_reset_erp(ic);
        error = ENETRESET;
    }
#ifdef notdef
    if (error == 0)
        ifp->if_baudrate = ifmedia_baudrate(ime->ifm_media);
#endif
    return error;
}

void
ieee80211_media_status(struct _ifnet *ifp, struct ifmediareq *imr)
{
    struct ieee80211com *ic = (struct ieee80211com *)ifp;
    const struct ieee80211_node *ni = NULL;
    
    imr->ifm_status = IFM_AVALID;
    imr->ifm_active = IFM_IEEE80211;
    if (ic->ic_state == IEEE80211_S_RUN &&
        (ic->ic_opmode != IEEE80211_M_STA ||
         !(ic->ic_flags & IEEE80211_F_RSNON) ||
         ic->ic_bss->ni_port_valid))
        imr->ifm_status |= IFM_ACTIVE;
    imr->ifm_active |= IFM_AUTO;
    switch (ic->ic_opmode) {
        case IEEE80211_M_STA:
            ni = ic->ic_bss;
            if (ic->ic_curmode == IEEE80211_MODE_11N ||
                ic->ic_curmode == IEEE80211_MODE_11AC ||
                ic->ic_curmode == IEEE80211_MODE_11AX)
                imr->ifm_active |= ieee80211_mcs2media(ic,
                                                       ni->ni_txmcs, (enum ieee80211_phymode)ic->ic_curmode);
            else if (ni->ni_flags & IEEE80211_NODE_HE) /* in MODE_AUTO */
                imr->ifm_active |= ieee80211_mcs2media(ic,
                                                       ni->ni_txmcs, IEEE80211_MODE_11AX);
            else if (ni->ni_flags & IEEE80211_NODE_VHT) /* in MODE_AUTO */
                imr->ifm_active |= ieee80211_mcs2media(ic,
                                                       ni->ni_txmcs, IEEE80211_MODE_11AC);
            else if (ni->ni_flags & IEEE80211_NODE_HT) /* in MODE_AUTO */
                imr->ifm_active |= ieee80211_mcs2media(ic,
                                                       ni->ni_txmcs, IEEE80211_MODE_11N);
            else
            /* calculate rate subtype */
                imr->ifm_active |= ieee80211_rate2media(ic,
                                                        ni->ni_rates.rs_rates[ni->ni_txrate],
                                                        (enum ieee80211_phymode)ic->ic_curmode);
            break;
#ifndef IEEE80211_STA_ONLY
        case IEEE80211_M_IBSS:
            imr->ifm_active |= IFM_IEEE80211_IBSS;
            break;
        case IEEE80211_M_AHDEMO:
            imr->ifm_active |= IFM_IEEE80211_ADHOC;
            break;
        case IEEE80211_M_HOSTAP:
            imr->ifm_active |= IFM_IEEE80211_HOSTAP;
            break;
#endif
        case IEEE80211_M_MONITOR:
            imr->ifm_active |= IFM_IEEE80211_MONITOR;
            break;
        default:
            break;
    }
    switch (ic->ic_curmode) {
        case IEEE80211_MODE_11A:
            imr->ifm_active |= IFM_IEEE80211_11A;
            break;
        case IEEE80211_MODE_11B:
            imr->ifm_active |= IFM_IEEE80211_11B;
            break;
        case IEEE80211_MODE_11G:
            imr->ifm_active |= IFM_IEEE80211_11G;
            break;
        case IEEE80211_MODE_11N:
            imr->ifm_active |= IFM_IEEE80211_11N;
            break;
        case IEEE80211_MODE_11AC:
            imr->ifm_active |= IFM_IEEE80211_11AC;
            break;
        case IEEE80211_MODE_11AX:
            imr->ifm_active |= IFM_IEEE80211_11AX;
            break;
    }
}

int
ieee80211_assoc_comeback_set_deadline(struct ieee80211com *ic,
    u_int32_t timeout_tu)
{
    u_int64_t timeout_us = (u_int64_t)timeout_tu * 1024U;

    if (ic == NULL || timeout_us == 0 || timeout_us >
        (u_int64_t)IEEE80211_ASSOC_COMEBACK_MAX_WAIT_SECONDS * 1000000U)
        return EINVAL;
    clock_interval_to_deadline((u_int32_t)timeout_us, kMicrosecondScale,
        &ic->ic_assoc_comeback_deadline);
    return 0;
}

void
ieee80211_watchdog(struct _ifnet *ifp)
{
    struct ieee80211com *ic = (struct ieee80211com *)ifp;
    
    if (ic->ic_mgt_timer && --ic->ic_mgt_timer == 0) {
        struct ieee80211_sae_driver_hook_snapshot sae_hooks;
        int sae_timeout_owned = 0;

        explicit_bzero(&sae_hooks, sizeof(sae_hooks));

        /* Status 30 is not an association failure.  Retry only the exact
         * pending request after its bounded AP-supplied comeback interval;
         * do not cross the association epoch fence or revoke the SAE PMK.
         * ieee80211_send_mgmt() arms the ordinary response timeout. */
        if (ic->ic_opmode == IEEE80211_M_STA &&
            ic->ic_assoc_comeback_pending && ic->ic_bss != NULL &&
            ic->ic_assoc_comeback_deadline != 0 &&
            ((!ic->ic_assoc_comeback_reassoc &&
              ic->ic_state == IEEE80211_S_ASSOC) ||
             (ic->ic_assoc_comeback_reassoc &&
              ic->ic_state == IEEE80211_S_RUN &&
              ic->ic_wcl_reassoc_owner_active))) {
            u_int64_t now;

            /* The shared watchdog tick can occur immediately after RX.
             * Its integer tick count is not elapsed time since the AP's
             * response. Preserve the original monotonic deadline while
             * waiting; do not renew it on each tick or touch the SAE PMK. */
            clock_get_uptime(&now);
            if (now < ic->ic_assoc_comeback_deadline) {
                ic->ic_mgt_timer = 1;
                goto done;
            }
            int subtype = ic->ic_assoc_comeback_reassoc ?
                IEEE80211_FC0_SUBTYPE_REASSOC_REQ :
                IEEE80211_FC0_SUBTYPE_ASSOC_REQ;
			struct ieee80211_assoc_comeback_retry retry;

			explicit_bzero(&retry, sizeof(retry));
			retry.association_epoch =
			    ieee80211_pae_assoc_epoch_current(ic);
			retry.timeout_tu = ic->ic_assoc_comeback_tu;
			retry.not_before = ic->ic_assoc_comeback_deadline;
			IEEE80211_ADDR_COPY(retry.bssid, ic->ic_bss->ni_bssid);
			retry.subtype = (u_int8_t)subtype;
			retry.retry = ic->ic_assoc_comeback_retries;
			if (ic->ic_assoc_comeback_retry != NULL) {
				int prepare_error =
				    (*ic->ic_assoc_comeback_retry)(ic, &retry);
				explicit_bzero(&retry, sizeof(retry));
				if (prepare_error == 0)
					goto done;
			} else {
				explicit_bzero(&retry, sizeof(retry));
				ic->ic_assoc_comeback_pending = 0;
				ic->ic_assoc_comeback_tu = 0;
				ic->ic_assoc_comeback_deadline = 0;
				ic->ic_assoc_status = 0xffff;
				if (IEEE80211_SEND_MGMT(ic, ic->ic_bss, subtype, 0) == 0)
					goto done;
			}
        }

        /* Capture ownership before the association fence below revokes the
         * driver's exact attempt.  The historical AUTH retry must not turn
         * a timed-out SAE exchange into Open-System authentication. */
        if (ic->ic_opmode == IEEE80211_M_STA &&
            ic->ic_state == IEEE80211_S_AUTH && ic->ic_bss != NULL) {
            ieee80211_sae_driver_hook_snapshot_copyout(ic, &sae_hooks);
            if (sae_hooks.auth_owned != NULL)
                sae_timeout_owned = sae_hooks.auth_owned(ic, ic->ic_bss);
        }
		explicit_bzero(&sae_hooks, sizeof(sae_hooks));
        /* The timeout callback publishes failure before its newstate call. */
        if (ic->ic_opmode == IEEE80211_M_STA &&
            (ic->ic_state == IEEE80211_S_AUTH ||
             ic->ic_state == IEEE80211_S_ASSOC ||
             (ic->ic_wcl_reassoc_owner_active &&
              ieee80211_wcl_reassoc_leaf_is_post_send(
                  ic->ic_wcl_reassoc_owner_last_leaf))))
            (void)ieee80211_pae_assoc_epoch_begin(ic);
        /*
         * Publish the WCL reassociation terminal failure selector
         * for a management-timer expiration that fires while a
         * host-owned reassociation request is in flight, before the
         * standard timeout handling transitions the state machine.
         * The helper's post-send gate filters non-active or pre-send
         * owners, so the call is a no-op when no reassociation is
         * outstanding.
         */
        ieee80211_wcl_reassoc_post_failure(ic, (u_int32_t)ETIMEDOUT);
        if (ic->ic_opmode == IEEE80211_M_STA &&
            (ic->ic_state == IEEE80211_S_AUTH ||
             ic->ic_state == IEEE80211_S_ASSOC)) {
            struct ieee80211_node *ni;
            if (ifp->if_flags & IFF_DEBUG)
                XYLog("%s: %s timed out for %s\n",
                      ifp->if_xname,
                      ic->ic_state == IEEE80211_S_ASSOC ?
                      "association" : "authentication",
                      ether_sprintf(ic->ic_bss->ni_macaddr));
            ni = ieee80211_find_node(ic, ic->ic_bss->ni_macaddr);
            if (ni)
                ni->ni_fails++;
            /* Try more times to join, some drivers will timeout when doing auth/assoc */
            if (ic->ic_state == IEEE80211_S_AUTH && !sae_timeout_owned &&
                ni && ni->ni_fails < 3) {
                ieee80211_node_join_bss(ic, ni);
                goto done;
            }
            if (ISSET(ic->ic_flags, IEEE80211_F_AUTO_JOIN))
                ieee80211_deselect_ess(ic);
        }
        ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
    }
    
done:
    if (ic->ic_mgt_timer != 0)
        ifp->if_timer = 1;
}

static int
ieee80211_assoc_comeback_retry_current(struct ieee80211com *ic,
    const struct ieee80211_assoc_comeback_retry *retry)
{
	int reassoc;

	if (ic == NULL || retry == NULL || ic->ic_opmode != IEEE80211_M_STA ||
	    !ic->ic_assoc_comeback_pending || ic->ic_bss == NULL ||
	    retry->association_epoch == 0 ||
	    ieee80211_pae_assoc_epoch_current(ic) != retry->association_epoch ||
	    retry->not_before == 0 ||
	    retry->not_before != ic->ic_assoc_comeback_deadline ||
	    retry->timeout_tu == 0 ||
	    retry->timeout_tu != ic->ic_assoc_comeback_tu ||
	    retry->retry == 0 || retry->retry != ic->ic_assoc_comeback_retries ||
	    !IEEE80211_ADDR_EQ(retry->bssid, ic->ic_bss->ni_bssid))
		return 0;
	reassoc = retry->subtype == IEEE80211_FC0_SUBTYPE_REASSOC_REQ;
	if ((!reassoc && retry->subtype != IEEE80211_FC0_SUBTYPE_ASSOC_REQ) ||
	    reassoc != (ic->ic_assoc_comeback_reassoc != 0))
		return 0;
	if (!reassoc)
		return ic->ic_state == IEEE80211_S_ASSOC;
	return ic->ic_state == IEEE80211_S_RUN &&
	    ic->ic_wcl_reassoc_owner_active;
}

int
ieee80211_assoc_comeback_retry_ready(struct ieee80211com *ic,
    const struct ieee80211_assoc_comeback_retry *retry)
{
	u_int64_t now;

	if (!ieee80211_assoc_comeback_retry_current(ic, retry))
		return ENOENT;
	clock_get_uptime(&now);
	return now < retry->not_before ? EAGAIN : 0;
}

int
ieee80211_assoc_comeback_retry_abort(struct ieee80211com *ic,
    const struct ieee80211_assoc_comeback_retry *retry, int error)
{
	if (!ieee80211_assoc_comeback_retry_current(ic, retry))
		return 0;

	ic->ic_assoc_comeback_pending = 0;
	ic->ic_assoc_comeback_tu = 0;
	ic->ic_assoc_comeback_deadline = 0;
	ic->ic_assoc_status = 0xffff;
	if (retry->subtype == IEEE80211_FC0_SUBTYPE_REASSOC_REQ)
		ieee80211_wcl_reassoc_post_failure(ic,
		    (u_int32_t)(error != 0 ? error : EIO));
	(void)ieee80211_pae_assoc_epoch_begin(ic);
	ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
	return 1;
}

int
ieee80211_assoc_comeback_retry_complete(struct ieee80211com *ic,
    const struct ieee80211_assoc_comeback_retry *retry)
{
	int error;

	error = ieee80211_assoc_comeback_retry_ready(ic, retry);
	if (error != 0)
		return error;

	/* The lower lease is live now.  Publish the management descriptor only
	 * after the immutable association identity has been revalidated. */
	ic->ic_assoc_comeback_pending = 0;
	ic->ic_assoc_comeback_tu = 0;
	ic->ic_assoc_comeback_deadline = 0;
	ic->ic_assoc_status = 0xffff;
	error = IEEE80211_SEND_MGMT(ic, ic->ic_bss, retry->subtype, 0);
	if (error != 0) {
		/* Restore only the values required by abort()'s exact identity gate. */
		ic->ic_assoc_comeback_pending = 1;
		ic->ic_assoc_comeback_tu = retry->timeout_tu;
		ic->ic_assoc_comeback_deadline = retry->not_before;
		(void)ieee80211_assoc_comeback_retry_abort(ic, retry, error);
	}
	return error;
}

const struct ieee80211_rateset ieee80211_std_rateset_11a =
{ 8, { 12, 18, 24, 36, 48, 72, 96, 108 } };

const struct ieee80211_rateset ieee80211_std_rateset_11b =
{ 4, { 2, 4, 11, 22 } };

const struct ieee80211_rateset ieee80211_std_rateset_11g =
{ 12, { 2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108 } };

const struct ieee80211_ht_rateset ieee80211_std_ratesets_11n[] = {
    /* MCS 0-7, 20MHz channel, no SGI */
    { 8, { 13, 26, 39, 52, 78, 104, 117, 130 }, 0x000000ff, 0, 7, 0},
    
    /* MCS 0-7, 20MHz channel, SGI */
    { 8, { 14, 29, 43, 58, 87, 116, 130, 144 }, 0x000000ff, 0, 7, 1 },
    
    /* MCS 8-15, 20MHz channel, no SGI */
    { 8, { 26, 52, 78, 104, 156, 208, 234, 260 }, 0x0000ff00, 8, 15, 0 },
    
    /* MCS 8-15, 20MHz channel, SGI */
    { 8, { 29, 58, 87, 116, 173, 231, 261, 289 }, 0x0000ff00, 8, 15, 1 },
    
    /* MCS 16-23, 20MHz channel, no SGI */
    { 8, { 39, 78, 117, 156, 234, 312, 351, 390 }, 0x00ff0000, 16, 23, 0 },
    
    /* MCS 16-23, 20MHz channel, SGI */
    { 8, { 43, 87, 130, 173, 260, 347, 390, 433 }, 0x00ff0000, 16, 23, 1 },
    
    /* MCS 24-31, 20MHz channel, no SGI */
    { 8, { 52, 104, 156, 208, 312, 416, 468, 520 }, 0xff000000, 24, 31, 0 },
    
    /* MCS 24-31, 20MHz channel, SGI */
    { 8, { 58, 116, 173, 231, 347, 462, 520, 578 }, 0xff000000, 24, 31, 1 },
    
    /* MCS 0-7, 40MHz channel, no SGI */
    { 8, { 27, 54, 81, 108, 162, 216, 243, 270 }, 0x000000ff, 0, 7, 0},
    
    /* MCS 0-7, 40MHz channel, SGI */
    { 8, { 30, 60, 90, 120, 180, 240, 270, 300 }, 0x000000ff, 0, 7, 1 },
    
    /* MCS 8-15, 40MHz channel, no SGI */
    { 8, { 54, 108, 162, 216, 324, 432, 486, 540 }, 0x0000ff00, 8, 15, 0 },
    
    /* MCS 8-15, 40MHz channel, SGI */
    { 8, { 60, 120, 180, 240, 360, 480, 540, 600 }, 0x0000ff00, 8, 15, 1 },
    
    /* MCS 16-23, 40MHz channel, no SGI */
    { 8, { 81, 162, 243, 324, 486, 648, 729, 810 }, 0x00ff0000, 16, 23, 0 },
    
    /* MCS 16-23, 40MHz channel, SGI */
    { 8, { 90, 180, 270, 360, 540, 720, 810, 900 }, 0x00ff0000, 16, 23, 1 },
    
    /* MCS 24-31, 40MHz channel, no SGI */
    { 8, { 108, 216, 324, 432, 648, 864, 972, 1080 }, 0xff000000, 24, 31, 0 },
    
    /* MCS 24-31, 40MHz channel, SGI */
    { 8, { 120, 240, 360, 480, 720, 960, 1080, 1200 }, 0xff000000, 24, 31, 1 },
};

const struct ieee80211_vht_rateset ieee80211_std_ratesets_11ac[] = {
    /* MCS 0-8 (MCS 9 N/A), 1 SS, 20MHz channel, no SGI */
    { 9, { 13, 26, 39, 52, 78, 104, 117, 130, 156 }, 1, 0 },
    
    /* MCS 0-8 (MCS 9 N/A), 1 SS, 20MHz channel, SGI */
    { 9, { 14, 29, 43, 58, 87, 116, 130, 144, 174 }, 1, 1 },
    
    /* MCS 0-8 (MCS 9 N/A), 2 SS, 20MHz channel, no SGI */
    { 9, { 26, 52, 78, 104, 156, 208, 234, 260, 312 }, 2, 0 },
    
    /* MCS 0-8 (MCS 9 N/A), 2 SS, 20MHz channel, SGI */
    { 9, { 29, 58, 87, 116, 173, 231, 261, 289, 347 }, 2, 1 },
    
    /* MCS 0-9, 1 SS, 40MHz channel, no SGI */
    { 10, { 27, 54, 81, 108, 162, 216, 243, 270, 324, 360 }, 1, 0 },
    
    /* MCS 0-9, 1 SS, 40MHz channel, SGI */
    { 10, { 30, 60, 90, 120, 180, 240, 270, 300, 360, 400 }, 1, 1 },
    
    /* MCS 0-9, 2 SS, 40MHz channel, no SGI */
    { 10, { 54, 108, 162, 216, 324, 432, 486, 540, 648, 720 }, 2, 0 },
    
    /* MCS 0-9, 2 SS, 40MHz channel, SGI */
    { 10, { 60, 120, 180, 240, 360, 480, 540, 600, 720, 800 }, 2, 1 },
    
    /* MCS 0-9, 1 SS, 80MHz channel, no SGI */
    { 10, { 59, 117, 176, 234, 351, 468, 527, 585, 702, 780 }, 1, 0 },
    
    /* MCS 0-9, 1 SS, 80MHz channel, SGI */
    { 10, { 65, 130, 195, 260, 390, 520, 585, 650, 780, 867 }, 1, 1 },
    
    /* MCS 0-9, 2 SS, 80MHz channel, no SGI */
    { 10, { 117, 234, 351, 468, 702, 936, 1053, 1404, 1560 }, 2, 0 },
    
    /* MCS 0-9, 2 SS, 80MHz channel, SGI */
    { 10, { 130, 260, 390, 520, 780, 1040, 1170, 1300, 1560, 1734 }, 2, 1 },
    
    /* MCS 0-9, 1 SS, 160MHz channel, no SGI */
    { 10, { 117, 234, 351, 468, 702, 936, 1053, 1170, 1404, 1560 }, 1, 0 },
    
    /* MCS 0-9, 1 SS, 160MHz channel, SGI */
    { 10, { 130, 260, 390, 520, 780, 1040, 1170, 1300, 1560, 1734 }, 1, 1 },
    
    /* MCS 0-9, 2 SS, 160MHz channel, no SGI */
    { 10, { 234, 468, 702, 936, 1404, 1872, 2106, 2340, 2808 }, 2, 0 },
    
    /* MCS 0-9, 2 SS, 160MHz channel, SGI */
    { 10, { 260, 520, 780, 1040, 1560, 2080, 2340, 2600, 3120, 3464 }, 2, 1 },
};

const struct ieee80211_he_rateset ieee80211_std_ratesets_11ax[] = {
    /* MCS 0-11 1 SS, 20MHz channel */
    { 12, { 17, 34, 52, 69, 103, 138, 155, 172, 206, 230, 258, 287 }, 1 },
    
    /* MCS 0-11 2 SS, 20MHz channel */
    { 12, { 34, 69, 103, 138, 206, 275, 310, 344, 413, 459, 516, 574 }, 2 },
    
    /* MCS 0-11 1 SS, 40MHz channel */
    { 12, { 34, 69, 103, 138, 206, 275, 310, 344, 413, 459, 516, 574 }, 1 },
    
    /* MCS 0-11 2 SS, 40MHz channel */
    { 12, { 69, 138, 206, 275, 413, 551, 619, 688, 826, 918, 1032, 1147 }, 2 },
    
    /* MCS 0-11 1 SS, 80MHz channel */
    { 12, { 72, 144, 216, 288, 432, 577, 649, 721, 865, 961, 1081, 1201 }, 1 },
    
    /* MCS 0-11 2 SS, 80MHz channel */
    { 12, { 288, 577, 865, 1153, 1729, 2306, 2594, 2882, 3459, 3843, 4324, 4804 }, 2},
    
    /* MCS 0-11 1 SS, 160MHz channel */
    { 12, { 144, 288, 432, 564, 865, 1152, 1298, 1442, 1730, 1922, 2162, 2402 }, 1 },
    
    /* MCS 0-11 2 SS, 160MHz channel */
    { 12, { 576, 1154, 1730, 2306, 3458, 4612, 5188, 5764, 6918, 7686, 8648, 9608 }, 2 },
};

/*
 * Mark the basic rates for the 11g rate table based on the
 * operating mode.  For real 11g we mark all the 11b rates
 * and 6, 12, and 24 OFDM.  For 11b compatibility we mark only
 * 11b rates.  There's also a pseudo 11a-mode used to mark only
 * the basic OFDM rates.
 */
void
ieee80211_setbasicrates(struct ieee80211com *ic)
{
    static const struct ieee80211_rateset basic[] = {
        { 0 },                /* IEEE80211_MODE_AUTO */
        { 3, { 12, 24, 48 } },        /* IEEE80211_MODE_11A */
        { 2, { 2, 4 } },            /* IEEE80211_MODE_11B */
        { 4, { 2, 4, 11, 22 } },        /* IEEE80211_MODE_11G */
        { 0 },                /* IEEE80211_MODE_11N    */
        { 0 },                /* IEEE80211_MODE_11AC    */
        { 0 },                /* IEEE80211_MODE_11AX    */
    };
    int mode;
    struct ieee80211_rateset *rs;
    int i, j;
    
    for (mode = 0; mode < IEEE80211_MODE_MAX; mode++) {
        rs = &ic->ic_sup_rates[mode];
        for (i = 0; i < rs->rs_nrates; i++) {
            rs->rs_rates[i] &= IEEE80211_RATE_VAL;
            for (j = 0; j < basic[mode].rs_nrates; j++) {
                if (basic[mode].rs_rates[j] ==
                    rs->rs_rates[i]) {
                    rs->rs_rates[i] |=
                    IEEE80211_RATE_BASIC;
                    break;
                }
            }
        }
    }
}

int
ieee80211_min_basic_rate(struct ieee80211com *ic)
{
    struct ieee80211_rateset *rs = &ic->ic_bss->ni_rates;
    int i, min, rval;
    
    min = -1;
    
    for (i = 0; i < rs->rs_nrates; i++) {
        if ((rs->rs_rates[i] & IEEE80211_RATE_BASIC) == 0)
            continue;
        rval = (rs->rs_rates[i] & IEEE80211_RATE_VAL);
        if (min == -1)
            min = rval;
        else if (rval < min)
            min = rval;
    }
    
    /* Default to 1 Mbit/s on 2GHz and 6 Mbit/s on 5GHz. */
    if (min == -1)
        min = IEEE80211_IS_CHAN_2GHZ(ic->ic_bss->ni_chan) ? 2 : 12;
    
    return min;
}

int
ieee80211_max_basic_rate(struct ieee80211com *ic)
{
    struct ieee80211_rateset *rs = &ic->ic_bss->ni_rates;
    int i, max, rval;
    
    /* Default to 1 Mbit/s on 2GHz and 6 Mbit/s on 5GHz. */
    max = IEEE80211_IS_CHAN_2GHZ(ic->ic_bss->ni_chan) ? 2 : 12;
    
    for (i = 0; i < rs->rs_nrates; i++) {
        if ((rs->rs_rates[i] & IEEE80211_RATE_BASIC) == 0)
            continue;
        rval = (rs->rs_rates[i] & IEEE80211_RATE_VAL);
        if (rval > max)
            max = rval;
    }
    
    return max;
}

/*
 * Set the current phy mode and recalculate the active channel
 * set based on the available channels for this mode.  Also
 * select a new default/current channel if the current one is
 * inappropriate for this mode.
 */
int
ieee80211_setmode(struct ieee80211com *ic, enum ieee80211_phymode mode)
{
    struct _ifnet *ifp = &ic->ic_if;
    static const u_int chanflags[] = {
        0,            /* IEEE80211_MODE_AUTO */
        IEEE80211_CHAN_A,    /* IEEE80211_MODE_11A */
        IEEE80211_CHAN_B,    /* IEEE80211_MODE_11B */
        IEEE80211_CHAN_PUREG,    /* IEEE80211_MODE_11G */
        IEEE80211_CHAN_HT,    /* IEEE80211_MODE_11N */
        IEEE80211_CHAN_VHT | IEEE80211_CHAN_HT,    /* IEEE80211_MODE_11AC */
        IEEE80211_CHAN_VHT | IEEE80211_CHAN_HT,    /* IEEE80211_MODE_11AX */
    };
    const struct ieee80211_channel *c;
    u_int modeflags;
    int i;
    
    /* validate new mode */
    if ((ic->ic_modecaps & (1<<mode)) == 0) {
        DPRINTF(("mode %u not supported (caps 0x%x)\n",
                 mode, ic->ic_modecaps));
        return EINVAL;
    }
    
    /*
     * Verify at least one channel is present in the available
     * channel list before committing to the new mode.
     */
    if (mode >= nitems(chanflags))
        panic("%s: unexpected mode %u", __func__, mode);
    modeflags = chanflags[mode];
    for (i = 0; i <= IEEE80211_CHAN_MAX; i++) {
        c = &ic->ic_channels[i];
        if (mode == IEEE80211_MODE_AUTO) {
            if (c->ic_flags != 0)
                break;
        } else if ((c->ic_flags & modeflags) != 0)
            break;
    }
    if (i > IEEE80211_CHAN_MAX) {
        DPRINTF(("no channels found for mode %u\n", mode));
        return EINVAL;
    }
    
    /*
     * Calculate the active channel set.
     */
    memset(ic->ic_chan_active, 0, sizeof(ic->ic_chan_active));
    for (i = 0; i <= IEEE80211_CHAN_MAX; i++) {
        c = &ic->ic_channels[i];
        if (mode == IEEE80211_MODE_AUTO) {
            if (c->ic_flags != 0)
                setbit(ic->ic_chan_active, i);
        } else if ((c->ic_flags & modeflags) != 0)
            setbit(ic->ic_chan_active, i);
    }
    /*
     * If no current/default channel is setup or the current
     * channel is wrong for the mode then pick the first
     * available channel from the active list.  This is likely
     * not the right one.
     */
    if (ic->ic_ibss_chan == NULL || isclr(ic->ic_chan_active,
                                          ieee80211_chan2ieee(ic, ic->ic_ibss_chan))) {
        for (i = 0; i <= IEEE80211_CHAN_MAX; i++)
            if (isset(ic->ic_chan_active, i)) {
                ic->ic_ibss_chan = &ic->ic_channels[i];
                break;
            }
        if ((ic->ic_ibss_chan == NULL) || isclr(ic->ic_chan_active,
                                                ieee80211_chan2ieee(ic, ic->ic_ibss_chan)))
            panic("Bad IBSS channel %u",
                  ieee80211_chan2ieee(ic, ic->ic_ibss_chan));
    }
    
    /*
     * Reset the scan state for the new mode. This avoids scanning
     * of invalid channels, ie. 5GHz channels in 11b mode.
     */
    ieee80211_reset_scan(ifp);
    
    ic->ic_curmode = mode;
    ieee80211_reset_erp(ic);    /* reset ERP state */
    
    return 0;
}

enum ieee80211_phymode
ieee80211_next_mode(struct _ifnet *ifp)
{
    struct ieee80211com *ic = (struct ieee80211com *)ifp;
    uint16_t mode;
    
    /*
     * Indicate a wrap-around if we're running in a fixed, user-specified
     * phy mode.
     */
    if (IFM_MODE(ic->ic_media.ifm_cur->ifm_media) != IFM_AUTO)
        return (IEEE80211_MODE_AUTO);
    
    /*
     * Always scan in AUTO mode if the driver scans all bands.
     * The current mode might have changed during association
     * so we must reset it here.
     */
    if (ic->ic_caps & IEEE80211_C_SCANALLBAND) {
        ieee80211_setmode(ic, IEEE80211_MODE_AUTO);
        return (enum ieee80211_phymode)(ic->ic_curmode);
    }
    
    /*
     * Get the next supported mode; effectively, this alternates between
     * the 11a (5GHz) and 11b/g (2GHz) modes. What matters is that each
     * supported channel gets scanned.
     */
    for (mode = ic->ic_curmode + 1; mode <= IEEE80211_MODE_MAX; mode++) {
        /*
         * Skip over 11n mode. Its set of channels is the superset
         * of all channels supported by the other modes.
         */
        if (mode == IEEE80211_MODE_11N)
            continue;
        /*
         * Skip over 11ac mode. Its set of channels is the set
         * of all channels supported by 11a.
         */
        if (mode == IEEE80211_MODE_11AC)
            continue;
        
        /*
         * Skip over 11ax mode. Its set of channels is the set
         * of all channels supported by 11a.
         */
        if (mode == IEEE80211_MODE_11AX)
            continue;
        
        /* Start over if we have already tried all modes. */
        if (mode == IEEE80211_MODE_MAX) {
            mode = IEEE80211_MODE_AUTO;
            break;
        }
        
        if (ic->ic_modecaps & (1 << mode))
            break;
    }
    
    if (mode != ic->ic_curmode)
        ieee80211_setmode(ic, (enum ieee80211_phymode)mode);
    
    return (enum ieee80211_phymode)(ic->ic_curmode);
}

/*
 * Return the phy mode for with the specified channel so the
 * caller can select a rate set.  This is problematic and the
 * work here assumes how things work elsewhere in this code.
 *
 * Because the result of this function is ultimately used to select a
 * rate from the rate set of the returned mode, it must return one of the
 * legacy 11a/b/g modes; 11n and 11ac modes use MCS instead of rate sets.
 */
enum ieee80211_phymode
ieee80211_chan2mode(struct ieee80211com *ic,
                    const struct ieee80211_channel *chan)
{
    /*
     * Are we fixed in 11a/b/g mode?
     * NB: this assumes the channel would not be supplied to us
     *     unless it was already compatible with the current mode.
     */
    if (ic->ic_curmode == IEEE80211_MODE_11A ||
        ic->ic_curmode == IEEE80211_MODE_11B ||
        ic->ic_curmode == IEEE80211_MODE_11G)
        return (enum ieee80211_phymode)ic->ic_curmode;
    
    /* If no channel was provided, return the most suitable legacy mode. */
    if (chan == IEEE80211_CHAN_ANYC) {
        switch (ic->ic_curmode) {
            case IEEE80211_MODE_AUTO:
            case IEEE80211_MODE_11N:
                if (ic->ic_modecaps & (1 << IEEE80211_MODE_11A))
                    return IEEE80211_MODE_11A;
                if (ic->ic_modecaps & (1 << IEEE80211_MODE_11G))
                    return IEEE80211_MODE_11G;
                return IEEE80211_MODE_11B;
            case IEEE80211_MODE_11AC:
            case IEEE80211_MODE_11AX:
                return IEEE80211_MODE_11A;
            default:
                return (enum ieee80211_phymode)ic->ic_curmode;
        }
    }
    
    /* Deduce a legacy mode based on the channel characteristics. */
    if (IEEE80211_IS_CHAN_5GHZ(chan))
        return IEEE80211_MODE_11A;
    else if (chan->ic_flags & (IEEE80211_CHAN_OFDM|IEEE80211_CHAN_DYN))
        return IEEE80211_MODE_11G;
    else
        return IEEE80211_MODE_11B;
}

/*
 * Convert IEEE80211 MCS index to ifmedia subtype.
 */
uint64_t
ieee80211_mcs2media(struct ieee80211com *ic, int mcs,
                    enum ieee80211_phymode mode)
{
    switch (mode) {
        case IEEE80211_MODE_11A:
        case IEEE80211_MODE_11B:
        case IEEE80211_MODE_11G:
            /* these modes use rates, not MCS */
            panic("%s: unexpected mode %d", __func__, mode);
            break;
        case IEEE80211_MODE_11N:
            if (mcs >= 0 && mcs < IEEE80211_HT_NUM_MCS)
                return (IFM_IEEE80211_11N |
                        (IFM_IEEE80211_HT_MCS0 + mcs));
            break;
        case IEEE80211_MODE_11AC:
            if (mcs >= 0 && mcs < IEEE80211_VHT_NUM_MCS)
                return (IFM_IEEE80211_11AC |
                        (IFM_IEEE80211_VHT_MCS0 + mcs));
            break;
        case IEEE80211_MODE_11AX:
            if (mcs >= 0 && mcs < IEEE80211_VHT_NUM_MCS)
                return (IFM_IEEE80211_11AX |
                        (IFM_IEEE80211_VHT_MCS0 + mcs));
            break;
        case IEEE80211_MODE_AUTO:
            break;
    }
    
    return IFM_AUTO;
}

/*
 * Convert ifmedia subtype to IEEE80211 MCS index.
 */
int
ieee80211_media2mcs(uint64_t mword)
{
    uint64_t subtype;
    
    subtype = IFM_SUBTYPE(mword);
    
    if (subtype == IFM_AUTO)
        return -1;
    else if (subtype == IFM_MANUAL || subtype == IFM_NONE)
        return 0;
    
    if (subtype >= IFM_IEEE80211_HT_MCS0 &&
        subtype <= IFM_IEEE80211_HT_MCS76)
        return (int)(subtype - IFM_IEEE80211_HT_MCS0);
    
    if (subtype >= IFM_IEEE80211_VHT_MCS0 &&
        subtype <= IFM_IEEE80211_VHT_MCS9)
        return (int)(subtype - IFM_IEEE80211_VHT_MCS0);
    
    return -1;
}

/*
 * convert IEEE80211 rate value to ifmedia subtype.
 * ieee80211 rate is in unit of 0.5Mbps.
 */
uint64_t
ieee80211_rate2media(struct ieee80211com *ic, int rate,
                     enum ieee80211_phymode mode)
{
    static const struct {
        uint64_t    m;    /* rate + mode */
        uint64_t    r;    /* if_media rate */
    } rates[] = {
        {   2 | IFM_IEEE80211_11B, IFM_IEEE80211_DS1 },
        {   4 | IFM_IEEE80211_11B, IFM_IEEE80211_DS2 },
        {  11 | IFM_IEEE80211_11B, IFM_IEEE80211_DS5 },
        {  22 | IFM_IEEE80211_11B, IFM_IEEE80211_DS11 },
        {  44 | IFM_IEEE80211_11B, IFM_IEEE80211_DS22 },
        {  12 | IFM_IEEE80211_11A, IFM_IEEE80211_OFDM6 },
        {  18 | IFM_IEEE80211_11A, IFM_IEEE80211_OFDM9 },
        {  24 | IFM_IEEE80211_11A, IFM_IEEE80211_OFDM12 },
        {  36 | IFM_IEEE80211_11A, IFM_IEEE80211_OFDM18 },
        {  48 | IFM_IEEE80211_11A, IFM_IEEE80211_OFDM24 },
        {  72 | IFM_IEEE80211_11A, IFM_IEEE80211_OFDM36 },
        {  96 | IFM_IEEE80211_11A, IFM_IEEE80211_OFDM48 },
        { 108 | IFM_IEEE80211_11A, IFM_IEEE80211_OFDM54 },
        {   2 | IFM_IEEE80211_11G, IFM_IEEE80211_DS1 },
        {   4 | IFM_IEEE80211_11G, IFM_IEEE80211_DS2 },
        {  11 | IFM_IEEE80211_11G, IFM_IEEE80211_DS5 },
        {  22 | IFM_IEEE80211_11G, IFM_IEEE80211_DS11 },
        {  12 | IFM_IEEE80211_11G, IFM_IEEE80211_OFDM6 },
        {  18 | IFM_IEEE80211_11G, IFM_IEEE80211_OFDM9 },
        {  24 | IFM_IEEE80211_11G, IFM_IEEE80211_OFDM12 },
        {  36 | IFM_IEEE80211_11G, IFM_IEEE80211_OFDM18 },
        {  48 | IFM_IEEE80211_11G, IFM_IEEE80211_OFDM24 },
        {  72 | IFM_IEEE80211_11G, IFM_IEEE80211_OFDM36 },
        {  96 | IFM_IEEE80211_11G, IFM_IEEE80211_OFDM48 },
        { 108 | IFM_IEEE80211_11G, IFM_IEEE80211_OFDM54 },
        /* NB: OFDM72 doesn't really exist so we don't handle it */
    };
    uint64_t mask;
    int i;
    
    mask = rate & IEEE80211_RATE_VAL;
    switch (mode) {
        case IEEE80211_MODE_11A:
            mask |= IFM_IEEE80211_11A;
            break;
        case IEEE80211_MODE_11B:
            mask |= IFM_IEEE80211_11B;
            break;
        case IEEE80211_MODE_AUTO:
            /* NB: hack, 11g matches both 11b+11a rates */
            /* FALLTHROUGH */
        case IEEE80211_MODE_11G:
            mask |= IFM_IEEE80211_11G;
            break;
        case IEEE80211_MODE_11N:
        case IEEE80211_MODE_11AC:
        case IEEE80211_MODE_11AX:
            /* 11n/11ac/11ax uses MCS, not rates. */
            panic("%s: unexpected mode %d", __func__, mode);
            break;
    }
    for (i = 0; i < nitems(rates); i++)
        if (rates[i].m == mask)
            return rates[i].r;
    return IFM_AUTO;
}

int
ieee80211_media2rate(uint64_t mword)
{
    int i;
    static const struct {
        uint64_t subtype;
        int rate;
    } ieeerates[] = {
        { IFM_AUTO,        -1    },
        { IFM_MANUAL,        0    },
        { IFM_NONE,        0    },
        { IFM_IEEE80211_DS1,    2    },
        { IFM_IEEE80211_DS2,    4    },
        { IFM_IEEE80211_DS5,    11    },
        { IFM_IEEE80211_DS11,    22    },
        { IFM_IEEE80211_DS22,    44    },
        { IFM_IEEE80211_OFDM6,    12    },
        { IFM_IEEE80211_OFDM9,    18    },
        { IFM_IEEE80211_OFDM12,    24    },
        { IFM_IEEE80211_OFDM18,    36    },
        { IFM_IEEE80211_OFDM24,    48    },
        { IFM_IEEE80211_OFDM36,    72    },
        { IFM_IEEE80211_OFDM48,    96    },
        { IFM_IEEE80211_OFDM54,    108    },
        { IFM_IEEE80211_OFDM72,    144    },
    };
    for (i = 0; i < nitems(ieeerates); i++) {
        if (ieeerates[i].subtype == IFM_SUBTYPE(mword))
            return ieeerates[i].rate;
    }
    return 0;
}

/*
 * Convert bit rate (in 0.5Mbps units) to PLCP signal (R4-R1) and vice versa.
 */
u_int8_t
ieee80211_rate2plcp(u_int8_t rate, enum ieee80211_phymode mode)
{
    rate &= IEEE80211_RATE_VAL;
    
    if (mode == IEEE80211_MODE_11B) {
        /* IEEE Std 802.11b-1999 page 15, subclause 18.2.3.3 */
        switch (rate) {
            case 2:        return 10;
            case 4:        return 20;
            case 11:    return 55;
            case 22:    return 110;
                /* IEEE Std 802.11g-2003 page 19, subclause 19.3.2.1 */
            case 44:    return 220;
        }
    } else if (mode == IEEE80211_MODE_11G || mode == IEEE80211_MODE_11A) {
        /* IEEE Std 802.11a-1999 page 14, subclause 17.3.4.1 */
        switch (rate) {
            case 12:    return 0x0b;
            case 18:    return 0x0f;
            case 24:    return 0x0a;
            case 36:    return 0x0e;
            case 48:    return 0x09;
            case 72:    return 0x0d;
            case 96:    return 0x08;
            case 108:    return 0x0c;
        }
    } else
        panic("%s: unexpected mode %u", __func__, mode);
    
    DPRINTF(("unsupported rate %u\n", rate));
    
    return 0;
}

u_int8_t
ieee80211_plcp2rate(u_int8_t plcp, enum ieee80211_phymode mode)
{
    if (mode == IEEE80211_MODE_11B) {
        /* IEEE Std 802.11g-2003 page 19, subclause 19.3.2.1 */
        switch (plcp) {
            case 10:    return 2;
            case 20:    return 4;
            case 55:    return 11;
            case 110:    return 22;
                /* IEEE Std 802.11g-2003 page 19, subclause 19.3.2.1 */
            case 220:    return 44;
        }
    } else if (mode == IEEE80211_MODE_11G || mode == IEEE80211_MODE_11A) {
        /* IEEE Std 802.11a-1999 page 14, subclause 17.3.4.1 */
        switch (plcp) {
            case 0x0b:    return 12;
            case 0x0f:    return 18;
            case 0x0a:    return 24;
            case 0x0e:    return 36;
            case 0x09:    return 48;
            case 0x0d:    return 72;
            case 0x08:    return 96;
            case 0x0c:    return 108;
        }
    } else
        panic("%s: unexpected mode %u", __func__, mode);
    
    DPRINTF(("unsupported plcp %u\n", plcp));

    return 0;
}

/* Host-only accepted-roam ownership. These helpers retain the existing wire
 * event mapping while protecting admission/retirement/publication identity.
 * The distinct reference progress, AUTH and overall-completion carriers are
 * still required; 0xcf is NOT the reference's general roam-failure event. */
u_int64_t
ieee80211_wcl_reassoc_serial(struct ieee80211com *ic)
{
    if (ic == NULL || ic->ic_pae_selected_bss_lock == NULL)
        return 0;
    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(ic->ic_pae_selected_bss_lock);
    const u_int64_t serial = ic->ic_wcl_reassoc_owner_active ?
        ic->ic_wcl_reassoc_owner_serial : 0;
    IOSimpleLockUnlockEnableInterrupt(ic->ic_pae_selected_bss_lock, irq);
    return serial;
}

int
ieee80211_wcl_reassoc_current(struct ieee80211com *ic, u_int64_t serial)
{
    return serial != 0 && ieee80211_wcl_reassoc_serial(ic) == serial;
}

int
ieee80211_wcl_reassoc_scan_completion_begin(struct ieee80211com *ic,
    u_int64_t serial)
{
    if (ic == NULL || ic->ic_pae_selected_bss_lock == NULL)
        return 0;
    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(ic->ic_pae_selected_bss_lock);
    int current = serial == 0 ? !ic->ic_wcl_reassoc_owner_active :
        (ic->ic_wcl_reassoc_owner_active && ic->ic_wcl_reassoc_owner_serial == serial &&
         ic->ic_pae_assoc_epoch == ic->ic_wcl_reassoc_source_epoch &&
         (ic->ic_wcl_reassoc_owner_last_leaf == IEEE80211_WCL_REASSOC_OWNER_LEAF_SETUP ||
          ic->ic_wcl_reassoc_owner_last_leaf == IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_STARTED));
    if (current && serial != 0) {
        /* Only an actual physical terminal calls this entry. It proves the
         * lower scan was accepted even if its sender has not returned yet. */
        ic->ic_wcl_reassoc_scan_accepted_serial = serial;
        ic->ic_wcl_reassoc_owner_last_leaf = IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_STARTED;
        ic->ic_flags |= IEEE80211_F_BGSCAN;
        ic->ic_flags &= ~IEEE80211_F_DISABLE_BG_AUTO_CONNECT;
    }
    IOSimpleLockUnlockEnableInterrupt(ic->ic_pae_selected_bss_lock, irq);
    return current;
}

/* The caller holds the selected-BSS leaf; no lower or upper callback here. */
static void
ieee80211_wcl_reassoc_clear_locked(struct ieee80211com *ic)
{
    ic->ic_wcl_reassoc_owner_active = 0;
    ic->ic_wcl_reassoc_owner_serial = 0;
    ic->ic_wcl_reassoc_source_epoch = 0;
    ic->ic_wcl_reassoc_owner_last_leaf =
        IEEE80211_WCL_REASSOC_OWNER_LEAF_IDLE;
    explicit_bzero(&ic->ic_wcl_reassoc_request,
                   sizeof(ic->ic_wcl_reassoc_request));
    explicit_bzero(ic->ic_wcl_reassoc_source_bssid,
                   sizeof(ic->ic_wcl_reassoc_source_bssid));
    explicit_bzero(ic->ic_wcl_reassoc_target_bssid,
                   sizeof(ic->ic_wcl_reassoc_target_bssid));
}

/* Logical source cancellation, not a firmware scan terminal. The caller
 * holds the selected-BSS leaf while advancing this exact source epoch.
 * A real link-down/leave owns its existing WCL notification; do not invent
 * an additional 0xcf command-start failure or an on-air reassociation result.
 * The lower tagged scan lease remains alive until its real abort/terminal.
 */
void
ieee80211_wcl_reassoc_cancel_scan_epoch_locked(struct ieee80211com *ic,
    u_int64_t source_epoch)
{
    if (ic == NULL || ic->ic_pae_selected_bss_lock == NULL ||
        source_epoch == 0 || !ic->ic_wcl_reassoc_owner_active ||
        ic->ic_wcl_reassoc_owner_serial == 0 ||
        ic->ic_wcl_reassoc_owner_serial != ic->ic_wcl_reassoc_next_serial ||
        ic->ic_wcl_reassoc_source_epoch != source_epoch ||
        (ic->ic_wcl_reassoc_owner_last_leaf != IEEE80211_WCL_REASSOC_OWNER_LEAF_SETUP &&
         ic->ic_wcl_reassoc_owner_last_leaf != IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_STARTED &&
         ic->ic_wcl_reassoc_owner_last_leaf != IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_FAILED))
        return;
    ieee80211_wcl_reassoc_clear_locked(ic);
    ic->ic_wcl_reassoc_terminal_serial = 0;
    ic->ic_flags &= ~(IEEE80211_F_BGSCAN | IEEE80211_F_DISABLE_BG_AUTO_CONNECT);
    /* Keep the monotonic sequence and the historical accepted-scan receipt.
     * Neither grants a late physical callback ownership of a successor. */
}

static int
ieee80211_wcl_reassoc_take_completion(struct ieee80211com *ic,
    u_int64_t serial, int success,
    struct ieee80211_wcl_reassoc_completion *completion, u_int32_t *leaf)
{
    if (ic == NULL || serial == 0 || completion == NULL || leaf == NULL ||
        ic->ic_pae_selected_bss_lock == NULL)
        return 0;
    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(ic->ic_pae_selected_bss_lock);
    *leaf = ic->ic_wcl_reassoc_owner_last_leaf;
    const int scan = *leaf == IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_STARTED ||
        *leaf == IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_FAILED;
    if (!ic->ic_wcl_reassoc_owner_active ||
        ic->ic_wcl_reassoc_owner_serial != serial ||
        !ieee80211_wcl_reassoc_leaf_is_post_send(*leaf) || (success && scan)) {
        IOSimpleLockUnlockEnableInterrupt(ic->ic_pae_selected_bss_lock, irq);
        return 0;
    }
    completion->serial = serial;
    completion->association_epoch = ic->ic_pae_assoc_epoch;
    IEEE80211_ADDR_COPY(completion->source_bssid, ic->ic_wcl_reassoc_source_bssid);
    IEEE80211_ADDR_COPY(completion->target_bssid, ic->ic_wcl_reassoc_target_bssid);
    /* Retire before epoch cancellation releases its leaf and invokes SAE,
     * credential, MFP and WCL callbacks. A nested retirement then has no owner. */
    ieee80211_wcl_reassoc_clear_locked(ic);
    ic->ic_wcl_reassoc_terminal_serial = serial;
    IOSimpleLockUnlockEnableInterrupt(ic->ic_pae_selected_bss_lock, irq);
    return 1;
}

int
ieee80211_wcl_reassoc_claim_completion(struct ieee80211com *ic,
    const struct ieee80211_wcl_reassoc_completion *completion)
{
    if (ic == NULL || completion == NULL || completion->serial == 0 ||
        ic->ic_pae_selected_bss_lock == NULL)
        return 0;
    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(ic->ic_pae_selected_bss_lock);
    const int current =
        ic->ic_wcl_reassoc_next_serial == completion->serial &&
        ic->ic_wcl_reassoc_terminal_serial == completion->serial &&
        ic->ic_pae_assoc_epoch == completion->association_epoch;
    if (current)
        ic->ic_wcl_reassoc_terminal_serial = 0;
    IOSimpleLockUnlockEnableInterrupt(ic->ic_pae_selected_bss_lock, irq);
    return current;
}

void
ieee80211_wcl_reassoc_post_success(struct ieee80211com *ic)
{
    struct ieee80211_wcl_reassoc_completion completion = {};
    u_int32_t leaf;
    const u_int64_t serial = ieee80211_wcl_reassoc_serial(ic);
    if (!ieee80211_wcl_reassoc_take_completion(ic, serial, 1, &completion, &leaf))
        return;
    if (ic->ic_event_handler)
        (*ic->ic_event_handler)(ic, IEEE80211_EVT_WCL_REASSOC_DONE, &completion);
}

u_int64_t
ieee80211_wcl_reassoc_post_failure_owned(struct ieee80211com *ic,
    u_int64_t serial, u_int32_t result)
{
    struct ieee80211_wcl_reassoc_completion completion = {};
    u_int32_t leaf;
    if (!ieee80211_wcl_reassoc_take_completion(ic, serial, 0, &completion, &leaf))
        return 0;
    completion.result = result != 0 ? result : (u_int32_t)EIO;
    /* A failed census leaves its source association alive. Once switching
     * started, fence only the retired serial and its captured epoch. */
    if (leaf != IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_STARTED &&
        leaf != IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_FAILED) {
        completion.association_epoch = ieee80211_pae_assoc_epoch_begin_reassoc(
            ic, serial, completion.association_epoch);
        if (completion.association_epoch == 0)
            return 0;
    }
    if (ic->ic_event_handler)
        (*ic->ic_event_handler)(ic, IEEE80211_EVT_WCL_REASSOC_FAIL, &completion);
    /* This is the retired owner's epoch, not a read of potentially replaced
     * current state after the public callback. A lower continuation must
     * still match its original join/reassociation sequences before acting. */
    return completion.association_epoch;
}

void
ieee80211_wcl_reassoc_post_failure(struct ieee80211com *ic, u_int32_t result)
{
    ieee80211_wcl_reassoc_post_failure_owned(ic,
        ieee80211_wcl_reassoc_serial(ic), result);
}

void
ieee80211_wcl_reassoc_target_running(struct ieee80211com *ic,
    const struct ieee80211_node *ni)
{
    if (ic == NULL || ni == NULL || !ic->ic_wcl_reassoc_owner_active ||
        ic->ic_wcl_reassoc_owner_last_leaf !=
            IEEE80211_WCL_REASSOC_OWNER_LEAF_ROAM_STARTED ||
        ic->ic_opmode != IEEE80211_M_STA ||
        ic->ic_state != IEEE80211_S_RUN || ic->ic_bss != ni ||
        !IEEE80211_ADDR_EQ(ni->ni_bssid,
            ic->ic_wcl_reassoc_target_bssid))
        return;
    /*
     * AppleBCMWLANCore::handleReassocEvent publishes the WCL 0x49 terminal
     * when firmware accepts the target reassociation, then enables its
     * supplicant-event stream.  For an RSN target that is S_RUN before the
     * four-way handshake opens the port.  Delay only the key-done fact until
     * port-valid; delaying 0x49 leaves WCLRoamManager in its pre-reassoc
     * state while those events arrive.
     */
    XYLog("wcl_reassoc TARGET_RUNNING bssid=%s\n",
          ether_sprintf(ni->ni_bssid));
    ieee80211_wcl_reassoc_post_success(ic);
}

void
ieee80211_wcl_reassoc_target_port_valid(struct ieee80211com *ic,
    const struct ieee80211_node *ni)
{
    /* Kept as a safe late edge for callers outside ieee80211_newstate().
     * The RUN edge above normally consumes the one-shot reassociation owner. */
    if (ic == NULL || ni == NULL ||
        ((ic->ic_flags & IEEE80211_F_RSNON) != 0 && !ni->ni_port_valid))
        return;
    ieee80211_wcl_reassoc_target_running(ic, ni);
}
