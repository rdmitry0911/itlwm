#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

python3 - "$PROJECT_DIR" <<'PY'
from pathlib import Path
import re
import sys

root = Path(sys.argv[1])
project = (root / "itlwm.xcodeproj/project.pbxproj").read_text()

configuration_ids = (
    "F8E94CF32B9ABFE20081A3C4",  # Tahoe Debug
    "F8E94CF42B9ABFE20081A3C4",  # Tahoe Release
)
for configuration_id in configuration_ids:
    match = re.search(
        rf"{configuration_id} /\* (?:Debug|Release) \*/ = \{{(.*?)\n\t\t\}};",
        project,
        re.DOTALL,
    )
    assert match is not None, f"missing Tahoe build configuration {configuration_id}"
    configuration = match.group(1)
    for definition in (
        "IEEE80211_OPT_OUT_STA_ONLY",
        "IEEE80211_APSTA_STATION_EVENT_OPT_OUT",
    ):
        assert definition in configuration, (
            f"Tahoe product configuration {configuration_id} does not admit "
            f"the AP runtime through {definition}"
        )

iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
iwm = (root / "itlwm/hal_iwm/ItlIwm.cpp").read_text()
iwx = (root / "itlwm/hal_iwx/IwxApGoCapability.hpp").read_text()
header = (root / "itl80211/openbsd/net80211/ieee80211_var.h").read_text()

assert "#ifdef IEEE80211_APSTA_STATION_EVENT_OPT_OUT" in iwn
assert "#if !defined(IEEE80211_OPT_OUT_STA_ONLY)" in iwm
assert "#if !defined(IEEE80211_OPT_OUT_STA_ONLY)" in iwx
assert "#undef IEEE80211_STA_ONLY" in header

print("PASS: Tahoe product admits the shared IWN/IWM/IWX AP runtime")
PY
