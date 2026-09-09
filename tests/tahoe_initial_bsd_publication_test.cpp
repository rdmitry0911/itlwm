#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
using IOReturn = int;
using errno_t = int;
using ifnet_t = int *;
enum { kIOReturnSuccess, kIOReturnNotReady, kIOReturnUnsupported, kIOReturnNoMemory };
enum { APPLE80211_VERSION = 1, APPLE80211_VIF_SOFT_AP = 7 };
static void testLog(const char *, ...) {}
#define XYLog(...) testLog(__VA_ARGS__)
static std::vector<int> calls;
enum { Capability = 1, Owner, Materialize, Lookup, Release, Primary, Delete };
static int apIfnet, otherIfnet, held;
static ifnet_t visibleIfnet = &apIfnet;
static int lookupError;
static errno_t ifnet_find_by_name(const char *name, ifnet_t *out) {
    calls.push_back(Lookup);
    assert(std::strcmp(name, "ap1") == 0);
    if (lookupError) return lookupError;
    *out = visibleIfnet;
    if (*out) ++held;
    return 0;
}
static void ifnet_release(ifnet_t value) {
    assert(value != nullptr && held > 0);
    --held;
    calls.push_back(Release);
}
static size_t test_strlcpy(char *out, const char *in, size_t capacity) {
    const size_t length = std::strlen(in);
    assert(length < capacity);
    std::memcpy(out, in, length + 1);
    return length;
}
#define strlcpy test_strlcpy
struct apple80211_virt_if_create_data { int version = 0, role = 0; uint8_t bsd_name[16]{}; };
struct AirportItlwmAPSTAOwner {};
struct Hal {
    bool supported = true;
    bool supportsAPMode() { calls.push_back(Capability); return supported; }
};
struct NetIf {
    const char *name = "ap1";
    const char *getBSDName() { return name; }
    ifnet_t getBSDInterface() { return &apIfnet; }
    void deferBSDAttach(bool defer) { assert(!defer); calls.push_back(Primary); }
};
class AirportItlwm {
public:
    Hal hal;
    Hal *fHalService = &hal;
    AirportItlwmAPSTAOwner owner;
    NetIf ap, primary;
    NetIf *fAPSTANetIf = nullptr, *fNetIf = &primary;
    bool failOwner = false;
    int materialization = kIOReturnSuccess;
    AirportItlwmAPSTAOwner *ensureAPSTAOwner(const apple80211_virt_if_create_data *create) {
        calls.push_back(Owner);
        assert(create->version == 1 && create->role == 7);
        assert(std::strcmp(reinterpret_cast<const char *>(create->bsd_name), "ap1") == 0);
        return failOwner ? nullptr : &owner;
    }
    IOReturn materializeAPSTAInterface(const apple80211_virt_if_create_data *) {
        calls.push_back(Materialize);
        if (!materialization) fAPSTANetIf = &ap;
        return materialization;
    }
    void deleteAPSTAOwner() { calls.push_back(Delete); fAPSTANetIf = nullptr; }
    IOReturn publishDefaultAPSTAInterface();
    void publishInitialBSDInterfaces();
};
#include "production.inc"
int main() {
    {
        AirportItlwm driver;
        driver.publishInitialBSDInterfaces();
        assert((calls == std::vector<int>{Capability, Owner, Materialize, Lookup, Release, Primary}));
        assert(held == 0);
    }
    calls.clear();
    {
        AirportItlwm driver;
        driver.hal.supported = false;
        driver.publishInitialBSDInterfaces();
        assert((calls == std::vector<int>{Capability, Primary}));
    }
    calls.clear();
    {
        AirportItlwm driver;
        driver.failOwner = true;
        driver.publishInitialBSDInterfaces();
        assert((calls == std::vector<int>{Capability, Owner, Primary}));
    }
    calls.clear();
    {
        AirportItlwm driver;
        driver.materialization = kIOReturnNotReady;
        driver.publishInitialBSDInterfaces();
        assert((calls == std::vector<int>{Capability, Owner, Materialize, Delete, Primary}));
    }
    for (int mode = 0; mode != 3; ++mode) {
        calls.clear();
        lookupError = mode == 0 ? ENXIO : 0;
        visibleIfnet = mode == 1 ? nullptr : &otherIfnet;
        AirportItlwm driver;
        driver.publishInitialBSDInterfaces();
        assert(calls.back() == Primary && held == 0);
    }
    calls.clear();
    {
        AirportItlwm driver;
        driver.fHalService = nullptr;
        driver.publishInitialBSDInterfaces();
        assert((calls == std::vector<int>{Primary}));
    }
    std::puts("PASS: real initial-publication methods order supported AP before primary and retain STA failure fallback");
}
