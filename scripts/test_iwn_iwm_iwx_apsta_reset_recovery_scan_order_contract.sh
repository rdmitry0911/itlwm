#!/usr/bin/env bash
# Keep retained HostAP replay behind an exact primary retained-ESS scan.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import re
import sys


root = Path(sys.argv[1])


def fail(message: str) -> None:
    raise SystemExit(f"APSTA reset recovery scan order: {message}")


def body(text: str, name: str, label: str) -> str:
    match = re.search(
        r"^" + re.escape(name) + r"\s*\([^;{}]*\)\s*(?:const\s*)?\{",
        text,
        re.M | re.S,
    )
    if match is None:
        fail(f"missing {label}")
    opening = text.rfind("{", match.start(), match.end())
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:index]
    fail(f"unterminated {label}")


def ordered(text: str, label: str, *tokens: str) -> None:
    cursor = 0
    for token in tokens:
        position = text.find(token, cursor)
        if position < 0:
            fail(f"{label} missing ordered token: {token}")
        cursor = position + len(token)


hal = (root / "include/HAL/ItlHalService.hpp").read_text()
if "virtual bool isPrimaryStaRecoveryScanPending() const" not in hal:
    fail("common HAL lacks the fail-closed recovery-scan query")

families = {
    "iwn": (
        root / "itlwm/hal_iwn/ItlIwn.hpp",
        root / "itlwm/hal_iwn/ItlIwn.cpp",
        "bool ItlIwn::isPrimaryStaRecoveryScanPending",
    ),
    "iwm": (
        root / "itlwm/hal_iwm/ItlIwm.hpp",
        root / "itlwm/hal_iwm/ItlIwm.cpp",
        "bool ItlIwm::\nisPrimaryStaRecoveryScanPending",
    ),
    "iwx": (
        root / "itlwm/hal_iwx/ItlIwx.hpp",
        root / "itlwm/hal_iwx/ItlIwx.cpp",
        "bool ItlIwx::\nisPrimaryStaRecoveryScanPending",
    ),
}

for family, (header_path, source_path, signature) in families.items():
    header = header_path.read_text()
    source = source_path.read_text()
    if "bool isPrimaryStaRecoveryScanPending() const override;" not in header:
        fail(f"{family} HAL lacks recovery-scan override")
    query = body(source, signature, f"{family} recovery-scan query")
    for token in (
        "ic->ic_opmode != IEEE80211_M_STA",
        "ic->ic_state != IEEE80211_S_SCAN",
        "IEEE80211_F_AUTO_JOIN",
        "ic->ic_des_esslen != 0",
        "sc->sc_sae_wcl_credential_lock == NULL",
        "sc->sc_sae_bss_loss_recovery_armed",
        "sc->sc_sae_bss_loss_recovery_generation != 0",
        "sc->sc_sae_wcl_credential_active",
        "!sc->sc_sae_wcl_credential_staged",
        "!sc->sc_sae_wcl_credential_pending",
        "sc->sc_sae_wcl_credential.request_generation ==",
        "itl_sae_wcl_credential_is_well_formed",
    ):
        if token not in query:
            fail(f"{family} recovery-scan query lacks fence: {token}")
    ordered(
        query,
        f"{family} credential leaf",
        "IOSimpleLockLock(sc->sc_sae_wcl_credential_lock);",
        "pending =",
        "IOSimpleLockUnlock(sc->sc_sae_wcl_credential_lock);",
        "return pending;",
    )

owner = (root / "AirportItlwm/AirportItlwmAPSTAOwner.cpp").read_text()
prepare = body(
    owner,
    "void AirportItlwmAPSTAOwner::prepareRetainedLowerReset",
    "retained lower-reset preparation",
)
ordered(
    prepare,
    "retained AP reset dependency",
    "isPrimaryStaRecoveryScanPending()",
    "radioResetWaitForPrimaryStaRun =",
    "primaryRecoveryScanPending",
    "radioResetResumePending = true;",
)
if "ic->ic_state == IEEE80211_S_SCAN" in prepare:
    fail("generic SCAN must not delay AP-only replay")

resume = body(
    owner,
    "IOReturn AirportItlwmAPSTAOwner::resumeAfterRadioReset",
    "retained AP reset replay",
)
ordered(
    resume,
    "primary-before-PAN replay",
    "if (!radioResetWaitForPrimaryStaRun &&",
    "isPrimaryStaRecoveryScanPending()",
    "radioResetWaitForPrimaryStaRun = true;",
    "if (radioResetWaitForPrimaryStaRun)",
    "ic->ic_state != IEEE80211_S_RUN",
    "return kIOReturnNotReady;",
    "const IOReturn result = startLowerIfReady();",
)

print("PASS: IWN/IWM/IWX defer retained HostAP replay for exact STA recovery scans")
PY
