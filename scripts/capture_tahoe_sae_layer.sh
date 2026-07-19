#!/usr/bin/env bash
# Capture one complete Tahoe SAE/PMK association timeline around one explicit
# caller-supplied connect command.  This script never assigns an address,
# changes routes, installs/loads a kext, or reboots a host.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
TOOL="${AIAM_REGDIAG_TOOL:-$ROOT/Build/Debug/Tahoe/airport_itlwm_regdiag}"
OUT_ROOT="${AIAM_SAE_CAPTURE_ROOT:-$ROOT/runtime-captures}"
SETTLE_SECONDS=15

usage() {
    cat >&2 <<'EOF'
usage: capture_tahoe_sae_layer.sh [--tool PATH] [--out DIR] [--settle SEC] -- COMMAND [ARG ...]

Runs COMMAND exactly once while opt-in RegDiag SAE/PMK tracing is enabled,
then saves a non-secret association/PMK/PLTI/EAPOL report.  The caller owns
the connection command; this wrapper never changes IP addressing or routing.
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
EVIDENCE_DIR="$OUT_ROOT/sae-layer-$STAMP"
mkdir -p "$EVIDENCE_DIR"

disable_diag() {
    "$TOOL" off >/dev/null 2>&1 || true
}
trap disable_diag EXIT HUP INT TERM

{
    printf 'capture_utc=%s\n' "$STAMP"
    printf 'trigger_program=%s\n' "$1"
    printf 'trigger_arg_count=%s\n' "$#"
    printf 'settle_seconds=%s\n' "$SETTLE_SECONDS"
    printf 'routing_mutation=none\n'
    printf 'ip_address_mutation=none\n'
} >"$EVIDENCE_DIR/manifest.txt"

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
"$TOOL" off >>"$EVIDENCE_DIR/regdiag-enable.txt"
trap - EXIT HUP INT TERM

echo "PASS: SAE/PMK layer capture complete"
echo "  evidence: $EVIDENCE_DIR"
echo "  trigger exit: $TRIGGER_RC (recorded; not treated as capture failure)"
