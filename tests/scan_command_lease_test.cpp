#include "include/HAL/ItlScanCommandLease.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>

static unsigned cases;

static ItlScanCommandLease opened()
{
    ItlScanCommandLease lease = {};
    assert(lease.reopen(0, 7));
    return lease;
}

static void permutations()
{
    /* Every ordering of command ACK, upper publication and final firmware
     * notification. ACK has no authority over the physical scan lifetime. */
    for (bool umac : {false, true}) {
        for (bool aborted : {false, true}) {
            std::array<int, 3> actions = {{0, 1, 2}};
            do {
                auto lease = opened();
                const uint64_t serial = lease.reserve(7, 91, umac, false, 0);
                assert(serial && lease.submit(serial, 7));
                bool ready = false, terminal = false, consumed = false;
                for (int action : actions) {
                    if (action == 1) {
                        assert(lease.ready(serial, 7));
                        ready = true;
                    } else if (action == 2) {
                        assert(lease.noteTerminal(7, umac, 0, aborted));
                        terminal = true;
                    }
                    ItlScanCommandTerminal result = {};
                    const bool claim = lease.claimTerminal(serial, 7, &result);
                    assert(claim == (ready && terminal && !consumed));
                    if (claim) {
                        assert(result.serial == serial &&
                               result.joinGeneration == 91 &&
                               result.hardwareGeneration == 7 &&
                               result.aborted == aborted &&
                               result.umac == umac && !result.background);
                        consumed = true;
                    }
                }
                assert(consumed && !lease.live());
                ++cases;
            } while (std::next_permutation(actions.begin(), actions.end()));
        }
    }
}

static void invalid_receipts()
{
    for (bool umac : {false, true}) {
        auto lease = opened();
        const uint64_t serial = lease.reserve(7, 91, umac, false, 0);
        ItlScanCommandTerminal result = {};
        assert(!lease.reserve(7, 92, umac, false, 0));
        assert(!lease.ready(serial, 7));
        assert(!lease.noteTerminal(7, umac, 0, false));
        assert(!lease.claimTerminal(serial, 7, &result));
        assert(!lease.submit(serial + 1, 7));
        assert(!lease.submit(serial, 8));
        assert(!lease.quarantine(serial, 7));
        assert(lease.submit(serial, 7));
        assert(!lease.submit(serial, 7));
        assert(!lease.rejectUnsubmitted(serial, 7));
        assert(!lease.noteTerminal(8, umac, 0, false));
        assert(!lease.noteTerminal(7, !umac, 0, false));
        if (umac)
            assert(!lease.noteTerminal(7, true, 1, false));
        assert(lease.noteTerminal(7, umac, umac ? 0 : 1234, false));
        assert(!lease.noteTerminal(7, umac, 0, true));
        assert(!lease.ready(serial + 1, 7));
        assert(!lease.ready(serial, 8));
        assert(lease.ready(serial, 7));
        assert(!lease.ready(serial, 7));
        assert(!lease.claimTerminal(serial, 8, &result));
        assert(!lease.claimTerminal(serial, 7, nullptr));
        assert(lease.claimTerminal(serial, 7, &result));
        assert(!result.aborted);
        assert(!lease.claimTerminal(serial, 7, &result));
        const uint64_t replacement = lease.reserve(7, 92, umac, false, 0);
        assert(replacement > serial);
        assert(!lease.rejectUnsubmitted(serial, 7));
        assert(!lease.submit(serial, 7));
        assert(!lease.ready(serial, 7));
        assert(lease.submit(replacement, 7));
        assert(lease.ready(replacement, 7));
        assert(lease.noteTerminal(7, umac, 0, false));
        /* A copied old dispatch cannot consume a newer ready terminal. */
        assert(!lease.claimTerminal(serial, 7, &result));
        assert(lease.claimTerminal(replacement, 7, &result));
        assert(result.joinGeneration == 92);
        ++cases;
    }
}

static void reset_and_transport_error()
{
    for (unsigned stage = 0; stage != 5; ++stage) {
        auto lease = opened();
        const uint64_t epoch = lease.resetEpoch;
        const uint64_t serial = lease.reserve(7, 91, true, false, 0);
        if (stage >= 1)
            assert(lease.submit(serial, 7));
        if (stage >= 2)
            assert(lease.ready(serial, 7));
        if (stage >= 3)
            assert(lease.noteTerminal(7, true, 0, false));
        if (stage >= 4)
            assert(lease.quarantine(serial, 7));
        lease.invalidate();
        assert(!lease.open && !lease.live());
        assert(!lease.reopen(epoch, 7));
        assert(!lease.submit(serial, 7));
        assert(!lease.ready(serial, 7));
        assert(!lease.noteTerminal(7, true, 0, false));
        assert(lease.reopen(lease.resetEpoch, 8));
        assert(!lease.reserve(7, 92, true, false, 0));
        const uint64_t replacement = lease.reserve(8, 92, true, false, 0);
        assert(replacement > serial);
        assert(!lease.rejectUnsubmitted(serial, 8));
        assert(!lease.quarantine(serial, 8));
        assert(lease.command.joinGeneration == 92);
        ++cases;
    }
    auto lease = opened();
    const uint64_t serial = lease.reserve(7, 91, true, false, 0);
    assert(lease.submit(serial, 7));
    assert(lease.quarantine(serial, 7));
    assert(lease.live() && !lease.open && lease.submitted);
    assert(!lease.reopen(lease.resetEpoch, 7));
    assert(!lease.reserve(7, 92, true, false, 0));
    assert(!lease.ready(serial, 7));
    assert(!lease.rejectUnsubmitted(serial, 7));
    assert(!lease.noteTerminal(7, true, 0, false));
    lease.invalidate();
    assert(lease.reopen(lease.resetEpoch, 8));
    ++cases;
}

static void abort_and_exhaustion()
{
    auto lease = opened();
    uint64_t serial = lease.reserve(7, 91, true, false, 0);
    assert(!lease.beginAbort(serial, 7));
    assert(lease.submit(serial, 7));
    assert(!lease.beginAbort(serial + 1, 7));
    assert(!lease.beginAbort(serial, 8));
    assert(lease.beginAbort(serial, 7));
    assert(!lease.beginAbort(serial, 7));
    assert(!lease.abortSubmissionFailed(serial + 1, 7));
    assert(lease.abortSubmissionFailed(serial, 7));
    assert(lease.beginAbort(serial, 7));
    assert(lease.noteTerminal(7, true, 0, true));
    assert(!lease.abortSubmissionFailed(serial, 7));
    assert(lease.ready(serial, 7));
    ItlScanCommandTerminal terminal = {};
    assert(lease.claimTerminal(serial, 7, &terminal));
    assert(terminal.stopping && terminal.aborted);
    assert(!lease.reserve(7, 92, true, true, 0));
    serial = lease.reserve(7, 0, true, true, 0);
    assert(serial && lease.command.background &&
           lease.command.joinGeneration == 0);
    assert(lease.rejectUnsubmitted(serial, 7));
    lease.nextSerial = UINT64_MAX - 1;
    assert(lease.reserve(7, 91, true, false, 0) == UINT64_MAX);
    assert(lease.rejectUnsubmitted(UINT64_MAX, 7));
    assert(!lease.reserve(7, 91, true, false, 0));
    lease.invalidate();
    assert(lease.reopen(lease.resetEpoch, 8));
    assert(!lease.reserve(8, 91, true, false, 0));
    lease.resetEpoch = UINT64_MAX - 1;
    lease.invalidate();
    assert(lease.resetEpoch == UINT64_MAX);
    assert(!lease.reopen(UINT64_MAX, 8));
    lease.invalidate();
    assert(lease.resetEpoch == UINT64_MAX && !lease.open);
    ++cases;
}

int main()
{
    permutations();
    invalid_receipts();
    reset_and_transport_error();
    abort_and_exhaustion();
    std::printf("scan command lease: %u scenario groups passed\n", cases);
}
