#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Deterministic model for BIP context retirement.  A reader joins the global
 * epoch before it can observe k_priv; an overwritten slot retires only its
 * former context; a deferred collector frees retired contexts only during an
 * empty reader epoch.
 *
 * IGTK slots 4 and 5 deliberately remain independent.  A 4 -> 5 rekey keeps
 * slot 4 usable for RX MMIEs until slot 4 is later replaced or unpublished.
 * It must not, however, allow a TX CMAC claim begun on old active slot 4 to
 * emit after active TX has switched to slot 5.  This model is packet- and
 * key-material-free; the source contract pairs it with the real lock and
 * retirement implementation.
 */
enum {
    bip_slot_4 = 0,
    bip_slot_5 = 1,
    bip_slot_count = 2,
};

enum bip_tx_key_kind {
    bip_tx_key_pairwise,
    bip_tx_key_gtk,
    bip_tx_key_igtk,
};

/* Mirrors the 802.11 key-domain split: IGTK protects multicast MMPDUs only.
 * A unicast robust-management frame still uses its pairwise key, while all
 * multicast data (including ARP/IPv6) remains on the GTK. */
static enum bip_tx_key_kind
bip_tx_key_select(bool pairwise, bool mfp, bool multicast, bool management)
{
    if (pairwise)
        return bip_tx_key_pairwise;
    if (mfp && multicast && management)
        return bip_tx_key_igtk;
    return bip_tx_key_gtk;
}

struct bip_context {
    uint64_t generation;
    uint64_t next_ipn;
    uint64_t mgmt_rsc;
    bool published;
    bool retired;
    bool freed;
};

struct bip_local_key {
    struct bip_context *context;
    bool owns_context;
};

struct bip_callback_key {
    uint64_t generation;
    bool valid;
    struct bip_context *private_context;
};

struct bip_tx_claim {
    struct bip_context *context;
    unsigned int slot;
    uint64_t ipn;
};

struct bip_rx_claim {
    struct bip_context *context;
    unsigned int slot;
    uint64_t observed_rsc;
};

struct bip_model {
    unsigned int readers;
    bool reap_scheduled;
    int active_tx_slot;
    struct bip_context *slot[bip_slot_count];
    struct bip_context *retired[4];
    size_t retired_count;
};

static void
bip_slot_assert(unsigned int slot)
{
    assert(slot < bip_slot_count);
}

static void
bip_reader_enter(struct bip_model *model)
{
    /* This precedes both the selected-slot lock and any k_priv observation. */
    model->readers++;
}

static struct bip_context *
bip_reader_observe(struct bip_model *model, unsigned int slot)
{
    struct bip_context *context;

    bip_slot_assert(slot);
    context = model->slot[slot];
    if (context == NULL || !context->published || context->retired ||
        context->freed) {
        assert(model->readers != 0);
        model->readers--;
        return NULL;
    }
    return context;
}

static struct bip_context *
bip_reader_acquire(struct bip_model *model, unsigned int slot)
{
    bip_reader_enter(model);
    return bip_reader_observe(model, slot);
}

static void
bip_reader_release(struct bip_model *model, struct bip_context *context)
{
    assert(context != NULL);
    assert(!context->freed);
    assert(model->readers != 0);
    model->readers--;
    /* Reader exit only drops a claim; it never frees a context. */
}

static bool
bip_try_reap(struct bip_model *model)
{
    size_t i;

    if (model->readers != 0) {
        model->reap_scheduled = true;
        return false;
    }
    for (i = 0; i < model->retired_count; i++) {
        assert(model->retired[i]->retired);
        assert(!model->retired[i]->freed);
        model->retired[i]->freed = true;
    }
    model->retired_count = 0;
    model->reap_scheduled = false;
    return true;
}

static bool
bip_lifetime_drain(struct bip_model *model)
{
    if (!bip_try_reap(model))
        return false;
    return model->readers == 0 && model->retired_count == 0 &&
        model->slot[bip_slot_4] == NULL && model->slot[bip_slot_5] == NULL;
}

static void
bip_retire(struct bip_model *model, struct bip_context *context)
{
    assert(context != NULL);
    assert(context->published);
    assert(!context->retired);
    assert(!context->freed);
    assert(model->retired_count < sizeof(model->retired) /
        sizeof(model->retired[0]));
    context->published = false;
    context->retired = true;
    model->retired[model->retired_count++] = context;
    model->reap_scheduled = true;
}

static void
bip_publish_local(struct bip_model *model, unsigned int slot,
    struct bip_local_key *local)
{
    struct bip_context *previous;
    struct bip_context *next;

    bip_slot_assert(slot);
    assert(local != NULL);
    assert(local->owns_context);
    next = local->context;
    assert(next != NULL);
    assert(!next->published);
    assert(!next->retired);
    assert(!next->freed);

    previous = model->slot[slot];
    next->published = true;
    model->slot[slot] = next;
    model->active_tx_slot = (int)slot;
    local->context = NULL;
    local->owns_context = false;
    if (previous != NULL)
        bip_retire(model, previous);
}

static void
bip_abort_unpublished(struct bip_local_key *local)
{
    struct bip_context *context;

    assert(local != NULL);
    assert(local->owns_context);
    context = local->context;
    assert(context != NULL);
    assert(!context->published);
    assert(!context->retired);
    context->freed = true;
    local->context = NULL;
    local->owns_context = false;
}

static struct bip_callback_key
bip_unpublish(struct bip_model *model, unsigned int slot)
{
    struct bip_callback_key callback = { 0 };
    struct bip_context *previous;
    unsigned int other;

    bip_slot_assert(slot);
    previous = model->slot[slot];
    assert(previous != NULL);
    callback.generation = previous->generation;
    callback.valid = true;
    /* Driver callback gets a value-only descriptor, never k_priv. */
    callback.private_context = NULL;
    model->slot[slot] = NULL;
    bip_retire(model, previous);
    if (model->active_tx_slot == (int)slot) {
        other = slot == bip_slot_4 ? bip_slot_5 : bip_slot_4;
        model->active_tx_slot = model->slot[other] == NULL ? -1 :
            (int)other;
    }
    return callback;
}

static struct bip_tx_claim
bip_tx_acquire_slot(struct bip_model *model, unsigned int slot)
{
    struct bip_tx_claim claim = { 0 };

    bip_slot_assert(slot);
    claim.context = bip_reader_acquire(model, slot);
    claim.slot = slot;
    if (claim.context == NULL)
        return claim;
    /* A formerly active slot may remain published strictly for MMIE RX.  It
     * must release its reader claim before reserving an IPN for TX. */
    if (model->active_tx_slot != (int)slot) {
        bip_reader_release(model, claim.context);
        claim.context = NULL;
        return claim;
    }
    claim.ipn = claim.context->next_ipn++;
    return claim;
}

static struct bip_tx_claim
bip_tx_acquire(struct bip_model *model)
{
    assert(model->active_tx_slot >= 0);
    return bip_tx_acquire_slot(model, (unsigned int)model->active_tx_slot);
}

static unsigned int
bip_get_txkey_slot(const struct bip_model *model)
{
    assert(model->active_tx_slot >= 0);
    return (unsigned int)model->active_tx_slot;
}

static bool
bip_tx_commit(const struct bip_model *model, const struct bip_tx_claim *claim)
{
    return claim != NULL && claim->context != NULL &&
        model->active_tx_slot == (int)claim->slot &&
        model->slot[claim->slot] == claim->context &&
        claim->context->published && !claim->context->retired;
}

static struct bip_rx_claim
bip_rx_acquire(struct bip_model *model, unsigned int slot)
{
    struct bip_rx_claim claim = { 0 };

    claim.context = bip_reader_acquire(model, slot);
    claim.slot = slot;
    if (claim.context != NULL)
        claim.observed_rsc = claim.context->mgmt_rsc;
    return claim;
}

static bool
bip_rx_validate_and_commit(struct bip_model *model,
    const struct bip_rx_claim *claim, bool mic_valid, uint64_t ipn)
{
    if (claim == NULL || claim->context == NULL || !mic_valid ||
        ipn <= claim->observed_rsc ||
        model->slot[claim->slot] != claim->context ||
        !claim->context->published || claim->context->retired ||
        ipn <= claim->context->mgmt_rsc)
        return false;
    claim->context->mgmt_rsc = ipn;
    return true;
}

static void
test_tx_key_domain_selection(void)
{
    unsigned int mfp;
    unsigned int multicast;
    unsigned int management;

    /* The concrete regression that previously routed ARP/IPv6 into BIP. */
    assert(bip_tx_key_select(false, true, true, false) == bip_tx_key_gtk);
    assert(bip_tx_key_select(false, true, true, true) == bip_tx_key_igtk);
    assert(bip_tx_key_select(true, true, false, true) ==
        bip_tx_key_pairwise);
    assert(bip_tx_key_select(false, false, true, true) == bip_tx_key_gtk);

    for (mfp = 0; mfp != 2; mfp++) {
        for (multicast = 0; multicast != 2; multicast++) {
            for (management = 0; management != 2; management++) {
                enum bip_tx_key_kind selected = bip_tx_key_select(false,
                    mfp != 0, multicast != 0, management != 0);

                if (selected == bip_tx_key_igtk)
                    assert(mfp != 0 && multicast != 0 && management != 0);
            }
        }
    }
}

static void
test_reader_before_same_slot_rekey(void)
{
    struct bip_model model = { .active_tx_slot = -1 };
    struct bip_context old = { .generation = 1 };
    struct bip_context next = { .generation = 2 };
    struct bip_local_key old_local = { &old, true };
    struct bip_local_key next_local = { &next, true };
    struct bip_context *reader;

    bip_publish_local(&model, bip_slot_4, &old_local);
    reader = bip_reader_acquire(&model, bip_slot_4);
    assert(reader == &old);
    bip_publish_local(&model, bip_slot_4, &next_local);
    assert(model.slot[bip_slot_4] == &next);
    assert(old.retired && !old.freed);
    assert(!bip_try_reap(&model));
    assert(model.reap_scheduled);
    assert(reader == &old && !reader->freed);
    bip_reader_release(&model, reader);
    assert(bip_try_reap(&model));
    assert(old.freed);
    assert(!next.freed);
}

static void
test_dual_slot_rx_and_tx_switch(void)
{
    struct bip_model model = { .active_tx_slot = -1 };
    struct bip_context old = { .generation = 10, .next_ipn = 100 };
    struct bip_context next = { .generation = 11, .next_ipn = 200 };
    struct bip_context later = { .generation = 12 };
    struct bip_local_key old_local = { &old, true };
    struct bip_local_key next_local = { &next, true };
    struct bip_local_key later_local = { &later, true };
    struct bip_context *old_reader;
    struct bip_context *next_reader;
    struct bip_tx_claim tx;

    bip_publish_local(&model, bip_slot_4, &old_local);
    old_reader = bip_reader_acquire(&model, bip_slot_4);
    bip_publish_local(&model, bip_slot_5, &next_local);
    assert(model.slot[bip_slot_4] == &old);
    assert(model.slot[bip_slot_5] == &next);
    assert(model.active_tx_slot == bip_slot_5);
    assert(!old.retired && !next.retired);
    next_reader = bip_reader_acquire(&model, bip_slot_5);
    assert(old_reader == &old && next_reader == &next);
    tx = bip_tx_acquire(&model);
    assert(tx.context == &next && tx.ipn == 200);
    assert(bip_tx_commit(&model, &tx));
    bip_reader_release(&model, tx.context);
    bip_reader_release(&model, next_reader);

    /* A later replacement of slot 4 retires only the former slot-4 context. */
    bip_publish_local(&model, bip_slot_4, &later_local);
    assert(model.slot[bip_slot_4] == &later);
    assert(model.slot[bip_slot_5] == &next);
    assert(old.retired && !next.retired);
    assert(!bip_try_reap(&model));
    bip_reader_release(&model, old_reader);
    assert(bip_try_reap(&model));
    assert(old.freed && !next.freed && !later.freed);
}

/* A reader enters before it locks the slot, so it cannot race reclamation. */
static void
test_reader_enter_before_publish_cannot_race_collection(void)
{
    struct bip_model model = { .active_tx_slot = -1 };
    struct bip_context old = { .generation = 15 };
    struct bip_context next = { .generation = 16 };
    struct bip_local_key old_local = { &old, true };
    struct bip_local_key next_local = { &next, true };
    struct bip_context *reader;

    bip_publish_local(&model, bip_slot_4, &old_local);
    bip_reader_enter(&model);
    bip_publish_local(&model, bip_slot_4, &next_local);
    assert(old.retired && !old.freed);
    assert(!bip_try_reap(&model));
    reader = bip_reader_observe(&model, bip_slot_4);
    assert(reader == &next);
    bip_reader_release(&model, reader);
    assert(bip_try_reap(&model));
    assert(old.freed);
}

static void
test_local_rollback_and_unpublish_callback(void)
{
    struct bip_model model = { .active_tx_slot = -1 };
    struct bip_context live = { .generation = 21 };
    struct bip_context local_context = { .generation = 22 };
    struct bip_local_key live_local = { &live, true };
    struct bip_local_key local = { &local_context, true };
    struct bip_context *reader;
    struct bip_callback_key callback;

    bip_publish_local(&model, bip_slot_4, &live_local);
    bip_abort_unpublished(&local);
    assert(local_context.freed);
    reader = bip_reader_acquire(&model, bip_slot_4);
    callback = bip_unpublish(&model, bip_slot_4);
    assert(callback.valid);
    assert(callback.generation == 21);
    assert(callback.private_context == NULL);
    assert(model.slot[bip_slot_4] == NULL);
    assert(model.active_tx_slot == -1);
    assert(live.retired && !live.freed);
    assert(!bip_try_reap(&model));
    bip_reader_release(&model, reader);
    assert(bip_try_reap(&model));
    assert(live.freed);
}

static void
test_terminal_drain_rejects_published_slots(void)
{
    struct bip_model model = { .active_tx_slot = -1 };
    struct bip_context live = { .generation = 25 };
    struct bip_local_key live_local = { &live, true };

    bip_publish_local(&model, bip_slot_4, &live_local);
    /* A final lifetime fence must not silently leak a still-live table slot. */
    assert(!bip_lifetime_drain(&model));
    (void)bip_unpublish(&model, bip_slot_4);
    assert(bip_lifetime_drain(&model));
    assert(live.freed);
}

static void
test_tx_ipn_reservation_and_active_rekey_commit(void)
{
    struct bip_model model = { .active_tx_slot = -1 };
    struct bip_context old = { .generation = 30, .next_ipn = 100 };
    struct bip_context next = { .generation = 31, .next_ipn = 200 };
    struct bip_context later = { .generation = 32 };
    struct bip_local_key old_local = { &old, true };
    struct bip_local_key next_local = { &next, true };
    struct bip_local_key later_local = { &later, true };
    struct bip_tx_claim first;
    struct bip_tx_claim second;
    struct bip_tx_claim stale;
    struct bip_tx_claim current;

    bip_publish_local(&model, bip_slot_4, &old_local);
    first = bip_tx_acquire(&model);
    second = bip_tx_acquire(&model);
    assert(first.context == &old && second.context == &old);
    assert(first.ipn == 100 && second.ipn == 101);
    assert(bip_tx_commit(&model, &first));
    assert(bip_tx_commit(&model, &second));
    stale = bip_tx_acquire(&model);
    assert(stale.ipn == 102);
    bip_publish_local(&model, bip_slot_5, &next_local);
    /* Slot 4 remains valid for RX, but cannot emit after active TX moved. */
    assert(!bip_tx_commit(&model, &stale));
    current = bip_tx_acquire(&model);
    assert(current.context == &next && current.ipn == 200);
    assert(bip_tx_commit(&model, &current));
    bip_reader_release(&model, first.context);
    bip_reader_release(&model, second.context);
    bip_reader_release(&model, stale.context);
    bip_reader_release(&model, current.context);

    bip_publish_local(&model, bip_slot_4, &later_local);
    assert(bip_try_reap(&model));
    assert(old.freed);
}

static void
test_tx_rx_only_slot_rejected_before_ipn_reservation(void)
{
    struct bip_model model = { .active_tx_slot = -1 };
    struct bip_context old = { .generation = 35, .next_ipn = 300 };
    struct bip_context next = { .generation = 36, .next_ipn = 400 };
    struct bip_local_key old_local = { &old, true };
    struct bip_local_key next_local = { &next, true };
    struct bip_tx_claim stale;
    struct bip_tx_claim current;
    unsigned int selected_by_get_txkey;

    bip_publish_local(&model, bip_slot_4, &old_local);
    /* get_txkey returns the old descriptor; a concurrent rekey follows
     * before BIP hold can take the selected-slot lock. */
    selected_by_get_txkey = bip_get_txkey_slot(&model);
    assert(selected_by_get_txkey == bip_slot_4);
    bip_publish_local(&model, bip_slot_5, &next_local);
    assert(model.slot[bip_slot_4] == &old && model.active_tx_slot ==
        bip_slot_5);

    /* Slot 4 stays valid for RX, but this pre-rekey descriptor must not reach
     * an IPN reservation or MMIE after active TX moved to slot 5. */
    stale = bip_tx_acquire_slot(&model, selected_by_get_txkey);
    assert(stale.context == NULL);
    assert(model.readers == 0);
    assert(old.next_ipn == 300);
    assert(next.next_ipn == 400);

    current = bip_tx_acquire(&model);
    assert(current.context == &next && current.ipn == 400);
    bip_reader_release(&model, current.context);
}

static void
test_rx_commit_is_per_slot_and_requires_valid_mic(void)
{
    struct bip_model model = { .active_tx_slot = -1 };
    struct bip_context old = { .generation = 40, .mgmt_rsc = 10 };
    struct bip_context next = { .generation = 41, .mgmt_rsc = 20 };
    struct bip_context later = { .generation = 42 };
    struct bip_local_key old_local = { &old, true };
    struct bip_local_key next_local = { &next, true };
    struct bip_local_key later_local = { &later, true };
    struct bip_rx_claim old_claim;
    struct bip_rx_claim stale;
    struct bip_rx_claim next_claim;

    bip_publish_local(&model, bip_slot_4, &old_local);
    old_claim = bip_rx_acquire(&model, bip_slot_4);
    bip_publish_local(&model, bip_slot_5, &next_local);
    /* Switching active TX does not invalidate legitimate old-slot MMIE RX. */
    assert(bip_rx_validate_and_commit(&model, &old_claim, true, 11));
    assert(old.mgmt_rsc == 11);
    bip_reader_release(&model, old_claim.context);

    next_claim = bip_rx_acquire(&model, bip_slot_5);
    assert(!bip_rx_validate_and_commit(&model, &next_claim, false, 21));
    assert(next.mgmt_rsc == 20);
    assert(!bip_rx_validate_and_commit(&model, &next_claim, true, 20));
    assert(bip_rx_validate_and_commit(&model, &next_claim, true, 21));
    assert(next.mgmt_rsc == 21);
    bip_reader_release(&model, next_claim.context);

    stale = bip_rx_acquire(&model, bip_slot_4);
    bip_publish_local(&model, bip_slot_4, &later_local);
    assert(!bip_rx_validate_and_commit(&model, &stale, true, 12));
    assert(old.mgmt_rsc == 11);
    bip_reader_release(&model, stale.context);
    assert(bip_try_reap(&model));
    assert(old.freed);
}

int
main(void)
{
    test_tx_key_domain_selection();
    test_reader_before_same_slot_rekey();
    test_dual_slot_rx_and_tx_switch();
    test_reader_enter_before_publish_cannot_race_collection();
    test_local_rollback_and_unpublish_callback();
    test_terminal_drain_rejects_published_slots();
    test_tx_ipn_reservation_and_active_rekey_commit();
    test_tx_rx_only_slot_rejected_before_ipn_reservation();
    test_rx_commit_is_per_slot_and_requires_valid_mic();
    return 0;
}
