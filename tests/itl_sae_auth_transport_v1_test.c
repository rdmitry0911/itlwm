#include <assert.h>
#include <string.h>

#include <HAL/ItlSaeAuthTransportV1.h>

static void
fill_request(struct ItlSaeAuthTxRequestV1 *request)
{
    memset(request, 0, sizeof(*request));
    request->version = kItlSaeAuthTransportV1Version;
    request->size = sizeof(*request);
    request->association_epoch = 9;
    request->relay_generation = 11;
    request->ticket = 13;
    request->phase = kItlSaeAuthTransportPhaseCommit;
    request->wire_transaction =
        itl_sae_auth_transport_sta_wire_transaction_for_phase(request->phase);
    request->body_len = 98;
    request->bssid[0] = 0x02;
    request->bssid[5] = 0x01;
    request->sta[0] = 0x02;
    request->sta[5] = 0x02;
    request->body[0] = 19;
}

static void
fill_event(const struct ItlSaeAuthTxRequestV1 *request,
           struct ItlSaeAuthTransportEventV1 *event)
{
    memset(event, 0, sizeof(*event));
    event->version = kItlSaeAuthTransportV1Version;
    event->size = sizeof(*event);
    event->kind = kItlSaeAuthTransportEventTxComplete;
    event->association_epoch = request->association_epoch;
    event->relay_generation = request->relay_generation;
    event->ticket = request->ticket;
    event->phase = request->phase;
    event->auth_status = request->auth_status;
    event->wire_transaction = request->wire_transaction;
    memcpy(event->bssid, request->bssid, sizeof(event->bssid));
    memcpy(event->sta, request->sta, sizeof(event->sta));
}

static void
assert_request_rejected(const struct ItlSaeAuthTxRequestV1 *request)
{
    assert(!itl_sae_auth_transport_request_is_well_formed(request));
}

static void
assert_event_rejected(const struct ItlSaeAuthTransportEventV1 *event)
{
    assert(!itl_sae_auth_transport_event_is_well_formed(event));
}

static void
test_null_arguments(void)
{
    struct ItlSaeAuthTxRequestV1 request;
    struct ItlSaeAuthTransportEventV1 event;

    fill_request(&request);
    fill_event(&request, &event);

    assert_request_rejected(NULL);
    assert_event_rejected(NULL);
    assert(!itl_sae_auth_transport_event_matches_request(NULL, &request));
    assert(!itl_sae_auth_transport_event_matches_request(&event, NULL));
}

static void
test_request_well_formed_boundaries_and_fixed_fields(void)
{
    struct ItlSaeAuthTxRequestV1 request;

    fill_request(&request);
    request.body_len = 1;
    memset(request.body, 0, sizeof(request.body));
    assert(itl_sae_auth_transport_request_is_well_formed(&request));

    fill_request(&request);
    request.phase = kItlSaeAuthTransportPhaseConfirm;
    request.wire_transaction =
        itl_sae_auth_transport_sta_wire_transaction_for_phase(request.phase);
    request.body_len = kItlSaeAuthTransportV1MaxBodyLength;
    assert(itl_sae_auth_transport_request_is_well_formed(&request));

    fill_request(&request);
    request.version = kItlSaeAuthTransportV1Version + 1u;
    assert_request_rejected(&request);

    fill_request(&request);
    request.size = sizeof(request) - 1u;
    assert_request_rejected(&request);

    fill_request(&request);
    request.association_epoch = 0;
    assert_request_rejected(&request);

    fill_request(&request);
    request.relay_generation = 0;
    assert_request_rejected(&request);

    fill_request(&request);
    request.ticket = 0;
    assert_request_rejected(&request);

    fill_request(&request);
    request.phase = 0;
    assert_request_rejected(&request);

    fill_request(&request);
    request.phase = 3;
    assert_request_rejected(&request);

    fill_request(&request);
    request.wire_transaction = kItlSaeAuthTransportPeerWireTransactionCommit;
    assert_request_rejected(&request);

    fill_request(&request);
    request.auth_status = 1;
    assert_request_rejected(&request);

    fill_request(&request);
    request.body_len = 0;
    assert_request_rejected(&request);

    fill_request(&request);
    request.body_len = kItlSaeAuthTransportV1MaxBodyLength + 1u;
    assert_request_rejected(&request);

    fill_request(&request);
    memset(request.bssid, 0, sizeof(request.bssid));
    assert_request_rejected(&request);

    fill_request(&request);
    request.bssid[0] |= 0x01u;
    assert_request_rejected(&request);

    fill_request(&request);
    memset(request.sta, 0, sizeof(request.sta));
    assert_request_rejected(&request);

    fill_request(&request);
    request.sta[0] |= 0x01u;
    assert_request_rejected(&request);

    fill_request(&request);
    request.reserved[sizeof(request.reserved) - 1u] = 1;
    assert_request_rejected(&request);
}

static void
test_event_well_formed_fixed_fields_and_result(void)
{
    struct ItlSaeAuthTxRequestV1 request;
    struct ItlSaeAuthTransportEventV1 event;

    fill_request(&request);
    fill_event(&request, &event);
    event.result = -17;
    assert(itl_sae_auth_transport_event_is_well_formed(&event));
    assert(itl_sae_auth_transport_event_matches_request(&event, &request));

    fill_event(&request, &event);
    event.result = 23;
    assert(itl_sae_auth_transport_event_is_well_formed(&event));
    assert(itl_sae_auth_transport_event_matches_request(&event, &request));

    fill_event(&request, &event);
    event.version = kItlSaeAuthTransportV1Version + 1u;
    assert_event_rejected(&event);

    fill_event(&request, &event);
    event.size = sizeof(event) - 1u;
    assert_event_rejected(&event);

    fill_event(&request, &event);
    event.kind = 0;
    assert_event_rejected(&event);

    fill_event(&request, &event);
    event.association_epoch = 0;
    assert_event_rejected(&event);

    fill_event(&request, &event);
    event.relay_generation = 0;
    assert_event_rejected(&event);

    fill_event(&request, &event);
    event.ticket = 0;
    assert_event_rejected(&event);

    fill_event(&request, &event);
    event.phase = 0;
    assert_event_rejected(&event);

    fill_event(&request, &event);
    event.phase = 3;
    assert_event_rejected(&event);

    fill_event(&request, &event);
    event.wire_transaction =
        kItlSaeAuthTransportPeerWireTransactionCommit;
    assert_event_rejected(&event);

    fill_event(&request, &event);
    event.auth_status = 1;
    assert_event_rejected(&event);

    fill_event(&request, &event);
    memset(event.bssid, 0, sizeof(event.bssid));
    assert_event_rejected(&event);

    fill_event(&request, &event);
    event.bssid[0] |= 0x01u;
    assert_event_rejected(&event);

    fill_event(&request, &event);
    memset(event.sta, 0, sizeof(event.sta));
    assert_event_rejected(&event);

    fill_event(&request, &event);
    event.sta[0] |= 0x01u;
    assert_event_rejected(&event);

    fill_event(&request, &event);
    event.reserved[sizeof(event.reserved) - 1u] = 1;
    assert_event_rejected(&event);
}

static void
assert_identity_mismatch(const struct ItlSaeAuthTransportEventV1 *event,
                         const struct ItlSaeAuthTxRequestV1 *request)
{
    assert(itl_sae_auth_transport_event_is_well_formed(event));
    assert(itl_sae_auth_transport_request_is_well_formed(request));
    assert(!itl_sae_auth_transport_event_matches_request(event, request));
}

static void
test_event_request_identity_matching(void)
{
    struct ItlSaeAuthTxRequestV1 request;
    struct ItlSaeAuthTransportEventV1 event;

    fill_request(&request);
    fill_event(&request, &event);
    assert(itl_sae_auth_transport_event_matches_request(&event, &request));

    event.association_epoch++;
    assert_identity_mismatch(&event, &request);

    fill_event(&request, &event);
    event.relay_generation++;
    assert_identity_mismatch(&event, &request);

    fill_event(&request, &event);
    event.ticket++;
    assert_identity_mismatch(&event, &request);

    fill_event(&request, &event);
    event.phase = kItlSaeAuthTransportPhaseConfirm;
    event.wire_transaction =
        itl_sae_auth_transport_sta_wire_transaction_for_phase(event.phase);
    assert_identity_mismatch(&event, &request);

    fill_event(&request, &event);
    event.bssid[sizeof(event.bssid) - 1u]++;
    assert_identity_mismatch(&event, &request);

    fill_event(&request, &event);
    event.sta[sizeof(event.sta) - 1u]++;
    assert_identity_mismatch(&event, &request);

    fill_event(&request, &event);
    event.auth_status = 1;
    assert(!itl_sae_auth_transport_event_matches_request(&event, &request));
}

static void
test_semantic_phase_and_wire_sequence_mapping(void)
{
    assert(itl_sae_auth_transport_sta_wire_transaction_for_phase(
        kItlSaeAuthTransportPhaseCommit) ==
        kItlSaeAuthTransportStaWireTransactionCommit);
    assert(itl_sae_auth_transport_sta_wire_transaction_for_phase(
        kItlSaeAuthTransportPhaseConfirm) ==
        kItlSaeAuthTransportStaWireTransactionConfirm);
    assert(itl_sae_auth_transport_peer_wire_transaction_for_phase(
        kItlSaeAuthTransportPhaseCommit) ==
        kItlSaeAuthTransportPeerWireTransactionCommit);
    assert(itl_sae_auth_transport_peer_wire_transaction_for_phase(
        kItlSaeAuthTransportPhaseConfirm) ==
        kItlSaeAuthTransportPeerWireTransactionConfirm);
    assert(itl_sae_auth_transport_sta_wire_transaction_for_phase(0) == 0);
    assert(itl_sae_auth_transport_peer_wire_transaction_for_phase(3) == 0);
}

int
main(void)
{
    test_null_arguments();
    test_request_well_formed_boundaries_and_fixed_fields();
    test_event_well_formed_fixed_fields_and_result();
    test_event_request_identity_matching();
    test_semantic_phase_and_wire_sequence_mapping();

    return 0;
}
