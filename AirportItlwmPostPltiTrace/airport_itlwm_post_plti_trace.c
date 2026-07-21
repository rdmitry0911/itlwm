#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <ClientKit/AirportItlwmPostPltiTrace.h>
#include <ClientKit/AirportItlwmPostPltiTraceMatrixContracts.h>

/*
 * This client intentionally reads only the separate safe-only trace
 * properties.  It never requests the unrelated diagnostic surface,
 * IORegistry-wide output, network
 * identity, packet bytes, credentials, firmware status, or pointer values.
 */

static CFStringRef
cfstr(const char *text)
{
    return CFStringCreateWithCString(kCFAllocatorDefault, text,
                                    kCFStringEncodingUTF8);
}

static io_service_t
find_service(void)
{
    CFMutableDictionaryRef match =
        IOServiceMatching("AirportItlwm");
    if (match == NULL)
        return IO_OBJECT_NULL;
    return IOServiceGetMatchingService(kIOMainPortDefault, match);
}

static uint32_t
next_sequence(void)
{
    static uint32_t last = 0;
    struct timeval now;
    gettimeofday(&now, NULL);
    uint32_t candidate = (uint32_t)now.tv_usec ^ (uint32_t)now.tv_sec;
    if (candidate == 0 || candidate <= last)
        candidate = last + 1;
    last = candidate;
    return candidate;
}

static int
set_control(io_service_t service, int enable, int reset, int seal)
{
    char control[112];
    int written = snprintf(control, sizeof(control),
                           "seq=%u;enable=%u;reset=%u;seal=%u", next_sequence(),
                           enable != 0 ? 1U : 0U, reset != 0 ? 1U : 0U,
                           seal != 0 ? 1U : 0U);
    if (written < 0 || (size_t)written >= sizeof(control))
        return 2;

    CFStringRef key =
        cfstr(AIRPORT_ITLWM_POST_PLTI_TRACE_CONTROL_PROPERTY);
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
        fprintf(stderr, "safe trace control write failed: 0x%x\n", kr);
        return 1;
    }
    printf("%s\n", control);
    return 0;
}

static CFTypeRef
copy_property(io_service_t service, const char *name)
{
    CFStringRef key = cfstr(name);
    if (key == NULL)
        return NULL;
    CFTypeRef value = IORegistryEntryCreateCFProperty(service, key,
                                                       kCFAllocatorDefault, 0);
    CFRelease(key);
    return value;
}

static const char *
event_name(uint32_t event)
{
    switch (event) {
    case kAirportItlwmPostPltiTraceEventWclPmkReadyScanResume:
        return "wcl-pmk-ready-scan-resume";
    case kAirportItlwmPostPltiTraceEventIwnScanCoalesced:
        return "iwn-scan-coalesced";
    case kAirportItlwmPostPltiTraceEventIwnScanStarted:
        return "iwn-scan-started";
    case kAirportItlwmPostPltiTraceEventScanCompleted:
        return "scan-completed";
    case kAirportItlwmPostPltiTraceEventSelectionHeld:
        return "selection-held";
    case kAirportItlwmPostPltiTraceEventBssSelected:
        return "bss-selected";
    case kAirportItlwmPostPltiTraceEventJoinBssEntered:
        return "join-bss-entered";
    case kAirportItlwmPostPltiTraceEventAuthStateEntered:
        return "auth-state-entered";
    case kAirportItlwmPostPltiTraceEventAuthEnqueued:
        return "auth-enqueued";
    case kAirportItlwmPostPltiTraceEventAuthDequeued:
        return "auth-dequeued";
    case kAirportItlwmPostPltiTraceEventAuthFwSubmitted:
        return "auth-fw-submitted";
    case kAirportItlwmPostPltiTraceEventAuthTxDone:
        return "auth-txdone";
    case kAirportItlwmPostPltiTraceEventAuthRxFromFirmware:
        return "auth-rx-from-firmware";
    case kAirportItlwmPostPltiTraceEventAuthRxNet80211:
        return "auth-rx-net80211";
    case kAirportItlwmPostPltiTraceEventAssocStateEntered:
        return "assoc-state-entered";
    case kAirportItlwmPostPltiTraceEventAssocEnqueued:
        return "assoc-enqueued";
    case kAirportItlwmPostPltiTraceEventAssocDequeued:
        return "assoc-dequeued";
    case kAirportItlwmPostPltiTraceEventAssocFwSubmitted:
        return "assoc-fw-submitted";
    case kAirportItlwmPostPltiTraceEventAssocTxDone:
        return "assoc-txdone";
    case kAirportItlwmPostPltiTraceEventAssocRxFromFirmware:
        return "assoc-rx-from-firmware";
    case kAirportItlwmPostPltiTraceEventAssocRxNet80211:
        return "assoc-rx-net80211";
    case kAirportItlwmPostPltiTraceEventRunEntered:
        return "run-entered";
    case kAirportItlwmPostPltiTraceEventEapolRxDecapped:
        return "eapol-rx-decapped";
    case kAirportItlwmPostPltiTraceEventEapolRxKernelPae:
        return "eapol-rx-kernel-pae";
    case kAirportItlwmPostPltiTraceEventEapolTxEnqueued:
        return "eapol-tx-enqueued";
    case kAirportItlwmPostPltiTraceEventEapolFwSubmitted:
        return "eapol-fw-submitted";
    case kAirportItlwmPostPltiTraceEventEapolTxDone:
        return "eapol-txdone";
    case kAirportItlwmPostPltiTraceEventPortValidTransition:
        return "port-valid-transition";
    case kAirportItlwmPostPltiTraceEventEpisodeAborted:
        return "episode-aborted";
    case kAirportItlwmPostPltiTraceEventStateScanSelfRequestObserved:
        return "state-scan-self-request-observed";
    case kAirportItlwmPostPltiTraceEventIwnScanStateEntered:
        return "iwn-scan-state-entered";
    case kAirportItlwmPostPltiTraceEventIwnScanCommandRejected:
        return "iwn-scan-command-rejected";
    case kAirportItlwmPostPltiTraceEventScanNoCandidate:
        return "scan-no-candidate";
    case kAirportItlwmPostPltiTraceEventCaptureWindowSealed:
        return "capture-window-sealed";
    default:
        return "unknown";
    }
}

static int
event_known(uint32_t event)
{
    return event > kAirportItlwmPostPltiTraceEventUnknown &&
           event < kAirportItlwmPostPltiTraceEventMax;
}

static int
entry_compare(const void *left, const void *right)
{
    const AirportItlwmPostPltiTraceEntry *a = left;
    const AirportItlwmPostPltiTraceEntry *b = right;
    if (a->sequence < b->sequence)
        return -1;
    if (a->sequence > b->sequence)
        return 1;
    return 0;
}

static int
copy_snapshot(io_service_t service, AirportItlwmPostPltiTraceSnapshot *out)
{
    CFTypeRef value = copy_property(
        service, AIRPORT_ITLWM_POST_PLTI_TRACE_SNAPSHOT_PROPERTY);
    if (value == NULL) {
        fprintf(stderr, "safe trace snapshot unavailable; arm/reset first\n");
        return 1;
    }
    if (CFGetTypeID(value) != CFDataGetTypeID() ||
        CFDataGetLength((CFDataRef)value) <
            (CFIndex)sizeof(AirportItlwmPostPltiTraceSnapshot)) {
        fprintf(stderr, "safe trace snapshot has unexpected type or size\n");
        CFRelease(value);
        return 1;
    }
    memcpy(out, CFDataGetBytePtr((CFDataRef)value), sizeof(*out));
    CFRelease(value);
    if (out->version != AIRPORT_ITLWM_POST_PLTI_TRACE_ABI_VERSION ||
        out->size != sizeof(*out)) {
        fprintf(stderr, "safe trace snapshot ABI mismatch\n");
        return 1;
    }
    return 0;
}

static int
copy_buffer(io_service_t service, AirportItlwmPostPltiTraceBuffer *out)
{
    CFTypeRef value = copy_property(
        service, AIRPORT_ITLWM_POST_PLTI_TRACE_BUFFER_PROPERTY);
    if (value == NULL) {
        fprintf(stderr, "safe trace buffer unavailable; arm/reset first\n");
        return 1;
    }
    if (CFGetTypeID(value) != CFDataGetTypeID() ||
        CFDataGetLength((CFDataRef)value) <
            (CFIndex)sizeof(AirportItlwmPostPltiTraceBuffer)) {
        fprintf(stderr, "safe trace buffer has unexpected type or size\n");
        CFRelease(value);
        return 1;
    }
    memcpy(out, CFDataGetBytePtr((CFDataRef)value), sizeof(*out));
    CFRelease(value);
    if (out->version != AIRPORT_ITLWM_POST_PLTI_TRACE_ABI_VERSION) {
        fprintf(stderr, "safe trace buffer ABI mismatch\n");
        return 1;
    }
    return 0;
}

static int
collect_entries(const AirportItlwmPostPltiTraceSnapshot *snapshot,
                const AirportItlwmPostPltiTraceBuffer *buffer,
                AirportItlwmPostPltiTraceEntry *out, uint32_t *out_count)
{
    uint32_t count = 0;
    uint32_t expected = 0;
    int integrity = 1;

    if (snapshot->captureGeneration == 0 ||
        snapshot->captureGeneration != buffer->captureGeneration ||
        snapshot->backend != buffer->backend ||
        snapshot->firstSequence != buffer->firstSequence ||
        snapshot->latestSequence != buffer->latestSequence ||
        snapshot->droppedEntries != 0 || buffer->droppedEntries != 0 ||
        snapshot->entryCount > AIRPORT_ITLWM_POST_PLTI_TRACE_MAX_ENTRIES ||
        buffer->entryCount > AIRPORT_ITLWM_POST_PLTI_TRACE_MAX_ENTRIES)
        integrity = 0;

    if (integrity) {
        if (snapshot->latestSequence + 1U == snapshot->firstSequence) {
            expected = 0;
        } else if (snapshot->latestSequence < snapshot->firstSequence ||
                   (uint64_t)snapshot->latestSequence -
                       snapshot->firstSequence + 1U >
                       AIRPORT_ITLWM_POST_PLTI_TRACE_MAX_ENTRIES) {
            integrity = 0;
        } else {
            expected = snapshot->latestSequence - snapshot->firstSequence + 1U;
        }
    }

    for (uint32_t i = 0; i < AIRPORT_ITLWM_POST_PLTI_TRACE_MAX_ENTRIES; i++) {
        const AirportItlwmPostPltiTraceEntry *entry = &buffer->entries[i];
        if (entry->sequence == 0)
            continue;
        if (entry->sequence < snapshot->firstSequence ||
            entry->sequence > snapshot->latestSequence)
            continue;
        if (entry->captureGeneration != snapshot->captureGeneration ||
            entry->episode == 0 || !event_known(entry->event)) {
            integrity = 0;
            continue;
        }
        out[count++] = *entry;
    }
    qsort(out, count, sizeof(*out), entry_compare);
    if (count != expected || count != snapshot->entryCount ||
        count != buffer->entryCount)
        integrity = 0;
    if (count != 0 &&
        (out[0].sequence != snapshot->firstSequence ||
         out[count - 1].sequence != snapshot->latestSequence))
        integrity = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (out[i].sequence != snapshot->firstSequence + i) {
            integrity = 0;
            break;
        }
    }
    *out_count = count;
    return integrity;
}

static const char *
backend_name(uint32_t backend)
{
    switch (backend) {
    case kAirportItlwmPostPltiTraceBackendIwn: return "IWN";
    case kAirportItlwmPostPltiTraceBackendUnsupported: return "UNSUPPORTED";
    default: return "UNKNOWN";
    }
}

static const char *
verdict_name(enum AirportItlwmPostPltiTraceMatrixVerdict verdict)
{
    switch (verdict) {
    case kAirportItlwmPostPltiTraceMatrixVerdictIntegrityInconclusive:
        return "INTEGRITY_INCONCLUSIVE";
    case kAirportItlwmPostPltiTraceMatrixVerdictBackendUnsupported:
        return "BACKEND_UNSUPPORTED";
    case kAirportItlwmPostPltiTraceMatrixVerdictBranchNotObserved:
        return "BRANCH_NOT_OBSERVED";
    case kAirportItlwmPostPltiTraceMatrixVerdictResumeNoStateRequest:
        return "RESUME_NO_STATE_REQUEST";
    case kAirportItlwmPostPltiTraceMatrixVerdictResumeNoIwnDispatch:
        return "RESUME_NO_IWN_DISPATCH";
    case kAirportItlwmPostPltiTraceMatrixVerdictScanCommandRejected:
        return "SCAN_COMMAND_REJECTED";
    case kAirportItlwmPostPltiTraceMatrixVerdictScanIncomplete:
        return "SCAN_INCOMPLETE";
    case kAirportItlwmPostPltiTraceMatrixVerdictScanNoCandidate:
        return "SCAN_NO_CANDIDATE";
    case kAirportItlwmPostPltiTraceMatrixVerdictResumeNoSelection:
        return "RESUME_NO_SELECTION";
    case kAirportItlwmPostPltiTraceMatrixVerdictAuthNotDrained:
        return "AUTH_NOT_DRAINED";
    case kAirportItlwmPostPltiTraceMatrixVerdictTxNoCompletion:
        return "TX_NO_COMPLETION";
    case kAirportItlwmPostPltiTraceMatrixVerdictNoEapol:
        return "NO_EAPOL";
    case kAirportItlwmPostPltiTraceMatrixVerdictKernelChainObserved:
        return "KERNEL_CHAIN_OBSERVED";
    }
    return "INTEGRITY_INCONCLUSIVE";
}

static const char *
missing_stage_name(enum AirportItlwmPostPltiTraceMissingStage stage)
{
    switch (stage) {
    case kAirportItlwmPostPltiTraceMissingStageNone: return "none";
    case kAirportItlwmPostPltiTraceMissingStageStateScanSelfRequest:
        return "state-scan-self-request";
    case kAirportItlwmPostPltiTraceMissingStageIwnScanState:
        return "iwn-scan-state";
    case kAirportItlwmPostPltiTraceMissingStageIwnScanCommand:
        return "iwn-scan-command";
    case kAirportItlwmPostPltiTraceMissingStageScanCompletion:
        return "scan-completion";
    case kAirportItlwmPostPltiTraceMissingStageBssSelection:
        return "bss-selection";
    case kAirportItlwmPostPltiTraceMissingStageJoinBss:
        return "join-bss";
    case kAirportItlwmPostPltiTraceMissingStageAuthState:
        return "auth-state";
    case kAirportItlwmPostPltiTraceMissingStageAuthEnqueue:
        return "auth-enqueue";
    case kAirportItlwmPostPltiTraceMissingStageAuthDequeue:
        return "auth-dequeue";
    case kAirportItlwmPostPltiTraceMissingStageAuthFirmwareSubmit:
        return "auth-firmware-submit";
    case kAirportItlwmPostPltiTraceMissingStageAuthExchange:
        return "auth-exchange";
    case kAirportItlwmPostPltiTraceMissingStageAssocState:
        return "assoc-state";
    case kAirportItlwmPostPltiTraceMissingStageAssocEnqueue:
        return "assoc-enqueue";
    case kAirportItlwmPostPltiTraceMissingStageAssocDequeue:
        return "assoc-dequeue";
    case kAirportItlwmPostPltiTraceMissingStageAssocFirmwareSubmit:
        return "assoc-firmware-submit";
    case kAirportItlwmPostPltiTraceMissingStageAssocExchange:
        return "assoc-exchange";
    case kAirportItlwmPostPltiTraceMissingStageRunState:
        return "run-state";
    case kAirportItlwmPostPltiTraceMissingStageEapolDecapped:
        return "eapol-decapped";
    case kAirportItlwmPostPltiTraceMissingStageEapolKernelPae:
        return "eapol-kernel-pae";
    case kAirportItlwmPostPltiTraceMissingStageEapolEnqueue:
        return "eapol-enqueue";
    case kAirportItlwmPostPltiTraceMissingStagePortValid:
        return "port-valid";
    default: return "unknown";
    }
}

static int
get_control(io_service_t service)
{
    CFTypeRef value = copy_property(
        service, AIRPORT_ITLWM_POST_PLTI_TRACE_CONTROL_ACK_PROPERTY);
    if (value == NULL) {
        fprintf(stderr, "safe trace control acknowledgement unavailable\n");
        return 1;
    }
    if (CFGetTypeID(value) != CFStringGetTypeID()) {
        fprintf(stderr, "safe trace control acknowledgement has unexpected type\n");
        CFRelease(value);
        return 1;
    }
    char text[128];
    int ok = CFStringGetCString((CFStringRef)value, text, sizeof(text),
                                kCFStringEncodingUTF8);
    CFRelease(value);
    if (!ok) {
        fprintf(stderr, "safe trace control acknowledgement is not renderable\n");
        return 1;
    }
    printf("%s\n", text);
    return 0;
}

static int
get_snapshot(io_service_t service)
{
    AirportItlwmPostPltiTraceSnapshot snapshot;
    if (copy_snapshot(service, &snapshot) != 0)
        return 1;
    printf("version=%u capture_generation=%u backend=%s enabled=%u target_bound=%u "
           "active_episode=%u episode_count=%u entry_count=%u "
           "dropped=%u first_sequence=%u latest_sequence=%u\n",
           snapshot.version, snapshot.captureGeneration,
           backend_name(snapshot.backend), snapshot.enabled,
           snapshot.targetBound, snapshot.activeEpisode, snapshot.episodeCount,
           snapshot.entryCount, snapshot.droppedEntries, snapshot.firstSequence,
           snapshot.latestSequence);
    return 0;
}

static int
get_trace(io_service_t service)
{
    AirportItlwmPostPltiTraceSnapshot snapshot;
    AirportItlwmPostPltiTraceBuffer buffer;
    AirportItlwmPostPltiTraceEntry entries[
        AIRPORT_ITLWM_POST_PLTI_TRACE_MAX_ENTRIES];
    uint32_t count = 0;
    if (copy_snapshot(service, &snapshot) != 0 ||
        copy_buffer(service, &buffer) != 0)
        return 1;
    int integrity = collect_entries(&snapshot, &buffer, entries, &count);
    printf("integrity=%s capture_generation=%u backend=%s entries=%u dropped=%u\n",
           integrity ? "ok" : "inconclusive", snapshot.captureGeneration,
           backend_name(snapshot.backend), count, snapshot.droppedEntries);
    for (uint32_t i = 0; i < count; i++) {
        printf("#%u episode=%u event=%s\n", entries[i].sequence,
               entries[i].episode, event_name(entries[i].event));
    }
    return integrity ? 0 : 1;
}

static int
get_report(io_service_t service)
{
    AirportItlwmPostPltiTraceSnapshot snapshot;
    AirportItlwmPostPltiTraceBuffer buffer;
    AirportItlwmPostPltiTraceEntry entries[
        AIRPORT_ITLWM_POST_PLTI_TRACE_MAX_ENTRIES];
    uint32_t count = 0;
    if (copy_snapshot(service, &snapshot) != 0 ||
        copy_buffer(service, &buffer) != 0)
        return 1;
    int integrity = collect_entries(&snapshot, &buffer, entries, &count);
    enum AirportItlwmPostPltiTraceMissingStage missing_stage =
        kAirportItlwmPostPltiTraceMissingStageUnknown;
    enum AirportItlwmPostPltiTraceMatrixVerdict verdict =
        airport_itlwm_post_plti_trace_matrix_classify_entries_with_stage(
            entries, count, integrity, snapshot.backend,
            snapshot.episodeCount, snapshot.activeEpisode, &missing_stage);
    printf("capture_generation=%u backend=%s entries=%u integrity=%s "
           "episode_count=%u active_episode=%u\n",
           snapshot.captureGeneration, backend_name(snapshot.backend), count,
           integrity ? "ok" : "inconclusive", snapshot.episodeCount,
           snapshot.activeEpisode);
    printf("verdict=%s first_missing_stage=%s\n", verdict_name(verdict),
           missing_stage_name(missing_stage));
    return 0;
}

static void
usage(const char *program)
{
    fprintf(stderr,
            "usage:\n"
            "  %s reset|on|off|seal\n"
            "  %s get control|snapshot|trace|report\n",
            program, program);
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
    if (strcmp(argv[1], "reset") == 0)
        rc = set_control(service, 1, 1, 0);
    else if (strcmp(argv[1], "on") == 0)
        rc = set_control(service, 1, 0, 0);
    else if (strcmp(argv[1], "off") == 0)
        rc = set_control(service, 0, 0, 0);
    else if (strcmp(argv[1], "seal") == 0)
        rc = set_control(service, 0, 0, 1);
    else if (strcmp(argv[1], "get") == 0 && argc == 3) {
        if (strcmp(argv[2], "control") == 0)
            rc = get_control(service);
        else if (strcmp(argv[2], "snapshot") == 0)
            rc = get_snapshot(service);
        else if (strcmp(argv[2], "trace") == 0)
            rc = get_trace(service);
        else if (strcmp(argv[2], "report") == 0)
            rc = get_report(service);
        else
            usage(argv[0]);
    } else
        usage(argv[0]);

    IOObjectRelease(service);
    return rc;
}
