#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <ClientKit/AirportItlwmWclPhysicalScanTraceContracts.h>

struct fixture {
    AirportItlwmPostPltiTraceEntry entries[32];
    uint32_t count;
};

static void
require(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "WCL physical scan C fixture failed: %s\n", message);
        exit(1);
    }
}

static void
append_for_episode(struct fixture *fixture, uint32_t episode, uint32_t event)
{
    const uint32_t index = fixture->count;

    require(index < (uint32_t)(sizeof(fixture->entries) /
        sizeof(fixture->entries[0])), "fixture capacity");
    fixture->entries[index].sequence = 9100 + index;
    fixture->entries[index].captureGeneration = 27;
    fixture->entries[index].episode = episode;
    fixture->entries[index].event = event;
    fixture->count++;
}

static void
append(struct fixture *fixture, uint32_t event)
{
    append_for_episode(fixture, 1, event);
}

static void
begin(struct fixture *fixture)
{
    fixture->count = 0;
    append(fixture,
        kAirportItlwmPostPltiTraceEventWclPhysicalScanRequestAccepted);
}

static void
append_lower_lease(struct fixture *fixture)
{
    append(fixture,
        kAirportItlwmPostPltiTraceEventWclPhysicalScanLowerLeaseReserved);
}

static void
append_terminal_complete(struct fixture *fixture)
{
    append(fixture,
        kAirportItlwmPostPltiTraceEventWclPhysicalScanTerminalComplete);
}

static void
append_done(struct fixture *fixture)
{
    append(fixture,
        kAirportItlwmPostPltiTraceEventWclPhysicalScanDonePublicationIssued);
}

static void
append_complete_episode(struct fixture *fixture, uint32_t episode,
    int include_result)
{
    append_for_episode(fixture, episode,
        kAirportItlwmPostPltiTraceEventWclPhysicalScanRequestAccepted);
    append_for_episode(fixture, episode,
        kAirportItlwmPostPltiTraceEventWclPhysicalScanLowerLeaseReserved);
    append_for_episode(fixture, episode,
        kAirportItlwmPostPltiTraceEventIwnScanStarted);
    append_for_episode(fixture, episode,
        kAirportItlwmPostPltiTraceEventWclPhysicalScanTerminalComplete);
    if (include_result) {
        append_for_episode(fixture, episode,
            kAirportItlwmPostPltiTraceEventWclPhysicalScanResultPublicationIssued);
    }
    append_for_episode(fixture, episode,
        kAirportItlwmPostPltiTraceEventWclPhysicalScanDonePublicationIssued);
}

static void
expect(const struct fixture *fixture, int integrity, uint32_t backend,
    uint32_t episode_count, uint32_t active_episode,
    enum AirportItlwmWclPhysicalScanTraceVerdict verdict,
    enum AirportItlwmWclPhysicalScanTraceMissingStage stage,
    const char *message)
{
    enum AirportItlwmWclPhysicalScanTraceMissingStage actual_stage =
        kAirportItlwmWclPhysicalScanTraceMissingStageUnknown;
    const enum AirportItlwmWclPhysicalScanTraceVerdict actual =
        airport_itlwm_wcl_physical_scan_trace_classify_entries_with_stage(
            fixture->entries, fixture->count, integrity, backend,
            episode_count, active_episode, &actual_stage);

    require(actual == verdict, message);
    require(actual_stage == stage, "fixture reports deterministic stage");
}

int
main(void)
{
    struct fixture fixture = { 0 };

    begin(&fixture);
    append_lower_lease(&fixture);
    append(&fixture, kAirportItlwmPostPltiTraceEventIwnScanStarted);
    append_terminal_complete(&fixture);
    append(&fixture, kAirportItlwmPostPltiTraceEventScanCompleted);
    append(&fixture, kAirportItlwmPostPltiTraceEventSelectionHeld);
    append_done(&fixture);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 1, 0,
        kAirportItlwmWclPhysicalScanTraceVerdictPhysicalScanObserved,
        kAirportItlwmWclPhysicalScanTraceMissingStageNone,
        "an empty physical scan reaches the exact DONE publication boundary");
    require(!airport_itlwm_wcl_physical_scan_trace_result_publication_issued(
                fixture.entries, fixture.count),
            "an empty scan does not infer result publication");

    fixture.count = 0;
    append_complete_episode(&fixture, 1, 0);
    append_complete_episode(&fixture, 2, 1);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 2, 0,
        kAirportItlwmWclPhysicalScanTraceVerdictPhysicalScanObserved,
        kAirportItlwmWclPhysicalScanTraceMissingStageNone,
        "two sequential complete WCL passes from one public scan are observed");
    require(airport_itlwm_wcl_physical_scan_trace_result_publication_issued(
                fixture.entries, fixture.count),
            "a bounded aggregate retains the result-publication fact");

    fixture.count = 0;
    append_complete_episode(&fixture, 1, 0);
    append_for_episode(&fixture, 2,
        kAirportItlwmPostPltiTraceEventWclPhysicalScanRequestAccepted);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 2, 0,
        kAirportItlwmWclPhysicalScanTraceVerdictLowerLeaseNotObserved,
        kAirportItlwmWclPhysicalScanTraceMissingStageLowerLease,
        "a complete first pass cannot hide an incomplete second pass");

    fixture.count = 0;
    append_complete_episode(&fixture, 1, 0);
    append_for_episode(&fixture, 3,
        kAirportItlwmPostPltiTraceEventWclPhysicalScanRequestAccepted);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 2, 0,
        kAirportItlwmWclPhysicalScanTraceVerdictIntegrityInconclusive,
        kAirportItlwmWclPhysicalScanTraceMissingStageUnknown,
        "a skipped second episode identifier is fail-closed");

    fixture.count = 0;
    append_for_episode(&fixture, 1,
        kAirportItlwmPostPltiTraceEventWclPhysicalScanRequestAccepted);
    append_for_episode(&fixture, 1,
        kAirportItlwmPostPltiTraceEventWclPhysicalScanLowerLeaseReserved);
    append_for_episode(&fixture, 1,
        kAirportItlwmPostPltiTraceEventWclPhysicalScanTerminalAborted);
    append_for_episode(&fixture, 2,
        kAirportItlwmPostPltiTraceEventWclPhysicalScanRequestAccepted);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 2, 0,
        kAirportItlwmWclPhysicalScanTraceVerdictIntegrityInconclusive,
        kAirportItlwmWclPhysicalScanTraceMissingStageUnknown,
        "a later pass cannot repair an aborted earlier pass");

    fixture.count = 0;
    append_complete_episode(&fixture, 1, 0);
    append_complete_episode(&fixture, 2, 0);
    append_complete_episode(&fixture, 3, 0);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 3, 0,
        kAirportItlwmWclPhysicalScanTraceVerdictIntegrityInconclusive,
        kAirportItlwmWclPhysicalScanTraceMissingStageUnknown,
        "more than two WCL passes are outside the bounded public-scan proof");

    begin(&fixture);
    append_lower_lease(&fixture);
    append_terminal_complete(&fixture);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventWclPhysicalScanResultPublicationIssued);
    append_done(&fixture);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 1, 0,
        kAirportItlwmWclPhysicalScanTraceVerdictPhysicalScanObserved,
        kAirportItlwmWclPhysicalScanTraceMissingStageNone,
        "one issued result publication remains optional positive evidence");
    require(airport_itlwm_wcl_physical_scan_trace_result_publication_issued(
                fixture.entries, fixture.count),
            "the result publication marker is a boolean fact");

    begin(&fixture);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 1, 0,
        kAirportItlwmWclPhysicalScanTraceVerdictLowerLeaseNotObserved,
        kAirportItlwmWclPhysicalScanTraceMissingStageLowerLease,
        "an upper admission cannot infer a lower lease");

    /* START_REJECTED is deliberately not a new trace event: a queued
     * no-doorbell handoff seals as request-only evidence, with no lower
     * lease, terminal, result, or DONE fact to infer. */
    begin(&fixture);
    append(&fixture, kAirportItlwmPostPltiTraceEventCaptureWindowSealed);
    require(fixture.count == 2,
        "queued start rejection adds no WCL trace vocabulary");
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 1, 0,
        kAirportItlwmWclPhysicalScanTraceVerdictLowerLeaseNotObserved,
        kAirportItlwmWclPhysicalScanTraceMissingStageLowerLease,
        "a sealed queued rejection remains lower-lease-not-observed");
    require(!airport_itlwm_wcl_physical_scan_trace_result_publication_issued(
                fixture.entries, fixture.count),
            "a queued rejection cannot infer a result publication");

    begin(&fixture);
    append_lower_lease(&fixture);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 1, 0,
        kAirportItlwmWclPhysicalScanTraceVerdictTerminalNotObserved,
        kAirportItlwmWclPhysicalScanTraceMissingStageTerminal,
        "a lower lease cannot infer a terminal");

    begin(&fixture);
    append_lower_lease(&fixture);
    append_terminal_complete(&fixture);
    append(&fixture, kAirportItlwmPostPltiTraceEventCaptureWindowSealed);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 1, 0,
        kAirportItlwmWclPhysicalScanTraceVerdictDoneNotPublished,
        kAirportItlwmWclPhysicalScanTraceMissingStageDone,
        "a lower terminal cannot infer a DONE publication call");

    begin(&fixture);
    append_lower_lease(&fixture);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventWclPhysicalScanTerminalAborted);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 1, 0,
        kAirportItlwmWclPhysicalScanTraceVerdictTerminalAborted,
        kAirportItlwmWclPhysicalScanTraceMissingStageTerminal,
        "a claimed aborted lower terminal is a negative outcome");

    begin(&fixture);
    append_lower_lease(&fixture);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventWclPhysicalScanResultPublicationIssued);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 1, 0,
        kAirportItlwmWclPhysicalScanTraceVerdictIntegrityInconclusive,
        kAirportItlwmWclPhysicalScanTraceMissingStageUnknown,
        "a result publication cannot precede a lower terminal");

    begin(&fixture);
    append_lower_lease(&fixture);
    append_terminal_complete(&fixture);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventWclPhysicalScanResultPublicationIssued);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventWclPhysicalScanResultPublicationIssued);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 1, 0,
        kAirportItlwmWclPhysicalScanTraceVerdictIntegrityInconclusive,
        kAirportItlwmWclPhysicalScanTraceMissingStageUnknown,
        "a categorical result marker is single-shot");

    begin(&fixture);
    append_lower_lease(&fixture);
    append_terminal_complete(&fixture);
    append_done(&fixture);
    append(&fixture, kAirportItlwmPostPltiTraceEventScanCompleted);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 1, 0,
        kAirportItlwmWclPhysicalScanTraceVerdictIntegrityInconclusive,
        kAirportItlwmWclPhysicalScanTraceMissingStageUnknown,
        "no event may append after the closed DONE boundary");

    begin(&fixture);
    append_lower_lease(&fixture);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventWclPhysicalScanTerminalAborted);
    append(&fixture, kAirportItlwmPostPltiTraceEventCaptureWindowSealed);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 1, 0,
        kAirportItlwmWclPhysicalScanTraceVerdictTerminalAborted,
        kAirportItlwmWclPhysicalScanTraceMissingStageTerminal,
        "a seal may follow a closed negative terminal");

    begin(&fixture);
    append_lower_lease(&fixture);
    append_terminal_complete(&fixture);
    append_done(&fixture);
    fixture.entries[2].captureGeneration++;
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 1, 0,
        kAirportItlwmWclPhysicalScanTraceVerdictIntegrityInconclusive,
        kAirportItlwmWclPhysicalScanTraceMissingStageUnknown,
        "mixed capture generations are fail-closed");

    begin(&fixture);
    append_lower_lease(&fixture);
    append_terminal_complete(&fixture);
    append_done(&fixture);
    fixture.entries[2].sequence++;
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 1, 0,
        kAirportItlwmWclPhysicalScanTraceVerdictIntegrityInconclusive,
        kAirportItlwmWclPhysicalScanTraceMissingStageUnknown,
        "a sequence gap is fail-closed");

    begin(&fixture);
    append_lower_lease(&fixture);
    append_terminal_complete(&fixture);
    append_done(&fixture);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 1, 1,
        kAirportItlwmWclPhysicalScanTraceVerdictIntegrityInconclusive,
        kAirportItlwmWclPhysicalScanTraceMissingStageUnknown,
        "an active episode cannot be treated as sealed evidence");
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwx, 1, 0,
        kAirportItlwmWclPhysicalScanTraceVerdictBackendUnsupported,
        kAirportItlwmWclPhysicalScanTraceMissingStageUnknown,
        "IWX cannot borrow IWN physical-scan evidence");
    expect(&fixture, 0, kAirportItlwmPostPltiTraceBackendIwn, 1, 0,
        kAirportItlwmWclPhysicalScanTraceVerdictIntegrityInconclusive,
        kAirportItlwmWclPhysicalScanTraceMissingStageUnknown,
        "dropped categorical evidence is fail-closed");

    fixture.count = 0;
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 0, 0,
        kAirportItlwmWclPhysicalScanTraceVerdictBranchNotObserved,
        kAirportItlwmWclPhysicalScanTraceMissingStageRequest,
        "an empty capture is an honest branch-not-observed result");

    puts("WCL physical scan C trace fixtures ok");
    return 0;
}
