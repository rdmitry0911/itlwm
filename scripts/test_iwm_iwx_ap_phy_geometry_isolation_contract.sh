#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

python3 - "$repo_root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
iwm = (root / "itlwm/hal_iwm/ctxt.cpp").read_text()
iwx = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()


def body(source: str, signature: str) -> str:
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


for family, source, width_name, ctrl_name, mode20, ctrl20 in (
    ("IWM", iwm, "iwm_get_channel_width", "iwm_get_ctrl_pos",
     "IWM_PHY_VHT_CHANNEL_MODE20", "IWM_PHY_VHT_CTRL_POS_1_BELOW"),
    ("IWX", iwx, "iwx_get_channel_width", "iwx_get_ctrl_pos",
     "IWX_PHY_VHT_CHANNEL_MODE20", "IWX_PHY_VHT_CTRL_POS_1_BELOW"),
):
    width = body(source, f"{width_name}(struct ieee80211com *ic,")
    ctrl = body(source, f"{ctrl_name}(struct ieee80211com *ic,")

    for function, label in ((width, "width"), (ctrl, "control position")):
        if "ic->ic_bss->ni_chan != c" not in function:
            raise SystemExit(
                f"FAIL: {family} {label} may inherit another PHY's BSS")

    if mode20 not in width:
        raise SystemExit(f"FAIL: {family} secondary PHY has no HT20 default")
    if ctrl20 not in ctrl:
        raise SystemExit(
            f"FAIL: {family} secondary PHY has no 20 MHz control default")
    if "c->ic_freq - c->ic_center_freq1" not in ctrl:
        raise SystemExit(
            f"FAIL: {family} control position does not use target channel")
    if "ic->ic_bss->ni_chan->ic_freq" in ctrl:
        raise SystemExit(
            f"FAIL: {family} control position still reads global BSS channel")

print("PASS: IWM/IWX AP PHY geometry is isolated from concurrent STA BSS")
PY
