#!/usr/bin/env bash
# Regression gate for a retained SAE ESS across an unexpected firmware epoch.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import re
import sys


root = Path(sys.argv[1])
families = {
    "iwn": {
        "var": root / "itlwm/hal_iwn/if_iwnvar.h",
        "hpp": root / "itlwm/hal_iwn/ItlIwn.hpp",
        "engine": root / "itlwm/hal_iwn/ItlIwn.cpp",
        "runtime": root / "itlwm/hal_iwn/ItlIwn.cpp",
        "init": "iwn_init",
        "stop": "iwn_stop",
    },
    "iwm": {
        "var": root / "itlwm/hal_iwm/if_iwmvar.h",
        "hpp": root / "itlwm/hal_iwm/ItlIwm.hpp",
        "engine": root / "itlwm/hal_iwm/IwmSaeEngine.inc",
        "runtime": root / "itlwm/hal_iwm/mac80211.cpp",
        "init": "iwm_init",
        "stop": "iwm_stop",
    },
    "iwx": {
        "var": root / "itlwm/hal_iwx/if_iwxvar.h",
        "hpp": root / "itlwm/hal_iwx/ItlIwx.hpp",
        "engine": root / "itlwm/hal_iwx/IwxSaeEngine.inc",
        "runtime": root / "itlwm/hal_iwx/ItlIwx.cpp",
        "init": "iwx_init_internal",
        "stop": "iwx_stop_internal",
    },
}


def fail(message: str) -> None:
    raise SystemExit(f"Tahoe SAE driver-reset reconnect: {message}")


def body(text: str, name: str, label: str) -> str:
    match = re.search(
        r"^" + re.escape(name) + r"\s*\([^;{}]*\)\s*\{",
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


for family, paths in families.items():
    var = paths["var"].read_text()
    hpp = paths["hpp"].read_text()
    engine = paths["engine"].read_text()
    runtime = paths["runtime"].read_text()
    prefix = family

    for token in (
        "sc_sae_bss_loss_recovery_armed",
        "sc_sae_driver_reset_recovery_pending",
        "sc_sae_bss_loss_recovery_generation",
    ):
        if token not in var:
            fail(f"{family} softc missing {token}")
    for token in (
        f"{prefix}_sae_bss_loss_arm",
        f"{prefix}_sae_bss_loss_recover",
        f"{prefix}_sae_driver_reset_recovery_prepare",
        f"{prefix}_sae_driver_reset_recovery_pending",
    ):
        if token not in hpp:
            fail(f"{family} HAL declaration missing {token}")

    clear = body(
        engine,
        f"{prefix}_sae_wcl_credential_clear_locked",
        f"{family} credential clear",
    )
    for token in (
        "sc->sc_sae_bss_loss_recovery_armed = false",
        "sc->sc_sae_driver_reset_recovery_pending = false",
        "sc->sc_sae_bss_loss_recovery_generation = 0",
    ):
        if token not in clear:
            fail(f"{family} credential clear does not retire {token}")

    prepare = body(
        engine,
        f"{prefix}_sae_driver_reset_recovery_prepare",
        f"{family} reset preparation",
    )
    ordered(
        prepare,
        f"{family} reset preparation",
        "ic->ic_state == IEEE80211_S_RUN",
        f"{prefix}_sae_bss_loss_arm(ic, ic->ic_bss)",
        "sc->sc_sae_bss_loss_recovery_armed",
        "sc->sc_sae_wcl_credential_active",
        "request_generation ==",
        "sc->sc_sae_bss_loss_recovery_generation",
        "sc->sc_sae_driver_reset_recovery_pending = true",
        "DRIVER_RESET_RECOVERY_PREPARED",
    )

    pending = body(
        engine,
        f"{prefix}_sae_driver_reset_recovery_pending",
        f"{family} reset marker validation",
    )
    for token in (
        "sc->sc_sae_driver_reset_recovery_pending",
        "sc->sc_sae_bss_loss_recovery_armed",
        "sc->sc_sae_wcl_credential_active",
        "itl_sae_wcl_credential_is_well_formed",
        "if (consume && pending)",
    ):
        if token not in pending:
            fail(f"{family} reset marker lacks validation: {token}")

    init = body(runtime, paths["init"], f"{family} init")
    ordered(
        init,
        f"{family} one-shot reset scan",
        f"{prefix}_sae_driver_reset_recovery_pending(sc, false)",
        "driver_reset_reconnect ? 0 : 1",
        "ieee80211_begin_scan",
        f"{prefix}_sae_driver_reset_recovery_pending(sc, true)",
        "DRIVER_RESET_SCAN_STARTED",
    )

    stop = body(runtime, paths["stop"], f"{family} ordinary stop")
    if f"{prefix}_sae_driver_reset_recovery_prepare" in stop:
        fail(f"{family} ordinary stop incorrectly impersonates DriverReset")

for family, runtime_path in (
    ("iwm", root / "itlwm/hal_iwm/mac80211.cpp"),
    ("iwx", root / "itlwm/hal_iwx/ItlIwx.cpp"),
):
    runtime = runtime_path.read_text()
    init_task = body(runtime, f"{family}_init_task", f"{family} init task")
    ordered(
        init_task,
        f"{family} non-fatal reset owner",
        "if (!fatal)",
        f"{family}_sae_driver_reset_recovery_prepare(sc)",
        f"{family}_stop",
    )

iwn = families["iwn"]["runtime"].read_text()
watchdog = body(iwn, "iwn_watchdog", "IWN watchdog")
ordered(
    watchdog,
    "IWN command timeout reset owner",
    "device timeout",
    "iwn_sae_driver_reset_recovery_prepare(sc)",
    "iwn_stop(ifp)",
    "task_add(systq, &sc->init_task)",
)
init_task = body(iwn, "iwn_init_task", "IWN fatal recovery task")
ordered(
    init_task,
    "IWN fatal reset owner",
    "IWN_FLAG_FATAL_RECOVERY",
    "iwn_sae_driver_reset_recovery_prepare(sc)",
    "iwn_stop(ifp)",
)

print("Tahoe SAE driver-reset reconnect contract: PASS")
PY
