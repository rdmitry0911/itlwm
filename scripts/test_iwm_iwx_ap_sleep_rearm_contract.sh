#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

python3 - "$repo_root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
owner = (root / "AirportItlwm/AirportItlwmAPSTAOwner.cpp").read_text()
iwm = (root / "itlwm/hal_iwm/ItlIwm.cpp").read_text()
iwx = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()
runtime = (root / "include/HAL/ItlApFirmwareRuntime.hpp").read_text()


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


prepare = body(owner, "void AirportItlwmAPSTAOwner::prepareForRadioReset()")
assert "owner->setAPSTADatapathEnabled(false);" in prepare
assert "radioResetResumePending = true;" in prepare
assert "stopLower()" in prepare, "empty AP must still terminate normally"

resume = body(owner, "IOReturn AirportItlwmAPSTAOwner::resumeAfterRadioReset()")
assert resume.index("ic->ic_state != IEEE80211_S_RUN") < \
       resume.index("startLowerIfReady()"), \
       "retained GO must re-arm after the primary STA boundary"
assert "radioResetResumePending = false;" in resume

for family, source, lower_stop in (
    ("IWM", iwm, "iwm_stop_ap_resources(&com, &apRuntime)"),
    ("IWX", iwx, "iwx_stop_ap_mode(&com, &apRuntime)"),
):
    disable = body(source, "disable(IONetworkInterface *netif)")
    assert "apRuntime.stage != kItlApFirmwareResourceIdle" in disable, \
        f"{family} must notice retained GO resources"
    assert lower_stop in disable, f"{family} must retire GO before radio stop"
    assert disable.index(lower_stop) < disable.index("DVACT_QUIESCE"), \
        f"{family} GO teardown must precede generic firmware teardown"
    assert disable.index(lower_stop) < disable.index("already !IFF_UP"), \
        f"{family} cleanup must survive an already-lowered primary ifnet"
    start = body(source,
        "startAPMode(const struct ItlHalApConfig *config)")
    assert "apRuntime.stage != kItlApFirmwareResourceIdle" in start
    assert "kIOReturnBusy" in start, \
        f"{family} stale pre-sleep state must remain fail-closed"

crypto_reset = body(runtime,
    "itl_ap_firmware_client_crypto_reset(")
for forbidden in ("groupKeyInstalled = true", "clientAuthorized = true"):
    assert forbidden not in crypto_reset
for required in (
    "clientAuthorized = false",
    "clientPairwiseKeyInstalled = false",
    "explicit_bzero(client->clientPairwiseKey",
    "explicit_bzero(client->clientRxPn",
):
    assert required in crypto_reset, \
        f"radio reset must close client security state: {required}"

print("PASS: paired IWM/IWX AP resources re-arm after radio reset")
PY
