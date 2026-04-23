//
//  AirportItlwmV2.cpp
//  AirportItlwm-Sonoma
//
//  Created by qcwap on 2023/6/27.
//  Copyright © 2023 钟先耀. All rights reserved.
//

#include "AirportItlwmV2.hpp"
#include "AirportItlwmRegDiag.hpp"
#include <sys/_netstat.h>
#include <crypto/sha1.h>
#include <net80211/ieee80211_priv.h>
#include <net80211/ieee80211_var.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSString.h>

// Build identification must print the actual source revision in load logs so the
// running kext can be matched 1:1 against the workspace and installed binary.
// Tahoe originally relied on an external script-only ITLWM_COMMIT_HASH define,
// which made plain Xcode/xcodebuild builds silently fall back to __DATE__/__TIME__.
// The project already injects GIT_COMMIT in GCC_PREPROCESSOR_DEFINITIONS, so use
// that canonical build setting before falling back to timestamps.
#ifndef ITLWM_COMMIT_HASH
#ifdef GIT_COMMIT
#define ITLWM_COMMIT_HASH GIT_COMMIT
#else
#define ITLWM_COMMIT_HASH __DATE__ " " __TIME__
#endif
#endif
#define ITLWM_XSTR(s) ITLWM_STR(s)
#define ITLWM_STR(s) #s
#define ITLWM_COMMIT_SUFFIX " (" ITLWM_XSTR(ITLWM_COMMIT_HASH) ")"

#include "AirportItlwmSkywalkInterface.hpp"
#include "IOPCIEDeviceWrapper.hpp"
#include <IOKit/skywalk/IOSkywalkPacketBuffer.h>

// ── Skywalk buflet struct definitions ──────────────────────────────
// The kernel-internal structs __buflet, skmem_bufctl, and __kern_buflet
// are defined in SDK headers but guarded by BSD_KERNEL_PRIVATE, which is
// never defined for third-party AuxKC kexts.  os_packet.h forward-declares
// `struct __kern_buflet` (via `typedef struct __kern_buflet *kern_buflet_t`)
// but never provides the body.  We complete that forward declaration here
// with the identical layout from the SDK headers so the inline _buflet_*
// accessors below can resolve struct member references at compile time.
//
// Source headers (all in MacKernelSDK/Headers/skywalk/):
//   __buflet       — packet/os_packet_private.h:242  (PRIVATE guard)
//   __kern_buflet  — packet/packet_var.h:47          (BSD_KERNEL_PRIVATE)
//   skmem_bufctl   — mem/skmem_cache_var.h:40        (BSD_KERNEL_PRIVATE)
// Field offsets verified against Xcode 26.2 / macOS Sequoia 26.3 SDK.
//
// These are stable kernel ABI — Skywalk packet processing depends on them.
// If Apple changes the layout, the kern_buflet_* C KPI would also break,
// so every Skywalk nexus provider (including Apple's own) relies on this.

struct __buflet {
    union {
        uint64_t            __buflet_next;
        const uint64_t      __nbft_addr;    // mach_vm_address_t
    };
    const uint64_t          __baddr;        // buffer data address
    const uint32_t          __bft_idx;      // obj_idx_t — buflet index in region
    const uint32_t          __bidx;         // obj_idx_t — buffer object index
    const uint32_t          __nbft_idx;     // obj_idx_t — next buflet index
    const uint16_t          __dlim;         // maximum data length
    uint16_t                __dlen;         // current data length
    uint16_t                __doff;         // current data offset
    const uint16_t          __flag;
} __attribute__((packed));

struct skmem_bufctl {
    struct { struct skmem_bufctl *sle_next; } bc_link;  // SLIST_ENTRY
    void                    *bc_addr;       // buffer object address
    void                    *bc_addrm;      // mirrored buffer obj addr
    void                    *bc_slab;       // struct skmem_slab *
    uint32_t                bc_lim;         // buffer object limit
    uint32_t                bc_flags;
    uint32_t                bc_idx;
    volatile uint32_t       bc_usecnt;
};

struct __kern_buflet {
    struct __buflet                     buf_com;
    const struct skmem_bufctl          *buf_ctl;
#if !defined(__LP64__)
    uint32_t __kern_buflet_padding;
#endif
};

#define super IO80211Controller
OSDefineMetaClassAndStructors(AirportItlwm, IO80211Controller);
OSDefineMetaClassAndStructors(AirportItlwmBootNub, IOService)
OSDefineMetaClassAndStructors(CTimeout, OSObject)

#include "Airport/CCDataStream.h"
#include "Airport/CCFaultReporter.h"


IO80211WorkQueue *_fWorkloop;
IOCommandGate *_fCommandGate;

// RuntimeDiag struct defined in AirportItlwmV2.hpp
RuntimeDiag sRT = {};

struct AirportItlwmRegDiagState {
    uint32_t modeFlags;
    uint32_t blockMask;
    uint32_t lastControlSequence;
    uint32_t snapshotSequence;
    AirportItlwmRegDiagSnapshot snapshot;
    AirportItlwmRegDiagTraceBuffer trace;
};

static AirportItlwmRegDiagState sRegDiag = {};

static bool
airportItlwmRegDiagEnabled(uint32_t modeMask)
{
    return (sRegDiag.modeFlags & kAirportItlwmRegDiagModeEnabled) != 0 &&
           (sRegDiag.modeFlags & modeMask) != 0;
}

static bool
airportItlwmRegDiagLogEnabled()
{
    return (sRegDiag.modeFlags & kAirportItlwmRegDiagModeEnabled) != 0 &&
           (sRegDiag.modeFlags & kAirportItlwmRegDiagModeLog) != 0;
}

static void
airportItlwmRegDiagInitTrace()
{
    sRegDiag.trace.version = AIRPORT_ITLWM_REGDIAG_ABI_VERSION;
}

static uint32_t
airportItlwmRegDiagMinU32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static void
airportItlwmRegDiagCopyBytes(uint8_t *dst, uint32_t dstLen,
                             const uint8_t *src, uint32_t srcLen)
{
    if (dst == nullptr || dstLen == 0)
        return;
    memset(dst, 0, dstLen);
    if (src == nullptr || srcLen == 0)
        return;
    memcpy(dst, src, airportItlwmRegDiagMinU32(dstLen, srcLen));
}

static void
airportItlwmRegDiagCopyCString(char *dst, uint32_t dstLen, const char *src)
{
    if (dst == nullptr || dstLen == 0)
        return;
    memset(dst, 0, dstLen);
    if (src == nullptr)
        return;
    strlcpy(dst, src, dstLen);
}

static bool
airportItlwmRegDiagParseU32Value(const char *value, uint32_t *out)
{
    if (value == nullptr || out == nullptr)
        return false;

    uint32_t base = 10;
    if (value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
        base = 16;
        value += 2;
    }

    uint32_t result = 0;
    bool seen = false;
    for (const char *p = value; *p != '\0'; p++) {
        uint32_t digit;
        if (*p >= '0' && *p <= '9') {
            digit = static_cast<uint32_t>(*p - '0');
        } else if (base == 16 && *p >= 'a' && *p <= 'f') {
            digit = static_cast<uint32_t>(*p - 'a' + 10);
        } else if (base == 16 && *p >= 'A' && *p <= 'F') {
            digit = static_cast<uint32_t>(*p - 'A' + 10);
        } else {
            break;
        }
        if (digit >= base)
            break;
        result = result * base + digit;
        seen = true;
    }

    if (!seen)
        return false;
    *out = result;
    return true;
}

static bool
airportItlwmRegDiagReadU32Key(const char *command, const char *key, uint32_t *out)
{
    if (command == nullptr || key == nullptr || out == nullptr)
        return false;

    const size_t keyLen = strlen(key);
    const char *p = command;
    while (*p != '\0') {
        while (*p == ' ' || *p == '\t' || *p == ';' || *p == ',')
            p++;
        if (strncmp(p, key, keyLen) == 0 && p[keyLen] == '=')
            return airportItlwmRegDiagParseU32Value(p + keyLen + 1, out);
        while (*p != '\0' && *p != ';' && *p != ',' && *p != ' ' && *p != '\t')
            p++;
    }
    return false;
}

static void
airportItlwmRegDiagSetFlagFromControl(const char *command, const char *key,
                                      uint32_t flag, uint32_t *modeFlags)
{
    uint32_t value = 0;
    if (!airportItlwmRegDiagReadU32Key(command, key, &value))
        return;
    if (value != 0)
        *modeFlags |= flag;
    else
        *modeFlags &= ~flag;
}

static void
airportItlwmRegDiagPublishString(AirportItlwm *driver, const char *name,
                                 const char *value)
{
    if (driver == nullptr || name == nullptr || value == nullptr)
        return;
    OSString *string = OSString::withCString(value);
    if (string != nullptr) {
        driver->setProperty(name, string);
        string->release();
    }
}

static void
airportItlwmRegDiagPublishData(AirportItlwm *driver, const char *name,
                               const void *bytes, uint32_t size)
{
    if (driver == nullptr || name == nullptr || bytes == nullptr || size == 0)
        return;
    OSData *data = OSData::withBytes(bytes, size);
    if (data != nullptr) {
        driver->setProperty(name, data);
        data->release();
    }
}

static void
airportItlwmRegDiagClearPreservingControl(uint32_t modeFlags, uint32_t blockMask,
                                          uint32_t controlSequence)
{
    memset(&sRegDiag, 0, sizeof(sRegDiag));
    sRegDiag.modeFlags = modeFlags;
    sRegDiag.blockMask = blockMask;
    sRegDiag.lastControlSequence = controlSequence;
    airportItlwmRegDiagInitTrace();
}

static void
airportItlwmRegDiagApplyControl(AirportItlwm *driver, const char *command)
{
    uint32_t sequence = 0;
    if (!airportItlwmRegDiagReadU32Key(command, "seq", &sequence)) {
        airportItlwmRegDiagPublishString(driver,
                                         AIRPORT_ITLWM_REGDIAG_CONTROL_ACK_PROPERTY,
                                         "seq=0 applied=0 error=missing-seq");
        return;
    }

    if (sequence == 0 || sequence == sRegDiag.lastControlSequence)
        return;

    uint32_t modeFlags = sRegDiag.modeFlags;
    uint32_t blockMask = sRegDiag.blockMask;
    uint32_t parsed = 0;
    if (airportItlwmRegDiagReadU32Key(command, "mode", &parsed))
        modeFlags = parsed;
    if (airportItlwmRegDiagReadU32Key(command, "block", &parsed))
        blockMask = parsed;

    airportItlwmRegDiagSetFlagFromControl(command, "enable",
                                          kAirportItlwmRegDiagModeEnabled,
                                          &modeFlags);
    airportItlwmRegDiagSetFlagFromControl(command, "log",
                                          kAirportItlwmRegDiagModeLog,
                                          &modeFlags);
    airportItlwmRegDiagSetFlagFromControl(command, "assoc",
                                          kAirportItlwmRegDiagModeAssoc,
                                          &modeFlags);
    airportItlwmRegDiagSetFlagFromControl(command, "data",
                                          kAirportItlwmRegDiagModeData,
                                          &modeFlags);
    airportItlwmRegDiagSetFlagFromControl(command, "control",
                                          kAirportItlwmRegDiagModeControl,
                                          &modeFlags);
    airportItlwmRegDiagSetFlagFromControl(command, "intervention",
                                          kAirportItlwmRegDiagModeIntervention,
                                          &modeFlags);

    uint32_t clear = 0;
    if (airportItlwmRegDiagReadU32Key(command, "clear", &clear) && clear != 0)
        airportItlwmRegDiagClearPreservingControl(modeFlags, blockMask, sequence);
    else
        sRegDiag.lastControlSequence = sequence;

    sRegDiag.modeFlags = modeFlags;
    sRegDiag.blockMask = blockMask;
    airportItlwmRegDiagInitTrace();

    char ack[128];
    snprintf(ack, sizeof(ack), "seq=%u applied=1 mode=0x%x block=0x%x",
             sequence, modeFlags, blockMask);
    airportItlwmRegDiagPublishString(driver,
                                     AIRPORT_ITLWM_REGDIAG_CONTROL_ACK_PROPERTY,
                                     ack);
    airportItlwmRegDiagTrace(kAirportItlwmRegDiagTraceControl,
                             kAirportItlwmRegDiagPathUnknown,
                             kIOReturnSuccess, 0, modeFlags, blockMask);
}

static void
airportItlwmRegDiagFillSnapshot(AirportItlwm *driver)
{
    AirportItlwmRegDiagSnapshot snap;
    memset(&snap, 0, sizeof(snap));

    snap.version = AIRPORT_ITLWM_REGDIAG_ABI_VERSION;
    snap.size = sizeof(snap);
    snap.sequence = ++sRegDiag.snapshotSequence;
    snap.lastControlSequence = sRegDiag.lastControlSequence;
    snap.modeFlags = sRegDiag.modeFlags;
    snap.blockMask = sRegDiag.blockMask;

    snap.rtMask = sRT.rtMask;
    snap.rtMask2 = sRT.rtMask2;
    snap.rtMask3 = sRT.rtMask3;
    snap.powerState = driver != nullptr ? driver->power_state : 0;
    snap.pmPowerState = driver != nullptr ? driver->pmPowerState : 0;
    snap.currentStatus = driver != nullptr ? driver->currentStatus : 0;
    snap.currentSpeed = driver != nullptr ? driver->currentSpeed : 0;

    if (driver != nullptr) {
        snap.hasHalService = driver->fHalService != nullptr ? 1 : 0;
        snap.hasNetIf = driver->fNetIf != nullptr ? 1 : 0;
        snap.fNetIfPtr = reinterpret_cast<uint64_t>(driver->fNetIf);
        snap.fTxQueuePtr = reinterpret_cast<uint64_t>(driver->fTxQueue);
        snap.fRxQueuePtr = reinterpret_cast<uint64_t>(driver->fRxQueue);

        if (driver->fNetIf != nullptr) {
            ifnet_t bif = driver->fNetIf->getBSDInterface();
            snap.bsdIfPtr = reinterpret_cast<uint64_t>(bif);
            snap.hasBSDInterface = bif != nullptr ? 1 : 0;
            if (bif != nullptr)
                snap.ifFlags = ifnet_flags(bif);
        }

        if (driver->fHalService != nullptr) {
            struct ieee80211com *ic = driver->fHalService->get80211Controller();
            struct _ifnet *ifp = &ic->ic_ac.ac_if;
            snap.icState = ic->ic_state;
            snap.icFlags = ic->ic_flags;
            if (snap.ifFlags == 0)
                snap.ifFlags = ifp->if_flags;
            snap.nodeCount = ic->ic_nnodes;
            snap.desiredEssLen = airportItlwmRegDiagMinU32(ic->ic_des_esslen,
                                                           AIRPORT_ITLWM_REGDIAG_MAX_SSID_LEN);
            airportItlwmRegDiagCopyBytes(snap.desiredSsid,
                                         AIRPORT_ITLWM_REGDIAG_MAX_SSID_LEN,
                                         ic->ic_des_essid,
                                         snap.desiredEssLen);
            airportItlwmRegDiagCopyCString(snap.bsdName,
                                           AIRPORT_ITLWM_REGDIAG_MAX_NAME_LEN,
                                           ifp->if_xname);
            if (ic->ic_bss != nullptr) {
                snap.hasBss = 1;
                snap.currentSsidLen =
                    airportItlwmRegDiagMinU32(ic->ic_bss->ni_esslen,
                                              AIRPORT_ITLWM_REGDIAG_MAX_SSID_LEN);
                airportItlwmRegDiagCopyBytes(snap.currentSsid,
                                             AIRPORT_ITLWM_REGDIAG_MAX_SSID_LEN,
                                             ic->ic_bss->ni_essid,
                                             snap.currentSsidLen);
                airportItlwmRegDiagCopyBytes(snap.currentBssid,
                                             sizeof(snap.currentBssid),
                                             ic->ic_bss->ni_bssid,
                                             sizeof(snap.currentBssid));
            }
        }
    }

    snap.publicAssocCount = sRegDiag.snapshot.publicAssocCount;
    snap.hiddenAssocCount = sRegDiag.snapshot.hiddenAssocCount;
    snap.linkStateCount = sRegDiag.snapshot.linkStateCount;
    snap.txCount = sRegDiag.snapshot.txCount;
    snap.rxCount = sRegDiag.snapshot.rxCount;
    snap.eapolTxCount = sRegDiag.snapshot.eapolTxCount;
    snap.eapolRxCount = sRegDiag.snapshot.eapolRxCount;
    snap.txDropCount = sRegDiag.snapshot.txDropCount;
    snap.rxDropCount = sRegDiag.snapshot.rxDropCount;
    snap.blockHitCount = sRegDiag.snapshot.blockHitCount;
    snap.lastPublicAssocResult = sRegDiag.snapshot.lastPublicAssocResult;
    snap.lastHiddenAssocResult = sRegDiag.snapshot.lastHiddenAssocResult;
    snap.lastLinkStateResult = sRegDiag.snapshot.lastLinkStateResult;
    snap.lastTxResult = sRegDiag.snapshot.lastTxResult;
    snap.lastRxResult = sRegDiag.snapshot.lastRxResult;
    snap.lastLinkState = sRegDiag.snapshot.lastLinkState;
    snap.lastAssocAuthLower = sRegDiag.snapshot.lastAssocAuthLower;
    snap.lastAssocAuthUpper = sRegDiag.snapshot.lastAssocAuthUpper;
    snap.lastAssocRsnIeLen = sRegDiag.snapshot.lastAssocRsnIeLen;
    snap.lastTxLength = sRegDiag.snapshot.lastTxLength;
    snap.lastRxLength = sRegDiag.snapshot.lastRxLength;
    snap.lastBlockMask = sRegDiag.snapshot.lastBlockMask;
    snap.lastAssocSsidLen = sRegDiag.snapshot.lastAssocSsidLen;
    airportItlwmRegDiagCopyBytes(snap.lastAssocSsid,
                                 AIRPORT_ITLWM_REGDIAG_MAX_SSID_LEN,
                                 sRegDiag.snapshot.lastAssocSsid,
                                 snap.lastAssocSsidLen);
    airportItlwmRegDiagCopyBytes(snap.lastAssocBssid,
                                 sizeof(snap.lastAssocBssid),
                                 sRegDiag.snapshot.lastAssocBssid,
                                 sizeof(snap.lastAssocBssid));

    sRegDiag.snapshot = snap;
}

static bool
airportItlwmRegDiagMbufIsEapol(mbuf_t m, uint32_t *length)
{
    if (length != nullptr)
        *length = 0;
    if (m == nullptr || mbuf_type(m) == MBUF_TYPE_FREE)
        return false;

    const size_t len = mbuf_len(m);
    if (length != nullptr)
        *length = static_cast<uint32_t>(len);
    if (len < sizeof(ether_header_t) || mbuf_data(m) == nullptr)
        return false;

    ether_header_t *eh = reinterpret_cast<ether_header_t *>(mbuf_data(m));
    return eh->ether_type == htons(ETHERTYPE_PAE);
}

void
airportItlwmRegDiagTrace(uint32_t kind, uint32_t path, IOReturn result,
                         int32_t arg0, uint64_t arg1, uint64_t arg2)
{
    if ((sRegDiag.modeFlags & kAirportItlwmRegDiagModeEnabled) == 0)
        return;

    airportItlwmRegDiagInitTrace();
    const uint32_t sequence = sRegDiag.trace.nextSequence++;
    const uint32_t index = sequence % AIRPORT_ITLWM_REGDIAG_MAX_TRACE_ENTRIES;
    AirportItlwmRegDiagTraceEntry *entry = &sRegDiag.trace.entries[index];
    entry->version = AIRPORT_ITLWM_REGDIAG_ABI_VERSION;
    entry->sequence = sequence;
    entry->kind = kind;
    entry->path = path;
    entry->result = static_cast<int32_t>(result);
    entry->arg0 = arg0;
    entry->arg1 = arg1;
    entry->arg2 = arg2;
    if (sRegDiag.trace.entryCount < AIRPORT_ITLWM_REGDIAG_MAX_TRACE_ENTRIES)
        sRegDiag.trace.entryCount++;
    else
        sRegDiag.trace.droppedEntries++;

    if (airportItlwmRegDiagLogEnabled()) {
        XYLog("ITLWM_REGDIAG trace seq=%u kind=%u path=%u result=0x%x arg0=%d arg1=0x%llx arg2=0x%llx\n",
              sequence, kind, path, result, arg0, arg1, arg2);
    }
}

void
airportItlwmRegDiagRecordAssoc(uint32_t path, const uint8_t *ssid,
                               uint32_t ssidLen, const uint8_t *bssid,
                               uint32_t authLower, uint32_t authUpper,
                               uint32_t rsnIeLen, IOReturn result)
{
    if (!airportItlwmRegDiagEnabled(kAirportItlwmRegDiagModeAssoc))
        return;

    AirportItlwmRegDiagSnapshot *snap = &sRegDiag.snapshot;
    if (path == kAirportItlwmRegDiagPathHiddenAssoc) {
        snap->hiddenAssocCount++;
        snap->lastHiddenAssocResult = static_cast<int32_t>(result);
    } else {
        snap->publicAssocCount++;
        snap->lastPublicAssocResult = static_cast<int32_t>(result);
    }
    snap->lastAssocAuthLower = authLower;
    snap->lastAssocAuthUpper = authUpper;
    snap->lastAssocRsnIeLen = rsnIeLen;
    snap->lastAssocSsidLen =
        airportItlwmRegDiagMinU32(ssidLen, AIRPORT_ITLWM_REGDIAG_MAX_SSID_LEN);
    airportItlwmRegDiagCopyBytes(snap->lastAssocSsid,
                                 AIRPORT_ITLWM_REGDIAG_MAX_SSID_LEN,
                                 ssid, snap->lastAssocSsidLen);
    airportItlwmRegDiagCopyBytes(snap->lastAssocBssid,
                                 sizeof(snap->lastAssocBssid),
                                 bssid, sizeof(snap->lastAssocBssid));

    airportItlwmRegDiagTrace(path == kAirportItlwmRegDiagPathHiddenAssoc ?
                             kAirportItlwmRegDiagTraceHiddenAssoc :
                             kAirportItlwmRegDiagTracePublicAssoc,
                             path, result, static_cast<int32_t>(ssidLen),
                             (static_cast<uint64_t>(authUpper) << 32) | authLower,
                             rsnIeLen);
}

void
airportItlwmRegDiagRecordData(uint32_t path, uint32_t length, bool eapol,
                              IOReturn result)
{
    if (!airportItlwmRegDiagEnabled(kAirportItlwmRegDiagModeData))
        return;

    AirportItlwmRegDiagSnapshot *snap = &sRegDiag.snapshot;
    if (path == kAirportItlwmRegDiagPathRx) {
        snap->rxCount++;
        snap->lastRxResult = static_cast<int32_t>(result);
        snap->lastRxLength = length;
        if (result != kIOReturnSuccess)
            snap->rxDropCount++;
        if (eapol)
            snap->eapolRxCount++;
    } else {
        snap->txCount++;
        snap->lastTxResult = static_cast<int32_t>(result);
        snap->lastTxLength = length;
        if (result != kIOReturnOutputSuccess && result != kIOReturnSuccess)
            snap->txDropCount++;
        if (eapol)
            snap->eapolTxCount++;
    }

    airportItlwmRegDiagTrace(path == kAirportItlwmRegDiagPathRx ?
                             kAirportItlwmRegDiagTraceRx :
                             kAirportItlwmRegDiagTraceTx,
                             path, result, eapol ? 1 : 0, length, 0);
}

bool
airportItlwmRegDiagShouldBlock(uint32_t blockMask)
{
    return (sRegDiag.modeFlags & kAirportItlwmRegDiagModeEnabled) != 0 &&
           (sRegDiag.modeFlags & kAirportItlwmRegDiagModeIntervention) != 0 &&
           (sRegDiag.blockMask & blockMask) != 0;
}

void
airportItlwmRegDiagRecordBlock(uint32_t blockMask, uint32_t path,
                               uint32_t length)
{
    if ((sRegDiag.modeFlags & kAirportItlwmRegDiagModeEnabled) == 0)
        return;
    sRegDiag.snapshot.blockHitCount++;
    sRegDiag.snapshot.lastBlockMask = blockMask;
    if (path == kAirportItlwmRegDiagPathRx)
        sRegDiag.snapshot.rxDropCount++;
    if (path == kAirportItlwmRegDiagPathTx)
        sRegDiag.snapshot.txDropCount++;
    airportItlwmRegDiagTrace(kAirportItlwmRegDiagTraceBlock, path,
                             kIOReturnAborted,
                             static_cast<int32_t>(blockMask), length, 0);
    XYLog("ITLWM_REGDIAG block mask=0x%x path=%u length=%u\n",
          blockMask, path, length);
}

void
airportItlwmRegDiagPoll(AirportItlwm *driver)
{
    if (driver == nullptr)
        return;

    OSObject *control = driver->copyProperty(AIRPORT_ITLWM_REGDIAG_CONTROL_PROPERTY);
    if (control != nullptr) {
        if (OSString *command = OSDynamicCast(OSString, control))
            airportItlwmRegDiagApplyControl(driver, command->getCStringNoCopy());
        control->release();
    }

    if ((sRegDiag.modeFlags & kAirportItlwmRegDiagModeEnabled) == 0)
        return;

    airportItlwmRegDiagFillSnapshot(driver);
    airportItlwmRegDiagPublishData(driver,
                                   AIRPORT_ITLWM_REGDIAG_SNAPSHOT_PROPERTY,
                                   &sRegDiag.snapshot,
                                   sizeof(sRegDiag.snapshot));
    airportItlwmRegDiagPublishData(driver,
                                   AIRPORT_ITLWM_REGDIAG_TRACE_PROPERTY,
                                   &sRegDiag.trace,
                                   sizeof(sRegDiag.trace));
}

static int ieeeChanFlag2apple(int flags, int bw)
{
    int ret = 0;
    if (flags & IEEE80211_CHAN_2GHZ)
        ret |= APPLE80211_C_FLAG_2GHZ;
    if (flags & IEEE80211_CHAN_5GHZ)
        ret |= APPLE80211_C_FLAG_5GHZ;
    if (!(flags & IEEE80211_CHAN_PASSIVE))
        ret |= APPLE80211_C_FLAG_ACTIVE;
    if (flags & IEEE80211_CHAN_DFS)
        ret |= APPLE80211_C_FLAG_DFS;
    if (bw == -1) {
        if (flags & IEEE80211_CHAN_VHT) {
            if ((flags & IEEE80211_CHAN_VHT160) || (flags & IEEE80211_CHAN_VHT80_80))
                ret |= APPLE80211_C_FLAG_160MHZ;
            if (flags & IEEE80211_CHAN_VHT80)
                ret |= APPLE80211_C_FLAG_80MHZ;
        } else if ((flags & IEEE80211_CHAN_HT40) && (flags & IEEE80211_CHAN_HT)) {
            ret |= APPLE80211_C_FLAG_40MHZ;
            if (flags & IEEE80211_CHAN_HT40U)
                ret |= APPLE80211_C_FLAG_EXT_ABV;
        } else if (flags & IEEE80211_CHAN_HT20)
            ret |= APPLE80211_C_FLAG_20MHZ;
        else if ((flags & IEEE80211_CHAN_CCK) || (flags & IEEE80211_CHAN_OFDM))
            ret |= APPLE80211_C_FLAG_10MHZ;
    } else {
        switch (bw) {
            case IEEE80211_CHAN_WIDTH_80P80:
            case IEEE80211_CHAN_WIDTH_160:
                ret |= APPLE80211_C_FLAG_160MHZ;
                break;
            case IEEE80211_CHAN_WIDTH_80:
                ret |= APPLE80211_C_FLAG_80MHZ;
                break;
            case IEEE80211_CHAN_WIDTH_40:
                ret |= APPLE80211_C_FLAG_40MHZ;
                if (flags & IEEE80211_CHAN_HT40U)
                    ret |= APPLE80211_C_FLAG_EXT_ABV;
                break;
            case IEEE80211_CHAN_WIDTH_20:
                ret |= APPLE80211_C_FLAG_20MHZ;
                break;
            default:
                if (flags & IEEE80211_CHAN_HT20)
                    ret |= APPLE80211_C_FLAG_20MHZ;
                else if ((flags & IEEE80211_CHAN_CCK) || (flags & IEEE80211_CHAN_OFDM))
                    ret |= APPLE80211_C_FLAG_10MHZ;
                break;
        }
    }
    return ret;
}

static bool shouldRouteTahoeSkywalkIoctlReq(const apple80211req *req, bool isSet)
{
    if (req == nullptr || req->req_data == nullptr)
        return false;

    switch (req->req_type) {
        case APPLE80211_IOC_SSID:
        case APPLE80211_IOC_BSSID:
        case APPLE80211_IOC_CHANNEL:
        case APPLE80211_IOC_CURRENT_NETWORK:
            return !isSet;
        case APPLE80211_IOC_ROAM_PROFILE:
            // Apple exposes both visible GET and SET wrappers for ROAM_PROFILE.
            // The local interface owner already supports both directions via
            // processApple80211Ioctl(...), so keep the selector symmetric here.
            return true;
        case APPLE80211_IOC_ASSOCIATE:
        case APPLE80211_IOC_DISASSOCIATE:
        case APPLE80211_IOC_AUTH_TYPE:
        case APPLE80211_IOC_RSN_IE:
            return isSet;
        default:
            return false;
    }
}

static IOReturn routeTahoeSkywalkIoctl(IO80211SkywalkInterface *interface, void *data, UInt cmd)
{
    AirportItlwmSkywalkInterface *sky =
        OSDynamicCast(AirportItlwmSkywalkInterface, interface);
    apple80211req *req = static_cast<apple80211req *>(data);
    if (sky == nullptr || req == nullptr)
        return kIOReturnUnsupported;

    const bool isSet = (cmd == SIOCSA80211);
    if (!shouldRouteTahoeSkywalkIoctlReq(req, isSet))
        return kIOReturnUnsupported;

    return sky->processApple80211Ioctl(cmd, req);
}

namespace {

constexpr uint32_t kTahoeWclScanResultMaxIELen = 0x800;
constexpr uint32_t kTahoeWclScanResultHeaderLen = 0x44;

struct TahoeWclBeaconMetaData {
    uint32_t ieLen;               // 0x00
    uint16_t chanSpec;            // 0x04
    uint8_t ssid[32];             // 0x06
    uint8_t ssidLen;              // 0x26
    uint8_t primaryChannel;       // 0x27
    uint8_t reserved28;           // 0x28
    uint8_t bssid[6];             // 0x29
    uint8_t reserved2f;           // 0x2f
    int32_t rssi;                 // 0x30
    uint16_t reserved34;          // 0x34
    uint16_t reserved36;          // 0x36
    uint16_t beaconInterval;      // 0x38
    uint16_t capability;          // 0x3a
    uint32_t reserved3c;          // 0x3c
    uint32_t flags;               // 0x40
} __attribute__((packed));

static_assert(sizeof(TahoeWclBeaconMetaData) == kTahoeWclScanResultHeaderLen,
              "TahoeWclBeaconMetaData size must match Apple 0x44 header");

struct TahoeWclScanResultPayload {
    TahoeWclBeaconMetaData meta;
    uint8_t ie[kTahoeWclScanResultMaxIELen];
} __attribute__((packed));

static uint16_t buildTahoePrimaryChanSpec(struct ieee80211com *ic,
                                          const struct ieee80211_channel *chan)
{
    if (ic == nullptr || chan == nullptr || chan == IEEE80211_CHAN_ANYC)
        return 0;

    const uint16_t primary = static_cast<uint16_t>(ieee80211_chan2ieee(ic, chan) & 0xff);
    /*
     * AppleBCMWLANScanAdapter::processScanResults does not synthesize a local
     * chanspec format for the 0xC9 WCL scan-result carrier. It converts the
     * firmware chanspec through AppleBCMWLANChanSpec::getAppleChannelSpec()
     * first, then writes that Apple-visible 16-bit value into the metadata.
     *
     * For the FW<2 primary-20 path used by Tahoe scan results, the Apple
     * encoding is:
     *   2.4 GHz -> channel number
     *   5   GHz -> 0xc000 | channel number
     *
     * The previous local 0x1000 | (band << 14) | channel encoding is not
     * Apple-compatible and causes fresh WCL scan ingestion to collapse part of
     * the candidate set on the client-visible/UI plane.
     */
    if ((chan->ic_flags & IEEE80211_CHAN_5GHZ) != 0)
        return static_cast<uint16_t>(0xc000 | primary);
    return primary;
}

static bool buildTahoeWclScanResultPayload(struct ieee80211com *ic,
                                           struct ieee80211_node *ni,
                                           TahoeWclScanResultPayload *payload,
                                           uint32_t *payloadLen)
{
    if (ic == nullptr || ni == nullptr || payload == nullptr || payloadLen == nullptr ||
        ni->ni_chan == nullptr || ni->ni_chan == IEEE80211_CHAN_ANYC)
        return false;

    const uint16_t chanSpec = buildTahoePrimaryChanSpec(ic, ni->ni_chan);
    if (chanSpec == 0)
        return false;

    bzero(payload, sizeof(*payload));

    uint32_t ieLen = 0;
    if (ni->ni_rsnie_tlv != nullptr && ni->ni_rsnie_tlv_len != 0) {
        ieLen = MIN(ni->ni_rsnie_tlv_len,
                    static_cast<uint32_t>(kTahoeWclScanResultMaxIELen));
        memcpy(payload->ie, ni->ni_rsnie_tlv, ieLen);
    }
    payload->meta.ieLen = ieLen;
    payload->meta.chanSpec = chanSpec;
    payload->meta.ssidLen = MIN(static_cast<uint8_t>(sizeof(payload->meta.ssid)), ni->ni_esslen);
    if (payload->meta.ssidLen != 0) {
        memcpy(payload->meta.ssid, ni->ni_essid, payload->meta.ssidLen);
        // Consumer-side setBeaconDataFromMsg uses bits 1|2 as the "header SSID
        // is present" hint before it parses the raw beacon IEs.
        payload->meta.flags |= 0x6;
    }
    const uint16_t primaryChannel =
        static_cast<uint16_t>(ieee80211_chan2ieee(ic, ni->ni_chan));
    payload->meta.primaryChannel = static_cast<uint8_t>(MIN(primaryChannel, 0xff));
    memcpy(payload->meta.bssid, ni->ni_bssid, sizeof(payload->meta.bssid));
    payload->meta.rssi = -(0 - IWM_MIN_DBM - ni->ni_rssi);
    payload->meta.beaconInterval = ni->ni_intval;
    payload->meta.capability = ni->ni_capinfo;

    *payloadLen = kTahoeWclScanResultHeaderLen + ieLen;
    return true;
}

} // namespace

// Skywalk TX submission callback — called when BSD stack has packets to transmit.
// Must match poolOpts.bufferSize in AirportItlwm::start() pool creation.
#define SKYWALK_BUF_SIZE 2048

static void
setCoreWiFiDriverReadyProperty(AirportItlwm *controller, bool ready)
{
    if (controller == NULL || controller->fNetIf == NULL)
        return;

    // AppleBCMWLANCore::signalDriverReady() does not synthesize readiness by
    // fabricating an APPLE80211_M_DRIVER_AVAILABLE payload. The recovered
    // producer path publishes CoreWiFiDriverReadyKey="true"/"false" on the
    // hidden interface-side object stored at core-state +0x1510.
    //
    // Live d7318b6 proof showed the earlier synthetic bulletin path was wrong:
    // DRIVER_AVAILABLE/55 appeared in logs, yet isDriverAvailable stayed 0 and
    // ioreg exposed no CoreWiFiDriverReadyKey on AirportItlwmSkywalkInterface.
    //
    // Live 5e5d8da then proved a second, subtler divergence in our first
    // attempt at that producer: the property existed in ioreg, but as
    // `CoreWiFiDriverReadyKey = Yes` (OSBoolean) instead of Apple's
    // OSString("true"/"false").  isDriverAvailable still remained 0, so the
    // type is part of the contract, not just the key name.
    const OSSymbol *key = OSSymbol::withCString("CoreWiFiDriverReadyKey");
    OSString *value = OSString::withCString(ready ? "true" : "false");
    if (key != NULL && value != NULL)
        controller->fNetIf->setProperty(key, value);
    OSSafeReleaseNULL(value);
    OSSafeReleaseNULL(key);
    XYLog("DEBUG %s ready=%d fNetIf=%p\n", __FUNCTION__, ready ? 1 : 0,
          controller->fNetIf);
}

static void
postTahoeDriverAvailableBulletin(AirportItlwm *controller, bool ready)
{
    if (controller == NULL || controller->fNetIf == NULL)
        return;

    // Live 43bf34f runtime proved that the hidden interface-enable subclass
    // body plus CoreWiFiDriverReadyKey still do not flip Tahoe availability
    // locally: `setInterfaceEnable(true)` runs, `CoreWiFiDriverReadyKey` is
    // visible in ioreg, scan reaches WCL_SCAN_DONE, yet IO80211Family keeps
    // reporting isDriverAvailable=<0> and no APPLE80211_M_DRIVER_AVAILABLE
    // posts appear in the current-boot kernel log.
    //
    // That matches the recovered family-side consumer contract exactly.
    // WCLSystemStateManager::driverAvailableEventHandler(...) accepts the
    // bulletin only when:
    // - message code is APPLE80211_M_DRIVER_AVAILABLE (0x37)
    // - payload length is exactly 0xf8
    // - the dword at payload +0x8 is NON-zero for the available edge
    // - the dword at payload +0x8 is zero for the unavailable edge
    //
    // The earlier local port inverted this polarity (`ready=true` published
    // `available=0`). The family-side handler then called processEvent(...,4),
    // and the recovered SSM matrix defines event 4 as DRIVER_UNAVAILABLE and
    // event 5 as DRIVER_AVAILABLE. So the old local bulletin was explicitly
    // feeding the opposite edge into WCL.
    //
    // AppleBCMWLANCore::signalDriverReady() itself only publishes
    // CoreWiFiDriverReadyKey, so the separate availability bulletin must still
    // be reproduced at the same ready transition boundary. Deliver it through
    // controller->postMessage(..., true) so the event flows through
    // IO80211Controller/PostOffice instead of bypassing the framework.
    apple80211_driver_available_data data = {};
    data.event = APPLE80211_M_DRIVER_AVAILABLE;
    data.avaliable = ready ? 1 : 0;
    data.reason = 0;
    data.sub_reason = 0;

    controller->postMessage(controller->fNetIf, APPLE80211_M_DRIVER_AVAILABLE,
                            &data, sizeof(data), true);
    XYLog("DEBUG %s ready=%d len=0x%zx available=%llu fNetIf=%p\n",
          __FUNCTION__, ready ? 1 : 0, sizeof(data), data.avaliable,
          controller->fNetIf);
}

static void
applyTahoeInterfaceReadyEdge(AirportItlwm *controller, bool ready)
{
    if (controller == NULL || controller->fNetIf == NULL)
        return;

    // The newer Tahoe decompile corrected the previous "property+broadcast"
    // theory.  AppleBCMWLANCore does not synthesize DRIVER_AVAILABLE itself.
    // The recovered caller order is:
    //   1) hidden interface-side +0x930 -> setInterfaceEnable(true)
    //   2) AppleBCMWLANCore::signalDriverReady()
    // and on down/error paths:
    //   1) hidden interface-side +0x920 -> interfaceAdvisoryEnable(...)
    //   2) AppleBCMWLANCore::signalDriverReady()
    //
    // So the Apple producer edge is an interface lifecycle callback first,
    // not a controller-local synthetic bulletin.  The local Tahoe port models
    // the hidden +0x1510 object with fNetIf, which exposes the same
    // IOSkywalkNetworkInterface slots.
    if (ready) {
        SInt32 ret = controller->fNetIf->setInterfaceEnable(true);
        XYLog("DEBUG %s ready-edge ret=0x%x fNetIf=%p\n",
              __FUNCTION__, ret, controller->fNetIf);
    } else {
        IOReturn ret = controller->fNetIf->interfaceAdvisoryEnable(false);
        XYLog("DEBUG %s advisory-edge ret=0x%x fNetIf=%p\n",
              __FUNCTION__, ret, controller->fNetIf);
    }
}

static void
publishTahoeDriverReadyState(AirportItlwm *controller, bool ready)
{
    applyTahoeInterfaceReadyEdge(controller, ready);
    setCoreWiFiDriverReadyProperty(controller, ready);
    postTahoeDriverAvailableBulletin(controller, ready);
}

// Apple's bootChipImage is triggered asynchronously by AppleBCMWLANUserClient.
// The thread_call handler routes through the command gate for serialization,
// matching the framework's expected execution context.
static IOReturn
performTahoeBootChipImageGated(OSObject *owner, void *, void *, void *, void *)
{
    static_cast<AirportItlwm *>(owner)->performTahoeBootChipImage();
    return kIOReturnSuccess;
}

static void
handleTahoeBootChipImage(thread_call_param_t param0, thread_call_param_t)
{
    AirportItlwm *self = (AirportItlwm *)param0;
    XYLog("DEBUG %s entry\n", __FUNCTION__);
    IOCommandGate *gate = self->getCommandGate();
    if (gate) {
        gate->runAction(performTahoeBootChipImageGated);
    }
}

void AirportItlwm::performTahoeBootChipImage()
{
    XYLog("DEBUG %s entry power_state=%u\n", __FUNCTION__, power_state);
    setLinkStatus(kIONetworkLinkValid);
    if (TAILQ_EMPTY(&fHalService->get80211Controller()->ic_ess))
        fHalService->get80211Controller()->ic_flags |= IEEE80211_F_AUTO_JOIN;
    power_state = kWiFiPowerOn;
    XYLog("DEBUG %s enabling adapter, power_state=%u\n", __FUNCTION__, power_state);
    enableAdapter(NULL);
    {
        struct ieee80211com *ic_dbg = fHalService->get80211Controller();
        struct _ifnet *ifp_dbg = &ic_dbg->ic_ac.ac_if;
        XYLog("DEBUG %s post-enable: ic_state=%d if_flags=0x%x\n",
              __FUNCTION__, ic_dbg->ic_state, ifp_dbg->if_flags);
    }
    publishTahoeDriverReadyState(this, true);
    XYLog("DEBUG %s readiness published\n", __FUNCTION__);
}

bool AirportItlwmBootNub::start(IOService *provider)
{
    if (!IOService::start(provider)) return false;
    AirportItlwm *controller = OSDynamicCast(AirportItlwm, provider);
    if (!controller) {
        stop(provider);
        return false;
    }
    // Apple's AppleBCMWLANUserClient triggers bootChipImage after IOKit
    // matches it against the controller.  This nub replicates that pattern:
    // IOKit matched us against AirportItlwm, now trigger the async boot.
    if (controller->tahoeBootThreadCall) {
        XYLog("AirportItlwmBootNub: triggering async boot\n");
        thread_call_enter(controller->tahoeBootThreadCall);
    }
    return true;
}

static IORegistryEntry *
getTahoeHiddenInterfaceObject(AirportItlwm *controller)
{
    if (controller == NULL)
        return NULL;

    // The recovered +0x1510 object behaves as an interface-side registry
    // facade for every system-visible path we have lifted so far:
    // - CoreWiFiDriverReadyKey publication (+0x9f8)
    // - provider-backed property acquisition used by PLATFORM_CONFIG (+0x970
    //   plus property fetch helpers)
    // - timesync text publication (+0xad8) already modeled in the Skywalk
    //   interface as an engine-missing report
    //
    // On the local Tahoe port the only interface-side object with that same
    // system-facing contract is `fNetIf`. Using the controller itself here
    // would skip the Apple-shaped interface registry surface and regress the
    // recovered producer target back into a controller-local shortcut.
    if (controller->fNetIf != NULL)
        return controller->fNetIf;
    return controller;
}

static IORegistryEntry *
getTahoeHiddenInterfaceProvider(AirportItlwm *controller)
{
    IORegistryEntry *entry = getTahoeHiddenInterfaceObject(controller);
    if (IOService *service = OSDynamicCast(IOService, entry)) {
        IORegistryEntry *provider = service->getProvider();
        if (provider != NULL)
            return provider;
    }
    if (controller != NULL)
        return controller->getProvider();
    return NULL;
}

static bool
copyBoolProperty(IORegistryEntry *entry, const char *name, bool *value)
{
    if (entry == NULL || name == NULL || value == NULL)
        return false;

    OSObject *obj = entry->copyProperty(name);
    if (obj == NULL)
        return false;

    bool found = false;
    if (OSBoolean *b = OSDynamicCast(OSBoolean, obj)) {
        *value = b->isTrue();
        found = true;
    } else if (OSNumber *n = OSDynamicCast(OSNumber, obj)) {
        *value = n->unsigned32BitValue() != 0;
        found = true;
    }

    obj->release();
    return found;
}

static bool
copyPresenceProperty(IORegistryEntry *entry, const char *name)
{
    if (entry == NULL || name == NULL)
        return false;

    OSObject *obj = entry->copyProperty(name);
    if (obj == NULL)
        return false;

    bool present = true;
    obj->release();
    return present;
}

static bool
copyUInt32Property(IORegistryEntry *entry, const char *name, uint32_t *value)
{
    if (entry == NULL || name == NULL || value == NULL)
        return false;

    OSObject *obj = entry->copyProperty(name);
    if (obj == NULL)
        return false;

    bool found = false;
    if (OSNumber *n = OSDynamicCast(OSNumber, obj)) {
        *value = n->unsigned32BitValue();
        found = true;
    } else if (OSBoolean *b = OSDynamicCast(OSBoolean, obj)) {
        *value = b->isTrue() ? 1u : 0u;
        found = true;
    }

    obj->release();
    return found;
}

// Inline replacements for kern_buflet_* C KPI functions.
// The kernel exports these symbols (T in BootKC), but no KPI re-exports
// them for third-party kexts linked into the AuxKC.  These helpers access
// the identical struct fields directly, compiling as inline loads/stores
// with no symbol dependency.  Field layouts come from the SDK headers:
//   __buflet        (skywalk/packet/os_packet_private.h)
//   __kern_buflet   (skywalk/packet/packet_var.h)
//   skmem_bufctl    (skywalk/mem/skmem_cache_var.h)
static inline void *
_buflet_get_object_address(kern_buflet_t buf)
{
    return buf->buf_ctl->bc_addr;  // == kern_buflet_get_object_address
}
static inline uint32_t
_buflet_get_object_limit(kern_buflet_t buf)
{
    return buf->buf_ctl->bc_lim;   // == kern_buflet_get_object_limit
}
static inline uint16_t
_buflet_get_data_offset(kern_buflet_t buf)
{
    return buf->buf_com.__doff;    // == kern_buflet_get_data_offset
}
static inline uint16_t
_buflet_get_data_length(kern_buflet_t buf)
{
    return buf->buf_com.__dlen;    // == kern_buflet_get_data_length
}
static inline void
_buflet_set_data_offset(kern_buflet_t buf, uint16_t off)
{
    buf->buf_com.__doff = off;     // == kern_buflet_set_data_offset
}
static inline void
_buflet_set_data_length(kern_buflet_t buf, uint16_t len)
{
    buf->buf_com.__dlen = len;     // == kern_buflet_set_data_length
}

// The Skywalk nexus delivers packets from the BSD ifnet TX path as
// IOSkywalkPacket objects. We extract the frame data from each packet's
// buflet, copy it into an mbuf, and send it through the existing
// outputPacket path which enqueues to the hardware via if_snd.
//
// Return type is unsigned int (not IOReturn/int) to match kernel ABI.
// Kernel symbol uses mangled 'j' (unsigned int), not 'i' (int).
// Packet param is IOSkywalkPacket * const * (PKP mangling) — the array
// entries are const, but the packets themselves are mutable.
static unsigned int
skywalkTxAction(OSObject *owner, IOSkywalkTxSubmissionQueue *queue,
                IOSkywalkPacket * const *packets, UInt32 count, void *refCon)
{
    AirportItlwm *that = OSDynamicCast(AirportItlwm, owner);
    if (!that) return 0;

    sRT.txCbCnt++;
    UInt32 sent = 0;
    for (UInt32 i = 0; i < count; i++) {
        IOSkywalkPacket *pkt = packets[i];

        // Get the first packet buffer to access the underlying buflet
        IOSkywalkPacketBuffer *bufs[1] = { NULL };
        UInt32 nBufs = pkt->getPacketBuffers(bufs, 1);
        if (nBufs == 0 || !bufs[0]) {
            sRT.txPktDrop++;
            if (sRT.txCbCnt <= 3)
                XYLog("skywalkTxAction: pkt %u/%u no buffers\n", i, count);
            continue;
        }

        kern_buflet_t buflet = bufs[0]->mBufletHandle;
        if (!buflet) { sRT.txPktDrop++; continue; }

        // Use inline struct accessors (_buflet_*) instead of kern_buflet_*
        // C KPI — see comment above for why.
        void *objAddr = _buflet_get_object_address(buflet);
        uint16_t dataOff = _buflet_get_data_offset(buflet);
        uint16_t dataLen = _buflet_get_data_length(buflet);

        if (!objAddr || dataLen == 0) { sRT.txPktDrop++; continue; }

        // Allocate an mbuf with packet header and copy the Ethernet frame
        mbuf_t m = NULL;
        if (mbuf_allocpacket(MBUF_DONTWAIT, dataLen, NULL, &m) != 0) {
            sRT.txPktDrop++;
            continue;
        }

        if (mbuf_copyback(m, 0, dataLen, (uint8_t *)objAddr + dataOff, MBUF_DONTWAIT) != 0) {
            mbuf_freem(m);
            sRT.txPktDrop++;
            continue;
        }

        that->outputPacket(m, NULL);
        sent++;
    }

    sRT.txPktSent += sent;
    if (sRT.txCbCnt <= 3 && count > 0)
        XYLog("skywalkTxAction: count=%u sent=%u (total tx=%u)\n", count, sent, sRT.txPktSent);
    return sent;
}

// Skywalk RX completion callback — notification from the Skywalk subsystem
// after it has consumed packets that the driver enqueued to the RX queue.
// This is a completion notification, not a receive path — the actual RX
// injection happens in skywalkRxInput() called from _if_input().
// Return type is unsigned int to match kernel ABI (see TX callback comment).
static unsigned int
skywalkRxAction(OSObject *owner, IOSkywalkRxCompletionQueue *queue,
                IOSkywalkPacket **packets, UInt32 count, void *refCon)
{
    sRT.rxCbCnt++;
    return count;
}

#if __IO80211_TARGET >= __MAC_26_0
// Skywalk RX input handler — called from _if_input() on the Sequoia path.
// Converts an mbuf (received from the 802.11 stack) into an IOSkywalkPacket
// and enqueues it to the RX completion queue for delivery to the BSD stack
// through the Skywalk nexus.
static int
skywalkRxInput(struct _ifnet *ifp, mbuf_t m)
{
    AirportItlwm *that = OSDynamicCast(AirportItlwm, ifp->controller);
    if (!that || !that->fRxPool || !that->fRxQueue) {
        mbuf_freem(m);
        return ENXIO;
    }

    sRT.rxInputCnt++;

    size_t len = mbuf_pkthdr_len(m);
    if (len == 0) {
        airportItlwmRegDiagRecordData(kAirportItlwmRegDiagPathRx, 0, false,
                                      static_cast<IOReturn>(EINVAL));
        mbuf_freem(m);
        return EINVAL;
    }
    uint32_t diagLength = static_cast<uint32_t>(len);
    const bool diagEapol = airportItlwmRegDiagMbufIsEapol(m, &diagLength);
    if (diagEapol && airportItlwmRegDiagShouldBlock(kAirportItlwmRegDiagBlockEapolRx)) {
        airportItlwmRegDiagRecordBlock(kAirportItlwmRegDiagBlockEapolRx,
                                       kAirportItlwmRegDiagPathRx, diagLength);
        airportItlwmRegDiagRecordData(kAirportItlwmRegDiagPathRx, diagLength,
                                      true, static_cast<IOReturn>(EIO));
        mbuf_freem(m);
        return 0;
    }
    if (airportItlwmRegDiagShouldBlock(kAirportItlwmRegDiagBlockRx)) {
        airportItlwmRegDiagRecordBlock(kAirportItlwmRegDiagBlockRx,
                                       kAirportItlwmRegDiagPathRx, diagLength);
        airportItlwmRegDiagRecordData(kAirportItlwmRegDiagPathRx, diagLength,
                                      diagEapol, static_cast<IOReturn>(EIO));
        mbuf_freem(m);
        return 0;
    }

    // Allocate an IOSkywalkPacket from the RX pool
    IOSkywalkPacket *rxPkt = NULL;
    if (that->fRxPool->allocatePacket(1, &rxPkt, 0) != kIOReturnSuccess || !rxPkt) {
        sRT.rxAllocFail++;
        if (sRT.rxAllocFail <= 5)
            XYLog("skywalkRxInput: allocatePacket failed (drop #%u)\n", sRT.rxAllocFail);
        airportItlwmRegDiagRecordData(kAirportItlwmRegDiagPathRx, diagLength,
                                      diagEapol, static_cast<IOReturn>(ENOMEM));
        mbuf_freem(m);
        return ENOMEM;
    }

    // Get the packet buffer to access the underlying buflet
    IOSkywalkPacketBuffer *bufs[1] = { NULL };
    UInt32 nBufs = rxPkt->getPacketBuffers(bufs, 1);
    if (nBufs == 0 || !bufs[0]) {
        that->fRxPool->deallocatePacket(rxPkt);
        airportItlwmRegDiagRecordData(kAirportItlwmRegDiagPathRx, diagLength,
                                      diagEapol, static_cast<IOReturn>(ENOMEM));
        mbuf_freem(m);
        return ENOMEM;
    }

    // Use inline struct accessors (_buflet_*) instead of kern_buflet_*
    // C KPI — see comment at top of file for why.
    kern_buflet_t buflet = bufs[0]->mBufletHandle;
    void *objAddr = _buflet_get_object_address(buflet);
    uint32_t bufSize = _buflet_get_object_limit(buflet);
    if (!objAddr || len > bufSize) {
        that->fRxPool->deallocatePacket(rxPkt);
        airportItlwmRegDiagRecordData(kAirportItlwmRegDiagPathRx, diagLength,
                                      diagEapol, static_cast<IOReturn>(EMSGSIZE));
        mbuf_freem(m);
        return EMSGSIZE;
    }

    // Copy the mbuf data into the Skywalk packet buffer
    mbuf_copydata(m, 0, len, objAddr);
    _buflet_set_data_offset(buflet, 0);
    _buflet_set_data_length(buflet, (uint16_t)len);
    rxPkt->setDataLength((UInt32)len);

    // Enqueue the packet to the RX completion queue
    const IOSkywalkPacket *pktArray[] = { rxPkt };
    IOReturn ret = that->fRxQueue->enqueuePackets(pktArray, 1, 0);

    mbuf_freem(m);

    if (ret != kIOReturnSuccess) {
        sRT.rxEnqFail++;
        if (sRT.rxEnqFail <= 5)
            XYLog("skywalkRxInput: enqueuePackets failed 0x%x (drop #%u)\n", ret, sRT.rxEnqFail);
        airportItlwmRegDiagRecordData(kAirportItlwmRegDiagPathRx, diagLength,
                                      diagEapol, ret);
        return EIO;
    }

    sRT.rxPktOK++;
    airportItlwmRegDiagRecordData(kAirportItlwmRegDiagPathRx, diagLength,
                                  diagEapol, kIOReturnSuccess);

    return 0;
}
#endif /* __IO80211_TARGET >= __MAC_26_0 */

void AirportItlwm::releaseAll()
{
    XYLog("DEBUG %s [1] logStream=%p(rc=%d) logPipe=%p dataPath=%p snapshots=%p faultReporter=%p\n",
          __FUNCTION__, driverLogStream, driverLogStream ? driverLogStream->getRetainCount() : -1,
          driverLogPipe, driverDataPathPipe, driverSnapshotsPipe, driverFaultReporter);

    // CRITICAL: Stop all timers FIRST, before releasing fHalService.
    // watchdogTimer runs on fWatchdogWorkLoop (a separate thread).
    // If fHalService is freed while watchdogAction is in flight,
    // the use-after-free chain fHalService→get80211Controller()→
    // ic_ac.ac_if→if_watchdog dereferences freed memory → panic14
    // (RIP=0x0 via IOTimerEventSource::timeoutSignaled).
    if (fWatchdogWorkLoop && watchdogTimer) {
        XYLog("DEBUG %s [1a] stopping watchdogTimer=%p on fWatchdogWorkLoop=%p\n",
              __FUNCTION__, watchdogTimer, fWatchdogWorkLoop);
        watchdogTimer->cancelTimeout();
        watchdogTimer->disable();
        fWatchdogWorkLoop->removeEventSource(watchdogTimer);
        watchdogTimer->release();
        watchdogTimer = NULL;
        fWatchdogWorkLoop->release();
        fWatchdogWorkLoop = NULL;
    }
    if (_fWorkloop && scanSource) {
        XYLog("DEBUG %s [1b] stopping scanSource=%p\n", __FUNCTION__, scanSource);
        scanSource->cancelTimeout();
        scanSource->disable();
        _fWorkloop->removeEventSource(scanSource);
        scanSource->release();
        scanSource = NULL;
    }

    OSSafeReleaseNULL(driverLogStream);
    OSSafeReleaseNULL(driverLogPipe);
    OSSafeReleaseNULL(driverDataPathPipe);
    OSSafeReleaseNULL(driverSnapshotsPipe);
    OSSafeReleaseNULL(driverFaultReporter);
    if (io80211FaultReporter) {
        io80211FaultReporter->release();
        io80211FaultReporter = NULL;
    }
    if (fHalService) {
        XYLog("DEBUG %s [2] releasing fHalService=%p\n", __FUNCTION__, fHalService);
        fHalService->release();
        fHalService = NULL;
    }
    if (_fWorkloop) {
        if (_fCommandGate) {
            XYLog("DEBUG %s [3] removing _fCommandGate=%p from _fWorkloop=%p\n", __FUNCTION__, _fCommandGate, _fWorkloop);
            _fWorkloop->removeEventSource(_fCommandGate);
            _fCommandGate->release();
            _fCommandGate = NULL;
        }
        XYLog("DEBUG %s [6] releasing _fWorkloop=%p\n", __FUNCTION__, _fWorkloop);
        _fWorkloop->release();
        _fWorkloop = NULL;
    }
    XYLog("DEBUG %s [7] unregistPM\n", __FUNCTION__);
    unregistPM();
    XYLog("DEBUG %s DONE\n", __FUNCTION__);
}

IOReturn AirportItlwm::
postMessageGated(OSObject *target, void *arg0, void *arg1, void *arg2, void *arg3)
{
    RT_SET(4);
    RT2_SET(15);
    sRT.postMsgCount++;
    AirportItlwm *that = OSDynamicCast(AirportItlwm, target);
    if (!that || !that->fNetIf) {
        XYLog("DEBUG %s SKIP: that=%p fNetIf=%p\n", __FUNCTION__, that,
              that ? that->fNetIf : NULL);
        return kIOReturnNotReady;
    }
    UInt32 msg = (UInt32)(uintptr_t)arg0;
    sRT.lastPostMsg = msg;
    // Use controller-level postMessage — routes through IO80211Controller
    // dispatch (like Apple's postMessageInfra), NOT through
    // IO80211InfraInterface::postMessage which calls
    // updateCountryCodeProperty → sendIOUCToWcl (panics on workloop).
    // arg1 = optional data pointer, arg2 = optional data length
    // (reference: scanComplete passes &status/4, others pass NULL/0).
    /*
     * Diagnose IO80211 framework internal state:
     *
     * IO80211Controller::postMessage dispatches through PostOffice:
     *   controller->expansion[0xb10]->vtable[36](iface, msg, data, len, async)
     * The PostOffice then calls IO80211SkywalkInterface::postMessageInternal
     * which checks *(iface+0x120)+0xd8 — the IO80211Glue event filter object.
     *
     * If PostOffice is NULL → crash (NULL deref in controller dispatch).
     * If IO80211Glue is NULL → async events silently fall through to
     *   postMessageSync which is a no-op for event delivery.
     *
     * See disassembly: IO80211Controller::postMessage @ 0xffffff8002219ffe
     *   mov rax,[rdi+0x120]; mov rdi,[rax+0xb10]; jmp [vtable+0x120]
     * See decompile: FUN_ffffff80022772b2 (postMessageInternal)
     */
    uint8_t *ctrlBase = (uint8_t *)that;
    void *ctrlExpansion = *(void **)(ctrlBase + 0x120);
    void *postOffice = ctrlExpansion ? *(void **)((uint8_t *)ctrlExpansion + 0xb10) : NULL;
    uint8_t *ifBase = (uint8_t *)that->fNetIf;
    void *ifExpansion = *(void **)(ifBase + 0x120);
    void *glueObj = ifExpansion ? *(void **)((uint8_t *)ifExpansion + 0xd8) : NULL;
    XYLog("DEBUG %s msg=%u fNetIf=%p postOffice=%p ifExpansion=%p glue=%p dataLen=%u\n",
          __FUNCTION__, msg, that->fNetIf, postOffice, ifExpansion, glueObj,
          (unsigned int)(uintptr_t)arg2);
    that->postMessage(that->fNetIf, msg, arg1, (unsigned int)(uintptr_t)arg2, true);
    RT_SET(5);
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
postWclScanResultsGated(OSObject *target, void *arg0, void *arg1, void *arg2, void *arg3)
{
    AirportItlwm *that = OSDynamicCast(AirportItlwm, target);
    if (!that || !that->fNetIf || !that->fHalService) {
        XYLog("DEBUG %s SKIP that=%p fNetIf=%p hal=%p\n", __FUNCTION__, that,
              that ? that->fNetIf : nullptr, that ? that->fHalService : nullptr);
        return kIOReturnNotReady;
    }

    struct ieee80211com *ic = that->fHalService->get80211Controller();
    if (ic == nullptr)
        return kIOReturnNotReady;

    TahoeWclScanResultPayload payload;
    uint32_t payloadLen = 0;
    uint32_t posted = 0;

    struct ieee80211_node *ni;
    RB_FOREACH(ni, ieee80211_tree, &ic->ic_tree) {
        if (!buildTahoeWclScanResultPayload(ic, ni, &payload, &payloadLen))
            continue;
        that->postMessage(that->fNetIf, APPLE80211_M_WCL_SCAN_RESULT,
                          &payload, payloadLen, true);
        posted++;
    }

    UInt32 status = 0;
    that->postMessage(that->fNetIf, APPLE80211_M_WCL_SCAN_DONE,
                      &status, sizeof(status), true);
    XYLog("DEBUG %s posted scanResults=%u scanDone=1 nodes=%u\n",
          __FUNCTION__, posted, ic->ic_nnodes);
    return kIOReturnSuccess;
}

void AirportItlwm::
eventHandler(struct ieee80211com *ic, int msgCode, void *data)
{
    AirportItlwm *that = OSDynamicCast(AirportItlwm, ic->ic_ac.ac_if.controller);
    RT_SET(0);
    sRT.evtCount++;
    sRT.lastEvtCode = msgCode;
    sRT.ic_state = ic->ic_state;
    sRT.if_flags = ic->ic_ac.ac_if.if_flags;
    XYLog("DEBUG %s msgCode=%d ic_state=%d if_flags=0x%x power_state=%u\n",
          __FUNCTION__, msgCode, ic->ic_state, ic->ic_ac.ac_if.if_flags, that->power_state);
    if (!that || !that->fNetIf) {
        XYLog("DEBUG %s SKIP: interface=NULL\n", __FUNCTION__);
        return;
    }
    UInt32 apple80211Msg;
    void *msgData = NULL;
    unsigned int msgDataLen = 0;
    static UInt32 scanStatus;  // static — must survive until postMessageGated runs
    switch (msgCode) {
        case IEEE80211_EVT_COUNTRY_CODE_UPDATE:
            RT_SET(1);
            apple80211Msg = APPLE80211_M_COUNTRY_CODE_CHANGED;
            break;
        case IEEE80211_EVT_STA_ASSOC_DONE:
            RT_SET(2);
            apple80211Msg = APPLE80211_M_ASSOC_DONE;
            break;
        case IEEE80211_EVT_STA_DEAUTH:
            RT_SET(3);
            apple80211Msg = APPLE80211_M_DEAUTH_RECEIVED;
            break;
        case IEEE80211_EVT_SCAN_DONE:
            // Reference: AppleBCMWLANCore::scanComplete calls
            // postMessage(infra, 10, &status, 4, 1) directly — no timer.
            RT_SET(25);
            sRT.scanDoneCount++;
            apple80211Msg = APPLE80211_M_SCAN_DONE;
            scanStatus = data ? *(UInt32 *)data : 0;
            msgData = &scanStatus;
            msgDataLen = sizeof(scanStatus);
            break;
        default:
            XYLog("DEBUG %s UNHANDLED msgCode=%d\n", __FUNCTION__, msgCode);
            return;
    }
    // Defer postMessage to workloop context — cannot call from interrupt thread.
    that->getCommandGate()->runAction(postMessageGated,
        (void *)(uintptr_t)apple80211Msg, msgData, (void *)(uintptr_t)msgDataLen);
}

void AirportItlwm::watchdogAction(IOTimerEventSource *timer)
{
    // Guard: watchdogAction runs on fWatchdogWorkLoop (separate thread).
    // During releaseAll(), fHalService may be freed before the timer is
    // fully cancelled.  Dereferencing freed fHalService causes a
    // use-after-free chain that ends in calling a NULL if_watchdog
    // function pointer (panic14: RIP=0x0, CR2=0x0).
    if (!fHalService)
        return;
    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
    struct ieee80211com *ic = fHalService->get80211Controller();
    RT_SET(8);
    sRT.wdCount++;
    sRT.ic_state = ic->ic_state;
    sRT.if_flags = ifp->if_flags;
    sRT.power_state = power_state;
    sRT.linkStatus = currentStatus;
    sRT.ic_flags = ic->ic_flags;
    sRT.ic_des_esslen = ic->ic_des_esslen;
    if (fNetIf) {
        ifnet_t bif = fNetIf->getBSDInterface();
        sRT.bsdIfPtr = (uint64_t)(uintptr_t)bif;
        if (bif) {
            sRT.bsdIfFlags = ifnet_flags(bif);
            sRT.bsdIfMtu = ifnet_mtu(bif);
        }
    }
    // Read node count — ic_nnodes is maintained by ieee80211_alloc_node/free_node.
    // RB_FOREACH was here previously but watchdogAction runs on fWatchdogWorkLoop
    // (separate from _fWorkloop that serializes 80211 node operations), so iterating
    // ic_tree without the command gate is a data race — panic13 CR2=0xc800000000.
    // Reading a single int is safe without locking.
    sRT.nodeCount = (uint32_t)ic->ic_nnodes;
    static int wd_count = 0;
    static int wd_last_state = -1;
    wd_count++;
    if (wd_count <= 30 || wd_count % 10 == 0 || ic->ic_state != wd_last_state) {
        XYLog("DEBUG %s [%d] ic_state=%d if_flags=0x%x power_state=%u pmPowerState=%u link=0x%x\n",
              __FUNCTION__, wd_count, ic->ic_state, ifp->if_flags, power_state, pmPowerState, currentStatus);
        wd_last_state = ic->ic_state;
    }
    airportItlwmRegDiagPoll(this);
    (*ifp->if_watchdog)(ifp);
    watchdogTimer->setTimeoutMS(kWatchDogTimerPeriod);
}

void AirportItlwm::fakeScanDone(OSObject *owner, IOTimerEventSource *sender)
{
    RT_SET(13);
    sRT.scanCount++;
    AirportItlwm *that = (AirportItlwm *)owner;
    struct ieee80211com *ic = that->fHalService->get80211Controller();
    /* Reset SCAN_RESULT iterator so airportd reads from the beginning */
    that->fNextNodeToSend = NULL;
    that->fScanResultWrapping = false;

    /*
     * Tahoe WCL scans do not complete through the generic Core::scanComplete()
     * bulletin. The Apple scan-adapter path emits a per-BSS WCL scan-result
     * bulletin (0xC9, BeaconMetaData + raw tagged IEs) before the scan-owned
     * completion bulletin (0xED, 4-byte status).  0xED alone leaves the
     * framework scan cache empty even when ic_tree already has nodes.
     *
     * The local port already preserves the full tagged-IE tail in
     * ni_rsnie_tlv. Rebuild the Apple-shaped 0x44 metadata carrier from the
     * node cache, seed the primary 20 MHz chanSpec, let IO80211 parse the raw
     * HT/VHT/HE operation IEs, then post the real WCL completion bulletin.
     */
    XYLog("DEBUG %s ic_state=%d nodes=%u posting WCL_SCAN_RESULT (0xC9) + WCL_SCAN_DONE (0xED)\n",
          __FUNCTION__, ic->ic_state, ic->ic_nnodes);

    if (that->getCommandGate() != nullptr) {
        that->getCommandGate()->runAction(postWclScanResultsGated,
            nullptr, nullptr, nullptr, nullptr);
        return;
    }

    postWclScanResultsGated(that, nullptr, nullptr, nullptr, nullptr);
}

bool AirportItlwm::isCommandProhibited(int command)
{
    RT_SET(11);
    sRT.ioctlCount++;
    sRT.lastIoctl = command;
    return false;
}

bool AirportItlwm::init(OSDictionary *properties)
{
    bool ret = super::init(properties);
    awdlSyncEnable = true;
    power_state = 0;
    tahoeRequestedPowerState = kWiFiPowerOff;
    tahoeBootstrapPowerPending = false;
    tahoeBootstrapPowerWindowOpen = true;
    driverLogStream = nullptr;
    fTxPool = NULL;
    fRxPool = NULL;
    fTxQueue = NULL;
    fRxQueue = NULL;
    memset(geo_location_cc, 0, sizeof(geo_location_cc));
    RT_SET(15);
    XYLog("DEBUG %s power_state=%u ret=%d\n", __FUNCTION__, power_state, ret);
    return ret;
}

IOService* AirportItlwm::probe(IOService *provider, SInt32 *score)
{
    XYLog("DEBUG %s entry provider=%p\n", __PRETTY_FUNCTION__, provider);

    int delay_secs = 0;
    if (PE_parse_boot_argn("itlwm_delay", &delay_secs, sizeof(delay_secs)) && delay_secs > 0) {
        XYLog("DEBUG %s itlwm_delay=%d — pausing before probe...\n", __FUNCTION__, delay_secs);
        IOSleep(delay_secs * 1000);
    }
    IOPCIEDeviceWrapper *wrapper = OSDynamicCast(IOPCIEDeviceWrapper, provider);
    if (!wrapper) {
        XYLog("DEBUG %s FAIL: Not a IOPCIEDeviceWrapper instance\n", __FUNCTION__);
        return NULL;
    }
    pciNub = wrapper->pciNub;
    fHalService = wrapper->fHalService;
    if (!pciNub || !fHalService) {
        XYLog("DEBUG %s FAIL: pciNub=%p fHalService=%p\n", __FUNCTION__, pciNub, fHalService);
        return NULL;
    }
    XYLog("DEBUG %s OK: pciNub=%p fHalService=%p — calling super::probe (IO80211Controller)\n", __FUNCTION__, pciNub, fHalService);

    // Panic timer: catch hangs inside IO80211Controller::probe()
    // Captures global rtMask + key pointers so one crash report is enough.
    struct ProbeDiag {
        volatile bool     returned;
        void             *self;
        void             *provider;
        void             *pciNub;
        void             *halService;
    };
    static ProbeDiag sPD = {};
    sPD = { false, this, provider, pciNub, fHalService };

    thread_call_t probeTimer = thread_call_allocate(
        [](thread_call_param_t ctx, thread_call_param_t) {
            ProbeDiag *d = (ProbeDiag *)ctx;
            if (!d->returned)
                panic("AirportItlwm::probe hung  "
                      "rtMask=0x%05x | self=%p prov=%p pci=%p hal=%p | "
                      "ic=%d fl=0x%x pwr=%u link=0x%x "
                      "evt=%u(last=%d) pm=%u wd=%u scan=%u",
                      sRT.rtMask, d->self, d->provider,
                      d->pciNub, d->halService,
                      sRT.ic_state, sRT.if_flags, sRT.power_state,
                      sRT.linkStatus,
                      sRT.evtCount, sRT.lastEvtCode,
                      sRT.postMsgCount, sRT.wdCount, sRT.scanCount);
        }, &sPD);
    uint64_t probeDeadline;
    clock_interval_to_deadline(60, kSecondScale, &probeDeadline);
    thread_call_enter_delayed(probeTimer, probeDeadline);

    IOService *result = super::probe(provider, score);

    sPD.returned = true;
    thread_call_cancel(probeTimer);
    thread_call_free(probeTimer);
    XYLog("DEBUG %s super::probe returned %p\n", __FUNCTION__, result);
    return result;
}

#define LOWER32(x)  ((uint64_t)(x) & 0xffffffff)
#define HIGHER32(x) ((uint64_t)(x) >> 32)

bool AirportItlwm::
initCCLogs()
{
    // ---------------------------------------------------------------
    // CoreCapture lifecycle diagnostic — bitmask tracks each factory
    // call so a single panic report pinpoints the exact failure.
    //
    // Bit  Hex   Phase
    //  0   0x001  initCCLogs entered
    //  1   0x002  driverLogPipe created
    //  2   0x004  driverDataPathPipe created
    //  3   0x008  driverSnapshotsPipe created
    //  4   0x010  driverFaultReporter (CCStream) created
    //  5   0x020  OSDynamicCast(CCDataStream) OK
    //  6   0x040  frWorkloop created
    //  7   0x080  CCFaultReporter::withStreamWorkloop OK
    //  8   0x100  IO80211FaultReporter::allocWithParams OK
    //  9   0x200  driverLogStream (CCLogStream) created
    // 10   0x400  initCCLogs returning true
    // ---------------------------------------------------------------
    struct CCDiag {
        volatile uint32_t mask;
        void *pciNub;
        void *logPipe;
        void *dataPathPipe;
        void *snapshotsPipe;
        void *faultStream;     // CCStream* from withPipeAndName
        void *dataStream;      // CCDataStream* after OSDynamicCast
        void *frWorkloop;
        void *ccFaultReporter;
        void *io80211FR;
        void *logStream;
    };
    static CCDiag sCCDiag = {};
    sCCDiag = {};
    sCCDiag.pciNub = pciNub;
#define CC_SET(bit) do { sCCDiag.mask |= (1u << (bit)); } while(0)

    CC_SET(0);

    CCPipeOptions driverLogOptions = { 0 };
    driverLogOptions.pipe_type = 0;
    driverLogOptions.log_data_type = 1;
    driverLogOptions.pipe_size = 0x200000;
    driverLogOptions.min_log_size_notify = 0xccccc;
    driverLogOptions.notify_threshold = 1000;
    strlcpy(driverLogOptions.file_name, "Itlwm_Logs", sizeof(driverLogOptions.file_name));
    snprintf(driverLogOptions.name, sizeof(driverLogOptions.name), "wlan%d", 0);
    strlcpy(driverLogOptions.directory_name, "WiFi", sizeof(driverLogOptions.directory_name));
    driverLogOptions.pad9 = 0x1000000;
    driverLogOptions.pad10 = 2;
    driverLogOptions.file_options = 0;
    driverLogOptions.log_policy = 0;
    // Use pciNub as owner (not this) — Apple's BCM driver uses the bus interface as CCPipe owner.
    // Using the IO80211Controller as owner conflicts with CoreCapture registration in
    // IO80211Controller::setupControlPathLogging(), causing a deadlock during super::start().
    driverLogPipe = CCPipe::withOwnerNameCapacity(pciNub, "com.zxystd.AirportItlwm", "DriverLogs", &driverLogOptions);
    sCCDiag.logPipe = driverLogPipe;
    if (driverLogPipe) CC_SET(1);

    memset(&driverLogOptions, 0, sizeof(driverLogOptions));
    driverLogOptions.pipe_type = 0;
    driverLogOptions.log_data_type = 0;
    driverLogOptions.pipe_size = 0x200000;
    driverLogOptions.min_log_size_notify = 0xccccc;
    driverLogOptions.notify_threshold = 1000;
    strlcpy(driverLogOptions.file_name, "AppleBCMWLAN_Datapath", sizeof(driverLogOptions.file_name));
    strlcpy(driverLogOptions.directory_name, "WiFi", sizeof(driverLogOptions.directory_name));
    driverLogOptions.pad9 = HIGHER32(0x202800000);
    driverLogOptions.pad10 = LOWER32(0x202800000);
    driverLogOptions.file_options = 0;
    driverLogOptions.log_policy = 0;
    driverDataPathPipe = CCPipe::withOwnerNameCapacity(pciNub, "com.zxystd.AirportItlwm", "DatapathEvents", &driverLogOptions);
    sCCDiag.dataPathPipe = driverDataPathPipe;
    if (driverDataPathPipe) CC_SET(2);

    memset(&driverLogOptions, 0, sizeof(driverLogOptions));
    driverLogOptions.pipe_type = 0x200000001;
    driverLogOptions.log_data_type = 2;
    strlcpy(driverLogOptions.file_name, "StateSnapshots", sizeof(driverLogOptions.file_name));
    strlcpy(driverLogOptions.name, "0", sizeof(driverLogOptions.name));
    strlcpy(driverLogOptions.directory_name, "WiFi", sizeof(driverLogOptions.directory_name));
    driverLogOptions.pipe_size = 128;
    driverSnapshotsPipe = CCPipe::withOwnerNameCapacity(pciNub, "com.zxystd.AirportItlwm", "StateSnapshots", &driverLogOptions);
    sCCDiag.snapshotsPipe = driverSnapshotsPipe;
    if (driverSnapshotsPipe) CC_SET(3);

    CCStreamOptions faultReportOptions = { 0 };
    faultReportOptions.stream_type = 1;
    faultReportOptions.console_level = 0xFFFFFFFFFFFFFFFF;
    driverFaultReporter = CCStream::withPipeAndName(driverSnapshotsPipe, "FaultReporter", &faultReportOptions);
    sCCDiag.faultStream = driverFaultReporter;
    if (driverFaultReporter) CC_SET(4);

    // CCLogStream MUST be created before super::start() — IO80211Controller::start()
    // calls vtable[431] (getDriverLogStream) to get CCLogStream* for setGlobalLogger().
    {
        CCStreamOptions logStreamOptions = { 0 };
        logStreamOptions.stream_type = 0;
        logStreamOptions.console_level = 0xFFFFFFFFFFFFFFFF;
        CCStream *logStreamBase = CCStream::withPipeAndName(driverLogPipe, "DriverLogStream", &logStreamOptions);
        driverLogStream = OSDynamicCast(CCLogStream, logStreamBase);
        if (logStreamBase && !driverLogStream)
            logStreamBase->release();
    }
    sCCDiag.logStream = driverLogStream;
    if (driverLogStream) CC_SET(9);

    // Create IO80211FaultReporter from the fault-reporter data stream.
    // Reference: AppleBCMWLANLogger::init() creates CCDataStream via
    // CCStream::withPipeAndName(pipe, name, {stream_type=1}), casts with
    // safeMetaCast, then CCFaultReporter::withStreamWorkloop →
    // IO80211FaultReporter::allocWithParams.
    if (driverFaultReporter) {
        CCDataStream *dataStream = OSDynamicCast(CCDataStream, driverFaultReporter);
        sCCDiag.dataStream = dataStream;
        if (dataStream) {
            CC_SET(5);
            IOWorkLoop *frWorkloop = IOWorkLoop::workLoop();
            sCCDiag.frWorkloop = frWorkloop;
            if (frWorkloop) {
                CC_SET(6);
                CCFaultReporter *ccfr = CCFaultReporter::withStreamWorkloop(
                    dataStream, frWorkloop);
                sCCDiag.ccFaultReporter = ccfr;
                if (ccfr) {
                    CC_SET(7);
                    io80211FaultReporter = IO80211FaultReporter::allocWithParams(ccfr);
                    sCCDiag.io80211FR = io80211FaultReporter;
                    if (io80211FaultReporter) CC_SET(8);
                }
                frWorkloop->release();
            }
        }
    }

    bool ok = driverLogPipe && driverDataPathPipe && driverSnapshotsPipe
        && driverFaultReporter && io80211FaultReporter;
    if (ok) CC_SET(10);

    XYLog("%s mask=0x%03x | logPipe=%p dataPath=%p snap=%p "
          "faultStream=%p ds=%p wl=%p ccfr=%p io80211fr=%p logStream=%p\n",
          __FUNCTION__, sCCDiag.mask,
          sCCDiag.logPipe, sCCDiag.dataPathPipe, sCCDiag.snapshotsPipe,
          sCCDiag.faultStream, sCCDiag.dataStream, sCCDiag.frWorkloop,
          sCCDiag.ccFaultReporter, sCCDiag.io80211FR, sCCDiag.logStream);

    if (!ok) {
        panic("AirportItlwm::initCCLogs FAILED  ccMask=0x%03x rtMask=0x%07x rt2=0x%04x | "
              "pci=%p logPipe=%p dataPath=%p snap=%p "
              "faultStream=%p dataStream=%p wl=%p ccfr=%p io80211fr=%p logStream=%p | "
              "ic=%d fl=0x%x pwr=%u evt=%u pm=%u",
              sCCDiag.mask, sRT.rtMask, sRT.rtMask2, sCCDiag.pciNub,
              sCCDiag.logPipe, sCCDiag.dataPathPipe, sCCDiag.snapshotsPipe,
              sCCDiag.faultStream, sCCDiag.dataStream, sCCDiag.frWorkloop,
              sCCDiag.ccFaultReporter, sCCDiag.io80211FR, sCCDiag.logStream,
              sRT.ic_state, sRT.if_flags, sRT.power_state,
              sRT.evtCount, sRT.postMsgCount);
    }

    return ok;
#undef CC_SET
}

bool AirportItlwm::start(IOService *provider)
{
    XYLog("AirportItlwm build=" ITLWM_XSTR(ITLWM_COMMIT_HASH) " entry provider=%p\n",
          provider);

    // boot-arg "itlwm_delay=N" — pause N seconds so verbose boot output
    // stays on screen long enough to read/photograph.
    int delay_secs = 0;
    if (PE_parse_boot_argn("itlwm_delay", &delay_secs, sizeof(delay_secs)) && delay_secs > 0) {
        XYLog("DEBUG %s itlwm_delay=%d — pausing to let you read the screen...\n", __FUNCTION__, delay_secs);
        IOSleep(delay_secs * 1000);
        XYLog("DEBUG %s delay done, continuing\n", __FUNCTION__);
    }

    struct IOSkywalkEthernetInterface::RegistrationInfo registInfo;
    int boot_value = 0;

    // ---------------------------------------------------------------
    // Diagnostic panic timer — bitmask tracks every lifecycle phase.
    // On timeout the panic message prints the mask + key pointers,
    // so a single crash report is enough to pinpoint the hang.
    //
    // Bit  Hex      Phase
    //  0   0x00001  initCCLogs entered
    //  1   0x00002  CCPipes created
    //  2   0x00004  driverLogStream OK
    //  3   0x00008  io80211FaultReporter OK
    //  4   0x00010  super::start entered
    //  5   0x00020  super::start returned OK
    //  6   0x00040  PCI configured
    //  7   0x00080  _fWorkloop OK
    //  8   0x00100  _fCommandGate OK
    //  9   0x00200  HAL attached
    // 10   0x00400  watchdog/scan timers OK
    // 11   0x00800  fNetIf init OK
    // 12   0x01000  fNetIf attach OK
    // 13   0x02000  attachInterface OK
    // 14   0x04000  bsdInterface OK
    // 15   0x08000  fNetIf->start OK
    // 16   0x10000  enableAdapter OK
    // 17   0x20000  registerService done (start complete)
    // ---------------------------------------------------------------
    struct StartDiag {
        volatile uint32_t mask;
        volatile int      step;          // legacy step counter
        void             *self;          // AirportItlwm*
        void             *logStream;
        void             *faultReporter;
        void             *workloop;
        void             *netIf;
        void             *bsdIf;
    };
    static StartDiag sDiag = {};
    sDiag = {};
    sDiag.self = this;

    thread_call_t panicTimer = thread_call_allocate(
        [](thread_call_param_t ctx, thread_call_param_t) {
            StartDiag *d = (StartDiag *)ctx;
            if (!(d->mask & 0x20000))
                panic("AirportItlwm::start hung  "
                      "sMask=0x%05x step=%d ss=%u "
                      "rtMask=0x%07x rt2=0x%04x rt3=0x%04x | "
                      "self=%p log=%p fault=%p wl=%p nif=%p bsd=%p | "
                      "ic=%d fl=0x%x pwr=%u link=0x%x "
                      "evt=%u(last=%d) pm=%u(req=%u) wd=%u | "
                      "ioctl=%u lastIo=%d lastPM=0x%x "
                      "ls=%d lsCnt=%u scan=%u pmCnt=%u | "
                      "scanReq=%u assoc=%u scanRes=%u "
                      "icfl=0x%x esslen=%u nodes=%u mfail=0x%x | "
                      "fVars=%p bsdIf=%p enCnt=%u disCnt=%u enRet=0x%x | "
                      "bsdFl=0x%x bsdMtu=%u | "
                      "pmPol=%p pmOffC=%u pmOnC=%u txDrop=%u "
                      "gateNullOff=%u gateNullOn=%u ackOff=%u ackOn=%u | "
                      "skNif=%p txP=%p rxP=%p txQ=%p rxQ=%p | "
                      "txCb=%u txS=%u txD=%u rxIn=%u rxOK=%u rxAF=%u rxEF=%u rxCb=%u | "
                      "nxProv=%p nifCtx=%p nxArena=%p async=%p "
                      "r90=%p r98=%p rA0=%p",
                      d->mask, d->step, sRT.startStep,
                      sRT.rtMask, sRT.rtMask2, sRT.rtMask3, d->self,
                      d->logStream, d->faultReporter,
                      d->workloop, d->netIf, d->bsdIf,
                      sRT.ic_state, sRT.if_flags, sRT.power_state,
                      sRT.linkStatus,
                      sRT.evtCount, sRT.lastEvtCode,
                      sRT.postMsgCount, sRT.lastPmReq, sRT.wdCount,
                      sRT.ioctlCount, sRT.lastIoctl,
                      sRT.lastPostMsg, sRT.lastLinkState,
                      sRT.linkSetCount, sRT.scanCount,
                      sRT.pmCount,
                      sRT.scanReqCount, sRT.assocCount, sRT.scanResCount,
                      sRT.ic_flags, sRT.ic_des_esslen,
                      sRT.nodeCount, sRT.matchFail,
                      (void *)(uintptr_t)sRT.fVarsPtr,
                      (void *)(uintptr_t)sRT.bsdIfPtr,
                      sRT.enableCnt, sRT.disableCnt, sRT.lastEnableRet,
                      sRT.bsdIfFlags, sRT.bsdIfMtu,
                      (void *)(uintptr_t)sRT.pmPolicyPtr,
                      sRT.pmOffCancelRet, sRT.pmOnCancelRet,
                      sRT.outputDropPwr,
                      sRT.pmOffGateNull, sRT.pmOnGateNull,
                      sRT.pmAckOffCnt, sRT.pmAckOnCnt,
                      (void *)(uintptr_t)sRT.fNetIfPtr,
                      (void *)(uintptr_t)sRT.fTxPoolPtr,
                      (void *)(uintptr_t)sRT.fRxPoolPtr,
                      (void *)(uintptr_t)sRT.fTxQueuePtr,
                      (void *)(uintptr_t)sRT.fRxQueuePtr,
                      sRT.txCbCnt, sRT.txPktSent, sRT.txPktDrop,
                      sRT.rxInputCnt, sRT.rxPktOK, sRT.rxAllocFail,
                      sRT.rxEnqFail, sRT.rxCbCnt,
                      (void *)(uintptr_t)sRT.nexusProvPtr,
                      (void *)(uintptr_t)sRT.nifCtxPtr,
                      (void *)(uintptr_t)sRT.nexusArenaPtr,
                      (void *)(uintptr_t)sRT.asyncSentinel,
                      (void *)(uintptr_t)sRT.regObj90,
                      (void *)(uintptr_t)sRT.regObj98,
                      (void *)(uintptr_t)sRT.regObjA0);
        }, &sDiag);
    uint64_t panicDeadline;
    clock_interval_to_deadline(60, kSecondScale, &panicDeadline);
    thread_call_enter_delayed(panicTimer, panicDeadline);
    XYLog("DEBUG %s panic timer armed (60s)\n", __FUNCTION__);
#define SD_SET(bit) do { sDiag.mask |= (1u << (bit)); } while(0)
#define DISARM_PANIC_TIMER() do { SD_SET(17); thread_call_cancel(panicTimer); thread_call_free(panicTimer); } while(0)

    UInt8 builtIn = 0;
    setProperty("built-in", OSData::withBytes(&builtIn, sizeof(builtIn)));
    setProperty("DriverKitDriver", kOSBooleanFalse);
#if __IO80211_TARGET >= __MAC_26_0
    XYLog("DEBUG %s [STEP 1] initCCLogs (Tahoe path)\n", __FUNCTION__);
    SD_SET(0); // initCCLogs entered
    if (!initCCLogs()) {
        XYLog("DEBUG %s [STEP 1] FAIL: CCLog init\n", __FUNCTION__);
        DISARM_PANIC_TIMER();
        return false;
    }
    SD_SET(1); // CCPipes created
    sDiag.logStream = driverLogStream;
    if (driverLogStream) SD_SET(2);
    sDiag.faultReporter = io80211FaultReporter;
    if (io80211FaultReporter) SD_SET(3);
    sDiag.step = 1;
#endif
    XYLog("DEBUG %s [STEP 2] super::start (logStream=%p faultRep=%p)\n",
          __FUNCTION__, driverLogStream, io80211FaultReporter);
    SD_SET(4); // super::start entered
    bool superResult = super::start(provider);
    if (!superResult) {
        XYLog("DEBUG %s [STEP 2] FAIL: super::start returned false\n", __FUNCTION__);
        DISARM_PANIC_TIMER();
        return false;
    }
    SD_SET(5); // super::start OK
    sDiag.step = 2;
    XYLog("DEBUG %s [STEP 3] PCI setup\n", __FUNCTION__);
    pciNub->setBusMasterEnable(true);
    pciNub->setIOEnable(true);
    pciNub->setMemoryEnable(true);
    pciNub->configWrite8(0x41, 0);
    if (pciNub->requestPowerDomainState(kIOPMPowerOn,
                                        (IOPowerConnection *) getParentEntry(gIOPowerPlane), IOPMLowestState) != IOPMNoErr) {
        XYLog("DEBUG %s [STEP 3] FAIL: requestPowerDomainState\n", __FUNCTION__);
        super::stop(provider);
        DISARM_PANIC_TIMER();
        return false;
    }
    if (initPCIPowerManagment(pciNub) == false) {
        XYLog("DEBUG %s [STEP 3] FAIL: initPCIPowerManagment\n", __FUNCTION__);
        super::stop(pciNub);
        DISARM_PANIC_TIMER();
        return false;
    }
    SD_SET(6); // PCI configured
    sDiag.step = 3;
    XYLog("DEBUG %s [STEP 4] workloop & command gate\n", __FUNCTION__);
    sDiag.workloop = _fWorkloop;
    if (_fWorkloop == NULL) {
        XYLog("DEBUG %s [STEP 4] FAIL: No _fWorkloop\n", __FUNCTION__);
        super::stop(pciNub);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    SD_SET(7); // _fWorkloop OK
    _fCommandGate = IOCommandGate::commandGate(this, (IOCommandGate::Action)AirportItlwm::tsleepHandler);
    if (_fCommandGate == 0) {
        XYLog("DEBUG %s [STEP 4] FAIL: No command gate\n", __FUNCTION__);
        super::stop(pciNub);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    SD_SET(8); // _fCommandGate OK
    _fWorkloop->addEventSource(_fCommandGate);
    const IONetworkMedium *primaryMedium;
    if (!createMediumTables(&primaryMedium) ||
        !setCurrentMedium(primaryMedium) || !setSelectedMedium(primaryMedium)) {
        XYLog("DEBUG %s [STEP 4] FAIL: setup medium\n", __FUNCTION__);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    sDiag.step = 4;
    XYLog("DEBUG %s [STEP 5] HAL initWithController + attach\n", __FUNCTION__);
    fHalService->initWithController(this, _fWorkloop, _fCommandGate);
    fHalService->get80211Controller()->ic_event_handler = eventHandler;

    if (PE_parse_boot_argn("-novht", &boot_value, sizeof(boot_value)))
        fHalService->get80211Controller()->ic_userflags |= IEEE80211_F_NOVHT;
    if (PE_parse_boot_argn("-noht40", &boot_value, sizeof(boot_value)))
        fHalService->get80211Controller()->ic_userflags |= IEEE80211_F_NOHT40;

    if (!fHalService->attach(pciNub)) {
        XYLog("DEBUG %s [STEP 5] FAIL: HAL attach\n", __FUNCTION__);
        super::stop(pciNub);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    SD_SET(9); // HAL attached
    sDiag.step = 6;
    XYLog("DEBUG %s [STEP 6] watchdog + scan timers\n", __FUNCTION__);
    fWatchdogWorkLoop = IOWorkLoop::workLoop();
    if (fWatchdogWorkLoop == NULL) {
        XYLog("DEBUG %s [STEP 6] FAIL: watchdog workloop\n", __FUNCTION__);
        fHalService->detach(pciNub);
        super::stop(pciNub);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    watchdogTimer = IOTimerEventSource::timerEventSource(this, OSMemberFunctionCast(IOTimerEventSource::Action, this, &AirportItlwm::watchdogAction));
    if (!watchdogTimer) {
        XYLog("DEBUG %s [STEP 6] FAIL: watchdog timer\n", __FUNCTION__);
        fHalService->detach(pciNub);
        super::stop(pciNub);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    fWatchdogWorkLoop->addEventSource(watchdogTimer);
    scanSource = IOTimerEventSource::timerEventSource(this, &fakeScanDone);
    _fWorkloop->addEventSource(scanSource);
    scanSource->enable();

    SD_SET(10); // watchdog/scan timers OK
    sDiag.step = 7;
    XYLog("DEBUG %s [STEP 7] Skywalk interface init + attach\n", __FUNCTION__);
    fNetIf = new AirportItlwmSkywalkInterface;
#if __IO80211_TARGET >= __MAC_26_0
    if (!fNetIf->init()) {
        XYLog("DEBUG %s [STEP 7] FAIL: Skywalk interface no-arg init\n", __FUNCTION__);
        super::stop(provider);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    // Apple APSTA construction splits subclass init() from the later
    // provider/role binding path. Keep the Tahoe port on the same shape
    // instead of routing controller construction through a fake 2-arg init.
    if (!static_cast<AirportItlwmSkywalkInterface *>(fNetIf)->bindController(this)) {
#else
    if (!fNetIf->init(this)) {
#endif
        XYLog("DEBUG %s [STEP 7] FAIL: Skywalk interface init\n", __FUNCTION__);
        super::stop(provider);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    SD_SET(11); // fNetIf init OK
    sDiag.netIf = fNetIf;
    sRT.fNetIfPtr = (uint64_t)(uintptr_t)fNetIf;
#if __IO80211_TARGET < __MAC_26_0
    fNetIf->setInterfaceRole(1);
    fNetIf->setInterfaceId(1);
#endif

#if __IO80211_TARGET < __MAC_26_0
    if (!initCCLogs()) {
        XYLog("DEBUG %s [STEP 7] FAIL: CCLog init (pre-Tahoe)\n", __FUNCTION__);
        super::stop(provider);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
#endif
    if (!fNetIf->attach(this)) {
        XYLog("DEBUG %s [STEP 7] FAIL: fNetIf attach\n", __FUNCTION__);
        super::stop(provider);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    SD_SET(12); // fNetIf attach OK
    sDiag.step = 8;
    XYLog("DEBUG %s [STEP 8] attachInterface + BSD interface\n", __FUNCTION__);
    if (!attachInterface(fNetIf, this)) {
        XYLog("DEBUG %s [STEP 8] FAIL: attachInterface\n", __FUNCTION__);
        super::stop(provider);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    SD_SET(13); // attachInterface OK

    // --- Skywalk registration: proper Sequoia path ---
    //
    // On macOS Sequoia (26.x), IONetworkStack no longer matches IOEthernetInterface
    // for WiFi controllers.  BSD ifnet creation goes through:
    //   registerEthernetInterface → creates LogicalLink + nexus provider
    //   deferBSDAttach(false) → registerService on interface
    //   IOSkywalkNetworkBSDClient matches → creates BSD ifnet via nexus
    //
    // Reference: Apple BCM WiFi driver calls registerInfraEthernetInterface
    // (IO80211InfraInterface non-virtual) which internally calls
    // registerEthernetInterface on IOSkywalkEthernetInterface.

    sRT.startStep = 80;
    memset(&registInfo, 0, sizeof(registInfo));
    if (!fNetIf->initRegistrationInfo(&registInfo, 1, sizeof(registInfo))) {
        XYLog("DEBUG %s [STEP 8] FAIL: initRegistrationInfo\n", __FUNCTION__);
        super::stop(provider);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    RT3_SET(0); // initRegistrationInfo OK
    {
        uint8_t *p = (uint8_t *)&registInfo;
        XYLog("DEBUG %s [STEP 8] initRegistrationInfo OK, size=%lu\n",
              __FUNCTION__, sizeof(registInfo));
        XYLog("DEBUG %s [STEP 8] regInfo[0-31]: %02x %02x %02x %02x %02x %02x %02x %02x "
              "%02x %02x %02x %02x %02x %02x %02x %02x "
              "%02x %02x %02x %02x %02x %02x %02x %02x "
              "%02x %02x %02x %02x %02x %02x %02x %02x\n",
              __FUNCTION__,
              p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7],
              p[8],p[9],p[10],p[11],p[12],p[13],p[14],p[15],
              p[16],p[17],p[18],p[19],p[20],p[21],p[22],p[23],
              p[24],p[25],p[26],p[27],p[28],p[29],p[30],p[31]);
    }

    // mExpansionData / mExpansionData2 are managed by the framework's
    // initRegistrationInfo (called above at STEP 8).  Manual allocation
    // was removed because it wrote to the WRONG offsets when our class
    // size was 0x10 too small (0xD0 vs real 0xE0), corrupting framework state.
    // See plan: "Fix Skywalk Class Layout Mismatch (S1-S4 Architecture)".
    XYLog("DEBUG %s [STEP 8-post] mExpansionData=%p mExpansionData2=%p "
          "(should be non-NULL if framework initialized them)\n",
          __FUNCTION__, fNetIf->mExpansionData, fNetIf->mExpansionData2);
    RT3_SET(1); // post-initRegistrationInfo check

    // Create Skywalk packet buffer pools for TX and RX
    sRT.startStep = 81;
    {
        IOSkywalkPacketBufferPool::PoolOptions poolOpts = {};
        poolOpts.packetCount = 256;
        poolOpts.bufferCount = 256;
        poolOpts.bufferSize  = SKYWALK_BUF_SIZE;
        poolOpts.maxBuffersPerPacket = 1;

        fTxPool = IOSkywalkPacketBufferPool::withName("AirportItlwm-TX", fNetIf, 0, &poolOpts);
        fRxPool = IOSkywalkPacketBufferPool::withName("AirportItlwm-RX", fNetIf, 0, &poolOpts);
        XYLog("DEBUG %s [STEP 8b] pools: TX=%p RX=%p\n", __FUNCTION__, fTxPool, fRxPool);
        if (!fTxPool || !fRxPool) {
            XYLog("DEBUG %s [STEP 8b] FAIL: pool creation (TX=%p RX=%p)\n",
                  __FUNCTION__, fTxPool, fRxPool);
            super::stop(provider);
            releaseAll();
            DISARM_PANIC_TIMER();
            return false;
        }
    }
    RT3_SET(2); // pools created
    sRT.fTxPoolPtr = (uint64_t)(uintptr_t)fTxPool;
    sRT.fRxPoolPtr = (uint64_t)(uintptr_t)fRxPool;

    // Create Skywalk TX submission and RX completion queues
    fTxQueue = IOSkywalkTxSubmissionQueue::withPool(fTxPool, 256, 0, this,
                                                    skywalkTxAction, NULL, 0);
    fRxQueue = IOSkywalkRxCompletionQueue::withPool(fRxPool, 256, 0, this,
                                                    skywalkRxAction, NULL, 0);
    XYLog("DEBUG %s [STEP 8c] queues: TX=%p RX=%p\n", __FUNCTION__, fTxQueue, fRxQueue);
    if (!fTxQueue || !fRxQueue) {
        XYLog("DEBUG %s [STEP 8c] FAIL: queue creation (TX=%p RX=%p)\n",
              __FUNCTION__, fTxQueue, fRxQueue);
        super::stop(provider);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    RT3_SET(3); // queues created
    sRT.fTxQueuePtr = (uint64_t)(uintptr_t)fTxQueue;
    sRT.fRxQueuePtr = (uint64_t)(uintptr_t)fRxQueue;

    // Wire up Skywalk RX input handler on the internal ifnet before
    // registration, so received frames go through the Skywalk path
    // instead of the legacy IOEthernetInterface::inputPacket path.
#if __IO80211_TARGET >= __MAC_26_0
    {
        struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
        ifp->if_skywalk_rx = skywalkRxInput;
        XYLog("DEBUG %s [STEP 8c-rx] wired if_skywalk_rx=%p on ifp=%p\n",
              __FUNCTION__, (void *)(uintptr_t)skywalkRxInput, ifp);
    }
#endif

    // Register the interface through the proper Skywalk path.
    // registerEthernetInterface internally creates a LogicalLink from the queues
    // and calls registerNetworkInterfaceWithLogicalLink, populating fVars[0] and
    // setting up the nexus provider for IOSkywalkNetworkBSDClient matching.
    sRT.startStep = 82;

    // Pre-registration diagnostics: dump critical kernel-side fields at the
    // offsets the framework expects.  These offsets come from Ghidra analysis
    // of IOSkywalkFamily.kext 26.3:
    //   +0xC0 = NIF_Context (ExpansionData for IOSkywalkNetworkInterface)
    //   +0xC8 = nexusProvider (must be non-NULL for core registration at FUN_0xa37640)
    //   +0xD0 = nexus arena
    //   +0x118 = mExpansionData2 (EthernetRegistrationContext, read by registerEthernetInterface)
    // Dump nexusProvider and surrounding fields (per YAML 91).
    // These offsets are critical for understanding registration success/failure.
    {
        uint8_t *raw = (uint8_t *)fNetIf;
        void *nifCtx      = *(void **)(raw + 0xC0);
        void *nexusProv   = *(void **)(raw + 0xC8);
        void *nexusArena  = *(void **)(raw + 0xD0);
        void *ethRegCtx   = *(void **)(raw + 0x118);
        void *asyncSent   = *(void **)(raw + 0xB8);
        void *regObj90    = *(void **)(raw + 0x90);
        void *regObj98    = *(void **)(raw + 0x98);
        void *regObjA0    = *(void **)(raw + 0xA0);

        // Persist in RuntimeDiag for panic timer visibility
        sRT.nifCtxPtr     = (uint64_t)(uintptr_t)nifCtx;
        sRT.nexusProvPtr  = (uint64_t)(uintptr_t)nexusProv;
        sRT.nexusArenaPtr = (uint64_t)(uintptr_t)nexusArena;
        sRT.asyncSentinel = (uint64_t)(uintptr_t)asyncSent;
        sRT.regObj90      = (uint64_t)(uintptr_t)regObj90;
        sRT.regObj98      = (uint64_t)(uintptr_t)regObj98;
        sRT.regObjA0      = (uint64_t)(uintptr_t)regObjA0;

        XYLog("DEBUG %s [PRE-REG] fNetIf=%p raw offsets:\n"
              "  +0x90 (queueSet)=%p  +0x98 (queueObj)=%p  +0xA0 (queueMgr)=%p\n"
              "  +0xB8 (asyncSentinel)=%p\n"
              "  +0xC0 (NIF_Context)=%p\n"
              "  +0xC8 (nexusProvider)=%p\n"
              "  +0xD0 (nexusArena)=%p\n"
              "  +0x118 (mExpansionData2/EthRegCtx)=%p\n",
              __FUNCTION__, fNetIf,
              regObj90, regObj98, regObjA0,
              asyncSent, nifCtx, nexusProv, nexusArena, ethRegCtx);
        if (ethRegCtx) {
            void *regInfoPtr = *(void **)ethRegCtx;
            XYLog("DEBUG %s [PRE-REG] *(+0x118)->fRegistrationInfo=%p\n",
                  __FUNCTION__, regInfoPtr);
        }
    }

    {
        IOSkywalkPacketQueue *queues[] = {
            (IOSkywalkPacketQueue *)fTxQueue,
            (IOSkywalkPacketQueue *)fRxQueue
        };
        // registerEthernetInterface returns IOReturn: 0 = kIOReturnSuccess.
        // Was incorrectly declared bool — 0 (success) was misread as false.
        // Ghidra FUN_0xa3d994: validates regInfo, builds LogicalLink from
        // queues via registerNetworkInterface(0xa36fd2), returns 0 on success.
        IOReturn regRet = fNetIf->registerEthernetInterface(
            (const IOSkywalkEthernetInterface::RegistrationInfo *)&registInfo,
            queues, 2, fTxPool, fRxPool, 0);
        XYLog("DEBUG %s [STEP 8d] registerEthernetInterface=0x%x\n", __FUNCTION__, regRet);
        if (regRet != kIOReturnSuccess) {
            XYLog("DEBUG %s [STEP 8d] FAIL: registerEthernetInterface ret=0x%x\n", __FUNCTION__, regRet);
            super::stop(provider);
            releaseAll();
            DISARM_PANIC_TIMER();
            return false;
        }
    }
    RT3_SET(4); // registerEthernetInterface OK
    SD_SET(14);

    // Post-registration diagnostics: verify kernel populated nexusProvider
    // during registerEthernetInterface (via kernel Skywalk dispatcher
    // FUN_ffffff8000a6be70 → FUN_0xa37640 core registration body).
    // nexusProvider (+0xC8) must be non-NULL for BSDClient nexus creation.
    {
        uint8_t *raw = (uint8_t *)fNetIf;
        void *nexusProv  = *(void **)(raw + 0xC8);
        void *nexusArena = *(void **)(raw + 0xD0);
        void *asyncSent  = *(void **)(raw + 0xB8);
        void *regObj90   = *(void **)(raw + 0x90);
        sRT.nexusProvPtr  = (uint64_t)(uintptr_t)nexusProv;
        sRT.nexusArenaPtr = (uint64_t)(uintptr_t)nexusArena;
        sRT.asyncSentinel = (uint64_t)(uintptr_t)asyncSent;
        XYLog("DEBUG %s [POST-REG] nexusProvider=%p nexusArena=%p "
              "asyncSentinel=%p queueSet=%p\n",
              __FUNCTION__, nexusProv, nexusArena, asyncSent, regObj90);
        if (!nexusProv) {
            XYLog("DEBUG %s [POST-REG] WARNING: nexusProvider still NULL "
                  "after registerEthernetInterface — BSDClient nexus "
                  "creation will fail\n", __FUNCTION__);
        }
    }

    // Start the Skywalk interface
    sDiag.step = 81;
    RT3_SET(10); // entering fNetIf->start
    XYLog("DEBUG %s [STEP 8e] calling fNetIf->start(this=%p)\n", __FUNCTION__, this);
    fNetIf->start(this);
    RT3_SET(11); // fNetIf->start returned
    SD_SET(15); // fNetIf->start OK

    // Trigger IOSkywalkNetworkBSDClient matching.
    // deferBSDAttach(false) removes IODeferBSDAttach property and calls
    // registerService() on fNetIf, causing IOKit to match BSDClient.
    // BSDClient::start creates the nexus channel and BSD ifnet.
    XYLog("DEBUG %s [STEP 8f] calling deferBSDAttach(false)\n", __FUNCTION__);
    fNetIf->deferBSDAttach(false);
    {
        const char *bsdName = fNetIf->getBSDName();
        ifnet_t bsdIf = fNetIf->getBSDInterface();
        sRT.bsdIfPtr = (uint64_t)(uintptr_t)bsdIf;
        if (bsdIf) RT2_SET(0);
        if (bsdName && bsdName[0]) RT2_SET(1);
        XYLog("DEBUG %s [STEP 8f] deferBSDAttach done, bsdName=%s bsdIf=%p rt2=0x%04x rt3=0x%04x\n",
              __FUNCTION__, bsdName ? bsdName : "(null)", bsdIf, sRT.rtMask2, sRT.rtMask3);
        // NOTE: bsdName may be empty here — the BSD ifnet is created
        // ASYNCHRONOUSLY via the nexus callback chain:
        //   deferBSDAttach(false) → registerService on fNetIf
        //     → IOSkywalkNetworkBSDClient matches and starts
        //     → BSDClient::start creates nexus registration (FUN_0x987c40)
        //     → kernel calls prepareNexusCallback (async)
        //     → gatedPrepareNexus → registerBSDInterface → setBSDName
        //     → BSD ifnet (en0) appears
        // (Ghidra: YAML 88, bsd_ifnet_creation_sequence phase_2_nexus_ready)
        //
        // Previous diagnosis (now corrected):
        //   Old code treated registerEthernetInterface IOReturn 0 (success)
        //   as bool false → aborted at STEP 8d → never reached here.
        //   nexusProvider was NULL only BEFORE registration; the kernel
        //   populates it during registerEthernetInterface via the Skywalk
        //   dispatcher (FUN_ffffff8000a6be70).
    }

    // Apple's bootChipImage is triggered by AppleBCMWLANUserClient — an IOService
    // matched against the controller via IOKit matching.  AirportItlwmBootNub
    // replicates this pattern: IOKit starts it after registerService(), and its
    // start() enters the thread_call that runs enableAdapter + readiness publication.
    //
    // Do NOT enable the adapter here.  registerService() must happen first so
    // the framework's WCL/PostOffice/monitor dispatch tables are wired up before
    // setDataPathState fires.  The gate must be enabled before registerService()
    // so the boot nub's thread_call can route through it.
    _fCommandGate->enable();
    tahoeBootThreadCall = thread_call_allocate(
        (thread_call_func_t)handleTahoeBootChipImage,
        (thread_call_param_t)this);
    sDiag.step = 9;
    SD_SET(16);
    XYLog("DEBUG %s [STEP 9] boot thread_call allocated, waiting for boot nub\n", __FUNCTION__);
    sDiag.step = 10;
    // registerService() makes the IO80211Controller visible to airportd and
    // triggers IOKit matching for AirportItlwmBootNub.  The BSD ifnet (en0)
    // is created asynchronously via the nexus callback chain triggered by
    // deferBSDAttach(false) at STEP 8f.
    registerService();
    RT_SET(18);
    XYLog("DEBUG %s start COMPLETE mask=0x%05x\n", __FUNCTION__, sDiag.mask | 0x20000);
    DISARM_PANIC_TIMER();
#undef DISARM_PANIC_TIMER
#undef SD_SET
    return true;
}

void AirportItlwm::stop(IOService *provider)
{
    RT_SET(19);
    sRT.stopStep = 1;
    XYLog("DEBUG %s [1] entry power_state=%u pmPowerState=%u provider=%p fHalService=%p "
          "rtMask=0x%07x\n",
          __PRETTY_FUNCTION__, power_state, pmPowerState, provider, fHalService, sRT.rtMask);

    // Panic timer — catches hangs in the teardown path.
    thread_call_t stopTimer = thread_call_allocate(
        [](thread_call_param_t, thread_call_param_t) {
            panic("AirportItlwm::stop hung  "
                  "stopStep=%u rtMask=0x%07x rt2=0x%04x rt3=0x%04x | "
                  "ic=%d fl=0x%x pwr=%u link=0x%x | "
                  "evt=%u(last=%d) pm=%u(req=%u) wd=%u ioctl=%u(last=%d) "
                  "scanDone=%u scan=%u pmCnt=%u ls=%d lsCnt=%u "
                  "ifType=0x%x skFree=%u free=%u ss=%u | "
                  "scanReq=%u assoc=%u scanRes=%u "
                  "icfl=0x%x esslen=%u nodes=%u mfail=0x%x | "
                  "fVars=%p bsdIf=%p enCnt=%u disCnt=%u enRet=0x%x | "
                  "bsdFl=0x%x bsdMtu=%u | "
                  "pmPol=%p pmOffC=%u pmOnC=%u txDrop=%u "
                  "gateNullOff=%u gateNullOn=%u ackOff=%u ackOn=%u | "
                  "skNif=%p txP=%p rxP=%p txQ=%p rxQ=%p | "
                  "txCb=%u txS=%u txD=%u rxIn=%u rxOK=%u rxAF=%u rxEF=%u rxCb=%u | "
                  "nxProv=%p nifCtx=%p nxArena=%p async=%p "
                  "r90=%p r98=%p rA0=%p",
                  sRT.stopStep, sRT.rtMask, sRT.rtMask2, sRT.rtMask3,
                  sRT.ic_state, sRT.if_flags, sRT.power_state,
                  sRT.linkStatus,
                  sRT.evtCount, sRT.lastEvtCode,
                  sRT.postMsgCount, sRT.lastPmReq, sRT.wdCount,
                  sRT.ioctlCount, sRT.lastIoctl,
                  sRT.scanDoneCount, sRT.scanCount, sRT.pmCount,
                  sRT.lastLinkState, sRT.linkSetCount,
                  sRT.ifType, sRT.skFreeStep, sRT.freeStep,
                  sRT.startStep,
                  sRT.scanReqCount, sRT.assocCount, sRT.scanResCount,
                  sRT.ic_flags, sRT.ic_des_esslen,
                  sRT.nodeCount, sRT.matchFail,
                  (void *)(uintptr_t)sRT.fVarsPtr,
                  (void *)(uintptr_t)sRT.bsdIfPtr,
                  sRT.enableCnt, sRT.disableCnt, sRT.lastEnableRet,
                  sRT.bsdIfFlags, sRT.bsdIfMtu,
                  (void *)(uintptr_t)sRT.pmPolicyPtr,
                  sRT.pmOffCancelRet, sRT.pmOnCancelRet,
                  sRT.outputDropPwr,
                  sRT.pmOffGateNull, sRT.pmOnGateNull,
                  sRT.pmAckOffCnt, sRT.pmAckOnCnt,
                  (void *)(uintptr_t)sRT.fNetIfPtr,
                  (void *)(uintptr_t)sRT.fTxPoolPtr,
                  (void *)(uintptr_t)sRT.fRxPoolPtr,
                  (void *)(uintptr_t)sRT.fTxQueuePtr,
                  (void *)(uintptr_t)sRT.fRxQueuePtr,
                  sRT.txCbCnt, sRT.txPktSent, sRT.txPktDrop,
                  sRT.rxInputCnt, sRT.rxPktOK, sRT.rxAllocFail,
                  sRT.rxEnqFail, sRT.rxCbCnt,
                  (void *)(uintptr_t)sRT.nexusProvPtr,
                  (void *)(uintptr_t)sRT.nifCtxPtr,
                  (void *)(uintptr_t)sRT.nexusArenaPtr,
                  (void *)(uintptr_t)sRT.asyncSentinel,
                  (void *)(uintptr_t)sRT.regObj90,
                  (void *)(uintptr_t)sRT.regObj98,
                  (void *)(uintptr_t)sRT.regObjA0);
        }, NULL);
    uint64_t stopDeadline;
    clock_interval_to_deadline(60, kSecondScale, &stopDeadline);
    thread_call_enter_delayed(stopTimer, stopDeadline);

    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
    sRT.stopStep = 2;
    if (tahoeBootThreadCall) {
        thread_call_cancel(tahoeBootThreadCall);
        thread_call_free(tahoeBootThreadCall);
        tahoeBootThreadCall = NULL;
    }
    XYLog("DEBUG %s [2] disableAdapter\n", __FUNCTION__);
    disableAdapter(NULL);
    sRT.stopStep = 3;
    XYLog("DEBUG %s [3] setLinkStatus\n", __FUNCTION__);
    setLinkStatus(kIONetworkLinkValid);
    sRT.stopStep = 4;
    XYLog("DEBUG %s [4] fHalService->detach pciNub=%p\n", __FUNCTION__, pciNub);
    fHalService->detach(pciNub);
    sRT.stopStep = 5;
    XYLog("DEBUG %s [5] ether_ifdetach ifp=%p\n", __FUNCTION__, ifp);
#if __IO80211_TARGET >= __MAC_26_0
    ifp->if_skywalk_rx = NULL;
#endif
    ether_ifdetach(ifp);
    sRT.stopStep = 6;
    // Release Skywalk queues and pools
    XYLog("DEBUG %s [6] releasing Skywalk queues/pools\n", __FUNCTION__);
    OSSafeReleaseNULL(fTxQueue);  sRT.fTxQueuePtr = 0;
    OSSafeReleaseNULL(fRxQueue);  sRT.fRxQueuePtr = 0;
    OSSafeReleaseNULL(fTxPool);   sRT.fTxPoolPtr = 0;
    OSSafeReleaseNULL(fRxPool);   sRT.fRxPoolPtr = 0;
    sRT.stopStep = 61;
    RT3_SET(15); // detachInterface entered
    XYLog("DEBUG %s [6b] detachInterface fNetIf=%p\n", __FUNCTION__, fNetIf);
    detachInterface(fNetIf, true);
    sRT.stopStep = 7;
    XYLog("DEBUG %s [7] release fNetIf\n", __FUNCTION__);
    OSSafeReleaseNULL(fNetIf);    sRT.fNetIfPtr = 0;
    sRT.stopStep = 8;
    XYLog("DEBUG %s [8] releaseAll\n", __FUNCTION__);
    releaseAll();
    sRT.stopStep = 9;
    XYLog("DEBUG %s [9] super::stop\n", __FUNCTION__);
    super::stop(provider);
    sRT.stopStep = 10;
    XYLog("DEBUG %s [10] DONE\n", __FUNCTION__);
    thread_call_cancel(stopTimer);
    thread_call_free(stopTimer);
}

void AirportItlwm::free()
{
    RT_SET(20);
    sRT.freeStep = 1;
    XYLog("DEBUG %s [1] entry fHalService=%p syncFrameTemplate=%p roamProfile=%p btcProfile=%p "
          "rtMask=0x%07x\n",
          __PRETTY_FUNCTION__, fHalService, syncFrameTemplate, roamProfile, btcProfile, sRT.rtMask);

    // Panic timer — catches hangs in IO80211Controller::free() chain
    thread_call_t freeTimer = thread_call_allocate(
        [](thread_call_param_t, thread_call_param_t) {
            if (!(sRT.rtMask & 0x200000))
                panic("AirportItlwm::free hung  "
                      "freeStep=%u rtMask=0x%07x rt2=0x%04x rt3=0x%04x | "
                      "stopStep=%u skFree=%u ss=%u | "
                      "ic=%d fl=0x%x pwr=%u link=0x%x | "
                      "evt=%u pm=%u(req=%u) wd=%u ioctl=%u(last=%d) "
                      "scanDone=%u ifType=0x%x | "
                      "scanReq=%u assoc=%u scanRes=%u "
                      "icfl=0x%x esslen=%u nodes=%u mfail=0x%x | "
                      "fVars=%p bsdIf=%p enCnt=%u disCnt=%u enRet=0x%x | "
                      "bsdFl=0x%x bsdMtu=%u | "
                      "pmPol=%p pmOffC=%u pmOnC=%u txDrop=%u "
                      "gateNullOff=%u gateNullOn=%u ackOff=%u ackOn=%u | "
                      "skNif=%p txP=%p rxP=%p txQ=%p rxQ=%p",
                      sRT.freeStep, sRT.rtMask, sRT.rtMask2, sRT.rtMask3,
                      sRT.stopStep, sRT.skFreeStep, sRT.startStep,
                      sRT.ic_state, sRT.if_flags, sRT.power_state,
                      sRT.linkStatus,
                      sRT.evtCount, sRT.postMsgCount, sRT.lastPmReq,
                      sRT.wdCount,
                      sRT.ioctlCount, sRT.lastIoctl,
                      sRT.scanDoneCount, sRT.ifType,
                      sRT.scanReqCount, sRT.assocCount, sRT.scanResCount,
                      sRT.ic_flags, sRT.ic_des_esslen,
                      sRT.nodeCount, sRT.matchFail,
                      (void *)(uintptr_t)sRT.fVarsPtr,
                      (void *)(uintptr_t)sRT.bsdIfPtr,
                      sRT.enableCnt, sRT.disableCnt, sRT.lastEnableRet,
                      sRT.bsdIfFlags, sRT.bsdIfMtu,
                      (void *)(uintptr_t)sRT.pmPolicyPtr,
                      sRT.pmOffCancelRet, sRT.pmOnCancelRet,
                      sRT.outputDropPwr,
                      sRT.pmOffGateNull, sRT.pmOnGateNull,
                      sRT.pmAckOffCnt, sRT.pmAckOnCnt,
                      (void *)(uintptr_t)sRT.fNetIfPtr,
                      (void *)(uintptr_t)sRT.fTxPoolPtr,
                      (void *)(uintptr_t)sRT.fRxPoolPtr,
                      (void *)(uintptr_t)sRT.fTxQueuePtr,
                      (void *)(uintptr_t)sRT.fRxQueuePtr);
        }, NULL);
    uint64_t freeDeadline;
    clock_interval_to_deadline(60, kSecondScale, &freeDeadline);
    thread_call_enter_delayed(freeTimer, freeDeadline);

    sRT.freeStep = 2;
    if (fHalService != NULL) {
        XYLog("DEBUG %s [2] releasing fHalService\n", __FUNCTION__);
        fHalService->release();
        fHalService = NULL;
    }
    sRT.freeStep = 3;
    if (syncFrameTemplate != NULL && syncFrameTemplateLength > 0) {
        IOFree(syncFrameTemplate, syncFrameTemplateLength);
        syncFrameTemplateLength = 0;
        syncFrameTemplate = NULL;
    }
    if (roamProfile != NULL) {
        IOFree(roamProfile, sizeof(struct apple80211_roam_profile_band_data));
        roamProfile = NULL;
    }
    if (btcProfile != NULL) {
        IOFree(btcProfile, sizeof(struct apple80211_btc_profiles_data));
        btcProfile = NULL;
    }
    sRT.freeStep = 4;
    XYLog("DEBUG %s [3] super::free\n", __FUNCTION__);
    super::free();
    RT_SET(21);
    thread_call_cancel(freeTimer);
    thread_call_free(freeTimer);
}

bool AirportItlwm::createWorkQueue()
{
    _fWorkloop = IO80211WorkQueue::workQueue();
    RT_SET(16);
    XYLog("DEBUG %s created IO80211WorkQueue _fWorkloop=%p\n", __FUNCTION__, _fWorkloop);
    return _fWorkloop != 0;
}

#if __IO80211_TARGET >= __MAC_26_0
IO80211WorkQueue *AirportItlwm::getWorkQueue() const
#else
IO80211WorkQueue *AirportItlwm::getWorkQueue()
#endif
{
    // getWorkQueue is called multiple times during IO80211Controller::start():
    //  #1 = createWorkQueue phase, #2 = CommandGate alloc (after RangingManager),
    //  #3 = inside CreatePostOffice, #4 = TimerSource alloc, #5 = TimerFactory alloc
    static int sGetWorkQueueCount = 0;
    if (++sGetWorkQueueCount <= 20)
        XYLog("DEBUG %s #%d returning %p\n", __FUNCTION__, sGetWorkQueueCount, _fWorkloop);
    return _fWorkloop;
}

void *AirportItlwm::getFaultReporterFromDriver()
{
    return io80211FaultReporter;
}

#if __IO80211_TARGET < __MAC_26_0
IOReturn AirportItlwm::enable(IO80211SkywalkInterface *netif)
{
    XYLog("DEBUG %s power_state=%u netif=%p bsdInterface=%p\n", __PRETTY_FUNCTION__, power_state, netif, bsdInterface);
    super::enable(netif);
    _fCommandGate->enable();
    if (power_state)
        enableAdapter(bsdInterface);
    else
        XYLog("DEBUG %s SKIPPED enableAdapter (power_state=0)\n", __FUNCTION__);
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::disable(IO80211SkywalkInterface *netif)
{
    XYLog("DEBUG %s power_state=%u\n", __PRETTY_FUNCTION__, power_state);
    disableAdapter(bsdInterface);
    super::disable(netif);
    setLinkStatus(kIONetworkLinkValid);
    return kIOReturnSuccess;
}
#endif

bool AirportItlwm::configureInterface(IONetworkInterface *netif)
{
    RT_SET(17);
    XYLog("DEBUG %s entry netif=%p power_state=%u\n", __FUNCTION__, netif, power_state);
    IONetworkData *nd;
    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;

    if (super::configureInterface(netif) == false) {
        XYLog("super failed\n");
        return false;
    }
    
    nd = netif->getParameter(kIONetworkStatsKey);
    if (!nd || !(fpNetStats = (IONetworkStats *)nd->getBuffer())) {
        XYLog("network statistics buffer unavailable?\n");
        return false;
    }
    ifp->netStat = fpNetStats;
    XYLog("DEBUG %s ether_ifattach ifp=%p netif=%p\n", __FUNCTION__, ifp, netif);
    ether_ifattach(ifp, OSDynamicCast(IOEthernetInterface, netif));
    RT_SET(26);
    fpNetStats->collisions = 0;

#if defined(__PRIVATE_SPI__) && __IO80211_TARGET < __MAC_26_0
    netif->configureOutputPullModel(fHalService->getDriverInfo()->getTxQueueSize(), 0, 0, IOEthernetInterface::kOutputPacketSchedulingModelNormal, 0);
#else
    XYLog("DEBUG %s Tahoe: skipping configureOutputPullModel\n", __FUNCTION__);
#endif
    XYLog("DEBUG %s DONE\n", __FUNCTION__);
    return true;
}

// On Sequoia (26.x), createInterface is not called by the Skywalk
// attachInterface(IOSkywalkInterface*, IOService*) path. BSD ifnet is
// created by IOSkywalkNetworkBSDClient after deferBSDAttach(false).
#if __IO80211_TARGET < __MAC_26_0
IONetworkInterface *AirportItlwm::createInterface()
{
    RT_SET(24);
    XYLog("DEBUG %s entry fNetIf=%p\n", __FUNCTION__, fNetIf);
    AirportItlwmEthernetInterface *netif = new AirportItlwmEthernetInterface;
    if (!netif) {
        XYLog("DEBUG %s FAIL: alloc AirportItlwmEthernetInterface\n", __FUNCTION__);
        return NULL;
    }
    if (!netif->initWithSkywalkInterfaceAndProvider(this, fNetIf)) {
        XYLog("DEBUG %s FAIL: initWithSkywalkInterfaceAndProvider\n", __FUNCTION__);
        netif->release();
        return NULL;
    }
    XYLog("DEBUG %s OK: netif=%p\n", __FUNCTION__, netif);
    return netif;
}
#endif

bool AirportItlwm::createMediumTables(const IONetworkMedium **primary)
{
    IONetworkMedium    *medium;

    OSDictionary *mediumDict = OSDictionary::withCapacity(2);
    if (mediumDict == NULL) {
        XYLog("Cannot allocate OSDictionary\n");
        return false;
    }
    
    medium = IONetworkMedium::medium(kIOMediumIEEE80211, 54000000);
    IONetworkMedium::addMedium(mediumDict, medium);
    medium->release();
    if (primary) {
        *primary = medium;
    }
    medium = IONetworkMedium::medium(kIOMediumIEEE80211None, 0);
    IONetworkMedium::addMedium(mediumDict, medium);
    medium->release();
    
    bool result = publishMediumDictionary(mediumDict);
    if (!result) {
        XYLog("Cannot publish medium dictionary!\n");
    }

    mediumDict->release();
    return result;
}

IOReturn AirportItlwm::selectMedium(const IONetworkMedium *medium) {
    setSelectedMedium(medium);
    return kIOReturnSuccess;
}

bool AirportItlwm::
setLinkStatus(UInt32 status, const IONetworkMedium * activeMedium, UInt64 speed, OSData * data)
{
    RT_SET(6);
    struct _ifnet *ifq = &fHalService->get80211Controller()->ic_ac.ac_if;
    XYLog("DEBUG %s status=0x%x (prev=0x%x) active=%d speed=%llu if_flags=0x%x ic_state=%d power_state=%u\n",
          __FUNCTION__, status, currentStatus, (status & kIONetworkLinkActive) != 0, speed,
          ifq->if_flags, fHalService->get80211Controller()->ic_state, power_state);
    if (status == currentStatus) {
        return true;
    }
    bool ret = super::setLinkStatus(status, activeMedium, speed, data);
    currentStatus = status;
    if (fNetIf) {
        if (status & kIONetworkLinkActive) {
#if defined(__PRIVATE_SPI__) && __IO80211_TARGET < __MAC_26_0
            bsdInterface->startOutputThread();
#endif
            getCommandGate()->runAction(setLinkStateGated, (void *)kIO80211NetworkLinkUp, (void *)0);
        } else if (!(status & kIONetworkLinkNoNetworkChange)) {
#if defined(__PRIVATE_SPI__) && __IO80211_TARGET < __MAC_26_0
            bsdInterface->stopOutputThread();
            bsdInterface->flushOutputQueue();
#endif
            ifq_flush(&ifq->if_snd);
            mq_purge(&fHalService->get80211Controller()->ic_mgtq);
            getCommandGate()->runAction(setLinkStateGated, (void *)kIO80211NetworkLinkDown, (void *)fHalService->get80211Controller()->ic_deauth_reason);
        }
    }
    return ret;
}

IOReturn AirportItlwm::
setLinkStateGated(OSObject *target, void *arg0, void *arg1, void *arg2, void *arg3)
{
    RT_SET(7);
    sRT.lastLinkState = (int)(uint64_t)arg0;
    sRT.linkSetCount++;
    AirportItlwm *that = OSDynamicCast(AirportItlwm, target);
    XYLog("DEBUG %s linkState=%llu deauthReason=%u power_state=%u\n",
          __FUNCTION__, (uint64_t)arg0, (unsigned int)(uint64_t)arg1, that->power_state);
#if __IO80211_TARGET >= __MAC_26_0
    XYLog("DEBUG %s Tahoe: calling IO80211InfraInterface::setLinkState fNetIf=%p\n", __FUNCTION__, that->fNetIf);
    IOReturn ret = ((IO80211InfraInterface *)that->fNetIf)->setLinkState((IO80211LinkState)(uint64_t)arg0, (unsigned int)(uint64_t)arg1, false, 0, 0);
#else
    IOReturn ret = that->fNetIf->setLinkState((IO80211LinkState)(uint64_t)arg0, (unsigned int)(uint64_t)arg1);
#endif
    if (ret == kIOReturnSuccess) RT_SET(14);
    if (airportItlwmRegDiagEnabled(kAirportItlwmRegDiagModeControl)) {
        sRegDiag.snapshot.linkStateCount++;
        sRegDiag.snapshot.lastLinkStateResult = static_cast<int32_t>(ret);
        sRegDiag.snapshot.lastLinkState = static_cast<int32_t>((uint64_t)arg0);
        airportItlwmRegDiagTrace(kAirportItlwmRegDiagTraceLinkState,
                                 kAirportItlwmRegDiagPathLink, ret,
                                 static_cast<int32_t>((uint64_t)arg0),
                                 static_cast<uint64_t>((uint64_t)arg1), 0);
    }
    XYLog("DEBUG %s setLinkState ret=0x%x\n", __FUNCTION__, ret);
    RT2_SET(13);
    that->fNetIf->setRunningState((IO80211LinkState)(uint64_t)arg0 == kIO80211NetworkLinkUp);
    that->postMessage(that->fNetIf, APPLE80211_M_LINK_CHANGED, NULL, 0, true);
    that->postMessage(that->fNetIf, APPLE80211_M_BSSID_CHANGED, NULL, 0, true);
    that->postMessage(that->fNetIf, APPLE80211_M_SSID_CHANGED, NULL, 0, true);
    if ((IO80211LinkState)(uint64_t)arg0 == kIO80211NetworkLinkUp) {
        that->fNetIf->reportLinkStatus(3, 0x80);
    } else {
        that->fNetIf->reportLinkStatus(1, 0);
    }
#if __IO80211_TARGET < __MAC_26_0
    if (that->bsdInterface) {
        XYLog("DEBUG %s calling bsdInterface->setLinkState bsdInterface=%p\n", __FUNCTION__, that->bsdInterface);
        that->bsdInterface->setLinkState((IO80211LinkState)(uint64_t)arg0);
    }
#endif
    XYLog("DEBUG %s DONE ret=0x%x\n", __FUNCTION__, ret);
    return ret;
}

#if defined(__PRIVATE_SPI__) && __IO80211_TARGET < __MAC_26_0
IOReturn AirportItlwm::outputStart(IONetworkInterface *interface, IOOptionBits options)
{
    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
    mbuf_t m = NULL;
    if (ifq_is_oactive(&ifp->if_snd))
        return kIOReturnNoResources;
    while (kIOReturnSuccess == interface->dequeueOutputPackets(1, &m)) {
        if (outputPacket(m, NULL)!= kIOReturnOutputSuccess ||
            ifq_is_oactive(&ifp->if_snd))
            return kIOReturnNoResources;
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::networkInterfaceNotification(
                    IONetworkInterface * interface,
                    uint32_t              type,
                    void *                  argument )
{
    XYLog("%s\n", __FUNCTION__);
    return kIOReturnSuccess;
}
#endif

extern const char* hexdump(uint8_t *buf, size_t len);

UInt32 AirportItlwm::outputPacket(mbuf_t m, void *param)
{
    static int sOutputCount = 0;
    if (++sOutputCount <= 5)
        XYLog("DEBUG %s #%d m=%p param=%p ic_state=%d\n", __FUNCTION__, sOutputCount, m, param,
              fHalService->get80211Controller()->ic_state);
    IOReturn ret = kIOReturnOutputSuccess;
    uint32_t diagLength = 0;
    const bool diagEapol = airportItlwmRegDiagMbufIsEapol(m, &diagLength);

    if (pmPowerState != kPowerStateOn) {
        sRT.outputDropPwr++;
        airportItlwmRegDiagRecordData(kAirportItlwmRegDiagPathTx, diagLength,
                                      diagEapol, kIOReturnOutputDropped);
        if (m && mbuf_type(m) != MBUF_TYPE_FREE)
            freePacket(m);
        return kIOReturnOutputDropped;
    }

    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;

    if (fHalService->get80211Controller()->ic_state != IEEE80211_S_RUN || ifp->if_snd.queue == NULL) {
        airportItlwmRegDiagRecordData(kAirportItlwmRegDiagPathTx, diagLength,
                                      diagEapol, kIOReturnOutputDropped);
        if (m && mbuf_type(m) != MBUF_TYPE_FREE)
            freePacket(m);
        return kIOReturnOutputDropped;
    }
    if (diagEapol && airportItlwmRegDiagShouldBlock(kAirportItlwmRegDiagBlockEapolTx)) {
        airportItlwmRegDiagRecordBlock(kAirportItlwmRegDiagBlockEapolTx,
                                       kAirportItlwmRegDiagPathTx, diagLength);
        airportItlwmRegDiagRecordData(kAirportItlwmRegDiagPathTx, diagLength,
                                      true, kIOReturnOutputDropped);
        if (m && mbuf_type(m) != MBUF_TYPE_FREE)
            freePacket(m);
        return kIOReturnOutputDropped;
    }
    if (airportItlwmRegDiagShouldBlock(kAirportItlwmRegDiagBlockTx)) {
        airportItlwmRegDiagRecordBlock(kAirportItlwmRegDiagBlockTx,
                                       kAirportItlwmRegDiagPathTx, diagLength);
        airportItlwmRegDiagRecordData(kAirportItlwmRegDiagPathTx, diagLength,
                                      diagEapol, kIOReturnOutputDropped);
        if (m && mbuf_type(m) != MBUF_TYPE_FREE)
            freePacket(m);
        return kIOReturnOutputDropped;
    }
    if (m == NULL) {
        XYLog("%s m==NULL!!\n", __FUNCTION__);
        ifp->netStat->outputErrors++;
        ret = kIOReturnOutputDropped;
    }
    if (!(mbuf_flags(m) & MBUF_PKTHDR) ){
        XYLog("%s pkthdr is NULL!!\n", __FUNCTION__);
        ifp->netStat->outputErrors++;
        freePacket(m);
        ret = kIOReturnOutputDropped;
    }
    if (mbuf_type(m) == MBUF_TYPE_FREE) {
        XYLog("%s mbuf is FREE!!\n", __FUNCTION__);
        ifp->netStat->outputErrors++;
        ret = kIOReturnOutputDropped;
    }
    size_t len = mbuf_len(m);
    ether_header_t *eh = (ether_header_t *)mbuf_data(m);
    if (len >= sizeof(ether_header_t) && eh->ether_type == htons(ETHERTYPE_PAE)) { // EAPOL packet
        const char* dump = hexdump((uint8_t*)mbuf_data(m), len);
        XYLog("output EAPOL packet, len: %zu, data: %s\n", len, dump ? dump : "Failed to allocate memory");
        if (dump)
            IOFree((void*)dump, 3 * len + 1);
    }
    if (!ifp->if_snd.queue->lockEnqueue(m)) {
        freePacket(m);
        ret = kIOReturnOutputDropped;
    }
    (*ifp->if_start)(ifp);
    airportItlwmRegDiagRecordData(kAirportItlwmRegDiagPathTx, diagLength,
                                  diagEapol, ret);
    return ret;
}

const OSString * AirportItlwm::newVendorString() const
{
    return OSString::withCString("Apple");
}

const OSString * AirportItlwm::newModelString() const
{
    return OSString::withCString(fHalService->getDriverInfo()->getFirmwareName());
}

IOReturn AirportItlwm::getHardwareAddress(IOEthernetAddress *addrP)
{
    if (IEEE80211_ADDR_EQ(etheranyaddr, fHalService->get80211Controller()->ic_myaddr))
        return kIOReturnError;
    else {
        IEEE80211_ADDR_COPY(addrP, fHalService->get80211Controller()->ic_myaddr);
        return kIOReturnSuccess;
    }
}

IOReturn AirportItlwm::setHardwareAddress(const void *addrP, UInt32 addrBytes)
{
    if (!fNetIf || !addrP)
        return kIOReturnError;
    if_setlladdr(&fHalService->get80211Controller()->ic_ac.ac_if, (const UInt8 *)addrP);
    if (fHalService->get80211Controller()->ic_state > IEEE80211_S_INIT) {
#if __IO80211_TARGET >= __MAC_26_0
        fHalService->disable(NULL);
        fHalService->enable(NULL);
#else
        fHalService->disable(bsdInterface);
        fHalService->enable(bsdInterface);
#endif
    }
    return kIOReturnSuccess;
}

UInt32 AirportItlwm::getFeatures() const
{
    return fHalService->getDriverInfo()->supportedFeatures();
}

IOReturn AirportItlwm::setPromiscuousMode(IOEnetPromiscuousMode mode)
{
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::setMulticastMode(IOEnetMulticastMode mode)
{
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::setMulticastList(IOEthernetAddress* addr, UInt32 len)
{
    return fHalService->getDriverController()->setMulticastList(addr, len);
}

IOReturn AirportItlwm::getPacketFilters(const OSSymbol *group, UInt32 *filters) const
{
    IOReturn    rtn = kIOReturnSuccess;
    if (group == gIOEthernetWakeOnLANFilterGroup && magicPacketSupported)
        *filters = kIOEthernetWakeOnMagicPacket;
    else if (group == gIONetworkFilterGroup)
        *filters = kIOPacketFilterMulticast | kIOPacketFilterPromiscuous;
    else
        rtn = IOEthernetController::getPacketFilters(group, filters);
    return rtn;
}

SInt32 AirportItlwm::
enableFeature(IO80211FeatureCode code, void *data)
{
    XYLog("DEBUG %s code=%d\n", __FUNCTION__, code);
    if (code == kIO80211Feature80211n) {
        return 0;
    }
    return 102;
}

#if __IO80211_TARGET >= __MAC_26_0
// vtable dump[429] = releaseFlowQueue at vptr+0xD58.  Not called during start().
static int sReleaseFlowQueueCallCount = 0;
void *AirportItlwm::releaseFlowQueue(IO80211FlowQueue *)
{
    int n = ++sReleaseFlowQueueCallCount;
    if (n <= 50)
        XYLog("DEBUG [vtable429] releaseFlowQueue #%d\n", n);
    return nullptr;
}

// vtable dump[431] = getDriverLogStream (pure virtual) at vptr+0xD68.
// IO80211Controller::start() calls this to get CCLogStream* for setGlobalLogger(),
// then createIOReporters() uses it.  Must return valid CCLogStream*.
// (Dump indexes include 2 RTTI entries, so vptr offset = (431-2)*8 = 0xD68.)
static int sGetDriverLogStreamCallCount = 0;
void *AirportItlwm::getDriverLogStream()
{
    int n = ++sGetDriverLogStreamCallCount;
    if (n <= 50)
        XYLog("DEBUG [vtable431] getDriverLogStream #%d driverLogStream=%p rc=%d\n",
              n, driverLogStream, driverLogStream ? driverLogStream->getRetainCount() : -1);
    return driverLogStream;
}
#endif

bool AirportItlwm::getLogPipes(CCPipe**logPipe, CCPipe**eventPipe, CCPipe**snapshotsPipe)
{
    XYLog("DEBUG %s logPipe=%p dataPipe=%p snapPipe=%p\n", __FUNCTION__,
          driverLogPipe, driverDataPathPipe, driverSnapshotsPipe);
    bool ret = false;
    if (logPipe) {
        *logPipe = driverLogPipe;
        ret = true;
    }
    if (eventPipe) {
        *eventPipe = driverDataPathPipe;
        ret = true;
    }
    if (snapshotsPipe) {
        *snapshotsPipe = driverSnapshotsPipe;
        ret = true;
    }
    return ret;
}

#define APPLE80211_CAPA_AWDL_FEATURE_AUTO_UNLOCK    0x00000004
#define APPLE80211_CAPA_AWDL_FEATURE_WOW            0x00000080

IOReturn AirportItlwm::
getCARD_CAPABILITIES(OSObject *object,
                                     struct apple80211_capability_data *cd)
{
#if __IO80211_TARGET >= __MAC_26_0
    static_assert(sizeof(struct apple80211_capability_data) == 0x1c,
                  "Tahoe apple80211_capability_data must be 0x1c bytes");
#endif
    uint32_t caps = fHalService->get80211Controller()->ic_caps;

    // Tahoe AppleBCMWLANCore writes advanced capability bytes through offset
    // +0x17. The old short local header only zeroed the prefix and leaked
    // uninitialized tail bytes into IO80211Family/WCL, which could advertise
    // arbitrary advanced AKM/capability state on hidden join paths.
    memset(cd, 0, sizeof(struct apple80211_capability_data));
    
    if (caps & IEEE80211_C_WEP)
        cd->capabilities[0] |= 1 << APPLE80211_CAP_WEP;
    if (caps & IEEE80211_C_RSN)
        cd->capabilities[0] |= 1 << APPLE80211_CAP_TKIP | 1 << APPLE80211_CAP_AES_CCM;
    // Disable not implemented capabilities
    // if (caps & IEEE80211_C_PMGT)
    //     cd->capabilities[0] |= 1 << APPLE80211_CAP_PMGT;
    // if (caps & IEEE80211_C_IBSS)
    //     cd->capabilities[0] |= 1 << APPLE80211_CAP_IBSS;
    // if (caps & IEEE80211_C_HOSTAP)
    //     cd->capabilities[0] |= 1 << APPLE80211_CAP_HOSTAP;
    // AES not enabled, like on Apple cards
    
    if (caps & IEEE80211_C_SHSLOT)
        cd->capabilities[1] |= 1 << (APPLE80211_CAP_SHSLOT - 8);
    if (caps & IEEE80211_C_SHPREAMBLE)
        cd->capabilities[1] |= 1 << (APPLE80211_CAP_SHPREAMBLE - 8);
    if (caps & IEEE80211_C_RSN)
        cd->capabilities[1] |= 1 << (APPLE80211_CAP_WPA1 - 8) | 1 << (APPLE80211_CAP_WPA2 - 8) | 1 << (APPLE80211_CAP_TKIPMIC - 8);
    // Disable not implemented capabilities
    // if (caps & IEEE80211_C_TXPMGT)
    //     cd->capabilities[1] |= 1 << (APPLE80211_CAP_TXPMGT - 8);
    // if (caps & IEEE80211_C_MONITOR)
    //     cd->capabilities[1] |= 1 << (APPLE80211_CAP_MONITOR - 8);
    // WPA not enabled, like on Apple cards

    cd->version = APPLE80211_VERSION;
    // CR-032: after the Tahoe carrier-size fix, the next mismatch was content.
    // AppleBCMWLANCore::getCARD_CAPABILITIES() never sets cap[2] bit 7,
    // cap[3] bit 3, or cap[6] bit 7, but the old local constants
    // 0xEF / 0x2B / 0x8C advertised exactly those impossible bits into the
    // still-active hidden association path. Sanitize the hard-coded cluster to
    // the Apple-consistent shape before the hidden join queue consumes it.
    cd->capabilities[2] = 0x6F;
    cd->capabilities[3] = 0x27;
    cd->capabilities[5] = 0x40;
    cd->capabilities[6] = 0x0C;
    *(uint16_t *)&cd->capabilities[8] = 0x201;
//
//    cd->capabilities[2] |= 0x10;
//    cd->capabilities[5] |= 0x1;
//
//    cd->capabilities[2] |= 0x2;
//
//    cd->capabilities[3] |= 0x20;
//
//    cd->capabilities[0] |= 0x80;
//
//    cd->capabilities[3] |= 0x80;
//    cd->capabilities[4] |= 0x4;
//
//    cd->capabilities[4] |= 0x1;
//    cd->capabilities[3] |= 0x1;
//    cd->capabilities[6] |= 0x8;
//
//    cd->capabilities[3] |= 3;
//    cd->capabilities[4] |= 2;
//    cd->capabilities[6] |= 0x10;
//    cd->capabilities[5] |= 0x20;
//    cd->capabilities[5] |= 0x80;
//
//    if (cd->capabilities[6] & 0x20) {
//        cd->capabilities[2] |= 8;
//    }
//    cd->capabilities[5] |= 8;
//    cd->capabilities[8] |= 2;
//
//    cd->capabilities[11] |= (2 | 4 | 8 | 0x10 | 0x20 | 0x40 | 0x80);
    
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getDRIVER_VERSION(OSObject *object,
                                  struct apple80211_version_data *hv)
{
    if (!hv)
        return kIOReturnError;
    hv->version = APPLE80211_VERSION;
    // Tahoe builds inject ITLWM_COMMIT_HASH via scripts/build_tahoe.sh.
    // Use that value here instead of the legacy GIT_COMMIT project setting:
    // the latter is empty in current Tahoe builds, which made ioreg/logs
    // indistinguishable across rebuilds while debugging bring-up.
    snprintf(hv->string, sizeof(hv->string), "itlwm: %s%s fw: %s",
             ITLWM_VERSION, ITLWM_COMMIT_SUFFIX,
             fHalService->getDriverInfo()->getFirmwareVersion());
    hv->string_len = strlen(hv->string);
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getHARDWARE_VERSION(OSObject *object,
                                    struct apple80211_version_data *hv)
{
    if (!hv)
        return kIOReturnError;
    hv->version = APPLE80211_VERSION;
    strncpy(hv->string, fHalService->getDriverInfo()->getFirmwareVersion(), sizeof(hv->string));
    hv->string_len = strlen(fHalService->getDriverInfo()->getFirmwareVersion());
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getCOUNTRY_CODE(OSObject *object,
                                struct apple80211_country_code_data *cd)
{
    char user_override_cc[3];
    const char *cc_fw = fHalService->getDriverInfo()->getFirmwareCountryCode();
    
    if (!cd)
        return kIOReturnError;
    cd->version = APPLE80211_VERSION;
    memset(user_override_cc, 0, sizeof(user_override_cc));
    PE_parse_boot_argn("itlwm_cc", user_override_cc, 3);
    /* user_override_cc > firmware_cc > geo_location_cc */
    strncpy((char*)cd->cc, user_override_cc[0] ? user_override_cc : ((cc_fw[0] == 'Z' && cc_fw[1] == 'Z' && geo_location_cc[0]) ? geo_location_cc : cc_fw), sizeof(cd->cc));
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getPLATFORM_CONFIG(OSObject *object,
                   struct apple80211_platform_config *data)
{
    if (!data)
        return kIOReturnError;

    bzero(data, sizeof(*data));

    // AppleBCMWLAN's Tahoe producer path for APPLE80211_IOC_PLATFORM_CONFIG is
    // not a generic base-class stub. The real vendor implementation populates a
    // packed 7-byte feature bitmap from IOService properties and cached config:
    //
    //   byte0 = wlan.6GHz.supported
    //   byte1 = wlan.ant-inefficiency-mitigation.enabled
    //   byte2 = wlan.externallypowered
    //   byte3 = wlan.adaptiveroaming.enabled
    //   byte4 = wlan.dfrts (property presence)
    //   byte5 = bcom.feature.pmmcast after loading wlan.ignore.mcast
    //   byte6 = wlan.ocl.enabled
    //
    // References recovered from the Apple 26.3 producer path on the remote
    // Ghidra host:
    // - real handler body at 0xffffff8001638544 in AppleBCMWLANCore
    // - consumer copy in WCLDeviceConfiguration::setPlatformConfig only reads
    //   these first seven bytes
    // - live Tahoe logs on our side proved that leaving this IOC unsupported
    //   keeps WCL in DRIVER_UNAVAILABLE
    //
    // The recovered producer does not query controller-local state first. It
    // routes through the hidden +0x1510 interface-side object and then consults
    // that object's provider when properties are published under "IOService".
    // Mirror that topology here instead of reading directly from the
    // controller, which would make Tahoe property resolution depend on a
    // non-Apple source object.
    IORegistryEntry *sources[2] = {
        getTahoeHiddenInterfaceObject(this),
        getTahoeHiddenInterfaceProvider(this)
    };
    bool boolValue = false;
    uint32_t u32 = 0;

    for (IORegistryEntry *source : sources) {
        if (source == NULL)
            continue;

        if (!data->flags && copyBoolProperty(source, "wlan.6GHz.supported", &boolValue) && boolValue)
            data->flags |= 0x00000001u;
        if (!(data->flags & 0x00000100u) &&
            copyBoolProperty(source, "wlan.ant-inefficiency-mitigation.enabled", &boolValue) &&
            boolValue)
            data->flags |= 0x00000100u;
        if (!(data->flags & 0x00010000u) &&
            copyPresenceProperty(source, "wlan.externallypowered"))
            data->flags |= 0x00010000u;
        if (!(data->flags & 0x01000000u) &&
            copyBoolProperty(source, "wlan.adaptiveroaming.enabled", &boolValue) &&
            boolValue)
            data->flags |= 0x01000000u;
        if (!(data->value_4 & 0x0001u) &&
            copyPresenceProperty(source, "wlan.dfrts"))
            data->value_4 |= 0x0001u;
        if (!(data->value_4 & 0x0100u) &&
            copyUInt32Property(source, "wlan.ignore.mcast", &u32) &&
            u32 != 0)
            data->value_4 |= 0x0100u;
        if (!data->value_6 &&
            copyBoolProperty(source, "wlan.ocl.enabled", &boolValue) &&
            boolValue)
            data->value_6 = 1;
    }

    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setCOUNTRY_CODE(OSObject *object, struct apple80211_country_code_data *data)
{
    XYLog("%s cc=%s\n", __FUNCTION__, data->cc);
    if (data && data->cc[0] != 120 && data->cc[0] != 88) {
        memcpy(geo_location_cc, data->cc, sizeof(geo_location_cc));
        postMessage(fNetIf, APPLE80211_M_COUNTRY_CODE_CHANGED, NULL, 0, true);
    }
    return kIOReturnSuccess;
}

IO80211SkywalkInterface *AirportItlwm::
getPrimarySkywalkInterface(void)
{
    // Tahoe family bootstrap/current-link paths still consult controller slot
    // `+0xc80` for the bound Skywalk interface before deeper request routing.
    // Returning `fNetIf` here keeps that family-visible primary-interface seam
    // aligned with the already created infrastructure interface.
    return OSDynamicCast(IO80211SkywalkInterface, fNetIf);
}

IOReturn AirportItlwm::
getSSID(OSObject *object, struct apple80211_ssid_data *sd)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    memset(sd, 0, sizeof(*sd));
    sd->version = APPLE80211_VERSION;
    if (ic->ic_state == IEEE80211_S_RUN) {
        memcpy(sd->ssid_bytes, ic->ic_des_essid,
               strlen((const char *)ic->ic_des_essid));
        sd->ssid_len = (uint32_t)strlen((const char *)ic->ic_des_essid);
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setSSID(OSObject *object, struct apple80211_ssid_data *sd)
{
    RT2_SET(5);
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getCHANNEL(OSObject *object, struct apple80211_channel_data *cd)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    memset(cd, 0, sizeof(*cd));
    cd->version = APPLE80211_VERSION;
    cd->channel.version = APPLE80211_VERSION;
    if (ic->ic_state == IEEE80211_S_RUN) {
        cd->channel.channel = ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);
        cd->channel.flags = ieeeChanFlag2apple(ic->ic_bss->ni_chan->ic_flags,
                                               ic->ic_bss->ni_chw);
    }
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setCHANNEL(OSObject *object, struct apple80211_channel_data *data)
{
    XYLog("%s channel=%d\n", __FUNCTION__, data->channel.channel);
    return kIOReturnError;
}

IOReturn AirportItlwm::
getBSSID(OSObject *object, struct apple80211_bssid_data *bd)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    memset(bd, 0, sizeof(*bd));
    bd->version = APPLE80211_VERSION;
    if (ic->ic_state == IEEE80211_S_RUN)
        memcpy(bd->bssid.octet, ic->ic_bss->ni_bssid, APPLE80211_ADDR_LEN);
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setBSSID(OSObject *object, struct apple80211_bssid_data *data)
{
    XYLog("%s bssid=%s\n", __FUNCTION__, ether_sprintf(data->bssid.octet));
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getPOWER(OSObject *object,
                         struct apple80211_power_data *pd)
{
    if (!pd)
        return kIOReturnError;
    pd->version = APPLE80211_VERSION;
    pd->num_radios = 4;
    pd->power_state[0] = power_state;
    pd->power_state[1] = power_state;
    pd->power_state[2] = power_state;
    pd->power_state[3] = power_state;
    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
    XYLog("DEBUG %s returning power_state=%u if_flags=0x%x ic_state=%d pmPowerState=%u\n",
          __FUNCTION__, power_state, ifp->if_flags, fHalService->get80211Controller()->ic_state, pmPowerState);
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setPOWER(OSObject *object,
                         struct apple80211_power_data *pd)
{
    RT2_SET(6);
    if (!pd)
        return kIOReturnError;
    XYLog("%s num_radios=%d req=%u cur=%u pmPowerState=%u\n",
          __FUNCTION__, pd->num_radios,
          pd->num_radios > 0 ? pd->power_state[0] : 0, power_state, pmPowerState);
    if (pd->num_radios > 0) {
        const uint32_t requestedState = pd->power_state[0];

        // AppleBCMWLANCore::setPOWER(...) does not call
        // handlePowerStateChange(...) directly during bootstrap. The recovered
        // producer caches the requested state into +0x289c and lets
        // setupDriver() consume it later. Applying the transient early OFF
        // request immediately was the exact local bug that drove
        // DRIVER_UNAVAILABLE and kept isDriverAvailable=0 on build 5da9d59.
        tahoeRequestedPowerState = (uint8_t)requestedState;
        if (tahoeBootstrapPowerWindowOpen) {
            tahoeBootstrapPowerPending = true;
            XYLog("DEBUG %s deferred bootstrap power req=%u cur=%u pending=%d\n",
                  __FUNCTION__, requestedState, power_state, tahoeBootstrapPowerPending);
            if (requestedState == power_state) {
                tahoeBootstrapPowerPending = false;
                tahoeBootstrapPowerWindowOpen = false;
                XYLog("DEBUG %s bootstrap power converged on live state=%u; closing deferred path\n",
                      __FUNCTION__, requestedState);
            }
            return kIOReturnSuccess;
        }
#if __IO80211_TARGET >= __MAC_26_0
        handlePowerStateChange(requestedState, NULL);
#else
        handlePowerStateChange(requestedState, bsdInterface);
#endif
    }
    return kIOReturnSuccess;
}

#if __IO80211_TARGET < __MAC_26_0
SInt32 AirportItlwm::apple80211_ioctl(IO80211SkywalkInterface *interface,unsigned long cmd,void *data, bool b1, bool b2)
{
    if (!ml_at_interrupt_context())
        XYLog("%s cmd: %s b1: %d b2: %d\n", __FUNCTION__, convertApple80211IOCTLToString((unsigned int)cmd), b1, b2);
    return super::apple80211_ioctl(interface, cmd, data, b1, b2);
}
#endif

SInt32 AirportItlwm::handleCardSpecific(IO80211SkywalkInterface *interface,unsigned long cmd,void *data,bool isSet)
{
    if (!ml_at_interrupt_context())
        XYLog("DEBUG %s cmd=%s(%lu) isSet=%d interface=%p data=%p\n",
              __FUNCTION__, convertApple80211IOCTLToString((unsigned int)cmd), cmd,
              isSet, interface, data);

    // Keep the carried Tahoe card-specific bridge for the visible set-side and
    // hidden-association selectors that arrive on this controller seam.
    // Public request-number fallback is now tracked separately on interface
    // slot `[411] isCommandProhibited(int)`, so `handleCardSpecific(...)`
    // remains only the supplementary ingress for selectors that never use that
    // public request gate.
    if (data != nullptr && interface != nullptr) {
        apple80211req req;
        bzero(&req, sizeof(req));
        req.req_type = (UInt)cmd;
        req.req_data = data;
        IOReturn ret = routeTahoeSkywalkIoctl(interface, &req,
                                              isSet ? SIOCSA80211 : SIOCGA80211);
        if (!ml_at_interrupt_context()) {
            XYLog("DEBUG %s local-route cmd=%s(%lu) ret=0x%x\n",
                  __FUNCTION__, convertApple80211IOCTLToString((unsigned int)cmd), cmd, ret);
        }
        if (ret != kIOReturnUnsupported)
            return ret;
    }

    return kIOReturnUnsupported;
}

IOReturn AirportItlwm::enableAdapter(IONetworkInterface *netif)
{
    RT_SET(9);
    sRT.enableCnt++;
    struct ieee80211com *ic = fHalService ? fHalService->get80211Controller() : NULL;
    XYLog("DEBUG %s netif=%p power_state=%u pmPowerState=%u ic_state=%d\n",
          __FUNCTION__, netif, power_state, pmPowerState, ic ? ic->ic_state : -1);
    if (!fHalService) {
        XYLog("DEBUG %s ABORT: fHalService is NULL\n", __FUNCTION__);
        return kIOReturnNotReady;
    }
    fHalService->enable(netif);
    struct _ifnet *ifp = &ic->ic_ac.ac_if;
    XYLog("DEBUG %s post-enable: ic_state=%d if_flags=0x%x\n",
          __FUNCTION__, ic->ic_state, ifp->if_flags);
    if (watchdogTimer) {
        watchdogTimer->setTimeoutMS(kWatchDogTimerPeriod);
        watchdogTimer->enable();
    }
    sRT.lastEnableRet = kIOReturnSuccess;
    return kIOReturnSuccess;
}

void AirportItlwm::disableAdapterCore(IONetworkInterface *netif)
{
    RT_SET(10);
    sRT.disableCnt++;
    XYLog("DEBUG %s netif=%p power_state=%u pmPowerState=%u\n", __FUNCTION__, netif, power_state, pmPowerState);
    if (watchdogTimer) {
        watchdogTimer->cancelTimeout();
        watchdogTimer->disable();
    }
    if (fHalService)
        fHalService->disable(netif);
}

void AirportItlwm::disableAdapter(IONetworkInterface *netif)
{
    disableAdapterCore(netif);
    publishTahoeDriverReadyState(this, false);
}

//
// State machine matching Apple's AppleBCMWLANCore::handlePowerStateChange.
// States: 0=OFF, 1=ON, 4=STANDBY
// Transitions:
//   cur=1→req=0: powerOff        cur=0→req=1: powerOn
//   cur=4→req=0: powerOff        cur=4→req=1: powerOn
//   cur=0→req=4: powerOn         cur=1→req=4: powerOff
//   other: error (-1)
// On powerOn/powerOff failure, state is rolled back.
//
int AirportItlwm::handlePowerStateChange(uint32_t newState, IONetworkInterface *netif)
{
    uint8_t prevState = power_state;
    int err = 0;

    XYLog("DEBUG %s cur=%u req=%u\n", __FUNCTION__, prevState, newState);

    if ((newState == kWiFiPowerOff && prevState == kWiFiPowerOn) ||
        (newState == kWiFiPowerOff && prevState == kWiFiPowerStandby)) {
        // ON→OFF or STANDBY→OFF: power off
        power_state = kWiFiPowerOff;
        net80211_ifstats(fHalService->get80211Controller());
        disableAdapterCore(netif);
    }
    else if (newState == kWiFiPowerOn && (prevState == kWiFiPowerOff || prevState == kWiFiPowerStandby)) {
        // OFF→ON or STANDBY→ON: power on
        power_state = kWiFiPowerOn;
        enableAdapter(netif);
    }
    else if (newState == kWiFiPowerStandby && prevState == kWiFiPowerOff) {
        // OFF→STANDBY: power on (into standby mode)
        power_state = kWiFiPowerStandby;
        enableAdapter(netif);
    }
    else if (newState == kWiFiPowerStandby && prevState == kWiFiPowerOn) {
        // ON→STANDBY: power off (into standby)
        power_state = kWiFiPowerStandby;
        net80211_ifstats(fHalService->get80211Controller());
        disableAdapterCore(netif);
    }
    else if (newState == prevState) {
        // Same state — no-op
        XYLog("DEBUG %s already in state %u, no-op\n", __FUNCTION__, prevState);
    }
    else {
        // Invalid transition
        XYLog("DEBUG %s INVALID transition %u → %u\n", __FUNCTION__, prevState, newState);
        err = -1;
    }

    if (err) {
        XYLog("DEBUG %s FAILED, rollback %u → %u\n", __FUNCTION__, power_state, prevState);
        power_state = prevState;
    }
    else if (fNetIf) {
        if (newState != prevState) {
            // Apple separates generic WiFi power transitions from the
            // signalDriverReady producer. handlePowerStateChange() drives only
            // powerOn()/powerOff() and the POWER_CHANGED bulletin.
            // The recovered event maps mark POWER_CHANGED as mandatory for real
            // setPowerState transitions, not as a bootstrap sticky event and
            // not for no-op req==cur calls. Live build 36e4cc3 proved the
            // earlier local behavior was wrong: posting POWER_CHANGED from
            // start() and from no-op 1->1 requests fed false
            // SSM_EVENT_SYSTEM_POWER_OFF/ON edges into WCL.
            postMessage(fNetIf, APPLE80211_M_POWER_CHANGED, NULL, 0, true);
        } else {
            XYLog("DEBUG %s no state change (%u), suppressing ready/power notifications\n",
                  __FUNCTION__, newState);
        }
    }

    return err;
}

void AirportItlwm::handleSystemPowerStateChange(bool powerOn, IONetworkInterface *netif)
{
    XYLog("DEBUG %s powerOn=%d power_state=%u pmPowerState=%u netif=%p\n",
          __FUNCTION__, powerOn ? 1 : 0, power_state, pmPowerState, netif);

    // Apple exposes a separate powerOffSystem()/powerOnSystem() path in
    // addition to handlePowerStateChange(). Preserve the logical WiFi
    // power_state across sleep/wake and drive only adapter quiesce/resume plus
    // the system POWER_CHANGED bulletin here.
    if (powerOn) {
        if (power_state)
            enableAdapter(netif);
        else
            XYLog("DEBUG %s SKIPPED enableAdapter (power_state=0)\n", __FUNCTION__);
    } else {
        if (power_state)
            disableAdapterCore(netif);
        else
            XYLog("DEBUG %s SKIPPED disableAdapter (power_state=0)\n", __FUNCTION__);
    }

    if (fNetIf)
        postMessage(fNetIf, APPLE80211_M_POWER_CHANGED, NULL, 0, true);
}

IOReturn AirportItlwm::
tsleepHandler(OSObject* owner, void* arg0, void* arg1, void* arg2, void* arg3)
{
    AirportItlwm* dev = OSDynamicCast(AirportItlwm, owner);
    if (dev == 0)
        return kIOReturnError;
    
    if (arg1 == 0) {
        if (_fCommandGate->commandSleep(arg0, THREAD_INTERRUPTIBLE) == THREAD_AWAKENED)
            return kIOReturnSuccess;
        else
            return kIOReturnTimeout;
    } else {
        AbsoluteTime deadline;
        clock_interval_to_deadline((*(int*)arg1), kNanosecondScale, reinterpret_cast<uint64_t*> (&deadline));
        if (_fCommandGate->commandSleep(arg0, deadline, THREAD_INTERRUPTIBLE) == THREAD_AWAKENED)
            return kIOReturnSuccess;
        else
            return kIOReturnTimeout;
    }
}

bool AirportItlwm::initPCIPowerManagment(IOPCIDevice *provider)
{
    UInt16 reg16;

    reg16 = provider->configRead16(kIOPCIConfigCommand);

    reg16 |= ( kIOPCICommandBusMaster       |
               kIOPCICommandMemorySpace     |
               kIOPCICommandMemWrInvalidate );

    reg16 &= ~kIOPCICommandIOSpace;  // disable I/O space

    provider->configWrite16( kIOPCIConfigCommand, reg16 );
    provider->findPCICapability(kIOPCIPowerManagementCapability,
                                &pmPCICapPtr);
    if (pmPCICapPtr) {
        UInt16 pciPMCReg = provider->configRead32( pmPCICapPtr ) >> 16;
        if (pciPMCReg & kPCIPMCPMESupportFromD3Cold)
            magicPacketSupported = true;
        provider->configWrite16((pmPCICapPtr + 4), 0x8000 );
        IOSleep(10);
    }
    return true;
}

static IOPMPowerState powerStateArray[kPowerStateCount] =
{
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, kIOPMDeviceUsable, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0}
};

void AirportItlwm::unregistPM()
{
    if (powerOffThreadCall) {
        sRT.pmOffCancelRet = thread_call_cancel(powerOffThreadCall);
        thread_call_free(powerOffThreadCall);
        powerOffThreadCall = NULL;
    }
    if (powerOnThreadCall) {
        sRT.pmOnCancelRet = thread_call_cancel(powerOnThreadCall);
        thread_call_free(powerOnThreadCall);
        powerOnThreadCall = NULL;
    }
}

IOReturn AirportItlwm::setPowerState(unsigned long powerStateOrdinal, IOService *policyMaker)
{
    RT_SET(12);
    sRT.pmCount++;
    sRT.lastPmReq = (uint32_t)powerStateOrdinal;
    IOReturn result = IOPMAckImplied;
    XYLog("DEBUG %s ordinal=%lu pmPowerState=%u power_state=%u\n", __FUNCTION__, powerStateOrdinal, pmPowerState, power_state);
    if (pmPowerState == powerStateOrdinal) {
        XYLog("DEBUG %s SKIPPED (already in state %lu)\n", __FUNCTION__, powerStateOrdinal);
        return result;
    }
    switch (powerStateOrdinal) {
        case kPowerStateOff:
            if (powerOffThreadCall) {
                retain();
                if (thread_call_enter(powerOffThreadCall))
                    release();
                result = 5000000;
            }
            break;
        case kPowerStateOn:
            if (powerOnThreadCall) {
                retain();
                if (thread_call_enter(powerOnThreadCall))
                    release();
                result = 5000000;
            }
            break;
            
        default:
            break;
    }
    return result;
}

unsigned long AirportItlwm::initialPowerStateForDomainState(IOPMPowerFlags domainState)
{
    // kIOPMDeviceUsable (bit 9) = parent can supply usable power → ON
    // kIOPMPowerOn (bit 1) = parent has power → ON
    // Neither → OFF
    unsigned long ret;
    if (domainState & kIOPMDeviceUsable)
        ret = kPowerStateOn;
    else if (domainState & kIOPMPowerOn)
        ret = kPowerStateOn;
    else
        ret = kPowerStateOff;
    XYLog("DEBUG %s domainState=0x%lx (DeviceUsable=%d PowerOn=%d) -> %lu power_state=%u pmPowerState=%u\n",
          __FUNCTION__, (unsigned long)domainState,
          (int)((domainState & kIOPMDeviceUsable) != 0), (int)((domainState & kIOPMPowerOn) != 0),
          ret, power_state, pmPowerState);
    return ret;
}

IOReturn AirportItlwm::setWakeOnMagicPacket(bool active)
{
    magicPacketEnabled = active;
    return kIOReturnSuccess;
}

static void handleSetPowerStateOff(thread_call_param_t param0,
                             thread_call_param_t param1)
{
    AirportItlwm *self = (AirportItlwm *)param0;
    if (!self) return;

    if (param1 == 0)
    {
        IOCommandGate *gate = self->getCommandGate();
        if (gate) {
            gate->runAction((IOCommandGate::Action)
                            handleSetPowerStateOff,
                            (void *) 1);
        } else {
            sRT.pmOffGateNull++;
            XYLog("DEBUG handleSetPowerStateOff: gate=NULL, calling directly\n");
            self->setPowerStateOff();
            self->release();
        }
    }
    else
    {
        self->setPowerStateOff();
        self->release();
    }
}

static void handleSetPowerStateOn(thread_call_param_t param0,
                            thread_call_param_t param1)
{
    AirportItlwm *self = (AirportItlwm *)param0;
    if (!self) return;

    if (param1 == 0)
    {
        IOCommandGate *gate = self->getCommandGate();
        if (gate) {
            gate->runAction((IOCommandGate::Action)
                            handleSetPowerStateOn,
                            (void *) 1);
        } else {
            sRT.pmOnGateNull++;
            XYLog("DEBUG handleSetPowerStateOn: gate=NULL, calling directly\n");
            self->setPowerStateOn();
            self->release();
        }
    }
    else
    {
        self->setPowerStateOn();
        self->release();
    }
}

IOReturn AirportItlwm::registerWithPolicyMaker(IOService *policyMaker)
{
    IOReturn ret;

    XYLog("DEBUG %s entry policyMaker=%p\n", __FUNCTION__, policyMaker);
    pmPowerState = kPowerStateOn;
    pmPolicyMaker = policyMaker;
    sRT.pmPolicyPtr = (uint64_t)(uintptr_t)policyMaker;

    powerOffThreadCall = thread_call_allocate(
                                            (thread_call_func_t)handleSetPowerStateOff,
                                            (thread_call_param_t)this);
    powerOnThreadCall  = thread_call_allocate(
                                            (thread_call_func_t)handleSetPowerStateOn,
                                              (thread_call_param_t)this);
    ret = pmPolicyMaker->registerPowerDriver(this,
                                             powerStateArray,
                                             kPowerStateCount);
    XYLog("DEBUG %s registerPowerDriver ret=%d pmPowerState=%u\n", __FUNCTION__, ret, pmPowerState);
    if (ret == kIOReturnSuccess) {
        // Match Apple's pattern: assert device desires ON, external starts at OFF.
        // Without this, PM framework may call setPowerState(0) after registration,
        // disabling the adapter that start() just enabled.
        changePowerStateToPriv(kPowerStateOn);
        changePowerStateTo(kPowerStateOff);
        XYLog("DEBUG %s changePowerStateToPriv(ON) + changePowerStateTo(OFF) done\n", __FUNCTION__);
    }
    return ret;
}

void AirportItlwm::setPowerStateOff()
{
    XYLog("DEBUG %s power_state=%u pmPowerState=%u\n", __FUNCTION__, power_state, pmPowerState);
    pmPowerState = kPowerStateOff;
#if __IO80211_TARGET >= __MAC_26_0
    handleSystemPowerStateChange(false, NULL);
#else
    handleSystemPowerStateChange(false, bsdInterface);
#endif
    if (pmPolicyMaker) {
        sRT.pmAckOffCnt++;
        pmPolicyMaker->acknowledgeSetPowerState();
    }
}

void AirportItlwm::setPowerStateOn()
{
    XYLog("DEBUG %s power_state=%u pmPowerState=%u\n", __FUNCTION__, power_state, pmPowerState);
    pmPowerState = kPowerStateOn;
#if __IO80211_TARGET >= __MAC_26_0
    handleSystemPowerStateChange(true, NULL);
#else
    handleSystemPowerStateChange(true, bsdInterface);
#endif
    if (pmPolicyMaker) {
        sRT.pmAckOnCnt++;
        pmPolicyMaker->acknowledgeSetPowerState();
    }
}
