#ifndef AirportItlwmRegDiag_h
#define AirportItlwmRegDiag_h

#include <stdint.h>

#define AIRPORT_ITLWM_REGDIAG_ABI_VERSION 1U
#define AIRPORT_ITLWM_REGDIAG_MAX_TRACE_ENTRIES 128U
#define AIRPORT_ITLWM_REGDIAG_MAX_SSID_LEN 32U
#define AIRPORT_ITLWM_REGDIAG_MAX_NAME_LEN 32U

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
    kAirportItlwmRegDiagTraceControl = 7
};

enum AirportItlwmRegDiagPath {
    kAirportItlwmRegDiagPathUnknown = 0,
    kAirportItlwmRegDiagPathPublicAssoc = 1,
    kAirportItlwmRegDiagPathHiddenAssoc = 2,
    kAirportItlwmRegDiagPathTx = 3,
    kAirportItlwmRegDiagPathRx = 4,
    kAirportItlwmRegDiagPathLink = 5
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
} AirportItlwmRegDiagSnapshot;

#endif /* AirportItlwmRegDiag_h */
