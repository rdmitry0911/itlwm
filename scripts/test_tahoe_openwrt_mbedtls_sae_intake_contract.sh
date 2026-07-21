#!/usr/bin/env bash
# Static contract for the source-pinned, test-only OpenWrt mbedTLS SAE KAT.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
LOCK="$ROOT/third_party/sae/openwrt_mbedtls_group19.lock"
RUNNER="$ROOT/scripts/run_tahoe_openwrt_mbedtls_sae_group19_kat.sh"
MAIN="$ROOT/tests/sae_group19_kat_main.c"
DOC="$ROOT/docs/TAHOE_OPENWRT_MBEDTLS_SAE_GROUP19_INTAKE.md"
EVIDENCE="$ROOT/evidence/runtime/tahoe_openwrt_mbedtls_sae_group19_kat.json"

for path in "$LOCK" "$RUNNER" "$MAIN" "$DOC" "$EVIDENCE"; do
	[ -f "$path" ] || {
		echo "FAIL: missing OpenWrt mbedTLS SAE intake input: $path" >&2
		exit 1
	}
done

bash -n "$RUNNER"
tmpdir="$(mktemp -d "${TMPDIR:-/tmp}/airportitlwm-openwrt-mbedtls-intake.XXXXXX")"
cleanup() {
	rm -rf "$tmpdir"
}
trap cleanup EXIT
clang -std=c11 -Wall -Wextra -Werror -c "$MAIN" -o "$tmpdir/sae-group19-kat-main.o"

python3 - "$ROOT" <<'PY'
from __future__ import annotations

import json
import re
import sys
from pathlib import Path


root = Path(sys.argv[1])
lock_path = root / "third_party/sae/openwrt_mbedtls_group19.lock"
runner = (root / "scripts/run_tahoe_openwrt_mbedtls_sae_group19_kat.sh").read_text()
main = (root / "tests/sae_group19_kat_main.c").read_text()
doc = (root / "docs/TAHOE_OPENWRT_MBEDTLS_SAE_GROUP19_INTAKE.md").read_text()
doc_flat = " ".join(doc.split())
evidence = json.loads(
    (root / "evidence/runtime/tahoe_openwrt_mbedtls_sae_group19_kat.json").read_text()
)
agent_makefile = (root / "AirportItlwmAgent/Makefile").read_text()


def fail(message: str) -> None:
    raise SystemExit(f"OpenWrt mbedTLS SAE intake contract: {message}")


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        fail(f"missing {label}: {needle}")


def forbid(text: str, needle: str, label: str) -> None:
    if needle in text:
        fail(f"forbidden {label}: {needle}")


lock: dict[str, str] = {}
for number, raw_line in enumerate(lock_path.read_text().splitlines(), 1):
    line = raw_line.strip()
    if not line or line.startswith("#"):
        continue
    if "=" not in line:
        fail(f"malformed lock line {number}")
    key, value = line.split("=", 1)
    if not re.fullmatch(r"[a-z0-9_]+", key) or not value:
        fail(f"invalid lock field at line {number}")
    if key in lock:
        fail(f"duplicate lock field: {key}")
    lock[key] = value


expected_lock = {
    "schema_version": "itlwm-openwrt-mbedtls-sae-group19-intake/v1",
    "hostap_git_url": "https://w1.fi/hostap.git",
    "hostap_commit": "b004de0bf1b54d669d358b7f33d6f474bd9719a6",
    "hostap_license": "BSD-3-Clause",
    "openwrt_commit": "0f256a0a7adf5741e4a061f59a08cd01c14dc526",
    "openwrt_patch_count": "7",
    "hostap_patched_tree_sha1": "a7bb37dda84e314ff252d27814007ff6e19de529",
    "mbedtls_version": "3.6.6",
    "mbedtls_source_url": "https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.6/mbedtls-3.6.6.tar.bz2",
    "mbedtls_source_sha256": "8fb65fae8dcae5840f793c0a334860a411f884cc537ea290ce1c52bb64ca007a",
    "mbedtls_license_upstream": "Apache-2.0 OR GPL-2.0-or-later",
    "mbedtls_license_choice": "GPL-2.0-or-later",
    "kat_profile": "sae-group19-hnp-h2e-vectors-only",
    "kat_linkage": "static-libmbedcrypto-only",
    "runtime_sae_integration": "false",
}
for key, value in expected_lock.items():
    if lock.get(key) != value:
        fail(f"unexpected lock value for {key}")

patches = (
    ("110-mbedtls-TLS-crypto-option-initial-port.patch", "a53e30d8866a3b9a3af66f24ceead47821101be217769f2cf0ce3d2cf50a8d5e"),
    ("120-mbedtls-fips186_2_prf.patch", "7c60f8e4b260403a22e0340a153d0677f09256286f3793e70b12a6d717e9884e"),
    ("130-mbedtls-annotate-with-TEST_FAIL-for-hwsim-tests.patch", "d2eb8b2381b20b325bf61fd647fc6ed3e8a226cd170846b4b3fb14dfe82876b4"),
    ("135-mbedtls-fix-owe-association.patch", "2140662fd40f198674db350199b5aaceecdab6d03dc4f5f39b300116ce0cb873"),
    ("140-tests-Makefile-make-run-tests-with-CONFIG_TLS.patch", "f4f77e45adc96b02648835b41dd7012535fb06c5e43dd1d22aa73397ec500e8a"),
    ("150-add-NULL-checks-encountered-during-tests-hwsim.patch", "6554c4ffacfc1be1d3913a6e0bcbfa7334586c6996a8c4dd029283cfffb6fe17"),
    ("160-dpp_pkex-EC-point-mul-w-value-prime.patch", "38498a268c070db722675d71ad9ff2f1685dfae247ab5f5f4e652a1dacbcb93c"),
)
for index, (name, digest) in enumerate(patches, 1):
    if lock.get(f"openwrt_patch_{index}_name") != name:
        fail(f"unexpected OpenWrt patch {index} name")
    if lock.get(f"openwrt_patch_{index}_sha256") != digest:
        fail(f"unexpected OpenWrt patch {index} SHA-256")
    if not re.fullmatch(r"[0-9a-f]{64}", digest):
        fail(f"invalid OpenWrt patch {index} SHA-256 shape")

# The runner consumes only exact source identities, applies every patch with
# git-am, and proves the resulting content tree rather than trusting a branch
# name or an unverified downloaded patch.
for needle in (
    'LOCK="$ROOT/third_party/sae/openwrt_mbedtls_group19.lock"',
    'MAIN="$ROOT/tests/sae_group19_kat_main.c"',
    'git -C "$HOSTAP" fetch -q --depth=1 origin "$hostap_commit"',
    'git -C "$HOSTAP" -c user.name="itlwm KAT harness"',
    'am --3way "$patch_path"',
    'rev-parse HEAD^{tree}',
    'curl -fL --connect-timeout 20 --retry 2',
    'require_hash "$ARCHIVE" "$mbedtls_hash"',
    'tar -xjf "$ARCHIVE"',
    'make -C "$MBEDTLS" -j"$JOBS" lib',
    '-Dstatic=',
    'src/crypto/crypto_mbedtls.c',
    '-lmbedcrypto',
    'otool -L "$OUT/hostap_sae_group19_kat"',
    '_mbedtls_ecp_group_load',
    '_mbedtls_mpi_mod_mpi',
    '_mbedtls_md_hmac_starts',
    '_mbedtls_sha256',
    'no Agent/kext SAE integration, load, join, route, or reboot',
):
    require(runner, needle, "source-pinned KAT runner")
forbidden_runner_forms = (
    '-lmbedtls', '-lmbedx509', 'kextload', 'kextutil', 'kmutil',
    'networksetup', 'airport -s', 'wdutil scan', 'route add',
    'route delete', 'route change', 'ifconfig ', 'ipconfig ',
    '/sbin/reboot', 'shutdown -r', 'sudo ', 'ssh ', 'scp ', 'rsync ',
)
for needle in forbidden_runner_forms:
    forbid(runner, needle, "unsafe or widened KAT runner capability")

# `-Dstatic=` is intentionally restricted to the temporary upstream test
# translation unit.  The checked-in entry point can only invoke sae_tests().
for needle in (
    'int sae_tests(void);',
    'if (sae_tests() != 0)',
    'hostap SAE group-19 HnP commit/KCK/PMK/PMKID KAT: PASS',
    'hostap SAE group-19 H2E PT/PWE x/y KAT: PASS',
):
    require(main, needle, "test-only KAT entry point")
for needle in ('password', 'pwe[', 'kck[', 'pmk['):
    forbid(main.lower(), needle, "secret-bearing KAT entry-point field")

# This intake deliberately changes no product linkage.  A later atomic Agent
# integration must change this assertion together with runtime ownership and
# interoperability evidence; it may not be silently smuggled into this KAT.
for needle in ('mbedtls', 'libmbedcrypto', 'sae_group19_kat'):
    forbid(agent_makefile.lower(), needle, "premature Agent integration")

for needle in (
    'does not enable SAE in the kext or Agent',
    'static `libmbedcrypto`',
    'not an SAE association',
    'not a PMF/IGTK result',
    'not an AX211 runtime result',
    'full OpenWrt suite is not claimed',
    'mkdir: illegal option -- L',
    'GPL-2.0-or-later',
):
    require(doc_flat, needle, "scope-bounded intake documentation")

if evidence.get("schema_version") != "itlwm-openwrt-mbedtls-sae-group19-kat/v1":
    fail("unexpected evidence schema")
if evidence.get("sources", {}).get("mbedtls", {}).get("license_choice") != "GPL-2.0-or-later":
    fail("evidence does not record the GPL mbedTLS choice")
if evidence.get("build", {}).get("dynamic_dependencies") != ["/usr/lib/libSystem.B.dylib"]:
    fail("evidence does not prove static mbedTLS linkage")
if evidence.get("checks", {}).get("group19_hnp") != "PASS":
    fail("evidence lacks the group-19 HnP PASS")
if evidence.get("checks", {}).get("group19_h2e") != "PASS":
    fail("evidence lacks the group-19 H2E PASS")
scope = evidence.get("scope", {})
for key in ("agent_or_kext_integration", "sae_association", "pmf_igtk", "ax211_runtime"):
    if scope.get(key) is not False:
        fail(f"evidence overclaims {key}")
if evidence.get("limitations", {}).get("full_openwrt_suite") != "not-claimed-macos-mkdir-L-incompatible":
    fail("evidence does not preserve the full-suite limitation")
PY

echo "PASS: OpenWrt mbedTLS SAE group-19 intake contract"
