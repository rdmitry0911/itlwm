/* Complete production TVQM allocator, fallback loop, ring initialization and
 * carrier exchange. Real firmware wire headers; DMA, transport, interrupt
 * exclusion and actual hardware reset are explicit fixture boundaries. */
#include "scan_test_byte_order.hpp"
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <vector>
#include <sys/types.h>
#include <HAL/ItlTxQueueAllocation.hpp>
using std::min;
using u8=uint8_t; using u16=uint16_t; using u32=uint32_t; using u64=uint64_t;
using s8=int8_t; using s16=int16_t; using s32=int32_t;
using __le16=uint16_t; using __le32=uint32_t; using __le64=uint64_t; using __be16=uint16_t;
using bus_addr_t=uint64_t; using bus_size_t=size_t;
#define __packed __attribute__((packed))
#define __aligned(x) __attribute__((aligned(x)))
#ifndef NBBY
#define NBBY 8
#endif
#ifndef howmany
#define howmany(x,y) (((x)+(y)-1)/(y))
#endif
#define ETHER_ADDR_LEN 6
#define BIT(x) (1U<<(x))
#define __BIT(x) BIT(x)
#define le32_to_cpup(x) le32toh(*(x))
#define cpu_to_le16(x) htole16(x)
#define cpu_to_le32(x) htole32(x)
#define nitems(a) (sizeof(a)/sizeof((a)[0]))
#define ARRAY_SIZE(a) nitems(a)
#define XYLog(...) ((void)0)
#define DPRINTF(...) ((void)0)
#define DEVNAME(sc) "fixture"
#define KASSERT(value,message) assert(value)
#define M_NOWAIT 1
#define M_ZERO 2
#define MCLBYTES 2048
#define PAGE_SIZE 4096
#define BUS_DMA_NOWAIT 1
#define _fls(x) ((x)==0?0:32-__builtin_clz(static_cast<unsigned>(x)))
#define flsl(x) ((x)==0?0:static_cast<int>(sizeof(unsigned long)*8)-__builtin_clzl(static_cast<unsigned long>(x)))
#include "defines.inc"
#include "itlwm/hal_iwm/if_iwmreg.h"
#include "itlwm/hal_iwx/if_iwxreg.h"

static unsigned leafDepth,allocations,maps,sendCalls,unsafeResets,unsafeFrees;
static unsigned dmaFailureAt,allocationCalls;
static uint64_t nextAddress=0x100000;
static std::set<uint64_t> firmwareMemory;
static std::map<uint64_t,unsigned> firmwareStations;
static bool observeUnsafe;
static const char *scenario;
static bool is(const char *name) { return std::strcmp(scenario,name)==0; }
static std::function<void()> beforeSubmit,afterSubmit;
static unsigned nextQueue=5,removeCalls,flushCalls,removeQueueCalls,failRetireAt;
static int retirementFailure;
static void *malloc(size_t size,int,int) { assert(!leafDepth); return std::calloc(1,size); }
struct IOSimpleLock { bool held=false; };
using IOInterruptState=unsigned;
using IOLock=IOSimpleLock;
static void IOLockLock(IOLock *lock) { assert(!leafDepth && lock && !lock->held); lock->held=true; }
static void IOLockUnlock(IOLock *lock) { assert(!leafDepth && lock && lock->held); lock->held=false; }
static void IOSimpleLockLock(IOSimpleLock *lock) { assert(lock && !lock->held); lock->held=true; ++leafDepth; }
static void IOSimpleLockUnlock(IOSimpleLock *lock) { assert(lock && lock->held && leafDepth); lock->held=false; --leafDepth; }
static IOInterruptState IOSimpleLockLockDisableInterrupt(IOSimpleLock *lock) {
    auto prior=leafDepth; IOSimpleLockLock(lock); return prior;
}
static void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *lock,IOInterruptState prior) {
    IOSimpleLockUnlock(lock); assert(leafDepth==prior);
}
struct iwx_dma_info { void *vaddr; bus_addr_t paddr; size_t size; };
struct iwx_tx_data { void *map; bus_addr_t cmd_paddr; };
struct iwx_tx_ring {
    ItlTxQueueFirmwareOwner firmware;
    iwx_dma_info desc_dma,cmd_dma,bc_tbl;
    iwx_tfh_tfd *desc;
    iwx_device_cmd *cmd;
    iwx_tx_data data[IWX_MIN_256_BA_QUEUE_SIZE_GEN3];
    unsigned ring_count,hi_mark,low_mark;
    bool ap_queue_full;
    int qid,queued,cur,tail;
};
struct iwx_softc {
    IOSimpleLock selectedLock,taskLock;
    IOLock *sc_task_gate_lock=&taskLock;
    unsigned sc_task_gate_active=0,sc_task_gate_init_refs=0,sc_task_gate_stop_refs=0;
    bool sc_task_gate_closed=false,sc_task_gate_detaching=false;
    uint32_t sc_flags=0,sc_generation=7;
    struct {
        IOSimpleLock *ic_pae_selected_bss_lock=nullptr;
        ItlStateTransitionIdentity identity{11,11,12};
    } sc_ic;
    int init_task=0;
    iwx_tx_ring sc_tvqm_ring{};
    iwx_tx_ring txq[16]{};
    IOSimpleLock locks[16];
    IOSimpleLock *sc_txq_locks[16];
    struct { int qid=IWX_INVALID_QUEUE; } sc_tid_data[9];
    void *sc_dmat=nullptr;
    unsigned qfullmsk=0;
    int sc_device_family=IWX_DEVICE_FAMILY_AX210;
    iwx_softc() {
        sc_ic.ic_pae_selected_bss_lock=&selectedLock;
        for(unsigned i=0;i<16;++i) {
            sc_txq_locks[i]=&locks[i]; txq[i].qid=IWX_INVALID_QUEUE;
        }
        sc_tvqm_ring.qid=IWX_INVALID_QUEUE;
    }
};
static int bus_dmamap_create(void *,size_t,int,size_t,int,int,void **map)
{ assert(!leafDepth); *map=std::malloc(1); assert(*map); ++maps; return 0; }
static size_t responseLength;
static size_t iwx_rx_packet_payload_len(iwx_rx_packet *) { return responseLength; }
static size_t iwx_rx_packet_len(iwx_rx_packet *packet) { return sizeof(packet->hdr)+responseLength; }
static int iwx_ap_exchange_tx_ring_carrier(iwx_softc *,uint16_t,iwx_tx_ring *,iwx_tx_ring *);
static int systq;
struct ItlScanCommandPolicy {
    template<class Ic> static ItlStateTransitionIdentity identityLocked(Ic *ic) {
        assert(ic->ic_pae_selected_bss_lock->held); return ic->identity;
    }
};
struct ItlIwx {
    iwx_softc com;
    IOSimpleLock scanLock;
    IOSimpleLock *wclScanLock=&scanLock;
    ItlTxQueueAllocation txQueueAllocation{};
    ItlFirmwareContextLease primaryStationContext{};
    ItlFirmwareStationUses primaryStationUses{};
    ItlFirmwareStationRetirement primaryStationRetirement{};
    struct { bool open=true; } scanCommand;
    unsigned recoveries=0;
    int version=0;
    ItlIwx() {
        auto &station=primaryStationContext.owner;
        station.serial=17; station.generation=com.sc_generation;
        station.identity.station=IWX_STATION_ID; station.identity.attempt=com.sc_ic.identity;
        primaryStationContext.confirmed=true;
        primaryStationContext.stage=ItlFirmwareContextLease::Stage::Active;
        assert(primaryStationUses.start(station));
    }
    int beginTxQueueAllocation(iwx_softc *,uint8_t,uint8_t,int,ItlTxQueueAllocationCommand *,bool=false);
    bool txQueueAllocationCurrentLocked(const ItlTxQueueAllocationCommand &) const;
    void finishTxQueueAllocation(iwx_softc *,ItlTxQueueAllocationCommand *,bool);
    void resetTxQueueAllocation(iwx_softc *);
    int iwx_allocate_tx_queue(iwx_softc *,uint8_t,int,int,uint32_t,int);
    int iwx_tvqm_allocate_memory(iwx_softc *,iwx_tx_ring *,uint32_t);
    int iwx_enable_txq(iwx_softc *,int,int,int,int);
    int iwx_disable_txq(iwx_softc *,int,int,uint8_t,ItlFirmwareContextCommand *,ItlTxQueueAllocationCommand *);
    int iwx_retire_station_tx_queues(iwx_softc *,uint8_t,ItlFirmwareContextCommand *,bool);
    int iwx_ap_remove_internal_sta(iwx_softc *,uint8_t,uint16_t);
    int iwx_flush_sta_tids(iwx_softc *,int,uint16_t,ItlFirmwareContextCommand *,ItlTxQueueAllocationCommand * = nullptr);
    void iwx_ampdu_txq_advance(iwx_softc *,iwx_tx_ring *,int) { assert(!leafDepth); }
    bool primaryStationCleanupCurrent(const ItlFirmwareContextReceipt &receipt) const {
        return primaryStationContext.commandCurrent(receipt.serial,com.sc_generation) &&
            primaryStationContext.owner.identity.equals(receipt.identity);
    }
    int iwx_lookup_cmd_ver(iwx_softc *,int,int) { return version; }
    bool releasePrimaryStationReader(ItlFirmwareContextReceipt *use) {
        assert(!leafDepth); return primaryStationUses.release(use);
    }
    void iwx_task_gate_leave(iwx_softc *sc) { assert(!leafDepth && sc->sc_task_gate_active); --sc->sc_task_gate_active; }
    void iwx_add_task(iwx_softc *,int,int *) { assert(!leafDepth); ++recoveries; }
    int iwx_tvqm_alloc_txq(iwx_softc *,int,int);
    int iwx_tvqm_enable_txq(iwx_softc *,int,int,uint32_t);
    int iwx_tvqm_enable_txq_for_sta(iwx_softc *,uint8_t,int,int,uint32_t);
    void iwx_tx_ring_init(iwx_softc *,iwx_tx_ring *,int);
    int iwx_alloc_tx_ring(iwx_softc *,iwx_tx_ring *,int);
    int iwx_dma_contig_alloc(void *,iwx_dma_info *dma,size_t size,int) {
        assert(!leafDepth);
        if(++allocationCalls==dmaFailureAt) return ENOMEM;
        dma->vaddr=std::calloc(1,size); assert(dma->vaddr);
        dma->size=size; dma->paddr=nextAddress; nextAddress+=0x100000;
        ++allocations; return 0;
    }
    void iwx_reset_tx_ring(iwx_softc *,iwx_tx_ring *ring) {
        assert(!leafDepth);
        if(firmwareMemory.count(ring->desc_dma.paddr)) {
            ++unsafeResets;
            assert(observeUnsafe && "reset touched a still firmware-owned TVQM carrier");
        }
    }
    void iwx_free_tx_ring(iwx_softc *,iwx_tx_ring *ring) {
        assert(!leafDepth);
        for(auto *dma : {&ring->desc_dma,&ring->cmd_dma,&ring->bc_tbl}) {
            if(!dma->vaddr) continue;
            if(firmwareMemory.count(dma->paddr)) {
                ++unsafeFrees;
                assert(observeUnsafe && "freed DMA without confirmed queue retirement or hardware reset");
            }
            std::free(dma->vaddr); *dma={}; assert(allocations); --allocations;
        }
        for(auto &data:ring->data) if(data.map) {
            std::free(data.map); data.map=nullptr; assert(maps); --maps;
        }
        ring->ring_count=0; ring->qid=IWX_INVALID_QUEUE;
    }
    int iwx_send_cmd(iwx_softc *sc,iwx_host_cmd *hcmd) {
        assert(!leafDepth);
        if(beforeSubmit) { auto hook=beforeSubmit; beforeSubmit={}; hook(); }
        if(hcmd->queue_allocation!=nullptr) {
            auto &command=*hcmd->queue_allocation;
            const bool live=command.primary && !command.retirement;
            if(live) IOSimpleLockLock(&sc->selectedLock);
            IOSimpleLockLock(wclScanLock);
            const bool current=txQueueAllocationCurrentLocked(command);
            if(current) command.submitted=true;
            IOSimpleLockUnlock(wclScanLock);
            if(live) IOSimpleLockUnlock(&sc->selectedLock);
            if(!current) return ENXIO;
        }
        if(hcmd->context_command) hcmd->context_command->submitted=true;
        ++sendCalls;
        const bool retirement=hcmd->queue_allocation && hcmd->queue_allocation->retirement;
        unsigned station=0;
        if(retirement) {
            assert(sc->sc_task_gate_active==1 && txQueueAllocation.phase==ItlTxQueueAllocation::Phase::Building);
            station=hcmd->queue_allocation->station;
            for(auto &ring:sc->txq) if(ring.firmware.owned && ring.firmware.station==station)
                assert(ring.firmware.closing);
            if(hcmd->id==IWX_REMOVE_STA) {
                assert(hcmd->len[0]==sizeof(struct iwx_rm_sta_cmd));
                const auto &wire=*static_cast<const struct iwx_rm_sta_cmd *>(hcmd->data[0]);
                assert(wire.sta_id==station); ++removeCalls;
            } else if(hcmd->id==IWX_TXPATH_FLUSH) {
                const auto &wire=*static_cast<const iwx_tx_path_flush_cmd *>(hcmd->data[0]);
                assert(le32toh(wire.sta_id)==station && le16toh(wire.tid_mask)==0xffff); ++flushCalls;
            } else {
                assert(version==3 && hcmd->id==IWX_WIDE_ID(IWX_DATA_PATH_GROUP,IWX_SCD_QUEUE_CONFIG_CMD));
                const auto &wire=*static_cast<const iwx_scd_queue_cfg_cmd *>(hcmd->data[0]);
                assert(le32toh(wire.operation)==IWX_SCD_QUEUE_REMOVE);
                assert(le32toh(wire.u.remove.sta_mask)==(1U<<station));
                ++removeQueueCalls;
            }
            if(afterSubmit) { auto hook=afterSubmit; afterSubmit={}; hook(); }
            if(failRetireAt==flushCalls+removeQueueCalls+removeCalls && retirementFailure==1)
                return ETIMEDOUT;
        } else {
            uint64_t desc=0,bc=0;
            uint32_t size=0;
            unsigned tid=0;
            // The modern SCD_QUEUE_CONFIG_CMD is used only when the firmware
            // advertises version 3.  Runtime on real AX211 proved that firmware
            // advertising NO version (UNKNOWN) does not implement the modern
            // command (UMAC BAD_COMMAND assert); like iwlwifi's cmd_ver 0, both
            // UNKNOWN and explicit 0 select the legacy IWX_SCD_QUEUE_CFG.
            if(version==3) {
                assert(hcmd->id==IWX_WIDE_ID(IWX_DATA_PATH_GROUP,IWX_SCD_QUEUE_CONFIG_CMD));
                const auto &wire=*static_cast<const iwx_scd_queue_cfg_cmd *>(hcmd->data[0]);
                assert(le32toh(wire.operation)==IWX_SCD_QUEUE_ADD);
                station=__builtin_ctz(le32toh(wire.u.add.sta_mask)); tid=wire.u.add.tid;
                desc=le64toh(wire.u.add.tfdq_dram_addr); bc=le64toh(wire.u.add.bc_dram_addr);
                size=8U<<le32toh(wire.u.add.cb_size);
            } else {
                assert(hcmd->id==IWX_SCD_QUEUE_CFG && hcmd->len[0]==sizeof(iwx_tx_queue_cfg_cmd));
                const auto &wire=*static_cast<const iwx_tx_queue_cfg_cmd *>(hcmd->data[0]);
                assert(le16toh(wire.flags)==IWX_TX_QUEUE_CFG_ENABLE_QUEUE);
                station=wire.sta_id; tid=wire.tid;
                desc=le64toh(wire.tfdq_addr); bc=le64toh(wire.byte_cnt_addr);
                size=8U<<le32toh(wire.cb_size);
            }
            auto *command=hcmd->queue_allocation;
            const auto &ring=command && command->queue>=0 ? sc->txq[command->queue] : sc->sc_tvqm_ring;
            assert(desc==ring.desc_dma.paddr && bc==ring.bc_tbl.paddr && size==ring.ring_count);
            assert(!command || (station==command->station && tid==command->tid));
            firmwareMemory.insert(desc); firmwareMemory.insert(bc);
            firmwareStations[desc]=station; firmwareStations[bc]=station;
            if(afterSubmit) { auto hook=afterSubmit; afterSubmit={}; hook(); }
        }
        if(!retirement && (is("transport") || is("retry")))
            return ETIMEDOUT;
        if(is("missing") || (retirement && retirementFailure==2 &&
            failRetireAt==flushCalls+removeQueueCalls+removeCalls)) return 0;
        hcmd->resp_pkt=static_cast<iwx_rx_packet *>(std::calloc(1,sizeof(iwx_rx_packet)+sizeof(iwx_tx_queue_cfg_rsp)));
        assert(hcmd->resp_pkt);
        if(retirement) {
            responseLength=0;
            const bool failed=retirementFailure==3 && failRetireAt==flushCalls+removeQueueCalls+removeCalls;
            if(failed) hcmd->resp_pkt->hdr.group_id=IWX_CMD_FAILED_MSK;
            if(!failed && hcmd->id==IWX_REMOVE_STA)
                for(const auto &entry:firmwareStations) if(entry.second==station) firmwareMemory.erase(entry.first);
            return 0;
        }
        responseLength=is("short")?0:sizeof(iwx_tx_queue_cfg_rsp);
        auto *rsp=reinterpret_cast<iwx_tx_queue_cfg_rsp *>(hcmd->resp_pkt->data);
        const bool fixed=hcmd->queue_allocation && hcmd->queue_allocation->queue>=0;
        rsp->queue_number=htole16(fixed ? hcmd->queue_allocation->queue :
            is("preallocated")?1:is("collision")?5:nextQueue++);
        rsp->write_pointer=htole16(fixed?0:7);
        if(is("flags")) rsp->flags=htole16(1);
        if(is("bad-id")) rsp->queue_number=htole16(0);
        return 0;
    }
    void iwx_free_resp(iwx_softc *,iwx_host_cmd *cmd) {
        assert(!leafDepth); std::free(cmd->resp_pkt); cmd->resp_pkt=nullptr;
    }
};
#include "production.inc"

int main(int argc,char **argv)
{
    scenario=argc>1?argv[1]:"control";
    observeUnsafe=std::strcmp(scenario,"retry")==0;
    if(std::strcmp(scenario,"local-dma")==0) dmaFailureAt=1;
    ItlIwx d; auto &sc=d.com;
    if(argc>2) d.version=std::atoi(argv[2]);
    auto cleanup=[&] {
        firmwareMemory.clear();
        d.resetTxQueueAllocation(&sc);
        d.iwx_free_tx_ring(&sc,&sc.sc_tvqm_ring);
        for(auto &ring:sc.txq) d.iwx_free_tx_ring(&sc,&ring);
        assert(!leafDepth && !allocations && !maps && !sc.sc_task_gate_active && !d.primaryStationUses.active);
    };
    const bool retirement=std::strncmp(scenario,"retire-",7)==0;
    if(retirement) {
        // Populate one primary and two AP queues; station retirement must
        // not touch an adjacent station, including an identical TID.
        assert(d.iwx_tvqm_alloc_txq(&sc,3,0)==5);
        assert(d.iwx_tvqm_enable_txq_for_sta(&sc,6,3,0,256)==6);
        assert(d.iwx_tvqm_enable_txq_for_sta(&sc,6,4,0,256)==7);
        const bool primary=is("retire-primary") || is("retire-fixed");
        const unsigned station=primary?IWX_STATION_ID:6;
        if(is("retire-fixed")) {
            assert(d.iwx_alloc_tx_ring(&sc,&sc.txq[2],2)==0);
            assert(d.iwx_enable_txq(&sc,IWX_STATION_ID,2,IWX_MGMT_TID,sc.txq[2].ring_count)==0);
        }
        ItlFirmwareContextCommand context{};
        if(primary) {
            d.primaryStationUses.close();
            assert(d.primaryStationContext.begin(ItlFirmwareContextLease::Operation::Remove,
                sc.sc_generation,d.primaryStationContext.owner.identity,&context.receipt)==ItlFirmwareContextLease::Admission::Submit);
            context.cleanup=true; context.kind=ItlFirmwareContextCommand::Kind::Station;
        }
        if(is("retire-timeout")) retirementFailure=1;
        if(is("retire-missing")) retirementFailure=2;
        if(is("retire-failed")) retirementFailure=3;
        failRetireAt=1+(d.version==3?2:0)+1; // REMOVE_STA, after successful flush/removals.
        if(is("retire-flush")) { retirementFailure=1; failRetireAt=1; }
        if(is("retire-queue")) { retirementFailure=1; failRetireAt=2; }
        if(is("retire-reset")) afterSubmit=[&] {
            firmwareMemory.clear(); ++sc.sc_generation; d.resetTxQueueAllocation(&sc);
        };
        if(is("retire-reentrant")) beforeSubmit=[&] {
            auto before=sendCalls;
            assert(d.iwx_tvqm_enable_txq_for_sta(&sc,7,3,0,256)==-EBUSY);
            assert(sendCalls==before && sc.sc_task_gate_active==1);
        };
        const auto primaryAddress=sc.txq[5].desc_dma.paddr;
        const auto apAddress=sc.txq[6].desc_dma.paddr;
        const int result=primary ? d.iwx_retire_station_tx_queues(&sc,station,&context,false) :
            d.iwx_ap_remove_internal_sta(&sc,station,6);
        if(retirementFailure || is("retire-reset")) {
            assert(result!=0 && sc.txq[6].desc_dma.paddr==apAddress && allocations>=9);
            if(!is("retire-reset")) {
                assert(d.txQueueAllocation.phase==ItlTxQueueAllocation::Phase::Quarantined);
                assert(d.recoveries==1 && sc.txq[6].firmware.owned && sc.txq[6].firmware.closing);
                assert(firmwareMemory.count(apAddress));
                assert(d.iwx_tvqm_enable_txq_for_sta(&sc,7,3,0,256)==-EBUSY);
            }
        } else {
            assert(result==0 && removeCalls==1 && flushCalls==1);
            assert(removeQueueCalls==(d.version==3?(primary?(is("retire-fixed")?2:1):2):0));
            if(primary) {
                assert(sc.txq[5].desc_dma.paddr==primaryAddress && !sc.txq[5].firmware.owned);
                assert(sc.sc_tid_data[3].qid==IWX_INVALID_QUEUE && sc.txq[6].desc_dma.paddr==apAddress);
                assert(firmwareMemory.count(apAddress) && !firmwareMemory.count(primaryAddress));
                if(is("retire-fixed")) {
                    assert(sc.txq[2].ring_count!=0 && !sc.txq[2].firmware.owned);
                    assert(d.primaryStationContext.finish(context.receipt,sc.sc_generation,
                        ItlFirmwareContextLease::Completion::Success));
                    auto owner=context.receipt; ++owner.serial;
                    d.primaryStationContext.owner=owner;
                    d.primaryStationContext.confirmed=true;
                    d.primaryStationContext.stage=ItlFirmwareContextLease::Stage::Active;
                    assert(d.primaryStationUses.start(owner));
                    assert(d.iwx_enable_txq(&sc,IWX_STATION_ID,2,IWX_MGMT_TID,sc.txq[2].ring_count)==0);
                }
            } else {
                assert(sc.txq[6].ring_count==0 && sc.txq[7].ring_count==0 && allocations==3);
                assert(sc.txq[5].desc_dma.paddr==primaryAddress && firmwareMemory.count(primaryAddress));
                const auto sent=sendCalls;
                assert(d.iwx_ap_remove_internal_sta(&sc,station,6)==ENOENT && sent==sendCalls);
            }
        }
        assert(!unsafeResets && !unsafeFrees);
        cleanup();
        std::printf("actual IWX TVQM retirement: %s v%d PASS\n",scenario,d.version);
        return 0;
    }
    if(is("fixed") || is("aux")) {
        assert(d.iwx_alloc_tx_ring(&sc,&sc.txq[2],2)==0);
        if(is("aux")) {
            sc.sc_task_gate_closed=true; sc.sc_task_gate_init_refs=1;
            d.scanCommand.open=false;
        }
        const unsigned station=is("aux")?IWX_AUX_STA_ID:IWX_STATION_ID;
        assert(d.iwx_enable_txq(&sc,station,2,IWX_MGMT_TID,sc.txq[2].ring_count)==0);
        auto before=sendCalls;
        assert(d.iwx_enable_txq(&sc,station,2,IWX_MGMT_TID,sc.txq[2].ring_count)==EBUSY && sendCalls==before);
        assert(sc.txq[2].firmware.owned && !d.recoveries);
        cleanup();
        std::printf("actual IWX fixed queue: %s v%d PASS\n",scenario,d.version);
        return 0;
    }
    if(is("stale-context")) --d.primaryStationContext.owner.generation;
    if(is("closed")) d.primaryStationUses.close();
    if(is("pre-reset")) beforeSubmit=[&] {
        ++sc.sc_generation; d.resetTxQueueAllocation(&sc);
    };
    if(is("post-reset")) afterSubmit=[&] {
        firmwareMemory.clear(); ++sc.sc_generation; d.resetTxQueueAllocation(&sc);
    };
    if(is("superseded")) afterSubmit=[&] { ++sc.sc_ic.identity.joinSequence; };
    if(is("reentrant")) beforeSubmit=[&] {
        assert(d.iwx_tvqm_enable_txq_for_sta(&sc,6,3,0,256)==-EBUSY);
        assert(sc.sc_task_gate_active==1 && d.primaryStationUses.active==1);
    };
    const int expectedQueue=std::strcmp(scenario,"preallocated")==0?1:5;
    if(expectedQueue==1) {
        // Attach preallocates q0/q1/q2 storage. An unused carrier is not a
        // firmware-owned queue; preserve this legal first dynamic allocation.
        assert(d.iwx_alloc_tx_ring(&sc,&sc.txq[1],1)==0);
        assert(allocations==3 && firmwareMemory.empty());
    }
    const bool ap=std::strcmp(scenario,"ap-control")==0;
    int queue=ap?d.iwx_tvqm_enable_txq_for_sta(&sc,6,3,0x345,IWX_DEFAULT_QUEUE_SIZE):
        d.iwx_tvqm_alloc_txq(&sc,3,0x345);
    if(std::strcmp(scenario,"retry")==0) {
        std::printf("submitted commands=%u unsafe resets=%u unsafe DMA frees=%u\n",sendCalls,unsafeResets,unsafeFrees);
        std::fflush(stdout);
        assert(sendCalls==1 && "size fallback resubmitted a firmware-accepted transaction");
    } else if(std::strcmp(scenario,"control")==0 || std::strcmp(scenario,"local-dma")==0 ||
              std::strcmp(scenario,"preallocated")==0 || ap ||
              std::strcmp(scenario,"collision")==0 || is("reentrant") || is("flags")) {
        // "flags": the firmware set response->flags=0x1 but still returned a
        // usable queue.  Matching the OpenBSD reference, flags is not treated as
        // fatal (validate queue_number/write_pointer only), so the queue is
        // accepted exactly like the control path.
        assert(queue==expectedQueue && sendCalls==1);
        assert(sc.sc_tid_data[3].qid==(ap?IWX_INVALID_QUEUE:expectedQueue));
        assert(sc.txq[expectedQueue].cur==7 && allocations==3 && maps==sc.txq[expectedQueue].ring_count);
        assert(sc.sc_tvqm_ring.ring_count==0 && !sc.sc_tvqm_ring.desc_dma.vaddr);
        if(std::strcmp(scenario,"local-dma")==0) assert(sc.txq[5].ring_count==IWX_DEFAULT_QUEUE_SIZE/2);
        if(std::strcmp(scenario,"collision")==0) {
            const auto oldAddress=sc.txq[5].desc_dma.paddr;
            queue=d.iwx_tvqm_enable_txq_for_sta(&sc,6,3,0x456,IWX_DEFAULT_QUEUE_SIZE);
            assert(queue<0 && sc.txq[5].desc_dma.paddr==oldAddress);
        }
    } else assert(queue<0);
    if(is("transport") || is("short") || is("retry") || is("missing") ||
        is("bad-id") || is("superseded") || is("collision")) {
        assert(d.txQueueAllocation.phase==ItlTxQueueAllocation::Phase::Quarantined);
        assert(d.recoveries==1 && allocations==(is("collision")?6U:3U));
        auto before=sendCalls;
        assert(d.iwx_tvqm_enable_txq_for_sta(&sc,6,3,0,256)==-EBUSY && sendCalls==before);
    }
    assert(!unsafeResets && !unsafeFrees);
    cleanup();
    std::printf("actual IWX TVQM allocation: %s PASS\n",scenario);
}
