#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <ClientKit/AirportItlwmIwnDirectSaeTraceContracts.h>

struct fixture {
    AirportItlwmPostPltiTraceEntry entries[96];
    uint32_t count;
};

static void
require(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "IWN direct SAE C fixture failed: %s\n", message);
        exit(1);
    }
}

static void
append(struct fixture *fixture, uint32_t event)
{
    const uint32_t index = fixture->count;

    require(index < (uint32_t)(sizeof(fixture->entries) /
        sizeof(fixture->entries[0])), "fixture capacity");
    fixture->entries[index].sequence = 7100 + index;
    fixture->entries[index].captureGeneration = 19;
    fixture->entries[index].episode = 3;
    fixture->entries[index].event = event;
    fixture->count++;
}

static void
begin(struct fixture *fixture)
{
    fixture->count = 0;
    append(fixture,
        kAirportItlwmPostPltiTraceEventIwnDirectSaeRequestAccepted);
}

static void
append_to_peer_confirm(struct fixture *fixture, int with_node_mfp)
{
    append(fixture, kAirportItlwmPostPltiTraceEventIwnScanStateEntered);
    append(fixture, kAirportItlwmPostPltiTraceEventIwnScanStarted);
    append(fixture, kAirportItlwmPostPltiTraceEventScanCompleted);
    append(fixture, kAirportItlwmPostPltiTraceEventBssSelected);
    append(fixture, kAirportItlwmPostPltiTraceEventJoinBssEntered);
    if (with_node_mfp)
        append(fixture, kAirportItlwmPostPltiTraceEventNodeMfpNegotiated);
    append(fixture, kAirportItlwmPostPltiTraceEventAuthStateEntered);
    append(fixture,
        kAirportItlwmPostPltiTraceEventIwnDirectSaeCommitTxComplete);
    append(fixture,
        kAirportItlwmPostPltiTraceEventIwnDirectSaePeerCommitAccepted);
    append(fixture,
        kAirportItlwmPostPltiTraceEventIwnDirectSaeConfirmTxComplete);
    append(fixture,
        kAirportItlwmPostPltiTraceEventIwnDirectSaePeerConfirmValidated);
}

static void
append_after_peer_confirm(struct fixture *fixture, int with_complete_pmf)
{
    append(fixture, kAirportItlwmPostPltiTraceEventIwnDirectSaePmkClaimed);
    append(fixture, kAirportItlwmPostPltiTraceEventAssocStateEntered);
    append(fixture, kAirportItlwmPostPltiTraceEventAssocEnqueued);
    append(fixture, kAirportItlwmPostPltiTraceEventAssocDequeued);
    append(fixture,
        kAirportItlwmPostPltiTraceEventIwnDirectSaeAssocDescriptorAccepted);
    append(fixture, kAirportItlwmPostPltiTraceEventAssocFwSubmitted);
    append(fixture, kAirportItlwmPostPltiTraceEventAssocTxDone);
    append(fixture, kAirportItlwmPostPltiTraceEventAssocRxFromFirmware);
    append(fixture, kAirportItlwmPostPltiTraceEventAssocRxNet80211);
    append(fixture, kAirportItlwmPostPltiTraceEventRunEntered);
    append(fixture, kAirportItlwmPostPltiTraceEventEapolRxDecapped);
    append(fixture, kAirportItlwmPostPltiTraceEventEapolRxKernelPae);
    append(fixture, kAirportItlwmPostPltiTraceEventEapolTxEnqueued);
    append(fixture, kAirportItlwmPostPltiTraceEventEapolRxDecapped);
    append(fixture, kAirportItlwmPostPltiTraceEventEapolRxKernelPae);
    append(fixture, kAirportItlwmPostPltiTraceEventEapolTxEnqueued);
    if (with_complete_pmf) {
        append(fixture,
            kAirportItlwmPostPltiTraceEventIwnMfpPaePtkSoftwarePrepared);
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
    }
    append(fixture, kAirportItlwmPostPltiTraceEventPortValidTransition);
}

static void
append_full_success(struct fixture *fixture)
{
    append_to_peer_confirm(fixture, 1);
    append_after_peer_confirm(fixture, 1);
}

static void
expect(const struct fixture *fixture, int integrity, uint32_t backend,
    uint32_t episode_count, uint32_t active_episode,
    enum AirportItlwmIwnDirectSaeTraceVerdict verdict,
    enum AirportItlwmIwnDirectSaeTraceMissingStage stage,
    const char *message)
{
    enum AirportItlwmIwnDirectSaeTraceMissingStage actual_stage =
        kAirportItlwmIwnDirectSaeTraceMissingStageUnknown;
    const enum AirportItlwmIwnDirectSaeTraceVerdict actual =
        airport_itlwm_iwn_direct_sae_trace_classify_entries_with_stage(
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
    append_full_success(&fixture);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 1, 0,
        kAirportItlwmIwnDirectSaeTraceVerdictDirectSae4WayPortValid,
        kAirportItlwmIwnDirectSaeTraceMissingStageNone,
        "one direct IWN SAE four-way chain reaches port valid");

    begin(&fixture);
    append(&fixture, kAirportItlwmPostPltiTraceEventIwnScanStarted);
    append(&fixture, kAirportItlwmPostPltiTraceEventBssSelected);
    append(&fixture, kAirportItlwmPostPltiTraceEventJoinBssEntered);
    append(&fixture, kAirportItlwmPostPltiTraceEventNodeMfpNegotiated);
    append(&fixture, kAirportItlwmPostPltiTraceEventAuthStateEntered);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventIwnDirectSaeCommitTxComplete);
    /* An anti-clogging reply prepares one retry Commit; it is not evidence
     * of a peer Commit, so the trace can contain a second completed Commit
     * before the one successful peer Commit boundary. */
    append(&fixture,
        kAirportItlwmPostPltiTraceEventIwnDirectSaeCommitTxComplete);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventIwnDirectSaePeerCommitAccepted);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventIwnDirectSaeConfirmTxComplete);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventIwnDirectSaePeerConfirmValidated);
    append_after_peer_confirm(&fixture, 1);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 1, 0,
        kAirportItlwmIwnDirectSaeTraceVerdictDirectSae4WayPortValid,
        kAirportItlwmIwnDirectSaeTraceMissingStageNone,
        "an anti-clogging Commit retry cannot manufacture peer-Commit evidence");

    begin(&fixture);
    append(&fixture, kAirportItlwmPostPltiTraceEventIwnScanStarted);
    append(&fixture, kAirportItlwmPostPltiTraceEventBssSelected);
    append(&fixture, kAirportItlwmPostPltiTraceEventJoinBssEntered);
    append(&fixture, kAirportItlwmPostPltiTraceEventCaptureWindowSealed);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 1, 0,
        kAirportItlwmIwnDirectSaeTraceVerdictNodeMfpNotNegotiated,
        kAirportItlwmIwnDirectSaeTraceMissingStageNodeMfp,
        "the selected-BSS PMF boundary precedes AUTH in a sealed prefix");

    begin(&fixture);
    append(&fixture, kAirportItlwmPostPltiTraceEventBssSelected);
    append(&fixture, kAirportItlwmPostPltiTraceEventJoinBssEntered);
    append(&fixture, kAirportItlwmPostPltiTraceEventAuthStateEntered);
    append(&fixture, kAirportItlwmPostPltiTraceEventNodeMfpNegotiated);
    append(&fixture, kAirportItlwmPostPltiTraceEventCaptureWindowSealed);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 1, 0,
        kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive,
        kAirportItlwmIwnDirectSaeTraceMissingStageUnknown,
        "late node-MFP evidence cannot repair an AUTH-order violation");

    begin(&fixture);
    append(&fixture, kAirportItlwmPostPltiTraceEventIwnScanCoalesced);
    append(&fixture, kAirportItlwmPostPltiTraceEventCaptureWindowSealed);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 1, 0,
        kAirportItlwmIwnDirectSaeTraceVerdictFreshScanNotObserved,
        kAirportItlwmIwnDirectSaeTraceMissingStageFreshScan,
        "a coalesced scan cannot satisfy the direct fresh-scan boundary");

    begin(&fixture);
    append(&fixture, kAirportItlwmPostPltiTraceEventBssSelected);
    append(&fixture, kAirportItlwmPostPltiTraceEventIwnScanStarted);
    append(&fixture, kAirportItlwmPostPltiTraceEventCaptureWindowSealed);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 1, 0,
        kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive,
        kAirportItlwmIwnDirectSaeTraceMissingStageUnknown,
        "a late scan cannot retroactively satisfy the selected-BSS boundary");

    begin(&fixture);
    append_to_peer_confirm(&fixture, 1);
    append(&fixture, kAirportItlwmPostPltiTraceEventIwnDirectSaePmkClaimed);
    append(&fixture, kAirportItlwmPostPltiTraceEventCaptureWindowSealed);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 1, 0,
        kAirportItlwmIwnDirectSaeTraceVerdictAssocDescriptorNotAccepted,
        kAirportItlwmIwnDirectSaeTraceMissingStageAssocDescriptor,
        "a claimed PMK cannot infer an accepted Association descriptor");

    begin(&fixture);
    append_to_peer_confirm(&fixture, 1);
    append_after_peer_confirm(&fixture, 0);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 1, 0,
        kAirportItlwmIwnDirectSaeTraceVerdictPmfPtkSoftwareCcmpNotObserved,
        kAirportItlwmIwnDirectSaeTraceMissingStagePmfPtkSoftwareCcmp,
        "a direct SAE four-way cannot claim WPA3 success without the PMF keyset");

    begin(&fixture);
    append_full_success(&fixture);
    fixture.entries[fixture.count - 3].event =
        kAirportItlwmPostPltiTraceEventIwnIgtkSlot5TxSelected;
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 1, 0,
        kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive,
        kAirportItlwmIwnDirectSaeTraceMissingStageUnknown,
        "a mismatched IGTK publication/selection pair cannot manufacture PMF evidence");

    begin(&fixture);
    append_to_peer_confirm(&fixture, 0);
    append(&fixture, kAirportItlwmPostPltiTraceEventCaptureWindowSealed);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 1, 0,
        kAirportItlwmIwnDirectSaeTraceVerdictNodeMfpNotNegotiated,
        kAirportItlwmIwnDirectSaeTraceMissingStageNodeMfp,
        "raw Confirm progress cannot claim the PMF-gated continuation");

    begin(&fixture);
    append(&fixture,
        kAirportItlwmPostPltiTraceEventIwnDirectSaePeerCommitAccepted);
    append(&fixture, kAirportItlwmPostPltiTraceEventCaptureWindowSealed);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 1, 0,
        kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive,
        kAirportItlwmIwnDirectSaeTraceMissingStageUnknown,
        "peer Commit cannot precede a completed local Commit");

    begin(&fixture);
    append_full_success(&fixture);
    expect(&fixture, 0, kAirportItlwmPostPltiTraceBackendIwn, 1, 0,
        kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive,
        kAirportItlwmIwnDirectSaeTraceMissingStageUnknown,
        "a dropped categorical record is fail-closed");

    begin(&fixture);
    append_to_peer_confirm(&fixture, 1);
    append(&fixture, kAirportItlwmPostPltiTraceEventEpisodeAborted);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 1, 0,
        kAirportItlwmIwnDirectSaeTraceVerdictIntegrityInconclusive,
        kAirportItlwmIwnDirectSaeTraceMissingStageUnknown,
        "an aborted direct SAE attempt is never success");

    fixture.count = 0;
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwn, 0, 0,
        kAirportItlwmIwnDirectSaeTraceVerdictBranchNotObserved,
        kAirportItlwmIwnDirectSaeTraceMissingStageNone,
        "no direct episode remains a neutral negative observation");

    begin(&fixture);
    append_full_success(&fixture);
    expect(&fixture, 1, kAirportItlwmPostPltiTraceBackendIwx, 1, 0,
        kAirportItlwmIwnDirectSaeTraceVerdictBackendUnsupported,
        kAirportItlwmIwnDirectSaeTraceMissingStageUnknown,
        "IWX cannot borrow the IWN direct SAE report");

    puts("IWN direct SAE C trace fixtures ok");
    return 0;
}
