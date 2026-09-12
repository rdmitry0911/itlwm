// Observable 25C56 WCLRoamManager transitions, re-expressed from the raw
// six-state/ten-event table. This is a consumer oracle, not Intel firmware.
#ifndef ReferenceRoamFsm_hpp
#define ReferenceRoamFsm_hpp
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

struct ReferenceRoamFsm {
    enum State : uint8_t { LinkDown, LinkUp, Scan, Reassoc, WaitDone, Sleep };
    struct Cell { uint8_t next, action; };
    static constexpr uint8_t Same = 255;
    inline static constexpr Cell table[6][10] = {
        {{1,2},{0,0},{Same,1},{Same,1},{Same,1},{Same,1},{Same,1},{Same,1},{Same,1},{Same,1}},
        {{Same,1},{0,3},{2,4},{Same,1},{1,6},{1,0},{1,0},{5,9},{Same,1},{Same,1}},
        {{Same,1},{0,3},{2,4},{2,5},{3,6},{Same,1},{1,8},{5,9},{Same,1},{1,0}},
        {{Same,1},{0,3},{Same,1},{3,5},{3,6},{4,0},{1,8},{5,9},{Same,1},{1,0}},
        {{Same,1},{0,3},{Same,1},{4,5},{Same,1},{Same,1},{1,8},{5,9},{Same,1},{1,0}},
        {{Same,1},{0,3},{5,0},{5,0},{5,0},{5,0},{5,0},{Same,1},{1,10},{Same,1}}
    };
    State state = LinkUp;
    bool timer = false, pending = true;
    unsigned starts = 0, preparations = 0, completions = 0, commandFailures = 0;

    void step(unsigned event) {
        assert(event < 10);
        const auto cell = table[state][event];
        if (cell.next != Same) state = static_cast<State>(cell.next);
        if (cell.action == 4) { timer = true; ++starts; }
        if (cell.action == 6) ++preparations;
        if (cell.action == 8) { timer = false; pending = false; ++completions; }
        if (cell.action == 3) { timer = false; pending = false; }
        if (cell.action == 9) timer = false;
    }

    void receive(uint32_t selector, const void *data, size_t length) {
        if (!data) return;
        uint32_t first = 0;
        if (length >= sizeof(first)) std::memcpy(&first, data, sizeof(first));
        switch (selector) {
        case 0x89: if (length == 12) step(2); break;
        case 0x8a: if (length == 216) step(3); break;
        case 0x8b: if (length == 12) step(4); break;
        case 0x49: if (length == 8 && first == 0) step(5); break;
        case 0x50: if (length == 168) step(6); break;
        case 0xcf:
            // The command-error subscriber has no FSM or timer operation.
            if (length == 4) { pending = false; ++commandFailures; }
            break;
        default: break;
        }
    }

    void seedStarted(bool prepared) {
        // Explicit reference-side precondition for terminal-only tests;
        // this must never be described as a production start publication.
        const uint32_t progress[3] = {};
        receive(0x89, progress, sizeof(progress));
        if (prepared) receive(0x8b, progress, sizeof(progress));
    }

    static void calibrate() {
        const uint32_t reassoc[2] = {};
        const uint32_t completion[42] = {};
        ReferenceRoamFsm success;
        success.seedStarted(true);
        success.receive(0x49, reassoc, sizeof(reassoc));
        assert(success.state == WaitDone && success.timer && success.pending);
        success.receive(0x50, completion, sizeof(completion));
        assert(success.state == LinkUp && !success.timer && !success.pending && success.completions == 1);
        for (bool prepared : {false, true}) {
            ReferenceRoamFsm failure;
            failure.seedStarted(prepared);
            const uint32_t error = 1;
            failure.receive(0xcf, &error, sizeof(error));
            assert(failure.timer && failure.completions == 0);
            uint32_t failed[42] = {1};
            failure.receive(0x50, failed, sizeof(failed)-1);
            assert(failure.timer && failure.completions == 0);
            failure.receive(0x50, failed, sizeof(failed));
            assert(failure.state == LinkUp && !failure.timer && failure.completions == 1);
        }
        ReferenceRoamFsm timeout;
        timeout.seedStarted(false);
        timeout.step(9);
        assert(timeout.state == LinkUp && timeout.completions == 0 && timeout.pending);
    }
};
#endif
