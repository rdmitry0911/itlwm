// Reuse the complete command-ring/lock fixture, including its original cases.
#define main iwn_abort_fixture_original_main
#include "iwn_scan_abort_owner_test.cpp"
#undef main

constexpr uint16_t IEEE80211_CHAN_2GHZ=0x80;
static unsigned submitMode, retrySubmissions, dmaReads;
static void iwn_scan_schedule_fatal_recovery(iwn_softc *) { ++resets; }
static int iwn_scan_submit(iwn_softc *sc,uint16_t band,int background,
    uint64_t serial,bool prepare,bool publish,bool wcl,uint64_t generation,
    uint32_t backend,bool *attempted,bool *prepared)
{
    assert(band==IEEE80211_CHAN_2GHZ && background==1);
    assert(!prepare && !publish && !wcl && !generation && !backend && !prepared);
    assert(!held && sc->sc_scan_lease.phase==IWN_SCAN_LEASE_ARMING);
    iwn_scan_lease_terminal premature{};
    assert(!iwn_scan_lease_claim_terminal(sc,&premature));
    ++retrySubmissions;
    *attempted=false;
    if(submitMode==1) return EIO;
    if(submitMode==2) { active(sc,serial+1,32); return EIO; }
    if(submitMode==3) {
        sc->sc_scan_lease.command_submitted=true;
        sc->sc_scan_lease.phase=IWN_SCAN_LEASE_ACTIVE;
        *attempted=true; return EIO;
    }
    auto *driver=static_cast<ItlIwn *>(sc->driver);
    iwn_scan_doorbell_context context{};
    context.serial=serial;
    context.reassoc_serial=sc->sc_scan_lease.reassoc_serial;
    context.background=true;
    context.passive_retry_channel=iwn_scan_lease_passive_retry_channel(sc,serial);
    assert(context.passive_retry_channel>=1 && context.passive_retry_channel<=14);
    // The real builder sees this same EEPROM-passive channel on the retry;
    // the doorbell must not use it to seed another pass.
    context.passive_retry_candidates=uint16_t(1U<<context.passive_retry_channel);
    const int result=driver->iwn_cmd_with_doorbell_hook(sc,IWN_CMD_SCAN,nullptr,0,1,
        iwn_scan_lease_prepare_doorbell,iwn_scan_lease_finish_doorbell,&context);
    *attempted=context.committed;
    return result;
}
#include "retry-wrapper.inc"

struct iwn_rx_desc { uint32_t len; uint8_t type,flags,idx,qid; };
struct iwn_rx_data { Map *map; };
constexpr unsigned IWN_SCAN_RESULTS=131,IWN_RX_DESC_LEN_MASK=0x3fff;
constexpr unsigned BUS_DMASYNC_POSTREAD=1;
#define __packed __attribute__((packed))
#include "retry-receipt-type.inc"
static_assert(sizeof(iwn_scan_results)==16 && sizeof(iwn_rx_desc)==8,"firmware ABI");
static void bus_dmamap_sync(void *,Map *,size_t offset,size_t size,unsigned operation)
{
    assert(offset==8 && size==16 && operation==BUS_DMASYNC_POSTREAD);
    ++dmaReads;
}
static void dispatch_result(iwn_softc *sc,iwn_rx_desc *desc,iwn_rx_data *data)
{
    switch(desc->type) {
#include "retry-receipt-case.inc"
    }
}

static void seed(ItlIwn &driver,uint16_t mask)
{
    writes=clears=resets=retrySubmissions=dmaReads=submitMode=0;
    wakeError=0; onWake={};
    auto *sc=&driver.com;
    active(sc,UINT64_C(0x100000005),31);
    sc->sc_scan_lease.phase=IWN_SCAN_LEASE_ARMING;
    sc->sc_scan_lease.command_submitted=false;
    iwn_scan_doorbell_context context{};
    context.serial=sc->sc_scan_lease.serial;
    context.reassoc_serial=31; context.background=true;
    context.passive_retry_candidates=mask;
    assert(driver.iwn_cmd_with_doorbell_hook(sc,IWN_CMD_SCAN,nullptr,0,1,
        iwn_scan_lease_prepare_doorbell,iwn_scan_lease_finish_doorbell,&context)==0);
    assert(writes==1 && !held);
}

int main()
{
    assert(iwn_abort_fixture_original_main()==0);
    unsigned cases=0;
    // All representable input masks: finite exact population, no reseeding.
    for(unsigned mask=0;mask<=0xffff;++mask) {
        ItlIwn driver; seed(driver,uint16_t(mask)); auto *sc=&driver.com;
        assert(sc->sc_scan_lease.passive_retry_pending==(mask&0x7ffe));
        unsigned visited=0,previous=0;
        while(iwn_scan_retry_passive_2ghz(sc)) {
            const auto channel=sc->sc_scan_lease.passive_retry_channel;
            assert(channel>previous && channel<=14 && (mask&(1U<<channel)));
            previous=channel;
            assert(++visited<=14 && !held);
            assert(sc->sc_scan_lease.phase==IWN_SCAN_LEASE_ACTIVE);
            const auto pending=sc->sc_scan_lease.passive_retry_pending;
            // A result from a recovery visit cannot alter remaining work.
            iwn_scan_lease_note_passive_result(sc,14,1,99);
            assert(sc->sc_scan_lease.passive_retry_pending==pending);
        }
        assert(visited==unsigned(__builtin_popcount(mask&0x7ffe)));
        assert(retrySubmissions==visited && writes==visited+1 && resets==0);
        assert(!sc->sc_scan_lease.passive_retry_enabled);
        assert(!sc->sc_scan_lease.passive_retry_channel);
        iwn_scan_lease_terminal terminal{};
        assert(iwn_scan_lease_claim_terminal(sc,&terminal));
        assert(terminal.serial==UINT64_C(0x100000005) && terminal.reassoc_serial==31);
        assert(!terminal.aborted && !terminal.wcl && !terminal.standard);
        assert(!iwn_scan_lease_claim_terminal(sc,&terminal));
        ++cases;
    }
    // Real firmware receipt case: only exact size/queue/band/channel/CRC
    // observations can remove a bit seeded by the first actual command.
    for(unsigned scenario=0;scenario<10;++scenario) {
        ItlIwn driver; seed(driver,(1U<<12)|(1U<<13)); auto *sc=&driver.com;
        struct { iwn_rx_desc desc; iwn_scan_results result; } packet{};
        packet.desc={20,IWN_SCAN_RESULTS,0,0,0x80};
        packet.result.channel=13; packet.result.band=1; packet.result.good_crc=1;
        if(scenario==1) packet.desc.len=19;
        if(scenario==2) packet.desc.len=21;
        if(scenario==3) packet.desc.qid=0;
        if(scenario==4) packet.result.band=0;
        if(scenario==5) packet.result.channel=0;
        if(scenario==6) packet.result.channel=15;
        if(scenario==7) packet.result.good_crc=0;
        if(scenario==8) packet.result.channel=9;
        if(scenario==9) packet.desc.len|=0x80000000;
        iwn_rx_data data{};
        dispatch_result(sc,&packet.desc,&data);
        assert(sc->sc_scan_lease.passive_retry_pending==
            ((scenario==0 || scenario==9)?(1U<<12):((1U<<12)|(1U<<13))));
        assert(dmaReads==((scenario>=1 && scenario<=3)?0U:1U));
        ++cases;
    }
    // Cancelled, draining, reset, untagged and unrelated owners do not retry.
    for(unsigned scenario=0;scenario<11;++scenario) {
        ItlIwn driver; seed(driver,1U<<13); auto *sc=&driver.com;
        if(scenario==0) sc->sc_scan_lease.abort_requested=true;
        if(scenario==1) sc->sc_scan_lease.hardware_invalidated=true;
        if(scenario==2) sc->sc_scan_lease.publication_invalidated=true;
        if(scenario==3) sc->sc_scan_lease.terminal_claimed=true;
        if(scenario==4) sc->sc_scan_lease.phase=IWN_SCAN_LEASE_ARMING;
        if(scenario==5) sc->sc_scan_lease.command_submitted=false;
        if(scenario==6) sc->sc_scan_lease.reassoc_serial=0;
        if(scenario==7) sc->sc_scan_lease.owner=IWN_SCAN_LEASE_WCL_BACKGROUND;
        if(scenario==8) sc->sc_scan_lease.owner=IWN_SCAN_LEASE_GENERIC_FOREGROUND;
        if(scenario==9) sc->sc_scan_lease.phase=IWN_SCAN_LEASE_DRAINING;
        if(scenario==10) sc->sc_scan_lease.serial=0;
        const auto before=sc->sc_scan_lease;
        assert(!iwn_scan_retry_passive_2ghz(sc));
        assert(!memcmp(&before,&sc->sc_scan_lease,sizeof(before)));
        assert(writes==1 && retrySubmissions==0 && resets==0);
        ++cases;
    }
    for(unsigned scenario=1;scenario<=3;++scenario) {
        ItlIwn driver; seed(driver,1U<<13); auto *sc=&driver.com;
        const auto serial=sc->sc_scan_lease.serial;
        submitMode=scenario;
        const bool consumed=iwn_scan_retry_passive_2ghz(sc);
        assert(consumed==(scenario!=1));
        if(scenario==1) {
            assert(sc->sc_scan_lease.serial==serial && sc->sc_scan_lease.abort_requested);
            assert(sc->sc_scan_lease.phase==IWN_SCAN_LEASE_ABORTING);
        }
        if(scenario==2) {
            assert(sc->sc_scan_lease.serial==serial+1 && sc->sc_scan_lease.reassoc_serial==32);
            assert(!sc->sc_scan_lease.abort_requested);
        }
        assert(resets==(scenario==3?1U:0U));
        ++cases;
    }
    for(unsigned scenario=0;scenario<3;++scenario) {
        ItlIwn driver; seed(driver,1U<<13); auto *sc=&driver.com;
        if(scenario==0) onWake=[](iwn_softc *v){++v->sc_ic.ic_pae_assoc_epoch;};
        if(scenario==1) onWake=[](iwn_softc *v){v->sc_ic.ic_wcl_reassoc_owner_active=0;};
        if(scenario==2) onWake=[](iwn_softc *v){v->sc_scan_lease.passive_retry_channel=12;};
        assert(!iwn_scan_retry_passive_2ghz(sc));
        assert(writes==1 && sc->sc_scan_lease.phase==IWN_SCAN_LEASE_ABORTING && !held);
        ++cases;
    }
    assert(iwn_scan_lease_passive_retry_channel(nullptr,1)==0);
    assert(!iwn_scan_lease_begin_passive_retry(nullptr,nullptr));
    printf("PASS: %u actual passive-retry/receipt/doorbell scenarios\n",cases);
}
