#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

using IOReturn = uint32_t;
using IOOptionBits = uint32_t;
using mbuf_t = void *;
constexpr IOReturn kIOReturnSuccess = 0;
constexpr IOReturn kIOReturnOutputDropped = 1;
constexpr IOReturn kIOReturnNoResources = 0xe00002be;
constexpr IOReturn kIOReturnBadArgument = 0xe00002c2;
constexpr IOReturn kIOReturnNotReady = 0xe00002d8;
constexpr IOReturn kIOReturnCannotLock = 0xe00002cc;
constexpr unsigned kAirportItlwmAPSTATxSubQueueCount = 4;

struct OSObject { virtual ~OSObject() = default; };
#define OSDynamicCast(type, object) dynamic_cast<type *>(object)
static unsigned logs;
#define XYLog(...) (++logs)

struct IOWorkLoop {
    bool gated = true;
    bool inGate() const { return gated; }
};
using Action = IOReturn (*)(OSObject *, void *, void *, void *, void *);
struct IOCommandGate {
    OSObject *owner = nullptr;
    unsigned calls = 0;
    bool reject = false;
    IOReturn attemptAction(Action action, void *packet) {
        ++calls;
        return reject ? kIOReturnCannotLock :
            action(owner, packet, nullptr, nullptr, nullptr);
    }
};

// Model only the recovered option-bit dispatch and event-source admission.
// The production request method below is NOT replaced by this fixture.
// Packet ownership and real workloop timing require the separate live test.
struct IOSkywalkTxSubmissionQueue {
    bool enabled = true;
    bool attached = true;
    uint32_t requested = 0, serviced = 0;
    unsigned calls = 0, callbacks = 0;
    unsigned pendingPackets = 0, completedPackets = 0;
    bool isEnabled() const { return enabled; }
    void consume() {
        ++callbacks;
        completedPackets += pendingPackets;
        pendingPackets = 0;
    }
    IOReturn requestDequeue(void *refcon, IOOptionBits options) {
        assert(refcon == nullptr);
        ++calls;
        if (!attached) return kIOReturnNotReady;
        if (options & 1U) ++requested;
        else consume();
        return kIOReturnSuccess;
    }
    void checkForWork() {
        if (!attached || requested == serviced) return;
        serviced = requested;
        if (enabled) consume();
    }
};

struct AirportItlwm {
    bool running = true;
    IOSkywalkTxSubmissionQueue *fAPSTATxQueues[4] = {};
    bool isHostApRunning() const { return running; }
    void requestAPTxDequeue();
};
struct ItlIwn : OSObject {
    IOWorkLoop *loop = nullptr;
    IOCommandGate *gate = nullptr;
    int error = 0;
    unsigned attempts = 0;
    IOWorkLoop *getMainWorkLoop() { return loop; }
    IOCommandGate *getMainCommandGate() { return gate; }
    int iwn_send_ap_data_frame(mbuf_t packet) {
        assert(packet != nullptr);
        ++attempts;
        return error;
    }
    static IOReturn iwn_ap_data_tx_action(OSObject *, void *, void *, void *, void *);
    IOReturn transmitAPData(mbuf_t);
};

#include "production.inc"

static void dequeueTests()
{
    for (unsigned mask = 0; mask != 16; ++mask) {
        AirportItlwm controller;
        IOSkywalkTxSubmissionQueue queues[4];
        for (unsigned i = 0; i != 4; ++i) {
            controller.fAPSTATxQueues[i] = &queues[i];
            queues[i].enabled = (mask & (1U << i)) != 0;
            queues[i].pendingPackets = 1020;
        }
        for (unsigned repeat = 0; repeat != 2000; ++repeat)
            controller.requestAPTxDequeue();
        for (unsigned i = 0; i != 4; ++i) {
            assert(queues[i].callbacks == 0); // No TX on the RX call stack.
            assert(queues[i].pendingPackets == 1020);
            assert(queues[i].calls == (queues[i].enabled ? 2000U : 0U));
            queues[i].checkForWork();
            queues[i].checkForWork();
            assert(queues[i].callbacks == (queues[i].enabled ? 1U : 0U));
            assert(queues[i].completedPackets == (queues[i].enabled ? 1020U : 0U));
        }
    }
    AirportItlwm controller;
    controller.requestAPTxDequeue(); // All-null partial construction.
    IOSkywalkTxSubmissionQueue queue;
    controller.fAPSTATxQueues[2] = &queue;
    controller.running = false;
    controller.requestAPTxDequeue();
    assert(queue.calls == 0);
    controller.running = true;
    queue.requested = queue.serviced = UINT32_MAX;
    queue.pendingPackets = 17;
    controller.requestAPTxDequeue();
    assert(queue.requested == 0 && queue.callbacks == 0);
    queue.checkForWork();
    assert(queue.completedPackets == 17);
    controller.requestAPTxDequeue();
    queue.enabled = false;
    queue.checkForWork();
    assert(queue.callbacks == 1); // Stop before dispatch cannot submit.
    queue.enabled = true;
    controller.requestAPTxDequeue();
    queue.attached = false;
    queue.checkForWork();
    assert(queue.callbacks == 1); // Removed source cannot dispatch.
}

static void packetResultTests()
{
    IOWorkLoop loop;
    IOCommandGate gate;
    ItlIwn driver;
    driver.loop = &loop;
    driver.gate = &gate;
    gate.owner = &driver;
    int packet;
    for (bool gated : {false, true}) {
        loop.gated = gated;
        for (int error : {0, EACCES, ENOBUFS, EHOSTUNREACH, EINVAL}) {
            driver.error = error;
            logs = driver.attempts = gate.calls = 0;
            for (unsigned i = 0; i != 1020; ++i) {
                IOReturn result = driver.transmitAPData(&packet);
                assert(result == (error == 0 ? kIOReturnSuccess :
                    error == ENOBUFS ? kIOReturnNoResources : kIOReturnOutputDropped));
            }
            assert(driver.attempts == 1020);
            assert(gate.calls == (gated ? 0U : 1020U));
            assert(logs == 0); // Not a console write per ordinary packet result.
        }
    }
    loop.gated = false;
    gate.reject = true;
    logs = driver.attempts = 0;
    assert(driver.transmitAPData(&packet) == kIOReturnCannotLock);
    assert(driver.attempts == 0 && logs == 1);
    assert(driver.transmitAPData(nullptr) == kIOReturnBadArgument);
    driver.gate = nullptr;
    assert(driver.transmitAPData(&packet) == kIOReturnNotReady);
    driver.gate = &gate;
    driver.loop = nullptr;
    assert(driver.transmitAPData(&packet) == kIOReturnNotReady);
    assert(ItlIwn::iwn_ap_data_tx_action(nullptr, &packet, nullptr, nullptr, nullptr)
        == kIOReturnBadArgument);
    assert(ItlIwn::iwn_ap_data_tx_action(&driver, nullptr, nullptr, nullptr, nullptr)
        == kIOReturnBadArgument);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (std::strcmp(argv[1], "logging") != 0) dequeueTests();
    if (std::strcmp(argv[1], "dequeue") != 0) packetResultTests();
    std::puts("PASS: production AP dequeue defers RX work and preserves packet results");
}
