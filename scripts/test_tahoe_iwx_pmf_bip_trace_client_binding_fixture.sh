#!/usr/bin/env bash
# Local-only regression fixture for the IWX trace-client byte-binding gate.
#
# It extracts the runner's remote heredocs and evaluates them only after
# rewriting their restricted guest path prefix into a private temporary tree.
# No SSH client, guest, AP, radio, or network command is invoked.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
RUNNER="$ROOT/scripts/run_tahoe_iwx_pmf_bip_runtime.sh"

fail() {
    printf 'FAIL: IWX trace-client binding fixture: %s\n' "$*" >&2
    exit 1
}

[ -f "$RUNNER" ] || fail 'runner is missing'

FIXTURE_DIR="$(mktemp -d /tmp/aiam-iwx-trace-client-binding.XXXXXX)"
TRUSTED_DIR="$FIXTURE_DIR/aiam-post-plti-trace-trusted"
WRONG_DIR="$FIXTURE_DIR/aiam-post-plti-trace-wrong"
SYMLINK_DIR="$FIXTURE_DIR/aiam-post-plti-trace-symlink"
SWAP_DIR="$FIXTURE_DIR/aiam-post-plti-trace-swap"
PARENT_REAL_DIR="$FIXTURE_DIR/aiam-post-plti-trace-parent-real"
PARENT_LINK_DIR="$FIXTURE_DIR/aiam-post-plti-trace-parent-link"
TRUSTED_TOOL="$TRUSTED_DIR/airport_itlwm_post_plti_trace"
WRONG_TOOL="$WRONG_DIR/airport_itlwm_post_plti_trace"
SYMLINK_TOOL="$SYMLINK_DIR/airport_itlwm_post_plti_trace"
SWAP_TOOL="$SWAP_DIR/airport_itlwm_post_plti_trace"
PARENT_LINK_TOOL="$PARENT_LINK_DIR/airport_itlwm_post_plti_trace"
EXEC_LOG="$FIXTURE_DIR/client-exec.log"
RESET_LOG="$FIXTURE_DIR/reset-request.log"

cleanup() {
    unlink "$TRUSTED_TOOL" 2>/dev/null || true
    unlink "$WRONG_TOOL" 2>/dev/null || true
    unlink "$SYMLINK_TOOL" 2>/dev/null || true
    unlink "$SWAP_TOOL" 2>/dev/null || true
    unlink "$PARENT_REAL_DIR/airport_itlwm_post_plti_trace" 2>/dev/null || true
    unlink "$PARENT_LINK_DIR" 2>/dev/null || true
    unlink "$EXEC_LOG" 2>/dev/null || true
    unlink "$RESET_LOG" 2>/dev/null || true
    rmdir "$TRUSTED_DIR" "$WRONG_DIR" "$SYMLINK_DIR" "$SWAP_DIR" \
        "$PARENT_REAL_DIR" "$FIXTURE_DIR" 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM

mkdir -p "$TRUSTED_DIR" "$WRONG_DIR" "$SYMLINK_DIR" "$SWAP_DIR" \
    "$PARENT_REAL_DIR"

make_client() {
    local path="$1" label="$2"
    printf '%s\n' '#!/bin/sh' \
        "printf '%s\\n' '$label' >>\"\$TRACE_BINDING_EXEC_LOG\"" >"$path"
    chmod 700 "$path"
}

make_client "$TRUSTED_TOOL" trusted-client
make_client "$WRONG_TOOL" wrong-client
make_client "$SWAP_TOOL" trusted-client
make_client "$PARENT_REAL_DIR/airport_itlwm_post_plti_trace" trusted-client
ln -s "$TRUSTED_TOOL" "$SYMLINK_TOOL"
ln -s "$PARENT_REAL_DIR" "$PARENT_LINK_DIR"
EXPECTED_SHA256="$(shasum -a 256 "$TRUSTED_TOOL" | awk 'NR == 1 { print $1; exit }')"
[[ "$EXPECTED_SHA256" =~ ^[0-9a-f]{64}$ ]] || fail 'trusted digest is malformed'
export TRACE_BINDING_EXEC_LOG="$EXEC_LOG"

extract_remote_body() {
    local function_name="$1"
    awk -v marker="$function_name() {" '
        $0 == marker { in_function = 1; next }
        in_function && $0 ~ /<<'\''REMOTE'\''/ { in_heredoc = 1; next }
        in_heredoc && $0 == "REMOTE" { exit }
        in_heredoc { print }
    ' "$RUNNER"
}

rewrite_guest_paths() {
    sed "s|/private/tmp|$FIXTURE_DIR|g"
}

run_remote_body() {
    local body="$1" tool="$2"
    printf '%s\n' "$body" | /bin/bash -s -- "$tool" "$EXPECTED_SHA256" get control
}

request_reset_after_preflight() {
    local body="$1" tool="$2"
    if run_remote_body "$body" "$tool"; then
        printf '%s\n' reset-requested >"$RESET_LOG"
        return 0
    fi
    return 1
}

# This models the exact legacy admission predicate and proves that an
# executable symlink was once sufficient before the extracted runner guard is
# asked to reject it.
legacy_executable() {
    [ -x "$1" ]
}
legacy_executable "$TRUSTED_TOOL" || fail 'legacy model rejected trusted executable'
legacy_executable "$WRONG_TOOL" || fail 'legacy model rejected wrong executable'
legacy_executable "$SYMLINK_TOOL" || fail 'legacy model did not reproduce executable symlink acceptance'
legacy_executable "$PARENT_LINK_TOOL" || fail 'legacy model did not reproduce parent-symlink acceptance'

PREFLIGHT_BODY="$(extract_remote_body remote_trace_client_exists | rewrite_guest_paths)"
[ -n "$PREFLIGHT_BODY" ] || fail 'runner trace-client preflight body is missing'
request_reset_after_preflight "$PREFLIGHT_BODY" "$TRUSTED_TOOL" ||
    fail 'trace-client binding rejected the trusted regular executable'
[ -s "$RESET_LOG" ] || fail 'trusted trace-client preflight did not authorize simulated reset'
unlink "$RESET_LOG"
if request_reset_after_preflight "$PREFLIGHT_BODY" "$SYMLINK_TOOL"; then
    fail 'trace client binding fixture accepted executable symlink'
fi
[ ! -e "$RESET_LOG" ] || fail 'executable symlink reached simulated reset'
if request_reset_after_preflight "$PREFLIGHT_BODY" "$PARENT_LINK_TOOL"; then
    fail 'trace client binding fixture accepted parent-directory symlink'
fi
[ ! -e "$RESET_LOG" ] || fail 'parent-directory symlink reached simulated reset'
if request_reset_after_preflight "$PREFLIGHT_BODY" "$WRONG_TOOL"; then
    fail 'trace client binding fixture accepted wrong executable digest'
fi
[ ! -e "$RESET_LOG" ] || fail 'wrong trace-client digest reached simulated reset'

# Bind one regular path, replace its leaf with a same-digest symlink, then run
# the extracted per-command heredoc. This checks the guard at execution time,
# not only once during preflight.
TRACE_BODY="$(extract_remote_body remote_trace | rewrite_guest_paths)"
[ -n "$TRACE_BODY" ] || fail 'runner trace-client execution body is missing'
run_remote_body "$PREFLIGHT_BODY" "$SWAP_TOOL" ||
    fail 'trace-client preflight rejected the regular swap fixture'
run_remote_body "$TRACE_BODY" "$SWAP_TOOL" ||
    fail 'trace-client execution rejected the trusted regular executable'
[ -s "$EXEC_LOG" ] || fail 'trusted trace client did not reach execution body'
unlink "$EXEC_LOG"
unlink "$SWAP_TOOL"
ln "$WRONG_TOOL" "$SWAP_TOOL"
if run_remote_body "$TRACE_BODY" "$SWAP_TOOL"; then
    fail 'post-preflight wrong trace-client bytes reached client exec'
fi
[ ! -e "$EXEC_LOG" ] || fail 'post-preflight wrong trace-client bytes executed client'
unlink "$SWAP_TOOL"
ln -s "$TRUSTED_TOOL" "$SWAP_TOOL"
if run_remote_body "$TRACE_BODY" "$SWAP_TOOL"; then
    fail 'post-preflight trace-client swap reached client exec'
fi
[ ! -e "$EXEC_LOG" ] || fail 'post-preflight trace-client swap executed client'

printf 'PASS: IWX trace-client byte-binding local fixture\n'
