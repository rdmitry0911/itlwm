/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "vap_guest.h"

#include <string.h>

enum { CTRL_FREE = 0, CTRL_FLIGHT = 1, CTRL_DONE = 2 };

struct vap_sg {
    uint64_t gpa;
    uint32_t len;
};

/* ---- memory ------------------------------------------------------------ */

static int pool_init(struct vap_dev *d, struct vap_pool *p, uint16_t n_bufs, uint32_t buf_len)
{
    uint16_t left = n_bufs;

    p->buf_len = buf_len;
    p->per_chunk = (uint16_t)(VAP_G_CHUNK / buf_len);
    p->n_bufs = n_bufs;
    p->n_chunks = 0;
    while (left) {
        uint16_t n = left < p->per_chunk ? left : p->per_chunk;
        if (p->n_chunks == sizeof(p->chunk) / sizeof(p->chunk[0])) return VAP_G_ENOMEM;
        if (d->pf.dma_alloc(d->pf.ctx, n * buf_len, &p->chunk[p->n_chunks])) return VAP_G_ENOMEM;
        p->n_chunks++;
        left -= n;
    }
    return VAP_G_OK;
}

static void pool_fini(struct vap_dev *d, struct vap_pool *p)
{
    while (p->n_chunks) d->pf.dma_free(d->pf.ctx, &p->chunk[--p->n_chunks]);
}

static uint8_t *pool_buf(const struct vap_pool *p, uint16_t i, uint64_t *gpa)
{
    const struct vap_dma *c = &p->chunk[i / p->per_chunk];
    uint32_t off = (uint32_t)(i % p->per_chunk) * p->buf_len;

    if (gpa) *gpa = c->gpa + off;
    return (uint8_t *)c->va + off;
}

/* ---- driver half of a split virtqueue ---------------------------------- */

static int gq_alloc(struct vap_dev *d, struct vap_gq *q, uint16_t index, uint16_t size)
{
    int i;

    q->index = index;
    q->size = size;
    for (i = 0; i < 3; i++)
        if (d->pf.dma_alloc(d->pf.ctx, VAP_G_PAGE, &q->mem[i])) return VAP_G_ENOMEM;
    q->desc = (struct vap_vq_desc *)q->mem[0].va;
    q->avail = (struct vap_vq_avail *)q->mem[1].va;
    q->used = (struct vap_vq_used *)q->mem[2].va;
    return VAP_G_OK;
}

static void gq_free(struct vap_dev *d, struct vap_gq *q)
{
    int i;

    for (i = 0; i < 3; i++)
        if (q->mem[i].va) d->pf.dma_free(d->pf.ctx, &q->mem[i]);
    memset(q, 0, sizeof(*q));
}

static void gq_reset(struct vap_gq *q)
{
    uint16_t i;

    memset(q->mem[0].va, 0, VAP_G_PAGE);
    memset(q->mem[1].va, 0, VAP_G_PAGE);
    memset(q->mem[2].va, 0, VAP_G_PAGE);
    for (i = 0; i < q->size; i++) q->desc[i].next = (uint16_t)(i + 1);
    q->free_head = 0;
    q->n_free = q->size;
    q->last_used = 0;
    q->avail_idx = 0;
}

static int gq_add(struct vap_gq *q, const struct vap_sg *sg, unsigned n_out, unsigned n_in, uint16_t token)
{
    unsigned n = n_out + n_in, k;
    uint16_t head, i;

    if (!n || n > q->n_free) return -1;
    head = i = q->free_head;
    for (k = 0; k < n; k++) {
        struct vap_vq_desc *dsc = &q->desc[i];
        uint16_t next = dsc->next;

        dsc->addr = sg[k].gpa;
        dsc->len = sg[k].len;
        dsc->flags = (uint16_t)((k >= n_out ? VAP_VQ_DESC_F_WRITE : 0) | (k + 1 < n ? VAP_VQ_DESC_F_NEXT : 0));
        if (k + 1 == n) q->free_head = next;
        i = next;
    }
    q->n_free = (uint16_t)(q->n_free - n);
    q->token[head] = token;
    q->avail->ring[q->avail_idx % q->size] = head;
    q->avail_idx++;
    __atomic_store_n(&q->avail->idx, q->avail_idx, __ATOMIC_RELEASE);
    return head;
}

static int gq_get_used(struct vap_gq *q, uint16_t *token, uint32_t *len)
{
    struct vap_vq_used_elem e;
    uint16_t i, n = 1;

    if (__atomic_load_n(&q->used->idx, __ATOMIC_ACQUIRE) == q->last_used) return 0;
    e = q->used->ring[q->last_used % q->size];
    q->last_used++;
    if (e.id >= q->size) return -1;
    for (i = (uint16_t)e.id; q->desc[i].flags & VAP_VQ_DESC_F_NEXT; i = q->desc[i].next) {
        if (q->desc[i].next >= q->size || n++ >= q->size) return -1;
    }
    q->desc[i].next = q->free_head;
    q->free_head = (uint16_t)e.id;
    q->n_free = (uint16_t)(q->n_free + n);
    *token = q->token[e.id];
    *len = e.len;
    return 1;
}

static void kick(struct vap_dev *d, struct vap_gq *q)
{
    /* The available index was just published; the device's flag must be read after it. */
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if (!(__atomic_load_n(&q->used->flags, __ATOMIC_RELAXED) & VAP_VQ_USED_F_NO_NOTIFY))
        d->pf.wr32(d->pf.ctx, VAP_REG_NOTIFY, q->index);
}

static void set_no_interrupt(struct vap_gq *q, int on)
{
    __atomic_store_n(&q->avail->flags, on ? VAP_VQ_AVAIL_F_NO_INTERRUPT : 0, __ATOMIC_SEQ_CST);
}

/* ---- device ------------------------------------------------------------ */

static uint32_t rd(struct vap_dev *d, uint32_t off) { return d->pf.rd32(d->pf.ctx, off); }
static void wr(struct vap_dev *d, uint32_t off, uint32_t v) { d->pf.wr32(d->pf.ctx, off, v); }

static int post_writable(struct vap_gq *q, const struct vap_pool *p, uint16_t buf)
{
    struct vap_sg sg;

    pool_buf(p, buf, &sg.gpa);
    sg.len = p->buf_len;
    return gq_add(q, &sg, 0, 1, buf);
}

void vap_dev_stop(struct vap_dev *d)
{
    wr(d, VAP_REG_STATUS, 0);
    d->running = 0;
}

int vap_dev_start(struct vap_dev *d)
{
    uint32_t status;
    uint16_t i;

    wr(d, VAP_REG_STATUS, 0);
    d->running = 0;
    if (rd(d, VAP_REG_MAGIC) != VAP_MAGIC || rd(d, VAP_REG_VERSION) != VAP_TRANSPORT_VERSION) return VAP_G_EIO;
    status = VAP_STATUS_ACKNOWLEDGE | VAP_STATUS_DRIVER;
    wr(d, VAP_REG_STATUS, status);
    wr(d, VAP_REG_DRV_FEATURES, 0);
    status |= VAP_STATUS_FEATURES_OK;
    wr(d, VAP_REG_STATUS, status);
    if (!(rd(d, VAP_REG_STATUS) & VAP_STATUS_FEATURES_OK) || rd(d, VAP_REG_NUM_QUEUES) < VAP_NUM_QUEUES) goto fail;

    for (i = 0; i < VAP_NUM_QUEUES; i++) {
        struct vap_gq *q = &d->q[i];

        gq_reset(q);
        wr(d, VAP_REG_QUEUE_SEL, i);
        if (rd(d, VAP_REG_QUEUE_SIZE) < q->size) goto fail;
        wr(d, VAP_REG_QUEUE_SIZE, q->size);
        wr(d, VAP_REG_QUEUE_DESC_LO, (uint32_t)q->mem[0].gpa);
        wr(d, VAP_REG_QUEUE_DESC_HI, (uint32_t)(q->mem[0].gpa >> 32));
        wr(d, VAP_REG_QUEUE_AVAIL_LO, (uint32_t)q->mem[1].gpa);
        wr(d, VAP_REG_QUEUE_AVAIL_HI, (uint32_t)(q->mem[1].gpa >> 32));
        wr(d, VAP_REG_QUEUE_USED_LO, (uint32_t)q->mem[2].gpa);
        wr(d, VAP_REG_QUEUE_USED_HI, (uint32_t)(q->mem[2].gpa >> 32));
        wr(d, VAP_REG_QUEUE_ENABLE, 1);
    }

    memset(d->ctrl_state, CTRL_FREE, sizeof(d->ctrl_state));
    for (i = 0; i < VAP_G_TX_SLOTS; i++) d->tx_free[i] = i;
    d->n_tx_free = VAP_G_TX_SLOTS;
    d->tx_starved = 0;
    /* Transmit completions are collected on the next transmit, not by interrupt. */
    set_no_interrupt(&d->q[VAP_Q_TX], 1);
    for (i = 0; i < VAP_G_EVENT_BUFS; i++) post_writable(&d->q[VAP_Q_EVENT], &d->event_pool, i);
    for (i = 0; i < VAP_G_RX_BUFS; i++) post_writable(&d->q[VAP_Q_RX], &d->rx_pool, i);

    wr(d, VAP_REG_STATUS, status | VAP_STATUS_DRIVER_OK);
    d->running = 1;
    kick(d, &d->q[VAP_Q_EVENT]);
    kick(d, &d->q[VAP_Q_RX]);
    return VAP_G_OK;

fail:
    wr(d, VAP_REG_STATUS, VAP_STATUS_FAILED);
    return VAP_G_EIO;
}

int vap_dev_init(struct vap_dev *d, const struct vap_platform *pf, const struct vap_callbacks *cb)
{
    static const uint16_t qsize[VAP_NUM_QUEUES] = {
        VAP_G_CTRL_QSIZE, VAP_G_EVENT_QSIZE, VAP_G_TX_QSIZE, VAP_G_RX_QSIZE,
    };
    int rc = VAP_G_OK;
    uint16_t i;

    memset(d, 0, sizeof(*d));
    d->pf = *pf;
    d->cb = *cb;
    for (i = 0; i < VAP_NUM_QUEUES && !rc; i++) rc = gq_alloc(d, &d->q[i], i, qsize[i]);
    if (!rc) rc = pool_init(d, &d->ctrl_pool, 2 * VAP_G_CTRL_SLOTS, VAP_G_CTRL_MAX);
    if (!rc) rc = pool_init(d, &d->event_pool, VAP_G_EVENT_BUFS, VAP_G_EVENT_MAX);
    if (!rc) rc = pool_init(d, &d->tx_pool, VAP_G_TX_SLOTS, VAP_G_FRAME_BUF);
    if (!rc) rc = pool_init(d, &d->rx_pool, VAP_G_RX_BUFS, VAP_G_FRAME_BUF);
    if (!rc) rc = vap_dev_start(d);
    if (rc) vap_dev_fini(d);
    return rc;
}

void vap_dev_fini(struct vap_dev *d)
{
    uint16_t i;

    if (!d->pf.wr32) return;
    vap_dev_stop(d);
    pool_fini(d, &d->rx_pool);
    pool_fini(d, &d->tx_pool);
    pool_fini(d, &d->event_pool);
    pool_fini(d, &d->ctrl_pool);
    for (i = 0; i < VAP_NUM_QUEUES; i++) gq_free(d, &d->q[i]);
}

int vap_dev_needs_reset(struct vap_dev *d)
{
    return (rd(d, VAP_REG_STATUS) & VAP_STATUS_NEEDS_RESET) != 0;
}

int vap_dev_lost(struct vap_dev *d)
{
    /* All ones is a device that is gone, not one that was reset. */
    uint32_t s;

    if (!d->running) return 0;
    s = rd(d, VAP_REG_STATUS);
    return s != 0xffffffffu && !(s & VAP_STATUS_DRIVER_OK);
}

static void reclaim_tx(struct vap_dev *d)
{
    uint16_t slot;
    uint32_t len;

    while (gq_get_used(&d->q[VAP_Q_TX], &slot, &len) > 0)
        if (slot < VAP_G_TX_SLOTS && d->n_tx_free < VAP_G_TX_SLOTS) d->tx_free[d->n_tx_free++] = slot;
}

static void poll_ctrl(struct vap_dev *d, int notify)
{
    uint16_t tok;
    uint32_t len;

    while (gq_get_used(&d->q[VAP_Q_CTRL], &tok, &len) > 0) {
        if (tok >= VAP_G_CTRL_SLOTS || d->ctrl_state[tok] != CTRL_FLIGHT) continue;
        d->ctrl_rsp_len[tok] = len;
        d->ctrl_state[tok] = CTRL_DONE;
        if (notify && d->cb.ctrl_done) d->cb.ctrl_done(d->cb.ctx, tok);
    }
}

void vap_dev_poll_ctrl(struct vap_dev *d)
{
    if (d->running) poll_ctrl(d, 0);
}

void vap_dev_poll(struct vap_dev *d)
{
    uint16_t tok, ev_limit;
    uint32_t len;
    int reposted;

    if (!d->running) return;
    poll_ctrl(d, 1);

    /*
     * The firmware publishes an event only after every frame it produced
     * before that event. Taking the event index first and draining the
     * receive queue after it delivers frames ahead of the events that follow
     * them (scan results before SCAN_DONE).
     */
    ev_limit = __atomic_load_n(&d->q[VAP_Q_EVENT].used->idx, __ATOMIC_ACQUIRE);

    reposted = 0;
    while (gq_get_used(&d->q[VAP_Q_RX], &tok, &len) > 0) {
        if (tok >= VAP_G_RX_BUFS) continue;
        if (len >= sizeof(struct vap_rx_meta) && len <= VAP_G_FRAME_BUF) {
            const struct vap_rx_meta *m = (const struct vap_rx_meta *)(const void *)pool_buf(&d->rx_pool, tok, NULL);
            if (m->frame_len <= len - sizeof(*m) && d->cb.rx)
                d->cb.rx(d->cb.ctx, m, (const uint8_t *)(m + 1), m->frame_len);
        }
        post_writable(&d->q[VAP_Q_RX], &d->rx_pool, tok);
        reposted = 1;
    }
    if (reposted) kick(d, &d->q[VAP_Q_RX]);

    reposted = 0;
    while (d->q[VAP_Q_EVENT].last_used != ev_limit && gq_get_used(&d->q[VAP_Q_EVENT], &tok, &len) > 0) {
        if (tok >= VAP_G_EVENT_BUFS) continue;
        if (len >= sizeof(struct vap_hdr) && len <= VAP_G_EVENT_MAX) {
            const struct vap_hdr *h = (const struct vap_hdr *)(const void *)pool_buf(&d->event_pool, tok, NULL);
            if (h->kind == VAP_KIND_EVT && h->len <= len - sizeof(*h) && d->cb.event)
                d->cb.event(d->cb.ctx, h, h + 1, h->len);
        }
        post_writable(&d->q[VAP_Q_EVENT], &d->event_pool, tok);
        reposted = 1;
    }
    if (reposted) kick(d, &d->q[VAP_Q_EVENT]);

    if (d->tx_starved) {
        reclaim_tx(d);
        if (d->n_tx_free) {
            d->tx_starved = 0;
            set_no_interrupt(&d->q[VAP_Q_TX], 1);
            if (d->cb.tx_ready) d->cb.tx_ready(d->cb.ctx);
        }
    }
}

int vap_ctrl_send(struct vap_dev *d, uint16_t type, const void *payload, uint32_t len)
{
    struct vap_sg sg[2];
    struct vap_hdr *h;
    int slot;

    if (!d->running) return VAP_G_EIO;
    if (len > VAP_G_CTRL_MAX - sizeof(*h)) return VAP_G_EINVAL;
    for (slot = 0; slot < (int)VAP_G_CTRL_SLOTS && d->ctrl_state[slot] != CTRL_FREE; slot++) {}
    if (slot == (int)VAP_G_CTRL_SLOTS) return VAP_G_BUSY;

    h = (struct vap_hdr *)(void *)pool_buf(&d->ctrl_pool, (uint16_t)(2 * slot), &sg[0].gpa);
    memset(h, 0, sizeof(*h));
    h->type = type;
    h->kind = VAP_KIND_REQ;
    h->len = len;
    h->seq = ++d->seq;
    if (len) memcpy(h + 1, payload, len);
    sg[0].len = (uint32_t)sizeof(*h) + len;
    pool_buf(&d->ctrl_pool, (uint16_t)(2 * slot + 1), &sg[1].gpa);
    sg[1].len = VAP_G_CTRL_MAX;
    if (gq_add(&d->q[VAP_Q_CTRL], sg, 1, 1, (uint16_t)slot) < 0) return VAP_G_BUSY;
    d->ctrl_seq[slot] = h->seq;
    d->ctrl_state[slot] = CTRL_FLIGHT;
    kick(d, &d->q[VAP_Q_CTRL]);
    return slot;
}

int vap_ctrl_done(const struct vap_dev *d, int slot)
{
    return slot >= 0 && slot < (int)VAP_G_CTRL_SLOTS && d->ctrl_state[slot] == CTRL_DONE;
}

int vap_ctrl_result(struct vap_dev *d, int slot, const void **payload, uint32_t *len)
{
    const struct vap_hdr *h;
    uint32_t got;

    if (!vap_ctrl_done(d, slot)) return VAP_G_EINVAL;
    h = (const struct vap_hdr *)(const void *)pool_buf(&d->ctrl_pool, (uint16_t)(2 * slot + 1), NULL);
    got = d->ctrl_rsp_len[slot];
    if (got < sizeof(*h) || got > VAP_G_CTRL_MAX || h->kind != VAP_KIND_RSP ||
        h->seq != d->ctrl_seq[slot] || h->len > got - sizeof(*h))
        return VAP_G_EIO;
    if (payload) *payload = h + 1;
    if (len) *len = h->len;
    return h->status;
}

void vap_ctrl_release(struct vap_dev *d, int slot)
{
    if (slot >= 0 && slot < (int)VAP_G_CTRL_SLOTS && d->ctrl_state[slot] == CTRL_DONE)
        d->ctrl_state[slot] = CTRL_FREE;
}

int vap_tx(struct vap_dev *d, const struct vap_tx_meta *meta, const struct vap_iov *iov, unsigned n)
{
    struct vap_tx_meta *m;
    struct vap_sg sg;
    uint32_t total = 0;
    uint16_t slot;
    unsigned i;
    uint8_t *p;

    if (!d->running) return VAP_G_EIO;
    for (i = 0; i < n; i++) total += iov[i].len;
    if (total > VAP_G_FRAME_BUF - sizeof(*m) || total > 0xffffu) return VAP_G_EINVAL;

    if (!d->n_tx_free) {
        reclaim_tx(d);
        if (!d->n_tx_free) {
            /* Ask for an interrupt, then look once more: a completion may have landed in between. */
            d->tx_starved = 1;
            set_no_interrupt(&d->q[VAP_Q_TX], 0);
            reclaim_tx(d);
            if (!d->n_tx_free) return VAP_G_BUSY;
            d->tx_starved = 0;
            set_no_interrupt(&d->q[VAP_Q_TX], 1);
        }
    }

    slot = d->tx_free[--d->n_tx_free];
    m = (struct vap_tx_meta *)(void *)pool_buf(&d->tx_pool, slot, &sg.gpa);
    *m = *meta;
    m->frame_len = (uint16_t)total;
    p = (uint8_t *)(m + 1);
    for (i = 0; i < n; i++) {
        memcpy(p, iov[i].p, iov[i].len);
        p += iov[i].len;
    }
    sg.len = (uint32_t)sizeof(*m) + total;
    if (gq_add(&d->q[VAP_Q_TX], &sg, 1, 0, slot) < 0) {
        d->tx_free[d->n_tx_free++] = slot;
        return VAP_G_BUSY;
    }
    kick(d, &d->q[VAP_Q_TX]);
    return VAP_G_OK;
}
