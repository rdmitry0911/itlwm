#include <cassert>
#include <cstdint>
#include <cstdio>
#include <functional>
static unsigned allocationStep, failStep, liveLocks, liveSources, lockDepth;
static unsigned signals, cancellations, arms;
static uint32_t lastMs;
static std::function<void()> removing;
using IOInterruptState = unsigned;
constexpr int kIOReturnSuccess = 0;
struct IOSimpleLock { bool held = false; };
static IOSimpleLock *IOSimpleLockAlloc() {
    if (++allocationStep == failStep) return nullptr;
    ++liveLocks; return new IOSimpleLock;
}
static void IOSimpleLockFree(IOSimpleLock *lock) { assert(!lock->held); --liveLocks; delete lock; }
static IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *lock) {
    assert(lock && !lock->held && !lockDepth); lock->held = true; ++lockDepth; return 0;
}
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *lock, IOInterruptState) {
    assert(lock && lock->held && lockDepth == 1); lock->held = false; --lockDepth;
}
struct OSObject { virtual ~OSObject() = default; };
struct EventSource : OSObject {
    unsigned references = 1;
    bool added = false;
    EventSource() { ++liveSources; }
    ~EventSource() { assert(!added && !references); --liveSources; }
    void retain() { ++references; }
    void release() { if (!--references) delete this; }
    void enable() { assert(!lockDepth); }
    void disable() { assert(!lockDepth); }
};
struct IOInterruptEventSource : EventSource {
    using Action = void (*)(OSObject *, IOInterruptEventSource *, int);
    static IOInterruptEventSource *interruptEventSource(OSObject *, Action) {
        if (++allocationStep == failStep) return nullptr;
        return new IOInterruptEventSource;
    }
    void interruptOccurred(void *, void *, int) { assert(!lockDepth && references == 2); ++signals; }
};
struct IOTimerEventSource : EventSource {
    using Action = void (*)(OSObject *, IOTimerEventSource *);
    static IOTimerEventSource *timerEventSource(OSObject *, Action) {
        if (++allocationStep == failStep) return nullptr;
        return new IOTimerEventSource;
    }
    int setTimeoutMS(uint32_t ms) { assert(!lockDepth && references == 2); ++arms; lastMs = ms; return 0; }
    void cancelTimeout() { assert(!lockDepth && !added); ++cancellations; }
};
struct IOWorkLoop {
    bool gated = true;
    bool inGate() { return gated; }
    int addEventSource(EventSource *source) {
        assert(!lockDepth);
        if (++allocationStep == failStep) return -1;
        source->added = true; return 0;
    }
    void removeEventSource(EventSource *source) {
        assert(!lockDepth && source->added);
        source->added = false;
        auto fn = std::move(removing); removing = {};
        if (fn) fn();
    }
};
static void absolutetime_to_nanoseconds(uint64_t ticks, uint64_t *ns) { *ns = ticks; }
#include "timer.inc"
static void event(OSObject *, IOInterruptEventSource *, int) {}
static void timeout(OSObject *, IOTimerEventSource *) {}
int main() {
    OSObject owner;
    IOWorkLoop loop;
    unsigned cases = 0;
    for (failStep = 1; failStep <= 5; ++failStep) {
        allocationStep = 0;
        ItlSaePeerTimer timer;
        assert(!timer.init(&owner, &loop, event, timeout));
        timer.shutdown();
        timer.signal();
        timer.arm(2000000, 0);
        assert(!liveLocks && !liveSources && !lockDepth);
        ++cases;
    }
    failStep = 0;
    allocationStep = 0;
    ItlSaePeerTimer timer;
    assert(timer.init(&owner, &loop, event, timeout));
    assert(!timer.init(&owner, &loop, event, timeout));
    timer.signal();
    assert(signals == 1);
    const uint64_t intervals[] = {1, 999999, 1000000, 1000001, 2000000000,
        UINT64_C(1000000) * UINT32_MAX + 1};
    const uint32_t expected[] = {1, 1, 1, 2, 2000, UINT32_MAX};
    for (unsigned i = 0; i < sizeof(intervals) / sizeof(intervals[0]); ++i) {
        timer.arm(intervals[i] + 99, 99);
        assert(lastMs == expected[i]); ++cases;
    }
    unsigned before = arms;
    timer.arm(0, 0);
    timer.arm(99, 99);
    timer.arm(98, 99);
    loop.gated = false;
    timer.arm(100, 99);
    assert(arms == before);
    loop.gated = true;
    removing = [&] { timer.arm(2000000000, 0); timer.signal(); };
    timer.shutdown();
    assert(arms == before && signals == 1 && !liveLocks && !liveSources);
    timer.shutdown();
    timer.signal();
    assert(!lockDepth);
    ++cases;
    std::printf("PASS: %u actual timer allocation/arm/teardown scenarios\n", cases);
}
