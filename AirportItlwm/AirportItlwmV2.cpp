//
//  AirportItlwmV2.cpp
//  AirportItlwm-Sonoma
//
//  Created by qcwap on 2023/6/27.
//  Copyright © 2023 钟先耀. All rights reserved.
//

#include "AirportItlwmV2.hpp"
#include "AirportItlwmCountryCode.hpp"
#include "TahoeBeaconIeBuilder.hpp"
#include "TahoeAssociationAuthContracts.hpp"
#include "TahoeBssBlacklistContracts.hpp"
#include "TahoeCapabilityContracts.hpp"
#include "TahoeDriverAvailabilityContracts.hpp"
#include <linux/iwx_diag_log.h>
#include "AirportItlwmRegDiag.hpp"
#include <ClientKit/AirportItlwmRegDiagBridge.h>
#include <ClientKit/AirportItlwmPostPltiTrace.h>
#include <ClientKit/AirportItlwmPostPltiTraceBridge.h>
#include "AirportItlwmAPSTAOwner.hpp"
#include "TahoeScanContracts.hpp"
#include "TahoeSkywalkIoctlRoutes.hpp"
#include <crypto/sha1.h>
#include <net80211/ieee80211_priv.h>
#include <net80211/ieee80211_var.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSString.h>
#include <libkern/c++/OSSymbol.h>
#include <libkern/c++/OSMetaClass.h>
#include <libkern/OSAtomic.h>

// CR-226: extern "C" asm-named declarations for the BootKC-exported
// IO80211NetworkPacket allocator and constructor. Direct asm-named
// declarations are required because:
//   - the freestanding kext build has no `<new>`, so C++ placement-new
//     is unavailable;
//   - `IO80211NetworkPacket::operator new` would otherwise resolve via
//     normal name lookup to the inherited `OSObject::operator new`
//     (the local IO80211NetworkPacket header doesn't redeclare it),
//     allocating from the generic OSObject zone instead of
//     IO80211NetworkPacket's own kalloc_type_view (CR-225 reviewer's
//     finding).
extern "C" void *
AirportItlwm_IO80211NetworkPacket_operatorNew(unsigned long size)
    __asm("__ZN20IO80211NetworkPacketnwEm");

extern "C" void
AirportItlwm_IO80211NetworkPacket_C1(void *self, OSMetaClass const *meta)
    __asm("__ZN20IO80211NetworkPacketC1EPK11OSMetaClass");

#if __IO80211_TARGET >= __MAC_26_0
// Tahoe's AppleBCMWLANCore OFF path calls this exported IOService helper before
// clearing its system-power bit.  It removes the named service property through
// the same IOService property transport used by the reference controller.
extern bool removePropertyHelper(IOService *service, const char *key);

extern "C" void *
AirportItlwm_IO80211BssManager_operatorNew(unsigned long size)
    __asm("__ZN17IO80211BssManagernwEm");

extern "C" void
AirportItlwm_IO80211BssManager_C1(void *self)
    __asm("__ZN17IO80211BssManagerC1Ev");

extern "C" void *
AirportItlwm_IO80211BSSBeacon_operatorNew(unsigned long size)
    __asm("__ZN16IO80211BSSBeaconnwEm");

extern "C" void
AirportItlwm_IO80211BSSBeacon_C1(void *self)
    __asm("__ZN16IO80211BSSBeaconC1Ev");

extern "C" bool
AirportItlwm_IO80211BSSBeacon_initWithChanSpec(
    void *self, CCLogStream *logger, CommonFaultReporter *faultReporter)
    __asm("__ZN16IO80211BSSBeacon16initWithChanSpecEP11CCLogStreamP19CommonFaultReporter");

extern "C" bool
AirportItlwm_IO80211BSSBeacon_setBeaconDataFromMsg(
    void *self,
    TahoeBssManagerContracts::BeaconMetaData *metadata,
    uint8_t *ie)
    __asm("__ZN16IO80211BSSBeacon20setBeaconDataFromMsgER14BeaconMetaDataPh");

#endif

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
#include "Airport/IO80211BSSBeacon.h"
#include "Airport/IO80211NetworkPacket.h"
#include "IOPCIEDeviceWrapper.hpp"
#include <IOKit/skywalk/IOSkywalkPacketBuffer.h>
#if __IO80211_TARGET >= __MAC_26_0
#include <IOKit/IOUserClient.h>
#endif

/*
 * A controller operation fence for callbacks that are entered outside the
 * command gate.  Stop first closes lifecycle admission and then waits for
 * these users before it removes Skywalk queues or detaches the HAL.  The
 * internal form is deliberately reserved for HAL/framework callbacks that
 * may be emitted while start() is still publishing the interface.
 */
class AirportItlwmControllerLifecycleOperationGuard
{
public:
    AirportItlwmControllerLifecycleOperationGuard(AirportItlwm *controller,
                                                  bool allowStarting)
        : fController(nullptr)
    {
        if (controller == nullptr)
            return;

        const bool admitted = allowStarting
            ? controller->beginLifecycleInternalOperation()
            : controller->beginLifecycleOperation();
        if (admitted)
            fController = controller;
    }

    ~AirportItlwmControllerLifecycleOperationGuard()
    {
        if (fController != nullptr)
            fController->endLifecycleOperation();
    }

    bool admitted() const { return fController != nullptr; }

private:
    AirportItlwm *fController;
};

class AirportItlwmSkywalkMulticastQueue : public IOEventSource
{
    OSDeclareDefaultStructors(AirportItlwmSkywalkMulticastQueue)

public:
    static AirportItlwmSkywalkMulticastQueue *withInterface(IO80211SkywalkInterface *interface)
    {
        AirportItlwmSkywalkMulticastQueue *queue = new AirportItlwmSkywalkMulticastQueue;
        if (queue != nullptr && !queue->initWithInterface(interface))
            OSSafeReleaseNULL(queue);
        return queue;
    }

    virtual bool initWithInterface(IO80211SkywalkInterface *interface)
    {
        if (interface == nullptr)
            return false;

        fInterface = nullptr;
        if (!IOEventSource::init(interface, nullptr))
            return false;

        fInterface = interface;
        fInterface->retain();
        return true;
    }

    virtual void free() override
    {
        OSSafeReleaseNULL(fInterface);
        IOEventSource::free();
    }

    virtual IO80211SkywalkInterface *getInterface()
    {
        return fInterface;
    }

    virtual void requestDequeue()
    {
    }

    virtual void collectQueueStats(void *)
    {
    }

    virtual void *getInterfaceContext()
    {
        return nullptr;
    }

private:
    IO80211SkywalkInterface *fInterface;
};

// CR-225 SYSTEM_CONTRACT_FIX. Direct IO80211NetworkPacket allocation —
// no subclass.
//
// CR-222 Stage 2 evidence proved the alloc gate. CR-224 attempted to
// bypass it via a same-kext IO80211NetworkPacket subclass, which kmutil
// rejected at AuxKC build time with `Malformed vtable. Super class
// '__ZTV20IO80211NetworkPacket' has 72 entries vs subclass
// '__ZTV25AirportItlwmIO80211Packet' with 69 entries`: our local
// IO80211NetworkPacket / IOSkywalkNetworkPacket / IOSkywalkPacket
// headers are incomplete relative to Tahoe's actual class definitions,
// so the subclass vtable cannot be aligned 1:1 from this kext.
//
// CR-225 takes a different reference path: instead of subclassing,
// construct an IO80211NetworkPacket *directly* using the framework's
// own exported allocation primitives:
//   - `IO80211NetworkPacket::operator new(size_t)` at BootKC
//     0xffffff80022c591c is a 0x10-byte thunk:
//        mov rsi, rdi                ; size
//        lea rdi, [rip + 0x13f116]   ; IO80211NetworkPacket kalloc_type_view
//        jmp kalloc_type_impl        ; 0xffffff8000a4ca30
//     This goes through IO80211NetworkPacket's OWN kalloc_type_view —
//     not the gated MetaClass::alloc() path (which at 0xffffff80022c5914
//     is literally `xor eax, eax; ret`, hardcoded to NULL by Apple).
//   - `IO80211NetworkPacket::IO80211NetworkPacket(OSMetaClass const *)`
//     constructor (C1) at 0xffffff80022c5840 chains
//     IOSkywalkNetworkPacket / IOSkywalkPacket / IOCommand / OSObject
//     constructors and installs the IO80211NetworkPacket vtable
//     (`lea rax, [rip+0x11d0f3]; mov [rbx], rax`).
//   - `IO80211NetworkPacket::gMetaClass` at 0xffffff80023f86e0
//     (S, exported).
//
// After construction, the produced object IS-A real IO80211NetworkPacket
// with the genuine IO80211NetworkPacket vtable. We then call the
// inherited `vt[35]` init virtual to bind it to the pool/descriptor
// (mirroring `IOSkywalkNetworkPacket::withPool` BootKC 0xffffff800297effa)
// and `vt[5]` failure destruct on init failure.
//
// No same-kext subclass is introduced. No vtable construction in our
// kext. No header alignment surgery. The approach is byte-for-byte the
// same allocate-then-init shape Apple's framework factory uses,
// substituting only the IO80211NetworkPacket-specific operator-new
// thunk for the IOSkywalkNetworkPacket-specific kalloc_type_view.
static IO80211NetworkPacket *AirportItlwm_newIO80211NetworkPacket(
    IOSkywalkPacketBufferPool *pool,
    IOSkywalkPacketDescriptor *desc,
    UInt32 options);

class AirportItlwmIO80211PacketPool : public IOSkywalkPacketBufferPool
{
    OSDeclareDefaultStructors(AirportItlwmIO80211PacketPool)

public:
    // CR-216 helper: render a 64-bit pointer-derived value as two 32-bit
    // halves. os_log on Tahoe redacts pointer-derived %llx as <private>
    // even after (uintptr_t) cast (CR-215 evidence 12:32 boot), but it
    // leaves %x of uint32_t public. Reading the raw memory via uint32_t
    // pointers strips the pointer lineage.
    static inline uint32_t ptrHi32(const void *p) {
        unsigned long long v = (unsigned long long)(uintptr_t)p;
        return (uint32_t)(v >> 32);
    }
    static inline uint32_t ptrLo32(const void *p) {
        unsigned long long v = (unsigned long long)(uintptr_t)p;
        return (uint32_t)(v & 0xffffffffu);
    }

    // CR-216 helper: read a 64-bit memory cell at (base+off) as two
    // uint32_t halves. The intermediate is uint32_t-typed so os_log
    // does not see pointer lineage.
    static inline uint32_t slotHi32(const void *base, size_t off) {
        return *reinterpret_cast<const uint32_t *>(
            reinterpret_cast<const uint8_t *>(base) + off + 4);
    }
    static inline uint32_t slotLo32(const void *base, size_t off) {
        return *reinterpret_cast<const uint32_t *>(
            reinterpret_cast<const uint8_t *>(base) + off);
    }

    static AirportItlwmIO80211PacketPool *withName(
        const char *name,
        OSObject *owner,
        const IOSkywalkPacketBufferPool::PoolOptions *options)
    {
        // CR-216 end-to-end branch coverage for the
        // "IOSkywalkPacketBufferPool::initWithName returns false" hypothesis,
        // building on CR-215. CR-215 evidence (2026-04-29 12:32 boot,
        // commit-approval/runtime_evidence/CR-215-stage2-boot-log-20260429.txt)
        // pinned the failure to the framework's POST_OSARRAY packet
        // inventory loop, which calls our subclass `newPacket` override
        // (vt slot 50). CR-216 adds branch-to-final-point coverage inside
        // newPacket itself (NEWPACKET_<branch>) and bypasses os_log's
        // privacy redaction by reading every pointer-derived 64-bit
        // value as a pair of uint32_t halves rendered as `0x%x_%x`.
        const UInt32 packetType = kIOSkywalkPacketTypeNetwork;

        AirportItlwmIO80211PacketPool *pool =
            new AirportItlwmIO80211PacketPool;
        if (pool == nullptr) {
            XYLog("itlwm: PACKETPOOL[%s] FAIL: new returned NULL\n",
                  name ? name : "(null)");
            XYLog("itlwm: PACKETPOOL[%s] FINAL branch=NEW_NULL return=0x0_0\n",
                  name ? name : "(null)");
            return nullptr;
        }

        bool ok = pool->initWithName(name, owner, packetType, options);
        if (ok)
            return pool;

        // Failure classification reads the internal-state slots that
        // initWithName writes in chronological order of the KDK disasm.
        // Each 64-bit pointer slot is split into hi/lo uint32_t halves.
        uint8_t *poolBytes = reinterpret_cast<uint8_t *>(pool);
        uint32_t s_thC_hi   = slotHi32(poolBytes, 0xb0);  // thread_call (0x9c7f)
        uint32_t s_thC_lo   = slotLo32(poolBytes, 0xb0);
        uint32_t s_seg_hi   = slotHi32(poolBytes, 0x78);  // SegmentStats (0x9c9b)
        uint32_t s_seg_lo   = slotLo32(poolBytes, 0x78);
        uint32_t s_lk1_hi   = slotHi32(poolBytes, 0x80);  // lock1 (0x9cad)
        uint32_t s_lk1_lo   = slotLo32(poolBytes, 0x80);
        uint32_t s_lk2_hi   = slotHi32(poolBytes, 0x88);  // lock2 (0x9cc2)
        uint32_t s_lk2_lo   = slotLo32(poolBytes, 0x88);
        uint32_t s_own_hi   = slotHi32(poolBytes, 0x20);  // mProvider (0x9cee)
        uint32_t s_own_lo   = slotLo32(poolBytes, 0x20);
        uint32_t s_pbp_hi   = slotHi32(poolBytes, 0x18);  // kern_pbufpool (post 0x9e7b)
        uint32_t s_pbp_lo   = slotLo32(poolBytes, 0x18);
        uint32_t s_a1_hi    = slotHi32(poolBytes, 0x68);  // OSArray (0x9eff)
        uint32_t s_a1_lo    = slotLo32(poolBytes, 0x68);
        uint32_t s_a2_hi    = slotHi32(poolBytes, 0x60);  // OSArray (0x9f24)
        uint32_t s_a2_lo    = slotLo32(poolBytes, 0x60);
        uint32_t s_typeCache  =
            *reinterpret_cast<uint32_t *>(poolBytes + 0x3c);  // packetType cache (0x9cea)
        uint8_t  s_disposed   =
            *reinterpret_cast<uint8_t *>(poolBytes + 0xba);   // mDisposed (0x9ef3)

        // Slot non-zero predicates: a 64-bit value is non-zero iff either
        // half is non-zero.
        bool nz_thC   = (s_thC_hi   | s_thC_lo)   != 0;
        bool nz_seg   = (s_seg_hi   | s_seg_lo)   != 0;
        bool nz_lk1   = (s_lk1_hi   | s_lk1_lo)   != 0;
        bool nz_lk2   = (s_lk2_hi   | s_lk2_lo)   != 0;
        bool nz_own   = (s_own_hi   | s_own_lo)   != 0;
        bool nz_pbp   = (s_pbp_hi   | s_pbp_lo)   != 0;
        bool nz_a1    = (s_a1_hi    | s_a1_lo)    != 0;
        bool nz_a2    = (s_a2_hi    | s_a2_lo)    != 0;

        // Classify the framework-internal stage where initWithName gave
        // up. Stage labels follow the chronological order of the writes
        // inside initWithName (KDK 0x9c5f..0x9f24); the first 0-valued
        // slot pinpoints the last completed stage.
        const char *failStage;
        if (!nz_thC) {
            failStage = "PRE_THCALL";        // thread_call_allocate failed (KDK 0x9c89)
        } else if (!nz_seg) {
            failStage = "PRE_SEGSTATS";      // IOMallocTypeImpl failed (KDK 0x9ca2)
        } else if (!nz_lk1) {
            failStage = "PRE_LOCK1";         // first IORecursiveLockAlloc failed (KDK 0x9cb7)
        } else if (!nz_lk2) {
            failStage = "PRE_LOCK2";         // second IORecursiveLockAlloc failed (KDK 0x9ccc)
        } else if (!nz_own || s_typeCache == 0) {
            failStage = "PRE_OWNER_CACHE";   // never reached the post-IOBSD writes (KDK 0x9cea-0x9cf6)
        } else if (!nz_pbp) {
            failStage = "KPBP_REJECT";       // kern_pbufpool_create rejected (KDK 0x9e84/0x9eb5)
        } else if (!nz_a1) {
            failStage = "OSARRAY_FIRST";     // first OSArray::withCapacity failed (KDK 0x9f06)
        } else if (!nz_a2) {
            failStage = "OSARRAY_SECOND";    // second OSArray::withCapacity failed (KDK 0x9f27)
        } else {
            failStage = "POST_OSARRAY";      // post-OSArray (packet inventory loop or later)
        }

        XYLog("itlwm: PACKETPOOL[%s] FINAL branch=INIT_FALSE_%s preRelease "
              "pool=0x%x_%x pbufpool=0x%x_%x owner=0x%x_%x "
              "arr1=0x%x_%x arr2=0x%x_%x disposed=%u\n",
              name ? name : "(null)", failStage,
              ptrHi32(pool), ptrLo32(pool),
              s_pbp_hi, s_pbp_lo,
              s_own_hi, s_own_lo,
              s_a1_hi,  s_a1_lo,
              s_a2_hi,  s_a2_lo,
              (unsigned)s_disposed);
        OSSafeReleaseNULL(pool);
        XYLog("itlwm: PACKETPOOL[%s] FAIL: initWithName returned false; "
              "pool released to NULL\n", name ? name : "(null)");
        XYLog("itlwm: PACKETPOOL[%s] FINAL branch=INIT_FALSE_%s "
              "return=0x%x_%x\n", name ? name : "(null)", failStage,
              ptrHi32(pool), ptrLo32(pool));
        return pool;
    }

    // CR-226 newPacket override. Mirrors the AppleBCMWLAN intermediate
    // dispatch pattern (`newPacket` -> `newPacketWithDescriptor`).
    virtual IOReturn newPacket(IOSkywalkPacketDescriptor *desc,
                               IOSkywalkPacket **outPacket) override
    {
        if (desc == nullptr || outPacket == nullptr)
            return kIOReturnBadArgument;
        IOSkywalkPacket *p = newPacketWithDescriptor(desc);
        if (p == nullptr) {
            XYLog("itlwm: NEWPACKET FINAL branch=ALLOC_NULL "
                  "this=0x%x_%x desc=0x%x_%x ret=0xe00002bd\n",
                  ptrHi32(this), ptrLo32(this),
                  ptrHi32(desc), ptrLo32(desc));
            return kIOReturnNoMemory;
        }
        *outPacket = p;
        return kIOReturnSuccess;
    }

    virtual IOSkywalkPacket *newPacketWithDescriptor(
        IOSkywalkPacketDescriptor *desc);
};

#if __IO80211_TARGET >= __MAC_26_0
// CR-239 Phase 1 — custom IOUserClient subclass exposing a private
// driver-side surface for our companion userspace daemon
// (AirportItlwmAgent). T1 (analysis/cr239_t1_tier_a_decisive_2026_05_01.txt)
// proved Apple80211BindToInterface fails with -3903 for non-airportd
// processes due to the Apple-private entitlement
// `com.apple.private.driverkit.driver-access =
// com.apple.private.wifi.driverkit`. A custom IOUserClient on our own
// controller bypasses that gate while keeping us within stable IOKit
// user-client APIs.
//
// Phase 1 SCOPE: this commit adds the user-client class, the
// `newUserClient` dispatch on `AirportItlwm`, and a stub
// `deliverExternalPMK` handler that ONLY logs the call. No
// connect-flow state is touched (ic->ic_psk is unchanged, no
// USE_APPLE_SUPPLICANT toggle, no setCIPHER_KEY rerouting). Phase 2
// will land the credential-wiring change behind its own review.

// Custom user-client connection type. Userspace passes this value as
// the `type` argument to IOServiceOpen() to request our private
// channel; any other value falls through to the parent
// IO80211Controller::newUserClient (which historically returned
// kIOReturnUnsupported for our class but now we delegate explicitly
// for forward compatibility).
//
// Magic value chosen as ASCII "ITLP" reversed (P-L-T-I) for grep-
// ability; NOT a security boundary. The access-control boundary is
// `IOUserClient::clientHasPrivilege(kIOClientPrivilegeAdministrator)`
// in `AirportItlwm::newUserClient` below.
#define kAirportItlwmUserClientType  ('PLTI')   // 0x504C5449

/*
 * The relay cookie is generated in the kext and never accepted as a caller
 * input.  `arc4random_buf` is the project-owned kernel CSPRNG wrapper used by
 * net80211 nonce/key material; retrying an all-zero result preserves zero as
 * the explicit invalid/unbound sentinel in the V1 relay ABI.
 */
static bool
airportItlwmSaeFillNonZero(uint8_t value[
                              kAirportItlwmSaeRelayV1NonceLength])
{
    if (value == nullptr)
        return false;
    for (unsigned int attempt = 0; attempt != 4; ++attempt) {
        arc4random_buf(value, kAirportItlwmSaeRelayV1NonceLength);
        if (!AirportItlwmSaeRelayFsmV1BytesAllZero(value,
                kAirportItlwmSaeRelayV1NonceLength))
            return true;
    }
    memset(value, 0, kAirportItlwmSaeRelayV1NonceLength);
    return false;
}

enum {
    // [0] DeliverPMK
    //   in: scalar[0] = generation echo (uint64_t) — must match the
    //                   currently pending fAssocTarget.generation
    //                   published by the PSK association-start edge,
    //                   otherwise delivery is rejected with
    //                   kIOReturnNotPermitted and ic_psk is left
    //                   untouched (replay guard);
    //       struct     = struct apple80211_key with
    //                   key_cipher_type=APPLE80211_CIPHER_PMK and
    //                   key_len=IEEE80211_PMK_LEN (32).
    //   out: none.
    // [1] WaitAssociationTarget
    //   in: scalar[0] = last_acked generation (uint64_t). The dispatch
    //                   blocks under the controller command gate until
    //                   fAssocTarget.generation > last_acked, then
    //                   returns the current AirportItlwmAssociationTarget
    //                   snapshot.
    //   out: struct    = AirportItlwmAssociationTarget snapshot.
    kAirportItlwmUserClientMethod_DeliverPMK = 0,
    kAirportItlwmUserClientMethod_WaitAssociationTarget = 1,
    // SAE relay selectors are append-only V1 ABI values.  They remain a
    // transport/lifecycle surface only until Algorithm-3 TX completion and
    // the Agent's SAE cryptographic owner arrive in later layers.
    kAirportItlwmUserClientMethod_WaitSaeTarget =
        kAirportItlwmSaeRelayWaitTargetSelector,
    kAirportItlwmUserClientMethod_SubmitSaeReply =
        kAirportItlwmSaeRelaySubmitReplySelector,
    kAirportItlwmUserClientMethod_WaitSaeAuthEvent =
        kAirportItlwmSaeRelayWaitAuthEventSelector,
    kAirportItlwmUserClientMethod_CompleteSae =
        kAirportItlwmSaeRelayCompleteSelector,
    kAirportItlwmUserClientMethod_AbortSae =
        kAirportItlwmSaeRelayAbortSelector,
    kAirportItlwmUserClientMethod_NumMethods =
        kAirportItlwmSaeRelaySelectorCount
};

class AirportItlwmUserClient : public IOUserClient
{
    OSDeclareDefaultStructors(AirportItlwmUserClient)

public:
    virtual bool initWithTask(task_t owningTask,
                              void *securityID,
                              UInt32 type,
                              OSDictionary *properties) APPLE_KEXT_OVERRIDE;
    virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
    virtual void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
    virtual void free(void) APPLE_KEXT_OVERRIDE;
    virtual IOReturn clientClose(void) APPLE_KEXT_OVERRIDE;
    virtual IOReturn externalMethod(uint32_t selector,
                                    IOExternalMethodArguments *args,
                                    IOExternalMethodDispatch *dispatch,
                                    OSObject *target,
                                    void *reference) APPLE_KEXT_OVERRIDE;

    // Static dispatch entries — referenced by sAirportItlwmUserClientMethods
    // table immediately below. Must be public so the file-scope table
    // initializer can take their addresses.
    static IOReturn sExtDeliverPMK(AirportItlwmUserClient *target,
                                   void *reference,
                                   IOExternalMethodArguments *args);
    static IOReturn sExtWaitAssociationTarget(AirportItlwmUserClient *target,
                                              void *reference,
                                              IOExternalMethodArguments *args);
    static IOReturn sExtWaitSaeTarget(AirportItlwmUserClient *target,
                                      void *reference,
                                      IOExternalMethodArguments *args);
    static IOReturn sExtSubmitSaeReply(AirportItlwmUserClient *target,
                                       void *reference,
                                       IOExternalMethodArguments *args);
    static IOReturn sExtWaitSaeAuthEvent(AirportItlwmUserClient *target,
                                          void *reference,
                                          IOExternalMethodArguments *args);
    static IOReturn sExtCompleteSae(AirportItlwmUserClient *target,
                                    void *reference,
                                    IOExternalMethodArguments *args);
    static IOReturn sExtAbortSae(AirportItlwmUserClient *target,
                                 void *reference,
                                 IOExternalMethodArguments *args);

    AirportItlwm *retainProvider();

private:
    AirportItlwm *takeProvider();
    bool copySaeClientCookie(uint8_t out[
                             kAirportItlwmSaeRelayV1NonceLength]);
    void clearSaeClientCookie();
    AirportItlwm *fProvider;
    IOLock       *fProviderLock;
    task_t       fOwningTask;
    uint8_t      fSaeClientCookie[kAirportItlwmSaeRelayV1NonceLength];
};

// Pin the carrier ABI for the helper-side mirror in
// AirportItlwmAgent/src/userclient.c, which inlines the
// apple80211_key layout without pulling MacKernelSDK into userspace.
// If Apple ever changes the SDK struct size, both sides must update;
// this assert catches drift on the kext build.
static_assert(sizeof(struct apple80211_key) == 148,
              "apple80211_key carrier size must match the helper-side "
              "mirror in AirportItlwmAgent/src/userclient.c");

static const IOExternalMethodDispatch
sAirportItlwmUserClientMethods[kAirportItlwmUserClientMethod_NumMethods] = {
    // [0] kAirportItlwmUserClientMethod_DeliverPMK
    {
        (IOExternalMethodAction)&AirportItlwmUserClient::sExtDeliverPMK,
        1,                                        // checkScalarInputCount (generation echo)
        sizeof(struct apple80211_key),            // checkStructureInputSize
        0,                                        // checkScalarOutputCount
        0                                         // checkStructureOutputSize
    },
    // [1] kAirportItlwmUserClientMethod_WaitAssociationTarget
    {
        (IOExternalMethodAction)&AirportItlwmUserClient::sExtWaitAssociationTarget,
        1,                                        // checkScalarInputCount (last_acked generation)
        0,                                        // checkStructureInputSize
        0,                                        // checkScalarOutputCount
        sizeof(struct AirportItlwmAssociationTarget) // checkStructureOutputSize
    },
    // [2] kAirportItlwmSaeRelayWaitTargetSelector
    {
        (IOExternalMethodAction)&AirportItlwmUserClient::sExtWaitSaeTarget,
        0,                                        // checkScalarInputCount
        0,                                        // checkStructureInputSize
        0,                                        // checkScalarOutputCount
        sizeof(struct AirportItlwmSaeTargetV1)     // checkStructureOutputSize
    },
    // [3] kAirportItlwmSaeRelaySubmitReplySelector
    {
        (IOExternalMethodAction)&AirportItlwmUserClient::sExtSubmitSaeReply,
        0,                                        // checkScalarInputCount
        sizeof(struct AirportItlwmSaeAuthReplyV1), // checkStructureInputSize
        0,                                        // checkScalarOutputCount
        0                                         // checkStructureOutputSize
    },
    // [4] kAirportItlwmSaeRelayWaitAuthEventSelector
    {
        (IOExternalMethodAction)&AirportItlwmUserClient::sExtWaitSaeAuthEvent,
        1,                                        // checkScalarInputCount (last event sequence)
        0,                                        // checkStructureInputSize
        0,                                        // checkScalarOutputCount
        sizeof(struct AirportItlwmSaeAuthEventV1)  // checkStructureOutputSize
    },
    // [5] kAirportItlwmSaeRelayCompleteSelector
    {
        (IOExternalMethodAction)&AirportItlwmUserClient::sExtCompleteSae,
        0,                                        // checkScalarInputCount
        sizeof(struct AirportItlwmSaeCompletionV1), // checkStructureInputSize
        0,                                        // checkScalarOutputCount
        0                                         // checkStructureOutputSize
    },
    // [6] kAirportItlwmSaeRelayAbortSelector
    {
        (IOExternalMethodAction)&AirportItlwmUserClient::sExtAbortSae,
        0,                                        // checkScalarInputCount
        sizeof(struct AirportItlwmSaeAbortV1),     // checkStructureInputSize
        0,                                        // checkScalarOutputCount
        0                                         // checkStructureOutputSize
    }
};
#endif // __IO80211_TARGET >= __MAC_26_0

#define super IO80211Controller
OSDefineMetaClassAndStructors(AirportItlwm, IO80211Controller);
OSDefineMetaClassAndStructors(AirportItlwmBootNub, IOService)
OSDefineMetaClassAndStructors(CTimeout, OSObject)
OSDefineMetaClassAndStructors(AirportItlwmSkywalkMulticastQueue, IOEventSource)
OSDefineMetaClassAndStructors(AirportItlwmIO80211PacketPool,
                              IOSkywalkPacketBufferPool)
#if __IO80211_TARGET >= __MAC_26_0
OSDefineMetaClassAndStructors(AirportItlwmUserClient, IOUserClient)
#endif

// CR-225: direct IO80211NetworkPacket allocation. Mirrors
// `IOSkywalkNetworkPacket::withPool` body (BootKC 0xffffff800297effa
// capstone disassembly):
//   1. operator new -> kalloc_type_impl(class-specific kalloc_type_view, 0x78)
//   2. C1 constructor (chains parents and installs class vtable)
//   3. virtual init at vt[35] (binds packet to pool/descriptor)
//   4. on failure, virtual destruct at vt[5]
//
// CR-225 substitutes IO80211NetworkPacket's own operator new
// (0xffffff80022c591c) and constructor (0xffffff80022c5840) for the
// IOSkywalkNetworkPacket variants used by the framework factory.
// IO80211NetworkPacket::MetaClass::alloc() (0xffffff80022c5914) is
// hardcoded by Apple to return NULL — we do not call it. Operator
// new bypasses MetaClass::alloc() and uses the IO80211NetworkPacket
// kalloc_type_view directly.
//
// Result: a real IO80211NetworkPacket instance with the IO80211Family
// vtable installed — no subclass, no vtable construction in our kext.
static IO80211NetworkPacket *
AirportItlwm_newIO80211NetworkPacket(IOSkywalkPacketBufferPool *pool,
                                      IOSkywalkPacketDescriptor *desc,
                                      UInt32 options)
{
    // Direct asm-named call to BootKC `__ZN20IO80211NetworkPacketnwEm` at
    // 0xffffff80022c591c (operator new thunk -> kalloc_type_impl with
    // IO80211NetworkPacket's own kalloc_type_view). C++ name lookup of
    // `IO80211NetworkPacket::operator new` would resolve to the inherited
    // `OSObject::operator new` symbol (`__ZN8OSObjectnwEm`) because our
    // local IO80211NetworkPacket header does not redeclare operator new.
    // Using the asm-named extern "C" declaration forces the linker to
    // emit the IO80211NetworkPacket-specific symbol reference.
    void *mem = AirportItlwm_IO80211NetworkPacket_operatorNew(0x78);
    if (mem == nullptr) return nullptr;

    // Direct call to BootKC-exported IO80211NetworkPacket C1 constructor
    // (asm name `__ZN20IO80211NetworkPacketC1EPK11OSMetaClass` at
    // 0xffffff80022c5860). The constructor chains
    // IOSkywalkNetworkPacket / IOSkywalkPacket / IOCommand / OSObject
    // ctors and installs the IO80211NetworkPacket vtable
    // (`lea rax, [rip+0x11d0f3]; mov [rbx], rax`). We avoid C++
    // placement-new because the freestanding kext build has no `<new>`.
    AirportItlwm_IO80211NetworkPacket_C1(mem, IO80211NetworkPacket::metaClass);
    IO80211NetworkPacket *p = static_cast<IO80211NetworkPacket *>(mem);

    // vt[35] init virtual (initWithPool) and vt[5] failure destruct
    // — same slots IOSkywalkNetworkPacket::withPool dispatches to.
    void **vtbl = *reinterpret_cast<void ***>(p);
    typedef bool (*InitFn)(IOSkywalkPacket *,
                           IOSkywalkPacketBufferPool *,
                           IOSkywalkPacketDescriptor *,
                           UInt32);
    auto init_fn = reinterpret_cast<InitFn>(vtbl[35]);
    if (!init_fn(p, pool, desc, options)) {
        typedef void (*DestructFn)(IOSkywalkPacket *);
        reinterpret_cast<DestructFn>(vtbl[5])(p);
        return nullptr;
    }
    return p;
}

// CR-226 newPacketWithDescriptor: delegate to direct IO80211NetworkPacket
// allocator (operator new + C1 ctor + vt[35] init), defined above.
IOSkywalkPacket *
AirportItlwmIO80211PacketPool::newPacketWithDescriptor(
    IOSkywalkPacketDescriptor *desc)
{
    return AirportItlwm_newIO80211NetworkPacket(this, desc, 0);
}

#include "Airport/CCDataStream.h"
#include "Airport/CCFaultReporter.h"


static constexpr UInt32 kAirportItlwmSkywalkQueueCapacity = 256;

IO80211WorkQueue *_fWorkloop;
IOCommandGate *_fCommandGate;

#if __IO80211_TARGET >= __MAC_26_0
static bool setupSaeTransportMailboxSource(AirportItlwm *that,
                                           IOWorkLoop *workloop);
static void teardownSaeTransportMailboxSource(AirportItlwm *that,
                                              IOWorkLoop *workloop);
static void queueSaeTransportMailbox(
    AirportItlwm *that, const struct ItlSaeAuthTransportEventV1 *event,
    bool isReset);
static void dispatchSaeTransportMailboxEvent(
    AirportItlwm *that, const struct ItlSaeAuthTransportEventV1 *event,
    bool isReset);
#endif

// Off-gate link-state publication layer.
//
// The inherited IO80211InfraInterface::setLinkState publication reaches
// IO80211Glue::sendIOUCToWcl, which requires the IO80211 work-queue serial
// owner to be on its own thread with the work-loop gate released. Invoking it
// from inside getCommandGate()->runAction holds the recursive work-loop gate,
// so the publication would observe inGate()==true and take the null-owner
// panic branch. The publication is therefore deferred to a software
// IOInterruptEventSource serviced by _fWorkloop (the same IO80211WorkQueue
// serial owner) outside the command gate. The pending transition is a single
// coalesced record: the latest accepted link state wins and the action
// publishes exactly once per coalesced transition (no retry/replay).
//
// The source state is per controller (fLinkStatePublishLifecycle), rather
// than a process-global sidecar.  Setup claims admission before creating an
// event source; teardown closes it and keeps tearingDown set through
// removeEventSource().  That makes overlapping controller starts/stops and
// simultaneous controllers independent.

#if __IO80211_TARGET >= __MAC_26_0
static bool
seedTahoeInitialMacAddress(IO80211SkywalkInterface *netIf,
                           struct ieee80211com *ic)
{
    if (netIf == nullptr || ic == nullptr)
        return false;

    const uint8_t *mac = ic->ic_myaddr;
    if ((mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) == 0)
        return false;

    ether_addr initMac;
    memcpy(initMac.octet, mac, IEEE80211_ADDR_LEN);
    netIf->setInitMacAddress(initMac);
    netIf->setProperty(kIOMACAddress, initMac.octet, kIOEthernetAddressSize);
    return true;
}

static void
publishTahoeSkywalkLinkCarrier(IOSkywalkNetworkInterface *netIf, bool active)
{
    if (netIf == nullptr)
        return;

    /*
     * Tahoe's IO80211/WCL link-state publication must stay on the guarded
     * off-gate path below, but the Skywalk carrier is the reference lower-half
     * provider state consumed by IOSkywalkLegacyEthernet for the BSD child.
     */
    (void)netIf->reportLinkStatus(active ? 3U : 1U, 0x80U);
}
#endif

static void publishLinkStateInterruptAction(OSObject *owner,
                                            IOInterruptEventSource *sender,
                                            int count)
{
    AirportItlwm *that = OSDynamicCast(AirportItlwm, owner);
    if (that == NULL)
        return;

    AirportItlwmLinkStatePublishLifecycle &state =
        that->fLinkStatePublishLifecycle;
    IOSimpleLock *admissionLock = state.admissionLock;
    if (that == NULL || admissionLock == NULL)
        return;

    // removeEventSource() drains an already-running action, but it may begin
    // while teardown closes admission. Hold admission while taking the
    // payload snapshot so teardown cannot free the payload lock underneath
    // this action.
    IOInterruptState admissionIrq =
        IOSimpleLockLockDisableInterrupt(admissionLock);
    if (state.settingUp || state.stopping || state.tearingDown ||
        sender != state.source || state.payloadLock == NULL) {
        IOSimpleLockUnlockEnableInterrupt(admissionLock, admissionIrq);
#if __IO80211_TARGET >= __MAC_26_0
        that->recordTahoeLinkContext(
            kAirportItlwmRegDiagLinkContextPublishAction,
            kAirportItlwmRegDiagLinkContextActionUnavailable, 0, 0,
            AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_STATUS_UNAVAILABLE,
            kAirportItlwmRegDiagLinkContextLifecyclePublicationUnavailable,
            kIOReturnNotReady,
            AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_EPOCH_CURRENT, -1);
#endif
        airportItlwmRegDiagRecordLinkPublish(
            kAirportItlwmRegDiagLinkPublishActionUnavailable, 0, 0,
            kIOReturnNotReady);
        return;
    }

    IO80211LinkState linkState;
    unsigned int rawCode;
    bool valid;
    IOSimpleLock *payloadLock = state.payloadLock;
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(payloadLock);
    valid = state.pendingValid;
    linkState = state.pendingState;
    rawCode = state.pendingRawCode;
    state.pendingValid = false;
    IOSimpleLockUnlockEnableInterrupt(payloadLock, irq);
    IOSimpleLockUnlockEnableInterrupt(admissionLock, admissionIrq);
    if (!valid)
        return;
#if __IO80211_TARGET >= __MAC_26_0
    that->recordTahoeLinkContext(
        kAirportItlwmRegDiagLinkContextPublishAction,
        kAirportItlwmRegDiagLinkContextActionReady,
        static_cast<uint32_t>(linkState), rawCode,
        AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_STATUS_UNAVAILABLE,
        kAirportItlwmRegDiagLinkContextLifecyclePublicationReady,
        kIOReturnSuccess, AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_EPOCH_CURRENT,
        -1);
#endif
    AirportItlwm::setLinkStateGated(that, (void *)(uintptr_t)linkState,
                                    (void *)(uintptr_t)rawCode, NULL, NULL);
}

static bool setupLinkStatePublishSource(AirportItlwm *that,
                                        IOWorkLoop *workloop)
{
    if (that == NULL || workloop == NULL)
        return false;

    AirportItlwmLinkStatePublishLifecycle &state =
        that->fLinkStatePublishLifecycle;
    IOSimpleLock *lifecycleLock = that->fLifecycleAdmissionLock;
    IOSimpleLock *admissionLock = state.admissionLock;
    if (lifecycleLock == NULL || admissionLock == NULL)
        return false;

    /*
     * Claim setup before allocation/addEventSource.  stop first changes the
     * controller phase to Draining, then observes settingUp and waits; the
     * retained workloop keeps this local setup/rollback path valid while it
     * does so.  Lock order is lifecycle admission -> source admission only.
     */
    IOInterruptState lifecycleIrq =
        IOSimpleLockLockDisableInterrupt(lifecycleLock);
    if (that->fLifecyclePhase != kAirportItlwmLifecycleStarting &&
        that->fLifecyclePhase != kAirportItlwmLifecycleLive) {
        IOSimpleLockUnlockEnableInterrupt(lifecycleLock, lifecycleIrq);
        return false;
    }
    IOInterruptState admissionIrq =
        IOSimpleLockLockDisableInterrupt(admissionLock);
    if (state.settingUp || state.stopping || state.tearingDown ||
        state.source != NULL || state.payloadLock != NULL) {
        IOSimpleLockUnlockEnableInterrupt(admissionLock, admissionIrq);
        IOSimpleLockUnlockEnableInterrupt(lifecycleLock, lifecycleIrq);
        return false;
    }
    state.settingUp = true;
    state.stopping = false;
    state.tearingDown = false;
    workloop->retain();
    IOSimpleLockUnlockEnableInterrupt(admissionLock, admissionIrq);
    IOSimpleLockUnlockEnableInterrupt(lifecycleLock, lifecycleIrq);

    IOSimpleLock *payloadLock = IOSimpleLockAlloc();
    IOInterruptEventSource *source =
        IOInterruptEventSource::interruptEventSource(
            that, (IOInterruptEventSource::Action)publishLinkStateInterruptAction);
    bool sourceAdded = false;
    bool installed = false;
    if (payloadLock != NULL && source != NULL &&
        workloop->addEventSource(source) == kIOReturnSuccess) {
        sourceAdded = true;
        source->enable();

        admissionIrq = IOSimpleLockLockDisableInterrupt(admissionLock);
        if (!state.stopping && !state.tearingDown) {
            state.source = source;
            state.payloadLock = payloadLock;
            state.users = 0;
            state.pendingValid = false;
            state.settingUp = false;
            installed = true;
        }
        IOSimpleLockUnlockEnableInterrupt(admissionLock, admissionIrq);
    }

    if (installed) {
        workloop->release();
        return true;
    }

    // Do not expose setup completion to a waiting stop until every local
    // source/payload allocation has been removed and released.
    if (source != NULL) {
        source->disable();
        if (sourceAdded)
            workloop->removeEventSource(source);
        source->release();
    }
    if (payloadLock != NULL)
        IOSimpleLockFree(payloadLock);
    admissionIrq = IOSimpleLockLockDisableInterrupt(admissionLock);
    state.settingUp = false;
    IOSimpleLockUnlockEnableInterrupt(admissionLock, admissionIrq);
    workloop->release();
    return false;
}

static void queueOffGateLinkStatePublish(AirportItlwm *that,
                                         IO80211LinkState linkState,
                                         unsigned int rawCode)
{
    if (that == NULL)
        return;

    AirportItlwmLinkStatePublishLifecycle &state =
        that->fLinkStatePublishLifecycle;
    IOSimpleLock *admissionLock = state.admissionLock;
    if (that == NULL || admissionLock == NULL)
        return;

    IOInterruptState admissionIrq =
        IOSimpleLockLockDisableInterrupt(admissionLock);
    if (state.settingUp || state.stopping || state.tearingDown ||
        state.source == NULL || state.payloadLock == NULL) {
        IOSimpleLockUnlockEnableInterrupt(admissionLock, admissionIrq);
#if __IO80211_TARGET >= __MAC_26_0
        that->recordTahoeLinkContext(
            kAirportItlwmRegDiagLinkContextPublishQueue,
            kAirportItlwmRegDiagLinkContextSourceUnavailable,
            static_cast<uint32_t>(linkState), rawCode,
            AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_STATUS_UNAVAILABLE,
            kAirportItlwmRegDiagLinkContextLifecyclePublicationUnavailable,
            kIOReturnNotReady,
            AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_EPOCH_CURRENT, -1);
#endif
        airportItlwmRegDiagRecordLinkPublish(
            kAirportItlwmRegDiagLinkPublishSourceUnavailable,
            static_cast<uint32_t>(linkState), rawCode, kIOReturnNotReady);
        return;
    }
    ++state.users;
    IOInterruptEventSource *source = state.source;
    IOSimpleLock *payloadLock = state.payloadLock;
    source->retain();
    IOSimpleLockUnlockEnableInterrupt(admissionLock, admissionIrq);

    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(payloadLock);
    state.pendingState = linkState;
    state.pendingRawCode = rawCode;
    state.pendingValid = true;
    IOSimpleLockUnlockEnableInterrupt(payloadLock, irq);
#if __IO80211_TARGET >= __MAC_26_0
    that->recordTahoeLinkContext(
        kAirportItlwmRegDiagLinkContextPublishQueue,
        kAirportItlwmRegDiagLinkContextSourceReady,
        static_cast<uint32_t>(linkState), rawCode,
        AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_STATUS_UNAVAILABLE,
        kAirportItlwmRegDiagLinkContextLifecyclePublicationReady,
        kIOReturnSuccess, AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_EPOCH_CURRENT,
        -1);
#endif
    airportItlwmRegDiagRecordLinkPublish(
        kAirportItlwmRegDiagLinkPublishQueued,
        static_cast<uint32_t>(linkState), rawCode, kIOReturnSuccess);
    source->interruptOccurred(0, 0, 0);
    source->release();

    admissionIrq = IOSimpleLockLockDisableInterrupt(admissionLock);
    if (state.users != 0)
        --state.users;
    IOSimpleLockUnlockEnableInterrupt(admissionLock, admissionIrq);
}

// Drain and release the off-gate publication source. Idempotent. Must be called
// before fNetIf is detached/released so a deferred action cannot reach the
// publication worker after fNetIf's lifetime ends: removeEventSource drains the
// in-flight action on the work-queue thread, and the pending record is cleared
// so no further transition can be serviced.  Closing sidecar admission first
// eliminates the producer check-then-free race with source/payload teardown.
static void teardownLinkStatePublishSource(AirportItlwm *that,
                                           IOWorkLoop *workloop)
{
    if (that == NULL)
        return;

    AirportItlwmLinkStatePublishLifecycle &state =
        that->fLinkStatePublishLifecycle;
    IOSimpleLock *admissionLock = state.admissionLock;
    if (that == NULL || admissionLock == NULL)
        return;

    IOInterruptState admissionIrq =
        IOSimpleLockLockDisableInterrupt(admissionLock);
    if (state.tearingDown) {
        IOSimpleLockUnlockEnableInterrupt(admissionLock, admissionIrq);
        // A concurrent lifecycle convergence must not release the workloop
        // while the first owner is inside removeEventSource().
        for (;;) {
            admissionIrq = IOSimpleLockLockDisableInterrupt(admissionLock);
            const bool complete = !state.tearingDown;
            IOSimpleLockUnlockEnableInterrupt(admissionLock, admissionIrq);
            if (complete)
                return;
            IOSleep(1);
        }
    }
    state.stopping = true;
    state.tearingDown = true;
    IOSimpleLockUnlockEnableInterrupt(admissionLock, admissionIrq);

    // Close admission before waiting. A setup owner does not clear
    // settingUp until it has removed every unpublished local object, and a
    // producer keeps its retained source until it decrements users.
    for (;;) {
        admissionIrq = IOSimpleLockLockDisableInterrupt(admissionLock);
        const bool drained = !state.settingUp && state.users == 0;
        IOSimpleLockUnlockEnableInterrupt(admissionLock, admissionIrq);
        if (drained)
            break;
        IOSleep(1);
    }

    admissionIrq = IOSimpleLockLockDisableInterrupt(admissionLock);
    IOInterruptEventSource *source = state.source;
    IOSimpleLock *payloadLock = state.payloadLock;
    if (source != NULL)
        source->retain();
    if (workloop != NULL)
        workloop->retain();
    IOSimpleLockUnlockEnableInterrupt(admissionLock, admissionIrq);

    // Do not clear source/payload or tearingDown before the workloop has
    // acknowledged the event source: an action already admitted above owns
    // `that` until removeEventSource() returns.
    if (source != NULL) {
        source->disable();
        if (workloop != NULL)
            workloop->removeEventSource(source);
    }

    // The workloop has acknowledged the action. Withdraw the published
    // pointers while tearingDown remains true, before a reference drop can
    // destroy either object. A second teardown continues to wait on the bit.
    admissionIrq = IOSimpleLockLockDisableInterrupt(admissionLock);
    state.source = NULL;
    state.payloadLock = NULL;
    state.pendingValid = false;
    IOSimpleLockUnlockEnableInterrupt(admissionLock, admissionIrq);

    if (payloadLock != NULL)
        IOSimpleLockFree(payloadLock);
    if (source != NULL) {
        // Drop the state-owned reference followed by the local teardown
        // retain. The latter protects the source through remove/clear.
        source->release();
        source->release();
    }
    if (workloop != NULL)
        workloop->release();

    // Do not publish completion until every owned reference and payload
    // allocation above is gone. A concurrent teardown waits on this bit.
    admissionIrq = IOSimpleLockLockDisableInterrupt(admissionLock);
    state.tearingDown = false;
    IOSimpleLockUnlockEnableInterrupt(admissionLock, admissionIrq);
}

#if __IO80211_TARGET >= __MAC_26_0
/*
 * The IWX nswq terminal worker must never synchronously wait for this
 * controller's command gate: a power-off path may own that gate while it
 * waits taskq_barrier(sc_nswq).  This mailbox owns a bounded copied record,
 * signals an IOInterruptEventSource, and returns.  Its action runs later on
 * the controller workloop, where the command gate is recursively available.
 */
static void
saeTransportMailboxInterruptAction(OSObject *owner,
                                   IOInterruptEventSource *sender,
                                   int /*count*/)
{
    AirportItlwm *that = OSDynamicCast(AirportItlwm, owner);
    if (that == NULL)
        return;

    AirportItlwmControllerLifecycleOperationGuard lifecycle(that, true);
    if (!lifecycle.admitted())
        return;

    AirportItlwmSaeTransportMailboxLifecycle &state =
        that->fSaeTransportMailbox;
    IOSimpleLock *admissionLock = state.admissionLock;
    if (admissionLock == NULL)
        return;

    for (;;) {
        AirportItlwmSaeTransportMailboxEntry entry;
        bool haveEntry = false;

        explicit_bzero(&entry, sizeof(entry));
        IOInterruptState admissionIrq =
            IOSimpleLockLockDisableInterrupt(admissionLock);
        if (state.settingUp || state.stopping || state.tearingDown ||
            sender != state.source || state.payloadLock == NULL) {
            IOSimpleLockUnlockEnableInterrupt(admissionLock, admissionIrq);
            return;
        }
        IOSimpleLock *payloadLock = state.payloadLock;
        IOInterruptState payloadIrq =
            IOSimpleLockLockDisableInterrupt(payloadLock);
        if (state.count != 0) {
            entry = state.entries[state.head];
            explicit_bzero(&state.entries[state.head],
                           sizeof(state.entries[state.head]));
            state.head = (state.head + 1) %
                kAirportItlwmSaeTransportMailboxCapacity;
            state.count--;
            haveEntry = true;
        }
        IOSimpleLockUnlockEnableInterrupt(payloadLock, payloadIrq);
        IOSimpleLockUnlockEnableInterrupt(admissionLock, admissionIrq);

        if (!haveEntry)
            return;
        if (itl_sae_auth_transport_event_is_well_formed(&entry.event))
            dispatchSaeTransportMailboxEvent(that, &entry.event,
                                             entry.isReset);
        explicit_bzero(&entry, sizeof(entry));
    }
}

static bool
setupSaeTransportMailboxSource(AirportItlwm *that, IOWorkLoop *workloop)
{
    if (that == NULL || workloop == NULL)
        return false;

    AirportItlwmSaeTransportMailboxLifecycle &state =
        that->fSaeTransportMailbox;
    IOSimpleLock *lifecycleLock = that->fLifecycleAdmissionLock;
    IOSimpleLock *admissionLock = state.admissionLock;
    if (lifecycleLock == NULL || admissionLock == NULL)
        return false;

    IOInterruptState lifecycleIrq =
        IOSimpleLockLockDisableInterrupt(lifecycleLock);
    if (that->fLifecyclePhase != kAirportItlwmLifecycleStarting &&
        that->fLifecyclePhase != kAirportItlwmLifecycleLive) {
        IOSimpleLockUnlockEnableInterrupt(lifecycleLock, lifecycleIrq);
        return false;
    }
    IOInterruptState admissionIrq =
        IOSimpleLockLockDisableInterrupt(admissionLock);
    if (state.settingUp || state.stopping || state.tearingDown ||
        state.source != NULL || state.payloadLock != NULL) {
        IOSimpleLockUnlockEnableInterrupt(admissionLock, admissionIrq);
        IOSimpleLockUnlockEnableInterrupt(lifecycleLock, lifecycleIrq);
        return false;
    }
    state.settingUp = true;
    state.stopping = false;
    state.tearingDown = false;
    workloop->retain();
    IOSimpleLockUnlockEnableInterrupt(admissionLock, admissionIrq);
    IOSimpleLockUnlockEnableInterrupt(lifecycleLock, lifecycleIrq);

    IOSimpleLock *payloadLock = IOSimpleLockAlloc();
    IOInterruptEventSource *source = IOInterruptEventSource::
        interruptEventSource(that,
            (IOInterruptEventSource::Action)saeTransportMailboxInterruptAction);
    bool sourceAdded = false;
    bool installed = false;
    if (payloadLock != NULL && source != NULL &&
        workloop->addEventSource(source) == kIOReturnSuccess) {
        sourceAdded = true;
        source->enable();

        admissionIrq = IOSimpleLockLockDisableInterrupt(admissionLock);
        if (!state.stopping && !state.tearingDown) {
            state.source = source;
            state.payloadLock = payloadLock;
            state.users = 0;
            state.head = 0;
            state.tail = 0;
            state.count = 0;
            explicit_bzero(state.entries, sizeof(state.entries));
            state.settingUp = false;
            installed = true;
        }
        IOSimpleLockUnlockEnableInterrupt(admissionLock, admissionIrq);
    }

    if (installed) {
        workloop->release();
        return true;
    }

    if (source != NULL) {
        source->disable();
        if (sourceAdded)
            workloop->removeEventSource(source);
        source->release();
    }
    if (payloadLock != NULL)
        IOSimpleLockFree(payloadLock);
    admissionIrq = IOSimpleLockLockDisableInterrupt(admissionLock);
    state.settingUp = false;
    IOSimpleLockUnlockEnableInterrupt(admissionLock, admissionIrq);
    workloop->release();
    return false;
}

static void
queueSaeTransportMailbox(
    AirportItlwm *that, const struct ItlSaeAuthTransportEventV1 *event,
    bool isReset)
{
    if (that == NULL || event == NULL ||
        !itl_sae_auth_transport_event_is_well_formed(event))
        return;

    AirportItlwmSaeTransportMailboxLifecycle &state =
        that->fSaeTransportMailbox;
    IOSimpleLock *admissionLock = state.admissionLock;
    if (admissionLock == NULL)
        return;

    IOInterruptState admissionIrq =
        IOSimpleLockLockDisableInterrupt(admissionLock);
    if (state.settingUp || state.stopping || state.tearingDown ||
        state.source == NULL || state.payloadLock == NULL) {
        IOSimpleLockUnlockEnableInterrupt(admissionLock, admissionIrq);
        /* A lifecycle-admitted SAE event without its mandatory source is fatal. */
        panic("%s: SAE transport mailbox unavailable", __FUNCTION__);
    }
    ++state.users;
    IOInterruptEventSource *source = state.source;
    IOSimpleLock *payloadLock = state.payloadLock;
    source->retain();
    IOSimpleLockUnlockEnableInterrupt(admissionLock, admissionIrq);

    bool overflow = false;
    IOInterruptState payloadIrq = IOSimpleLockLockDisableInterrupt(payloadLock);
    if (state.count >= kAirportItlwmSaeTransportMailboxCapacity) {
        overflow = true;
    } else {
        state.entries[state.tail].event = *event;
        state.entries[state.tail].isReset = isReset;
        state.tail = (state.tail + 1) %
            kAirportItlwmSaeTransportMailboxCapacity;
        state.count++;
    }
    IOSimpleLockUnlockEnableInterrupt(payloadLock, payloadIrq);

    if (!overflow)
        source->interruptOccurred(0, 0, 0);
    source->release();

    admissionIrq = IOSimpleLockLockDisableInterrupt(admissionLock);
    KASSERT(state.users != 0, "state.users != 0");
    state.users--;
    IOSimpleLockUnlockEnableInterrupt(admissionLock, admissionIrq);

    if (overflow)
        panic("%s: SAE transport mailbox overflow", __FUNCTION__);
}

static void
teardownSaeTransportMailboxSource(AirportItlwm *that, IOWorkLoop *workloop)
{
    if (that == NULL)
        return;

    AirportItlwmSaeTransportMailboxLifecycle &state =
        that->fSaeTransportMailbox;
    IOSimpleLock *admissionLock = state.admissionLock;
    if (admissionLock == NULL)
        return;

    IOInterruptState admissionIrq =
        IOSimpleLockLockDisableInterrupt(admissionLock);
    if (state.tearingDown) {
        IOSimpleLockUnlockEnableInterrupt(admissionLock, admissionIrq);
        for (;;) {
            admissionIrq = IOSimpleLockLockDisableInterrupt(admissionLock);
            const bool complete = !state.tearingDown;
            IOSimpleLockUnlockEnableInterrupt(admissionLock, admissionIrq);
            if (complete)
                return;
            IOSleep(1);
        }
    }
    state.stopping = true;
    state.tearingDown = true;
    IOSimpleLockUnlockEnableInterrupt(admissionLock, admissionIrq);

    /* Producers retain the source while they copy/signal outside this lock. */
    for (;;) {
        admissionIrq = IOSimpleLockLockDisableInterrupt(admissionLock);
        const bool drained = !state.settingUp && state.users == 0;
        IOSimpleLockUnlockEnableInterrupt(admissionLock, admissionIrq);
        if (drained)
            break;
        IOSleep(1);
    }

    admissionIrq = IOSimpleLockLockDisableInterrupt(admissionLock);
    IOInterruptEventSource *source = state.source;
    IOSimpleLock *payloadLock = state.payloadLock;
    if (source != NULL)
        source->retain();
    if (workloop != NULL)
        workloop->retain();
    IOSimpleLockUnlockEnableInterrupt(admissionLock, admissionIrq);

    if (source != NULL) {
        source->disable();
        if (workloop != NULL)
            workloop->removeEventSource(source);
    }

    admissionIrq = IOSimpleLockLockDisableInterrupt(admissionLock);
    state.source = NULL;
    state.payloadLock = NULL;
    state.head = 0;
    state.tail = 0;
    state.count = 0;
    explicit_bzero(state.entries, sizeof(state.entries));
    IOSimpleLockUnlockEnableInterrupt(admissionLock, admissionIrq);

    if (payloadLock != NULL)
        IOSimpleLockFree(payloadLock);
    if (source != NULL) {
        source->release();
        source->release();
    }
    if (workloop != NULL)
        workloop->release();

    admissionIrq = IOSimpleLockLockDisableInterrupt(admissionLock);
    state.tearingDown = false;
    IOSimpleLockUnlockEnableInterrupt(admissionLock, admissionIrq);
}
#endif

// The Skywalk interface historically copied AirportItlwm::scanSource into a
// second raw field. Keep that field layout-compatible, but the per-controller
// fScanSourceLifecycle record is the only live owner/admission authority.

static bool setupScanSource(AirportItlwm *that, IOWorkLoop *workloop)
{
    if (that == NULL || workloop == NULL)
        return false;

    AirportItlwmScanSourceLifecycle &state = that->fScanSourceLifecycle;
    IOSimpleLock *lifecycleLock = that->fLifecycleAdmissionLock;
    IOSimpleLock *admissionLock = state.admissionLock;
    if (lifecycleLock == NULL || admissionLock == NULL)
        return false;

    IOInterruptState lifecycleIrq =
        IOSimpleLockLockDisableInterrupt(lifecycleLock);
    if (that->fLifecyclePhase != kAirportItlwmLifecycleStarting &&
        that->fLifecyclePhase != kAirportItlwmLifecycleLive) {
        IOSimpleLockUnlockEnableInterrupt(lifecycleLock, lifecycleIrq);
        return false;
    }
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(admissionLock);
    if (state.settingUp || state.stopping || state.tearingDown ||
        state.source != NULL) {
        IOSimpleLockUnlockEnableInterrupt(admissionLock, irq);
        IOSimpleLockUnlockEnableInterrupt(lifecycleLock, lifecycleIrq);
        return false;
    }
    state.settingUp = true;
    state.stopping = false;
    state.tearingDown = false;
    workloop->retain();
    IOSimpleLockUnlockEnableInterrupt(admissionLock, irq);
    IOSimpleLockUnlockEnableInterrupt(lifecycleLock, lifecycleIrq);

    IOTimerEventSource *source =
        IOTimerEventSource::timerEventSource(that, &AirportItlwm::fakeScanDone);
    bool sourceAdded = false;
    bool installed = false;
    if (source != NULL && workloop->addEventSource(source) == kIOReturnSuccess) {
        sourceAdded = true;
        source->enable();

        irq = IOSimpleLockLockDisableInterrupt(admissionLock);
        if (!state.stopping && !state.tearingDown) {
            state.source = source;
            state.users = 0;
            that->scanSource = source;
            state.settingUp = false;
            installed = true;
        }
        IOSimpleLockUnlockEnableInterrupt(admissionLock, irq);
    }

    if (installed) {
        workloop->release();
        return true;
    }

    if (source != NULL) {
        source->disable();
        if (sourceAdded)
            workloop->removeEventSource(source);
        source->release();
    }
    irq = IOSimpleLockLockDisableInterrupt(admissionLock);
    state.settingUp = false;
    IOSimpleLockUnlockEnableInterrupt(admissionLock, irq);
    workloop->release();
    return false;
}

static bool acquireScanSource(AirportItlwm *that,
                              IOTimerEventSource **out)
{
    if (out == NULL)
        return false;
    *out = NULL;
    if (that == NULL)
        return false;

    AirportItlwmScanSourceLifecycle &state = that->fScanSourceLifecycle;
    IOSimpleLock *admissionLock = state.admissionLock;
    if (that == NULL || admissionLock == NULL)
        return false;

    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(admissionLock);
    if (state.settingUp || state.stopping || state.tearingDown ||
        state.source == NULL || that->scanSource != state.source) {
        IOSimpleLockUnlockEnableInterrupt(admissionLock, irq);
        return false;
    }
    ++state.users;
    *out = state.source;
    (*out)->retain();
    IOSimpleLockUnlockEnableInterrupt(admissionLock, irq);
    return true;
}

static void releaseScanSource(AirportItlwm *that,
                              IOTimerEventSource *source)
{
    if (source != NULL)
        source->release();

    if (that == NULL)
        return;

    AirportItlwmScanSourceLifecycle &state = that->fScanSourceLifecycle;
    IOSimpleLock *admissionLock = state.admissionLock;
    if (that == NULL || admissionLock == NULL)
        return;
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(admissionLock);
    if (state.users != 0)
        --state.users;
    IOSimpleLockUnlockEnableInterrupt(admissionLock, irq);
}

bool AirportItlwm::scheduleScanSource(uint32_t timeoutMs)
{
    IOTimerEventSource *source = NULL;
    if (!acquireScanSource(this, &source))
        return false;
    source->setTimeoutMS(timeoutMs);
    source->enable();
    releaseScanSource(this, source);
    return true;
}

bool AirportItlwm::cancelScanSource()
{
    IOTimerEventSource *source = NULL;
    if (!acquireScanSource(this, &source))
        return false;
    source->cancelTimeout();
    source->disable();
    releaseScanSource(this, source);
    return true;
}

bool AirportItlwm::scanSourceCallbackLive(IOTimerEventSource *sender)
{
    AirportItlwmScanSourceLifecycle &state = fScanSourceLifecycle;
    IOSimpleLock *admissionLock = state.admissionLock;
    if (sender == NULL || admissionLock == NULL)
        return false;
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(admissionLock);
    const bool live = !state.settingUp && !state.stopping &&
        !state.tearingDown && state.source == sender && scanSource == sender;
    IOSimpleLockUnlockEnableInterrupt(admissionLock, irq);
    return live;
}

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
static volatile uint32_t sEapolRxProbeLogCount = 0;
static volatile uint32_t sEapolTxProbeLogCount = 0;

#if __IO80211_TARGET >= __MAC_26_0
/*
 * Isolated safe-only post-PLTI trace.  Unlike RegDiag, this state has no
 * identity-bearing fields and its fast path never publishes or allocates.
 * activeToken couples capture generation and episode atomically, so a reset
 * cannot relabel a pre-reset producer as a fresh capture.
 */
struct AirportItlwmPostPltiTraceState {
    volatile uint32_t enabled;
    volatile uint32_t captureGeneration;
    volatile uint32_t backend;
    volatile uintptr_t targetController;
    volatile uint64_t activeToken;
    volatile uint32_t admitEpisodes;
    volatile uint8_t recorderLock;
    /* Even is open; odd fences a reset/seal/invalidation transition. */
    volatile uint32_t controlEpoch;
    volatile uint32_t producerCount;
    volatile uint32_t nextEpisode;
    volatile uint32_t episodeCount;
    volatile uint32_t droppedEntries;
    volatile uint32_t nextSequence;
    volatile uint32_t captureFirstSequence;
    volatile uint32_t lastControlSequence;
    AirportItlwmPostPltiTraceEntry
        entries[AIRPORT_ITLWM_POST_PLTI_TRACE_MAX_ENTRIES];
};

static AirportItlwmPostPltiTraceState sPostPltiTrace = {};
#endif

static const uint32_t kAirportItlwmEapolProbeLogLimit = 16;

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

static bool
airportItlwmRegDiagPacketProbeEnabled()
{
    return (sRegDiag.modeFlags & kAirportItlwmRegDiagModeEnabled) != 0 &&
           ((sRegDiag.modeFlags & kAirportItlwmRegDiagModeData) != 0 ||
            (sRegDiag.modeFlags & kAirportItlwmRegDiagModePmk) != 0 ||
            (sRegDiag.modeFlags & kAirportItlwmRegDiagModeIntervention) != 0);
}

static void
airportItlwmRegDiagInitTrace()
{
    sRegDiag.trace.version = AIRPORT_ITLWM_REGDIAG_ABI_VERSION;
}

static bool
airportItlwmEthernetBufferIsEapol(const void *data, size_t length)
{
    if (data == nullptr || length < sizeof(ether_header_t))
        return false;

    const ether_header_t *eh = reinterpret_cast<const ether_header_t *>(data);
    return eh->ether_type == htons(ETHERTYPE_PAE);
}

static const char *
airportItlwmEapolProbePathName(uint32_t path)
{
    return path == kAirportItlwmRegDiagPathRx ? "rx" : "tx";
}

static void
airportItlwmLogEapolProbe(uint32_t path, const char *stage, uint32_t length,
                          IOReturn result)
{
    volatile uint32_t *counter = path == kAirportItlwmRegDiagPathRx ?
                                 &sEapolRxProbeLogCount :
                                 &sEapolTxProbeLogCount;
    const uint32_t count = ++(*counter);
    if (count <= kAirportItlwmEapolProbeLogLimit) {
        XYLog("ITLWM_EAPOL path=%s count=%u stage=%s len=%u result=0x%x\n",
              airportItlwmEapolProbePathName(path), count,
              stage != nullptr ? stage : "unknown", length,
              static_cast<uint32_t>(result));
    }
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
    airportItlwmRegDiagSetFlagFromControl(command, "pmk",
                                          kAirportItlwmRegDiagModePmk,
                                          &modeFlags);
    airportItlwmRegDiagSetFlagFromControl(command, "context",
                                          kAirportItlwmRegDiagModeLinkContext,
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
    snap.pmPowerState = driver != nullptr ?
        (driver->pmPowerStateFlags & kAirportItlwmPmSystemOnBit) : 0;
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
    snap.lastAssocAuthFlags = sRegDiag.snapshot.lastAssocAuthFlags;
    snap.lastAssocCandidateCount = sRegDiag.snapshot.lastAssocCandidateCount;
    snap.lastAssocPmfCapability = sRegDiag.snapshot.lastAssocPmfCapability;
    snap.lastAssocPolicyFlags = sRegDiag.snapshot.lastAssocPolicyFlags;
    snap.pmkIngressCount = sRegDiag.snapshot.pmkIngressCount;
    snap.pmkIngressRejectCount = sRegDiag.snapshot.pmkIngressRejectCount;
    snap.pmkClearCount = sRegDiag.snapshot.pmkClearCount;
    snap.pltiPublishCount = sRegDiag.snapshot.pltiPublishCount;
    snap.pltiPublishRejectCount = sRegDiag.snapshot.pltiPublishRejectCount;
    snap.pltiDeliverCount = sRegDiag.snapshot.pltiDeliverCount;
    snap.pltiDeliverRejectCount = sRegDiag.snapshot.pltiDeliverRejectCount;
    snap.lastPmkSource = sRegDiag.snapshot.lastPmkSource;
    snap.lastPmkDecision = sRegDiag.snapshot.lastPmkDecision;
    snap.lastPmkKeyLen = sRegDiag.snapshot.lastPmkKeyLen;
    snap.lastPmkAuthUpper = sRegDiag.snapshot.lastPmkAuthUpper;
    snap.lastPmkGeneration = sRegDiag.snapshot.lastPmkGeneration;
    snap.lastPmkClearReason = sRegDiag.snapshot.lastPmkClearReason;

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

    return airportItlwmEthernetBufferIsEapol(mbuf_data(m), len);
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
airportItlwmRegDiagRecordAssocPolicy(uint32_t path, uint32_t authLower,
                                     uint32_t authUpper, uint32_t rsnIeLen,
                                     uint32_t pmfCapability,
                                     uint32_t authFlags,
                                     uint32_t candidateCount,
                                     uint32_t policyFlags)
{
    if (!airportItlwmRegDiagEnabled(kAirportItlwmRegDiagModeAssoc))
        return;

    AirportItlwmRegDiagSnapshot *snap = &sRegDiag.snapshot;
    snap->lastAssocAuthFlags = authFlags;
    snap->lastAssocCandidateCount = candidateCount;
    snap->lastAssocPmfCapability = pmfCapability;
    snap->lastAssocPolicyFlags = policyFlags;
    airportItlwmRegDiagTrace(kAirportItlwmRegDiagTraceAuthPolicy, path,
                             (policyFlags &
                              kAirportItlwmRegDiagAssocPolicyRejectWpa3) != 0 ?
                                 kIOReturnUnsupported : kIOReturnSuccess,
                             static_cast<int32_t>(pmfCapability),
                             (static_cast<uint64_t>(authUpper) << 32) |
                                 authLower,
                             (static_cast<uint64_t>(rsnIeLen) << 32) |
                                 policyFlags);
}

static uint32_t
airportItlwmRegDiagPmkSourceForTag(const char *sourceTag)
{
    if (sourceTag == nullptr)
        return kAirportItlwmRegDiagPmkSourceUnknown;
    if (strcmp(sourceTag, "CIPHER_KEY") == 0)
        return kAirportItlwmRegDiagPmkSourceCipherKey;
    if (strcmp(sourceTag, "CIPHER_KEY_MSK") == 0)
        return kAirportItlwmRegDiagPmkSourceCipherKeyMsk;
    if (strcmp(sourceTag, "CUR_PMK") == 0)
        return kAirportItlwmRegDiagPmkSourceCurPmk;
    return kAirportItlwmRegDiagPmkSourceUnknown;
}

static uint32_t
airportItlwmRegDiagPmkClearReasonForTag(const char *reasonTag)
{
    if (reasonTag == nullptr)
        return kAirportItlwmRegDiagPmkClearUnknown;
    if (strcmp(reasonTag, "associateSSID_disable_rsn") == 0)
        return kAirportItlwmRegDiagPmkClearAssocDisableRsn;
    if (strcmp(reasonTag, "setDISASSOCIATE") == 0)
        return kAirportItlwmRegDiagPmkClearDisassociate;
    if (strcmp(reasonTag, "setCLEAR_PMKSA_CACHE") == 0)
        return kAirportItlwmRegDiagPmkClearPmksa;
    if (strcmp(reasonTag, "setWCL_LEAVE_NETWORK") == 0)
        return kAirportItlwmRegDiagPmkClearLeave;
    if (strcmp(reasonTag, "setWCL_REASSOC") == 0)
        return kAirportItlwmRegDiagPmkClearReassoc;
    if (strcmp(reasonTag, "setWCL_JOIN_ABORT") == 0)
        return kAirportItlwmRegDiagPmkClearJoinAbort;
    if (strcmp(reasonTag, "releaseAll") == 0)
        return kAirportItlwmRegDiagPmkClearTerminate;
    return kAirportItlwmRegDiagPmkClearUnknown;
}

void
airportItlwmRegDiagRecordPmkIngress(const char *sourceTag, uint32_t decision,
                                    IOReturn result, uint32_t authUpper,
                                    uint32_t keyLen)
{
    if (!airportItlwmRegDiagEnabled(kAirportItlwmRegDiagModePmk))
        return;

    AirportItlwmRegDiagSnapshot *snap = &sRegDiag.snapshot;
    snap->pmkIngressCount++;
    if (decision != kAirportItlwmRegDiagPmkDecisionAccepted)
        snap->pmkIngressRejectCount++;
    snap->lastPmkSource = airportItlwmRegDiagPmkSourceForTag(sourceTag);
    snap->lastPmkDecision = decision;
    snap->lastPmkKeyLen = keyLen;
    snap->lastPmkAuthUpper = authUpper;
    snap->lastPmkGeneration = 0;
    airportItlwmRegDiagTrace(kAirportItlwmRegDiagTracePmkIngress,
                             kAirportItlwmRegDiagPathPmk, result,
                             static_cast<int32_t>(snap->lastPmkSource),
                             (static_cast<uint64_t>(authUpper) << 32) |
                                 keyLen,
                             decision);
}

void
airportItlwmRegDiagRecordPmkClear(const char *reasonTag)
{
    if (!airportItlwmRegDiagEnabled(kAirportItlwmRegDiagModePmk))
        return;

    AirportItlwmRegDiagSnapshot *snap = &sRegDiag.snapshot;
    snap->pmkClearCount++;
    snap->lastPmkClearReason =
        airportItlwmRegDiagPmkClearReasonForTag(reasonTag);
    airportItlwmRegDiagTrace(kAirportItlwmRegDiagTracePmkClear,
                             kAirportItlwmRegDiagPathLifecycle,
                             kIOReturnSuccess,
                             static_cast<int32_t>(snap->lastPmkClearReason),
                             0, 0);
}

void
airportItlwmRegDiagRecordPlti(uint32_t traceKind, uint32_t decision,
                              IOReturn result, uint32_t authUpper,
                              uint64_t generation)
{
    if (!airportItlwmRegDiagEnabled(kAirportItlwmRegDiagModePmk))
        return;

    AirportItlwmRegDiagSnapshot *snap = &sRegDiag.snapshot;
    if (traceKind == kAirportItlwmRegDiagTracePltiPublish) {
        snap->pltiPublishCount++;
        if (decision != kAirportItlwmRegDiagPmkDecisionAccepted)
            snap->pltiPublishRejectCount++;
    } else if (traceKind == kAirportItlwmRegDiagTracePltiDeliver) {
        snap->pltiDeliverCount++;
        if (decision != kAirportItlwmRegDiagPmkDecisionAccepted)
            snap->pltiDeliverRejectCount++;
    } else {
        return;
    }
    snap->lastPmkSource = kAirportItlwmRegDiagPmkSourcePlti;
    snap->lastPmkDecision = decision;
    snap->lastPmkAuthUpper = authUpper;
    snap->lastPmkGeneration = generation;
    airportItlwmRegDiagTrace(traceKind, kAirportItlwmRegDiagPathPlti, result,
                             static_cast<int32_t>(decision), generation,
                             authUpper);
}

void
airportItlwmRegDiagRecordLinkStatus(uint32_t decision,
                                    uint32_t previousStatus,
                                    uint32_t requestedStatus,
                                    IOReturn result)
{
    if (!airportItlwmRegDiagEnabled(kAirportItlwmRegDiagModeControl))
        return;

    airportItlwmRegDiagTrace(kAirportItlwmRegDiagTraceLinkStatus,
                             kAirportItlwmRegDiagPathLink, result,
                             static_cast<int32_t>(decision),
                             (static_cast<uint64_t>(previousStatus) << 32) |
                                 requestedStatus,
                             0);
}

void
airportItlwmRegDiagRecordLinkPublish(uint32_t decision, uint32_t linkState,
                                     uint32_t rawCode, IOReturn result)
{
    if (!airportItlwmRegDiagEnabled(kAirportItlwmRegDiagModeControl))
        return;

    airportItlwmRegDiagTrace(kAirportItlwmRegDiagTraceLinkPublish,
                             kAirportItlwmRegDiagPathLink, result,
                             static_cast<int32_t>(decision), linkState,
                             rawCode);
}

bool
airportItlwmRegDiagShouldRecordLinkContext()
{
    return airportItlwmRegDiagEnabled(kAirportItlwmRegDiagModeLinkContext);
}

static uint32_t
airportItlwmRegDiagLinkContextPredicate(int32_t value)
{
    if (value > 0)
        return kAirportItlwmRegDiagLinkContextPredicateTrue;
    if (value == 0)
        return kAirportItlwmRegDiagLinkContextPredicateFalse;
    return kAirportItlwmRegDiagLinkContextPredicateUnknown;
}

void
airportItlwmRegDiagRecordLinkContext(uint32_t route, uint32_t stage,
                                     uint32_t linkState, uint32_t rawCode,
                                     uint32_t controllerStatus,
                                     uint32_t lifecycle, uint64_t assocEpoch,
                                     int32_t onThread, int32_t inGate,
                                     int32_t onDispatchQueue, IOReturn result)
{
    if (!airportItlwmRegDiagShouldRecordLinkContext())
        return;

    const uint32_t context =
        (route & AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_ROUTE_MASK) |
        ((stage << AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_STAGE_SHIFT) &
         AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_STAGE_MASK) |
        ((airportItlwmRegDiagLinkContextPredicate(onThread) <<
          AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_ON_THREAD_SHIFT) &
         AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_ON_THREAD_MASK) |
        ((airportItlwmRegDiagLinkContextPredicate(inGate) <<
          AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_IN_GATE_SHIFT) &
         AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_IN_GATE_MASK) |
        ((airportItlwmRegDiagLinkContextPredicate(onDispatchQueue) <<
          AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_ON_DISPATCH_SHIFT) &
         AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_ON_DISPATCH_MASK) |
        ((lifecycle << AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_LIFECYCLE_SHIFT) &
         AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_LIFECYCLE_MASK) |
        ((linkState << AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_LINK_STATE_SHIFT) &
         AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_LINK_STATE_MASK);
    airportItlwmRegDiagTrace(
        kAirportItlwmRegDiagTraceLinkContext, kAirportItlwmRegDiagPathLink,
        result, static_cast<int32_t>(context), assocEpoch,
        (static_cast<uint64_t>(controllerStatus) << 32) | rawCode);
}

#if __IO80211_TARGET >= __MAC_26_0
uint64_t
AirportItlwm::currentTahoeAssociationEpoch() const
{
    ItlHalService *hal = fHalService;
    if (hal == nullptr)
        return 0;
    const struct ieee80211com *ic = hal->get80211Controller();
    return ieee80211_pae_assoc_epoch_current(ic);
}

void
AirportItlwm::recordTahoeLinkContext(uint32_t route, uint32_t stage,
                                     uint32_t linkState, uint32_t rawCode,
                                     uint32_t controllerStatus,
                                     uint32_t lifecycle, IOReturn result,
                                     uint64_t assocEpoch,
                                     int32_t onDispatchQueue)
{
    if (!airportItlwmRegDiagShouldRecordLinkContext())
        return;

    if (assocEpoch == AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_EPOCH_CURRENT)
        assocEpoch = currentTahoeAssociationEpoch();

    IOWorkLoop *workLoop = getWorkLoop();
    const int32_t onThread = workLoop != nullptr && workLoop->onThread() ? 1 :
                             workLoop != nullptr ? 0 : -1;
    const int32_t inGate = workLoop != nullptr && workLoop->inGate() ? 1 :
                           workLoop != nullptr ? 0 : -1;
    airportItlwmRegDiagRecordLinkContext(
        route, stage, linkState, rawCode, controllerStatus, lifecycle,
        assocEpoch, onThread, inGate, onDispatchQueue, result);
}

extern "C" void
AirportItlwmRegDiagNet80211LinkContext(struct ieee80211com *ic,
                                       uint32_t linkState,
                                       uint64_t assocEpoch)
{
    if (!airportItlwmRegDiagShouldRecordLinkContext() || ic == nullptr)
        return;

    /*
     * net80211 can call this from a producer context without a controller
     * lifecycle admission.  Keep the bridge strictly self-contained: it
     * records the already-sampled atomic epoch but never casts, dereferences
     * the controller, samples a work loop, or reaches HAL state.
     *
     * linkState is the local BSD LINK_STATE_* value here; downstream Tahoe
     * route markers use IO80211LinkState.  The shared epoch and ordered stage
     * chain, not those route-local numeric encodings, correlate the census.
     */
    airportItlwmRegDiagRecordLinkContext(
        kAirportItlwmRegDiagLinkContextNet80211Bridge,
        kAirportItlwmRegDiagLinkContextEnter, linkState, 0,
        AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_STATUS_UNAVAILABLE,
        kAirportItlwmRegDiagLinkContextLifecycleUnknown, assocEpoch,
        -1, -1, -1, kIOReturnSuccess);
}
#endif

void
airportItlwmRegDiagRecordJoinAbort(uint32_t phase, int32_t icState,
                                   uint32_t requestCompletion, IOReturn result)
{
    if (!airportItlwmRegDiagEnabled(kAirportItlwmRegDiagModeControl))
        return;

    airportItlwmRegDiagTrace(kAirportItlwmRegDiagTraceWclJoinAbort,
                             kAirportItlwmRegDiagPathLifecycle, result,
                             static_cast<int32_t>(phase),
                             static_cast<uint32_t>(icState),
                             requestCompletion);
}

bool
airportItlwmRegDiagShouldTracePacket(bool eapol)
{
    return airportItlwmRegDiagEnabled(kAirportItlwmRegDiagModeData) ||
           (eapol &&
            airportItlwmRegDiagEnabled(kAirportItlwmRegDiagModePmk));
}

void
airportItlwmRegDiagRecordData(uint32_t path, uint32_t length, bool eapol,
                              IOReturn result)
{
    if (!airportItlwmRegDiagShouldTracePacket(eapol))
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

/*
 * BEGIN SAFE_POST_PLTI_TRACE
 *
 * This recorder is deliberately separate from AirportItlwmRegDiag.  The
 * recorder only writes categorical event identifiers into a preallocated
 * fixed ring.  Property publication occurs solely from watchdog polling.
 */
#if __IO80211_TARGET >= __MAC_26_0
static bool
airportItlwmPostPltiTraceEventIsKnown(uint32_t event)
{
    return event > kAirportItlwmPostPltiTraceEventUnknown &&
           event < kAirportItlwmPostPltiTraceEventMax;
}

/* Keep IWX-only PMF/BIP producer facts out of the established IWN ordered
 * association matrix even though the post-publication BIP helper is shared.
 * Keep the upper bound explicit: a future append-only generic event must not
 * silently become IWX-only simply because it follows this vocabulary. */
static bool
airportItlwmPostPltiTraceEventRequiresIwx(uint32_t event)
{
    return event >= kAirportItlwmPostPltiTraceEventIwxMfpPaeRxDelivered &&
           event <= kAirportItlwmPostPltiTraceEventIwxIgtkSlot5TxSelected;
}

/*
 * The trace has a fixed, non-sleeping serialization gate.  A producer first
 * enters an even control epoch; reset/seal/invalidation turn that epoch odd
 * and wait for established producers before changing capture state.  A
 * producer-side try-lock failure is accounted as a dropped entry while that
 * epoch guard is held, so an omitted categorical event cannot produce an
 * exact negative diagnostic verdict.
 * Producers never reserve a sequence without publishing it.
 */
static bool
airportItlwmPostPltiTraceTryLock()
{
    return !__atomic_test_and_set(&sPostPltiTrace.recorderLock,
                                  __ATOMIC_ACQUIRE);
}

static void
airportItlwmPostPltiTraceLock()
{
    while (!airportItlwmPostPltiTraceTryLock()) {
    }
}

static void
airportItlwmPostPltiTraceUnlock()
{
    __atomic_clear(&sPostPltiTrace.recorderLock, __ATOMIC_RELEASE);
}

/*
 * This is a non-sleeping reader-style guard for the state transitions that
 * change capture meaning.  A producer that loses the epoch race is discarded
 * rather than relabeled into the transition's new capture generation.
 */
static bool
airportItlwmPostPltiTraceProducerEnter()
{
    const uint32_t epoch = __atomic_load_n(&sPostPltiTrace.controlEpoch,
                                           __ATOMIC_ACQUIRE);
    if ((epoch & 1U) != 0)
        return false;
    __atomic_add_fetch(&sPostPltiTrace.producerCount, 1, __ATOMIC_ACQ_REL);
    if (epoch == __atomic_load_n(&sPostPltiTrace.controlEpoch,
                                 __ATOMIC_ACQUIRE))
        return true;
    /* A call racing a transition must not be relabeled into its new epoch. */
    __atomic_sub_fetch(&sPostPltiTrace.producerCount, 1, __ATOMIC_RELEASE);
    return false;
}

static void
airportItlwmPostPltiTraceProducerLeave()
{
    __atomic_sub_fetch(&sPostPltiTrace.producerCount, 1, __ATOMIC_RELEASE);
}

/*
 * A control transition becomes visible before it waits, then excludes all
 * newly arriving producers.  Existing producers either publish an event or
 * mark a loss before this function is allowed to take recorderLock.
 */
static void
airportItlwmPostPltiTraceControlLock()
{
    for (;;) {
        uint32_t epoch = __atomic_load_n(&sPostPltiTrace.controlEpoch,
                                         __ATOMIC_ACQUIRE);
        if ((epoch & 1U) != 0)
            continue;
        const uint32_t closingEpoch = epoch + 1;
        if (__atomic_compare_exchange_n(&sPostPltiTrace.controlEpoch, &epoch,
                                        closingEpoch, false, __ATOMIC_ACQ_REL,
                                        __ATOMIC_ACQUIRE))
            break;
    }
    while (__atomic_load_n(&sPostPltiTrace.producerCount,
                           __ATOMIC_ACQUIRE) != 0) {
    }
    airportItlwmPostPltiTraceLock();
}

static void
airportItlwmPostPltiTraceControlUnlock()
{
    airportItlwmPostPltiTraceUnlock();
    __atomic_add_fetch(&sPostPltiTrace.controlEpoch, 1, __ATOMIC_RELEASE);
}

static uint64_t
airportItlwmPostPltiTraceToken(uint32_t generation, uint32_t episode)
{
    return (static_cast<uint64_t>(generation) << 32) | episode;
}

static uint32_t
airportItlwmPostPltiTraceTokenGeneration(uint64_t token)
{
    return static_cast<uint32_t>(token >> 32);
}

static uint32_t
airportItlwmPostPltiTraceTokenEpisode(uint64_t token)
{
    return static_cast<uint32_t>(token);
}

static bool
airportItlwmPostPltiTraceAdmits(struct ieee80211com *ic)
{
    const uint32_t backend = __atomic_load_n(&sPostPltiTrace.backend,
                                              __ATOMIC_ACQUIRE);
    if (ic == nullptr ||
        __atomic_load_n(&sPostPltiTrace.enabled, __ATOMIC_ACQUIRE) == 0 ||
        (backend != kAirportItlwmPostPltiTraceBackendIwn &&
         backend != kAirportItlwmPostPltiTraceBackendIwx))
        return false;
    return __atomic_load_n(&sPostPltiTrace.targetController,
                           __ATOMIC_ACQUIRE) ==
        reinterpret_cast<uintptr_t>(ic);
}

static bool
airportItlwmPostPltiTraceMayBegin(struct ieee80211com *ic)
{
    return airportItlwmPostPltiTraceAdmits(ic) &&
           __atomic_load_n(&sPostPltiTrace.admitEpisodes,
                           __ATOMIC_ACQUIRE) != 0;
}

static bool
airportItlwmPostPltiTraceTokenIsCurrent(struct ieee80211com *ic,
                                        uint64_t token, bool requireActive)
{
    const uint32_t generation = airportItlwmPostPltiTraceTokenGeneration(token);
    if (token == 0 || generation == 0 ||
        airportItlwmPostPltiTraceTokenEpisode(token) == 0 ||
        !airportItlwmPostPltiTraceAdmits(ic) ||
        generation != __atomic_load_n(&sPostPltiTrace.captureGeneration,
                                      __ATOMIC_ACQUIRE))
        return false;
    return !requireActive ||
        __atomic_load_n(&sPostPltiTrace.activeToken, __ATOMIC_ACQUIRE) == token;
}

static void
airportItlwmPostPltiTraceMarkDropped()
{
    uint32_t observed = __atomic_load_n(&sPostPltiTrace.droppedEntries,
                                        __ATOMIC_ACQUIRE);
    while (observed != UINT32_MAX) {
        if (__atomic_compare_exchange_n(&sPostPltiTrace.droppedEntries,
                                        &observed, observed + 1, false,
                                        __ATOMIC_ACQ_REL,
                                        __ATOMIC_ACQUIRE))
            return;
    }
}

/* The caller holds a producer epoch guard while it accounts this loss. */
static void
airportItlwmPostPltiTraceNoteContendedProducer(struct ieee80211com *ic,
                                                bool mayBegin)
{
    const uint64_t token = __atomic_load_n(&sPostPltiTrace.activeToken,
                                           __ATOMIC_ACQUIRE);
    if (airportItlwmPostPltiTraceTokenIsCurrent(ic, token, true) ||
        (mayBegin && airportItlwmPostPltiTraceMayBegin(ic)))
        airportItlwmPostPltiTraceMarkDropped();
}

static void
airportItlwmPostPltiTraceRecordToken(struct ieee80211com *ic, uint32_t event,
                                     uint64_t token, bool requireActive)
{
    /* Every caller holds recorderLock; state cannot change mid-reservation. */
    if (!airportItlwmPostPltiTraceEventIsKnown(event) ||
        (airportItlwmPostPltiTraceEventRequiresIwx(event) &&
         __atomic_load_n(&sPostPltiTrace.backend, __ATOMIC_ACQUIRE) !=
             kAirportItlwmPostPltiTraceBackendIwx) ||
        !airportItlwmPostPltiTraceTokenIsCurrent(ic, token, requireActive))
        return;

    const uint32_t generation = airportItlwmPostPltiTraceTokenGeneration(token);
    const uint32_t episode = airportItlwmPostPltiTraceTokenEpisode(token);
    const uint32_t sequence =
        __atomic_add_fetch(&sPostPltiTrace.nextSequence, 1,
                           __ATOMIC_RELAXED);

    const uint32_t first = __atomic_load_n(
        &sPostPltiTrace.captureFirstSequence, __ATOMIC_ACQUIRE);
    if (sequence - first >= AIRPORT_ITLWM_POST_PLTI_TRACE_MAX_ENTRIES)
        airportItlwmPostPltiTraceMarkDropped();

    AirportItlwmPostPltiTraceEntry *entry =
        &sPostPltiTrace.entries[
            (sequence - 1) % AIRPORT_ITLWM_POST_PLTI_TRACE_MAX_ENTRIES];
    /* A sequence release store publishes atomic categorical fields only. */
    __atomic_store_n(&entry->sequence, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&entry->captureGeneration, generation, __ATOMIC_RELAXED);
    __atomic_store_n(&entry->episode, episode, __ATOMIC_RELAXED);
    __atomic_store_n(&entry->event, event, __ATOMIC_RELAXED);
    __atomic_store_n(&entry->sequence, sequence, __ATOMIC_RELEASE);
}

static void
airportItlwmPostPltiTraceCloseActive(struct ieee80211com *ic, uint32_t event)
{
    uint64_t token = __atomic_load_n(&sPostPltiTrace.activeToken,
                                     __ATOMIC_ACQUIRE);
    /* Every caller holds recorderLock before detaching this token. */
    if (token == 0)
        return;
    uint64_t expected = token;
    if (!__atomic_compare_exchange_n(&sPostPltiTrace.activeToken, &expected,
                                     0, false, __ATOMIC_ACQ_REL,
                                     __ATOMIC_ACQUIRE))
        return;
    /* The token was detached first, so later traffic cannot append to it. */
    airportItlwmPostPltiTraceRecordToken(ic, event, token, false);
}

extern "C" void
AirportItlwmPostPltiTraceRecord(struct ieee80211com *ic, uint32_t event)
{
    if (!airportItlwmPostPltiTraceProducerEnter())
        return;
    if (!airportItlwmPostPltiTraceTryLock()) {
        airportItlwmPostPltiTraceNoteContendedProducer(ic, false);
        airportItlwmPostPltiTraceProducerLeave();
        return;
    }
    const uint64_t token = __atomic_load_n(&sPostPltiTrace.activeToken,
                                           __ATOMIC_ACQUIRE);
    airportItlwmPostPltiTraceRecordToken(ic, event, token, true);
    airportItlwmPostPltiTraceUnlock();
    airportItlwmPostPltiTraceProducerLeave();
}

extern "C" void
AirportItlwmPostPltiTraceBeginEpisode(struct ieee80211com *ic)
{
    if (!airportItlwmPostPltiTraceProducerEnter())
        return;
    if (!airportItlwmPostPltiTraceTryLock()) {
        airportItlwmPostPltiTraceNoteContendedProducer(ic, true);
        airportItlwmPostPltiTraceProducerLeave();
        return;
    }
    if (!airportItlwmPostPltiTraceMayBegin(ic))
    {
        airportItlwmPostPltiTraceUnlock();
        airportItlwmPostPltiTraceProducerLeave();
        return;
    }

    if (__atomic_load_n(&sPostPltiTrace.activeToken, __ATOMIC_ACQUIRE) != 0)
        airportItlwmPostPltiTraceCloseActive(
            ic, kAirportItlwmPostPltiTraceEventEpisodeAborted);
    if (!airportItlwmPostPltiTraceMayBegin(ic))
    {
        airportItlwmPostPltiTraceUnlock();
        airportItlwmPostPltiTraceProducerLeave();
        return;
    }

    const uint32_t generation = __atomic_load_n(
        &sPostPltiTrace.captureGeneration, __ATOMIC_ACQUIRE);
    const uint32_t episode = __atomic_add_fetch(&sPostPltiTrace.nextEpisode,
                                                 1, __ATOMIC_RELAXED);
    if (generation == 0 || episode == 0)
    {
        airportItlwmPostPltiTraceUnlock();
        airportItlwmPostPltiTraceProducerLeave();
        return;
    }
    const uint64_t token = airportItlwmPostPltiTraceToken(generation, episode);
    uint64_t expected = 0;
    if (!__atomic_compare_exchange_n(&sPostPltiTrace.activeToken, &expected,
                                     token, false, __ATOMIC_ACQ_REL,
                                     __ATOMIC_ACQUIRE))
    {
        airportItlwmPostPltiTraceUnlock();
        airportItlwmPostPltiTraceProducerLeave();
        return;
    }
    if (!airportItlwmPostPltiTraceTokenIsCurrent(ic, token, true)) {
        expected = token;
        (void)__atomic_compare_exchange_n(&sPostPltiTrace.activeToken,
                                          &expected, 0, false,
                                          __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
        airportItlwmPostPltiTraceUnlock();
        airportItlwmPostPltiTraceProducerLeave();
        return;
    }
    __atomic_add_fetch(&sPostPltiTrace.episodeCount, 1, __ATOMIC_RELAXED);
    airportItlwmPostPltiTraceRecordToken(
        ic, kAirportItlwmPostPltiTraceEventWclPmkReadyScanResume, token, true);
    airportItlwmPostPltiTraceUnlock();
    airportItlwmPostPltiTraceProducerLeave();
}

extern "C" void
AirportItlwmPostPltiTraceCompleteEpisode(struct ieee80211com *ic)
{
    if (!airportItlwmPostPltiTraceProducerEnter())
        return;
    if (!airportItlwmPostPltiTraceTryLock()) {
        airportItlwmPostPltiTraceNoteContendedProducer(ic, false);
        airportItlwmPostPltiTraceProducerLeave();
        return;
    }
    /*
     * IWX needs one bounded post-port-valid observation window for the
     * subsequent group rekey.  It records the normal port-valid boundary but
     * leaves the same capture episode active until the explicit safe seal.
     * IWN's established ordered evaluator keeps its historical close-on-port
     * behavior unchanged.
     */
    if (__atomic_load_n(&sPostPltiTrace.backend, __ATOMIC_ACQUIRE) ==
        kAirportItlwmPostPltiTraceBackendIwx) {
        const uint64_t token = __atomic_load_n(&sPostPltiTrace.activeToken,
                                               __ATOMIC_ACQUIRE);
        airportItlwmPostPltiTraceRecordToken(
            ic, kAirportItlwmPostPltiTraceEventPortValidTransition, token,
            true);
    } else {
        airportItlwmPostPltiTraceCloseActive(
            ic, kAirportItlwmPostPltiTraceEventPortValidTransition);
    }
    airportItlwmPostPltiTraceUnlock();
    airportItlwmPostPltiTraceProducerLeave();
}

extern "C" void
AirportItlwmPostPltiTraceAbortEpisode(struct ieee80211com *ic)
{
    if (!airportItlwmPostPltiTraceProducerEnter())
        return;
    if (!airportItlwmPostPltiTraceTryLock()) {
        airportItlwmPostPltiTraceNoteContendedProducer(ic, false);
        airportItlwmPostPltiTraceProducerLeave();
        return;
    }
    airportItlwmPostPltiTraceCloseActive(
        ic, kAirportItlwmPostPltiTraceEventEpisodeAborted);
    airportItlwmPostPltiTraceUnlock();
    airportItlwmPostPltiTraceProducerLeave();
}

extern "C" void
AirportItlwmPostPltiTraceNoteStateRequest(struct ieee80211com *ic,
                                          uint32_t oldState,
                                          uint32_t nextState)
{
    if (!airportItlwmPostPltiTraceProducerEnter())
        return;
    if (!airportItlwmPostPltiTraceTryLock()) {
        airportItlwmPostPltiTraceNoteContendedProducer(ic, false);
        airportItlwmPostPltiTraceProducerLeave();
        return;
    }
    const uint64_t token = __atomic_load_n(&sPostPltiTrace.activeToken,
                                           __ATOMIC_ACQUIRE);
    if (!airportItlwmPostPltiTraceTokenIsCurrent(ic, token, true)) {
        airportItlwmPostPltiTraceUnlock();
        airportItlwmPostPltiTraceProducerLeave();
        return;
    }

    if (oldState == IEEE80211_S_SCAN && nextState == IEEE80211_S_SCAN) {
        airportItlwmPostPltiTraceRecordToken(
            ic, kAirportItlwmPostPltiTraceEventStateScanSelfRequestObserved,
            token, true);
        airportItlwmPostPltiTraceUnlock();
        airportItlwmPostPltiTraceProducerLeave();
        return;
    }

    if ((oldState == IEEE80211_S_SCAN && nextState == IEEE80211_S_AUTH) ||
        (oldState == IEEE80211_S_AUTH && nextState == IEEE80211_S_ASSOC) ||
        (oldState == IEEE80211_S_ASSOC && nextState == IEEE80211_S_RUN))
    {
        airportItlwmPostPltiTraceUnlock();
        airportItlwmPostPltiTraceProducerLeave();
        return;
    }
    airportItlwmPostPltiTraceCloseActive(
        ic, kAirportItlwmPostPltiTraceEventEpisodeAborted);
    airportItlwmPostPltiTraceUnlock();
    airportItlwmPostPltiTraceProducerLeave();
}

static bool
airportItlwmPostPltiTraceParseU32(const char *value, uint32_t *out)
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
    const char *p = value;
    for (; *p != '\0'; p++) {
        uint32_t digit;
        if (*p >= '0' && *p <= '9')
            digit = static_cast<uint32_t>(*p - '0');
        else if (base == 16 && *p >= 'a' && *p <= 'f')
            digit = static_cast<uint32_t>(*p - 'a' + 10);
        else if (base == 16 && *p >= 'A' && *p <= 'F')
            digit = static_cast<uint32_t>(*p - 'A' + 10);
        else
            break;
        if (digit >= base)
            break;
        result = result * base + digit;
        seen = true;
    }
    if (!seen || (*p != '\0' && *p != ';' && *p != ',' && *p != ' ' &&
                  *p != '\t'))
        return false;
    *out = result;
    return true;
}

static bool
airportItlwmPostPltiTraceReadU32Key(const char *command, const char *key,
                                    uint32_t *out)
{
    if (command == nullptr || key == nullptr || out == nullptr)
        return false;

    const size_t keyLen = strlen(key);
    const char *p = command;
    while (*p != '\0') {
        while (*p == ' ' || *p == '\t' || *p == ';' || *p == ',')
            p++;
        if (strncmp(p, key, keyLen) == 0 && p[keyLen] == '=')
            return airportItlwmPostPltiTraceParseU32(p + keyLen + 1, out);
        while (*p != '\0' && *p != ';' && *p != ',' && *p != ' ' &&
               *p != '\t')
            p++;
    }
    return false;
}

static void
airportItlwmPostPltiTracePublishData(AirportItlwm *driver, const char *name,
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
airportItlwmPostPltiTracePublishString(AirportItlwm *driver, const char *name,
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
airportItlwmPostPltiTraceBind(AirportItlwm *driver)
{
    struct ieee80211com *ic = nullptr;
    uint32_t backend = kAirportItlwmPostPltiTraceBackendUnknown;
    if (driver != nullptr && driver->fHalService != nullptr) {
        ic = driver->fHalService->get80211Controller();
        if (OSDynamicCast(ItlIwn, driver->fHalService) != nullptr)
            backend = kAirportItlwmPostPltiTraceBackendIwn;
        else if (OSDynamicCast(ItlIwx, driver->fHalService) != nullptr)
            backend = kAirportItlwmPostPltiTraceBackendIwx;
        else
            backend = kAirportItlwmPostPltiTraceBackendUnsupported;
    }
    __atomic_store_n(&sPostPltiTrace.backend, backend, __ATOMIC_RELEASE);
    __atomic_store_n(&sPostPltiTrace.targetController,
                     reinterpret_cast<uintptr_t>(ic), __ATOMIC_RELEASE);
}

/* Control fences established producers before it changes capture state. */
static void
airportItlwmPostPltiTraceApplyControl(AirportItlwm *driver,
                                      const char *command)
{
    uint32_t sequence = 0;
    if (!airportItlwmPostPltiTraceReadU32Key(command, "seq", &sequence)) {
        airportItlwmPostPltiTracePublishString(
            driver, AIRPORT_ITLWM_POST_PLTI_TRACE_CONTROL_ACK_PROPERTY,
            "seq=0 applied=0 error=missing-seq");
        return;
    }
    if (sequence == 0 || sequence == __atomic_load_n(
            &sPostPltiTrace.lastControlSequence, __ATOMIC_ACQUIRE))
        return;

    airportItlwmPostPltiTraceControlLock();
    if (sequence == __atomic_load_n(&sPostPltiTrace.lastControlSequence,
                                    __ATOMIC_ACQUIRE)) {
        if (__atomic_load_n(&sPostPltiTrace.enabled, __ATOMIC_ACQUIRE) != 0)
            airportItlwmPostPltiTraceMarkDropped();
        airportItlwmPostPltiTraceControlUnlock();
        return;
    }
    uint32_t enable = __atomic_load_n(&sPostPltiTrace.enabled,
                                      __ATOMIC_ACQUIRE);
    const uint32_t wasEnabled = enable;
    uint32_t reset = 0;
    uint32_t seal = 0;
    (void)airportItlwmPostPltiTraceReadU32Key(command, "enable", &enable);
    (void)airportItlwmPostPltiTraceReadU32Key(command, "reset", &reset);
    (void)airportItlwmPostPltiTraceReadU32Key(command, "seal", &seal);

    if (reset != 0) {
        /* Stop new admissions first; stale writers retain their old epoch. */
        __atomic_store_n(&sPostPltiTrace.enabled, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&sPostPltiTrace.admitEpisodes, 0,
                         __ATOMIC_RELEASE);
        uint32_t generation = __atomic_add_fetch(
            &sPostPltiTrace.captureGeneration, 1, __ATOMIC_RELAXED);
        if (generation == 0) {
            generation = 1;
            __atomic_store_n(&sPostPltiTrace.captureGeneration, generation,
                             __ATOMIC_RELEASE);
        }
        __atomic_store_n(&sPostPltiTrace.activeToken, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&sPostPltiTrace.nextEpisode, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&sPostPltiTrace.episodeCount, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&sPostPltiTrace.droppedEntries, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&sPostPltiTrace.captureFirstSequence,
                         __atomic_load_n(&sPostPltiTrace.nextSequence,
                                         __ATOMIC_ACQUIRE) + 1,
                         __ATOMIC_RELEASE);
        airportItlwmPostPltiTraceBind(driver);
    } else if (seal != 0) {
        /* Stop new episodes before detaching the active token and sealing it. */
        __atomic_store_n(&sPostPltiTrace.admitEpisodes, 0,
                         __ATOMIC_RELEASE);
        struct ieee80211com *ic = reinterpret_cast<struct ieee80211com *>(
            __atomic_load_n(&sPostPltiTrace.targetController,
                            __ATOMIC_ACQUIRE));
        airportItlwmPostPltiTraceCloseActive(
            ic, kAirportItlwmPostPltiTraceEventCaptureWindowSealed);
        enable = 0;
    } else if (enable != 0) {
        /* Rebinding an armed capture is a diagnostic loss boundary. */
        if (wasEnabled != 0)
            airportItlwmPostPltiTraceMarkDropped();
        airportItlwmPostPltiTraceBind(driver);
    } else {
        __atomic_store_n(&sPostPltiTrace.admitEpisodes, 0,
                         __ATOMIC_RELEASE);
        struct ieee80211com *ic = reinterpret_cast<struct ieee80211com *>(
            __atomic_load_n(&sPostPltiTrace.targetController,
                            __ATOMIC_ACQUIRE));
        airportItlwmPostPltiTraceCloseActive(
            ic, kAirportItlwmPostPltiTraceEventEpisodeAborted);
    }

    __atomic_store_n(&sPostPltiTrace.enabled, enable != 0 ? 1U : 0U,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&sPostPltiTrace.admitEpisodes, enable != 0 ? 1U : 0U,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&sPostPltiTrace.lastControlSequence, sequence,
                     __ATOMIC_RELEASE);

    const uint32_t bound = __atomic_load_n(&sPostPltiTrace.targetController,
                                           __ATOMIC_ACQUIRE) != 0 ? 1U : 0U;
    const uint32_t generation = __atomic_load_n(
        &sPostPltiTrace.captureGeneration, __ATOMIC_ACQUIRE);
    const uint32_t backend = __atomic_load_n(&sPostPltiTrace.backend,
                                              __ATOMIC_ACQUIRE);
    airportItlwmPostPltiTraceControlUnlock();

    char ack[176];
    snprintf(ack, sizeof(ack),
             "seq=%u applied=1 enable=%u reset=%u seal=%u bound=%u generation=%u backend=%u",
             sequence, enable != 0 ? 1U : 0U, reset != 0 ? 1U : 0U,
             seal != 0 ? 1U : 0U,
             bound, generation, backend);
    airportItlwmPostPltiTracePublishString(
        driver, AIRPORT_ITLWM_POST_PLTI_TRACE_CONTROL_ACK_PROPERTY, ack);
}

static void
airportItlwmPostPltiTracePublish(AirportItlwm *driver)
{
    airportItlwmPostPltiTraceLock();
    const uint32_t generation = __atomic_load_n(
        &sPostPltiTrace.captureGeneration, __ATOMIC_ACQUIRE);
    if (generation == 0) {
        airportItlwmPostPltiTraceUnlock();
        return;
    }

    const uint32_t firstSequence = __atomic_load_n(
        &sPostPltiTrace.captureFirstSequence, __ATOMIC_ACQUIRE);
    const uint32_t latestSequence = __atomic_load_n(
        &sPostPltiTrace.nextSequence, __ATOMIC_ACQUIRE);
    const uint32_t backend = __atomic_load_n(&sPostPltiTrace.backend,
                                              __ATOMIC_ACQUIRE);
    AirportItlwmPostPltiTraceBuffer buffer = {};
    buffer.version = AIRPORT_ITLWM_POST_PLTI_TRACE_ABI_VERSION;
    buffer.captureGeneration = generation;
    buffer.backend = backend;
    buffer.droppedEntries = __atomic_load_n(&sPostPltiTrace.droppedEntries,
                                            __ATOMIC_ACQUIRE);
    buffer.firstSequence = firstSequence;
    buffer.latestSequence = latestSequence;

    for (uint32_t i = 0; i < AIRPORT_ITLWM_POST_PLTI_TRACE_MAX_ENTRIES; i++) {
        const AirportItlwmPostPltiTraceEntry *source =
            &sPostPltiTrace.entries[i];
        const uint32_t entrySequence = __atomic_load_n(&source->sequence,
                                                        __ATOMIC_ACQUIRE);
        if (entrySequence == 0 || entrySequence < firstSequence ||
            entrySequence > latestSequence)
            continue;
        AirportItlwmPostPltiTraceEntry entry = {};
        entry.sequence = entrySequence;
        entry.captureGeneration = __atomic_load_n(&source->captureGeneration,
                                                  __ATOMIC_RELAXED);
        entry.episode = __atomic_load_n(&source->episode, __ATOMIC_RELAXED);
        entry.event = __atomic_load_n(&source->event, __ATOMIC_RELAXED);
        if (entrySequence != __atomic_load_n(&source->sequence,
                                             __ATOMIC_ACQUIRE) ||
            entry.captureGeneration != generation || entry.episode == 0 ||
            !airportItlwmPostPltiTraceEventIsKnown(entry.event))
            continue;
        buffer.entries[i] = entry;
        buffer.entryCount++;
    }

    /* Do not relabel a pre-reset snapshot as the current generation. */
    if (generation != __atomic_load_n(&sPostPltiTrace.captureGeneration,
                                      __ATOMIC_ACQUIRE) ||
        firstSequence != __atomic_load_n(
            &sPostPltiTrace.captureFirstSequence, __ATOMIC_ACQUIRE) ||
        backend != __atomic_load_n(&sPostPltiTrace.backend,
                                   __ATOMIC_ACQUIRE))
    {
        airportItlwmPostPltiTraceUnlock();
        return;
    }

    AirportItlwmPostPltiTraceSnapshot snapshot = {};
    snapshot.version = AIRPORT_ITLWM_POST_PLTI_TRACE_ABI_VERSION;
    snapshot.size = sizeof(snapshot);
    snapshot.captureGeneration = generation;
    snapshot.backend = backend;
    snapshot.enabled = __atomic_load_n(&sPostPltiTrace.enabled,
                                       __ATOMIC_ACQUIRE);
    snapshot.targetBound = __atomic_load_n(&sPostPltiTrace.targetController,
                                           __ATOMIC_ACQUIRE) != 0 ? 1U : 0U;
    const uint64_t activeToken = __atomic_load_n(&sPostPltiTrace.activeToken,
                                                 __ATOMIC_ACQUIRE);
    snapshot.activeEpisode =
        airportItlwmPostPltiTraceTokenGeneration(activeToken) == generation ?
        airportItlwmPostPltiTraceTokenEpisode(activeToken) : 0;
    snapshot.episodeCount = __atomic_load_n(&sPostPltiTrace.episodeCount,
                                            __ATOMIC_ACQUIRE);
    snapshot.firstSequence = buffer.firstSequence;
    snapshot.entryCount = buffer.entryCount;
    snapshot.droppedEntries = buffer.droppedEntries;
    snapshot.latestSequence = buffer.latestSequence;

    airportItlwmPostPltiTraceUnlock();
    airportItlwmPostPltiTracePublishData(
        driver, AIRPORT_ITLWM_POST_PLTI_TRACE_SNAPSHOT_PROPERTY, &snapshot,
        sizeof(snapshot));
    airportItlwmPostPltiTracePublishData(
        driver, AIRPORT_ITLWM_POST_PLTI_TRACE_BUFFER_PROPERTY, &buffer,
        sizeof(buffer));
}

static void
airportItlwmPostPltiTracePoll(AirportItlwm *driver)
{
    if (driver == nullptr)
        return;

    OSObject *control = driver->copyProperty(
        AIRPORT_ITLWM_POST_PLTI_TRACE_CONTROL_PROPERTY);
    if (control != nullptr) {
        if (OSString *command = OSDynamicCast(OSString, control))
            airportItlwmPostPltiTraceApplyControl(driver,
                                                   command->getCStringNoCopy());
        control->release();
    }
    if (__atomic_load_n(&sPostPltiTrace.enabled, __ATOMIC_ACQUIRE) != 0) {
        struct ieee80211com *actual =
            driver->fHalService != nullptr ?
            driver->fHalService->get80211Controller() : nullptr;
        const uintptr_t bound = __atomic_load_n(
            &sPostPltiTrace.targetController, __ATOMIC_ACQUIRE);
        if (reinterpret_cast<uintptr_t>(actual) != bound) {
            airportItlwmPostPltiTraceControlLock();
            if (__atomic_load_n(&sPostPltiTrace.enabled, __ATOMIC_ACQUIRE) != 0 &&
                reinterpret_cast<uintptr_t>(actual) !=
                    __atomic_load_n(&sPostPltiTrace.targetController,
                                    __ATOMIC_ACQUIRE)) {
                /* Never silently rebind a live capture to a replacement HAL. */
                __atomic_store_n(&sPostPltiTrace.enabled, 0, __ATOMIC_RELEASE);
                __atomic_store_n(&sPostPltiTrace.admitEpisodes, 0,
                                 __ATOMIC_RELEASE);
                __atomic_store_n(&sPostPltiTrace.activeToken, 0,
                                 __ATOMIC_RELEASE);
                __atomic_store_n(&sPostPltiTrace.targetController, 0,
                                 __ATOMIC_RELEASE);
                __atomic_store_n(&sPostPltiTrace.backend,
                                 kAirportItlwmPostPltiTraceBackendUnknown,
                                 __ATOMIC_RELEASE);
            }
            airportItlwmPostPltiTraceControlUnlock();
        }
    }
    airportItlwmPostPltiTracePublish(driver);
}

static void
airportItlwmPostPltiTraceInvalidate()
{
    airportItlwmPostPltiTraceControlLock();
    __atomic_store_n(&sPostPltiTrace.enabled, 0, __ATOMIC_RELEASE);
    uint32_t generation = __atomic_add_fetch(
        &sPostPltiTrace.captureGeneration, 1, __ATOMIC_RELAXED);
    if (generation == 0)
        __atomic_store_n(&sPostPltiTrace.captureGeneration, 1,
                         __ATOMIC_RELEASE);
    __atomic_store_n(&sPostPltiTrace.admitEpisodes, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&sPostPltiTrace.activeToken, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&sPostPltiTrace.targetController, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&sPostPltiTrace.backend,
                     kAirportItlwmPostPltiTraceBackendUnknown,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&sPostPltiTrace.nextEpisode, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&sPostPltiTrace.episodeCount, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&sPostPltiTrace.droppedEntries, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&sPostPltiTrace.lastControlSequence, 0,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&sPostPltiTrace.captureFirstSequence,
                     __atomic_load_n(&sPostPltiTrace.nextSequence,
                                     __ATOMIC_ACQUIRE) + 1,
                     __ATOMIC_RELEASE);
    airportItlwmPostPltiTraceControlUnlock();
}
#endif /* __IO80211_TARGET >= __MAC_26_0 */
/* END SAFE_POST_PLTI_TRACE */

IOReturn
AirportItlwm::setProperties(OSObject *properties)
{
    if (OSDictionary *dict = OSDynamicCast(OSDictionary, properties)) {
#if __IO80211_TARGET >= __MAC_26_0
        OSObject *postPltiControl = dict->getObject(
            AIRPORT_ITLWM_POST_PLTI_TRACE_CONTROL_PROPERTY);
        if (OSString *command = OSDynamicCast(OSString, postPltiControl)) {
            AirportItlwmControllerLifecycleOperationGuard lifecycle(this, false);
            if (!lifecycle.admitted())
                return kIOReturnNotReady;

            setProperty(AIRPORT_ITLWM_POST_PLTI_TRACE_CONTROL_PROPERTY,
                        command);
            airportItlwmPostPltiTraceApplyControl(this,
                                                   command->getCStringNoCopy());
            return kIOReturnSuccess;
        }
#endif
        OSObject *control = dict->getObject(AIRPORT_ITLWM_REGDIAG_CONTROL_PROPERTY);
        if (OSString *command = OSDynamicCast(OSString, control)) {
            setProperty(AIRPORT_ITLWM_REGDIAG_CONTROL_PROPERTY, command);
            airportItlwmRegDiagApplyControl(this, command->getCStringNoCopy());
            return kIOReturnSuccess;
        }
        return super::setProperties(properties);
    }

    if (OSString *command = OSDynamicCast(OSString, properties)) {
        setProperty(AIRPORT_ITLWM_REGDIAG_CONTROL_PROPERTY, command);
        airportItlwmRegDiagApplyControl(this, command->getCStringNoCopy());
        return kIOReturnSuccess;
    }

    return super::setProperties(properties);
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
    if (req == nullptr)
        return false;

    if (!isSet && req->req_type == APPLE80211_IOC_STATE)
        return true;

    if (req->req_data == nullptr)
        return false;

    return TahoeSkywalkIoctlRoutes::shouldRoute(req->req_type, isSet);
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
constexpr UInt32 kTahoeWclLinkChanged = 0xd8;
constexpr uint8_t kTahoeWclInfraInterfaceType = 1;
constexpr uint32_t kTahoeWclInvalidLinkReason = 0xff;
constexpr uint32_t kTahoeAssocStatusErrorBase = 0xe0820400;
constexpr uint32_t kTahoeAssocReasonErrorBase = 0xe0821000;
constexpr uint32_t kTahoeAssocGenericError = 0xe3ff8100;

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

static_assert(sizeof(apple80211_wcl_connect_complete_event) ==
              APPLE80211_WCL_CONNECT_COMPLETE_LEN,
              "Tahoe WCL connect-complete payload must match Apple 0xA4 layout");

static_assert(sizeof(apple80211_wcl_auth_assoc_complete_event) ==
              APPLE80211_WCL_AUTH_ASSOC_COMPLETE_LEN,
              "Tahoe WCL auth/assoc payload must match Apple 0x08 layout");

struct TahoeWclLinkChangedPayload {
    uint8_t bssid[IEEE80211_ADDR_LEN]; // 0x00
    uint8_t linkState;                 // 0x06
    uint8_t interfaceType;             // 0x07
    uint32_t reasonCode;               // 0x08
    uint32_t reserved;                 // 0x0c
} __attribute__((packed));

static_assert(sizeof(TahoeWclLinkChangedPayload) == 0x10,
              "Tahoe WCL link-changed payload must match Apple 0x10 layout");

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
    /*
     * Apple WCLNetManager::updateBss rejects BeaconMetaData.bssid == 00:00:00:00:00:00
     * before constructing WCLBSSBeacon. The OpenBSD scan cursor can carry a
     * channel with no BSS identity; publishing it as a WCL scan result gives
     * CoreWLAN a candidate with nil SSID/BSSID.
     */
    if (!TahoeScanContracts::hasRenderableBssid(ni->ni_bssid))
        return false;

    const uint16_t chanSpec = buildTahoePrimaryChanSpec(ic, ni->ni_chan);
    if (chanSpec == 0)
        return false;

    bzero(payload, sizeof(*payload));

    const uint8_t ssidLen = MIN(static_cast<uint8_t>(sizeof(payload->meta.ssid)),
                                ni->ni_esslen);
    const uint32_t ieLen = TahoeBeaconIeBuilder::buildCurrentBssIeStream(
        ni->ni_essid,
        ssidLen,
        ni->ni_dtimcount,
        ni->ni_dtimperiod,
        ni->ni_rsnie_tlv,
        ni->ni_rsnie_tlv_len,
        payload->ie,
        static_cast<uint32_t>(sizeof(payload->ie)));
    payload->meta.ieLen = ieLen;
    payload->meta.chanSpec = chanSpec;
    payload->meta.ssidLen = ssidLen;
    // AppleBCMWLANScanAdapter's BeaconMetaData builder sets bit 1 and
    // explicitly clears bit 2; bit 2 is not an SSID-present marker.
    payload->meta.flags |= TahoeScanContracts::kWclScanResultMetaFlags;
    if (payload->meta.ssidLen != 0)
        memcpy(payload->meta.ssid, ni->ni_essid, payload->meta.ssidLen);
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

static uint32_t buildTahoeWclLinkReason(unsigned int rawReason)
{
    if (rawReason == 0)
        return kTahoeWclInvalidLinkReason;

    const uint32_t mapped = static_cast<uint32_t>(rawReason - 1);
    return (mapped < 9) ? mapped : kTahoeWclInvalidLinkReason;
}

static uint32_t mapTahoeWclAssocStatus(uint32_t rawStatus)
{
    if (rawStatus == 0)
        return 0;
    if (rawStatus < 0x100)
        return rawStatus | kTahoeAssocStatusErrorBase;
    return kTahoeAssocGenericError;
}

static uint32_t mapTahoeWclAssocReason(uint32_t rawReason)
{
    if (rawReason == 0)
        return 0;
    if (rawReason < 0x45)
        return rawReason | kTahoeAssocReasonErrorBase;
    return kTahoeAssocGenericError;
}

static void buildTahoeWclAuthAssocCompletePayload(
    uint32_t rawStatus,
    uint32_t rawReason,
    apple80211_wcl_auth_assoc_complete_event *payload)
{
    if (payload == nullptr)
        return;

    payload->status = mapTahoeWclAssocStatus(rawStatus);
    payload->reason = mapTahoeWclAssocReason(rawReason);
}

static bool postTahoeWclLinkUpInd(AirportItlwm *controller,
                                  unsigned int rawReason)
{
    if (controller == nullptr || controller->fNetIf == nullptr ||
        controller->fHalService == nullptr)
        return false;

    struct ieee80211com *ic = controller->fHalService->get80211Controller();
    if (ic == nullptr || ic->ic_state != IEEE80211_S_RUN ||
        ic->ic_bss == nullptr) {
        XYLog("DEBUG %s SKIP ic=%p ic_state=%d ic_bss=%p\n", __FUNCTION__, ic,
              ic ? ic->ic_state : -1, ic ? ic->ic_bss : nullptr);
        return false;
    }

    TahoeWclLinkChangedPayload payload;
    bzero(&payload, sizeof(payload));
    IEEE80211_ADDR_COPY(payload.bssid, ic->ic_bss->ni_bssid);
    payload.linkState = 1;
    payload.interfaceType = kTahoeWclInfraInterfaceType;
    payload.reasonCode = buildTahoeWclLinkReason(rawReason);

    controller->postMessage(controller->fNetIf, kTahoeWclLinkChanged,
                            &payload, sizeof(payload), true);
    return true;
}

static void publishResolvedCountryCodeProperty(AirportItlwm *controller)
{
    if (controller == nullptr || controller->fNetIf == nullptr ||
        controller->fHalService == nullptr)
        return;

    char userOverrideCc[APPLE80211_MAX_CC_LEN];
    uint8_t resolvedCc[APPLE80211_MAX_CC_LEN];
    memset(userOverrideCc, 0, sizeof(userOverrideCc));
    PE_parse_boot_argn("itlwm_cc", userOverrideCc, sizeof(userOverrideCc));

    AirportItlwmCountryCode::selectCountryCode(
        controller->fHalService, userOverrideCc,
        controller->fHalService->getDriverInfo()->getFirmwareCountryCode(),
        controller->geo_location_cc, resolvedCc);

    OSString *value =
        OSString::withCString(reinterpret_cast<const char *>(resolvedCc));
    if (value != nullptr) {
        controller->fNetIf->setProperty(APPLE80211_REGKEY_COUNTRY_CODE, value);
        value->release();
    }
    controller->fNetIf->setProperty(
        APPLE80211_REGKEY_LOCALE,
        AirportItlwmCountryCode::localePropertyString(APPLE80211_LOCALE_FCC));
}

static bool buildTahoeWclConnectCompletePayload(
    AirportItlwm *controller,
    apple80211_wcl_connect_complete_event *payload)
{
    if (controller == nullptr || controller->fHalService == nullptr ||
        payload == nullptr)
        return false;

    struct ieee80211com *ic = controller->fHalService->get80211Controller();
    if (ic == nullptr || ic->ic_state != IEEE80211_S_RUN ||
        ic->ic_bss == nullptr) {
        XYLog("DEBUG %s SKIP ic=%p ic_state=%d ic_bss=%p\n", __FUNCTION__, ic,
              ic ? ic->ic_state : -1, ic ? ic->ic_bss : nullptr);
        return false;
    }

    bzero(payload, sizeof(*payload));
    IEEE80211_ADDR_COPY(payload->records[0].bssid, ic->ic_bss->ni_bssid);
    return true;
}

static bool postTahoeWclConnectCompleteEvent(AirportItlwm *controller)
{
    if (controller == nullptr || controller->fNetIf == nullptr)
        return false;

    apple80211_wcl_connect_complete_event payload;
    if (!buildTahoeWclConnectCompletePayload(controller, &payload))
        return false;

    publishResolvedCountryCodeProperty(controller);
    controller->postMessage(controller->fNetIf,
                            APPLE80211_M_WCL_CONNECT_COMPLETE_EVENT,
                            &payload, sizeof(payload), true);
    return true;
}

static bool postTahoeJoinAcceptedSsidChangedEvent(AirportItlwm *controller)
{
    if (controller == nullptr || controller->fNetIf == nullptr ||
        controller->fHalService == nullptr)
        return false;

    struct ieee80211com *ic = controller->fHalService->get80211Controller();
    if (ic == nullptr || ic->ic_state != IEEE80211_S_RUN ||
        ic->ic_bss == nullptr ||
        ic->ic_bss->ni_esslen > APPLE80211_MAX_SSID_LEN) {
        XYLog("DEBUG %s SKIP ic=%p ic_state=%d ic_bss=%p\n", __FUNCTION__, ic,
              ic ? ic->ic_state : -1, ic ? ic->ic_bss : nullptr);
        return false;
    }

    /*
     * AppleBCMWLANCore::handleSetSSIDEvent posts APPLE80211_M_SSID_CHANGED
     * with an 8-byte status/reason carrier before JoinAdapter handling. For
     * the accepted local join-up edge both firmware event fields are success,
     * so the Apple-identical carrier is two zero dwords. This is status data,
     * not SSID string material.
     */
    apple80211_ssid_changed_event_data payload;
    bzero(&payload, sizeof(payload));
    controller->postMessage(controller->fNetIf, APPLE80211_M_SSID_CHANGED,
                            &payload, sizeof(payload), true);
    return true;
}

static bool postTahoeJoinAcceptedBssidChangedEvent(AirportItlwm *controller,
                                                   const char *source)
{
    if (controller == nullptr || controller->fNetIf == nullptr)
        return false;

    AirportItlwmSkywalkInterface *iface =
        OSDynamicCast(AirportItlwmSkywalkInterface, controller->fNetIf);
    if (iface == nullptr)
        return false;

    return iface->publishTahoeBssidChangedFromCurrentBss(source);
}

static bool postTahoeAcceptedJoinIdentityEvents(AirportItlwm *controller,
                                                const char *source)
{
    const bool bssidPublished =
        postTahoeJoinAcceptedBssidChangedEvent(controller, source);
    const bool ssidPublished = postTahoeJoinAcceptedSsidChangedEvent(controller);
    return bssidPublished || ssidPublished;
}

} // namespace

#if __IO80211_TARGET >= __MAC_26_0
bool AirportItlwm::
publishTahoeAcceptedJoinIdentityEvents(const char *source)
{
    return postTahoeAcceptedJoinIdentityEvents(this, source);
}
#endif

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
}

static void
postTahoeDriverAvailabilityTransition(
    AirportItlwm *controller,
    TahoeDriverAvailabilityContracts::Transition transition)
{
    if (controller == NULL || controller->fNetIf == NULL)
        return;

    // Current 25C56 AppleBCMWLANCore has three distinct normal-lifecycle
    // producers. bootChipImage, powerOff, and powerOn each send an exact 0xf8
    // carrier through IO80211Controller::postMessage; they do not share a
    // boolean-only payload shape and signalDriverReady is not this producer.
    apple80211_driver_available_data data =
        TahoeDriverAvailabilityContracts::build(transition);

    controller->postMessage(controller->fNetIf, APPLE80211_M_DRIVER_AVAILABLE,
                            &data, sizeof(data), true);
}

static void
applyTahoeInterfaceReadyEdge(AirportItlwm *controller, bool ready)
{
    if (controller == NULL || controller->fNetIf == NULL)
        return;

    // signalDriverReady is separate from the DRIVER_AVAILABLE carrier. The
    // recovered bootChipImage caller order before its later 0x37 post is:
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
        if (ret != kIOReturnSuccess)
            XYLog("DEBUG %s FAIL: ready-edge ret=0x%x fNetIf=%p\n",
                  __FUNCTION__, ret, controller->fNetIf);
    } else {
        IOReturn ret = controller->fNetIf->interfaceAdvisoryEnable(false);
        if (ret != kIOReturnSuccess)
            XYLog("DEBUG %s FAIL: advisory-edge ret=0x%x fNetIf=%p\n",
                  __FUNCTION__, ret, controller->fNetIf);
    }
}

static void
publishTahoeBootReadyState(AirportItlwm *controller)
{
    applyTahoeInterfaceReadyEdge(controller, true);
    setCoreWiFiDriverReadyProperty(controller, true);
    postTahoeDriverAvailabilityTransition(
        controller, TahoeDriverAvailabilityContracts::Transition::BootReady);
}

static void
publishTahoeBootFailureState(AirportItlwm *controller)
{
    // bootChipImage's failure tail drives the hidden advisory slot and
    // signalDriverReady, but does not manufacture a normal power-off bulletin.
    applyTahoeInterfaceReadyEdge(controller, false);
    setCoreWiFiDriverReadyProperty(controller, false);
}

// Apple's bootChipImage is triggered asynchronously by AppleBCMWLANUserClient.
// The thread_call handler routes through the command gate for serialization,
// matching the framework's expected execution context.
static IOReturn
performTahoeBootChipImageGated(OSObject *owner, void *, void *, void *, void *)
{
    AirportItlwm *self = static_cast<AirportItlwm *>(owner);
    // The thread-call handler can already be queued when stop begins.  Check
    // again after entering the command gate so a stale invocation never
    // reaches fHalService while the boot-callback drain owns it.
    if (self == nullptr || !self->tahoeBootThreadCallLive())
        return kIOReturnAborted;
    self->performTahoeBootChipImage();
    return kIOReturnSuccess;
}

static void
handleTahoeBootChipImage(thread_call_param_t param0, thread_call_param_t)
{
    AirportItlwm *self = (AirportItlwm *)param0;
    if (self == nullptr)
        return;

    // beginTahoeBootThreadCall() records this callback owner before its first
    // liveness decision. The one schedule-owned retain remains held through
    // the literal final release below, so no stop/free path can reclaim this
    // raw thread-call param while the handler is still returning.
    if (self->beginTahoeBootThreadCall()) {
        IOCommandGate *gate = self->getCommandGate();
        if (gate && self->tahoeBootThreadCallLive())
            gate->runAction(performTahoeBootChipImageGated);
    }
    self->completeTahoeBootThreadCall();
    self->releaseTahoeBootThreadCallRetain();
}

void AirportItlwm::performTahoeBootChipImage()
{
    OSBitOrAtomic(kAirportItlwmPmBootInProgressBit, &pmPowerStateFlags);
    setLinkStatus(kIONetworkLinkValid);
    if (TAILQ_EMPTY(&fHalService->get80211Controller()->ic_ess))
        fHalService->get80211Controller()->ic_flags |= IEEE80211_F_AUTO_JOIN;
    power_state = kWiFiPowerOn;
    const IOReturn enableResult = enableAdapter(NULL);
    if (enableResult == kIOReturnSuccess) {
        // setupDriver() in the reference consumes its bootstrap POWER cache
        // exactly once after adapter setup and before bootChipImage's normal
        // terminal tail.  Clear the local cache before replaying it: a
        // re-entrant request must take the ordinary POWER path, rather than
        // being folded into this completed bootstrap transaction.
        const bool replayBootstrapPower = tahoeBootstrapPowerPending;
        const uint8_t cachedPowerState = tahoeRequestedPowerState;
        tahoeBootstrapPowerPending = false;
        tahoeBootstrapPowerWindowOpen = false;
        if (replayBootstrapPower) {
#if __IO80211_TARGET >= __MAC_26_0
            handlePowerStateChange(cachedPowerState, NULL);
#else
            handlePowerStateChange(cachedPowerState, bsdInterface);
#endif
        }

        // The normal boot tail remains unconditional, including after a
        // cached OFF.  In the reference this publishes BootReady (0x37) after
        // the POWER transition; it is not a normal radio-on notification.
        OSBitAndAtomic(~static_cast<UInt32>(kAirportItlwmPmBootInProgressBit),
                       &pmPowerStateFlags);
        publishTahoeBootReadyState(this);
    } else {
        power_state = kWiFiPowerOff;
        OSBitAndAtomic(~static_cast<UInt32>(kAirportItlwmPmBootInProgressBit),
                       &pmPowerStateFlags);
        publishTahoeBootFailureState(this);
    }
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
    controller->scheduleTahoeBootThreadCall();
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

// The Skywalk nexus delivers packets from the BSD ifnet TX path as
// IOSkywalkPacket objects. We extract the frame data from each packet's
// packet data API, copy it into an mbuf, and send it through the existing
// outputPacket path which enqueues to the hardware via if_snd.
//
// Return type is unsigned int (not IOReturn/int) to match kernel ABI.
// Kernel symbol uses mangled 'j' (unsigned int), not 'i' (int).
// Packet param is IOSkywalkPacket * const * (PKP mangling) — the array
// entries are const, but the packets themselves are mutable.
static bool
skywalkTxStageCompletionPacket(AirportItlwm *that, IOSkywalkPacket *pkt)
{
    if (that == nullptr || that->fTxCompletionPendingLock == nullptr ||
        pkt == nullptr)
        return false;

    IOLockLock(that->fTxCompletionPendingLock);
    bool staged = false;
    if (that->fTxCompletionPendingCount <
        kAirportItlwmTxCompletionPendingCapacity) {
        that->fTxCompletionPendingPackets[that->fTxCompletionPendingTail] =
            pkt;
        that->fTxCompletionPendingTail =
            (that->fTxCompletionPendingTail + 1) %
            kAirportItlwmTxCompletionPendingCapacity;
        that->fTxCompletionPendingCount++;
        staged = true;
    }
    IOLockUnlock(that->fTxCompletionPendingLock);
    return staged;
}

static IOSkywalkPacket *
skywalkTxPopCompletionPacket(AirportItlwm *that)
{
    if (that == nullptr || that->fTxCompletionPendingLock == nullptr)
        return nullptr;

    IOLockLock(that->fTxCompletionPendingLock);
    IOSkywalkPacket *pkt = nullptr;
    if (that->fTxCompletionPendingCount != 0) {
        pkt = that->fTxCompletionPendingPackets[
            that->fTxCompletionPendingHead];
        that->fTxCompletionPendingPackets[that->fTxCompletionPendingHead] =
            nullptr;
        that->fTxCompletionPendingHead =
            (that->fTxCompletionPendingHead + 1) %
            kAirportItlwmTxCompletionPendingCapacity;
        that->fTxCompletionPendingCount--;
    }
    IOLockUnlock(that->fTxCompletionPendingLock);
    return pkt;
}

static void
skywalkTxReleaseCompletedPacket(AirportItlwm *that, IOSkywalkPacket *pkt)
{
    if (pkt == nullptr)
        return;

    if (that != nullptr && that->fTxCompQueue != nullptr) {
        pkt->completeWithQueue(that->fTxCompQueue,
                               kIOSkywalkPacketDirectionTx, 0);
        return;
    }
    if (that != nullptr && that->fTxPool != nullptr)
        that->fTxPool->deallocatePacket(pkt);
}

static void
skywalkTxDrainCompletionPackets(AirportItlwm *that)
{
    if (that == nullptr)
        return;

    while (IOSkywalkPacket *pkt = skywalkTxPopCompletionPacket(that))
        skywalkTxReleaseCompletedPacket(that, pkt);
}

static unsigned int
skywalkTxAction(OSObject *owner, IOSkywalkTxSubmissionQueue *queue,
                IOSkywalkPacket * const *packets, UInt32 count, void *refCon)
{
    AirportItlwm *that = OSDynamicCast(AirportItlwm, owner);
    if (!that || !packets) {
        sRT.txPktDrop += count;
        return count;
    }

    sRT.txCbCnt++;
    UInt32 consumed = 0;
    UInt32 delivered = 0;
    UInt32 deliveredBytes = 0;
    UInt32 stagedCompletions = 0;
    for (UInt32 i = 0; i < count; i++) {
        IOSkywalkPacket *pkt = packets[i];
        if (!pkt) {
            consumed++;
            sRT.txPktDrop++;
            continue;
        }
        if (!skywalkTxStageCompletionPacket(that, pkt)) {
            sRT.txPktDrop++;
            if (sRT.txCbCnt <= 3)
                XYLog("skywalkTxAction: completion stage failed pkt %u/%u "
                      "pending=%u\n",
                      i, count, that->fTxCompletionPendingCount);
            break;
        }
        stagedCompletions++;
        consumed++;

        // Confirm that prepareWithQueue populated at least one packet buffer.
        IOSkywalkPacketBuffer *bufs[1] = { NULL };
        UInt32 nBufs = pkt->getPacketBuffers(bufs, 1);
        if (nBufs == 0 || !bufs[0]) {
            sRT.txPktDrop++;
            if (sRT.txCbCnt <= 3)
                XYLog("skywalkTxAction: pkt %u/%u no buffers\n", i, count);
            continue;
        }

        void *objAddr = pkt->getDataVirtualAddress();
        UInt16 dataOff = pkt->getDataOffset();
        UInt32 dataLen = pkt->getDataLength();

        if (!objAddr || dataLen == 0 ||
            dataOff > SKYWALK_BUF_SIZE || dataLen > SKYWALK_BUF_SIZE - dataOff) {
            sRT.txPktDrop++;
            continue;
        }
        bool txEapol = airportItlwmEthernetBufferIsEapol(
            static_cast<const uint8_t *>(objAddr) + dataOff, dataLen);
        // Allocate an mbuf with packet header and copy the Ethernet frame
        mbuf_t m = NULL;
        if (mbuf_allocpacket(MBUF_DONTWAIT, dataLen, NULL, &m) != 0) {
            sRT.txPktDrop++;
            if (txEapol) {
                airportItlwmRegDiagRecordData(kAirportItlwmRegDiagPathTx,
                                              dataLen, true,
                                              kIOReturnOutputDropped);
                airportItlwmLogEapolProbe(kAirportItlwmRegDiagPathTx,
                                          "mbuf-alloc", dataLen,
                                          kIOReturnOutputDropped);
            }
            continue;
        }

        if (mbuf_copyback(m, 0, dataLen, (uint8_t *)objAddr + dataOff, MBUF_DONTWAIT) != 0) {
            mbuf_freem(m);
            sRT.txPktDrop++;
            if (txEapol) {
                airportItlwmRegDiagRecordData(kAirportItlwmRegDiagPathTx,
                                              dataLen, true,
                                              kIOReturnOutputDropped);
                airportItlwmLogEapolProbe(kAirportItlwmRegDiagPathTx,
                                          "copyback", dataLen,
                                          kIOReturnOutputDropped);
            }
            continue;
        }

        IOReturn outRet = that->outputPacket(m, NULL);
        if (txEapol)
            airportItlwmLogEapolProbe(kAirportItlwmRegDiagPathTx, "output",
                                      dataLen, outRet);
        if (outRet == kIOReturnOutputSuccess) {
            delivered++;
            deliveredBytes += dataLen;
        } else {
            sRT.txPktDrop++;
        }
    }

    sRT.txPktSent += delivered;
    if (delivered != 0 && that->fNetIf != nullptr) {
        apple80211_wme_ac ac = { APPLE80211_WME_AC_BE };
        that->fNetIf->recordOutputPacket(ac, static_cast<int>(delivered),
                                         static_cast<int>(deliveredBytes));
    }
    if (stagedCompletions != 0 && that->fTxCompQueue != nullptr) {
        IOReturn ret = that->fTxCompQueue->requestEnqueue(nullptr, 0);
        if (ret != kIOReturnSuccess && sRT.txCbCnt <= 3)
            XYLog("skywalkTxAction: tx completion requestEnqueue failed "
                  "0x%x pending=%u\n",
                  ret, that->fTxCompletionPendingCount);
    }
    return consumed;
}

// Skywalk RX completion producer action.  AppleBCMWLAN stages prepared RX
// packets in its queue owner, then IOSkywalkRxCompletionQueue::requestEnqueue()
// calls this action to fill the Skywalk-provided packet array.  The IO80211
// input handoff happens here, before the base RX completion queue publishes the
// produced packets to the networking side.
// Return type is unsigned int to match kernel ABI (see TX callback comment).
static void
skywalkRxReleasePreparedPacket(AirportItlwm *that, IOSkywalkPacket *rxPkt)
{
    if (rxPkt == nullptr)
        return;

    rxPkt->completeWithQueue(nullptr, kIOSkywalkPacketDirectionRx, 0);
    if (that != nullptr && that->fRxPool != nullptr)
        that->fRxPool->deallocatePacket(rxPkt);
}

static bool
skywalkRxBuildInputTag(packet_info_tag *tag)
{
    if (tag == nullptr)
        return false;

    bzero(tag, sizeof(*tag));
    tag->tid = 0;
    tag->service_class = 4;
    return true;
}

static bool
skywalkRxStagePendingPacket(AirportItlwm *that, IOSkywalkPacket *rxPkt,
                            const packet_info_tag *tag, UInt32 length)
{
    if (that == nullptr || that->fRxPendingLock == nullptr ||
        rxPkt == nullptr || tag == nullptr)
        return false;

    IOLockLock(that->fRxPendingLock);
    bool staged = false;
    if (that->fRxPendingCount < kAirportItlwmRxPendingCapacity) {
        that->fRxPendingPackets[that->fRxPendingTail] = rxPkt;
        that->fRxPendingTags[that->fRxPendingTail] = *tag;
        that->fRxPendingLengths[that->fRxPendingTail] = length;
        that->fRxPendingTail =
            (that->fRxPendingTail + 1) % kAirportItlwmRxPendingCapacity;
        that->fRxPendingCount++;
        staged = true;
    }
    IOLockUnlock(that->fRxPendingLock);
    return staged;
}

static IOSkywalkPacket *
skywalkRxPopPendingPacket(AirportItlwm *that, packet_info_tag *tag,
                          UInt32 *length)
{
    if (that == nullptr || that->fRxPendingLock == nullptr)
        return nullptr;

    IOLockLock(that->fRxPendingLock);
    IOSkywalkPacket *rxPkt = nullptr;
    if (that->fRxPendingCount != 0) {
        const UInt32 index = that->fRxPendingHead;
        rxPkt = that->fRxPendingPackets[index];
        if (tag != nullptr)
            *tag = that->fRxPendingTags[index];
        if (length != nullptr)
            *length = that->fRxPendingLengths[index];
        that->fRxPendingPackets[index] = nullptr;
        bzero(&that->fRxPendingTags[index], sizeof(that->fRxPendingTags[index]));
        that->fRxPendingLengths[index] = 0;
        that->fRxPendingHead =
            (that->fRxPendingHead + 1) % kAirportItlwmRxPendingCapacity;
        that->fRxPendingCount--;
    }
    IOLockUnlock(that->fRxPendingLock);
    return rxPkt;
}

static bool
skywalkRxRemovePendingPacket(AirportItlwm *that, IOSkywalkPacket *target)
{
    if (that == nullptr || that->fRxPendingLock == nullptr || target == nullptr)
        return false;

    IOLockLock(that->fRxPendingLock);
    const UInt32 count = that->fRxPendingCount;
    UInt32 read = that->fRxPendingHead;
    UInt32 write = that->fRxPendingHead;
    bool removed = false;

    for (UInt32 i = 0; i < count; i++) {
        IOSkywalkPacket *pkt = that->fRxPendingPackets[read];
        packet_info_tag tag = that->fRxPendingTags[read];
        UInt32 length = that->fRxPendingLengths[read];
        that->fRxPendingPackets[read] = nullptr;
        bzero(&that->fRxPendingTags[read], sizeof(that->fRxPendingTags[read]));
        that->fRxPendingLengths[read] = 0;
        read = (read + 1) % kAirportItlwmRxPendingCapacity;
        if (!removed && pkt == target) {
            removed = true;
            continue;
        }
        that->fRxPendingPackets[write] = pkt;
        that->fRxPendingTags[write] = tag;
        that->fRxPendingLengths[write] = length;
        write = (write + 1) % kAirportItlwmRxPendingCapacity;
    }

    that->fRxPendingTail = write;
    if (removed) {
        that->fRxPendingCount = count - 1;
        bzero(&that->fRxPendingTags[write], sizeof(that->fRxPendingTags[write]));
        that->fRxPendingLengths[write] = 0;
    }
    IOLockUnlock(that->fRxPendingLock);
    return removed;
}

static void
skywalkRxDrainPendingPackets(AirportItlwm *that)
{
    if (that == nullptr)
        return;

    while (IOSkywalkPacket *rxPkt = skywalkRxPopPendingPacket(that, nullptr,
                                                             nullptr))
        skywalkRxReleasePreparedPacket(that, rxPkt);
}

static unsigned int
skywalkRxAction(OSObject *owner, IOSkywalkRxCompletionQueue *queue,
                IOSkywalkPacket **packets, UInt32 count, void *refCon)
{
    sRT.rxCbCnt++;
    AirportItlwm *that = OSDynamicCast(AirportItlwm, owner);
    if (that == nullptr || that->fNetIf == nullptr || packets == nullptr)
        return 0;

    UInt32 produced = 0;
    UInt32 producedBytes = 0;
    for (UInt32 i = 0; i < count; i++) {
        packet_info_tag tag;
        UInt32 stagedLength = 0;
        IOSkywalkPacket *pkt = skywalkRxPopPendingPacket(that, &tag,
                                                        &stagedLength);
        if (pkt == nullptr)
            break;

        void *base = pkt->getDataVirtualAddress();
        UInt16 dataOffset = pkt->getDataOffset();
        UInt32 dataLength = pkt->getDataLength();
        if (base == nullptr || dataLength < sizeof(ether_header) ||
            dataOffset > SKYWALK_BUF_SIZE ||
            dataLength > SKYWALK_BUF_SIZE - dataOffset) {
            skywalkRxReleasePreparedPacket(that, pkt);
            continue;
        }

        ether_header *eh = reinterpret_cast<ether_header *>(
            static_cast<uint8_t *>(base) + dataOffset);
        IOReturn ret = that->fNetIf->inputPacket(
            reinterpret_cast<IO80211NetworkPacket *>(pkt),
            &tag,
            eh,
            nullptr,
            false);

        if (airportItlwmEthernetBufferIsEapol(eh, dataLength)) {
            airportItlwmLogEapolProbe(kAirportItlwmRegDiagPathRx,
                                      "producer-input", dataLength, ret);
        }

        packets[produced++] = pkt;
        producedBytes += dataLength != 0 ? dataLength : stagedLength;
    }
    if (produced != 0) {
        that->fNetIf->recordInputPacket(static_cast<int>(produced),
                                        static_cast<int>(producedBytes));
        that->fNetIf->updateRxCounter(produced);
    }
    (void)queue;
    (void)refCon;
    return produced;
}

static UInt32
skywalkTxCompletionAction(OSObject *owner, IOSkywalkTxCompletionQueue *,
                          IOSkywalkPacket **packets, UInt32 count, void *)
{
    AirportItlwm *that = OSDynamicCast(AirportItlwm, owner);
    if (that == nullptr || packets == nullptr)
        return 0;

    UInt32 produced = 0;
    for (; produced < count; produced++) {
        IOSkywalkPacket *pkt = skywalkTxPopCompletionPacket(that);
        if (pkt == nullptr)
            break;
        packets[produced] = pkt;
    }
    return produced;
}

#if __IO80211_TARGET >= __MAC_26_0
static void
skywalkRxReturnPreparedPacket(AirportItlwm *that, IOSkywalkPacket *rxPkt)
{
    skywalkRxReleasePreparedPacket(that, rxPkt);
}

// Skywalk RX input handler — called from _if_input() on the Tahoe path.
// Converts an mbuf into a prepared IOSkywalkPacket, stages it in the local
// RX producer queue, and rings IOSkywalkRxCompletionQueue::requestEnqueue().
static int
skywalkRxInput(struct _ifnet *ifp, mbuf_t m)
{
    if (ifp == nullptr) {
        if (m != nullptr)
            mbuf_freem(m);
        return ENXIO;
    }

    AirportItlwm *that = OSDynamicCast(AirportItlwm, ifp->controller);
    AirportItlwmControllerLifecycleOperationGuard lifecycle(that, false);
    if (!lifecycle.admitted() || !that->fRxPool || !that->fRxQueue) {
        if (m != nullptr)
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
    bool diagEapol = airportItlwmRegDiagMbufIsEapol(m, nullptr);
    if (diagEapol && airportItlwmRegDiagShouldBlock(kAirportItlwmRegDiagBlockEapolRx)) {
        airportItlwmRegDiagRecordBlock(kAirportItlwmRegDiagBlockEapolRx,
                                       kAirportItlwmRegDiagPathRx, diagLength);
        airportItlwmRegDiagRecordData(kAirportItlwmRegDiagPathRx, diagLength,
                                      true, static_cast<IOReturn>(EIO));
        airportItlwmLogEapolProbe(kAirportItlwmRegDiagPathRx, "block-eapol-rx",
                                  diagLength, static_cast<IOReturn>(EIO));
        mbuf_freem(m);
        return 0;
    }
    if (airportItlwmRegDiagShouldBlock(kAirportItlwmRegDiagBlockRx)) {
        airportItlwmRegDiagRecordBlock(kAirportItlwmRegDiagBlockRx,
                                       kAirportItlwmRegDiagPathRx, diagLength);
        airportItlwmRegDiagRecordData(kAirportItlwmRegDiagPathRx, diagLength,
                                      diagEapol, static_cast<IOReturn>(EIO));
        if (diagEapol)
            airportItlwmLogEapolProbe(kAirportItlwmRegDiagPathRx, "block-rx",
                                      diagLength, static_cast<IOReturn>(EIO));
        mbuf_freem(m);
        return 0;
    }

    // Allocate an IOSkywalkPacket from the RX pool
    IOSkywalkPacket *rxPkt = NULL;
    IOReturn allocRet = that->fRxPool->allocatePacket(1, &rxPkt, 0);
    if (allocRet != kIOReturnSuccess || !rxPkt) {
        sRT.rxAllocFail++;
        if (sRT.rxAllocFail <= 5)
            XYLog("skywalkRxInput: allocatePacket failed 0x%x (drop #%u)\n",
                  allocRet, sRT.rxAllocFail);
        airportItlwmRegDiagRecordData(kAirportItlwmRegDiagPathRx, diagLength,
                                      diagEapol,
                                      allocRet != kIOReturnSuccess ? allocRet :
                                      static_cast<IOReturn>(ENOMEM));
        if (diagEapol)
            airportItlwmLogEapolProbe(kAirportItlwmRegDiagPathRx, "alloc",
                                      diagLength,
                                      allocRet != kIOReturnSuccess ? allocRet :
                                      static_cast<IOReturn>(ENOMEM));
        mbuf_freem(m);
        return ENOMEM;
    }

    IOReturn prepRet = rxPkt->prepareWithQueue(nullptr,
                                               kIOSkywalkPacketDirectionRx, 0);
    if (prepRet != kIOReturnSuccess) {
        skywalkRxReturnPreparedPacket(that, rxPkt);
        airportItlwmRegDiagRecordData(kAirportItlwmRegDiagPathRx, diagLength,
                                      diagEapol, prepRet);
        if (diagEapol)
            airportItlwmLogEapolProbe(kAirportItlwmRegDiagPathRx, "prepare",
                                      diagLength, prepRet);
        mbuf_freem(m);
        return EIO;
    }

    // Confirm that prepareWithQueue populated at least one packet buffer.
    IOSkywalkPacketBuffer *bufs[1] = { NULL };
    UInt32 nBufs = rxPkt->getPacketBuffers(bufs, 1);
    if (nBufs == 0 || !bufs[0]) {
        skywalkRxReturnPreparedPacket(that, rxPkt);
        airportItlwmRegDiagRecordData(kAirportItlwmRegDiagPathRx, diagLength,
                                      diagEapol, static_cast<IOReturn>(ENOMEM));
        if (diagEapol)
            airportItlwmLogEapolProbe(kAirportItlwmRegDiagPathRx, "buffers",
                                      diagLength, static_cast<IOReturn>(ENOMEM));
        mbuf_freem(m);
        return ENOMEM;
    }

    void *objAddr = rxPkt->getDataVirtualAddress();
    if (!objAddr || len > SKYWALK_BUF_SIZE) {
        skywalkRxReturnPreparedPacket(that, rxPkt);
        airportItlwmRegDiagRecordData(kAirportItlwmRegDiagPathRx, diagLength,
                                      diagEapol, static_cast<IOReturn>(EMSGSIZE));
        if (diagEapol)
            airportItlwmLogEapolProbe(kAirportItlwmRegDiagPathRx, "capacity",
                                      diagLength, static_cast<IOReturn>(EMSGSIZE));
        mbuf_freem(m);
        return EMSGSIZE;
    }

    // Copy the mbuf data into the Skywalk packet buffer
    errno_t copyRet = mbuf_copydata(m, 0, len, objAddr);
    if (copyRet != 0) {
        skywalkRxReturnPreparedPacket(that, rxPkt);
        airportItlwmRegDiagRecordData(kAirportItlwmRegDiagPathRx, diagLength,
                                      diagEapol,
                                      static_cast<IOReturn>(copyRet));
        if (diagEapol)
            airportItlwmLogEapolProbe(kAirportItlwmRegDiagPathRx, "copy",
                                      diagLength, static_cast<IOReturn>(copyRet));
        mbuf_freem(m);
        return EIO;
    }
    IOReturn dataRet = rxPkt->setDataOffsetAndLength(0, static_cast<UInt32>(len));
    if (dataRet != kIOReturnSuccess) {
        skywalkRxReturnPreparedPacket(that, rxPkt);
        airportItlwmRegDiagRecordData(kAirportItlwmRegDiagPathRx, diagLength,
                                      diagEapol, dataRet);
        if (diagEapol)
            airportItlwmLogEapolProbe(kAirportItlwmRegDiagPathRx, "length",
                                      diagLength, dataRet);
        mbuf_freem(m);
        return EIO;
    }

    packet_info_tag tag;
    if (!skywalkRxBuildInputTag(&tag) ||
        !skywalkRxStagePendingPacket(that, rxPkt, &tag,
                                     static_cast<UInt32>(len))) {
        skywalkRxReturnPreparedPacket(that, rxPkt);
        sRT.rxEnqFail++;
        if (sRT.rxEnqFail <= 5)
            XYLog("skywalkRxInput: pending stage failed (drop #%u) "
                  "pending=%u RXenabled=%d RXwl=%p\n",
                  sRT.rxEnqFail, that->fRxPendingCount,
                  that->fRxQueue->isEnabled() ? 1 : 0,
                  that->fRxQueue->getWorkLoop());
        airportItlwmRegDiagRecordData(kAirportItlwmRegDiagPathRx, diagLength,
                                      diagEapol,
                                      static_cast<IOReturn>(ENOSPC));
        if (diagEapol)
            airportItlwmLogEapolProbe(kAirportItlwmRegDiagPathRx,
                                      "pending-full", diagLength,
                                      static_cast<IOReturn>(ENOSPC));
        mbuf_freem(m);
        return ENOSPC;
    }

    IOReturn ret = that->fRxQueue->requestEnqueue(nullptr, 0);

    mbuf_freem(m);

    if (ret != kIOReturnSuccess) {
        sRT.rxEnqFail++;
        if (sRT.rxEnqFail <= 5)
            XYLog("skywalkRxInput: requestEnqueue failed 0x%x (fail #%u) "
                  "pending=%u RXenabled=%d RXwl=%p\n",
                  ret, sRT.rxEnqFail, that->fRxPendingCount,
                  that->fRxQueue->isEnabled() ? 1 : 0,
                  that->fRxQueue->getWorkLoop());
        airportItlwmRegDiagRecordData(kAirportItlwmRegDiagPathRx, diagLength,
                                      diagEapol, ret);
        if (diagEapol)
            airportItlwmLogEapolProbe(kAirportItlwmRegDiagPathRx,
                                      "request-enqueue", diagLength, ret);
        if (skywalkRxRemovePendingPacket(that, rxPkt))
            skywalkRxReturnPreparedPacket(that, rxPkt);
        return EIO;
    }

    sRT.rxPktOK++;
    airportItlwmRegDiagRecordData(kAirportItlwmRegDiagPathRx, diagLength,
                                  diagEapol, kIOReturnSuccess);
    if (diagEapol)
        airportItlwmLogEapolProbe(kAirportItlwmRegDiagPathRx, "request-ok",
                                  diagLength, kIOReturnSuccess);

    return 0;
}
#endif /* __IO80211_TARGET >= __MAC_26_0 */

void AirportItlwm::scheduleTahoeBootThreadCall()
{
    if (fTahoeBootCallLock == nullptr)
        return;

    IOLockLock(fTahoeBootCallLock);
    if (!fTahoeBootStopping && !fTahoeBootScheduled &&
        !fTahoeBootCallActive &&
        tahoeBootThreadCall != nullptr) {
        // fTahoeBootScheduled is the lifetime one-shot latch.  The separate
        // active latch covers exactly the submitted invocation, including the
        // interval in which it has started but is blocked in runAction().
        // That distinction lets the public cancel API safely replace the
        // private/aux-kext-unavailable synchronous-cancel API during teardown.
        fTahoeBootScheduled = true;
        fTahoeBootCallActive = true;
        // The thread call stores only a raw `this` parameter. Retain before
        // submit and release only at one of the two terminal edges: successful
        // pending cancellation below, or the callback's literal last action.
        fTahoeBootCallRetained = true;
        retain();
        thread_call_enter(tahoeBootThreadCall);
    }
    IOLockUnlock(fTahoeBootCallLock);
}

bool AirportItlwm::beginTahoeBootThreadCall()
{
    IOLock *lock = fTahoeBootCallLock;
    if (lock == nullptr)
        return false;

    IOLockLock(lock);
    // Set this before checking Stopping: a callback which was dispatched just
    // before teardown must never wait for itself if a framework callback
    // reaches stopTahoeBootThreadCallAndDrain() re-entrantly.
    if (fTahoeBootCallActive && tahoeBootThreadCall != nullptr)
        fTahoeBootCallOwner = current_thread();
    const bool live = !fTahoeBootStopping && fTahoeBootCallActive &&
        tahoeBootThreadCall != nullptr;
    IOLockUnlock(lock);
    return live;
}

bool AirportItlwm::tahoeBootThreadCallLive()
{
    bool live = false;

    if (fTahoeBootCallLock == nullptr)
        return false;

    IOLockLock(fTahoeBootCallLock);
    live = !fTahoeBootStopping && fTahoeBootCallActive &&
        tahoeBootThreadCall != nullptr;
    IOLockUnlock(fTahoeBootCallLock);
    return live;
}

void AirportItlwm::completeTahoeBootThreadCall()
{
    IOLock *lock = fTahoeBootCallLock;
    if (lock == nullptr)
        return;

    IOLockLock(lock);
    fTahoeBootCallOwner = nullptr;
    fTahoeBootCallActive = false;
    // stopTahoeBootThreadCallAndDrain() sleeps only after cancellation could
    // not remove the invocation.  Wake it after every callback exit, whether
    // the callback reached runAction() or observed fTahoeBootStopping first.
    IOLockWakeup(lock, &fTahoeBootCallActive, false);
    IOLockUnlock(lock);
}

void AirportItlwm::releaseTahoeBootThreadCallRetain()
{
    IOLock *lock = fTahoeBootCallLock;
    bool dropRetain = false;
    if (lock != nullptr) {
        IOLockLock(lock);
        if (fTahoeBootCallRetained) {
            fTahoeBootCallRetained = false;
            dropRetain = true;
        }
        IOLockUnlock(lock);
    }

    // This is intentionally the last use of `this` on the callback path.
    // The matching schedule retain makes it safe even if teardown has already
    // observed fTahoeBootCallActive=false and released the thread_call.
    if (dropRetain)
        release();
}

void AirportItlwm::stopTahoeBootThreadCallAndDrain()
{
    IOLock *lock = fTahoeBootCallLock;
    if (lock == nullptr)
        return;

    IOLockLock(lock);
    // beginLifecycleDrain()/stopHalAndDrainClaimed() supplies the normal sole
    // owner. Keep this local claim too: a second caller must not snapshot and
    // free the same thread_call after the first has begun draining it.
    if (fTahoeBootStopping) {
        IOLockUnlock(lock);
        return;
    }
    if (fTahoeBootCallOwner == current_thread()) {
        IOLockUnlock(lock);
        panic("%s: Tahoe boot callback attempted self-drain", __FUNCTION__);
    }
    fTahoeBootStopping = true;
    thread_call_t call = tahoeBootThreadCall;
    IOLockUnlock(lock);
    if (call == nullptr)
        return;

    // The private synchronous-cancel API is present in some kernel images but
    // is not an auxiliary-kext-linkable contract on the 25C56 lab target. This
    // call is
    // submitted at most once and never re-entered.  Therefore a successful
    // cancel proves that no callback can still hold `this`; otherwise the
    // callback itself clears and wakes the active latch after its final gate
    // check/runAction returns.  Do not replace this with cancel()+free(): a
    // false cancellation result may mean the callback is already running.
    const bool canceled = thread_call_cancel(call);
    IOLockLock(lock);
    if (canceled && tahoeBootThreadCall == call) {
        fTahoeBootCallActive = false;
        IOLockWakeup(lock, &fTahoeBootCallActive, false);
    }
    while (tahoeBootThreadCall == call && fTahoeBootCallActive)
        IOLockSleep(lock, &fTahoeBootCallActive, THREAD_UNINT);
    IOLockUnlock(lock);

    if (!thread_call_free(call)) {
        // A failed free leaves ownership of a call that can still reference
        // this controller.  Continuing into controller destruction would turn
        // that contract violation into a delayed use-after-free.
        panic("%s: tahoe boot thread_call_free failed", __FUNCTION__);
    }

    IOLockLock(lock);
    if (tahoeBootThreadCall == call)
        tahoeBootThreadCall = nullptr;
    IOLockUnlock(lock);

    // A cancelled pending call never reaches the callback's final release.
    // Drop exactly its matching schedule retain only after call ownership is
    // unpublished; the callback owns the retain on every cancel-false path.
    if (canceled)
        releaseTahoeBootThreadCallRetain();
}

bool AirportItlwm::beginLifecycleOperation()
{
    IOSimpleLock *lock = fLifecycleAdmissionLock;
    if (lock == NULL)
        return false;

    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(lock);
    // External selector admission begins only after the controller has
    // completed start(). Internal startup sources have their own setup fence.
    const bool admitted = fLifecyclePhase == kAirportItlwmLifecycleLive;
    if (admitted)
        ++fLifecycleOperationUsers;
    IOSimpleLockUnlockEnableInterrupt(lock, irq);
    return admitted;
}

bool AirportItlwm::beginLifecycleInternalOperation()
{
    IOSimpleLock *lock = fLifecycleAdmissionLock;
    if (lock == NULL)
        return false;

    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(lock);
    // A small, explicitly-audited set of framework bootstrap callbacks can
    // run during Starting. They share the same user count as externally
    // visible work, so Draining waits both classes before HAL teardown.
    const bool admitted = fLifecyclePhase == kAirportItlwmLifecycleStarting ||
        fLifecyclePhase == kAirportItlwmLifecycleLive;
    if (admitted)
        ++fLifecycleOperationUsers;
    IOSimpleLockUnlockEnableInterrupt(lock, irq);
    return admitted;
}

void AirportItlwm::endLifecycleOperation()
{
    IOSimpleLock *lock = fLifecycleAdmissionLock;
    if (lock == NULL)
        return;

    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(lock);
    if (fLifecycleOperationUsers != 0)
        --fLifecycleOperationUsers;
    IOSimpleLockUnlockEnableInterrupt(lock, irq);
}

bool AirportItlwm::markLifecycleLive()
{
    IOSimpleLock *lock = fLifecycleAdmissionLock;
    if (lock == NULL)
        return false;

    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(lock);
    const bool live = fLifecyclePhase == kAirportItlwmLifecycleStarting;
    if (live)
        fLifecyclePhase = kAirportItlwmLifecycleLive;
    IOSimpleLockUnlockEnableInterrupt(lock, irq);
    return live;
}

bool AirportItlwm::beginLifecycleDrain()
{
    IOLock *controlLock = fLifecycleLock;
    IOSimpleLock *admissionLock = fLifecycleAdmissionLock;
    if (controlLock == NULL || admissionLock == NULL)
        return false;

    /*
     * Serialize all stop/release/free convergence before any caller touches
     * HAL state. The control lock is only an owner/wait lock; it is released
     * before command-gate cancellation, thread-call waits, and every source
     * removeEventSource() call below.
     */
    IOLockLock(controlLock);
    if (fLifecycleFinalizing) {
        IOLockUnlock(controlLock);
        return false;
    }
    while (fLifecycleTeardownInFlight) {
        if (fLifecycleDrainOwner == current_thread()) {
            IOLockUnlock(controlLock);
            return true;
        }

        // The final free path must not free controlLock while a follower is
        // still inside IOLockSleep() and about to reacquire it.  Account for
        // that precise window before sleeping, then retire the count only
        // after the wake has reacquired controlLock.
        ++fLifecycleDrainWaiters;
        IOLockSleep(controlLock, this, THREAD_UNINT);
        if (fLifecycleDrainWaiters != 0)
            --fLifecycleDrainWaiters;
        if (fLifecycleDrainWaiters == 0)
            IOLockWakeup(controlLock, &fLifecycleDrainWaiters, false);

        // free() sets this barrier before its final wake.  A follower must
        // not become a second owner or release any shared object after that
        // point; its caller treats false as a strict no-op.
        if (fLifecycleFinalizing) {
            IOLockUnlock(controlLock);
            return false;
        }
    }

    IOInterruptState irq =
        IOSimpleLockLockDisableInterrupt(admissionLock);
    if (fLifecyclePhase == kAirportItlwmLifecycleStopped) {
        IOSimpleLockUnlockEnableInterrupt(admissionLock, irq);
        IOLockUnlock(controlLock);
        return false;
    }
    fLifecyclePhase = kAirportItlwmLifecycleDraining;
    fLifecycleTeardownInFlight = true;
    fLifecycleDrainOwner = current_thread();
    IOSimpleLockUnlockEnableInterrupt(admissionLock, irq);
    IOLockUnlock(controlLock);
    return true;
}

bool AirportItlwm::lifecycleFinalizing()
{
    IOLock *controlLock = fLifecycleLock;
    if (controlLock == NULL)
        return false;

    IOLockLock(controlLock);
    const bool finalizing = fLifecycleFinalizing;
    IOLockUnlock(controlLock);
    return finalizing;
}

void AirportItlwm::beginLifecycleFinalization()
{
    IOLock *controlLock = fLifecycleLock;
    if (controlLock == NULL)
        return;

    IOLockLock(controlLock);
    // This is intentionally set while the drain owner still holds its
    // claim, before finishLifecycleDrain() wakes followers.  New callers
    // then return without touching shared controller state.
    fLifecycleFinalizing = true;
    IOLockUnlock(controlLock);
}

void AirportItlwm::waitForLifecycleDrainWaiters()
{
    IOLock *controlLock = fLifecycleLock;
    if (controlLock == NULL)
        return;

    IOLockLock(controlLock);
    while (fLifecycleDrainWaiters != 0)
        IOLockSleep(controlLock, &fLifecycleDrainWaiters, THREAD_UNINT);
    IOLockUnlock(controlLock);
}

bool AirportItlwm::lifecycleDrainOwnedByCurrentThread()
{
    IOLock *controlLock = fLifecycleLock;
    if (controlLock == NULL)
        return false;
    IOLockLock(controlLock);
    const bool owned = fLifecycleTeardownInFlight &&
        fLifecycleDrainOwner == current_thread();
    IOLockUnlock(controlLock);
    return owned;
}

void AirportItlwm::prepareLifecycleDrain()
{
#if __IO80211_TARGET >= __MAC_26_0
    /*
     * WaitAssociationTarget may sleep under the command gate. Wake it before
     * waiting lifecycle operation users, otherwise a user-client waiter can
     * deadlock teardown while it waits for this cancellation edge. This call
     * does not hold either lifecycle lock across runAction().
     */
    cancelPendingAssocTarget("prepareLifecycleDrain", true);
    cancelSaeRelay("prepareLifecycleDrain", true);
#endif

    IOSimpleLock *admissionLock = fLifecycleAdmissionLock;
    if (admissionLock == NULL)
        return;
    for (;;) {
        IOInterruptState irq =
            IOSimpleLockLockDisableInterrupt(admissionLock);
        const bool drained = fLifecycleOperationUsers == 0;
        IOSimpleLockUnlockEnableInterrupt(admissionLock, irq);
        if (drained)
            return;
        IOSleep(1);
    }
}

void AirportItlwm::releaseAPSTAOwnerClaimed()
{
    /*
     * Keep the APSTA owner release after the common producer fence but before
     * fHalService is detached.  The shipped Tahoe/iwn surface is STA-only,
     * so this is normally dormant; retaining the ordering nevertheless keeps
     * a future explicitly-enabled APSTA backend from skipping stopAPMode()
     * because its lower HAL was already gone.
     */
    if (fAPSTAOwner == NULL)
        return;

#ifdef IEEE80211_APSTA_STATION_EVENT_OPT_OUT
    if (fHalService != NULL) {
        struct ieee80211com *ic = fHalService->get80211Controller();
        if (ic != NULL)
            ieee80211_apsta_event_unregister(ic, fAPSTAOwner);
    }
#endif
    fAPSTAOwner->release();
    fAPSTAOwner = NULL;
}

void AirportItlwm::stopHalAndDrainClaimed()
{
    // The caller owns the Draining transition. Keep every producer fence
    // ahead of HAL detach and publish Stopped only after every source has
    // removed/released its workloop registration.
    prepareLifecycleDrain();
    stopTahoeBootThreadCallAndDrain();
#if __IO80211_TARGET >= __MAC_26_0
    teardownSaeTransportMailboxSource(this, _fWorkloop);
#endif
    teardownLinkStatePublishSource(this, _fWorkloop);
    stopWatchdogAndDrain();
#if __IO80211_TARGET >= __MAC_26_0
    // The producer fence above makes this global trace disarm safe before the
    // controller address can be detached or released.
    airportItlwmPostPltiTraceInvalidate();
#endif
    releaseAPSTAOwnerClaimed();

    // The direct Skywalk RX trampoline is published before registration and
    // is independent of whether configureInterface() reached ether_ifattach.
    // Clear it after the queue-source fence, but before detaching the HAL, on
    // every teardown path.
#if __IO80211_TARGET >= __MAC_26_0
    if (fHalService != nullptr) {
        struct ieee80211com *ic = fHalService->get80211Controller();
        if (ic != nullptr)
            ic->ic_ac.ac_if.if_skywalk_rx = NULL;
    }
#endif

    if (fHalAttached) {
        if (fHalService != nullptr)
            fHalService->detach(pciNub);
        fHalAttached = false;
    }

}

void AirportItlwm::stopHalAndDrain()
{
    if (!beginLifecycleDrain())
        return;
    stopHalAndDrainClaimed();
}

void AirportItlwm::finishLifecycleDrain()
{
    // The drain owner reaches here only after all shared ifnet, Skywalk pool,
    // workloop, and HAL references have been released. Do not wake a second
    // stop merely because the HAL detach itself completed.
    IOLock *controlLock = fLifecycleLock;
    if (controlLock != NULL) {
        IOLockLock(controlLock);
        const bool owner = fLifecycleTeardownInFlight &&
            fLifecycleDrainOwner == current_thread();
        IOLockUnlock(controlLock);
        if (!owner)
            return;
    }

    IOSimpleLock *admissionLock = fLifecycleAdmissionLock;
    if (admissionLock != NULL) {
        IOInterruptState irq =
            IOSimpleLockLockDisableInterrupt(admissionLock);
        fLifecyclePhase = kAirportItlwmLifecycleStopped;
        IOSimpleLockUnlockEnableInterrupt(admissionLock, irq);
    }

    if (controlLock != NULL) {
        IOLockLock(controlLock);
        fLifecycleDrainOwner = NULL;
        fLifecycleTeardownInFlight = false;
        IOLockWakeup(controlLock, this, false);
        IOLockUnlock(controlLock);
    }
}

void AirportItlwm::stopWatchdogAndDrain()
{
    stopScanSourceAndDrain();
#if __IO80211_TARGET >= __MAC_26_0
    stopSkywalkQueuesAndDrain();
#endif

    // Both timers execute on fWatchdogWorkLoop and can reach HAL state.
    // IOWorkLoop::removeEventSource() waits for acknowledgement, so after
    // this flag prevents re-arm it is the lifetime fence before HAL teardown.
    fWatchdogStopping = true;
    IOWorkLoop *workLoop = fWatchdogWorkLoop;

#if __IO80211_TARGET >= __MAC_26_0
    stopTahoeLqmStatsTimer();
    IOTimerEventSource *lqmTimer = fTahoeLqmStatsTimer;
    if (lqmTimer != nullptr) {
        if (workLoop != nullptr)
            workLoop->removeEventSource(lqmTimer);
        fTahoeLqmStatsTimer = nullptr;
        lqmTimer->release();
    }
#endif

    IOTimerEventSource *timer = watchdogTimer;
    if (timer != nullptr) {
        timer->cancelTimeout();
        timer->disable();
        if (workLoop != nullptr)
            workLoop->removeEventSource(timer);
        watchdogTimer = NULL;
        timer->release();
    }
}

void AirportItlwm::stopScanSourceAndDrain()
{
    AirportItlwmScanSourceLifecycle &state = fScanSourceLifecycle;
    IOSimpleLock *admissionLock = state.admissionLock;
    if (admissionLock == NULL)
        return;

    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(admissionLock);
    if (state.tearingDown) {
        IOSimpleLockUnlockEnableInterrupt(admissionLock, irq);
        for (;;) {
            irq = IOSimpleLockLockDisableInterrupt(admissionLock);
            const bool complete = !state.tearingDown;
            IOSimpleLockUnlockEnableInterrupt(admissionLock, irq);
            if (complete)
                return;
            IOSleep(1);
        }
    }
    state.stopping = true;
    state.tearingDown = true;
    IOSimpleLockUnlockEnableInterrupt(admissionLock, irq);

    // Close admission before waiting. schedule/cancel users hold only a
    // retained timer while changing its timeout state; a setup owner holds
    // settingUp until it has rolled an unpublished timer back.
    for (;;) {
        irq = IOSimpleLockLockDisableInterrupt(admissionLock);
        const bool drained = !state.settingUp && state.users == 0;
        IOSimpleLockUnlockEnableInterrupt(admissionLock, irq);
        if (drained)
            break;
        IOSleep(1);
    }

    irq = IOSimpleLockLockDisableInterrupt(admissionLock);
    IOTimerEventSource *source = state.source;
    if (source != NULL)
        source->retain();
    IOWorkLoop *workloop = _fWorkloop;
    if (workloop != NULL)
        workloop->retain();
    IOSimpleLockUnlockEnableInterrupt(admissionLock, irq);

    // fakeScanDone() posts WCL scan results through the controller.  The
    // removeEventSource acknowledgement drains any callback that was already
    // admitted before stopping became visible. Keep tearingDown true until
    // the remove/release sequence has completed so a second stop cannot hand
    // the workloop to releaseAll() in the middle of this fence.
    if (source != NULL) {
        source->cancelTimeout();
        source->disable();
        if (workloop != nullptr)
            workloop->removeEventSource(source);
    }

    // removeEventSource() is now the callback fence. Clear the published
    // pointers while tearingDown remains true, before either reference drop
    // can destroy the timer.
    irq = IOSimpleLockLockDisableInterrupt(admissionLock);
    state.source = NULL;
    scanSource = NULL;
    IOSimpleLockUnlockEnableInterrupt(admissionLock, irq);

    if (source != NULL) {
        source->release();
        source->release();
    }
    if (workloop != NULL)
        workloop->release();

    irq = IOSimpleLockLockDisableInterrupt(admissionLock);
    state.tearingDown = false;
    IOSimpleLockUnlockEnableInterrupt(admissionLock, irq);
}

#if __IO80211_TARGET >= __MAC_26_0
void AirportItlwm::stopSkywalkQueuesAndDrain()
{
    // Queue callbacks run on _fWorkloop and may enter TX/RX paths that use
    // fHalService.  disable() only prevents later scheduling; removing each
    // source is the synchronous acknowledgement fence for an in-flight one.
    if (_fWorkloop == nullptr)
        return;

    if (fMultiCastQueue != nullptr &&
        fMultiCastQueue->getWorkLoop() == _fWorkloop) {
        fMultiCastQueue->disable();
        _fWorkloop->removeEventSource(fMultiCastQueue);
    }
    if (fTxCompQueue != nullptr &&
        fTxCompQueue->getWorkLoop() == _fWorkloop) {
        fTxCompQueue->disable();
        _fWorkloop->removeEventSource(fTxCompQueue);
    }
    if (fTxQueue != nullptr && fTxQueue->getWorkLoop() == _fWorkloop) {
        fTxQueue->disable();
        _fWorkloop->removeEventSource(fTxQueue);
    }
    if (fRxQueue != nullptr && fRxQueue->getWorkLoop() == _fWorkloop) {
        fRxQueue->disable();
        _fWorkloop->removeEventSource(fRxQueue);
    }
}
#endif

void AirportItlwm::detachSkywalkInterfaceAndFenceBorrowers()
{
    /*
     * Queue getters intentionally return borrowed Skywalk objects, matching
     * the framework ABI. A framework consumer may still hold a borrowed
     * result after that getter returns, including while this recursive detach
     * asks the interface for its queue inventory. The required lifetime fence
     * is therefore detachInterface(), not merely a null store or a scoped
     * lifecycle user: STOPPED -> BSD detached -> logical link detached ->
     * queue/pool released.
     *
     * Keep the two inverses independent.  A failed start can attach the
     * controller interface before configureInterface() reaches
     * ether_ifattach(), while a configured interface needs exactly one BSD
     * detach before its framework detach.
     */
    if (fSkywalkEthernetAttached) {
        if (fHalService != nullptr) {
            struct ieee80211com *ic = fHalService->get80211Controller();
            if (ic != nullptr) {
                struct _ifnet *ifp = &ic->ic_ac.ac_if;
                ether_ifdetach(ifp);
            }
        }
        fSkywalkEthernetAttached = false;
    }

    const bool hadInterfaceAttachment = fSkywalkInterfaceAttached;
    if (hadInterfaceAttachment && fNetIf != nullptr) {
        RT3_SET(15); // IONetworkController::detachInterface entered
        detachInterface(fNetIf, true);
    }
    fSkywalkInterfaceAttached = false;

    // fNetIf->attach(this) can succeed even if attachInterface() immediately
    // fails. Only that partial path needs an explicit provider inverse: a
    // successful detachInterface() owns normal framework/IOService teardown,
    // and a second fNetIf->detach(this) after it would be out of order.
    if (!hadInterfaceAttachment && fSkywalkInterfaceProviderAttached &&
        fNetIf != nullptr)
        fNetIf->detach(this);
    fSkywalkInterfaceProviderAttached = false;
}

void AirportItlwm::releaseAll(bool finishLifecycle)
{
    /*
     * releaseAll() is reached from normal stop, start failures, and free.
     * It either inherits the current drain claim or becomes the sole owner;
     * a follower waits in beginLifecycleDrain() and then returns without
     * releasing any shared object under the first owner.
     */
    if (!lifecycleDrainOwnedByCurrentThread() && !beginLifecycleDrain())
        return;

    // Stop/cleanup is idempotent but must be completed before either workloop
    // is released. This path also performs cancellation before waiting any
    // admitted PLTI/selector operation users.
    stopHalAndDrainClaimed();

    // stopHalAndDrainClaimed() disabled and synchronously removed all queue
    // sources. Detach the BSD/framework borrowers while queues and pools are
    // still owned; start failures after attachInterface() must take exactly
    // the same fence as normal stop.
    detachSkywalkInterfaceAndFenceBorrowers();

    if (fWatchdogWorkLoop) {
        fWatchdogWorkLoop->release();
        fWatchdogWorkLoop = NULL;
    }
#if __IO80211_TARGET >= __MAC_26_0
    skywalkTxDrainCompletionPackets(this);
    skywalkRxDrainPendingPackets(this);
    OSSafeReleaseNULL(fMultiCastQueue);
    OSSafeReleaseNULL(fTxCompQueue);
    OSSafeReleaseNULL(fTxQueue);
    OSSafeReleaseNULL(fRxQueue);
    OSSafeReleaseNULL(fTxPool);
    OSSafeReleaseNULL(fRxPool);
    sRT.fTxQueuePtr = 0;
    sRT.fRxQueuePtr = 0;
    sRT.fTxPoolPtr = 0;
    sRT.fRxPoolPtr = 0;
    fSkywalkTxQueueDepth = 0;
    fSkywalkRxQueueCapacity = 0;
#endif

    // Queue/pool release is intentionally above this drop: Skywalk logical
    // link teardown can still dereference the packet-pool inventory.
    OSSafeReleaseNULL(fNetIf);
    sRT.fNetIfPtr = 0;

#if __IO80211_TARGET >= __MAC_26_0
    if (fBssManager != nullptr) {
        reinterpret_cast<OSObject *>(fBssManager)->release();
        fBssManager = nullptr;
    }
#endif
    if (driverSnapshotsPipe != nullptr && driverSnapshotsPipeStarted) {
        driverSnapshotsPipe->stopPipe();
        driverSnapshotsPipeStarted = false;
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
    // stopHalAndDrainClaimed() fenced producers, released any dormant APSTA
    // owner while the lower HAL was live, and returned HAL attach ownership
    // while this drain owner remained active.
    if (fHalService) {
        fHalService->release();
        fHalService = NULL;
    }
    if (_fWorkloop) {
        if (_fCommandGate) {
            _fWorkloop->removeEventSource(_fCommandGate);
            _fCommandGate->release();
            _fCommandGate = NULL;
        }
        _fWorkloop->release();
        _fWorkloop = NULL;
    }
    if (finishLifecycle)
        finishLifecycleDrain();
}

#if __IO80211_TARGET >= __MAC_26_0
static_assert(IEEE80211_ADDR_LEN == TahoeBssBlacklistContracts::kBssidLength,
              "BSS blacklist contract requires six-byte addresses");
static_assert(
    sizeof(((struct ieee80211com *)0)->ic_bss_blacklist_requested) ==
        TahoeBssBlacklistContracts::kRequestLength,
    "BSS blacklist requested owner must preserve all 43 input bytes");
static_assert(
    sizeof(((struct ieee80211com *)0)->ic_bss_blacklist_bssid) ==
        TahoeBssBlacklistContracts::kMaxEntries * IEEE80211_ADDR_LEN,
    "BSS blacklist applied owner must preserve seven addresses");
static_assert(
    sizeof(((struct ieee80211com *)0)->ic_bss_blacklist_event_count) +
            sizeof(((struct ieee80211com *)0)->ic_bss_blacklist_event_body) ==
        TahoeBssBlacklistContracts::kEventCapacity,
    "BSS blacklist persistent event owner must preserve 48 bytes");
static_assert(
    offsetof(struct ieee80211com, ic_bss_blacklist_event_body) ==
        offsetof(struct ieee80211com, ic_bss_blacklist_event_count) +
            sizeof(uint32_t),
    "BSS blacklist event variable body must immediately follow the count");

static IOReturn
airportItlwmPublishBssBlacklist(AirportItlwm *self, struct ieee80211com *ic)
{
    using namespace TahoeBssBlacklistContracts;

    EventCarrier event;
    memset(&event, 0, sizeof(event));
    const size_t eventLength = buildEventCarrier(
        ic->ic_bss_blacklist_count,
        &ic->ic_bss_blacklist_bssid[0][0], &event);

    ic->ic_bss_blacklist_event_count = event.count;
    memcpy(ic->ic_bss_blacklist_event_body, event.body,
           sizeof(ic->ic_bss_blacklist_event_body));

    if (eventLength != 0 && self->fNetIf != nullptr) {
        self->postMessage(
            self->fNetIf, kEventMessage,
            &ic->ic_bss_blacklist_event_count,
            static_cast<unsigned int>(eventLength), true);
    }
    return kIOReturnSuccess;
}

static IOReturn
airportItlwmSetBssBlacklistAction(OSObject *target, void *arg0,
                                  void *, void *, void *)
{
    using namespace TahoeBssBlacklistContracts;

    AirportItlwm *self = OSDynamicCast(AirportItlwm, target);
    const uint8_t *request = static_cast<const uint8_t *>(arg0);
    if (self == nullptr || request == nullptr)
        return static_cast<IOReturn>(kBadArgumentStatus);
    if (self->fHalService == nullptr)
        return kIOReturnNotReady;

    struct ieee80211com *ic = self->fHalService->get80211Controller();
    if (ic == nullptr)
        return kIOReturnNotReady;

    memcpy(ic->ic_bss_blacklist_requested, request, kRequestLength);

    AppliedState applied;
    if (decodeAppliedState(request, &applied)) {
        ic->ic_bss_blacklist_count = 0;
        memset(ic->ic_bss_blacklist_bssid, 0,
               sizeof(ic->ic_bss_blacklist_bssid));
        memcpy(ic->ic_bss_blacklist_bssid, applied.bssids,
               sizeof(ic->ic_bss_blacklist_bssid));
        ic->ic_bss_blacklist_count = applied.count;
    }

    // The reference setter ignores lower programming status, then launches
    // the same async query used by GET. Publish the current applied list even
    // when an invalid requested count left that list unchanged.
    return airportItlwmPublishBssBlacklist(self, ic);
}

static IOReturn
airportItlwmQueryBssBlacklistAction(OSObject *target, void *,
                                    void *, void *, void *)
{
    AirportItlwm *self = OSDynamicCast(AirportItlwm, target);
    if (self == nullptr)
        return kIOReturnBadArgument;
    if (self->fHalService == nullptr)
        return kIOReturnNotReady;

    struct ieee80211com *ic = self->fHalService->get80211Controller();
    if (ic == nullptr)
        return kIOReturnNotReady;
    return airportItlwmPublishBssBlacklist(self, ic);
}

IOReturn AirportItlwm::setBssBlacklistOwner(const uint8_t *request)
{
    if (request == nullptr) {
        return static_cast<IOReturn>(
            TahoeBssBlacklistContracts::kBadArgumentStatus);
    }

    IOCommandGate *gate = getCommandGate();
    if (gate == nullptr)
        return kIOReturnNotReady;
    return gate->runAction(
        airportItlwmSetBssBlacklistAction,
        const_cast<uint8_t *>(request));
}

IOReturn AirportItlwm::queryBssBlacklistOwner()
{
    IOCommandGate *gate = getCommandGate();
    if (gate == nullptr)
        return kIOReturnNotReady;
    return gate->runAction(airportItlwmQueryBssBlacklistAction);
}
#endif

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
    that->postMessage(that->fNetIf, msg, arg1, (unsigned int)(uintptr_t)arg2, true);
    RT_SET(5);
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
postRsnHandshakeDoneGated(OSObject *target, void *arg0, void *arg1, void *arg2, void *arg3)
{
    AirportItlwm *that = OSDynamicCast(AirportItlwm, target);
    if (!that || !that->fNetIf) {
        XYLog("DEBUG %s SKIP: that=%p fNetIf=%p\n", __FUNCTION__, that,
              that ? that->fNetIf : NULL);
        return kIOReturnNotReady;
    }

#if __IO80211_TARGET >= __MAC_26_0
    const bool rekey = (uintptr_t)arg0 != 0;
    ((IO80211InfraInterface *)that->fNetIf)->handleKeyDone(true, rekey);
#endif
    return postMessageGated(target,
        (void *)(uintptr_t)APPLE80211_M_RSN_HANDSHAKE_DONE, NULL,
        (void *)(uintptr_t)0, NULL);
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

    struct ieee80211_node *ni;
    RB_FOREACH(ni, ieee80211_tree, &ic->ic_tree) {
        if (!buildTahoeWclScanResultPayload(ic, ni, &payload, &payloadLen))
            continue;
        that->postMessage(that->fNetIf, APPLE80211_M_WCL_SCAN_RESULT,
                          &payload, payloadLen, true);
    }

    UInt32 status = 0;
    that->postMessage(that->fNetIf, APPLE80211_M_WCL_SCAN_DONE,
                      &status, sizeof(status), true);
    return kIOReturnSuccess;
}

void AirportItlwm::
eventHandler(struct ieee80211com *ic, int msgCode, void *data)
{
    if (ic == nullptr)
        return;

    AirportItlwm *that = OSDynamicCast(AirportItlwm, ic->ic_ac.ac_if.controller);
    AirportItlwmControllerLifecycleOperationGuard lifecycle(that, true);
    if (!lifecycle.admitted() || !that->fNetIf) {
        XYLog("DEBUG %s SKIP: interface=NULL or draining\n", __FUNCTION__);
        return;
    }

#if __IO80211_TARGET >= __MAC_26_0
    /*
     * IWX invokes this from its terminal nswq worker and from reset. Keep
     * this branch strictly nonblocking: it only copies/signals the private
     * mailbox, so taskq_barrier(sc_nswq) can never wait for this command gate.
     */
    if (msgCode == IEEE80211_EVT_SAE_AUTH_TRANSPORT ||
        msgCode == IEEE80211_EVT_SAE_AUTH_TRANSPORT_RESET) {
        if (data != nullptr) {
            handleSaeAuthTransportEvent(that,
                (const struct ItlSaeAuthTransportEventV1 *)data,
                msgCode == IEEE80211_EVT_SAE_AUTH_TRANSPORT_RESET);
        }
        return;
    }
#endif

    IOCommandGate *gate = that->getCommandGate();
    if (gate == nullptr) {
        XYLog("DEBUG %s SKIP: command gate unavailable\n", __FUNCTION__);
        return;
    }

    RT_SET(0);
    sRT.evtCount++;
    sRT.lastEvtCode = msgCode;
    sRT.ic_state = ic->ic_state;
    sRT.if_flags = ic->ic_ac.ac_if.if_flags;
    UInt32 apple80211Msg;
    void *msgData = NULL;
    unsigned int msgDataLen = 0;
    static UInt32 scanStatus;  // static — must survive until postMessageGated runs
    static UInt32 reassocEventStatus[2];
    static UInt32 reassocFailureStatus;
    static apple80211_wcl_auth_assoc_complete_event authAssocStatus;
    switch (msgCode) {
        case IEEE80211_EVT_COUNTRY_CODE_UPDATE:
            RT_SET(1);
            publishResolvedCountryCodeProperty(that);
            apple80211Msg = APPLE80211_M_COUNTRY_CODE_CHANGED;
            break;
        case IEEE80211_EVT_STA_ASSOC_DONE:
            RT_SET(2);
#if __IO80211_TARGET >= __MAC_26_0
            buildTahoeWclAuthAssocCompletePayload(0, 0, &authAssocStatus);
            apple80211Msg = APPLE80211_M_WCL_AUTH_ASSOC_EVENT;
            msgData = &authAssocStatus;
            msgDataLen = sizeof(authAssocStatus);
            break;
#else
            apple80211Msg = APPLE80211_M_ASSOC_DONE;
            break;
#endif
        case IEEE80211_EVT_STA_RSN_HANDSHAKE_DONE:
            RT_SET(2);
            // The in-kernel PAE completed the 4-way handshake (PTK+GTK
            // installed, 802.1X port opened). On the macOS-supplicant path
            // setCIPHER_KEY posts these on GTK install; the kernel-PAE path
            // never calls setCIPHER_KEY, so publish the reference completion
            // state here or wifid's WCL join state machine times out and fires
            // setWCL_JOIN_ABORT even though the handshake succeeded on air.
            // The RSN key-done path is command-gated, and the WCL routes below
            // use the gate-safe controller->postMessage (PostOffice) dispatch.
            gate->runAction(postRsnHandshakeDoneGated,
                            (void *)(uintptr_t)false, NULL, NULL);
#if __IO80211_TARGET >= __MAC_26_0
            postTahoeWclLinkUpInd(that, 0);
            postTahoeWclConnectCompleteEvent(that);
            /*
             * Keep RSN_HANDSHAKE_DONE limited to key-completion and WCL join
             * FSM completion. The BSSID/SSID identity events that wake
             * airportd's __associatedNetwork path are owned by the
             * parent-accepted Skywalk link-up transition, after IO80211 has
             * made the interface current-state model coherent for that
             * re-read.
             */
#endif
            return;
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
        case IEEE80211_EVT_WCL_REASSOC_DONE:
            // Recovered Apple terminal: reassociation result selector
            // 0x49 with 8-byte status payload, first dword == 0 means
            // success. The host owner publishes this only after a real
            // reassociation request send/attempt has produced a
            // success result; the post-send gate in
            // ieee80211_wcl_reassoc_post_success() enforces that.
            reassocEventStatus[0] = 0;
            reassocEventStatus[1] = 0;
            apple80211Msg = IEEE80211_WCL_REASSOC_OWNER_SELECTOR_REASSOC_EVENT;
            msgData = reassocEventStatus;
            msgDataLen = sizeof(reassocEventStatus);
            break;
        case IEEE80211_EVT_WCL_REASSOC_FAIL:
            // Recovered Apple terminal: reassociation failure selector
            // 0xcf with 4-byte nonzero failure code. Published only by
            // the gated host owner helper after a real send/attempt
            // failure (send error, response failure/discard, or
            // management timeout).
            reassocFailureStatus = data ? *(UInt32 *)data : 1U;
            if (reassocFailureStatus == 0)
                reassocFailureStatus = 1U;
            apple80211Msg = IEEE80211_WCL_REASSOC_OWNER_SELECTOR_FAILURE;
            msgData = &reassocFailureStatus;
            msgDataLen = sizeof(reassocFailureStatus);
            break;
        default:
            XYLog("DEBUG %s UNHANDLED msgCode=%d\n", __FUNCTION__, msgCode);
            return;
    }
    // Defer postMessage to workloop context — cannot call from interrupt thread.
    gate->runAction(postMessageGated,
                    (void *)(uintptr_t)apple80211Msg, msgData,
                    (void *)(uintptr_t)msgDataLen);
}

void AirportItlwm::watchdogAction(IOTimerEventSource *timer)
{
    // stopWatchdogAndDrain() sets the permanent-stop flag before removing
    // this source synchronously from fWatchdogWorkLoop.  Do not touch HAL
    // state or re-arm the timer once teardown has begun.
    if (fWatchdogStopping || timer == nullptr || timer != watchdogTimer)
        return;

    ItlHalService *hal = fHalService;
    if (hal == nullptr)
        return;

    struct _ifnet *ifp = &hal->get80211Controller()->ic_ac.ac_if;
    struct ieee80211com *ic = hal->get80211Controller();
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
    airportItlwmRegDiagPoll(this);
#if __IO80211_TARGET >= __MAC_26_0
    airportItlwmPostPltiTracePoll(this);
#endif
    if (ifp->if_watchdog)
        (*ifp->if_watchdog)(ifp);
    if (!fWatchdogStopping && timer == watchdogTimer)
        timer->setTimeoutMS(kWatchDogTimerPeriod);
}

#if __IO80211_TARGET >= __MAC_26_0
IOReturn AirportItlwm::publishTahoeLqmStatsGated(
    OSObject *target, void *, void *, void *, void *)
{
    AirportItlwm *that = OSDynamicCast(AirportItlwm, target);
    if (that == nullptr || that->fWatchdogStopping ||
        !that->fTahoeLqmAssociated ||
        that->fHalService == nullptr || that->fNetIf == nullptr)
        return kIOReturnNotReady;

    struct ieee80211com *ic = that->fHalService->get80211Controller();
    ItlDriverInfo *driverInfo = that->fHalService->getDriverInfo();
    if (ic == nullptr || ic->ic_state != IEEE80211_S_RUN ||
        ic->ic_bss == nullptr || driverInfo == nullptr)
        return kIOReturnNotReady;

    struct _ifnet *ifp = &ic->ic_ac.ac_if;
    if (ifp->netStat == nullptr)
        return kIOReturnNotReady;

    TahoeLqmContracts::CounterSnapshot current{};
    current.txErrors = ifp->netStat->outputErrors;
    current.rxErrors = ifp->netStat->inputErrors;
    current.txFrames = ifp->netStat->outputPackets;
    current.rxFrames = ifp->netStat->inputPackets;
    current.beaconFrames = driverInfo->getLqmBeaconCount();

    TahoeLqmContracts::EventData event{};
    const int32_t rssi = IWM_MIN_DBM + ic->ic_bss->ni_rssi;
    const TahoeLqmContracts::CounterSnapshot *previous =
        that->fTahoeLqmHasPreviousSnapshot
            ? &that->fTahoeLqmPreviousSnapshot
            : nullptr;
    if (!TahoeLqmContracts::buildEventData(
            rssi, driverInfo->getBSSNoise(), current, previous, &event))
        return kIOReturnBadArgument;

    that->fTahoeLqmPreviousSnapshot = current;
    that->fTahoeLqmHasPreviousSnapshot = true;
    that->postMessage(that->fNetIf, TahoeLqmContracts::kEventMessage,
                      &event, sizeof(event), true);
    return kIOReturnSuccess;
}

void AirportItlwm::tahoeLqmStatsAction(IOTimerEventSource *timer)
{
    if (fWatchdogStopping || timer == nullptr ||
        timer != fTahoeLqmStatsTimer ||
        !fTahoeLqmAssociated || fHalService == nullptr)
        return;

    IOCommandGate *gate = getCommandGate();
    if (gate != nullptr)
        gate->runAction(publishTahoeLqmStatsGated);

    if (!fWatchdogStopping && fTahoeLqmAssociated &&
        fBssManager != nullptr &&
        fBssManager->isAssociated() && timer == fTahoeLqmStatsTimer) {
        timer->setTimeoutMS(fTahoeLqmStatsIntervalMs);
    }
}

void AirportItlwm::startTahoeLqmStatsTimer()
{
    fTahoeLqmAssociated = true;
    fTahoeLqmHasPreviousSnapshot = false;
    fTahoeLqmPreviousSnapshot = TahoeLqmContracts::CounterSnapshot{};
    if (fWatchdogStopping || fTahoeLqmStatsTimer == nullptr)
        return;

    fTahoeLqmStatsTimer->cancelTimeout();
    fTahoeLqmStatsTimer->enable();
    fTahoeLqmStatsTimer->setTimeoutMS(fTahoeLqmStatsIntervalMs);
}

void AirportItlwm::stopTahoeLqmStatsTimer()
{
    fTahoeLqmAssociated = false;
    fTahoeLqmHasPreviousSnapshot = false;
    fTahoeLqmPreviousSnapshot = TahoeLqmContracts::CounterSnapshot{};
    if (fTahoeLqmStatsTimer == nullptr)
        return;

    fTahoeLqmStatsTimer->cancelTimeout();
    fTahoeLqmStatsTimer->disable();
}

void AirportItlwm::setTahoeLqmStatsInterval(uint32_t intervalMs)
{
    if (intervalMs < TahoeLqmContracts::kMinimumIntervalMs)
        return;

    fTahoeLqmStatsIntervalMs = intervalMs;
    if (!fWatchdogStopping && fTahoeLqmAssociated &&
        fTahoeLqmStatsTimer != nullptr) {
        fTahoeLqmStatsTimer->cancelTimeout();
        fTahoeLqmStatsTimer->enable();
        fTahoeLqmStatsTimer->setTimeoutMS(intervalMs);
    }
}
#endif

void AirportItlwm::fakeScanDone(OSObject *owner, IOTimerEventSource *sender)
{
    AirportItlwm *that = (AirportItlwm *)owner;
    if (that == NULL || !that->scanSourceCallbackLive(sender))
        return;
    RT_SET(13);
    sRT.scanCount++;
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
     * Apple copies the firmware wl_bss_info tagged-IE tail into this carrier.
     * The local node cache may only retain the RSN tail, so rebuild the
     * mandatory beacon SSID/TIM elements from node fields before appending the
     * retained raw IEs. Seed the primary 20 MHz chanSpec, let IO80211 parse the
     * raw HT/VHT/HE operation IEs when present, then post the real WCL
     * completion bulletin.
     */
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
    fLifecycleLock = IOLockAlloc();
    fLifecycleAdmissionLock = IOSimpleLockAlloc();
    fLifecyclePhase = kAirportItlwmLifecycleStarting;
    fLifecycleTeardownInFlight = false;
    fLifecycleDrainWaiters = 0;
    fLifecycleFinalizing = false;
    fLifecycleDrainOwner = NULL;
    fLifecycleOperationUsers = 0;
    memset(&fLinkStatePublishLifecycle, 0,
           sizeof(fLinkStatePublishLifecycle));
    memset(&fScanSourceLifecycle, 0, sizeof(fScanSourceLifecycle));
#if __IO80211_TARGET >= __MAC_26_0
    memset(&fSaeTransportMailbox, 0, sizeof(fSaeTransportMailbox));
#endif
    fLinkStatePublishLifecycle.admissionLock = IOSimpleLockAlloc();
    fScanSourceLifecycle.admissionLock = IOSimpleLockAlloc();
#if __IO80211_TARGET >= __MAC_26_0
    fSaeTransportMailbox.admissionLock = IOSimpleLockAlloc();
#endif
    fWatchdogStopping = false;
    tahoeBootThreadCall = nullptr;
    fTahoeBootCallLock = IOLockAlloc();
    fTahoeBootStopping = false;
    fTahoeBootScheduled = false;
    fTahoeBootCallActive = false;
    fTahoeBootCallRetained = false;
    fTahoeBootCallOwner = nullptr;
    fHalAttached = false;
    fSkywalkInterfaceProviderAttached = false;
    fSkywalkInterfaceAttached = false;
    fSkywalkEthernetAttached = false;
    if (fTahoeBootCallLock == nullptr || fLifecycleLock == nullptr ||
        fLifecycleAdmissionLock == nullptr ||
        fLinkStatePublishLifecycle.admissionLock == nullptr ||
        fScanSourceLifecycle.admissionLock == nullptr
#if __IO80211_TARGET >= __MAC_26_0
        || fSaeTransportMailbox.admissionLock == nullptr
#endif
        )
        ret = false;
    awdlSyncEnable = true;
    power_state = 0;
    pmPowerStateFlags = 0;
    fpNetStats = NULL;
#if __IO80211_TARGET >= __MAC_26_0
    memset(&tahoeLegacyNetStats, 0, sizeof(tahoeLegacyNetStats));
#endif
    tahoeRequestedPowerState = kWiFiPowerOff;
    tahoeBootstrapPowerPending = false;
    tahoeBootstrapPowerWindowOpen = true;
    driverLogStream = nullptr;
    fBssManager = nullptr;
    driverSnapshotsPipeStarted = false;
#if __IO80211_TARGET >= __MAC_26_0
    fTahoeLqmStatsTimer = nullptr;
    fTahoeLqmStatsIntervalMs =
        TahoeLqmContracts::kDefaultStatsIntervalMs;
    fTahoeLqmAssociated = false;
    fTahoeLqmHasPreviousSnapshot = false;
    fTahoeLqmPreviousSnapshot = TahoeLqmContracts::CounterSnapshot{};
#endif
    fTxPool = NULL;
    fRxPool = NULL;
    fTxQueue = NULL;
    fTxCompQueue = NULL;
    fRxQueue = NULL;
    fMultiCastQueue = NULL;
    fSkywalkTxQueueDepth = 0;
    fSkywalkRxQueueCapacity = 0;
    fRxPendingLock = IOLockAlloc();
    memset(fRxPendingPackets, 0, sizeof(fRxPendingPackets));
    memset(fRxPendingTags, 0, sizeof(fRxPendingTags));
    memset(fRxPendingLengths, 0, sizeof(fRxPendingLengths));
    fRxPendingHead = 0;
    fRxPendingTail = 0;
    fRxPendingCount = 0;
    fTxCompletionPendingLock = IOLockAlloc();
    memset(fTxCompletionPendingPackets, 0, sizeof(fTxCompletionPendingPackets));
    fTxCompletionPendingHead = 0;
    fTxCompletionPendingTail = 0;
    fTxCompletionPendingCount = 0;
    fAPSTAOwner = NULL;
    scanSource = NULL;
    memset(fAPSTACoreFeatureFlags, 0, sizeof(fAPSTACoreFeatureFlags));
    fAPSTACorePrivateFeatureByte4d59 = 0;
    if (fRxPendingLock == NULL || fTxCompletionPendingLock == NULL)
        ret = false;
    memset(geo_location_cc, 0, sizeof(geo_location_cc));
    RT_SET(15);
    return ret;
}

IOService* AirportItlwm::probe(IOService *provider, SInt32 *score)
{
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
    // 11   0x800  driverSnapshotsPipe started
    // ---------------------------------------------------------------
    struct CCDiag {
        volatile uint32_t mask;
        void *pciNub;
        void *logPipe;
        void *dataPathPipe;
        void *snapshotsPipe;
        bool snapshotsPipeStarted;
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
    driverSnapshotsPipeStarted = false;
    if (driverSnapshotsPipe) {
        CC_SET(3);
        // CCDataPipe::start initializes the queue used by deferred fault
        // reports. Create the stream only after its public lifecycle starts.
        driverSnapshotsPipeStarted = driverSnapshotsPipe->startPipe();
        sCCDiag.snapshotsPipeStarted = driverSnapshotsPipeStarted;
        if (driverSnapshotsPipeStarted) CC_SET(11);
    }

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
        && driverSnapshotsPipeStarted && driverFaultReporter
        && io80211FaultReporter;
    if (ok) CC_SET(10);

    if (!ok) {
        panic("AirportItlwm::initCCLogs FAILED  ccMask=0x%03x rtMask=0x%07x rt2=0x%04x | "
              "pci=%p logPipe=%p dataPath=%p snap=%p snapStarted=%u "
              "faultStream=%p dataStream=%p wl=%p ccfr=%p io80211fr=%p logStream=%p | "
              "ic=%d fl=0x%x pwr=%u evt=%u pm=%u",
              sCCDiag.mask, sRT.rtMask, sRT.rtMask2, sCCDiag.pciNub,
              sCCDiag.logPipe, sCCDiag.dataPathPipe, sCCDiag.snapshotsPipe,
              sCCDiag.snapshotsPipeStarted,
              sCCDiag.faultStream, sCCDiag.dataStream, sCCDiag.frWorkloop,
              sCCDiag.ccFaultReporter, sCCDiag.io80211FR, sCCDiag.logStream,
              sRT.ic_state, sRT.if_flags, sRT.power_state,
              sRT.evtCount, sRT.postMsgCount);
    }

    return ok;
#undef CC_SET
}

#if __IO80211_TARGET >= __MAC_26_0
bool AirportItlwm::initTahoeBssManager()
{
    if (fBssManager != nullptr)
        return true;
    if (driverLogStream == nullptr)
        return false;

    void *storage = AirportItlwm_IO80211BssManager_operatorNew(
        TahoeBssManagerContracts::kBssManagerObjectSize);
    if (storage == nullptr)
        return false;

    AirportItlwm_IO80211BssManager_C1(storage);
    IO80211BssManager *manager =
        reinterpret_cast<IO80211BssManager *>(storage);
    if (!manager->initwithOptions(driverLogStream, nullptr)) {
        reinterpret_cast<OSObject *>(storage)->release();
        return false;
    }

    fBssManager = manager;
    return true;
}

bool AirportItlwm::setTahoeCurrentBss(
    TahoeBssManagerContracts::BeaconMetaData &metadata,
    uint8_t *ie)
{
    if (fBssManager == nullptr || driverLogStream == nullptr || ie == nullptr)
        return false;

    CommonFaultReporter *faultReporter = getCommonFaultReporter();
    if (faultReporter == nullptr)
        return false;

    void *storage = AirportItlwm_IO80211BSSBeacon_operatorNew(
        TahoeBssManagerContracts::kBssBeaconObjectSize);
    if (storage == nullptr)
        return false;

    AirportItlwm_IO80211BSSBeacon_C1(storage);
    if (!AirportItlwm_IO80211BSSBeacon_initWithChanSpec(
            storage, driverLogStream, faultReporter) ||
        !AirportItlwm_IO80211BSSBeacon_setBeaconDataFromMsg(
            storage, &metadata, ie)) {
        reinterpret_cast<OSObject *>(storage)->release();
        return false;
    }

    IO80211BSSBeacon *beacon =
        reinterpret_cast<IO80211BSSBeacon *>(storage);
    fBssManager->setCurrentBSS(
        beacon,
        isAPSTACoreFeatureFlagSet(
            TahoeBssManagerContracts::kBaseCurrentBssStateFeatureGate));

    apple80211_channel channel{};
    if (fNetIf != nullptr &&
        fBssManager->getCurrentChannel(&channel) == kIOReturnSuccess) {
        postMessage(fNetIf, TahoeBssManagerContracts::kCurrentBssChannelMessage,
                    &channel, sizeof(channel), true);
    }
    fAPSTACorePrivateFeatureByte4d59 |=
        TahoeBssManagerContracts::kCurrentBssPrivateStateMask;

    reinterpret_cast<OSObject *>(storage)->release();
    return true;
}

void AirportItlwm::clearTahoeCurrentBss()
{
    if (fBssManager == nullptr)
        return;

    fBssManager->setCurrentBSS(
        nullptr,
        isAPSTACoreFeatureFlagSet(
            TahoeBssManagerContracts::kBaseCurrentBssStateFeatureGate));
    apple80211_channel channel{};
    if (fNetIf != nullptr) {
        postMessage(fNetIf, TahoeBssManagerContracts::kCurrentBssChannelMessage,
                    &channel, sizeof(channel), true);
    }
    fAPSTACorePrivateFeatureByte4d59 &= static_cast<uint8_t>(
        ~TahoeBssManagerContracts::kCurrentBssPrivateStateMask);
}
#else
bool AirportItlwm::initTahoeBssManager()
{
    return false;
}

bool AirportItlwm::setTahoeCurrentBss(
    TahoeBssManagerContracts::BeaconMetaData &,
    uint8_t *)
{
    return false;
}

void AirportItlwm::clearTahoeCurrentBss()
{
}
#endif

bool AirportItlwm::start(IOService *provider)
{
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
                      "pmPol=%p pmGate=%u pmNoop=%u txDrop=%u "
                      "sysOff=%u sysOn=%u invalid=%u gateErr=%u | "
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
                      sRT.pmGateCount, sRT.pmNoopCount,
                      sRT.outputDropPwr,
                      sRT.pmSystemOffCnt, sRT.pmSystemOnCnt,
                      sRT.pmInvalidCount, sRT.pmGateErrorCnt,
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
#define SD_SET(bit) do { sDiag.mask |= (1u << (bit)); } while(0)
#define DISARM_PANIC_TIMER() do { SD_SET(17); thread_call_cancel(panicTimer); thread_call_free(panicTimer); } while(0)

    UInt8 builtIn = 0;
    setProperty("built-in", OSData::withBytes(&builtIn, sizeof(builtIn)));
    setProperty("DriverKitDriver", kOSBooleanFalse);
#if __IO80211_TARGET >= __MAC_26_0
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
    SD_SET(4); // super::start entered
    bool superResult = super::start(provider);
    if (!superResult) {
        XYLog("DEBUG %s [STEP 2] FAIL: super::start returned false\n", __FUNCTION__);
        DISARM_PANIC_TIMER();
        return false;
    }
    SD_SET(5); // super::start OK
#if __IO80211_TARGET >= __MAC_26_0
    if (!initTahoeBssManager()) {
        XYLog("DEBUG %s [STEP 2] FAIL: BssManager init\n", __FUNCTION__);
        super::stop(provider);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
#endif
    sDiag.step = 2;
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
#if __IO80211_TARGET >= __MAC_26_0
    // Initialize the project-owned PLTI PMK producer trigger state.
    // fAssocTarget is published lazily on the PSK association-start
    // edge; until then generation==0 means "no pending request" and
    // the helper's WaitAssociationTarget call sleeps under the
    // controller command gate.
    memset(&fAssocTarget, 0, sizeof(fAssocTarget));
    fAssocGenCounter       = 0;
    fAssocTargetCanceled   = false;
    fAssocTargetTerminating = false;
    AirportItlwmSaeRelayFsmV1Clear(&fSaeRelay);
    explicit_bzero(&fSaePendingTxReply, sizeof(fSaePendingTxReply));
    explicit_bzero(&fSaePendingTxRequest, sizeof(fSaePendingTxRequest));
    fSaeNextTxTicket = 0;
    fSaePendingTxActive = false;
    explicit_bzero(&fSaeLastTerminalTxEvent,
                   sizeof(fSaeLastTerminalTxEvent));
    fSaeLastTerminalTxEventValid = false;
    memset(fSaeControllerNonce, 0, sizeof(fSaeControllerNonce));
    if (!airportItlwmSaeFillNonZero(fSaeControllerNonce)) {
        XYLog("DEBUG %s [STEP 4] FAIL: SAE relay nonce\n", __FUNCTION__);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    memset(fSaeRelayWaitingCookie, 0, sizeof(fSaeRelayWaitingCookie));
    fSaeRelayCancelEpoch = 0;
    fSaeRelayWaiterActive = false;
    fSaeRelayCanceled = false;
    fSaeRelayTerminating = false;
#endif
    const IONetworkMedium *primaryMedium;
    if (!createMediumTables(&primaryMedium) ||
        !setCurrentMedium(primaryMedium) || !setSelectedMedium(primaryMedium)) {
        XYLog("DEBUG %s [STEP 4] FAIL: setup medium\n", __FUNCTION__);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    sDiag.step = 4;
    fHalService->initWithController(this, _fWorkloop, _fCommandGate);
    fHalService->get80211Controller()->ic_event_handler = eventHandler;
#if __IO80211_TARGET >= __MAC_26_0
    {
        struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
        memset(&tahoeLegacyNetStats, 0, sizeof(tahoeLegacyNetStats));
        fpNetStats = &tahoeLegacyNetStats;
        ifp->netStat = fpNetStats;
    }
#endif

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
    fHalAttached = true;
    SD_SET(9); // HAL attached
    sDiag.step = 6;
    fWatchdogWorkLoop = IOWorkLoop::workLoop();
    if (fWatchdogWorkLoop == NULL) {
        XYLog("DEBUG %s [STEP 6] FAIL: watchdog workloop\n", __FUNCTION__);
        stopHalAndDrain();
        super::stop(pciNub);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    watchdogTimer = IOTimerEventSource::timerEventSource(this, OSMemberFunctionCast(IOTimerEventSource::Action, this, &AirportItlwm::watchdogAction));
    if (!watchdogTimer) {
        XYLog("DEBUG %s [STEP 6] FAIL: watchdog timer\n", __FUNCTION__);
        stopHalAndDrain();
        super::stop(pciNub);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    fWatchdogStopping = false;
    if (fWatchdogWorkLoop->addEventSource(watchdogTimer) !=
        kIOReturnSuccess) {
        XYLog("DEBUG %s [STEP 6] FAIL: watchdog event source\n",
              __FUNCTION__);
        stopHalAndDrain();
        super::stop(pciNub);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
#if __IO80211_TARGET >= __MAC_26_0
    fTahoeLqmStatsTimer = IOTimerEventSource::timerEventSource(
        this,
        OSMemberFunctionCast(IOTimerEventSource::Action, this,
                             &AirportItlwm::tahoeLqmStatsAction));
    if (fTahoeLqmStatsTimer == nullptr ||
        fWatchdogWorkLoop->addEventSource(fTahoeLqmStatsTimer) !=
            kIOReturnSuccess) {
        XYLog("DEBUG %s [STEP 6] FAIL: Tahoe LQM stats timer\n",
              __FUNCTION__);
        stopHalAndDrain();
        super::stop(pciNub);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    fTahoeLqmStatsTimer->disable();
#endif
    if (!setupScanSource(this, _fWorkloop)) {
        XYLog("DEBUG %s [STEP 6] FAIL: scan source\n", __FUNCTION__);
        stopHalAndDrain();
        super::stop(pciNub);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }

    if (!setupLinkStatePublishSource(this, _fWorkloop)) {
        XYLog("DEBUG %s [STEP 7] FAIL: link-state publish source alloc\n", __FUNCTION__);
        stopHalAndDrain();
        super::stop(pciNub);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }

#if __IO80211_TARGET >= __MAC_26_0
    if (!setupSaeTransportMailboxSource(this, _fWorkloop)) {
        XYLog("DEBUG %s [STEP 7] FAIL: SAE transport mailbox source alloc\n",
              __FUNCTION__);
        stopHalAndDrain();
        super::stop(pciNub);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
#endif

    SD_SET(10); // watchdog/scan timers OK
    sDiag.step = 7;
    fNetIf = new AirportItlwmSkywalkInterface;
#if __IO80211_TARGET >= __MAC_26_0
    if (!fNetIf->init()) {
        XYLog("DEBUG %s [STEP 7] FAIL: Skywalk interface no-arg init\n", __FUNCTION__);
        stopHalAndDrain();
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
        stopHalAndDrain();
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
#else
    (void)seedTahoeInitialMacAddress(
        fNetIf, fHalService->get80211Controller());
#endif

#if __IO80211_TARGET < __MAC_26_0
    if (!initCCLogs()) {
        XYLog("DEBUG %s [STEP 7] FAIL: CCLog init (pre-Tahoe)\n", __FUNCTION__);
        stopHalAndDrain();
        super::stop(provider);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
#endif
    if (!fNetIf->attach(this)) {
        XYLog("DEBUG %s [STEP 7] FAIL: fNetIf attach\n", __FUNCTION__);
        stopHalAndDrain();
        super::stop(provider);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    // This provider attachment is distinct from attachInterface(). If the
    // latter fails, releaseAll() must still pair this successful edge before
    // dropping fNetIf.
    fSkywalkInterfaceProviderAttached = true;
    SD_SET(12); // fNetIf attach OK
    sDiag.step = 8;
    if (!attachInterface(fNetIf, this)) {
        XYLog("DEBUG %s [STEP 8] FAIL: attachInterface\n", __FUNCTION__);
        stopHalAndDrain();
        super::stop(provider);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    // `attachInterface()` gives the framework a raw borrower path to the
    // Skywalk queue/pool inventory.  Every later cleanup must run its paired
    // detachInterface() fence before releasing that inventory.
    fSkywalkInterfaceAttached = true;
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
        stopHalAndDrain();
        super::stop(provider);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    RT3_SET(0); // initRegistrationInfo OK

    // mExpansionData / mExpansionData2 are managed by the framework's
    // initRegistrationInfo (called above at STEP 8).  Manual allocation
    // was removed because it wrote to the WRONG offsets when our class
    // size was 0x10 too small (0xD0 vs real 0xE0), corrupting framework state.
    // See plan: "Fix Skywalk Class Layout Mismatch (S1-S4 Architecture)".
    RT3_SET(1); // post-initRegistrationInfo check

    // Create Skywalk packet buffer pools for TX and RX
    sRT.startStep = 81;

    {
        IOSkywalkPacketBufferPool::PoolOptions poolOpts = {};
        poolOpts.packetCount = kAirportItlwmSkywalkQueueCapacity;
        poolOpts.bufferCount = kAirportItlwmSkywalkQueueCapacity;
        poolOpts.bufferSize  = SKYWALK_BUF_SIZE;
        poolOpts.maxBuffersPerPacket = 1;
        // poolFlags bit 0 is required for kIOSkywalkPacketTypeNetwork pools.
        // 1:1 with AppleBCMWLAN reference: AppleBCMWLANPCIeSkywalk::
        // allocSkywalkCommonResources @ 0xffffff80014ccd56 sets
        // local_30 = 0x100000000 in PoolOptions, i.e. memorySegmentSize=0
        // and poolFlags=1. Inside IOSkywalkPacketBufferPool::initWithName
        // (KDK IOSkywalkFamily.kext @ 0x9bf0) bit 0 of poolFlags maps to
        // kern_pbufpool_create flag bit 5 (shll $5 / andl $0x20). For
        // packetType=0 the framework auto-sets the LSB; for
        // packetType=Network (=1) it does not, so the caller must
        // supply this bit explicitly or kern_pbufpool_create rejects
        // the pool.
        poolOpts.poolFlags = 1;

        sRT.startStep = 811;
        fTxPool = AirportItlwmIO80211PacketPool::withName(
            "AirportItlwm-TX", fNetIf, &poolOpts);
        sRT.startStep = 812;
        fRxPool = AirportItlwmIO80211PacketPool::withName(
            "AirportItlwm-RX", fNetIf, &poolOpts);
        sRT.startStep = 813;
        if (!fTxPool || !fRxPool) {
            uint32_t poolFailMask =
                (fTxPool ? 0u : 1u) | (fRxPool ? 0u : 2u);
            const char *poolFailBranch =
                (poolFailMask == 1) ? "TX_ONLY" :
                (poolFailMask == 2) ? "RX_ONLY" : "TX_RX";
            sRT.startStep = 814;
            XYLog("itlwm: POOLTRACE[STEP8b] FINAL branch=%s "
                  "failMask=0x%x tx=0x%x_%x rx=0x%x_%x cleanup="
                  "super_stop_releaseAll_disarm_return_false\n",
                  poolFailBranch, poolFailMask,
                  AirportItlwmIO80211PacketPool::ptrHi32(fTxPool),
                  AirportItlwmIO80211PacketPool::ptrLo32(fTxPool),
                  AirportItlwmIO80211PacketPool::ptrHi32(fRxPool),
                  AirportItlwmIO80211PacketPool::ptrLo32(fRxPool));
            XYLog("DEBUG %s [STEP 8b] FAIL: pool creation (TX=%p RX=%p)\n",
                  __FUNCTION__, fTxPool, fRxPool);
            stopHalAndDrain();
            super::stop(provider);
            releaseAll();
            DISARM_PANIC_TIMER();
            return false;
        }
        sRT.startStep = 815;
    }
    RT3_SET(2); // pools created
    sRT.fTxPoolPtr = (uint64_t)(uintptr_t)fTxPool;
    sRT.fRxPoolPtr = (uint64_t)(uintptr_t)fRxPool;

    // Create the Wi-Fi Skywalk queue inventory. Reference AppleBCMWLAN exposes
    // distinct TX submission, TX completion, RX completion, and multicast work
    // source objects through IO80211SkywalkInterface accessors.
    fSkywalkTxQueueDepth = kAirportItlwmSkywalkQueueCapacity;
    fSkywalkRxQueueCapacity = kAirportItlwmSkywalkQueueCapacity;
    fTxQueue = IOSkywalkTxSubmissionQueue::withPool(fTxPool, fSkywalkTxQueueDepth, 0, this,
                                                    skywalkTxAction, NULL, 0);
    fTxCompQueue = IOSkywalkTxCompletionQueue::withPool(
        fTxPool, fSkywalkTxQueueDepth, 0, this, skywalkTxCompletionAction,
        NULL, 0);
    fRxQueue = IOSkywalkRxCompletionQueue::withPool(fRxPool, fSkywalkRxQueueCapacity, 0, this,
                                                    skywalkRxAction, NULL, 0);
    fMultiCastQueue = AirportItlwmSkywalkMulticastQueue::withInterface(fNetIf);
    if (!fTxQueue || !fTxCompQueue || !fRxQueue || !fMultiCastQueue) {
        uint32_t queueFailMask =
            (fTxQueue ? 0u : 1u) |
            (fTxCompQueue ? 0u : 2u) |
            (fRxQueue ? 0u : 4u) |
            (fMultiCastQueue ? 0u : 8u);
        XYLog("itlwm: POOLTRACE[STEP8c] FINAL branch=DOWNSTREAM_QUEUE_FAIL "
              "poolResult=BOTH_OK queueFailMask=0x%x "
              "TX=0x%llx TXC=0x%llx RX=0x%llx MC=0x%llx cleanup="
              "super_stop_releaseAll_disarm_return_false\n",
              queueFailMask,
              (unsigned long long)(uintptr_t)fTxQueue,
              (unsigned long long)(uintptr_t)fTxCompQueue,
              (unsigned long long)(uintptr_t)fRxQueue,
              (unsigned long long)(uintptr_t)fMultiCastQueue);
        XYLog("DEBUG %s [STEP 8c] FAIL: queue creation (TX=%p TXC=%p RX=%p MC=%p)\n",
              __FUNCTION__, fTxQueue, fTxCompQueue, fRxQueue, fMultiCastQueue);
        stopHalAndDrain();
        super::stop(provider);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    RT3_SET(3); // queues created
    sRT.fTxQueuePtr = (uint64_t)(uintptr_t)fTxQueue;
    sRT.fRxQueuePtr = (uint64_t)(uintptr_t)fRxQueue;

    // Skywalk queues are IOEventSource subclasses.  The RX completion queue's
    // enqueue path requires IOEventSource::workLoop to be set before runtime
    // packet delivery; register the queues with the same controller workloop
    // before handing them to the logical-link registration path.
    IOReturn txQueueWorkloopRet = _fWorkloop->addEventSource(fTxQueue);
    IOReturn txCompQueueWorkloopRet = _fWorkloop->addEventSource(fTxCompQueue);
    IOReturn rxQueueWorkloopRet = _fWorkloop->addEventSource(fRxQueue);
    IOReturn multicastQueueWorkloopRet = _fWorkloop->addEventSource(fMultiCastQueue);
    if (txQueueWorkloopRet != kIOReturnSuccess ||
        txCompQueueWorkloopRet != kIOReturnSuccess ||
        rxQueueWorkloopRet != kIOReturnSuccess ||
        multicastQueueWorkloopRet != kIOReturnSuccess) {
        uint32_t workloopFailMask =
            (txQueueWorkloopRet == kIOReturnSuccess ? 0u : 1u) |
            (txCompQueueWorkloopRet == kIOReturnSuccess ? 0u : 2u) |
            (rxQueueWorkloopRet == kIOReturnSuccess ? 0u : 4u) |
            (multicastQueueWorkloopRet == kIOReturnSuccess ? 0u : 8u);
        XYLog("itlwm: POOLTRACE[STEP8c-wl] FINAL "
              "branch=DOWNSTREAM_WORKLOOP_FAIL poolResult=BOTH_OK "
              "workloopFailMask=0x%x TX=0x%x TXC=0x%x RX=0x%x MC=0x%x "
              "cleanup=remove_successful_sources_super_stop_releaseAll_"
              "disarm_return_false\n",
              workloopFailMask, txQueueWorkloopRet, txCompQueueWorkloopRet,
              rxQueueWorkloopRet, multicastQueueWorkloopRet);
        XYLog("DEBUG %s [STEP 8c-wl] FAIL: queue workloop attach "
              "TX=0x%x TXC=0x%x RX=0x%x MC=0x%x\n",
              __FUNCTION__, txQueueWorkloopRet, txCompQueueWorkloopRet,
              rxQueueWorkloopRet, multicastQueueWorkloopRet);
        if (txQueueWorkloopRet == kIOReturnSuccess)
            _fWorkloop->removeEventSource(fTxQueue);
        if (txCompQueueWorkloopRet == kIOReturnSuccess)
            _fWorkloop->removeEventSource(fTxCompQueue);
        if (rxQueueWorkloopRet == kIOReturnSuccess)
            _fWorkloop->removeEventSource(fRxQueue);
        if (multicastQueueWorkloopRet == kIOReturnSuccess)
            _fWorkloop->removeEventSource(fMultiCastQueue);
        stopHalAndDrain();
        super::stop(provider);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }

    // Wire up Skywalk RX input handler on the internal ifnet before
    // registration, so received frames go through the Skywalk path
    // instead of the legacy IOEthernetInterface::inputPacket path.
#if __IO80211_TARGET >= __MAC_26_0
    {
        struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
        ifp->if_skywalk_rx = skywalkRxInput;
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
    }

    {
        IOSkywalkPacketQueue *queues[] = {
            (IOSkywalkPacketQueue *)fTxQueue,
            (IOSkywalkPacketQueue *)fRxQueue
        };
#if __IO80211_TARGET >= __MAC_26_0
        // Reference AppleBCMWLANSkywalkInterface enters the Wi-Fi infra shim,
        // which then delegates to the same underlying Ethernet registration.
        IOReturn regRet = static_cast<IO80211InfraInterface *>(fNetIf)
            ->registerInfraEthernetInterface(
                (IOSkywalkEthernetInterface::RegistrationInfo *)&registInfo,
                queues, 2, fTxPool, fRxPool);
#else
        IOReturn regRet = fNetIf->registerEthernetInterface(
            (const IOSkywalkEthernetInterface::RegistrationInfo *)&registInfo,
            queues, 2, fTxPool, fRxPool, 0);
#endif
        if (regRet != kIOReturnSuccess) {
            XYLog("DEBUG %s [STEP 8d] FAIL: Skywalk registration ret=0x%x\n", __FUNCTION__, regRet);
            stopHalAndDrain();
            super::stop(provider);
            releaseAll();
            DISARM_PANIC_TIMER();
            return false;
        }
    }
    RT3_SET(4); // Skywalk registration OK
    SD_SET(14);

    // Post-registration diagnostics: verify kernel populated nexusProvider
    // during Skywalk registration (via kernel Skywalk dispatcher
    // FUN_ffffff8000a6be70 → FUN_0xa37640 core registration body).
    // nexusProvider (+0xC8) must be non-NULL for BSDClient nexus creation.
    {
        uint8_t *raw = (uint8_t *)fNetIf;
        void *nexusProv  = *(void **)(raw + 0xC8);
        void *nexusArena = *(void **)(raw + 0xD0);
        void *asyncSent  = *(void **)(raw + 0xB8);
        sRT.nexusProvPtr  = (uint64_t)(uintptr_t)nexusProv;
        sRT.nexusArenaPtr = (uint64_t)(uintptr_t)nexusArena;
        sRT.asyncSentinel = (uint64_t)(uintptr_t)asyncSent;
        if (!nexusProv) {
            XYLog("DEBUG %s [POST-REG] WARNING: nexusProvider still NULL "
                  "after Skywalk registration — BSDClient nexus "
                  "creation will fail\n", __FUNCTION__);
        }
    }

    // Start the Skywalk interface
    sDiag.step = 81;
    RT3_SET(10); // entering fNetIf->start
    fNetIf->start(this);
    RT3_SET(11); // fNetIf->start returned
    SD_SET(15); // fNetIf->start OK

    // Trigger IOSkywalkNetworkBSDClient matching.
    // deferBSDAttach(false) removes IODeferBSDAttach property and calls
    // registerService() on fNetIf, causing IOKit to match BSDClient.
    // BSDClient::start creates the nexus channel and BSD ifnet.
    fNetIf->deferBSDAttach(false);
    {
        const char *bsdName = fNetIf->getBSDName();
        ifnet_t bsdIf = fNetIf->getBSDInterface();
        sRT.bsdIfPtr = (uint64_t)(uintptr_t)bsdIf;
        if (bsdIf) RT2_SET(0);
        if (bsdName && bsdName[0]) RT2_SET(1);
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
    thread_call_t bootCall = thread_call_allocate(
        (thread_call_func_t)handleTahoeBootChipImage,
        (thread_call_param_t)this);
    if (fTahoeBootCallLock != nullptr) {
        IOLockLock(fTahoeBootCallLock);
        const bool discardBootCall = fTahoeBootStopping ||
            tahoeBootThreadCall != nullptr;
        if (!discardBootCall) {
            tahoeBootThreadCall = bootCall;
            // This is a newly allocated call on a freshly starting
            // controller.  Once scheduleTahoeBootThreadCall() admits it,
            // fTahoeBootScheduled intentionally remains set for this
            // object's lifetime; fTahoeBootCallActive tracks only its one
            // pending/running invocation for safe teardown draining.
            fTahoeBootScheduled = false;
            fTahoeBootCallActive = false;
            fTahoeBootCallRetained = false;
            fTahoeBootCallOwner = nullptr;
        }
        IOLockUnlock(fTahoeBootCallLock);
        if (discardBootCall && bootCall != nullptr)
            (void)thread_call_free(bootCall);
    } else if (bootCall != nullptr) {
        (void)thread_call_free(bootCall);
    }
    sDiag.step = 9;
    SD_SET(16);
    sDiag.step = 10;
    // Do not admit an external selector until every source, workloop, and
    // Skywalk object above is fully initialized. A concurrent controller
    // stop changes the phase to Draining; in that case converge through the
    // ordinary failure teardown instead of publishing a half-live service.
    if (!markLifecycleLive()) {
        XYLog("DEBUG %s [STEP 9] FAIL: lifecycle stopped during start\n",
              __FUNCTION__);
        stopHalAndDrain();
        super::stop(provider);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    // registerService() makes the IO80211Controller visible to airportd and
    // triggers IOKit matching for AirportItlwmBootNub.  The BSD ifnet (en0)
    // is created asynchronously via the nexus callback chain triggered by
    // deferBSDAttach(false) at STEP 8f.
    registerService();
    RT_SET(18);
    DISARM_PANIC_TIMER();
#undef DISARM_PANIC_TIMER
#undef SD_SET

    iwx_auth_diag_init();

    return true;
}

void AirportItlwm::stop(IOService *provider)
{
    RT_SET(19);
    sRT.stopStep = 1;

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
                  "pmPol=%p pmGate=%u pmNoop=%u txDrop=%u "
                  "sysOff=%u sysOn=%u invalid=%u gateErr=%u | "
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
                  sRT.pmGateCount, sRT.pmNoopCount,
                  sRT.outputDropPwr,
                  sRT.pmSystemOffCnt, sRT.pmSystemOnCnt,
                  sRT.pmInvalidCount, sRT.pmGateErrorCnt,
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

    /*
     * Claim Draining before disableAdapter().  External selectors acquire an
     * operation user only while the phase is Live, so once this returns no
     * new association/scan/key operation can begin.  Wake command-gate PMK
     * waiters first, then wait already-admitted users before touching HAL.
     */
    if (!beginLifecycleDrain()) {
        // Another lifecycle owner completes the whole shared-resource path.
        // A follower must be a strict no-op after its wait: releaseAll() or
        // super::stop() here could free objects still used by that owner.
        thread_call_cancel(stopTimer);
        thread_call_free(stopTimer);
        return;
    }
    prepareLifecycleDrain();

    sRT.stopStep = 2;
    if (fHalService != nullptr) {
        disableAdapter(NULL);
        setLinkStatus(kIONetworkLinkValid);
    }
    sRT.stopStep = 3;
    sRT.stopStep = 4;
    // The current caller owns the Draining transition, so retain that claim
    // through producer removal and HAL detach rather than re-entering it.
    stopHalAndDrainClaimed();
    sRT.stopStep = 5;
    // releaseAll() owns the common reverse order for full and partial starts:
    // queue sources stopped -> optional ether_ifdetach -> detachInterface ->
    // queue/pool release -> fNetIf release.  Keeping it in one owner avoids
    // a duplicate BSD detach on the normal stop path.
    releaseAll(false);
    sRT.stopStep = 9;
    super::stop(provider);
    sRT.stopStep = 10;
    finishLifecycleDrain();
    thread_call_cancel(stopTimer);
    thread_call_free(stopTimer);
}

void AirportItlwm::free()
{
    RT_SET(20);
    sRT.freeStep = 1;

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
                      sRT.pmGateCount, sRT.pmNoopCount,
                      sRT.outputDropPwr,
                      sRT.pmSystemOffCnt, sRT.pmSystemOnCnt,
                      sRT.pmInvalidCount, sRT.pmGateErrorCnt,
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
    // free() is a fallback lifecycle edge for partially started controllers.
    // It must not release a HAL that still owns an attach or a callback.  A
    // lifecycle follower is fail-closed: another free owner has set the final
    // barrier, so this invocation must not touch shared state or call the
    // superclass a second time.
    bool lifecycleOwner = lifecycleDrainOwnedByCurrentThread();
    if (!lifecycleOwner)
        lifecycleOwner = beginLifecycleDrain();
    if (!lifecycleOwner && lifecycleFinalizing()) {
        thread_call_cancel(freeTimer);
        thread_call_free(freeTimer);
        return;
    }
    if (lifecycleOwner)
        releaseAll(false);

    // In the owner case this is set before finishLifecycleDrain() wakes any
    // beginLifecycleDrain() follower.  After a prior normal stop it closes
    // the same ingress before free-only allocations are released.
    beginLifecycleFinalization();
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
    skywalkTxDrainCompletionPackets(this);
    skywalkRxDrainPendingPackets(this);
    if (fTxCompletionPendingLock != NULL) {
        IOLockFree(fTxCompletionPendingLock);
        fTxCompletionPendingLock = NULL;
    }
    if (fRxPendingLock != NULL) {
        IOLockFree(fRxPendingLock);
        fRxPendingLock = NULL;
    }
    if (fTahoeBootCallLock != nullptr) {
        IOLockLock(fTahoeBootCallLock);
        const bool canFreeBootCallLock =
            tahoeBootThreadCall == nullptr && !fTahoeBootCallActive &&
            !fTahoeBootCallRetained && fTahoeBootCallOwner == nullptr;
        IOLockUnlock(fTahoeBootCallLock);
        if (canFreeBootCallLock) {
            IOLockFree(fTahoeBootCallLock);
            fTahoeBootCallLock = nullptr;
        }
    }
    if (lifecycleOwner)
        finishLifecycleDrain();
    // finishLifecycleDrain() may have woken followers out of
    // IOLockSleep(fLifecycleLock). They decrement this count only after they
    // have reacquired the control lock, so wait before freeing that lock.
    waitForLifecycleDrainWaiters();
    if (fLinkStatePublishLifecycle.admissionLock != NULL) {
        IOSimpleLockFree(fLinkStatePublishLifecycle.admissionLock);
        fLinkStatePublishLifecycle.admissionLock = NULL;
    }
    if (fScanSourceLifecycle.admissionLock != NULL) {
        IOSimpleLockFree(fScanSourceLifecycle.admissionLock);
        fScanSourceLifecycle.admissionLock = NULL;
    }
#if __IO80211_TARGET >= __MAC_26_0
    if (fSaeTransportMailbox.admissionLock != NULL) {
        IOSimpleLockFree(fSaeTransportMailbox.admissionLock);
        fSaeTransportMailbox.admissionLock = NULL;
    }
#endif
    if (fLifecycleAdmissionLock != NULL) {
        IOSimpleLockFree(fLifecycleAdmissionLock);
        fLifecycleAdmissionLock = NULL;
    }
    if (fLifecycleLock != NULL) {
        IOLockFree(fLifecycleLock);
        fLifecycleLock = NULL;
    }
    sRT.freeStep = 4;
    super::free();
    RT_SET(21);
    thread_call_cancel(freeTimer);
    thread_call_free(freeTimer);
}

bool AirportItlwm::createWorkQueue()
{
    _fWorkloop = IO80211WorkQueue::workQueue();
    RT_SET(16);
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
    // configureInterface is a framework bootstrap callback and can precede
    // markLifecycleLive(), but it must not race a failed-start/stop teardown.
    AirportItlwmControllerLifecycleOperationGuard lifecycle(this, true);
    if (!lifecycle.admitted() || fHalService == nullptr)
        return false;

    RT_SET(17);
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
    ether_ifattach(ifp, OSDynamicCast(IOEthernetInterface, netif));
    // attachInterface() can succeed before configureInterface() reaches this
    // BSD edge. Track its inverse separately so a partial start neither skips
    // a real ether_ifdetach() nor performs one before ether_ifattach().
    fSkywalkEthernetAttached = true;
    RT_SET(26);
    fpNetStats->collisions = 0;

#if defined(__PRIVATE_SPI__) && __IO80211_TARGET < __MAC_26_0
    netif->configureOutputPullModel(fHalService->getDriverInfo()->getTxQueueSize(), 0, 0, IOEthernetInterface::kOutputPacketSchedulingModelNormal, 0);
#endif
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
    (void)speed;
    const UInt32 previousStatus = currentStatus;
#if __IO80211_TARGET >= __MAC_26_0
    recordTahoeLinkContext(
        kAirportItlwmRegDiagLinkContextControllerStatus,
        kAirportItlwmRegDiagLinkContextEnter, 0, status, previousStatus,
        kAirportItlwmRegDiagLinkContextLifecycleUnknown, kIOReturnSuccess,
        0, -1);
#endif
    if (status == previousStatus) {
        airportItlwmRegDiagRecordLinkStatus(
            kAirportItlwmRegDiagLinkStatusSame, previousStatus, status,
            kIOReturnSuccess);
#if __IO80211_TARGET >= __MAC_26_0
        recordTahoeLinkContext(
            kAirportItlwmRegDiagLinkContextControllerStatus,
            kAirportItlwmRegDiagLinkContextSameStatus, 0, status,
            previousStatus,
            kAirportItlwmRegDiagLinkContextLifecycleControllerSame,
            kIOReturnSuccess, 0, -1);
#endif
        return true;
    }

    // Base status handling may itself consult the bound interface. Admit a
    // normal Starting|Live callback before entering it. The one lifecycle
    // drain owner is allowed to complete that base transition for stop(), but
    // is kept out of every driver-local interface/HAL side effect below.
    const bool drainOwner = lifecycleDrainOwnedByCurrentThread();
    AirportItlwmControllerLifecycleOperationGuard lifecycle(this, true);
    if (!lifecycle.admitted() && !drainOwner) {
        airportItlwmRegDiagRecordLinkStatus(
            kAirportItlwmRegDiagLinkStatusLifecycleRejected, previousStatus,
            status, kIOReturnNotReady);
#if __IO80211_TARGET >= __MAC_26_0
        recordTahoeLinkContext(
            kAirportItlwmRegDiagLinkContextControllerStatus,
            kAirportItlwmRegDiagLinkContextLifecycleRejected, 0, status,
            previousStatus,
            kAirportItlwmRegDiagLinkContextLifecycleControllerRejected,
            kIOReturnNotReady, 0, -1);
#endif
        return false;
    }

    bool ret = super::setLinkStatus(status, activeMedium, speed, data);
    currentStatus = status;
    airportItlwmRegDiagRecordLinkStatus(
        kAirportItlwmRegDiagLinkStatusApplied, previousStatus, status,
        ret ? kIOReturnSuccess : kIOReturnError);
#if __IO80211_TARGET >= __MAC_26_0
    recordTahoeLinkContext(
        kAirportItlwmRegDiagLinkContextControllerStatus,
        kAirportItlwmRegDiagLinkContextBaseApplied, 0, status, status,
        drainOwner ? kAirportItlwmRegDiagLinkContextLifecycleControllerDrainOwner :
                     kAirportItlwmRegDiagLinkContextLifecycleControllerAdmitted,
        ret ? kIOReturnSuccess : kIOReturnError,
        AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_EPOCH_CURRENT, -1);
#endif

    // In the stop owner's Draining phase complete only the base transition.
    // A late non-owner was rejected before super; neither path can touch
    // fNetIf, the publish source, or lower queues after releaseAll retires
    // those owners.
    if (!lifecycle.admitted())
        return ret;

    if (fNetIf) {
        if (status & kIONetworkLinkActive) {
#if defined(__PRIVATE_SPI__) && __IO80211_TARGET < __MAC_26_0
            bsdInterface->startOutputThread();
#endif
#if __IO80211_TARGET >= __MAC_26_0
            publishTahoeSkywalkLinkCarrier(fNetIf, true);
#endif
            queueOffGateLinkStatePublish(this, kIO80211NetworkLinkUp, 0);
        } else if (!(status & kIONetworkLinkNoNetworkChange)) {
#if defined(__PRIVATE_SPI__) && __IO80211_TARGET < __MAC_26_0
            bsdInterface->stopOutputThread();
            bsdInterface->flushOutputQueue();
#endif
            // The enclosing Starting|Live admission keeps this lower queue
            // flush/deauth snapshot from racing releaseAll().
            uint16_t deauthReason = 0;
            if (fHalService != nullptr) {
                struct ieee80211com *ic = fHalService->get80211Controller();
                if (ic != nullptr) {
                    ifq_flush(&ic->ic_ac.ac_if.if_snd);
                    mq_purge(&ic->ic_mgtq);
                    deauthReason = ic->ic_deauth_reason;
                }
            }
#if __IO80211_TARGET >= __MAC_26_0
            publishTahoeSkywalkLinkCarrier(fNetIf, false);
#endif
            queueOffGateLinkStatePublish(this, kIO80211NetworkLinkDown,
                                         deauthReason);
        }
    }
    return ret;
}

IOReturn AirportItlwm::
setLinkStateGated(OSObject *target, void *arg0, void *arg1, void *arg2, void *arg3)
{
    RT_SET(7);
    const IO80211LinkState linkState =
        static_cast<IO80211LinkState>((uint64_t)arg0);
    const unsigned int rawCode = static_cast<unsigned int>((uint64_t)arg1);
    sRT.lastLinkState = static_cast<int>((uint64_t)arg0);
    sRT.linkSetCount++;
    AirportItlwm *that = OSDynamicCast(AirportItlwm, target);
    if (that == NULL) {
        XYLog("DEBUG %s skipped: null target\n", __FUNCTION__);
        return kIOReturnNotReady;
    }
#if __IO80211_TARGET >= __MAC_26_0
    /*
     * Off-gate publication precondition guard, evaluated BEFORE any publication
     * side effect. The inherited IO80211 publication path reaches
     * IO80211Glue::sendIOUCToWcl, which requires the IO80211 work-queue serial
     * owner to be on its own thread (onThread() == true) with the work-loop gate
     * released (inGate() == false); otherwise it takes the null-owner panic
     * branch. If the off-gate route did not reach this point with that
     * precondition satisfied, perform NO WCL/IO80211 link-state publication at
     * all (no WCL link-up indication, inherited setLinkState, setRunningState,
     * connect-complete, or postMessage) and return kIOReturnNotReady. This is a
     * precondition guard, not retry/replay/masking/forced-success: when the
     * precondition fails the link is simply not published (the negative branch).
     */
    if (that->fNetIf == NULL) {
        XYLog("DEBUG %s skipped: null fNetIf\n", __FUNCTION__);
        that->recordTahoeLinkContext(
            kAirportItlwmRegDiagLinkContextGate,
            kAirportItlwmRegDiagLinkContextActionUnavailable,
            static_cast<uint32_t>(linkState), rawCode,
            AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_STATUS_UNAVAILABLE,
            kAirportItlwmRegDiagLinkContextLifecyclePublicationUnavailable,
            kIOReturnNotReady, 0, -1);
        airportItlwmRegDiagRecordLinkPublish(
            kAirportItlwmRegDiagLinkPublishActionUnavailable,
            static_cast<uint32_t>(linkState), rawCode, kIOReturnNotReady);
        return kIOReturnNotReady;
    }
    {
        IOWorkLoop *publishWorkLoop = that->getWorkLoop();
        const int onThreadPred =
            publishWorkLoop ? (publishWorkLoop->onThread() ? 1 : 0) : -1;
        const int inGatePred =
            publishWorkLoop ? (publishWorkLoop->inGate() ? 1 : 0) : -1;
        const bool offGateOwner = onThreadPred == 1 && inGatePred == 0;
        if (airportItlwmRegDiagShouldRecordLinkContext()) {
            airportItlwmRegDiagRecordLinkContext(
                kAirportItlwmRegDiagLinkContextGate,
                offGateOwner ? kAirportItlwmRegDiagLinkContextGateReady :
                               kAirportItlwmRegDiagLinkContextGateRejected,
                static_cast<uint32_t>(linkState), rawCode,
                AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_STATUS_UNAVAILABLE,
                kAirportItlwmRegDiagLinkContextLifecyclePublicationReady,
                that->currentTahoeAssociationEpoch(), onThreadPred, inGatePred,
                -1, offGateOwner ? kIOReturnSuccess : kIOReturnNotReady);
        }
        if (!offGateOwner) {
            const uint32_t predicates =
                (onThreadPred == 1 ? 0x1U : 0U) |
                (inGatePred == 1 ? 0x2U : 0U);
            airportItlwmRegDiagRecordLinkPublish(
                kAirportItlwmRegDiagLinkPublishOffGateRejected,
                static_cast<uint32_t>(linkState), predicates,
                kIOReturnNotReady);
            return kIOReturnNotReady;
        }
    }
    const unsigned int setLinkCode =
        (linkState == kIO80211NetworkLinkUp) ? 1U : rawCode;
    if (linkState == kIO80211NetworkLinkUp) {
        postTahoeWclLinkUpInd(that, rawCode);
    }
    // The off-gate precondition (onThread==1, inGate==0) was guarded at the top
    // of this publication path; reaching here means it holds, so the inherited
    // publication is safe to invoke.
    // Tahoe's inherited selector returns bool, not IOReturn: true means the
    // parent accepted the transition.  Keep that ABI value distinct and
    // normalize it at this IOReturn callback boundary so diagnostics and the
    // legacy caller do not invert accepted and rejected transitions.
    const bool linkTransitionAccepted =
        ((IO80211InfraInterface *)that->fNetIf)->setLinkState(
            linkState, setLinkCode, false, 0, 0);
    const IOReturn ret = linkTransitionAccepted ? kIOReturnSuccess
                                                 : kIOReturnError;
    that->recordTahoeLinkContext(
        kAirportItlwmRegDiagLinkContextGate,
        linkTransitionAccepted ? kAirportItlwmRegDiagLinkContextParentAccepted :
                                 kAirportItlwmRegDiagLinkContextParentRejected,
        static_cast<uint32_t>(linkState), setLinkCode,
        AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_STATUS_UNAVAILABLE,
        linkTransitionAccepted ?
            kAirportItlwmRegDiagLinkContextLifecycleParentAccepted :
            kAirportItlwmRegDiagLinkContextLifecycleParentRejected,
        ret, AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_EPOCH_CURRENT, -1);
    airportItlwmRegDiagRecordLinkPublish(
        kAirportItlwmRegDiagLinkPublishPublished,
        static_cast<uint32_t>(linkState), setLinkCode, ret);
#else
    const IOReturn ret = that->fNetIf->setLinkState(linkState, rawCode);
#endif
#if __IO80211_TARGET >= __MAC_26_0
    if (linkTransitionAccepted) RT_SET(14);
#else
    if (ret == kIOReturnSuccess) RT_SET(14);
#endif
    if (airportItlwmRegDiagEnabled(kAirportItlwmRegDiagModeControl)) {
        sRegDiag.snapshot.linkStateCount++;
        sRegDiag.snapshot.lastLinkStateResult = static_cast<int32_t>(ret);
        sRegDiag.snapshot.lastLinkState = static_cast<int32_t>((uint64_t)arg0);
        airportItlwmRegDiagTrace(kAirportItlwmRegDiagTraceLinkState,
                                 kAirportItlwmRegDiagPathLink, ret,
                                 static_cast<int32_t>((uint64_t)arg0),
                                 static_cast<uint64_t>((uint64_t)arg1),
#if __IO80211_TARGET >= __MAC_26_0
                                 linkTransitionAccepted ? 1U : 0U);
#else
                                 AIRPORT_ITLWM_REGDIAG_LINK_STATE_PARENT_ACCEPTED_UNAVAILABLE);
#endif
    }
    RT2_SET(13);
    that->fNetIf->setRunningState(linkState == kIO80211NetworkLinkUp);
#if __IO80211_TARGET >= __MAC_26_0
    /*
     * Tahoe legacy event-publication ownership.
     *
     * The 32-byte APPLE80211_M_LINK_CHANGED carrier that Apple's userspace
     * event handler `__setupEventHandlersWithInterfaceName:` length-checks
     * against `sizeof(apple80211_link_changed_event_data) == 0x20` is
     * published exactly once per accepted link-state transition from the
     * Tahoe Skywalk `AirportItlwmSkywalkInterface::setLinkStateInternal`
     * override. `setLinkStateGated` reaches that override through the
     * preceding `((IO80211InfraInterface *)fNetIf)->setLinkState(...)`
     * call; the override emits the recovered 32-byte payload on the parent
     * transition's success edge, so re-publishing here would deliver the
     * same userspace event twice for a single accepted transition.
     *
     * APPLE80211_M_SSID_CHANGED has a separate Apple producer:
     * `AppleBCMWLANCore::handleSetSSIDEvent` publishes the 8-byte successful
     * status/reason carrier before handing the SET_SSID event to
     * `AppleBCMWLANJoinAdapter::handleSetSSID`. The recovered airportd
     * `ssidChanged` block schedules a fresh `__associatedNetwork` read and
     * forwards that object through `setAssociatedNetwork:`. The local
     * net80211 bridge has no JoinAdapter, so the corresponding accepted join
     * edge is the Tahoe Skywalk `setLinkStateInternal` parent-success link-up
     * transition, after the inherited IO80211 state change has been accepted.
     *
     * APPLE80211_M_BSSID_CHANGED has a recovered Apple writer on the
     * WCL/IOUC side (selector 0x1b1) that produces a populated 24-byte
     * BSSID-changed compact carrier with the BSSID at offset 0x00 and
     * the reason at offset 0x14; see
     * struct apple80211_bssid_changed_event_data. Tahoe userspace
     * length-checks this carrier (prior zero-length publication
     * produced an `expected=24 actual=0` CoreWiFi rejection), so the
     * Tahoe branch must not republish APPLE80211_M_BSSID_CHANGED with
     * a NULL/0 payload. The accepted join-up path publishes the populated
     * carrier from the current associated BSS through
     * `publishTahoeAcceptedJoinIdentityEvents` on the same parent-success
     * link-up transition that publishes SSID_CHANGED. The Tahoe Skywalk
     * `setCurrentApAddress` override remains only a passive parent cache/rate
     * hook and is not a local event-3 producer. The legacy zero-length BSSID
     * notify remains only in the pre-Tahoe branch below.
     */
#else
    that->postMessage(that->fNetIf, APPLE80211_M_LINK_CHANGED, NULL, 0, true);
    that->postMessage(that->fNetIf, APPLE80211_M_BSSID_CHANGED, NULL, 0, true);
    that->postMessage(that->fNetIf, APPLE80211_M_SSID_CHANGED, NULL, 0, true);
#endif
#if __IO80211_TARGET < __MAC_26_0
    if (linkState != kIO80211NetworkLinkUp)
        that->fNetIf->reportLinkStatus(1, 0);
    if (that->bsdInterface) {
        XYLog("DEBUG %s calling bsdInterface->setLinkState bsdInterface=%p\n", __FUNCTION__, that->bsdInterface);
        that->bsdInterface->setLinkState(linkState);
    }
#endif
    return ret;
}

#if defined(__PRIVATE_SPI__) && __IO80211_TARGET < __MAC_26_0
IOReturn AirportItlwm::outputStart(IONetworkInterface *interface, IOOptionBits options)
{
    // Output callbacks are externally scheduled. Keep the controller/HAL
    // alive through the queue probes as well as through outputPacket().
    AirportItlwmControllerLifecycleOperationGuard lifecycle(this, false);
    if (!lifecycle.admitted() || fHalService == nullptr)
        return kIOReturnNoResources;

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

UInt32 AirportItlwm::outputPacket(mbuf_t m, void *param)
{
    // The network stack may enter this virtual after stop has detached the
    // HAL but before it has retired its output callback. Admission is held
    // across every fHalService/ifnet use below, and rejected packets are
    // consumed exactly as the existing power-off path consumes them.
    AirportItlwmControllerLifecycleOperationGuard lifecycle(this, false);
    if (!lifecycle.admitted() || fHalService == nullptr) {
        if (m != nullptr && mbuf_type(m) != MBUF_TYPE_FREE)
            freePacket(m);
        return kIOReturnOutputDropped;
    }

    IOReturn ret = kIOReturnOutputSuccess;
    uint32_t diagLength = 0;
    bool diagEapol = false;
    if (airportItlwmRegDiagPacketProbeEnabled())
        diagEapol = airportItlwmRegDiagMbufIsEapol(m, &diagLength);

    if ((pmPowerStateFlags & kAirportItlwmPmSystemOnBit) == 0) {
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
    // IOEthernetController can ask for descriptive strings while the
    // interface is being constructed. Starting is permitted, Draining is
    // not; avoid dereferencing a released lower service on a late query.
    AirportItlwmControllerLifecycleOperationGuard lifecycle(
        const_cast<AirportItlwm *>(this), true);
    if (!lifecycle.admitted() || fHalService == nullptr)
        return OSString::withCString("AirportItlwm");
    return OSString::withCString(fHalService->getDriverInfo()->getFirmwareName());
}

IOReturn AirportItlwm::getHardwareAddress(IOEthernetAddress *addrP)
{
    AirportItlwmControllerLifecycleOperationGuard lifecycle(this, true);
    if (!lifecycle.admitted() || fHalService == nullptr || addrP == nullptr)
        return kIOReturnNotReady;

    if (IEEE80211_ADDR_EQ(etheranyaddr, fHalService->get80211Controller()->ic_myaddr))
        return kIOReturnError;
    else {
        IEEE80211_ADDR_COPY(addrP, fHalService->get80211Controller()->ic_myaddr);
        return kIOReturnSuccess;
    }
}

IOReturn AirportItlwm::setHardwareAddress(const void *addrP, UInt32 addrBytes)
{
    // MAC changes are post-Live external requests. Do not let one restart the
    // HAL after stop has claimed Draining.
    AirportItlwmControllerLifecycleOperationGuard lifecycle(this, false);
    if (!lifecycle.admitted() || fHalService == nullptr)
        return kIOReturnNotReady;
    if (!fNetIf || !addrP)
        return kIOReturnError;
#if __IO80211_TARGET >= __MAC_26_0
    /* Invalidate an old-station SAE request before changing its identity. */
    cancelSaeRelay("setHardwareAddress", false);
#endif
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
    AirportItlwmControllerLifecycleOperationGuard lifecycle(
        const_cast<AirportItlwm *>(this), true);
    if (!lifecycle.admitted() || fHalService == nullptr)
        return 0;
    return fHalService->getDriverInfo()->supportedFeatures();
}

IOReturn AirportItlwm::setPromiscuousMode(IOEnetPromiscuousMode mode)
{
    tahoeOwnerRegistry.controller.promiscuousMode = mode != 0;
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::setMulticastMode(IOEnetMulticastMode mode)
{
    tahoeOwnerRegistry.controller.multicastMode = mode != 0;
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::setMulticastList(IOEthernetAddress* addr, UInt32 len)
{
    // This IOEthernetController virtual bypasses the raw Skywalk selector
    // dispatcher and reaches iwx_send_cmd_pdu directly. Hold a Live user
    // before mutating the mirrored state or touching the lower controller.
    AirportItlwmControllerLifecycleOperationGuard lifecycle(this, false);
    if (!lifecycle.admitted() || fHalService == nullptr)
        return kIOReturnNotReady;

    if (len > TahoeControllerContracts::kMulticastMaxEntries)
        return static_cast<IOReturn>(TahoeControllerContracts::kErrorStatus);

    tahoeOwnerRegistry.controller.multicastCount = len;
    memset(tahoeOwnerRegistry.controller.multicastList, 0,
           sizeof(tahoeOwnerRegistry.controller.multicastList));
    if (addr != nullptr && len != 0) {
        memcpy(tahoeOwnerRegistry.controller.multicastList, addr,
               len * TahoeControllerContracts::kMulticastAddressLength);
    }
    return fHalService->getDriverController()->setMulticastList(addr, len);
}

UInt32 AirportItlwm::getDataQueueDepth(OSObject *)
{
    return tahoeOwnerRegistry.controller.dataQueueDepth;
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
    if (code == kIO80211Feature80211n) {
        return 0;
    }
    return 102;
}

#if __IO80211_TARGET >= __MAC_26_0
void *AirportItlwm::releaseFlowQueue(IO80211FlowQueue *)
{
    tahoeOwnerRegistry.hiddenInterface.flowQueueReleaseCount++;
    return nullptr;
}

// vtable dump[431] = getDriverLogStream (pure virtual) at vptr+0xD68.
// IO80211Controller::start() calls this to get CCLogStream* for setGlobalLogger(),
// then createIOReporters() uses it.  Must return valid CCLogStream*.
// (Dump indexes include 2 RTTI entries, so vptr offset = (431-2)*8 = 0xD68.)
void *AirportItlwm::getDriverLogStream()
{
    return driverLogStream;
}
#endif

bool AirportItlwm::getLogPipes(CCPipe**logPipe, CCPipe**eventPipe, CCPipe**snapshotsPipe)
{
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
    // Tahoe AppleBCMWLANCore writes advanced capability bytes through offset
    // +0x17. The old short local header only zeroed the prefix and leaked
    // uninitialized tail bytes into IO80211Family/WCL, which could advertise
    // arbitrary advanced AKM/capability state on hidden join paths.
    memset(cd, 0, sizeof(struct apple80211_capability_data));

    cd->version = APPLE80211_VERSION;
    // AppleBCMWLANCore seeds cap[0..1] from fixed request-capability bytes,
    // not from the local net80211 ic_caps mask. CoreWiFi gates current-link
    // properties directly on those request bits before it asks the driver.
    // Keep the advanced-byte sanitation from CR-032 in the same helper.
    TahoeCapabilityContracts::applyAppleConsistentCardCapabilityCluster(
        cd->capabilities);
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
    /*
     * Apple keeps a real current-country state and airportd also derives
     * 802.11d country codes from scan cache. When firmware only exposes the
     * local "ZZ" fallback, prefer the associated BSS' 802.11d alpha2 carrier
     * before falling back to a geolocation value or the firmware placeholder.
     */
    AirportItlwmCountryCode::selectCountryCode(
        fHalService, user_override_cc, cc_fw, geo_location_cc, cd->cc);
    if (fNetIf != nullptr) {
        fNetIf->setProperty(APPLE80211_REGKEY_COUNTRY_CODE,
                            reinterpret_cast<const char *>(cd->cc));
        fNetIf->setProperty(
            APPLE80211_REGKEY_LOCALE,
            AirportItlwmCountryCode::localePropertyString(APPLE80211_LOCALE_FCC));
    }
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
    if (data && data->cc[0] != 120 && data->cc[0] != 88) {
        uint8_t normalizedCc[APPLE80211_MAX_CC_LEN];
        AirportItlwmCountryCode::copyAlpha2(
            normalizedCc, reinterpret_cast<const char *>(data->cc));
        memcpy(geo_location_cc, normalizedCc, sizeof(geo_location_cc));
        if (fNetIf != nullptr) {
            fNetIf->setProperty(APPLE80211_REGKEY_COUNTRY_CODE,
                                reinterpret_cast<const char *>(normalizedCc));
            fNetIf->setProperty(
                APPLE80211_REGKEY_LOCALE,
                AirportItlwmCountryCode::localePropertyString(APPLE80211_LOCALE_FCC));
        }
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
    if (ic->ic_state == IEEE80211_S_RUN && ic->ic_bss != NULL &&
        ic->ic_bss->ni_esslen <= APPLE80211_MAX_SSID_LEN) {
        sd->ssid_len = ic->ic_bss->ni_esslen;
        if (sd->ssid_len != 0)
            memcpy(sd->ssid_bytes, ic->ic_bss->ni_essid, sd->ssid_len);
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
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setPOWER(OSObject *object,
                         struct apple80211_power_data *pd)
{
    RT2_SET(6);
    if (!pd)
        return kIOReturnError;
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
static bool isTahoeCurrentLinkProbeSelector(unsigned long cmd)
{
    switch (cmd) {
        case APPLE80211_IOC_SSID:
        case APPLE80211_IOC_BSSID:
        case APPLE80211_IOC_SCAN_RESULT:
        case APPLE80211_IOC_CURRENT_NETWORK:
            return true;
        default:
            return false;
    }
}

SInt32 AirportItlwm::apple80211_ioctl(IO80211SkywalkInterface *interface,unsigned long cmd,void *data, bool b1, bool b2)
{
    if (isTahoeCurrentLinkProbeSelector(cmd)) {
        XYLog("DEBUG %s probe cmd=%s(%lu) interface=%p data=%p b1=%d b2=%d ic_state=%d interrupt=%d\n",
              __FUNCTION__, convertApple80211IOCTLToString((unsigned int)cmd), cmd,
              interface, data, b1 ? 1 : 0, b2 ? 1 : 0,
              fHalService ? fHalService->get80211Controller()->ic_state : -1,
              ml_at_interrupt_context() ? 1 : 0);
    }
    if (!ml_at_interrupt_context())
        XYLog("%s cmd: %s b1: %d b2: %d\n", __FUNCTION__, convertApple80211IOCTLToString((unsigned int)cmd), b1, b2);
    return super::apple80211_ioctl(interface, cmd, data, b1, b2);
}
#endif

SInt32 AirportItlwm::handleCardSpecific(IO80211SkywalkInterface *interface,unsigned long cmd,void *data,bool isSet)
{
    // Keep the carried Tahoe card-specific bridge for the visible set-side and
    // hidden-association selectors that arrive on this controller seam. Public
    // current-link selectors stay owned by the BSD Apple80211 dispatcher; slot
    // `[411] isCommandProhibited(int)` is not a public payload ingress.
    if (data != nullptr && interface != nullptr) {
        apple80211req req;
        bzero(&req, sizeof(req));
        req.req_type = (UInt)cmd;
        req.req_data = data;
        // The card-specific virtual does not carry apple80211req::req_len.
        // Preserve Tahoe's recovered compact CopyValue lengths before entering
        // the shared BSD bridge so compact carriers cannot fall through to
        // legacy versioned-struct writers with an unknown destination size.
        if (!isSet) {
            switch (cmd) {
                case APPLE80211_IOC_SSID:
                    req.req_len = APPLE80211_MAX_SSID_LEN;
                    break;
                case APPLE80211_IOC_BSSID:
                    req.req_len = APPLE80211_ADDR_LEN;
                    break;
                case APPLE80211_IOC_CARD_CAPABILITIES:
                    req.req_len =
                        TahoeCapabilityContracts::kApple80211BindCardCapabilitiesLength;
                    break;
                case APPLE80211_IOC_VIRTUAL_IF_ROLE:
                    req.req_len = sizeof(uint32_t);
                    break;
                case APPLE80211_IOC_VIRTUAL_IF_PARENT:
                    req.req_len = IFNAMSIZ;
                    break;
                default:
                    break;
            }
        }
        IOReturn ret = routeTahoeSkywalkIoctl(interface, &req,
                                              isSet ? SIOCSA80211 : SIOCGA80211);
        if (ret != kIOReturnUnsupported)
            return ret;
    }

    return kIOReturnUnsupported;
}

IOReturn AirportItlwm::enableAdapter(IONetworkInterface *netif)
{
    // Startup and power-management paths both converge here. They may run
    // while Starting, but a stale power-on must not reopen or dereference the
    // HAL after Draining has been claimed.
    AirportItlwmControllerLifecycleOperationGuard lifecycle(this, true);
    if (!lifecycle.admitted() || fHalService == nullptr)
        return kIOReturnNotReady;

    RT_SET(9);
    sRT.enableCnt++;
#if __IO80211_TARGET >= __MAC_26_0
    if (fTxCompQueue)
        fTxCompQueue->enable();
    if (fRxQueue)
        fRxQueue->enable();
    if (fTxQueue)
        fTxQueue->enable();
#endif
    fHalService->enable(netif);
    if (!fWatchdogStopping && watchdogTimer) {
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
#if __IO80211_TARGET >= __MAC_26_0
    stopTahoeLqmStatsTimer();
#endif
    if (watchdogTimer) {
        watchdogTimer->cancelTimeout();
        watchdogTimer->disable();
    }
#if __IO80211_TARGET >= __MAC_26_0
    if (fTxQueue)
        fTxQueue->disable();
    if (fMultiCastQueue)
        fMultiCastQueue->disable();
    if (fRxQueue)
        fRxQueue->disable();
    if (fTxCompQueue)
        fTxCompQueue->disable();

    skywalkTxDrainCompletionPackets(this);
    skywalkRxDrainPendingPackets(this);
#endif
    if (fHalService) {
#if __IO80211_TARGET >= __MAC_26_0
        /* Cancel while HAL is still live; disable() may reset TX rings. */
        cancelSaeRelay("disableAdapterCore", false);
#endif
        fHalService->disable(netif);
    }
}

void AirportItlwm::disableAdapter(IONetworkInterface *netif)
{
    postTahoeDriverAvailabilityTransition(
        this, TahoeDriverAvailabilityContracts::Transition::PowerOff);
    disableAdapterCore(netif);
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

    if ((newState == kWiFiPowerOff && prevState == kWiFiPowerOn) ||
        (newState == kWiFiPowerOff && prevState == kWiFiPowerStandby)) {
        // ON→OFF or STANDBY→OFF: power off
        power_state = kWiFiPowerOff;
        postTahoeDriverAvailabilityTransition(
            this, TahoeDriverAvailabilityContracts::Transition::PowerOff);
        disableAdapterCore(netif);
    }
    else if (newState == kWiFiPowerOn && (prevState == kWiFiPowerOff || prevState == kWiFiPowerStandby)) {
        // OFF→ON or STANDBY→ON: power on
        power_state = kWiFiPowerOn;
        err = enableAdapter(netif);
        if (err == kIOReturnSuccess) {
            postTahoeDriverAvailabilityTransition(
                this, TahoeDriverAvailabilityContracts::Transition::PowerOn);
        }
    }
    else if (newState == kWiFiPowerStandby && prevState == kWiFiPowerOff) {
        // OFF→STANDBY: power on (into standby mode)
        power_state = kWiFiPowerStandby;
        err = enableAdapter(netif);
        if (err == kIOReturnSuccess) {
            postTahoeDriverAvailabilityTransition(
                this, TahoeDriverAvailabilityContracts::Transition::PowerOn);
        }
    }
    else if (newState == kWiFiPowerStandby && prevState == kWiFiPowerOn) {
        // ON→STANDBY: power off (into standby)
        power_state = kWiFiPowerStandby;
        postTahoeDriverAvailabilityTransition(
            this, TahoeDriverAvailabilityContracts::Transition::PowerOff);
        disableAdapterCore(netif);
    }
    else if (newState == prevState) {
        // Same state — no-op
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

    // Current 25C56 AppleBCMWLANCore::handlePowerStateChange updates the
    // logical radio state and calls powerOff()/powerOn(); it does not publish
    // APPLE80211_M_POWER_CHANGED. That bulletin belongs to the separate IOPM
    // system sleep/wake path below. Publishing it for a radio toggle makes SSM
    // replay WAKE while WCLNetManager can still be WAITING_FOR_IP.

    return err;
}

void AirportItlwm::handleSystemPowerStateChange(bool powerOn, IONetworkInterface *netif)
{
    // Apple exposes a separate powerOffSystem()/powerOnSystem() path in
    // addition to handlePowerStateChange().  Those helpers preserve the
    // logical radio state, but powerOffSystem() enters powerOff(true) and
    // powerOnSystem() enters powerOn(): the normal unavailable/available 0x37
    // carriers therefore still bracket the physical sleep/wake transition.
    // powerOnSystem() publishes selector 1 only after powerOn() has completed.
    if (powerOn) {
        if (power_state && enableAdapter(netif) == kIOReturnSuccess) {
            postTahoeDriverAvailabilityTransition(
                this, TahoeDriverAvailabilityContracts::Transition::PowerOn);
        }
        if (fNetIf)
            postMessage(fNetIf, APPLE80211_M_POWER_CHANGED, NULL, 0, true);
    } else {
        if (power_state) {
            postTahoeDriverAvailabilityTransition(
                this, TahoeDriverAvailabilityContracts::Transition::PowerOff);
            disableAdapterCore(netif);
        }
    }
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

IOReturn AirportItlwm::setPowerState(unsigned long powerStateOrdinal, IOService *policyMaker)
{
    // IOPM can issue the initial policy OFF before start() has published
    // Live, but must never race the Draining teardown with its command-gate
    // enable/disable path.
    if (!beginLifecycleInternalOperation())
        return kIOReturnNotReady;

    RT_SET(12);
    sRT.pmCount++;
    sRT.lastPmReq = (uint32_t)powerStateOrdinal;

    IOCommandGate *gate = getCommandGate();
    if (gate == nullptr) {
        endLifecycleOperation();
        return kIOReturnNotReady;
    }
    IOReturn result = gate->runAction(
        setPowerStateGated,
        reinterpret_cast<void *>(powerStateOrdinal),
        policyMaker);

    // AppleBCMWLANCore conditionally updates its private
    // AppleBCMWLANIOReportingCore owner here.  AirportItlwm has no equivalent
    // private reporter owner; generic inherited IOReport entry points are not
    // that state machine.  The applicable reference branch is therefore the
    // explicit owner-null path, with no substitute publication.
    if (result != kIOReturnSuccess)
        sRT.pmGateErrorCnt++;
    endLifecycleOperation();
    return result;
}

IOReturn AirportItlwm::setPowerStateGated(OSObject *target, void *arg0,
                                           void *, void *, void *)
{
    AirportItlwm *self = OSDynamicCast(AirportItlwm, target);
    if (self == nullptr)
        return kIOReturnBadArgument;

    sRT.pmGateCount++;
    unsigned long ordinal = reinterpret_cast<unsigned long>(arg0);

    const UInt32 state = self->pmPowerStateFlags;
    if ((state & kAirportItlwmPmTransitionGateMask) ==
        kAirportItlwmPmTransitionBlockedValue) {
        sRT.pmNoopCount++;
        return kIOReturnSuccess;
    }

    switch (ordinal) {
        case kPowerStateOff:
            if ((state & kAirportItlwmPmSystemOnBit) == 0) {
                sRT.pmNoopCount++;
                break;
            }
#if __IO80211_TARGET >= __MAC_26_0
            removePropertyHelper(self, "IO80211WokeSystem");
#else
            self->removeProperty("IO80211WokeSystem");
#endif
            OSBitAndAtomic(~static_cast<UInt32>(kAirportItlwmPmSystemOnBit),
                           &self->pmPowerStateFlags);
            sRT.pmSystemOffCnt++;
#if __IO80211_TARGET >= __MAC_26_0
            self->handleSystemPowerStateChange(false, NULL);
#else
            self->handleSystemPowerStateChange(false, self->bsdInterface);
#endif
            break;

        case kPowerStateOn:
            if ((state & kAirportItlwmPmSystemOnBit) != 0) {
                sRT.pmNoopCount++;
                break;
            }
            OSBitOrAtomic(kAirportItlwmPmSystemOnBit,
                          &self->pmPowerStateFlags);
            sRT.pmSystemOnCnt++;
#if __IO80211_TARGET >= __MAC_26_0
            self->handleSystemPowerStateChange(true, NULL);
#else
            self->handleSystemPowerStateChange(true, self->bsdInterface);
#endif
            break;

        default:
            sRT.pmInvalidCount++;
            break;
    }

    return kIOReturnSuccess;
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
    return ret;
}

IOReturn AirportItlwm::setWakeOnMagicPacket(bool active)
{
    magicPacketEnabled = active;
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::registerWithPolicyMaker(IOService *policyMaker)
{
    IOReturn ret;

    // The reference state bit is zero-initialized before policy registration.
    // Consequently the framework's initial external OFF request is a no-op;
    // the first real ON request performs powerOnSystem synchronously.
    pmPowerStateFlags = 0;
    sRT.pmPolicyPtr = (uint64_t)(uintptr_t)policyMaker;

    ret = policyMaker->registerPowerDriver(this,
                                           powerStateArray,
                                           kPowerStateCount);
    // Apple only performs the changePowerStateToPriv(ON) / external-OFF pair
    // when its separate core-private +0x2a00 owner has bit 0 set.  The Intel
    // port has no corresponding owner, initializer, or lifecycle, so the
    // applicable reference branch is the false branch.  Unconditionally
    // forcing the pair locally manufactures a startup IOPM resume and its
    // POWER_CHANGED bulletin before the radio lifecycle is established.
    return ret;
}

#if __IO80211_TARGET >= __MAC_26_0
// =====================================================================
// CR-239 Phase 1 — AirportItlwmUserClient implementation.
//
// Userspace open sequence (from a privileged root LaunchDaemon):
//   io_service_t svc = IOServiceGetMatchingService(kIOMainPortDefault,
//                          IOServiceMatching("AirportItlwm"));
//   io_connect_t conn;
//   IOServiceOpen(svc, mach_task_self(),
//                 kAirportItlwmUserClientType /*'PLTI'*/, &conn);
//   IOConnectCallStructMethod(conn,
//                kAirportItlwmUserClientMethod_DeliverPMK,
//                &apple80211_key, sizeof(struct apple80211_key),
//                NULL, NULL);
// =====================================================================

bool AirportItlwmUserClient::
initWithTask(task_t owningTask, void *securityID, UInt32 type,
             OSDictionary *properties)
{
    /* free() may run after a base-class init failure; keep its locks inert. */
    fOwningTask = nullptr;
    fProvider = nullptr;
    fProviderLock = nullptr;
    memset(fSaeClientCookie, 0, sizeof(fSaeClientCookie));
    if (!IOUserClient::initWithTask(owningTask, securityID, type,
                                    properties)) {
        return false;
    }
    fOwningTask = owningTask;
    if (!airportItlwmSaeFillNonZero(fSaeClientCookie))
        return false;
    fProviderLock = IOLockAlloc();
    if (fProviderLock == nullptr)
        return false;
    return true;
}

bool AirportItlwmUserClient::
start(IOService *provider)
{
    if (!IOUserClient::start(provider))
        return false;
    AirportItlwm *controller = OSDynamicCast(AirportItlwm, provider);
    if (controller == nullptr) {
        XYLog("CR239 AirportItlwmUserClient::start provider is not AirportItlwm\n");
        return false;
    }
    IOLock *providerLock = fProviderLock;
    if (providerLock == nullptr)
        return false;

    // UserClient::stop can null the provider concurrently with an external
    // method. Hold a persistent retain while the client is attached, and let
    // each method take a second retain under this lock before it calls the
    // controller's lifecycle admission API.
    controller->retain();
    IOLockLock(providerLock);
    if (fProvider != nullptr) {
        IOLockUnlock(providerLock);
        controller->release();
        return false;
    }
    fProvider = controller;
    IOLockUnlock(providerLock);
    return true;
}

AirportItlwm *AirportItlwmUserClient::retainProvider()
{
    IOLock *providerLock = fProviderLock;
    if (providerLock == nullptr)
        return nullptr;

    IOLockLock(providerLock);
    AirportItlwm *provider = fProvider;
    if (provider != nullptr)
        provider->retain();
    IOLockUnlock(providerLock);
    return provider;
}

AirportItlwm *AirportItlwmUserClient::takeProvider()
{
    IOLock *providerLock = fProviderLock;
    if (providerLock == nullptr)
        return nullptr;

    IOLockLock(providerLock);
    AirportItlwm *provider = fProvider;
    fProvider = nullptr;
    IOLockUnlock(providerLock);
    return provider;
}

bool AirportItlwmUserClient::
copySaeClientCookie(uint8_t out[kAirportItlwmSaeRelayV1NonceLength])
{
    IOLock *providerLock = fProviderLock;
    if (out == nullptr || providerLock == nullptr)
        return false;

    IOLockLock(providerLock);
    const bool available = fProvider != nullptr &&
        !AirportItlwmSaeRelayFsmV1BytesAllZero(fSaeClientCookie,
            sizeof(fSaeClientCookie));
    if (available)
        memcpy(out, fSaeClientCookie, sizeof(fSaeClientCookie));
    else
        memset(out, 0, kAirportItlwmSaeRelayV1NonceLength);
    IOLockUnlock(providerLock);
    return available;
}

void AirportItlwmUserClient::
clearSaeClientCookie()
{
    IOLock *providerLock = fProviderLock;
    if (providerLock == nullptr)
        return;
    IOLockLock(providerLock);
    memset(fSaeClientCookie, 0, sizeof(fSaeClientCookie));
    IOLockUnlock(providerLock);
}

void AirportItlwmUserClient::
stop(IOService *provider)
{
    uint8_t sae_cookie[kAirportItlwmSaeRelayV1NonceLength];
    const bool have_sae_cookie = copySaeClientCookie(sae_cookie);
    AirportItlwm *controller = takeProvider();
    if (controller != nullptr && have_sae_cookie &&
        controller->beginLifecycleOperation()) {
        /*
         * Do not retain the UserClient provider lock across this gate entry.
         * A normal close may cancel only the exact cookie it owns; a draining
         * controller has already cancelled/woken all relay waiters.
         */
        controller->abortSaeRelayForClient(sae_cookie);
        controller->endLifecycleOperation();
    }
    clearSaeClientCookie();
    memset(sae_cookie, 0, sizeof(sae_cookie));
    IOUserClient::stop(provider);
    if (controller != nullptr)
        controller->release();
}

void AirportItlwmUserClient::
free(void)
{
    uint8_t sae_cookie[kAirportItlwmSaeRelayV1NonceLength];
    const bool have_sae_cookie = copySaeClientCookie(sae_cookie);
    AirportItlwm *controller = takeProvider();
    if (controller != nullptr && have_sae_cookie &&
        controller->beginLifecycleOperation()) {
        controller->abortSaeRelayForClient(sae_cookie);
        controller->endLifecycleOperation();
    }
    clearSaeClientCookie();
    memset(sae_cookie, 0, sizeof(sae_cookie));
    if (controller != nullptr)
        controller->release();
    if (fProviderLock != nullptr) {
        IOLockFree(fProviderLock);
        fProviderLock = nullptr;
    }
    IOUserClient::free();
}

IOReturn AirportItlwmUserClient::
clientClose(void)
{
    if (!isInactive())
        terminate();
    return kIOReturnSuccess;
}

IOReturn AirportItlwmUserClient::
externalMethod(uint32_t selector,
               IOExternalMethodArguments *args,
               IOExternalMethodDispatch *dispatch,
               OSObject *target,
               void *reference)
{
    if (selector >= kAirportItlwmUserClientMethod_NumMethods) {
        XYLog("CR239 AirportItlwmUserClient::externalMethod selector=%u "
              "out of range (max=%u)\n",
              (unsigned)selector,
              (unsigned)kAirportItlwmUserClientMethod_NumMethods);
        return kIOReturnBadArgument;
    }
    dispatch = (IOExternalMethodDispatch *)
               &sAirportItlwmUserClientMethods[selector];
    target = this;
    return IOUserClient::externalMethod(selector, args, dispatch,
                                        target, reference);
}

IOReturn AirportItlwmUserClient::
sExtDeliverPMK(AirportItlwmUserClient *target,
               void *reference,
               IOExternalMethodArguments *args)
{
    // Access control is enforced once, at open time, by
    // `AirportItlwm::newUserClient` via
    // `IOUserClient::clientHasPrivilege(securityID,
    // kIOClientPrivilegeAdministrator)`. By the time
    // `sExtDeliverPMK` runs we have a per-connection
    // `AirportItlwmUserClient` that the caller already proved they
    // were entitled to open; the `securityID` captured at open time
    // would only re-confirm a frozen value, so we do not re-check
    // it here either.
    //
    // scalarInputCount=1 carries the 64-bit generation echo that
    // the AirportItlwmAgent helper received from
    // WaitAssociationTarget. Both the structure input size and the
    // scalar input count are validated by IOUserClient dispatch via
    // `checkStructureInputSize == sizeof(struct apple80211_key)` and
    // `checkScalarInputCount == 1` in `sAirportItlwmUserClientMethods[0]`
    // before this handler runs, so wrong-size calls return
    // `kIOReturnBadArgument` directly from the dispatch layer.
    if (target == nullptr)
        return kIOReturnNotReady;
    // retainProvider snapshots under the UserClient stop lock and retains the
    // controller before stop() can clear/release its persistent reference.
    AirportItlwm *provider = target->retainProvider();
    if (provider == nullptr || !provider->beginLifecycleOperation())
    {
        if (provider != nullptr)
            provider->release();
        return kIOReturnNotReady;
    }
    if (args == nullptr || args->structureInput == nullptr)
    {
        provider->endLifecycleOperation();
        provider->release();
        return kIOReturnBadArgument;
    }
    const uint64_t generation_echo = (uint64_t)args->scalarInput[0];
    IOReturn rc = provider->deliverExternalPMK(
        (const struct apple80211_key *)args->structureInput,
        generation_echo);
    provider->endLifecycleOperation();
    provider->release();
    return rc;
}

IOReturn AirportItlwmUserClient::
sExtWaitAssociationTarget(AirportItlwmUserClient *target,
                          void *reference,
                          IOExternalMethodArguments *args)
{
    // The AirportItlwmAgent helper calls this once per association
    // edge. It passes the last_acked generation it already produced
    // a PMK for (0 on first call). The controller blocks under its
    // command gate until a NEW generation is published by the PSK
    // association-start edge in
    // AirportItlwmSkywalkInterface::associateSSID and then returns
    // the current AirportItlwmAssociationTarget snapshot so the
    // helper can locate the matching credential.
    if (target == nullptr)
        return kIOReturnNotReady;
    AirportItlwm *provider = target->retainProvider();
    if (provider == nullptr || !provider->beginLifecycleOperation())
    {
        if (provider != nullptr)
            provider->release();
        return kIOReturnNotReady;
    }
    if (args == nullptr || args->structureOutput == nullptr)
    {
        provider->endLifecycleOperation();
        provider->release();
        return kIOReturnBadArgument;
    }
    const uint64_t last_acked = (uint64_t)args->scalarInput[0];
    AirportItlwmAssociationTarget snap;
    memset(&snap, 0, sizeof(snap));
    IOReturn rc = provider->waitAssocTarget(last_acked, &snap);
    if (rc != kIOReturnSuccess) {
        provider->endLifecycleOperation();
        provider->release();
        return rc;
    }
    memcpy(args->structureOutput, &snap, sizeof(snap));
    provider->endLifecycleOperation();
    provider->release();
    return kIOReturnSuccess;
}

IOReturn AirportItlwmUserClient::
sExtWaitSaeTarget(AirportItlwmUserClient *target,
                  void *reference,
                  IOExternalMethodArguments *args)
{
    if (target == nullptr || args == nullptr ||
        args->structureOutput == nullptr)
        return kIOReturnBadArgument;
    uint8_t cookie[kAirportItlwmSaeRelayV1NonceLength];
    if (!target->copySaeClientCookie(cookie))
        return kIOReturnNotReady;
    AirportItlwm *provider = target->retainProvider();
    if (provider == nullptr || !provider->beginLifecycleOperation()) {
        if (provider != nullptr)
            provider->release();
        memset(cookie, 0, sizeof(cookie));
        return kIOReturnNotReady;
    }
    struct AirportItlwmSaeTargetV1 snap;
    memset(&snap, 0, sizeof(snap));
    IOReturn rc = provider->waitSaeTarget(cookie, &snap);
    if (rc == kIOReturnSuccess)
        memcpy(args->structureOutput, &snap, sizeof(snap));
    memset(&snap, 0, sizeof(snap));
    provider->endLifecycleOperation();
    provider->release();
    memset(cookie, 0, sizeof(cookie));
    return rc;
}

IOReturn AirportItlwmUserClient::
sExtSubmitSaeReply(AirportItlwmUserClient *target,
                   void *reference,
                   IOExternalMethodArguments *args)
{
    if (target == nullptr || args == nullptr ||
        args->structureInput == nullptr)
        return kIOReturnBadArgument;
    AirportItlwm *provider = target->retainProvider();
    if (provider == nullptr || !provider->beginLifecycleOperation()) {
        if (provider != nullptr)
            provider->release();
        return kIOReturnNotReady;
    }
    IOReturn rc = provider->submitSaeReply(
        (const struct AirportItlwmSaeAuthReplyV1 *)args->structureInput);
    provider->endLifecycleOperation();
    provider->release();
    return rc;
}

IOReturn AirportItlwmUserClient::
sExtWaitSaeAuthEvent(AirportItlwmUserClient *target,
                     void *reference,
                     IOExternalMethodArguments *args)
{
    if (target == nullptr || args == nullptr ||
        args->structureOutput == nullptr)
        return kIOReturnBadArgument;
    uint8_t cookie[kAirportItlwmSaeRelayV1NonceLength];
    if (!target->copySaeClientCookie(cookie))
        return kIOReturnNotReady;
    AirportItlwm *provider = target->retainProvider();
    if (provider == nullptr || !provider->beginLifecycleOperation()) {
        if (provider != nullptr)
            provider->release();
        memset(cookie, 0, sizeof(cookie));
        return kIOReturnNotReady;
    }
    struct AirportItlwmSaeAuthEventV1 event;
    memset(&event, 0, sizeof(event));
    IOReturn rc = provider->waitSaeAuthEvent(cookie,
        (uint64_t)args->scalarInput[0], &event);
    if (rc == kIOReturnSuccess)
        memcpy(args->structureOutput, &event, sizeof(event));
    memset(&event, 0, sizeof(event));
    provider->endLifecycleOperation();
    provider->release();
    memset(cookie, 0, sizeof(cookie));
    return rc;
}

IOReturn AirportItlwmUserClient::
sExtCompleteSae(AirportItlwmUserClient *target,
                void *reference,
                IOExternalMethodArguments *args)
{
    if (target == nullptr || args == nullptr ||
        args->structureInput == nullptr)
        return kIOReturnBadArgument;
    AirportItlwm *provider = target->retainProvider();
    if (provider == nullptr || !provider->beginLifecycleOperation()) {
        if (provider != nullptr)
            provider->release();
        return kIOReturnNotReady;
    }
    IOReturn rc = provider->completeSae(
        (const struct AirportItlwmSaeCompletionV1 *)args->structureInput);
    provider->endLifecycleOperation();
    provider->release();
    return rc;
}

IOReturn AirportItlwmUserClient::
sExtAbortSae(AirportItlwmUserClient *target,
             void *reference,
             IOExternalMethodArguments *args)
{
    if (target == nullptr || args == nullptr ||
        args->structureInput == nullptr)
        return kIOReturnBadArgument;
    AirportItlwm *provider = target->retainProvider();
    if (provider == nullptr || !provider->beginLifecycleOperation()) {
        if (provider != nullptr)
            provider->release();
        return kIOReturnNotReady;
    }
    IOReturn rc = provider->abortSae(
        (const struct AirportItlwmSaeAbortV1 *)args->structureInput);
    provider->endLifecycleOperation();
    provider->release();
    return rc;
}

// =====================================================================
// AirportItlwm controller-side dispatch.
// =====================================================================

IOReturn AirportItlwm::
newUserClient(task_t owningTask, void *securityID, UInt32 type,
              OSDictionary *properties, IOUserClient **handler)
{
    if (type != kAirportItlwmUserClientType) {
        // Defer to base class so existing IO80211APIUserClient
        // dispatch is preserved unchanged. We only intercept the
        // unique 'PLTI' type magic for our private channel.
        return IO80211Controller::newUserClient(owningTask, securityID,
                                                type, properties,
                                                handler);
    }
    // Do not open a fresh PLTI channel once controller teardown has claimed
    // Draining. Keep this admission through authorization, allocation,
    // attach/start, and handler publication: an atomic live check alone lets
    // stop free the controller while IOServiceOpen is still wiring the new
    // user-client to it.
    if (!beginLifecycleOperation())
        return kIOReturnNotReady;
    if (handler == NULL) {
        endLifecycleOperation();
        return kIOReturnBadArgument;
    }
    // CR-240 — Open-time authorization gate. The PLTI type magic is a
    // selector, NOT an access-control boundary (per CR-239 reviewer
    // note: "security-through-obscurity"). A custom user-client that
    // will (in Phase 2) accept PSK material MUST require admin
    // privilege so a non-root local process cannot inject keying
    // state by guessing the type magic. We use IOKit's documented
    // `clientHasPrivilege(securityToken, kIOClientPrivilegeAdministrator)`
    // (static method on IOUserClient, declared in
    // MacKernelSDK/Headers/IOKit/IOUserClient.h:326). Only callers
    // running as root (effective uid 0) pass.
    IOReturn auth = IOUserClient::clientHasPrivilege(
        securityID, kIOClientPrivilegeAdministrator);
    if (auth != kIOReturnSuccess) {
        XYLog("CR240 AirportItlwm::newUserClient(type=0x%x) DENIED -- "
              "clientHasPrivilege(kIOClientPrivilegeAdministrator)=0x%x\n",
              (unsigned)type, (unsigned)auth);
        endLifecycleOperation();
        return kIOReturnNotPrivileged;
    }
    AirportItlwmUserClient *client = OSTypeAlloc(AirportItlwmUserClient);
    if (client == nullptr) {
        endLifecycleOperation();
        return kIOReturnNoMemory;
    }
    if (!client->initWithTask(owningTask, securityID, type, properties)) {
        client->release();
        endLifecycleOperation();
        return kIOReturnError;
    }
    if (!client->attach(this)) {
        client->release();
        endLifecycleOperation();
        return kIOReturnError;
    }
    if (!client->start(this)) {
        client->detach(this);
        client->release();
        endLifecycleOperation();
        return kIOReturnError;
    }
    *handler = client;
    endLifecycleOperation();
    return kIOReturnSuccess;
}

// =====================================================================
// Project-owned PLTI PMK producer trigger surface.
//
// Producer/consumer pipeline (entirely project-owned; no CWWiFiClient,
// CoreWLAN, airportd, eventType 0x6d, or private entitlement is used):
//
//   PRODUCER (kext, AirportItlwmSkywalkInterface::associateSSID):
//     external-PSK branch -> publishPendingAssocTarget(ssid, bssid,
//                                                     authtype_*)
//     -> assigns new monotonic generation, replaces fAssocTarget,
//        wakes waiters via getCommandGate()->commandWakeup.
//
//   CONSUMER (helper, AirportItlwmAgent root LaunchDaemon):
//     io_connect_t conn = IOServiceOpen(svc, 'PLTI');
//     loop forever {
//       WaitAssociationTarget(conn, last_acked) ->
//         AirportItlwmAssociationTarget tgt (generation = G);
//       look up tgt.ssid in System keychain (PSK password);
//       PBKDF2-SHA1(password, ssid, 4096) -> 32-byte PMK;
//       DeliverPMK(conn, generation=G, apple80211_key(PMK,32,...));
//       last_acked = G;
//     }
//
//   SINK (kext, AirportItlwm::deliverExternalPMK):
//     under one IOCommandGate::runAction hold:
//       validate (generation_echo == fAssocTarget.generation &&
//                 !fAssocTargetCanceled &&
//                 !fAssocTargetTerminating);
//       on mismatch -> kIOReturnNotPermitted, no ic state change;
//       on match    -> memcpy 32 bytes into ic->ic_psk;
//                      set IEEE80211_F_PSK;
//                      clear ic_external_pmk_owner=0 so the first
//                      4-way M1 routes through the local PAE
//                      (owner=local), matching the existing
//                      installExternalPmkLocked sink semantic;
//                      maps the generation-bound association selector to
//                      its exact AKM; it must not broaden PSK into
//                      SHA256-PSK.
//
// Lifecycle reset / replay invariant:
//   cancelPendingAssocTarget runs under the SAME command gate as
//   deliverExternalPMK, AND its gated action ALSO clears ic_psk +
//   drops IEEE80211_F_PSK + zeros ic_external_pmk_owner. The two
//   actions are therefore mutually exclusive: a cancel cannot
//   interleave between the helper's generation validation and the
//   ic_psk write, and a delivery cannot interleave between a cancel
//   and the ic_psk zero. clearExternalPmkEligibilityLocked routes
//   its ic_psk reset through cancelPendingAssocTarget for the same
//   reason, so the legacy lifecycle edges (disassociate, leave,
//   PMKSA clear, RSN disable, JOIN_ABORT, REASSOC) all share the
//   same atomic critical section. Lifecycle cancels leave
//   WaitAssociationTarget parked for the next published generation;
//   releaseAll marks fAssocTargetTerminating so teardown still
//   returns kIOReturnAborted before the command gate disappears.
// =====================================================================

namespace {

static constexpr uint32_t kAirportItlwmExternalPmkWaitStepMs = 10;

static unsigned int
airportItlwmPmkNonZeroByteCount(const struct ieee80211com *ic)
{
    if (ic == nullptr)
        return 0;

    unsigned int nonzero = 0;
    for (size_t i = 0; i < sizeof(ic->ic_psk); ++i) {
        if (ic->ic_psk[i] != 0)
            nonzero++;
    }
    return nonzero;
}

struct AirportItlwmPublishAssocArgs {
    AirportItlwm *self;
    const uint8_t *ssid;
    uint32_t ssid_len;
    const uint8_t *bssid;
    uint32_t authtype_lower;
    uint32_t authtype_upper;
    uint64_t out_generation;
};

struct AirportItlwmWaitAssocArgs {
    AirportItlwm *self;
    uint64_t last_acked;
    AirportItlwmAssociationTarget *out;
    IOReturn rc;
};

struct AirportItlwmCancelAssocArgs {
    AirportItlwm *self;
    bool terminating;
};

struct AirportItlwmDeliverPmkArgs {
    AirportItlwm *self;
    const struct apple80211_key *key;
    uint64_t generation_echo;
    IOReturn rc;
};

struct AirportItlwmBeginSaeRelayArgs {
    AirportItlwm *self;
    struct AirportItlwmSaeTargetV1 target;
    IOReturn rc;
};

struct AirportItlwmWaitSaeTargetArgs {
    AirportItlwm *self;
    uint8_t client_cookie[kAirportItlwmSaeRelayV1NonceLength];
    uint64_t cancel_epoch;
    struct AirportItlwmSaeTargetV1 *out;
    IOReturn rc;
};

struct AirportItlwmSaeReplyArgs {
    AirportItlwm *self;
    struct AirportItlwmSaeAuthReplyV1 reply;
    struct ItlSaeAuthTxRequestV1 request;
    uint64_t ticket;
    IOReturn rc;
};

/* HAL admission failure is reconciled after submitSaeAuthFrame() returns. */
struct AirportItlwmSaeSubmitFailureArgs {
    AirportItlwm *self;
    uint64_t ticket;
    uint64_t cancel_ticket;
    IOReturn rc;
};

/* IWX terminal record copied out of the deferred event-handler callback. */
struct AirportItlwmSaeTxCompletionArgs {
    AirportItlwm *self;
    struct ItlSaeAuthTransportEventV1 event;
    uint64_t cancel_ticket;
    IOReturn rc;
};

/* IWX reset has already purged the descriptor; this action never cancels HAL. */
struct AirportItlwmSaeTxResetArgs {
    AirportItlwm *self;
    struct ItlSaeAuthTransportEventV1 event;
    IOReturn rc;
};

struct AirportItlwmWaitSaeEventArgs {
    AirportItlwm *self;
    uint8_t client_cookie[kAirportItlwmSaeRelayV1NonceLength];
    uint64_t cancel_epoch;
    uint64_t last_seen_sequence;
    struct AirportItlwmSaeAuthEventV1 *out;
    IOReturn rc;
};

struct AirportItlwmSaeCompletionArgs {
    AirportItlwm *self;
    const struct AirportItlwmSaeCompletionV1 *completion;
    IOReturn rc;
};

struct AirportItlwmSaeAbortArgs {
    AirportItlwm *self;
    const struct AirportItlwmSaeAbortV1 *abort_message;
    uint64_t cancel_ticket;
    IOReturn rc;
};

struct AirportItlwmCancelSaeRelayArgs {
    AirportItlwm *self;
    bool terminating;
    uint64_t cancel_ticket;
};

struct AirportItlwmAbortSaeClientArgs {
    AirportItlwm *self;
    uint8_t client_cookie[kAirportItlwmSaeRelayV1NonceLength];
    uint64_t cancel_ticket;
};

static IOReturn
airportItlwmSaeFsmResultToIOReturn(enum AirportItlwmSaeRelayFsmResultV1 rc)
{
    switch (rc) {
    case kAirportItlwmSaeRelayFsmAccepted:
        return kIOReturnSuccess;
    case kAirportItlwmSaeRelayFsmBadArgument:
        return kIOReturnBadArgument;
    case kAirportItlwmSaeRelayFsmNotReady:
        return kIOReturnNotReady;
    case kAirportItlwmSaeRelayFsmNotPermitted:
        return kIOReturnNotPermitted;
    case kAirportItlwmSaeRelayFsmResultAborted:
        return kIOReturnAborted;
    }
    return kIOReturnError;
}

static void
airportItlwmAdvanceSaeRelayCancelEpochLocked(AirportItlwm *self)
{
    if (self == nullptr)
        return;
    self->fSaeRelayCancelEpoch += 1;
    if (self->fSaeRelayCancelEpoch == 0)
        self->fSaeRelayCancelEpoch = 1;
}

static void
airportItlwmClearSaeRelayWaiterLocked(
    AirportItlwm *self, const uint8_t client_cookie[
        kAirportItlwmSaeRelayV1NonceLength])
{
    if (self == nullptr || client_cookie == nullptr ||
        !self->fSaeRelayWaiterActive ||
        !AirportItlwmSaeRelayFsmV1BytesEqual(
            self->fSaeRelayWaitingCookie, client_cookie,
            kAirportItlwmSaeRelayV1NonceLength))
        return;
    memset(self->fSaeRelayWaitingCookie, 0,
           sizeof(self->fSaeRelayWaitingCookie));
    self->fSaeRelayWaiterActive = false;
}

static uint64_t
airportItlwmClearSaePendingTxLocked(AirportItlwm *self)
{
    uint64_t ticket = 0;

    if (self == nullptr)
        return 0;
    if (self->fSaePendingTxActive)
        ticket = self->fSaePendingTxRequest.ticket;
    explicit_bzero(&self->fSaePendingTxReply,
                   sizeof(self->fSaePendingTxReply));
    explicit_bzero(&self->fSaePendingTxRequest,
                   sizeof(self->fSaePendingTxRequest));
    self->fSaePendingTxActive = false;
    return ticket;
}

static void
airportItlwmClearSaeLastTerminalTxLocked(AirportItlwm *self)
{
    if (self == nullptr)
        return;
    explicit_bzero(&self->fSaeLastTerminalTxEvent,
                   sizeof(self->fSaeLastTerminalTxEvent));
    self->fSaeLastTerminalTxEventValid = false;
}

static bool
airportItlwmSaeTerminalEventMatches(
    const struct ItlSaeAuthTransportEventV1 *left,
    const struct ItlSaeAuthTransportEventV1 *right)
{
    return left != nullptr && right != nullptr &&
        left->association_epoch == right->association_epoch &&
        left->relay_generation == right->relay_generation &&
        left->ticket == right->ticket &&
        left->phase == right->phase &&
        left->wire_transaction == right->wire_transaction &&
        left->auth_status == right->auth_status &&
        memcmp(left->bssid, right->bssid, sizeof(left->bssid)) == 0 &&
        memcmp(left->sta, right->sta, sizeof(left->sta)) == 0;
}

static uint64_t
airportItlwmClearSaeRelayLocked(AirportItlwm *self, bool canceled)
{
    uint64_t ticket;

    if (self == nullptr)
        return 0;
    ticket = airportItlwmClearSaePendingTxLocked(self);
    airportItlwmClearSaeLastTerminalTxLocked(self);
    AirportItlwmSaeRelayFsmV1Clear(&self->fSaeRelay);
    airportItlwmAdvanceSaeRelayCancelEpochLocked(self);
    memset(self->fSaeRelayWaitingCookie, 0,
           sizeof(self->fSaeRelayWaitingCookie));
    self->fSaeRelayWaiterActive = false;
    self->fSaeRelayCanceled = canceled;
    self->getCommandGate()->commandWakeup(&self->fSaeRelay,
                                           /*oneThread=*/false);
    return ticket;
}

static IOReturn
airportItlwmBeginSaeRelayAction(OSObject * /*owner*/, void *arg0,
                                void * /*arg1*/, void * /*arg2*/,
                                void * /*arg3*/)
{
    AirportItlwmBeginSaeRelayArgs *a =
        (AirportItlwmBeginSaeRelayArgs *)arg0;
    AirportItlwm *s = a->self;
    if (s->fSaeRelayTerminating) {
        a->rc = kIOReturnAborted;
        return kIOReturnSuccess;
    }
    /* A Begin is not a reset primitive: never erase a bound/live session. */
    if (s->fSaeRelay.phase != kAirportItlwmSaeRelayFsmIdle ||
        s->fSaePendingTxActive) {
        a->rc = kIOReturnNotReady;
        return kIOReturnSuccess;
    }
    if (AirportItlwmSaeRelayFsmV1BytesAllZero(s->fSaeControllerNonce,
            sizeof(s->fSaeControllerNonce))) {
        a->rc = kIOReturnNotReady;
        return kIOReturnSuccess;
    }
    memcpy(a->target.controller_nonce, s->fSaeControllerNonce,
        sizeof(a->target.controller_nonce));
    memset(a->target.client_cookie, 0, sizeof(a->target.client_cookie));
    a->rc = airportItlwmSaeFsmResultToIOReturn(
        AirportItlwmSaeRelayFsmV1Begin(&s->fSaeRelay, &a->target));
    if (a->rc == kIOReturnSuccess) {
        s->fSaeRelayCanceled = false;
        s->getCommandGate()->commandWakeup(&s->fSaeRelay,
                                            /*oneThread=*/false);
    }
    return kIOReturnSuccess;
}

static IOReturn
airportItlwmWaitSaeTargetAction(OSObject * /*owner*/, void *arg0,
                                 void * /*arg1*/, void * /*arg2*/,
                                 void * /*arg3*/)
{
    AirportItlwmWaitSaeTargetArgs *a =
        (AirportItlwmWaitSaeTargetArgs *)arg0;
    AirportItlwm *s = a->self;
    /*
     * Only one UserClient may be parked for the next target.  The cancel
     * epoch is captured before the first sleep, so a cancelled waiter cannot
     * wake later and bind a target from a replacement association session.
     */
    if (s->fSaeRelayWaiterActive) {
        a->rc = kIOReturnNotReady;
        return kIOReturnSuccess;
    }
    memcpy(s->fSaeRelayWaitingCookie, a->client_cookie,
           sizeof(s->fSaeRelayWaitingCookie));
    s->fSaeRelayWaiterActive = true;
    a->cancel_epoch = s->fSaeRelayCancelEpoch;
    for (;;) {
        if (s->fSaeRelayTerminating || s->fSaeRelayCanceled ||
            a->cancel_epoch != s->fSaeRelayCancelEpoch) {
            airportItlwmClearSaeRelayWaiterLocked(s, a->client_cookie);
            a->rc = kIOReturnAborted;
            return kIOReturnSuccess;
        }
        if (s->fSaeRelay.phase ==
            kAirportItlwmSaeRelayFsmAwaitAgentInitialCommit) {
            a->rc = airportItlwmSaeFsmResultToIOReturn(
                AirportItlwmSaeRelayFsmV1BindClient(&s->fSaeRelay,
                                                      a->client_cookie));
            if (a->rc == kIOReturnSuccess) {
                memcpy(a->out, &s->fSaeRelay.target, sizeof(*a->out));
                airportItlwmClearSaeRelayWaiterLocked(s, a->client_cookie);
            } else {
                airportItlwmClearSaeRelayWaiterLocked(s, a->client_cookie);
            }
            return kIOReturnSuccess;
        }
        IOReturn sleep_rc = s->getCommandGate()->commandSleep(
            &s->fSaeRelay, THREAD_ABORTSAFE);
        if (sleep_rc != THREAD_AWAKENED && sleep_rc != THREAD_TIMED_OUT) {
            airportItlwmClearSaeRelayWaiterLocked(s, a->client_cookie);
            a->rc = kIOReturnAborted;
            return kIOReturnSuccess;
        }
    }
}

static IOReturn
airportItlwmSubmitSaeReplyAction(OSObject * /*owner*/, void *arg0,
                                  void * /*arg1*/, void * /*arg2*/,
                                  void * /*arg3*/)
{
    AirportItlwmSaeReplyArgs *a = (AirportItlwmSaeReplyArgs *)arg0;
    AirportItlwm *s = a->self;
    enum AirportItlwmSaeRelayFsmResultV1 validation;

    if (s->fSaeRelayTerminating || s->fSaeRelayCanceled) {
        a->rc = kIOReturnAborted;
        return kIOReturnSuccess;
    }
    if (!AirportItlwmSaeRelayFsmV1TargetBound(&s->fSaeRelay)) {
        a->rc = kIOReturnNotReady;
        return kIOReturnSuccess;
    }
    if (!AirportItlwmSaeRelayFsmV1IdentityMatches(&s->fSaeRelay,
            a->reply.generation, a->reply.association_epoch,
            a->reply.controller_nonce, a->reply.client_cookie,
            a->reply.bssid, a->reply.sta)) {
        a->rc = kIOReturnNotPermitted;
        return kIOReturnSuccess;
    }
    /*
     * One relay may own only one physical Algorithm-3 ticket.  This fence is
     * distinct from the FSM phase: it covers the interval from accepted HAL
     * admission until IWX's deferred terminal completion reaches this gate.
     */
    if (s->fSaePendingTxActive) {
        a->rc = kIOReturnNotReady;
        return kIOReturnSuccess;
    }

    /* Validate on a scrubbed FSM copy; live state advances only on TX success. */
    validation = AirportItlwmSaeRelayFsmV1ValidateReply(&s->fSaeRelay,
                                                          &a->reply);
    if (validation != kAirportItlwmSaeRelayFsmAccepted) {
        a->rc = airportItlwmSaeFsmResultToIOReturn(validation);
        /* Match the FSM's terminal invalid-body semantics without accepting it. */
        if (validation == kAirportItlwmSaeRelayFsmNotPermitted)
            (void)airportItlwmClearSaeRelayLocked(s, true);
        return kIOReturnSuccess;
    }
    if (s->fSaeNextTxTicket == UINT64_MAX) {
        /* IWX cancellation uses a monotonic numerical reject-through fence. */
        a->rc = kIOReturnNoResources;
        return kIOReturnSuccess;
    }

    explicit_bzero(&a->request, sizeof(a->request));
    a->ticket = ++s->fSaeNextTxTicket;
    a->request.version = kItlSaeAuthTransportV1Version;
    a->request.size = sizeof(a->request);
    a->request.association_epoch = a->reply.association_epoch;
    a->request.relay_generation = a->reply.generation;
    a->request.ticket = a->ticket;
    a->request.phase =
        a->reply.kind == kAirportItlwmSaeRelayReplyCommit ?
            kItlSaeAuthTransportPhaseCommit :
            kItlSaeAuthTransportPhaseConfirm;
    a->request.wire_transaction =
        itl_sae_auth_transport_sta_wire_transaction_for_phase(
            a->request.phase);
    a->request.auth_status = 0;
    a->request.body_len = a->reply.body_len;
    memcpy(a->request.bssid, a->reply.bssid, sizeof(a->request.bssid));
    memcpy(a->request.sta, a->reply.sta, sizeof(a->request.sta));
    memcpy(a->request.body, a->reply.body, a->request.body_len);
    if (!itl_sae_auth_transport_request_is_well_formed(&a->request)) {
        explicit_bzero(&a->request, sizeof(a->request));
        a->ticket = 0;
        a->rc = kIOReturnBadArgument;
        return kIOReturnSuccess;
    }

    /* A delayed reset for the preceding terminal ticket must not match this. */
    airportItlwmClearSaeLastTerminalTxLocked(s);
    memcpy(&s->fSaePendingTxReply, &a->reply,
           sizeof(s->fSaePendingTxReply));
    memcpy(&s->fSaePendingTxRequest, &a->request,
           sizeof(s->fSaePendingTxRequest));
    s->fSaePendingTxActive = true;
    /* Do not call AcceptReply(): HAL admission is not a peer-visible TX. */
    a->rc = kIOReturnSuccess;
    return kIOReturnSuccess;
}

static IOReturn
airportItlwmSaeSubmitFailureAction(OSObject * /*owner*/, void *arg0,
                                   void * /*arg1*/, void * /*arg2*/,
                                   void * /*arg3*/)
{
    AirportItlwmSaeSubmitFailureArgs *a =
        (AirportItlwmSaeSubmitFailureArgs *)arg0;
    AirportItlwm *s = a->self;

    if (!s->fSaePendingTxActive || a->ticket == 0 ||
        s->fSaePendingTxRequest.ticket != a->ticket) {
        a->rc = kIOReturnNotReady;
        return kIOReturnSuccess;
    }
    /* submitSaeAuthFrame() returned before a successful doorbell ownership. */
    a->cancel_ticket = airportItlwmClearSaePendingTxLocked(s);
    a->rc = kIOReturnSuccess;
    return kIOReturnSuccess;
}

static IOReturn
airportItlwmSaeTxCompletionAction(OSObject * /*owner*/, void *arg0,
                                  void * /*arg1*/, void * /*arg2*/,
                                  void * /*arg3*/)
{
    AirportItlwmSaeTxCompletionArgs *a =
        (AirportItlwmSaeTxCompletionArgs *)arg0;
    AirportItlwm *s = a->self;
    struct ieee80211com *ic;
    enum AirportItlwmSaeRelayFsmResultV1 result;

    if (!itl_sae_auth_transport_event_is_well_formed(&a->event)) {
        a->rc = kIOReturnBadArgument;
        return kIOReturnSuccess;
    }
    if (!s->fSaePendingTxActive) {
        a->rc = kIOReturnNotReady;
        return kIOReturnSuccess;
    }
    /* A late completion may never erase a newer pending controller request. */
    if (!itl_sae_auth_transport_event_matches_request(
            &a->event, &s->fSaePendingTxRequest)) {
        a->rc = kIOReturnNotPermitted;
        return kIOReturnSuccess;
    }

    ic = s->fHalService != nullptr
        ? s->fHalService->get80211Controller() : nullptr;
    if (s->fSaeRelayTerminating || s->fSaeRelayCanceled ||
        !AirportItlwmSaeRelayFsmV1TargetBound(&s->fSaeRelay) ||
        s->fSaeRelay.target.generation != a->event.relay_generation ||
        s->fSaeRelay.target.association_epoch !=
            a->event.association_epoch ||
        memcmp(s->fSaeRelay.target.bssid, a->event.bssid,
               sizeof(a->event.bssid)) != 0 ||
        memcmp(s->fSaeRelay.target.sta, a->event.sta,
               sizeof(a->event.sta)) != 0 ||
        ic == nullptr ||
        ieee80211_pae_assoc_epoch_current(ic) !=
            a->event.association_epoch) {
        a->cancel_ticket = airportItlwmClearSaeRelayLocked(s, true);
        a->rc = kIOReturnAborted;
        return kIOReturnSuccess;
    }
    if (a->event.result != 0) {
        a->cancel_ticket = airportItlwmClearSaeRelayLocked(s, true);
        a->rc = kIOReturnError;
        return kIOReturnSuccess;
    }

    /* This is the sole live-FSM AcceptReply() site in the TX spine. */
    result = AirportItlwmSaeRelayFsmV1AcceptReply(
        &s->fSaeRelay, &s->fSaePendingTxReply);
    if (result != kAirportItlwmSaeRelayFsmAccepted) {
        a->cancel_ticket = airportItlwmClearSaeRelayLocked(s, true);
        a->rc = airportItlwmSaeFsmResultToIOReturn(result);
        return kIOReturnSuccess;
    }
    /* Retain exact non-secret identity until next TX/cancel/reset. */
    s->fSaeLastTerminalTxEvent = a->event;
    s->fSaeLastTerminalTxEventValid = true;
    (void)airportItlwmClearSaePendingTxLocked(s);
    a->rc = kIOReturnSuccess;
    return kIOReturnSuccess;
}

static IOReturn
airportItlwmSaeTxResetAction(OSObject * /*owner*/, void *arg0,
                             void * /*arg1*/, void * /*arg2*/,
                             void * /*arg3*/)
{
    AirportItlwmSaeTxResetArgs *a = (AirportItlwmSaeTxResetArgs *)arg0;
    AirportItlwm *s = a->self;

    if (!itl_sae_auth_transport_event_is_well_formed(&a->event)) {
        a->rc = kIOReturnBadArgument;
        return kIOReturnSuccess;
    }
    const bool matchesPending = s->fSaePendingTxActive &&
        itl_sae_auth_transport_event_matches_request(
            &a->event, &s->fSaePendingTxRequest);
    /*
     * Firmware may have completed successfully just before reset. The normal
     * completion action has then cleared pending state, but the reset still
     * invalidates the relay; match its retained exact ticket, never merely a
     * BSSID, so a delayed reset cannot clear a newer same-target TX.
     */
    const bool matchesLastTerminal = !s->fSaePendingTxActive &&
        s->fSaeLastTerminalTxEventValid &&
        airportItlwmSaeTerminalEventMatches(
            &a->event, &s->fSaeLastTerminalTxEvent);
    if (!matchesPending && !matchesLastTerminal) {
        a->rc = kIOReturnNotPermitted;
        return kIOReturnSuccess;
    }
    if (s->fSaeRelayTerminating || s->fSaeRelayCanceled ||
        !AirportItlwmSaeRelayFsmV1TargetBound(&s->fSaeRelay) ||
        s->fSaeRelay.target.generation != a->event.relay_generation ||
        s->fSaeRelay.target.association_epoch != a->event.association_epoch ||
        memcmp(s->fSaeRelay.target.bssid, a->event.bssid,
               sizeof(a->event.bssid)) != 0 ||
        memcmp(s->fSaeRelay.target.sta, a->event.sta,
               sizeof(a->event.sta)) != 0) {
        a->rc = kIOReturnNotPermitted;
        return kIOReturnSuccess;
    }

    /* The lower driver has reset/purged it: no AcceptReply or HAL callback. */
    (void)airportItlwmClearSaeRelayLocked(s, true);
    a->rc = kIOReturnSuccess;
    return kIOReturnSuccess;
}

static IOReturn
airportItlwmWaitSaeEventAction(OSObject * /*owner*/, void *arg0,
                                void * /*arg1*/, void * /*arg2*/,
                                void * /*arg3*/)
{
    AirportItlwmWaitSaeEventArgs *a =
        (AirportItlwmWaitSaeEventArgs *)arg0;
    AirportItlwm *s = a->self;
    a->cancel_epoch = s->fSaeRelayCancelEpoch;
    for (;;) {
        if (s->fSaeRelayTerminating || s->fSaeRelayCanceled ||
            a->cancel_epoch != s->fSaeRelayCancelEpoch) {
            a->rc = kIOReturnAborted;
            return kIOReturnSuccess;
        }
        if (!AirportItlwmSaeRelayFsmV1TargetBound(&s->fSaeRelay)) {
            a->rc = kIOReturnNotReady;
            return kIOReturnSuccess;
        }
        enum AirportItlwmSaeRelayFsmResultV1 result =
            AirportItlwmSaeRelayFsmV1TakeEvent(&s->fSaeRelay,
                a->client_cookie, a->last_seen_sequence, a->out);
        if (result != kAirportItlwmSaeRelayFsmNotReady) {
            a->rc = airportItlwmSaeFsmResultToIOReturn(result);
            return kIOReturnSuccess;
        }
        IOReturn sleep_rc = s->getCommandGate()->commandSleep(
            &s->fSaeRelay, THREAD_ABORTSAFE);
        if (sleep_rc != THREAD_AWAKENED && sleep_rc != THREAD_TIMED_OUT) {
            a->rc = kIOReturnAborted;
            return kIOReturnSuccess;
        }
    }
}

static IOReturn
airportItlwmCompleteSaeAction(OSObject * /*owner*/, void *arg0,
                               void * /*arg1*/, void * /*arg2*/,
                               void * /*arg3*/)
{
    AirportItlwmSaeCompletionArgs *a =
        (AirportItlwmSaeCompletionArgs *)arg0;
    AirportItlwm *s = a->self;
    if (s->fSaeRelayTerminating || s->fSaeRelayCanceled) {
        a->rc = kIOReturnAborted;
        return kIOReturnSuccess;
    }
    if (!AirportItlwmSaeRelayFsmV1TargetBound(&s->fSaeRelay)) {
        a->rc = kIOReturnNotReady;
        return kIOReturnSuccess;
    }
    if (!AirportItlwmSaeRelayFsmV1IdentityMatches(&s->fSaeRelay,
            a->completion->generation, a->completion->association_epoch,
            a->completion->controller_nonce, a->completion->client_cookie,
            a->completion->bssid, a->completion->sta)) {
        a->rc = kIOReturnNotPermitted;
        return kIOReturnSuccess;
    }
    /* No PMK is copied or installed until a later SAE-specific PMK owner. */
    a->rc = kIOReturnNotReady;
    return kIOReturnSuccess;
}

static IOReturn
airportItlwmAbortSaeAction(OSObject * /*owner*/, void *arg0,
                            void * /*arg1*/, void * /*arg2*/,
                            void * /*arg3*/)
{
    AirportItlwmSaeAbortArgs *a = (AirportItlwmSaeAbortArgs *)arg0;
    AirportItlwm *s = a->self;
    if (s->fSaeRelayTerminating || s->fSaeRelayCanceled) {
        a->rc = kIOReturnAborted;
        return kIOReturnSuccess;
    }
    a->rc = airportItlwmSaeFsmResultToIOReturn(
        AirportItlwmSaeRelayFsmV1AcceptAbort(&s->fSaeRelay,
                                               a->abort_message));
    if (a->rc == kIOReturnSuccess)
        a->cancel_ticket = airportItlwmClearSaeRelayLocked(s, true);
    return kIOReturnSuccess;
}

static IOReturn
airportItlwmCancelSaeRelayAction(OSObject * /*owner*/, void *arg0,
                                  void * /*arg1*/, void * /*arg2*/,
                                  void * /*arg3*/)
{
    AirportItlwmCancelSaeRelayArgs *a =
        (AirportItlwmCancelSaeRelayArgs *)arg0;
    AirportItlwm *s = a->self;
    if (a->terminating)
        s->fSaeRelayTerminating = true;
    a->cancel_ticket = airportItlwmClearSaeRelayLocked(s, true);
    return kIOReturnSuccess;
}

static IOReturn
airportItlwmAbortSaeClientAction(OSObject * /*owner*/, void *arg0,
                                  void * /*arg1*/, void * /*arg2*/,
                                  void * /*arg3*/)
{
    AirportItlwmAbortSaeClientArgs *a =
        (AirportItlwmAbortSaeClientArgs *)arg0;
    AirportItlwm *s = a->self;
    const bool owns_bound_target =
        AirportItlwmSaeRelayFsmV1TargetBound(&s->fSaeRelay) &&
        AirportItlwmSaeRelayFsmV1BytesEqual(a->client_cookie,
            s->fSaeRelay.target.client_cookie,
            kAirportItlwmSaeRelayV1NonceLength);
    const bool owns_pending_wait = s->fSaeRelayWaiterActive &&
        AirportItlwmSaeRelayFsmV1BytesEqual(a->client_cookie,
            s->fSaeRelayWaitingCookie,
            kAirportItlwmSaeRelayV1NonceLength);
    if (!s->fSaeRelayTerminating && !s->fSaeRelayCanceled &&
        (owns_bound_target || owns_pending_wait))
        a->cancel_ticket = airportItlwmClearSaeRelayLocked(s, true);
    return kIOReturnSuccess;
}

static IOReturn
airportItlwmPublishAssocAction(OSObject * /*owner*/, void *arg0,
                               void * /*arg1*/, void * /*arg2*/,
                               void * /*arg3*/)
{
    AirportItlwmPublishAssocArgs *a = (AirportItlwmPublishAssocArgs *)arg0;
    AirportItlwm *s = a->self;
    /* The PLTI carrier is deliberately a WPA/WPA2 PSK-PMK ingress only. */
    if (!TahoeAssociationAuthContracts::mayUseLocalPskPmk(
            a->authtype_upper)) {
        a->out_generation = 0;
        airportItlwmRegDiagRecordPlti(
            kAirportItlwmRegDiagTracePltiPublish,
            kAirportItlwmRegDiagPmkDecisionRejectPolicy,
            kIOReturnNotPermitted, a->authtype_upper, 0);
        return kIOReturnSuccess;
    }
    // Teardown's terminal cancel is sticky for this controller lifetime.
    // Never let a queued association producer reopen the target after it has
    // serialized behind the cancel and before HAL detach.
    if (s->fAssocTargetTerminating) {
        a->out_generation = 0;
        airportItlwmRegDiagRecordPlti(
            kAirportItlwmRegDiagTracePltiPublish,
            kAirportItlwmRegDiagPmkDecisionRejectTerminating,
            kIOReturnNotPermitted, a->authtype_upper, 0);
        return kIOReturnSuccess;
    }
    s->fAssocGenCounter += 1;
    if (s->fAssocGenCounter == 0) {
        // Skip the reserved "no pending request" value on wraparound
        // so the helper's last_acked!=0 check still distinguishes a
        // fresh request from the no-request state.
        s->fAssocGenCounter = 1;
    }
    memset(&s->fAssocTarget, 0, sizeof(s->fAssocTarget));
    s->fAssocTarget.version    = kAirportItlwmAssocTargetVersion;
    s->fAssocTarget.generation = s->fAssocGenCounter;
    s->fAssocTarget.ssid_len   = a->ssid_len;
    memcpy(s->fAssocTarget.ssid, a->ssid, a->ssid_len);
    memcpy(s->fAssocTarget.bssid, a->bssid, 6);
    s->fAssocTarget.authtype_lower = a->authtype_lower;
    s->fAssocTarget.authtype_upper = a->authtype_upper;
    s->fAssocTargetCanceled = false;
    a->out_generation = s->fAssocGenCounter;
    s->getCommandGate()->commandWakeup(&s->fAssocTarget,
                                       /*oneThread=*/false);
    airportItlwmRegDiagRecordPlti(
        kAirportItlwmRegDiagTracePltiPublish,
        kAirportItlwmRegDiagPmkDecisionAccepted, kIOReturnSuccess,
        a->authtype_upper, a->out_generation);
    return kIOReturnSuccess;
}

static IOReturn
airportItlwmWaitAssocAction(OSObject * /*owner*/, void *arg0,
                            void * /*arg1*/, void * /*arg2*/,
                            void * /*arg3*/)
{
    AirportItlwmWaitAssocArgs *a = (AirportItlwmWaitAssocArgs *)arg0;
    AirportItlwm *s = a->self;
    for (;;) {
        if (s->fAssocTargetTerminating) {
            a->rc = kIOReturnAborted;
            return kIOReturnSuccess;
        }
        if (!s->fAssocTargetCanceled &&
            s->fAssocTarget.generation != 0 &&
            s->fAssocTarget.generation != a->last_acked) {
            break;
        }
        IOReturn sr = s->getCommandGate()->commandSleep(
            &s->fAssocTarget, THREAD_ABORTSAFE);
        if (sr != THREAD_AWAKENED && sr != THREAD_TIMED_OUT) {
            a->rc = kIOReturnAborted;
            return kIOReturnSuccess;
        }
    }
    memcpy(a->out, &s->fAssocTarget,
           sizeof(AirportItlwmAssociationTarget));
    a->rc = kIOReturnSuccess;
    return kIOReturnSuccess;
}

static IOReturn
airportItlwmCancelAssocAction(OSObject * /*owner*/, void *arg0,
                              void * /*arg1*/, void * /*arg2*/,
                              void * /*arg3*/)
{
    // DESTRUCTIVE cancel. Drop the PMK installation AND the
    // pending-target generation in the SAME command-gate critical
    // section, so a concurrent airportItlwmDeliverPmkAction running
    // under the same gate either (a) ran fully before us and we now
    // wipe its install, or (b) runs after us and sees
    // fAssocTargetCanceled=true / fAssocTarget.generation=0 and
    // rejects. Lifecycle resets keep WaitAssociationTarget parked
    // for the next generation; teardown marks terminating so waiters
    // can return kIOReturnAborted before the command gate disappears.
    AirportItlwmCancelAssocArgs *a = (AirportItlwmCancelAssocArgs *)arg0;
    AirportItlwm *s = a->self;
    s->fAssocTargetCanceled = true;
    if (a->terminating)
        s->fAssocTargetTerminating = true;
    memset(&s->fAssocTarget, 0, sizeof(s->fAssocTarget));
    if (s->fHalService != nullptr) {
        struct ieee80211com *ic = s->fHalService->get80211Controller();
        if (ic != nullptr) {
            /* Destructive PLTI cancellation also invalidates a future SAE owner. */
            (void)ieee80211_pae_assoc_epoch_begin(ic);
            memset(ic->ic_psk, 0, sizeof(ic->ic_psk));
            ic->ic_flags &= ~IEEE80211_F_PSK;
            ic->ic_external_pmk_owner = 0;
        }
    }
    s->getCommandGate()->commandWakeup(&s->fAssocTarget,
                                       /*oneThread=*/false);
    return kIOReturnSuccess;
}

static IOReturn
airportItlwmInvalidateAssocOnlyAction(OSObject * /*owner*/, void *arg0,
                                      void * /*arg1*/, void * /*arg2*/,
                                      void * /*arg3*/)
{
    // NON-DESTRUCTIVE invalidate. Drop the pending PLTI target
    // (zero generation, set canceled, wake waiters) so any
    // subsequent gated DeliverPMK rejects on
    // fAssocTargetCanceled=true or fAssocTarget.generation=0.
    // Does NOT touch ic_psk / IEEE80211_F_PSK /
    // ic_external_pmk_owner. Used by PSK sub-branches in
    // AirportItlwmSkywalkInterface::associateSSID where the kext is
    // about to install a caller-supplied PMK directly
    // (localImportHasKey) or has already cleared the PMK state by
    // an earlier ieee80211_disable_rsn (owner=none): the helper
    // must be locked out, but the locally installed PMK (or the
    // legitimately empty PMK state) must survive.
    AirportItlwm *s = (AirportItlwm *)arg0;
    s->fAssocTargetCanceled = true;
    memset(&s->fAssocTarget, 0, sizeof(s->fAssocTarget));
    s->getCommandGate()->commandWakeup(&s->fAssocTarget,
                                       /*oneThread=*/false);
    return kIOReturnSuccess;
}

static IOReturn
airportItlwmDeliverPmkAction(OSObject * /*owner*/, void *arg0,
                             void * /*arg1*/, void * /*arg2*/,
                             void * /*arg3*/)
{
    // Single critical section that holds the controller command gate
    // across the target-identity replay-guard check AND the ic_psk
    // write AND the IEEE80211_F_PSK / ic_external_pmk_owner /
    // setwpaparms updates. This is the structural invariant that
    // makes the replay guard sound: a concurrent
    // airportItlwmCancelAssocAction cannot run between our check
    // and our writes because it serializes on the same gate.
    AirportItlwmDeliverPmkArgs *a = (AirportItlwmDeliverPmkArgs *)arg0;
    AirportItlwm *s = a->self;

    if (a->generation_echo == 0 ||
        s->fAssocTargetTerminating ||
        s->fAssocTargetCanceled ||
        s->fAssocTarget.generation == 0 ||
        a->generation_echo != s->fAssocTarget.generation) {
        a->rc = kIOReturnNotPermitted;
        airportItlwmRegDiagRecordPlti(
            kAirportItlwmRegDiagTracePltiDeliver,
            s->fAssocTargetTerminating ?
                kAirportItlwmRegDiagPmkDecisionRejectTerminating :
                kAirportItlwmRegDiagPmkDecisionRejectGeneration,
            a->rc, s->fAssocTarget.authtype_upper, a->generation_echo);
        return kIOReturnSuccess;
    }

    /* A malformed or stale non-PSK target must never consume a WPA2 PMK. */
    if (!TahoeAssociationAuthContracts::mayUseLocalPskPmk(
            s->fAssocTarget.authtype_upper)) {
        a->rc = kIOReturnNotPermitted;
        airportItlwmRegDiagRecordPlti(
            kAirportItlwmRegDiagTracePltiDeliver,
            kAirportItlwmRegDiagPmkDecisionRejectPolicy, a->rc,
            s->fAssocTarget.authtype_upper, a->generation_echo);
        return kIOReturnSuccess;
    }
    const uint32_t localAuthUpper =
        TahoeAssociationAuthContracts::localAuthMaskWithoutFallbackRewrite(
            s->fAssocTarget.authtype_upper);
    if (!TahoeAssociationAuthContracts::usesLocalPskAkm(localAuthUpper)) {
        a->rc = kIOReturnNotPermitted;
        airportItlwmRegDiagRecordPlti(
            kAirportItlwmRegDiagTracePltiDeliver,
            kAirportItlwmRegDiagPmkDecisionRejectPolicy, a->rc,
            s->fAssocTarget.authtype_upper, a->generation_echo);
        return kIOReturnSuccess;
    }

    struct ieee80211com *ic = (s->fHalService != nullptr)
        ? s->fHalService->get80211Controller() : nullptr;
    if (ic == nullptr) {
        a->rc = kIOReturnNotReady;
        airportItlwmRegDiagRecordPlti(
            kAirportItlwmRegDiagTracePltiDeliver,
            kAirportItlwmRegDiagPmkDecisionNotReady, a->rc,
            s->fAssocTarget.authtype_upper, a->generation_echo);
        return kIOReturnSuccess;
    }

    memcpy(ic->ic_psk, a->key->key, sizeof(ic->ic_psk));
    ic->ic_flags |= IEEE80211_F_PSK;
    ic->ic_external_pmk_owner = 0;
    s->getCommandGate()->commandWakeup(&s->fAssocTarget,
                                       /*oneThread=*/false);

    struct ieee80211_wpaparams wpa;
    memset(&wpa, 0, sizeof(wpa));
    wpa.i_enabled = 1;
    wpa.i_protos = IEEE80211_WPA_PROTO_WPA1 | IEEE80211_WPA_PROTO_WPA2;
    if (TahoeAssociationAuthContracts::usesLocalLegacyPskAkm(
            localAuthUpper))
        wpa.i_akms |= IEEE80211_WPA_AKM_PSK;
    if (TahoeAssociationAuthContracts::usesLocalSha256PskAkm(
            localAuthUpper))
        wpa.i_akms |= IEEE80211_WPA_AKM_SHA256_PSK;
    ieee80211_ioctl_setwpaparms(ic, &wpa);

    a->rc = kIOReturnSuccess;
    airportItlwmRegDiagRecordPlti(
        kAirportItlwmRegDiagTracePltiDeliver,
        kAirportItlwmRegDiagPmkDecisionAccepted, a->rc,
        s->fAssocTarget.authtype_upper, a->generation_echo);
    return kIOReturnSuccess;
}

} // namespace

#if __IO80211_TARGET >= __MAC_26_0
static void
dispatchSaeTransportMailboxEvent(
    AirportItlwm *that, const struct ItlSaeAuthTransportEventV1 *event,
    bool isReset)
{
    IOCommandGate *gate;

    if (that == nullptr || event == nullptr ||
        !itl_sae_auth_transport_event_is_well_formed(event))
        return;
    gate = that->getCommandGate();
    if (gate == nullptr)
        return;

    /* The mailbox owns `event`; action now runs on the controller workloop. */
    if (isReset) {
        AirportItlwmSaeTxResetArgs args;

        explicit_bzero(&args, sizeof(args));
        args.self = that;
        memcpy(&args.event, event, sizeof(args.event));
        args.rc = kIOReturnNotReady;
        (void)gate->runAction(&airportItlwmSaeTxResetAction, &args);
        explicit_bzero(&args, sizeof(args));
        return;
    }

    AirportItlwmSaeTxCompletionArgs args;
    ItlHalService *hal;
    uint64_t cancel_ticket;

    explicit_bzero(&args, sizeof(args));
    args.self = that;
    memcpy(&args.event, event, sizeof(args.event));
    args.rc = kIOReturnNotReady;
    (void)gate->runAction(&airportItlwmSaeTxCompletionAction, &args);

    /* Cancellation may re-enter IWX, so it is intentionally outside this gate. */
    hal = that->fHalService;
    cancel_ticket = args.cancel_ticket;
    explicit_bzero(&args, sizeof(args));
    if (cancel_ticket != 0 && hal != nullptr)
        hal->cancelSaeAuthFrame(cancel_ticket);
}

void AirportItlwm::
handleSaeAuthTransportEvent(
    AirportItlwm *that, const struct ItlSaeAuthTransportEventV1 *event,
    bool isReset)
{
    /* Strict nswq-side path: value-copy, signal source, return. */
    queueSaeTransportMailbox(that, event, isReset);
}
#endif

uint64_t AirportItlwm::
publishPendingAssocTarget(const uint8_t *ssid,
                          uint32_t ssid_len,
                          const uint8_t bssid[6],
                          uint32_t authtype_lower,
                          uint32_t authtype_upper)
{
    if (ssid == nullptr || ssid_len == 0 ||
        ssid_len > sizeof(fAssocTarget.ssid) || bssid == nullptr) {
        XYLog("plti_publish_assoc_target REJECT_INPUT ssid_len=%u "
              "ssid_nonnull=%d bssid_nonnull=%d\n",
              ssid_len,
              ssid != nullptr ? 1 : 0,
              bssid != nullptr ? 1 : 0);
        airportItlwmRegDiagRecordPlti(
            kAirportItlwmRegDiagTracePltiPublish,
            kAirportItlwmRegDiagPmkDecisionRejectInput,
            kIOReturnBadArgument, authtype_upper, 0);
        return 0;
    }
    if (!TahoeAssociationAuthContracts::mayUseLocalPskPmk(authtype_upper)) {
        XYLog("plti_publish_assoc_target REJECT_NON_PSK auth_upper=0x%x\n",
              authtype_upper);
        airportItlwmRegDiagRecordPlti(
            kAirportItlwmRegDiagTracePltiPublish,
            kAirportItlwmRegDiagPmkDecisionRejectPolicy,
            kIOReturnNotPermitted, authtype_upper, 0);
        return 0;
    }
    IOCommandGate *gate = getCommandGate();
    if (gate == nullptr) {
        XYLog("plti_publish_assoc_target NOT_READY gate=NULL\n");
        airportItlwmRegDiagRecordPlti(
            kAirportItlwmRegDiagTracePltiPublish,
            kAirportItlwmRegDiagPmkDecisionNotReady,
            kIOReturnNotReady, authtype_upper, 0);
        return 0;
    }
    AirportItlwmPublishAssocArgs a;
    a.self = this;
    a.ssid = ssid;
    a.ssid_len = ssid_len;
    a.bssid = bssid;
    a.authtype_lower = authtype_lower;
    a.authtype_upper = authtype_upper;
    a.out_generation = 0;
    gate->runAction(&airportItlwmPublishAssocAction, &a);
    return a.out_generation;
}

bool AirportItlwm::
waitForExternalPmkReady(uint64_t generation,
                        uint32_t timeout_ms,
                        uint32_t *waited_ms,
                        unsigned int *pmk_nonzero_bytes,
                        uint32_t *external_owner,
                        uint32_t *sleep_result,
                        bool *in_gate)
{
    if (waited_ms != nullptr)
        *waited_ms = 0;
    if (pmk_nonzero_bytes != nullptr)
        *pmk_nonzero_bytes = 0;
    if (external_owner != nullptr)
        *external_owner = 0;
    if (sleep_result != nullptr)
        *sleep_result = THREAD_AWAKENED;
    if (in_gate != nullptr)
        *in_gate = false;

    struct ieee80211com *ic = (fHalService != nullptr)
        ? fHalService->get80211Controller() : nullptr;
    IOCommandGate *gate = getCommandGate();
    IOWorkLoop *wl = getWorkLoop();
    const bool gatedContext = wl != nullptr && wl->inGate();
    if (in_gate != nullptr)
        *in_gate = gatedContext;

    uint32_t waited = 0;
    uint32_t lastSleep = THREAD_AWAKENED;
    unsigned int nonzero = airportItlwmPmkNonZeroByteCount(ic);
    uint32_t owner = (ic != nullptr)
        ? (uint32_t)ic->ic_external_pmk_owner : 0;

    while (!(nonzero != 0 && owner == 0) && waited < timeout_ms) {
        if (generation != 0 &&
            (fAssocTargetCanceled ||
             fAssocTarget.generation != generation)) {
            lastSleep = THREAD_INTERRUPTED;
            break;
        }

        uint32_t step = kAirportItlwmExternalPmkWaitStepMs;
        if (step > timeout_ms - waited)
            step = timeout_ms - waited;

        if (gatedContext && gate != nullptr) {
            AbsoluteTime deadline;
            int deadlineNs = (int)(step * 1000000U);
            clock_interval_to_deadline(deadlineNs, kNanosecondScale,
                                       reinterpret_cast<uint64_t *>(&deadline));
            lastSleep = gate->commandSleep(&fAssocTarget, deadline,
                                           THREAD_ABORTSAFE);
            if (lastSleep != THREAD_AWAKENED &&
                lastSleep != THREAD_TIMED_OUT)
                break;
        } else {
            IOSleep(step);
            lastSleep = THREAD_TIMED_OUT;
        }

        waited += step;
        nonzero = airportItlwmPmkNonZeroByteCount(ic);
        owner = (ic != nullptr)
            ? (uint32_t)ic->ic_external_pmk_owner : 0;
    }

    if (waited_ms != nullptr)
        *waited_ms = waited;
    if (pmk_nonzero_bytes != nullptr)
        *pmk_nonzero_bytes = nonzero;
    if (external_owner != nullptr)
        *external_owner = owner;
    if (sleep_result != nullptr)
        *sleep_result = lastSleep;

    return nonzero != 0 && owner == 0;
}

IOReturn AirportItlwm::
waitAssocTarget(uint64_t last_acked,
                AirportItlwmAssociationTarget *out)
{
    if (out == nullptr)
        return kIOReturnBadArgument;
    IOCommandGate *gate = getCommandGate();
    if (gate == nullptr)
        return kIOReturnNotReady;
    AirportItlwmWaitAssocArgs a;
    a.self = this;
    a.last_acked = last_acked;
    a.out = out;
    a.rc = kIOReturnSuccess;
    gate->runAction(&airportItlwmWaitAssocAction, &a);
    return a.rc;
}

bool AirportItlwm::
cancelPendingAssocTarget(const char *reason, bool terminating)
{
    (void)reason;
    IOCommandGate *gate = getCommandGate();
    if (gate == nullptr)
        return false;
    AirportItlwmCancelAssocArgs a;
    a.self = this;
    a.terminating = terminating;
    return gate->runAction(&airportItlwmCancelAssocAction, &a) ==
        kIOReturnSuccess;
}

void AirportItlwm::
invalidatePendingAssocTargetOnly(const char *reason)
{
    (void)reason;
    IOCommandGate *gate = getCommandGate();
    if (gate == nullptr)
        return;
    gate->runAction(&airportItlwmInvalidateAssocOnlyAction, this);
}

IOReturn AirportItlwm::
beginSaeRelay(const struct AirportItlwmSaeTargetV1 *target)
{
    if (target == nullptr)
        return kIOReturnBadArgument;
    IOCommandGate *gate = getCommandGate();
    if (gate == nullptr)
        return kIOReturnNotReady;
    AirportItlwmBeginSaeRelayArgs a;
    memset(&a, 0, sizeof(a));
    a.self = this;
    memcpy(&a.target, target, sizeof(a.target));
    a.rc = kIOReturnNotReady;
    gate->runAction(&airportItlwmBeginSaeRelayAction, &a);
    memset(&a.target, 0, sizeof(a.target));
    return a.rc;
}

IOReturn AirportItlwm::
waitSaeTarget(const uint8_t client_cookie[
                  kAirportItlwmSaeRelayV1NonceLength],
              struct AirportItlwmSaeTargetV1 *out)
{
    if (client_cookie == nullptr || out == nullptr ||
        AirportItlwmSaeRelayFsmV1BytesAllZero(client_cookie,
            kAirportItlwmSaeRelayV1NonceLength))
        return kIOReturnBadArgument;
    IOCommandGate *gate = getCommandGate();
    if (gate == nullptr)
        return kIOReturnNotReady;
    AirportItlwmWaitSaeTargetArgs a;
    memset(&a, 0, sizeof(a));
    a.self = this;
    memcpy(a.client_cookie, client_cookie, sizeof(a.client_cookie));
    a.out = out;
    a.rc = kIOReturnNotReady;
    gate->runAction(&airportItlwmWaitSaeTargetAction, &a);
    memset(a.client_cookie, 0, sizeof(a.client_cookie));
    return a.rc;
}

IOReturn AirportItlwm::
submitSaeReply(const struct AirportItlwmSaeAuthReplyV1 *reply)
{
    AirportItlwmSaeReplyArgs args;
    AirportItlwmSaeSubmitFailureArgs failure;
    if (reply == nullptr)
        return kIOReturnBadArgument;
    IOCommandGate *gate = getCommandGate();
    if (gate == nullptr)
        return kIOReturnNotReady;
    explicit_bzero(&args, sizeof(args));
    args.self = this;
    memcpy(&args.reply, reply, sizeof(args.reply));
    args.rc = kIOReturnNotReady;
    (void)gate->runAction(&airportItlwmSubmitSaeReplyAction, &args);
    if (args.rc != kIOReturnSuccess) {
        IOReturn rc = args.rc;
        explicit_bzero(&args, sizeof(args));
        return rc;
    }

    /* The controller gate protects the pending record, never HAL submission. */
    ItlHalService *hal = fHalService;
    IOReturn rc = hal != nullptr
        ? hal->submitSaeAuthFrame(&args.request) : kIOReturnNotReady;
    if (rc != kIOReturnSuccess) {
        explicit_bzero(&failure, sizeof(failure));
        failure.self = this;
        failure.ticket = args.ticket;
        failure.rc = kIOReturnNotReady;
        (void)gate->runAction(&airportItlwmSaeSubmitFailureAction, &failure);
        if (failure.cancel_ticket != 0 && hal != nullptr)
            hal->cancelSaeAuthFrame(failure.cancel_ticket);
        explicit_bzero(&failure, sizeof(failure));
    }
    explicit_bzero(&args, sizeof(args));
    return rc;
}

IOReturn AirportItlwm::
waitSaeAuthEvent(const uint8_t client_cookie[
                     kAirportItlwmSaeRelayV1NonceLength],
                 uint64_t last_seen_sequence,
                 struct AirportItlwmSaeAuthEventV1 *out)
{
    if (client_cookie == nullptr || out == nullptr ||
        AirportItlwmSaeRelayFsmV1BytesAllZero(client_cookie,
            kAirportItlwmSaeRelayV1NonceLength))
        return kIOReturnBadArgument;
    IOCommandGate *gate = getCommandGate();
    if (gate == nullptr)
        return kIOReturnNotReady;
    AirportItlwmWaitSaeEventArgs a;
    memset(&a, 0, sizeof(a));
    a.self = this;
    memcpy(a.client_cookie, client_cookie, sizeof(a.client_cookie));
    a.last_seen_sequence = last_seen_sequence;
    a.out = out;
    a.rc = kIOReturnNotReady;
    gate->runAction(&airportItlwmWaitSaeEventAction, &a);
    memset(a.client_cookie, 0, sizeof(a.client_cookie));
    return a.rc;
}

IOReturn AirportItlwm::
completeSae(const struct AirportItlwmSaeCompletionV1 *completion)
{
    if (completion == nullptr)
        return kIOReturnBadArgument;
    IOCommandGate *gate = getCommandGate();
    if (gate == nullptr)
        return kIOReturnNotReady;
    AirportItlwmSaeCompletionArgs a;
    a.self = this;
    a.completion = completion;
    a.rc = kIOReturnNotReady;
    gate->runAction(&airportItlwmCompleteSaeAction, &a);
    return a.rc;
}

IOReturn AirportItlwm::
abortSae(const struct AirportItlwmSaeAbortV1 *abort_message)
{
    if (abort_message == nullptr)
        return kIOReturnBadArgument;
    IOCommandGate *gate = getCommandGate();
    if (gate == nullptr)
        return kIOReturnNotReady;
    AirportItlwmSaeAbortArgs a;
    memset(&a, 0, sizeof(a));
    a.self = this;
    a.abort_message = abort_message;
    a.rc = kIOReturnNotReady;
    (void)gate->runAction(&airportItlwmAbortSaeAction, &a);
    ItlHalService *hal = fHalService;
    if (a.cancel_ticket != 0 && hal != nullptr)
        hal->cancelSaeAuthFrame(a.cancel_ticket);
    return a.rc;
}

void AirportItlwm::
cancelSaeRelay(const char *reason, bool terminating)
{
    (void)reason;
    IOCommandGate *gate = getCommandGate();
    if (gate == nullptr)
        return;
    AirportItlwmCancelSaeRelayArgs a;
    memset(&a, 0, sizeof(a));
    a.self = this;
    a.terminating = terminating;
    (void)gate->runAction(&airportItlwmCancelSaeRelayAction, &a);
    ItlHalService *hal = fHalService;
    if (a.cancel_ticket != 0 && hal != nullptr)
        hal->cancelSaeAuthFrame(a.cancel_ticket);
}

void AirportItlwm::
abortSaeRelayForClient(const uint8_t client_cookie[
                        kAirportItlwmSaeRelayV1NonceLength])
{
    if (client_cookie == nullptr ||
        AirportItlwmSaeRelayFsmV1BytesAllZero(client_cookie,
            kAirportItlwmSaeRelayV1NonceLength))
        return;
    IOCommandGate *gate = getCommandGate();
    if (gate == nullptr)
        return;
    AirportItlwmAbortSaeClientArgs a;
    memset(&a, 0, sizeof(a));
    a.self = this;
    memcpy(a.client_cookie, client_cookie, sizeof(a.client_cookie));
    (void)gate->runAction(&airportItlwmAbortSaeClientAction, &a);
    ItlHalService *hal = fHalService;
    if (a.cancel_ticket != 0 && hal != nullptr)
        hal->cancelSaeAuthFrame(a.cancel_ticket);
    explicit_bzero(&a, sizeof(a));
}

IOReturn AirportItlwm::
deliverExternalPMK(const struct apple80211_key *key,
                   uint64_t generation_echo)
{
    // PMK-only sink for the PLTI external method DeliverPMK.
    //
    // Cheap input validation runs BEFORE the gated critical section
    // so malformed callers do not contend with publish/cancel/wait
    // on the command gate. The gated section then performs the
    // target-identity replay-guard check AND the ic_psk install in
    // a single atomic step so a concurrent
    // airportItlwmCancelAssocAction cannot interleave between
    // validation and write. See airportItlwmDeliverPmkAction above
    // for the full atomicity argument.
    //
    // CONTRACT (in evaluation order):
    //   - key == NULL                  -> kIOReturnBadArgument
    //   - cipher_type != APPLE80211_CIPHER_PMK
    //                                  -> kIOReturnBadArgument
    //   - key_len != IEEE80211_PMK_LEN -> kIOReturnBadArgument
    //   - command gate unavailable     -> kIOReturnNotReady
    //   - inside the gated action:
    //     - generation_echo == 0       -> kIOReturnNotPermitted
    //     - fAssocTargetTerminating
    //                                  -> kIOReturnNotPermitted
    //     - fAssocTargetCanceled       -> kIOReturnNotPermitted
    //     - fAssocTarget.generation == 0
    //                                  -> kIOReturnNotPermitted
    //     - generation_echo != pending -> kIOReturnNotPermitted
    //     - ic unavailable             -> kIOReturnNotReady
    //     - all checks pass            -> memcpy 32 bytes,
    //                                      ic_flags |= IEEE80211_F_PSK,
    //                                      ic_external_pmk_owner = 0,
    //                                      ieee80211_ioctl_setwpaparms,
    //                                      kIOReturnSuccess.
    if (key == nullptr) {
        XYLog("deliverExternalPMK REJECT key=NULL\n");
        airportItlwmRegDiagRecordPlti(
            kAirportItlwmRegDiagTracePltiDeliver,
            kAirportItlwmRegDiagPmkDecisionRejectNull,
            kIOReturnBadArgument, 0, generation_echo);
        return kIOReturnBadArgument;
    }
    if (key->key_cipher_type != APPLE80211_CIPHER_PMK) {
        XYLog("deliverExternalPMK REJECT_CIPHER cipher=%u expected=%u\n",
              key->key_cipher_type, (unsigned)APPLE80211_CIPHER_PMK);
        airportItlwmRegDiagRecordPlti(
            kAirportItlwmRegDiagTracePltiDeliver,
            kAirportItlwmRegDiagPmkDecisionRejectInput,
            kIOReturnBadArgument, 0, generation_echo);
        return kIOReturnBadArgument;
    }
    if (key->key_len != IEEE80211_PMK_LEN) {
        XYLog("deliverExternalPMK REJECT_LEN key_len=%u expected=%u\n",
              key->key_len, (unsigned)IEEE80211_PMK_LEN);
        airportItlwmRegDiagRecordPlti(
            kAirportItlwmRegDiagTracePltiDeliver,
            kAirportItlwmRegDiagPmkDecisionRejectLength,
            kIOReturnBadArgument, 0, generation_echo);
        return kIOReturnBadArgument;
    }
    IOCommandGate *gate = getCommandGate();
    if (gate == nullptr) {
        XYLog("deliverExternalPMK NOT_READY gate=NULL\n");
        airportItlwmRegDiagRecordPlti(
            kAirportItlwmRegDiagTracePltiDeliver,
            kAirportItlwmRegDiagPmkDecisionNotReady,
            kIOReturnNotReady, 0, generation_echo);
        return kIOReturnNotReady;
    }
    AirportItlwmDeliverPmkArgs a;
    a.self             = this;
    a.key              = key;
    a.generation_echo  = generation_echo;
    a.rc               = kIOReturnSuccess;
    gate->runAction(&airportItlwmDeliverPmkAction, &a);
    if (a.rc == kIOReturnNotPermitted) {
        XYLog("deliverExternalPMK REJECT_GENERATION echo=%llu\n",
              (unsigned long long)generation_echo);
        return a.rc;
    }
    if (a.rc == kIOReturnNotReady) {
        XYLog("deliverExternalPMK NOT_READY ic=NULL\n");
        return a.rc;
    }
    return a.rc;
}
#endif // __IO80211_TARGET >= __MAC_26_0

/*
 * AP-mode HostAP selector wiring (Tahoe IO80211Family parity).
 *
 * The Apple BCM HostAP path stores
 * setSOFTAP_EXTENDED_CAPABILITIES_IE input bytes into a private
 * APSTA state region. The recovered body first clears qword +0x50,
 * qword +0x58 and word +0x60 (bytes 0x50..0x61), then writes the
 * input fields back at state offsets +0x50 (1 byte from input
 * +0x00), +0x51 (qword from input +0x01) and +0x59 (qword from
 * input +0x09). The qword writes at +0x51 and +0x59 are unaligned
 * inside the cleared region. That offset-pinned layout is held in
 * the host APSTA owner's state block: the
 * AirportItlwmAPSTAStateBlock fields softapAppleVendorIEExtra50,
 * softapAppleVendorIETail51 and softapAppleVendorIETail59 are
 * pinned to +0x50/+0x51/+0x59 by the compile-time static_asserts
 * in AirportItlwmAPSTAInterface.hpp. The controller-layer selector
 * forwards input through AirportItlwmAPSTAOwner::
 * setSoftAPExtCaps when the host APSTA owner has been allocated by
 * role-7 create. When the owner is absent (default STA boot before
 * role-7 create) the recovered Apple body still returns success
 * without firmware interaction, so the selector returns success
 * without touching driver state and the boot-time HostAP probe
 * completes without producing a fake AP-mode side effect.
 *
 * setMIS_MAX_STA follows the same controller-to-owner routing
 * pattern. The recovered Apple body gates on the APSTA AP-up flag;
 * when AP is up it forwards the input dword +0x00 to the maxassoc
 * backend, ignores the helper result, and returns success, and
 * when AP is down the body silently returns success. That AP-up
 * gate, the APSTA state-block fields softapAssociatedStaCount00/
 * softapMaxAssoc04/softapMaxAssocLimit08, and the net80211
 * ic->ic_max_aid mutation all live inside the host APSTA owner
 * (AirportItlwmAPSTAOwner::setMisMaxSta and setMaxAssoc) so the
 * controller-layer selector is purely a forward to the owner.
 * When the owner is absent (default STA boot before role-7 create)
 * the recovered Apple body still returns success without firmware
 * interaction, so the selector returns success without touching
 * driver state.
 *
 * The local backend for the maxassoc admission limit is the
 * OpenBSD net80211 ic->ic_max_aid field, consumed by the existing
 * AID allocation loop in ieee80211_node_join() (rejects beyond
 * limit with IEEE80211_REASON_ASSOC_TOOMANY = 17). The AID/TIM
 * bitmap allocated at attach time covers IEEE80211_AID_DEF
 * entries. The owner follows the recovered Apple cap gate:
 * same requested count is a no-op, otherwise associated + requested
 * must fit within softapMaxAssocLimit08 before state or backend
 * publication changes.
 *
 * Functional AP-mode operation requires separate iwx/iwm HAL work
 * (both currently panic on IEEE80211_M_HOSTAP). This wiring stops
 * at selector dispatch + APSTA owner state mirror + admission-
 * limit plumbing; AP firmware enablement is residual scope.
 */
bool AirportItlwm::isHostApRunning() const
{
    /*
     * Recovered Apple AppleBCMWLAN APSTA contract: AP-up is true
     * only when the host APSTA owner is present (allocated by
     * role-7 create) AND the lower HAL backend has reported a
     * successful startAPMode through AirportItlwmAPSTAOwner::
     * startLowerIfReady. The iwx and iwm HALs do not currently
     * advertise AP/GO firmware support, so the owner's isApRunning
     * gate stays false in the present runtime; this preserves the
     * structurally-false AP-up behaviour that selector callers
     * (setMIS_MAX_STA owner forward, downstream HostAP probes)
     * depend on. Once a HAL backend advertises AP/GO and
     * startLowerIfReady succeeds, the owner publishes AP-up true
     * through this gate without any further selector wiring change.
     */
    if (fAPSTAOwner == NULL) {
        return false;
    }
    return fAPSTAOwner->isApRunning();
}

bool AirportItlwm::isAPSTACoreFeatureFlagSet(uint32_t bit) const
{
    if (bit >= kAirportItlwmAPSTACoreFeatureFlagMaxExclusive) {
        return false;
    }
    const uint32_t byteIndex =
        bit >> kAirportItlwmAPSTACoreFeatureFlagIndexShift;
    const uint8_t bitMask =
        static_cast<uint8_t>(1U << (bit &
            kAirportItlwmAPSTACoreFeatureFlagIndexMask));
    return (fAPSTACoreFeatureFlags[byteIndex] & bitMask) != 0;
}

bool AirportItlwm::isAPSTASoftAPConcurrencyEnabled() const
{
    return AirportItlwmAPSTAEventContracts::softAPConcurrencyIsEnabled(
        isAPSTACoreFeatureFlagSet(kAirportItlwmAPSTAConcurrencyFeatureGate46),
        fAPSTACorePrivateFeatureByte4d59);
}

/*
 * Host APSTA owner factory.
 *
 * ensureAPSTAOwner is invoked from the role-7 (APPLE80211_VIF_SOFT_AP)
 * dispatch in AirportItlwmSkywalkInterface::setVIRTUAL_IF_CREATE.
 * It returns the existing owner if role-7 create has been called
 * before; otherwise it allocates a new AirportItlwmAPSTAOwner
 * and initializes it from the carrier descriptor. The owner stores
 * the carrier role, MAC address, and BSD name, and prepares the
 * APSTA state block (max-assoc default, beacon interval, DTIM)
 * before this function returns. AP-up remains false until a HAL
 * backend advertises AP/GO and startLowerIfReady succeeds.
 */
AirportItlwmAPSTAOwner *
AirportItlwm::ensureAPSTAOwner(const struct apple80211_virt_if_create_data *create)
{
    if (create == nullptr) {
        return nullptr;
    }
    if (fAPSTAOwner != NULL) {
        return fAPSTAOwner;
    }
    AirportItlwmAPSTAOwner *owner = new AirportItlwmAPSTAOwner;
    if (owner == NULL) {
        return nullptr;
    }
    if (!owner->initWithController(this, create)) {
        owner->release();
        return nullptr;
    }
    fAPSTAOwner = owner;
    /*
     * Bind the host APSTA owner as the net80211 station-event
     * consumer only in the scoped APSTA station-event opt-out
     * build. The producer bridge in ieee80211_node_join /
     * ieee80211_node_leave publishes
     * IEEE80211_APSTA_EVENT_{ASSOC,REASSOC,LEAVE} when an AP
     * station transitions; default Tahoe builds keep
     * IEEE80211_STA_ONLY, do not compile those publish call sites,
     * and do not register the bridge consumer. The producer-bridge
     * register is idempotent for the same (cb, arg) pair and the
     * bridge snapshots cb/arg at publication, so paired
     * (un)register around owner lifetime is sufficient for the
     * admitted opt-out surface. A register failure is treated as
     * non-fatal because the owner itself is functional without the
     * bridge binding.
     */
#ifdef IEEE80211_APSTA_STATION_EVENT_OPT_OUT
    if (fHalService != NULL) {
        struct ieee80211com *ic = fHalService->get80211Controller();
        if (ic != NULL) {
            (void)ieee80211_apsta_event_register(
                ic, AirportItlwmAPSTANet80211Event, owner);
        }
    }
#endif
    return fAPSTAOwner;
}

/*
 * Host APSTA owner cleanup.
 *
 * deleteAPSTAOwner is the explicit teardown path. It releases the
 * owner reference, which invokes AirportItlwmAPSTAOwner::free
 * -> teardown -> stopLower, stopping the lower AP backend through
 * fHalService and clearing the station table before the owner
 * memory is reclaimed. The Tahoe Skywalk dispatch surface does
 * not expose a per-role-7 delete entry point, so this function is
 * reached through AirportItlwm::releaseAll during driver release and through
 * the legacy controller VIRTUAL_IF_DELETE path. Current 25C56 Tahoe public
 * VIRTUAL_IF_DELETE SET is an unread fixed nonzero wrapper, so the Skywalk
 * public dispatcher must not treat it as an APSTA owner-teardown entry point.
 * This does not remove cleanup from release or failed-create paths.
 */
void AirportItlwm::deleteAPSTAOwner()
{
    if (fAPSTAOwner == NULL) {
        return;
    }
    /*
     * Unbind the producer-bridge consumer before releasing the
     * owner so a producer with stale cb/arg cannot dispatch into
     * reclaimed owner storage. The unregister API uses the cookie
     * passed at register time and is a no-op when no consumer is
     * currently bound. Default Tahoe builds do not bind this bridge.
     */
#ifdef IEEE80211_APSTA_STATION_EVENT_OPT_OUT
    if (fHalService != NULL) {
        struct ieee80211com *ic = fHalService->get80211Controller();
        if (ic != NULL) {
            ieee80211_apsta_event_unregister(ic, fAPSTAOwner);
        }
    }
#endif
    fAPSTAOwner->release();
    fAPSTAOwner = NULL;
}

IOReturn AirportItlwm::deleteAPSTAOwnerForBSDName(const uint8_t *bsdName)
{
    /*
     * The public delete carrier has no role field. Only the existing
     * APSTA owner can represent role 7 in this driver, so match the
     * carrier BSD name to that owner before releasing it. Non-matches
     * and absent owners stay fail-closed and do not allocate, start,
     * or publish any AP/GO state.
     */
    if (fAPSTAOwner == NULL) {
        return kIOReturnUnsupported;
    }

    if (!fAPSTAOwner->matchesBSDName(bsdName)) {
        return kIOReturnUnsupported;
    }

    deleteAPSTAOwner();
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setSOFTAP_EXTENDED_CAPABILITIES_IE(OSObject *object,
    struct apple80211_softap_extended_capabilities_info *in)
{
    /*
     * Forward the selector input through the host APSTA owner's
     * setSoftAPExtCaps entry point, which mirrors flag00/value01/
     * value09 into the offset-pinned state-block fields
     * softapAppleVendorIEExtra50/Tail51/Tail59 (recovered Apple
     * +0x50/+0x51/+0x59 layout, with the +0x51 and +0x59 qwords
     * unaligned inside the cleared region; the offsets are pinned
     * by the compile-time static_asserts in
     * AirportItlwmAPSTAInterface.hpp). The recovered Apple body
     * returns success without firmware interaction, so the absence
     * of the owner (default STA boot before role-7 create) is not
     * an error: the controller-layer selector returns success
     * without touching driver state, preserving the boot-time
     * HostAP probe completion without producing a fake AP-mode
     * side effect.
     */
    if (fAPSTAOwner == NULL) {
        return static_cast<IOReturn>(kAirportItlwmAPSTASetSoftAPExtCapsReturn);
    }
    return fAPSTAOwner->setSoftAPExtCaps(in);
}

IOReturn AirportItlwm::
setMIS_MAX_STA(OSObject *object, struct apple80211_mis_max_sta *in)
{
    /*
     * Forward the selector input through the host APSTA owner's
     * setMisMaxSta entry point. The owner enforces the recovered
     * Apple AP-up gate (only an owner whose lifecycle is Running
     * and whose lower-HAL startAPMode has reported success
     * publishes AP-up true), and on AP-up forwards the input
     * dword +0x00 through the owner's setMaxAssoc, which applies
     * the recovered associated + requested <= limit gate, writes
     * softapMaxAssoc04 in the APSTA state block, and publishes the
     * same computed payload through the net80211 ic->ic_max_aid
     * admission limit. The recovered Apple body returns success
     * without firmware
     * interaction whether AP is up or down, so the absence of
     * the owner (default STA boot before role-7 create) is not
     * an error: the controller-layer selector returns success
     * without touching driver state, preserving the boot-time
     * HostAP probe completion without producing a fake AP-mode
     * side effect.
     */
    if (fAPSTAOwner == NULL) {
        return static_cast<IOReturn>(kAirportItlwmAPSTASetMisMaxStaReturn);
    }
    return fAPSTAOwner->setMisMaxSta(in);
}

IOReturn AirportItlwm::getAPSTA_SSID(OSObject *object,
    AirportItlwmAPSTASsidDataLayout *in)
{
    if (fAPSTAOwner == NULL) {
        return kIOReturnUnsupported;
    }
    return fAPSTAOwner->getSSID(in);
}

IOReturn AirportItlwm::setAPSTA_SSID(OSObject *object,
    struct apple80211_ssid_data *in)
{
    if (fAPSTAOwner == NULL) {
        return setSSID(object, in);
    }
    return fAPSTAOwner->setSSID(in);
}

IOReturn AirportItlwm::getAPSTA_STATE(OSObject *object,
    AirportItlwmAPSTAStateDataLayout *in)
{
    if (fAPSTAOwner == NULL) {
        return kIOReturnUnsupported;
    }
    return fAPSTAOwner->getState(in);
}

IOReturn AirportItlwm::getAPSTA_OP_MODE(OSObject *object,
    AirportItlwmAPSTAOpModeDataLayout *in)
{
    if (fAPSTAOwner == NULL) {
        return kIOReturnUnsupported;
    }
    return fAPSTAOwner->getOpMode(in);
}

IOReturn AirportItlwm::getAPSTA_PEER_CACHE_MAXIMUM_SIZE(OSObject *object,
    AirportItlwmAPSTAPeerCacheMaximumSizeLayout *in)
{
    if (fAPSTAOwner == NULL) {
        return kIOReturnUnsupported;
    }
    return fAPSTAOwner->getPeerCacheMaximumSize(in);
}

IOReturn AirportItlwm::setAPSTA_CHANNEL(OSObject *object,
    struct apple80211_channel_data *in)
{
    if (fAPSTAOwner == NULL) {
        AirportItlwmSkywalkInterface *interface =
            OSDynamicCast(AirportItlwmSkywalkInterface, object);
        if (interface != NULL) {
            // The dispatcher has already admitted the controller for this
            // APSTA/no-owner fallback. Avoid re-entering the raw Skywalk
            // wrapper solely to reach its local malformed-channel contract.
            return interface->setCHANNELImpl(in);
        }
        return setCHANNEL(object, in);
    }
    return fAPSTAOwner->setChannel(in);
}

IOReturn AirportItlwm::setHOST_AP_MODE(OSObject *object,
    AirportItlwmAPSTAHostApModeNetworkDataLayout *in)
{
    /*
     * Selector 25 is the public HostAP profile boundary. The recovered
     * APSTA body validates the apple80211_network_data SSID length at
     * +0x1c and vendor-IE length at +0x2dc before entering the AP-up
     * firmware path. This driver has no persistent APSTA owner on the
     * default STA path because role-7 create tears the owner back down
     * when the HAL AP/GO gate rejects the lower start. Preserve that
     * owner/not-up failure shape here instead of falling through to a
     * generic unsupported BSD route.
     */
    (void)object;
    if (fAPSTAOwner == NULL) {
        return static_cast<IOReturn>(kAirportItlwmAPSTASetHostApModeNotUpReturn);
    }
    return fAPSTAOwner->setHostAPMode(in);
}

IOReturn AirportItlwm::setAPSTA_CIPHER_KEY(OSObject *object,
    struct apple80211_key *in)
{
    if (fAPSTAOwner == NULL) {
        return kIOReturnUnsupported;
    }
    return fAPSTAOwner->setCipherKey(in);
}

IOReturn AirportItlwm::getAPSTA_STATION_LIST(OSObject *object,
    struct apple80211_sta_data *in)
{
    if (in == nullptr) {
        return static_cast<IOReturn>(kAirportItlwmAPSTAGetStationListNullReturn);
    }
    if (fAPSTAOwner == NULL) {
        return static_cast<IOReturn>(kAirportItlwmAPSTAGetStationListNotUpReturn);
    }
    return fAPSTAOwner->getStationList(in);
}

IOReturn AirportItlwm::getAPSTA_STA_IE_LIST(OSObject *object,
    AirportItlwmAPSTAStaIEDataLayout *in)
{
    if (in == nullptr) {
        return static_cast<IOReturn>(kAirportItlwmAPSTAGetStaIEListNullReturn);
    }
    if (fAPSTAOwner == NULL) {
        return static_cast<IOReturn>(kAirportItlwmAPSTAGetStaIEListNotFoundReturn);
    }
    return fAPSTAOwner->getStaIEList(in);
}

IOReturn AirportItlwm::getAPSTA_KEY_RSC(OSObject *object,
    AirportItlwmAPSTAKeyRscDataLayout *in)
{
    if (fAPSTAOwner == NULL) {
        return kIOReturnUnsupported;
    }
    return fAPSTAOwner->getKeyRsc(in);
}

IOReturn AirportItlwm::getAPSTA_STA_STATS(OSObject *object,
    AirportItlwmAPSTAStaStatsDataLayout *in)
{
    if (fAPSTAOwner == NULL) {
        return static_cast<IOReturn>(kAirportItlwmAPSTAGetStaStatsNotUpReturn);
    }
    return fAPSTAOwner->getStaStats(in);
}

IOReturn AirportItlwm::getHOST_AP_MODE_HIDDEN(OSObject *object,
    AirportItlwmAPSTAHostApModeHiddenOutputLayout *in)
{
    if (in == nullptr) {
        return static_cast<IOReturn>(kAirportItlwmAPSTAGetHostApModeHiddenInvalidArgumentReturn);
    }
    if (fAPSTAOwner == NULL) {
        return kIOReturnUnsupported;
    }
    return fAPSTAOwner->getHostAPModeHidden(in);
}

IOReturn AirportItlwm::getSOFTAP_PARAMS(OSObject *object,
    AirportItlwmAPSTASoftAPParamsOutputLayout *in)
{
    if (fAPSTAOwner == NULL) {
        return kIOReturnUnsupported;
    }
    return fAPSTAOwner->getSoftAPParams(in);
}

IOReturn AirportItlwm::getSOFTAP_STATS(OSObject *object,
    AirportItlwmAPSTASoftAPStatsLayout *in)
{
    if (fAPSTAOwner == NULL) {
        return kIOReturnUnsupported;
    }
    return fAPSTAOwner->getSoftAPStats(in);
}

IOReturn AirportItlwm::setPEER_CACHE_CONTROL(OSObject *object,
    AirportItlwmAPSTAPeerCacheControlLayout *in)
{
    if (fAPSTAOwner == NULL) {
        return kIOReturnUnsupported;
    }
    return fAPSTAOwner->setPeerCacheControl(in);
}

IOReturn AirportItlwm::setHOST_AP_MODE_HIDDEN(OSObject *object,
    AirportItlwmAPSTAHostApModeHiddenLayout *in)
{
    if (fAPSTAOwner == NULL) {
        return static_cast<IOReturn>(kAirportItlwmAPSTAHiddenNotUpReturn);
    }
    return fAPSTAOwner->setHostAPModeHidden(in);
}

IOReturn AirportItlwm::setSTA_AUTHORIZE(OSObject *object,
    AirportItlwmAPSTAStaAuthorizeInputLayout *in)
{
    if (in == nullptr) {
        return static_cast<IOReturn>(kAirportItlwmAPSTAStaAuthorizeNullReturn);
    }
    if (fAPSTAOwner == NULL) {
        return static_cast<IOReturn>(kAirportItlwmAPSTASoftAPNotReadyReturn);
    }
    return fAPSTAOwner->setStationAuthorization(in);
}

IOReturn AirportItlwm::setSTA_DISASSOCIATE(OSObject *object,
    AirportItlwmAPSTAStaDisassocInputLayout *in,
    bool deauth)
{
    if (fAPSTAOwner == NULL) {
        return static_cast<IOReturn>(kAirportItlwmAPSTASoftAPNotReadyReturn);
    }
    return fAPSTAOwner->setStationDisassociation(in, deauth);
}

IOReturn AirportItlwm::setSOFTAP_PARAMS(OSObject *object,
    AirportItlwmAPSTASoftAPParamsInputLayout *in)
{
    if (fAPSTAOwner == NULL) {
        return static_cast<IOReturn>(kAirportItlwmAPSTASoftAPNotReadyReturn);
    }
    return fAPSTAOwner->setSoftAPParams(in);
}

IOReturn AirportItlwm::setRSN_CONF(OSObject *object,
    struct apple80211_rsn_conf_data *in)
{
    (void)object;
    if (fAPSTAOwner == NULL) {
        return kIOReturnUnsupported;
    }
    return fAPSTAOwner->setRsnConf(in);
}

IOReturn AirportItlwm::setSOFTAP_TRIGGER_CSA(OSObject *object,
    AirportItlwmAPSTACsaInputLayout *in)
{
    if (fAPSTAOwner == NULL) {
        return static_cast<IOReturn>(kAirportItlwmAPSTACsaNotUpReturn);
    }
    return fAPSTAOwner->setSoftAPTriggerCSA(in);
}

IOReturn AirportItlwm::setSOFTAP_WIFI_NETWORK_INFO_IE(OSObject *object,
    AirportItlwmAPSTASoftAPWifiNetworkInfoCarrierLayout *in)
{
    if (fAPSTAOwner != NULL) {
        return fAPSTAOwner->setSoftAPWifiNetworkInfoIE(in);
    }
    if (in == nullptr) {
        return kIOReturnBadArgument;
    }
    return static_cast<IOReturn>(kAirportItlwmAPSTASoftAPNotReadyReturn);
}
