#ifndef ITL_FIRMWARE_CONTEXT_LEASE_HPP
#define ITL_FIRMWARE_CONTEXT_LEASE_HPP

#include "ItlStateTransitionLease.hpp"

/* Host values, not a firmware wire layout. No node/PHY pointer survives a
 * command wait. The HAL serializes this owner under its scan leaf. */
struct ItlFirmwareContextIdentity {
    ItlStateTransitionIdentity attempt;
    uint32_t mac;
    uint32_t phy;
    uint32_t lmac;
    uint32_t commandLength;
    uint32_t station;
    int mode;
    uint8_t peer[6];

    bool sameEndpoint(const ItlFirmwareContextIdentity &other) const
    {
        if (!attempt.equals(other.attempt) || mac != other.mac ||
            mode != other.mode)
            return false;
        for (unsigned i = 0; i < sizeof(peer); ++i)
            if (peer[i] != other.peer[i])
                return false;
        return true;
    }

    bool equals(const ItlFirmwareContextIdentity &other) const
    {
        return sameEndpoint(other) && phy == other.phy && lmac == other.lmac &&
            commandLength == other.commandLength && station == other.station;
    }
};

struct ItlFirmwareContextReceipt {
    uint64_t serial;
    uint32_t generation;
    ItlFirmwareContextIdentity identity;
};

/* The ADD serial identifies an incarnation; MODIFY/REMOVE command serials
 * may change while an admitted constructor still owns its copied receipt.
 * clear/stop closes admission, but may not erase outstanding readers. */
struct ItlFirmwareStationUses {
    ItlFirmwareContextReceipt owner;
    uint32_t active;
    bool closed;
    bool retiring;

    bool start(const ItlFirmwareContextReceipt &station)
    {
        if (active != 0 || station.serial == 0)
            return false;
        owner = station;
        closed = false;
        retiring = false;
        return true;
    }

    bool acquire(const ItlFirmwareContextReceipt &station,
                 ItlFirmwareContextReceipt *receipt, bool hostRetirement = false)
    {
        if (receipt == nullptr || (closed && !hostRetirement) || owner.serial == 0 ||
            active == UINT32_MAX || owner.generation != station.generation ||
            !owner.identity.equals(station.identity))
            return false;
        ++active;
        *receipt = owner;
        return true;
    }

    void close(bool retire = true) { closed = true; retiring |= retire; }

    bool reopen(const ItlFirmwareContextReceipt &station)
    {
        if (active != 0 || retiring || owner.serial == 0 ||
            owner.generation != station.generation ||
            !owner.identity.equals(station.identity))
            return false;
        closed = false;
        return true;
    }

    bool release(ItlFirmwareContextReceipt *receipt)
    {
        if (receipt == nullptr || receipt->serial == 0 || active == 0 ||
            receipt->serial != owner.serial ||
            receipt->generation != owner.generation ||
            !receipt->identity.equals(owner.identity))
            return false;
        --active;
        *receipt = ItlFirmwareContextReceipt{};
        return true;
    }
};

template <class Driver, class Node>
class ItlFirmwareStationUseGuard {
    Driver *driver;
    ItlFirmwareContextReceipt receipt;
public:
    ItlFirmwareStationUseGuard(Driver *value, Node *node, bool currentAttempt = true) :
        driver(value), receipt{}
    { (void)driver->beginPrimaryStationUse(node, &receipt, currentAttempt); }
    ~ItlFirmwareStationUseGuard()
    { if (receipt.serial != 0) driver->endPrimaryStationUse(&receipt); }
    bool admitted() const { return receipt.serial != 0; }
    const ItlFirmwareContextReceipt &identity() const { return receipt; }
    ItlFirmwareStationUseGuard(const ItlFirmwareStationUseGuard &) = delete;
    ItlFirmwareStationUseGuard &operator=(const ItlFirmwareStationUseGuard &) = delete;
};

/* Confirmed teardown progress belongs to a station incarnation, not to a
 * particular retry's command serial. Clear on fresh ADD or actual stop. */
struct ItlFirmwareStationRetirement {
    enum : uint8_t { DrainEnabled = 1, Flushed = 2, DrainDisabled = 4, Removed = 8 };
    enum : unsigned { MaxQueues = 512 };
    ItlFirmwareContextIdentity identity;
    uint32_t generation;
    uint32_t flushQueues;
    int managementQueue;
    uint8_t completed;
    bool started;
    bool drain;
    uint64_t retiredQueues[MaxQueues / 64];

    bool owns(const ItlFirmwareContextReceipt &receipt) const
    {
        return started && generation == receipt.generation &&
            identity.equals(receipt.identity);
    }
    bool queueRetired(unsigned queue) const
    {
        return queue < MaxQueues && (retiredQueues[queue / 64] & (UINT64_C(1) << (queue % 64)));
    }
};

/* Stack-owned only until send_cmd returns. No pointer is retained in a TX
 * descriptor or asynchronous completion. submitted changes at the doorbell. */
struct ItlFirmwareContextCommand {
    enum class Kind : uint8_t { Mac, Binding, Station };
    ItlFirmwareContextReceipt receipt;
    Kind kind;
    bool cleanup;
    bool submitted;
};

struct ItlFirmwareContextLease {
    enum class Stage : uint8_t { Empty, Adding, Active, Modifying, Removing, Uncertain };
    enum class Operation : uint8_t { Add, Modify, Remove };
    enum class Admission : uint8_t { Submit, Already, Busy, Missing, Exhausted };
    enum class Completion : uint8_t { Success, Rejected, Uncertain };
    uint64_t nextSerial;
    ItlFirmwareContextReceipt owner;
    Stage stage;
    bool confirmed;
    bool uncertain;

    bool occupied() const { return stage != Stage::Empty; }

    bool commandCurrent(uint64_t serial, uint32_t generation) const
    {
        return serial != 0 && owner.serial == serial &&
            owner.generation == generation &&
            (stage == Stage::Adding || stage == Stage::Modifying || stage == Stage::Removing);
    }

    /* Only successful REMOVE, definitely rejected ADD, or actual hardware
     * stop may forget the owner. Logical request cancellation must not. */
    void clear()
    {
        owner = ItlFirmwareContextReceipt{};
        stage = Stage::Empty;
        confirmed = false;
        uncertain = false;
    }

    Admission begin(Operation operation, uint32_t generation,
                    const ItlFirmwareContextIdentity &identity,
                    ItlFirmwareContextReceipt *receipt)
    {
        if (receipt == nullptr)
            return Admission::Missing;
        if (occupied() && owner.generation != generation)
            return Admission::Busy;
        if (stage == Stage::Adding || stage == Stage::Modifying ||
            stage == Stage::Removing)
            return Admission::Busy;
        if (operation == Operation::Add && occupied())
            return stage == Stage::Active && owner.identity.equals(identity) ?
                Admission::Already : Admission::Busy;
        if (operation == Operation::Remove && !occupied())
            return Admission::Already;
        if (operation == Operation::Modify &&
            (stage != Stage::Active || !owner.identity.equals(identity)))
            return occupied() ? Admission::Busy : Admission::Missing;
        if (nextSerial == UINT64_MAX)
            return Admission::Exhausted;
        if (operation == Operation::Add) {
            owner.identity = identity;
            owner.generation = generation;
            confirmed = false;
            uncertain = false;
        }
        owner.serial = ++nextSerial;
        *receipt = owner;
        stage = operation == Operation::Add ? Stage::Adding :
            operation == Operation::Modify ? Stage::Modifying : Stage::Removing;
        return Admission::Submit;
    }

    bool finish(const ItlFirmwareContextReceipt &receipt, uint32_t generation,
                Completion completion)
    {
        if (receipt.serial == 0 || owner.serial != receipt.serial ||
            receipt.generation != generation || owner.generation != generation ||
            (stage != Stage::Adding && stage != Stage::Modifying && stage != Stage::Removing))
            return false;
        if (completion == Completion::Uncertain) {
            uncertain = true;
            stage = Stage::Uncertain;
            return true;
        }
        if ((stage == Stage::Removing && completion == Completion::Success) ||
            (stage == Stage::Adding && completion == Completion::Rejected)) {
            clear();
            return true;
        }
        if (completion == Completion::Success) {
            confirmed = true;
            uncertain = false;
        }
        stage = confirmed && !uncertain ? Stage::Active : Stage::Uncertain;
        return true;
    }
};

#endif
