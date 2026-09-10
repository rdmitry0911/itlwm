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

using bus_addr_t = uint64_t;
using IOInterruptState = unsigned;
struct IOSimpleLock { bool held = false; unsigned rank = 2; };
static IOSimpleLock *lockStack[2];
static unsigned lock_depth, allocations, doorbells, nic_locks, nic_unlocks;
static bool fail_allocation, fail_mapping, fail_nic;
static int sleep_result;
static std::function<void()> map_hook, unlock_hook;

static IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *lock)
{
    assert(lock && !lock->held && lock_depth < 2);
    assert(!lock_depth || lockStack[lock_depth - 1]->rank < lock->rank);
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
    if (!lock_depth && unlock_hook) {
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
struct Cursor {
    unsigned getPhysicalSegmentsWithCoalesce(mbuf_t, IOPhysicalSegment *seg, int)
    {
        assert(lock_depth == 0);
        if (map_hook)
            map_hook();
        seg->location = 0x12345000;
        return fail_mapping ? 0 : 1;
    }
};
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
    iwm_device_cmd cmd[4];
    iwm_tfd desc[4];
    iwm_tx_data data[4];
};
struct iwm_rx_packet { uint32_t header; };
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
    struct { IOSimpleLock *ic_pae_selected_bss_lock; } sc_ic;
    iwm_tx_ring txq[1];
    int cmdqid, sc_generation, sc_device_family;
    uint32_t sc_flags;
    uint8_t *sc_cmd_resp_pkt[4];
    unsigned sc_cmd_resp_len[4];
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
static uint32_t iwm_cmd_id(int code, int group, int version)
{ return code | group << 8 | version << 16; }
static int iwm_cmd_groupid(int code) { return (code >> 8) & 0xff; }
static int iwm_cmd_opcode(int code) { return code & 0xff; }
static int iwm_cmd_version(int code) { return (code >> 16) & 0xff; }
static uint16_t iwm_get_dma_hi_addr(uint64_t paddr) { return paddr >> 32; }
static int splnet() { return 0; }
static void splx(int) { assert(lock_depth == 0); }
static int tsleep_nsec(void *, int, const char *, uint64_t)
{ assert(lock_depth == 0); return sleep_result; }
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
    assert(sc->txq[0].queued == 1 && sc->txq[0].cur == 1);
    if (expect_scan) {
        const bool owner = sc->sc_ic.ic_pae_selected_bss_lock->held;
        assert(owner == !observed_lease->command.stopping);
        assert(lock_depth == (owner ? 2U : 1U) && observed_lease->submitted);
    }
    else if (observed_context) {
        assert(lock_depth == (observed_context->cleanup ? 1U : 2U));
        assert(!observed_context->submitted);
    } else
        assert(lock_depth == 0);
}
struct ItlIwm {
    ItlScanCommandLease scanCommand = {};
    ItlFirmwareContextLease contextOwner = {};
    IOSimpleLock scanLock;
    IOSimpleLock *wclScanLock = &scanLock;
    bool ownerCurrent = true;
    bool firmwareContextCommandCurrentLocked(const ItlFirmwareContextCommand &command) const {
        assert(lock_depth == (command.cleanup ? 1U : 2U) && scanLock.held);
        return scanCommand.open && !command.submitted &&
            command.receipt.generation == static_cast<uint32_t>(context_sc->sc_generation) &&
            contextOwner.commandCurrent(command.receipt.serial, command.receipt.generation) &&
            (command.cleanup || ownerCurrent);
    }
    bool scanCommandOwnerCurrentLocked(uint64_t serial, uint32_t generation) const {
        assert(lock_depth == 2 && scanLock.held);
        return ownerCurrent && scanCommand.current(serial, generation);
    }
    int iwm_send_cmd(iwm_softc *, iwm_host_cmd *);
};
#define nitems(a) (sizeof(a) / sizeof((a)[0]))
#define _KASSERT(x) assert(x)
#define KASSERT(x, text) assert(x)
#define XYLog(...) ((void)0)
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
    IOSimpleLock ownerLock = {false, 1};
    Cursor cursor;
    DmaMap maps[4] = {};
    uint8_t payload[1025] = {};
    iwm_host_cmd cmd = {};
    ItlFirmwareContextCommand context = {};
    Fixture(bool umac = true, bool large = true, bool response = false)
    {
        assert(allocations == 0 && lock_depth == 0);
        fail_allocation = fail_mapping = fail_nic = false;
        sleep_result = 0;
        doorbells = nic_locks = nic_unlocks = 0;
        map_hook = unlock_hook = {};
        sc.sc_generation = 7;
        sc.sc_ic.ic_pae_selected_bss_lock = &ownerLock;
        sc.sc_device_family = IWM_DEVICE_FAMILY_7000;
        for (unsigned i = 0; i != 4; ++i) {
            maps[i].cursor = &cursor;
            sc.txq[0].data[i].map = &maps[i];
            sc.txq[0].data[i].cmd_paddr = 0x4000 + i * 512;
        }
        assert(driver.scanCommand.reopen(0, 7));
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
        for (unsigned i = 0; i != 4; ++i) {
            mbuf_freem(sc.txq[0].data[i].m);
            test_free(sc.sc_cmd_resp_pkt[i]);
        }
        test_free(cmd.resp_pkt);
        assert(allocations == 0 && lock_depth == 0);
    }
};

int main(int argc, char **argv)
{
    if (argc == 2 && std::strcmp(argv[1], "dma-failure") == 0) {
        Fixture f(true, true, true);
        fail_mapping = true;
        f.rejected(ENOMEM);
        std::puts("actual IWM DMA failure: nonzero result and no retained allocation");
        return 0;
    }
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
            /* IWM publishes the reply allocation before DMA mapping.
             * Actual stop frees that old-generation slot before restart. */
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
      assert(f.sc.txq[0].data[0].m && !f.sc.sc_cmd_resp_pkt[0]);
      assert(!f.driver.scanCommand.rejectUnsubmitted(f.cmd.scan_serial, 7));
      ++cases; }
    { Fixture f(true, true, true);
      unlock_hook = [&] {
          assert(f.driver.scanCommand.noteTerminal(7, true, 0, false));
      };
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
