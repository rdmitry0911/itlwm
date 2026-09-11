#ifndef ITL_SCAN_COMMAND_LEASE_HPP
#define ITL_SCAN_COMMAND_LEASE_HPP

#include <stdint.h>

/* Value-only ownership of one firmware scan. The caller serializes every
 * method with the scan leaf, including submission and its hardware doorbell.
 * Host serials never replace the firmware's bounded UID namespace. */
struct ItlScanCommandTerminal {
    uint64_t serial;
    uint64_t joinGeneration;
    uint64_t reassocSerial;
    uint32_t hardwareGeneration;
    uint32_t uid;
    bool umac;
    bool background;
    bool aborted;
    bool stopping;
};

struct ItlScanCommandLease {
    uint64_t nextSerial;
    uint64_t resetEpoch;
    uint32_t hardwareGeneration;
    bool open;
    uint64_t apSerial;
    ItlScanCommandTerminal command;
    bool submitted;
    bool upperReady;
    bool terminalSeen;
    bool abortSubmitted;

    bool live() const { return command.serial != 0; }

    void clearCommand()
    {
        command = ItlScanCommandTerminal{};
        submitted = false;
        upperReady = false;
        terminalSeen = false;
        abortSubmitted = false;
    }

    /* Call before hardware erasure. A saved init snapshot cannot reopen
     * admission if another stop/detach occurred while firmware init slept. */
    void invalidate()
    {
        open = false;
        apSerial = 0;
        if (resetEpoch != UINT64_MAX)
            ++resetEpoch;
        clearCommand();
    }

    bool reopen(uint64_t expectedResetEpoch, uint32_t generation)
    {
        if (expectedResetEpoch != resetEpoch || resetEpoch == UINT64_MAX ||
            live() || apSerial != 0)
            return false;
        hardwareGeneration = generation;
        open = true;
        return true;
    }

    uint64_t reserve(uint32_t generation, uint64_t joinGeneration,
                     bool umac, bool background, uint32_t uid,
                     uint64_t reassocSerial = 0)
    {
        if (!open || live() || apSerial != 0 || generation != hardwareGeneration ||
            nextSerial == UINT64_MAX || (background && joinGeneration != 0) ||
            (reassocSerial != 0 && (!background || joinGeneration != 0)))
            return 0;
        clearCommand();
        command.serial = ++nextSerial;
        command.joinGeneration = joinGeneration;
        command.reassocSerial = reassocSerial;
        command.hardwareGeneration = generation;
        command.uid = uid;
        command.umac = umac;
        command.background = background;
        return command.serial;
    }

    bool current(uint64_t serial, uint32_t generation) const
    {
        return open && serial != 0 && command.serial == serial &&
            command.hardwareGeneration == generation &&
            hardwareGeneration == generation;
    }

    uint64_t reserveAP(uint32_t generation)
    {
        if (!open || live() || apSerial != 0 ||
            generation != hardwareGeneration || nextSerial == UINT64_MAX)
            return 0;
        apSerial = ++nextSerial;
        return apSerial;
    }

    bool releaseAP(uint64_t serial, uint32_t generation)
    {
        if (!open || serial == 0 || serial != apSerial ||
            generation != hardwareGeneration)
            return false;
        apSerial = 0;
        return true;
    }

    bool quarantineAP(uint64_t serial, uint32_t generation)
    {
        if (!open || serial == 0 || serial != apSerial ||
            generation != hardwareGeneration)
            return false;
        open = false;
        return true;
    }

    bool submit(uint64_t serial, uint32_t generation)
    {
        if (!current(serial, generation) || submitted)
            return false;
        submitted = true;
        return true;
    }

    /* A returned command error is not proof that a doorbelled scan ended.
     * Only an unsubmitted reservation may be discarded by its sender. */
    bool rejectUnsubmitted(uint64_t serial, uint32_t generation)
    {
        if (!current(serial, generation) || submitted)
            return false;
        clearCommand();
        return true;
    }

    /* A transport error after publication is ambiguous. Keep its physical
     * ownership until hardware reset, and prevent a new scan from borrowing
     * this firmware UID in the meantime. */
    bool quarantine(uint64_t serial, uint32_t generation)
    {
        if (!current(serial, generation) || !submitted)
            return false;
        open = false;
        return true;
    }

    bool ready(uint64_t serial, uint32_t generation)
    {
        if (!current(serial, generation) || !submitted || upperReady)
            return false;
        upperReady = true;
        return true;
    }

    bool noteTerminal(uint32_t generation, bool umac, uint32_t uid,
                      bool aborted)
    {
        if (!current(command.serial, generation) || !submitted ||
            terminalSeen || command.umac != umac ||
            (umac && command.uid != uid))
            return false;
        terminalSeen = true;
        command.aborted = aborted;
        return true;
    }

    bool claimTerminal(uint64_t serial, uint32_t generation,
                       ItlScanCommandTerminal *out)
    {
        if (out == nullptr || !current(serial, generation) ||
            !submitted || !upperReady || !terminalSeen)
            return false;
        *out = command;
        clearCommand();
        return true;
    }

    bool beginAbort(uint64_t serial, uint32_t generation)
    {
        if (!current(serial, generation) || !submitted ||
            command.stopping)
            return false;
        command.stopping = true;
        return true;
    }

    bool submitAbort(uint64_t serial, uint32_t generation)
    {
        if (!current(serial, generation) || !submitted ||
            !command.stopping || terminalSeen || abortSubmitted)
            return false;
        abortSubmitted = true;
        return true;
    }

    bool abortSubmissionFailed(uint64_t serial, uint32_t generation)
    {
        if (!current(serial, generation) || !command.stopping ||
            terminalSeen || abortSubmitted)
            return false;
        command.stopping = false;
        return true;
    }
};

#endif
