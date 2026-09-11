/* Full production add/drain/remove methods and firmware headers. Kernel
 * softc/node fields and transport completions are explicit fixture boundaries;
 * this verifies ownership publication, not an on-air association. */
#include "scan_test_byte_order.hpp"
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/types.h>
#include <type_traits>
#include <vector>
#include <functional>
#include <cstdlib>
#include <HAL/ItlFirmwareContextLease.hpp>
#include <HAL/ItlStationRxBa.hpp>
#include <HAL/ItlTxQueueAllocation.hpp>
using std::min;
using u8 = uint8_t; using u16 = uint16_t; using u32 = uint32_t;
using u64 = uint64_t; using s8 = int8_t; using s16 = int16_t;
using s32 = int32_t;
using __le16 = uint16_t; using __le32 = uint32_t; using __le64 = uint64_t;
using __be16 = uint16_t;
using bus_addr_t = uint64_t;
#define __packed __attribute__((packed))
#define __aligned(x) __attribute__((aligned(x)))
#ifndef NBBY
#define NBBY 8
#endif
#ifndef howmany
#define howmany(x, y) (((x) + (y) - 1) / (y))
#endif
#define ETHER_ADDR_LEN 6
#define BIT(x) (1U << (x))
#define __BIT(x) BIT(x)
#define le32_to_cpup(x) le32toh(*(x))
#define cpu_to_le16(x) htole16(x)
#define cpu_to_le32(x) htole32(x)
#define isset(a, b) ((a)[(b) / 8] & (1U << ((b) % 8)))
#define setbit(a, b) ((a)[(b) / 8] |= (1U << ((b) % 8)))
#define IEEE80211_ADDR_COPY(a, b) std::memcpy((a), (b), 6)
#define XYLog(...) ((void)0)
#define DPRINTF(...) ((void)0)
#define nitems(a) (sizeof(a)/sizeof((a)[0]))
#define DEVNAME(sc) "fixture"
#define PAGE_SIZE 4096
#define M_NOWAIT 1
#define M_ZERO 2
static void *malloc(size_t size,int,int) { return std::calloc(1,size); }
#include "sta-defines.inc"
#include "itlwm/hal_iwm/if_iwmreg.h"
#include "itlwm/hal_iwx/if_iwxreg.h"
#include "sta-host-commands.inc"
using Lease = ItlFirmwareContextLease;
struct IOSimpleLock { unsigned rank; };
using IOInterruptState = unsigned;
static IOSimpleLock selectedLock{1}, halLock{2};
static std::vector<unsigned> locks;
static IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *lock)
{ assert(lock && (locks.empty() || locks.back() < lock->rank)); auto depth=locks.size(); locks.push_back(lock->rank); return depth; }
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *lock, IOInterruptState depth)
{ assert(lock && !locks.empty() && locks.back()==lock->rank); locks.pop_back(); assert(locks.size()==depth); }
static void IOSimpleLockLock(IOSimpleLock *lock) {
    assert(locks.empty()); (void)IOSimpleLockLockDisableInterrupt(lock);
}
static void IOSimpleLockUnlock(IOSimpleLock *lock) { IOSimpleLockUnlockEnableInterrupt(lock,0); }

enum { IEEE80211_M_STA, IEEE80211_M_MONITOR, IEEE80211_S_ASSOC = 3 };
enum { IEEE80211_NODE_HT = 1, IEEE80211_NODE_VHT = 2, IEEE80211_NODE_HE = 4 };
enum { IEEE80211_CHAN_WIDTH_20, IEEE80211_CHAN_WIDTH_40,
       IEEE80211_CHAN_WIDTH_80, IEEE80211_CHAN_WIDTH_160,
       IEEE80211_CHAN_WIDTH_80P80 };
enum { EDCA_NUM_AC = 4, IEEE80211_AMPDU_PARAM_SS = 7,
       IEEE80211_AMPDU_PARAM_SS_2 = 4, IEEE80211_AMPDU_PARAM_SS_4 = 5,
       IEEE80211_AMPDU_PARAM_SS_8 = 6, IEEE80211_AMPDU_PARAM_SS_16 = 7 };
constexpr uint32_t IEEE80211_VHTCAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK = 7U << 23;
constexpr uint32_t IEEE80211_VHTCAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT = 23;
constexpr uint8_t IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_MASK = 0x18;
#define splassert(x) ((void)0)
#define IPL_NET 0
constexpr int IEEE80211_NUM_TID = 16, IEEE80211_BA_AGREED = 1;
struct ieee80211_tx_ba { int ba_state = 0; };
static uint8_t u8_get_bits(uint8_t value, uint8_t mask)
{ return (value & mask) / (mask & -mask); }
static uint8_t etheranyaddr[6];
struct ieee80211_node {
    unsigned ni_flags = 0, ni_chw = 0, ni_rx_nss = 2;
    uint32_t ni_vhtcaps = 3U << 23;
    struct { uint8_t mac_cap_info[6] = {}; } ni_he_cap_elem;
    ieee80211_tx_ba ni_tx_ba[IEEE80211_NUM_TID];
};
struct ieee80211com {
    IOSimpleLock *ic_pae_selected_bss_lock = &selectedLock;
    ItlStateTransitionIdentity identity{11,11,12};
    int ic_opmode = IEEE80211_M_STA, ic_state = IEEE80211_S_ASSOC;
    unsigned ic_ampdu_params = 4;
    ieee80211_node *ic_bss = nullptr;
};
struct ItlScanCommandPolicy {
    static ItlStateTransitionIdentity identityLocked(const ieee80211com *ic)
    { assert(!locks.empty() && locks.front()==1); return ic->identity; }
};
struct iwm_node { ieee80211_node in_ni; unsigned in_id = 1, in_color = 2; uint8_t in_macaddr[6] = {2,3,4,5,6,7}; };
struct iwx_node { ieee80211_node in_ni; unsigned in_id = 1, in_color = 2; uint8_t in_macaddr[6] = {2,3,4,5,6,7}; };
struct iwx_tx_ring { int cur=0, ring_count=256, retired=0; ItlTxQueueFirmwareOwner firmware{}; };
struct Softc {
    ieee80211com sc_ic;
    uint32_t sc_flags = 0;
    int sc_generation = 7;
    unsigned taskActive = 0;
    bool taskAdmission = true;
    bool sc_mqrx_supported = true;
    unsigned sc_rxba_data[IWM_MAX_BAID] = {};
    uint32_t agg_queue_mask = 0x3000, agg_tid_disable = 0xfeed;
    uint8_t sc_ucode_api[128] = {}, sc_enabled_capa[128] = {};
    int first_data_qid = 4, sc_rx_ba_sessions = 2;
    struct { int start_tidmask = 1, stop_tidmask = 2; } ba_rx, ba_tx;
    iwx_tx_ring txq[64];
    IOSimpleLock queueLock{1};
    IOSimpleLock *sc_txq_locks[64];
    struct { int qid=IWX_INVALID_QUEUE; } sc_tid_data[IWX_MAX_TID_COUNT+1];
    Softc() { for(auto &lock:sc_txq_locks) lock=&queueLock; }
};
struct iwm_softc : Softc {};
struct iwx_softc : Softc {};
/* Carrier/DMA lifetime is executed by test_iwx_tvqm_allocation; this suite
 * keeps the station command graph with an explicit value-only carrier. */
static int iwx_ap_exchange_tx_ring_carrier(iwx_softc *sc,uint16_t queue,iwx_tx_ring *,iwx_tx_ring *out) {
    assert(locks.empty() && !sc->txq[queue].firmware.owned);
    *out=sc->txq[queue]; sc->txq[queue]={}; return 0;
}
static bool iwm_mimo_enabled(iwm_softc *) { return true; }
static bool iwx_mimo_enabled(iwx_softc *) { return true; }


enum Edge { NoEdge = 0, Add, DrainOn, Flush, DrainOff, DisableQueue, Remove, Delba, RxBa, TxBa, MlBa };
static std::vector<int> edges;
static int failEdge, resetEdge, statusEdge, replaceEdge;
static int transportError = ETIMEDOUT;
static unsigned cases;
static constexpr uint32_t unrelatedFlag = 0x40000000;
static std::function<void()> beforeSubmit, afterSubmit;
static Softc *baDevice;
static uint32_t lastStation, lastMac, lastQueues, lastFlags, lastModify;
static unsigned lastLength;
static int commandVersion;
static unsigned packetOwners, replyKind, reclaimed;
static bool baMode;
static unsigned baResponse = 3, baResponseKind = 0;
static unsigned lastTid, lastSsn, lastWindow, lastDisabled, lastBaid;
static unsigned disableCalls, failDisableCall;
static bool rejectRepeatedQueueRemoval;
static std::vector<unsigned> removedQueues;
static void cleanFixture()
{
    assert(locks.empty()); edges.clear();
    failEdge=resetEdge=statusEdge=replaceEdge=0;
    beforeSubmit=nullptr; afterSubmit=nullptr; baDevice=nullptr;
    lastStation=lastMac=lastQueues=lastFlags=lastModify=lastLength=0;
    commandVersion=0; assert(packetOwners==0); replyKind=reclaimed=0;
    baMode=false; baResponse=3; baResponseKind=0;
    lastTid=lastSsn=lastWindow=lastDisabled=lastBaid=0;
    disableCalls=failDisableCall=0; rejectRepeatedQueueRemoval=false; removedQueues.clear();
}
static void ieee80211_delba_request(ieee80211com *, ieee80211_node *, int, int, int)
{
    assert(baDevice); edges.push_back(Delba);
    if (resetEdge == Delba) { ++baDevice->sc_generation; baDevice->sc_flags=unrelatedFlag; }
}
struct DriverState {
    IOSimpleLock *wclScanLock = &halLock;
    Lease primaryMacContext{}, primaryBindingContext{}, primaryStationContext{};
    ItlFirmwareStationRetirement primaryStationRetirement{};
    ItlFirmwareStationUses primaryStationUses{};
    ItlStationRxBa primaryRxBa{};
    ItlTxQueueAllocation txQueueAllocation{};
    unsigned resumeChecks = 0;
    void resumePrimaryStationUsers() { assert(locks.empty()); ++resumeChecks; }
    struct { bool open=true; } scanCommand;
};
template<class Driver, class Device>
static int submit(Driver &driver, Device *sc, ItlFirmwareContextCommand *context, int edge)
{
    assert(locks.empty() && context);
    auto before=beforeSubmit; beforeSubmit=nullptr; if(before) before();
    IOInterruptState selected=0;
    if(!context->cleanup) selected=IOSimpleLockLockDisableInterrupt(sc->sc_ic.ic_pae_selected_bss_lock);
    auto irq=IOSimpleLockLockDisableInterrupt(driver.wclScanLock);
    const bool current=driver.firmwareContextCommandCurrentLocked(*context);
    if(current) context->submitted=true;
    IOSimpleLockUnlockEnableInterrupt(driver.wclScanLock,irq);
    if(!context->cleanup) IOSimpleLockUnlockEnableInterrupt(sc->sc_ic.ic_pae_selected_bss_lock,selected);
    if(!current) return ENXIO;
    edges.push_back(edge);
    if(edge==DisableQueue && ++disableCalls==failDisableCall)
        return ETIMEDOUT;
    if(replaceEdge==edge) {
        ++sc->sc_ic.identity.associationEpoch;
        sc->sc_ic.ic_opmode=IEEE80211_M_MONITOR;
    }
    auto after=afterSubmit; afterSubmit=nullptr; if(after) after();
    if(resetEdge==edge) {
        ++sc->sc_generation;
        driver.primaryStationContext.clear();
        driver.primaryMacContext.clear(); driver.primaryBindingContext.clear();
        sc->sc_flags=unrelatedFlag; sc->agg_queue_mask=0xface; sc->agg_tid_disable=0xbeef;
        return ENXIO;
    }
    return failEdge==edge ? transportError : 0;
}
#define OWNER_DECLS \
    int beginPrimaryBaCommand(const ItlFirmwareContextReceipt *, ItlFirmwareContextCommand *); \
    int finishPrimaryBaCommand(const ItlFirmwareContextCommand &,int,bool); \
    bool beginPrimaryStationUse(ieee80211_node *, ItlFirmwareContextReceipt *, bool = true); \
    void endPrimaryStationUse(ItlFirmwareContextReceipt *); \
    bool releasePrimaryStationReader(ItlFirmwareContextReceipt *); \
    int retirePrimaryRxBa() { return 0; } \
    bool firmwareContextCommandCurrentLocked(const ItlFirmwareContextCommand &) const; \
    int beginPrimaryStationCleanup(bool, ItlFirmwareContextReceipt *); \
    int finishPrimaryStationCleanup(const ItlFirmwareContextReceipt &, int); \
    bool notePrimaryStationRetirement(const ItlFirmwareContextReceipt &, uint8_t, int = -1)
template<class Driver,class Device,class Command,class Wire>
static int statusCommand(Driver &d,Device *sc,Command *command,uint32_t *status,uint32_t drainFlag,uint32_t success)
{
    const auto &wire=*static_cast<const Wire *>(command->data[0]);
    const bool drain=wire.station_flags_msk==htole32(drainFlag);
    const bool rx=baMode && (wire.modify_mask==IWM_STA_MODIFY_ADD_BA_TID ||
                            wire.modify_mask==IWM_STA_MODIFY_REMOVE_BA_TID);
    const int edge=baMode ? (rx?RxBa:TxBa) : drain ? (wire.station_flags ? DrainOn : DrainOff) : Add;
    lastMac=le32toh(wire.mac_id_n_color); lastStation=wire.sta_id;
    lastQueues=wire.tfd_queue_msk; lastFlags=wire.station_flags;
    lastModify=wire.modify_mask; lastLength=command->len[0];
    *status=statusEdge==edge ? 0 : success;
    if(baMode) {
        assert(wire.add_modify==IWM_STA_MODE_MODIFY);
        if(baResponseKind>=2)
            *status=baResponseKind==2 ? IWM_ADD_STA_IMMEDIATE_BA_FAILURE :
                baResponseKind==3 ? IWM_ADD_STA_MODIFY_NON_EXISTING_STA : 0;
        lastTid=wire.modify_mask==IWM_STA_MODIFY_ADD_BA_TID ?
            wire.add_immediate_ba_tid : wire.remove_immediate_ba_tid;
        lastSsn=le16toh(wire.add_immediate_ba_ssn);
        lastWindow=le16toh(wire.rx_ba_window);
        lastDisabled=le16toh(wire.tid_disable_tx);
        if(rx && wire.modify_mask==IWM_STA_MODIFY_ADD_BA_TID)
            *status|=(baResponse<<IWM_ADD_STA_BAID_SHIFT) |
                (baResponseKind==1?0:IWM_ADD_STA_BAID_VALID_MASK);
    }
    const int result=submit(d,sc,command->context_command,edge);
    if(!baMode && edge==Add && wire.add_modify==0 && result==0 && (*status&0xff)==success)
        sc->txq[4].firmware={command->context_command->receipt.serial,d.txQueueAllocation.lifecycle,
            static_cast<uint32_t>(sc->sc_generation),wire.sta_id,IWX_MGMT_TID,true,false,false,false};
    return result;
}
class ItlIwm : public DriverState {
public:
    iwm_softc com;
    struct iwm_add_sta_cmd primaryStationCommand{};
    OWNER_DECLS;
    int iwm_add_sta_cmd(iwm_softc *,iwm_node *,int,unsigned);
    int iwm_sta_rx_ba_cmd(iwm_softc *,const ItlFirmwareContextReceipt *,uint8_t,uint16_t,uint16_t,bool,uint8_t *);
    int iwm_sta_tx_ba_cmd(iwm_softc *,const ItlFirmwareContextReceipt *,uint32_t,uint16_t);
    int iwm_drain_sta(iwm_softc *,const ItlFirmwareContextReceipt &,bool);
    int iwm_rm_sta_cmd(iwm_softc *,iwm_node *);
    int iwm_send_cmd_status(iwm_softc *sc,iwm_host_cmd *cmd,uint32_t *status) {
        return statusCommand<ItlIwm,iwm_softc,iwm_host_cmd,struct iwm_add_sta_cmd>(*this,sc,cmd,status,IWM_STA_FLG_DRAIN_FLOW,IWM_ADD_STA_SUCCESS);
    }
    int iwm_send_cmd(iwm_softc *sc,iwm_host_cmd *cmd) {
        if(cmd->id==IWM_TXPATH_FLUSH) {
            const auto *wire=static_cast<const iwm_tx_path_flush_cmd_v1 *>(cmd->data[0]);
            lastQueues=wire->queues_ctl;
            assert(le16toh(wire->flush_ctl)==IWM_DUMP_TX_FIFO_FLUSH);
            return submit(*this,sc,cmd->context_command,Flush);
        }
        if(cmd->id==IWM_SCD_QUEUE_CFG) {
            const auto *wire=static_cast<const iwm_scd_txq_cfg_cmd *>(cmd->data[0]);
            assert(wire->sta_id==cmd->context_command->receipt.identity.station);
            assert(wire->enable==IWM_SCD_CFG_DISABLE_QUEUE);
            int result=submit(*this,sc,cmd->context_command,DisableQueue);
            if(result==0 && rejectRepeatedQueueRemoval) {
                if(std::find(removedQueues.begin(),removedQueues.end(),wire->scd_queue)!=removedQueues.end())
                    return EIO;
                removedQueues.push_back(wire->scd_queue);
            }
            return result;
        }
        assert(cmd->id==IWM_REMOVE_STA);
        const auto *wire=static_cast<const struct iwm_rm_sta_cmd *>(cmd->data[0]);
        lastStation=wire->sta_id;
        assert(wire->reserved[0]==0 && wire->reserved[1]==0 && wire->reserved[2]==0);
        return submit(*this,sc,cmd->context_command,Remove);
    }
    int iwm_send_cmd_pdu_status(iwm_softc *sc,int,size_t,const void *,uint32_t *status) {
        *status=statusEdge==Add ? 0 : IWM_ADD_STA_SUCCESS; edges.push_back(Add);
        if(resetEdge==Add) { ++sc->sc_generation; sc->sc_flags=unrelatedFlag; }
        return failEdge==Add ? transportError : 0;
    }
    int iwm_flush_tx_path(iwm_softc *,int,ItlFirmwareContextCommand *);
    int iwm_disable_txq(iwm_softc *,uint8_t,uint8_t,uint8_t,ItlFirmwareContextCommand *);
};
class ItlIwx : public DriverState {
public:
    iwx_softc com;
    int beginTxQueueAllocation(iwx_softc *sc,uint8_t station,uint8_t tid,int queue,
        ItlTxQueueAllocationCommand *command,bool retirement=false) {
        assert(locks.empty() && retirement && primaryStationUses.closed && !primaryStationUses.active);
        if(txQueueAllocation.phase!=ItlTxQueueAllocation::Phase::Idle) return EBUSY;
        *command={}; command->serial=++txQueueAllocation.nextSerial;
        command->lifecycle=txQueueAllocation.lifecycle; command->generation=sc->sc_generation;
        command->station=station; command->tid=tid; command->queue=queue; command->retirement=true;
        txQueueAllocation.current=*command; txQueueAllocation.phase=ItlTxQueueAllocation::Phase::Building;
        ++sc->taskActive; return 0;
    }
    void finishTxQueueAllocation(iwx_softc *sc,ItlTxQueueAllocationCommand *,bool quarantine) {
        assert(locks.empty() && sc->taskActive); --sc->taskActive;
        txQueueAllocation.phase=quarantine?ItlTxQueueAllocation::Phase::Quarantined:ItlTxQueueAllocation::Phase::Idle;
    }
    int iwx_retire_station_tx_queues(iwx_softc *,uint8_t,ItlFirmwareContextCommand *,bool);
    bool iwx_task_gate_enter(iwx_softc *sc,bool) {
        assert(locks.empty());
        if(!sc->taskAdmission) return false;
        ++sc->taskActive; return true;
    }
    void iwx_task_gate_leave(iwx_softc *sc) {
        assert(locks.empty() && sc->taskActive);
        --sc->taskActive;
    }
    struct iwx_add_sta_cmd primaryStationCommand{};
    OWNER_DECLS;
    bool primaryStationCleanupCurrent(const ItlFirmwareContextReceipt &) const;
    int iwx_add_sta_cmd(iwx_softc *,iwx_node *,int);
    int iwx_sta_rx_ba_cmd(iwx_softc *,const ItlFirmwareContextReceipt *,uint8_t,uint16_t,uint16_t,bool,uint8_t *);
    int iwx_rx_baid_cfg_cmd(iwx_softc *,uint8_t,uint8_t,uint16_t,uint16_t,bool,uint8_t *,ItlFirmwareContextCommand * = nullptr);
    int iwx_drain_sta(iwx_softc *,const ItlFirmwareContextReceipt &,int);
    int iwx_flush_sta(iwx_softc *,iwx_node *);
    int iwx_flush_station(iwx_softc *,const ItlFirmwareContextReceipt &);
    int iwx_remove_station(iwx_softc *,const ItlFirmwareContextReceipt &);
    int iwx_rm_sta_cmd(iwx_softc *,iwx_node *);
    int iwx_rm_sta(iwx_softc *,iwx_node *);
    int iwx_send_cmd_status(iwx_softc *sc,iwx_host_cmd *cmd,uint32_t *status) {
        if(cmd->id==IWX_WIDE_ID(IWX_DATA_PATH_GROUP,IWX_RX_BAID_ALLOCATION_CONFIG_CMD)) {
            const auto &wire=*static_cast<const struct iwx_rx_baid_cfg_cmd *>(cmd->data[0]);
            if(le32toh(wire.action)==IWX_RX_BAID_ACTION_ADD) {
                lastStation=le32toh(wire.alloc.sta_id_mask);
                lastTid=wire.alloc.tid; lastSsn=le16toh(wire.alloc.ssn);
                lastWindow=le16toh(wire.alloc.win_size);
            } else if(commandVersion==1) lastBaid=le32toh(wire.remove_v1.baid);
            else { lastStation=le32toh(wire.remove.sta_id_mask); lastTid=le32toh(wire.remove.tid); }
            *status=baResponse;
            if(cmd->context_command==nullptr) { // AP transport boundary, independent of primary STA.
                edges.push_back(MlBa);
                return failEdge==MlBa ? transportError : 0;
            }
            return submit(*this,sc,cmd->context_command,MlBa);
        }
        return statusCommand<ItlIwx,iwx_softc,iwx_host_cmd,struct iwx_add_sta_cmd>(*this,sc,cmd,status,IWX_STA_FLG_DRAIN_FLOW,IWX_ADD_STA_SUCCESS);
    }
    int iwx_send_cmd(iwx_softc *sc,iwx_host_cmd *cmd) {
        if(cmd->queue_allocation) {
            assert(txQueueAllocation.physical(*cmd->queue_allocation,sc->sc_generation));
            cmd->queue_allocation->submitted=true;
        }
        if(cmd->id!=IWX_REMOVE_STA) {
            const bool flush=cmd->id==IWX_TXPATH_FLUSH;
            if(flush) {
                const auto *wire=static_cast<const iwx_tx_path_flush_cmd *>(cmd->data[0]);
                assert(le32toh(wire->sta_id)==cmd->context_command->receipt.identity.station);
                assert(le16toh(wire->tid_mask)==0xffff);
            } else if(commandVersion==3) {
                const auto *wire=static_cast<const iwx_scd_queue_cfg_cmd *>(cmd->data[0]);
                assert(le32toh(wire->operation)==IWX_SCD_QUEUE_REMOVE);
                assert(le32toh(wire->u.remove.sta_mask)==(1U<<cmd->context_command->receipt.identity.station));
            } else {
                const auto *wire=static_cast<const iwx_tx_queue_cfg_cmd *>(cmd->data[0]);
                assert(wire->sta_id==cmd->context_command->receipt.identity.station);
                assert((le16toh(wire->flags)&IWX_TX_QUEUE_CFG_ENABLE_QUEUE)==0);
            }
            assert(sc->sc_flags&IWX_FLAG_TXFLUSH);
            int err=submit(*this,sc,cmd->context_command,flush?Flush:DisableQueue);
            if(!flush && err==0 && rejectRepeatedQueueRemoval) {
                if(std::find(removedQueues.begin(),removedQueues.end(),4U)!=removedQueues.end())
                    return EIO;
                removedQueues.push_back(4);
            }
            if(err || replyKind==1) return err;
            const size_t length=flush?sizeof(iwx_tx_path_flush_cmd_rsp):sizeof(iwx_tx_queue_cfg_rsp);
            auto *pkt=static_cast<iwx_rx_packet *>(std::calloc(1,sizeof(iwx_rx_packet)+length));
            assert(pkt); ++packetOwners; cmd->resp_pkt=pkt;
            pkt->len_n_flags=htole32(sizeof(pkt->hdr)+length);
            if(replyKind==2) pkt->hdr.group_id|=IWX_CMD_FAILED_MSK;
            if(flush) {
                auto *response=reinterpret_cast<iwx_tx_path_flush_cmd_rsp *>(pkt->data);
                response->sta_id=htole16(cmd->context_command->receipt.identity.station);
                response->num_flushed_queues=htole16(1);
                response->queues[0].tid=htole16(IWX_MGMT_TID);
                response->queues[0].queue_num=htole16(4);
                response->queues[0].read_after_flush=htole16(17);
            }
            return 0;
        }
        const auto *wire=static_cast<const struct iwx_rm_sta_cmd *>(cmd->data[0]);
        lastStation=wire->sta_id;
        assert(wire->reserved[0]==0 && wire->reserved[1]==0 && wire->reserved[2]==0);
        int result=submit(*this,sc,cmd->context_command,Remove);
        if(result==0 && replyKind!=1) {
            auto *pkt=static_cast<iwx_rx_packet *>(std::calloc(1,sizeof(iwx_rx_packet)));
            assert(pkt); ++packetOwners; cmd->resp_pkt=pkt;
            pkt->len_n_flags=htole32(sizeof(pkt->hdr));
            if(replyKind==2) pkt->hdr.group_id|=IWX_CMD_FAILED_MSK;
        }
        return result;
    }
    int iwx_send_cmd_pdu_status(iwx_softc *sc,int,size_t,const void *,uint32_t *status) {
        *status=statusEdge==Add ? 0 : IWX_ADD_STA_SUCCESS; edges.push_back(Add);
        if(resetEdge==Add) { ++sc->sc_generation; sc->sc_flags=unrelatedFlag; }
        return failEdge==Add ? transportError : 0;
    }
    int iwx_flush_sta_tids(iwx_softc *,int,uint16_t,ItlFirmwareContextCommand *,ItlTxQueueAllocationCommand * = nullptr);
    int iwx_disable_txq(iwx_softc *,int,int,uint8_t,ItlFirmwareContextCommand *,ItlTxQueueAllocationCommand * = nullptr);
    int iwx_lookup_cmd_ver(iwx_softc *,int,int) { return commandVersion; }
    void iwx_free_resp(iwx_softc *,iwx_host_cmd *cmd) {
        if(cmd->resp_pkt) { assert(packetOwners); --packetOwners; std::free(cmd->resp_pkt); cmd->resp_pkt=nullptr; }
    }
    void iwx_reset_tx_ring(iwx_softc *,iwx_tx_ring *ring) { ++ring->retired; }
    void iwx_free_tx_ring(iwx_softc *,iwx_tx_ring *) { assert(false); }
    void iwx_ampdu_txq_advance(iwx_softc *,iwx_tx_ring *,int index) { assert(index==17); ++reclaimed; }
};
#include "sta-commands.inc"
template<class Driver,class Node>
static void prepare(Driver &d,Node &node,int mode=IEEE80211_M_STA)
{
    auto &sc=d.com;
    sc.sc_ic.ic_bss=&node.in_ni; sc.sc_ic.ic_opmode=mode; sc.sc_flags=unrelatedFlag;
    setbit(sc.sc_ucode_api,IWM_UCODE_TLV_API_STA_TYPE);
    ItlFirmwareContextIdentity id{};
    id.attempt=sc.sc_ic.identity; id.mac=(node.in_id | node.in_color<<8);
    id.mode=mode; std::memcpy(id.peer,node.in_macaddr,6);
    for(auto *context : {&d.primaryMacContext,&d.primaryBindingContext}) {
        context->owner={1,static_cast<uint32_t>(sc.sc_generation),id};
        context->stage=Lease::Stage::Active; context->confirmed=true;
    }
    d.primaryBindingContext.owner.identity.phy=0x123;
    d.primaryBindingContext.owner.identity.lmac=1;
    sc.sc_tid_data[IWX_MAX_TID_COUNT].qid=4;
}
template<class Driver,class Node>
static int add(Driver &d,Node &node,int update=0)
{
    if constexpr(std::is_same<Driver,ItlIwm>::value)
        return d.iwm_add_sta_cmd(&d.com,&node,update,0);
    else return d.iwx_add_sta_cmd(&d.com,&node,update);
}
template<class Driver,class Node>
static int removeStation(Driver &d,Node *node)
{
    if constexpr(std::is_same<Driver,ItlIwm>::value) return d.iwm_rm_sta_cmd(&d.com,node);
    else return d.iwx_rm_sta(&d.com,node);
}
template<class Driver,class Node>
static void familyTests(const char *selected)
{
    constexpr bool iwm=std::is_same<Driver,ItlIwm>::value;
    const uint32_t active=iwm?IWM_FLAG_STA_ACTIVE:IWX_FLAG_STA_ACTIVE;
    if(std::strcmp(selected,"all")==0 || std::strcmp(selected,"ba")==0) {
        for(int abi : {0,1,2})
        for(bool reader : {false,true})
        for(bool start : {false,true})
        for(int fault=0; fault<13; ++fault) {
            cleanFixture(); Driver d; Node n; prepare(d,n);
            if constexpr(iwm) {
                if(abi==0) d.com.sc_ucode_api[IWM_UCODE_TLV_API_STA_TYPE/8]&=
                    ~(1U<<(IWM_UCODE_TLV_API_STA_TYPE%8));
                d.com.sc_mqrx_supported=abi!=2;
            } else if(abi!=0) {
                setbit(d.com.sc_enabled_capa,IWX_UCODE_TLV_CAPA_BAID_ML_SUPPORT);
            }
            commandVersion=abi;
            assert(add(d,n)==0);
            const auto original=d.primaryStationContext.owner;
            ItlFirmwareContextReceipt use{};
            if(reader) assert(d.beginPrimaryStationUse(&n.in_ni,&use));
            else { ++d.com.sc_ic.identity.associationEpoch; ++n.in_id; ++n.in_macaddr[5]; }
            // Changing advertised layout after ADD may not change retained wire size.
            if constexpr(iwm)
                d.com.sc_ucode_api[IWM_UCODE_TLV_API_STA_TYPE/8]^=1U<<(IWM_UCODE_TLV_API_STA_TYPE%8);
            baMode=true; edges.clear();
            const int edge=!iwm && abi!=0 ? MlBa : RxBa;
            if(fault==1) failEdge=edge;
            if(fault==2) beforeSubmit=[&] { ++d.com.sc_generation; d.primaryStationContext.clear(); };
            if(fault==3) resetEdge=edge;
            if(fault==4) beforeSubmit=[&] { ++d.com.sc_ic.identity.associationEpoch; };
            if(fault==5) replaceEdge=edge;
            if(fault==6) baResponseKind=1;
            if(fault==7) baResponse=IWM_MAX_BAID;
            if(fault>=8 && fault<=10) baResponseKind=fault-6;
            if(fault==11) { failEdge=edge; baResponseKind=2; }
            if(fault==12) baResponse=UINT32_MAX;
            uint8_t baid=7;
            int error;
            if constexpr(iwm)
                error=d.iwm_sta_rx_ba_cmd(&d.com,reader?&use:nullptr,5,0x123,64,start,&baid);
            else
                error=d.iwx_sta_rx_ba_cmd(&d.com,reader?&use:nullptr,5,0x123,64,start,&baid);
            const bool legacy=iwm || abi==0;
            const bool validatesId=start && (!iwm || abi!=2);
            const bool invalidId=validatesId && ((fault==6 && legacy) || fault==7 || fault==12);
            const bool statusFailure=legacy && fault>=8 && fault<=10;
            const bool refused=statusFailure && fault==8 && start;
            int expected=(fault==1 || fault==11) ? ETIMEDOUT :
                (fault==2 || fault==3 || (fault==4 && reader)) ? ENXIO :
                statusFailure ? (refused?ENOSPC:EIO) :
                invalidId ? (!legacy?ERANGE:EPROTO) : 0;
            assert(error==expected);
            if(error==0) {
                assert(baid==(start ? (iwm && abi==2 ? IWM_RX_REORDER_DATA_INVALID_BAID : 3) : 7));
                if(legacy) {
                    assert(lastMac==original.identity.mac && lastStation==original.identity.station);
                    assert(lastLength==original.identity.commandLength);
                } else if(start || abi!=1) {
                    assert(lastStation==(1U<<original.identity.station));
                } else assert(lastBaid==7);
                assert(lastTid==5 || (!start && !legacy && abi==1));
                if(start) assert(lastSsn==0x123 && lastWindow==64);
            } else assert(baid==7);
            if(fault==1 || fault==11 || invalidId || (statusFailure && !refused))
                assert(d.primaryStationContext.uncertain);
            if(refused) {
                assert(d.primaryStationContext.confirmed && !d.primaryStationContext.uncertain);
                assert(d.primaryStationContext.stage==Lease::Stage::Active);
                if(reader) {
                    ItlFirmwareContextReceipt tx{};
                    assert(d.beginPrimaryStationUse(&n.in_ni,&tx));
                    d.endPrimaryStationUse(&tx);
                }
                baResponseKind=0;
                if constexpr(iwm)
                    assert(d.iwm_sta_rx_ba_cmd(&d.com,reader?&use:nullptr,5,0x123,64,true,&baid)==0);
                else
                    assert(d.iwx_sta_rx_ba_cmd(&d.com,reader?&use:nullptr,5,0x123,64,true,&baid)==0);
            }
            if(fault==4 && reader) {
                assert(edges.empty() && !d.primaryStationContext.uncertain);
                assert(d.primaryStationContext.stage==Lease::Stage::Active);
            }
            if(reader) d.endPrimaryStationUse(&use);
            assert(d.com.taskActive==0);
            ++cases;
        }
        if constexpr(iwm) {
            for(bool reader : {false,true}) {
                cleanFixture(); Driver d; Node n; prepare(d,n); assert(add(d,n)==0);
                const auto original=d.primaryStationContext.owner;
                ItlFirmwareContextReceipt use{};
                if(reader) assert(d.beginPrimaryStationUse(&n.in_ni,&use));
                else { ++n.in_id; ++n.in_macaddr[5]; ++d.com.sc_ic.identity.associationEpoch; }
                baMode=true;
                assert(d.iwm_sta_tx_ba_cmd(&d.com,reader?&use:nullptr,0x1680,0xffad)==0);
                assert(lastMac==original.identity.mac && lastStation==original.identity.station);
                assert(le32toh(lastQueues)==0x1680 && lastDisabled==0xffad);
                if(reader) d.endPrimaryStationUse(&use);
                ++cases;
            }
        } else {
            for(int version : {1,2}) for(bool start : {false,true})
            for(int fault=0; fault<6; ++fault) {
                cleanFixture(); Driver d; uint8_t baid=7; baMode=true;
                commandVersion=version;
                if(fault==1) failEdge=MlBa;
                if(fault==2) baResponse=IWX_MAX_BAID;
                if(fault==5) baid=IWX_RX_REORDER_DATA_INVALID_BAID;
                const int error=d.iwx_rx_baid_cfg_cmd(&d.com,fault==3?32:6,
                    fault==4?IWX_MAX_TID_COUNT:4,0x456,32,start,&baid);
                const int expected=fault==3 || fault==4 ? EINVAL :
                    fault==5 && !start ? ENOENT : fault==1 ? ETIMEDOUT :
                    fault==2 && start ? ERANGE : 0;
                assert(error==expected);
                if(error==0) {
                    assert(baid==(start?3:7));
                    if(start || version==2) assert(lastStation==(1U<<6) && lastTid==4);
                    else assert(lastBaid==7);
                    if(start) assert(lastSsn==0x456 && lastWindow==32);
                } else assert(baid==(fault==5?IWX_RX_REORDER_DATA_INVALID_BAID:7));
                assert(d.primaryStationContext.stage==Lease::Stage::Empty);
                assert(d.primaryStationUses.active==0 && d.com.taskActive==0);
                ++cases;
            }
        }
        if(std::strcmp(selected,"ba")==0) return;
    }
    if(std::strcmp(selected,"all")==0 || std::strcmp(selected,"users")==0) {
        for(int mismatch=0; mismatch<5; ++mismatch) {
            cleanFixture(); Driver d; Node n; prepare(d,n); assert(add(d,n)==0);
            if(mismatch==1) ++d.com.sc_ic.identity.associationEpoch;
            if(mismatch==2) ++n.in_id;
            if(mismatch==3) ++n.in_macaddr[5];
            if(mismatch==4) ++d.com.sc_generation;
            ItlFirmwareContextReceipt use{};
            assert(d.beginPrimaryStationUse(&n.in_ni,&use)==(mismatch==0));
            if(mismatch==0) {
                assert(d.primaryStationUses.active==1);
                d.endPrimaryStationUse(&use);
                assert(d.primaryStationUses.active==0 && use.serial==0 && d.resumeChecks==1);
            }
            assert(d.com.taskActive==0);
            ++cases;
        }
        {
            cleanFixture(); Driver d; Node n; prepare(d,n); assert(add(d,n)==0);
            ItlFirmwareContextReceipt first{},second{},rejected{};
            assert(d.beginPrimaryStationUse(&n.in_ni,&first));
            assert(d.beginPrimaryStationUse(&n.in_ni,&second));
            edges.clear();
            assert(removeStation(d,static_cast<Node *>(nullptr))==EBUSY && edges.empty());
            assert(d.primaryStationUses.closed && d.primaryStationUses.active==2);
            assert(!d.beginPrimaryStationUse(&n.in_ni,&rejected,false));
            d.endPrimaryStationUse(&first);
            assert(removeStation(d,static_cast<Node *>(nullptr))==EBUSY && edges.empty());
            d.endPrimaryStationUse(&second);
            assert(removeStation(d,static_cast<Node *>(nullptr))==0);
            assert(add(d,n)==0 && d.beginPrimaryStationUse(&n.in_ni,&first));
            d.endPrimaryStationUse(&first);
            ++cases;
        }
        {
            cleanFixture(); Driver d; Node n; prepare(d,n); assert(add(d,n)==0);
            ++d.com.sc_ic.identity.associationEpoch;
            ItlFirmwareContextReceipt use{};
            assert(!d.beginPrimaryStationUse(&n.in_ni,&use));
            // The still-live old peer can send its protected leave.
            assert(d.beginPrimaryStationUse(&n.in_ni,&use,false));
            d.primaryStationUses.close();
            ItlFirmwareContextReceipt rejected{};
            assert(!d.beginPrimaryStationUse(&n.in_ni,&rejected,false));
            d.endPrimaryStationUse(&use);
            ++cases;
        }
        {
            cleanFixture(); Driver d; Node n; prepare(d,n); assert(add(d,n)==0);
            ItlFirmwareContextReceipt use{};
            assert(d.beginPrimaryStationUse(&n.in_ni,&use));
            const auto copied=use;
            // Model the reset admission edge, NOT hardware/DMA reclamation.
            d.primaryStationUses.close(); d.primaryStationContext.clear();
            ++d.com.sc_generation; prepare(d,n);
            assert(add(d,n)==EBUSY);
            d.endPrimaryStationUse(&use);
            assert(add(d,n)==0);
            auto stale=copied;
            assert(!d.primaryStationUses.release(&stale));
            assert(d.primaryStationUses.active==0);
            ++cases;
        }
        {
            cleanFixture(); Driver d; Node n; prepare(d,n); assert(add(d,n)==0);
            {
                ItlFirmwareStationUseGuard<Driver,ieee80211_node> guard(&d,&n.in_ni);
                assert(guard.admitted() && d.primaryStationUses.active==1);
                assert(add(d,n,1)==0); // MODIFY does not invalidate an ADD-incarnation reader.
            }
            assert(d.primaryStationUses.active==0 && d.resumeChecks==1);
            ++cases;
        }
        if constexpr(!iwm) {
            cleanFixture(); Driver d; Node n; prepare(d,n); assert(add(d,n)==0);
            ItlFirmwareContextReceipt use{},rejected{};
            assert(d.beginPrimaryStationUse(&n.in_ni,&use) && d.com.taskActive==1);
            d.com.taskAdmission=false; // Actual stop must drain this reference before ring reset.
            assert(!d.beginPrimaryStationUse(&n.in_ni,&rejected));
            assert(d.com.taskActive==1);
            d.endPrimaryStationUse(&use);
            assert(d.com.taskActive==0);
            ++cases;
        }
        if(std::strcmp(selected,"users")==0) return;
    }
    if(std::strcmp(selected,"all")==0 || std::strcmp(selected,"retirement")==0) {
        for(bool replace : {false,true}) {
            cleanFixture(); Driver d; Node n; prepare(d,n); assert(add(d,n)==0);
            if(iwm) d.com.agg_queue_mask|=1U<<IWM_FIRST_AGG_TX_QUEUE;
            rejectRepeatedQueueRemoval=true; failEdge=Remove;
            assert(removeStation(d,static_cast<Node *>(nullptr))==ETIMEDOUT);
            if(replace) {
                ++d.com.sc_ic.identity.associationEpoch; ++n.in_id; ++n.in_macaddr[5];
                d.com.first_data_qid=7;
            }
            edges.clear(); failEdge=0;
            if constexpr(!iwm) {
                // A lost REMOVE_STA response is not permission to reissue
                // it or reuse firmware-owned DMA without physical recovery.
                assert(removeStation(d,static_cast<Node *>(nullptr))==EBUSY && edges.empty());
                assert(d.com.txq[4].firmware.owned && d.com.txq[4].retired==0);
                assert(d.txQueueAllocation.phase==ItlTxQueueAllocation::Phase::Quarantined);
                ++d.com.sc_generation; ++d.txQueueAllocation.lifecycle;
                d.txQueueAllocation.phase=ItlTxQueueAllocation::Phase::Idle;
                d.primaryStationContext.clear();
                for(auto &ring:d.com.txq) ring.firmware={};
                prepare(d,n); assert(add(d,n)==0);
                edges.clear();
            }
            assert(removeStation(d,static_cast<Node *>(nullptr))==0);
            if constexpr(iwm) assert(edges==std::vector<int>{Remove});
            else assert(edges==(std::vector<int>{DrainOn,Flush,DrainOff,Remove}));
            assert(!d.primaryStationContext.occupied()); ++cases;
        }
        if constexpr(iwm) {
            cleanFixture(); Driver d; Node n; prepare(d,n); assert(add(d,n)==0);
            d.com.agg_queue_mask|=3U<<IWM_FIRST_AGG_TX_QUEUE;
            rejectRepeatedQueueRemoval=true; failDisableCall=2;
            assert(removeStation(d,static_cast<Node *>(nullptr))==ETIMEDOUT);
            assert(removedQueues==std::vector<unsigned>{IWM_FIRST_AGG_TX_QUEUE});
            edges.clear(); failDisableCall=0;
            assert(removeStation(d,static_cast<Node *>(nullptr))==0);
            assert(edges==(std::vector<int>{DisableQueue,Remove}));
            assert(removedQueues==(std::vector<unsigned>{IWM_FIRST_AGG_TX_QUEUE,IWM_FIRST_AGG_TX_QUEUE+1}));
            ++cases;
        }
        {
            cleanFixture(); Driver d; Node n; prepare(d,n); assert(add(d,n)==0);
            failEdge=Remove;
            assert(removeStation(d,static_cast<Node *>(nullptr))==ETIMEDOUT);
            const auto stale=d.primaryStationContext.owner;
            ++d.com.sc_generation; d.primaryStationContext.clear(); prepare(d,n);
            failEdge=0; assert(add(d,n)==0);
            assert(!d.primaryStationRetirement.started);
            assert(!d.notePrimaryStationRetirement(stale,ItlFirmwareStationRetirement::Removed));
            ++cases;
        }
        if(std::strcmp(selected,"retirement")==0) return;
    }
    if(std::strcmp(selected,"owner_add")==0) {
        cleanFixture(); Driver d; Node n; prepare(d,n);
        d.primaryStationContext=d.primaryBindingContext;
        d.primaryStationContext.owner.identity.station=iwm?IWM_STATION_ID:IWX_STATION_ID;
        d.primaryStationContext.owner.identity.commandLength=sizeof(d.primaryStationCommand);
        d.com.sc_flags|=active;
        ++d.com.sc_ic.identity.associationEpoch;
        assert(add(d,n)==EBUSY && edges.empty());
        ++cases; return;
    }
    for(int mode : {IEEE80211_M_STA,IEEE80211_M_MONITOR})
    for(int update : {0,1})
    for(unsigned phy : {0U,1U,2U,4U})
    for(int failure : {0,1,2}) {
        cleanFixture(); Driver d; Node n; prepare(d,n,mode); n.in_ni.ni_flags=phy;
        if(update) assert(add(d,n)==0);
        edges.clear();
        if(failure==1) failEdge=Add;
        if(failure==2) statusEdge=Add;
        const auto oldMask=d.com.agg_queue_mask;
        const auto oldTid=d.com.agg_tid_disable;
        assert(add(d,n,update)==(failure==0?0:failure==1?ETIMEDOUT:EIO));
        assert((d.com.sc_flags&active)==((update||!failure)?active:0));
        if(failure) {
            assert(d.com.agg_queue_mask==oldMask && d.com.agg_tid_disable==oldTid);
            if(failure==1) {
                assert(d.primaryStationContext.stage==Lease::Stage::Uncertain);
                failEdge=0; assert(add(d,n,update)==EBUSY);
                assert(removeStation(d,static_cast<Node *>(nullptr))==0);
                assert(!d.primaryStationContext.occupied());
                assert(add(d,n)==0);
            } else {
                statusEdge=0; assert(add(d,n,update)==0);
            }
        }
        assert(d.primaryStationContext.stage==Lease::Stage::Active); ++cases;
    }
    for(int change : {0,1,2,3,4,5,6,7}) {
        cleanFixture(); Driver d; Node n; prepare(d,n); assert(add(d,n)==0);
        auto old=d.primaryStationContext.owner;
        if(change==0) ++d.com.sc_ic.identity.associationEpoch;
        if(change==1) ++n.in_macaddr[5];
        if(change==2) ++n.in_id;
        if(change==3) d.com.sc_ic.ic_opmode=IEEE80211_M_MONITOR;
        if(change==4) d.primaryBindingContext.stage=Lease::Stage::Uncertain;
        if(change==5) ++d.primaryBindingContext.owner.generation;
        if(change==6) ++d.primaryMacContext.owner.generation;
        if(change==7) d.primaryMacContext.clear();
        const auto count=edges.size();
        assert(add(d,n)==EBUSY && edges.size()==count);
        assert(d.primaryStationContext.owner.serial==old.serial); ++cases;
    }
    for(int edge : {Add,DrainOn,Flush,DrainOff,DisableQueue,Remove})
    for(int disturbance : {0,1,2}) {
        cleanFixture(); Driver d; Node n; prepare(d,n);
        if(edge!=Add) assert(add(d,n)==0);
        if(iwm && edge==DisableQueue) d.com.agg_queue_mask|=1U<<IWM_FIRST_AGG_TX_QUEUE;
        if(!iwm && edge==DisableQueue) commandVersion=3;
        edges.clear();
        if(disturbance==0) resetEdge=edge;
        if(disturbance==1) replaceEdge=edge;
        if(disturbance==2) failEdge=edge;
        int result=edge==Add ? add(d,n) : removeStation(d,&n);
        if(disturbance==0) {
            assert(result==ENXIO && edges.back()==edge);
            assert(d.com.sc_flags==unrelatedFlag && d.com.agg_queue_mask==0xface);
        } else if(disturbance==2) {
            assert(result==ETIMEDOUT && edges.back()==edge);
            assert(d.primaryStationContext.stage==Lease::Stage::Uncertain);
            if(!iwm && edge!=Add) assert(d.com.sc_flags&IWX_FLAG_TXFLUSH);
        } else {
            assert(result==0);
            if(edge!=Add) assert(!d.primaryStationContext.occupied());
        }
        ++cases;
    }
    for(int before : {0,1,2,3,4}) {
        cleanFixture(); Driver d; Node n; prepare(d,n);
        beforeSubmit=[&] {
            if(before==0) ++d.com.sc_ic.identity.associationEpoch;
            if(before==1) { ++d.com.sc_generation; d.primaryStationContext.clear(); }
            if(before==2) d.scanCommand.open=false;
            if(before==3) ++d.primaryStationContext.owner.serial;
            if(before==4) d.primaryStationContext.clear();
        };
        assert(add(d,n)==ENXIO && edges.empty());
        if(before==0 || before==2) assert(!d.primaryStationContext.occupied());
        ++cases;
    }
    for(int change : {0,1,2}) {
        cleanFixture(); Driver d; Node n; prepare(d,n); assert(add(d,n)==0);
        if(change==0) { ++n.in_id; ++n.in_color; ++n.in_macaddr[5]; }
        if(change==1) d.com.sc_ic.ic_opmode=IEEE80211_M_MONITOR;
        if(change==2) ++d.com.sc_ic.identity.associationEpoch;
        edges.clear();
        assert(removeStation(d,&n)==0 && lastStation==(iwm?IWM_STATION_ID:IWX_STATION_ID));
        assert(!d.primaryStationContext.occupied()); ++cases;
    }
    cleanFixture(); Driver d; Node n; prepare(d,n); assert(add(d,n)==0);
    edges.clear(); assert(add(d,n)==0 && edges.empty());
    afterSubmit=[&] { assert(add(d,n)==EBUSY); };
    assert(removeStation(d,&n)==0); ++cases;
}
int main(int argc,char **argv)
{
    const char *selected=argc>1?argv[1]:"all";
    const char *family=argc>2?argv[2]:"all";
    if(std::strcmp(family,"iwx")) familyTests<ItlIwm,iwm_node>(selected);
    if(std::strcmp(family,"iwm")) familyTests<ItlIwx,iwx_node>(selected);
    if(std::strcmp(family,"iwm") && std::strcmp(selected,"owner_add")) {
        for(int version : {0,3,IWX_FW_CMD_VER_UNKNOWN})
        for(bool flush : {false,true})
        for(unsigned reply : {0U,1U,2U})
        for(bool superseded : {false,true}) {
            cleanFixture(); ItlIwx d; iwx_node n; prepare(d,n); assert(add(d,n)==0);
            ItlFirmwareContextReceipt receipt{};
            assert(d.beginPrimaryStationCleanup(true,&receipt)==0);
            ItlFirmwareContextCommand command{receipt,ItlFirmwareContextCommand::Kind::Station,true,false};
            commandVersion=version; replyKind=reply;
            ItlTxQueueAllocationCommand transaction{};
            assert(d.beginTxQueueAllocation(&d.com,receipt.identity.station,IWX_MGMT_TID,-2,&transaction,true)==0);
            if(superseded) afterSubmit=[&] { ++d.primaryStationContext.owner.serial; };
            const int result=flush ?
                d.iwx_flush_sta_tids(&d.com,receipt.identity.station,0xffff,&command) :
                d.iwx_disable_txq(&d.com,receipt.identity.station,4,IWX_MGMT_TID,&command,&transaction);
            const bool wire=flush || version==3;
            assert(result==(wire?(superseded?ENXIO:reply?EIO:0):0));
            assert(packetOwners==0);
            assert(d.com.txq[4].retired==0); // No DMA reset before REMOVE_STA.
            if(!flush) assert(d.com.txq[4].firmware.owned && d.com.txq[4].firmware.closing);
            assert(reclaimed==(flush && result==0 ? 1U : 0U));
            d.finishTxQueueAllocation(&d.com,&transaction,false);
            ++cases;
        }
        for(int queue : {-1,IWX_DQA_CMD_QUEUE,64}) {
            cleanFixture(); ItlIwx d; iwx_node n; prepare(d,n); assert(add(d,n)==0);
            ItlFirmwareContextReceipt receipt{}; assert(d.beginPrimaryStationCleanup(true,&receipt)==0);
            ItlFirmwareContextCommand command{receipt,ItlFirmwareContextCommand::Kind::Station,true,false};
            edges.clear();
            ItlTxQueueAllocationCommand transaction{};
            assert(d.beginTxQueueAllocation(&d.com,receipt.identity.station,IWX_MGMT_TID,-2,&transaction,true)==0);
            assert(d.iwx_disable_txq(&d.com,receipt.identity.station,queue,IWX_MGMT_TID,&command,&transaction)==EINVAL);
            assert(!command.submitted && edges.empty()); ++cases;
            d.finishTxQueueAllocation(&d.com,&transaction,false);
        }
        for(int edge : {NoEdge,DrainOn,Flush,DrainOff})
        for(int disturbance : {0,1,2}) {
            cleanFixture(); ItlIwx d; iwx_node n; prepare(d,n); assert(add(d,n)==0);
            edges.clear();
            if(disturbance==0) failEdge=edge;
            if(disturbance==1) resetEdge=edge;
            if(disturbance==2) statusEdge=edge;
            if(disturbance==2 && edge==Flush) continue;
            int expected=edge==0?0:disturbance==0?ETIMEDOUT:disturbance==1?ENXIO:EIO;
            assert(d.iwx_flush_sta(&d.com,&n)==expected);
            assert((d.com.sc_flags&IWX_FLAG_TXFLUSH)==((expected && disturbance!=1)?IWX_FLAG_TXFLUSH:0));
            ++cases;
        }
        cleanFixture(); ItlIwx d; iwx_node n; prepare(d,n); assert(add(d,n)==0);
        n.in_ni.ni_tx_ba[3].ba_state=IEEE80211_BA_AGREED; baDevice=&d.com;
        replaceEdge=Remove;
        assert(d.iwx_rm_sta(&d.com,&n)==0);
        assert(std::find(edges.begin(),edges.end(),Delba)==edges.end()); ++cases;
    }
    std::printf("actual IWM/IWX STA ownership and completion: PASS (%u scenarios)\n",cases);
}
