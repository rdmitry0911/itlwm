#!/usr/bin/env bash
# One credential-safe Tahoe laboratory batch with four isolated SAE/PMK epochs.
#
# This runner is an evidence collector, not an SAE implementation switch.  It
# verifies the exact WPA2 baseline and audited transition carriers separately,
# proves that pure SAE+PMF remains fail-closed, then repeats the baseline as a
# recovery check.  Every connect request uses an already saved macOS profile;
# no password is accepted, logged, or stored in a process argument.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
OUT_ROOT="${AIAM_SAE_CAPTURE_ROOT:-$ROOT/runtime-captures}"
CAPTURE_TOOL="${AIAM_SAE_LAB_CAPTURE_TOOL:-$ROOT/scripts/capture_tahoe_sae_layer.sh}"
NETWORKSETUP_TOOL="${AIAM_SAE_LAB_NETWORKSETUP_TOOL:-/usr/sbin/networksetup}"
ROUTE_TOOL="${AIAM_SAE_LAB_ROUTE_TOOL:-/usr/sbin/route}"
INTERFACE=""
WPA2_PSK_SSID=""
PURE_SAE_SSID=""
TRANSITION_SSID=""
SETTLE_SECONDS=15
STRICT=1

usage() {
    cat >&2 <<'EOF'
usage: run_tahoe_sae_lab_profiles.sh --interface IFACE \
       --wpa2-psk-ssid SSID --pure-sae-ssid SSID --sae-transition-ssid SSID
       [--out DIR] [--settle SEC] [--no-strict]

Runs four independently cleared diagnostic epochs in this fixed order:
  1. WPA2-PSK baseline: exact auth=0x8, policy=0x6, PMK/EAPOL/link success;
  2. pure SAE + required PMF: exact auth=0x1000, hidden-WCL pmf=1, reject;
  3. audited SAE-transition + PSK: exact auth=0x1008, policy=0xe, success;
  4. WPA2-PSK recovery: same exact baseline after the SAE rejection.

All three profiles must already be known to macOS.  The runner records only
SHA-256 profile identifiers and hashes of raw capture files in its local
attestation; raw trace/snapshot material is intentionally not versioned.
It invokes no address, route, DHCP, install, load, or reboot command.  The
default route is observed before the batch and after every epoch; a change
makes the batch inconclusive even though the runner itself never mutates it.

Compatibility aliases: --psk-ssid for --wpa2-psk-ssid and --sae-ssid for
--pure-sae-ssid.  They do not relax the exact evaluator expectations.
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --interface|--wpa2-psk-ssid|--psk-ssid|--pure-sae-ssid|--sae-ssid|--sae-transition-ssid|--out|--settle)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            case "$1" in
                --interface) INTERFACE="$2" ;;
                --wpa2-psk-ssid|--psk-ssid) WPA2_PSK_SSID="$2" ;;
                --pure-sae-ssid|--sae-ssid) PURE_SAE_SSID="$2" ;;
                --sae-transition-ssid) TRANSITION_SSID="$2" ;;
                --out) OUT_ROOT="$2" ;;
                --settle) SETTLE_SECONDS="$2" ;;
            esac
            shift 2
            ;;
        --no-strict)
            STRICT=0
            shift
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

case "$INTERFACE" in ''|*[!A-Za-z0-9_.-]*) usage; exit 2;; esac
case "$SETTLE_SECONDS" in ''|*[!0-9]*) usage; exit 2;; esac
[ "$SETTLE_SECONDS" -ge 1 ] && [ "$SETTLE_SECONDS" -le 120 ] || {
    usage
    exit 2
}
[ -n "$WPA2_PSK_SSID" ] && [ -n "$PURE_SAE_SSID" ] && [ -n "$TRANSITION_SSID" ] || {
    usage
    exit 2
}
command -v shasum >/dev/null 2>&1 || {
    echo "missing required shasum for credential-safe attestation" >&2
    exit 1
}
if [ "$CAPTURE_TOOL" != "$ROOT/scripts/capture_tahoe_sae_layer.sh" ] || \
   [ "$NETWORKSETUP_TOOL" != /usr/sbin/networksetup ] || \
   [ "$ROUTE_TOOL" != /usr/sbin/route ]; then
    [ "${AIAM_SAE_LAB_TEST_MODE:-}" = 1 ] || {
        echo "custom lab tools are accepted only with AIAM_SAE_LAB_TEST_MODE=1" >&2
        exit 2
    }
fi
[ -x "$CAPTURE_TOOL" ] || {
    echo "missing SAE capture tool: $CAPTURE_TOOL" >&2
    exit 1
}
[ -x "$NETWORKSETUP_TOOL" ] || {
    echo "missing networksetup tool: $NETWORKSETUP_TOOL" >&2
    exit 1
}
[ -x "$ROUTE_TOOL" ] || {
    echo "missing route observation tool: $ROUTE_TOOL" >&2
    exit 1
}

sha256_text() {
    printf '%s' "$1" | shasum -a 256 | awk '{ print $1 }'
}

sha256_file_or_missing() {
    if [ -f "$1" ]; then
        shasum -a 256 "$1" | awk '{ print $1 }'
    else
        printf 'missing'
    fi
}

default_route_signature() {
    "$ROUTE_TOOL" -n get default 2>/dev/null | awk '
        /^[[:space:]]*gateway:/ { gateway = $2 }
        /^[[:space:]]*interface:/ { iface = $2 }
        END {
            if (iface == "")
                exit 1
            if (gateway == "")
                gateway = "direct"
            printf "gateway=%s interface=%s", gateway, iface
        }'
}

runner_git_head() {
    git -C "$ROOT" rev-parse --verify HEAD 2>/dev/null || printf 'unavailable'
}

runner_tree_state() {
    if ! git -C "$ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        printf 'unavailable'
    elif [ -z "$(git -C "$ROOT" status --porcelain)" ]; then
        printf 'clean'
    else
        printf 'dirty'
    fi
}

umask 077
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$OUT_ROOT"
BATCH_DIR="$(mktemp -d "$OUT_ROOT/sae-pmf-four-epoch-$STAMP.XXXXXX")"
MANIFEST="$BATCH_DIR/manifest.txt"
ATTESTATION="$BATCH_DIR/committable-attestation.txt"
DEFAULT_ROUTE_BASELINE="$(default_route_signature)" || {
    echo "unable to observe the pre-batch default route" >&2
    exit 1
}
RUNNER_GIT_HEAD="$(runner_git_head)"
RUNNER_TREE_STATE="$(runner_tree_state)"

{
    printf 'schema_version=itlwm-tahoe-sae-pmf-four-epoch/v1\n'
    printf 'batch_utc=%s\n' "$STAMP"
    printf 'runner_git_head=%s\n' "$RUNNER_GIT_HEAD"
    printf 'runner_tree_state=%s\n' "$RUNNER_TREE_STATE"
    printf 'interface=%s\n' "$INTERFACE"
    printf 'password_carrier=keychain-only\n'
    printf 'explicit_route_command=none\n'
    printf 'explicit_address_command=none\n'
    printf 'explicit_dhcp_command=none\n'
    printf 'install_load_reboot_command=none\n'
    printf 'diagnostic_epoch=sae-on-clear-per-attempt\n'
    printf 'raw_capture_material=local-only-not-versioned\n'
    printf 'default_route_baseline_observed=true\n'
} >"$MANIFEST"

{
    printf 'schema_version=itlwm-tahoe-sae-pmf-committable-attestation/v1\n'
    printf 'batch_utc=%s\n' "$STAMP"
    printf 'runner_git_head=%s\n' "$RUNNER_GIT_HEAD"
    printf 'runner_tree_state=%s\n' "$RUNNER_TREE_STATE"
    printf 'credential_material=not-recorded\n'
    printf 'profile_identifiers=sha256-only\n'
    printf 'raw_capture_material=not-versioned\n'
    printf 'pure_sae_pmf_success_claim=false\n'
    printf 'explicit_route_address_dhcp_mutation=false\n'
} >"$ATTESTATION"

run_epoch() {
    local label="$1"
    local expected="$2"
    local ssid="$3"
    local log="$BATCH_DIR/$label-run.txt"
    local rc capture_rc evidence verdict default_route_after route_preserved
    local -a capture_args

    capture_args=(
        --out "$BATCH_DIR"
        --interface "$INTERFACE"
        --settle "$SETTLE_SECONDS"
        --expect "$expected"
    )
    if [ "$STRICT" -ne 0 ]; then
        capture_args+=(--strict)
    fi
    if "$CAPTURE_TOOL" "${capture_args[@]}" \
        -- "$NETWORKSETUP_TOOL" -setairportnetwork "$INTERFACE" "$ssid" \
        >"$log" 2>&1; then
        capture_rc=0
    else
        capture_rc=$?
    fi
    rc=$capture_rc
    evidence="$(sed -n 's/^  evidence: //p' "$log" | tail -n 1)"
    verdict=missing
    if [ -n "$evidence" ] && [ -f "$evidence/verdict.txt" ]; then
        cp "$evidence/verdict.txt" "$BATCH_DIR/$label-verdict.txt"
        if grep -Fq "expect=$expected verdict=PASS " "$evidence/verdict.txt"; then
            verdict=PASS
        else
            verdict=INCONCLUSIVE_OR_FAIL
        fi
    fi
    [ "$verdict" = PASS ] || rc=1
    default_route_after="$(default_route_signature || true)"
    if [ -n "$default_route_after" ] && \
       [ "$default_route_after" = "$DEFAULT_ROUTE_BASELINE" ]; then
        route_preserved=true
    else
        route_preserved=false
        rc=1
    fi

    {
        printf '%s_epoch_exit=%s\n' "$label" "$rc"
        printf '%s_capture_exit=%s\n' "$label" "$capture_rc"
        printf '%s_expected=%s\n' "$label" "$expected"
        printf '%s_verdict=%s\n' "$label" "$verdict"
        printf '%s_default_route_preserved=%s\n' "$label" "$route_preserved"
        if [ -n "$evidence" ] && [ -f "$evidence/verdict.txt" ]; then
            printf '%s_evidence_present=true\n' "$label"
        else
            printf '%s_evidence_present=false\n' "$label"
        fi
    } >>"$MANIFEST"

    {
        printf 'scenario_begin=%s\n' "$label"
        printf 'expect=%s\n' "$expected"
        printf 'profile_identifier_sha256=%s\n' "$(sha256_text "$ssid")"
        printf 'capture_exit=%s\n' "$capture_rc"
        printf 'scenario_exit=%s\n' "$rc"
        printf 'verdict=%s\n' "$verdict"
        printf 'default_route_preserved=%s\n' "$route_preserved"
        printf 'capture_manifest_sha256=%s\n' "$(sha256_file_or_missing "$evidence/manifest.txt")"
        printf 'capture_control_sha256=%s\n' "$(sha256_file_or_missing "$evidence/regdiag-control.txt")"
        printf 'capture_snapshot_sha256=%s\n' "$(sha256_file_or_missing "$evidence/post-snapshot.txt")"
        printf 'capture_trace_sha256=%s\n' "$(sha256_file_or_missing "$evidence/trace.txt")"
        printf 'capture_verdict_sha256=%s\n' "$(sha256_file_or_missing "$evidence/verdict.txt")"
        printf 'scenario_end=%s\n' "$label"
    } >>"$ATTESTATION"
    return "$rc"
}

if run_epoch wpa2-psk-baseline wpa2-psk "$WPA2_PSK_SSID"; then
    BASELINE_RC=0
else
    BASELINE_RC=$?
fi
if run_epoch pure-sae-required-pmf-reject pure-sae-required-pmf-reject "$PURE_SAE_SSID"; then
    PURE_SAE_RC=0
else
    PURE_SAE_RC=$?
fi
if run_epoch sae-transition-psk sae-transition-psk "$TRANSITION_SSID"; then
    TRANSITION_RC=0
else
    TRANSITION_RC=$?
fi
if run_epoch wpa2-psk-recovery wpa2-psk "$WPA2_PSK_SSID"; then
    RECOVERY_RC=0
else
    RECOVERY_RC=$?
fi

if [ "$BASELINE_RC" -eq 0 ] && [ "$PURE_SAE_RC" -eq 0 ] && \
   [ "$TRANSITION_RC" -eq 0 ] && [ "$RECOVERY_RC" -eq 0 ]; then
    OVERALL=PASS
    OVERALL_RC=0
else
    OVERALL=INCONCLUSIVE_OR_FAIL
    OVERALL_RC=1
fi
printf 'overall=%s\n' "$OVERALL" >>"$MANIFEST"
printf 'overall=%s\n' "$OVERALL" >>"$ATTESTATION"

echo "CAPTURED: SAE/PMF four-epoch laboratory batch"
echo "  evidence: $BATCH_DIR"
echo "  attestation: $ATTESTATION"
echo "  baseline verdict: $BATCH_DIR/wpa2-psk-baseline-verdict.txt"
echo "  pure-SAE verdict: $BATCH_DIR/pure-sae-required-pmf-reject-verdict.txt"
echo "  transition verdict: $BATCH_DIR/sae-transition-psk-verdict.txt"
echo "  recovery verdict: $BATCH_DIR/wpa2-psk-recovery-verdict.txt"
echo "  overall: $OVERALL"
if [ "$STRICT" -ne 0 ]; then
    exit "$OVERALL_RC"
fi
exit 0
