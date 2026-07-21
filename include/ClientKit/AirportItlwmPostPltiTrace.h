#ifndef AirportItlwmPostPltiTrace_h
#define AirportItlwmPostPltiTrace_h

/*
 * Safe-only post-PLTI association trace ABI.
 *
 * This ABI is intentionally independent from AirportItlwmRegDiag.  It
 * exports only fixed event classes and monotonic capture bookkeeping so a
 * runtime experiment can localise a failed saved-profile association without
 * exporting network identity, credentials, packet contents, firmware status,
 * signal/channel data, addresses, or kernel pointers.
 */

#include <stddef.h>
#include <stdint.h>

#define AIRPORT_ITLWM_POST_PLTI_TRACE_ABI_VERSION 1U
#define AIRPORT_ITLWM_POST_PLTI_TRACE_MAX_ENTRIES 128U

#define AIRPORT_ITLWM_POST_PLTI_TRACE_CONTROL_PROPERTY \
    "AirportItlwmPostPltiTraceControl"
#define AIRPORT_ITLWM_POST_PLTI_TRACE_CONTROL_ACK_PROPERTY \
    "AirportItlwmPostPltiTraceControlAck"
#define AIRPORT_ITLWM_POST_PLTI_TRACE_SNAPSHOT_PROPERTY \
    "AirportItlwmPostPltiTraceSnapshot"
#define AIRPORT_ITLWM_POST_PLTI_TRACE_BUFFER_PROPERTY \
    "AirportItlwmPostPltiTraceBuffer"

/*
 * Every event is categorical.  Values are ABI identifiers only; their order
 * is not a protocol state-machine or a claim that pure SAE is supported.
 */
enum AirportItlwmPostPltiTraceEvent {
    kAirportItlwmPostPltiTraceEventUnknown = 0,
    kAirportItlwmPostPltiTraceEventWclPmkReadyScanResume = 1,
    kAirportItlwmPostPltiTraceEventIwnScanCoalesced = 2,
    kAirportItlwmPostPltiTraceEventIwnScanStarted = 3,
    kAirportItlwmPostPltiTraceEventScanCompleted = 4,
    kAirportItlwmPostPltiTraceEventSelectionHeld = 5,
    kAirportItlwmPostPltiTraceEventBssSelected = 6,
    kAirportItlwmPostPltiTraceEventJoinBssEntered = 7,
    kAirportItlwmPostPltiTraceEventAuthStateEntered = 8,
    kAirportItlwmPostPltiTraceEventAuthEnqueued = 9,
    kAirportItlwmPostPltiTraceEventAuthDequeued = 10,
    kAirportItlwmPostPltiTraceEventAuthFwSubmitted = 11,
    kAirportItlwmPostPltiTraceEventAuthTxDone = 12,
    kAirportItlwmPostPltiTraceEventAuthRxFromFirmware = 13,
    kAirportItlwmPostPltiTraceEventAuthRxNet80211 = 14,
    kAirportItlwmPostPltiTraceEventAssocStateEntered = 15,
    kAirportItlwmPostPltiTraceEventAssocEnqueued = 16,
    kAirportItlwmPostPltiTraceEventAssocDequeued = 17,
    kAirportItlwmPostPltiTraceEventAssocFwSubmitted = 18,
    kAirportItlwmPostPltiTraceEventAssocTxDone = 19,
    kAirportItlwmPostPltiTraceEventAssocRxFromFirmware = 20,
    kAirportItlwmPostPltiTraceEventAssocRxNet80211 = 21,
    kAirportItlwmPostPltiTraceEventRunEntered = 22,
    kAirportItlwmPostPltiTraceEventEapolRxDecapped = 23,
    kAirportItlwmPostPltiTraceEventEapolRxKernelPae = 24,
    kAirportItlwmPostPltiTraceEventEapolTxEnqueued = 25,
    kAirportItlwmPostPltiTraceEventEapolFwSubmitted = 26,
    kAirportItlwmPostPltiTraceEventEapolTxDone = 27,
    kAirportItlwmPostPltiTraceEventPortValidTransition = 28,
    kAirportItlwmPostPltiTraceEventEpisodeAborted = 29,
    kAirportItlwmPostPltiTraceEventMax = 30
};

/* Categorical backend coverage, never a hardware identifier. */
enum AirportItlwmPostPltiTraceBackend {
    kAirportItlwmPostPltiTraceBackendUnknown = 0,
    kAirportItlwmPostPltiTraceBackendIwn = 1,
    kAirportItlwmPostPltiTraceBackendUnsupported = 2,
};

typedef struct AirportItlwmPostPltiTraceEntry {
    /* Written last with release ordering; zero means an unreadable slot. */
    uint32_t sequence;
    uint32_t captureGeneration;
    uint32_t episode;
    uint32_t event;
} AirportItlwmPostPltiTraceEntry;

typedef struct AirportItlwmPostPltiTraceSnapshot {
    uint32_t version;
    uint32_t size;
    uint32_t captureGeneration;
    uint32_t backend;
    uint32_t enabled;
    uint32_t targetBound;
    uint32_t activeEpisode;
    uint32_t episodeCount;
    uint32_t firstSequence;
    uint32_t entryCount;
    uint32_t droppedEntries;
    uint32_t latestSequence;
} AirportItlwmPostPltiTraceSnapshot;

typedef struct AirportItlwmPostPltiTraceBuffer {
    uint32_t version;
    uint32_t captureGeneration;
    uint32_t backend;
    uint32_t entryCount;
    uint32_t droppedEntries;
    uint32_t firstSequence;
    uint32_t latestSequence;
    AirportItlwmPostPltiTraceEntry
        entries[AIRPORT_ITLWM_POST_PLTI_TRACE_MAX_ENTRIES];
} AirportItlwmPostPltiTraceBuffer;

#endif /* AirportItlwmPostPltiTrace_h */
