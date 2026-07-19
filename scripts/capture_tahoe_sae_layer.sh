#!/usr/bin/env bash
# Capture one complete Tahoe SAE/PMK association timeline around one explicit
# caller-supplied connect command.  This script never assigns an address,
# changes routes, installs/loads a kext, or reboots a host.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
TOOL="${AIAM_REGDIAG_TOOL:-$ROOT/Build/Debug/Tahoe/airport_itlwm_regdiag}"
OUT_ROOT="${AIAM_SAE_CAPTURE_ROOT:-$ROOT/runtime-captures}"
SETTLE_SECONDS=15
EXPECT="auto"
INTERFACE=""
STRICT=0
EVALUATOR="$ROOT/scripts/evaluate_tahoe_sae_capture.py"

usage() {
    cat >&2 <<'EOF'
usage: capture_tahoe_sae_layer.sh [--tool PATH] [--out DIR] [--settle SEC]
       [--expect auto|wpa2-psk|sae-reject] [--interface IFACE] [--strict]
       -- COMMAND [ARG ...]

Runs COMMAND exactly once while opt-in RegDiag SAE/PMK tracing is enabled,
then saves a non-secret association/PMK/PLTI/EAPOL report.  The caller owns
the connection command; this wrapper never changes IP addressing or routing.
--interface adds read-only before/after interface and routing evidence only.
--strict makes an unmet --expect return non-zero after evidence is saved.
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --tool)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            TOOL="$2"
            shift 2
            ;;
        --out)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            OUT_ROOT="$2"
            shift 2
            ;;
        --settle)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            SETTLE_SECONDS="$2"
            shift 2
            ;;
        --expect)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            EXPECT="$2"
            shift 2
            ;;
        --interface)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            INTERFACE="$2"
            shift 2
            ;;
        --strict)
            STRICT=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            break
            ;;
        *)
            usage
            exit 2
            ;;
    esac
done

[ "$#" -gt 0 ] || { usage; exit 2; }
[ -x "$TOOL" ] || {
    echo "missing RegDiag client: $TOOL (build it with scripts/build_regdiag.sh)" >&2
    exit 1
}
[ -x "$EVALUATOR" ] || {
    echo "missing SAE/PMK capture evaluator: $EVALUATOR" >&2
    exit 1
}
case "$EXPECT" in
    auto|wpa2-psk|sae-reject) ;;
    *)
        echo "--expect must be auto, wpa2-psk, or sae-reject" >&2
        exit 2
        ;;
esac
case "$INTERFACE" in
    ''|*[!A-Za-z0-9_.-]*)
        if [ -n "$INTERFACE" ]; then
            echo "--interface contains unsupported characters" >&2
            exit 2
        fi
        ;;
esac
case "$SETTLE_SECONDS" in
    ''|*[!0-9]*)
        echo "--settle must be an integer number of seconds" >&2
        exit 2
        ;;
esac
if [ "$SETTLE_SECONDS" -lt 1 ] || [ "$SETTLE_SECONDS" -gt 120 ]; then
    echo "--settle must be between 1 and 120 seconds" >&2
    exit 2
fi

STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$OUT_ROOT"
EVIDENCE_DIR="$(mktemp -d "$OUT_ROOT/sae-layer-$STAMP.XXXXXX")"

disable_diag() {
    "$TOOL" off >/dev/null 2>&1 || true
}
trap disable_diag EXIT HUP INT TERM

{
    printf 'capture_utc=%s\n' "$STAMP"
    printf 'trigger_program=%s\n' "$1"
    printf 'trigger_arg_count=%s\n' "$#"
    printf 'settle_seconds=%s\n' "$SETTLE_SECONDS"
    printf 'expect=%s\n' "$EXPECT"
    printf 'interface=%s\n' "${INTERFACE:-none}"
    printf 'routing_mutation=none\n'
    printf 'ip_address_mutation=none\n'
} >"$EVIDENCE_DIR/manifest.txt"

# These are read-only snapshots.  Capturing them lets a single experiment
# distinguish a driver/association failure from an accidental environment
# change without allowing this wrapper to alter routes or addresses.
netstat -rn -f inet >"$EVIDENCE_DIR/routes-before.txt" 2>&1 || true
if [ -n "$INTERFACE" ]; then
    ifconfig "$INTERFACE" >"$EVIDENCE_DIR/interface-before.txt" 2>&1 || true
fi

"$TOOL" sae-on >"$EVIDENCE_DIR/regdiag-enable.txt"
sleep 1
"$TOOL" get control >"$EVIDENCE_DIR/regdiag-control.txt"
if ! "$TOOL" get snapshot >"$EVIDENCE_DIR/pre-snapshot.txt" 2>&1; then
    printf 'pre_snapshot=not-yet-published\n' >>"$EVIDENCE_DIR/manifest.txt"
fi

# Deliberately discard arbitrary trigger output: connect commands often carry
# a passphrase, and the diagnostics need structural evidence rather than it.
set +e
"$@" >/dev/null 2>&1
TRIGGER_RC=$?
set -e
printf 'trigger_exit=%s\n' "$TRIGGER_RC" >>"$EVIDENCE_DIR/manifest.txt"

sleep "$SETTLE_SECONDS"
"$TOOL" get report >"$EVIDENCE_DIR/report.txt"
"$TOOL" get snapshot >"$EVIDENCE_DIR/post-snapshot.txt"
"$TOOL" get trace >"$EVIDENCE_DIR/trace.txt"
netstat -rn -f inet >"$EVIDENCE_DIR/routes-after.txt" 2>&1 || true
if [ -n "$INTERFACE" ]; then
    ifconfig "$INTERFACE" >"$EVIDENCE_DIR/interface-after.txt" 2>&1 || true
fi
"$TOOL" off >>"$EVIDENCE_DIR/regdiag-enable.txt"
trap - EXIT HUP INT TERM

set +e
"$EVALUATOR" --expect "$EXPECT" "$EVIDENCE_DIR" >"$EVIDENCE_DIR/verdict.txt" 2>&1
EVALUATION_RC=$?
set -e
printf 'evaluation_exit=%s\n' "$EVALUATION_RC" >>"$EVIDENCE_DIR/manifest.txt"

echo "CAPTURED: SAE/PMK layer evidence complete"
echo "  evidence: $EVIDENCE_DIR"
echo "  trigger exit: $TRIGGER_RC (recorded; not treated as capture failure)"
echo "  layer verdict: $EVIDENCE_DIR/verdict.txt"
if [ "$STRICT" -ne 0 ] && [ "$EVALUATION_RC" -ne 0 ]; then
    exit "$EVALUATION_RC"
fi
