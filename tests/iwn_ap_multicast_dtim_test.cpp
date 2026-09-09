#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#define MIN(a, b) std::min((a), (b))
constexpr unsigned kItlApFirmwareMaxClients = 4;
constexpr int IWN_AP_STAGE_RUNNING = 3;
struct iwn_tx_ring { unsigned qid = 0, queued = 0; };
struct IwnApClientRuntime {
    bool inUse = false, associated = false, nodeInstalled = false;
    uint16_t txBaMask = 0;
    int txBaQueue[1] = {-1};
};
class ItlIwn {
public:
    bool apFirmwareTransitionActive = true;
    int apFirmwareStage = IWN_AP_STAGE_RUNNING;
    struct {
        int command_queue = 9, ntxqs = 20, first_agg_txq = 11;
        uint32_t qfullmsk = 0;
        iwn_tx_ring txq[20];
    } com;
    IwnApClientRuntime apClients[kItlApFirmwareMaxClients];
    uint32_t getAPTxFreeSpace() const;
    ItlIwn() {
        for (unsigned i = 0; i < 20; ++i) com.txq[i].qid = i;
        apClients[0].inUse = apClients[0].associated =
            apClients[0].nodeInstalled = true;
    }
};
#include "production.inc"

int main()
{
    assert(IWN_IPAN_MCAST_QUEUE == 8);
    for (uint16_t mask : {0, 1, 2, 0xffff}) {
        assert(iwn_ap_data_queue(true, mask, 11) == 8);
        assert(iwn_ap_data_queue(true, mask, -1) == 8);
        assert(iwn_ap_data_queue(false, mask, 11) == ((mask & 1) ? 11 : 5));
    }
    ItlIwn hal;
    const auto usable = IWN_TX_RING_COUNT - 1;
    assert(hal.getAPTxFreeSpace() == usable);
    hal.com.txq[8].queued = IWN_TX_RING_HIMARK + 1;
    assert(hal.getAPTxFreeSpace() == 0); // old production method fails here
    hal.com.txq[8].queued = 0;
    hal.com.qfullmsk = 1U << 8;
    assert(hal.getAPTxFreeSpace() == 0);
    hal.com.qfullmsk = 0;
    hal.com.txq[8].queued = 17;
    assert(hal.getAPTxFreeSpace() == usable - 17);
    hal.com.txq[5].queued = 21;
    assert(hal.getAPTxFreeSpace() == usable - 21);
    hal.apClients[0].txBaMask = 1;
    hal.apClients[0].txBaQueue[0] = 11;
    hal.com.txq[11].queued = 23;
    assert(hal.getAPTxFreeSpace() == usable - 23);
    hal.com.qfullmsk = 1U << 11;
    assert(hal.getAPTxFreeSpace() == 0);
    hal.com.qfullmsk = 0;
    hal.com.ntxqs = 8;
    assert(hal.getAPTxFreeSpace() == 0);
    hal.com.ntxqs = 20;
    hal.apClients[0].associated = false;
    assert(hal.getAPTxFreeSpace() == 0);
    hal.apClients[0].associated = true;
    hal.apFirmwareTransitionActive = false;
    assert(hal.getAPTxFreeSpace() == 0);
    std::puts("PASS: production IWN CAB selection and multicast/unicast/BA backpressure");
}
