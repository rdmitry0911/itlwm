/* Compile the complete production iwm_send_cmd against deterministic DMA,
 * firmware and allocation boundaries. This is not a radio qualification. */
#include "include/HAL/ItlScanCommandLease.hpp"
#include "include/HAL/ItlFirmwareContextLease.hpp"
#include "scan_test_byte_order.hpp"
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "itlwm/hal_iwm/IwmCommandSlot.hpp"
#ifndef IWM_COMMAND_SLOT_HISTORICAL
#define IWM_COMMAND_SLOT_HISTORICAL 0
#endif
constexpr unsigned command_leaf_depth=IWM_COMMAND_SLOT_HISTORICAL ? 0 : 1;

using bus_addr_t = uint64_t;
using IOInterruptState = unsigned;
struct IOSimpleLock { bool held = false; unsigned rank = 2; std::mutex mutex; };
static thread_local IOSimpleLock *lockStack[3];
static thread_local unsigned lock_depth;
static unsigned allocations, doorbells, nic_locks, nic_unlocks;
static bool fail_allocation, fail_mapping, fail_nic, fail_cursor;
static unsigned cursor_live;
static int sleep_result;
static thread_local bool wait_mutex_held;
static std::mutex actual_wait_mutex, scheduling_mutex;
static std::condition_variable actual_wait_cv, scheduling_cv;
static bool threaded_wait, wait_registered, wait_released;
static bool pause_wait_return, wake_observed, release_wait_return, sender_finished;
static bool firmware_irq_double;
static unsigned sleep_calls, wake_calls;
static std::function<void()> map_hook, unlock_hook;
static std::function<void()> doorbell_hook, sleep_hook;
static std::function<void()> pending_irq, gate_sleep_hook, stop_wait_hook;
static std::function<void()> allocation_hook;
static uint64_t test_now;
static unsigned gate_sleeps, gate_wakes;
using AbsoluteTime = uint64_t;
[[maybe_unused]] constexpr unsigned kSecondScale=1000000000;
constexpr unsigned kMillisecondScale=1000000;
constexpr unsigned THREAD_UNINT=0;
static void clock_interval_to_deadline(unsigned interval, unsigned scale, uint64_t *deadline)
{ *deadline=test_now+uint64_t(interval)*scale; }
[[maybe_unused]] static void clock_get_uptime(uint64_t *now) { *now=test_now; }
[[maybe_unused]] static void absolutetime_to_nanoseconds(uint64_t value, uint64_t *ns) { *ns=value; }
struct IOLock { bool held=false; std::mutex mutex; };
static void IOLockLock(IOLock *lock) {
    assert(lock && !lock_depth); lock->mutex.lock(); assert(!lock->held); lock->held=true;
}
static void IOLockUnlock(IOLock *lock) {
    assert(lock->held && !lock_depth); lock->held=false; lock->mutex.unlock();
}
static IOSimpleLock *IOSimpleLockAlloc() { assert(!lock_depth); return new IOSimpleLock{false,0,{}}; }
static void IOSimpleLockFree(IOSimpleLock *lock) { assert(!lock_depth && !lock->held); delete lock; }
static void IOSleep(unsigned) { assert(!lock_depth && !wait_mutex_held); assert(stop_wait_hook); stop_wait_hook(); }
struct WorkLoop {
    unsigned depth=0;
    bool inGate() const { return depth!=0; }
protected:
    int sleepGate(void *, AbsoluteTime deadline, unsigned) {
        assert(depth && !lock_depth && !wait_mutex_held);
        const unsigned saved=depth; depth=0;
        ++gate_sleeps;
        if (gate_sleep_hook) gate_sleep_hook();
        test_now=deadline;
        depth=saved;
        return 0;
    }
    void wakeupGate(void *, bool) { assert(!lock_depth); ++gate_wakes; }
    friend struct CommandGate;
};
struct CommandGate {
    WorkLoop *loop;
    int commandSleep(void *event, AbsoluteTime deadline, unsigned flags)
    { return loop->sleepGate(event,deadline,flags); }
    void commandWakeup(void *event, bool one) { loop->wakeupGate(event,one); }
};

static IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *lock)
{
    assert(lock && lock_depth < 3);
    assert(!lock_depth || lockStack[lock_depth - 1]->rank < lock->rank);
    lock->mutex.lock();
    assert(!lock->held);
    lockStack[lock_depth] = lock;
    lock->held = true;
    ++lock_depth;
    return 11;
}
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *lock,
                                             IOInterruptState irq)
{
    assert(irq == 11 && lock->held && lock_depth && lockStack[lock_depth - 1] == lock);
    lock->held = false;
    --lock_depth;
    lock->mutex.unlock();
    if (!lock_depth && doorbells && unlock_hook) {
        auto hook = unlock_hook;
        unlock_hook = {};
        hook();
    }
}
static void *test_alloc(size_t size, int, int)
{
    assert(lock_depth == 0);
    if (fail_allocation)
        return nullptr;
    void *p = std::calloc(1, size);
    assert(p);
    ++allocations;
    if (allocation_hook) { auto hook=allocation_hook; allocation_hook={}; hook(); }
    return p;
}
static void test_free(void *p)
{
    assert(lock_depth == 0);
    if (p) {
        assert(allocations > 0);
        --allocations;
        std::free(p);
    }
}
struct Mbuf { alignas(16) unsigned char bytes[4096]; };
using mbuf_t = Mbuf *;
static void mbuf_allocpacket(int, size_t size, unsigned *, mbuf_t *out)
{
    assert(size <= sizeof(Mbuf));
    *out = static_cast<Mbuf *>(test_alloc(sizeof(Mbuf), 0, 0));
}
static void mbuf_freem(mbuf_t m) { test_free(m); }
static void mbuf_setlen(mbuf_t, size_t) {}
static void mbuf_pkthdr_setlen(mbuf_t, size_t) {}
struct IOPhysicalSegment { uint64_t location; };
struct IOMbufNaturalMemoryCursor {
    static IOMbufNaturalMemoryCursor *withSpecification(size_t, unsigned)
    {
        assert(!lock_depth);
        if (fail_cursor) return nullptr;
        ++cursor_live; return new IOMbufNaturalMemoryCursor;
    }
    void release() { assert(!lock_depth && cursor_live); --cursor_live; delete this; }
    unsigned getPhysicalSegmentsWithCoalesce(mbuf_t, IOPhysicalSegment *seg, int)
    {
        assert(lock_depth == 0);
        if (map_hook) { auto hook=map_hook; hook(); }
        seg->location = 0x12345000;
        return fail_mapping ? 0 : 1;
    }
};
using Cursor = IOMbufNaturalMemoryCursor;
struct DmaMap { unsigned dm_nsegs; Cursor *cursor; };
struct iwm_device_cmd {
    union {
        struct { uint8_t code, flags, qid, idx; } hdr;
        struct {
            uint8_t opcode, group_id, qid, idx;
            uint16_t length;
            uint8_t version, reserved;
        } hdr_wide;
    };
    union { uint8_t data[32]; uint8_t data_wide[32]; };
};
struct iwm_tfd {
    struct { uint32_t lo; uint16_t hi_n_len; } tbs[1];
    uint8_t num_tbs;
};
struct iwm_tx_data { DmaMap *map; mbuf_t m; bus_addr_t cmd_paddr; };
struct iwm_tx_ring {
    int cur, queued, qid;
    iwm_device_cmd commandStorage[4];
    iwm_device_cmd *cmd = commandStorage;
    iwm_tfd descriptorStorage[4];
    iwm_tfd *desc = descriptorStorage;
    iwm_tx_data data[4];
};
struct iwm_rx_packet { struct { uint8_t flags; } hdr; uint8_t data[15]; };
struct iwm_host_cmd {
    ItlFirmwareContextCommand *context_command;
    uint64_t scan_serial;
    uint32_t id;
    uint16_t len[2];
    const void *data[2];
    uint32_t flags;
    unsigned resp_pkt_len;
    iwm_rx_packet *resp_pkt;
};
struct iwm_softc {
    struct { IOSimpleLock *ic_pae_selected_bss_lock; struct { unsigned if_flags; } ic_if; } sc_ic;
    iwm_tx_ring txq[10];
    int cmdqid, sc_generation, sc_device_family;
    uint32_t sc_flags;
    unsigned sc_scan_abort_pending;
    uint8_t *sc_cmd_resp_pkt[4];
    unsigned sc_cmd_resp_len[4];
    IOSimpleLock *sc_cmdq_lock;
    uint64_t sc_cmdq_next_serial;
    uint32_t sc_cmdq_epoch, sc_cmdq_senders, sc_cmdq_stoppers;
    int sc_cmdq_generation;
    bool sc_cmdq_stopping, sc_cmdq_detaching;
    iwm_cmd_slot sc_cmdq_slots[4];
    IOLock *sc_sae_tx_lifecycle_lock;
    bool sc_sae_tx_detaching;
    unsigned sc_radio_init_refs, sc_radio_stop_refs;
};
constexpr int IWM_SCAN_OFFLOAD_REQUEST_CMD = 0x51;
constexpr int IWM_SCAN_REQ_UMAC = 0xd, IWM_LONG_GROUP = 1;
constexpr int IWM_SCAN_ABORT_UMAC = 0xe, IWM_SCAN_OFFLOAD_ABORT_CMD = 0x52;
constexpr int IWM_CMD_ASYNC = 1, IWM_CMD_WANT_RESP = 2;
constexpr int IWM_FLAG_SHUTDOWN = 1, IWM_DEVICE_FAMILY_7000 = 7000;
constexpr int IWM_CMD_RESP_MAX = 64, IWM_MAX_CMD_PAYLOAD_SIZE = 1024;
constexpr int IWM_TX_RING_COUNT = 4, IWM_HBUS_TARG_WRPTR = 0;
constexpr int M_DEVBUF = 0, M_NOWAIT = 0, M_ZERO = 0, MBUF_WAITOK = 0;
constexpr int PCATCH = 0;
constexpr unsigned IFF_UP=1, IFF_RUNNING=2, IWM_CMD_FAILED_MSK=0x40;
static uint32_t iwm_cmd_id(int code, int group, int version)
{ return code | group << 8 | version << 16; }
static int iwm_cmd_groupid(int code) { return (code >> 8) & 0xff; }
static int iwm_cmd_opcode(int code) { return code & 0xff; }
static int iwm_cmd_version(int code) { return (code >> 16) & 0xff; }
static uint16_t iwm_get_dma_hi_addr(uint64_t paddr) { return paddr >> 32; }
static int splnet() { return 0; }
static void splx(int) { assert(lock_depth == 0); }
[[maybe_unused]] static int tsleep_nsec(void *, int, const char *, uint64_t)
{ assert(lock_depth == 0 && !wait_mutex_held); ++sleep_calls; return sleep_result; }
static void lockTsleep() {
    assert(!wait_mutex_held && lock_depth==0);
    actual_wait_mutex.lock(); wait_mutex_held=true;
}
static void unlockTsleep() {
    assert(wait_mutex_held); wait_mutex_held=false;
    actual_wait_mutex.unlock();
    if (pending_irq) {
        auto hook=pending_irq; pending_irq={}; hook();
    }
}
[[maybe_unused]] static int tsleep_nsec_locked(void *, int, const char *, uint64_t remaining)
{
    assert(lock_depth==0 && wait_mutex_held);
    ++sleep_calls;
    if (threaded_wait) {
        std::unique_lock<std::mutex> wait(actual_wait_mutex,std::adopt_lock);
        wait_mutex_held=false;
        { std::lock_guard<std::mutex> stage(scheduling_mutex);
          wait_registered=true; scheduling_cv.notify_all(); }
        const bool woken=actual_wait_cv.wait_for(wait,std::chrono::seconds(5),
            [] { return wait_released; });
        if (pause_wait_return) {
            std::unique_lock<std::mutex> stage(scheduling_mutex);
            wake_observed=true; scheduling_cv.notify_all();
            assert(scheduling_cv.wait_for(stage,std::chrono::seconds(5),
                [] { return release_wait_return; }));
        }
        wait.release(); wait_mutex_held=true;
        return woken ? 0 : ETIMEDOUT;
    }
    if (sleep_hook) {
        /* Explicit msleep double: registration releases the wait mutex,
         * cancellation can acquire it, and return reacquires it. */
        unlockTsleep();
        sleep_hook();
        lockTsleep();
    }
    if (sleep_result!=0 || !sleep_hook) test_now+=remaining;
    return sleep_result;
}
static void wakeupOn(void *) {
    assert(wait_mutex_held || firmware_irq_double);
    ++wake_calls;
    if (threaded_wait) { wait_released=true; actual_wait_cv.notify_all(); }
}
static void iwm_update_sched(iwm_softc *, int, int, int, int) {}
static bool iwm_nic_lock(iwm_softc *)
{ assert(lock_depth == 0); ++nic_locks; return !fail_nic; }
static void iwm_nic_unlock(iwm_softc *)
{ assert(lock_depth == 0); ++nic_unlocks; }
static ItlScanCommandLease *observed_lease;
static bool expect_scan;
static ItlFirmwareContextCommand *observed_context;
static iwm_softc *context_sc;
static void test_doorbell(iwm_softc *sc, int, int)
{
    ++doorbells;
    assert(sc->txq[sc->cmdqid].queued > 0 &&
           sc->txq[sc->cmdqid].queued <= IWM_TX_RING_COUNT);
    if (expect_scan) {
        const bool owner = sc->sc_ic.ic_pae_selected_bss_lock->held;
        assert(owner == !observed_lease->command.stopping);
        assert(lock_depth == (owner ? 2U : 1U)+command_leaf_depth && observed_lease->submitted);
    }
    else if (observed_context) {
        assert(lock_depth == (observed_context->cleanup ? 1U : 2U)+command_leaf_depth);
        assert(!observed_context->submitted);
    } else
        assert(lock_depth == command_leaf_depth);
    if (doorbell_hook) {
        /* An actual IRQ cannot execute inside publication's IRQ-disabled
         * command leaf and wait mutex. Deliver it at the unlocked boundary. */
        if (IWM_COMMAND_SLOT_HISTORICAL) doorbell_hook();
        else { assert(!pending_irq); pending_irq=doorbell_hook; }
    }
}
struct ItlIwm {
    ItlScanCommandLease scanCommand = {};
    ItlFirmwareContextLease contextOwner = {};
    IOSimpleLock scanLock;
    IOSimpleLock *wclScanLock = &scanLock;
    bool ownerCurrent = true;
    WorkLoop workLoop;
    CommandGate commandGate{&workLoop};
    WorkLoop *getMainWorkLoop() { return &workLoop; }
    CommandGate *getMainCommandGate() { return &commandGate; }
    bool firmwareContextCommandCurrentLocked(const ItlFirmwareContextCommand &command) const {
        assert(lock_depth == (command.cleanup ? 1U : 2U)+command_leaf_depth && scanLock.held);
        return scanCommand.open && !command.submitted &&
            command.receipt.generation == static_cast<uint32_t>(context_sc->sc_generation) &&
            contextOwner.commandCurrent(command.receipt.serial, command.receipt.generation) &&
            (command.cleanup || ownerCurrent);
    }
    bool scanCommandOwnerCurrentLocked(uint64_t serial, uint32_t generation) const {
        assert(lock_depth == 2+command_leaf_depth && scanLock.held);
        return ownerCurrent && scanCommand.current(serial, generation);
    }
    int iwm_send_cmd(iwm_softc *, iwm_host_cmd *);
    void iwm_cmd_done(iwm_softc *, int, int, int);
    void iwm_radio_abort_command_waits(iwm_softc *);
    int iwm_cmdq_init(iwm_softc *);
    bool iwm_cmdq_select(iwm_softc *,int,int);
    bool iwm_cmdq_start(iwm_softc *,int);
    bool iwm_cmdq_enter(iwm_softc *);
    void iwm_cmdq_leave(iwm_softc *);
    void iwm_cmdq_stop(iwm_softc *);
    void iwm_cmdq_detach_begin(iwm_softc *);
    void iwm_cmdq_destroy(iwm_softc *);
    void iwm_cmdq_store_response(iwm_softc *,int,int,int,const iwm_rx_packet *,size_t);
};
#define nitems(a) (sizeof(a) / sizeof((a)[0]))
#define _KASSERT(x) assert(x)
#define KASSERT(x, text) assert(x)
template<typename... Args> static void test_log(Args...) {}
#define DEVNAME(sc) "iwm-sender-test"
#define XYLog(...) test_log(__VA_ARGS__)
#define mtod(m, type) reinterpret_cast<type>((m)->bytes)
#define SEC_TO_NSEC(x) (uint64_t(x) * 1000000000ULL)
#define IWM_WRITE(sc, reg, value) test_doorbell(sc, reg, value)
#define IWM_WIDE_ID(group, code) iwm_cmd_id(code, group, 0)
#define malloc test_alloc
#define free test_free
#include "iwm-send-cmd.inc"
#undef malloc
#undef free

struct Fixture {
    ItlIwm driver;
    iwm_softc sc = {};
    IOSimpleLock ownerLock = {false, 1, {}};
    IOLock lifecycleLock;
    Cursor cursor;
    DmaMap maps[4] = {};
    uint8_t payload[1025] = {};
    iwm_host_cmd cmd = {};
    ItlFirmwareContextCommand context = {};
    Fixture(bool umac = true, bool large = true, bool response = false)
    {
        assert(allocations == 0 && lock_depth == 0 && cursor_live==0);
        fail_allocation = fail_mapping = fail_nic = fail_cursor = false;
        sleep_result = 0;
        wait_mutex_held=false; firmware_irq_double=false; sleep_calls=wake_calls=0;
        doorbells = nic_locks = nic_unlocks = 0;
        map_hook = unlock_hook = {};
        doorbell_hook = sleep_hook = {};
        pending_irq = gate_sleep_hook = stop_wait_hook = {};
        allocation_hook={};
        test_now=0; gate_sleeps=gate_wakes=0;
        threaded_wait=wait_registered=wait_released=false;
        pause_wait_return=wake_observed=release_wait_return=sender_finished=false;
        sc.sc_generation = 7;
        sc.sc_ic.ic_pae_selected_bss_lock = &ownerLock;
        sc.sc_device_family = IWM_DEVICE_FAMILY_7000;
        sc.sc_sae_tx_lifecycle_lock=&lifecycleLock;
        for (unsigned q=0;q<10;q++) sc.txq[q].qid=q;
        for (unsigned i = 0; i != 4; ++i) {
            maps[i].cursor = &cursor;
            sc.txq[0].data[i].map = &maps[i];
            sc.txq[0].data[i].cmd_paddr = 0x4000 + i * 512;
        }
        assert(driver.scanCommand.reopen(0, 7));
        assert(driver.iwm_cmdq_init(&sc)==0);
        assert(driver.iwm_cmdq_select(&sc,0,sc.sc_generation));
        assert(driver.iwm_cmdq_start(&sc,sc.sc_generation));
        cmd.scan_serial = driver.scanCommand.reserve(7, 91, umac, false, 0);
        cmd.id = umac ? iwm_cmd_id(IWM_SCAN_REQ_UMAC, IWM_LONG_GROUP, 0) :
            IWM_SCAN_OFFLOAD_REQUEST_CMD;
        cmd.len[0] = large ? 128 : 8;
        cmd.data[0] = payload;
        cmd.flags = response ? IWM_CMD_WANT_RESP : IWM_CMD_ASYNC;
        cmd.resp_pkt_len = 16;
        observed_lease = &driver.scanCommand;
        expect_scan = true;
        observed_context = nullptr;
        context_sc = &sc;
    }
    void useContext(bool cleanup = false) {
        assert(driver.scanCommand.rejectUnsubmitted(cmd.scan_serial, sc.sc_generation));
        cmd.scan_serial = 0;
        cmd.id = 0x28;
        cmd.context_command = &context;
        context.receipt.serial = 23;
        context.receipt.generation = sc.sc_generation;
        context.kind = ItlFirmwareContextCommand::Kind::Mac;
        context.cleanup = cleanup;
        driver.contextOwner.owner = context.receipt;
        driver.contextOwner.stage = cleanup ? ItlFirmwareContextLease::Stage::Removing :
            ItlFirmwareContextLease::Stage::Adding;
        expect_scan = false;
        observed_context = &context;
    }
    int send() { return driver.iwm_send_cmd(&sc, &cmd); }
    void ordinary(bool response=false) {
        cmd.id=0x20; cmd.scan_serial=0;
        cmd.flags=response ? IWM_CMD_WANT_RESP : 0;
        expect_scan=false;
    }
    void rejected(int error)
    {
        assert(send() == error);
        assert(doorbells == 0 && sc.txq[0].queued == 0 && sc.txq[0].cur == 0);
        assert(sc.txq[0].data[0].m == nullptr);
        assert(allocations == 0 && !driver.scanCommand.submitted);
    }
    ~Fixture()
    {
        map_hook = unlock_hook = {};
        doorbell_hook = sleep_hook = {};
        pending_irq = gate_sleep_hook = stop_wait_hook = {};
        allocation_hook={};
        driver.iwm_cmdq_stop(&sc);
        for (unsigned q=0;q<10;q++) for (unsigned i = 0; i != 4; ++i)
            mbuf_freem(sc.txq[q].data[i].m);
        for (unsigned i = 0; i != 4; ++i) {
            test_free(sc.sc_cmd_resp_pkt[i]);
        }
        test_free(cmd.resp_pkt);
        driver.iwm_cmdq_destroy(&sc);
        assert(allocations == 0 && lock_depth == 0 && cursor_live==0);
    }
};

int main(int argc, char **argv)
{
    if (argc==2 && std::strcmp(argv[1],"command-slot-threaded")==0) {
        unsigned cases=0;
        for (bool stopped : {false,true}) for (bool large : {false,true})
            for (bool response : {false,true}) for (bool gated : {false,true}) {
                if (!stopped && gated) continue;
                Fixture f(true,large,response); f.ordinary(response);
                threaded_wait=true; pause_wait_return=stopped;
                int result=-1;
                std::thread sender([&] {
                    result=f.send();
                    std::lock_guard<std::mutex> stage(scheduling_mutex);
                    sender_finished=true; scheduling_cv.notify_all();
                });
                { std::unique_lock<std::mutex> stage(scheduling_mutex);
                  assert(scheduling_cv.wait_for(stage,std::chrono::seconds(5),
                    [] { return wait_registered; })); }
                if (stopped) {
                    f.driver.workLoop.depth=gated ? 2 : 0;
                    auto releaseSender=[&] {
                        std::unique_lock<std::mutex> stage(scheduling_mutex);
                        assert(scheduling_cv.wait_for(stage,std::chrono::seconds(5),
                            [] { return wake_observed; }));
                        // Full sender is paused after the actual stop wake.
                        // Queue storage must remain owned until it really exits.
                        assert(f.sc.sc_cmdq_stopping && f.sc.sc_cmdq_senders==1);
                        assert(f.sc.sc_cmdq_slots[0].state==IWM_CMD_SLOT_ABORTED);
                        assert(bool(f.sc.sc_cmd_resp_pkt[0])==response);
                        assert(bool(f.sc.txq[0].data[0].m)==large);
                        release_wait_return=true; scheduling_cv.notify_all();
                        assert(scheduling_cv.wait_for(stage,std::chrono::seconds(5),
                            [] { return sender_finished; }));
                    };
                    stop_wait_hook=releaseSender; gate_sleep_hook=releaseSender;
                    f.driver.iwm_cmdq_stop(&f.sc);
                } else {
                    iwm_rx_packet packet={}; packet.data[0]=0x43;
                    if (response)
                        f.driver.iwm_cmdq_store_response(&f.sc,0,0,0x20,&packet,sizeof(packet));
                    f.driver.iwm_cmd_done(&f.sc,0,0,0x20);
                }
                sender.join();
                assert(result==(stopped ? ENXIO : 0) && sleep_calls==1);
                assert(f.sc.sc_cmdq_senders==0 && f.sc.sc_cmdq_stoppers==0);
                assert(!f.sc.sc_cmd_resp_pkt[0] && bool(f.cmd.resp_pkt)==(!stopped && response));
                if (stopped) {
                    assert(bool(f.sc.txq[0].data[0].m)==large);
                    assert(f.driver.workLoop.depth==(gated ? 2U : 0U));
                } else {
                    assert(!f.sc.txq[0].data[0].m && f.sc.txq[0].queued==0);
                    if (response) assert(f.cmd.resp_pkt->data[0]==0x43);
                }
                threaded_wait=false;
                ++cases;
            }
        for (bool large : {false,true}) for (bool gated : {false,true}) {
            Fixture f(true,large,true); f.ordinary(true);
            auto preparePause=[&] {
                std::unique_lock<std::mutex> stage(scheduling_mutex);
                wait_registered=true; scheduling_cv.notify_all();
                assert(scheduling_cv.wait_for(stage,std::chrono::seconds(5),
                    [] { return release_wait_return; }));
            };
            if (large) map_hook=preparePause; else allocation_hook=preparePause;
            int result=-1;
            std::thread sender([&] {
                result=f.send();
                std::lock_guard<std::mutex> stage(scheduling_mutex);
                sender_finished=true; scheduling_cv.notify_all();
            });
            { std::unique_lock<std::mutex> stage(scheduling_mutex);
              assert(scheduling_cv.wait_for(stage,std::chrono::seconds(5),
                [] { return wait_registered; })); }
            f.driver.workLoop.depth=gated ? 2 : 0;
            auto releasePrepare=[&] {
                std::unique_lock<std::mutex> stage(scheduling_mutex);
                assert(f.sc.sc_cmdq_stopping && f.sc.sc_cmdq_senders==1 &&
                    !f.sc.sc_cmd_resp_pkt[0] && !f.sc.txq[0].data[0].m);
                assert(allocations!=0 && doorbells==0);
                release_wait_return=true; scheduling_cv.notify_all();
                assert(scheduling_cv.wait_for(stage,std::chrono::seconds(5),
                    [] { return sender_finished; }));
            };
            stop_wait_hook=releasePrepare; gate_sleep_hook=releasePrepare;
            f.driver.iwm_cmdq_stop(&f.sc);
            sender.join();
            assert(result==ENXIO && doorbells==0 && allocations==0 && cursor_live==0);
            assert(f.sc.sc_cmdq_senders==0 && f.driver.workLoop.depth==(gated ? 2U : 0U));
            ++cases;
        }
        std::printf("IWM complete sender/ACK/stop with real thread waits: %u scenarios passed\n",cases);
        return 0;
    }
    if (argc==2 && std::strcmp(argv[1],"command-slot-matrix")==0) {
        unsigned cases=0;
        for (bool large : {false,true}) for (bool response : {false,true}) {
            Fixture f(true,large,response); f.ordinary(response);
            iwm_rx_packet packet={}; packet.data[0]=0x7b;
            sleep_hook=[&] {
                if (response)
                    f.driver.iwm_cmdq_store_response(&f.sc,0,0,0x20,&packet,sizeof(packet));
                f.driver.iwm_cmd_done(&f.sc,0,0,0x20);
            };
            assert(f.send()==0 && sleep_calls==1 && wake_calls==1);
            assert(f.sc.sc_cmdq_slots[0].state==IWM_CMD_SLOT_FREE);
            assert(f.sc.sc_cmdq_senders==0 && f.sc.txq[0].queued==0);
            assert(!f.sc.txq[0].data[0].m && !f.sc.sc_cmd_resp_pkt[0]);
            assert(bool(f.cmd.resp_pkt)==response);
            if (response) assert(f.cmd.resp_pkt->data[0]==0x7b);
            const unsigned wakes=wake_calls;
            f.driver.iwm_cmd_done(&f.sc,0,0,0x20);
            assert(wake_calls==wakes && f.sc.txq[0].queued==0);
            ++cases;
        }
        { Fixture f(true,false); f.ordinary();
          sleep_hook=[&] {
              if (sleep_calls==1) {
                  // A wake by itself is not an ACK or a success predicate.
                  lockTsleep(); wakeupOn(&f.sc.txq[0].desc[0]); unlockTsleep();
                  test_now+=1000000;
              } else f.driver.iwm_cmd_done(&f.sc,0,0,0x20);
          };
          assert(f.send()==0 && sleep_calls==2 && wake_calls==2); ++cases; }
        { Fixture f(true,true,true); f.ordinary(true);
          sleep_hook=[&] {
              if (sleep_calls==1) {
                  for (int idx : {-1,4}) f.driver.iwm_cmd_done(&f.sc,0,idx,0x20);
                  f.driver.iwm_cmd_done(&f.sc,1,0,0x20);
                  f.driver.iwm_cmd_done(&f.sc,0,0,0x21);
                  assert(wake_calls==0 && f.sc.txq[0].queued==1 && f.sc.txq[0].data[0].m);
                  assert(f.sc.sc_cmdq_slots[0].state==IWM_CMD_SLOT_SUBMITTED);
                  test_now+=1000000;
              } else f.driver.iwm_cmd_done(&f.sc,0,0,0x20);
          };
          assert(f.send()==0 && sleep_calls==2 && wake_calls==1); ++cases; }
        for (unsigned malformed=0;malformed<3;malformed++) {
            Fixture f(true,false,true); f.ordinary(true);
            iwm_rx_packet packet={};
            if (malformed==0) packet.hdr.flags=IWM_CMD_FAILED_MSK;
            const size_t size=malformed==1 ? sizeof(packet)-1 :
                malformed==2 ? sizeof(packet)+1 : sizeof(packet);
            sleep_hook=[&] {
                f.driver.iwm_cmdq_store_response(&f.sc,0,0,0x20,&packet,size);
                assert(!f.sc.sc_cmd_resp_pkt[0]);
                f.driver.iwm_cmd_done(&f.sc,0,0,0x20);
            };
            // Header ACK still retires the command; the caller's actual status
            // parser rejects NULL. No successful status payload is invented.
            assert(f.send()==0 && !f.cmd.resp_pkt && wake_calls==1); ++cases;
        }
        { Fixture f(true,false,true); f.ordinary(true); iwm_rx_packet packet={};
          packet.data[0]=0x6d;
          sleep_hook=[&] {
              const auto owned=f.sc.sc_cmd_resp_pkt[0];
              f.driver.iwm_cmdq_store_response(&f.sc,1,0,0x20,&packet,sizeof(packet));
              f.driver.iwm_cmdq_store_response(&f.sc,0,-1,0x20,&packet,sizeof(packet));
              f.driver.iwm_cmdq_store_response(&f.sc,0,4,0x20,&packet,sizeof(packet));
              f.driver.iwm_cmdq_store_response(&f.sc,0,0,0x21,&packet,sizeof(packet));
              assert(f.sc.sc_cmd_resp_pkt[0]==owned && owned[1]==0);
              f.driver.iwm_cmdq_store_response(&f.sc,0,0,0x20,&packet,sizeof(packet));
              f.driver.iwm_cmd_done(&f.sc,0,0,0x20);
          };
          assert(f.send()==0 && f.cmd.resp_pkt->data[0]==0x6d); ++cases; }
        for (bool large : {false,true}) {
            Fixture f(true,large,true); f.ordinary(true); sleep_result=ETIMEDOUT;
            assert(f.send()==ETIMEDOUT && sleep_calls==1);
            const uint64_t serial=f.sc.sc_cmdq_slots[0].serial;
            const auto response=f.sc.sc_cmd_resp_pkt[0];
            const auto dma=f.sc.txq[0].data[0].m;
            assert(response && bool(dma)==large && !f.cmd.resp_pkt);
            assert(f.sc.sc_cmdq_slots[0].state==IWM_CMD_SLOT_TIMED_OUT);
            f.cmd.flags=IWM_CMD_ASYNC; f.cmd.len[0]=8;
            for (unsigned n=0;n<3;n++) { f.cmd.id=0x21+n; assert(f.send()==0); }
            assert(f.sc.txq[0].cur==0 && f.sc.txq[0].queued==4);
            f.cmd.id=0x55;
            assert(f.send()==ENOSPC && doorbells==4 && f.sc.txq[0].cur==0);
            assert(f.sc.sc_cmd_resp_pkt[0]==response && f.sc.txq[0].data[0].m==dma);
            f.driver.iwm_cmd_done(&f.sc,0,0,0x20);
            assert(!f.sc.sc_cmd_resp_pkt[0] && !f.sc.txq[0].data[0].m);
            assert(f.sc.sc_cmdq_slots[0].state==IWM_CMD_SLOT_FREE && f.sc.txq[0].queued==3);
            assert(f.send()==0 && f.sc.sc_cmdq_slots[0].serial!=serial);
            const unsigned wakes=wake_calls;
            f.driver.iwm_cmd_done(&f.sc,0,0,0x20);
            assert(wake_calls==wakes && f.sc.txq[0].queued==4);
            f.driver.iwm_cmd_done(&f.sc,0,0,0x55);
            assert(f.sc.txq[0].queued==3);
            ++cases;
        }
        { Fixture f(true,true,true); f.ordinary(true); sleep_result=ETIMEDOUT;
          assert(f.send()==ETIMEDOUT);
          iwm_rx_packet packet={};
          f.driver.iwm_cmdq_store_response(&f.sc,0,0,0x20,&packet,sizeof(packet));
          assert(!f.sc.sc_cmd_resp_pkt[0] && f.sc.txq[0].data[0].m);
          f.driver.iwm_cmd_done(&f.sc,0,0,0x20);
          assert(!f.sc.txq[0].data[0].m && f.sc.txq[0].queued==0); ++cases; }
        { Fixture f(true,false); f.ordinary();
          doorbell_hook=[&] {
              f.driver.iwm_cmd_done(&f.sc,0,0,0x20);
              assert(f.sc.txq[0].queued==0 && f.sc.sc_cmdq_slots[0].state==IWM_CMD_SLOT_COMPLETED);
              doorbell_hook={};
              iwm_host_cmd other=f.cmd; other.flags=IWM_CMD_ASYNC; other.id=0x21;
              for (unsigned n=0;n<3;n++) assert(f.driver.iwm_send_cmd(&f.sc,&other)==0);
              assert(f.sc.txq[0].cur==0 && f.sc.txq[0].queued==3);
              assert(f.driver.iwm_send_cmd(&f.sc,&other)==ENOSPC);
          };
          assert(f.send()==0 && sleep_calls==0 && f.sc.txq[0].queued==3);
          assert(f.sc.sc_cmdq_slots[0].state==IWM_CMD_SLOT_FREE); ++cases; }
        { Fixture f(true,true,true); f.ordinary(true);
          map_hook=[&] {
              map_hook={};
              assert(f.sc.sc_cmdq_senders==1 && !f.sc.sc_cmd_resp_pkt[0]);
              iwm_host_cmd other=f.cmd; other.flags=IWM_CMD_ASYNC; other.len[0]=8; other.id=0x21;
              assert(f.driver.iwm_send_cmd(&f.sc,&other)==0);
              assert(f.sc.sc_cmdq_slots[0].code==0x21 && !f.sc.txq[0].data[0].m);
          };
          sleep_hook=[&] { f.driver.iwm_cmd_done(&f.sc,0,1,0x20); };
          assert(f.send()==0 && f.sc.sc_cmdq_slots[0].code==0x21 &&
              f.sc.sc_cmdq_slots[1].code==0x20 && f.sc.txq[0].queued==1);
          assert(f.sc.sc_cmdq_senders==0); ++cases; }
        for (bool drop_wake : {false,true}) {
            Fixture f(true,true); f.ordinary(); f.driver.workLoop.depth=2;
            unsigned steps=0;
            gate_sleep_hook=[&] {
                assert(f.driver.workLoop.depth==0 && !wait_mutex_held);
                if (++steps==(drop_wake ? 2U : 1U))
                    f.driver.iwm_cmd_done(&f.sc,0,0,0x20);
            };
            assert(f.send()==0 && sleep_calls==0 && gate_sleeps==steps &&
                steps==(drop_wake ? 2U : 1U));
            assert(f.driver.workLoop.depth==2 && f.sc.sc_cmdq_senders==0);
            ++cases;
        }
        { Fixture f(true,true,true); f.ordinary(true); f.driver.workLoop.depth=2;
          assert(f.send()==ETIMEDOUT && gate_sleeps==200 && sleep_calls==0);
          assert(f.sc.sc_cmdq_slots[0].state==IWM_CMD_SLOT_TIMED_OUT &&
              f.sc.sc_cmd_resp_pkt[0] && f.sc.txq[0].data[0].m);
          assert(f.driver.workLoop.depth==2); ++cases; }
        { Fixture f(true,false); f.ordinary(); f.driver.workLoop.depth=2;
          gate_sleep_hook=[&] {
              ++f.sc.sc_generation; f.sc.sc_flags|=IWM_FLAG_SHUTDOWN;
              f.driver.iwm_radio_abort_command_waits(&f.sc);
          };
          assert(f.send()==ENXIO && gate_sleeps==1 && sleep_calls==0);
          assert(f.driver.workLoop.depth==2); ++cases; }
        for (bool gated : {false,true}) {
            Fixture f(true,false); f.ordinary(); f.driver.workLoop.depth=gated ? 2 : 0;
            assert(f.driver.iwm_cmdq_enter(&f.sc));
            f.sc.sc_cmd_resp_pkt[0]=static_cast<uint8_t *>(test_alloc(16,0,0));
            auto retire=[&] {
                assert(f.sc.sc_cmdq_stopping && f.sc.sc_cmdq_stoppers==1);
                assert(f.sc.sc_cmdq_senders==1 && f.sc.sc_cmd_resp_pkt[0]);
                assert(!f.driver.iwm_cmdq_start(&f.sc,7));
                f.driver.iwm_cmdq_leave(&f.sc);
            };
            stop_wait_hook=retire; gate_sleep_hook=retire;
            f.driver.iwm_cmdq_stop(&f.sc);
            assert(!f.sc.sc_cmd_resp_pkt[0] && !f.sc.sc_cmdq_senders && !f.sc.sc_cmdq_stoppers);
            assert(f.driver.workLoop.depth==(gated ? 2U : 0U));
            assert(f.driver.iwm_cmdq_select(&f.sc,9,7));
            assert(f.driver.iwm_cmdq_start(&f.sc,7));
            f.cmd.flags=IWM_CMD_ASYNC;
            assert(f.send()==0 && f.sc.txq[9].queued==1);
            f.driver.iwm_cmd_done(&f.sc,9,0,0x20);
            assert(f.sc.txq[9].queued==0 && f.sc.sc_cmdq_slots[0].state==IWM_CMD_SLOT_FREE);
            ++cases;
        }
        for (unsigned edge=0;edge<8;edge++) {
            Fixture f(true,false); f.ordinary(); f.driver.iwm_cmdq_stop(&f.sc);
            if (edge==0) f.sc.sc_flags|=IWM_FLAG_SHUTDOWN;
            if (edge==1) f.sc.sc_sae_tx_detaching=true;
            if (edge==2) f.sc.sc_cmdq_detaching=true;
            if (edge==3) f.sc.sc_radio_stop_refs=1;
            if (edge==4) f.sc.sc_ic.ic_if.if_flags=IFF_UP;
            if (edge==5) ++f.sc.sc_generation;
            if (edge==6) f.sc.txq[0].desc=nullptr;
            if (edge==7) f.sc.txq[0].cmd=nullptr;
            assert(!f.driver.iwm_cmdq_start(&f.sc,7));
            assert(f.send()==ENXIO && !doorbells && !f.sc.sc_cmdq_senders);
            ++cases;
        }
        { Fixture f(true,false); f.ordinary(); f.driver.iwm_cmdq_stop(&f.sc);
          f.sc.sc_ic.ic_if.if_flags=IFF_UP; f.sc.sc_radio_init_refs=1;
          assert(f.driver.iwm_cmdq_start(&f.sc,7));
          f.driver.iwm_cmdq_detach_begin(&f.sc);
          assert(!f.driver.iwm_cmdq_start(&f.sc,7) && f.send()==ENXIO); ++cases; }
        { Fixture f(true,true,true); f.ordinary(true); fail_cursor=true;
          f.rejected(ENOMEM); assert(cursor_live==0 && f.sc.sc_cmdq_senders==0); ++cases; }
        std::printf("IWM actual command-slot ownership: %u scenarios passed\n",cases);
        return 0;
    }
    if (argc==2 && (std::strcmp(argv[1], "ack-before-wait")==0 ||
        std::strcmp(argv[1], "ack-before-wait-dma")==0)) {
        const bool large=std::strcmp(argv[1], "ack-before-wait-dma")==0;
        Fixture f(true,large,false);
        f.cmd.id=0x20; f.cmd.scan_serial=0; f.cmd.flags=0; expect_scan=false;
        sleep_result=ETIMEDOUT;
        doorbell_hook=[&] {
            // Actual complete ACK body executes before the sender registers
            // its wait. Only IRQ delivery/timing is an explicit double.
            firmware_irq_double=true;
            f.driver.iwm_cmd_done(&f.sc,f.sc.cmdqid,0,f.cmd.id);
            firmware_irq_double=false;
        };
        const int result=f.send();
        std::fprintf(stderr,
            "IWM actual command ACK before wait payload=%s result=%d "
            "sleeps=%u wakes=%u queued=%d dmaLive=%u\n",
            large ? "DMA" : "inline", result, sleep_calls, wake_calls,
            f.sc.txq[0].queued, f.sc.txq[0].data[0].m!=nullptr);
        assert(result==0 && sleep_calls==0 && wake_calls==1);
        assert(f.sc.txq[0].queued==0 && !f.sc.txq[0].data[0].m);
        assert(nic_locks==1 && nic_unlocks==1 && !wait_mutex_held);
        std::puts("IWM complete sender and ACK before wait: PASS");
        return 0;
    }
    if (argc==2 && std::strcmp(argv[1], "stop-before-wait")==0) {
        Fixture f(true,false,true);
        f.cmd.id=0x20; f.cmd.scan_serial=0; expect_scan=false;
        sleep_result=ETIMEDOUT;
        doorbell_hook=[&] {
            ++f.sc.sc_generation;
            f.sc.sc_flags|=IWM_FLAG_SHUTDOWN;
            const auto response=f.sc.sc_cmd_resp_pkt[0];
            f.driver.iwm_radio_abort_command_waits(&f.sc);
            assert(f.sc.sc_cmd_resp_pkt[0]==response && response);
        };
        const int result=f.send();
        std::fprintf(stderr, "IWM published command cancelled before wait result=%d sleeps=%u\n",
            result, sleep_calls);
        assert(result==ENXIO && sleep_calls==0);
        assert(wake_calls==IWM_TX_RING_COUNT+1 && !wait_mutex_held);
        assert(f.sc.sc_cmd_resp_pkt[0] && !f.cmd.resp_pkt);
        std::puts("IWM complete command cancellation prewait: PASS");
        return 0;
    }
    if (argc==2 && std::strcmp(argv[1], "stop-during-wait")==0) {
        for (bool large : {false,true}) {
            Fixture f(true,large,true);
            f.cmd.id=0x20; f.cmd.scan_serial=0; expect_scan=false;
            sleep_hook=[&] {
                const auto response=f.sc.sc_cmd_resp_pkt[0];
                const auto dma=f.sc.txq[0].data[0].m;
                ++f.sc.sc_generation;
                f.sc.sc_flags|=IWM_FLAG_SHUTDOWN;
                f.driver.iwm_radio_abort_command_waits(&f.sc);
                assert(f.sc.sc_cmd_resp_pkt[0]==response && response);
                assert(f.sc.txq[0].data[0].m==dma);
            };
            assert(f.send()==ENXIO && sleep_calls==1);
            assert(wake_calls==IWM_TX_RING_COUNT+1 && !wait_mutex_held);
            assert(f.sc.sc_cmd_resp_pkt[0] && !f.cmd.resp_pkt);
            assert(bool(f.sc.txq[0].data[0].m)==large);
        }
        std::puts("IWM complete command cancellation during wait: PASS");
        return 0;
    }
    if (argc==2 && std::strcmp(argv[1], "abort-partial-ring")==0) {
        Fixture f;
        f.sc.cmdqid=-1;
        f.driver.iwm_radio_abort_command_waits(&f.sc);
        f.sc.cmdqid=10;
        f.driver.iwm_radio_abort_command_waits(&f.sc);
        f.sc.cmdqid=0; f.sc.txq[0].desc=nullptr;
        f.driver.iwm_radio_abort_command_waits(&f.sc);
        assert(wake_calls==0 && !wait_mutex_held);
        std::puts("IWM command abort partial ring: PASS");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "dma-failure") == 0) {
        Fixture f(true, true, true);
        fail_mapping = true;
        f.rejected(ENOMEM);
        std::puts("actual IWM DMA failure: nonzero result and no retained allocation");
        return 0;
    }
    if (argc!=1) { std::fprintf(stderr,"unknown test case: %s\n",argv[1]); return 2; }
    unsigned cases = 0;
    for (bool cleanup : {false,true}) for (bool large : {false,true}) {
        Fixture f(true,large); f.useContext(cleanup);
        if (cleanup) f.driver.ownerCurrent = false;
        assert(f.send() == 0 && doorbells == 1 && f.context.submitted);
        assert(f.send() == EALREADY && doorbells == 1); ++cases;
    }
    for (unsigned edge = 0; edge != 8; ++edge) {
        Fixture f(true,true,true); f.useContext();
        if (edge == 0) ++f.sc.sc_generation;
        if (edge == 1) map_hook = [&] {
            /* Generation changes during preparation. The new sender keeps
             * its response local; historical senders publish it early. */
            test_free(f.sc.sc_cmd_resp_pkt[0]); f.sc.sc_cmd_resp_pkt[0] = nullptr;
            f.sc.sc_cmd_resp_len[0] = 0; ++f.sc.sc_generation;
        };
        if (edge == 2) map_hook = [&] { f.driver.contextOwner.clear(); };
        if (edge == 3) map_hook = [&] { f.driver.ownerCurrent = false; };
        if (edge == 4) map_hook = [&] { f.driver.scanCommand.open = false; };
        if (edge == 5) f.sc.sc_ic.ic_pae_selected_bss_lock = nullptr;
        if (edge == 6) ++f.context.receipt.serial;
        if (edge == 7) f.driver.wclScanLock = nullptr;
        f.rejected(edge == 7 ? EINVAL : ENXIO);
        assert(!f.context.submitted); ++cases;
    }
    { Fixture f(true,true,true); f.useContext(); fail_mapping = true;
      f.rejected(ENOMEM); assert(!f.context.submitted); ++cases; }
    { Fixture f(true,true,true); f.useContext(); sleep_result = ETIMEDOUT;
      assert(f.send() == ETIMEDOUT && f.context.submitted && doorbells == 1); ++cases; }
    { Fixture f; f.useContext(); f.cmd.id = IWM_SCAN_OFFLOAD_REQUEST_CMD;
      f.rejected(ENXIO); assert(!f.context.submitted); ++cases; }
    for (bool umac : {false, true}) {
        for (bool large : {false, true}) {
            Fixture f(umac, large);
            assert(f.send() == 0 && doorbells == 1);
            assert(f.driver.scanCommand.submitted);
            assert(bool(f.sc.txq[0].data[0].m) == large);
            assert(nic_locks == 1 && nic_unlocks == 0);
            ++cases;
        }
    }
    { Fixture f; f.cmd.scan_serial = 0; f.rejected(ENXIO); ++cases; }
    { Fixture f; f.driver.wclScanLock = nullptr; f.rejected(ENXIO); ++cases; }
    { Fixture f; f.sc.sc_ic.ic_pae_selected_bss_lock = nullptr;
      f.rejected(ENXIO); ++cases; }
    { Fixture f(true, true, true); map_hook = [&] { f.driver.ownerCurrent = false; };
      f.rejected(ENXIO); assert(nic_locks == nic_unlocks); ++cases; }
    { Fixture f(true, true, true); fail_mapping = true;
      f.rejected(ENOMEM); ++cases; }
    { Fixture f; fail_allocation = true; f.rejected(ENOMEM); ++cases; }
    { Fixture f(true, true, true); f.cmd.len[0] = 1025;
      f.rejected(EINVAL); ++cases; }
    { Fixture f(true, true, true); fail_nic = true;
      f.rejected(EBUSY); assert(nic_unlocks == 0); ++cases; }
    { Fixture f; map_hook = [&] { ++f.sc.sc_generation;
                                 f.driver.scanCommand.invalidate(); };
      f.rejected(ENXIO); assert(nic_locks == 0); ++cases; }
    { Fixture f(true, true, true);
      map_hook = [&] { f.driver.scanCommand.invalidate(); };
      f.rejected(ENXIO); assert(nic_locks == 1 && nic_unlocks == 1); ++cases; }
    { Fixture f(true, true, true); ++f.cmd.scan_serial;
      f.rejected(ENXIO); assert(nic_locks == nic_unlocks); ++cases; }
    { Fixture f(true, true, true); sleep_result = ETIMEDOUT;
      assert(f.send() == ETIMEDOUT && doorbells == 1);
      assert(f.driver.scanCommand.submitted && f.driver.scanCommand.live());
      assert(f.sc.txq[0].data[0].m && f.sc.sc_cmd_resp_pkt[0]);
      assert(f.sc.sc_cmdq_slots[0].state==IWM_CMD_SLOT_TIMED_OUT);
      assert(!f.driver.scanCommand.rejectUnsubmitted(f.cmd.scan_serial, 7));
      ++cases; }
    { Fixture f(true, true, true);
      unlock_hook = [&] {
          assert(f.driver.scanCommand.noteTerminal(7, true, 0, false));
      };
      sleep_hook = [&] { f.driver.iwm_cmd_done(&f.sc,0,0,f.cmd.id & 0xffff); };
      assert(f.send() == 0 && doorbells == 1);
      ItlScanCommandTerminal terminal = {};
      assert(!f.driver.scanCommand.claimTerminal(f.cmd.scan_serial, 7, &terminal));
      assert(f.driver.scanCommand.ready(f.cmd.scan_serial, 7));
      assert(f.driver.scanCommand.claimTerminal(f.cmd.scan_serial, 7, &terminal));
      assert(terminal.joinGeneration == 91);
      ++cases; }
    { Fixture f; f.cmd.id = 0x20; expect_scan = false;
      f.driver.scanCommand.invalidate(); f.cmd.scan_serial = UINT64_MAX;
      assert(f.send() == 0 && doorbells == 1);
      assert(!f.driver.scanCommand.submitted); ++cases; }
    for (bool umac : {false, true}) {
      Fixture f(umac, false);
      assert(f.driver.scanCommand.submit(f.cmd.scan_serial, 7));
      assert(f.driver.scanCommand.beginAbort(f.cmd.scan_serial, 7));
      f.cmd.id = umac ? iwm_cmd_id(IWM_SCAN_ABORT_UMAC, IWM_LONG_GROUP, 0) :
          IWM_SCAN_OFFLOAD_ABORT_CMD;
      f.driver.ownerCurrent = false; // A successor cannot prevent retiring this owner.
      assert(f.send() == 0 && doorbells == 1);
      assert(f.driver.scanCommand.abortSubmitted); ++cases;
    }
    { Fixture f(true, false);
      assert(f.driver.scanCommand.submit(f.cmd.scan_serial, 7));
      assert(f.driver.scanCommand.beginAbort(f.cmd.scan_serial, 7));
      assert(f.driver.scanCommand.noteTerminal(7, true, 0, false));
      f.cmd.id = iwm_cmd_id(IWM_SCAN_ABORT_UMAC, IWM_LONG_GROUP, 0);
      assert(f.send() == EALREADY && !doorbells && !allocations);
      assert(!f.driver.scanCommand.abortSubmitted); ++cases; }
    { Fixture f(true, false);
      assert(f.driver.scanCommand.submit(f.cmd.scan_serial, 7));
      assert(f.driver.scanCommand.ready(f.cmd.scan_serial, 7));
      assert(f.driver.scanCommand.noteTerminal(7, true, 0, false));
      ItlScanCommandTerminal terminal;
      assert(f.driver.scanCommand.claimTerminal(f.cmd.scan_serial, 7, &terminal));
      auto next = f.driver.scanCommand.reserve(7, 92, true, false, 0);
      assert(f.driver.scanCommand.submit(next, 7));
      f.cmd.id = iwm_cmd_id(IWM_SCAN_ABORT_UMAC, IWM_LONG_GROUP, 0);
      assert(f.send() == ENXIO && !doorbells && !allocations);
      assert(f.driver.scanCommand.command.serial == next &&
             !f.driver.scanCommand.command.stopping); ++cases; }
    std::printf("actual IWM command sender: %u scenarios passed\n", cases);
}
