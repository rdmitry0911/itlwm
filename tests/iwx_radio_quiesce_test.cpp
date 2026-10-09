// Exact caller bodies with an observed stop spy. This does not execute
// IWX init/stop, task gates, firmware erasure, or any on-air traffic.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#define XYLog(...) ((void)0)
#define container_of(pointer, type, member) reinterpret_cast<type *>(pointer)
constexpr unsigned IFF_UP=1, IFF_RUNNING=2;
constexpr int DVACT_QUIESCE=1, DVACT_RESUME=2, DVACT_WAKEUP=3;
using IOReturn=int;
constexpr int kIOReturnSuccess=0;
struct IONetworkInterface {};
struct _ifnet { unsigned if_flags=0; };
struct iwx_softc {
    struct Com {
        union { _ifnet ic_if; struct { _ifnet ac_if; } ic_ac; };
        Com() : ic_if{} {}
    } sc_ic;
    unsigned init_retry_count=0;
};
static void timeout_del(int *) {}
class ItlIwx {
public:
    iwx_softc com;
    uint64_t radioPowerOnEpoch=42;
    bool apCsaTimerInitialized=false;
    int apCsaTimeout=0;
    unsigned stops=0;
    IOReturn disable(IONetworkInterface *);
    int iwx_activate(iwx_softc *, int);
    void iwx_stop(_ifnet *ifp) {
        assert(ifp==&com.sc_ic.ic_ac.ac_if);
        ++stops;
    }
    int iwx_resume(iwx_softc *) { return 0; }
    int iwx_prepare_card_hw(iwx_softc *) { return 0; }
    void iwx_bootstrap_init_task(iwx_softc *) {}
};
#include "quiesce.inc"
int main(int argc, char **argv) {
    assert(argc==2);
    const std::string scenario=argv[1];
    ItlIwx driver;
    if (scenario=="running") driver.com.sc_ic.ic_ac.ac_if.if_flags=IFF_UP | IFF_RUNNING;
    else if (scenario=="primary-down-running") driver.com.sc_ic.ic_ac.ac_if.if_flags=IFF_RUNNING;
    else if (scenario=="early-off") driver.com.sc_ic.ic_ac.ac_if.if_flags=IFF_UP;
    else if (scenario=="early-off-primary-down") driver.com.sc_ic.ic_ac.ac_if.if_flags=0;
    else return 2;
    assert(driver.disable(nullptr)==kIOReturnSuccess);
    assert(driver.radioPowerOnEpoch==0 && !(driver.com.sc_ic.ic_ac.ac_if.if_flags & IFF_UP));
    std::fprintf(stderr, "IWX %s: actual disable/activate stop_calls=%u\n", argv[1], driver.stops);
    assert(driver.stops==1);
    std::puts("IWX full disable/activate boundary: PASS (stop spy only)");
}
