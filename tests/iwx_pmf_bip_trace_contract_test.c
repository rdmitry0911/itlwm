#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <ClientKit/AirportItlwmIwxPmfBipTraceContracts.h>

struct fixture {
    AirportItlwmPostPltiTraceEntry entries[32];
    uint32_t count;
};

static void
require(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "IWX PMF/BIP C fixture failed: %s\n", message);
        exit(1);
    }
}

static void
append(struct fixture *fixture, uint32_t event)
{
    const uint32_t index = fixture->count;

    require(index < (uint32_t)(sizeof(fixture->entries) /
        sizeof(fixture->entries[0])), "fixture capacity");
    fixture->entries[index].sequence = 500 + index;
    fixture->entries[index].captureGeneration = 71;
    fixture->entries[index].episode = 9;
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
append_q0_igtk(struct fixture *fixture, uint32_t publication,
    uint32_t selection)
{
    append(fixture, kAirportItlwmPostPltiTraceEventIwxMfpPaeRxDelivered);
    append(fixture,
        kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0Doorbelled);
    append(fixture,
        kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0CompletionObserved);
    append(fixture, publication);
    append(fixture, selection);
}

static void
append_initial_mfp_msg3_chain(struct fixture *fixture, uint32_t publication,
    uint32_t selection)
{
    append(fixture, kAirportItlwmPostPltiTraceEventIwxMfpPaeRxDelivered);
    for (uint32_t stage = 0; stage < 3; stage++) {
        append(fixture,
            kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0Doorbelled);
        append(fixture,
            kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0CompletionObserved);
    }
    append(fixture, publication);
    append(fixture, selection);
}

static void
append_initial_mfp_m1_msg3_chain(struct fixture *fixture, uint32_t publication,
    uint32_t selection)
{
    /* The IWX worker categorically records every MFP EAPOL-Key delivery.
     * Four-way Msg1 is therefore visible before Msg3 begins the PMF Q0
     * transaction. */
    append(fixture, kAirportItlwmPostPltiTraceEventIwxMfpPaeRxDelivered);
    append_initial_mfp_msg3_chain(fixture, publication, selection);
}

static enum AirportItlwmIwxPmfBipTraceVerdict
classify(const struct fixture *fixture, int integrity, uint32_t backend,
    uint32_t active_episode,
    enum AirportItlwmIwxPmfBipTraceMissingStage *out_stage)
{
    return airport_itlwm_iwx_pmf_bip_trace_classify_entries_with_stage(
        fixture->entries, fixture->count, integrity, backend, 1,
        active_episode, out_stage);
}

static enum AirportItlwmIwxPmfBipTraceInitialProgress
classify_initial_progress(const struct fixture *fixture, int integrity,
    uint32_t backend, uint32_t active_episode,
    enum AirportItlwmIwxPmfBipTraceMissingStage *out_stage)
{
    return airport_itlwm_iwx_pmf_bip_trace_classify_initial_prefix_with_stage(
        fixture->entries, fixture->count, integrity, backend, 1,
        active_episode, out_stage);
}

static void
expect(const struct fixture *fixture, int integrity, uint32_t backend,
    uint32_t active_episode, enum AirportItlwmIwxPmfBipTraceVerdict verdict,
    enum AirportItlwmIwxPmfBipTraceMissingStage stage, const char *message)
{
    enum AirportItlwmIwxPmfBipTraceMissingStage actual_stage =
        kAirportItlwmIwxPmfBipTraceMissingStageUnknown;
    const enum AirportItlwmIwxPmfBipTraceVerdict actual = classify(
        fixture, integrity, backend, active_episode, &actual_stage);

    require(actual == verdict, message);
    require(actual_stage == stage, "fixture reports deterministic stage");
}

static void
expect_initial_progress(const struct fixture *fixture, int integrity,
    uint32_t backend, uint32_t active_episode,
    enum AirportItlwmIwxPmfBipTraceInitialProgress progress,
    enum AirportItlwmIwxPmfBipTraceMissingStage stage, const char *message)
{
    enum AirportItlwmIwxPmfBipTraceMissingStage actual_stage =
        kAirportItlwmIwxPmfBipTraceMissingStageUnknown;
    const enum AirportItlwmIwxPmfBipTraceInitialProgress actual =
        classify_initial_progress(fixture, integrity, backend, active_episode,
            &actual_stage);

    require(actual == progress, message);
    require(actual_stage == stage,
        "initial progress reports deterministic stage");
}

int
main(void)
{
    struct fixture fixture = { 0 };

    begin(&fixture);
    append_q0_igtk(&fixture,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot4Published,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot4TxSelected);
    append(&fixture, kAirportItlwmPostPltiTraceEventPortValidTransition);
    append(&fixture, kAirportItlwmPostPltiTraceEventCaptureWindowSealed);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwx, 0,
        kAirportItlwmIwxPmfBipTraceVerdictInitialPmfBipObserved,
        kAirportItlwmIwxPmfBipTraceMissingStageNone,
        "initial slot-4 PMF transaction reaches a sealed initial verdict");

    begin(&fixture);
    append_initial_mfp_msg3_chain(&fixture,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot4Published,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot4TxSelected);
    append(&fixture, kAirportItlwmPostPltiTraceEventPortValidTransition);
    expect_initial_progress(&fixture, 1,
        kAirportItlwmPostPltiTraceBackendIwx, 9,
        kAirportItlwmIwxPmfBipTraceInitialProgressInitialPmfBipReady,
        kAirportItlwmIwxPmfBipTraceMissingStageNone,
        "one PMF Msg3 with PTK/GTK/IGTK q0 stages authorizes the active prefix");
    append(&fixture, kAirportItlwmPostPltiTraceEventCaptureWindowSealed);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwx, 0,
        kAirportItlwmIwxPmfBipTraceVerdictInitialPmfBipObserved,
        kAirportItlwmIwxPmfBipTraceMissingStageNone,
        "the three-stage initial MFP transaction reaches a sealed initial verdict");

    begin(&fixture);
    append_initial_mfp_m1_msg3_chain(&fixture,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot4Published,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot4TxSelected);
    append(&fixture, kAirportItlwmPostPltiTraceEventPortValidTransition);
    expect_initial_progress(&fixture, 1,
        kAirportItlwmPostPltiTraceBackendIwx, 9,
        kAirportItlwmIwxPmfBipTraceInitialProgressInitialPmfBipReady,
        kAirportItlwmIwxPmfBipTraceMissingStageNone,
        "MFP Msg1 then Msg3 with PTK/GTK/IGTK q0 stages authorizes the active prefix");
    append(&fixture, kAirportItlwmPostPltiTraceEventCaptureWindowSealed);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwx, 0,
        kAirportItlwmIwxPmfBipTraceVerdictInitialPmfBipObserved,
        kAirportItlwmIwxPmfBipTraceMissingStageNone,
        "the Msg1 then Msg3 initial PMF transaction reaches a sealed initial verdict");

    begin(&fixture);
    append(&fixture, kAirportItlwmPostPltiTraceEventIwxMfpPaeRxDelivered);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0Doorbelled);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0CompletionObserved);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0Doorbelled);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0Doorbelled);
    expect_initial_progress(&fixture, 1,
        kAirportItlwmPostPltiTraceBackendIwx, 9,
        kAirportItlwmIwxPmfBipTraceInitialProgressIntegrityInconclusive,
        kAirportItlwmIwxPmfBipTraceMissingStageQ0Completion,
        "a repeated q0 doorbell still requires the preceding completion");

    begin(&fixture);
    append(&fixture, kAirportItlwmPostPltiTraceEventIwxMfpPaeRxDelivered);
    append(&fixture, kAirportItlwmPostPltiTraceEventIwxMfpPaeRxDelivered);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0Doorbelled);
    append(&fixture, kAirportItlwmPostPltiTraceEventIwxMfpPaeRxDelivered);
    expect_initial_progress(&fixture, 1,
        kAirportItlwmPostPltiTraceBackendIwx, 9,
        kAirportItlwmIwxPmfBipTraceInitialProgressIntegrityInconclusive,
        kAirportItlwmIwxPmfBipTraceMissingStageQ0Completion,
        "an MFP RX while q0 completion is outstanding remains inconclusive");

    begin(&fixture);
    append_q0_igtk(&fixture,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot4Published,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot4TxSelected);
    append(&fixture, kAirportItlwmPostPltiTraceEventPortValidTransition);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwx, 9,
        kAirportItlwmIwxPmfBipTraceVerdictIntegrityInconclusive,
        kAirportItlwmIwxPmfBipTraceMissingStageCaptureSeal,
        "a still-active IWX episode is explicitly missing its capture seal");
    expect_initial_progress(&fixture, 1,
        kAirportItlwmPostPltiTraceBackendIwx, 9,
        kAirportItlwmIwxPmfBipTraceInitialProgressInitialPmfBipReady,
        kAirportItlwmIwxPmfBipTraceMissingStageNone,
        "only the exact active initial PMF chain authorizes a later rekey");

    begin(&fixture);
    append_q0_igtk(&fixture,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot4Published,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot4TxSelected);
    append(&fixture, kAirportItlwmPostPltiTraceEventPortValidTransition);
    append_q0_igtk(&fixture,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot5Published,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot5TxSelected);
    append(&fixture, kAirportItlwmPostPltiTraceEventCaptureWindowSealed);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwx, 0,
        kAirportItlwmIwxPmfBipTraceVerdictCrossSlotRekeyObserved,
        kAirportItlwmIwxPmfBipTraceMissingStageNone,
        "slot-4 initial followed by slot-5 rekey remains distinct");

    begin(&fixture);
    append_q0_igtk(&fixture,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot5Published,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot5TxSelected);
    append(&fixture, kAirportItlwmPostPltiTraceEventPortValidTransition);
    append_q0_igtk(&fixture,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot4Published,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot4TxSelected);
    append(&fixture, kAirportItlwmPostPltiTraceEventCaptureWindowSealed);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwx, 0,
        kAirportItlwmIwxPmfBipTraceVerdictCrossSlotRekeyObserved,
        kAirportItlwmIwxPmfBipTraceMissingStageNone,
        "slot-5 initial followed by slot-4 rekey remains distinct");

    begin(&fixture);
    append_q0_igtk(&fixture,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot4Published,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot4TxSelected);
    append(&fixture, kAirportItlwmPostPltiTraceEventPortValidTransition);
    append_q0_igtk(&fixture,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot5Published,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot5TxSelected);
    append_q0_igtk(&fixture,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot4Published,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot4TxSelected);
    append(&fixture, kAirportItlwmPostPltiTraceEventCaptureWindowSealed);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwx, 0,
        kAirportItlwmIwxPmfBipTraceVerdictIntegrityInconclusive,
        kAirportItlwmIwxPmfBipTraceMissingStageCrossSlotRekey,
        "more than one cross-slot rekey is not attributed to one bounded request");

    begin(&fixture);
    append(&fixture, kAirportItlwmPostPltiTraceEventIwxMfpPaeRxDelivered);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0Doorbelled);
    append(&fixture, kAirportItlwmPostPltiTraceEventCaptureWindowSealed);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwx, 0,
        kAirportItlwmIwxPmfBipTraceVerdictQ0CompletionNotObserved,
        kAirportItlwmIwxPmfBipTraceMissingStageQ0Completion,
        "a missing q0 completion cannot publish PMF ownership");
    fixture.count--;
    expect_initial_progress(&fixture, 1,
        kAirportItlwmPostPltiTraceBackendIwx, 9,
        kAirportItlwmIwxPmfBipTraceInitialProgressIntegrityInconclusive,
        kAirportItlwmIwxPmfBipTraceMissingStageQ0Completion,
        "initial progress rejects a q0 chain that has not completed");

    begin(&fixture);
    append(&fixture, kAirportItlwmPostPltiTraceEventIwxIgtkSlot4Published);
    append(&fixture, kAirportItlwmPostPltiTraceEventCaptureWindowSealed);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwx, 0,
        kAirportItlwmIwxPmfBipTraceVerdictIntegrityInconclusive,
        kAirportItlwmIwxPmfBipTraceMissingStagePmfRx,
        "a publication without a PMF owner sequence is rejected");

    begin(&fixture);
    append(&fixture, kAirportItlwmPostPltiTraceEventIwxMfpPaeRxDelivered);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0Doorbelled);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0CompletionObserved);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot4TxSelected);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwx, 0,
        kAirportItlwmIwxPmfBipTraceVerdictIntegrityInconclusive,
        kAirportItlwmIwxPmfBipTraceMissingStageIgtkPublication,
        "an active-slot fact before publication is rejected");

    begin(&fixture);
    append_q0_igtk(&fixture,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot4Published,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot4TxSelected);
    append(&fixture, kAirportItlwmPostPltiTraceEventPortValidTransition);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot4TxSelected);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwx, 0,
        kAirportItlwmIwxPmfBipTraceVerdictIntegrityInconclusive,
        kAirportItlwmIwxPmfBipTraceMissingStageCrossSlotRekey,
        "repeated active-slot selection without a rekey is rejected");

    begin(&fixture);
    append_q0_igtk(&fixture,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot4Published,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot4TxSelected);
    append(&fixture, kAirportItlwmPostPltiTraceEventPortValidTransition);
    append_q0_igtk(&fixture,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot4Published,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot4TxSelected);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwx, 0,
        kAirportItlwmIwxPmfBipTraceVerdictIntegrityInconclusive,
        kAirportItlwmIwxPmfBipTraceMissingStageCrossSlotRekey,
        "same-slot replacement is not a cross-slot rekey");

    begin(&fixture);
    append_q0_igtk(&fixture,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot4Published,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot4TxSelected);
    append(&fixture, kAirportItlwmPostPltiTraceEventPortValidTransition);
    append(&fixture, kAirportItlwmPostPltiTraceEventCaptureWindowSealed);
    fixture.entries[3].episode++;
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwx, 0,
        kAirportItlwmIwxPmfBipTraceVerdictIntegrityInconclusive,
        kAirportItlwmIwxPmfBipTraceMissingStageUnknown,
        "a mixed episode is never a PMF/BIP verdict");
    fixture.entries[3].episode--;
    fixture.entries[3].captureGeneration++;
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwx, 0,
        kAirportItlwmIwxPmfBipTraceVerdictIntegrityInconclusive,
        kAirportItlwmIwxPmfBipTraceMissingStageUnknown,
        "a mixed generation is never a PMF/BIP verdict");
    fixture.entries[3].captureGeneration--;
    expect(&fixture, 0, kAirportItlwmPostPltiTraceBackendIwx, 0,
        kAirportItlwmIwxPmfBipTraceVerdictIntegrityInconclusive,
        kAirportItlwmIwxPmfBipTraceMissingStageUnknown,
        "a caller-detected drop or overflow is fail-closed");

    append(&fixture,
        kAirportItlwmPostPltiTraceEventStateScanSelfRequestObserved);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwx, 0,
        kAirportItlwmIwxPmfBipTraceVerdictIntegrityInconclusive,
        kAirportItlwmIwxPmfBipTraceMissingStageUnknown,
        "an event after the terminal seal is rejected");

    begin(&fixture);
    append(&fixture, kAirportItlwmPostPltiTraceEventIwxMfpPaeRxDelivered);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0Doorbelled);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0CompletionObserved);
    append(&fixture, kAirportItlwmPostPltiTraceEventEpisodeAborted);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwx, 0,
        kAirportItlwmIwxPmfBipTraceVerdictIntegrityInconclusive,
        kAirportItlwmIwxPmfBipTraceMissingStageIgtkPublication,
        "a cancellation or detach before final publication is rejected");

    begin(&fixture);
    append_q0_igtk(&fixture,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot4Published,
        kAirportItlwmPostPltiTraceEventIwxIgtkSlot4TxSelected);
    append(&fixture, kAirportItlwmPostPltiTraceEventPortValidTransition);
    append(&fixture, kAirportItlwmPostPltiTraceEventCaptureWindowSealed);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 0,
        kAirportItlwmIwxPmfBipTraceVerdictBackendUnsupported,
        kAirportItlwmIwxPmfBipTraceMissingStageUnknown,
        "the IWN backend cannot borrow the IWX evaluator");
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendUnknown, 0,
        kAirportItlwmIwxPmfBipTraceVerdictBackendUnsupported,
        kAirportItlwmIwxPmfBipTraceMissingStageUnknown,
        "an unknown backend cannot borrow the IWX evaluator");

    puts("IWX PMF/BIP C trace fixtures ok");
    return 0;
}
