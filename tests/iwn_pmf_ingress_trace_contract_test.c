#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <ClientKit/AirportItlwmIwnPmfIngressTraceContracts.h>

struct fixture {
    AirportItlwmPostPltiTraceEntry entries[64];
    uint32_t count;
};

static void
require(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "IWN PMF ingress C fixture failed: %s\n", message);
        exit(1);
    }
}

static void
append(struct fixture *fixture, uint32_t event)
{
    const uint32_t index = fixture->count;

    require(index < (uint32_t)(sizeof(fixture->entries) /
        sizeof(fixture->entries[0])), "fixture capacity");
    fixture->entries[index].sequence = 1200 + index;
    fixture->entries[index].captureGeneration = 57;
    fixture->entries[index].episode = 9;
    fixture->entries[index].event = event;
    fixture->count++;
}

static void
begin(struct fixture *fixture)
{
    fixture->count = 0;
    append(fixture, kAirportItlwmPostPltiTraceEventWclPmkReadyScanResume);
}

static void
append_join_prefix(struct fixture *fixture)
{
    append(fixture, kAirportItlwmPostPltiTraceEventStateScanSelfRequestObserved);
    append(fixture, kAirportItlwmPostPltiTraceEventIwnScanStateEntered);
    append(fixture, kAirportItlwmPostPltiTraceEventIwnScanStarted);
    append(fixture, kAirportItlwmPostPltiTraceEventScanCompleted);
    append(fixture, kAirportItlwmPostPltiTraceEventBssSelected);
    append(fixture, kAirportItlwmPostPltiTraceEventJoinBssEntered);
}

static void
expect(const struct fixture *fixture, int integrity, uint32_t backend,
    uint32_t active_episode, enum AirportItlwmIwnPmfIngressTraceVerdict verdict,
    enum AirportItlwmIwnPmfIngressTraceMissingStage stage, const char *message)
{
    enum AirportItlwmIwnPmfIngressTraceMissingStage actual_stage =
        kAirportItlwmIwnPmfIngressTraceMissingStageUnknown;
    const enum AirportItlwmIwnPmfIngressTraceVerdict actual =
        airport_itlwm_iwn_pmf_ingress_trace_classify_entries_with_stage(
            fixture->entries, fixture->count, integrity, backend, 1,
            active_episode, &actual_stage);

    require(actual == verdict, message);
    require(actual_stage == stage, "fixture reports deterministic stage");
}

int
main(void)
{
    struct fixture fixture = { 0 };

    begin(&fixture);
    append(&fixture, kAirportItlwmPostPltiTraceEventWclPmfRequestRetained);
    append_join_prefix(&fixture);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventNodeMfpNegotiated);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventIwnMfpPaePtkSoftwarePrepared);
    append(&fixture, kAirportItlwmPostPltiTraceEventCaptureWindowSealed);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 0,
        kAirportItlwmIwnPmfIngressTraceVerdictNodeMfpNegotiated,
        kAirportItlwmIwnPmfIngressTraceMissingStageNone,
        "one sealed WCL-to-node MFP boundary is observed");

    begin(&fixture);
    append_join_prefix(&fixture);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventNodeMfpNegotiated);
    append(&fixture, kAirportItlwmPostPltiTraceEventCaptureWindowSealed);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 0,
        kAirportItlwmIwnPmfIngressTraceVerdictIntegrityInconclusive,
        kAirportItlwmIwnPmfIngressTraceMissingStageUnknown,
        "a node-MFP fact cannot exist without the retained WCL request");

    begin(&fixture);
    append_join_prefix(&fixture);
    append(&fixture, kAirportItlwmPostPltiTraceEventCaptureWindowSealed);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 0,
        kAirportItlwmIwnPmfIngressTraceVerdictWclPmfRequestNotObserved,
        kAirportItlwmIwnPmfIngressTraceMissingStageWclPmfRequest,
        "a sealed ordinary association preserves the missing WCL boundary");

    begin(&fixture);
    append(&fixture, kAirportItlwmPostPltiTraceEventWclPmfRequestRetained);
    append(&fixture, kAirportItlwmPostPltiTraceEventStateScanSelfRequestObserved);
    append(&fixture, kAirportItlwmPostPltiTraceEventIwnScanStateEntered);
    append(&fixture, kAirportItlwmPostPltiTraceEventIwnScanStarted);
    append(&fixture, kAirportItlwmPostPltiTraceEventScanCompleted);
    append(&fixture, kAirportItlwmPostPltiTraceEventScanNoCandidate);
    append(&fixture, kAirportItlwmPostPltiTraceEventCaptureWindowSealed);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 0,
        kAirportItlwmIwnPmfIngressTraceVerdictBssSelectionNotObserved,
        kAirportItlwmIwnPmfIngressTraceMissingStageBssSelection,
        "a retained request cannot infer an MFP result without a BSS");

    begin(&fixture);
    append(&fixture, kAirportItlwmPostPltiTraceEventWclPmfRequestRetained);
    append(&fixture, kAirportItlwmPostPltiTraceEventStateScanSelfRequestObserved);
    append(&fixture, kAirportItlwmPostPltiTraceEventIwnScanStateEntered);
    append(&fixture, kAirportItlwmPostPltiTraceEventIwnScanStarted);
    append(&fixture, kAirportItlwmPostPltiTraceEventScanCompleted);
    append(&fixture, kAirportItlwmPostPltiTraceEventBssSelected);
    append(&fixture, kAirportItlwmPostPltiTraceEventCaptureWindowSealed);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 0,
        kAirportItlwmIwnPmfIngressTraceVerdictJoinBssNotObserved,
        kAirportItlwmIwnPmfIngressTraceMissingStageJoinBss,
        "a selected BSS without join cannot infer negotiated MFP");

    begin(&fixture);
    append(&fixture, kAirportItlwmPostPltiTraceEventWclPmfRequestRetained);
    append_join_prefix(&fixture);
    append(&fixture, kAirportItlwmPostPltiTraceEventCaptureWindowSealed);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 0,
        kAirportItlwmIwnPmfIngressTraceVerdictNodeMfpNotNegotiated,
        kAirportItlwmIwnPmfIngressTraceMissingStageNodeMfp,
        "join without the immediate node-MFP fact stays a negative result");

    begin(&fixture);
    append(&fixture, kAirportItlwmPostPltiTraceEventWclPmfRequestRetained);
    append_join_prefix(&fixture);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventNodeMfpNegotiated);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 0,
        kAirportItlwmIwnPmfIngressTraceVerdictIntegrityInconclusive,
        kAirportItlwmIwnPmfIngressTraceMissingStageCaptureSeal,
        "an unsealed closed episode cannot establish PMF ingress");

    begin(&fixture);
    append(&fixture, kAirportItlwmPostPltiTraceEventWclPmfRequestRetained);
    append_join_prefix(&fixture);
    append(&fixture, kAirportItlwmPostPltiTraceEventAuthStateEntered);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventNodeMfpNegotiated);
    append(&fixture, kAirportItlwmPostPltiTraceEventCaptureWindowSealed);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 0,
        kAirportItlwmIwnPmfIngressTraceVerdictIntegrityInconclusive,
        kAirportItlwmIwnPmfIngressTraceMissingStageUnknown,
        "node-MFP must be recorded at the choose-RSN join boundary");

    begin(&fixture);
    append(&fixture, kAirportItlwmPostPltiTraceEventWclPmfRequestRetained);
    append(&fixture, kAirportItlwmPostPltiTraceEventStateScanSelfRequestObserved);
    append(&fixture, kAirportItlwmPostPltiTraceEventIwnScanStateEntered);
    append(&fixture, kAirportItlwmPostPltiTraceEventIwnScanStarted);
    append(&fixture, kAirportItlwmPostPltiTraceEventScanCompleted);
    append(&fixture, kAirportItlwmPostPltiTraceEventBssSelected);
    append(&fixture, kAirportItlwmPostPltiTraceEventBssSelected);
    append(&fixture, kAirportItlwmPostPltiTraceEventCaptureWindowSealed);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 0,
        kAirportItlwmIwnPmfIngressTraceVerdictIntegrityInconclusive,
        kAirportItlwmIwnPmfIngressTraceMissingStageUnknown,
        "repeated BSS selection cannot manufacture a PMF ingress verdict");

    begin(&fixture);
    append(&fixture, kAirportItlwmPostPltiTraceEventWclPmfRequestRetained);
    append_join_prefix(&fixture);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventNodeMfpNegotiated);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 9,
        kAirportItlwmIwnPmfIngressTraceVerdictIntegrityInconclusive,
        kAirportItlwmIwnPmfIngressTraceMissingStageUnknown,
        "an unsealed active episode cannot establish PMF ingress");

    begin(&fixture);
    append(&fixture, kAirportItlwmPostPltiTraceEventWclPmfRequestRetained);
    append_join_prefix(&fixture);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventNodeMfpNegotiated);
    append(&fixture, kAirportItlwmPostPltiTraceEventCaptureWindowSealed);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwx, 0,
        kAirportItlwmIwnPmfIngressTraceVerdictBackendUnsupported,
        kAirportItlwmIwnPmfIngressTraceMissingStageUnknown,
        "IWX cannot borrow the IWN PMF ingress evaluator");

    begin(&fixture);
    append(&fixture, kAirportItlwmPostPltiTraceEventWclPmfRequestRetained);
    append_join_prefix(&fixture);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventNodeMfpNegotiated);
    append(&fixture, kAirportItlwmPostPltiTraceEventCaptureWindowSealed);
    fixture.entries[4].captureGeneration++;
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 0,
        kAirportItlwmIwnPmfIngressTraceVerdictIntegrityInconclusive,
        kAirportItlwmIwnPmfIngressTraceMissingStageUnknown,
        "mixed generations are never an ingress verdict");
    fixture.entries[4].captureGeneration--;
    expect(&fixture, 0, kAirportItlwmPostPltiTraceBackendIwn, 0,
        kAirportItlwmIwnPmfIngressTraceVerdictIntegrityInconclusive,
        kAirportItlwmIwnPmfIngressTraceMissingStageUnknown,
        "a caller-detected drop or overflow is fail-closed");

    puts("IWN PMF ingress C trace fixtures ok");
    return 0;
}
