/* Included by ieee80211_proto.c. Kernel binding for the value-only policy. */

static u_int64_t
ieee80211_sta_sa_query_now(void)
{
    struct timeval now;
    microuptime(&now);
    return (u_int64_t)now.tv_sec * 1000000 + now.tv_usec;
}

static int
ieee80211_sta_sa_query_peer_locked(struct ieee80211com *ic,
    const struct ieee80211_node *ni, u_int64_t epoch,
    const u_int8_t *bssid, const u_int8_t *sta)
{
    const u_int32_t protection = IEEE80211_NODE_MFP |
        IEEE80211_NODE_TXMGMTPROT | IEEE80211_NODE_RXMGMTPROT;
    return ic->ic_sta_sa_query_enabled && ic->ic_sta_sa_query_tag_valid &&
        ic->ic_opmode == IEEE80211_M_STA && ic->ic_state == IEEE80211_S_RUN &&
        (ic->ic_if.if_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING) &&
        epoch != 0 && ieee80211_pae_assoc_epoch_current(ic) == epoch &&
        ic->ic_pae_assoc_replace_epoch == 0 &&
        ic->ic_pae_selected_bss.epoch == epoch && ni != NULL && ni == ic->ic_bss &&
        ni->ni_port_valid && (ni->ni_flags & protection) == protection &&
        IEEE80211_ADDR_EQ(ic->ic_pae_selected_bss.bssid, bssid) &&
        IEEE80211_ADDR_EQ(ni->ni_bssid, bssid) &&
        IEEE80211_ADDR_EQ(ni->ni_macaddr, bssid) &&
        IEEE80211_ADDR_EQ(ic->ic_myaddr, sta);
}

static void
ieee80211_sta_sa_query_arm(struct ieee80211com *ic,
    const struct ieee80211_sta_sa_query_token *owner, u_int64_t next_us)
{
    if (timeout_add_msec(&ic->ic_sta_sa_query_timeout,
        (int)((next_us + 999) / 1000)) != 0)
        return;
    /* A missing timer resource must not leave a permanently active owner.
     * Allocation/gate failure may yield: cancel only the value we armed. */
    IOSimpleLock *lock = ic->ic_pae_selected_bss_lock;
    if (lock == NULL)
        return;
    const IOInterruptState irq = IOSimpleLockLockDisableInterrupt(lock);
    if (ieee80211_sta_sa_query_token_equal(&ic->ic_sta_sa_query.owner, owner))
        ieee80211_sta_sa_query_cancel_value(&ic->ic_sta_sa_query);
    IOSimpleLockUnlockEnableInterrupt(lock, irq);
}

/* This CTimeout action executes on the main net80211 workloop. RX only
 * admits a value and schedules it; no node pointer crosses the timer edge. */
static void
ieee80211_sta_sa_query_timeout(void *arg)
{
    struct ieee80211com *ic = (struct ieee80211com *)arg;
    struct ieee80211_sta_sa_query_token token;
    struct ieee80211_sta_sa_query_token timer_owner;
    struct ieee80211_node *ni = NULL;
    IOSimpleLock *lock = ic->ic_pae_selected_bss_lock;
    IOInterruptState irq;
    u_int64_t next_us = 0;
    const u_int64_t now = ieee80211_sta_sa_query_now();
    u_int16_t random_id;
    int action = 0;

    if (lock == NULL)
        return;
    arc4random_buf(&random_id, sizeof(random_id));
    irq = IOSimpleLockLockDisableInterrupt(lock);
    const struct ieee80211_sta_sa_query_token *owner = &ic->ic_sta_sa_query.owner;
    if (!ieee80211_sta_sa_query_peer_locked(ic, ic->ic_bss, owner->epoch,
        owner->bssid, owner->sta)) {
        ieee80211_sta_sa_query_cancel_value(&ic->ic_sta_sa_query);
    } else {
        timer_owner = *owner;
        action = ieee80211_sta_sa_query_poll_value(&ic->ic_sta_sa_query,
            now, random_id, &token, &next_us);
        if (action == 1)
            ni = ic->ic_bss;
    }
    IOSimpleLockUnlockEnableInterrupt(lock, irq);
    /* Allocation, queue/TX, timers and controller callbacks are outside the
     * leaf. The packet carries its immutable token through the lower queue. */
    if (action == 1)
        (void)ieee80211_send_sta_sa_query(ic, ni, &token);
    else if (action == -1 && ic->ic_event_handler != NULL)
        (*ic->ic_event_handler)(ic, IEEE80211_EVT_STA_SA_QUERY_TIMEOUT, &token);
    if (next_us != 0)
        ieee80211_sta_sa_query_arm(ic, &timer_owner, next_us);
}

void
ieee80211_sta_sa_query_attach(struct ieee80211com *ic)
{
    ic->ic_sta_sa_query_tag_valid = mbuf_tag_id_find(
        "com.zxystd.itlwm.sta-sa-query", &ic->ic_sta_sa_query_tag_id) == 0;
    timeout_set(&ic->ic_sta_sa_query_timeout, ieee80211_sta_sa_query_timeout, ic);
}

void
ieee80211_sta_sa_query_detach(struct ieee80211com *ic)
{
    IOSimpleLock *lock = ic->ic_pae_selected_bss_lock;
    IOInterruptState irq;
    if (lock != NULL)
        irq = IOSimpleLockLockDisableInterrupt(lock);
    ic->ic_sta_sa_query_enabled = 0;
    ieee80211_sta_sa_query_cancel_value(&ic->ic_sta_sa_query);
    if (lock != NULL)
        IOSimpleLockUnlockEnableInterrupt(lock, irq);
    timeout_del(&ic->ic_sta_sa_query_timeout);
    timeout_free(&ic->ic_sta_sa_query_timeout);
}

void
ieee80211_sta_sa_query_unprotected(struct ieee80211com *ic,
    struct ieee80211_node *ni, const struct ieee80211_frame *wh, size_t len)
{
    IOSimpleLock *lock = ic->ic_pae_selected_bss_lock;
    IOInterruptState irq;
    int started = 0;
    struct ieee80211_sta_sa_query_token owner;
    u_int16_t reason;
    const u_int8_t subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;
    if (lock == NULL || len < sizeof(*wh) + 2 ||
        (subtype != IEEE80211_FC0_SUBTYPE_DEAUTH &&
        subtype != IEEE80211_FC0_SUBTYPE_DISASSOC) ||
        (wh->i_fc[1] & (IEEE80211_FC1_PROTECTED | IEEE80211_FC1_DIR_MASK)) != 0 ||
        IEEE80211_IS_MULTICAST(wh->i_addr1) ||
        !IEEE80211_ADDR_EQ(wh->i_addr2, wh->i_addr3))
        return;
    reason = LE_READ_2((const u_int8_t *)&wh[1]);
    if (reason != IEEE80211_REASON_NOT_AUTHED &&
        reason != IEEE80211_REASON_NOT_ASSOCED)
        return;
    const u_int64_t now = ieee80211_sta_sa_query_now();
    irq = IOSimpleLockLockDisableInterrupt(lock);
    const u_int64_t epoch = ieee80211_pae_assoc_epoch_current(ic);
    if (ieee80211_sta_sa_query_peer_locked(ic, ni, epoch,
        wh->i_addr2, wh->i_addr1))
        started = ieee80211_sta_sa_query_begin_value(&ic->ic_sta_sa_query,
            now, epoch, wh->i_addr2, wh->i_addr1);
    if (started)
        owner = ic->ic_sta_sa_query.owner;
    IOSimpleLockUnlockEnableInterrupt(lock, irq);
    if (started)
        ieee80211_sta_sa_query_arm(ic, &owner, 1000);
}

/* Called only AFTER inputm's CCMP/PN verification for robust management.
 * Never route the unprotected drop branch into this response consumer. */
void
ieee80211_sta_sa_query_response(struct ieee80211com *ic,
    struct ieee80211_node *ni, const struct ieee80211_frame *wh, size_t len)
{
    IOSimpleLock *lock = ic->ic_pae_selected_bss_lock;
    IOInterruptState irq;
    if (lock == NULL || len < sizeof(*wh) + 4 ||
        IEEE80211_IS_MULTICAST(wh->i_addr1) ||
        !IEEE80211_ADDR_EQ(wh->i_addr2, wh->i_addr3))
        return;
    const u_int8_t *body = (const u_int8_t *)&wh[1];
    if (body[0] != IEEE80211_CATEG_SA_QUERY ||
        body[1] != IEEE80211_ACTION_SA_QUERY_RESP)
        return;
    irq = IOSimpleLockLockDisableInterrupt(lock);
    const u_int64_t epoch = ieee80211_pae_assoc_epoch_current(ic);
    if (ieee80211_sta_sa_query_peer_locked(ic, ni, epoch,
        wh->i_addr2, wh->i_addr1))
        (void)ieee80211_sta_sa_query_response_value(&ic->ic_sta_sa_query,
            epoch, wh->i_addr2, wh->i_addr1, LE_READ_2(body + 2));
    IOSimpleLockUnlockEnableInterrupt(lock, irq);
    /* No timeout_del here: it could cancel a newer owner's timer after the
     * leaf is dropped. An idle timer harmlessly observes an inactive value. */
}

int
ieee80211_sta_sa_query_tag(struct ieee80211com *ic, mbuf_t m,
    const struct ieee80211_sta_sa_query_token *token)
{
    void *data = NULL;
    if (!ic->ic_sta_sa_query_tag_valid || token == NULL ||
        mbuf_tag_allocate(m, ic->ic_sta_sa_query_tag_id, 1, sizeof(*token),
            MBUF_DONTWAIT, &data) != 0)
        return ENOBUFS;
    memcpy(data, token, sizeof(*token));
    return 0;
}

/* -1 rejects a malformed or stale tagged frame, 0 is an ordinary frame,
 * 1 is one live query. Classification precedes encryption and mbuf trimming. */
int
ieee80211_sta_sa_query_tx_snapshot(struct ieee80211com *ic,
    struct ieee80211_node *ni, mbuf_t m, struct ieee80211_sta_sa_query_token *token)
{
    size_t len = 0;
    void *data = NULL;
    IOSimpleLock *lock = ic->ic_pae_selected_bss_lock;
    IOInterruptState irq;
    int current;
    memset(token, 0, sizeof(*token));
    if (!ic->ic_sta_sa_query_tag_valid ||
        mbuf_tag_find(m, ic->ic_sta_sa_query_tag_id, 1, &len, &data) != 0)
        return 0;
    if (lock == NULL || len != sizeof(*token) || data == NULL ||
        mbuf_len(m) < sizeof(struct ieee80211_frame) + 4 ||
        mbuf_pkthdr_len(m) != sizeof(struct ieee80211_frame) + 4)
        return -1;
    memcpy(token, data, sizeof(*token));
    const struct ieee80211_frame *wh = mtod(m, struct ieee80211_frame *);
    const u_int8_t *body = (const u_int8_t *)&wh[1];
    if (wh->i_fc[0] != (IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_ACTION) ||
        wh->i_fc[1] != IEEE80211_FC1_PROTECTED ||
        body[0] != IEEE80211_CATEG_SA_QUERY ||
        body[1] != IEEE80211_ACTION_SA_QUERY_REQ ||
        LE_READ_2(body + 2) != token->transaction ||
        !IEEE80211_ADDR_EQ(wh->i_addr1, token->bssid) ||
        !IEEE80211_ADDR_EQ(wh->i_addr2, token->sta) ||
        !IEEE80211_ADDR_EQ(wh->i_addr3, token->bssid))
        return -1;
    irq = IOSimpleLockLockDisableInterrupt(lock);
    current = ieee80211_sta_sa_query_peer_locked(ic, ni, token->epoch,
        token->bssid, token->sta) &&
        ieee80211_sta_sa_query_tx_value(&ic->ic_sta_sa_query, token, 0);
    IOSimpleLockUnlockEnableInterrupt(lock, irq);
    return current ? 1 : -1;
}

int
ieee80211_sta_sa_query_tx_commit_locked(struct ieee80211com *ic,
    struct ieee80211_node *ni, const struct ieee80211_sta_sa_query_token *token)
{
    return ieee80211_sta_sa_query_peer_locked(ic, ni, token->epoch,
        token->bssid, token->sta) &&
        ieee80211_sta_sa_query_tx_value(&ic->ic_sta_sa_query, token, 1);
}

/* The native controller calls this in its gate immediately before posting
 * a copied link-loss carrier. It does not issue an unowned SCAN after a
 * potentially yielding callback; WCL's ordinary leave/join owns recovery. */
int
ieee80211_sta_sa_query_claim_failure(struct ieee80211com *ic,
    const struct ieee80211_sta_sa_query_token *token)
{
    IOSimpleLock *lock = ic->ic_pae_selected_bss_lock;
    IOInterruptState irq;
    int claimed;
    if (lock == NULL || token == NULL)
        return 0;
    irq = IOSimpleLockLockDisableInterrupt(lock);
    claimed = ieee80211_sta_sa_query_peer_locked(ic, ic->ic_bss, token->epoch,
        token->bssid, token->sta) &&
        ieee80211_sta_sa_query_failure_value(&ic->ic_sta_sa_query, token);
    IOSimpleLockUnlockEnableInterrupt(lock, irq);
    return claimed;
}
