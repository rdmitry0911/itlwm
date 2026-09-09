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
/*	$OpenBSD: ieee80211_proto.h,v 1.46 2019/09/12 12:55:07 stsp Exp $	*/
/*	$NetBSD: ieee80211_proto.h,v 1.3 2003/10/13 04:23:56 dyoung Exp $	*/

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
 *
 * $FreeBSD: src/sys/net80211/ieee80211_proto.h,v 1.4 2003/08/19 22:17:03 sam Exp $
 */
#ifndef _NET80211_IEEE80211_PROTO_H_
#define _NET80211_IEEE80211_PROTO_H_

/*
 * 802.11 protocol implementation definitions.
 */

enum ieee80211_state {
	IEEE80211_S_INIT	= 0,	/* default state */
	IEEE80211_S_SCAN	= 1,	/* scanning */
	IEEE80211_S_AUTH	= 2,	/* try to authenticate */
	IEEE80211_S_ASSOC	= 3,	/* try to assoc */
	IEEE80211_S_RUN		= 4	/* associated */
};
#define	IEEE80211_S_MAX		(IEEE80211_S_RUN+1)

#define	IEEE80211_SEND_MGMT(_ic,_ni,_type,_arg) \
	((*(_ic)->ic_send_mgmt)(_ic, _ni, _type, _arg, 0))
/* shortcut */
#define IEEE80211_SEND_ACTION(_ic,_ni,_categ,_action,_arg) \
	((*(_ic)->ic_send_mgmt)(_ic, _ni, IEEE80211_FC0_SUBTYPE_ACTION, \
	    (_categ) << 16 | (_action), _arg))

extern	const char * const ieee80211_mgt_subtype_name[];
extern	const char * const ieee80211_state_name[IEEE80211_S_MAX];
extern	const char * const ieee80211_phymode_name[];

struct ieee80211_assoc_comeback_retry;

extern	void ieee80211_proto_attach(struct _ifnet *);
extern	void ieee80211_proto_detach(struct _ifnet *);
extern	int ieee80211_assoc_comeback_retry_complete(struct ieee80211com *,
	    const struct ieee80211_assoc_comeback_retry *);
extern	int ieee80211_assoc_comeback_retry_abort(struct ieee80211com *,
	    const struct ieee80211_assoc_comeback_retry *, int);

struct ieee80211_node;
struct ieee80211_rxinfo;
struct ieee80211_rsnparams;
struct ieee80211_pae_selected_bss;
struct ieee80211_sae_wcl_bound_request;
struct ItlSaeAuthTxRequestV1;
struct ItlSaeAuthPeerEventV1;
struct ItlSaePmkContinuationV1;
struct ItlSaePmkContinuationIdentityV1;

/* Private generic state argument for the direct SAE PMK continuation.  It is
 * never an on-air management subtype and may enter S_ASSOC only after the
 * exact local claim checks below succeed. */
#define IEEE80211_SAE_WCL_MGMT_PMK_CONTINUE 0x534145u

/* Private net80211 marker for the single ieee80211_next_scan() channel-hop
 * edge.  It crosses preflight/deferred replay to IWN's exact scan callback,
 * which selects its already-fenced cleanup before restoring the historic -1
 * management argument for all lower callbacks.  Queued IWM/IWX frontends
 * instead normalize it before recording task state, retaining their historic
 * generic cleanup until they have an equivalent request-identity fence. */
#define IEEE80211_NEWSTATE_ARG_SCAN_HOP (-2)
/* A public association replaces any scan command which was built before its
 * desired ESS/BSSID and RSN policy existed.  IWN carries this marker through
 * its fenced abort/replay path, preserves the just-armed initial-BSSID
 * provenance, and restores the historic -1 argument before lower callbacks. */
#define IEEE80211_NEWSTATE_ARG_PUBLIC_ASSOCIATE (-3)
/* A protected BTM scan has already confirmed its exact target before the
 * source deauthentication leaves the hardware queue.  The IWN backend uses
 * this one marker to enter S_SCAN and retire the source BSS without issuing
 * a second, generic physical scan.  The next WCL carrier may consume only
 * that still-confirmed target through the separate leaf-lock admission
 * below; no generic backend callback may observe this private value. */
#define IEEE80211_NEWSTATE_ARG_WNM_RECONNECT_HOLD (-4)
/*
 * The four direct-SAE hook fields are published and withdrawn under the
 * selected-BSS leaf.  Readers must take one coherent value snapshot before
 * dropping that leaf: a separate NULL check followed by a second field load
 * can otherwise race an IWN stop/detach unpublish into a NULL call.
 *
 * A copied function pointer remains valid only for the duration of the
 * callback itself.  The IWN owner supplies its own close/drain lease for
 * that interval; this structure deliberately carries no driver state.
 */
struct ieee80211_sae_driver_hook_snapshot {
	int	(*auth_hold)(struct ieee80211com *, struct ieee80211_node *,
		    enum ieee80211_state, int);
	int	(*auth_owned)(struct ieee80211com *,
		    const struct ieee80211_node *);
	int	(*engine_peer_event)(struct ieee80211com *,
		    const struct ItlSaeAuthPeerEventV1 *);
};
extern	void ieee80211_set_link_state(struct ieee80211com *, int);
extern	u_int ieee80211_get_hdrlen(const struct ieee80211_frame *);
extern	int ieee80211_classify(struct ieee80211com *, mbuf_t);
extern	void ieee80211_inputm(struct _ifnet *, mbuf_t,
		struct ieee80211_node *, struct ieee80211_rxinfo *,
		struct mbuf_list *);
extern	void ieee80211_input(struct _ifnet *, mbuf_t,
		struct ieee80211_node *, struct ieee80211_rxinfo *);
extern	int ieee80211_output(struct _ifnet *, mbuf_t, struct sockaddr *,
		struct rtentry *);
extern	void ieee80211_recv_mgmt(struct ieee80211com *, mbuf_t,
		struct ieee80211_node *, struct ieee80211_rxinfo *, int);
extern	int ieee80211_send_mgmt(struct ieee80211com *, struct ieee80211_node *,
		int, int, int);
/*
 * Dedicated, bounded SAE Algorithm-3 frame builder. This deliberately does
 * not use ieee80211_send_mgmt(): the generic AUTH path is Open-System-only.
 * The caller retains one reference on ni until the backend accepts m and
 * assumes that reference on successful TX submission.
 */
extern	mbuf_t ieee80211_sae_auth_frame_build(struct ieee80211com *,
		struct ieee80211_node *,
		const struct ItlSaeAuthTxRequestV1 *);
extern	void ieee80211_eapol_key_input(struct ieee80211com *, mbuf_t,
		struct ieee80211_node *);
extern	void ieee80211_tx_compressed_bar(struct ieee80211com *,
		struct ieee80211_node *, int, uint16_t);
extern	mbuf_t ieee80211_encap(struct _ifnet *, mbuf_t,
		struct ieee80211_node **);
extern	mbuf_t ieee80211_get_rts(struct ieee80211com *,
		const struct ieee80211_frame *, u_int16_t);
extern	mbuf_t ieee80211_get_cts_to_self(struct ieee80211com *,
		u_int16_t);
extern	mbuf_t ieee80211_get_compressed_bar(struct ieee80211com *,
		struct ieee80211_node *, int, uint16_t);
extern	mbuf_t ieee80211_beacon_alloc(struct ieee80211com *,
		struct ieee80211_node *);
extern int ieee80211_save_ie(const u_int8_t *, u_int8_t **);
extern int ieee80211_save_ie_tlv(const u_int8_t *, u_int8_t **, uint32_t *, uint32_t save_len);
extern	void ieee80211_eapol_timeout(void *);
extern	int ieee80211_send_4way_msg1(struct ieee80211com *,
		struct ieee80211_node *);
extern	int ieee80211_send_4way_msg2(struct ieee80211com *,
		struct ieee80211_node *, const u_int8_t *,
		const struct ieee80211_ptk *);
extern	int ieee80211_send_4way_msg3(struct ieee80211com *,
		struct ieee80211_node *);
extern	int ieee80211_send_4way_msg4(struct ieee80211com *,
		struct ieee80211_node *);
extern	int ieee80211_send_group_msg1(struct ieee80211com *,
		struct ieee80211_node *);
extern	int ieee80211_send_group_msg2(struct ieee80211com *,
		struct ieee80211_node *, const struct ieee80211_key *);
extern	int ieee80211_send_eapol_key_req(struct ieee80211com *,
		struct ieee80211_node *, u_int16_t, u_int64_t);
extern	int ieee80211_pwrsave(struct ieee80211com *, mbuf_t,
		struct ieee80211_node *);
extern	u_int64_t ieee80211_pae_assoc_epoch_current(const struct ieee80211com *);
extern	u_int64_t ieee80211_pae_assoc_epoch_begin(struct ieee80211com *);
extern	u_int64_t ieee80211_pae_assoc_epoch_begin_replacement(
	    struct ieee80211com *);
/* Public IOC_ASSOCIATE may use an initial BSSID only through this bounded
 * controller-owned marker.  Raw ioctl, legacy, and WCL BSSID pins never arm
 * it and therefore retain their existing persistent semantics. */
extern	void ieee80211_public_initial_bssid_pin_arm(struct ieee80211com *,
	    const u_int8_t[IEEE80211_ADDR_LEN]);
extern	void ieee80211_public_initial_bssid_pin_disarm(struct ieee80211com *);
extern	void ieee80211_public_initial_bssid_pin_port_valid(
	    struct ieee80211com *, struct ieee80211_node *);
/* True only for the exact public initial-BSS owner which has reached RUN but
 * has not yet opened its RSN port.  It lets Tahoe defer its otherwise-early
 * USE_APPLE_SUPPLICANT LINK_UP edge without changing WCL/raw semantics. */
extern	int ieee80211_public_initial_bssid_pin_should_defer_link_up(
	    struct ieee80211com *, struct ieee80211_node *);
extern	int ieee80211_wnm_bss_transition_arm(struct ieee80211com *,
	    const u_int8_t[IEEE80211_ADDR_LEN],
	    const u_int8_t[IEEE80211_ADDR_LEN], const u_int8_t *, u_int8_t,
	    u_int8_t, u_int8_t);
extern	void ieee80211_wnm_bss_transition_clear(struct ieee80211com *);
extern	int ieee80211_wnm_bss_transition_defer_fresh_scan(
	    struct ieee80211com *);
extern	int ieee80211_wnm_bss_transition_fresh_scan_pending(
	    struct ieee80211com *);
extern	int ieee80211_wnm_bss_transition_retry_fresh_scan(
	    struct ieee80211com *);
extern	void ieee80211_wnm_bss_transition_fresh_scan_started(
	    struct ieee80211com *);
/* Returns 0 with no owner, 1 for the exact target, and -1 for an active
 * transition whose target does not match this scan node. */
extern	int ieee80211_wnm_bss_transition_candidate_disposition(
	    struct ieee80211com *, const struct ieee80211_node *);
extern	int ieee80211_wnm_bss_transition_active(
	    struct ieee80211com *, u_int8_t *);
extern	int ieee80211_wnm_bss_transition_scan_start(
	    struct ieee80211com *);
extern	void ieee80211_wnm_bss_transition_scan_end(
	    struct ieee80211com *);
extern	int ieee80211_wnm_bss_transition_scan_owns_admission(
	    struct ieee80211com *);
/* Copies the exact non-zero Neighbor Report channel only while the protected
 * BTM caller is synchronously admitting its own physical scan. */
extern	int ieee80211_wnm_bss_transition_target_channel(
	    struct ieee80211com *, u_int8_t *);
extern	int ieee80211_wnm_bss_transition_confirm_candidate(
	    struct ieee80211com *, const struct ieee80211_node *, u_int8_t *,
	    u_int8_t[IEEE80211_ADDR_LEN]);
/*
 * A protected accepted BTM leave owns two exact management descriptors.
 * This explicit fence is independent of the current-BSS lifetime reference:
 * completion of the response and deauthentication, not a global node
 * refcount reaching zero, releases the driver-resident reconnect.
 */
#define IEEE80211_WNM_TX_FENCE_RESPONSE	0x01
#define IEEE80211_WNM_TX_FENCE_DEAUTH	0x02
extern	int ieee80211_wnm_bss_transition_tx_fence_arm(
	    struct ieee80211com *, const struct ieee80211_node *, u_int8_t,
	    const u_int8_t[IEEE80211_ADDR_LEN], u_int64_t *);
extern	void ieee80211_wnm_bss_transition_tx_fence_cancel(
	    struct ieee80211com *, u_int64_t);
extern	int ieee80211_wnm_bss_transition_tx_fence_classify(
	    struct ieee80211com *, const struct ieee80211_node *,
	    const struct ieee80211_frame *, size_t, u_int64_t *, u_int8_t *);
extern	int ieee80211_wnm_bss_transition_tx_fence_submit(
	    struct ieee80211com *, u_int64_t, u_int8_t);
extern	void ieee80211_wnm_bss_transition_tx_fence_submit_failed(
	    struct ieee80211com *, struct ieee80211_node *, u_int64_t,
	    u_int8_t);
extern	void ieee80211_wnm_bss_transition_tx_fence_complete(
	    struct ieee80211com *, struct ieee80211_node *, u_int64_t,
	    u_int8_t);
extern	int ieee80211_wnm_bss_transition_copy_retarget(
	    struct ieee80211com *, const u_int8_t *, u_int8_t,
	    u_int8_t[IEEE80211_ADDR_LEN]);
extern	void ieee80211_wnm_bss_transition_consume(
	    struct ieee80211com *, const u_int8_t *, u_int8_t,
	    const u_int8_t[IEEE80211_ADDR_LEN]);
extern	int ieee80211_send_bss_transition_response(struct ieee80211com *,
	    struct ieee80211_node *, u_int8_t, u_int8_t,
	    const u_int8_t[IEEE80211_ADDR_LEN]);
extern	void ieee80211_pae_mfp_txn_complete(struct ieee80211com *,
	    u_int64_t, u_int8_t, int);
extern	void ieee80211_pae_mfp_txn_abort(struct ieee80211com *);
/* Caller holds ic_pae_selected_bss_lock.  It transfers only a locally
 * prepared software BIP context and value state into the live association;
 * no callback, allocation, or retirement reap is permitted in this helper. */
extern	int ieee80211_pae_mfp_txn_finish_publish_locked(
	    struct ieee80211com *, u_int64_t);
extern	int ieee80211_pae_mfp_txn_begin(struct ieee80211com *,
	    struct ieee80211_node *, const struct ieee80211_ptk *,
	    const struct ieee80211_key *, int, const struct ieee80211_key *, int,
	    const struct ieee80211_key *, int, int, u_int64_t, u_int16_t, u_int8_t);
extern	void ieee80211_pae_selected_bss_lock_destroy(struct ieee80211com *);
extern	void ieee80211_pae_selected_bss_capture(struct ieee80211com *,
	    const struct ieee80211_node *, int, u_int64_t);
extern	int ieee80211_pae_selected_bss_copyout_current(struct ieee80211com *,
	    u_int64_t, struct ieee80211_pae_selected_bss *);
extern	void ieee80211_sae_driver_hook_snapshot_copyout(
	    struct ieee80211com *, struct ieee80211_sae_driver_hook_snapshot *);
/*
 * Direct-WCL SAE policy carries only an exact public SSID+BSSID selection.
 * publish() allocates a strictly increasing, nonzero generation and does not
 * retain a credential.  A caller that cannot stage its separate private
 * credential slot must clear the same generation explicitly.  resume_scan()
 * is a one-shot PENDING -> SCAN_STARTING transition; the IWN driver alone
 * promotes it to SCAN_ISSUED after a fresh scan submission succeeds.  It
 * invokes the driver's ic_newstate directly so the ordinary SCAN -> SCAN
 * cancellation fence cannot erase the controlled request before
 * node_join_bss binds it.
 */
/* begin() is the narrow pure-SAE policy entry: it configures only RSN/SAE,
 * CCMP/BIP and required MFP for one exact S_SCAN WCL request, then returns
 * the public generation to which the separately-owned credential must bind.
 * It carries no password or raw RSN IE and never starts a scan itself. */
extern	u_int64_t ieee80211_sae_wcl_request_begin(struct ieee80211com *,
	    const u_int8_t[IEEE80211_ADDR_LEN], const u_int8_t *, u_int);
extern	u_int64_t ieee80211_sae_wcl_request_publish(struct ieee80211com *,
	    const u_int8_t[IEEE80211_ADDR_LEN], const u_int8_t *, u_int);
extern	int ieee80211_sae_wcl_request_clear_if_generation(
	    struct ieee80211com *, u_int64_t);
extern	int ieee80211_sae_wcl_request_resume_scan(struct ieee80211com *,
	    u_int64_t);
/* IWN calls this only after it has observed a live older scan and before it
 * records the exact generation for terminal replay.  It rolls STARTING back
 * to selection-held PENDING; failure leaves the raw scan call terminal. */
extern	int ieee80211_sae_wcl_request_scan_deferred(struct ieee80211com *,
	    u_int64_t);
/* The driver queries an exact STARTING request only while servicing the raw
 * S_SCAN handoff.  It must promote the same generation only after a fresh
 * scan has been accepted; a coalesced pre-existing scan remains unowned. */
extern	int ieee80211_sae_wcl_request_scan_starting(struct ieee80211com *,
	    u_int64_t *);
extern	int ieee80211_sae_wcl_request_scan_started(struct ieee80211com *,
	    u_int64_t);
/* Promote one exact PENDING direct-SAE request without another physical scan
 * only while the protected WNM record still confirms the same SSID+BSSID.
 * This carries no credential or node and is false for every ordinary join. */
extern	int ieee80211_sae_wcl_request_admit_confirmed_wnm_candidate(
	    struct ieee80211com *, u_int64_t);
/* Promote the exact ordinary WCL-selected SSID+BSSID to the selection-owned
 * phase.  The caller separately proves that the physical scan owners are
 * idle and that the matching live node is still usable before joining it. */
extern	int ieee80211_sae_wcl_request_admit_cached_wcl_candidate(
	    struct ieee80211com *, u_int64_t,
	    const u_int8_t[IEEE80211_ADDR_LEN], const u_int8_t *, u_int);
extern	int ieee80211_sae_wcl_request_admit_cached_roam_candidate(
	    struct ieee80211com *, u_int64_t,
	    const u_int8_t[IEEE80211_ADDR_LEN], const u_int8_t *, u_int);
/* Retarget a completed direct-SAE RUN owner without first destroying its
 * source link.  The returned generation is an accepted lower handoff which
 * node_join_bss() may bind through exactly one controlled replacement.
 * rollback_run_retarget() is valid only before that replacement begins. */
extern	u_int64_t ieee80211_sae_wcl_request_retarget_run(
	    struct ieee80211com *, const struct ieee80211_node *, u_int64_t,
	    const u_int8_t[IEEE80211_ADDR_LEN], const u_int8_t *, u_int, int);
extern	int ieee80211_sae_wcl_request_rollback_run_retarget(
	    struct ieee80211com *, u_int64_t, u_int64_t,
	    const struct ieee80211_node *);
extern	int ieee80211_sae_wcl_request_admit_bss_loss_candidate(
	    struct ieee80211com *, u_int64_t,
	    const u_int8_t[IEEE80211_ADDR_LEN], const u_int8_t *, u_int);
/* During the one direct pure-SAE scan handoff the historical ESS list must
 * not overwrite the already-published RSN/SAE policy before BSS selection.
 * HOLD is the pre-publication/PENDING-or-STARTING half: end_scan() must
 * return without choosing an old result.  It is deliberately separate from
 * the issued selection predicate below, which allows exactly the new scan to
 * choose its BSS.  Both are read-only, credential-free, and false for
 * ordinary WCL. */
extern	int ieee80211_sae_wcl_request_scan_selection_held(
	    struct ieee80211com *);
extern	int ieee80211_sae_wcl_request_scan_selection_owned(
	    struct ieee80211com *);
/* node_join_bss() brackets its copy-to-S_AUTH window with this leaf-lock
 * fence so a concurrent direct-WCL publication fails busy rather than
 * preempting an already selected legacy join. */
/* Returns zero when a direct pure-SAE policy reservation has already won the
 * same pre-selection window; node_join_bss() must return to SCAN in that
 * case without touching the legacy candidate. */
extern	int ieee80211_sae_wcl_request_join_begin(struct ieee80211com *);
extern	void ieee80211_sae_wcl_request_join_end(struct ieee80211com *);
/* Bind only after node_join_bss copied its chosen BSS and published the
 * selected-BSS value for expected_epoch.  A live request that does not match
 * exactly is erased and returns BIND_REJECTED; no request returns BIND_NONE.
 */
extern	int ieee80211_sae_wcl_request_bind_selected_bss(
	    struct ieee80211com *, const struct ieee80211_node *, u_int64_t);
/* Read-side predicate for choose_rsnparams() and the eventual direct SAE
 * owner.  It succeeds only for the exact current BSS, selected profile, and
 * association epoch bound above. */
extern	int ieee80211_sae_wcl_request_bound_current(struct ieee80211com *,
	    const struct ieee80211_node *);
/* Copy an exact BOUND direct-WCL request into caller-owned value storage.
 * expected_generation == 0 atomically captures whichever exact current BOUND
 * request the first driver owner sees; a nonzero value is a stale-worker
 * fence.  The output contains only generation/epoch, BSSID/STA/SSID, and
 * selected scan profile facts; it never retains a node or credential/key
 * material. */
extern	int ieee80211_sae_wcl_request_copyout_bound_current(
	    struct ieee80211com *, u_int64_t,
	    struct ieee80211_sae_wcl_bound_request *);
/*
 * Admit one driver-owned Algorithm-3 peer-RX path for an exact direct-WCL
 * request already bound to the current BSS.  Unlike the controller-facing
 * admission below, this narrow owner path may use a scan-proven SAE|PSK
 * transition BSS, but only after it rechecks the copied BOUND request under
 * the selected-BSS leaf lock.  The request is public value data only.
 */
extern	int ieee80211_sae_wcl_peer_rx_admit(struct ieee80211com *,
	    const struct ieee80211_sae_wcl_bound_request *, u_int64_t);
/* Claim a verified direct-SAE completion while the caller holds
 * ic_pae_selected_bss_lock.  The continuation is copied only into the local
 * PAE/node stores; no callback, state transition, or controller handoff is
 * permitted under that leaf. */
extern	int ieee80211_sae_wcl_request_pmk_claim_locked(
	    struct ieee80211com *, const struct ItlSaePmkContinuationV1 *);
/* Check the one-shot claim for the exact current Association Request.  The
 * locked form is for IWN's final descriptor admission; the wrapper takes the
 * selected-BSS leaf itself. */
extern	int ieee80211_sae_wcl_request_pmk_claim_assoc_current_locked(
	    struct ieee80211com *, const struct ieee80211_node *,
	    const struct ItlSaePmkContinuationIdentityV1 *);
extern	int ieee80211_sae_wcl_request_pmk_claim_assoc_current(
	    struct ieee80211com *, const struct ieee80211_node *,
	    const struct ItlSaePmkContinuationIdentityV1 *);
/* Enter S_ASSOC through the private continuation sentinel after a successful
 * claim.  The generic S_ASSOC handler repeats current-BSS validation before
 * it can enqueue Association Request. */
extern	int ieee80211_sae_wcl_request_pmk_continue_assoc(
	    struct ieee80211com *, const struct ItlSaePmkContinuationIdentityV1 *);
/* A genuine new association carrier supersedes a retired direct-SAE owner
 * and may re-enable the ordinary Open/WPA2 association path. */
extern	void ieee80211_sae_wcl_fresh_carrier_accepted(
	    struct ieee80211com *);
/*
 * A controller may admit exactly one bounded Algorithm-3 peer-RX relay for
 * the current selected BSS.  The RX path receives a copied epoch/generation
 * only after snapshot_admission() validates that admission under the same
 * leaf lock.  These APIs never retain nodes, mbufs, credentials, or IEs.
 */
extern	int ieee80211_sae_peer_rx_admit(struct ieee80211com *, u_int64_t,
	    u_int64_t, const u_int8_t[IEEE80211_ADDR_LEN],
	    const u_int8_t[IEEE80211_ADDR_LEN]);
extern	void ieee80211_sae_peer_rx_revoke(struct ieee80211com *, u_int64_t,
	    u_int64_t);
extern	int ieee80211_sae_peer_rx_snapshot_admission(struct ieee80211com *,
	    const u_int8_t[IEEE80211_ADDR_LEN],
	    const u_int8_t[IEEE80211_ADDR_LEN], u_int64_t *, u_int64_t *);
extern	void ieee80211_pae_assoc_epoch_note_newstate(struct ieee80211com *,
		enum ieee80211_state, int);
#define IEEE80211_NEWSTATE_BACKEND_ARG(_nstate, _arg) \
	(((_nstate) == IEEE80211_S_SCAN && \
	  ((_arg) == IEEE80211_NEWSTATE_ARG_SCAN_HOP || \
	   (_arg) == IEEE80211_NEWSTATE_ARG_PUBLIC_ASSOCIATE || \
	   (_arg) == IEEE80211_NEWSTATE_ARG_WNM_RECONNECT_HOLD)) ? -1 : (_arg))
#define    ieee80211_new_state(_ic, _nstate, _arg) \
do {    \
if ((_ic)->ic_newstate_preflight == NULL || \
    ((_ic)->ic_newstate_preflight((_ic), (_nstate), (_arg)) == 0)) { \
ieee80211_pae_assoc_epoch_note_newstate((_ic), (_nstate), (_arg)); \
(((_ic)->ic_newstate)((_ic), (_nstate), (_arg)));   \
} \
} while (0)
extern	enum ieee80211_edca_ac ieee80211_up_to_ac(struct ieee80211com *, int);
extern	u_int8_t *ieee80211_add_capinfo(u_int8_t *, struct ieee80211com *,
		const struct ieee80211_node *);
extern	u_int8_t *ieee80211_add_ssid(u_int8_t *, const u_int8_t *, u_int);
extern	u_int8_t *ieee80211_add_rates(u_int8_t *,
		const struct ieee80211_rateset *);
extern	u_int8_t *ieee80211_add_fh_params(u_int8_t *, struct ieee80211com *,
		const struct ieee80211_node *);
extern	u_int8_t *ieee80211_add_ds_params(u_int8_t *, struct ieee80211com *,
		const struct ieee80211_node *);
extern	u_int8_t *ieee80211_add_tim(u_int8_t *, struct ieee80211com *);
extern	u_int8_t *ieee80211_add_ibss_params(u_int8_t *,
		const struct ieee80211_node *);
extern	u_int8_t *ieee80211_add_edca_params(u_int8_t *, struct ieee80211com *);
extern	u_int8_t *ieee80211_add_erp(u_int8_t *, struct ieee80211com *);
extern	u_int8_t *ieee80211_add_qos_capability(u_int8_t *,
		struct ieee80211com *);
extern	u_int8_t *ieee80211_add_rsn(u_int8_t *, struct ieee80211com *,
		const struct ieee80211_node *);
extern	u_int8_t *ieee80211_add_wpa(u_int8_t *, struct ieee80211com *,
		const struct ieee80211_node *);
extern	u_int8_t *ieee80211_add_xrates(u_int8_t *,
		const struct ieee80211_rateset *);
extern	u_int8_t *ieee80211_add_htcaps(u_int8_t *, struct ieee80211com *);
extern	u_int8_t *ieee80211_add_htop(u_int8_t *, struct ieee80211com *);
extern	u_int8_t *ieee80211_add_tie(u_int8_t *, u_int8_t, u_int32_t);
extern  u_int8_t *ieee80211_add_vhtcaps(u_int8_t *, struct ieee80211com *);
extern  u_int8_t *ieee80211_add_hecaps(u_int8_t *, struct ieee80211com *);
extern	int ieee80211_parse_rsn(struct ieee80211com *, const u_int8_t *,
		struct ieee80211_rsnparams *);
extern	int ieee80211_parse_wpa(struct ieee80211com *, const u_int8_t *,
		struct ieee80211_rsnparams *);
extern	void ieee80211_print_essid(const u_int8_t *, int);
#ifdef IEEE80211_DEBUG
extern	void ieee80211_dump_pkt(const u_int8_t *, int, int, int);
#endif
extern	int ieee80211_ibss_merge(struct ieee80211com *,
		struct ieee80211_node *, u_int64_t);
extern	void ieee80211_reset_erp(struct ieee80211com *);
extern	void ieee80211_set_shortslottime(struct ieee80211com *, int);
extern	void ieee80211_auth_open_confirm(struct ieee80211com *,
	    struct ieee80211_node *, uint16_t);
extern	void ieee80211_auth_open(struct ieee80211com *,
	    const struct ieee80211_frame *, struct ieee80211_node *,
	    struct ieee80211_rxinfo *rs, u_int16_t, u_int16_t);
extern	void ieee80211_stop_ampdu_tx(struct ieee80211com *,
	    struct ieee80211_node *, int);
extern	void ieee80211_gtk_rekey_timeout(void *);
extern	int ieee80211_keyrun(struct ieee80211com *, u_int8_t *);
extern	void ieee80211_setkeys(struct ieee80211com *);
extern	void ieee80211_setkeysdone(struct ieee80211com *);
extern	void ieee80211_sa_query_timeout(void *);
extern	void ieee80211_sa_query_request(struct ieee80211com *,
	    struct ieee80211_node *);
extern  void ieee80211_ht_negotiate_chw(struct ieee80211com *,
    struct ieee80211_node *);
extern	void ieee80211_ht_negotiate(struct ieee80211com *,
    struct ieee80211_node *);
extern  void ieee80211_vht_negotiate(struct ieee80211com *,
    struct ieee80211_node *);
extern  void ieee80211_he_negotiate(struct ieee80211com *,
    struct ieee80211_node *);
extern  void ieee80211_sta_set_rx_nss(struct ieee80211com *, struct ieee80211_node *);
extern	void ieee80211_tx_ba_timeout(void *);
extern	void ieee80211_rx_ba_timeout(void *);
extern	int ieee80211_addba_request(struct ieee80211com *,
	    struct ieee80211_node *,  u_int16_t, u_int8_t);
extern	void ieee80211_delba_request(struct ieee80211com *,
	    struct ieee80211_node *, u_int16_t, u_int8_t, u_int8_t);
extern	void ieee80211_addba_req_accept(struct ieee80211com *,
	    struct ieee80211_node *, uint8_t);
extern	void ieee80211_addba_req_refuse(struct ieee80211com *,
	    struct ieee80211_node *, uint8_t);
extern	void ieee80211_addba_resp_accept(struct ieee80211com *,
	    struct ieee80211_node *, uint8_t);
extern	void ieee80211_addba_resp_refuse(struct ieee80211com *,
	    struct ieee80211_node *, uint8_t, uint16_t);
extern	void ieee80211_output_ba_move_window(struct ieee80211com *,
	    struct ieee80211_node *, uint8_t, uint16_t);
extern	void ieee80211_output_ba_move_window_to_first_unacked(
	    struct ieee80211com *, struct ieee80211_node *, uint8_t, uint16_t);
extern	void ieee80211_output_ba_record_ack(struct ieee80211com *,
	    struct ieee80211_node *, uint8_t, uint16_t);

#endif /* _NET80211_IEEE80211_PROTO_H_ */
