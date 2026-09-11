#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

python3 - "$root" <<'PY'
from pathlib import Path
import sys

source = (Path(sys.argv[1]) / "itlwm/hal_iwn/ItlIwn.cpp").read_text()


def body(signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise SystemExit(f"FAIL: missing function: {signature}")
    opening = source.find("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening:index + 1]
    raise SystemExit(f"FAIL: unterminated function: {signature}")


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise SystemExit(f"FAIL: missing {label}: {needle}")


policy = body("iwn_dvm_use_rts_for_aggregation(")
for needle, label in (
    ("IWN_HW_REV_TYPE_1000", "1000-family RTS policy"),
    ("IWN_HW_REV_TYPE_6005", "6235/6035 RTS policy"),
    ("IWN_HW_REV_TYPE_2000", "2000-family RTS policy"),
    ("default:", "5000/4965 exclusion"),
):
    require(policy, needle, label)

attach = body("iwn_attach(struct iwn_softc")
require(attach,
        "IEEE80211_C_QOS | IEEE80211_C_TX_AMPDU |",
        "TX A-MPDU capability for every HT-capable iwn product")
if "PCI_PRODUCT_INTEL_WL_6235" in attach:
    raise SystemExit("FAIL: attach retains a 6235-only aggregation bypass")

tx = body("iwn_tx(struct iwn_softc *sc")
for needle, label in (
    ("ring->qid >= sc->first_agg_txq", "aggregate queue classifier"),
    ("sc->hw_type != IWN_HW_REV_TYPE_4965", "post-4965 protection policy"),
    ("flags |= IWN_TX_NEED_PROTECTION", "PROT_REQUIRE TX command"),
):
    require(tx, needle, label)

linkq = body("iwn_set_link_quality(struct iwn_softc *sc")
for needle, label in (
    ("sc->sc_tx_ba[tid].wn != wn", "hardware RA/TID ownership"),
    ("ni->ni_tx_ba[tid].ba_winsize", "negotiated BA window"),
    ("static_cast<uint16_t>(IWN_AMPDU_MAX)", "63-frame LQ clamp"),
    ("linkq.ampdu_max = aggregateLimit", "station aggregate limit"),
    ("txAggregationActive && iwn_dvm_use_rts_for_aggregation(sc)",
     "active aggregate RTS policy"),
    ("IWN_LINK_QUAL_FLAGS_SET_STA_TLC_RTS", "TLC_RTS Link Quality flag"),
):
    require(linkq, needle, label)
if "IEEE80211_F_USEPROT" in linkq:
    raise SystemExit("FAIL: aggregate RTS remains tied to legacy USEPROT")

ap_linkq = body("int ItlIwn::iwn_send_ap_client_link_quality()")
require(ap_linkq, "iwn_dvm_use_rts_for_aggregation(&com)",
        "AP per-family TLC_RTS policy")
require(ap_linkq, "IWN_LINK_QUAL_FLAGS_SET_STA_TLC_RTS",
        "AP TLC_RTS Link Quality flag")
if "com.hw_type != IWN_HW_REV_TYPE_4965" in ap_linkq:
    raise SystemExit("FAIL: AP TLC_RTS is enabled on non-RTS 5000 families")

start = body("iwn_ampdu_tx_start(struct ieee80211com *ic")
for needle, label in (
    ("qid >= sc->ntxqs", "queue upper-bound fence"),
    ("sc->sc_tx_ba[tid].wn = wn", "hardware RA/TID commit"),
    ("error = that->iwn_set_link_quality(sc, ni)",
     "post-ADDBA LQ update"),
    ("ops->ampdu_tx_stop(sc, tid, ba->ba_winstart, &retired)",
     "failed-LQ scheduler rollback"),
    ("sc->sc_tx_ba[tid].wn = NULL", "failed-LQ owner rollback"),
):
    require(start, needle, label)
if start.index("sc->sc_tx_ba[tid].wn = wn") > start.index(
        "error = that->iwn_set_link_quality(sc, ni)"):
    raise SystemExit("FAIL: LQ update precedes aggregate queue ownership")

stop = body("iwn_ampdu_tx_stop(struct ieee80211com *ic")
require(stop, "sc->sc_tx_ba[tid].wn = NULL", "DELBA owner teardown")
require(stop, "(void)that->iwn_set_link_quality(sc, ni)",
        "post-DELBA LQ update")
if stop.index("sc->sc_tx_ba[tid].wn = NULL") > stop.index(
        "(void)that->iwn_set_link_quality(sc, ni)"):
    raise SystemExit("FAIL: DELBA LQ update precedes owner teardown")

scheduler = body("iwn5000_ampdu_tx_start(struct iwn_softc *sc")
for needle, label in (
    ("ni->ni_tx_ba[tid].ba_winsize", "negotiated scheduler window"),
    ("static_cast<uint16_t>(IWN_AMPDU_MAX)", "63-frame SCD clamp"),
    ("IWN5000_SCHED_QUEUE_OFFSET(qid), 0", "recycled context reset"),
    ("static_cast<uint32_t>(frameLimit) << 16 | frameLimit",
     "identical SCD window/frame limit"),
):
    require(scheduler, needle, label)

print("PASS: IWN STA DVM transmit A-MPDU scheduler/protection contract")
PY
