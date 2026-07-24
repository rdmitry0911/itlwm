#!/usr/bin/env bash
# Local-only fixture for the IWN lab candidate collection/staging adapter.
# It intentionally exercises no SSH, guest build, guest path, or runtime
# operation; the adapter self-test covers only grammar and safe-token guards.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)"
ADAPTER="$ROOT/scripts/prepare_tahoe_iwn_lab_candidate_stage.sh"

fail() {
    printf 'FAIL: Tahoe IWN lab candidate-stage fixture: %s\n' "$*" >&2
    exit 1
}

[ -x "$ADAPTER" ] || fail 'adapter is not executable'
bash -n "$ADAPTER"
"$ADAPTER" --self-test
"$ADAPTER" --help >/dev/null 2>&1

if "$ADAPTER" --self-test --dry-run >/dev/null 2>&1; then
    fail 'self-test accepted a runtime phase flag'
fi
if "$ADAPTER" --collect --gate-build-dir /tmp/not-a-gate-token \
        --worktree /tmp/not-created-worktree \
        --artifacts-dir /tmp/not-created-artifacts >/dev/null 2>&1; then
    fail 'collect accepted an unsafe gate directory before refusing execution'
fi

printf 'PASS: Tahoe IWN lab candidate-stage fixture\n'
