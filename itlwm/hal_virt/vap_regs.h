/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * v-Airport device: PCI identity, BAR0 register block and queue layout.
 *
 * The queues are VIRTIO 1.x split virtqueues (descriptor table, available
 * ring, used ring) in guest memory. The register block is a reduced form of
 * the virtio-pci common configuration: same meaning, fixed offsets, no
 * capability list. One MSI vector; the driver finds work by reading the used
 * rings, never a status register. The only register touched on the hot path
 * is VAP_REG_NOTIFY, written and not read.
 */
#ifndef VAP_REGS_H
#define VAP_REGS_H

#include <stdint.h>

#define VAP_PCI_VENDOR   0x1234u
#define VAP_PCI_DEVICE   0x8021u
#define VAP_PCI_CLASS    0x028000u      /* network controller, other */

#define VAP_BAR0_SIZE    0x1000u

/* All registers are 32-bit, little-endian, accessed as whole words. */
#define VAP_REG_MAGIC          0x00     /* RO  VAP_MAGIC */
#define VAP_REG_VERSION        0x04     /* RO  VAP_TRANSPORT_VERSION */
#define VAP_REG_DEV_FEATURES   0x08     /* RO  transport features offered */
#define VAP_REG_DRV_FEATURES   0x0c     /* RW  transport features accepted */
#define VAP_REG_STATUS         0x10     /* RW  VAP_STATUS_*; writing 0 resets the device */
#define VAP_REG_NUM_QUEUES     0x14     /* RO */
#define VAP_REG_QUEUE_SEL      0x18     /* RW  selects the queue the QUEUE_* registers address */
#define VAP_REG_QUEUE_SIZE     0x1c     /* RW  reads the maximum after reset; power of two */
#define VAP_REG_QUEUE_DESC_LO  0x20     /* RW  guest-physical addresses of the three ring parts */
#define VAP_REG_QUEUE_DESC_HI  0x24
#define VAP_REG_QUEUE_AVAIL_LO 0x28
#define VAP_REG_QUEUE_AVAIL_HI 0x2c
#define VAP_REG_QUEUE_USED_LO  0x30
#define VAP_REG_QUEUE_USED_HI  0x34
#define VAP_REG_QUEUE_ENABLE   0x38     /* RW  1 = ring addresses are final */
#define VAP_REG_NOTIFY         0x40     /* WO  queue index: new entries in its available ring */

#define VAP_MAGIC              0x31504156u   /* "VAP1" */
#define VAP_TRANSPORT_VERSION  1u

#define VAP_STATUS_ACKNOWLEDGE 0x01u
#define VAP_STATUS_DRIVER      0x02u
#define VAP_STATUS_DRIVER_OK   0x04u
#define VAP_STATUS_FEATURES_OK 0x08u
#define VAP_STATUS_NEEDS_RESET 0x40u   /* set by the device: firmware restarted, rings are void */
#define VAP_STATUS_FAILED      0x80u

/* Queue indices. */
#define VAP_Q_CTRL  0u   /* guest -> firmware requests; the reply rides in the chain's writable tail */
#define VAP_Q_EVENT 1u   /* firmware -> guest events, into buffers the guest posted */
#define VAP_Q_TX    2u   /* guest -> firmware frames */
#define VAP_Q_RX    3u   /* firmware -> guest frames, into buffers the guest posted */
#define VAP_NUM_QUEUES 4u

#define VAP_QUEUE_MAX_SIZE 256u

/* Split virtqueue, as in VIRTIO 1.x section 2.7. */
struct vap_vq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};
#define VAP_VQ_DESC_F_NEXT  1u
#define VAP_VQ_DESC_F_WRITE 2u

struct vap_vq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
};
#define VAP_VQ_AVAIL_F_NO_INTERRUPT 1u   /* driver: do not interrupt, I am already polling */

struct vap_vq_used_elem {
    uint32_t id;
    uint32_t len;
};
struct vap_vq_used {
    uint16_t flags;
    uint16_t idx;
    struct vap_vq_used_elem ring[];
};
#define VAP_VQ_USED_F_NO_NOTIFY 1u       /* device: do not notify, I am already polling */

#endif /* VAP_REGS_H */
