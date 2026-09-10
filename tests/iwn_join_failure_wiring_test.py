"""Check real call-site wiring in addition to the compiled value/leaf tests."""
from pathlib import Path
import sys

root = Path(sys.argv[1])
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
node = (root / "itl80211/openbsd/net80211/ieee80211_node.c").read_text()


def body(text, name):
    start = text.index("\n" + name + "(")
    opening = text.index("{", start)
    depth = 0
    for pos in range(opening, len(text)):
        if text[pos] == "{":
            depth += 1
        elif text[pos] == "}":
            depth -= 1
            if depth == 0:
                return text[opening:pos + 1]
    raise AssertionError(name)


def ordered(text, *tokens):
    end = 0
    for token in tokens:
        start = text.find(token, end)
        assert start >= end, token
        end = start + len(token)


assert "ieee80211_end_scan_owned(ifp, mode, 0);" in body(node, "ieee80211_end_scan_controlled")
scan = body(node, "ieee80211_end_scan_owned")
ordered(scan, "IEEE80211_EVT_SCAN_DONE", "ieee80211_wcl_join_scan_current",
        "if (!generic_terminal)", "if (initial_scan_census_only)",
        "notfound:", "IEEE80211_C_SCANALLBAND",
        "ieee80211_wcl_join_scan_failed(ic, join_generation)",
        "ieee80211_next_scan(ifp)")
reserve = body(iwn, "iwn_scan_lease_reserve")
ordered(reserve, "owner == IWN_SCAN_LEASE_GENERIC_FOREGROUND",
        "ieee80211_wcl_join_scan_generation", "IOSimpleLockLock",
        "sc->sc_scan_lease.join_generation = join_generation")
stop_start = iwn.index("case IWN_STOP_SCAN:")
stop = iwn[stop_start:iwn.index("case IWN5000_CALIBRATION_RESULT:", stop_start)]
ordered(stop, "iwn_scan_continue", "iwn_scan_lease_claim_terminal",
        "terminal.join_generation != 0 && terminal.aborted",
        "IEEE80211_SCAN_COMPLETION_WCL_HANDOFF", "ieee80211_end_scan_owned",
        "iwn_scan_lease_finish_terminal", "if (join_terminal_retired",
        "iwn_wcl_join_failure_scan", "IEEE80211_JOIN_CLEANUP_PRODUCER")
replay = body(iwn, "iwn_scan_lease_replay_task")
ordered(replay, "iwn_scan_lease_take_join_cleanup",
        "ieee80211_wcl_join_failure_pending", "ieee80211_pae_assoc_epoch_note_newstate",
        "ieee80211_wcl_join_failure_pending", "iwn_newstate_impl",
        "iwn_sae_engine_request_join_retirement", "IOSimpleLockLock")
queue = body(iwn, "iwn_wcl_join_failure_scan")
assert "generation >= sc->sc_wcl_join_cleanup_generation" in queue
assert "!sc->sc_scan_lease.hardware_invalidated" in queue
assert "iwn_cmd" not in queue and "replay_pending =" not in queue
worker = body(iwn, "iwn_sae_engine_task")
ordered(worker[worker.rindex("out:"):], "explicit_bzero(&continuation",
        "explicit_bzero(&peer", "explicit_bzero(&terminal",
        "iwn_sae_engine_finish_join_retirement", "iwn_sae_tx_lifecycle_leave")
terminal = body(iwn, "iwn_sae_tx_queue_terminal")
assert terminal.rindex("IOSimpleLockUnlock") < terminal.index("iwn_sae_engine_wake_join_retirement")
txworker = body(iwn, "iwn_sae_tx_task")
ordered(txworker[txworker.rindex("explicit_bzero(&event"):],
        "explicit_bzero(&event", "iwn_sae_tx_finish_join_retirement",
        "else if (sc->sc_sae_engine_lock != NULL)", "iwn_sae_engine_wake_join_retirement",
        "iwn_sae_tx_lifecycle_leave")
for name in ("iwn_sae_tx_stop_begin", "iwn_sae_tx_cancel_all", "iwn_sae_tx_purge"):
    assert "sc_sae_tx_join_failure_generation = 0" in body(iwn, name)
cancel = body(iwn, "cancelSaeAuthFrame")
ordered(cancel[cancel.rindex("IOSimpleLockUnlock"):],
        "IOSimpleLockUnlock", "iwn_sae_engine_wake_join_retirement",
        "iwn_sae_tx_lifecycle_leave")
print("PASS: production scan-to-failure wiring, exact physical retirement, cleanup worker and DMA wake/lifetime ordering")
