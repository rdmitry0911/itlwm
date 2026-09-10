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
#include <set>
#include <vector>
#include <sys/types.h>
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
#define DEVNAME(sc) "fixture"
#define KASSERT(value,message) assert(value)
#define M_NOWAIT 1
#define M_ZERO 2
#define MCLBYTES 2048
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
static bool observeUnsafe;
static const char *scenario;
static void *malloc(size_t size,int,int) { assert(!leafDepth); return std::calloc(1,size); }
struct IOSimpleLock { bool held=false; };
static void IOSimpleLockLock(IOSimpleLock *lock) { assert(lock && !lock->held); lock->held=true; ++leafDepth; }
static void IOSimpleLockUnlock(IOSimpleLock *lock) { assert(lock && lock->held && leafDepth); lock->held=false; --leafDepth; }
struct iwx_dma_info { void *vaddr; bus_addr_t paddr; size_t size; };
struct iwx_tx_data { void *map; bus_addr_t cmd_paddr; };
struct iwx_tx_ring {
    iwx_dma_info desc_dma,cmd_dma,bc_tbl;
    iwx_tfh_tfd *desc;
    iwx_device_cmd *cmd;
    iwx_tx_data data[IWX_MIN_256_BA_QUEUE_SIZE_GEN3];
    unsigned ring_count,hi_mark,low_mark;
    bool ap_queue_full;
    int qid,queued,cur,tail;
};
struct iwx_softc {
    iwx_tx_ring sc_tvqm_ring{};
    iwx_tx_ring txq[16]{};
    IOSimpleLock locks[16];
    IOSimpleLock *sc_txq_locks[16];
    struct { int qid=IWX_INVALID_QUEUE; } sc_tid_data[8];
    void *sc_dmat=nullptr;
    unsigned qfullmsk=0;
    int sc_device_family=IWX_DEVICE_FAMILY_AX210;
    iwx_softc() {
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
static int iwx_ap_exchange_tx_ring_carrier(iwx_softc *,uint16_t,iwx_tx_ring *,iwx_tx_ring *);
struct ItlIwx {
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
        assert(!leafDepth && hcmd->id==IWX_SCD_QUEUE_CFG);
        assert(hcmd->len[0]==sizeof(iwx_tx_queue_cfg_cmd));
        const auto *wire=static_cast<const iwx_tx_queue_cfg_cmd *>(hcmd->data[0]);
        assert(le16toh(wire->flags)==IWX_TX_QUEUE_CFG_ENABLE_QUEUE);
        assert(wire->tid==3 && (wire->sta_id==IWX_STATION_ID || wire->sta_id==6));
        assert(le64toh(wire->tfdq_addr)==sc->sc_tvqm_ring.desc_dma.paddr);
        assert(le64toh(wire->byte_cnt_addr)==sc->sc_tvqm_ring.bc_tbl.paddr);
        firmwareMemory.insert(le64toh(wire->tfdq_addr));
        firmwareMemory.insert(le64toh(wire->byte_cnt_addr));
        ++sendCalls;
        if(std::strcmp(scenario,"transport")==0 || std::strcmp(scenario,"retry")==0)
            return ETIMEDOUT;
        hcmd->resp_pkt=static_cast<iwx_rx_packet *>(std::calloc(1,sizeof(iwx_rx_packet)+sizeof(iwx_tx_queue_cfg_rsp)));
        assert(hcmd->resp_pkt);
        responseLength=std::strcmp(scenario,"short")==0?0:sizeof(iwx_tx_queue_cfg_rsp);
        auto *rsp=reinterpret_cast<iwx_tx_queue_cfg_rsp *>(hcmd->resp_pkt->data);
        rsp->queue_number=htole16(std::strcmp(scenario,"preallocated")==0?1:5);
        rsp->write_pointer=htole16(7);
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
    ItlIwx d; iwx_softc sc;
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
              std::strcmp(scenario,"collision")==0) {
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
    // Only this explicit fixture hardware-reset boundary retires all DMA.
    firmwareMemory.clear();
    d.iwx_free_tx_ring(&sc,&sc.sc_tvqm_ring);
    for(auto &ring:sc.txq) d.iwx_free_tx_ring(&sc,&ring);
    assert(!leafDepth && !allocations && !maps);
    std::printf("actual IWX TVQM allocation: %s PASS\n",scenario);
}
