/*
 * IWM firmware has no reliable PMF-key command path.  A negotiated PMF
 * association therefore uses the existing net80211 software CCMP/BIP
 * implementations.  These helpers own only the asynchronous *preparation*
 * of software CCMP contexts; the generic PAE transaction remains the sole
 * publisher of live keys and protection flags.
 */
static bool
iwm_mfp_pae_stage_valid(u_int8_t stage)
{
    return stage == IEEE80211_PAE_MFP_STAGE_PTK ||
        stage == IEEE80211_PAE_MFP_STAGE_GTK ||
        stage == IEEE80211_PAE_MFP_STAGE_IGTK;
}

static u_int8_t
iwm_mfp_pae_stage_mask(u_int8_t stage)
{
    if (!iwm_mfp_pae_stage_valid(stage))
        return 0;
    return (u_int8_t)(1U << (stage - IEEE80211_PAE_MFP_STAGE_PTK));
}

static bool
iwm_mfp_pae_key_valid(const struct ieee80211_key *key, u_int8_t stage)
{
    if (key == NULL || !iwm_mfp_pae_stage_valid(stage) ||
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

/* Caller holds the selected-BSS fence and the IWM PMF owner lock.  The final
 * fact is deliberately stricter than a successful callback: it describes the
 * exact live software-CCMP plus BIP descriptor state after the atomic generic
 * publication, without exporting any descriptor contents. */
static bool
iwm_mfp_pae_software_keyset_live_locked(struct ieee80211com *ic,
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
iwm_mfp_pae_dispose_key(struct ieee80211com *ic, struct ieee80211_key *key)
{
    if (key == NULL)
        return;
    if (key->k_priv != NULL)
        ieee80211_delete_key(ic, NULL, key);
    else
        explicit_bzero(key, sizeof(*key));
}

#define IWM_MFP_PAE_CALLBACK_COUNT_MASK  0x7fffffffU

/* The generic owner snapshots callback pointers before it drops its
 * selected-BSS leaf.  A detach can therefore race a just-copied IWM hook.
 * This lease is deliberately atomic on admission: the hook may be called
 * from an interrupt-adjacent path, while only detach is allowed to sleep.
 * Close makes a stale snapshot that has not entered yet fast-fail; drain
 * waits only for calls which have already incremented the active count. */
static bool
iwm_mfp_pae_callback_enter(struct iwm_softc *sc)
{
    u_int32_t state, next;

    if (sc == NULL)
        return false;
    state = __atomic_load_n(&sc->sc_mfp_pae_callback_state,
        __ATOMIC_ACQUIRE);
    for (;;) {
        if ((state & IWM_MFP_PAE_CALLBACK_CLOSED) != 0 ||
            (state & IWM_MFP_PAE_CALLBACK_COUNT_MASK) ==
            IWM_MFP_PAE_CALLBACK_COUNT_MASK)
            return false;
        next = state + 1;
        if (__atomic_compare_exchange_n(&sc->sc_mfp_pae_callback_state,
            &state, next, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return true;
    }
}

static void
iwm_mfp_pae_callback_leave(struct iwm_softc *sc)
{
    u_int32_t state, next;

    if (sc == NULL)
        return;
    /* RX-side completion must not block on a detach mutex.  The closed
     * detach owner polls this atomic count, so no wakeup can be lost. */
    state = __atomic_load_n(&sc->sc_mfp_pae_callback_state,
        __ATOMIC_ACQUIRE);
    for (;;) {
        if ((state & IWM_MFP_PAE_CALLBACK_COUNT_MASK) == 0)
            return;
        next = state - 1;
        if (__atomic_compare_exchange_n(&sc->sc_mfp_pae_callback_state,
            &state, next, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return;
    }
}

static void
iwm_mfp_pae_callback_close(struct iwm_softc *sc)
{
    u_int32_t state, next;

    if (sc == NULL)
        return;
    state = __atomic_load_n(&sc->sc_mfp_pae_callback_state,
        __ATOMIC_ACQUIRE);
    for (;;) {
        if ((state & IWM_MFP_PAE_CALLBACK_CLOSED) != 0)
            return;
        next = state | IWM_MFP_PAE_CALLBACK_CLOSED;
        if (__atomic_compare_exchange_n(&sc->sc_mfp_pae_callback_state,
            &state, next, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return;
    }
}

static bool
iwm_mfp_pae_callback_open(struct iwm_softc *sc)
{
    u_int32_t expected = IWM_MFP_PAE_CALLBACK_CLOSED;

    if (sc == NULL)
        return false;
    return __atomic_compare_exchange_n(&sc->sc_mfp_pae_callback_state,
        &expected, 0, false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE);
}

static void
iwm_mfp_pae_callback_drain(struct iwm_softc *sc)
{
    if (sc == NULL)
        return;
    /* Close rejects every new enter.  A short sleep/recheck is intentional:
     * it avoids a blocking callback-side wakeup and therefore cannot miss a
     * final decrement between a test and a wait. */
    while ((__atomic_load_n(&sc->sc_mfp_pae_callback_state,
        __ATOMIC_ACQUIRE) & IWM_MFP_PAE_CALLBACK_COUNT_MASK) != 0)
        IOSleep(1);
}

static void
iwm_mfp_pae_callback_destroy(struct iwm_softc *sc)
{
    if (sc == NULL)
        return;
    /* This does not claim to own arbitrary producers.  IWM detach removes
     * the IRQ event source and barriers systq before ItlIwm::free() can
     * release the embedded softc, so a copied pre-enter hook still reaches
     * the CLOSED fast-fail while this state remains valid. */
    iwm_mfp_pae_callback_close(sc);
    iwm_mfp_pae_callback_drain(sc);
}

static void
iwm_mfp_pae_generation_advance_locked(struct iwm_softc *sc)
{
    if (++sc->sc_mfp_pae_lifecycle_generation == 0)
        ++sc->sc_mfp_pae_lifecycle_generation;
}

/* A completed hardware init starts a fresh PMF lifetime.  Old workers may
 * still be unwinding, but their saved generation can no longer publish or
 * complete against this new one. */
void ItlIwm::
iwm_mfp_pae_reopen(struct iwm_softc *sc)
{
    if (sc == NULL || sc->sc_mfp_pae_lock == NULL)
        return;
    IOSimpleLockLock(sc->sc_mfp_pae_lock);
    if (!sc->sc_mfp_pae_detaching) {
        iwm_mfp_pae_generation_advance_locked(sc);
        sc->sc_mfp_pae_stopping = false;
    }
    IOSimpleLockUnlock(sc->sc_mfp_pae_lock);
}

/* Caller holds sc_mfp_pae_lock.  A worker keeps task_active set, so it alone
 * owns its local key while cancellation can take only non-worker records. */
static bool
iwm_mfp_pae_take_record_locked(struct iwm_mfp_pae_txn *txn,
                               struct iwm_mfp_pae_txn *taken)
{
    if (txn == NULL || taken == NULL || !txn->active || txn->task_active)
        return false;
    *taken = *txn;
    explicit_bzero(txn, sizeof(*txn));
    return true;
}

static void
iwm_mfp_pae_dispose_record(struct ieee80211com *ic,
                           struct iwm_mfp_pae_txn *txn)
{
    if (txn == NULL)
        return;
    iwm_mfp_pae_dispose_key(ic, &txn->ptk_key);
    iwm_mfp_pae_dispose_key(ic, &txn->gtk_key);
    iwm_mfp_pae_dispose_key(ic, &txn->pending_key);
    if (txn->ni != NULL)
        ieee80211_release_node(ic, txn->ni);
    explicit_bzero(txn, sizeof(*txn));
}

static bool
iwm_mfp_pae_record_matches(const struct iwm_mfp_pae_txn *txn,
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
iwm_mfp_pae_generic_stage_live_locked(struct ieee80211com *ic,
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
iwm_mfp_pae_cancel_record_locked(struct iwm_mfp_pae_txn *txn,
                                 struct iwm_mfp_pae_txn *taken)
{
    if (txn == NULL || !txn->active)
        return false;
    txn->cancelled = true;
    txn->pending = false;
    txn->pending_stage = IEEE80211_PAE_MFP_STAGE_NONE;
    explicit_bzero(&txn->pending_key, sizeof(txn->pending_key));
    return !txn->task_active && iwm_mfp_pae_take_record_locked(txn, taken);
}

/* Caller holds sc_mfp_pae_lock, has already retired the main record, and
 * supplies a separate cleanup destination for a stale pending successor. */
static bool
iwm_mfp_pae_promote_successor_locked(struct iwm_softc *sc,
                                     struct iwm_mfp_pae_txn *retired)
{
    struct iwm_mfp_pae_txn *successor;

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
            (void)iwm_mfp_pae_take_record_locked(successor, retired);
        return false;
    }
    sc->sc_mfp_pae_txn = *successor;
    explicit_bzero(successor, sizeof(*successor));
    return sc->sc_mfp_pae_txn.pending;
}

static bool
iwm_mfp_runtime_enabled(const struct iwm_softc *sc)
{
    return sc != NULL && sc->sc_mfp_pae_lock != NULL &&
        sc->sc_mfp_pae_task_ready && sc->sc_mfp_pae_runtime_enabled &&
        sc->sc_ic.ic_pae_selected_bss_lock != NULL;
}

/* Hook writers share the selected-BSS leaf with generic's callback snapshot.
 * The callback lease then protects a callback already copied before removal. */
static void
iwm_mfp_pae_publish_hooks(struct iwm_softc *sc, bool enabled)
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
        ic->ic_pae_mfp_txn_submit = ItlIwm::iwm_pae_mfp_txn_submit;
        ic->ic_pae_mfp_txn_cancel = ItlIwm::iwm_pae_mfp_txn_cancel;
        ic->ic_pae_mfp_txn_finish = ItlIwm::iwm_pae_mfp_txn_finish;
    }
    if (lock != NULL)
        IOSimpleLockUnlockEnableInterrupt(lock, irq);
}

/* Capability publication is intentionally independent of a particular SAE
 * request.  It admits only the completed software PMF owner; the product WCL
 * selected-BSS bridge decides whether an exact request may use it. */
void ItlIwm::
iwm_publish_mfp_capability(struct iwm_softc *sc)
{
    if (sc == NULL)
        return;
    if (!iwm_mfp_runtime_enabled(sc) || !iwm_mfp_pae_callback_open(sc)) {
        iwm_mfp_pae_callback_close(sc);
        iwm_mfp_pae_publish_hooks(sc, false);
        return;
    }
    iwm_mfp_pae_publish_hooks(sc, true);
}
/*
 * A PMF PAE transaction needs a sleepable software-CCMP preparation phase.
 * IWM's normal pairwise-key command is asynchronous firmware work and cannot
 * be used as that owner: its completion returns through the same RX action
 * that delivered Msg3.  This backend instead allocates only net80211's local
 * CCMP contexts on systq, then lets the generic transaction publish them
 * atomically with its BIP/port-state handoff.
 */
int ItlIwm::
iwm_pae_mfp_txn_submit(struct ieee80211com *ic, u_int64_t txn_id,
                       u_int64_t assoc_epoch, struct ieee80211_node *ni,
                       const struct ieee80211_key *key, u_int8_t stage)
{
    struct iwm_softc *sc;
    struct iwm_mfp_pae_txn *main, *successor, *target = NULL;
    struct iwm_mfp_pae_txn retired_successor;
    struct ieee80211_node *held_ni = NULL;
    IOSimpleLock *bss_lock;
    IOInterruptState irq;
    bool queue_task = false;
    int error = 0;

    if (ic == NULL)
        return EINVAL;
    sc = (struct iwm_softc *)ic->ic_softc;
    /* This must be the first action after obtaining sc: detach may have
     * unpublished the hook but a generic caller may already have copied it. */
    if (!iwm_mfp_pae_callback_enter(sc))
        return ECANCELED;
    explicit_bzero(&retired_successor, sizeof(retired_successor));
    if (ni == NULL || key == NULL || txn_id == 0 || assoc_epoch == 0 ||
        !iwm_mfp_pae_key_valid(key, stage)) {
        error = EINVAL;
        goto out;
    }
    if (!iwm_mfp_runtime_enabled(sc) ||
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
        !iwm_mfp_pae_generic_stage_live_locked(ic, txn_id, assoc_epoch, ni,
        stage)) {
        error = ECANCELED;
        goto unlock;
    }
    main = &sc->sc_mfp_pae_txn;
    successor = &sc->sc_mfp_pae_successor;
    if (!main->active) {
        target = main;
    } else if (iwm_mfp_pae_record_matches(main, txn_id, assoc_epoch,
        sc->sc_mfp_pae_lifecycle_generation, ni) && !main->cancelled) {
        target = main;
    } else {
        /* A cancelled or still-running predecessor is not allowed to reject
         * the next selected BSS.  Keep exactly one latest successor; a newer
         * reconnect replaces an unstarted older successor deterministically. */
        if (successor->active) {
            if (iwm_mfp_pae_record_matches(successor, txn_id, assoc_epoch,
                sc->sc_mfp_pae_lifecycle_generation, ni) &&
                !successor->cancelled) {
                target = successor;
            } else if (!successor->task_active) {
                (void)iwm_mfp_pae_take_record_locked(successor,
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
    iwm_mfp_pae_dispose_record(ic, &retired_successor);
    if (queue_task)
        (void)task_add(systq, &sc->mfp_pae_task);
    if (held_ni != NULL)
        ieee80211_release_node(ic, held_ni);
out:
    iwm_mfp_pae_callback_leave(sc);
    return error;
}

void ItlIwm::
iwm_mfp_pae_task(void *arg)
{
    struct iwm_softc *sc = (struct iwm_softc *)arg;
    struct ieee80211com *ic;
    struct iwm_mfp_pae_txn *txn;
    struct iwm_mfp_pae_txn retired_main, retired_successor;
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
        iwm_mfp_pae_generic_stage_live_locked(ic, txn->txn_id,
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
        (void)iwm_mfp_pae_take_record_locked(txn, &retired_main);
        queue_successor = iwm_mfp_pae_promote_successor_locked(sc,
            &retired_successor);
    }
    IOSimpleLockUnlock(sc->sc_mfp_pae_lock);
    IOSimpleLockUnlockEnableInterrupt(bss_lock, irq);
    iwm_mfp_pae_dispose_record(ic, &retired_main);
    iwm_mfp_pae_dispose_record(ic, &retired_successor);
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
    worker_live = iwm_mfp_pae_record_matches(txn, txn_id, assoc_epoch,
        generation, ni) && txn->task_active && !txn->cancelled &&
        !sc->sc_mfp_pae_detaching && !sc->sc_mfp_pae_stopping &&
        generation == sc->sc_mfp_pae_lifecycle_generation &&
        iwm_mfp_pae_generic_stage_live_locked(ic, txn_id, assoc_epoch, ni,
        stage);
    IOSimpleLockUnlock(sc->sc_mfp_pae_lock);
    IOSimpleLockUnlockEnableInterrupt(bss_lock, irq);
    if (!worker_live || ni == NULL || !iwm_mfp_pae_key_valid(&key, stage)) {
        error = ECANCELED;
    } else if (stage == IEEE80211_PAE_MFP_STAGE_IGTK) {
        error = 0;
    } else {
        error = ieee80211_set_key(ic, ni, &key);
    }

    irq = IOSimpleLockLockDisableInterrupt(bss_lock);
    IOSimpleLockLock(sc->sc_mfp_pae_lock);
    txn = &sc->sc_mfp_pae_txn;
    if (iwm_mfp_pae_record_matches(txn, txn_id, assoc_epoch, generation,
        ni) && txn->task_active) {
        txn->task_active = false;
        if (txn->cancelled || sc->sc_mfp_pae_detaching ||
            sc->sc_mfp_pae_stopping || generation !=
            sc->sc_mfp_pae_lifecycle_generation ||
            !iwm_mfp_pae_generic_stage_live_locked(ic, txn_id,
            assoc_epoch, ni, stage)) {
            (void)iwm_mfp_pae_take_record_locked(txn, &retired_main);
            queue_successor = iwm_mfp_pae_promote_successor_locked(sc,
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
                txn->accepted_mask |= iwm_mfp_pae_stage_mask(stage);
                deliver = true;
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
    iwm_mfp_pae_dispose_record(ic, &retired_main);
    iwm_mfp_pae_dispose_record(ic, &retired_successor);
    if (queue_successor && systq != NULL)
        (void)task_add(systq, &sc->mfp_pae_task);
    iwm_mfp_pae_dispose_key(ic, &key);
    if (deliver)
        ieee80211_pae_mfp_txn_complete(ic, txn_id, stage, error);
}

void ItlIwm::
iwm_pae_mfp_txn_cancel(struct ieee80211com *ic, u_int64_t txn_id)
{
    struct iwm_softc *sc;
    struct iwm_mfp_pae_txn retired_main, retired_successor;
    bool queue_successor = false;

    if (ic == NULL)
        return;
    sc = (struct iwm_softc *)ic->ic_softc;
    if (!iwm_mfp_pae_callback_enter(sc))
        return;
    explicit_bzero(&retired_main, sizeof(retired_main));
    explicit_bzero(&retired_successor, sizeof(retired_successor));
    if (txn_id == 0 || !iwm_mfp_runtime_enabled(sc))
        goto out;

    IOSimpleLockLock(sc->sc_mfp_pae_lock);
    if (sc->sc_mfp_pae_txn.active &&
        sc->sc_mfp_pae_txn.txn_id == txn_id) {
        if (iwm_mfp_pae_cancel_record_locked(&sc->sc_mfp_pae_txn,
            &retired_main)) {
            queue_successor = iwm_mfp_pae_promote_successor_locked(sc,
                &retired_successor);
        }
    } else if (sc->sc_mfp_pae_successor.active &&
        sc->sc_mfp_pae_successor.txn_id == txn_id) {
        (void)iwm_mfp_pae_cancel_record_locked(&sc->sc_mfp_pae_successor,
            &retired_successor);
    }
    IOSimpleLockUnlock(sc->sc_mfp_pae_lock);
    iwm_mfp_pae_dispose_record(ic, &retired_main);
    iwm_mfp_pae_dispose_record(ic, &retired_successor);
    if (queue_successor && systq != NULL)
        (void)task_add(systq, &sc->mfp_pae_task);
out:
    iwm_mfp_pae_callback_leave(sc);
}

int ItlIwm::
iwm_pae_mfp_txn_finish(struct ieee80211com *ic, u_int64_t txn_id)
{
    struct iwm_softc *sc;
    struct iwm_mfp_pae_txn *txn;
    struct ieee80211_pae_mfp_txn *generic;
    struct ieee80211_node *release_ni = NULL;
    IOSimpleLock *bss_lock;
    IOInterruptState irq;
    u_int8_t required_mask = 0;
    u_int ptk_flags = 0, gtk_flags = 0;
    int error = ECANCELED;

    if (ic == NULL)
        return EINVAL;
    sc = (struct iwm_softc *)ic->ic_softc;
    if (!iwm_mfp_pae_callback_enter(sc))
        return ECANCELED;
    if (txn_id == 0 || !iwm_mfp_runtime_enabled(sc) ||
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
            required_mask |= iwm_mfp_pae_stage_mask(IEEE80211_PAE_MFP_STAGE_PTK);
        if (generic->have_gtk)
            required_mask |= iwm_mfp_pae_stage_mask(IEEE80211_PAE_MFP_STAGE_GTK);
        if (generic->have_igtk)
            required_mask |= iwm_mfp_pae_stage_mask(IEEE80211_PAE_MFP_STAGE_IGTK);
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
                (void)iwm_mfp_pae_software_keyset_live_locked(ic, generic,
                    txn->ni);
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
    iwm_mfp_pae_callback_leave(sc);
    return error;
}

void ItlIwm::
iwm_mfp_pae_abort_all(struct iwm_softc *sc)
{
    struct ieee80211com *ic;
    struct iwm_mfp_pae_txn retired_main, retired_successor;

    if (sc == NULL || sc->sc_mfp_pae_lock == NULL)
        return;
    ic = &sc->sc_ic;
    explicit_bzero(&retired_main, sizeof(retired_main));
    explicit_bzero(&retired_successor, sizeof(retired_successor));
    IOSimpleLockLock(sc->sc_mfp_pae_lock);
    /* Stop is a hard generation boundary.  Unlike ordinary cancellation it
     * never promotes a successor: no worker may start during hardware stop. */
    sc->sc_mfp_pae_stopping = true;
    iwm_mfp_pae_generation_advance_locked(sc);
    (void)iwm_mfp_pae_cancel_record_locked(&sc->sc_mfp_pae_txn,
        &retired_main);
    (void)iwm_mfp_pae_cancel_record_locked(&sc->sc_mfp_pae_successor,
        &retired_successor);
    IOSimpleLockUnlock(sc->sc_mfp_pae_lock);
    iwm_mfp_pae_dispose_record(ic, &retired_main);
    iwm_mfp_pae_dispose_record(ic, &retired_successor);
}

void ItlIwm::
iwm_mfp_pae_detach_begin(struct iwm_softc *sc)
{
    struct ieee80211com *ic;

    if (sc == NULL)
        return;
    ic = &sc->sc_ic;
    /* Close first so a hook captured before unpublication cannot touch the
     * PMF leaf after detach starts.  Do not hold a PMF lock across generic
     * abort or the callback drain. */
    iwm_mfp_pae_callback_close(sc);
    if (sc->sc_mfp_pae_lock != NULL) {
        IOSimpleLockLock(sc->sc_mfp_pae_lock);
        sc->sc_mfp_pae_detaching = true;
        IOSimpleLockUnlock(sc->sc_mfp_pae_lock);
    }
    /* Generic abort still sees the installed cancel hook; admission is
     * already closed, so that callback is a harmless no-op. */
    ieee80211_pae_mfp_txn_abort(ic);
    iwm_mfp_pae_publish_hooks(sc, false);
    iwm_mfp_pae_abort_all(sc);
    /* A pre-close submit may queue a task after generic abort returns; drain
     * admitted callbacks before task_del()+barrier fences that producer.
     * A copied-but-not-entered hook observes CLOSED and never touches PMF
     * state; final ifdetach/IRQ/task fences retain the surrounding softc. */
    iwm_mfp_pae_callback_drain(sc);

    if (sc->sc_mfp_pae_task_ready && systq != NULL) {
        (void)task_del(systq, &sc->mfp_pae_task);
        /* task_del() cannot recall a callback already copied by the worker. */
        taskq_barrier(systq);
    }
    sc->sc_mfp_pae_task_ready = false;
    iwm_mfp_pae_abort_all(sc);
}
