#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

python3 - "$repo_root/itlwm/hal_iwx/ItlIwx.cpp" <<'PY'
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text()


def body(signature: str) -> str:
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


sta_tx = body("iwx_tx(struct iwx_softc *sc, mbuf_t m,")
required = (
    "IWX_TX_CMD_OFFLD_MH_SIZE(",
    "(hdrlen / 2) & IWX_TX_CMD_OFFLD_MH_MASK",
    "const uint16_t header_pad = hdrlen & 3 ? 4 - (hdrlen & 3) : 0",
    "offload_assist |= IWX_TX_CMD_OFFLD_PAD",
    "hdrlen + header_pad",
    "cmd_size + hdrlen + header_pad -",
)
for token in required:
    if token not in sta_tx:
        raise SystemExit(f"FAIL: missing STA TX header-boundary token: {token}")

if "_ALIGN(sizeof(struct iwx_cmd_header)" in sta_tx:
    raise SystemExit(
        "FAIL: STA TX descriptor must use the same explicit pad it advertises"
    )

mh_size = sta_tx.index("IWX_TX_CMD_OFFLD_MH_SIZE(")
command_fill = sta_tx.index("if (sc->sc_device_family >= IWX_DEVICE_FAMILY_AX210)")
descriptor_fill = sta_tx.index("desc->tbs[1].tb_len")
if not mh_size < command_fill < descriptor_fill:
    raise SystemExit("FAIL: STA TX header boundary is published too late")

print("PASS: IWX STA TX publishes the firmware MAC-header boundary")
PY
