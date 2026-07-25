#!/usr/bin/env bash
# Static guard for the bounded WCL 5 GHz dwell adjustments.  They must not
# introduce a wildcard active scan or relax DFS/serving-BSS limits.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
iwn_hpp = (root / "itlwm/hal_iwn/ItlIwn.hpp").read_text()


def fail(message):
    raise SystemExit(f"IWN WCL 5 GHz dwell contract: {message}")


def require(text, needle, label):
    if needle not in text:
        fail(f"missing {label}: {needle}")


def forbid(text, needle, label):
    if needle in text:
        fail(f"unexpected {label}: {needle}")


def body(text, marker):
    start = text.find(marker)
    if start < 0:
        fail(f"missing {marker}")
    opening = text.find("{", start)
    if opening < 0:
        fail(f"missing body for {marker}")
    depth = 0
    for pos in range(opening, len(text)):
        if text[pos] == "{":
            depth += 1
        elif text[pos] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:pos]
    fail(f"unterminated {marker}")


submit = body(iwn, "int ItlIwn::\niwn_scan_submit")
require(iwn_hpp, "bool, bool, bool, u_int64_t, u_int32_t,",
        "WCL ownership in submit ABI")
require(iwn, "bool wcl_scan,", "explicit WCL submit ownership")
for needle, label in (
    ("bool wcl_foreground_5ghz_extended_dwell = false;", "bounded dwell state"),
    ("bool wcl_background_5ghz_directed_active_dwell = false;", "background dwell state"),
    ("wcl_scan && bgscan == 0 &&", "foreground WCL guard"),
    ("ic->ic_des_esslen == 0", "undirected WCL guard"),
    ("(flags & IEEE80211_CHAN_5GHZ) != 0", "5 GHz-only guard"),
    ("(c->ic_flags & IEEE80211_CHAN_DFS) == 0", "DFS exclusion"),
    ("dwell_passive = MAX(dwell_passive, 130);", "130 ms dwell floor"),
    ("wcl_scan && bgscan != 0 &&\n        is_active != 0 &&", "background directed WCL guard"),
    ("dwell_passive > dwell_active", "serving-BSS dwell cap guard"),
    ("dwell_active = MAX(dwell_active,\n                MIN((uint16_t)40, (uint16_t)(dwell_passive - 1)));", "40 ms active dwell cap"),
    ("if (ic->ic_des_esslen != 0)\n            chan->flags |= htole32(IWN_CHAN_NPBREQS(1));", "directed-SSID-only probe template selection"),
):
    require(submit, needle, label)
forbid(submit, "else if (wcl_foreground_5ghz_extended_dwell)",
       "wildcard active probing")
foreground_dwell_begin = submit.find("if (wcl_foreground_5ghz_extended_dwell &&")
background_dwell_begin = submit.find(
    "if (wcl_background_5ghz_directed_active_dwell &&",
    foreground_dwell_begin)
if foreground_dwell_begin < 0 or background_dwell_begin < 0:
    fail("missing separated foreground/background dwell guards")
foreground_dwell = submit[foreground_dwell_begin:background_dwell_begin]
forbid(foreground_dwell, "(c->ic_flags & (IEEE80211_CHAN_PASSIVE |\n                            IEEE80211_CHAN_DFS)) == 0",
       "regulatory-passive WCL listening exclusion")
require(submit[background_dwell_begin:],
        "(c->ic_flags & (IEEE80211_CHAN_PASSIVE |\n                            IEEE80211_CHAN_DFS)) == 0",
        "background active dwell passive/DFS exclusion")
forbid(submit, "dwell_active = MAX(dwell_active, 40);",
       "uncapped background active dwell")
require(submit, "if (c->ic_flags & IEEE80211_CHAN_PASSIVE)\n            chan->flags |= htole32(IWN_CHAN_PASSIVE);",
        "passive channel flag preserved while dwell is extended")
require(submit, "hdr->crc_threshold = is_active ?\n            IWN_GOOD_CRC_TH_DEFAULT : IWN_GOOD_CRC_TH_DISABLED;",
        "unchanged new-scan passive CRC semantics")
require(submit, "hdr->crc_threshold = is_active ?\n            IWN_GOOD_CRC_TH_DEFAULT : IWN_GOOD_CRC_TH_NEVER;",
        "unchanged legacy passive CRC semantics")
passive_dwell = body(iwn, "uint16_t ItlIwn::\niwn_get_passive_dwell_time")
require(passive_dwell, "return (iwn_limit_dwell(sc, passive));",
        "unchanged serving-BSS passive dwell limiter")

continuation = body(iwn, "static bool\niwn_scan_lease_begin_continuation")
require(iwn, "bool *out_wcl_scan", "continuation ownership output")
require(continuation, "*out_wcl_scan = iwn_scan_lease_owner_is_wcl(",
        "lock-protected continuation ownership")
continue_submit = body(iwn, "int ItlIwn::\niwn_scan_continue")
require(continue_submit, "iwn_scan_lease_begin_continuation(sc, &serial, &wcl_scan)",
        "continuation WCL ownership capture")
require(continue_submit, "wcl_scan, 0, 0,", "continuation WCL ownership forward")

print("IWN WCL 5 GHz dwell contract: PASS")
PY
