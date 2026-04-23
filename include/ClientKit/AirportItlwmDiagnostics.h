#ifndef AirportItlwmDiagnostics_h
#define AirportItlwmDiagnostics_h

#include <stdint.h>

#define AIRPORT_ITLWM_DIAG_ABI_VERSION 1U
#define AIRPORT_ITLWM_DIAG_MAX_TRACE_ENTRIES 256U
#define AIRPORT_ITLWM_DIAG_MAX_SCAN_ENTRIES 64U
#define AIRPORT_ITLWM_DIAG_MAX_SSID_LEN 32U
#define AIRPORT_ITLWM_DIAG_MAX_NAME_LEN 32U

enum AirportItlwmDiagSelector {
    kAirportItlwmDiagGetConfig = 0,
    kAirportItlwmDiagSetConfig = 1,
    kAirportItlwmDiagClear = 2,
    kAirportItlwmDiagGetSnapshot = 3,
    kAirportItlwmDiagGetTrace = 4,
    kAirportItlwmDiagGetScanCache = 5,
    kAirportItlwmDiagSelectorCount
};

enum AirportItlwmDiagModeFlag {
    kAirportItlwmDiagModeEnabled = 0x00000001U,
    kAirportItlwmDiagModeTrace = 0x00000002U,
    kAirportItlwmDiagModeAssoc = 0x00000004U,
    kAirportItlwmDiagModeData = 0x00000008U,
    kAirportItlwmDiagModeIntervention = 0x80000000U
};

enum AirportItlwmDiagBlockFlag {
    kAirportItlwmDiagBlockPublicAssoc = 0x00000001U,
    kAirportItlwmDiagBlockHiddenAssoc = 0x00000002U,
    kAirportItlwmDiagBlockTx = 0x00000004U,
    kAirportItlwmDiagBlockRx = 0x00000008U,
    kAirportItlwmDiagBlockEapolTx = 0x00000010U,
    kAirportItlwmDiagBlockEapolRx = 0x00000020U
};

enum AirportItlwmDiagTraceMask {
    kAirportItlwmDiagTraceAssoc = 0x00000001U,
    kAirportItlwmDiagTraceData = 0x00000002U,
    kAirportItlwmDiagTraceControl = 0x00000004U,
    kAirportItlwmDiagTraceBlocks = 0x00000008U,
    kAirportItlwmDiagTraceAll = 0xffffffffU
};

enum AirportItlwmDiagTraceKind {
    kAirportItlwmDiagTraceUnknown = 0,
    kAirportItlwmDiagTraceCommandGate = 1,
    kAirportItlwmDiagTraceHandleCardSpecific = 2,
    kAirportItlwmDiagTraceBSDCommand = 3,
    kAirportItlwmDiagTraceApple80211Ioctl = 4,
    kAirportItlwmDiagTracePublicAssoc = 5,
    kAirportItlwmDiagTraceHiddenAssoc = 6,
    kAirportItlwmDiagTraceLinkState = 7,
    kAirportItlwmDiagTraceTx = 8,
    kAirportItlwmDiagTraceRx = 9,
    kAirportItlwmDiagTraceBlock = 10
};

enum AirportItlwmDiagPath {
    kAirportItlwmDiagPathUnknown = 0,
    kAirportItlwmDiagPathPublicAssoc = 1,
    kAirportItlwmDiagPathHiddenAssoc = 2,
    kAirportItlwmDiagPathHandleCardSpecific = 3,
    kAirportItlwmDiagPathBSD = 4,
    kAirportItlwmDiagPathApple80211Ioctl = 5,
    kAirportItlwmDiagPathTx = 6,
    kAirportItlwmDiagPathRx = 7
};

typedef struct AirportItlwmDiagConfig {
    uint32_t version;
    uint32_t size;
    uint32_t modeFlags;
    uint32_t traceMask;
    uint32_t blockMask;
    uint32_t reserved[3];
} AirportItlwmDiagConfig;

typedef struct AirportItlwmDiagTraceEntry {
    uint32_t version;
    uint32_t sequence;
    uint32_t kind;
    uint32_t path;
    int32_t command;
    int32_t requestType;
    int32_t result;
    int32_t arg0;
    uint64_t arg1;
    uint64_t arg2;
} AirportItlwmDiagTraceEntry;

typedef struct AirportItlwmDiagTraceBuffer {
    uint32_t version;
    uint32_t entryCount;
    uint32_t nextSequence;
    uint32_t droppedEntries;
    struct AirportItlwmDiagTraceEntry entries[AIRPORT_ITLWM_DIAG_MAX_TRACE_ENTRIES];
} AirportItlwmDiagTraceBuffer;

typedef struct AirportItlwmDiagScanEntry {
    uint32_t version;
    uint32_t channel;
    int16_t rssi;
    int16_t noise;
    uint8_t ssidLen;
    uint8_t bssid[6];
    char ssid[AIRPORT_ITLWM_DIAG_MAX_SSID_LEN];
    uint32_t rsnProtos;
    uint32_t supportedRsnProtos;
    uint32_t rsnAkms;
    uint32_t supportedRsnAkms;
    uint32_t rsnCiphers;
    uint32_t groupCipher;
    uint32_t groupMgmtCipher;
} AirportItlwmDiagScanEntry;

typedef struct AirportItlwmDiagScanCache {
    uint32_t version;
    uint32_t totalNodeCount;
    uint32_t entryCount;
    uint32_t reserved;
    struct AirportItlwmDiagScanEntry entries[AIRPORT_ITLWM_DIAG_MAX_SCAN_ENTRIES];
} AirportItlwmDiagScanCache;

typedef struct AirportItlwmDiagSnapshot {
    uint32_t version;
    uint32_t size;
    uint32_t modeFlags;
    uint32_t traceMask;
    uint32_t blockMask;
    uint32_t reserved0;

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

    uint8_t myAddress[6];
    uint8_t currentBssid[6];
    uint8_t desiredSsid[AIRPORT_ITLWM_DIAG_MAX_SSID_LEN];
    uint8_t currentSsid[AIRPORT_ITLWM_DIAG_MAX_SSID_LEN];
    uint8_t lastAssocBssid[6];
    uint8_t lastAssocSsid[AIRPORT_ITLWM_DIAG_MAX_SSID_LEN];

    char bsdName[AIRPORT_ITLWM_DIAG_MAX_NAME_LEN];
    char firmwareVersion[AIRPORT_ITLWM_DIAG_MAX_NAME_LEN];
    char driverVersion[AIRPORT_ITLWM_DIAG_MAX_NAME_LEN];

    uint32_t commandGateCount;
    uint32_t handleCardSpecificCount;
    uint32_t bsdCommandCount;
    uint32_t apple80211IoctlCount;
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

    int32_t lastCommandGateResult;
    int32_t lastHandleCardSpecificResult;
    int32_t lastBSDCommandResult;
    int32_t lastApple80211IoctlResult;
    int32_t lastPublicAssocResult;
    int32_t lastHiddenAssocResult;
    int32_t lastLinkStateResult;
    int32_t lastTxResult;
    int32_t lastRxResult;

    int32_t lastCommand;
    int32_t lastRequestType;
    int32_t lastLinkState;
    uint32_t lastAssocAuthLower;
    uint32_t lastAssocAuthUpper;
    uint32_t lastAssocRsnIeLen;
    uint32_t lastTxLength;
    uint32_t lastRxLength;
    uint32_t lastBlockMask;

    uint64_t fNetIfPtr;
    uint64_t bsdIfPtr;
    uint64_t fTxPoolPtr;
    uint64_t fRxPoolPtr;
    uint64_t fTxQueuePtr;
    uint64_t fRxQueuePtr;
} AirportItlwmDiagSnapshot;

#endif /* AirportItlwmDiagnostics_h */
