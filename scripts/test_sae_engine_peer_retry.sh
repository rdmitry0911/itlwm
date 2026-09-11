#!/usr/bin/env bash
# Actual production core + vendored crypto. No extracted/reimplemented SAE.
set -euo pipefail
ulimit -c 0
task_root=$(cd "$(dirname "$0")/.." && pwd)
task_out=$(mktemp -d "${TMPDIR:-/tmp}/itlwm-sae-peer-core.XXXXXX")
task_cc=${CC:-clang}
task_flags=(
  -std=gnu11 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter
  -fsanitize=address,undefined -fno-omit-frame-pointer
  -DCONFIG_SAE -DCONFIG_NO_STDOUT_DEBUG -DCONFIG_NO_WPA_MSG
  -DCONFIG_NO_RANDOM_POOL -DITL_SAE_DRIVER_CRYPTO=1 -D__IO80211_TARGET=260000
  -Dtimingsafe_bcmp=os_memcmp_const
  -DMBEDTLS_CONFIG_FILE='"net80211/ieee80211_sae_mbedtls_config.h"'
  -I"$task_root/include" -I"$task_root/itl80211/openbsd"
  -I"$task_root/third_party/sae/driver/hostapd/src"
  -I"$task_root/third_party/sae/driver/hostapd/src/utils"
  -I"$task_root/third_party/sae/driver/mbedtls/include"
  -I"$task_root/third_party/sae/driver/mbedtls/library"
)
task_sources=(
  tests/sae_engine_peer_retry_test.c
  tests/sae_driver_crypto_userland_platform.c
  itl80211/openbsd/net80211/ieee80211_sae_engine.c
  itl80211/openbsd/net80211/ieee80211_sae_mbedtls_adapter.c
  third_party/sae/driver/hostapd/src/common/dragonfly.c
  third_party/sae/driver/hostapd/src/common/sae.c
  third_party/sae/driver/hostapd/src/crypto/sha256-internal.c
  third_party/sae/driver/hostapd/src/crypto/sha256-kdf.c
  third_party/sae/driver/hostapd/src/crypto/sha256-prf.c
  third_party/sae/driver/hostapd/src/crypto/sha256.c
  third_party/sae/driver/hostapd/src/utils/wpabuf.c
  third_party/sae/driver/mbedtls/library/bignum.c
  third_party/sae/driver/mbedtls/library/bignum_core.c
  third_party/sae/driver/mbedtls/library/constant_time.c
  third_party/sae/driver/mbedtls/library/ecp.c
  third_party/sae/driver/mbedtls/library/ecp_curves.c
  third_party/sae/driver/mbedtls/library/platform.c
  third_party/sae/driver/mbedtls/library/platform_util.c
)
task_objects=()
for task_source in "${task_sources[@]}"; do
  task_object="$task_out/${#task_objects[@]}.o"
  "$task_cc" "${task_flags[@]}" -c "$task_root/$task_source" -o "$task_object"
  task_objects+=("$task_object")
done
"$task_cc" -fsanitize=address,undefined -o "$task_out/peer-core" "${task_objects[@]}"
printf 'SAE core test artifacts: %s\n' "$task_out"
"$task_out/peer-core"
