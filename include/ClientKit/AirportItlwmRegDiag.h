#ifndef AirportItlwmRegDiag_h
#define AirportItlwmRegDiag_h

#include <stddef.h>
#include <stdint.h>

#define AIRPORT_ITLWM_REGDIAG_ABI_VERSION 2U
#define AIRPORT_ITLWM_REGDIAG_MAX_TRACE_ENTRIES 128U
#define AIRPORT_ITLWM_REGDIAG_MAX_SSID_LEN 32U
#define AIRPORT_ITLWM_REGDIAG_MAX_NAME_LEN 32U
/* TraceLinkState arg2: parent bool is unavailable on pre-Tahoe targets. */
#define AIRPORT_ITLWM_REGDIAG_LINK_STATE_PARENT_ACCEPTED_UNAVAILABLE UINT64_MAX
/* TraceLinkContext arg2[63:32]: controller status was not sampled here. */
#define AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_STATUS_UNAVAILABLE UINT32_MAX
/* Recorder sentinel: load the safe atomic association epoch after opt-in. */
#define AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_EPOCH_CURRENT UINT64_MAX

/* TraceLinkContext arg0 packs compact, redaction-safe owner predicates. */
#define AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_ROUTE_MASK 0x0000003fU
#define AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_STAGE_SHIFT 6U
#define AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_STAGE_MASK 0x000007c0U
#define AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_ON_THREAD_SHIFT 11U
#define AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_ON_THREAD_MASK 0x00001800U
#define AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_IN_GATE_SHIFT 13U
#define AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_IN_GATE_MASK 0x00006000U
#define AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_ON_DISPATCH_SHIFT 15U
#define AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_ON_DISPATCH_MASK 0x00018000U
#define AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_LIFECYCLE_SHIFT 17U
#define AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_LIFECYCLE_MASK 0x001e0000U
#define AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_LINK_STATE_SHIFT 21U
#define AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_LINK_STATE_MASK 0x00e00000U

#define AIRPORT_ITLWM_REGDIAG_CONTROL_PROPERTY "AirportItlwmDiagControl"
#define AIRPORT_ITLWM_REGDIAG_CONTROL_ACK_PROPERTY "AirportItlwmDiagControlAck"
#define AIRPORT_ITLWM_REGDIAG_SNAPSHOT_PROPERTY "AirportItlwmDiagSnapshot"
#define AIRPORT_ITLWM_REGDIAG_TRACE_PROPERTY "AirportItlwmDiagTrace"

enum AirportItlwmRegDiagModeFlag {
    kAirportItlwmRegDiagModeEnabled = 0x00000001U,
    kAirportItlwmRegDiagModeLog = 0x00000002U,
    kAirportItlwmRegDiagModeAssoc = 0x00000004U,
    kAirportItlwmRegDiagModeData = 0x00000008U,
    kAirportItlwmRegDiagModeControl = 0x00000010U,
    /* Opt-in association/PMK timeline; carries no key bytes. */
    kAirportItlwmRegDiagModePmk = 0x00000020U,
    /* Opt-in passive Tahoe link-owner census; no publication is performed. */
    kAirportItlwmRegDiagModeLinkContext = 0x00000040U,
    kAirportItlwmRegDiagModeIntervention = 0x80000000U
};

enum AirportItlwmRegDiagBlockFlag {
    kAirportItlwmRegDiagBlockPublicAssoc = 0x00000001U,
    kAirportItlwmRegDiagBlockHiddenAssoc = 0x00000002U,
    kAirportItlwmRegDiagBlockTx = 0x00000004U,
    kAirportItlwmRegDiagBlockRx = 0x00000008U,
    kAirportItlwmRegDiagBlockEapolTx = 0x00000010U,
    kAirportItlwmRegDiagBlockEapolRx = 0x00000020U
};

enum AirportItlwmRegDiagTraceKind {
    kAirportItlwmRegDiagTraceUnknown = 0,
    kAirportItlwmRegDiagTracePublicAssoc = 1,
    kAirportItlwmRegDiagTraceHiddenAssoc = 2,
    kAirportItlwmRegDiagTraceLinkState = 3,
    kAirportItlwmRegDiagTraceTx = 4,
    kAirportItlwmRegDiagTraceRx = 5,
    kAirportItlwmRegDiagTraceBlock = 6,
    kAirportItlwmRegDiagTraceControl = 7,
    kAirportItlwmRegDiagTraceAuthPolicy = 8,
    kAirportItlwmRegDiagTracePmkIngress = 9,
    kAirportItlwmRegDiagTracePmkClear = 10,
    kAirportItlwmRegDiagTracePltiPublish = 11,
    kAirportItlwmRegDiagTracePltiDeliver = 12,
    /* Tahoe link-handoff timeline; no packet, identifier, or key material. */
    kAirportItlwmRegDiagTraceLinkStatus = 13,
    kAirportItlwmRegDiagTraceLinkPublish = 14,
    kAirportItlwmRegDiagTraceWclJoinAbort = 15,
    /* Tahoe passive owner-context census; no publication is performed. */
    kAirportItlwmRegDiagTraceLinkContext = 16
};

enum AirportItlwmRegDiagPath {
    kAirportItlwmRegDiagPathUnknown = 0,
    kAirportItlwmRegDiagPathPublicAssoc = 1,
    kAirportItlwmRegDiagPathHiddenAssoc = 2,
    kAirportItlwmRegDiagPathTx = 3,
    kAirportItlwmRegDiagPathRx = 4,
    kAirportItlwmRegDiagPathLink = 5,
    kAirportItlwmRegDiagPathPmk = 6,
    kAirportItlwmRegDiagPathPlti = 7,
    kAirportItlwmRegDiagPathLifecycle = 8
};

/* Non-secret source/decision IDs carried by the PMK timeline. */
enum AirportItlwmRegDiagPmkSource {
    kAirportItlwmRegDiagPmkSourceUnknown = 0,
    kAirportItlwmRegDiagPmkSourceCipherKey = 1,
    kAirportItlwmRegDiagPmkSourceCipherKeyMsk = 2,
    kAirportItlwmRegDiagPmkSourceCurPmk = 3,
    kAirportItlwmRegDiagPmkSourcePlti = 4
};

enum AirportItlwmRegDiagPmkDecision {
    kAirportItlwmRegDiagPmkDecisionAccepted = 0,
    kAirportItlwmRegDiagPmkDecisionRejectInput = 1,
    kAirportItlwmRegDiagPmkDecisionRejectNull = 2,
    kAirportItlwmRegDiagPmkDecisionRejectLength = 3,
    kAirportItlwmRegDiagPmkDecisionRejectWpa3 = 4,
    kAirportItlwmRegDiagPmkDecisionRejectPolicy = 5,
    kAirportItlwmRegDiagPmkDecisionRejectGeneration = 6,
    kAirportItlwmRegDiagPmkDecisionRejectTerminating = 7,
    kAirportItlwmRegDiagPmkDecisionNotReady = 8
};

enum AirportItlwmRegDiagPmkClearReason {
    kAirportItlwmRegDiagPmkClearUnknown = 0,
    kAirportItlwmRegDiagPmkClearAssocDisableRsn = 1,
    kAirportItlwmRegDiagPmkClearDisassociate = 2,
    kAirportItlwmRegDiagPmkClearPmksa = 3,
    kAirportItlwmRegDiagPmkClearLeave = 4,
    kAirportItlwmRegDiagPmkClearReassoc = 5,
    kAirportItlwmRegDiagPmkClearJoinAbort = 6,
    kAirportItlwmRegDiagPmkClearTerminate = 7
};

enum AirportItlwmRegDiagAssocPolicyFlag {
    kAirportItlwmRegDiagAssocPolicyRejectWpa3 = 0x00000001U,
    kAirportItlwmRegDiagAssocPolicyPskPmkEligible = 0x00000002U,
    kAirportItlwmRegDiagAssocPolicyLocalPsk = 0x00000004U,
    kAirportItlwmRegDiagAssocPolicyAuditedWpa3Transition = 0x00000008U
};

/* Non-secret branch IDs for the Tahoe link-handoff diagnostic timeline. */
enum AirportItlwmRegDiagLinkStatusDecision {
    kAirportItlwmRegDiagLinkStatusSame = 0,
    kAirportItlwmRegDiagLinkStatusApplied = 1,
    kAirportItlwmRegDiagLinkStatusLifecycleRejected = 2
};

enum AirportItlwmRegDiagLinkPublishDecision {
    kAirportItlwmRegDiagLinkPublishQueued = 0,
    kAirportItlwmRegDiagLinkPublishSourceUnavailable = 1,
    kAirportItlwmRegDiagLinkPublishOffGateRejected = 2,
    kAirportItlwmRegDiagLinkPublishPublished = 3,
    kAirportItlwmRegDiagLinkPublishActionUnavailable = 4
};

/* Passive route markers for TraceLinkContext. */
enum AirportItlwmRegDiagLinkContextRoute {
    kAirportItlwmRegDiagLinkContextNet80211Bridge = 1,
    kAirportItlwmRegDiagLinkContextControllerStatus = 2,
    kAirportItlwmRegDiagLinkContextPublishQueue = 3,
    kAirportItlwmRegDiagLinkContextPublishAction = 4,
    kAirportItlwmRegDiagLinkContextGate = 5,
    kAirportItlwmRegDiagLinkContextSkywalkParent = 6,
    kAirportItlwmRegDiagLinkContextWclUpdate = 7
};

/* Passive route-local stages for TraceLinkContext. */
enum AirportItlwmRegDiagLinkContextStage {
    kAirportItlwmRegDiagLinkContextEnter = 0,
    kAirportItlwmRegDiagLinkContextSameStatus = 1,
    kAirportItlwmRegDiagLinkContextLifecycleRejected = 2,
    kAirportItlwmRegDiagLinkContextBaseApplied = 3,
    kAirportItlwmRegDiagLinkContextSourceUnavailable = 4,
    kAirportItlwmRegDiagLinkContextSourceReady = 5,
    kAirportItlwmRegDiagLinkContextActionUnavailable = 6,
    kAirportItlwmRegDiagLinkContextActionReady = 7,
    kAirportItlwmRegDiagLinkContextGateRejected = 8,
    kAirportItlwmRegDiagLinkContextGateReady = 9,
    kAirportItlwmRegDiagLinkContextParentEnter = 10,
    kAirportItlwmRegDiagLinkContextParentAccepted = 11,
    kAirportItlwmRegDiagLinkContextParentRejected = 12,
    kAirportItlwmRegDiagLinkContextWclDecoded = 13,
    kAirportItlwmRegDiagLinkContextWclReturn = 14
};

/* Three-valued owner predicates: unknown, false, true. */
enum AirportItlwmRegDiagLinkContextPredicate {
    kAirportItlwmRegDiagLinkContextPredicateUnknown = 0,
    kAirportItlwmRegDiagLinkContextPredicateFalse = 1,
    kAirportItlwmRegDiagLinkContextPredicateTrue = 2
};

/* Locally-known admission facts, never an unlocked lifecycle-field read. */
enum AirportItlwmRegDiagLinkContextLifecycle {
    kAirportItlwmRegDiagLinkContextLifecycleUnknown = 0,
    kAirportItlwmRegDiagLinkContextLifecycleControllerSame = 1,
    kAirportItlwmRegDiagLinkContextLifecycleControllerAdmitted = 2,
    kAirportItlwmRegDiagLinkContextLifecycleControllerRejected = 3,
    kAirportItlwmRegDiagLinkContextLifecycleControllerDrainOwner = 4,
    kAirportItlwmRegDiagLinkContextLifecyclePublicationUnavailable = 5,
    kAirportItlwmRegDiagLinkContextLifecyclePublicationReady = 6,
    kAirportItlwmRegDiagLinkContextLifecycleInternalAdmitted = 7,
    kAirportItlwmRegDiagLinkContextLifecycleParentAccepted = 8,
    kAirportItlwmRegDiagLinkContextLifecycleParentRejected = 9
};

enum AirportItlwmRegDiagJoinAbortPhase {
    kAirportItlwmRegDiagJoinAbortEnter = 0,
    kAirportItlwmRegDiagJoinAbortExit = 1
};

typedef struct AirportItlwmRegDiagTraceEntry {
    uint32_t version;
    uint32_t sequence;
    uint32_t kind;
    uint32_t path;
    int32_t result;
    int32_t arg0;
    uint64_t arg1;
    uint64_t arg2;
} AirportItlwmRegDiagTraceEntry;

typedef struct AirportItlwmRegDiagTraceBuffer {
    uint32_t version;
    uint32_t entryCount;
    uint32_t nextSequence;
    uint32_t droppedEntries;
    AirportItlwmRegDiagTraceEntry entries[AIRPORT_ITLWM_REGDIAG_MAX_TRACE_ENTRIES];
} AirportItlwmRegDiagTraceBuffer;

typedef struct AirportItlwmRegDiagSnapshot {
    uint32_t version;
    uint32_t size;
    uint32_t sequence;
    uint32_t lastControlSequence;
    uint32_t modeFlags;
    uint32_t blockMask;

    uint32_t rtMask;
    uint32_t rtMask2;
    uint32_t rtMask3;
    int32_t icState;
    uint32_t icFlags;
    uint32_t ifFlags;
    uint32_t powerState;
    uint32_t pmPowerState;
    uint32_t currentStatus;
    uint64_t currentSpeed;

    uint32_t hasHalService;
    uint32_t hasNetIf;
    uint32_t hasBSDInterface;
    uint32_t hasBss;
    uint32_t nodeCount;
    uint32_t desiredEssLen;
    uint32_t currentSsidLen;
    uint32_t lastAssocSsidLen;

    uint8_t currentBssid[6];
    uint8_t desiredSsid[AIRPORT_ITLWM_REGDIAG_MAX_SSID_LEN];
    uint8_t currentSsid[AIRPORT_ITLWM_REGDIAG_MAX_SSID_LEN];
    uint8_t lastAssocBssid[6];
    uint8_t lastAssocSsid[AIRPORT_ITLWM_REGDIAG_MAX_SSID_LEN];
    char bsdName[AIRPORT_ITLWM_REGDIAG_MAX_NAME_LEN];

    uint32_t publicAssocCount;
    uint32_t hiddenAssocCount;
    uint32_t linkStateCount;
    uint32_t txCount;
    uint32_t rxCount;
    uint32_t eapolTxCount;
    uint32_t eapolRxCount;
    uint32_t txDropCount;
    uint32_t rxDropCount;
    uint32_t blockHitCount;

    int32_t lastPublicAssocResult;
    int32_t lastHiddenAssocResult;
    int32_t lastLinkStateResult;
    int32_t lastTxResult;
    int32_t lastRxResult;
    int32_t lastLinkState;
    uint32_t lastAssocAuthLower;
    uint32_t lastAssocAuthUpper;
    uint32_t lastAssocRsnIeLen;
    uint32_t lastTxLength;
    uint32_t lastRxLength;
    uint32_t lastBlockMask;

    uint64_t fNetIfPtr;
    uint64_t bsdIfPtr;
    uint64_t fTxQueuePtr;
    uint64_t fRxQueuePtr;

    /* ABI v2: opt-in SAE/PMK diagnosis, never key material. */
    uint32_t lastAssocAuthFlags;
    uint32_t lastAssocCandidateCount;
    uint32_t lastAssocPmfCapability;
    uint32_t lastAssocPolicyFlags;
    uint32_t pmkIngressCount;
    uint32_t pmkIngressRejectCount;
    uint32_t pmkClearCount;
    uint32_t pltiPublishCount;
    uint32_t pltiPublishRejectCount;
    uint32_t pltiDeliverCount;
    uint32_t pltiDeliverRejectCount;
    uint32_t lastPmkSource;
    uint32_t lastPmkDecision;
    uint32_t lastPmkKeyLen;
    uint32_t lastPmkAuthUpper;
    uint64_t lastPmkGeneration;
    uint32_t lastPmkClearReason;
    uint32_t reservedV2;
} AirportItlwmRegDiagSnapshot;

#define AIRPORT_ITLWM_REGDIAG_SNAPSHOT_V1_SIZE \
    offsetof(AirportItlwmRegDiagSnapshot, lastAssocAuthFlags)

#endif /* AirportItlwmRegDiag_h */
