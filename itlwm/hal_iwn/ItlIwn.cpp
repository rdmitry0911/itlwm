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
#include "../../AirportItlwm/TahoeNrateContracts.hpp"
#include <ClientKit/AirportItlwmPostPltiTraceBridge.h>
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

#include <sys/_task.h>
#include <kern/clock.h>
#include <sys/pcireg.h>

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
static void iwn_wcl_initial_scan_pending_clear_locked(struct iwn_softc *);

/* Software PMF is an on-air experiment until protected MPDU transport has
 * passed on the physical IWN device.  A normal binary must remain incapable
 * of exposing it; a deliberately separate lab artifact is the sole opt-in. */
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
iwn_mfp_pae_lab_opted_in(void)
{
#if IWN_SOFTWARE_PMF_LAB_BUILD
    return true;
#else
    return false;
#endif
}

/* SAE transport is experimental until the selected-BSS state owner and the
 * association bridge exist.  Keep even its physical Algorithm-3 admission
 * in the disposable software-PMF laboratory artifact. */
static bool
iwn_sae_auth_transport_lab_opted_in(void)
{
#if IWN_SOFTWARE_PMF_LAB_BUILD
    return true;
#else
    return false;
#endif
}

/* The private WCL CIPHER_PWD ingress is kept in the same explicitly built
 * laboratory artifact as the direct SAE transport.  It does not depend on
 * PMF capability: it only stages a pre-selection password and cannot emit a
 * frame or install a key by itself. */
static bool
iwn_sae_wcl_credential_lab_opted_in(void)
{
#if IWN_SOFTWARE_PMF_LAB_BUILD
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
    if (sc->sc_sae_wcl_credential_staged &&
        iwn_sae_wcl_credential_cancelled_locked(sc,
            sc->sc_sae_wcl_credential.request_generation))
        iwn_sae_wcl_credential_clear_locked(sc);
}

/* Consume a staged CIPHER_PWD only after a driver-owned selected join has
 * rebound every public identity.  The copy is worker-local: clear the sole
 * staging slot before returning so reset, cancellation and a later request
 * cannot observe or reuse this password. */
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
        iwn_sae_wcl_credential_clear_locked(sc);
        taken = true;
    }
    IOSimpleLockUnlock(sc->sc_sae_wcl_credential_lock);
    return taken;
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
    return sc != NULL && sc->sc_sae_engine_lab_enabled &&
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
    if (sc == NULL || !iwn_sae_engine_task_admission_enter(sc))
        return;
    if (sc->sc_sae_engine_task_ready && systq != NULL)
        (void)task_add(systq, &sc->sae_engine_task);
    iwn_sae_engine_task_admission_leave(sc);
}

} // namespace

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
        credential_empty = !sc->sc_sae_wcl_credential_staged;
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
        if (!iwn_scan_lease_live_locked(sc) &&
            !sc->sc_wcl_initial_scan_pending.queued &&
            !sc->sc_sae_wcl_admission_reserved &&
            (sc->sc_flags & IWN_FLAG_SCANNING) == 0) {
            sc->sc_sae_wcl_admission_reserved = true;
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
        credential_empty = !sc->sc_sae_wcl_credential_staged;
        IOSimpleLockUnlock(sc->sc_sae_wcl_credential_lock);
        if (!engine_idle || !credential_empty) {
            IOSimpleLockLock(sc->sc_scan_lease_lock);
            sc->sc_sae_wcl_admission_reserved = false;
            IOSimpleLockUnlock(sc->sc_scan_lease_lock);
            reserved = false;
        }
    }
    IOLockUnlock(sc->sc_sae_tx_lifecycle_lock);
    iwn_sae_tx_lifecycle_leave(sc);
    return reserved;
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
    if (!iwn_sae_auth_transport_lab_opted_in())
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
    iwn_sae_tx_lifecycle_leave(sc);
}

/*
 * Tahoe WCL supplies CIPHER_PWD before it asks net80211 to select a BSS.
 * This method owns only that short-lived, one-record copy: it neither starts
 * SAE, emits Authentication traffic, enters an engine, nor carries a secret
 * through an Agent/controller callback.  The later selected-BSS owner must
 * rebind SSID/BSSID and atomically consume the record before it can use it.
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
    if (!iwn_sae_wcl_credential_lab_opted_in())
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

    if (request_generation == 0 || !iwn_sae_wcl_credential_lab_opted_in() ||
        !iwn_sae_tx_lifecycle_enter(sc, true))
        return;
    if (sc->sc_sae_wcl_credential_lock != NULL) {
        IOSimpleLockLock(sc->sc_sae_wcl_credential_lock);
        iwn_sae_wcl_credential_cancel_through_locked(sc,
            request_generation);
        IOSimpleLockUnlock(sc->sc_sae_wcl_credential_lock);
    }
    iwn_sae_tx_lifecycle_leave(sc);
}

/* A queue-overflow path has no trustworthy request generation.  Scrub only
 * the private secret slot; the policy owner separately closes its epoch. */
void ItlIwn::
purgeSaeWclCredentialStage()
{
    struct iwn_softc *sc = &com;

    if (!iwn_sae_tx_lifecycle_enter(sc, true))
        return;
    if (sc->sc_sae_wcl_credential_lock != NULL) {
        IOSimpleLockLock(sc->sc_sae_wcl_credential_lock);
        iwn_sae_wcl_credential_clear_locked(sc);
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
 * before taking the WCL leaf, then turn a staged generation into the same
 * cancellation fence used for cancellation-before-stage.  No WCL pointer or
 * password survives reset, radio stop, or a later reopen. */
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
        IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    }
    if (sc->sc_sae_wcl_credential_lock == NULL)
        return;
    IOSimpleLockLock(sc->sc_sae_wcl_credential_lock);
    if (sc->sc_sae_wcl_credential_staged)
        generation = sc->sc_sae_wcl_credential.request_generation;
    if (generation != 0)
        iwn_sae_wcl_credential_cancel_through_locked(sc, generation);
    else
        iwn_sae_wcl_credential_clear_locked(sc);
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
    /* Keep the lease until any requeue is admitted or rejected by close().
     * Otherwise detach could drain, free the task storage, and race the
     * post-callback task_add() below. */
    if (more)
        iwn_sae_tx_schedule_task(sc, false);
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
    /* This callback is admitted from generic/RX-adjacent paths.  It has
     * recorded all public cancellation identity under an interrupt-safe leaf
     * and must not take the sleeping TX lifecycle lock or WCL leaf here. */
    iwn_sae_engine_schedule_task(sc);
    explicit_bzero(&cancel, sizeof(cancel));
    iwn_sae_engine_callback_leave(sc);
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
    struct iwn_sae_engine_owner *owner;
    u_int64_t relay_generation = 0;
    int held = 0;

    explicit_bzero(&bound, sizeof(bound));
    explicit_bzero(&selected, sizeof(selected));
    explicit_bzero(&activated, sizeof(activated));
    explicit_bzero(&cancel, sizeof(cancel));
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

    selected.version = kItlSaeAuthTransportV1Version;
    selected.size = sizeof(selected);
    selected.request_generation = bound.generation;
    selected.association_epoch = bound.association_epoch;
    selected.sae_group = IEEE80211_SAE_ENGINE_GROUP19;
    selected.sae_method = IEEE80211_SAE_ENGINE_HNP_METHOD;
    /* The current scan census intentionally exposes only the bounded HnP
     * decision, not raw RSNXE bytes; zero is the sole modeled capability. */
    selected.rsnxe_capabilities = 0;
    selected.ssid_len = bound.ssid_len;
    selected.credential_source = 1u; /* private WCL CIPHER_PWD slot */
    IEEE80211_ADDR_COPY(selected.bssid, bound.bssid);
    IEEE80211_ADDR_COPY(selected.sta, bound.sta);
    memcpy(selected.ssid, bound.ssid, bound.ssid_len);
    if (!itl_sae_selected_join_event_is_well_formed(&selected))
        goto leave;

    IOSimpleLockLock(sc->sc_sae_engine_lock);
    owner = &sc->sc_sae_engine_owner;
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
     * rollback is therefore exact and one bounded deferred retry is safe. */
    IWN_SAE_ENGINE_SUBMIT_RETRY = 1,
    IWN_SAE_ENGINE_SUBMIT_FAIL = -1,
};

/* Worker-only: materialize one prepared engine frame, then send it through
 * the existing IWN gate/descriptor/doorbell path.  A submit failure is known
 * to be pre-doorbell, so the engine's rollback API is safe; this owner then
 * takes at most one bounded retry for a private-gate busy result and fails
 * closed for every ambiguous radio state. */
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
    if (generation == 0 ||
        !ieee80211_sae_wcl_request_copyout_bound_current(ic, generation,
            &bound) || !iwn_sae_engine_selected_matches_bound(&selected,
            &bound) || !iwn_sae_wcl_credential_take_bound(sc, &selected,
            &credential) ||
        !ieee80211_sae_wcl_request_copyout_bound_current(ic, generation,
            &bound) || !iwn_sae_engine_selected_matches_bound(&selected,
            &bound))
        goto out;
    if (ieee80211_sae_engine_begin_hnp(&selected, &activated,
        credential.password, credential.password_len, &engine) != 0)
        goto out;
    /* begin_hnp consumes its password synchronously; no secret remains in
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

/* This runs only on sae_engine_task after detach has drained that task.  It
 * may call generic state transitions only after it has scrubbed every local
 * SAE identity and released the engine leaf. */
static void
iwn_sae_engine_worker_retire(struct iwn_softc *sc, bool request_scan)
{
    struct iwn_sae_engine_owner *owner;
    struct ieee80211_sae_engine *engine = NULL;
    struct IwnSaeEngineCancellation cancel;
    bool suppress_scan = true;
    bool issue_scan = false;
    bool reopen_hooks = false;
    u_int32_t reopen_generation = 0;

    if (sc == NULL || sc->sc_sae_engine_lock == NULL)
        return;
    explicit_bzero(&cancel, sizeof(cancel));
    IOSimpleLockLock(sc->sc_sae_engine_lock);
    owner = &sc->sc_sae_engine_owner;
    if (owner->active) {
        cancel.active = true;
        cancel.request_generation = owner->request_generation;
        cancel.association_epoch = owner->association_epoch;
        cancel.relay_generation = owner->relay_generation;
        cancel.ticket = owner->in_flight_ticket;
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
    iwn_sae_engine_cancel_owned(sc, &cancel);
    issue_scan = request_scan && cancel.active && !suppress_scan &&
        (IC2IFP(&sc->sc_ic)->if_flags & IFF_RUNNING) != 0 &&
        (sc->sc_ic.ic_state == IEEE80211_S_AUTH ||
        sc->sc_ic.ic_state == IEEE80211_S_ASSOC);
    if (issue_scan)
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
}

} // namespace

void ItlIwn::
iwn_sae_engine_task(void *arg)
{
    struct iwn_softc *sc = (struct iwn_softc *)arg;
    struct iwn_sae_engine_owner *owner;
    struct ItlSaeAuthTransportEventV1 terminal;
    struct ItlSaeAuthPeerEventV1 peer;
    struct ItlSaePmkContinuationV1 continuation;
    u_int8_t canonical_pmkid[IEEE80211_PMKID_LEN];
    struct ieee80211_sae_engine *engine;
    enum ieee80211_sae_engine_peer_result peer_result;
    u_int64_t wcl_cancel_generation = 0;
    bool start = false;
    bool retry_submit = false;
    bool have_terminal = false;
    bool have_peer = false;
    bool cancel = false;
    bool assoc_tx_accepted = false;
    bool more = false;
    bool fail = false;
    int submit_result = IWN_SAE_ENGINE_SUBMIT_OK;

    if (sc == NULL || !iwn_sae_tx_lifecycle_enter(sc, true))
        return;
    explicit_bzero(&terminal, sizeof(terminal));
    explicit_bzero(&peer, sizeof(peer));
    explicit_bzero(&continuation, sizeof(continuation));
    explicit_bzero(canonical_pmkid, sizeof(canonical_pmkid));
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
        /* attemptAction() can reject a task solely because the private IWN
         * gate is occupied.  That is pre-doorbell and gets only this short,
         * bounded deferred retry; all other submission failures retire. */
        if (retry_submit)
            IOSleep(1);
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
                 * accepted the successful wire-sequence-2 peer Commit and
                 * prepared our Confirm; no peer bytes leave the recorder. */
                if (peer.phase == kItlSaeAuthTransportPhaseCommit &&
                    peer.wire_transaction ==
                        kItlSaeAuthTransportPeerWireTransactionCommit &&
                    peer.auth_status == IEEE80211_STATUS_SUCCESS)
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
                /* The engine generated a PMK Name itself.  Recompute it at
                 * the IWN/net80211 boundary before a single PMK byte can
                 * enter the local PAE; a malformed or mismatched result is
                 * terminal and never reaches WCL, PLTI, or an Agent route. */
                if (ieee80211_sae_engine_derive_rsn_pmkid(
                    continuation.pmk, continuation.identity.bssid,
                    continuation.identity.sta, canonical_pmkid) != 0 ||
                    timingsafe_bcmp(canonical_pmkid, continuation.pmkid,
                    sizeof(canonical_pmkid)) != 0) {
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
                        &sc->sc_ic, &continuation, canonical_pmkid)) {
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
            owner->submit_retry_count < 2) {
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
    explicit_bzero(canonical_pmkid, sizeof(canonical_pmkid));
    explicit_bzero(&continuation, sizeof(continuation));
    explicit_bzero(&peer, sizeof(peer));
    explicit_bzero(&terminal, sizeof(terminal));
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
        sc->sc_mfp_pae_task_ready && sc->sc_mfp_pae_lab_enabled &&
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

/* Capability publication is intentionally independent of SAE.  It admits
 * only the completed software PMF owner; the separately lab-gated selected-
 * BSS SAE bridge decides whether an exact pure-WCL request may use it. */
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
#define IWN_APGO_FIRMWARE_BACKEND_OPT_IN 0
#endif

#ifdef DELAY
#undef DELAY
#define DELAY IODelay
#endif

bool ItlIwn::attach(IOPCIDevice *device)
{
    /* iwn_attach() may fail after publishing an event source; detach owns it. */
    fSaeTxGate = NULL;
    pci.pa_tag = device;
    pci.workloop = getMainWorkLoop();
    if (!iwn_attach(&com, &pci)) {
        detach(device);
        releaseAll();
        return false;
    }
    return true;
}

void ItlIwn::
detach(IOPCIDevice *device)
{
    struct _ifnet *ifp = &com.sc_ic.ic_ac.ac_if;
    struct iwn_softc *sc = &com;

    if (com.sc_ic.ic_newstate_preflight == iwn_newstate_preflight)
        com.sc_ic.ic_newstate_preflight = NULL;
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
    return true;
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
    IEEE80211_ADDR_COPY(rxon->myaddr, ic->ic_myaddr);
    IEEE80211_ADDR_COPY(rxon->bssid, config->bssid);
    IEEE80211_ADDR_COPY(rxon->wlap, config->bssid);
    rxon->mode = IWN_MODE_HOSTAP;
    rxon->chan = static_cast<uint8_t>(config->channel);
    rxon->flags = htole32(IWN_RXON_TSF | IWN_RXON_CTS_TO_SELF);
    if (IEEE80211_IS_CHAN_2GHZ(chan)) {
        rxon->flags |= htole32(IWN_RXON_AUTO | IWN_RXON_24GHZ);
        if (ic->ic_flags & IEEE80211_F_USEPROT) {
            rxon->flags |= htole32(IWN_RXON_TGG_PROT);
        }
    }
    rxon->filter = htole32(IWN_FILTER_MULTICAST | IWN_FILTER_BSS |
                           IWN_FILTER_BEACON);
    rxon->cck_mask = 0x0f;
    rxon->ofdm_mask = 0xff;
    rxon->ht_single_mask = 0xff;
    rxon->ht_dual_mask = 0xff;
    rxon->ht_triple_mask = 0xff;
    rxon->rxchain = htole16(IWN_RXCHAIN_VALID(com.rxchainmask) |
                            IWN_RXCHAIN_MIMO_COUNT(com.nrxchains) |
                            IWN_RXCHAIN_IDLE_COUNT(com.nrxchains));
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

    struct iwn_rxon ap_rxon;
    int error = iwn_build_ap_rxon(&ap_rxon, config);
    if (error != 0) {
        return kIOReturnBadArgument;
    }

    error = iwn_cmd(&com, IWN_CMD_RXON, &ap_rxon, com.rxonsz, 1);
    if (error != 0) {
        return kIOReturnError;
    }
    memcpy(&com.rxon, &ap_rxon, sizeof(com.rxon));
    return kIOReturnSuccess;
}

IOReturn ItlIwn::stopAPMode()
{
    if (!supportsAPMode()) {
        return kIOReturnSuccess;
    }
    struct _ifnet *ifp = &com.sc_ic.ic_ac.ac_if;
    if ((ifp->if_flags & IFF_RUNNING) != 0) {
        iwn_stop(ifp);
    }
    return kIOReturnSuccess;
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
    __atomic_store_n(&sc->sc_sae_engine_lifecycle_generation, 1,
        __ATOMIC_RELEASE);
    sc->sc_sae_engine_next_ticket = 0;
    sc->sc_sae_engine_next_relay_generation = 0;
    sc->sc_sae_engine_task_ready = false;
    sc->sc_sae_engine_stopping = true;
    sc->sc_sae_engine_detaching = false;
    sc->sc_sae_engine_lab_enabled = false;
    sc->sc_scan_lease_lock = NULL;
    explicit_bzero(&sc->sc_scan_lease, sizeof(sc->sc_scan_lease));
    sc->sc_sae_wcl_admission_reserved = false;
    sc->sc_scan_lease_next_serial = 0;
    sc->sc_scan_lease_replay_task_ready = false;
    sc->sc_scan_lease_replay_pending = false;
    sc->sc_scan_lease_replay_nstate = IEEE80211_S_INIT;
    sc->sc_scan_lease_replay_arg = -1;
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
    sc->sc_sae_wcl_admission_reserved = false;
    sc->sc_scan_lease_next_serial = 0;
    __atomic_store_n(&sc->sc_scan_lease_replay_task_admission_state,
        IWN_SCAN_LEASE_REPLAY_TASK_ADMISSION_CLOSED, __ATOMIC_RELEASE);
    sc->sc_scan_lease_replay_task_ready = false;
    sc->sc_scan_lease_replay_pending = false;
    sc->sc_scan_lease_replay_nstate = IEEE80211_S_INIT;
    sc->sc_scan_lease_replay_arg = -1;
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
    __atomic_store_n(&sc->sc_sae_engine_lifecycle_generation, 1,
        __ATOMIC_RELEASE);
    __atomic_store_n(&sc->sc_sae_engine_task_admission_state,
        IWN_SAE_ENGINE_TASK_ADMISSION_CLOSED, __ATOMIC_RELEASE);
    sc->sc_sae_engine_next_ticket = 0;
    sc->sc_sae_engine_next_relay_generation = 0;
    sc->sc_sae_engine_task_ready = false;
    sc->sc_sae_engine_stopping = true;
    sc->sc_sae_engine_detaching = false;
    sc->sc_sae_engine_lab_enabled = iwn_sae_auth_transport_lab_opted_in() &&
        iwn_sae_wcl_credential_lab_opted_in();

    /* The normal binary will never admit this slot, but allocate and zero it
     * with the SAE lifecycle so a separately built lab artifact has one
     * bounded owner and every attach-unwind path can scrub it uniformly. */
    sc->sc_sae_wcl_credential_lock = IOSimpleLockAlloc();
    if (sc->sc_sae_wcl_credential_lock == NULL)
        XYLog("%s: SAE WCL staging unavailable\n", DEVNAME(sc));
    sc->sc_sae_wcl_credential_staged = false;
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
    sc->sc_mfp_pae_lab_enabled = iwn_mfp_pae_lab_opted_in();
    
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
        ic->ic_caps |= (IEEE80211_C_QOS | IEEE80211_C_TX_AMPDU | IEEE80211_C_AMSDU_IN_AMPDU);
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
    ieee80211_media_init(ifp);

    sc->amrr.amrr_min_success_threshold =  1;
    sc->amrr.amrr_max_success_threshold = 15;

#if NBPFILTER > 0
    iwn_radiotap_attach(sc);
#endif
    timeout_set(&sc->calib_to, iwn_calib_timeout, sc);
//    rw_init(&sc->sc_rwlock, "iwnlock");
    task_set(&sc->init_task, iwn_init_task, sc, "iwn_init_task");
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
    task_add(systq, &sc->init_task);
}

void ItlIwn::
iwn_init_task(void *arg1)
{
    struct iwn_softc *sc = (struct iwn_softc *)arg1;
    struct _ifnet *ifp = &sc->sc_ic.ic_if;
    ItlIwn *that = container_of(sc, ItlIwn, com);
    int s;

//    rw_enter_write(&sc->sc_rwlock);
    s = splnet();

    /* The interrupt action leaves a fatal firmware fault with device IRQs
     * masked.  timeout/state/hardware teardown crosses macOS work-loop
     * boundaries, so it must run here rather than in that interrupt action. */
    if (sc->sc_flags & IWN_FLAG_FATAL_RECOVERY) {
        sc->sc_flags &= ~IWN_FLAG_FATAL_RECOVERY;
        if (ifp->if_flags & IFF_RUNNING)
            that->iwn_stop(ifp);
    }

    if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) == IFF_UP)
        that->iwn_init(ifp);

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

    ring->qid = qid;
    ring->queued = 0;
    ring->cur = 0;

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

    paddr = ring->cmd_dma.paddr;
    for (i = 0; i < IWN_TX_RING_COUNT; i++) {
        struct iwn_tx_data *data = &ring->data[i];

        data->cmd_paddr = paddr;
        data->scratch_paddr = paddr + 12;
        data->tx_apple_nrate = 0;
        data->tx_apple_nrate_valid = 0;
        data->post_plti_trace_class = IWN_POST_PLTI_TRACE_TX_NONE;
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
        data->tx_apple_nrate = 0;
        data->tx_apple_nrate_valid = 0;
        data->post_plti_trace_class = IWN_POST_PLTI_TRACE_TX_NONE;
        iwn_sae_tx_data_clear(data);
    }
    /* Clear TX descriptors. */
    memset(ring->desc, 0, ring->desc_dma.size);
//    bus_dmamap_sync(sc->sc_dmat, ring->desc_dma.map, 0,
//        ring->desc_dma.size, BUS_DMASYNC_PREWRITE);
    sc->qfullmsk &= ~(1 << ring->qid);
    ring->queued = 0;
    ring->cur = 0;
}

void ItlIwn::
iwn_free_tx_ring(struct iwn_softc *sc, struct iwn_tx_ring *ring)
{
    ItlIwn *that = container_of(sc, ItlIwn, com);
    int i;

    iwn_dma_contig_free(&ring->desc_dma);
    iwn_dma_contig_free(&ring->cmd_dma);

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
        data->tx_apple_nrate = 0;
        data->tx_apple_nrate_valid = 0;
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
                       bool direct_sae_scan)
{
    u_int64_t serial;

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
        (sc->sc_sae_wcl_admission_reserved && !direct_sae_scan) ||
        (required_initial_handoff_serial != 0 && !exact_initial_pending) ||
        (initial_pending && !exact_initial_pending)) {
        IOSimpleLockUnlock(sc->sc_scan_lease_lock);
        return false;
    }
    if (direct_sae_scan)
        sc->sc_sae_wcl_admission_reserved = false;
    serial = ++sc->sc_scan_lease_next_serial;
    if (serial == 0)
        serial = ++sc->sc_scan_lease_next_serial;
    iwn_scan_lease_clear_locked(sc);
    sc->sc_scan_lease.serial = serial;
    sc->sc_scan_lease.owner = owner;
    sc->sc_scan_lease.phase = IWN_SCAN_LEASE_ARMING;
    sc->sc_scan_lease.upper_generation = upper_generation;
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
iwn_scan_lease_finish_terminal(struct iwn_softc *sc, u_int64_t serial)
{
    bool schedule_replay = false;

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
            sc->sc_scan_lease_replay_pending;
        iwn_scan_lease_clear_locked(sc);
    }
    IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    return schedule_replay;
}

static bool
iwn_scan_lease_defer_scan(struct iwn_softc *sc,
                          enum ieee80211_state nstate, int arg,
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
    that = container_of(sc, ItlIwn, com);
    if (!iwn_scan_lease_defer_scan(sc, nstate, arg, &serial, &submit_abort))
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

void ItlIwn::
iwn_scan_lease_replay_task(void *arg)
{
    struct iwn_softc *sc = (struct iwn_softc *)arg;
    struct ieee80211com *ic;
    enum ieee80211_state nstate;
    int nstate_arg;
    u_int64_t initial_generation = 0;
    u_int64_t initial_handoff_serial = 0;
    u_int32_t initial_backend_generation = 0;
    bool launch_initial = false;
    bool reject_initial = false;
    bool replay = false;

    if (sc == NULL || sc->sc_scan_lease_lock == NULL)
        return;
    ic = &sc->sc_ic;
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
        sc->sc_scan_lease_replay_pending = false;
        sc->sc_scan_lease_replay_nstate = IEEE80211_S_INIT;
        sc->sc_scan_lease_replay_arg = -1;
        replay = true;
    }
    IOSimpleLockUnlock(sc->sc_scan_lease_lock);
    if (launch_initial) {
        ItlIwn *that = container_of(sc, ItlIwn, com);
        const int error = that->iwn_scan_start(sc, IEEE80211_CHAN_2GHZ, 0,
            IWN_SCAN_LEASE_WCL_INITIAL, initial_generation,
            initial_handoff_serial,
            &initial_backend_generation, false);

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
    if (replay)
        ieee80211_new_state(ic, nstate, nstate_arg);
}

IOReturn ItlIwn::
beginWclBackgroundScan(uint64_t generation, uint32_t *outBackendGeneration)
{
    u_int32_t backend_generation = 0;

    if (outBackendGeneration == NULL || generation == 0)
        return kIOReturnBadArgument;
    *outBackendGeneration = 0;
    if (iwn_scan_start(&com, IEEE80211_CHAN_2GHZ, 1,
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
        /* A radio-reset teardown can leave a BSSID pin after its ESS and
         * security selection were deselected.  It must not prevent this
         * WCL-only, empty-ESS discovery census: the SAE selection fences
         * and non-empty-ESS check below keep directed association out of
         * this path, and the pin itself is deliberately preserved. */
        (ic->ic_flags & IEEE80211_F_BGSCAN) != 0 ||
        (ic->ic_flags & IEEE80211_F_AUTO_JOIN) == 0 ||
        ic->ic_des_esslen != 0 ||
        ieee80211_sae_wcl_request_scan_selection_held(ic) ||
        ieee80211_sae_wcl_request_scan_selection_owned(ic))
        return kIOReturnBusy;

    if (!iwn_wcl_initial_scan_queue(&com, generation, &queued))
        return kIOReturnBusy;
    if (queued)
        return kIOReturnSuccess;

    error = iwn_scan_start(&com, IEEE80211_CHAN_2GHZ, 0,
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
    struct _ifnet *ifp = &ic->ic_if;
    struct iwn_softc *sc = (struct iwn_softc *)ifp->if_softc;
    struct ieee80211_node *ni = ic->ic_bss;
    ItlIwn *that = container_of(sc, ItlIwn, com);
    u_int64_t direct_sae_scan_generation = 0;
    const bool scan_hop = nstate == IEEE80211_S_SCAN &&
        ic->ic_state == IEEE80211_S_SCAN &&
        arg == IEEE80211_NEWSTATE_ARG_SCAN_HOP;
    int error;

    /* The tagged net80211 channel hop reaches this exact callback so its
     * transient current-BSS cleanup can avoid a duplicate epoch cancellation.
     * No lower IWN or generic net80211 callback may observe the private tag. */
    arg = IEEE80211_NEWSTATE_BACKEND_ARG(nstate, arg);

    /* Most callers pass through ieee80211_new_state(), whose preflight has
     * already consumed a conflicting RUN->SCAN request before epoch change.
     * Keep the same fence for the few raw backend callers. */
    if (nstate == IEEE80211_S_SCAN && ic->ic_state == IEEE80211_S_RUN &&
        iwn_newstate_preflight(ic, nstate, arg) != 0)
        return 0;
    if (nstate == IEEE80211_S_SCAN &&
        iwn_wcl_initial_scan_pending_blocks_generic(sc))
        return 0;

    if (nstate == IEEE80211_S_SCAN) {
        AirportItlwmPostPltiTraceRecord(
            ic, kAirportItlwmPostPltiTraceEventIwnScanStateEntered);
        /* A direct request remains HOLD-only until this raw state call has
         * accepted a fresh IWN scan.  The copied generation is public and
         * lets the coalesce branch reject only that exact request. */
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
                AirportItlwmPostPltiTraceRecord(
                    ic, kAirportItlwmPostPltiTraceEventIwnScanCoalesced);
                /* Ordinary SCAN -> SCAN stays coalesced, but direct SAE must
                 * never select from the pre-existing scan census.  Do not
                 * abort it asynchronously: resume_scan() will scrub this
                 * exact generation and return a bounded NotReady retry. */
                if (direct_sae_scan_generation != 0)
                    return EAGAIN;
                return 0;
            }
        } else
            sc->sc_flags &= ~IWN_FLAG_SCANNING;
        /* Turn LED off when leaving scan state. */
        that->iwn_set_led(sc, IWN_LED_LINK, 1, 0);
    }

    if (ic->ic_state >= IEEE80211_S_ASSOC &&
        nstate <= IEEE80211_S_ASSOC) {
        /* Reset state to handle re- and disassociations. */
        iwn_clear_apple_nrate_cache(sc);
        sc->rxon.associd = 0;
        sc->rxon.filter &= ~htole32(IWN_FILTER_BSS);
        /* Do not leak PMF's no-decrypt RXON policy into the next ordinary
         * association, whose pairwise CCMP key remains firmware-owned. */
        sc->rxon.filter &= ~htole32(IWN_FILTER_NODECRYPT);
        sc->rxon.flags &= ~htole32(IWN_RXON_HT_CHANMODE_MIXED2040 |
                                   IWN_RXON_HT_CHANMODE_PURE40 | IWN_RXON_HT_HT40MINUS);
        sc->calib.state = IWN_CALIB_STATE_INIT;
        sc->agg_queue_mask = 0;
        error = that->iwn_cmd(sc, IWN_CMD_RXON, &sc->rxon, sc->rxonsz, 1);
        if (error != 0)
            XYLog("%s: RXON command failed\n",
                sc->sc_dev.dv_xname);
    }

    switch (nstate) {
    case IEEE80211_S_SCAN:
    {
        /* Make the link LED blink while we're scanning. */
        that->iwn_set_led(sc, IWN_LED_LINK, 10, 10);

        if ((sc->sc_flags & IWN_FLAG_BGSCAN) == 0) {
            ieee80211_set_link_state(ic, LINK_STATE_DOWN);
            if (scan_hop)
                ieee80211_node_cleanup_scan_hop(ic, ic->ic_bss);
            else
                ieee80211_node_cleanup(ic, ic->ic_bss);
        }
        ic->ic_state = nstate;
        if ((error = that->iwn_scan(sc, IEEE80211_CHAN_2GHZ, 0,
            direct_sae_scan_generation != 0)) != 0) {
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
            return error;
        }
        break;

    case IEEE80211_S_RUN:
        if ((error = that->iwn_run(sc)) != 0) {
            XYLog("%s: could not move to run state\n",
                sc->sc_dev.dv_xname);
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
    struct iwn_rx_data *data, struct mbuf_list *ml)
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
    } else if (len < sizeof (*wh)) {
        ic->ic_stats.is_rx_tooshort++;
        ifp->netStat->inputErrors++;
        return;
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

    if (ic->ic_state != IEEE80211_S_RUN)
        return;

    bus_dmamap_sync(sc->sc_dmat, data->map, sizeof (*desc), sizeof (*cba),
        BUS_DMASYNC_POSTREAD);

    if (!IEEE80211_ADDR_EQ(ic->ic_bss->ni_macaddr, cba->macaddr))
        return;

    ni = ic->ic_bss;

    qid = le16toh(cba->qid);
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

void ItlIwn::
iwn_ampdu_txq_advance(struct iwn_softc *sc, struct iwn_tx_ring *txq, int qid,
    int idx)
{
    struct iwn_ops *ops = &sc->ops;

    while (txq->read != idx) {
        struct iwn_tx_data *txdata = &txq->data[txq->read];
        if (txdata->m != NULL) {
            ops->reset_sched(sc, qid, txq->read);
            if (txdata->sae_active) {
                ItlIwn *that = container_of(sc, ItlIwn, com);
                that->iwn_sae_tx_report_terminal(sc, txdata, EIO);
            }
            iwn_tx_done_free_txdata(sc, txdata);
            txq->queued--;
        }
        txq->read = (txq->read + 1) % IWN_TX_RING_COUNT;
    }
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
    int tid = desc->qid - sc->first_agg_txq;
    struct iwn_tx_data *txdata = &txq->data[desc->idx];
    struct ieee80211_node *ni = txdata->ni;
    int txfail = (status != IWN_TX_STATUS_SUCCESS &&
        status != IWN_TX_STATUS_DIRECT_DONE);
    struct ieee80211_tx_ba *ba;
    uint16_t seq;

    sc->sc_tx_timer = 0;

    if (ic->ic_state != IEEE80211_S_RUN)
        return;

    if (nframes > 1) {
        int i;
        
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
            if (txdata->ni == NULL)
                continue;
            
            /* The Tx rate was the same for all subframes. */
            if (iwn_build_ht_apple_nrate(rate, rflags,
                                         &txdata->tx_apple_nrate))
                txdata->tx_apple_nrate_valid = 1;
            txdata->ampdu_txmcs = rate;
            txdata->ampdu_nframes = nframes;
        }
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

        that->iwn_tx_done(sc, desc, stat->ackfailcnt, stat->rate,
                          stat->rflags, txfail, desc->qid, letoh16(stat->len));
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
    mbuf_freem(data->m);
    data->m = NULL;
    ieee80211_release_node(ic, data->ni);
    data->ni = NULL;
    data->totlen = 0;
    data->ampdu_nframes = 0;
    data->ampdu_txmcs = 0;
    data->tx_apple_nrate = 0;
    data->tx_apple_nrate_valid = 0;
    data->post_plti_trace_class = IWN_POST_PLTI_TRACE_TX_NONE;
    iwn_sae_tx_data_clear(data);
}

void ItlIwn::
iwn_clear_oactive(struct iwn_softc *sc, struct iwn_tx_ring *ring)
{
    struct ieee80211com *ic = &sc->sc_ic;
    struct _ifnet *ifp = &ic->ic_if;

    if (ring->queued < IWN_TX_RING_LOMARK) {
        sc->qfullmsk &= ~(1 << ring->qid);
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
        if (rate != data->ni->ni_txrate) {
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

    iwn_tx_done_free_txdata(sc, data);

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
    struct iwn_tx_ring *ring = &sc->txq[4];
    struct iwn_tx_data *data;

    if ((desc->qid & 0xf) != 4)
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
    wakeupOn(&ring->desc[desc->idx]);
}

/*
 * Process an INT_FH_RX or INT_SW_RX interrupt.
 */
void ItlIwn::
iwn_notif_intr(struct iwn_softc *sc)
{
    struct mbuf_list ml = MBUF_LIST_INITIALIZER();
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
            iwn_rx_done(sc, desc, data, &ml);
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
             * Rather than go directly to scan state, try to send a
             * directed probe request first. If that fails then the
             * state machine will drop us into scanning after timing
             * out waiting for a probe response.
             */
            if (missed > ic->ic_bmissthres && !ic->ic_mgt_timer) {
                if (ic->ic_if.if_flags & IFF_DEBUG)
                    XYLog("%s: receiving no beacons from "
                        "%s; checking if this AP is still "
                        "responding to probe requests\n",
                        sc->sc_dev.dv_xname, ether_sprintf(
                        ic->ic_bss->ni_macaddr));
                IEEE80211_SEND_MGMT(ic, ic->ic_bss,
                    IEEE80211_FC0_SUBTYPE_PROBE_REQ, 0);
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
            bool initial_handoff = false;
            bool replay_scan = false;

            bus_dmamap_sync(sc->sc_dmat, data->map, sizeof (*desc),
                sizeof (*scan), BUS_DMASYNC_POSTREAD);

            if (scan->status == 1 && scan->chan <= 14 &&
                (sc->sc_flags & IWN_FLAG_HAS_5GHZ)) {
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
            sc->sc_flags &= ~(IWN_FLAG_SCANNING | IWN_FLAG_BGSCAN);
            if (initial_handoff)
                ieee80211_end_scan_controlled(ifp,
                    IEEE80211_SCAN_COMPLETION_WCL_HANDOFF);
            else if (terminal.wcl_foreground)
                ieee80211_end_scan_controlled(ifp,
                    IEEE80211_SCAN_COMPLETION_WCL_FOREGROUND);
            else
                ieee80211_end_scan(ifp);
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
            replay_scan = iwn_scan_lease_finish_terminal(sc, terminal.serial);
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
            return EINVAL;
        }
        /* BIP table identity must be routed before any live descriptor
         * field is observed.  The later hardware-IV path sees k == NULL. */
        if (ieee80211_bip_key_is_slot(ic, k) ||
            k->k_cipher != IEEE80211_CIPHER_CCMP ||
            (k->k_flags & IEEE80211_KEY_SWCRYPTO) ||
            (ni->ni_flags & IEEE80211_NODE_MFP)) {
            /* Do software encryption. */
            if ((m = ieee80211_encrypt(ic, m, k)) == NULL)
                return ENOBUFS;
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
        return ENOMEM;
    }

    data->m = m;
    data->ni = ni;
    data->ampdu_txmcs = ni->ni_txmcs; /* updated upon Tx interrupt */
    data->tx_apple_nrate = tx_apple_nrate;
    data->tx_apple_nrate_valid = tx_apple_nrate_valid ? 1 : 0;
    data->post_plti_trace_class = post_plti_trace_class;
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
            XYLog("%s: device timeout\n", sc->sc_dev.dv_xname);
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
    struct iwn_tx_ring *ring = &sc->txq[4];
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

    /* A scan lease can make the final ownership decision only here: all
     * allocation and descriptor construction is complete, while the command
     * remains invisible to firmware.  Its post hook releases the IRQ-safe
     * fence only after the real WRPTR write below. */
    if (pre_doorbell != NULL && !(*pre_doorbell)(sc, doorbell_context)) {
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

    /* Update TX scheduler. */
    ops->update_sched(sc, ring->qid, ring->cur, 0, 0);

    /* Kick command ring. */
    ring->cur = (ring->cur + 1) % IWN_TX_RING_COUNT;
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
    linkq.ampdu_max = IWN_AMPDU_MAX;
    linkq.ampdu_threshold = 3;
    linkq.ampdu_limit = htole16(4000);    /* 4ms */
    ht40Enabled = iwn_rxon_ht40_enabled(sc);
    sgiEnabled = IwnHt40Contracts::allowsSgiForEffectiveHtWidth(
        ht40Enabled,
        ieee80211_node_supports_ht_sgi20(ni),
        ieee80211_node_supports_ht_sgi40(ni));

#if 0 // RTS/CTS protection not yet tested
    if (ni->ni_flags & IEEE80211_NODE_HT &&
        sc->agg_queue_mask > 0 &&
        ic->ic_flags & IEEE80211_F_USEPROT)
        if (sc->hw_type != IWN_HW_REV_TYPE_4965 &&
            sc->hw_type != IWN_HW_REV_TYPE_5300 &&
            sc->hw_type != IWN_HW_REV_TYPE_5150 &&
            sc->hw_type != IWN_HW_REV_TYPE_5350 &&
            sc->hw_type != IWN_HW_REV_TYPE_5100)
            linkq.flags |= IWN_LINK_QUAL_FLAGS_SET_STA_TLC_RTS;
#endif
    
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
 * Limit the total dwell time to 85% of the beacon interval.
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

    /*
     * If it's non-zero, we should calculate the minimum of
     * it and the DWELL_BASE.
     *
     * XXX Yes, the math should take into account that bintval
     * is 1.024mS, not 1mS..
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
               bool direct_sae_scan)
{
    struct ieee80211com *ic;
    u_int64_t serial = 0;
    u_int32_t backend_generation = 0;
    u_int32_t old_ic_flags = 0;
    time_t old_cache_scan_ts = 0;
    bool wcl_background = owner == IWN_SCAN_LEASE_WCL_BACKGROUND;
    bool wcl_foreground = owner == IWN_SCAN_LEASE_WCL_INITIAL;
    bool wcl = iwn_scan_lease_owner_is_wcl(owner);
    bool standard = owner == IWN_SCAN_LEASE_STANDARD_CONTROLLER;
    bool tagged_controller_owner = wcl || standard;
    bool prearm_background = wcl_background || (standard && bgscan != 0);
    bool controller_foreground = wcl_foreground ||
        (standard && bgscan == 0);
    bool abort_requested = false;
    bool command_attempted = false;
    bool foreground_prepared = false;
    int error;

    if (sc == NULL || (ic = &sc->sc_ic) == NULL ||
        (tagged_controller_owner &&
         (upper_generation == 0 || out_backend_generation == NULL)))
        return EINVAL;
    if (out_backend_generation != NULL)
        *out_backend_generation = 0;
    if (prearm_background && (ic->ic_state != IEEE80211_S_RUN ||
                              ic->ic_mgt_timer != 0 ||
                              ((ic->ic_flags & IEEE80211_F_RSNON) != 0 &&
                               (ic->ic_bss == NULL ||
                                !ic->ic_bss->ni_port_valid))))
        return EBUSY;
    if (wcl_foreground &&
        (ic->ic_state != IEEE80211_S_SCAN ||
         ic->ic_opmode != IEEE80211_M_STA ||
         ic->ic_mgt_timer != 0 ||
         /* As in beginWclInitialScan(), a BSSID pin without an ESS is
          * permitted only for this WCL initial discovery scan.  Do not
          * clear it here: later association policy remains pinned while
          * the empty-ESS/SAE fences keep direct selection fail-closed. */
         (ic->ic_flags & IEEE80211_F_BGSCAN) != 0 ||
         (ic->ic_flags & IEEE80211_F_AUTO_JOIN) == 0 ||
         ic->ic_des_esslen != 0 ||
         ieee80211_sae_wcl_request_scan_selection_held(ic) ||
         ieee80211_sae_wcl_request_scan_selection_owned(ic)))
        return EBUSY;
    if (standard && bgscan == 0 && ic->ic_state != IEEE80211_S_SCAN)
        return EBUSY;
    if (!iwn_scan_lease_reserve(sc, owner, upper_generation,
                                required_initial_handoff_serial,
                                &backend_generation, &serial,
                                direct_sae_scan))
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
         bool direct_sae_scan)
{
    return iwn_scan_start(sc, flags, bgscan,
        bgscan ? IWN_SCAN_LEASE_GENERIC_BACKGROUND :
            IWN_SCAN_LEASE_GENERIC_FOREGROUND,
        0, 0, NULL, direct_sae_scan);
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
    uint8_t *buf, *frm;
    uint16_t rxchain, dwell_active, dwell_passive;
    uint8_t txant;
    struct iwn_scan_doorbell_context doorbell;
    int buflen, error, is_active;
    bool wcl_foreground_5ghz_extended_dwell = false;
    bool wcl_background_5ghz_unassociated_dwell = false;
    bool wcl_background_5ghz_directed_dwell = false;
    bool foreground_5ghz_directed_dwell = false;

    if (out_command_attempted != NULL)
        *out_command_attempted = false;
    if (out_foreground_prepared != NULL)
        *out_foreground_prepared = false;

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

    if (bgscan) {
        int bintval;

        /* Set maximum off-channel time. */
        hdr->max_out = htole32(200 * 1024);

        /* Configure scan pauses which service on-channel traffic. */
        bintval = ic->ic_bss->ni_intval ? ic->ic_bss->ni_intval : 100;
        hdr->pause_scan = htole32(((100 / bintval) << 22) |
            ((100 % bintval) * 1024));
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

    /* Only do active scanning if we're announcing a probe request for a
     * given SSID (or more, if we ever add it to the driver.) */
    is_active = 0;

    /*
     * If we're scanning for a specific SSID, add it to the command.
     */
    essid = (struct iwn_scan_essid *)(tx + 1);
    if (ic->ic_des_esslen != 0) {
        essid[0].id = IEEE80211_ELEMID_SSID;
        essid[0].len = ic->ic_des_esslen;
        memcpy(essid[0].data, ic->ic_des_essid, ic->ic_des_esslen);

        is_active = 1;
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
        is_active != 0 && (flags & IEEE80211_CHAN_5GHZ) != 0;
    /* A public ASSOCIATE can replace an undirected discovery command with a
     * foreground directed scan.  On an NVM-passive non-DFS channel, the
     * legacy 110 ms budget is only 7.6 ms above a normal 100-TU beacon
     * interval and can expire before firmware accounts the beacon and emits
     * the permitted directed probe.  Give every foreground directed 5 GHz
     * join one bounded full-beacon margin.  The channel remains passive and
     * DFS remains excluded, so this does not authorize a new transmission. */
    foreground_5ghz_directed_dwell = bgscan == 0 && is_active != 0 &&
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

        chan->chan = htole16(ieee80211_chan2ieee(ic, c));
        chan->flags = 0;
        if (ic->ic_des_esslen != 0)
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
        if (foreground_5ghz_directed_dwell &&
            (c->ic_flags & IEEE80211_CHAN_PASSIVE) != 0 &&
            (c->ic_flags & IEEE80211_CHAN_DFS) == 0)
            dwell_passive = MAX(dwell_passive, 130);
        if ((wcl_foreground_5ghz_extended_dwell ||
             wcl_background_5ghz_unassociated_dwell ||
             (wcl_background_5ghz_directed_dwell &&
              (c->ic_flags & IEEE80211_CHAN_PASSIVE) != 0)) &&
            (c->ic_flags & IEEE80211_CHAN_DFS) == 0)
            dwell_passive = MAX(dwell_passive, 130);
        if (wcl_background_5ghz_directed_dwell &&
            (c->ic_flags & (IEEE80211_CHAN_PASSIVE |
                            IEEE80211_CHAN_DFS)) == 0 &&
            dwell_passive > dwell_active)
            dwell_active = MAX(dwell_active,
                MIN((uint16_t)40, (uint16_t)(dwell_passive - 1)));

        /* Make sure they're valid */
        if (dwell_passive <= dwell_active)
            dwell_passive = dwell_active + 1;

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

    buflen = (uint8_t *)chan - buf;
    hdr->len = htole16(buflen);

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
iwn_bgscan(struct ieee80211com *ic)
{
    struct iwn_softc *sc = (struct iwn_softc *)ic->ic_softc;
    ItlIwn *that = container_of(sc, ItlIwn, com);
    int error;

    error = that->iwn_scan(sc, IEEE80211_CHAN_2GHZ, 1, false);
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
     * unicast BSS node. The BSS node is added in iwn_run() after
     * association. Adding it pre-association (together with FILTER_BSS)
     * put the firmware into associated mode with associd=0 and stalled
     * the data TX FIFO.
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
    if ((error = iwn_set_timing(sc, ni)) != 0) {
        XYLog("%s: could not set timing\n", sc->sc_dev.dv_xname);
        return error;
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
    error = iwn_cmd(sc, IWN_CMD_RXON, &sc->rxon, sc->rxonsz, 1);
    if (error != 0) {
        XYLog("%s: could not update configuration\n",
            sc->sc_dev.dv_xname);
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
        return error;
    }

    error = iwn_add_bss_node(sc, ni);
    if (error != 0) {
        XYLog("%s: could not add BSS node\n", sc->sc_dev.dv_xname);
        return error;
    }

    if ((error = iwn_init_sensitivity(sc)) != 0) {
        XYLog("%s: could not set sensitivity\n",
            sc->sc_dev.dv_xname);
        return error;
    }
    /* Start periodic calibration timer. */
    sc->calib.state = IWN_CALIB_STATE_ASSOC;
    sc->calib_cnt = 0;
    timeout_add_msec(&sc->calib_to, 500);

    ieee80211_ra_node_init(ic, &wn->rn, &wn->ni);

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
    if (deliver)
        ieee80211_pae_mfp_txn_complete(ic, txn_id, stage, error);
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
    struct iwn_ops *ops = &sc->ops;
    struct iwn_node *wn = (struct iwn_node *)ni;
    struct iwn_node_info node;
    int qid = sc->first_agg_txq + tid;
    int error;

    /* Ensure we can map this TID to an aggregation queue. */
    if (tid >= IWN_NUM_AMPDU_TID || ba->ba_winsize > IWN_SCHED_WINSZ ||
        qid > sc->ntxqs || (sc->agg_queue_mask & (1 << qid)))
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

    return 0;
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

    /* Discard all frames in the current window. */
    that->iwn_ampdu_txq_advance(sc, &sc->txq[qid], qid,
        IWN_AGG_SSN_TO_TXQ_IDX(ba->ba_winend));

    if (iwn_nic_lock(sc) != 0)
        return;
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
    int qid = IWN4965_FIRST_AGG_TXQUEUE + tid;
    uint16_t idx = IWN_AGG_SSN_TO_TXQ_IDX(ssn);

    /* Stop TX scheduler while we're changing its configuration. */
    iwn_prph_write(sc, IWN4965_SCHED_QUEUE_STATUS(qid),
        IWN4965_TXQ_STATUS_CHGACT);

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
    int qid = IWN5000_FIRST_AGG_TXQUEUE + tid;
    int idx = IWN_AGG_SSN_TO_TXQ_IDX(ssn);
    struct iwn_node *wn = (struct iwn_node *)ni;

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

    /* Set scheduler window size and frame limit. */
    iwn_mem_write(sc, sc->sched_base + IWN5000_SCHED_QUEUE_OFFSET(qid) + 4,
        IWN_SCHED_LIMIT << 16 | IWN_SCHED_WINSZ);

    /* Enable interrupts for the queue. */
    iwn_prph_setbits(sc, IWN5000_SCHED_INTR_MASK, 1 << qid);

    /* Mark the queue as active. */
    iwn_prph_write(sc, IWN5000_SCHED_QUEUE_STATUS(qid),
        IWN5000_TXQ_STATUS_ACTIVE | iwn_tid2fifo[tid]);
}

void ItlIwn::
iwn5000_ampdu_tx_stop(struct iwn_softc *sc, uint8_t tid, uint16_t ssn)
{
    int qid = IWN5000_FIRST_AGG_TXQUEUE + tid;
    int idx = IWN_AGG_SSN_TO_TXQ_IDX(ssn);

    /* Stop TX scheduler while we're changing its configuration. */
    iwn_prph_write(sc, IWN5000_SCHED_QUEUE_STATUS(qid),
        IWN5000_TXQ_STATUS_CHGACT);

    /* Disable aggregation for the queue. */
    iwn_prph_clrbits(sc, IWN5000_SCHED_AGGR_SEL, 1 << qid);

    /* Set starting sequence number from the ADDBA request. */
    sc->txq[qid].cur = sc->txq[qid].read = idx;
    IWN_WRITE(sc, IWN_HBUS_TARG_WRPTR, qid << 8 | idx);
    iwn_prph_write(sc, IWN5000_SCHED_QUEUE_RDPTR(qid), ssn);

    /* Disable interrupts for the queue. */
    iwn_prph_clrbits(sc, IWN5000_SCHED_INTR_MASK, 1 << qid);

    /* Mark the queue as inactive. */
    iwn_prph_write(sc, IWN5000_SCHED_QUEUE_STATUS(qid),
        IWN5000_TXQ_STATUS_INACTIVE | iwn_tid2fifo[tid]);
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

    /* Enable chain mode for all queues, except command queue. */
    iwn_prph_write(sc, IWN5000_SCHED_QCHAIN_SEL, 0xfffef);
    iwn_prph_write(sc, IWN5000_SCHED_AGGR_SEL, 0);

    for (qid = 0; qid < IWN5000_NTXQUEUES; qid++) {
        iwn_prph_write(sc, IWN5000_SCHED_QUEUE_RDPTR(qid), 0);
        IWN_WRITE(sc, IWN_HBUS_TARG_WRPTR, qid << 8 | 0);

        iwn_mem_write(sc, sc->sched_base +
            IWN5000_SCHED_QUEUE_OFFSET(qid), 0);
        /* Set scheduler window size and frame limit. */
        iwn_mem_write(sc, sc->sched_base +
            IWN5000_SCHED_QUEUE_OFFSET(qid) + 4,
            IWN_SCHED_LIMIT << 16 | IWN_SCHED_WINSZ);
    }

    /* Enable interrupts for all our 20 queues. */
    iwn_prph_write(sc, IWN5000_SCHED_INTR_MASK, 0xfffff);
    /* Identify TX FIFO rings (0-7). */
    iwn_prph_write(sc, IWN5000_SCHED_TXFACT, 0xff);

    /* Mark TX rings (4 EDCA + cmd + 2 HCCA) as active. */
    for (qid = 0; qid < 7; qid++) {
        static uint8_t qid2fifo[] = { 3, 2, 1, 0, 7, 5, 6 };
        iwn_prph_write(sc, IWN5000_SCHED_QUEUE_STATUS(qid),
            IWN5000_TXQ_STATUS_ACTIVE | qid2fifo[qid]);
    }

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
    if (fw->size < hdrlen + fw->main.textsz + fw->main.datasz +
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
    while (alt > 0 && !(altmask & (1ULL << alt)))
        alt--;    /* Downgrade. */

    ptr = (const uint8_t *)(hdr + 1);
    end = (const uint8_t *)(fw->data + fw->size);

    /* Parse type-length-value fields. */
    while (ptr + sizeof (*tlv) <= end) {
        tlv = (const struct iwn_fw_tlv *)ptr;
        len = letoh32(tlv->len);

        ptr += sizeof (*tlv);
        if (ptr + len > end) {
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
            if (letoh32(*ptr) <= IWN5000_PHY_CALIB_MAX) {
                sc->reset_noise_gain = letoh32(*ptr);
                sc->noise_gain = letoh32(*ptr) + 1;
            }
            break;
        case IWN_FW_TLV_FLAGS:
            if (len < sizeof(uint32_t))
                break;
            if (len % sizeof(uint32_t))
                break;
            sc->tlv_feature_flags = letoh32(*ptr);
            break;
        default:
            break;
        }
 next:        /* TLV fields are 32-bit aligned. */
        ptr += (len + 3) & ~3;
    }
    return 0;
}

int ItlIwn::
iwn_read_firmware(struct iwn_softc *sc)
{
    struct iwn_fw_info *fw = &sc->fw;
    int error = 0;
    OSData *fwData = NULL;

    /*
     * Some PHY calibration commands are firmware-dependent; these
     * are the default values that will be overridden if
     * necessary.
     */
    sc->reset_noise_gain = IWN5000_PHY_CALIB_RESET_NOISE_GAIN;
    sc->noise_gain = IWN5000_PHY_CALIB_NOISE_GAIN;

    memset(fw, 0, sizeof (*fw));

    /* Read firmware image from filesystem. */
//    if ((error = loadfirmware(sc->fwname, &fw->data, &fw->size)) != 0) {
//        XYLog("%s: could not read firmware %s (error %d)\n",
//            sc->sc_dev.dv_xname, sc->fwname, error);
//        return error;
//    }
    fwData = getFWDescByName(sc->fwname);
    if (fwData == NULL) {
        error = EINVAL;
        XYLog("%s resource load fail.\n", sc->fwname);
        return error;
    }
    fw->size = fwData->getLength() * 4;
    fw->data = (u_char *)malloc(fw->size, 1, 1);
    uncompressFirmware((u_char *)fw->data, (uint *)&fw->size, (u_char *)fwData->getBytesNoCopy(), fwData->getLength());
    OSSafeReleaseNULL(fwData);
    
    if (fw->size < sizeof (uint32_t)) {
        XYLog("%s: firmware too short: %zu bytes\n",
            sc->sc_dev.dv_xname, fw->size);
        ::free(fw->data);
        return EINVAL;
    }

    /* Retrieve text and data sections. */
    if (*(const uint32_t *)fw->data != 0)    /* Legacy image. */
        error = iwn_read_firmware_leg(sc, fw);
    else
        error = iwn_read_firmware_tlv(sc, fw, 1);
    if (error != 0) {
        XYLog("%s: could not read firmware sections\n",
            sc->sc_dev.dv_xname);
        ::free(fw->data);
        return error;
    }

    /* Make sure text and data sections fit in hardware memory. */
    if (fw->main.textsz > sc->fw_text_maxsz ||
        fw->main.datasz > sc->fw_data_maxsz ||
        fw->init.textsz > sc->fw_text_maxsz ||
        fw->init.datasz > sc->fw_data_maxsz ||
        fw->boot.textsz > IWN_FW_BOOT_TEXT_MAXSZ ||
        (fw->boot.textsz & 3) != 0) {
        XYLog("%s: firmware sections too large\n",
            sc->sc_dev.dv_xname);
        ::free(fw->data);
        return EINVAL;
    }
  
    /* We can proceed with loading the firmware. */
    return 0;
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

int ItlIwn::
iwn_init(struct _ifnet *ifp)
{
    struct iwn_softc *sc = (struct iwn_softc *)ifp->if_softc;
    struct ieee80211com *ic = &sc->sc_ic;
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

    /* Initialize hardware and upload firmware. */
    error = iwn_hw_init(sc);
    ::free(sc->fw.data);
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

    if (ic->ic_opmode != IEEE80211_M_MONITOR)
        ieee80211_begin_scan(ifp);
    else
        ieee80211_new_state(ic, IEEE80211_S_RUN, -1);

    /* WCL's reopen fence is also the controller's lower-ready edge.  It must
     * follow the synchronous first state transition above: publishing it
     * earlier lets an availability consumer submit into S_INIT. */
    if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
            (IFF_UP | IFF_RUNNING) &&
        ic->ic_event_handler != NULL)
        (*ic->ic_event_handler)(ic, IEEE80211_EVT_WCL_SCAN_REOPENED, NULL);

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

    /* Power OFF hardware. */
    iwn_hw_stop(sc);
}
