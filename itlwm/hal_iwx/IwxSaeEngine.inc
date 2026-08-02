namespace {

static void
iwx_sae_wcl_credential_clear_locked(struct iwx_softc *sc)
{
    if (sc == NULL)
        return;
    explicit_bzero(&sc->sc_sae_wcl_credential,
        sizeof(sc->sc_sae_wcl_credential));
    sc->sc_sae_wcl_credential_staged = false;
    sc->sc_sae_wcl_credential_pending = false;
    sc->sc_sae_wcl_credential_active = false;
}

/* Reset/cancellation may retire a not-yet-validated credential without
 * erasing the record for an already port-valid SAE ESS.  A single value slot
 * means the three states are mutually exclusive. */
static void
iwx_sae_wcl_credential_clear_transient_locked(struct iwx_softc *sc)
{
    if (sc == NULL)
        return;
    if (sc->sc_sae_wcl_credential_staged ||
        sc->sc_sae_wcl_credential_pending)
        iwx_sae_wcl_credential_clear_locked(sc);
}

/* The caller supplies the fixed, already-validated ABI, so compare the full
 * canonical record without an early exit.  This runs only under the private
 * staging leaf and never exposes an equality result outside the HAL. */
static bool
iwx_sae_wcl_credential_equal(const struct ItlSaeWclCredentialV1 *left,
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
iwx_sae_wcl_credential_cancelled_locked(const struct iwx_softc *sc,
    uint64_t request_generation)
{
    return sc != NULL && request_generation != 0 &&
        sc->sc_sae_wcl_credential_cancel_valid &&
        request_generation <=
            sc->sc_sae_wcl_credential_cancel_through_generation;
}

static void
iwx_sae_wcl_credential_cancel_through_locked(struct iwx_softc *sc,
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
        iwx_sae_wcl_credential_cancelled_locked(sc,
            sc->sc_sae_wcl_credential.request_generation))
        iwx_sae_wcl_credential_clear_locked(sc);
}

/* Consume a staged CIPHER_PWD only after a driver-owned selected join has
 * rebound every public identity.  The copy is worker-local; the sole private
 * value changes from STAGED to PENDING until port-valid proves the complete
 * SAE/PMF association.  No second durable secret copy is created. */
static bool
iwx_sae_wcl_credential_take_bound(struct iwx_softc *sc,
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
        !iwx_sae_wcl_credential_cancelled_locked(sc,
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
iwx_sae_wcl_credential_retire_pending_generation(struct iwx_softc *sc,
    u_int64_t request_generation)
{
    if (sc == NULL || request_generation == 0 ||
        sc->sc_sae_wcl_credential_lock == NULL)
        return;
    IOSimpleLockLock(sc->sc_sae_wcl_credential_lock);
    if (sc->sc_sae_wcl_credential_pending &&
        sc->sc_sae_wcl_credential.request_generation ==
            request_generation)
        iwx_sae_wcl_credential_clear_locked(sc);
    IOSimpleLockUnlock(sc->sc_sae_wcl_credential_lock);
}

/* S_SCAN is the ordinary pre-selection path.  S_RUN is permitted only for a
 * live STA reconnect request: it records a future candidate while preserving
 * the current BSS unchanged.  The staging method never starts a scan,
 * changes state, queues a task, invokes an engine, or transmits a frame. */
static bool
iwx_sae_wcl_credential_stage_state_permitted(const struct ieee80211com *ic,
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
#define IWX_SAE_ENGINE_CALLBACK_CLOSED      0x80000000U
#define IWX_SAE_ENGINE_CALLBACK_COUNT_MASK  0x7fffffffU
#define IWX_SAE_ENGINE_TASK_ADMISSION_CLOSED      0x80000000U
#define IWX_SAE_ENGINE_TASK_ADMISSION_COUNT_MASK  0x7fffffffU
#define IWX_SCAN_LEASE_REPLAY_TASK_ADMISSION_CLOSED      0x80000000U
#define IWX_SCAN_LEASE_REPLAY_TASK_ADMISSION_COUNT_MASK  0x7fffffffU

/* Generic net80211 can retain a hook pointer briefly after it has dropped
 * its selected-BSS lock.  RX may invoke that copied pointer adjacent to an
 * interrupt, so admission uses a CAS lease rather than the sleeping SAE-TX
 * lifecycle mutex.  Detach closes first and drains only already-admitted
 * callbacks before it frees the private owner leaf. */
static bool
iwx_sae_engine_callback_enter(struct iwx_softc *sc)
{
    u_int32_t state, next;

    if (sc == NULL)
        return false;
    state = __atomic_load_n(&sc->sc_sae_engine_callback_state,
        __ATOMIC_ACQUIRE);
    for (;;) {
        if ((state & IWX_SAE_ENGINE_CALLBACK_CLOSED) != 0 ||
            (state & IWX_SAE_ENGINE_CALLBACK_COUNT_MASK) ==
            IWX_SAE_ENGINE_CALLBACK_COUNT_MASK)
            return false;
        next = state + 1;
        if (__atomic_compare_exchange_n(&sc->sc_sae_engine_callback_state,
            &state, next, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return true;
    }
}

static void
iwx_sae_engine_callback_leave(struct iwx_softc *sc)
{
    u_int32_t state, next;

    if (sc == NULL)
        return;
    state = __atomic_load_n(&sc->sc_sae_engine_callback_state,
        __ATOMIC_ACQUIRE);
    for (;;) {
        if ((state & IWX_SAE_ENGINE_CALLBACK_COUNT_MASK) == 0)
            return;
        next = state - 1;
        if (__atomic_compare_exchange_n(&sc->sc_sae_engine_callback_state,
            &state, next, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return;
    }
}

static void
iwx_sae_engine_callback_close(struct iwx_softc *sc)
{
    u_int32_t state, next;

    if (sc == NULL)
        return;
    state = __atomic_load_n(&sc->sc_sae_engine_callback_state,
        __ATOMIC_ACQUIRE);
    for (;;) {
        if ((state & IWX_SAE_ENGINE_CALLBACK_CLOSED) != 0)
            return;
        next = state | IWX_SAE_ENGINE_CALLBACK_CLOSED;
        if (__atomic_compare_exchange_n(&sc->sc_sae_engine_callback_state,
            &state, next, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return;
    }
}

static bool
iwx_sae_engine_callback_open(struct iwx_softc *sc)
{
    u_int32_t expected = IWX_SAE_ENGINE_CALLBACK_CLOSED;

    return sc != NULL && __atomic_compare_exchange_n(
        &sc->sc_sae_engine_callback_state, &expected, 0, false,
        __ATOMIC_RELEASE, __ATOMIC_ACQUIRE);
}

static void
iwx_sae_engine_callback_drain(struct iwx_softc *sc)
{
    if (sc == NULL)
        return;
    while ((__atomic_load_n(&sc->sc_sae_engine_callback_state,
        __ATOMIC_ACQUIRE) & IWX_SAE_ENGINE_CALLBACK_COUNT_MASK) != 0)
        IOSleep(1);
}

/* task_add() takes the taskq's sleeping lock, so it cannot run beneath the
 * engine simple lock.  This tiny CAS lease is instead the explicit handoff
 * between an RX/terminal producer and detach: close+drain guarantees every
 * pre-close task_add finished before detach removes its task and queues the
 * barrier, while a post-close producer cannot enqueue at all. */
static bool
iwx_sae_engine_task_admission_enter(struct iwx_softc *sc)
{
    u_int32_t state, next;

    if (sc == NULL)
        return false;
    state = __atomic_load_n(&sc->sc_sae_engine_task_admission_state,
        __ATOMIC_ACQUIRE);
    for (;;) {
        if ((state & IWX_SAE_ENGINE_TASK_ADMISSION_CLOSED) != 0 ||
            (state & IWX_SAE_ENGINE_TASK_ADMISSION_COUNT_MASK) ==
            IWX_SAE_ENGINE_TASK_ADMISSION_COUNT_MASK)
            return false;
        next = state + 1;
        if (__atomic_compare_exchange_n(
            &sc->sc_sae_engine_task_admission_state, &state, next, false,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return true;
    }
}

static void
iwx_sae_engine_task_admission_leave(struct iwx_softc *sc)
{
    u_int32_t state, next;

    if (sc == NULL)
        return;
    state = __atomic_load_n(&sc->sc_sae_engine_task_admission_state,
        __ATOMIC_ACQUIRE);
    for (;;) {
        if ((state & IWX_SAE_ENGINE_TASK_ADMISSION_COUNT_MASK) == 0)
            return;
        next = state - 1;
        if (__atomic_compare_exchange_n(
            &sc->sc_sae_engine_task_admission_state, &state, next, false,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return;
    }
}

static void
iwx_sae_engine_task_admission_close(struct iwx_softc *sc)
{
    u_int32_t state, next;

    if (sc == NULL)
        return;
    state = __atomic_load_n(&sc->sc_sae_engine_task_admission_state,
        __ATOMIC_ACQUIRE);
    for (;;) {
        if ((state & IWX_SAE_ENGINE_TASK_ADMISSION_CLOSED) != 0)
            return;
        next = state | IWX_SAE_ENGINE_TASK_ADMISSION_CLOSED;
        if (__atomic_compare_exchange_n(
            &sc->sc_sae_engine_task_admission_state, &state, next, false,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return;
    }
}

static void
iwx_sae_engine_task_admission_drain(struct iwx_softc *sc)
{
    if (sc == NULL)
        return;
    while ((__atomic_load_n(&sc->sc_sae_engine_task_admission_state,
        __ATOMIC_ACQUIRE) & IWX_SAE_ENGINE_TASK_ADMISSION_COUNT_MASK) != 0)
        IOSleep(1);
}

static bool
iwx_sae_engine_runtime_enabled(const struct iwx_softc *sc)
{
#if ITL_SAE_DRIVER_CRYPTO_AVAILABLE
    return sc != NULL && sc->sc_sae_engine_runtime_enabled &&
        sc->sc_sae_engine_lock != NULL &&
        sc->sc_sae_wcl_credential_lock != NULL &&
        sc->sc_sae_tx_lock != NULL && sc->sc_task_gate_lock != NULL &&
        sc->sc_taskq_initialized && sc->sc_task_callbacks_ready &&
        sc->sc_nswq != NULL && iwx_mfp_runtime_enabled(sc) &&
        sc->sc_ic.ic_pae_selected_bss_lock != NULL;
#else
    (void)sc;
    return false;
#endif
}

static bool
iwx_sae_task_lifecycle_is_open(struct iwx_softc *sc)
{
    bool open = false;

    if (sc == NULL || sc->sc_task_gate_lock == NULL)
        return false;
    IOLockLock(sc->sc_task_gate_lock);
    open = !sc->sc_task_gate_closed && !sc->sc_task_gate_detaching &&
        (sc->sc_flags & IWX_FLAG_SHUTDOWN) == 0;
    IOLockUnlock(sc->sc_task_gate_lock);
    return open;
}

/* No secret belongs to this record.  The crypto engine itself is worker-only
 * and destroyed separately by that worker after this public identity has
 * been retired. */
static void
iwx_sae_engine_owner_clear_locked(struct iwx_softc *sc)
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
iwx_sae_engine_queue_wcl_cancel_locked(struct iwx_softc *sc,
    u_int64_t request_generation)
{
    if (sc != NULL && request_generation != 0 &&
        request_generation > sc->sc_sae_engine_wcl_cancel_generation)
        sc->sc_sae_engine_wcl_cancel_generation = request_generation;
}

static u_int64_t
iwx_sae_engine_generation_advance_locked(struct iwx_softc *sc)
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
iwx_sae_engine_owner_matches_node_locked(const struct iwx_softc *sc,
    const struct ieee80211com *ic, const struct ieee80211_node *ni)
{
    const struct iwx_sae_engine_owner *owner;

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
iwx_sae_engine_owner_matches_terminal_locked(const struct iwx_softc *sc,
    const struct ItlSaeAuthTransportEventV1 *event)
{
    const struct iwx_sae_engine_owner *owner;

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
iwx_sae_engine_owner_matches_peer_locked(const struct iwx_softc *sc,
    const struct ItlSaeAuthPeerEventV1 *event)
{
    const struct iwx_sae_engine_owner *owner;

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
 * epoch.  Lock order is selected-BSS -> engine -> TX simple fence; no IWX
 * path takes the selected-BSS leaf while it holds the engine leaf. */
static bool
iwx_sae_engine_peer_owner_current_locked(const struct iwx_softc *sc,
    const struct iwx_sae_engine_owner *owner)
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
 * before a concurrent IWX doorbell can linearize a now-revoked Commit or
 * Confirm.  No lifecycle lock, taskq operation, generic callback or WCL
 * leaf may occur under either lock. */
static void
iwx_sae_engine_fence_inflight_ticket_locked(struct iwx_softc *sc,
    const struct iwx_sae_engine_owner *owner)
{
    u_int64_t ticket;

    if (sc == NULL || owner == NULL ||
        (ticket = owner->in_flight_ticket) == 0 ||
        sc->sc_sae_tx_lock == NULL)
        return;
    IOSimpleLockLock(sc->sc_sae_tx_lock);
    iwx_sae_tx_cancel_ticket_locked(sc, ticket);
    if (sc->sc_sae_tx_active && !sc->sc_sae_tx_doorbelled &&
        sc->sc_sae_tx_active_ticket == ticket) {
        sc->sc_sae_tx_active = false;
        sc->sc_sae_tx_active_ticket = 0;
        explicit_bzero(&sc->sc_sae_tx_active_event,
            sizeof(sc->sc_sae_tx_active_event));
    }
    if (sc->sc_sae_tx_last_event_valid &&
        iwx_sae_tx_ticket_cancelled_locked(sc,
            sc->sc_sae_tx_last_event.ticket)) {
        sc->sc_sae_tx_last_event_valid = false;
        explicit_bzero(&sc->sc_sae_tx_last_event,
            sizeof(sc->sc_sae_tx_last_event));
    }
    IOSimpleLockUnlock(sc->sc_sae_tx_lock);
}

static void
iwx_sae_engine_schedule_task(struct iwx_softc *sc)
{
    if (sc == NULL || !iwx_sae_engine_task_admission_enter(sc))
        return;
    if (sc->sc_sae_engine_task_ready && systq != NULL)
        (void)task_add(systq, &sc->sae_engine_task);
    iwx_sae_engine_task_admission_leave(sc);
}

} // namespace
bool ItlIwx::
supportsDriverResidentSae()
{
    return iwx_sae_engine_runtime_enabled(&com);
}

bool ItlIwx::
isSaeWclCredentialAdmissionReady()
{
    struct iwx_softc *sc = &com;
    struct ieee80211com *ic = &sc->sc_ic;
    struct _ifnet *ifp = IC2IFP(ic);
    bool lifecycle_open = false;
    bool scan_idle = false;
    bool engine_idle = false;
    bool credential_empty = false;

    if (!iwx_sae_engine_runtime_enabled(sc) ||
        !iwx_task_gate_enter(sc, false))
        return false;

    lifecycle_open = wclScanLock != NULL &&
        sc->sc_sae_engine_lock != NULL &&
        sc->sc_sae_wcl_credential_lock != NULL &&
        iwx_sae_wcl_credential_stage_state_permitted(ic, ifp);
    if (lifecycle_open) {
        IOInterruptState irq =
            IOSimpleLockLockDisableInterrupt(wclScanLock);
        scan_idle = wclScanPhase == ItlIwxWclScanPhase::Idle &&
            !wclSaeAdmissionReserved &&
            (sc->sc_flags & IWX_FLAG_SCANNING) == 0;
        IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);

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
    iwx_task_gate_leave(sc);

    return lifecycle_open && scan_idle && engine_idle && credential_empty;
}

bool ItlIwx::
reserveSaeWclCredentialAdmission()
{
    struct iwx_softc *sc = &com;
    struct ieee80211com *ic = &sc->sc_ic;
    struct _ifnet *ifp = IC2IFP(ic);
    bool reserved = false;
    bool engine_idle = false;
    bool credential_empty = false;

    if (!iwx_sae_engine_runtime_enabled(sc) ||
        !iwx_task_gate_enter(sc, false))
        return false;

    /*
     * The scan leaf is the linearization point.  Once the identity-free bit
     * is set, every ordinary physical-scan reserve fails until the direct
     * SAE scan atomically consumes it.  The other leaves are checked without
     * nesting; a failure rolls the bit back before returning.
     */
    if (wclScanLock != NULL &&
        sc->sc_sae_engine_lock != NULL &&
        sc->sc_sae_wcl_credential_lock != NULL &&
        iwx_sae_wcl_credential_stage_state_permitted(ic, ifp)) {
        IOInterruptState irq =
            IOSimpleLockLockDisableInterrupt(wclScanLock);
        if (wclScanPhase == ItlIwxWclScanPhase::Idle &&
            !wclSaeAdmissionReserved &&
            (sc->sc_flags & IWX_FLAG_SCANNING) == 0) {
            wclSaeAdmissionReserved = true;
            reserved = true;
        }
        IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
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
            IOInterruptState irq =
                IOSimpleLockLockDisableInterrupt(wclScanLock);
            wclSaeAdmissionReserved = false;
            IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
            reserved = false;
        }
    }
    iwx_task_gate_leave(sc);
    return reserved;
}

void ItlIwx::
releaseSaeWclCredentialAdmission()
{
    struct iwx_softc *sc = &com;

    if (!iwx_task_gate_enter(sc, false))
        return;
    if (wclScanLock != NULL) {
        IOInterruptState irq =
            IOSimpleLockLockDisableInterrupt(wclScanLock);
        wclSaeAdmissionReserved = false;
        IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    }
    iwx_task_gate_leave(sc);
}
IOReturn ItlIwx::
stageSaeWclCredential(const struct ItlSaeWclCredentialV1 *credential)
{
    struct iwx_softc *sc = &com;
    struct ieee80211com *ic = &sc->sc_ic;
    struct _ifnet *ifp = IC2IFP(ic);
    struct ItlSaeWclCredentialV1 copy;
    IOReturn rc = kIOReturnNotReady;

    if (credential == NULL)
        return kIOReturnBadArgument;
    if (!iwx_sae_wcl_credential_runtime_opted_in())
        return kIOReturnUnsupported;

    /* Copy before validation and never retain the caller's WCL buffer. */
    explicit_bzero(&copy, sizeof(copy));
    memcpy(&copy, credential, sizeof(copy));
    if (!itl_sae_wcl_credential_is_well_formed(&copy)) {
        rc = kIOReturnBadArgument;
        goto out;
    }
    if (!iwx_sae_engine_runtime_enabled(sc) ||
        !iwx_task_gate_enter(sc, false))
        goto out;

    if (sc->sc_sae_wcl_credential_lock != NULL &&
        iwx_sae_wcl_credential_stage_state_permitted(ic, ifp)) {
        IOSimpleLockLock(sc->sc_sae_wcl_credential_lock);
        if (iwx_sae_wcl_credential_cancelled_locked(sc,
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
                iwx_sae_wcl_credential_clear_locked(sc);
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
            if (iwx_sae_wcl_credential_equal(
                    &sc->sc_sae_wcl_credential, &copy)) {
                /* A WCL retry is harmless only when it is byte-for-byte the
                 * same canonical record; do not make a second secret copy. */
                rc = kIOReturnSuccess;
            } else {
                /* One generation is immutable.  A conflicting retry could
                 * otherwise select a password different from the candidate
                 * it claims to resume, so retire both representations. */
                iwx_sae_wcl_credential_cancel_through_locked(sc,
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
            iwx_sae_wcl_credential_cancel_through_locked(sc,
                sc->sc_sae_wcl_credential.request_generation);
            if (!sc->sc_sae_wcl_credential_staged &&
                !iwx_sae_wcl_credential_cancelled_locked(sc,
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
    iwx_task_gate_leave(sc);
out:
    explicit_bzero(&copy, sizeof(copy));
    return rc;
}

/* A cancellation is meaningful before a selected BSS exists.  Preserve its
 * monotonic high-water even if an older slot is currently staged: that newer
 * cancellation retires the old secret and fences every delayed predecessor. */
void ItlIwx::
cancelSaeWclCredential(uint64_t request_generation)
{
    struct iwx_softc *sc = &com;

    if (request_generation == 0 ||
        !iwx_sae_wcl_credential_runtime_opted_in() ||
        !iwx_task_gate_enter(sc, false))
        return;
    if (sc->sc_sae_wcl_credential_lock != NULL) {
        IOSimpleLockLock(sc->sc_sae_wcl_credential_lock);
        iwx_sae_wcl_credential_cancel_through_locked(sc,
            request_generation);
        IOSimpleLockUnlock(sc->sc_sae_wcl_credential_lock);
    }
    iwx_task_gate_leave(sc);
}

/* A queue-overflow path has no trustworthy request generation.  Scrub only
 * a transient staged/pending value; a validated active ESS is a separate
 * roam owner and is not implicated by a producer queue overflow. */
void ItlIwx::
purgeSaeWclCredentialStage()
{
    struct iwx_softc *sc = &com;

    if (!iwx_task_gate_enter(sc, false))
        return;
    if (sc->sc_sae_wcl_credential_lock != NULL) {
        IOSimpleLockLock(sc->sc_sae_wcl_credential_lock);
        iwx_sae_wcl_credential_clear_transient_locked(sc);
        IOSimpleLockUnlock(sc->sc_sae_wcl_credential_lock);
    }
    iwx_task_gate_leave(sc);
}

void ItlIwx::
iwx_sae_wcl_stop_begin(struct iwx_softc *sc)
{
    uint64_t generation = 0;

    if (sc == NULL)
        return;
    if (wclScanLock != NULL) {
        IOInterruptState irq =
            IOSimpleLockLockDisableInterrupt(wclScanLock);
        wclSaeAdmissionReserved = false;
        IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    }
    if (sc->sc_sae_wcl_credential_lock == NULL)
        return;
    IOSimpleLockLock(sc->sc_sae_wcl_credential_lock);
    if (sc->sc_sae_wcl_credential_staged ||
        sc->sc_sae_wcl_credential_pending)
        generation = sc->sc_sae_wcl_credential.request_generation;
    if (generation != 0)
        iwx_sae_wcl_credential_cancel_through_locked(sc, generation);
    else
        iwx_sae_wcl_credential_clear_transient_locked(sc);
    IOSimpleLockUnlock(sc->sc_sae_wcl_credential_lock);
}

void ItlIwx::
iwx_sae_wcl_detach_begin(struct iwx_softc *sc)
{
    if (sc == NULL)
        return;
    if (wclScanLock != NULL) {
        IOInterruptState irq =
            IOSimpleLockLockDisableInterrupt(wclScanLock);
        wclSaeAdmissionReserved = false;
        IOSimpleLockUnlockEnableInterrupt(wclScanLock, irq);
    }
    if (sc->sc_sae_wcl_credential_lock != NULL) {
        IOSimpleLockLock(sc->sc_sae_wcl_credential_lock);
        iwx_sae_wcl_credential_clear_locked(sc);
        IOSimpleLockUnlock(sc->sc_sae_wcl_credential_lock);
    }
}
namespace {

struct IwxSaeEngineCancellation {
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
iwx_sae_engine_mark_cancelled_locked(struct iwx_softc *sc,
    u_int64_t expected_generation, bool suppress_scan,
    struct IwxSaeEngineCancellation *cancel)
{
    struct iwx_sae_engine_owner *owner;

    if (cancel != NULL)
        explicit_bzero(cancel, sizeof(*cancel));
    if (sc == NULL || cancel == NULL || sc->sc_sae_engine_lock == NULL)
        return false;
    owner = &sc->sc_sae_engine_owner;
    /* A revoke can precede selected-BSS ownership; preserve that generation
     * for the worker even when no active engine record matches it yet. */
    iwx_sae_engine_queue_wcl_cancel_locked(sc, expected_generation);
    if (owner->active && (expected_generation == 0 ||
        owner->request_generation == expected_generation)) {
        cancel->active = true;
        cancel->request_generation = owner->request_generation;
        cancel->association_epoch = owner->association_epoch;
        cancel->relay_generation = owner->relay_generation;
        cancel->ticket = owner->in_flight_ticket;
        owner->cancelled = true;
        iwx_sae_engine_queue_wcl_cancel_locked(sc,
            owner->request_generation);
        iwx_sae_engine_fence_inflight_ticket_locked(sc, owner);
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
iwx_sae_engine_mark_cancelled(struct iwx_softc *sc,
    u_int64_t expected_generation, bool suppress_scan,
    struct IwxSaeEngineCancellation *cancel)
{
    bool marked;

    if (cancel != NULL)
        explicit_bzero(cancel, sizeof(*cancel));
    if (sc == NULL || cancel == NULL || sc->sc_sae_engine_lock == NULL)
        return false;
    IOSimpleLockLock(sc->sc_sae_engine_lock);
    marked = iwx_sae_engine_mark_cancelled_locked(sc, expected_generation,
        suppress_scan, cancel);
    IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    return marked;
}

enum IwxSaeAssocTxAdmission {
    IWX_SAE_ASSOC_TX_NOT_DIRECT = 0,
    IWX_SAE_ASSOC_TX_ADMITTED = 1,
    IWX_SAE_ASSOC_TX_REJECTED = -1,
};

/* The descriptor fence retains only this public completion identity between
 * the pre-trim Association Request check and the final firmware doorbell. */
struct IwxSaeAssocTxClaim {
    struct ItlSaePmkContinuationIdentityV1 identity;
    bool                                    active;
};

static bool
iwx_sae_engine_pmk_identity_equal(
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
iwx_sae_engine_owner_matches_pmk_identity_locked(
    const struct iwx_softc *sc, const struct iwx_sae_engine_owner *owner,
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
iwx_sae_engine_assoc_tx_frame_matches_identity(
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
iwx_sae_engine_assoc_tx_frame_matches_selected(
    const struct ieee80211_frame *wh,
    const struct iwx_sae_engine_owner *owner)
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
static enum IwxSaeAssocTxAdmission
iwx_sae_engine_assoc_tx_preflight(struct iwx_softc *sc,
    const struct ieee80211_node *ni, const struct ieee80211_frame *wh,
    struct IwxSaeAssocTxClaim *claim)
{
    struct iwx_sae_engine_owner *owner;
    struct IwxSaeEngineCancellation cancel;
    IOSimpleLock *bss_lock;
    IOInterruptState irq;
    enum IwxSaeAssocTxAdmission result = IWX_SAE_ASSOC_TX_NOT_DIRECT;
    bool schedule = false;

    if (claim != NULL)
        explicit_bzero(claim, sizeof(*claim));
    if (sc == NULL || ni == NULL || wh == NULL || claim == NULL ||
        sc->sc_sae_engine_lock == NULL)
        return IWX_SAE_ASSOC_TX_NOT_DIRECT;
    bss_lock = sc->sc_ic.ic_pae_selected_bss_lock;
    if (bss_lock == NULL)
        return IWX_SAE_ASSOC_TX_NOT_DIRECT;

    explicit_bzero(&cancel, sizeof(cancel));
    irq = IOSimpleLockLockDisableInterrupt(bss_lock);
    IOSimpleLockLock(sc->sc_sae_engine_lock);
    owner = &sc->sc_sae_engine_owner;
    if (owner->active) {
        result = IWX_SAE_ASSOC_TX_REJECTED;
        if (owner->completion_claimed && owner->assoc_tx_pending &&
            !owner->assoc_tx_accepted &&
            iwx_sae_engine_owner_matches_pmk_identity_locked(sc, owner,
            &owner->completion) &&
            iwx_sae_engine_assoc_tx_frame_matches_identity(wh,
            &owner->completion) &&
            ieee80211_sae_wcl_request_pmk_claim_assoc_current_locked(
            &sc->sc_ic, ni, &owner->completion)) {
            claim->identity = owner->completion;
            claim->active = true;
            result = IWX_SAE_ASSOC_TX_ADMITTED;
        } else if (iwx_sae_engine_assoc_tx_frame_matches_selected(wh,
            owner)) {
            /* This is an invalid request for the current owner, not a stale
             * descriptor from a former BSS.  Retire only its exact
             * generation; never use the broad generation-zero cancel. */
            schedule = iwx_sae_engine_mark_cancelled_locked(sc,
                owner->request_generation, false, &cancel);
        }
    }
    IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    IOSimpleLockUnlockEnableInterrupt(bss_lock, irq);
    if (schedule)
        iwx_sae_engine_schedule_task(sc);
    explicit_bzero(&cancel, sizeof(cancel));
    return result;
}

/* Final Association Request linearization.  The generic mgtq can delay this
 * far beyond S_ASSOC, so no earlier PMK claim is sufficient: the exact same
 * public identity must still be live when scheduler state and WRPTR become
 * visible to firmware. */
static bool
iwx_sae_engine_assoc_tx_commit(struct iwx_softc *sc,
    struct iwx_tx_ring *ring, int descriptor_idx, uint8_t station_id,
    uint16_t length, const struct ieee80211_node *ni,
    const struct IwxSaeAssocTxClaim *claim)
{
    struct iwx_sae_engine_owner *owner;
    struct IwxSaeEngineCancellation cancel;
    IOSimpleLock *bss_lock;
    IOInterruptState irq;
    bool committed = false;
    bool schedule = false;

    (void)station_id;
    (void)length;

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
    if (iwx_sae_engine_pmk_identity_equal(&owner->completion,
        &claim->identity)) {
        if (owner->completion_claimed && owner->assoc_tx_pending &&
            !owner->assoc_tx_accepted &&
            iwx_sae_engine_owner_matches_pmk_identity_locked(sc, owner,
            &claim->identity) &&
            iwx_sae_engine_owner_matches_node_locked(sc, &sc->sc_ic, ni) &&
            ieee80211_sae_wcl_request_pmk_claim_assoc_current_locked(
            &sc->sc_ic, ni, &claim->identity) &&
            ring->ring_count != 0 && ring->cur == descriptor_idx) {
            const int next_cur =
                (descriptor_idx + 1) % ring->ring_count;

            ring->cur = next_cur;
            IWX_WRITE(sc, IWX_HBUS_TARG_WRPTR,
                ring->qid << 16 | ring->cur);
            owner->assoc_tx_pending = false;
            owner->assoc_tx_accepted = true;
            committed = true;
        } else {
            /* The matching owner lost one of its current-BSS/PMK fences
             * before the doorbell.  Let the sole worker scrub and scan. */
            schedule = iwx_sae_engine_mark_cancelled_locked(sc,
                owner->request_generation, false, &cancel);
        }
    }
    IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    IOSimpleLockUnlockEnableInterrupt(bss_lock, irq);
    if (schedule)
        iwx_sae_engine_schedule_task(sc);
    explicit_bzero(&cancel, sizeof(cancel));
    return committed;
}

/* All calls below are after the owner leaf is released: generic selected-BSS
 * state, the private credential leaf, the TX gate and taskq must never nest
 * below the engine leaf. */
static void
iwx_sae_engine_cancel_owned(struct iwx_softc *sc,
    const struct IwxSaeEngineCancellation *cancel)
{
    ItlIwx *that;

    if (sc == NULL || cancel == NULL || !cancel->active)
        return;
    that = container_of(sc, ItlIwx, com);
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
iwx_sae_engine_queue_terminal(struct iwx_softc *sc,
    const struct ItlSaeAuthTransportEventV1 *event)
{
    struct iwx_sae_engine_owner *owner;
    bool consumed = false;

    if (sc == NULL || event == NULL || sc->sc_sae_engine_lock == NULL ||
        !itl_sae_auth_transport_event_is_well_formed(event))
        return false;
    IOSimpleLockLock(sc->sc_sae_engine_lock);
    owner = &sc->sc_sae_engine_owner;
    if (iwx_sae_engine_owner_matches_terminal_locked(sc, event)) {
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
            iwx_sae_engine_queue_wcl_cancel_locked(sc,
                owner->request_generation);
            iwx_sae_engine_fence_inflight_ticket_locked(sc, owner);
        } else {
            owner->terminal = *event;
            owner->terminal_valid = true;
        }
    }
    IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    if (consumed)
        iwx_sae_engine_schedule_task(sc);
    return consumed;
}

/* Returns 1 when a matching owner copied the peer value, -1 when a live
 * owner consumes a late/invalid/overflow value, and 0 only when no direct
 * owner exists so the historical controller route may still decide. */
static int
iwx_sae_engine_queue_peer(struct iwx_softc *sc,
    const struct ItlSaeAuthPeerEventV1 *event)
{
    struct iwx_sae_engine_owner *owner;
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
        matching_peer = iwx_sae_engine_owner_matches_peer_locked(sc, event);
        current_owner = iwx_sae_engine_peer_owner_current_locked(sc, owner);
        if (matching_peer && current_owner && !owner->cancelled &&
            owner->peer_count < IWX_SAE_ENGINE_PEERQ_LEN) {
            owner->peerq[owner->peer_tail] = *event;
            owner->peer_tail = (owner->peer_tail + 1) %
                IWX_SAE_ENGINE_PEERQ_LEN;
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
            iwx_sae_engine_queue_wcl_cancel_locked(sc,
                owner->request_generation);
            iwx_sae_engine_fence_inflight_ticket_locked(sc, owner);
        }
    }
    IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    IOSimpleLockUnlockEnableInterrupt(bss_lock, irq);
    if (result != 0)
        iwx_sae_engine_schedule_task(sc);
    return result;
}

/* Writers share generic's selected-BSS leaf so a request cannot observe a
 * partial four-hook owner.  The independent callback lease protects a hook
 * copied immediately before this writer removes it. */
static bool
iwx_sae_engine_publish_hooks(struct iwx_softc *sc, bool enabled,
    bool retain_auth_tombstone, u_int32_t expected_generation)
{
    struct ieee80211com *ic;
    IOSimpleLock *lock;
    IOInterruptState irq;
    bool retain_owner = false;

    if (sc == NULL)
        return false;
    if (enabled && (!iwx_sae_engine_runtime_enabled(sc) ||
        expected_generation == 0 ||
        __atomic_load_n(&sc->sc_sae_engine_lifecycle_generation,
            __ATOMIC_ACQUIRE) != expected_generation ||
        (__atomic_load_n(&sc->sc_sae_engine_callback_state,
            __ATOMIC_ACQUIRE) & IWX_SAE_ENGINE_CALLBACK_CLOSED) != 0))
        return false;
    ic = &sc->sc_ic;
    lock = ic->ic_pae_selected_bss_lock;
    if (lock != NULL)
        irq = IOSimpleLockLockDisableInterrupt(lock);
    /* The second token check is under the same generic hook writer leaf as
     * publication.  A stop which won in between leaves its own unpublish
     * authoritative; this older reopen never resurrects a hook. */
    if (enabled && (!iwx_sae_engine_runtime_enabled(sc) ||
        __atomic_load_n(&sc->sc_sae_engine_lifecycle_generation,
            __ATOMIC_ACQUIRE) != expected_generation ||
        (__atomic_load_n(&sc->sc_sae_engine_callback_state,
            __ATOMIC_ACQUIRE) & IWX_SAE_ENGINE_CALLBACK_CLOSED) != 0)) {
        if (lock != NULL)
            IOSimpleLockUnlockEnableInterrupt(lock, irq);
        return false;
    }
    if (enabled) {
        ic->ic_sae_auth_hold = ItlIwx::iwx_sae_auth_hold;
        ic->ic_sae_auth_owned = ItlIwx::iwx_sae_auth_owned;
        ic->ic_sae_engine_peer_event = ItlIwx::iwx_sae_engine_peer_event;
        ic->ic_sae_wcl_request_revoke = ItlIwx::iwx_sae_wcl_request_revoke;
        ic->ic_sae_roam_port_valid = ItlIwx::iwx_sae_roam_port_valid;
        ic->ic_sae_wnm_roam_start = ItlIwx::iwx_sae_wnm_roam_start;
        ic->ic_sae_wcl_roam_start = ItlIwx::iwx_sae_wcl_roam_start;
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
            ItlIwx::iwx_sae_auth_owned : NULL;
        ic->ic_sae_engine_peer_event = NULL;
        ic->ic_sae_wcl_request_revoke = NULL;
        ic->ic_sae_roam_port_valid = NULL;
        ic->ic_sae_wnm_roam_start = NULL;
        ic->ic_sae_wcl_roam_start = NULL;
    }
    if (lock != NULL)
        IOSimpleLockUnlockEnableInterrupt(lock, irq);
    return enabled;
}

} // namespace

int ItlIwx::
iwx_sae_auth_owned(struct ieee80211com *ic,
    const struct ieee80211_node *ni)
{
    struct iwx_softc *sc;
    int owned = 0;

    if (ic == NULL || ni == NULL)
        return 0;
    sc = (struct iwx_softc *)ic->ic_softc;
    /* A selected-BSS reader may have copied this hook immediately before
     * stop/detach closes it.  Returning ownership on that closed edge is a
     * fail-closed tombstone: a late Open response must not revive S_AUTH. */
    if (!iwx_sae_engine_callback_enter(sc))
        return 1;
    if (iwx_sae_engine_runtime_enabled(sc) &&
        sc->sc_sae_engine_lock != NULL) {
        IOSimpleLockLock(sc->sc_sae_engine_lock);
        owned = iwx_sae_engine_owner_matches_node_locked(sc, ic, ni) ? 1 : 0;
        IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    }
    iwx_sae_engine_callback_leave(sc);
    return owned;
}

int ItlIwx::
iwx_sae_engine_peer_event(struct ieee80211com *ic,
    const struct ItlSaeAuthPeerEventV1 *event)
{
    struct iwx_softc *sc;
    int result = 0;

    if (ic == NULL || event == NULL)
        return 0;
    sc = (struct iwx_softc *)ic->ic_softc;
    /* Do not turn a copied hook which lost the close race into a controller
     * fallback.  -1 is the generic contract's consumed/rejected result. */
    if (!iwx_sae_engine_callback_enter(sc))
        return -1;
    if (iwx_sae_engine_runtime_enabled(sc))
        result = iwx_sae_engine_queue_peer(sc, event);
    iwx_sae_engine_callback_leave(sc);
    return result;
}

void ItlIwx::
iwx_sae_wcl_request_revoke(struct ieee80211com *ic,
    u_int64_t request_generation)
{
    struct iwx_softc *sc;
    struct IwxSaeEngineCancellation cancel;

    if (ic == NULL || request_generation == 0)
        return;
    sc = (struct iwx_softc *)ic->ic_softc;
    if (!iwx_sae_engine_callback_enter(sc))
        return;
    explicit_bzero(&cancel, sizeof(cancel));
    (void)iwx_sae_engine_mark_cancelled(sc, request_generation, false,
        &cancel);
    /* This callback is admitted from generic/RX-adjacent paths.  It has
     * recorded all public cancellation identity under an interrupt-safe leaf
     * and must not take the sleeping TX lifecycle lock or WCL leaf here. */
    iwx_sae_engine_schedule_task(sc);
    explicit_bzero(&cancel, sizeof(cancel));
    iwx_sae_engine_callback_leave(sc);
}

void ItlIwx::
iwx_sae_roam_port_valid(struct ieee80211com *ic,
    const struct ieee80211_node *ni)
{
    struct iwx_softc *sc;
    bool exact_sae_ess;
    bool identity_matches;

    if (ic == NULL || ni == NULL)
        return;
    sc = (struct iwx_softc *)ic->ic_softc;
    if (!iwx_sae_engine_callback_enter(sc))
        return;
    if (!iwx_sae_engine_runtime_enabled(sc) ||
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
        XYLog("iwx_sae_roam ACTIVE_ESS_CREDENTIAL\n");
    } else if (sc->sc_sae_wcl_credential_pending ||
        (sc->sc_sae_wcl_credential_active &&
         (!exact_sae_ess || !identity_matches))) {
        iwx_sae_wcl_credential_clear_locked(sc);
    }
    IOSimpleLockUnlock(sc->sc_sae_wcl_credential_lock);
out:
    iwx_sae_engine_callback_leave(sc);
}

int ItlIwx::
iwx_sae_targeted_roam_start(struct ieee80211com *ic,
    const struct ieee80211_node *source,
    const u_int8_t target_bssid[IEEE80211_ADDR_LEN], bool consume_wnm)
{
    struct iwx_softc *sc;
    ItlIwx *that;
    struct ItlSaeWclCredentialV1 credential;
    struct ieee80211_node *candidate;
    u_int8_t source_ssid[IEEE80211_NWID_LEN];
    u_int8_t source_ssid_len = 0;
    u_int64_t generation = 0;
    bool active_copied = false;
    bool transitioned = false;
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
    sc = (struct iwx_softc *)ic->ic_softc;
    if (!iwx_sae_engine_callback_enter(sc))
        goto out;
    that = container_of(sc, ItlIwx, com);

    source_ssid_len = source->ni_esslen;
    memcpy(source_ssid, source->ni_essid, source_ssid_len);
    if (!iwx_sae_engine_runtime_enabled(sc) ||
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

    /* The reference starts a bounded firmware roam scan rather than sending
     * a same-BSS reassociation frame.  IWX has no matching firmware command,
     * so cross the association epoch and retarget the retained ESS secret to
     * the already selected scan candidate exactly as the IWN host owner does. */
    ieee80211_new_state(ic, IEEE80211_S_SCAN,
        IEEE80211_NEWSTATE_ARG_WNM_RECONNECT_HOLD);
    transitioned = ic->ic_state == IEEE80211_S_SCAN;
    if (!transitioned)
        goto leave;

    generation = ieee80211_sae_wcl_request_begin(ic, target_bssid,
        source_ssid, source_ssid_len);
    if (generation == 0)
        goto leave;
    credential.request_generation = generation;
    IEEE80211_ADDR_COPY(credential.bssid, target_bssid);
    if (!itl_sae_wcl_credential_is_well_formed(&credential) ||
        that->stageSaeWclCredential(&credential) != kIOReturnSuccess ||
        !(consume_wnm ?
          ieee80211_sae_wcl_request_admit_confirmed_wnm_candidate(
              ic, generation) :
          ieee80211_sae_wcl_request_admit_cached_roam_candidate(
              ic, generation, target_bssid, source_ssid,
              source_ssid_len)))
        goto leave;

    candidate = ieee80211_find_node(ic, target_bssid);
    if (candidate == NULL || candidate == ic->ic_bss ||
        candidate->ni_fails != 0 ||
        candidate->ni_chan == IEEE80211_CHAN_ANYC ||
        candidate->ni_esslen != ic->ic_des_esslen ||
        memcmp(candidate->ni_essid, ic->ic_des_essid,
            ic->ic_des_esslen) != 0 ||
        ieee80211_match_bss(ic, candidate, 0) != 0)
        goto leave;

    ieee80211_node_join_bss(ic, candidate);
    if (!ieee80211_sae_wcl_request_bound_current(ic, ic->ic_bss))
        goto leave;
    if (consume_wnm)
        ieee80211_wnm_bss_transition_consume(ic, source_ssid,
            source_ssid_len, target_bssid);
    XYLog("iwx_sae_roam DRIVER_RESIDENT_%s_STARTED\n",
        consume_wnm ? "BTM" : "WCL");
    started = 1;
leave:
    if (!started && generation != 0) {
        (void)ieee80211_sae_wcl_request_clear_if_generation(ic,
            generation);
        that->cancelSaeWclCredential(generation);
    }
    explicit_bzero(&credential, sizeof(credential));
    iwx_sae_engine_callback_leave(sc);
out:
    explicit_bzero(&credential, sizeof(credential));
    explicit_bzero(source_ssid, sizeof(source_ssid));
    (void)transitioned;
    return started;
}

int ItlIwx::
iwx_sae_wnm_roam_start(struct ieee80211com *ic,
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
    started = iwx_sae_targeted_roam_start(ic, source, target_bssid, true);
out:
    explicit_bzero(source_ssid, sizeof(source_ssid));
    explicit_bzero(target_bssid, sizeof(target_bssid));
    return started;
}

int ItlIwx::
iwx_sae_wcl_roam_start(struct ieee80211com *ic,
    const struct ieee80211_node *source,
    const u_int8_t target_bssid[IEEE80211_ADDR_LEN])
{
    return iwx_sae_targeted_roam_start(ic, source, target_bssid, false);
}

int ItlIwx::
iwx_sae_auth_hold(struct ieee80211com *ic, struct ieee80211_node *ni,
    enum ieee80211_state /*oldstate*/, int /*mgt*/)
{
    struct iwx_softc *sc;
    struct ieee80211_sae_wcl_bound_request bound;
    struct ItlSaeSelectedJoinEventV1 selected;
    struct ItlSaeAuthActivatedEventV1 activated;
    struct IwxSaeEngineCancellation cancel;
    struct iwx_sae_engine_owner *owner;
    u_int64_t relay_generation = 0;
    int held = 0;

    explicit_bzero(&bound, sizeof(bound));
    explicit_bzero(&selected, sizeof(selected));
    explicit_bzero(&activated, sizeof(activated));
    explicit_bzero(&cancel, sizeof(cancel));
    if (ic == NULL || ni == NULL)
        goto out;
    sc = (struct iwx_softc *)ic->ic_softc;
    if (!iwx_sae_engine_callback_enter(sc)) {
        /* generic may have copied auth_hold immediately before a stop/detach
         * close.  Preserve S_AUTH until the already-scheduled cancellation
         * edge runs; returning zero here would fall through to Open-System. */
        if (sc != NULL && (__atomic_load_n(
            &sc->sc_sae_engine_callback_state, __ATOMIC_ACQUIRE) &
            IWX_SAE_ENGINE_CALLBACK_CLOSED) != 0)
            held = 1;
        goto out;
    }
    if (!iwx_sae_engine_runtime_enabled(sc) ||
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
        (void)iwx_sae_engine_mark_cancelled(sc, bound.generation, false,
            &cancel);
        /* The reservation has already held S_AUTH.  Whether a concurrent
         * revoke won the owner leaf or this admission check did, give the
         * sole worker a chance to retire it and scan away. */
        iwx_sae_engine_schedule_task(sc);
    } else {
        iwx_sae_engine_schedule_task(sc);
    }
leave:
    iwx_sae_engine_callback_leave(sc);
out:
    explicit_bzero(&cancel, sizeof(cancel));
    explicit_bzero(&activated, sizeof(activated));
    explicit_bzero(&selected, sizeof(selected));
    explicit_bzero(&bound, sizeof(bound));
    return held;
}

namespace {

static bool
iwx_sae_engine_selected_matches_bound(
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

enum IwxSaeEngineSubmitResult {
    IWX_SAE_ENGINE_SUBMIT_OK = 0,
    /* The private workloop gate rejected us before a doorbell.  The engine
     * rollback is therefore exact and one bounded deferred retry is safe. */
    IWX_SAE_ENGINE_SUBMIT_RETRY = 1,
    IWX_SAE_ENGINE_SUBMIT_FAIL = -1,
};

/* Worker-only: materialize one prepared engine frame, then send it through
 * the existing IWX gate/descriptor/doorbell path.  A submit failure is known
 * to be pre-doorbell, so the engine's rollback API is safe; this owner then
 * takes at most one bounded retry for a private-gate busy result and fails
 * closed for every ambiguous radio state. */
static int
iwx_sae_engine_submit_prepared(struct iwx_softc *sc)
{
    struct iwx_sae_engine_owner *owner;
    struct ItlSaeAuthTxRequestV1 request;
    struct ieee80211_sae_engine *engine;
    ItlIwx *that;
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
        IWX_SAE_ENGINE_TICKET_COUNTER_MASK) {
        ticket = IWX_SAE_ENGINE_TICKET_DIRECT_BIT |
            ++sc->sc_sae_engine_next_ticket;
        owner->in_flight_ticket = ticket;
    }
    IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    if (ticket == 0 || ieee80211_sae_engine_prepare_tx(engine, ticket,
        &request) != 0)
        goto fail;

    that = container_of(sc, ItlIwx, com);
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
            iwx_sae_engine_owner_matches_node_locked(sc, &sc->sc_ic,
                sc->sc_ic.ic_bss);
        IOSimpleLockUnlock(sc->sc_sae_engine_lock);
        if (rearm_mgt_timer)
            sc->sc_ic.ic_mgt_timer = IEEE80211_TRANS_WAIT;
        explicit_bzero(&request, sizeof(request));
        return IWX_SAE_ENGINE_SUBMIT_OK;
    }
    (void)ieee80211_sae_engine_tx_rollback_unsubmitted(engine, ticket);
fail:
    IOSimpleLockLock(sc->sc_sae_engine_lock);
    owner = &sc->sc_sae_engine_owner;
    if (owner->active && owner->in_flight_ticket == ticket)
        owner->in_flight_ticket = 0;
    IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    explicit_bzero(&request, sizeof(request));
    return rc == kIOReturnNotReady ? IWX_SAE_ENGINE_SUBMIT_RETRY :
        IWX_SAE_ENGINE_SUBMIT_FAIL;
}

/* Worker-only start: two current-bound checks bracket private credential
 * consumption.  Thus a cancellation that wins either before or during the
 * bounded password copy cannot cause a Commit for a former BSS/request. */
static int
iwx_sae_engine_start(struct iwx_softc *sc)
{
    struct ieee80211com *ic;
    struct iwx_sae_engine_owner *owner;
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
    if (!iwx_sae_engine_selected_matches_bound(&selected, &bound))
        goto out;
    if (!iwx_sae_wcl_credential_take_bound(sc, &selected, &credential))
        goto out;
    if (!ieee80211_sae_wcl_request_copyout_bound_current(ic, generation,
            &bound))
        goto out;
    if (!iwx_sae_engine_selected_matches_bound(&selected, &bound))
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
        result = iwx_sae_engine_submit_prepared(sc);
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
 * only iwx_init() is authorized to reopen hardware lifecycle admission. */
static void
iwx_sae_engine_reopen_if_current(struct iwx_softc *sc,
    u_int32_t expected_generation)
{
    struct iwx_sae_engine_owner *owner;
    u_int32_t callback_state;
    bool may_publish = false;

    if (sc == NULL || expected_generation == 0 ||
        !iwx_sae_engine_runtime_enabled(sc) ||
        !iwx_sae_task_lifecycle_is_open(sc))
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
    if ((callback_state & IWX_SAE_ENGINE_CALLBACK_CLOSED) != 0)
        return;
    (void)iwx_sae_engine_publish_hooks(sc, true, false,
        expected_generation);
}

/* This runs only on sae_engine_task after detach has drained that task.  It
 * may call generic state transitions only after it has scrubbed every local
 * SAE identity and released the engine leaf. */
static void
iwx_sae_engine_worker_retire(struct iwx_softc *sc, bool request_scan)
{
    struct iwx_sae_engine_owner *owner;
    struct ieee80211_sae_engine *engine = NULL;
    struct IwxSaeEngineCancellation cancel;
    struct IwxSaeEngineCancellation transport_cancel;
    bool suppress_scan = true;
    bool issue_scan = false;
    bool reopen_hooks = false;
    u_int32_t reopen_generation = 0;

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
    iwx_sae_engine_cancel_owned(sc, &transport_cancel);
    explicit_bzero(&transport_cancel, sizeof(transport_cancel));
    if (request_scan && cancel.active)
        iwx_sae_wcl_credential_retire_pending_generation(sc,
            cancel.request_generation);
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
        iwx_sae_engine_owner_clear_locked(sc);
    } else if (!owner->active && !sc->sc_sae_engine_stopping &&
        !sc->sc_sae_engine_detaching) {
        /* A start failure before publishing an owner has no tombstone. */
        iwx_sae_engine_owner_clear_locked(sc);
    }
    reopen_hooks = !sc->sc_sae_engine_stopping &&
        !sc->sc_sae_engine_detaching && !owner->active &&
        sc->sc_sae_engine == NULL;
    if (reopen_hooks)
        reopen_generation = __atomic_load_n(
            &sc->sc_sae_engine_lifecycle_generation, __ATOMIC_ACQUIRE);
    IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    if (reopen_hooks)
        iwx_sae_engine_reopen_if_current(sc, reopen_generation);
    explicit_bzero(&cancel, sizeof(cancel));
    explicit_bzero(&transport_cancel, sizeof(transport_cancel));
}

} // namespace

void ItlIwx::
iwx_sae_engine_task(void *arg)
{
    struct iwx_softc *sc = (struct iwx_softc *)arg;
    ItlIwx *that;
    struct iwx_sae_engine_owner *owner;
    struct ItlSaeAuthTransportEventV1 terminal;
    struct ItlSaeAuthPeerEventV1 peer;
    struct ItlSaePmkContinuationV1 continuation;
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
    int submit_result = IWX_SAE_ENGINE_SUBMIT_OK;

    if (sc == NULL)
        return;
    that = container_of(sc, ItlIwx, com);
    if (!that->iwx_task_gate_enter(sc, false))
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
                IWX_SAE_ENGINE_PEERQ_LEN;
            owner->peer_count--;
            have_peer = true;
        }
    }
    IOSimpleLockUnlock(sc->sc_sae_engine_lock);

    if (wcl_cancel_generation != 0) {
        ItlIwx *that = container_of(sc, ItlIwx, com);
        that->cancelSaeWclCredential(wcl_cancel_generation);
    }

    if (cancel) {
        iwx_sae_engine_worker_retire(sc, true);
        goto out;
    }
    /* The engine remains alive until generic's asynchronously queued
     * Association Request has crossed the real descriptor doorbell.  Only
     * that accepted doorbell may retire the direct SAE owner without a scan. */
    if (assoc_tx_accepted) {
        iwx_sae_engine_worker_retire(sc, false);
        goto out;
    }
    if (start || retry_submit) {
        /* attemptAction() can reject a task solely because the private IWX
         * gate is occupied.  That is pre-doorbell and gets only this short,
         * bounded deferred retry; all other submission failures retire. */
        if (retry_submit)
            IOSleep(1);
        submit_result = start ? iwx_sae_engine_start(sc) :
            iwx_sae_engine_submit_prepared(sc);
    } else if (have_terminal) {
        IOSimpleLockLock(sc->sc_sae_engine_lock);
        engine = sc->sc_sae_engine;
        IOSimpleLockUnlock(sc->sc_sae_engine_lock);
        if (engine == NULL || ieee80211_sae_engine_tx_complete(engine,
            &terminal) != 0) {
            fail = true;
        } else {
            if (terminal.phase == kItlSaeAuthTransportPhaseCommit) {
                IWX_DIRECT_SAE_TRACE(&sc->sc_ic,
                    kAirportItlwmPostPltiTraceEventIwxDirectSaeCommitTxComplete);
            } else if (terminal.phase == kItlSaeAuthTransportPhaseConfirm) {
                IWX_DIRECT_SAE_TRACE(&sc->sc_ic,
                    kAirportItlwmPostPltiTraceEventIwxDirectSaeConfirmTxComplete);
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
                    IWX_DIRECT_SAE_TRACE(&sc->sc_ic,
                        kAirportItlwmPostPltiTraceEventIwxDirectSaePeerCommitAccepted);
                submit_result = iwx_sae_engine_submit_prepared(sc);
            } else if (peer_result == IEEE80211_SAE_ENGINE_PEER_COMPLETE) {
                IOSimpleLock *bss_lock;
                IOInterruptState irq;
                bool pmk_claimed = false;
                bool assoc_started = false;

                /* `PEER_COMPLETE` is the in-kext engine's verified peer
                 * Confirm boundary; it carries no value out of the engine. */
                IWX_DIRECT_SAE_TRACE(&sc->sc_ic,
                    kAirportItlwmPostPltiTraceEventIwxDirectSaePeerConfirmValidated);
                /* The PMK and scalar-derived SAE PMKID came from the same
                 * accepted in-kext exchange.  Recheck the bounded record at
                 * the IWX/net80211 boundary before either value can enter
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
                        iwx_sae_engine_owner_matches_pmk_identity_locked(sc,
                        owner, &continuation.identity) &&
                        iwx_sae_engine_peer_owner_current_locked(sc, owner) &&
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
                        IWX_DIRECT_SAE_TRACE(&sc->sc_ic,
                            kAirportItlwmPostPltiTraceEventIwxDirectSaePmkClaimed);
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
    if (submit_result == IWX_SAE_ENGINE_SUBMIT_RETRY) {
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
            iwx_sae_engine_schedule_task(sc);
            goto out;
        }
        fail = true;
    } else if (submit_result != IWX_SAE_ENGINE_SUBMIT_OK) {
        fail = true;
    }
    if (fail) {
        iwx_sae_engine_worker_retire(sc, true);
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
        iwx_sae_engine_schedule_task(sc);
out:
    explicit_bzero(&continuation, sizeof(continuation));
    explicit_bzero(&peer, sizeof(peer));
    explicit_bzero(&terminal, sizeof(terminal));
    that->iwx_task_gate_leave(sc);
}

/* Stop may be reached from IWX notification/IRQ-adjacent paths.  It closes
 * admission, records cancellation and queues the worker only; no generic
 * revoke, WCL leaf, workloop gate, sleep or task barrier is permitted here. */
void ItlIwx::
iwx_sae_engine_stop_begin(struct iwx_softc *sc)
{
    struct IwxSaeEngineCancellation cancel;
    bool schedule = false;

    if (sc == NULL)
        return;
    explicit_bzero(&cancel, sizeof(cancel));
    if (sc->sc_sae_engine_lock != NULL) {
        IOSimpleLockLock(sc->sc_sae_engine_lock);
        sc->sc_sae_engine_stopping = true;
        (void)iwx_sae_engine_generation_advance_locked(sc);
        (void)iwx_sae_engine_mark_cancelled_locked(sc, 0, true, &cancel);
        schedule = sc->sc_sae_engine_task_ready;
        IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    }
    /* Cancellation/fence linearizes before close.  A hook which had already
     * entered can now only observe the cancelled owner, never progress it. */
    iwx_sae_engine_callback_close(sc);
    (void)iwx_sae_engine_publish_hooks(sc, false, true, 0);
    if (schedule)
        iwx_sae_engine_schedule_task(sc);
    explicit_bzero(&cancel, sizeof(cancel));
}

/* This is called only after a completed hardware init.  It is the sole
 * lifecycle path allowed to clear the stop flag and reopen a closed hook
 * lease; deferred engine retirement uses reopen_if_current() above. */
void ItlIwx::
iwx_sae_engine_reopen(struct iwx_softc *sc)
{
    struct iwx_sae_engine_owner *owner;
    u_int32_t generation = 0;
    u_int32_t callback_state;
    bool pending_retire = false;
    bool may_publish = false;
    bool may_open_callback = false;

    if (sc == NULL)
        return;
    if (!iwx_sae_engine_runtime_enabled(sc) ||
        !iwx_sae_task_lifecycle_is_open(sc)) {
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
        iwx_sae_engine_callback_close(sc);
        (void)iwx_sae_engine_publish_hooks(sc, false, true, 0);
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
    if ((callback_state & IWX_SAE_ENGINE_CALLBACK_CLOSED) != 0) {
        iwx_sae_engine_callback_drain(sc);
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
                may_open_callback = iwx_sae_engine_callback_open(sc);
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
        iwx_sae_engine_schedule_task(sc);
        return;
    }
    if (may_publish)
        (void)iwx_sae_engine_publish_hooks(sc, true, false, generation);
}

/* Detach is process context and executes before the IWX TX gate/lifecycle is
 * removed.  The engine worker can therefore retire a doorbelled descriptor,
 * revoke peer RX and scrub staged WCL state before the final task barrier
 * permits its private engine/leaf to disappear. */
void ItlIwx::
iwx_sae_engine_detach_begin(struct iwx_softc *sc)
{
    struct IwxSaeEngineCancellation cancel;
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
        (void)iwx_sae_engine_generation_advance_locked(sc);
        (void)iwx_sae_engine_mark_cancelled_locked(sc, 0, true, &cancel);
        task_ready = sc->sc_sae_engine_task_ready;
        IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    }
    /* Close copied generic hooks before task admission.  Otherwise a hook
     * which enters in the small CLOSED-task window sees runtime disabled and
     * could return the legacy-fallback value instead of the fail-closed SAE
     * tombstone result. */
    iwx_sae_engine_callback_close(sc);
    /* Detaching is visible before task admission closes, so a stale init
     * worker cannot interpret CLOSED as a normal runtime-unavailable state
     * and withdraw the S_AUTH tombstone.  A producer which already won this
     * lease finishes task_add before task_del()+barrier below. */
    iwx_sae_engine_task_admission_close(sc);
    iwx_sae_engine_task_admission_drain(sc);
    if (sc->sc_sae_engine_lock != NULL) {
        IOSimpleLockLock(sc->sc_sae_engine_lock);
        /* Enqueue admission is closed and drained above, so no producer can
         * add the task after the task_del()+barrier snapshot below. */
        sc->sc_sae_engine_task_ready = false;
        IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    }
    (void)iwx_sae_engine_publish_hooks(sc, false, true, 0);
    /* Callback drain protects copied generic/TX hook bodies; the independent
     * task-admission drain above already fenced every task_add before the
     * task_del()+barrier snapshot below. */
    iwx_sae_engine_callback_drain(sc);
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
        iwx_sae_engine_owner_clear_locked(sc);
        IOSimpleLockUnlock(sc->sc_sae_engine_lock);
    }
    if (engine != NULL)
        ieee80211_sae_engine_destroy(&engine);
    if (cancel.active)
        iwx_sae_engine_cancel_owned(sc, &cancel);
    if (wcl_cancel_generation != 0) {
        ItlIwx *that = container_of(sc, ItlIwx, com);
        that->cancelSaeWclCredential(wcl_cancel_generation);
    }
    explicit_bzero(&cancel, sizeof(cancel));
}
