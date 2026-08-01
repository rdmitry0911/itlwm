#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

python3 - "$PROJECT_DIR" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
firmware = (root / "include/HAL/ItlApFirmwareRuntime.hpp").read_text()
open_runtime = (root / "include/HAL/ItlApOpenRuntime.hpp").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
iwn_h = (root / "itlwm/hal_iwn/ItlIwn.hpp").read_text()
iwm = (root / "itlwm/hal_iwm/ItlIwm.cpp").read_text()
iwm_h = (root / "itlwm/hal_iwm/ItlIwm.hpp").read_text()
iwx = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()
iwx_h = (root / "itlwm/hal_iwx/ItlIwx.hpp").read_text()
probe = (root / "AirportItlwmLabAPProbe/airport_itlwm_lab_ap_probe.c").read_text()
owner = (root / "AirportItlwm/AirportItlwmAPSTAOwner.cpp").read_text()
owner_h = (root / "AirportItlwm/AirportItlwmAPSTAOwner.hpp").read_text()


def require(source: str, needle: str, label: str) -> None:
    assert needle in source, f"missing {label}: {needle}"


for needle, label in (
    ("bool hidden;", "shared closednet state"),
    ("itl_ap_beacon_set_hidden", "transactional beacon SSID rewriter"),
    ("element[1] = 0;", "zero-length hidden beacon SSID"),
    ("memmove(element + 2 + ssidLength", "visible SSID restoration"),
    ("runtime->config.beaconTemplateLength", "owned template length update"),
):
    require(firmware, needle, label)

for needle, label in (
    ("runtime->hidden && wildcard", "wildcard probe suppression"),
    ("runtime->hidden &&", "hidden directed-probe branch"),
    ("runtime->config.ssidLength", "real directed-probe SSID length"),
    ("memcpy(result->reply + outputOffset, runtime->ssid", "real directed-probe SSID"),
):
    require(open_runtime, needle, label)

for header, label in ((iwn_h, "IWN"), (iwm_h, "IWM"), (iwx_h, "IWX")):
    require(header, "IOReturn setAPHidden(bool hidden) override;",
            f"{label} closednet HAL override")

for source, sender, label in (
    (iwn, "iwn_send_ap_beacon(&apFirmwareConfig)", "IWN"),
    (iwm, "iwm_ap_send_beacon_template(&com, &apRuntime)", "IWM"),
    (iwx, "iwx_ap_send_beacon_template(&com, &apRuntime)", "IWX"),
):
    require(source, "setAPHidden(bool hidden)", f"{label} closednet implementation")
    require(source, sender, f"{label} live beacon upload")
    require(source, "previousHidden", f"{label} transactional rollback")

for needle, label in (
    ("apHidden && wildcard", "IWN wildcard probe suppression"),
    ("apHidden &&", "IWN directed-probe real SSID branch"),
    ("memcpy(response + outputOffset, apFirmwareSsid", "IWN real directed-probe SSID"),
    ("apFirmwareStage < IWN_AP_STAGE_FIRST_BEACON", "IWN queued-start closednet acceptance"),
    ("if (queuedBeforeFirstBeacon)", "IWN first-beacon template ownership"),
    ("apHidden = false;", "IWN reset clears closednet state"),
):
    require(iwn, needle, label)

for needle, label in (
    ("APPLE80211_IOC_HOST_AP_MODE_HIDDEN 336", "public hidden selector"),
    ('strcmp(hidden_mode, "toggle")', "live hidden/visible toggle mode"),
    ("hidden.hidden04 = 0;", "visible restoration probe"),
):
    require(probe, needle, label)

for needle, label in (
    ("state.hiddenNetworkFlag0d != 0", "retained public closednet state"),
    ("state.hiddenNetworkFlag0d = 0;", "explicit-stop closednet reset"),
    ("APSTA replaying retained hidden AP profile ",
     "observable closednet wake replay"),
    ("owner->fHalService->setAPHidden(true)",
     "lower closednet wake replay"),
    ("(void)owner->fHalService->stopAPMode();",
     "failed replay rollback"),
):
    require(owner, needle, label)

for needle, label in (
    ("lowerAssociatedStaCount", "lower all-peer sleep census"),
    ("lowerAssociatedStaMacs", "lower all-peer identity census"),
):
    require(owner_h, needle, label)

for needle, label in (
    ("noteLowerAssociatedStation(macAddr);",
     "pre-public-filter firmware association census"),
    ("forgetLowerAssociatedStation(macAddr);",
     "firmware disassociation census"),
    ("lowerAssociatedStaCount == 0", "all-peer power-off gate"),
    ("clearLowerAssociatedStations();", "radio-reset lower census reset"),
):
    require(owner, needle, label)

note = owner.index("noteLowerAssociatedStation(macAddr);")
public_filter = owner.index("associationIsAdmitted(", note)
assert note < public_filter, "lower census must precede hidden Apple-IE filter"

start = owner.index("IOReturn ret = owner->fHalService->startAPMode(&cfg);")
replay = owner.index("owner->fHalService->setAPHidden(true)", start)
publish = owner.index("lifecycle = kAirportItlwmAPSTAOwnerRunning;", replay)
assert start < replay < publish, "closednet must replay before AP running publication"

print("PASS: IWN/IWM/IWX live hidden-AP beacon and directed-probe contract")
PY
