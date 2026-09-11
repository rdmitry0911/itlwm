/* Execute the actual opaque driver core with real HnP/H2E peer crypto.
 * TX receipts and elapsed timeout admission are caller boundaries, not RF.
 */
#include <assert.h>
#include "utils/includes.h"
#include "utils/common.h"
#include "utils/wpabuf.h"
#include "common/sae.h"
#include "common/ieee802_11_defs.h"
#include "common/wpa_common.h"
#include <net80211/ieee80211_sae_engine.h>

static const uint8_t password[] = "test-peer-retry";
static const uint8_t ap_mac[6] = { 2, 0, 0, 0, 0, 1 };
static const uint8_t sta_mac[6] = { 2, 0, 0, 0, 0, 2 };
static uint64_t next_ticket = UINT64_C(1) << 63;
static unsigned int cases;

static struct ieee80211_sae_engine *
begin(unsigned int method)
{
    struct ItlSaeSelectedJoinEventV1 selected = {0};
    struct ItlSaeAuthActivatedEventV1 activated = {0};
    struct ieee80211_sae_engine *engine = NULL;
    selected.version = activated.version = kItlSaeAuthTransportV1Version;
    selected.size = sizeof(selected);
    activated.size = sizeof(activated);
    selected.request_generation = activated.request_generation = 31;
    selected.association_epoch = activated.association_epoch = 43;
    activated.relay_generation = 59;
    selected.sae_group = 19;
    selected.sae_method = method;
    selected.rsnxe_capabilities = method == 2 ? kItlSaeAuthTransportRsnxeH2e : 0;
    selected.ssid_len = 4;
    memcpy(selected.ssid, "test", 4);
    memcpy(selected.bssid, ap_mac, 6);
    memcpy(activated.bssid, ap_mac, 6);
    memcpy(selected.sta, sta_mac, 6);
    memcpy(activated.sta, sta_mac, 6);
    assert(ieee80211_sae_engine_begin(&selected, &activated, password,
        sizeof(password) - 1, &engine) == 0);
    assert(engine != NULL);
    return engine;
}

static struct ItlSaeAuthTransportEventV1
terminal_for(const struct ItlSaeAuthTxRequestV1 *request, int result)
{
    struct ItlSaeAuthTransportEventV1 event = {0};
    event.version = kItlSaeAuthTransportV1Version;
    event.size = sizeof(event);
    event.kind = kItlSaeAuthTransportEventTxComplete;
    event.result = result;
    event.association_epoch = request->association_epoch;
    event.relay_generation = request->relay_generation;
    event.ticket = request->ticket;
    event.phase = request->phase;
    event.auth_status = request->auth_status;
    event.wire_transaction = request->wire_transaction;
    memcpy(event.bssid, request->bssid, 6);
    memcpy(event.sta, request->sta, 6);
    assert(itl_sae_auth_transport_event_matches_request(&event, request));
    return event;
}

static struct ItlSaeAuthPeerEventV1
peer_for(const struct ItlSaeAuthTxRequestV1 *request)
{
    struct ItlSaeAuthPeerEventV1 peer = {0};
    peer.version = kItlSaeAuthTransportV1Version;
    peer.size = sizeof(peer);
    peer.association_epoch = request->association_epoch;
    peer.relay_generation = request->relay_generation;
    peer.phase = request->phase;
    peer.auth_status = request->auth_status;
    peer.wire_transaction = request->wire_transaction;
    memcpy(peer.bssid, request->bssid, 6);
    memcpy(peer.sta, request->sta, 6);
    return peer;
}

static void
same_body(const struct ItlSaeAuthTxRequestV1 *first,
    const struct ItlSaeAuthTxRequestV1 *retry)
{
    struct ItlSaeAuthTxRequestV1 copy = *retry;
    assert(retry->ticket > first->ticket);
    copy.ticket = first->ticket;
    assert(memcmp(first, &copy, sizeof(copy)) == 0);
}

/* All copies after the first are actual terminal-successful transmissions.
 * A pre-doorbell rollback between them must not spend the protocol budget.
 */
static struct ItlSaeAuthTxRequestV1
transmit(struct ieee80211_sae_engine *engine, unsigned int omissions)
{
    struct ItlSaeAuthTxRequestV1 first, request, scratch;
    struct ItlSaeAuthTransportEventV1 terminal, stale;
    assert(ieee80211_sae_engine_prepare_tx(engine, ++next_ticket, &first) == 0);
    request = first;
    for (unsigned int attempt = 0; attempt <= omissions; attempt++) {
        terminal = terminal_for(&request, 0);
        stale = terminal;
        stale.association_epoch++;
        assert(ieee80211_sae_engine_tx_complete(engine, &stale) == -1);
        assert(ieee80211_sae_engine_retry_peer(engine, request.ticket) == -1);
        assert(ieee80211_sae_engine_tx_complete(engine, &terminal) == 0);
        assert(ieee80211_sae_engine_tx_complete(engine, &terminal) == -1);
        assert(ieee80211_sae_engine_retry_peer(engine, request.ticket + 1) == -1);
        if (attempt == omissions)
            break;
        assert(ieee80211_sae_engine_retry_peer(engine, request.ticket) == 0);
        assert(ieee80211_sae_engine_retry_peer(engine, request.ticket) == -1);
        assert(ieee80211_sae_engine_prepare_tx(engine, ++next_ticket, &scratch) == 0);
        same_body(&first, &scratch);
        assert(ieee80211_sae_engine_tx_rollback_unsubmitted(engine, scratch.ticket) == 0);
        assert(ieee80211_sae_engine_prepare_tx(engine, scratch.ticket, &request) == -1);
        assert(ieee80211_sae_engine_prepare_tx(engine, ++next_ticket, &request) == 0);
        same_body(&first, &request);
        assert(ieee80211_sae_engine_tx_complete(engine, &terminal) == -1);
    }
    return request;
}

static void
write_peer(struct sae_data *ap, struct ItlSaeAuthPeerEventV1 *peer, int confirm)
{
    struct wpabuf *buffer = wpabuf_alloc(sizeof(peer->body));
    assert(buffer != NULL);
    assert((confirm ? sae_write_confirm(ap, buffer) :
        sae_write_commit(ap, buffer, NULL, NULL, 0)) == 0);
    peer->body_len = wpabuf_len(buffer);
    memcpy(peer->body, wpabuf_head(buffer), peer->body_len);
    wpabuf_clear_free(buffer);
}

static void
round_trip(unsigned int method, unsigned int commit_omissions,
    unsigned int confirm_omissions, int token_required)
{
    static int groups[] = {19, 0};
    static const uint8_t token[] = "0123456789abcdef0123456789abcdef";
    struct ieee80211_sae_engine *engine = begin(method);
    struct ItlSaeAuthTxRequestV1 commit = transmit(engine, commit_omissions);
    struct ItlSaeAuthPeerEventV1 peer, duplicate_commit;
    struct ItlSaePmkContinuationV1 result;
    struct sae_data ap = {0};
    const uint8_t *parsed_token = NULL;
    size_t token_len = 0;
    int offset = 0;

    if (token_required) {
        struct ItlSaeAuthTxRequestV1 original_commit = commit;
        peer = peer_for(&commit);
        peer.auth_status = WLAN_STATUS_ANTI_CLOGGING_TOKEN_REQ;
        peer.body[0] = 19;
        if (method == 2) {
            peer.body[2] = WLAN_EID_EXTENSION;
            peer.body[3] = sizeof(token);
            peer.body[4] = WLAN_EID_EXT_ANTI_CLOGGING_TOKEN;
            memcpy(peer.body + 5, token, sizeof(token) - 1);
            peer.body_len = 5 + sizeof(token) - 1;
        } else {
            memcpy(peer.body + 2, token, sizeof(token) - 1);
            peer.body_len = 2 + sizeof(token) - 1;
        }
        assert(ieee80211_sae_engine_handle_peer(engine, &peer, &result) ==
            IEEE80211_SAE_ENGINE_PEER_TX_READY);
        uint64_t old_ticket = commit.ticket;
        assert(ieee80211_sae_engine_retry_peer(engine, old_ticket) == -1);
        commit = transmit(engine, 2);
        /* A token produces a new serialized body with its own bounded
         * transmission budget, without restarting the crypto exchange. */
        assert(commit.body_len == (method == 2 ? 133u : 130u));
        if (method == 2)
            assert(memcmp(original_commit.body, commit.body, 98) == 0);
        else
            assert(memcmp(original_commit.body + 2, commit.body + 34, 96) == 0);
    }

    ap.no_pw_id = 1;
    ap.akmp = WPA_KEY_MGMT_SAE;
    assert(sae_set_group(&ap, 19) == 0);
    if (method == 2) {
        struct sae_pt *pt = sae_derive_pt(groups, (const uint8_t *)"test", 4,
            password, sizeof(password) - 1, NULL, 0);
        assert(pt != NULL);
        assert(sae_prepare_commit_pt(&ap, pt, ap_mac, sta_mac, NULL, NULL) == 0);
        sae_deinit_pt(pt);
    } else {
        assert(sae_prepare_commit(ap_mac, sta_mac, password,
            sizeof(password) - 1, &ap) == 0);
    }
    uint16_t parse_status = sae_parse_commit(&ap, commit.body, commit.body_len,
        &parsed_token, &token_len, groups, method == 2, &offset);
    if (parse_status != 0)
        fprintf(stderr, "peer parser method=%u commit_omissions=%u token=%d status=%u\n",
            method, commit_omissions, token_required, parse_status);
    assert(parse_status == 0);
    if (token_required && method == 1) {
        assert(token_len == sizeof(token) - 1 &&
            memcmp(parsed_token, token, sizeof(token) - 1) == 0);
    }
    assert(sae_process_commit(&ap) == 0);
    peer = peer_for(&commit);
    write_peer(&ap, &peer, 0);
    duplicate_commit = peer;
    peer.relay_generation++;
    assert(ieee80211_sae_engine_handle_peer(engine, &peer, &result) ==
        IEEE80211_SAE_ENGINE_PEER_DROP);
    peer = duplicate_commit;
    assert(ieee80211_sae_engine_handle_peer(engine, &peer, &result) ==
        IEEE80211_SAE_ENGINE_PEER_TX_READY);
    assert(ieee80211_sae_engine_retry_peer(engine, commit.ticket) == -1);
    struct ItlSaeAuthTxRequestV1 confirm = transmit(engine, confirm_omissions);
    assert(confirm.phase == kItlSaeAuthTransportPhaseConfirm);
    if (confirm_omissions == 2)
        assert(ieee80211_sae_engine_retry_peer(engine, confirm.ticket) == -2);
    assert(sae_check_confirm(&ap, confirm.body, confirm.body_len, &offset) == 0);
    assert(ieee80211_sae_engine_handle_peer(engine, &duplicate_commit, &result) ==
        IEEE80211_SAE_ENGINE_PEER_DROP);
    peer = peer_for(&confirm);
    write_peer(&ap, &peer, 1);
    assert(ieee80211_sae_engine_handle_peer(engine, &peer, &result) ==
        IEEE80211_SAE_ENGINE_PEER_COMPLETE);
    assert(memcmp(result.pmk, ap.pmk, sizeof(result.pmk)) == 0);
    assert(memcmp(result.pmkid, ap.pmkid, sizeof(result.pmkid)) == 0);
    assert(!ieee80211_sae_engine_is_active(engine));
    assert(ieee80211_sae_engine_retry_peer(engine, confirm.ticket) == -1);
    sae_clear_data(&ap);
    ieee80211_sae_engine_destroy(&engine);
    assert(engine == NULL);
    cases++;
}

static void
terminal_cases(unsigned int method)
{
    struct ieee80211_sae_engine *engine = begin(method);
    struct ItlSaeAuthTxRequestV1 request = transmit(engine, 2), scratch;
    assert(ieee80211_sae_engine_retry_peer(engine, request.ticket) == -2);
    assert(ieee80211_sae_engine_retry_peer(engine, request.ticket) == -2);
    assert(ieee80211_sae_engine_prepare_tx(engine, ++next_ticket, &scratch) == -1);
    ieee80211_sae_engine_destroy(&engine);
    assert(ieee80211_sae_engine_retry_peer(engine, request.ticket) == -1);
    cases++;
    const int failures[] = {-1, 5, 60, 125};
    for (unsigned int i = 0; i < sizeof(failures) / sizeof(failures[0]); i++) {
        engine = begin(method);
        assert(ieee80211_sae_engine_prepare_tx(engine, ++next_ticket, &request) == 0);
        struct ItlSaeAuthTransportEventV1 terminal = terminal_for(&request, failures[i]);
        assert(ieee80211_sae_engine_tx_complete(engine, &terminal) == -1);
        assert(!ieee80211_sae_engine_is_active(engine));
        assert(ieee80211_sae_engine_retry_peer(engine, request.ticket) == -1);
        ieee80211_sae_engine_destroy(&engine);
        cases++;
    }
}

int main(void)
{
    for (unsigned int method = 1; method <= 2; method++) {
        for (unsigned int commit = 0; commit < 3; commit++)
            for (unsigned int confirm = 0; confirm < 3; confirm++)
                round_trip(method, commit, confirm, 0);
        round_trip(method, 2, 2, 1);
        terminal_cases(method);
    }
    printf("production SAE HnP/H2E peer-retry core: PASS (%u scenarios)\n", cases);
    return 0;
}
