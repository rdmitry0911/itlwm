#!/usr/bin/env bash
# Rebuild a test-only hostap SAE group-19 KAT with exact OpenWrt mbedTLS
# patches and a source-verified static mbedTLS archive.  It creates one fresh
# directory under /private/tmp (or the caller's empty --workdir), downloads
# only the lock-pinned public source inputs, and never installs, loads,
# publishes, joins, scans, routes, addresses, or reboots anything.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
LOCK="$ROOT/third_party/sae/openwrt_mbedtls_group19.lock"
MAIN="$ROOT/tests/sae_group19_kat_main.c"
WORKDIR=""
JOBS="${ITLWM_SAE_KAT_JOBS:-2}"

usage() {
	cat >&2 <<'EOF'
usage: run_tahoe_openwrt_mbedtls_sae_group19_kat.sh [--workdir DIR]

Builds a test-only x86_64 Tahoe group-19 HnP/H2E vector executable in a fresh
directory. DIR must not exist; without --workdir the script uses /private/tmp.
EOF
}

die() {
	echo "ERROR: $*" >&2
	exit 1
}

lock_value() {
	local key="$1"
	awk -F= -v key="$key" '
		$1 == key { count++; value = substr($0, length(key) + 2) }
		END {
			if (count != 1 || value == "")
				exit 1
			print value
		}
	' "$LOCK"
}

sha256_file() {
	shasum -a 256 "$1" | awk '{ print $1 }'
}

require_tool() {
	command -v "$1" >/dev/null 2>&1 || die "missing required tool: $1"
}

require_hash() {
	local path="$1"
	local expected="$2"
	local observed
	observed="$(sha256_file "$path")"
	[ "$observed" = "$expected" ] ||
		die "SHA-256 mismatch for $(basename "$path")"
}

safe_patch_name() {
	case "$1" in
		""|*/*|*..*|*[!A-Za-z0-9._-]*) return 1 ;;
		*) return 0 ;;
	esac
}

while [ "$#" -gt 0 ]; do
	case "$1" in
		--workdir)
			[ "$#" -ge 2 ] || { usage; exit 2; }
			WORKDIR="$2"
			shift 2
			;;
		-h|--help)
			usage
			exit 0
			;;
		*)
			usage
			exit 2
			;;
	esac
done

[ -f "$LOCK" ] || die "missing source lock: $LOCK"
[ -f "$MAIN" ] || die "missing KAT entry point: $MAIN"
case "$JOBS" in
	""|*[!0-9]*|0) die "ITLWM_SAE_KAT_JOBS must be a positive integer" ;;
esac

for tool in awk curl file git lipo make nm otool shasum tar xcrun /usr/bin/cc; do
	require_tool "$tool"
done

schema="$(lock_value schema_version)" || die "invalid source lock"
[ "$schema" = "itlwm-openwrt-mbedtls-sae-group19-intake/v1" ] ||
	die "unsupported source-lock schema"
hostap_url="$(lock_value hostap_git_url)" || die "missing hostap URL"
hostap_commit="$(lock_value hostap_commit)" || die "missing hostap commit"
openwrt_commit="$(lock_value openwrt_commit)" || die "missing OpenWrt commit"
patch_count="$(lock_value openwrt_patch_count)" || die "missing OpenWrt patch count"
patched_tree="$(lock_value hostap_patched_tree_sha1)" || die "missing patched hostap tree"
mbedtls_version="$(lock_value mbedtls_version)" || die "missing mbedTLS version"
mbedtls_url="$(lock_value mbedtls_source_url)" || die "missing mbedTLS URL"
mbedtls_hash="$(lock_value mbedtls_source_sha256)" || die "missing mbedTLS hash"
license_choice="$(lock_value mbedtls_license_choice)" || die "missing mbedTLS license choice"

[ "$hostap_url" = "https://w1.fi/hostap.git" ] || die "unexpected hostap URL"
[ "$openwrt_commit" = "0f256a0a7adf5741e4a061f59a08cd01c14dc526" ] ||
	die "unexpected OpenWrt commit"
[ "$patch_count" = "7" ] || die "unexpected OpenWrt patch count"
[ "$mbedtls_version" = "3.6.6" ] || die "unexpected mbedTLS version"
[ "$mbedtls_url" = "https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.6/mbedtls-3.6.6.tar.bz2" ] ||
	die "unexpected mbedTLS URL"
[ "$license_choice" = "GPL-2.0-or-later" ] || die "unexpected mbedTLS license choice"

if [ -n "$WORKDIR" ]; then
	[ ! -e "$WORKDIR" ] || die "--workdir must not already exist"
	mkdir -p "$WORKDIR"
else
	WORKDIR="$(mktemp -d "${TMPDIR:-/private/tmp}/itlwm-openwrt-mbedtls-sae-kat.XXXXXX")"
fi

HOSTAP="$WORKDIR/hostap"
PATCHDIR="$WORKDIR/openwrt-patches"
ARCHIVE="$WORKDIR/mbedtls-${mbedtls_version}.tar.bz2"
MBEDTLS="$WORKDIR/mbedtls-${mbedtls_version}"
OUT="$WORKDIR/group19-kat"
mkdir -p "$PATCHDIR" "$OUT"

echo "[1/6] fetching exact hostap source"
git init -q "$HOSTAP"
git -C "$HOSTAP" remote add origin "$hostap_url"
git -C "$HOSTAP" fetch -q --depth=1 origin "$hostap_commit"
git -C "$HOSTAP" checkout -q --detach FETCH_HEAD
[ "$(git -C "$HOSTAP" rev-parse HEAD)" = "$hostap_commit" ] ||
	die "hostap commit does not match lock"

echo "[2/6] fetching and checking exact OpenWrt patch series"
patch_index=1
while [ "$patch_index" -le "$patch_count" ]; do
	patch_name="$(lock_value "openwrt_patch_${patch_index}_name")" ||
		die "missing OpenWrt patch name $patch_index"
	patch_hash="$(lock_value "openwrt_patch_${patch_index}_sha256")" ||
		die "missing OpenWrt patch hash $patch_index"
	safe_patch_name "$patch_name" || die "unsafe OpenWrt patch name"
	patch_path="$PATCHDIR/$patch_name"
	patch_url="https://raw.githubusercontent.com/openwrt/openwrt/${openwrt_commit}/package/network/services/hostapd/patches/${patch_name}"
	curl -fL --connect-timeout 20 --retry 2 --output "$patch_path" "$patch_url"
	require_hash "$patch_path" "$patch_hash"
	git -C "$HOSTAP" -c user.name="itlwm KAT harness" \
		-c user.email="noreply@localhost" am --3way "$patch_path"
	patch_index=$((patch_index + 1))
done
[ "$(git -C "$HOSTAP" rev-parse HEAD^{tree})" = "$patched_tree" ] ||
	die "patched hostap tree does not match lock"

echo "[3/6] fetching and checking mbedTLS source"
curl -fL --connect-timeout 20 --retry 2 --output "$ARCHIVE" "$mbedtls_url"
require_hash "$ARCHIVE" "$mbedtls_hash"
tar -xjf "$ARCHIVE" -C "$WORKDIR"
[ -f "$MBEDTLS/LICENSE" ] || die "mbedTLS archive layout changed"
grep -F "dual [Apache-2.0]" "$MBEDTLS/LICENSE" >/dev/null ||
	die "mbedTLS license text changed"
grep -F "GPL-2.0-or-later" "$MBEDTLS/LICENSE" >/dev/null ||
	die "mbedTLS GPL license choice is absent"

SDK="$(xcrun --sdk macosx --show-sdk-path)"
[ -d "$SDK" ] || die "macOS SDK path is unavailable"
BASE_CFLAGS=(
	-isysroot "$SDK" -mmacosx-version-min=15.0 -arch x86_64 -O2 -fPIC
	-ffunction-sections -fdata-sections
	-I"$HOSTAP/src" -I"$HOSTAP/src/utils" -I"$MBEDTLS/include"
	-DCONFIG_SAE -DCONFIG_ECC -DCONFIG_SHA256 -DCONFIG_HMAC_SHA256_KDF
	-DCRYPTO_RSA_OAEP_SHA256
)
MBEDTLS_CFLAGS="-isysroot $SDK -mmacosx-version-min=15.0 -arch x86_64 -O2 -fPIC"

echo "[4/6] building static mbedTLS libmbedcrypto"
make -C "$MBEDTLS" -j"$JOBS" lib CC=/usr/bin/cc CFLAGS="$MBEDTLS_CFLAGS"
[ -f "$MBEDTLS/library/libmbedcrypto.a" ] || die "static libmbedcrypto was not built"

echo "[5/6] compiling upstream group-19 HnP/H2E vectors"
OBJS=()
compile_source() {
	local source="$1"
	local object="$OUT/$(printf '%s' "$source" | tr '/.' '__').o"
	/usr/bin/cc "${BASE_CFLAGS[@]}" -c "$HOSTAP/$source" -o "$object"
	OBJS+=("$object")
}

/usr/bin/cc "${BASE_CFLAGS[@]}" -Dstatic= \
	-c "$HOSTAP/src/common/common_module_tests.c" \
	-o "$OUT/common_module_tests.o"
OBJS+=("$OUT/common_module_tests.o")
for source in \
	src/common/sae.c \
	src/common/dragonfly.c \
	src/common/wpa_common.c \
	src/crypto/crypto_mbedtls.c \
	src/crypto/dh_groups.c \
	src/crypto/random.c \
	src/utils/common.c \
	src/utils/wpa_debug.c \
	src/utils/wpabuf.c \
	src/utils/os_unix.c; do
	compile_source "$source"
done
/usr/bin/cc "${BASE_CFLAGS[@]}" -c "$MAIN" -o "$OUT/main.o"
/usr/bin/cc -arch x86_64 -isysroot "$SDK" -mmacosx-version-min=15.0 \
	-Wl,-dead_strip -o "$OUT/hostap_sae_group19_kat" "$OUT/main.o" \
	"${OBJS[@]}" -L"$MBEDTLS/library" -lmbedcrypto

echo "[6/6] verifying static linkage and vectors"
file "$OUT/hostap_sae_group19_kat"
lipo -info "$OUT/hostap_sae_group19_kat"
otool -L "$OUT/hostap_sae_group19_kat" | tee "$OUT/otool-L.txt"
if ! sed '1d' "$OUT/otool-L.txt" | awk '
	/^[[:space:]]*\/usr\/lib\/libSystem\.B\.dylib / { seen++; next }
	{ bad = 1 }
	END { exit (seen == 1 && bad != 1) ? 0 : 1 }
'; then
	die "KAT has a dynamic dependency other than libSystem"
fi
nm -arch x86_64 "$OUT/hostap_sae_group19_kat" >"$OUT/symbols.txt"
for symbol in _mbedtls_ecp_group_load _mbedtls_mpi_mod_mpi \
	_mbedtls_md_hmac_starts _mbedtls_sha256; do
	grep -F "$symbol" "$OUT/symbols.txt" >/dev/null ||
		die "missing expected static mbedTLS symbol: $symbol"
done
"$OUT/hostap_sae_group19_kat"

echo "PASS: OpenWrt-patched hostap SAE group-19 KAT with static libmbedcrypto"
echo "  work directory: $WORKDIR"
echo "  scope: test-only; no Agent/kext SAE integration, load, join, route, or reboot"
