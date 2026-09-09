#include <cassert>
#include <cstdio>
#include <vector>

using UInt = unsigned int;
using IOReturn = int;
static constexpr IOReturn kIOReturnSuccess = 0;
static constexpr IOReturn kAirportItlwmAPSTAEnableNotRunningReturn = 6;
#define XYLog(...) ((void)0)

enum Event { BaseEnable, Confirm, CarrierUp, EnableQueues, DisableQueues,
             BaseDisable };
static std::vector<Event> events;

struct AirportItlwm {
    bool running = false;
    void noteAPSTAInterfaceEnableDuringPendingHostAPStart() {
        events.push_back(Confirm);
    }
    bool isHostApRunning() const { return running; }
};
struct IO80211SkywalkInterface {
    UInt carrier = 1;
    IOReturn disable(UInt) {
        events.push_back(BaseDisable);
        carrier = 1;
        return kIOReturnSuccess;
    }
};
struct IO80211VirtualInterface : IO80211SkywalkInterface {
    IOReturn enableResult = 0;
    IOReturn enable(UInt) {
        events.push_back(BaseEnable);
        // The family restores association; it does not restore carrier.
        return enableResult;
    }
};
struct AirportItlwmAPSTASkywalkInterface : IO80211VirtualInterface {
    AirportItlwm *controller = nullptr;
    UInt association = 2;
    UInt getAssocState() const { return association; }
    IOReturn reportLinkStatus(UInt status, UInt media) {
        assert(status == 3 && media == 0x80);
        carrier = status;
        events.push_back(CarrierUp);
        return 0;
    }
    void enableDatapath() { events.push_back(EnableQueues); }
    void disableDatapath() { events.push_back(DisableQueues); }
    IOReturn enable(UInt);
    IOReturn disable(UInt);
};

#include "production.inc"

int main() {
    AirportItlwm owner;
    AirportItlwmAPSTASkywalkInterface ap;
    ap.controller = &owner;
    owner.running = true;
    assert(ap.enable(1) == 0);
    assert(ap.carrier == 3);
    assert((events == std::vector<Event>{BaseEnable, Confirm, CarrierUp,
                                        EnableQueues}));
    for (unsigned i = 0; i != 4; ++i) {
        events.clear();
        assert(ap.disable(1) == 0);
        assert(ap.carrier == 1);
        assert((events == std::vector<Event>{DisableQueues, BaseDisable}));
        events.clear();
        assert(ap.enable(1) == 0);
        assert(ap.carrier == 3);
        assert((events == std::vector<Event>{BaseEnable, Confirm, CarrierUp,
                                            EnableQueues}));
    }
    for (bool running : {false, true}) {
        owner.running = running;
        for (IOReturn result : {0, 19}) {
            events.clear();
            ap.carrier = 1;
            ap.enableResult = result;
            assert(ap.enable(1) == result);
            assert(ap.carrier == (running && result == 0 ? 3U : 1U));
        }
    }
    ap.enableResult = 0;
    ap.controller = nullptr;
    ap.carrier = 1;
    events.clear();
    assert(ap.enable(1) == 0 && ap.carrier == 1);
    assert((events == std::vector<Event>{BaseEnable, EnableQueues}));
    ap.controller = &owner;
    ap.association = 0;
    events.clear();
    assert(ap.enable(1) == kAirportItlwmAPSTAEnableNotRunningReturn);
    assert(events.empty() && ap.carrier == 1);
    std::puts("PASS: actual APSTA BSD enable restores only a confirmed AP carrier");
}
