/* Exercise the complete production q0 sender, including storage ownership
 * and lock order; firmware/radio behavior remains a separate live gate. */
#include "include/HAL/ItlScanCommandLease.hpp"
#include "include/HAL/ItlFirmwareContextLease.hpp"
#include "include/HAL/ItlTxQueueAllocation.hpp"
#include "scan_test_byte_order.hpp"
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>

using bus_addr_t = uint64_t;
using IOInterruptState = unsigned;
struct IOSimpleLock { bool held = false; bool scan = false; bool owner = false; };
static IOSimpleLock *lockStack[3];
static unsigned locks, allocations, cursors, refs, doorbells, sleeps;
static bool fail_alloc, fail_cursor, fail_map, enter_allowed;
static int sleep_result;
static std::function<void()> map_hook, scan_unlock_hook;
static void IOSimpleLockLock(IOSimpleLock *lock)
{
    assert(lock && !lock->held && !lock->scan && locks == 0);
    lock->held = true;
    lockStack[locks] = lock;
    ++locks;
}
static void IOSimpleLockUnlock(IOSimpleLock *lock)
{
    assert(lock && lock->held && !lock->scan && locks == 1);
    lock->held = false;
    --locks;
}
static IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *lock)
{
    assert(lock && !lock->held && locks >= 1 && locks < 3);
    assert((lock->owner && locks == 1) ||
           (lock->scan && (locks == 1 || lockStack[locks - 1]->owner)));
    lock->held = true;
    lockStack[locks] = lock;
    ++locks;
    return 9;
}
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *lock, IOInterruptState irq)
{
    assert(lock && lock->held && locks >= 2 && irq == 9 && lockStack[locks - 1] == lock);
    lock->held = false;
    --locks;
    if (locks == 1 && scan_unlock_hook) {
        auto hook = scan_unlock_hook;
        scan_unlock_hook = {};
        hook();
    }
}
static void *test_alloc(size_t size, int, int)
{
    assert(locks == 0);
    if (fail_alloc)
        return nullptr;
    void *p = std::calloc(1, size);
    assert(p);
    ++allocations;
    return p;
}
static void test_free(void *p)
{
    assert(locks == 0);
    if (p) { assert(allocations); --allocations; std::free(p); }
}
struct Mbuf { alignas(16) uint8_t bytes[4096]; };
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
    static IOMbufNaturalMemoryCursor *withSpecification(size_t, int)
    {
        assert(locks == 0);
        if (fail_cursor)
            return nullptr;
        ++cursors;
        return new IOMbufNaturalMemoryCursor;
    }
    int getPhysicalSegmentsWithCoalesce(mbuf_t, IOPhysicalSegment *seg, int)
    {
        assert(locks == 0);
        if (map_hook)
            map_hook();
        seg->location = 0x12345000;
        return fail_map ? 0 : 1;
    }
    void release() { assert(locks == 0 && cursors); --cursors; delete this; }
};
struct iwx_device_cmd {
    struct {
        uint8_t cmd, group_id, qid, idx;
        uint16_t length, reserved;
        uint8_t version;
    } hdr_wide;
    uint8_t data_wide[32];
};
struct iwx_tfh_tfd {
    struct { uint16_t tb_len; uint64_t addr; } tbs[2];
    uint16_t num_tbs;
};
struct iwx_tx_data { uint32_t flags; mbuf_t m; bus_addr_t cmd_paddr; };
struct iwx_tx_ring {
    int cur, queued, qid;
    unsigned ring_count;
    iwx_device_cmd *cmd;
    iwx_tfh_tfd desc[4];
    iwx_tx_data data[4];
};
struct iwx_rx_packet { uint32_t header; };
struct iwx_cmd_async_identity {
    uint64_t slot_serial;
    uint32_t slot_epoch, code, ack_kind;
    bool valid;
};
struct iwx_host_cmd {
    ItlFirmwareContextCommand *context_command;
    ItlTxQueueAllocationCommand *queue_allocation;
    uint64_t scan_serial;
    uint32_t id;
    uint16_t len[2];
    const void *data[2];
    uint32_t flags, resp_pkt_len;
    iwx_rx_packet *resp_pkt;
    unsigned async_owner, async_ack_kind;
    uint64_t async_cookie;
    iwx_cmd_async_identity *async_identity;
};
struct Slot {
    uint64_t serial, async_cookie;
    uint32_t epoch, code, async_owner, async_ack_kind;
    bool async;
    unsigned state;
};
struct ieee80211com {
    IOSimpleLock *ic_pae_selected_bss_lock;
    ItlStateTransitionIdentity identity{11,11,12};
};
struct ItlScanCommandPolicy {
    static ItlStateTransitionIdentity identityLocked(const ieee80211com *ic) {
        assert(ic->ic_pae_selected_bss_lock->held); return ic->identity;
    }
};
struct iwx_softc {
    iwx_tx_ring txq[1];
    ieee80211com sc_ic;
    int sc_generation;
    uint32_t sc_flags;
    bool sc_cmdq_stopping, sc_cmdq_detaching;
    IOSimpleLock *sc_cmdq_lock;
    uint64_t sc_cmdq_next_serial;
    uint32_t sc_cmdq_epoch;
    Slot sc_cmdq_slots[4];
    uint8_t *sc_cmd_resp_pkt[4];
    unsigned sc_cmd_resp_len[4];
};
constexpr int IWX_SCAN_REQ_UMAC = 0xd, IWX_LONG_GROUP = 1;
constexpr int IWX_SCD_QUEUE_CFG=0x1d, IWX_DATA_PATH_GROUP=5, IWX_SCD_QUEUE_CONFIG_CMD=0x17;
constexpr int IWX_REMOVE_STA=0x19, IWX_TXPATH_FLUSH=0x1e;
constexpr int IWX_SCAN_ABORT_UMAC = 0xe;
constexpr int IWX_CMD_ASYNC = 1, IWX_CMD_WANT_RESP = 2;
constexpr int IWX_FLAG_SHUTDOWN = 0x100;
constexpr int IWX_CMD_ASYNC_OWNER_NONE = 0, IWX_CMD_ASYNC_OWNER_MFP_PAE = 1;
constexpr int IWX_CMD_ASYNC_ACK_NONE = 0, IWX_CMD_ASYNC_ACK_HEADER = 1;
constexpr int IWX_CMD_ASYNC_ACK_ADD_STA_STATUS = 2;
constexpr int IWX_DQA_CMD_QUEUE = 0, IWX_MAX_CMD_PAYLOAD_SIZE = 1024;
constexpr int IWX_TFH_NUM_TBS = 4, IWX_FIRST_TB_SIZE = 20;
constexpr int IWX_TXDATA_FLAG_CMD_IS_NARROW = 1, IWX_HBUS_TARG_WRPTR = 0;
constexpr unsigned IWX_CMD_SLOT_FREE = 0, IWX_CMD_SLOT_SUBMITTED = 1;
constexpr unsigned IWX_CMD_SLOT_COMPLETED = 2, IWX_CMD_SLOT_TIMED_OUT = 3;
constexpr unsigned IWX_CMD_SLOT_ABORTED = 4;
constexpr int M_NOWAIT = 0, M_ZERO = 0, MBUF_WAITOK = 0, PCATCH = 0;
constexpr int kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0Doorbelled = 1;
static uint32_t iwx_cmd_id(int code, int group, int version)
{ return code | group << 8 | version << 16; }
static int iwx_cmd_groupid(int code) { return (code >> 8) & 0xff; }
static int iwx_cmd_opcode(int code) { return code & 0xff; }
static int iwx_cmd_version(int code) { return (code >> 16) & 0xff; }
static int getTxQueueSize() { return 4; }
static bool iwx_cmdq_enter(iwx_softc *)
{ assert(locks == 0); if (!enter_allowed) return false; ++refs; return true; }
static void iwx_cmdq_leave(iwx_softc *) { assert(locks == 0 && refs); --refs; }
static bool iwx_cmdq_ring_valid(iwx_softc *, iwx_tx_ring *ring)
{ return ring->ring_count == 4; }
static bool sleep_locked;
static void lockTsleep() { assert(!sleep_locked && !locks); sleep_locked = true; }
static void unlockTsleep() { assert(sleep_locked && !locks); sleep_locked = false; }
static iwx_softc *observed_sc;
static int tsleep_nsec_locked(void *, int, const char *, uint64_t)
{
    assert(!locks && sleep_locked);
    ++sleeps;
    if (!sleep_result)
        observed_sc->sc_cmdq_slots[0].state = IWX_CMD_SLOT_COMPLETED;
    return sleep_result;
}
static void AirportItlwmPostPltiTraceRecord(ieee80211com *, int)
{ assert(locks == 1); }
static ItlScanCommandLease *observed_lease;
static bool expect_scan;
static ItlFirmwareContextCommand *observed_context;
static ItlTxQueueAllocationCommand *observed_queue;
static iwx_softc *context_sc;
static void test_doorbell(iwx_softc *sc, int, int)
{
    ++doorbells;
    assert(sc->txq[0].queued == 1 && sc->txq[0].cur == 1);
    assert(sc->sc_cmdq_slots[0].state == IWX_CMD_SLOT_SUBMITTED);
    const bool owner = sc->sc_ic.ic_pae_selected_bss_lock != nullptr &&
        sc->sc_ic.ic_pae_selected_bss_lock->held;
    assert(locks == ((expect_scan || observed_context || observed_queue) ? (owner ? 3U : 2U) : 1U));
    if (expect_scan) {
        assert(owner == !observed_lease->command.stopping);
        assert(observed_lease->submitted);
    }
    if (observed_context) {
        assert(owner == !observed_context->cleanup);
        assert(!observed_context->submitted);
    }
    if (observed_queue) {
        assert(owner == (observed_queue->primary && !observed_queue->retirement));
        assert(!observed_queue->submitted);
    }
}
struct ItlIwx {
    iwx_softc com{};
    ItlTxQueueAllocation txQueueAllocation{};
    ItlFirmwareContextLease primaryStationContext{};
    ItlFirmwareStationUses primaryStationUses{};
    bool txQueueAllocationCurrentLocked(const ItlTxQueueAllocationCommand &) const;
    ItlScanCommandLease scanCommand = {};
    ItlFirmwareContextLease contextOwner = {};
    IOSimpleLock scanLock = {false, true};
    IOSimpleLock *wclScanLock = &scanLock;
    bool ownerCurrent = true;
    bool firmwareContextCommandCurrentLocked(const ItlFirmwareContextCommand &command) const {
        assert(locks == (command.cleanup ? 2U : 3U) && scanLock.held);
        return scanCommand.open && !command.submitted &&
            command.receipt.generation == static_cast<uint32_t>(context_sc->sc_generation) &&
            contextOwner.commandCurrent(command.receipt.serial, command.receipt.generation) &&
            (command.cleanup || ownerCurrent);
    }
    bool scanCommandOwnerCurrentLocked(uint64_t serial, uint32_t generation) const {
        assert(locks == 3 && scanLock.held);
        return ownerCurrent && scanCommand.current(serial, generation);
    }
    int iwx_send_cmd(iwx_softc *, iwx_host_cmd *);
};
#define nitems(a) (sizeof(a) / sizeof((a)[0]))
#define KASSERT(x, text) assert(x)
#define XYLog(...) ((void)0)
#define mtod(m, type) reinterpret_cast<type>((m)->bytes)
#define SEC_TO_NSEC(x) (uint64_t(x) * 1000000000ULL)
#define IWX_WRITE(sc, reg, value) test_doorbell(sc, reg, value)
#define IWX_WIDE_ID(group, code) iwx_cmd_id(code, group, 0)
#define MIN(a, b) std::min<size_t>(a, b)
#define malloc test_alloc
#define free test_free
#include "iwx-send-cmd.inc"
#undef malloc
#undef free

struct Fixture {
    ItlIwx driver;
    iwx_softc &sc = driver.com;
    IOSimpleLock q0;
    IOSimpleLock ownerLock = {false, false, true};
    iwx_device_cmd commands[4] = {};
    uint8_t payload[1025] = {};
    iwx_host_cmd cmd = {};
    ItlFirmwareContextCommand context = {};
    ItlTxQueueAllocationCommand queue = {};
    Fixture(bool large = true, bool synchronous = false)
    {
        assert(!allocations && !cursors && !refs && !locks && !sleep_locked);
        fail_alloc = fail_cursor = fail_map = false;
        enter_allowed = true;
        sleep_result = 0;
        doorbells = sleeps = 0;
        map_hook = scan_unlock_hook = {};
        sc.sc_generation = 7;
        sc.sc_ic.ic_pae_selected_bss_lock = &ownerLock;
        sc.sc_cmdq_epoch = 3;
        sc.sc_cmdq_lock = &q0;
        sc.txq[0].cmd = commands;
        sc.txq[0].ring_count = 4;
        for (unsigned i = 0; i != 4; ++i)
            sc.txq[0].data[i].cmd_paddr = 0x4000 + i * 512;
        assert(driver.scanCommand.reopen(0, 7));
        cmd.scan_serial = driver.scanCommand.reserve(7, 91, true, false, 0);
        cmd.id = iwx_cmd_id(IWX_SCAN_REQ_UMAC, IWX_LONG_GROUP, 0);
        cmd.len[0] = large ? 128 : 8;
        cmd.data[0] = payload;
        cmd.flags = synchronous ? IWX_CMD_WANT_RESP : IWX_CMD_ASYNC;
        cmd.resp_pkt_len = 16;
        observed_sc = &sc;
        observed_lease = &driver.scanCommand;
        expect_scan = true;
        observed_context = nullptr;
        observed_queue = nullptr;
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
    void useQueue(bool primary, bool retirement, bool modern = false) {
        useContext(true);
        context.kind=ItlFirmwareContextCommand::Kind::Station;
        context.receipt.identity.station=primary?0:6;
        context.receipt.identity.attempt=sc.sc_ic.identity;
        driver.contextOwner.owner=context.receipt;
        driver.primaryStationContext=driver.contextOwner;
        driver.primaryStationContext.confirmed=true;
        assert(driver.primaryStationUses.start(context.receipt));
        if(retirement) driver.primaryStationUses.close();
        else {
            driver.primaryStationContext.stage=ItlFirmwareContextLease::Stage::Active;
            assert(driver.primaryStationUses.acquire(context.receipt,&queue.use));
        }
        queue.serial=91; queue.lifecycle=4; queue.generation=sc.sc_generation;
        queue.station=primary?0:6; queue.tid=3; queue.queue=-1;
        queue.primary=primary; queue.retirement=retirement;
        driver.txQueueAllocation.current=queue;
        driver.txQueueAllocation.lifecycle=queue.lifecycle;
        driver.txQueueAllocation.phase=ItlTxQueueAllocation::Phase::Building;
        cmd.queue_allocation=&queue;
        cmd.id=modern?IWX_WIDE_ID(IWX_DATA_PATH_GROUP,IWX_SCD_QUEUE_CONFIG_CMD):
            retirement?IWX_REMOVE_STA:IWX_SCD_QUEUE_CFG;
        if(!retirement || !primary) { cmd.context_command=nullptr; observed_context=nullptr; }
        observed_queue=&queue;
    }
    int send() { return driver.iwx_send_cmd(&sc, &cmd); }
    void rejected(int error)
    {
        assert(send() == error);
        assert(!doorbells && sc.txq[0].cur == 0);
        assert(!sc.txq[0].data[0].m && !sc.sc_cmd_resp_pkt[0]);
        assert(!allocations && !cursors && !refs && !locks);
        assert(!driver.scanCommand.submitted);
    }
    ~Fixture()
    {
        map_hook = scan_unlock_hook = {};
        for (unsigned i = 0; i != 4; ++i) {
            mbuf_freem(sc.txq[0].data[i].m);
            test_free(sc.sc_cmd_resp_pkt[i]);
        }
        test_free(cmd.resp_pkt);
        assert(!allocations && !cursors && !refs && !locks && !sleep_locked);
    }
};

int main()
{
    unsigned cases = 0;
    for(bool primary : {false,true}) for(bool retire : {false,true}) for(bool modern : {false,true}) {
        Fixture f(true,true); f.useQueue(primary,retire,modern);
        if(retire) f.driver.ownerCurrent=false;
        assert(f.send()==0 && f.queue.submitted && doorbells==1);
        assert(f.send()==EINVAL && doorbells==1); ++cases;
    }
    for(unsigned edge=0;edge<12;++edge) {
        Fixture f(true,true); f.useQueue(true,false);
        if(edge==0) ++f.queue.generation;
        if(edge==1) map_hook=[&] { ++f.sc.sc_generation; };
        if(edge==2) map_hook=[&] { ++f.driver.txQueueAllocation.lifecycle; };
        if(edge==3) map_hook=[&] { ++f.driver.txQueueAllocation.current.serial; };
        if(edge==4) map_hook=[&] { f.driver.primaryStationUses.close(); };
        if(edge==5) map_hook=[&] { ++f.sc.sc_ic.identity.joinSequence; };
        if(edge==6) map_hook=[&] { f.driver.primaryStationContext.uncertain=true; };
        if(edge==7) map_hook=[&] { f.driver.scanCommand.open=false; };
        if(edge==8) f.cmd.id=IWX_REMOVE_STA;
        if(edge==9) f.cmd.context_command=&f.context;
        if(edge==10) fail_map=true;
        if(edge==11) f.sc.sc_ic.ic_pae_selected_bss_lock=nullptr;
        f.rejected(edge==0 || edge==8 || edge==9 ? EINVAL : edge==10 ? ENOMEM : ENXIO);
        assert(!f.queue.submitted); ++cases;
    }
    { Fixture f(true,true); f.useQueue(false,true); f.cmd.id=IWX_TXPATH_FLUSH;
      assert(f.send()==0 && f.queue.submitted); ++cases; }
    { Fixture f(true,true); f.useQueue(true,false); sleep_result=ETIMEDOUT;
      assert(f.send()==ETIMEDOUT && f.queue.submitted && doorbells==1);
      assert(f.sc.sc_cmdq_slots[0].state==IWX_CMD_SLOT_TIMED_OUT && f.sc.txq[0].data[0].m); ++cases; }
    for (bool cleanup : {false,true}) for (bool large : {false,true}) {
        Fixture f(large); f.useContext(cleanup);
        if (cleanup) f.driver.ownerCurrent = false;
        assert(f.send() == 0 && doorbells == 1 && f.context.submitted);
        assert(f.send() == EALREADY && doorbells == 1); ++cases;
    }
    for (unsigned edge = 0; edge != 8; ++edge) {
        Fixture f(true,true); f.useContext();
        if (edge == 0) ++f.sc.sc_generation;
        if (edge == 1) map_hook = [&] { ++f.sc.sc_generation; };
        if (edge == 2) map_hook = [&] { f.driver.contextOwner.clear(); };
        if (edge == 3) map_hook = [&] { f.driver.ownerCurrent = false; };
        if (edge == 4) map_hook = [&] { f.driver.scanCommand.open = false; };
        if (edge == 5) f.sc.sc_ic.ic_pae_selected_bss_lock = nullptr;
        if (edge == 6) ++f.context.receipt.serial;
        if (edge == 7) f.driver.wclScanLock = nullptr;
        f.rejected(edge == 7 ? EINVAL : ENXIO);
        assert(!f.context.submitted); ++cases;
    }
    { Fixture f(true,true); f.useContext(); fail_map = true;
      f.rejected(ENOMEM); assert(!f.context.submitted); ++cases; }
    { Fixture f(true,true); f.useContext(); sleep_result = ETIMEDOUT;
      assert(f.send() == ETIMEDOUT && f.context.submitted && doorbells == 1); ++cases; }
    { Fixture f; f.useContext(); enter_allowed = false;
      f.rejected(ENXIO); assert(!f.context.submitted); ++cases; }
    for (bool large : {false, true}) {
        for (bool synchronous : {false, true}) {
            Fixture f(large, synchronous);
            assert(f.send() == 0 && doorbells == 1);
            assert(f.driver.scanCommand.submitted);
            assert(bool(f.sc.txq[0].data[0].m) == large);
            assert(sleeps == (synchronous ? 1U : 0U));
            ++cases;
        }
    }
    { Fixture f; f.cmd.scan_serial = 0; f.rejected(ENXIO); ++cases; }
    { Fixture f; f.driver.wclScanLock = nullptr; f.rejected(ENXIO); ++cases; }
    { Fixture f; f.sc.sc_ic.ic_pae_selected_bss_lock = nullptr;
      f.rejected(ENXIO); ++cases; }
    { Fixture f(true, true); map_hook = [&] { f.driver.ownerCurrent = false; };
      f.rejected(ENXIO); ++cases; }
    { Fixture f; enter_allowed = false; f.rejected(ENXIO); ++cases; }
    { Fixture f; fail_alloc = true; f.rejected(ENOMEM); ++cases; }
    { Fixture f(true, true); fail_cursor = true; f.rejected(ENOMEM); ++cases; }
    { Fixture f(true, true); fail_map = true; f.rejected(ENOMEM); ++cases; }
    { Fixture f(true, true); f.cmd.len[0] = 1025; f.rejected(EINVAL); ++cases; }
    { Fixture f(true, true); ++f.cmd.scan_serial; f.rejected(ENXIO); ++cases; }
    { Fixture f(true, true); f.sc.sc_cmdq_slots[0].state = IWX_CMD_SLOT_SUBMITTED;
      f.rejected(ENOSPC); ++cases; }
    { Fixture f(true, true); f.sc.sc_cmdq_stopping = true;
      f.rejected(ENXIO); ++cases; }
    { Fixture f(true, true); f.sc.sc_cmdq_detaching = true;
      f.rejected(ENXIO); ++cases; }
    { Fixture f(true, true); map_hook = [&] { ++f.sc.sc_generation;
          f.driver.scanCommand.invalidate(); };
      f.rejected(ENXIO); ++cases; }
    { Fixture f(true, true); map_hook = [&] { f.driver.scanCommand.invalidate(); };
      f.rejected(ENXIO); ++cases; }
    { Fixture f(true, true); sleep_result = ETIMEDOUT;
      assert(f.send() == ETIMEDOUT && doorbells == 1);
      assert(f.driver.scanCommand.submitted && f.driver.scanCommand.live());
      assert(f.sc.sc_cmdq_slots[0].state == IWX_CMD_SLOT_TIMED_OUT);
      assert(f.sc.txq[0].data[0].m && f.sc.sc_cmd_resp_pkt[0]);
      assert(!f.driver.scanCommand.rejectUnsubmitted(f.cmd.scan_serial, 7));
      ++cases; }
    { Fixture f(true, true); scan_unlock_hook = [&] {
          assert(f.driver.scanCommand.noteTerminal(7, true, 0, false)); };
      assert(f.send() == 0 && doorbells == 1);
      ItlScanCommandTerminal terminal = {};
      assert(!f.driver.scanCommand.claimTerminal(f.cmd.scan_serial, 7, &terminal));
      assert(f.driver.scanCommand.ready(f.cmd.scan_serial, 7));
      assert(f.driver.scanCommand.claimTerminal(f.cmd.scan_serial, 7, &terminal));
      assert(terminal.joinGeneration == 91); ++cases; }
    { Fixture f; f.cmd.id = 0x20; expect_scan = false;
      f.driver.scanCommand.invalidate(); f.cmd.scan_serial = UINT64_MAX;
      assert(f.send() == 0 && doorbells == 1);
      assert(!f.driver.scanCommand.submitted);
      assert(f.sc.txq[0].data[0].flags & IWX_TXDATA_FLAG_CMD_IS_NARROW); ++cases; }
    { Fixture f(false);
      assert(f.driver.scanCommand.submit(f.cmd.scan_serial, 7));
      assert(f.driver.scanCommand.beginAbort(f.cmd.scan_serial, 7));
      f.cmd.id = iwx_cmd_id(IWX_SCAN_ABORT_UMAC, IWX_LONG_GROUP, 0);
      f.driver.ownerCurrent = false; // Exact abort survives newer upper intent.
      assert(f.send() == 0 && doorbells == 1);
      assert(f.driver.scanCommand.abortSubmitted); ++cases; }
    { Fixture f(false);
      assert(f.driver.scanCommand.submit(f.cmd.scan_serial, 7));
      assert(f.driver.scanCommand.beginAbort(f.cmd.scan_serial, 7));
      assert(f.driver.scanCommand.noteTerminal(7, true, 0, false));
      f.cmd.id = iwx_cmd_id(IWX_SCAN_ABORT_UMAC, IWX_LONG_GROUP, 0);
      assert(f.send() == EALREADY && !doorbells && !allocations);
      assert(!f.driver.scanCommand.abortSubmitted); ++cases; }
    { Fixture f(false);
      assert(f.driver.scanCommand.submit(f.cmd.scan_serial, 7));
      assert(f.driver.scanCommand.ready(f.cmd.scan_serial, 7));
      assert(f.driver.scanCommand.noteTerminal(7, true, 0, false));
      ItlScanCommandTerminal terminal;
      assert(f.driver.scanCommand.claimTerminal(f.cmd.scan_serial, 7, &terminal));
      auto next = f.driver.scanCommand.reserve(7, 92, true, false, 0);
      assert(f.driver.scanCommand.submit(next, 7));
      f.cmd.id = iwx_cmd_id(IWX_SCAN_ABORT_UMAC, IWX_LONG_GROUP, 0);
      assert(f.send() == ENXIO && !doorbells && !allocations);
      assert(f.driver.scanCommand.command.serial == next &&
             !f.driver.scanCommand.command.stopping); ++cases; }
    std::printf("actual IWX q0 command sender: %u scenarios passed\n", cases);
}
