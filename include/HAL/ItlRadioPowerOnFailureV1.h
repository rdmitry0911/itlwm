#ifndef ITL_RADIO_POWER_ON_FAILURE_V1_H
#define ITL_RADIO_POWER_ON_FAILURE_V1_H

#include <stdint.h>

enum {
    kItlRadioPowerOnFailureVersion = 1,
    kItlRadioPowerOnFailureRfKill = 1,
    kItlRadioPowerOnFailureHardware = 2,
    kItlRadioPowerOnFailureRecoveryExhausted = 3,
};

/* Borrowed for one synchronous lower event callback. No node, controller,
 * credential or transport pointer is retained. Epoch belongs to the actual
 * controller activation accepted before the lower init task was scheduled. */
struct ItlRadioPowerOnFailureV1 {
    uint32_t version;
    uint32_t size;
    uint64_t requestEpoch;
    uint32_t status;
    uint32_t reason;
    int32_t lowerError;
    uint32_t reserved;
};

#endif
