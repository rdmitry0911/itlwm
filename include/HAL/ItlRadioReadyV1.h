#ifndef ITL_RADIO_READY_V1_H
#define ITL_RADIO_READY_V1_H

#include <stdint.h>

enum { kItlRadioReadyVersion = 1 };

/* Borrowed only for the synchronous internal REOPENED callback. Epoch zero
 * means untagged bootstrap, never permission to borrow a newer POWER epoch.
 * Receipt survives scan-command completion but is invalidated by reset or
 * replacement activation. All fields are copied values, not owner pointers. */
struct ItlRadioReadyV1 {
    uint32_t version;
    uint32_t size;
    uint64_t requestEpoch;
    uint64_t receiptSerial;
    uint32_t backendGeneration;
    uint32_t reserved;
};

#endif
