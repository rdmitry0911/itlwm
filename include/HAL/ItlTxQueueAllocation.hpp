#ifndef ITL_TX_QUEUE_ALLOCATION_HPP
#define ITL_TX_QUEUE_ALLOCATION_HPP

#include "ItlFirmwareContextLease.hpp"

/* Host-only pending command, held on the allocating worker's stack. The q0
 * sender sets submitted at the actual doorbell; it retains no pointer. */
struct ItlTxQueueAllocationCommand {
    uint64_t serial;
    uint64_t lifecycle;
    uint32_t generation;
    int queue; // -1: dynamic allocation, otherwise a preallocated host slot.
    uint8_t station;
    uint8_t tid;
    bool primary;
    bool submitted;
    ItlFirmwareContextReceipt use;
    bool retirement; // The serial owns the complete station/queue teardown.
};

/* This identity moves with a ring carrier. Allocated memory alone is not
 * firmware ownership: attach preallocates unused q1/q2 storage. */
struct ItlTxQueueFirmwareOwner {
    uint64_t serial;
    uint64_t lifecycle;
    uint32_t generation;
    uint8_t station;
    uint8_t tid;
    bool owned;
    bool uncertain;
    bool closing;
    bool removed; // Explicit REMOVE_QUEUE acknowledged; station still owns it.

    bool accepts(uint8_t expectedStation, uint32_t expectedGeneration) const
    {
        return owned && !uncertain && !closing && !removed &&
            station == expectedStation && generation == expectedGeneration;
    }
};

struct ItlTxQueueAllocation {
    enum class Phase : uint8_t { Idle, Building, Quarantined, Reclaiming };
    uint64_t nextSerial;
    uint64_t lifecycle;
    ItlTxQueueAllocationCommand current;
    Phase phase;

    bool matches(const ItlTxQueueAllocationCommand &command) const
    {
        return command.serial != 0 && current.serial == command.serial &&
            current.lifecycle == command.lifecycle &&
            current.generation == command.generation &&
            current.station == command.station && current.tid == command.tid &&
            current.queue == command.queue && current.retirement == command.retirement;
    }
    bool physical(const ItlTxQueueAllocationCommand &command, uint32_t generation) const
    {
        return matches(command) && command.lifecycle == lifecycle &&
            command.generation == generation;
    }
};

#endif
