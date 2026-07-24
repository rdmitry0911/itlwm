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

#define AIRPORT_ITLWM_POST_PLTI_TRACE_ABI_VERSION 5U
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
    kAirportItlwmPostPltiTraceEventStateScanSelfRequestObserved = 30,
    kAirportItlwmPostPltiTraceEventIwnScanStateEntered = 31,
    kAirportItlwmPostPltiTraceEventIwnScanCommandRejected = 32,
    kAirportItlwmPostPltiTraceEventScanNoCandidate = 33,
    kAirportItlwmPostPltiTraceEventCaptureWindowSealed = 34,
    kAirportItlwmPostPltiTraceEventIwxMfpPaeRxDelivered = 35,
    kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0Doorbelled = 36,
    kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0CompletionObserved = 37,
    /*
     * IWX PMF/BIP evidence v3.  These are intentionally only categorical
     * post-acknowledgement ownership facts; they carry neither key material
     * nor a firmware status/identifier.  Keep this vocabulary append-only.
     */
    kAirportItlwmPostPltiTraceEventIwxIgtkSlot4Published = 38,
    kAirportItlwmPostPltiTraceEventIwxIgtkSlot5Published = 39,
    kAirportItlwmPostPltiTraceEventIwxIgtkSlot4TxSelected = 40,
    kAirportItlwmPostPltiTraceEventIwxIgtkSlot5TxSelected = 41,
    /*
     * IWN software-PMF evidence v4.  These are emitted only after a local
     * software owner has reached the stated durable boundary; no event
     * carries a key, transaction ID, peer identity, or firmware result.
     * Keep this vocabulary append-only.
     */
    kAirportItlwmPostPltiTraceEventIwnMfpPaePtkSoftwarePrepared = 42,
    kAirportItlwmPostPltiTraceEventIwnMfpPaeGtkSoftwarePrepared = 43,
    kAirportItlwmPostPltiTraceEventIwnMfpPaeIgtkStageAcknowledged = 44,
    kAirportItlwmPostPltiTraceEventIwnMfpPaeSoftwareCcmpBipPublished = 45,
    kAirportItlwmPostPltiTraceEventIwnIgtkSlot4Published = 46,
    kAirportItlwmPostPltiTraceEventIwnIgtkSlot5Published = 47,
    kAirportItlwmPostPltiTraceEventIwnIgtkSlot4TxSelected = 48,
    kAirportItlwmPostPltiTraceEventIwnIgtkSlot5TxSelected = 49,
    /*
     * IWN PMF ingress evidence v5.  These two net80211/WCL boundaries
     * establish only whether a PMF request survived WCL and whether MFP was
     * negotiated for the selected BSS.  They carry no capability bitset,
     * BSS identity, key, status, or peer data.  Keep this vocabulary
     * append-only.
     */
    kAirportItlwmPostPltiTraceEventWclPmfRequestRetained = 50,
    kAirportItlwmPostPltiTraceEventNodeMfpNegotiated = 51,
    kAirportItlwmPostPltiTraceEventMax = 52
};

#define AIRPORT_ITLWM_POST_PLTI_TRACE_IWN_SOFTWARE_PMF_EVENT_FIRST \
    kAirportItlwmPostPltiTraceEventIwnMfpPaePtkSoftwarePrepared
#define AIRPORT_ITLWM_POST_PLTI_TRACE_IWN_SOFTWARE_PMF_EVENT_LAST \
    kAirportItlwmPostPltiTraceEventIwnIgtkSlot5TxSelected

#define AIRPORT_ITLWM_POST_PLTI_TRACE_PMF_INGRESS_EVENT_FIRST \
    kAirportItlwmPostPltiTraceEventWclPmfRequestRetained
#define AIRPORT_ITLWM_POST_PLTI_TRACE_PMF_INGRESS_EVENT_LAST \
    kAirportItlwmPostPltiTraceEventNodeMfpNegotiated

/*
 * Categorical backend coverage, never a hardware identifier.  IWX exposes
 * raw PMF-owner observations only; the ordered IWN trace verdict remains
 * deliberately unsupported for IWX.
 */
enum AirportItlwmPostPltiTraceBackend {
    kAirportItlwmPostPltiTraceBackendUnknown = 0,
    kAirportItlwmPostPltiTraceBackendIwn = 1,
    kAirportItlwmPostPltiTraceBackendUnsupported = 2,
    kAirportItlwmPostPltiTraceBackendIwx = 3,
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
