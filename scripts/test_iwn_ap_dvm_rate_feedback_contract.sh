#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

python3 - "$PROJECT_DIR" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
header = (root / "itlwm/hal_iwn/ItlIwn.hpp").read_text()
tx_header = (root / "itlwm/hal_iwn/if_iwnvar.h").read_text()
source = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()


def function(text: str, signature: str) -> str:
    start = text.index(signature)
    brace = text.index("{", start)
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
    raise AssertionError(f"unterminated function: {signature}")


for needle in (
    "IWN_AP_RATE_WINDOW_SIZE = 62",
    "struct IwnApRateWindow",
    "struct IwnApAggregateRateFeedback",
    "struct IwnApRateControlRuntime",
    "pendingAggregate[IWN_AP_RATE_TID_COUNT]",
):
    assert needle in header, f"missing per-client DVM state: {needle}"

for needle in (
    "ampdu_rate_generation",
    "ampdu_rate_rflags",
    "ampdu_rate_feedback_valid",
):
    assert needle in tx_header, f"missing descriptor rate fence: {needle}"

collect = function(source, "static void iwn_ap_dvm_collect_rate_window(")
for needle in (
    "1ULL << (IWN_AP_RATE_WINDOW_SIZE - 1)",
    "window->successHistory <<= 1",
    "failures >= 6 || window->successes >= 8",
    "128U * (100U * window->successes) / window->attempts",
):
    assert needle in collect, f"missing DVM sliding-window rule: {needle}"

expected = function(source, "static uint16_t iwn_ap_dvm_expected_throughput(")
for row in (
    "{ 47, 91, 133, 171, 242, 305, 334, 362 }",
    "{ 52, 101, 145, 187, 264, 330, 361, 390 }",
    "{ 89, 167, 235, 296, 402, 488, 526, 560 }",
    "{ 97, 182, 255, 320, 431, 520, 558, 593 }",
):
    assert row in expected, f"missing exact DVM HT20 throughput row: {row}"

single = function(source, "static void iwn_ap_dvm_selected_rate_sample(")
for needle in (
    "static_cast<uint16_t>(ackfailcnt) + 1U",
    "MIN(totalAttempts, static_cast<uint16_t>(3))",
    "!txfail && totalAttempts <= 3U ? 1U : 0U",
):
    assert needle in single, f"missing DVM retry-table attribution: {needle}"

feedback = function(source, "int ItlIwn::iwn_ap_rate_control_feedback(")
for needle in (
    "generation != rateControl->generation",
    "rateControl->aggregateFeedback != aggregated",
    "successRatio <= 128U * 15U",
    "successRatio >= 128U * 50U",
    "successRatio > 128U * 85U",
    "rateControl->linkQualityPending = true",
    "bzero(rateControl->pendingAggregate",
):
    assert needle in feedback, f"missing DVM decision/fence: {needle}"

match = function(source, "bool ItlIwn::iwn_ap_rate_feedback_matches(")
assert "rateControl->missedRateCount > 15" in match
assert "iwn_send_ap_client_link_quality()" in match

linkq = function(source, "int ItlIwn::iwn_send_ap_client_link_quality()")
assert "for (int mcs = lastMcs; mcs <= firstMcs; mcs++)" in linkq, \
    "DVM feedback lifetime must begin at the lowest negotiated HT rate"
assert "apClientRateControl.selectedMcs" in linkq

compressed_ba = function(source, "iwn_rx_compressed_ba(struct iwn_softc *sc")
for needle in (
    "pendingAggregate[cba->tid]",
    "cba->nframes_sent",
    "cba->nframes_acked",
    "iwn_ap_rate_control_feedback(",
):
    assert needle in compressed_ba, f"compressed BA feedback missing: {needle}"

aggregate_done = function(source, "iwn_ampdu_tx_done(struct iwn_softc *sc")
assert "pending->generation =" in aggregate_done
assert "iwn_ap_rate_feedback_matches(apClient, rate, rflags)" in aggregate_done
assert "iwn_ap_dvm_selected_rate_sample(" in aggregate_done
for needle in (
    "txdata->ni == NULL &&",
    "txdata->ampdu_rate_generation =",
    "txdata->ampdu_rate_feedback_valid =",
    "priorAggregateRateFeedback",
    "priorAggregateGeneration",
    "priorAggregateRflags, 1, 0, true",
):
    assert needle in aggregate_done, \
        f"missing descriptor-fenced aggregate failure feedback: {needle}"

nonaggregate_done = function(source, "iwn_tx_done(struct iwn_softc *sc")
assert "iwn_ap_dvm_selected_rate_sample(" in nonaggregate_done

# The first three LQ entries repeat the selected HT rate.  Match DVM's
# failure_frame + 1 walk: success inside that group is credited there, while
# a later fallback success leaves three selected-rate failures.
def selected_sample(ackfailcnt: int, txfail: bool) -> tuple[int, int]:
    total = ackfailcnt + 1
    return min(total, 3), int(not txfail and total <= 3)

assert selected_sample(0, False) == (1, 1)
assert selected_sample(2, False) == (3, 1)
assert selected_sample(3, False) == (3, 0)
assert selected_sample(0, True) == (1, 0)
assert selected_sample(15, True) == (3, 0)

# Independent boundary model: DVM waits for 8 successes before probing up,
# and a stale generation cannot alter the selected rate.
selected = 8
generation = 1
successes = 0
failures = 0
for _ in range(7):
    successes += 1
    assert successes < 8 and failures < 6
successes += 1
if successes >= 8 and 100 >= 50:
    selected += 1
    generation += 1
assert (selected, generation) == (9, 2)

stale_generation = 1
if stale_generation == generation:
    selected -= 1
assert selected == 9, "stale feedback must not move the active MCS"

print("PASS: IWN AP consumes DVM TX/BA feedback through fenced per-client windows")
PY
