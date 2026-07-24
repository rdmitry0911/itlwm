#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <ClientKit/AirportItlwmIwnSoftwarePmfTraceContracts.h>

struct fixture {
    AirportItlwmPostPltiTraceEntry entries[64];
    uint32_t count;
};

static void
require(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "IWN software-PMF C fixture failed: %s\n", message);
        exit(1);
    }
}

static void
append(struct fixture *fixture, uint32_t event)
{
    const uint32_t index = fixture->count;

    require(index < (uint32_t)(sizeof(fixture->entries) /
        sizeof(fixture->entries[0])), "fixture capacity");
    fixture->entries[index].sequence = 900 + index;
    fixture->entries[index].captureGeneration = 43;
    fixture->entries[index].episode = 5;
    fixture->entries[index].event = event;
    fixture->count++;
}

static void
begin(struct fixture *fixture)
{
    fixture->count = 0;
    append(fixture, kAirportItlwmPostPltiTraceEventWclPmkReadyScanResume);
    append(fixture,
        kAirportItlwmPostPltiTraceEventStateScanSelfRequestObserved);
}

static void
append_observed_generic_wpa2_chain(struct fixture *fixture)
{
    append(fixture, kAirportItlwmPostPltiTraceEventIwnScanStateEntered);
    append(fixture, kAirportItlwmPostPltiTraceEventIwnScanCoalesced);
    append(fixture, kAirportItlwmPostPltiTraceEventIwnScanStarted);
    append(fixture, kAirportItlwmPostPltiTraceEventScanCompleted);
    append(fixture, kAirportItlwmPostPltiTraceEventBssSelected);
    append(fixture, kAirportItlwmPostPltiTraceEventJoinBssEntered);
    append(fixture, kAirportItlwmPostPltiTraceEventAuthStateEntered);
    append(fixture, kAirportItlwmPostPltiTraceEventAuthEnqueued);
    append(fixture, kAirportItlwmPostPltiTraceEventAuthDequeued);
    append(fixture, kAirportItlwmPostPltiTraceEventAuthFwSubmitted);
    append(fixture, kAirportItlwmPostPltiTraceEventAuthTxDone);
    append(fixture, kAirportItlwmPostPltiTraceEventAuthRxFromFirmware);
    append(fixture, kAirportItlwmPostPltiTraceEventAuthRxNet80211);
    append(fixture, kAirportItlwmPostPltiTraceEventAssocStateEntered);
    append(fixture, kAirportItlwmPostPltiTraceEventAssocEnqueued);
    append(fixture, kAirportItlwmPostPltiTraceEventAssocDequeued);
    append(fixture, kAirportItlwmPostPltiTraceEventAssocFwSubmitted);
    append(fixture, kAirportItlwmPostPltiTraceEventAssocTxDone);
    append(fixture, kAirportItlwmPostPltiTraceEventAssocRxFromFirmware);
    append(fixture, kAirportItlwmPostPltiTraceEventAssocRxNet80211);
    append(fixture, kAirportItlwmPostPltiTraceEventRunEntered);
    append(fixture, kAirportItlwmPostPltiTraceEventEapolRxDecapped);
    append(fixture, kAirportItlwmPostPltiTraceEventEapolRxKernelPae);
    append(fixture, kAirportItlwmPostPltiTraceEventEapolTxEnqueued);
    append(fixture, kAirportItlwmPostPltiTraceEventEapolFwSubmitted);
    append(fixture, kAirportItlwmPostPltiTraceEventEapolTxDone);
    append(fixture, kAirportItlwmPostPltiTraceEventEapolRxDecapped);
    append(fixture, kAirportItlwmPostPltiTraceEventEapolRxKernelPae);
    append(fixture, kAirportItlwmPostPltiTraceEventEapolTxEnqueued);
    append(fixture, kAirportItlwmPostPltiTraceEventEapolFwSubmitted);
    append(fixture, kAirportItlwmPostPltiTraceEventPortValidTransition);
}

static void
append_complete_initial_transaction(struct fixture *fixture)
{
    append(fixture,
        kAirportItlwmPostPltiTraceEventIwnMfpPaePtkSoftwarePrepared);
    append(fixture, kAirportItlwmPostPltiTraceEventEapolTxEnqueued);
    append(fixture,
        kAirportItlwmPostPltiTraceEventIwnMfpPaeGtkSoftwarePrepared);
    append(fixture,
        kAirportItlwmPostPltiTraceEventIwnMfpPaeIgtkStageAcknowledged);
    append(fixture,
        kAirportItlwmPostPltiTraceEventIwnIgtkSlot4Published);
    append(fixture,
        kAirportItlwmPostPltiTraceEventIwnIgtkSlot4TxSelected);
    append(fixture,
        kAirportItlwmPostPltiTraceEventIwnMfpPaeSoftwareCcmpBipPublished);
    append(fixture, kAirportItlwmPostPltiTraceEventPortValidTransition);
}

static void
expect(const struct fixture *fixture, int integrity, uint32_t backend,
    uint32_t active_episode, enum AirportItlwmIwnSoftwarePmfTraceVerdict verdict,
    enum AirportItlwmIwnSoftwarePmfTraceMissingStage stage, const char *message)
{
    enum AirportItlwmIwnSoftwarePmfTraceMissingStage actual_stage =
        kAirportItlwmIwnSoftwarePmfTraceMissingStageUnknown;
    const enum AirportItlwmIwnSoftwarePmfTraceVerdict actual =
        airport_itlwm_iwn_software_pmf_trace_classify_entries_with_stage(
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
    append_complete_initial_transaction(&fixture);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 0,
        kAirportItlwmIwnSoftwarePmfTraceVerdictInitialSoftwarePmfObserved,
        kAirportItlwmIwnSoftwarePmfTraceMissingStageNone,
        "one closed initial transaction proves software PMF ownership");

    begin(&fixture);
    append_observed_generic_wpa2_chain(&fixture);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 0,
        kAirportItlwmIwnSoftwarePmfTraceVerdictPtkSoftwareCcmpNotObserved,
        kAirportItlwmIwnSoftwarePmfTraceMissingStagePtkSoftwareCcmp,
        "the observed generic WPA2 chain reports absent software PMF at PTK");

    begin(&fixture);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventWclPmfRequestRetained);
    append(&fixture, kAirportItlwmPostPltiTraceEventNodeMfpNegotiated);
    append_complete_initial_transaction(&fixture);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 0,
        kAirportItlwmIwnSoftwarePmfTraceVerdictInitialSoftwarePmfObserved,
        kAirportItlwmIwnSoftwarePmfTraceMissingStageNone,
        "PMF ingress categories cannot perturb the software-PMF owner verdict");

    begin(&fixture);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventIwnMfpPaePtkSoftwarePrepared);
    append(&fixture, kAirportItlwmPostPltiTraceEventPortValidTransition);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 0,
        kAirportItlwmIwnSoftwarePmfTraceVerdictIntegrityInconclusive,
        kAirportItlwmIwnSoftwarePmfTraceMissingStageUnknown,
        "a partial IWN software-PMF branch cannot close at port-valid");

    begin(&fixture);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventIwnMfpPaePtkSoftwarePrepared);
    append(&fixture, kAirportItlwmPostPltiTraceEventCaptureWindowSealed);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 0,
        kAirportItlwmIwnSoftwarePmfTraceVerdictGtkSoftwareCcmpNotObserved,
        kAirportItlwmIwnSoftwarePmfTraceMissingStageGtkSoftwareCcmp,
        "a sealed partial transaction cannot skip software GTK preparation");

    begin(&fixture);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventIwnMfpPaeGtkSoftwarePrepared);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 0,
        kAirportItlwmIwnSoftwarePmfTraceVerdictIntegrityInconclusive,
        kAirportItlwmIwnSoftwarePmfTraceMissingStageUnknown,
        "software GTK cannot precede the software PTK stage");

    begin(&fixture);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventIwnMfpPaePtkSoftwarePrepared);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventIwnMfpPaeIgtkStageAcknowledged);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 0,
        kAirportItlwmIwnSoftwarePmfTraceVerdictIntegrityInconclusive,
        kAirportItlwmIwnSoftwarePmfTraceMissingStageUnknown,
        "the IGTK acknowledgement cannot precede software GTK preparation");

    begin(&fixture);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventIwnMfpPaePtkSoftwarePrepared);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventIwnMfpPaeGtkSoftwarePrepared);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventIwnMfpPaeIgtkStageAcknowledged);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventIwnIgtkSlot4Published);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventIwnIgtkSlot5TxSelected);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 0,
        kAirportItlwmIwnSoftwarePmfTraceVerdictIntegrityInconclusive,
        kAirportItlwmIwnSoftwarePmfTraceMissingStageUnknown,
        "a selected IGTK slot must match its categorical publication");

    begin(&fixture);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventIwnMfpPaePtkSoftwarePrepared);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventIwnMfpPaeGtkSoftwarePrepared);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventIwnMfpPaeIgtkStageAcknowledged);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventIwnMfpPaeSoftwareCcmpBipPublished);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 0,
        kAirportItlwmIwnSoftwarePmfTraceVerdictIntegrityInconclusive,
        kAirportItlwmIwnSoftwarePmfTraceMissingStageUnknown,
        "final publication cannot precede an active IGTK slot");

    begin(&fixture);
    append_complete_initial_transaction(&fixture);
    append(&fixture, kAirportItlwmPostPltiTraceEventCaptureWindowSealed);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 0,
        kAirportItlwmIwnSoftwarePmfTraceVerdictIntegrityInconclusive,
        kAirportItlwmIwnSoftwarePmfTraceMissingStageUnknown,
        "a record after the close-on-port-valid terminal is rejected");

    begin(&fixture);
    append_complete_initial_transaction(&fixture);
    fixture.entries[4].captureGeneration++;
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 0,
        kAirportItlwmIwnSoftwarePmfTraceVerdictIntegrityInconclusive,
        kAirportItlwmIwnSoftwarePmfTraceMissingStageUnknown,
        "a mixed capture generation is never a PMF verdict");
    fixture.entries[4].captureGeneration--;
    expect(&fixture, 0, kAirportItlwmPostPltiTraceBackendIwn, 0,
        kAirportItlwmIwnSoftwarePmfTraceVerdictIntegrityInconclusive,
        kAirportItlwmIwnSoftwarePmfTraceMissingStageUnknown,
        "a caller-detected drop or overflow is fail-closed");

    begin(&fixture);
    append_complete_initial_transaction(&fixture);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwx, 0,
        kAirportItlwmIwnSoftwarePmfTraceVerdictBackendUnsupported,
        kAirportItlwmIwnSoftwarePmfTraceMissingStageUnknown,
        "the IWX backend cannot borrow the IWN software-PMF evaluator");

    puts("IWN software-PMF C trace fixtures ok");
    return 0;
}
