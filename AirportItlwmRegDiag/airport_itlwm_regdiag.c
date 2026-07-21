#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <ClientKit/AirportItlwmRegDiag.h>

static const char *
kind_name(uint32_t kind)
{
    switch (kind) {
        case kAirportItlwmRegDiagTracePublicAssoc: return "public-assoc";
        case kAirportItlwmRegDiagTraceHiddenAssoc: return "hidden-assoc";
        case kAirportItlwmRegDiagTraceLinkState: return "link-state";
        case kAirportItlwmRegDiagTraceTx: return "tx";
        case kAirportItlwmRegDiagTraceRx: return "rx";
        case kAirportItlwmRegDiagTraceBlock: return "block";
        case kAirportItlwmRegDiagTraceControl: return "control";
        case kAirportItlwmRegDiagTraceAuthPolicy: return "auth-policy";
        case kAirportItlwmRegDiagTracePmkIngress: return "pmk-ingress";
        case kAirportItlwmRegDiagTracePmkClear: return "pmk-clear";
        case kAirportItlwmRegDiagTracePltiPublish: return "plti-publish";
        case kAirportItlwmRegDiagTracePltiDeliver: return "plti-deliver";
        case kAirportItlwmRegDiagTraceLinkStatus: return "link-status";
        case kAirportItlwmRegDiagTraceLinkPublish: return "link-publish";
        case kAirportItlwmRegDiagTraceWclJoinAbort: return "join-abort";
        case kAirportItlwmRegDiagTraceLinkContext: return "link-context";
        default: return "unknown";
    }
}

static const char *
path_name(uint32_t path)
{
    switch (path) {
        case kAirportItlwmRegDiagPathPublicAssoc: return "public-assoc";
        case kAirportItlwmRegDiagPathHiddenAssoc: return "hidden-assoc";
        case kAirportItlwmRegDiagPathTx: return "tx";
        case kAirportItlwmRegDiagPathRx: return "rx";
        case kAirportItlwmRegDiagPathLink: return "link";
        case kAirportItlwmRegDiagPathPmk: return "pmk";
        case kAirportItlwmRegDiagPathPlti: return "plti";
        case kAirportItlwmRegDiagPathLifecycle: return "lifecycle";
        default: return "unknown";
    }
}

static const char *
pmk_source_name(uint32_t source)
{
    switch (source) {
        case kAirportItlwmRegDiagPmkSourceCipherKey: return "cipher-key";
        case kAirportItlwmRegDiagPmkSourceCipherKeyMsk: return "cipher-key-msk";
        case kAirportItlwmRegDiagPmkSourceCurPmk: return "cur-pmk";
        case kAirportItlwmRegDiagPmkSourcePlti: return "plti";
        default: return "unknown";
    }
}

static const char *
pmk_decision_name(uint32_t decision)
{
    switch (decision) {
        case kAirportItlwmRegDiagPmkDecisionAccepted: return "accepted";
        case kAirportItlwmRegDiagPmkDecisionRejectInput: return "reject-input";
        case kAirportItlwmRegDiagPmkDecisionRejectNull: return "reject-null";
        case kAirportItlwmRegDiagPmkDecisionRejectLength: return "reject-length";
        case kAirportItlwmRegDiagPmkDecisionRejectWpa3: return "reject-wpa3";
        case kAirportItlwmRegDiagPmkDecisionRejectPolicy: return "reject-policy";
        case kAirportItlwmRegDiagPmkDecisionRejectGeneration: return "reject-generation";
        case kAirportItlwmRegDiagPmkDecisionRejectTerminating: return "reject-terminating";
        case kAirportItlwmRegDiagPmkDecisionNotReady: return "not-ready";
        default: return "unknown";
    }
}

static const char *
pmk_clear_reason_name(uint32_t reason)
{
    switch (reason) {
        case kAirportItlwmRegDiagPmkClearAssocDisableRsn: return "assoc-disable-rsn";
        case kAirportItlwmRegDiagPmkClearDisassociate: return "disassociate";
        case kAirportItlwmRegDiagPmkClearPmksa: return "pmksa";
        case kAirportItlwmRegDiagPmkClearLeave: return "leave";
        case kAirportItlwmRegDiagPmkClearReassoc: return "reassoc";
        case kAirportItlwmRegDiagPmkClearJoinAbort: return "join-abort";
        case kAirportItlwmRegDiagPmkClearTerminate: return "terminate";
        default: return "unknown";
    }
}

static const char *
link_status_decision_name(uint32_t decision)
{
    switch (decision) {
        case kAirportItlwmRegDiagLinkStatusSame: return "same";
        case kAirportItlwmRegDiagLinkStatusApplied: return "applied";
        case kAirportItlwmRegDiagLinkStatusLifecycleRejected: return "lifecycle-rejected";
        default: return "unknown";
    }
}

static const char *
link_publish_decision_name(uint32_t decision)
{
    switch (decision) {
        case kAirportItlwmRegDiagLinkPublishQueued: return "queued";
        case kAirportItlwmRegDiagLinkPublishSourceUnavailable: return "source-unavailable";
        case kAirportItlwmRegDiagLinkPublishOffGateRejected: return "off-gate-rejected";
        case kAirportItlwmRegDiagLinkPublishPublished: return "published";
        case kAirportItlwmRegDiagLinkPublishActionUnavailable: return "action-unavailable";
        default: return "unknown";
    }
}

static const char *
link_context_route_name(uint32_t route)
{
    switch (route) {
        case kAirportItlwmRegDiagLinkContextNet80211Bridge:
            return "net80211-bridge";
        case kAirportItlwmRegDiagLinkContextControllerStatus:
            return "controller-status";
        case kAirportItlwmRegDiagLinkContextPublishQueue:
            return "publish-queue";
        case kAirportItlwmRegDiagLinkContextPublishAction:
            return "publish-action";
        case kAirportItlwmRegDiagLinkContextGate:
            return "link-gate";
        case kAirportItlwmRegDiagLinkContextSkywalkParent:
            return "skywalk-parent";
        case kAirportItlwmRegDiagLinkContextWclUpdate:
            return "wcl-update";
        default:
            return "unknown";
    }
}

static const char *
link_context_stage_name(uint32_t stage)
{
    switch (stage) {
        case kAirportItlwmRegDiagLinkContextEnter: return "enter";
        case kAirportItlwmRegDiagLinkContextSameStatus: return "same-status";
        case kAirportItlwmRegDiagLinkContextLifecycleRejected:
            return "lifecycle-rejected";
        case kAirportItlwmRegDiagLinkContextBaseApplied: return "base-applied";
        case kAirportItlwmRegDiagLinkContextSourceUnavailable:
            return "source-unavailable";
        case kAirportItlwmRegDiagLinkContextSourceReady: return "source-ready";
        case kAirportItlwmRegDiagLinkContextActionUnavailable:
            return "action-unavailable";
        case kAirportItlwmRegDiagLinkContextActionReady: return "action-ready";
        case kAirportItlwmRegDiagLinkContextGateRejected: return "gate-rejected";
        case kAirportItlwmRegDiagLinkContextGateReady: return "gate-ready";
        case kAirportItlwmRegDiagLinkContextParentEnter: return "parent-enter";
        case kAirportItlwmRegDiagLinkContextParentAccepted:
            return "parent-accepted";
        case kAirportItlwmRegDiagLinkContextParentRejected:
            return "parent-rejected";
        case kAirportItlwmRegDiagLinkContextWclDecoded: return "wcl-decoded";
        case kAirportItlwmRegDiagLinkContextWclReturn: return "wcl-return";
        default: return "unknown";
    }
}

static const char *
link_context_predicate_name(uint32_t predicate)
{
    switch (predicate) {
        case kAirportItlwmRegDiagLinkContextPredicateFalse: return "no";
        case kAirportItlwmRegDiagLinkContextPredicateTrue: return "yes";
        default: return "unknown";
    }
}

static const char *
link_context_lifecycle_name(uint32_t lifecycle)
{
    switch (lifecycle) {
        case kAirportItlwmRegDiagLinkContextLifecycleControllerSame:
            return "controller-same";
        case kAirportItlwmRegDiagLinkContextLifecycleControllerAdmitted:
            return "controller-admitted";
        case kAirportItlwmRegDiagLinkContextLifecycleControllerRejected:
            return "controller-rejected";
        case kAirportItlwmRegDiagLinkContextLifecycleControllerDrainOwner:
            return "controller-drain-owner";
        case kAirportItlwmRegDiagLinkContextLifecyclePublicationUnavailable:
            return "publication-unavailable";
        case kAirportItlwmRegDiagLinkContextLifecyclePublicationReady:
            return "publication-ready";
        case kAirportItlwmRegDiagLinkContextLifecycleInternalAdmitted:
            return "internal-admitted";
        case kAirportItlwmRegDiagLinkContextLifecycleParentAccepted:
            return "parent-accepted";
        case kAirportItlwmRegDiagLinkContextLifecycleParentRejected:
            return "parent-rejected";
        default:
            return "unknown";
    }
}

static const char *
join_abort_phase_name(uint32_t phase)
{
    switch (phase) {
        case kAirportItlwmRegDiagJoinAbortEnter: return "enter";
        case kAirportItlwmRegDiagJoinAbortExit: return "exit";
        default: return "unknown";
    }
}

static uint32_t
next_sequence(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec ^ tv.tv_usec);
}

static CFStringRef
cfstr(const char *s)
{
    return CFStringCreateWithCString(kCFAllocatorDefault, s, kCFStringEncodingUTF8);
}

static io_service_t
find_service(void)
{
    CFMutableDictionaryRef matching = IOServiceMatching("AirportItlwm");
    if (matching == NULL)
        return IO_OBJECT_NULL;

    io_iterator_t iterator = IO_OBJECT_NULL;
    kern_return_t kr = IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator);
    if (kr != KERN_SUCCESS)
        return IO_OBJECT_NULL;

    io_service_t service = IOIteratorNext(iterator);
    IOObjectRelease(iterator);
    return service;
}

static CFTypeRef
copy_property(io_service_t service, const char *name)
{
    CFStringRef key = cfstr(name);
    if (key == NULL)
        return NULL;
    CFTypeRef value = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
    CFRelease(key);
    return value;
}

static int
set_control(io_service_t service, int argc, char **argv)
{
    char control[512];
    uint32_t seq = next_sequence();
    int written = snprintf(control, sizeof(control), "seq=%u", seq);
    if (written < 0 || (size_t)written >= sizeof(control))
        return 2;

    for (int i = 0; i < argc; i += 2) {
        if (i + 1 >= argc) {
            fprintf(stderr, "set expects key value pairs\n");
            return 2;
        }
        written += snprintf(control + written, sizeof(control) - (size_t)written,
                            ";%s=%s", argv[i], argv[i + 1]);
        if (written < 0 || (size_t)written >= sizeof(control)) {
            fprintf(stderr, "control string too long\n");
            return 2;
        }
    }

    CFStringRef key = cfstr(AIRPORT_ITLWM_REGDIAG_CONTROL_PROPERTY);
    CFStringRef value = cfstr(control);
    if (key == NULL || value == NULL) {
        if (key != NULL)
            CFRelease(key);
        if (value != NULL)
            CFRelease(value);
        return 2;
    }

    kern_return_t kr = IORegistryEntrySetCFProperty(service, key, value);
    CFRelease(value);
    CFRelease(key);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "IORegistryEntrySetCFProperty failed: 0x%x\n", kr);
        return 1;
    }

    printf("%s\n", control);
    return 0;
}

static void
print_bytes_ascii(const uint8_t *bytes, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        uint8_t c = bytes[i];
        putchar((c >= 32 && c <= 126) ? c : '.');
    }
}

static void
print_mac(const uint8_t *mac)
{
    printf("%02x:%02x:%02x:%02x:%02x:%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static int
get_control(io_service_t service)
{
    CFTypeRef value = copy_property(service, AIRPORT_ITLWM_REGDIAG_CONTROL_ACK_PROPERTY);
    if (value == NULL) {
        fprintf(stderr, "no control ack property\n");
        return 1;
    }

    if (CFGetTypeID(value) == CFStringGetTypeID()) {
        char buf[256];
        if (CFStringGetCString((CFStringRef)value, buf, sizeof(buf), kCFStringEncodingUTF8))
            printf("%s\n", buf);
    } else {
        fprintf(stderr, "control ack has unexpected type\n");
    }
    CFRelease(value);
    return 0;
}

static int
get_snapshot(io_service_t service)
{
    CFTypeRef value = copy_property(service, AIRPORT_ITLWM_REGDIAG_SNAPSHOT_PROPERTY);
    if (value == NULL) {
        fprintf(stderr, "no snapshot property; enable diagnostics first\n");
        return 1;
    }
    if (CFGetTypeID(value) != CFDataGetTypeID() ||
        CFDataGetLength((CFDataRef)value) <
            (CFIndex)AIRPORT_ITLWM_REGDIAG_SNAPSHOT_V1_SIZE) {
        fprintf(stderr, "snapshot has unexpected type or size\n");
        CFRelease(value);
        return 1;
    }

    const AirportItlwmRegDiagSnapshot *s =
        (const AirportItlwmRegDiagSnapshot *)CFDataGetBytePtr((CFDataRef)value);
    printf("version=%u size=%u seq=%u control_seq=%u\n",
           s->version, s->size, s->sequence, s->lastControlSequence);
    printf("mode=0x%x block=0x%x rt=0x%x/0x%x/0x%x\n",
           s->modeFlags, s->blockMask, s->rtMask, s->rtMask2, s->rtMask3);
    printf("state ic=%d ic_flags=0x%x if_flags=0x%x power=%u pm=%u link=0x%x speed=%" PRIu64 "\n",
           s->icState, s->icFlags, s->ifFlags, s->powerState, s->pmPowerState,
           s->currentStatus, s->currentSpeed);
    printf("objects hal=%u netif=%u bsd=%u bss=%u nodes=%u bsd_name=%s\n",
           s->hasHalService, s->hasNetIf, s->hasBSDInterface, s->hasBss,
           s->nodeCount, s->bsdName);
    printf("desired_ssid_len=%u desired_ssid=", s->desiredEssLen);
    print_bytes_ascii(s->desiredSsid, s->desiredEssLen);
    printf("\ncurrent_ssid_len=%u current_ssid=", s->currentSsidLen);
    print_bytes_ascii(s->currentSsid, s->currentSsidLen);
    printf(" bssid=");
    print_mac(s->currentBssid);
    printf("\nlast_assoc_ssid_len=%u last_assoc_ssid=", s->lastAssocSsidLen);
    print_bytes_ascii(s->lastAssocSsid, s->lastAssocSsidLen);
    printf(" last_assoc_bssid=");
    print_mac(s->lastAssocBssid);
    printf(" auth=0x%x/0x%x rsn_len=%u\n",
           s->lastAssocAuthLower, s->lastAssocAuthUpper, s->lastAssocRsnIeLen);
    printf("counts public_assoc=%u hidden_assoc=%u link=%u tx=%u rx=%u eapol_tx=%u eapol_rx=%u tx_drop=%u rx_drop=%u block=%u\n",
           s->publicAssocCount, s->hiddenAssocCount, s->linkStateCount,
           s->txCount, s->rxCount, s->eapolTxCount, s->eapolRxCount,
           s->txDropCount, s->rxDropCount, s->blockHitCount);
    printf("last_result public_assoc=0x%x hidden_assoc=0x%x link=0x%x tx=0x%x rx=0x%x link_state=%d last_block=0x%x tx_len=%u rx_len=%u\n",
           (uint32_t)s->lastPublicAssocResult,
           (uint32_t)s->lastHiddenAssocResult,
           (uint32_t)s->lastLinkStateResult,
           (uint32_t)s->lastTxResult,
           (uint32_t)s->lastRxResult,
           s->lastLinkState, s->lastBlockMask, s->lastTxLength, s->lastRxLength);
    printf("ptrs fNetIf=0x%" PRIx64 " bsdIf=0x%" PRIx64 " txQ=0x%" PRIx64 " rxQ=0x%" PRIx64 "\n",
           s->fNetIfPtr, s->bsdIfPtr, s->fTxQueuePtr, s->fRxQueuePtr);
    if (CFDataGetLength((CFDataRef)value) >=
        (CFIndex)sizeof(AirportItlwmRegDiagSnapshot)) {
        printf("assoc_policy auth_flags=0x%x candidates=%u pmf=%u flags=0x%x\n",
               s->lastAssocAuthFlags, s->lastAssocCandidateCount,
               s->lastAssocPmfCapability, s->lastAssocPolicyFlags);
        printf("pmk counts ingress=%u reject=%u clear=%u plti_publish=%u/%u plti_deliver=%u/%u\n",
               s->pmkIngressCount, s->pmkIngressRejectCount,
               s->pmkClearCount, s->pltiPublishCount,
               s->pltiPublishRejectCount, s->pltiDeliverCount,
               s->pltiDeliverRejectCount);
        printf("pmk last source=%s decision=%s key_len=%u auth=0x%x generation=%" PRIu64 " clear=%s\n",
               pmk_source_name(s->lastPmkSource),
               pmk_decision_name(s->lastPmkDecision), s->lastPmkKeyLen,
               s->lastPmkAuthUpper, s->lastPmkGeneration,
               pmk_clear_reason_name(s->lastPmkClearReason));
    } else {
        printf("pmk_timeline=unavailable (loaded kext exposes ABI v1 snapshot)\n");
    }

    CFRelease(value);
    return 0;
}

static int
get_trace(io_service_t service)
{
    CFTypeRef value = copy_property(service, AIRPORT_ITLWM_REGDIAG_TRACE_PROPERTY);
    if (value == NULL) {
        fprintf(stderr, "no trace property; enable diagnostics first\n");
        return 1;
    }
    if (CFGetTypeID(value) != CFDataGetTypeID() ||
        CFDataGetLength((CFDataRef)value) < (CFIndex)sizeof(AirportItlwmRegDiagTraceBuffer)) {
        fprintf(stderr, "trace has unexpected type or size\n");
        CFRelease(value);
        return 1;
    }

    const AirportItlwmRegDiagTraceBuffer *t =
        (const AirportItlwmRegDiagTraceBuffer *)CFDataGetBytePtr((CFDataRef)value);
    printf("version=%u count=%u next=%u dropped=%u\n",
           t->version, t->entryCount, t->nextSequence, t->droppedEntries);
    uint32_t count = t->entryCount;
    if (count > AIRPORT_ITLWM_REGDIAG_MAX_TRACE_ENTRIES)
        count = AIRPORT_ITLWM_REGDIAG_MAX_TRACE_ENTRIES;
    uint32_t start = t->nextSequence >= count ? t->nextSequence - count : 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t seq = start + i;
        const AirportItlwmRegDiagTraceEntry *e =
            &t->entries[seq % AIRPORT_ITLWM_REGDIAG_MAX_TRACE_ENTRIES];
        printf("#%u kind=%s path=%s result=0x%x",
               e->sequence, kind_name(e->kind), path_name(e->path),
               (uint32_t)e->result);
        if (e->kind == kAirportItlwmRegDiagTraceAuthPolicy) {
            uint32_t auth_lower = (uint32_t)e->arg1;
            uint32_t auth_upper = (uint32_t)(e->arg1 >> 32);
            uint32_t rsn_len = (uint32_t)(e->arg2 >> 32);
            uint32_t flags = (uint32_t)e->arg2;
            printf(" pmf=%d auth=0x%x/0x%x rsn_len=%u policy=0x%x",
                   e->arg0, auth_lower, auth_upper, rsn_len, flags);
        } else if (e->kind == kAirportItlwmRegDiagTracePmkIngress) {
            printf(" source=%s auth=0x%x key_len=%u decision=%s",
                   pmk_source_name((uint32_t)e->arg0),
                   (uint32_t)(e->arg1 >> 32), (uint32_t)e->arg1,
                   pmk_decision_name((uint32_t)e->arg2));
        } else if (e->kind == kAirportItlwmRegDiagTracePmkClear) {
            printf(" reason=%s", pmk_clear_reason_name((uint32_t)e->arg0));
        } else if (e->kind == kAirportItlwmRegDiagTracePltiPublish ||
                   e->kind == kAirportItlwmRegDiagTracePltiDeliver) {
            printf(" decision=%s generation=%" PRIu64 " auth=0x%x",
                   pmk_decision_name((uint32_t)e->arg0), e->arg1,
                   (uint32_t)e->arg2);
        } else if (e->kind == kAirportItlwmRegDiagTraceTx ||
                   e->kind == kAirportItlwmRegDiagTraceRx) {
            printf(" eapol=%d length=%" PRIu64,
                   e->arg0 != 0 ? 1 : 0, e->arg1);
        } else if (e->kind == kAirportItlwmRegDiagTraceLinkState) {
            printf(" link_state=%d raw_code=%" PRIu64, e->arg0, e->arg1);
            if (e->arg2 ==
                AIRPORT_ITLWM_REGDIAG_LINK_STATE_PARENT_ACCEPTED_UNAVAILABLE) {
                printf(" parent_accepted=n/a");
            } else {
                printf(" parent_accepted=%" PRIu64, e->arg2);
            }
        } else if (e->kind == kAirportItlwmRegDiagTraceLinkStatus) {
            printf(" decision=%s previous=0x%x requested=0x%x",
                   link_status_decision_name((uint32_t)e->arg0),
                   (uint32_t)(e->arg1 >> 32), (uint32_t)e->arg1);
        } else if (e->kind == kAirportItlwmRegDiagTraceLinkPublish) {
            printf(" decision=%s link_state=%" PRIu64 " raw_code=%" PRIu64,
                   link_publish_decision_name((uint32_t)e->arg0),
                   e->arg1, e->arg2);
        } else if (e->kind == kAirportItlwmRegDiagTraceLinkContext) {
            const uint32_t context = (uint32_t)e->arg0;
            const uint32_t route =
                context & AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_ROUTE_MASK;
            const uint32_t stage =
                (context & AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_STAGE_MASK) >>
                AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_STAGE_SHIFT;
            const uint32_t on_thread =
                (context & AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_ON_THREAD_MASK) >>
                AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_ON_THREAD_SHIFT;
            const uint32_t in_gate =
                (context & AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_IN_GATE_MASK) >>
                AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_IN_GATE_SHIFT;
            const uint32_t on_dispatch =
                (context & AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_ON_DISPATCH_MASK) >>
                AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_ON_DISPATCH_SHIFT;
            const uint32_t lifecycle =
                (context & AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_LIFECYCLE_MASK) >>
                AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_LIFECYCLE_SHIFT;
            const uint32_t link_state =
                (context & AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_LINK_STATE_MASK) >>
                AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_LINK_STATE_SHIFT;
            const uint32_t raw_code = (uint32_t)e->arg2;
            const uint32_t controller_status = (uint32_t)(e->arg2 >> 32);
            printf(" route=%s stage=%s epoch=%" PRIu64
                   " link_state=%u raw_code=%u controller_status=",
                   link_context_route_name(route), link_context_stage_name(stage),
                   e->arg1, link_state, raw_code);
            if (controller_status ==
                AIRPORT_ITLWM_REGDIAG_LINK_CONTEXT_STATUS_UNAVAILABLE) {
                printf("n/a");
            } else {
                printf("0x%x", controller_status);
            }
            printf(" lifecycle=%s on_thread=%s in_gate=%s on_dispatch=%s",
                   link_context_lifecycle_name(lifecycle),
                   link_context_predicate_name(on_thread),
                   link_context_predicate_name(in_gate),
                   link_context_predicate_name(on_dispatch));
        } else if (e->kind == kAirportItlwmRegDiagTraceWclJoinAbort) {
            printf(" phase=%s ic_state=%" PRIu64 " request_completion=%" PRIu64,
                   join_abort_phase_name((uint32_t)e->arg0), e->arg1,
                   e->arg2);
        } else {
            printf(" arg0=%d arg1=0x%" PRIx64 " arg2=0x%" PRIx64,
                   e->arg0, e->arg1, e->arg2);
        }
        putchar('\n');
    }

    CFRelease(value);
    return 0;
}

static void
usage(const char *prog)
{
    fprintf(stderr,
            "usage:\n"
            "  %s set enable 1 assoc 1 control 1 data 1 log 0 clear 1\n"
            "  %s set block 0x0 intervention 0\n"
            "  %s get snapshot|trace|control|report\n"
            "  %s on|sae-on|link-context-on|off\n",
            prog, prog, prog, prog);
}

int
main(int argc, char **argv)
{
    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }

    io_service_t service = find_service();
    if (service == IO_OBJECT_NULL) {
        fprintf(stderr, "AirportItlwm service not found\n");
        return 1;
    }

    int rc = 2;
    if (strcmp(argv[1], "set") == 0) {
        rc = set_control(service, argc - 2, argv + 2);
    } else if (strcmp(argv[1], "on") == 0) {
        char *args[] = {
            (char *)"enable", (char *)"1",
            (char *)"assoc", (char *)"1",
            (char *)"control", (char *)"1",
            (char *)"data", (char *)"1",
            (char *)"log", (char *)"0",
            (char *)"context", (char *)"0",
            (char *)"intervention", (char *)"0",
            (char *)"clear", (char *)"1",
        };
        rc = set_control(service, 16, args);
    } else if (strcmp(argv[1], "off") == 0) {
        char *args[] = {
            (char *)"enable", (char *)"0",
            (char *)"context", (char *)"0",
            (char *)"intervention", (char *)"0",
            (char *)"block", (char *)"0",
        };
        rc = set_control(service, 8, args);
    } else if (strcmp(argv[1], "sae-on") == 0) {
        char *args[] = {
            (char *)"enable", (char *)"1",
            (char *)"assoc", (char *)"1",
            (char *)"control", (char *)"1",
            (char *)"pmk", (char *)"1",
            (char *)"data", (char *)"0",
            (char *)"log", (char *)"0",
            (char *)"context", (char *)"0",
            (char *)"intervention", (char *)"0",
            (char *)"block", (char *)"0",
            (char *)"clear", (char *)"1",
        };
        rc = set_control(service, 20, args);
    } else if (strcmp(argv[1], "link-context-on") == 0) {
        char *args[] = {
            (char *)"enable", (char *)"1",
            (char *)"assoc", (char *)"0",
            (char *)"control", (char *)"1",
            (char *)"pmk", (char *)"0",
            (char *)"data", (char *)"0",
            (char *)"log", (char *)"0",
            (char *)"context", (char *)"1",
            (char *)"intervention", (char *)"0",
            (char *)"block", (char *)"0",
            (char *)"clear", (char *)"1",
        };
        rc = set_control(service, 20, args);
    } else if (strcmp(argv[1], "get") == 0 && argc >= 3) {
        if (strcmp(argv[2], "snapshot") == 0)
            rc = get_snapshot(service);
        else if (strcmp(argv[2], "trace") == 0)
            rc = get_trace(service);
        else if (strcmp(argv[2], "control") == 0)
            rc = get_control(service);
        else if (strcmp(argv[2], "report") == 0) {
            rc = get_snapshot(service);
            if (rc == 0)
                rc = get_trace(service);
        }
        else
            usage(argv[0]);
    } else {
        usage(argv[0]);
    }

    IOObjectRelease(service);
    return rc;
}
