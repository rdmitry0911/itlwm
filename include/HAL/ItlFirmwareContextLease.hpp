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
    int mode;
    uint8_t peer[6];

    bool equals(const ItlFirmwareContextIdentity &other) const
    {
        if (!attempt.equals(other.attempt) || mac != other.mac ||
            phy != other.phy || lmac != other.lmac ||
            commandLength != other.commandLength || mode != other.mode)
            return false;
        for (unsigned i = 0; i < sizeof(peer); ++i)
            if (peer[i] != other.peer[i])
                return false;
        return true;
    }
};

struct ItlFirmwareContextReceipt {
    uint64_t serial;
    uint32_t generation;
    ItlFirmwareContextIdentity identity;
};

/* Stack-owned only until send_cmd returns. No pointer is retained in a TX
 * descriptor or asynchronous completion. submitted changes at the doorbell. */
struct ItlFirmwareContextCommand {
    enum class Kind : uint8_t { Mac, Binding };
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
