// Reuse only the explicit kernel/crypto/transport boundaries, not its main.
// worker.inc holds one family's unmodified complete production functions.
#define SAE_PEER_RETRY_ONLY 1
#include "tests/iwn_sae_join_failure_test.cpp"

struct Fixture {
    ItlIwn device;
    IOSimpleLock bssLock, engineLock, txLock;
    ieee80211_node node;
    ieee80211_sae_engine core;
    Fixture() {
        scheduled = genericScans = ownedScans = destroyed = delivered = 0;
        producerAcks = credentialRetired = retryCoreCalls = submitCalls = 0;
        terminalCoreCalls = peerCoreCalls = 0;
        retryCoreResult = terminalCoreResult = 0;
        cryptoResult = IEEE80211_SAE_ENGINE_PEER_DROP;
        testNow = 10000;
        auto &sc = device.com;
        auto &ic = sc.sc_ic;
        auto &owner = sc.sc_sae_engine_owner;
        sc.sc_sae_engine_lock = &engineLock;
        sc.sc_sae_tx_lock = &txLock;
        sc.sc_sae_engine = &core;
        ic.ic_bss = &node;
        ic.ic_pae_selected_bss_lock = &bssLock;
        const uint8_t ap[6] = {2, 1, 2, 3, 4, 5}, sta[6] = {2, 6, 7, 8, 9, 10};
        memcpy(node.ni_bssid, ap, 6);
        memcpy(ic.ic_myaddr, sta, 6);
        memcpy(ic.ic_sae_peer_rx_admission.bssid, ap, 6);
        memcpy(ic.ic_sae_peer_rx_admission.sta, sta, 6);
        memcpy(ic.ic_sae_wcl_request.bssid, ap, 6);
        memcpy(ic.ic_sae_wcl_request.ssid, "test", 4);
        owner.active = true;
        owner.request_generation = 19;
        owner.association_epoch = 41;
        owner.relay_generation = 7;
        owner.selected.request_generation = 19;
        owner.selected.association_epoch = 41;
        owner.selected.ssid_len = 4;
        memcpy(owner.selected.bssid, ap, 6);
        memcpy(owner.selected.sta, sta, 6);
        memcpy(owner.selected.ssid, "test", 4);
        owner.activated.version = 1;
        owner.activated.size = sizeof(owner.activated);
        owner.activated.request_generation = 19;
        owner.activated.association_epoch = 41;
        owner.activated.relay_generation = 7;
        memcpy(owner.activated.bssid, ap, 6);
        memcpy(owner.activated.sta, sta, 6);
        owner.peer_reply_ticket = 103;
        owner.peer_reply_deadline = 12000;
    }
    void run() {
        ItlIwn::iwn_sae_engine_task(&device.com);
        assert(!workerLive && leaves == 0);
    }
    void queuePeer() {
        auto &owner = device.com.sc_sae_engine_owner;
        auto &peer = owner.peerq[0];
        peer.version = 1;
        peer.size = sizeof(peer);
        peer.association_epoch = 41;
        peer.relay_generation = 7;
        peer.phase = peer.wire_transaction = 1;
        memcpy(peer.bssid, owner.selected.bssid, 6);
        memcpy(peer.sta, owner.selected.sta, 6);
        owner.peer_count = owner.peer_tail = 1;
    }
    ItlSaeAuthTransportEventV1 terminal(int result = 0) {
        auto &owner = device.com.sc_sae_engine_owner;
        owner.in_flight_ticket = 103;
        owner.peer_reply_ticket = owner.peer_reply_deadline = 0;
        ItlSaeAuthTransportEventV1 event{};
        event.version = 1;
        event.size = sizeof(event);
        event.kind = kItlSaeAuthTransportEventTxComplete;
        event.result = result;
        event.association_epoch = 41;
        event.relay_generation = 7;
        event.ticket = 103;
        event.phase = event.wire_transaction = 1;
        memcpy(event.bssid, owner.selected.bssid, 6);
        memcpy(event.sta, owner.selected.sta, 6);
        return event;
    }
};

int main() {
    unsigned cases = 0;
    {
        Fixture f;
        testNow = 11999;
        f.device.iwn_sae_peer_timer_drain();
        f.run();
        assert(retryCoreCalls == 0 && scheduled == 0);
        assert(f.device.saePeerTimer.arms == 1 && f.device.saePeerTimer.deadline == 12000);
        testNow = 12000;
        f.device.iwn_sae_peer_timer_drain();
        assert(scheduled == 1);
        f.run();
        assert(retryCoreCalls == 1 && retryCoreTicket == 103 && submitCalls == 1);
        f.run();
        assert(retryCoreCalls == 1 && submitCalls == 1);
        ++cases;
    }
    {
        Fixture f;
        auto event = f.terminal();
        assert(iwn_sae_engine_queue_terminal(&f.device.com, &event));
        testNow = 10300; // Worker delay must not extend the receipt deadline.
        f.run();
        assert(terminalCoreCalls == 1 && f.device.saePeerTimer.signals == 1);
        f.device.iwn_sae_peer_timer_drain();
        assert(f.device.saePeerTimer.deadline == 12000);
        ++cases;
    }
    for (bool progress : {false, true}) {
        Fixture f;
        f.queuePeer();
        cryptoResult = progress ? IEEE80211_SAE_ENGINE_PEER_TX_READY : IEEE80211_SAE_ENGINE_PEER_DROP;
        testNow = 12000;
        f.run();
        assert(peerCoreCalls == 1 && retryCoreCalls == 0);
        f.run();
        assert(retryCoreCalls == (progress ? 0u : 1u));
        assert(submitCalls == 1);
        ++cases;
    }
    {
        Fixture f;
        testNow = 12000;
        retryCoreResult = -2;
        f.run();
        assert(retryCoreCalls == 1 && submitCalls == 0);
        assert(genericScans == 0 && destroyed == 0);
        assert(f.device.com.sc_ic.ic_mgt_timer == 1 && f.device.com.sc_ic.ic_if.if_timer == 1);
        f.run();
        assert(retryCoreCalls == 1);
        ++cases;
    }
    for (unsigned condition = 0; condition < 15; ++condition) {
        Fixture f;
        auto &sc = f.device.com;
        auto &ic = sc.sc_ic;
        auto &owner = sc.sc_sae_engine_owner;
        testNow = 12000;
        switch (condition) {
        case 0: ic.ic_pae_assoc_epoch++; break;
        case 1: ic.ic_sae_peer_rx_admission.relay_generation++; break;
        case 2: ic.ic_sae_wcl_request.ssid[0]++; break;
        case 3: f.node.ni_bssid[0] ^= 2; break;
        case 4: owner.active = false; break;
        case 5: owner.cancelled = true; owner.suppress_scan = true; break;
        case 6: sc.sc_sae_engine_stopping = true; break; // off/sleep
        case 7: sc.sc_sae_engine_detaching = true; break;
        case 8: owner.completion_claimed = true; break;
        case 9: owner.in_flight_ticket = 104; break;
        case 10: owner.peer_reply_deadline = 0; break;
        case 11: owner.peer_reply_ticket = 0; break;
        case 12: ic.ic_pae_assoc_replace_epoch = 41; break;
        case 13: ic.ic_pae_selected_bss.epoch++; break;
        case 14: ic.ic_sae_wcl_request.generation++; break;
        }
        f.device.iwn_sae_peer_timer_drain();
        assert(scheduled == 0);
        f.run();
        assert(retryCoreCalls == 0 && submitCalls == 0 && ic.ic_mgt_timer == 0);
        if (condition == 0 || condition == 1 || condition == 2 || condition == 3 || condition >= 12)
            assert(scheduled == 0); // No obsolete-deadline busy loop.
        ++cases;
    }
    {
        Fixture f;
        ItlSaeAuthActivatedEventV1 identity{};
        assert(!iwn_sae_engine_take_peer_retry(&f.device.com, 103, &identity));
        testNow = 12000;
        assert(!iwn_sae_engine_take_peer_retry(&f.device.com, 102, &identity));
        f.queuePeer();
        assert(!iwn_sae_engine_take_peer_retry(&f.device.com, 103, &identity));
        assert(f.device.com.sc_sae_engine_owner.peer_reply_ticket == 103);
        ++cases;
    }
    {
        Fixture f;
        auto identity = f.device.com.sc_sae_engine_owner.activated;
        f.device.com.sc_sae_engine_owner.activated.relay_generation++;
        iwn_sae_engine_peer_exhausted(&f.device.com, &identity);
        assert(f.device.com.sc_ic.ic_mgt_timer == 0);
        ++cases;
    }
    {
        Fixture f;
        f.device.com.sc_sae_engine_owner.peer_reply_ticket = 104;
        f.device.com.sc_sae_engine_owner.peer_reply_deadline = 14000;
        testNow = 12000; // An old timer firing must arm the successor deadline.
        f.device.iwn_sae_peer_timer_drain();
        f.run();
        assert(retryCoreCalls == 0 && scheduled == 0 && f.device.saePeerTimer.deadline == 14000);
        ++cases;
    }
    for (unsigned condition = 0; condition < 3; ++condition) {
        Fixture f;
        auto event = f.terminal(condition == 0 ? -5 : 0);
        if (condition == 0) {
            terminalCoreResult = -1;
            assert(iwn_sae_engine_queue_terminal(&f.device.com, &event));
            assert(f.device.com.sc_sae_engine_owner.terminal_peer_deadline == 0);
        } else if (condition == 1) {
            event.ticket++;
            assert(!iwn_sae_engine_queue_terminal(&f.device.com, &event));
        } else {
            assert(iwn_sae_engine_queue_terminal(&f.device.com, &event));
            assert(iwn_sae_engine_queue_terminal(&f.device.com, &event));
            assert(f.device.com.sc_sae_engine_owner.cancelled);
        }
        f.run();
        assert(retryCoreCalls == 0 && submitCalls == 0 && f.device.saePeerTimer.signals == 0);
        ++cases;
    }
    printf("PASS: %u actual peer-timeout worker/receipt/deadline scenarios\n", cases);
}
