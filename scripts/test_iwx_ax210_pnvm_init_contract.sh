#!/usr/bin/env bash
# Source-and-firmware contract for AX210-family PNVM initialization.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import re
import struct
import sys


root = Path(sys.argv[1])
cpp = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()
var = (root / "itlwm/hal_iwx/if_iwxvar.h").read_text()


def fail(message: str) -> None:
    raise SystemExit(f"IWX AX210 PNVM init contract: {message}")


def body(source: str, marker: str) -> str:
    start = source.find(marker)
    if start < 0:
        fail(f"missing function {marker}")
    opening = source.find("{", start)
    depth = 0
    for pos in range(opening, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:pos]
    fail(f"unterminated function {marker}")


def require(source: str, token: str, label: str) -> None:
    if token not in source:
        fail(f"missing {label}: {token}")


def require_order(source: str, label: str, *tokens: str) -> None:
    cursor = 0
    for token in tokens:
        pos = source.find(token, cursor)
        if pos < 0:
            fail(f"{label} missing ordered token: {token}")
        cursor = pos + len(token)


read_pnvm = body(cpp, "iwx_read_pnvm(struct iwx_softc *sc)")
require(read_pnvm, "if (fwname_copy[index] == '-')",
        "last API separator lookup on the bounded copy")
require(read_pnvm, "api_separator = index;",
        "last API separator ownership")
require(read_pnvm, "fwname_copy[api_separator] = '\\0';",
        "API suffix-only truncation")
if "strrchr" in read_pnvm or "find - sc->sc_fwname - 1" in read_pnvm:
    fail("ambiguous compatibility strrchr PNVM filename derivation returned")
require_order(
    read_pnvm,
    "PNVM resource flow",
    "fwname_copy[api_separator] = '\\0';",
    'snprintf(pnvm_name, sizeof(pnvm_name), "%s.pnvm"',
    "fwData = getFWDescByName(pnvm_name);",
    "uncompressFirmware(",
    "iwx_pnvm_handle_section(sc, data, len)",
)

load_pnvm = body(cpp, "iwx_load_pnvm(struct iwx_softc *sc)")
require_order(
    load_pnvm,
    "fail-closed PNVM load and completion",
    "err = iwx_read_pnvm(sc);",
    "required PNVM load failed",
    "return err < 0 ? -err : err;",
    "sc->sc_init_complete &= ~wait_flags;",
    "sc->sc_pnvm_status = UINT32_MAX;",
    "IWX_UREG_DOORBELL_TO_ISR6_PNVM",
    "while ((sc->sc_init_complete & wait_flags) != wait_flags)",
)
if "sc->sc_pnvm_status != 0" in load_pnvm or "err = EIO" in load_pnvm:
    fail("PNVM notification status was incorrectly promoted to host errno")

require(var, "#define IWX_PNVM_COMPLETE\t0x04",
        "distinct PNVM completion bit")
require(var, "uint32_t sc_pnvm_status;", "owned PNVM firmware status")

rx = body(cpp, "iwx_rx_pkt(struct iwx_softc *sc,")
pnvm_case = rx[rx.find("IWX_PNVM_INIT_COMPLETE_NTFY"):]
pnvm_case = pnvm_case[:pnvm_case.find("case IWX_INIT_COMPLETE_NOTIF")]
require_order(
    pnvm_case,
    "PNVM notification publish-before-wake",
    "iwx_rx_packet_payload_len(pkt) < sizeof(*pnvm_ntf)",
    "sc->sc_pnvm_status = le32toh(pnvm_ntf->status);",
    "PNVM init complete notification status",
    "sc->sc_init_complete |= IWX_PNVM_COMPLETE;",
    "wakeupOn(&sc->sc_init_complete);",
)

run_init = body(cpp, "iwx_run_init_mvm_ucode(struct iwx_softc *sc, int readnvm)")
require(run_init,
        "while ((sc->sc_init_complete & wait_flags) != wait_flags)",
        "predicate-checked INIT_COMPLETE wait")

# Reproduce the filename mapping exercised by the selected AX210-family
# firmware assets and independently validate that each resulting PNVM file is
# a well-formed TLV stream with the two payloads required by API-68's
# unfragmented PNVM ABI.
selected = (
    "iwlwifi-so-a0-gf-a0-68.ucode",
    "iwlwifi-ty-a0-gf-a0-68.ucode",
    "iwlwifi-so-a0-gf4-a0-68.ucode",
)
for fw_name in selected:
    prefix, separator, api_suffix = fw_name.rpartition("-")
    if not separator or api_suffix != "68.ucode":
        fail(f"unexpected selected firmware name {fw_name}")
    pnvm_name = f"{prefix}.pnvm"
    pnvm_path = root / "itlwm/firmware" / pnvm_name
    if not pnvm_path.is_file():
        fail(f"derived PNVM resource is absent: {pnvm_name}")

    data = pnvm_path.read_bytes()
    offset = 0
    saw_sku = False
    payload_count = 0
    section_counts = []
    while offset + 8 <= len(data):
        tlv_type, tlv_len = struct.unpack_from("<II", data, offset)
        next_offset = offset + 8 + ((tlv_len + 3) & ~3)
        if next_offset > len(data):
            fail(f"truncated TLV in {pnvm_name}")
        if tlv_type == 64:  # IWX_UCODE_TLV_PNVM_SKU
            if saw_sku:
                section_counts.append(payload_count)
            saw_sku = True
            payload_count = 0
        elif tlv_type == 19 and saw_sku:  # IWX_UCODE_TLV_SEC_RT
            if tlv_len < 4:
                fail(f"short PNVM payload in {pnvm_name}")
            payload_count += 1
        offset = next_offset
    if offset != len(data) or not saw_sku:
        fail(f"invalid or SKU-less PNVM stream {pnvm_name}")
    section_counts.append(payload_count)
    if any(count != 2 for count in section_counts):
        fail(f"{pnvm_name} violates unfragmented two-payload ABI: "
             f"{section_counts}")

print("PASS: IWX AX210 PNVM filename, payload and completion contract")
PY
