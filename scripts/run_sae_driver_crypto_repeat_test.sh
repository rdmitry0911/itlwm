#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${TMPDIR:-/tmp}/itlwm-sae-driver-crypto-repeat
CC=${CC:-cc}
ROUNDS=${1:-1000}

mkdir -p "$OUT"
rm -f "$OUT"/*.o "$OUT/sae_driver_crypto_repeat"

COMMON_FLAGS=(
	-std=gnu11 -O2 -g -Wall -Wextra -Werror -Wno-unused-parameter
	-DCONFIG_SAE -DCONFIG_NO_STDOUT_DEBUG -DCONFIG_NO_WPA_MSG
	-DCONFIG_NO_RANDOM_POOL
	-DMBEDTLS_CONFIG_FILE='"net80211/ieee80211_sae_mbedtls_config.h"'
	-I"$ROOT/itl80211/openbsd"
	-I"$ROOT/third_party/sae/driver/hostapd/src"
	-I"$ROOT/third_party/sae/driver/hostapd/src/utils"
	-I"$ROOT/third_party/sae/driver/mbedtls/include"
	-I"$ROOT/third_party/sae/driver/mbedtls/library"
)

SOURCES=(
	"$ROOT/tests/sae_driver_crypto_repeat_main.c"
	"$ROOT/tests/sae_driver_crypto_userland_platform.c"
	"$ROOT/itl80211/openbsd/net80211/ieee80211_sae_mbedtls_adapter.c"
	"$ROOT/third_party/sae/driver/hostapd/src/common/dragonfly.c"
	"$ROOT/third_party/sae/driver/hostapd/src/common/sae.c"
	"$ROOT/third_party/sae/driver/hostapd/src/crypto/sha256-internal.c"
	"$ROOT/third_party/sae/driver/hostapd/src/crypto/sha256-kdf.c"
	"$ROOT/third_party/sae/driver/hostapd/src/crypto/sha256-prf.c"
	"$ROOT/third_party/sae/driver/hostapd/src/crypto/sha256.c"
	"$ROOT/third_party/sae/driver/hostapd/src/utils/wpabuf.c"
	"$ROOT/third_party/sae/driver/mbedtls/library/bignum.c"
	"$ROOT/third_party/sae/driver/mbedtls/library/bignum_core.c"
	"$ROOT/third_party/sae/driver/mbedtls/library/constant_time.c"
	"$ROOT/third_party/sae/driver/mbedtls/library/ecp.c"
	"$ROOT/third_party/sae/driver/mbedtls/library/ecp_curves.c"
	"$ROOT/third_party/sae/driver/mbedtls/library/platform.c"
	"$ROOT/third_party/sae/driver/mbedtls/library/platform_util.c"
)

OBJECTS=()
for source in "${SOURCES[@]}"; do
	object="$OUT/$(printf '%s' "$source" | sha256sum | cut -c1-16).o"
	"$CC" "${COMMON_FLAGS[@]}" -c "$source" -o "$object"
	OBJECTS+=("$object")
done

"$CC" -o "$OUT/sae_driver_crypto_repeat" "${OBJECTS[@]}"
"$OUT/sae_driver_crypto_repeat" "$ROUNDS"
