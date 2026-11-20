/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * IOKit implementation of struct vap_platform for the guest transport core
 * (guest/core/vap_guest.c): BAR0 register access and DMA memory.
 *
 * Memory is cacheable: the "device" is a host process reading the same RAM,
 * so there is nothing to keep coherent with, and uncached rings would only
 * slow the guest down.
 */
#ifndef VAP_PLATFORM_IOKIT_HPP
#define VAP_PLATFORM_IOKIT_HPP

#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IODMACommand.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <libkern/OSByteOrder.h>

extern "C" {
#include "vap_guest.h"
}

struct VapDmaCookie {
    IOBufferMemoryDescriptor *bmd;
    IODMACommand *cmd;
};

class VapPlatformIOKit {
public:
    bool init(IOPCIDevice *pci)
    {
        fBar0 = pci->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0);
        if (!fBar0 || fBar0->getLength() < VAP_BAR0_SIZE) {
            if (fBar0) fBar0->release();
            fBar0 = nullptr;
            return false;
        }
        fRegs = reinterpret_cast<volatile uint8_t *>(fBar0->getVirtualAddress());
        return true;
    }

    void free()
    {
        if (fBar0) fBar0->release();
        fBar0 = nullptr;
        fRegs = nullptr;
    }

    struct vap_platform platform()
    {
        struct vap_platform pf = { this, rd32, wr32, dmaAlloc, dmaFree };
        return pf;
    }

private:
    static uint32_t rd32(void *ctx, uint32_t off)
    {
        VapPlatformIOKit *self = static_cast<VapPlatformIOKit *>(ctx);
        return OSReadLittleInt32(const_cast<uint8_t *>(self->fRegs), off);
    }

    static void wr32(void *ctx, uint32_t off, uint32_t val)
    {
        VapPlatformIOKit *self = static_cast<VapPlatformIOKit *>(ctx);
        OSWriteLittleInt32(const_cast<uint8_t *>(self->fRegs), off, val);
    }

    static int dmaAlloc(void *ctx, uint32_t len, struct vap_dma *out)
    {
        IODMACommand::Segment64 seg;
        UInt64 ofs = 0;
        UInt32 nsegs = 1;
        IOBufferMemoryDescriptor *bmd;
        IODMACommand *cmd;
        VapDmaCookie *ck;

        bmd = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
                  kIODirectionInOut | kIOMemoryPhysicallyContiguous, len, 0xFFFFFFFFFFFFF000ULL);
        if (!bmd) return -1;
        if (bmd->prepare() != kIOReturnSuccess) {
            bmd->release();
            return -1;
        }
        cmd = IODMACommand::withSpecification(kIODMACommandOutputHost64, 64, 0,
                                              IODMACommand::kMapped, 0, VAP_G_PAGE);
        if (!cmd || cmd->setMemoryDescriptor(bmd) != kIOReturnSuccess ||
            cmd->gen64IOVMSegments(&ofs, &seg, &nsegs) != kIOReturnSuccess || seg.fLength < len) {
            if (cmd) {
                cmd->clearMemoryDescriptor();
                cmd->release();
            }
            bmd->complete();
            bmd->release();
            return -1;
        }
        ck = IONew(VapDmaCookie, 1);
        if (!ck) {
            cmd->clearMemoryDescriptor();
            cmd->release();
            bmd->complete();
            bmd->release();
            return -1;
        }
        ck->bmd = bmd;
        ck->cmd = cmd;
        out->va = bmd->getBytesNoCopy();
        out->gpa = seg.fIOVMAddr;
        out->len = len;
        out->cookie = ck;
        bzero(out->va, len);
        return 0;
    }

    static void dmaFree(void *ctx, struct vap_dma *m)
    {
        VapDmaCookie *ck = static_cast<VapDmaCookie *>(m->cookie);

        if (!ck) return;
        ck->cmd->clearMemoryDescriptor();
        ck->cmd->release();
        ck->bmd->complete();
        ck->bmd->release();
        IODelete(ck, VapDmaCookie, 1);
        m->va = nullptr;
        m->cookie = nullptr;
    }

    IOMemoryMap *fBar0 = nullptr;
    volatile uint8_t *fRegs = nullptr;
};

#endif /* VAP_PLATFORM_IOKIT_HPP */
