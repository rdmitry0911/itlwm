/* Included by ItlIwn.cpp. Continuations run on the existing main workloop.
 * IRQ and ingress publish copied values and signal a retained event source;
 * no worker waits on the controller gate. */
static ItlStateTransitionIdentity
iwn_auth_identity_locked(const struct ieee80211com *ic)
{
    ItlStateTransitionIdentity identity = {};
    identity.joinSequence = ic->ic_wcl_join_attempt.next_generation;
    if (ic->ic_wcl_join_attempt.phase != IEEE80211_JOIN_IDLE)
        identity.joinGeneration = ic->ic_wcl_join_attempt.result.generation;
    identity.associationEpoch = ic->ic_pae_assoc_epoch;
    return identity;
}

static bool
iwn_auth_identity_current_locked(struct ieee80211com *ic,
                                  const IwnAuthBeaconRequest &request)
{
    const struct ieee80211_node *ni = ic->ic_bss;
    return ic->ic_opmode == IEEE80211_M_STA && ni != NULL &&
        (ic->ic_if.if_flags & IFF_RUNNING) != 0 &&
        ic->ic_state == request.sourceState &&
        request.identity.equals(iwn_auth_identity_locked(ic)) &&
        ic->ic_wcl_reassoc_next_serial == request.reassocSequence &&
        (ic->ic_wcl_reassoc_owner_active ?
            ic->ic_wcl_reassoc_owner_serial : 0) == request.reassocSerial &&
        IEEE80211_ADDR_EQ(ni->ni_bssid, request.bssid) &&
        IEEE80211_ADDR_EQ(ni->ni_macaddr, request.bssid) &&
        IEEE80211_ADDR_EQ(ic->ic_myaddr, request.station) &&
        ni->ni_chan != NULL &&
        ieee80211_chan2ieee(ic, ni->ni_chan) == request.channel;
}

bool ItlIwn::iwn_auth_beacon_init()
{
    authBeaconLock = IOSimpleLockAlloc();
    if (authBeaconLock == NULL)
        return false;
    authBeaconSource = IOInterruptEventSource::interruptEventSource(
        this, &ItlIwn::iwn_auth_beacon_event);
    authBeaconTimer = IOTimerEventSource::timerEventSource(
        this, &ItlIwn::iwn_auth_beacon_timeout);
    if (authBeaconSource == NULL || authBeaconTimer == NULL)
        return false;
    if (getMainWorkLoop()->addEventSource(authBeaconSource) != kIOReturnSuccess)
        return false;
    authBeaconSourceAdded = true;
    if (getMainWorkLoop()->addEventSource(authBeaconTimer) != kIOReturnSuccess)
        return false;
    authBeaconTimerAdded = true;
    authBeaconSource->enable();
    authBeaconTimer->enable();
    return true;
}

void ItlIwn::iwn_auth_beacon_cancel(bool close)
{
    if (authBeaconLock == NULL)
        return;
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(authBeaconLock);
    if (close)
        authBeacon.close();
    else
        authBeacon.cancel();
    IOSimpleLockUnlockEnableInterrupt(authBeaconLock, irq);
    /* Do not cancel a timer from an ungated predecessor: a new owner may
     * already have armed it. A stale firing checks the current deadline. */
}

void ItlIwn::iwn_auth_beacon_shutdown()
{
    iwn_auth_beacon_cancel(true);
    IOInterruptEventSource *source = authBeaconSource;
    IOTimerEventSource *timer = authBeaconTimer;
    if (authBeaconLock != NULL) {
        IOInterruptState irq = IOSimpleLockLockDisableInterrupt(authBeaconLock);
        source = authBeaconSource;
        timer = authBeaconTimer;
        authBeaconSource = NULL;
        authBeaconTimer = NULL;
        IOSimpleLockUnlockEnableInterrupt(authBeaconLock, irq);
    }
    if (source != NULL) {
        source->disable();
        if (authBeaconSourceAdded)
            getMainWorkLoop()->removeEventSource(source);
        authBeaconSourceAdded = false;
        source->release();
    }
    if (timer != NULL) {
        timer->cancelTimeout();
        timer->disable();
        if (authBeaconTimerAdded)
            getMainWorkLoop()->removeEventSource(timer);
        authBeaconTimerAdded = false;
        timer->release();
    }
}

bool ItlIwn::iwn_auth_beacon_pending()
{
    if (authBeaconLock == NULL)
        return false;
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(authBeaconLock);
    const bool pending = authBeacon.open && authBeacon.pending();
    IOSimpleLockUnlockEnableInterrupt(authBeaconLock, irq);
    return pending;
}

bool ItlIwn::iwn_auth_beacon_current(const IwnAuthBeaconRequest &request)
{
    struct ieee80211com *ic = &com.sc_ic;
    IOSimpleLock *ownerLock = ic->ic_pae_selected_bss_lock;
    if (ownerLock == NULL || authBeaconLock == NULL)
        return false;
    IOInterruptState ownerIrq = IOSimpleLockLockDisableInterrupt(ownerLock);
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(authBeaconLock);
    const bool current = authBeaconSource != NULL &&
        authBeacon.current(request) &&
        iwn_auth_identity_current_locked(ic, request);
    IOSimpleLockUnlockEnableInterrupt(authBeaconLock, irq);
    IOSimpleLockUnlockEnableInterrupt(ownerLock, ownerIrq);
    return current;
}

void ItlIwn::iwn_auth_beacon_kick()
{
    if (authBeaconLock == NULL)
        return;
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(authBeaconLock);
    IOInterruptEventSource *source = authBeacon.open && authBeacon.pending() ?
        authBeaconSource : NULL;
    if (source != NULL)
        source->retain();
    IOSimpleLockUnlockEnableInterrupt(authBeaconLock, irq);
    if (source != NULL) {
        source->interruptOccurred(NULL, NULL, 0);
        source->release();
    }
}

int ItlIwn::iwn_auth_beacon_enqueue(int state, int argument)
{
    struct ieee80211com *ic = &com.sc_ic;
    IOSimpleLock *ownerLock = ic->ic_pae_selected_bss_lock;
    IwnAuthBeaconRequest request = {}, copy = {};
    if (ownerLock == NULL || authBeaconLock == NULL)
        return ENXIO;
    clock_interval_to_deadline(IEEE80211_TRANS_WAIT, kSecondScale,
        &request.deadline);
    IOInterruptState ownerIrq = IOSimpleLockLockDisableInterrupt(ownerLock);
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(authBeaconLock);
    const struct ieee80211_node *ni = ic->ic_bss;
    bool accepted = false;
    if (authBeaconSource != NULL && authBeaconTimer != NULL &&
        ic->ic_opmode == IEEE80211_M_STA && ni != NULL && ni->ni_chan != NULL &&
        (ic->ic_if.if_flags & IFF_RUNNING) != 0) {
        request.identity = iwn_auth_identity_locked(ic);
        request.reassocSequence = ic->ic_wcl_reassoc_next_serial;
        request.reassocSerial = ic->ic_wcl_reassoc_owner_active ?
            ic->ic_wcl_reassoc_owner_serial : 0;
        request.sourceState = ic->ic_state;
        request.state = state;
        request.argument = argument;
        request.channel = ieee80211_chan2ieee(ic, ni->ni_chan);
        IEEE80211_ADDR_COPY(request.bssid, ni->ni_bssid);
        IEEE80211_ADDR_COPY(request.station, ic->ic_myaddr);
        const bool switching =
            !IEEE80211_ADDR_EQ(com.bss_node_addr, etheranyaddr) &&
            !IEEE80211_ADDR_EQ(com.bss_node_addr, ni->ni_macaddr);
        request.requireBeacon = (argument == -1 || switching) && ic->ic_mgt_timer == 0;
        accepted = authBeacon.enqueue(request, &copy);
    }
    IOSimpleLockUnlockEnableInterrupt(authBeaconLock, irq);
    IOSimpleLockUnlockEnableInterrupt(ownerLock, ownerIrq);
    if (!accepted)
        return ECANCELED;
    iwn_auth_beacon_kick();
    return 0;
}

struct IwnAuthRxonDoorbell {
    ItlIwn *driver;
    IwnAuthBeaconRequest request;
    int preparationSlot; // -1: RXON, 0: broadcast ADD_NODE, 1: link quality.
    IOInterruptState ownerIrq;
    IOInterruptState irq;
};

static bool iwn_auth_rxon_pre_doorbell(struct iwn_softc *sc, void *argument)
{
    IwnAuthRxonDoorbell *context = static_cast<IwnAuthRxonDoorbell *>(argument);
    ItlIwn *that = context->driver;
    IOSimpleLock *ownerLock = sc->sc_ic.ic_pae_selected_bss_lock;
    context->ownerIrq = IOSimpleLockLockDisableInterrupt(ownerLock);
    context->irq = IOSimpleLockLockDisableInterrupt(that->authBeaconLock);
    const uint16_t index = sc->txq[sc->command_queue].cur;
    if (iwn_auth_identity_current_locked(&sc->sc_ic, context->request) &&
        (context->preparationSlot < 0 ?
            that->authBeacon.submitRxon(context->request, index) :
            that->authBeacon.submitPreparation(context->request,
                static_cast<unsigned>(context->preparationSlot), index))) {
        sc->txq[sc->command_queue].data[index].auth_rxon_serial =
            context->request.serial;
        return true; // Both leaves remain held until the real WRPTR write.
    }
    IOSimpleLockUnlockEnableInterrupt(that->authBeaconLock, context->irq);
    IOSimpleLockUnlockEnableInterrupt(ownerLock, context->ownerIrq);
    return false;
}

static void iwn_auth_rxon_post_doorbell(struct iwn_softc *sc, void *argument)
{
    IwnAuthRxonDoorbell *context = static_cast<IwnAuthRxonDoorbell *>(argument);
    IOSimpleLockUnlockEnableInterrupt(context->driver->authBeaconLock, context->irq);
    IOSimpleLockUnlockEnableInterrupt(sc->sc_ic.ic_pae_selected_bss_lock,
        context->ownerIrq);
}

int ItlIwn::iwn_auth_rxon(struct iwn_softc *sc)
{
    if (sc->sc_ic.ic_opmode != IEEE80211_M_STA)
        return iwn_cmd(sc, IWN_CMD_RXON, &sc->rxon, sc->rxonsz, 1);
    IwnAuthRxonDoorbell context = {};
    context.driver = this;
    context.preparationSlot = -1;
    if (authBeaconLock == NULL)
        return ENXIO;
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(authBeaconLock);
    const bool preparing = authBeacon.open &&
        authBeacon.stage == IwnAuthBeaconLease::Stage::Programming;
    if (preparing)
        context.request = authBeacon.request;
    IOSimpleLockUnlockEnableInterrupt(authBeaconLock, irq);
    if (!preparing || !iwn_auth_beacon_current(context.request))
        return ECANCELED;
    return iwn_cmd_with_doorbell_hook(sc, IWN_CMD_RXON, &sc->rxon, sc->rxonsz, 1,
        iwn_auth_rxon_pre_doorbell, iwn_auth_rxon_post_doorbell, &context);
}

int ItlIwn::iwn_auth_preparation_cmd(struct iwn_softc *sc, int code,
                                     const void *buffer, int size, int async)
{
    IwnAuthRxonDoorbell context = {};
    context.driver = this;
    context.preparationSlot = code == IWN_CMD_ADD_NODE ? 0 : 1;
    bool preparing = false;
    if (authBeaconLock != NULL && getMainWorkLoop()->inGate()) {
        IOInterruptState irq = IOSimpleLockLockDisableInterrupt(authBeaconLock);
        preparing = authBeacon.open && authBeacon.rxonSubmitted &&
            authBeacon.stage == IwnAuthBeaconLease::Stage::Programming;
        if (preparing)
            context.request = authBeacon.request;
        IOSimpleLockUnlockEnableInterrupt(authBeaconLock, irq);
    }
    if (!preparing)
        return iwn_cmd_with_doorbell_hook(sc, code, buffer, size, async, NULL, NULL, NULL);
    if (!iwn_auth_beacon_current(context.request))
        return ECANCELED;
    return iwn_cmd_with_doorbell_hook(sc, code, buffer, size, async,
        iwn_auth_rxon_pre_doorbell, iwn_auth_rxon_post_doorbell, &context);
}

void ItlIwn::iwn_auth_beacon_note_command(struct iwn_rx_desc *desc, uint64_t serial)
{
    if (authBeaconLock == NULL || serial == 0)
        return;
    /* DVM RXON has no variable success payload. Require the exact command
     * response and an unflagged header, not just a recycled descriptor index. */
    const uint32_t length = letoh32(desc->len) & IWN_RX_DESC_LEN_MASK;
    bool accepted = desc->flags == 0 && length >= 4;
    if (desc->type == IWN_CMD_ADD_NODE)
        accepted = accepted && length >= 5 &&
            *reinterpret_cast<const uint8_t *>(desc + 1) == 1;
    uint64_t now;
    clock_get_uptime(&now);
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(authBeaconLock);
    bool changed;
    if (desc->type == IWN_CMD_ADD_NODE || desc->type == IWN_CMD_LINK_QUALITY)
        changed = authBeacon.notePreparation(serial,
            desc->type == IWN_CMD_ADD_NODE ? 0 : 1, desc->idx, accepted, EIO, now);
    else
        changed = authBeacon.noteRxon(serial, desc->idx,
            accepted && desc->type == IWN_CMD_RXON, EIO, now);
    IOSimpleLockUnlockEnableInterrupt(authBeaconLock, irq);
    if (changed)
        iwn_auth_beacon_kick();
}

uint64_t ItlIwn::iwn_auth_beacon_rx_owner()
{
    if (authBeaconLock == NULL)
        return 0;
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(authBeaconLock);
    const uint64_t serial = authBeacon.open && authBeacon.pending() &&
        authBeacon.rxonAccepted ? authBeacon.request.serial : 0;
    IOSimpleLockUnlockEnableInterrupt(authBeaconLock, irq);
    return serial;
}

void ItlIwn::iwn_auth_beacon_note_rx(const struct ieee80211_frame *frame,
                                    size_t length, uint16_t channel, uint64_t serial)
{
    if (authBeaconLock == NULL || serial == 0 || frame == NULL || length < sizeof(*frame) + 12 ||
        frame->i_fc[0] != (IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_BEACON))
        return;
    uint64_t now;
    clock_get_uptime(&now);
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(authBeaconLock);
    const bool changed = serial == authBeacon.request.serial &&
        authBeacon.noteBeacon(frame->i_addr3, frame->i_addr2, channel, now);
    IOSimpleLockUnlockEnableInterrupt(authBeaconLock, irq);
    if (changed)
        iwn_auth_beacon_kick();
}

void ItlIwn::iwn_auth_beacon_fail(IwnAuthBeaconRequest request, int error)
{
    struct ieee80211com *ic = &com.sc_ic;
    if (!iwn_auth_beacon_current(request))
        return;
    struct ieee80211_join_failure join = {};
    if (request.identity.joinGeneration != 0 &&
        ieee80211_wcl_join_copy_current(ic, request.identity.associationEpoch, &join) &&
        join.generation == request.identity.joinGeneration &&
        ieee80211_wcl_join_fail(ic, &join, error == ETIMEDOUT ?
            IEEE80211_JOIN_FAILURE_TIMEOUT : IEEE80211_JOIN_FAILURE_LOCAL,
            0, 0, error, IEEE80211_JOIN_CLEANUP_ALL)) {
        const uint64_t generation = join.generation;
        iwn_auth_beacon_cancel();
        iwn_wcl_join_failure_scan(ic, generation);
        explicit_bzero(&request, sizeof(request));
        explicit_bzero(&join, sizeof(join));
        /* The AUTH producer has no queued continuation after cancellation.
         * Existing lower/SAE owners acknowledge only their real retirement. */
        ieee80211_wcl_join_cleanup_done(ic, generation, IEEE80211_JOIN_CLEANUP_PRODUCER);
        return;
    }
    if (request.identity.joinGeneration != 0)
        return; // A changed fresh attempt must not become an unowned roam.
    if (request.reassocSerial != 0) {
        request.identity.associationEpoch = ieee80211_wcl_reassoc_post_failure_owned(
            ic, request.reassocSerial, error);
        request.reassocSerial = 0;
    }
    IOSimpleLock *ownerLock = ic->ic_pae_selected_bss_lock;
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(ownerLock);
    const bool current = iwn_auth_identity_current_locked(ic, request);
    IOSimpleLockUnlockEnableInterrupt(ownerLock, irq);
    if (!current)
        return;
    irq = IOSimpleLockLockDisableInterrupt(authBeaconLock);
    const bool retiring = authBeacon.current(request);
    if (retiring)
        authBeacon.cancel();
    const uint64_t retirementSequence = authBeacon.cancellationSequence;
    IOSimpleLockUnlockEnableInterrupt(authBeaconLock, irq);
    if (!retiring)
        return;
    /* INIT retires the failed target without sending AUTH. A generic SCAN
     * directly from the old protected RUN would be consumed by its join
     * fence. A successor/cancellation inside INIT must not acquire this
     * failed operation's subsequent recovery scan. */
    ieee80211_new_state(ic, IEEE80211_S_INIT, -1);
    irq = IOSimpleLockLockDisableInterrupt(ownerLock);
    IOInterruptState authIrq = IOSimpleLockLockDisableInterrupt(authBeaconLock);
    const bool recover = authBeacon.open && retirementSequence != UINT64_MAX &&
        authBeacon.cancellationSequence == retirementSequence + 1 &&
        ic->ic_opmode == IEEE80211_M_STA && ic->ic_state == IEEE80211_S_INIT &&
        (ic->ic_if.if_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING) &&
        ic->ic_wcl_join_attempt.next_generation == request.identity.joinSequence &&
        ic->ic_wcl_reassoc_next_serial == request.reassocSequence;
    IOSimpleLockUnlockEnableInterrupt(authBeaconLock, authIrq);
    IOSimpleLockUnlockEnableInterrupt(ownerLock, irq);
    if (recover)
        (void)ieee80211_begin_scan_with_result(&ic->ic_if);
}

void ItlIwn::iwn_auth_beacon_drain()
{
    if (authBeaconLock == NULL || !getMainWorkLoop()->inGate())
        return;
    IwnAuthBeaconRequest request = {};
    uint64_t now;
    clock_get_uptime(&now);
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(authBeaconLock);
    const bool prepare = authBeaconSource != NULL &&
        authBeacon.takeProgramming(now, ETIMEDOUT, &request);
    IOSimpleLockUnlockEnableInterrupt(authBeaconLock, irq);
    if (prepare) {
        if (!iwn_auth_beacon_current(request)) {
            irq = IOSimpleLockLockDisableInterrupt(authBeaconLock);
            if (authBeacon.current(request))
                authBeacon.cancel();
            IOSimpleLockUnlockEnableInterrupt(authBeaconLock, irq);
            return;
        }
        const int error = iwn_newstate_impl(&com.sc_ic,
            static_cast<enum ieee80211_state>(request.state), request.argument, 0,
            request.serial);
        irq = IOSimpleLockLockDisableInterrupt(authBeaconLock);
        (void)authBeacon.programmed(request, error);
        IOSimpleLockUnlockEnableInterrupt(authBeaconLock, irq);
    }
    clock_get_uptime(&now);
    int error = 0;
    uint64_t deadline = 0;
    irq = IOSimpleLockLockDisableInterrupt(authBeaconLock);
    const bool terminal = authBeaconSource != NULL &&
        authBeacon.takeTerminal(now, ETIMEDOUT, &request, &error);
    if (authBeacon.open && authBeacon.stage == IwnAuthBeaconLease::Stage::Waiting)
        deadline = authBeacon.request.deadline;
    IOSimpleLockUnlockEnableInterrupt(authBeaconLock, irq);
    if (!terminal) {
        if (deadline != 0 && authBeaconTimer != NULL) {
            uint64_t nanoseconds = 0;
            if (deadline > now)
                absolutetime_to_nanoseconds(deadline - now, &nanoseconds);
            authBeaconTimer->setTimeoutMS(static_cast<uint32_t>(
                MAX(1ULL, (nanoseconds + 999999) / 1000000)));
        }
        return;
    }
    if (!iwn_auth_beacon_current(request))
        return;
    XYLog("iwn_auth_beacon terminal serial=%llu epoch=%llu beacon=%u error=%d\n",
        request.serial, request.identity.associationEpoch,
        request.requireBeacon ? 1U : 0U, error);
    if (error != 0) {
        iwn_auth_beacon_fail(request, error);
        return;
    }
    /* Exactly one generic callback, after hardware receipt. Neither RXON
     * nor epoch preparation is replayed here. Both Open and SAE start below. */
    error = com.sc_newstate(&com.sc_ic,
        static_cast<enum ieee80211_state>(request.state), request.argument);
    if (error != 0 && iwn_auth_beacon_current(request))
        iwn_auth_beacon_fail(request, error);
}

void ItlIwn::iwn_auth_beacon_event(OSObject *owner, IOInterruptEventSource *, int)
{
    static_cast<ItlIwn *>(owner)->iwn_auth_beacon_drain();
}

void ItlIwn::iwn_auth_beacon_timeout(OSObject *owner, IOTimerEventSource *)
{
    static_cast<ItlIwn *>(owner)->iwn_auth_beacon_drain();
}
