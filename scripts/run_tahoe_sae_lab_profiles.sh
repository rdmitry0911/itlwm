#!/usr/bin/env bash
# One top-level lab run with two independently cleared diagnostic epochs.
#
# A single mixed trace cannot safely assign pre-WCL direct PMK to one of two
# association attempts.  This runner solves that ambiguity without a new kext
# ABI: pure SAE and PSK each get sae-on/clear, their own evidence directory,
# and their own strict verdict.  It never accepts or prints a password; macOS
# must already have the credentials in its Keychain/known-network store.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
OUT_ROOT="${AIAM_SAE_CAPTURE_ROOT:-$ROOT/runtime-captures}"
INTERFACE=""
SAE_SSID=""
PSK_SSID=""
SETTLE_SECONDS=15
STRICT=1

usage() {
    cat >&2 <<'EOF'
usage: run_tahoe_sae_lab_profiles.sh --interface IFACE --sae-ssid SSID --psk-ssid SSID
       [--out DIR] [--settle SEC] [--no-strict]

Runs one top-level lab batch with two separately cleared SAE/PMK epochs:
  1. pure SAE SSID: must fail before PMK/PLTI/EAPOL/link;
  2. PSK or audited SAE-transition SSID: must reach PMK/PLTI, EAPOL, link.

Both SSIDs must already be known to macOS.  No password is accepted, logged,
or placed in a process argument.  The wrapper does not assign an address or
change routes; it records before/after state for each Wi-Fi attempt.
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --interface|--sae-ssid|--psk-ssid|--out|--settle)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            case "$1" in
                --interface) INTERFACE="$2" ;;
                --sae-ssid) SAE_SSID="$2" ;;
                --psk-ssid) PSK_SSID="$2" ;;
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
[ "$SETTLE_SECONDS" -ge 1 ] && [ "$SETTLE_SECONDS" -le 120 ] || { usage; exit 2; }
[ -n "$SAE_SSID" ] && [ -n "$PSK_SSID" ] || { usage; exit 2; }

STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$OUT_ROOT"
BATCH_DIR="$(mktemp -d "$OUT_ROOT/sae-layer-batch-$STAMP.XXXXXX")"

run_epoch() {
    local label="$1"
    local expected="$2"
    local ssid="$3"
    local log="$BATCH_DIR/$label-run.txt"
    local rc evidence

    if "$ROOT/scripts/capture_tahoe_sae_layer.sh" \
        --out "$BATCH_DIR" \
        --interface "$INTERFACE" \
        --settle "$SETTLE_SECONDS" \
        --expect "$expected" \
        --strict \
        -- /usr/sbin/networksetup -setairportnetwork "$INTERFACE" "$ssid" \
        >"$log" 2>&1; then
        rc=0
    else
        rc=$?
    fi
    evidence="$(sed -n 's/^  evidence: //p' "$log" | tail -n 1)"
    printf '%s_epoch_exit=%s\n' "$label" "$rc" >>"$BATCH_DIR/manifest.txt"
    if [ -n "$evidence" ] && [ -f "$evidence/verdict.txt" ]; then
        printf '%s_evidence=%s\n' "$label" "$evidence" >>"$BATCH_DIR/manifest.txt"
        cp "$evidence/verdict.txt" "$BATCH_DIR/$label-verdict.txt"
    else
        printf '%s_evidence=missing\n' "$label" >>"$BATCH_DIR/manifest.txt"
    fi
    return "$rc"
}

{
    printf 'batch_utc=%s\n' "$STAMP"
    printf 'interface=%s\n' "$INTERFACE"
    printf 'password_carrier=keychain-only\n'
    printf 'routing_mutation=none\n'
    printf 'ip_address_mutation=none\n'
    printf 'epoch_isolation=sae-on-clear-per-attempt\n'
} >"$BATCH_DIR/manifest.txt"

if run_epoch sae sae-reject "$SAE_SSID"; then
    SAE_RC=0
else
    SAE_RC=$?
fi
if run_epoch psk wpa2-psk "$PSK_SSID"; then
    PSK_RC=0
else
    PSK_RC=$?
fi

if [ "$SAE_RC" -eq 0 ] && [ "$PSK_RC" -eq 0 ]; then
    OVERALL=PASS
    OVERALL_RC=0
else
    OVERALL=INCONCLUSIVE_OR_FAIL
    OVERALL_RC=1
fi
printf 'overall=%s\n' "$OVERALL" >>"$BATCH_DIR/manifest.txt"

echo "CAPTURED: SAE two-epoch laboratory batch"
echo "  evidence: $BATCH_DIR"
echo "  SAE verdict: $BATCH_DIR/sae-verdict.txt"
echo "  PSK verdict: $BATCH_DIR/psk-verdict.txt"
echo "  overall: $OVERALL"
if [ "$STRICT" -ne 0 ]; then
    exit "$OVERALL_RC"
fi
