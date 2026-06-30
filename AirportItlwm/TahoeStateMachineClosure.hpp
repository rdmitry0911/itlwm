//
//  TahoeStateMachineClosure.hpp
//  AirportItlwm
//

#ifndef TahoeStateMachineClosure_hpp
#define TahoeStateMachineClosure_hpp

#include <stdint.h>

namespace TahoeStateMachineClosure {

static constexpr uint32_t kStateMachineContractMinimum = 4;
static constexpr uint32_t kProducerConsumerChainMinimum = 4;

enum ClosureFlags : uint32_t {
    kOrderedProducerConsumer = 1U << 0,
    kBackpressureBounded = 1U << 1,
    kRecoverySafe = 1U << 2,
    kWakeRecoverySafe = 1U << 3,
    kErrorRecoverySafe = 1U << 4,
};

struct StateMachineContract {
    const char *name;
    const char *domain;
    const char *states;
    const char *orderedPath;
    const char *recoveryPath;
    uint32_t flags;
};

struct ProducerConsumerChain {
    const char *id;
    const char *producer;
    const char *queue;
    const char *consumer;
    const char *backpressure;
    const char *recovery;
    uint32_t flags;
};

static const StateMachineContract kStateMachineContracts[] = {
    {
        "association-join",
        "association",
        "INIT->SCAN->AUTH->ASSOC->RUN plus WCL JOIN_MANAGER "
        "IDLE->IN_PROGRESS->ASSOC_DONE->CONNECT_COMPLETE->IDLE",
        "SCAN_RESULT precedes APPLE80211_IOC_ASSOCIATE, WCL_ASSOCIATE, "
        "WCL_LINK_UP_DONE, and WCL_CONNECT_COMPLETE publication",
        "JOIN_ABORT_REQ, TIMEOUT, DRIVER_RESET, and SYSTEM_POWER_OFF enter "
        "ABORTED or HALTED; RESUME or SYSTEM_POWER_ON returns to IDLE while "
        "net80211 clears BA state, bgscan timeout, management queues, "
        "power-save queues, and group keys",
        kOrderedProducerConsumer | kRecoverySafe | kWakeRecoverySafe |
            kErrorRecoverySafe,
    },
    {
        "auth-pmk",
        "authentication",
        "NO_TARGET->TARGET_PUBLISHED->WAITING_FOR_PMK->PMK_INSTALLED->"
        "HANDSHAKE_READY->CLEARED",
        "AirportItlwmSkywalkInterface::associateSSID publishes the PLTI "
        "target before AirportItlwm::deliverExternalPMK can install a PMK; "
        "generation_echo rejects stale deliveries",
        "cancelExternalPMKWait and clearExternalPmkEligibilityLocked clear "
        "pending generation, ic_psk, and IEEE80211_F_PSK on disassociate, "
        "leave, PMKSA clear, RSN disable, JOIN_ABORT, and REASSOC edges",
        kOrderedProducerConsumer | kRecoverySafe | kWakeRecoverySafe |
            kErrorRecoverySafe,
    },
    {
        "scan-wcl",
        "scan",
        "IDLE->IN_PROGRESS->IDLE with HALTED and ABORTED recovery states",
        "setWCL_SCAN_REQ or setSCAN_REQ starts cache bgscan; "
        "postWclScanResultsGated publishes every WCL_SCAN_RESULT before "
        "the terminal WCL_SCAN_DONE status",
        "scanSource is cancelled and disabled on stop; WCL SCAN_ABORT_REQ, "
        "TIMEOUT, DRIVER_RESET, and SYSTEM_POWER_OFF transition to ABORTED "
        "or HALTED and resume through IDLE",
        kOrderedProducerConsumer | kRecoverySafe | kWakeRecoverySafe |
            kErrorRecoverySafe,
    },
    {
        "data-path",
        "data-path",
        "DOWN->QUEUE_READY->TX_RUNNING/RX_RUNNING->DRAINING->DOWN",
        "ifq_enqueue feeds the bounded IOPacketQueue, ifq_dequeue drains "
        "only while IFF_RUNNING and not TXFLUSH, and firmware RX completion "
        "is reordered before net80211 delivery",
        "qfullmsk sets ifq_oactive, TXFLUSH stops new dequeue, TX/RX rings, "
        "reorder buffers, and local Skywalk pending queues are drained on "
        "disable/stop/error, and wake re-enters through init/newstate "
        "instead of replaying stale packets",
        kOrderedProducerConsumer | kBackpressureBounded | kRecoverySafe |
            kWakeRecoverySafe | kErrorRecoverySafe,
    },
};

static const ProducerConsumerChain kProducerConsumerChains[] = {
    {
        "scan-request-to-wcl-done",
        "AirportItlwmSkywalkInterface::setWCL_SCAN_REQ / setSCAN_REQ",
        "scanSource timer plus ieee80211_begin_cache_bgscan",
        "AirportItlwm::postWclScanResultsGated",
        "single timer-driven scan completion and IWX_FLAG_SCANNING prevent "
        "overlapping scan iteration",
        "WCLScanManager abort, timeout, driver reset, and power-off edges "
        "reach ABORTED or HALTED before returning to IDLE",
        kOrderedProducerConsumer | kBackpressureBounded | kRecoverySafe |
            kWakeRecoverySafe | kErrorRecoverySafe,
    },
    {
        "join-candidate-to-connect-complete",
        "AirportItlwmSkywalkInterface::setWCL_ASSOCIATE",
        "WCL JOIN_MANAGER and net80211 newstate work queue",
        "postTahoeWclConnectCompleteEvent",
        "candidate selection is serialized by WCLJoinManager IN_PROGRESS and "
        "net80211 state transition ordering",
        "JOIN_ABORT_REQ, TIMEOUT, DRIVER_RESET, SYSTEM_POWER_OFF, RESUME, "
        "and SYSTEM_POWER_ON are explicit WCLJoinManager recovery edges",
        kOrderedProducerConsumer | kBackpressureBounded | kRecoverySafe |
            kWakeRecoverySafe | kErrorRecoverySafe,
    },
    {
        "auth-target-to-pmk-install",
        "AirportItlwmSkywalkInterface::associateSSID",
        "fAssocTarget generation gate",
        "AirportItlwm::deliverExternalPMK",
        "one pending generation is valid at a time and generation_echo "
        "rejects late producers",
        "clearExternalPmkEligibilityLocked and cancelExternalPMKWait discard "
        "stale PMK state on reset, leave, disassociate, PMKSA clear, "
        "JOIN_ABORT, and REASSOC",
        kOrderedProducerConsumer | kBackpressureBounded | kRecoverySafe |
            kWakeRecoverySafe | kErrorRecoverySafe,
    },
    {
        "tx-ifqueue-to-firmware-ring",
        "ifq_enqueue",
        "bounded IOPacketQueue and iwx qfullmsk",
        "iwx_tx",
        "lockEnqueueWithDrop bounds producer pressure; qfullmsk marks "
        "ifq_oactive and TXFLUSH blocks dequeue",
        "disableAdapterCore drains staged TX completions, and iwx_stop clears "
        "TXFLUSH, TX timer, TX rings, and queued state before init/newstate "
        "restarts output",
        kOrderedProducerConsumer | kBackpressureBounded | kRecoverySafe |
            kWakeRecoverySafe | kErrorRecoverySafe,
    },
    {
        "rx-firmware-ring-to-net80211",
        "firmware RX completion ring",
        "RX ring plus AMPDU reorder buffer",
        "ieee80211_input path",
        "RX descriptor validity, duplicate detection, BA window, and reorder "
        "buffer size checks drop invalid or out-of-window frames",
        "disableAdapterCore drains staged RX packets, and "
        "iwx_clear_reorder_buffer plus RX ring free/reset paths run on stop, "
        "error, and init failure",
        kOrderedProducerConsumer | kBackpressureBounded | kRecoverySafe |
            kWakeRecoverySafe | kErrorRecoverySafe,
    },
};

static_assert(sizeof(kStateMachineContracts) / sizeof(kStateMachineContracts[0]) >=
                  kStateMachineContractMinimum,
              "Tahoe closure requires association, auth, scan, and data-path machines");

static_assert(sizeof(kProducerConsumerChains) / sizeof(kProducerConsumerChains[0]) >=
                  kProducerConsumerChainMinimum,
              "Tahoe closure requires at least four ordered producer-consumer chains");

inline const StateMachineContract *stateMachineContracts(uint32_t *count)
{
    if (count != nullptr)
        *count = static_cast<uint32_t>(sizeof(kStateMachineContracts) /
                                      sizeof(kStateMachineContracts[0]));
    return kStateMachineContracts;
}

inline const ProducerConsumerChain *producerConsumerChains(uint32_t *count)
{
    if (count != nullptr)
        *count = static_cast<uint32_t>(sizeof(kProducerConsumerChains) /
                                      sizeof(kProducerConsumerChains[0]));
    return kProducerConsumerChains;
}

inline bool hasOrderedRecovery(uint32_t flags)
{
    return (flags & kOrderedProducerConsumer) != 0 &&
           (flags & kRecoverySafe) != 0 &&
           (flags & kErrorRecoverySafe) != 0;
}

inline bool hasBackpressureRecovery(uint32_t flags)
{
    return hasOrderedRecovery(flags) &&
           (flags & kBackpressureBounded) != 0 &&
           (flags & kWakeRecoverySafe) != 0;
}

} // namespace TahoeStateMachineClosure

#endif /* TahoeStateMachineClosure_hpp */
