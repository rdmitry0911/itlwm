// Full production bodies are extracted by the runner. IOKit locks, taskq,
// interrupt delivery and native engine queueing are explicit host doubles.
// This is not firmware execution, controller detach or on-air qualification.
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <thread>
#include <HAL/ItlSaeAuthTransportV1.h>

#if defined(__APPLE__)
// The macOS host libc does not expose the kernel's explicit_bzero helper.
static void explicit_bzero(void *buffer, size_t size) {
    volatile unsigned char *bytes = static_cast<volatile unsigned char *>(buffer);
    while (size-- != 0) *bytes++ = 0;
}
#endif

using u_int64_t = uint64_t;
using u_int32_t = uint32_t;
using IOInterruptState = unsigned;
constexpr unsigned IWX_SAE_TX_EVENTQ_LEN = 4;
constexpr uint32_t IWX_FLAG_SHUTDOWN = 0x100;
constexpr uint32_t IWX_SAE_ENGINE_CALLBACK_CLOSED = 0x80000000;
constexpr uint32_t IWX_SAE_ENGINE_CALLBACK_COUNT_MASK = 0x7fffffff;
constexpr uint64_t IWX_SAE_ENGINE_TICKET_DIRECT_BIT = kItlSaeAuthTransportV1DriverTicketBit;
constexpr uint64_t IWX_SAE_ENGINE_TICKET_COUNTER_MASK = ~IWX_SAE_ENGINE_TICKET_DIRECT_BIT;
constexpr int IEEE80211_EVT_SAE_AUTH_TRANSPORT = 8;
constexpr int IEEE80211_JOIN_CLEANUP_SAE = 4;
constexpr int THREAD_INTERRUPTIBLE = 1;
#define KASSERT(condition, message) assert(condition)
static void panic(const char *, const char *) { std::abort(); }
static thread_local std::function<void()> sleepHook;
static void IOSleep(unsigned milliseconds) {
    assert(milliseconds == 1);
    if (sleepHook) sleepHook();
    std::this_thread::yield();
}

struct IOLock {
    std::mutex mutex;
    std::condition_variable wake;
    std::function<void()> sleeping;
    std::function<void()> contended;
};
static void IOLockLock(IOLock *lock) {
    if (lock->mutex.try_lock()) return;
    if (lock->contended) lock->contended();
    lock->mutex.lock();
}
static void IOLockUnlock(IOLock *lock) { lock->mutex.unlock(); }
static void IOLockWakeup(IOLock *lock, void *, bool) { lock->wake.notify_all(); }
static void IOLockSleep(IOLock *lock, void *, int mode) {
    assert(mode == THREAD_INTERRUPTIBLE);
    if (lock->sleeping) lock->sleeping();
    std::unique_lock<std::mutex> held(lock->mutex, std::adopt_lock);
    lock->wake.wait(held);
    held.release();
}
struct IOSimpleLock { std::mutex mutex; };
static void IOSimpleLockLock(IOSimpleLock *lock) { lock->mutex.lock(); }
static void IOSimpleLockUnlock(IOSimpleLock *lock) { lock->mutex.unlock(); }
static IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *lock) {
    IOSimpleLockLock(lock); return 1;
}
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *lock, IOInterruptState irq) {
    assert(irq == 1); IOSimpleLockUnlock(lock);
}
struct IOInterruptEventSource {
    unsigned references = 1, signals = 0;
    std::function<void()> signalHook;
    void retain() { ++references; }
    void release() { assert(references > 1); --references; }
    void interruptOccurred(int, int, int) { ++signals; if (signalHook) signalHook(); }
};
constexpr unsigned kAirportItlwmSaeTransportMailboxCapacity = 4;
struct AirportItlwmSaeTransportMailboxLifecycle {
    IOSimpleLock *admissionLock = nullptr, *payloadLock = nullptr;
    IOInterruptEventSource *source = nullptr;
    bool settingUp = false, stopping = false, tearingDown = false;
    unsigned users = 0, count = 0, tail = 0;
    struct Entry { ItlSaeAuthTransportEventV1 event; bool isReset; } entries[4] = {};
};
class AirportItlwm {
public:
    IOSimpleLock admission, payload;
    IOInterruptEventSource source;
    AirportItlwmSaeTransportMailboxLifecycle fSaeTransportMailbox;
    AirportItlwm() {
        fSaeTransportMailbox.admissionLock = &admission;
        fSaeTransportMailbox.payloadLock = &payload;
        fSaeTransportMailbox.source = &source;
    }
    static void handleSaeAuthTransportEvent(AirportItlwm *, const ItlSaeAuthTransportEventV1 *, bool);
};
#include "controller.inc"
class ItlIwx;
struct ieee80211com {
    AirportItlwm *controller = nullptr;
    void (*ic_event_handler)(ieee80211com *, int, void *) = nullptr;
    unsigned cleanupCalls = 0;
    uint64_t cleanedGeneration = 0;
    std::function<void()> cleanupHook;
};
struct task {};
struct taskq { unsigned adds = 0; std::function<void()> addHook; };
static int task_add(taskq *queue, task *) {
    ++queue->adds;
    if (queue->addHook) queue->addHook();
    return 1;
}
struct iwx_softc {
    ItlIwx *owner = nullptr;
    ieee80211com sc_ic;
    IOLock *sc_task_gate_lock = nullptr;
    bool sc_task_gate_closed = false, sc_task_gate_bootstrap_init = false;
    bool sc_task_gate_detaching = false, sc_task_callbacks_ready = true;
    uint32_t sc_task_gate_active = 0, sc_task_gate_init_refs = 0, sc_task_gate_stop_refs = 0;
    uint32_t sc_flags = 0;
    int sc_generation = 0;
    IOSimpleLock *sc_sae_tx_lock = nullptr, *sc_sae_engine_lock = nullptr;
    unsigned sc_sae_tx_event_count = 0, sc_sae_tx_event_head = 0;
    struct Entry { ItlSaeAuthTransportEventV1 event; } sc_sae_tx_eventq[4] = {};
    uint64_t sc_sae_tx_direct_cancel_through = 0, sc_sae_tx_cancel_through = 0;
    bool sc_sae_tx_active = false, sc_sae_engine_stopping = false, sc_sae_engine_detaching = false;
    uint64_t sc_sae_tx_join_failure_generation = 0, sc_sae_engine_join_failure_generation = 0;
    volatile uint32_t sc_sae_engine_callback_state = 0;
    taskq *sc_nswq = nullptr;
    task sae_tx_task;
    bool queueDirect = false;
    unsigned nativeQueues = 0, engineSchedules = 0;
    std::function<void()> engineHook, nativeQueueHook;
};
// Owner lookup, not an assertion about userspace/kernel object layout.
#define container_of(pointer, type, member) ((pointer)->owner)
class ItlIwx {
public:
    iwx_softc com;
    IOLock gate;
    IOSimpleLock txLock, engineLock;
    taskq queue;
    AirportItlwm controller;
    ItlIwx() {
        com.owner = this;
        com.sc_task_gate_lock = &gate;
        com.sc_sae_tx_lock = &txLock;
        com.sc_nswq = &queue;
        com.sc_ic.controller = &controller;
        com.sc_ic.ic_event_handler = [](ieee80211com *ic, int kind, void *data) {
            assert(kind == IEEE80211_EVT_SAE_AUTH_TRANSPORT);
            // Explicit event-router boundary; the actual helper/mailbox run.
            AirportItlwm::handleSaeAuthTransportEvent(ic->controller,
                static_cast<const ItlSaeAuthTransportEventV1 *>(data), false);
        };
    }
    bool iwx_task_gate_close(iwx_softc *, bool, int *);
    bool iwx_task_gate_enter(iwx_softc *, bool);
    void iwx_task_gate_leave(iwx_softc *);
    void iwx_task_gate_drain(iwx_softc *, uint32_t, uint32_t, uint32_t);
    void iwx_add_task(iwx_softc *, taskq *, task *);
    static void iwx_sae_tx_task_dispatch(void *);
};
static bool iwx_sae_engine_callback_enter(iwx_softc *);
static void iwx_sae_engine_callback_leave(iwx_softc *);
static void iwx_sae_engine_callback_close(iwx_softc *);
static bool iwx_sae_engine_callback_open(iwx_softc *);
static void iwx_sae_engine_callback_drain(iwx_softc *);
static void iwx_sae_engine_wake_join_retirement(iwx_softc *);
static void iwx_sae_tx_finish_join_retirement(iwx_softc *);
static bool iwx_sae_engine_queue_terminal(iwx_softc *sc, const ItlSaeAuthTransportEventV1 *) {
    ++sc->nativeQueues;
    if (sc->nativeQueueHook) sc->nativeQueueHook();
    return sc->queueDirect;
}
static void iwx_sae_engine_schedule_task(iwx_softc *sc) {
    ++sc->engineSchedules;
    if (sc->engineHook) sc->engineHook();
}
static void ieee80211_wcl_join_cleanup_done(ieee80211com *ic, uint64_t generation, int kind) {
    assert(kind == IEEE80211_JOIN_CLEANUP_SAE);
    ++ic->cleanupCalls; ic->cleanedGeneration = generation;
    if (ic->cleanupHook) ic->cleanupHook();
}
#include "worker.inc"

static ItlSaeAuthTransportEventV1 event(uint64_t ticket = 1) {
    ItlSaeAuthTransportEventV1 value = {};
    value.version = 1; value.size = sizeof(value);
    value.kind = kItlSaeAuthTransportEventTxComplete;
    value.association_epoch = 13; value.relay_generation = 17;
    value.ticket = ticket; value.phase = kItlSaeAuthTransportPhaseCommit;
    value.wire_transaction = kItlSaeAuthTransportStaWireTransactionCommit;
    value.bssid[0] = 2; value.sta[0] = 4;
    assert(itl_sae_auth_transport_event_is_well_formed(&value));
    return value;
}
static void enqueue(ItlIwx &driver, ItlSaeAuthTransportEventV1 value) {
    auto &sc = driver.com;
    sc.sc_sae_tx_eventq[(sc.sc_sae_tx_event_head + sc.sc_sae_tx_event_count) % 4].event = value;
    ++sc.sc_sae_tx_event_count;
}
static void routing_and_retirement() {
    for (unsigned mode = 0; mode != 7; ++mode) {
        ItlIwx driver;
        auto value = event(mode == 1 || mode == 2 ? IWX_SAE_ENGINE_TICKET_DIRECT_BIT | 1 : 1);
        if (mode == 2) driver.com.sc_sae_engine_callback_state = IWX_SAE_ENGINE_CALLBACK_CLOSED;
        if (mode == 3) driver.com.sc_sae_tx_cancel_through = 1;
        if (mode == 4) value.size = 0;
        if (mode == 5) driver.com.sc_ic.ic_event_handler = nullptr;
        if (mode == 6) driver.com.queueDirect = true;
        enqueue(driver, value);
        driver.com.sc_sae_tx_join_failure_generation = 31;
        ItlIwx::iwx_sae_tx_task_dispatch(&driver.com);
        assert(driver.com.sc_task_gate_active == 0 && driver.com.sc_sae_tx_event_count == 0);
        assert((driver.com.sc_sae_engine_callback_state & IWX_SAE_ENGINE_CALLBACK_COUNT_MASK) == 0);
        assert(driver.controller.source.signals == (mode == 0 ? 1U : 0U));
        assert(driver.com.sc_ic.cleanupCalls == 1 && driver.com.sc_ic.cleanedGeneration == 31);
        assert(driver.com.sc_sae_tx_join_failure_generation == 0);
        if (mode == 0) {
            const auto &copy = driver.controller.fSaeTransportMailbox.entries[0].event;
            assert(std::memcmp(&copy, &value, sizeof(value)) == 0);
            assert(!driver.controller.fSaeTransportMailbox.entries[0].isReset);
        }
        const auto &cleared = driver.com.sc_sae_tx_eventq[0].event;
        const ItlSaeAuthTransportEventV1 zero = {};
        assert(std::memcmp(&cleared, &zero, sizeof(zero)) == 0);
    }
    ItlIwx closed;
    enqueue(closed, event());
    int generation = 0;
    assert(closed.iwx_task_gate_close(&closed.com, true, &generation));
    ItlIwx::iwx_sae_tx_task_dispatch(&closed.com);
    assert(closed.com.sc_sae_tx_event_count == 1 && closed.controller.source.signals == 0);
    ItlIwx more;
    enqueue(more, event(1)); enqueue(more, event(2));
    ItlIwx::iwx_sae_tx_task_dispatch(&more.com);
    assert(more.queue.adds == 1 && more.com.sc_sae_tx_event_count == 1);
}
static void stop_during_tail(unsigned boundary) {
    ItlIwx driver;
    enqueue(driver, event());
    std::mutex coordination;
    std::condition_variable changed;
    bool paused = false, release = false, drainSleeping = false, gateBlocked = false, reclaimed = false;
    auto pause = [&] {
        std::unique_lock<std::mutex> lock(coordination);
        paused = true; changed.notify_all();
        assert(changed.wait_for(lock, std::chrono::seconds(5), [&] { return release; }));
    };
    if (boundary == 0) driver.controller.source.signalHook = pause;
    if (boundary == 1) {
        driver.com.sc_sae_tx_join_failure_generation = 31;
        driver.com.sc_ic.cleanupHook = pause;
    }
    if (boundary == 2) {
        driver.com.sc_sae_engine_lock = &driver.engineLock;
        driver.com.sc_sae_engine_join_failure_generation = 31;
        driver.com.engineHook = pause;
    }
    if (boundary == 3) {
        enqueue(driver, event(2));
        driver.queue.addHook = pause;
    }
    driver.gate.sleeping = [&] {
        std::lock_guard<std::mutex> lock(coordination);
        drainSleeping = true; changed.notify_all();
    };
    driver.gate.contended = [&] {
        std::lock_guard<std::mutex> lock(coordination);
        gateBlocked = true; changed.notify_all();
    };
    std::thread worker([&] { ItlIwx::iwx_sae_tx_task_dispatch(&driver.com); });
    {
        std::unique_lock<std::mutex> lock(coordination);
        assert(changed.wait_for(lock, std::chrono::seconds(5), [&] { return paused; }));
    }
    std::thread stop([&] {
        int generation = 0;
        assert(driver.iwx_task_gate_close(&driver.com, true, &generation));
        driver.iwx_task_gate_drain(&driver.com, 0, 0, 0);
        std::lock_guard<std::mutex> lock(coordination);
        reclaimed = true; changed.notify_all();
    });
    bool retained;
    {
        std::unique_lock<std::mutex> lock(coordination);
        assert(changed.wait_for(lock, std::chrono::seconds(5), [&] { return drainSleeping || gateBlocked || reclaimed; }));
        retained = (boundary == 3 ? gateBlocked : drainSleeping) && !reclaimed;
        release = true; changed.notify_all();
    }
    worker.join(); stop.join();
    assert(retained && "stop reclaimed the owner before terminal tail returned");
    assert(reclaimed && driver.com.sc_task_gate_active == 0);
    assert(driver.controller.source.references == 1 && driver.controller.fSaeTransportMailbox.users == 0);
    assert(driver.com.sc_task_gate_closed && driver.com.sc_task_gate_detaching);
}
static void native_callback_close_drain_reopen() {
    ItlIwx driver;
    enqueue(driver, event(IWX_SAE_ENGINE_TICKET_DIRECT_BIT | 1));
    std::mutex coordination;
    std::condition_variable changed;
    bool paused = false, release = false, draining = false, reclaimed = false;
    driver.com.nativeQueueHook = [&] {
        std::unique_lock<std::mutex> lock(coordination);
        paused = true; changed.notify_all();
        assert(changed.wait_for(lock, std::chrono::seconds(5), [&] { return release; }));
    };
    std::thread worker([&] { ItlIwx::iwx_sae_tx_task_dispatch(&driver.com); });
    {
        std::unique_lock<std::mutex> lock(coordination);
        assert(changed.wait_for(lock, std::chrono::seconds(5), [&] { return paused; }));
    }
    std::thread stop([&] {
        iwx_sae_engine_callback_close(&driver.com);
        sleepHook = [&] {
            std::lock_guard<std::mutex> lock(coordination);
            draining = true; changed.notify_all();
        };
        iwx_sae_engine_callback_drain(&driver.com);
        std::lock_guard<std::mutex> lock(coordination);
        reclaimed = true; changed.notify_all();
    });
    {
        std::unique_lock<std::mutex> lock(coordination);
        assert(changed.wait_for(lock, std::chrono::seconds(5), [&] { return draining || reclaimed; }));
        assert(draining && !reclaimed);
        assert(!iwx_sae_engine_callback_enter(&driver.com));
        assert(!iwx_sae_engine_callback_open(&driver.com));
        release = true; changed.notify_all();
    }
    worker.join(); stop.join();
    assert(driver.com.sc_sae_engine_callback_state == IWX_SAE_ENGINE_CALLBACK_CLOSED);
    assert(driver.controller.source.signals == 0 && driver.com.nativeQueues == 1);
    assert(iwx_sae_engine_callback_open(&driver.com));
    assert(iwx_sae_engine_callback_enter(&driver.com));
    iwx_sae_engine_callback_leave(&driver.com);
    assert(driver.com.sc_sae_engine_callback_state == 0);
}
int main() {
    // Run the race first so the historical complete dispatcher fails at its
    // early-leave behavior, not merely at the later-added retirement helper.
    for (unsigned round = 0; round != 32; ++round) {
        for (unsigned boundary = 0; boundary != 4; ++boundary) stop_during_tail(boundary);
        native_callback_close_drain_reopen();
    }
    routing_and_retirement();
    std::puts("IWX SAE terminal lifetime: PASS (160 controlled races, actual dispatcher/lease/mailbox/CAS admission, stop during callback/retirement/wake/enqueue, native close/drain/reopen, private tickets, cancellation, malformed FIFO)");
}
