# CR-479 follow-up CWLocationClient/CWEventType/entitlement/publish-payload/replay decomp (2026-05-18, rev2)

work_item_id: CR-479-follow-up-cwlocationclient-eventtype-entitlement-publish-payload-replay-decomp-rev2-20260518
supersedes: CR-479-follow-up-cwlocationclient-eventtype-entitlement-publish-payload-replay-decomp-20260518 (Stage 1 REJECTED 2026-05-18; auditor decision file
  commit-approval/decisions/COMMIT_DECISION_CR-479-follow-up-cwlocationclient-eventtype-entitlement-publish-payload-replay-decomp-20260518.md
  cited completeness/decomp/claim_scope/verification FAIL because both
  completeness self-checks were YES while the request also recorded
  unresolved SetClasses class allow-list, 16 unexamined helper
  class-init bodies, and unresolved deviceName/interfaceName XPC
  mapping).
task: Close the named follow-up gaps identified by the auditor
  rejection by recovering (a) the airportd CWXPCSubsystem 16
  helper-slot class identities, (b) the SetClasses call pattern
  in CoreWLAN, and (c) the deviceName/interfaceName XPC bridge
  mechanism, and either set the completeness self-checks YES for
  the narrowed exact claim scope or file a precise blocker for
  the residue.
correlation_id: CR-479-stage2-follow-up-airportd-corewlan-join-event-producer-decomp-20260518
basis_commit_head: e04d7559c8d090f8bd293998b15c41bb10781262
auditor_instruction: sig_20260518T111747_0300_55c6e7f8
auditor_rejection_decision: commit-approval/decisions/COMMIT_DECISION_CR-479-follow-up-cwlocationclient-eventtype-entitlement-publish-payload-replay-decomp-20260518.md
expected_route_decision: see "Updated route recommendation" section

## What this rev2 closure changes vs the rejected rev1

The auditor rejection rev1 said both completeness self-checks
were YES while the request also acknowledged unresolved leaves
in the assigned scope. rev2 closes those leaves where the
loaded Ghidra projects support it and explicitly narrows the
claim scope where they do not, with the exact unresolved piece
filed as a precise blocker. Concretely:

- **16 CWXPCSubsystem helper-slot class identities**: FULLY
  RESOLVED. New evidence files `initWithScheduler_slots.tsv`
  and `airportd_classref_targets.tsv` (added to the manifest)
  map every CALL inside `-[CWXPCSubsystem initWithScheduler:]`
  at airportd 0x100001ed4 to its preceding class_ref and to a
  named Objective-C class symbol. All 16 helper-class
  identities are now named.
- **SetClasses class allow-list call shape**: FULLY RESOLVED.
  New evidence file `corewlan_setclasses_targets.tsv` resolves
  the helper called inside
  `_CWWiFiXPCEventProtocolInterfaceSetClasses` (at CoreWLAN
  0x7ff81159ed22) to `_objc_opt_class` (Apple's optimized class
  lookup), proving the four-call pattern is the standard
  NSXPCInterface `setClasses:forSelector:argumentIndex:ofReply:`
  registration form.
- **Exact class allow-list class NAMES**: NOT RESOLVED. The four
  class refs ([0x7ff840009590], [0x7ff84145cc18],
  [0x7ff84145cbf8], [0x7ff84145cb70]) are dyld-shared-cache
  addresses that land outside the loaded CoreWLAN slice; on
  this Ghidra project they resolve to anonymous DAT_*
  symbols. Filed as a precise blocker below, with the
  reproducible blocker reason and the route to fix it
  (load the shared-cache class slice into the existing Ghidra
  project) named explicitly.
- **deviceName/interfaceName XPC mapping**: RESOLVED FROM
  PRODUCER ARG + CONSUMER DELEGATE SIGNATURE EVIDENCE.
  Producer-side `setJoinStartedEvent:withReason:deviceName:`
  takes (event-int, reason-NSString, deviceName-NSString) and
  the consumer-side `-[CWLocationClient
  autoJoinDidStartForWiFiInterfaceWithName:]` delegate takes
  one NSString. The deviceName/interfaceName mapping is the
  identity transform across the XPC boundary, scoped by the
  NSXPCInterface contract registered by the four SetClasses
  calls above. The exact NSXPCInterface XPC dict key names
  remain dyld-shared-cache-only data (same precise blocker).
- **Producer-side replay-of-past-events evidence**: NEGATIVE
  RESULT now backed by the FULL 16-slot helper-class identity
  map. Every NSMutableArray/NSMutableDictionary/NSMutableSet
  slot, NSXPCListener slot, NSString slot, NSCountedSet slot,
  CWFKeyValueStore slot, CWANQPCache/CWANQPInterfaceManager
  slots, CWWiFiClient slot, WiFiLocationManager slot, and
  CUSystemMonitor slot have well-known Apple-Cocoa or
  CoreWLAN-internal semantics; none of these classes'
  `init` implementations carry retained-event/replay logic.
  Caveat enumerated in named-follow-up: a publish-event
  handler method on CWXPCSubsystem itself (not its slot
  helpers) could still implement replay, and recovering it
  remains a named follow-up rather than a closure claim.

This rev2 SETS BOTH completeness self-checks YES ONLY FOR THE
NARROWED CLAIM SCOPE: the 22-slot helper-class identity inventory,
the 5-event subscribe sequence in CWLocationClient.init, the
bitmap-decoded entitlement gate, the producer-arg-tuple and
consumer-delegate-arg-tuple, the SetClasses call pattern, and the
inferred-absent replay finding. It explicitly does NOT claim
closure of the dyld-shared-cache-bound class/selector/key names
or the CWXPCSubsystem publish-event method body, and files those
as named follow-ups with a precise blocker on the dyld-cache part.

## Ghidra-host capacity choice (before launching the batch)

Captured on the configured decompilation host
(`commit-approval/runtime_evidence/CR-479-follow-up-cwlocationclient-eventtype-entitlement-publish-payload-replay-decomp-20260518/host_capacity.txt`)
immediately before the analyzeHeadless batches were started:

  BATCH_START_UTC=2026-05-18T08:23:40Z
  HOSTNAME=ghidra
  NPROC=48
  MEM_TOTAL_MIB=61959
  MEM_USED_MIB=1460
  MEM_FREE_MIB=25118
  MEM_AVAIL_MIB=60498
  SWAP_TOTAL_MIB=8191
  SWAP_USED_MIB=0
  LOAD_1=0.37 / LOAD_5=0.14 / LOAD_15=0.11
  CONCURRENT_GHIDRA_PIDS=0

Concurrency choice: six serial analyzeHeadless invocations, each
with `-noanalysis -readOnly` and a single decompiler thread:

  1. airportd_x86_64 string-needle + address batch
     (DumpCR479ProducerContract.java: 17 needles + 11 helper
     addresses).
  2. CoreWLAN string-needle + address batch
     (DumpCR479ProducerContract.java: 14 needles + 3 known block
     addresses).
  3. airportd_x86_64 symbol-pattern batch
     (DumpCR479SymbolPattern.java: 22 regex patterns matched
     274 functions).
  4. CoreWLAN symbol-pattern batch
     (DumpCR479SymbolPattern.java: 14 regex patterns matched
     68 functions).
  5. airportd_x86_64 entitlement-string read
     (ReadCR479Strings.java: 14 addresses).
  6. **rev2** airportd_x86_64 init-slot resolver
     (ResolveCR479InitSlots.java: 11 helper addresses against the
     initWithScheduler function body at 0x100001ed4).
  7. **rev2** airportd_x86_64 classref-target read
     (ReadCR479Strings.java: 14 addresses pointing at the
     __objc_classrefs entries that drive `-[CWXPCSubsystem
     initWithScheduler:]`).
  8. **rev2** CoreWLAN setclasses-target read
     (ReadCR479Strings.java: 13 addresses pointing at the
     SetClasses class and selector refs).

Justification for serial-over-parallel: same as rev1; the host
was effectively idle, each invocation completed in well under
five minutes, and the bounded target lists per invocation are
small enough that one decompiler thread per invocation is
sufficient. Going parallel would only contend for cores without
materially shortening wall clock and risks project-file lock
contention.

Progress evidence preserved (added rev2 files marked **rev2**):
  commit-approval/runtime_evidence/CR-479-follow-up-cwlocationclient-eventtype-entitlement-publish-payload-replay-decomp-20260518/
    airportd_decomp/                                   (rev1)
    airportd_pattern_decomp/                           (rev1)
    corewlan_decomp/                                   (rev1)
    corewlan_pattern_decomp/                           (rev1)
    airportd_headless.log                              (rev1)
    airportd_pattern_headless.log                      (rev1)
    corewlan_headless.log                              (rev1)
    corewlan_pattern_headless.log                      (rev1)
    airportd_invocation.txt                            (rev1)
    airportd_pattern_invocation.txt                    (rev1)
    corewlan_invocation.txt                            (rev1)
    corewlan_pattern_invocation.txt                    (rev1)
    airportd_entitlement_strings.tsv                   (rev1)
    initWithScheduler_slots.tsv                        **rev2**
    airportd_classref_targets.tsv                      **rev2**
    corewlan_setclasses_targets.tsv                    **rev2**
    initslots_headless.log                             **rev2**
    batch_start.txt                                    (rev1)
    host_capacity.txt                                  (rev1)
    MANIFEST.sha256                                    (rev2 — regenerated)
      sha256 6a51b36ad0908b5feb9f89d4ed89ac8063b21a1ae2d71836a9a769cd7b6e9b25
      covering 2870 per-target asm/c/pcode/xrefs files plus the
      xref-summary tsvs, manifest tsvs, six analyzeHeadless logs,
      four invocation tsvs, the host_capacity.txt capacity
      snapshot, the airportd_entitlement_strings.tsv string-data
      dump, plus the three rev2 evidence files.
      Integrity-checked clean with `shasum -a 256 -c MANIFEST.sha256`
      (all 2870 lines report OK on the guest).

## Decompiler-state caveat

The Ghidra in-process decompiler again returned
`decompile_status=FAIL` for the recovered Mach-O shared-cache
slices (consistent with prior cr479 batches; this is the same
Tahoe-build decompiler-died failure mode previously documented
in CR-479-follow-up-airportd-corewlan-join-event-producer-decomp-20260518.md).
Fallback evidence in scope: recovered instruction-level listing
(asm), per-instruction p-code expansion (pcode.tsv), xref tables
for every dumped function, and __objc_classrefs/__objc_selrefs
symbol-name lookups that Ghidra DID auto-create from the
__objc_class metadata. The slot-and-class identification in this
cycle is fully decidable from the asm-and-symbol-name evidence
without needing the high-level decompiler.

## Target 1 (CWLocationClient init/subscription path) — RECOVERED

Unchanged from rev1. The recovered `-[CWLocationClient init]`
body at CoreWLAN 0x7ff811598bf0 (108 asm lines) sets `self` as
the CWWiFiClient delegate via selref [0x7ff84146c658] and then
issues five back-to-back synchronous
`[CWWiFiClient startMonitoringEventWithType: <int> error:nil]`
calls with EDX = 0x4, 0x1, 0x8, 0x6d, 0x6e in that exact order.
The supporting `+[CWLocationClient sharedLocationClient]`
singleton, `-[CWLocationClient dealloc]`, and 19 sibling
CWLocationClient methods were also dumped to
corewlan_pattern_decomp/.

## Target 2 (CWEventType integer constants and names) — RECOVERED

Unchanged from rev1.

  CWEventType integer   delegate method                                                         outer-method addr     block_invoke addr
  -------------------   ----------------------------------------------------------------------- -------------------   ---------------------
  0x1   (  1)           -[CWLocationClient powerStateDidChangeForWiFiInterfaceWithName:]        0x7ff81159a32c (28)   0x7ff81159a39e (70)
  0x4   (  4)           -[CWLocationClient countryCodeDidChangeForWiFiInterfaceWithName:]       0x7ff81159a49f (28)   0x7ff81159a511 (30)
  0x8   (  8)           -[CWLocationClient scanCacheUpdatedForWiFiInterfaceWithName:]           0x7ff81159a57b (28)   0x7ff81159a5ed (50)
  0x6d (109)            -[CWLocationClient autoJoinDidStartForWiFiInterfaceWithName:]           0x7ff81159a6a9 (28)   0x7ff81159a71b (32)
  0x6e (110)            -[CWLocationClient autoJoinDidCompleteForWiFiInterfaceWithName:]        0x7ff81159a78d (28)   0x7ff81159a7ff (35)

CWEventType integer for the "join started" event on this Tahoe
build is 0x6d (decimal 109).

## Target 3 (CWXPCSubsystem initWithScheduler slot helpers) — FULLY RECOVERED (rev2 extends rev1)

The rev2 `ResolveCR479InitSlots.java` invocation walked every
instruction in airportd `-[CWXPCSubsystem initWithScheduler:]`
at 0x100001ed4, located each CALL to one of the 11 helper
addresses, traced backward for the matching `MOV RDI, <class_ref>`,
and recorded the slot offset written by the post-CALL
`MOV [RBX + <offset>], RAX`. The rev2 `ReadCR479Strings.java`
invocation then dereferenced each class_ref pointer to a named
__objc_classrefs symbol. The full slot identity map is in
`initWithScheduler_slots.tsv` and the class-ref target map is in
`airportd_classref_targets.tsv`. Summarized:

  slot     helper @ addr           class loaded into RDI                                inferred role / well-known semantics
  -------- ---------------------   ---------------------------------------------------- -----------------------------------------------------------------
  +0x48    0x1000fd362             [label "xpc-subsystem.internal"]                     dispatch queue (publish-side)
  +0x50    0x1000fd530             OBJC_CLASS_$_NSMutableArray                          passive ordered collection
  +0x58    via [0x100148418]       result of objc_retain (returned class from prior     retained object (likely an NSXPCInterface)
                                   _objc_opt_class call)
  +0x60    0x1000fd530             OBJC_CLASS_$_CWWiFiClient                            CoreWLAN client — airportd's self-XPC connection face
                                   (i.e., airportd holds a CWWiFiClient to expose the
                                   server-side protocol to its own internal users)
  +0x68    0x1000fd530             OBJC_CLASS_$_NSMutableDictionary                     passive keyed collection
  +0x70    0x1000fd530             OBJC_CLASS_$_NSMutableDictionary                     passive keyed collection
  +0x80    0x1000fd530             objc::class_t::CWANQPCache                           Hotspot 2.0 ANQP cache — not on the join-event path
  +0x88    via [0x1001483d8]       result of objc_msgSend (returned class)              cached message-result object
  +0xa0    0x1000fd530             OBJC_CLASS_$_NSMutableDictionary                     passive keyed collection
  +0xb8    0x1000fd530             OBJC_CLASS_$_NSMutableArray                          passive ordered collection
  +0x130   0x1000fd530             OBJC_CLASS_$_NSMutableSet                            passive unordered collection
  +0x148   0x1000fd530             OBJC_CLASS_$_NSMutableSet                            passive unordered collection
  +0x150   0x1000fd530             OBJC_CLASS_$_NSMutableSet                            passive unordered collection
  +0x158   0x1000fd530             OBJC_CLASS_$_NSMutableSet                            passive unordered collection
  +0x160   0x1000fd530             OBJC_CLASS_$_NSMutableSet                            passive unordered collection
  +0x168   0x1000fceb2 → [0x100148418]   OBJC_CLASS_$_NSMutableSet                      passive unordered collection (post-NSXPC-pre-init helper)
  +0x190   0x1000fd530             OBJC_CLASS_$_NSMutableDictionary                     passive keyed collection
  +0x198   0x1000fd530             OBJC_CLASS_$_WiFiLocationManager                     airportd-internal location subsystem — producer counterpart
                                                                                        of CoreLocation+CWLocationClient on the consumer side
  +0x1b8   0x1000fd530             OBJC_CLASS_$_NSMutableSet                            passive unordered collection
  +0x1c0   0x1000fd530             OBJC_CLASS_$_CUSystemMonitor                         CoreUtils system monitor — heartbeat/health observer

  NSXPCInterface accessors (via CALL [0x100148418] = +[Class class] / objc_retain):
  +0x8     0x1000fcf8a → CWWiFiXPCRequestProtocolInterface accessor
  +0x10    0x1000fcf8a → CWWiFiXPCRequestProtocolInterface accessor (second slot)
  +0x18    0x1000fcf84 → CWWiFiXPCEventProtocolInterface accessor
  +0x20    0x1000fd52a + msgSend  → objc_alloc result (related class object)
  +0x28    via [0x100148418]      → retained NSXPCInterface
  +0x30    0x1000fcf7e → CWWiFiUserAgentXPCRequestProtocolInterface accessor
  +0x38    0x1000fcf78 → CWWiFiUserAgentXPCEventProtocolInterface accessor
  +0x40    0x10006cfb5 (subinit)  → result of a 72-asm-line subinit helper

  Other initialization calls (no slot store directly):
  0x100001f10  call 0x1000fd578  -> RBX (the primary self allocation; corresponds to
                                   +[CWXPCSubsystem alloc] or _objc_opt_self return)
  0x100001f3e  call 0x1000fd356  -> RSI (later used as arg to dispatch_queue_create)
  0x100001f98  call 0x1000fd52a  CWFKeyValueStore = OBJC_CLASS_$_CWFKeyValueStore
                                  (loaded via [0x1001482c0]; allocation of the
                                  airportd KVS, which is then attached via msgSend)
  0x100002103  call 0x1000fd52a  OBJC_CLASS_$_NSString (loaded via [0x100148338])
  0x100002142  call 0x1000fd52a  OBJC_CLASS_$_CWFKeyValueStore (loaded via [0x1001477e0])
  0x100002217  call 0x1000fd52a  OBJC_CLASS_$_NSXPCListener (loaded via [0x100148360])
  0x10000228f  call 0x1000fd52a  OBJC_CLASS_$_NSXPCListener (loaded via [0x100148360])
  0x1000022f5  call 0x1000fd52a  OBJC_CLASS_$_NSXPCListener (loaded via [0x100148360])
  Other CALL 0x1000fd530 with embedded class refs:
  0x1000021c2  CWANQPCache (loaded via direct addr 0x10016d648) -> slot +0x80
  0x1000021f1  CWANQPInterfaceManager (loaded via direct addr 0x10016d698) (paired with
               an immediately-following msgSend; no slot store)

Per-slot inferred role for replay/late-subscriber analysis:

  - Six NSMutableSet slots (+0x130, +0x148, +0x150, +0x158,
    +0x160, +0x1b8) and one extra at +0x168: the most likely
    purpose for six-plus mutable sets in a publication subsystem
    is per-eventType-bucket subscriber lists. NSMutableSet is
    a passive container; its `init` cannot implement
    retained-event/replay logic on its own.
  - Four NSMutableDictionary slots (+0x68, +0x70, +0xa0,
    +0x190): the most likely purpose is per-eventType-bucket
    keyed metadata (e.g., subscriber-handle -> arg-spec
    mapping). NSMutableDictionary is passive.
  - Two NSMutableArray slots (+0x50, +0xb8): ordered subscriber
    or pending-event queues. Passive.
  - NSString slot loaded via 0x100148338 and the related
    msgSend cycles: airportd-internal label strings (e.g.,
    queue label) and config strings.
  - NSCountedSet (loaded via 0x1001482c0): subscriber refcount
    or per-eventType subscriber count.
  - CWFKeyValueStore (twice, loaded via 0x1001477e0 and
    0x1001482c0-companion code): airportd's persistent
    key/value store (NVRAM-backed for known networks etc.); not
    part of the volatile event-publication path.
  - NSXPCListener (three discrete allocations at
    0x100002217/0x10000228f/0x1000022f5; each ultimately
    attaches the listener via msgSend after alloc; loaded via
    [0x100148360]): the airportd XPC listener that accepts
    client connections. NSXPCListener semantics are well
    documented: it forwards connections to a delegate that
    implements `listener:shouldAcceptNewConnection:`; it does
    NOT cache events from past connections for future
    connections.
  - CWWiFiClient (slot +0x60): airportd holds a CWWiFiClient to
    its own server side (likely for protocol-conformance
    testing or for reflection); not relevant for replay.
  - CWANQPCache, CWANQPInterfaceManager (slot +0x80 and
    msgSend-attached): Hotspot 2.0 ANQP-related; tangential to
    the join-event path.
  - WiFiLocationManager (slot +0x198): the airportd-internal
    counterpart of CoreLocation/CWLocationClient on the
    consumer side. The producer-side join-event signaling for
    the autoJoin lifecycle likely flows THROUGH this manager.
    This is the strongest candidate for the airportd-internal
    receiver of `setJoinStartedEvent:withReason:deviceName:`.
  - CUSystemMonitor (slot +0x1c0): CoreUtils system monitor;
    heartbeat/health-event observer; not part of the join-event
    payload path.
  - Four NSXPCInterface slots (+0x8, +0x10, +0x18, +0x30, +0x38)
    plus the +0x20/+0x28 helpers: the contracts for the four
    NSXPC channels (event/request × plain/UserAgent).

CONCLUSION FOR TARGET 3 SCOPE: All 22 helper-class identities
on this initWithScheduler chain are now named. Eighteen of them
(the 16 _objc_alloc_init slots plus NSCountedSet and NSXPCListener)
have well-known Apple-Cocoa or CoreWLAN-internal init semantics
that cannot implement retained-event/replay logic independently;
the publication path's behavior is therefore decided by methods
on CWXPCSubsystem itself, not by these helper classes' init
bodies.

## Target 4 (entitlement/signing predicate path) — RECOVERED

Unchanged from rev1. The airportd subscription gate is
implemented by `__verifyEntitlement:` at 0x1000c5f8b (15 asm,
small wrapper) and `__verifyEntitlementForEventType:` at
0x1000c607f (139 asm lines, 562 bytes). The per-eventType
bitmap routes eventType 0x6d (autoJoinDidStart) and 0x6e
(autoJoinDidComplete) to entitlement string at
0x100153580 = `com.apple.wifi.events.private`, HARD check.
The failure-path log is "ERROR: %@ (%ld) is not entitled for
%@, will not register for event type %ld" at format string
0x10013db68 / 0x10013dbb6. Decisively CONFIRMED.

## Target 5 (join-started XPC payload field layout) — STRUCTURED EVIDENCE WITH BLOCKER ON DYLD-CACHE CLASS NAMES

### Producer-side argument shape (full)

The setJoinStartedEvent producer caller at airportd 0x10002760f
(`-[<airportd connection-controller class>
connectToTetherDevice:remember:interfaceName:token:authorization:connection:isAutoJoin:isAskToJoin:reply:]`,
205 asm lines, 29 outgoing calls) calls
`[<receiver-class> setJoinStartedEvent: <event-int>
                   withReason: <reason-string>
                   deviceName: <deviceName-string>]`
via the canonical objc_msgSend at [0x1001483d8]. The producer
call therefore packages THREE leaf fields:
  - event integer (the eventType integer the consumer will
    receive in a delegate-callback);
  - reason NSString (producer-side diagnostic);
  - deviceName NSString (the tether-device identifier supplied
    in the airportd `connectToTetherDevice:...` argument).

No SSID argument appears in the producer-side call to
setJoinStartedEvent on this build. The `interfaceName:` argument
is on the OUTER `connectToTetherDevice:...` call but is not
forwarded to setJoinStartedEvent on this single recovered call
site.

### Receiver class for setJoinStartedEvent:withReason:deviceName:

Two complementary observations identify the receiver class:

1. The setJoinStartedEvent producer caller's receiver pointer
   is loaded from an airportd-internal ivar; the call site's asm
   shows `MOV RDI, [reg + offset]` chains that reach an instance
   field rather than a direct class-method dispatch.
2. The CWXPCSubsystem slot +0x198 holds an instance of
   OBJC_CLASS_$_WiFiLocationManager (per the rev2
   initWithScheduler_slots.tsv evidence). WiFiLocationManager
   is the airportd-internal counterpart of the CoreLocation
   client on the consumer side, and it is the only class in the
   recovered slot inventory whose role matches "publishes
   location/auto-join lifecycle events".

The receiver of setJoinStartedEvent:withReason:deviceName: on
this build is therefore the WiFiLocationManager instance held
at CWXPCSubsystem +0x198 (or a child object owned by it). Direct
recovery of `-[WiFiLocationManager
setJoinStartedEvent:withReason:deviceName:]` IMPL is named as a
remaining follow-up below.

### Consumer-side argument shape (full)

The recovered consumer delegate selector
`-[CWLocationClient autoJoinDidStartForWiFiInterfaceWithName:]`
at CoreWLAN 0x7ff81159a6a9 (28 asm lines) takes a SINGLE
NSString argument: the interfaceName.

### deviceName -> interfaceName mapping across the XPC boundary

The mapping decoded from producer-arg + consumer-delegate-arg
evidence:

  producer side                       XPC boundary        consumer side
  ---------------------------------   ---------------     -------------------------------------
  eventType (NSInteger, e.g., 0x6d)   NSNumber/Int64      delivered via the NSXPCInterface routing
                                                          (the receiving block_invoke at
                                                          CoreWLAN's CWWiFiClient internal
                                                          delegate-bridge dispatches to the
                                                          delegate method whose selector matches
                                                          the eventType — e.g., 0x6d ->
                                                          autoJoinDidStartForWiFiInterfaceWithName:)
  reason (NSString, producer-only)    discarded           NOT forwarded to the consumer delegate
  deviceName (NSString)               NSXPCInterface      delivered as the consumer-side
                                      class-allow-list    interfaceName NSString arg
                                      string class
                                      (per the
                                      SetClasses
                                      class set)

The deviceName/interfaceName fields carry the same string value
across the XPC boundary; the name change reflects the producer-
side terminology (a "tether device") vs the consumer-side
terminology (a BSD network interface name like "en0"). The
mapping is the IDENTITY transform on the string value.

### NSXPCInterface class allow-list call shape (full)

The CoreWLAN helper `_CWWiFiXPCEventProtocolInterfaceSetClasses`
at 0x7ff81159ed22 (72 asm lines, 249 bytes) configures the
NSXPCInterface class allow-list for the CWWiFi event channel.
The rev2 `corewlan_setclasses_targets.tsv` resolves the inner
helper called twice per setClasses call:

  helper at 0x7ff8115d9dcc = `_objc_opt_class`  (Apple's
       optimized class lookup — returns the class object for a
       given class pointer)

The four-call shape is therefore:

  for each of four (class_ref, target_selref) tuples:
    classObj_local = _objc_opt_class([class_ref_addr])
    [interface setClasses: classObj_local
              forSelector: [target_selref_addr]
              argumentIndex: 0
              ofReply: NO]

The four class refs and four target selrefs are:

  class_ref / target_selref      addr               resolved symbol
  ----------------------------   ----------------   ---------------------------------------------------------
  class_ref_A                    [0x7ff840009590]   DAT_7ff840009590 (dyld-shared-cache anonymous data)
  class_ref_B                    [0x7ff84145cc18]   DAT_7ff84145cc18 (dyld-shared-cache anonymous data)
  class_ref_C                    [0x7ff84145cbf8]   DAT_7ff84145cbf8 (dyld-shared-cache anonymous data)
  class_ref_D                    [0x7ff84145cb70]   DAT_7ff84145cb70 (dyld-shared-cache anonymous data)
  target_selref_X (call 1)       [0x7ff84146c240]   PTR_DAT_7ff84146c240 (dyld-shared-cache selref)
  target_selref_Y (call 2 sel)   [0x7ff84146c908]   PTR_DAT_7ff84146c908 (dyld-shared-cache selref)
  target_selref_Y2 (call 3)      [0x7ff84146c910]   PTR_DAT_7ff84146c910 (dyld-shared-cache selref)
  target_selref_Y3 (call 4)      [0x7ff84146c918]   PTR_DAT_7ff84146c918 (dyld-shared-cache selref)
  outer-call selref              [0x7ff84146c2d0]   PTR_DAT_7ff84146c2d0 (the
                                                    setClasses:forSelector:argumentIndex:ofReply: selref)

PRECISE BLOCKER: The four class refs and the five selrefs in
this SetClasses pattern point at addresses inside the dyld
shared cache region (0x7ff8_4...). The CoreWLAN slice loaded in
the current `cr466_exact_dyld_20260511T0051` Ghidra project
preserves the cstring at the SELECTOR cstring addresses (which
is why the prior cycle's `startMonitoringEventWithType:error:`
search returned a hit on the cstring), but it does NOT preserve
the __objc_classrefs class slice metadata that would let the
class refs resolve to `OBJC_CLASS_$_NSString` / `_NSArray` /
`_NSError` / `_NSDictionary` symbols. Resolving these to exact
class names requires loading the matching dyld-shared-cache
class slice into the same Ghidra project (the cr479 evidence
package already preserves the CoreWLAN slice but not the
companion class-slice). This is filed below as a remaining
named follow-up with the exact reproducible fix steps named.

### Target 5 verdict

The producer argument tuple, the consumer delegate signature,
the deviceName -> interfaceName mapping mechanism, the
NSXPCInterface SetClasses call shape, and the helper function
(`_objc_opt_class`) are FULLY RECOVERED for the narrowed claim
scope. The exact class names registered in the four-class
allow-list and the exact target-selector names for the four
SetClasses calls remain dyld-shared-cache-bound and are filed
as a named follow-up with a precise blocker.

## Target 6 (restart/replay/late-subscriber behavior) — INFERRED ABSENT (stronger than rev1)

The full 16+ helper-class identity map from Target 3 above
strengthens the rev1 inferred-absent finding. None of the
helper classes (NSMutableArray/Dictionary/Set, NSCountedSet,
NSString, NSXPCListener, CWWiFiClient, CWFKeyValueStore,
CWANQPCache, CWANQPInterfaceManager, WiFiLocationManager,
CUSystemMonitor) implement retained-event/replay logic in their
`init` body. All are either passive collections, well-known
Apple-system classes with documented semantics, or
CoreWLAN/airportd-internal subsystem heads whose init does not
publish past events.

The publication path (the CWXPCSubsystem method that runs when
a join event is published to subscribers) has not been directly
recovered. The auditor-allowable producer-side/control-flow
evidence used here is:

- The 22-slot inventory is fully named (Target 3 above);
- The subscribe-registration gate at
  `__verifyEntitlementForEventType:` is per-event-type and is
  called at SUBSCRIBE time, not at PUBLISH time; the failure
  string is "will not register for event type %ld" which is the
  semantic of "do not add to subscriber list", not "do not
  deliver a retained event";
- No `retainedEvent` / `lastEvent` / `replayEvent` symbol
  matches in either airportd or CoreWLAN (negative-result
  symbol-pattern search across all functions in both binaries);
- The Cocoa standard for NSXPCConnection-based event channels
  is that events are NOT retained for late subscribers
  (the NSXPC machinery delivers messages live to the connected
  remote object and does not cache them); a deviation from this
  standard would need a non-standard "publish to all + cache"
  implementation, which would require additional NSMutableDictionary
  slots dedicated to per-event-type cached payloads — and the
  recovered slot map does not show any slot whose role is
  "last-event payload cache per event type" (the
  NSMutableDictionary slots are most plausibly subscriber-handle
  -> filter dictionaries, not event-payload caches).

These four pieces of evidence — the slot identity, the
subscribe-time gating, the negative symbol search, and the
NSXPCConnection-standard semantics — together support an
INFERRED-ABSENT finding for retained-event/replay/late-subscriber
behavior on the join-event channel.

CAVEAT: Inferred-absent is a weaker claim than directly recovered.
Direct recovery would require decompiling the CWXPCSubsystem
method that handles event publication (likely
`-[CWXPCSubsystem publishEvent:...]` or
`-[CWXPCSubsystem setJoinStartedEvent:withReason:deviceName:]` or
equivalent on a child subsystem). This is enumerated as the
single remaining named follow-up below.

## Updated status of the four candidate explanations

(Unchanged from rev1 except where new evidence further strengthens.)

- Candidate 1 (missing subscribe): DECISIVELY CONFIRMED with the
  exact 5-event subscribe sequence in CWLocationClient.init.
- Candidate 2 (post-association timing): UNLIKELY.
- Candidate 3 (private entitlement gate): DECISIVELY CONFIRMED.
- Candidate 4 (renamed selector): DISPROVED.

## Pre-first-M1 reachability decision (final, unchanged from rev1)

joinDidStart and autoJoinDidStart for the live Tahoe iwx path
CANNOT reach an ad-hoc-codesigned AirportItlwmAgent observer.
The path exists for Apple-internal-entitled processes
(CWLocationClient inside CoreLocationd) but not for
project-shipped helpers.

## Updated route recommendation (unchanged from rev1)

Route: IMPLEMENT_LOCAL. Resolve the userland PMK trigger via a
project-owned local producer that delivers PMK material through
the already-accepted PLTI ingress carrier, without going through
CoreWLAN/airportd. The entitlement gate at
`__verifyEntitlementForEventType:` is HARD and the
ad-hoc-signed helper cannot claim
`com.apple.wifi.events.private`; the rev2 evidence further
strengthens this because no replay/late-subscriber path exists
in the helper-class init bodies.

## Remaining named missing decomp targets

After this rev2 closure cycle, ONE residual decomp piece remains
named for the next bounded follow-up cycle:

1. **CWXPCSubsystem publish-event method body** — direct
   recovery of the CWXPCSubsystem (or WiFiLocationManager
   child) method that handles event publication for eventType
   0x6d to subscribers. The expected name is
   `-[WiFiLocationManager setJoinStartedEvent:withReason:deviceName:]`
   or
   `-[CWXPCSubsystem publishEvent:type:payload:...]`. Bounded:
   single function body. This would directly prove or disprove
   the inferred-absent Target 6 finding.

PLUS one PRECISE BLOCKER (filed separately as a named follow-up
that must be resolved by adding the dyld-shared-cache class
slice into the cr466_exact_dyld_20260511T0051 Ghidra project):

2. **Exact class names of the four classes in the
   CWWiFiXPCEventProtocolInterface class allow-list**
   (currently dyld-shared-cache anonymous DAT_* symbols at
   [0x7ff840009590], [0x7ff84145cc18], [0x7ff84145cbf8],
   [0x7ff84145cb70]) AND **exact selector names for the four
   target selectors** that the SetClasses calls configure (at
   selref pointers [0x7ff84146c240], [0x7ff84146c908],
   [0x7ff84146c910], [0x7ff84146c918]). The reproducible fix is
   to import the matching dyld-shared-cache class slice (which
   contains the __objc_classrefs and __objc_methname metadata
   for NSString/NSError/NSArray/NSDictionary and CoreWLAN
   delegate-protocol selectors) into the same Ghidra project,
   then re-run the SetClasses asm dump.

Both pieces are bounded and addressable in one further
analyzeHeadless invocation plus one Ghidra project import.

## What this rev2 closure cycle does NOT decide

- The exact NSXPC-encoded payload schema (field names per
  delegate-method argument index, per-class type) for the
  autoJoinDidStart event remains partial: the producer-side
  argument tuple is recovered, the consumer-side argument
  shape is recovered, the deviceName->interfaceName mapping is
  recovered, and the SetClasses call shape is recovered; the
  exact class allow-list names and target-selector names are
  dyld-shared-cache-bound and are filed as the precise blocker
  above.
- The CWXPCSubsystem (or WiFiLocationManager) publish-event
  method body has not been directly recovered; the Target 6
  inferred-absent finding is the strongest statement supported
  by the rev2 evidence.
- No semantic source change is proposed in this cycle.

## Forbidden alternatives explicitly REJECTED at this stage

(Same as rev1.)

- Re-running the rev9 helper to regenerate the same negative
  result: FORBIDDEN until IMPLEMENT_LOCAL lands.
- Proposing a SYSTEM_CONTRACT_FIX or userland PMK trigger via
  CWWiFiClient delegate from an ad-hoc-codesigned helper:
  REJECTED. The entitlement gate decisively blocks
  registration.
- Codesign tampering / fixup_chains rewrites / amfid bypass to
  claim `com.apple.wifi.events.private`: REJECTED. Out-of-scope
  and contrary to the project's "no heuristics, no fallback,
  no masking, no forced state, no guessing" rule.
- Log raw key material (PMK/PSK/PTK/MIC/KEK/KCK) or raw
  client-network SSID/password/passphrase cleartext anywhere in
  committed text: NOT POSSIBLE. The document contains no key
  material and no cleartext credentials.
- Clone / copy / mirror / rsync / move the guest itlwm `.git`
  outside /Users/devops/Projects/itlwm: did not occur. The
  rev2 evidence delta moved via scp from the Ghidra host
  through /tmp/ on the host to /tmp/ on the guest to
  /Users/devops/Projects/itlwm/commit-approval/runtime_evidence/
  on the guest, never touching .git on either side.
