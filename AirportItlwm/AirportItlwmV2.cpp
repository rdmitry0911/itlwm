//
//  AirportItlwmV2.cpp
//  AirportItlwm-Sonoma
//
//  Created by qcwap on 2023/6/27.
//  Copyright © 2023 钟先耀. All rights reserved.
//

#include "AirportItlwmV2.hpp"
#include <linux/iwx_diag_log.h>
#include "AirportItlwmRegDiag.hpp"
#include "AirportItlwmAPSTAOwner.hpp"
#include <sys/_netstat.h>
#include <crypto/sha1.h>
#include <net80211/ieee80211_priv.h>
#include <net80211/ieee80211_var.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSString.h>
#include <libkern/c++/OSSymbol.h>
#include <libkern/c++/OSMetaClass.h>

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
#include "Airport/IO80211NetworkPacket.h"
#include "IOPCIEDeviceWrapper.hpp"
#include <IOKit/skywalk/IOSkywalkPacketBuffer.h>
#if __IO80211_TARGET >= __MAC_26_0
#include <IOKit/IOUserClient.h>
#endif

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
        XYLog("itlwm: PACKETPOOL[%s] new=0x%x_%x poolVtable=0x%x_%x "
              "(size=%lu opts=0x%x_%x owner=0x%x_%x ownerVtable=0x%x_%x "
              "pktCount=%u bufCount=%u bufSize=%u maxBPP=%u memSegSz=%u "
              "poolFlags=0x%x type=%d)\n",
              name ? name : "(null)",
              ptrHi32(pool), ptrLo32(pool),
              ptrHi32(pool ? *reinterpret_cast<void **>(pool) : nullptr),
              ptrLo32(pool ? *reinterpret_cast<void **>(pool) : nullptr),
              sizeof(AirportItlwmIO80211PacketPool),
              ptrHi32(options), ptrLo32(options),
              ptrHi32(owner), ptrLo32(owner),
              ptrHi32(owner ? *reinterpret_cast<void **>(owner) : nullptr),
              ptrLo32(owner ? *reinterpret_cast<void **>(owner) : nullptr),
              options ? options->packetCount : 0,
              options ? options->bufferCount : 0,
              options ? options->bufferSize : 0,
              options ? options->maxBuffersPerPacket : 0,
              options ? options->memorySegmentSize : 0,
              options ? options->poolFlags : 0,
              (int)packetType);
        if (pool == nullptr) {
            XYLog("itlwm: PACKETPOOL[%s] FAIL: new returned NULL\n",
                  name ? name : "(null)");
            XYLog("itlwm: PACKETPOOL[%s] FINAL branch=NEW_NULL return=0x0_0\n",
                  name ? name : "(null)");
            return nullptr;
        }

        bool ok = pool->initWithName(name, owner, packetType, options);

        // Read every internal-state slot the framework's initWithName
        // writes, in chronological order of the writes per its disasm.
        // Each 64-bit pointer slot is split into hi/lo uint32_t halves
        // (slotHi32 / slotLo32) to bypass os_log privacy redaction.
        uint8_t *poolBytes = reinterpret_cast<uint8_t *>(pool);
        uint32_t s_name_hi  = slotHi32(poolBytes, 0x98);  // OSString name (0x9c5f)
        uint32_t s_name_lo  = slotLo32(poolBytes, 0x98);
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
        uint32_t s_flagsCache =
            *reinterpret_cast<uint32_t *>(poolBytes + 0x48);  // poolFlags cache (0x9cf6)
        uint8_t  s_singleSeg  =
            *reinterpret_cast<uint8_t *>(poolBytes + 0xb8);   // mSingleMemorySegment (0x9d60)
        uint8_t  s_disposed   =
            *reinterpret_cast<uint8_t *>(poolBytes + 0xba);   // mDisposed (0x9ef3)

        // Slot non-zero predicates: a 64-bit value is non-zero iff either
        // half is non-zero.
        bool nz_name  = (s_name_hi  | s_name_lo)  != 0;
        bool nz_thC   = (s_thC_hi   | s_thC_lo)   != 0;
        bool nz_seg   = (s_seg_hi   | s_seg_lo)   != 0;
        bool nz_lk1   = (s_lk1_hi   | s_lk1_lo)   != 0;
        bool nz_lk2   = (s_lk2_hi   | s_lk2_lo)   != 0;
        bool nz_own   = (s_own_hi   | s_own_lo)   != 0;
        bool nz_pbp   = (s_pbp_hi   | s_pbp_lo)   != 0;
        bool nz_a1    = (s_a1_hi    | s_a1_lo)    != 0;
        bool nz_a2    = (s_a2_hi    | s_a2_lo)    != 0;
        (void)nz_name;

        XYLog("itlwm: PACKETPOOL[%s] initWithName=%d (type=%d) slots: "
              "name=0x%x_%x thCall=0x%x_%x segStats=0x%x_%x "
              "lock1=0x%x_%x lock2=0x%x_%x owner=0x%x_%x "
              "pbufpool=0x%x_%x arr1=0x%x_%x arr2=0x%x_%x "
              "typeCache=%u flagsCache=0x%x singleSeg=%u disposed=%u\n",
              name ? name : "(null)", ok ? 1 : 0, (int)packetType,
              s_name_hi, s_name_lo,
              s_thC_hi,  s_thC_lo,
              s_seg_hi,  s_seg_lo,
              s_lk1_hi,  s_lk1_lo,
              s_lk2_hi,  s_lk2_lo,
              s_own_hi,  s_own_lo,
              s_pbp_hi,  s_pbp_lo,
              s_a1_hi,   s_a1_lo,
              s_a2_hi,   s_a2_lo,
              s_typeCache, s_flagsCache,
              (unsigned)s_singleSeg, (unsigned)s_disposed);

        if (ok) {
            XYLog("itlwm: PACKETPOOL[%s] FINAL branch=INIT_TRUE "
                  "return=0x%x_%x pbufpool=0x%x_%x owner=0x%x_%x "
                  "arr1=0x%x_%x arr2=0x%x_%x\n",
                  name ? name : "(null)",
                  ptrHi32(pool), ptrLo32(pool),
                  s_pbp_hi, s_pbp_lo,
                  s_own_hi, s_own_lo,
                  s_a1_hi,  s_a1_lo,
                  s_a2_hi,  s_a2_lo);
            return pool;
        }

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
    // dispatch pattern (`newPacket` → `newPacketWithDescriptor`) and
    // logs both ALLOC_NULL and OK terminal points using the CR-216
    // split-halves redaction-bypass helpers.
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
        XYLog("itlwm: NEWPACKET FINAL branch=OK "
              "this=0x%x_%x packet=0x%x_%x\n",
              ptrHi32(this), ptrLo32(this),
              ptrHi32(p),    ptrLo32(p));
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
    kAirportItlwmUserClientMethod_NumMethods
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

private:
    AirportItlwm *fProvider;
    task_t       fOwningTask;
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
static IOInterruptEventSource *_fLinkStatePublishSource;
static IOSimpleLock *_fLinkStatePublishLock;
static bool _fLinkStatePublishPendingValid;
static IO80211LinkState _fLinkStatePublishPendingState;
static unsigned int _fLinkStatePublishPendingRawCode;

static void publishLinkStateInterruptAction(OSObject *owner,
                                            IOInterruptEventSource *sender,
                                            int count)
{
    AirportItlwm *that = OSDynamicCast(AirportItlwm, owner);
    if (that == NULL || _fLinkStatePublishLock == NULL)
        return;
    IO80211LinkState linkState;
    unsigned int rawCode;
    bool valid;
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(_fLinkStatePublishLock);
    valid = _fLinkStatePublishPendingValid;
    linkState = _fLinkStatePublishPendingState;
    rawCode = _fLinkStatePublishPendingRawCode;
    _fLinkStatePublishPendingValid = false;
    IOSimpleLockUnlockEnableInterrupt(_fLinkStatePublishLock, irq);
    if (!valid)
        return;
    AirportItlwm::setLinkStateGated(that, (void *)(uintptr_t)linkState,
                                    (void *)(uintptr_t)rawCode, NULL, NULL);
}

static void queueOffGateLinkStatePublish(AirportItlwm *that,
                                         IO80211LinkState linkState,
                                         unsigned int rawCode)
{
    if (_fLinkStatePublishSource == NULL || _fLinkStatePublishLock == NULL)
        return;
    IOInterruptState irq = IOSimpleLockLockDisableInterrupt(_fLinkStatePublishLock);
    _fLinkStatePublishPendingState = linkState;
    _fLinkStatePublishPendingRawCode = rawCode;
    _fLinkStatePublishPendingValid = true;
    IOSimpleLockUnlockEnableInterrupt(_fLinkStatePublishLock, irq);
    _fLinkStatePublishSource->interruptOccurred(0, 0, 0);
}

// Drain and release the off-gate publication source. Idempotent. Must be called
// before fNetIf is detached/released so a deferred action cannot reach the
// publication worker after fNetIf's lifetime ends: removeEventSource drains the
// in-flight action on the work-queue thread, and the pending record is cleared
// so no further transition can be serviced.
static void teardownLinkStatePublishSource(void)
{
    if (_fWorkloop && _fLinkStatePublishSource) {
        _fLinkStatePublishSource->disable();
        _fWorkloop->removeEventSource(_fLinkStatePublishSource);
    }
    if (_fLinkStatePublishLock) {
        IOInterruptState irq = IOSimpleLockLockDisableInterrupt(_fLinkStatePublishLock);
        _fLinkStatePublishPendingValid = false;
        IOSimpleLockUnlockEnableInterrupt(_fLinkStatePublishLock, irq);
    }
    if (_fLinkStatePublishSource) {
        _fLinkStatePublishSource->release();
        _fLinkStatePublishSource = NULL;
    }
    if (_fLinkStatePublishLock) {
        IOSimpleLockFree(_fLinkStatePublishLock);
        _fLinkStatePublishLock = NULL;
    }
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

IOReturn
AirportItlwm::setProperties(OSObject *properties)
{
    if (OSDictionary *dict = OSDynamicCast(OSDictionary, properties)) {
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
        case APPLE80211_IOC_SET_MAC_ADDRESS:
            return isSet;
        case APPLE80211_IOC_CIPHER_KEY:
            // Tahoe Skywalk PMK / pairwise / group / PMKSA key ingress
            // through the card-specific bridge. The recovered Apple
            // delivery for APPLE80211_IOC_CIPHER_KEY = 3 with
            // key_cipher_type = APPLE80211_CIPHER_PMK (6) or
            // APPLE80211_CIPHER_MSK (9) reaches the driver as a
            // card-specific SIOCSA80211 IOCTL on the Skywalk
            // interface. Without an explicit route here the IOCTL
            // falls back to the default IO80211 path and never
            // reaches the local setCIPHER_KEY handler, so the
            // shared external-PMK ingestion sink is never invoked
            // and ieee80211com::ic_psk stays empty before the host
            // supplicant consumes its first 4-way M1. Route the
            // SET-side here so case 6 / case 9 converge on
            // installExternalPmkLocked the same way Apple
            // AppleBCMWLANCore PMK owner state is populated.
            return isSet;
        case APPLE80211_IOC_CUR_PMK:
            // Tahoe Skywalk current-PMK ingress through the
            // card-specific bridge. The recovered Apple delivery for
            // selector 0x168 / IOC 360 reaches the driver as a
            // card-specific IOCTL on the Skywalk interface; routing
            // both SIOCSA80211 and SIOCGA80211 through processApple80211Ioctl
            // gives the local sink a single concrete entry point.
            // The SIOCGA80211 path keeps the credential-safe Apple
            // failure 0xe00002c7 from getCUR_PMK and never snapshots
            // PMK material into the caller buffer.
            return true;
        case APPLE80211_IOC_SOFTAP_EXTENDED_CAPABILITIES_IE:
        case APPLE80211_IOC_MIS_MAX_STA:
            return isSet;
        default:
            return false;
    }
}

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

static uint32_t buildTahoeWclLinkReason(unsigned int rawReason)
{
    if (rawReason == 0)
        return kTahoeWclInvalidLinkReason;

    const uint32_t mapped = static_cast<uint32_t>(rawReason - 1);
    return (mapped < 9) ? mapped : kTahoeWclInvalidLinkReason;
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

    XYLog("DEBUG %s msg=0x%x bssid=%s linkState=%u interfaceType=%u reason=0x%x\n",
          __FUNCTION__, kTahoeWclLinkChanged, ether_sprintf(payload.bssid),
          payload.linkState, payload.interfaceType, payload.reasonCode);
    controller->postMessage(controller->fNetIf, kTahoeWclLinkChanged,
                            &payload, sizeof(payload), true);
    return true;
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

// CR-230 DIAGNOSTIC_INSTRUMENTATION: log a per-byte hex dump of the
// 0xa4-byte payload we ship to the IO80211 bulletin board so we can
// verify the on-the-wire bytes match what the AppleBCMWLAN reference
// (BootKC 0xffffff800158abce sendConnectComplete -> bulletin board
// subscriber 0xffffff800220a9a2 connectCompleteEventHandler ->
// 0xffffff8002232ab0 updateConnectCompleteEvent) reads from the same
// payload offsets. Reference decomp:
// analysis/cr230_applebcmwlan_sendConnectComplete_2026_04_29.txt.
//
// Also captures the postMessage return code to confirm whether the
// framework accepted, queued, or rejected our event. CR-229 Stage 2
// proved the next-layer blocker is `ENCAP_BR_PORT_NOT_VALID` because
// `ni_port_valid` never flips to 1; airportd never issues
// setCIPHER_KEY because it sees `isAssociated=0`. This event is the
// only known driver→framework signal that should mark association
// complete on Tahoe; if airportd isn't acting on it, we need to
// know whether the issue is (a) wrong payload bytes, (b) the post
// not reaching the WCL subscriber, or (c) something earlier in the
// state machine gating. The diagnostic logs the payload + post
// return value at the only producer site so we can attribute.
// CR-232: safe payload hex dumper. CR-230's earlier version
// unconditionally read p[row+0..15] before the trailing-row check,
// which over-read the 0xa4-byte payload by 12 bytes on the final
// row (row=0xa0 → reads p[0xa0..0xaf], but valid range is p[0..0xa3]).
// CR-231 reviewer flagged this as a kernel-safety blocker. Every
// dereference is now guarded by `idx < sizeof(*payload)`; OOB indices
// pad with 0 in the format-arg list so the log line keeps a fixed
// 16-byte-per-row visual structure.
static void
cr232LogPayloadHex(const apple80211_wcl_connect_complete_event *payload)
{
    const uint8_t *p = reinterpret_cast<const uint8_t *>(payload);
    const size_t plen = sizeof(*payload);  // 0xa4
    for (size_t row = 0; row < plen; row += 16) {
        // Each byte is fetched safely: returns 0 if outside the valid
        // [0..plen) range. The 16-arg format is preserved so the log
        // line is always exactly 16 hex pairs.
        #define BYTE_AT(off) (((row + (off)) < plen) ? p[row + (off)] : (uint8_t)0)
        XYLog("DEBUG CR230_PAYLOAD off=0x%02zx %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n",
              row,
              BYTE_AT(0),  BYTE_AT(1),  BYTE_AT(2),  BYTE_AT(3),
              BYTE_AT(4),  BYTE_AT(5),  BYTE_AT(6),  BYTE_AT(7),
              BYTE_AT(8),  BYTE_AT(9),  BYTE_AT(10), BYTE_AT(11),
              BYTE_AT(12), BYTE_AT(13), BYTE_AT(14), BYTE_AT(15));
        #undef BYTE_AT
    }
}

// CR-231 end-to-end branch coverage of the WCL connect-complete
// hypothesis. Adds rate-limited log emitters at every checkpoint our
// kext can observe between (a) producer entry and (b) airportd-facing
// IOCTL responses. CR-230 covered branches 1-3 (producer state,
// payload bytes, post markers); CR-231 adds:
//   branch 4: every apple80211 IOCTL airportd polls — single log at
//             processApple80211Ioctl entry with req_type + ic_state.
//   branch 5: every postMessage call from kext — log msg-code + len
//             + ic_state at the centralized postMessageGated and at
//             each direct controller->postMessage() call site near
//             LINK_UP / ASSOC events.
//   branch 6: producer entry expansion — log fNetIf + ic_state +
//             ic_bss + currentStatus + power_state at every entry
//             into postTahoeWclConnectCompleteEvent.
//   branch 7: IOREG state snapshot — log CoreWiFiDriverReadyKey and
//             interface state once per LINK_UP event.
// Each emitter has a per-site rate limit (16 emissions) to bound log
// volume during a stalled handshake.
#define CR231_LOG_LIMIT 16
#define CR231_LOG(fmt, ...) do { \
    static volatile unsigned int _cr231_n; \
    unsigned int _v = ++_cr231_n; \
    if (_v <= CR231_LOG_LIMIT) { XYLog(fmt, ##__VA_ARGS__); } \
} while (0)

// CR-234/CR-235: per-site 32-cap logger for the getAssocState
// reader/writer end-to-end probes. Distinct from CR231_LOG so the
// rate-limit advertised in the request matches the binary.
#define CR234_LOG_LIMIT 32
#define CR234_LOG(fmt, ...) do { \
    static volatile unsigned int _cr234_n; \
    unsigned int _v = ++_cr234_n; \
    if (_v <= CR234_LOG_LIMIT) { XYLog(fmt, ##__VA_ARGS__); } \
} while (0)

// CR-236: file-scope atomic counter from SkywalkInterface.cpp.
// Incremented on every entry to our getAssocState override; read
// here at d5_producer snapshots (capped at 32 events spanning the
// connect window) so we get periodic samples of cumulative call
// rate. Resolves the CR-235 deferred polling-frequency question.
// extern "C" linkage avoids name-mangling mismatch from the
// surrounding anonymous namespace.
extern "C" volatile uint64_t cr236GetAssocStateCount;

// CR-237: end-to-end uncapped counters for every potential PSK/PMK
// delivery channel. Snapshot of all 12 counters at d5_producer below
// gives a timeline-correlated dump of which channels are firing
// during the connect window (and which are silent).
extern "C" volatile uint64_t cr237_setCipherKey_count;
extern "C" volatile uint64_t cr237_setPTK_count;
extern "C" volatile uint64_t cr237_setGTK_count;
extern "C" volatile uint64_t cr237_setRSN_IE_count;
extern "C" volatile uint64_t cr237_setAUTH_TYPE_count;
extern "C" volatile uint64_t cr237_setWCL_ASSOCIATE_count;
extern "C" volatile uint64_t cr237_setWCL_LINK_UP_DONE_count;
extern "C" volatile uint64_t cr237_setWCL_REASSOC_count;
extern "C" volatile uint64_t cr237_setWCL_LINK_STATE_UPDATE_count;
extern "C" volatile uint64_t cr237_processApple80211Ioctl_count;
extern "C" volatile uint64_t cr237_processBSDCommand_count;
extern "C" volatile uint64_t cr237_associateSSID_count;
extern "C" volatile uint64_t cr237_eapol_rx_count;

// CR-258: extended PMK-delivery instrumentation. See SkywalkInterface.cpp
// for declarations and payload-shape detection rationale. Snapshot at
// d5_producer dumps the per-channel counters AND the non-zero entries
// of the per-opcode array, so we see exactly which Apple80211 opcodes
// fire on Tahoe and which set* methods are reached. Branch D (IORegistry
// setProperty / non-EAPOL inputPacket / WCLJoinRequest accessor) is OUT
// OF SCOPE for this CR; no Branch D counters are declared or snapshotted.
extern "C" volatile uint64_t cr257_setASSOCIATE_count;
extern "C" volatile uint64_t cr257_setIE_count;
extern "C" volatile uint64_t cr257_setRSN_XE_count;
extern "C" volatile uint64_t cr257_setSET_PROPERTY_count;
extern "C" volatile uint64_t cr257_setCLEAR_PMKSA_CACHE_count;
extern "C" volatile uint64_t cr257_setWCL_TRIGGER_CC_count;
extern "C" volatile uint64_t cr257_setWCL_SCAN_REQ_count;
extern "C" volatile uint64_t cr257_setWCL_LEAVE_NETWORK_count;
extern "C" volatile uint64_t cr257_setWCL_SCAN_ABORT_count;
extern "C" volatile uint64_t cr257_setWCL_SET_ROAM_LOCK_count;
extern "C" volatile uint64_t cr257_setWCL_UPDATE_FAST_LANE_count;
extern "C" volatile uint64_t cr257_setWCL_REAL_TIME_MODE_count;
extern "C" volatile uint64_t cr257_setWCL_ACTION_FRAME_count;
extern "C" volatile uint64_t cr257_setWCL_ROAM_USER_CACHE_count;
extern "C" volatile uint64_t cr257_setWCL_LEGACY_ROAM_PROFILE_CONFIG_count;
extern "C" volatile uint64_t cr257_setWCL_ROAM_PROFILE_CONFIG_count;
extern "C" volatile uint64_t cr257_setWCL_ARP_MODE_count;
extern "C" volatile uint64_t cr257_setWCL_CONFIG_BG_MOTIONPROFILE_count;
extern "C" volatile uint64_t cr257_setWCL_CONFIG_BG_NETWORK_count;
extern "C" volatile uint64_t cr257_setWCL_CONFIG_BGSCAN_count;
extern "C" volatile uint64_t cr257_setWCL_CONFIG_BG_PARAMS_count;
extern "C" volatile uint64_t cr257_setWCL_JOIN_ABORT_count;
extern "C" volatile uint64_t cr257_setWCL_QOS_PARAMS_count;
extern "C" volatile uint64_t cr257_setWCL_SET_SCAN_HOME_AWAY_TIME_count;
extern "C" volatile uint64_t cr257_setWCL_ULOFDMA_STATE_count;
extern "C" volatile uint64_t cr257_setWCL_LIMITED_AGGREGATION_count;
extern "C" volatile uint64_t cr257_setWCL_BCN_MUTE_CONFIG_count;
extern "C" volatile uint64_t cr257_setWCL_ASSOCIATED_SLEEP_count;
extern "C" volatile uint64_t cr257_setWCL_SOI_CONFIG_count;
extern "C" volatile uint64_t cr257_setWCL_WNM_OPS_count;
extern "C" volatile uint64_t cr257_setWCL_WNM_OFFLOAD_count;
extern "C" volatile uint64_t cr257_apple80211_ioctl_per_op[320];

static bool postTahoeWclConnectCompleteEvent(AirportItlwm *controller)
{
    if (controller == nullptr || controller->fNetIf == nullptr)
        return false;

    // CR-231 BRANCH 6: producer entry-state expansion.
    // Captures all fields that gate buildTahoeWclConnectCompletePayload
    // and the upstream LinkState/RUN preconditions, so Stage 2 evidence
    // can attribute "post fired but framework state wrong" vs. "post
    // skipped because of state".
    {
        struct ieee80211com *ic = (controller->fHalService != nullptr)
            ? controller->fHalService->get80211Controller() : nullptr;
        const uint8_t *bssid = (ic && ic->ic_bss) ? ic->ic_bss->ni_bssid : nullptr;
        CR231_LOG("DEBUG CR231_PRODUCER_STATE fNetIf=%d ic=%d ic_state=%d ic_bss=%d "
                  "ic_flags=0x%x bssid=%02x:%02x:%02x:%02x:%02x:%02x\n",
                  controller->fNetIf != nullptr ? 1 : 0,
                  ic != nullptr ? 1 : 0,
                  ic ? (int)ic->ic_state : -1,
                  ic && ic->ic_bss ? 1 : 0,
                  ic ? (unsigned)ic->ic_flags : 0,
                  bssid ? bssid[0] : 0, bssid ? bssid[1] : 0,
                  bssid ? bssid[2] : 0, bssid ? bssid[3] : 0,
                  bssid ? bssid[4] : 0, bssid ? bssid[5] : 0);
    }

    apple80211_wcl_connect_complete_event payload;
    if (!buildTahoeWclConnectCompletePayload(controller, &payload))
        return false;

    XYLog("DEBUG %s msg=0x%x len=0x%zx status=%u reason=%u bssid=%s\n",
          __FUNCTION__, APPLE80211_M_WCL_CONNECT_COMPLETE_EVENT,
          sizeof(payload), payload.status, payload.reason,
          ether_sprintf(payload.records[0].bssid));
    // CR-232: dump the actual payload bytes the framework will read.
    // (CR-230's unsafe original was replaced after the CR-231 reviewer
    //  flagged out-of-bounds reads on the final row.)
    cr232LogPayloadHex(&payload);
    // CR-231 BRANCH 7: IOREG state snapshot at LINK_UP post site.
    {
        OSObject *prop = controller->fNetIf
            ? controller->fNetIf->getProperty("CoreWiFiDriverReadyKey")
            : nullptr;
        OSString *propStr = OSDynamicCast(OSString, prop);
        const char *propVal = propStr ? propStr->getCStringNoCopy() : "<missing>";
        CR231_LOG("DEBUG CR231_IOREG_STATE CoreWiFiDriverReadyKey=%s fNetIf=%d\n",
                  propVal, controller->fNetIf != nullptr ? 1 : 0);
    }
    // CR-234 BRANCH E: dump BOTH reader bytes + identity check at d5
    // producer entry. i120_88 is what IO80211SkywalkInterface::
    // getAssocState reads; i128_180 is what IO80211InfraInterface::
    // getAssocState reads. i128_198/19c are the bssid bytes the
    // framework's setCurrentApAddress writes — comparing to the
    // bssid we passed proves the inner pointer at this+0x128 matches
    // the framework's view.
    {
        const void *thisPtr = controller->fNetIf;
        if (thisPtr != nullptr) {
            const char *base = reinterpret_cast<const char *>(thisPtr);
            const void *i120 = *reinterpret_cast<const void * const *>(base + 0x120);
            const void *i128 = *reinterpret_cast<const void * const *>(base + 0x128);
            uint8_t b120_88 = (i120 != nullptr)
                ? *(reinterpret_cast<const uint8_t *>(i120) + 0x88) : 0xff;
            uint8_t b128_180 = (i128 != nullptr)
                ? *(reinterpret_cast<const uint8_t *>(i128) + 0x180) : 0xff;
            uint32_t b128_198 = (i128 != nullptr)
                ? *(reinterpret_cast<const uint32_t *>(reinterpret_cast<const uint8_t *>(i128) + 0x198)) : 0;
            uint16_t b128_19c = (i128 != nullptr)
                ? *(reinterpret_cast<const uint16_t *>(reinterpret_cast<const uint8_t *>(i128) + 0x19c)) : 0;
            CR234_LOG("DEBUG CR234_INNER d5_producer thisPtr=%p i120=%p i128=%p "
                      "i120_88=0x%02x i128_180=0x%02x i128_198=0x%08x i128_19c=0x%04x\n",
                      thisPtr, i120, i128,
                      (unsigned)b120_88, (unsigned)b128_180,
                      (unsigned)b128_198, (unsigned)b128_19c);
            // CR-236: snapshot of uncapped getAssocState counter.
            // Resolves CR-235 deferred polling-frequency question.
            CR234_LOG("DEBUG CR236_POLL d5_producer getAssocState_count=%llu\n",
                      (unsigned long long)__atomic_load_n(&cr236GetAssocStateCount,
                                                         __ATOMIC_RELAXED));
            // CR-237: snapshot of all 12 PSK-delivery channel counters.
            // End-to-end coverage per feedback_diagnostic_end_to_end_criterion:
            //   - H1 BSD-ioctl path : bsdcmd / ioctl / setCipherKey
            //   - H2 WCL channels   : wclAssoc / wclLinkUpDone / wclReassoc
            //   - H3 Pre-associate  : setRSN_IE / setAUTH_TYPE
            //   - H4 Key install    : setPTK / setGTK
            //   - H5 Convergence    : associateSSID
            //   - H6 EAPOL RX       : eapolRX
            CR234_LOG("DEBUG CR237_POLL d5_producer "
                      "cipherKey=%llu PTK=%llu GTK=%llu RSN_IE=%llu AUTH_TYPE=%llu "
                      "wclAssoc=%llu wclLinkUp=%llu wclReassoc=%llu wclLSU=%llu "
                      "ioctl=%llu bsdcmd=%llu assocSSID=%llu eapolRX=%llu\n",
                      (unsigned long long)__atomic_load_n(&cr237_setCipherKey_count,            __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr237_setPTK_count,                  __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr237_setGTK_count,                  __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr237_setRSN_IE_count,               __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr237_setAUTH_TYPE_count,            __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr237_setWCL_ASSOCIATE_count,        __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr237_setWCL_LINK_UP_DONE_count,     __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr237_setWCL_REASSOC_count,          __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr237_setWCL_LINK_STATE_UPDATE_count,__ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr237_processApple80211Ioctl_count,  __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr237_processBSDCommand_count,       __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr237_associateSSID_count,           __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr237_eapol_rx_count,                __ATOMIC_RELAXED));
            // CR-257: extended snapshot covering Tier-1 candidates
            // (legacy direct PMK carriers + IE channels) and all
            // uninstrumented setWCL_* methods.
            CR234_LOG("DEBUG CR257_POLL_TIER1 d5_producer "
                      "setASSOC=%llu setIE=%llu setRSN_XE=%llu "
                      "setSET_PROP=%llu setCLEAR_PMKSA=%llu\n",
                      (unsigned long long)__atomic_load_n(&cr257_setASSOCIATE_count,            __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr257_setIE_count,                   __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr257_setRSN_XE_count,               __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr257_setSET_PROPERTY_count,         __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr257_setCLEAR_PMKSA_CACHE_count,    __ATOMIC_RELAXED));
            CR234_LOG("DEBUG CR257_POLL_WCL_A d5_producer "
                      "TRIG_CC=%llu SCAN_REQ=%llu LEAVE_NET=%llu SCAN_ABORT=%llu "
                      "ROAM_LOCK=%llu FAST_LANE=%llu RT_MODE=%llu ACT_FR=%llu\n",
                      (unsigned long long)__atomic_load_n(&cr257_setWCL_TRIGGER_CC_count,                __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr257_setWCL_SCAN_REQ_count,                  __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr257_setWCL_LEAVE_NETWORK_count,             __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr257_setWCL_SCAN_ABORT_count,                __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr257_setWCL_SET_ROAM_LOCK_count,             __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr257_setWCL_UPDATE_FAST_LANE_count,          __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr257_setWCL_REAL_TIME_MODE_count,            __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr257_setWCL_ACTION_FRAME_count,              __ATOMIC_RELAXED));
            CR234_LOG("DEBUG CR257_POLL_WCL_B d5_producer "
                      "ROAM_UC=%llu LEG_RPC=%llu RPC=%llu ARP=%llu "
                      "BG_MP=%llu BG_NET=%llu BGSCAN=%llu BG_PRM=%llu\n",
                      (unsigned long long)__atomic_load_n(&cr257_setWCL_ROAM_USER_CACHE_count,           __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr257_setWCL_LEGACY_ROAM_PROFILE_CONFIG_count,__ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr257_setWCL_ROAM_PROFILE_CONFIG_count,       __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr257_setWCL_ARP_MODE_count,                  __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr257_setWCL_CONFIG_BG_MOTIONPROFILE_count,   __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr257_setWCL_CONFIG_BG_NETWORK_count,         __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr257_setWCL_CONFIG_BGSCAN_count,             __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr257_setWCL_CONFIG_BG_PARAMS_count,          __ATOMIC_RELAXED));
            CR234_LOG("DEBUG CR257_POLL_WCL_C d5_producer "
                      "JOIN_ABT=%llu QOS=%llu HAW_TIME=%llu ULOFDMA=%llu "
                      "LIM_AGG=%llu BCN_MUTE=%llu ASSOC_SLP=%llu SOI=%llu "
                      "WNM_OPS=%llu WNM_OFF=%llu\n",
                      (unsigned long long)__atomic_load_n(&cr257_setWCL_JOIN_ABORT_count,                __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr257_setWCL_QOS_PARAMS_count,                __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr257_setWCL_SET_SCAN_HOME_AWAY_TIME_count,   __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr257_setWCL_ULOFDMA_STATE_count,             __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr257_setWCL_LIMITED_AGGREGATION_count,       __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr257_setWCL_BCN_MUTE_CONFIG_count,           __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr257_setWCL_ASSOCIATED_SLEEP_count,          __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr257_setWCL_SOI_CONFIG_count,                __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr257_setWCL_WNM_OPS_count,                   __ATOMIC_RELAXED),
                      (unsigned long long)__atomic_load_n(&cr257_setWCL_WNM_OFFLOAD_count,               __ATOMIC_RELAXED));
            // Dump non-zero entries of per-opcode counter array. Bounded
            // emission per d5 fire so logs don't explode if airportd
            // hammers many opcodes.
            {
                int nz_emitted = 0;
                for (int op = 0;
                     op < (int)(sizeof(cr257_apple80211_ioctl_per_op) /
                                sizeof(cr257_apple80211_ioctl_per_op[0]));
                     op++) {
                    uint64_t v = __atomic_load_n(&cr257_apple80211_ioctl_per_op[op],
                                                 __ATOMIC_RELAXED);
                    if (v == 0) continue;
                    CR234_LOG("DEBUG CR257_POLL_OP d5_producer op=%d count=%llu\n",
                              op, (unsigned long long)v);
                    if (++nz_emitted >= 24) break;  // bound per-fire
                }
            }
        }
    }
    XYLog("DEBUG CR230_POST_PRE msg=0x%x size=0x%zx\n",
          APPLE80211_M_WCL_CONNECT_COMPLETE_EVENT, sizeof(payload));
    controller->postMessage(controller->fNetIf,
                            APPLE80211_M_WCL_CONNECT_COMPLETE_EVENT,
                            &payload, sizeof(payload), true);
    XYLog("DEBUG CR230_POST_DONE msg=0x%x\n",
          APPLE80211_M_WCL_CONNECT_COMPLETE_EVENT);
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

static void
logTahoeSkywalkLinkCarrier(const char *tag, IO80211SkywalkInterface *interface)
{
    if (interface == nullptr) {
        XYLog("DEBUG %s fNetIf=(null)\n", tag);
        return;
    }

    uint8_t *raw = reinterpret_cast<uint8_t *>(interface);
    void *expansion = *reinterpret_cast<void **>(raw + 0xC0);
    uint32_t speed = 0;
    uint32_t linkStatus = 0xffffffffU;
    if (expansion != nullptr) {
        uint8_t *ed = reinterpret_cast<uint8_t *>(expansion);
        speed = *reinterpret_cast<uint32_t *>(ed + 0x30);
        linkStatus = *reinterpret_cast<uint32_t *>(ed + 0x38);
    }

    XYLog("DEBUG %s fNetIf=%p expansion=%p speed=0x%x linkStatus=0x%x bsdIf=%p\n",
          tag, interface, expansion, speed, linkStatus, interface->getBSDInterface());
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
    if (sRT.txCbCnt <= 3 && count > 0)
        XYLog("skywalkTxAction: count=%u consumed=%u delivered=%u (total tx=%u)\n",
              count, consumed, delivered, sRT.txPktSent);
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

void AirportItlwm::releaseAll()
{
    XYLog("DEBUG %s [1] logStream=%p(rc=%d) logPipe=%p dataPath=%p snapshots=%p faultReporter=%p\n",
          __FUNCTION__, driverLogStream, driverLogStream ? driverLogStream->getRetainCount() : -1,
          driverLogPipe, driverDataPathPipe, driverSnapshotsPipe, driverFaultReporter);
#if __IO80211_TARGET >= __MAC_26_0
    // Wake any helper currently blocked in waitAssocTarget so the
    // user client thread can return kIOReturnAborted instead of
    // dying with the command gate. Safe to call before _fCommandGate
    // is released because cancelPendingAssocTarget is a no-op when
    // getCommandGate() returns NULL. The gated cancel action also
    // zeros ic_psk and drops IEEE80211_F_PSK / ic_external_pmk_owner
    // so a PLTI DeliverPMK racing teardown cannot reinstall a stale
    // PMK after the controller is on its way down.
    cancelPendingAssocTarget("releaseAll");
#endif

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
    teardownLinkStatePublishSource();
#if __IO80211_TARGET >= __MAC_26_0
    skywalkTxDrainCompletionPackets(this);
    skywalkRxDrainPendingPackets(this);
    if (_fWorkloop && fMultiCastQueue && fMultiCastQueue->getWorkLoop() == _fWorkloop) {
        fMultiCastQueue->disable();
        _fWorkloop->removeEventSource(fMultiCastQueue);
    }
    if (_fWorkloop && fTxCompQueue && fTxCompQueue->getWorkLoop() == _fWorkloop) {
        fTxCompQueue->disable();
        _fWorkloop->removeEventSource(fTxCompQueue);
    }
    if (_fWorkloop && fTxQueue && fTxQueue->getWorkLoop() == _fWorkloop) {
        fTxQueue->disable();
        _fWorkloop->removeEventSource(fTxQueue);
    }
    if (_fWorkloop && fRxQueue && fRxQueue->getWorkLoop() == _fWorkloop) {
        fRxQueue->disable();
        _fWorkloop->removeEventSource(fRxQueue);
    }
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

    OSSafeReleaseNULL(driverLogStream);
    OSSafeReleaseNULL(driverLogPipe);
    OSSafeReleaseNULL(driverDataPathPipe);
    OSSafeReleaseNULL(driverSnapshotsPipe);
    OSSafeReleaseNULL(driverFaultReporter);
    if (io80211FaultReporter) {
        io80211FaultReporter->release();
        io80211FaultReporter = NULL;
    }
    /*
     * Tear down the host APSTA owner before fHalService.
     * AirportItlwmAPSTAOwner::teardown stops the lower AP
     * backend (if it was ever started) through fHalService and
     * clears the station table; running teardown after fHalService
     * has been released would leave the lower-stop call without a
     * HAL service and skip the contractual stopAPMode invocation.
     *
     * Unbind the producer-bridge consumer before releasing the
     * owner so the producer reads NULL cb/arg fields after the
     * owner storage is reclaimed. The unregister API requires the
     * cookie passed at register time and is a no-op when no
     * consumer is currently bound, so calling it unconditionally
     * is safe in APSTA station-event opt-out builds. Default Tahoe
     * builds keep IEEE80211_STA_ONLY and do not bind this bridge.
     */
    if (fAPSTAOwner != NULL) {
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
    // CR-231 BRANCH 5: log every event posted through the centralized
    // workloop dispatcher. Captures msg-code + data-len + ic_state at
    // post time so Stage 2 evidence can correlate which events the
    // framework saw between LINK_UP and the airportd `isAssociated=0`
    // observation.
    {
        struct ieee80211com *ic = (that->fHalService != nullptr)
            ? that->fHalService->get80211Controller() : nullptr;
        unsigned int dataLen = (unsigned int)(uintptr_t)arg2;
        CR231_LOG("DEBUG CR231_POST_GATED msg=0x%x len=0x%x ic_state=%d\n",
                  msg, dataLen, ic ? (int)ic->ic_state : -1);
    }
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
    static UInt32 reassocEventStatus[2];
    static UInt32 reassocFailureStatus;
    switch (msgCode) {
        case IEEE80211_EVT_COUNTRY_CODE_UPDATE:
            RT_SET(1);
            apple80211Msg = APPLE80211_M_COUNTRY_CODE_CHANGED;
            break;
        case IEEE80211_EVT_STA_ASSOC_DONE:
            RT_SET(2);
#if __IO80211_TARGET >= __MAC_26_0
            XYLog("DEBUG %s Tahoe: skip legacy zero-payload ASSOC_DONE; WCL JoinDone owns assoc completion\n",
                  __FUNCTION__);
            return;
#else
            apple80211Msg = APPLE80211_M_ASSOC_DONE;
            break;
#endif
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
    fpNetStats = NULL;
#if __IO80211_TARGET >= __MAC_26_0
    memset(&tahoeLegacyNetStats, 0, sizeof(tahoeLegacyNetStats));
    memset(&tahoeCurrentApAddress, 0, sizeof(tahoeCurrentApAddress));
    tahoeCurrentApKnown = false;
    tahoeCurrentApValid = false;
#endif
    tahoeRequestedPowerState = kWiFiPowerOff;
    tahoeBootstrapPowerPending = false;
    tahoeBootstrapPowerWindowOpen = true;
    driverLogStream = nullptr;
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
    if (fRxPendingLock == NULL || fTxCompletionPendingLock == NULL)
        ret = false;
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
#if __IO80211_TARGET >= __MAC_26_0
    // Initialize the project-owned PLTI PMK producer trigger state.
    // fAssocTarget is published lazily on the PSK association-start
    // edge; until then generation==0 means "no pending request" and
    // the helper's WaitAssociationTarget call sleeps under the
    // controller command gate.
    memset(&fAssocTarget, 0, sizeof(fAssocTarget));
    fAssocGenCounter     = 0;
    fAssocTargetCanceled = false;
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
    XYLog("DEBUG %s [STEP 5] HAL initWithController + attach\n", __FUNCTION__);
    fHalService->initWithController(this, _fWorkloop, _fCommandGate);
    fHalService->get80211Controller()->ic_event_handler = eventHandler;
#if __IO80211_TARGET >= __MAC_26_0
    {
        struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
        memset(&tahoeLegacyNetStats, 0, sizeof(tahoeLegacyNetStats));
        fpNetStats = &tahoeLegacyNetStats;
        ifp->netStat = fpNetStats;
        XYLog("DEBUG %s Tahoe legacy netStat fallback ifp=%p netStat=%p\n",
              __FUNCTION__, ifp, fpNetStats);
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

    _fLinkStatePublishLock = IOSimpleLockAlloc();
    _fLinkStatePublishPendingValid = false;
    _fLinkStatePublishSource = IOInterruptEventSource::interruptEventSource(
        this, (IOInterruptEventSource::Action)publishLinkStateInterruptAction);
    if (_fLinkStatePublishLock == NULL || _fLinkStatePublishSource == NULL) {
        XYLog("DEBUG %s [STEP 7] FAIL: link-state publish source alloc\n", __FUNCTION__);
        super::stop(pciNub);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    _fWorkloop->addEventSource(_fLinkStatePublishSource);
    _fLinkStatePublishSource->enable();

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

        // CR-216: unredact controller-level pointers via split-halves
        // (`0x%x_%x`) since CR-215 evidence (12:32 boot) showed the
        // earlier `0x%llx` form was still privacy-redacted by os_log.
        XYLog("itlwm: POOLTRACE[STEP8b] BEGIN owner=0x%x_%x opts=0x%x_%x "
              "pktCount=%u bufCount=%u bufSize=%u maxBPP=%u "
              "memSegSz=%u poolFlags=0x%x\n",
              AirportItlwmIO80211PacketPool::ptrHi32(fNetIf),
              AirportItlwmIO80211PacketPool::ptrLo32(fNetIf),
              AirportItlwmIO80211PacketPool::ptrHi32(&poolOpts),
              AirportItlwmIO80211PacketPool::ptrLo32(&poolOpts),
              poolOpts.packetCount,
              poolOpts.bufferCount, poolOpts.bufferSize,
              poolOpts.maxBuffersPerPacket, poolOpts.memorySegmentSize,
              poolOpts.poolFlags);
        sRT.startStep = 811;
        fTxPool = AirportItlwmIO80211PacketPool::withName(
            "AirportItlwm-TX", fNetIf, &poolOpts);
        XYLog("itlwm: POOLTRACE[STEP8b] AFTER_TX tx=0x%x_%x rx=0x%x_%x\n",
              AirportItlwmIO80211PacketPool::ptrHi32(fTxPool),
              AirportItlwmIO80211PacketPool::ptrLo32(fTxPool),
              AirportItlwmIO80211PacketPool::ptrHi32(fRxPool),
              AirportItlwmIO80211PacketPool::ptrLo32(fRxPool));
        sRT.startStep = 812;
        fRxPool = AirportItlwmIO80211PacketPool::withName(
            "AirportItlwm-RX", fNetIf, &poolOpts);
        XYLog("itlwm: POOLTRACE[STEP8b] AFTER_RX tx=0x%x_%x rx=0x%x_%x\n",
              AirportItlwmIO80211PacketPool::ptrHi32(fTxPool),
              AirportItlwmIO80211PacketPool::ptrLo32(fTxPool),
              AirportItlwmIO80211PacketPool::ptrHi32(fRxPool),
              AirportItlwmIO80211PacketPool::ptrLo32(fRxPool));
        sRT.startStep = 813;
        XYLog("DEBUG %s [STEP 8b] pools: TX=%p RX=%p\n", __FUNCTION__, fTxPool, fRxPool);
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
            super::stop(provider);
            releaseAll();
            DISARM_PANIC_TIMER();
            return false;
        }
        sRT.startStep = 815;
        XYLog("itlwm: POOLTRACE[STEP8b] FINAL branch=BOTH_OK "
              "tx=0x%x_%x rx=0x%x_%x handoff=STEP8c\n",
              AirportItlwmIO80211PacketPool::ptrHi32(fTxPool),
              AirportItlwmIO80211PacketPool::ptrLo32(fTxPool),
              AirportItlwmIO80211PacketPool::ptrHi32(fRxPool),
              AirportItlwmIO80211PacketPool::ptrLo32(fRxPool));
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
    XYLog("DEBUG %s [STEP 8c] queues: TX=%p TXC=%p RX=%p MC=%p\n",
          __FUNCTION__, fTxQueue, fTxCompQueue, fRxQueue, fMultiCastQueue);
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
        super::stop(provider);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    XYLog("itlwm: POOLTRACE[STEP8c] boundary=QUEUES_OK "
          "poolResult=BOTH_OK TX=0x%llx TXC=0x%llx RX=0x%llx MC=0x%llx\n",
          (unsigned long long)(uintptr_t)fTxQueue,
          (unsigned long long)(uintptr_t)fTxCompQueue,
          (unsigned long long)(uintptr_t)fRxQueue,
          (unsigned long long)(uintptr_t)fMultiCastQueue);
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
    XYLog("DEBUG %s [STEP 8c-wl] queue addEventSource TX=0x%x TXC=0x%x "
          "RX=0x%x MC=0x%x TXwl=%p TXCwl=%p RXwl=%p MCwl=%p\n",
          __FUNCTION__, txQueueWorkloopRet, txCompQueueWorkloopRet,
          rxQueueWorkloopRet, multicastQueueWorkloopRet,
          fTxQueue->getWorkLoop(), fTxCompQueue->getWorkLoop(),
          fRxQueue->getWorkLoop(), fMultiCastQueue->getWorkLoop());
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
        super::stop(provider);
        releaseAll();
        DISARM_PANIC_TIMER();
        return false;
    }
    XYLog("itlwm: POOLTRACE[STEP8c-wl] boundary=WORKLOOPS_OK "
          "poolResult=BOTH_OK handoff=STEP8d\n");

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
#if __IO80211_TARGET >= __MAC_26_0
        // Reference AppleBCMWLANSkywalkInterface enters the Wi-Fi infra shim,
        // which then delegates to the same underlying Ethernet registration.
        IOReturn regRet = static_cast<IO80211InfraInterface *>(fNetIf)
            ->registerInfraEthernetInterface(
                (IOSkywalkEthernetInterface::RegistrationInfo *)&registInfo,
                queues, 2, fTxPool, fRxPool);
        XYLog("DEBUG %s [STEP 8d] registerInfraEthernetInterface=0x%x\n",
              __FUNCTION__, regRet);
#else
        IOReturn regRet = fNetIf->registerEthernetInterface(
            (const IOSkywalkEthernetInterface::RegistrationInfo *)&registInfo,
            queues, 2, fTxPool, fRxPool, 0);
        XYLog("DEBUG %s [STEP 8d] registerEthernetInterface=0x%x\n", __FUNCTION__, regRet);
#endif
        if (regRet != kIOReturnSuccess) {
            XYLog("DEBUG %s [STEP 8d] FAIL: Skywalk registration ret=0x%x\n", __FUNCTION__, regRet);
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
        void *regObj90   = *(void **)(raw + 0x90);
        sRT.nexusProvPtr  = (uint64_t)(uintptr_t)nexusProv;
        sRT.nexusArenaPtr = (uint64_t)(uintptr_t)nexusArena;
        sRT.asyncSentinel = (uint64_t)(uintptr_t)asyncSent;
        XYLog("DEBUG %s [POST-REG] nexusProvider=%p nexusArena=%p "
              "asyncSentinel=%p queueSet=%p\n",
              __FUNCTION__, nexusProv, nexusArena, asyncSent, regObj90);
        if (!nexusProv) {
            XYLog("DEBUG %s [POST-REG] WARNING: nexusProvider still NULL "
                  "after Skywalk registration — BSDClient nexus "
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

    /* HAL-independent same-carrier smoke marker: prove the
     * project-owned os_log carrier is reachable on this VM
     * regardless of which iwx / iwn / iwm HAL handles the
     * underlying PCI device. The marker fires exactly once
     * per successful AirportItlwm::start so the next runtime
     * cycle can confirm carrier visibility via
     *   sudo log show --info --debug --predicate
     *     'subsystem == "com.zxystd.AirportItlwm" AND
     *      category == "iwx.auth_ack"'
     * before relying on the auth-ACK Case A-F classification
     * built on the per-HAL leaf probes. iwx_auth_diag_init()
     * is idempotent; the same os_log handle is reused by any
     * subsequent per-HAL attach (iwx_attach, iwn_attach). */
    iwx_auth_diag_init();
    IWX_AUTH_DIAG("smoke_marker AirportItlwm_start OK\n");

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
    // Drain the off-gate publication source before fNetIf is detached/released
    // below, so the deferred action cannot dereference fNetIf after its lifetime
    // ends. releaseAll() calls this again (idempotent).
    teardownLinkStatePublishSource();
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
#if __IO80211_TARGET >= __MAC_26_0
    skywalkTxDrainCompletionPackets(this);
    skywalkRxDrainPendingPackets(this);
#endif
    if (_fWorkloop && fMultiCastQueue && fMultiCastQueue->getWorkLoop() == _fWorkloop) {
        XYLog("DEBUG %s [6] removing fMultiCastQueue=%p from _fWorkloop=%p\n",
              __FUNCTION__, fMultiCastQueue, _fWorkloop);
        fMultiCastQueue->disable();
        _fWorkloop->removeEventSource(fMultiCastQueue);
    }
    if (_fWorkloop && fTxCompQueue && fTxCompQueue->getWorkLoop() == _fWorkloop) {
        XYLog("DEBUG %s [6] removing fTxCompQueue=%p from _fWorkloop=%p\n",
              __FUNCTION__, fTxCompQueue, _fWorkloop);
        fTxCompQueue->disable();
        _fWorkloop->removeEventSource(fTxCompQueue);
    }
    if (_fWorkloop && fTxQueue && fTxQueue->getWorkLoop() == _fWorkloop) {
        XYLog("DEBUG %s [6] removing fTxQueue=%p from _fWorkloop=%p\n",
              __FUNCTION__, fTxQueue, _fWorkloop);
        fTxQueue->disable();
        _fWorkloop->removeEventSource(fTxQueue);
    }
    if (_fWorkloop && fRxQueue && fRxQueue->getWorkLoop() == _fWorkloop) {
        XYLog("DEBUG %s [6] removing fRxQueue=%p from _fWorkloop=%p\n",
              __FUNCTION__, fRxQueue, _fWorkloop);
        fRxQueue->disable();
        _fWorkloop->removeEventSource(fRxQueue);
    }
    OSSafeReleaseNULL(fMultiCastQueue);
    OSSafeReleaseNULL(fTxCompQueue);
    OSSafeReleaseNULL(fTxQueue);  sRT.fTxQueuePtr = 0;
    OSSafeReleaseNULL(fRxQueue);  sRT.fRxQueuePtr = 0;
    OSSafeReleaseNULL(fTxPool);   sRT.fTxPoolPtr = 0;
    OSSafeReleaseNULL(fRxPool);   sRT.fRxPoolPtr = 0;
    fSkywalkTxQueueDepth = 0;
    fSkywalkRxQueueCapacity = 0;
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

#if __IO80211_TARGET >= __MAC_26_0
bool AirportItlwm::syncTahoeCurrentApAddress(bool forceClear, bool allowInitialClear)
{
    AirportItlwmSkywalkInterface *skyIf =
        OSDynamicCast(AirportItlwmSkywalkInterface, fNetIf);
    if (skyIf == nullptr)
        return false;

    struct ieee80211com *ic = fHalService != nullptr
        ? fHalService->get80211Controller()
        : nullptr;
    struct ether_addr targetAp;
    bool targetValid = false;
    if (!forceClear && ic != nullptr &&
        ic->ic_state == IEEE80211_S_RUN && ic->ic_bss != nullptr) {
        IEEE80211_ADDR_COPY(targetAp.octet, ic->ic_bss->ni_bssid);
        targetValid = true;
    }

    if (targetValid) {
        if (tahoeCurrentApKnown && tahoeCurrentApValid &&
            IEEE80211_ADDR_EQ(tahoeCurrentApAddress.octet, targetAp.octet)) {
            return false;
        }

        XYLog("DEBUG %s addr=%s ic_state=%d ic_bss=%p forceClear=%d\n",
              __FUNCTION__, ether_sprintf(targetAp.octet),
              ic ? ic->ic_state : -1, ic ? ic->ic_bss : nullptr, forceClear);
        skyIf->setCurrentApAddress(&targetAp);
        IEEE80211_ADDR_COPY(tahoeCurrentApAddress.octet, targetAp.octet);
        tahoeCurrentApKnown = true;
        tahoeCurrentApValid = true;
        return true;
    }

    if (tahoeCurrentApKnown && !tahoeCurrentApValid)
        return false;
    if (!allowInitialClear && !tahoeCurrentApKnown)
        return false;

    XYLog("DEBUG %s addr=(null) ic_state=%d ic_bss=%p forceClear=%d\n",
          __FUNCTION__, ic ? ic->ic_state : -1, ic ? ic->ic_bss : nullptr,
          forceClear);
    skyIf->setCurrentApAddress(nullptr);
    memset(&tahoeCurrentApAddress, 0, sizeof(tahoeCurrentApAddress));
    tahoeCurrentApKnown = true;
    tahoeCurrentApValid = false;
    return true;
}
#endif

bool AirportItlwm::
setLinkStatus(UInt32 status, const IONetworkMedium * activeMedium, UInt64 speed, OSData * data)
{
    RT_SET(6);
    struct _ifnet *ifq = &fHalService->get80211Controller()->ic_ac.ac_if;
    XYLog("DEBUG %s status=0x%x (prev=0x%x) active=%d speed=%llu if_flags=0x%x ic_state=%d power_state=%u\n",
          __FUNCTION__, status, currentStatus, (status & kIONetworkLinkActive) != 0, speed,
          ifq->if_flags, fHalService->get80211Controller()->ic_state, power_state);
    if (status == currentStatus) {
#if __IO80211_TARGET >= __MAC_26_0
        if ((status & kIONetworkLinkActive) != 0)
            syncTahoeCurrentApAddress(false, false);
#endif
        return true;
    }
    bool ret = super::setLinkStatus(status, activeMedium, speed, data);
    currentStatus = status;
    if (fNetIf) {
        if (status & kIONetworkLinkActive) {
#if defined(__PRIVATE_SPI__) && __IO80211_TARGET < __MAC_26_0
            bsdInterface->startOutputThread();
#endif
            queueOffGateLinkStatePublish(this, kIO80211NetworkLinkUp, 0);
        } else if (!(status & kIONetworkLinkNoNetworkChange)) {
#if defined(__PRIVATE_SPI__) && __IO80211_TARGET < __MAC_26_0
            bsdInterface->stopOutputThread();
            bsdInterface->flushOutputQueue();
#endif
            ifq_flush(&ifq->if_snd);
            mq_purge(&fHalService->get80211Controller()->ic_mgtq);
            queueOffGateLinkStatePublish(this, kIO80211NetworkLinkDown, fHalService->get80211Controller()->ic_deauth_reason);
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
    XYLog("DEBUG %s linkState=%u rawCode=%u power_state=%u\n",
          __FUNCTION__, static_cast<unsigned int>(linkState),
          rawCode, that->power_state);
#if __IO80211_TARGET >= __MAC_26_0
    /*
     * Off-gate publication precondition guard, evaluated BEFORE any publication
     * side effect. The inherited IO80211 publication path reaches
     * IO80211Glue::sendIOUCToWcl, which requires the IO80211 work-queue serial
     * owner to be on its own thread (onThread() == true) with the work-loop gate
     * released (inGate() == false); otherwise it takes the null-owner panic
     * branch. If the off-gate route did not reach this point with that
     * precondition satisfied, perform NO link-state publication at all (no WCL
     * link-up indication, reportLinkStatus, setLinkState, setRunningState,
     * connect-complete, or postMessage) and return kIOReturnNotReady. This is a
     * precondition guard, not retry/replay/masking/forced-success: when the
     * precondition fails the link is simply not published (the negative branch).
     */
    if (that->fNetIf == NULL) {
        XYLog("DEBUG %s skipped: null fNetIf\n", __FUNCTION__);
        return kIOReturnNotReady;
    }
    {
        IOWorkLoop *publishWorkLoop = that->getWorkLoop();
        const int onThreadPred =
            publishWorkLoop ? (publishWorkLoop->onThread() ? 1 : 0) : -1;
        const int inGatePred =
            publishWorkLoop ? (publishWorkLoop->inGate() ? 1 : 0) : -1;
        const int onDispatchQueuePred =
            ((IO80211InfraInterface *)that->fNetIf)->onDispatchQueue() ? 1 : 0;
        XYLog("DEBUG %s linkstate-publish-predicate onThread=%d inGate=%d onDispatchQueue=%d\n",
              __FUNCTION__, onThreadPred, inGatePred, onDispatchQueuePred);
        if (!(onThreadPred == 1 && inGatePred == 0)) {
            XYLog("DEBUG %s off-gate precondition not met (onThread=%d inGate=%d); skipping all link-state publication\n",
                  __FUNCTION__, onThreadPred, inGatePred);
            return kIOReturnNotReady;
        }
    }
    const unsigned int setLinkCode =
        (linkState == kIO80211NetworkLinkUp) ? 1U : rawCode;
    that->syncTahoeCurrentApAddress(
        linkState != kIO80211NetworkLinkUp, true);
    if (linkState == kIO80211NetworkLinkUp) {
        postTahoeWclLinkUpInd(that, rawCode);
        logTahoeSkywalkLinkCarrier("DEBUG setLinkStateGated pre-reportLinkStatus", that->fNetIf);
        that->fNetIf->reportLinkStatus(3, 0x80);
        logTahoeSkywalkLinkCarrier("DEBUG setLinkStateGated post-reportLinkStatus", that->fNetIf);
    }
    logTahoeSkywalkLinkCarrier("DEBUG setLinkStateGated pre-setLinkState", that->fNetIf);
    XYLog("DEBUG %s Tahoe: calling IO80211InfraInterface::setLinkState fNetIf=%p\n", __FUNCTION__, that->fNetIf);
    // The off-gate precondition (onThread==1, inGate==0) was guarded at the top
    // of this publication path; reaching here means it holds, so the inherited
    // publication is safe to invoke.
    IOReturn ret = ((IO80211InfraInterface *)that->fNetIf)->setLinkState(linkState, setLinkCode, false, 0, 0);
    logTahoeSkywalkLinkCarrier("DEBUG setLinkStateGated post-setLinkState", that->fNetIf);
#else
    IOReturn ret = that->fNetIf->setLinkState(linkState, rawCode);
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
    // CR-234 BRANCH D: PRE/POST setRunningState - probes the
    // IO80211SkywalkInterface variant byte at (this+0x120)+0x88 (per
    // decomp at ffffff8002277284 where setRunningState is the writer
    // and ffffff800227855a where IO80211SkywalkInterface::getAssocState
    // reads the same byte). Compares against the InfraInterface
    // variant byte at (this+0x128)+0x180 to disambiguate which reader
    // path airportd uses.
    {
        const void *thisPtr = that->fNetIf;
        if (thisPtr != nullptr) {
            const char *base = reinterpret_cast<const char *>(thisPtr);
            const void *i120 = *reinterpret_cast<const void * const *>(base + 0x120);
            const void *i128 = *reinterpret_cast<const void * const *>(base + 0x128);
            uint8_t b120_88 = (i120 != nullptr)
                ? *(reinterpret_cast<const uint8_t *>(i120) + 0x88) : 0xff;
            uint8_t b128_180 = (i128 != nullptr)
                ? *(reinterpret_cast<const uint8_t *>(i128) + 0x180) : 0xff;
            CR234_LOG("DEBUG CR234_INNER setRunningState_PRE thisPtr=%p i120=%p i128=%p "
                      "i120_88=0x%02x i128_180=0x%02x linkUp=%d\n",
                      thisPtr, i120, i128,
                      (unsigned)b120_88, (unsigned)b128_180,
                      linkState == kIO80211NetworkLinkUp ? 1 : 0);
        }
    }
    that->fNetIf->setRunningState(linkState == kIO80211NetworkLinkUp);
    {
        const void *thisPtr = that->fNetIf;
        if (thisPtr != nullptr) {
            const char *base = reinterpret_cast<const char *>(thisPtr);
            const void *i120 = *reinterpret_cast<const void * const *>(base + 0x120);
            const void *i128 = *reinterpret_cast<const void * const *>(base + 0x128);
            uint8_t b120_88 = (i120 != nullptr)
                ? *(reinterpret_cast<const uint8_t *>(i120) + 0x88) : 0xff;
            uint8_t b128_180 = (i128 != nullptr)
                ? *(reinterpret_cast<const uint8_t *>(i128) + 0x180) : 0xff;
            CR234_LOG("DEBUG CR234_INNER setRunningState_POST thisPtr=%p i120=%p i128=%p "
                      "i120_88=0x%02x i128_180=0x%02x linkUp=%d\n",
                      thisPtr, i120, i128,
                      (unsigned)b120_88, (unsigned)b128_180,
                      linkState == kIO80211NetworkLinkUp ? 1 : 0);
        }
    }
#if __IO80211_TARGET >= __MAC_26_0
    if (linkState == kIO80211NetworkLinkUp)
        postTahoeWclConnectCompleteEvent(that);
#endif
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
     * Keep this branch limited to the link-down zero-length
     * APPLE80211_M_SSID_CHANGED carrier. The recovered reference does not
     * contain a payload-bearing writer for that event code, and Tahoe
     * userspace does not length-reject the zero-length publication.
     * APPLE80211_M_BSSID_CHANGED has a recovered Apple writer on the
     * WCL/IOUC side (selector 0x1b1) that produces a populated 24-byte
     * BSSID-changed compact carrier with the BSSID at offset 0x00 and
     * the reason at offset 0x14; see
     * struct apple80211_bssid_changed_event_data. Tahoe userspace
     * length-checks this carrier (prior zero-length publication
     * produced an `expected=24 actual=0` CoreWiFi rejection), so the
     * Tahoe branch must not republish APPLE80211_M_BSSID_CHANGED with
     * a NULL/0 payload. The populated 24-byte publication is owned by
     * the Tahoe Skywalk override
     * `AirportItlwmSkywalkInterface::setCurrentApAddress`, which is
     * the natural Apple framework producer of BSSID transitions and
     * which publishes the populated payload through the standard
     * IO80211Controller::postMessage / IO80211Glue routing on every
     * call with a non-null, non-zero address, honouring the recovered
     * zero-BSSID rejection and same-BSS reason-1 suppression gates.
     * This Tahoe branch therefore does not emit APPLE80211_M_BSSID_CHANGED
     * itself; the legacy zero-length BSSID-changed notify remains in
     * the pre-Tahoe branch below as it predates the Tahoe userspace
     * length check.
     */
    if (linkState != kIO80211NetworkLinkUp) {
        that->postMessage(that->fNetIf, APPLE80211_M_SSID_CHANGED, NULL, 0, true);
    }
#else
    that->postMessage(that->fNetIf, APPLE80211_M_LINK_CHANGED, NULL, 0, true);
    that->postMessage(that->fNetIf, APPLE80211_M_BSSID_CHANGED, NULL, 0, true);
    that->postMessage(that->fNetIf, APPLE80211_M_SSID_CHANGED, NULL, 0, true);
#endif
    if (linkState != kIO80211NetworkLinkUp) {
        logTahoeSkywalkLinkCarrier("DEBUG setLinkStateGated pre-reportLinkStatus", that->fNetIf);
        that->fNetIf->reportLinkStatus(1, 0);
        logTahoeSkywalkLinkCarrier("DEBUG setLinkStateGated post-reportLinkStatus", that->fNetIf);
    }
#if __IO80211_TARGET < __MAC_26_0
    if (that->bsdInterface) {
        XYLog("DEBUG %s calling bsdInterface->setLinkState bsdInterface=%p\n", __FUNCTION__, that->bsdInterface);
        that->bsdInterface->setLinkState(linkState);
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
    bool diagEapol = false;
    if (airportItlwmRegDiagPacketProbeEnabled())
        diagEapol = airportItlwmRegDiagMbufIsEapol(m, &diagLength);

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
    XYLog("DEBUG %s code=%d\n", __FUNCTION__, code);
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
    if (isTahoeCurrentLinkProbeSelector(cmd)) {
        XYLog("DEBUG %s probe cmd=%s(%lu) isSet=%d interface=%p data=%p ic_state=%d interrupt=%d\n",
              __FUNCTION__, convertApple80211IOCTLToString((unsigned int)cmd), cmd,
              isSet ? 1 : 0, interface, data,
              fHalService ? fHalService->get80211Controller()->ic_state : -1,
              ml_at_interrupt_context() ? 1 : 0);
    }
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
        if (isTahoeCurrentLinkProbeSelector(cmd)) {
            XYLog("DEBUG %s probe-route cmd=%s(%lu) ret=0x%x ic_state=%d\n",
                  __FUNCTION__, convertApple80211IOCTLToString((unsigned int)cmd),
                  cmd, ret,
                  fHalService ? fHalService->get80211Controller()->ic_state : -1);
        }
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
#if __IO80211_TARGET >= __MAC_26_0
    if (fTxCompQueue)
        fTxCompQueue->enable();
    if (fRxQueue)
        fRxQueue->enable();
    if (fTxQueue)
        fTxQueue->enable();
    if (fTxCompQueue || fRxQueue || fTxQueue || fMultiCastQueue) {
        XYLog("DEBUG %s skywalk queues enabled TX=%p TXenabled=%d "
              "TXC=%p TXCenabled=%d RX=%p RXenabled=%d MC=%p MCenabled=%d\n",
              __FUNCTION__,
              fTxQueue,
              fTxQueue && fTxQueue->isEnabled() ? 1 : 0,
              fTxCompQueue,
              fTxCompQueue && fTxCompQueue->isEnabled() ? 1 : 0,
              fRxQueue,
              fRxQueue && fRxQueue->isEnabled() ? 1 : 0,
              fMultiCastQueue,
              fMultiCastQueue && fMultiCastQueue->isEnabled() ? 1 : 0);
    }
#endif
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

    if (fTxCompQueue || fRxQueue || fTxQueue || fMultiCastQueue) {
        XYLog("DEBUG %s skywalk queues disabled TX=%p TXenabled=%d "
              "TXC=%p TXCenabled=%d RX=%p RXenabled=%d MC=%p MCenabled=%d "
              "TXpending=%u RXpending=%u\n",
              __FUNCTION__,
              fTxQueue,
              fTxQueue && fTxQueue->isEnabled() ? 1 : 0,
              fTxCompQueue,
              fTxCompQueue && fTxCompQueue->isEnabled() ? 1 : 0,
              fRxQueue,
              fRxQueue && fRxQueue->isEnabled() ? 1 : 0,
              fMultiCastQueue,
              fMultiCastQueue && fMultiCastQueue->isEnabled() ? 1 : 0,
              fTxCompletionPendingCount,
              fRxPendingCount);
    }
#endif
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
    if (!IOUserClient::initWithTask(owningTask, securityID, type,
                                    properties)) {
        return false;
    }
    fOwningTask = owningTask;
    fProvider = nullptr;
    XYLog("CR239 AirportItlwmUserClient::initWithTask type=0x%x task=%p\n",
          (unsigned)type, owningTask);
    return true;
}

bool AirportItlwmUserClient::
start(IOService *provider)
{
    if (!IOUserClient::start(provider))
        return false;
    fProvider = OSDynamicCast(AirportItlwm, provider);
    if (fProvider == nullptr) {
        XYLog("CR239 AirportItlwmUserClient::start provider is not AirportItlwm\n");
        return false;
    }
    XYLog("CR239 AirportItlwmUserClient::start provider=%p\n", provider);
    return true;
}

void AirportItlwmUserClient::
stop(IOService *provider)
{
    XYLog("CR239 AirportItlwmUserClient::stop\n");
    fProvider = nullptr;
    IOUserClient::stop(provider);
}

IOReturn AirportItlwmUserClient::
clientClose(void)
{
    XYLog("CR239 AirportItlwmUserClient::clientClose\n");
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
    if (target == nullptr || target->fProvider == nullptr)
        return kIOReturnNotReady;
    if (args == nullptr || args->structureInput == nullptr)
        return kIOReturnBadArgument;
    const uint64_t generation_echo = (uint64_t)args->scalarInput[0];
    return target->fProvider->deliverExternalPMK(
        (const struct apple80211_key *)args->structureInput,
        generation_echo);
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
    if (target == nullptr || target->fProvider == nullptr)
        return kIOReturnNotReady;
    if (args == nullptr || args->structureOutput == nullptr)
        return kIOReturnBadArgument;
    const uint64_t last_acked = (uint64_t)args->scalarInput[0];
    AirportItlwmAssociationTarget snap;
    memset(&snap, 0, sizeof(snap));
    IOReturn rc = target->fProvider->waitAssocTarget(last_acked, &snap);
    if (rc != kIOReturnSuccess)
        return rc;
    memcpy(args->structureOutput, &snap, sizeof(snap));
    return kIOReturnSuccess;
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
        return kIOReturnNotPrivileged;
    }
    XYLog("CR240 AirportItlwm::newUserClient(type=0x%x) AUTHORIZED -- "
          "creating AirportItlwmUserClient\n",
          (unsigned)type);
    AirportItlwmUserClient *client = OSTypeAlloc(AirportItlwmUserClient);
    if (client == nullptr) {
        return kIOReturnNoMemory;
    }
    if (!client->initWithTask(owningTask, securityID, type, properties)) {
        client->release();
        return kIOReturnError;
    }
    if (!client->attach(this)) {
        client->release();
        return kIOReturnError;
    }
    if (!client->start(this)) {
        client->detach(this);
        client->release();
        return kIOReturnError;
    }
    *handler = client;
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
//                 !fAssocTargetCanceled);
//       on mismatch -> kIOReturnNotPermitted, no ic state change;
//       on match    -> memcpy 32 bytes into ic->ic_psk;
//                      set IEEE80211_F_PSK;
//                      clear ic_external_pmk_owner=0 so the first
//                      4-way M1 routes through the local PAE
//                      (owner=local), matching the existing
//                      installExternalPmkLocked sink semantic;
//                      ieee80211_ioctl_setwpaparms with
//                      protos=WPA1|WPA2 + akms=PSK|SHA256_PSK.
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
//   same atomic critical section.
// =====================================================================

namespace {

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

struct AirportItlwmDeliverPmkArgs {
    AirportItlwm *self;
    const struct apple80211_key *key;
    uint64_t generation_echo;
    IOReturn rc;
    int wparc;
    uint32_t ic_flags_after;
    uint32_t ic_rsnprotos_after;
    uint32_t ic_rsnakms_after;
};

static IOReturn
airportItlwmPublishAssocAction(OSObject * /*owner*/, void *arg0,
                               void * /*arg1*/, void * /*arg2*/,
                               void * /*arg3*/)
{
    AirportItlwmPublishAssocArgs *a = (AirportItlwmPublishAssocArgs *)arg0;
    AirportItlwm *s = a->self;
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
    return kIOReturnSuccess;
}

static IOReturn
airportItlwmWaitAssocAction(OSObject * /*owner*/, void *arg0,
                            void * /*arg1*/, void * /*arg2*/,
                            void * /*arg3*/)
{
    AirportItlwmWaitAssocArgs *a = (AirportItlwmWaitAssocArgs *)arg0;
    AirportItlwm *s = a->self;
    while (!s->fAssocTargetCanceled &&
           (s->fAssocTarget.generation == 0 ||
            s->fAssocTarget.generation == a->last_acked)) {
        IOReturn sr = s->getCommandGate()->commandSleep(
            &s->fAssocTarget, THREAD_ABORTSAFE);
        if (sr != THREAD_AWAKENED && sr != THREAD_TIMED_OUT) {
            a->rc = kIOReturnAborted;
            return kIOReturnSuccess;
        }
    }
    if (s->fAssocTargetCanceled) {
        a->rc = kIOReturnAborted;
        return kIOReturnSuccess;
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
    // rejects. Used by clearExternalPmkEligibilityLocked and
    // releaseAll where wiping ic_psk is the intended lifecycle
    // semantic.
    AirportItlwm *s = (AirportItlwm *)arg0;
    s->fAssocTargetCanceled = true;
    memset(&s->fAssocTarget, 0, sizeof(s->fAssocTarget));
    if (s->fHalService != nullptr) {
        struct ieee80211com *ic = s->fHalService->get80211Controller();
        if (ic != nullptr) {
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
    a->wparc            = 0;
    a->ic_flags_after   = 0;
    a->ic_rsnprotos_after = 0;
    a->ic_rsnakms_after = 0;

    if (a->generation_echo == 0 ||
        s->fAssocTargetCanceled ||
        s->fAssocTarget.generation == 0 ||
        a->generation_echo != s->fAssocTarget.generation) {
        a->rc = kIOReturnNotPermitted;
        return kIOReturnSuccess;
    }

    struct ieee80211com *ic = (s->fHalService != nullptr)
        ? s->fHalService->get80211Controller() : nullptr;
    if (ic == nullptr) {
        a->rc = kIOReturnNotReady;
        return kIOReturnSuccess;
    }

    memcpy(ic->ic_psk, a->key->key, sizeof(ic->ic_psk));
    ic->ic_flags |= IEEE80211_F_PSK;
    ic->ic_external_pmk_owner = 0;

    struct ieee80211_wpaparams wpa;
    memset(&wpa, 0, sizeof(wpa));
    wpa.i_enabled = 1;
    wpa.i_protos  = IEEE80211_WPA_PROTO_WPA1 | IEEE80211_WPA_PROTO_WPA2;
    wpa.i_akms    = IEEE80211_WPA_AKM_PSK | IEEE80211_WPA_AKM_SHA256_PSK;
    a->wparc = ieee80211_ioctl_setwpaparms(ic, &wpa);

    a->ic_flags_after     = (uint32_t)ic->ic_flags;
    a->ic_rsnprotos_after = (uint32_t)ic->ic_rsnprotos;
    a->ic_rsnakms_after   = (uint32_t)ic->ic_rsnakms;
    a->rc = kIOReturnSuccess;
    return kIOReturnSuccess;
}

} // namespace

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
        return 0;
    }
    IOCommandGate *gate = getCommandGate();
    if (gate == nullptr) {
        XYLog("plti_publish_assoc_target NOT_READY gate=NULL\n");
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
    XYLog("plti_publish_assoc_target PUBLISHED generation=%llu "
          "ssid_len=%u bssid=%02x:%02x:%02x:%02x:%02x:%02x "
          "authtype_lower=0x%x authtype_upper=0x%x\n",
          (unsigned long long)a.out_generation, ssid_len,
          bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
          authtype_lower, authtype_upper);
    return a.out_generation;
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
    if (a.rc == kIOReturnSuccess) {
        XYLog("plti_wait_assoc_target RETURNED generation=%llu "
              "ssid_len=%u\n",
              (unsigned long long)out->generation, out->ssid_len);
    }
    return a.rc;
}

void AirportItlwm::
cancelPendingAssocTarget(const char *reason)
{
    IOCommandGate *gate = getCommandGate();
    if (gate == nullptr)
        return;
    gate->runAction(&airportItlwmCancelAssocAction, this);
    XYLog("plti_cancel_assoc_target CLEARED reason=%s "
          "ic_psk_zeroed=1 IEEE80211_F_PSK_dropped=1 "
          "ic_external_pmk_owner=0\n",
          reason != nullptr ? reason : "?");
}

void AirportItlwm::
invalidatePendingAssocTargetOnly(const char *reason)
{
    IOCommandGate *gate = getCommandGate();
    if (gate == nullptr)
        return;
    gate->runAction(&airportItlwmInvalidateAssocOnlyAction, this);
    XYLog("plti_invalidate_assoc_target_only CLEARED reason=%s "
          "ic_psk_preserved=1\n",
          reason != nullptr ? reason : "?");
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
        return kIOReturnBadArgument;
    }
    XYLog("deliverExternalPMK ENTRY key_len=%u "
          "cipher=%u flags=%u idx=%u generation_echo=%llu\n",
          key->key_len, key->key_cipher_type,
          key->key_flags, key->key_index,
          (unsigned long long)generation_echo);
    if (key->key_cipher_type != APPLE80211_CIPHER_PMK) {
        XYLog("deliverExternalPMK REJECT_CIPHER cipher=%u expected=%u\n",
              key->key_cipher_type, (unsigned)APPLE80211_CIPHER_PMK);
        return kIOReturnBadArgument;
    }
    if (key->key_len != IEEE80211_PMK_LEN) {
        XYLog("deliverExternalPMK REJECT_LEN key_len=%u expected=%u\n",
              key->key_len, (unsigned)IEEE80211_PMK_LEN);
        return kIOReturnBadArgument;
    }
    IOCommandGate *gate = getCommandGate();
    if (gate == nullptr) {
        XYLog("deliverExternalPMK NOT_READY gate=NULL\n");
        return kIOReturnNotReady;
    }
    AirportItlwmDeliverPmkArgs a;
    a.self             = this;
    a.key              = key;
    a.generation_echo  = generation_echo;
    a.rc               = kIOReturnSuccess;
    a.wparc            = 0;
    a.ic_flags_after   = 0;
    a.ic_rsnprotos_after = 0;
    a.ic_rsnakms_after = 0;
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
    XYLog("deliverExternalPMK INSTALLED generation_echo=%llu "
          "ic_psk_len=%zu IEEE80211_F_PSK_set=1 "
          "ic_external_pmk_owner=0 ic_flags=0x%x "
          "ic_rsnprotos=0x%x ic_rsnakms=0x%x setwpaparms_rc=%d "
          "(ENETRESET=%d expected)\n",
          (unsigned long long)generation_echo,
          sizeof(((struct ieee80211com *)nullptr)->ic_psk),
          a.ic_flags_after, a.ic_rsnprotos_after,
          a.ic_rsnakms_after, a.wparc, (int)ENETRESET);
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
 * gate, the [1, IEEE80211_AID_DEF] clamp, the APSTA state-block
 * fields softapMaxAssoc04/softapMaxAssocLimit08, and the
 * net80211 ic->ic_max_aid mutation all live inside the host
 * APSTA owner (AirportItlwmAPSTAOwner::setMisMaxSta and
 * setMaxAssoc) so the controller-layer selector is purely a
 * forward to the owner. When the owner is absent (default STA
 * boot before role-7 create) the recovered Apple body still
 * returns success without firmware interaction, so the selector
 * returns success without touching driver state.
 *
 * The local backend for the maxassoc admission limit is the
 * OpenBSD net80211 ic->ic_max_aid field, consumed by the existing
 * AID allocation loop in ieee80211_node_join() (rejects beyond
 * limit with IEEE80211_REASON_ASSOC_TOOMANY = 17). The AID/TIM
 * bitmap allocated at attach time covers IEEE80211_AID_DEF
 * entries; raising ic_max_aid above that capacity would overrun
 * ic_aid_bitmap and ic_tim_bitmap, so writes are clamped to
 * [1, IEEE80211_AID_DEF] inside the owner's setMaxAssoc body.
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
 * reached through AirportItlwm::releaseAll during driver release
 * and through the switch-only Tahoe VIRTUAL_IF_DELETE carrier
 * after that dispatch is migrated to the host owner in this
 * follow-up layer.
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
    if (in == nullptr) {
        return kIOReturnBadArgument;
    }
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
        return kIOReturnSuccess;
    }
    return fAPSTAOwner->setSoftAPExtCaps(in);
}

IOReturn AirportItlwm::
setMIS_MAX_STA(OSObject *object, struct apple80211_mis_max_sta *in)
{
    if (in == nullptr) {
        return kIOReturnBadArgument;
    }
    /*
     * Forward the selector input through the host APSTA owner's
     * setMisMaxSta entry point. The owner enforces the recovered
     * Apple AP-up gate (only an owner whose lifecycle is Running
     * and whose lower-HAL startAPMode has reported success
     * publishes AP-up true), and on AP-up forwards the input
     * dword +0x00 through the owner's setMaxAssoc, which writes
     * softapMaxAssoc04/softapMaxAssocLimit08 in the APSTA state
     * block and updates the net80211 ic->ic_max_aid admission
     * limit while preserving the AID/TIM bitmap invariant. The
     * recovered Apple body returns success without firmware
     * interaction whether AP is up or down, so the absence of
     * the owner (default STA boot before role-7 create) is not
     * an error: the controller-layer selector returns success
     * without touching driver state, preserving the boot-time
     * HostAP probe completion without producing a fake AP-mode
     * side effect.
     */
    if (fAPSTAOwner == NULL) {
        return kIOReturnSuccess;
    }
    return fAPSTAOwner->setMisMaxSta(in);
}

IOReturn AirportItlwm::setAPSTA_SSID(OSObject *object,
    struct apple80211_ssid_data *in)
{
    if (fAPSTAOwner == NULL) {
        return setSSID(object, in);
    }
    return fAPSTAOwner->setSSID(in);
}

IOReturn AirportItlwm::setAPSTA_CHANNEL(OSObject *object,
    struct apple80211_channel_data *in)
{
    if (fAPSTAOwner == NULL) {
        return setCHANNEL(object, in);
    }
    return fAPSTAOwner->setChannel(in);
}

IOReturn AirportItlwm::setAPSTA_CIPHER_KEY(OSObject *object,
    struct apple80211_key *in)
{
    if (fAPSTAOwner == NULL) {
        return kIOReturnUnsupported;
    }
    return fAPSTAOwner->setCipherKey(in);
}

IOReturn AirportItlwm::setHOST_AP_MODE_HIDDEN(OSObject *object,
    AirportItlwmAPSTAHostApModeHiddenLayout *in)
{
    if (in == nullptr) {
        return kIOReturnBadArgument;
    }
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
    if (in == nullptr) {
        return kIOReturnBadArgument;
    }
    if (fAPSTAOwner == NULL) {
        return static_cast<IOReturn>(kAirportItlwmAPSTASoftAPNotReadyReturn);
    }
    return fAPSTAOwner->setStationDisassociation(in, deauth);
}

IOReturn AirportItlwm::setSOFTAP_PARAMS(OSObject *object,
    AirportItlwmAPSTASoftAPParamsInputLayout *in)
{
    if (in == nullptr) {
        return kIOReturnBadArgument;
    }
    if (fAPSTAOwner == NULL) {
        return static_cast<IOReturn>(kAirportItlwmAPSTASoftAPNotReadyReturn);
    }
    return fAPSTAOwner->setSoftAPParams(in);
}

IOReturn AirportItlwm::setSOFTAP_TRIGGER_CSA(OSObject *object,
    AirportItlwmAPSTACsaInputLayout *in)
{
    if (in == nullptr) {
        return kIOReturnBadArgument;
    }
    if (fAPSTAOwner == NULL) {
        return kIOReturnNotReady;
    }
    return fAPSTAOwner->triggerCSA(
        static_cast<uint16_t>(in->channel04.channelNumber04),
        in->mode10);
}

IOReturn AirportItlwm::setSOFTAP_WIFI_NETWORK_INFO_IE(OSObject *object,
    AirportItlwmAPSTASoftAPWifiNetworkInfoCarrierLayout *in)
{
    if (in == nullptr) {
        return kIOReturnBadArgument;
    }
    if (fAPSTAOwner == NULL) {
        return static_cast<IOReturn>(kAirportItlwmAPSTASoftAPNotReadyReturn);
    }
    return fAPSTAOwner->setSoftAPWifiNetworkInfoIE(in);
}
