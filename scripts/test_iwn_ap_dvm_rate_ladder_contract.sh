#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

python3 - "$PROJECT_DIR" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
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


linkq = function(source, "int ItlIwn::iwn_send_ap_client_link_quality()")

required = (
    "Match rs_fill_link_cmd() in Intel DVM",
    "const int htMcs[2] = { selectedMcs, lowerMcs };",
    "attempt < 3 && retry < IWN_MAX_TX_RETRIES",
    "linkq.retry[retry].plcp = rate->ht_plcp;",
    "static const uint8_t htToLegacy[] = {",
    "4, 5, 6, 7, 8, 9, 10, 11",
    "static const int8_t legacyPrevious[] = {",
    "-1, 0, 1, 5, 2, 4, 3, 6, 7, 8, 9, 10",
    "legacyIndex = legacyPrevious[legacyIndex];",
    "while (retry < IWN_MAX_TX_RETRIES)",
)
for needle in required:
    assert needle in linkq, f"IWN DVM AP rate ladder missing: {needle}"

ht_groups = linkq.index("const int htMcs[2]")
mimo_delimiter = linkq.index("linkq.mimo = static_cast<uint8_t>(retry)")
legacy_ladder = linkq.index("static const uint8_t htToLegacy[]")
minimum_fill = linkq.rindex("while (retry < IWN_MAX_TX_RETRIES)")
assert ht_groups < mimo_delimiter < legacy_ladder < minimum_fill, \
    "IWN DVM retry groups must precede MIMO delimiter, legacy fallback and fill"

old_ladder = "for (int mcs = firstMcs;\n             mcs >= lastMcs && retry"
assert old_ladder not in linkq, \
    "IWN AP must not publish every HT MCS as a single retry"

print("PASS: IWN AP link quality uses the Intel DVM HT/legacy retry ladder")
PY
