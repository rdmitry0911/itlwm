#include "include/HAL/ItlScanCommandLease.hpp"
#include "include/HAL/ItlTxQueueAllocation.hpp"
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <type_traits>

using IOInterruptState = unsigned;
struct IOSimpleLock { bool held = false; };
struct IOLock { bool held = false; };
static unsigned leaf_held, commands, fail_at, resets;
static unsigned resumeCalls;
static std::function<void()> command_hook;
static IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *lock)
{ assert(lock && !lock->held && !leaf_held); lock->held = true; ++leaf_held; return 1; }
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *lock, IOInterruptState irq)
{ assert(lock && lock->held && leaf_held == 1 && irq == 1); lock->held = false; --leaf_held; }
static void IOLockLock(IOLock *lock)
{ assert(lock && !lock->held && !leaf_held); lock->held = true; }
static void IOLockUnlock(IOLock *lock)
{ assert(lock && lock->held && !leaf_held); lock->held = false; }
struct task {};
static void *systq;
static void task_add(void *, task *) { assert(!leaf_held); ++resets; }
static void taskq_barrier(void *) { assert(!leaf_held); }
enum { IWM_FLAG_SHUTDOWN = 1, IWM_FLAG_HW_ERR = 2, IWM_FLAG_SCANNING = 4,
       IWM_FLAG_BGSCAN = 8, IWM_FLAG_BINDING_ACTIVE = 16,
       IWX_FLAG_SHUTDOWN = 1, IWX_FLAG_HW_ERR = 2, IWX_FLAG_SCANNING = 4,
       IWX_FLAG_BGSCAN = 8, IWX_FLAG_BINDING_ACTIVE = 16,
       IEEE80211_S_INIT = 0, IEEE80211_S_SCAN = 1, IEEE80211_S_AUTH = 2,
       IEEE80211_S_RUN = 4, IEEE80211_ADDR_LEN = 6,
       IWM_DQA_AP_PROBE_RESP_QUEUE = 1, IWM_DQA_GCAST_QUEUE = 2,
       IWM_FW_CTXT_ACTION_ADD = 1, IWM_FW_CTXT_ACTION_REMOVE = 2,
       IWX_FW_CTXT_ACTION_ADD = 1, IWX_FW_CTXT_ACTION_REMOVE = 2,
       IWM_STA_MULTICAST = 1, IWM_STA_GENERAL_PURPOSE = 2,
       IWX_STA_MULTICAST = 1, IWX_STA_GENERAL_PURPOSE = 2,
       IWM_TX_FIFO_MCAST = 1, IWM_TX_FIFO_VO = 2,
       IWM_TID_NON_QOS = 1, IWM_MAX_TID_COUNT = 16, IWX_MGMT_TID = 15,
       IWX_LONG_GROUP = 1, IWX_BEACON_TEMPLATE_CMD = 2,
       IWX_FW_CMD_VER_UNKNOWN = 255, kIOReturnSuccess = 0, kIOReturnNotReady = 1 };
enum { kItlApFirmwareResourceIdle, kItlApFirmwareResourceBeacon,
       kItlApFirmwareResourceMac, kItlApFirmwareResourceBinding,
       kItlApFirmwareResourceMulticastStation, kItlApFirmwareResourceBroadcastStation,
       kItlApFirmwareResourceRunning, kItlApFirmwareResourceStopping };
constexpr unsigned kItlApFirmwareMaxClients = 2;
static const uint8_t etherbroadcastaddr[6] = {255,255,255,255,255,255};
struct ieee80211_channel { unsigned number = 1; };
struct Phy { unsigned id = 0; ieee80211_channel *channel = nullptr; };
struct Node { Phy *in_phyctxt = nullptr; };
struct Ifnet { unsigned if_flags = 0; };
struct Com { unsigned ic_state = IEEE80211_S_INIT; Node *ic_bss = nullptr; Ifnet ic_if; };
struct Softc {
    unsigned sc_flags = 0, sc_generation = 7;
    Com sc_ic;
    Phy sc_phyctxt[2];
    void *sc_nswq = nullptr;
    task init_task, ap_client_task;
};
struct ItlApFirmwareClientRuntime { bool clientStationInstalled = false; };
struct ItlApFirmwareRuntime {
    unsigned stage = kItlApFirmwareResourceIdle;
    struct { unsigned channel = 1; } config;
    uint8_t macId = 0, macColor = 0, broadcastStaId = 0, multicastStaId = 0;
    uint8_t firstClientStaId = 0, broadcastQueueId = 0, multicastQueueId = 0, phyId = 0;
    bool samePhyAsPrimary = false;
    ItlApFirmwareClientRuntime clients[kItlApFirmwareMaxClients];
};
static void itl_ap_firmware_runtime_reset(ItlApFirmwareRuntime *r, bool = false)
{ *r = ItlApFirmwareRuntime{}; }
static unsigned ieee80211_chan2ieee(Com *, ieee80211_channel *channel)
{ return channel->number; }
enum class Phase { Idle, InitialStarting, BackgroundActive };
using ItlIwmWclScanPhase = Phase;
using ItlIwxWclScanPhase = Phase;
using IOReturn = int;
#define XYLog(...) ((void)0)
#define DEVNAME(...) "fixture"
#define container_of(ptr, type, member) reinterpret_cast<type *>(reinterpret_cast<char *>(ptr) - offsetof(type, member))
#define DECLARE(family, lower) \
struct Itl##family { \
    Softc com; IOSimpleLock leaf; IOSimpleLock *wclScanLock = &leaf; \
    IOLock lifecycle; IOLock *apLifecycleLock = &lifecycle; \
    bool apPrimaryStaRecoveryScanYielded = false; \
    Phase wclScanPhase = Phase::Idle; uint64_t wclScanUpperGeneration = 0; \
    ItlScanCommandLease scanCommand = {}; ieee80211_channel channel; \
    ItlTxQueueAllocation txQueueAllocation{}; \
    ItlApFirmwareRuntime apRuntime; \
    bool apLifecycleDetached=false, apStartPending=false, apStopPending=false; \
    bool apLowerRunning=false, apStopRequested=false, apStartResultValid=false; \
    IOReturn apStartResult=0; \
    void resumePrimaryStaRecoveryScanAfterAPHandoff() { assert(!leaf_held && !lifecycle.held); ++resumeCalls; } \
    uint8_t beacon_version = 12; \
    uint64_t reserveAPScanCommand(); uint64_t currentAPScanCommand() const; \
    void finishAPScanCommand(uint64_t, bool); \
    int command() { \
        assert(!leaf_held && !lifecycle.held && scanCommand.apSerial != 0); \
        ++commands; const bool fail = commands == fail_at; \
        if (command_hook) { auto hook = command_hook; command_hook = {}; hook(); } \
        return fail ? EIO : 0; \
    } \
    ieee80211_channel *lower##_ap_find_channel(Softc *, unsigned number) \
        { return number ? &channel : nullptr; } \
    uint8_t lower##_lookup_cmd_ver(Softc *, int, int) { return beacon_version; } \
    IOReturn abortWclBackgroundScan(uint64_t) { return kIOReturnNotReady; } \
    template<class... T> int lower##_phy_ctxt_update(T...) { return command(); } \
    template<class... T> int lower##_ap_send_beacon_template(T...) { return command(); } \
    template<class... T> int lower##_ap_mac_ctxt_cmd(T...) { return command(); } \
    template<class... T> int lower##_ap_binding_cmd(T...) { return command(); } \
    template<class... T> int lower##_ap_add_internal_sta(T...) { return command(); } \
    template<class... T> int lower##_ap_update_quotas(T...) { return command(); } \
    template<class... T> int lower##_ap_remove_client_sta(T...) { return command(); } \
    template<class... T> int lower##_ap_remove_internal_sta(T...) { return command(); } \
    template<class... T> void lower##_del_task(T...) { assert(!leaf_held); } \
    int lower##_start_ap_resources(Softc *, ItlApFirmwareRuntime *); \
    int lower##_stop_ap_resources(Softc *, ItlApFirmwareRuntime *, bool = false); \
    int lower##_start_ap_mode(Softc *, ItlApFirmwareRuntime *); \
    int lower##_stop_ap_mode(Softc *, ItlApFirmwareRuntime *); \
};
DECLARE(Iwm, iwm)
DECLARE(Iwx, iwx)
static IOReturn iwx_ap_start_result_from_errno(int error) { return error; }
#define iwm_softc Softc
#define iwx_softc Softc
#define iwm_node Node
#define iwx_node Node
#include "scan-ap-resources.inc"
#undef iwm_softc
#undef iwx_softc
static int start(ItlIwm &d, ItlApFirmwareRuntime &r) { return d.iwm_start_ap_resources(&d.com, &r); }
static int start(ItlIwx &d, ItlApFirmwareRuntime &r) { return d.iwx_start_ap_mode(&d.com, &r); }
static int stop(ItlIwm &d, ItlApFirmwareRuntime &r) { return d.iwm_stop_ap_resources(&d.com, &r); }
static int stop(ItlIwx &d, ItlApFirmwareRuntime &r) { return d.iwx_stop_ap_mode(&d.com, &r); }
static void observers()
{ assert(!leaf_held); commands = fail_at = resets = resumeCalls = 0; command_hook = {}; }
template<class D> static unsigned exercise()
{
    unsigned count = 0, start_commands = 0, stop_commands = 0;
    observers();
    { D d; ItlApFirmwareRuntime r; assert(d.scanCommand.reopen(0, 7));
      auto scan = d.scanCommand.reserve(7, 91, true, false, 0);
      assert(scan && d.com.sc_flags == 0);
      assert(start(d, r) == EBUSY && !commands && !d.currentAPScanCommand()); ++count; }
    observers();
    { D d; ItlApFirmwareRuntime r; assert(d.scanCommand.reopen(0, 7));
      command_hook = [&] {
          assert(r.stage == kItlApFirmwareResourceIdle);
          assert(d.currentAPScanCommand());
          assert(!d.scanCommand.reserve(7, 92, true, false, 0));
      };
      assert(start(d, r) == 0 && r.stage == kItlApFirmwareResourceRunning);
      start_commands = commands; assert(start_commands == 7);
      auto ap = d.currentAPScanCommand(); assert(ap);
      assert(!d.scanCommand.reserve(7, 92, true, false, 0));
      commands = 0;
      command_hook = [&] { assert(d.currentAPScanCommand() == ap);
          assert(!d.scanCommand.reserve(7, 92, true, false, 0)); };
      assert(stop(d, r) == 0 && r.stage == kItlApFirmwareResourceIdle);
      stop_commands = commands; assert(stop_commands == 5);
      assert(!d.currentAPScanCommand() && !resets);
      assert(d.scanCommand.reserve(7, 92, true, false, 0)); ++count; }
    for (unsigned failed = 1; failed <= start_commands; ++failed) {
      observers(); D d; ItlApFirmwareRuntime r; assert(d.scanCommand.reopen(0, 7));
      fail_at = failed;
      assert(start(d, r) == EIO);
      assert(resets == 1 && !d.scanCommand.open && d.currentAPScanCommand());
      assert(!d.scanCommand.reserve(7, 92, true, false, 0)); ++count;
    }
    for (unsigned failed = 1; failed <= stop_commands; ++failed) {
      observers(); D d; ItlApFirmwareRuntime r; assert(d.scanCommand.reopen(0, 7));
      assert(start(d, r) == 0); commands = 0; fail_at = failed;
      assert(stop(d, r) == EIO);
      if constexpr (std::is_same<D, ItlIwx>::value) {
          assert(r.stage == kItlApFirmwareResourceStopping && commands == failed);
          assert(stop(d, r) == EBUSY && commands == failed);
      } else assert(r.stage == kItlApFirmwareResourceIdle);
      assert(resets == 1 && !d.scanCommand.open && d.currentAPScanCommand());
      assert(!d.scanCommand.reserve(7, 92, true, false, 0)); ++count;
    }
    observers();
    { D d; ItlApFirmwareRuntime r; r.config.channel = 0;
      assert(d.scanCommand.reopen(0, 7));
      assert(start(d, r) == EINVAL && !d.currentAPScanCommand() && !commands); ++count; }
    observers(); return count;
}
int main()
{
    unsigned count = exercise<ItlIwm>() + exercise<ItlIwx>();
    // Execute the real outer workers too: a lower error must not be turned
    // into an unconditional runtime_reset after retaining physical ownership.
    for(unsigned test=0;test<3;++test) {
        observers(); ItlIwx d; assert(d.scanCommand.reopen(0,7));
        if(test==0) {
            assert(start(d,d.apRuntime)==0);
            d.apLowerRunning=true; d.apStopPending=true;
            commands=0; fail_at=1;
            iwx_ap_stop_task(&d.com);
            assert(!d.apStopPending && !d.apLowerRunning && resumeCalls==0);
        } else {
            d.apStartPending=true;
            if(test==1) {
                fail_at=8;
                command_hook=[&] { d.apStopRequested=true; };
            } else {
                fail_at=6;
                std::function<void()> hook;
                hook=[&] {
                    if(commands==6) fail_at=7;
                    else command_hook=hook;
                };
                command_hook=hook;
                iwx_ap_start_task(&d.com);
                assert(d.apStartResultValid && !d.apStartPending);
                assert(d.apRuntime.stage==kItlApFirmwareResourceStopping && !d.scanCommand.open);
                ++count; continue;
            }
            iwx_ap_start_task(&d.com);
            assert(!d.apStartPending && !d.apLowerRunning && resumeCalls==0);
        }
        assert(d.apRuntime.stage==kItlApFirmwareResourceStopping && !d.scanCommand.open);
        ++count;
    }
    { observers(); ItlIwx d; assert(d.scanCommand.reopen(0,7));
      assert(start(d,d.apRuntime)==0); commands=0;
      d.txQueueAllocation.phase=ItlTxQueueAllocation::Phase::Quarantined;
      assert(stop(d,d.apRuntime)==EIO && commands==0 && resets==1);
      assert(d.apRuntime.stage==kItlApFirmwareResourceStopping); ++count; }
    // Link-owned v13 uses the opposite MAC/beacon order; both first commands
    // must still be inside the same physical reservation.
    observers(); ItlIwx d; ItlApFirmwareRuntime r; d.beacon_version = 13;
    assert(d.scanCommand.reopen(0, 7)); assert(start(d, r) == 0);
    assert(d.currentAPScanCommand() && stop(d, r) == 0 && !resets); ++count;
    std::printf("actual IWM/IWX AP/scan resource exclusion: %u scenario groups passed\n", count);
}
