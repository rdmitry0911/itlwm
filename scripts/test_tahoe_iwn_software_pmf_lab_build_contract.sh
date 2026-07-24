#!/usr/bin/env bash
# Contract for the separately staged IWN software-PMF lab artifact.  This
# checks that the sole opt-in is compile-time and cannot contaminate the
# normal or AP-mode build outputs.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

bash -n "$root/scripts/build_tahoe.sh"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


source = (Path(sys.argv[1]) / "scripts/build_tahoe.sh").read_text()


def fail(message: str) -> None:
    raise SystemExit(f"IWN software-PMF lab build contract: {message}")


def require(token: str, label: str) -> None:
    if token not in source:
        fail(f"missing {label}: {token}")


def ordered(label: str, *tokens: str) -> None:
    cursor = 0
    for token in tokens:
        position = source.find(token, cursor)
        if position < 0:
            fail(f"{label} missing ordered token: {token}")
        cursor = position + len(token)


for token in (
    "BUILD_IWN_SOFTWARE_PMF_LAB",
    "--iwn-software-pmf-lab",
    "IWN_SOFTWARE_PMF_LAB_BUILD=1",
    "Tahoe-IwnSoftwarePmfLab",
    "DerivedData-iwn-software-pmf-lab",
    "IWN SAE transport compiled; WPA3 association remains disabled.",
):
    require(token, "lab-only build surface")

ordered("lab flag selects isolated artifact",
        "if [ \"$IWN_SOFTWARE_PMF_LAB\" -eq 1 ]; then",
        "VARIANT_LABEL=\"Tahoe-IwnSoftwarePmfLab\"",
        "DERIVED_DATA=\"$PROJECT_DIR/DerivedData-iwn-software-pmf-lab\"",
        "EXTRA_PP=\"$EXTRA_PP IWN_SOFTWARE_PMF_LAB_BUILD=1\"")
ordered("AP exploration cannot opt in IWN PMF",
        "if [ \"$OPT_OUT_STA_ONLY\" -eq 1 ] && [ \"$IWN_SOFTWARE_PMF_LAB\" -eq 1 ]; then",
        "--opt-out cannot be combined with --iwn-software-pmf-lab",
        "exit 2")

# The test artifact is explicit in the compiler setting, not a mutable guest
# boot argument.  That keeps activation reproducible in the disposable Tahoe
# guest where NVRAM boot-args are deliberately unavailable.
iwn = (Path(sys.argv[1]) / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
if "PE_parse_boot_argn(\"itlwm_iwn_software_pmf\"" in iwn:
    fail("IWN PMF lab activation still depends on an unavailable boot argument")
if "#define IWN_SOFTWARE_PMF_LAB_BUILD 0" not in iwn:
    fail("missing safe production default")
if "#if IWN_SOFTWARE_PMF_LAB_BUILD" not in iwn:
    fail("missing lab-build capability predicate")

print("PASS: IWN software-PMF lab build is isolated, STA-only, and does not enable WPA3 association")
PY
