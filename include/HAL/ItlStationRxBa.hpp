#ifndef ITL_STATION_RX_BA_HPP
#define ITL_STATION_RX_BA_HPP

#include "ItlFirmwareContextLease.hpp"

/* Primary RX completion and AP client work do not share a command gate.
 * Account their firmware resources without losing adjacent-owner updates.
 * This counter is not an allocation reservation; firmware can still refuse. */
struct ItlRxBaSessionCount {
    static int load(const int *count)
    {
        return __atomic_load_n(count, __ATOMIC_ACQUIRE);
    }
    static void add(int *count)
    {
        (void)__atomic_fetch_add(count, 1, __ATOMIC_ACQ_REL);
    }
    static void drop(int *count)
    {
        int prior = load(count);
        while (prior > 0 && !__atomic_compare_exchange_n(count, &prior, prior - 1,
            true, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {}
    }
    static void reset(int *count)
    {
        __atomic_store_n(count, 0, __ATOMIC_RELEASE);
    }
};

/* Host-only values. The HAL scan leaf owns this mailbox and resource ledger;
 * no ieee80211_node or timeout pointer crosses the hardware command wait. */
struct ItlStationRxBaRequest {
    uint64_t serial;
    uint64_t lifecycle;
    ItlFirmwareContextReceipt station;
    uint16_t ssn;
    uint16_t window;
    uint32_t timeout;
    uint8_t tid;
    uint8_t token;
    bool start;
    bool cleanup;
    bool hardwareBaid;
};

struct ItlStationRxBaResource {
    ItlFirmwareContextReceipt station;
    uint8_t baid;
    bool occupied;
    bool hardwareBaid;
    bool uncertain;
    bool firmwareRemoved;
    bool hostPublished;
    bool counted;
};

struct ItlStationRxBa {
    enum : unsigned { TidCount = 8, CleanupTid = TidCount, SessionLimit = 16 };
    enum class Phase : uint8_t { Idle, Hardware, Ready, Publishing };
    uint64_t nextSerial;
    uint64_t lifecycle;
    uint64_t latest[TidCount];
    /* Stop and successor start are separate: a new ADDBA cannot erase DELBA. */
    ItlStationRxBaRequest pending[2][TidCount];
    ItlStationRxBaRequest current;
    ItlFirmwareContextReceipt use;
    ItlStationRxBaResource resource[TidCount];
    Phase phase;
    int result;
    int cleanupError;
    uint8_t resultBaid;

    bool matches(const ItlStationRxBaRequest &request) const
    {
        return request.serial != 0 && current.serial == request.serial &&
            current.lifecycle == request.lifecycle;
    }

    void cancelPending()
    {
        for (unsigned tid = 0; tid < TidCount; ++tid) {
            latest[tid] = 0;
            pending[0][tid] = ItlStationRxBaRequest{};
            pending[1][tid] = ItlStationRxBaRequest{};
        }
    }

    bool occupied() const
    {
        if (phase != Phase::Idle)
            return true;
        for (const auto &entry : resource)
            if (entry.occupied)
                return true;
        return false;
    }
};

#endif
