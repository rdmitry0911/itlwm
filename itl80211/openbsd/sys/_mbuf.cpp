//
//  _mbuf.cpp
//  itlwm
//
//  Created by qcwap on 2020/6/14.
//  Copyright © 2020 钟先耀. All rights reserved.
//
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

#include <sys/_mbuf.h>
extern "C" {
#include <net/bpf.h>
}
#include <IOKit/IOCommandGate.h>

extern IOCommandGate *_fCommandGate;

struct network_header {
    char pad[0x48];
};

static IOReturn _if_input(OSObject *target, void *arg0, void *arg1, void *arg2, void *arg3)
{
    mbuf_t m, next;
    bool isEmpty = true;
    struct _ifnet *ifq = (struct _ifnet *)arg0;
    struct mbuf_list *ml = (struct mbuf_list *)arg1;
    const bool apsta = (uintptr_t)arg2 != 0;
    int (*skywalkRx)(struct _ifnet *, mbuf_t) =
        apsta ? ifq->if_skywalk_rx_ap : ifq->if_skywalk_rx;

    // Save next pointer before calling the handler, since the handler
    // may free the mbuf (Skywalk path) or take ownership (legacy path).
    for (m = MBUF_LIST_FIRST(ml); m != NULL; m = next) {
        next = MBUF_LIST_NEXT(m);
        isEmpty = false;
        if (skywalkRx) {
            // Skywalk path (macOS 26.x+): copy mbuf into IOSkywalkPacket,
            // enqueue to RX completion queue, free the mbuf.
            skywalkRx(ifq, m);
        } else if (!apsta && ifq->iface != NULL) {
            ifq->iface->inputPacket(m, 0, IONetworkInterface::kInputOptionQueuePacket);
        } else {
            // APSTA has no legacy IONetworkInterface fallback: its Tahoe
            // interface is backed by the role-specific Skywalk queue.
            mbuf_freem(m);
        }
        if (ifq->netStat != NULL) {
            ifq->netStat->inputPackets++;
        }
    }
    if (!isEmpty && !apsta && ifq->iface && !skywalkRx) {
        ifq->iface->flushInputQueue();
    }
    return kIOReturnSuccess;
}

int if_input(struct _ifnet *ifq, struct mbuf_list *ml)
{
    return _fCommandGate->runAction((IOCommandGate::Action)_if_input, ifq, ml);
}

int if_input_ap(struct _ifnet *ifq, struct mbuf_list *ml)
{
    return _fCommandGate->runAction((IOCommandGate::Action)_if_input, ifq, ml,
                                    reinterpret_cast<void *>(1));
}
