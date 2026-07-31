#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

python3 - "$repo_root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
shared = (root / "include/HAL/ItlApOpenRuntime.hpp").read_text()
runtime = (root / "include/HAL/ItlApFirmwareRuntime.hpp").read_text()
hal = (root / "include/HAL/ItlHalService.hpp").read_text()
owner = (root / "AirportItlwm/AirportItlwmAPSTAOwner.cpp").read_text()
iwm_hal = (root / "itlwm/hal_iwm/ItlIwm.cpp").read_text()
iwm = (root / "itlwm/hal_iwm/mac80211.cpp").read_text()
iwm_rx = (root / "itlwm/hal_iwm/rx.cpp").read_text()
iwx = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()


def require(source: str, needle: str, label: str) -> None:
    if needle not in source:
        raise SystemExit(f"FAIL: missing {label}: {needle}")


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


for needle, label in (
    ("config->authUpper == 0x8", "WPA2-PSK profile admission"),
    ("ccmpSuite", "CCMP RSN suite validation"),
    ("pskSuite", "PSK AKM validation"),
    ("IEEE80211_CAPINFO_PRIVACY", "secure association privacy bit"),
    ("runtime->config.rsnIELength", "association response RSN IE"),
    ("ethernet.ether_type == htons(ETHERTYPE_PAE)",
     "pre-authorization EAPOL TX"),
    ("llc.llc_snap.ether_type == htons(ETHERTYPE_PAE)",
     "pre-authorization EAPOL RX"),
    ("!client->clientAuthorized", "per-client controlled-port data gate"),
    ("client->clientRxPn[tid]", "per-client per-TID replay fence"),
    ("result->disposition = kItlApOpenRxConsumed;",
     "closed-port AP-role ownership"),
):
    require(shared, needle, label)

for field in (
    "clientPairwiseKeyInstalled", "groupKeyInstalled",
    "clientPairwiseKey[16]", "groupKey[16]", "groupKeyId",
    "clientPairwiseTxPn", "groupTxPn", "clientRxPn[16]",
):
    require(runtime, field, f"runtime {field}")

for needle in (
    "kItlHalApKeyPairwise = 4",
    "kItlHalApKeyGroup = 0",
    "kItlHalApCipherAesCcm = 5",
    "kItlHalApStationAuthorize = 0x79",
    "kItlHalApStationUnauthorize = 0x7a",
):
    require(hal, needle, "recovered Apple AP key/station ABI")
require(owner, "halKey.flags = key->key_flags;",
        "Apple PTK/GTK discriminator forwarding")

for family, source in (("IWM", iwm_hal), ("IWX", iwx)):
    start = body(source, "startAPMode(const struct ItlHalApConfig *config)")
    require(start, "itl_ap_client_config_supported(config)",
            f"{family} secure profile admission")
    set_key = body(source, "setAPKey(const struct ItlHalApKey *key)")
    for needle, label in (
        ("key->keyLength != 16", "16-byte CCMP key shape"),
        ("key->keyIndex > 3", "data key index bound"),
        ("key->flags == kItlHalApKeyPairwise", "PTK/GTK split"),
        ("2 + itl_ap_firmware_client_index(&apRuntime, client)",
         "per-client STA/AP coexistence key slots"),
        ("2 + kItlApFirmwareMaxClients", "dedicated group key slot"),
        ("clientPairwiseKeyInstalled = true", "PTK commit-after-firmware"),
        ("groupKeyInstalled = true", "GTK commit-after-firmware"),
    ):
        require(set_key, needle, f"{family} {label}")
    station = body(source,
        "sendAPStationCommand(const struct ItlHalApStationCommand *command)")
    require(station, "!client->clientPairwiseKeyInstalled",
            f"{family} authorize-after-PTK")
    require(station, "!apRuntime.groupKeyInstalled",
            f"{family} authorize-after-GTK")
    require(station, "client->clientAuthorized = true;",
            f"{family} controlled-port open")

iwm_key = body(iwm,
    "iwm_ap_set_ccmp_key(struct iwm_softc *sc, uint8_t staId,")
for needle, label in (
    ("IWM_STA_KEY_FLG_CCM", "CCMP firmware key"),
    ("IWM_STA_KEY_FLG_WEP_KEY_MAP", "station key map"),
    ("IWM_ADD_STA_KEY", "synchronous key command"),
    ("IWM_ADD_STA_SUCCESS", "firmware status validation"),
):
    require(iwm_key, needle, f"IWM {label}")
iwm_raw = body(iwm,
    "iwm_ap_send_raw_frame(struct iwm_softc *sc, mbuf_t m,")
for needle, label in (
    ("IWM_TX_CMD_SEC_CCM", "CCMP TX descriptor"),
    ("IEEE80211_CCMP_HDRLEN", "driver IV space"),
    ("apRuntime.groupKey", "inline AP GTK"),
    ("client->clientPairwiseKey", "inline per-client AP PTK"),
):
    require(iwm_raw, needle, f"IWM {label}")
require(iwm_rx, "apHardwareDecrypted", "IWM verified CCMP RX handoff")

iwx_key = body(iwx,
    "iwx_ap_set_ccmp_key(struct iwx_softc *sc, uint8_t staId,")
for needle, label in (
    ("IWX_STA_KEY_FLG_CCM", "CCMP firmware key"),
    ("IWX_STA_KEY_MULTICAST", "GTK multicast binding"),
    ("IWX_ADD_STA_KEY", "synchronous key command"),
    ("IWX_ADD_STA_SUCCESS", "firmware status validation"),
):
    require(iwx_key, needle, f"IWX {label}")
iwx_raw = body(iwx,
    "iwx_ap_send_raw_frame(struct iwx_softc *sc, mbuf_t m,")
require(iwx_raw, "protectedFrame ? 0 : IWX_TX_FLAGS_ENCRYPT_DIS",
        "IWX firmware CCMP TX enable")
require(iwx, "apHardwareDecrypted", "IWX verified CCMP RX handoff")

print("PASS: paired IWM/IWX WPA2 AP EAPOL/key/CCMP contract")
PY
