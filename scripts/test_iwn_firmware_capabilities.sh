#!/usr/bin/env bash
set -euo pipefail
PROJECT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
TEST_DIR="$(mktemp -d)"
trap 'rm -f "$TEST_DIR/production.inc" "$TEST_DIR/registers.inc" "$TEST_DIR/test"; rmdir "$TEST_DIR"' EXIT
python3 - "$PROJECT_DIR" "$TEST_DIR" <<'PY'
from pathlib import Path
import os
import subprocess
import sys
root, out = map(Path, sys.argv[1:])
baseline = os.environ.get('IWN_FIRMWARE_BASELINE')
source = subprocess.check_output(['git', 'show', baseline + ':itlwm/hal_iwn/ItlIwn.cpp'],
    cwd=root, text=True) if baseline else (root / 'itlwm/hal_iwn/ItlIwn.cpp').read_text()
def function(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    for end in range(opening, len(source)):
        if source[end] == '{': depth += 1
        elif source[end] == '}':
            depth -= 1
            if depth == 0: return source[start:end + 1]
    raise AssertionError(signature)
body = '\n\n'.join(function('int ItlIwn::\n' + name + '(') for name in
    ('iwn_read_firmware_leg', 'iwn_read_firmware_tlv', 'iwn_read_firmware'))
if not baseline:
    body += '\n' + function('void ItlIwn::iwn_release_firmware(')
    body += '\n' + function('int ItlIwn::iwn_prepare_firmware_capabilities(')
else:
    body += '\nvoid ItlIwn::iwn_release_firmware(iwn_softc *sc) { ::free(sc->fw.data); bzero(&sc->fw, sizeof(sc->fw)); }\n'
    body += 'int ItlIwn::iwn_prepare_firmware_capabilities(iwn_softc *sc) { int error = iwn_read_firmware(sc); if (error == 0) iwn_release_firmware(sc); return error; }\n'
(out / 'production.inc').write_text(body)
registers = (root / 'itlwm/hal_iwn/if_iwnreg.h').read_text()
begin = registers.index('/* TLV firmware header. */')
end = registers.index('/*\n * Microcode flags TLV', begin)
selected = {'IWN5000_PHY_CALIB_RESET_NOISE_GAIN', 'IWN5000_PHY_CALIB_NOISE_GAIN', 'IWN5000_PHY_CALIB_MAX'}
extra = '\n'.join(line for line in registers.splitlines() if line.startswith('#define ') and line.split()[1] in selected)
var = (root / 'itlwm/hal_iwn/if_iwnvar.h').read_text()
parts = var[var.index('struct iwn_fw_part {'):var.index('struct iwn_ops {')]
(out / 'registers.inc').write_text(registers[begin:end] + extra + '\n' + parts)
if not baseline:
    attach = source[source.index('bool ItlIwn::attach('):source.index('bool ItlIwn::iwn_ap_uses_sae()')]
    assert attach.index('iwn_attach(&com, &pci)') < attach.index('iwn_prepare_firmware_capabilities(&com)')
    init = function('int ItlIwn::\niwn_init(')
    assert init.index('iwn_hw_init(sc)') < init.index('iwn_release_firmware(sc)')
PY
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer -I "$TEST_DIR" \
    "$PROJECT_DIR/tests/iwn_firmware_capabilities_test.cpp" -lz -o "$TEST_DIR/test"
"$TEST_DIR/test" "$PROJECT_DIR/itlwm/firmware"
