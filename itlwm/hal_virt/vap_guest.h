/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Guest side of the v-Airport transport: device bring-up, the driver half of
 * the split virtqueues, and the four channels (control, events, transmit,
 * receive). Plain C with no operating-system dependency: the kext supplies
 * register access and DMA memory through vap_platform, and so does the host
 * selftest, which runs this same code against the real plugin.
 *
 * Not thread-safe: the caller serializes every call on one device.
 */
#ifndef VAP_GUEST_H
#define VAP_GUEST_H

#include <stdint.h>

#include "vap_proto.h"
#include "vap_regs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VAP_G_PAGE         4096u
#define VAP_G_CHUNK        65536u   /* largest contiguous allocation asked of the platform */

#define VAP_G_CTRL_QSIZE   16u
#define VAP_G_EVENT_QSIZE  64u
#define VAP_G_TX_QSIZE     256u
#define VAP_G_RX_QSIZE     256u

#define VAP_G_CTRL_SLOTS   4u       /* requests in flight */
#define VAP_G_CTRL_MAX     VAP_G_PAGE   /* header + payload, each direction */
#define VAP_G_EVENT_BUFS   32u
#define VAP_G_EVENT_MAX    1024u
#define VAP_G_TX_SLOTS     128u
#define VAP_G_RX_BUFS      256u
#define VAP_G_FRAME_BUF    VAP_G_PAGE   /* meta + frame */

#define VAP_G_OK       0
#define VAP_G_BUSY     (-100)   /* no free slot; for vap_tx() tx_ready() follows */
#define VAP_G_EIO      (-101)   /* the device misbehaved or is not a v-Airport device */
#define VAP_G_EINVAL   (-102)
#define VAP_G_ENOMEM   (-103)

struct vap_dma {
    void    *va;
    uint64_t gpa;       /* guest-physical address the device will be given */
    uint32_t len;
    void    *cookie;    /* for the platform's dma_free */
};

struct vap_platform {
    void *ctx;
    uint32_t (*rd32)(void *ctx, uint32_t off);
    void     (*wr32)(void *ctx, uint32_t off, uint32_t val);
    /* Zeroed, page-aligned, physically contiguous, len <= VAP_G_CHUNK. 0 or nonzero. */
    int      (*dma_alloc)(void *ctx, uint32_t len, struct vap_dma *out);
    void     (*dma_free)(void *ctx, struct vap_dma *m);
};

/* Called from vap_dev_poll(). Pointers are valid only during the call. */
struct vap_callbacks {
    void *ctx;
    void (*ctrl_done)(void *ctx, int slot);
    void (*event)(void *ctx, const struct vap_hdr *h, const void *payload, uint32_t len);
    void (*rx)(void *ctx, const struct vap_rx_meta *meta, const uint8_t *frame, uint32_t len);
    void (*tx_ready)(void *ctx);
};

struct vap_iov {
    const void *p;
    uint32_t    len;
};

struct vap_gq {
    uint16_t index, size;
    uint16_t free_head, n_free;
    uint16_t last_used, avail_idx;
    struct vap_dma mem[3];
    struct vap_vq_desc  *desc;
    struct vap_vq_avail *avail;
    struct vap_vq_used  *used;
    uint16_t token[VAP_QUEUE_MAX_SIZE];
};

struct vap_pool {
    struct vap_dma chunk[16];
    uint16_t n_chunks, per_chunk, n_bufs;
    uint32_t buf_len;
};

struct vap_dev {
    struct vap_platform  pf;
    struct vap_callbacks cb;
    struct vap_gq   q[VAP_NUM_QUEUES];
    struct vap_pool ctrl_pool, event_pool, tx_pool, rx_pool;
    uint8_t  ctrl_state[VAP_G_CTRL_SLOTS];
    uint32_t ctrl_seq[VAP_G_CTRL_SLOTS];
    uint32_t ctrl_rsp_len[VAP_G_CTRL_SLOTS];
    uint16_t tx_free[VAP_G_TX_SLOTS];
    uint16_t n_tx_free;
    uint8_t  tx_starved;
    uint8_t  running;
    uint32_t seq;
};

/* Allocate memory and bring the device up. */
int  vap_dev_init(struct vap_dev *d, const struct vap_platform *pf, const struct vap_callbacks *cb);
/* Reset the device and free everything. */
void vap_dev_fini(struct vap_dev *d);
/* Bring the device up again on the memory already allocated (firmware restart, wake). */
int  vap_dev_start(struct vap_dev *d);
void vap_dev_stop(struct vap_dev *d);
/* VAP_STATUS_NEEDS_RESET: the firmware restarted, call vap_dev_start(). */
int  vap_dev_needs_reset(struct vap_dev *d);
/*
 * The device was reset under a running driver and no longer has the queues
 * (a virtual machine's wake from S3 resets every device): call vap_dev_start().
 * One register read, a round trip to the host: not for the data path.
 */
int  vap_dev_lost(struct vap_dev *d);

/* Drain completed work and invoke the callbacks. Call on every interrupt. */
void vap_dev_poll(struct vap_dev *d);
/*
 * Collect control replies only, without delivering events or frames: for a
 * caller waiting on a request from a context that must not be re-entered.
 */
void vap_dev_poll_ctrl(struct vap_dev *d);

/*
 * Send a request. Returns a slot >= 0 or VAP_G_*. When ctrl_done(slot) has
 * been called, read the reply with vap_ctrl_result() and free the slot with
 * vap_ctrl_release().
 */
int  vap_ctrl_send(struct vap_dev *d, uint16_t type, const void *payload, uint32_t len);
int  vap_ctrl_done(const struct vap_dev *d, int slot);
/* The firmware's status (0 or VAP_E_*), or VAP_G_EIO for a malformed reply. */
int  vap_ctrl_result(struct vap_dev *d, int slot, const void **payload, uint32_t *len);
void vap_ctrl_release(struct vap_dev *d, int slot);

/* Queue one frame; meta->frame_len is filled in here. */
int  vap_tx(struct vap_dev *d, const struct vap_tx_meta *meta, const struct vap_iov *iov, unsigned n);

#ifdef __cplusplus
}
#endif

#endif /* VAP_GUEST_H */
