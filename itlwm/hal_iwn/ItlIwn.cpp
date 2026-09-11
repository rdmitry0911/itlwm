/*
 * Copyright (C) 2020  pigworlds
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
/*    $OpenBSD: if_iwn.c,v 1.243 2020/11/12 15:16:18 krw Exp $    */

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

/*
 * Driver for Intel WiFi Link 4965 and 1000/5000/6000 Series 802.11 network
 * adapters.
 */

#include "ItlIwn.hpp"
#include "IwnHt40Contracts.hpp"
#include "IwnScanDwellBudget.hpp"
#include <HAL/ItlApFirmwareRuntime.hpp>
#include "../../AirportItlwm/TahoeNrateContracts.hpp"
#include <ClientKit/AirportItlwmPostPltiTraceBridge.h>
#include <ClientKit/AirportItlwmScanHomeAwayBridge.h>
#include <linux/types.h>
#include <linux/iwx_diag_log.h>
#include <linux/kernel.h>
#include <FwData.h>
#include <crypto/sha1.h>

#include <IOKit/IOInterruptController.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/network/IONetworkMedium.h>
#include <net/ethernet.h>
#include <net/if_llc.h>
#include <net80211/ieee80211_crypto.h>
#include <net80211/ieee80211_proto.h>
#include <net80211/ieee80211_sae_engine.h>
#include <net80211/ieee80211_sae_policy.h>

#include <sys/_task.h>
#include <kern/clock.h>
#include <sys/pcireg.h>

#if __IO80211_TARGET >= __MAC_26_0
extern "C" void airportItlwmRequestAPTxDequeue(
    IOEthernetController *controller);
#endif
extern "C" bool airportItlwmQueryIwmAPTxFreeSpace(
    ItlHalService *, uint32_t *);
extern "C" bool airportItlwmQueryIwxAPTxFreeSpace(
    ItlHalService *, uint32_t *);
extern "C" bool airportItlwmHandoffIwmPrimaryStaRecoveryScanToAP(
    ItlHalService *, IOReturn *);
extern "C" bool airportItlwmHandoffIwxPrimaryStaRecoveryScanToAP(
    ItlHalService *, IOReturn *);
extern "C" bool airportItlwmConsumeAPSTAPrimaryStaHandoffScan(
    IOEthernetController *, struct ieee80211com *, int);

#define super ItlHalService
OSDefineMetaClassAndStructors(ItlIwn, ItlHalService)

#define DEVNAME(_s)    ((_s)->sc_dev.dv_xname)

#define IC2IFP(_ic_) (&(_ic_)->ic_if)

#define IWN_DEBUG

#ifdef IWN_DEBUG
#define DPRINTF(x)    do { if (iwn_debug > 0) XYLog x; } while (0)
#define DPRINTFN(n, x)    do { if (iwn_debug >= (n)) XYLog x; } while (0)
int iwn_debug = 1;
#else
#define DPRINTF(x)    do { ; } while (0)
#define DPRINTFN(n, x)    do { ; } while (0)
#endif

#define M_DEVBUF 2

/* Linux DVM's per-device HT policy enables RTS/CTS for aggregate transport
 * on the 1000, 2000 and 6000 families.  Keep that hardware split explicit:
 * the 5000 family and 4965 use different scheduler/protection contracts. */
static bool
iwn_dvm_use_rts_for_aggregation(const struct iwn_softc *sc)
{
    switch (sc->hw_type) {
    case IWN_HW_REV_TYPE_1000:
    case IWN_HW_REV_TYPE_6000:
    case IWN_HW_REV_TYPE_6050:
    case IWN_HW_REV_TYPE_6005:
    case IWN_HW_REV_TYPE_2030:
    case IWN_HW_REV_TYPE_2000:
    case IWN_HW_REV_TYPE_105:
    case IWN_HW_REV_TYPE_135:
        return true;
    default:
        return false;
    }
}

static void iwn_clear_apple_nrate_cache(struct iwn_softc *sc);
static void iwn_publish_apple_nrate(struct iwn_softc *sc, uint32_t nrate);
static bool iwn_build_ht_apple_nrate(uint8_t rate, uint8_t rflags,
                                     uint32_t *nrate);
static void iwn_post_plti_trace_record_completion(struct ieee80211com *ic,
                                                   uint8_t txClass);
static enum iwn_scan_lease_owner iwn_scan_lease_begin_hardware_invalidation(
    struct iwn_softc *, struct ieee80211_wcl_scan_invalidation *,
    struct ieee80211_standard_scan_invalidation *, u_int64_t *);
static void iwn_scan_lease_retire_after_hardware_stop(struct iwn_softc *);
static bool iwn_scan_lease_live_locked(const struct iwn_softc *);
static bool iwn_scan_lease_owner_is_wcl(u_int8_t);
static int iwn_wcl_scan_initial_band(struct iwn_softc *, uint16_t *);
static bool iwn_sae_join_scan_block_promote(
    struct iwn_softc *, u_int64_t);
static bool iwn_sae_bss_loss_join_handoff_arm(
    struct iwn_softc *, u_int64_t);
static bool iwn_sae_bss_loss_join_handoff_completed(
    struct iwn_softc *, u_int64_t);
static void iwn_sae_join_scan_block_clear_generation(
    struct iwn_softc *, u_int64_t);
static bool iwn_sae_join_scan_blocked(struct iwn_softc *);
static void iwn_wcl_initial_scan_pending_clear_locked(struct iwn_softc *);
static bool iwn_scan_lease_mark_abort(
    struct iwn_softc *, enum iwn_scan_lease_owner, u_int64_t,
    u_int64_t *, bool *);
static void iwn_scan_lease_abort_submission_failed(
    struct iwn_softc *, u_int64_t);
int iwn_nic_lock(struct iwn_softc *);
void iwn_nic_unlock(struct iwn_softc *);
void iwn_prph_write(struct iwn_softc *, uint32_t, uint32_t);
void iwn_prph_setbits(struct iwn_softc *, uint32_t, uint32_t);
void iwn_prph_clrbits(struct iwn_softc *, uint32_t, uint32_t);
void iwn_mem_write(struct iwn_softc *, uint32_t, uint32_t);
void iwn_mem_write_2(struct iwn_softc *, uint32_t, uint16_t);
void iwn_mem_set_region_4(struct iwn_softc *, uint32_t, uint32_t, int);

/* The laboratory switch exposes diagnostic observation and stimulus only.
 * Product WCL SAE/PMF admission is tied separately to the Tahoe in-kext
 * crypto core below. */
#ifndef IWN_SOFTWARE_PMF_LAB_BUILD
#define IWN_SOFTWARE_PMF_LAB_BUILD 0
#endif

/* Direct-SAE runtime evidence exists only in the separately compiled lab
 * artifact.  Its recorder receives categorical facts after the real driver
 * boundary, never a peer identity, secret, ticket, status, descriptor, or
 * frame. */
#if IWN_SOFTWARE_PMF_LAB_BUILD
#define IWN_DIRECT_SAE_TRACE(_ic, _event) \
    AirportItlwmPostPltiTraceRecord((_ic), (_event))
#else
#define IWN_DIRECT_SAE_TRACE(_ic, _event) \
    do { (void)(_ic); (void)(_event); } while (0)
#endif

static bool
iwn_mfp_pae_runtime_opted_in(void)
{
#if ITL_SAE_DRIVER_CRYPTO_AVAILABLE
    return true;
#else
    return false;
#endif
}

/* The selected-BSS owner, in-kext Algorithm-3 engine, PMK continuation and
 * PMF transaction have completed physical IWN validation.  Enable them in
 * every Tahoe artifact which actually links that crypto core. */
static bool
iwn_sae_auth_transport_runtime_opted_in(void)
{
#if ITL_SAE_DRIVER_CRYPTO_AVAILABLE
    return true;
#else
    return false;
#endif
}

/* WCL CIPHER_PWD is the product credential ingress.  It stages one bounded
 * pre-selection record and remains unreachable on targets without the
 * driver-owned Tahoe crypto core. */
static bool
iwn_sae_wcl_credential_runtime_opted_in(void)
{
#if ITL_SAE_DRIVER_CRYPTO_AVAILABLE
    return true;
#else
    return false;
#endif
}

namespace {

static bool iwn_sae_engine_queue_terminal(struct iwn_softc *,
    const struct ItlSaeAuthTransportEventV1 *);

struct IwnSaeTxGateArgs {
    struct ItlSaeAuthTxRequestV1 request;
    IOReturn rc;
};

static void
iwn_sae_tx_make_terminal_event(const struct ItlSaeAuthTxRequestV1 *request,
    int32_t result, struct ItlSaeAuthTransportEventV1 *event)
{
    if (request == NULL || event == NULL)
        return;
    explicit_bzero(event, sizeof(*event));
    event->version = kItlSaeAuthTransportV1Version;
    event->size = sizeof(*event);
    event->kind = kItlSaeAuthTransportEventTxComplete;
    event->result = result;
    event->association_epoch = request->association_epoch;
    event->relay_generation = request->relay_generation;
    event->ticket = request->ticket;
    event->phase = request->phase;
    event->auth_status = request->auth_status;
    event->wire_transaction = request->wire_transaction;
    memcpy(event->bssid, request->bssid, sizeof(event->bssid));
    memcpy(event->sta, request->sta, sizeof(event->sta));
}

static void
iwn_sae_tx_make_terminal_event_from_data(const struct iwn_tx_data *data,
    int32_t result, struct ItlSaeAuthTransportEventV1 *event)
{
    if (data == NULL || event == NULL)
        return;
    explicit_bzero(event, sizeof(*event));
    event->version = kItlSaeAuthTransportV1Version;
    event->size = sizeof(*event);
    event->kind = kItlSaeAuthTransportEventTxComplete;
    event->result = result;
    event->association_epoch = data->sae_association_epoch;
    event->relay_generation = data->sae_relay_generation;
    event->ticket = data->sae_ticket;
    event->phase = data->sae_phase;
    event->auth_status = data->sae_auth_status;
    event->wire_transaction = data->sae_wire_transaction;
    memcpy(event->bssid, data->sae_bssid, sizeof(event->bssid));
    memcpy(event->sta, data->sae_sta, sizeof(event->sta));
}

static void
iwn_sae_tx_data_clear(struct iwn_tx_data *data)
{
    if (data == NULL)
        return;
    data->sae_active = false;
    data->sae_phase = 0;
    data->sae_auth_status = 0;
    data->sae_wire_transaction = 0;
    data->sae_association_epoch = 0;
    data->sae_relay_generation = 0;
    data->sae_ticket = 0;
    data->sae_lifecycle_generation = 0;
    explicit_bzero(data->sae_bssid, sizeof(data->sae_bssid));
    explicit_bzero(data->sae_sta, sizeof(data->sae_sta));
    data->wnm_tx_fence_generation = 0;
    data->wnm_tx_fence_kind = 0;
}

/* Caller holds sc_sae_wcl_credential_lock.  This is the sole erase path for
 * the private pre-selection password; the cancellation fence itself carries
 * only a public request generation. */
static void
iwn_sae_wcl_credential_clear_locked(struct iwn_softc *sc)
{
    if (sc == NULL)
        return;
    explicit_bzero(&sc->sc_sae_wcl_credential,
        sizeof(sc->sc_sae_wcl_credential));
    sc->sc_sae_wcl_credential_staged = false;
    sc->sc_sae_wcl_credential_pending = false;
    sc->sc_sae_wcl_credential_active = false;
    sc->sc_sae_bss_loss_recovery_armed = false;
    sc->sc_sae_driver_reset_recovery_pending = false;
    sc->sc_sae_bss_loss_recovery_generation = 0;
}

/* Reset/cancellation may retire a not-yet-validated credential without
 * erasing the record for an already port-valid SAE ESS.  A single value slot
 * means the three states are mutually exclusive. */
static void
iwn_sae_wcl_credential_clear_transient_locked(struct iwn_softc *sc)
{
    if (sc == NULL)
        return;
    if (sc->sc_sae_wcl_credential_staged ||
        sc->sc_sae_wcl_credential_pending)
        iwn_sae_wcl_credential_clear_locked(sc);
}

/* The caller supplies the fixed, already-validated ABI, so compare the full
 * canonical record without an early exit.  This runs only under the private
 * staging leaf and never exposes an equality result outside the HAL. */
static bool
iwn_sae_wcl_credential_equal(const struct ItlSaeWclCredentialV1 *left,
    const struct ItlSaeWclCredentialV1 *right)
{
    const uint8_t *left_bytes;
    const uint8_t *right_bytes;
    volatile uint8_t difference = 0;
    size_t index;

    if (left == NULL || right == NULL)
        return false;
    left_bytes = (const uint8_t *)left;
    right_bytes = (const uint8_t *)right;
    for (index = 0; index < sizeof(*left); index++)
        difference |= left_bytes[index] ^ right_bytes[index];
    return difference == 0;
}

/* The scan parser has reset ni_inact only for a beacon/probe response seen in
 * the current census.  Keep candidate discovery public and credential-free:
 * this predicate compares only the active record's SSID and the normalized
 * RSN/SAE facts already present in the node cache. */
static bool
iwn_sae_bss_loss_candidate_eligible(const struct ieee80211_node *ni,
    const struct ItlSaeWclCredentialV1 *credential)
{
    bool pure_sae;
    bool transition_sae;

    if (ni == NULL || credential == NULL || ni->ni_fails != 0 ||
        ni->ni_inact != 0 || ni->ni_chan == IEEE80211_CHAN_ANYC ||
        ni->ni_esslen != credential->ssid_len ||
        memcmp(ni->ni_essid, credential->ssid,
            credential->ssid_len) != 0)
        return false;

    pure_sae = ieee80211_sae_scan_profile_is_strict(
        ni->ni_supported_rsnprotos == IEEE80211_PROTO_RSN &&
            ni->ni_rsnprotos == IEEE80211_PROTO_RSN,
        ni->ni_supported_rsnakms == IEEE80211_AKM_SAE &&
            ni->ni_rsnakms == IEEE80211_AKM_SAE,
        (ni->ni_capinfo & IEEE80211_CAPINFO_ESS) != 0,
        (ni->ni_capinfo & IEEE80211_CAPINFO_IBSS) != 0,
        (ni->ni_capinfo & IEEE80211_CAPINFO_PRIVACY) != 0,
        (ni->ni_rsncaps & IEEE80211_RSNCAP_NOPAIRWISE) != 0,
        ni->ni_rsnciphers == IEEE80211_CIPHER_CCMP,
        ni->ni_rsngroupcipher == IEEE80211_CIPHER_CCMP,
        ni->ni_rsngroupmgmtcipher == IEEE80211_CIPHER_BIP,
        (ni->ni_rsncaps & IEEE80211_RSNCAP_MFPC) != 0,
        (ni->ni_rsncaps & IEEE80211_RSNCAP_MFPR) != 0,
        ni->ni_sae_scan_flags);
    transition_sae = ieee80211_sae_scan_profile_is_transition(
        ni->ni_supported_rsnprotos == IEEE80211_PROTO_RSN &&
            ni->ni_rsnprotos == IEEE80211_PROTO_RSN,
        ((ni->ni_supported_rsnakms ==
          (IEEE80211_AKM_SAE | IEEE80211_AKM_PSK) &&
          ni->ni_rsnakms ==
          (IEEE80211_AKM_SAE | IEEE80211_AKM_PSK)) ||
         (ni->ni_supported_rsnakms ==
          (IEEE80211_AKM_SAE | IEEE80211_AKM_PSK |
           IEEE80211_AKM_SHA256_PSK) &&
          ni->ni_rsnakms ==
          (IEEE80211_AKM_SAE | IEEE80211_AKM_PSK |
           IEEE80211_AKM_SHA256_PSK))),
        (ni->ni_capinfo & IEEE80211_CAPINFO_ESS) != 0,
        (ni->ni_capinfo & IEEE80211_CAPINFO_IBSS) != 0,
        (ni->ni_capinfo & IEEE80211_CAPINFO_PRIVACY) != 0,
        (ni->ni_rsncaps & IEEE80211_RSNCAP_NOPAIRWISE) != 0,
        ni->ni_rsnciphers == IEEE80211_CIPHER_CCMP,
        ni->ni_rsngroupcipher == IEEE80211_CIPHER_CCMP,
        ni->ni_rsngroupmgmtcipher == IEEE80211_CIPHER_BIP,
        (ni->ni_rsncaps & IEEE80211_RSNCAP_MFPC) != 0,
        (ni->ni_rsncaps & IEEE80211_RSNCAP_MFPR) != 0,
        ni->ni_sae_scan_flags);
    return pure_sae || transition_sae;
}

/* Caller holds sc_sae_wcl_credential_lock.  The generic WCL policy assigns
 * nonzero request generations in strict ascending order.  A cancellation is
 * therefore a high-water mark: preserving its maximum prevents a delayed
 * stage from resurrecting any earlier request, including when a newer cancel
 * races an older staged password. */
static bool
iwn_sae_wcl_credential_cancelled_locked(const struct iwn_softc *sc,
    uint64_t request_generation)
{
    return sc != NULL && request_generation != 0 &&
        sc->sc_sae_wcl_credential_cancel_valid &&
        request_generation <=
            sc->sc_sae_wcl_credential_cancel_through_generation;
}

static void
iwn_sae_wcl_credential_cancel_through_locked(struct iwn_softc *sc,
    uint64_t request_generation)
{
    if (sc == NULL || request_generation == 0)
        return;
    if (!sc->sc_sae_wcl_credential_cancel_valid ||
        request_generation >
            sc->sc_sae_wcl_credential_cancel_through_generation) {
        sc->sc_sae_wcl_credential_cancel_through_generation =
            request_generation;
        sc->sc_sae_wcl_credential_cancel_valid = true;
    }
    if ((sc->sc_sae_wcl_credential_staged ||
         sc->sc_sae_wcl_credential_pending) &&
        iwn_sae_wcl_credential_cancelled_locked(sc,
            sc->sc_sae_wcl_credential.request_generation))
        iwn_sae_wcl_credential_clear_locked(sc);
}

/* Consume a staged CIPHER_PWD only after a driver-owned selected join has
 * rebound every public identity.  The copy is worker-local; the sole private
 * value changes from STAGED to PENDING until port-valid proves the complete
 * SAE/PMF association.  No second durable secret copy is created. */
static bool
iwn_sae_wcl_credential_take_bound(struct iwn_softc *sc,
    const struct ItlSaeSelectedJoinEventV1 *selected,
    struct ItlSaeWclCredentialV1 *credential)
{
    bool taken = false;

    if (credential != NULL)
        explicit_bzero(credential, sizeof(*credential));
    if (sc == NULL || selected == NULL || credential == NULL ||
        sc->sc_sae_wcl_credential_lock == NULL ||
        !itl_sae_selected_join_event_is_well_formed(selected) ||
        selected->credential_source != 1u)
        return false;

    IOSimpleLockLock(sc->sc_sae_wcl_credential_lock);
    if (sc->sc_sae_wcl_credential_staged &&
        !iwn_sae_wcl_credential_cancelled_locked(sc,
            selected->request_generation) &&
        itl_sae_wcl_credential_is_well_formed(
            &sc->sc_sae_wcl_credential) &&
        sc->sc_sae_wcl_credential.request_generation ==
            selected->request_generation &&
        sc->sc_sae_wcl_credential.ssid_len == selected->ssid_len &&
        IEEE80211_ADDR_EQ(sc->sc_sae_wcl_credential.bssid,
            selected->bssid) &&
        memcmp(sc->sc_sae_wcl_credential.ssid, selected->ssid,
            selected->ssid_len) == 0) {
        *credential = sc->sc_sae_wcl_credential;
        sc->sc_sae_wcl_credential_staged = false;
        sc->sc_sae_wcl_credential_pending = true;
        sc->sc_sae_wcl_credential_active = false;
        taken = true;
    }
    IOSimpleLockUnlock(sc->sc_sae_wcl_credential_lock);
    return taken;
}

static void
iwn_sae_wcl_credential_retire_pending_generation(struct iwn_softc *sc,
    u_int64_t request_generation)
{
    if (sc == NULL || request_generation == 0 ||
        sc->sc_sae_wcl_credential_lock == NULL)
        return;
    IOSimpleLockLock(sc->sc_sae_wcl_credential_lock);
    if (sc->sc_sae_wcl_credential_pending &&
        sc->sc_sae_wcl_credential.request_generation ==
            request_generation)
        iwn_sae_wcl_credential_clear_locked(sc);
    IOSimpleLockUnlock(sc->sc_sae_wcl_credential_lock);
}

/* S_SCAN is the ordinary pre-selection path.  S_RUN is permitted only for a
 * live STA reconnect request: it records a future candidate while preserving
 * the current BSS unchanged.  The staging method never starts a scan,
 * changes state, queues a task, invokes an engine, or transmits a frame. */
static bool
iwn_sae_wcl_credential_stage_state_permitted(const struct ieee80211com *ic,
    const struct _ifnet *ifp)
{
    if (ic == NULL || ifp == NULL ||
        (ifp->if_flags & IFF_RUNNING) == 0 ||
        ic->ic_opmode != IEEE80211_M_STA)
        return false;
    if (ic->ic_state == IEEE80211_S_SCAN)
        return true;
    return ic->ic_state == IEEE80211_S_RUN && ic->ic_bss != NULL;
}

/*
 * The lifecycle lock guards private-gate lifetime, not the descriptor owner.
 * It is deliberately separate from sc_sae_tx_lock, which completion may take
 * in interrupt context.  The lock order wherever both are needed is
 * lifecycle -> SAE leaf.
 */
static bool
iwn_sae_tx_lifecycle_enter(struct iwn_softc *sc, bool allow_closed)
{
    bool entered = false;

    if (sc == NULL || sc->sc_sae_tx_lifecycle_lock == NULL)
        return false;
    IOLockLock(sc->sc_sae_tx_lifecycle_lock);
    if (!sc->sc_sae_tx_detaching &&
        (!sc->sc_sae_tx_lifecycle_closed || allow_closed)) {
        sc->sc_sae_tx_lifecycle_active++;
        entered = true;
    }
    IOLockUnlock(sc->sc_sae_tx_lifecycle_lock);
    return entered;
}

static void
iwn_sae_tx_lifecycle_leave(struct iwn_softc *sc)
{
    if (sc == NULL || sc->sc_sae_tx_lifecycle_lock == NULL)
        return;
    IOLockLock(sc->sc_sae_tx_lifecycle_lock);
    KASSERT(sc->sc_sae_tx_lifecycle_active != 0,
        "sc->sc_sae_tx_lifecycle_active != 0");
    if (sc->sc_sae_tx_lifecycle_active != 0)
        sc->sc_sae_tx_lifecycle_active--;
    IOLockWakeup(sc->sc_sae_tx_lifecycle_lock, sc, false);
    IOLockUnlock(sc->sc_sae_tx_lifecycle_lock);
}

static void
iwn_sae_tx_lifecycle_close(struct iwn_softc *sc, bool detaching)
{
    if (sc == NULL || sc->sc_sae_tx_lifecycle_lock == NULL)
        return;
    IOLockLock(sc->sc_sae_tx_lifecycle_lock);
    sc->sc_sae_tx_lifecycle_closed = true;
    if (detaching)
        sc->sc_sae_tx_detaching = true;
    IOLockUnlock(sc->sc_sae_tx_lifecycle_lock);
}

static bool
iwn_sae_tx_lifecycle_is_open(struct iwn_softc *sc)
{
    bool open = false;

    if (sc == NULL || sc->sc_sae_tx_lifecycle_lock == NULL)
        return false;
    IOLockLock(sc->sc_sae_tx_lifecycle_lock);
    open = !sc->sc_sae_tx_lifecycle_closed && !sc->sc_sae_tx_detaching;
    IOLockUnlock(sc->sc_sae_tx_lifecycle_lock);
    return open;
}

static void
iwn_sae_tx_lifecycle_drain(struct iwn_softc *sc)
{
    if (sc == NULL || sc->sc_sae_tx_lifecycle_lock == NULL)
        return;
    IOLockLock(sc->sc_sae_tx_lifecycle_lock);
    while (sc->sc_sae_tx_lifecycle_active != 0)
        IOLockSleep(sc->sc_sae_tx_lifecycle_lock, sc, THREAD_UNINT);
    IOLockUnlock(sc->sc_sae_tx_lifecycle_lock);
}

static void
iwn_sae_tx_generation_advance_locked(struct iwn_softc *sc)
{
    if (++sc->sc_sae_tx_generation == 0)
        ++sc->sc_sae_tx_generation;
}

/* The historical controller owns its monotonically increasing low ticket
 * domain.  A driver-owned engine must not advance that cancellation fence:
 * doing so would make a later controller request appear already cancelled.
 * The high bit is therefore a private, wire-invisible IWN domain; only the
 * existing local descriptor/event correlation observes it. */
#define IWN_SAE_ENGINE_TICKET_DIRECT_BIT \
    ((u_int64_t)kItlSaeAuthTransportV1DriverTicketBit)
#define IWN_SAE_ENGINE_TICKET_COUNTER_MASK \
    (~IWN_SAE_ENGINE_TICKET_DIRECT_BIT)

static bool
iwn_sae_tx_ticket_is_direct(u_int64_t ticket)
{
    return (ticket & IWN_SAE_ENGINE_TICKET_DIRECT_BIT) != 0;
}

static bool
iwn_sae_tx_ticket_cancelled_locked(const struct iwn_softc *sc,
    u_int64_t ticket)
{
    if (sc == NULL || ticket == 0)
        return true;
    if (iwn_sae_tx_ticket_is_direct(ticket))
        return (ticket & IWN_SAE_ENGINE_TICKET_COUNTER_MASK) <=
            sc->sc_sae_tx_direct_cancel_through;
    return ticket <= sc->sc_sae_tx_cancel_through;
}

static void
iwn_sae_tx_cancel_ticket_locked(struct iwn_softc *sc, u_int64_t ticket)
{
    u_int64_t counter;

    if (sc == NULL || ticket == 0)
        return;
    if (iwn_sae_tx_ticket_is_direct(ticket)) {
        counter = ticket & IWN_SAE_ENGINE_TICKET_COUNTER_MASK;
        if (counter > sc->sc_sae_tx_direct_cancel_through)
            sc->sc_sae_tx_direct_cancel_through = counter;
    } else if (ticket > sc->sc_sae_tx_cancel_through) {
        sc->sc_sae_tx_cancel_through = ticket;
    }
}

static bool
iwn_sae_tx_request_is_live(struct iwn_softc *sc, uint64_t ticket)
{
    bool live = false;

    if (sc == NULL || ticket == 0 || sc->sc_sae_tx_lifecycle_lock == NULL ||
        sc->sc_sae_tx_lock == NULL)
        return false;
    IOLockLock(sc->sc_sae_tx_lifecycle_lock);
    if (!sc->sc_sae_tx_lifecycle_closed && !sc->sc_sae_tx_detaching) {
        IOSimpleLockLock(sc->sc_sae_tx_lock);
        live = !sc->sc_sae_tx_stopping && sc->sc_sae_tx_active &&
            sc->sc_sae_tx_active_ticket == ticket &&
            sc->sc_sae_tx_active_generation == sc->sc_sae_tx_generation &&
            !iwn_sae_tx_ticket_cancelled_locked(sc, ticket);
        IOSimpleLockUnlock(sc->sc_sae_tx_lock);
    }
    IOLockUnlock(sc->sc_sae_tx_lifecycle_lock);
    return live;
}

static void
iwn_sae_tx_schedule_task(struct iwn_softc *sc, bool allow_closed)
{
    if (sc == NULL || sc->sc_sae_tx_lifecycle_lock == NULL ||
        systq == NULL)
        return;

    /* Gate-close and enqueue share this lock: detach cannot miss a task
     * requeued by a completion that it is about to barrier and destroy. */
    IOLockLock(sc->sc_sae_tx_lifecycle_lock);
    if ((!sc->sc_sae_tx_lifecycle_closed || allow_closed) &&
        !sc->sc_sae_tx_detaching && sc->sc_sae_tx_task_ready)
        (void)task_add(systq, &sc->sae_tx_task);
    IOLockUnlock(sc->sc_sae_tx_lifecycle_lock);
}

#define IWN_SAE_ENGINE_CALLBACK_CLOSED      0x80000000U
#define IWN_SAE_ENGINE_CALLBACK_COUNT_MASK  0x7fffffffU
#define IWN_SAE_ENGINE_TASK_ADMISSION_CLOSED      0x80000000U
#define IWN_SAE_ENGINE_TASK_ADMISSION_COUNT_MASK  0x7fffffffU
#define IWN_SCAN_LEASE_REPLAY_TASK_ADMISSION_CLOSED      0x80000000U
#define IWN_SCAN_LEASE_REPLAY_TASK_ADMISSION_COUNT_MASK  0x7fffffffU

/* Generic net80211 can retain a hook pointer briefly after it has dropped
 * its selected-BSS lock.  RX may invoke that copied pointer adjacent to an
 * interrupt, so admission uses a CAS lease rather than the sleeping SAE-TX
 * lifecycle mutex.  Detach closes first and drains only already-admitted
 * callbacks before it frees the private owner leaf. */
static bool
iwn_sae_engine_callback_enter(struct iwn_softc *sc)
{
    u_int32_t state, next;

    if (sc == NULL)
        return false;
    state = __atomic_load_n(&sc->sc_sae_engine_callback_state,
        __ATOMIC_ACQUIRE);
    for (;;) {
        if ((state & IWN_SAE_ENGINE_CALLBACK_CLOSED) != 0 ||
            (state & IWN_SAE_ENGINE_CALLBACK_COUNT_MASK) ==
            IWN_SAE_ENGINE_CALLBACK_COUNT_MASK)
            return false;
        next = state + 1;
        if (__atomic_compare_exchange_n(&sc->sc_sae_engine_callback_state,
            &state, next, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return true;
    }
}

static void
iwn_sae_engine_callback_leave(struct iwn_softc *sc)
{
    u_int32_t state, next;

    if (sc == NULL)
        return;
    state = __atomic_load_n(&sc->sc_sae_engine_callback_state,
        __ATOMIC_ACQUIRE);
    for (;;) {
        if ((state & IWN_SAE_ENGINE_CALLBACK_COUNT_MASK) == 0)
            return;
        next = state - 1;
        if (__atomic_compare_exchange_n(&sc->sc_sae_engine_callback_state,
            &state, next, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return;
    }
}

static void
iwn_sae_engine_callback_close(struct iwn_softc *sc)
{
    u_int32_t state, next;

    if (sc == NULL)
        return;
    state = __atomic_load_n(&sc->sc_sae_engine_callback_state,
        __ATOMIC_ACQUIRE);
    for (;;) {
        if ((state & IWN_SAE_ENGINE_CALLBACK_CLOSED) != 0)
            return;
        next = state | IWN_SAE_ENGINE_CALLBACK_CLOSED;
        if (__atomic_compare_exchange_n(&sc->sc_sae_engine_callback_state,
            &state, next, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return;
    }
}

static bool
iwn_sae_engine_callback_open(struct iwn_softc *sc)
{
    u_int32_t expected = IWN_SAE_ENGINE_CALLBACK_CLOSED;

    return sc != NULL && __atomic_compare_exchange_n(
        &sc->sc_sae_engine_callback_state, &expected, 0, false,
        __ATOMIC_RELEASE, __ATOMIC_ACQUIRE);
}

static void
iwn_sae_engine_callback_drain(struct iwn_softc *sc)
{
    if (sc == NULL)
        return;
    while ((__atomic_load_n(&sc->sc_sae_engine_callback_state,
        __ATOMIC_ACQUIRE) & IWN_SAE_ENGINE_CALLBACK_COUNT_MASK) != 0)
        IOSleep(1);
}

/* task_add() takes the taskq's sleeping lock, so it cannot run beneath the
 * engine simple lock.  This tiny CAS lease is instead the explicit handoff
 * between an RX/terminal producer and detach: close+drain guarantees every
 * pre-close task_add finished before detach removes its task and queues the
 * barrier, while a post-close producer cannot enqueue at all. */
static bool
iwn_sae_engine_task_admission_enter(struct iwn_softc *sc)
{
    u_int32_t state, next;

    if (sc == NULL)
        return false;
    state = __atomic_load_n(&sc->sc_sae_engine_task_admission_state,
        __ATOMIC_ACQUIRE);
    for (;;) {
        if ((state & IWN_SAE_ENGINE_TASK_ADMISSION_CLOSED) != 0 ||
            (state & IWN_SAE_ENGINE_TASK_ADMISSION_COUNT_MASK) ==
            IWN_SAE_ENGINE_TASK_ADMISSION_COUNT_MASK)
            return false;
        next = state + 1;
        if (__atomic_compare_exchange_n(
            &sc->sc_sae_engine_task_admission_state, &state, next, false,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return true;
    }
}

static void
iwn_sae_engine_task_admission_leave(struct iwn_softc *sc)
{
    u_int32_t state, next;

    if (sc == NULL)
        return;
    state = __atomic_load_n(&sc->sc_sae_engine_task_admission_state,
        __ATOMIC_ACQUIRE);
    for (;;) {
        if ((state & IWN_SAE_ENGINE_TASK_ADMISSION_COUNT_MASK) == 0)
            return;
        next = state - 1;
        if (__atomic_compare_exchange_n(
            &sc->sc_sae_engine_task_admission_state, &state, next, false,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return;
    }
}

static void
iwn_sae_engine_task_admission_close(struct iwn_softc *sc)
{
    u_int32_t state, next;

    if (sc == NULL)
        return;
    state = __atomic_load_n(&sc->sc_sae_engine_task_admission_state,
        __ATOMIC_ACQUIRE);
    for (;;) {
        if ((state & IWN_SAE_ENGINE_TASK_ADMISSION_CLOSED) != 0)
            return;
        next = state | IWN_SAE_ENGINE_TASK_ADMISSION_CLOSED;
        if (__atomic_compare_exchange_n(
            &sc->sc_sae_engine_task_admission_state, &state, next, false,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return;
    }
}

static void
iwn_sae_engine_task_admission_drain(struct iwn_softc *sc)
{
    if (sc == NULL)
        return;
    while ((__atomic_load_n(&sc->sc_sae_engine_task_admission_state,
        __ATOMIC_ACQUIRE) & IWN_SAE_ENGINE_TASK_ADMISSION_COUNT_MASK) != 0)
        IOSleep(1);
}

/* The scan-lease replay task is submitted from RX terminal handling after
 * its simple lock has been dropped.  This independently closes that enqueue
 * window before detach snapshots task_del()+barrier. */
static bool
iwn_scan_lease_replay_task_admission_enter(struct iwn_softc *sc)
{
    u_int32_t state, next;

    if (sc == NULL)
        return false;
    state = __atomic_load_n(&sc->sc_scan_lease_replay_task_admission_state,
        __ATOMIC_ACQUIRE);
    for (;;) {
        if ((state & IWN_SCAN_LEASE_REPLAY_TASK_ADMISSION_CLOSED) != 0 ||
            (state & IWN_SCAN_LEASE_REPLAY_TASK_ADMISSION_COUNT_MASK) ==
            IWN_SCAN_LEASE_REPLAY_TASK_ADMISSION_COUNT_MASK)
            return false;
        next = state + 1;
        if (__atomic_compare_exchange_n(
            &sc->sc_scan_lease_replay_task_admission_state, &state, next,
            false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return true;
    }
}

static void
iwn_scan_lease_replay_task_admission_leave(struct iwn_softc *sc)
{
    u_int32_t state, next;

    if (sc == NULL)
        return;
    state = __atomic_load_n(&sc->sc_scan_lease_replay_task_admission_state,
        __ATOMIC_ACQUIRE);
    for (;;) {
        if ((state & IWN_SCAN_LEASE_REPLAY_TASK_ADMISSION_COUNT_MASK) == 0)
            return;
        next = state - 1;
        if (__atomic_compare_exchange_n(
            &sc->sc_scan_lease_replay_task_admission_state, &state, next,
            false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return;
    }
}

static void
iwn_scan_lease_replay_task_admission_close(struct iwn_softc *sc)
{
    u_int32_t state, next;

    if (sc == NULL)
        return;
    state = __atomic_load_n(&sc->sc_scan_lease_replay_task_admission_state,
        __ATOMIC_ACQUIRE);
    for (;;) {
        if ((state & IWN_SCAN_LEASE_REPLAY_TASK_ADMISSION_CLOSED) != 0)
            return;
        next = state | IWN_SCAN_LEASE_REPLAY_TASK_ADMISSION_CLOSED;
        if (__atomic_compare_exchange_n(
            &sc->sc_scan_lease_replay_task_admission_state, &state, next,
            false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return;
    }
}

static void
iwn_scan_lease_replay_task_admission_drain(struct iwn_softc *sc)
{
    if (sc == NULL)
        return;
    while ((__atomic_load_n(&sc->sc_scan_lease_replay_task_admission_state,
        __ATOMIC_ACQUIRE) &
        IWN_SCAN_LEASE_REPLAY_TASK_ADMISSION_COUNT_MASK) != 0)
        IOSleep(1);
}

static void
iwn_scan_lease_schedule_replay_task(struct iwn_softc *sc)
{
    bool ready = false;

    if (sc == NULL || !iwn_scan_lease_replay_task_admission_enter(sc))
        return;
    if (sc->sc_scan_lease_lock != NULL) {
        IOSimpleLockLock(sc->sc_scan_lease_lock);
        ready = sc->sc_scan_lease_replay_task_ready;
        IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    }
    if (ready && systq != NULL)
        (void)task_add(systq, &sc->scan_lease_replay_task);
    iwn_scan_lease_replay_task_admission_leave(sc);
}

static bool
iwn_sae_engine_runtime_enabled(const struct iwn_softc *sc)
{
#if ITL_SAE_DRIVER_CRYPTO_AVAILABLE
    return sc != NULL && sc->sc_sae_engine_runtime_enabled &&
        sc->sc_sae_engine_lock != NULL &&
        sc->sc_sae_wcl_credential_lock != NULL &&
        sc->sc_sae_tx_lifecycle_lock != NULL && sc->sc_sae_tx_lock != NULL &&
        sc->sc_sae_tx_task_ready &&
        sc->sc_ic.ic_pae_selected_bss_lock != NULL;
#else
    (void)sc;
    return false;
#endif
}

/* No secret belongs to this record.  The crypto engine itself is worker-only
 * and destroyed separately by that worker after this public identity has
 * been retired. */
static void
iwn_sae_engine_owner_clear_locked(struct iwn_softc *sc)
{
    if (sc == NULL)
        return;
    explicit_bzero(&sc->sc_sae_engine_owner,
        sizeof(sc->sc_sae_engine_owner));
}

/* Generic selected-BSS callbacks must never enter the TX gate or the WCL
 * leaf.  They queue this monotonic public cancellation fence; the worker
 * performs the actual credential revoke after it owns task context. */
static void
iwn_sae_engine_queue_wcl_cancel_locked(struct iwn_softc *sc,
    u_int64_t request_generation)
{
    if (sc != NULL && request_generation != 0 &&
        request_generation > sc->sc_sae_engine_wcl_cancel_generation)
        sc->sc_sae_engine_wcl_cancel_generation = request_generation;
}

static u_int64_t
iwn_sae_engine_generation_advance_locked(struct iwn_softc *sc)
{
    u_int32_t generation;

    if (sc == NULL)
        return 0;
    generation = __atomic_load_n(&sc->sc_sae_engine_lifecycle_generation,
        __ATOMIC_ACQUIRE);
    if (++generation == 0)
        ++generation;
    __atomic_store_n(&sc->sc_sae_engine_lifecycle_generation, generation,
        __ATOMIC_RELEASE);
    return generation;
}

static bool
iwn_sae_engine_owner_matches_node_locked(const struct iwn_softc *sc,
    const struct ieee80211com *ic, const struct ieee80211_node *ni)
{
    const struct iwn_sae_engine_owner *owner;

    if (sc == NULL || ic == NULL || ni == NULL)
        return false;
    owner = &sc->sc_sae_engine_owner;
    return owner->active && owner->association_epoch != 0 &&
        owner->selected.request_generation == owner->request_generation &&
        owner->selected.association_epoch == owner->association_epoch &&
        owner->activated.request_generation == owner->request_generation &&
        owner->activated.association_epoch == owner->association_epoch &&
        owner->activated.relay_generation == owner->relay_generation &&
        ic->ic_bss == ni &&
        IEEE80211_ADDR_EQ(ni->ni_bssid, owner->selected.bssid) &&
        IEEE80211_ADDR_EQ(ic->ic_myaddr, owner->selected.sta) &&
        /* ieee80211_new_state advances its epoch before it calls the state
         * hook which finally leaves S_AUTH.  Keep a cancelled tombstone
         * authoritative over that narrow pre-transition window; otherwise a
         * late Open response could bypass the driver owner. */
        (ic->ic_state == IEEE80211_S_AUTH ||
         ieee80211_pae_assoc_epoch_current((struct ieee80211com *)ic) ==
         owner->association_epoch);
}

static bool
iwn_sae_engine_owner_matches_terminal_locked(const struct iwn_softc *sc,
    const struct ItlSaeAuthTransportEventV1 *event)
{
    const struct iwn_sae_engine_owner *owner;

    if (sc == NULL || event == NULL ||
        !itl_sae_auth_transport_event_is_well_formed(event))
        return false;
    owner = &sc->sc_sae_engine_owner;
    return owner->active && owner->in_flight_ticket != 0 &&
        owner->in_flight_ticket == event->ticket &&
        owner->association_epoch == event->association_epoch &&
        owner->relay_generation == event->relay_generation &&
        IEEE80211_ADDR_EQ(owner->selected.bssid, event->bssid) &&
        IEEE80211_ADDR_EQ(owner->selected.sta, event->sta);
}

static bool
iwn_sae_engine_owner_matches_peer_locked(const struct iwn_softc *sc,
    const struct ItlSaeAuthPeerEventV1 *event)
{
    const struct iwn_sae_engine_owner *owner;

    if (sc == NULL || event == NULL ||
        !itl_sae_auth_peer_event_is_well_formed(event))
        return false;
    owner = &sc->sc_sae_engine_owner;
    return owner->active && owner->association_epoch ==
        event->association_epoch && owner->relay_generation ==
        event->relay_generation &&
        IEEE80211_ADDR_EQ(owner->selected.bssid, event->bssid) &&
        IEEE80211_ADDR_EQ(owner->selected.sta, event->sta);
}

/* Caller holds ic_pae_selected_bss_lock followed by the engine leaf.  RX
 * first snapshots generic admission, then drops that leaf before it invokes
 * the hook.  Taking the same leaf here serializes enqueue against a WCL
 * clear/revoke which otherwise could win in that gap without changing BSS or
 * epoch.  Lock order is selected-BSS -> engine -> TX simple fence; no IWN
 * path takes the selected-BSS leaf while it holds the engine leaf. */
static bool
iwn_sae_engine_peer_owner_current_locked(const struct iwn_softc *sc,
    const struct iwn_sae_engine_owner *owner)
{
    const struct ieee80211com *ic;
    const struct ieee80211_sae_peer_rx_admission *admission;
    const struct ieee80211_sae_wcl_request *request;

    if (sc == NULL || owner == NULL || !owner->active)
        return false;
    ic = &sc->sc_ic;
    admission = &ic->ic_sae_peer_rx_admission;
    request = &ic->ic_sae_wcl_request;
    return ic->ic_state == IEEE80211_S_AUTH && ic->ic_bss != NULL &&
        ieee80211_pae_assoc_epoch_current((struct ieee80211com *)ic) ==
        owner->association_epoch &&
        __atomic_load_n(&ic->ic_pae_assoc_replace_epoch,
            __ATOMIC_ACQUIRE) == 0 &&
        __atomic_load_n(&ic->ic_pae_selected_bss.epoch,
            __ATOMIC_ACQUIRE) == owner->association_epoch &&
        IEEE80211_ADDR_EQ(ic->ic_bss->ni_bssid, owner->selected.bssid) &&
        IEEE80211_ADDR_EQ(ic->ic_myaddr, owner->selected.sta) &&
        admission->active != 0 &&
        admission->association_epoch == owner->association_epoch &&
        admission->relay_generation == owner->relay_generation &&
        IEEE80211_ADDR_EQ(admission->bssid, owner->selected.bssid) &&
        IEEE80211_ADDR_EQ(admission->sta, owner->selected.sta) &&
        request->phase == IEEE80211_SAE_WCL_REQUEST_BOUND &&
        request->generation == owner->request_generation &&
        request->association_epoch == owner->association_epoch &&
        request->ssid_len == owner->selected.ssid_len &&
        IEEE80211_ADDR_EQ(request->bssid, owner->selected.bssid) &&
        memcmp(request->ssid, owner->selected.ssid,
            owner->selected.ssid_len) == 0;
}

/* Caller holds the engine leaf.  This is the one deliberate engine -> TX
 * simple-lock nesting: it raises the real descriptor cancellation fence
 * before a concurrent IWN doorbell can linearize a now-revoked Commit or
 * Confirm.  No lifecycle lock, taskq operation, generic callback or WCL
 * leaf may occur under either lock. */
static void
iwn_sae_engine_fence_inflight_ticket_locked(struct iwn_softc *sc,
    const struct iwn_sae_engine_owner *owner)
{
    u_int64_t ticket;

    if (sc == NULL || owner == NULL ||
        (ticket = owner->in_flight_ticket) == 0 ||
        sc->sc_sae_tx_lock == NULL)
        return;
    IOSimpleLockLock(sc->sc_sae_tx_lock);
    iwn_sae_tx_cancel_ticket_locked(sc, ticket);
    if (sc->sc_sae_tx_active && !sc->sc_sae_tx_doorbelled &&
        sc->sc_sae_tx_active_ticket == ticket) {
        sc->sc_sae_tx_active = false;
        sc->sc_sae_tx_active_ticket = 0;
        sc->sc_sae_tx_active_generation = 0;
        explicit_bzero(&sc->sc_sae_tx_active_event,
            sizeof(sc->sc_sae_tx_active_event));
    }
    if (sc->sc_sae_tx_last_event_valid &&
        iwn_sae_tx_ticket_cancelled_locked(sc,
            sc->sc_sae_tx_last_event.ticket)) {
        sc->sc_sae_tx_last_event_valid = false;
        explicit_bzero(&sc->sc_sae_tx_last_event,
            sizeof(sc->sc_sae_tx_last_event));
    }
    IOSimpleLockUnlock(sc->sc_sae_tx_lock);
}

static void
iwn_sae_engine_schedule_task(struct iwn_softc *sc)
{
    if (sc == NULL)
        return;
    if (!iwn_sae_engine_task_admission_enter(sc))
        return;
    if (sc->sc_sae_engine_task_ready && systq != NULL)
        (void)task_add(systq, &sc->sae_engine_task);
    iwn_sae_engine_task_admission_leave(sc);
}

static void
iwn_sae_engine_wake_join_retirement(struct iwn_softc *sc)
{
    bool waiting;

    if (sc == NULL)
        return;
    if (sc->sc_sae_engine_lock == NULL) {
        if (sc->sc_sae_tx_lock == NULL)
            return;
        IOSimpleLockLock(sc->sc_sae_tx_lock);
        waiting = sc->sc_sae_tx_join_failure_generation != 0 &&
            !sc->sc_sae_tx_stopping;
        IOSimpleLockUnlock(sc->sc_sae_tx_lock);
        if (waiting)
            iwn_sae_tx_schedule_task(sc, false);
        return;
    }
    IOSimpleLockLock(sc->sc_sae_engine_lock);
    waiting = sc->sc_sae_engine_join_failure_generation != 0 &&
        !sc->sc_sae_engine_stopping && !sc->sc_sae_engine_detaching;
    IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    if (waiting)
        iwn_sae_engine_schedule_task(sc);
}

/* The no-engine case still runs through the ordinary transport worker,
 * after its local terminal value is scrubbed and with its lifecycle held. */
static void
iwn_sae_tx_finish_join_retirement(struct iwn_softc *sc)
{
    u_int64_t generation = 0;

    if (sc == NULL || sc->sc_sae_engine_lock != NULL ||
        sc->sc_sae_tx_lock == NULL)
        return;
    IOSimpleLockLock(sc->sc_sae_tx_lock);
    if (!sc->sc_sae_tx_stopping && !sc->sc_sae_tx_active &&
        sc->sc_sae_tx_event_count == 0) {
        generation = sc->sc_sae_tx_join_failure_generation;
        sc->sc_sae_tx_join_failure_generation = 0;
    }
    IOSimpleLockUnlock(sc->sc_sae_tx_lock);
    if (generation != 0)
        ieee80211_wcl_join_cleanup_done(&sc->sc_ic, generation,
            IEEE80211_JOIN_CLEANUP_SAE);
}

} // namespace

bool ItlIwn::
supportsDriverResidentSae()
{
    return iwn_sae_engine_runtime_enabled(&com);
}

/*
 * Report whether a new private WCL credential can reach the driver-owned SAE
 * worker without colliding with a physical scan or an older SAE request.
 * This method deliberately reads no credential bytes.  Its snapshot is only
 * an early secret-copy fence; stageSaeWclCredential() and the selected-BSS
 * worker still perform the authoritative checks after admission.
 */
bool ItlIwn::
isSaeWclCredentialAdmissionReady()
{
    struct iwn_softc *sc = &com;
    struct ieee80211com *ic = &sc->sc_ic;
    struct _ifnet *ifp = IC2IFP(ic);
    bool lifecycle_open = false;
    bool scan_idle = false;
    bool engine_idle = false;
    bool credential_empty = false;

    if (!iwn_sae_engine_runtime_enabled(sc) ||
        !iwn_sae_tx_lifecycle_enter(sc, false))
        return false;

    /*
     * Hold a lifecycle lease while visiting each separately ordered leaf so
     * stop/detach cannot free one between the pointer test and its lock.
     * Never hold two simple leaves together.
     */
    IOLockLock(sc->sc_sae_tx_lifecycle_lock);
    lifecycle_open = !sc->sc_sae_tx_lifecycle_closed &&
        !sc->sc_sae_tx_detaching && sc->sc_scan_lease_lock != NULL &&
        sc->sc_sae_engine_lock != NULL &&
        sc->sc_sae_wcl_credential_lock != NULL &&
        iwn_sae_wcl_credential_stage_state_permitted(ic, ifp);
    if (lifecycle_open) {
        IOSimpleLockLock(sc->sc_scan_lease_lock);
        scan_idle = !iwn_scan_lease_live_locked(sc) &&
            !sc->sc_wcl_initial_scan_pending.queued &&
            !sc->sc_sae_wcl_admission_reserved &&
            sc->sc_sae_join_scan_block_generation == 0 &&
            sc->sc_sae_bss_loss_join_handoff_generation == 0 &&
            (sc->sc_flags & IWN_FLAG_SCANNING) == 0;
        IOSimpleLockUnlock(sc->sc_scan_lease_lock);

        IOSimpleLockLock(sc->sc_sae_engine_lock);
        engine_idle = sc->sc_sae_engine_task_ready &&
            !sc->sc_sae_engine_stopping &&
            !sc->sc_sae_engine_detaching &&
            !sc->sc_sae_engine_owner.active &&
            sc->sc_sae_engine == NULL;
        IOSimpleLockUnlock(sc->sc_sae_engine_lock);

        IOSimpleLockLock(sc->sc_sae_wcl_credential_lock);
        credential_empty = !sc->sc_sae_wcl_credential_staged &&
            !sc->sc_sae_wcl_credential_pending;
        IOSimpleLockUnlock(sc->sc_sae_wcl_credential_lock);
    }
    IOLockUnlock(sc->sc_sae_tx_lifecycle_lock);
    iwn_sae_tx_lifecycle_leave(sc);

    return lifecycle_open && scan_idle && engine_idle && credential_empty;
}

bool ItlIwn::
reserveSaeWclCredentialAdmission()
{
    struct iwn_softc *sc = &com;
    struct ieee80211com *ic = &sc->sc_ic;
    struct _ifnet *ifp = IC2IFP(ic);
    bool reserved = false;
    bool lower_scan_deferable = false;
    bool engine_idle = false;
    bool credential_empty = false;

    if (!iwn_sae_engine_runtime_enabled(sc) ||
        !iwn_sae_tx_lifecycle_enter(sc, false))
        return false;

    /*
     * The scan leaf is the linearization point.  Once the identity-free bit
     * is set, every ordinary physical-scan reserve fails until the direct
     * SAE scan atomically consumes it.  The other leaves are checked without
     * nesting; a failure rolls the bit back before returning.
     */
    IOLockLock(sc->sc_sae_tx_lifecycle_lock);
    if (!sc->sc_sae_tx_lifecycle_closed && !sc->sc_sae_tx_detaching &&
        sc->sc_scan_lease_lock != NULL &&
        sc->sc_sae_engine_lock != NULL &&
        sc->sc_sae_wcl_credential_lock != NULL &&
        iwn_sae_wcl_credential_stage_state_permitted(ic, ifp)) {
        IOSimpleLockLock(sc->sc_scan_lease_lock);
        lower_scan_deferable =
            iwn_scan_lease_live_locked(sc) &&
            !sc->sc_scan_lease.hardware_invalidated &&
            !sc->sc_scan_lease.terminal_claimed &&
            sc->sc_scan_lease.phase != IWN_SCAN_LEASE_DRAINING &&
            ic->ic_state == IEEE80211_S_SCAN &&
            (sc->sc_flags & IWN_FLAG_SCANNING) != 0;
        if (!sc->sc_wcl_initial_scan_pending.queued &&
            !sc->sc_sae_wcl_admission_reserved &&
            sc->sc_sae_join_scan_block_generation == 0 &&
            sc->sc_sae_bss_loss_join_handoff_generation == 0 &&
            !sc->sc_ap_transition_scan_blocked &&
            ((!iwn_scan_lease_live_locked(sc) &&
              (sc->sc_flags & IWN_FLAG_SCANNING) == 0) ||
             lower_scan_deferable)) {
            sc->sc_sae_wcl_admission_reserved = true;
            sc->sc_sae_wcl_admission_requires_fresh_scan =
                lower_scan_deferable;
            reserved = true;
        }
        IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    }
    if (reserved) {
        IOSimpleLockLock(sc->sc_sae_engine_lock);
        engine_idle = sc->sc_sae_engine_task_ready &&
            !sc->sc_sae_engine_stopping &&
            !sc->sc_sae_engine_detaching &&
            !sc->sc_sae_engine_owner.active &&
            sc->sc_sae_engine == NULL;
        IOSimpleLockUnlock(sc->sc_sae_engine_lock);

        IOSimpleLockLock(sc->sc_sae_wcl_credential_lock);
        credential_empty = !sc->sc_sae_wcl_credential_staged &&
            !sc->sc_sae_wcl_credential_pending;
        IOSimpleLockUnlock(sc->sc_sae_wcl_credential_lock);
        if (!engine_idle || !credential_empty) {
            IOSimpleLockLock(sc->sc_scan_lease_lock);
            sc->sc_sae_wcl_admission_reserved = false;
            sc->sc_sae_wcl_admission_requires_fresh_scan = false;
            IOSimpleLockUnlock(sc->sc_scan_lease_lock);
            reserved = false;
        }
    }
    IOLockUnlock(sc->sc_sae_tx_lifecycle_lock);
    iwn_sae_tx_lifecycle_leave(sc);
    return reserved;
}

bool ItlIwn::
saeWclCredentialAdmissionRequiresFreshScan()
{
    struct iwn_softc *sc = &com;
    bool required = false;

    if (sc->sc_scan_lease_lock == NULL)
        return false;
    IOSimpleLockLock(sc->sc_scan_lease_lock);
    required = sc->sc_sae_wcl_admission_reserved &&
        sc->sc_sae_wcl_admission_requires_fresh_scan;
    IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    return required;
}

void ItlIwn::
releaseSaeWclCredentialAdmission()
{
    struct iwn_softc *sc = &com;

    if (!iwn_sae_tx_lifecycle_enter(sc, true))
        return;
    IOLockLock(sc->sc_sae_tx_lifecycle_lock);
    if (sc->sc_scan_lease_lock != NULL) {
        IOSimpleLockLock(sc->sc_scan_lease_lock);
        sc->sc_sae_wcl_admission_reserved = false;
        sc->sc_sae_wcl_admission_requires_fresh_scan = false;
        IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    }
    IOLockUnlock(sc->sc_sae_tx_lifecycle_lock);
    iwn_sae_tx_lifecycle_leave(sc);
}

IOReturn ItlIwn::
submitSaeAuthFrame(const struct ItlSaeAuthTxRequestV1 *request)
{
    struct iwn_softc *sc = &com;
    IwnSaeTxGateArgs args;
    IOCommandGate *gate = NULL;
    IOReturn rc = kIOReturnNotReady;

    if (!itl_sae_auth_transport_request_is_well_formed(request))
        return kIOReturnBadArgument;
    if (!iwn_sae_auth_transport_runtime_opted_in())
        return kIOReturnUnsupported;
    if (!iwn_sae_tx_lifecycle_enter(sc, false))
        return kIOReturnNotReady;

    /*
     * Admission and gate retention share the lifecycle lock.  Detach closes
     * it before removing fSaeTxGate, and drains this lease before freeing the
     * softc storage, so a copied submit never dereferences a stale gate.
     */
    IOLockLock(sc->sc_sae_tx_lifecycle_lock);
    if (!sc->sc_sae_tx_lifecycle_closed && !sc->sc_sae_tx_detaching &&
        sc->sc_sae_tx_task_ready && fSaeTxGate != NULL &&
        sc->sc_sae_tx_lock != NULL) {
        IOSimpleLockLock(sc->sc_sae_tx_lock);
        if (iwn_sae_tx_ticket_cancelled_locked(sc, request->ticket))
            rc = kIOReturnAborted;
        else if (sc->sc_sae_tx_stopping || sc->sc_sae_tx_active ||
                 sc->sc_sae_tx_event_count != 0)
            rc = kIOReturnNotReady;
        else {
            sc->sc_sae_tx_active = true;
            sc->sc_sae_tx_doorbelled = false;
            sc->sc_sae_tx_active_ticket = request->ticket;
            sc->sc_sae_tx_active_generation = sc->sc_sae_tx_generation;
            iwn_sae_tx_make_terminal_event(request, EIO,
                &sc->sc_sae_tx_active_event);
            sc->sc_sae_tx_last_event_valid = false;
            explicit_bzero(&sc->sc_sae_tx_last_event,
                sizeof(sc->sc_sae_tx_last_event));
            gate = fSaeTxGate;
            gate->retain();
            rc = kIOReturnSuccess;
        }
        IOSimpleLockUnlock(sc->sc_sae_tx_lock);
    }
    IOLockUnlock(sc->sc_sae_tx_lifecycle_lock);
    if (rc != kIOReturnSuccess) {
        iwn_sae_tx_lifecycle_leave(sc);
        return rc;
    }

    explicit_bzero(&args, sizeof(args));
    memcpy(&args.request, request, sizeof(args.request));
    args.rc = kIOReturnNotReady;
    /*
     * This is intentionally an IWN-private workloop gate.  It serializes the
     * bounded builder/TX leaf with IWN output and completion ownership, but
     * never re-enters AirportItlwm's controller policy gate.
     */
    rc = gate->attemptAction(&ItlIwn::iwn_sae_tx_gate_action, &args);
    gate->release();
    if (rc == kIOReturnCannotLock)
        rc = kIOReturnNotReady;
    if (rc != kIOReturnSuccess)
        iwn_sae_tx_retire_unsubmitted(sc, request->ticket);
    iwn_sae_tx_lifecycle_leave(sc);
    explicit_bzero(&args, sizeof(args));
    return rc;
}

void ItlIwn::
cancelSaeAuthFrame(uint64_t ticket)
{
    struct iwn_softc *sc = &com;

    if (ticket == 0 || !iwn_sae_tx_lifecycle_enter(sc, true))
        return;
    if (sc->sc_sae_tx_lock == NULL) {
        iwn_sae_tx_lifecycle_leave(sc);
        return;
    }

    IOSimpleLockLock(sc->sc_sae_tx_lock);
    iwn_sae_tx_cancel_ticket_locked(sc, ticket);
    /* A pre-doorbell reservation owns no descriptor and has no event. */
    if (sc->sc_sae_tx_active &&
        sc->sc_sae_tx_active_ticket == ticket &&
        !sc->sc_sae_tx_doorbelled) {
        sc->sc_sae_tx_active = false;
        sc->sc_sae_tx_active_ticket = 0;
        sc->sc_sae_tx_active_generation = 0;
        explicit_bzero(&sc->sc_sae_tx_active_event,
            sizeof(sc->sc_sae_tx_active_event));
    }
    if (sc->sc_sae_tx_last_event_valid &&
        iwn_sae_tx_ticket_cancelled_locked(sc,
            sc->sc_sae_tx_last_event.ticket)) {
        sc->sc_sae_tx_last_event_valid = false;
        explicit_bzero(&sc->sc_sae_tx_last_event,
            sizeof(sc->sc_sae_tx_last_event));
    }
    if (!sc->sc_sae_tx_active && sc->sc_sae_tx_event_count != 0) {
        explicit_bzero(sc->sc_sae_tx_eventq,
            sizeof(sc->sc_sae_tx_eventq));
        sc->sc_sae_tx_event_head = 0;
        sc->sc_sae_tx_event_tail = 0;
        sc->sc_sae_tx_event_count = 0;
    }
    IOSimpleLockUnlock(sc->sc_sae_tx_lock);
    iwn_sae_engine_wake_join_retirement(sc);
    iwn_sae_tx_lifecycle_leave(sc);
}

/*
 * Tahoe WCL supplies CIPHER_PWD before it asks net80211 to select a BSS.
 * This method owns the sole private value slot: initially STAGED, then
 * PENDING while SAE/PMF completes, and ACTIVE only for a port-valid ESS.  It
 * neither starts SAE, emits Authentication traffic, nor carries a secret
 * through an Agent/controller callback.
 */
IOReturn ItlIwn::
stageSaeWclCredential(const struct ItlSaeWclCredentialV1 *credential)
{
    struct iwn_softc *sc = &com;
    struct ieee80211com *ic = &sc->sc_ic;
    struct _ifnet *ifp = IC2IFP(ic);
    struct ItlSaeWclCredentialV1 copy;
    IOReturn rc = kIOReturnNotReady;

    if (credential == NULL)
        return kIOReturnBadArgument;
    if (!iwn_sae_wcl_credential_runtime_opted_in())
        return kIOReturnUnsupported;

    /* Copy before validation and never retain the caller's WCL buffer. */
    explicit_bzero(&copy, sizeof(copy));
    memcpy(&copy, credential, sizeof(copy));
    if (!itl_sae_wcl_credential_is_well_formed(&copy)) {
        rc = kIOReturnBadArgument;
        goto out;
    }
    if (!iwn_sae_tx_lifecycle_enter(sc, false))
        goto out;

    /* Lifecycle -> WCL leaf is the only nesting order.  Closing the
     * lifecycle blocks a late producer before reset/stop can scrub the
     * record, while the lease keeps this leaf allocated through the copy. */
    IOLockLock(sc->sc_sae_tx_lifecycle_lock);
    if (!sc->sc_sae_tx_lifecycle_closed && !sc->sc_sae_tx_detaching &&
        sc->sc_sae_wcl_credential_lock != NULL &&
        iwn_sae_wcl_credential_stage_state_permitted(ic, ifp)) {
        IOSimpleLockLock(sc->sc_sae_wcl_credential_lock);
        if (iwn_sae_wcl_credential_cancelled_locked(sc,
                copy.request_generation)) {
            rc = kIOReturnAborted;
        } else if (sc->sc_sae_wcl_credential_pending) {
            /* An association which already consumed the slot owns it until
             * port-valid promotion or deterministic cancellation. */
            rc = kIOReturnNotReady;
        } else if (sc->sc_sae_wcl_credential_active) {
            /* A fresh explicit carrier supersedes the old validated ESS.
             * Reuse the same storage only for a strictly newer generation. */
            if (copy.request_generation >
                sc->sc_sae_wcl_credential.request_generation) {
                iwn_sae_wcl_credential_clear_locked(sc);
                sc->sc_sae_wcl_credential = copy;
                sc->sc_sae_wcl_credential_staged = true;
                rc = kIOReturnSuccess;
            } else {
                rc = kIOReturnAborted;
            }
        } else if (!sc->sc_sae_wcl_credential_staged) {
            sc->sc_sae_wcl_credential = copy;
            sc->sc_sae_wcl_credential_staged = true;
            rc = kIOReturnSuccess;
        } else if (sc->sc_sae_wcl_credential.request_generation ==
            copy.request_generation) {
            if (iwn_sae_wcl_credential_equal(
                    &sc->sc_sae_wcl_credential, &copy)) {
                /* A WCL retry is harmless only when it is byte-for-byte the
                 * same canonical record; do not make a second secret copy. */
                rc = kIOReturnSuccess;
            } else {
                /* One generation is immutable.  A conflicting retry could
                 * otherwise select a password different from the candidate
                 * it claims to resume, so retire both representations. */
                iwn_sae_wcl_credential_cancel_through_locked(sc,
                    copy.request_generation);
                rc = kIOReturnAborted;
            }
        } else if (copy.request_generation >
            sc->sc_sae_wcl_credential.request_generation) {
            /* Skywalk intentionally carries only the new request identity;
             * it cannot safely rediscover the abandoned older generation.
             * Retire that exact old slot under the same leaf, then accept B
             * only after confirming its generation remains above the
             * cancellation high-water. */
            iwn_sae_wcl_credential_cancel_through_locked(sc,
                sc->sc_sae_wcl_credential.request_generation);
            if (!sc->sc_sae_wcl_credential_staged &&
                !iwn_sae_wcl_credential_cancelled_locked(sc,
                    copy.request_generation)) {
                sc->sc_sae_wcl_credential = copy;
                sc->sc_sae_wcl_credential_staged = true;
                rc = kIOReturnSuccess;
            } else {
                rc = kIOReturnAborted;
            }
        } else {
            /* An out-of-order candidate is never allowed to displace the
             * current slot or lower the monotonic cancellation fence. */
            rc = kIOReturnAborted;
        }
        IOSimpleLockUnlock(sc->sc_sae_wcl_credential_lock);
    }
    IOLockUnlock(sc->sc_sae_tx_lifecycle_lock);
    iwn_sae_tx_lifecycle_leave(sc);
out:
    explicit_bzero(&copy, sizeof(copy));
    return rc;
}

/* A cancellation is meaningful before a selected BSS exists.  Preserve its
 * monotonic high-water even if an older slot is currently staged: that newer
 * cancellation retires the old secret and fences every delayed predecessor. */
void ItlIwn::
cancelSaeWclCredential(uint64_t request_generation)
{
    struct iwn_softc *sc = &com;

    if (request_generation == 0 ||
        !iwn_sae_wcl_credential_runtime_opted_in() ||
        !iwn_sae_tx_lifecycle_enter(sc, true))
        return;
    /* Cancellation is the exact failed-join terminal.  Release radio
     * continuity before any recovery scan is requested by the SAE worker. */
    iwn_sae_join_scan_block_clear_generation(sc, request_generation);
    if (sc->sc_sae_wcl_credential_lock != NULL) {
        IOSimpleLockLock(sc->sc_sae_wcl_credential_lock);
        iwn_sae_wcl_credential_cancel_through_locked(sc,
            request_generation);
        IOSimpleLockUnlock(sc->sc_sae_wcl_credential_lock);
    }
    iwn_sae_tx_lifecycle_leave(sc);
}

/* A queue-overflow path has no trustworthy request generation.  Scrub only
 * a transient staged/pending value; a validated active ESS is a separate
 * roam owner and is not implicated by a producer queue overflow. */
void ItlIwn::
purgeSaeWclCredentialStage()
{
    struct iwn_softc *sc = &com;

    if (!iwn_sae_tx_lifecycle_enter(sc, true))
        return;
    if (sc->sc_sae_wcl_credential_lock != NULL) {
        IOSimpleLockLock(sc->sc_sae_wcl_credential_lock);
        iwn_sae_wcl_credential_clear_transient_locked(sc);
        IOSimpleLockUnlock(sc->sc_sae_wcl_credential_lock);
    }
    iwn_sae_tx_lifecycle_leave(sc);
}

IOReturn ItlIwn::
iwn_sae_tx_gate_action(OSObject *owner, void *arg0, void * /*arg1*/,
    void * /*arg2*/, void * /*arg3*/)
{
    ItlIwn *that = OSDynamicCast(ItlIwn, owner);
    IwnSaeTxGateArgs *args = (IwnSaeTxGateArgs *)arg0;

    if (that == NULL || args == NULL ||
        !itl_sae_auth_transport_request_is_well_formed(&args->request))
        return kIOReturnBadArgument;
    args->rc = that->iwn_sae_tx_submit_on_gate(&that->com, &args->request);
    return args->rc;
}

IOReturn ItlIwn::
iwn_sae_tx_submit_on_gate(struct iwn_softc *sc,
    const struct ItlSaeAuthTxRequestV1 *request)
{
    struct ieee80211com *ic;
    struct _ifnet *ifp;
    struct ieee80211_node *ni = NULL;
    mbuf_t m = NULL;
    IOReturn rc = kIOReturnError;
    int error;

    if (sc == NULL || !itl_sae_auth_transport_request_is_well_formed(request))
        return kIOReturnBadArgument;
    ic = &sc->sc_ic;
    ifp = IC2IFP(ic);
    if (!iwn_sae_tx_request_is_live(sc, request->ticket)) {
        iwn_sae_tx_retire_unsubmitted(sc, request->ticket);
        return kIOReturnAborted;
    }
    if (!(ifp->if_flags & IFF_RUNNING) || sc->qfullmsk != 0 ||
        ic->ic_bss == NULL) {
        iwn_sae_tx_retire_unsubmitted(sc, request->ticket);
        return kIOReturnNotReady;
    }

    ni = ieee80211_ref_node(ic->ic_bss);
    if (ni == NULL)
        goto out;
    m = ieee80211_sae_auth_frame_build(ic, ni, request);
    if (m == NULL) {
        rc = kIOReturnNoMemory;
        goto out;
    }

    /* Cancellation or a reset may have won during bounded frame assembly. */
    if (!iwn_sae_tx_request_is_live(sc, request->ticket)) {
        rc = kIOReturnAborted;
        goto out;
    }

    /*
     * The held node ref prevents UAF only.  These checks make the on-air
     * frame belong to the current selected BSS and current association epoch,
     * rather than a former scan candidate or a stale reconnect.
     */
    if (ic->ic_state != IEEE80211_S_AUTH || ic->ic_bss != ni ||
        ieee80211_pae_assoc_epoch_current(ic) != request->association_epoch ||
        memcmp(ni->ni_macaddr, request->bssid, sizeof(request->bssid)) != 0 ||
        memcmp(ni->ni_bssid, request->bssid, sizeof(request->bssid)) != 0 ||
        memcmp(ic->ic_myaddr, request->sta, sizeof(request->sta)) != 0)
        goto out;

    error = iwn_tx(sc, m, ni, request);
    /* iwn_tx() consumes m on every accepted or rejected direct path. */
    m = NULL;
    if (error != 0) {
        rc = error == ENOMEM ? kIOReturnNoMemory : kIOReturnError;
        goto out;
    }

    if (ifp->netStat != NULL)
        ifp->netStat->outputPackets++;
    if (ifp->if_flags & IFF_UP) {
        sc->sc_tx_timer = 5;
        ifp->if_timer = 1;
    }
    /* The accepted descriptor owns exactly this node reference to TX_DONE. */
    ni = NULL;
    return kIOReturnSuccess;

out:
    if (m != NULL)
        mbuf_freem(m);
    if (ni != NULL)
        ieee80211_release_node(ic, ni);
    iwn_sae_tx_retire_unsubmitted(sc, request->ticket);
    return rc;
}

bool ItlIwn::
iwn_sae_tx_commit_doorbell(struct iwn_softc *sc, uint64_t ticket,
    int qid, int descriptor_idx, int next_cur, uint8_t station_id,
    uint16_t length)
{
    bool committed = false;

    if (sc == NULL || ticket == 0 ||
        sc->sc_sae_tx_lifecycle_lock == NULL || sc->sc_sae_tx_lock == NULL)
        return false;

    /*
     * Stop/cancel and the hardware doorbell linearize under the lifecycle
     * then SAE leaf locks.  Scheduler mutation and WRPTR are in that same
     * critical section: cancellation either wins before either hardware
     * state is published or observes a descriptor that remains
     * firmware-owned to its native completion/reset.  This matters for 4965,
     * whose legacy reset_sched callback has no per-slot rollback.
     */
    IOLockLock(sc->sc_sae_tx_lifecycle_lock);
    if (!sc->sc_sae_tx_lifecycle_closed && !sc->sc_sae_tx_detaching) {
        IOSimpleLockLock(sc->sc_sae_tx_lock);
        committed = !sc->sc_sae_tx_stopping && sc->sc_sae_tx_active &&
            !sc->sc_sae_tx_doorbelled &&
            sc->sc_sae_tx_active_ticket == ticket &&
            sc->sc_sae_tx_active_generation == sc->sc_sae_tx_generation &&
            !iwn_sae_tx_ticket_cancelled_locked(sc, ticket);
        if (committed) {
            sc->ops.update_sched(sc, qid, descriptor_idx, station_id, length);
            sc->sc_sae_tx_doorbelled = true;
            IWN_WRITE(sc, IWN_HBUS_TARG_WRPTR, qid << 8 | next_cur);
        }
        IOSimpleLockUnlock(sc->sc_sae_tx_lock);
    }
    IOLockUnlock(sc->sc_sae_tx_lifecycle_lock);
    return committed;
}

bool ItlIwn::
iwn_sae_tx_queue_terminal(struct iwn_softc *sc,
    const struct ItlSaeAuthTransportEventV1 *event,
    uint32_t lifecycle_generation)
{
    bool queued = false;
    bool schedule_allow_closed = false;

    if (sc == NULL || event == NULL || sc->sc_sae_tx_lock == NULL ||
        !itl_sae_auth_transport_event_is_well_formed(event))
        return false;

    IOSimpleLockLock(sc->sc_sae_tx_lock);
    if (!sc->sc_sae_tx_active || !sc->sc_sae_tx_doorbelled ||
        sc->sc_sae_tx_active_ticket != event->ticket ||
        sc->sc_sae_tx_active_generation != lifecycle_generation) {
        IOSimpleLockUnlock(sc->sc_sae_tx_lock);
        return false;
    }

    /* Native TX_DONE (or reset reclaim) is the sole descriptor release. */
    sc->sc_sae_tx_active = false;
    sc->sc_sae_tx_doorbelled = false;
    sc->sc_sae_tx_active_ticket = 0;
    sc->sc_sae_tx_active_generation = 0;
    explicit_bzero(&sc->sc_sae_tx_active_event,
        sizeof(sc->sc_sae_tx_active_event));
    if (!iwn_sae_tx_ticket_cancelled_locked(sc, event->ticket)) {
        /* One live descriptor means FIFO saturation is an ownership bug,
         * not backpressure.  Preserve the present identity as a reset so
         * the controller cannot wait forever behind stale queued records. */
        KASSERT(sc->sc_sae_tx_event_count < IWN_SAE_TX_EVENTQ_LEN,
            "sc->sc_sae_tx_event_count < IWN_SAE_TX_EVENTQ_LEN");
        if (sc->sc_sae_tx_event_count >= IWN_SAE_TX_EVENTQ_LEN) {
            struct iwn_sae_tx_event_entry *entry;

            explicit_bzero(sc->sc_sae_tx_eventq,
                sizeof(sc->sc_sae_tx_eventq));
            sc->sc_sae_tx_event_head = 0;
            sc->sc_sae_tx_event_tail = 0;
            sc->sc_sae_tx_event_count = 0;
            entry = &sc->sc_sae_tx_eventq[0];
            entry->event = *event;
            entry->event.result = EIO;
            entry->is_reset = true;
            sc->sc_sae_tx_last_event = entry->event;
            sc->sc_sae_tx_last_event_valid = true;
            sc->sc_sae_tx_event_tail = 1;
            sc->sc_sae_tx_event_count = 1;
            queued = true;
            schedule_allow_closed = true;
        } else {
            struct iwn_sae_tx_event_entry *entry =
                &sc->sc_sae_tx_eventq[sc->sc_sae_tx_event_tail];
            entry->event = *event;
            entry->is_reset = false;
            sc->sc_sae_tx_last_event = *event;
            sc->sc_sae_tx_last_event_valid = true;
            sc->sc_sae_tx_event_tail = (sc->sc_sae_tx_event_tail + 1) %
                IWN_SAE_TX_EVENTQ_LEN;
            sc->sc_sae_tx_event_count++;
            queued = true;
        }
    }
    IOSimpleLockUnlock(sc->sc_sae_tx_lock);

    /* A cancelled ticket reaches terminal state but is deliberately silent. */
    if (queued)
        iwn_sae_tx_schedule_task(sc, schedule_allow_closed);
    /* Even a silent cancelled descriptor must release a failed join which
     * is waiting for real TX_DONE, not merely the cancellation fence. */
    iwn_sae_engine_wake_join_retirement(sc);
    return true;
}

void ItlIwn::
iwn_sae_tx_report_terminal(struct iwn_softc *sc, struct iwn_tx_data *data,
    int32_t result)
{
    struct ItlSaeAuthTransportEventV1 event;
    uint32_t lifecycle_generation;

    if (data == NULL || !data->sae_active)
        return;
    lifecycle_generation = data->sae_lifecycle_generation;
    iwn_sae_tx_make_terminal_event_from_data(data, result, &event);
    iwn_sae_tx_data_clear(data);
    (void)iwn_sae_tx_queue_terminal(sc, &event, lifecycle_generation);
    explicit_bzero(&event, sizeof(event));
}

void ItlIwn::
iwn_sae_tx_retire_unsubmitted(struct iwn_softc *sc, uint64_t ticket)
{
    if (sc == NULL || ticket == 0 || sc->sc_sae_tx_lock == NULL)
        return;

    IOSimpleLockLock(sc->sc_sae_tx_lock);
    iwn_sae_tx_cancel_ticket_locked(sc, ticket);
    /* This helper is only valid before the hardware doorbell. */
    if (sc->sc_sae_tx_active && !sc->sc_sae_tx_doorbelled &&
        sc->sc_sae_tx_active_ticket == ticket) {
        sc->sc_sae_tx_active = false;
        sc->sc_sae_tx_active_ticket = 0;
        sc->sc_sae_tx_active_generation = 0;
        explicit_bzero(&sc->sc_sae_tx_active_event,
            sizeof(sc->sc_sae_tx_active_event));
    }
    if (sc->sc_sae_tx_last_event_valid &&
        iwn_sae_tx_ticket_cancelled_locked(sc,
            sc->sc_sae_tx_last_event.ticket)) {
        sc->sc_sae_tx_last_event_valid = false;
        explicit_bzero(&sc->sc_sae_tx_last_event,
            sizeof(sc->sc_sae_tx_last_event));
    }
    IOSimpleLockUnlock(sc->sc_sae_tx_lock);
    iwn_sae_engine_wake_join_retirement(sc);
}

void ItlIwn::
iwn_sae_tx_stop_begin(struct iwn_softc *sc)
{
    if (sc == NULL)
        return;

    /* Do not wait here: iwn_hw_stop() is also a calibration-reset edge. */
    iwn_sae_tx_lifecycle_close(sc, false);
    if (sc->sc_sae_tx_lock == NULL)
        return;
    IOSimpleLockLock(sc->sc_sae_tx_lock);
    sc->sc_sae_tx_stopping = true;
    sc->sc_sae_tx_join_failure_generation = 0;
    iwn_sae_tx_generation_advance_locked(sc);
    /* A reservation not yet doorbelled has no physical owner to retain. */
    if (sc->sc_sae_tx_active && !sc->sc_sae_tx_doorbelled) {
        iwn_sae_tx_cancel_ticket_locked(sc,
            sc->sc_sae_tx_active_ticket);
        sc->sc_sae_tx_active = false;
        sc->sc_sae_tx_active_ticket = 0;
        sc->sc_sae_tx_active_generation = 0;
        explicit_bzero(&sc->sc_sae_tx_active_event,
            sizeof(sc->sc_sae_tx_active_event));
    }
    IOSimpleLockUnlock(sc->sc_sae_tx_lock);
}

/* Stop is a pre-selection secret boundary too.  Close the shared lifecycle
 * before taking the WCL leaf, then retire any staged/pending generation.  A
 * port-valid ACTIVE value survives the hardware reset used by sleep/wake so
 * the same ESS can reconnect or honor a later protected BTM without asking
 * WCL for the password again; detach and a fresh explicit carrier remain
 * hard scrub/replacement boundaries. */
void ItlIwn::
iwn_sae_wcl_stop_begin(struct iwn_softc *sc)
{
    uint64_t generation = 0;

    if (sc == NULL)
        return;
    iwn_sae_tx_lifecycle_close(sc, false);
    if (sc->sc_scan_lease_lock != NULL) {
        IOSimpleLockLock(sc->sc_scan_lease_lock);
        sc->sc_sae_wcl_admission_reserved = false;
        sc->sc_sae_wcl_admission_requires_fresh_scan = false;
        sc->sc_sae_join_scan_block_generation = 0;
        sc->sc_sae_bss_loss_join_handoff_generation = 0;
        IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    }
    if (sc->sc_sae_wcl_credential_lock == NULL)
        return;
    IOSimpleLockLock(sc->sc_sae_wcl_credential_lock);
    if (sc->sc_sae_wcl_credential_staged ||
        sc->sc_sae_wcl_credential_pending)
        generation = sc->sc_sae_wcl_credential.request_generation;
    if (generation != 0)
        iwn_sae_wcl_credential_cancel_through_locked(sc, generation);
    else
        iwn_sae_wcl_credential_clear_transient_locked(sc);
    IOSimpleLockUnlock(sc->sc_sae_wcl_credential_lock);
}

/* iwn_sae_tx_detach_begin() has already closed and drained the shared lease.
 * Keep this separate from the ordinary stop edge so the detach sequencing is
 * explicit before the leaf lock itself is freed. */
void ItlIwn::
iwn_sae_wcl_detach_begin(struct iwn_softc *sc)
{
    if (sc == NULL)
        return;
    iwn_sae_tx_lifecycle_close(sc, true);
    if (sc->sc_scan_lease_lock != NULL) {
        IOSimpleLockLock(sc->sc_scan_lease_lock);
        sc->sc_sae_wcl_admission_reserved = false;
        sc->sc_sae_wcl_admission_requires_fresh_scan = false;
        sc->sc_sae_join_scan_block_generation = 0;
        sc->sc_sae_bss_loss_join_handoff_generation = 0;
        IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    }
    if (sc->sc_sae_wcl_credential_lock == NULL)
        return;
    IOSimpleLockLock(sc->sc_sae_wcl_credential_lock);
    iwn_sae_wcl_credential_clear_locked(sc);
    IOSimpleLockUnlock(sc->sc_sae_wcl_credential_lock);
}

void ItlIwn::
iwn_sae_tx_cancel_all(struct iwn_softc *sc)
{
    if (sc == NULL || sc->sc_sae_tx_lock == NULL)
        return;

    IOSimpleLockLock(sc->sc_sae_tx_lock);
    iwn_sae_tx_cancel_ticket_locked(sc, sc->sc_sae_tx_active_ticket);
    if (sc->sc_sae_tx_active && !sc->sc_sae_tx_doorbelled) {
        sc->sc_sae_tx_active = false;
        sc->sc_sae_tx_active_ticket = 0;
        sc->sc_sae_tx_active_generation = 0;
        explicit_bzero(&sc->sc_sae_tx_active_event,
            sizeof(sc->sc_sae_tx_active_event));
    }
    sc->sc_sae_tx_last_event_valid = false;
    explicit_bzero(&sc->sc_sae_tx_last_event,
        sizeof(sc->sc_sae_tx_last_event));
    /* Deferred normal completions are stale at a hardware-reset boundary. */
    explicit_bzero(sc->sc_sae_tx_eventq, sizeof(sc->sc_sae_tx_eventq));
    sc->sc_sae_tx_event_head = 0;
    sc->sc_sae_tx_event_tail = 0;
    sc->sc_sae_tx_event_count = 0;
    sc->sc_sae_tx_join_failure_generation = 0;
    IOSimpleLockUnlock(sc->sc_sae_tx_lock);
}

void ItlIwn::
iwn_sae_tx_reopen(struct iwn_softc *sc)
{
    if (sc == NULL || sc->sc_sae_tx_lifecycle_lock == NULL ||
        sc->sc_sae_tx_lock == NULL)
        return;

    IOLockLock(sc->sc_sae_tx_lifecycle_lock);
    if (!sc->sc_sae_tx_detaching) {
        IOSimpleLockLock(sc->sc_sae_tx_lock);
        iwn_sae_tx_generation_advance_locked(sc);
        sc->sc_sae_tx_stopping = false;
        IOSimpleLockUnlock(sc->sc_sae_tx_lock);
        sc->sc_sae_tx_lifecycle_closed = false;
    }
    IOLockUnlock(sc->sc_sae_tx_lifecycle_lock);
}

bool ItlIwn::
iwn_sae_tx_snapshot_reset(struct iwn_softc *sc,
    struct ItlSaeAuthTransportEventV1 *snapshot)
{
    bool have_snapshot = false;

    if (sc == NULL || snapshot == NULL || sc->sc_sae_tx_lock == NULL)
        return false;
    explicit_bzero(snapshot, sizeof(*snapshot));
    IOSimpleLockLock(sc->sc_sae_tx_lock);
    if (sc->sc_sae_tx_active && sc->sc_sae_tx_doorbelled &&
        !iwn_sae_tx_ticket_cancelled_locked(sc,
            sc->sc_sae_tx_active_ticket) &&
        itl_sae_auth_transport_event_is_well_formed(
            &sc->sc_sae_tx_active_event)) {
        *snapshot = sc->sc_sae_tx_active_event;
        snapshot->result = EIO;
        have_snapshot = true;
    } else if (sc->sc_sae_tx_event_count != 0) {
        const struct ItlSaeAuthTransportEventV1 *event =
            &sc->sc_sae_tx_eventq[sc->sc_sae_tx_event_head].event;
        if (!iwn_sae_tx_ticket_cancelled_locked(sc, event->ticket) &&
            itl_sae_auth_transport_event_is_well_formed(event)) {
            *snapshot = *event;
            snapshot->result = EIO;
            have_snapshot = true;
        }
    } else if (sc->sc_sae_tx_last_event_valid &&
        !iwn_sae_tx_ticket_cancelled_locked(sc,
            sc->sc_sae_tx_last_event.ticket) &&
        itl_sae_auth_transport_event_is_well_formed(
            &sc->sc_sae_tx_last_event)) {
        *snapshot = sc->sc_sae_tx_last_event;
        snapshot->result = EIO;
        have_snapshot = true;
    }
    IOSimpleLockUnlock(sc->sc_sae_tx_lock);
    return have_snapshot;
}

void ItlIwn::
iwn_sae_tx_emit_reset_event(struct iwn_softc *sc,
    const struct ItlSaeAuthTransportEventV1 *event)
{
    bool queued = false;

    if (sc == NULL || event == NULL || sc->sc_sae_tx_lock == NULL ||
        !itl_sae_auth_transport_event_is_well_formed(event) ||
        !iwn_sae_tx_lifecycle_enter(sc, true))
        return;
    IOLockLock(sc->sc_sae_tx_lifecycle_lock);
    IOSimpleLockLock(sc->sc_sae_tx_lock);
    if (!sc->sc_sae_tx_detaching && sc->sc_sae_tx_task_ready &&
        sc->sc_sae_tx_event_count < IWN_SAE_TX_EVENTQ_LEN) {
        struct iwn_sae_tx_event_entry *entry =
            &sc->sc_sae_tx_eventq[sc->sc_sae_tx_event_tail];
        entry->event = *event;
        entry->is_reset = true;
        sc->sc_sae_tx_event_tail = (sc->sc_sae_tx_event_tail + 1) %
            IWN_SAE_TX_EVENTQ_LEN;
        sc->sc_sae_tx_event_count++;
        queued = true;
    }
    IOSimpleLockUnlock(sc->sc_sae_tx_lock);
    IOLockUnlock(sc->sc_sae_tx_lifecycle_lock);
    if (queued)
        /* A reset notification is the one allowed post-close delivery. */
        iwn_sae_tx_schedule_task(sc, true);
    iwn_sae_tx_lifecycle_leave(sc);
}

void ItlIwn::
iwn_sae_tx_purge(struct iwn_softc *sc)
{
    if (sc == NULL || sc->sc_sae_tx_lock == NULL)
        return;

    /* Caller has reset/reclaimed every descriptor before forgetting ownership. */
    IOSimpleLockLock(sc->sc_sae_tx_lock);
    explicit_bzero(sc->sc_sae_tx_eventq, sizeof(sc->sc_sae_tx_eventq));
    sc->sc_sae_tx_event_head = 0;
    sc->sc_sae_tx_event_tail = 0;
    sc->sc_sae_tx_event_count = 0;
    sc->sc_sae_tx_join_failure_generation = 0;
    sc->sc_sae_tx_active = false;
    sc->sc_sae_tx_doorbelled = false;
    sc->sc_sae_tx_active_ticket = 0;
    sc->sc_sae_tx_active_generation = 0;
    explicit_bzero(&sc->sc_sae_tx_active_event,
        sizeof(sc->sc_sae_tx_active_event));
    sc->sc_sae_tx_last_event_valid = false;
    explicit_bzero(&sc->sc_sae_tx_last_event,
        sizeof(sc->sc_sae_tx_last_event));
    IOSimpleLockUnlock(sc->sc_sae_tx_lock);
}

void ItlIwn::
iwn_sae_tx_task(void *arg)
{
    struct iwn_softc *sc = (struct iwn_softc *)arg;
    struct ieee80211com *ic;
    struct ItlSaeAuthTransportEventV1 event;
    bool have_event = false;
    bool is_reset = false;
    bool suppressed = false;
    bool more = false;
    bool deliver_normal = false;
    bool engine_consumed = false;

    if (sc == NULL || !iwn_sae_tx_lifecycle_enter(sc, true))
        return;
    ic = &sc->sc_ic;
    explicit_bzero(&event, sizeof(event));

    if (sc->sc_sae_tx_lock != NULL) {
        IOSimpleLockLock(sc->sc_sae_tx_lock);
        if (sc->sc_sae_tx_event_count != 0) {
            struct iwn_sae_tx_event_entry *entry =
                &sc->sc_sae_tx_eventq[sc->sc_sae_tx_event_head];
            event = entry->event;
            is_reset = entry->is_reset;
            explicit_bzero(entry, sizeof(*entry));
            sc->sc_sae_tx_event_head = (sc->sc_sae_tx_event_head + 1) %
                IWN_SAE_TX_EVENTQ_LEN;
            sc->sc_sae_tx_event_count--;
            suppressed = !is_reset &&
                iwn_sae_tx_ticket_cancelled_locked(sc, event.ticket);
            more = sc->sc_sae_tx_event_count != 0;
            have_event = true;
        }
        IOSimpleLockUnlock(sc->sc_sae_tx_lock);
    }
    deliver_normal = iwn_sae_tx_lifecycle_is_open(sc);

    if (have_event && !suppressed &&
        (is_reset || deliver_normal) &&
        itl_sae_auth_transport_event_is_well_formed(&event)) {
        /* A direct engine gets the native terminal value first and never
         * recreates an Agent/controller cryptographic relay. */
        if (iwn_sae_engine_callback_enter(sc)) {
            bool queued_direct = iwn_sae_engine_queue_terminal(sc, &event);
            engine_consumed = iwn_sae_tx_ticket_is_direct(event.ticket) ||
                queued_direct;
            iwn_sae_engine_callback_leave(sc);
        } else if (iwn_sae_tx_ticket_is_direct(event.ticket)) {
            /* A copied terminal producer raced close/unpublication.  This
             * can only be the direct laboratory owner; consume it rather
             * than leaking its live exchange into the old relay path. */
            engine_consumed = true;
        }
        if (!engine_consumed && ic->ic_event_handler != NULL) {
            /* Deferred task context only; never firmware completion/IRQ context. */
            (*ic->ic_event_handler)(ic,
                is_reset ? IEEE80211_EVT_SAE_AUTH_TRANSPORT_RESET :
                IEEE80211_EVT_SAE_AUTH_TRANSPORT, &event);
        }
    }
    explicit_bzero(&event, sizeof(event));
    iwn_sae_tx_finish_join_retirement(sc);
    /* Keep the lease until any requeue is admitted or rejected by close().
     * Otherwise detach could drain, free the task storage, and race the
     * post-callback task_add() below. */
    if (more)
        iwn_sae_tx_schedule_task(sc, false);
    else if (sc->sc_sae_engine_lock != NULL)
        iwn_sae_engine_wake_join_retirement(sc);
    /* Detach drains this lease before it can release ic/softc storage. */
    iwn_sae_tx_lifecycle_leave(sc);
}

void ItlIwn::
iwn_sae_tx_detach_begin(struct iwn_softc *sc)
{
    IOCommandGate *gate;
    bool task_ready = false;

    if (sc == NULL)
        return;
    iwn_sae_tx_lifecycle_close(sc, true);
    iwn_sae_tx_cancel_all(sc);
    if (sc->sc_sae_tx_lifecycle_lock != NULL) {
        IOLockLock(sc->sc_sae_tx_lifecycle_lock);
        task_ready = sc->sc_sae_tx_task_ready;
        sc->sc_sae_tx_task_ready = false;
        IOLockUnlock(sc->sc_sae_tx_lifecycle_lock);
    }
    if (task_ready && systq != NULL) {
        (void)task_del(systq, &sc->sae_tx_task);
        /* task_del() cannot retract a callback already copied by systq. */
        taskq_barrier(systq);
    }
    iwn_sae_tx_lifecycle_drain(sc);

    /* Close+drain makes a retained submit gate unreachable from now on. */
    gate = fSaeTxGate;
    fSaeTxGate = NULL;
    if (gate != NULL) {
        if (pci.workloop != NULL)
            pci.workloop->removeEventSource(gate);
        gate->release();
    }
}

namespace {

struct IwnSaeEngineCancellation {
    bool      active;
    u_int64_t request_generation;
    u_int64_t association_epoch;
    u_int64_t relay_generation;
    u_int64_t ticket;
};

/* Mark only public owner state under the interrupt-safe leaf.  The worker
 * remains the sole destroyer of sc_sae_engine, so cancellation can safely
 * race peer RX, native completion and a selected-BSS reset. */
static bool
iwn_sae_engine_mark_cancelled_locked(struct iwn_softc *sc,
    u_int64_t expected_generation, bool suppress_scan,
    struct IwnSaeEngineCancellation *cancel)
{
    struct iwn_sae_engine_owner *owner;

    if (cancel != NULL)
        explicit_bzero(cancel, sizeof(*cancel));
    if (sc == NULL || cancel == NULL || sc->sc_sae_engine_lock == NULL)
        return false;
    owner = &sc->sc_sae_engine_owner;
    /* A revoke can precede selected-BSS ownership; preserve that generation
     * for the worker even when no active engine record matches it yet. */
    iwn_sae_engine_queue_wcl_cancel_locked(sc, expected_generation);
    if (owner->active && (expected_generation == 0 ||
        owner->request_generation == expected_generation)) {
        cancel->active = true;
        cancel->request_generation = owner->request_generation;
        cancel->association_epoch = owner->association_epoch;
        cancel->relay_generation = owner->relay_generation;
        cancel->ticket = owner->in_flight_ticket;
        owner->cancelled = true;
        iwn_sae_engine_queue_wcl_cancel_locked(sc,
            owner->request_generation);
        iwn_sae_engine_fence_inflight_ticket_locked(sc, owner);
        if (suppress_scan)
            owner->suppress_scan = true;
        owner->start_pending = false;
        owner->submit_retry_pending = false;
        owner->terminal_valid = false;
        explicit_bzero(&owner->terminal, sizeof(owner->terminal));
        explicit_bzero(owner->peerq, sizeof(owner->peerq));
        owner->peer_head = 0;
        owner->peer_tail = 0;
        owner->peer_count = 0;
    }
    return cancel->active;
}

static bool
iwn_sae_engine_mark_cancelled(struct iwn_softc *sc,
    u_int64_t expected_generation, bool suppress_scan,
    struct IwnSaeEngineCancellation *cancel)
{
    bool marked;

    if (cancel != NULL)
        explicit_bzero(cancel, sizeof(*cancel));
    if (sc == NULL || cancel == NULL || sc->sc_sae_engine_lock == NULL)
        return false;
    IOSimpleLockLock(sc->sc_sae_engine_lock);
    marked = iwn_sae_engine_mark_cancelled_locked(sc, expected_generation,
        suppress_scan, cancel);
    IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    return marked;
}

enum IwnSaeAssocTxAdmission {
    IWN_SAE_ASSOC_TX_NOT_DIRECT = 0,
    IWN_SAE_ASSOC_TX_ADMITTED = 1,
    IWN_SAE_ASSOC_TX_REJECTED = -1,
};

/* The descriptor fence retains only this public completion identity between
 * the pre-trim Association Request check and the final firmware doorbell. */
struct IwnSaeAssocTxClaim {
    struct ItlSaePmkContinuationIdentityV1 identity;
    bool                                    active;
};

static bool
iwn_sae_engine_pmk_identity_equal(
    const struct ItlSaePmkContinuationIdentityV1 *left,
    const struct ItlSaePmkContinuationIdentityV1 *right)
{
    return left != NULL && right != NULL &&
        left->version == right->version && left->size == right->size &&
        left->request_generation == right->request_generation &&
        left->association_epoch == right->association_epoch &&
        left->relay_generation == right->relay_generation &&
        left->event_sequence == right->event_sequence &&
        IEEE80211_ADDR_EQ(left->bssid, right->bssid) &&
        IEEE80211_ADDR_EQ(left->sta, right->sta) &&
        itl_sae_pmk_continuation_bytes_all_zero(left->reserved,
            sizeof(left->reserved)) &&
        itl_sae_pmk_continuation_bytes_all_zero(right->reserved,
            sizeof(right->reserved));
}

/* Caller holds the engine leaf.  This identity has no PMK bytes: it binds the
 * driver owner to exactly one Confirm result before generic code may queue an
 * Association Request. */
static bool
iwn_sae_engine_owner_matches_pmk_identity_locked(
    const struct iwn_softc *sc, const struct iwn_sae_engine_owner *owner,
    const struct ItlSaePmkContinuationIdentityV1 *identity)
{
    return sc != NULL && owner != NULL && identity != NULL &&
        itl_sae_pmk_continuation_identity_is_well_formed(identity) &&
        owner->active && !owner->cancelled && !owner->suppress_scan &&
        !sc->sc_sae_engine_stopping && !sc->sc_sae_engine_detaching &&
        owner->request_generation == identity->request_generation &&
        owner->association_epoch == identity->association_epoch &&
        owner->relay_generation == identity->relay_generation &&
        owner->selected.request_generation == identity->request_generation &&
        owner->selected.association_epoch == identity->association_epoch &&
        owner->activated.request_generation == identity->request_generation &&
        owner->activated.association_epoch == identity->association_epoch &&
        owner->activated.relay_generation == identity->relay_generation &&
        IEEE80211_ADDR_EQ(owner->selected.bssid, identity->bssid) &&
        IEEE80211_ADDR_EQ(owner->selected.sta, identity->sta) &&
        IEEE80211_ADDR_EQ(owner->activated.bssid, identity->bssid) &&
        IEEE80211_ADDR_EQ(owner->activated.sta, identity->sta);
}

static bool
iwn_sae_engine_assoc_tx_frame_matches_identity(
    const struct ieee80211_frame *wh,
    const struct ItlSaePmkContinuationIdentityV1 *identity)
{
    return wh != NULL && identity != NULL &&
        itl_sae_pmk_continuation_identity_is_well_formed(identity) &&
        (wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) == IEEE80211_FC0_TYPE_MGT &&
        (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) ==
        IEEE80211_FC0_SUBTYPE_ASSOC_REQ &&
        IEEE80211_ADDR_EQ(wh->i_addr1, identity->bssid) &&
        IEEE80211_ADDR_EQ(wh->i_addr2, identity->sta) &&
        IEEE80211_ADDR_EQ(wh->i_addr3, identity->bssid);
}

/* A stale queued frame for a former BSS must not cancel a newer direct SAE
 * owner.  This looser match decides only whether an invalid Association
 * Request is about the current owner and is therefore allowed to retire it. */
static bool
iwn_sae_engine_assoc_tx_frame_matches_selected(
    const struct ieee80211_frame *wh,
    const struct iwn_sae_engine_owner *owner)
{
    return wh != NULL && owner != NULL && owner->active &&
        (wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) == IEEE80211_FC0_TYPE_MGT &&
        (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) ==
        IEEE80211_FC0_SUBTYPE_ASSOC_REQ &&
        IEEE80211_ADDR_EQ(wh->i_addr1, owner->selected.bssid) &&
        IEEE80211_ADDR_EQ(wh->i_addr2, owner->selected.sta) &&
        IEEE80211_ADDR_EQ(wh->i_addr3, owner->selected.bssid);
}

/* Pre-trim admission for a generic Association Request.  It leaves ordinary
 * traffic untouched when there is no direct SAE owner, rejects an old queued
 * frame without cancelling a newer owner, and snapshots only public state for
 * the final descriptor fence below. */
static enum IwnSaeAssocTxAdmission
iwn_sae_engine_assoc_tx_preflight(struct iwn_softc *sc,
    const struct ieee80211_node *ni, const struct ieee80211_frame *wh,
    struct IwnSaeAssocTxClaim *claim)
{
    struct iwn_sae_engine_owner *owner;
    struct IwnSaeEngineCancellation cancel;
    IOSimpleLock *bss_lock;
    IOInterruptState irq;
    enum IwnSaeAssocTxAdmission result = IWN_SAE_ASSOC_TX_NOT_DIRECT;
    bool schedule = false;

    if (claim != NULL)
        explicit_bzero(claim, sizeof(*claim));
    if (sc == NULL || ni == NULL || wh == NULL || claim == NULL ||
        sc->sc_sae_engine_lock == NULL)
        return IWN_SAE_ASSOC_TX_NOT_DIRECT;
    bss_lock = sc->sc_ic.ic_pae_selected_bss_lock;
    if (bss_lock == NULL)
        return IWN_SAE_ASSOC_TX_NOT_DIRECT;

    explicit_bzero(&cancel, sizeof(cancel));
    irq = IOSimpleLockLockDisableInterrupt(bss_lock);
    IOSimpleLockLock(sc->sc_sae_engine_lock);
    owner = &sc->sc_sae_engine_owner;
    if (owner->active) {
        result = IWN_SAE_ASSOC_TX_REJECTED;
        if (owner->completion_claimed && owner->assoc_tx_pending &&
            !owner->assoc_tx_accepted &&
            iwn_sae_engine_owner_matches_pmk_identity_locked(sc, owner,
            &owner->completion) &&
            iwn_sae_engine_assoc_tx_frame_matches_identity(wh,
            &owner->completion) &&
            ieee80211_sae_wcl_request_pmk_claim_assoc_current_locked(
            &sc->sc_ic, ni, &owner->completion)) {
            claim->identity = owner->completion;
            claim->active = true;
            result = IWN_SAE_ASSOC_TX_ADMITTED;
        } else if (iwn_sae_engine_assoc_tx_frame_matches_selected(wh,
            owner)) {
            /* This is an invalid request for the current owner, not a stale
             * descriptor from a former BSS.  Retire only its exact
             * generation; never use the broad generation-zero cancel. */
            schedule = iwn_sae_engine_mark_cancelled_locked(sc,
                owner->request_generation, false, &cancel);
        }
    }
    IOSimpleLockUnlock(sc->sc_sae_engine_lock);

    IOSimpleLockUnlockEnableInterrupt(bss_lock, irq);
    if (schedule)
        iwn_sae_engine_schedule_task(sc);
    explicit_bzero(&cancel, sizeof(cancel));
    return result;
}

/* Final Association Request linearization.  The generic mgtq can delay this
 * far beyond S_ASSOC, so no earlier PMK claim is sufficient: the exact same
 * public identity must still be live when scheduler state and WRPTR become
 * visible to firmware. */
static bool
iwn_sae_engine_assoc_tx_commit(struct iwn_softc *sc,
    struct iwn_tx_ring *ring, int descriptor_idx, uint8_t station_id,
    uint16_t length, const struct ieee80211_node *ni,
    const struct IwnSaeAssocTxClaim *claim)
{
    struct iwn_sae_engine_owner *owner;
    struct IwnSaeEngineCancellation cancel;
    IOSimpleLock *bss_lock;
    IOInterruptState irq;
    bool committed = false;
    bool schedule = false;

    if (sc == NULL || ring == NULL || ni == NULL || claim == NULL ||
        !claim->active || sc->sc_sae_engine_lock == NULL)
        return false;
    bss_lock = sc->sc_ic.ic_pae_selected_bss_lock;
    if (bss_lock == NULL)
        return false;

    explicit_bzero(&cancel, sizeof(cancel));
    irq = IOSimpleLockLockDisableInterrupt(bss_lock);
    IOSimpleLockLock(sc->sc_sae_engine_lock);
    owner = &sc->sc_sae_engine_owner;
    if (iwn_sae_engine_pmk_identity_equal(&owner->completion,
        &claim->identity)) {
        if (owner->completion_claimed && owner->assoc_tx_pending &&
            !owner->assoc_tx_accepted &&
            iwn_sae_engine_owner_matches_pmk_identity_locked(sc, owner,
            &claim->identity) &&
            iwn_sae_engine_owner_matches_node_locked(sc, &sc->sc_ic, ni) &&
            ieee80211_sae_wcl_request_pmk_claim_assoc_current_locked(
            &sc->sc_ic, ni, &claim->identity) &&
            ring->cur == descriptor_idx && sc->ops.update_sched != NULL) {
            const int next_cur = (descriptor_idx + 1) % IWN_TX_RING_COUNT;

            sc->ops.update_sched(sc, ring->qid, descriptor_idx, station_id,
                length);
            ring->cur = next_cur;
            IWN_WRITE(sc, IWN_HBUS_TARG_WRPTR,
                ring->qid << 8 | ring->cur);
            owner->assoc_tx_pending = false;
            owner->assoc_tx_accepted = true;
            committed = true;
        } else {
            /* The matching owner lost one of its current-BSS/PMK fences
             * before the doorbell.  Let the sole worker scrub and scan. */
            schedule = iwn_sae_engine_mark_cancelled_locked(sc,
                owner->request_generation, false, &cancel);
        }
    }
    IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    IOSimpleLockUnlockEnableInterrupt(bss_lock, irq);
    if (schedule)
        iwn_sae_engine_schedule_task(sc);
    explicit_bzero(&cancel, sizeof(cancel));
    return committed;
}

/* All calls below are after the owner leaf is released: generic selected-BSS
 * state, the private credential leaf, the TX gate and taskq must never nest
 * below the engine leaf. */
static void
iwn_sae_engine_cancel_owned(struct iwn_softc *sc,
    const struct IwnSaeEngineCancellation *cancel)
{
    ItlIwn *that;

    if (sc == NULL || cancel == NULL || !cancel->active)
        return;
    that = container_of(sc, ItlIwn, com);
    if (cancel->ticket != 0)
        that->cancelSaeAuthFrame(cancel->ticket);
    if (cancel->association_epoch != 0 && cancel->relay_generation != 0)
        ieee80211_sae_peer_rx_revoke(&sc->sc_ic,
            cancel->association_epoch, cancel->relay_generation);
    if (cancel->request_generation != 0)
        that->cancelSaeWclCredential(cancel->request_generation);
}

/* The existing SAE TX task invokes this in deferred context before it can
 * offer a terminal event to the controller mailbox.  A matching live engine
 * consumes the value privately; unrelated legacy SAE transport remains
 * unchanged. */
static bool
iwn_sae_engine_queue_terminal(struct iwn_softc *sc,
    const struct ItlSaeAuthTransportEventV1 *event)
{
    struct iwn_sae_engine_owner *owner;
    bool consumed = false;

    if (sc == NULL || event == NULL || sc->sc_sae_engine_lock == NULL ||
        !itl_sae_auth_transport_event_is_well_formed(event))
        return false;
    IOSimpleLockLock(sc->sc_sae_engine_lock);
    owner = &sc->sc_sae_engine_owner;
    if (iwn_sae_engine_owner_matches_terminal_locked(sc, event)) {
        consumed = true;
        if (owner->cancelled || sc->sc_sae_engine_stopping ||
            sc->sc_sae_engine_detaching) {
            /* Keep a cancelled owner's tombstone authoritative until the
             * generic S_AUTH boundary has moved. */
        } else if (owner->terminal_valid) {
            /* One physical descriptor is live at a time.  A second terminal
             * value is an ownership violation, not a retry opportunity. */
            owner->cancelled = true;
            owner->submit_retry_pending = false;
            iwn_sae_engine_queue_wcl_cancel_locked(sc,
                owner->request_generation);
            iwn_sae_engine_fence_inflight_ticket_locked(sc, owner);
        } else {
            owner->terminal = *event;
            owner->terminal_valid = true;
        }
    }
    IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    if (consumed)
        iwn_sae_engine_schedule_task(sc);
    return consumed;
}

/* Returns 1 when a matching owner copied the peer value, -1 when a live
 * owner consumes a late/invalid/overflow value, and 0 only when no direct
 * owner exists so the historical controller route may still decide. */
static int
iwn_sae_engine_queue_peer(struct iwn_softc *sc,
    const struct ItlSaeAuthPeerEventV1 *event)
{
    struct iwn_sae_engine_owner *owner;
    IOSimpleLock *bss_lock;
    IOInterruptState irq;
    bool matching_peer;
    bool current_owner;
    int result = 0;

    if (sc == NULL || event == NULL || sc->sc_sae_engine_lock == NULL ||
        !itl_sae_auth_peer_event_is_well_formed(event))
        return -1;
    bss_lock = sc->sc_ic.ic_pae_selected_bss_lock;
    if (bss_lock == NULL)
        return -1;
    irq = IOSimpleLockLockDisableInterrupt(bss_lock);
    IOSimpleLockLock(sc->sc_sae_engine_lock);
    owner = &sc->sc_sae_engine_owner;
    if (owner->active) {
        result = -1;
        matching_peer = iwn_sae_engine_owner_matches_peer_locked(sc, event);
        current_owner = iwn_sae_engine_peer_owner_current_locked(sc, owner);
        if (matching_peer && current_owner && !owner->cancelled &&
            owner->peer_count < IWN_SAE_ENGINE_PEERQ_LEN) {
            owner->peerq[owner->peer_tail] = *event;
            owner->peer_tail = (owner->peer_tail + 1) %
                IWN_SAE_ENGINE_PEERQ_LEN;
            owner->peer_count++;
            result = 1;
        } else {
            /* A direct owner never hands a suspicious peer frame back to
             * the controller route.  The generic admission fence should
             * already exclude an identity mismatch; treating one as a
             * terminal local failure keeps that invariant fail-closed. */
            if (matching_peer && current_owner)
                owner->peer_overflow = true;
            owner->cancelled = true;
            owner->submit_retry_pending = false;
            iwn_sae_engine_queue_wcl_cancel_locked(sc,
                owner->request_generation);
            iwn_sae_engine_fence_inflight_ticket_locked(sc, owner);
        }
    }
    IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    IOSimpleLockUnlockEnableInterrupt(bss_lock, irq);
    if (result != 0)
        iwn_sae_engine_schedule_task(sc);
    return result;
}

/* Writers share generic's selected-BSS leaf so a request cannot observe a
 * partial four-hook owner.  The independent callback lease protects a hook
 * copied immediately before this writer removes it. */
static bool
iwn_sae_engine_publish_hooks(struct iwn_softc *sc, bool enabled,
    bool retain_auth_tombstone, u_int32_t expected_generation)
{
    struct ieee80211com *ic;
    IOSimpleLock *lock;
    IOInterruptState irq;
    bool retain_owner = false;

    if (sc == NULL)
        return false;
    if (enabled && (!iwn_sae_engine_runtime_enabled(sc) ||
        expected_generation == 0 ||
        __atomic_load_n(&sc->sc_sae_engine_lifecycle_generation,
            __ATOMIC_ACQUIRE) != expected_generation ||
        (__atomic_load_n(&sc->sc_sae_engine_callback_state,
            __ATOMIC_ACQUIRE) & IWN_SAE_ENGINE_CALLBACK_CLOSED) != 0))
        return false;
    ic = &sc->sc_ic;
    lock = ic->ic_pae_selected_bss_lock;
    if (lock != NULL)
        irq = IOSimpleLockLockDisableInterrupt(lock);
    /* The second token check is under the same generic hook writer leaf as
     * publication.  A stop which won in between leaves its own unpublish
     * authoritative; this older reopen never resurrects a hook. */
    if (enabled && (!iwn_sae_engine_runtime_enabled(sc) ||
        __atomic_load_n(&sc->sc_sae_engine_lifecycle_generation,
            __ATOMIC_ACQUIRE) != expected_generation ||
        (__atomic_load_n(&sc->sc_sae_engine_callback_state,
            __ATOMIC_ACQUIRE) & IWN_SAE_ENGINE_CALLBACK_CLOSED) != 0)) {
        if (lock != NULL)
            IOSimpleLockUnlockEnableInterrupt(lock, irq);
        return false;
    }
    if (enabled) {
        ic->ic_sae_auth_hold = ItlIwn::iwn_sae_auth_hold;
        ic->ic_sae_auth_owned = ItlIwn::iwn_sae_auth_owned;
        ic->ic_sae_engine_peer_event = ItlIwn::iwn_sae_engine_peer_event;
        ic->ic_sae_wcl_request_revoke = ItlIwn::iwn_sae_wcl_request_revoke;
        ic->ic_sae_roam_port_valid = ItlIwn::iwn_sae_roam_port_valid;
        ic->ic_sae_wnm_roam_start = ItlIwn::iwn_sae_wnm_roam_start;
        ic->ic_sae_wcl_roam_start = ItlIwn::iwn_sae_wcl_roam_start;
        ic->ic_sae_bss_loss_recover =
            ItlIwn::iwn_sae_bss_loss_recover;
    } else {
        ic->ic_sae_auth_hold = NULL;
        /* A closing S_AUTH owner must remain visible even after the other
         * hooks are withdrawn.  Generic RX checks this predicate separately
         * and therefore cannot turn a fresh late Open response into ASSOC.
         * Final detach/ifdetach passes false once RX can no longer observe
         * this softc. */
        if (retain_auth_tombstone && sc->sc_sae_engine_lock != NULL) {
            /* The hook writer lock precedes the engine leaf everywhere it
             * nests.  Do not publish a closed predicate for an ordinary
             * non-SAE AUTH attempt: retain only an actual S_AUTH tombstone. */
            IOSimpleLockLock(sc->sc_sae_engine_lock);
            retain_owner = sc->sc_sae_engine_owner.active;
            IOSimpleLockUnlock(sc->sc_sae_engine_lock);
        }
        ic->ic_sae_auth_owned = retain_owner ?
            ItlIwn::iwn_sae_auth_owned : NULL;
        ic->ic_sae_engine_peer_event = NULL;
        ic->ic_sae_wcl_request_revoke = NULL;
        ic->ic_sae_roam_port_valid = NULL;
        ic->ic_sae_wnm_roam_start = NULL;
        ic->ic_sae_wcl_roam_start = NULL;
        ic->ic_sae_bss_loss_recover = NULL;
    }
    if (lock != NULL)
        IOSimpleLockUnlockEnableInterrupt(lock, irq);
    return enabled;
}

} // namespace

int ItlIwn::
iwn_sae_auth_owned(struct ieee80211com *ic,
    const struct ieee80211_node *ni)
{
    struct iwn_softc *sc;
    int owned = 0;

    if (ic == NULL || ni == NULL)
        return 0;
    sc = (struct iwn_softc *)ic->ic_softc;
    /* A selected-BSS reader may have copied this hook immediately before
     * stop/detach closes it.  Returning ownership on that closed edge is a
     * fail-closed tombstone: a late Open response must not revive S_AUTH. */
    if (!iwn_sae_engine_callback_enter(sc))
        return 1;
    if (iwn_sae_engine_runtime_enabled(sc) &&
        sc->sc_sae_engine_lock != NULL) {
        IOSimpleLockLock(sc->sc_sae_engine_lock);
        owned = iwn_sae_engine_owner_matches_node_locked(sc, ic, ni) ? 1 : 0;
        IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    }
    iwn_sae_engine_callback_leave(sc);
    return owned;
}

int ItlIwn::
iwn_sae_engine_peer_event(struct ieee80211com *ic,
    const struct ItlSaeAuthPeerEventV1 *event)
{
    struct iwn_softc *sc;
    int result = 0;

    if (ic == NULL || event == NULL)
        return 0;
    sc = (struct iwn_softc *)ic->ic_softc;
    /* Do not turn a copied hook which lost the close race into a controller
     * fallback.  -1 is the generic contract's consumed/rejected result. */
    if (!iwn_sae_engine_callback_enter(sc))
        return -1;
    if (iwn_sae_engine_runtime_enabled(sc))
        result = iwn_sae_engine_queue_peer(sc, event);
    iwn_sae_engine_callback_leave(sc);
    return result;
}

void ItlIwn::
iwn_sae_wcl_request_revoke(struct ieee80211com *ic,
    u_int64_t request_generation)
{
    struct iwn_softc *sc;
    struct IwnSaeEngineCancellation cancel;

    if (ic == NULL || request_generation == 0)
        return;
    sc = (struct iwn_softc *)ic->ic_softc;
    if (!iwn_sae_engine_callback_enter(sc))
        return;
    explicit_bzero(&cancel, sizeof(cancel));
    (void)iwn_sae_engine_mark_cancelled(sc, request_generation, false,
        &cancel);
    /* Generic has already revoked this exact association epoch.  Drop the
     * continuity owner synchronously so its immediately following recovery
     * S_SCAN is admitted; private credential scrubbing remains deferred. */
    iwn_sae_join_scan_block_clear_generation(sc, request_generation);
    /* This callback is admitted from generic/RX-adjacent paths.  It has
     * recorded all public cancellation identity under an interrupt-safe leaf
     * and must not take the sleeping TX lifecycle lock or WCL leaf here. */
    iwn_sae_engine_schedule_task(sc);
    explicit_bzero(&cancel, sizeof(cancel));
    iwn_sae_engine_callback_leave(sc);
}

void ItlIwn::
iwn_sae_roam_port_valid(struct ieee80211com *ic,
    const struct ieee80211_node *ni)
{
    struct iwn_softc *sc;
    u_int64_t completed_generation = 0;
    bool exact_sae_ess;
    bool identity_matches;

    if (ic == NULL || ni == NULL)
        return;
    sc = (struct iwn_softc *)ic->ic_softc;
    if (!iwn_sae_engine_callback_enter(sc))
        return;
    if (!iwn_sae_engine_runtime_enabled(sc) ||
        sc->sc_sae_wcl_credential_lock == NULL)
        goto out;

    exact_sae_ess = ic->ic_opmode == IEEE80211_M_STA &&
        ic->ic_state == IEEE80211_S_RUN && ic->ic_bss == ni &&
        ni->ni_port_valid != 0 &&
        (ic->ic_flags & (IEEE80211_F_RSNON | IEEE80211_F_MFPR)) ==
            (IEEE80211_F_RSNON | IEEE80211_F_MFPR) &&
        (ic->ic_flags & IEEE80211_F_PSK) == 0 &&
        ic->ic_pae_mfp_requested != 0 &&
        ic->ic_rsnprotos == IEEE80211_PROTO_RSN &&
        ic->ic_rsnakms == IEEE80211_AKM_SAE &&
        ic->ic_rsnciphers == IEEE80211_CIPHER_CCMP &&
        ic->ic_rsngroupcipher == IEEE80211_CIPHER_CCMP &&
        ic->ic_rsngroupmgmtcipher == IEEE80211_CIPHER_BIP;

    IOSimpleLockLock(sc->sc_sae_wcl_credential_lock);
    if ((sc->sc_sae_wcl_credential_pending ||
        sc->sc_sae_wcl_credential_active) &&
        itl_sae_wcl_credential_is_well_formed(
            &sc->sc_sae_wcl_credential))
        completed_generation =
            sc->sc_sae_wcl_credential.request_generation;
    identity_matches =
        itl_sae_wcl_credential_is_well_formed(
            &sc->sc_sae_wcl_credential) &&
        sc->sc_sae_wcl_credential.ssid_len == ni->ni_esslen &&
        IEEE80211_ADDR_EQ(sc->sc_sae_wcl_credential.bssid,
            ni->ni_bssid) &&
        memcmp(sc->sc_sae_wcl_credential.ssid, ni->ni_essid,
            ni->ni_esslen) == 0;
    if (sc->sc_sae_wcl_credential_pending && exact_sae_ess &&
        identity_matches) {
        sc->sc_sae_wcl_credential_pending = false;
        sc->sc_sae_wcl_credential_active = true;
        XYLog("iwn_sae_roam ACTIVE_ESS_CREDENTIAL\n");
    } else if (sc->sc_sae_wcl_credential_pending ||
        (sc->sc_sae_wcl_credential_active &&
         (!exact_sae_ess || !identity_matches))) {
        iwn_sae_wcl_credential_clear_locked(sc);
    }
    IOSimpleLockUnlock(sc->sc_sae_wcl_credential_lock);
    /* Port-valid is the successful join terminal.  The 4-way/PMF exchange
     * has published its keys, so ordinary background scans may resume. */
    iwn_sae_join_scan_block_clear_generation(sc, completed_generation);
out:
    iwn_sae_engine_callback_leave(sc);
}

bool ItlIwn::
iwn_sae_bss_loss_arm(struct ieee80211com *ic,
    const struct ieee80211_node *source)
{
    struct iwn_softc *sc;
    bool armed = false;

    if (ic == NULL || source == NULL || source != ic->ic_bss ||
        ic->ic_opmode != IEEE80211_M_STA ||
        ic->ic_state != IEEE80211_S_RUN || !source->ni_port_valid ||
        (ic->ic_flags & IEEE80211_F_AUTO_JOIN) == 0)
        return false;
    sc = (struct iwn_softc *)ic->ic_softc;
    if (!iwn_sae_engine_callback_enter(sc))
        return false;
    if (!iwn_sae_engine_runtime_enabled(sc) ||
        sc->sc_sae_wcl_credential_lock == NULL)
        goto out;

    IOSimpleLockLock(sc->sc_sae_wcl_credential_lock);
    if (sc->sc_sae_wcl_credential_active &&
        !sc->sc_sae_wcl_credential_staged &&
        !sc->sc_sae_wcl_credential_pending &&
        itl_sae_wcl_credential_is_well_formed(
            &sc->sc_sae_wcl_credential) &&
        sc->sc_sae_wcl_credential.ssid_len == source->ni_esslen &&
        IEEE80211_ADDR_EQ(sc->sc_sae_wcl_credential.bssid,
            source->ni_bssid) &&
        memcmp(sc->sc_sae_wcl_credential.ssid, source->ni_essid,
            source->ni_esslen) == 0) {
        sc->sc_sae_bss_loss_recovery_generation =
            sc->sc_sae_wcl_credential.request_generation;
        sc->sc_sae_bss_loss_recovery_armed = true;
        armed = true;
    }
    IOSimpleLockUnlock(sc->sc_sae_wcl_credential_lock);
    if (armed)
        XYLog("iwn_sae_reconnect BSS_LOSS_ARMED\n");
out:
    iwn_sae_engine_callback_leave(sc);
    return armed;
}

bool ItlIwn::
iwn_sae_driver_reset_recovery_pending(struct iwn_softc *sc, bool consume)
{
    bool pending = false;

    if (sc == NULL || sc->sc_sae_wcl_credential_lock == NULL)
        return false;
    IOSimpleLockLock(sc->sc_sae_wcl_credential_lock);
    pending = sc->sc_sae_driver_reset_recovery_pending &&
        sc->sc_sae_bss_loss_recovery_armed &&
        sc->sc_sae_bss_loss_recovery_generation != 0 &&
        sc->sc_sae_wcl_credential_active &&
        !sc->sc_sae_wcl_credential_staged &&
        !sc->sc_sae_wcl_credential_pending &&
        sc->sc_sae_wcl_credential.request_generation ==
            sc->sc_sae_bss_loss_recovery_generation &&
        itl_sae_wcl_credential_is_well_formed(
            &sc->sc_sae_wcl_credential);
    if (consume && pending)
        sc->sc_sae_driver_reset_recovery_pending = false;
    IOSimpleLockUnlock(sc->sc_sae_wcl_credential_lock);
    return pending;
}

void ItlIwn::
iwn_sae_driver_reset_recovery_prepare(struct iwn_softc *sc)
{
    struct ieee80211com *ic;
    bool prepared = false;

    if (sc == NULL)
        return;
    ic = &sc->sc_ic;
    if (ic->ic_state == IEEE80211_S_RUN && ic->ic_bss != NULL)
        (void)iwn_sae_bss_loss_arm(ic, ic->ic_bss);
    if (sc->sc_sae_wcl_credential_lock == NULL)
        return;

    IOSimpleLockLock(sc->sc_sae_wcl_credential_lock);
    if (sc->sc_sae_bss_loss_recovery_armed &&
        sc->sc_sae_bss_loss_recovery_generation != 0 &&
        sc->sc_sae_wcl_credential_active &&
        !sc->sc_sae_wcl_credential_staged &&
        !sc->sc_sae_wcl_credential_pending &&
        sc->sc_sae_wcl_credential.request_generation ==
            sc->sc_sae_bss_loss_recovery_generation &&
        itl_sae_wcl_credential_is_well_formed(
            &sc->sc_sae_wcl_credential)) {
        sc->sc_sae_driver_reset_recovery_pending = true;
        prepared = true;
    }
    IOSimpleLockUnlock(sc->sc_sae_wcl_credential_lock);
    if (prepared)
        XYLog("iwn_sae_reconnect DRIVER_RESET_RECOVERY_PREPARED\n");
}

int ItlIwn::
iwn_sae_bss_loss_recover(struct ieee80211com *ic)
{
    struct iwn_softc *sc;
    ItlIwn *that;
    struct ItlSaeWclCredentialV1 credential;
    struct ieee80211_node *candidate;
    struct ieee80211_node *selected = NULL;
    u_int64_t generation = 0;
    bool active_copied = false;
    int started = 0;

    explicit_bzero(&credential, sizeof(credential));
    if (ic == NULL || ic->ic_opmode != IEEE80211_M_STA ||
        ic->ic_state != IEEE80211_S_SCAN ||
        (ic->ic_flags & IEEE80211_F_AUTO_JOIN) == 0 ||
        ic->ic_des_esslen != 0)
        goto out;
    sc = (struct iwn_softc *)ic->ic_softc;
    if (!iwn_sae_engine_callback_enter(sc))
        goto out;
    that = container_of(sc, ItlIwn, com);
    if (!iwn_sae_engine_runtime_enabled(sc) ||
        sc->sc_sae_wcl_credential_lock == NULL)
        goto leave;

    IOSimpleLockLock(sc->sc_sae_wcl_credential_lock);
    if (sc->sc_sae_bss_loss_recovery_armed &&
        sc->sc_sae_bss_loss_recovery_generation != 0 &&
        sc->sc_sae_wcl_credential_active &&
        !sc->sc_sae_wcl_credential_staged &&
        !sc->sc_sae_wcl_credential_pending &&
        sc->sc_sae_wcl_credential.request_generation ==
            sc->sc_sae_bss_loss_recovery_generation &&
        itl_sae_wcl_credential_is_well_formed(
            &sc->sc_sae_wcl_credential)) {
        credential = sc->sc_sae_wcl_credential;
        active_copied = true;
    }
    IOSimpleLockUnlock(sc->sc_sae_wcl_credential_lock);
    if (!active_copied)
        goto leave;

    RB_FOREACH(candidate, ieee80211_tree, &ic->ic_tree) {
        if (!iwn_sae_bss_loss_candidate_eligible(candidate, &credential))
            continue;
        if (selected == NULL || candidate->ni_rssi > selected->ni_rssi)
            selected = candidate;
    }
    if (selected == NULL)
        goto leave;

    generation = ieee80211_sae_wcl_request_begin(ic,
        selected->ni_bssid, credential.ssid, credential.ssid_len);
    if (generation == 0)
        goto leave;
    credential.request_generation = generation;
    IEEE80211_ADDR_COPY(credential.bssid, selected->ni_bssid);
    if (!itl_sae_wcl_credential_is_well_formed(&credential) ||
        that->stageSaeWclCredential(&credential) != kIOReturnSuccess ||
        !ieee80211_sae_wcl_request_admit_bss_loss_candidate(ic,
            generation, selected->ni_bssid, credential.ssid,
            credential.ssid_len))
        goto leave;

    candidate = ieee80211_find_node(ic, selected->ni_bssid);
    if (candidate == NULL ||
        !iwn_sae_bss_loss_candidate_eligible(candidate, &credential) ||
        candidate->ni_esslen != ic->ic_des_esslen ||
        memcmp(candidate->ni_essid, ic->ic_des_essid,
            ic->ic_des_esslen) != 0 ||
        ieee80211_match_bss(ic, candidate, 0) != 0)
        goto leave;

    /* ieee80211_end_scan() is still inside the exact STOP_SCAN terminal.
     * Give the synchronous AUTH hook a value-only proof that this generation
     * may inherit that completed foreground radio owner. */
    if (!iwn_sae_bss_loss_join_handoff_arm(sc, generation))
        goto leave;
    ieee80211_node_join_bss(ic, candidate);
    if (!ieee80211_sae_wcl_request_bound_current(ic, ic->ic_bss) ||
        !iwn_sae_bss_loss_join_handoff_completed(sc, generation))
        goto leave;
    XYLog("iwn_sae_reconnect DRIVER_RESIDENT_BSS_LOSS_STARTED\n");
    started = 1;
leave:
    if (!started && generation != 0) {
        (void)ieee80211_sae_wcl_request_clear_if_generation(ic,
            generation);
        that->cancelSaeWclCredential(generation);
    }
    explicit_bzero(&credential, sizeof(credential));
    iwn_sae_engine_callback_leave(sc);
out:
    explicit_bzero(&credential, sizeof(credential));
    return started;
}

int ItlIwn::
iwn_sae_targeted_roam_start(struct ieee80211com *ic,
    const struct ieee80211_node *source,
    const u_int8_t target_bssid[IEEE80211_ADDR_LEN], bool consume_wnm)
{
    struct iwn_softc *sc;
    ItlIwn *that;
    struct ItlSaeWclCredentialV1 credential;
    struct ieee80211_node *candidate;
    u_int8_t source_ssid[IEEE80211_NWID_LEN];
    u_int8_t source_ssid_len = 0;
    u_int64_t generation = 0;
    u_int64_t source_generation = 0;
    bool active_copied = false;
    int started = 0;

    explicit_bzero(&credential, sizeof(credential));
    explicit_bzero(source_ssid, sizeof(source_ssid));
    if (ic == NULL || source == NULL || target_bssid == NULL ||
        source != ic->ic_bss ||
        ic->ic_opmode != IEEE80211_M_STA ||
        ic->ic_state != IEEE80211_S_RUN || !source->ni_port_valid ||
        source->ni_esslen == 0 ||
        source->ni_esslen > sizeof(source_ssid))
        goto out;
    sc = (struct iwn_softc *)ic->ic_softc;
    if (!iwn_sae_engine_callback_enter(sc))
        goto out;
    that = container_of(sc, ItlIwn, com);

    source_ssid_len = source->ni_esslen;
    memcpy(source_ssid, source->ni_essid, source_ssid_len);
    if (!iwn_sae_engine_runtime_enabled(sc) ||
        sc->sc_sae_wcl_credential_lock == NULL ||
        (ic->ic_flags & (IEEE80211_F_RSNON | IEEE80211_F_MFPR)) !=
            (IEEE80211_F_RSNON | IEEE80211_F_MFPR) ||
        (ic->ic_flags & IEEE80211_F_PSK) != 0 ||
        ic->ic_rsnakms != IEEE80211_AKM_SAE ||
        IEEE80211_ADDR_EQ(target_bssid, source->ni_bssid))
        goto leave;

    candidate = ieee80211_find_node(ic, target_bssid);
    if (candidate == NULL || candidate == source ||
        candidate->ni_fails != 0 ||
        candidate->ni_chan == IEEE80211_CHAN_ANYC ||
        candidate->ni_esslen != source_ssid_len ||
        memcmp(candidate->ni_essid, source_ssid,
            source_ssid_len) != 0)
        goto leave;

    IOSimpleLockLock(sc->sc_sae_wcl_credential_lock);
    if (sc->sc_sae_wcl_credential_active &&
        !sc->sc_sae_wcl_credential_staged &&
        !sc->sc_sae_wcl_credential_pending &&
        itl_sae_wcl_credential_is_well_formed(
            &sc->sc_sae_wcl_credential) &&
        sc->sc_sae_wcl_credential.ssid_len == source_ssid_len &&
        IEEE80211_ADDR_EQ(sc->sc_sae_wcl_credential.bssid,
            source->ni_bssid) &&
        memcmp(sc->sc_sae_wcl_credential.ssid, source_ssid,
            source_ssid_len) == 0) {
        credential = sc->sc_sae_wcl_credential;
        active_copied = true;
    }
    IOSimpleLockUnlock(sc->sc_sae_wcl_credential_lock);
    if (!active_copied)
        goto leave;
    source_generation = credential.request_generation;

    /*
     * The reference submits its lower reassociation command while the source
     * remains associated and restores the roam parameters on synchronous
     * failure.  Prepare the matching public target without queuing RUN->SCAN;
     * private staging below is our lower acceptance point, and only its
     * success may enter node_join_bss()'s controlled RUN->AUTH replacement.
     */
    generation = ieee80211_sae_wcl_request_retarget_run(ic, source,
        source_generation,
        target_bssid, source_ssid, source_ssid_len, consume_wnm ? 1 : 0);
    if (generation == 0)
        goto leave;
    credential.request_generation = generation;
    IEEE80211_ADDR_COPY(credential.bssid, target_bssid);
    if (!itl_sae_wcl_credential_is_well_formed(&credential) ||
        candidate->ni_esslen != ic->ic_des_esslen ||
        memcmp(candidate->ni_essid, ic->ic_des_essid,
            ic->ic_des_esslen) != 0 ||
        ieee80211_match_bss(ic, candidate, 0) != 0 ||
        that->stageSaeWclCredential(&credential) != kIOReturnSuccess) {
        if (ieee80211_sae_wcl_request_rollback_run_retarget(ic,
            generation, source_generation, source))
            generation = 0;
        goto leave;
    }

    XYLog("iwn_sae_roam LOWER_RETARGET_ACCEPTED generation=%llu owner=%s\n",
        generation, consume_wnm ? "BTM" : "WCL");
    ieee80211_node_join_bss(ic, candidate);
    if (!ieee80211_sae_wcl_request_bound_current(ic, ic->ic_bss))
        goto leave;
    if (consume_wnm)
        ieee80211_wnm_bss_transition_consume(ic, source_ssid,
            source_ssid_len, target_bssid);
    XYLog("iwn_sae_roam DRIVER_RESIDENT_%s_STARTED\n",
        consume_wnm ? "BTM" : "WCL");
    started = 1;
leave:
    if (!started && generation != 0) {
        (void)ieee80211_sae_wcl_request_clear_if_generation(ic,
            generation);
        that->cancelSaeWclCredential(generation);
    }
    explicit_bzero(&credential, sizeof(credential));
    iwn_sae_engine_callback_leave(sc);
out:
    explicit_bzero(&credential, sizeof(credential));
    explicit_bzero(source_ssid, sizeof(source_ssid));
    return started;
}

int ItlIwn::
iwn_sae_wnm_roam_start(struct ieee80211com *ic,
    const struct ieee80211_node *source)
{
    u_int8_t target_bssid[IEEE80211_ADDR_LEN];
    u_int8_t source_ssid[IEEE80211_NWID_LEN];
    u_int8_t source_ssid_len;
    int started = 0;

    explicit_bzero(target_bssid, sizeof(target_bssid));
    explicit_bzero(source_ssid, sizeof(source_ssid));
    if (ic == NULL || source == NULL || source != ic->ic_bss ||
        source->ni_esslen == 0 ||
        source->ni_esslen > sizeof(source_ssid))
        goto out;
    source_ssid_len = source->ni_esslen;
    memcpy(source_ssid, source->ni_essid, source_ssid_len);
    if (ieee80211_wnm_bss_transition_copy_retarget(ic, source_ssid,
            source_ssid_len, target_bssid) == 0)
        goto out;
    started = iwn_sae_targeted_roam_start(ic, source, target_bssid, true);
out:
    explicit_bzero(source_ssid, sizeof(source_ssid));
    explicit_bzero(target_bssid, sizeof(target_bssid));
    return started;
}

int ItlIwn::
iwn_sae_wcl_roam_start(struct ieee80211com *ic,
    const struct ieee80211_node *source,
    const u_int8_t target_bssid[IEEE80211_ADDR_LEN])
{
    return iwn_sae_targeted_roam_start(ic, source, target_bssid, false);
}

int ItlIwn::
iwn_sae_auth_hold(struct ieee80211com *ic, struct ieee80211_node *ni,
    enum ieee80211_state /*oldstate*/, int /*mgt*/)
{
    struct iwn_softc *sc;
    struct ieee80211_sae_wcl_bound_request bound;
    struct ItlSaeSelectedJoinEventV1 selected;
    struct ItlSaeAuthActivatedEventV1 activated;
    struct IwnSaeEngineCancellation cancel;
    struct ieee80211_join_failure join_request;
    struct iwn_sae_engine_owner *owner;
    u_int64_t relay_generation = 0;
    u_int64_t join_attempt_generation = 0;
    int held = 0;

    explicit_bzero(&bound, sizeof(bound));
    explicit_bzero(&selected, sizeof(selected));
    explicit_bzero(&activated, sizeof(activated));
    explicit_bzero(&cancel, sizeof(cancel));
    explicit_bzero(&join_request, sizeof(join_request));
    if (ic == NULL || ni == NULL)
        goto out;
    sc = (struct iwn_softc *)ic->ic_softc;
    if (!iwn_sae_engine_callback_enter(sc)) {
        /* generic may have copied auth_hold immediately before a stop/detach
         * close.  Preserve S_AUTH until the already-scheduled cancellation
         * edge runs; returning zero here would fall through to Open-System. */
        if (sc != NULL && (__atomic_load_n(
            &sc->sc_sae_engine_callback_state, __ATOMIC_ACQUIRE) &
            IWN_SAE_ENGINE_CALLBACK_CLOSED) != 0)
            held = 1;
        goto out;
    }
    if (!iwn_sae_engine_runtime_enabled(sc) ||
        sc->sc_sae_engine_lock == NULL ||
        !ieee80211_sae_wcl_request_copyout_bound_current(ic, 0, &bound) ||
        !IEEE80211_ADDR_EQ(ni->ni_bssid, bound.bssid) ||
        !IEEE80211_ADDR_EQ(ic->ic_myaddr, bound.sta))
        goto leave;

    /* Cached-candidate joins do not necessarily issue a physical scan.
     * Promote their mailbox reservation here; a physical direct scan has
     * already installed the same exact generation and this is idempotent. */
    if (!iwn_sae_join_scan_block_promote(sc, bound.generation)) {
        held = 1;
        goto leave;
    }

    selected.version = kItlSaeAuthTransportV1Version;
    selected.size = sizeof(selected);
    selected.request_generation = bound.generation;
    selected.association_epoch = bound.association_epoch;
    selected.sae_group = IEEE80211_SAE_ENGINE_GROUP19;
    selected.sae_method =
        (bound.sae_scan_flags & IEEE80211_SAE_SCAN_H2E_ONLY_SELECTOR) != 0 ?
        IEEE80211_SAE_ENGINE_H2E_METHOD :
        IEEE80211_SAE_ENGINE_HNP_METHOD;
    selected.rsnxe_capabilities =
        (bound.sae_scan_flags & IEEE80211_SAE_SCAN_RSNXE_H2E) != 0 ?
        kItlSaeAuthTransportRsnxeH2e : 0;
    selected.ssid_len = bound.ssid_len;
    selected.credential_source = 1u; /* private WCL CIPHER_PWD slot */
    IEEE80211_ADDR_COPY(selected.bssid, bound.bssid);
    IEEE80211_ADDR_COPY(selected.sta, bound.sta);
    memcpy(selected.ssid, bound.ssid, bound.ssid_len);
    if (!itl_sae_selected_join_event_is_well_formed(&selected))
        goto leave;

    /* Snapshot the separately accepted JoinAdapter request at producer
     * admission, not when an eventual peer result is consumed. A newer
     * request naming the same BSS cannot inherit this engine's outcome. */
    if (ieee80211_wcl_join_copy_current(ic, bound.association_epoch,
            &join_request) && join_request.phase == IEEE80211_JOIN_AUTH &&
        join_request.ssid_len == selected.ssid_len &&
        IEEE80211_ADDR_EQ(join_request.bssid, selected.bssid) &&
        memcmp(join_request.ssid, selected.ssid, selected.ssid_len) == 0)
        join_attempt_generation = join_request.generation;

    IOSimpleLockLock(sc->sc_sae_engine_lock);
    owner = &sc->sc_sae_engine_owner;
    /* A queued lower SCAN may outlive the failed worker which retained this
     * cancelled S_AUTH tombstone.  A strictly newer selected request/epoch
     * is a fresh reference-style reassociation owner, but may supersede the
     * tombstone only after every old producer and frame is empty. */
    if (!sc->sc_sae_engine_stopping && !sc->sc_sae_engine_detaching &&
        owner->active && owner->cancelled &&
        sc->sc_sae_engine == NULL && owner->in_flight_ticket == 0 &&
        !owner->start_pending && !owner->submit_retry_pending &&
        !owner->terminal_valid && owner->peer_count == 0 &&
        !owner->completion_claimed && !owner->assoc_tx_pending &&
        !owner->assoc_tx_accepted &&
        selected.request_generation > owner->request_generation &&
        selected.association_epoch != owner->association_epoch)
        iwn_sae_engine_owner_clear_locked(sc);
    if (!sc->sc_sae_engine_stopping && !sc->sc_sae_engine_detaching &&
        !owner->active && sc->sc_sae_engine == NULL &&
        sc->sc_sae_engine_next_relay_generation != (u_int64_t)-1) {
        relay_generation = ++sc->sc_sae_engine_next_relay_generation;
        explicit_bzero(owner, sizeof(*owner));
        activated.version = kItlSaeAuthTransportV1Version;
        activated.size = sizeof(activated);
        activated.request_generation = selected.request_generation;
        activated.association_epoch = selected.association_epoch;
        activated.relay_generation = relay_generation;
        IEEE80211_ADDR_COPY(activated.bssid, selected.bssid);
        IEEE80211_ADDR_COPY(activated.sta, selected.sta);
        if (itl_sae_auth_activated_event_is_well_formed(&activated)) {
            owner->active = true;
            owner->start_pending = true;
            owner->request_generation = selected.request_generation;
            owner->association_epoch = selected.association_epoch;
            owner->relay_generation = relay_generation;
            owner->join_attempt_generation = join_attempt_generation;
            owner->selected = selected;
            owner->activated = activated;
            held = 1;
        }
    }
    IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    if (!held)
        goto leave;

    /* Admission rechecks the exact public BOUND snapshot under generic's
     * leaf and is the only route that permits a direct SAE|PSK transition
     * BSS.  A local failure still holds S_AUTH until the worker scans away. */
    if (!ieee80211_sae_wcl_peer_rx_admit(ic, &bound, relay_generation)) {
        (void)iwn_sae_engine_mark_cancelled(sc, bound.generation, false,
            &cancel);
        /* The reservation has already held S_AUTH.  Whether a concurrent
         * revoke won the owner leaf or this admission check did, give the
         * sole worker a chance to retire it and scan away. */
        iwn_sae_engine_schedule_task(sc);
    } else {
        iwn_sae_engine_schedule_task(sc);
    }
leave:
    iwn_sae_engine_callback_leave(sc);
out:
    explicit_bzero(&join_request, sizeof(join_request));
    explicit_bzero(&cancel, sizeof(cancel));
    explicit_bzero(&activated, sizeof(activated));
    explicit_bzero(&selected, sizeof(selected));
    explicit_bzero(&bound, sizeof(bound));
    return held;
}

namespace {

static bool
iwn_sae_engine_selected_matches_bound(
    const struct ItlSaeSelectedJoinEventV1 *selected,
    const struct ieee80211_sae_wcl_bound_request *bound)
{
    return selected != NULL && bound != NULL &&
        selected->request_generation == bound->generation &&
        selected->association_epoch == bound->association_epoch &&
        selected->ssid_len == bound->ssid_len &&
        IEEE80211_ADDR_EQ(selected->bssid, bound->bssid) &&
        IEEE80211_ADDR_EQ(selected->sta, bound->sta) &&
        memcmp(selected->ssid, bound->ssid, sizeof(selected->ssid)) == 0;
}

enum IwnSaeEngineSubmitResult {
    IWN_SAE_ENGINE_SUBMIT_OK = 0,
    /* The private workloop gate rejected us before a doorbell.  The engine
     * rollback is therefore exact and a bounded deferred retry is safe. */
    IWN_SAE_ENGINE_SUBMIT_RETRY = 1,
    IWN_SAE_ENGINE_SUBMIT_FAIL = -1,
    /* APSTA traffic can occupy the private gate for more than the historical
     * two 1 ms retries.  Six exponentially spaced attempts still keep the
     * complete per-frame contention window below 100 ms. */
    IWN_SAE_ENGINE_SUBMIT_RETRY_LIMIT = 6,
    IWN_SAE_ENGINE_SUBMIT_RETRY_MAX_SHIFT = 5,
};

static u_int32_t
iwn_sae_engine_submit_retry_delay_ms(u_int8_t retry_count)
{
    if (retry_count > IWN_SAE_ENGINE_SUBMIT_RETRY_MAX_SHIFT)
        retry_count = IWN_SAE_ENGINE_SUBMIT_RETRY_MAX_SHIFT;
    return 1U << retry_count;
}

/* Worker-only: materialize one prepared engine frame, then send it through
 * the existing IWN gate/descriptor/doorbell path.  A submit failure is known
 * to be pre-doorbell, so the engine's rollback API is safe; this owner then
 * takes a bounded non-blocking retry window for a private-gate busy result
 * and fails closed for every ambiguous radio state. */
static int
iwn_sae_engine_submit_prepared(struct iwn_softc *sc)
{
    struct iwn_sae_engine_owner *owner;
    struct ItlSaeAuthTxRequestV1 request;
    struct ieee80211_sae_engine *engine;
    ItlIwn *that;
    u_int64_t ticket = 0;
    IOReturn rc = kIOReturnError;
    bool rearm_mgt_timer = false;

    if (sc == NULL || sc->sc_sae_engine_lock == NULL)
        return -1;
    explicit_bzero(&request, sizeof(request));
    IOSimpleLockLock(sc->sc_sae_engine_lock);
    owner = &sc->sc_sae_engine_owner;
    engine = sc->sc_sae_engine;
    if (owner->active && !owner->cancelled && !owner->suppress_scan &&
        !sc->sc_sae_engine_stopping && !sc->sc_sae_engine_detaching &&
        engine != NULL && owner->in_flight_ticket == 0 &&
        sc->sc_sae_engine_next_ticket !=
        IWN_SAE_ENGINE_TICKET_COUNTER_MASK) {
        ticket = IWN_SAE_ENGINE_TICKET_DIRECT_BIT |
            ++sc->sc_sae_engine_next_ticket;
        owner->in_flight_ticket = ticket;
    }
    IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    if (ticket == 0 || ieee80211_sae_engine_prepare_tx(engine, ticket,
        &request) != 0)
        goto fail;

    that = container_of(sc, ItlIwn, com);
    rc = that->submitSaeAuthFrame(&request);
    if (rc == kIOReturnSuccess) {
        IOSimpleLockLock(sc->sc_sae_engine_lock);
        owner = &sc->sc_sae_engine_owner;
        if (owner->active && !owner->cancelled && !owner->suppress_scan &&
            !sc->sc_sae_engine_stopping && !sc->sc_sae_engine_detaching &&
            owner->in_flight_ticket == ticket)
            owner->submit_retry_count = 0;
        rearm_mgt_timer = owner->active && !owner->cancelled &&
            !owner->suppress_scan && !sc->sc_sae_engine_stopping &&
            !sc->sc_sae_engine_detaching &&
            owner->in_flight_ticket == ticket &&
            sc->sc_ic.ic_state == IEEE80211_S_AUTH &&
            sc->sc_ic.ic_bss != NULL &&
            iwn_sae_engine_owner_matches_node_locked(sc, &sc->sc_ic,
                sc->sc_ic.ic_bss);
        IOSimpleLockUnlock(sc->sc_sae_engine_lock);
        if (rearm_mgt_timer)
            sc->sc_ic.ic_mgt_timer = IEEE80211_TRANS_WAIT;
        explicit_bzero(&request, sizeof(request));
        return IWN_SAE_ENGINE_SUBMIT_OK;
    }
    (void)ieee80211_sae_engine_tx_rollback_unsubmitted(engine, ticket);
fail:
    IOSimpleLockLock(sc->sc_sae_engine_lock);
    owner = &sc->sc_sae_engine_owner;
    if (owner->active && owner->in_flight_ticket == ticket)
        owner->in_flight_ticket = 0;
    IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    explicit_bzero(&request, sizeof(request));
    return rc == kIOReturnNotReady ? IWN_SAE_ENGINE_SUBMIT_RETRY :
        IWN_SAE_ENGINE_SUBMIT_FAIL;
}

/* Worker-only start: two current-bound checks bracket private credential
 * consumption.  Thus a cancellation that wins either before or during the
 * bounded password copy cannot cause a Commit for a former BSS/request. */
static int
iwn_sae_engine_start(struct iwn_softc *sc)
{
    struct ieee80211com *ic;
    struct iwn_sae_engine_owner *owner;
    struct ieee80211_sae_wcl_bound_request bound;
    struct ItlSaeSelectedJoinEventV1 selected;
    struct ItlSaeAuthActivatedEventV1 activated;
    struct ItlSaeWclCredentialV1 credential;
    struct ieee80211_sae_engine *engine = NULL;
    u_int64_t generation = 0;
    int result = -1;

    if (sc == NULL || sc->sc_sae_engine_lock == NULL)
        return -1;
    ic = &sc->sc_ic;
    explicit_bzero(&bound, sizeof(bound));
    explicit_bzero(&selected, sizeof(selected));
    explicit_bzero(&activated, sizeof(activated));
    explicit_bzero(&credential, sizeof(credential));
    IOSimpleLockLock(sc->sc_sae_engine_lock);
    owner = &sc->sc_sae_engine_owner;
    if (owner->active && !owner->cancelled && !owner->suppress_scan &&
        !sc->sc_sae_engine_stopping && !sc->sc_sae_engine_detaching &&
        sc->sc_sae_engine == NULL) {
        selected = owner->selected;
        activated = owner->activated;
        generation = owner->request_generation;
    }
    IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    if (generation == 0)
        goto out;
    if (!ieee80211_sae_wcl_request_copyout_bound_current(ic, generation,
            &bound))
        goto out;
    if (!iwn_sae_engine_selected_matches_bound(&selected, &bound))
        goto out;
    if (!iwn_sae_wcl_credential_take_bound(sc, &selected, &credential))
        goto out;
    if (!ieee80211_sae_wcl_request_copyout_bound_current(ic, generation,
            &bound))
        goto out;
    if (!iwn_sae_engine_selected_matches_bound(&selected, &bound))
        goto out;
    if (ieee80211_sae_engine_begin(&selected, &activated,
        credential.password, credential.password_len, &engine) != 0)
        goto out;
    /* begin consumes its password synchronously; no secret remains in
     * this driver after the immediately following scrub. */
    explicit_bzero(&credential, sizeof(credential));

    IOSimpleLockLock(sc->sc_sae_engine_lock);
    owner = &sc->sc_sae_engine_owner;
    if (owner->active && !owner->cancelled && !owner->suppress_scan &&
        !sc->sc_sae_engine_stopping && !sc->sc_sae_engine_detaching &&
        owner->request_generation == generation && sc->sc_sae_engine == NULL) {
        sc->sc_sae_engine = engine;
        engine = NULL;
        result = 0;
    }
    IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    if (result == 0)
        result = iwn_sae_engine_submit_prepared(sc);
out:
    if (engine != NULL)
        ieee80211_sae_engine_destroy(&engine);
    explicit_bzero(&credential, sizeof(credential));
    explicit_bzero(&activated, sizeof(activated));
    explicit_bzero(&selected, sizeof(selected));
    explicit_bzero(&bound, sizeof(bound));
    return result;
}

/* A normal engine retirement may make a fresh direct request possible.  It
 * must not clear a stop flag or reopen after a newer stop/reset generation:
 * only iwn_init() is authorized to reopen hardware lifecycle admission. */
static void
iwn_sae_engine_reopen_if_current(struct iwn_softc *sc,
    u_int32_t expected_generation)
{
    struct iwn_sae_engine_owner *owner;
    u_int32_t callback_state;
    bool may_publish = false;

    if (sc == NULL || expected_generation == 0 ||
        !iwn_sae_engine_runtime_enabled(sc) ||
        !iwn_sae_tx_lifecycle_is_open(sc))
        return;
    IOSimpleLockLock(sc->sc_sae_engine_lock);
    owner = &sc->sc_sae_engine_owner;
    if (!sc->sc_sae_engine_detaching && !sc->sc_sae_engine_stopping &&
        __atomic_load_n(&sc->sc_sae_engine_lifecycle_generation,
            __ATOMIC_ACQUIRE) == expected_generation && !owner->active &&
        sc->sc_sae_engine == NULL)
        may_publish = true;
    IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    if (!may_publish)
        return;
    /* This worker does not reopen a callback lease that stop closed.  A
     * concurrent close either remains visible here or makes publication's
     * second token check fail under the generic hook writer leaf. */
    callback_state = __atomic_load_n(&sc->sc_sae_engine_callback_state,
        __ATOMIC_ACQUIRE);
    if ((callback_state & IWN_SAE_ENGINE_CALLBACK_CLOSED) != 0)
        return;
    (void)iwn_sae_engine_publish_hooks(sc, true, false,
        expected_generation);
}

/* The crypto worker has consumed a real peer value. Claim only the fresh
 * JoinAdapter AUTH request which still owns this exact engine/BSS/epoch.
 * Reassociation has a separate completion owner and must not acquire a
 * fabricated fresh-join ledger here. No callback runs under either leaf. */
static u_int64_t
iwn_sae_engine_claim_peer_failure(struct iwn_softc *sc,
    struct ieee80211_sae_engine *engine,
    const struct ItlSaeAuthPeerEventV1 *peer,
    enum ieee80211_sae_engine_peer_result result)
{
    IOSimpleLock *bss_lock;
    IOInterruptState irq;
    u_int64_t generation = 0;

    if (sc == NULL || engine == NULL || peer == NULL ||
        sc->sc_sae_engine_lock == NULL ||
        (bss_lock = sc->sc_ic.ic_pae_selected_bss_lock) == NULL ||
        (result != IEEE80211_SAE_ENGINE_PEER_AP_REJECT &&
         result != IEEE80211_SAE_ENGINE_PEER_ABORT))
        return 0;
    irq = IOSimpleLockLockDisableInterrupt(bss_lock);
    IOSimpleLockLock(sc->sc_sae_engine_lock);
    auto *owner = &sc->sc_sae_engine_owner;
    auto *attempt = &sc->sc_ic.ic_wcl_join_attempt;
    const bool peer_rejected = result == IEEE80211_SAE_ENGINE_PEER_AP_REJECT &&
        peer->auth_status != 0;
    if (sc->sc_ic.ic_opmode == IEEE80211_M_STA &&
        sc->sc_sae_engine == engine && !owner->cancelled &&
        !owner->suppress_scan && !sc->sc_sae_engine_stopping &&
        !sc->sc_sae_engine_detaching && !owner->completion_claimed &&
        owner->join_failure_generation == 0 &&
        owner->join_attempt_generation != 0 &&
        owner->join_attempt_generation == attempt->result.generation &&
        iwn_sae_engine_owner_matches_peer_locked(sc, peer) &&
        iwn_sae_engine_peer_owner_current_locked(sc, owner) &&
        attempt->phase == IEEE80211_JOIN_AUTH &&
        attempt->result.association_epoch == owner->association_epoch &&
        attempt->result.ssid_len == owner->selected.ssid_len &&
        IEEE80211_ADDR_EQ(attempt->result.bssid, owner->selected.bssid) &&
        memcmp(attempt->result.ssid, owner->selected.ssid,
            owner->selected.ssid_len) == 0 &&
        ieee80211_join_attempt_fail(attempt, attempt->result.generation,
            owner->association_epoch, IEEE80211_JOIN_AUTH,
            peer_rejected ? IEEE80211_JOIN_FAILURE_PEER_STATUS :
                IEEE80211_JOIN_FAILURE_LOCAL,
            peer_rejected ? peer->auth_status : 0, 0,
            peer_rejected ? 0 : EPROTO, IEEE80211_JOIN_CLEANUP_ALL)) {
        generation = attempt->result.generation;
        owner->join_failure_generation = generation;
    }
    IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    IOSimpleLockUnlockEnableInterrupt(bss_lock, irq);
    return generation;
}

/* This runs only on sae_engine_task after detach has drained that task.  It
 * may call generic state transitions only after it has scrubbed every local
 * SAE identity and released the engine leaf. */
static void
iwn_sae_engine_worker_retire(struct iwn_softc *sc, bool request_scan)
{
    struct iwn_sae_engine_owner *owner;
    struct ieee80211_sae_engine *engine = NULL;
    struct IwnSaeEngineCancellation cancel;
    struct IwnSaeEngineCancellation transport_cancel;
    bool suppress_scan = true;
    bool issue_scan = false;
    bool reopen_hooks = false;
    u_int32_t reopen_generation = 0;
    u_int64_t join_failure_generation = 0;

    if (sc == NULL || sc->sc_sae_engine_lock == NULL)
        return;
    explicit_bzero(&cancel, sizeof(cancel));
    explicit_bzero(&transport_cancel, sizeof(transport_cancel));
    IOSimpleLockLock(sc->sc_sae_engine_lock);
    owner = &sc->sc_sae_engine_owner;
    if (owner->active) {
        cancel.active = true;
        cancel.request_generation = owner->request_generation;
        cancel.association_epoch = owner->association_epoch;
        cancel.relay_generation = owner->relay_generation;
        cancel.ticket = owner->in_flight_ticket;
        join_failure_generation = owner->join_failure_generation;
        suppress_scan = owner->suppress_scan || sc->sc_sae_engine_stopping ||
            sc->sc_sae_engine_detaching;
        /* Preserve this cancelled public identity as an S_AUTH tombstone.
         * auth_owned() must remain true until generic has crossed its state
         * boundary, otherwise a late Open response can downgrade the exact
         * SAE attempt while its worker is still unwinding. */
        owner->cancelled = true;
        owner->start_pending = false;
        owner->submit_retry_pending = false;
        owner->terminal_valid = false;
        explicit_bzero(&owner->terminal, sizeof(owner->terminal));
        explicit_bzero(owner->peerq, sizeof(owner->peerq));
        owner->peer_head = 0;
        owner->peer_tail = 0;
        owner->peer_count = 0;
    }
    engine = sc->sc_sae_engine;
    sc->sc_sae_engine = NULL;
    IOSimpleLockUnlock(sc->sc_sae_engine_lock);

    if (engine != NULL)
        ieee80211_sae_engine_destroy(&engine);
    /*
     * An accepted Association descriptor has finished the Algorithm-3
     * transport owner, not the association.  Revoke its frame/peer-RX
     * resources while preserving the sole PENDING CIPHER_PWD record until
     * the later 4-way/PMF port-valid edge promotes it to ACTIVE.  Every
     * failure path still supplies the generation and therefore scrubs it.
     */
    transport_cancel = cancel;
    if (!request_scan)
        transport_cancel.request_generation = 0;
    iwn_sae_engine_cancel_owned(sc, &transport_cancel);
    explicit_bzero(&transport_cancel, sizeof(transport_cancel));
    if (request_scan && cancel.active)
        iwn_sae_wcl_credential_retire_pending_generation(sc,
            cancel.request_generation);
    issue_scan = request_scan && cancel.active && !suppress_scan &&
        (IC2IFP(&sc->sc_ic)->if_flags & IFF_RUNNING) != 0 &&
        (sc->sc_ic.ic_state == IEEE80211_S_AUTH ||
        sc->sc_ic.ic_state == IEEE80211_S_ASSOC);
    if (issue_scan && join_failure_generation != 0)
        ItlIwn::iwn_wcl_join_failure_scan(&sc->sc_ic,
            join_failure_generation);
    else if (issue_scan)
        ieee80211_new_state(&sc->sc_ic, IEEE80211_S_SCAN, -1);

    IOSimpleLockLock(sc->sc_sae_engine_lock);
    owner = &sc->sc_sae_engine_owner;
    if (owner->active && cancel.active &&
        owner->request_generation == cancel.request_generation &&
        owner->association_epoch == cancel.association_epoch &&
        owner->relay_generation == cancel.relay_generation &&
        /* A stopped exchange stays a tombstone until its later safe reopen;
         * detach owns its final explicit destroy below. */
        !sc->sc_sae_engine_stopping && !sc->sc_sae_engine_detaching &&
        sc->sc_ic.ic_state != IEEE80211_S_AUTH) {
        iwn_sae_engine_owner_clear_locked(sc);
    } else if (!owner->active && !sc->sc_sae_engine_stopping &&
        !sc->sc_sae_engine_detaching) {
        /* A start failure before publishing an owner has no tombstone. */
        iwn_sae_engine_owner_clear_locked(sc);
    }
    reopen_hooks = !sc->sc_sae_engine_stopping &&
        !sc->sc_sae_engine_detaching && !owner->active &&
        sc->sc_sae_engine == NULL;
    if (reopen_hooks)
        reopen_generation = __atomic_load_n(
            &sc->sc_sae_engine_lifecycle_generation, __ATOMIC_ACQUIRE);
    IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    if (reopen_hooks)
        iwn_sae_engine_reopen_if_current(sc, reopen_generation);
    explicit_bzero(&cancel, sizeof(cancel));
    explicit_bzero(&transport_cancel, sizeof(transport_cancel));
}

} // namespace

static void
iwn_sae_engine_request_join_retirement(struct iwn_softc *sc,
    u_int64_t generation)
{
    bool queued = false;

    if (sc == NULL ||
        !ieee80211_wcl_join_failure_pending(&sc->sc_ic, generation))
        return;
    if (sc->sc_sae_engine_lock == NULL) {
        /* Crypto allocation can fail independently of the legacy transport.
         * Its worker must retire any remaining native descriptor/event. */
        if (sc->sc_sae_tx_lock == NULL)
            return;
        IOSimpleLockLock(sc->sc_sae_tx_lock);
        if (!sc->sc_sae_tx_stopping &&
            generation >= sc->sc_sae_tx_join_failure_generation) {
            sc->sc_sae_tx_join_failure_generation = generation;
            queued = true;
        }
        IOSimpleLockUnlock(sc->sc_sae_tx_lock);
        if (queued)
            iwn_sae_tx_schedule_task(sc, false);
        return;
    }
    IOSimpleLockLock(sc->sc_sae_engine_lock);
    if (sc->sc_sae_engine_task_ready && !sc->sc_sae_engine_stopping &&
        !sc->sc_sae_engine_detaching &&
        generation >= sc->sc_sae_engine_join_failure_generation) {
        sc->sc_sae_engine_join_failure_generation = generation;
        queued = true;
    }
    IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    if (queued)
        iwn_sae_engine_schedule_task(sc);
}

/* Called by the engine worker with its TX lifecycle admission still held,
 * after local secret buffers and every cancellation/engine owner are gone. */
static void
iwn_sae_engine_finish_join_retirement(struct iwn_softc *sc)
{
    u_int64_t generation = 0;

    if (sc == NULL || sc->sc_sae_engine_lock == NULL)
        return;
    IOSimpleLockLock(sc->sc_sae_engine_lock);
    if (!sc->sc_sae_engine_owner.active && sc->sc_sae_engine == NULL &&
        sc->sc_sae_engine_wcl_cancel_generation == 0 &&
        !sc->sc_sae_engine_stopping && !sc->sc_sae_engine_detaching) {
        /* Same engine -> TX leaf order as the doorbell cancellation fence.
         * A cancelled descriptor remains live until TX_DONE/reset reclaims
         * it; queued terminal values are drained by the TX worker as well. */
        if (sc->sc_sae_tx_lock != NULL)
            IOSimpleLockLock(sc->sc_sae_tx_lock);
        if (!sc->sc_sae_tx_active && sc->sc_sae_tx_event_count == 0) {
            generation = sc->sc_sae_engine_join_failure_generation;
            sc->sc_sae_engine_join_failure_generation = 0;
        }
        if (sc->sc_sae_tx_lock != NULL)
            IOSimpleLockUnlock(sc->sc_sae_tx_lock);
    }
    IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    if (generation != 0)
        ieee80211_wcl_join_cleanup_done(&sc->sc_ic, generation,
            IEEE80211_JOIN_CLEANUP_SAE);
}

void ItlIwn::
iwn_sae_engine_task(void *arg)
{
    struct iwn_softc *sc = (struct iwn_softc *)arg;
    struct iwn_sae_engine_owner *owner;
    struct ItlSaeAuthTransportEventV1 terminal;
    struct ItlSaeAuthPeerEventV1 peer;
    struct ItlSaePmkContinuationV1 continuation;
    struct ieee80211_sae_engine *engine;
    enum ieee80211_sae_engine_peer_result peer_result;
    u_int64_t wcl_cancel_generation = 0;
    u_int64_t join_failure_generation = 0;
    bool start = false;
    bool retry_submit = false;
    bool have_terminal = false;
    bool have_peer = false;
    bool cancel = false;
    bool assoc_tx_accepted = false;
    bool more = false;
    bool fail = false;
    u_int8_t retry_count = 0;
    int submit_result = IWN_SAE_ENGINE_SUBMIT_OK;

    if (sc == NULL)
        return;
    if (!iwn_sae_tx_lifecycle_enter(sc, true))
        return;
    explicit_bzero(&terminal, sizeof(terminal));
    explicit_bzero(&peer, sizeof(peer));
    explicit_bzero(&continuation, sizeof(continuation));
    if (sc->sc_sae_engine_lock == NULL)
        goto out;

    IOSimpleLockLock(sc->sc_sae_engine_lock);
    owner = &sc->sc_sae_engine_owner;
    /* A generic callback can revoke a staged password before selected-BSS
     * ownership exists.  Drain that public fence even with no active engine. */
    wcl_cancel_generation = sc->sc_sae_engine_wcl_cancel_generation;
    sc->sc_sae_engine_wcl_cancel_generation = 0;
    if (owner->active) {
        cancel = owner->cancelled || owner->suppress_scan ||
            sc->sc_sae_engine_stopping || sc->sc_sae_engine_detaching;
        if (!cancel && owner->start_pending) {
            owner->start_pending = false;
            start = true;
        } else if (!cancel && owner->submit_retry_pending) {
            owner->submit_retry_pending = false;
            retry_submit = true;
            retry_count = owner->submit_retry_count;
        } else if (!cancel && owner->terminal_valid) {
            terminal = owner->terminal;
            owner->terminal_valid = false;
            explicit_bzero(&owner->terminal, sizeof(owner->terminal));
            have_terminal = true;
        } else if (!cancel && owner->assoc_tx_accepted) {
            assoc_tx_accepted = true;
        } else if (!cancel && owner->peer_count != 0 &&
            owner->in_flight_ticket == 0) {
            peer = owner->peerq[owner->peer_head];
            explicit_bzero(&owner->peerq[owner->peer_head],
                sizeof(owner->peerq[owner->peer_head]));
            owner->peer_head = (owner->peer_head + 1) %
                IWN_SAE_ENGINE_PEERQ_LEN;
            owner->peer_count--;
            have_peer = true;
        }
    }
    IOSimpleLockUnlock(sc->sc_sae_engine_lock);

    if (wcl_cancel_generation != 0) {
        ItlIwn *that = container_of(sc, ItlIwn, com);
        that->cancelSaeWclCredential(wcl_cancel_generation);
    }

    if (cancel) {
        iwn_sae_engine_worker_retire(sc, true);
        goto out;
    }
    /* The engine remains alive until generic's asynchronously queued
     * Association Request has crossed the real descriptor doorbell.  Only
     * that accepted doorbell may retire the direct SAE owner without a scan. */
    if (assoc_tx_accepted) {
        iwn_sae_engine_worker_retire(sc, false);
        goto out;
    }
    if (start || retry_submit) {
        /* attemptAction() can reject a task solely because APSTA work owns
         * the private IWN gate.  Never block on that gate from systq: use a
         * bounded exponential delay and let detach/power-off keep draining. */
        if (retry_submit)
            IOSleep(iwn_sae_engine_submit_retry_delay_ms(retry_count));
        submit_result = start ? iwn_sae_engine_start(sc) :
            iwn_sae_engine_submit_prepared(sc);
    } else if (have_terminal) {
        IOSimpleLockLock(sc->sc_sae_engine_lock);
        engine = sc->sc_sae_engine;
        IOSimpleLockUnlock(sc->sc_sae_engine_lock);
        if (engine == NULL || ieee80211_sae_engine_tx_complete(engine,
            &terminal) != 0) {
            fail = true;
        } else {
            if (terminal.phase == kItlSaeAuthTransportPhaseCommit) {
                IWN_DIRECT_SAE_TRACE(&sc->sc_ic,
                    kAirportItlwmPostPltiTraceEventIwnDirectSaeCommitTxComplete);
            } else if (terminal.phase == kItlSaeAuthTransportPhaseConfirm) {
                IWN_DIRECT_SAE_TRACE(&sc->sc_ic,
                    kAirportItlwmPostPltiTraceEventIwnDirectSaeConfirmTxComplete);
            }
            IOSimpleLockLock(sc->sc_sae_engine_lock);
            owner = &sc->sc_sae_engine_owner;
            if (owner->active && owner->in_flight_ticket == terminal.ticket)
                owner->in_flight_ticket = 0;
            IOSimpleLockUnlock(sc->sc_sae_engine_lock);
        }
    } else if (have_peer) {
        IOSimpleLockLock(sc->sc_sae_engine_lock);
        engine = sc->sc_sae_engine;
        IOSimpleLockUnlock(sc->sc_sae_engine_lock);
        if (engine == NULL) {
            fail = true;
        } else {
            peer_result = ieee80211_sae_engine_handle_peer(engine, &peer,
                &continuation);
            if (peer_result == IEEE80211_SAE_ENGINE_PEER_TX_READY) {
                /* `PEER_TX_READY` also covers an anti-clogging token retry,
                 * which prepares a second local Commit but has not accepted
                 * a peer Commit.  Record this boundary only after the engine
                 * accepted the successful wire-sequence-1 peer Commit and
                 * prepared our Confirm; no peer bytes leave the recorder. */
                if (peer.phase == kItlSaeAuthTransportPhaseCommit &&
                    peer.wire_transaction ==
                        kItlSaeAuthTransportPeerWireTransactionCommit &&
                    (peer.auth_status == IEEE80211_STATUS_SUCCESS ||
                     peer.auth_status ==
                        kItlSaeAuthTransportStatusSaeHashToElement))
                    IWN_DIRECT_SAE_TRACE(&sc->sc_ic,
                        kAirportItlwmPostPltiTraceEventIwnDirectSaePeerCommitAccepted);
                submit_result = iwn_sae_engine_submit_prepared(sc);
            } else if (peer_result == IEEE80211_SAE_ENGINE_PEER_COMPLETE) {
                IOSimpleLock *bss_lock;
                IOInterruptState irq;
                bool pmk_claimed = false;
                bool assoc_started = false;

                /* `PEER_COMPLETE` is the in-kext engine's verified peer
                 * Confirm boundary; it carries no value out of the engine. */
                IWN_DIRECT_SAE_TRACE(&sc->sc_ic,
                    kAirportItlwmPostPltiTraceEventIwnDirectSaePeerConfirmValidated);
                /* The PMK and scalar-derived SAE PMKID came from the same
                 * accepted in-kext exchange.  Recheck the bounded record at
                 * the IWN/net80211 boundary before either value can enter
                 * the local PAE. */
                if (!itl_sae_pmk_continuation_is_well_formed(
                    &continuation)) {
                    fail = true;
                } else if ((bss_lock = sc->sc_ic.ic_pae_selected_bss_lock) ==
                    NULL) {
                    fail = true;
                } else {
                    /* The selected-BSS leaf precedes the engine leaf.  This
                     * one short critical section claims the public direct
                     * request and stores only its completion identity; state
                     * transition and all callbacks occur after both unlock. */
                    irq = IOSimpleLockLockDisableInterrupt(bss_lock);
                    IOSimpleLockLock(sc->sc_sae_engine_lock);
                    owner = &sc->sc_sae_engine_owner;
                    if (sc->sc_sae_engine == engine &&
                        !owner->completion_claimed &&
                        !owner->assoc_tx_pending &&
                        !owner->assoc_tx_accepted &&
                        iwn_sae_engine_owner_matches_pmk_identity_locked(sc,
                        owner, &continuation.identity) &&
                        iwn_sae_engine_peer_owner_current_locked(sc, owner) &&
                        ieee80211_sae_wcl_request_pmk_claim_locked(
                        &sc->sc_ic, &continuation)) {
                        owner->completion = continuation.identity;
                        owner->completion_claimed = true;
                        owner->assoc_tx_pending = true;
                        pmk_claimed = true;
                    }
                    IOSimpleLockUnlock(sc->sc_sae_engine_lock);
                    IOSimpleLockUnlockEnableInterrupt(bss_lock, irq);

                    if (pmk_claimed) {
                        /* This is after the selected-BSS + engine leaves
                         * released their one-shot local PMK claim. */
                        IWN_DIRECT_SAE_TRACE(&sc->sc_ic,
                            kAirportItlwmPostPltiTraceEventIwnDirectSaePmkClaimed);
                        assoc_started =
                            ieee80211_sae_wcl_request_pmk_continue_assoc(
                            &sc->sc_ic, &continuation.identity) != 0;
                    }
                    /* A driver newstate override can run before generic's
                     * sentinel.  Confirm that it actually left us in the
                     * claimed S_ASSOC state; otherwise retire deterministically. */
                    if (assoc_started) {
                        irq = IOSimpleLockLockDisableInterrupt(bss_lock);
                        assoc_started = sc->sc_ic.ic_state ==
                            IEEE80211_S_ASSOC &&
                            ieee80211_sae_wcl_request_pmk_claim_assoc_current_locked(
                            &sc->sc_ic, sc->sc_ic.ic_bss,
                            &continuation.identity);
                        IOSimpleLockUnlockEnableInterrupt(bss_lock, irq);
                    }
                    if (!assoc_started)
                        fail = true;
                }
            } else if (peer_result == IEEE80211_SAE_ENGINE_PEER_ABORT ||
                peer_result == IEEE80211_SAE_ENGINE_PEER_AP_REJECT) {
                join_failure_generation = iwn_sae_engine_claim_peer_failure(
                    sc, engine, &peer, peer_result);
                fail = true;
            }
        }
    }
    if (submit_result == IWN_SAE_ENGINE_SUBMIT_RETRY) {
        bool retry_admitted = false;

        IOSimpleLockLock(sc->sc_sae_engine_lock);
        owner = &sc->sc_sae_engine_owner;
        if (owner->active && !owner->cancelled && !owner->suppress_scan &&
            !sc->sc_sae_engine_stopping && !sc->sc_sae_engine_detaching &&
            owner->in_flight_ticket == 0 &&
            owner->submit_retry_count <
                IWN_SAE_ENGINE_SUBMIT_RETRY_LIMIT) {
            owner->submit_retry_count++;
            owner->submit_retry_pending = true;
            retry_admitted = true;
        }
        IOSimpleLockUnlock(sc->sc_sae_engine_lock);
        if (retry_admitted) {
            iwn_sae_engine_schedule_task(sc);
            goto out;
        }
        fail = true;
    } else if (submit_result != IWN_SAE_ENGINE_SUBMIT_OK) {
        fail = true;
    }
    if (fail) {
        iwn_sae_engine_worker_retire(sc, true);
        goto out;
    }

    IOSimpleLockLock(sc->sc_sae_engine_lock);
    owner = &sc->sc_sae_engine_owner;
    more = owner->active && (owner->cancelled || owner->start_pending ||
        owner->submit_retry_pending || owner->terminal_valid ||
        owner->assoc_tx_accepted ||
        (owner->peer_count != 0 && owner->in_flight_ticket == 0));
    IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    if (more)
        iwn_sae_engine_schedule_task(sc);
out:
    explicit_bzero(&continuation, sizeof(continuation));
    explicit_bzero(&peer, sizeof(peer));
    explicit_bzero(&terminal, sizeof(terminal));
    if (join_failure_generation != 0)
        ieee80211_wcl_join_cleanup_done(&sc->sc_ic, join_failure_generation,
            IEEE80211_JOIN_CLEANUP_PRODUCER);
    iwn_sae_engine_finish_join_retirement(sc);
    iwn_sae_tx_lifecycle_leave(sc);
}

/* Stop may be reached from IWN notification/IRQ-adjacent paths.  It closes
 * admission, records cancellation and queues the worker only; no generic
 * revoke, WCL leaf, workloop gate, sleep or task barrier is permitted here. */
void ItlIwn::
iwn_sae_engine_stop_begin(struct iwn_softc *sc)
{
    struct IwnSaeEngineCancellation cancel;
    bool schedule = false;

    if (sc == NULL)
        return;
    explicit_bzero(&cancel, sizeof(cancel));
    if (sc->sc_sae_engine_lock != NULL) {
        IOSimpleLockLock(sc->sc_sae_engine_lock);
        sc->sc_sae_engine_stopping = true;
        sc->sc_sae_engine_join_failure_generation = 0;
        (void)iwn_sae_engine_generation_advance_locked(sc);
        (void)iwn_sae_engine_mark_cancelled_locked(sc, 0, true, &cancel);
        schedule = sc->sc_sae_engine_task_ready;
        IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    }
    /* Cancellation/fence linearizes before close.  A hook which had already
     * entered can now only observe the cancelled owner, never progress it. */
    iwn_sae_engine_callback_close(sc);
    (void)iwn_sae_engine_publish_hooks(sc, false, true, 0);
    if (schedule)
        iwn_sae_engine_schedule_task(sc);
    explicit_bzero(&cancel, sizeof(cancel));
}

/* This is called only after a completed hardware init.  It is the sole
 * lifecycle path allowed to clear the stop flag and reopen a closed hook
 * lease; deferred engine retirement uses reopen_if_current() above. */
void ItlIwn::
iwn_sae_engine_reopen(struct iwn_softc *sc)
{
    struct iwn_sae_engine_owner *owner;
    u_int32_t generation = 0;
    u_int32_t callback_state;
    bool pending_retire = false;
    bool may_publish = false;
    bool may_open_callback = false;

    if (sc == NULL)
        return;
    if (!iwn_sae_engine_runtime_enabled(sc) ||
        !iwn_sae_tx_lifecycle_is_open(sc)) {
        bool detaching = false;

        /* A queued init task can reach this edge after detach has closed
         * task admission but before its final IRQ quiescence.  It must not
         * withdraw auth_owned: only final detach unpublishes that tombstone. */
        if (sc->sc_sae_engine_lock != NULL) {
            IOSimpleLockLock(sc->sc_sae_engine_lock);
            detaching = sc->sc_sae_engine_detaching;
            IOSimpleLockUnlock(sc->sc_sae_engine_lock);
        }
        if (detaching)
            return;
        iwn_sae_engine_callback_close(sc);
        (void)iwn_sae_engine_publish_hooks(sc, false, true, 0);
        return;
    }

    IOSimpleLockLock(sc->sc_sae_engine_lock);
    if (!sc->sc_sae_engine_detaching) {
        sc->sc_sae_engine_stopping = false;
        generation = __atomic_load_n(
            &sc->sc_sae_engine_lifecycle_generation, __ATOMIC_ACQUIRE);
        owner = &sc->sc_sae_engine_owner;
        /* A stop-suppressed tombstone becomes safe to retire once generic
         * has already changed its epoch/state.  If it is still S_AUTH, let
         * the worker drive the explicit fail-closed SCAN edge now that the
         * radio is usable again. */
        if (owner->active && owner->cancelled) {
            owner->suppress_scan = false;
            pending_retire = true;
        }
        may_publish = !owner->active && sc->sc_sae_engine == NULL;
    }
    IOSimpleLockUnlock(sc->sc_sae_engine_lock);

    if (generation == 0 || (!pending_retire && !may_publish))
        return;

    callback_state = __atomic_load_n(&sc->sc_sae_engine_callback_state,
        __ATOMIC_ACQUIRE);
    if ((callback_state & IWN_SAE_ENGINE_CALLBACK_CLOSED) != 0) {
        iwn_sae_engine_callback_drain(sc);
        /* Drain is deliberately outside the engine leaf, so a concurrent
         * stop/detach may win while we wait.  Revalidate its generation and
         * reopen the lease while that same leaf is held: a stale init must
         * never reopen a copied peer hook after detach has closed it.
         *
         * An explicit hardware init is also allowed to reopen the lease for
         * one still-active cancelled tombstone.  It leaves the other hooks
         * withdrawn, schedules retirement, and lets that worker publish only
         * after it has released the old owner (and SCAN when still in AUTH). */
        IOSimpleLockLock(sc->sc_sae_engine_lock);
        owner = &sc->sc_sae_engine_owner;
        pending_retire = false;
        may_publish = false;
        if (!sc->sc_sae_engine_detaching && !sc->sc_sae_engine_stopping &&
            __atomic_load_n(&sc->sc_sae_engine_lifecycle_generation,
                __ATOMIC_ACQUIRE) == generation) {
            if (owner->active && owner->cancelled) {
                owner->suppress_scan = false;
                pending_retire = true;
            }
            may_publish = !owner->active && sc->sc_sae_engine == NULL;
            may_open_callback = pending_retire || may_publish;
            if (may_open_callback)
                may_open_callback = iwn_sae_engine_callback_open(sc);
        }
        IOSimpleLockUnlock(sc->sc_sae_engine_lock);
        if (!may_open_callback)
            return;
    }

    /* The callback may already have been open when this init entered.  Take
     * one fresh owner snapshot so a worker which retired during the drain
     * cannot leave a lease open without either scheduling retirement or
     * restoring the complete direct-owner hook set. */
    IOSimpleLockLock(sc->sc_sae_engine_lock);
    owner = &sc->sc_sae_engine_owner;
    pending_retire = false;
    may_publish = false;
    if (!sc->sc_sae_engine_detaching && !sc->sc_sae_engine_stopping &&
        __atomic_load_n(&sc->sc_sae_engine_lifecycle_generation,
            __ATOMIC_ACQUIRE) == generation) {
        if (owner->active && owner->cancelled) {
            owner->suppress_scan = false;
            pending_retire = true;
        }
        may_publish = !owner->active && sc->sc_sae_engine == NULL;
    }
    IOSimpleLockUnlock(sc->sc_sae_engine_lock);

    if (pending_retire) {
        iwn_sae_engine_schedule_task(sc);
        return;
    }
    if (may_publish)
        (void)iwn_sae_engine_publish_hooks(sc, true, false, generation);
}

/* Detach is process context and executes before the IWN TX gate/lifecycle is
 * removed.  The engine worker can therefore retire a doorbelled descriptor,
 * revoke peer RX and scrub staged WCL state before the final task barrier
 * permits its private engine/leaf to disappear. */
void ItlIwn::
iwn_sae_engine_detach_begin(struct iwn_softc *sc)
{
    struct IwnSaeEngineCancellation cancel;
    struct ieee80211_sae_engine *engine = NULL;
    u_int64_t wcl_cancel_generation = 0;
    bool task_ready = false;

    if (sc == NULL)
        return;
    explicit_bzero(&cancel, sizeof(cancel));
    if (sc->sc_sae_engine_lock != NULL) {
        IOSimpleLockLock(sc->sc_sae_engine_lock);
        sc->sc_sae_engine_detaching = true;
        sc->sc_sae_engine_stopping = true;
        (void)iwn_sae_engine_generation_advance_locked(sc);
        (void)iwn_sae_engine_mark_cancelled_locked(sc, 0, true, &cancel);
        task_ready = sc->sc_sae_engine_task_ready;
        IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    }
    /* Close copied generic hooks before task admission.  Otherwise a hook
     * which enters in the small CLOSED-task window sees runtime disabled and
     * could return the legacy-fallback value instead of the fail-closed SAE
     * tombstone result. */
    iwn_sae_engine_callback_close(sc);
    /* Detaching is visible before task admission closes, so a stale init
     * worker cannot interpret CLOSED as a normal runtime-unavailable state
     * and withdraw the S_AUTH tombstone.  A producer which already won this
     * lease finishes task_add before task_del()+barrier below. */
    iwn_sae_engine_task_admission_close(sc);
    iwn_sae_engine_task_admission_drain(sc);
    if (sc->sc_sae_engine_lock != NULL) {
        IOSimpleLockLock(sc->sc_sae_engine_lock);
        /* Enqueue admission is closed and drained above, so no producer can
         * add the task after the task_del()+barrier snapshot below. */
        sc->sc_sae_engine_task_ready = false;
        IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    }
    (void)iwn_sae_engine_publish_hooks(sc, false, true, 0);
    /* Callback drain protects copied generic/TX hook bodies; the independent
     * task-admission drain above already fenced every task_add before the
     * task_del()+barrier snapshot below. */
    iwn_sae_engine_callback_drain(sc);
    if (task_ready && systq != NULL) {
        (void)task_del(systq, &sc->sae_engine_task);
        taskq_barrier(systq);
    }

    if (sc->sc_sae_engine_lock != NULL) {
        IOSimpleLockLock(sc->sc_sae_engine_lock);
        engine = sc->sc_sae_engine;
        sc->sc_sae_engine = NULL;
        wcl_cancel_generation = sc->sc_sae_engine_wcl_cancel_generation;
        sc->sc_sae_engine_wcl_cancel_generation = 0;
        sc->sc_sae_engine_join_failure_generation = 0;
        iwn_sae_engine_owner_clear_locked(sc);
        IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    }
    if (engine != NULL)
        ieee80211_sae_engine_destroy(&engine);
    if (cancel.active)
        iwn_sae_engine_cancel_owned(sc, &cancel);
    if (wcl_cancel_generation != 0) {
        ItlIwn *that = container_of(sc, ItlIwn, com);
        that->cancelSaeWclCredential(wcl_cancel_generation);
    }
    explicit_bzero(&cancel, sizeof(cancel));
}

/*
 * IWN firmware has no reliable PMF-key command path.  A negotiated PMF
 * association therefore uses the existing net80211 software CCMP/BIP
 * implementations.  These helpers own only the asynchronous *preparation*
 * of software CCMP contexts; the generic PAE transaction remains the sole
 * publisher of live keys and protection flags.
 */
static bool
iwn_mfp_pae_stage_valid(u_int8_t stage)
{
    return stage == IEEE80211_PAE_MFP_STAGE_PTK ||
        stage == IEEE80211_PAE_MFP_STAGE_GTK ||
        stage == IEEE80211_PAE_MFP_STAGE_IGTK;
}

static u_int8_t
iwn_mfp_pae_stage_mask(u_int8_t stage)
{
    if (!iwn_mfp_pae_stage_valid(stage))
        return 0;
    return (u_int8_t)(1U << (stage - IEEE80211_PAE_MFP_STAGE_PTK));
}

/* A stage fact is emitted only after the worker has made the value durable in
 * its local owner.  It is a fixed category, never the key, BSS, or txn ID. */
static uint32_t
iwn_mfp_pae_trace_stage_event(u_int8_t stage)
{
    switch (stage) {
    case IEEE80211_PAE_MFP_STAGE_PTK:
        return kAirportItlwmPostPltiTraceEventIwnMfpPaePtkSoftwarePrepared;
    case IEEE80211_PAE_MFP_STAGE_GTK:
        return kAirportItlwmPostPltiTraceEventIwnMfpPaeGtkSoftwarePrepared;
    case IEEE80211_PAE_MFP_STAGE_IGTK:
        return kAirportItlwmPostPltiTraceEventIwnMfpPaeIgtkStageAcknowledged;
    default:
        return kAirportItlwmPostPltiTraceEventUnknown;
    }
}

static bool
iwn_mfp_pae_key_valid(const struct ieee80211_key *key, u_int8_t stage)
{
    if (key == NULL || !iwn_mfp_pae_stage_valid(stage) ||
        key->k_priv != NULL)
        return false;
    if (stage == IEEE80211_PAE_MFP_STAGE_IGTK) {
        return key->k_id >= IEEE80211_WEP_NKID &&
            key->k_id < IEEE80211_GROUP_NKID &&
            key->k_cipher == IEEE80211_CIPHER_BIP &&
            (key->k_flags & IEEE80211_KEY_IGTK) != 0 &&
            key->k_len == IEEE80211_BIP_KEYLEN;
    }
    if (key->k_cipher != IEEE80211_CIPHER_CCMP ||
        key->k_len != ieee80211_cipher_keylen(IEEE80211_CIPHER_CCMP))
        return false;
    if (stage == IEEE80211_PAE_MFP_STAGE_GTK)
        return (key->k_flags & IEEE80211_KEY_GROUP) != 0;
    return (key->k_flags & IEEE80211_KEY_GROUP) == 0;
}

/* Caller holds the selected-BSS fence and the IWN PMF owner lock.  The final
 * fact is deliberately stricter than a successful callback: it describes the
 * exact live software-CCMP plus BIP descriptor state after the atomic generic
 * publication, without exporting any descriptor contents. */
static bool
iwn_mfp_pae_software_keyset_live_locked(struct ieee80211com *ic,
    const struct ieee80211_pae_mfp_txn *generic,
    const struct ieee80211_node *ni)
{
    const struct ieee80211_key *ptk, *gtk;
    const u_int ccmp_flags = IEEE80211_KEY_SWCRYPTO |
        IEEE80211_KEY_PAE_MFP_LIVE;
    const u_int mgmt_flags = IEEE80211_NODE_TXMGMTPROT |
        IEEE80211_NODE_RXMGMTPROT;

    if (ic == NULL || generic == NULL || ni == NULL ||
        !generic->have_ptk || !generic->have_gtk || !generic->have_igtk ||
        !generic->finish_published ||
        generic->gtk_key.k_id >= IEEE80211_WEP_NKID ||
        generic->igtk_key.k_id < IEEE80211_WEP_NKID ||
        generic->igtk_key.k_id >= IEEE80211_GROUP_NKID)
        return false;
    ptk = &ni->ni_pairwise_key;
    gtk = &ic->ic_nw_keys[generic->gtk_key.k_id];
    return ptk->k_priv != NULL && gtk->k_priv != NULL &&
        (ptk->k_flags & ccmp_flags) == ccmp_flags &&
        (gtk->k_flags & ccmp_flags) == ccmp_flags &&
        /* A zero return from finish_publish_locked() is the authoritative
         * locked BIP publication proof: it admits only a prepared local BIP
         * context, publishes it into its live slot, then sets these flags. */
        ic->ic_igtk_kid == generic->igtk_key.k_id &&
        (ni->ni_flags & mgmt_flags) == mgmt_flags;
}

static void
iwn_mfp_pae_dispose_key(struct ieee80211com *ic, struct ieee80211_key *key)
{
    if (key == NULL)
        return;
    if (key->k_priv != NULL)
        ieee80211_delete_key(ic, NULL, key);
    else
        explicit_bzero(key, sizeof(*key));
}

#define IWN_MFP_PAE_CALLBACK_CLOSED      0x80000000U
#define IWN_MFP_PAE_CALLBACK_COUNT_MASK  0x7fffffffU

/* The generic owner snapshots callback pointers before it drops its
 * selected-BSS leaf.  A detach can therefore race a just-copied IWN hook.
 * This lease is deliberately atomic on admission: the hook may be called
 * from an interrupt-adjacent path, while only detach is allowed to sleep.
 * Close makes a stale snapshot that has not entered yet fast-fail; drain
 * waits only for calls which have already incremented the active count. */
static bool
iwn_mfp_pae_callback_enter(struct iwn_softc *sc)
{
    u_int32_t state, next;

    if (sc == NULL)
        return false;
    state = __atomic_load_n(&sc->sc_mfp_pae_callback_state,
        __ATOMIC_ACQUIRE);
    for (;;) {
        if ((state & IWN_MFP_PAE_CALLBACK_CLOSED) != 0 ||
            (state & IWN_MFP_PAE_CALLBACK_COUNT_MASK) ==
            IWN_MFP_PAE_CALLBACK_COUNT_MASK)
            return false;
        next = state + 1;
        if (__atomic_compare_exchange_n(&sc->sc_mfp_pae_callback_state,
            &state, next, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return true;
    }
}

static void
iwn_mfp_pae_callback_leave(struct iwn_softc *sc)
{
    u_int32_t state, next;

    if (sc == NULL)
        return;
    /* RX-side completion must not block on a detach mutex.  The closed
     * detach owner polls this atomic count, so no wakeup can be lost. */
    state = __atomic_load_n(&sc->sc_mfp_pae_callback_state,
        __ATOMIC_ACQUIRE);
    for (;;) {
        if ((state & IWN_MFP_PAE_CALLBACK_COUNT_MASK) == 0)
            return;
        next = state - 1;
        if (__atomic_compare_exchange_n(&sc->sc_mfp_pae_callback_state,
            &state, next, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return;
    }
}

static void
iwn_mfp_pae_callback_close(struct iwn_softc *sc)
{
    u_int32_t state, next;

    if (sc == NULL)
        return;
    state = __atomic_load_n(&sc->sc_mfp_pae_callback_state,
        __ATOMIC_ACQUIRE);
    for (;;) {
        if ((state & IWN_MFP_PAE_CALLBACK_CLOSED) != 0)
            return;
        next = state | IWN_MFP_PAE_CALLBACK_CLOSED;
        if (__atomic_compare_exchange_n(&sc->sc_mfp_pae_callback_state,
            &state, next, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return;
    }
}

static bool
iwn_mfp_pae_callback_open(struct iwn_softc *sc)
{
    u_int32_t expected = IWN_MFP_PAE_CALLBACK_CLOSED;

    if (sc == NULL)
        return false;
    return __atomic_compare_exchange_n(&sc->sc_mfp_pae_callback_state,
        &expected, 0, false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE);
}

static void
iwn_mfp_pae_callback_drain(struct iwn_softc *sc)
{
    if (sc == NULL)
        return;
    /* Close rejects every new enter.  A short sleep/recheck is intentional:
     * it avoids a blocking callback-side wakeup and therefore cannot miss a
     * final decrement between a test and a wait. */
    while ((__atomic_load_n(&sc->sc_mfp_pae_callback_state,
        __ATOMIC_ACQUIRE) & IWN_MFP_PAE_CALLBACK_COUNT_MASK) != 0)
        IOSleep(1);
}

static void
iwn_mfp_pae_callback_destroy(struct iwn_softc *sc)
{
    if (sc == NULL)
        return;
    /* This does not claim to own arbitrary producers.  IWN detach removes
     * the IRQ event source and barriers systq before ItlIwn::free() can
     * release the embedded softc, so a copied pre-enter hook still reaches
     * the CLOSED fast-fail while this state remains valid. */
    iwn_mfp_pae_callback_close(sc);
    iwn_mfp_pae_callback_drain(sc);
}

static void
iwn_mfp_pae_generation_advance_locked(struct iwn_softc *sc)
{
    if (++sc->sc_mfp_pae_lifecycle_generation == 0)
        ++sc->sc_mfp_pae_lifecycle_generation;
}

/* A completed hardware init starts a fresh PMF lifetime.  Old workers may
 * still be unwinding, but their saved generation can no longer publish or
 * complete against this new one. */
static void
iwn_mfp_pae_reopen(struct iwn_softc *sc)
{
    if (sc == NULL || sc->sc_mfp_pae_lock == NULL)
        return;
    IOSimpleLockLock(sc->sc_mfp_pae_lock);
    if (!sc->sc_mfp_pae_detaching) {
        iwn_mfp_pae_generation_advance_locked(sc);
        sc->sc_mfp_pae_stopping = false;
    }
    IOSimpleLockUnlock(sc->sc_mfp_pae_lock);
}

/* Caller holds sc_mfp_pae_lock.  A worker keeps task_active set, so it alone
 * owns its local key while cancellation can take only non-worker records. */
static bool
iwn_mfp_pae_take_record_locked(struct iwn_mfp_pae_txn *txn,
                               struct iwn_mfp_pae_txn *taken)
{
    if (txn == NULL || taken == NULL || !txn->active || txn->task_active)
        return false;
    *taken = *txn;
    explicit_bzero(txn, sizeof(*txn));
    return true;
}

static void
iwn_mfp_pae_dispose_record(struct ieee80211com *ic,
                           struct iwn_mfp_pae_txn *txn)
{
    if (txn == NULL)
        return;
    iwn_mfp_pae_dispose_key(ic, &txn->ptk_key);
    iwn_mfp_pae_dispose_key(ic, &txn->gtk_key);
    iwn_mfp_pae_dispose_key(ic, &txn->pending_key);
    if (txn->ni != NULL)
        ieee80211_release_node(ic, txn->ni);
    explicit_bzero(txn, sizeof(*txn));
}

static bool
iwn_mfp_pae_record_matches(const struct iwn_mfp_pae_txn *txn,
                           u_int64_t txn_id, u_int64_t assoc_epoch,
                           u_int32_t generation,
                           const struct ieee80211_node *ni)
{
    return txn != NULL && txn->active && txn->txn_id == txn_id &&
        txn->assoc_epoch == assoc_epoch &&
        txn->lifecycle_generation == generation && txn->ni == ni;
}

/* Caller holds the selected-BSS leaf. */
static bool
iwn_mfp_pae_generic_stage_live_locked(struct ieee80211com *ic,
                                      u_int64_t txn_id,
                                      u_int64_t assoc_epoch,
                                      const struct ieee80211_node *ni,
                                      u_int8_t stage)
{
    const struct ieee80211_pae_mfp_txn *generic;

    if (ic == NULL || ni == NULL)
        return false;
    generic = &ic->ic_pae_mfp_txn;
    return generic->active && generic->id == txn_id &&
        generic->assoc_epoch == assoc_epoch && generic->ni == ni &&
        generic->phase == stage && ic->ic_bss == ni &&
        __atomic_load_n(&ic->ic_pae_assoc_epoch, __ATOMIC_ACQUIRE) ==
        assoc_epoch;
}

static bool
iwn_mfp_pae_cancel_record_locked(struct iwn_mfp_pae_txn *txn,
                                 struct iwn_mfp_pae_txn *taken)
{
    if (txn == NULL || !txn->active)
        return false;
    txn->cancelled = true;
    txn->pending = false;
    txn->pending_stage = IEEE80211_PAE_MFP_STAGE_NONE;
    explicit_bzero(&txn->pending_key, sizeof(txn->pending_key));
    return !txn->task_active && iwn_mfp_pae_take_record_locked(txn, taken);
}

/* Caller holds sc_mfp_pae_lock, has already retired the main record, and
 * supplies a separate cleanup destination for a stale pending successor. */
static bool
iwn_mfp_pae_promote_successor_locked(struct iwn_softc *sc,
                                     struct iwn_mfp_pae_txn *retired)
{
    struct iwn_mfp_pae_txn *successor;

    if (sc == NULL || sc->sc_mfp_pae_txn.active)
        return false;
    successor = &sc->sc_mfp_pae_successor;
    if (!successor->active)
        return false;
    /* A successor is deliberately non-worker-owned until it is promoted. */
    if (successor->task_active || successor->cancelled ||
        sc->sc_mfp_pae_detaching || sc->sc_mfp_pae_stopping ||
        successor->lifecycle_generation !=
        sc->sc_mfp_pae_lifecycle_generation) {
        if (retired != NULL)
            (void)iwn_mfp_pae_take_record_locked(successor, retired);
        return false;
    }
    sc->sc_mfp_pae_txn = *successor;
    explicit_bzero(successor, sizeof(*successor));
    return sc->sc_mfp_pae_txn.pending;
}

static bool
iwn_mfp_runtime_enabled(const struct iwn_softc *sc)
{
    return sc != NULL && sc->sc_mfp_pae_lock != NULL &&
        sc->sc_mfp_pae_task_ready && sc->sc_mfp_pae_runtime_enabled &&
        sc->sc_ic.ic_pae_selected_bss_lock != NULL;
}

/* Hook writers share the selected-BSS leaf with generic's callback snapshot.
 * The callback lease then protects a callback already copied before removal. */
static void
iwn_mfp_pae_publish_hooks(struct iwn_softc *sc, bool enabled)
{
    struct ieee80211com *ic;
    IOSimpleLock *lock;
    IOInterruptState irq;

    if (sc == NULL)
        return;
    ic = &sc->sc_ic;
    lock = ic->ic_pae_selected_bss_lock;
    if (lock != NULL)
        irq = IOSimpleLockLockDisableInterrupt(lock);
    if (!enabled) {
        ic->ic_caps &= ~IEEE80211_C_MFP;
        ic->ic_pae_mfp_requested = 0;
        ic->ic_pae_mfp_txn_submit = NULL;
        ic->ic_pae_mfp_txn_cancel = NULL;
        ic->ic_pae_mfp_txn_finish = NULL;
    } else {
        ic->ic_caps |= IEEE80211_C_MFP;
        ic->ic_set_key_wait = NULL;
        ic->ic_pae_mfp_txn_submit = ItlIwn::iwn_pae_mfp_txn_submit;
        ic->ic_pae_mfp_txn_cancel = ItlIwn::iwn_pae_mfp_txn_cancel;
        ic->ic_pae_mfp_txn_finish = ItlIwn::iwn_pae_mfp_txn_finish;
    }
    if (lock != NULL)
        IOSimpleLockUnlockEnableInterrupt(lock, irq);
}

/* Capability publication is intentionally independent of a particular SAE
 * request.  It admits only the completed software PMF owner; the product WCL
 * selected-BSS bridge decides whether an exact request may use it. */
static void
iwn_publish_mfp_capability(struct iwn_softc *sc)
{
    if (sc == NULL)
        return;
    if (!iwn_mfp_runtime_enabled(sc) || !iwn_mfp_pae_callback_open(sc)) {
        iwn_mfp_pae_callback_close(sc);
        iwn_mfp_pae_publish_hooks(sc, false);
        return;
    }
    iwn_mfp_pae_publish_hooks(sc, true);
}

#ifndef IWN_APGO_FIRMWARE_BACKEND_OPT_IN
#define IWN_APGO_FIRMWARE_BACKEND_OPT_IN 1
#endif

enum {
    IWN_AP_STAGE_IDLE = 0,
    IWN_AP_STAGE_INITIAL_RXON,
    IWN_AP_STAGE_TIMING,
    IWN_AP_STAGE_UNASSOCIATED_RXON,
    IWN_AP_STAGE_ADD_NODE,
    IWN_AP_STAGE_LINK_QUALITY,
    IWN_AP_STAGE_PAN_PARAMS,
    IWN_AP_STAGE_EDCA,
    IWN_AP_STAGE_FIRST_BEACON,
    IWN_AP_STAGE_ASSOCIATED_RXON,
    IWN_AP_STAGE_SENSITIVITY,
    IWN_AP_STAGE_TXPOWER,
    IWN_AP_STAGE_POWER,
    IWN_AP_STAGE_SECOND_BEACON,
    IWN_AP_STAGE_POST_ASSOC_EDCA,
    IWN_AP_STAGE_THIRD_BEACON,
    IWN_AP_STAGE_FINAL_RXON_ASSOC,
    IWN_AP_STAGE_FINAL_POWER,
    IWN_AP_STAGE_FINAL_PAN_PARAMS,
    IWN_AP_STAGE_RUNNING,
    IWN_AP_STAGE_STOP_TX_FLUSH,
    IWN_AP_STAGE_STOP_TX_RETIRE,
    IWN_AP_STAGE_STOP_RXON,
    IWN_AP_STAGE_STOP_PAN_PARAMS
};

enum {
    /* A complete unassociated DVM scan can span both bands.  HostAP owns
     * the next radio epoch, but must allow the exact firmware STOP_SCAN
     * terminal enough time to retire instead of exposing a transient
     * two-second timeout to Internet Sharing. */
    IWN_AP_TRANSITION_SCAN_WAIT_SECONDS = 8
};

enum {
    IWN_AP_RSN_DISABLED = 0,
    IWN_AP_RSN_WAIT_M2,
    IWN_AP_RSN_WAIT_M4,
    IWN_AP_RSN_AUTHORIZED
};

enum {
    IWN_AP_CLIENT_MATERIALIZATION_IDLE = 0,
    IWN_AP_CLIENT_MATERIALIZATION_ADD_NODE,
    IWN_AP_CLIENT_MATERIALIZATION_WAKE_NODE,
    IWN_AP_CLIENT_MATERIALIZATION_LINK_QUALITY
};

enum {
    IWN_AP_CSA_CLIENT_RESTORE_IDLE = 0,
    IWN_AP_CSA_CLIENT_RESTORE_PREPARED,
    IWN_AP_CSA_CLIENT_RESTORE_ADD_NODE,
    IWN_AP_CSA_CLIENT_RESTORE_LINK_QUALITY,
    IWN_AP_CSA_CLIENT_RESTORE_GROUP_KEY,
    IWN_AP_CSA_CLIENT_RESTORE_PAIRWISE_KEY
};

enum {
    IWN_AP_AUTH_UPPER_WPA3_SAE = 0x1000,
    IWN_AP_IGTK_KDE_TYPE = 9,
    IWN_AP_IGTK_KEY_ID = 4,
    IWN_AP_SAE_COMMIT_TRANSACTION = 1,
    IWN_AP_SAE_CONFIRM_TRANSACTION = 2,
    IWN_AP_STATUS_INVALID_PMKID = 53
};

/* Keep the proven single-client state machine readable while making its
 * storage genuinely per-client.  Every entry point that consumes a peer MAC,
 * station ID, or TX queue selects the corresponding slot first. */
#define apClientMac                apClientContext->mac
#define apClientNodeInstalled      apClientContext->nodeInstalled
#define apClientMaterializationStage apClientContext->materializationStage
#define apClientAuthenticated      apClientContext->authenticated
#define apClientReassociationPending apClientContext->reassociationPending
#define apClientLegacyRateMask     apClientContext->legacyRateMask
#define apClientQos                apClientContext->qos
#define apClientHt                 apClientContext->ht
#define apClientHtNss              apClientContext->htNss
#define apClientHtCapabilities     apClientContext->htCapabilities
#define apClientHtAmpduParams      apClientContext->htAmpduParams
#define apClientHtMcs              apClientContext->htMcs
#define apClientRateControl        apClientContext->rateControl
#define apClientRxBaMask           apClientContext->rxBaMask
#define apClientRxBa               apClientContext->rxBa
#define apClientTxBaMask           apClientContext->txBaMask
#define apClientDisableTid         apClientContext->disableTid
#define apClientTxBaEnablePending  apClientContext->txBaEnablePending
#define apClientTxBaPendingTid     apClientContext->txBaPendingTid
#define apClientTxBaPendingQueue   apClientContext->txBaPendingQueue
#define apClientTxBaPendingSsn     apClientContext->txBaPendingSsn
#define apClientTxBaPendingOldDisableTid apClientContext->txBaPendingOldDisableTid
#define apClientTxDialogToken      apClientContext->txDialogToken
#define apClientTxBaQueue          apClientContext->txBaQueue
#define apClientTxSequence         apClientContext->txSequence
#define apClientTxBa               apClientContext->txBa
#define apClientAssociated         apClientContext->associated
#define apClientAuthorized         apClientContext->authorized
#define apClientPowerSave          apClientContext->powerSave
#define apClientAid                apClientContext->aid
#define apRsnState                 apClientContext->rsnState
#define apClientRsnIE              apClientContext->rsnIE
#define apPmk                      apClientContext->pmk
#define apAnonce                   apClientContext->anonce
#define apPtk                      apClientContext->ptk
#define apPairwiseSoftwareKey      apClientContext->pairwiseSoftwareKey
#define apSoftwareCcmpRxObserved   apClientContext->softwareCcmpRxObserved
#define apReplayCounter            apClientContext->replayCounter
#define apPairwiseTxPn             apClientContext->pairwiseTxPn
#define apPairwiseRxPn             apClientContext->pairwiseRxPn
#define apClientRsnIELength        apClientContext->rsnIELength
#define apSae                      apClientContext->sae
#define apSaePmksaPmk              apClientContext->saePmksaPmk
#define apSaePmksaPmkid            apClientContext->saePmksaPmkid
#define apSaePmksaSta              apClientContext->saePmksaSta
#define apSaePmksaBssid            apClientContext->saePmksaBssid
#define apSaePmksaValid            apClientContext->saePmksaValid
#define apClientOpenAuthenticated  apClientContext->openAuthenticated
#define apPsQueue                  apClientContext->psQueue
#define apPsQueueHead              apClientContext->psQueueHead
#define apPsQueueTail              apClientContext->psQueueTail
#define apPsQueueCount             apClientContext->psQueueCount
#define apPsQueueReady             apClientContext->psQueueReady
#define apTimSet                   apClientContext->timSet

#ifdef DELAY
#undef DELAY
#define DELAY IODelay
#endif

void ItlIwn::iwn_select_ap_client(struct IwnApClientRuntime *client)
{
    if (client != NULL)
        apClientContext = client;
}

static bool iwn_ap_dvm_mcs_supported(
    const struct IwnApClientRuntime *client, int mcs)
{
    if (client == NULL || !client->ht || mcs < 0 ||
        mcs >= IWN_AP_RATE_MCS_COUNT)
        return false;
    const bool mimo = client->htNss > 1;
    if ((mimo && mcs < 8) || (!mimo && mcs >= 8))
        return false;
    return (client->htMcs[mcs / 8] & (1U << (mcs & 7))) != 0;
}

static void iwn_ap_dvm_clear_rate_windows(
    struct IwnApRateControlRuntime *rateControl)
{
    if (rateControl == NULL)
        return;
    bzero(rateControl->windows, sizeof(rateControl->windows));
    for (size_t index = 0; index < IWN_AP_RATE_MCS_COUNT; index++)
        rateControl->windows[index].averageThroughput = -1;
}

static uint16_t iwn_ap_dvm_expected_throughput(
    const struct IwnApClientRuntime *client, uint8_t mcs,
    bool aggregated)
{
    /* Exact DVM HT20 throughput tables, indexed as Normal, SGI, AGG,
     * AGG+SGI.  IWN HostAP currently negotiates HT20 only. */
    static const uint16_t siso20[4][8] = {
        { 42, 76, 102, 124, 159, 183, 193, 202 },
        { 46, 82, 110, 132, 168, 192, 202, 210 },
        { 47, 91, 133, 171, 242, 305, 334, 362 },
        { 52, 101, 145, 187, 264, 330, 361, 390 }
    };
    static const uint16_t mimo20[4][8] = {
        { 74, 123, 155, 179, 214, 236, 244, 251 },
        { 81, 131, 164, 188, 223, 243, 251, 257 },
        { 89, 167, 235, 296, 402, 488, 526, 560 },
        { 97, 182, 255, 320, 431, 520, 558, 593 }
    };
    const bool sgi = client != NULL &&
        (client->htCapabilities & IEEE80211_HTCAP_SGI20) != 0;
    const size_t row = (aggregated ? 2U : 0U) + (sgi ? 1U : 0U);
    return client != NULL && client->htNss > 1 ?
        mimo20[row][mcs & 7] : siso20[row][mcs & 7];
}

static void iwn_ap_dvm_collect_rate_window(
    struct IwnApRateWindow *window, uint16_t attempts,
    uint16_t successes, uint16_t expectedThroughput)
{
    if (window == NULL || attempts == 0)
        return;
    if (successes > attempts)
        successes = attempts;
    const uint64_t oldest = 1ULL << (IWN_AP_RATE_WINDOW_SIZE - 1);
    while (attempts != 0) {
        if (window->attempts >= IWN_AP_RATE_WINDOW_SIZE) {
            window->attempts = IWN_AP_RATE_WINDOW_SIZE - 1;
            if ((window->successHistory & oldest) != 0) {
                window->successHistory &= ~oldest;
                if (window->successes != 0)
                    window->successes--;
            }
        }
        window->attempts++;
        window->successHistory <<= 1;
        if (successes != 0) {
            window->successes++;
            window->successHistory |= 1;
            successes--;
        }
        attempts--;
    }
    window->successRatio = static_cast<uint16_t>(
        128U * (100U * window->successes) / window->attempts);
    const uint8_t failures =
        static_cast<uint8_t>(window->attempts - window->successes);
    if (failures >= 6 || window->successes >= 8) {
        window->averageThroughput = static_cast<int32_t>(
            (window->successRatio * expectedThroughput + 64U) / 128U);
    } else {
        window->averageThroughput = -1;
    }
}

static void iwn_ap_dvm_selected_rate_sample(
    uint8_t ackfailcnt, int txfail, uint16_t *attempts,
    uint16_t *successes)
{
    /*
     * DVM exposes failure_frame + 1 attempts to rs_tx_status(), which then
     * walks the Link Quality retry table one entry at a time.  Our table has
     * the selected HT rate in entries 0..2, so count every attempt which
     * actually used that rate.  A successful final attempt belongs to the
     * selected-rate window only while it is still inside those three entries;
     * after that it succeeded at the lower HT or legacy fallback rate.
     */
    const uint16_t totalAttempts =
        static_cast<uint16_t>(ackfailcnt) + 1U;
    *attempts = MIN(totalAttempts, static_cast<uint16_t>(3));
    *successes = !txfail && totalAttempts <= 3U ? 1U : 0U;
}

void ItlIwn::iwn_reset_ap_client_rate_control(
    struct IwnApClientRuntime *client)
{
    if (client == NULL)
        return;
    bzero(&client->rateControl, sizeof(client->rateControl));
    client->rateControl.selectedMcs = UINT8_MAX;
    client->rateControl.generation = 1;
    iwn_ap_dvm_clear_rate_windows(&client->rateControl);
}

struct IwnApClientRuntime *ItlIwn::iwn_find_ap_client(
    const uint8_t *station)
{
    if (station == NULL)
        return NULL;
    for (size_t index = 0; index < kItlApFirmwareMaxClients; index++) {
        struct IwnApClientRuntime *client = &apClients[index];
        if (client->inUse && IEEE80211_ADDR_EQ(client->mac, station))
            return client;
    }
    return NULL;
}

struct IwnApClientRuntime *ItlIwn::iwn_find_ap_client_by_id(
    uint8_t stationId)
{
    for (size_t index = 0; index < kItlApFirmwareMaxClients; index++) {
        struct IwnApClientRuntime *client = &apClients[index];
        if (client->inUse && client->stationId == stationId)
            return client;
    }
    return NULL;
}

struct IwnApClientRuntime *ItlIwn::iwn_first_ap_client(
    bool requireAssociated)
{
    for (size_t index = 0; index < kItlApFirmwareMaxClients; index++) {
        struct IwnApClientRuntime *client = &apClients[index];
        if (client->inUse && (!requireAssociated || client->associated))
            return client;
    }
    return NULL;
}

void ItlIwn::iwn_reset_ap_client(
    struct IwnApClientRuntime *client, bool releaseSlot,
    bool preserveSaePmksa)
{
    if (client == NULL)
        return;
    const uint8_t stationId = client->stationId;
    uint8_t cachedPmk[IEEE80211_PMK_LEN];
    uint8_t cachedPmkid[IEEE80211_PMKID_LEN];
    uint8_t cachedSta[IEEE80211_ADDR_LEN];
    uint8_t cachedBssid[IEEE80211_ADDR_LEN];
    const bool cached = preserveSaePmksa && client->saePmksaValid;
    if (cached) {
        memcpy(cachedPmk, client->saePmksaPmk, sizeof(cachedPmk));
        memcpy(cachedPmkid, client->saePmksaPmkid,
               sizeof(cachedPmkid));
        IEEE80211_ADDR_COPY(cachedSta, client->saePmksaSta);
        IEEE80211_ADDR_COPY(cachedBssid, client->saePmksaBssid);
    }
    if (client->pairwiseSoftwareKey.k_priv != NULL)
        ieee80211_ccmp_delete_key(
            &com.sc_ic, &client->pairwiseSoftwareKey);
    ieee80211_sae_ap_destroy(&client->sae);
    while (client->psQueueCount != 0) {
        mbuf_t packet = client->psQueue[client->psQueueHead];
        client->psQueue[client->psQueueHead] = NULL;
        client->psQueueHead = static_cast<uint8_t>(
            (client->psQueueHead + 1) % IWN_AP_PS_QUEUE_LEN);
        client->psQueueCount--;
        if (packet != NULL)
            mbuf_freem(packet);
    }
    for (size_t tid = 0; tid < kItlApRxBaTidCount; tid++) {
        itl_ap_rx_ba_stop(&client->rxBa[tid]);
        itl_ap_tx_ba_reset(&client->txBa[tid]);
    }
    explicit_bzero(client, sizeof(*client));
    client->stationId = releaseSlot ? UINT8_MAX : stationId;
    client->txBaPendingTid = UINT8_MAX;
    client->txBaPendingQueue = UINT8_MAX;
    for (size_t tid = 0; tid < kItlApRxBaTidCount; tid++)
        client->txBaQueue[tid] = UINT8_MAX;
    client->psQueueReady = true;
    if (cached) {
        memcpy(client->saePmksaPmk, cachedPmk,
               sizeof(client->saePmksaPmk));
        memcpy(client->saePmksaPmkid, cachedPmkid,
               sizeof(client->saePmksaPmkid));
        IEEE80211_ADDR_COPY(client->saePmksaSta, cachedSta);
        IEEE80211_ADDR_COPY(client->saePmksaBssid, cachedBssid);
        client->saePmksaValid = true;
        explicit_bzero(cachedPmk, sizeof(cachedPmk));
        explicit_bzero(cachedPmkid, sizeof(cachedPmkid));
    }
}

struct IwnApClientRuntime *ItlIwn::iwn_allocate_ap_client(
    const uint8_t *station)
{
    struct IwnApClientRuntime *client = iwn_find_ap_client(station);
    if (client != NULL)
        return client;
    if (station == NULL)
        return NULL;
    const size_t limit = MIN(
        static_cast<size_t>(apMaxStations),
        static_cast<size_t>(kItlApFirmwareMaxClients));
    struct IwnApClientRuntime *cachedClient = NULL;
    for (size_t index = 0; index < kItlApFirmwareMaxClients; index++) {
        struct IwnApClientRuntime *candidate = &apClients[index];
        if (!candidate->inUse && candidate->saePmksaValid &&
            IEEE80211_ADDR_EQ(candidate->saePmksaSta, station) &&
            IEEE80211_ADDR_EQ(
                candidate->saePmksaBssid, apFirmwareConfig.bssid)) {
            cachedClient = candidate;
            break;
        }
    }
    size_t freeIndex = limit;
    for (size_t index = 0; index < limit; index++) {
        if (!apClients[index].inUse &&
            !apClients[index].saePmksaValid) {
            freeIndex = index;
            break;
        }
    }
    if (freeIndex == limit) {
        for (size_t index = 0; index < limit; index++) {
            if (!apClients[index].inUse) {
                freeIndex = index;
                break;
            }
        }
    }
    if (freeIndex < limit) {
        client = &apClients[freeIndex];
        uint8_t cachedPmk[IEEE80211_PMK_LEN];
        uint8_t cachedPmkid[IEEE80211_PMKID_LEN];
        const bool importCache = cachedClient != NULL;
        if (importCache) {
            memcpy(cachedPmk, cachedClient->saePmksaPmk,
                   sizeof(cachedPmk));
            memcpy(cachedPmkid, cachedClient->saePmksaPmkid,
                   sizeof(cachedPmkid));
        }
        iwn_reset_ap_client(client, true);
        client->inUse = true;
        client->stationId = static_cast<uint8_t>(
            IWN5000_ID_PAN_CLIENT + freeIndex);
        client->aid = static_cast<uint16_t>(freeIndex + 1);
        client->rsnState = apFirmwareConfig.rsnIELength == 0 ?
            IWN_AP_RSN_AUTHORIZED : IWN_AP_RSN_DISABLED;
        if (!iwn_ap_uses_sae())
            memcpy(client->pmk, apProfilePmk, sizeof(client->pmk));
        IEEE80211_ADDR_COPY(client->mac, station);
        if (importCache) {
            memcpy(client->saePmksaPmk, cachedPmk,
                   sizeof(client->saePmksaPmk));
            memcpy(client->saePmksaPmkid, cachedPmkid,
                   sizeof(client->saePmksaPmkid));
            IEEE80211_ADDR_COPY(client->saePmksaSta, station);
            IEEE80211_ADDR_COPY(
                client->saePmksaBssid, apFirmwareConfig.bssid);
            client->saePmksaValid = true;
            if (cachedClient != client) {
                explicit_bzero(cachedClient->saePmksaPmk,
                               sizeof(cachedClient->saePmksaPmk));
                explicit_bzero(cachedClient->saePmksaPmkid,
                               sizeof(cachedClient->saePmksaPmkid));
                bzero(cachedClient->saePmksaSta,
                      sizeof(cachedClient->saePmksaSta));
                bzero(cachedClient->saePmksaBssid,
                      sizeof(cachedClient->saePmksaBssid));
                cachedClient->saePmksaValid = false;
            }
            explicit_bzero(cachedPmk, sizeof(cachedPmk));
            explicit_bzero(cachedPmkid, sizeof(cachedPmkid));
        }
        return client;
    }
    return NULL;
}

int ItlIwn::iwn_submit_next_ap_client_materialization()
{
    for (size_t index = 0; index < kItlApFirmwareMaxClients; index++) {
        if (apClients[index].inUse && apClients[index].commandPending)
            return 0;
    }
    for (size_t index = 0; index < kItlApFirmwareMaxClients; index++) {
        struct IwnApClientRuntime *client = &apClients[index];
        if (!client->inUse || client->commandPending ||
            (client->materializationStage !=
                 IWN_AP_CLIENT_MATERIALIZATION_ADD_NODE &&
             client->materializationStage !=
                 IWN_AP_CLIENT_MATERIALIZATION_WAKE_NODE))
            continue;
        iwn_select_ap_client(client);
        const int error = client->materializationStage ==
            IWN_AP_CLIENT_MATERIALIZATION_ADD_NODE ?
            iwn_add_ap_client_node(client->mac) :
            iwn_update_ap_client_node();
        if (error != 0) {
            client->materializationStage =
                IWN_AP_CLIENT_MATERIALIZATION_IDLE;
            client->reassociationPending = false;
            return error;
        }
        client->commandPending = true;
        return 0;
    }
    return 0;
}

bool ItlIwn::attach(IOPCIDevice *device)
{
    /* iwn_attach() may fail after publishing an event source; detach owns it. */
    com.sc_scan_lease_lock = NULL;
    com.sc_ap_transition_scan_blocked = false;
    fSaeTxGate = NULL;
    bzero(apClients, sizeof(apClients));
    apClientContext = &apClients[0];
    apMaxStations = kItlApFirmwareMaxClients;
    for (size_t index = 0; index < kItlApFirmwareMaxClients; index++)
        iwn_reset_ap_client(&apClients[index], true);
    explicit_bzero(apProfilePmk, sizeof(apProfilePmk));
    apCsaTimeout = NULL;
    apCsaTimerInitialized = false;
    timeout_set(&apCsaTimeout, iwn_ap_csa_timeout, this);
    apCsaTimerInitialized = true;
    iwn_reset_ap_runtime_state();
    pci.pa_tag = device;
    pci.workloop = getMainWorkLoop();
    if (!iwn_attach(&com, &pci)) {
        detach(device);
        releaseAll();
        return false;
    }
    /*
     * Tahoe must expose a supported AP identity before the primary BSD
     * interface is discovered.  Reading the embedded image is sufficient to
     * establish the real TLV capability; do not boot firmware or start radio
     * service here.  IWM/IWX already discover firmware/NVM during attach.
     * A failed preview leaves AP unsupported and preserves the ordinary STA
     * power-on retry, whose normal firmware reader must succeed independently.
     */
    const int capabilityError = iwn_prepare_firmware_capabilities(&com);
    XYLog("iwn: attach firmware capability result=%d PAN=%u\n",
          capabilityError, supportsAPMode() ? 1U : 0U);
    return true;
}

bool ItlIwn::iwn_ap_uses_sae() const
{
    return apFirmwareConfig.authUpper == IWN_AP_AUTH_UPPER_WPA3_SAE;
}

void ItlIwn::iwn_reset_ap_sae()
{
    ieee80211_sae_ap_destroy(&apSae);
}

void ItlIwn::iwn_clear_ap_sae_pmksa()
{
    explicit_bzero(apSaePmksaPmk, sizeof(apSaePmksaPmk));
    explicit_bzero(apSaePmksaPmkid, sizeof(apSaePmksaPmkid));
    bzero(apSaePmksaSta, sizeof(apSaePmksaSta));
    bzero(apSaePmksaBssid, sizeof(apSaePmksaBssid));
    apSaePmksaValid = false;
}

bool ItlIwn::iwn_ap_sae_pmksa_matches(
    const uint8_t *station, const uint8_t *pmkid) const
{
    return apSaePmksaValid && station != NULL && pmkid != NULL &&
        IEEE80211_ADDR_EQ(apSaePmksaSta, station) &&
        IEEE80211_ADDR_EQ(apSaePmksaBssid, apFirmwareConfig.bssid) &&
        timingsafe_bcmp(
            apSaePmksaPmkid, pmkid, sizeof(apSaePmksaPmkid)) == 0;
}

int ItlIwn::iwn_prepare_ap_client_reauthentication(
    bool preserveSaePmksa)
{
    if (apClientContext == NULL || !apClientContext->inUse)
        return EINVAL;
    if (apTimSet) {
        const int timError = iwn_update_ap_tim(false);
        if (timError != 0)
            return timError;
    }
    iwn_purge_ap_ps_queue();
    if (apClientNodeInstalled) {
        iwn_stop_all_ap_client_tx_ba();
        iwn_stop_all_ap_client_rx_ba();
        const int removeError = iwn_remove_ap_client_node(apClientMac);
        if (removeError != 0)
            return removeError;
    } else {
        for (size_t tid = 0; tid < kItlApRxBaTidCount; tid++) {
            itl_ap_rx_ba_stop(&apClientRxBa[tid]);
            itl_ap_tx_ba_reset(&apClientTxBa[tid]);
            apClientTxBaQueue[tid] = UINT8_MAX;
        }
    }
    if (apPairwiseSoftwareKey.k_priv != NULL)
        ieee80211_ccmp_delete_key(
            &com.sc_ic, &apPairwiseSoftwareKey);
    explicit_bzero(&apPairwiseSoftwareKey,
                   sizeof(apPairwiseSoftwareKey));
    iwn_reset_ap_sae();
    if (!preserveSaePmksa)
        iwn_clear_ap_sae_pmksa();

    apClientNodeInstalled = false;
    apClientMaterializationStage =
        IWN_AP_CLIENT_MATERIALIZATION_IDLE;
    apClientContext->commandPending = false;
    apClientAuthenticated = false;
    apClientOpenAuthenticated = false;
    apClientReassociationPending = false;
    apClientLegacyRateMask = 0;
    apClientQos = false;
    apClientHt = false;
    apClientHtNss = 0;
    apClientHtCapabilities = 0;
    apClientHtAmpduParams = 0;
    bzero(apClientHtMcs, sizeof(apClientHtMcs));
    iwn_reset_ap_client_rate_control(apClientContext);
    apClientRxBaMask = 0;
    apClientTxBaMask = 0;
    apClientDisableTid = 0;
    apClientTxBaEnablePending = false;
    apClientTxBaPendingTid = UINT8_MAX;
    apClientTxBaPendingQueue = UINT8_MAX;
    apClientTxBaPendingSsn = 0;
    apClientTxBaPendingOldDisableTid = 0;
    bzero(apClientTxSequence, sizeof(apClientTxSequence));
    apClientAssociated = false;
    apClientAuthorized = false;
    apClientPowerSave = false;
    apRsnState = apFirmwareConfig.rsnIELength == 0 ?
        IWN_AP_RSN_AUTHORIZED : IWN_AP_RSN_DISABLED;
    apClientRsnIELength = 0;
    bzero(apClientRsnIE, sizeof(apClientRsnIE));
    explicit_bzero(apPmk, sizeof(apPmk));
    if (!iwn_ap_uses_sae())
        memcpy(apPmk, apProfilePmk, sizeof(apPmk));
    explicit_bzero(apAnonce, sizeof(apAnonce));
    explicit_bzero(&apPtk, sizeof(apPtk));
    apSoftwareCcmpRxObserved = false;
    apReplayCounter = 0;
    apPairwiseTxPn = 0;
    bzero(apPairwiseRxPn, sizeof(apPairwiseRxPn));
    apTimSet = false;
    return 0;
}

void ItlIwn::iwn_reset_ap_runtime_state()
{
    if (apCsaTimerInitialized)
        timeout_del(&apCsaTimeout);
    apCsaPending = false;
    apCsaTargetChannel = 0;
    apCsaMode = 0;
    apCsaCount = 0;
    apCsaClientRestoreStage = IWN_AP_CSA_CLIENT_RESTORE_IDLE;
    apCsaRestoreIndex = 0;
    apCsaGroupKeyRestored = false;
    iwn_set_ap_scan_transition_blocked(false);
    for (size_t index = 0; index < kItlApFirmwareMaxClients; index++)
        iwn_reset_ap_client(&apClients[index], true, true);
    apClientContext = &apClients[0];
    apMaxStations = kItlApFirmwareMaxClients;
    apFirmwareTransitionActive = false;
    apFirmwareDeactivationReplySeen = false;
    apFirmwareDeactivationNotificationSeen = false;
    apFirmwarePostDeactivateQueued = false;
    apFirmwareUnassociatedReplySeen = false;
    apFirmwareUnassociatedNotificationSeen = false;
    apStaScanPriorityActive = false;
    apStaAuthPriorityActive = false;
    apStaBssAssociated = false;
    apStaRunPanFencePending = false;
    apStaRunPanFenceIndex = 0;
    apStopTxFlushIndex = 0;
    apStopTxQueueMask = 0;
    iwn_set_ap_primary_tx_quiesced(false, false);
    apFirmwareStage = IWN_AP_STAGE_IDLE;
    bzero(&apFirmwareConfig, sizeof(apFirmwareConfig));
    bzero(&apFirmwareRxon, sizeof(apFirmwareRxon));
    bzero(apFirmwareSsid, sizeof(apFirmwareSsid));
    bzero(apFirmwareCredential, sizeof(apFirmwareCredential));
    bzero(apFirmwareRsnIE, sizeof(apFirmwareRsnIE));
    bzero(apFirmwareBeacon, sizeof(apFirmwareBeacon));
    explicit_bzero(apProfilePmk, sizeof(apProfilePmk));
    explicit_bzero(apGtk, sizeof(apGtk));
    explicit_bzero(apIgtk, sizeof(apIgtk));
    apGroupTxPn = 0;
    apGtkKid = 1;
    apIgtkKid = IWN_AP_IGTK_KEY_ID;
    apHidden = false;
}

void ItlIwn::iwn_set_ap_scan_transition_blocked(bool blocked)
{
    struct iwn_softc *sc = &com;

    if (sc->sc_scan_lease_lock == NULL) {
        sc->sc_ap_transition_scan_blocked = blocked;
        return;
    }
    IOSimpleLockLock(sc->sc_scan_lease_lock);
    sc->sc_ap_transition_scan_blocked = blocked;
    IOSimpleLockUnlock(sc->sc_scan_lease_lock);
}

void ItlIwn::iwn_set_ap_primary_tx_quiesced(
    bool quiesced, bool resumeOutput)
{
    struct _ifnet *ifp = &com.sc_ic.ic_ac.ac_if;

    apPrimaryTxQuiesced = quiesced;
    if (quiesced) {
        ifq_set_oactive(&ifp->if_snd);
        return;
    }

    if (ifq_is_oactive(&ifp->if_snd))
        ifq_clr_oactive(&ifp->if_snd);
    if (resumeOutput &&
        (ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
            (IFF_UP | IFF_RUNNING) &&
        ifp->if_start != NULL) {
        (*ifp->if_start)(ifp);
    }
}

bool ItlIwn::iwn_ap_primary_tx_pending() const
{
    for (int qid = 0; qid < com.ntxqs; qid++) {
        /*
         * Host commands must remain live while the AP transition is
         * serialized.  Every data/mgmt queue, including an old PAN queue,
         * must reach its native TX_DONE/reset boundary first.
         */
        if (qid != com.command_queue && com.txq[qid].queued != 0)
            return true;
    }
    return false;
}

IOReturn ItlIwn::iwn_quiesce_scan_for_ap_transition()
{
    struct iwn_softc *sc = &com;
    u_int64_t serial = 0;
    bool submitAbort = false;
    bool scanOwned = false;

    if (sc->sc_scan_lease_lock == NULL)
        return kIOReturnNotReady;

    /*
     * AppleBCMWLAN's recovered HostAP preamble disables BG-scan private-MAC
     * programming before issuing the AP firmware commands and does not
     * reject HostAP merely because a scan is active. DVM has a stricter
     * hardware boundary: WIPAN_RXON must not cross an active SCAN command.
     * Make HostAP the next radio owner, abort the exact current lease, and
     * wait for its native STOP_SCAN terminal instead of returning Busy to
     * CoreWLAN or inventing a timer retry.
     */
    IOSimpleLockLock(sc->sc_scan_lease_lock);
    sc->sc_ap_transition_scan_blocked = true;
    scanOwned = iwn_scan_lease_live_locked(sc);
    const bool scanning =
        scanOwned || (sc->sc_flags & IWN_FLAG_SCANNING) != 0;
    IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    if (!scanning)
        return kIOReturnSuccess;

    if (!scanOwned ||
        !iwn_scan_lease_mark_abort(sc, IWN_SCAN_LEASE_NONE, 0,
                                   &serial, &submitAbort)) {
        iwn_set_ap_scan_transition_blocked(false);
        XYLog("%s: AP transition cannot claim active scan owner\n",
              sc->sc_dev.dv_xname);
        return kIOReturnBusy;
    }
    if (submitAbort &&
        iwn_cmd(sc, IWN_CMD_SCAN_ABORT, NULL, 0, 1) != 0) {
        iwn_scan_lease_abort_submission_failed(sc, serial);
        iwn_set_ap_scan_transition_blocked(false);
        XYLog("%s: AP transition scan abort submission failed\n",
              sc->sc_dev.dv_xname);
        return kIOReturnError;
    }

    IOCommandGate *gate = getMainCommandGate();
    IOWorkLoop *workLoop = getMainWorkLoop();
    if (gate == NULL || workLoop == NULL || !workLoop->inGate()) {
        iwn_set_ap_scan_transition_blocked(false);
        return kIOReturnNotReady;
    }

    AbsoluteTime deadline;
    clock_interval_to_deadline(
        IWN_AP_TRANSITION_SCAN_WAIT_SECONDS, kSecondScale,
        reinterpret_cast<uint64_t *>(&deadline));
    IOReturn sleepResult = THREAD_AWAKENED;
    while ((sc->sc_flags & IWN_FLAG_SCANNING) != 0) {
        if (sleepResult == THREAD_TIMED_OUT)
            break;
        sleepResult = gate->commandSleep(
            &sc->sc_ap_transition_scan_blocked,
            deadline, THREAD_ABORTSAFE);
        if (sleepResult != THREAD_AWAKENED &&
            sleepResult != THREAD_TIMED_OUT)
            break;
    }
    const bool scanStopped =
        (sc->sc_flags & IWN_FLAG_SCANNING) == 0;

    if (!scanStopped) {
        iwn_set_ap_scan_transition_blocked(false);
        XYLog("%s: AP transition scan terminal wait result=%d\n",
              sc->sc_dev.dv_xname, sleepResult);
        return sleepResult == THREAD_TIMED_OUT ?
            kIOReturnTimeout : kIOReturnAborted;
    }
    XYLog("%s: AP transition owns radio after scan terminal\n",
          sc->sc_dev.dv_xname);
    return kIOReturnSuccess;
}

void ItlIwn::iwn_purge_ap_ps_queue()
{
    if (!apPsQueueReady)
        return;
    while (apPsQueueCount != 0) {
        mbuf_t packet = apPsQueue[apPsQueueHead];
        apPsQueue[apPsQueueHead] = NULL;
        apPsQueueHead =
            (apPsQueueHead + 1) % IWN_AP_PS_QUEUE_LEN;
        apPsQueueCount--;
        if (packet != NULL)
            mbuf_freem(packet);
    }
    bzero(apPsQueue, sizeof(apPsQueue));
    apPsQueueHead = 0;
    apPsQueueTail = 0;
}

int ItlIwn::iwn_install_ap_ccmp_key(bool pairwise, uint8_t keyId,
    const uint8_t *key)
{
    if (key == NULL || !apClientNodeInstalled)
        return EINVAL;

    struct iwn_node_info node;
    bzero(&node, sizeof(node));
    /*
     * DVM's iwlagn_send_sta_key() starts from the complete descriptor saved
     * when the station was added, then overlays MODIFY/SET_KEY.  Preserve
     * the same station identity and PAN role here.  A sparse descriptor can
     * receive ADD_STA_SUCCESS while cold 6x35 firmware uploads the key but
     * does not link it into the PAN station's RX key map.
     */
    if (pairwise)
        IEEE80211_ADDR_COPY(node.macaddr, apClientMac);
    else
        IEEE80211_ADDR_COPY(node.macaddr, etherbroadcastaddr);
    node.control = IWN_NODE_UPDATE;
    node.id = pairwise ?
        apClientContext->stationId : IWN5000_ID_PAN_BROADCAST;
    node.flags = IWN_FLAG_SET_KEY;
    uint16_t keyFlags =
        IWN_KFLAG_CCMP | IWN_KFLAG_MAP | IWN_KFLAG_KID(keyId);
    if (!pairwise)
        keyFlags |= IWN_KFLAG_GROUP;
    node.kflags = htole16(keyFlags);
    node.kid = keyId;
    memcpy(node.key, key, sizeof(node.key));
    node.htflags = htole32(IWN_PAN_STATION);
    return com.ops.add_node(&com, &node, 1);
}

int ItlIwn::iwn_send_ap_eapol_key(const void *eapol, size_t eapolLength)
{
    if (eapol == NULL || eapolLength < sizeof(struct ieee80211_eapol_key) ||
        eapolLength > MCLBYTES - ETHER_HDR_LEN ||
        !apClientAssociated) {
        return EINVAL;
    }

    const size_t ethernetLength = ETHER_HDR_LEN + eapolLength;
    unsigned int maxChunks = 1;
    mbuf_t packet = NULL;
    if (mbuf_allocpacket(MBUF_DONTWAIT, ethernetLength,
            &maxChunks, &packet) != 0 || packet == NULL) {
        return ENOMEM;
    }
    mbuf_setlen(packet, ethernetLength);
    mbuf_pkthdr_setlen(packet, ethernetLength);
    struct ether_header *ethernetHeader =
        mtod(packet, struct ether_header *);
    IEEE80211_ADDR_COPY(ethernetHeader->ether_dhost, apClientMac);
    IEEE80211_ADDR_COPY(ethernetHeader->ether_shost,
                        apFirmwareConfig.bssid);
    ethernetHeader->ether_type = htons(ETHERTYPE_PAE);
    memcpy(reinterpret_cast<uint8_t *>(ethernetHeader) + ETHER_HDR_LEN,
           eapol, eapolLength);

    const int error = iwn_send_ap_data_frame(packet);
    if (error != 0)
        mbuf_freem(packet);
    return error;
}

int ItlIwn::iwn_send_ap_4way_msg1()
{
    uint8_t frame[
        sizeof(struct ieee80211_eapol_key) +
        2 + 4 + IEEE80211_PMKID_LEN];
    bzero(frame, sizeof(frame));
    struct ieee80211_eapol_key *key =
        reinterpret_cast<struct ieee80211_eapol_key *>(frame);
    key->version = EAPOL_VERSION;
    key->type = EAPOL_KEY;
    key->desc = EAPOL_KEY_DESC_IEEE80211;
    const uint16_t descriptor = iwn_ap_uses_sae() ?
        EAPOL_KEY_DESC_AKM_DEFINED : EAPOL_KEY_DESC_V2;
    BE_WRITE_2(key->info,
        EAPOL_KEY_PAIRWISE | EAPOL_KEY_KEYACK | descriptor);
    BE_WRITE_2(key->keylen, 16);
    apReplayCounter++;
    BE_WRITE_8(key->replaycnt, apReplayCounter);
    memcpy(key->nonce, apAnonce, sizeof(key->nonce));
    uint8_t *cursor = reinterpret_cast<uint8_t *>(key + 1);
    if (iwn_ap_uses_sae() &&
        iwn_ap_sae_pmksa_matches(apClientMac, apSaePmksaPmkid)) {
        *cursor++ = IEEE80211_ELEMID_VENDOR;
        *cursor++ = 4 + IEEE80211_PMKID_LEN;
        memcpy(cursor, IEEE80211_OUI, 3);
        cursor += 3;
        *cursor++ = IEEE80211_KDE_PMKID;
        memcpy(cursor, apSaePmksaPmkid, IEEE80211_PMKID_LEN);
        cursor += IEEE80211_PMKID_LEN;
    }
    const size_t keyDataLength =
        static_cast<size_t>(
            cursor - reinterpret_cast<uint8_t *>(key + 1));
    BE_WRITE_2(key->paylen, keyDataLength);
    BE_WRITE_2(key->len, sizeof(*key) + keyDataLength - 4);
    const size_t frameLength = sizeof(*key) + keyDataLength;
    apRsnState = IWN_AP_RSN_WAIT_M2;
    const int error = iwn_send_ap_eapol_key(frame, frameLength);
    if (error != 0)
        apRsnState = IWN_AP_RSN_DISABLED;
    XYLog("%s: AP %s EAPOL M1 queue=%d replay=%llu\n",
          com.sc_dev.dv_xname,
          iwn_ap_uses_sae() ? "WPA3" : "WPA2",
          error, apReplayCounter);
    return error;
}

int ItlIwn::iwn_send_ap_4way_msg3()
{
#ifdef IEEE80211_STA_ONLY
    /*
     * ieee80211_eapol_key_encrypt() is intentionally omitted from a
     * station-only net80211 build.  Keep the dormant HostAP entry point
     * fail-closed without making the ordinary Tahoe/IWN artifact depend on
     * an AP-only symbol.
     */
    return ENOTSUP;
#else
    uint8_t frame[256];
    bzero(frame, sizeof(frame));
    struct ieee80211_eapol_key *key =
        reinterpret_cast<struct ieee80211_eapol_key *>(frame);
    key->version = EAPOL_VERSION;
    key->type = EAPOL_KEY;
    key->desc = EAPOL_KEY_DESC_IEEE80211;
    const uint16_t descriptor = iwn_ap_uses_sae() ?
        EAPOL_KEY_DESC_AKM_DEFINED : EAPOL_KEY_DESC_V2;
    uint16_t info = EAPOL_KEY_PAIRWISE | EAPOL_KEY_KEYACK |
        EAPOL_KEY_KEYMIC | EAPOL_KEY_INSTALL | EAPOL_KEY_SECURE |
        EAPOL_KEY_ENCRYPTED | descriptor;
    BE_WRITE_2(key->info, info);
    BE_WRITE_2(key->keylen, 16);
    apReplayCounter++;
    BE_WRITE_8(key->replaycnt, apReplayCounter);
    memcpy(key->nonce, apAnonce, sizeof(key->nonce));

    uint8_t *cursor = reinterpret_cast<uint8_t *>(key + 1);
    if (apFirmwareConfig.rsnIELength == 0 ||
        apFirmwareConfig.rsnIELength > sizeof(apFirmwareRsnIE)) {
        return EINVAL;
    }
    memcpy(cursor, apFirmwareRsnIE, apFirmwareConfig.rsnIELength);
    cursor += apFirmwareConfig.rsnIELength;
    *cursor++ = IEEE80211_ELEMID_VENDOR;
    *cursor++ = 6 + sizeof(apGtk);
    memcpy(cursor, IEEE80211_OUI, 3);
    cursor += 3;
    *cursor++ = IEEE80211_KDE_GTK;
    *cursor++ = apGtkKid & 3;
    *cursor++ = 0;
    memcpy(cursor, apGtk, sizeof(apGtk));
    cursor += sizeof(apGtk);
    if (iwn_ap_uses_sae()) {
        /*
         * WPA3 requires PMF.  Carry the IGTK in M3 using the standard
         * 00:0f:ac:09 KDE: little-endian key id, six-byte initial IPN,
         * then the BIP-CMAC-128 key.
         */
        *cursor++ = IEEE80211_ELEMID_VENDOR;
        *cursor++ = 4 + 2 + 6 + sizeof(apIgtk);
        memcpy(cursor, IEEE80211_OUI, 3);
        cursor += 3;
        *cursor++ = IWN_AP_IGTK_KDE_TYPE;
        *cursor++ = apIgtkKid;
        *cursor++ = 0;
        bzero(cursor, 6);
        cursor += 6;
        memcpy(cursor, apIgtk, sizeof(apIgtk));
        cursor += sizeof(apIgtk);
    }

    const size_t plainKeyDataLength =
        static_cast<size_t>(cursor - reinterpret_cast<uint8_t *>(key + 1));
    BE_WRITE_2(key->paylen, plainKeyDataLength);
    BE_WRITE_2(key->len, sizeof(*key) + plainKeyDataLength - 4);
    ieee80211_eapol_key_encrypt(&com.sc_ic, key, apPtk.kek);
    ieee80211_eapol_key_mic(key, apPtk.kck);
    const size_t eapolLength =
        sizeof(*key) + BE_READ_2(key->paylen);
    if (eapolLength > sizeof(frame))
        return EMSGSIZE;

    apRsnState = IWN_AP_RSN_WAIT_M4;
    const int error = iwn_send_ap_eapol_key(frame, eapolLength);
    if (error != 0)
        apRsnState = IWN_AP_RSN_WAIT_M2;
    XYLog("%s: AP %s EAPOL M3 queue=%d replay=%llu\n",
          com.sc_dev.dv_xname,
          iwn_ap_uses_sae() ? "WPA3" : "WPA2",
          error, apReplayCounter);
    return error;
#endif
}

void ItlIwn::iwn_begin_ap_4way()
{
    if (apFirmwareConfig.rsnIELength == 0 ||
        !apClientAssociated || !apClientNodeInstalled)
        return;

    apClientAuthorized = false;
    apReplayCounter = 0;
    apPairwiseTxPn = 0;
    bzero(apPairwiseRxPn, sizeof(apPairwiseRxPn));
    if (apPairwiseSoftwareKey.k_priv != NULL)
        ieee80211_ccmp_delete_key(
            &com.sc_ic, &apPairwiseSoftwareKey);
    explicit_bzero(
        &apPairwiseSoftwareKey, sizeof(apPairwiseSoftwareKey));
    apSoftwareCcmpRxObserved = false;
    explicit_bzero(&apPtk, sizeof(apPtk));
    arc4random_buf(apAnonce, sizeof(apAnonce));
    (void)iwn_send_ap_4way_msg1();
}

bool ItlIwn::iwn_handle_ap_eapol_key(const uint8_t *eapol,
    size_t eapolLength)
{
    if (eapol == NULL ||
        eapolLength < sizeof(struct ieee80211_eapol_key) ||
        eapolLength > 512 ||
        apFirmwareConfig.rsnIELength == 0 ||
        !apClientAssociated) {
        return false;
    }

    uint8_t frame[512];
    memcpy(frame, eapol, eapolLength);
    struct ieee80211_eapol_key *key =
        reinterpret_cast<struct ieee80211_eapol_key *>(frame);
    const size_t declaredLength = 4 + BE_READ_2(key->len);
    const uint16_t keyInfo = BE_READ_2(key->info);
    const size_t keyDataLength = BE_READ_2(key->paylen);
    const uint16_t expectedDescriptor = iwn_ap_uses_sae() ?
        EAPOL_KEY_DESC_AKM_DEFINED : EAPOL_KEY_DESC_V2;
    if (key->type != EAPOL_KEY ||
        key->desc != EAPOL_KEY_DESC_IEEE80211 ||
        declaredLength != eapolLength ||
        keyDataLength > eapolLength - sizeof(*key) ||
        (keyInfo & EAPOL_KEY_VERSION_MASK) != expectedDescriptor ||
        (keyInfo & EAPOL_KEY_PAIRWISE) == 0 ||
        (keyInfo & EAPOL_KEY_KEYMIC) == 0 ||
        (keyInfo & (EAPOL_KEY_KEYACK | EAPOL_KEY_REQUEST |
                    EAPOL_KEY_ERROR)) != 0 ||
        BE_READ_8(key->replaycnt) != apReplayCounter) {
        XYLog("%s: AP %s EAPOL rejected state=%u info=0x%x "
              "length=%zu replay=%llu expected=%llu\n",
              com.sc_dev.dv_xname,
              iwn_ap_uses_sae() ? "WPA3" : "WPA2",
              static_cast<unsigned>(apRsnState),
              static_cast<unsigned>(keyInfo), eapolLength,
              BE_READ_8(key->replaycnt), apReplayCounter);
        return true;
    }

    if (apRsnState == IWN_AP_RSN_WAIT_M2) {
        const uint8_t *rsn = NULL;
        const uint8_t *cursor =
            reinterpret_cast<const uint8_t *>(key + 1);
        const uint8_t *end = cursor + keyDataLength;
        while (cursor + 2 <= end) {
            const size_t elementLength = cursor[1];
            if (cursor + 2 + elementLength > end)
                break;
            if (cursor[0] == IEEE80211_ELEMID_RSN) {
                rsn = cursor;
                break;
            }
            cursor += 2 + elementLength;
        }
        if (rsn == NULL ||
            apClientRsnIELength != static_cast<size_t>(rsn[1]) + 2 ||
            memcmp(rsn, apClientRsnIE, apClientRsnIELength) != 0) {
            XYLog("%s: AP %s M2 rejected: association RSN mismatch\n",
                  com.sc_dev.dv_xname,
                  iwn_ap_uses_sae() ? "WPA3" : "WPA2");
            return true;
        }

        struct ieee80211_ptk transientPtk;
        explicit_bzero(&transientPtk, sizeof(transientPtk));
        ieee80211_derive_ptk(
            iwn_ap_uses_sae() ?
                IEEE80211_AKM_SAE : IEEE80211_AKM_PSK,
            apPmk,
            apFirmwareConfig.bssid, apClientMac,
            apAnonce, key->nonce, &transientPtk);
        if (ieee80211_eapol_key_check_mic(key, transientPtk.kck) != 0) {
            explicit_bzero(&transientPtk, sizeof(transientPtk));
            XYLog("%s: AP %s M2 rejected: MIC mismatch\n",
                  com.sc_dev.dv_xname,
                  iwn_ap_uses_sae() ? "WPA3" : "WPA2");
            return true;
        }
        memcpy(&apPtk, &transientPtk, sizeof(apPtk));
        explicit_bzero(&transientPtk, sizeof(transientPtk));

        int error =
            iwn_install_ap_ccmp_key(false, apGtkKid, apGtk);
        if (error == 0)
            error = iwn_send_ap_4way_msg3();
        XYLog("%s: AP %s M2 accepted GTK/M3=%d\n",
              com.sc_dev.dv_xname,
              iwn_ap_uses_sae() ? "WPA3" : "WPA2", error);
        return true;
    }

    if (apRsnState == IWN_AP_RSN_WAIT_M4) {
        if (keyDataLength != 0 ||
            ieee80211_eapol_key_check_mic(key, apPtk.kck) != 0) {
            XYLog("%s: AP %s M4 rejected\n", com.sc_dev.dv_xname,
                  iwn_ap_uses_sae() ? "WPA3" : "WPA2");
            return true;
        }
        if (apPairwiseSoftwareKey.k_priv != NULL)
            ieee80211_ccmp_delete_key(
                &com.sc_ic, &apPairwiseSoftwareKey);
        explicit_bzero(
            &apPairwiseSoftwareKey,
            sizeof(apPairwiseSoftwareKey));
        apPairwiseSoftwareKey.k_id = 0;
        apPairwiseSoftwareKey.k_cipher = IEEE80211_CIPHER_CCMP;
        apPairwiseSoftwareKey.k_flags = IEEE80211_KEY_SWCRYPTO;
        apPairwiseSoftwareKey.k_len =
            ieee80211_cipher_keylen(IEEE80211_CIPHER_CCMP);
        memcpy(apPairwiseSoftwareKey.k_key, apPtk.tk,
               apPairwiseSoftwareKey.k_len);
        int error = ieee80211_ccmp_set_key(
            &com.sc_ic, &apPairwiseSoftwareKey);
        if (error == 0)
            error = iwn_install_ap_ccmp_key(true, 0, apPtk.tk);
        if (error == 0) {
            apClientAuthorized = true;
            apRsnState = IWN_AP_RSN_AUTHORIZED;
        } else {
            if (apPairwiseSoftwareKey.k_priv != NULL)
                ieee80211_ccmp_delete_key(
                    &com.sc_ic, &apPairwiseSoftwareKey);
            explicit_bzero(
                &apPairwiseSoftwareKey,
                sizeof(apPairwiseSoftwareKey));
        }
        XYLog("%s: AP %s 4-way complete PTK=%d authorized=%u\n",
              com.sc_dev.dv_xname,
              iwn_ap_uses_sae() ? "WPA3" : "WPA2", error,
              apClientAuthorized ? 1U : 0U);
        return true;
    }

    return true;
}

int ItlIwn::iwn_send_ap_mgmt_frame(const void *frameBytes,
    size_t frameLength)
{
    if (frameBytes == NULL || frameLength < sizeof(struct ieee80211_frame))
        return EINVAL;
    const struct ieee80211_frame *wh =
        static_cast<const struct ieee80211_frame *>(frameBytes);
    if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) !=
        IEEE80211_FC0_TYPE_MGT)
        return EINVAL;
    return iwn_send_ap_raw_frame(frameBytes, frameLength);
}

int ItlIwn::iwn_send_ap_compressed_bar(uint8_t tid, uint16_t ssn)
{
    uint8_t frame[sizeof(struct ieee80211_frame_min) + 4];
    const size_t frameLength = itl_ap_block_ack_build_bar(
        frame, sizeof(frame), apFirmwareConfig.bssid, apClientMac,
        tid, ssn);
    return frameLength == 0 ? EINVAL :
        iwn_send_ap_raw_frame(frame, frameLength);
}

int ItlIwn::iwn_send_ap_raw_frame(const void *frameBytes,
    size_t frameLength)
{
    if (frameBytes == NULL ||
        frameLength < sizeof(struct ieee80211_frame_min))
        return EINVAL;
    const struct ieee80211_frame *wh =
        static_cast<const struct ieee80211_frame *>(frameBytes);
    const uint8_t frameType =
        wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
    const uint8_t frameSubtype =
        wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;
    const bool managementFrame = frameType == IEEE80211_FC0_TYPE_MGT;
    const bool compressedBar = frameType == IEEE80211_FC0_TYPE_CTL &&
        frameSubtype == IEEE80211_FC0_SUBTYPE_BAR;
    const size_t headerLength = compressedBar ?
        sizeof(struct ieee80211_frame_min) :
        sizeof(struct ieee80211_frame);
    const size_t firstTransportBufferLength = IWN_TX_FIRST_TB_SIZE;
    const size_t bodyLength =
        frameLength > headerLength ? frameLength - headerLength : 0;
    if ((!managementFrame && !compressedBar) ||
        frameLength <= headerLength ||
        frameLength > MCLBYTES ||
        !apFirmwareTransitionActive ||
        apFirmwareStage != IWN_AP_STAGE_RUNNING ||
        com.command_queue != IWN_IPAN_CMD_QUEUE ||
        IWN_IPAN_MGMT_QUEUE >= com.ntxqs ||
        IWN_IPAN_BE_QUEUE >= com.ntxqs) {
        return EINVAL;
    }

    if ((managementFrame && frameLength < sizeof(*wh)) ||
        !IEEE80211_ADDR_EQ(wh->i_addr2, apFirmwareConfig.bssid) ||
        (managementFrame &&
         !IEEE80211_ADDR_EQ(wh->i_addr3, apFirmwareConfig.bssid))) {
        return EINVAL;
    }
    struct IwnApClientRuntime *client =
        iwn_find_ap_client(wh->i_addr1);
    if (client != NULL)
        iwn_select_ap_client(client);
    const bool protectedFrame = managementFrame &&
        (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) != 0;
    if (protectedFrame &&
        (client == NULL || !client->authorized ||
         !IEEE80211_ADDR_EQ(wh->i_addr1, client->mac) ||
         client->pairwiseTxPn >= 0xffffffffffffULL))
        return EACCES;
    const bool clientOwned = client != NULL && apClientNodeInstalled &&
        !IEEE80211_IS_MULTICAST(wh->i_addr1) &&
        IEEE80211_ADDR_EQ(wh->i_addr1, apClientMac);
    if (compressedBar && !clientOwned)
        return EHOSTUNREACH;
    const size_t transportBodyLength = bodyLength +
        (protectedFrame ? IEEE80211_CCMP_HDRLEN : 0);
    const size_t firmwareFrameLength = frameLength +
        (protectedFrame ? IEEE80211_CCMP_HDRLEN : 0);

    const int queueId = compressedBar ? IWN_IPAN_BE_QUEUE :
                                        IWN_IPAN_MGMT_QUEUE;
    const size_t payloadStride = compressedBar ?
        IWN_AP_DATA_PAYLOAD_SIZE : IWN_AP_MGMT_PAYLOAD_SIZE;
    struct iwn_tx_ring *ring = &com.txq[queueId];
    if ((com.qfullmsk & (1U << ring->qid)) != 0 ||
        ring->queued > IWN_TX_RING_HIMARK ||
        ring->queued >= IWN_TX_RING_COUNT - 1)
        return ENOBUFS;

    struct iwn_tx_desc *desc = &ring->desc[ring->cur];
    struct iwn_tx_data *data = &ring->data[ring->cur];
    struct iwn_tx_cmd *cmd = &ring->cmd[ring->cur];
    bzero(desc, sizeof(*desc));
    bzero(cmd, sizeof(*cmd));

    cmd->code = IWN_CMD_TX_DATA;
    cmd->qid = ring->qid;
    cmd->idx = ring->cur;

    struct iwn_cmd_data *tx =
        reinterpret_cast<struct iwn_cmd_data *>(cmd->data);
    tx->len = htole16(static_cast<uint16_t>(firmwareFrameLength));
    /*
     * DVM bypasses advanced-BT arbitration for Authentication frames, but
     * not for Probe or Association Responses.
     */
    const bool ignoreBluetooth = managementFrame &&
        (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) ==
            IEEE80211_FC0_SUBTYPE_AUTH &&
        apFirmwareConfig.channel <= 14;
    const bool insertTimestamp = managementFrame &&
        (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) ==
            IEEE80211_FC0_SUBTYPE_PROBE_RESP;
    tx->flags = htole32(
        IWN_TX_NEED_ACK |
        (compressedBar ? IWN_TX_IMM_BA | IWN_TX_LINKQ :
                         IWN_TX_AUTO_SEQ) |
        (insertTimestamp ? IWN_TX_INSERT_TSTAMP : 0) |
        (ignoreBluetooth ? IWN_TX_BT_DISABLE : 0));
    /*
     * DVM sends every non-data frame through the context's broadcast
     * station, even when its receiver already has a data station/BA owner.
     * The receiver address and ACK flag still describe unicast delivery;
     * the firmware station must not borrow that receiver's aggregation
     * context. Protected management retains the peer's inline CCMP key/PN.
     */
    tx->id = IWN5000_ID_PAN_BROADCAST;
    tx->lifetime = htole32(IWN_LIFETIME_INFINITE);
    tx->rts_ntries = insertTimestamp ? 3 : 60;
    tx->data_ntries = compressedBar ? 60 :
        (insertTimestamp ? 3 : 15);
    tx->tid = compressedBar ? static_cast<uint8_t>(
        (LE_READ_2(static_cast<const uint8_t *>(frameBytes) +
                   headerLength) & IEEE80211_BA_TID_INFO_MASK) >>
        IEEE80211_BA_TID_INFO_SHIFT) : IWN_NONQOS_TID;
    tx->timeout = htole16(managementFrame ? 2 : 0);
    if (apFirmwareConfig.channel <= 14) {
        tx->plcp = iwn_rates[IWN_RATE_1M_INDEX].plcp;
        tx->rflags = IWN_RFLAG_CCK;
    } else {
        tx->plcp = iwn_rates[IWN_RATE_6M_INDEX].plcp;
        tx->rflags = 0;
    }
    /*
     * DVM rotates the shared management antenna for every beacon and
     * management frame.  The first Auth response after enabling the AP in
     * the 6235 reference trace uses antenna B (rflags 0x82), while the old
     * fixed-first-chain path always emitted antenna A (0x42).  Select the
     * second valid chain when present to reproduce that first response.
     */
    uint8_t managementAntenna = IWN_LSB(com.txchainmask);
    const uint8_t remainingAntennas =
        com.txchainmask & ~managementAntenna;
    if (remainingAntennas != 0)
        managementAntenna = IWN_LSB(remainingAntennas);
    tx->rflags |= IWN_RFLAG_ANT(managementAntenna);

    const size_t commandAndHeaderLength =
        4 + sizeof(*tx) + headerLength;
    if (commandAndHeaderLength <= firstTransportBufferLength ||
        transportBodyLength > payloadStride ||
        ring->first_tb == NULL || ring->ap_payload == NULL) {
        return EMSGSIZE;
    }

    unsigned int maxChunks = 1;
    mbuf_t m = NULL;
    if (mbuf_allocpacket(MBUF_DONTWAIT, frameLength,
            &maxChunks, &m) != 0 || m == NULL) {
        return ENOMEM;
    }
    mbuf_setlen(m, frameLength);
    mbuf_pkthdr_setlen(m, frameLength);
    memcpy(mtod(m, void *), frameBytes, frameLength);

    /*
     * Match DVM's normal TX transport: the 802.11 header is inline with the
     * firmware command and the remaining frame body is its own transport
     * segment.  DVM maps that payload independently from both transport
     * command buffers.  Keep an equivalent per-slot low-DMA payload area so
     * the 6235 sees the same topology without depending on Tahoe's
     * unrestricted mbuf physical placement.
     */
    memcpy(reinterpret_cast<uint8_t *>(tx + 1),
           frameBytes, headerLength);
    /*
     * mac80211 assigns one monotonically increasing management sequence
     * before DVM builds the TX command.  This AP path owns no net80211 AP
     * sequence counter, but its q7 descriptor order is the same serialization
     * boundary: reference q7 indices 0, 1, 2 carried sequence numbers 1, 2,
     * 3 for two Probe Responses and the following Auth Response.
     */
    if (managementFrame) {
        struct ieee80211_frame *submittedHeader =
            reinterpret_cast<struct ieee80211_frame *>(tx + 1);
        LE_WRITE_2(submittedHeader->i_seq,
            static_cast<uint16_t>(((ring->cur + 1) & 0x0fff) << 4));
    }
    uint8_t *frameBody =
        ring->ap_payload + ring->cur * payloadStride;
    if (protectedFrame) {
        ++apPairwiseTxPn;
        frameBody[0] = apPairwiseTxPn;
        frameBody[1] = apPairwiseTxPn >> 8;
        frameBody[2] = 0;
        frameBody[3] = IEEE80211_WEP_EXTIV;
        frameBody[4] = apPairwiseTxPn >> 16;
        frameBody[5] = apPairwiseTxPn >> 24;
        frameBody[6] = apPairwiseTxPn >> 32;
        frameBody[7] = apPairwiseTxPn >> 40;
        tx->security = IWN_CIPHER_CCMP;
        memcpy(tx->key, apPtk.tk, sizeof(tx->key));
    }
    memcpy(frameBody + (protectedFrame ? IEEE80211_CCMP_HDRLEN : 0),
           static_cast<const uint8_t *>(frameBytes) + headerLength,
           bodyLength);
    mbuf_adj(m, headerLength);

    data->m = m;
    data->ni = NULL;
    data->totlen = static_cast<int>(firmwareFrameLength);
    data->ampdu_txmcs = 0;
    data->ampdu_nframes = 0;
    data->ampdu_rate_generation = 0;
    data->ampdu_rate_rflags = 0;
    data->ampdu_rate_feedback_valid = 0;
    data->tx_apple_nrate = 0;
    data->tx_apple_nrate_valid = 0;
    data->post_plti_trace_class = IWN_POST_PLTI_TRACE_TX_NONE;
    data->ap_mgmt = true;
    data->ap_data = false;
    iwn_sae_tx_data_clear(data);
    data->diag_subtype =
        managementFrame ? frameSubtype : 0xff;
    data->diag_auth_seq =
        managementFrame &&
        data->diag_subtype == IEEE80211_FC0_SUBTYPE_AUTH &&
        frameLength >= headerLength + 4 ?
        LE_READ_2(static_cast<const uint8_t *>(frameBytes) +
                  headerLength + 2) :
        0xffff;
    IEEE80211_ADDR_COPY(data->diag_peer, wh->i_addr1);

    /*
     * Gen1 PCIe transport presents TX commands as a physically dedicated
     * 20-byte first TB (command header through the bidirectional scratch
     * pointer), then the remainder of the command plus the 802.11 header,
     * and only then the frame body.  Linux keeps TB0 in first_tb_bufs rather
     * than merely splitting one contiguous command allocation.  Preserve
     * that boundary with the ring's dedicated low-DMA first-TB pool.  TB1
     * is the command remainder, and TB2 is the independently allocated
     * management payload, matching the three final DVM transport buffers.
     */
    const bus_addr_t firstTransportBufferAddress =
        ring->first_tb_dma.paddr +
        ring->cur * IWN_TX_FIRST_TB_STRIDE;
    const bus_addr_t firmwareScratchAddress =
        firstTransportBufferAddress + 4 +
        offsetof(struct iwn_cmd_data, scratch);
    tx->loaddr = htole32(IWN_LOADDR(firmwareScratchAddress));
    tx->hiaddr = IWN_HIADDR(firmwareScratchAddress);
    memcpy(ring->first_tb +
               ring->cur * IWN_TX_FIRST_TB_STRIDE,
           cmd, firstTransportBufferLength);

    desc->nsegs = 3;
    desc->segs[0].addr =
        htole32(IWN_LOADDR(firstTransportBufferAddress));
    desc->segs[0].len = htole16(
        IWN_HIADDR(firstTransportBufferAddress) |
        firstTransportBufferLength << 4);
    const bus_addr_t commandRemainderAddress =
        data->cmd_paddr + firstTransportBufferLength;
    desc->segs[1].addr =
        htole32(IWN_LOADDR(commandRemainderAddress));
    desc->segs[1].len = htole16(
        IWN_HIADDR(commandRemainderAddress) |
        (commandAndHeaderLength - firstTransportBufferLength) << 4);
    const bus_addr_t frameBodyAddress =
        ring->ap_payload_dma.paddr +
        ring->cur * payloadStride;
    desc->segs[2].addr = htole32(IWN_LOADDR(frameBodyAddress));
    desc->segs[2].len = htole16(
        IWN_HIADDR(frameBodyAddress) | transportBodyLength << 4);

    com.ops.update_sched(
        &com, ring->qid, ring->cur, tx->id,
        static_cast<uint16_t>(firmwareFrameLength +
            (protectedFrame && com.hw_type != IWN_HW_REV_TYPE_4965 ?
                IEEE80211_CCMP_MICLEN : 0)));
    ring->cur = (ring->cur + 1) % IWN_TX_RING_COUNT;
    IWN_WRITE(&com, IWN_HBUS_TARG_WRPTR, ring->qid << 8 | ring->cur);
    if (++ring->queued > IWN_TX_RING_HIMARK)
        com.qfullmsk |= 1 << ring->qid;
    iwn_refresh_tx_timer(&com);
    return 0;
}

static int iwn_ap_data_queue(bool multicast, uint16_t txBaMask,
    int txBaQueue)
{
    /* mac80211 assigns the AP CAB queue before DVM submits a multicast
     * frame. Firmware owns its release after DTIM, independently of the
     * selected unicast peer's aggregation state. */
    if (multicast)
        return IWN_IPAN_MCAST_QUEUE;
    return (txBaMask & 1U) != 0 ? txBaQueue : IWN_IPAN_BE_QUEUE;
}

int ItlIwn::iwn_send_ap_data_frame(mbuf_t ethernetPacket, bool moreData,
    bool psDelivery)
{
    const size_t ethernetLength =
        ethernetPacket != NULL ? mbuf_pkthdr_len(ethernetPacket) : 0;
    static uint32_t apDataTxRejectCount = 0;
    if (ethernetPacket == NULL ||
        ethernetLength < ETHER_HDR_LEN ||
        ethernetLength - ETHER_HDR_LEN >
            IWN_AP_DATA_PAYLOAD_SIZE - LLC_SNAPFRAMELEN ||
        !apFirmwareTransitionActive ||
        apFirmwareStage != IWN_AP_STAGE_RUNNING ||
        com.command_queue != IWN_IPAN_CMD_QUEUE ||
        IWN_IPAN_BE_QUEUE >= com.ntxqs) {
        if (++apDataTxRejectCount <= 32) {
            XYLog("%s: AP Ethernet TX reject #%u length=%u active=%u "
                  "stage=%u command_queue=%u ntxqs=%u\n",
                  com.sc_dev.dv_xname,
                  static_cast<unsigned>(apDataTxRejectCount),
                  static_cast<unsigned>(ethernetLength),
                  apFirmwareTransitionActive ? 1U : 0U,
                  static_cast<unsigned>(apFirmwareStage),
                  static_cast<unsigned>(com.command_queue),
                  static_cast<unsigned>(com.ntxqs));
        }
        return EINVAL;
    }

    struct ether_header ethernetHeader;
    if (mbuf_copydata(ethernetPacket, 0, sizeof(ethernetHeader),
                      &ethernetHeader) != 0) {
        return EINVAL;
    }
    const bool multicast =
        IEEE80211_IS_MULTICAST(ethernetHeader.ether_dhost);
    bool multicastPowerSave = false;
    struct IwnApClientRuntime *client = NULL;
    if (multicast) {
        for (size_t index = 0;
             index < kItlApFirmwareMaxClients; index++) {
            struct IwnApClientRuntime *candidate = &apClients[index];
            if (!candidate->inUse || !candidate->associated ||
                !candidate->nodeInstalled)
                continue;
            multicastPowerSave |= candidate->powerSave;
            if (client == NULL &&
                (apFirmwareConfig.rsnIELength == 0 || candidate->authorized)) {
                client = candidate;
            }
        }
    } else {
        client = iwn_find_ap_client(ethernetHeader.ether_dhost);
    }
    if (client == NULL || !client->associated || !client->nodeInstalled)
        return EHOSTUNREACH;
    iwn_select_ap_client(client);
    const bool eapol =
        ethernetHeader.ether_type == htons(ETHERTYPE_PAE);
    const bool protectedFrame =
        apFirmwareConfig.rsnIELength != 0 && !eapol;
    if (protectedFrame && !apClientAuthorized)
        return EACCES;
    if (protectedFrame &&
        ethernetLength - ETHER_HDR_LEN >
            IWN_AP_DATA_PAYLOAD_SIZE -
                LLC_SNAPFRAMELEN - IEEE80211_CCMP_HDRLEN) {
        return EMSGSIZE;
    }
    if (!multicast &&
        !IEEE80211_ADDR_EQ(ethernetHeader.ether_dhost, apClientMac)) {
        if (++apDataTxRejectCount <= 32) {
            XYLog("%s: AP Ethernet TX reject #%u unreachable "
                  "dst=%02x:%02x:%02x:%02x:%02x:%02x "
                  "client=%02x:%02x:%02x:%02x:%02x:%02x\n",
                  com.sc_dev.dv_xname,
                  static_cast<unsigned>(apDataTxRejectCount),
                  ethernetHeader.ether_dhost[0],
                  ethernetHeader.ether_dhost[1],
                  ethernetHeader.ether_dhost[2],
                  ethernetHeader.ether_dhost[3],
                  ethernetHeader.ether_dhost[4],
                  ethernetHeader.ether_dhost[5],
                  apClientMac[0], apClientMac[1], apClientMac[2],
                  apClientMac[3], apClientMac[4], apClientMac[5]);
        }
        return EHOSTUNREACH;
    }

    /*
     * mac80211 stops a sleeping station's TXQ, sets its AID in the TIM and
     * only releases one frame after PS-Poll (or all frames on an awake
     * edge).  Taking ownership here keeps the Skywalk submission contract
     * unchanged while reproducing that queue boundary.
     */
    if (!multicast && apClientPowerSave && !psDelivery) {
        return iwn_queue_ap_ps_packet(ethernetPacket);
    }

    if (!multicast && psDelivery) {
        const int sleepTxError = iwn_allow_ap_client_sleep_tx();
        if (sleepTxError != 0) {
            if (++apDataTxRejectCount <= 32) {
                XYLog("%s: AP Ethernet TX reject #%u sleep-count=%d\n",
                      com.sc_dev.dv_xname,
                      static_cast<unsigned>(apDataTxRejectCount),
                      sleepTxError);
            }
            return sleepTxError;
        }
    }

    const int dataQueueId = iwn_ap_data_queue(multicast,
        apClientTxBaMask, apClientTxBaQueue[0]);
    if (dataQueueId < 0 || dataQueueId >= com.ntxqs)
        return ENOSPC;
    struct iwn_tx_ring *ring = &com.txq[dataQueueId];
    if ((com.qfullmsk & (1U << ring->qid)) != 0 ||
        ring->queued > IWN_TX_RING_HIMARK ||
        ring->queued >= IWN_TX_RING_COUNT - 1) {
        if (++apDataTxRejectCount <= 32)
            XYLog("%s: AP Ethernet TX reject #%u ring-full queued=%u\n",
                  com.sc_dev.dv_xname,
                  static_cast<unsigned>(apDataTxRejectCount),
                  static_cast<unsigned>(ring->queued));
        return ENOBUFS;
    }
    if (ring->first_tb == NULL || ring->ap_payload == NULL) {
        if (++apDataTxRejectCount <= 32) {
            XYLog("%s: AP Ethernet TX reject #%u ring storage "
                  "first_tb=%p payload=%p\n",
                  com.sc_dev.dv_xname,
                  static_cast<unsigned>(apDataTxRejectCount),
                  ring->first_tb, ring->ap_payload);
        }
        return ENXIO;
    }

    /* Group frames have no per-RA QoS/BA agreement. Do not borrow an
     * arbitrary client's TID sequence for the broadcast station. */
    const bool qosData = !multicast && apClientQos;
    const size_t headerLength = qosData ?
        sizeof(struct ieee80211_qosframe) :
        sizeof(struct ieee80211_frame);
    const size_t ccmpHeaderLength =
        protectedFrame ? IEEE80211_CCMP_HDRLEN : 0;
    const size_t bodyLength =
        ccmpHeaderLength + LLC_SNAPFRAMELEN +
        ethernetLength - ETHER_HDR_LEN;
    const size_t frameLength = headerLength + bodyLength;
    const size_t firstTransportBufferLength = IWN_TX_FIRST_TB_SIZE;
    const size_t padLength =
        headerLength & 3 ? 4 - (headerLength & 3) : 0;
    const size_t commandAndHeaderLength =
        4 + sizeof(struct iwn_cmd_data) + headerLength + padLength;
    if (frameLength > UINT16_MAX ||
        commandAndHeaderLength <= firstTransportBufferLength) {
        return EMSGSIZE;
    }

    struct iwn_tx_desc *desc = &ring->desc[ring->cur];
    struct iwn_tx_data *data = &ring->data[ring->cur];
    struct iwn_tx_cmd *cmd = &ring->cmd[ring->cur];
    bzero(desc, sizeof(*desc));
    bzero(cmd, sizeof(*cmd));

    cmd->code = IWN_CMD_TX_DATA;
    cmd->qid = ring->qid;
    cmd->idx = ring->cur;

    struct iwn_cmd_data *tx =
        reinterpret_cast<struct iwn_cmd_data *>(cmd->data);
    /*
     * Exact DVM rule: firmware sequence control is used for non-QoS frames,
     * while every QoS data MPDU carries the driver-owned per-RA/TID sequence.
     * An aggregate scheduler starts at the ADDBA SSN and will not consume a
     * descriptor whose header is left at sequence zero under AUTO_SEQ.
     */
    uint32_t flags = qosData ? 0 : IWN_TX_AUTO_SEQ;
    if (!multicast)
        flags |= IWN_TX_NEED_ACK | IWN_TX_LINKQ;
    if (!multicast && ring->qid >= com.first_agg_txq &&
        com.hw_type != IWN_HW_REV_TYPE_4965)
        flags |= IWN_TX_NEED_PROTECTION;
    if (padLength != 0)
        flags |= IWN_TX_NEED_PADDING;
    tx->flags = htole32(flags);
    tx->len = htole16(static_cast<uint16_t>(frameLength));
    tx->id = multicast ?
        IWN5000_ID_PAN_BROADCAST : apClientContext->stationId;
    tx->lifetime = htole32(IWN_LIFETIME_INFINITE);
    tx->rts_ntries = 60;
    tx->data_ntries = 15;
    tx->tid = qosData ? 0 : IWN_NONQOS_TID;
    tx->timeout = 0;
    tx->linkq = 0;
    if (apFirmwareConfig.channel <= 14) {
        tx->plcp = iwn_rates[IWN_RATE_1M_INDEX].plcp;
        tx->rflags = IWN_RFLAG_CCK;
    } else {
        tx->plcp = iwn_rates[IWN_RATE_6M_INDEX].plcp;
        tx->rflags = 0;
    }
    tx->rflags |= IWN_RFLAG_ANT(IWN_LSB(com.txchainmask));

    uint8_t frameBytes[sizeof(struct ieee80211_qosframe)];
    bzero(frameBytes, sizeof(frameBytes));
    struct ieee80211_frame *frame =
        reinterpret_cast<struct ieee80211_frame *>(frameBytes);
    frame->i_fc[0] = IEEE80211_FC0_VERSION_0 |
        IEEE80211_FC0_TYPE_DATA |
        (qosData ? IEEE80211_FC0_SUBTYPE_QOS : 0);
    frame->i_fc[1] = IEEE80211_FC1_DIR_FROMDS;
    if (protectedFrame)
        frame->i_fc[1] |= IEEE80211_FC1_PROTECTED;
    /* DVM's SEND_AFTER_DTIM path marks the pending burst; firmware clears
     * More Data on the last CAB frame. Do not wake a sleeping peer by
     * sending its group traffic immediately on the ordinary BE queue. */
    if (moreData || multicastPowerSave)
        frame->i_fc[1] |= IEEE80211_FC1_MORE_DATA;
    IEEE80211_ADDR_COPY(frame->i_addr1, ethernetHeader.ether_dhost);
    IEEE80211_ADDR_COPY(frame->i_addr2, apFirmwareConfig.bssid);
    IEEE80211_ADDR_COPY(frame->i_addr3, ethernetHeader.ether_shost);
    if (qosData) {
        struct ieee80211_qosframe *qos =
            reinterpret_cast<struct ieee80211_qosframe *>(frameBytes);
        LE_WRITE_2(qos->i_qos, 0);
    }
    memcpy(reinterpret_cast<uint8_t *>(tx + 1),
           frameBytes, headerLength);
    if (qosData) {
        struct ieee80211_frame *submittedHeader =
            reinterpret_cast<struct ieee80211_frame *>(tx + 1);
        LE_WRITE_2(submittedHeader->i_seq,
            static_cast<uint16_t>(
                (apClientTxSequence[0] & 0x0fff) <<
                IEEE80211_SEQ_SEQ_SHIFT));
    }
    /*
     * DVM requires the command/header transport segment to end on a
     * four-byte boundary.  IWN_TX_NEED_PADDING makes firmware discard
     * these zero bytes instead of consuming the first bytes of LLC/SNAP.
     * cmd was cleared above, so extending the descriptor through
     * padLength publishes exactly the required zero padding.
     */

    uint8_t *frameBody =
        ring->ap_payload +
        ring->cur * IWN_AP_DATA_PAYLOAD_SIZE;
    bzero(frameBody, bodyLength);
    if (protectedFrame) {
        uint64_t *packetNumber =
            multicast ? &apGroupTxPn : &apPairwiseTxPn;
        (*packetNumber)++;
        frameBody[0] = *packetNumber;
        frameBody[1] = *packetNumber >> 8;
        frameBody[2] = 0;
        frameBody[3] =
            (multicast ? apGtkKid : 0) << 6 | IEEE80211_WEP_EXTIV;
        frameBody[4] = *packetNumber >> 16;
        frameBody[5] = *packetNumber >> 24;
        frameBody[6] = *packetNumber >> 32;
        frameBody[7] = *packetNumber >> 40;
        tx->security = IWN_CIPHER_CCMP;
        if (ring->qid >= com.first_agg_txq)
            tx->flags |= htole32(IWN_TX_AMPDU_CCMP);
        memcpy(tx->key, multicast ? apGtk : apPtk.tk,
               sizeof(tx->key));
    } else {
        tx->security = 0;
    }
    struct llc *llc = reinterpret_cast<struct llc *>(
        frameBody + ccmpHeaderLength);
    llc->llc_dsap = LLC_SNAP_LSAP;
    llc->llc_ssap = LLC_SNAP_LSAP;
    llc->llc_control = LLC_UI;
    llc->llc_snap.ether_type = ethernetHeader.ether_type;
    if (ethernetLength > ETHER_HDR_LEN &&
        mbuf_copydata(ethernetPacket, ETHER_HDR_LEN,
            ethernetLength - ETHER_HDR_LEN,
            frameBody + ccmpHeaderLength + LLC_SNAPFRAMELEN) != 0) {
        bzero(desc, sizeof(*desc));
        bzero(cmd, sizeof(*cmd));
        return EINVAL;
    }

    data->m = ethernetPacket;
    data->ni = NULL;
    data->totlen = static_cast<int>(frameLength);
    data->ampdu_txmcs = 0;
    data->ampdu_nframes = 0;
    data->ampdu_rate_generation = 0;
    data->ampdu_rate_rflags = 0;
    data->ampdu_rate_feedback_valid = 0;
    data->tx_apple_nrate = 0;
    data->tx_apple_nrate_valid = 0;
    data->post_plti_trace_class = IWN_POST_PLTI_TRACE_TX_NONE;
    data->ap_mgmt = false;
    data->ap_data = true;
    iwn_sae_tx_data_clear(data);
    data->diag_subtype = 0xff;
    data->diag_auth_seq = 0xffff;
    IEEE80211_ADDR_COPY(data->diag_peer, ethernetHeader.ether_dhost);

    const bus_addr_t firstTransportBufferAddress =
        ring->first_tb_dma.paddr +
        ring->cur * IWN_TX_FIRST_TB_STRIDE;
    const bus_addr_t firmwareScratchAddress =
        firstTransportBufferAddress + 4 +
        offsetof(struct iwn_cmd_data, scratch);
    tx->loaddr = htole32(IWN_LOADDR(firmwareScratchAddress));
    tx->hiaddr = IWN_HIADDR(firmwareScratchAddress);
    memcpy(ring->first_tb +
               ring->cur * IWN_TX_FIRST_TB_STRIDE,
           cmd, firstTransportBufferLength);

    desc->nsegs = 3;
    desc->segs[0].addr =
        htole32(IWN_LOADDR(firstTransportBufferAddress));
    desc->segs[0].len = htole16(
        IWN_HIADDR(firstTransportBufferAddress) |
        firstTransportBufferLength << 4);
    const bus_addr_t commandRemainderAddress =
        data->cmd_paddr + firstTransportBufferLength;
    desc->segs[1].addr =
        htole32(IWN_LOADDR(commandRemainderAddress));
    desc->segs[1].len = htole16(
        IWN_HIADDR(commandRemainderAddress) |
        (commandAndHeaderLength - firstTransportBufferLength) << 4);
    const bus_addr_t frameBodyAddress =
        ring->ap_payload_dma.paddr +
        ring->cur * IWN_AP_DATA_PAYLOAD_SIZE;
    desc->segs[2].addr = htole32(IWN_LOADDR(frameBodyAddress));
    desc->segs[2].len = htole16(
        IWN_HIADDR(frameBodyAddress) | bodyLength << 4);

    const uint16_t schedulerLength = static_cast<uint16_t>(
        frameLength +
        (protectedFrame && com.hw_type != IWN_HW_REV_TYPE_4965 ?
            IEEE80211_CCMP_MICLEN : 0));
    com.ops.update_sched(
        &com, ring->qid, ring->cur, tx->id,
        schedulerLength);
    ring->cur = (ring->cur + 1) % IWN_TX_RING_COUNT;
    IWN_WRITE(&com, IWN_HBUS_TARG_WRPTR, ring->qid << 8 | ring->cur);
    if (++ring->queued > IWN_TX_RING_HIMARK)
        com.qfullmsk |= 1 << ring->qid;
    iwn_refresh_tx_timer(&com);

    if (!multicast && apClientQos)
        itl_ap_tx_ba_advance_sequence(&apClientTxSequence[0]);
    if (!multicast && !eapol && apClientQos && apClientHt &&
        itl_ap_tx_ba_note_data(&apClientTxBa[0])) {
        uint8_t token = ++apClientTxDialogToken;
        if (token == 0)
            token = ++apClientTxDialogToken;
        itl_ap_tx_ba_request(&apClientTxBa[0], token, 0,
                             apClientTxSequence[0]);
        uint8_t request[sizeof(struct ieee80211_frame) + 9];
        const size_t requestLength = itl_ap_block_ack_build_request(
            request, sizeof(request), apFirmwareConfig.bssid, apClientMac,
            token, 0, apClientTxSequence[0], IEEE80211_BA_MAX_WINSZ, 0,
            iwn_ap_uses_sae() && apClientAuthorized);
        const int requestError = requestLength == 0 ? EINVAL :
            iwn_send_ap_mgmt_frame(request, requestLength);
        if (requestError != 0) {
            itl_ap_tx_ba_reset(&apClientTxBa[0]);
        } else {
            XYLog("%s: IWN AP TX ADDBA request tid=0 ssn=%u token=%u\n",
                  com.sc_dev.dv_xname,
                  static_cast<unsigned>(apClientTxSequence[0]),
                  static_cast<unsigned>(token));
        }
    }

    return 0;
}

IOReturn ItlIwn::iwn_ap_data_tx_action(OSObject *target, void *arg0,
    void *, void *, void *)
{
    ItlIwn *that = OSDynamicCast(ItlIwn, target);
    if (that == NULL || arg0 == NULL)
        return kIOReturnBadArgument;
    const int error = that->iwn_send_ap_data_frame(
        static_cast<mbuf_t>(arg0));
    if (error == 0)
        return kIOReturnSuccess;
    if (error == ENOBUFS)
        return kIOReturnNoResources;
    return kIOReturnOutputDropped;
}

IOReturn ItlIwn::transmitAPData(mbuf_t packet)
{
    if (packet == NULL)
        return kIOReturnBadArgument;
    IOCommandGate *gate = getMainCommandGate();
    IOWorkLoop *workLoop = getMainWorkLoop();
    if (gate == NULL || workLoop == NULL)
        return kIOReturnNotReady;

    /*
     * APSTA Skywalk submission queues are attached to the same controller
     * IO80211WorkQueue as the IWN command gate. Their dequeue action already
     * runs with that work-loop gate held, exactly like the reference PCIe
     * Skywalk TX queue. attemptAction() rejects this recursive entry with
     * kIOReturnCannotLock before the frame reaches the PAN ring, so reuse the
     * existing serialized context. Keep attemptAction for any caller that is
     * not already on the gated work loop.
     */
    const IOReturn result = workLoop->inGate()
        ? iwn_ap_data_tx_action(this, packet, NULL, NULL, NULL)
        : gate->attemptAction(&ItlIwn::iwn_ap_data_tx_action, packet);
    /*
     * Ordinary security/client rejection and ring backpressure are packet
     * results, not command-gate failures. Preserve their completion/drop
     * accounting in the caller, without synchronously printing one console
     * line per rejected packet while the controller workloop is held.
     */
    if (result != kIOReturnSuccess &&
        result != kIOReturnOutputDropped &&
        result != kIOReturnNoResources) {
        XYLog("%s: AP Ethernet TX gate failed result=0x%x workloop=%p "
              "in_gate=%u\n",
              com.sc_dev.dv_xname, result,
              workLoop, workLoop->inGate() ? 1U : 0U);
    }
    return result;
}

uint32_t ItlIwn::getAPTxFreeSpace() const
{
    if (!apFirmwareTransitionActive ||
        apFirmwareStage != IWN_AP_STAGE_RUNNING ||
        com.command_queue != IWN_IPAN_CMD_QUEUE ||
        IWN_IPAN_MCAST_QUEUE >= com.ntxqs) {
        return 0;
    }

    const struct iwn_tx_ring *ring = &com.txq[IWN_IPAN_BE_QUEUE];
    /* Keep one descriptor empty so producer and consumer indices cannot
     * alias.  All four AP Skywalk ACs share this single PAN BE ring. */
    const uint32_t usable = IWN_TX_RING_COUNT - 1;
    uint32_t freeSpace =
        (com.qfullmsk & (1U << ring->qid)) != 0 ||
        ring->queued > IWN_TX_RING_HIMARK ? 0 :
        (ring->queued < usable ? usable - ring->queued : 0);
    const struct iwn_tx_ring *multicast = &com.txq[IWN_IPAN_MCAST_QUEUE];
    const uint32_t multicastFree =
        (com.qfullmsk & (1U << multicast->qid)) != 0 ||
        multicast->queued > IWN_TX_RING_HIMARK ? 0 :
        (multicast->queued < usable ? usable - multicast->queued : 0);
    freeSpace = MIN(freeSpace, multicastFree);
    bool found = false;
    for (size_t index = 0; index < kItlApFirmwareMaxClients; index++) {
        const struct IwnApClientRuntime *client = &apClients[index];
        if (!client->inUse || !client->associated ||
            !client->nodeInstalled)
            continue;
        found = true;
        if ((client->txBaMask & 1U) == 0 ||
            client->txBaQueue[0] < com.first_agg_txq ||
            client->txBaQueue[0] >= com.ntxqs)
            continue;
        const struct iwn_tx_ring *aggregate =
            &com.txq[client->txBaQueue[0]];
        const uint32_t aggregateFree =
            (com.qfullmsk & (1U << aggregate->qid)) != 0 ||
            aggregate->queued > IWN_TX_RING_HIMARK ? 0 :
            (aggregate->queued < usable ?
                usable - aggregate->queued : 0);
        freeSpace = MIN(freeSpace, aggregateFree);
    }
    return found ? freeSpace : 0;
}

extern "C" uint32_t
airportItlwmQueryAPTxFreeSpace(ItlHalService *service)
{
    ItlIwn *that = OSDynamicCast(ItlIwn, service);
    if (that != NULL)
        return that->getAPTxFreeSpace();
    uint32_t freeSpace = 0;
    if (airportItlwmQueryIwmAPTxFreeSpace(service, &freeSpace))
        return freeSpace;
    return airportItlwmQueryIwxAPTxFreeSpace(service, &freeSpace) ?
        freeSpace : 0;
}

extern "C" IOReturn
airportItlwmHandoffPrimaryStaRecoveryScanToAP(ItlHalService *service)
{
    if (service == NULL)
        return kIOReturnBadArgument;

    IOReturn result = kIOReturnUnsupported;
    if (airportItlwmHandoffIwmPrimaryStaRecoveryScanToAP(service, &result))
        return result;
    if (airportItlwmHandoffIwxPrimaryStaRecoveryScanToAP(service, &result))
        return result;

    /* DVM already owns an exact lease-tagged abort plus terminal wait in
     * iwn_quiesce_scan_for_ap_transition().  Let its existing startAPMode()
     * boundary perform that handoff without extending the HAL vtable. */
    if (OSDynamicCast(ItlIwn, service) != NULL)
        return kIOReturnUnsupported;
    return kIOReturnUnsupported;
}

bool ItlIwn::iwn_handle_ap_probe_req(const struct ieee80211_frame *request,
    size_t frameLength)
{
    const size_t headerLength = sizeof(*request);
    const size_t fixedBeaconLength = headerLength + 12;
    if (request == NULL ||
        !apFirmwareTransitionActive ||
        apFirmwareStage != IWN_AP_STAGE_RUNNING ||
        frameLength < headerLength + 2 ||
        apFirmwareConfig.beaconTemplateLength < fixedBeaconLength ||
        (request->i_fc[0] & IEEE80211_FC0_TYPE_MASK) !=
            IEEE80211_FC0_TYPE_MGT ||
        (request->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) !=
            IEEE80211_FC0_SUBTYPE_PROBE_REQ) {
        return false;
    }

    const bool addressedToAp =
        IEEE80211_ADDR_EQ(request->i_addr1, apFirmwareConfig.bssid);
    const bool broadcastDestination =
        IEEE80211_IS_MULTICAST(request->i_addr1);
    if (!addressedToAp && !broadcastDestination)
        return false;

    const uint8_t *cursor =
        reinterpret_cast<const uint8_t *>(request) + headerLength;
    const uint8_t *end =
        reinterpret_cast<const uint8_t *>(request) + frameLength;
    const uint8_t *ssid = NULL;
    while (cursor + 2 <= end) {
        const size_t elementLength = cursor[1];
        if (cursor + 2 + elementLength > end)
            break;
        if (cursor[0] == IEEE80211_ELEMID_SSID) {
            ssid = cursor;
            break;
        }
        cursor += 2 + elementLength;
    }
    const bool wildcard =
        ssid != NULL && ssid[1] == 0;
    const bool exactSsid =
        ssid != NULL &&
        ssid[1] == apFirmwareConfig.ssidLength &&
        memcmp(ssid + 2, apFirmwareSsid, ssid[1]) == 0;
    if (!wildcard && !exactSsid)
        return true;
    if (apHidden && wildcard)
        return true;

    const size_t templateLength = apFirmwareConfig.beaconTemplateLength;
    const size_t responseCapacity = templateLength +
        (apHidden ? apFirmwareConfig.ssidLength : 0);
    uint8_t *response = static_cast<uint8_t *>(
        malloc(responseCapacity, M_DEVBUF, M_NOWAIT | M_ZERO));
    if (response == NULL)
        return true;

    const uint8_t *templateBytes =
        static_cast<const uint8_t *>(apFirmwareConfig.beaconTemplate);
    memcpy(response, templateBytes, fixedBeaconLength);
    size_t inputOffset = fixedBeaconLength;
    size_t outputOffset = fixedBeaconLength;
    while (inputOffset + 2 <= templateLength) {
        const size_t elementLength =
            static_cast<size_t>(templateBytes[inputOffset + 1]);
        const size_t totalLength = 2 + elementLength;
        if (inputOffset + totalLength > templateLength)
            break;
        if (apHidden &&
            templateBytes[inputOffset] == IEEE80211_ELEMID_SSID) {
            response[outputOffset++] = IEEE80211_ELEMID_SSID;
            response[outputOffset++] =
                static_cast<uint8_t>(apFirmwareConfig.ssidLength);
            memcpy(response + outputOffset, apFirmwareSsid,
                   apFirmwareConfig.ssidLength);
            outputOffset += apFirmwareConfig.ssidLength;
        } else if (templateBytes[inputOffset] != IEEE80211_ELEMID_TIM) {
            memcpy(response + outputOffset,
                   templateBytes + inputOffset, totalLength);
            outputOffset += totalLength;
        }
        inputOffset += totalLength;
    }

    struct ieee80211_frame *reply =
        reinterpret_cast<struct ieee80211_frame *>(response);
    reply->i_fc[0] = IEEE80211_FC0_VERSION_0 |
        IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_PROBE_RESP;
    reply->i_fc[1] = IEEE80211_FC1_DIR_NODS;
    LE_WRITE_2(reply->i_dur,
        apFirmwareConfig.channel <= 14 ? 0x013a : 0x003c);
    IEEE80211_ADDR_COPY(reply->i_addr1, request->i_addr2);
    IEEE80211_ADDR_COPY(reply->i_addr2, apFirmwareConfig.bssid);
    IEEE80211_ADDR_COPY(reply->i_addr3, apFirmwareConfig.bssid);

    const int error = iwn_send_ap_mgmt_frame(response, outputOffset);
    if (error != 0)
        XYLog("%s: AP probe response queue failed error=%d\n",
              com.sc_dev.dv_xname, error);
    explicit_bzero(response, responseCapacity);
    ::free(response);
    return true;
}

int ItlIwn::iwn_send_ap_sae_auth(const uint8_t *station,
    uint16_t transaction, uint16_t status,
    const void *bodyBytes, size_t bodyLength)
{
    const size_t frameLength =
        sizeof(struct ieee80211_frame) + 6 + bodyLength;
    if (station == NULL ||
        (bodyLength != 0 && bodyBytes == NULL) ||
        frameLength > MCLBYTES)
        return EINVAL;

    uint8_t *response = static_cast<uint8_t *>(
        malloc(frameLength, M_DEVBUF, M_NOWAIT | M_ZERO));
    if (response == NULL)
        return ENOMEM;

    struct ieee80211_frame *wh =
        reinterpret_cast<struct ieee80211_frame *>(response);
    wh->i_fc[0] = IEEE80211_FC0_VERSION_0 |
        IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_AUTH;
    wh->i_fc[1] = IEEE80211_FC1_DIR_NODS;
    LE_WRITE_2(wh->i_dur,
        apFirmwareConfig.channel <= 14 ? 0x013a : 0x003c);
    IEEE80211_ADDR_COPY(wh->i_addr1, station);
    IEEE80211_ADDR_COPY(wh->i_addr2, apFirmwareConfig.bssid);
    IEEE80211_ADDR_COPY(wh->i_addr3, apFirmwareConfig.bssid);
    uint8_t *auth = response + sizeof(*wh);
    LE_WRITE_2(auth, IEEE80211_AUTH_ALG_SAE);
    LE_WRITE_2(auth + 2, transaction);
    LE_WRITE_2(auth + 4, status);
    if (bodyLength != 0)
        memcpy(auth + 6, bodyBytes, bodyLength);

    const int error = iwn_send_ap_mgmt_frame(response, frameLength);
    explicit_bzero(response, frameLength);
    ::free(response);
    return error;
}

bool ItlIwn::iwn_handle_ap_sae_auth(
    const struct ieee80211_frame *request, size_t frameLength)
{
    const size_t headerLength = sizeof(*request);
    if (request == NULL ||
        !apFirmwareTransitionActive ||
        apFirmwareStage != IWN_AP_STAGE_RUNNING ||
        frameLength < headerLength + 6 ||
        (request->i_fc[0] & IEEE80211_FC0_TYPE_MASK) !=
            IEEE80211_FC0_TYPE_MGT ||
        (request->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) !=
            IEEE80211_FC0_SUBTYPE_AUTH ||
        !IEEE80211_ADDR_EQ(request->i_addr1, apFirmwareConfig.bssid) ||
        !IEEE80211_ADDR_EQ(request->i_addr3, apFirmwareConfig.bssid)) {
        return false;
    }

    const uint8_t *auth =
        reinterpret_cast<const uint8_t *>(request) + headerLength;
    if (LE_READ_2(auth) != IEEE80211_AUTH_ALG_SAE)
        return false;
    if (!iwn_ap_uses_sae()) {
        (void)iwn_send_ap_sae_auth(
            request->i_addr2, LE_READ_2(auth + 2),
            IEEE80211_STATUS_UNSPECIFIED, NULL, 0);
        return true;
    }

    const uint16_t transaction = LE_READ_2(auth + 2);
    const uint16_t status = LE_READ_2(auth + 4);
    const uint8_t *body = auth + 6;
    const size_t bodyLength =
        frameLength - headerLength - 6;

    struct IwnApClientRuntime *client =
        iwn_find_ap_client(request->i_addr2);
    if (transaction == IWN_AP_SAE_COMMIT_TRANSACTION && client == NULL)
        client = iwn_allocate_ap_client(request->i_addr2);
    if (client == NULL) {
        (void)iwn_send_ap_sae_auth(
            request->i_addr2, transaction,
            IEEE80211_STATUS_TOOMANY, NULL, 0);
        return true;
    }
    iwn_select_ap_client(client);

    if (status != IEEE80211_SAE_AP_STATUS_SUCCESS ||
        (transaction != IWN_AP_SAE_COMMIT_TRANSACTION &&
         transaction != IWN_AP_SAE_CONFIRM_TRANSACTION)) {
        (void)iwn_send_ap_sae_auth(
            request->i_addr2, transaction,
            IEEE80211_SAE_AP_STATUS_UNSPECIFIED, NULL, 0);
        return true;
    }

    if (transaction == IWN_AP_SAE_COMMIT_TRANSACTION) {
        /*
         * Infrastructure SAE uses alternating Authentication frames:
         * the station and AP both use transaction 1 for Commit, and both
         * use transaction 2 for Confirm.  This is the responder order in
         * hostapd's handle_auth_sae()/sae_sm_step(), not Open-System's
         * request/response sequence numbering.
         */
        const int resetError =
            iwn_prepare_ap_client_reauthentication(false);
        if (resetError != 0) {
            (void)iwn_send_ap_sae_auth(
                request->i_addr2, transaction,
                IEEE80211_SAE_AP_STATUS_UNSPECIFIED, NULL, 0);
            XYLog("%s: AP SAE peer reset failed error=%d\n",
                  com.sc_dev.dv_xname, resetError);
            return true;
        }
        uint8_t responseBody[
            IEEE80211_SAE_ENGINE_HNP_COMMIT_BODY_LEN];
        bzero(responseBody, sizeof(responseBody));
        size_t responseBodyLength = 0;
        const uint16_t beginStatus = ieee80211_sae_ap_begin_hnp(
            apFirmwareConfig.bssid, request->i_addr2,
            apFirmwareCredential, apFirmwareConfig.credentialLength,
            body, bodyLength, &apSae,
            responseBody, sizeof(responseBody), &responseBodyLength);
        if (beginStatus != IEEE80211_SAE_AP_STATUS_SUCCESS ||
            apSae == NULL) {
            (void)iwn_send_ap_sae_auth(
                request->i_addr2, transaction, beginStatus,
                responseBody, responseBodyLength);
            explicit_bzero(responseBody, sizeof(responseBody));
            iwn_reset_ap_sae();
            return true;
        }

        const int error = iwn_send_ap_sae_auth(
            request->i_addr2, IWN_AP_SAE_COMMIT_TRANSACTION,
            IEEE80211_SAE_AP_STATUS_SUCCESS,
            responseBody, responseBodyLength);
        explicit_bzero(responseBody, sizeof(responseBody));
        if (error != 0) {
            XYLog("%s: AP SAE Commit failed error=%d\n",
                  com.sc_dev.dv_xname, error);
            iwn_reset_ap_sae();
            return true;
        }

        XYLog("%s: AP SAE Commit accepted peer="
              "%02x:%02x:%02x:%02x:%02x:%02x group=19\n",
              com.sc_dev.dv_xname,
              request->i_addr2[0], request->i_addr2[1],
              request->i_addr2[2], request->i_addr2[3],
              request->i_addr2[4], request->i_addr2[5]);
        return true;
    }

    uint8_t responseBody[
        IEEE80211_SAE_ENGINE_CONFIRM_BODY_LEN];
    bzero(responseBody, sizeof(responseBody));
    size_t responseBodyLength = 0;
    uint8_t pmkid[IEEE80211_PMKID_LEN];
    bzero(pmkid, sizeof(pmkid));
    explicit_bzero(apPmk, sizeof(apPmk));
    const uint16_t confirmStatus = ieee80211_sae_ap_confirm(
        apSae, request->i_addr2, body, bodyLength,
        responseBody, sizeof(responseBody), &responseBodyLength,
        apPmk, sizeof(apPmk), pmkid, sizeof(pmkid));
    if (confirmStatus != IEEE80211_SAE_AP_STATUS_SUCCESS) {
        (void)iwn_send_ap_sae_auth(
            request->i_addr2, transaction,
            confirmStatus, NULL, 0);
        explicit_bzero(responseBody, sizeof(responseBody));
        explicit_bzero(apPmk, sizeof(apPmk));
        explicit_bzero(pmkid, sizeof(pmkid));
        XYLog("%s: AP SAE Confirm rejected status=%u\n",
              com.sc_dev.dv_xname,
              static_cast<unsigned>(confirmStatus));
        return true;
    }
    const int error = iwn_send_ap_sae_auth(
        request->i_addr2, IWN_AP_SAE_CONFIRM_TRANSACTION,
        IEEE80211_SAE_AP_STATUS_SUCCESS,
        responseBody, responseBodyLength);
    explicit_bzero(responseBody, sizeof(responseBody));
    if (error == 0) {
        apClientAuthenticated = true;
        apClientOpenAuthenticated = false;
        memcpy(apSaePmksaPmk, apPmk, sizeof(apSaePmksaPmk));
        memcpy(apSaePmksaPmkid, pmkid, sizeof(apSaePmksaPmkid));
        IEEE80211_ADDR_COPY(apSaePmksaSta, request->i_addr2);
        IEEE80211_ADDR_COPY(
            apSaePmksaBssid, apFirmwareConfig.bssid);
        apSaePmksaValid = true;
    } else {
        explicit_bzero(apPmk, sizeof(apPmk));
        iwn_reset_ap_sae();
    }
    explicit_bzero(pmkid, sizeof(pmkid));
    XYLog("%s: AP SAE Confirm response=%d authenticated=%u\n",
          com.sc_dev.dv_xname, error,
          apClientAuthenticated ? 1U : 0U);
    return true;
}

bool ItlIwn::iwn_handle_ap_open_auth(const struct ieee80211_frame *request,
    size_t frameLength)
{
    const size_t headerLength = sizeof(*request);
    if (request == NULL ||
        !apFirmwareTransitionActive ||
        apFirmwareStage != IWN_AP_STAGE_RUNNING ||
        frameLength < headerLength + 6 ||
        (request->i_fc[0] & IEEE80211_FC0_TYPE_MASK) !=
            IEEE80211_FC0_TYPE_MGT ||
        (request->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) !=
            IEEE80211_FC0_SUBTYPE_AUTH ||
        !IEEE80211_ADDR_EQ(request->i_addr1, apFirmwareConfig.bssid) ||
        !IEEE80211_ADDR_EQ(request->i_addr3, apFirmwareConfig.bssid)) {
        return false;
    }

    const uint8_t *auth =
        reinterpret_cast<const uint8_t *>(request) + headerLength;
    if (LE_READ_2(auth) != IEEE80211_AUTH_ALG_OPEN ||
        LE_READ_2(auth + 2) != IEEE80211_AUTH_OPEN_REQUEST ||
        LE_READ_2(auth + 4) != IEEE80211_STATUS_SUCCESS) {
        return false;
    }
    struct IwnApClientRuntime *client =
        iwn_allocate_ap_client(request->i_addr2);
    if (client == NULL) {
        uint8_t rejection[sizeof(struct ieee80211_frame) + 6];
        bzero(rejection, sizeof(rejection));
        struct ieee80211_frame *response =
            reinterpret_cast<struct ieee80211_frame *>(rejection);
        response->i_fc[0] = IEEE80211_FC0_VERSION_0 |
            IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_AUTH;
        response->i_fc[1] = IEEE80211_FC1_DIR_NODS;
        IEEE80211_ADDR_COPY(response->i_addr1, request->i_addr2);
        IEEE80211_ADDR_COPY(response->i_addr2, apFirmwareConfig.bssid);
        IEEE80211_ADDR_COPY(response->i_addr3, apFirmwareConfig.bssid);
        uint8_t *body = rejection + sizeof(*response);
        LE_WRITE_2(body, IEEE80211_AUTH_ALG_OPEN);
        LE_WRITE_2(body + 2, IEEE80211_AUTH_OPEN_RESPONSE);
        LE_WRITE_2(body + 4, IEEE80211_STATUS_TOOMANY);
        (void)iwn_send_ap_mgmt_frame(rejection, sizeof(rejection));
        return true;
    }
    iwn_select_ap_client(client);
    /*
     * IEEE 802.11 SAE PMKSA caching deliberately uses Open-System
     * authentication before the Association Request carries its PMKID.
     * hostapd admits this authentication even on a pure-SAE BSS, then
     * either selects the cached PMKSA or returns INVALID_PMKID from
     * association so the station can fall back to a fresh SAE exchange.
     */
    /*
     * Authentication starts a fresh per-peer power-save lifetime.  Retire
     * any buffered frames and remove the old AID from the beacon before the
     * station can receive a new successful response.
     */
    const int resetError = iwn_prepare_ap_client_reauthentication(
        iwn_ap_uses_sae());
    if (resetError != 0) {
        XYLog("%s: AP authentication peer reset failed error=%d\n",
              com.sc_dev.dv_xname, resetError);
        return true;
    }

    uint8_t response[sizeof(struct ieee80211_frame) + 6];
    bzero(response, sizeof(response));
    struct ieee80211_frame *wh =
        reinterpret_cast<struct ieee80211_frame *>(response);
    wh->i_fc[0] = IEEE80211_FC0_VERSION_0 |
        IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_AUTH;
    wh->i_fc[1] = IEEE80211_FC1_DIR_NODS;
    /* At 1 Mbps the ACK exchange occupies 314 us. */
    LE_WRITE_2(wh->i_dur, 0x013a);
    IEEE80211_ADDR_COPY(wh->i_addr1, request->i_addr2);
    IEEE80211_ADDR_COPY(wh->i_addr2, apFirmwareConfig.bssid);
    IEEE80211_ADDR_COPY(wh->i_addr3, apFirmwareConfig.bssid);
    uint8_t *body = response + sizeof(*wh);
    LE_WRITE_2(body, IEEE80211_AUTH_ALG_OPEN);
    LE_WRITE_2(body + 2, IEEE80211_AUTH_OPEN_RESPONSE);
    LE_WRITE_2(body + 4, IEEE80211_STATUS_SUCCESS);

    const int error = iwn_send_ap_mgmt_frame(response, sizeof(response));
    if (error == 0) {
        IEEE80211_ADDR_COPY(apClientMac, request->i_addr2);
        apClientAuthenticated = true;
        apClientOpenAuthenticated = iwn_ap_uses_sae();
        apClientReassociationPending = false;
        apClientLegacyRateMask = 0;
        apClientQos = false;
        apClientHt = false;
        apClientHtNss = 0;
        apClientHtCapabilities = 0;
        apClientHtAmpduParams = 0;
        bzero(apClientHtMcs, sizeof(apClientHtMcs));
        apClientAssociated = false;
        apClientAuthorized = false;
        apClientPowerSave = false;
        apRsnState = IWN_AP_RSN_DISABLED;
        apClientRsnIELength = 0;
        bzero(apClientRsnIE, sizeof(apClientRsnIE));
        explicit_bzero(&apPtk, sizeof(apPtk));
    }
    XYLog("%s: AP open authentication request from "
          "%02x:%02x:%02x:%02x:%02x:%02x response_queue=%d\n",
          com.sc_dev.dv_xname,
          request->i_addr2[0], request->i_addr2[1],
          request->i_addr2[2], request->i_addr2[3],
          request->i_addr2[4], request->i_addr2[5], error);
    return true;
}

bool ItlIwn::iwn_handle_ap_assoc_req(const struct ieee80211_frame *request,
    size_t frameLength)
{
    const size_t headerLength = sizeof(*request);
    const uint8_t subtype = request != NULL ?
        request->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK : 0;
    const bool reassociation =
        subtype == IEEE80211_FC0_SUBTYPE_REASSOC_REQ;
    const size_t fixedLength = reassociation ? 10 : 4;
    if (request == NULL ||
        !apFirmwareTransitionActive ||
        apFirmwareStage != IWN_AP_STAGE_RUNNING ||
        frameLength < headerLength + fixedLength ||
        (request->i_fc[0] & IEEE80211_FC0_TYPE_MASK) !=
            IEEE80211_FC0_TYPE_MGT ||
        (subtype != IEEE80211_FC0_SUBTYPE_ASSOC_REQ &&
         subtype != IEEE80211_FC0_SUBTYPE_REASSOC_REQ) ||
        !IEEE80211_ADDR_EQ(request->i_addr1, apFirmwareConfig.bssid) ||
        !IEEE80211_ADDR_EQ(request->i_addr3, apFirmwareConfig.bssid)) {
        return false;
    }

    struct IwnApClientRuntime *client =
        iwn_find_ap_client(request->i_addr2);
    if (client == NULL)
        return true;
    iwn_select_ap_client(client);

    const uint8_t *body =
        reinterpret_cast<const uint8_t *>(request) + headerLength;
    const uint16_t capability = LE_READ_2(body);
    const uint8_t *cursor = body + fixedLength;
    const uint8_t *end =
        reinterpret_cast<const uint8_t *>(request) + frameLength;
    const uint8_t *ssid = NULL;
    const uint8_t *rates = NULL;
    const uint8_t *extendedRates = NULL;
    const uint8_t *rsn = NULL;
    const uint8_t *htCapabilities = NULL;
    bool qos = false;
    const uint8_t *saePmkidList = NULL;
    uint16_t saePmkidCount = 0;
    while (cursor + 2 <= end) {
        const size_t elementLength = cursor[1];
        if (cursor + 2 + elementLength > end)
            break;
        if (cursor[0] == IEEE80211_ELEMID_SSID)
            ssid = cursor;
        else if (cursor[0] == IEEE80211_ELEMID_RATES)
            rates = cursor;
        else if (cursor[0] == IEEE80211_ELEMID_XRATES)
            extendedRates = cursor;
        else if (cursor[0] == IEEE80211_ELEMID_RSN)
            rsn = cursor;
        else if (cursor[0] == IEEE80211_ELEMID_HTCAPS &&
                 elementLength == 26)
            htCapabilities = cursor;
        else if (cursor[0] == IEEE80211_ELEMID_QOS_CAP &&
                 elementLength >= 1)
            qos = true;
        else if (cursor[0] == IEEE80211_ELEMID_VENDOR &&
                 elementLength == 7 &&
                 memcmp(cursor + 2, MICROSOFT_OUI, 3) == 0 &&
                 cursor[5] == WME_OUI_TYPE &&
                 cursor[6] == WME_INFO_OUI_SUBTYPE &&
                 cursor[7] == WME_VERSION)
            qos = true;
        cursor += 2 + elementLength;
    }

    /*
     * Validate the negotiated RSN suite instead of comparing the whole IE:
     * clients may append optional RSN capabilities/PMKID fields. WPA2
     * admits PSK/CCMP. WPA3 admits SAE/CCMP only when both MFPC and MFPR are
     * set and BIP-CMAC-128 is the negotiated group-management cipher.
     */
    bool rsnValid = apFirmwareConfig.rsnIELength == 0;
    if (apFirmwareConfig.rsnIELength != 0 &&
        rsn != NULL && rsn[1] >= 18) {
        const uint8_t *rsnBody = rsn + 2;
        const uint8_t *rsnEnd = rsnBody + rsn[1];
        static const uint8_t ccmpSuite[] = { 0x00, 0x0f, 0xac, 0x04 };
        static const uint8_t pskSuite[] = { 0x00, 0x0f, 0xac, 0x02 };
        static const uint8_t saeSuite[] = { 0x00, 0x0f, 0xac, 0x08 };
        static const uint8_t bipCmac128Suite[] = {
            0x00, 0x0f, 0xac, 0x06
        };
        if (LE_READ_2(rsnBody) == 1 &&
            rsnBody + 2 + sizeof(ccmpSuite) + 2 <= rsnEnd &&
            memcmp(rsnBody + 2, ccmpSuite, sizeof(ccmpSuite)) == 0) {
            const uint8_t *pairwise = rsnBody + 2 + sizeof(ccmpSuite);
            const uint16_t pairwiseCount = LE_READ_2(pairwise);
            pairwise += 2;
            if (pairwiseCount != 0 &&
                pairwiseCount <=
                    static_cast<uint16_t>((rsnEnd - pairwise) / 4)) {
                bool hasCCMP = false;
                for (uint16_t i = 0; i < pairwiseCount; i++) {
                    if (memcmp(pairwise + i * 4, ccmpSuite,
                               sizeof(ccmpSuite)) == 0)
                        hasCCMP = true;
                }
                const uint8_t *akm = pairwise + pairwiseCount * 4;
                if (hasCCMP && akm + 2 <= rsnEnd) {
                    const uint16_t akmCount = LE_READ_2(akm);
                    akm += 2;
                    if (akmCount != 0 &&
                        akmCount <=
                            static_cast<uint16_t>((rsnEnd - akm) / 4)) {
                        bool hasPSK = false;
                        bool hasSAE = false;
                        for (uint16_t i = 0; i < akmCount; i++) {
                            if (memcmp(akm + i * 4, pskSuite,
                                       sizeof(pskSuite)) == 0)
                                hasPSK = true;
                            if (memcmp(akm + i * 4, saeSuite,
                                       sizeof(saeSuite)) == 0)
                                hasSAE = true;
                        }

                        const uint8_t *optional =
                            akm + akmCount * 4;
                        uint16_t capabilities = 0;
                        bool capabilitiesPresent = false;
                        if (optional + 2 <= rsnEnd) {
                            capabilities = LE_READ_2(optional);
                            capabilitiesPresent = true;
                            optional += 2;
                        }

                        bool groupManagementValid = false;
                        if (optional + 2 <= rsnEnd) {
                            const uint16_t pmkidCount =
                                LE_READ_2(optional);
                            optional += 2;
                            const size_t pmkidBytes =
                                static_cast<size_t>(pmkidCount) * 16;
                            if (pmkidBytes <=
                                static_cast<size_t>(rsnEnd - optional)) {
                                saePmkidList = optional;
                                saePmkidCount = pmkidCount;
                                optional += pmkidBytes;
                                groupManagementValid =
                                    optional + 4 <= rsnEnd &&
                                    memcmp(optional, bipCmac128Suite,
                                           sizeof(bipCmac128Suite)) == 0;
                            }
                        }

                        if (iwn_ap_uses_sae()) {
                            rsnValid = hasSAE &&
                                capabilitiesPresent &&
                                (capabilities & 0x00c0) == 0x00c0 &&
                                groupManagementValid;
                        } else {
                            rsnValid = hasPSK;
                        }
                    }
                }
            }
        }
    }

    const bool saeAuthenticated =
        ieee80211_sae_ap_is_accepted(apSae) != 0;
    uint16_t legacyRateMask = rates != NULL ?
        itl_hal_ap_legacy_rate_mask(rates + 2, rates[1]) : 0;
    if (extendedRates != NULL)
        legacyRateMask |= itl_hal_ap_legacy_rate_mask(
            extendedRates + 2, extendedRates[1]);
    if (apFirmwareConfig.channel > 14)
        legacyRateMask &= 0x0ff0;
    uint8_t htMcs[2] = { 0, 0 };
    bool ht = qos && itl_hal_ap_ht_enabled(&apFirmwareConfig) &&
        htCapabilities != NULL;
    if (ht) {
        htMcs[0] = htCapabilities[5] & apFirmwareConfig.htMcsSet[0];
        htMcs[1] = htCapabilities[6] & apFirmwareConfig.htMcsSet[1];
        ht = htMcs[0] != 0;
    }
    bool saePmksaAuthenticated = false;
    if (iwn_ap_uses_sae() && apClientOpenAuthenticated) {
        for (uint16_t i = 0; i < saePmkidCount; i++) {
            if (iwn_ap_sae_pmksa_matches(
                    request->i_addr2,
                    saePmkidList + static_cast<size_t>(i) *
                        IEEE80211_PMKID_LEN)) {
                saePmksaAuthenticated = true;
                break;
            }
        }
    }
    const bool authenticated =
        apClientAuthenticated &&
        IEEE80211_ADDR_EQ(apClientMac, request->i_addr2) &&
        (!iwn_ap_uses_sae() ||
         saeAuthenticated || saePmksaAuthenticated);
    const bool invalidSaePmkid =
        iwn_ap_uses_sae() && apClientOpenAuthenticated &&
        !saePmksaAuthenticated;
    const bool valid =
        authenticated &&
        (capability & IEEE80211_CAPINFO_ESS) != 0 &&
        ssid != NULL &&
        ssid[1] == apFirmwareConfig.ssidLength &&
        memcmp(ssid + 2, apFirmwareSsid, ssid[1]) == 0 &&
        rates != NULL && rates[1] != 0 &&
        rates[1] <= IEEE80211_RATE_MAXSIZE &&
        legacyRateMask != 0 &&
        rsnValid &&
        (apFirmwareConfig.rsnIELength != 0 ?
            (capability & IEEE80211_CAPINFO_PRIVACY) != 0 :
            (capability & IEEE80211_CAPINFO_PRIVACY) == 0 && rsn == NULL);
    if (!valid) {
        int rejectError = 0;
        if (invalidSaePmkid) {
            uint8_t rejection[sizeof(struct ieee80211_frame) + 6];
            bzero(rejection, sizeof(rejection));
            struct ieee80211_frame *response =
                reinterpret_cast<struct ieee80211_frame *>(rejection);
            response->i_fc[0] = IEEE80211_FC0_VERSION_0 |
                IEEE80211_FC0_TYPE_MGT |
                (reassociation ? IEEE80211_FC0_SUBTYPE_REASSOC_RESP :
                                 IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
            response->i_fc[1] = IEEE80211_FC1_DIR_NODS;
            IEEE80211_ADDR_COPY(response->i_addr1, request->i_addr2);
            IEEE80211_ADDR_COPY(
                response->i_addr2, apFirmwareConfig.bssid);
            IEEE80211_ADDR_COPY(
                response->i_addr3, apFirmwareConfig.bssid);
            uint8_t *rejectBody = rejection + sizeof(*response);
            LE_WRITE_2(rejectBody, IEEE80211_CAPINFO_ESS |
                IEEE80211_CAPINFO_PRIVACY |
                (apFirmwareConfig.channel <= 14 ?
                    IEEE80211_CAPINFO_SHORT_SLOTTIME : 0));
            LE_WRITE_2(
                rejectBody + 2, IWN_AP_STATUS_INVALID_PMKID);
            LE_WRITE_2(rejectBody + 4, 0);
            rejectError =
                iwn_send_ap_mgmt_frame(rejection, sizeof(rejection));
        }
        XYLog("%s: AP association request rejected from "
              "%02x:%02x:%02x:%02x:%02x:%02x authenticated=%u "
              "ssid_valid=%u rates_valid=%u rsn_valid=%u privacy=%u "
              "invalid_pmkid=%u response_queue=%d\n",
              com.sc_dev.dv_xname,
              request->i_addr2[0], request->i_addr2[1],
              request->i_addr2[2], request->i_addr2[3],
              request->i_addr2[4], request->i_addr2[5],
              authenticated ? 1U : 0U,
              ssid != NULL &&
                  ssid[1] == apFirmwareConfig.ssidLength &&
                  memcmp(ssid + 2, apFirmwareSsid, ssid[1]) == 0 ? 1U : 0U,
              rates != NULL && rates[1] != 0 &&
                  rates[1] <= IEEE80211_RATE_MAXSIZE ? 1U : 0U,
              rsnValid ? 1U : 0U,
              (capability & IEEE80211_CAPINFO_PRIVACY) != 0 ? 1U : 0U,
              invalidSaePmkid ? 1U : 0U, rejectError);
        return true;
    }

    if (saePmksaAuthenticated)
        memcpy(apPmk, apSaePmksaPmk, sizeof(apPmk));

    const uint16_t aid = apClientContext->aid;
    if (apClientMaterializationStage !=
        IWN_AP_CLIENT_MATERIALIZATION_IDLE) {
        /*
         * A station may retransmit Association Request while ADD_STA is
         * still in flight.  Firmware cannot accept a second add for the
         * same id; the pending success response will satisfy this retry.
         */
        return true;
    }

    apClientAid = aid;
    apClientReassociationPending = reassociation;
    iwn_reset_ap_client_rate_control(apClientContext);
    apClientLegacyRateMask = legacyRateMask;
    apClientQos = qos;
    apClientHt = ht;
    apClientHtNss = ht ? (htMcs[1] != 0 ? 2 : 1) : 0;
    apClientHtCapabilities = ht ?
        LE_READ_2(htCapabilities + 2) &
            apFirmwareConfig.htCapabilities : 0;
    apClientHtAmpduParams = ht ? htCapabilities[4] : 0;
    memcpy(apClientHtMcs, htMcs, sizeof(apClientHtMcs));
    apClientRsnIELength = 0;
    bzero(apClientRsnIE, sizeof(apClientRsnIE));
    if (rsn != NULL &&
        static_cast<size_t>(rsn[1]) + 2 <= sizeof(apClientRsnIE)) {
        apClientRsnIELength = static_cast<size_t>(rsn[1]) + 2;
        memcpy(apClientRsnIE, rsn, apClientRsnIELength);
    }

    int error;
    if (!apClientNodeInstalled) {
        apClientMaterializationStage =
            IWN_AP_CLIENT_MATERIALIZATION_ADD_NODE;
    } else {
        /*
         * Reassociation of the same station reuses its firmware table
         * entry.  Reissuing ADD_STA for id 2 is a firmware-fatal contract
         * violation; DVM removes the entry on disassociation, while a
         * reassociation that arrives without that edge updates it in place.
         */
        apClientMaterializationStage =
            IWN_AP_CLIENT_MATERIALIZATION_WAKE_NODE;
    }
    error = iwn_submit_next_ap_client_materialization();
    if (error != 0) {
        apClientMaterializationStage =
            IWN_AP_CLIENT_MATERIALIZATION_IDLE;
        apClientReassociationPending = false;
    }
    XYLog("%s: AP association request from "
          "%02x:%02x:%02x:%02x:%02x:%02x aid=%u rsn=%u "
          "materialization=%u queue=%d\n",
          com.sc_dev.dv_xname,
          request->i_addr2[0], request->i_addr2[1],
          request->i_addr2[2], request->i_addr2[3],
          request->i_addr2[4], request->i_addr2[5],
          static_cast<unsigned>(aid),
          apFirmwareConfig.rsnIELength != 0 ? 1U : 0U,
          static_cast<unsigned>(apClientMaterializationStage), error);
    return true;
}

void ItlIwn::iwn_publish_ap_station_event(const uint8_t *station,
    const uint8_t *ies, size_t iesLength, int event)
{
    if (station == NULL)
        return;

    /*
     * The custom DVM PAN path owns its firmware station table separately
     * from the infrastructure net80211 node tree, but the recovered APSTA
     * event consumer intentionally needs only ni_macaddr and the synchronous
     * IE slice. Publish a bounded stack witness through the same registered
     * net80211 bridge used by the generic HostAP path.
     */
    struct ieee80211_node witness;
    bzero(&witness, sizeof(witness));
    IEEE80211_ADDR_COPY(witness.ni_macaddr, station);
    if (ies != NULL && iesLength != 0 && iesLength <= UINT16_MAX) {
        witness.ni_rsnie_tlv = const_cast<uint8_t *>(ies);
        witness.ni_rsnie_tlv_len = static_cast<uint16_t>(iesLength);
    }
    ieee80211_apsta_event_publish(&com.sc_ic, &witness, event);
    XYLog("%s: AP station event=%d peer="
          "%02x:%02x:%02x:%02x:%02x:%02x ie_len=%zu\n",
          com.sc_dev.dv_xname, event,
          station[0], station[1], station[2],
          station[3], station[4], station[5], iesLength);
}

bool ItlIwn::iwn_handle_ap_disconnect(
    const struct ieee80211_frame *request, size_t frameLength)
{
    if (request == NULL ||
        !apFirmwareTransitionActive ||
        apFirmwareStage != IWN_AP_STAGE_RUNNING ||
        frameLength < sizeof(*request) + sizeof(uint16_t) ||
        (request->i_fc[0] & IEEE80211_FC0_TYPE_MASK) !=
            IEEE80211_FC0_TYPE_MGT) {
        return false;
    }
    const uint8_t subtype =
        request->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;
    if (subtype != IEEE80211_FC0_SUBTYPE_DEAUTH &&
        subtype != IEEE80211_FC0_SUBTYPE_DISASSOC) {
        return false;
    }
    struct IwnApClientRuntime *client =
        iwn_find_ap_client(request->i_addr2);
    if (client == NULL)
        return true;
    iwn_select_ap_client(client);
    if (!apClientNodeInstalled ||
        !IEEE80211_ADDR_EQ(request->i_addr1, apFirmwareConfig.bssid) ||
        !IEEE80211_ADDR_EQ(request->i_addr2, apClientMac)) {
        return true;
    }

    const int removeError = iwn_remove_ap_client_node(apClientMac);
    if (removeError == 0) {
        iwn_publish_ap_station_event(
            apClientMac, NULL, 0, IEEE80211_APSTA_EVENT_LEAVE);
        if (apTimSet) {
            const int timError = iwn_update_ap_tim(false);
            if (timError != 0)
                XYLog("%s: AP disconnect TIM clear failed error=%d\n",
                      com.sc_dev.dv_xname, timError);
        }
        iwn_reset_ap_client(client, true, true);
    }
    XYLog("%s: AP client disconnect subtype=0x%02x remove=%d\n",
          com.sc_dev.dv_xname, static_cast<unsigned>(subtype),
          removeError);
    return true;
}

int ItlIwn::iwn_set_ap_client_rx_ba(uint8_t tid, uint16_t ssn,
                                    uint16_t window, bool start)
{
    if (!apClientNodeInstalled || !apClientAssociated || !apClientHt ||
        tid >= IWN_NUM_AMPDU_TID)
        return EINVAL;
    const uint16_t bit = static_cast<uint16_t>(1U << tid);
    if (((apClientRxBaMask & bit) != 0) == start)
        return 0;

    struct iwn_node_info node;
    bzero(&node, sizeof(node));
    node.id = apClientContext->stationId;
    node.control = IWN_NODE_UPDATE;
    node.flags = start ? IWN_FLAG_SET_ADDBA : IWN_FLAG_SET_DELBA;
    if (start) {
        node.addba_tid = tid;
        node.addba_ssn = htole16(ssn);
    } else {
        node.delba_tid = tid;
    }
    const int error = com.ops.add_node(&com, &node, 1);
    if (error == 0) {
        if (start) {
            itl_ap_rx_ba_start(&apClientRxBa[tid], ssn, window,
                               this, iwn_ap_rx_ba_deliver);
            apClientRxBaMask |= bit;
        } else {
            itl_ap_rx_ba_stop(&apClientRxBa[tid]);
            apClientRxBaMask &= static_cast<uint16_t>(~bit);
        }
    }
    return error;
}

void ItlIwn::iwn_ap_rx_ba_deliver(void *owner,
                                  struct ItlApRxBaReady *ready)
{
    ItlIwn *that = static_cast<ItlIwn *>(owner);
    if (that == NULL || ready == NULL)
        return;
    struct mbuf_list apFrames = MBUF_LIST_INITIALIZER();
    for (size_t index = 0; index < ready->count; index++) {
        struct ItlApRxBaBufferedFrame *frame = &ready->frames[index];
        mbuf_t packet;
        while ((packet = frame->packet) != NULL) {
            frame->packet = mbuf_nextpkt(packet);
            mbuf_setnextpkt(packet, NULL);
            (void)that->iwn_handle_ap_data(
                packet, mbuf_pkthdr_len(packet), &apFrames,
                frame->rxFlags, frame->descriptorType);
            mbuf_freem(packet);
        }
        frame->packetTail = NULL;
        frame->packet = NULL;
    }
    if_input_ap(&that->com.sc_ic.ic_if, &apFrames);
}

void ItlIwn::iwn_stop_all_ap_client_rx_ba()
{
    for (uint8_t tid = 0; tid < IWN_NUM_AMPDU_TID; tid++) {
        if ((apClientRxBaMask & (1U << tid)) == 0)
            continue;
        const int error = iwn_set_ap_client_rx_ba(tid, 0, 0, false);
        if (error != 0)
            XYLog("%s: AP DELBA cleanup tid=%u error=%d\n",
                  com.sc_dev.dv_xname, static_cast<unsigned>(tid), error);
    }
}

void ItlIwn::iwn_ap_ampdu_tx_start(
    int qid, uint8_t tid, uint16_t ssn, uint8_t frameLimit)
{
    /* DVM keeps a distinct AC-to-FIFO table for the PAN RXON context.
     * TIDs 0/3 are BE, 1/2 BK, 4/5 VI and 6/7 VO.  The corresponding PAN
     * FIFOs are BE=4, BK=0, VI=2 and VO=5; using the ordinary BSS FIFO here
     * leaves a valid q2ratid queue permanently unconsumed by FH DMA. */
    static const uint8_t iwnIpanTid2Fifo[IWN_NUM_AMPDU_TID] = {
        4, 0, 0, 4, 2, 2, 5, 5
    };
    const uint8_t fifo = iwnIpanTid2Fifo[tid];
    const uint16_t idx = IWN_AGG_SSN_TO_TXQ_IDX(ssn);
    /* DVM clamps the peer's negotiated reorder-buffer size to the
     * firmware-wide station limit (63), then programs that same value into
     * both halves of SCD context2.  64 is a valid 802.11 BA window but is
     * not a valid 6x35 aggregate frame-count limit. */
    frameLimit = MIN(frameLimit, static_cast<uint8_t>(IWN_AMPDU_MAX));
    if (frameLimit == 0)
        frameLimit = IWN_AMPDU_MAX;
    if (com.hw_type == IWN_HW_REV_TYPE_4965) {
        iwn_prph_write(&com, IWN4965_SCHED_QUEUE_STATUS(qid),
            IWN4965_TXQ_STATUS_CHGACT);
        iwn_mem_write_2(
            &com, com.sched_base + IWN4965_SCHED_TRANS_TBL(qid),
            apClientContext->stationId << 4 | tid);
        iwn_prph_setbits(&com, IWN4965_SCHED_QCHAIN_SEL, 1U << qid);
        com.txq[qid].cur = com.txq[qid].read = idx;
        IWN_WRITE(&com, IWN_HBUS_TARG_WRPTR, qid << 8 | idx);
        iwn_prph_write(
            &com, IWN4965_SCHED_QUEUE_RDPTR(qid), ssn);
        iwn_mem_write(
            &com, com.sched_base + IWN4965_SCHED_QUEUE_OFFSET(qid),
            frameLimit);
        iwn_mem_write(
            &com, com.sched_base + IWN4965_SCHED_QUEUE_OFFSET(qid) + 4,
            static_cast<uint32_t>(frameLimit) << 16);
        iwn_prph_setbits(&com, IWN4965_SCHED_INTR_MASK, 1U << qid);
        iwn_prph_write(&com, IWN4965_SCHED_QUEUE_STATUS(qid),
            IWN4965_TXQ_STATUS_ACTIVE | IWN4965_TXQ_STATUS_AGGR_ENA |
            fifo << 1);
        return;
    }

    iwn_prph_write(&com, IWN5000_SCHED_QUEUE_STATUS(qid),
        IWN5000_TXQ_STATUS_CHGACT);
    iwn_mem_write_2(
        &com, com.sched_base + IWN5000_SCHED_TRANS_TBL(qid),
        apClientContext->stationId << 4 | tid);
    iwn_prph_setbits(&com, IWN5000_SCHED_QCHAIN_SEL, 1U << qid);
    iwn_prph_setbits(&com, IWN5000_SCHED_AGGR_SEL, 1U << qid);
    com.txq[qid].cur = com.txq[qid].read = idx;
    IWN_WRITE(&com, IWN_HBUS_TARG_WRPTR, qid << 8 | idx);
    iwn_prph_write(&com, IWN5000_SCHED_QUEUE_RDPTR(qid), ssn);
    /* Match DVM's iwl_trans_txq_enable ordering: clear the first queue
     * context dword every time this dynamic aggregate queue is assigned.
     * post_alive() initializes it too, but a queue can be recycled without a
     * firmware restart and must not inherit scheduler state from its former
     * RA/TID owner. */
    iwn_mem_write(
        &com, com.sched_base + IWN5000_SCHED_QUEUE_OFFSET(qid), 0);
    iwn_mem_write(
        &com, com.sched_base + IWN5000_SCHED_QUEUE_OFFSET(qid) + 4,
        static_cast<uint32_t>(frameLimit) << 16 | frameLimit);
    /* Gen1 Linux DVM leaves SCD_INTERRUPT_MASK at zero for the PAN queue
     * topology and does not modify it in iwl_trans_pcie_txq_enable().  Keep
     * this dynamic RA/TID transition identical to that transport contract;
     * the queue status write below is the activation edge. */
    iwn_prph_write(&com, IWN5000_SCHED_QUEUE_STATUS(qid),
        IWN5000_TXQ_STATUS_ACTIVE | fifo);
}

void ItlIwn::iwn_ap_ampdu_tx_stop(
    int qid, uint8_t tid, uint16_t ssn)
{
    static const uint8_t iwnIpanTid2Fifo[IWN_NUM_AMPDU_TID] = {
        4, 0, 0, 4, 2, 2, 5, 5
    };
    const uint8_t fifo = iwnIpanTid2Fifo[tid];
    const uint16_t idx = IWN_AGG_SSN_TO_TXQ_IDX(ssn);
    if (com.hw_type == IWN_HW_REV_TYPE_4965) {
        iwn_prph_write(&com, IWN4965_SCHED_QUEUE_STATUS(qid),
            IWN4965_TXQ_STATUS_CHGACT);
        iwn_ampdu_txq_advance(&com, &com.txq[qid], qid,
                              com.txq[qid].cur);
        com.qfullmsk &= ~(1U << qid);
        com.txq[qid].cur = com.txq[qid].read = idx;
        IWN_WRITE(&com, IWN_HBUS_TARG_WRPTR, qid << 8 | idx);
        iwn_prph_write(
            &com, IWN4965_SCHED_QUEUE_RDPTR(qid), ssn);
        iwn_prph_clrbits(&com, IWN4965_SCHED_INTR_MASK, 1U << qid);
        iwn_prph_write(&com, IWN4965_SCHED_QUEUE_STATUS(qid),
            IWN4965_TXQ_STATUS_INACTIVE | fifo << 1);
        return;
    }

    iwn_prph_write(&com, IWN5000_SCHED_QUEUE_STATUS(qid),
        IWN5000_TXQ_STATUS_CHGACT);
    iwn_prph_clrbits(&com, IWN5000_SCHED_AGGR_SEL, 1U << qid);
    /* DVM retires the queue's four TX-status words separately from its
     * configuration context. Clearing context1 at the next start does not
     * retire the previous RA/TID's scheduler status. */
    iwn_mem_set_region_4(&com,
        com.sched_base + IWN5000_SCHED_TX_STATUS_OFFSET(qid), 0, 4);
    /* The scheduler must stop fetching before the physical submitted
     * interval is released. A BA window endpoint is not its write cursor. */
    iwn_ampdu_txq_advance(&com, &com.txq[qid], qid,
                          com.txq[qid].cur);
    com.qfullmsk &= ~(1U << qid);
    /* Leave the empty transport at its real submitted endpoint. The next
     * start owns sequence/cursor assignment and the activation doorbell.
     * See start: the PAN SCD interrupt mask remains firmware-owned. */
}

int ItlIwn::iwn_set_ap_client_tx_ba(uint8_t tid, uint16_t ssn, bool start)
{
    if (!apClientNodeInstalled || !apClientAssociated || !apClientHt ||
        tid >= IWN_NUM_AMPDU_TID)
        return EINVAL;
    const uint16_t bit = static_cast<uint16_t>(1U << tid);
    if (start && apClientTxBaEnablePending)
        return EBUSY;
    if (!start && apClientTxBaEnablePending &&
        apClientTxBaPendingTid == tid) {
        apClientDisableTid = apClientTxBaPendingOldDisableTid;
        apClientTxBaEnablePending = false;
        apClientTxBaPendingTid = UINT8_MAX;
        apClientTxBaPendingQueue = UINT8_MAX;
        apClientTxBaPendingSsn = 0;
        apClientTxBaPendingOldDisableTid = 0;
        struct iwn_node_info cancel;
        bzero(&cancel, sizeof(cancel));
        cancel.id = apClientContext->stationId;
        cancel.control = IWN_NODE_UPDATE;
        cancel.flags = IWN_FLAG_SET_DISABLE_TID;
        cancel.disable_tid = htole16(apClientDisableTid);
        return com.ops.add_node(&com, &cancel, 1);
    }
    if (((apClientTxBaMask & bit) != 0) == start)
        return 0;
    int qid = start ? -1 : apClientTxBaQueue[tid];
    if (start) {
        /*
         * DVM aggregation queues belong to an RA/TID, not to a TID alone.
         * The primary STA can already own first_agg_txq + tid, so choose a
         * separately provisioned free queue for this PAN station. Linux DVM's
         * iwlagn_alloc_agg_txq() scans FIRST_AMPDU_QUEUE..num_of_queues in
         * ascending order; preserve that allocation order instead of making
         * the highest hardware queue the first live AP aggregate queue.
         */
        const int firstCandidate =
            com.command_queue == IWN_IPAN_CMD_QUEUE ?
                IWN_IPAN_FIRST_AGG_QUEUE : com.first_agg_txq;
        for (int candidate = firstCandidate;
             candidate < com.ntxqs; candidate++) {
            struct iwn_tx_ring *available = &com.txq[candidate];
            if ((com.agg_queue_mask & (1U << candidate)) == 0 &&
                available->queued == 0 && available->first_tb != NULL &&
                available->ap_payload != NULL) {
                qid = candidate;
                break;
            }
        }
    }
    if (qid < com.first_agg_txq || qid >= com.ntxqs)
        return start ? ENOSPC : EINVAL;
    struct iwn_tx_ring *ring = &com.txq[qid];
    if (ring->first_tb == NULL || ring->ap_payload == NULL)
        return ENOSPC;

    struct iwn_node_info node;
    bzero(&node, sizeof(node));
    node.id = apClientContext->stationId;
    node.control = IWN_NODE_UPDATE;
    node.flags = IWN_FLAG_SET_DISABLE_TID;
    const uint16_t oldDisableTid = apClientDisableTid;
    if (start) {
        apClientDisableTid &= static_cast<uint16_t>(~bit);
        apClientTxBaEnablePending = true;
        apClientTxBaPendingTid = tid;
        apClientTxBaPendingQueue = static_cast<uint8_t>(qid);
        apClientTxBaPendingSsn = ssn & 0x0fff;
        apClientTxBaPendingOldDisableTid = oldDisableTid;
    }
    node.disable_tid = htole16(apClientDisableTid);
    int error = start ? com.ops.add_node(&com, &node, 1) : 0;
    if (start && error != 0) {
        apClientDisableTid = oldDisableTid;
        apClientTxBaEnablePending = false;
        apClientTxBaPendingTid = UINT8_MAX;
        apClientTxBaPendingQueue = UINT8_MAX;
        apClientTxBaPendingSsn = 0;
        apClientTxBaPendingOldDisableTid = 0;
        return error;
    }
    /* DVM's synchronous iwl_sta_tx_modify_enable_tid() is the ordering
     * fence for SCD activation.  This port issues ADD_STA asynchronously
     * from the RX path, so its exact command completion activates qid and
     * publishes apClientTxBaMask in iwn_note_ap_firmware_event(). */
    if (start)
        return 0;

    const int lockError = iwn_nic_lock(&com);
    if (lockError != 0) {
        apClientDisableTid = oldDisableTid;
        node.disable_tid = htole16(apClientDisableTid);
        (void)com.ops.add_node(&com, &node, 1);
        return lockError;
    }
    iwn_ap_ampdu_tx_stop(qid, tid, ssn & 0x0fff);
    iwn_nic_unlock(&com);

    com.agg_queue_mask &= ~(1U << qid);
    apClientTxBaMask &= static_cast<uint16_t>(~bit);
    apClientTxBaQueue[tid] = UINT8_MAX;
    apClientDisableTid |= bit;
    node.disable_tid = htole16(apClientDisableTid);
    error = com.ops.add_node(&com, &node, 1);
    XYLog("%s: IWN AP TX BA %s tid=%u qid=%d ssn=%u\n",
          com.sc_dev.dv_xname, "stopped",
          static_cast<unsigned>(tid), qid,
          static_cast<unsigned>(ssn & 0x0fff));
    return error;
}

void ItlIwn::iwn_stop_all_ap_client_tx_ba()
{
    if (apClientTxBaEnablePending &&
        apClientTxBaPendingTid < IWN_NUM_AMPDU_TID) {
        (void)iwn_set_ap_client_tx_ba(
            apClientTxBaPendingTid, apClientTxBaPendingSsn, false);
    }
    for (uint8_t tid = 0; tid < IWN_NUM_AMPDU_TID; tid++) {
        if ((apClientTxBaMask & (1U << tid)) != 0) {
            const int error = iwn_set_ap_client_tx_ba(
                tid, apClientTxSequence[tid], false);
            if (error != 0)
                XYLog("%s: AP TX DELBA cleanup tid=%u error=%d\n",
                      com.sc_dev.dv_xname, static_cast<unsigned>(tid), error);
        }
        itl_ap_tx_ba_reset(&apClientTxBa[tid]);
    }
    apClientTxBaMask = 0;
    apClientDisableTid = 0;
    apClientTxBaEnablePending = false;
    apClientTxBaPendingTid = UINT8_MAX;
    apClientTxBaPendingQueue = UINT8_MAX;
    apClientTxBaPendingSsn = 0;
    apClientTxBaPendingOldDisableTid = 0;
    for (size_t tid = 0; tid < kItlApRxBaTidCount; tid++)
        apClientTxBaQueue[tid] = UINT8_MAX;
    bzero(apClientRateControl.pendingAggregate,
          sizeof(apClientRateControl.pendingAggregate));
}

bool ItlIwn::iwn_handle_ap_block_ack(
    const struct ieee80211_frame *request, size_t frameLength,
    bool hardwareDecrypted)
{
    if (request == NULL || !apFirmwareTransitionActive ||
        apFirmwareStage != IWN_AP_STAGE_RUNNING)
        return false;
    struct IwnApClientRuntime *client =
        iwn_find_ap_client(request->i_addr2);
    if (client == NULL)
        return false;
    iwn_select_ap_client(client);
    const bool addressedToAp =
        frameLength >= sizeof(*request) + 2 &&
        (request->i_fc[0] & IEEE80211_FC0_TYPE_MASK) ==
            IEEE80211_FC0_TYPE_MGT &&
        (request->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) ==
            IEEE80211_FC0_SUBTYPE_ACTION &&
        IEEE80211_ADDR_EQ(request->i_addr1, apFirmwareConfig.bssid) &&
        IEEE80211_ADDR_EQ(request->i_addr2, apClientMac) &&
        IEEE80211_ADDR_EQ(request->i_addr3, apFirmwareConfig.bssid);
    if (!addressedToAp)
        return false;

    const bool protectedFrame =
        (request->i_fc[1] & IEEE80211_FC1_PROTECTED) != 0;
    const struct ieee80211_frame *parsedFrame = request;
    size_t parsedLength = frameLength;
    bool protectedVerified = protectedFrame && hardwareDecrypted;
    bool plaintextBody = false;
    mbuf_t plaintext = NULL;

    /*
     * Match net80211's robust-management RX contract before inspecting the
     * Action category.  DVM normally leaves the CCMP IV in a hardware-
     * decrypted frame, but 6x35 PAN RX can deliver the intact encrypted
     * MMPDU without cipher status.  The pairwise software descriptor is the
     * exact fallback owner already used by AP data RX: it verifies MIC and
     * the management PN, strips IV/MIC, and clears Protected.  Restore that
     * bit only as metadata for the AP classifier; plaintextBody records the
     * normalized layout explicitly.
     */
    if (protectedFrame && !hardwareDecrypted) {
        if (!apClientAuthorized ||
            apPairwiseSoftwareKey.k_priv == NULL ||
            apPairwiseSoftwareKey.k_cipher != IEEE80211_CIPHER_CCMP ||
            (apPairwiseSoftwareKey.k_flags & IEEE80211_KEY_SWCRYPTO) == 0 ||
            frameLength < sizeof(*request) + IEEE80211_CCMP_HDRLEN + 2 +
                IEEE80211_CCMP_MICLEN) {
            return true;
        }
        unsigned int maxChunks = 1;
        mbuf_t encrypted = NULL;
        if (mbuf_allocpacket(MBUF_DONTWAIT, frameLength, &maxChunks,
                &encrypted) != 0 || encrypted == NULL)
            return true;
        mbuf_setlen(encrypted, frameLength);
        mbuf_pkthdr_setlen(encrypted, frameLength);
        memcpy(mtod(encrypted, void *), request, frameLength);
        plaintext = ieee80211_ccmp_decrypt(
            &com.sc_ic, encrypted, &apPairwiseSoftwareKey);
        if (plaintext == NULL)
            return true;
        parsedLength = mbuf_pkthdr_len(plaintext);
        if (parsedLength < sizeof(*request) + 2) {
            mbuf_freem(plaintext);
            return true;
        }
        struct ieee80211_frame *normalized =
            mtod(plaintext, struct ieee80211_frame *);
        normalized->i_fc[1] |= IEEE80211_FC1_PROTECTED;
        parsedFrame = normalized;
        protectedVerified = true;
        plaintextBody = true;
    }
    struct ItlApBlockAckAction action;
    const bool claimed = itl_ap_block_ack_parse(
        parsedFrame, parsedLength, apFirmwareConfig.bssid, apClientMac,
        apClientAssociated, apClientHt, apClientAuthorized,
        iwn_ap_uses_sae() && apClientAuthorized,
        protectedVerified, plaintextBody, &action);
    if (plaintext != NULL)
        mbuf_freem(plaintext);
    if (!claimed)
        return protectedFrame;
    if (action.kind == kItlApBlockAckAddRequest) {
        const int baError = iwn_set_ap_client_rx_ba(
            action.tid, action.ssn, action.window, true);
        uint8_t response[sizeof(struct ieee80211_frame) + 9];
        const size_t responseLength = itl_ap_block_ack_build_response(
            response, sizeof(response), apFirmwareConfig.bssid,
            apClientMac, action.token, action.tid,
            baError == 0 ? IEEE80211_STATUS_SUCCESS :
                           IEEE80211_STATUS_REFUSED,
            action.window, action.timeout,
            iwn_ap_uses_sae() && apClientAuthorized);
        const int responseError = responseLength == 0 ? EINVAL :
            iwn_send_ap_mgmt_frame(response, responseLength);
        XYLog("%s: IWN AP RX ADDBA tid=%u ssn=%u win=%u "
              "firmware=%d response=%d\n", com.sc_dev.dv_xname,
              static_cast<unsigned>(action.tid),
              static_cast<unsigned>(action.ssn),
              static_cast<unsigned>(action.window),
              baError, responseError);
        return true;
    }
    if (action.kind == kItlApBlockAckAddResponse) {
        struct ItlApTxBaRuntime *txBa = &apClientTxBa[action.tid];
        int error = 0;
        if (!itl_ap_tx_ba_response_matches(txBa, &action)) {
            error = EINVAL;
        } else if (action.status != IEEE80211_STATUS_SUCCESS) {
            itl_ap_tx_ba_block(txBa);
        } else {
            error = iwn_set_ap_client_tx_ba(
                action.tid, txBa->ssn, true);
            if (error == 0) {
                itl_ap_tx_ba_accept(txBa, &action);
            } else {
                uint8_t delba[sizeof(struct ieee80211_frame) + 6];
                const size_t delbaLength = itl_ap_block_ack_build_delete(
                    delba, sizeof(delba), apFirmwareConfig.bssid,
                    apClientMac, action.tid,
                    IEEE80211_REASON_SETUP_REQUIRED, true,
                    iwn_ap_uses_sae() && apClientAuthorized);
                if (delbaLength != 0)
                    (void)iwn_send_ap_mgmt_frame(delba, delbaLength);
                itl_ap_tx_ba_reset(txBa);
            }
        }
        XYLog("%s: IWN AP TX ADDBA response tid=%u status=%u win=%u "
              "timeout=%u error=%d\n",
              com.sc_dev.dv_xname, static_cast<unsigned>(action.tid),
              static_cast<unsigned>(action.status),
              static_cast<unsigned>(action.window),
              static_cast<unsigned>(action.timeout), error);
        return true;
    }
    if (action.kind == kItlApBlockAckDelete) {
        const int error = action.peerInitiator ?
            iwn_set_ap_client_rx_ba(action.tid, 0, 0, false) :
            iwn_set_ap_client_tx_ba(
                action.tid, apClientTxSequence[action.tid], false);
        if (!action.peerInitiator)
            itl_ap_tx_ba_reset(&apClientTxBa[action.tid]);
        XYLog("%s: IWN AP RX DELBA tid=%u peer_initiator=%u error=%d\n",
              com.sc_dev.dv_xname, static_cast<unsigned>(action.tid),
              action.peerInitiator ? 1U : 0U, error);
    }
    return true;
}

bool ItlIwn::iwn_handle_ap_ps_poll(
    const struct ieee80211_frame_pspoll *request, size_t frameLength)
{
    if (request == NULL ||
        frameLength < sizeof(*request) ||
        (request->i_fc[0] & IEEE80211_FC0_TYPE_MASK) !=
            IEEE80211_FC0_TYPE_CTL ||
        (request->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) !=
            IEEE80211_FC0_SUBTYPE_PS_POLL) {
        return false;
    }
    if (!apFirmwareTransitionActive ||
        apFirmwareStage != IWN_AP_STAGE_RUNNING ||
        !IEEE80211_ADDR_EQ(request->i_bssid,
                           apFirmwareConfig.bssid)) {
        return false;
    }
    struct IwnApClientRuntime *client =
        iwn_find_ap_client(request->i_ta);
    if (client == NULL)
        return true;
    iwn_select_ap_client(client);
    if (!apClientAssociated ||
        !IEEE80211_ADDR_EQ(request->i_ta, apClientMac) ||
        (LE_READ_2(request->i_aid) & 0x3fff) != apClientAid) {
        return true;
    }

    if (apPsQueueCount == 0) {
        if (apTimSet) {
            const int timError = iwn_update_ap_tim(false);
            if (timError != 0)
                XYLog("%s: AP empty PS-Poll TIM clear failed error=%d\n",
                      com.sc_dev.dv_xname, timError);
        }
        return true;
    }

    mbuf_t packet = apPsQueue[apPsQueueHead];
    const bool moreData = apPsQueueCount > 1;
    const int error =
        iwn_send_ap_data_frame(packet, moreData, true);
    if (error == 0) {
        apPsQueue[apPsQueueHead] = NULL;
        apPsQueueHead =
            (apPsQueueHead + 1) % IWN_AP_PS_QUEUE_LEN;
        apPsQueueCount--;
        if (apPsQueueCount == 0) {
            const int timError = iwn_update_ap_tim(false);
            if (timError != 0)
                XYLog("%s: AP PS-Poll TIM clear failed error=%d\n",
                      com.sc_dev.dv_xname, timError);
        }
    }
    if (error != 0)
        XYLog("%s: AP PS-Poll delivery failed aid=%u error=%d\n",
              com.sc_dev.dv_xname,
              static_cast<unsigned>(apClientAid), error);
    return true;
}

bool ItlIwn::iwn_handle_ap_data(mbuf_t packet, size_t frameLength,
    struct mbuf_list *frames, uint32_t rxFlags, uint8_t descriptorType)
{
    if (packet == NULL || frames == NULL ||
        !apFirmwareTransitionActive ||
        apFirmwareStage != IWN_AP_STAGE_RUNNING ||
        frameLength < sizeof(struct ieee80211_frame)) {
        return false;
    }

    const struct ieee80211_frame *wh =
        mtod(packet, const struct ieee80211_frame *);
    struct IwnApClientRuntime *client =
        iwn_find_ap_client(wh->i_addr2);
    if (client == NULL || !client->associated)
        return false;
    iwn_select_ap_client(client);
    if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) !=
            IEEE80211_FC0_TYPE_DATA ||
        (wh->i_fc[1] & IEEE80211_FC1_DIR_MASK) !=
            IEEE80211_FC1_DIR_TODS ||
        !IEEE80211_ADDR_EQ(wh->i_addr1, apFirmwareConfig.bssid) ||
        !IEEE80211_ADDR_EQ(wh->i_addr2, apClientMac)) {
        return false;
    }

    /*
     * DVM lets firmware learn the sleeping edge from the station's PM bit,
     * but the awake edge is an explicit ADD_STA modify.  mac80211 expresses
     * that edge as STA_NOTIFY_AWAKE and iwl_sta_modify_ps_wake() clears only
     * STA_FLG_PWR_SAVE_MSK.  Mirror that exact command here before either a
     * null-data frame is consumed or its Ethernet payload reaches BSD.
     */
    const bool powerSave =
        (wh->i_fc[1] & IEEE80211_FC1_PWR_MGT) != 0;
    if (powerSave) {
        apClientPowerSave = true;
    } else if (apClientPowerSave) {
        const int wakeError = iwn_wake_ap_client_node();
        if (wakeError == 0)
            apClientPowerSave = false;
        if (wakeError != 0)
            XYLog("%s: AP client wake update failed error=%d\n",
                  com.sc_dev.dv_xname, wakeError);
        if (wakeError == 0)
            iwn_drain_ap_ps_queue();
    }

    /* Frames for this PAN MAC never enter the primary STA node tree. */
    if ((wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_NODATA) != 0)
        return true;

    size_t headerLength = ieee80211_get_hdrlen(wh);
    if (headerLength < sizeof(struct ieee80211_frame))
        return true;

    const bool protectedFrame =
        (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) != 0;
    size_t payloadOffset = headerLength;
    size_t payloadEnd = frameLength;
    if (protectedFrame) {
        if (apFirmwareConfig.rsnIELength == 0 ||
            !apClientAuthorized ||
            frameLength < headerLength + IEEE80211_CCMP_HDRLEN +
                LLC_SNAPFRAMELEN + IEEE80211_CCMP_MICLEN) {
            return true;
        }

        const bool hardwareDecrypted =
            (rxFlags & IWN_RX_CIPHER_MASK) == IWN_RX_CIPHER_CCMP &&
            (descriptorType == IWN_MPDU_RX_DONE ?
                (rxFlags & (IWN_RX_MPDU_DEC | IWN_RX_MPDU_MIC_OK)) ==
                    (IWN_RX_MPDU_DEC | IWN_RX_MPDU_MIC_OK) :
                (rxFlags & IWN_RX_DECRYPT_MASK) ==
                    IWN_RX_DECRYPT_OK);
        if (!hardwareDecrypted) {
            /*
             * Cold 6x35 PAN startup can acknowledge SET_KEY yet report an
             * encrypted MPDU with no cipher/DEC/MIC result.  The bytes are
             * intact, so keep the normal hardware fast path and use the
             * net80211 CCMP implementation only for this explicit firmware
             * miss.  Its MIC and per-TID RSC checks remain fail-closed.
             */
            if (apPairwiseSoftwareKey.k_priv == NULL ||
                apPairwiseSoftwareKey.k_cipher !=
                    IEEE80211_CIPHER_CCMP ||
                (apPairwiseSoftwareKey.k_flags &
                    IEEE80211_KEY_SWCRYPTO) == 0) {
                return true;
            }
            mbuf_t encryptedCopy = NULL;
            if (mbuf_dup(packet, MBUF_DONTWAIT,
                    &encryptedCopy) != 0 ||
                encryptedCopy == NULL) {
                return true;
            }
            mbuf_t plain = ieee80211_ccmp_decrypt(
                &com.sc_ic, encryptedCopy,
                &apPairwiseSoftwareKey);
            if (plain == NULL)
                return true;
            const size_t plainLength = mbuf_pkthdr_len(plain);
            if (plainLength > frameLength ||
                plainLength <
                    sizeof(struct ieee80211_frame) +
                    LLC_SNAPFRAMELEN ||
                mbuf_copydata(plain, 0, plainLength,
                    mbuf_data(packet)) != 0) {
                mbuf_freem(plain);
                return true;
            }
            mbuf_freem(plain);
            frameLength = plainLength;
            mbuf_setlen(packet, frameLength);
            mbuf_pkthdr_setlen(packet, frameLength);
            wh = mtod(packet, const struct ieee80211_frame *);
            headerLength = ieee80211_get_hdrlen(wh);
            if (headerLength < sizeof(struct ieee80211_frame) ||
                frameLength < headerLength + LLC_SNAPFRAMELEN) {
                return true;
            }
            payloadOffset = headerLength;
            payloadEnd = frameLength;
            if (!apSoftwareCcmpRxObserved) {
                apSoftwareCcmpRxObserved = true;
                XYLog("%s: AP protected RX software CCMP "
                      "fallback active\n", com.sc_dev.dv_xname);
            }
        } else {
            uint8_t ccmp[IEEE80211_CCMP_HDRLEN];
            if (mbuf_copydata(packet, headerLength,
                    sizeof(ccmp), ccmp) != 0 ||
                (ccmp[3] & IEEE80211_WEP_EXTIV) == 0 ||
                ((ccmp[3] >> 6) & 3) != 0) {
                return true;
            }
            const uint64_t packetNumber =
                static_cast<uint64_t>(ccmp[0]) |
                static_cast<uint64_t>(ccmp[1]) << 8 |
                static_cast<uint64_t>(ccmp[4]) << 16 |
                static_cast<uint64_t>(ccmp[5]) << 24 |
                static_cast<uint64_t>(ccmp[6]) << 32 |
                static_cast<uint64_t>(ccmp[7]) << 40;
            const uint8_t tid = ieee80211_has_qos(wh) ?
                ieee80211_get_qos(wh) & IEEE80211_QOS_TID : 0;
            if (packetNumber == 0 ||
                packetNumber <= apPairwiseRxPn[tid]) {
                return true;
            }
            apPairwiseRxPn[tid] = packetNumber;
            payloadOffset += IEEE80211_CCMP_HDRLEN;
            payloadEnd -= IEEE80211_CCMP_MICLEN;
        }
    }
    if (payloadEnd < payloadOffset + LLC_SNAPFRAMELEN)
        return true;

    struct llc llc;
    if (mbuf_copydata(packet, payloadOffset, sizeof(llc), &llc) != 0 ||
        llc.llc_dsap != LLC_SNAP_LSAP ||
        llc.llc_ssap != LLC_SNAP_LSAP ||
        llc.llc_control != LLC_UI ||
        llc.llc_snap.org_code[0] != 0 ||
        llc.llc_snap.org_code[1] != 0 ||
        llc.llc_snap.org_code[2] != 0) {
        return true;
    }

    const size_t payloadLength =
        payloadEnd - payloadOffset - LLC_SNAPFRAMELEN;
    if (llc.llc_snap.ether_type == htons(ETHERTYPE_PAE)) {
        if (protectedFrame || payloadLength > 512)
            return true;
        uint8_t eapol[512];
        if (payloadLength >= sizeof(struct ieee80211_eapol_key) &&
            mbuf_copydata(packet, payloadOffset + LLC_SNAPFRAMELEN,
                payloadLength, eapol) == 0) {
            (void)iwn_handle_ap_eapol_key(eapol, payloadLength);
        }
        explicit_bzero(eapol, sizeof(eapol));
        return true;
    }
    if (apFirmwareConfig.rsnIELength != 0 &&
        (!protectedFrame || !apClientAuthorized)) {
        return true;
    }
    const size_t ethernetLength = ETHER_HDR_LEN + payloadLength;
    if (ethernetLength > MCLBYTES)
        return true;

    unsigned int maxChunks = 1;
    mbuf_t ethernetPacket = NULL;
    if (mbuf_allocpacket(MBUF_DONTWAIT, ethernetLength,
            &maxChunks, &ethernetPacket) != 0 ||
        ethernetPacket == NULL) {
        return true;
    }
    mbuf_setlen(ethernetPacket, ethernetLength);
    mbuf_pkthdr_setlen(ethernetPacket, ethernetLength);

    struct ether_header *ethernetHeader =
        mtod(ethernetPacket, struct ether_header *);
    IEEE80211_ADDR_COPY(ethernetHeader->ether_dhost, wh->i_addr3);
    IEEE80211_ADDR_COPY(ethernetHeader->ether_shost, wh->i_addr2);
    ethernetHeader->ether_type = llc.llc_snap.ether_type;
    if (payloadLength != 0 &&
        mbuf_copydata(packet,
            payloadOffset + LLC_SNAPFRAMELEN,
            payloadLength,
            reinterpret_cast<uint8_t *>(ethernetHeader) +
                ETHER_HDR_LEN) != 0) {
        mbuf_freem(ethernetPacket);
        return true;
    }

    ml_enqueue(frames, ethernetPacket);
    return true;
}

void ItlIwn::
detach(IOPCIDevice *device)
{
    iwn_purge_ap_ps_queue();
    struct _ifnet *ifp = &com.sc_ic.ic_ac.ac_if;
    struct iwn_softc *sc = &com;

    if (com.sc_ic.ic_newstate_preflight == iwn_newstate_preflight)
        com.sc_ic.ic_newstate_preflight = NULL;
    if (com.sc_ic.ic_wcl_join_failure_scan == iwn_wcl_join_failure_scan)
        com.sc_ic.ic_wcl_join_failure_scan = NULL;
    ieee80211_wcl_join_cancel(&com.sc_ic, 0);
    /* Close and drain every producer which could otherwise enqueue replay
     * after the task_del()+barrier snapshot below.  A worker already inside
     * its body is fenced separately by clearing its exact initial-handoff
     * token under sc_scan_lease_lock before it can reach WRPTR. */
    iwn_scan_lease_replay_task_admission_close(sc);
    iwn_scan_lease_replay_task_admission_drain(sc);
    bool drain_scan_replay = false;
    if (sc->sc_scan_lease_lock != NULL) {
        IOSimpleLockLock(sc->sc_scan_lease_lock);
        drain_scan_replay = sc->sc_scan_lease_replay_task_ready;
        sc->sc_scan_lease_replay_task_ready = false;
        sc->sc_scan_lease_replay_pending = false;
        sc->sc_scan_lease_replay_sae_generation = 0;
        sc->sc_wcl_join_cleanup_generation = 0;
        if (iwn_scan_lease_live_locked(sc) &&
            iwn_scan_lease_owner_is_wcl(sc->sc_scan_lease.owner)) {
            sc->sc_scan_lease.publication_invalidated = true;
            if (sc->sc_scan_lease.owner == IWN_SCAN_LEASE_WCL_INITIAL &&
                !sc->sc_scan_lease.wcl_initial_started)
                sc->sc_scan_lease.abort_requested = true;
        }
        iwn_wcl_initial_scan_pending_clear_locked(sc);
        IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    }
    if (drain_scan_replay && systq != NULL) {
        (void)task_del(systq, &sc->scan_lease_replay_task);
        taskq_barrier(systq);
    }

    /* Retire the direct crypto owner while its native TX cancellation gate
     * still exists; it keeps only an auth-owned tombstone until IRQ teardown
     * makes a fresh RX fallback impossible. */
    iwn_sae_engine_detach_begin(sc);
    /* Close direct Algorithm-3 TX before its gate, IRQ or DMA disappear. */
    iwn_sae_tx_detach_begin(sc);
    /* The shared SAE lifecycle is now closed and drained, so the private
     * pre-selection password cannot race this final scrub/free boundary. */
    iwn_sae_wcl_detach_begin(sc);
    /* Close the software-PMF producer before DMA, net80211 nodes, or systq
     * disappear.  This is safe on an early attach unwind as every helper
     * tolerates an uninitialised PMF lock. */
    iwn_mfp_pae_detach_begin(sc);
    /* PMF close/drain covers admitted callbacks, but a direct IWN RX action
     * can still hold a copied generic hook just before callback admission.
     * Removing this source synchronously acknowledges that action before any
     * PMF lock, DMA ring, or net80211 state below is released. */
    iwn_interrupt_teardown(sc);
    /* No hardware RX producer remains.  Drop the closing auth-owned hook
     * before generic ifdetach destroys its selected-BSS leaf. */
    iwn_sae_engine_callback_close(sc);
    iwn_sae_engine_callback_drain(sc);
    (void)iwn_sae_engine_publish_hooks(sc, false, false, 0);
    
    for (int txq_i = 0; txq_i < nitems(sc->txq); txq_i++)
        iwn_free_tx_ring(sc, &sc->txq[txq_i]);
    iwn_sae_tx_purge(sc);
    iwn_free_rx_ring(sc, &sc->rxq);
    iwn_free_sched(sc);
    iwn_free_ict(sc);
    iwn_free_kw(sc);
    iwn_free_fwmem(sc);
    ieee80211_ifdetach(ifp);
    /* detach_begin closed admission and drained every captured callback;
     * repeat the assertion after ifdetach's final generic cleanup. */
    iwn_mfp_pae_callback_destroy(sc);
    taskq_destroy(systq);
    if (sc->sc_mfp_pae_lock != NULL) {
        IOSimpleLockFree(sc->sc_mfp_pae_lock);
        sc->sc_mfp_pae_lock = NULL;
    }
    if (sc->sc_scan_lease_lock != NULL) {
        IOSimpleLockFree(sc->sc_scan_lease_lock);
        sc->sc_scan_lease_lock = NULL;
    }
    if (sc->sc_sae_tx_lock != NULL) {
        IOSimpleLockFree(sc->sc_sae_tx_lock);
        sc->sc_sae_tx_lock = NULL;
    }
    if (sc->sc_sae_engine_lock != NULL) {
        IOSimpleLockFree(sc->sc_sae_engine_lock);
        sc->sc_sae_engine_lock = NULL;
    }
    if (sc->sc_sae_wcl_credential_lock != NULL) {
        IOSimpleLockFree(sc->sc_sae_wcl_credential_lock);
        sc->sc_sae_wcl_credential_lock = NULL;
    }
    if (sc->sc_sae_tx_lifecycle_lock != NULL) {
        IOLockFree(sc->sc_sae_tx_lifecycle_lock);
        sc->sc_sae_tx_lifecycle_lock = NULL;
    }
    releaseAll();
}

void ItlIwn::
iwn_interrupt_teardown(struct iwn_softc *sc)
{
    IOInterruptEventSource *ih;

    if (sc == NULL || (ih = sc->sc_ih) == NULL)
        return;
    ih->disable();
    if (pci.workloop != NULL)
        pci.workloop->removeEventSource(ih);
    ih->release();
    sc->sc_ih = NULL;
}

void ItlIwn::
releaseAll()
{
    pci_intr_handle *intrHandler = com.ih;
    if (apCsaTimerInitialized) {
        timeout_del(&apCsaTimeout);
        timeout_free(&apCsaTimeout);
        apCsaTimerInitialized = false;
    }
    
    if (com.calib_to) {
        timeout_del(&com.calib_to);
        timeout_free(&com.calib_to);
    }
    if (intrHandler) {
        if (intrHandler->intr && intrHandler->workloop) {
//            intrHandler->intr->disable();
            intrHandler->workloop->removeEventSource(intrHandler->intr);
            intrHandler->intr->release();
        }
        intrHandler->intr = NULL;
        intrHandler->workloop = NULL;
        intrHandler->arg = NULL;
        intrHandler->dev = NULL;
        intrHandler->func = NULL;
        intrHandler->release();
        com.ih = NULL;
    }
    pci.pa_tag = NULL;
    pci.workloop = NULL;
}

void ItlIwn::free()
{
	for (size_t index = 0; index < kItlApFirmwareMaxClients; index++)
		iwn_reset_ap_client(&apClients[index], true);
	apClientContext = &apClients[0];
	if (ieee80211_bip_lifetime_drain(&com.sc_ic) != 0)
		panic("ItlIwn::free BIP lifetime");
	ieee80211_pae_selected_bss_lock_destroy(&com.sc_ic);
    super::free();
}

ItlDriverInfo *ItlIwn::
getDriverInfo()
{
    return this;
}

ItlDriverController *ItlIwn::
getDriverController()
{
    return this;
}

IOReturn ItlIwn::enable(IONetworkInterface *netif)
{
    struct _ifnet *ifp = &com.sc_ic.ic_ac.ac_if;
    if (ifp->if_flags & IFF_UP) {
        return kIOReturnSuccess;
    }
    ifp->if_flags |= IFF_UP;
    iwn_activate(&com, DVACT_RESUME);
    iwn_activate(&com, DVACT_WAKEUP);
    return kIOReturnSuccess;
}

IOReturn ItlIwn::disable(IONetworkInterface *netif)
{
    struct _ifnet *ifp = &com.sc_ic.ic_ac.ac_if;
    /*
     * A whole-radio quiesce destroys both firmware contexts.  Do not retain
     * a logically-running PAN owner across radio off/sleep: the next public
     * HostAP start must program a fresh context after lower activation.
     */
    iwn_reset_ap_runtime_state();
    if (!(ifp->if_flags & IFF_UP)) {
        return kIOReturnSuccess;
    }
    ifp->if_flags &= ~IFF_UP;
    iwn_activate(&com, DVACT_QUIESCE);
    return kIOReturnSuccess;
}

void ItlIwn::
clearScanningFlags()
{
    bool lease_live = false;

    if (com.sc_scan_lease_lock != NULL) {
        IOSimpleLockLock(com.sc_scan_lease_lock);
        lease_live = com.sc_scan_lease.owner != IWN_SCAN_LEASE_NONE &&
            com.sc_scan_lease.phase != IWN_SCAN_LEASE_IDLE;
        IOSimpleLockUnlock(com.sc_scan_lease_lock);
    }
    /* A late STOP_SCAN has no firmware UID.  Do not erase the only lower
     * owner marker while its exact physical lease is still live. */
    if (lease_live)
        return;
    com.sc_flags &= ~(IWN_FLAG_SCANNING | IWN_FLAG_BGSCAN);
}

IOReturn ItlIwn::
setMulticastList(IOEthernetAddress *addr, int count)
{
    return kIOReturnSuccess;
}

const char *ItlIwn::
getFirmwareVersion()
{
    return com.fwname;
}

const char *ItlIwn::
getFirmwareName()
{
    return com.fwname;
}

UInt32 ItlIwn::
supportedFeatures()
{
    return kIONetworkFeatureMultiPages;
}

const char *ItlIwn::
getFirmwareCountryCode()
{
    return "ZZ";
}

uint32_t ItlIwn::
getTxQueueSize()
{
    return IWN_TX_RING_COUNT;
}

int16_t ItlIwn::
getBSSNoise()
{
    return com.noise;
}

bool ItlIwn::
is5GBandSupport()
{
    return com.sc_flags & IWN_FLAG_HAS_5GHZ;
}

int ItlIwn::
getTxNSS()
{
    return com.ntxchains;
}

uint8_t ItlIwn::
getTxChainMask()
{
    return com.txchainmask;
}

uint8_t ItlIwn::
getRxChainMask()
{
    return com.rxchainmask;
}

uint32_t ItlIwn::
getLqmBeaconCount()
{
    return com.lqm_beacon_count;
}

struct ieee80211com *ItlIwn::
get80211Controller()
{
    return &com.sc_ic;
}

bool ItlIwn::supportsAPMode() const
{
#if IWN_APGO_FIRMWARE_BACKEND_OPT_IN
#ifdef IEEE80211_APSTA_STATION_EVENT_OPT_OUT
    return com.hw_type != IWN_HW_REV_TYPE_4965 &&
        com.eeprom_pan_capable &&
        (com.tlv_feature_flags & IWN_UCODE_TLV_FLAGS_PAN) != 0;
#else
    return false;
#endif
#else
    return false;
#endif
}

int ItlIwn::iwn_build_ap_rxon(struct iwn_rxon *rxon,
    const struct ItlHalApConfig *config)
{
    if (rxon == NULL || config == NULL) {
        return EINVAL;
    }
    if (config->channel == 0 || config->channel > IEEE80211_CHAN_MAX) {
        return EINVAL;
    }
    if (IEEE80211_IS_MULTICAST(config->bssid) ||
        IEEE80211_ADDR_EQ(config->bssid, etheranyaddr)) {
        return EINVAL;
    }

    struct ieee80211com *ic = &com.sc_ic;
    struct ieee80211_channel *chan = NULL;
    for (struct ieee80211_channel *c = &ic->ic_channels[1];
         c <= &ic->ic_channels[IEEE80211_CHAN_MAX]; c++) {
        if (ieee80211_chan2ieee(ic, c) == config->channel) {
            chan = c;
            break;
        }
    }
    if (chan == NULL) {
        return EINVAL;
    }

    bzero(rxon, sizeof(*rxon));
    /*
     * DVM's AP RXON context is keyed by the AP vif address, not by the
     * concurrently published station address.  The role-7 interface owns
     * config->bssid, and its beacon source address must match RXON node_addr.
     */
    IEEE80211_ADDR_COPY(rxon->myaddr, config->bssid);
    IEEE80211_ADDR_COPY(rxon->bssid, config->bssid);
    /*
     * DVM exposes a concurrent AP through the PAN context.  Its AP device
     * type is CP (7); HOSTAP (1) belongs to a legacy BSS-context mode which
     * the 6x35 firmware does not admit as the second interface.
     */
    rxon->mode = IWN_MODE_CP;
    rxon->chan = static_cast<uint8_t>(config->channel);
    /*
     * Match DVM's first associated WIPAN_RXON exactly.  Operational
     * short-slot/CTS/protection flags are committed later by
     * WIPAN_RXON_ASSOC, after the post-association beacon/EDCA sequence.
     */
    rxon->flags = htole32(IWN_RXON_TSF);
    if (IEEE80211_IS_CHAN_2GHZ(chan)) {
        rxon->flags |= htole32(IWN_RXON_AUTO | IWN_RXON_24GHZ);
        if (ic->ic_flags & IEEE80211_F_USEPROT) {
            rxon->flags |= htole32(IWN_RXON_TGG_PROT);
        }
        rxon->cck_mask = 0x0f;
    } else {
        rxon->cck_mask = 0;
    }
    /*
     * Enabling beaconing marks the AP context associated.  The CP context
     * routes frames addressed to its node/BSSID without promiscuous mode.
     */
    rxon->filter = htole32(IWN_FILTER_BSS);
    rxon->ofdm_mask = 0x15;
    rxon->ht_single_mask = 0xff;
    rxon->ht_dual_mask = 0xff;
    rxon->ht_triple_mask = 0xff;
    /*
     * DVM keeps one receive chain awake while an unassociated PAN context
     * is idle.  Programming every active chain as idle changes 0x2406 into
     * 0x2806 on a 2x2 6235, and the firmware never acknowledges the first
     * WIPAN_RXON.
     */
    rxon->rxchain = htole16(IWN_RXCHAIN_VALID(com.rxchainmask) |
                            IWN_RXCHAIN_MIMO_COUNT(com.nrxchains) |
                            IWN_RXCHAIN_IDLE_COUNT(1));
    return 0;
}

static bool iwn_ap_sta_run_pan_prepare_doorbell(
    struct iwn_softc *sc, void *context)
{
    ItlIwn *that = static_cast<ItlIwn *>(context);

    if (sc == NULL || that == NULL || sc != &that->com ||
        that->apStaRunPanFencePending)
        return false;

    that->apStaRunPanFenceIndex =
        static_cast<uint16_t>(sc->txq[sc->command_queue].cur);
    that->apStaRunPanFencePending = true;
    return true;
}

int ItlIwn::iwn_send_ap_pan_params(
    const struct ItlHalApConfig *config, bool primaryRunFence)
{
    if (config == NULL)
        return EINVAL;

    uint16_t beaconInterval =
        config->beaconInterval != 0 ? config->beaconInterval : 100;
    const uint16_t bssBeaconInterval =
        com.sc_ic.ic_bss != NULL ? com.sc_ic.ic_bss->ni_intval : 0;
    if (bssBeaconInterval > beaconInterval)
        beaconInterval = bssBeaconInterval;
    const uint16_t dtimPeriod =
        config->dtimPeriod != 0 ? config->dtimPeriod : 1;
    /*
     * Match iwlwifi DVM's two-context scheduler, including its otherwise
     * easy-to-miss scan transition.  The primary BSS context must own nearly
     * the whole three-DTIM admission window while it scans or authenticates;
     * a 20 TU slot cannot contain one normal active/passive dwell.  Outside
     * that temporary BSS-priority interval, an unassociated PAN AP owns the
     * admission window.  Once both contexts are associated, DVM stops using
     * either transition bias and splits one beacon interval evenly.  Keeping
     * the 20/280 transition split after RUN can starve a different-channel
     * primary STA until both its q0 data and PAN q5 management traffic stall.
     */
    const uint16_t minimumSlotWidth = 20;
    const uint32_t admissionWindow =
        static_cast<uint32_t>(dtimPeriod) * beaconInterval * 3;
    const uint16_t admissionRemainder = static_cast<uint16_t>(
        admissionWindow > 0xffffU + minimumSlotWidth ?
            0xffffU :
            (admissionWindow > minimumSlotWidth ?
                admissionWindow - minimumSlotWidth : minimumSlotWidth));
    uint16_t bssSlotWidth;
    uint16_t panSlotWidth;
    const bool bssPriorityActive =
        apStaScanPriorityActive || apStaAuthPriorityActive;
    if (bssPriorityActive) {
        bssSlotWidth = admissionRemainder;
        panSlotWidth = minimumSlotWidth;
    } else if (apStaBssAssociated &&
               (apFirmwareStage == IWN_AP_STAGE_FINAL_PAN_PARAMS ||
                apFirmwareStage == IWN_AP_STAGE_RUNNING)) {
        bssSlotWidth = beaconInterval / 2;
        panSlotWidth = beaconInterval - bssSlotWidth;
    } else {
        bssSlotWidth = minimumSlotWidth;
        panSlotWidth = admissionRemainder;
    }
    struct iwn_cmd_wipan_params command;
    bzero(&command, sizeof(command));
    command.flags = htole16(IWN_WIPAN_PARAMS_SLOTTED_MODE);
    command.nslots = 2;
    command.slots[0].type = 0;
    command.slots[0].width = htole16(bssSlotWidth);
    command.slots[1].type = 1;
    command.slots[1].width = htole16(panSlotWidth);
    XYLog("%s: APSTA PAN slots bss=%u pan=%u priority=%u "
          "scan=%u auth=%u bss_associated=%u stage=%u\n",
          com.sc_dev.dv_xname,
          static_cast<unsigned>(bssSlotWidth),
          static_cast<unsigned>(panSlotWidth),
          bssPriorityActive ? 1U : 0U,
          apStaScanPriorityActive ? 1U : 0U,
          apStaAuthPriorityActive ? 1U : 0U,
          apStaBssAssociated ? 1U : 0U,
          static_cast<unsigned>(apFirmwareStage));
    if (primaryRunFence) {
        return iwn_cmd_with_doorbell_hook(
            &com, IWN_CMD_WIPAN_PARAMS, &command, sizeof(command), 1,
            iwn_ap_sta_run_pan_prepare_doorbell, NULL, this);
    }
    return iwn_cmd(
        &com, IWN_CMD_WIPAN_PARAMS, &command, sizeof(command), 1);
}

int ItlIwn::iwn_set_ap_sta_scan_priority(bool active)
{
    if (!apFirmwareTransitionActive ||
        apFirmwareStage != IWN_AP_STAGE_RUNNING)
        return 0;

    const bool previousScan = apStaScanPriorityActive;
    const bool previousAuth = apStaAuthPriorityActive;
    apStaScanPriorityActive = active;
    if (active)
        apStaAuthPriorityActive = false;
    const int error = iwn_send_ap_pan_params(&apFirmwareConfig);
    if (error != 0) {
        apStaScanPriorityActive = previousScan;
        apStaAuthPriorityActive = previousAuth;
    }
    return error;
}

int ItlIwn::iwn_set_ap_sta_auth_priority(bool active)
{
    if (!apFirmwareTransitionActive ||
        apFirmwareStage != IWN_AP_STAGE_RUNNING)
        return 0;

    const bool previous = apStaAuthPriorityActive;
    apStaAuthPriorityActive = active;
    const int error = iwn_send_ap_pan_params(&apFirmwareConfig);
    if (error != 0)
        apStaAuthPriorityActive = previous;
    return error;
}

int ItlIwn::iwn_clear_ap_sta_pan_priority(bool primaryRunFence)
{
    if (!apFirmwareTransitionActive ||
        apFirmwareStage != IWN_AP_STAGE_RUNNING)
        return 0;

    const bool previousScan = apStaScanPriorityActive;
    const bool previousAuth = apStaAuthPriorityActive;
    apStaScanPriorityActive = false;
    apStaAuthPriorityActive = false;
    const int error = iwn_send_ap_pan_params(
        &apFirmwareConfig, primaryRunFence);
    if (error != 0) {
        apStaScanPriorityActive = previousScan;
        apStaAuthPriorityActive = previousAuth;
    }
    return error;
}

void ItlIwn::iwn_note_ap_sta_run_pan_fence(
    int command, uint16_t commandIndex)
{
    if (!apStaRunPanFencePending || command != IWN_CMD_WIPAN_PARAMS ||
        commandIndex != apStaRunPanFenceIndex)
        return;

    apStaRunPanFencePending = false;
    apStaRunPanFenceIndex = 0;
    iwn_set_ap_primary_tx_quiesced(false, true);
    XYLog("%s: APSTA associated BSS command fence completed; "
          "primary TX resumed\n", com.sc_dev.dv_xname);
}

void ItlIwn::iwn_abort_ap_sta_run_pan_fence(bool resumeOutput)
{
    apStaRunPanFencePending = false;
    apStaRunPanFenceIndex = 0;
    iwn_set_ap_primary_tx_quiesced(false, resumeOutput);
}

int ItlIwn::iwn_send_ap_stop_pan_params()
{
    /*
     * Linux DVM tears down an inactive PAN vif by returning the scheduler to
     * WLAN-only service: the BSS slot gets 300 TU and the PAN slot gets zero.
     * This is deliberately separate from iwn_stop(); the concurrently-owned
     * STA context remains initialized and can keep scanning or associating.
     */
    struct iwn_cmd_wipan_params command;
    bzero(&command, sizeof(command));
    command.nslots = 2;
    command.slots[0].type = 0;
    command.slots[0].width = htole16(300);
    command.slots[1].type = 1;
    command.slots[1].width = 0;
    return iwn_cmd(
        &com, IWN_CMD_WIPAN_PARAMS, &command, sizeof(command), 1);
}

int ItlIwn::iwn_add_ap_broadcast_node()
{
    struct iwn_node_info node;
    bzero(&node, sizeof(node));
    IEEE80211_ADDR_COPY(node.macaddr, etherbroadcastaddr);
    node.id = IWN5000_ID_PAN_BROADCAST;
    node.htflags = htole32(IWN_PAN_STATION);
    return com.ops.add_node(&com, &node, 1);
}

int ItlIwn::iwn_send_ap_broadcast_link_quality(int ridx)
{
    struct iwn_cmd_link_quality linkq;
    bzero(&linkq, sizeof(linkq));
    linkq.id = IWN5000_ID_PAN_BROADCAST;
    const uint8_t txant = IWN_LSB(com.txchainmask);
    linkq.antmsk_1stream = txant;
    linkq.antmsk_2stream = IWN_ANT_AB;
    /*
     * Match DVM's zero-initialized PAN broadcast LQ command.  Broadcast
     * management traffic is not aggregated, so the limit is immaterial,
     * but firmware observes the exact transport contract.
     */
    linkq.ampdu_max = IWN_AMPDU_MAX_UNLIMITED;
    linkq.ampdu_threshold = 3;
    linkq.ampdu_limit = htole16(4000);
    const struct iwn_rate *rate = &iwn_rates[ridx];
    for (int index = 0; index < IWN_MAX_TX_RETRIES; index++) {
        linkq.retry[index].plcp = rate->plcp;
        linkq.retry[index].rflags =
            IWN_RFLAG_ANT(txant) |
            (IWN_RIDX_IS_CCK(ridx) ? IWN_RFLAG_CCK : 0);
    }
    return iwn_cmd(
        &com, IWN_CMD_LINK_QUALITY, &linkq, sizeof(linkq), 1);
}

static uint32_t iwn_ap_client_ht_flags(const ItlIwn *that)
{
    uint32_t flags = IWN_PAN_STATION;
    if (that != NULL && that->apClientHt) {
        flags |= IWN_AMDPU_SIZE_FACTOR(
            MIN(static_cast<uint32_t>(
                    that->apClientHtAmpduParams &
                    IEEE80211_AMPDU_PARAM_LE),
                3U));
        flags |= IWN_AMDPU_DENSITY(
            (that->apClientHtAmpduParams &
             IEEE80211_AMPDU_PARAM_SS) >> 2);
    }
    return flags;
}

int ItlIwn::iwn_add_ap_client_node(const uint8_t *macAddress)
{
    if (macAddress == NULL)
        return EINVAL;

    /*
     * DVM assigns the first station associated with a PAN/AP vif to firmware
     * selected dynamic station ID. This command must precede the Association Response:
     * firmware cannot route ACKed unicast data merely from the host-side AID.
     * Publish only the HT20/A-MPDU limits intersected from the station's
     * Association Request.  BA remains disabled until the aggregation layer
     * is materialized, but DVM still needs these peer limits before its
     * Link Quality command can carry MCS rates.
     */
    struct iwn_node_info node;
    bzero(&node, sizeof(node));
    IEEE80211_ADDR_COPY(node.macaddr, macAddress);
    node.id = apClientContext->stationId;
    node.htflags = htole32(iwn_ap_client_ht_flags(this));
    node.htmask = htole32(
        IWN_AMDPU_SIZE_FACTOR_MASK | IWN_AMDPU_DENSITY_MASK |
        IWN_40MHZ_ENABLE | IWN_MIMO_DISABLE);
    return com.ops.add_node(&com, &node, 1);
}

int ItlIwn::iwn_update_ap_client_node()
{
    if (!apClientNodeInstalled)
        return EINVAL;

    /* DVM ADD_STA modify: preserve the selected station ID/PAN ownership while a
     * reassociation replaces the negotiated HT20 limits in place. */
    struct iwn_node_info node;
    bzero(&node, sizeof(node));
    node.control = IWN_NODE_UPDATE;
    node.id = apClientContext->stationId;
    node.htflags = htole32(iwn_ap_client_ht_flags(this));
    node.htmask = htole32(
        IWN_PWR_SAVE | IWN_AMDPU_SIZE_FACTOR_MASK |
        IWN_AMDPU_DENSITY_MASK | IWN_40MHZ_ENABLE |
        IWN_MIMO_DISABLE);
    return com.ops.add_node(&com, &node, 1);
}

int ItlIwn::iwn_remove_ap_client_node(const uint8_t *macAddress)
{
    if (macAddress == NULL)
        return EINVAL;

    iwn_stop_all_ap_client_tx_ba();
    iwn_stop_all_ap_client_rx_ba();
    apClientRxBaMask = 0;

    struct iwn_remove_node node;
    bzero(&node, sizeof(node));
    node.count = 1;
    IEEE80211_ADDR_COPY(node.macaddr, macAddress);
    return iwn_cmd(
        &com, IWN_CMD_REMOVE_NODE, &node, sizeof(node), 1);
}

int ItlIwn::iwn_wake_ap_client_node()
{
    /*
     * Exact DVM iwl_sta_modify_ps_wake() wire contract.  In iwn_node_info,
     * control is ADD_STA.mode, htflags is station_flags and htmask is
     * station_flags_msk. A zero value under the PS mask marks this station
     * awake without disturbing its PAN identity or rate configuration.
     */
    struct iwn_node_info node;
    bzero(&node, sizeof(node));
    node.control = IWN_NODE_UPDATE;
    node.id = apClientContext->stationId;
    node.htmask = htole32(IWN_PWR_SAVE);
    return com.ops.add_node(&com, &node, 1);
}

int ItlIwn::iwn_allow_ap_client_sleep_tx()
{
    /*
     * Exact DVM iwl_sta_modify_sleep_tx_count(..., 1) command.  This is
     * issued for an immediately deliverable frame while the peer's PM bit
     * says asleep; firmware consumes the count for the following TX.
     */
    struct iwn_node_info node;
    bzero(&node, sizeof(node));
    node.control = IWN_NODE_UPDATE;
    node.id = apClientContext->stationId;
    node.flags = IWN_FLAG_SET_SLEEP_TX_COUNT;
    node.htflags = htole32(IWN_PWR_SAVE);
    node.htmask = htole32(IWN_PWR_SAVE);
    node.sleep_tx_count = htole16(1);
    return com.ops.add_node(&com, &node, 1);
}

bool ItlIwn::iwn_ap_rate_feedback_matches(
    struct IwnApClientRuntime *client, uint8_t mcs, uint8_t rflags)
{
    if (client == NULL)
        return false;
    iwn_select_ap_client(client);
    struct IwnApRateControlRuntime *rateControl =
        &client->rateControl;
    const bool expectedSgi =
        (client->htCapabilities & IEEE80211_HTCAP_SGI20) != 0;
    const bool matches = rateControl->initialized &&
        (rflags & IWN_RFLAG_MCS) != 0 &&
        mcs == rateControl->selectedMcs &&
        ((rflags & IWN_RFLAG_SGI) != 0) == expectedSgi;
    if (matches) {
        rateControl->missedRateCount = 0;
        return true;
    }

    /* rs_tx_status() ignores a completion whose initial rate no longer
     * matches rs_table[0].  After fifteen consecutive mismatches it republishes
     * the current LQ table in case the firmware missed a command. */
    if (rateControl->missedRateCount < UINT8_MAX)
        rateControl->missedRateCount++;
    if (rateControl->missedRateCount > 15 &&
        rateControl->initialized && client->associated &&
        !client->commandPending && !rateControl->linkQualityPending) {
        rateControl->missedRateCount = 0;
        rateControl->linkQualityPending = true;
        const int error = iwn_send_ap_client_link_quality();
        if (error != 0)
            rateControl->linkQualityPending = false;
        XYLog("%s: IWN AP DVM rate feedback resync id=%u "
              "expected_mcs=%u observed=0x%02x/0x%02x error=%d\n",
              com.sc_dev.dv_xname,
              static_cast<unsigned>(client->stationId),
              static_cast<unsigned>(rateControl->selectedMcs),
              static_cast<unsigned>(mcs),
              static_cast<unsigned>(rflags), error);
    }
    return false;
}

int ItlIwn::iwn_ap_rate_control_feedback(
    struct IwnApClientRuntime *client, uint8_t mcs, uint8_t rflags,
    uint16_t attempts, uint16_t successes, bool aggregated,
    uint32_t generation)
{
    if (client == NULL || attempts == 0 || !client->ht)
        return EINVAL;
    iwn_select_ap_client(client);
    struct IwnApRateControlRuntime *rateControl =
        &client->rateControl;
    if (!rateControl->initialized ||
        generation != rateControl->generation ||
        mcs != rateControl->selectedMcs ||
        (rflags & IWN_RFLAG_MCS) == 0 ||
        !iwn_ap_dvm_mcs_supported(client, mcs)) {
        return 0;
    }

    /* DVM changes its expected-throughput table when aggregation starts or
     * stops and clears the old windows because those samples are not
     * comparable. */
    if (!rateControl->feedbackModeInitialized ||
        rateControl->aggregateFeedback != aggregated) {
        iwn_ap_dvm_clear_rate_windows(rateControl);
        rateControl->feedbackModeInitialized = true;
        rateControl->aggregateFeedback = aggregated;
    }

    struct IwnApRateWindow *window =
        &rateControl->windows[mcs];
    iwn_ap_dvm_collect_rate_window(
        window, attempts, successes,
        iwn_ap_dvm_expected_throughput(client, mcs, aggregated));
    const uint8_t failures =
        static_cast<uint8_t>(window->attempts - window->successes);
    if ((failures < 6 && window->successes < 8) ||
        window->averageThroughput < 0 ||
        rateControl->linkQualityPending || client->commandPending) {
        return 0;
    }

    const int familyLow = client->htNss > 1 ? 8 : 0;
    const int familyHigh = client->htNss > 1 ? 15 : 7;
    int lowerMcs = -1;
    int higherMcs = -1;
    for (int candidate = static_cast<int>(mcs) - 1;
         candidate >= familyLow; candidate--) {
        if (iwn_ap_dvm_mcs_supported(client, candidate)) {
            lowerMcs = candidate;
            break;
        }
    }
    for (int candidate = static_cast<int>(mcs) + 1;
         candidate <= familyHigh; candidate++) {
        if (iwn_ap_dvm_mcs_supported(client, candidate)) {
            higherMcs = candidate;
            break;
        }
    }

    const int32_t currentThroughput = window->averageThroughput;
    const int32_t lowerThroughput = lowerMcs >= 0 ?
        rateControl->windows[lowerMcs].averageThroughput : -1;
    const int32_t higherThroughput = higherMcs >= 0 ?
        rateControl->windows[higherMcs].averageThroughput : -1;
    const uint16_t successRatio = window->successRatio;
    int action = 0;

    if (successRatio <= 128U * 15U || currentThroughput == 0) {
        action = -1;
    } else if (lowerThroughput < 0 && higherThroughput < 0) {
        if (higherMcs >= 0 && successRatio >= 128U * 50U)
            action = 1;
    } else if (lowerThroughput >= 0 && higherThroughput >= 0 &&
               lowerThroughput < currentThroughput &&
               higherThroughput < currentThroughput) {
        action = 0;
    } else if (higherThroughput >= 0) {
        if (higherThroughput > currentThroughput &&
            successRatio >= 128U * 50U)
            action = 1;
    } else if (lowerThroughput >= 0) {
        if (lowerThroughput > currentThroughput)
            action = -1;
        else if (higherMcs >= 0 && successRatio >= 128U * 50U)
            action = 1;
    }

    /* Match DVM's guard against downscaling a rate which is succeeding well
     * or still out-throughputting the ideal lower adjacent rate. */
    if (action < 0 && lowerMcs >= 0 &&
        (successRatio > 128U * 85U ||
         currentThroughput > 100 *
            iwn_ap_dvm_expected_throughput(
                client, static_cast<uint8_t>(lowerMcs), aggregated))) {
        action = 0;
    }

    const int nextMcs = action < 0 ? lowerMcs :
        (action > 0 ? higherMcs : -1);
    if (nextMcs < 0)
        return 0;

    const uint8_t previousMcs = rateControl->selectedMcs;
    const uint32_t previousGeneration = rateControl->generation;
    rateControl->selectedMcs = static_cast<uint8_t>(nextMcs);
    if (++rateControl->generation == 0)
        rateControl->generation = 1;
    rateControl->linkQualityPending = true;
    bzero(rateControl->pendingAggregate,
          sizeof(rateControl->pendingAggregate));
    const int error = iwn_send_ap_client_link_quality();
    if (error != 0) {
        rateControl->selectedMcs = previousMcs;
        rateControl->generation = previousGeneration;
        rateControl->linkQualityPending = false;
        return error;
    }
    XYLog("%s: IWN AP DVM rate id=%u MCS%u->MCS%u "
          "window=%u/%u ratio=%u generation=%u\n",
          com.sc_dev.dv_xname,
          static_cast<unsigned>(client->stationId),
          static_cast<unsigned>(previousMcs),
          static_cast<unsigned>(nextMcs),
          static_cast<unsigned>(window->successes),
          static_cast<unsigned>(window->attempts),
          static_cast<unsigned>(successRatio),
          static_cast<unsigned>(rateControl->generation));
    return 0;
}

int ItlIwn::iwn_send_ap_client_link_quality()
{
    struct iwn_cmd_link_quality linkq;
    bzero(&linkq, sizeof(linkq));
    linkq.id = apClientContext->stationId;
    const uint8_t txant = IWN_LSB(com.txchainmask);
    linkq.antmsk_1stream = txant;
    linkq.antmsk_2stream = IWN_ANT_AB;
    /* DVM starts at the hardware default and, after every successful ADDBA,
     * republishes the minimum negotiated buffer size across the station's
     * active TIDs.  Firmware has one aggregate limit per station even when
     * the peer advertises a different window for each TID. */
    uint8_t aggregateLimit = apClientHt ? IWN_AMPDU_MAX :
        IWN_AMPDU_MAX_NO_AGG;
    if (apClientHt) {
        for (uint8_t tid = 0; tid < IWN_NUM_AMPDU_TID; tid++) {
            const struct ItlApTxBaRuntime *txBa = &apClientTxBa[tid];
            if (txBa->state != kItlApTxBaAgreed)
                continue;
            uint8_t negotiated = static_cast<uint8_t>(MIN(
                txBa->window, static_cast<uint16_t>(IWN_AMPDU_MAX)));
            if (negotiated == 0)
                negotiated = IWN_AMPDU_MAX;
            aggregateLimit = MIN(aggregateLimit, negotiated);
        }
    }
    linkq.ampdu_max = aggregateLimit;
    /* 6x35 belongs to the DVM families whose HT aggregate transport uses
     * RTS/CTS.  Linux publishes both sides of that contract together:
     * TX_CMD carries PROT_REQUIRE and the post-ADDBA LQ table carries the
     * station TLC_RTS flag.  A protected aggregate with only the TX_CMD bit
     * can remain owned by SCD without ever reaching a transmit FIFO. */
    if (apClientHt && apClientTxBaMask != 0 &&
        iwn_dvm_use_rts_for_aggregation(&com)) {
        linkq.flags |= IWN_LINK_QUAL_FLAGS_SET_STA_TLC_RTS;
    }
    linkq.ampdu_threshold = 3;
    linkq.ampdu_limit = htole16(4000);

    static const uint8_t legacyRidx[] = {
        IWN_RATE_1M_INDEX, IWN_RATE_2M_INDEX,
        IWN_RATE_5M_INDEX, IWN_RATE_11M_INDEX,
        IWN_RATE_6M_INDEX, IWN_RATE_9M_INDEX,
        IWN_RATE_12M_INDEX, IWN_RATE_18M_INDEX,
        IWN_RATE_24M_INDEX, IWN_RATE_36M_INDEX,
        IWN_RATE_48M_INDEX, IWN_RATE_54M_INDEX
    };
    int lowestLegacy = apFirmwareConfig.channel <= 14 ? 0 : 4;
    while (lowestLegacy < static_cast<int>(nitems(legacyRidx)) &&
           (apClientLegacyRateMask & (1U << lowestLegacy)) == 0)
        lowestLegacy++;
    if (lowestLegacy == static_cast<int>(nitems(legacyRidx)))
        return EINVAL;

    int retry = 0;
    if (apClientHt) {
        const bool mimo = apClientHtNss > 1;
        const int firstMcs = mimo ? 15 : 7;
        const int lastMcs = mimo ? 8 : 0;
        const uint8_t antennaMask = mimo ?
            static_cast<uint8_t>(com.txchainmask & IWN_ANT_AB) : txant;
        if (!apClientRateControl.initialized ||
            !iwn_ap_dvm_mcs_supported(
                apClientContext, apClientRateControl.selectedMcs)) {
            iwn_reset_ap_client_rate_control(apClientContext);
            for (int mcs = lastMcs; mcs <= firstMcs; mcs++) {
                if (iwn_ap_dvm_mcs_supported(apClientContext, mcs)) {
                    apClientRateControl.selectedMcs =
                        static_cast<uint8_t>(mcs);
                    apClientRateControl.initialized = true;
                    break;
                }
            }
            if (apClientRateControl.initialized) {
                XYLog("%s: IWN AP DVM rate initialized id=%u MCS%u "
                      "generation=%u\n",
                      com.sc_dev.dv_xname,
                      static_cast<unsigned>(
                          apClientContext->stationId),
                      static_cast<unsigned>(
                          apClientRateControl.selectedMcs),
                      static_cast<unsigned>(
                          apClientRateControl.generation));
            }
        }
        const int selectedMcs = apClientRateControl.initialized ?
            apClientRateControl.selectedMcs : -1;
        if (selectedMcs < 0)
            return EINVAL;

        /*
         * Match rs_fill_link_cmd() in Intel DVM.  The current HT rate is
         * attempted three times, then one lower HT rate is attempted three
         * times.  Only after those two groups does the table cross into a
         * legacy ladder.  Publishing every HT MCS once and filling the
         * remaining half with the minimum basic rate made one ordinary fade
         * traverse MCS15..8 and then spend eight attempts at 1 Mbps.  Apart
         * from being unlike DVM, that inflated aggregate retry latency enough
         * to hold the PAN queue at its high-water mark.
         */
        int lowerMcs = selectedMcs;
        for (int mcs = selectedMcs - 1; mcs >= lastMcs; mcs--) {
            const size_t stream = static_cast<size_t>(mcs / 8);
            const uint8_t bit = static_cast<uint8_t>(1U << (mcs & 7));
            if ((apClientHtMcs[stream] & bit) != 0) {
                lowerMcs = mcs;
                break;
            }
        }
        const int htMcs[2] = { selectedMcs, lowerMcs };
        for (size_t group = 0; group < nitems(htMcs); group++) {
            const struct iwn_rate *rate =
                &iwn_rates[iwn_mcs2ridx[htMcs[group]]];
            for (int attempt = 0;
                 attempt < 3 && retry < IWN_MAX_TX_RETRIES;
                 attempt++) {
                linkq.retry[retry].plcp = rate->ht_plcp;
                linkq.retry[retry].rflags =
                    IWN_RFLAG_MCS | IWN_RFLAG_ANT(antennaMask);
                if ((apClientHtCapabilities &
                     IEEE80211_HTCAP_SGI20) != 0) {
                    linkq.retry[retry].rflags |= IWN_RFLAG_SGI;
                }
                retry++;
            }
        }
        if (mimo)
            linkq.mimo = static_cast<uint8_t>(retry);

        /* rs_ht_to_legacy[] maps the base MCS modulation to the closest
         * supported legacy rate before rs_get_lower_rate() walks prev_rs.
         * Indices here address legacyRidx/apClientLegacyRateMask. */
        static const uint8_t htToLegacy[] = {
            4, 5, 6, 7, 8, 9, 10, 11
        };
        static const int8_t legacyPrevious[] = {
            -1, 0, 1, 5, 2, 4, 3, 6, 7, 8, 9, 10
        };
        int legacyIndex = htToLegacy[lowerMcs & 7];
        while (legacyIndex >= 0 && retry < IWN_MAX_TX_RETRIES) {
            if ((apClientLegacyRateMask & (1U << legacyIndex)) != 0) {
                const int ridx = legacyRidx[legacyIndex];
                linkq.retry[retry].plcp = iwn_rates[ridx].plcp;
                linkq.retry[retry].rflags = IWN_RFLAG_ANT(txant) |
                    (IWN_RIDX_IS_CCK(ridx) ? IWN_RFLAG_CCK : 0);
                retry++;
            }
            legacyIndex = legacyPrevious[legacyIndex];
        }
    } else {
        for (int rateIndex = static_cast<int>(nitems(legacyRidx)) - 1;
             rateIndex >= lowestLegacy && retry < IWN_MAX_TX_RETRIES;
             rateIndex--) {
            if ((apClientLegacyRateMask & (1U << rateIndex)) == 0)
                continue;
            const int ridx = legacyRidx[rateIndex];
            linkq.retry[retry].plcp = iwn_rates[ridx].plcp;
            linkq.retry[retry].rflags = IWN_RFLAG_ANT(txant) |
                (IWN_RIDX_IS_CCK(ridx) ? IWN_RFLAG_CCK : 0);
            retry++;
        }
    }
    const int fallbackRidx = legacyRidx[lowestLegacy];
    while (retry < IWN_MAX_TX_RETRIES) {
        linkq.retry[retry].plcp = iwn_rates[fallbackRidx].plcp;
        linkq.retry[retry].rflags = IWN_RFLAG_ANT(txant) |
            (IWN_RIDX_IS_CCK(fallbackRidx) ? IWN_RFLAG_CCK : 0);
        retry++;
    }
    return iwn_cmd(
        &com, IWN_CMD_LINK_QUALITY, &linkq, sizeof(linkq), 1);
}

int ItlIwn::iwn_send_ap_assoc_success()
{
    if (!apClientNodeInstalled ||
        apClientMaterializationStage !=
            IWN_AP_CLIENT_MATERIALIZATION_LINK_QUALITY)
        return EINVAL;

    const uint8_t supportedRates2g[] = {
        0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24
    };
    const uint8_t supportedRates5g[] = {
        0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c
    };
    const uint8_t extendedRates[] = { 0x30, 0x48, 0x60, 0x6c };
    const bool is2g = apFirmwareConfig.channel <= 14;
    const size_t htLength = apClientHt ?
        kItlHalApHtCapabilityIELength +
        kItlHalApHtOperationIELength : 0;
    uint8_t response[
        sizeof(struct ieee80211_frame) + 6 +
        2 + sizeof(supportedRates2g) +
        2 + sizeof(extendedRates) +
        sizeof(apFirmwareRsnIE) +
        kItlHalApHtCapabilityIELength +
        kItlHalApHtOperationIELength +
        sizeof(kItlHalApWmmParameterIE)];
    bzero(response, sizeof(response));
    struct ieee80211_frame *wh =
        reinterpret_cast<struct ieee80211_frame *>(response);
    wh->i_fc[0] = IEEE80211_FC0_VERSION_0 |
        IEEE80211_FC0_TYPE_MGT |
        (apClientReassociationPending ?
            IEEE80211_FC0_SUBTYPE_REASSOC_RESP :
            IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
    wh->i_fc[1] = IEEE80211_FC1_DIR_NODS;
    IEEE80211_ADDR_COPY(wh->i_addr1, apClientMac);
    IEEE80211_ADDR_COPY(wh->i_addr2, apFirmwareConfig.bssid);
    IEEE80211_ADDR_COPY(wh->i_addr3, apFirmwareConfig.bssid);

    uint8_t *out = response + sizeof(*wh);
    LE_WRITE_2(out, IEEE80211_CAPINFO_ESS |
                    (apFirmwareConfig.rsnIELength != 0 ?
                        IEEE80211_CAPINFO_PRIVACY : 0) |
                    (is2g ?
                        IEEE80211_CAPINFO_SHORT_SLOTTIME : 0));
    out += 2;
    LE_WRITE_2(out, IEEE80211_STATUS_SUCCESS);
    out += 2;
    LE_WRITE_2(out, apClientAid | 0xc000);
    out += 2;
    *out++ = IEEE80211_ELEMID_RATES;
    *out++ = sizeof(supportedRates2g);
    memcpy(out, is2g ? supportedRates2g : supportedRates5g,
           sizeof(supportedRates2g));
    out += sizeof(supportedRates2g);
    if (is2g) {
        *out++ = IEEE80211_ELEMID_XRATES;
        *out++ = sizeof(extendedRates);
        memcpy(out, extendedRates, sizeof(extendedRates));
        out += sizeof(extendedRates);
    }
    if (apFirmwareConfig.rsnIELength != 0) {
        memcpy(out, apFirmwareRsnIE, apFirmwareConfig.rsnIELength);
        out += apFirmwareConfig.rsnIELength;
    }
    if (apClientHt) {
        out += itl_hal_ap_build_ht_capability_ie(
            out, static_cast<size_t>(response + sizeof(response) - out),
            &apFirmwareConfig);
        out += itl_hal_ap_build_ht_operation_ie(
            out, static_cast<size_t>(response + sizeof(response) - out),
            &apFirmwareConfig);
    }
    if (apClientQos) {
        memcpy(out, kItlHalApWmmParameterIE,
               sizeof(kItlHalApWmmParameterIE));
        out += sizeof(kItlHalApWmmParameterIE);
    }

    if (static_cast<size_t>(out - response) !=
        sizeof(struct ieee80211_frame) + 6 +
        2 + sizeof(supportedRates2g) +
        (is2g ? 2 + sizeof(extendedRates) : 0) +
        apFirmwareConfig.rsnIELength + htLength +
        (apClientQos ? sizeof(kItlHalApWmmParameterIE) : 0))
        return EINVAL;

    const bool reassociation = apClientReassociationPending;
    const int error = iwn_send_ap_mgmt_frame(
        response, static_cast<size_t>(out - response));
    if (error != 0)
        return error;

    apClientReassociationPending = false;
    apClientAssociated = true;
    apClientAuthorized = apFirmwareConfig.rsnIELength == 0;
    apClientPowerSave = false;
    apRsnState = apClientAuthorized ?
        IWN_AP_RSN_AUTHORIZED : IWN_AP_RSN_DISABLED;
    apReplayCounter = 0;
    apPairwiseTxPn = 0;
    bzero(apPairwiseRxPn, sizeof(apPairwiseRxPn));
    explicit_bzero(&apPtk, sizeof(apPtk));
    iwn_publish_ap_station_event(
        apClientMac, apClientRsnIELength != 0 ? apClientRsnIE : NULL,
        apClientRsnIELength,
        reassociation ? IEEE80211_APSTA_EVENT_REASSOC :
                        IEEE80211_APSTA_EVENT_ASSOC);
#if __IO80211_TARGET >= __MAC_26_0
    /*
     * AP Skywalk queues stay stopped until a firmware station exists.  The
     * association response is the first point at which both the PAN node and
     * its unicast queue are usable, so explicitly publish that transition to
     * the dequeue side just as the IWM/IWX AP backends do.
     */
    airportItlwmRequestAPTxDequeue(getController());
#endif
    return 0;
}

int ItlIwn::iwn_send_ap_sensitivity()
{
    /*
     * RX sensitivity is device-global even though RXON is per context.
     * Linux DVM reinitializes the 6x35 work table immediately after the
     * associated WIPAN_RXON and before TX power/CAM.  The older generic iwn
     * table has different 6000-family energy and Barker-MRC values, so build
     * the exact live-6235 command here instead of inheriting the idle BSS
     * context's calibration state.
     */
    struct iwn_enhanced_sensitivity_cmd command;
    bzero(&command, sizeof(command));
    command.which = htole16(IWN_SENSITIVITY_WORKTBL);
    command.energy_cck = htole16(110);
    command.energy_ofdm = htole16(110);
    command.corr_ofdm_x1 = htole16(105);
    command.corr_ofdm_mrc_x1 = htole16(192);
    command.corr_cck_mrc_x4 = htole16(160);
    command.corr_ofdm_x4 = htole16(80);
    command.corr_ofdm_mrc_x4 = htole16(128);
    command.corr_barker = htole16(190);
    command.corr_barker_mrc = htole16(336);
    command.corr_cck_x4 = htole16(125);
    command.energy_ofdm_th = htole16(62);

    int commandLength = sizeof(struct iwn_sensitivity_cmd);
    if (com.sc_flags & IWN_FLAG_ENH_SENS) {
        commandLength = sizeof(command);
        command.ofdm_det_slope_mrc = htole16(668);
        command.ofdm_det_icept_mrc = htole16(4);
        command.ofdm_det_slope = htole16(486);
        command.ofdm_det_icept = htole16(37);
        command.cck_det_slope_mrc = htole16(853);
        command.cck_det_icept_mrc = htole16(4);
        command.cck_det_slope = htole16(476);
        command.cck_det_icept = htole16(99);
    }
    return iwn_cmd(
        &com, IWN_CMD_SET_SENSITIVITY,
        &command, commandLength, 1);
}

int ItlIwn::iwn_send_ap_timing(const struct ItlHalApConfig *config)
{
    if (config == NULL)
        return EINVAL;

    uint16_t beaconInterval =
        config->beaconInterval != 0 ? config->beaconInterval : 100;
    struct iwn_cmd_timing command;
    bzero(&command, sizeof(command));
    bool retainedBssTiming = false;

    /*
     * Linux DVM's iwl_send_rxon_timing() does not give a concurrently
     * beaconing PAN context an unrelated zero-epoch TBTT.  When the primary
     * BSS is already associated, the PAN timing inherits that BSS beacon
     * interval and uses its last received TSF to schedule the next TBTT.
     *
     * A standalone AP tolerates a zero timestamp, but in APSTA that creates
     * two formally valid same-channel contexts with unrelated beacon
     * epochs.  6x35 then services PAN while the BSS misses its beacons and
     * Tahoe keeps publishing a stale association until DHCP falls back to
     * link-local.  Preserve the firmware's shared timing owner instead.
     */
    struct ieee80211_node *bss = com.sc_ic.ic_bss;
    if (apStaBssAssociated && bss != NULL && bss->ni_intval != 0) {
        beaconInterval = bss->ni_intval;
        memcpy(&command.tstamp, bss->ni_tstamp, sizeof(command.tstamp));
        retainedBssTiming = true;
    }
    command.bintval = htole16(beaconInterval);
    const uint64_t intervalUsec =
        static_cast<uint64_t>(beaconInterval) * IEEE80211_DUR_TU;
    const uint64_t timestamp = letoh64(command.tstamp);
    const uint64_t remainder = timestamp % intervalUsec;
    command.binitval = htole32(static_cast<uint32_t>(
        intervalUsec - remainder));
    command.lintval = htole16(10);
    command.dtim_period =
        config->dtimPeriod != 0 ? config->dtimPeriod : 1;
    XYLog("%s: AP PAN timing source=%s interval=%u init=%u\n",
          com.sc_dev.dv_xname,
          retainedBssTiming ? "retained-BSS" : "standalone",
          static_cast<unsigned>(beaconInterval),
          static_cast<unsigned>(le32toh(command.binitval)));
    return iwn_cmd(
        &com, IWN_CMD_WIPAN_TIMING, &command, sizeof(command), 1);
}

int ItlIwn::iwn_send_ap_edca(bool accessPointValues)
{
#define IWN_AP_EXP2(x) ((1 << (x)) - 1)
    struct iwn_edca_params command;
    bzero(&command, sizeof(command));
    /*
     * WIPAN_QOS_PARAM is not a replace-by-presence command.  DVM only
     * commits the supplied access categories when UPDATE is asserted.
     * Without it the command still gets a successful generic reply, while
     * the PAN transmit FIFOs retain their reset QoS state.
     */
    /*
     * The final, transmitting 6235 APSTA state uses UPDATE|TGN (0x3).
     * UPDATE alone appears in the earlier pre-beacon staging commands, but
     * the reference enables TGN before q7 Probe/Auth traffic is admitted.
     */
    command.flags = htole32(IWN_EDCA_UPDATE | IWN_EDCA_FLG_TGN);
    static const uint8_t firmwareAcToNet80211[EDCA_NUM_AC] = {
        EDCA_AC_BK, EDCA_AC_BE, EDCA_AC_VI, EDCA_AC_VO
    };
    static const struct ieee80211_edca_ac_params qapEdca[EDCA_NUM_AC] = {
        { 4, 10, 7,  0 },
        { 4,  6, 3,  0 },
        { 3,  4, 1, 94 },
        { 2,  3, 1, 47 }
    };
    for (int firmwareAc = 0; firmwareAc < EDCA_NUM_AC; firmwareAc++) {
        const int aci = firmwareAcToNet80211[firmwareAc];
        const struct ieee80211_edca_ac_params *ac =
            accessPointValues ?
            &qapEdca[firmwareAc] :
            &com.sc_ic.ic_edca_ac[aci];
        command.ac[firmwareAc].aifsn = ac->ac_aifsn;
        command.ac[firmwareAc].cwmin =
            htole16(IWN_AP_EXP2(ac->ac_ecwmin));
        command.ac[firmwareAc].cwmax =
            htole16(IWN_AP_EXP2(ac->ac_ecwmax));
        command.ac[firmwareAc].txoplimit =
            htole16(IEEE80211_TXOP_TO_US(ac->ac_txoplimit));
    }
    const int error = iwn_cmd(
        &com, IWN_CMD_WIPAN_EDCA_PARAMS, &command, sizeof(command), 1);
#undef IWN_AP_EXP2
    return error;
}

int ItlIwn::iwn_send_ap_beacon(const struct ItlHalApConfig *config)
{
    const size_t fixedLength = sizeof(struct ieee80211_frame) + 12;
    if (config == NULL || config->beaconTemplate == NULL ||
        config->beaconTemplateLength < fixedLength ||
        config->beaconTemplateLength >
            MCLBYTES - sizeof(struct iwn_cmd_beacon)) {
        return EINVAL;
    }

    const size_t commandLength =
        sizeof(struct iwn_cmd_beacon) + config->beaconTemplateLength;
    struct iwn_cmd_beacon *command =
        static_cast<struct iwn_cmd_beacon *>(
            malloc(commandLength, M_DEVBUF, M_NOWAIT | M_ZERO));
    if (command == NULL)
        return ENOMEM;

    command->tx.len =
        htole16(static_cast<uint16_t>(config->beaconTemplateLength));
    command->tx.flags = htole32(
        IWN_TX_AUTO_SEQ | IWN_TX_INSERT_TSTAMP | IWN_TX_LINKQ);
    command->tx.id = IWN5000_ID_PAN_BROADCAST;
    command->tx.lifetime = htole32(IWN_LIFETIME_INFINITE);
    /*
     * TX_LINKQ makes the PAN broadcast station's retry table authoritative.
     * Linux therefore leaves both retry limits and the low rate byte zero;
     * only modulation/antenna flags remain in this template command.
     */
    command->tx.rts_ntries = 0;
    command->tx.data_ntries = 0;
    if (config->channel <= 14) {
        command->tx.plcp = 0;
        command->tx.rflags = IWN_RFLAG_CCK;
    } else {
        command->tx.plcp = 0;
    }
    command->tx.rflags |= IWN_RFLAG_ANT(IWN_LSB(com.txchainmask));

    const uint8_t *templateBytes =
        static_cast<const uint8_t *>(config->beaconTemplate);
    memcpy(command->frame, templateBytes, config->beaconTemplateLength);
    size_t offset = fixedLength;
    while (offset + 2 <= config->beaconTemplateLength) {
        const size_t elementLength =
            static_cast<size_t>(templateBytes[offset + 1]);
        if (offset + 2 + elementLength > config->beaconTemplateLength)
            break;
        if (templateBytes[offset] == IEEE80211_ELEMID_TIM) {
            command->tim_idx = htole16(static_cast<uint16_t>(offset));
            command->tim_size = static_cast<uint8_t>(elementLength);
            break;
        }
        offset += 2 + elementLength;
    }
    if (command->tim_idx == 0) {
        explicit_bzero(command, commandLength);
        ::free(command);
        return EINVAL;
    }

    const int error = iwn_cmd(
        &com, IWN_CMD_TX_BEACON, command,
        static_cast<int>(commandLength), 1);
    explicit_bzero(command, commandLength);
    ::free(command);
    return error;
}

int ItlIwn::iwn_update_ap_tim(bool pending)
{
    if (!apFirmwareTransitionActive ||
        apFirmwareStage != IWN_AP_STAGE_RUNNING ||
        apClientAid == 0 ||
        apFirmwareConfig.beaconTemplate != apFirmwareBeacon) {
        return EINVAL;
    }
    if (apTimSet == pending)
        return 0;

    const size_t fixedLength = sizeof(struct ieee80211_frame) + 12;
    size_t offset = fixedLength;
    while (offset + 2 <= apFirmwareConfig.beaconTemplateLength) {
        const size_t elementLength = apFirmwareBeacon[offset + 1];
        if (offset + 2 + elementLength >
            apFirmwareConfig.beaconTemplateLength) {
            return EINVAL;
        }
        if (apFirmwareBeacon[offset] == IEEE80211_ELEMID_TIM) {
            if (elementLength < 4)
                return EINVAL;
            const size_t bitmapOffset =
                apFirmwareBeacon[offset + 4] & 0xfe;
            const size_t aidByte = apClientAid >> 3;
            const size_t bitmapLength = elementLength - 3;
            if (aidByte < bitmapOffset ||
                aidByte >= bitmapOffset + bitmapLength) {
                return ENOTSUP;
            }
            uint8_t *aidBitmap =
                &apFirmwareBeacon[offset + 5 +
                    aidByte - bitmapOffset];
            const uint8_t oldValue = *aidBitmap;
            const uint8_t aidMask =
                static_cast<uint8_t>(1U << (apClientAid & 7));
            if (pending)
                *aidBitmap |= aidMask;
            else
                *aidBitmap &= ~aidMask;
            const int error =
                iwn_send_ap_beacon(&apFirmwareConfig);
            if (error != 0) {
                *aidBitmap = oldValue;
                return error;
            }
            apTimSet = pending;
            return 0;
        }
        offset += 2 + elementLength;
    }
    return ENOENT;
}

int ItlIwn::iwn_queue_ap_ps_packet(mbuf_t packet, bool atFront)
{
    if (packet == NULL || !apPsQueueReady)
        return EINVAL;
    if (apPsQueueCount >= IWN_AP_PS_QUEUE_LEN)
        return ENOBUFS;

    const uint8_t oldHead = apPsQueueHead;
    const uint8_t oldTail = apPsQueueTail;
    if (atFront) {
        apPsQueueHead =
            (apPsQueueHead + IWN_AP_PS_QUEUE_LEN - 1) %
                IWN_AP_PS_QUEUE_LEN;
        apPsQueue[apPsQueueHead] = packet;
    } else {
        apPsQueue[apPsQueueTail] = packet;
        apPsQueueTail =
            (apPsQueueTail + 1) % IWN_AP_PS_QUEUE_LEN;
    }
    apPsQueueCount++;

    if (apPsQueueCount == 1) {
        const int timError = iwn_update_ap_tim(true);
        if (timError != 0) {
            if (atFront)
                apPsQueue[apPsQueueHead] = NULL;
            else
                apPsQueue[oldTail] = NULL;
            apPsQueueHead = oldHead;
            apPsQueueTail = oldTail;
            apPsQueueCount = 0;
            return timError;
        }
    }
    return 0;
}

void ItlIwn::iwn_drain_ap_ps_queue()
{
    while (!apClientPowerSave && apPsQueueCount != 0) {
        mbuf_t packet = apPsQueue[apPsQueueHead];
        const bool moreData = apPsQueueCount > 1;
        const int error =
            iwn_send_ap_data_frame(packet, moreData, false);
        if (error != 0)
            break;
        apPsQueue[apPsQueueHead] = NULL;
        apPsQueueHead =
            (apPsQueueHead + 1) % IWN_AP_PS_QUEUE_LEN;
        apPsQueueCount--;
    }
    if (apPsQueueCount == 0 && apTimSet) {
        const int timError = iwn_update_ap_tim(false);
        if (timError != 0)
            XYLog("%s: AP awake-drain TIM clear failed error=%d\n",
                  com.sc_dev.dv_xname, timError);
    }
}

int ItlIwn::iwn_send_ap_rxon_assoc()
{
    struct iwn_rxon_assoc command;
    bzero(&command, sizeof(command));
    command.flags = apFirmwareRxon.flags;
    if (apFirmwareConfig.channel <= 14) {
        /*
         * DVM's AP setup first admits the CP context with the minimal
         * 2.4-GHz flags, then commits the operational protection policy
         * through WIPAN_RXON_ASSOC after the final beacon/QoS update.
         */
        command.flags |= htole32(
            IWN_RXON_SHSLOT | IWN_RXON_TGG_PROT |
            IWN_RXON_CTS_TO_SELF);
    }
    command.filter = apFirmwareRxon.filter;
    command.ofdm_mask = apFirmwareRxon.ofdm_mask;
    command.cck_mask = apFirmwareRxon.cck_mask;
    command.ht_single_mask = apFirmwareRxon.ht_single_mask;
    command.ht_dual_mask = apFirmwareRxon.ht_dual_mask;
    command.ht_triple_mask = apFirmwareRxon.ht_triple_mask;
    command.rxchain = apFirmwareRxon.rxchain;
    command.acquisition = apFirmwareRxon.acquisition;
    return iwn_cmd(
        &com, IWN_CMD_WIPAN_RXON_ASSOC,
        &command, sizeof(command), 1);
}

void ItlIwn::iwn_continue_ap_after_deactivation()
{
    if (!apFirmwareTransitionActive ||
        apFirmwarePostDeactivateQueued ||
        !apFirmwareDeactivationReplySeen ||
        !apFirmwareDeactivationNotificationSeen) {
        return;
    }

    apFirmwarePostDeactivateQueued = true;
    apFirmwareStage = IWN_AP_STAGE_TIMING;
    const int error = iwn_send_ap_timing(&apFirmwareConfig);
    if (error != 0) {
        XYLog("%s: AP timing command queue failed error=%d\n",
              com.sc_dev.dv_xname, error);
        apFirmwareTransitionActive = false;
        apFirmwareStage = IWN_AP_STAGE_IDLE;
        return;
    }
    XYLog("%s: AP post-deactivation timing queued channel=%u\n",
          com.sc_dev.dv_xname, apFirmwareConfig.channel);
}

void ItlIwn::iwn_note_ap_firmware_event(
    int command, int notification, int addNodeStatus,
    int addNodeFlags, int addNodeId)
{
    if (!apFirmwareTransitionActive)
        return;

    int error = 0;
    if (apFirmwareStage == IWN_AP_STAGE_STOP_RXON) {
        if (command == IWN_CMD_WIPAN_RXON)
            apFirmwareDeactivationReplySeen = true;
        if (notification == IWN_WIPAN_DEACTIVATION_COMPLETE)
            apFirmwareDeactivationNotificationSeen = true;
        if (apFirmwareDeactivationReplySeen &&
            apFirmwareDeactivationNotificationSeen &&
            !apFirmwarePostDeactivateQueued) {
            apFirmwarePostDeactivateQueued = true;
            apFirmwareStage = IWN_AP_STAGE_STOP_PAN_PARAMS;
            error = iwn_send_ap_stop_pan_params();
        }
        if (error != 0) {
            XYLog("%s: AP stop PAN parameters queue failed error=%d\n",
                  com.sc_dev.dv_xname, error);
            iwn_reset_ap_runtime_state();
        }
        return;
    }
    if (apFirmwareStage == IWN_AP_STAGE_STOP_PAN_PARAMS) {
        if (command == IWN_CMD_WIPAN_PARAMS) {
            XYLog("%s: AP PAN context stopped; STA context preserved\n",
                  com.sc_dev.dv_xname);
            iwn_reset_ap_runtime_state();
            /*
             * The primary STA output queue was fenced before the PAN RXON
             * transition.  The reset helper deliberately only clears that
             * fence: it is also used by power-off and fatal-reset paths,
             * where invoking if_start would be wrong.  This is the one
             * ordinary HostAP-stop terminal where the BSS RXON remains live,
             * however.  Wake the already-queued primary DHCP/ARP/data work
             * only after the WIPAN scheduler has acknowledged WLAN-only
             * service.  Do not manufacture a link, key, or association edge;
             * the existing authorized BSS remains its sole owner.
             */
            iwn_set_ap_primary_tx_quiesced(false, true);
            XYLog("%s: AP PAN stop terminal resumed primary STA output\n",
                  com.sc_dev.dv_xname);

        }
        return;
    }

    const int ridx = apFirmwareConfig.channel <= 14 ?
        IWN_RIDX_CCK : IWN_RIDX_OFDM;

    struct IwnApClientRuntime *completedClient = NULL;
    if ((command == IWN_CMD_ADD_NODE ||
         command == IWN_CMD_LINK_QUALITY) &&
        addNodeId >= IWN5000_ID_PAN_CLIENT &&
        addNodeId < IWN5000_ID_PAN_BROADCAST) {
        /* ADD_STA and LINK_QUALITY both carry the firmware station ID in
         * their submitted command body.  Recover that exact owner from the
         * completed command-ring descriptor: another associated client's
         * BA-driven LQ update may complete while a new peer is awaiting its
         * initial LQ fence, so "first pending client" is not an owner. */
        completedClient = iwn_find_ap_client_by_id(
            static_cast<uint8_t>(addNodeId));
        if (completedClient != NULL)
            iwn_select_ap_client(completedClient);
    }
    if (command == IWN_CMD_LINK_QUALITY && completedClient != NULL)
        completedClient->rateControl.linkQualityPending = false;

    /* Linux DVM makes iwl_sta_tx_modify_enable_tid() synchronous before it
     * exposes the aggregate SCD queue.  RX action processing cannot block on
     * an IWN host command, so the matching ADD_STA completion is our exact
     * fence: no qid descriptor can be selected before this point. */
    if (apFirmwareStage == IWN_AP_STAGE_RUNNING &&
        apClientTxBaEnablePending && command == IWN_CMD_ADD_NODE &&
        addNodeId == apClientContext->stationId &&
        (addNodeFlags & IWN_FLAG_SET_DISABLE_TID) != 0) {
        const uint8_t tid = apClientTxBaPendingTid;
        const uint8_t qid = apClientTxBaPendingQueue;
        const uint16_t requestedSsn = apClientTxBaPendingSsn;
        /* mac80211 stops a TID while DVM crosses ADDBA -> ADD_STA -> SCD,
         * so its negotiated SSN is still the next frame sequence when the
         * aggregate queue is activated.  Tahoe's Skywalk producer cannot be
         * synchronously stopped from this RX notification: frames already
         * accepted during the asynchronous command fence use the fixed PAN
         * BE queue and advance apClientTxSequence.  Start SCD at that live
         * next sequence so ring index and the first aggregate QoS header are
         * identical; the peer's negotiated reorder window still begins at
         * requestedSsn and admits this bounded forward advance. */
        const uint16_t activationSsn =
            tid < IWN_NUM_AMPDU_TID ?
            apClientTxSequence[tid] & 0x0fff : requestedSsn;
        const uint16_t oldDisableTid = apClientTxBaPendingOldDisableTid;
        apClientTxBaEnablePending = false;
        apClientTxBaPendingTid = UINT8_MAX;
        apClientTxBaPendingQueue = UINT8_MAX;
        apClientTxBaPendingSsn = 0;
        apClientTxBaPendingOldDisableTid = 0;

        int startError = addNodeStatus == 1 ? 0 : EIO;
        if (startError == 0 &&
            (tid >= IWN_NUM_AMPDU_TID ||
             qid < com.first_agg_txq ||
             qid >= com.ntxqs)) {
            startError = EINVAL;
        }
        if (startError == 0) {
            startError = iwn_nic_lock(&com);
            if (startError == 0) {
                uint8_t frameLimit = static_cast<uint8_t>(MIN(
                    apClientTxBa[tid].window,
                    static_cast<uint16_t>(IWN_AMPDU_MAX)));
                if (frameLimit == 0)
                    frameLimit = IWN_AMPDU_MAX;
                iwn_ap_ampdu_tx_start(
                    qid, tid, activationSsn, frameLimit);
                iwn_nic_unlock(&com);
                com.agg_queue_mask |= 1U << qid;
                apClientTxBaMask |= static_cast<uint16_t>(1U << tid);
                apClientTxBaQueue[tid] = qid;
                itl_ap_tx_ba_set_window_start(
                    &apClientTxBa[tid], activationSsn);
                XYLog("%s: IWN AP TX BA started tid=%u qid=%u "
                      "requested_ssn=%u activation_ssn=%u win=%u "
                      "after ADD_STA\n", com.sc_dev.dv_xname,
                      static_cast<unsigned>(tid),
                      static_cast<unsigned>(qid),
                      static_cast<unsigned>(requestedSsn),
                      static_cast<unsigned>(activationSsn),
                      static_cast<unsigned>(frameLimit));
                const int lqError = iwn_send_ap_client_link_quality();
                if (lqError != 0)
                    XYLog("%s: IWN AP TX BA LQ update tid=%u "
                          "win=%u error=%d\n", com.sc_dev.dv_xname,
                          static_cast<unsigned>(tid),
                          static_cast<unsigned>(frameLimit), lqError);
                return;
            }
        }

        apClientDisableTid = oldDisableTid;
        struct iwn_node_info rollback;
        bzero(&rollback, sizeof(rollback));
        rollback.id = apClientContext->stationId;
        rollback.control = IWN_NODE_UPDATE;
        rollback.flags = IWN_FLAG_SET_DISABLE_TID;
        rollback.disable_tid = htole16(apClientDisableTid);
        (void)com.ops.add_node(&com, &rollback, 1);
        XYLog("%s: IWN AP TX BA enable rejected tid=%u qid=%u "
              "status=0x%02x error=%d\n", com.sc_dev.dv_xname,
              static_cast<unsigned>(tid), static_cast<unsigned>(qid),
              static_cast<unsigned>(addNodeStatus & 0xff), startError);
        return;
    }

    /*
     * Linux DVM does not expose Association Response until ADD_STA has
     * completed successfully and the initial link-quality command has
     * completed.  That order is functional, not cosmetic: accepting the
     * client's 4-way handshake while its firmware station ID is still being
     * materialized can make a later SET_KEY reply succeed without linking
     * the CCMP key into the PAN RX station map on a cold 6x35 start.
     */
    if (apFirmwareStage == IWN_AP_STAGE_RUNNING &&
        apCsaClientRestoreStage == IWN_AP_CSA_CLIENT_RESTORE_ADD_NODE &&
        command == IWN_CMD_ADD_NODE &&
        addNodeId == apClientContext->stationId) {
        if (addNodeStatus != 1) {
            iwn_finish_ap_csa_client_restore(EIO);
            return;
        }
        apClientNodeInstalled = true;
        apCsaClientRestoreStage =
            IWN_AP_CSA_CLIENT_RESTORE_LINK_QUALITY;
        error = iwn_send_ap_client_link_quality();
        if (error != 0)
            iwn_finish_ap_csa_client_restore(error);
        return;
    }
    if (apFirmwareStage == IWN_AP_STAGE_RUNNING &&
        apCsaClientRestoreStage ==
            IWN_AP_CSA_CLIENT_RESTORE_LINK_QUALITY &&
        command == IWN_CMD_LINK_QUALITY &&
        completedClient != NULL) {
        if (apFirmwareConfig.rsnIELength == 0 ||
            !apClientAuthorized) {
            iwn_finish_ap_csa_client_restore(0);
            return;
        }
        if (apCsaGroupKeyRestored) {
            apCsaClientRestoreStage =
                IWN_AP_CSA_CLIENT_RESTORE_PAIRWISE_KEY;
            error = iwn_install_ap_ccmp_key(true, 0, apPtk.tk);
            if (error != 0)
                iwn_finish_ap_csa_client_restore(error);
            return;
        }
        apCsaClientRestoreStage = IWN_AP_CSA_CLIENT_RESTORE_GROUP_KEY;
        error = iwn_install_ap_ccmp_key(false, apGtkKid, apGtk);
        if (error != 0)
            iwn_finish_ap_csa_client_restore(error);
        return;
    }
    if (apFirmwareStage == IWN_AP_STAGE_RUNNING &&
        apCsaClientRestoreStage == IWN_AP_CSA_CLIENT_RESTORE_GROUP_KEY &&
        command == IWN_CMD_ADD_NODE &&
        addNodeId == IWN5000_ID_PAN_BROADCAST &&
        (addNodeFlags & IWN_FLAG_SET_KEY) != 0) {
        if (addNodeStatus != 1) {
            iwn_finish_ap_csa_client_restore(EIO);
            return;
        }
        apCsaGroupKeyRestored = true;
        apCsaClientRestoreStage =
            IWN_AP_CSA_CLIENT_RESTORE_PAIRWISE_KEY;
        error = iwn_install_ap_ccmp_key(true, 0, apPtk.tk);
        if (error != 0)
            iwn_finish_ap_csa_client_restore(error);
        return;
    }
    if (apFirmwareStage == IWN_AP_STAGE_RUNNING &&
        apCsaClientRestoreStage ==
            IWN_AP_CSA_CLIENT_RESTORE_PAIRWISE_KEY &&
        command == IWN_CMD_ADD_NODE &&
        addNodeId == apClientContext->stationId &&
        (addNodeFlags & IWN_FLAG_SET_KEY) != 0) {
        iwn_finish_ap_csa_client_restore(
            addNodeStatus == 1 ? 0 : EIO);
        return;
    }

    if (apFirmwareStage == IWN_AP_STAGE_RUNNING &&
        (apClientMaterializationStage ==
             IWN_AP_CLIENT_MATERIALIZATION_ADD_NODE ||
         apClientMaterializationStage ==
             IWN_AP_CLIENT_MATERIALIZATION_WAKE_NODE) &&
        command == IWN_CMD_ADD_NODE &&
        addNodeId == apClientContext->stationId) {
        if (addNodeStatus != 1) {
            if (apClientMaterializationStage ==
                IWN_AP_CLIENT_MATERIALIZATION_ADD_NODE)
                apClientNodeInstalled = false;
            apClientMaterializationStage =
                IWN_AP_CLIENT_MATERIALIZATION_IDLE;
            apClientReassociationPending = false;
            apClientContext->commandPending = false;
            XYLog("%s: AP client station materialization rejected "
                  "status=0x%02x\n", com.sc_dev.dv_xname,
                  static_cast<unsigned>(addNodeStatus & 0xff));
            (void)iwn_submit_next_ap_client_materialization();
            return;
        }
        apClientNodeInstalled = true;
        apClientMaterializationStage =
            IWN_AP_CLIENT_MATERIALIZATION_LINK_QUALITY;
        error = iwn_send_ap_client_link_quality();
        if (error != 0) {
            apClientMaterializationStage =
                IWN_AP_CLIENT_MATERIALIZATION_IDLE;
            apClientReassociationPending = false;
            apClientContext->commandPending = false;
            XYLog("%s: AP client link-quality queue failed error=%d\n",
                  com.sc_dev.dv_xname, error);
        }
        if (error != 0)
            (void)iwn_submit_next_ap_client_materialization();
        return;
    }
    if (apFirmwareStage == IWN_AP_STAGE_RUNNING &&
        apClientMaterializationStage ==
            IWN_AP_CLIENT_MATERIALIZATION_LINK_QUALITY &&
        command == IWN_CMD_LINK_QUALITY &&
        completedClient != NULL) {
        error = iwn_send_ap_assoc_success();
        apClientMaterializationStage =
            IWN_AP_CLIENT_MATERIALIZATION_IDLE;
        apClientContext->commandPending = false;
        if (error != 0) {
            apClientReassociationPending = false;
            XYLog("%s: AP association response queue failed error=%d\n",
                  com.sc_dev.dv_xname, error);
        }
        (void)iwn_submit_next_ap_client_materialization();
        return;
    }

    if (apFirmwareStage == IWN_AP_STAGE_INITIAL_RXON) {
        if (command == IWN_CMD_WIPAN_RXON)
            apFirmwareDeactivationReplySeen = true;
        if (notification == IWN_WIPAN_DEACTIVATION_COMPLETE)
            apFirmwareDeactivationNotificationSeen = true;
        iwn_continue_ap_after_deactivation();
        return;
    }

    if (apFirmwareStage == IWN_AP_STAGE_TIMING &&
        command == IWN_CMD_WIPAN_TIMING) {
        struct iwn_rxon unassociatedRxon = apFirmwareRxon;
        unassociatedRxon.filter &= ~htole32(IWN_FILTER_BSS);
        apFirmwareUnassociatedReplySeen = false;
        apFirmwareUnassociatedNotificationSeen = false;
        apFirmwareStage = IWN_AP_STAGE_UNASSOCIATED_RXON;
        error = iwn_cmd(&com, IWN_CMD_WIPAN_RXON,
                        &unassociatedRxon, com.rxonsz, 1);
    } else if (apFirmwareStage == IWN_AP_STAGE_UNASSOCIATED_RXON) {
        if (command == IWN_CMD_WIPAN_RXON)
            apFirmwareUnassociatedReplySeen = true;
        if (notification == IWN_WIPAN_DEACTIVATION_COMPLETE)
            apFirmwareUnassociatedNotificationSeen = true;
        if (apFirmwareUnassociatedReplySeen &&
            apFirmwareUnassociatedNotificationSeen) {
            apFirmwareStage = IWN_AP_STAGE_ADD_NODE;
            error = iwn_add_ap_broadcast_node();
        }
    } else if (apFirmwareStage == IWN_AP_STAGE_ADD_NODE &&
               command == IWN_CMD_ADD_NODE) {
        apFirmwareStage = IWN_AP_STAGE_LINK_QUALITY;
        error = iwn_send_ap_broadcast_link_quality(ridx);
    } else if (apFirmwareStage == IWN_AP_STAGE_LINK_QUALITY &&
               command == IWN_CMD_LINK_QUALITY) {
        apFirmwareStage = IWN_AP_STAGE_PAN_PARAMS;
        error = iwn_send_ap_pan_params(&apFirmwareConfig);
    } else if (apFirmwareStage == IWN_AP_STAGE_PAN_PARAMS &&
               command == IWN_CMD_WIPAN_PARAMS) {
        apFirmwareStage = IWN_AP_STAGE_EDCA;
        error = iwn_send_ap_edca(false);
    } else if (apFirmwareStage == IWN_AP_STAGE_EDCA &&
               command == IWN_CMD_WIPAN_EDCA_PARAMS) {
        apFirmwareStage = IWN_AP_STAGE_FIRST_BEACON;
        error = iwn_send_ap_beacon(&apFirmwareConfig);
    } else if (apFirmwareStage == IWN_AP_STAGE_FIRST_BEACON &&
               command == IWN_CMD_TX_BEACON) {
        /*
         * Match DVM's operational AP RX chain transition.  The idle,
         * unassociated PAN context is admitted with one idle receiver
         * (0x2406 on a 2x2 6235).  Once beaconing is enabled DVM recomputes
         * the chain in CAM: every active receiver remains awake and
         * MIMO_FORCE is asserted (0x6806).  Carry the same value into both
         * the associated WIPAN_RXON below and the final RXON_ASSOC command.
         * Leaving the pre-association 0x2406 in place lets FH fetch q7
         * descriptors but can leave the PAN radio FIFO without TX_DONE.
         */
        apFirmwareRxon.rxchain = htole16(
            IWN_RXCHAIN_VALID(com.rxchainmask) |
            IWN_RXCHAIN_MIMO_COUNT(com.nrxchains) |
            IWN_RXCHAIN_IDLE_COUNT(com.nrxchains) |
            IWN_RXCHAIN_MIMO_FORCE);
        apFirmwareStage = IWN_AP_STAGE_ASSOCIATED_RXON;
        error = iwn_cmd(&com, IWN_CMD_WIPAN_RXON,
                        &apFirmwareRxon, com.rxonsz, 1);
    } else if (apFirmwareStage == IWN_AP_STAGE_ASSOCIATED_RXON &&
               command == IWN_CMD_WIPAN_RXON) {
        apFirmwareStage = IWN_AP_STAGE_SENSITIVITY;
        error = iwn_send_ap_sensitivity();
    } else if (apFirmwareStage == IWN_AP_STAGE_SENSITIVITY &&
               command == IWN_CMD_SET_SENSITIVITY) {
        apFirmwareStage = IWN_AP_STAGE_TXPOWER;
        error = com.ops.set_txpower(&com, 1);
    } else if (apFirmwareStage == IWN_AP_STAGE_TXPOWER &&
               command == IWN_CMD_TXPOWER_DBM) {
        apFirmwareStage = IWN_AP_STAGE_POWER;
        error = iwn_set_pslevel(&com, 0, 0, 1);
    } else if (apFirmwareStage == IWN_AP_STAGE_POWER &&
               command == IWN_CMD_SET_POWER_MODE) {
        apFirmwareStage = IWN_AP_STAGE_SECOND_BEACON;
        error = iwn_send_ap_beacon(&apFirmwareConfig);
    } else if (apFirmwareStage == IWN_AP_STAGE_SECOND_BEACON &&
               command == IWN_CMD_TX_BEACON) {
        apFirmwareStage = IWN_AP_STAGE_POST_ASSOC_EDCA;
        error = iwn_send_ap_edca(true);
    } else if (apFirmwareStage == IWN_AP_STAGE_POST_ASSOC_EDCA &&
               command == IWN_CMD_WIPAN_EDCA_PARAMS) {
        apFirmwareStage = IWN_AP_STAGE_THIRD_BEACON;
        error = iwn_send_ap_beacon(&apFirmwareConfig);
    } else if (apFirmwareStage == IWN_AP_STAGE_THIRD_BEACON &&
               command == IWN_CMD_TX_BEACON) {
        apFirmwareStage = IWN_AP_STAGE_FINAL_RXON_ASSOC;
        error = iwn_send_ap_rxon_assoc();
    } else if (apFirmwareStage == IWN_AP_STAGE_FINAL_RXON_ASSOC &&
               command == IWN_CMD_WIPAN_RXON_ASSOC) {
        apFirmwareStage = IWN_AP_STAGE_FINAL_POWER;
        error = iwn_set_pslevel(&com, 0, 0, 1);
    } else if (apFirmwareStage == IWN_AP_STAGE_FINAL_POWER &&
               command == IWN_CMD_SET_POWER_MODE) {
        if (apStaBssAssociated) {
            /*
             * iwlagn_commit_rxon() recomputes PAN parameters after the
             * associated RXON.  Preserve that completion boundary here:
             * ordinary TX remains fenced until firmware has accepted the
             * steady two-associated-context 50/50 schedule.
             */
            apFirmwareStage = IWN_AP_STAGE_FINAL_PAN_PARAMS;
            error = iwn_send_ap_pan_params(&apFirmwareConfig);
        } else {
            apFirmwareStage = IWN_AP_STAGE_RUNNING;
            iwn_set_ap_scan_transition_blocked(false);
            iwn_set_ap_primary_tx_quiesced(false, true);
            iwn_complete_ap_csa_rebind();
            XYLog("%s: AP PAN context running after DVM RXON/beacon/EDCA "
                  "transition\n", com.sc_dev.dv_xname);
        }
    } else if (apFirmwareStage == IWN_AP_STAGE_FINAL_PAN_PARAMS &&
               command == IWN_CMD_WIPAN_PARAMS) {
        apFirmwareStage = IWN_AP_STAGE_RUNNING;
        iwn_set_ap_scan_transition_blocked(false);
        iwn_set_ap_primary_tx_quiesced(false, true);
        iwn_complete_ap_csa_rebind();
        XYLog("%s: AP PAN context running after DVM RXON/beacon/EDCA "
              "transition\n", com.sc_dev.dv_xname);
    }

    if (error != 0) {
        XYLog("%s: AP firmware stage=%u queue failed error=%d\n",
              com.sc_dev.dv_xname,
              static_cast<unsigned>(apFirmwareStage), error);
        iwn_reset_ap_runtime_state();
    }
}

static uint16_t
iwn_apsta_primary_channel(struct iwn_softc *sc)
{
    if (sc == NULL)
        return 0;

    struct ieee80211com *ic = &sc->sc_ic;

    /*
     * In steady RUN, net80211's committed BSS is authoritative.  Do not
     * require ic_opmode to still say STA here: Tahoe creates the APSTA
     * virtual interface before it calls the lower HostAP start, while the
     * DVM BSS RXON remains associated and continues to own the physical
     * radio channel.  Treating that transient public role change as loss of
     * the BSS admits an impossible split-channel APSTA configuration.
     */
    if (ic->ic_state == IEEE80211_S_RUN && ic->ic_bss != NULL &&
        ic->ic_bss->ni_chan != NULL) {
        const int channel = ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);
        if (channel > 0 && channel <= IEEE80211_CHAN_MAX)
            return static_cast<uint16_t>(channel);
    }

    /*
     * A public HostAP operation can race an associated scan.  net80211 then
     * exposes SCAN even though firmware still owns the serving BSS RXON.
     * Recover that physical channel only when both independent association
     * carriers remain present; an unassociated discovery RXON must not pin
     * AP admission to whichever channel the scan happens to visit.
     */
    const uint16_t rxonChannel = sc->rxon.chan;
    if ((le32toh(sc->rxon.filter) & IWN_FILTER_BSS) != 0 &&
        IEEE80211_AID(le16toh(sc->rxon.associd)) != 0 &&
        rxonChannel > 0 && rxonChannel <= IEEE80211_CHAN_MAX)
        return rxonChannel;

    return 0;
}

IOReturn ItlIwn::startAPMode(const struct ItlHalApConfig *config)
{
    if (!supportsAPMode()) {
        return kIOReturnUnsupported;
    }
    if (config == NULL) {
        return kIOReturnBadArgument;
    }
    if (config->ssid == NULL ||
        config->ssidLength == 0 ||
        config->ssidLength > sizeof(apFirmwareSsid) ||
        config->credentialLength > sizeof(apFirmwareCredential) ||
        (config->credentialLength != 0 && config->credential == NULL) ||
        config->rsnIELength > sizeof(apFirmwareRsnIE) ||
        (config->rsnIELength != 0 && config->rsnIE == NULL) ||
        (config->rsnIELength != 0 &&
         (config->credentialLength < 8 ||
          config->credentialLength > 63)) ||
        config->beaconTemplate == NULL ||
        config->beaconTemplateLength == 0 ||
        config->beaconTemplateLength > sizeof(apFirmwareBeacon)) {
        return kIOReturnBadArgument;
    }
    if (apFirmwareTransitionActive)
        return kIOReturnBusy;

    struct iwn_rxon ap_rxon;
    int error = iwn_build_ap_rxon(&ap_rxon, config);
    if (error != 0) {
        return kIOReturnBadArgument;
    }
    struct ieee80211com *ic = &com.sc_ic;
    struct _ifnet *ifp = &ic->ic_ac.ac_if;
    if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) !=
        (IFF_UP | IFF_RUNNING)) {
        return kIOReturnNotReady;
    }
    const uint16_t primaryChannel = iwn_apsta_primary_channel(&com);
    if (primaryChannel != 0 && config->channel != primaryChannel) {
        XYLog("%s: rejecting off-channel AP start requested=%u "
              "primary=%u\n", com.sc_dev.dv_xname,
              static_cast<unsigned>(config->channel),
              static_cast<unsigned>(primaryChannel));
        return kIOReturnBusy;
    }
    const IOReturn scanResult =
        iwn_quiesce_scan_for_ap_transition();
    if (scanResult != kIOReturnSuccess)
        return scanResult;
    /*
     * A public HostAP start commonly lands while airportd has net80211 in
     * the transient SCAN state.  The associated BSS RXON remains live during
     * that scan, so ic_state alone would misclassify it as unassociated and
     * leave the scheduler at the PAN-admission 20/280 split.  DVM keys this
     * decision from the BSS context's association state; retain the same
     * truth from the firmware carrier (ASSOC filter plus a negotiated AID).
     */
    const bool bssRxonAssociated =
        (le32toh(com.rxon.filter) & IWN_FILTER_BSS) != 0 &&
        IEEE80211_AID(le16toh(com.rxon.associd)) != 0;
    apStaBssAssociated =
        com.sc_ic.ic_state == IEEE80211_S_RUN || bssRxonAssociated;
    /*
     * mac80211 stops its software queues and waits for every non-command
     * DVM TX queue to drain before reconfiguring a second RXON context.
     * Tahoe otherwise lets the newly reassociated primary STA enqueue q0
     * traffic while retained PAN replay is changing scheduler ownership;
     * 6x35 can then strand both q0 and q5 across wake.  Keep HCMD q9 live,
     * but fence all ordinary output until the final PAN power reply.
     */
    iwn_set_ap_primary_tx_quiesced(true, false);
    if (iwn_ap_primary_tx_pending()) {
        iwn_set_ap_primary_tx_quiesced(false, false);
        iwn_set_ap_scan_transition_blocked(false);
        return kIOReturnNotReady;
    }
    /*
     * DVM exposes APSTA as two firmware contexts: the existing net80211 STA
     * remains the BSS context while this role-7 interface owns PAN.  Do not
     * overwrite sc->rxon or ic_opmode; both describe the still-live BSS
     * station context.  The PAN context has a separate WIPAN command family.
     *
     * Before programming CP/AP state, DVM first sends a P2P deactivation
     * RXON and waits for both its command reply and notification 0xbd.  The
     * rest of the sequence is queued by iwn_note_ap_firmware_event() only
     * after that boundary.  This avoids wedging 6x35 firmware with a direct
     * transition from its reset PAN state to CP.
     */
    bzero(&apFirmwareConfig, sizeof(apFirmwareConfig));
    apFirmwareConfig = *config;
    memcpy(apFirmwareSsid, config->ssid, config->ssidLength);
    if (config->credentialLength != 0) {
        memcpy(apFirmwareCredential, config->credential,
               config->credentialLength);
    }
    if (config->rsnIELength != 0) {
        memcpy(apFirmwareRsnIE, config->rsnIE, config->rsnIELength);
    }
    memcpy(apFirmwareBeacon, config->beaconTemplate,
           config->beaconTemplateLength);
    apFirmwareConfig.ssid = apFirmwareSsid;
    apFirmwareConfig.credential =
        config->credentialLength != 0 ? apFirmwareCredential : NULL;
    apFirmwareConfig.rsnIE =
        config->rsnIELength != 0 ? apFirmwareRsnIE : NULL;
    apFirmwareConfig.beaconTemplate = apFirmwareBeacon;
    apMaxStations = MIN(
        config->maxStations != 0 ? config->maxStations :
                                   static_cast<uint32_t>(1),
        static_cast<uint32_t>(kItlApFirmwareMaxClients));
    memcpy(&apFirmwareRxon, &ap_rxon, sizeof(apFirmwareRxon));

    /* Preserve bounded SAE PMKSA entries only across a radio reset that
     * replays the same BSSID. An explicit stop clears every entry below; a
     * different profile must never inherit cache state merely because the
     * controller object survived. */
    for (size_t index = 0; index < kItlApFirmwareMaxClients; index++) {
        struct IwnApClientRuntime *client = &apClients[index];
        if (!client->saePmksaValid)
            continue;
        iwn_select_ap_client(client);
        if (!iwn_ap_uses_sae() ||
            !IEEE80211_ADDR_EQ(
                client->saePmksaBssid, apFirmwareConfig.bssid))
            iwn_clear_ap_sae_pmksa();
    }

    if (config->rsnIELength != 0) {
        explicit_bzero(apProfilePmk, sizeof(apProfilePmk));
        if (iwn_ap_uses_sae()) {
#if !ITL_SAE_DRIVER_CRYPTO_AVAILABLE
            iwn_reset_ap_runtime_state();
            return kIOReturnUnsupported;
#else
            XYLog("%s: AP WPA3 SAE authenticator prepared SSID length=%zu\n",
                  com.sc_dev.dv_xname, config->ssidLength);
#endif
        } else {
            char passphrase[65];
            bzero(passphrase, sizeof(passphrase));
            memcpy(passphrase, apFirmwareCredential,
                   config->credentialLength);
            const int deriveError = pbkdf2_sha1(
                passphrase, apFirmwareSsid, config->ssidLength, 4096,
                apProfilePmk, sizeof(apProfilePmk));
            explicit_bzero(passphrase, sizeof(passphrase));
            if (deriveError != 0) {
                iwn_reset_ap_runtime_state();
                return kIOReturnError;
            }
            XYLog("%s: AP WPA2 authenticator prepared SSID length=%zu\n",
                  com.sc_dev.dv_xname, config->ssidLength);
        }
        arc4random_buf(apGtk, sizeof(apGtk));
        if (iwn_ap_uses_sae())
            arc4random_buf(apIgtk, sizeof(apIgtk));
        apGtkKid = 1;
        apIgtkKid = IWN_AP_IGTK_KEY_ID;
    }

    apFirmwareTransitionActive = true;
    apFirmwareDeactivationReplySeen = false;
    apFirmwareDeactivationNotificationSeen = false;
    apFirmwarePostDeactivateQueued = false;
    apFirmwareUnassociatedReplySeen = false;
    apFirmwareUnassociatedNotificationSeen = false;
    apFirmwareStage = IWN_AP_STAGE_INITIAL_RXON;

    struct iwn_rxon deactivateRxon = ap_rxon;
    bzero(deactivateRxon.bssid, sizeof(deactivateRxon.bssid));
    bzero(deactivateRxon.wlap, sizeof(deactivateRxon.wlap));
    deactivateRxon.filter = 0;
    deactivateRxon.mode = IWN_MODE_P2P;
    error = iwn_cmd(&com, IWN_CMD_WIPAN_RXON,
                    &deactivateRxon, com.rxonsz, 1);
    if (error != 0) {
        iwn_reset_ap_runtime_state();
        XYLog("%s: AP PAN deactivation queue failed error=%d\n",
              com.sc_dev.dv_xname, error);
        return kIOReturnError;
    }
    XYLog("%s: AP PAN deactivation queued channel=%u interval=%u "
          "dtim=%u ssid_len=%zu auth_upper=0x%x credential_len=%zu "
          "rsn_len=%zu\n",
          com.sc_dev.dv_xname, config->channel,
          config->beaconInterval, config->dtimPeriod,
          config->ssidLength, config->authUpper,
          config->credentialLength, config->rsnIELength);
    return kIOReturnSuccess;
}

int ItlIwn::iwn_ap_stop_tx_queue_mask(uint32_t *result) const
{
    if (result == NULL || com.command_queue != IWN_IPAN_CMD_QUEUE ||
        com.ntxqs <= IWN_IPAN_MCAST_QUEUE || com.ntxqs > 32)
        return EINVAL;

    /* DVM PAN owns fixed queues 4..8. Queue 9 is commands, 10 auxiliary;
     * neither those nor the primary's queues belong to this flush. */
    uint32_t mask = 0;
    for (int qid = 4; qid <= IWN_IPAN_MCAST_QUEUE; qid++)
        mask |= 1U << qid;
    for (size_t index = 0; index < kItlApFirmwareMaxClients; index++) {
        const struct IwnApClientRuntime *client = &apClients[index];
        if (client->txBaMask != 0 && !client->inUse)
            return EINVAL;
        for (uint8_t tid = 0; tid < IWN_NUM_AMPDU_TID; tid++) {
            if ((client->txBaMask & (1U << tid)) == 0)
                continue;
            const int qid = client->txBaQueue[tid];
            if (qid < IWN_IPAN_FIRST_AGG_QUEUE || qid >= com.ntxqs ||
                (mask & (1U << qid)) != 0 ||
                (com.agg_queue_mask & (1U << qid)) == 0)
                return EINVAL;
            for (uint8_t staTid = 0; staTid < IWN_NUM_AMPDU_TID; staTid++) {
                if (com.sc_tx_ba[staTid].wn != NULL &&
                    qid == com.first_agg_txq + staTid)
                    return EBUSY;
            }
            const struct iwn_tx_ring *ring = &com.txq[qid];
            if (ring->read < 0 || ring->read >= IWN_TX_RING_COUNT ||
                ring->cur < 0 || ring->cur >= IWN_TX_RING_COUNT)
                return EINVAL;
            int owned = 0;
            for (int slot = ring->read; slot != ring->cur;
                 slot = (slot + 1) % IWN_TX_RING_COUNT) {
                const struct iwn_tx_data *data = &ring->data[slot];
                if (data->m != NULL || data->ap_mgmt || data->ap_data) {
                    if (!data->ap_data)
                        return EINVAL;
                    owned++;
                }
            }
            if (owned != ring->queued)
                return EINVAL;
            mask |= 1U << qid;
        }
    }
    *result = mask;
    return 0;
}

int ItlIwn::iwn_retire_flushed_ap_tx()
{
    if (!apFirmwareTransitionActive ||
        apFirmwareStage != IWN_AP_STAGE_STOP_TX_RETIRE)
        return EINVAL;
    uint32_t mask = 0;
    int error = iwn_ap_stop_tx_queue_mask(&mask);
    if (error != 0 || (mask & ~apStopTxQueueMask) != 0)
        return error != 0 ? error : EINVAL;

    /* Flush completion means DMA and FIFO are empty, not that software may
     * invent TX_DONE for the fixed queues. Let their real responses reclaim
     * them before RXON destroys the PAN station context. */
    for (int qid = 4; qid <= IWN_IPAN_MCAST_QUEUE; qid++) {
        if (com.txq[qid].queued != 0)
            return EBUSY;
    }
    error = iwn_nic_lock(&com);
    if (error != 0)
        return error;

    /* RX command processing and public AP work share the command gate.
     * STOP_TX_FLUSH/RETIRE admit no new AP frames or ADDBA activation. Keep
     * every RA/TID owner until the exact flush reply and this NIC fence. */
    for (size_t index = 0; index < kItlApFirmwareMaxClients; index++) {
        struct IwnApClientRuntime *client = &apClients[index];
        for (uint8_t tid = 0; tid < IWN_NUM_AMPDU_TID; tid++) {
            if ((client->txBaMask & (1U << tid)) != 0) {
                const int qid = client->txBaQueue[tid];
                const int pending = com.txq[qid].queued;
                iwn_ap_ampdu_tx_stop(qid, tid,
                                     client->txSequence[tid] & 0x0fff);
                com.agg_queue_mask &= ~(1U << qid);
                client->txBaMask &= static_cast<uint16_t>(~(1U << tid));
                client->txBaQueue[tid] = UINT8_MAX;
                XYLog("%s: AP stop retired aggregate qid=%d pending=%d "
                      "remaining=%d\n", com.sc_dev.dv_xname, qid,
                      pending, com.txq[qid].queued);
            }
            itl_ap_tx_ba_reset(&client->txBa[tid]);
        }
        client->txBaEnablePending = false;
        client->txBaPendingTid = UINT8_MAX;
        client->txBaPendingQueue = UINT8_MAX;
        client->txBaPendingSsn = 0;
        client->txBaPendingOldDisableTid = 0;
        bzero(client->rateControl.pendingAggregate,
              sizeof(client->rateControl.pendingAggregate));
    }
    iwn_nic_unlock(&com);
    return 0;
}

int ItlIwn::iwn_continue_ap_stop_after_flush()
{
    const int retireError = iwn_retire_flushed_ap_tx();
    if (retireError != 0)
        return retireError;

    apFirmwareDeactivationReplySeen = false;
    apFirmwareDeactivationNotificationSeen = false;
    apFirmwarePostDeactivateQueued = false;
    apFirmwareStage = IWN_AP_STAGE_STOP_RXON;
    struct iwn_rxon deactivateRxon = apFirmwareRxon;
    bzero(deactivateRxon.bssid, sizeof(deactivateRxon.bssid));
    bzero(deactivateRxon.wlap, sizeof(deactivateRxon.wlap));
    deactivateRxon.filter = 0;
    deactivateRxon.mode = IWN_MODE_P2P;
    const int error = iwn_cmd(&com, IWN_CMD_WIPAN_RXON,
                              &deactivateRxon, com.rxonsz, 1);
    if (error != 0)
        apFirmwareStage = IWN_AP_STAGE_STOP_TX_RETIRE;
    return error;
}

static bool iwn_ap_stop_tx_prepare_doorbell(
    struct iwn_softc *sc, void *context)
{
    ItlIwn *that = static_cast<ItlIwn *>(context);
    if (that == NULL || !that->apFirmwareTransitionActive ||
        that->apFirmwareStage != IWN_AP_STAGE_STOP_TX_FLUSH)
        return false;
    that->apStopTxFlushIndex = sc->txq[sc->command_queue].cur;
    return true;
}

void ItlIwn::iwn_note_ap_stop_tx_flush(
    int command, uint16_t commandIndex, bool failed)
{
    if (!apFirmwareTransitionActive ||
        apFirmwareStage != IWN_AP_STAGE_STOP_TX_FLUSH ||
        command != IWN_CMD_TXFIFO_FLUSH ||
        commandIndex != apStopTxFlushIndex)
        return;
    if (failed) {
        /* A rejected command is not permission to release DMA ownership.
         * The upper pending-stop census can retry the real flush. */
        apFirmwareStage = IWN_AP_STAGE_RUNNING;
        XYLog("%s: AP stop TX flush rejected; retaining queue owners\n",
              com.sc_dev.dv_xname);
        return;
    }
    apFirmwareStage = IWN_AP_STAGE_STOP_TX_RETIRE;
    const int error = iwn_continue_ap_stop_after_flush();
    XYLog("%s: AP stop TX flush completed mask=0x%x retirement=%d\n",
          com.sc_dev.dv_xname, apStopTxQueueMask, error);
}

IOReturn ItlIwn::stopAPMode()
{
    if (!supportsAPMode()) {
        return kIOReturnSuccess;
    }
    XYLog("%s: AP stop requested transition_active=%u stage=%u\n",
          com.sc_dev.dv_xname,
          static_cast<unsigned>(apFirmwareTransitionActive),
          static_cast<unsigned>(apFirmwareStage));
    const IOReturn scanResult =
        iwn_quiesce_scan_for_ap_transition();
    if (scanResult != kIOReturnSuccess)
        return scanResult;
    for (size_t index = 0; index < kItlApFirmwareMaxClients; index++) {
        iwn_select_ap_client(&apClients[index]);
        iwn_clear_ap_sae_pmksa();
    }
    if (!apFirmwareTransitionActive ||
        apFirmwareStage == IWN_AP_STAGE_IDLE) {
        iwn_reset_ap_runtime_state();
        return kIOReturnSuccess;
    }
    if (apFirmwareStage == IWN_AP_STAGE_STOP_TX_RETIRE) {
        (void)iwn_continue_ap_stop_after_flush();
        return kIOReturnNotReady;
    }
    if (apFirmwareStage == IWN_AP_STAGE_STOP_TX_FLUSH ||
        apFirmwareStage == IWN_AP_STAGE_STOP_RXON ||
        apFirmwareStage == IWN_AP_STAGE_STOP_PAN_PARAMS) {
        /*
         * The corresponding command reply only proves that DVM accepted
         * the teardown request.  The AP context remains owned until the
         * terminal WIPAN_PARAMS reply retires it through
         * iwn_reset_ap_runtime_state().  Treating this interval as a
         * completed stop lets a following CoreWLAN HostAP request submit a
         * second PAN transition over the old one.  APSTAOwner recognizes
         * NotReady as an asynchronous lower-stop fence and retries from its
         * command-gated census.
         */
        return kIOReturnNotReady;
    }
    if (apFirmwareStage != IWN_AP_STAGE_RUNNING) {
        return kIOReturnBusy;
    }

    uint32_t queueMask = 0;
    if (iwn_ap_stop_tx_queue_mask(&queueMask) != 0)
        return kIOReturnError;
    struct iwn_txfifo_flush_cmd flush;
    bzero(&flush, sizeof(flush));
    flush.queue_control = htole32(queueMask);
    flush.flush_control = htole16(IWN_TXFIFO_FLUSH_DROP_ALL);
    apStopTxQueueMask = queueMask;
    apFirmwareStage = IWN_AP_STAGE_STOP_TX_FLUSH;
    const int error = iwn_cmd_with_doorbell_hook(
        &com, IWN_CMD_TXFIFO_FLUSH, &flush, sizeof(flush), 1,
        iwn_ap_stop_tx_prepare_doorbell, NULL, this);
    if (error != 0) {
        apFirmwareStage = IWN_AP_STAGE_RUNNING;
        iwn_set_ap_scan_transition_blocked(false);
        return kIOReturnError;
    }
    /* Intel DVM forced teardown first flushes DMA/FIFO, then disables and
     * unmaps the queue. The RXON/notification/PAN-params chain follows that
     * real completion; submitting the flush is not a lower stop terminal. */
    return kIOReturnNotReady;
}

IOReturn ItlIwn::setAPMaxStations(uint32_t maxStations)
{
    if (!apFirmwareTransitionActive ||
        apFirmwareStage != IWN_AP_STAGE_RUNNING)
        return kIOReturnNotReady;
    if (maxStations == 0)
        return kIOReturnBadArgument;
    uint32_t effective = MIN(
        maxStations, static_cast<uint32_t>(kItlApFirmwareMaxClients));
    for (size_t index = 0; index < kItlApFirmwareMaxClients; index++) {
        if (apClients[index].inUse)
            effective = MAX(effective, static_cast<uint32_t>(index + 1));
    }
    apMaxStations = effective;
    apFirmwareConfig.maxStations = effective;
    return kIOReturnSuccess;
}

IOReturn ItlIwn::setAPHidden(bool hidden)
{
    if (!apFirmwareTransitionActive ||
        apFirmwareStage == IWN_AP_STAGE_IDLE ||
        apFirmwareStage >= IWN_AP_STAGE_STOP_TX_FLUSH)
        return kIOReturnNotReady;
    if (apHidden == hidden)
        return kIOReturnSuccess;
    /*
     * HostAP start returns once the DVM transition is queued, while the
     * public closednet selector follows immediately.  Before the first
     * beacon command, update the owned template in place and let the normal
     * three-beacon bring-up sequence upload it.  Once that sequence has
     * started, accept a live change only from RUNNING; callers can retry the
     * short intermediate window instead of receiving a false success for a
     * template firmware has already consumed.
     */
    const bool queuedBeforeFirstBeacon =
        apFirmwareStage < IWN_AP_STAGE_FIRST_BEACON;
    if (!queuedBeforeFirstBeacon &&
        apFirmwareStage != IWN_AP_STAGE_RUNNING)
        return kIOReturnBusy;

    const bool previousHidden = apHidden;
    int error = itl_ap_beacon_set_hidden(
        apFirmwareBeacon, &apFirmwareConfig.beaconTemplateLength,
        sizeof(apFirmwareBeacon), apFirmwareSsid,
        apFirmwareConfig.ssidLength, &apHidden, hidden);
    if (error != 0) {
        const size_t firstElement = sizeof(struct ieee80211_frame) + 12;
        XYLog("%s: AP closednet beacon rewrite failed error=%d "
              "beacon_len=%zu ssid_len=%zu first_ie=%u first_ie_len=%u\n",
              com.sc_dev.dv_xname, error,
              apFirmwareConfig.beaconTemplateLength,
              apFirmwareConfig.ssidLength,
              firstElement < apFirmwareConfig.beaconTemplateLength ?
                  static_cast<unsigned>(apFirmwareBeacon[firstElement]) :
                  UINT_MAX,
              firstElement + 1 < apFirmwareConfig.beaconTemplateLength ?
                  static_cast<unsigned>(apFirmwareBeacon[firstElement + 1]) :
                  UINT_MAX);
        return error == ENOENT ? kIOReturnUnsupported : kIOReturnBadArgument;
    }
    if (queuedBeforeFirstBeacon)
        return kIOReturnSuccess;
    error = iwn_send_ap_beacon(&apFirmwareConfig);
    if (error != 0) {
        const int rollback = itl_ap_beacon_set_hidden(
            apFirmwareBeacon, &apFirmwareConfig.beaconTemplateLength,
            sizeof(apFirmwareBeacon), apFirmwareSsid,
            apFirmwareConfig.ssidLength, &apHidden, previousHidden);
        if (rollback == 0)
            (void)iwn_send_ap_beacon(&apFirmwareConfig);
        return kIOReturnError;
    }
    return kIOReturnSuccess;
}

IOReturn ItlIwn::triggerAPCSA(const struct ItlHalApCSA *csa)
{
    if (!apFirmwareTransitionActive ||
        apFirmwareStage != IWN_AP_STAGE_RUNNING)
        return kIOReturnNotReady;
    if (csa == NULL || csa->channel == 0 || csa->channel > 14 ||
        apFirmwareConfig.channel == 0 || apFirmwareConfig.channel > 14 ||
        csa->mode > 1)
        return kIOReturnBadArgument;
    if (apCsaPending)
        return kIOReturnBusy;
    const uint16_t primaryChannel = iwn_apsta_primary_channel(&com);
    if (primaryChannel != 0 && csa->channel != primaryChannel) {
        XYLog("%s: rejecting off-channel AP CSA requested=%u "
              "primary=%u\n", com.sc_dev.dv_xname,
              static_cast<unsigned>(csa->channel),
              static_cast<unsigned>(primaryChannel));
        return kIOReturnBusy;
    }
    if (csa->channel == apFirmwareConfig.channel)
        return kIOReturnSuccess;

    struct ItlHalApConfig targetConfig = apFirmwareConfig;
    targetConfig.channel = csa->channel;
    struct iwn_rxon targetRxon;
    if (iwn_build_ap_rxon(&targetRxon, &targetConfig) != 0)
        return kIOReturnBadArgument;

    const uint8_t count = csa->count != 0 ? csa->count :
        kItlApCsaDefaultCount;
    int error = itl_ap_beacon_begin_csa(
        apFirmwareBeacon, &apFirmwareConfig.beaconTemplateLength,
        sizeof(apFirmwareBeacon), csa->mode,
        static_cast<uint8_t>(csa->channel), count);
    if (error != 0)
        return error == EBUSY ? kIOReturnBusy : kIOReturnBadArgument;
    error = iwn_send_ap_beacon(&apFirmwareConfig);
    if (error != 0) {
        (void)itl_ap_beacon_end_csa(
            apFirmwareBeacon, &apFirmwareConfig.beaconTemplateLength,
            static_cast<uint8_t>(csa->channel), false);
        return kIOReturnError;
    }

    apCsaPending = true;
    apCsaTargetChannel = csa->channel;
    apCsaMode = csa->mode;
    apCsaCount = count;
    const uint32_t delayMs = MAX(1U, static_cast<uint32_t>(
        (static_cast<uint64_t>(apFirmwareConfig.beaconInterval) *
         IEEE80211_DUR_TU + 999) / 1000));
    timeout_add_msec(&apCsaTimeout, delayMs);
    XYLog("%s: IWN AP CSA armed channel=%u mode=%u count=%u delay=%u ms\n",
          com.sc_dev.dv_xname, static_cast<unsigned>(csa->channel),
          static_cast<unsigned>(csa->mode), static_cast<unsigned>(count),
          static_cast<unsigned>(delayMs));
    return kIOReturnSuccess;
}

uint16_t ItlIwn::getAPCurrentChannel() const
{
    /*
     * The APSTA owner uses this query as its lower-epoch liveness witness.
     * IWN startAPMode() has accepted and queued the asynchronous DVM PAN
     * transaction before the first watchdog census can run, whereas
     * IWN_AP_STAGE_RUNNING is reached only after its command/notification
     * chain completes.  Reporting zero during that accepted interval made
     * the owner mistake normal AP materialisation for a firmware reset and
     * tear down the still-associated primary STA itself.
     *
     * apFirmwareTransitionActive is set only after the profile and the
     * first WIPAN_RXON command have been accepted.  Every terminal queue
     * failure calls iwn_reset_ap_runtime_state(), so a real lower loss still
     * becomes zero on the next census and retains the existing recovery
     * behavior.  The configured channel is therefore a valid liveness
     * witness from INITIAL_RXON through RUNNING, but never after teardown.
     */
    if (!apFirmwareTransitionActive || apFirmwareConfig.channel == 0)
        return 0;
    return apFirmwareConfig.channel;
}

bool ItlIwn::requiresAPSTASharedChannel() const
{
    /* Linux DVM publishes STA+AP with num_different_channels == 1. */
    return true;
}

uint16_t ItlIwn::getAPSTARequiredSharedChannel() const
{
    /* Linux DVM advertises STA+AP with num_different_channels == 1.  PAN
     * scheduling permits two firmware contexts, not two simultaneous radio
     * channels.  The query is consumed before startAPMode(), when there is
     * no running PAN context yet, so the associated BSS carrier must take
     * precedence over getAPCurrentChannel(). */
    const uint16_t primaryChannel =
        iwn_apsta_primary_channel(const_cast<struct iwn_softc *>(&com));
    return primaryChannel != 0 ? primaryChannel : getAPCurrentChannel();
}

bool ItlIwn::isPrimaryStaRecoveryScanPending() const
{
    struct iwn_softc *sc = const_cast<struct iwn_softc *>(&com);
    struct ieee80211com *ic = &sc->sc_ic;
    bool pending = false;

    if (ic->ic_opmode != IEEE80211_M_STA ||
        ic->ic_state != IEEE80211_S_SCAN ||
        (ic->ic_flags & IEEE80211_F_AUTO_JOIN) == 0 ||
        ic->ic_des_esslen != 0 ||
        sc->sc_sae_wcl_credential_lock == NULL)
        return false;

    IOSimpleLockLock(sc->sc_sae_wcl_credential_lock);
    pending = sc->sc_sae_bss_loss_recovery_armed &&
        sc->sc_sae_bss_loss_recovery_generation != 0 &&
        sc->sc_sae_wcl_credential_active &&
        !sc->sc_sae_wcl_credential_staged &&
        !sc->sc_sae_wcl_credential_pending &&
        sc->sc_sae_wcl_credential.request_generation ==
            sc->sc_sae_bss_loss_recovery_generation &&
        itl_sae_wcl_credential_is_well_formed(
            &sc->sc_sae_wcl_credential);
    IOSimpleLockUnlock(sc->sc_sae_wcl_credential_lock);
    return pending;
}

void ItlIwn::iwn_ap_csa_timeout(void *arg)
{
    ItlIwn *that = static_cast<ItlIwn *>(arg);
    if (that == NULL)
        return;
    const int s = splnet();
    if (that->apCsaPending && that->apCsaCount > 1) {
        that->apCsaCount--;
        const int countError = itl_ap_beacon_set_csa_count(
            that->apFirmwareBeacon,
            that->apFirmwareConfig.beaconTemplateLength,
            that->apCsaCount);
        const int beaconError = countError == 0 ?
            that->iwn_send_ap_beacon(&that->apFirmwareConfig) : countError;
        if (beaconError == 0) {
            const uint32_t delayMs = MAX(1U, static_cast<uint32_t>(
                (static_cast<uint64_t>(
                    that->apFirmwareConfig.beaconInterval) *
                 IEEE80211_DUR_TU + 999) / 1000));
            timeout_add_msec(&that->apCsaTimeout, delayMs);
            splx(s);
            return;
        }
    }
    const int error = that->iwn_finish_ap_csa();
    if (error != 0)
        XYLog("%s: IWN AP CSA terminal error=%d\n",
              that->com.sc_dev.dv_xname, error);
    splx(s);
}

int ItlIwn::iwn_finish_ap_csa()
{
    if (!apCsaPending || !apFirmwareTransitionActive ||
        apFirmwareStage != IWN_AP_STAGE_RUNNING)
        return EINVAL;

    const uint16_t oldChannel = apFirmwareConfig.channel;
    struct ItlHalApConfig targetConfig = apFirmwareConfig;
    targetConfig.channel = apCsaTargetChannel;
    struct iwn_rxon targetRxon;
    if (iwn_build_ap_rxon(&targetRxon, &targetConfig) != 0)
        return EINVAL;

    const struct iwn_rxon oldRxon = apFirmwareRxon;
    struct iwn_rxon deactivateRxon = targetRxon;
    bool restoreClients = false;
    apFirmwareConfig.channel = apCsaTargetChannel;
    memcpy(&apFirmwareRxon, &targetRxon, sizeof(apFirmwareRxon));
    int error = itl_ap_beacon_end_csa(
        apFirmwareBeacon, &apFirmwareConfig.beaconTemplateLength,
        static_cast<uint8_t>(apFirmwareConfig.channel), true);
    if (error != 0)
        goto rollback_beacon;

    /* Intel's published DVM driver never submits command 0xb9 for a CP/AP
     * context: its ordinary channel-switch command is explicitly BSS-only,
     * while the 0xb9 P2P carrier is undocumented.  6x35 firmware asserts if
     * that P2P command is sent to our live CP context.  Rebind the PAN owner
     * through the same proven deactivation -> timing -> unassociated RXON ->
     * associated RXON sequence used for initial HostAP materialization.
     * Peers have already received the 3/2/1 CSA beacons; clearing the old
     * firmware station makes them authenticate freshly after the target
     * channel starts instead of retaining a client id destroyed by RXON.
     * A fully-authorized peer, however, follows the advertised CSA without
     * performing a new association.  Preserve that logical association and
     * its replay/key epoch, retire only firmware-owned TX aggregation, then
     * recreate every retained dynamic station id and its keys after the PAN
     * context is running on the target channel.  This is the same two-table
     * contract as DVM iwl_clear_ucode_stations()/iwl_restore_stations(): the
     * RXON transition invalidates firmware ownership but does not collapse
     * the driver's logical station table to one selected peer. */
    for (size_t index = 0; index < kItlApFirmwareMaxClients; index++) {
        struct IwnApClientRuntime *client = &apClients[index];
        if (!client->inUse)
            continue;
        iwn_select_ap_client(client);
        if (!client->associated ||
            (apFirmwareConfig.rsnIELength != 0 && !client->authorized)) {
            iwn_clear_ap_client_for_csa(client);
            continue;
        }
        if (client->timSet) {
            const int timError = iwn_update_ap_tim(false);
            if (timError != 0)
                XYLog("%s: IWN AP CSA TIM clear id=%u error=%d\n",
                      com.sc_dev.dv_xname,
                      static_cast<unsigned>(client->stationId), timError);
        }
        iwn_purge_ap_ps_queue();
        iwn_stop_all_ap_client_tx_ba();
        client->nodeInstalled = false;
        client->materializationStage =
            IWN_AP_CLIENT_MATERIALIZATION_IDLE;
        client->commandPending = false;
        client->powerSave = false;
        restoreClients = true;
    }
    apCsaRestoreIndex = 0;
    apCsaGroupKeyRestored = false;
    apCsaClientRestoreStage = restoreClients ?
        IWN_AP_CSA_CLIENT_RESTORE_PREPARED :
        IWN_AP_CSA_CLIENT_RESTORE_IDLE;
    apFirmwareDeactivationReplySeen = false;
    apFirmwareDeactivationNotificationSeen = false;
    apFirmwarePostDeactivateQueued = false;
    apFirmwareUnassociatedReplySeen = false;
    apFirmwareUnassociatedNotificationSeen = false;
    apFirmwareStage = IWN_AP_STAGE_INITIAL_RXON;
    bzero(deactivateRxon.bssid, sizeof(deactivateRxon.bssid));
    bzero(deactivateRxon.wlap, sizeof(deactivateRxon.wlap));
    deactivateRxon.filter = 0;
    deactivateRxon.mode = IWN_MODE_P2P;
    error = iwn_cmd(&com, IWN_CMD_WIPAN_RXON,
                    &deactivateRxon, com.rxonsz, 1);
    if (error != 0) {
        apFirmwareStage = IWN_AP_STAGE_RUNNING;
        apFirmwareConfig.channel = oldChannel;
        memcpy(&apFirmwareRxon, &oldRxon, sizeof(apFirmwareRxon));
        for (size_t index = 0; index < kItlApFirmwareMaxClients; index++) {
            struct IwnApClientRuntime *client = &apClients[index];
            if (client->inUse && client->associated)
                client->nodeInstalled = true;
        }
        apCsaClientRestoreStage = IWN_AP_CSA_CLIENT_RESTORE_IDLE;
        apCsaRestoreIndex = 0;
        apCsaGroupKeyRestored = false;
        (void)itl_ap_beacon_set_channel(
            apFirmwareBeacon, apFirmwareConfig.beaconTemplateLength,
            static_cast<uint8_t>(oldChannel));
        (void)iwn_send_ap_beacon(&apFirmwareConfig);
        goto clear_csa;
    }
    XYLog("%s: IWN AP CSA PAN rebind queued channel=%u\n",
          com.sc_dev.dv_xname,
          static_cast<unsigned>(apFirmwareConfig.channel));
    return 0;

rollback_beacon:
    apFirmwareConfig.channel = oldChannel;
    memcpy(&apFirmwareRxon, &oldRxon, sizeof(apFirmwareRxon));
    (void)itl_ap_beacon_end_csa(
        apFirmwareBeacon, &apFirmwareConfig.beaconTemplateLength,
        static_cast<uint8_t>(apCsaTargetChannel), false);
    (void)itl_ap_beacon_set_channel(
        apFirmwareBeacon, apFirmwareConfig.beaconTemplateLength,
        static_cast<uint8_t>(oldChannel));
    (void)iwn_send_ap_beacon(&apFirmwareConfig);
clear_csa:
    apCsaPending = false;
    apCsaTargetChannel = 0;
    apCsaMode = 0;
    apCsaCount = 0;
    apCsaClientRestoreStage = IWN_AP_CSA_CLIENT_RESTORE_IDLE;
    apCsaRestoreIndex = 0;
    apCsaGroupKeyRestored = false;
    return error != 0 ? error : EIO;
}

void ItlIwn::iwn_complete_ap_csa_rebind()
{
    if (!apCsaPending)
        return;
    if (apCsaClientRestoreStage ==
        IWN_AP_CSA_CLIENT_RESTORE_PREPARED) {
        while (apCsaRestoreIndex < kItlApFirmwareMaxClients) {
            struct IwnApClientRuntime *client =
                &apClients[apCsaRestoreIndex];
            if (!client->inUse || !client->associated ||
                (apFirmwareConfig.rsnIELength != 0 &&
                 !client->authorized)) {
                apCsaRestoreIndex++;
                continue;
            }
            iwn_select_ap_client(client);
            apCsaClientRestoreStage =
                IWN_AP_CSA_CLIENT_RESTORE_ADD_NODE;
            const int error = iwn_add_ap_client_node(client->mac);
            if (error == 0) {
                XYLog("%s: IWN AP CSA client restore queued id=%u peer="
                      "%02x:%02x:%02x:%02x:%02x:%02x\n",
                      com.sc_dev.dv_xname,
                      static_cast<unsigned>(client->stationId),
                      client->mac[0], client->mac[1], client->mac[2],
                      client->mac[3], client->mac[4], client->mac[5]);
                return;
            }
            iwn_finish_ap_csa_client_restore(error);
            return;
        }
        apCsaClientRestoreStage = IWN_AP_CSA_CLIENT_RESTORE_IDLE;
    }
    if (apCsaClientRestoreStage != IWN_AP_CSA_CLIENT_RESTORE_IDLE)
        return;
    apCsaPending = false;
    apCsaTargetChannel = 0;
    apCsaMode = 0;
    apCsaCount = 0;
    apCsaRestoreIndex = 0;
    apCsaGroupKeyRestored = false;
    XYLog("%s: IWN AP CSA complete channel=%u\n",
          com.sc_dev.dv_xname,
          static_cast<unsigned>(apFirmwareConfig.channel));
}

void ItlIwn::iwn_finish_ap_csa_client_restore(int error)
{
    struct IwnApClientRuntime *client = apClientContext;
    if (error != 0) {
        XYLog("%s: IWN AP CSA client restore failed id=%u stage=%u "
              "error=%d\n",
              com.sc_dev.dv_xname,
              static_cast<unsigned>(client->stationId),
              static_cast<unsigned>(apCsaClientRestoreStage), error);
        iwn_clear_ap_client_for_csa(client);
    } else {
        XYLog("%s: IWN AP CSA client restore complete id=%u peer="
              "%02x:%02x:%02x:%02x:%02x:%02x\n",
              com.sc_dev.dv_xname,
              static_cast<unsigned>(client->stationId),
              client->mac[0], client->mac[1], client->mac[2],
              client->mac[3], client->mac[4], client->mac[5]);
    }
    apCsaRestoreIndex++;
    apCsaClientRestoreStage = IWN_AP_CSA_CLIENT_RESTORE_IDLE;
    if (apCsaRestoreIndex < kItlApFirmwareMaxClients)
        apCsaClientRestoreStage = IWN_AP_CSA_CLIENT_RESTORE_PREPARED;
    iwn_complete_ap_csa_rebind();
}

void ItlIwn::iwn_clear_ap_client_for_csa(
    struct IwnApClientRuntime *client)
{
    if (client == NULL || !client->inUse)
        return;
    iwn_select_ap_client(client);
    if (apClientAssociated)
        iwn_publish_ap_station_event(
            apClientMac, NULL, 0, IEEE80211_APSTA_EVENT_LEAVE);
    if (apClientNodeInstalled) {
        const int removeError = iwn_remove_ap_client_node(apClientMac);
        if (removeError != 0)
            XYLog("%s: IWN AP CSA client removal error=%d\n",
                  com.sc_dev.dv_xname, removeError);
    }
    iwn_reset_ap_client(client, true, true);
}

#define    PCI_VENDOR_INTEL    0x8086        /* Intel */
#define    PCI_PRODUCT_INTEL_WL_4965_1    0x4229        /* Wireless WiFi Link 4965 */
#define    PCI_PRODUCT_INTEL_WL_6300_1    0x422b        /* Centrino Ultimate-N 6300 */
#define    PCI_PRODUCT_INTEL_WL_6200_1    0x422c        /* Centrino Advanced-N 6200 */
#define    PCI_PRODUCT_INTEL_WL_4965_2    0x4230        /* Wireless WiFi Link 4965 */
#define    PCI_PRODUCT_INTEL_WL_5100_1    0x4232        /* WiFi Link 5100 */
#define    PCI_PRODUCT_INTEL_WL_5300_1    0x4235        /* WiFi Link 5300 */
#define    PCI_PRODUCT_INTEL_WL_5300_2    0x4236        /* WiFi Link 5300 */
#define    PCI_PRODUCT_INTEL_WL_5100_2    0x4237        /* WiFi Link 5100 */
#define    PCI_PRODUCT_INTEL_WL_6300_2    0x4238        /* Centrino Ultimate-N 6300 */
#define    PCI_PRODUCT_INTEL_WL_6200_2    0x4239        /* Centrino Advanced-N 6200 */
#define    PCI_PRODUCT_INTEL_WL_5350_1    0x423a        /* WiFi Link 5350 */
#define    PCI_PRODUCT_INTEL_WL_5350_2    0x423b        /* WiFi Link 5350 */
#define    PCI_PRODUCT_INTEL_WL_5150_1    0x423c        /* WiFi Link 5150 */
#define    PCI_PRODUCT_INTEL_WL_5150_2    0x423d        /* WiFi Link 5150 */
#define    PCI_PRODUCT_INTEL_WL_6005_1    0x0082        /* Centrino Advanced-N 6205 */
#define    PCI_PRODUCT_INTEL_WL_1000_1    0x0083        /* WiFi Link 1000 */
#define    PCI_PRODUCT_INTEL_WL_1000_2    0x0084        /* WiFi Link 1000 */
#define    PCI_PRODUCT_INTEL_WL_6005_2    0x0085        /* Centrino Advanced-N 6205 */
#define    PCI_PRODUCT_INTEL_WL_6050_1    0x0087        /* Centrino Advanced-N 6250 */
#define    PCI_PRODUCT_INTEL_WL_6050_2    0x0089        /* Centrino Advanced-N 6250 */
#define    PCI_PRODUCT_INTEL_WL_1030_1    0x008a        /* WiFi Link 1030 */
#define    PCI_PRODUCT_INTEL_WL_1030_2    0x008b        /* WiFi Link 1030 */
#define    PCI_PRODUCT_INTEL_WL_6030_1    0x0090        /* Centrino Advanced-N 6030 */
#define    PCI_PRODUCT_INTEL_WL_6030_2    0x0091        /* Centrino Advanced-N 6030 */
#define    PCI_PRODUCT_INTEL_WL_135_1    0x0892        /* Centrino Wireless-N 135 */
#define    PCI_PRODUCT_INTEL_WL_135_2    0x0893        /* Centrino Wireless-N 135 */
#define    PCI_PRODUCT_INTEL_WL_105_1    0x0894        /* Centrino Wireless-N 105 */
#define    PCI_PRODUCT_INTEL_WL_105_2    0x0895        /* Centrino Wireless-N 105 */
#define    PCI_PRODUCT_INTEL_WL_130_1    0x0896        /* Centrino Wireless-N 130 */
#define    PCI_PRODUCT_INTEL_WL_130_2    0x0897        /* Centrino Wireless-N 130 */
#define    PCI_PRODUCT_INTEL_WL_100_1    0x08ae        /* Centrino Wireless-N 100 */
#define    PCI_PRODUCT_INTEL_WL_100_2    0x08af        /* Centrino Wireless-N 100 */
#define    PCI_PRODUCT_INTEL_WL_6235_1    0x088e        /* Centrino Advanced-N 6235 */
#define    PCI_PRODUCT_INTEL_WL_6235_2    0x088f        /* Centrino Advanced-N 6235 */
#define    PCI_PRODUCT_INTEL_WL_2200_1    0x0890        /* Centrino Wireless-N 2200 */
#define    PCI_PRODUCT_INTEL_WL_2200_2    0x0891        /* Centrino Wireless-N 2200 */
#define    PCI_PRODUCT_INTEL_WL_6150_1    0x0885        /* Centrino Wireless-N 6150 */
#define    PCI_PRODUCT_INTEL_WL_6150_2    0x0886        /* Centrino Wireless-N 6150 */
#define    PCI_PRODUCT_INTEL_WL_2230_1    0x0887        /* Centrino Wireless-N 2230 */
#define    PCI_PRODUCT_INTEL_WL_2230_2    0x0888        /* Centrino Wireless-N 2230 */

static const struct pci_matchid iwn_devices[] = {
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_4965_1 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_4965_2 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_5100_1 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_5100_2 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_5150_1 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_5150_2 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_5300_1 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_5300_2 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_5350_1 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_5350_2 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_1000_1 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_1000_2 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_6300_1 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_6300_2 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_6200_1 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_6200_2 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_6050_1 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_6050_2 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_6005_1 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_6005_2 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_6030_1 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_6030_2 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_1030_1 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_1030_2 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_100_1 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_100_2 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_130_1 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_130_2 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_6235_1 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_6235_2 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_2230_1 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_2230_2 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_2200_1 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_2200_2 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_135_1 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_135_2 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_105_1 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_105_2 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_6150_1 },
    { PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_WL_6150_2 },
};

int ItlIwn::
iwn_match(struct IOPCIDevice *device)
{
    int devId = device->configRead16(kIOPCIConfigDeviceID);
    return pci_matchbyid(PCI_VENDOR_INTEL, devId, iwn_devices,
                         nitems(iwn_devices));
}

bool ItlIwn::
intrFilter(OSObject *object, IOFilterInterruptEventSource *src)
{
    ItlIwn *that = (ItlIwn*)object;
    IWN_WRITE(&that->com, IWN_INT_MASK, 0);
    return true;
}

bool ItlIwn::
iwn_attach(struct iwn_softc *sc, struct pci_attach_args *pa)
{
    struct ieee80211com *ic = &sc->sc_ic;
    struct _ifnet *ifp = &ic->ic_if;
    pcireg_t memtype, reg;
    int i, error;

    /* detach() owns every early-attach unwind below. */
    sc->sc_ih = NULL;
    sc->ih = NULL;
    sc->sc_pct = pa->pa_pc;
    sc->sc_pcitag = pa->pa_tag;
    sc->sc_dmat = pa->pa_dmat;
    __atomic_store_n(&sc->sc_cmd_in_flight, 0, __ATOMIC_RELEASE);
    /* An early attach unwind may run detach before the PMF locks exist.
     * CLOSED is safe in that case: no hook has been published yet. */
    __atomic_store_n(&sc->sc_mfp_pae_callback_state,
        IWN_MFP_PAE_CALLBACK_CLOSED, __ATOMIC_RELEASE);
    __atomic_store_n(&sc->sc_sae_engine_callback_state,
        IWN_SAE_ENGINE_CALLBACK_CLOSED, __ATOMIC_RELEASE);
    __atomic_store_n(&sc->sc_sae_engine_task_admission_state,
        IWN_SAE_ENGINE_TASK_ADMISSION_CLOSED, __ATOMIC_RELEASE);
    __atomic_store_n(&sc->sc_scan_lease_replay_task_admission_state,
        IWN_SCAN_LEASE_REPLAY_TASK_ADMISSION_CLOSED, __ATOMIC_RELEASE);
    sc->sc_sae_engine_lock = NULL;
    explicit_bzero(&sc->sc_sae_engine_owner,
        sizeof(sc->sc_sae_engine_owner));
    sc->sc_sae_engine = NULL;
    sc->sc_sae_engine_wcl_cancel_generation = 0;
    sc->sc_sae_engine_join_failure_generation = 0;
    sc->sc_sae_tx_join_failure_generation = 0;
    __atomic_store_n(&sc->sc_sae_engine_lifecycle_generation, 1,
        __ATOMIC_RELEASE);
    sc->sc_sae_engine_next_ticket = 0;
    sc->sc_sae_engine_next_relay_generation = 0;
    sc->sc_sae_engine_task_ready = false;
    sc->sc_sae_engine_stopping = true;
    sc->sc_sae_engine_detaching = false;
    sc->sc_sae_engine_runtime_enabled = false;
    sc->sc_scan_lease_lock = NULL;
    explicit_bzero(&sc->sc_scan_lease, sizeof(sc->sc_scan_lease));
    sc->sc_ap_transition_scan_blocked = false;
    sc->sc_sae_wcl_admission_reserved = false;
    sc->sc_sae_wcl_admission_requires_fresh_scan = false;
    sc->sc_sae_join_scan_block_generation = 0;
    sc->sc_sae_bss_loss_join_handoff_generation = 0;
    sc->sc_scan_lease_next_serial = 0;
    sc->sc_scan_lease_replay_task_ready = false;
    sc->sc_scan_lease_replay_pending = false;
    sc->sc_scan_lease_replay_nstate = IEEE80211_S_INIT;
    sc->sc_scan_lease_replay_arg = -1;
    sc->sc_scan_lease_replay_sae_generation = 0;
    sc->sc_wcl_join_cleanup_generation = 0;
    explicit_bzero(&sc->sc_wcl_initial_scan_pending,
                   sizeof(sc->sc_wcl_initial_scan_pending));

    /*
     * Get the offset of the PCI Express Capability Structure in PCI
     * Configuration Space.
     */
    error = pci_get_capability(sc->sc_pct, sc->sc_pcitag,
        PCI_CAP_PCIEXPRESS, &sc->sc_cap_off, NULL);
    if (error == 0) {
        XYLog(": PCIe capability structure not found!\n");
        return false;
    }

    /* Clear device-specific "PCI retry timeout" register (41h). */
    reg = pci_conf_read(sc->sc_pct, sc->sc_pcitag, 0x40);
    if (reg & 0xff00)
        pci_conf_write(sc->sc_pct, sc->sc_pcitag, 0x40, reg & ~0xff00);

    /* Hardware bug workaround. */
    reg = pci_conf_read(sc->sc_pct, sc->sc_pcitag, PCI_COMMAND_STATUS_REG);
    if (reg & PCI_COMMAND_INTERRUPT_DISABLE) {
        reg &= ~PCI_COMMAND_INTERRUPT_DISABLE;
        pci_conf_write(sc->sc_pct, sc->sc_pcitag,
            PCI_COMMAND_STATUS_REG, reg);
    }

    memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, IWN_PCI_BAR0);
    error = pci_mapreg_map(pa, IWN_PCI_BAR0, memtype, 0, &sc->sc_st,
        &sc->sc_sh, NULL, &sc->sc_sz, 0);
    if (error != 0) {
        XYLog(": can't map mem space\n");
        return false;
    }

    /* Install interrupt handler. */
    if (pci_intr_map_msi(pa, &sc->ih) != 0) {
        XYLog(": can't map interrupt\n");
        return false;
    }

    int msiIntrIndex = -1;
    for (int index = 0; ; index++)
    {
        int interruptType;
        int ret = pa->pa_tag->getInterruptType(index, &interruptType);
        if (ret != kIOReturnSuccess)
            break;
        if (interruptType & kIOInterruptTypePCIMessaged)
        {
            msiIntrIndex = index;
            break;
        }
    }
    if (msiIntrIndex == -1) {
        XYLog("%s: can't find MSI interrupt controller\n", DEVNAME(sc));
        return false;
    }

    sc->sc_ih = IOFilterInterruptEventSource::filterInterruptEventSource(this,
                                                                         (IOInterruptEventSource::Action)&ItlIwn::iwn_intr, &ItlIwn::intrFilter
                                                                         ,pa->pa_tag, msiIntrIndex);
    if (sc->sc_ih == NULL || pa->workloop->addEventSource(sc->sc_ih) != kIOReturnSuccess) {
        XYLog("%s: can't establish interrupt\n", DEVNAME(sc));
        return false;
    }
    sc->sc_ih->enable();

    /* Read hardware revision and attach. */
    sc->hw_type = (IWN_READ(sc, IWN_HW_REV) >> 4) & 0x1f;
    int pa_id = pa->pa_tag->configRead16(kIOPCIConfigDeviceID);
    if (sc->hw_type == IWN_HW_REV_TYPE_4965)
        error = iwn4965_attach(sc, pa_id);
    else
        error = iwn5000_attach(sc, pa_id);
    if (error != 0) {
        XYLog(": could not attach device\n");
        return false;
    }

    if ((error = iwn_hw_prepare(sc)) != 0) {
        XYLog(": hardware not ready\n");
        return false;
    }

    /* Read MAC address, channels, etc from EEPROM. */
    if ((error = iwn_read_eeprom(sc)) != 0) {
        XYLog(": could not read EEPROM\n");
        return false;
    }

    /* Allocate DMA memory for firmware transfers. */
    if ((error = iwn_alloc_fwmem(sc)) != 0) {
        XYLog(": could not allocate memory for firmware\n");
        return false;
    }

    /* Allocate "Keep Warm" page. */
    if ((error = iwn_alloc_kw(sc)) != 0) {
        XYLog(": could not allocate keep warm page\n");
        goto fail1;
    }

    /* Allocate ICT table for 5000 Series. */
    if (sc->hw_type != IWN_HW_REV_TYPE_4965 &&
        (error = iwn_alloc_ict(sc)) != 0) {
        XYLog(": could not allocate ICT table\n");
        goto fail2;
    }

    /* Allocate TX scheduler "rings". */
    if ((error = iwn_alloc_sched(sc)) != 0) {
        XYLog(": could not allocate TX scheduler rings\n");
        goto fail3;
    }

    /* Allocate TX rings (16 on 4965AGN, 20 on >=5000). */
    for (i = 0; i < sc->ntxqs; i++) {
        if ((error = iwn_alloc_tx_ring(sc, &sc->txq[i], i)) != 0) {
            XYLog(": could not allocate TX ring %d\n", i);
            goto fail4;
        }
    }

    /* Allocate RX ring. */
    if ((error = iwn_alloc_rx_ring(sc, &sc->rxq)) != 0) {
        XYLog(": could not allocate RX ring\n");
        goto fail4;
    }

    /* Clear pending interrupts. */
    IWN_WRITE(sc, IWN_INT, 0xffffffff);

    /* Count the number of available chains. */
    sc->ntxchains =
        ((sc->txchainmask >> 2) & 1) +
        ((sc->txchainmask >> 1) & 1) +
        ((sc->txchainmask >> 0) & 1);
    sc->nrxchains =
        ((sc->rxchainmask >> 2) & 1) +
        ((sc->rxchainmask >> 1) & 1) +
        ((sc->rxchainmask >> 0) & 1);

    taskq_init();
    sc->sc_scan_lease_lock = IOSimpleLockAlloc();
    if (sc->sc_scan_lease_lock == NULL) {
        XYLog("%s: scan lease owner unavailable\n", DEVNAME(sc));
        goto fail4;
    }
    explicit_bzero(&sc->sc_scan_lease, sizeof(sc->sc_scan_lease));
    sc->sc_scan_lease.phase = IWN_SCAN_LEASE_IDLE;
    sc->sc_ap_transition_scan_blocked = false;
    sc->sc_sae_wcl_admission_reserved = false;
    sc->sc_sae_wcl_admission_requires_fresh_scan = false;
    sc->sc_sae_join_scan_block_generation = 0;
    sc->sc_sae_bss_loss_join_handoff_generation = 0;
    sc->sc_scan_lease_next_serial = 0;
    __atomic_store_n(&sc->sc_scan_lease_replay_task_admission_state,
        IWN_SCAN_LEASE_REPLAY_TASK_ADMISSION_CLOSED, __ATOMIC_RELEASE);
    sc->sc_scan_lease_replay_task_ready = false;
    sc->sc_scan_lease_replay_pending = false;
    sc->sc_scan_lease_replay_nstate = IEEE80211_S_INIT;
    sc->sc_scan_lease_replay_arg = -1;
    sc->sc_scan_lease_replay_sae_generation = 0;
    sc->sc_wcl_join_cleanup_generation = 0;
    explicit_bzero(&sc->sc_wcl_initial_scan_pending,
                   sizeof(sc->sc_wcl_initial_scan_pending));
    sc->sc_sae_tx_lifecycle_lock = IOLockAlloc();
    if (sc->sc_sae_tx_lifecycle_lock == NULL) {
        XYLog("%s: SAE TX lifecycle unavailable\n", DEVNAME(sc));
        goto fail4;
    }
    sc->sc_sae_tx_lifecycle_active = 0;
    sc->sc_sae_tx_lifecycle_closed = true;
    sc->sc_sae_tx_detaching = false;
    sc->sc_sae_tx_task_ready = false;
    sc->sc_sae_tx_lock = IOSimpleLockAlloc();
    if (sc->sc_sae_tx_lock == NULL) {
        XYLog("%s: SAE TX owner unavailable\n", DEVNAME(sc));
        goto fail4;
    }
    sc->sc_sae_tx_active = false;
    sc->sc_sae_tx_doorbelled = false;
    sc->sc_sae_tx_stopping = true;
    sc->sc_sae_tx_active_ticket = 0;
    sc->sc_sae_tx_cancel_through = 0;
    sc->sc_sae_tx_direct_cancel_through = 0;
    sc->sc_sae_tx_generation = 1;
    sc->sc_sae_tx_active_generation = 0;
    sc->sc_sae_tx_last_event_valid = false;
    explicit_bzero(&sc->sc_sae_tx_active_event,
        sizeof(sc->sc_sae_tx_active_event));
    explicit_bzero(&sc->sc_sae_tx_last_event,
        sizeof(sc->sc_sae_tx_last_event));
    explicit_bzero(sc->sc_sae_tx_eventq, sizeof(sc->sc_sae_tx_eventq));
    sc->sc_sae_tx_event_head = 0;
    sc->sc_sae_tx_event_tail = 0;
    sc->sc_sae_tx_event_count = 0;

    /* The direct engine remains inert until a completed hardware init opens
     * its callback lease.  Initialize every field before any later attach
     * failure can enter detach(). */
    sc->sc_sae_engine_lock = IOSimpleLockAlloc();
    if (sc->sc_sae_engine_lock == NULL)
        XYLog("%s: direct SAE owner unavailable\n", DEVNAME(sc));
    explicit_bzero(&sc->sc_sae_engine_owner,
        sizeof(sc->sc_sae_engine_owner));
    sc->sc_sae_engine = NULL;
    sc->sc_sae_engine_wcl_cancel_generation = 0;
    sc->sc_sae_engine_join_failure_generation = 0;
    __atomic_store_n(&sc->sc_sae_engine_lifecycle_generation, 1,
        __ATOMIC_RELEASE);
    __atomic_store_n(&sc->sc_sae_engine_task_admission_state,
        IWN_SAE_ENGINE_TASK_ADMISSION_CLOSED, __ATOMIC_RELEASE);
    sc->sc_sae_engine_next_ticket = 0;
    sc->sc_sae_engine_next_relay_generation = 0;
    sc->sc_sae_engine_task_ready = false;
    sc->sc_sae_engine_stopping = true;
    sc->sc_sae_engine_detaching = false;
    sc->sc_sae_engine_runtime_enabled =
        iwn_sae_auth_transport_runtime_opted_in() &&
        iwn_sae_wcl_credential_runtime_opted_in();

    /* Allocate and zero the product WCL credential slot with the SAE
     * lifecycle so every attach-unwind path can scrub it uniformly. */
    sc->sc_sae_wcl_credential_lock = IOSimpleLockAlloc();
    if (sc->sc_sae_wcl_credential_lock == NULL)
        XYLog("%s: SAE WCL staging unavailable\n", DEVNAME(sc));
    sc->sc_sae_wcl_credential_staged = false;
    sc->sc_sae_wcl_credential_pending = false;
    sc->sc_sae_wcl_credential_active = false;
    sc->sc_sae_bss_loss_recovery_armed = false;
    sc->sc_sae_driver_reset_recovery_pending = false;
    sc->sc_sae_bss_loss_recovery_generation = 0;
    sc->sc_sae_wcl_credential_cancel_valid = false;
    sc->sc_sae_wcl_credential_cancel_through_generation = 0;
    explicit_bzero(&sc->sc_sae_wcl_credential,
        sizeof(sc->sc_sae_wcl_credential));

    /* This is IWN-private and intentionally not the controller policy gate. */
    fSaeTxGate = IOCommandGate::commandGate(this);
    if (fSaeTxGate == NULL || pa->workloop == NULL ||
        pa->workloop->addEventSource(fSaeTxGate) != kIOReturnSuccess) {
        if (fSaeTxGate != NULL) {
            fSaeTxGate->release();
            fSaeTxGate = NULL;
        }
        XYLog("%s: could not establish SAE TX workloop gate\n", DEVNAME(sc));
        goto fail4;
    }

    sc->sc_mfp_pae_lock = IOSimpleLockAlloc();
    if (sc->sc_mfp_pae_lock == NULL) {
        XYLog("%s: software PMF owner unavailable\n", DEVNAME(sc));
    }
    explicit_bzero(&sc->sc_mfp_pae_txn, sizeof(sc->sc_mfp_pae_txn));
    explicit_bzero(&sc->sc_mfp_pae_successor,
        sizeof(sc->sc_mfp_pae_successor));
    sc->sc_mfp_pae_lifecycle_generation = 1;
    sc->sc_mfp_pae_detaching = false;
    sc->sc_mfp_pae_stopping = true;
    sc->sc_mfp_pae_task_ready = false;
    sc->sc_mfp_pae_runtime_enabled = iwn_mfp_pae_runtime_opted_in();
    
    ic->ic_phytype = IEEE80211_T_OFDM;    /* not only, but not used */
    ic->ic_opmode = IEEE80211_M_STA;    /* default to BSS mode */
    ic->ic_state = IEEE80211_S_INIT;

    /* Set device capabilities. */
    ic->ic_caps =
        IEEE80211_C_WEP |        /* WEP */
        IEEE80211_C_RSN |        /* WPA/RSN */
        IEEE80211_C_SCANALL |    /* device scans all channels at once */
        IEEE80211_C_SCANALLBAND |    /* driver scans all bands at once */
        IEEE80211_C_MONITOR |    /* monitor mode supported */
        IEEE80211_C_SHSLOT |    /* short slot time supported */
        IEEE80211_C_SHPREAMBLE |    /* short preamble supported */
        IEEE80211_C_WNM_BSS_TRANSITION | /* generic 802.11v BTM */
        IEEE80211_C_PMGT;        /* power saving supported */

    /* No optional HT features supported for now, */
    ic->ic_htcaps = 0;
    ic->ic_htxcaps = 0;
    ic->ic_txbfcaps = 0;
    ic->ic_aselcaps = 0;
    ic->ic_vhtcaps = 0;
    ic->ic_hecaps = 0;
    ic->ic_vht_tx_mcs_map = 0;
    ic->ic_vht_rx_mcs_map = 0;
    ic->ic_vht_tx_highest = 0;
    ic->ic_vht_rx_highest = 0;
    memset(ic->ic_vht_sup_mcs, 0, sizeof(ic->ic_vht_sup_mcs));
    memset(&ic->ic_he_cap_elem, 0, sizeof(ic->ic_he_cap_elem));
    memset(&ic->ic_he_mcs_nss_supp, 0, sizeof(ic->ic_he_mcs_nss_supp));
    memset(ic->ic_ppe_thres, 0, sizeof(ic->ic_ppe_thres));
    ic->ic_ampdu_params = (IEEE80211_AMPDU_PARAM_SS_4 | 0x3 /* 64k */);
    if (sc->sc_flags & IWN_FLAG_HAS_11N) {
        ic->ic_caps |= (IEEE80211_C_QOS | IEEE80211_C_TX_AMPDU |
            IEEE80211_C_AMSDU_IN_AMPDU);
        /* Set HT capabilities. */
        ic->ic_htcaps = IEEE80211_HTCAP_SGI20;
        /* 6200 devices have issues with SGI40 for some reason. */
        if ((sc->sc_flags & IWN_FLAG_INTERNAL_PA) == 0)
            ic->ic_htcaps |= IEEE80211_HTCAP_SGI40;
        ic->ic_htcaps |= IEEE80211_HTCAP_CBW20_40;
#if IWN_RBUF_SIZE == 8192
        ic->ic_htcaps |=
            IEEE80211_HTCAP_AMSDU7935;
#endif
#ifdef notyet
        if (sc->hw_type != IWN_HW_REV_TYPE_4965)
            ic->ic_htcaps |= IEEE80211_HTCAP_GF;
#endif
        if (sc->hw_type == IWN_HW_REV_TYPE_6050)
            ic->ic_htcaps |= IEEE80211_HTCAP_SMPS_DYN << IEEE80211_HTCAP_SMPS_SHIFT;
        else
            ic->ic_htcaps |= IEEE80211_HTCAP_SMPS_DIS << IEEE80211_HTCAP_SMPS_SHIFT;
    }

    /* Set supported legacy rates. */
    ic->ic_sup_rates[IEEE80211_MODE_11B] = ieee80211_std_rateset_11b;
    ic->ic_sup_rates[IEEE80211_MODE_11G] = ieee80211_std_rateset_11g;
    if (sc->sc_flags & IWN_FLAG_HAS_5GHZ) {
        ic->ic_sup_rates[IEEE80211_MODE_11A] =
            ieee80211_std_rateset_11a;
    }
    if (sc->sc_flags & IWN_FLAG_HAS_11N) {
        /* Set supported HT rates. */
        if (ic->ic_userflags & IEEE80211_F_NOMIMO)
            sc->ntxchains = sc->nrxchains = 1;

        int ntxstreams = sc->ntxchains;
        int nrxstreams = sc->nrxchains;

        ic->ic_sup_mcs[0] = 0xff;        /* MCS 0-7 */
        if (nrxstreams > 1)
            ic->ic_sup_mcs[1] = 0xff;    /* MCS 8-15 */
        if (nrxstreams > 2)
            ic->ic_sup_mcs[2] = 0xff;    /* MCS 16-23 */

        ic->ic_tx_mcs_set = IEEE80211_TX_MCS_SET_DEFINED;
        if (ntxstreams != nrxstreams) {
            ic->ic_tx_mcs_set |= IEEE80211_TX_RX_MCS_NOT_EQUAL;
            ic->ic_tx_mcs_set |= (ntxstreams - 1) << 2;
        }
    }

    /* IBSS channel undefined for now. */
    ic->ic_ibss_chan = &ic->ic_channels[0];
    
    ic->ic_max_rssi = IWN_MAX_DBM - IWN_MIN_DBM;

    ifp->if_softc = sc;
    ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST | IFF_DEBUG;
    ifp->if_ioctl = iwn_ioctl;
    ifp->if_start = iwn_start;
    ifp->if_watchdog = iwn_watchdog;
    memcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);

    if_attach(ifp);
    ieee80211_ifattach(ifp, getController());
    ic->ic_node_alloc = iwn_node_alloc;
    ic->ic_bgscan_start = iwn_bgscan;
    ic->ic_bgscan_abort = iwn_wnm_bgscan_abort;
    ic->ic_newassoc = iwn_newassoc;
    ic->ic_updateedca = iwn_updateedca;
    ic->ic_set_key = iwn_set_key;
    ic->ic_delete_key = iwn_delete_key;
    ic->ic_updateprot = iwn_updateprot;
    ic->ic_updateslot = iwn_updateslot;
    ic->ic_ampdu_rx_start = iwn_ampdu_rx_start;
    ic->ic_ampdu_rx_stop = iwn_ampdu_rx_stop;
    ic->ic_ampdu_tx_start = iwn_ampdu_tx_start;
    ic->ic_ampdu_tx_stop = iwn_ampdu_tx_stop;
    ic->ic_update_chw = iwn_update_chw;

    /* Override 802.11 state transition machine. */
    sc->sc_newstate = ic->ic_newstate;
    ic->ic_newstate = iwn_newstate;
    ic->ic_wcl_join_failure_scan = iwn_wcl_join_failure_scan;
    ieee80211_media_init(ifp);

    sc->amrr.amrr_min_success_threshold =  1;
    sc->amrr.amrr_max_success_threshold = 15;

#if NBPFILTER > 0
    iwn_radiotap_attach(sc);
#endif
    timeout_set(&sc->calib_to, iwn_calib_timeout, sc);
//    rw_init(&sc->sc_rwlock, "iwnlock");
    task_set(&sc->init_task, iwn_init_task, sc, "iwn_init_task");
    __atomic_store_n(&sc->init_retry_count, 0, __ATOMIC_RELEASE);
    task_set(&sc->scan_lease_replay_task, iwn_scan_lease_replay_task, sc,
        "iwn_scan_lease_replay_task");
    task_set(&sc->sae_tx_task, iwn_sae_tx_task, sc, "iwn_sae_tx_task");
    task_set(&sc->sae_engine_task, iwn_sae_engine_task, sc,
        "iwn_sae_engine_task");
    task_set(&sc->mfp_pae_task, iwn_mfp_pae_task, sc, "iwn_mfp_pae_task");
    sc->sc_sae_tx_task_ready = true;
    sc->sc_scan_lease_replay_task_ready = true;
    __atomic_store_n(&sc->sc_scan_lease_replay_task_admission_state, 0,
        __ATOMIC_RELEASE);
    ic->ic_newstate_preflight = iwn_newstate_preflight;
    sc->sc_sae_engine_task_ready = sc->sc_sae_engine_lock != NULL;
    if (sc->sc_sae_engine_task_ready)
        __atomic_store_n(&sc->sc_sae_engine_task_admission_state, 0,
            __ATOMIC_RELEASE);
    sc->sc_mfp_pae_task_ready = sc->sc_mfp_pae_lock != NULL;
    iwn_publish_mfp_capability(sc);

    iwx_auth_diag_init();
    return true;

    /* Free allocated memory if something failed during attachment. */
fail4:    while (--i >= 0)
        iwn_free_tx_ring(sc, &sc->txq[i]);
    iwn_free_sched(sc);
fail3:    if (sc->ict != NULL)
        iwn_free_ict(sc);
fail2:    iwn_free_kw(sc);
fail1:    iwn_free_fwmem(sc);
    return false;
}

int ItlIwn::
iwn4965_attach(struct iwn_softc *sc, pci_product_id_t pid)
{
    struct iwn_ops *ops = &sc->ops;

    ops->load_firmware = iwn4965_load_firmware;
    ops->read_eeprom = iwn4965_read_eeprom;
    ops->post_alive = iwn4965_post_alive;
    ops->nic_config = iwn4965_nic_config;
    ops->reset_sched = iwn4965_reset_sched;
    ops->update_sched = iwn4965_update_sched;
    ops->update_rxon = iwn4965_update_rxon;
    ops->get_temperature = iwn4965_get_temperature;
    ops->get_rssi = iwn4965_get_rssi;
    ops->set_txpower = iwn4965_set_txpower;
    ops->init_gains = iwn4965_init_gains;
    ops->set_gains = iwn4965_set_gains;
    ops->add_node = iwn4965_add_node;
    ops->tx_done = iwn4965_tx_done;
    ops->ampdu_tx_start = iwn4965_ampdu_tx_start;
    ops->ampdu_tx_stop = iwn4965_ampdu_tx_stop;
    sc->ntxqs = IWN4965_NTXQUEUES;
    sc->first_agg_txq = IWN4965_FIRST_AGG_TXQUEUE;
    sc->ndmachnls = IWN4965_NDMACHNLS;
    sc->broadcast_id = IWN4965_ID_BROADCAST;
    sc->command_queue = IWN_DEFAULT_CMD_QUEUE;
    sc->eeprom_pan_capable = false;
    sc->rxonsz = IWN4965_RXONSZ;
    sc->schedsz = IWN4965_SCHEDSZ;
    sc->fw_text_maxsz = IWN4965_FW_TEXT_MAXSZ;
    sc->fw_data_maxsz = IWN4965_FW_DATA_MAXSZ;
    sc->fwsz = IWN4965_FWSZ;
    sc->sched_txfact_addr = IWN4965_SCHED_TXFACT;
    sc->limits = &iwn4965_sensitivity_limits;
    sc->fwname = "iwn-4965";
    /* Override chains masks, ROM is known to be broken. */
    sc->txchainmask = IWN_ANT_AB;
    sc->rxchainmask = IWN_ANT_ABC;

    return 0;
}

int ItlIwn::
iwn5000_attach(struct iwn_softc *sc, pci_product_id_t pid)
{
    struct iwn_ops *ops = &sc->ops;

    ops->load_firmware = iwn5000_load_firmware;
    ops->read_eeprom = iwn5000_read_eeprom;
    ops->post_alive = iwn5000_post_alive;
    ops->nic_config = iwn5000_nic_config;
    ops->reset_sched = iwn5000_reset_sched;
    ops->update_sched = iwn5000_update_sched;
    ops->update_rxon = iwn5000_update_rxon;
    ops->get_temperature = iwn5000_get_temperature;
    ops->get_rssi = iwn5000_get_rssi;
    ops->set_txpower = iwn5000_set_txpower;
    ops->init_gains = iwn5000_init_gains;
    ops->set_gains = iwn5000_set_gains;
    ops->add_node = iwn5000_add_node;
    ops->tx_done = iwn5000_tx_done;
    ops->ampdu_tx_start = iwn5000_ampdu_tx_start;
    ops->ampdu_tx_stop = iwn5000_ampdu_tx_stop;
    sc->ntxqs = IWN5000_NTXQUEUES;
    sc->first_agg_txq = IWN5000_FIRST_AGG_TXQUEUE;
    sc->ndmachnls = IWN5000_NDMACHNLS;
    sc->broadcast_id = IWN5000_ID_BROADCAST;
    sc->command_queue = IWN_DEFAULT_CMD_QUEUE;
    sc->eeprom_pan_capable = false;
    sc->rxonsz = IWN5000_RXONSZ;
    sc->schedsz = IWN5000_SCHEDSZ;
    sc->fw_text_maxsz = IWN5000_FW_TEXT_MAXSZ;
    sc->fw_data_maxsz = IWN5000_FW_DATA_MAXSZ;
    sc->fwsz = IWN5000_FWSZ;
    sc->sched_txfact_addr = IWN5000_SCHED_TXFACT;

    switch (sc->hw_type) {
    case IWN_HW_REV_TYPE_5100:
        sc->limits = &iwn5000_sensitivity_limits;
        sc->fwname = "iwn-5000";
        /* Override chains masks, ROM is known to be broken. */
        sc->txchainmask = IWN_ANT_B;
        sc->rxchainmask = IWN_ANT_AB;
        break;
    case IWN_HW_REV_TYPE_5150:
        sc->limits = &iwn5150_sensitivity_limits;
        sc->fwname = "iwn-5150";
        break;
    case IWN_HW_REV_TYPE_5300:
    case IWN_HW_REV_TYPE_5350:
        sc->limits = &iwn5000_sensitivity_limits;
        sc->fwname = "iwn-5000";
        break;
    case IWN_HW_REV_TYPE_1000:
        sc->limits = &iwn1000_sensitivity_limits;
        sc->fwname = "iwn-1000";
        break;
    case IWN_HW_REV_TYPE_6000:
        sc->limits = &iwn6000_sensitivity_limits;
        sc->fwname = "iwn-6000";
        if (pid == PCI_PRODUCT_INTEL_WL_6200_1 ||
            pid == PCI_PRODUCT_INTEL_WL_6200_2) {
            sc->sc_flags |= IWN_FLAG_INTERNAL_PA;
            /* Override chains masks, ROM is known to be broken. */
            sc->txchainmask = IWN_ANT_BC;
            sc->rxchainmask = IWN_ANT_BC;
        }
        break;
    case IWN_HW_REV_TYPE_6050:
        sc->limits = &iwn6000_sensitivity_limits;
        sc->fwname = "iwn-6050";
        break;
    case IWN_HW_REV_TYPE_6005:
        sc->limits = &iwn6000_sensitivity_limits;
        if (pid != PCI_PRODUCT_INTEL_WL_6005_1 &&
            pid != PCI_PRODUCT_INTEL_WL_6005_2) {
            sc->fwname = "iwn-6030";
            sc->sc_flags |= IWN_FLAG_ADV_BT_COEX;
        } else
            sc->fwname = "iwn-6005";
        break;
    case IWN_HW_REV_TYPE_2030:
        sc->limits = &iwn2000_sensitivity_limits;
        sc->fwname = "iwn-2030";
        sc->sc_flags |= IWN_FLAG_ADV_BT_COEX;
        break;
    case IWN_HW_REV_TYPE_2000:
        sc->limits = &iwn2000_sensitivity_limits;
        sc->fwname = "iwn-2000";
        break;
    case IWN_HW_REV_TYPE_135:
        sc->limits = &iwn2000_sensitivity_limits;
        sc->fwname = "iwn-135";
        sc->sc_flags |= IWN_FLAG_ADV_BT_COEX;
        break;
    case IWN_HW_REV_TYPE_105:
        sc->limits = &iwn2000_sensitivity_limits;
        sc->fwname = "iwn-105";
        break;
    default:
        XYLog(": adapter type %d not supported\n", sc->hw_type);
        return ENOTSUP;
    }
    return 0;
}

#if NBPFILTER > 0
/*
 * Attach the interface to 802.11 radiotap.
 */
void ItlIwn::
iwn_radiotap_attach(struct iwn_softc *sc)
{
    bpfattach(&sc->sc_drvbpf, &sc->sc_ic.ic_if, DLT_IEEE802_11_RADIO,
        sizeof (struct ieee80211_frame) + IEEE80211_RADIOTAP_HDRLEN);

    sc->sc_rxtap_len = sizeof sc->sc_rxtapu;
    sc->sc_rxtap.wr_ihdr.it_len = htole16(sc->sc_rxtap_len);
    sc->sc_rxtap.wr_ihdr.it_present = htole32(IWN_RX_RADIOTAP_PRESENT);

    sc->sc_txtap_len = sizeof sc->sc_txtapu;
    sc->sc_txtap.wt_ihdr.it_len = htole16(sc->sc_txtap_len);
    sc->sc_txtap.wt_ihdr.it_present = htole32(IWN_TX_RADIOTAP_PRESENT);
}
#endif

int ItlIwn::
iwn_activate(struct iwn_softc *sc, int act)
{
    struct _ifnet *ifp = &sc->sc_ic.ic_if;

    switch (act) {
    case DVACT_QUIESCE:
        if (ifp->if_flags & IFF_RUNNING)
            iwn_stop(ifp);
        break;
    case DVACT_WAKEUP:
        iwn_wakeup(sc);
        break;
    }

    return 0;
}

void ItlIwn::
iwn_wakeup(struct iwn_softc *sc)
{
    pcireg_t reg;

    /* Clear device-specific "PCI retry timeout" register (41h). */
    reg = pci_conf_read(sc->sc_pct, sc->sc_pcitag, 0x40);
    if (reg & 0xff00)
        pci_conf_write(sc->sc_pct, sc->sc_pcitag, 0x40, reg & ~0xff00);
    __atomic_store_n(&sc->init_retry_count, 0, __ATOMIC_RELEASE);
    task_add(systq, &sc->init_task);
}

void ItlIwn::
iwn_init_task(void *arg1)
{
    struct iwn_softc *sc = (struct iwn_softc *)arg1;
    struct _ifnet *ifp = &sc->sc_ic.ic_if;
    ItlIwn *that = container_of(sc, ItlIwn, com);
    int error = 0;
    int s;

//    rw_enter_write(&sc->sc_rwlock);
    s = splnet();

    /* The interrupt action leaves a fatal firmware fault with device IRQs
     * masked.  timeout/state/hardware teardown crosses macOS work-loop
     * boundaries, so it must run here rather than in that interrupt action. */
    if (sc->sc_flags & IWN_FLAG_FATAL_RECOVERY) {
        sc->sc_flags &= ~IWN_FLAG_FATAL_RECOVERY;
        if (ifp->if_flags & IFF_RUNNING) {
            /* The reference publishes a distinct DriverReset before it
             * halts the live Join FSM.  Retain the equivalent private SAE
             * join owner before iwn_stop() destroys the selected BSS. */
            that->iwn_sae_driver_reset_recovery_prepare(sc);
            that->iwn_stop(ifp);
        }
    }

    if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) == IFF_UP)
        error = that->iwn_init(ifp);

    if (error == 0) {
        __atomic_store_n(&sc->init_retry_count, 0, __ATOMIC_RELEASE);
    } else if ((ifp->if_flags & IFF_UP) != 0) {
        const u_int8_t attempt = __atomic_add_fetch(
            &sc->init_retry_count, 1, __ATOMIC_ACQ_REL);

        /* Tahoe's reference powerOn path has a five-attempt recovery
         * counter before its permanent-failure terminal.  Retain that
         * bounded ownership here: a failed first scan keeps the controller
         * unavailable and retries the complete firmware epoch, while a
         * persistent hardware failure cannot spin systq forever. */
        if (attempt < 5) {
            sc->sc_flags |= IWN_FLAG_FATAL_RECOVERY;
            (void)task_add(systq, &sc->init_task);
        } else {
            sc->sc_flags &= ~IWN_FLAG_FATAL_RECOVERY;
            (void)task_del(systq, &sc->init_task);
            XYLog("%s: power-on recovery exhausted after %u attempts\n",
                  sc->sc_dev.dv_xname, (unsigned)attempt);
        }
    }

    splx(s);
//    rw_exit_write(&sc->sc_rwlock);
}

int
iwn_nic_lock(struct iwn_softc *sc)
{
    int ntries;

    /* Request exclusive access to NIC. */
    IWN_SETBITS(sc, IWN_GP_CNTRL, IWN_GP_CNTRL_MAC_ACCESS_REQ);

    /* Spin until we actually get the lock. */
    for (ntries = 0; ntries < 1000; ntries++) {
        if ((IWN_READ(sc, IWN_GP_CNTRL) &
             (IWN_GP_CNTRL_MAC_ACCESS_ENA | IWN_GP_CNTRL_SLEEP)) ==
            IWN_GP_CNTRL_MAC_ACCESS_ENA)
            return 0;
        DELAY(10);
    }
    
    XYLog("%s acquiring device failed.", __FUNCTION__);
    return ETIMEDOUT;
}

void
iwn_nic_unlock(struct iwn_softc *sc)
{
    IWN_CLRBITS(sc, IWN_GP_CNTRL, IWN_GP_CNTRL_MAC_ACCESS_REQ);
}

uint32_t
iwn_prph_read(struct iwn_softc *sc, uint32_t addr)
{
    IWN_WRITE(sc, IWN_PRPH_RADDR, IWN_PRPH_DWORD | addr);
    IWN_BARRIER_READ_WRITE(sc);
    return IWN_READ(sc, IWN_PRPH_RDATA);
}

void
iwn_prph_write(struct iwn_softc *sc, uint32_t addr, uint32_t data)
{
    IWN_WRITE(sc, IWN_PRPH_WADDR, IWN_PRPH_DWORD | addr);
    IWN_BARRIER_WRITE(sc);
    IWN_WRITE(sc, IWN_PRPH_WDATA, data);
}

void
iwn_prph_setbits(struct iwn_softc *sc, uint32_t addr, uint32_t mask)
{
    iwn_prph_write(sc, addr, iwn_prph_read(sc, addr) | mask);
}

void
iwn_prph_clrbits(struct iwn_softc *sc, uint32_t addr, uint32_t mask)
{
    iwn_prph_write(sc, addr, iwn_prph_read(sc, addr) & ~mask);
}

void
iwn_prph_write_region_4(struct iwn_softc *sc, uint32_t addr,
    const uint32_t *data, int count)
{
    for (; count > 0; count--, data++, addr += 4)
        iwn_prph_write(sc, addr, *data);
}

uint32_t
iwn_mem_read(struct iwn_softc *sc, uint32_t addr)
{
    IWN_WRITE(sc, IWN_MEM_RADDR, addr);
    IWN_BARRIER_READ_WRITE(sc);
    return IWN_READ(sc, IWN_MEM_RDATA);
}

void
iwn_mem_write(struct iwn_softc *sc, uint32_t addr, uint32_t data)
{
    IWN_WRITE(sc, IWN_MEM_WADDR, addr);
    IWN_BARRIER_WRITE(sc);
    IWN_WRITE(sc, IWN_MEM_WDATA, data);
}

void
iwn_mem_write_2(struct iwn_softc *sc, uint32_t addr, uint16_t data)
{
    uint32_t tmp;

    tmp = iwn_mem_read(sc, addr & ~3);
    if (addr & 3)
        tmp = (tmp & 0x0000ffff) | data << 16;
    else
        tmp = (tmp & 0xffff0000) | data;
    iwn_mem_write(sc, addr & ~3, tmp);
}

#ifdef IWN_DEBUG

void
iwn_mem_read_region_4(struct iwn_softc *sc, uint32_t addr, uint32_t *data,
    int count)
{
    for (; count > 0; count--, addr += 4)
        *data++ = iwn_mem_read(sc, addr);
}

#endif

void
iwn_mem_set_region_4(struct iwn_softc *sc, uint32_t addr, uint32_t val,
    int count)
{
    for (; count > 0; count--, addr += 4)
        iwn_mem_write(sc, addr, val);
}

int ItlIwn::
iwn_eeprom_lock(struct iwn_softc *sc)
{
    int i, ntries;

    for (i = 0; i < 100; i++) {
        /* Request exclusive access to EEPROM. */
        IWN_SETBITS(sc, IWN_HW_IF_CONFIG,
            IWN_HW_IF_CONFIG_EEPROM_LOCKED);

        /* Spin until we actually get the lock. */
        for (ntries = 0; ntries < 100; ntries++) {
            if (IWN_READ(sc, IWN_HW_IF_CONFIG) &
                IWN_HW_IF_CONFIG_EEPROM_LOCKED)
                return 0;
            DELAY(10);
        }
    }
    return ETIMEDOUT;
}

void
iwn_eeprom_unlock(struct iwn_softc *sc)
{
    IWN_CLRBITS(sc, IWN_HW_IF_CONFIG, IWN_HW_IF_CONFIG_EEPROM_LOCKED);
}

/*
 * Initialize access by host to One Time Programmable ROM.
 * NB: This kind of ROM can be found on 1000 or 6000 Series only.
 */
int ItlIwn::
iwn_init_otprom(struct iwn_softc *sc)
{
    uint16_t prev, base, next;
    int count, error;

    /* Wait for clock stabilization before accessing prph. */
    if ((error = iwn_clock_wait(sc)) != 0)
        return error;

    if ((error = iwn_nic_lock(sc)) != 0)
        return error;
    iwn_prph_setbits(sc, IWN_APMG_PS, IWN_APMG_PS_RESET_REQ);
    DELAY(5);
    iwn_prph_clrbits(sc, IWN_APMG_PS, IWN_APMG_PS_RESET_REQ);
    iwn_nic_unlock(sc);

    /* Set auto clock gate disable bit for HW with OTP shadow RAM. */
    if (sc->hw_type != IWN_HW_REV_TYPE_1000) {
        IWN_SETBITS(sc, IWN_DBG_LINK_PWR_MGMT,
            IWN_RESET_LINK_PWR_MGMT_DIS);
    }
    IWN_CLRBITS(sc, IWN_EEPROM_GP, IWN_EEPROM_GP_IF_OWNER);
    /* Clear ECC status. */
    IWN_SETBITS(sc, IWN_OTP_GP,
        IWN_OTP_GP_ECC_CORR_STTS | IWN_OTP_GP_ECC_UNCORR_STTS);

    /*
     * Find the block before last block (contains the EEPROM image)
     * for HW without OTP shadow RAM.
     */
    if (sc->hw_type == IWN_HW_REV_TYPE_1000) {
        /* Switch to absolute addressing mode. */
        IWN_CLRBITS(sc, IWN_OTP_GP, IWN_OTP_GP_RELATIVE_ACCESS);
        base = 0;
        for (count = 0; count < IWN1000_OTP_NBLOCKS; count++) {
            error = iwn_read_prom_data(sc, base, &next, 2);
            if (error != 0)
                return error;
            if (next == 0)    /* End of linked-list. */
                break;
            prev = base;
            base = letoh16(next);
        }
        if (count == 0 || count == IWN1000_OTP_NBLOCKS)
            return EIO;
        /* Skip "next" word. */
        sc->prom_base = prev + 1;
    }
    return 0;
}

int ItlIwn::
iwn_read_prom_data(struct iwn_softc *sc, uint32_t addr, void *data, int count)
{
    uint8_t *out = (uint8_t *)data;
    uint32_t val, tmp;
    int ntries;

    addr += sc->prom_base;
    for (; count > 0; count -= 2, addr++) {
        IWN_WRITE(sc, IWN_EEPROM, addr << 2);
        for (ntries = 0; ntries < 10; ntries++) {
            val = IWN_READ(sc, IWN_EEPROM);
            if (val & IWN_EEPROM_READ_VALID)
                break;
            DELAY(5);
        }
        if (ntries == 10) {
            XYLog("%s: timeout reading ROM at 0x%x\n",
                sc->sc_dev.dv_xname, addr);
            return ETIMEDOUT;
        }
        if (sc->sc_flags & IWN_FLAG_HAS_OTPROM) {
            /* OTPROM, check for ECC errors. */
            tmp = IWN_READ(sc, IWN_OTP_GP);
            if (tmp & IWN_OTP_GP_ECC_UNCORR_STTS) {
                XYLog("%s: OTPROM ECC error at 0x%x\n",
                    sc->sc_dev.dv_xname, addr);
                return EIO;
            }
            if (tmp & IWN_OTP_GP_ECC_CORR_STTS) {
                /* Correctable ECC error, clear bit. */
                IWN_SETBITS(sc, IWN_OTP_GP,
                    IWN_OTP_GP_ECC_CORR_STTS);
            }
        }
        *out++ = val >> 16;
        if (count > 1)
            *out++ = val >> 24;
    }
    return 0;
}

bool allocDmaMemory2(struct iwn_dma_info *dma, size_t size, int alignment)
{
    IOBufferMemoryDescriptor *bmd;
    IODMACommand::Segment64 seg;
    UInt64 ofs = 0;
    UInt32 numSegs = 1;
    
    /* PASSTHROUGH TX FIX (iwn): constrain all iwn DMA buffers to the low 4GB.
     * On a >4GB VM the TX scheduler byte-count table (sched_dma) and keep-warm
     * page can otherwise be placed above 4GB; the iwn 5000/6000 TX-scheduler
     * DMA path then reads garbage byte-counts and the firmware never services
     * the data TX queues (RX and the command queue still work). A 32-bit
     * physical mask keeps these small structures reachable. */
    bmd = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task, kIODirectionInOut | kIOMemoryPhysicallyContiguous | kIOMapInhibitCache, size, DMA_BIT_MASK(32));
    
    if (bmd == NULL) {
        XYLog("%s alloc DMA memory failed.\n", __FUNCTION__);
        return false;
    }
    
    bmd->prepare();
    IODMACommand *cmd = IODMACommand::withSpecification(kIODMACommandOutputHost64, 64, 0, IODMACommand::kMapped, 0, alignment);
    
    if (cmd == NULL) {
        XYLog("%s alloc IODMACommand memory failed.\n", __FUNCTION__);
        bmd->complete();
        bmd->release();
        return false;
    }
    
    cmd->setMemoryDescriptor(bmd);

    if (cmd->gen64IOVMSegments(&ofs, &seg, &numSegs) != kIOReturnSuccess) {
        cmd->release();
        cmd = NULL;
        bmd->complete();
        bmd->release();
        bmd = NULL;
        return false;
    }
    dma->paddr = seg.fIOVMAddr;
    dma->vaddr = bmd->getBytesNoCopy();
    dma->size = size;
    dma->buffer = bmd;
    dma->cmd = cmd;
    memset(dma->vaddr, 0, dma->size);
    return true;
}

int ItlIwn::
iwn_dma_contig_alloc(bus_dma_tag_t tag, struct iwn_dma_info *dma,
                     void** kvap, bus_size_t size, bus_size_t alignment)
{
    if (!allocDmaMemory2(dma, size, alignment)) {
        return 1;
    }
    
    if (kvap != NULL)
        *kvap = dma->vaddr;

    return 0;
}

void ItlIwn::
iwn_dma_contig_free(struct iwn_dma_info *dma)
{
    if (dma == NULL || dma->cmd == NULL)
        return;
    if (dma->vaddr == NULL)
        return;
    dma->cmd->clearMemoryDescriptor();
    dma->cmd->release();
    dma->cmd = NULL;
    dma->buffer->complete();
    dma->buffer->release();
    dma->buffer = NULL;
    dma->vaddr = NULL;
}

int ItlIwn::
iwn_alloc_sched(struct iwn_softc *sc)
{
    /* TX scheduler rings must be aligned on a 1KB boundary. */
    return iwn_dma_contig_alloc(sc->sc_dmat, &sc->sched_dma,
        (void **)&sc->sched, sc->schedsz, 1024);
}

void ItlIwn::
iwn_free_sched(struct iwn_softc *sc)
{
    iwn_dma_contig_free(&sc->sched_dma);
}

int ItlIwn::
iwn_alloc_kw(struct iwn_softc *sc)
{
    /* "Keep Warm" page must be aligned on a 4KB boundary. */
    return iwn_dma_contig_alloc(sc->sc_dmat, &sc->kw_dma, NULL, 4096,
        4096);
}

void ItlIwn::
iwn_free_kw(struct iwn_softc *sc)
{
    iwn_dma_contig_free(&sc->kw_dma);
}

int ItlIwn::
iwn_alloc_ict(struct iwn_softc *sc)
{
    /* ICT table must be aligned on a 4KB boundary. */
    return iwn_dma_contig_alloc(sc->sc_dmat, &sc->ict_dma,
        (void **)&sc->ict, IWN_ICT_SIZE, 4096);
}

void ItlIwn::
iwn_free_ict(struct iwn_softc *sc)
{
    iwn_dma_contig_free(&sc->ict_dma);
}

int ItlIwn::
iwn_alloc_fwmem(struct iwn_softc *sc)
{
    /* Must be aligned on a 16-byte boundary. */
    return iwn_dma_contig_alloc(sc->sc_dmat, &sc->fw_dma, NULL,
        sc->fwsz, 16);
}

void ItlIwn::
iwn_free_fwmem(struct iwn_softc *sc)
{
    iwn_dma_contig_free(&sc->fw_dma);
}

int ItlIwn::
iwn_alloc_rx_ring(struct iwn_softc *sc, struct iwn_rx_ring *ring)
{
    bus_size_t size;
    int i, error;
    mbuf_t m;

    ring->cur = 0;

    /* Allocate RX descriptors (256-byte aligned). */
    size = IWN_RX_RING_COUNT * sizeof (uint32_t);
    error = iwn_dma_contig_alloc(sc->sc_dmat, &ring->desc_dma,
        (void **)&ring->desc, size, 256);
    if (error != 0) {
        XYLog("%s: could not allocate RX ring DMA memory\n",
            sc->sc_dev.dv_xname);
        goto fail;
    }

    /* Allocate RX status area (16-byte aligned). */
    error = iwn_dma_contig_alloc(sc->sc_dmat, &ring->stat_dma,
        (void **)&ring->stat, sizeof (struct iwn_rx_status), 16);
    if (error != 0) {
        XYLog("%s: could not allocate RX status DMA memory\n",
            sc->sc_dev.dv_xname);
        goto fail;
    }

    /*
     * Allocate and map RX buffers.
     */
    for (i = 0; i < IWN_RX_RING_COUNT; i++) {
        struct iwn_rx_data *data = &ring->data[i];

        error = bus_dmamap_create(sc->sc_dmat, IWN_RBUF_SIZE, 1,
            IWN_RBUF_SIZE, 0, BUS_DMA_NOWAIT,
            &data->map);
        if (error != 0) {
            XYLog("%s: could not create RX buf DMA map\n",
                sc->sc_dev.dv_xname);
            goto fail;
        }

        m = getController()->allocatePacket(IWN_RBUF_SIZE);
        if (m == NULL) {
            XYLog("could not allocate RX mbuf\n");
            error = ENOBUFS;
            goto fail;
        }
        data->map->dm_nsegs = data->map->cursor->getPhysicalSegments(m, &data->map->dm_segs[0], 1);
        if (data->map->dm_nsegs == 0) {
            mbuf_freem(m);
            error = ENOMEM;
            goto fail;
        }
        
        data->m = m;
        
//        data->m = MCLGETI(NULL, M_DONTWAIT, NULL, IWN_RBUF_SIZE);
//        if (data->m == NULL) {
//            XYLog("%s: could not allocate RX mbuf\n",
//                sc->sc_dev.dv_xname);
//            error = ENOBUFS;
//            goto fail;
//        }
//
//        error = bus_dmamap_load(sc->sc_dmat, data->map,
//            mtod(data->m, void *), IWN_RBUF_SIZE, NULL,
//            BUS_DMA_NOWAIT | BUS_DMA_READ);
//        if (error != 0) {
//            XYLog("%s: can't map mbuf (error %d)\n",
//                sc->sc_dev.dv_xname, error);
//            goto fail;
//        }

        /* Set physical address of RX buffer (256-byte aligned). */
        ring->desc[i] = htole32(data->map->dm_segs[0].location >> 8);
    }

//    bus_dmamap_sync(sc->sc_dmat, ring->desc_dma.map, 0, size,
//        BUS_DMASYNC_PREWRITE);

    return 0;

fail:    iwn_free_rx_ring(sc, ring);
    return error;
}

void ItlIwn::
iwn_reset_rx_ring(struct iwn_softc *sc, struct iwn_rx_ring *ring)
{
    int ntries;

    if (iwn_nic_lock(sc) == 0) {
        IWN_WRITE(sc, IWN_FH_RX_CONFIG, 0);
        for (ntries = 0; ntries < 1000; ntries++) {
            if (IWN_READ(sc, IWN_FH_RX_STATUS) &
                IWN_FH_RX_STATUS_IDLE)
                break;
            DELAY(10);
        }
        iwn_nic_unlock(sc);
    }
    ring->cur = 0;
    sc->last_rx_valid = 0;
}

void ItlIwn::
iwn_free_rx_ring(struct iwn_softc *sc, struct iwn_rx_ring *ring)
{
    int i;

    iwn_dma_contig_free(&ring->desc_dma);
    iwn_dma_contig_free(&ring->stat_dma);

    for (i = 0; i < IWN_RX_RING_COUNT; i++) {
        struct iwn_rx_data *data = &ring->data[i];

        if (data->m != NULL) {
//            bus_dmamap_sync(sc->sc_dmat, data->map, 0,
//                data->map->dm_mapsize, BUS_DMASYNC_POSTREAD);
//            bus_dmamap_unload(sc->sc_dmat, data->map);
            mbuf_freem(data->m);
            data->m = NULL;
        }
        if (data->map != NULL) {
            bus_dmamap_destroy(sc->sc_dmat, data->map);
            data->map = NULL;
        }
    }
}

int ItlIwn::
iwn_alloc_tx_ring(struct iwn_softc *sc, struct iwn_tx_ring *ring, int qid)
{
    bus_addr_t paddr;
    bus_size_t size;
    int i, error;
    const bool apAggregateQueue =
        qid >= sc->first_agg_txq && qid < sc->ntxqs;

    ring->qid = qid;
    ring->queued = 0;
    ring->cur = 0;
    ring->read = 0;
    ring->first_tb = NULL;
    ring->ap_payload = NULL;

    /* Allocate TX descriptors (256-byte aligned). */
    size = IWN_TX_RING_COUNT * sizeof (struct iwn_tx_desc);
    error = iwn_dma_contig_alloc(sc->sc_dmat, &ring->desc_dma,
        (void **)&ring->desc, size, 256);
    if (error != 0) {
        XYLog("%s: could not allocate TX ring DMA memory\n",
            sc->sc_dev.dv_xname);
        goto fail;
    }

    size = IWN_TX_RING_COUNT * sizeof (struct iwn_tx_cmd);
    error = iwn_dma_contig_alloc(sc->sc_dmat, &ring->cmd_dma,
        (void **)&ring->cmd, size, 4);
    if (error != 0) {
        XYLog("%s: could not allocate TX cmd DMA memory\n",
            sc->sc_dev.dv_xname);
        goto fail;
    }

    /*
     * Gen1 data transport keeps the bidirectional first 20 command bytes in
     * a physically separate low-DMA pool.  The conventional itlwm queues
     * coalesce that prefix with the rest of the command, but PAN q7 needs
     * the original transport shape.  Dynamic DVM uses every hardware queue
     * at or above first_agg_txq (q7..q15 on 4965, q10..q19 on 5000) as its
     * hardware resource pool.  PAN reserves q10 as AUX, so its RA/TID
     * allocator starts at q11; provision the complete range while keeping
     * that allocation boundary explicit.
     */
    if (qid == IWN_IPAN_MGMT_QUEUE || qid == IWN_IPAN_BE_QUEUE ||
        qid == IWN_IPAN_MCAST_QUEUE ||
        apAggregateQueue) {
        size = IWN_TX_RING_COUNT * IWN_TX_FIRST_TB_STRIDE;
        error = iwn_dma_contig_alloc(sc->sc_dmat, &ring->first_tb_dma,
            (void **)&ring->first_tb, size, IWN_TX_FIRST_TB_STRIDE);
        if (error != 0) {
            XYLog("%s: could not allocate PAN first-TB DMA memory\n",
                sc->sc_dev.dv_xname);
            goto fail;
        }
        const bus_size_t payloadStride =
            (qid == IWN_IPAN_BE_QUEUE || qid == IWN_IPAN_MCAST_QUEUE ||
             apAggregateQueue) ?
                IWN_AP_DATA_PAYLOAD_SIZE : IWN_AP_MGMT_PAYLOAD_SIZE;
        size = IWN_TX_RING_COUNT * payloadStride;
        error = iwn_dma_contig_alloc(sc->sc_dmat, &ring->ap_payload_dma,
            (void **)&ring->ap_payload, size, 64);
        if (error != 0) {
            XYLog("%s: could not allocate PAN payload DMA memory\n",
                sc->sc_dev.dv_xname);
            goto fail;
        }
    }

    paddr = ring->cmd_dma.paddr;
    for (i = 0; i < IWN_TX_RING_COUNT; i++) {
        struct iwn_tx_data *data = &ring->data[i];

        data->cmd_paddr = paddr;
        data->scratch_paddr = paddr + 12;
        data->ampdu_rate_generation = 0;
        data->ampdu_rate_rflags = 0;
        data->ampdu_rate_feedback_valid = 0;
        data->tx_apple_nrate = 0;
        data->tx_apple_nrate_valid = 0;
        data->post_plti_trace_class = IWN_POST_PLTI_TRACE_TX_NONE;
        data->ap_mgmt = false;
        data->ap_data = false;
        iwn_sae_tx_data_clear(data);
        paddr += sizeof (struct iwn_tx_cmd);

        error = bus_dmamap_create(sc->sc_dmat, MCLBYTES,
            IWN_MAX_SCATTER - 1, MCLBYTES, 0, BUS_DMA_NOWAIT,
            &data->map);
        if (error != 0) {
            XYLog("%s: could not create TX buf DMA map\n",
                sc->sc_dev.dv_xname);
            goto fail;
        }
    }
    return 0;

fail:    iwn_free_tx_ring(sc, ring);
    return error;
}

void ItlIwn::
iwn_reset_tx_ring(struct iwn_softc *sc, struct iwn_tx_ring *ring)
{
    ItlIwn *that = container_of(sc, ItlIwn, com);
    int i;

    for (i = 0; i < IWN_TX_RING_COUNT; i++) {
        struct iwn_tx_data *data = &ring->data[i];

        /* Reset has no native TX_DONE; retire an accepted SAE descriptor. */
        if (data->sae_active) {
            that->iwn_sae_tx_report_terminal(sc, data, EIO);
            if (data->ni != NULL) {
                ieee80211_release_node(&sc->sc_ic, data->ni);
                data->ni = NULL;
            }
        }

        if (data->m != NULL) {
//            bus_dmamap_sync(sc->sc_dmat, data->map, 0,
//                data->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
//            bus_dmamap_unload(sc->sc_dmat, data->map);
            mbuf_freem(data->m);
            data->m = NULL;
        }
        data->ampdu_rate_generation = 0;
        data->ampdu_rate_rflags = 0;
        data->ampdu_rate_feedback_valid = 0;
        data->tx_apple_nrate = 0;
        data->tx_apple_nrate_valid = 0;
        data->post_plti_trace_class = IWN_POST_PLTI_TRACE_TX_NONE;
        data->ap_mgmt = false;
        data->ap_data = false;
        iwn_sae_tx_data_clear(data);
    }
    /* Clear TX descriptors. */
    memset(ring->desc, 0, ring->desc_dma.size);
    if (ring->first_tb != NULL)
        memset(ring->first_tb, 0, ring->first_tb_dma.size);
    if (ring->ap_payload != NULL)
        memset(ring->ap_payload, 0, ring->ap_payload_dma.size);
//    bus_dmamap_sync(sc->sc_dmat, ring->desc_dma.map, 0,
//        ring->desc_dma.size, BUS_DMASYNC_PREWRITE);
    sc->qfullmsk &= ~(1 << ring->qid);
    ring->queued = 0;
    ring->cur = 0;
    ring->read = 0;
}

void ItlIwn::
iwn_free_tx_ring(struct iwn_softc *sc, struct iwn_tx_ring *ring)
{
    ItlIwn *that = container_of(sc, ItlIwn, com);
    int i;

    iwn_dma_contig_free(&ring->desc_dma);
    iwn_dma_contig_free(&ring->cmd_dma);
    iwn_dma_contig_free(&ring->first_tb_dma);
    iwn_dma_contig_free(&ring->ap_payload_dma);
    ring->first_tb = NULL;
    ring->ap_payload = NULL;

    for (i = 0; i < IWN_TX_RING_COUNT; i++) {
        struct iwn_tx_data *data = &ring->data[i];

        /* Detach/unwind may reclaim without a firmware completion. */
        if (data->sae_active) {
            that->iwn_sae_tx_report_terminal(sc, data, EIO);
            if (data->ni != NULL) {
                ieee80211_release_node(&sc->sc_ic, data->ni);
                data->ni = NULL;
            }
        }

        if (data->m != NULL) {
//            bus_dmamap_sync(sc->sc_dmat, data->map, 0,
//                data->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
//            bus_dmamap_unload(sc->sc_dmat, data->map);
            mbuf_freem(data->m);
            data->m = NULL;
        }
        data->ampdu_rate_generation = 0;
        data->ampdu_rate_rflags = 0;
        data->ampdu_rate_feedback_valid = 0;
        data->tx_apple_nrate = 0;
        data->tx_apple_nrate_valid = 0;
        data->post_plti_trace_class = IWN_POST_PLTI_TRACE_TX_NONE;
        data->ap_mgmt = false;
        data->ap_data = false;
        iwn_sae_tx_data_clear(data);
        if (data->map != NULL) {
            bus_dmamap_destroy(sc->sc_dmat, data->map);
            data->map = NULL;
        }
    }
}

void ItlIwn::
iwn5000_ict_reset(struct iwn_softc *sc)
{
    /* Disable interrupts. */
    IWN_WRITE(sc, IWN_INT_MASK, 0);

    /* Reset ICT table. */
    memset(sc->ict, 0, IWN_ICT_SIZE);
    sc->ict_cur = 0;

    /* Set physical address of ICT table (4KB aligned). */
    IWN_WRITE(sc, IWN_DRAM_INT_TBL, IWN_DRAM_INT_TBL_ENABLE |
        IWN_DRAM_INT_TBL_WRAP_CHECK | sc->ict_dma.paddr >> 12);

    /* Enable periodic RX interrupt. */
    sc->int_mask |= IWN_INT_RX_PERIODIC;
    /* Switch to ICT interrupt mode in driver. */
    sc->sc_flags |= IWN_FLAG_USE_ICT;

    /* Re-enable interrupts. */
    IWN_WRITE(sc, IWN_INT, 0xffffffff);
    IWN_WRITE(sc, IWN_INT_MASK, sc->int_mask);
}

int ItlIwn::
iwn_read_eeprom(struct iwn_softc *sc)
{
    struct iwn_ops *ops = &sc->ops;
    struct ieee80211com *ic = &sc->sc_ic;
    uint16_t val;
    int error;

    /* Check whether adapter has an EEPROM or an OTPROM. */
    if (sc->hw_type >= IWN_HW_REV_TYPE_1000 &&
        (IWN_READ(sc, IWN_OTP_GP) & IWN_OTP_GP_DEV_SEL_OTP))
        sc->sc_flags |= IWN_FLAG_HAS_OTPROM;

    /* Adapter has to be powered on for EEPROM access to work. */
    if ((error = iwn_apm_init(sc)) != 0) {
        XYLog("%s: could not power ON adapter\n",
            sc->sc_dev.dv_xname);
        return error;
    }

    if ((IWN_READ(sc, IWN_EEPROM_GP) & 0x7) == 0) {
        XYLog("%s: bad ROM signature\n", sc->sc_dev.dv_xname);
        return EIO;
    }
    if ((error = iwn_eeprom_lock(sc)) != 0) {
        XYLog("%s: could not lock ROM (error=%d)\n",
            sc->sc_dev.dv_xname, error);
        return error;
    }
    if (sc->sc_flags & IWN_FLAG_HAS_OTPROM) {
        if ((error = iwn_init_otprom(sc)) != 0) {
            XYLog("%s: could not initialize OTPROM\n",
                sc->sc_dev.dv_xname);
            return error;
        }
    }

    iwn_read_prom_data(sc, IWN_EEPROM_SKU_CAP, &val, 2);
    /* Check if HT support is bonded out. */
    if (val & htole16(IWN_EEPROM_SKU_CAP_11N))
        sc->sc_flags |= IWN_FLAG_HAS_11N;
    sc->eeprom_pan_capable =
        (val & htole16(IWN_EEPROM_SKU_CAP_IPAN)) != 0;

    iwn_read_prom_data(sc, IWN_EEPROM_RFCFG, &val, 2);
    sc->rfcfg = letoh16(val);
    /* Read Tx/Rx chains from ROM unless it's known to be broken. */
    if (sc->txchainmask == 0)
        sc->txchainmask = IWN_RFCFG_TXANTMSK(sc->rfcfg);
    if (sc->rxchainmask == 0)
        sc->rxchainmask = IWN_RFCFG_RXANTMSK(sc->rfcfg);

    /* Read MAC address. */
    iwn_read_prom_data(sc, IWN_EEPROM_MAC, ic->ic_myaddr, 6);

    /* Read adapter-specific information from EEPROM. */
    ops->read_eeprom(sc);

    iwn_apm_stop(sc);    /* Power OFF adapter. */

    iwn_eeprom_unlock(sc);
    return 0;
}

void ItlIwn::
iwn4965_read_eeprom(struct iwn_softc *sc)
{
    ItlIwn *that = container_of(sc, ItlIwn, com);
    
    uint32_t addr;
    uint16_t val;
    int i;

    /* Read regulatory domain (4 ASCII characters). */
    that->iwn_read_prom_data(sc, IWN4965_EEPROM_DOMAIN, sc->eeprom_domain, 4);

    /* Read the list of authorized channels. */
    for (i = 0; i < 7; i++) {
        addr = iwn4965_regulatory_bands[i];
        that->iwn_read_eeprom_channels(sc, i, addr);
    }

    /* Read maximum allowed TX power for 2GHz and 5GHz bands. */
    that->iwn_read_prom_data(sc, IWN4965_EEPROM_MAXPOW, &val, 2);
    sc->maxpwr2GHz = val & 0xff;
    sc->maxpwr5GHz = val >> 8;
    /* Check that EEPROM values are within valid range. */
    if (sc->maxpwr5GHz < 20 || sc->maxpwr5GHz > 50)
        sc->maxpwr5GHz = 38;
    if (sc->maxpwr2GHz < 20 || sc->maxpwr2GHz > 50)
        sc->maxpwr2GHz = 38;

    /* Read samples for each TX power group. */
    that->iwn_read_prom_data(sc, IWN4965_EEPROM_BANDS, sc->bands,
        sizeof sc->bands);

    /* Read voltage at which samples were taken. */
    that->iwn_read_prom_data(sc, IWN4965_EEPROM_VOLTAGE, &val, 2);
    sc->eeprom_voltage = (int16_t)letoh16(val);

}

void ItlIwn::
iwn5000_read_eeprom(struct iwn_softc *sc)
{
    ItlIwn *that = container_of(sc, ItlIwn, com);
    struct iwn5000_eeprom_calib_hdr hdr;
    int32_t volt;
    uint32_t base, addr;
    uint16_t val;
    int i;

    /* Read regulatory domain (4 ASCII characters). */
    that->iwn_read_prom_data(sc, IWN5000_EEPROM_REG, &val, 2);
    base = letoh16(val);
    that->iwn_read_prom_data(sc, base + IWN5000_EEPROM_DOMAIN,
        sc->eeprom_domain, 4);

    /* Read the list of authorized channels. */
    for (i = 0; i < 7; i++) {
        addr = base + iwn5000_regulatory_bands[i];
        that->iwn_read_eeprom_channels(sc, i, addr);
    }

    /* Read enhanced TX power information for 6000 Series. */
    if (sc->hw_type >= IWN_HW_REV_TYPE_6000)
        that->iwn_read_eeprom_enhinfo(sc);

    that->iwn_read_prom_data(sc, IWN5000_EEPROM_CAL, &val, 2);
    base = letoh16(val);
    that->iwn_read_prom_data(sc, base, &hdr, sizeof hdr);
    sc->calib_ver = hdr.version;

    if (sc->hw_type == IWN_HW_REV_TYPE_2030 ||
        sc->hw_type == IWN_HW_REV_TYPE_2000 ||
        sc->hw_type == IWN_HW_REV_TYPE_135 ||
        sc->hw_type == IWN_HW_REV_TYPE_105) {
        sc->eeprom_voltage = letoh16(hdr.volt);
        that->iwn_read_prom_data(sc, base + IWN5000_EEPROM_TEMP, &val, 2);
        sc->eeprom_temp = letoh16(val);
        that->iwn_read_prom_data(sc, base + IWN2000_EEPROM_RAWTEMP, &val, 2);
        sc->eeprom_rawtemp = letoh16(val);
    }

    if (sc->hw_type == IWN_HW_REV_TYPE_5150) {
        /* Compute temperature offset. */
        that->iwn_read_prom_data(sc, base + IWN5000_EEPROM_TEMP, &val, 2);
        sc->eeprom_temp = letoh16(val);
        that->iwn_read_prom_data(sc, base + IWN5000_EEPROM_VOLT, &val, 2);
        volt = letoh16(val);
        sc->temp_off = sc->eeprom_temp - (volt / -5);
    } else {
        /* Read crystal calibration. */
        that->iwn_read_prom_data(sc, base + IWN5000_EEPROM_CRYSTAL,
            &sc->eeprom_crystal, sizeof (uint32_t));
    }
}

void ItlIwn::
iwn_read_eeprom_channels(struct iwn_softc *sc, int n, uint32_t addr)
{
    struct ieee80211com *ic = &sc->sc_ic;
    const struct iwn_chan_band *band = &iwn_bands[n];
    struct iwn_eeprom_chan channels[IWN_MAX_CHAN_PER_BAND];
    uint8_t chan;
    unsigned int upper;
    int i;

    iwn_read_prom_data(sc, addr, channels,
        band->nchan * sizeof (struct iwn_eeprom_chan));

    for (i = 0; i < band->nchan; i++) {
        if (!(channels[i].flags & IWN_EEPROM_CHAN_VALID))
            continue;

        chan = band->chan[i];

        if (n == 0) {    /* 2GHz band */
            ic->ic_channels[chan].ic_freq =
                ieee80211_ieee2mhz(chan, IEEE80211_CHAN_2GHZ);
            ic->ic_channels[chan].ic_flags =
                IEEE80211_CHAN_CCK | IEEE80211_CHAN_OFDM |
                IEEE80211_CHAN_DYN | IEEE80211_CHAN_2GHZ;

        } else if (n < 5) {    /* 5GHz band */
            /*
             * Some adapters support channels 7, 8, 11 and 12
             * both in the 2GHz and 4.9GHz bands.
             * Because of limitations in our net80211 layer,
             * we don't support them in the 4.9GHz band.
             */
            if (chan <= 14)
                continue;

            ic->ic_channels[chan].ic_freq =
                ieee80211_ieee2mhz(chan, IEEE80211_CHAN_5GHZ);
            ic->ic_channels[chan].ic_flags = IEEE80211_CHAN_A;
            /* We have at least one valid 5GHz channel. */
            sc->sc_flags |= IWN_FLAG_HAS_5GHZ;
        } else  { /* 40 MHz */
            upper = (unsigned int)chan +
                IwnHt40Contracts::kPrimarySecondaryChannelDelta;
            if (!(sc->sc_flags & IWN_FLAG_HAS_11N) ||
                !IwnHt40Contracts::isPrimaryWithSecondaryAbove(chan, upper) ||
                chan >= nitems(sc->maxpwr40) ||
                upper >= nitems(ic->ic_channels) ||
                upper >= nitems(sc->maxpwr40) ||
                ic->ic_channels[chan].ic_freq == 0 ||
                ic->ic_channels[upper].ic_freq == 0)
                continue;
            sc->maxpwr40[chan] = channels[i].maxpwr;
            ic->ic_channels[chan].ic_flags |= IEEE80211_CHAN_HT40U;
            ic->ic_channels[upper].ic_flags |= IEEE80211_CHAN_HT40D;
        }

        /* Is active scan allowed on this channel? */
        if (n < 5) {
            /* Is active scan allowed on this channel? */
            if (!(channels[i].flags & IWN_EEPROM_CHAN_ACTIVE)) {
                ic->ic_channels[chan].ic_flags |=
                IEEE80211_CHAN_PASSIVE;
            }

            /* Save maximum allowed TX power for this channel. */
            sc->maxpwr[chan] = channels[i].maxpwr;

            if (sc->sc_flags & IWN_FLAG_HAS_11N)
                ic->ic_channels[chan].ic_flags |=
                IEEE80211_CHAN_HT20;
        }

    }
}

void ItlIwn::
iwn_read_eeprom_enhinfo(struct iwn_softc *sc)
{
    struct iwn_eeprom_enhinfo enhinfo[35];
    uint16_t val, base;
    int8_t maxpwr;
    int i;

    iwn_read_prom_data(sc, IWN5000_EEPROM_REG, &val, 2);
    base = letoh16(val);
    iwn_read_prom_data(sc, base + IWN6000_EEPROM_ENHINFO,
        enhinfo, sizeof enhinfo);

    memset(sc->enh_maxpwr, 0, sizeof sc->enh_maxpwr);
    for (i = 0; i < nitems(enhinfo); i++) {
        if ((enhinfo[i].flags & IWN_TXP_VALID) == 0)
            continue;    /* Skip invalid entries. */

        maxpwr = 0;
        if (sc->txchainmask & IWN_ANT_A)
            maxpwr = MAX(maxpwr, enhinfo[i].chain[0]);
        if (sc->txchainmask & IWN_ANT_B)
            maxpwr = MAX(maxpwr, enhinfo[i].chain[1]);
        if (sc->txchainmask & IWN_ANT_C)
            maxpwr = MAX(maxpwr, enhinfo[i].chain[2]);
        if (sc->ntxchains == 2)
            maxpwr = MAX(maxpwr, enhinfo[i].mimo2);
        else if (sc->ntxchains == 3)
            maxpwr = MAX(maxpwr, enhinfo[i].mimo3);
        maxpwr /= 2;    /* Convert half-dBm to dBm. */

        sc->enh_maxpwr[i] = maxpwr;
    }
}

struct ieee80211_node * ItlIwn::
iwn_node_alloc(struct ieee80211com *ic)
{
    return (ieee80211_node *)malloc(sizeof (struct iwn_node), M_DEVBUF, M_NOWAIT | M_ZERO);
}

void ItlIwn::
iwn_newassoc(struct ieee80211com *ic, struct ieee80211_node *ni, int isnew)
{
    struct iwn_softc *sc = (struct iwn_softc *)ic->ic_if.if_softc;
    struct iwn_node *wn = (struct iwn_node *)ni;
    uint8_t rate;
    int ridx, i;

    if ((ni->ni_flags & IEEE80211_NODE_HT) == 0)
        ieee80211_amrr_node_init(&sc->amrr, &wn->amn);

    iwn_clear_apple_nrate_cache(sc);

    /* Start at lowest available bit-rate, AMRR/MiRA will raise. */
    ni->ni_txrate = 0;
    ni->ni_txmcs = 0;

    for (i = 0; i < ni->ni_rates.rs_nrates; i++) {
        rate = ni->ni_rates.rs_rates[i] & IEEE80211_RATE_VAL;
        /* Map 802.11 rate to HW rate index. */
        for (ridx = 0; ridx <= IWN_RIDX_MAX; ridx++) {
            if (iwn_rates[ridx].plcp != IWN_RATE_INVM_PLCP &&
                iwn_rates[ridx].rate == rate)
                break;
        }
        wn->ridx[i] = ridx;
    }
}

int ItlIwn::
iwn_media_change(struct _ifnet *ifp)
{
    struct iwn_softc *sc = (struct iwn_softc *)ifp->if_softc;
    struct ieee80211com *ic = &sc->sc_ic;
    uint8_t rate, ridx;
    int error;

    error = ieee80211_media_change(ifp);
    if (error != ENETRESET)
        return error;

    if (ic->ic_fixed_mcs != -1)
        sc->fixed_ridx = iwn_mcs2ridx[ic->ic_fixed_mcs];
    if (ic->ic_fixed_rate != -1) {
        rate = ic->ic_sup_rates[ic->ic_curmode].
            rs_rates[ic->ic_fixed_rate] & IEEE80211_RATE_VAL;
        /* Map 802.11 rate to HW rate index. */
        for (ridx = 0; ridx <= IWN_RIDX_MAX; ridx++)
            if (iwn_rates[ridx].plcp != IWN_RATE_INVM_PLCP &&
                iwn_rates[ridx].rate == rate)
                break;
        sc->fixed_ridx = ridx;
    }

    if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
        (IFF_UP | IFF_RUNNING)) {
        iwn_stop(ifp);
        error = iwn_init(ifp);
    }
    return error;
}

/* IWN STOP_SCAN carries no firmware scan identifier.  Every physical scan
 * therefore takes this one host-side lease before its command doorbell and
 * keeps it until terminal net80211 cleanup is complete. */
struct iwn_scan_lease_terminal {
    bool valid;
    enum iwn_scan_lease_owner owner;
    bool wcl;
    bool wcl_foreground;
    bool publish_wcl_terminal;
    bool standard;
    bool publish_standard_terminal;
    bool aborted;
    u_int64_t serial;
    u_int64_t upper_generation;
    u_int64_t join_generation;
    u_int64_t reassoc_serial;
    u_int32_t backend_generation;
};

/* The command ring does not attach a scan identifier to STOP_SCAN.  This
 * context therefore keeps the lease lock held from the last ownership check
 * through the actual command-ring doorbell.  iwn_cmd_with_doorbell_hook()
 * invokes the post hook immediately after IWN_WRITE(), before an IRQ handler
 * can classify the command as an exact scan owner. */
struct iwn_scan_doorbell_context {
    u_int64_t serial;
    u_int64_t upper_generation;
    u_int64_t initial_handoff_serial;
    u_int32_t backend_generation;
    IOSimpleLock *lock;
    bool background;
    bool publish_wcl_initial_started;
    bool lock_held;
    bool committed;
};

static void
iwn_scan_lease_clear_locked(struct iwn_softc *sc)
{
    explicit_bzero(&sc->sc_scan_lease, sizeof(sc->sc_scan_lease));
    sc->sc_scan_lease.phase = IWN_SCAN_LEASE_IDLE;
}

static bool
iwn_scan_lease_live_locked(const struct iwn_softc *sc)
{
    return sc->sc_scan_lease.owner != IWN_SCAN_LEASE_NONE &&
        sc->sc_scan_lease.phase != IWN_SCAN_LEASE_IDLE;
}

/* The reference firmware join is one atomic radio owner: an unrelated scan
 * cannot replace the selected BSS between SAE Commit and the protected
 * 4-way terminal.  DVM exposes those stages to the host, so retain an exact
 * generation under the existing physical-scan leaf.  Promotion either
 * consumes the pre-secret mailbox reservation or validates an already
 * transferred direct-scan owner; an unreserved cached roam may acquire the
 * idle leaf only when no physical command is live. */
static bool
iwn_sae_bss_loss_join_handoff_arm(struct iwn_softc *sc,
    u_int64_t request_generation)
{
    bool armed = false;

    if (sc == NULL || request_generation == 0 ||
        sc->sc_scan_lease_lock == NULL)
        return false;
    IOSimpleLockLock(sc->sc_scan_lease_lock);
    /* The fallback is called only from the completed generic census.  Keep
     * this proof narrower than WCL roam: it cannot borrow a background,
     * aborted, invalidated, still-on-air, or AP-transition scan. */
    if (sc->sc_sae_bss_loss_join_handoff_generation == 0 &&
        sc->sc_sae_join_scan_block_generation == 0 &&
        iwn_scan_lease_live_locked(sc) &&
        sc->sc_scan_lease.owner == IWN_SCAN_LEASE_GENERIC_FOREGROUND &&
        sc->sc_scan_lease.phase == IWN_SCAN_LEASE_DRAINING &&
        sc->sc_scan_lease.command_submitted &&
        sc->sc_scan_lease.terminal_claimed &&
        !sc->sc_scan_lease.abort_requested &&
        !sc->sc_scan_lease.hardware_invalidated &&
        !sc->sc_scan_lease.publication_invalidated &&
        (sc->sc_flags & IWN_FLAG_SCANNING) == 0 &&
        !sc->sc_ap_transition_scan_blocked &&
        !sc->sc_wcl_initial_scan_pending.queued) {
        sc->sc_sae_bss_loss_join_handoff_generation = request_generation;
        armed = true;
    }
    IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    return armed;
}

static bool
iwn_sae_bss_loss_join_handoff_completed(struct iwn_softc *sc,
    u_int64_t request_generation)
{
    bool completed = false;

    if (sc == NULL || request_generation == 0 ||
        sc->sc_scan_lease_lock == NULL)
        return false;
    IOSimpleLockLock(sc->sc_scan_lease_lock);
    completed =
        sc->sc_sae_bss_loss_join_handoff_generation == 0 &&
        sc->sc_sae_join_scan_block_generation == request_generation;
    IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    return completed;
}

static bool
iwn_sae_join_scan_block_promote(struct iwn_softc *sc,
    u_int64_t request_generation)
{
    struct ieee80211com *ic;
    bool completing_wcl_roam = false;
    bool completing_bss_loss = false;
    bool promoted = false;

    if (sc == NULL || request_generation == 0 ||
        sc->sc_scan_lease_lock == NULL)
        return false;
    ic = &sc->sc_ic;
    IOSimpleLockLock(sc->sc_scan_lease_lock);
    /* STOP_SCAN deliberately keeps its lease in DRAINING while
     * ieee80211_end_scan() selects and starts the target.  A pure-SAE WCL
     * roam reaches auth_hold() inside that exact callback, before
     * finish_terminal() can make the scan leaf idle.  Transfer continuity
     * from that one completed WCL command to the selected SAE generation;
     * every other live scan remains a conflict. */
    completing_wcl_roam =
        sc->sc_sae_join_scan_block_generation == 0 &&
        iwn_scan_lease_live_locked(sc) &&
        (sc->sc_scan_lease.owner == IWN_SCAN_LEASE_GENERIC_BACKGROUND ||
         sc->sc_scan_lease.owner == IWN_SCAN_LEASE_WCL_BACKGROUND) &&
        sc->sc_scan_lease.phase == IWN_SCAN_LEASE_DRAINING &&
        sc->sc_scan_lease.command_submitted &&
        sc->sc_scan_lease.terminal_claimed &&
        !sc->sc_scan_lease.abort_requested &&
        !sc->sc_scan_lease.hardware_invalidated &&
        !sc->sc_scan_lease.publication_invalidated &&
        (sc->sc_flags & IWN_FLAG_SCANNING) == 0 &&
        !sc->sc_ap_transition_scan_blocked &&
        !sc->sc_wcl_initial_scan_pending.queued &&
        ic->ic_wcl_reassoc_owner_active &&
        sc->sc_scan_lease.reassoc_serial != 0 &&
        sc->sc_scan_lease.reassoc_serial == ic->ic_wcl_reassoc_owner_serial &&
        ic->ic_wcl_reassoc_owner_last_leaf ==
            IEEE80211_WCL_REASSOC_OWNER_LEAF_ROAM_STARTED;
    completing_bss_loss =
        sc->sc_sae_join_scan_block_generation == 0 &&
        sc->sc_sae_bss_loss_join_handoff_generation ==
            request_generation &&
        iwn_scan_lease_live_locked(sc) &&
        sc->sc_scan_lease.owner == IWN_SCAN_LEASE_GENERIC_FOREGROUND &&
        sc->sc_scan_lease.phase == IWN_SCAN_LEASE_DRAINING &&
        sc->sc_scan_lease.command_submitted &&
        sc->sc_scan_lease.terminal_claimed &&
        !sc->sc_scan_lease.abort_requested &&
        !sc->sc_scan_lease.hardware_invalidated &&
        !sc->sc_scan_lease.publication_invalidated &&
        (sc->sc_flags & IWN_FLAG_SCANNING) == 0 &&
        !sc->sc_ap_transition_scan_blocked &&
        !sc->sc_wcl_initial_scan_pending.queued;
    if (sc->sc_sae_join_scan_block_generation == request_generation) {
        if (sc->sc_sae_bss_loss_join_handoff_generation ==
            request_generation)
            sc->sc_sae_bss_loss_join_handoff_generation = 0;
        promoted = true;
    } else if (sc->sc_sae_join_scan_block_generation == 0 &&
        ((!iwn_scan_lease_live_locked(sc) || completing_wcl_roam) ||
         completing_bss_loss) &&
        (sc->sc_flags & IWN_FLAG_SCANNING) == 0 &&
        !sc->sc_ap_transition_scan_blocked &&
        !sc->sc_wcl_initial_scan_pending.queued) {
        sc->sc_sae_wcl_admission_reserved = false;
        sc->sc_sae_wcl_admission_requires_fresh_scan = false;
        if (completing_bss_loss)
            sc->sc_sae_bss_loss_join_handoff_generation = 0;
        sc->sc_sae_join_scan_block_generation = request_generation;
        promoted = true;
    }
    IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    return promoted;
}

static void
iwn_sae_join_scan_block_clear_generation(struct iwn_softc *sc,
    u_int64_t request_generation)
{
    if (sc == NULL || request_generation == 0 ||
        sc->sc_scan_lease_lock == NULL)
        return;
    IOSimpleLockLock(sc->sc_scan_lease_lock);
    if (sc->sc_sae_join_scan_block_generation == request_generation)
        sc->sc_sae_join_scan_block_generation = 0;
    if (sc->sc_sae_bss_loss_join_handoff_generation == request_generation)
        sc->sc_sae_bss_loss_join_handoff_generation = 0;
    IOSimpleLockUnlock(sc->sc_scan_lease_lock);
}

static bool
iwn_sae_join_scan_blocked(struct iwn_softc *sc)
{
    bool blocked = false;

    if (sc == NULL || sc->sc_scan_lease_lock == NULL)
        return false;
    IOSimpleLockLock(sc->sc_scan_lease_lock);
    blocked = sc->sc_sae_join_scan_block_generation != 0 ||
        sc->sc_sae_bss_loss_join_handoff_generation != 0;
    IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    return blocked;
}

static bool
iwn_scan_lease_wnm_target_channel(struct iwn_softc *sc,
                                  u_int64_t required_serial,
                                  u_int8_t *target_channel)
{
    bool exact = false;

    if (target_channel != NULL)
        *target_channel = 0;
    if (sc == NULL || target_channel == NULL ||
        sc->sc_scan_lease_lock == NULL)
        return false;
    IOSimpleLockLock(sc->sc_scan_lease_lock);
    if (iwn_scan_lease_live_locked(sc) &&
        (required_serial == 0 ||
         sc->sc_scan_lease.serial == required_serial) &&
        sc->sc_scan_lease.wnm_target_channel != 0) {
        *target_channel = sc->sc_scan_lease.wnm_target_channel;
        exact = true;
    }
    IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    return exact;
}

static bool
iwn_scan_lease_owner_is_wcl(u_int8_t owner)
{
    return owner == IWN_SCAN_LEASE_WCL_BACKGROUND ||
        owner == IWN_SCAN_LEASE_WCL_INITIAL;
}

static bool
iwn_scan_lease_owner_is_wcl_initial(u_int8_t owner)
{
    return owner == IWN_SCAN_LEASE_WCL_INITIAL;
}

static void
iwn_wcl_initial_scan_pending_clear_locked(struct iwn_softc *sc)
{
    if (sc != NULL)
        explicit_bzero(&sc->sc_wcl_initial_scan_pending,
                       sizeof(sc->sc_wcl_initial_scan_pending));
}

/* Queue only behind the exact generic foreground lease which is already
 * servicing initial discovery.  A different live owner must finish through
 * its native lifecycle; WCL never retags or borrows it. */
static bool
iwn_wcl_initial_scan_queue(struct iwn_softc *sc, u_int64_t generation,
                           bool *out_queued)
{
    bool admitted = false;

    if (out_queued != NULL)
        *out_queued = false;
    if (sc == NULL || generation == 0 || out_queued == NULL ||
        sc->sc_scan_lease_lock == NULL)
        return false;

    IOSimpleLockLock(sc->sc_scan_lease_lock);
    if (!sc->sc_wcl_initial_scan_pending.queued) {
        if (iwn_scan_lease_live_locked(sc)) {
            if (sc->sc_scan_lease.owner ==
                    IWN_SCAN_LEASE_GENERIC_FOREGROUND &&
                !sc->sc_scan_lease.hardware_invalidated &&
                !sc->sc_scan_lease.terminal_claimed &&
                !sc->sc_scan_lease.abort_requested &&
                sc->sc_scan_lease.command_submitted &&
                sc->sc_scan_lease.phase == IWN_SCAN_LEASE_ACTIVE) {
                sc->sc_wcl_initial_scan_pending.upper_generation = generation;
                sc->sc_wcl_initial_scan_pending.generic_serial =
                    sc->sc_scan_lease.serial;
                sc->sc_wcl_initial_scan_pending.queued = true;
                *out_queued = true;
                admitted = true;
            }
        } else if ((sc->sc_flags & IWN_FLAG_SCANNING) == 0) {
            /* S_SCAN can briefly be idle after a prior controlled terminal.
             * The caller may reserve a fresh WCL_INITIAL lease directly. */
            admitted = true;
        }
    }
    IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    return admitted;
}

static bool
iwn_wcl_initial_scan_claim_generic_terminal(
    struct iwn_softc *sc, const struct iwn_scan_lease_terminal *terminal)
{
    bool claimed = false;

    if (sc == NULL || terminal == NULL || sc->sc_scan_lease_lock == NULL ||
        terminal->owner != IWN_SCAN_LEASE_GENERIC_FOREGROUND)
        return false;
    IOSimpleLockLock(sc->sc_scan_lease_lock);
    if (sc->sc_wcl_initial_scan_pending.queued &&
        !sc->sc_wcl_initial_scan_pending.launching &&
        sc->sc_wcl_initial_scan_pending.generic_serial == terminal->serial) {
        sc->sc_wcl_initial_scan_pending.terminal_handoff_ready = true;
        claimed = true;
    }
    IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    return claimed;
}

static bool
iwn_wcl_initial_scan_pending_blocks_generic(struct iwn_softc *sc)
{
    bool blocks = false;

    if (sc == NULL || sc->sc_scan_lease_lock == NULL)
        return false;
    IOSimpleLockLock(sc->sc_scan_lease_lock);
    blocks = sc->sc_wcl_initial_scan_pending.queued;
    IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    return blocks;
}

/* A replayed WCL initial scan may use its queued token only through the
 * first command doorbell.  Clearing the pending record is therefore a real
 * cancellation fence, rather than a best-effort hint to a task which may
 * already have copied the upper generation.  Call with scan_lease_lock held. */
static bool
iwn_scan_lease_initial_handoff_valid_locked(const struct iwn_softc *sc)
{
    const struct iwn_wcl_initial_scan_pending *pending;

    if (sc == NULL ||
        sc->sc_scan_lease.owner != IWN_SCAN_LEASE_WCL_INITIAL)
        return false;
    if (sc->sc_scan_lease.wcl_initial_handoff_serial == 0)
        return true;
    pending = &sc->sc_wcl_initial_scan_pending;
    return pending->queued && pending->launching &&
        pending->terminal_handoff_ready &&
        pending->upper_generation == sc->sc_scan_lease.upper_generation &&
        pending->generic_serial ==
            sc->sc_scan_lease.wcl_initial_handoff_serial;
}

static bool
iwn_scan_lease_reserve(struct iwn_softc *sc, enum iwn_scan_lease_owner owner,
                       u_int64_t upper_generation,
                       u_int64_t required_initial_handoff_serial,
                       u_int32_t *out_backend_generation,
                       u_int64_t *out_serial,
                       u_int64_t direct_sae_scan_generation,
                       u_int8_t wnm_target_channel,
                       u_int64_t reassoc_serial = 0)
{
    u_int64_t serial;
    u_int64_t join_generation = 0;
    const bool direct_sae_scan = direct_sae_scan_generation != 0;

    const bool tagged_controller_owner = iwn_scan_lease_owner_is_wcl(owner) ||
        owner == IWN_SCAN_LEASE_STANDARD_CONTROLLER;
    if (sc == NULL || sc->sc_scan_lease_lock == NULL ||
        owner == IWN_SCAN_LEASE_NONE || out_serial == NULL ||
        (required_initial_handoff_serial != 0 &&
         owner != IWN_SCAN_LEASE_WCL_INITIAL) ||
        (tagged_controller_owner &&
         (upper_generation == 0 || out_backend_generation == NULL)))
        return false;
    if (out_backend_generation != NULL)
        *out_backend_generation = 0;

    /* No nested selected-BSS/scan leaf locks. A later replacement can only
     * make this copied token stale; it cannot retag the admitted command. */
    if (owner == IWN_SCAN_LEASE_GENERIC_FOREGROUND)
        join_generation = ieee80211_wcl_join_scan_generation(&sc->sc_ic);
    if (reassoc_serial != 0 &&
        (owner != IWN_SCAN_LEASE_GENERIC_BACKGROUND ||
         !ieee80211_wcl_reassoc_current(&sc->sc_ic, reassoc_serial)))
        return false;

    IOSimpleLockLock(sc->sc_scan_lease_lock);
    const bool initial_pending = sc->sc_wcl_initial_scan_pending.queued;
    const bool exact_initial_pending =
        owner == IWN_SCAN_LEASE_WCL_INITIAL && initial_pending &&
        sc->sc_wcl_initial_scan_pending.launching &&
        sc->sc_wcl_initial_scan_pending.terminal_handoff_ready &&
        sc->sc_wcl_initial_scan_pending.upper_generation == upper_generation &&
        required_initial_handoff_serial != 0 &&
        sc->sc_wcl_initial_scan_pending.generic_serial ==
            required_initial_handoff_serial;
    if (iwn_scan_lease_live_locked(sc) ||
        (sc->sc_flags & IWN_FLAG_SCANNING) != 0 ||
        sc->sc_ap_transition_scan_blocked ||
        sc->sc_sae_join_scan_block_generation != 0 ||
        sc->sc_sae_bss_loss_join_handoff_generation != 0 ||
        (sc->sc_sae_wcl_admission_reserved && !direct_sae_scan) ||
        (direct_sae_scan && !sc->sc_sae_wcl_admission_reserved) ||
        (required_initial_handoff_serial != 0 && !exact_initial_pending) ||
        (initial_pending && !exact_initial_pending)) {
        IOSimpleLockUnlock(sc->sc_scan_lease_lock);
        return false;
    }
    if (direct_sae_scan) {
        sc->sc_sae_wcl_admission_reserved = false;
        sc->sc_sae_wcl_admission_requires_fresh_scan = false;
        sc->sc_sae_join_scan_block_generation =
            direct_sae_scan_generation;
    }
    serial = ++sc->sc_scan_lease_next_serial;
    if (serial == 0)
        serial = ++sc->sc_scan_lease_next_serial;
    iwn_scan_lease_clear_locked(sc);
    sc->sc_scan_lease.serial = serial;
    sc->sc_scan_lease.owner = owner;
    sc->sc_scan_lease.phase = IWN_SCAN_LEASE_ARMING;
    sc->sc_scan_lease.wnm_target_channel = wnm_target_channel;
    sc->sc_scan_lease.upper_generation = upper_generation;
    sc->sc_scan_lease.join_generation = join_generation;
    sc->sc_scan_lease.reassoc_serial = reassoc_serial;
    sc->sc_scan_lease.wcl_initial_handoff_serial =
        required_initial_handoff_serial;
    if (tagged_controller_owner) {
        u_int32_t generation = (u_int32_t)serial;
        if (generation == 0)
            generation = 1;
        sc->sc_scan_lease.backend_generation = generation;
        *out_backend_generation = generation;
    }
    *out_serial = serial;
    IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    return true;
}

static bool
iwn_scan_lease_rollback(struct iwn_softc *sc, u_int64_t serial)
{
    bool rolled_back = false;

    if (sc == NULL || sc->sc_scan_lease_lock == NULL || serial == 0)
        return false;
    IOSimpleLockLock(sc->sc_scan_lease_lock);
    if (iwn_scan_lease_live_locked(sc) &&
        sc->sc_scan_lease.serial == serial &&
        sc->sc_scan_lease.phase == IWN_SCAN_LEASE_ARMING &&
        !sc->sc_scan_lease.command_submitted &&
        !sc->sc_scan_lease.hardware_invalidated &&
        !sc->sc_scan_lease.terminal_claimed) {
        if (sc->sc_scan_lease.owner == IWN_SCAN_LEASE_GENERIC_FOREGROUND &&
            sc->sc_wcl_initial_scan_pending.queued &&
            sc->sc_wcl_initial_scan_pending.generic_serial == serial)
            sc->sc_wcl_initial_scan_pending.terminal_handoff_ready = true;
        iwn_scan_lease_clear_locked(sc);
        rolled_back = true;
    }
    IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    return rolled_back;
}

static bool
iwn_scan_lease_arm_submission(struct iwn_softc *sc, u_int64_t serial,
                               bool *out_abort_requested)
{
    bool marked = false;

    if (out_abort_requested != NULL)
        *out_abort_requested = false;
    if (sc == NULL || sc->sc_scan_lease_lock == NULL || serial == 0)
        return false;
    IOSimpleLockLock(sc->sc_scan_lease_lock);
    if (iwn_scan_lease_live_locked(sc) &&
        sc->sc_scan_lease.serial == serial &&
        sc->sc_scan_lease.phase == IWN_SCAN_LEASE_ARMING &&
        !sc->sc_scan_lease.command_submitted &&
        !sc->sc_scan_lease.publication_invalidated &&
        !sc->sc_scan_lease.terminal_claimed &&
        (sc->sc_scan_lease.owner != IWN_SCAN_LEASE_WCL_INITIAL ||
         iwn_scan_lease_initial_handoff_valid_locked(sc))) {
        const bool abort_requested = sc->sc_scan_lease.abort_requested;
        /* ARMING is deliberately distinct from a firmware-visible command.
         * The doorbell hook below alone publishes command_submitted while it
         * holds this IRQ-safe leaf through IWN_WRITE(). */
        if (out_abort_requested != NULL)
            *out_abort_requested = abort_requested;
        marked = true;
    }
    IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    return marked;
}

static bool
iwn_scan_lease_prepare_doorbell(struct iwn_softc *sc, void *opaque)
{
    struct iwn_scan_doorbell_context *context =
        (struct iwn_scan_doorbell_context *)opaque;

    if (context != NULL) {
        context->lock = NULL;
        context->lock_held = false;
        context->committed = false;
        context->initial_handoff_serial = 0;
    }
    if (sc == NULL || context == NULL || context->serial == 0 ||
        sc->sc_scan_lease_lock == NULL)
        return false;

    IOSimpleLockLock(sc->sc_scan_lease_lock);
    if (iwn_scan_lease_live_locked(sc) &&
        sc->sc_scan_lease.serial == context->serial &&
        sc->sc_scan_lease.phase == IWN_SCAN_LEASE_ARMING &&
        !sc->sc_scan_lease.command_submitted &&
        !sc->sc_scan_lease.abort_requested &&
        !sc->sc_scan_lease.publication_invalidated &&
        !sc->sc_scan_lease.terminal_claimed &&
        (sc->sc_scan_lease.owner != IWN_SCAN_LEASE_WCL_INITIAL ||
         iwn_scan_lease_initial_handoff_valid_locked(sc))) {
        /* Keep the simple lock across scheduler publication and WRPTR.  Both
         * operations are non-blocking register/DMA writes; an IRQ STOP_SCAN
         * is either rejected before this point or observes a doorbelled scan. */
        sc->sc_scan_lease.command_submitted = true;
        sc->sc_scan_lease.phase = IWN_SCAN_LEASE_ACTIVE;
        /* Publish the net80211-facing scan flags in this same pre-doorbell
         * leaf.  STOP_SCAN may be delivered immediately after WRPTR; it must
         * never clear the flags only for submit() to set them again later. */
        sc->sc_flags |= IWN_FLAG_SCANNING;
        if (context->background)
            sc->sc_flags |= IWN_FLAG_BGSCAN;
        context->initial_handoff_serial =
            sc->sc_scan_lease.wcl_initial_handoff_serial;
        context->lock = sc->sc_scan_lease_lock;
        context->lock_held = true;
        context->committed = true;
        return true;
    }
    IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    return false;
}

static void
iwn_scan_lease_finish_doorbell(struct iwn_softc *sc, void *opaque)
{
    struct iwn_scan_doorbell_context *context =
        (struct iwn_scan_doorbell_context *)opaque;

    if (context == NULL || !context->lock_held || context->lock == NULL)
        return;
    /* The WRPTR write is now complete while the exact lease fence still
     * excludes a STOP_SCAN IRQ.  Publish the backend generation before that
     * IRQ can classify the terminal; the upper reducer may then safely fence
     * an immediately completing fresh WCL_INITIAL command. */
    bool publish_wcl_initial_started = false;
    if (context->publish_wcl_initial_started && sc != NULL &&
        context->upper_generation != 0 &&
        context->backend_generation != 0 &&
        sc->sc_scan_lease.serial == context->serial &&
        sc->sc_scan_lease.owner == IWN_SCAN_LEASE_WCL_INITIAL &&
        sc->sc_scan_lease.command_submitted) {
        /* This is the physical start boundary, not merely a successful
         * reserve.  Retire the handoff token while the same lock excludes a
         * STOP_SCAN interrupt, then retain a sticky reset fence across a
         * later multi-band continuation. */
        sc->sc_scan_lease.wcl_initial_handoff_serial = 0;
        sc->sc_scan_lease.wcl_initial_started = true;
        if (sc->sc_wcl_initial_scan_pending.queued &&
            sc->sc_wcl_initial_scan_pending.launching &&
            sc->sc_wcl_initial_scan_pending.upper_generation ==
                context->upper_generation &&
            context->initial_handoff_serial != 0 &&
            sc->sc_wcl_initial_scan_pending.generic_serial ==
                context->initial_handoff_serial)
            sc->sc_wcl_initial_scan_pending.command_started = true;
        publish_wcl_initial_started = true;
    }
    if (publish_wcl_initial_started && sc->sc_ic.ic_event_handler != NULL) {
        struct ieee80211_wcl_scan_started started;
        explicit_bzero(&started, sizeof(started));
        started.generation = context->upper_generation;
        started.backend_generation = context->backend_generation;
        (*sc->sc_ic.ic_event_handler)(&sc->sc_ic,
            IEEE80211_EVT_WCL_SCAN_STARTED, &started);
        explicit_bzero(&started, sizeof(started));
    }
    context->lock_held = false;
    IOSimpleLockUnlock(context->lock);
}

static bool
iwn_scan_lease_mark_abort(struct iwn_softc *sc,
                          enum iwn_scan_lease_owner required_owner,
                          u_int64_t required_upper_generation,
                          u_int64_t *out_serial, bool *out_submit_abort)
{
    bool matched = false;

    if (out_serial != NULL)
        *out_serial = 0;
    if (out_submit_abort != NULL)
        *out_submit_abort = false;
    if (sc == NULL || sc->sc_scan_lease_lock == NULL)
        return false;
    IOSimpleLockLock(sc->sc_scan_lease_lock);
    if (iwn_scan_lease_live_locked(sc) &&
        (required_owner == IWN_SCAN_LEASE_NONE ||
         (required_owner == IWN_SCAN_LEASE_WCL_BACKGROUND &&
          iwn_scan_lease_owner_is_wcl(sc->sc_scan_lease.owner)) ||
         sc->sc_scan_lease.owner == required_owner) &&
        (required_upper_generation == 0 ||
         sc->sc_scan_lease.upper_generation == required_upper_generation) &&
        !sc->sc_scan_lease.hardware_invalidated &&
        !sc->sc_scan_lease.terminal_claimed &&
        sc->sc_scan_lease.phase != IWN_SCAN_LEASE_DRAINING) {
        const bool was_requested = sc->sc_scan_lease.abort_requested;
        const bool was_arming =
            sc->sc_scan_lease.phase == IWN_SCAN_LEASE_ARMING;
        sc->sc_scan_lease.abort_requested = true;
        if (!sc->sc_scan_lease.terminal_claimed && !was_arming)
            sc->sc_scan_lease.phase = IWN_SCAN_LEASE_ABORTING;
        if (out_serial != NULL)
            *out_serial = sc->sc_scan_lease.serial;
        if (out_submit_abort != NULL)
            *out_submit_abort = sc->sc_scan_lease.command_submitted &&
                !was_arming &&
                !was_requested && !sc->sc_scan_lease.terminal_claimed;
        matched = true;
    }
    IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    return matched;
}

static void
iwn_scan_lease_abort_submission_failed(struct iwn_softc *sc, u_int64_t serial)
{
    if (sc == NULL || sc->sc_scan_lease_lock == NULL || serial == 0)
        return;
    IOSimpleLockLock(sc->sc_scan_lease_lock);
    if (iwn_scan_lease_live_locked(sc) &&
        sc->sc_scan_lease.serial == serial &&
        !sc->sc_scan_lease.hardware_invalidated &&
        !sc->sc_scan_lease.terminal_claimed) {
        sc->sc_scan_lease.abort_requested = false;
        if (sc->sc_scan_lease.command_submitted)
            sc->sc_scan_lease.phase = IWN_SCAN_LEASE_ACTIVE;
    }
    IOSimpleLockUnlock(sc->sc_scan_lease_lock);
}

static bool
iwn_scan_lease_claim_terminal(struct iwn_softc *sc,
                              struct iwn_scan_lease_terminal *terminal)
{
    bool claimed = false;

    if (terminal != NULL)
        explicit_bzero(terminal, sizeof(*terminal));
    if (sc == NULL || terminal == NULL || sc->sc_scan_lease_lock == NULL)
        return false;
    IOSimpleLockLock(sc->sc_scan_lease_lock);
    if (iwn_scan_lease_live_locked(sc) &&
        sc->sc_scan_lease.command_submitted &&
        !sc->sc_scan_lease.hardware_invalidated &&
        (sc->sc_scan_lease.phase == IWN_SCAN_LEASE_ACTIVE ||
         sc->sc_scan_lease.phase == IWN_SCAN_LEASE_ABORTING) &&
        !sc->sc_scan_lease.terminal_claimed) {
        terminal->valid = true;
        terminal->owner = (enum iwn_scan_lease_owner)
            sc->sc_scan_lease.owner;
        terminal->serial = sc->sc_scan_lease.serial;
        terminal->wcl = iwn_scan_lease_owner_is_wcl(
            sc->sc_scan_lease.owner);
        terminal->wcl_foreground = iwn_scan_lease_owner_is_wcl_initial(
            sc->sc_scan_lease.owner);
        terminal->publish_wcl_terminal = terminal->wcl &&
            !sc->sc_scan_lease.publication_invalidated;
        terminal->standard = sc->sc_scan_lease.owner ==
            IWN_SCAN_LEASE_STANDARD_CONTROLLER;
        terminal->publish_standard_terminal = terminal->standard &&
            !sc->sc_scan_lease.publication_invalidated;
        terminal->aborted = sc->sc_scan_lease.abort_requested;
        terminal->upper_generation = sc->sc_scan_lease.upper_generation;
        terminal->join_generation = sc->sc_scan_lease.join_generation;
        terminal->reassoc_serial = sc->sc_scan_lease.reassoc_serial;
        terminal->backend_generation = sc->sc_scan_lease.backend_generation;
        sc->sc_scan_lease.terminal_claimed = true;
        sc->sc_scan_lease.phase = IWN_SCAN_LEASE_DRAINING;
        claimed = true;
    }
    IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    return claimed;
}

static bool
iwn_scan_lease_begin_continuation(struct iwn_softc *sc,
                                  u_int64_t *out_serial,
                                  bool *out_wcl_scan)
{
    bool continuing = false;

    if (out_serial != NULL)
        *out_serial = 0;
    if (out_wcl_scan != NULL)
        *out_wcl_scan = false;
    if (sc == NULL || sc->sc_scan_lease_lock == NULL || out_serial == NULL ||
        out_wcl_scan == NULL)
        return false;
    IOSimpleLockLock(sc->sc_scan_lease_lock);
    if (iwn_scan_lease_live_locked(sc) &&
        sc->sc_scan_lease.command_submitted &&
        sc->sc_scan_lease.phase == IWN_SCAN_LEASE_ACTIVE &&
        !sc->sc_scan_lease.publication_invalidated &&
        !sc->sc_scan_lease.terminal_claimed &&
        !sc->sc_scan_lease.abort_requested) {
        /* The current STOP_SCAN is consumed by its handler.  Close admission
         * to duplicate/stale terminals while the 5 GHz command is built. */
        sc->sc_scan_lease.command_submitted = false;
        sc->sc_scan_lease.phase = IWN_SCAN_LEASE_ARMING;
        *out_serial = sc->sc_scan_lease.serial;
        *out_wcl_scan = iwn_scan_lease_owner_is_wcl(
            sc->sc_scan_lease.owner);
        continuing = true;
    }
    IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    return continuing;
}

static bool
iwn_scan_lease_restore_continuation(struct iwn_softc *sc, u_int64_t serial,
                                    bool abort_terminal)
{
    bool restored = false;

    if (sc == NULL || sc->sc_scan_lease_lock == NULL || serial == 0)
        return false;
    IOSimpleLockLock(sc->sc_scan_lease_lock);
    if (iwn_scan_lease_live_locked(sc) &&
        sc->sc_scan_lease.serial == serial &&
        sc->sc_scan_lease.phase == IWN_SCAN_LEASE_ARMING &&
        !sc->sc_scan_lease.command_submitted &&
        !sc->sc_scan_lease.hardware_invalidated &&
        !sc->sc_scan_lease.terminal_claimed) {
        /* The 2.4 GHz STOP_SCAN currently being handled remains the native
         * terminal when its 5 GHz continuation cannot cross a doorbell.  Mark
         * it aborted rather than publishing an apparently complete dual-band
         * scan which never attempted the second band. */
        if (abort_terminal)
            sc->sc_scan_lease.abort_requested = true;
        sc->sc_scan_lease.command_submitted = true;
        sc->sc_scan_lease.phase = sc->sc_scan_lease.abort_requested ?
            IWN_SCAN_LEASE_ABORTING : IWN_SCAN_LEASE_ACTIVE;
        restored = true;
    }
    IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    return restored;
}

static bool
iwn_scan_lease_finish_terminal(struct iwn_softc *sc, u_int64_t serial,
    bool *join_terminal_retired = NULL)
{
    bool schedule_replay = false;

    if (join_terminal_retired != NULL)
        *join_terminal_retired = false;
    if (sc == NULL || sc->sc_scan_lease_lock == NULL || serial == 0)
        return false;
    IOSimpleLockLock(sc->sc_scan_lease_lock);
    if (iwn_scan_lease_live_locked(sc) &&
        sc->sc_scan_lease.serial == serial &&
        sc->sc_scan_lease.terminal_claimed) {
        /* A hardware reset may race terminal net80211 cleanup.  It owns the
         * invalidated lease and has already discarded replay; never revive a
         * queued S_SCAN intent after its radio epoch has been fenced. */
        schedule_replay = !sc->sc_scan_lease.hardware_invalidated &&
            (sc->sc_scan_lease_replay_pending ||
             sc->sc_wcl_join_cleanup_generation != 0);
        if (join_terminal_retired != NULL)
            *join_terminal_retired = !sc->sc_scan_lease.hardware_invalidated;
        /* auth_hold() must synchronously consume a hard-loss handoff while
         * end_scan() owns this terminal.  Never let an unconsumed token
         * escape into a later scan or association generation. */
        sc->sc_sae_bss_loss_join_handoff_generation = 0;
        iwn_scan_lease_clear_locked(sc);
    }
    IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    return schedule_replay;
}

static bool
iwn_scan_lease_defer_scan(struct iwn_softc *sc,
                          enum ieee80211_state nstate, int arg,
                          u_int64_t direct_sae_generation,
                          u_int64_t *out_serial, bool *out_submit_abort)
{
    bool deferred = false;

    if (out_serial != NULL)
        *out_serial = 0;
    if (out_submit_abort != NULL)
        *out_submit_abort = false;
    if (sc == NULL || sc->sc_scan_lease_lock == NULL ||
        nstate != IEEE80211_S_SCAN)
        return false;
    IOSimpleLockLock(sc->sc_scan_lease_lock);
    if (iwn_scan_lease_live_locked(sc) &&
        !sc->sc_scan_lease.hardware_invalidated &&
        !sc->sc_scan_lease.terminal_claimed &&
        sc->sc_scan_lease.phase != IWN_SCAN_LEASE_DRAINING) {
        const bool was_requested = sc->sc_scan_lease.abort_requested;
        const bool was_arming =
            sc->sc_scan_lease.phase == IWN_SCAN_LEASE_ARMING;
        sc->sc_scan_lease_replay_pending = true;
        sc->sc_scan_lease_replay_nstate = nstate;
        sc->sc_scan_lease_replay_arg = arg;
        sc->sc_scan_lease_replay_sae_generation =
            direct_sae_generation;
        sc->sc_scan_lease.abort_requested = true;
        if (!sc->sc_scan_lease.terminal_claimed && !was_arming)
            sc->sc_scan_lease.phase = IWN_SCAN_LEASE_ABORTING;
        if (out_serial != NULL)
            *out_serial = sc->sc_scan_lease.serial;
        if (out_submit_abort != NULL)
            *out_submit_abort = sc->sc_scan_lease.command_submitted &&
                !was_arming &&
                !was_requested && !sc->sc_scan_lease.terminal_claimed;
        deferred = true;
    }
    IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    return deferred;
}

/* ieee80211_end_scan() may immediately request the next foreground mode
 * after STOP_SCAN has claimed its terminal.  That request must not submit a
 * generic scan under the retiring lease: retain the intent and replay it only
 * after iwn_scan_lease_finish_terminal() has made the radio idle. */
static bool
iwn_scan_lease_defer_terminal_replay(struct iwn_softc *sc,
                                     enum ieee80211_state nstate, int arg)
{
    bool deferred = false;

    if (sc == NULL || sc->sc_scan_lease_lock == NULL ||
        nstate != IEEE80211_S_SCAN)
        return false;
    IOSimpleLockLock(sc->sc_scan_lease_lock);
    if (iwn_scan_lease_live_locked(sc) &&
        !sc->sc_scan_lease.hardware_invalidated &&
        sc->sc_scan_lease.terminal_claimed &&
        sc->sc_scan_lease.phase == IWN_SCAN_LEASE_DRAINING) {
        sc->sc_scan_lease_replay_pending = true;
        sc->sc_scan_lease_replay_nstate = nstate;
        sc->sc_scan_lease_replay_arg = arg;
        sc->sc_scan_lease_replay_sae_generation = 0;
        deferred = true;
    }
    IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    return deferred;
}

static void
iwn_scan_lease_drop_replay(struct iwn_softc *sc)
{
    if (sc == NULL || sc->sc_scan_lease_lock == NULL)
        return;
    IOSimpleLockLock(sc->sc_scan_lease_lock);
    sc->sc_scan_lease_replay_pending = false;
    sc->sc_scan_lease_replay_nstate = IEEE80211_S_INIT;
    sc->sc_scan_lease_replay_arg = -1;
    if (sc->sc_scan_lease_replay_sae_generation != 0) {
        sc->sc_sae_wcl_admission_reserved = false;
        sc->sc_sae_wcl_admission_requires_fresh_scan = false;
    }
    sc->sc_scan_lease_replay_sae_generation = 0;
    IOSimpleLockUnlock(sc->sc_scan_lease_lock);
}

static enum iwn_scan_lease_owner
iwn_scan_lease_begin_hardware_invalidation(
    struct iwn_softc *sc, struct ieee80211_wcl_scan_invalidation *wcl_event,
    struct ieee80211_standard_scan_invalidation *standard_event,
    u_int64_t *queued_initial_rejected_generation)
{
    enum iwn_scan_lease_owner publish_owner = IWN_SCAN_LEASE_NONE;

    if (wcl_event != NULL)
        explicit_bzero(wcl_event, sizeof(*wcl_event));
    if (standard_event != NULL)
        explicit_bzero(standard_event, sizeof(*standard_event));
    if (queued_initial_rejected_generation != NULL)
        *queued_initial_rejected_generation = 0;
    if (sc == NULL || wcl_event == NULL || standard_event == NULL ||
        queued_initial_rejected_generation == NULL ||
        sc->sc_scan_lease_lock == NULL)
        return IWN_SCAN_LEASE_NONE;
    IOSimpleLockLock(sc->sc_scan_lease_lock);
    if (iwn_scan_lease_live_locked(sc)) {
        const bool started_initial =
            sc->sc_scan_lease.owner == IWN_SCAN_LEASE_WCL_INITIAL &&
            sc->sc_scan_lease.wcl_initial_started;
        if (iwn_scan_lease_owner_is_wcl(sc->sc_scan_lease.owner) &&
            (sc->sc_scan_lease.owner == IWN_SCAN_LEASE_WCL_BACKGROUND ||
             started_initial) &&
            !sc->sc_scan_lease.terminal_claimed &&
            !sc->sc_scan_lease.publication_invalidated) {
            wcl_event->generation = sc->sc_scan_lease.upper_generation;
            wcl_event->backend_generation =
                sc->sc_scan_lease.backend_generation;
            if (wcl_event->generation != 0 &&
                wcl_event->backend_generation != 0)
                publish_owner = (enum iwn_scan_lease_owner)
                    sc->sc_scan_lease.owner;
        } else if (sc->sc_scan_lease.owner ==
                       IWN_SCAN_LEASE_STANDARD_CONTROLLER &&
                   !sc->sc_scan_lease.terminal_claimed &&
                   !sc->sc_scan_lease.publication_invalidated) {
            standard_event->generation = sc->sc_scan_lease.upper_generation;
            standard_event->backend_generation =
                sc->sc_scan_lease.backend_generation;
            if (standard_event->generation != 0 &&
                standard_event->backend_generation != 0)
                publish_owner = IWN_SCAN_LEASE_STANDARD_CONTROLLER;
        }
        sc->sc_scan_lease.publication_invalidated = true;
        sc->sc_scan_lease.hardware_invalidated = true;
        sc->sc_scan_lease.phase = IWN_SCAN_LEASE_DRAINING;
    }
    if (sc->sc_wcl_initial_scan_pending.queued) {
        /* A queued handoff has not doorbelled a WCL command.  Do not report a
         * zero-backend INVALIDATED event; reject the pending upper ticket
         * without C9/ED instead.  A real WCL_INITIAL lease above owns the
         * normal generation-fenced invalidation. */
        const bool pending_started =
            sc->sc_wcl_initial_scan_pending.command_started ||
            (iwn_scan_lease_live_locked(sc) &&
             iwn_scan_lease_owner_is_wcl_initial(
                 sc->sc_scan_lease.owner) &&
             sc->sc_scan_lease.wcl_initial_started);
        if (!pending_started)
            *queued_initial_rejected_generation =
                sc->sc_wcl_initial_scan_pending.upper_generation;
        iwn_wcl_initial_scan_pending_clear_locked(sc);
    }
    /* A hardware reset discards a queued generic retry.  It must never replay
     * against a different post-reset association epoch. */
    sc->sc_scan_lease_replay_pending = false;
    sc->sc_scan_lease_replay_nstate = IEEE80211_S_INIT;
    sc->sc_scan_lease_replay_arg = -1;
    sc->sc_scan_lease_replay_sae_generation = 0;
    sc->sc_wcl_join_cleanup_generation = 0;
    sc->sc_sae_wcl_admission_reserved = false;
    sc->sc_sae_wcl_admission_requires_fresh_scan = false;
    IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    return publish_owner;
}

static void
iwn_scan_lease_retire_after_hardware_stop(struct iwn_softc *sc)
{
    if (sc == NULL || sc->sc_scan_lease_lock == NULL)
        return;
    IOSimpleLockLock(sc->sc_scan_lease_lock);
    if (iwn_scan_lease_live_locked(sc))
        iwn_scan_lease_clear_locked(sc);
    IOSimpleLockUnlock(sc->sc_scan_lease_lock);
}

/*
 * A protected join is not usable until the supplicant has installed its keys
 * and opened the controlled port.  In particular, do not let a generic scan
 * retune the BSS context after authentication has started or between the
 * Association Response and the four-way handshake.  The driver's associated
 * RXON can be committed while net80211 is still inside the ASSOC -> RUN
 * transition, so keying this fence only on S_RUN leaves a real workloop race.
 * Controller-owned background scans already had a partial copy of this
 * policy; keep the invariant here so every state-machine entry and every
 * physical scan owner observes the same association boundary.
 */
static bool
iwn_rsn_join_scan_blocked(const struct ieee80211com *ic)
{
    const bool protected_join = ic != NULL &&
        (ic->ic_state == IEEE80211_S_AUTH ||
         ic->ic_state == IEEE80211_S_ASSOC ||
         ic->ic_state == IEEE80211_S_RUN);

    return protected_join &&
        (ic->ic_flags & IEEE80211_F_RSNON) != 0 &&
        (ic->ic_bss == NULL || !ic->ic_bss->ni_port_valid);
}

int ItlIwn::
iwn_newstate_preflight(struct ieee80211com *ic,
                       enum ieee80211_state nstate, int arg)
{
    struct iwn_softc *sc;
    ItlIwn *that;
    u_int64_t serial = 0;
    bool submit_abort = false;

    if (ic == NULL || nstate != IEEE80211_S_SCAN)
        return 0;
    sc = (struct iwn_softc *)ic->ic_if.if_softc;
    if (sc == NULL)
        return 0;
    that = container_of(sc, ItlIwn, com);
    /* Tahoe's public role-7 AP transaction injects precisely one generic
     * RUN -> SCAN(-1) after enabling ap1 and before its HOST_AP_MODE carrier.
     * Let the APSTA owner consume only that one confirmed handoff while the
     * primary BSS is still fully associated.  Returning here is before
     * net80211 advances its association epoch or clears RXON/BSS state. */
    if (airportItlwmConsumeAPSTAPrimaryStaHandoffScan(
            that->getController(), ic, arg))
        return 1;
    /* Reject before net80211 advances the association epoch or tears down
     * the current BSS.  A scanner-internal hop belongs to a command which
     * was admitted before RUN and must still be allowed to complete. */
    if (arg != IEEE80211_NEWSTATE_ARG_SCAN_HOP &&
        iwn_rsn_join_scan_blocked(ic))
        return 1;
    /* Do not let an ordinary state-machine scan tear down the BSS/epoch
     * owned by an in-progress direct SAE + PMF join.  Scanner-internal hops
     * remain part of the already admitted physical command. */
    if (arg != IEEE80211_NEWSTATE_ARG_SCAN_HOP &&
        iwn_sae_join_scan_blocked(sc))
        return 1;
    if (iwn_wcl_initial_scan_pending_blocks_generic(sc))
        return 1;
    const bool public_associate_restart =
        arg == IEEE80211_NEWSTATE_ARG_PUBLIC_ASSOCIATE;
    if (ic->ic_state == IEEE80211_S_SCAN &&
        !public_associate_restart)
        return iwn_scan_lease_defer_terminal_replay(sc, nstate, arg) ? 1 : 0;
    if (ic->ic_state != IEEE80211_S_RUN &&
        !(ic->ic_state == IEEE80211_S_SCAN &&
          public_associate_restart))
        return 0;
    if (!iwn_scan_lease_defer_scan(sc, nstate, arg, 0, &serial,
                                   &submit_abort))
        return 0;

    /* Consume before generic epoch advancement.  Public ASSOCIATE must not
     * coalesce onto a physical command whose directed SSID/probe template was
     * built before the new policy existed.  The same fenced abort/replay path
     * used for a live RUN scan starts one fresh command after the old terminal.
     * A command failure cannot safely replay over a still-live radio
     * transaction, so reset/reinit is the fail-closed recovery owner. */
    if (submit_abort && that->iwn_cmd(sc, IWN_CMD_SCAN_ABORT, NULL, 0, 1) != 0) {
        iwn_scan_lease_abort_submission_failed(sc, serial);
        iwn_scan_lease_drop_replay(sc);
        sc->sc_flags |= IWN_FLAG_FATAL_RECOVERY;
        (void)task_add(systq, &sc->init_task);
    }
    return 1;
}

static u_int64_t
iwn_scan_lease_take_join_cleanup(struct iwn_softc *sc)
{
    u_int64_t generation = 0;

    if (sc == NULL || sc->sc_scan_lease_lock == NULL)
        return 0;
    IOSimpleLockLock(sc->sc_scan_lease_lock);
    if (sc->sc_scan_lease_replay_task_ready &&
        !iwn_scan_lease_live_locked(sc) &&
        (sc->sc_flags & IWN_FLAG_SCANNING) == 0 &&
        (sc->sc_ic.ic_if.if_flags & (IFF_UP | IFF_RUNNING)) ==
            (IFF_UP | IFF_RUNNING)) {
        generation = sc->sc_wcl_join_cleanup_generation;
        sc->sc_wcl_join_cleanup_generation = 0;
    }
    IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    return generation;
}

void ItlIwn::
iwn_scan_lease_replay_task(void *arg)
{
    struct iwn_softc *sc = (struct iwn_softc *)arg;
    struct ieee80211com *ic;
    enum ieee80211_state nstate;
    int nstate_arg;
    u_int64_t initial_generation = 0;
    u_int64_t initial_handoff_serial = 0;
    u_int64_t direct_sae_generation = 0;
    u_int64_t join_cleanup_generation = 0;
    u_int32_t initial_backend_generation = 0;
    bool launch_initial = false;
    bool reject_initial = false;
    bool replay = false;

    if (sc == NULL || sc->sc_scan_lease_lock == NULL)
        return;
    ic = &sc->sc_ic;
    join_cleanup_generation = iwn_scan_lease_take_join_cleanup(sc);
    if (join_cleanup_generation != 0 &&
        ieee80211_wcl_join_failure_pending(ic, join_cleanup_generation)) {
        ieee80211_pae_assoc_epoch_note_newstate(ic, IEEE80211_S_SCAN, -1);
        if (ieee80211_wcl_join_failure_pending(ic, join_cleanup_generation) &&
            iwn_newstate_impl(ic, IEEE80211_S_SCAN, -1,
                join_cleanup_generation) == 0)
            iwn_sae_engine_request_join_retirement(sc, join_cleanup_generation);
    }
    /* Cleanup can yield to a new association or scan. Recheck all physical
     * admission below rather than carrying the earlier idle observation. */
    IOSimpleLockLock(sc->sc_scan_lease_lock);
    if (sc->sc_scan_lease_replay_task_ready &&
        !iwn_scan_lease_live_locked(sc) &&
        sc->sc_wcl_initial_scan_pending.queued &&
        sc->sc_wcl_initial_scan_pending.terminal_handoff_ready &&
        !sc->sc_wcl_initial_scan_pending.launching &&
        (ic->ic_if.if_flags & (IFF_UP | IFF_RUNNING)) ==
            (IFF_UP | IFF_RUNNING)) {
        initial_generation =
            sc->sc_wcl_initial_scan_pending.upper_generation;
        initial_handoff_serial =
            sc->sc_wcl_initial_scan_pending.generic_serial;
        sc->sc_wcl_initial_scan_pending.launching = true;
        launch_initial = initial_generation != 0 &&
            initial_handoff_serial != 0;
    } else if (sc->sc_scan_lease_replay_task_ready &&
        !sc->sc_wcl_initial_scan_pending.queued &&
        sc->sc_scan_lease_replay_pending &&
        !iwn_scan_lease_live_locked(sc) &&
        (ic->ic_if.if_flags & (IFF_UP | IFF_RUNNING)) ==
            (IFF_UP | IFF_RUNNING)) {
        nstate = sc->sc_scan_lease_replay_nstate;
        nstate_arg = sc->sc_scan_lease_replay_arg;
        direct_sae_generation =
            sc->sc_scan_lease_replay_sae_generation;
        sc->sc_scan_lease_replay_pending = false;
        sc->sc_scan_lease_replay_nstate = IEEE80211_S_INIT;
        sc->sc_scan_lease_replay_arg = -1;
        sc->sc_scan_lease_replay_sae_generation = 0;
        replay = true;
    }
    IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    if (launch_initial) {
        ItlIwn *that = container_of(sc, ItlIwn, com);
        /* The queued ticket retains the same exact WCL plan as immediate
         * admission. Select its first eligible band again at handoff; a
         * 5-GHz-only request must not build an empty 2.4-GHz command after
         * the preceding generic scan has released the hardware. The exact
         * generation/serial reservation below still arbitrates cancellation. */
        uint16_t scan_flags = 0;
        int error = iwn_wcl_scan_initial_band(sc, &scan_flags);
        if (error == 0) {
            error = that->iwn_scan_start(sc, scan_flags, 0,
                IWN_SCAN_LEASE_WCL_INITIAL, initial_generation,
                initial_handoff_serial,
                &initial_backend_generation, false);
        }

        IOSimpleLockLock(sc->sc_scan_lease_lock);
        if (sc->sc_wcl_initial_scan_pending.queued &&
            sc->sc_wcl_initial_scan_pending.launching &&
            sc->sc_wcl_initial_scan_pending.upper_generation ==
                initial_generation &&
            sc->sc_wcl_initial_scan_pending.generic_serial ==
                initial_handoff_serial) {
            const bool command_started =
                sc->sc_wcl_initial_scan_pending.command_started;
            iwn_wcl_initial_scan_pending_clear_locked(sc);
            /* A WCL_SCAN_STARTED edge is published only by the post-WRPTR
             * hook.  Reject only a nonzero return which never crossed that
             * edge; an error after WRPTR remains a real lower transaction
             * whose reset/terminal owner will close the active upper ticket. */
            reject_initial = error != 0 && !command_started;
        }
        IOSimpleLockUnlock(sc->sc_scan_lease_lock);
        if (reject_initial && ic->ic_event_handler != NULL) {
            struct ieee80211_wcl_scan_start_rejected rejected;
            explicit_bzero(&rejected, sizeof(rejected));
            rejected.generation = initial_generation;
            (*ic->ic_event_handler)(ic,
                IEEE80211_EVT_WCL_SCAN_START_REJECTED, &rejected);
            explicit_bzero(&rejected, sizeof(rejected));
        }
        return;
    }
    if (replay) {
        if (direct_sae_generation != 0) {
            int resume_result;

            XYLog("wcl_assoc CACHED_CANDIDATE_REFRESH_REPLAY\n");
            resume_result = ieee80211_sae_wcl_request_resume_scan(
                ic, direct_sae_generation);
            /* Cancellation may supersede the upper generation after the old
             * scan terminal queued this task but before the replay runs.  A
             * successful direct scan consumes the lower reservation in
             * iwn_scan_lease_reserve(); every other terminal result must
             * release it here so the next user join is not starved. */
            if (resume_result !=
                    IEEE80211_SAE_WCL_REQUEST_RESUME_STARTED &&
                resume_result !=
                    IEEE80211_SAE_WCL_REQUEST_RESUME_DEFERRED) {
                ItlIwn *that = container_of(sc, ItlIwn, com);
                that->releaseSaeWclCredentialAdmission();
            }
        } else {
            ieee80211_new_state(ic, nstate, nstate_arg);
        }
    }
}

/*
 * DVM needs one firmware command per band. Tahoe's ScanAdapter, however,
 * can split one public census into a 2.4-GHz and a 5-GHz carrier. Do not
 * always submit the historical 2.4-GHz first command: an exact 5-GHz-only
 * plan would then leave the command with no eligible channels and make
 * IO80211 reject the public request before the radio sees it.
 *
 * With no exact plan (or an empty channel list), retain the established
 * 2.4-GHz-first behaviour; STOP_SCAN will continue to 5 GHz. A non-empty
 * exact plan must instead prove that this band has a locally supported
 * channel.
 */
static bool
iwn_wcl_scan_plan_has_eligible_band(struct iwn_softc *sc, uint16_t flags)
{
    struct ieee80211com *ic;
    struct ieee80211_wcl_scan_plan plan;
    struct ieee80211_channel *channel;

    if (sc == NULL || (ic = &sc->sc_ic) == NULL)
        return false;
    if (ieee80211_wcl_scan_plan_snapshot(ic, &plan) == 0 ||
        plan.channel_filter == 0)
        return true;

    for (channel = &ic->ic_channels[1];
         channel <= &ic->ic_channels[IEEE80211_CHAN_MAX]; channel++) {
        if ((channel->ic_flags & flags) != flags)
            continue;
        if (ieee80211_wcl_scan_plan_channel_allowed(ic, &plan, channel)) {
            explicit_bzero(&plan, sizeof(plan));
            return true;
        }
    }
    explicit_bzero(&plan, sizeof(plan));
    return false;
}

static int
iwn_wcl_scan_initial_band(struct iwn_softc *sc, uint16_t *outFlags)
{
    if (sc == NULL || outFlags == NULL)
        return EINVAL;

    if (iwn_wcl_scan_plan_has_eligible_band(sc, IEEE80211_CHAN_2GHZ)) {
        *outFlags = IEEE80211_CHAN_2GHZ;
        return 0;
    }
    if ((sc->sc_flags & IWN_FLAG_HAS_5GHZ) != 0 &&
        iwn_wcl_scan_plan_has_eligible_band(sc, IEEE80211_CHAN_5GHZ)) {
        *outFlags = IEEE80211_CHAN_5GHZ;
        return 0;
    }
    return EINVAL;
}

IOReturn ItlIwn::
beginWclBackgroundScan(uint64_t generation, uint32_t *outBackendGeneration)
{
    u_int32_t backend_generation = 0;
    uint16_t scan_flags = 0;

    if (outBackendGeneration == NULL || generation == 0)
        return kIOReturnBadArgument;
    *outBackendGeneration = 0;
    if (iwn_wcl_scan_initial_band(&com, &scan_flags) != 0)
        return kIOReturnBadArgument;
    if (iwn_scan_start(&com, scan_flags, 1,
                       IWN_SCAN_LEASE_WCL_BACKGROUND, generation,
                       0,
                       &backend_generation, false) != 0)
        return kIOReturnBusy;
    if (backend_generation == 0)
        return kIOReturnAborted;
    *outBackendGeneration = backend_generation;
    return kIOReturnSuccess;
}

IOReturn ItlIwn::
beginWclInitialScan(uint64_t generation, uint32_t *outBackendGeneration)
{
    struct ieee80211com *ic = &com.sc_ic;
    u_int32_t backend_generation = 0;
    uint16_t scan_flags = 0;
    bool queued = false;
    int error;

    if (generation == 0 || outBackendGeneration == NULL)
        return kIOReturnBadArgument;
    *outBackendGeneration = 0;
    /* An initial scan's upper ticket is activated exclusively by the
     * post-doorbell STARTED event.  Without a synchronous event consumer a
     * zero-generation success would strand that ticket in Starting/Queued,
     * so refuse before reserving any radio work. */
    if (ic->ic_event_handler == NULL)
        return kIOReturnNotReady;
    if (ic->ic_state != IEEE80211_S_SCAN ||
        ic->ic_opmode != IEEE80211_M_STA ||
        (ic->ic_if.if_flags & IFF_RUNNING) == 0 ||
        ic->ic_mgt_timer != 0 ||
        /* WCL owns admission through the exact lower scan lease, matching
         * AppleBCMWLANScanAdapter rather than the legacy net80211 BGSCAN
         * marker.  Link loss can leave that marker set after its physical
         * owner is gone; the lease queue below still rejects real overlap.
         * Preserve any BSSID pin while the empty-ESS and SAE-selection
         * fences keep directed association out of this discovery path. */
        (ic->ic_flags & IEEE80211_F_AUTO_JOIN) == 0 ||
        ic->ic_des_esslen != 0 ||
        ieee80211_sae_wcl_request_scan_selection_held(ic) ||
        ieee80211_sae_wcl_request_scan_selection_owned(ic))
        return kIOReturnBusy;

    if (iwn_wcl_scan_initial_band(&com, &scan_flags) != 0)
        return kIOReturnBadArgument;

    if (!iwn_wcl_initial_scan_queue(&com, generation, &queued))
        return kIOReturnBusy;
    if (queued)
        return kIOReturnSuccess;

    error = iwn_scan_start(&com, scan_flags, 0,
                           IWN_SCAN_LEASE_WCL_INITIAL, generation, 0,
                           &backend_generation, false);
    /* WCL initial ownership becomes active only at the post-WRPTR STARTED
     * edge.  Do not expose a reserve-time backend number to the upper
     * reducer: a reset/no-doorbell rejection must leave it in Starting and
     * be resolved by this call's result instead. */
    *outBackendGeneration = 0;
    if (error == 0)
        return backend_generation != 0 ? kIOReturnSuccess : kIOReturnAborted;
    if (error == EBUSY || error == ECANCELED)
        return kIOReturnBusy;
    if (error == ENETDOWN)
        return kIOReturnNotReady;
    return kIOReturnError;
}

IOReturn ItlIwn::
beginStandardScan(uint64_t generation, bool background,
                  uint32_t *outBackendGeneration)
{
    struct ieee80211com *ic = &com.sc_ic;
    u_int32_t backend_generation = 0;
    int error;

    if (generation == 0 || outBackendGeneration == NULL)
        return kIOReturnBadArgument;
    *outBackendGeneration = 0;

    if (background) {
        if (ic->ic_state != IEEE80211_S_RUN ||
            (ic->ic_flags & IEEE80211_F_BGSCAN) != 0 ||
            ic->ic_mgt_timer != 0 ||
            ((ic->ic_flags & IEEE80211_F_RSNON) != 0 &&
             (ic->ic_bss == NULL || !ic->ic_bss->ni_port_valid)))
            return kIOReturnBusy;
    } else if (ic->ic_state != IEEE80211_S_SCAN) {
        return kIOReturnBusy;
    }

    error = iwn_scan_start(&com, IEEE80211_CHAN_2GHZ, background ? 1 : 0,
                           IWN_SCAN_LEASE_STANDARD_CONTROLLER, generation,
                           0,
                           &backend_generation, false);
    *outBackendGeneration = backend_generation;
    if (error == 0)
        return backend_generation != 0 ? kIOReturnSuccess : kIOReturnAborted;
    if (error == EBUSY || error == ECANCELED)
        return kIOReturnBusy;
    if (error == ENETDOWN)
        return kIOReturnNotReady;
    return kIOReturnError;
}

IOReturn ItlIwn::
abortWclBackgroundScan(uint64_t generation)
{
    u_int64_t serial = 0;
    bool submit_abort = false;

    if (generation == 0)
        return kIOReturnBadArgument;
    if (!iwn_scan_lease_mark_abort(&com, IWN_SCAN_LEASE_WCL_BACKGROUND,
                                   generation, &serial, &submit_abort))
        return kIOReturnNotReady;
    if (!submit_abort)
        return kIOReturnSuccess;
    if (iwn_cmd(&com, IWN_CMD_SCAN_ABORT, NULL, 0, 1) == 0)
        return kIOReturnSuccess;
    iwn_scan_lease_abort_submission_failed(&com, serial);
    return kIOReturnError;
}

void ItlIwn::
invalidateWclBackgroundScan()
{
    if (com.sc_scan_lease_lock == NULL)
        return;
    IOSimpleLockLock(com.sc_scan_lease_lock);
    if (iwn_scan_lease_live_locked(&com) &&
        iwn_scan_lease_owner_is_wcl(com.sc_scan_lease.owner))
        com.sc_scan_lease.publication_invalidated = true;
    if (com.sc_wcl_initial_scan_pending.queued)
        iwn_wcl_initial_scan_pending_clear_locked(&com);
    IOSimpleLockUnlock(com.sc_scan_lease_lock);
}

int ItlIwn::
iwn_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
    return iwn_newstate_impl(ic, nstate, arg, 0);
}

void ItlIwn::
iwn_wcl_join_failure_scan(struct ieee80211com *ic, u_int64_t generation)
{
    struct iwn_softc *sc;
    bool queued = false;

    if (!ieee80211_wcl_join_failure_pending(ic, generation))
        return;
    sc = (struct iwn_softc *)ic->ic_if.if_softc;
    if (sc == NULL || sc->sc_scan_lease_lock == NULL)
        return;
    IOSimpleLockLock(sc->sc_scan_lease_lock);
    if (sc->sc_scan_lease_replay_task_ready &&
        !sc->sc_scan_lease.hardware_invalidated &&
        generation >= sc->sc_wcl_join_cleanup_generation) {
        sc->sc_wcl_join_cleanup_generation = generation;
        queued = true;
    }
    IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    /* Do not revoke an unrelated foreground scan or overwrite its replay
     * intent. Its real terminal will wake this separate cleanup token. */
    if (queued)
        iwn_scan_lease_schedule_replay_task(sc);
}

int ItlIwn::
iwn_newstate_impl(struct ieee80211com *ic, enum ieee80211_state nstate, int arg,
                  u_int64_t join_failure_generation)
{
    struct _ifnet *ifp = &ic->ic_if;
    struct iwn_softc *sc = (struct iwn_softc *)ifp->if_softc;
    struct ieee80211_node *ni = ic->ic_bss;
    ItlIwn *that = container_of(sc, ItlIwn, com);
    const u_int64_t roam_epoch = ieee80211_pae_assoc_epoch_current(ic);
    u_int64_t direct_sae_scan_generation = 0;
    const bool scan_hop = nstate == IEEE80211_S_SCAN &&
        ic->ic_state == IEEE80211_S_SCAN &&
        arg == IEEE80211_NEWSTATE_ARG_SCAN_HOP;
    const bool wnm_reconnect_hold = nstate == IEEE80211_S_SCAN &&
        ic->ic_state == IEEE80211_S_RUN &&
        arg == IEEE80211_NEWSTATE_ARG_WNM_RECONNECT_HOLD;
    int error;

    if (join_failure_generation != 0 &&
        (nstate != IEEE80211_S_SCAN ||
         !ieee80211_wcl_join_failure_pending(ic, join_failure_generation)))
        return ECANCELED;

    /* The tagged net80211 channel hop reaches this exact callback so its
     * transient current-BSS cleanup can avoid a duplicate epoch cancellation.
     * No lower IWN or generic net80211 callback may observe the private tag. */
    arg = IEEE80211_NEWSTATE_BACKEND_ARG(nstate, arg);

    /* Most callers pass through ieee80211_new_state(), whose preflight has
     * already consumed a conflicting RUN->SCAN request before epoch change.
     * Keep the same fence for the few raw backend callers. */
    if (join_failure_generation == 0 &&
        nstate == IEEE80211_S_SCAN && ic->ic_state == IEEE80211_S_RUN &&
        iwn_newstate_preflight(ic, nstate, arg) != 0)
        return 0;
    if (join_failure_generation == 0 && nstate == IEEE80211_S_SCAN &&
        iwn_wcl_initial_scan_pending_blocks_generic(sc))
        return 0;

    if (nstate == IEEE80211_S_SCAN) {
        AirportItlwmPostPltiTraceRecord(
            ic, kAirportItlwmPostPltiTraceEventIwnScanStateEntered);
        /* A direct request remains HOLD-only until this raw state call has
         * accepted a fresh IWN scan.  The copied generation is public and
         * lets the coalesce branch reject only that exact request. */
        if (join_failure_generation == 0)
            (void)ieee80211_sae_wcl_request_scan_starting(ic,
                &direct_sae_scan_generation);
    }

    if (ic->ic_state == IEEE80211_S_RUN) {
        if (nstate == IEEE80211_S_SCAN) {
            /*
             * During RUN->SCAN we don't call sc_newstate() so
             * we must stop A-MPDU Tx ourselves in this case.
             */
            ieee80211_stop_ampdu_tx(ic, ni, -1);
            ieee80211_ba_del(ni);
        }
        timeout_del(&sc->calib_to);
        sc->calib.state = IWN_CALIB_STATE_INIT;
        if (sc->sc_flags & IWN_FLAG_BGSCAN)
            that->iwn_scan_abort(sc);
    }

    if (ic->ic_state == IEEE80211_S_SCAN) {
        if (nstate == IEEE80211_S_SCAN) {
            if (sc->sc_flags & IWN_FLAG_SCANNING) {
                if (join_failure_generation != 0) {
                    iwn_wcl_join_failure_scan(ic, join_failure_generation);
                    return EAGAIN;
                }
                AirportItlwmPostPltiTraceRecord(
                    ic, kAirportItlwmPostPltiTraceEventIwnScanCoalesced);
                /* Ordinary SCAN -> SCAN stays coalesced, but direct SAE must
                 * never select from the pre-existing scan census.  Preserve
                 * the exact request behind that command, abort the old lease,
                 * and let its terminal replay one fresh direct scan.  The
                 * reference accepts a replacement JoinAdapter carrier here;
                 * EAGAIN is therefore an internal deferred-start result, not
                 * a NotReady result for WCL. */
                if (direct_sae_scan_generation != 0) {
                    u_int64_t serial = 0;
                    bool submit_abort = false;
                    if (!ieee80211_sae_wcl_request_scan_deferred(
                        ic, direct_sae_scan_generation) ||
                        !iwn_scan_lease_defer_scan(
                        sc, IEEE80211_S_SCAN, -1,
                        direct_sae_scan_generation, &serial,
                        &submit_abort))
                        return ECANCELED;
                    if (submit_abort && that->iwn_cmd(
                        sc, IWN_CMD_SCAN_ABORT, NULL, 0, 1) != 0) {
                        iwn_scan_lease_abort_submission_failed(sc, serial);
                        iwn_scan_lease_drop_replay(sc);
                        sc->sc_flags |= IWN_FLAG_FATAL_RECOVERY;
                        (void)task_add(systq, &sc->init_task);
                        return ECANCELED;
                    }
                    XYLog("wcl_assoc CACHED_CANDIDATE_REFRESH_DEFERRED\n");
                    return EAGAIN;
                }
                return 0;
            }
        } else
            sc->sc_flags &= ~IWN_FLAG_SCANNING;
        /* Turn LED off when leaving scan state. */
        that->iwn_set_led(sc, IWN_LED_LINK, 1, 0);
    }

    if (ic->ic_state >= IEEE80211_S_ASSOC &&
        nstate <= IEEE80211_S_ASSOC) {
        const bool authWillCommitRxon =
            that->apFirmwareTransitionActive &&
            that->apFirmwareStage == IWN_AP_STAGE_RUNNING &&
            (nstate == IEEE80211_S_AUTH ||
             (nstate == IEEE80211_S_ASSOC &&
              ic->ic_state == IEEE80211_S_RUN));
        /* Reset state to handle re- and disassociations. */
        iwn_clear_apple_nrate_cache(sc);
        sc->rxon.associd = 0;
        sc->rxon.filter &= ~htole32(IWN_FILTER_BSS);
        /*
         * Keep the APSTA scheduler's logical association carrier in lockstep
         * with the BSS RXON that it describes.  Linux DVM derives PAN policy
         * from vif->cfg.assoc after clearing RXON_FILTER_ASSOC_MSK; retaining
         * a separate true value here made a completed reconnect scan restore
         * the steady two-associated 50/50 split even though the primary BSS
         * had already been removed from firmware.  iwn_auth() also clears
         * this flag, but that is too late for a scan with no immediately
         * selected candidate.
         */
        if (that->apFirmwareTransitionActive &&
            that->apFirmwareStage == IWN_AP_STAGE_RUNNING)
            that->apStaBssAssociated = false;
        /* Do not leak PMF's no-decrypt RXON policy into the next ordinary
         * association, whose pairwise CCMP key remains firmware-owned. */
        sc->rxon.filter &= ~htole32(IWN_FILTER_NODECRYPT);
        sc->rxon.flags &= ~htole32(IWN_RXON_HT_CHANMODE_MIXED2040 |
                                   IWN_RXON_HT_CHANMODE_PURE40 | IWN_RXON_HT_HT40MINUS);
        sc->calib.state = IWN_CALIB_STATE_INIT;
        sc->agg_queue_mask = 0;
        if (!authWillCommitRxon) {
            error = that->iwn_cmd(
                sc, IWN_CMD_RXON, &sc->rxon, sc->rxonsz, 1);
            if (error != 0)
                XYLog("%s: RXON command failed\n",
                    sc->sc_dev.dv_xname);
        } else {
            /* iwlagn_commit_rxon() owns one unassociated RXON transaction.
             * The generic net80211 reset above and iwn_auth() historically
             * emitted two back-to-back full BSS RXON commands.  With a live
             * PAN context, the first one invalidates outstanding primary
             * descriptors without rebuilding its broadcast station or PAN
             * scheduler; a later reconnect then wedges an ordinary AC queue.
             * Keep the logical reset, but let iwn_auth() commit the candidate
             * RXON and immediately follow it with broadcast/PAN restoration. */
            XYLog("%s: APSTA auth coalesced duplicate reset RXON\n",
                  sc->sc_dev.dv_xname);
        }
    }

    if (join_failure_generation != 0 &&
        !ieee80211_wcl_join_failure_pending(ic, join_failure_generation))
        return ECANCELED;

    switch (nstate) {
    case IEEE80211_S_SCAN:
    {
        /* Make the link LED blink while we're scanning. */
        that->iwn_set_led(sc, IWN_LED_LINK, 10, 10);

        if ((sc->sc_flags & IWN_FLAG_BGSCAN) == 0) {
            ieee80211_set_link_state(ic, LINK_STATE_DOWN);
            if (scan_hop)
                ieee80211_node_cleanup_scan_hop(ic, ic->ic_bss);
            else if (direct_sae_scan_generation != 0) {
                if (!ieee80211_node_cleanup_sae_wcl_scan_starting(
                    ic, ic->ic_bss, direct_sae_scan_generation))
                    return EAGAIN;
            }
            else
                ieee80211_node_cleanup(ic, ic->ic_bss);
        }
        if (join_failure_generation != 0 &&
            !ieee80211_wcl_join_failure_pending(ic, join_failure_generation))
            return ECANCELED;
        ic->ic_state = nstate;
        if (join_failure_generation != 0) {
            /* The next JoinAdapter candidate owns its own scan. This edge
             * only retires the failed lower association and must not start
             * an autonomous replacement before its terminal is delivered. */
            ieee80211_wcl_join_cleanup_done(ic, join_failure_generation,
                IEEE80211_JOIN_CLEANUP_LOWER);
            return 0;
        }
        if (wnm_reconnect_hold) {
            /* The protected BTM census retained one freshly confirmed node.
             * Enter SCAN so WCL can restage its SAE credential, but do not
             * replace that evidence with a second all-channel command. */
            that->iwn_set_led(sc, IWN_LED_LINK, 1, 0);
            return 0;
        }
        if ((error = that->iwn_scan(sc, IEEE80211_CHAN_2GHZ, 0,
            direct_sae_scan_generation)) != 0) {
            printf("%s: could not initiate scan\n",
                sc->sc_dev.dv_xname);
        } else if (direct_sae_scan_generation != 0 &&
            ((sc->sc_flags & IWN_FLAG_SCANNING) == 0 ||
            !ieee80211_sae_wcl_request_scan_started(ic,
            direct_sae_scan_generation))) {
            /* A completion/cancellation that wins before promotion cannot
             * borrow this scan for SAE.  Report retry so generic code clears
             * the staged credential rather than binding a stale BSS. */
            error = EAGAIN;
        }
        return error;
    }

    case IEEE80211_S_ASSOC:
        AirportItlwmPostPltiTraceRecord(
            ic, kAirportItlwmPostPltiTraceEventAssocStateEntered);
        if (ic->ic_state != IEEE80211_S_RUN)
            break;
        /* FALLTHROUGH */
    case IEEE80211_S_AUTH:
        if (nstate == IEEE80211_S_AUTH) {
            AirportItlwmPostPltiTraceRecord(
                ic, kAirportItlwmPostPltiTraceEventAuthStateEntered);
        }
        if ((error = that->iwn_auth(sc, arg)) != 0) {
            XYLog("%s: could not move to auth state\n",
                sc->sc_dev.dv_xname);
            ieee80211_roam_link_failed(ic, roam_epoch);
            return error;
        }
        break;

    case IEEE80211_S_RUN:
        if ((error = that->iwn_run(sc)) != 0) {
            XYLog("%s: could not move to run state\n",
                sc->sc_dev.dv_xname);
            ieee80211_roam_link_failed(ic, roam_epoch);
            return error;
        }
        AirportItlwmPostPltiTraceRecord(
            ic, kAirportItlwmPostPltiTraceEventRunEntered);
        break;

    case IEEE80211_S_INIT:
        sc->calib.state = IWN_CALIB_STATE_INIT;
        break;
    }

    return sc->sc_newstate(ic, nstate, arg);
}

void ItlIwn::
iwn_iter_func(void *arg, struct ieee80211_node *ni)
{
    struct iwn_softc *sc = (struct iwn_softc*)arg;
    struct iwn_node *wn = (struct iwn_node*)ni;
    ItlIwn *that = container_of(sc, ItlIwn, com);

    if ((ni->ni_flags & IEEE80211_NODE_HT) == 0) {
        int old_txrate = ni->ni_txrate;
        ieee80211_amrr_choose(&sc->amrr, ni, &wn->amn);
        if (old_txrate != ni->ni_txrate)
            that->iwn_set_link_quality(sc, ni);
    }
}

void ItlIwn::
iwn_calib_timeout(void *arg)
{
    struct iwn_softc *sc = (struct iwn_softc *)arg;
    struct ieee80211com *ic = &sc->sc_ic;
    ItlIwn *that = container_of(sc, ItlIwn, com);
    int s;

    s = splnet();
    if (ic->ic_fixed_rate == -1) {
        if (ic->ic_opmode == IEEE80211_M_STA)
            that->iwn_iter_func(sc, ic->ic_bss);
        else
            ieee80211_iterate_nodes(ic, iwn_iter_func, sc);
    }
    /* Force automatic TX power calibration every 60 secs. */
    if (++sc->calib_cnt >= 120) {
        uint32_t flags = 0;

        (void)that->iwn_cmd(sc, IWN_CMD_GET_STATISTICS, &flags,
            sizeof flags, 1);
        sc->calib_cnt = 0;
    }
    splx(s);

    /* Automatic rate control triggered every 500ms. */
    timeout_add_msec(&sc->calib_to, 500);
}

int ItlIwn::
iwn_ccmp_decap(struct iwn_softc *sc, mbuf_t m, struct ieee80211_node *ni)
{
    struct ieee80211com *ic = &sc->sc_ic;
    struct ieee80211_key *k = &ni->ni_pairwise_key;
    struct ieee80211_frame *wh;
    uint64_t pn, *prsc;
    uint8_t *ivp;
    uint8_t tid;
    int hdrlen, hasqos;

    wh = mtod(m, struct ieee80211_frame *);
    hdrlen = ieee80211_get_hdrlen(wh);
    ivp = (uint8_t *)wh + hdrlen;

    /* Check that ExtIV bit is set. */
    if (!(ivp[3] & IEEE80211_WEP_EXTIV)) {
        DPRINTF(("CCMP decap ExtIV not set\n"));
        return 1;
    }
    hasqos = ieee80211_has_qos(wh);
    tid = hasqos ? ieee80211_get_qos(wh) & IEEE80211_QOS_TID : 0;
    prsc = &k->k_rsc[tid];

    /* Extract the 48-bit PN from the CCMP header. */
    pn = (uint64_t)ivp[0]       |
         (uint64_t)ivp[1] <<  8 |
         (uint64_t)ivp[4] << 16 |
         (uint64_t)ivp[5] << 24 |
         (uint64_t)ivp[6] << 32 |
         (uint64_t)ivp[7] << 40;
    if (pn <= *prsc) {
        ic->ic_stats.is_ccmp_replays++;
        return 1;
    }
    /* Last seen packet number is updated in ieee80211_inputm(). */

    /* Strip MIC. IV will be stripped by ieee80211_inputm(). */
    mbuf_adj(m, -IEEE80211_CCMP_MICLEN);
    return 0;
}

/*
 * Process an RX_PHY firmware notification.  This is usually immediately
 * followed by an MPDU_RX_DONE notification.
 */
void ItlIwn::
iwn_rx_phy(struct iwn_softc *sc, struct iwn_rx_desc *desc,
    struct iwn_rx_data *data)
{
    struct iwn_rx_stat *stat = (struct iwn_rx_stat *)(desc + 1);

    bus_dmamap_sync(sc->sc_dmat, data->map, sizeof (*desc),
        sizeof (*stat), BUS_DMASYNC_POSTREAD);

    /* Save RX statistics, they will be used on MPDU_RX_DONE. */
    memcpy(&sc->last_rx_stat, stat, sizeof (*stat));
    sc->last_rx_valid = IWN_LAST_RX_VALID;
    /*
     * The firmware does not send separate RX_PHY
     * notifications for A-MPDU subframes.
     */
    if (stat->flags & htole16(IWN_STAT_FLAG_AGG))
        sc->last_rx_valid |= IWN_LAST_RX_AMPDU;
}

/*
 * Process an RX_DONE (4965AGN only) or MPDU_RX_DONE firmware notification.
 * Each MPDU_RX_DONE notification must be preceded by an RX_PHY one.
 */
void ItlIwn::
iwn_rx_done(struct iwn_softc *sc, struct iwn_rx_desc *desc,
    struct iwn_rx_data *data, struct mbuf_list *ml,
    struct mbuf_list *apMl)
{
    struct iwn_ops *ops = &sc->ops;
    struct ieee80211com *ic = &sc->sc_ic;
    struct _ifnet *ifp = &ic->ic_if;
    struct iwn_rx_ring *ring = &sc->rxq;
    struct ieee80211_frame *wh;
    struct ieee80211_rxinfo rxi;
    struct ieee80211_node *ni;
    mbuf_t m, m1;
    struct iwn_rx_stat *stat;
    caddr_t head;
    uint32_t flags;
    int error, len, rssi;
    uint16_t chan;

    if (desc->type == IWN_MPDU_RX_DONE) {
        /* Check for prior RX_PHY notification. */
        if (!sc->last_rx_valid) {
            DPRINTF(("missing RX_PHY\n"));
            return;
        }
        sc->last_rx_valid &= ~IWN_LAST_RX_VALID;
        stat = &sc->last_rx_stat;
        if ((sc->last_rx_valid & IWN_LAST_RX_AMPDU) &&
            (stat->flags & htole16(IWN_STAT_FLAG_AGG)) == 0) {
            DPRINTF(("missing RX_PHY (expecting A-MPDU)\n"));
            return;
        }
        if ((sc->last_rx_valid & IWN_LAST_RX_AMPDU) == 0 &&
            (stat->flags & htole16(IWN_STAT_FLAG_AGG))) {
            DPRINTF(("missing RX_PHY (unexpected A-MPDU)\n"));
            return;
        }
    } else
        stat = (struct iwn_rx_stat *)(desc + 1);

    bus_dmamap_sync(sc->sc_dmat, data->map, 0, IWN_RBUF_SIZE,
        BUS_DMASYNC_POSTREAD);

    if (stat->cfg_phy_len > IWN_STAT_MAXLEN) {
        XYLog("%s: invalid RX statistic header\n",
            sc->sc_dev.dv_xname);
        return;
    }
    if (desc->type == IWN_MPDU_RX_DONE) {
        struct iwn_rx_mpdu *mpdu = (struct iwn_rx_mpdu *)(desc + 1);
        head = (caddr_t)(mpdu + 1);
        len = letoh16(mpdu->len);
    } else {
        head = (caddr_t)(stat + 1) + stat->cfg_phy_len;
        len = letoh16(stat->len);
    }

    flags = letoh32(*(uint32_t *)(head + len));

    /* Discard frames with a bad FCS early. */
    if ((flags & IWN_RX_NOERROR) != IWN_RX_NOERROR) {
        DPRINTFN(2, ("RX flags error %x\n", flags));
        ifp->netStat->inputErrors++;
        return;
    }
    /* Discard frames that are too short. */
    if (ic->ic_opmode == IEEE80211_M_MONITOR) {
        /* Allow control frames in monitor mode. */
        if (len < sizeof (struct ieee80211_frame_cts)) {
            ic->ic_stats.is_rx_tooshort++;
            ifp->netStat->inputErrors++;
            return;
        }
    } else {
        const uint8_t frameType =
            len >= 1 ? head[0] & IEEE80211_FC0_TYPE_MASK : 0xff;
        const size_t minimumLength =
            frameType == IEEE80211_FC0_TYPE_CTL ?
                sizeof(struct ieee80211_frame_cts) :
                sizeof(*wh);
        if (len < minimumLength) {
            ic->ic_stats.is_rx_tooshort++;
            ifp->netStat->inputErrors++;
            return;
        }
    }
    
    m1 = getController()->allocatePacket(IWN_RBUF_SIZE);
    if (m1 == NULL) {
        XYLog("could not allocate RX mbuf\n");
        ic->ic_stats.is_rx_nombuf++;
        ifp->netStat->inputErrors++;
        return;
    }
    data->map->dm_nsegs = data->map->cursor->getPhysicalSegments(m1, &data->map->dm_segs[0], 1);
    if (data->map->dm_nsegs == 0) {
        XYLog("could not map RX mbuf\n");
        mbuf_freem(m1);
        ifp->netStat->inputErrors++;
        return;
    }
    
//    m1 = MCLGETI(NULL, M_DONTWAIT, NULL, IWN_RBUF_SIZE);
//    if (m1 == NULL) {
//        ic->ic_stats.is_rx_nombuf++;
//        ifp->netStat->inputErrors++;
//        return;
//    }
//    bus_dmamap_unload(sc->sc_dmat, data->map);
//
//    error = bus_dmamap_load(sc->sc_dmat, data->map, mtod(m1, void *),
//        IWN_RBUF_SIZE, NULL, BUS_DMA_NOWAIT | BUS_DMA_READ);
//    if (error != 0) {
//        mbuf_freem(m1);
//
//        /* Try to reload the old mbuf. */
//        error = bus_dmamap_load(sc->sc_dmat, data->map,
//            mtod(data->m, void *), IWN_RBUF_SIZE, NULL,
//            BUS_DMA_NOWAIT | BUS_DMA_READ);
//        if (error != 0) {
//            panic("%s: could not load old RX mbuf",
//                sc->sc_dev.dv_xname);
//        }
//        /* Physical address may have changed. */
//        ring->desc[ring->cur] =
//            htole32(data->map->dm_segs[0].ds_addr >> 8);
//        bus_dmamap_sync(sc->sc_dmat, ring->desc_dma.map,
//            ring->cur * sizeof (uint32_t), sizeof (uint32_t),
//            BUS_DMASYNC_PREWRITE);
//        ifp->netStat->inputErrors++;
//        return;
//    }

    m = data->m;
    data->m = m1;
    /* Update RX descriptor. */
    ring->desc[ring->cur] = htole32(data->map->dm_segs[0].location >> 8);
//    bus_dmamap_sync(sc->sc_dmat, ring->desc_dma.map,
//        ring->cur * sizeof (uint32_t), sizeof (uint32_t),
//        BUS_DMASYNC_PREWRITE);

    /* Finalize mbuf. */
//    m->m_data = pktdata + sizeof(*desc);
//    m->m_pkthdr.len = m->m_len = len;
    mbuf_setdata(m, head, len);
    mbuf_pkthdr_setlen(m, len);
    mbuf_setlen(m, len);

    /*
     * Grab a reference to the source node. Note that control frames are
     * shorter than struct ieee80211_frame but ieee80211_find_rxnode()
     * is being careful about control frames.
     */
    wh = mtod(m, struct ieee80211_frame *);
    if (len < sizeof (*wh) &&
       (wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) != IEEE80211_FC0_TYPE_CTL) {
        ic->ic_stats.is_rx_tooshort++;
        ifp->netStat->inputErrors++;
        mbuf_freem(m);
        return;
    }
    /*
     * APSTA owns a distinct PAN MAC context.  Do not feed frames addressed
     * to that role into the concurrently-live station net80211 state
     * machine.  Open-System Authentication is host-generated on Intel DVM:
     * unlike Broadcom FullMAC, the firmware does not synthesize seq=2.
     */
    if (iwn_handle_ap_probe_req(wh, len)) {
        mbuf_freem(m);
        return;
    }
    if (iwn_handle_ap_sae_auth(wh, len)) {
        mbuf_freem(m);
        return;
    }
    if (iwn_handle_ap_open_auth(wh, len)) {
        mbuf_freem(m);
        return;
    }
    if (iwn_handle_ap_assoc_req(wh, len)) {
        mbuf_freem(m);
        return;
    }
    const bool apHardwareDecrypted =
        (flags & IWN_RX_CIPHER_MASK) == IWN_RX_CIPHER_CCMP &&
        (desc->type == IWN_MPDU_RX_DONE ?
            (flags & (IWN_RX_MPDU_DEC | IWN_RX_MPDU_MIC_OK)) ==
                (IWN_RX_MPDU_DEC | IWN_RX_MPDU_MIC_OK) :
            (flags & IWN_RX_DECRYPT_MASK) == IWN_RX_DECRYPT_OK);
    if (iwn_handle_ap_block_ack(wh, len, apHardwareDecrypted)) {
        mbuf_freem(m);
        return;
    }
    if (iwn_handle_ap_disconnect(wh, len)) {
        mbuf_freem(m);
        return;
    }
    if (iwn_handle_ap_ps_poll(
            reinterpret_cast<const struct ieee80211_frame_pspoll *>(wh),
            len)) {
        mbuf_freem(m);
        return;
    }
    struct ItlApRxBaReady apBaReady;
    struct IwnApClientRuntime *apRxClient =
        iwn_find_ap_client(wh->i_addr2);
    if (apRxClient != NULL)
        iwn_select_ap_client(apRxClient);
    if (apRxClient != NULL && apRxClient->associated &&
        itl_ap_rx_ba_reorder(
            apClientRxBa, apFirmwareConfig.bssid, apClientMac,
            m, len, apHardwareDecrypted, 0, flags, desc->type,
            false, 0, true,
            &apBaReady)) {
        for (size_t index = 0; index < apBaReady.count; index++) {
            struct ItlApRxBaBufferedFrame *frame =
                &apBaReady.frames[index];
            mbuf_t readyPacket;
            while ((readyPacket = frame->packet) != NULL) {
                frame->packet = mbuf_nextpkt(readyPacket);
                mbuf_setnextpkt(readyPacket, NULL);
                (void)iwn_handle_ap_data(
                    readyPacket, mbuf_pkthdr_len(readyPacket), apMl,
                    frame->rxFlags, frame->descriptorType);
                mbuf_freem(readyPacket);
            }
            frame->packetTail = NULL;
        }
        return;
    }
    if (iwn_handle_ap_data(m, len, apMl, flags, desc->type)) {
        mbuf_freem(m);
        return;
    }
    if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) ==
        IEEE80211_FC0_TYPE_MGT) {
        const uint8_t subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;
        if (subtype == IEEE80211_FC0_SUBTYPE_AUTH) {
            AirportItlwmPostPltiTraceRecord(
                ic, kAirportItlwmPostPltiTraceEventAuthRxFromFirmware);
        } else if (subtype == IEEE80211_FC0_SUBTYPE_ASSOC_RESP ||
                   subtype == IEEE80211_FC0_SUBTYPE_REASSOC_RESP) {
            AirportItlwmPostPltiTraceRecord(
                ic, kAirportItlwmPostPltiTraceEventAssocRxFromFirmware);
        }
    }
    ni = ieee80211_find_rxnode(ic, wh);

    memset(&rxi, 0, sizeof(rxi));
    if (((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) != IEEE80211_FC0_TYPE_CTL)
        && (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) &&
        !IEEE80211_IS_MULTICAST(wh->i_addr1) &&
        (ni->ni_flags & IEEE80211_NODE_RXPROT) &&
        ni->ni_pairwise_key.k_cipher == IEEE80211_CIPHER_CCMP &&
        (ni->ni_pairwise_key.k_flags & IEEE80211_KEY_SWCRYPTO) == 0 &&
        (ni->ni_flags & IEEE80211_NODE_MFP) == 0) {
        if ((flags & IWN_RX_CIPHER_MASK) != IWN_RX_CIPHER_CCMP) {
            ic->ic_stats.is_ccmp_dec_errs++;
            ifp->netStat->inputErrors++;
            mbuf_freem(m);
            ieee80211_release_node(ic, ni);
            return;
        }
        /* Check whether decryption was successful or not. */
        if ((desc->type == IWN_MPDU_RX_DONE &&
             (flags & (IWN_RX_MPDU_DEC | IWN_RX_MPDU_MIC_OK)) !=
              (IWN_RX_MPDU_DEC | IWN_RX_MPDU_MIC_OK)) ||
            (desc->type != IWN_MPDU_RX_DONE &&
             (flags & IWN_RX_DECRYPT_MASK) != IWN_RX_DECRYPT_OK)) {
            DPRINTF(("CCMP decryption failed 0x%x\n", flags));
            ic->ic_stats.is_ccmp_dec_errs++;
            ifp->netStat->inputErrors++;
            mbuf_freem(m);
            ieee80211_release_node(ic, ni);
            return;
        }
        if (iwn_ccmp_decap(sc, m, ni) != 0) {
            ifp->netStat->inputErrors++;
            mbuf_freem(m);
            ieee80211_release_node(ic, ni);
            return;
        }
        rxi.rxi_flags |= IEEE80211_RXI_HWDEC;
    }

    rssi = ops->get_rssi(stat);
    rssi = (0 - IWN_MIN_DBM) + rssi;    /* normalize */
    rssi = MIN(rssi, ic->ic_max_rssi);    /* clip to max. 100% */

    chan = stat->chan;
    if (chan > IEEE80211_CHAN_MAX)
        chan = IEEE80211_CHAN_MAX;

#if NBPFILTER > 0
    if (sc->sc_drvbpf != NULL) {
        struct iwn_rx_radiotap_header *tap = &sc->sc_rxtap;
        uint16_t chan_flags;

        tap->wr_flags = 0;
        if (stat->flags & htole16(IWN_STAT_FLAG_SHPREAMBLE))
            tap->wr_flags |= IEEE80211_RADIOTAP_F_SHORTPRE;
        tap->wr_chan_freq = htole16(ic->ic_channels[chan].ic_freq);
        chan_flags = ic->ic_channels[chan].ic_flags;
        if (ic->ic_curmode != IEEE80211_MODE_11N)
            chan_flags &= ~IEEE80211_CHAN_HT;
        tap->wr_chan_flags = htole16(chan_flags);
        tap->wr_dbm_antsignal = (int8_t)rssi;
        tap->wr_dbm_antnoise = (int8_t)sc->noise;
        tap->wr_tsft = stat->tstamp;
        if (stat->rflags & IWN_RFLAG_MCS) {
            tap->wr_rate = (0x80 | stat->rate); /* HT MCS index */
        } else {
            switch (stat->rate) {
            /* CCK rates. */
            case  10: tap->wr_rate =   2; break;
            case  20: tap->wr_rate =   4; break;
            case  55: tap->wr_rate =  11; break;
            case 110: tap->wr_rate =  22; break;
            /* OFDM rates. */
            case 0xd: tap->wr_rate =  12; break;
            case 0xf: tap->wr_rate =  18; break;
            case 0x5: tap->wr_rate =  24; break;
            case 0x7: tap->wr_rate =  36; break;
            case 0x9: tap->wr_rate =  48; break;
            case 0xb: tap->wr_rate =  72; break;
            case 0x1: tap->wr_rate =  96; break;
            case 0x3: tap->wr_rate = 108; break;
            /* Unknown rate: should not happen. */
            default:  tap->wr_rate =  0;
            }
        }

        bpf_mtap_hdr(sc->sc_drvbpf, tap, sc->sc_rxtap_len,
            m, BPF_DIRECTION_IN);
    }
#endif

    /* Send the frame to the 802.11 layer. */
    rxi.rxi_rssi = rssi;
    rxi.rxi_chan = chan;

    /* Diagnostic probe (auth-ACK boundary, iwn HAL): log MGT
     * RX frames at firmware-to-host delivery time. Filter to
     * AUTH / ASSOC_RESP / REASSOC_RESP / DEAUTH / DISASSOC so
     * beacons and probe responses do not flood oslog; auth-ACK
     * Case A-F discrimination only needs the AP-originated
     * management frames listed above. Behavior-neutral:
     * read-only inspection of the existing RX mbuf. */
    {
        uint8_t rxfc0 = wh->i_fc[0];
        if ((rxfc0 & IEEE80211_FC0_TYPE_MASK) ==
            IEEE80211_FC0_TYPE_MGT) {
            uint8_t rxsub = rxfc0 & IEEE80211_FC0_SUBTYPE_MASK;
            if (rxsub == IEEE80211_FC0_SUBTYPE_AUTH ||
                rxsub == IEEE80211_FC0_SUBTYPE_ASSOC_RESP ||
                rxsub == IEEE80211_FC0_SUBTYPE_REASSOC_RESP ||
                rxsub == IEEE80211_FC0_SUBTYPE_DEAUTH ||
                rxsub == IEEE80211_FC0_SUBTYPE_DISASSOC) {
                IWX_AUTH_DIAG("iwn_rx_done: MGT subtype=0x%02x "
                      "from=%02x:%02x:%02x:%02x:%02x:%02x "
                      "rssi=%d len=%d chan=%u\n",
                      rxsub,
                      ((const u_int8_t *)wh->i_addr2)[0],
                      ((const u_int8_t *)wh->i_addr2)[1],
                      ((const u_int8_t *)wh->i_addr2)[2],
                      ((const u_int8_t *)wh->i_addr2)[3],
                      ((const u_int8_t *)wh->i_addr2)[4],
                      ((const u_int8_t *)wh->i_addr2)[5],
                      rssi, len, (unsigned)chan);
            }
        }
    }
    ieee80211_inputm(ifp, m, ni, &rxi, ml);

    /* Node is no longer needed. */
    ieee80211_release_node(ic, ni);
}

void ItlIwn::
iwn_ra_choose(struct iwn_softc *sc, struct ieee80211_node *ni)
{
    struct ieee80211com *ic = &sc->sc_ic;
    struct iwn_node *wn = (struct iwn_node *)ni;
    int old_txmcs = ni->ni_txmcs;

    ieee80211_ra_choose(&wn->rn, ic, ni);

    /* Update firmware's LQ retry table if RA has chosen a new MCS. */
    if (ni->ni_txmcs != old_txmcs)
        iwn_set_link_quality(sc, ni);
}

void ItlIwn::
iwn_ampdu_rate_control(struct iwn_softc *sc, struct ieee80211_node *ni,
    struct iwn_tx_ring *txq, uint16_t seq, uint16_t ssn)
{
    struct ieee80211com *ic = &sc->sc_ic;
    struct iwn_node *wn = (struct iwn_node *)ni;
    int idx, end_idx;

    /*
     * Update Tx rate statistics for A-MPDUs before firmware's BA window.
     */
    idx = IWN_AGG_SSN_TO_TXQ_IDX(seq);
    end_idx = IWN_AGG_SSN_TO_TXQ_IDX(ssn);
    while (idx != end_idx) {
        struct iwn_tx_data *txdata = &txq->data[idx];
        if (txdata->m != NULL && txdata->ampdu_nframes > 1) {
            if (txdata->tx_apple_nrate_valid)
                iwn_publish_apple_nrate(sc, txdata->tx_apple_nrate);
            /*
             * We can assume that this subframe has been ACKed
             * because ACK failures come as single frames and
             * before failing an A-MPDU subframe the firmware
             * sends it as a single frame at least once.
             */
            ieee80211_ra_add_stats_ht(&wn->rn, ic, ni,
                                      txdata->ampdu_txmcs, 1, 0);
            
            /* Report this frame only once. */
            txdata->ampdu_nframes = 0;
        }
        
        idx = (idx + 1) % IWN_TX_RING_COUNT;
    }
    
    iwn_ra_choose(sc, ni);
}

void ItlIwn::
iwn_ht_single_rate_control(struct iwn_softc *sc, struct ieee80211_node *ni,
    uint8_t rate, uint8_t rflags, uint8_t ackfailcnt, int txfail)
{
    struct ieee80211com *ic = &sc->sc_ic;
    struct iwn_node *wn = (struct iwn_node *)ni;
    int mcs = rate;
    const struct ieee80211_ra_rate *rs;
    unsigned int retries = 0, i;
    
    /*
     * Ignore Tx reports which don't match our last LQ command.
     */
    if (rate != ni->ni_txmcs) {
        if (++wn->lq_rate_mismatch > 15) {
            /* Try to sync firmware with driver. */
            iwn_set_link_quality(sc, ni);
            wn->lq_rate_mismatch = 0;
        }
        return;
    }
    
    wn->lq_rate_mismatch = 0;
    
    rs = ieee80211_ra_get_rateset(&wn->rn, ic, ni, rate);
    /*
     * Firmware has attempted rates in this rate set in sequence.
     * Retries at a basic rate are counted against the minimum MCS.
     */
    for (i = 0; i < ackfailcnt; i++) {
        if (mcs > rs->min_mcs) {
            ieee80211_ra_add_stats_ht(&wn->rn, ic, ni, mcs, 1, 1);
            mcs--;
        } else
            retries++;
    }
    
    if (txfail && ackfailcnt == 0)
        ieee80211_ra_add_stats_ht(&wn->rn, ic, ni, mcs, 1, 1);
    else
        ieee80211_ra_add_stats_ht(&wn->rn, ic, ni, mcs, retries + 1, retries);
    
    iwn_ra_choose(sc, ni);
}

/*
 * Process an incoming Compressed BlockAck.
 * Note that these block ack notifications are generated by firmware and do
 * not necessarily correspond to contents of block ack frames seen on the air.
 */
void ItlIwn::
iwn_rx_compressed_ba(struct iwn_softc *sc, struct iwn_rx_desc *desc,
    struct iwn_rx_data *data)
{
    struct iwn_compressed_ba *cba = (struct iwn_compressed_ba *)(desc + 1);
    struct ieee80211com *ic = &sc->sc_ic;
    struct ieee80211_node *ni;
    struct ieee80211_tx_ba *ba;
    struct iwn_tx_ring *txq;
    uint16_t seq, ssn;
    int qid;

    struct IwnApClientRuntime *apClient =
        iwn_find_ap_client(cba->macaddr);
    if (apClient != NULL)
        iwn_select_ap_client(apClient);
    const bool apRunning = apFirmwareTransitionActive &&
        apFirmwareStage == IWN_AP_STAGE_RUNNING &&
        apClient != NULL && apClient->associated;
    if (ic->ic_state != IEEE80211_S_RUN && !apRunning)
        return;

    bus_dmamap_sync(sc->sc_dmat, data->map, sizeof (*desc), sizeof (*cba),
        BUS_DMASYNC_POSTREAD);

    qid = le16toh(cba->qid);
    if (apRunning &&
        cba->tid < IWN_NUM_AMPDU_TID &&
        (apClientTxBaMask & (1U << cba->tid)) != 0 &&
        IEEE80211_ADDR_EQ(apClientMac, cba->macaddr) &&
        qid == apClientTxBaQueue[cba->tid] && qid < sc->ntxqs) {
        txq = &sc->txq[qid];
        ssn = le16toh(cba->ssn);
        if (!itl_ap_tx_ba_accept_completion(
                &apClientTxBa[cba->tid], ssn,
                static_cast<uint16_t>(txq->queued)) ||
            !iwn_ampdu_txq_can_advance(
                txq, IWN_AGG_SSN_TO_TXQ_IDX(ssn))) {
            static uint32_t staleApBaCount = 0;
            if (++staleApBaCount <= 32) {
                XYLog("%s: IWN AP stale compressed BA tid=%u qid=%d "
                      "ssn=%u winstart=%u queued=%u cur=%u read=%u\n",
                      sc->sc_dev.dv_xname,
                      static_cast<unsigned>(cba->tid), qid,
                      static_cast<unsigned>(ssn & 0x0fff),
                      static_cast<unsigned>(
                          apClientTxBa[cba->tid].winstart),
                      static_cast<unsigned>(txq->queued),
                      static_cast<unsigned>(txq->cur),
                      static_cast<unsigned>(txq->read));
            }
            iwn_refresh_tx_timer(sc);
            return;
        }
        struct IwnApAggregateRateFeedback feedback =
            apClient->rateControl.pendingAggregate[cba->tid];
        apClient->rateControl.pendingAggregate[cba->tid].valid = false;
        if (!iwn_ampdu_txq_advance(sc, txq, qid,
                                   IWN_AGG_SSN_TO_TXQ_IDX(ssn))) {
            iwn_refresh_tx_timer(sc);
            return;
        }
        itl_ap_tx_ba_set_window_start(
            &apClientTxBa[cba->tid], ssn);
        if (feedback.valid) {
            uint16_t attempts = cba->nframes_sent;
            const uint16_t successes = cba->nframes_acked;
            /* DVM treats a bogus acked>sent report as sent=acked so corrupt
             * firmware telemetry cannot manufacture failures. */
            if (successes > attempts)
                attempts = successes;
            if (attempts != 0) {
                const int rateError = iwn_ap_rate_control_feedback(
                    apClient, feedback.mcs, feedback.rflags,
                    attempts, successes, true, feedback.generation);
                if (rateError != 0) {
                    XYLog("%s: IWN AP compressed BA rate feedback "
                          "error=%d\n",
                          sc->sc_dev.dv_xname, rateError);
                }
            }
        }
        iwn_clear_oactive(sc, txq);
        iwn_refresh_tx_timer(sc);
#if __IO80211_TARGET >= __MAC_26_0
        airportItlwmRequestAPTxDequeue(getController());
#endif
        return;
    }

    if (ic->ic_state != IEEE80211_S_RUN)
        return;

    if (!IEEE80211_ADDR_EQ(ic->ic_bss->ni_macaddr, cba->macaddr))
        return;

    ni = ic->ic_bss;

    if (qid < sc->first_agg_txq || qid >= sc->ntxqs)
        return;

    txq = &sc->txq[qid];

    /* Protect against a firmware bug where the queue/TID are off. */
    if (qid != sc->first_agg_txq + cba->tid)
        return;

    ba = &ni->ni_tx_ba[cba->tid];
    if (ba->ba_state != IEEE80211_BA_AGREED)
        return;

    /*
     * The first bit in cba->bitmap corresponds to the sequence number
     * stored in the sequence control field cba->seq.
     * Multiple BA notifications in a row may be using this number, with
     * additional bits being set in cba->bitmap. It is unclear how the
     * firmware decides to shift this window forward.
     * We rely on ba->ba_winstart instead.
     */
    seq = le16toh(cba->seq) >> IEEE80211_SEQ_SEQ_SHIFT;

    /*
     * The firmware's new BA window starting sequence number
     * corresponds to the first hole in cba->bitmap, implying
     * that all frames between 'seq' and 'ssn' (non-inclusive)
     * have been acked.
     */
    ssn = le16toh(cba->ssn);
    
    if (SEQ_LT(ssn, ba->ba_winstart))
        return;

    /* Skip rate control if our Tx rate is fixed. */
    if (ic->ic_fixed_mcs == -1)
        iwn_ampdu_rate_control(sc, ni, txq, ba->ba_winstart, ssn);

    /*
     * SSN corresponds to the first (perhaps not yet transmitted) frame
     * in firmware's BA window. Firmware is not going to retransmit any
     * frames before its BA window so mark them all as done.
     */
    ieee80211_output_ba_move_window(ic, ni, cba->tid, ssn);
    iwn_ampdu_txq_advance(sc, txq, qid,
                          IWN_AGG_SSN_TO_TXQ_IDX(ssn));
    iwn_clear_oactive(sc, txq);
    iwn_refresh_tx_timer(sc);
}

/*
 * Process a CALIBRATION_RESULT notification sent by the initialization
 * firmware on response to a CMD_CALIB_CONFIG command (5000 only).
 */
void ItlIwn::
iwn5000_rx_calib_results(struct iwn_softc *sc, struct iwn_rx_desc *desc,
    struct iwn_rx_data *data)
{
    struct iwn_phy_calib *calib = (struct iwn_phy_calib *)(desc + 1);
    int len, idx = -1;

    /* Runtime firmware should not send such a notification. */
    if (sc->sc_flags & IWN_FLAG_CALIB_DONE)
        return;

    len = (letoh32(desc->len) & IWN_RX_DESC_LEN_MASK) - 4;
    bus_dmamap_sync(sc->sc_dmat, data->map, sizeof (*desc), len,
        BUS_DMASYNC_POSTREAD);

    switch (calib->code) {
    case IWN5000_PHY_CALIB_DC:
        if (sc->hw_type == IWN_HW_REV_TYPE_5150 ||
            sc->hw_type == IWN_HW_REV_TYPE_2030 ||
            sc->hw_type == IWN_HW_REV_TYPE_2000 ||
            sc->hw_type == IWN_HW_REV_TYPE_135 ||
            sc->hw_type == IWN_HW_REV_TYPE_105)
            idx = 0;
        break;
    case IWN5000_PHY_CALIB_LO:
        idx = 1;
        break;
    case IWN5000_PHY_CALIB_TX_IQ:
        idx = 2;
        break;
    case IWN5000_PHY_CALIB_TX_IQ_PERIODIC:
        if (sc->hw_type < IWN_HW_REV_TYPE_6000 &&
            sc->hw_type != IWN_HW_REV_TYPE_5150)
            idx = 3;
        break;
    case IWN5000_PHY_CALIB_BASE_BAND:
        idx = 4;
        break;
    }
    if (idx == -1)    /* Ignore other results. */
        return;

    /* Save calibration result. */
    if (sc->calibcmd[idx].buf != NULL)
        ::free(sc->calibcmd[idx].buf);
    sc->calibcmd[idx].buf = (uint8_t *)malloc(len, M_DEVBUF, M_NOWAIT);
    if (sc->calibcmd[idx].buf == NULL) {
        DPRINTF(("not enough memory for calibration result %d\n",
            calib->code));
        return;
    }
    sc->calibcmd[idx].len = len;
    memcpy(sc->calibcmd[idx].buf, calib, len);
}

/*
 * Process an RX_STATISTICS or BEACON_STATISTICS firmware notification.
 * The latter is sent by the firmware after each received beacon.
 */
void ItlIwn::
iwn_rx_statistics(struct iwn_softc *sc, struct iwn_rx_desc *desc,
    struct iwn_rx_data *data)
{
    struct iwn_ops *ops = &sc->ops;
    struct ieee80211com *ic = &sc->sc_ic;
    struct iwn_calib_state *calib = &sc->calib;
    struct iwn_stats *stats = (struct iwn_stats *)(desc + 1);
    int temp;

    /* Ignore statistics received during a scan. */
    if (ic->ic_state != IEEE80211_S_RUN)
        return;

    bus_dmamap_sync(sc->sc_dmat, data->map, sizeof (*desc),
        sizeof (*stats), BUS_DMASYNC_POSTREAD);

    sc->calib_cnt = 0;    /* Reset TX power calibration timeout. */
    
    sc->rx_stats_flags = htole32(stats->flags);

    /* Test if temperature has changed. */
    if (stats->general.temp != sc->rawtemp) {
        /* Convert "raw" temperature to degC. */
        sc->rawtemp = stats->general.temp;
        temp = ops->get_temperature(sc);
        /* Update TX power if need be (4965AGN only). */
        if (sc->hw_type == IWN_HW_REV_TYPE_4965)
            iwn4965_power_calibration(sc, temp);
    }

    if (desc->type != IWN_BEACON_STATISTICS)
        return;    /* Reply to a statistics request. */

    sc->lqm_beacon_count++;

    sc->noise = iwn_get_noise(&stats->rx.general);

    /* Test that RSSI and noise are present in stats report. */
    if (sc->noise == -127)
        return;

    if (letoh32(stats->rx.general.flags) != 1) {
        DPRINTF(("received statistics without RSSI\n"));
        return;
    }

    /*
     * XXX Differential gain calibration makes the 6005 firmware
     * crap out, so skip it for now.  This effectively disables
     * sensitivity tuning as well.
     */
    if (sc->hw_type == IWN_HW_REV_TYPE_6005)
        return;

    if (calib->state == IWN_CALIB_STATE_ASSOC)
        iwn_collect_noise(sc, &stats->rx.general);
    else if (calib->state == IWN_CALIB_STATE_RUN)
        iwn_tune_sensitivity(sc, &stats->rx);
}

bool ItlIwn::
iwn_ampdu_txq_can_advance(const struct iwn_tx_ring *txq, int idx) const
{
    if (txq == NULL)
        return false;
    idx &= IWN_TX_RING_COUNT - 1;
    if (txq->read == idx)
        return true;

    /* Intel's iwl_txq_reclaim() never trusts a firmware SSN by itself.  It
     * proves that the last descriptor before the exclusive target is still
     * between read_ptr and write_ptr.  Without this transport fence, a valid
     * 12-bit BA-window advance whose low byte lies beyond cur can wrap read
     * through descriptors which software never submitted. */
    const int owned =
        (txq->cur - txq->read) & (IWN_TX_RING_COUNT - 1);
    const int lastToFree =
        (idx - 1) & (IWN_TX_RING_COUNT - 1);
    const int lastDistance =
        (lastToFree - txq->read) & (IWN_TX_RING_COUNT - 1);
    return owned != 0 && lastDistance < owned;
}

bool ItlIwn::
iwn_ampdu_txq_advance(struct iwn_softc *sc, struct iwn_tx_ring *txq, int qid,
    int idx)
{
    struct iwn_ops *ops = &sc->ops;

    idx &= IWN_TX_RING_COUNT - 1;
    if (!iwn_ampdu_txq_can_advance(txq, idx)) {
        static uint32_t rejectedReclaimCount = 0;
        if (++rejectedReclaimCount <= 32) {
            XYLog("%s: aggregate reclaim outside owned ring qid=%d "
                  "target=%d queued=%u cur=%u read=%u\n",
                  sc->sc_dev.dv_xname, qid, idx,
                  static_cast<unsigned>(txq->queued),
                  static_cast<unsigned>(txq->cur),
                  static_cast<unsigned>(txq->read));
        }
        return false;
    }

    while (txq->read != idx) {
        struct iwn_tx_data *txdata = &txq->data[txq->read];
        /* Descriptor ownership is independent of mbuf ownership.  In the
         * AP DEST_PS path the mbuf has already moved to the software PS
         * queue, but this physical TFD still belongs to the aggregate ring
         * and must be reset and removed from queued exactly once. */
        const bool descriptorOwned = txdata->m != NULL ||
            txdata->ap_mgmt || txdata->ap_data;
        if (descriptorOwned) {
            const bool transferredApPsMbuf =
                txdata->m == NULL && txdata->ap_data;
            ops->reset_sched(sc, qid, txq->read);
            if (txdata->sae_active) {
                ItlIwn *that = container_of(sc, ItlIwn, com);
                that->iwn_sae_tx_report_terminal(sc, txdata, EIO);
            }
            iwn_tx_done_free_txdata(sc, txdata);
            if (txq->queued > 0)
                txq->queued--;
            if (transferredApPsMbuf) {
                static uint32_t apPsDescriptorReclaimCount = 0;
                if (++apPsDescriptorReclaimCount <= 32) {
                    XYLog("%s: IWN AP DEST_PS descriptor reclaimed "
                          "qid=%d queued=%u cur=%u read=%u\n",
                          sc->sc_dev.dv_xname, qid,
                          static_cast<unsigned>(txq->queued),
                          static_cast<unsigned>(txq->cur),
                          static_cast<unsigned>(txq->read));
                }
            }
        }
        txq->read = (txq->read + 1) % IWN_TX_RING_COUNT;
    }
    return true;
}

/*
 * Handle A-MPDU Tx queue status report.
 * Tx failures come as single frames (perhaps out of order), and before failing
 * an A-MPDU subframe the firmware transmits it as a single frame at least once.
 * Frames successfully transmitted in an A-MPDU are completed when a compressed
 * block ack notification is received.
 */
void ItlIwn::
iwn_ampdu_tx_done(struct iwn_softc *sc, struct iwn_tx_ring *txq,
    struct iwn_rx_desc *desc, uint16_t status, uint8_t ackfailcnt,
    uint8_t rate, uint8_t rflags, int nframes, uint32_t ssn,
    struct iwn_txagg_status *agg_status)
{
    struct ieee80211com *ic = &sc->sc_ic;
    struct _ifnet *ifp = &ic->ic_if;
    struct iwn_tx_data *txdata = &txq->data[desc->idx];
    struct ieee80211_node *ni = txdata->ni;
    int txfail = (status != IWN_TX_STATUS_SUCCESS &&
        status != IWN_TX_STATUS_DIRECT_DONE);
    struct ieee80211_tx_ba *ba;
    uint16_t seq;
    int apTid = -1;
    struct IwnApClientRuntime *apClient = NULL;
    for (size_t index = 0;
         index < kItlApFirmwareMaxClients && apTid < 0; index++) {
        struct IwnApClientRuntime *candidateClient = &apClients[index];
        if (!candidateClient->inUse)
            continue;
        for (uint8_t candidate = 0; candidate < IWN_NUM_AMPDU_TID;
             candidate++) {
            if ((candidateClient->txBaMask & (1U << candidate)) != 0 &&
                candidateClient->txBaQueue[candidate] == desc->qid) {
                apClient = candidateClient;
                apTid = candidate;
                break;
            }
        }
    }
    if (apClient != NULL)
        iwn_select_ap_client(apClient);
    const int tid = apTid >= 0 ? apTid :
        desc->qid - sc->first_agg_txq;
    const bool apAggregate = apTid >= 0 &&
        apFirmwareTransitionActive &&
        apFirmwareStage == IWN_AP_STAGE_RUNNING &&
        apClient != NULL && apClient->associated;

    sc->sc_tx_timer = 0;

    if (ic->ic_state != IEEE80211_S_RUN && !apAggregate)
        return;

    if (nframes > 1) {
        int i;
        bool aggregateRateFeedback = false;
        uint32_t aggregateRateGeneration = 0;

        if (apAggregate && tid >= 0 &&
            tid < IWN_AP_RATE_TID_COUNT) {
            struct IwnApAggregateRateFeedback *pending =
                &apClient->rateControl.pendingAggregate[tid];
            pending->valid = false;
            /* DVM saves tx_resp->rate_n_flags on the RA/TID and consumes it
             * only when the matching compressed BA arrives. */
            if (iwn_ap_rate_feedback_matches(apClient, rate, rflags)) {
                aggregateRateFeedback = true;
                aggregateRateGeneration =
                    apClient->rateControl.generation;
                pending->valid = true;
                pending->mcs = rate;
                pending->rflags = rflags;
                pending->generation = aggregateRateGeneration;
            }
        }
        
        /*
         * Collect information about this A-MPDU.
         */
        for (i = 0; i < nframes; i++) {
            uint8_t qid = agg_status[i].qid;
            uint8_t idx = agg_status[i].idx;
            uint16_t txstatus = (le16toh(agg_status[i].status) &
                                 IWN_AGG_TX_STATUS_MASK);
            
            if (txstatus != IWN_AGG_TX_STATE_TRANSMITTED)
                continue;
            
            if (qid != desc->qid)
                continue;
            
            txdata = &txq->data[idx];
            if (txdata->ni == NULL &&
                !(apAggregate && txdata->ap_data))
                continue;
            
            /* The Tx rate was the same for all subframes. */
            if (iwn_build_ht_apple_nrate(rate, rflags,
                                         &txdata->tx_apple_nrate))
                txdata->tx_apple_nrate_valid = 1;
            txdata->ampdu_txmcs = rate;
            txdata->ampdu_nframes = nframes;
            if (apAggregate && txdata->ap_data) {
                /* Preserve the original aggregate rate and generation on
                 * every descriptor.  A later single-frame completion reports
                 * the fallback PLCP, while DVM also charges one failed sample
                 * to the A-MPDU rate which originally owned this subframe. */
                txdata->ampdu_rate_generation =
                    aggregateRateGeneration;
                txdata->ampdu_rate_rflags = rflags;
                txdata->ampdu_rate_feedback_valid =
                    aggregateRateFeedback ? 1 : 0;
            }
        }
        return;
    }

    if (txdata->ap_data && apAggregate) {
        struct IwnApAggregateRateFeedback *pending =
            tid >= 0 && tid < IWN_AP_RATE_TID_COUNT ?
            &apClient->rateControl.pendingAggregate[tid] : NULL;
        if (pending != NULL)
            pending->valid = false;
        uint32_t feedbackGeneration = 0;
        const bool rateFeedback =
            status != IWN_TX_STATUS_FAIL_DEST_PS &&
            iwn_ap_rate_feedback_matches(apClient, rate, rflags);
        if (rateFeedback)
            feedbackGeneration = apClient->rateControl.generation;
        if (!itl_ap_tx_ba_accept_completion(
                &apClientTxBa[tid], static_cast<uint16_t>(ssn),
                static_cast<uint16_t>(txq->queued)) ||
            !iwn_ampdu_txq_can_advance(
                txq, IWN_AGG_SSN_TO_TXQ_IDX(ssn))) {
            static uint32_t staleApTxCount = 0;
            if (++staleApTxCount <= 32) {
                XYLog("%s: IWN AP stale aggregate completion tid=%d "
                      "status=0x%02x ssn=%u winstart=%u queued=%u "
                      "cur=%u read=%u\n",
                      sc->sc_dev.dv_xname, tid,
                      static_cast<unsigned>(status),
                      static_cast<unsigned>(ssn & 0x0fff),
                      static_cast<unsigned>(apClientTxBa[tid].winstart),
                      static_cast<unsigned>(txq->queued),
                      static_cast<unsigned>(txq->cur),
                      static_cast<unsigned>(txq->read));
            }
            iwn_refresh_tx_timer(sc);
            return;
        }
        const struct iwn_cmd_data *failedTx =
            reinterpret_cast<const struct iwn_cmd_data *>(
                txq->cmd[desc->idx].data);
        const struct ieee80211_frame *failedFrame =
            reinterpret_cast<const struct ieee80211_frame *>(failedTx + 1);
        const uint16_t failedSequence = static_cast<uint16_t>(
            LE_READ_2(failedFrame->i_seq) >> IEEE80211_SEQ_SEQ_SHIFT);
        const uint16_t barSsn = static_cast<uint16_t>(
            (failedSequence + 1) & 0x0fff);
        const bool priorAggregateRateFeedback =
            status != IWN_TX_STATUS_FAIL_DEST_PS &&
            txdata->ampdu_nframes > 1 &&
            txdata->ampdu_rate_feedback_valid != 0;
        const uint8_t priorAggregateMcs =
            static_cast<uint8_t>(txdata->ampdu_txmcs);
        const uint8_t priorAggregateRflags =
            txdata->ampdu_rate_rflags;
        const uint32_t priorAggregateGeneration =
            txdata->ampdu_rate_generation;
        mbuf_t apPsFilteredPacket = NULL;
        if (status == IWN_TX_STATUS_FAIL_DEST_PS && txdata->m != NULL) {
            /* DVM exposes DEST_PS as TX_FILTERED even for an aggregation
             * queue.  mac80211 then returns that same frame behind the
             * station PS gate instead of dropping it.  Transfer the mbuf
             * out of the firmware-owned descriptor before reclaiming the
             * aggregate window and make the AP TIM/PS queue its next owner. */
            apPsFilteredPacket = txdata->m;
            txdata->m = NULL;
            apClientPowerSave = true;
        }
        const int queuedBeforeReclaim = txq->queued;
        if (!iwn_ampdu_txq_advance(
                sc, txq, desc->qid, IWN_AGG_SSN_TO_TXQ_IDX(ssn))) {
            iwn_refresh_tx_timer(sc);
            return;
        }
        itl_ap_tx_ba_set_window_start(
            &apClientTxBa[tid], static_cast<uint16_t>(ssn));
        const bool reclaimedDescriptor =
            txq->queued < queuedBeforeReclaim;
        if (priorAggregateRateFeedback && reclaimedDescriptor) {
            const int aggregateRateError =
                iwn_ap_rate_control_feedback(
                    apClient, priorAggregateMcs,
                    priorAggregateRflags, 1, 0, true,
                    priorAggregateGeneration);
            if (aggregateRateError != 0) {
                XYLog("%s: IWN AP prior aggregate rate feedback "
                      "error=%d\n",
                      sc->sc_dev.dv_xname, aggregateRateError);
            }
        }
        if (txfail && status != IWN_TX_STATUS_FAIL_DEST_PS &&
            reclaimedDescriptor) {
            /* A failed single-frame aggregate completion is retry
             * exhaustion for one MPDU, not a teardown of the peer's RA/TID
             * agreement.  Like DVM/mac80211, reclaim first, then send a BAR
             * for the failed MPDU's sequence + 1.  An empty reclaim produces
             * no TX status in mac80211 and therefore must not emit another
             * BAR for a duplicate firmware completion. */
            const int barError = iwn_send_ap_compressed_bar(
                static_cast<uint8_t>(tid), barSsn);
            static uint32_t apAggregateFailureCount = 0;
            if (++apAggregateFailureCount <= 32) {
                XYLog("%s: IWN AP aggregate TX failed tid=%d "
                      "status=0x%02x firmware_ssn=%u bar_ssn=%u BAR=%d; "
                      "keeping BA session\n",
                      sc->sc_dev.dv_xname, tid,
                      static_cast<unsigned>(status),
                      static_cast<unsigned>(ssn & 0x0fff),
                      static_cast<unsigned>(barSsn), barError);
            }
        }
        if (rateFeedback && reclaimedDescriptor) {
            uint16_t feedbackAttempts = 0;
            uint16_t feedbackSuccesses = 0;
            iwn_ap_dvm_selected_rate_sample(
                ackfailcnt, txfail, &feedbackAttempts,
                &feedbackSuccesses);
            const int rateError = iwn_ap_rate_control_feedback(
                apClient, rate, rflags, feedbackAttempts,
                feedbackSuccesses,
                true, feedbackGeneration);
            if (rateError != 0) {
                XYLog("%s: IWN AP aggregate rate feedback error=%d\n",
                      sc->sc_dev.dv_xname, rateError);
            }
        }
        iwn_clear_oactive(sc, txq);
        iwn_refresh_tx_timer(sc);
        if (apPsFilteredPacket != NULL) {
            const int queueError =
                iwn_queue_ap_ps_packet(apPsFilteredPacket, true);
            if (queueError != 0)
                mbuf_freem(apPsFilteredPacket);
        }
#if __IO80211_TARGET >= __MAC_26_0
        airportItlwmRequestAPTxDequeue(getController());
#endif
        return;
    }

    if (ni == NULL)
        return;

    if (txdata->tx_apple_nrate_valid)
        iwn_publish_apple_nrate(sc, txdata->tx_apple_nrate);
    if (iwn_build_ht_apple_nrate(rate, rflags, &txdata->tx_apple_nrate)) {
        txdata->tx_apple_nrate_valid = 1;
        iwn_publish_apple_nrate(sc, txdata->tx_apple_nrate);
    }

    ba = &ni->ni_tx_ba[tid];
    if (ba->ba_state != IEEE80211_BA_AGREED)
        return;
    if (SEQ_LT(ssn, ba->ba_winstart))
        return;

    /* This was a final single-frame Tx attempt for frame SSN-1. */
    seq = (ssn - 1) & 0xfff;

    /*
     * Skip rate control if our Tx rate is fixed.
     */
    if (ic->ic_fixed_mcs == -1) {
        if (txdata->ampdu_nframes > 1) {
            struct iwn_node *wn = (struct iwn_node *)ni;
            /*
             * This frame was once part of an A-MPDU.
             * Report one failed A-MPDU Tx attempt.
             * The firmware might have made several such
             * attempts but we don't keep track of this.
             */
            ieee80211_ra_add_stats_ht(&wn->rn, ic, ni,
                                      txdata->ampdu_txmcs, 1, 1);
        }
        
        /* Report the final single-frame Tx attempt. */
        if (rflags & IWN_RFLAG_MCS)
            iwn_ht_single_rate_control(sc, ni, rate, rflags,
                                       ackfailcnt, txfail);
    }

    if (txfail)
        ieee80211_tx_compressed_bar(ic, ni, tid, ssn);

    /*
     * SSN corresponds to the first (perhaps not yet transmitted) frame
     * in firmware's BA window. Firmware is not going to retransmit any
     * frames before its BA window so mark them all as done.
     */
    ieee80211_output_ba_move_window(ic, ni, tid, ssn);
    iwn_ampdu_txq_advance(sc, txq, desc->qid, IWN_AGG_SSN_TO_TXQ_IDX(ssn));
    iwn_clear_oactive(sc, txq);
    iwn_refresh_tx_timer(sc);
}

/*
 * Process a TX_DONE firmware notification.  Unfortunately, the 4965AGN
 * and 5000 adapters have different incompatible TX status formats.
 */
void ItlIwn::
iwn4965_tx_done(struct iwn_softc *sc, struct iwn_rx_desc *desc,
    struct iwn_rx_data *data)
{
    ItlIwn *that = container_of(sc, ItlIwn, com);
    struct iwn4965_tx_stat *stat = (struct iwn4965_tx_stat *)(desc + 1);
    struct iwn_tx_ring *ring;
    size_t len = (letoh32(desc->len) & IWN_RX_DESC_LEN_MASK);
    uint16_t status = letoh32(stat->stat.status) & 0xff;
    uint32_t ssn;

    if (desc->qid > IWN4965_NTXQUEUES)
        return;

    ring = &sc->txq[desc->qid];

    bus_dmamap_sync(sc->sc_dmat, data->map, sizeof (*desc),
        len, BUS_DMASYNC_POSTREAD);

    /* Sanity checks. */
    if (sizeof(*stat) > len)
        return;
    if (stat->nframes < 1 || stat->nframes > IWN_AMPDU_MAX)
        return;
    if (desc->qid < sc->first_agg_txq && stat->nframes > 1)
        return;
    if (desc->qid >= sc->first_agg_txq && sizeof(*stat) + sizeof(ssn) +
        stat->nframes * sizeof(stat->stat) > len)
        return;

    if (desc->qid < sc->first_agg_txq) {
        /* XXX 4965 does not report byte count */
        struct iwn_tx_data *txdata = &ring->data[desc->idx];
        uint16_t framelen = txdata->totlen + IEEE80211_CRC_LEN;
        int txfail = (status != IWN_TX_STATUS_SUCCESS &&
                      status != IWN_TX_STATUS_DIRECT_DONE);

        that->iwn_tx_done(sc, desc, stat->ackfailcnt, stat->rate,
                          stat->rflags, txfail, desc->qid, framelen);
    } else {
        memcpy(&ssn, &stat->stat.status + stat->nframes, sizeof(ssn));
        ssn = le32toh(ssn) & 0xfff;
        that->iwn_ampdu_tx_done(sc, ring, desc, status, stat->ackfailcnt,
            stat->rate, stat->rflags, stat->nframes, ssn,
            stat->stat.agg_status);
    }
}

void ItlIwn::
iwn5000_tx_done(struct iwn_softc *sc, struct iwn_rx_desc *desc,
    struct iwn_rx_data *data)
{
    ItlIwn *that = container_of(sc, ItlIwn, com);
    struct iwn5000_tx_stat *stat = (struct iwn5000_tx_stat *)(desc + 1);
    struct iwn_tx_ring *ring;
    size_t len = (letoh32(desc->len) & IWN_RX_DESC_LEN_MASK);
    uint16_t status = letoh32(stat->stat.status) & 0xff;
    uint32_t ssn;

    if (desc->qid > IWN5000_NTXQUEUES)
        return;

    ring = &sc->txq[desc->qid];

    bus_dmamap_sync(sc->sc_dmat, data->map, sizeof (*desc),
        sizeof (*stat), BUS_DMASYNC_POSTREAD);

    /* Sanity checks. */
    if (sizeof(*stat) > len)
        return;
    if (stat->nframes < 1 || stat->nframes > IWN_AMPDU_MAX)
        return;
    if (desc->qid < sc->first_agg_txq && stat->nframes > 1)
        return;
    if (desc->qid >= sc->first_agg_txq && sizeof(*stat) + sizeof(ssn) +
        stat->nframes * sizeof(stat->stat) > len)
        return;

    /* If this was not an aggregated frame, complete it now. */
    if (desc->qid < sc->first_agg_txq) {
        int txfail = (status != IWN_TX_STATUS_SUCCESS &&
                      status != IWN_TX_STATUS_DIRECT_DONE);
        mbuf_t apPsFilteredPacket = NULL;
        struct IwnApClientRuntime *apTxClient =
            that->iwn_find_ap_client(
                ring->data[desc->idx].diag_peer);
        if (status == IWN_TX_STATUS_FAIL_DEST_PS &&
            ring->data[desc->idx].ap_data &&
            ring->data[desc->idx].m != NULL) {
            /*
             * Linux reports DEST_PS as TX_FILTERED so mac80211 puts the
             * same frame back behind the station's PS gate and advertises
             * it through TIM.  Preserve the mbuf across normal completion;
             * the AP PS queue becomes its new owner below.
             */
            apPsFilteredPacket = ring->data[desc->idx].m;
            ring->data[desc->idx].m = NULL;
        }
        if ((ring->data[desc->idx].ap_mgmt ||
             ring->data[desc->idx].ap_data) &&
            status != IWN_TX_STATUS_SUCCESS &&
            status != IWN_TX_STATUS_DIRECT_DONE &&
            status != IWN_TX_STATUS_FAIL_DEST_PS) {
            XYLog("%s: AP %s raw TX status=0x%02x "
                  "qid=%u idx=%u station=%u\n",
                  sc->sc_dev.dv_xname,
                  ring->data[desc->idx].ap_data ? "data" : "management",
                  static_cast<unsigned>(status),
                  static_cast<unsigned>(desc->qid),
                  static_cast<unsigned>(desc->idx),
                  static_cast<unsigned>(
                      apTxClient != NULL ? apTxClient->stationId :
                          IWN5000_ID_PAN_BROADCAST));
        }
        /* DIAGNOSTIC (auth-ACK boundary): capture the firmware TX status
         * for the pending AUTH(seq=1) frame. status==SUCCESS/DIRECT_DONE +
         * ackfailcnt==0 => the AP ACKed our auth frame (so a missing seq=2
         * response is an RX/processing problem, not a TX problem). txfail /
         * high ackfailcnt => the AP never heard/ACKed us. */
        /* NB: iwn_notif_intr clears auth_seq1_tx_pending BEFORE calling
         * ops->tx_done, so we must NOT gate on pending here. Match on the
         * pending qid/idx (set at auth enqueue); safe because we never
         * associate, so queue 0 carries only auth frames in this state. */
        if (((desc->qid & 0xf) == sc->auth_seq1_tx_qid) &&
            desc->idx == sc->auth_seq1_tx_idx &&
            !IEEE80211_ADDR_EQ(sc->auth_seq1_tx_bssid, etheranyaddr)) {
            sc->dbg_auth_txstatus = status;
            sc->dbg_auth_ackfailcnt = stat->ackfailcnt;
            sc->dbg_auth_txrate = stat->rate;
            sc->dbg_auth_txstatus_seen++;
            char auth_txstatus_buf[160];
            snprintf(auth_txstatus_buf, sizeof(auth_txstatus_buf),
                "status=0x%02x txfail=%d ackfailcnt=%u rate=0x%02x "
                "success=%d seen=%u qid=%u idx=%u",
                (unsigned)status, txfail, (unsigned)stat->ackfailcnt,
                (unsigned)stat->rate,
                (int)(status == IWN_TX_STATUS_SUCCESS ||
                      status == IWN_TX_STATUS_DIRECT_DONE),
                (unsigned)sc->dbg_auth_txstatus_seen,
                (unsigned)(desc->qid & 0xf), (unsigned)desc->idx);
            that->getController()->setProperty(
                "itlwm-iwn-auth-txstatus", auth_txstatus_buf);
        }
        /* Reset TX scheduler slot. */
        iwn5000_reset_sched(sc, desc->qid, desc->idx);

        that->iwn_tx_done(
            sc, desc, stat->ackfailcnt, stat->rate, stat->rflags,
            apPsFilteredPacket != NULL ? 0 : txfail,
            desc->qid, letoh16(stat->len));
        if (apPsFilteredPacket != NULL) {
            if (apTxClient == NULL) {
                mbuf_freem(apPsFilteredPacket);
                return;
            }
            that->iwn_select_ap_client(apTxClient);
            that->apClientPowerSave = true;
            const int queueError =
                that->iwn_queue_ap_ps_packet(
                    apPsFilteredPacket, true);
            if (queueError != 0)
                mbuf_freem(apPsFilteredPacket);
        }
    } else {
        memcpy(&ssn, &stat->stat.status + stat->nframes, sizeof(ssn));
        ssn = le32toh(ssn) & 0xfff;
        that->iwn_ampdu_tx_done(sc, ring, desc, status, stat->ackfailcnt,
            stat->rate, stat->rflags, stat->nframes, ssn,
            stat->stat.agg_status);
    }
}

void ItlIwn::
iwn_tx_done_free_txdata(struct iwn_softc *sc, struct iwn_tx_data *data)
{
    struct ieee80211com *ic = &sc->sc_ic;

//    bus_dmamap_sync(sc->sc_dmat, data->map, 0, data->map->dm_mapsize,
//        BUS_DMASYNC_POSTWRITE);
//    bus_dmamap_unload(sc->sc_dmat, data->map);
    if (data->m != NULL)
        mbuf_freem(data->m);
    data->m = NULL;
    if (data->ni != NULL) {
        ieee80211_release_node(ic, data->ni);
        data->ni = NULL;
    } else {
        KASSERT(data->ap_mgmt || data->ap_data,
                "iwn tx data has node or AP owner");
    }
    data->totlen = 0;
    data->txrate = 0;
    data->ampdu_nframes = 0;
    data->ampdu_txmcs = 0;
    data->ampdu_rate_generation = 0;
    data->ampdu_rate_rflags = 0;
    data->ampdu_rate_feedback_valid = 0;
    data->tx_apple_nrate = 0;
    data->tx_apple_nrate_valid = 0;
    data->post_plti_trace_class = IWN_POST_PLTI_TRACE_TX_NONE;
    data->ap_mgmt = false;
    data->ap_data = false;
    iwn_sae_tx_data_clear(data);
}

void ItlIwn::
iwn_clear_oactive(struct iwn_softc *sc, struct iwn_tx_ring *ring)
{
    struct ieee80211com *ic = &sc->sc_ic;
    struct _ifnet *ifp = &ic->ic_if;
    ItlIwn *that = container_of(sc, ItlIwn, com);

    bool apDataQueue = ring->qid == IWN_IPAN_BE_QUEUE ||
        ring->qid == IWN_IPAN_MCAST_QUEUE;
    for (size_t index = 0;
         !apDataQueue && index < kItlApFirmwareMaxClients; index++) {
        const struct IwnApClientRuntime *client =
            &that->apClients[index];
        if (!client->inUse)
            continue;
        for (uint8_t tid = 0;
             !apDataQueue && tid < IWN_NUM_AMPDU_TID; tid++) {
            apDataQueue =
                (client->txBaMask & (1U << tid)) != 0 &&
                client->txBaQueue[tid] == ring->qid;
        }
    }
    const bool apQueueWasFull =
        apDataQueue &&
        (sc->qfullmsk & (1 << ring->qid)) != 0;
    if (ring->queued < IWN_TX_RING_LOMARK) {
        sc->qfullmsk &= ~(1 << ring->qid);
        if (that->apPrimaryTxQuiesced)
            return;
#if __IO80211_TARGET >= __MAC_26_0
        if (apQueueWasFull)
            airportItlwmRequestAPTxDequeue(that->getController());
#endif
        if (sc->qfullmsk == 0 && ifq_is_oactive(&ifp->if_snd)) {
            ifq_clr_oactive(&ifp->if_snd);
            (*ifp->if_start)(ifp);
        }
#if defined(__PRIVATE_SPI__) && __IO80211_TARGET < __MAC_26_0
        ifp->iface->signalOutputThread();
#endif
    }
}

bool ItlIwn::
iwn_tx_pending(struct iwn_softc *sc)
{
    for (int qid = 0; qid < sc->ntxqs; qid++) {
        if (sc->txq[qid].queued > 0)
            return true;
    }
    return false;
}

void ItlIwn::
iwn_refresh_tx_timer(struct iwn_softc *sc)
{
    struct _ifnet *ifp = &sc->sc_ic.ic_if;

    if (iwn_tx_pending(sc)) {
        sc->sc_tx_timer = 5;
        ifp->if_timer = 1;
    } else {
        sc->sc_tx_timer = 0;
    }
}

/*
 * Adapter-independent backend for TX_DONE firmware notifications.
 * This handles Tx status for non-aggregation queues.
 */
void ItlIwn::
iwn_tx_done(struct iwn_softc *sc, struct iwn_rx_desc *desc,
    uint8_t ackfailcnt, uint8_t rate, uint8_t rflags, int txfail,
    int qid, uint16_t len)
{
    ItlIwn *that = container_of(sc, ItlIwn, com);
    struct ieee80211com *ic = &sc->sc_ic;
    struct _ifnet *ifp = &ic->ic_if;
    struct iwn_tx_ring *ring = &sc->txq[qid];
    struct iwn_tx_data *data = &ring->data[desc->idx];
    struct iwn_node *wn = (struct iwn_node *)data->ni;
    struct ieee80211_node *wnm_tx_fence_node = NULL;
    u_int64_t wnm_tx_fence_generation = 0;
    u_int8_t wnm_tx_fence_kind = 0;

    if (data->ap_mgmt || data->ap_data) {
        struct IwnApClientRuntime *client =
            that->iwn_find_ap_client(data->diag_peer);
        if (client != NULL)
            that->iwn_select_ap_client(client);
        const bool beginApFourWay =
            data->ap_mgmt && !txfail &&
            data->diag_subtype == IEEE80211_FC0_SUBTYPE_ASSOC_RESP &&
            that->apFirmwareConfig.rsnIELength != 0 &&
            client != NULL && client->associated &&
            IEEE80211_ADDR_EQ(data->diag_peer, client->mac);
        if (data->ap_data && data->m != NULL && client != NULL &&
            iwn_ap_rate_feedback_matches(client, rate, rflags)) {
            const uint32_t generation =
                client->rateControl.generation;
            uint16_t feedbackAttempts = 0;
            uint16_t feedbackSuccesses = 0;
            iwn_ap_dvm_selected_rate_sample(
                ackfailcnt, txfail, &feedbackAttempts,
                &feedbackSuccesses);
            const int rateError = iwn_ap_rate_control_feedback(
                client, rate, rflags, feedbackAttempts,
                feedbackSuccesses,
                false, generation);
            if (rateError != 0) {
                XYLog("%s: IWN AP nonaggregate rate feedback error=%d\n",
                      sc->sc_dev.dv_xname, rateError);
            }
        }
        if (txfail)
            ifp->netStat->outputErrors++;
        if (txfail)
            XYLog("%s: AP %s TX failed peer="
                  "%02x:%02x:%02x:%02x:%02x:%02x "
                  "ackfailcnt=%u qid=%d\n",
                  sc->sc_dev.dv_xname,
                  data->ap_data ? "data" : "management",
                  data->diag_peer[0], data->diag_peer[1],
                  data->diag_peer[2], data->diag_peer[3],
                  data->diag_peer[4], data->diag_peer[5],
                  static_cast<unsigned>(ackfailcnt), qid);
        if (data->m != NULL)
            mbuf_freem(data->m);
        data->m = NULL;
        data->totlen = 0;
        data->ap_mgmt = false;
        data->ap_data = false;
        data->diag_subtype = 0xff;
        data->diag_auth_seq = 0xffff;
        explicit_bzero(data->diag_peer, sizeof(data->diag_peer));
        ring->queued--;
        iwn_clear_oactive(sc, ring);
        iwn_refresh_tx_timer(sc);
        if (beginApFourWay && client != NULL) {
            that->iwn_select_ap_client(client);
            that->iwn_begin_ap_4way();
        }
        return;
    }

    if (data->ni == NULL) {
        iwn_refresh_tx_timer(sc);
        return;
    }

    if (data->tx_apple_nrate_valid)
        iwn_publish_apple_nrate(sc, data->tx_apple_nrate);
    if (iwn_build_ht_apple_nrate(rate, rflags, &data->tx_apple_nrate)) {
        data->tx_apple_nrate_valid = 1;
        iwn_publish_apple_nrate(sc, data->tx_apple_nrate);
    }

    if (data->ni->ni_flags & IEEE80211_NODE_HT) {
        if (ic->ic_state == IEEE80211_S_RUN &&
            ic->ic_fixed_mcs == -1 && (rflags & IWN_RFLAG_MCS)) {
            iwn_ht_single_rate_control(sc, data->ni, rate, rflags,
                                       ackfailcnt, txfail);
        }
    } else {
        /*
         * Match the OpenBSD iwn AMRR ownership rule.  stat->rate is a
         * firmware PLCP value (and may describe a retry fallback), whereas
         * ni_txrate is an index into ni_rates.  The untested RA conversion
         * compared those different domains and therefore rejected every
         * legacy completion: AMRR never accumulated samples and a 5 GHz STA
         * remained pinned to the initial 6 Mbps rate indefinitely.
         */
        if (data->txrate != data->ni->ni_txrate) {
            if (++wn->lq_rate_mismatch > 15) {
                /* Try to sync firmware with driver. */
                iwn_set_link_quality(sc, data->ni);
                wn->lq_rate_mismatch = 0;
            }
        } else {
            wn->lq_rate_mismatch = 0;
            
            wn->amn.amn_txcnt++;
            if (ackfailcnt > 0)
                wn->amn.amn_retrycnt++;
            if (txfail)
                wn->amn.amn_retrycnt++;
        }
    }
    if (txfail) {
        ifp->netStat->outputErrors++;
    }

    /* Diagnostic probe (auth-ACK boundary, iwn HAL): on MGT
     * TX completion, log the firmware TX_RESP attributes
     * (txfail, retry count, rate, fragment length) together
     * with the per-tx-buffer identity captured before
     * mbuf_adj. The MGT filter uses the diag_subtype
     * sentinel (0xff = not captured) instead of dereferencing
     * data->m as a struct ieee80211_frame, because data->m at
     * completion is the post-trim payload only. */
    if (data->diag_subtype != 0xff) {
        IWX_AUTH_DIAG("iwn_tx_done: MGT subtype=0x%02x "
              "peer=%02x:%02x:%02x:%02x:%02x:%02x "
              "auth_seq=0x%04x txfail=%d ackfailcnt=%d "
              "rate=0x%02x rflags=0x%02x len=%u\n",
              data->diag_subtype,
              data->diag_peer[0], data->diag_peer[1],
              data->diag_peer[2], data->diag_peer[3],
              data->diag_peer[4], data->diag_peer[5],
              (unsigned)data->diag_auth_seq,
              txfail, ackfailcnt, rate, rflags,
              (unsigned)len);
    }

    /* Completion is categorical only; the trace intentionally omits status. */
    iwn_post_plti_trace_record_completion(ic, data->post_plti_trace_class);

    /* Both 4965 and 5000 native status formats funnel here exactly once. */
    if (data->sae_active)
        that->iwn_sae_tx_report_terminal(sc, data, txfail ? EIO : 0);

    if (data->wnm_tx_fence_generation != 0 &&
        data->wnm_tx_fence_kind != 0) {
        wnm_tx_fence_node = data->ni;
        wnm_tx_fence_generation = data->wnm_tx_fence_generation;
        wnm_tx_fence_kind = data->wnm_tx_fence_kind;
    }
    iwn_tx_done_free_txdata(sc, data);
    if (wnm_tx_fence_node != NULL)
        ieee80211_wnm_bss_transition_tx_fence_complete(ic,
            wnm_tx_fence_node, wnm_tx_fence_generation,
            wnm_tx_fence_kind);

    ring->queued--;
    iwn_clear_oactive(sc, ring);
    iwn_refresh_tx_timer(sc);
}

/*
 * Process a "command done" firmware notification.  This is where we wakeup
 * processes waiting for a synchronous command completion.
 */
void ItlIwn::
iwn_cmd_done(struct iwn_softc *sc, struct iwn_rx_desc *desc)
{
    ItlIwn *that = container_of(sc, ItlIwn, com);
    struct iwn_tx_ring *ring = &sc->txq[sc->command_queue];
    struct iwn_tx_data *data;

    if ((desc->qid & 0xf) != sc->command_queue)
        return;    /* Not a command ack. */

    data = &ring->data[desc->idx];

    /* If the command was mapped in an mbuf, free it. */
    if (data->m != NULL) {
//        bus_dmamap_sync(sc->sc_dmat, data->map, 0,
//            data->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
//        bus_dmamap_unload(sc->sc_dmat, data->map);
        mbuf_freem(data->m);
        data->m = NULL;
    }
    that->iwn_clear_cmd_in_flight(sc);
    wakeupOn(&ring->desc[desc->idx]);
}

/*
 * Process an INT_FH_RX or INT_SW_RX interrupt.
 */
void ItlIwn::
iwn_notif_intr(struct iwn_softc *sc)
{
    struct mbuf_list ml = MBUF_LIST_INITIALIZER();
    struct mbuf_list apMl = MBUF_LIST_INITIALIZER();
    struct iwn_ops *ops = &sc->ops;
    struct ieee80211com *ic = &sc->sc_ic;
    struct _ifnet *ifp = &ic->ic_if;
    uint16_t hw;

//    bus_dmamap_sync(sc->sc_dmat, sc->rxq.stat_dma.map,
//        0, sc->rxq.stat_dma.size, BUS_DMASYNC_POSTREAD);

    hw = letoh16(sc->rxq.stat->closed_count) & 0xfff;
    while (sc->rxq.cur != hw) {
        struct iwn_rx_data *data = &sc->rxq.data[sc->rxq.cur];
        struct iwn_rx_desc *desc;

        bus_dmamap_sync(sc->sc_dmat, data->map, 0, sizeof (*desc),
            BUS_DMASYNC_POSTREAD);
        desc = mtod(data->m, struct iwn_rx_desc *);

        if (!(desc->qid & 0x80) &&
            (desc->qid & 0xf) == sc->command_queue &&
            desc->idx < IWN_TX_RING_COUNT) {
            struct iwn_tx_ring *commandRing =
                &sc->txq[sc->command_queue];
            struct iwn_tx_data *commandData =
                &commandRing->data[desc->idx];
            const struct iwn_tx_cmd *completedCommand =
                commandData->m != NULL ?
                mtod(commandData->m, const struct iwn_tx_cmd *) :
                &commandRing->cmd[desc->idx];
            iwn_note_ap_stop_tx_flush(
                completedCommand->code, static_cast<uint16_t>(desc->idx),
                desc->type != IWN_CMD_TXFIFO_FLUSH);
            int completedAddNodeStatus = -1;
            if (apFirmwareTransitionActive &&
                completedCommand->code == IWN_CMD_ADD_NODE) {
                const uint32_t replyLength =
                    letoh32(desc->len) & IWN_RX_DESC_LEN_MASK;
                const uint8_t addNodeStatus =
                    replyLength != 0 ?
                    *(reinterpret_cast<const uint8_t *>(desc + 1)) : 0xff;
                completedAddNodeStatus = addNodeStatus;
                const struct iwn_node_info *completedNode =
                    reinterpret_cast<const struct iwn_node_info *>(
                        completedCommand->data);
                if (apFirmwareStage != IWN_AP_STAGE_RUNNING ||
                    apClientMaterializationStage !=
                        IWN_AP_CLIENT_MATERIALIZATION_IDLE ||
                    apClientTxBaEnablePending ||
                    (completedNode->flags & IWN_FLAG_SET_KEY) != 0) {
                    XYLog("%s: AP ADD_NODE reply id=%u flags=0x%02x "
                          "status=0x%02x len=%u\n",
                          sc->sc_dev.dv_xname,
                          static_cast<unsigned>(completedNode->id),
                          static_cast<unsigned>(completedNode->flags),
                          static_cast<unsigned>(addNodeStatus),
                          static_cast<unsigned>(replyLength));
                }
            }
            if (apFirmwareTransitionActive) {
                int completedAddNodeFlags = -1;
                int completedAddNodeId = -1;
                if (completedCommand->code == IWN_CMD_ADD_NODE) {
                    const struct iwn_node_info *completedNode =
                        reinterpret_cast<const struct iwn_node_info *>(
                            completedCommand->data);
                    completedAddNodeFlags = completedNode->flags;
                    completedAddNodeId = completedNode->id;
                } else if (completedCommand->code ==
                           IWN_CMD_LINK_QUALITY) {
                    const struct iwn_cmd_link_quality *completedLinkQuality =
                        reinterpret_cast<const struct iwn_cmd_link_quality *>(
                            completedCommand->data);
                    completedAddNodeId = completedLinkQuality->id;
                }
                iwn_note_ap_firmware_event(
                    completedCommand->code, -1,
                    completedAddNodeStatus, completedAddNodeFlags,
                    completedAddNodeId);
            }
            iwn_note_ap_sta_run_pan_fence(
                completedCommand->code,
                static_cast<uint16_t>(desc->idx));
        }
        if (apFirmwareTransitionActive &&
            desc->type == IWN_WIPAN_DEACTIVATION_COMPLETE) {
            iwn_note_ap_firmware_event(-1, desc->type, -1, -1, -1);
        }

        if (sc->auth_seq1_tx_pending) {
            sc->auth_seq1_tx_notif_count++;
            sc->auth_seq1_tx_lastnotif_qid = (desc->qid & 0xf);
            sc->auth_seq1_tx_lastnotif_type = desc->type;
            if (desc->type == IWN_MPDU_RX_DONE ||
                desc->type == IWN_RX_DONE)
                sc->auth_seq1_rx_mpdu_count++;
            if (desc->type == IWN_TX_DONE) {
                sc->auth_seq1_tx_last_txdone_qid = (desc->qid & 0xf);
                sc->auth_seq1_tx_last_txdone_idx = desc->idx;
                if (((desc->qid & 0xf) == sc->auth_seq1_tx_qid) &&
                    desc->idx == sc->auth_seq1_tx_idx)
                    sc->auth_seq1_tx_txdone_count++;
                else
                    sc->auth_seq1_tx_other_txdone_count++;
            }
        }

        if (!(desc->qid & 0x80))    /* Reply to a command. */
            iwn_cmd_done(sc, desc);

        switch (desc->type) {
        case IWN_RX_PHY:
            iwn_rx_phy(sc, desc, data);
            break;

        case IWN_RX_DONE:        /* 4965AGN only. */
        case IWN_MPDU_RX_DONE:
            /* An 802.11 frame has been received. */
            iwn_rx_done(sc, desc, data, &ml, &apMl);
            break;
        case IWN_RX_COMPRESSED_BA:
            /* A Compressed BlockAck has been received. */
            iwn_rx_compressed_ba(sc, desc, data);
            break;
        case IWN_TX_DONE:
            /* An 802.11 frame has been transmitted. */
            if (sc->auth_seq1_tx_pending &&
                ((desc->qid & 0xf) == sc->auth_seq1_tx_qid) &&
                desc->idx == sc->auth_seq1_tx_idx) {
                char auth_txdone_guard_buf[256];
                snprintf(auth_txdone_guard_buf,
                    sizeof(auth_txdone_guard_buf),
                    "stage=raw_txdone decision=raw_txdone_observed "
                    "subtype=0x%02x peer=%02x:%02x:%02x:%02x:%02x:%02x "
                    "auth_seq=0x%04x pending_qid=%u pending_idx=%u "
                    "qid=%u raw_qid=%u idx=%u desc_flags=0x%02x",
                    IEEE80211_FC0_SUBTYPE_AUTH,
                    sc->auth_seq1_tx_bssid[0],
                    sc->auth_seq1_tx_bssid[1],
                    sc->auth_seq1_tx_bssid[2],
                    sc->auth_seq1_tx_bssid[3],
                    sc->auth_seq1_tx_bssid[4],
                    sc->auth_seq1_tx_bssid[5],
                    1,
                    (unsigned)sc->auth_seq1_tx_qid,
                    (unsigned)sc->auth_seq1_tx_idx,
                    (unsigned)(desc->qid & 0xf),
                    (unsigned)desc->qid,
                    (unsigned)desc->idx,
                    (unsigned)desc->flags);
                IWX_AUTH_DIAG("iwn_notif_intr: AUTH TXDONE raw %s\n",
                    auth_txdone_guard_buf);
                getController()->setProperty(
                    "itlwm-iwn-auth-txdone-guard",
                    auth_txdone_guard_buf);
                {
                    struct iwn_tx_ring *auth_ring =
                        &sc->txq[sc->auth_seq1_tx_qid];
                    struct iwn_tx_data *auth_data =
                        &auth_ring->data[sc->auth_seq1_tx_idx];
                    const char *stage = (auth_data->ni != NULL) ?
                        "canonical_decoder_reached" :
                        "completion_status_dropped_before_reclaim";
                    const char *reason = (auth_data->ni != NULL) ?
                        "raw_iwn_txdone_delivered_to_driver_for_pending_auth_qid_idx" :
                        "raw_iwn_txdone_arrived_after_pending_slot_lost_node_before_reclaim";
                    char auth_tx_source_buf[384];
                    snprintf(auth_tx_source_buf,
                        sizeof(auth_tx_source_buf),
                        "stage=%s reason=%s subtype=0x%02x "
                        "peer=%02x:%02x:%02x:%02x:%02x:%02x "
                        "auth_seq=0x%04x pending_qid=%u pending_idx=%u "
                        "qid=%u raw_qid=%u idx=%u desc_flags=0x%02x "
                        "notif_count=%u txdone_count=%u other_txdone_count=%u "
                        "data_ni_present=%u",
                        stage, reason, IEEE80211_FC0_SUBTYPE_AUTH,
                        sc->auth_seq1_tx_bssid[0],
                        sc->auth_seq1_tx_bssid[1],
                        sc->auth_seq1_tx_bssid[2],
                        sc->auth_seq1_tx_bssid[3],
                        sc->auth_seq1_tx_bssid[4],
                        sc->auth_seq1_tx_bssid[5],
                        1,
                        (unsigned)sc->auth_seq1_tx_qid,
                        (unsigned)sc->auth_seq1_tx_idx,
                        (unsigned)(desc->qid & 0xf),
                        (unsigned)desc->qid,
                        (unsigned)desc->idx,
                        (unsigned)desc->flags,
                        (unsigned)sc->auth_seq1_tx_notif_count,
                        (unsigned)sc->auth_seq1_tx_txdone_count,
                        (unsigned)sc->auth_seq1_tx_other_txdone_count,
                        (unsigned)(auth_data->ni != NULL));
                    IWX_AUTH_DIAG(
                        "iwn_notif_intr: AUTH TX completion source %s\n",
                        auth_tx_source_buf);
                    getController()->setProperty(
                        "itlwm-iwn-auth-tx-completion-source",
                        auth_tx_source_buf);
                }
                sc->auth_seq1_tx_pending = 0;
            }
            ops->tx_done(sc, desc, data);
            break;

        case IWN_RX_STATISTICS:
        case IWN_BEACON_STATISTICS:
            iwn_rx_statistics(sc, desc, data);
            break;

        case IWN_BEACON_MISSED:
        {
            struct iwn_beacon_missed *miss =
                (struct iwn_beacon_missed *)(desc + 1);
            uint32_t missed;

            if ((ic->ic_opmode != IEEE80211_M_STA) ||
                (ic->ic_state != IEEE80211_S_RUN))
                break;

            bus_dmamap_sync(sc->sc_dmat, data->map, sizeof (*desc),
                sizeof (*miss), BUS_DMASYNC_POSTREAD);
            missed = letoh32(miss->consecutive);

            /*
             * If more than 5 consecutive beacons are missed,
             * reinitialize the sensitivity state machine.
             */
            if (missed > 5)
                (void)iwn_init_sensitivity(sc);

            /*
             * Tahoe's reference firmware link event enters
             * WCLNetManager::linkDownInd as "Net Beacons Lost" and leaves
             * the network immediately.  The historical OpenBSD directed
             * probe arms the management watchdog for another minute; on a
             * hard AP outage that kept macOS attached to a dead BSSID long
             * after the firmware threshold had already been crossed.
             *
             * Publish the distinct beacon-loss edge while the selected BSS
             * is still authoritative, then enter the same RUN -> SCAN path
             * that the eventual management timeout would have used.  Do not
             * manufacture a received deauthentication frame.
             */
            if (missed > ic->ic_bmissthres && !ic->ic_mgt_timer) {
                if (ic->ic_if.if_flags & IFF_DEBUG)
                    XYLog("%s: receiving no beacons from "
                        "%s; leaving the lost BSS\n",
                        sc->sc_dev.dv_xname, ether_sprintf(
                        ic->ic_bss->ni_macaddr));
                /* Arm only from this real firmware loss edge while the
                 * port-valid source BSS still proves the active private
                 * credential's identity.  The following generic state
                 * transition is then free to retire the old WCL request and
                 * public RSN policy exactly as before. */
                (void)iwn_sae_bss_loss_arm(ic, ic->ic_bss);
                if (ic->ic_event_handler != NULL)
                    (*ic->ic_event_handler)(
                        ic, IEEE80211_EVT_STA_BEACON_LOSS, NULL);
                ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
            }
            break;
        }
        case IWN_UC_READY:
        {
            struct iwn_ucode_info *uc =
                (struct iwn_ucode_info *)(desc + 1);

            /* The microcontroller is ready. */
            bus_dmamap_sync(sc->sc_dmat, data->map, sizeof (*desc),
                sizeof (*uc), BUS_DMASYNC_POSTREAD);

            if (letoh32(uc->valid) != 1) {
                XYLog("%s: microcontroller initialization "
                    "failed\n", sc->sc_dev.dv_xname);
                break;
            }
            if (uc->subtype == IWN_UCODE_INIT) {
                /* Save microcontroller report. */
                memcpy(&sc->ucode_info, uc, sizeof (*uc));
            }
            /* Save the address of the error log in SRAM. */
            sc->errptr = letoh32(uc->errptr);
            break;
        }
        case IWN_STATE_CHANGED:
        {
            uint32_t *status = (uint32_t *)(desc + 1);

            /* Enabled/disabled notification. */
            bus_dmamap_sync(sc->sc_dmat, data->map, sizeof (*desc),
                sizeof (*status), BUS_DMASYNC_POSTREAD);
            if (letoh32(*status) & 1) {
                /* Radio transmitter is off, power down. */
                iwn_stop(ifp);
                return;    /* No further processing. */
            }
            break;
        }
        case IWN_START_SCAN:
        {
            struct iwn_start_scan *scan =
                (struct iwn_start_scan *)(desc + 1);

            bus_dmamap_sync(sc->sc_dmat, data->map, sizeof (*desc),
                sizeof (*scan), BUS_DMASYNC_POSTREAD);

            if (sc->sc_flags & IWN_FLAG_BGSCAN)
                break;

            /* Fix current channel. */
            ic->ic_bss->ni_chan = &ic->ic_channels[scan->chan];
            break;
        }
        case IWN_STOP_SCAN:
        {
            struct iwn_stop_scan *scan =
                (struct iwn_stop_scan *)(desc + 1);
            struct iwn_scan_lease_terminal terminal;
            u_int8_t wnm_target_channel = 0;
            bool initial_handoff = false;
            bool replay_scan = false;
            bool join_terminal_retired = false;
            const bool wnm_exact_channel =
                iwn_scan_lease_wnm_target_channel(sc, 0,
                    &wnm_target_channel);

            bus_dmamap_sync(sc->sc_dmat, data->map, sizeof (*desc),
                sizeof (*scan), BUS_DMASYNC_POSTREAD);

            if (scan->status == 1 && scan->chan <= 14 &&
                (sc->sc_flags & IWN_FLAG_HAS_5GHZ) &&
                !wnm_exact_channel &&
                iwn_wcl_scan_plan_has_eligible_band(
                    sc, IEEE80211_CHAN_5GHZ)) {
                int error;
                /*
                 * We just finished scanning 2GHz channels,
                 * start scanning 5GHz ones under the same exact lease.
                 */
                error = iwn_scan_continue(sc, IEEE80211_CHAN_5GHZ,
                    (sc->sc_flags & IWN_FLAG_BGSCAN) ? 1 : 0);
                if (error == 0)
                    break;
            }

            if (scan->status != 1) {
                u_int64_t ignored_serial = 0;
                bool ignored_submit_abort = false;
                (void)iwn_scan_lease_mark_abort(sc, IWN_SCAN_LEASE_NONE, 0,
                                                 &ignored_serial,
                                                 &ignored_submit_abort);
            }

            /* A STOP_SCAN with no exact current lease is stale or belongs to
             * a lifecycle already closed above.  Never turn it into a fresh
             * generic/WCL terminal by clearing flags underneath a successor. */
            if (!iwn_scan_lease_claim_terminal(sc, &terminal))
                break;
            /*
             * DVM clears STATUS_SCAN_HW before post-scan PAN programming.
             * Keep the scan-priority schedule across the 2.4 -> 5 GHz
             * continuation above, then restore it only for this exact final
             * physical terminal.
             */
            if (iwn_set_ap_sta_scan_priority(false) != 0) {
                XYLog("%s: could not restore APSTA PAN parameters after "
                      "scan\n", sc->sc_dev.dv_xname);
                sc->sc_flags |= IWN_FLAG_FATAL_RECOVERY;
                (void)task_add(systq, &sc->init_task);
            }
            initial_handoff =
                iwn_wcl_initial_scan_claim_generic_terminal(sc, &terminal);
            /* This is the lower owner's exact terminal claim, before
             * net80211 cleanup can create an unrelated generic scan fact.
             * A WCL terminal that cannot be published upward remains an
             * honest missing-DONE diagnostic, rather than a false success. */
            if (terminal.wcl) {
                if (terminal.aborted) {
                    AirportItlwmPostPltiTraceAbortWclPhysicalScanEpisode(ic);
                } else {
                    AirportItlwmPostPltiTraceRecordWclPhysicalScan(
                        ic,
                        kAirportItlwmPostPltiTraceEventWclPhysicalScanTerminalComplete);
                }
            }
            if (terminal.wcl && !terminal.wcl_foreground)
                __atomic_store_n(&ic->ic_wcl_scan_suppress_scan_done_once,
                                 1, __ATOMIC_RELEASE);
            /*
             * HostAP may be sleeping for this exact physical terminal while
             * it owns the controller gate. Wake it before end_scan() enters
             * the upper event callback, which may need that same gate.
             */
            sc->sc_flags &= ~(IWN_FLAG_SCANNING | IWN_FLAG_BGSCAN);
            IOCommandGate *gate = getMainCommandGate();
            if (gate != NULL) {
                gate->commandWakeup(
                    &sc->sc_ap_transition_scan_blocked,
                    /*oneThread=*/false);
            }
            if (initial_handoff)
                ieee80211_end_scan_controlled(ifp,
                    IEEE80211_SCAN_COMPLETION_WCL_HANDOFF);
            else if (terminal.wcl_foreground)
                ieee80211_end_scan_controlled(ifp,
                    IEEE80211_SCAN_COMPLETION_WCL_FOREGROUND);
            else if (terminal.join_generation != 0 && terminal.aborted)
                ieee80211_end_scan_controlled(ifp,
                    IEEE80211_SCAN_COMPLETION_WCL_HANDOFF);
            else
                ieee80211_end_scan_owned(ifp,
                    IEEE80211_SCAN_COMPLETION_GENERIC,
                    terminal.join_generation, terminal.reassoc_serial);
            if (terminal.standard && terminal.publish_standard_terminal &&
                ic->ic_event_handler != NULL) {
                struct ieee80211_standard_scan_terminal standard_terminal;
                explicit_bzero(&standard_terminal, sizeof(standard_terminal));
                standard_terminal.generation = terminal.upper_generation;
                standard_terminal.backend_generation =
                    terminal.backend_generation;
                standard_terminal.status = terminal.aborted ?
                    IEEE80211_STANDARD_SCAN_TERMINAL_STATUS_ABORTED :
                    IEEE80211_STANDARD_SCAN_TERMINAL_STATUS_COMPLETE;
                /* This exact tag comes after ieee80211_end_scan() consumed
                 * the matching generic SCAN_DONE.  It is only the ownership
                 * fence for the controller's normal-scan ticket. */
                (*ic->ic_event_handler)(ic, IEEE80211_EVT_STANDARD_SCAN_TERMINAL,
                                        &standard_terminal);
                explicit_bzero(&standard_terminal, sizeof(standard_terminal));
            }
            if (terminal.wcl && terminal.publish_wcl_terminal &&
                ic->ic_event_handler != NULL) {
                struct ieee80211_wcl_scan_terminal wcl_terminal;
                explicit_bzero(&wcl_terminal, sizeof(wcl_terminal));
                wcl_terminal.generation = terminal.upper_generation;
                wcl_terminal.backend_generation = terminal.backend_generation;
                wcl_terminal.status = terminal.aborted ?
                    IEEE80211_WCL_SCAN_TERMINAL_STATUS_ABORTED :
                    IEEE80211_WCL_SCAN_TERMINAL_STATUS_COMPLETE;
                (*ic->ic_event_handler)(ic, IEEE80211_EVT_WCL_SCAN_TERMINAL,
                                        &wcl_terminal);
                explicit_bzero(&wcl_terminal, sizeof(wcl_terminal));
            }
            if (terminal.wcl && !terminal.wcl_foreground)
                __atomic_store_n(&ic->ic_wcl_scan_active, 0,
                                 __ATOMIC_RELEASE);
            replay_scan = iwn_scan_lease_finish_terminal(sc, terminal.serial,
                &join_terminal_retired);
            if (join_terminal_retired && terminal.join_generation != 0 &&
                ieee80211_wcl_join_failure_pending(ic,
                    terminal.join_generation)) {
                iwn_wcl_join_failure_scan(ic, terminal.join_generation);
                ieee80211_wcl_join_cleanup_done(ic, terminal.join_generation,
                    IEEE80211_JOIN_CLEANUP_PRODUCER);
            }
            explicit_bzero(&terminal, sizeof(terminal));
            if (initial_handoff || replay_scan)
                iwn_scan_lease_schedule_replay_task(sc);
            break;
        }
        case IWN5000_CALIBRATION_RESULT:
            iwn5000_rx_calib_results(sc, desc, data);
            break;

        case IWN5000_CALIBRATION_DONE:
            lockTsleep();
            sc->sc_flags |= IWN_FLAG_CALIB_DONE;
            wakeupOn(sc);
            unlockTsleep();
            break;
        }

        sc->rxq.cur = (sc->rxq.cur + 1) % IWN_RX_RING_COUNT;
    }
    if_input(&sc->sc_ic.ic_if, &ml);
    if_input_ap(&sc->sc_ic.ic_if, &apMl);

    /* Tell the firmware what we have processed. */
    hw = (hw == 0) ? IWN_RX_RING_COUNT - 1 : hw - 1;
    IWN_WRITE(sc, IWN_FH_RX_WPTR, hw & ~7);
}

/*
 * Process an INT_WAKEUP interrupt raised when the microcontroller wakes up
 * from power-down sleep mode.
 */
void ItlIwn::
iwn_wakeup_intr(struct iwn_softc *sc)
{
    int qid;

    /* Wakeup RX and TX rings. */
    IWN_WRITE(sc, IWN_FH_RX_WPTR, sc->rxq.cur & ~7);
    for (qid = 0; qid < sc->ntxqs; qid++) {
        struct iwn_tx_ring *ring = &sc->txq[qid];
        IWN_WRITE(sc, IWN_HBUS_TARG_WRPTR, qid << 8 | ring->cur);
    }
}

#ifdef IWN_DEBUG
/*
 * Dump the error log of the firmware when a firmware panic occurs.  Although
 * we can't debug the firmware because it is neither open source nor free, it
 * can help us to identify certain classes of problems.
 */
void ItlIwn::
iwn_fatal_intr(struct iwn_softc *sc)
{
    struct iwn_fw_dump dump;
    int i;

    /* Check that the error log address is valid. */
    if (sc->errptr < IWN_FW_DATA_BASE ||
        sc->errptr + sizeof (dump) >
        IWN_FW_DATA_BASE + sc->fw_data_maxsz) {
        XYLog("%s: bad firmware error log address 0x%08x\n",
            sc->sc_dev.dv_xname, sc->errptr);
        return;
    }
    if (iwn_nic_lock(sc) != 0) {
        XYLog("%s: could not read firmware error log\n",
            sc->sc_dev.dv_xname);
        return;
    }
    /* Read firmware error log from SRAM. */
    iwn_mem_read_region_4(sc, sc->errptr, (uint32_t *)&dump,
        sizeof (dump) / sizeof (uint32_t));
    iwn_nic_unlock(sc);

    if (dump.valid == 0) {
        XYLog("%s: firmware error log is empty\n",
            sc->sc_dev.dv_xname);
        return;
    }
    XYLog("firmware error log:\n");
    XYLog("  error type      = \"%s\" (0x%08X)\n",
        (dump.id < nitems(iwn_fw_errmsg)) ?
        iwn_fw_errmsg[dump.id] : "UNKNOWN",
        dump.id);
    XYLog("  program counter = 0x%08X\n", dump.pc);
    XYLog("  source line     = 0x%08X\n", dump.src_line);
    XYLog("  error data      = 0x%08X%08X\n",
        dump.error_data[0], dump.error_data[1]);
    XYLog("  branch link     = 0x%08X%08X\n",
        dump.branch_link[0], dump.branch_link[1]);
    XYLog("  interrupt link  = 0x%08X%08X\n",
        dump.interrupt_link[0], dump.interrupt_link[1]);
    XYLog("  time            = %u\n", dump.time[0]);

    /* Dump driver status (TX and RX rings) while we're here. */
    XYLog("driver status:\n");
    for (i = 0; i < sc->ntxqs; i++) {
        struct iwn_tx_ring *ring = &sc->txq[i];
        XYLog("  tx ring %2d: qid=%-2d cur=%-3d queued=%-3d\n",
            i, ring->qid, ring->cur, ring->queued);
    }
    XYLog("  rx ring: cur=%d\n", sc->rxq.cur);
    XYLog("  802.11 state %d\n", sc->sc_ic.ic_state);
}
#endif

int ItlIwn::
iwn_intr(OSObject *object, IOInterruptEventSource* sender, int count)
{
    ItlIwn *that = (ItlIwn*)object;
    struct iwn_softc *sc = &that->com;
    struct _ifnet *ifp = &sc->sc_ic.ic_if;
    uint32_t r1, r2, tmp;

//    IWN_WRITE(sc, IWN_INT_MASK, 0);

    /* Read interrupts from ICT (fast) or from registers (slow). */
    if (sc->sc_flags & IWN_FLAG_USE_ICT) {
        tmp = 0;
        while (sc->ict[sc->ict_cur] != 0) {
            tmp |= sc->ict[sc->ict_cur];
            sc->ict[sc->ict_cur] = 0;    /* Acknowledge. */
            sc->ict_cur = (sc->ict_cur + 1) % IWN_ICT_COUNT;
        }
        tmp = letoh32(tmp);
        if (tmp == 0xffffffff)    /* Shouldn't happen. */
            tmp = 0;
        else if (tmp & 0xc0000)    /* Workaround a HW bug. */
            tmp |= 0x8000;
        r1 = (tmp & 0xff00) << 16 | (tmp & 0xff);
        r2 = 0;    /* Unused. */
    } else {
        r1 = IWN_READ(sc, IWN_INT);
        if (r1 == 0xffffffff || (r1 & 0xfffffff0) == 0xa5a5a5a0)
            return 0;    /* Hardware gone! */
        r2 = IWN_READ(sc, IWN_FH_INT);
    }
    if (r1 == 0 && r2 == 0) {
        if (ifp->if_flags & IFF_UP)
            IWN_WRITE(sc, IWN_INT_MASK, sc->int_mask);
        return 0;    /* Interrupt not for us. */
    }

    /* Acknowledge interrupts. */
    IWN_WRITE(sc, IWN_INT, r1);
    if (!(sc->sc_flags & IWN_FLAG_USE_ICT))
        IWN_WRITE(sc, IWN_FH_INT, r2);

    if (r1 & IWN_INT_RF_TOGGLED) {
        tmp = IWN_READ(sc, IWN_GP_CNTRL) & IWN_GP_CNTRL_RFKILL;
        XYLog("%s: RF switch: radio %s\n", sc->sc_dev.dv_xname,
            tmp ? "enabled" : "disabled");
        if (tmp)
            task_add(systq, &sc->init_task);
    }
    if (r1 & IWN_INT_CT_REACHED) {
        XYLog("%s: critical temperature reached!\n",
            sc->sc_dev.dv_xname);
    }
    if (r1 & (IWN_INT_SW_ERR | IWN_INT_HW_ERR)) {
        XYLog("%s: fatal firmware error\n", sc->sc_dev.dv_xname);

        /* Force a complete recalibration on next init. */
        sc->sc_flags &= ~IWN_FLAG_CALIB_DONE;

        /* Dump firmware error log.  State/hardware teardown is deferred to
         * init_task so the interrupt action never crosses work-loop state. */
#ifdef IWN_DEBUG
        that->iwn_fatal_intr(sc);
#endif
        sc->sc_flags |= IWN_FLAG_FATAL_RECOVERY;
        task_add(systq, &sc->init_task);
        return 1;
    }
    if ((r1 & (IWN_INT_FH_RX | IWN_INT_SW_RX | IWN_INT_RX_PERIODIC)) ||
        (r2 & IWN_FH_INT_RX)) {
        if (sc->sc_flags & IWN_FLAG_USE_ICT) {
            if (r1 & (IWN_INT_FH_RX | IWN_INT_SW_RX))
                IWN_WRITE(sc, IWN_FH_INT, IWN_FH_INT_RX);
            IWN_WRITE_1(sc, IWN_INT_PERIODIC,
                IWN_INT_PERIODIC_DIS);
            that->iwn_notif_intr(sc);
            if (r1 & (IWN_INT_FH_RX | IWN_INT_SW_RX)) {
                IWN_WRITE_1(sc, IWN_INT_PERIODIC,
                    IWN_INT_PERIODIC_ENA);
            }
        } else
            that->iwn_notif_intr(sc);
    }

    if ((r1 & IWN_INT_FH_TX) || (r2 & IWN_FH_INT_TX)) {
        if (sc->sc_flags & IWN_FLAG_USE_ICT)
            IWN_WRITE(sc, IWN_FH_INT, IWN_FH_INT_TX);
        that->wakeupOn(sc);    /* FH DMA transfer completed. */
    }

    if (r1 & IWN_INT_ALIVE)
        that->wakeupOn(sc);    /* Firmware is alive. */

    if (r1 & IWN_INT_WAKEUP)
        that->iwn_wakeup_intr(sc);

    /* Re-enable interrupts. */
    if (ifp->if_flags & IFF_UP)
        IWN_WRITE(sc, IWN_INT_MASK, sc->int_mask);

    return 1;
}

/*
 * Update TX scheduler ring when transmitting an 802.11 frame (4965AGN and
 * 5000 adapters use a slightly different format).
 */
void ItlIwn::
iwn4965_update_sched(struct iwn_softc *sc, int qid, int idx, uint8_t id,
    uint16_t len)
{
    uint16_t *w = &sc->sched[qid * IWN4965_SCHED_COUNT + idx];

    *w = htole16(len + 8);
//    bus_dmamap_sync(sc->sc_dmat, sc->sched_dma.map,
//        (caddr_t)w - sc->sched_dma.vaddr, sizeof (uint16_t),
//        BUS_DMASYNC_PREWRITE);
    if (idx < IWN_SCHED_WINSZ) {
        *(w + IWN_TX_RING_COUNT) = *w;
//        bus_dmamap_sync(sc->sc_dmat, sc->sched_dma.map,
//            (caddr_t)(w + IWN_TX_RING_COUNT) - sc->sched_dma.vaddr,
//            sizeof (uint16_t), BUS_DMASYNC_PREWRITE);
    }
}

void ItlIwn::
iwn4965_reset_sched(struct iwn_softc *sc, int qid, int idx)
{
    /* TBD */
}

void ItlIwn::
iwn5000_update_sched(struct iwn_softc *sc, int qid, int idx, uint8_t id,
    uint16_t len)
{
    uint16_t *w = &sc->sched[qid * IWN5000_SCHED_COUNT + idx];

    *w = htole16(id << 12 | (len + 8));
//    bus_dmamap_sync(sc->sc_dmat, sc->sched_dma.map,
//        (caddr_t)w - sc->sched_dma.vaddr, sizeof (uint16_t),
//        BUS_DMASYNC_PREWRITE);
    if (idx < IWN_SCHED_WINSZ) {
        *(w + IWN_TX_RING_COUNT) = *w;
//        bus_dmamap_sync(sc->sc_dmat, sc->sched_dma.map,
//            (caddr_t)(w + IWN_TX_RING_COUNT) - sc->sched_dma.vaddr,
//            sizeof (uint16_t), BUS_DMASYNC_PREWRITE);
    }
}

void ItlIwn::
iwn5000_reset_sched(struct iwn_softc *sc, int qid, int idx)
{
    uint16_t *w = &sc->sched[qid * IWN5000_SCHED_COUNT + idx];

    *w = (*w & htole16(0xf000)) | htole16(1);
//    bus_dmamap_sync(sc->sc_dmat, sc->sched_dma.map,
//        (caddr_t)w - sc->sched_dma.vaddr, sizeof (uint16_t),
//        BUS_DMASYNC_PREWRITE);
    if (idx < IWN_SCHED_WINSZ) {
        *(w + IWN_TX_RING_COUNT) = *w;
//        bus_dmamap_sync(sc->sc_dmat, sc->sched_dma.map,
//            (caddr_t)(w + IWN_TX_RING_COUNT) - sc->sched_dma.vaddr,
//            sizeof (uint16_t), BUS_DMASYNC_PREWRITE);
    }
}

int ItlIwn::
iwn_rval2ridx(int rval)
{
    int ridx;

    for (ridx = 0; ridx < nitems(iwn_rates); ridx++) {
        if (rval == iwn_rates[ridx].rate)
            break;
    }

    return ridx;
}

static int
iwn_is_mimo_ht_plcp(uint8_t ht_plcp)
{
    return (ht_plcp != IWN_RATE_HT_SISO_MCS_INV_PLCP &&
            (ht_plcp & IWN_RATE_HT_MCS_NSS_MSK));
}

static int
iwn_is_mimo_mcs(int mcs)
{
    int ridx = iwn_mcs2ridx[mcs];
    return iwn_is_mimo_ht_plcp(iwn_rates[ridx].ht_plcp);
    
}

static void
iwn_clear_apple_nrate_cache(struct iwn_softc *sc)
{
    sc->sc_last_apple_nrate = 0;
    sc->sc_has_last_apple_nrate = 0;
}

static void
iwn_publish_apple_nrate(struct iwn_softc *sc, uint32_t nrate)
{
    sc->sc_last_apple_nrate = nrate;
    sc->sc_has_last_apple_nrate = 1;
}

static bool
iwn_build_ht_apple_nrate(uint8_t rate, uint8_t rflags, uint32_t *nrate)
{
    if ((rflags & IWN_RFLAG_MCS) == 0 || rate > IWN_RATE_HT_MIMO2_MCS_15_PLCP)
        return false;

    return TahoeNrateContracts::buildHtNrateFromMcs(
        rate, (rflags & IWN_RFLAG_HT40) != 0, nrate);
}

/*
 * Classify only fixed 802.11/LLC protocol fields before iwn_tx() trims the
 * header.  The class is retained in a ring slot solely until TX_DONE; no
 * address, sequence, status, rate, length, or frame bytes are retained.
 */
static uint8_t
iwn_post_plti_trace_classify_tx(const struct ieee80211_frame *wh,
                                uint8_t type, uint8_t subtype, u_int hdrlen,
                                mbuf_t m)
{
    if (wh == NULL || m == NULL)
        return IWN_POST_PLTI_TRACE_TX_NONE;
    if (type == IEEE80211_FC0_TYPE_MGT) {
        if (subtype == IEEE80211_FC0_SUBTYPE_AUTH)
            return IWN_POST_PLTI_TRACE_TX_AUTH;
        if (subtype == IEEE80211_FC0_SUBTYPE_ASSOC_REQ ||
            subtype == IEEE80211_FC0_SUBTYPE_REASSOC_REQ)
            return IWN_POST_PLTI_TRACE_TX_ASSOC;
        return IWN_POST_PLTI_TRACE_TX_NONE;
    }
    if (type != IEEE80211_FC0_TYPE_DATA ||
        mbuf_len(m) < hdrlen + LLC_SNAPFRAMELEN)
        return IWN_POST_PLTI_TRACE_TX_NONE;

    const struct llc *llc = reinterpret_cast<const struct llc *>(
        reinterpret_cast<const uint8_t *>(wh) + hdrlen);
    if (llc->llc_dsap == LLC_SNAP_LSAP &&
        llc->llc_ssap == LLC_SNAP_LSAP && llc->llc_control == LLC_UI &&
        llc->llc_snap.org_code[0] == 0 && llc->llc_snap.org_code[1] == 0 &&
        llc->llc_snap.org_code[2] == 0 &&
        llc->llc_snap.ether_type == htons(ETHERTYPE_PAE))
        return IWN_POST_PLTI_TRACE_TX_EAPOL;
    return IWN_POST_PLTI_TRACE_TX_NONE;
}

static void
iwn_post_plti_trace_record_submit(struct ieee80211com *ic, uint8_t txClass)
{
    switch (txClass) {
    case IWN_POST_PLTI_TRACE_TX_AUTH:
        AirportItlwmPostPltiTraceRecord(
            ic, kAirportItlwmPostPltiTraceEventAuthFwSubmitted);
        break;
    case IWN_POST_PLTI_TRACE_TX_ASSOC:
        AirportItlwmPostPltiTraceRecord(
            ic, kAirportItlwmPostPltiTraceEventAssocFwSubmitted);
        break;
    case IWN_POST_PLTI_TRACE_TX_EAPOL:
        AirportItlwmPostPltiTraceRecord(
            ic, kAirportItlwmPostPltiTraceEventEapolFwSubmitted);
        break;
    default:
        break;
    }
}

static void
iwn_post_plti_trace_record_completion(struct ieee80211com *ic,
                                      uint8_t txClass)
{
    switch (txClass) {
    case IWN_POST_PLTI_TRACE_TX_AUTH:
        AirportItlwmPostPltiTraceRecord(
            ic, kAirportItlwmPostPltiTraceEventAuthTxDone);
        break;
    case IWN_POST_PLTI_TRACE_TX_ASSOC:
        AirportItlwmPostPltiTraceRecord(
            ic, kAirportItlwmPostPltiTraceEventAssocTxDone);
        break;
    case IWN_POST_PLTI_TRACE_TX_EAPOL:
        AirportItlwmPostPltiTraceRecord(
            ic, kAirportItlwmPostPltiTraceEventEapolTxDone);
        break;
    default:
        break;
    }
}

int ItlIwn::
iwn_tx(struct iwn_softc *sc, mbuf_t m, struct ieee80211_node *ni,
    const struct ItlSaeAuthTxRequestV1 *sae_request)
{
    struct iwn_ops *ops = &sc->ops;
    struct ieee80211com *ic = &sc->sc_ic;
    struct iwn_node *wn = (struct iwn_node *)ni;
    struct iwn_tx_ring *ring;
    struct iwn_tx_desc *desc;
    struct iwn_tx_data *data;
    struct iwn_tx_cmd *cmd;
    struct iwn_cmd_data *tx;
    const struct iwn_rate *rinfo;
    struct ieee80211_frame *wh;
    struct ieee80211_key *k = NULL;
    enum ieee80211_edca_ac ac;
    int qid;
    uint32_t flags;
    uint16_t qos;
    u_int hdrlen;
    IOPhysicalSegment *seg;
    IOPhysicalSegment segs[IWN_MAX_SCATTER - 1];
    int nsegs = 0;
    uint32_t tx_apple_nrate = 0;
    bool tx_apple_nrate_valid = false;
    uint8_t *ivp, tid, ridx, txant, type, subtype;
    uint8_t post_plti_trace_class;
    uint64_t wnm_tx_fence_generation = 0;
    uint8_t wnm_tx_fence_kind = 0;
    struct IwnSaeAssocTxClaim sae_assoc_claim;
    enum IwnSaeAssocTxAdmission sae_assoc_tx = IWN_SAE_ASSOC_TX_NOT_DIRECT;
    int i, totlen, hasqos, error, pad;

    explicit_bzero(&sae_assoc_claim, sizeof(sae_assoc_claim));
    wh = mtod(m, struct ieee80211_frame *);
    type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
    subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;
    if (type == IEEE80211_FC0_TYPE_CTL)
        hdrlen = sizeof(struct ieee80211_frame_min);
    else
        hdrlen = ieee80211_get_hdrlen(wh);

    /*
     * The private direct path transports exactly one public Algorithm-3
     * Authentication frame.  Validate its complete pre-trim representation
     * before an mbuf or later diagnostics can become the source of truth.
     */
    if (sae_request != NULL) {
        const u_int8_t *auth;

        if (!itl_sae_auth_transport_request_is_well_formed(sae_request) ||
            type != IEEE80211_FC0_TYPE_MGT ||
            subtype != IEEE80211_FC0_SUBTYPE_AUTH ||
            hdrlen != sizeof(struct ieee80211_frame) ||
            mbuf_len(m) < hdrlen + 6 + sae_request->body_len ||
            mbuf_pkthdr_len(m) != hdrlen + 6 + sae_request->body_len ||
            memcmp(wh->i_addr1, sae_request->bssid,
                sizeof(sae_request->bssid)) != 0 ||
            memcmp(wh->i_addr2, sae_request->sta,
                sizeof(sae_request->sta)) != 0 ||
            memcmp(wh->i_addr3, sae_request->bssid,
                sizeof(sae_request->bssid)) != 0) {
            mbuf_freem(m);
            return EINVAL;
        }
        auth = (const u_int8_t *)wh + hdrlen;
        if (LE_READ_2(auth) != IEEE80211_AUTH_ALG_SAE ||
            LE_READ_2(auth + 2) != sae_request->wire_transaction ||
            LE_READ_2(auth + 4) != sae_request->auth_status ||
            memcmp(auth + 6, sae_request->body,
                sae_request->body_len) != 0) {
            mbuf_freem(m);
            return EINVAL;
        }
    }
    /* Generic S_ASSOC output is asynchronous.  A direct SAE owner admits
     * only its exact Association Request here, while a final fence below
     * repeats the claim immediately before scheduler publication. */
    if (sae_request == NULL && type == IEEE80211_FC0_TYPE_MGT &&
        (subtype == IEEE80211_FC0_SUBTYPE_ASSOC_REQ ||
        subtype == IEEE80211_FC0_SUBTYPE_REASSOC_REQ)) {
        sae_assoc_tx = iwn_sae_engine_assoc_tx_preflight(sc, ni, wh,
            &sae_assoc_claim);
        if (sae_assoc_tx == IWN_SAE_ASSOC_TX_REJECTED) {
            mbuf_freem(m);
            explicit_bzero(&sae_assoc_claim, sizeof(sae_assoc_claim));
            return ECANCELED;
        }
    }
    post_plti_trace_class = iwn_post_plti_trace_classify_tx(
        wh, type, subtype, hdrlen, m);

    /* Capture-before-mbuf_adj diagnostic identity. iwn_tx()
     * calls mbuf_adj(m, hdrlen) below (in both the CCMP-trim
     * branch and the non-CCMP branch) BEFORE storing the
     * post-trim mbuf on data->m. iwn_tx_done() therefore
     * cannot read a struct ieee80211_frame from data->m at
     * completion time. The locals captured here are stored on
     * the per-tx-buffer iwn_tx_data entry just below the
     * existing `data->m = m; data->ni = ni;` site so the
     * completion handler can attribute the firmware TX_RESP
     * to a specific management subtype + receiver MAC + AUTH
     * transaction sequence. The capture is read-only on the
     * still-untrimmed mbuf; behavior is unchanged.
     *
     * Sentinels:
     *   diag_subtype = 0xff if the slot identity was not
     *                       captured (e.g., MGT branch not
     *                       taken for this frame).
     *   diag_auth_seq = 0xffff if the frame is not AUTH or
     *                       the first mbuf segment is shorter
     *                       than hdrlen + 4 bytes (so the
     *                       AUTH body is not contiguous with
     *                       the 802.11 header in this read).
     *   diag_peer = {0,0,0,0,0,0} when identity not captured. */
    uint8_t  diag_subtype  = 0xff;
    uint16_t diag_auth_seq = 0xffff;
    uint8_t  diag_peer[6]  = { 0, 0, 0, 0, 0, 0 };
    if (type == IEEE80211_FC0_TYPE_MGT) {
        diag_subtype = subtype;
        IEEE80211_ADDR_COPY(diag_peer, wh->i_addr1);
        if (subtype == IEEE80211_FC0_SUBTYPE_AUTH &&
            mbuf_len(m) >= (size_t)(hdrlen + 4)) {
            /*
             * IEEE 802.11 auth body: algo(2) seq(2) status(2),
             * little-endian, immediately after the 802.11
             * header. Use mbuf_len(m) (first-segment length,
             * not the total pkthdr length) so the read is
             * contiguous with wh in the same mbuf segment.
             * Match the iwx-side accepted safe pattern.
             */
            const u_int8_t *auth_body =
                (const u_int8_t *)wh + hdrlen;
            diag_auth_seq =
                (uint16_t)(auth_body[2] |
                           (auth_body[3] << 8));
        }
    }
    if (sae_request == NULL)
        (void)ieee80211_wnm_bss_transition_tx_fence_classify(
            ic, ni, wh, mbuf_len(m), &wnm_tx_fence_generation,
            &wnm_tx_fence_kind);

    if ((hasqos = ieee80211_has_qos(wh))) {
        /* Select EDCA Access Category and TX ring for this frame. */
        struct ieee80211_tx_ba *ba;
        qos = ieee80211_get_qos(wh);
        tid = qos & IEEE80211_QOS_TID;
        ac = ieee80211_up_to_ac(ic, tid);
        qid = ac;

        /* If possible, put this frame on an aggregation queue. */
        if (sc->sc_tx_ba[tid].wn == wn) {
            ba = &ni->ni_tx_ba[tid];
            if (!IEEE80211_IS_MULTICAST(wh->i_addr1) &&
                ba->ba_state == IEEE80211_BA_AGREED) {
                qid = sc->first_agg_txq + tid;
                if (sc->qfullmsk & (1 << qid)) {
                    mbuf_freem(m);
                    ieee80211_wnm_bss_transition_tx_fence_submit_failed(
                        ic, ni, wnm_tx_fence_generation,
                        wnm_tx_fence_kind);
                    return ENOBUFS;
                }
            }
        }
    } else {
        qos = 0;
        tid = IWN_NONQOS_TID;
        ac = EDCA_AC_BE;
        qid = ac;
    }

    ring = &sc->txq[qid];
    desc = &ring->desc[ring->cur];
    data = &ring->data[ring->cur];

    /* Choose a TX rate index. */
    if (IEEE80211_IS_MULTICAST(wh->i_addr1) ||
        type != IEEE80211_FC0_TYPE_DATA)
        ridx = iwn_rval2ridx(ieee80211_min_basic_rate(ic));
    else if (ic->ic_fixed_mcs != -1)
        ridx = sc->fixed_ridx;
    else if (ic->ic_fixed_rate != -1)
        ridx = sc->fixed_ridx;
    else {
        if (ni->ni_flags & IEEE80211_NODE_HT)
            ridx = iwn_mcs2ridx[ni->ni_txmcs];
        else
            ridx = wn->ridx[ni->ni_txrate];
    }
    rinfo = &iwn_rates[ridx];
#if NBPFILTER > 0
    if (sc->sc_drvbpf != NULL) {
        struct iwn_tx_radiotap_header *tap = &sc->sc_txtap;
        uint16_t chan_flags;

        tap->wt_flags = 0;
        tap->wt_chan_freq = htole16(ni->ni_chan->ic_freq);
        chan_flags = ni->ni_chan->ic_flags;
        if (ic->ic_curmode != IEEE80211_MODE_11N)
            chan_flags &= ~IEEE80211_CHAN_HT;
        tap->wt_chan_flags = htole16(chan_flags);
        if ((ni->ni_flags & IEEE80211_NODE_HT) &&
            !IEEE80211_IS_MULTICAST(wh->i_addr1) &&
            type == IEEE80211_FC0_TYPE_DATA) {
            tap->wt_rate = (0x80 | ni->ni_txmcs);
        } else
            tap->wt_rate = rinfo->rate;
        if ((ic->ic_flags & IEEE80211_F_WEPON) &&
            (wh->i_fc[1] & IEEE80211_FC1_PROTECTED))
            tap->wt_flags |= IEEE80211_RADIOTAP_F_WEP;

        bpf_mtap_hdr(sc->sc_drvbpf, tap, sc->sc_txtap_len,
            m, BPF_DIRECTION_OUT);
    }
#endif

    //    totlen = m->m_pkthdr.len;
    totlen = mbuf_pkthdr_len(m);

    /* Encrypt the frame if need be. */
    if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
        /* Retrieve key for TX. */
        k = ieee80211_get_txkey(ic, wh, ni);
        if (k == NULL) {
            mbuf_freem(m);
            ieee80211_wnm_bss_transition_tx_fence_submit_failed(
                ic, ni, wnm_tx_fence_generation,
                wnm_tx_fence_kind);
            return EINVAL;
        }
        /* BIP table identity must be routed before any live descriptor
         * field is observed.  The later hardware-IV path sees k == NULL. */
        if (ieee80211_bip_key_is_slot(ic, k) ||
            k->k_cipher != IEEE80211_CIPHER_CCMP ||
            (k->k_flags & IEEE80211_KEY_SWCRYPTO) ||
            (ni->ni_flags & IEEE80211_NODE_MFP)) {
            /* Do software encryption. */
            if ((m = ieee80211_encrypt(ic, m, k)) == NULL) {
                ieee80211_wnm_bss_transition_tx_fence_submit_failed(
                    ic, ni, wnm_tx_fence_generation,
                    wnm_tx_fence_kind);
                return ENOBUFS;
            }
            /* 802.11 header may have moved. */
            wh = mtod(m, struct ieee80211_frame *);
            //    totlen = m->m_pkthdr.len;
            totlen = mbuf_pkthdr_len(m);
            k = NULL;

        } else    /* HW appends CCMP MIC. */
            totlen += IEEE80211_CCMP_HDRLEN;
    }

    data->totlen = totlen;

    /* Prepare TX firmware command. */
    cmd = &ring->cmd[ring->cur];
    cmd->code = IWN_CMD_TX_DATA;
    cmd->flags = 0;
    cmd->qid = ring->qid;
    cmd->idx = ring->cur;

    tx = (struct iwn_cmd_data *)cmd->data;
    /* NB: No need to clear tx, all fields are reinitialized here. */
    tx->scratch = 0;    /* clear "scratch" area */

    flags = 0;
    if (!IEEE80211_IS_MULTICAST(wh->i_addr1)) {
        /* Unicast frame, check if an ACK is expected. */
        if (!hasqos || (qos & IEEE80211_QOS_ACK_POLICY_MASK) !=
            IEEE80211_QOS_ACK_POLICY_NOACK)
            flags |= IWN_TX_NEED_ACK;
    }
    if (type == IEEE80211_FC0_TYPE_CTL &&
        subtype == IEEE80211_FC0_SUBTYPE_BAR) {
        struct ieee80211_frame_min *mwh;
        uint8_t *barfrm;
        uint16_t ctl;
        mwh = mtod(m, struct ieee80211_frame_min *);
        barfrm = (uint8_t *)&mwh[1];
        ctl = LE_READ_2(barfrm);
        tid = (ctl & IEEE80211_BA_TID_INFO_MASK) >>
            IEEE80211_BA_TID_INFO_SHIFT;
        flags |= (IWN_TX_NEED_ACK | IWN_TX_IMM_BA);
    }

    if (wh->i_fc[1] & IEEE80211_FC1_MORE_FRAG)
        flags |= IWN_TX_MORE_FRAG;    /* Cannot happen yet. */

    /* Check if frame must be protected using RTS/CTS or CTS-to-self. */
    if (!IEEE80211_IS_MULTICAST(wh->i_addr1)) {
        /* NB: Group frames are sent using CCK in 802.11b/g/n (2GHz). */
        if (totlen + IEEE80211_CRC_LEN > ic->ic_rtsthreshold) {
            flags |= IWN_TX_NEED_RTS;
        } else if ((ic->ic_flags & IEEE80211_F_USEPROT) &&
            IWN_RIDX_IS_OFDM(ridx)) {
            if (ic->ic_protmode == IEEE80211_PROT_CTSONLY)
                flags |= IWN_TX_NEED_CTS;
            else if (ic->ic_protmode == IEEE80211_PROT_RTSCTS)
                flags |= IWN_TX_NEED_RTS;
        }

        if (flags & (IWN_TX_NEED_RTS | IWN_TX_NEED_CTS)) {
            if (sc->hw_type != IWN_HW_REV_TYPE_4965) {
                /* 5000 autoselects RTS/CTS or CTS-to-self. */
                flags &= ~(IWN_TX_NEED_RTS | IWN_TX_NEED_CTS);
                flags |= IWN_TX_NEED_PROTECTION;
            } else
                flags |= IWN_TX_FULL_TXOP;
        }
    }

    /* DVM requires aggregate transport protection independently of the
     * BSS's legacy mixed-mode protection setting.  qid is already the
     * selected RA/TID scheduler queue, so this is the local equivalent of
     * Linux's IEEE80211_TX_CTL_AMPDU -> PROT_REQUIRE rule.  The separate
     * per-family use_rts_for_aggregation policy controls only TLC_RTS in the
     * station Link Quality table; every post-4965 aggregate TX command still
     * carries PROT_REQUIRE. */
    if (!IEEE80211_IS_MULTICAST(wh->i_addr1) &&
        ring->qid >= sc->first_agg_txq &&
        sc->hw_type != IWN_HW_REV_TYPE_4965)
        flags |= IWN_TX_NEED_PROTECTION;

    /*
     * Stock/Apple behavior: all management frames (including pre-association
     * AUTH) are transmitted via the broadcast/aux station. Routing AUTH via
     * the unicast BSS node (wn->id) before association is off-stock and
     * pairs with the FILTER_BSS/add_bss_node deviations that stalled the
     * data TX FIFO.
     */
    if (type == IEEE80211_FC0_TYPE_CTL &&
        subtype == IEEE80211_FC0_SUBTYPE_BAR)
        tx->id = wn->id;
    else if (IEEE80211_IS_MULTICAST(wh->i_addr1) ||
        type != IEEE80211_FC0_TYPE_DATA)
        tx->id = sc->broadcast_id;
    else
        tx->id = wn->id;

    if (type == IEEE80211_FC0_TYPE_MGT) {
#ifndef IEEE80211_STA_ONLY
        /* Tell HW to set timestamp in probe responses. */
        if (subtype == IEEE80211_FC0_SUBTYPE_PROBE_RESP)
            flags |= IWN_TX_INSERT_TSTAMP;
#endif
        if (subtype == IEEE80211_FC0_SUBTYPE_ASSOC_REQ ||
            subtype == IEEE80211_FC0_SUBTYPE_REASSOC_REQ)
            tx->timeout = htole16(3);
        else
            tx->timeout = htole16(2);
    } else
        tx->timeout = htole16(0);

    if (hdrlen & 3) {
        /* First segment length must be a multiple of 4. */
        flags |= IWN_TX_NEED_PADDING;
        pad = 4 - (hdrlen & 3);
    } else
        pad = 0;

    tx->len = htole16(totlen);
    tx->tid = tid;
    tx->rts_ntries = 60;
    tx->data_ntries = 15;
    tx->lifetime = htole32(IWN_LIFETIME_INFINITE);

    if ((ni->ni_flags & IEEE80211_NODE_HT) &&
        tx->id != sc->broadcast_id &&
        rinfo->ht_plcp != IWN_RATE_HT_SISO_MCS_INV_PLCP) {
        bool ht40Enabled = iwn_rxon_ht40_enabled(sc);

        tx->plcp = rinfo->ht_plcp;
        tx->rflags = IWN_RFLAG_MCS;
        if (ht40Enabled)
            tx->rflags |= IWN_RFLAG_HT40;
        if (IwnHt40Contracts::allowsSgiForEffectiveHtWidth(
                ht40Enabled,
                ieee80211_node_supports_ht_sgi20(ni),
                ieee80211_node_supports_ht_sgi40(ni)))
            tx->rflags |= IWN_RFLAG_SGI;
        if (iwn_is_mimo_ht_plcp(rinfo->ht_plcp))
            tx->rflags |= IWN_RFLAG_ANT(sc->txchainmask);
        else
            tx->rflags |= IWN_RFLAG_ANT(IWN_LSB(sc->txchainmask));
    }
    else {
        tx->plcp = rinfo->plcp;
        if (IWN_RIDX_IS_CCK(ridx))
            tx->rflags = IWN_RFLAG_CCK;
        else
            tx->rflags = 0;
    }
    if (tx->id == sc->broadcast_id || ic->ic_fixed_mcs != -1 ||
        ic->ic_fixed_rate != -1) {
        /* Group or management frame, or fixed Tx rate. */
        tx->linkq = 0;
        /* XXX Alternate between antenna A and B? */
        txant = IWN_LSB(sc->txchainmask);
        tx->rflags |= IWN_RFLAG_ANT(txant);
    } else {
        tx->linkq = 0; /* initial index into firmware LQ retry table */
        flags |= IWN_TX_LINKQ;    /* enable multi-rate retry */
    }

    if (tx->id != sc->broadcast_id && type == IEEE80211_FC0_TYPE_DATA) {
        if (tx->rflags & IWN_RFLAG_MCS) {
            tx_apple_nrate_valid =
                iwn_build_ht_apple_nrate(tx->plcp, tx->rflags,
                                         &tx_apple_nrate);
        } else {
            tx_apple_nrate_valid =
                TahoeNrateContracts::buildLegacyNrateFromHalfMbps(
                    rinfo->rate, &tx_apple_nrate);
        }
    }

    /* Set physical address of "scratch area". */
    tx->loaddr = htole32(IWN_LOADDR(data->scratch_paddr));
    tx->hiaddr = IWN_HIADDR(data->scratch_paddr);

    /* Copy 802.11 header in TX command. */
    memcpy((uint8_t *)(tx + 1), wh, hdrlen);

    if (k != NULL && k->k_cipher == IEEE80211_CIPHER_CCMP) {
        /* Trim 802.11 header and prepend CCMP IV. */
        mbuf_adj(m, hdrlen - IEEE80211_CCMP_HDRLEN);
        ivp = mtod(m, uint8_t *);
        k->k_tsc++;
        ivp[0] = k->k_tsc;
        ivp[1] = k->k_tsc >> 8;
        ivp[2] = 0;
        ivp[3] = k->k_id << 6 | IEEE80211_WEP_EXTIV;
        ivp[4] = k->k_tsc >> 16;
        ivp[5] = k->k_tsc >> 24;
        ivp[6] = k->k_tsc >> 32;
        ivp[7] = k->k_tsc >> 40;

        tx->security = IWN_CIPHER_CCMP;
        if (qid >= sc->first_agg_txq)
            flags |= IWN_TX_AMPDU_CCMP;
        memcpy(tx->key, k->k_key, k->k_len);

        /* TX scheduler includes CCMP MIC len w/5000 Series. */
        if (sc->hw_type != IWN_HW_REV_TYPE_4965)
            totlen += IEEE80211_CCMP_MICLEN;
    } else {
        /* Trim 802.11 header. */
        mbuf_adj(m, hdrlen);
        tx->security = 0;
    }
    tx->flags = htole32(flags);

    nsegs = data->map->cursor->getPhysicalSegmentsWithCoalesce(m, &segs[0], IWN_MAX_SCATTER - 1);
    if (nsegs == 0) {
        XYLog("%s: can't map mbuf (error %d)\n", DEVNAME(sc),
              nsegs);
        mbuf_freem(m);
        ieee80211_wnm_bss_transition_tx_fence_submit_failed(
            ic, ni, wnm_tx_fence_generation, wnm_tx_fence_kind);
        return ENOMEM;
    }

    data->m = m;
    data->ni = ni;
    data->txrate = ni->ni_txrate;
    data->ampdu_txmcs = ni->ni_txmcs; /* updated upon Tx interrupt */
    data->ampdu_rate_generation = 0;
    data->ampdu_rate_rflags = 0;
    data->ampdu_rate_feedback_valid = 0;
    data->tx_apple_nrate = tx_apple_nrate;
    data->tx_apple_nrate_valid = tx_apple_nrate_valid ? 1 : 0;
    data->post_plti_trace_class = post_plti_trace_class;
    data->ap_mgmt = false;
    data->ap_data = false;
    iwn_sae_tx_data_clear(data);
    if (sae_request != NULL) {
        data->sae_active = true;
        data->sae_phase = sae_request->phase;
        data->sae_auth_status = sae_request->auth_status;
        data->sae_wire_transaction = sae_request->wire_transaction;
        data->sae_association_epoch = sae_request->association_epoch;
        data->sae_relay_generation = sae_request->relay_generation;
        data->sae_ticket = sae_request->ticket;
        if (sc->sc_sae_tx_lock != NULL) {
            IOSimpleLockLock(sc->sc_sae_tx_lock);
            data->sae_lifecycle_generation = sc->sc_sae_tx_generation;
            IOSimpleLockUnlock(sc->sc_sae_tx_lock);
        }
        memcpy(data->sae_bssid, sae_request->bssid,
            sizeof(data->sae_bssid));
        memcpy(data->sae_sta, sae_request->sta, sizeof(data->sae_sta));
    }
    if (data->tx_apple_nrate_valid)
        iwn_publish_apple_nrate(sc, data->tx_apple_nrate);
    /* Store captured diagnostic identity onto the per-tx-buffer
     * entry. The captured locals diag_subtype / diag_auth_seq /
     * diag_peer above are the load-bearing copies for the
     * iwn_tx_done MGT TX completion probe. */
    data->diag_subtype  = diag_subtype;
    data->diag_auth_seq = diag_auth_seq;
    memcpy(data->diag_peer, diag_peer, sizeof(data->diag_peer));

    /* Fill TX descriptor. */
    desc->nsegs = 1 + nsegs;
    /* First DMA segment is used by the TX command. */
    desc->segs[0].addr = htole32(IWN_LOADDR(data->cmd_paddr));
    desc->segs[0].len  = htole16(IWN_HIADDR(data->cmd_paddr) |
        (4 + sizeof (*tx) + hdrlen + pad) << 4);
    /* Other DMA segments are for data payload. */
    for (i = 0; i < nsegs; i++) {
        seg = &segs[i];
        desc->segs[i + 1].addr = htole32(IWN_LOADDR(seg->location));
        desc->segs[i + 1].len  = htole16(IWN_HIADDR(seg->location) |
            seg->length << 4);
//        XYLog("DMA segments index=%d location=0x%llx length=%llu", i, seg->location, seg->length);
    }
//    XYLog("----------end sending data------\n");

//    bus_dmamap_sync(sc->sc_dmat, data->map, 0, data->map->dm_mapsize,
//        BUS_DMASYNC_PREWRITE);
//    bus_dmamap_sync(sc->sc_dmat, ring->cmd_dma.map,
//        (caddr_t)cmd - ring->cmd_dma.vaddr, sizeof (*cmd),
//        BUS_DMASYNC_PREWRITE);
//    bus_dmamap_sync(sc->sc_dmat, ring->desc_dma.map,
//        (caddr_t)desc - ring->desc_dma.vaddr, sizeof (*desc),
//        BUS_DMASYNC_PREWRITE);

    /*
     * Claim an accepted BTM leave only at the descriptor publication edge.
     * Earlier mapping/crypto failures therefore cannot leave a phantom
     * submitted bit, while the generation rejects a request superseded
     * between classification and this final fence.
     */
    if (wnm_tx_fence_kind != 0 &&
        ieee80211_wnm_bss_transition_tx_fence_submit(ic,
            wnm_tx_fence_generation, wnm_tx_fence_kind)) {
        data->wnm_tx_fence_generation = wnm_tx_fence_generation;
        data->wnm_tx_fence_kind = wnm_tx_fence_kind;
    }

    /* Kick TX ring.  SAE keeps scheduler publication in the same leaf as
     * cancellation and WRPTR, so a pre-doorbell abort cannot leave a 4965
     * scheduler slot live without a matching descriptor. */
    if (sae_request != NULL) {
        const int saved_cur = ring->cur;
        const int next_cur = (ring->cur + 1) % IWN_TX_RING_COUNT;

        ring->cur = next_cur;
        if (!iwn_sae_tx_commit_doorbell(sc, sae_request->ticket,
            ring->qid, saved_cur, next_cur, tx->id, totlen)) {
            ring->cur = saved_cur;
            explicit_bzero(desc, sizeof(*desc));
            mbuf_freem(data->m);
            data->m = NULL;
            data->ni = NULL;
            data->totlen = 0;
            data->ampdu_nframes = 0;
            data->ampdu_txmcs = 0;
            data->ampdu_rate_generation = 0;
            data->ampdu_rate_rflags = 0;
            data->ampdu_rate_feedback_valid = 0;
            data->tx_apple_nrate = 0;
            data->tx_apple_nrate_valid = 0;
            data->post_plti_trace_class = IWN_POST_PLTI_TRACE_TX_NONE;
            data->diag_subtype = 0xff;
            data->diag_auth_seq = 0xffff;
            explicit_bzero(data->diag_peer, sizeof(data->diag_peer));
            iwn_sae_tx_data_clear(data);
            return EIO;
        }
    } else if (sae_assoc_tx == IWN_SAE_ASSOC_TX_ADMITTED) {
        const int descriptor_idx = ring->cur;

        /* This is the actual direct-SAE Association Request linearization.
         * The helper repeats the public PMK claim under selected-BSS then
         * engine leaves before it publishes scheduler state and WRPTR. */
        if (!iwn_sae_engine_assoc_tx_commit(sc, ring, descriptor_idx,
            tx->id, totlen, ni, &sae_assoc_claim)) {
            explicit_bzero(desc, sizeof(*desc));
            mbuf_freem(data->m);
            data->m = NULL;
            data->ni = NULL;
            data->totlen = 0;
            data->ampdu_nframes = 0;
            data->ampdu_txmcs = 0;
            data->ampdu_rate_generation = 0;
            data->ampdu_rate_rflags = 0;
            data->ampdu_rate_feedback_valid = 0;
            data->tx_apple_nrate = 0;
            data->tx_apple_nrate_valid = 0;
            data->post_plti_trace_class = IWN_POST_PLTI_TRACE_TX_NONE;
            data->diag_subtype = 0xff;
            data->diag_auth_seq = 0xffff;
            explicit_bzero(data->diag_peer, sizeof(data->diag_peer));
            iwn_sae_tx_data_clear(data);
            explicit_bzero(&sae_assoc_claim, sizeof(sae_assoc_claim));
            return ECANCELED;
        }
        /* Task admission occurs only after the descriptor fence releases its
         * leaves.  The worker may now destroy the SAE engine but never before
         * this accepted Association Request is firmware-owned. */
        IWN_DIRECT_SAE_TRACE(ic,
            kAirportItlwmPostPltiTraceEventIwnDirectSaeAssocDescriptorAccepted);
        iwn_sae_engine_schedule_task(sc);
    } else {
        /* Existing non-SAE output keeps its historical scheduler ordering. */
        ops->update_sched(sc, ring->qid, ring->cur, tx->id, totlen);
        ring->cur = (ring->cur + 1) % IWN_TX_RING_COUNT;
        IWN_WRITE(sc, IWN_HBUS_TARG_WRPTR, ring->qid << 8 | ring->cur);
    }
    explicit_bzero(&sae_assoc_claim, sizeof(sae_assoc_claim));
    iwn_post_plti_trace_record_submit(ic, data->post_plti_trace_class);
    /* Legacy Open-System AUTH telemetry must not claim SAE Commit(seq=1). */
    if (sae_request == NULL && diag_subtype == IEEE80211_FC0_SUBTYPE_AUTH) {
        char auth_tx_path_buf[224];
        snprintf(auth_tx_path_buf, sizeof(auth_tx_path_buf),
            "subtype=0x%02x peer=%02x:%02x:%02x:%02x:%02x:%02x "
            "auth_seq=0x%04x qid=%d idx=%d wrptr=%d queued_before=%d "
            "flags=0x%08x txid=%u broadcast_txid=%u unicast=%u "
            "len=%d nsegs=%d",
            diag_subtype,
            diag_peer[0], diag_peer[1], diag_peer[2],
            diag_peer[3], diag_peer[4], diag_peer[5],
            (unsigned)diag_auth_seq, ring->qid, cmd->idx, ring->cur,
            ring->queued, (unsigned)flags, (unsigned)tx->id,
            (unsigned)sc->broadcast_id,
            (unsigned)(tx->id != sc->broadcast_id), totlen, nsegs);
        IWX_AUTH_DIAG("iwn_tx: MGT ring_kick %s\n", auth_tx_path_buf);
        getController()->setProperty("itlwm-iwn-auth-tx-path",
            auth_tx_path_buf);
        if (diag_auth_seq == 1) {
            if (sc->auth_seq1_tx_pending &&
                IEEE80211_ADDR_EQ(sc->auth_seq1_tx_bssid, diag_peer)) {
                char auth_txdone_guard_buf[256];
                snprintf(auth_txdone_guard_buf,
                    sizeof(auth_txdone_guard_buf),
                    "stage=guard_before_retry "
                    "decision=no_raw_txdone_before_retry "
                    "subtype=0x%02x peer=%02x:%02x:%02x:%02x:%02x:%02x "
                    "auth_seq=0x%04x pending_qid=%u pending_idx=%u "
                    "retry_qid=%d retry_idx=%d txid=%u "
                    "broadcast_txid=%u unicast=%u",
                    diag_subtype,
                    diag_peer[0], diag_peer[1], diag_peer[2],
                    diag_peer[3], diag_peer[4], diag_peer[5],
                    (unsigned)diag_auth_seq,
                    (unsigned)sc->auth_seq1_tx_qid,
                    (unsigned)sc->auth_seq1_tx_idx,
                    ring->qid, cmd->idx, (unsigned)tx->id,
                    (unsigned)sc->broadcast_id,
                    (unsigned)(tx->id != sc->broadcast_id));
                IWX_AUTH_DIAG("iwn_tx: AUTH TXDONE guard %s\n",
                    auth_txdone_guard_buf);
                getController()->setProperty(
                    "itlwm-iwn-auth-txdone-guard",
                    auth_txdone_guard_buf);
                {
                    /* DIAGNOSTIC (passthrough TX): does the firmware scheduler
                     * actually dequeue queue 0? Read the SCD read pointers and
                     * the byte-count table entry we wrote for the pending
                     * frame. rd0==0 while we kicked wrptr>0 => firmware never
                     * services q0; rd0 advanced => firmware dequeued but no
                     * TX_DONE (radio/PHY). bc_entry==0 => update_sched never
                     * wrote the byte count. */
                    if (iwn_nic_lock(sc) == 0) {
                        sc->dbg_scd_rd0 = iwn_prph_read(sc,
                            IWN5000_SCHED_QUEUE_RDPTR(0));
                        sc->dbg_scd_rd4 = iwn_prph_read(sc,
                            IWN5000_SCHED_QUEUE_RDPTR(4));
                        sc->dbg_scd_q0_now = iwn_prph_read(sc,
                            IWN5000_SCHED_QUEUE_STATUS(0));
                        iwn_nic_unlock(sc);
                    }
                    sc->dbg_wrptr_reg = IWN_READ(sc, IWN_HBUS_TARG_WRPTR);
                    sc->dbg_bc_entry = letoh16(sc->sched[0 *
                        IWN5000_SCHED_COUNT + sc->auth_seq1_tx_idx]);
                    char auth_txring_probe_buf[320];
                    snprintf(auth_txring_probe_buf,
                        sizeof(auth_txring_probe_buf),
                        "notif_count=%u txdone_count=%u "
                        "other_txdone_count=%u rx_mpdu_count=%u "
                        "lastnotif_qid=%u "
                        "lastnotif_type=%u last_txdone_qid=%u "
                        "last_txdone_idx=%u pending_qid=%u pending_idx=%u "
                        "scd_rd0=0x%x scd_rd4=0x%x scd_q0_now=0x%x "
                        "bc_entry=0x%x wrptr_reg=0x%x",
                        (unsigned)sc->auth_seq1_tx_notif_count,
                        (unsigned)sc->auth_seq1_tx_txdone_count,
                        (unsigned)sc->auth_seq1_tx_other_txdone_count,
                        (unsigned)sc->auth_seq1_rx_mpdu_count,
                        (unsigned)sc->auth_seq1_tx_lastnotif_qid,
                        (unsigned)sc->auth_seq1_tx_lastnotif_type,
                        (unsigned)sc->auth_seq1_tx_last_txdone_qid,
                        (unsigned)sc->auth_seq1_tx_last_txdone_idx,
                        (unsigned)sc->auth_seq1_tx_qid,
                        (unsigned)sc->auth_seq1_tx_idx,
                        (unsigned)sc->dbg_scd_rd0,
                        (unsigned)sc->dbg_scd_rd4,
                        (unsigned)sc->dbg_scd_q0_now,
                        (unsigned)sc->dbg_bc_entry,
                        (unsigned)sc->dbg_wrptr_reg);
                    IWX_AUTH_DIAG("iwn_tx: AUTH TXRING probe %s\n",
                        auth_txring_probe_buf);
                    getController()->setProperty(
                        "itlwm-iwn-auth-txring-probe",
                        auth_txring_probe_buf);
                }
            }
            sc->auth_seq1_tx_pending = 1;
            sc->auth_seq1_tx_qid = ring->qid;
            sc->auth_seq1_tx_idx = cmd->idx;
            IEEE80211_ADDR_COPY(sc->auth_seq1_tx_bssid, diag_peer);
            getController()->setProperty("itlwm-iwn-auth-tx-pending",
                auth_tx_path_buf);
        }
    }

    /* Mark TX ring as full if we reach a certain threshold. */
    if (++ring->queued > IWN_TX_RING_HIMARK) {
//        XYLog("%s sc->qfullmsk is FULL qid=%d ring->cur=%d ring->queued=%d\n", __FUNCTION__, ring->qid, ring->cur, ring->queued);
        sc->qfullmsk |= 1 << ring->qid;
    }

    return 0;
}

void ItlIwn::
iwn_start(struct _ifnet *ifp)
{
    struct iwn_softc *sc = (struct iwn_softc*)ifp->if_softc;
    ItlIwn *that = container_of(sc, ItlIwn, com);
    that->getMainCommandGate()->attemptAction(_iwn_start_task, &that->com.sc_ic.ic_ac.ac_if);
}

IOReturn ItlIwn::
_iwn_start_task(OSObject *target, void *arg0, void *arg1, void *arg2, void *arg3)
{
    struct _ifnet *ifp = (struct _ifnet *)arg0;
    struct iwn_softc *sc = (struct iwn_softc *)ifp->if_softc;
    ItlIwn *that = container_of(sc, ItlIwn, com);
    struct ieee80211com *ic = &sc->sc_ic;
    struct ieee80211_node *ni;
    mbuf_t m;

    if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
        return kIOReturnError;

    for (;;) {
        if (sc->qfullmsk != 0) {
            ifq_set_oactive(&ifp->if_snd);
            break;
        }

        /* Send pending management frames first. */
        m = mq_dequeue(&ic->ic_mgtq);
        if (m != NULL) {
//            ni = m->m_pkthdr.ph_cookie;
            ni = (struct ieee80211_node *)mbuf_pkthdr_rcvif(m);
            {
                struct ieee80211_frame *mwh =
                    mtod(m, struct ieee80211_frame *);
                uint8_t mtype = mwh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
                uint8_t msubtype = mwh->i_fc[0] &
                    IEEE80211_FC0_SUBTYPE_MASK;
                if (mtype == IEEE80211_FC0_TYPE_MGT) {
                    if (msubtype == IEEE80211_FC0_SUBTYPE_AUTH) {
                        AirportItlwmPostPltiTraceRecord(
                            ic, kAirportItlwmPostPltiTraceEventAuthDequeued);
                    } else if (msubtype == IEEE80211_FC0_SUBTYPE_ASSOC_REQ ||
                               msubtype == IEEE80211_FC0_SUBTYPE_REASSOC_REQ) {
                        AirportItlwmPostPltiTraceRecord(
                            ic, kAirportItlwmPostPltiTraceEventAssocDequeued);
                    }
                }
                if (mtype == IEEE80211_FC0_TYPE_MGT &&
                    msubtype == IEEE80211_FC0_SUBTYPE_AUTH) {
                    uint16_t auth_seq = 0xffff;
                    u_int hdrlen = ieee80211_get_hdrlen(mwh);
                    if (mbuf_len(m) >= (size_t)(hdrlen + 4)) {
                        const u_int8_t *auth_body =
                            (const u_int8_t *)mwh + hdrlen;
                        auth_seq = (uint16_t)(auth_body[2] |
                                              (auth_body[3] << 8));
                    }
                    char auth_drain_buf[192];
                    snprintf(auth_drain_buf, sizeof(auth_drain_buf),
                        "subtype=0x%02x peer=%02x:%02x:%02x:%02x:%02x:%02x "
                        "auth_seq=0x%04x queue=ic_mgtq qfullmsk=0x%x",
                        msubtype,
                        mwh->i_addr1[0], mwh->i_addr1[1],
                        mwh->i_addr1[2], mwh->i_addr1[3],
                        mwh->i_addr1[4], mwh->i_addr1[5],
                        (unsigned)auth_seq, sc->qfullmsk);
                    IWX_AUTH_DIAG("iwn_start: ic_mgtq drain MGT %s\n",
                        auth_drain_buf);
                    that->getController()->setProperty(
                        "itlwm-iwn-auth-mgtq-drain", auth_drain_buf);
                }
            }
            goto sendit;
        }
        if (
#ifndef AIRPORT
            ic->ic_state != IEEE80211_S_RUN ||
#endif
            (ic->ic_xflags & IEEE80211_F_TX_MGMT_ONLY))
            break;

        /* Encapsulate and send data frames. */
        m = ifq_dequeue(&ifp->if_snd);
        if (m == NULL)
            break;
#if NBPFILTER > 0
        if (ifp->if_bpf != NULL)
            bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif
        if ((m = ieee80211_encap(ifp, m, &ni)) == NULL)
            continue;
sendit:
#if NBPFILTER > 0
        if (ic->ic_rawbpf != NULL)
            bpf_mtap(ic->ic_rawbpf, m, BPF_DIRECTION_OUT);
#endif
        if (that->iwn_tx(sc, m, ni) != 0) {
            ieee80211_release_node(ic, ni);
            ifp->netStat->outputErrors++;
            continue;
        }
        ifp->netStat->outputPackets++;

        sc->sc_tx_timer = 5;
        ifp->if_timer = 1;
    }
    
    return kIOReturnSuccess;
}

void ItlIwn::
iwn_watchdog(struct _ifnet *ifp)
{
    struct iwn_softc *sc = (struct iwn_softc *)ifp->if_softc;
    ItlIwn *that = container_of(sc, ItlIwn, com);

    ifp->if_timer = 0;

    if (sc->sc_tx_timer > 0) {
        if (!that->iwn_tx_pending(sc)) {
            sc->sc_tx_timer = 0;
            goto done;
        }
        if (--sc->sc_tx_timer == 0) {
            int aggregateQueueId = -1;
            for (int qid = sc->first_agg_txq;
                 qid < sc->ntxqs; qid++) {
                struct iwn_tx_ring *candidate = &sc->txq[qid];
                if (candidate->queued != 0 &&
                    candidate->data[candidate->read].ap_data) {
                    aggregateQueueId = qid;
                    break;
                }
            }
            uint32_t panMgmtReadPointer = 0xffffffff;
            uint32_t panMgmtStatus = 0xffffffff;
            uint32_t queueChainMask = 0xffffffff;
            uint32_t schedulerInterruptMask = 0xffffffff;
            uint32_t schedulerTxFifos = 0xffffffff;
            uint32_t schedulerGpControl = 0xffffffff;
            uint32_t schedulerChainExtension = 0xffffffff;
            uint32_t schedulerAggregation = 0xffffffff;
            uint32_t schedulerEnableControl = 0xffffffff;
            uint32_t panMgmtContext1 = 0xffffffff;
            uint32_t panMgmtContext2 = 0xffffffff;
            uint32_t aggregateReadPointer = 0xffffffff;
            uint32_t aggregateStatus = 0xffffffff;
            uint32_t aggregateContext1 = 0xffffffff;
            uint32_t aggregateContext2 = 0xffffffff;
            uint32_t aggregateTranslationDword = 0xffffffff;
            if (sc->hw_type != IWN_HW_REV_TYPE_4965 &&
                iwn_nic_lock(sc) == 0) {
                panMgmtReadPointer = iwn_prph_read(
                    sc, IWN5000_SCHED_QUEUE_RDPTR(IWN_IPAN_MGMT_QUEUE));
                panMgmtStatus = iwn_prph_read(
                    sc, IWN5000_SCHED_QUEUE_STATUS(IWN_IPAN_MGMT_QUEUE));
                queueChainMask =
                    iwn_prph_read(sc, IWN5000_SCHED_QCHAIN_SEL);
                schedulerInterruptMask =
                    iwn_prph_read(sc, IWN5000_SCHED_INTR_MASK);
                schedulerTxFifos =
                    iwn_prph_read(sc, IWN5000_SCHED_TXFACT);
                schedulerGpControl =
                    iwn_prph_read(sc, IWN5000_SCHED_GP_CTRL);
                schedulerChainExtension =
                    iwn_prph_read(sc, IWN5000_SCHED_CHAINEXT_EN);
                schedulerAggregation =
                    iwn_prph_read(sc, IWN5000_SCHED_AGGR_SEL);
                schedulerEnableControl =
                    iwn_prph_read(sc, IWN5000_SCHED_EN_CTRL);
                panMgmtContext1 = iwn_mem_read(
                    sc, sc->sched_base +
                    IWN5000_SCHED_QUEUE_OFFSET(IWN_IPAN_MGMT_QUEUE));
                panMgmtContext2 = iwn_mem_read(
                    sc, sc->sched_base +
                    IWN5000_SCHED_QUEUE_OFFSET(IWN_IPAN_MGMT_QUEUE) + 4);
                if (aggregateQueueId >= sc->first_agg_txq &&
                    aggregateQueueId < sc->ntxqs) {
                    aggregateReadPointer = iwn_prph_read(
                        sc, IWN5000_SCHED_QUEUE_RDPTR(
                            aggregateQueueId));
                    aggregateStatus = iwn_prph_read(
                        sc, IWN5000_SCHED_QUEUE_STATUS(
                            aggregateQueueId));
                    aggregateContext1 = iwn_mem_read(
                        sc, sc->sched_base +
                        IWN5000_SCHED_QUEUE_OFFSET(aggregateQueueId));
                    aggregateContext2 = iwn_mem_read(
                        sc, sc->sched_base +
                        IWN5000_SCHED_QUEUE_OFFSET(aggregateQueueId) + 4);
                    const uint32_t translationAddress =
                        sc->sched_base +
                        IWN5000_SCHED_TRANS_TBL(aggregateQueueId);
                    aggregateTranslationDword = iwn_mem_read(
                        sc, translationAddress & ~3U);
                }
                iwn_nic_unlock(sc);
            }
            struct iwn_tx_ring *panMgmt =
                &sc->txq[IWN_IPAN_MGMT_QUEUE];
            struct iwn_tx_desc *panDescriptor =
                &panMgmt->desc[panMgmt->read];
            struct iwn_tx_cmd *panCommand =
                &panMgmt->cmd[panMgmt->read];
            struct iwn_cmd_data *panTx =
                reinterpret_cast<struct iwn_cmd_data *>(panCommand->data);
            struct iwn_tx_data *panData =
                &panMgmt->data[panMgmt->read];
            const uint16_t byteCount = letoh16(
                sc->sched[IWN_IPAN_MGMT_QUEUE * IWN5000_SCHED_COUNT +
                          panMgmt->read]);
            XYLog("%s: device timeout PAN management "
                  "queued=%d cur=%d read=%d scd_read=0x%08x "
                  "scd_status=0x%08x qchain=0x%08x intr=0x%08x "
                  "byte_count=0x%04x wrptr=0x%08x\n",
                  sc->sc_dev.dv_xname, panMgmt->queued, panMgmt->cur,
                  panMgmt->read, panMgmtReadPointer, panMgmtStatus,
                  queueChainMask, schedulerInterruptMask,
                  static_cast<unsigned>(byteCount),
                  IWN_READ(sc, IWN_HBUS_TARG_WRPTR));
            XYLog("%s: device timeout PAN transport "
                  "txfact=0x%08x gp=0x%08x ctx1=0x%08x ctx2=0x%08x "
                  "chainext=0x%08x aggr=0x%08x enctrl=0x%08x "
                  "fh5_config=0x%08x fh5_status=0x%08x "
                  "tssr=0x%08x txerr=0x%08x "
                  "cbbc7=0x%08x expected_cbbc7=0x%08x\n",
                  sc->sc_dev.dv_xname, schedulerTxFifos,
                  schedulerGpControl, panMgmtContext1, panMgmtContext2,
                  schedulerChainExtension, schedulerAggregation,
                  schedulerEnableControl,
                  IWN_READ(sc, IWN_FH_TX_CONFIG(5)),
                  IWN_READ(sc, IWN_FH_TXBUF_STATUS(5)),
                  IWN_READ(sc, IWN_FH_TX_STATUS),
                  IWN_READ(sc, IWN_FH_TX_ERROR),
                  IWN_READ(sc, IWN_FH_CBBC_QUEUE(IWN_IPAN_MGMT_QUEUE)),
                  static_cast<uint32_t>(panMgmt->desc_dma.paddr >> 8));
            XYLog("%s: device timeout PAN FH5 "
                  "tfbd0=0x%08x tfbd1=0x%08x sram=0x%08x\n",
                  sc->sc_dev.dv_xname,
                  IWN_READ(sc, IWN_FH_TFBD_CTRL0(5)),
                  IWN_READ(sc, IWN_FH_TFBD_CTRL1(5)),
                  IWN_READ(sc, IWN_FH_SRAM_ADDR(5)));
            XYLog("%s: device timeout PAN descriptor "
                  "desc_paddr=0x%llx cmd_paddr=0x%llx scratch=0x%llx "
                  "nsegs=%u seg0_addr=0x%08x seg0_len=0x%04x "
                  "seg1_addr=0x%08x seg1_len=0x%04x "
                  "seg2_addr=0x%08x seg2_len=0x%04x "
                  "cmd=%u/%u/%u txid=%u txlen=%u txflags=0x%08x "
                  "totlen=%d ap_mgmt=%u\n",
                  sc->sc_dev.dv_xname,
                  static_cast<unsigned long long>(
                      panMgmt->desc_dma.paddr),
                  static_cast<unsigned long long>(panData->cmd_paddr),
                  static_cast<unsigned long long>(
                      panMgmt->first_tb != NULL ?
                      panMgmt->first_tb_dma.paddr +
                          panMgmt->read * IWN_TX_FIRST_TB_STRIDE + 12 :
                      panData->scratch_paddr),
                  static_cast<unsigned>(panDescriptor->nsegs),
                  static_cast<unsigned>(letoh32(
                      panDescriptor->segs[0].addr)),
                  static_cast<unsigned>(letoh16(
                      panDescriptor->segs[0].len)),
                  static_cast<unsigned>(letoh32(
                      panDescriptor->segs[1].addr)),
                  static_cast<unsigned>(letoh16(
                      panDescriptor->segs[1].len)),
                  static_cast<unsigned>(letoh32(
                      panDescriptor->segs[2].addr)),
                  static_cast<unsigned>(letoh16(
                      panDescriptor->segs[2].len)),
                  static_cast<unsigned>(panCommand->code),
                  static_cast<unsigned>(panCommand->qid),
                  static_cast<unsigned>(panCommand->idx),
                  static_cast<unsigned>(panTx->id),
                  static_cast<unsigned>(letoh16(panTx->len)),
                  static_cast<unsigned>(letoh32(panTx->flags)),
                  panData->totlen,
                  static_cast<unsigned>(panData->ap_mgmt));
            if (aggregateQueueId >= sc->first_agg_txq &&
                aggregateQueueId < sc->ntxqs) {
                struct iwn_tx_ring *aggregate =
                    &sc->txq[aggregateQueueId];
                const int aggregateIndex = aggregate->read;
                struct iwn_tx_desc *aggregateDescriptor =
                    &aggregate->desc[aggregateIndex];
                struct iwn_tx_cmd *aggregateCommand =
                    &aggregate->cmd[aggregateIndex];
                struct iwn_cmd_data *aggregateTx =
                    reinterpret_cast<struct iwn_cmd_data *>(
                        aggregateCommand->data);
                struct iwn_tx_data *aggregateData =
                    &aggregate->data[aggregateIndex];
                const uint16_t aggregateByteCount = letoh16(
                    sc->sched[aggregateQueueId * IWN5000_SCHED_COUNT +
                              aggregateIndex]);
                const uint16_t aggregateRaTid = static_cast<uint16_t>(
                    aggregateQueueId & 1 ?
                    aggregateTranslationDword >> 16 :
                    aggregateTranslationDword);
                const struct ieee80211_frame *aggregateHeader =
                    reinterpret_cast<const struct ieee80211_frame *>(
                        aggregateTx + 1);
                const unsigned aggregateFifo =
                    static_cast<unsigned>(aggregateStatus & 7U);
                XYLog("%s: device timeout AP aggregate qid=%d "
                      "queued=%d cur=%d read=%d scd_read=0x%08x "
                      "status=0x%08x fifo=%u ctx1=0x%08x ctx2=0x%08x "
                      "ratid=0x%04x byte_count=0x%04x "
                      "cbbc=0x%08x expected_cbbc=0x%08x\n",
                      sc->sc_dev.dv_xname, aggregateQueueId,
                      aggregate->queued, aggregate->cur, aggregate->read,
                      aggregateReadPointer, aggregateStatus, aggregateFifo,
                      aggregateContext1, aggregateContext2,
                      static_cast<unsigned>(aggregateRaTid),
                      static_cast<unsigned>(aggregateByteCount),
                      IWN_READ(sc, IWN_FH_CBBC_QUEUE(aggregateQueueId)),
                      static_cast<uint32_t>(
                          aggregate->desc_dma.paddr >> 8));
                XYLog("%s: device timeout AP aggregate descriptor "
                      "nsegs=%u seg0=0x%08x/0x%04x "
                      "seg1=0x%08x/0x%04x seg2=0x%08x/0x%04x "
                      "cmd=%u/%u/%u txid=%u tid=%u security=0x%02x "
                      "txlen=%u txflags=0x%08x seq=%u totlen=%d "
                      "ap_data=%u\n",
                      sc->sc_dev.dv_xname,
                      static_cast<unsigned>(aggregateDescriptor->nsegs),
                      static_cast<unsigned>(letoh32(
                          aggregateDescriptor->segs[0].addr)),
                      static_cast<unsigned>(letoh16(
                          aggregateDescriptor->segs[0].len)),
                      static_cast<unsigned>(letoh32(
                          aggregateDescriptor->segs[1].addr)),
                      static_cast<unsigned>(letoh16(
                          aggregateDescriptor->segs[1].len)),
                      static_cast<unsigned>(letoh32(
                          aggregateDescriptor->segs[2].addr)),
                      static_cast<unsigned>(letoh16(
                          aggregateDescriptor->segs[2].len)),
                      static_cast<unsigned>(aggregateCommand->code),
                      static_cast<unsigned>(aggregateCommand->qid),
                      static_cast<unsigned>(aggregateCommand->idx),
                      static_cast<unsigned>(aggregateTx->id),
                      static_cast<unsigned>(aggregateTx->tid),
                      static_cast<unsigned>(aggregateTx->security),
                      static_cast<unsigned>(letoh16(aggregateTx->len)),
                      static_cast<unsigned>(letoh32(aggregateTx->flags)),
                      static_cast<unsigned>(
                          LE_READ_2(aggregateHeader->i_seq) >>
                          IEEE80211_SEQ_SEQ_SHIFT),
                      aggregateData->totlen,
                      aggregateData->ap_data ? 1U : 0U);
            }
            for (int qid = 0; qid < sc->ntxqs; qid++) {
                if (sc->txq[qid].queued != 0) {
                    XYLog("%s: device timeout pending qid=%d "
                          "queued=%d cur=%d read=%d\n",
                          sc->sc_dev.dv_xname, qid,
                          sc->txq[qid].queued, sc->txq[qid].cur,
                          sc->txq[qid].read);
                }
            }
            /* Preserve a validated SAE ESS across this internal firmware
             * reset.  Ordinary power-off paths never set this marker. */
            that->iwn_sae_driver_reset_recovery_prepare(sc);
            that->iwn_stop(ifp);
            task_add(systq, &sc->init_task);
            ifp->netStat->outputErrors++;
            return;
        }
        ifp->if_timer = 1;
    }

done:
    ieee80211_watchdog(ifp);
}

int ItlIwn::
iwn_ioctl(struct _ifnet *ifp, u_long cmd, caddr_t data)
{
    struct iwn_softc *sc = (struct iwn_softc *)ifp->if_softc;
    struct ieee80211com *ic = &sc->sc_ic;
    ItlIwn *that = container_of(sc, ItlIwn, com);
    int s, error = 0;

//    error = rw_enter(&sc->sc_rwlock, RW_WRITE | RW_INTR);
    if (error)
        return error;
    s = splnet();

    switch (cmd) {
    case SIOCSIFADDR:
        ifp->if_flags |= IFF_UP;
        /* FALLTHROUGH */
    case SIOCSIFFLAGS:
        if (ifp->if_flags & IFF_UP) {
            if (!(ifp->if_flags & IFF_RUNNING))
                error = that->iwn_init(ifp);
        } else {
            if (ifp->if_flags & IFF_RUNNING)
                that->iwn_stop(ifp);
        }
        break;

    case SIOCS80211POWER:
        error = ieee80211_ioctl(ifp, cmd, data);
        if (error != ENETRESET)
            break;
        if (ic->ic_state == IEEE80211_S_RUN &&
            sc->calib.state == IWN_CALIB_STATE_RUN) {
            if (ic->ic_flags & IEEE80211_F_PMGTON)
                error = that->iwn_set_pslevel(sc, 0, 3, 0);
            else    /* back to CAM */
                error = that->iwn_set_pslevel(sc, 0, 0, 0);
        } else {
            /* Defer until transition to IWN_CALIB_STATE_RUN. */
            error = 0;
        }
        break;

    default:
        error = ieee80211_ioctl(ifp, cmd, data);
    }

    if (error == ENETRESET) {
        error = 0;
        if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
            (IFF_UP | IFF_RUNNING)) {
            that->iwn_stop(ifp);
            error = that->iwn_init(ifp);
        }
    }

    splx(s);
//    rw_exit_write(&sc->sc_rwlock);
    return error;
}

/*
 * Send a command to the firmware.
 */
int ItlIwn::
iwn_set_cmd_in_flight(struct iwn_softc *sc)
{
    int transitionTries;

    /*
     * 4965 does not need the APMG host-command wake workaround.  DVM
     * devices from 5000 onward do: keep MAC_ACCESS_REQ asserted from the
     * first HCMD doorbell until firmware reclaims the final descriptor.
     */
    if (sc->hw_type == IWN_HW_REV_TYPE_4965)
        return 0;

    for (transitionTries = 0; transitionTries < 3000;
         transitionTries++) {
        int32_t state = __atomic_load_n(
            &sc->sc_cmd_in_flight, __ATOMIC_ACQUIRE);
        if (state > 0) {
            if (state == INT32_MAX)
                return EBUSY;
            int32_t expected = state;
            if (__atomic_compare_exchange_n(
                    &sc->sc_cmd_in_flight, &expected, state + 1, false,
                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                return 0;
            continue;
        }
        if (state < 0) {
            DELAY(10);
            continue;
        }

        int32_t expected = 0;
        if (!__atomic_compare_exchange_n(
                &sc->sc_cmd_in_flight, &expected, -1, false,
                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            continue;

        IWN_SETBITS(sc, IWN_GP_CNTRL, IWN_GP_CNTRL_MAC_ACCESS_REQ);
        for (int wakeTries = 0; wakeTries < 1500; wakeTries++) {
            if ((IWN_READ(sc, IWN_GP_CNTRL) &
                 (IWN_GP_CNTRL_MAC_ACCESS_ENA |
                  IWN_GP_CNTRL_SLEEP)) ==
                IWN_GP_CNTRL_MAC_ACCESS_ENA) {
                __atomic_store_n(
                    &sc->sc_cmd_in_flight, 1, __ATOMIC_RELEASE);
                return 0;
            }
            DELAY(10);
        }

        IWN_CLRBITS(sc, IWN_GP_CNTRL, IWN_GP_CNTRL_MAC_ACCESS_REQ);
        __atomic_store_n(&sc->sc_cmd_in_flight, 0, __ATOMIC_RELEASE);
        XYLog("%s: failed to wake NIC for host command\n",
              sc->sc_dev.dv_xname);
        return ETIMEDOUT;
    }

    XYLog("%s: host-command wake transition timed out\n",
          sc->sc_dev.dv_xname);
    return ETIMEDOUT;
}

void ItlIwn::
iwn_clear_cmd_in_flight(struct iwn_softc *sc)
{
    if (sc->hw_type == IWN_HW_REV_TYPE_4965)
        return;

    for (int transitionTries = 0; transitionTries < 3000;
         transitionTries++) {
        int32_t state = __atomic_load_n(
            &sc->sc_cmd_in_flight, __ATOMIC_ACQUIRE);
        if (state == 0)
            return;
        if (state < 0) {
            DELAY(10);
            continue;
        }
        if (state > 1) {
            int32_t expected = state;
            if (__atomic_compare_exchange_n(
                    &sc->sc_cmd_in_flight, &expected, state - 1, false,
                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                return;
            continue;
        }

        int32_t expected = 1;
        if (!__atomic_compare_exchange_n(
                &sc->sc_cmd_in_flight, &expected, -2, false,
                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            continue;
        IWN_CLRBITS(sc, IWN_GP_CNTRL, IWN_GP_CNTRL_MAC_ACCESS_REQ);
        __atomic_store_n(&sc->sc_cmd_in_flight, 0, __ATOMIC_RELEASE);
        return;
    }

    XYLog("%s: host-command wake release timed out\n",
          sc->sc_dev.dv_xname);
}

int ItlIwn::
iwn_cmd(struct iwn_softc *sc, int code, const void *buf, int size, int async)
{
    return iwn_cmd_with_doorbell_hook(sc, code, buf, size, async,
                                      NULL, NULL, NULL);
}

int ItlIwn::
iwn_cmd_with_doorbell_hook(struct iwn_softc *sc, int code, const void *buf,
                           int size, int async,
                           bool (*pre_doorbell)(struct iwn_softc *, void *),
                           void (*post_doorbell)(struct iwn_softc *, void *),
                           void *doorbell_context)
{
    struct iwn_ops *ops = &sc->ops;
    struct iwn_tx_ring *ring = &sc->txq[sc->command_queue];
    struct iwn_tx_desc *desc;
    struct iwn_tx_data *data;
    struct iwn_tx_cmd *cmd;
    mbuf_t m = NULL;
    bus_addr_t paddr;
    int totlen, error = 0;
    unsigned int max_chunks = 1;
    IOPhysicalSegment seg;

    desc = &ring->desc[ring->cur];
    data = &ring->data[ring->cur];
    totlen = 4 + size;

    if (size > sizeof cmd->data) {
        /* Command is too large to fit in a descriptor. */
        if (totlen > MCLBYTES)
            return EINVAL;
//        MGETHDR(m, M_DONTWAIT, MT_DATA);
//        if (totlen > MHLEN) {
//            MCLGET(m, M_DONTWAIT);
//            if (!(m->m_flags & M_EXT)) {
//                mbuf_freem(m);
//                return ENOMEM;
//            }
//        }
        mbuf_allocpacket(MBUF_WAITOK, totlen, &max_chunks, &m);
        if (m == NULL) {
            XYLog("%s: could not get fw cmd mbuf (%zd bytes)\n",
                  DEVNAME(sc), totlen);
            return ENOMEM;
        }
        mbuf_setlen(m, totlen);
        mbuf_pkthdr_setlen(m, totlen);

        cmd = mtod(m, struct iwn_tx_cmd *);
//        error = bus_dmamap_load(sc->sc_dmat, data->map, cmd, totlen,
//            NULL, BUS_DMA_NOWAIT | BUS_DMA_WRITE);
//        if (error != 0) {
//            mbuf_freem(m);
//            return error;
//        }
        data->map->dm_nsegs = data->map->cursor->getPhysicalSegmentsWithCoalesce(m, &seg, 1);
        if (data->map->dm_nsegs == 0) {
            XYLog("%s: could not load fw cmd mbuf (%zd bytes)\n",
                  DEVNAME(sc), totlen);
            mbuf_freem(m);
            return ENOMEM;
        }
        data->m = m;
        paddr = seg.location;
    } else {
        cmd = &ring->cmd[ring->cur];
        paddr = data->cmd_paddr;
    }

    cmd->code = code;
    cmd->flags = 0;
    cmd->qid = ring->qid;
    cmd->idx = ring->cur;
    memcpy(cmd->data, buf, size);

    desc->nsegs = 1;
    desc->segs[0].addr = htole32(IWN_LOADDR(paddr));
    desc->segs[0].len  = htole16(IWN_HIADDR(paddr) | totlen << 4);

//    if (size > sizeof cmd->data) {
//        bus_dmamap_sync(sc->sc_dmat, data->map, 0, totlen,
//            BUS_DMASYNC_PREWRITE);
//    } else {
//        bus_dmamap_sync(sc->sc_dmat, ring->cmd_dma.map,
//            (caddr_t)cmd - ring->cmd_dma.vaddr, totlen,
//            BUS_DMASYNC_PREWRITE);
//    }
//    bus_dmamap_sync(sc->sc_dmat, ring->desc_dma.map,
//        (caddr_t)desc - ring->desc_dma.vaddr, sizeof (*desc),
//        BUS_DMASYNC_PREWRITE);

    /* Wake/reserve the command transport before an optional pre-doorbell
     * hook takes an IRQ-safe ownership fence.  A sleeping or stopped NIC can
     * reject this step; in that case no hook has run and no simple lock can
     * escape into the caller's taskq thread. */
    error = iwn_set_cmd_in_flight(sc);
    if (error != 0) {
        if (m != NULL) {
            explicit_bzero(cmd, totlen);
            mbuf_freem(m);
            data->m = NULL;
            data->map->dm_nsegs = 0;
        } else {
            explicit_bzero(cmd, sizeof(*cmd));
        }
        explicit_bzero(desc, sizeof(*desc));
        return error;
    }

    /* A scan lease can make the final ownership decision only here: all
     * allocation, descriptor construction, and transport wake are complete,
     * while the command remains invisible to firmware.  Its post hook
     * releases the IRQ-safe fence only after the real WRPTR write below. */
    if (pre_doorbell != NULL && !(*pre_doorbell)(sc, doorbell_context)) {
        iwn_clear_cmd_in_flight(sc);
        if (m != NULL) {
            explicit_bzero(cmd, totlen);
            mbuf_freem(m);
            data->m = NULL;
            data->map->dm_nsegs = 0;
        } else {
            explicit_bzero(cmd, sizeof(*cmd));
        }
        explicit_bzero(desc, sizeof(*desc));
        return ECANCELED;
    }

    const int submittedIndex = ring->cur;

    /* Update TX scheduler. */
    ops->update_sched(sc, ring->qid, submittedIndex, 0, 0);

    /* Kick command ring. */
    ring->cur = (submittedIndex + 1) % IWN_TX_RING_COUNT;
    IWN_WRITE(sc, IWN_HBUS_TARG_WRPTR, ring->qid << 8 | ring->cur);
    if (post_doorbell != NULL)
        (*post_doorbell)(sc, doorbell_context);

    return async ? 0 : tsleep_nsec(desc, PCATCH, "iwncmd", SEC_TO_NSEC(1));
}

int ItlIwn::
iwn4965_add_node(struct iwn_softc *sc, struct iwn_node_info *node, int async)
{
    ItlIwn *that = container_of(sc, ItlIwn, com);
    struct iwn4965_node_info hnode;
    caddr_t src, dst;

    /*
     * We use the node structure for 5000 Series internally (it is
     * a superset of the one for 4965AGN). We thus copy the common
     * fields before sending the command.
     */
    src = (caddr_t)node;
    dst = (caddr_t)&hnode;
    memcpy(dst, src, 48);
    /* Skip TSC, RX MIC and TX MIC fields from ``src''. */
    memcpy(dst + 48, src + 72, 20);
    return that->iwn_cmd(sc, IWN_CMD_ADD_NODE, &hnode, sizeof hnode, async);
}

int ItlIwn::
iwn5000_add_node(struct iwn_softc *sc, struct iwn_node_info *node, int async)
{
    ItlIwn *that = container_of(sc, ItlIwn, com);
    
    /* Direct mapping. */
    return that->iwn_cmd(sc, IWN_CMD_ADD_NODE, node, sizeof (*node), async);
}

int ItlIwn::
iwn_add_bss_node(struct iwn_softc *sc, struct ieee80211_node *ni)
{
    struct iwn_ops *ops = &sc->ops;
    struct ieee80211com *ic = &sc->sc_ic;
    struct iwn_node *wn = (struct iwn_node *)ni;
    struct iwn_node_info node;
    int error;

    wn->id = IWN_ID_BSS;
    iwn_newassoc(ic, ni, 1);

    memset(&node, 0, sizeof node);
    IEEE80211_ADDR_COPY(node.macaddr, ni->ni_macaddr);
    node.id = IWN_ID_BSS;
    if (ni->ni_flags & IEEE80211_NODE_HT) {
        node.htmask = (IWN_AMDPU_SIZE_FACTOR_MASK |
            IWN_AMDPU_DENSITY_MASK);
        node.htflags = htole32(
            IWN_AMDPU_SIZE_FACTOR(
            (ic->ic_ampdu_params & IEEE80211_AMPDU_PARAM_LE)) |
            IWN_AMDPU_DENSITY(
            (ic->ic_ampdu_params & IEEE80211_AMPDU_PARAM_SS) >> 2));

        if (iwn_rxon_ht40_enabled(sc))
            node.htflags |= htole32(IWN_40MHZ_ENABLE);
    }
    error = ops->add_node(sc, &node, 1);
    if (error != 0)
        return error;

    IEEE80211_ADDR_COPY(sc->bss_node_addr, ni->ni_macaddr);

    return iwn_set_link_quality(sc, ni);
}

int ItlIwn::
iwn_set_link_quality(struct iwn_softc *sc, struct ieee80211_node *ni)
{
    struct ieee80211com *ic = &sc->sc_ic;
    struct iwn_node *wn = (struct iwn_node *)ni;
    struct iwn_cmd_link_quality linkq;
    struct ieee80211_rateset *rs = &ni->ni_rates;
    uint8_t txant;
    bool ht40Enabled;
    bool sgiEnabled;
    int i, ridx, ridx_min, ridx_max, j, mimo, tab = 0, rflags = 0;

    /* Use the first valid TX antenna. */
    txant = IWN_LSB(sc->txchainmask);

    memset(&linkq, 0, sizeof linkq);
    linkq.id = wn->id;
    linkq.antmsk_1stream = txant;
    linkq.antmsk_2stream = IWN_ANT_AB;
    /* Firmware exposes one aggregate frame-count limit per station even if
     * the peer negotiated different reorder windows on individual TIDs.
     * Publish the minimum active window, clamped to DVM's maximum of 63. */
    uint8_t aggregateLimit = IWN_AMPDU_MAX;
    bool txAggregationActive = false;
    if (ni->ni_flags & IEEE80211_NODE_HT) {
        for (uint8_t tid = 0; tid < IWN_NUM_AMPDU_TID; tid++) {
            if (sc->sc_tx_ba[tid].wn != wn)
                continue;
            txAggregationActive = true;
            uint8_t negotiated = static_cast<uint8_t>(MIN(
                ni->ni_tx_ba[tid].ba_winsize,
                static_cast<uint16_t>(IWN_AMPDU_MAX)));
            if (negotiated == 0)
                negotiated = IWN_AMPDU_MAX;
            aggregateLimit = MIN(aggregateLimit, negotiated);
        }
    }
    linkq.ampdu_max = aggregateLimit;
    linkq.ampdu_threshold = 3;
    linkq.ampdu_limit = htole16(4000);    /* 4ms */
    ht40Enabled = iwn_rxon_ht40_enabled(sc);
    sgiEnabled = IwnHt40Contracts::allowsSgiForEffectiveHtWidth(
        ht40Enabled,
        ieee80211_node_supports_ht_sgi20(ni),
        ieee80211_node_supports_ht_sgi40(ni));

    /* The matching post-ADDBA half of DVM aggregate protection.  This must
     * follow the hardware queue transition; legacy USEPROT is unrelated. */
    if (txAggregationActive && iwn_dvm_use_rts_for_aggregation(sc))
        linkq.flags |= IWN_LINK_QUAL_FLAGS_SET_STA_TLC_RTS;
    
    /*
     * Fill the LQ rate selection table with legacy and/or HT rates
     * in descending order, i.e. with the node's current TX rate first.
     * In cases where throughput of an HT rate corresponds to a legacy
     * rate it makes no sense to add both. We rely on the fact that
     * iwn_rates is laid out such that equivalent HT/legacy rates share
     * the same IWN_RATE_*_INDEX value. Also, rates not applicable to
     * legacy/HT are assumed to be marked with an 'invalid' PLCP value.
     */
    j = 0;
    ridx_min = iwn_rval2ridx(ieee80211_min_basic_rate(ic));
    mimo = iwn_is_mimo_mcs(ni->ni_txmcs);
    ridx_max = (mimo ? IWN_LAST_HT_RATE : IWN_LAST_HT_SISO_RATE);
    for (ridx = ridx_max; ridx >= ridx_min; ridx--) {
        uint8_t plcp = iwn_rates[ridx].plcp;
        uint8_t ht_plcp = iwn_rates[ridx].ht_plcp;
        
        if (j >= IWN_MAX_TX_RETRIES)
            break;
        tab = 0;
        rflags = 0;
        if (ni->ni_flags & IEEE80211_NODE_HT) {
            if (ht_plcp == IWN_RATE_HT_SISO_MCS_INV_PLCP)
                continue;
            /* Do not mix SISO and MIMO HT rates. */
            if ((mimo && !iwn_is_mimo_ht_plcp(ht_plcp)) ||
                (!mimo && iwn_is_mimo_ht_plcp(ht_plcp)))
                continue;
            for (i = ni->ni_txmcs; i >= 0; i--) {
                if (ic->ic_tx_mcs_set == IEEE80211_TX_MCS_SET_DEFINED &&
                    isclr(ni->ni_rxmcs, i))
                    continue;
                if (ridx != iwn_mcs2ridx[i])
                    continue;
                tab = ht_plcp;
                rflags |= IWN_RFLAG_MCS;
                /* First two Tx attempts may use 40MHz/SGI. */
                if (j > 1)
                    break;
                if (ht40Enabled)
                    rflags |= IWN_RFLAG_HT40;
                if (sgiEnabled)
                    rflags |= IWN_RFLAG_SGI;
            }
        } else if (plcp != IWN_RATE_INVM_PLCP) {
            for (i = ni->ni_txrate; i >= 0; i--) {
                if (iwn_rates[ridx].rate == (rs->rs_rates[i] &
                                             IEEE80211_RATE_VAL)) {
                    tab = plcp;
                    break;
                }
            }
        }
        
        if (tab == 0 && rflags == 0)
            continue;
        
        if (iwn_is_mimo_ht_plcp(ht_plcp))
            rflags |= IWN_RFLAG_ANT(sc->txchainmask);
        else
            rflags |= IWN_RFLAG_ANT(txant);
        
        if (IWN_RIDX_IS_CCK(ridx))
            rflags |= IWN_RFLAG_CCK;

        linkq.retry[j].plcp = tab;
        linkq.retry[j].rflags = rflags;
        j++;
    }
    
    linkq.mimo = (mimo ? j : 0);
    
    /* Fill the rest with the lowest possible rate */
    while (j < IWN_MAX_TX_RETRIES) {
        tab = iwn_rates[ridx_min].plcp;
        rflags = 0;
        if (IWN_RIDX_IS_CCK(ridx_min))
            rflags |= IWN_RFLAG_CCK;
        rflags |= IWN_RFLAG_ANT(txant);
        linkq.retry[j].plcp = tab;
        linkq.retry[j].rflags = rflags;
        j++;
    }

    return iwn_cmd(sc, IWN_CMD_LINK_QUALITY, &linkq, sizeof linkq, 1);
}

/*
 * Broadcast node is used to send group-addressed and management frames.
 */
int ItlIwn::
iwn_add_broadcast_node(struct iwn_softc *sc, int async, int ridx)
{
    struct iwn_ops *ops = &sc->ops;
    struct iwn_node_info node;
    struct iwn_cmd_link_quality linkq;
    const struct iwn_rate *rinfo;
    uint8_t txant;
    int i, error;

    memset(&node, 0, sizeof node);
    IEEE80211_ADDR_COPY(node.macaddr, etherbroadcastaddr);
    node.id = sc->broadcast_id;
    if ((error = ops->add_node(sc, &node, async)) != 0)
        return error;

    /* Use the first valid TX antenna. */
    txant = IWN_LSB(sc->txchainmask);

    memset(&linkq, 0, sizeof linkq);
    linkq.id = sc->broadcast_id;
    linkq.antmsk_1stream = txant;
    linkq.antmsk_2stream = IWN_ANT_AB;
    linkq.ampdu_max = IWN_AMPDU_MAX_NO_AGG;
    linkq.ampdu_threshold = 3;
    linkq.ampdu_limit = htole16(4000);    /* 4ms */

    /* Use lowest mandatory bit-rate. */
    rinfo = &iwn_rates[ridx];
    linkq.retry[0].plcp = rinfo->plcp;
    if (IWN_RIDX_IS_CCK(ridx))
        linkq.retry[0].rflags = IWN_RFLAG_CCK;
    linkq.retry[0].rflags |= IWN_RFLAG_ANT(txant);
    /* Use same bit-rate for all TX retries. */
    for (i = 1; i < IWN_MAX_TX_RETRIES; i++) {
        linkq.retry[i].plcp = linkq.retry[0].plcp;
        linkq.retry[i].rflags = linkq.retry[0].rflags;
    }
    return iwn_cmd(sc, IWN_CMD_LINK_QUALITY, &linkq, sizeof linkq, async);
}

void ItlIwn::
iwn_updateedca(struct ieee80211com *ic)
{
#define IWN_EXP2(x)    ((1 << (x)) - 1)    /* CWmin = 2^ECWmin - 1 */
    struct iwn_softc *sc = (struct iwn_softc *)ic->ic_softc;
    struct ieee80211_node *ni = ic->ic_bss;
    ItlIwn *that = container_of(sc, ItlIwn, com);
    struct iwn_edca_params cmd;
    int aci;

    memset(&cmd, 0, sizeof cmd);
    for (aci = 0; aci < EDCA_NUM_AC; aci++) {
        const struct ieee80211_edca_ac_params *ac =
            &ic->ic_edca_ac[aci];
        cmd.ac[aci].aifsn = ac->ac_aifsn;
        cmd.ac[aci].cwmin = htole16(IWN_EXP2(ac->ac_ecwmin));
        cmd.ac[aci].cwmax = htole16(IWN_EXP2(ac->ac_ecwmax));
        cmd.ac[aci].txoplimit =
            htole16(IEEE80211_TXOP_TO_US(ac->ac_txoplimit));
    }
    if (ni->ni_flags & IEEE80211_NODE_QOS)
        cmd.flags |= htole32(IWN_EDCA_UPDATE);

    if (ni->ni_flags & IEEE80211_NODE_HT)
        cmd.flags |= htole32(IWN_EDCA_FLG_TGN);

    (void)that->iwn_cmd(sc, IWN_CMD_EDCA_PARAMS, &cmd, sizeof cmd, 1);
#undef IWN_EXP2
}

void ItlIwn::
iwn_set_led(struct iwn_softc *sc, uint8_t which, uint8_t off, uint8_t on)
{
    struct iwn_cmd_led led;

    /* Clear microcode LED ownership. */
    IWN_CLRBITS(sc, IWN_LED, IWN_LED_BSM_CTRL);

    led.which = which;
    led.unit = htole32(10000);    /* on/off in unit of 100ms */
    led.off = off;
    led.on = on;
    (void)iwn_cmd(sc, IWN_CMD_SET_LED, &led, sizeof led, 1);
}

/*
 * Set the critical temperature at which the firmware will stop the radio
 * and notify us.
 */
int ItlIwn::
iwn_set_critical_temp(struct iwn_softc *sc)
{
    struct iwn_critical_temp crit;
    int32_t temp;

    IWN_WRITE(sc, IWN_UCODE_GP1_CLR, IWN_UCODE_GP1_CTEMP_STOP_RF);

    if (sc->hw_type == IWN_HW_REV_TYPE_5150)
        temp = (IWN_CTOK(110) - sc->temp_off) * -5;
    else if (sc->hw_type == IWN_HW_REV_TYPE_4965)
        temp = IWN_CTOK(110);
    else
        temp = 110;
    memset(&crit, 0, sizeof crit);
    crit.tempR = htole32(temp);
    return iwn_cmd(sc, IWN_CMD_SET_CRITICAL_TEMP, &crit, sizeof crit, 0);
}

int ItlIwn::
iwn_set_timing(struct iwn_softc *sc, struct ieee80211_node *ni)
{
    struct iwn_cmd_timing cmd;
    uint64_t val, mod;

    memset(&cmd, 0, sizeof cmd);
    memcpy(&cmd.tstamp, ni->ni_tstamp, sizeof (uint64_t));
    cmd.bintval = htole16(ni->ni_intval);
    cmd.lintval = htole16(10);

    /* Compute remaining time until next beacon. */
    val = (uint64_t)ni->ni_intval * IEEE80211_DUR_TU;
    mod = letoh64(cmd.tstamp) % val;
    cmd.binitval = htole32((uint32_t)(val - mod));

    return iwn_cmd(sc, IWN_CMD_TIMING, &cmd, sizeof cmd, 1);
}

void ItlIwn::
iwn4965_power_calibration(struct iwn_softc *sc, int temp)
{
    /* Adjust TX power if need be (delta >= 3 degC). */
    if (abs(temp - sc->temp) >= 3) {
        /* Record temperature of last calibration. */
        sc->temp = temp;
        (void)iwn4965_set_txpower(sc, 1);
    }
}

/*
 * Set TX power for current channel (each rate has its own power settings).
 * This function takes into account the regulatory information from EEPROM,
 * the current temperature and the current voltage.
 */
int ItlIwn::
iwn4965_set_txpower(struct iwn_softc *sc, int async)
{
/* Fixed-point arithmetic division using a n-bit fractional part. */
#define fdivround(a, b, n)    \
    ((((1 << n) * (a)) / (b) + (1 << n) / 2) / (1 << n))
/* Linear interpolation. */
#define interpolate(x, x1, y1, x2, y2, n)    \
    ((y1) + fdivround(((int)(x) - (x1)) * ((y2) - (y1)), (x2) - (x1), n))

    static const int tdiv[IWN_NATTEN_GROUPS] = { 9, 8, 8, 8, 6 };
    struct ieee80211com *ic = &sc->sc_ic;
    ItlIwn *that = container_of(sc, ItlIwn, com);
    struct iwn_ucode_info *uc = &sc->ucode_info;
    struct ieee80211_channel *ch;
    struct iwn4965_cmd_txpower cmd;
    struct iwn4965_eeprom_chan_samples *chans;
    const uint8_t *rf_gain, *dsp_gain;
    int32_t vdiff, tdiff;
    int i, c, grp, maxpwr, is_ht40 = 0;
    uint8_t chan, ext_chan;
    unsigned int ht40chan;

    /* Retrieve current channel from last RXON. */
    chan = sc->rxon.chan;
    ch = &ic->ic_channels[chan];

    memset(&cmd, 0, sizeof cmd);
    cmd.band = IEEE80211_IS_CHAN_5GHZ(ch) ? 0 : 1;
    cmd.chan = chan;

    if (IEEE80211_IS_CHAN_5GHZ(ch)) {
        maxpwr   = sc->maxpwr5GHz;
        rf_gain  = iwn4965_rf_gain_5ghz;
        dsp_gain = iwn4965_dsp_gain_5ghz;
    } else {
        maxpwr   = sc->maxpwr2GHz;
        rf_gain  = iwn4965_rf_gain_2ghz;
        dsp_gain = iwn4965_dsp_gain_2ghz;
    }

    /* Compute voltage compensation. */
    vdiff = ((int32_t)letoh32(uc->volt) - sc->eeprom_voltage) / 7;
    if (vdiff > 0)
        vdiff *= 2;
    if (abs(vdiff) > 2)
        vdiff = 0;
    /* Get channel attenuation group. */
    if (chan <= 20)        /* 1-20 */
        grp = 4;
    else if (chan <= 43)    /* 34-43 */
        grp = 0;
    else if (chan <= 70)    /* 44-70 */
        grp = 1;
    else if (chan <= 124)    /* 71-124 */
        grp = 2;
    else            /* 125-200 */
        grp = 3;
    /* Get channel sub-band. */
    for (i = 0; i < IWN_NBANDS; i++)
        if (sc->bands[i].lo != 0 &&
            sc->bands[i].lo <= chan && chan <= sc->bands[i].hi)
            break;
    if (i == IWN_NBANDS)    /* Can't happen in real-life. */
        return EINVAL;
    chans = sc->bands[i].chans;

    ht40chan = chan;
    ext_chan = chan;
    if (that->iwn_rxon_ht40_enabled(sc)) {
        bool secondaryBelow =
            (le32toh(sc->rxon.flags) & IWN_RXON_HT_HT40MINUS) != 0;

        if (IwnHt40Contracts::nvmPowerChannel(
                chan, secondaryBelow, &ht40chan) &&
            ht40chan < nitems(sc->maxpwr40)) {
            is_ht40 = 1;
            if (secondaryBelow)
                ext_chan = chan - 2;
            else
                ext_chan = chan + 2;
        }
    }

    for (c = 0; c < 2; c++) {
        uint8_t power, gain, temp;
        int maxchpwr, pwr, ridx, idx;

        power = interpolate(ext_chan,
            chans[0].num, chans[0].samples[c][1].power,
            chans[1].num, chans[1].samples[c][1].power, 1);
        gain  = interpolate(ext_chan,
            chans[0].num, chans[0].samples[c][1].gain,
            chans[1].num, chans[1].samples[c][1].gain, 1);
        temp  = interpolate(ext_chan,
            chans[0].num, chans[0].samples[c][1].temp,
            chans[1].num, chans[1].samples[c][1].temp, 1);
        /* Compute temperature compensation. */
        tdiff = ((sc->temp - temp) * 2) / tdiv[grp];

        for (ridx = 0; ridx <= IWN_RIDX_MAX; ridx++) {
            /* Convert dBm to half-dBm. */
            if (is_ht40)
                maxchpwr = sc->maxpwr40[ht40chan] * 2;
            else
                maxchpwr = sc->maxpwr[chan] * 2;
#ifdef notyet
            if (ridx > iwn_mcs2ridx[7] && ridx < iwn_mcs2ridx[16])
                maxchpwr -= 6;    /* MIMO 2T: -3dB */
#endif

            pwr = maxpwr;

            /* Adjust TX power based on rate. */
            if ((ridx % 8) == 5)
                pwr -= 15;    /* OFDM48: -7.5dB */
            else if ((ridx % 8) == 6)
                pwr -= 17;    /* OFDM54: -8.5dB */
            else if ((ridx % 8) == 7)
                pwr -= 20;    /* OFDM60: -10dB */
            else
                pwr -= 10;    /* Others: -5dB */

            /* Do not exceed channel max TX power. */
            if (pwr > maxchpwr)
                pwr = maxchpwr;

            idx = gain - (pwr - power) - tdiff - vdiff;
            if (ridx > iwn_mcs2ridx[7]) /* MIMO */
                idx += (int32_t)letoh32(uc->atten[grp][c]);

            if (cmd.band == 0)
                idx += 9;    /* 5GHz */
            if (ridx == IWN_RIDX_MAX)
                idx += 5;    /* CCK */

            /* Make sure idx stays in a valid range. */
            if (idx < 0)
                idx = 0;
            else if (idx > IWN4965_MAX_PWR_INDEX)
                idx = IWN4965_MAX_PWR_INDEX;

            cmd.power[ridx].rf_gain[c] = rf_gain[idx];
            cmd.power[ridx].dsp_gain[c] = dsp_gain[idx];
        }
    }

    return that->iwn_cmd(sc, IWN_CMD_TXPOWER, &cmd, sizeof cmd, async);

#undef interpolate
#undef fdivround
}

int ItlIwn::
iwn5000_set_txpower(struct iwn_softc *sc, int async)
{
    ItlIwn *that = container_of(sc, ItlIwn, com);
    struct iwn5000_cmd_txpower cmd;

    /*
     * TX power calibration is handled automatically by the firmware
     * for 5000 Series.
     */
    memset(&cmd, 0, sizeof cmd);
    cmd.global_limit = 2 * IWN5000_TXPOWER_MAX_DBM;    /* 16 dBm */
    cmd.flags = IWN5000_TXPOWER_NO_CLOSED;
    cmd.srv_limit = IWN5000_TXPOWER_AUTO;
    return that->iwn_cmd(sc, IWN_CMD_TXPOWER_DBM, &cmd, sizeof cmd, async);
}

/*
 * Retrieve the maximum RSSI (in dBm) among receivers.
 */
int ItlIwn::
iwn4965_get_rssi(const struct iwn_rx_stat *stat)
{
    struct iwn4965_rx_phystat *phy = (struct iwn4965_rx_phystat *)stat->phybuf;
    uint8_t mask, agc;
    int rssi;

    mask = (letoh16(phy->antenna) >> 4) & IWN_ANT_ABC;
    agc  = (letoh16(phy->agc) >> 7) & 0x7f;

    rssi = 0;
    if (mask & IWN_ANT_A)
        rssi = MAX(rssi, phy->rssi[0]);
    if (mask & IWN_ANT_B)
        rssi = MAX(rssi, phy->rssi[2]);
    if (mask & IWN_ANT_C)
        rssi = MAX(rssi, phy->rssi[4]);

    return rssi - agc - IWN_RSSI_TO_DBM;
}

int ItlIwn::
iwn5000_get_rssi(const struct iwn_rx_stat *stat)
{
    struct iwn5000_rx_phystat *phy = (struct iwn5000_rx_phystat *)stat->phybuf;
    uint8_t agc;
    int rssi;

    agc = (letoh32(phy->agc) >> 9) & 0x7f;

    rssi = MAX(letoh16(phy->rssi[0]) & 0xff,
           letoh16(phy->rssi[1]) & 0xff);
    rssi = MAX(letoh16(phy->rssi[2]) & 0xff, rssi);

    return rssi - agc - IWN_RSSI_TO_DBM;
}

/*
 * Retrieve the average noise (in dBm) among receivers.
 */
int ItlIwn::
iwn_get_noise(const struct iwn_rx_general_stats *stats)
{
    int i, total, nbant, noise;

    total = nbant = 0;
    for (i = 0; i < 3; i++) {
        if ((noise = letoh32(stats->noise[i]) & 0xff) == 0)
            continue;
        total += noise;
        nbant++;
    }
    /* There should be at least one antenna but check anyway. */
    return (nbant == 0) ? -127 : (total / nbant) - 107;
}

/*
 * Compute temperature (in degC) from last received statistics.
 */
int ItlIwn::
iwn4965_get_temperature(struct iwn_softc *sc)
{
    struct iwn_ucode_info *uc = &sc->ucode_info;
    int32_t r1, r2, r3, r4, temp;

    if (sc->rx_stats_flags & IWN_STATS_FLAGS_BAND_HT40) {
        r1 = letoh32(uc->temp[0].chan40MHz);
        r2 = letoh32(uc->temp[1].chan40MHz);
        r3 = letoh32(uc->temp[2].chan40MHz);
    } else {
        r1 = letoh32(uc->temp[0].chan20MHz);
        r2 = letoh32(uc->temp[1].chan20MHz);
        r3 = letoh32(uc->temp[2].chan20MHz);
    }
    r4 = letoh32(sc->rawtemp);

    if (r1 == r3)    /* Prevents division by 0 (should not happen). */
        return 0;

    /* Sign-extend 23-bit R4 value to 32-bit. */
    r4 = ((r4 & 0xffffff) ^ 0x800000) - 0x800000;
    /* Compute temperature in Kelvin. */
    temp = (259 * (r4 - r2)) / (r3 - r1);
    temp = (temp * 97) / 100 + 8;

    return IWN_KTOC(temp);
}

int ItlIwn::
iwn5000_get_temperature(struct iwn_softc *sc)
{
    int32_t temp;

    /*
     * Temperature is not used by the driver for 5000 Series because
     * TX power calibration is handled by firmware.
     */
    temp = letoh32(sc->rawtemp);
    if (sc->hw_type == IWN_HW_REV_TYPE_5150) {
        temp = (temp / -5) + sc->temp_off;
        temp = IWN_KTOC(temp);
    }
    return temp;
}

/*
 * Initialize sensitivity calibration state machine.
 */
int ItlIwn::
iwn_init_sensitivity(struct iwn_softc *sc)
{
    struct iwn_ops *ops = &sc->ops;
    struct iwn_calib_state *calib = &sc->calib;
    uint32_t flags;
    int error;

    /* Reset calibration state machine. */
    memset(calib, 0, sizeof (*calib));
    calib->state = IWN_CALIB_STATE_INIT;
    calib->cck_state = IWN_CCK_STATE_HIFA;
    /* Set initial correlation values. */
    calib->ofdm_x1     = sc->limits->min_ofdm_x1;
    calib->ofdm_mrc_x1 = sc->limits->min_ofdm_mrc_x1;
    calib->ofdm_x4     = sc->limits->min_ofdm_x4;
    calib->ofdm_mrc_x4 = sc->limits->min_ofdm_mrc_x4;
    calib->cck_x4      = 125;
    calib->cck_mrc_x4  = sc->limits->min_cck_mrc_x4;
    calib->energy_cck  = sc->limits->energy_cck;

    /* Write initial sensitivity. */
    if ((error = iwn_send_sensitivity(sc)) != 0)
        return error;

    /* Write initial gains. */
    if ((error = ops->init_gains(sc)) != 0)
        return error;

    /* Request statistics at each beacon interval. */
    flags = 0;
    return iwn_cmd(sc, IWN_CMD_GET_STATISTICS, &flags, sizeof flags, 1);
}

/*
 * Collect noise and RSSI statistics for the first 20 beacons received
 * after association and use them to determine connected antennas and
 * to set differential gains.
 */
void ItlIwn::
iwn_collect_noise(struct iwn_softc *sc,
    const struct iwn_rx_general_stats *stats)
{
    struct iwn_ops *ops = &sc->ops;
    struct iwn_calib_state *calib = &sc->calib;
    uint32_t val;
    int i;

    /* Accumulate RSSI and noise for all 3 antennas. */
    for (i = 0; i < 3; i++) {
        calib->rssi[i] += letoh32(stats->rssi[i]) & 0xff;
        calib->noise[i] += letoh32(stats->noise[i]) & 0xff;
    }
    /* NB: We update differential gains only once after 20 beacons. */
    if (++calib->nbeacons < 20)
        return;

    /* Determine highest average RSSI. */
    val = MAX(calib->rssi[0], calib->rssi[1]);
    val = MAX(calib->rssi[2], val);

    /* Determine which antennas are connected. */
    sc->chainmask = sc->rxchainmask;
    for (i = 0; i < 3; i++)
        if (val - calib->rssi[i] > 15 * 20)
            sc->chainmask &= ~(1 << i);
    /* If none of the TX antennas are connected, keep at least one. */
    if ((sc->chainmask & sc->txchainmask) == 0)
        sc->chainmask |= IWN_LSB(sc->txchainmask);

    (void)ops->set_gains(sc);
    calib->state = IWN_CALIB_STATE_RUN;

#ifdef notyet
    /* XXX Disable RX chains with no antennas connected. */
    sc->rxon.rxchain = htole16(IWN_RXCHAIN_SEL(sc->chainmask));
    (void)iwn_cmd(sc, IWN_CMD_RXON, &sc->rxon, sc->rxonsz, 1);
#endif

    /* Enable power-saving mode if requested by user. */
    if (sc->sc_ic.ic_flags & IEEE80211_F_PMGTON)
        (void)iwn_set_pslevel(sc, 0, 3, 1);
}

int ItlIwn::
iwn4965_init_gains(struct iwn_softc *sc)
{
    ItlIwn *that = container_of(sc, ItlIwn, com);
    struct iwn_phy_calib_gain cmd;

    memset(&cmd, 0, sizeof cmd);
    cmd.code = IWN4965_PHY_CALIB_DIFF_GAIN;
    /* Differential gains initially set to 0 for all 3 antennas. */
    return that->iwn_cmd(sc, IWN_CMD_PHY_CALIB, &cmd, sizeof cmd, 1);
}

int ItlIwn::
iwn5000_init_gains(struct iwn_softc *sc)
{
    ItlIwn *that = container_of(sc, ItlIwn, com);
    struct iwn_phy_calib cmd;

    memset(&cmd, 0, sizeof cmd);
    cmd.code = sc->reset_noise_gain;
    cmd.ngroups = 1;
    cmd.isvalid = 1;
    return that->iwn_cmd(sc, IWN_CMD_PHY_CALIB, &cmd, sizeof cmd, 1);
}

int ItlIwn::
iwn4965_set_gains(struct iwn_softc *sc)
{
    ItlIwn *that = container_of(sc, ItlIwn, com);
    struct iwn_calib_state *calib = &sc->calib;
    struct iwn_phy_calib_gain cmd;
    int i, delta, noise;

    /* Get minimal noise among connected antennas. */
    noise = INT_MAX;    /* NB: There's at least one antenna. */
    for (i = 0; i < 3; i++)
        if (sc->chainmask & (1 << i))
            noise = MIN(calib->noise[i], noise);

    memset(&cmd, 0, sizeof cmd);
    cmd.code = IWN4965_PHY_CALIB_DIFF_GAIN;
    /* Set differential gains for connected antennas. */
    for (i = 0; i < 3; i++) {
        if (sc->chainmask & (1 << i)) {
            /* Compute attenuation (in unit of 1.5dB). */
            delta = (noise - (int32_t)calib->noise[i]) / 30;
            /* NB: delta <= 0 */
            /* Limit to [-4.5dB,0]. */
            cmd.gain[i] = MIN(abs(delta), 3);
            if (delta < 0)
                cmd.gain[i] |= 1 << 2;    /* sign bit */
        }
    }
    return that->iwn_cmd(sc, IWN_CMD_PHY_CALIB, &cmd, sizeof cmd, 1);
}

int ItlIwn::
iwn5000_set_gains(struct iwn_softc *sc)
{
    ItlIwn *that = container_of(sc, ItlIwn, com);
    struct iwn_calib_state *calib = &sc->calib;
    struct iwn_phy_calib_gain cmd;
    int i, ant, div, delta;

    /* We collected 20 beacons and !=6050 need a 1.5 factor. */
    div = (sc->hw_type == IWN_HW_REV_TYPE_6050) ? 20 : 30;

    memset(&cmd, 0, sizeof cmd);
    cmd.code = sc->noise_gain;
    cmd.ngroups = 1;
    cmd.isvalid = 1;
    /*
     * Get first available RX antenna as referential.
     * IWN_LSB() return values start with 1, but antenna gain array
     * cmd.gain[] and noise array calib->noise[] start with 0.
     */
    ant = IWN_LSB(sc->rxchainmask) - 1;

    /* Set differential gains for other antennas. */
    for (i = ant + 1; i < 3; i++) {
        if (sc->chainmask & (1 << i)) {
            /* The delta is relative to antenna "ant". */
            delta = ((int32_t)calib->noise[ant] -
                (int32_t)calib->noise[i]) / div;
            /* Limit to [-4.5dB,+4.5dB]. */
            cmd.gain[i] = MIN(abs(delta), 3);
            if (delta < 0)
                cmd.gain[i] |= 1 << 2;    /* sign bit */
        }
    }
    return that->iwn_cmd(sc, IWN_CMD_PHY_CALIB, &cmd, sizeof cmd, 1);
}

/*
 * Tune RF RX sensitivity based on the number of false alarms detected
 * during the last beacon period.
 */
void ItlIwn::
iwn_tune_sensitivity(struct iwn_softc *sc, const struct iwn_rx_stats *stats)
{
#define inc(val, inc, max)            \
    if ((val) < (max)) {            \
        if ((val) < (max) - (inc))    \
            (val) += (inc);        \
        else                \
            (val) = (max);        \
        needs_update = 1;        \
    }
#define dec(val, dec, min)            \
    if ((val) > (min)) {            \
        if ((val) > (min) + (dec))    \
            (val) -= (dec);        \
        else                \
            (val) = (min);        \
        needs_update = 1;        \
    }

    const struct iwn_sensitivity_limits *limits = sc->limits;
    struct iwn_calib_state *calib = &sc->calib;
    uint32_t val, rxena, fa;
    uint32_t energy[3], energy_min;
    uint8_t noise[3], noise_ref;
    int i, needs_update = 0;

    /* Check that we've been enabled long enough. */
    if ((rxena = letoh32(stats->general.load)) == 0)
        return;

    /* Compute number of false alarms since last call for OFDM. */
    fa  = letoh32(stats->ofdm.bad_plcp) - calib->bad_plcp_ofdm;
    fa += letoh32(stats->ofdm.fa) - calib->fa_ofdm;
    fa *= 200 * IEEE80211_DUR_TU;    /* 200TU */

    /* Save counters values for next call. */
    calib->bad_plcp_ofdm = letoh32(stats->ofdm.bad_plcp);
    calib->fa_ofdm = letoh32(stats->ofdm.fa);

    if (fa > 50 * rxena) {
        /* High false alarm count, decrease sensitivity. */
        inc(calib->ofdm_x1,     1, limits->max_ofdm_x1);
        inc(calib->ofdm_mrc_x1, 1, limits->max_ofdm_mrc_x1);
        inc(calib->ofdm_x4,     1, limits->max_ofdm_x4);
        inc(calib->ofdm_mrc_x4, 1, limits->max_ofdm_mrc_x4);

    } else if (fa < 5 * rxena) {
        /* Low false alarm count, increase sensitivity. */
        dec(calib->ofdm_x1,     1, limits->min_ofdm_x1);
        dec(calib->ofdm_mrc_x1, 1, limits->min_ofdm_mrc_x1);
        dec(calib->ofdm_x4,     1, limits->min_ofdm_x4);
        dec(calib->ofdm_mrc_x4, 1, limits->min_ofdm_mrc_x4);
    }

    /* Compute maximum noise among 3 receivers. */
    for (i = 0; i < 3; i++)
        noise[i] = (letoh32(stats->general.noise[i]) >> 8) & 0xff;
    val = MAX(noise[0], noise[1]);
    val = MAX(noise[2], val);
    /* Insert it into our samples table. */
    calib->noise_samples[calib->cur_noise_sample] = val;
    calib->cur_noise_sample = (calib->cur_noise_sample + 1) % 20;

    /* Compute maximum noise among last 20 samples. */
    noise_ref = calib->noise_samples[0];
    for (i = 1; i < 20; i++)
        noise_ref = MAX(noise_ref, calib->noise_samples[i]);

    /* Compute maximum energy among 3 receivers. */
    for (i = 0; i < 3; i++)
        energy[i] = letoh32(stats->general.energy[i]);
    val = MIN(energy[0], energy[1]);
    val = MIN(energy[2], val);
    /* Insert it into our samples table. */
    calib->energy_samples[calib->cur_energy_sample] = val;
    calib->cur_energy_sample = (calib->cur_energy_sample + 1) % 10;

    /* Compute minimum energy among last 10 samples. */
    energy_min = calib->energy_samples[0];
    for (i = 1; i < 10; i++)
        energy_min = MAX(energy_min, calib->energy_samples[i]);
    energy_min += 6;

    /* Compute number of false alarms since last call for CCK. */
    fa  = letoh32(stats->cck.bad_plcp) - calib->bad_plcp_cck;
    fa += letoh32(stats->cck.fa) - calib->fa_cck;
    fa *= 200 * IEEE80211_DUR_TU;    /* 200TU */

    /* Save counters values for next call. */
    calib->bad_plcp_cck = letoh32(stats->cck.bad_plcp);
    calib->fa_cck = letoh32(stats->cck.fa);

    if (fa > 50 * rxena) {
        /* High false alarm count, decrease sensitivity. */
        calib->cck_state = IWN_CCK_STATE_HIFA;
        calib->low_fa = 0;

        if (calib->cck_x4 > 160) {
            calib->noise_ref = noise_ref;
            if (calib->energy_cck > 2)
                dec(calib->energy_cck, 2, energy_min);
        }
        if (calib->cck_x4 < 160) {
            calib->cck_x4 = 161;
            needs_update = 1;
        } else
            inc(calib->cck_x4, 3, limits->max_cck_x4);

        inc(calib->cck_mrc_x4, 3, limits->max_cck_mrc_x4);

    } else if (fa < 5 * rxena) {
        /* Low false alarm count, increase sensitivity. */
        calib->cck_state = IWN_CCK_STATE_LOFA;
        calib->low_fa++;

        if (calib->cck_state != IWN_CCK_STATE_INIT &&
            (((int32_t)calib->noise_ref - (int32_t)noise_ref) > 2 ||
             calib->low_fa > 100)) {
            inc(calib->energy_cck, 2, limits->min_energy_cck);
            dec(calib->cck_x4,     3, limits->min_cck_x4);
            dec(calib->cck_mrc_x4, 3, limits->min_cck_mrc_x4);
        }
    } else {
        /* Not worth to increase or decrease sensitivity. */
        calib->low_fa = 0;
        calib->noise_ref = noise_ref;

        if (calib->cck_state == IWN_CCK_STATE_HIFA) {
            /* Previous interval had many false alarms. */
            dec(calib->energy_cck, 8, energy_min);
        }
        calib->cck_state = IWN_CCK_STATE_INIT;
    }

    if (needs_update)
        (void)iwn_send_sensitivity(sc);
#undef dec
#undef inc
}

int ItlIwn::
iwn_send_sensitivity(struct iwn_softc *sc)
{
    struct iwn_calib_state *calib = &sc->calib;
    struct iwn_enhanced_sensitivity_cmd cmd;
    int len;

    memset(&cmd, 0, sizeof cmd);
    len = sizeof (struct iwn_sensitivity_cmd);
    cmd.which = IWN_SENSITIVITY_WORKTBL;
    /* OFDM modulation. */
    cmd.corr_ofdm_x1       = htole16(calib->ofdm_x1);
    cmd.corr_ofdm_mrc_x1   = htole16(calib->ofdm_mrc_x1);
    cmd.corr_ofdm_x4       = htole16(calib->ofdm_x4);
    cmd.corr_ofdm_mrc_x4   = htole16(calib->ofdm_mrc_x4);
    cmd.energy_ofdm        = htole16(sc->limits->energy_ofdm);
    cmd.energy_ofdm_th     = htole16(62);
    /* CCK modulation. */
    cmd.corr_cck_x4        = htole16(calib->cck_x4);
    cmd.corr_cck_mrc_x4    = htole16(calib->cck_mrc_x4);
    cmd.energy_cck         = htole16(calib->energy_cck);
    /* Barker modulation: use default values. */
    cmd.corr_barker        = htole16(190);
    cmd.corr_barker_mrc    = htole16(390);
    if (!(sc->sc_flags & IWN_FLAG_ENH_SENS))
        goto send;
    /* Enhanced sensitivity settings. */
    len = sizeof (struct iwn_enhanced_sensitivity_cmd);
    cmd.ofdm_det_slope_mrc = htole16(668);
    cmd.ofdm_det_icept_mrc = htole16(4);
    cmd.ofdm_det_slope     = htole16(486);
    cmd.ofdm_det_icept     = htole16(37);
    cmd.cck_det_slope_mrc  = htole16(853);
    cmd.cck_det_icept_mrc  = htole16(4);
    cmd.cck_det_slope      = htole16(476);
    cmd.cck_det_icept      = htole16(99);
send:
    return iwn_cmd(sc, IWN_CMD_SET_SENSITIVITY, &cmd, len, 1);
}

/*
 * Set STA mode power saving level (between 0 and 5).
 * Level 0 is CAM (Continuously Aware Mode), 5 is for maximum power saving.
 */
int ItlIwn::
iwn_set_pslevel(struct iwn_softc *sc, int dtim, int level, int async)
{
    struct iwn_pmgt_cmd cmd;
    const struct iwn_pmgt *pmgt;
    uint32_t max, skip_dtim;
    pcireg_t reg;
    int i;

    /* Select which PS parameters to use. */
    if (dtim <= 2)
        pmgt = &iwn_pmgt[0][level];
    else if (dtim <= 10)
        pmgt = &iwn_pmgt[1][level];
    else
        pmgt = &iwn_pmgt[2][level];

    memset(&cmd, 0, sizeof cmd);
    if (level != 0)    /* not CAM */
        cmd.flags |= htole16(IWN_PS_ALLOW_SLEEP);
    if (level == 5)
        cmd.flags |= htole16(IWN_PS_FAST_PD);
    /* Retrieve PCIe Active State Power Management (ASPM). */
    reg = pci_conf_read(sc->sc_pct, sc->sc_pcitag,
        sc->sc_cap_off + PCI_PCIE_LCSR);
    if (!(reg & PCI_PCIE_LCSR_ASPM_L0S))    /* L0s Entry disabled. */
        cmd.flags |= htole16(IWN_PS_PCI_PMGT);
    cmd.rxtimeout = htole32(pmgt->rxtimeout * 1024);
    cmd.txtimeout = htole32(pmgt->txtimeout * 1024);

    if (dtim == 0) {
        dtim = 1;
        skip_dtim = 0;
    } else
        skip_dtim = pmgt->skip_dtim;
    if (skip_dtim != 0) {
        cmd.flags |= htole16(IWN_PS_SLEEP_OVER_DTIM);
        max = pmgt->intval[4];
        if (max == (uint32_t)-1)
            max = dtim * (skip_dtim + 1);
        else if (max > dtim)
            max = (max / dtim) * dtim;
    } else
        max = dtim;
    for (i = 0; i < 5; i++)
        cmd.intval[i] = htole32(MIN(max, pmgt->intval[i]));

    return iwn_cmd(sc, IWN_CMD_SET_POWER_MODE, &cmd, sizeof cmd, async);
}

int ItlIwn::
iwn_send_btcoex(struct iwn_softc *sc)
{
    struct iwn_bluetooth cmd;

    memset(&cmd, 0, sizeof cmd);
    cmd.flags = IWN_BT_COEX_CHAN_ANN | IWN_BT_COEX_BT_PRIO;
    cmd.lead_time = IWN_BT_LEAD_TIME_DEF;
    cmd.max_kill = IWN_BT_MAX_KILL_DEF;
    return iwn_cmd(sc, IWN_CMD_BT_COEX, &cmd, sizeof(cmd), 0);
}

int ItlIwn::
iwn_send_advanced_btcoex(struct iwn_softc *sc)
{
    static const uint32_t btcoex_3wire[12] = {
        0xaaaaaaaa, 0xaaaaaaaa, 0xaeaaaaaa, 0xaaaaaaaa,
        0xcc00ff28, 0x0000aaaa, 0xcc00aaaa, 0x0000aaaa,
        0xc0004000, 0x00004000, 0xf0005000, 0xf0005000,
    };
    struct iwn_btcoex_priotable btprio;
    struct iwn_btcoex_prot btprot;
    int error, i;

    if (sc->hw_type == IWN_HW_REV_TYPE_2030 ||
        sc->hw_type == IWN_HW_REV_TYPE_135) {
        struct iwn2000_btcoex_config btconfig;

        memset(&btconfig, 0, sizeof btconfig);
        btconfig.flags = IWN_BT_COEX6000_CHAN_INHIBITION |
            (IWN_BT_COEX6000_MODE_3W << IWN_BT_COEX6000_MODE_SHIFT) |
            IWN_BT_SYNC_2_BT_DISABLE;
        btconfig.max_kill = 5;
        btconfig.bt3_t7_timer = 1;
        btconfig.kill_ack = htole32(0xffff0000);
        btconfig.kill_cts = htole32(0xffff0000);
        btconfig.sample_time = 2;
        btconfig.bt3_t2_timer = 0xc;
        for (i = 0; i < 12; i++)
            btconfig.lookup_table[i] = htole32(btcoex_3wire[i]);
        btconfig.valid = htole16(0xff);
        btconfig.prio_boost = htole32(0xf0);
        error = iwn_cmd(sc, IWN_CMD_BT_COEX, &btconfig,
            sizeof(btconfig), 1);
        if (error != 0)
            return (error);
    } else {
        struct iwn6000_btcoex_config btconfig;

        memset(&btconfig, 0, sizeof btconfig);
        btconfig.flags = IWN_BT_COEX6000_CHAN_INHIBITION |
            (IWN_BT_COEX6000_MODE_3W << IWN_BT_COEX6000_MODE_SHIFT) |
            IWN_BT_SYNC_2_BT_DISABLE;
        btconfig.max_kill = 5;
        btconfig.bt3_t7_timer = 1;
        btconfig.kill_ack = htole32(0xffff0000);
        btconfig.kill_cts = htole32(0xffff0000);
        btconfig.sample_time = 2;
        btconfig.bt3_t2_timer = 0xc;
        for (i = 0; i < 12; i++)
            btconfig.lookup_table[i] = htole32(btcoex_3wire[i]);
        btconfig.valid = htole16(0xff);
        btconfig.prio_boost = 0xf0;
        error = iwn_cmd(sc, IWN_CMD_BT_COEX, &btconfig,
            sizeof(btconfig), 1);
        if (error != 0)
            return (error);
    }

    memset(&btprio, 0, sizeof btprio);
    btprio.calib_init1 = 0x6;
    btprio.calib_init2 = 0x7;
    btprio.calib_periodic_low1 = 0x2;
    btprio.calib_periodic_low2 = 0x3;
    btprio.calib_periodic_high1 = 0x4;
    btprio.calib_periodic_high2 = 0x5;
    btprio.dtim = 0x6;
    btprio.scan52 = 0x8;
    btprio.scan24 = 0xa;
    error = iwn_cmd(sc, IWN_CMD_BT_COEX_PRIOTABLE, &btprio, sizeof(btprio),
        1);
    if (error != 0)
        return (error);

    /* Force BT state machine change */
    memset(&btprot, 0, sizeof btprot);
    btprot.open = 1;
    btprot.type = 1;
    error = iwn_cmd(sc, IWN_CMD_BT_COEX_PROT, &btprot, sizeof(btprot), 1);
    if (error != 0)
        return (error);

    btprot.open = 0;
    return (iwn_cmd(sc, IWN_CMD_BT_COEX_PROT, &btprot, sizeof(btprot), 1));
}

int ItlIwn::
iwn5000_runtime_calib(struct iwn_softc *sc)
{
    struct iwn5000_calib_config cmd;

    memset(&cmd, 0, sizeof cmd);
    cmd.ucode.once.enable = 0xffffffff;
    cmd.ucode.once.start = IWN5000_CALIB_DC;
    return iwn_cmd(sc, IWN5000_CMD_CALIB_CONFIG, &cmd, sizeof(cmd), 0);
}

int ItlIwn::
iwn_config(struct iwn_softc *sc)
{
    struct iwn_ops *ops = &sc->ops;
    struct ieee80211com *ic = &sc->sc_ic;
    struct _ifnet *ifp = &ic->ic_if;
    uint32_t txmask;
    uint16_t rxchain;
    int error, ridx;

    /* Set radio temperature sensor offset. */
    if (sc->hw_type == IWN_HW_REV_TYPE_6005) {
        error = iwn6000_temp_offset_calib(sc);
        if (error != 0) {
            XYLog("%s: could not set temperature offset\n",
                sc->sc_dev.dv_xname);
            return error;
        }
    }

    if (sc->hw_type == IWN_HW_REV_TYPE_2030 ||
        sc->hw_type == IWN_HW_REV_TYPE_2000 ||
        sc->hw_type == IWN_HW_REV_TYPE_135 ||
        sc->hw_type == IWN_HW_REV_TYPE_105) {
        error = iwn2000_temp_offset_calib(sc);
        if (error != 0) {
            XYLog("%s: could not set temperature offset\n",
                sc->sc_dev.dv_xname);
            return error;
        }
    }

    if (sc->hw_type == IWN_HW_REV_TYPE_6050 ||
        sc->hw_type == IWN_HW_REV_TYPE_6005) {
        /* Configure runtime DC calibration. */
        error = iwn5000_runtime_calib(sc);
        if (error != 0) {
            XYLog("%s: could not configure runtime calibration\n",
                sc->sc_dev.dv_xname);
            return error;
        }
    }

    /* Configure valid TX chains for >=5000 Series. */
    if (sc->hw_type != IWN_HW_REV_TYPE_4965) {
        txmask = htole32(sc->txchainmask);
        error = iwn_cmd(sc, IWN5000_CMD_TX_ANT_CONFIG, &txmask,
            sizeof txmask, 0);
        if (error != 0) {
            XYLog("%s: could not configure valid TX chains\n",
                sc->sc_dev.dv_xname);
            return error;
        }
    }

    /* Configure bluetooth coexistence. */
    if (sc->sc_flags & IWN_FLAG_ADV_BT_COEX)
        error = iwn_send_advanced_btcoex(sc);
    else
        error = iwn_send_btcoex(sc);
    if (error != 0) {
        XYLog("%s: could not configure bluetooth coexistence\n",
            sc->sc_dev.dv_xname);
        return error;
    }

    /* Set mode, channel, RX filter and enable RX. */
    memset(&sc->rxon, 0, sizeof (struct iwn_rxon));
//    IEEE80211_ADDR_COPY(ic->ic_myaddr, LLADDR(ifp->if_sadl));
    IEEE80211_ADDR_COPY(sc->rxon.myaddr, ic->ic_myaddr);
    IEEE80211_ADDR_COPY(sc->rxon.wlap, ic->ic_myaddr);
    sc->rxon.chan = ieee80211_chan2ieee(ic, ic->ic_ibss_chan);
    sc->rxon.flags = htole32(IWN_RXON_TSF | IWN_RXON_CTS_TO_SELF);
    if (IEEE80211_IS_CHAN_2GHZ(ic->ic_ibss_chan)) {
        sc->rxon.flags |= htole32(IWN_RXON_AUTO | IWN_RXON_24GHZ);
        if (ic->ic_flags & IEEE80211_F_USEPROT)
            sc->rxon.flags |= htole32(IWN_RXON_TGG_PROT);
    }
    switch (ic->ic_opmode) {
    case IEEE80211_M_STA:
        sc->rxon.mode = IWN_MODE_STA;
        sc->rxon.filter = htole32(IWN_FILTER_MULTICAST);
        break;
    case IEEE80211_M_MONITOR:
        sc->rxon.mode = IWN_MODE_MONITOR;
        sc->rxon.filter = htole32(IWN_FILTER_MULTICAST |
            IWN_FILTER_CTL | IWN_FILTER_PROMISC);
        break;
    default:
        /* Should not get there. */
        break;
    }
    sc->rxon.cck_mask  = 0x0f;    /* not yet negotiated */
    sc->rxon.ofdm_mask = 0xff;    /* not yet negotiated */
    sc->rxon.ht_single_mask = 0xff;
    sc->rxon.ht_dual_mask = 0xff;
    sc->rxon.ht_triple_mask = 0xff;
    rxchain =
        IWN_RXCHAIN_VALID(sc->rxchainmask) |
        IWN_RXCHAIN_MIMO_COUNT(sc->nrxchains) |
        IWN_RXCHAIN_IDLE_COUNT(sc->nrxchains);
    if (ic->ic_opmode == IEEE80211_M_MONITOR) {
        rxchain |= IWN_RXCHAIN_FORCE_SEL(sc->rxchainmask);
        rxchain |= IWN_RXCHAIN_FORCE_MIMO_SEL(sc->rxchainmask);
            rxchain |= (IWN_RXCHAIN_DRIVER_FORCE | IWN_RXCHAIN_MIMO_FORCE);
    }
    sc->rxon.rxchain = htole16(rxchain);
    error = iwn_cmd(sc, IWN_CMD_RXON, &sc->rxon, sc->rxonsz, 0);
    if (error != 0) {
        XYLog("%s: RXON command failed\n", sc->sc_dev.dv_xname);
        return error;
    }

    ridx = (sc->sc_ic.ic_curmode == IEEE80211_MODE_11A) ?
        IWN_RIDX_OFDM : IWN_RIDX_CCK;
    if ((error = iwn_add_broadcast_node(sc, 0, ridx)) != 0) {
        XYLog("%s: could not add broadcast node\n",
            sc->sc_dev.dv_xname);
        return error;
    }

    /* Configuration has changed, set TX power accordingly. */
    if ((error = ops->set_txpower(sc, 0)) != 0) {
        XYLog("%s: could not set TX power\n", sc->sc_dev.dv_xname);
        return error;
    }

    if ((error = iwn_set_critical_temp(sc)) != 0) {
        XYLog("%s: could not set critical temperature\n",
            sc->sc_dev.dv_xname);
        return error;
    }

    /* Set power saving level to CAM during initialization. */
    if ((error = iwn_set_pslevel(sc, 0, 0, 0)) != 0) {
        XYLog("%s: could not set power saving level\n",
            sc->sc_dev.dv_xname);
        return error;
    }
    return 0;
}

uint16_t ItlIwn::
iwn_get_active_dwell_time(struct iwn_softc *sc,
    uint16_t flags, uint8_t n_probes)
{
    /* No channel? Default to 2GHz settings */
    if (flags & IEEE80211_CHAN_2GHZ) {
        return (IWN_ACTIVE_DWELL_TIME_2GHZ +
        IWN_ACTIVE_DWELL_FACTOR_2GHZ * (n_probes + 1));
    }

    /* 5GHz dwell time */
    return (IWN_ACTIVE_DWELL_TIME_5GHZ +
        IWN_ACTIVE_DWELL_FACTOR_5GHZ * (n_probes + 1));
}

/*
 * Limit the total dwell time to the live firmware contexts' beacon budget.
 *
 * Returns the dwell time in milliseconds.
 */
uint16_t ItlIwn::
iwn_limit_dwell(struct iwn_softc *sc, uint16_t dwell_time)
{
    struct ieee80211com *ic = &sc->sc_ic;
    struct ieee80211_node *ni = ic->ic_bss;
    int bintval = 0;

    /* bintval is in TU (1.024mS) */
    if (ni != NULL)
        bintval = ni->ni_intval;

    const bool apContextRunning =
        apFirmwareTransitionActive &&
        apFirmwareStage == IWN_AP_STAGE_RUNNING;
    if (apContextRunning) {
        uint16_t limits[2];
        unsigned activeContexts = 0;

        /*
         * Match DVM's iwl_limit_dwell().  CP has a live TBTT timer even
         * before a station joins it, so an operational PAN/AP context is a
         * dwell constraint in its own right.  The BSS context adds a second
         * constraint only while its actual RXON carrier is associated; the
         * net80211 state may already be SCAN during a foreground reconnect.
         */
        if ((le32toh(sc->rxon.filter) & IWN_FILTER_BSS) != 0 &&
            IEEE80211_AID(le16toh(sc->rxon.associd)) != 0) {
            limits[activeContexts++] = bintval > 0 ?
                static_cast<uint16_t>(bintval) : IWN_PASSIVE_DWELL_BASE;
        }
        limits[activeContexts++] = apFirmwareConfig.beaconInterval != 0 ?
            apFirmwareConfig.beaconInterval : IWN_PASSIVE_DWELL_BASE;

        for (unsigned index = 0; index < activeContexts; index++) {
            const int available =
                (static_cast<int>(limits[index]) * 98) / 100 -
                IWN_CHANNEL_TUNE_TIME * 2;
            if (available > 0) {
                const uint16_t limit = static_cast<uint16_t>(
                    available / static_cast<int>(activeContexts));
                dwell_time = MIN(dwell_time, limit);
            }
        }
        return dwell_time;
    }

    /*
     * Preserve the established single-context OpenBSD policy when HostAP is
     * not active.  XXX bintval is TU (1.024ms), not exactly milliseconds.
     */
    if (ic->ic_state == IEEE80211_S_RUN && bintval > 0)
        return (MIN(IWN_PASSIVE_DWELL_BASE, ((bintval * 85) / 100)));

    /* No association context? Default */
    return dwell_time;
}

uint16_t ItlIwn::
iwn_get_passive_dwell_time(struct iwn_softc *sc, uint16_t flags)
{
    uint16_t passive;
    if (flags & IEEE80211_CHAN_2GHZ) {
        passive = IWN_PASSIVE_DWELL_BASE + IWN_PASSIVE_DWELL_TIME_2GHZ;
    } else {
        passive = IWN_PASSIVE_DWELL_BASE + IWN_PASSIVE_DWELL_TIME_5GHZ;
    }

    /* Clamp to the beacon interval if we're associated */
    return (iwn_limit_dwell(sc, passive));
}

static void
iwn_prepare_controller_foreground_scan(struct ieee80211com *ic)
{
    if (ic == NULL)
        return;
    ieee80211_prepare_scan(&ic->ic_ac.ac_if);
}

static void
iwn_scan_restore_prearmed_background(struct iwn_softc *sc,
                                     struct ieee80211com *ic, bool wcl,
                                     u_int32_t old_ic_flags,
                                     time_t old_cache_scan_ts)
{
    if (sc == NULL || ic == NULL)
        return;
    sc->sc_flags &= ~(IWN_FLAG_SCANNING | IWN_FLAG_BGSCAN);
    if (wcl)
        __atomic_store_n(&ic->ic_wcl_scan_active, 0, __ATOMIC_RELEASE);
    ic->ic_flags = old_ic_flags;
    ic->ic_last_cache_scan_ts = old_cache_scan_ts;
}

static void
iwn_scan_schedule_fatal_recovery(struct iwn_softc *sc)
{
    if (sc == NULL)
        return;
    sc->sc_flags |= IWN_FLAG_FATAL_RECOVERY;
    (void)task_add(systq, &sc->init_task);
}

int ItlIwn::
iwn_scan_start(struct iwn_softc *sc, uint16_t flags, int bgscan,
               enum iwn_scan_lease_owner owner, u_int64_t upper_generation,
               u_int64_t required_initial_handoff_serial,
               u_int32_t *out_backend_generation,
               u_int64_t direct_sae_scan_generation, u_int64_t reassoc_serial)
{
    struct ieee80211com *ic;
    u_int64_t serial = 0;
    u_int32_t backend_generation = 0;
    u_int32_t old_ic_flags = 0;
    u_int8_t wnm_target_channel = 0;
    time_t old_cache_scan_ts = 0;
    bool wcl_background = owner == IWN_SCAN_LEASE_WCL_BACKGROUND;
    bool wcl_foreground = owner == IWN_SCAN_LEASE_WCL_INITIAL;
    bool wcl = iwn_scan_lease_owner_is_wcl(owner);
    bool standard = owner == IWN_SCAN_LEASE_STANDARD_CONTROLLER;
    bool wnm_scan = false;
    bool tagged_controller_owner = wcl || standard;
    bool prearm_background = wcl_background || (standard && bgscan != 0);
    bool controller_foreground = wcl_foreground ||
        (standard && bgscan == 0);
    bool abort_requested = false;
    bool command_attempted = false;
    bool foreground_prepared = false;
    const bool direct_sae_scan = direct_sae_scan_generation != 0;
    int error;

    if (sc == NULL || (ic = &sc->sc_ic) == NULL ||
        (tagged_controller_owner &&
         (upper_generation == 0 || out_backend_generation == NULL)))
        return EINVAL;
    wnm_scan =
        ieee80211_wnm_bss_transition_scan_owns_admission(ic) != 0;
    if (out_backend_generation != NULL)
        *out_backend_generation = 0;
    /* All physical scan owners share the protected-association fence.  The
     * generic background owner is deliberately included: it does not set
     * prearm_background, and used to retune APSTA between Association
     * Response and EAPOL M1. */
    if (iwn_rsn_join_scan_blocked(ic))
        return EBUSY;
    if (prearm_background && (ic->ic_state != IEEE80211_S_RUN ||
                              ic->ic_mgt_timer != 0 ||
                              ((ic->ic_flags & IEEE80211_F_RSNON) != 0 &&
                               (ic->ic_bss == NULL ||
                                !ic->ic_bss->ni_port_valid))))
        return EBUSY;
    /*
     * Once a protected BTM has displaced an older background census, it
     * owns the next background admission.  Do not let a WCL retry win the
     * abort-terminal race and force this roam through another full command.
     */
    if (bgscan != 0 && !wnm_scan &&
        ieee80211_wnm_bss_transition_fresh_scan_pending(ic))
        return EBUSY;
    if (wcl_foreground &&
        (ic->ic_state != IEEE80211_S_SCAN ||
         ic->ic_opmode != IEEE80211_M_STA ||
         ic->ic_mgt_timer != 0 ||
         /* As in beginWclInitialScan(), a BSSID pin without an ESS is
          * permitted only for this WCL initial discovery scan.  Do not
          * clear it here: later association policy remains pinned while
          * the empty-ESS/SAE fences keep direct selection fail-closed.
          * The exact scan lease, not a possibly stale net80211 BGSCAN bit,
          * arbitrates physical overlap. */
         (ic->ic_flags & IEEE80211_F_AUTO_JOIN) == 0 ||
         ic->ic_des_esslen != 0 ||
         ieee80211_sae_wcl_request_scan_selection_held(ic) ||
         ieee80211_sae_wcl_request_scan_selection_owned(ic)))
        return EBUSY;
    if (standard && bgscan == 0 && ic->ic_state != IEEE80211_S_SCAN)
        return EBUSY;
    (void)ieee80211_wnm_bss_transition_target_channel(ic,
        &wnm_target_channel);
    if (!iwn_scan_lease_reserve(sc, owner, upper_generation,
                                required_initial_handoff_serial,
                                &backend_generation, &serial,
                                direct_sae_scan_generation,
                                wnm_target_channel, reassoc_serial))
        return EBUSY;
    if (out_backend_generation != NULL)
        *out_backend_generation = backend_generation;

    /* The lease is the lower physical owner's first durable boundary.  Do
     * not call it a firmware submission: STOP_SCAN may race before the
     * command doorbell, and the trace must preserve that distinction. */
    if (wcl) {
        AirportItlwmPostPltiTraceRecordWclPhysicalScan(
            ic,
            kAirportItlwmPostPltiTraceEventWclPhysicalScanLowerLeaseReserved);
    }
    if (prearm_background) {
        struct timeval tv;

        /* Arm the lower/net80211 ownership markers before command build. The
         * lease remains ARMING until iwn_cmd accepts the scan command, so a
         * delayed STOP_SCAN cannot be attributed to this new request. */
        old_ic_flags = ic->ic_flags;
        old_cache_scan_ts = ic->ic_last_cache_scan_ts;
        ic->ic_flags |= IEEE80211_F_DISABLE_BG_AUTO_CONNECT |
            IEEE80211_F_BGSCAN;
        if (wcl_background)
            __atomic_store_n(&ic->ic_wcl_scan_active, 1, __ATOMIC_RELEASE);
        sc->sc_flags |= IWN_FLAG_SCANNING | IWN_FLAG_BGSCAN;
        microtime(&tv);
        if (ic->ic_last_cache_scan_ts > 0 &&
            tv.tv_sec - ic->ic_last_cache_scan_ts > 5 * 60)
            ieee80211_free_allnodes(ic, 0 /* keep ic->ic_bss */);
        ic->ic_last_cache_scan_ts = tv.tv_sec;
    }
    if (!iwn_scan_lease_arm_submission(sc, serial, &abort_requested)) {
        /* A revoke can win after prearming the background flags but before
         * build begins.  Only a successful ARMING rollback proves the lower
         * command never became visible, so restore those flags in that case. */
        if (iwn_scan_lease_rollback(sc, serial)) {
            if (prearm_background)
                iwn_scan_restore_prearmed_background(sc, ic, wcl_background,
                                                      old_ic_flags,
                                                      old_cache_scan_ts);
            if (out_backend_generation != NULL)
                *out_backend_generation = 0;
            iwn_scan_lease_schedule_replay_task(sc);
        }
        return ECANCELED;
    }
    if (abort_requested) {
        /* A generic SCAN intent won while this WCL request was only armed.
         * No doorbell exists yet, so retire locally and replay that intent
         * instead of racing an abort command ahead of a new scan command. */
        if (iwn_scan_lease_rollback(sc, serial)) {
            if (prearm_background)
                iwn_scan_restore_prearmed_background(sc, ic, wcl_background,
                                                      old_ic_flags,
                                                      old_cache_scan_ts);
            if (out_backend_generation != NULL)
                *out_backend_generation = 0;
            iwn_scan_lease_schedule_replay_task(sc);
        }
        return ECANCELED;
    }

    /* The submit helper builds every fallible command resource first, makes
     * one last ARMING check, then performs this non-idempotent preparation.
     * It reports whether recovery rather than a clean lease rollback owns a
     * later no-doorbell failure. */
    error = iwn_scan_submit(sc, flags, bgscan, serial,
                            controller_foreground, wcl_foreground, wcl,
                            upper_generation, backend_generation,
                            &command_attempted,
                            &foreground_prepared);
    if (error != 0) {
        if (!command_attempted &&
            (foreground_prepared || (bgscan == 0 && !wcl_foreground))) {
            /* A foreground caller already owns S_SCAN even when build fails
             * after prepare_scan().  For a WCL initial request that failed
             * before preparation, however, the exact handoff token still
             * proves no WCL command was visible, so rollback lets the upper
             * reducer reject it without inventing an invalidation. */
            iwn_scan_schedule_fatal_recovery(sc);
            return error;
        }
        if (!command_attempted && iwn_scan_lease_rollback(sc, serial)) {
            if (prearm_background)
                iwn_scan_restore_prearmed_background(sc, ic, wcl_background,
                                                      old_ic_flags,
                                                      old_cache_scan_ts);
            if (out_backend_generation != NULL)
                *out_backend_generation = 0;
            iwn_scan_lease_schedule_replay_task(sc);
            return error;
        }
        /* A command that crossed the doorbell must retire only through its
         * native terminal/reset owner; never free the lease underneath it. */
        iwn_scan_schedule_fatal_recovery(sc);
        return error;
    }

    if (out_backend_generation != NULL)
        *out_backend_generation = backend_generation;
    return 0;
}

int ItlIwn::
iwn_scan(struct iwn_softc *sc, uint16_t flags, int bgscan,
         u_int64_t direct_sae_scan_generation)
{
    return iwn_scan_start(sc, flags, bgscan,
        bgscan ? IWN_SCAN_LEASE_GENERIC_BACKGROUND :
            IWN_SCAN_LEASE_GENERIC_FOREGROUND,
        0, 0, NULL, direct_sae_scan_generation);
}

int ItlIwn::
iwn_scan_continue(struct iwn_softc *sc, uint16_t flags, int bgscan)
{
    u_int64_t serial = 0;
    bool command_attempted = false;
    bool wcl_scan = false;
    int error;

    if (!iwn_scan_lease_begin_continuation(sc, &serial, &wcl_scan))
        return EBUSY;
    error = iwn_scan_submit(sc, flags, bgscan, serial, false, false,
                            wcl_scan, 0, 0, &command_attempted, NULL);
    if (error != 0 && !command_attempted)
        (void)iwn_scan_lease_restore_continuation(sc, serial, true);
    else if (error != 0)
        iwn_scan_schedule_fatal_recovery(sc);
    return error;
}

int ItlIwn::
iwn_scan_submit(struct iwn_softc *sc, uint16_t flags, int bgscan,
                u_int64_t lease_serial, bool prepare_controller_foreground,
                bool publish_wcl_initial_started,
                bool wcl_scan,
                u_int64_t upper_generation, u_int32_t backend_generation,
                bool *out_command_attempted, bool *out_foreground_prepared)
{
    struct ieee80211com *ic = &sc->sc_ic;
    struct iwn_scan_hdr *hdr;
    struct iwn_cmd_data *tx;
    struct iwn_scan_essid *essid;
    struct iwn_scan_chan *chan;
    struct ieee80211_frame *wh;
    struct ieee80211_rateset *rs;
    struct ieee80211_channel *c;
    struct ieee80211_wcl_scan_plan wclPlan;
    uint8_t *buf, *frm;
    u_int8_t wnm_target_channel = 0;
    uint16_t rxchain, dwell_active, dwell_passive;
    uint8_t txant;
    struct iwn_scan_doorbell_context doorbell;
    int buflen, error, is_active;
    bool wnm_exact_channel = false;
    bool wcl_foreground_5ghz_extended_dwell = false;
    bool wcl_background_5ghz_unassociated_dwell = false;
    bool wcl_background_5ghz_directed_dwell = false;
    bool foreground_5ghz_directed_dwell = false;
    bool ap_sta_pan_priority_changed = false;
    const bool exactWclPlan = wcl_scan &&
        ieee80211_wcl_scan_plan_snapshot(ic, &wclPlan) != 0;
    const u_int8_t scanSsidLength = exactWclPlan ?
        wclPlan.ssid_len : ic->ic_des_esslen;
    const u_int8_t *scanSsid = exactWclPlan ?
        wclPlan.ssid : ic->ic_des_essid;
    const bool activeScan = exactWclPlan ?
        wclPlan.scan_type != IEEE80211_WCL_SCAN_TYPE_PASSIVE :
        scanSsidLength != 0;
    const bool directedSsid = activeScan && scanSsidLength != 0;

    if (out_command_attempted != NULL)
        *out_command_attempted = false;
    if (out_foreground_prepared != NULL)
        *out_foreground_prepared = false;

    wnm_exact_channel =
        iwn_scan_lease_wnm_target_channel(sc, lease_serial,
            &wnm_target_channel);

    buf = (uint8_t *)malloc(IWN_SCAN_MAXSZ, M_DEVBUF, M_NOWAIT | M_ZERO);
    if (buf == NULL) {
        XYLog("%s: could not allocate buffer for scan command\n",
            sc->sc_dev.dv_xname);
        AirportItlwmPostPltiTraceRecord(
            ic, kAirportItlwmPostPltiTraceEventIwnScanCommandRejected);
        return ENOMEM;
    }
    hdr = (struct iwn_scan_hdr *)buf;
    /*
     * Move to the next channel if no frames are received within 10ms
     * after sending the probe request.
     */
    hdr->quiet_time = htole16(10);        /* timeout in milliseconds */
    hdr->quiet_threshold = htole16(1);    /* min # of packets */

    const bool apContextRunning =
        apFirmwareTransitionActive &&
        apFirmwareStage == IWN_AP_STAGE_RUNNING;
    if (bgscan || apContextRunning) {
        int bintval;
        u_int32_t configuredHomeAwayMs = 0;
        const bool hasConfiguredHomeAway =
            airportItlwmGetScanHomeAwayTime(&configuredHomeAwayMs);
        const u_int32_t maxOutMs = hasConfiguredHomeAway ?
            configuredHomeAwayMs : 200U;
        const u_int32_t defaultPauseMs = hasConfiguredHomeAway ?
            configuredHomeAwayMs : 100U;
        const u_int32_t pauseMs =
            ieee80211_wcl_scan_time_or_default(
                exactWclPlan ? wclPlan.home_dwell_ms : 0,
                defaultPauseMs);

        /*
         * DVM applies associated-scan home/away scheduling whenever any
         * RXON context is associated.  PAN/AP therefore keeps this policy
         * even when the primary STA is doing a foreground reconnect scan.
         * A live Tahoe WCL policy replaces both legacy DVM constants; zero
         * deliberately leaves both command fields disabled.
         */
        hdr->max_out = htole32(maxOutMs * 1024U);

        /* Configure scan pauses which service on-channel traffic. */
        bintval = apContextRunning &&
            apFirmwareConfig.beaconInterval != 0 ?
            apFirmwareConfig.beaconInterval :
            (ic->ic_bss->ni_intval ? ic->ic_bss->ni_intval : 100);
        if (pauseMs != 0) {
            hdr->pause_scan = htole32(((pauseMs / bintval) << 22) |
                ((pauseMs % bintval) * 1024U));
        }
    }

    /* Select antennas for scanning. */
    rxchain =
        IWN_RXCHAIN_VALID(sc->rxchainmask) |
        IWN_RXCHAIN_FORCE_MIMO_SEL(sc->rxchainmask) |
        IWN_RXCHAIN_DRIVER_FORCE;
    if ((flags & IEEE80211_CHAN_5GHZ) &&
        sc->hw_type == IWN_HW_REV_TYPE_4965) {
        /*
         * On 4965 ant A and C must be avoided in 5GHz because of a
         * HW bug which causes very weak RSSI values being reported.
         */
        rxchain |= IWN_RXCHAIN_FORCE_SEL(IWN_ANT_B);
    } else    /* Use all available RX antennas. */
        rxchain |= IWN_RXCHAIN_FORCE_SEL(sc->rxchainmask);
    hdr->rxchain = htole16(rxchain);
    hdr->filter = htole32(IWN_FILTER_MULTICAST | IWN_FILTER_BEACON);

    tx = (struct iwn_cmd_data *)(hdr + 1);
    tx->flags = htole32(IWN_TX_AUTO_SEQ);
    tx->id = sc->broadcast_id;
    tx->lifetime = htole32(IWN_LIFETIME_INFINITE);

    if (flags & IEEE80211_CHAN_5GHZ) {
        /* Send probe requests at 6Mbps. */
        tx->plcp = iwn_rates[IWN_RATE_6M_INDEX].plcp;
        rs = &ic->ic_sup_rates[IEEE80211_MODE_11A];
    } else {
        hdr->flags = htole32(IWN_RXON_24GHZ | IWN_RXON_AUTO);
        if (bgscan && sc->hw_type == IWN_HW_REV_TYPE_4965 &&
            sc->rxon.chan > 14) {
            /*
             * 4965 firmware can crash when sending probe requests
             * with CCK rates while associated to a 5GHz AP.
             * Send probe requests at 6Mbps OFDM as a workaround.
             */
            tx->plcp = iwn_rates[IWN_RATE_6M_INDEX].plcp;
        } else {
            /* Send probe requests at 1Mbps. */
            tx->plcp = iwn_rates[IWN_RATE_1M_INDEX].plcp;
            tx->rflags = IWN_RFLAG_CCK;
        }
        rs = &ic->ic_sup_rates[IEEE80211_MODE_11G];
    }
    /* Use the first valid TX antenna. */
    txant = IWN_LSB(sc->txchainmask);
    tx->rflags |= IWN_RFLAG_ANT(txant);

    /* WCL's initial public scan is deliberately undirected.  Give every
     * non-DFS 5 GHz channel a bounded passive listening dwell above one
     * default beacon interval, including regulatory-passive channels.  This
     * neither enables a probe template nor changes their passive flag.
     *
     * The platform can also label a physical discovery request as a
     * background scan while its public interface remains in RUN after reset.
     * Do not use an empty selector or port_valid to recognize that case:
     * both are valid during ordinary live connections.  Instead require all
     * three independent association markers to be absent: net80211's AID,
     * the RXON AID, and RXON's BSS filter.  A normal or malformed live
     * association retains at least the firmware BSS context, so it keeps the
     * existing serving-BSS dwell budget. */
    wcl_foreground_5ghz_extended_dwell = wcl_scan && bgscan == 0 &&
        ic->ic_des_esslen == 0 && (flags & IEEE80211_CHAN_5GHZ) != 0;
    wcl_background_5ghz_unassociated_dwell = wcl_scan && bgscan != 0 &&
        ic->ic_state == IEEE80211_S_RUN && ic->ic_bss != NULL &&
        ic->ic_bss->ni_associd == 0 && le16toh(sc->rxon.associd) == 0 &&
        (le32toh(sc->rxon.filter) & IWN_FILTER_BSS) == 0 &&
        (flags & IEEE80211_CHAN_5GHZ) != 0;

    /* AppleBCMWLAN treats scan type 1 as active even when the SSID selector
     * is empty.  DVM already builds a wildcard probe template below, so keep
     * active transmission independent of the optional directed ESSID. */
    is_active = activeScan ? 1 : 0;

    /*
     * If we're scanning for a specific SSID, add it to the command.
     */
    essid = (struct iwn_scan_essid *)(tx + 1);
    if (scanSsidLength != 0) {
        essid[0].id = IEEE80211_ELEMID_SSID;
        essid[0].len = scanSsidLength;
        memcpy(essid[0].data, scanSsid, scanSsidLength);

    }
    /* An associated WCL scan for a selected SSID is an actual directed
     * active scan.  Its 24 ms 5 GHz dwell is too short to reliably collect
     * a weak probe response, while the existing passive dwell has already
     * been limited to the serving BSS beacon budget.  Some NVM-allowed,
     * non-DFS 5 GHz channels remain regulatory-passive: firmware must not
     * probe there until it hears traffic, so a sub-beacon 85 ms listen can
     * miss the only beacon that would unlock directed discovery.  Give only
     * that passive directed-WCL case a bounded full-beacon listen; retain the
     * passive flag and leave DFS channels untouched.  For non-passive
     * channels, raise only the active portion below the existing budget.
     * Undirected, foreground, and non-WCL scans keep their prior behavior. */
    wcl_background_5ghz_directed_dwell = wcl_scan && bgscan != 0 &&
        directedSsid && (flags & IEEE80211_CHAN_5GHZ) != 0;
    /* A public ASSOCIATE can replace an undirected discovery command with a
     * foreground directed scan.  On an NVM-passive non-DFS channel, the
     * legacy 110 ms budget is only 7.6 ms above a normal 100-TU beacon
     * interval and can expire before firmware accounts the beacon and emits
     * the permitted directed probe.  Give every foreground directed 5 GHz
     * join one bounded full-beacon margin.  The channel remains passive and
     * DFS remains excluded, so this does not authorize a new transmission. */
    foreground_5ghz_directed_dwell = bgscan == 0 && directedSsid &&
        (flags & IEEE80211_CHAN_5GHZ) != 0;
    /*
     * Build a probe request frame.  Most of the following code is a
     * copy & paste of what is done in net80211.
     */
    wh = (struct ieee80211_frame *)(essid + 20);
    wh->i_fc[0] = IEEE80211_FC0_VERSION_0 | IEEE80211_FC0_TYPE_MGT |
        IEEE80211_FC0_SUBTYPE_PROBE_REQ;
    wh->i_fc[1] = IEEE80211_FC1_DIR_NODS;
    IEEE80211_ADDR_COPY(wh->i_addr1, etherbroadcastaddr);
    IEEE80211_ADDR_COPY(wh->i_addr2, ic->ic_myaddr);
    IEEE80211_ADDR_COPY(wh->i_addr3, etherbroadcastaddr);
    *(uint16_t *)&wh->i_dur[0] = 0;    /* filled by HW */
    *(uint16_t *)&wh->i_seq[0] = 0;    /* filled by HW */

    frm = (uint8_t *)(wh + 1);
    frm = ieee80211_add_ssid(frm, NULL, 0);
    frm = ieee80211_add_rates(frm, rs);
    if (rs->rs_nrates > IEEE80211_RATE_SIZE)
        frm = ieee80211_add_xrates(frm, rs);
    if (ic->ic_flags & IEEE80211_F_HTON)
        frm = ieee80211_add_htcaps(frm, ic);

    /* Set length of probe request. */
    tx->len = htole16(frm - (uint8_t *)wh);

    /*
     * If active scanning is requested but a certain channel is
     * marked passive, we can do active scanning if we detect
     * transmissions.
     *
     * There is an issue with some firmware versions that triggers
     * a sysassert on a "good CRC threshold" of zero (== disabled),
     * on a radar channel even though this means that we should NOT
     * send probes.
     *
     * The "good CRC threshold" is the number of frames that we
     * need to receive during our dwell time on a channel before
     * sending out probes -- setting this to a huge value will
     * mean we never reach it, but at the same time work around
     * the aforementioned issue. Thus use IWN_GOOD_CRC_TH_NEVER
     * here instead of IWN_GOOD_CRC_TH_DISABLED.
     *
     * This was fixed in later versions along with some other
     * scan changes, and the threshold behaves as a flag in those
     * versions.
     */

    /*
     * If we're doing active scanning, set the crc_threshold
     * to a suitable value.  This is different to active veruss
     * passive scanning depending upon the channel flags; the
     * firmware will obey that particular check for us.
     */
    if (sc->tlv_feature_flags & IWN_UCODE_TLV_FLAGS_NEWSCAN)
        hdr->crc_threshold = is_active ?
            IWN_GOOD_CRC_TH_DEFAULT : IWN_GOOD_CRC_TH_DISABLED;
    else
        hdr->crc_threshold = is_active ?
            IWN_GOOD_CRC_TH_DEFAULT : IWN_GOOD_CRC_TH_NEVER;

    chan = (struct iwn_scan_chan *)frm;
    for (c  = &ic->ic_channels[1];
         c <= &ic->ic_channels[IEEE80211_CHAN_MAX]; c++) {
        if ((c->ic_flags & flags) != flags)
            continue;
        if (exactWclPlan &&
            !ieee80211_wcl_scan_plan_channel_allowed(ic, &wclPlan, c))
            continue;
        if (wnm_exact_channel &&
            ieee80211_chan2ieee(ic, c) != wnm_target_channel)
            continue;

        chan->chan = htole16(ieee80211_chan2ieee(ic, c));
        chan->flags = 0;
        if (is_active != 0)
            chan->flags |= htole32(IWN_CHAN_NPBREQS(1));

        if (c->ic_flags & IEEE80211_CHAN_PASSIVE)
            chan->flags |= htole32(IWN_CHAN_PASSIVE);
        else
            chan->flags |= htole32(IWN_CHAN_ACTIVE);

        /*
         * Calculate the active/passive dwell times.
         */

        dwell_active = iwn_get_active_dwell_time(sc, flags, is_active);
        dwell_passive = iwn_get_passive_dwell_time(sc, flags);
        if (exactWclPlan) {
            dwell_active = static_cast<uint16_t>(
                ieee80211_wcl_scan_time_or_default(
                    wclPlan.active_dwell_ms, dwell_active));
            dwell_passive = static_cast<uint16_t>(
                ieee80211_wcl_scan_time_or_default(
                    wclPlan.passive_dwell_ms, dwell_passive));
        }
        if (!exactWclPlan && foreground_5ghz_directed_dwell &&
            (c->ic_flags & IEEE80211_CHAN_PASSIVE) != 0 &&
            (c->ic_flags & IEEE80211_CHAN_DFS) == 0)
            dwell_passive = MAX(dwell_passive, 130);
        if (!exactWclPlan &&
            (wcl_foreground_5ghz_extended_dwell ||
             wcl_background_5ghz_unassociated_dwell ||
             (wcl_background_5ghz_directed_dwell &&
              (c->ic_flags & IEEE80211_CHAN_PASSIVE) != 0)) &&
            (c->ic_flags & IEEE80211_CHAN_DFS) == 0)
            dwell_passive = MAX(dwell_passive, 130);
        if (!exactWclPlan && wcl_background_5ghz_directed_dwell &&
            (c->ic_flags & (IEEE80211_CHAN_PASSIVE |
                            IEEE80211_CHAN_DFS)) == 0 &&
            dwell_passive > dwell_active)
            dwell_active = MAX(dwell_active,
                MIN((uint16_t)40, (uint16_t)(dwell_passive - 1)));

        /* Exact Apple dwell requests and discovery extensions are upper
         * policy, not permission to exceed DVM's live STA/PAN TBTT or
         * off-channel budget. In particular 110 TU passive with max_out of
         * 110 * 1024 us violates firmware's strict inequality and can yield
         * an empty passive scan despite an accepted command. Do not repair
         * active/passive ordering by raising passive past that ceiling. */
        uint16_t quietTime = le16toh(hdr->quiet_time);
        if (!iwn_bound_scan_dwell(iwn_limit_dwell(sc, UINT16_MAX),
                le32toh(hdr->max_out), &dwell_active, &dwell_passive,
                &quietTime)) {
            XYLog("%s: scan has no legal DVM dwell budget\n", DEVNAME(sc));
            explicit_bzero(buf, IWN_SCAN_MAXSZ);
            ::free(buf);
            return EINVAL;
        }
        hdr->quiet_time = htole16(quietTime);

        chan->active = htole16(dwell_active);
        chan->passive = htole16(dwell_passive);

        chan->dsp_gain = 0x6e;
        if (IEEE80211_IS_CHAN_5GHZ(c)) {
            chan->rf_gain = 0x3b;
        } else {
            chan->rf_gain = 0x28;
        }
        hdr->nchan++;
        chan++;
    }

    /*
     * A malformed or unsupported Neighbor Report channel must fail before
     * prepare_scan() or the firmware doorbell.  The BTM caller will reject
     * that candidate; never submit a zero-channel command or silently widen
     * it to channels the AP did not nominate.
     */
    if (hdr->nchan == 0) {
        AirportItlwmPostPltiTraceRecord(
            ic, kAirportItlwmPostPltiTraceEventIwnScanCommandRejected);
        explicit_bzero(buf, IWN_SCAN_MAXSZ);
        ::free(buf);
        return EINVAL;
    }

    buflen = (uint8_t *)chan - buf;
    hdr->len = htole16(buflen);

    /*
     * Linux DVM sets STATUS_SCAN_HW and programs WIPAN_PARAMS immediately
     * before REPLY_SCAN_CMD.  Preserve that command order here: with the AP
     * context active, the old 20 TU BSS slot is shorter than one channel
     * dwell and firmware aborts or never completes the physical scan.
     */
    if (apFirmwareTransitionActive &&
        apFirmwareStage == IWN_AP_STAGE_RUNNING &&
        !apStaScanPriorityActive) {
        error = iwn_set_ap_sta_scan_priority(true);
        if (error != 0) {
            AirportItlwmPostPltiTraceRecord(
                ic, kAirportItlwmPostPltiTraceEventIwnScanCommandRejected);
            explicit_bzero(buf, IWN_SCAN_MAXSZ);
            ::free(buf);
            return error;
        }
        ap_sta_pan_priority_changed = true;
    }

    /* All buffer allocation and command construction is now complete.  The
     * direct normal foreground path still needs net80211's preparation, but
     * doing it here avoids a reset for a merely failed malloc/build above.
     * A revoke after this point is deliberately fail-closed by the caller:
     * prepare_scan() has changed nodes/mode and cannot be rolled back. */
    if (prepare_controller_foreground) {
        bool abort_requested = false;

        if (lease_serial == 0 ||
            !iwn_scan_lease_arm_submission(sc, lease_serial,
                                           &abort_requested) ||
            abort_requested) {
            AirportItlwmPostPltiTraceRecord(
                ic, kAirportItlwmPostPltiTraceEventIwnScanCommandRejected);
            if (ap_sta_pan_priority_changed)
                (void)iwn_set_ap_sta_scan_priority(false);
            explicit_bzero(buf, IWN_SCAN_MAXSZ);
            ::free(buf);
            return ECANCELED;
        }
        if (publish_wcl_initial_started)
            ieee80211_free_allnodes(ic, 1 /* fresh initial census */);
        iwn_prepare_controller_foreground_scan(ic);
        if (out_foreground_prepared != NULL)
            *out_foreground_prepared = true;
    }

    explicit_bzero(&doorbell, sizeof(doorbell));
    doorbell.serial = lease_serial;
    doorbell.upper_generation = upper_generation;
    doorbell.backend_generation = backend_generation;
    doorbell.background = bgscan != 0;
    doorbell.publish_wcl_initial_started = publish_wcl_initial_started;
    if (lease_serial != 0) {
        error = iwn_cmd_with_doorbell_hook(sc, IWN_CMD_SCAN, buf, buflen, 1,
                                           iwn_scan_lease_prepare_doorbell,
                                           iwn_scan_lease_finish_doorbell,
                                           &doorbell);
        if (out_command_attempted != NULL)
            *out_command_attempted = doorbell.committed;
    } else {
        error = iwn_cmd(sc, IWN_CMD_SCAN, buf, buflen, 1);
        if (out_command_attempted != NULL)
            *out_command_attempted = error == 0;
    }
    if (error == 0) {
        /*
         * The current mode might have been fixed during association.
         * Ensure all channels get scanned.
         */
        if (IFM_MODE(ic->ic_media.ifm_cur->ifm_media) == IFM_AUTO)
            ieee80211_setmode(ic, IEEE80211_MODE_AUTO);

        AirportItlwmPostPltiTraceRecord(
            ic, kAirportItlwmPostPltiTraceEventIwnScanStarted);
    } else {
        AirportItlwmPostPltiTraceRecord(
            ic, kAirportItlwmPostPltiTraceEventIwnScanCommandRejected);
        if (ap_sta_pan_priority_changed)
            (void)iwn_set_ap_sta_scan_priority(false);
    }
    ::free(buf);
    return error;
}

void ItlIwn::
iwn_scan_abort(struct iwn_softc *sc)
{
    u_int64_t serial = 0;
    bool submit_abort = false;

    if (!iwn_scan_lease_mark_abort(sc, IWN_SCAN_LEASE_GENERIC_BACKGROUND,
                                   0, &serial, &submit_abort))
        return;
    if (!submit_abort)
        return;
    if (iwn_cmd(sc, IWN_CMD_SCAN_ABORT, NULL, 0, 1) != 0) {
        iwn_scan_lease_abort_submission_failed(sc, serial);
        sc->sc_flags |= IWN_FLAG_FATAL_RECOVERY;
        (void)task_add(systq, &sc->init_task);
    }
}

int ItlIwn::
iwn_wnm_bgscan_abort(struct ieee80211com *ic)
{
    struct iwn_softc *sc;
    ItlIwn *that;
    u_int64_t serial = 0;
    bool submit_abort = false;

    if (ic == NULL || (sc = (struct iwn_softc *)ic->ic_softc) == NULL ||
        (sc->sc_flags & IWN_FLAG_BGSCAN) == 0)
        return EBUSY;
    that = container_of(sc, ItlIwn, com);
    if (!iwn_scan_lease_mark_abort(sc, IWN_SCAN_LEASE_NONE, 0,
                                   &serial, &submit_abort))
        return EBUSY;
    if (!submit_abort)
        return 0;
    if (that->iwn_cmd(sc, IWN_CMD_SCAN_ABORT, NULL, 0, 1) == 0)
        return 0;
    iwn_scan_lease_abort_submission_failed(sc, serial);
    sc->sc_flags |= IWN_FLAG_FATAL_RECOVERY;
    (void)task_add(systq, &sc->init_task);
    return EIO;
}

int ItlIwn::
iwn_bgscan(struct ieee80211com *ic, u_int64_t reassoc_serial)
{
    struct iwn_softc *sc = (struct iwn_softc *)ic->ic_softc;
    ItlIwn *that = container_of(sc, ItlIwn, com);
    u_int8_t wnm_target_channel = 0;
    uint16_t flags = IEEE80211_CHAN_2GHZ;
    int error;

    /*
     * Tahoe's reference roam path keeps a firmware user-roam channel cache
     * and has a distinct setROAMWithChannel operation.  A protected BTM
     * Neighbor Report is the equivalent exact-channel owner here: verify its
     * BSSID freshly on that one advertised channel instead of turning one
     * steering request into a minute-long all-band census.  A request with
     * no concrete channel retains the ordinary complete background scan.
     */
    if (ieee80211_wnm_bss_transition_target_channel(ic,
            &wnm_target_channel) != 0 && wnm_target_channel > 14)
        flags = IEEE80211_CHAN_5GHZ;
    error = that->iwn_scan_start(sc, flags, 1,
        IWN_SCAN_LEASE_GENERIC_BACKGROUND, 0, 0, NULL, 0, reassoc_serial);
    if (error)
        XYLog("%s: could not initiate background scan\n",
            sc->sc_dev.dv_xname);
    return error;
}

static bool
iwn_ht40_pair_permitted(struct ieee80211_node *ni, uint8_t sco)
{
    if (ni == NULL || ni->ni_chan == NULL)
        return false;

    return IwnHt40Contracts::allowsLocalDirection(
        ieee80211_node_supports_ht_chan40(ni), sco,
        IEEE80211_IS_CHAN_HT40U(ni->ni_chan),
        IEEE80211_IS_CHAN_HT40D(ni->ni_chan));
}

void ItlIwn::
iwn_rxon_configure_ht40(struct ieee80211com *ic, struct ieee80211_node *ni)
{
    struct iwn_softc *sc = (struct iwn_softc *)ic->ic_softc;

    sc->rxon.flags &= ~htole32(IWN_RXON_HT_CHANMODE_MIXED2040 |
                               IWN_RXON_HT_CHANMODE_PURE40 | IWN_RXON_HT_HT40MINUS);

    if (ni == NULL)
        return;

    uint8_t sco = (ni->ni_htop0 & IEEE80211_HTOP0_SCO_MASK);
    int htprot = (ni->ni_htop1 &
                  IEEE80211_HTOP1_PROT_MASK);

    if (iwn_ht40_pair_permitted(ni, sco)) {
        if (sco == IEEE80211_HTOP0_SCO_SCB)
            sc->rxon.flags |= htole32(IWN_RXON_HT_HT40MINUS);
        if (htprot == IEEE80211_HTPROT_20MHZ)
            sc->rxon.flags |= htole32(IWN_RXON_HT_CHANMODE_PURE40);
        else
            sc->rxon.flags |= htole32(
                                      IWN_RXON_HT_CHANMODE_MIXED2040);
    }
}

int ItlIwn::
iwn_rxon_ht40_enabled(struct iwn_softc *sc)
{
    return ((le32toh(sc->rxon.flags) & IWN_RXON_HT_CHANMODE_MIXED2040) ||
            (le32toh(sc->rxon.flags) & IWN_RXON_HT_CHANMODE_PURE40)) ? 1 : 0;
}

int ItlIwn::
iwn_auth(struct iwn_softc *sc, int arg)
{
    struct iwn_ops *ops = &sc->ops;
    struct ieee80211com *ic = &sc->sc_ic;
    struct ieee80211_node *ni = ic->ic_bss;
    int error, ridx;
    int bss_switch =
        (!IEEE80211_ADDR_EQ(sc->bss_node_addr, etheranyaddr) &&
        !IEEE80211_ADDR_EQ(sc->bss_node_addr, ni->ni_macaddr));

    /*
     * Authentication is the other DVM "active but unassociated" BSS case.
     * Mark the station context unassociated now, but preserve Linux DVM's
     * command order below: first commit the unassociated BSS RXON carrying
     * the candidate channel, then recompute PAN slots for that new context.
     */
    apStaBssAssociated = false;

    /* Update adapter configuration. */
    IEEE80211_ADDR_COPY(sc->rxon.bssid, ni->ni_bssid);
    sc->rxon.chan = ieee80211_chan2ieee(ic, ni->ni_chan);
    sc->rxon.flags = htole32(IWN_RXON_TSF | IWN_RXON_CTS_TO_SELF);
    if (IEEE80211_IS_CHAN_2GHZ(ni->ni_chan)) {
        sc->rxon.flags |= htole32(IWN_RXON_AUTO | IWN_RXON_24GHZ);
        if (ic->ic_flags & IEEE80211_F_USEPROT)
            sc->rxon.flags |= htole32(IWN_RXON_TGG_PROT);
    }
    if (ic->ic_flags & IEEE80211_F_SHSLOT)
        sc->rxon.flags |= htole32(IWN_RXON_SHSLOT);
    else
        sc->rxon.flags &= ~htole32(IWN_RXON_SHSLOT);
    if (ic->ic_flags & IEEE80211_F_SHPREAMBLE)
        sc->rxon.flags |= htole32(IWN_RXON_SHPREAMBLE);
    else
        sc->rxon.flags &= ~htole32(IWN_RXON_SHPREAMBLE);
    switch (ic->ic_curmode) {
    case IEEE80211_MODE_11A:
        sc->rxon.cck_mask  = 0;
        sc->rxon.ofdm_mask = 0x15;
        break;
    case IEEE80211_MODE_11B:
        sc->rxon.cck_mask  = 0x03;
        sc->rxon.ofdm_mask = 0;
        break;
    default:    /* Assume 802.11b/g/n. */
        sc->rxon.cck_mask  = 0x0f;
        sc->rxon.ofdm_mask = 0x15;
    }
    /* Configure 40MHz early to avoid problems on 6205 devices. */
    iwn_rxon_configure_ht40(ic, ni);
    /*
     * Stock/Apple behavior: do NOT set IWN_FILTER_BSS during AUTH. It is
     * set only in iwn_run() after association (with a valid associd and
     * TSF timing). Asserting BSS/associated mode pre-association with
     * associd=0 makes the dvm firmware refuse to activate the data TX
     * FIFO (scd read-ptr for the mgmt/data queue never advances), which
     * silently drops the AUTH(seq=1) frame. AUTH responses are still
     * received in the non-BSS RXON state.
     */
    /*
     * Sync the RXON station address to the CURRENT interface MAC. macOS
     * assigns a randomized (locally-administered) MAC per network via
     * setHardwareAddress()->if_setlladdr(), which updates ic_myaddr, but
     * rxon.myaddr was captured once at iwn_config() time from the original
     * EEPROM MAC. Management frames go out with SA=ic_myaddr (the random
     * MAC), so if rxon.myaddr is stale the firmware filters RX/ACK on the
     * wrong address: the AP ACKs and sends AUTH(seq=2) to the random MAC,
     * the firmware ignores both, TX reports ackfailcnt=max, and auth times
     * out. Refresh myaddr/wlap here so the on-air SA and the firmware's
     * receive-address filter agree.
     */
    IEEE80211_ADDR_COPY(sc->rxon.myaddr, ic->ic_myaddr);
    IEEE80211_ADDR_COPY(sc->rxon.wlap, ic->ic_myaddr);
    error = iwn_cmd(sc, IWN_CMD_RXON, &sc->rxon, sc->rxonsz, 1);
    if (error != 0) {
        XYLog("%s: RXON command failed\n", sc->sc_dev.dv_xname);
        return error;
    }

    /* iwlagn_commit_rxon() performs iwlagn_rxon_disconn() before
     * iwlagn_set_pan_params().  Sending WIPAN_PARAMS against the old BSS
     * channel leaves the two-context scheduler on the previous tune and can
     * make the replacement BSS miss every beacon while PAN remains live. */
    error = iwn_set_ap_sta_auth_priority(true);
    if (error != 0) {
        XYLog("%s: could not prioritize STA PAN slot after auth RXON\n",
              sc->sc_dev.dv_xname);
        return error;
    }

    /* Configuration has changed, set TX power accordingly. */
    if ((error = ops->set_txpower(sc, 1)) != 0) {
        XYLog("%s: could not set TX power\n", sc->sc_dev.dv_xname);
        return error;
    }
    /*
     * Reconfiguring RXON clears the firmware nodes table so we must
     * add the broadcast node again.
     */
    ridx = IEEE80211_IS_CHAN_5GHZ(ni->ni_chan) ?
        IWN_RIDX_OFDM : IWN_RIDX_CCK;
    if ((error = iwn_add_broadcast_node(sc, 1, ridx)) != 0) {
        XYLog("%s: could not add broadcast node\n",
            sc->sc_dev.dv_xname);
        return error;
    }

    /*
     * Stock/Apple behavior: do NOT add the BSS node here. Reconfiguring
     * RXON clears the firmware node table, and pre-association AUTH is
     * transmitted via the broadcast/aux station (id=broadcast_id), not a
     * unicast BSS node. The BSS node is added in iwn_run() after the
     * association response has supplied a valid AID, but before committing
     * the associated RXON. Adding it during AUTH (together with FILTER_BSS)
     * put the firmware into associated mode with associd=0 and stalled the
     * data TX FIFO.
     */

    /*
     * Make sure the firmware gets to see a beacon before we send
     * the auth request. Otherwise the Tx attempt can fail due to
     * the firmware's built-in regulatory domain enforcement.
     * Delaying here for every incoming deauth frame can result in a DoS.
     * Don't delay if we're here because of an incoming frame (arg != -1)
     * or if we're already waiting for a response (ic_mgt_timer != 0).
     * If we are switching APs after a background scan then net80211 has
     * just faked the reception of a deauth frame from our old AP, so it
     * is safe to delay in that case.
     */
    if ((arg == -1 || bss_switch) && ic->ic_mgt_timer == 0)
        DELAY(ni->ni_intval * 3 * IEEE80211_DUR_TU);

    /* We can now clear the cached address of our previous AP. */
    memset(sc->bss_node_addr, 0, sizeof(sc->bss_node_addr));

    return 0;
}

int ItlIwn::
iwn_run(struct iwn_softc *sc)
{
    struct iwn_ops *ops = &sc->ops;
    struct ieee80211com *ic = &sc->sc_ic;
    struct ieee80211_node *ni = ic->ic_bss;
    struct iwn_node *wn = (struct iwn_node *)ni;
    int error;

    if (ic->ic_opmode == IEEE80211_M_MONITOR) {
        /* Link LED blinks while monitoring. */
        iwn_set_led(sc, IWN_LED_LINK, 50, 50);
        return 0;
    }

    const bool apStaRunFence =
        apFirmwareTransitionActive &&
        apFirmwareStage == IWN_AP_STAGE_RUNNING;
    if (apStaRunFence) {
        if (apStaRunPanFencePending) {
            XYLog("%s: APSTA associated BSS command fence already pending\n",
                  sc->sc_dev.dv_xname);
            return EBUSY;
        }
        iwn_set_ap_primary_tx_quiesced(true, false);
    }

    /* Update adapter configuration. */
    sc->rxon.associd = htole16(IEEE80211_AID(ni->ni_associd));
    /* Short preamble and slot time are negotiated when associating. */
    sc->rxon.flags &= ~htole32(IWN_RXON_SHPREAMBLE | IWN_RXON_SHSLOT);
    if (ic->ic_flags & IEEE80211_F_SHSLOT)
        sc->rxon.flags |= htole32(IWN_RXON_SHSLOT);
    if (ic->ic_flags & IEEE80211_F_SHPREAMBLE)
        sc->rxon.flags |= htole32(IWN_RXON_SHPREAMBLE);
    sc->rxon.filter |= htole32(IWN_FILTER_BSS);
    /* Firmware otherwise decrypts unicast CCMP before delivery.  PMF uses
     * one software pairwise key for data and robust management alike, so
     * disable that firmware path for the entire negotiated MFP session. */
    if (ni->ni_flags & IEEE80211_NODE_MFP)
        sc->rxon.filter |= htole32(IWN_FILTER_NODECRYPT);
    else
        sc->rxon.filter &= ~htole32(IWN_FILTER_NODECRYPT);

    /* HT is negotiated when associating. */
    if (ni->ni_flags & IEEE80211_NODE_HT) {
        enum ieee80211_htprot htprot =
            (ieee80211_htprot)(ni->ni_htop1 & IEEE80211_HTOP1_PROT_MASK);
        sc->rxon.flags |= htole32(IWN_RXON_HT_PROTMODE(htprot));
    } else
        sc->rxon.flags &= ~htole32(IWN_RXON_HT_PROTMODE(3));
    iwn_rxon_configure_ht40(ic, ni);

    if (IEEE80211_IS_CHAN_5GHZ(ni->ni_chan)) {
        /* 11a or 11n 5GHz */
        sc->rxon.cck_mask  = 0;
        sc->rxon.ofdm_mask = 0x15;
    } else if (ni->ni_flags & IEEE80211_NODE_HT) {
        /* 11n 2GHz */
        sc->rxon.cck_mask  = 0x0f;
        sc->rxon.ofdm_mask = 0x15;
    } else {
        if (ni->ni_rates.rs_nrates == 4) {
            /* 11b */
            sc->rxon.cck_mask  = 0x03;
            sc->rxon.ofdm_mask = 0;
        } else {
            /* assume 11g */
            sc->rxon.cck_mask  = 0x0f;
            sc->rxon.ofdm_mask = 0x15;
        }
    }
    /* Keep the RXON station address synced to the current (possibly
     * macOS-randomized) interface MAC; see the note in iwn_auth(). */
    IEEE80211_ADDR_COPY(sc->rxon.myaddr, ic->ic_myaddr);
    IEEE80211_ADDR_COPY(sc->rxon.wlap, ic->ic_myaddr);

    /* Linux DVM's documented connect transaction adds the AP station before
     * the associated RXON.  The preceding unassociated RXON has already
     * selected the candidate BSSID/channel and rebuilt the broadcast station;
     * installing the negotiated BSS station now lets the associated commit
     * retain a valid unicast owner instead of briefly exposing an associated
     * context with no AP station.  That transient ordering can leave the DVM
     * data queue inert even though net80211 has opened the controlled port. */
    error = iwn_add_bss_node(sc, ni);
    if (error != 0) {
        XYLog("%s: could not add BSS node before associated RXON\n",
              sc->sc_dev.dv_xname);
        if (apStaRunFence)
            iwn_abort_ap_sta_run_pan_fence(true);
        return error;
    }

    /* DVM's connect transaction installs the selected station first, then
     * programs its beacon timing immediately before the associated RXON. */
    if ((error = iwn_set_timing(sc, ni)) != 0) {
        XYLog("%s: could not set timing\n", sc->sc_dev.dv_xname);
        if (apStaRunFence)
            iwn_abort_ap_sta_run_pan_fence(true);
        return error;
    }

    error = iwn_cmd(sc, IWN_CMD_RXON, &sc->rxon, sc->rxonsz, 1);
    if (error != 0) {
        XYLog("%s: could not update configuration\n",
            sc->sc_dev.dv_xname);
        if (apStaRunFence)
            iwn_abort_ap_sta_run_pan_fence(true);
        return error;
    }
    /* Diagnostic publication (auth-ACK boundary, iwn HAL): publish the
     * post-IWN_CMD_RXON BSSID/channel/filter/flags/associd state in
     * iwn_run to the AirportItlwm IOService property table. Mirrors the
     * iwn_auth publication but adds associd because iwn_run sets
     * IWN_FILTER_BSS and the negotiated associd before re-issuing
     * IWN_CMD_RXON. The property is durable across unified-log
     * eviction. Behavior-neutral: setProperty publishes into the
     * IOService property dictionary and does not change driver/firmware
     * state, control flow, or scheduling. */
    {
        char post_rxon_buf[160];
        snprintf(post_rxon_buf, sizeof(post_rxon_buf),
            "bssid=%02x:%02x:%02x:%02x:%02x:%02x chan=%u "
            "filter=0x%08x flags=0x%08x associd=0x%04x",
            sc->rxon.bssid[0], sc->rxon.bssid[1],
            sc->rxon.bssid[2], sc->rxon.bssid[3],
            sc->rxon.bssid[4], sc->rxon.bssid[5],
            (unsigned)sc->rxon.chan,
            (unsigned)le32toh(sc->rxon.filter),
            (unsigned)le32toh(sc->rxon.flags),
            (unsigned)le16toh(sc->rxon.associd));
        getController()->setProperty("itlwm-iwn-run-post-rxon",
            post_rxon_buf);
    }

    /* Configuration has changed, set TX power accordingly. */
    if ((error = ops->set_txpower(sc, 1)) != 0) {
        XYLog("%s: could not set TX power\n", sc->sc_dev.dv_xname);
        if (apStaRunFence)
            iwn_abort_ap_sta_run_pan_fence(true);
        return error;
    }

    if ((error = iwn_init_sensitivity(sc)) != 0) {
        XYLog("%s: could not set sensitivity\n",
            sc->sc_dev.dv_xname);
        if (apStaRunFence)
            iwn_abort_ap_sta_run_pan_fence(true);
        return error;
    }
    /* Start periodic calibration timer. */
    sc->calib.state = IWN_CALIB_STATE_ASSOC;
    sc->calib_cnt = 0;
    timeout_add_msec(&sc->calib_to, 500);

    ieee80211_ra_node_init(ic, &wn->rn, &wn->ni);

    /*
     * A full BSS RXON retunes the primary context.  Linux DVM retains the
     * concurrently beaconing PAN owner across that commit; replaying the
     * already-built template here restores the same invariant for this
     * backend before the scheduler returns to its steady split.  Without
     * this edge, the replacement STA link runs but a different-channel AP
     * stops emitting beacons and every client leaves with beacon loss.
     */
    if (apFirmwareTransitionActive &&
        apFirmwareStage == IWN_AP_STAGE_RUNNING) {
        error = iwn_send_ap_beacon(&apFirmwareConfig);
        if (error != 0) {
            XYLog("%s: could not replay HostAP beacon after STA RXON\n",
                  sc->sc_dev.dv_xname);
            if (apStaRunFence)
                iwn_abort_ap_sta_run_pan_fence(true);
            return error;
        }
        XYLog("%s: IWN AP beacon replay queued after STA RXON channel=%u\n",
              sc->sc_dev.dv_xname,
              static_cast<unsigned>(apFirmwareConfig.channel));
    }

    /* RXON now carries the negotiated AID/BSS filter.  Leave temporary
     * scan/auth priority and return both associated contexts to DVM's
     * normal admission window.  Linux can wait synchronously for every
     * command in iwlagn_rxon_connect(); this RX notification path cannot.
     * Its exact final WIPAN_PARAMS descriptor therefore acts as the FIFO
     * completion fence for ADD_NODE, LINK_QUALITY, TIMING, associated RXON,
     * TX power, sensitivity, and retained AP-beacon replay.  Keep primary
     * output stopped until firmware acknowledges that descriptor so EAPOL
     * and DHCP cannot overtake the context transaction. */
    apStaBssAssociated = true;
    error = iwn_clear_ap_sta_pan_priority(apStaRunFence);
    if (error != 0) {
        XYLog("%s: could not restore HostAP PAN slots after STA auth\n",
              sc->sc_dev.dv_xname);
        if (apStaRunFence)
            iwn_abort_ap_sta_run_pan_fence(true);
        return error;
    }

    /* Link LED always on while associated. */
    iwn_set_led(sc, IWN_LED_LINK, 0, 1);
    return 0;
}

/*
 * A PMF PAE transaction needs a sleepable software-CCMP preparation phase.
 * IWN's normal pairwise-key command is asynchronous firmware work and cannot
 * be used as that owner: its completion returns through the same RX action
 * that delivered Msg3.  This backend instead allocates only net80211's local
 * CCMP contexts on systq, then lets the generic transaction publish them
 * atomically with its BIP/port-state handoff.
 */
int ItlIwn::
iwn_pae_mfp_txn_submit(struct ieee80211com *ic, u_int64_t txn_id,
                       u_int64_t assoc_epoch, struct ieee80211_node *ni,
                       const struct ieee80211_key *key, u_int8_t stage)
{
    struct iwn_softc *sc;
    struct iwn_mfp_pae_txn *main, *successor, *target = NULL;
    struct iwn_mfp_pae_txn retired_successor;
    struct ieee80211_node *held_ni = NULL;
    IOSimpleLock *bss_lock;
    IOInterruptState irq;
    bool queue_task = false;
    int error = 0;

    if (ic == NULL)
        return EINVAL;
    sc = (struct iwn_softc *)ic->ic_softc;
    /* This must be the first action after obtaining sc: detach may have
     * unpublished the hook but a generic caller may already have copied it. */
    if (!iwn_mfp_pae_callback_enter(sc))
        return ECANCELED;
    explicit_bzero(&retired_successor, sizeof(retired_successor));
    if (ni == NULL || key == NULL || txn_id == 0 || assoc_epoch == 0 ||
        !iwn_mfp_pae_key_valid(key, stage)) {
        error = EINVAL;
        goto out;
    }
    if (!iwn_mfp_runtime_enabled(sc) ||
        (bss_lock = ic->ic_pae_selected_bss_lock) == NULL || systq == NULL) {
        error = ECANCELED;
        goto out;
    }

    /* Hold the BSS until cancellation/commit, not just until this enqueue. */
    held_ni = ieee80211_ref_node(ni);
    /* The generic transaction and selected BSS must still be this exact
     * request when the local record is registered.  This closes the gap
     * where generic cancellation ran before the driver reached its lock. */
    irq = IOSimpleLockLockDisableInterrupt(bss_lock);
    IOSimpleLockLock(sc->sc_mfp_pae_lock);
    if (sc->sc_mfp_pae_detaching || sc->sc_mfp_pae_stopping ||
        !sc->sc_mfp_pae_task_ready ||
        !iwn_mfp_pae_generic_stage_live_locked(ic, txn_id, assoc_epoch, ni,
        stage)) {
        error = ECANCELED;
        goto unlock;
    }
    main = &sc->sc_mfp_pae_txn;
    successor = &sc->sc_mfp_pae_successor;
    if (!main->active) {
        target = main;
    } else if (iwn_mfp_pae_record_matches(main, txn_id, assoc_epoch,
        sc->sc_mfp_pae_lifecycle_generation, ni) && !main->cancelled) {
        target = main;
    } else {
        /* A cancelled or still-running predecessor is not allowed to reject
         * the next selected BSS.  Keep exactly one latest successor; a newer
         * reconnect replaces an unstarted older successor deterministically. */
        if (successor->active) {
            if (iwn_mfp_pae_record_matches(successor, txn_id, assoc_epoch,
                sc->sc_mfp_pae_lifecycle_generation, ni) &&
                !successor->cancelled) {
                target = successor;
            } else if (!successor->task_active) {
                (void)iwn_mfp_pae_take_record_locked(successor,
                    &retired_successor);
            } else {
                error = EIO;
            }
        }
        if (error == 0 && target == NULL)
            target = successor;
    }
    if (error == 0 && target != NULL && !target->active) {
        explicit_bzero(target, sizeof(*target));
        target->active = true;
        target->txn_id = txn_id;
        target->assoc_epoch = assoc_epoch;
        target->lifecycle_generation =
            sc->sc_mfp_pae_lifecycle_generation;
        target->ni = held_ni;
        held_ni = NULL;
    }
    if (error == 0) {
        /* A zero return means this exact stage has a durable local owner.
         * Never report EBUSY for a backend-local collision: generic reserves
         * that value for an already-live generic transaction. */
        if (target == NULL || target->cancelled || target->pending ||
            target->task_active) {
            error = EIO;
        } else {
            target->pending = true;
            target->pending_stage = stage;
            target->pending_key = *key;
            target->pending_key.k_priv = NULL;
            queue_task = true;
        }
    }
unlock:
    IOSimpleLockUnlock(sc->sc_mfp_pae_lock);
    IOSimpleLockUnlockEnableInterrupt(bss_lock, irq);
    iwn_mfp_pae_dispose_record(ic, &retired_successor);
    if (queue_task)
        (void)task_add(systq, &sc->mfp_pae_task);
    if (held_ni != NULL)
        ieee80211_release_node(ic, held_ni);
out:
    iwn_mfp_pae_callback_leave(sc);
    return error;
}

struct IwnMfpPaeCompletionAction {
    struct ieee80211com *ic;
    u_int64_t txn_id;
    u_int8_t stage;
    int error;
};

IOReturn ItlIwn::
iwn_mfp_pae_complete_action(OSObject *target, void *arg0, void *arg1,
                            void *arg2, void *arg3)
{
    struct IwnMfpPaeCompletionAction *completion =
        (struct IwnMfpPaeCompletionAction *)arg0;

    (void)target;
    (void)arg1;
    (void)arg2;
    (void)arg3;
    if (completion == NULL || completion->ic == NULL ||
        completion->txn_id == 0 ||
        !iwn_mfp_pae_stage_valid(completion->stage))
        return kIOReturnBadArgument;
    ieee80211_pae_mfp_txn_complete(completion->ic, completion->txn_id,
        completion->stage, completion->error);
    return kIOReturnSuccess;
}

void ItlIwn::
iwn_mfp_pae_task(void *arg)
{
    struct iwn_softc *sc = (struct iwn_softc *)arg;
    struct ieee80211com *ic;
    struct iwn_mfp_pae_txn *txn;
    struct iwn_mfp_pae_txn retired_main, retired_successor;
    struct ieee80211_key key;
    struct ieee80211_node *ni = NULL;
    IOSimpleLock *bss_lock;
    IOInterruptState irq;
    u_int64_t txn_id = 0, assoc_epoch = 0;
    u_int32_t generation = 0;
    u_int8_t stage = IEEE80211_PAE_MFP_STAGE_NONE;
    bool have_work = false, deliver = false, worker_live = false;
    bool queue_successor = false;
    int error = ECANCELED;

    if (sc == NULL || sc->sc_mfp_pae_lock == NULL ||
        (bss_lock = sc->sc_ic.ic_pae_selected_bss_lock) == NULL)
        return;
    ic = &sc->sc_ic;
    explicit_bzero(&key, sizeof(key));
    explicit_bzero(&retired_main, sizeof(retired_main));
    explicit_bzero(&retired_successor, sizeof(retired_successor));

    irq = IOSimpleLockLockDisableInterrupt(bss_lock);
    IOSimpleLockLock(sc->sc_mfp_pae_lock);
    txn = &sc->sc_mfp_pae_txn;
    if (txn->active && txn->pending && !txn->task_active &&
        !txn->cancelled && !sc->sc_mfp_pae_detaching &&
        !sc->sc_mfp_pae_stopping &&
        txn->lifecycle_generation == sc->sc_mfp_pae_lifecycle_generation &&
        iwn_mfp_pae_generic_stage_live_locked(ic, txn->txn_id,
        txn->assoc_epoch, txn->ni, txn->pending_stage)) {
        txn_id = txn->txn_id;
        assoc_epoch = txn->assoc_epoch;
        generation = txn->lifecycle_generation;
        ni = txn->ni;
        stage = txn->pending_stage;
        key = txn->pending_key;
        explicit_bzero(&txn->pending_key, sizeof(txn->pending_key));
        txn->pending = false;
        txn->pending_stage = IEEE80211_PAE_MFP_STAGE_NONE;
        txn->task_active = true;
        have_work = true;
    } else if (txn->active && !txn->task_active &&
        (txn->cancelled || sc->sc_mfp_pae_detaching ||
        sc->sc_mfp_pae_stopping || txn->lifecycle_generation !=
        sc->sc_mfp_pae_lifecycle_generation)) {
        (void)iwn_mfp_pae_take_record_locked(txn, &retired_main);
        queue_successor = iwn_mfp_pae_promote_successor_locked(sc,
            &retired_successor);
    }
    IOSimpleLockUnlock(sc->sc_mfp_pae_lock);
    IOSimpleLockUnlockEnableInterrupt(bss_lock, irq);
    iwn_mfp_pae_dispose_record(ic, &retired_main);
    iwn_mfp_pae_dispose_record(ic, &retired_successor);
    if (queue_successor && systq != NULL)
        (void)task_add(systq, &sc->mfp_pae_task);
    if (!have_work)
        return;

    /* The context allocation can wait, hence this dedicated serial task.
     * IGTK itself is prepared locally by generic PAE after this ordered
     * acknowledgement; only PTK/GTK need a CCMP context here. */
    irq = IOSimpleLockLockDisableInterrupt(bss_lock);
    IOSimpleLockLock(sc->sc_mfp_pae_lock);
    txn = &sc->sc_mfp_pae_txn;
    worker_live = iwn_mfp_pae_record_matches(txn, txn_id, assoc_epoch,
        generation, ni) && txn->task_active && !txn->cancelled &&
        !sc->sc_mfp_pae_detaching && !sc->sc_mfp_pae_stopping &&
        generation == sc->sc_mfp_pae_lifecycle_generation &&
        iwn_mfp_pae_generic_stage_live_locked(ic, txn_id, assoc_epoch, ni,
        stage);
    IOSimpleLockUnlock(sc->sc_mfp_pae_lock);
    IOSimpleLockUnlockEnableInterrupt(bss_lock, irq);
    if (!worker_live || ni == NULL || !iwn_mfp_pae_key_valid(&key, stage)) {
        error = ECANCELED;
    } else if (stage == IEEE80211_PAE_MFP_STAGE_IGTK) {
        error = 0;
    } else {
        error = ieee80211_set_key(ic, ni, &key);
    }

    irq = IOSimpleLockLockDisableInterrupt(bss_lock);
    IOSimpleLockLock(sc->sc_mfp_pae_lock);
    txn = &sc->sc_mfp_pae_txn;
    if (iwn_mfp_pae_record_matches(txn, txn_id, assoc_epoch, generation,
        ni) && txn->task_active) {
        txn->task_active = false;
        if (txn->cancelled || sc->sc_mfp_pae_detaching ||
            sc->sc_mfp_pae_stopping || generation !=
            sc->sc_mfp_pae_lifecycle_generation ||
            !iwn_mfp_pae_generic_stage_live_locked(ic, txn_id,
            assoc_epoch, ni, stage)) {
            (void)iwn_mfp_pae_take_record_locked(txn, &retired_main);
            queue_successor = iwn_mfp_pae_promote_successor_locked(sc,
                &retired_successor);
        } else if (error == 0) {
            if (stage == IEEE80211_PAE_MFP_STAGE_PTK) {
                if (txn->ptk_key.k_priv != NULL)
                    error = EIO;
                else {
                    txn->ptk_key = key;
                    explicit_bzero(&key, sizeof(key));
                }
            } else if (stage == IEEE80211_PAE_MFP_STAGE_GTK) {
                if (txn->gtk_key.k_priv != NULL)
                    error = EIO;
                else {
                    txn->gtk_key = key;
                    explicit_bzero(&key, sizeof(key));
                }
            }
            if (error == 0) {
                txn->accepted_mask |= iwn_mfp_pae_stage_mask(stage);
                deliver = true;
                const uint32_t trace_stage_event =
                    iwn_mfp_pae_trace_stage_event(stage);
                if (trace_stage_event !=
                    kAirportItlwmPostPltiTraceEventUnknown)
                    AirportItlwmPostPltiTraceRecord(ic, trace_stage_event);
            } else
                deliver = true;
        } else {
            /* Generic completion owns the matching cancellation and the
             * previously prepared contexts after this terminal callback. */
            deliver = true;
        }
    }
    IOSimpleLockUnlock(sc->sc_mfp_pae_lock);
    IOSimpleLockUnlockEnableInterrupt(bss_lock, irq);
    iwn_mfp_pae_dispose_record(ic, &retired_main);
    iwn_mfp_pae_dispose_record(ic, &retired_successor);
    if (queue_successor && systq != NULL)
        (void)task_add(systq, &sc->mfp_pae_task);
    iwn_mfp_pae_dispose_key(ic, &key);
    if (deliver) {
        ItlIwn *that = container_of(sc, ItlIwn, com);
        struct IwnMfpPaeCompletionAction completion;
        IOCommandGate *gate = that->getMainCommandGate();

        completion.ic = ic;
        completion.txn_id = txn_id;
        completion.stage = stage;
        completion.error = error;

        /* Generic completion can enqueue the transaction's sole EAPOL reply.
         * IWN if_start() deliberately uses attemptAction(); from systq that
         * non-blocking gate entry can lose the only M4 kick while the main
         * workloop is busy.  Complete on the main gate instead, where
         * if_start() enters recursively and publishes the q0 doorbell before
         * PTK/GTK/IGTK and port-valid become live. */
        if (gate == NULL || gate->runAction(iwn_mfp_pae_complete_action,
            &completion) != kIOReturnSuccess) {
            /* A disappearing command gate is a lifecycle failure.  Retire the
             * exact generic transaction instead of leaving its key owner live
             * with an EAPOL reply stranded in if_snd. */
            ieee80211_pae_mfp_txn_complete(ic, txn_id, stage, EIO);
        }
        explicit_bzero(&completion, sizeof(completion));
    }
}

void ItlIwn::
iwn_pae_mfp_txn_cancel(struct ieee80211com *ic, u_int64_t txn_id)
{
    struct iwn_softc *sc;
    struct iwn_mfp_pae_txn retired_main, retired_successor;
    bool queue_successor = false;

    if (ic == NULL)
        return;
    sc = (struct iwn_softc *)ic->ic_softc;
    if (!iwn_mfp_pae_callback_enter(sc))
        return;
    explicit_bzero(&retired_main, sizeof(retired_main));
    explicit_bzero(&retired_successor, sizeof(retired_successor));
    if (txn_id == 0 || !iwn_mfp_runtime_enabled(sc))
        goto out;

    IOSimpleLockLock(sc->sc_mfp_pae_lock);
    if (sc->sc_mfp_pae_txn.active &&
        sc->sc_mfp_pae_txn.txn_id == txn_id) {
        if (iwn_mfp_pae_cancel_record_locked(&sc->sc_mfp_pae_txn,
            &retired_main)) {
            queue_successor = iwn_mfp_pae_promote_successor_locked(sc,
                &retired_successor);
        }
    } else if (sc->sc_mfp_pae_successor.active &&
        sc->sc_mfp_pae_successor.txn_id == txn_id) {
        (void)iwn_mfp_pae_cancel_record_locked(&sc->sc_mfp_pae_successor,
            &retired_successor);
    }
    IOSimpleLockUnlock(sc->sc_mfp_pae_lock);
    iwn_mfp_pae_dispose_record(ic, &retired_main);
    iwn_mfp_pae_dispose_record(ic, &retired_successor);
    if (queue_successor && systq != NULL)
        (void)task_add(systq, &sc->mfp_pae_task);
out:
    iwn_mfp_pae_callback_leave(sc);
}

int ItlIwn::
iwn_pae_mfp_txn_finish(struct ieee80211com *ic, u_int64_t txn_id)
{
    struct iwn_softc *sc;
    struct iwn_mfp_pae_txn *txn;
    struct ieee80211_pae_mfp_txn *generic;
    struct ieee80211_node *release_ni = NULL;
    IOSimpleLock *bss_lock;
    IOInterruptState irq;
    u_int8_t required_mask = 0;
    u_int ptk_flags = 0, gtk_flags = 0;
    int error = ECANCELED;

    if (ic == NULL)
        return EINVAL;
    sc = (struct iwn_softc *)ic->ic_softc;
    if (!iwn_mfp_pae_callback_enter(sc))
        return ECANCELED;
    if (txn_id == 0 || !iwn_mfp_runtime_enabled(sc) ||
        (bss_lock = ic->ic_pae_selected_bss_lock) == NULL)
        goto out;

    /* Match generic's lock order: selected-BSS fence first, then the local
     * owner.  That makes epoch replacement and context publication one
     * atomic choice. */
    irq = IOSimpleLockLockDisableInterrupt(bss_lock);
    IOSimpleLockLock(sc->sc_mfp_pae_lock);
    txn = &sc->sc_mfp_pae_txn;
    generic = &ic->ic_pae_mfp_txn;
    if (txn->active && !txn->cancelled && !txn->task_active &&
        !txn->pending && !sc->sc_mfp_pae_detaching &&
        !sc->sc_mfp_pae_stopping && txn->lifecycle_generation ==
        sc->sc_mfp_pae_lifecycle_generation &&
        txn->txn_id == txn_id && generic->active &&
        generic->id == txn_id && generic->assoc_epoch == txn->assoc_epoch &&
        generic->ni == txn->ni && generic->phase == IEEE80211_PAE_MFP_STAGE_NONE &&
        ic->ic_bss == txn->ni &&
        __atomic_load_n(&ic->ic_pae_assoc_epoch, __ATOMIC_ACQUIRE) ==
        txn->assoc_epoch) {
        if (generic->have_ptk)
            required_mask |= iwn_mfp_pae_stage_mask(IEEE80211_PAE_MFP_STAGE_PTK);
        if (generic->have_gtk)
            required_mask |= iwn_mfp_pae_stage_mask(IEEE80211_PAE_MFP_STAGE_GTK);
        if (generic->have_igtk)
            required_mask |= iwn_mfp_pae_stage_mask(IEEE80211_PAE_MFP_STAGE_IGTK);
        if ((txn->accepted_mask & required_mask) != required_mask ||
            (generic->have_ptk && txn->ptk_key.k_priv == NULL) ||
            (generic->have_gtk && txn->gtk_key.k_priv == NULL)) {
            error = EIO;
        } else {
            /* Put the software contexts into generic's value descriptors
             * before it copies them live.  This prevents a reader from ever
             * observing NODE_RXPROT/TXRXPROT with a NULL CCMP context. */
            if (generic->have_ptk) {
                ptk_flags = generic->ptk_key.k_flags;
                generic->ptk_key.k_priv = txn->ptk_key.k_priv;
                generic->ptk_key.k_flags |= IEEE80211_KEY_SWCRYPTO;
            }
            if (generic->have_gtk) {
                gtk_flags = generic->gtk_key.k_flags;
                generic->gtk_key.k_priv = txn->gtk_key.k_priv;
                generic->gtk_key.k_flags |= IEEE80211_KEY_SWCRYPTO;
            }
            error = ieee80211_pae_mfp_txn_finish_publish_locked(ic, txn_id);
            if (error == 0) {
                if (iwn_mfp_pae_software_keyset_live_locked(ic, generic,
                    txn->ni))
                    AirportItlwmPostPltiTraceRecord(ic,
                        kAirportItlwmPostPltiTraceEventIwnMfpPaeSoftwareCcmpBipPublished);
                txn->ptk_key.k_priv = NULL;
                txn->gtk_key.k_priv = NULL;
                release_ni = txn->ni;
                explicit_bzero(txn, sizeof(*txn));
            } else {
                /* Generic cancellation scrubs only values.  Keep local
                 * ownership intact on a rejected publication. */
                if (generic->have_ptk) {
                    generic->ptk_key.k_priv = NULL;
                    generic->ptk_key.k_flags = ptk_flags;
                }
                if (generic->have_gtk) {
                    generic->gtk_key.k_priv = NULL;
                    generic->gtk_key.k_flags = gtk_flags;
                }
            }
        }
    }
    IOSimpleLockUnlock(sc->sc_mfp_pae_lock);
    IOSimpleLockUnlockEnableInterrupt(bss_lock, irq);
    if (release_ni != NULL)
        ieee80211_release_node(ic, release_ni);
out:
    iwn_mfp_pae_callback_leave(sc);
    return error;
}

void ItlIwn::
iwn_mfp_pae_abort_all(struct iwn_softc *sc)
{
    struct ieee80211com *ic;
    struct iwn_mfp_pae_txn retired_main, retired_successor;

    if (sc == NULL || sc->sc_mfp_pae_lock == NULL)
        return;
    ic = &sc->sc_ic;
    explicit_bzero(&retired_main, sizeof(retired_main));
    explicit_bzero(&retired_successor, sizeof(retired_successor));
    IOSimpleLockLock(sc->sc_mfp_pae_lock);
    /* Stop is a hard generation boundary.  Unlike ordinary cancellation it
     * never promotes a successor: no worker may start during hardware stop. */
    sc->sc_mfp_pae_stopping = true;
    iwn_mfp_pae_generation_advance_locked(sc);
    (void)iwn_mfp_pae_cancel_record_locked(&sc->sc_mfp_pae_txn,
        &retired_main);
    (void)iwn_mfp_pae_cancel_record_locked(&sc->sc_mfp_pae_successor,
        &retired_successor);
    IOSimpleLockUnlock(sc->sc_mfp_pae_lock);
    iwn_mfp_pae_dispose_record(ic, &retired_main);
    iwn_mfp_pae_dispose_record(ic, &retired_successor);
}

void ItlIwn::
iwn_mfp_pae_detach_begin(struct iwn_softc *sc)
{
    struct ieee80211com *ic;

    if (sc == NULL)
        return;
    ic = &sc->sc_ic;
    /* Close first so a hook captured before unpublication cannot touch the
     * PMF leaf after detach starts.  Do not hold a PMF lock across generic
     * abort or the callback drain. */
    iwn_mfp_pae_callback_close(sc);
    if (sc->sc_mfp_pae_lock != NULL) {
        IOSimpleLockLock(sc->sc_mfp_pae_lock);
        sc->sc_mfp_pae_detaching = true;
        IOSimpleLockUnlock(sc->sc_mfp_pae_lock);
    }
    /* Generic abort still sees the installed cancel hook; admission is
     * already closed, so that callback is a harmless no-op. */
    ieee80211_pae_mfp_txn_abort(ic);
    iwn_mfp_pae_publish_hooks(sc, false);
    iwn_mfp_pae_abort_all(sc);
    /* A pre-close submit may queue a task after generic abort returns; drain
     * admitted callbacks before task_del()+barrier fences that producer.
     * A copied-but-not-entered hook observes CLOSED and never touches PMF
     * state; final ifdetach/IRQ/task fences retain the surrounding softc. */
    iwn_mfp_pae_callback_drain(sc);

    if (sc->sc_mfp_pae_task_ready && systq != NULL) {
        (void)task_del(systq, &sc->mfp_pae_task);
        /* task_del() cannot recall a callback already copied by the worker. */
        taskq_barrier(systq);
    }
    sc->sc_mfp_pae_task_ready = false;
    iwn_mfp_pae_abort_all(sc);
}

/*
 * We support CCMP hardware encryption/decryption of unicast frames only.
 * A negotiated software-PMF association intentionally bypasses that hardware
 * path for every protected CCMP frame, not just management frames: pairwise
 * data and robust management share one key and one packet-number lifetime.
 */
int ItlIwn::
iwn_set_key(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct ieee80211_key *k)
{
    struct iwn_softc *sc = (struct iwn_softc *)ic->ic_softc;
    struct iwn_ops *ops = &sc->ops;
    struct iwn_node *wn = (iwn_node *)ni;
    struct iwn_node_info node;
    uint16_t kflags;

    /* Do not expose a selected-BSS BIP slot to a legacy hardware command.
     * Legitimate BIP installation remains a local software-key carrier. */
    if (k == NULL || ieee80211_bip_key_is_slot(ic, k))
        return EINVAL;
    /* An MFP association owns its complete pairwise CCMP lifetime in
     * software.  Do not install only data traffic in firmware: robust
     * management and data share the same key/PN state. */
    if ((k->k_flags & IEEE80211_KEY_SWCRYPTO) ||
        (ni != NULL && (ni->ni_flags & IEEE80211_NODE_MFP)))
        return ieee80211_set_key(ic, ni, k);
    if ((k->k_flags & IEEE80211_KEY_GROUP) ||
        k->k_cipher != IEEE80211_CIPHER_CCMP)
        return ieee80211_set_key(ic, ni, k);

    kflags = IWN_KFLAG_CCMP | IWN_KFLAG_MAP | IWN_KFLAG_KID(k->k_id);
    if (k->k_flags & IEEE80211_KEY_GROUP)
        kflags |= IWN_KFLAG_GROUP;

    memset(&node, 0, sizeof node);
    node.id = (k->k_flags & IEEE80211_KEY_GROUP) ?
        sc->broadcast_id : wn->id;
    node.control = IWN_NODE_UPDATE;
    node.flags = IWN_FLAG_SET_KEY;
    node.kflags = htole16(kflags);
    node.kid = k->k_id;
    memcpy(node.key, k->k_key, k->k_len);
    return ops->add_node(sc, &node, 1);
}

void ItlIwn::
iwn_delete_key(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct ieee80211_key *k)
{
    struct iwn_softc *sc = (struct iwn_softc *)ic->ic_softc;
    struct iwn_ops *ops = &sc->ops;
    struct iwn_node *wn = (struct iwn_node *)ni;
    struct iwn_node_info node;

    /* Teardown callbacks receive only a value after generic unpublication. */
    if (k == NULL || ieee80211_bip_key_is_slot(ic, k))
        return;
    if ((k->k_flags & IEEE80211_KEY_SWCRYPTO) ||
        (ni != NULL && (ni->ni_flags & IEEE80211_NODE_MFP))) {
        ieee80211_delete_key(ic, ni, k);
        return;
    }
    if ((k->k_flags & IEEE80211_KEY_GROUP) ||
        k->k_cipher != IEEE80211_CIPHER_CCMP) {
        /* See comment about other ciphers above. */
        ieee80211_delete_key(ic, ni, k);
        return;
    }
    if (ic->ic_state != IEEE80211_S_RUN)
        return;    /* Nothing to do. */
    memset(&node, 0, sizeof node);
    node.id = (k->k_flags & IEEE80211_KEY_GROUP) ?
        sc->broadcast_id : wn->id;
    node.control = IWN_NODE_UPDATE;
    node.flags = IWN_FLAG_SET_KEY;
    node.kflags = htole16(IWN_KFLAG_INVALID);
    node.kid = 0xff;
    (void)ops->add_node(sc, &node, 1);
}

void ItlIwn::
iwn_update_chw(struct ieee80211com *ic)
{
    struct iwn_softc *sc = (struct iwn_softc *)ic->ic_softc;
    ItlIwn *that = container_of(sc, ItlIwn, com);
    
    if (ic->ic_state != IEEE80211_S_RUN)
        return;
    
    that->iwn_rxon_configure_ht40(ic, ic->ic_bss);
    sc->ops.update_rxon(sc);
    that->iwn_set_link_quality(sc, ic->ic_bss);
}

void ItlIwn::
iwn_updateprot(struct ieee80211com *ic)
{
    struct iwn_softc *sc = (struct iwn_softc *)ic->ic_softc;
    enum ieee80211_htprot htprot;
    
    if (ic->ic_state != IEEE80211_S_RUN)
        return;
    
    /* Update ERP protection setting. */
    if (ic->ic_flags & IEEE80211_F_USEPROT)
        sc->rxon.flags |= htole32(IWN_RXON_TGG_PROT);
    else
        sc->rxon.flags &= ~htole32(IWN_RXON_TGG_PROT);

    /* Update HT protection mode setting. */
    htprot = (enum ieee80211_htprot)((ic->ic_bss->ni_htop1 & IEEE80211_HTOP1_PROT_MASK) >>
        IEEE80211_HTOP1_PROT_SHIFT);
    sc->rxon.flags &= ~htole32(IWN_RXON_HT_PROTMODE(3));
    sc->rxon.flags |= htole32(IWN_RXON_HT_PROTMODE(htprot));

    sc->ops.update_rxon(sc);
}

void ItlIwn::
iwn_updateslot(struct ieee80211com *ic)
{
    struct iwn_softc *sc = (struct iwn_softc *)ic->ic_softc;
    
    if (ic->ic_state != IEEE80211_S_RUN)
        return;
    
    if (ic->ic_flags & IEEE80211_F_SHSLOT)
        sc->rxon.flags |= htole32(IWN_RXON_SHSLOT);
    else
        sc->rxon.flags &= ~htole32(IWN_RXON_SHSLOT);
    
    if (ic->ic_flags & IEEE80211_F_SHPREAMBLE)
        sc->rxon.flags |= htole32(IWN_RXON_SHPREAMBLE);
    else
        sc->rxon.flags &= ~htole32(IWN_RXON_SHPREAMBLE);
    
    sc->ops.update_rxon(sc);
}

void ItlIwn::
iwn_update_rxon_restore_power(struct iwn_softc *sc)
{
    struct ieee80211com *ic = &sc->sc_ic;
    struct iwn_ops *ops = &sc->ops;
    int error;
    
    DELAY(100);
    
    /* All RXONs wipe the firmware's txpower table. Restore it. */
    error = ops->set_txpower(sc, 1);
    if (error != 0)
        printf("%s: could not set TX power\n", sc->sc_dev.dv_xname);
    
    DELAY(100);
    
    /* Restore power saving level */
    if (ic->ic_flags & IEEE80211_F_PMGTON)
        error = iwn_set_pslevel(sc, 0, 3, 1);
    else
        error = iwn_set_pslevel(sc, 0, 0, 1);
    if (error != 0)
        printf("%s: could not set PS level\n", sc->sc_dev.dv_xname);
}

void ItlIwn::
iwn5000_update_rxon(struct iwn_softc *sc)
{
    ItlIwn *that = container_of(sc, ItlIwn, com);
    struct iwn_rxon_assoc rxon_assoc;
    int s, error;
    
    /* Update RXON config. */
    memset(&rxon_assoc, 0, sizeof(rxon_assoc));
    rxon_assoc.flags = sc->rxon.flags;
    rxon_assoc.filter = sc->rxon.filter;
    rxon_assoc.ofdm_mask = sc->rxon.ofdm_mask;
    rxon_assoc.cck_mask = sc->rxon.cck_mask;
    rxon_assoc.ht_single_mask = sc->rxon.ht_single_mask;
    rxon_assoc.ht_dual_mask = sc->rxon.ht_dual_mask;
    rxon_assoc.ht_triple_mask = sc->rxon.ht_triple_mask;
    rxon_assoc.rxchain = sc->rxon.rxchain;
    rxon_assoc.acquisition = sc->rxon.acquisition;
    
    s = splnet();
    
    error = that->iwn_cmd(sc, IWN_CMD_RXON_ASSOC, &rxon_assoc, sizeof(rxon_assoc), 1);
    if (error != 0)
        printf("%s: RXON_ASSOC command failed\n", sc->sc_dev.dv_xname);
    
    that->iwn_update_rxon_restore_power(sc);
    
    splx(s);
}

void ItlIwn::
iwn4965_update_rxon(struct iwn_softc *sc)
{
    ItlIwn *that = container_of(sc, ItlIwn, com);
    struct iwn4965_rxon_assoc rxon_assoc;
    int s, error;
    
    /* Update RXON config. */
    memset(&rxon_assoc, 0, sizeof(rxon_assoc));
    rxon_assoc.flags = sc->rxon.flags;
    rxon_assoc.filter = sc->rxon.filter;
    rxon_assoc.ofdm_mask = sc->rxon.ofdm_mask;
    rxon_assoc.cck_mask = sc->rxon.cck_mask;
    rxon_assoc.ht_single_mask = sc->rxon.ht_single_mask;
    rxon_assoc.ht_dual_mask = sc->rxon.ht_dual_mask;
    rxon_assoc.rxchain = sc->rxon.rxchain;
    
    s = splnet();
    
    error = that->iwn_cmd(sc, IWN_CMD_RXON_ASSOC, &rxon_assoc, sizeof(rxon_assoc), 1);
    if (error != 0)
        printf("%s: RXON_ASSOC command failed\n", sc->sc_dev.dv_xname);
    
    that->iwn_update_rxon_restore_power(sc);
    
    splx(s);
}

/*
 * This function is called by upper layer when an ADDBA request is received
 * from another STA and before the ADDBA response is sent.
 */
int ItlIwn::
iwn_ampdu_rx_start(struct ieee80211com *ic, struct ieee80211_node *ni,
    uint8_t tid)
{
    struct ieee80211_rx_ba *ba = &ni->ni_rx_ba[tid];
    struct iwn_softc *sc = (struct iwn_softc *)ic->ic_softc;
    struct iwn_ops *ops = &sc->ops;
    struct iwn_node *wn = (struct iwn_node *)ni;
    struct iwn_node_info node;

    memset(&node, 0, sizeof node);
    node.id = wn->id;
    node.control = IWN_NODE_UPDATE;
    node.flags = IWN_FLAG_SET_ADDBA;
    node.addba_tid = tid;
    node.addba_ssn = htole16(ba->ba_winstart);
    /* XXX async command, so firmware may still fail to add BA agreement */
    return ops->add_node(sc, &node, 1);
}

/*
 * This function is called by upper layer on teardown of an HT-immediate
 * Block Ack agreement (eg. uppon receipt of a DELBA frame).
 */
void ItlIwn::
iwn_ampdu_rx_stop(struct ieee80211com *ic, struct ieee80211_node *ni,
    uint8_t tid)
{
    struct iwn_softc *sc = (struct iwn_softc *)ic->ic_softc;
    struct iwn_ops *ops = &sc->ops;
    struct iwn_node *wn = (struct iwn_node *)ni;
    struct iwn_node_info node;

    memset(&node, 0, sizeof node);
    node.id = wn->id;
    node.control = IWN_NODE_UPDATE;
    node.flags = IWN_FLAG_SET_DELBA;
    node.delba_tid = tid;
    (void)ops->add_node(sc, &node, 1);
}

/*
 * This function is called by upper layer when an ADDBA response is received
 * from another STA.
 */
int ItlIwn::
iwn_ampdu_tx_start(struct ieee80211com *ic, struct ieee80211_node *ni,
    uint8_t tid)
{
    struct ieee80211_tx_ba *ba = &ni->ni_tx_ba[tid];
    struct iwn_softc *sc = (struct iwn_softc *)ic->ic_softc;
    ItlIwn *that = container_of(sc, ItlIwn, com);
    struct iwn_ops *ops = &sc->ops;
    struct iwn_node *wn = (struct iwn_node *)ni;
    struct iwn_node_info node;
    int qid = sc->first_agg_txq + tid;
    int error;

    /* Ensure we can map this TID to an aggregation queue. */
    if (tid >= IWN_NUM_AMPDU_TID || ba->ba_winsize > IWN_SCHED_WINSZ ||
        qid >= sc->ntxqs || (sc->agg_queue_mask & (1 << qid)))
        return ENOSPC;

    /* Enable TX for the specified RA/TID. */
    wn->disable_tid &= ~(1 << tid);
    memset(&node, 0, sizeof node);
    node.id = wn->id;
    node.control = IWN_NODE_UPDATE;
    node.flags = IWN_FLAG_SET_DISABLE_TID;
    node.disable_tid = htole16(wn->disable_tid);
    error = ops->add_node(sc, &node, 1);
    if (error != 0)
        return error;

    if ((error = iwn_nic_lock(sc)) != 0)
        return error;
    ops->ampdu_tx_start(sc, ni, tid, ba->ba_winstart);
    iwn_nic_unlock(sc);

    sc->agg_queue_mask |= (1 << qid);
    sc->sc_tx_ba[tid].wn = wn;
    ba->ba_bitmap = 0;

    /* DVM republishes Link Quality after the scheduler owns the RA/TID so
     * firmware receives the negotiated frame limit and TLC_RTS together. */
    error = that->iwn_set_link_quality(sc, ni);
    if (error == 0)
        return 0;

    /* net80211 treats an error as an ADDBA refusal and does not call the
     * driver's stop callback, so undo the queue transition here. */
    if (iwn_nic_lock(sc) == 0) {
        ops->ampdu_tx_stop(sc, tid, ba->ba_winstart);
        iwn_nic_unlock(sc);
    }
    sc->agg_queue_mask &= ~(1 << qid);
    sc->sc_tx_ba[tid].wn = NULL;
    ba->ba_bitmap = 0;
    wn->disable_tid |= (1 << tid);
    memset(&node, 0, sizeof node);
    node.id = wn->id;
    node.control = IWN_NODE_UPDATE;
    node.flags = IWN_FLAG_SET_DISABLE_TID;
    node.disable_tid = htole16(wn->disable_tid);
    (void)ops->add_node(sc, &node, 1);
    return error;
}

void ItlIwn::
iwn_ampdu_tx_stop(struct ieee80211com *ic, struct ieee80211_node *ni,
    uint8_t tid)
{
    struct ieee80211_tx_ba *ba = (struct ieee80211_tx_ba *)&ni->ni_tx_ba[tid];
    struct iwn_softc *sc = (struct iwn_softc *)ic->ic_softc;
    ItlIwn *that = container_of(sc, ItlIwn, com);
    struct iwn_ops *ops = &sc->ops;
    int qid = sc->first_agg_txq + tid;
    struct iwn_node *wn = (struct iwn_node *)ni;
    struct iwn_node_info node;

    if (iwn_nic_lock(sc) != 0)
        return;
    /* The backend stops the scheduler and drains the actual submitted
     * descriptors before retiring queue ownership. ba_winend is a logical
     * admission limit, not an exclusive transport completion pointer. */
    ops->ampdu_tx_stop(sc, tid, ba->ba_winstart);
    iwn_nic_unlock(sc);

    sc->agg_queue_mask &= ~(1 << qid);
    sc->sc_tx_ba[tid].wn = NULL;
    ba->ba_bitmap = 0;

    /* Disable TX for the specified RA/TID. */
    wn->disable_tid |= (1 << tid);
    memset(&node, 0, sizeof node);
    node.id = wn->id;
    node.control = IWN_NODE_UPDATE;
    node.flags = IWN_FLAG_SET_DISABLE_TID;
    node.disable_tid = htole16(wn->disable_tid);
    ops->add_node(sc, &node, 1);
    (void)that->iwn_set_link_quality(sc, ni);
}

void ItlIwn::
iwn4965_ampdu_tx_start(struct iwn_softc *sc, struct ieee80211_node *ni,
    uint8_t tid, uint16_t ssn)
{
    struct iwn_node *wn = (struct iwn_node *)ni;
    int qid = IWN4965_FIRST_AGG_TXQUEUE + tid;
    uint16_t idx = IWN_AGG_SSN_TO_TXQ_IDX(ssn);

    /* Stop TX scheduler while we're changing its configuration. */
    iwn_prph_write(sc, IWN4965_SCHED_QUEUE_STATUS(qid),
        IWN4965_TXQ_STATUS_CHGACT);

    /* Assign RA/TID translation to the queue. */
    iwn_mem_write_2(sc, sc->sched_base + IWN4965_SCHED_TRANS_TBL(qid),
        wn->id << 4 | tid);

    /* Enable chain-building mode for the queue. */
    iwn_prph_setbits(sc, IWN4965_SCHED_QCHAIN_SEL, 1 << qid);

    /* Set starting sequence number from the ADDBA request. */
    sc->txq[qid].cur = sc->txq[qid].read = idx;
    IWN_WRITE(sc, IWN_HBUS_TARG_WRPTR, qid << 8 | idx);
    iwn_prph_write(sc, IWN4965_SCHED_QUEUE_RDPTR(qid), ssn);

    /* Set scheduler window size. */
    iwn_mem_write(sc, sc->sched_base + IWN4965_SCHED_QUEUE_OFFSET(qid),
        IWN_SCHED_WINSZ);
    /* Set scheduler frame limit. */
    iwn_mem_write(sc, sc->sched_base + IWN4965_SCHED_QUEUE_OFFSET(qid) + 4,
        IWN_SCHED_LIMIT << 16);

    /* Enable interrupts for the queue. */
    iwn_prph_setbits(sc, IWN4965_SCHED_INTR_MASK, 1 << qid);

    /* Mark the queue as active. */
    iwn_prph_write(sc, IWN4965_SCHED_QUEUE_STATUS(qid),
        IWN4965_TXQ_STATUS_ACTIVE | IWN4965_TXQ_STATUS_AGGR_ENA |
        iwn_tid2fifo[tid] << 1);
}

void ItlIwn::
iwn4965_ampdu_tx_stop(struct iwn_softc *sc, uint8_t tid, uint16_t ssn)
{
    ItlIwn *that = container_of(sc, ItlIwn, com);
    int qid = IWN4965_FIRST_AGG_TXQUEUE + tid;
    uint16_t idx = IWN_AGG_SSN_TO_TXQ_IDX(ssn);
    struct iwn_tx_ring *ring = &sc->txq[qid];

    /* Stop TX scheduler while we're changing its configuration. */
    iwn_prph_write(sc, IWN4965_SCHED_QUEUE_STATUS(qid),
        IWN4965_TXQ_STATUS_CHGACT);

    /* As in Intel's transport queue-disable path, retain read/write
     * ownership until every submitted descriptor has been released. */
    that->iwn_ampdu_txq_advance(sc, ring, qid, ring->cur);

    /* Set starting sequence number from the ADDBA request. */
    sc->txq[qid].cur = sc->txq[qid].read = idx;
    IWN_WRITE(sc, IWN_HBUS_TARG_WRPTR, qid << 8 | idx);
    iwn_prph_write(sc, IWN4965_SCHED_QUEUE_RDPTR(qid), ssn);

    /* Disable interrupts for the queue. */
    iwn_prph_clrbits(sc, IWN4965_SCHED_INTR_MASK, 1 << qid);

    /* Mark the queue as inactive. */
    iwn_prph_write(sc, IWN4965_SCHED_QUEUE_STATUS(qid),
        IWN4965_TXQ_STATUS_INACTIVE | iwn_tid2fifo[tid] << 1);
}

void ItlIwn::
iwn5000_ampdu_tx_start(struct iwn_softc *sc, struct ieee80211_node *ni,
    uint8_t tid, uint16_t ssn)
{
    int qid = sc->first_agg_txq + tid;
    int idx = IWN_AGG_SSN_TO_TXQ_IDX(ssn);
    struct iwn_node *wn = (struct iwn_node *)ni;
    uint8_t frameLimit = static_cast<uint8_t>(MIN(
        ni->ni_tx_ba[tid].ba_winsize,
        static_cast<uint16_t>(IWN_AMPDU_MAX)));
    if (frameLimit == 0)
        frameLimit = IWN_AMPDU_MAX;

    /* Stop TX scheduler while we're changing its configuration. */
    iwn_prph_write(sc, IWN5000_SCHED_QUEUE_STATUS(qid),
        IWN5000_TXQ_STATUS_CHGACT);

    /* Assign RA/TID translation to the queue. */
    iwn_mem_write_2(sc, sc->sched_base + IWN5000_SCHED_TRANS_TBL(qid),
        wn->id << 4 | tid);

    /* Enable chain-building mode for the queue. */
    iwn_prph_setbits(sc, IWN5000_SCHED_QCHAIN_SEL, 1 << qid);

    /* Enable aggregation for the queue. */
    iwn_prph_setbits(sc, IWN5000_SCHED_AGGR_SEL, 1 << qid);

    /* Set starting sequence number from the ADDBA request. */
    sc->txq[qid].cur = sc->txq[qid].read = idx;
    IWN_WRITE(sc, IWN_HBUS_TARG_WRPTR, qid << 8 | idx);
    iwn_prph_write(sc, IWN5000_SCHED_QUEUE_RDPTR(qid), ssn);

    /* DVM clamps the negotiated BA window to 63 and writes the same value
     * into the SCD window-size and frame-limit fields.  A 64-entry 802.11
     * reorder window is valid on air but is not a valid firmware aggregate
     * frame-count limit.  Clear context1 when recycling this RA/TID queue. */
    iwn_mem_write(sc,
        sc->sched_base + IWN5000_SCHED_QUEUE_OFFSET(qid), 0);
    iwn_mem_write(sc, sc->sched_base + IWN5000_SCHED_QUEUE_OFFSET(qid) + 4,
        static_cast<uint32_t>(frameLimit) << 16 | frameLimit);

    /* Enable interrupts for the queue. */
    iwn_prph_setbits(sc, IWN5000_SCHED_INTR_MASK, 1 << qid);

    /* Mark the queue as active. */
    iwn_prph_write(sc, IWN5000_SCHED_QUEUE_STATUS(qid),
        IWN5000_TXQ_STATUS_ACTIVE | iwn_tid2fifo[tid]);
}

void ItlIwn::
iwn5000_ampdu_tx_stop(struct iwn_softc *sc, uint8_t tid, uint16_t ssn)
{
    ItlIwn *that = container_of(sc, ItlIwn, com);
    int qid = sc->first_agg_txq + tid;
    struct iwn_tx_ring *ring = &sc->txq[qid];
    (void)ssn;

    /* Stop TX scheduler while we're changing its configuration. */
    iwn_prph_write(sc, IWN5000_SCHED_QUEUE_STATUS(qid),
        IWN5000_TXQ_STATUS_CHGACT);

    /* Disable aggregation for the queue. */
    iwn_prph_clrbits(sc, IWN5000_SCHED_AGGR_SEL, 1 << qid);

    /* DVM queue disable clears TX status before descriptor unmapping.
     * Context configuration is a different SRAM area, owned by start. */
    iwn_mem_set_region_4(sc,
        sc->sched_base + IWN5000_SCHED_TX_STATUS_OFFSET(qid), 0, 4);

    /* A short queue need not extend to ba_winend.  Reclaim only the
     * submitted interval after deactivation. The next start, not stop,
     * assigns the successor sequence and writes the hardware pointers. */
    that->iwn_ampdu_txq_advance(sc, ring, qid, ring->cur);

    /* Disable interrupts for the queue. */
    iwn_prph_clrbits(sc, IWN5000_SCHED_INTR_MASK, 1 << qid);
}

/*
 * Query calibration tables from the initialization firmware.  We do this
 * only once at first boot.  Called from a process context.
 */
int ItlIwn::
iwn5000_query_calibration(struct iwn_softc *sc)
{
    ItlIwn *that = container_of(sc, ItlIwn, com);
    struct iwn5000_calib_config cmd;
    int error;

    memset(&cmd, 0, sizeof cmd);
    cmd.ucode.once.enable = 0xffffffff;
    cmd.ucode.once.start  = 0xffffffff;
    cmd.ucode.once.send   = 0xffffffff;
    cmd.ucode.flags       = 0xffffffff;
    error = that->iwn_cmd(sc, IWN5000_CMD_CALIB_CONFIG, &cmd, sizeof cmd, 0);
    if (error != 0)
        return error;

    uint64_t deadline;
    clock_interval_to_deadline(2, kSecondScale, &deadline);
    that->lockTsleep();
    while (!(sc->sc_flags & IWN_FLAG_CALIB_DONE)) {
        uint64_t now;
        uint64_t remaining_nsec;
        clock_get_uptime(&now);
        if (now >= deadline) {
            error = EWOULDBLOCK;
            break;
        }
        absolutetime_to_nanoseconds(deadline - now, &remaining_nsec);
        if (remaining_nsec == 0) {
            error = EWOULDBLOCK;
            break;
        }
        error = that->tsleep_nsec_locked(sc, PCATCH, "iwncal", remaining_nsec);
        if (error != 0 && !(sc->sc_flags & IWN_FLAG_CALIB_DONE))
            break;
    }
    if (sc->sc_flags & IWN_FLAG_CALIB_DONE)
        error = 0;
    that->unlockTsleep();
    return error;
}

/*
 * Send calibration results to the runtime firmware.  These results were
 * obtained on first boot from the initialization firmware.
 */
int ItlIwn::
iwn5000_send_calibration(struct iwn_softc *sc)
{
    ItlIwn *that = container_of(sc, ItlIwn, com);
    int idx, error;

    for (idx = 0; idx < 5; idx++) {
        if (sc->calibcmd[idx].buf == NULL)
            continue;    /* No results available. */
        error = that->iwn_cmd(sc, IWN_CMD_PHY_CALIB, sc->calibcmd[idx].buf,
            sc->calibcmd[idx].len, 0);
        if (error != 0) {
            XYLog("%s: could not send calibration result\n",
                sc->sc_dev.dv_xname);
            return error;
        }
    }
    return 0;
}

int ItlIwn::
iwn5000_send_wimax_coex(struct iwn_softc *sc)
{
    ItlIwn *that = container_of(sc, ItlIwn, com);
    struct iwn5000_wimax_coex wimax;

#ifdef notyet
    if (sc->hw_type == IWN_HW_REV_TYPE_6050) {
        /* Enable WiMAX coexistence for combo adapters. */
        wimax.flags =
            IWN_WIMAX_COEX_ASSOC_WA_UNMASK |
            IWN_WIMAX_COEX_UNASSOC_WA_UNMASK |
            IWN_WIMAX_COEX_STA_TABLE_VALID |
            IWN_WIMAX_COEX_ENABLE;
        memcpy(wimax.events, iwn6050_wimax_events,
            sizeof iwn6050_wimax_events);
    } else
#endif
    {
        /* Disable WiMAX coexistence. */
        wimax.flags = 0;
        memset(wimax.events, 0, sizeof wimax.events);
    }
    return that->iwn_cmd(sc, IWN5000_CMD_WIMAX_COEX, &wimax, sizeof wimax, 0);
}

int ItlIwn::
iwn5000_crystal_calib(struct iwn_softc *sc)
{
    ItlIwn *that = container_of(sc, ItlIwn, com);
    struct iwn5000_phy_calib_crystal cmd;

    memset(&cmd, 0, sizeof cmd);
    cmd.code = IWN5000_PHY_CALIB_CRYSTAL;
    cmd.ngroups = 1;
    cmd.isvalid = 1;
    cmd.cap_pin[0] = letoh32(sc->eeprom_crystal) & 0xff;
    cmd.cap_pin[1] = (letoh32(sc->eeprom_crystal) >> 16) & 0xff;
    return that->iwn_cmd(sc, IWN_CMD_PHY_CALIB, &cmd, sizeof cmd, 0);
}

int ItlIwn::
iwn6000_temp_offset_calib(struct iwn_softc *sc)
{
    ItlIwn *that = container_of(sc, ItlIwn, com);
    struct iwn6000_phy_calib_temp_offset cmd;

    memset(&cmd, 0, sizeof cmd);
    cmd.code = IWN6000_PHY_CALIB_TEMP_OFFSET;
    cmd.ngroups = 1;
    cmd.isvalid = 1;
    if (sc->eeprom_temp != 0)
        cmd.offset = htole16(sc->eeprom_temp);
    else
        cmd.offset = htole16(IWN_DEFAULT_TEMP_OFFSET);
    return that->iwn_cmd(sc, IWN_CMD_PHY_CALIB, &cmd, sizeof cmd, 0);
}

int ItlIwn::
iwn2000_temp_offset_calib(struct iwn_softc *sc)
{
    ItlIwn *that = container_of(sc, ItlIwn, com);
    struct iwn2000_phy_calib_temp_offset cmd;

    memset(&cmd, 0, sizeof cmd);
    cmd.code = IWN2000_PHY_CALIB_TEMP_OFFSET;
    cmd.ngroups = 1;
    cmd.isvalid = 1;
    if (sc->eeprom_rawtemp != 0) {
        cmd.offset_low = htole16(sc->eeprom_rawtemp);
        cmd.offset_high = htole16(sc->eeprom_temp);
    } else {
        cmd.offset_low = htole16(IWN_DEFAULT_TEMP_OFFSET);
        cmd.offset_high = htole16(IWN_DEFAULT_TEMP_OFFSET);
    }
    cmd.burnt_voltage_ref = htole16(sc->eeprom_voltage);
    return that->iwn_cmd(sc, IWN_CMD_PHY_CALIB, &cmd, sizeof cmd, 0);
}

/*
 * This function is called after the runtime firmware notifies us of its
 * readiness (called in a process context).
 */
int ItlIwn::
iwn4965_post_alive(struct iwn_softc *sc)
{
    int error, qid;

    if ((error = iwn_nic_lock(sc)) != 0)
        return error;

    /* Clear TX scheduler state in SRAM. */
    sc->sched_base = iwn_prph_read(sc, IWN_SCHED_SRAM_ADDR);
    iwn_mem_set_region_4(sc, sc->sched_base + IWN4965_SCHED_CTX_OFF, 0,
        IWN4965_SCHED_CTX_LEN / sizeof (uint32_t));

    /* Set physical address of TX scheduler rings (1KB aligned). */
    iwn_prph_write(sc, IWN4965_SCHED_DRAM_ADDR, sc->sched_dma.paddr >> 10);

    IWN_SETBITS(sc, IWN_FH_TX_CHICKEN, IWN_FH_TX_CHICKEN_SCHED_RETRY);

    /* Disable chain mode for all our 16 queues. */
    iwn_prph_write(sc, IWN4965_SCHED_QCHAIN_SEL, 0);

    for (qid = 0; qid < IWN4965_NTXQUEUES; qid++) {
        iwn_prph_write(sc, IWN4965_SCHED_QUEUE_RDPTR(qid), 0);
        IWN_WRITE(sc, IWN_HBUS_TARG_WRPTR, qid << 8 | 0);

        /* Set scheduler window size. */
        iwn_mem_write(sc, sc->sched_base +
            IWN4965_SCHED_QUEUE_OFFSET(qid), IWN_SCHED_WINSZ);
        /* Set scheduler frame limit. */
        iwn_mem_write(sc, sc->sched_base +
            IWN4965_SCHED_QUEUE_OFFSET(qid) + 4,
            IWN_SCHED_LIMIT << 16);
    }

    /* Enable interrupts for all our 16 queues. */
    iwn_prph_write(sc, IWN4965_SCHED_INTR_MASK, 0xffff);
    /* Identify TX FIFO rings (0-7). */
    iwn_prph_write(sc, IWN4965_SCHED_TXFACT, 0xff);

    /* Mark TX rings (4 EDCA + cmd + 2 HCCA) as active. */
    for (qid = 0; qid < 7; qid++) {
        static uint8_t qid2fifo[] = { 3, 2, 1, 0, 4, 5, 6 };
        iwn_prph_write(sc, IWN4965_SCHED_QUEUE_STATUS(qid),
            IWN4965_TXQ_STATUS_ACTIVE | qid2fifo[qid] << 1);
    }
    iwn_nic_unlock(sc);
    return 0;
}

/*
 * This function is called after the initialization or runtime firmware
 * notifies us of its readiness (called in a process context).
 */
int ItlIwn::
iwn5000_post_alive(struct iwn_softc *sc)
{
    ItlIwn *that = container_of(sc, ItlIwn, com);
    int error, qid;

    /* Switch to using ICT interrupt mode. */
    that->iwn5000_ict_reset(sc);

    if ((error = iwn_nic_lock(sc)) != 0)
        return error;

    /* Clear TX scheduler state in SRAM. */
    sc->sched_base = iwn_prph_read(sc, IWN_SCHED_SRAM_ADDR);
    iwn_mem_set_region_4(sc, sc->sched_base + IWN5000_SCHED_CTX_OFF, 0,
        IWN5000_SCHED_CTX_LEN / sizeof (uint32_t));

    /* Set physical address of TX scheduler rings (1KB aligned). */
    iwn_prph_write(sc, IWN5000_SCHED_DRAM_ADDR, sc->sched_dma.paddr >> 10);

    /* Disable scheduler chain extension (enabled by default in HW). */
    iwn_prph_write(sc, IWN5000_SCHED_CHAINEXT_EN, 0);

    IWN_SETBITS(sc, IWN_FH_TX_CHICKEN, IWN_FH_TX_CHICKEN_SCHED_RETRY);

    iwn_prph_write(sc, IWN5000_SCHED_QCHAIN_SEL,
        sc->command_queue == IWN_IPAN_CMD_QUEUE ?
        0 : (0xfffff & ~(1U << sc->command_queue)));
    iwn_prph_write(sc, IWN5000_SCHED_AGGR_SEL, 0);

    if (sc->command_queue == IWN_IPAN_CMD_QUEUE) {
        static const uint8_t qid2fifo[] = {
            3, 2, 1, 0, 0, 4, 2, 5, 4, 7, 5
        };
        uint32_t chainSelection = 0;

        /*
         * Match gen1 iwlwifi's two transport phases exactly.  tx_start
         * configures HCMD q9 first; alive_notify then configures each fixed
         * data queue with one indivisible
         * INACTIVE -> chain/context/pointers -> ACTIVE transition.  Batching
         * every INACTIVE write before a second ACTIVE pass leaves the visible
         * q7 status correct but does not reproduce the scheduler's internal
         * transition state.
         */
        qid = IWN_IPAN_CMD_QUEUE;
        iwn_prph_write(sc, IWN5000_SCHED_QUEUE_STATUS(qid),
            IWN5000_TXQ_STATUS_CHGACT);
        iwn_prph_clrbits(sc, IWN5000_SCHED_AGGR_SEL, 1U << qid);
        IWN_WRITE(sc, IWN_HBUS_TARG_WRPTR, qid << 8 | 0);
        iwn_prph_write(sc, IWN5000_SCHED_QUEUE_RDPTR(qid), 0);
        iwn_mem_write(sc, sc->sched_base +
            IWN5000_SCHED_QUEUE_OFFSET(qid), 0);
        iwn_mem_write(sc, sc->sched_base +
            IWN5000_SCHED_QUEUE_OFFSET(qid) + 4,
            IWN_SCHED_LIMIT << 16 | IWN_SCHED_WINSZ);
        iwn_prph_write(sc, IWN5000_SCHED_QUEUE_STATUS(qid),
            IWN5000_TXQ_STATUS_ACTIVE | qid2fifo[qid]);

        iwn_prph_write(sc, IWN5000_SCHED_INTR_MASK, 0);
        iwn_prph_write(sc, IWN5000_SCHED_TXFACT, 0xff);

        for (qid = 0; qid < 11; qid++) {
            if (qid == IWN_IPAN_CMD_QUEUE)
                continue;
            iwn_prph_write(sc, IWN5000_SCHED_QUEUE_STATUS(qid),
                IWN5000_TXQ_STATUS_CHGACT);
            chainSelection |= 1U << qid;
            iwn_prph_write(sc, IWN5000_SCHED_QCHAIN_SEL,
                chainSelection);
            iwn_prph_clrbits(sc, IWN5000_SCHED_AGGR_SEL, 1U << qid);
            IWN_WRITE(sc, IWN_HBUS_TARG_WRPTR, qid << 8 | 0);
            iwn_prph_write(sc, IWN5000_SCHED_QUEUE_RDPTR(qid), 0);
            iwn_mem_write(sc, sc->sched_base +
                IWN5000_SCHED_QUEUE_OFFSET(qid), 0);
            iwn_mem_write(sc, sc->sched_base +
                IWN5000_SCHED_QUEUE_OFFSET(qid) + 4,
                IWN_SCHED_LIMIT << 16 | IWN_SCHED_WINSZ);
            iwn_prph_write(sc, IWN5000_SCHED_QUEUE_STATUS(qid),
                IWN5000_TXQ_STATUS_ACTIVE | qid2fifo[qid]);
        }
        for (qid = 11; qid < IWN5000_NTXQUEUES; qid++) {
            IWN_WRITE(sc, IWN_HBUS_TARG_WRPTR, qid << 8 | 0);
            iwn_prph_write(sc, IWN5000_SCHED_QUEUE_RDPTR(qid), 0);
            iwn_mem_write(sc, sc->sched_base +
                IWN5000_SCHED_QUEUE_OFFSET(qid), 0);
            iwn_mem_write(sc, sc->sched_base +
                IWN5000_SCHED_QUEUE_OFFSET(qid) + 4,
                IWN_SCHED_LIMIT << 16 | IWN_SCHED_WINSZ);
        }
    } else {
        static const uint8_t qid2fifo[] = { 3, 2, 1, 0, 7, 5, 6 };
        for (qid = 0; qid < IWN5000_NTXQUEUES; qid++) {
            IWN_WRITE(sc, IWN_HBUS_TARG_WRPTR, qid << 8 | 0);
            iwn_prph_write(sc, IWN5000_SCHED_QUEUE_RDPTR(qid), 0);
            iwn_mem_write(sc, sc->sched_base +
                IWN5000_SCHED_QUEUE_OFFSET(qid), 0);
            iwn_mem_write(sc, sc->sched_base +
                IWN5000_SCHED_QUEUE_OFFSET(qid) + 4,
                IWN_SCHED_LIMIT << 16 | IWN_SCHED_WINSZ);
        }
        iwn_prph_write(sc, IWN5000_SCHED_INTR_MASK, 0xfffff);
        iwn_prph_write(sc, IWN5000_SCHED_TXFACT, 0xff);
        for (qid = 0; qid < 7; qid++) {
            iwn_prph_write(sc, IWN5000_SCHED_QUEUE_STATUS(qid),
                IWN5000_TXQ_STATUS_ACTIVE | qid2fifo[qid]);
        }
    }
    XYLog("%s: DVM command queue=%u firmware_flags=0x%08x "
          "eeprom_ipan=%u\n", sc->sc_dev.dv_xname,
          static_cast<unsigned>(sc->command_queue),
          static_cast<unsigned>(sc->tlv_feature_flags),
          static_cast<unsigned>(sc->eeprom_pan_capable));

    /* DIAGNOSTIC (passthrough TX): capture the DMA addresses and the SCD
     * DRAM-base / queue-status register read-backs while the NIC lock is
     * held, then publish after unlock. Confirms whether sched/kw buffers are
     * above 4GB and whether the SCD DRAM base write actually landed. */
    sc->dbg_scd_dram_reg = iwn_prph_read(sc, IWN5000_SCHED_DRAM_ADDR);
    sc->dbg_scd_q0 = iwn_prph_read(sc, IWN5000_SCHED_QUEUE_STATUS(0));
    sc->dbg_scd_q4 = iwn_prph_read(sc, IWN5000_SCHED_QUEUE_STATUS(4));
    iwn_nic_unlock(sc);
    /* Stash for publication from iwn_auth (early-boot os_log/serial are not
     * reliably captured; the AUTH path setProperty reaches ioreg). */
    sc->dbg_sched_paddr = sc->sched_dma.paddr;
    sc->dbg_kw_paddr = sc->kw_dma.paddr;
    sc->dbg_txq0_paddr = sc->txq[0].desc_dma.paddr;
    sc->dbg_rxq_paddr = sc->rxq.desc_dma.paddr;
    sc->dbg_scd_dram_expect = (uint32_t)(sc->sched_dma.paddr >> 10);

    /* Configure WiMAX coexistence for combo adapters. */
    error = iwn5000_send_wimax_coex(sc);
    if (error != 0) {
        XYLog("%s: could not configure WiMAX coexistence\n",
            sc->sc_dev.dv_xname);
        return error;
    }
    if (sc->hw_type != IWN_HW_REV_TYPE_5150) {
        /* Perform crystal calibration. */
        error = iwn5000_crystal_calib(sc);
        if (error != 0) {
            XYLog("%s: crystal calibration failed\n",
                sc->sc_dev.dv_xname);
            return error;
        }
    }
    if (!(sc->sc_flags & IWN_FLAG_CALIB_DONE)) {
        /* Query calibration from the initialization firmware. */
        if ((error = iwn5000_query_calibration(sc)) != 0) {
            XYLog("%s: could not query calibration\n",
                sc->sc_dev.dv_xname);
            return error;
        }
        /*
         * We have the calibration results now, reboot with the
         * runtime firmware (call ourselves recursively!)
         */
        that->iwn_hw_stop(sc);
        error = that->iwn_hw_init(sc);
    } else {
        /* Send calibration results to runtime firmware. */
        error = iwn5000_send_calibration(sc);
    }
    return error;
}

/*
 * The firmware boot code is small and is intended to be copied directly into
 * the NIC internal memory (no DMA transfer).
 */
int ItlIwn::
iwn4965_load_bootcode(struct iwn_softc *sc, const uint8_t *ucode, int size)
{
    int error, ntries;

    size /= sizeof (uint32_t);

    if ((error = iwn_nic_lock(sc)) != 0)
        return error;

    /* Copy microcode image into NIC memory. */
    iwn_prph_write_region_4(sc, IWN_BSM_SRAM_BASE,
        (const uint32_t *)ucode, size);

    iwn_prph_write(sc, IWN_BSM_WR_MEM_SRC, 0);
    iwn_prph_write(sc, IWN_BSM_WR_MEM_DST, IWN_FW_TEXT_BASE);
    iwn_prph_write(sc, IWN_BSM_WR_DWCOUNT, size);

    /* Start boot load now. */
    iwn_prph_write(sc, IWN_BSM_WR_CTRL, IWN_BSM_WR_CTRL_START);

    /* Wait for transfer to complete. */
    for (ntries = 0; ntries < 1000; ntries++) {
        if (!(iwn_prph_read(sc, IWN_BSM_WR_CTRL) &
            IWN_BSM_WR_CTRL_START))
            break;
        DELAY(10);
    }
    if (ntries == 1000) {
        XYLog("%s: could not load boot firmware\n",
            sc->sc_dev.dv_xname);
        iwn_nic_unlock(sc);
        return ETIMEDOUT;
    }

    /* Enable boot after power up. */
    iwn_prph_write(sc, IWN_BSM_WR_CTRL, IWN_BSM_WR_CTRL_START_EN);

    iwn_nic_unlock(sc);
    return 0;
}

int ItlIwn::
iwn4965_load_firmware(struct iwn_softc *sc)
{
    ItlIwn *that = container_of(sc, ItlIwn, com);
    struct iwn_fw_info *fw = &sc->fw;
    struct iwn_dma_info *dma = &sc->fw_dma;
    int error;

    /* Copy initialization sections into pre-allocated DMA-safe memory. */
    memcpy(dma->vaddr, fw->init.data, fw->init.datasz);
//    bus_dmamap_sync(sc->sc_dmat, dma->map, 0, fw->init.datasz,
//        BUS_DMASYNC_PREWRITE);
    memcpy((uint8_t *)dma->vaddr + IWN4965_FW_DATA_MAXSZ,
        fw->init.text, fw->init.textsz);
//    bus_dmamap_sync(sc->sc_dmat, dma->map, IWN4965_FW_DATA_MAXSZ,
//        fw->init.textsz, BUS_DMASYNC_PREWRITE);

    /* Tell adapter where to find initialization sections. */
    if ((error = iwn_nic_lock(sc)) != 0)
        return error;
    iwn_prph_write(sc, IWN_BSM_DRAM_DATA_ADDR, dma->paddr >> 4);
    iwn_prph_write(sc, IWN_BSM_DRAM_DATA_SIZE, fw->init.datasz);
    iwn_prph_write(sc, IWN_BSM_DRAM_TEXT_ADDR,
        (dma->paddr + IWN4965_FW_DATA_MAXSZ) >> 4);
    iwn_prph_write(sc, IWN_BSM_DRAM_TEXT_SIZE, fw->init.textsz);
    iwn_nic_unlock(sc);

    /* Load firmware boot code. */
    error = iwn4965_load_bootcode(sc, fw->boot.text, fw->boot.textsz);
    if (error != 0) {
        XYLog("%s: could not load boot firmware\n",
            sc->sc_dev.dv_xname);
        return error;
    }
    /* Now press "execute". */
    IWN_WRITE(sc, IWN_RESET, 0);

    /* Wait at most one second for first alive notification. */
    if ((error = that->tsleep_nsec(sc, PCATCH, "iwninit", SEC_TO_NSEC(1))) != 0) {
        XYLog("%s: timeout waiting for adapter to initialize\n",
            sc->sc_dev.dv_xname);
        return error;
    }

    /* Retrieve current temperature for initial TX power calibration. */
    sc->rawtemp = sc->ucode_info.temp[3].chan20MHz;
    sc->temp = iwn4965_get_temperature(sc);

    /* Copy runtime sections into pre-allocated DMA-safe memory. */
    memcpy(dma->vaddr, fw->main.data, fw->main.datasz);
//    bus_dmamap_sync(sc->sc_dmat, dma->map, 0, fw->main.datasz,
//        BUS_DMASYNC_PREWRITE);
    memcpy((uint8_t *)dma->vaddr + IWN4965_FW_DATA_MAXSZ,
        fw->main.text, fw->main.textsz);
//    bus_dmamap_sync(sc->sc_dmat, dma->map, IWN4965_FW_DATA_MAXSZ,
//        fw->main.textsz, BUS_DMASYNC_PREWRITE);

    /* Tell adapter where to find runtime sections. */
    if ((error = iwn_nic_lock(sc)) != 0)
        return error;
    iwn_prph_write(sc, IWN_BSM_DRAM_DATA_ADDR, dma->paddr >> 4);
    iwn_prph_write(sc, IWN_BSM_DRAM_DATA_SIZE, fw->main.datasz);
    iwn_prph_write(sc, IWN_BSM_DRAM_TEXT_ADDR,
        (dma->paddr + IWN4965_FW_DATA_MAXSZ) >> 4);
    iwn_prph_write(sc, IWN_BSM_DRAM_TEXT_SIZE,
        IWN_FW_UPDATED | fw->main.textsz);
    iwn_nic_unlock(sc);

    return 0;
}

int ItlIwn::
iwn5000_load_firmware_section(struct iwn_softc *sc, uint32_t dst,
    const uint8_t *section, int size)
{
    ItlIwn *that = container_of(sc, ItlIwn, com);
    struct iwn_dma_info *dma = &sc->fw_dma;
    int error;

    /* Copy firmware section into pre-allocated DMA-safe memory. */
    memcpy(dma->vaddr, section, size);
//    bus_dmamap_sync(sc->sc_dmat, dma->map, 0, size, BUS_DMASYNC_PREWRITE);

    if ((error = iwn_nic_lock(sc)) != 0)
        return error;

    IWN_WRITE(sc, IWN_FH_TX_CONFIG(IWN_SRVC_DMACHNL),
        IWN_FH_TX_CONFIG_DMA_PAUSE);

    IWN_WRITE(sc, IWN_FH_SRAM_ADDR(IWN_SRVC_DMACHNL), dst);
    IWN_WRITE(sc, IWN_FH_TFBD_CTRL0(IWN_SRVC_DMACHNL),
        IWN_LOADDR(dma->paddr));
    IWN_WRITE(sc, IWN_FH_TFBD_CTRL1(IWN_SRVC_DMACHNL),
        IWN_HIADDR(dma->paddr) << 28 | size);
    IWN_WRITE(sc, IWN_FH_TXBUF_STATUS(IWN_SRVC_DMACHNL),
        IWN_FH_TXBUF_STATUS_TBNUM(1) |
        IWN_FH_TXBUF_STATUS_TBIDX(1) |
        IWN_FH_TXBUF_STATUS_TFBD_VALID);

    /* Kick Flow Handler to start DMA transfer. */
    IWN_WRITE(sc, IWN_FH_TX_CONFIG(IWN_SRVC_DMACHNL),
        IWN_FH_TX_CONFIG_DMA_ENA | IWN_FH_TX_CONFIG_CIRQ_HOST_ENDTFD);

    iwn_nic_unlock(sc);

    /* Wait at most five seconds for FH DMA transfer to complete. */
    return that->tsleep_nsec(sc, PCATCH, "iwninit", SEC_TO_NSEC(5));
}

int ItlIwn::
iwn5000_load_firmware(struct iwn_softc *sc)
{
    struct iwn_fw_part *fw;
    int error;

    /* Load the initialization firmware on first boot only. */
    fw = (sc->sc_flags & IWN_FLAG_CALIB_DONE) ?
        &sc->fw.main : &sc->fw.init;

    error = iwn5000_load_firmware_section(sc, IWN_FW_TEXT_BASE,
        fw->text, fw->textsz);
    if (error != 0) {
        XYLog("%s: could not load firmware %s section\n",
            sc->sc_dev.dv_xname, ".text");
        return error;
    }
    error = iwn5000_load_firmware_section(sc, IWN_FW_DATA_BASE,
        fw->data, fw->datasz);
    if (error != 0) {
        XYLog("%s: could not load firmware %s section\n",
            sc->sc_dev.dv_xname, ".data");
        return error;
    }

    /* Now press "execute". */
    IWN_WRITE(sc, IWN_RESET, 0);
    return 0;
}

/*
 * Extract text and data sections from a legacy firmware image.
 */
int ItlIwn::
iwn_read_firmware_leg(struct iwn_softc *sc, struct iwn_fw_info *fw)
{
    const uint32_t *ptr;
    size_t hdrlen = 24;
    uint32_t rev;

    ptr = (const uint32_t *)fw->data;
    rev = letoh32(*ptr++);

    /* Check firmware API version. */
    if (IWN_FW_API(rev) <= 1) {
        XYLog("%s: bad firmware, need API version >=2\n",
            sc->sc_dev.dv_xname);
        return EINVAL;
    }
    if (IWN_FW_API(rev) >= 3) {
        /* Skip build number (version 2 header). */
        hdrlen += 4;
        ptr++;
    }
    if (fw->size < hdrlen) {
        XYLog("%s: firmware too short: %zu bytes\n",
            sc->sc_dev.dv_xname, fw->size);
        return EINVAL;
    }
    fw->main.textsz = letoh32(*ptr++);
    fw->main.datasz = letoh32(*ptr++);
    fw->init.textsz = letoh32(*ptr++);
    fw->init.datasz = letoh32(*ptr++);
    fw->boot.textsz = letoh32(*ptr++);

    /* Check that all firmware sections fit. */
    if (fw->size < (uint64_t)hdrlen + fw->main.textsz + fw->main.datasz +
        fw->init.textsz + fw->init.datasz + fw->boot.textsz) {
        XYLog("%s: firmware too short: %zu bytes\n",
            sc->sc_dev.dv_xname, fw->size);
        return EINVAL;
    }

    /* Get pointers to firmware sections. */
    fw->main.text = (const uint8_t *)ptr;
    fw->main.data = fw->main.text + fw->main.textsz;
    fw->init.text = fw->main.data + fw->main.datasz;
    fw->init.data = fw->init.text + fw->init.textsz;
    fw->boot.text = fw->init.data + fw->init.datasz;
    return 0;
}

/*
 * Extract text and data sections from a TLV firmware image.
 */
int ItlIwn::
iwn_read_firmware_tlv(struct iwn_softc *sc, struct iwn_fw_info *fw,
    uint16_t alt)
{
    const struct iwn_fw_tlv_hdr *hdr;
    const struct iwn_fw_tlv *tlv;
    const uint8_t *ptr, *end;
    uint64_t altmask;
    uint32_t len;

    if (fw->size < sizeof (*hdr)) {
        XYLog("%s: firmware too short: %zu bytes\n",
            sc->sc_dev.dv_xname, fw->size);
        return EINVAL;
    }
    hdr = (const struct iwn_fw_tlv_hdr *)fw->data;
    if (hdr->signature != htole32(IWN_FW_SIGNATURE)) {
        XYLog("%s: bad firmware signature 0x%08x\n",
            sc->sc_dev.dv_xname, letoh32(hdr->signature));
        return EINVAL;
    }
    /*
     * Select the closest supported alternative that is less than
     * or equal to the specified one.
     */
    altmask = letoh64(hdr->altmask);
    while (alt > 0 && (alt >= 64 || !(altmask & (1ULL << alt))))
        alt--;    /* Downgrade. */

    ptr = (const uint8_t *)(hdr + 1);
    end = (const uint8_t *)(fw->data + fw->size);

    /* Parse type-length-value fields. */
    while (ptr != end) {
        if ((size_t)(end - ptr) < sizeof (*tlv))
            return EINVAL;
        tlv = (const struct iwn_fw_tlv *)ptr;
        len = letoh32(tlv->len);

        ptr += sizeof (*tlv);
        const size_t paddedLength = ((size_t)len + 3U) & ~(size_t)3U;
        if (paddedLength > (size_t)(end - ptr)) {
            XYLog("%s: firmware too short: %zu bytes\n",
                sc->sc_dev.dv_xname, fw->size);
            return EINVAL;
        }
        /* Skip other alternatives. */
        if (tlv->alt != 0 && tlv->alt != htole16(alt))
            goto next;

        switch (letoh16(tlv->type)) {
        case IWN_FW_TLV_MAIN_TEXT:
            fw->main.text = ptr;
            fw->main.textsz = len;
            break;
        case IWN_FW_TLV_MAIN_DATA:
            fw->main.data = ptr;
            fw->main.datasz = len;
            break;
        case IWN_FW_TLV_INIT_TEXT:
            fw->init.text = ptr;
            fw->init.textsz = len;
            break;
        case IWN_FW_TLV_INIT_DATA:
            fw->init.data = ptr;
            fw->init.datasz = len;
            break;
        case IWN_FW_TLV_BOOT_TEXT:
            fw->boot.text = ptr;
            fw->boot.textsz = len;
            break;
        case IWN_FW_TLV_ENH_SENS:
            if (len !=  0) {
                XYLog("%s: TLV type %d has invalid size %u\n",
                    sc->sc_dev.dv_xname, letoh16(tlv->type),
                    len);
                goto next;
            }
            sc->sc_flags |= IWN_FLAG_ENH_SENS;
            break;
        case IWN_FW_TLV_PHY_CALIB:
            if (len != sizeof(uint32_t)) {
                XYLog("%s: TLV type %d has invalid size %u\n",
                    sc->sc_dev.dv_xname, letoh16(tlv->type),
                    len);
                goto next;
            }
            uint32_t calibration;
            memcpy(&calibration, ptr, sizeof(calibration));
            calibration = letoh32(calibration);
            if (calibration <= IWN5000_PHY_CALIB_MAX) {
                sc->reset_noise_gain = calibration;
                sc->noise_gain = calibration + 1;
            }
            break;
        case IWN_FW_TLV_FLAGS:
            if (len < sizeof(uint32_t))
                break;
            if (len % sizeof(uint32_t))
                break;
            uint32_t flags;
            memcpy(&flags, ptr, sizeof(flags));
            sc->tlv_feature_flags = letoh32(flags);
            break;
        default:
            break;
        }
 next:        /* TLV fields are 32-bit aligned. */
        ptr += paddedLength;
    }
    return 0;
}

int ItlIwn::
iwn_read_firmware(struct iwn_softc *sc)
{
    struct iwn_fw_info *fw = &sc->fw;
    int error = 0;
    OSData *fwData = NULL;
    uint expandedSize = 0;

    /* Do not replace a still-owned hardware upload buffer. */
    if (fw->data != NULL)
        return EBUSY;

    /*
     * Some PHY calibration commands are firmware-dependent; these
     * are the default values that will be overridden if
     * necessary.
     */
    sc->reset_noise_gain = IWN5000_PHY_CALIB_RESET_NOISE_GAIN;
    sc->noise_gain = IWN5000_PHY_CALIB_NOISE_GAIN;
    sc->tlv_feature_flags = 0;
    sc->sc_flags &= ~IWN_FLAG_ENH_SENS;

    memset(fw, 0, sizeof (*fw));

    /* Read the embedded compressed image, without activating hardware. */
    fwData = getFWDescByName(sc->fwname);
    if (fwData == NULL) {
        error = EINVAL;
        XYLog("%s resource load fail.\n", sc->fwname);
        return error;
    }
    if (fwData->getLength() == 0 ||
        fwData->getLength() > UINT32_MAX / 4U) {
        error = EINVAL;
        goto fail;
    }
    expandedSize = fwData->getLength() * 4U;
    fw->data = (u_char *)malloc(expandedSize, 1, 1);
    if (fw->data == NULL) {
        error = ENOMEM;
        goto fail;
    }
    if (!uncompressFirmware(fw->data, &expandedSize,
            (u_char *)fwData->getBytesNoCopy(), fwData->getLength())) {
        error = EINVAL;
        goto fail;
    }
    fw->size = expandedSize;
    OSSafeReleaseNULL(fwData);
    
    if (fw->size < sizeof (uint32_t)) {
        XYLog("%s: firmware too short: %zu bytes\n",
            sc->sc_dev.dv_xname, fw->size);
        error = EINVAL;
        goto fail;
    }

    /* Retrieve text and data sections. */
    if (*(const uint32_t *)fw->data != 0)    /* Legacy image. */
        error = iwn_read_firmware_leg(sc, fw);
    else
        error = iwn_read_firmware_tlv(sc, fw, 1);
    if (error != 0) {
        XYLog("%s: could not read firmware sections\n",
            sc->sc_dev.dv_xname);
        goto fail;
    }

    /* Make sure text and data sections fit in hardware memory. */
    if (fw->main.text == NULL || fw->main.textsz == 0 ||
        fw->main.data == NULL || fw->main.datasz == 0 ||
        fw->init.text == NULL || fw->init.textsz == 0 ||
        fw->init.data == NULL || fw->init.datasz == 0 ||
        fw->main.textsz > sc->fw_text_maxsz ||
        fw->main.datasz > sc->fw_data_maxsz ||
        fw->init.textsz > sc->fw_text_maxsz ||
        fw->init.datasz > sc->fw_data_maxsz ||
        fw->boot.textsz > IWN_FW_BOOT_TEXT_MAXSZ ||
        (fw->boot.textsz & 3) != 0) {
        XYLog("%s: firmware sections too large\n",
            sc->sc_dev.dv_xname);
        error = EINVAL;
        goto fail;
    }
  
    /* We can proceed with loading the firmware. */
    return 0;

fail:
    OSSafeReleaseNULL(fwData);
    iwn_release_firmware(sc);
    sc->tlv_feature_flags = 0;
    sc->sc_flags &= ~IWN_FLAG_ENH_SENS;
    sc->reset_noise_gain = IWN5000_PHY_CALIB_RESET_NOISE_GAIN;
    sc->noise_gain = IWN5000_PHY_CALIB_NOISE_GAIN;
    return error;
}

void ItlIwn::iwn_release_firmware(struct iwn_softc *sc)
{
    ::free(sc->fw.data);
    bzero(&sc->fw, sizeof(sc->fw));
}

int ItlIwn::iwn_prepare_firmware_capabilities(struct iwn_softc *sc)
{
    const int error = iwn_read_firmware(sc);
    if (error == 0)
        iwn_release_firmware(sc);
    return error;
}

int ItlIwn::
iwn_clock_wait(struct iwn_softc *sc)
{
    int ntries;

    /* Set "initialization complete" bit. */
    IWN_SETBITS(sc, IWN_GP_CNTRL, IWN_GP_CNTRL_INIT_DONE);

    /* Wait for clock stabilization. */
    for (ntries = 0; ntries < 2500; ntries++) {
        if (IWN_READ(sc, IWN_GP_CNTRL) & IWN_GP_CNTRL_MAC_CLOCK_READY)
            return 0;
        DELAY(10);
    }
    XYLog("%s: timeout waiting for clock stabilization\n",
        sc->sc_dev.dv_xname);
    return ETIMEDOUT;
}

int ItlIwn::
iwn_apm_init(struct iwn_softc *sc)
{
    pcireg_t reg;
    int error;

    /* Disable L0s exit timer (NMI bug workaround). */
    IWN_SETBITS(sc, IWN_GIO_CHICKEN, IWN_GIO_CHICKEN_DIS_L0S_TIMER);
    /* Don't wait for ICH L0s (ICH bug workaround). */
    IWN_SETBITS(sc, IWN_GIO_CHICKEN, IWN_GIO_CHICKEN_L1A_NO_L0S_RX);

    /* Set FH wait threshold to max (HW bug under stress workaround). */
    IWN_SETBITS(sc, IWN_DBG_HPET_MEM, 0xffff0000);

    /* Enable HAP INTA to move adapter from L1a to L0s. */
    IWN_SETBITS(sc, IWN_HW_IF_CONFIG, IWN_HW_IF_CONFIG_HAP_WAKE_L1A);

    /* Retrieve PCIe Active State Power Management (ASPM). */
    reg = pci_conf_read(sc->sc_pct, sc->sc_pcitag,
        sc->sc_cap_off + PCI_PCIE_LCSR);
    /* Workaround for HW instability in PCIe L0->L0s->L1 transition. */
    if (reg & PCI_PCIE_LCSR_ASPM_L1)    /* L1 Entry enabled. */
        IWN_SETBITS(sc, IWN_GIO, IWN_GIO_L0S_ENA);
    else
        IWN_CLRBITS(sc, IWN_GIO, IWN_GIO_L0S_ENA);

    if (sc->hw_type != IWN_HW_REV_TYPE_4965 &&
        sc->hw_type <= IWN_HW_REV_TYPE_1000)
        IWN_SETBITS(sc, IWN_ANA_PLL, IWN_ANA_PLL_INIT);

    /* Wait for clock stabilization before accessing prph. */
    if ((error = iwn_clock_wait(sc)) != 0)
        return error;

    if ((error = iwn_nic_lock(sc)) != 0)
        return error;
    if (sc->hw_type == IWN_HW_REV_TYPE_4965) {
        /* Enable DMA and BSM (Bootstrap State Machine). */
        iwn_prph_write(sc, IWN_APMG_CLK_EN,
            IWN_APMG_CLK_CTRL_DMA_CLK_RQT |
            IWN_APMG_CLK_CTRL_BSM_CLK_RQT);
    } else {
        /* Enable DMA. */
        iwn_prph_write(sc, IWN_APMG_CLK_EN,
            IWN_APMG_CLK_CTRL_DMA_CLK_RQT);
    }
    DELAY(20);
    /* Disable L1-Active. */
    iwn_prph_setbits(sc, IWN_APMG_PCI_STT, IWN_APMG_PCI_STT_L1A_DIS);
    iwn_nic_unlock(sc);

    return 0;
}

void ItlIwn::
iwn_apm_stop_master(struct iwn_softc *sc)
{
    int ntries;

    /* Stop busmaster DMA activity. */
    IWN_SETBITS(sc, IWN_RESET, IWN_RESET_STOP_MASTER);
    for (ntries = 0; ntries < 100; ntries++) {
        if (IWN_READ(sc, IWN_RESET) & IWN_RESET_MASTER_DISABLED)
            return;
        DELAY(10);
    }
    XYLog("%s: timeout waiting for master\n", sc->sc_dev.dv_xname);
}

void ItlIwn::
iwn_apm_stop(struct iwn_softc *sc)
{
    iwn_apm_stop_master(sc);

    /* Reset the entire device. */
    IWN_SETBITS(sc, IWN_RESET, IWN_RESET_SW);
    DELAY(10);
    /* Clear "initialization complete" bit. */
    IWN_CLRBITS(sc, IWN_GP_CNTRL, IWN_GP_CNTRL_INIT_DONE);
}

int ItlIwn::
iwn4965_nic_config(struct iwn_softc *sc)
{
    if (IWN_RFCFG_TYPE(sc->rfcfg) == 1) {
        /*
         * I don't believe this to be correct but this is what the
         * vendor driver is doing. Probably the bits should not be
         * shifted in IWN_RFCFG_*.
         */
        IWN_SETBITS(sc, IWN_HW_IF_CONFIG,
            IWN_RFCFG_TYPE(sc->rfcfg) |
            IWN_RFCFG_STEP(sc->rfcfg) |
            IWN_RFCFG_DASH(sc->rfcfg));
    }
    IWN_SETBITS(sc, IWN_HW_IF_CONFIG,
        IWN_HW_IF_CONFIG_RADIO_SI | IWN_HW_IF_CONFIG_MAC_SI);
    return 0;
}

int ItlIwn::
iwn5000_nic_config(struct iwn_softc *sc)
{
    uint32_t tmp;
    int error;

    if (IWN_RFCFG_TYPE(sc->rfcfg) < 3) {
        IWN_SETBITS(sc, IWN_HW_IF_CONFIG,
            IWN_RFCFG_TYPE(sc->rfcfg) |
            IWN_RFCFG_STEP(sc->rfcfg) |
            IWN_RFCFG_DASH(sc->rfcfg));
    }
    IWN_SETBITS(sc, IWN_HW_IF_CONFIG,
        IWN_HW_IF_CONFIG_RADIO_SI | IWN_HW_IF_CONFIG_MAC_SI);

    if ((error = iwn_nic_lock(sc)) != 0)
        return error;
    iwn_prph_setbits(sc, IWN_APMG_PS, IWN_APMG_PS_EARLY_PWROFF_DIS);

    if (sc->hw_type == IWN_HW_REV_TYPE_1000) {
        /*
         * Select first Switching Voltage Regulator (1.32V) to
         * solve a stability issue related to noisy DC2DC line
         * in the silicon of 1000 Series.
         */
        tmp = iwn_prph_read(sc, IWN_APMG_DIGITAL_SVR);
        tmp &= ~IWN_APMG_DIGITAL_SVR_VOLTAGE_MASK;
        tmp |= IWN_APMG_DIGITAL_SVR_VOLTAGE_1_32;
        iwn_prph_write(sc, IWN_APMG_DIGITAL_SVR, tmp);
    }
    iwn_nic_unlock(sc);

    if (sc->sc_flags & IWN_FLAG_INTERNAL_PA) {
        /* Use internal power amplifier only. */
        IWN_WRITE(sc, IWN_GP_DRIVER, IWN_GP_DRIVER_RADIO_2X2_IPA);
    }
    if ((sc->hw_type == IWN_HW_REV_TYPE_6050 ||
         sc->hw_type == IWN_HW_REV_TYPE_6005) && sc->calib_ver >= 6) {
        /* Indicate that ROM calibration version is >=6. */
        IWN_SETBITS(sc, IWN_GP_DRIVER, IWN_GP_DRIVER_CALIB_VER6);
    }
    if (sc->hw_type == IWN_HW_REV_TYPE_6005)
        IWN_SETBITS(sc, IWN_GP_DRIVER, IWN_GP_DRIVER_6050_1X2);
    if (sc->hw_type == IWN_HW_REV_TYPE_2030 ||
        sc->hw_type == IWN_HW_REV_TYPE_2000 ||
        sc->hw_type == IWN_HW_REV_TYPE_135 ||
        sc->hw_type == IWN_HW_REV_TYPE_105)
        IWN_SETBITS(sc, IWN_GP_DRIVER, IWN_GP_DRIVER_RADIO_IQ_INVERT);
    return 0;
}

/*
 * Take NIC ownership over Intel Active Management Technology (AMT).
 */
int ItlIwn::
iwn_hw_prepare(struct iwn_softc *sc)
{
    int ntries;

    /* Check if hardware is ready. */
    IWN_SETBITS(sc, IWN_HW_IF_CONFIG, IWN_HW_IF_CONFIG_NIC_READY);
    for (ntries = 0; ntries < 5; ntries++) {
        if (IWN_READ(sc, IWN_HW_IF_CONFIG) &
            IWN_HW_IF_CONFIG_NIC_READY)
            return 0;
        DELAY(10);
    }

    /* Hardware not ready, force into ready state. */
    IWN_SETBITS(sc, IWN_HW_IF_CONFIG, IWN_HW_IF_CONFIG_PREPARE);
    for (ntries = 0; ntries < 15000; ntries++) {
        if (!(IWN_READ(sc, IWN_HW_IF_CONFIG) &
            IWN_HW_IF_CONFIG_PREPARE_DONE))
            break;
        DELAY(10);
    }
    if (ntries == 15000)
        return ETIMEDOUT;

    /* Hardware should be ready now. */
    IWN_SETBITS(sc, IWN_HW_IF_CONFIG, IWN_HW_IF_CONFIG_NIC_READY);
    for (ntries = 0; ntries < 5; ntries++) {
        if (IWN_READ(sc, IWN_HW_IF_CONFIG) &
            IWN_HW_IF_CONFIG_NIC_READY)
            return 0;
        DELAY(10);
    }
    return ETIMEDOUT;
}

int ItlIwn::
iwn_hw_init(struct iwn_softc *sc)
{
    struct iwn_ops *ops = &sc->ops;
    int error, chnl, qid;

    /* Clear pending interrupts. */
    IWN_WRITE(sc, IWN_INT, 0xffffffff);

    if ((error = iwn_apm_init(sc)) != 0) {
        XYLog("%s: could not power on adapter\n",
            sc->sc_dev.dv_xname);
        return error;
    }

    /* Select VMAIN power source. */
    if ((error = iwn_nic_lock(sc)) != 0)
        return error;
    iwn_prph_clrbits(sc, IWN_APMG_PS, IWN_APMG_PS_PWR_SRC_MASK);
    iwn_nic_unlock(sc);

    /* Perform adapter-specific initialization. */
    if ((error = ops->nic_config(sc)) != 0)
        return error;

    /* Initialize RX ring. */
    if ((error = iwn_nic_lock(sc)) != 0)
        return error;
    IWN_WRITE(sc, IWN_FH_RX_CONFIG, 0);
    IWN_WRITE(sc, IWN_FH_RX_WPTR, 0);
    /* Set physical address of RX ring (256-byte aligned). */
    IWN_WRITE(sc, IWN_FH_RX_BASE, sc->rxq.desc_dma.paddr >> 8);
    /* Set physical address of RX status (16-byte aligned). */
    IWN_WRITE(sc, IWN_FH_STATUS_WPTR, sc->rxq.stat_dma.paddr >> 4);
    /* Enable RX. */
    IWN_WRITE(sc, IWN_FH_RX_CONFIG,
        IWN_FH_RX_CONFIG_ENA           |
        IWN_FH_RX_CONFIG_IGN_RXF_EMPTY |    /* HW bug workaround */
        IWN_FH_RX_CONFIG_IRQ_DST_HOST  |
        IWN_FH_RX_CONFIG_SINGLE_FRAME  |
        IWN_FH_RX_CONFIG_RB_TIMEOUT(0x11) | /* about 1/2 msec */
        IWN_FH_RX_CONFIG_NRBD(IWN_RX_RING_COUNT_LOG));
    iwn_nic_unlock(sc);
    IWN_WRITE(sc, IWN_FH_RX_WPTR, (IWN_RX_RING_COUNT - 1) & ~7);

    if ((error = iwn_nic_lock(sc)) != 0)
        return error;

    /* Initialize TX scheduler. */
    iwn_prph_write(sc, sc->sched_txfact_addr, 0);

    /* Set physical address of "keep warm" page (16-byte aligned). */
    IWN_WRITE(sc, IWN_FH_KW_ADDR, sc->kw_dma.paddr >> 4);

    /* Initialize TX rings. */
    for (qid = 0; qid < sc->ntxqs; qid++) {
        struct iwn_tx_ring *txq = &sc->txq[qid];

        /* Set physical address of TX ring (256-byte aligned). */
        IWN_WRITE(sc, IWN_FH_CBBC_QUEUE(qid),
            txq->desc_dma.paddr >> 8);
    }
    iwn_nic_unlock(sc);

    /* Enable DMA channels. */
    for (chnl = 0; chnl < sc->ndmachnls; chnl++) {
        IWN_WRITE(sc, IWN_FH_TX_CONFIG(chnl),
            IWN_FH_TX_CONFIG_DMA_ENA |
            IWN_FH_TX_CONFIG_DMA_CREDIT_ENA);
    }

    /* Clear "radio off" and "commands blocked" bits. */
    IWN_WRITE(sc, IWN_UCODE_GP1_CLR, IWN_UCODE_GP1_RFKILL);
    IWN_WRITE(sc, IWN_UCODE_GP1_CLR, IWN_UCODE_GP1_CMD_BLOCKED);

    /* Clear pending interrupts. */
    IWN_WRITE(sc, IWN_INT, 0xffffffff);
    /* Enable interrupt coalescing. */
    IWN_WRITE(sc, IWN_INT_COALESCING, 512 / 8);
    /* Enable interrupts. */
    IWN_WRITE(sc, IWN_INT_MASK, sc->int_mask);

    /* _Really_ make sure "radio off" bit is cleared! */
    IWN_WRITE(sc, IWN_UCODE_GP1_CLR, IWN_UCODE_GP1_RFKILL);
    IWN_WRITE(sc, IWN_UCODE_GP1_CLR, IWN_UCODE_GP1_RFKILL);

    /* Enable shadow registers. */
    if (sc->hw_type >= IWN_HW_REV_TYPE_6000)
        IWN_SETBITS(sc, IWN_SHADOW_REG_CTRL, 0x800fffff);

    if ((error = ops->load_firmware(sc)) != 0) {
        XYLog("%s: could not load firmware\n", sc->sc_dev.dv_xname);
        return error;
    }
    /* Wait at most one second for firmware alive notification. */
    if ((error = tsleep_nsec(sc, PCATCH, "iwninit", SEC_TO_NSEC(1))) != 0) {
        XYLog("%s: timeout waiting for adapter to initialize\n",
            sc->sc_dev.dv_xname);
        return error;
    }
    /* Do post-firmware initialization. */
    return ops->post_alive(sc);
}

void ItlIwn::
iwn_hw_stop(struct iwn_softc *sc)
{
    ItlIwn *that = container_of(sc, ItlIwn, com);
    struct ieee80211com *ic = &sc->sc_ic;
    struct ItlSaeAuthTransportEventV1 reset_event;
    struct ieee80211_wcl_scan_invalidation wcl_invalidation;
    struct ieee80211_standard_scan_invalidation standard_invalidation;
    enum iwn_scan_lease_owner invalidated_scan_owner = IWN_SCAN_LEASE_NONE;
    u_int64_t queued_initial_rejected_generation = 0;
    bool emit_reset_event = false;
    int chnl, qid, ntries;

    explicit_bzero(&reset_event, sizeof(reset_event));
    explicit_bzero(&wcl_invalidation, sizeof(wcl_invalidation));
    explicit_bzero(&standard_invalidation, sizeof(standard_invalidation));
    invalidated_scan_owner = iwn_scan_lease_begin_hardware_invalidation(sc,
        &wcl_invalidation, &standard_invalidation,
        &queued_initial_rejected_generation);
    /*
     * This helper also runs during the init-firmware calibration reboot, so
     * it uses a generation/cancel fence rather than waiting for systq here.
     * A direct frame already owned by firmware is snapshotted before reset;
     * a controller-cancelled ticket remains deliberately silent.
     */
    that->iwn_sae_engine_stop_begin(sc);
    that->iwn_sae_tx_stop_begin(sc);
    that->iwn_sae_wcl_stop_begin(sc);
    emit_reset_event = that->iwn_sae_tx_snapshot_reset(sc, &reset_event);
    that->iwn_sae_tx_cancel_all(sc);

    __atomic_store_n(&sc->sc_cmd_in_flight, 0, __ATOMIC_RELEASE);
    IWN_CLRBITS(sc, IWN_GP_CNTRL, IWN_GP_CNTRL_MAC_ACCESS_REQ);
    IWN_WRITE(sc, IWN_RESET, IWN_RESET_NEVO);

    /* Disable interrupts. */
    IWN_WRITE(sc, IWN_INT_MASK, 0);
    IWN_WRITE(sc, IWN_INT, 0xffffffff);
    IWN_WRITE(sc, IWN_FH_INT, 0xffffffff);
    sc->sc_flags &= ~IWN_FLAG_USE_ICT;

    /* Make sure we no longer hold the NIC lock. */
    iwn_nic_unlock(sc);

    /* Stop TX scheduler. */
    iwn_prph_write(sc, sc->sched_txfact_addr, 0);

    /* Stop all DMA channels. */
    if (iwn_nic_lock(sc) == 0) {
        for (chnl = 0; chnl < sc->ndmachnls; chnl++) {
            IWN_WRITE(sc, IWN_FH_TX_CONFIG(chnl), 0);
            for (ntries = 0; ntries < 200; ntries++) {
                if (IWN_READ(sc, IWN_FH_TX_STATUS) &
                    IWN_FH_TX_STATUS_IDLE(chnl))
                    break;
                DELAY(10);
            }
        }
        iwn_nic_unlock(sc);
    }

    /* Stop RX ring. */
    iwn_reset_rx_ring(sc, &sc->rxq);

    /* Reset all TX rings. */
    for (qid = 0; qid < sc->ntxqs; qid++)
        iwn_reset_tx_ring(sc, &sc->txq[qid]);

    if (iwn_nic_lock(sc) == 0) {
        iwn_prph_write(sc, IWN_APMG_CLK_DIS,
            IWN_APMG_CLK_CTRL_DMA_CLK_RQT);
        iwn_nic_unlock(sc);
    }
    DELAY(5);
    /* Power OFF adapter. */
    iwn_apm_stop(sc);

    /* Ring reset reclaimed every descriptor; forget stale terminal state. */
    that->iwn_sae_tx_purge(sc);
    sc->sc_flags &= ~(IWN_FLAG_SCANNING | IWN_FLAG_BGSCAN);
    ic->ic_flags &= ~(IEEE80211_F_BGSCAN |
                      IEEE80211_F_DISABLE_BG_AUTO_CONNECT);
    __atomic_store_n(&ic->ic_wcl_scan_active, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&ic->ic_wcl_scan_suppress_scan_done_once, 0,
                     __ATOMIC_RELEASE);
    iwn_scan_lease_retire_after_hardware_stop(sc);
    if (iwn_scan_lease_owner_is_wcl(invalidated_scan_owner) &&
        ic->ic_event_handler != NULL)
        (*ic->ic_event_handler)(ic, IEEE80211_EVT_WCL_SCAN_INVALIDATED,
                                &wcl_invalidation);
    if (invalidated_scan_owner == IWN_SCAN_LEASE_STANDARD_CONTROLLER &&
        ic->ic_event_handler != NULL)
        (*ic->ic_event_handler)(ic, IEEE80211_EVT_STANDARD_SCAN_INVALIDATED,
                                &standard_invalidation);
    if (queued_initial_rejected_generation != 0 &&
        ic->ic_event_handler != NULL) {
        struct ieee80211_wcl_scan_start_rejected rejected;
        explicit_bzero(&rejected, sizeof(rejected));
        rejected.generation = queued_initial_rejected_generation;
        (*ic->ic_event_handler)(ic, IEEE80211_EVT_WCL_SCAN_START_REJECTED,
                                &rejected);
        explicit_bzero(&rejected, sizeof(rejected));
    }
    explicit_bzero(&wcl_invalidation, sizeof(wcl_invalidation));
    explicit_bzero(&standard_invalidation, sizeof(standard_invalidation));
    if (emit_reset_event)
        that->iwn_sae_tx_emit_reset_event(sc, &reset_event);
    explicit_bzero(&reset_event, sizeof(reset_event));
}

static void iwn_configure_tx_queue_topology(struct iwn_softc *sc)
{
    const bool pan = sc->hw_type != IWN_HW_REV_TYPE_4965 &&
        sc->eeprom_pan_capable &&
        (sc->tlv_feature_flags & IWN_UCODE_TLV_FLAGS_PAN) != 0;
    sc->command_queue = pan ? IWN_IPAN_CMD_QUEUE : IWN_DEFAULT_CMD_QUEUE;
    /* PAN post_alive assigns q10 to the fixed AUX FIFO. Every STA/AP TX,
     * completion and BA-retirement consumer must share the same dynamic
     * boundary; changing only the hardware start method aliases that AUX
     * queue or makes completions decode another TID. DMA was provisioned
     * for the complete family range during attach and remains unchanged. */
    sc->first_agg_txq = sc->hw_type == IWN_HW_REV_TYPE_4965 ?
        IWN4965_FIRST_AGG_TXQUEUE :
        (pan ? IWN_IPAN_FIRST_AGG_QUEUE : IWN5000_FIRST_AGG_TXQUEUE);
}

int ItlIwn::
iwn_init(struct _ifnet *ifp)
{
    struct iwn_softc *sc = (struct iwn_softc *)ifp->if_softc;
    struct ieee80211com *ic = &sc->sc_ic;
    bool driver_reset_reconnect;
    int error;

    memset(sc->bss_node_addr, 0, sizeof(sc->bss_node_addr));
    sc->agg_queue_mask = 0;
    memset(sc->sc_tx_ba, 0, sizeof(sc->sc_tx_ba));

    if ((error = iwn_hw_prepare(sc)) != 0) {
        XYLog("%s: hardware not ready\n", sc->sc_dev.dv_xname);
        goto fail;
    }

    /* Initialize interrupt mask to default value. */
    sc->int_mask = IWN_INT_MASK_DEF;
    sc->sc_flags &= ~IWN_FLAG_USE_ICT;

    /* Check that the radio is not disabled by hardware switch. */
    if (!(IWN_READ(sc, IWN_GP_CNTRL) & IWN_GP_CNTRL_RFKILL)) {
        XYLog("%s: radio is disabled by hardware switch\n",
            sc->sc_dev.dv_xname);
        error = EPERM;    /* :-) */
        /* Re-enable interrupts. */
        IWN_WRITE(sc, IWN_INT, 0xffffffff);
        IWN_WRITE(sc, IWN_INT_MASK, sc->int_mask);
        return error;
    }

    /* Read firmware images from the filesystem. */
    if ((error = iwn_read_firmware(sc)) != 0) {
        XYLog("%s: could not read firmware\n", sc->sc_dev.dv_xname);
        goto fail;
    }
    iwn_configure_tx_queue_topology(sc);

    /* Initialize hardware and upload firmware. */
    error = iwn_hw_init(sc);
    iwn_release_firmware(sc);
    if (error != 0) {
        XYLog("%s: could not initialize hardware\n",
            sc->sc_dev.dv_xname);
        goto fail;
    }

    /* Configure adapter now that it is ready. */
    if ((error = iwn_config(sc)) != 0) {
        XYLog("%s: could not configure device\n",
            sc->sc_dev.dv_xname);
        goto fail;
    }

    ifq_clr_oactive(&ifp->if_snd);
    ifp->if_flags |= IFF_RUNNING;

    /* The reset boundary stays closed until hardware configuration is fully
     * usable.  Reopening creates a new generation for future PMF workers. */
    iwn_mfp_pae_reopen(sc);
    iwn_sae_tx_reopen(sc);
    iwn_sae_engine_reopen(sc);

    if (ic->ic_opmode != IEEE80211_M_MONITOR) {
        /* A cold power-on census must never synthesize an association.  An
         * unexpected firmware epoch is different: Tahoe broadcasts
         * WCL_DriverReset and later starts a fresh known-network join.  The
         * private active-ESS marker is that exact lower recovery owner. */
        driver_reset_reconnect =
            iwn_sae_driver_reset_recovery_pending(sc, false);
        __atomic_store_n(&ic->ic_initial_scan_census_only,
                         driver_reset_reconnect ? 0 : 1,
                         __ATOMIC_RELEASE);
        error = ieee80211_begin_scan_with_result(ifp);
        if (error != 0) {
            XYLog("%s: initial scan rejected during power-on (%d)\n",
                  sc->sc_dev.dv_xname, error);
            goto fail;
        }
        if (driver_reset_reconnect) {
            (void)iwn_sae_driver_reset_recovery_pending(sc, true);
            XYLog("iwn_sae_reconnect DRIVER_RESET_SCAN_STARTED\n");
        }
    } else
        ieee80211_new_state(ic, IEEE80211_S_RUN, -1);

    /* WCL's reopen fence is also the controller's lower-ready edge.  It must
     * follow the synchronous first state transition above: publishing it
     * earlier lets an availability consumer submit into S_INIT. */
    if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
            (IFF_UP | IFF_RUNNING) &&
        ic->ic_event_handler != NULL)
        (*ic->ic_event_handler)(ic, IEEE80211_EVT_WCL_SCAN_REOPENED, NULL);

    __atomic_store_n(&sc->init_retry_count, 0, __ATOMIC_RELEASE);

    return 0;

fail:    iwn_stop(ifp);
    return error;
}

void ItlIwn::
iwn_stop(struct _ifnet *ifp)
{
    struct iwn_softc *sc = (struct iwn_softc *)ifp->if_softc;
    struct ieee80211com *ic = &sc->sc_ic;

    timeout_del(&sc->calib_to);
    __atomic_store_n(&ic->ic_initial_scan_census_only, 0,
                     __ATOMIC_RELEASE);
    ifp->if_timer = sc->sc_tx_timer = 0;
    ifp->if_flags &= ~IFF_RUNNING;
    ifq_clr_oactive(&ifp->if_snd);

    /* Stop is an association-lifetime boundary even when the caller reaches
     * it before the usual state-machine cancellation edge.  Close local PMF
     * first: generic abort then cannot promote a reconnect successor while
     * this interrupt/timer path is powering the radio down. */
    iwn_sae_engine_stop_begin(sc);
    iwn_sae_tx_stop_begin(sc);
    iwn_sae_wcl_stop_begin(sc);
    iwn_mfp_pae_abort_all(sc);
    ieee80211_pae_mfp_txn_abort(ic);
    ieee80211_new_state(ic, IEEE80211_S_INIT, -1);

    /*
     * iwn_hw_stop() destroys the PAN RXON, stations and queues.  Retire the
     * matching software AP runtime at the same lower epoch boundary so the
     * host APSTA owner observes getAPCurrentChannel()==0 and can replay its
     * durable profile.  Keeping RUNNING here left Internet Sharing enabled
     * above a vanished BSS after firmware-fatal and TX-watchdog recovery.
     */
    iwn_reset_ap_runtime_state();

    /* Power OFF hardware. */
    iwn_hw_stop(sc);
}
