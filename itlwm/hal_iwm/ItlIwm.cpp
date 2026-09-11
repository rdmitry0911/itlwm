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

#include "ItlIwm.hpp"
#include <linux/iwx_diag_log.h>

#if __IO80211_TARGET >= __MAC_26_0
extern "C" void airportItlwmRequestAPTxDequeue(
    IOEthernetController *controller);
#endif

#define super ItlHalService
OSDefineMetaClassAndStructors(ItlIwm, ItlHalService)

static bool
iwm_sae_wcl_credential_runtime_opted_in(void)
{
#if ITL_SAE_DRIVER_CRYPTO_AVAILABLE
    return true;
#else
    return false;
#endif
}

namespace {

static bool iwm_sae_engine_queue_terminal(struct iwm_softc *,
    const struct ItlSaeAuthTransportEventV1 *);
static bool iwm_sae_engine_callback_enter(struct iwm_softc *);
static void iwm_sae_engine_callback_leave(struct iwm_softc *);

struct IwmSaeTxGateArgs {
    struct ItlSaeAuthTxRequestV1 request;
    IOReturn rc;
};

static void
iwm_sae_tx_make_terminal_event(const struct ItlSaeAuthTxRequestV1 *request,
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
iwm_sae_tx_make_terminal_event_from_data(const struct iwm_tx_data *data,
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

static bool
iwm_sae_tx_lifecycle_enter(struct iwm_softc *sc, bool allow_closed)
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
iwm_sae_tx_lifecycle_leave(struct iwm_softc *sc)
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
iwm_sae_tx_lifecycle_close(struct iwm_softc *sc, bool detaching)
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
iwm_sae_tx_lifecycle_is_open(struct iwm_softc *sc)
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
iwm_sae_tx_lifecycle_drain(struct iwm_softc *sc)
{
    if (sc == NULL || sc->sc_sae_tx_lifecycle_lock == NULL)
        return;
    IOLockLock(sc->sc_sae_tx_lifecycle_lock);
    while (sc->sc_sae_tx_lifecycle_active != 0)
        IOLockSleep(sc->sc_sae_tx_lifecycle_lock, sc, THREAD_UNINT);
    IOLockUnlock(sc->sc_sae_tx_lifecycle_lock);
}

static void
iwm_sae_tx_generation_advance_locked(struct iwm_softc *sc)
{
    if (++sc->sc_sae_tx_generation == 0)
        ++sc->sc_sae_tx_generation;
}

#define IWM_SAE_ENGINE_TICKET_DIRECT_BIT \
    ((uint64_t)kItlSaeAuthTransportV1DriverTicketBit)
#define IWM_SAE_ENGINE_TICKET_COUNTER_MASK \
    (~IWM_SAE_ENGINE_TICKET_DIRECT_BIT)

static bool
iwm_sae_tx_ticket_is_direct(uint64_t ticket)
{
    return (ticket & IWM_SAE_ENGINE_TICKET_DIRECT_BIT) != 0;
}

static bool
iwm_sae_tx_ticket_cancelled_locked(const struct iwm_softc *sc,
    uint64_t ticket)
{
    if (sc == NULL || ticket == 0)
        return true;
    if (iwm_sae_tx_ticket_is_direct(ticket))
        return (ticket & IWM_SAE_ENGINE_TICKET_COUNTER_MASK) <=
            sc->sc_sae_tx_direct_cancel_through;
    return ticket <= sc->sc_sae_tx_cancel_through;
}

static void
iwm_sae_tx_cancel_ticket_locked(struct iwm_softc *sc, uint64_t ticket)
{
    uint64_t counter;

    if (sc == NULL || ticket == 0)
        return;
    if (iwm_sae_tx_ticket_is_direct(ticket)) {
        counter = ticket & IWM_SAE_ENGINE_TICKET_COUNTER_MASK;
        if (counter > sc->sc_sae_tx_direct_cancel_through)
            sc->sc_sae_tx_direct_cancel_through = counter;
    } else if (ticket > sc->sc_sae_tx_cancel_through) {
        sc->sc_sae_tx_cancel_through = ticket;
    }
}

static bool
iwm_sae_tx_request_is_live(struct iwm_softc *sc, uint64_t ticket)
{
    bool live = false;

    if (sc == NULL || ticket == 0 ||
        sc->sc_sae_tx_lifecycle_lock == NULL || sc->sc_sae_tx_lock == NULL)
        return false;
    IOLockLock(sc->sc_sae_tx_lifecycle_lock);
    if (!sc->sc_sae_tx_lifecycle_closed && !sc->sc_sae_tx_detaching) {
        IOSimpleLockLock(sc->sc_sae_tx_lock);
        live = !sc->sc_sae_tx_stopping && sc->sc_sae_tx_active &&
            sc->sc_sae_tx_active_ticket == ticket &&
            sc->sc_sae_tx_active_generation == sc->sc_sae_tx_generation &&
            !iwm_sae_tx_ticket_cancelled_locked(sc, ticket);
        IOSimpleLockUnlock(sc->sc_sae_tx_lock);
    }
    IOLockUnlock(sc->sc_sae_tx_lifecycle_lock);
    return live;
}

static void
iwm_sae_tx_schedule_task(struct iwm_softc *sc, bool allow_closed)
{
    if (sc == NULL || sc->sc_sae_tx_lifecycle_lock == NULL || systq == NULL)
        return;
    IOLockLock(sc->sc_sae_tx_lifecycle_lock);
    if ((!sc->sc_sae_tx_lifecycle_closed || allow_closed) &&
        !sc->sc_sae_tx_detaching && sc->sc_sae_tx_task_ready)
        (void)task_add(systq, &sc->sae_tx_task);
    IOLockUnlock(sc->sc_sae_tx_lifecycle_lock);
}

} // namespace

IOReturn ItlIwm::
submitSaeAuthFrame(const struct ItlSaeAuthTxRequestV1 *request)
{
    struct iwm_softc *sc = &com;
    IwmSaeTxGateArgs args;
    IOCommandGate *gate = NULL;
    IOReturn rc = kIOReturnNotReady;

    if (!itl_sae_auth_transport_request_is_well_formed(request))
        return kIOReturnBadArgument;
    if (!iwm_sae_tx_lifecycle_enter(sc, false))
        return kIOReturnNotReady;

    IOLockLock(sc->sc_sae_tx_lifecycle_lock);
    if (!sc->sc_sae_tx_lifecycle_closed && !sc->sc_sae_tx_detaching &&
        sc->sc_sae_tx_task_ready && fSaeTxGate != NULL &&
        sc->sc_sae_tx_lock != NULL) {
        IOSimpleLockLock(sc->sc_sae_tx_lock);
        if (iwm_sae_tx_ticket_cancelled_locked(sc, request->ticket))
            rc = kIOReturnAborted;
        else if (sc->sc_sae_tx_stopping || sc->sc_sae_tx_active ||
                 sc->sc_sae_tx_event_count != 0)
            rc = kIOReturnNotReady;
        else {
            sc->sc_sae_tx_active = true;
            sc->sc_sae_tx_doorbelled = false;
            sc->sc_sae_tx_active_ticket = request->ticket;
            sc->sc_sae_tx_active_generation = sc->sc_sae_tx_generation;
            iwm_sae_tx_make_terminal_event(request, EIO,
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
        iwm_sae_tx_lifecycle_leave(sc);
        return rc;
    }

    explicit_bzero(&args, sizeof(args));
    memcpy(&args.request, request, sizeof(args.request));
    args.rc = kIOReturnNotReady;
    rc = gate->attemptAction(&ItlIwm::iwm_sae_tx_gate_action, &args);
    gate->release();
    if (rc == kIOReturnCannotLock)
        rc = kIOReturnNotReady;
    if (rc != kIOReturnSuccess)
        iwm_sae_tx_retire_unsubmitted(sc, request->ticket);
    iwm_sae_tx_lifecycle_leave(sc);
    explicit_bzero(&args, sizeof(args));
    return rc;
}

void ItlIwm::
cancelSaeAuthFrame(uint64_t ticket)
{
    struct iwm_softc *sc = &com;

    if (ticket == 0 || !iwm_sae_tx_lifecycle_enter(sc, true))
        return;
    if (sc->sc_sae_tx_lock == NULL) {
        iwm_sae_tx_lifecycle_leave(sc);
        return;
    }
    IOSimpleLockLock(sc->sc_sae_tx_lock);
    iwm_sae_tx_cancel_ticket_locked(sc, ticket);
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
        iwm_sae_tx_ticket_cancelled_locked(sc,
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
    iwm_sae_tx_lifecycle_leave(sc);
}

IOReturn ItlIwm::
iwm_sae_tx_gate_action(OSObject *owner, void *arg0, void *, void *, void *)
{
    ItlIwm *that = OSDynamicCast(ItlIwm, owner);
    IwmSaeTxGateArgs *args = (IwmSaeTxGateArgs *)arg0;

    if (that == NULL || args == NULL ||
        !itl_sae_auth_transport_request_is_well_formed(&args->request))
        return kIOReturnBadArgument;
    args->rc = that->iwm_sae_tx_submit_on_gate(&that->com, &args->request);
    return args->rc;
}

IOReturn ItlIwm::
iwm_sae_tx_submit_on_gate(struct iwm_softc *sc,
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
    ifp = &ic->ic_if;
    if (!iwm_sae_tx_request_is_live(sc, request->ticket)) {
        iwm_sae_tx_retire_unsubmitted(sc, request->ticket);
        return kIOReturnAborted;
    }
    if (!(ifp->if_flags & IFF_RUNNING) || sc->qfullmsk != 0 ||
        ic->ic_bss == NULL) {
        iwm_sae_tx_retire_unsubmitted(sc, request->ticket);
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
    if (!iwm_sae_tx_request_is_live(sc, request->ticket)) {
        rc = kIOReturnAborted;
        goto out;
    }
    if (ic->ic_state != IEEE80211_S_AUTH || ic->ic_bss != ni ||
        ieee80211_pae_assoc_epoch_current(ic) != request->association_epoch ||
        memcmp(ni->ni_macaddr, request->bssid, sizeof(request->bssid)) != 0 ||
        memcmp(ni->ni_bssid, request->bssid, sizeof(request->bssid)) != 0 ||
        memcmp(ic->ic_myaddr, request->sta, sizeof(request->sta)) != 0)
        goto out;

    error = iwm_tx(sc, m, ni, EDCA_AC_BE, request);
    m = NULL;
    if (error != 0) {
        rc = error == ENOMEM ? kIOReturnNoMemory : kIOReturnError;
        goto out;
    }
    if (ifp->netStat != NULL)
        ifp->netStat->outputPackets++;
    if (ifp->if_flags & IFF_UP) {
        sc->sc_tx_timer[EDCA_AC_BE] = 15;
        ifp->if_timer = 1;
    }
    ni = NULL;
    return kIOReturnSuccess;

out:
    if (m != NULL)
        mbuf_freem(m);
    if (ni != NULL)
        ieee80211_release_node(ic, ni);
    iwm_sae_tx_retire_unsubmitted(sc, request->ticket);
    return rc;
}

bool ItlIwm::
iwm_sae_tx_commit_doorbell(struct iwm_softc *sc, uint64_t ticket,
    int qid, int descriptor_idx, int next_cur, uint8_t station_id,
    uint16_t length)
{
    bool committed = false;

    if (sc == NULL || ticket == 0 ||
        sc->sc_sae_tx_lifecycle_lock == NULL || sc->sc_sae_tx_lock == NULL)
        return false;
    IOLockLock(sc->sc_sae_tx_lifecycle_lock);
    if (!sc->sc_sae_tx_lifecycle_closed && !sc->sc_sae_tx_detaching) {
        IOSimpleLockLock(sc->sc_sae_tx_lock);
        committed = !sc->sc_sae_tx_stopping && sc->sc_sae_tx_active &&
            !sc->sc_sae_tx_doorbelled &&
            sc->sc_sae_tx_active_ticket == ticket &&
            sc->sc_sae_tx_active_generation == sc->sc_sae_tx_generation &&
            !iwm_sae_tx_ticket_cancelled_locked(sc, ticket);
        if (committed) {
            iwm_update_sched(sc, qid, descriptor_idx, station_id, length);
            sc->sc_sae_tx_doorbelled = true;
            IWM_WRITE(sc, IWM_HBUS_TARG_WRPTR, qid << 8 | next_cur);
        }
        IOSimpleLockUnlock(sc->sc_sae_tx_lock);
    }
    IOLockUnlock(sc->sc_sae_tx_lifecycle_lock);
    return committed;
}

bool ItlIwm::
iwm_sae_tx_queue_terminal(struct iwm_softc *sc,
    const struct ItlSaeAuthTransportEventV1 *event,
    uint32_t lifecycle_generation, bool is_reset)
{
    bool queued = false;

    if (sc == NULL || event == NULL || sc->sc_sae_tx_lock == NULL ||
        !itl_sae_auth_transport_event_is_well_formed(event))
        return false;
    IOSimpleLockLock(sc->sc_sae_tx_lock);
    if (!is_reset) {
        if (!sc->sc_sae_tx_active || !sc->sc_sae_tx_doorbelled ||
            sc->sc_sae_tx_active_ticket != event->ticket ||
            sc->sc_sae_tx_active_generation != lifecycle_generation) {
            IOSimpleLockUnlock(sc->sc_sae_tx_lock);
            return false;
        }
        sc->sc_sae_tx_active = false;
        sc->sc_sae_tx_doorbelled = false;
        sc->sc_sae_tx_active_ticket = 0;
        sc->sc_sae_tx_active_generation = 0;
        explicit_bzero(&sc->sc_sae_tx_active_event,
            sizeof(sc->sc_sae_tx_active_event));
    }
    if (is_reset || !iwm_sae_tx_ticket_cancelled_locked(sc, event->ticket)) {
        KASSERT(sc->sc_sae_tx_event_count < IWM_SAE_TX_EVENTQ_LEN,
            "sc->sc_sae_tx_event_count < IWM_SAE_TX_EVENTQ_LEN");
        if (sc->sc_sae_tx_event_count >= IWM_SAE_TX_EVENTQ_LEN) {
            IOSimpleLockUnlock(sc->sc_sae_tx_lock);
            panic("%s: SAE TX terminal FIFO ownership violation", __FUNCTION__);
        }
        struct iwm_sae_tx_event_entry *entry =
            &sc->sc_sae_tx_eventq[sc->sc_sae_tx_event_tail];
        entry->event = *event;
        entry->is_reset = is_reset;
        sc->sc_sae_tx_last_event = *event;
        sc->sc_sae_tx_last_event_valid = true;
        sc->sc_sae_tx_event_tail = (sc->sc_sae_tx_event_tail + 1) %
            IWM_SAE_TX_EVENTQ_LEN;
        sc->sc_sae_tx_event_count++;
        queued = true;
    }
    IOSimpleLockUnlock(sc->sc_sae_tx_lock);
    if (queued)
        iwm_sae_tx_schedule_task(sc, is_reset);
    return true;
}

void ItlIwm::
iwm_sae_tx_report_terminal(struct iwm_softc *sc, struct iwm_tx_data *data,
    int32_t result)
{
    struct ItlSaeAuthTransportEventV1 event;
    uint32_t lifecycle_generation;

    if (data == NULL || !data->sae_active)
        return;
    lifecycle_generation = data->sae_lifecycle_generation;
    iwm_sae_tx_make_terminal_event_from_data(data, result, &event);
    iwm_sae_tx_data_clear(data);
    (void)iwm_sae_tx_queue_terminal(sc, &event, lifecycle_generation);
    explicit_bzero(&event, sizeof(event));
}

void ItlIwm::
iwm_sae_tx_retire_unsubmitted(struct iwm_softc *sc, uint64_t ticket)
{
    if (sc == NULL || ticket == 0 || sc->sc_sae_tx_lock == NULL)
        return;
    IOSimpleLockLock(sc->sc_sae_tx_lock);
    iwm_sae_tx_cancel_ticket_locked(sc, ticket);
    if (sc->sc_sae_tx_active && !sc->sc_sae_tx_doorbelled &&
        sc->sc_sae_tx_active_ticket == ticket) {
        sc->sc_sae_tx_active = false;
        sc->sc_sae_tx_active_ticket = 0;
        sc->sc_sae_tx_active_generation = 0;
        explicit_bzero(&sc->sc_sae_tx_active_event,
            sizeof(sc->sc_sae_tx_active_event));
    }
    if (sc->sc_sae_tx_last_event_valid &&
        iwm_sae_tx_ticket_cancelled_locked(sc,
            sc->sc_sae_tx_last_event.ticket)) {
        sc->sc_sae_tx_last_event_valid = false;
        explicit_bzero(&sc->sc_sae_tx_last_event,
            sizeof(sc->sc_sae_tx_last_event));
    }
    IOSimpleLockUnlock(sc->sc_sae_tx_lock);
}

void ItlIwm::
iwm_sae_tx_cancel_all(struct iwm_softc *sc)
{
    if (sc == NULL || sc->sc_sae_tx_lock == NULL)
        return;
    IOSimpleLockLock(sc->sc_sae_tx_lock);
    iwm_sae_tx_cancel_ticket_locked(sc, sc->sc_sae_tx_active_ticket);
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
    explicit_bzero(sc->sc_sae_tx_eventq, sizeof(sc->sc_sae_tx_eventq));
    sc->sc_sae_tx_event_head = 0;
    sc->sc_sae_tx_event_tail = 0;
    sc->sc_sae_tx_event_count = 0;
    IOSimpleLockUnlock(sc->sc_sae_tx_lock);
}

void ItlIwm::
iwm_sae_tx_stop_begin(struct iwm_softc *sc)
{
    if (sc == NULL)
        return;
    iwm_sae_tx_lifecycle_close(sc, false);
    if (sc->sc_sae_tx_lock == NULL)
        return;
    IOSimpleLockLock(sc->sc_sae_tx_lock);
    sc->sc_sae_tx_stopping = true;
    iwm_sae_tx_generation_advance_locked(sc);
    if (sc->sc_sae_tx_active && !sc->sc_sae_tx_doorbelled) {
        iwm_sae_tx_cancel_ticket_locked(sc, sc->sc_sae_tx_active_ticket);
        sc->sc_sae_tx_active = false;
        sc->sc_sae_tx_active_ticket = 0;
        sc->sc_sae_tx_active_generation = 0;
        explicit_bzero(&sc->sc_sae_tx_active_event,
            sizeof(sc->sc_sae_tx_active_event));
    }
    IOSimpleLockUnlock(sc->sc_sae_tx_lock);
}

void ItlIwm::
iwm_sae_tx_reopen(struct iwm_softc *sc)
{
    if (sc == NULL || sc->sc_sae_tx_lifecycle_lock == NULL ||
        sc->sc_sae_tx_lock == NULL)
        return;
    IOLockLock(sc->sc_sae_tx_lifecycle_lock);
    if (!sc->sc_sae_tx_detaching) {
        IOSimpleLockLock(sc->sc_sae_tx_lock);
        iwm_sae_tx_generation_advance_locked(sc);
        sc->sc_sae_tx_stopping = false;
        IOSimpleLockUnlock(sc->sc_sae_tx_lock);
        sc->sc_sae_tx_lifecycle_closed = false;
    }
    IOLockUnlock(sc->sc_sae_tx_lifecycle_lock);
}

bool ItlIwm::
iwm_sae_tx_snapshot_reset(struct iwm_softc *sc,
    struct ItlSaeAuthTransportEventV1 *snapshot)
{
    bool have_snapshot = false;

    if (sc == NULL || snapshot == NULL || sc->sc_sae_tx_lock == NULL)
        return false;
    explicit_bzero(snapshot, sizeof(*snapshot));
    IOSimpleLockLock(sc->sc_sae_tx_lock);
    if (sc->sc_sae_tx_active && sc->sc_sae_tx_doorbelled &&
        !iwm_sae_tx_ticket_cancelled_locked(sc,
            sc->sc_sae_tx_active_ticket) &&
        itl_sae_auth_transport_event_is_well_formed(
            &sc->sc_sae_tx_active_event)) {
        *snapshot = sc->sc_sae_tx_active_event;
        snapshot->result = EIO;
        have_snapshot = true;
    } else if (sc->sc_sae_tx_event_count != 0) {
        const struct ItlSaeAuthTransportEventV1 *event =
            &sc->sc_sae_tx_eventq[sc->sc_sae_tx_event_head].event;
        if (!iwm_sae_tx_ticket_cancelled_locked(sc, event->ticket) &&
            itl_sae_auth_transport_event_is_well_formed(event)) {
            *snapshot = *event;
            snapshot->result = EIO;
            have_snapshot = true;
        }
    } else if (sc->sc_sae_tx_last_event_valid &&
        !iwm_sae_tx_ticket_cancelled_locked(sc,
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

void ItlIwm::
iwm_sae_tx_emit_reset_event(struct iwm_softc *sc,
    const struct ItlSaeAuthTransportEventV1 *event)
{
    if (sc == NULL || event == NULL ||
        !itl_sae_auth_transport_event_is_well_formed(event) ||
        !iwm_sae_tx_lifecycle_enter(sc, true))
        return;
    (void)iwm_sae_tx_queue_terminal(sc, event, 0, true);
    iwm_sae_tx_lifecycle_leave(sc);
}

void ItlIwm::
iwm_sae_tx_purge(struct iwm_softc *sc)
{
    if (sc == NULL || sc->sc_sae_tx_lock == NULL)
        return;
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

void ItlIwm::
iwm_sae_tx_task(void *arg)
{
    struct iwm_softc *sc = (struct iwm_softc *)arg;
    struct ieee80211com *ic;
    struct ItlSaeAuthTransportEventV1 event;
    bool have_event = false;
    bool is_reset = false;
    bool suppressed = false;
    bool more = false;
    bool engine_consumed = false;

    if (sc == NULL || !iwm_sae_tx_lifecycle_enter(sc, true))
        return;
    ic = &sc->sc_ic;
    explicit_bzero(&event, sizeof(event));
    if (sc->sc_sae_tx_lock != NULL) {
        IOSimpleLockLock(sc->sc_sae_tx_lock);
        if (sc->sc_sae_tx_event_count != 0) {
            struct iwm_sae_tx_event_entry *entry =
                &sc->sc_sae_tx_eventq[sc->sc_sae_tx_event_head];
            event = entry->event;
            is_reset = entry->is_reset;
            explicit_bzero(entry, sizeof(*entry));
            sc->sc_sae_tx_event_head = (sc->sc_sae_tx_event_head + 1) %
                IWM_SAE_TX_EVENTQ_LEN;
            sc->sc_sae_tx_event_count--;
            suppressed = !is_reset &&
                iwm_sae_tx_ticket_cancelled_locked(sc, event.ticket);
            more = sc->sc_sae_tx_event_count != 0;
            have_event = true;
        }
        IOSimpleLockUnlock(sc->sc_sae_tx_lock);
    }
    if (have_event && !suppressed &&
        itl_sae_auth_transport_event_is_well_formed(&event)) {
        if (iwm_sae_engine_callback_enter(sc)) {
            const bool queued_direct =
                iwm_sae_engine_queue_terminal(sc, &event);
            engine_consumed = iwm_sae_tx_ticket_is_direct(event.ticket) ||
                queued_direct;
            iwm_sae_engine_callback_leave(sc);
        } else if (iwm_sae_tx_ticket_is_direct(event.ticket)) {
            engine_consumed = true;
        }
    }
    if (have_event && !suppressed && !engine_consumed &&
        (is_reset || iwm_sae_tx_lifecycle_is_open(sc)) &&
        itl_sae_auth_transport_event_is_well_formed(&event) &&
        ic->ic_event_handler != NULL) {
        (*ic->ic_event_handler)(ic,
            is_reset ? IEEE80211_EVT_SAE_AUTH_TRANSPORT_RESET :
                       IEEE80211_EVT_SAE_AUTH_TRANSPORT,
            &event);
    }
    explicit_bzero(&event, sizeof(event));
    if (more)
        iwm_sae_tx_schedule_task(sc, is_reset);
    iwm_sae_tx_lifecycle_leave(sc);
}

void ItlIwm::
iwm_sae_tx_detach_begin(struct iwm_softc *sc)
{
    IOCommandGate *gate;
    bool task_ready = false;

    if (sc == NULL)
        return;
    iwm_sae_tx_lifecycle_close(sc, true);
    iwm_sae_tx_cancel_all(sc);
    if (sc->sc_sae_tx_lifecycle_lock != NULL) {
        IOLockLock(sc->sc_sae_tx_lifecycle_lock);
        task_ready = sc->sc_sae_tx_task_ready;
        sc->sc_sae_tx_task_ready = false;
        IOLockUnlock(sc->sc_sae_tx_lifecycle_lock);
    }
    if (task_ready && systq != NULL) {
        (void)task_del(systq, &sc->sae_tx_task);
        taskq_barrier(systq);
    }
    iwm_sae_tx_lifecycle_drain(sc);
    gate = fSaeTxGate;
    fSaeTxGate = NULL;
    if (gate != NULL) {
        if (pci.workloop != NULL)
            pci.workloop->removeEventSource(gate);
        gate->release();
    }
}

int ItlIwm::
iwm_assoc_comeback_retry(struct ieee80211com *ic,
    const struct ieee80211_assoc_comeback_retry *retry)
{
    struct iwm_softc *sc;
    bool queued = false;

    if (ic == NULL || retry == NULL)
        return EINVAL;
    sc = (struct iwm_softc *)ic->ic_softc;
    if (sc == NULL || sc->sc_nswq == NULL ||
        !iwm_sae_tx_lifecycle_enter(sc, false))
        return ENXIO;

    /* Publish only an immutable association identity.  In particular, no
     * node pointer survives until the process-context firmware command. */
    IOLockLock(sc->sc_sae_tx_lifecycle_lock);
    if (!sc->sc_sae_tx_lifecycle_closed && !sc->sc_sae_tx_detaching &&
        sc->sc_assoc_comeback_task_ready &&
        (sc->sc_flags & IWM_FLAG_SHUTDOWN) == 0 &&
        !sc->sc_assoc_comeback_queued) {
        sc->sc_assoc_comeback_retry = *retry;
        sc->sc_assoc_comeback_generation = sc->sc_generation;
        sc->sc_assoc_comeback_queued = true;
        queued = task_add(sc->sc_nswq, &sc->assoc_comeback_task) != 0;
        if (!queued) {
            sc->sc_assoc_comeback_queued = false;
            explicit_bzero(&sc->sc_assoc_comeback_retry,
                sizeof(sc->sc_assoc_comeback_retry));
            sc->sc_assoc_comeback_generation = 0;
        }
    }
    IOLockUnlock(sc->sc_sae_tx_lifecycle_lock);
    iwm_sae_tx_lifecycle_leave(sc);
    return queued ? 0 : EBUSY;
}

void ItlIwm::
iwm_assoc_comeback_task(void *arg)
{
    struct iwm_softc *sc = (struct iwm_softc *)arg;
    ItlIwm *that;
    struct ieee80211com *ic;
    struct ieee80211_assoc_comeback_retry retry;
    struct iwm_node *in;
    uint32_t duration_tu;
    int error = ENOENT;
    int generation = 0;
    int s;
    bool have_work = false;

    if (sc == NULL || !iwm_sae_tx_lifecycle_enter(sc, false))
        return;
    that = container_of(sc, ItlIwm, com);
    ic = &sc->sc_ic;
    explicit_bzero(&retry, sizeof(retry));

    IOLockLock(sc->sc_sae_tx_lifecycle_lock);
    if (sc->sc_assoc_comeback_task_ready &&
        sc->sc_assoc_comeback_queued) {
        retry = sc->sc_assoc_comeback_retry;
        generation = sc->sc_assoc_comeback_generation;
        have_work = true;
    }
    IOLockUnlock(sc->sc_sae_tx_lifecycle_lock);

    s = splnet();
    if (have_work && generation == sc->sc_generation &&
        (sc->sc_flags & IWM_FLAG_SHUTDOWN) == 0 &&
        ic->ic_assoc_comeback_pending && ic->ic_bss != NULL &&
        ieee80211_pae_assoc_epoch_current(ic) == retry.association_epoch &&
        IEEE80211_ADDR_EQ(ic->ic_bss->ni_bssid, retry.bssid)) {
        in = (struct iwm_node *)ic->ic_bss;
        duration_tu = in->in_ni.ni_intval != 0 ?
            in->in_ni.ni_intval * 9U : 900U;
        duration_tu = MAX(duration_tu, 900U);

        /* mac80211 mgd_prepare_tx -> iwl_mvm_protect_assoc: renew the
         * firmware lease before publishing the delayed q1 descriptor. */
        if (sc->sc_flags & IWM_FLAG_TE_ACTIVE)
            that->iwm_unprotect_session(sc, in);
        if (sc->sc_flags & IWM_FLAG_TE_ACTIVE) {
            error = EBUSY;
        } else {
            that->iwm_protect_session(sc, in, duration_tu,
                in->in_ni.ni_intval / 2);
            error = (sc->sc_flags & IWM_FLAG_TE_ACTIVE) != 0 ? 0 : EIO;
        }

        if (error == 0) {
            error = ieee80211_assoc_comeback_retry_complete(ic, &retry);
            if (error == 0) {
                IOCommandGate *gate = that->getMainCommandGate();
                const IOReturn drain = gate != NULL ?
                    gate->runAction(_iwm_start_task, &ic->ic_ac.ac_if) :
                    kIOReturnNotReady;
                if (drain != kIOReturnSuccess)
                    error = EIO;
            }
        } else {
            (void)ieee80211_assoc_comeback_retry_abort(ic, &retry, error);
        }

        XYLog("IWM assoc comeback lower retry=%u protection=%d state=%u\n",
            (unsigned)retry.retry, error, (unsigned)ic->ic_state);
    }
    splx(s);

    IOLockLock(sc->sc_sae_tx_lifecycle_lock);
    sc->sc_assoc_comeback_queued = false;
    explicit_bzero(&sc->sc_assoc_comeback_retry,
        sizeof(sc->sc_assoc_comeback_retry));
    sc->sc_assoc_comeback_generation = 0;
    IOLockUnlock(sc->sc_sae_tx_lifecycle_lock);
    explicit_bzero(&retry, sizeof(retry));
    iwm_sae_tx_lifecycle_leave(sc);
}

void ItlIwm::
iwm_assoc_comeback_cancel(struct iwm_softc *sc)
{
    if (sc == NULL || sc->sc_sae_tx_lifecycle_lock == NULL)
        return;
    IOLockLock(sc->sc_sae_tx_lifecycle_lock);
    sc->sc_assoc_comeback_queued = false;
    explicit_bzero(&sc->sc_assoc_comeback_retry,
        sizeof(sc->sc_assoc_comeback_retry));
    sc->sc_assoc_comeback_generation = 0;
    IOLockUnlock(sc->sc_sae_tx_lifecycle_lock);
}

#include "IwmMfpPae.inc"
#include "IwmSaeEngine.inc"

void ItlIwm::
iwm_newstate_task_dispatch(void *arg)
{
    struct iwm_softc *sc = (struct iwm_softc *)arg;
    /* Initial SCAN precedes SAE admission. The scan/state leaf is the
     * readiness gate; this lease only keeps the softc alive through detach. */
    if (!iwm_sae_tx_lifecycle_enter(sc, true))
        return;
    iwm_newstate_task(arg);
    iwm_sae_tx_lifecycle_leave(sc);
}

void ItlIwm::
detach(IOPCIDevice *device)
{
    struct _ifnet *ifp = &com.sc_ic.ic_ac.ac_if;
    struct iwm_softc *sc = &com;

    /* No submitter, deferred terminal, or retained private gate may outlive
     * the descriptor rings and net80211 event sink below. */
    invalidateWclScanForReset();
    shutdownStateTransitions();
    sc->sc_ic.ic_assoc_comeback_retry = NULL;
    iwm_sae_engine_detach_begin(sc);
    iwm_sae_tx_detach_begin(sc);
    if (sc->sc_nswq != NULL) {
        (void)task_del(sc->sc_nswq, &sc->newstate_task);
        if (sc->sc_assoc_comeback_task_ready)
            (void)task_del(sc->sc_nswq, &sc->assoc_comeback_task);
        taskq_barrier(sc->sc_nswq);
    }
    sc->sc_assoc_comeback_task_ready = false;
    iwm_assoc_comeback_cancel(sc);
    iwm_sae_wcl_detach_begin(sc);
    iwm_mfp_pae_detach_begin(sc);
    iwm_sae_engine_callback_close(sc);
    iwm_sae_engine_callback_drain(sc);
    (void)iwm_sae_engine_publish_hooks(sc, false, false, 0);
    
    for (int txq_i = 0; txq_i < nitems(sc->txq); txq_i++)
        iwm_free_tx_ring(sc, &sc->txq[txq_i]);
    iwm_sae_tx_purge(sc);
    iwm_rs_free(sc);
    iwm_free_rx_ring(sc, &sc->rxq);
    iwm_dma_contig_free(&sc->ict_dma);
    iwm_dma_contig_free(&sc->kw_dma);
    iwm_dma_contig_free(&sc->sched_dma);
    iwm_dma_contig_free(&sc->fw_dma);
    ieee80211_ifdetach(ifp);
    iwm_mfp_pae_callback_destroy(sc);
    taskq_destroy(systq);
    taskq_destroy(com.sc_nswq);
    releaseAll();
}

bool ItlIwm::
attach(IOPCIDevice *device)
{
    itl_ap_firmware_runtime_reset(&apRuntime);
    apPrimaryStaRecoveryScanAbortPending = false;
    apPrimaryStaRecoveryScanYielded = false;
    apPrimaryStaRecoveryScanGeneric = false;
    apPrimaryStaRecoveryScanGeneration = 0;
    apCsaTimeout = NULL;
    apCsaTimerInitialized = false;
    timeout_set(&apCsaTimeout, iwm_ap_csa_timeout, this);
    apCsaTimerInitialized = true;
    fSaeTxGate = NULL;
    stateTransitionSource = NULL;
    stateTransition = ItlStateTransitionLease{};
    primaryMacContext = ItlFirmwareContextLease{};
    primaryBindingContext = ItlFirmwareContextLease{};
    primaryStationContext = ItlFirmwareContextLease{};
    primaryStationUses = ItlFirmwareStationUses{};
    primaryRxBa = ItlStationRxBa{};
    primaryStationRetirement = ItlFirmwareStationRetirement{};
    memset(&primaryStationCommand, 0, sizeof(primaryStationCommand));
    memset(&primaryMacCommand, 0, sizeof(primaryMacCommand));
    scanCommand = ItlScanCommandLease{};
    scanCommandPolicy = ItlScanCommandPolicy{};
    scanCommandAbortSerial = 0;
    wclScanLock = IOSimpleLockAlloc();
    if (wclScanLock == NULL)
        return false;
    wclScanPhase = ItlIwmWclScanPhase::Idle;
    wclScanUpperGeneration = 0;
    wclScanBackendGeneration = 0;
    wclScanNextBackendGeneration = 0;
    wclScanPublicationInvalidated = false;
    /*
     * The first committed SCAN state is the initial lower-radio-ready edge,
     * not only a reset recovery edge.  AirportItlwm consumes the matching
     * one-shot WCL_SCAN_REOPENED event to publish the idle Tahoe APSTA role
     * after firmware capabilities are available.  Starting this false left
     * IWM with no ap1 inventory until a later radio reset even when the
     * loaded firmware passed every AP/GO carrier gate.
     */
    wclScanNeedsReopen = true;
    wclSaeAdmissionReserved = false;

    pci.pa_tag = device;
    pci.workloop = getMainWorkLoop();
    if (!initStateTransitions()) {
        releaseAll();
        return false;
    }
    if (!iwm_attach(&com, &pci)) {
        detach(device);
        releaseAll();
        return false;
    }
    return true;
}

void ItlIwm::
free()
{
	if (ieee80211_bip_lifetime_drain(&com.sc_ic) != 0)
		panic("ItlIwm::free BIP lifetime");
	ieee80211_pae_selected_bss_lock_destroy(&com.sc_ic);
    super::free();
}

void ItlIwm::
releaseAll()
{
    shutdownStateTransitions();
    pci_intr_handle *intrHandler = com.ih;
    if (apCsaTimerInitialized) {
        timeout_del(&apCsaTimeout);
        timeout_free(&apCsaTimeout);
        apCsaTimerInitialized = false;
    }
    if (com.sc_calib_to) {
        timeout_del(&com.sc_calib_to);
        timeout_free(&com.sc_calib_to);
    }
    if (com.sc_led_blink_to) {
        timeout_del(&com.sc_led_blink_to);
        timeout_free(&com.sc_led_blink_to);
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
    if (wclScanLock != NULL) {
        IOSimpleLockFree(wclScanLock);
        wclScanLock = NULL;
    }
    if (com.sc_sae_tx_lock != NULL) {
        IOSimpleLockFree(com.sc_sae_tx_lock);
        com.sc_sae_tx_lock = NULL;
    }
    if (com.sc_sae_tx_lifecycle_lock != NULL) {
        IOLockFree(com.sc_sae_tx_lifecycle_lock);
        com.sc_sae_tx_lifecycle_lock = NULL;
    }
    if (com.sc_sae_engine_lock != NULL) {
        IOSimpleLockFree(com.sc_sae_engine_lock);
        com.sc_sae_engine_lock = NULL;
    }
    if (com.sc_sae_wcl_credential_lock != NULL) {
        IOSimpleLockFree(com.sc_sae_wcl_credential_lock);
        com.sc_sae_wcl_credential_lock = NULL;
    }
    if (com.sc_mfp_pae_lock != NULL) {
        IOSimpleLockFree(com.sc_mfp_pae_lock);
        com.sc_mfp_pae_lock = NULL;
    }
}

IOReturn ItlIwm::
enable(IONetworkInterface *netif)
{
    struct _ifnet *ifp = &com.sc_ic.ic_ac.ac_if;
    if (ifp->if_flags & IFF_UP) {
        XYLog("DEBUG %s SKIP: already IFF_UP\n", __FUNCTION__);
        return kIOReturnSuccess;
    }
    ifp->if_flags |= IFF_UP;
    iwm_activate(&com, DVACT_RESUME);
    iwm_activate(&com, DVACT_WAKEUP);
    return kIOReturnSuccess;
}

IOReturn ItlIwm::
disable(IONetworkInterface *netif)
{
    struct _ifnet *ifp = &com.sc_ic.ic_ac.ac_if;
    /* APSTAOwner has already closed the role-7 datapath but deliberately
     * retains its profile across sleep.  Retire the firmware GO resources
     * before the generic iwm_stop() destroys their rings; wake will rebuild
     * them from that upper snapshot after the primary STA boundary. */
    if (apCsaTimerInitialized)
        timeout_del(&apCsaTimeout);
    if (apRuntime.stage != kItlApFirmwareResourceIdle) {
        const int apError = iwm_stop_ap_resources(
            &com, &apRuntime, true);
        if (apError != 0)
            XYLog("%s: IWM AP radio-reset teardown error=%d\n",
                  DEVNAME(&com), apError);
    }
    if (!(ifp->if_flags & IFF_UP)) {
        XYLog("DEBUG %s SKIP: already !IFF_UP\n", __FUNCTION__);
        return kIOReturnSuccess;
    }
    ifp->if_flags &= ~IFF_UP;
    iwm_activate(&com, DVACT_QUIESCE);
    return kIOReturnSuccess;
}

bool ItlIwm::
supportsAPMode() const
{
#if !defined(IEEE80211_OPT_OUT_STA_ONLY)
    return false;
#else
    /*
     * This implementation uses DQA queues, typed GO/link stations, and the
     * MQ-RX descriptor path.  Admit only firmware that advertised all three;
     * 7k/legacy queue and legacy RX families remain fail-closed.
     */
    return (com.sc_device_family == IWM_DEVICE_FAMILY_8000 ||
            com.sc_device_family == IWM_DEVICE_FAMILY_9000) &&
        isset(com.sc_enabled_capa, IWM_UCODE_TLV_CAPA_DQA_SUPPORT) &&
        isset(com.sc_ucode_api, IWM_UCODE_TLV_API_STA_TYPE) &&
        com.sc_mqrx_supported;
#endif
}

bool ItlIwm::
isAPScanFenceActive() const
{
    return currentAPScanCommand() != 0 ||
        apRuntime.stage != kItlApFirmwareResourceIdle;
}

IOReturn ItlIwm::
startAPMode(const struct ItlHalApConfig *config)
{
    if (!supportsAPMode()) {
        resumePrimaryStaRecoveryScanAfterAPHandoff();
        return kIOReturnUnsupported;
    }
    if (!itl_ap_client_config_supported(config)) {
        resumePrimaryStaRecoveryScanAfterAPHandoff();
        return kIOReturnUnsupported;
    }
    if (apRuntime.stage != kItlApFirmwareResourceIdle) {
        resumePrimaryStaRecoveryScanAfterAPHandoff();
        return kIOReturnBusy;
    }
    int error = itl_ap_firmware_runtime_snapshot(&apRuntime, config);
    if (error != 0) {
        resumePrimaryStaRecoveryScanAfterAPHandoff();
        return kIOReturnBadArgument;
    }
    error = iwm_start_ap_resources(&com, &apRuntime);
    if (error != 0) {
        /* A failed AP admission no longer owns the radio.  Return the exact
         * yielded foreground scan only after firmware has rejected the AP
         * resource transaction.  A successful HostAP keeps that scan policy
         * suspended until stopAPMode() reaches its lower terminal. */
        resumePrimaryStaRecoveryScanAfterAPHandoff();
        itl_ap_firmware_runtime_reset(&apRuntime);
        if (error == EOPNOTSUPP)
            return kIOReturnUnsupported;
        if (error == EINVAL)
            return kIOReturnBadArgument;
        return error == EBUSY ? kIOReturnBusy : kIOReturnError;
    }
    return kIOReturnSuccess;
}

IOReturn ItlIwm::
stopAPMode()
{
    if (apCsaTimerInitialized)
        timeout_del(&apCsaTimeout);
    if (apRuntime.stage == kItlApFirmwareResourceIdle) {
        if (currentAPScanCommand() != 0)
            return kIOReturnNotReady;
        resumePrimaryStaRecoveryScanAfterAPHandoff();
        return kIOReturnSuccess;
    }
    const int error = iwm_stop_ap_resources(&com, &apRuntime);
    if (error != 0)
        return kIOReturnError;
    resumePrimaryStaRecoveryScanAfterAPHandoff();
    return kIOReturnSuccess;
}

IOReturn ItlIwm::
transmitAPData(mbuf_t packet)
{
    struct ether_header ethernet;
    if (packet == NULL || mbuf_pkthdr_len(packet) < sizeof(ethernet) ||
        mbuf_copydata(packet, 0, sizeof(ethernet), &ethernet) != 0)
        return kIOReturnBadArgument;
    struct ItlApFirmwareClientRuntime *client =
        itl_ap_firmware_find_tx_client(&apRuntime, ethernet.ether_dhost);
    if (client == NULL)
        return kIOReturnNotReady;
    if (itl_ap_power_save_should_buffer(&apRuntime, client, packet)) {
        const bool queueWasEmpty = client->powerSaveQueueCount == 0;
        const int queueError =
            itl_ap_power_save_enqueue(client, packet);
        if (queueError != 0)
            return queueError == ENOBUFS ? kIOReturnNoResources :
                                           kIOReturnBadArgument;
        bool changed = false;
        int timError =
            itl_ap_power_save_set_tim(&apRuntime, client, true, &changed);
        if (timError == 0 && changed)
            timError = iwm_ap_send_beacon_template(&com, &apRuntime);
        if (timError != 0 && changed) {
            bool ignored = false;
            (void)itl_ap_power_save_set_tim(
                &apRuntime, client, false, &ignored);
            XYLog("%s: IWM AP power-save TIM arm failed\n",
                  DEVNAME(&com));
        }
        if (timError != 0 && queueWasEmpty) {
            (void)itl_ap_power_save_dequeue(client);
            return timError == ENOBUFS ? kIOReturnNoResources :
                                         kIOReturnError;
        }
        return kIOReturnSuccess;
    }
    mbuf_t wirePacket = NULL;
    int error = itl_ap_open_encap_data(
        &apRuntime, client, packet, false, &wirePacket);
    if (error != 0)
        return error == ENOBUFS ? kIOReturnNoResources : kIOReturnNotReady;
    const struct ieee80211_frame *wh =
        mtod(wirePacket, const struct ieee80211_frame *);
    const bool multicast = IEEE80211_IS_MULTICAST(wh->i_addr1);
    error = iwm_ap_send_raw_frame(&com, wirePacket,
        static_cast<uint8_t>(multicast ? apRuntime.multicastQueueId :
                                        client->queueId),
        multicast ? apRuntime.multicastStaId : client->staId);
    if (error != 0) {
        mbuf_freem(wirePacket);
        return error == ENOBUFS ? kIOReturnNoResources : kIOReturnError;
    }
    if (!multicast && ethernet.ether_type != htons(ETHERTYPE_PAE) &&
        client->clientQos && client->clientHt &&
        itl_ap_tx_ba_note_data(&client->clientTxBa[0])) {
        uint8_t token = ++client->clientTxDialogToken;
        if (token == 0)
            token = ++client->clientTxDialogToken;
        itl_ap_tx_ba_request(&client->clientTxBa[0], token, 0,
                             client->clientTxSequence[0]);
        mbuf_t request = NULL;
        int requestError = itl_ap_open_build_tx_addba_request(
            &apRuntime, client, 0, &request);
        if (requestError == 0)
            /* Management action frames use the AP management queue/station;
             * the per-client queue remains a QoS data-TID owner. */
            requestError = iwm_ap_send_raw_frame(
                &com, request,
                static_cast<uint8_t>(apRuntime.broadcastQueueId),
                apRuntime.broadcastStaId);
        if (requestError != 0) {
            if (request != NULL)
                mbuf_freem(request);
            itl_ap_tx_ba_reset(&client->clientTxBa[0]);
        } else {
            XYLog("%s: IWM AP TX ADDBA request tid=0 ssn=%u token=%u\n",
                  DEVNAME(&com),
                  static_cast<unsigned>(client->clientTxSequence[0]),
                  static_cast<unsigned>(token));
        }
    }
    mbuf_freem(packet);
    return kIOReturnSuccess;
}

IOReturn ItlIwm::
setAPKey(const struct ItlHalApKey *key)
{
    if (!itl_ap_open_is_running(&apRuntime))
        return kIOReturnNotReady;
    if (!itl_ap_client_is_secure(&apRuntime))
        return kIOReturnUnsupported;
    if (key == NULL || key->keyData == NULL || key->keyLength != 16 ||
        key->cipher != kItlHalApCipherAesCcm || key->keyIndex > 3 ||
        (key->flags != kItlHalApKeyPairwise &&
         key->flags != kItlHalApKeyGroup))
        return kIOReturnBadArgument;

    const bool pairwise = key->flags == kItlHalApKeyPairwise;
    struct ItlApFirmwareClientRuntime *client = pairwise ?
        itl_ap_firmware_find_client(&apRuntime, key->station) : NULL;
    if (pairwise && (client == NULL || !client->clientAssociated ||
        !client->clientStationInstalled))
        return kIOReturnNotReady;
    if (!pairwise && apRuntime.groupKeyInstalled &&
        apRuntime.groupKeyId == key->keyIndex &&
        timingsafe_bcmp(apRuntime.groupKey, key->keyData,
                        sizeof(apRuntime.groupKey)) == 0)
        return kIOReturnSuccess;

    const uint8_t staId = pairwise ? client->staId :
                                     apRuntime.multicastStaId;
    const uint8_t keyOffset = pairwise ? static_cast<uint8_t>(
        2 + itl_ap_firmware_client_index(&apRuntime, client)) :
        static_cast<uint8_t>(2 + kItlApFirmwareMaxClients);
    int error = 0;
    /* Legacy IWM TX commands carry the AP GTK inline.  Linux iwlwifi keeps
     * that AP group key out of ADD_STA_KEY; only the PTK is needed for RX. */
    if (pairwise)
        error = iwm_ap_set_ccmp_key(&com, staId, true, keyOffset,
            key->keyIndex, key->keyData, key->keyLength,
            key->rsc, key->rscLength);
    if (error != 0)
        return kIOReturnError;

    const uint64_t receiveSequence = itl_ap_key_rsc(key);
    if (pairwise) {
        memcpy(client->clientPairwiseKey, key->keyData,
               sizeof(client->clientPairwiseKey));
        client->clientPairwiseTxPn = 0;
        for (size_t tid = 0; tid < nitems(client->clientRxPn); tid++)
            client->clientRxPn[tid] = receiveSequence;
        client->clientPairwiseKeyInstalled = true;
    } else {
        memcpy(apRuntime.groupKey, key->keyData,
               sizeof(apRuntime.groupKey));
        apRuntime.groupKeyId = key->keyIndex;
        apRuntime.groupTxPn = 0;
        apRuntime.groupKeyInstalled = true;
    }
    return kIOReturnSuccess;
}

IOReturn ItlIwm::
setAPMaxStations(uint32_t maxStations)
{
    if (!itl_ap_open_is_running(&apRuntime))
        return kIOReturnNotReady;
    return itl_ap_firmware_set_client_limit(
        &apRuntime, maxStations) == 0 ? kIOReturnSuccess :
                                       kIOReturnBadArgument;
}

IOReturn ItlIwm::
setAPHidden(bool hidden)
{
    if (!itl_ap_open_is_running(&apRuntime))
        return kIOReturnNotReady;
    if (apRuntime.hidden == hidden)
        return kIOReturnSuccess;

    const bool previousHidden = apRuntime.hidden;
    int error = itl_ap_firmware_set_hidden(&apRuntime, hidden);
    if (error != 0)
        return error == ENOENT ? kIOReturnUnsupported : kIOReturnBadArgument;
    error = iwm_ap_send_beacon_template(&com, &apRuntime);
    if (error != 0) {
        const int rollback =
            itl_ap_firmware_set_hidden(&apRuntime, previousHidden);
        if (rollback == 0)
            (void)iwm_ap_send_beacon_template(&com, &apRuntime);
        return kIOReturnError;
    }
    return kIOReturnSuccess;
}

IOReturn ItlIwm::
triggerAPCSA(const struct ItlHalApCSA *csa)
{
    if (!itl_ap_open_is_running(&apRuntime))
        return kIOReturnNotReady;
    if (csa == NULL || csa->channel == 0 || csa->channel > UINT8_MAX ||
        csa->mode > 1 || iwm_ap_find_channel(&com, csa->channel) == NULL)
        return kIOReturnBadArgument;
    if (apRuntime.csaPending)
        return kIOReturnBusy;
    if (csa->channel == apRuntime.config.channel)
        return kIOReturnSuccess;

    const uint8_t count = csa->count != 0 ? csa->count :
        kItlApCsaDefaultCount;
    int error = itl_ap_beacon_begin_csa(
        apRuntime.beacon, &apRuntime.config.beaconTemplateLength,
        sizeof(apRuntime.beacon), csa->mode,
        static_cast<uint8_t>(csa->channel), count);
    if (error != 0)
        return error == EBUSY ? kIOReturnBusy : kIOReturnBadArgument;
    error = iwm_ap_send_beacon_template(&com, &apRuntime);
    if (error != 0) {
        (void)itl_ap_beacon_end_csa(
            apRuntime.beacon, &apRuntime.config.beaconTemplateLength,
            static_cast<uint8_t>(csa->channel), false);
        return kIOReturnError;
    }

    apRuntime.csaPending = true;
    apRuntime.csaTargetChannel = csa->channel;
    apRuntime.csaMode = csa->mode;
    apRuntime.csaCount = count;
    const uint32_t delayMs = MAX(1U, static_cast<uint32_t>(
        (static_cast<uint64_t>(apRuntime.config.beaconInterval) *
         IEEE80211_DUR_TU + 999) / 1000));
    timeout_add_msec(&apCsaTimeout, delayMs);
    XYLog("%s: IWM AP CSA armed channel=%u mode=%u count=%u delay=%u ms\n",
          DEVNAME(&com), static_cast<unsigned>(csa->channel),
          static_cast<unsigned>(csa->mode), static_cast<unsigned>(count),
          static_cast<unsigned>(delayMs));
    return kIOReturnSuccess;
}

uint16_t ItlIwm::
getAPCurrentChannel() const
{
    return itl_ap_open_is_running(&apRuntime) ?
        apRuntime.config.channel : 0;
}

bool ItlIwm::
isPrimaryStaRecoveryScanPending() const
{
    struct iwm_softc *sc = const_cast<struct iwm_softc *>(&com);
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

IOReturn ItlIwm::
handoffPrimaryStaRecoveryScanToAP()
{
    struct iwm_softc *sc = &com;
    struct ieee80211com *ic = &sc->sc_ic;

    if (!supportsAPMode() || wclScanLock == NULL ||
        sc->sc_sae_wcl_credential_lock == NULL)
        return kIOReturnNotReady;

    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    const bool wclOwned = wclScanPhase != ItlIwmWclScanPhase::Idle;
    const bool yielded = apPrimaryStaRecoveryScanYielded;
    const bool abortPending = apPrimaryStaRecoveryScanAbortPending;
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    if (wclOwned)
        return kIOReturnBusy;
    if (yielded)
        return kIOReturnSuccess;
    if (abortPending)
        return kIOReturnNotReady;
    if (ic->ic_state != IEEE80211_S_SCAN ||
        (sc->sc_flags & IWM_FLAG_BGSCAN) != 0)
        return kIOReturnBusy;
    if (apRuntime.stage != kItlApFirmwareResourceIdle)
        return kIOReturnBusy;

    uint64_t generation = 0;
    IOSimpleLockLock(sc->sc_sae_wcl_credential_lock);
    if (sc->sc_sae_bss_loss_recovery_armed &&
        sc->sc_sae_bss_loss_recovery_generation != 0 &&
        sc->sc_sae_wcl_credential_active &&
        !sc->sc_sae_wcl_credential_staged &&
        !sc->sc_sae_wcl_credential_pending &&
        sc->sc_sae_wcl_credential.request_generation ==
            sc->sc_sae_bss_loss_recovery_generation &&
        itl_sae_wcl_credential_is_well_formed(
            &sc->sc_sae_wcl_credential))
        generation = sc->sc_sae_bss_loss_recovery_generation;
    IOSimpleLockUnlock(sc->sc_sae_wcl_credential_lock);
    const bool scanGeneric = generation == 0;

    /* Publish the terminal owner before submitting the abort.  The command
     * response only acknowledges the request; iwm_endscan() promotes the
     * handoff after the separate native scan-complete notification. */
    irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    if (wclScanPhase != ItlIwmWclScanPhase::Idle ||
        apPrimaryStaRecoveryScanAbortPending ||
        apPrimaryStaRecoveryScanYielded) {
        IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
        return kIOReturnBusy;
    }
    apPrimaryStaRecoveryScanGeneration = generation;
    apPrimaryStaRecoveryScanGeneric = scanGeneric;
    apPrimaryStaRecoveryScanAbortPending = true;
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);

    if ((sc->sc_flags & IWM_FLAG_SCANNING) == 0) {
        irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
        if (apPrimaryStaRecoveryScanAbortPending &&
            apPrimaryStaRecoveryScanGeneration == generation &&
            apPrimaryStaRecoveryScanGeneric == scanGeneric) {
            apPrimaryStaRecoveryScanAbortPending = false;
            apPrimaryStaRecoveryScanYielded = true;
        }
        const bool quiescentYielded =
            apPrimaryStaRecoveryScanYielded &&
            apPrimaryStaRecoveryScanGeneration == generation &&
            apPrimaryStaRecoveryScanGeneric == scanGeneric;
        IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
        XYLog("%s: IWM quiescent foreground scan yielded to AP "
              "generation=%llu generic=%u yielded=%u\n",
              DEVNAME(sc), (unsigned long long)generation,
              scanGeneric ? 1U : 0U,
              quiescentYielded ? 1U : 0U);
        return quiescentYielded ? kIOReturnSuccess : kIOReturnNotReady;
    }

    const int abortError =
        isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_UMAC_SCAN) ?
            iwm_umac_scan_abort(sc) : iwm_lmac_scan_abort(sc);
    if (abortError != 0) {
        irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
        if (apPrimaryStaRecoveryScanAbortPending &&
            apPrimaryStaRecoveryScanGeneration == generation &&
            apPrimaryStaRecoveryScanGeneric == scanGeneric) {
            apPrimaryStaRecoveryScanAbortPending = false;
            apPrimaryStaRecoveryScanGeneric = false;
            apPrimaryStaRecoveryScanGeneration = 0;
        }
        const bool terminalWon = apPrimaryStaRecoveryScanYielded &&
            apPrimaryStaRecoveryScanGeneration == generation &&
            apPrimaryStaRecoveryScanGeneric == scanGeneric;
        IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
        return terminalWon ? kIOReturnSuccess : kIOReturnError;
    }

    irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    const bool terminalWon = apPrimaryStaRecoveryScanYielded &&
        apPrimaryStaRecoveryScanGeneration == generation &&
        apPrimaryStaRecoveryScanGeneric == scanGeneric;
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    XYLog("%s: IWM foreground scan abort submitted for AP "
          "generation=%llu generic=%u state=%u flags=0x%x terminal=%u\n",
          DEVNAME(sc), (unsigned long long)generation,
          scanGeneric ? 1U : 0U,
          static_cast<unsigned>(ic->ic_state),
          static_cast<unsigned>(sc->sc_flags),
          static_cast<unsigned>(terminalWon));
    return terminalWon ? kIOReturnSuccess : kIOReturnNotReady;
}

bool ItlIwm::
completePrimaryStaRecoveryScanAPHandoff()
{
    struct iwm_softc *sc = &com;
    struct ieee80211com *ic = &sc->sc_ic;
    if (wclScanLock == NULL)
        return false;

    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    uint64_t generation = 0;
    bool generic = false;
    bool claimed = false;
    if (apPrimaryStaRecoveryScanAbortPending) {
        claimed = true;
        generation = apPrimaryStaRecoveryScanGeneration;
        generic = apPrimaryStaRecoveryScanGeneric;
        apPrimaryStaRecoveryScanAbortPending = false;
    }
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    if (!claimed || (generation == 0 && !generic))
        return false;

    ieee80211_end_scan_controlled(
        &ic->ic_if, IEEE80211_SCAN_COMPLETION_AP_HANDOFF);

    irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    if (apPrimaryStaRecoveryScanGeneration == generation &&
        apPrimaryStaRecoveryScanGeneric == generic)
        apPrimaryStaRecoveryScanYielded = true;
    const bool terminalYielded = apPrimaryStaRecoveryScanYielded &&
        apPrimaryStaRecoveryScanGeneration == generation &&
        apPrimaryStaRecoveryScanGeneric == generic;
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    XYLog("%s: IWM foreground scan terminal yielded to AP generation=%llu "
          "generic=%u state=%u flags=0x%x yielded=%u\n",
          DEVNAME(sc), (unsigned long long)generation,
          generic ? 1U : 0U,
          static_cast<unsigned>(ic->ic_state),
          static_cast<unsigned>(sc->sc_flags),
          static_cast<unsigned>(terminalYielded));
    return true;
}

void ItlIwm::
resumePrimaryStaRecoveryScanAfterAPHandoff()
{
    struct iwm_softc *sc = &com;
    struct ieee80211com *ic = &sc->sc_ic;
    if (wclScanLock == NULL)
        return;
    if (isAPScanFenceActive())
        return;

    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    const bool yielded = apPrimaryStaRecoveryScanYielded;
    const uint64_t generation = yielded ?
        apPrimaryStaRecoveryScanGeneration : 0;
    const bool generic = yielded && apPrimaryStaRecoveryScanGeneric;
    if (yielded) {
        apPrimaryStaRecoveryScanYielded = false;
        apPrimaryStaRecoveryScanGeneric = false;
        apPrimaryStaRecoveryScanGeneration = 0;
    }
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    if (scanCommandReplayPending()) {
        resumeScanCommand();
        return;
    }
    if (generation == 0 && !generic)
        return;
    if (sc->sc_sae_wcl_credential_lock == NULL)
        return;

    bool generationCurrent = false;
    if (generation != 0) {
        IOSimpleLockLock(sc->sc_sae_wcl_credential_lock);
        generationCurrent = sc->sc_sae_bss_loss_recovery_armed &&
            sc->sc_sae_bss_loss_recovery_generation == generation &&
            sc->sc_sae_wcl_credential_active &&
            !sc->sc_sae_wcl_credential_staged &&
            !sc->sc_sae_wcl_credential_pending &&
            sc->sc_sae_wcl_credential.request_generation == generation &&
            itl_sae_wcl_credential_is_well_formed(
                &sc->sc_sae_wcl_credential);
        IOSimpleLockUnlock(sc->sc_sae_wcl_credential_lock);
    }

    if ((!generic && !generationCurrent) ||
        ic->ic_opmode != IEEE80211_M_STA ||
        ic->ic_state != IEEE80211_S_SCAN ||
        (ic->ic_flags & IEEE80211_F_AUTO_JOIN) == 0 ||
        ic->ic_des_esslen != 0 ||
        (ic->ic_if.if_flags & IFF_RUNNING) == 0 ||
        (sc->sc_flags & (IWM_FLAG_SHUTDOWN | IWM_FLAG_HW_ERR)) != 0)
        return;
    if ((sc->sc_flags & (IWM_FLAG_SCANNING | IWM_FLAG_BGSCAN)) != 0)
        return;

    irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    const bool wclOwned = wclScanPhase != ItlIwmWclScanPhase::Idle;
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    if (wclOwned)
        return;

    XYLog("%s: IWM resuming foreground scan after AP handoff "
          "generation=%llu generic=%u\n",
          DEVNAME(sc), (unsigned long long)generation,
          generic ? 1U : 0U);
    ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
}

void ItlIwm::
iwm_ap_csa_timeout(void *arg)
{
    ItlIwm *that = static_cast<ItlIwm *>(arg);
    if (that == NULL)
        return;
    const int s = splnet();
    if (that->apRuntime.csaPending && that->apRuntime.csaCount > 1) {
        that->apRuntime.csaCount--;
        const int countError = itl_ap_beacon_set_csa_count(
            that->apRuntime.beacon,
            that->apRuntime.config.beaconTemplateLength,
            that->apRuntime.csaCount);
        const int beaconError = countError == 0 ?
            that->iwm_ap_send_beacon_template(
                &that->com, &that->apRuntime) : countError;
        if (beaconError == 0) {
            const uint32_t delayMs = MAX(1U, static_cast<uint32_t>(
                (static_cast<uint64_t>(
                    that->apRuntime.config.beaconInterval) *
                 IEEE80211_DUR_TU + 999) / 1000));
            timeout_add_msec(&that->apCsaTimeout, delayMs);
            splx(s);
            return;
        }
    }
    const int error = that->iwm_ap_finish_csa(&that->com, &that->apRuntime);
    if (error != 0)
        XYLog("%s: IWM AP CSA terminal error=%d\n",
              DEVNAME(&that->com), error);
    splx(s);
}

int ItlIwm::
iwm_ap_finish_csa(struct iwm_softc *sc,
                  struct ItlApFirmwareRuntime *runtime)
{
    if (runtime == NULL || !runtime->csaPending ||
        runtime->stage != kItlApFirmwareResourceRunning)
        return EINVAL;
    struct ieee80211_channel *target = iwm_ap_find_channel(
        sc, runtime->csaTargetChannel);
    if (target == NULL)
        return EINVAL;

    const uint16_t oldChannel = runtime->config.channel;
    const uint8_t oldPhyId = runtime->phyId;
    const bool oldSamePhy = runtime->samePhyAsPrimary;
    struct iwm_node *primary = (struct iwm_node *)sc->sc_ic.ic_bss;
    const bool newSamePhy =
        (sc->sc_flags & IWM_FLAG_BINDING_ACTIVE) != 0 &&
        primary != NULL && primary->in_phyctxt != NULL &&
        ieee80211_chan2ieee(&sc->sc_ic, primary->in_phyctxt->channel) ==
            runtime->csaTargetChannel;
    const uint8_t newPhyId = newSamePhy ? primary->in_phyctxt->id : 1;
    bool newBindingAdded = false;

    int error = iwm_ap_binding_cmd(sc, runtime, false);
    if (error != 0)
        goto rollback_beacon;
    runtime->samePhyAsPrimary = newSamePhy;
    runtime->phyId = newPhyId;
    runtime->config.channel = runtime->csaTargetChannel;
    if (!newSamePhy) {
        error = iwm_phy_ctxt_update(sc, &sc->sc_phyctxt[newPhyId],
                                    target, 1, 1, 0);
        if (error != 0)
            goto rollback_context;
    }
    error = iwm_ap_mac_ctxt_cmd(sc, runtime, IWM_FW_CTXT_ACTION_MODIFY);
    if (error != 0)
        goto rollback_context;
    error = iwm_ap_binding_cmd(sc, runtime, true);
    if (error != 0)
        goto rollback_context;
    newBindingAdded = true;
    error = iwm_ap_update_quotas(sc, runtime, true);
    if (error != 0)
        goto rollback_context;
    error = itl_ap_beacon_end_csa(
        runtime->beacon, &runtime->config.beaconTemplateLength,
        static_cast<uint8_t>(runtime->config.channel), true);
    if (error != 0)
        goto rollback_context;
    error = iwm_ap_send_beacon_template(sc, runtime);
    if (error != 0)
        goto rollback_context;
    runtime->csaPending = false;
    runtime->csaTargetChannel = 0;
    runtime->csaCount = 0;
    XYLog("%s: IWM AP CSA complete channel=%u phy=%u shared=%u\n",
          DEVNAME(sc), static_cast<unsigned>(runtime->config.channel),
          static_cast<unsigned>(runtime->phyId),
          static_cast<unsigned>(runtime->samePhyAsPrimary));
    return 0;

rollback_context:
    if (newBindingAdded)
        (void)iwm_ap_binding_cmd(sc, runtime, false);
    runtime->config.channel = oldChannel;
    runtime->phyId = oldPhyId;
    runtime->samePhyAsPrimary = oldSamePhy;
    if (!oldSamePhy) {
        struct ieee80211_channel *old = iwm_ap_find_channel(sc, oldChannel);
        if (old != NULL)
            (void)iwm_phy_ctxt_update(sc, &sc->sc_phyctxt[oldPhyId],
                                      old, 1, 1, 0);
    }
    (void)iwm_ap_mac_ctxt_cmd(sc, runtime, IWM_FW_CTXT_ACTION_MODIFY);
    (void)iwm_ap_binding_cmd(sc, runtime, true);
    (void)iwm_ap_update_quotas(sc, runtime, true);
rollback_beacon:
    (void)itl_ap_beacon_end_csa(
        runtime->beacon, &runtime->config.beaconTemplateLength,
        static_cast<uint8_t>(runtime->csaTargetChannel), false);
    (void)itl_ap_beacon_set_channel(
        runtime->beacon, runtime->config.beaconTemplateLength,
        static_cast<uint8_t>(oldChannel));
    (void)iwm_ap_send_beacon_template(sc, runtime);
    runtime->csaPending = false;
    runtime->csaTargetChannel = 0;
    runtime->csaCount = 0;
    return error != 0 ? error : EIO;
}

IOReturn ItlIwm::
sendAPStationCommand(const struct ItlHalApStationCommand *command)
{
    if (!itl_ap_open_is_running(&apRuntime))
        return kIOReturnNotReady;
    if (command == NULL)
        return kIOReturnBadArgument;
    struct ItlApFirmwareClientRuntime *client =
        itl_ap_firmware_find_client(&apRuntime, command->station);
    if (client == NULL)
        return kIOReturnNotReady;
    if (command->command == kItlHalApStationDisassociate) {
        if (client->timSet) {
            bool changed = false;
            if (itl_ap_power_save_set_tim(
                    &apRuntime, client, false, &changed) == 0 && changed)
                (void)iwm_ap_send_beacon_template(&com, &apRuntime);
        }
        client->clientAssociationPending = false;
        client->clientAuthenticated = false;
        client->clientAssociated = false;
        const int error = iwm_ap_remove_client_sta(
            &com, &apRuntime, client);
        itl_ap_firmware_client_reset(client, true);
        return error == 0 ? kIOReturnSuccess : kIOReturnError;
    }
    if (!client->clientAssociated)
        return kIOReturnNotReady;
    if (command->command == kItlHalApStationUnauthorize) {
        client->clientAuthorized = false;
        return kIOReturnSuccess;
    }
    if (command->command != kItlHalApStationAuthorize)
        return kIOReturnUnsupported;
    if (itl_ap_client_is_secure(&apRuntime) &&
        (!client->clientPairwiseKeyInstalled ||
         !apRuntime.groupKeyInstalled))
        return kIOReturnNotReady;
    client->clientAuthorized = true;
#if __IO80211_TARGET >= __MAC_26_0
    airportItlwmRequestAPTxDequeue(getController());
#endif
    return kIOReturnSuccess;
}

uint32_t ItlIwm::
getAPTxFreeSpace() const
{
    if (!itl_ap_open_is_running(&apRuntime) ||
        apRuntime.multicastQueueId >= IWM_MAX_QUEUES)
        return 0;
    const struct iwm_tx_ring *multicast = &com.txq[apRuntime.multicastQueueId];
    const uint32_t usable = IWM_TX_RING_COUNT - 1;
    const uint32_t multicastFree =
        (com.qfullmsk & (1U << multicast->qid)) != 0 ||
        multicast->queued > IWM_TX_RING_HIMARK ? 0 :
        (multicast->queued < usable ? usable - multicast->queued : 0);
    uint32_t freeSpace = multicastFree;
    bool found = false;
    for (size_t index = 0; index < kItlApFirmwareMaxClients; index++) {
        const struct ItlApFirmwareClientRuntime *client =
            &apRuntime.clients[index];
        if (!client->inUse || !client->clientStationInstalled ||
            !client->clientAssociated)
            continue;
        if (client->queueId >= IWM_MAX_QUEUES)
            return 0;
        found = true;
        const struct iwm_tx_ring *ring = &com.txq[client->queueId];
        const uint32_t clientFree =
            (com.qfullmsk & (1U << ring->qid)) != 0 ||
            ring->queued > IWM_TX_RING_HIMARK ? 0 :
            (ring->queued < usable ? usable - ring->queued : 0);
        freeSpace = MIN(freeSpace, clientFree);
    }
    return found ? freeSpace : 0;
}

extern "C" bool
airportItlwmQueryIwmAPTxFreeSpace(ItlHalService *service, uint32_t *freeSpace)
{
    ItlIwm *that = OSDynamicCast(ItlIwm, service);
    if (that == NULL || freeSpace == NULL)
        return false;
    *freeSpace = that->getAPTxFreeSpace();
    return true;
}

extern "C" bool
airportItlwmHandoffIwmPrimaryStaRecoveryScanToAP(
    ItlHalService *service, IOReturn *result)
{
    ItlIwm *that = OSDynamicCast(ItlIwm, service);
    if (that == NULL)
        return false;
    if (result == NULL)
        return true;
    *result = that->handoffPrimaryStaRecoveryScanToAP();
    return true;
}

struct ieee80211com *ItlIwm::
get80211Controller()
{
    return &com.sc_ic;
}

ItlDriverInfo *ItlIwm::
getDriverInfo()
{
    return this;
}

ItlDriverController *ItlIwm::
getDriverController()
{
    return this;
}

void ItlIwm::
clearScanningFlags()
{
    com.sc_flags &= ~(IWM_FLAG_SCANNING | IWM_FLAG_BGSCAN);
    if (__atomic_exchange_n(&com.sc_scan_abort_pending, 0,
                            __ATOMIC_ACQ_REL) != 0)
        wakeupOn(&com.sc_scan_abort_pending);
}

static void
iwm_wcl_scan_ticket_reset_locked(ItlIwm *that)
{
    that->wclScanPhase = ItlIwmWclScanPhase::Idle;
    that->wclScanUpperGeneration = 0;
    that->wclScanBackendGeneration = 0;
    that->wclScanPublicationInvalidated = false;
}

static uint32_t
iwm_wcl_scan_next_backend_generation_locked(ItlIwm *that)
{
    ++that->wclScanNextBackendGeneration;
    if (that->wclScanNextBackendGeneration == 0)
        ++that->wclScanNextBackendGeneration;
    return that->wclScanNextBackendGeneration;
}

static void
iwm_wcl_scan_publish_started(ItlIwm *that, uint64_t generation,
                             uint32_t backendGeneration)
{
    struct ieee80211com *ic = &that->com.sc_ic;
    if (ic->ic_event_handler == NULL || generation == 0 ||
        backendGeneration == 0)
        return;

    struct ieee80211_wcl_scan_started started;
    explicit_bzero(&started, sizeof(started));
    started.generation = generation;
    started.backend_generation = backendGeneration;
    (*ic->ic_event_handler)(ic, IEEE80211_EVT_WCL_SCAN_STARTED, &started);
    explicit_bzero(&started, sizeof(started));
}

static void
iwm_wcl_scan_publish_start_rejected(ItlIwm *that, uint64_t generation,
                                    uint32_t backendGeneration)
{
    struct ieee80211com *ic = &that->com.sc_ic;
    if (ic->ic_event_handler == NULL || generation == 0)
        return;

    struct ieee80211_wcl_scan_start_rejected rejected;
    explicit_bzero(&rejected, sizeof(rejected));
    rejected.generation = generation;
    rejected.backend_generation = backendGeneration;
    (*ic->ic_event_handler)(ic, IEEE80211_EVT_WCL_SCAN_START_REJECTED,
                            &rejected);
    explicit_bzero(&rejected, sizeof(rejected));
}

void ItlIwm::
publishWclScanTerminal(const ItlIwmWclScanTerminal *terminal,
                       uint32_t status)
{
    struct ieee80211com *ic = &com.sc_ic;
    if (terminal == NULL || !terminal->publish ||
        terminal->upperGeneration == 0 ||
        terminal->backendGeneration == 0 ||
        ic->ic_event_handler == NULL)
        return;

    struct ieee80211_wcl_scan_terminal event;
    explicit_bzero(&event, sizeof(event));
    event.generation = terminal->upperGeneration;
    event.backend_generation = terminal->backendGeneration;
    event.status = status;
    (*ic->ic_event_handler)(ic, IEEE80211_EVT_WCL_SCAN_TERMINAL, &event);
    explicit_bzero(&event, sizeof(event));
}

IOReturn ItlIwm::
beginWclInitialScan(uint64_t generation, uint32_t *outBackendGeneration)
{
    struct ieee80211com *ic = &com.sc_ic;
    bool startNow = false;

    if (generation == 0 || outBackendGeneration == NULL)
        return kIOReturnBadArgument;
    *outBackendGeneration = 0;
    if (isAPScanFenceActive())
        return kIOReturnBusy;
    if (wclScanLock == NULL || ic->ic_state != IEEE80211_S_SCAN ||
        ic->ic_opmode != IEEE80211_M_STA ||
        (ic->ic_if.if_flags & IFF_RUNNING) == 0 ||
        ic->ic_mgt_timer != 0 || ic->ic_des_esslen != 0)
        return kIOReturnBusy;

    /*
     * AppleBCMWLANScanAdapter accepts a valid WCL carrier independently of
     * legacy background-scan state.  Use wclScanPhase/IWM_FLAG_SCANNING as
     * the physical overlap owner below; IEEE80211_F_BGSCAN can remain set
     * briefly after link loss even though no lower background owner exists.
     */
    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    if (wclScanPhase != ItlIwmWclScanPhase::Idle ||
        wclSaeAdmissionReserved || !scanCommand.open || scanCommand.apSerial != 0) {
        IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
        return kIOReturnBusy;
    }
    wclScanUpperGeneration = generation;
    wclScanBackendGeneration = 0;
    wclScanPublicationInvalidated = false;
    if (scanCommand.live() ||
        (com.sc_flags & IWM_FLAG_SCANNING) != 0) {
        /*
         * Do not reuse the boot scan's terminal or cache.  Its exact terminal
         * only advances this ticket to InitialStarting; iwm_endscan() then
         * performs controlled cleanup and queues one fresh IWM command.
         */
        wclScanPhase = ItlIwmWclScanPhase::InitialQueued;
    } else {
        wclScanPhase = ItlIwmWclScanPhase::InitialStarting;
        startNow = true;
    }
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);

    if (startNow)
        ieee80211_begin_scan(&ic->ic_if);
    return kIOReturnSuccess;
}

IOReturn ItlIwm::
beginWclBackgroundScan(uint64_t generation,
                       uint32_t *outBackendGeneration)
{
    struct ieee80211com *ic = &com.sc_ic;
    bool rejectedBeforeSubmit = false;

    if (generation == 0 || outBackendGeneration == NULL)
        return kIOReturnBadArgument;
    *outBackendGeneration = 0;
    if (isAPScanFenceActive())
        return kIOReturnBusy;
    if (wclScanLock == NULL || ic->ic_state != IEEE80211_S_RUN ||
        ic->ic_bss == NULL || ic->ic_mgt_timer != 0 ||
        (ic->ic_flags & IEEE80211_F_BGSCAN) != 0 ||
        (com.sc_flags & (IWM_FLAG_SCANNING | IWM_FLAG_BGSCAN)) != 0 ||
        ((ic->ic_flags & IEEE80211_F_RSNON) != 0 &&
         !ic->ic_bss->ni_port_valid))
        return kIOReturnBusy;

    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    if (wclScanPhase != ItlIwmWclScanPhase::Idle ||
        wclSaeAdmissionReserved || scanCommand.live() || !scanCommand.open ||
        scanCommand.apSerial != 0) {
        IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
        return kIOReturnBusy;
    }
    wclScanPhase = ItlIwmWclScanPhase::BackgroundStarting;
    wclScanUpperGeneration = generation;
    wclScanBackendGeneration = 0;
    wclScanPublicationInvalidated = false;
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);

    ieee80211_begin_cache_bgscan(&ic->ic_if);

    irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    if (wclScanPhase == ItlIwmWclScanPhase::BackgroundActive &&
        wclScanUpperGeneration == generation) {
        *outBackendGeneration = wclScanBackendGeneration;
        IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
        return kIOReturnSuccess;
    }
    /*
     * A very short scan may already have published its tagged terminal.  In
     * that case the upper reducer owns a Pending completion and will recover
     * the backend generation from the terminal mailbox.
     */
    if (wclScanPhase == ItlIwmWclScanPhase::Idle) {
        IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
        return kIOReturnSuccess;
    }
    if (wclScanPhase == ItlIwmWclScanPhase::BackgroundStarting &&
        wclScanUpperGeneration == generation) {
        iwm_wcl_scan_ticket_reset_locked(this);
        rejectedBeforeSubmit = true;
    }
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    if (rejectedBeforeSubmit) {
        /* ieee80211_begin_cache_bgscan() has a void result and trusts a zero
         * ic_bgscan_start return.  If the IWM callback did not cross its
         * post-submit edge, retract those generic policy bits as part of the
         * same rejected request. */
        ic->ic_flags &= ~(IEEE80211_F_BGSCAN |
                          IEEE80211_F_DISABLE_BG_AUTO_CONNECT);
    }
    return kIOReturnBusy;
}

void ItlIwm::
noteWclInitialScanCommandStarted(uint64_t serial)
{
    uint64_t generation = 0;
    uint32_t backendGeneration = 0;

    if (wclScanLock == NULL)
        return;
    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    if (scanCommand.current(serial, com.sc_generation) &&
        scanCommandPolicy.plan.active != 0 &&
        scanCommandPolicy.plan.generation == wclScanUpperGeneration &&
        wclScanPhase == ItlIwmWclScanPhase::InitialStarting) {
        backendGeneration =
            iwm_wcl_scan_next_backend_generation_locked(this);
        wclScanBackendGeneration = backendGeneration;
        wclScanPhase = ItlIwmWclScanPhase::InitialActive;
        generation = wclScanUpperGeneration;
    }
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    iwm_wcl_scan_publish_started(this, generation, backendGeneration);
}

void ItlIwm::
noteWclInitialScanCommandRejected(uint64_t serial, uint64_t requiredGeneration)
{
    uint64_t generation = 0;
    uint32_t backendGeneration = 0;

    if (wclScanLock == NULL)
        return;
    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    const uint64_t expected = serial == 0 ? requiredGeneration :
        scanCommandPolicy.plan.generation;
    if (expected != 0 && expected == wclScanUpperGeneration &&
        (serial == 0 || scanCommand.current(serial, com.sc_generation)) &&
        wclScanPhase == ItlIwmWclScanPhase::InitialStarting) {
        generation = wclScanUpperGeneration;
        backendGeneration = wclScanBackendGeneration;
        iwm_wcl_scan_ticket_reset_locked(this);
    }
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    iwm_wcl_scan_publish_start_rejected(this, generation,
                                        backendGeneration);
}

void ItlIwm::
noteWclBackgroundScanCommandStarted(uint64_t serial)
{
    if (wclScanLock == NULL)
        return;
    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    if (scanCommand.current(serial, com.sc_generation) &&
        scanCommandPolicy.plan.active != 0 &&
        scanCommandPolicy.plan.generation == wclScanUpperGeneration &&
        wclScanPhase == ItlIwmWclScanPhase::BackgroundStarting) {
        wclScanBackendGeneration =
            iwm_wcl_scan_next_backend_generation_locked(this);
        wclScanPhase = ItlIwmWclScanPhase::BackgroundActive;
        __atomic_store_n(&com.sc_ic.ic_wcl_scan_active, 1,
                         __ATOMIC_RELEASE);
    }
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
}

void ItlIwm::
noteWclScanRadioReady(uint64_t serial)
{
    bool publish = false;

    if (wclScanLock == NULL)
        return;
    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    if (scanCommand.current(serial, com.sc_generation) && wclScanNeedsReopen) {
        wclScanNeedsReopen = false;
        publish = true;
    }
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    if (publish && com.sc_ic.ic_event_handler != NULL)
        (*com.sc_ic.ic_event_handler)(&com.sc_ic,
                                     IEEE80211_EVT_WCL_SCAN_REOPENED, NULL);
}

ItlIwmWclScanTerminalKind ItlIwm::
claimWclScanTerminal(ItlIwmWclScanTerminal *terminal, bool leafHeld)
{
    if (terminal == NULL || wclScanLock == NULL)
        return ItlIwmWclScanTerminalKind::None;
    explicit_bzero(terminal, sizeof(*terminal));

    IOInterruptState irq = 0;
    if (!leafHeld)
        irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    if (wclScanPhase == ItlIwmWclScanPhase::InitialQueued) {
        if (wclScanPublicationInvalidated) {
            iwm_wcl_scan_ticket_reset_locked(this);
            if (!leafHeld)
                IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
            return ItlIwmWclScanTerminalKind::None;
        }
        wclScanPhase = ItlIwmWclScanPhase::InitialStarting;
        if (!leafHeld)
            IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
        return ItlIwmWclScanTerminalKind::ReplayInitial;
    }

    ItlIwmWclScanTerminalKind kind =
        ItlIwmWclScanTerminalKind::None;
    if (wclScanPhase == ItlIwmWclScanPhase::InitialActive)
        kind = ItlIwmWclScanTerminalKind::Foreground;
    else if (wclScanPhase == ItlIwmWclScanPhase::BackgroundActive)
        kind = ItlIwmWclScanTerminalKind::Background;
    if (kind != ItlIwmWclScanTerminalKind::None) {
        terminal->upperGeneration = wclScanUpperGeneration;
        terminal->backendGeneration = wclScanBackendGeneration;
        terminal->publish = !wclScanPublicationInvalidated;
        iwm_wcl_scan_ticket_reset_locked(this);
    }
    if (!leafHeld)
        IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    return kind;
}

IOReturn ItlIwm::
abortWclBackgroundScan(uint64_t generation)
{
    struct ieee80211com *ic = &com.sc_ic;
    ItlIwmWclScanTerminal terminal;
    ItlIwmWclScanTerminalKind kind =
        ItlIwmWclScanTerminalKind::None;

    if (generation == 0 || wclScanLock == NULL)
        return kIOReturnBadArgument;

    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    const bool active = wclScanUpperGeneration == generation &&
        (wclScanPhase == ItlIwmWclScanPhase::InitialActive ||
         wclScanPhase == ItlIwmWclScanPhase::BackgroundActive);
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    if (!active)
        return kIOReturnNotReady;
    if (iwm_scan_abort(&com) != 0)
        return kIOReturnError;

    explicit_bzero(&terminal, sizeof(terminal));
    irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    if (wclScanUpperGeneration == generation) {
        if (wclScanPhase == ItlIwmWclScanPhase::InitialActive)
            kind = ItlIwmWclScanTerminalKind::Foreground;
        else if (wclScanPhase == ItlIwmWclScanPhase::BackgroundActive)
            kind = ItlIwmWclScanTerminalKind::Background;
        if (kind != ItlIwmWclScanTerminalKind::None) {
            terminal.upperGeneration = wclScanUpperGeneration;
            terminal.backendGeneration = wclScanBackendGeneration;
            terminal.publish = !wclScanPublicationInvalidated;
            iwm_wcl_scan_ticket_reset_locked(this);
        }
    }
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);

    if (kind == ItlIwmWclScanTerminalKind::Foreground) {
        ieee80211_end_scan_controlled(
            &ic->ic_if, IEEE80211_SCAN_COMPLETION_WCL_FOREGROUND);
    } else if (kind == ItlIwmWclScanTerminalKind::Background) {
        __atomic_store_n(&ic->ic_wcl_scan_suppress_scan_done_once, 1,
                         __ATOMIC_RELEASE);
        ieee80211_end_scan(&ic->ic_if);
        __atomic_store_n(&ic->ic_wcl_scan_active, 0, __ATOMIC_RELEASE);
    }
    publishWclScanTerminal(
        &terminal, IEEE80211_WCL_SCAN_TERMINAL_STATUS_ABORTED);
    return kIOReturnSuccess;
}

void ItlIwm::
invalidateWclBackgroundScan()
{
    if (wclScanLock == NULL)
        return;
    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    if (wclScanPhase != ItlIwmWclScanPhase::Idle)
        wclScanPublicationInvalidated = true;
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
}

uint64_t ItlIwm::
scanCommandResetEpoch()
{
    if (wclScanLock == NULL)
        return UINT64_MAX;
    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    const uint64_t epoch = scanCommand.resetEpoch;
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    return epoch;
}

bool ItlIwm::
reopenScanCommands(uint64_t resetEpoch, uint32_t hardwareGeneration)
{
    if (wclScanLock == NULL)
        return false;
    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    const bool reopened =
        static_cast<uint32_t>(com.sc_generation) == hardwareGeneration &&
        (com.sc_flags & IWM_FLAG_SHUTDOWN) == 0 &&
        scanCommand.reopen(resetEpoch, hardwareGeneration);
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    return reopened;
}

int ItlIwm::
reserveScanCommand(bool background, bool umac, uint64_t *serial,
                   const ItlStateTransitionRequest *request, uint64_t reassocSerial)
{
    if (serial == NULL)
        return EINVAL;
    *serial = 0;
    struct ieee80211com *ic = &com.sc_ic;
    IOSimpleLock *ownerLock = ic->ic_pae_selected_bss_lock;
    if (wclScanLock == NULL || ownerLock == NULL)
        return ENXIO;
    if (background ? request != NULL :
        (request == NULL || request->state != IEEE80211_S_SCAN))
        return EINVAL;

    const uint32_t homeAwayMs = background ?
        ItlScanCommandPolicy::homeAwayTime() : request->scanHomeAwayMs;
    ItlScanCommandPolicy policy = {};
    IOInterruptState ownerIrq = IOSimpleLockLockDisableInterrupt(ownerLock);
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    int error = 0;
    const uint64_t wclGeneration = background ?
        (wclScanPhase == ItlIwmWclScanPhase::BackgroundStarting ?
            wclScanUpperGeneration : 0) : request->scanGeneration;
    if (!scanCommand.open || (com.sc_flags & IWM_FLAG_SHUTDOWN) != 0)
        error = ENXIO;
    else if (!background && !stateTransition.current(*request, com.sc_generation))
        error = ECANCELED;
    else if (wclGeneration != 0 &&
        (wclScanUpperGeneration != wclGeneration || wclScanPublicationInvalidated ||
         wclScanPhase != (background ? ItlIwmWclScanPhase::BackgroundStarting :
                                      ItlIwmWclScanPhase::InitialStarting)))
        error = ECANCELED;
    else if (reassocSerial != 0 && (!background || wclGeneration != 0 ||
        !ic->ic_wcl_reassoc_owner_active ||
        ic->ic_wcl_reassoc_owner_serial != reassocSerial))
        error = ECANCELED;
    else
        error = ItlScanCommandPolicy::captureOwnedLocked(ic, wclGeneration,
                                                         request, &policy);
    if (error == 0) {
        *serial = scanCommand.reserve(com.sc_generation, policy.joinGeneration,
                                      umac, background, 0, reassocSerial);
        if (*serial == 0)
            error = EBUSY;
        else {
            policy.homeAwayMs = homeAwayMs;
            policy.reassocSerial = reassocSerial;
            scanCommandPolicy = policy;
        }
    }
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    IOSimpleLockUnlockEnableInterrupt(ownerLock, ownerIrq);
    return error;
}

/* Both selected-BSS and scan leaves are held, in that order (IWX may hold
 * its command-queue leaf outside them). Abort deliberately does not use this
 * latest-intent check: an obsolete physical command still needs retirement. */
bool ItlIwm::
scanCommandOwnerCurrentLocked(uint64_t serial, uint32_t generation) const
{
    if (!scanCommand.current(serial, generation) ||
        !scanCommandPolicy.currentLocked(&com.sc_ic))
        return false;
    if (scanCommandPolicy.stateSerial != 0 &&
        (stateTransition.request.serial != scanCommandPolicy.stateSerial ||
         stateTransition.request.hardwareGeneration != generation))
        return false;
    return scanCommandPolicy.plan.active == 0 ||
        (!wclScanPublicationInvalidated &&
         wclScanUpperGeneration == scanCommandPolicy.plan.generation);
}

bool ItlIwm::
copyScanCommandPolicy(uint64_t serial, ItlScanCommandPolicy *policy)
{
    if (policy == NULL)
        return false;
    *policy = ItlScanCommandPolicy{};
    IOSimpleLock *ownerLock = com.sc_ic.ic_pae_selected_bss_lock;
    if (wclScanLock == NULL || ownerLock == NULL)
        return false;
    IOInterruptState ownerIrq = IOSimpleLockLockDisableInterrupt(ownerLock);
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    const bool current = scanCommandOwnerCurrentLocked(serial, com.sc_generation);
    if (current)
        *policy = scanCommandPolicy;
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    IOSimpleLockUnlockEnableInterrupt(ownerLock, ownerIrq);
    return current;
}

void ItlIwm::
rejectScanCommand(uint64_t serial)
{
    if (wclScanLock == NULL || serial == 0)
        return;
    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    bool reset = false;
    if (scanCommand.rejectUnsubmitted(serial, com.sc_generation))
        scanCommandPolicy = ItlScanCommandPolicy{};
    else
        reset = scanCommand.quarantine(serial, com.sc_generation);
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);

    /* A command response timeout cannot retire a doorbelled scan. The
     * existing reset worker owns firmware erasure and admission reopening. */
    if (reset)
        task_add(systq, &com.init_task);
}

bool ItlIwm::
claimScanCommandTerminal(uint64_t serial,
    ItlScanCommandTerminal *physical, ItlIwmWclScanTerminal *terminal,
    ItlIwmWclScanTerminalKind *kind)
{
    if (wclScanLock == NULL || physical == NULL || terminal == NULL ||
        kind == NULL)
        return false;
    *physical = ItlScanCommandTerminal{};
    *terminal = ItlIwmWclScanTerminal{};
    *kind = ItlIwmWclScanTerminalKind::None;
    lockTsleep();
    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    if (!scanCommand.claimTerminal(serial, com.sc_generation, physical)) {
        IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
        unlockTsleep();
        return false;
    }
    /* Retire only the completed physical owner before any callback is
     * allowed to reserve and publish its replacement. */
    const bool wake = scanCommandAbortSerial == serial;
    if (wake) {
        scanCommandAbortSerial = 0;
        __atomic_store_n(&com.sc_scan_abort_pending, 0, __ATOMIC_RELEASE);
        physical->stopping = true;
    }
    com.sc_flags &= ~(IWM_FLAG_SCANNING | IWM_FLAG_BGSCAN);
    if (!physical->stopping)
        *kind = claimWclScanTerminal(terminal, true);
    if (*kind == ItlIwmWclScanTerminalKind::Background)
        __atomic_store_n(&com.sc_ic.ic_wcl_scan_active, 0, __ATOMIC_RELEASE);
    if (!physical->stopping && physical->background &&
        (physical->aborted || stateTransition.stage == ItlStateTransitionLease::Stage::Deferred))
        com.sc_ic.ic_flags &= ~(IEEE80211_F_BGSCAN |
                               IEEE80211_F_DISABLE_BG_AUTO_CONNECT);
    scanCommandPolicy = ItlScanCommandPolicy{};
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    if (wake)
        wakeupOn(&com.sc_scan_abort_pending);
    unlockTsleep();
    return true;
}

bool ItlIwm::
activateScanCommand(uint64_t serial, bool background)
{
    if (wclScanLock == NULL)
        return false;
    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    const bool current = scanCommand.current(serial, com.sc_generation) &&
        scanCommand.submitted && scanCommand.command.background == background;
    if (current)
        com.sc_flags |= background ? IWM_FLAG_BGSCAN : IWM_FLAG_SCANNING;
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    return current;
}

bool ItlIwm::
scanCommandCurrent(uint64_t serial)
{
    if (wclScanLock == NULL)
        return false;
    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    const bool current = scanCommand.current(serial, com.sc_generation);
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    return current;
}

uint64_t ItlIwm::
reserveAPScanCommand()
{
    if (wclScanLock == NULL)
        return 0;
    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    const uint64_t serial =
        wclScanPhase == ItlIwmWclScanPhase::Idle &&
        (com.sc_flags & (IWM_FLAG_SHUTDOWN |
                         IWM_FLAG_SCANNING | IWM_FLAG_BGSCAN)) == 0 ?
        scanCommand.reserveAP(com.sc_generation) : 0;
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    return serial;
}

uint64_t ItlIwm::
currentAPScanCommand() const
{
    if (wclScanLock == NULL)
        return 0;
    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    const uint64_t serial = scanCommand.apSerial;
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    return serial;
}

void ItlIwm::
finishAPScanCommand(uint64_t serial, bool quiescent)
{
    if (wclScanLock == NULL || serial == 0)
        return;
    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    bool reset = false;
    if (quiescent)
        (void)scanCommand.releaseAP(serial, com.sc_generation);
    else
        reset = scanCommand.quarantineAP(serial, com.sc_generation);
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    if (reset)
        task_add(systq, &com.init_task);
}

bool ItlIwm::
scanCommandBackgroundPending()
{
    if (wclScanLock == NULL)
        return false;
    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    const bool pending = scanCommand.live() && scanCommand.command.background;
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    return pending;
}

bool ItlIwm::
deferScanCommand(const ItlStateTransitionRequest &request, bool includeBackground)
{
    struct ieee80211com *ic = &com.sc_ic;
    IOSimpleLock *ownerLock = ic->ic_pae_selected_bss_lock;
    if (wclScanLock == NULL || ownerLock == NULL)
        return false;
    const bool apFenced = isAPScanFenceActive();
    IOInterruptState ownerIrq = IOSimpleLockLockDisableInterrupt(ownerLock);
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    const bool current = stateTransitionSource != NULL &&
        request.state == IEEE80211_S_SCAN &&
        stateTransition.current(request, com.sc_generation) &&
        request.identity.equals(ItlScanCommandPolicy::identityLocked(ic));
    const bool occupied = current && scanCommand.open &&
        (com.sc_flags & IWM_FLAG_SHUTDOWN) == 0 &&
        ((scanCommand.live() &&
          (includeBackground || !scanCommand.command.background)) ||
         scanCommand.apSerial != 0 || apFenced);
    const bool deferred = occupied &&
        stateTransition.defer(request, com.sc_generation);
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    IOSimpleLockUnlockEnableInterrupt(ownerLock, ownerIrq);

    /* The release edge may have preceded publication of this waiter. A
     * level check queues at most one exact Deferred -> Queued transition;
     * it neither spins nor invokes common scan preparation a second time. */
    if (deferred)
        resumeScanCommand();
    return deferred;
}

bool ItlIwm::
scanCommandReplayPending()
{
    if (wclScanLock == NULL)
        return false;
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    const bool pending = stateTransition.stage == ItlStateTransitionLease::Stage::Deferred &&
        stateTransition.request.state == IEEE80211_S_SCAN;
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    return pending;
}

void ItlIwm::
resumeScanCommand()
{
    struct ieee80211com *ic = &com.sc_ic;
    IOSimpleLock *ownerLock = ic->ic_pae_selected_bss_lock;
    if (wclScanLock == NULL || ownerLock == NULL || isAPScanFenceActive() ||
        !iwm_sae_tx_lifecycle_enter(&com, true))
        return;

    IOInterruptState ownerIrq = IOSimpleLockLockDisableInterrupt(ownerLock);
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    bool queued = false;
    if (stateTransition.stage == ItlStateTransitionLease::Stage::Deferred &&
        stateTransition.deferredKind == ItlStateTransitionLease::DeferredKind::Scan) {
        const ItlStateTransitionRequest &request = stateTransition.request;
        ItlScanCommandPolicy policy = {};
        const bool current = stateTransitionSource != NULL && scanCommand.open &&
            (com.sc_flags & IWM_FLAG_SHUTDOWN) == 0 &&
            request.hardwareGeneration == static_cast<uint32_t>(com.sc_generation) &&
            request.state == IEEE80211_S_SCAN &&
            (ic->ic_if.if_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING) &&
            (request.scanGeneration == 0 ||
             (wclScanPhase == ItlIwmWclScanPhase::InitialStarting &&
              !wclScanPublicationInvalidated &&
              request.scanGeneration == wclScanUpperGeneration)) &&
            ItlScanCommandPolicy::captureOwnedLocked(ic, request.scanGeneration,
                                                       &request, &policy) == 0;
        if (!current)
            stateTransition.invalidate();
        else if (!scanCommand.live() && scanCommand.apSerial == 0)
            queued = stateTransition.resume(com.sc_generation);
    }
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    IOSimpleLockUnlockEnableInterrupt(ownerLock, ownerIrq);
    if (queued)
        iwm_add_task(&com, com.sc_nswq, &com.newstate_task);
    iwm_sae_tx_lifecycle_leave(&com);
}

bool ItlIwm::
noteStateTransitionProgress(ItlStateTransitionRequest *request, uint8_t step)
{
    IOSimpleLock *ownerLock = com.sc_ic.ic_pae_selected_bss_lock;
    if (request == NULL || wclScanLock == NULL || ownerLock == NULL)
        return false;
    IOInterruptState ownerIrq = IOSimpleLockLockDisableInterrupt(ownerLock);
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    const bool current = stateTransitionSource != NULL && scanCommand.open &&
        (com.sc_flags & IWM_FLAG_SHUTDOWN) == 0 &&
        request->identity.equals(ItlScanCommandPolicy::identityLocked(&com.sc_ic)) &&
        stateTransition.completeLowerStep(request, com.sc_generation, step);
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    IOSimpleLockUnlockEnableInterrupt(ownerLock, ownerIrq);
    return current;
}

int ItlIwm::
reserveScanCommandAbort(bool wait, uint64_t *serial, bool backgroundOnly)
{
    if (serial == NULL)
        return EINVAL;
    *serial = 0;
    if (wclScanLock == NULL)
        return ENXIO;
    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    if (!scanCommand.open) {
        IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
        return ENXIO;
    }
    if (!scanCommand.live() ||
        (backgroundOnly && !scanCommand.command.background)) {
        IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
        return 0;
    }
    /* The original sender must publish readiness before another caller
     * waits for its terminal. A reserved/unready census is busy, not absent. */
    if (backgroundOnly && !scanCommand.upperReady) {
        IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
        return EBUSY;
    }
    const uint64_t current = scanCommand.command.serial;
    if (!scanCommand.beginAbort(current, com.sc_generation)) {
        IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
        return EBUSY;
    }
    *serial = current;
    if (wait) {
        scanCommandAbortSerial = current;
        __atomic_store_n(&com.sc_scan_abort_pending, 1, __ATOMIC_RELEASE);
    }
    const bool complete = scanCommand.terminalSeen && scanCommand.upperReady;
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    if (complete)
        iwm_endscan(&com, current);
    return 0;
}

int ItlIwm::
waitScanCommandAbort(uint64_t serial, uint32_t generation)
{
    int error = 0;
    lockTsleep();
    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    bool current = scanCommand.open &&
        static_cast<uint32_t>(com.sc_generation) == generation;
    bool pending = current && scanCommandAbortSerial == serial;
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    if (pending)
        error = tsleep_nsec_locked(&com.sc_scan_abort_pending, 0,
                                   "scan-abort", SEC_TO_NSEC(1));
    irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    current = scanCommand.open &&
        static_cast<uint32_t>(com.sc_generation) == generation;
    pending = current && scanCommandAbortSerial == serial;
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    unlockTsleep();
    if (!current)
        return ENXIO;
    if (!pending)
        return 0;

    /* Timeout never clears another command's waiter or reuses a UID whose
     * physical terminal was not received. Hardware reset owns that erasure. */
    rejectScanCommand(serial);
    return error != 0 ? error : ETIMEDOUT;
}

bool ItlIwm::
readyScanCommand(uint64_t serial)
{
    if (wclScanLock == NULL)
        return false;
    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    const bool accepted = scanCommand.ready(serial, com.sc_generation);
    const bool complete = accepted && scanCommand.terminalSeen;
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    if (complete)
        iwm_endscan(&com, serial);
    return accepted;
}

void ItlIwm::
noteScanCommandTerminal(bool umac, uint32_t uid, bool aborted)
{
    if (wclScanLock == NULL)
        return;
    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    const bool complete = scanCommand.noteTerminal(
        com.sc_generation, umac, uid, aborted) && scanCommand.upperReady;
    const uint64_t serial = complete ? scanCommand.command.serial : 0;
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    if (complete)
        iwm_endscan(&com, serial);
}

bool ItlIwm::
initStateTransitions()
{
    IOWorkLoop *workloop = getMainWorkLoop();
    if (wclScanLock == NULL || workloop == NULL)
        return false;
    IOInterruptEventSource *source = IOInterruptEventSource::interruptEventSource(
        this, &ItlIwm::stateTransitionEvent);
    if (source == NULL)
        return false;
    if (workloop->addEventSource(source) != kIOReturnSuccess) {
        source->release();
        return false;
    }
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    stateTransitionSource = source;
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    source->enable();
    return true;
}

void ItlIwm::
shutdownStateTransitions()
{
    if (wclScanLock == NULL)
        return;
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    IOInterruptEventSource *source = stateTransitionSource;
    stateTransitionSource = NULL;
    stateTransition.invalidate();
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    if (source != NULL) {
        source->disable();
        getMainWorkLoop()->removeEventSource(source);
        source->release();
    }
}

int ItlIwm::
prepareStateTransition(int state, int argument, ItlStateTransitionRequest *request)
{
    if (request == NULL)
        return EINVAL;
    *request = ItlStateTransitionRequest{};
    IOSimpleLock *ownerLock = com.sc_ic.ic_pae_selected_bss_lock;
    if (wclScanLock == NULL || ownerLock == NULL)
        return ENXIO;
    const uint32_t homeAwayMs = state == IEEE80211_S_SCAN ?
        ItlScanCommandPolicy::homeAwayTime() : 0;
    IOInterruptState ownerIrq = IOSimpleLockLockDisableInterrupt(ownerLock);
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    const ItlStateTransitionIdentity identity =
        ItlScanCommandPolicy::identityLocked(&com.sc_ic);
    ItlStateTransitionRequest scanFacts = {};
    scanFacts.identity = identity;
    const uint64_t scanGeneration = state == IEEE80211_S_SCAN &&
        wclScanPhase == ItlIwmWclScanPhase::InitialStarting &&
        !wclScanPublicationInvalidated ? wclScanUpperGeneration : 0;
    const bool validScan = state != IEEE80211_S_SCAN ||
        ItlScanCommandPolicy::captureIngressLocked(&com.sc_ic, scanGeneration,
                                                  &scanFacts);
    int error = ENXIO;
    if (stateTransitionSource != NULL && scanCommand.open &&
        (com.sc_flags & IWM_FLAG_SHUTDOWN) == 0) {
        if (!validScan)
            error = EINVAL;
        else if (state != IEEE80211_S_SCAN && state != IEEE80211_S_AUTH &&
            stateTransition.duplicate(com.sc_generation, state, argument, identity))
            error = EALREADY;
        else if (stateTransition.prepare(com.sc_generation, state, argument,
                                          identity, request)) {
            request->scanGeneration = scanFacts.scanGeneration;
            request->scanJoinGeneration = scanFacts.scanJoinGeneration;
            request->scanHomeAwayMs = homeAwayMs;
            request->scanSsidLength = scanFacts.scanSsidLength;
            memcpy(request->scanSsid, scanFacts.scanSsid, sizeof(request->scanSsid));
            stateTransition.request = *request;
            /* Compatibility/debug fields are not the queued worker's input. */
            com.ns_nstate = (enum ieee80211_state)state;
            com.ns_arg = argument;
            error = 0;
        } else
            error = EOVERFLOW;
    }
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    IOSimpleLockUnlockEnableInterrupt(ownerLock, ownerIrq);
    return error;
}

bool ItlIwm::
stateTransitionCurrent(const ItlStateTransitionRequest &request)
{
    ItlStateTransitionIdentity identity = {};
    if (wclScanLock == NULL ||
        !ieee80211_wcl_join_state_identity(&com.sc_ic, &identity.joinSequence,
            &identity.joinGeneration, &identity.associationEpoch) ||
        !request.identity.equals(identity))
        return false;
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    const bool current = stateTransitionSource != NULL && scanCommand.open &&
        (com.sc_flags & IWM_FLAG_SHUTDOWN) == 0 &&
        stateTransition.current(request, com.sc_generation);
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    return current;
}

bool ItlIwm::
primaryFirmwareContextsPresent()
{
    if (wclScanLock == NULL)
        return false;
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    const bool present = primaryMacContext.occupied() ||
        primaryBindingContext.occupied() || primaryStationContext.occupied();
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    return present;
}

bool ItlIwm::
firmwareContextCommandCurrentLocked(const ItlFirmwareContextCommand &command) const
{
    const ItlFirmwareContextLease *context = NULL;
    switch (command.kind) {
        case ItlFirmwareContextCommand::Kind::Mac:
            context = &primaryMacContext;
            break;
        case ItlFirmwareContextCommand::Kind::Binding:
            context = &primaryBindingContext;
            break;
        case ItlFirmwareContextCommand::Kind::Station:
            context = &primaryStationContext;
            break;
    }
    if (context == NULL || command.submitted || !scanCommand.open ||
        (com.sc_flags & IWM_FLAG_SHUTDOWN) != 0 ||
        command.receipt.generation != static_cast<uint32_t>(com.sc_generation) ||
        !context->commandCurrent(command.receipt.serial, com.sc_generation) ||
        !context->owner.identity.equals(command.receipt.identity))
        return false;
    if (command.cleanup)
        return context->stage == ItlFirmwareContextLease::Stage::Removing ||
            ((command.kind == ItlFirmwareContextCommand::Kind::Mac ||
              command.kind == ItlFirmwareContextCommand::Kind::Station) &&
             context->stage == ItlFirmwareContextLease::Stage::Modifying);
    return command.receipt.identity.attempt.equals(
        ItlScanCommandPolicy::identityLocked(&com.sc_ic));
}

bool ItlIwm::
beginPrimaryStationUse(struct ieee80211_node *node, ItlFirmwareContextReceipt *receipt,
                       bool currentAttempt)
{
    struct ieee80211com *ic = &com.sc_ic;
    IOSimpleLock *ownerLock = ic->ic_pae_selected_bss_lock;
    if (receipt == NULL)
        return false;
    *receipt = ItlFirmwareContextReceipt{};
    if (node == NULL || wclScanLock == NULL || ownerLock == NULL)
        return false;
    const struct iwm_node *in = (const struct iwm_node *)node;
    IOInterruptState ownerIrq = IOSimpleLockLockDisableInterrupt(ownerLock);
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    const ItlFirmwareContextIdentity &identity = primaryStationContext.owner.identity;
    const bool admitted = scanCommand.open && !(com.sc_flags & IWM_FLAG_SHUTDOWN) &&
        primaryStationContext.confirmed && !primaryStationContext.uncertain &&
        (primaryStationContext.stage == ItlFirmwareContextLease::Stage::Active ||
         primaryStationContext.stage == ItlFirmwareContextLease::Stage::Modifying) &&
        primaryStationContext.owner.generation == static_cast<uint32_t>(com.sc_generation) &&
        (!currentAttempt ||
         (identity.attempt.equals(ItlScanCommandPolicy::identityLocked(ic)) &&
          identity.mode == ic->ic_opmode)) &&
        identity.mac == IWM_FW_CMD_ID_AND_COLOR(in->in_id, in->in_color) &&
        memcmp(identity.peer, in->in_macaddr, sizeof(identity.peer)) == 0 &&
        primaryStationUses.acquire(primaryStationContext.owner, receipt);
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    IOSimpleLockUnlockEnableInterrupt(ownerLock, ownerIrq);
    return admitted;
}

void ItlIwm::
endPrimaryStationUse(ItlFirmwareContextReceipt *receipt)
{
    (void)releasePrimaryStationReader(receipt);
}

bool ItlIwm::
releasePrimaryStationReader(ItlFirmwareContextReceipt *receipt)
{
    if (wclScanLock == NULL)
        return false;
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    const bool released = primaryStationUses.release(receipt);
    const bool ready = released && primaryStationUses.active == 0;
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    if (ready)
        resumePrimaryStationUsers();
    return released;
}

bool ItlIwm::
deferPrimaryStationUsers(const ItlStateTransitionRequest &request, bool continuation)
{
    struct ieee80211com *ic = &com.sc_ic;
    IOSimpleLock *ownerLock = ic->ic_pae_selected_bss_lock;
    if (wclScanLock == NULL || ownerLock == NULL)
        return false;
    IOInterruptState ownerIrq = IOSimpleLockLockDisableInterrupt(ownerLock);
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    const bool current = scanCommand.open && !(com.sc_flags & IWM_FLAG_SHUTDOWN) &&
        stateTransitionSource != NULL &&
        (request.state <= IEEE80211_S_AUTH || ic->ic_state == IEEE80211_S_RUN) &&
        stateTransition.current(request, com.sc_generation) &&
        request.identity.equals(ItlScanCommandPolicy::identityLocked(ic));
    if (current && primaryStationContext.occupied())
        primaryStationUses.close(request.state <= IEEE80211_S_AUTH);
    const bool deferred = current && (primaryStationUses.active != 0 || continuation) &&
        stateTransition.defer(request, com.sc_generation,
                              ItlStateTransitionLease::DeferredKind::StationUsers);
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    IOSimpleLockUnlockEnableInterrupt(ownerLock, ownerIrq);
    /* Close the exit-before-deferral lost wakeup without waiting on a gate. */
    if (deferred)
        resumePrimaryStationUsers();
    return deferred;
}

void ItlIwm::
resumePrimaryStationUsers()
{
    struct ieee80211com *ic = &com.sc_ic;
    IOSimpleLock *ownerLock = ic->ic_pae_selected_bss_lock;
    if (wclScanLock == NULL || ownerLock == NULL || !iwm_sae_tx_lifecycle_enter(&com, true))
        return;
    IOInterruptState ownerIrq = IOSimpleLockLockDisableInterrupt(ownerLock);
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    bool queued = false;
    if (stateTransition.stage == ItlStateTransitionLease::Stage::Deferred &&
        stateTransition.deferredKind == ItlStateTransitionLease::DeferredKind::StationUsers) {
        const ItlStateTransitionRequest &request = stateTransition.request;
        const bool current = scanCommand.open && !(com.sc_flags & IWM_FLAG_SHUTDOWN) &&
            stateTransitionSource != NULL &&
            request.hardwareGeneration == static_cast<uint32_t>(com.sc_generation) &&
            request.identity.equals(ItlScanCommandPolicy::identityLocked(ic));
        if (!current)
            stateTransition.invalidate();
        else if (primaryStationUses.active == 0)
            queued = stateTransition.resume(com.sc_generation);
    }
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    IOSimpleLockUnlockEnableInterrupt(ownerLock, ownerIrq);
    if (queued)
        iwm_add_task(&com, com.sc_nswq, &com.newstate_task);
    iwm_sae_tx_lifecycle_leave(&com);
}

void ItlIwm::
reopenPrimaryStationUsers(const ItlStateTransitionRequest &request)
{
    struct ieee80211com *ic = &com.sc_ic;
    IOSimpleLock *ownerLock = ic->ic_pae_selected_bss_lock;
    if (wclScanLock == NULL || ownerLock == NULL || request.state < IEEE80211_S_ASSOC)
        return;
    IOInterruptState ownerIrq = IOSimpleLockLockDisableInterrupt(ownerLock);
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    if (scanCommand.open && !(com.sc_flags & IWM_FLAG_SHUTDOWN) &&
        stateTransition.current(request, com.sc_generation) &&
        request.identity.equals(ItlScanCommandPolicy::identityLocked(ic)) &&
        primaryStationContext.stage == ItlFirmwareContextLease::Stage::Active &&
        primaryStationContext.confirmed && !primaryStationContext.uncertain &&
        primaryStationContext.owner.generation == static_cast<uint32_t>(com.sc_generation) &&
        primaryStationContext.owner.identity.attempt.equals(request.identity))
        (void)primaryStationUses.reopen(primaryStationContext.owner);
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    IOSimpleLockUnlockEnableInterrupt(ownerLock, ownerIrq);
}

int ItlIwm::
queuePrimaryRxBa(struct ieee80211_node *node, uint8_t tid, bool start)
{
    if (node == NULL || tid >= ItlStationRxBa::TidCount ||
        wclScanLock == NULL || !getMainWorkLoop()->inGate())
        return EINVAL;
    struct ieee80211com *ic = &com.sc_ic;
    IOSimpleLock *ownerLock = ic->ic_pae_selected_bss_lock;
    if (ownerLock == NULL)
        return ENXIO;
    const struct iwm_node *in = (const struct iwm_node *)node;
    ItlStationRxBaRequest request = {};
    request.tid = tid;
    request.start = start;
    request.hardwareBaid = com.sc_mqrx_supported;
    if (start) {
        const struct ieee80211_rx_ba *ba = &node->ni_rx_ba[tid];
        request.ssn = ba->ba_winstart;
        request.window = ba->ba_winsize;
        request.timeout = ba->ba_timeout_val;
        request.token = ba->ba_token;
    }
    IOInterruptState ownerIrq = IOSimpleLockLockDisableInterrupt(ownerLock);
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    const auto &station = primaryStationUses.owner;
    const bool current = stateTransitionSource != NULL && scanCommand.open &&
        !(com.sc_flags & IWM_FLAG_SHUTDOWN) && ic->ic_state == IEEE80211_S_RUN &&
        !primaryStationUses.closed && primaryStationContext.confirmed &&
        !primaryStationContext.uncertain && station.serial != 0 &&
        station.generation == static_cast<uint32_t>(com.sc_generation) &&
        station.identity.mode == IEEE80211_M_STA && ic->ic_opmode == IEEE80211_M_STA &&
        station.identity.attempt.equals(ItlScanCommandPolicy::identityLocked(ic)) &&
        station.identity.mac == IWM_FW_CMD_ID_AND_COLOR(in->in_id, in->in_color) &&
        memcmp(station.identity.peer, in->in_macaddr, IEEE80211_ADDR_LEN) == 0 &&
        primaryRxBa.nextSerial != UINT64_MAX && primaryRxBa.lifecycle != UINT64_MAX;
    if (current) {
        request.station = station;
        request.serial = ++primaryRxBa.nextSerial;
        request.lifecycle = primaryRxBa.lifecycle;
        primaryRxBa.latest[tid] = request.serial;
        if (!start)
            primaryRxBa.pending[1][tid] = ItlStationRxBaRequest{};
        primaryRxBa.pending[start ? 1 : 0][tid] = request;
    }
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    IOSimpleLockUnlockEnableInterrupt(ownerLock, ownerIrq);
    if (!current)
        return ECANCELED;
    iwm_add_task(&com, systq, &com.ba_task);
    return 0;
}

void ItlIwm::
runPrimaryRxBa()
{
    struct ieee80211com *ic = &com.sc_ic;
    IOSimpleLock *ownerLock = ic->ic_pae_selected_bss_lock;
    if (wclScanLock == NULL || ownerLock == NULL)
        return;
    /* IWM's actual producer reset barrier is a separate prerequisite. */
    ItlStationRxBaRequest request = {};
    ItlFirmwareContextReceipt use = {};
    ItlStationRxBaResource resource = {};
    IOInterruptState ownerIrq = IOSimpleLockLockDisableInterrupt(ownerLock);
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    if (primaryRxBa.phase == ItlStationRxBa::Phase::Idle &&
        stateTransitionSource != NULL && scanCommand.open &&
        !(com.sc_flags & IWM_FLAG_SHUTDOWN) &&
        primaryStationContext.stage == ItlFirmwareContextLease::Stage::Active &&
        primaryStationContext.confirmed && !primaryStationContext.uncertain) {
        for (unsigned direction = 0; direction < 2 && request.serial == 0; ++direction) {
            for (unsigned tid = 0; tid < ItlStationRxBa::TidCount; ++tid) {
                auto &pending = primaryRxBa.pending[direction][tid];
                if (pending.serial == 0)
                    continue;
                const bool current = pending.lifecycle == primaryRxBa.lifecycle &&
                    pending.station.serial == primaryStationUses.owner.serial &&
                    pending.station.generation == static_cast<uint32_t>(com.sc_generation) &&
                    pending.station.identity.equals(primaryStationUses.owner.identity) &&
                    pending.station.identity.attempt.equals(ItlScanCommandPolicy::identityLocked(ic));
                if (current && primaryStationUses.acquire(pending.station, &use)) {
                    request = pending;
                    primaryRxBa.current = request;
                    primaryRxBa.phase = ItlStationRxBa::Phase::Hardware;
                    resource = primaryRxBa.resource[tid];
                }
                pending = ItlStationRxBaRequest{};
                if (request.serial != 0)
                    break;
            }
        }
    }
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    IOSimpleLockUnlockEnableInterrupt(ownerLock, ownerIrq);
    if (request.serial == 0) {
        return;
    }
    int error = 0;
    uint8_t baid = resource.occupied ? resource.baid : IWM_RX_REORDER_DATA_INVALID_BAID;
    if (request.start && (resource.occupied ||
        ItlRxBaSessionCount::load(&com.sc_rx_ba_sessions) >= ItlStationRxBa::SessionLimit))
        error = ENOSPC;
    else if (request.start || resource.occupied)
        error = iwm_sta_rx_ba_cmd(&com, &use, request.tid, request.ssn,
            request.window, request.start, &baid);
    postPrimaryRxBa(request, &use, error, baid, false);
}

void ItlIwm::
postPrimaryRxBa(const ItlStationRxBaRequest &request, ItlFirmwareContextReceipt *use,
                int error, uint8_t baid, bool hardwareTaskHeld)
{
    IOInterruptEventSource *source = NULL;
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    const bool owns = primaryRxBa.matches(request) &&
        primaryRxBa.phase == ItlStationRxBa::Phase::Hardware;
    const bool physical = owns && request.lifecycle == primaryRxBa.lifecycle &&
        request.station.generation == static_cast<uint32_t>(com.sc_generation);
    if (physical && !request.cleanup) {
        auto &resource = primaryRxBa.resource[request.tid];
        if (request.start && !resource.occupied &&
            (error == 0 || primaryStationContext.uncertain)) {
            resource = ItlStationRxBaResource{};
            resource.station = request.station;
            resource.occupied = true;
            resource.hardwareBaid = request.hardwareBaid;
            resource.baid = baid;
            resource.uncertain = error != 0;
            resource.counted = true;
            ItlRxBaSessionCount::add(&com.sc_rx_ba_sessions);
        } else if (!request.start && resource.occupied) {
            if (error == 0)
                resource.firmwareRemoved = true;
            else if (primaryStationContext.uncertain)
                resource.uncertain = true;
        }
    }
    if (physical && stateTransitionSource != NULL && scanCommand.open &&
        !(com.sc_flags & IWM_FLAG_SHUTDOWN) && use->serial != 0) {
        primaryRxBa.use = *use;
        *use = ItlFirmwareContextReceipt{};
        primaryRxBa.result = error;
        primaryRxBa.resultBaid = baid;
        primaryRxBa.phase = ItlStationRxBa::Phase::Ready;
        source = stateTransitionSource;
        source->retain();
    } else if (owns) {
        primaryRxBa.current = ItlStationRxBaRequest{};
        primaryRxBa.phase = ItlStationRxBa::Phase::Idle;
    }
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    if (use->serial != 0)
        (void)releasePrimaryStationReader(use);
    (void)hardwareTaskHeld;
    if (source != NULL) {
        /* Always asynchronous, including a caller already on the main gate. */
        source->interruptOccurred(NULL, NULL, 0);
        source->release();
    }
}

void ItlIwm::
drainPrimaryRxBa(IOInterruptEventSource *source)
{
    if (wclScanLock == NULL || !getMainWorkLoop()->inGate())
        return;
    struct ieee80211com *ic = &com.sc_ic;
    ItlStationRxBaRequest request = {};
    ItlFirmwareContextReceipt use = {};
    int error = 0;
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    const bool taken = source != NULL && source == stateTransitionSource &&
        primaryRxBa.phase == ItlStationRxBa::Phase::Ready;
    bool physical = false;
    if (taken) {
        request = primaryRxBa.current;
        use = primaryRxBa.use;
        error = primaryRxBa.result;
        primaryRxBa.phase = ItlStationRxBa::Phase::Publishing;
        physical = request.lifecycle == primaryRxBa.lifecycle && scanCommand.open &&
            request.station.generation == static_cast<uint32_t>(com.sc_generation) &&
            !(com.sc_flags & IWM_FLAG_SHUTDOWN);
    }
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    if (!taken)
        return;

    bool upper = false, recover = false;
    struct ieee80211_node *node = ic->ic_bss;
    IOSimpleLock *ownerLock = ic->ic_pae_selected_bss_lock;
    if (physical && !request.cleanup && node != NULL && ownerLock != NULL) {
        const struct iwm_node *in = (const struct iwm_node *)node;
        IOInterruptState ownerIrq = IOSimpleLockLockDisableInterrupt(ownerLock);
        irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
        upper = primaryRxBa.latest[request.tid] == request.serial &&
            request.station.identity.attempt.equals(ItlScanCommandPolicy::identityLocked(ic)) &&
            request.station.identity.mac == IWM_FW_CMD_ID_AND_COLOR(in->in_id, in->in_color) &&
            memcmp(request.station.identity.peer, in->in_macaddr, IEEE80211_ADDR_LEN) == 0 &&
            ic->ic_state == IEEE80211_S_RUN && ic->ic_opmode == request.station.identity.mode;
        IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
        IOSimpleLockUnlockEnableInterrupt(ownerLock, ownerIrq);
        if (upper && request.start) {
            const struct ieee80211_rx_ba *ba = &node->ni_rx_ba[request.tid];
            upper = ba->ba_state == IEEE80211_BA_REQUESTED &&
                ba->ba_token == request.token && ba->ba_winstart == request.ssn &&
                ba->ba_winsize == request.window && ba->ba_timeout_val == request.timeout;
        }
    }

    if (physical) {
        const unsigned first = request.cleanup ? 0 : request.tid;
        const unsigned last = request.cleanup ? ItlStationRxBa::TidCount : first + 1;
        for (unsigned tid = first; tid < last; ++tid) {
            irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
            const auto resource = primaryRxBa.resource[tid];
            IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
            if (!resource.occupied)
                continue;
            recover |= resource.uncertain;
            if (resource.firmwareRemoved) {
                if (resource.hostPublished && resource.hardwareBaid) {
                    struct iwm_rxba_data *rxba = &com.sc_rxba_data[resource.baid];
                    if (rxba->baid != resource.baid || rxba->sta_id != resource.station.identity.station ||
                        rxba->tid != tid) {
                        error = EIO;
                        recover = true;
                        continue;
                    }
                    iwm_clear_reorder_buffer(&com, rxba);
                }
                irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
                if (request.lifecycle == primaryRxBa.lifecycle &&
                    request.station.generation == static_cast<uint32_t>(com.sc_generation)) {
                    if (resource.counted)
                        ItlRxBaSessionCount::drop(&com.sc_rx_ba_sessions);
                    primaryRxBa.resource[tid] = ItlStationRxBaResource{};
                }
                IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
            } else if (!request.cleanup && request.start && error == 0 && !resource.hostPublished) {
                if (resource.hardwareBaid) {
                    struct iwm_rxba_data *rxba = &com.sc_rxba_data[resource.baid];
                    if (rxba->baid != IWM_RX_REORDER_DATA_INVALID_BAID) {
                        error = EIO;
                        recover = true;
                        continue;
                    }
                    rxba->sta_id = resource.station.identity.station;
                    rxba->tid = tid;
                    rxba->baid = resource.baid;
                    rxba->timeout = upper ? request.timeout : 0;
                    /* Previous DELBA/reset frees these persistent-slot timers. */
                    if (!timeout_initialized(&rxba->session_timer))
                        timeout_set(&rxba->session_timer, iwm_rx_ba_session_expired, rxba);
                    if (!timeout_initialized(&rxba->reorder_buf.reorder_timer))
                        timeout_set(&rxba->reorder_buf.reorder_timer,
                            iwm_reorder_timer_expired, &rxba->reorder_buf);
                    getmicrouptime(&rxba->last_rx);
                    iwm_init_reorder_buffer(&rxba->reorder_buf, request.ssn, request.window);
                    if (upper && request.timeout != 0) {
                        timeout_add_usec(&rxba->session_timer, request.timeout);
                        node->ni_rx_ba[tid].ba_timeout_val = 0;
                    }
                }
                irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
                if (request.lifecycle == primaryRxBa.lifecycle &&
                    request.station.generation == static_cast<uint32_t>(com.sc_generation))
                    primaryRxBa.resource[tid].hostPublished = true;
                else
                    upper = false;
                IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
            }
        }
    }
    /* No borrowed node is used after either callback; its output may yield. */
    if (upper && request.start) {
        if (error == 0)
            ieee80211_addba_req_accept(ic, node, request.tid);
        else
            ieee80211_addba_req_refuse(ic, node, request.tid);
    }
    irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    if (primaryRxBa.matches(request)) {
        if (physical && request.cleanup && request.lifecycle == primaryRxBa.lifecycle &&
            request.station.generation == static_cast<uint32_t>(com.sc_generation))
            primaryRxBa.cleanupError = error;
        primaryRxBa.current = ItlStationRxBaRequest{};
        primaryRxBa.use = ItlFirmwareContextReceipt{};
        primaryRxBa.phase = ItlStationRxBa::Phase::Idle;
    }
    const bool replay = scanCommand.open && stateTransitionSource != NULL &&
        !(com.sc_flags & IWM_FLAG_SHUTDOWN);
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    (void)releasePrimaryStationReader(&use);
    if (replay) {
        if (recover && !request.cleanup)
            iwm_add_task(&com, systq, &com.init_task);
        else
            iwm_add_task(&com, systq, &com.ba_task);
    }
}

int ItlIwm::
retirePrimaryRxBa()
{
    if (wclScanLock == NULL)
        return ENXIO;
    ItlStationRxBaRequest request = {};
    ItlStationRxBaResource resources[ItlStationRxBa::TidCount] = {};
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    const int priorError = primaryRxBa.cleanupError;
    primaryRxBa.cleanupError = 0;
    if (priorError != 0) {
        IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
        return priorError;
    }
    if (primaryRxBa.phase != ItlStationRxBa::Phase::Idle) {
        IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
        return EINPROGRESS;
    }
    bool occupied = false;
    for (unsigned tid = 0; tid < ItlStationRxBa::TidCount; ++tid) {
        resources[tid] = primaryRxBa.resource[tid];
        occupied |= resources[tid].occupied;
    }
    primaryRxBa.cancelPending();
    if (!occupied) {
        IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
        return 0;
    }
    if (!scanCommand.open || primaryRxBa.nextSerial == UINT64_MAX ||
        primaryStationUses.active != 0 || primaryStationUses.owner.serial == 0) {
        IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
        return EBUSY;
    }
    request.serial = ++primaryRxBa.nextSerial;
    request.lifecycle = primaryRxBa.lifecycle;
    request.station = primaryStationUses.owner;
    request.tid = ItlStationRxBa::CleanupTid;
    request.cleanup = true;
    primaryRxBa.current = request;
    primaryRxBa.phase = ItlStationRxBa::Phase::Hardware;
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);

    int error = 0;
    bool removed = false;
    for (unsigned tid = 0; tid < ItlStationRxBa::TidCount; ++tid) {
        auto &resource = resources[tid];
        if (!resource.occupied)
            continue;
        if (resource.station.serial != request.station.serial ||
            resource.station.generation != request.station.generation ||
            !resource.station.identity.equals(request.station.identity)) {
            error = ENXIO;
            break;
        }
        if (!resource.firmwareRemoved) {
            uint8_t baid = resource.baid;
            error = iwm_sta_rx_ba_cmd(&com, NULL, tid, 0, 0, false, &baid);
            if (error != 0)
                break;
            irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
            const bool current = primaryRxBa.matches(request) &&
                request.lifecycle == primaryRxBa.lifecycle &&
                request.station.generation == static_cast<uint32_t>(com.sc_generation);
            if (current)
                primaryRxBa.resource[tid].firmwareRemoved = true;
            else
                error = ENXIO;
            IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
            if (error != 0)
                break;
        }
        removed = true;
    }
    ItlFirmwareContextReceipt use = {};
    irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    const bool current = primaryRxBa.matches(request) &&
        request.lifecycle == primaryRxBa.lifecycle &&
        request.station.generation == static_cast<uint32_t>(com.sc_generation);
    const bool retained = current && removed &&
        primaryStationUses.acquire(request.station, &use, true);
    if (!retained && primaryRxBa.matches(request)) {
        primaryRxBa.current = ItlStationRxBaRequest{};
        primaryRxBa.phase = ItlStationRxBa::Phase::Idle;
    }
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    if (!retained)
        return error != 0 ? error : ENXIO;
    postPrimaryRxBa(request, &use, error, IWM_RX_REORDER_DATA_INVALID_BAID, false);
    return EINPROGRESS;
}

void ItlIwm::
resetPrimaryRxBaLocked()
{
    if (primaryRxBa.lifecycle != UINT64_MAX)
        ++primaryRxBa.lifecycle;
    primaryRxBa.cancelPending();
    primaryRxBa.cleanupError = 0;
    for (auto &resource : primaryRxBa.resource)
        resource = ItlStationRxBaResource{};
    if (primaryRxBa.phase == ItlStationRxBa::Phase::Ready) {
        (void)primaryStationUses.release(&primaryRxBa.use);
        primaryRxBa.current = ItlStationRxBaRequest{};
        primaryRxBa.phase = ItlStationRxBa::Phase::Idle;
    }
    /* A hardware worker or reentrant publishing callback owns its reader
     * until it exits. Do not erase it or permit a new ADD to reuse it. */
}


int ItlIwm::
beginPrimaryBaCommand(const ItlFirmwareContextReceipt *use,
                      ItlFirmwareContextCommand *command)
{
    using Lease = ItlFirmwareContextLease;
    if (command == NULL)
        return EINVAL;
    *command = ItlFirmwareContextCommand{};
    command->kind = ItlFirmwareContextCommand::Kind::Station;
    command->cleanup = use == NULL;
    if (use == NULL) {
        const int error = beginPrimaryStationCleanup(false, &command->receipt);
        return error != 0 ? error : command->receipt.serial != 0 ? 0 : ENOENT;
    }
    struct ieee80211com *ic = &com.sc_ic;
    IOSimpleLock *ownerLock = ic->ic_pae_selected_bss_lock;
    if (wclScanLock == NULL || ownerLock == NULL)
        return ENXIO;
    IOInterruptState ownerIrq = IOSimpleLockLockDisableInterrupt(ownerLock);
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    Lease::Admission admission = Lease::Admission::Busy;
    if (scanCommand.open && !(com.sc_flags & IWM_FLAG_SHUTDOWN) &&
        primaryStationUses.active != 0 && use->serial != 0 &&
        use->serial == primaryStationUses.owner.serial &&
        use->generation == static_cast<uint32_t>(com.sc_generation) &&
        use->generation == primaryStationUses.owner.generation &&
        use->identity.equals(primaryStationUses.owner.identity) &&
        use->identity.attempt.equals(ItlScanCommandPolicy::identityLocked(ic)) &&
        primaryStationContext.confirmed && !primaryStationContext.uncertain)
        admission = primaryStationContext.begin(Lease::Operation::Modify,
            com.sc_generation, use->identity, &command->receipt);
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    IOSimpleLockUnlockEnableInterrupt(ownerLock, ownerIrq);
    return admission == Lease::Admission::Submit ? 0 :
        admission == Lease::Admission::Exhausted ? EOVERFLOW : EBUSY;
}

int ItlIwm::
finishPrimaryBaCommand(const ItlFirmwareContextCommand &command, int error,
                       bool definitelyRejected)
{
    using Lease = ItlFirmwareContextLease;
    if (wclScanLock == NULL)
        return ENXIO;
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    const Lease::Completion completion = error == 0 ? Lease::Completion::Success :
        command.submitted && !definitelyRejected ?
            Lease::Completion::Uncertain : Lease::Completion::Rejected;
    if (!primaryStationContext.finish(command.receipt, com.sc_generation, completion))
        error = ENXIO;
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    return error;
}

int ItlIwm::
beginPrimaryStationCleanup(bool remove, ItlFirmwareContextReceipt *receipt)
{
    using Lease = ItlFirmwareContextLease;
    if (wclScanLock == NULL || receipt == NULL)
        return ENXIO;
    *receipt = ItlFirmwareContextReceipt{};
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    Lease::Admission admission = Lease::Admission::Busy;
    if (scanCommand.open && !(com.sc_flags & IWM_FLAG_SHUTDOWN)) {
        if (!primaryStationContext.occupied())
            admission = Lease::Admission::Already;
        else {
            primaryStationUses.close(remove);
            if (primaryStationUses.active == 0)
                admission = primaryStationContext.begin(
                    remove ? Lease::Operation::Remove : Lease::Operation::Modify,
                    com.sc_generation, primaryStationContext.owner.identity, receipt);
        }
    }
    if (admission == Lease::Admission::Submit && remove &&
        !primaryStationRetirement.started) {
        primaryStationRetirement = ItlFirmwareStationRetirement{};
        primaryStationRetirement.started = true;
        primaryStationRetirement.identity = receipt->identity;
        primaryStationRetirement.generation = receipt->generation;
        primaryStationRetirement.drain = primaryStationContext.confirmed &&
            receipt->identity.mode == IEEE80211_M_STA;
        primaryStationRetirement.flushQueues = com.agg_queue_mask | primaryStationCommand.tfd_queue_msk;
    }
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    return admission == Lease::Admission::Submit || admission == Lease::Admission::Already ? 0 :
        admission == Lease::Admission::Exhausted ? EOVERFLOW : EBUSY;
}

bool ItlIwm::
notePrimaryStationRetirement(const ItlFirmwareContextReceipt &receipt, uint8_t step, int queue)
{
    if (wclScanLock == NULL || queue < -1 ||
        queue >= static_cast<int>(ItlFirmwareStationRetirement::MaxQueues))
        return false;
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    const bool current = primaryStationContext.commandCurrent(receipt.serial, com.sc_generation) &&
        primaryStationContext.stage == ItlFirmwareContextLease::Stage::Removing &&
        primaryStationContext.owner.identity.equals(receipt.identity) &&
        receipt.generation == static_cast<uint32_t>(com.sc_generation) &&
        primaryStationRetirement.owns(receipt);
    if (current) {
        primaryStationRetirement.completed |= step;
        if (queue >= 0)
            primaryStationRetirement.retiredQueues[queue / 64] |= UINT64_C(1) << (queue % 64);
    }
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    return current;
}

int ItlIwm::
finishPrimaryStationCleanup(const ItlFirmwareContextReceipt &receipt, int error)
{
    using Lease = ItlFirmwareContextLease;
    if (wclScanLock == NULL)
        return ENXIO;
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    /* A failed multi-command cleanup may already have drained/removed queues.
     * Retain the station and forbid new live modification until retirement. */
    if (!primaryStationContext.finish(receipt, com.sc_generation,
            error == 0 ? Lease::Completion::Success : Lease::Completion::Uncertain)) {
        error = ENXIO;
    } else {
        if (primaryStationContext.confirmed)
            com.sc_flags |= IWM_FLAG_STA_ACTIVE;
        else
            com.sc_flags &= ~IWM_FLAG_STA_ACTIVE;
        if (!primaryStationContext.occupied()) {
            memset(&primaryStationCommand, 0, sizeof(primaryStationCommand));
            com.agg_queue_mask = 0;
            com.agg_tid_disable = 0xffff;
        }
    }
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    return error;
}

bool ItlIwm::
enqueueStateTransition(const ItlStateTransitionRequest &request)
{
    if (!stateTransitionCurrent(request) || !iwm_sae_tx_lifecycle_enter(&com, true))
        return false;
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    const bool queued = stateTransitionSource != NULL && scanCommand.open &&
        (com.sc_flags & IWM_FLAG_SHUTDOWN) == 0 &&
        stateTransition.enqueue(request, com.sc_generation);
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    if (queued)
        iwm_add_task(&com, com.sc_nswq, &com.newstate_task);
    iwm_sae_tx_lifecycle_leave(&com);
    return queued;
}

bool ItlIwm::
takeStateTransition(ItlStateTransitionRequest *request)
{
    if (wclScanLock == NULL || request == NULL)
        return false;
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    const bool taken = stateTransitionSource != NULL && scanCommand.open &&
        (com.sc_flags & IWM_FLAG_SHUTDOWN) == 0 &&
        stateTransition.take(com.sc_generation, request);
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    return taken && stateTransitionCurrent(*request);
}

int ItlIwm::
postStateTransitionCommit(const ItlStateTransitionRequest &request, int error)
{
    if (!stateTransitionCurrent(request))
        return ECANCELED;
    IOInterruptEventSource *source = NULL;
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    if (stateTransitionSource != NULL && scanCommand.open &&
        (com.sc_flags & IWM_FLAG_SHUTDOWN) == 0 &&
        stateTransition.publish(request, com.sc_generation, error)) {
        source = stateTransitionSource;
        source->retain();
    }
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    if (source == NULL)
        return ECANCELED;
    int result = 0;
    if (getMainWorkLoop()->inGate())
        result = drainStateTransitionCommit(source);
    else
        source->interruptOccurred(NULL, NULL, 0);
    source->release();
    return result;
}

void ItlIwm::
recoverStateTransition(const ItlStateTransitionRequest &request)
{
    if (!getMainWorkLoop()->inGate() || !stateTransitionCurrent(request))
        return;
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    const bool recover = stateTransitionSource != NULL && scanCommand.open &&
        (com.sc_flags & IWM_FLAG_SHUTDOWN) == 0 &&
        stateTransition.current(request, com.sc_generation);
    if (recover) {
        /* Partial lower resources cannot admit a successor before reset. */
        scanCommand.open = false;
        stateTransition.invalidate();
    }
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    if (recover)
        iwm_add_task(&com, systq, &com.init_task);
}

int ItlIwm::
drainStateTransitionCommit(IOInterruptEventSource *source)
{
    if (wclScanLock == NULL || !getMainWorkLoop()->inGate())
        return ENXIO;
    ItlStateTransitionRequest request = {};
    int error = 0;
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(wclScanLock);
    const bool taken = source != NULL && source == stateTransitionSource &&
        scanCommand.open && (com.sc_flags & IWM_FLAG_SHUTDOWN) == 0 &&
        stateTransition.takeCommit(com.sc_generation, &request, &error);
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    if (!taken || !stateTransitionCurrent(request))
        return ECANCELED;
    if (error != 0) {
        ieee80211_roam_link_failed(&com.sc_ic, request.identity.associationEpoch);
        recoverStateTransition(request);
        return error;
    }

    /* Generic AUTH/ASSOC enqueue and its if_start now share the recursive
     * main workloop gate. No drained worker waits for that gate. */
    reopenPrimaryStationUsers(request);
    error = com.sc_newstate(&com.sc_ic,
        (enum ieee80211_state)request.state, request.argument);
    if (request.state == IEEE80211_S_RUN)
        IWX_AUTH_DIAG(
            "iwm_newstate_task: RUN state_commit result=%d committed_state=%u\n",
            error, (unsigned)com.sc_ic.ic_state);
    if (error != 0)
        recoverStateTransition(request);
    return error;
}

void ItlIwm::
stateTransitionEvent(OSObject *owner, IOInterruptEventSource *source, int count)
{
    (void)count;
    ItlIwm *that = static_cast<ItlIwm *>(owner);
    that->drainPrimaryRxBa(source);
    (void)that->drainStateTransitionCommit(source);
}

void ItlIwm::
invalidateWclScanForReset()
{
    uint64_t generation = 0;
    uint32_t backendGeneration = 0;
    bool rejectStart = false;
    bool invalidateActive = false;

    if (wclScanLock == NULL)
        return;
    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(wclScanLock);
    generation = wclScanUpperGeneration;
    backendGeneration = wclScanBackendGeneration;
    rejectStart =
        wclScanPhase == ItlIwmWclScanPhase::InitialQueued ||
        wclScanPhase == ItlIwmWclScanPhase::InitialStarting ||
        wclScanPhase == ItlIwmWclScanPhase::BackgroundStarting;
    invalidateActive =
        wclScanPhase == ItlIwmWclScanPhase::InitialActive ||
        wclScanPhase == ItlIwmWclScanPhase::BackgroundActive;
    iwm_wcl_scan_ticket_reset_locked(this);
    wclScanNeedsReopen = true;
    scanCommand.invalidate();
    scanCommandPolicy = ItlScanCommandPolicy{};
    stateTransition.invalidate();
    scanCommandAbortSerial = 0;
    IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    __atomic_store_n(&com.sc_ic.ic_wcl_scan_active, 0, __ATOMIC_RELEASE);

    if (rejectStart) {
        iwm_wcl_scan_publish_start_rejected(this, generation,
                                            backendGeneration);
    } else if (invalidateActive && generation != 0 &&
               backendGeneration != 0 &&
               com.sc_ic.ic_event_handler != NULL) {
        struct ieee80211_wcl_scan_invalidation invalidation;
        explicit_bzero(&invalidation, sizeof(invalidation));
        invalidation.generation = generation;
        invalidation.backend_generation = backendGeneration;
        (*com.sc_ic.ic_event_handler)(
            &com.sc_ic, IEEE80211_EVT_WCL_SCAN_INVALIDATED, &invalidation);
        explicit_bzero(&invalidation, sizeof(invalidation));
    }
}

IOReturn ItlIwm::
setMulticastList(IOEthernetAddress *addr, int count)
{
    struct ieee80211com *ic = &com.sc_ic;
    struct iwm_mcast_filter_cmd *cmd;
    int len;
    uint8_t addr_count;
    int err;
    
    if (ic->ic_state != IEEE80211_S_RUN || ic->ic_bss == NULL)
        return kIOReturnError;
    addr_count = count;
    if (count > IWM_MAX_MCAST_FILTERING_ADDRESSES)
        addr_count = 0;
    if (addr == NULL)
        addr_count = 0;
    len = roundup(sizeof(struct iwm_mcast_filter_cmd) + addr_count * ETHER_ADDR_LEN, 4);
    cmd = (struct iwm_mcast_filter_cmd *)malloc(len, 0, 0);
    if (!cmd)
        return kIOReturnError;
    cmd->pass_all = addr_count == 0;
    cmd->count = addr_count;
    cmd->port_id = 0;
    IEEE80211_ADDR_COPY(cmd->bssid, ic->ic_bss->ni_bssid);
    if (addr_count > 0)
        memcpy(cmd->addr_list,
               addr->bytes, ETHER_ADDR_LEN * cmd->count);
    err = iwm_send_cmd_pdu(&com, IWM_MCAST_FILTER_CMD, IWM_CMD_ASYNC, len,
                     cmd);
    ::free(cmd);
    return err ? kIOReturnError : kIOReturnSuccess;
}

const char *ItlIwm::
getFirmwareVersion()
{
    return com.sc_fwver;
}

const char *ItlIwm::
getFirmwareName()
{
    return com.sc_fwname;
}

UInt32 ItlIwm::
supportedFeatures()
{
    return kIONetworkFeatureMultiPages;
}

const char *ItlIwm::
getFirmwareCountryCode()
{
    return com.sc_fw_mcc;
}

uint32_t ItlIwm::
getTxQueueSize()
{
    return IWM_TX_RING_COUNT;
}

int16_t ItlIwm::
getBSSNoise()
{
    return com.sc_noise;
}

bool ItlIwm::
is5GBandSupport()
{
    return com.sc_nvm.sku_cap_band_52GHz_enable;
}

int ItlIwm::
getTxNSS()
{
    return iwm_mimo_enabled(&com) &&
    (com.sc_ic.ic_bss != NULL && com.sc_ic.ic_bss->ni_rx_nss > 1) ?
    2 : 1;
}

uint8_t ItlIwm::
getTxChainMask()
{
    return iwm_fw_valid_tx_ant(&com);
}

uint8_t ItlIwm::
getRxChainMask()
{
    return iwm_fw_valid_rx_ant(&com);
}

uint32_t ItlIwm::
getLqmBeaconCount()
{
    return le32toh(com.sc_stats.rx.general.channel_beacons);
}
