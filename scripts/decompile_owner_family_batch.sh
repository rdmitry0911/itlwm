#!/bin/zsh
set -euo pipefail

# Batch-decompile the 13 remaining hidden-owner families from the remote Ghidra host.
# This uses the existing single-program Ghidra projects on dima@192.168.40.116 and
# writes one file per family for both the AppleBCMWLANCoreMac and IO80211Family sides.

REMOTE_HOST="${REMOTE_HOST:-dima@192.168.40.116}"
REMOTE_GHIDRA_HOME="${REMOTE_GHIDRA_HOME:-/srv/project/ghidra_11.4.2_PUBLIC}"
REMOTE_OUT_BASE="${REMOTE_OUT_BASE:-/srv/project/ghidra_output}"
REMOTE_SCRIPT_DIR="${REMOTE_SCRIPT_DIR:-/tmp/itlwm_owner_batch_$$}"
REMOTE_PROJECT_BASE="${REMOTE_PROJECT_BASE:-/tmp/itlwm_owner_projects_$$}"
CORE_BINARY="${CORE_BINARY:-/srv/project/ghidra_output/wifi_kexts/com.apple.driver.AppleBCMWLANCoreMac}"
IO80211_BINARY="${IO80211_BINARY:-/srv/project/ghidra_output/wifi_kexts/com.apple.iokit.IO80211Family}"
BATCH_NAME="${1:-owner_family_batch_$(date +%Y%m%d_%H%M%S)}"
REMOTE_BATCH_DIR="${REMOTE_OUT_BASE}/${BATCH_NAME}"
LOCAL_HELPER="$(cd "$(dirname "$0")" && pwd)/ghidra/DecompileOwnerFamilyBatch.py"

if [[ ! -f "$LOCAL_HELPER" ]]; then
  echo "missing helper script: $LOCAL_HELPER" >&2
  exit 1
fi

echo "remote host: $REMOTE_HOST"
echo "batch dir:   $REMOTE_BATCH_DIR"

ssh "$REMOTE_HOST" "mkdir -p '$REMOTE_SCRIPT_DIR' '$REMOTE_PROJECT_BASE' '$REMOTE_BATCH_DIR/core' '$REMOTE_BATCH_DIR/io80211'"
scp "$LOCAL_HELPER" "$REMOTE_HOST:$REMOTE_SCRIPT_DIR/DecompileOwnerFamilyBatch.py" >/dev/null

run_headless() {
  local binary_path="$1"
  local scope="$2"
  local project_dir="$REMOTE_PROJECT_BASE/$scope"
  local project_name="owner_batch_${scope}"
  local out_dir="$REMOTE_BATCH_DIR/$scope"
  local manifest="$REMOTE_BATCH_DIR/manifest.txt"

  ssh "$REMOTE_HOST" \
    "rm -rf '$project_dir' && mkdir -p '$project_dir' && \
     '$REMOTE_GHIDRA_HOME/support/analyzeHeadless' '$project_dir' '$project_name' \
      -import '$binary_path' \
      -overwrite \
      -postScript DecompileOwnerFamilyBatch.py '$out_dir' '$manifest' '$scope' \
      -scriptPath '$REMOTE_SCRIPT_DIR'"
}

run_headless "$CORE_BINARY" core
run_headless "$IO80211_BINARY" io80211

ssh "$REMOTE_HOST" "printf 'batch=%s\n' '$BATCH_NAME' | cat - '$REMOTE_BATCH_DIR/manifest.txt' > '$REMOTE_BATCH_DIR/manifest.tmp' && mv '$REMOTE_BATCH_DIR/manifest.tmp' '$REMOTE_BATCH_DIR/manifest.txt'"
ssh "$REMOTE_HOST" "rm -rf '$REMOTE_SCRIPT_DIR' '$REMOTE_PROJECT_BASE'"

echo "done"
echo "manifest: $REMOTE_BATCH_DIR/manifest.txt"
echo "core:     $REMOTE_BATCH_DIR/core"
echo "io80211:  $REMOTE_BATCH_DIR/io80211"
