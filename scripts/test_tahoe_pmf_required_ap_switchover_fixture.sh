#!/usr/bin/env bash
# Local fixture for the PMF-required hostapd helper.
#
# Every command path is replaced under explicit test mode.  This never reads
# the live AP configuration, starts a real hostapd, or changes host networking.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
HELPER="$ROOT/scripts/tahoe_pmf_required_ap_switchover.sh"
TMP_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/aiam-pmf-ap-helper-fixture.XXXXXX")"
umask 077

cleanup() {
    local pidfile pid
    set +e
    for pidfile in "$TMP_ROOT/run/hostapd-5g.pid" \
                   "$TMP_ROOT/run/hostapd-5g-pmf-required.pid" \
                   "$TMP_ROOT"/state.*/watchdog.pid; do
        [ -r "$pidfile" ] || continue
        pid="$(tr -d '[:space:]' <"$pidfile" 2>/dev/null || true)"
        case "$pid" in ''|*[!0-9]*) continue;; esac
        /bin/kill "$pid" >/dev/null 2>&1 || true
    done
    /bin/rm -rf -- "$TMP_ROOT"
}
trap cleanup EXIT

fail() {
    printf 'FAIL: PMF-required AP helper fixture: %s\n' "$*" >&2
    exit 1
}

LAB_ROOT="$TMP_ROOT/lab"
RUN_DIR="$TMP_ROOT/run"
STATE_PREFIX="$TMP_ROOT/state."
CONTROL_DIR="$TMP_ROOT/control"
OPTIONAL="$LAB_ROOT/hostapd-5g.conf"
REQUIRED="$LAB_ROOT/hostapd-5g-wpa2-pmf.conf"
FAKE_HOSTAPD="$TMP_ROOT/hostapd"
FAKE_CLI="$TMP_ROOT/hostapd_cli"
FAKE_IW="$TMP_ROOT/iw"
FAKE_IP="$TMP_ROOT/ip"
FAKE_SYSCTL="$TMP_ROOT/sysctl"
FAKE_SUDO="$TMP_ROOT/sudo"
FAKE_HOSTAPD_LOG="$TMP_ROOT/fake-hostapd.log"
FAKE_CLI_LOG="$TMP_ROOT/fake-cli.log"
FAKE_NETWORK_STATE="$TMP_ROOT/fake-network-state"
# This ephemeral token exists only inside the protected local fixture config.
# It is never supplied to a real AP, printed, committed, or reused as a lab
# credential; the helper needs a nonempty syntactic field to exercise its
# credential-free comparison path.
FIXTURE_CONFIG_TOKEN="$(/usr/bin/od -An -N16 -tx1 /dev/urandom | tr -d '[:space:]')"

mkdir -p "$LAB_ROOT" "$RUN_DIR"

write_config() {
    local path="$1" pmf="$2" akm="$3" ssid="$4"
    {
        printf 'interface=wlp0s20f3\n'
        printf 'driver=nl80211\n'
        printf 'hw_mode=a\n'
        printf 'channel=149\n'
        printf 'ieee80211n=1\n'
        printf 'ieee80211ac=1\n'
        printf 'vht_oper_chwidth=1\n'
        printf 'vht_oper_centr_freq_seg0_idx=155\n'
        printf 'wpa=2\n'
        printf 'rsn_pairwise=CCMP\n'
        printf 'ctrl_interface=/run/hostapd\n'
        printf 'ieee80211w=%s\n' "$pmf"
        printf 'wpa_key_mgmt=%s\n' "$akm"
        printf 'ssid=%s\n' "$ssid"
        printf 'wpa_passphrase=%s\n' "$FIXTURE_CONFIG_TOKEN"
    } >"$path"
}

write_config "$OPTIONAL" 1 'WPA-PSK WPA-PSK-SHA256 SAE' fixture-network
write_config "$REQUIRED" 2 'WPA-PSK-SHA256' fixture-network

printf '%s\n' \
    '#!/usr/bin/env bash' \
    'set -euo pipefail' \
    'if [ "${1:-}" = --child ]; then' \
    '    shift' \
    '    pidfile=""' \
    '    while [ "$#" -gt 0 ]; do' \
    '        case "$1" in' \
    '            -P) pidfile="$2"; shift 2 ;;' \
    '            *) shift ;;' \
    '        esac' \
    '    done' \
    '    trap '\''[ -z "$pidfile" ] || /bin/rm -f -- "$pidfile"; exit 0'\'' TERM INT' \
    '    while :; do sleep 1; done' \
    'fi' \
    'pidfile=""' \
    'logfile=""' \
    'config=""' \
    'while [ "$#" -gt 0 ]; do' \
    '    case "$1" in' \
    '        -B) shift ;;' \
    '        -P) pidfile="$2"; shift 2 ;;' \
    '        -f) logfile="$2"; shift 2 ;;' \
    '        *) config="$1"; shift ;;' \
    '    esac' \
    'done' \
    '[ -n "$pidfile" ] && [ -n "$logfile" ] && [ -n "$config" ]' \
    'if [ "${FAKE_FAIL_REQUIRED:-0}" = 1 ] && [[ "$config" = *hostapd-5g-wpa2-pmf.conf ]]; then exit 1; fi' \
    '"$0" --child -B -P "$pidfile" -f "$logfile" "$config" &' \
    'child=$!' \
    'printf "%s\n" "$child" >"$pidfile"' \
    ': >"$logfile"' \
    'printf "%s\n" "${config##*/}" >>"$FAKE_HOSTAPD_LOG"' \
    >"$FAKE_HOSTAPD"

printf '%s\n' \
    '#!/usr/bin/env bash' \
    'set -euo pipefail' \
    '[ "$*" = "-p /run/hostapd -i wlp0s20f3 raw REKEY_GTK" ] || exit 64' \
    'printf "%s\n" "$*" >>"$FAKE_CLI_LOG"' \
    'if [ "${FAKE_MUTATE_DURING_REKEY:-0}" = 1 ]; then printf "drift\n" >"$FAKE_NETWORK_STATE"; fi' \
    'printf "OK\n"' \
    >"$FAKE_CLI"

printf '%s\n' \
    '#!/usr/bin/env bash' \
    'set -euo pipefail' \
    'printf "Interface wlp0s20f3\n"' \
    'printf "\ttype AP\n"' \
    'printf "\tchannel 153 (5775 MHz), width: 80 MHz, center1: 5775 MHz\n"' \
    >"$FAKE_IW"

printf '%s\n' \
    '#!/usr/bin/env bash' \
    'set -euo pipefail' \
    'case "$*" in' \
    '  "-4 route show default")' \
    '      state="$(cat "$FAKE_NETWORK_STATE" 2>/dev/null || true)"' \
    '      if [ "${FAKE_NETWORK_MUTATED:-0}" = 1 ] || [ "$state" = drift ]; then printf "default fixture-route-mutated\n"; else printf "default fixture-route\n"; fi ;;' \
    '  "-4 -o addr show dev wlp0s20f3") printf "fixture-address\n" ;;' \
    '  *) exit 64 ;;' \
    'esac' \
    >"$FAKE_IP"

printf '%s\n' \
    '#!/usr/bin/env bash' \
    'set -euo pipefail' \
    '[ "$*" = "-n net.ipv4.ip_forward" ] || exit 64' \
    'printf "1\n"' \
    >"$FAKE_SYSCTL"

printf '%s\n' \
    '#!/usr/bin/env bash' \
    'set -euo pipefail' \
    '[ "${1:-}" = -n ] && shift' \
    'exec "$@"' \
    >"$FAKE_SUDO"

chmod +x "$FAKE_HOSTAPD" "$FAKE_CLI" "$FAKE_IW" "$FAKE_IP" \
    "$FAKE_SYSCTL" "$FAKE_SUDO"

export AIAM_PMF_AP_TEST_MODE=1
export AIAM_PMF_AP_LAB_ROOT="$LAB_ROOT"
export AIAM_PMF_AP_OPTIONAL_CONFIG="$OPTIONAL"
export AIAM_PMF_AP_REQUIRED_CONFIG="$REQUIRED"
export AIAM_PMF_AP_HOSTAPD="$FAKE_HOSTAPD"
export AIAM_PMF_AP_HOSTAPD_CLI="$FAKE_CLI"
export AIAM_PMF_AP_IW="$FAKE_IW"
export AIAM_PMF_AP_IP="$FAKE_IP"
export AIAM_PMF_AP_SYSCTL="$FAKE_SYSCTL"
export AIAM_PMF_AP_SUDO="$FAKE_SUDO"
export AIAM_PMF_AP_RUN_DIR="$RUN_DIR"
export AIAM_PMF_AP_STATE_PREFIX="$STATE_PREFIX"
export AIAM_PMF_AP_CONTROL_DIR="$CONTROL_DIR"
export FAKE_HOSTAPD_LOG FAKE_CLI_LOG FAKE_NETWORK_STATE
: >"$FAKE_CLI_LOG"
printf 'stable\n' >"$FAKE_NETWORK_STATE"

"$FAKE_HOSTAPD" -B -P "$RUN_DIR/hostapd-5g.pid" \
    -f "$RUN_DIR/hostapd-5g.log" "$OPTIONAL"
sleep 1

"$HELPER" --preflight >"$TMP_ROOT/preflight.out" 2>"$TMP_ROOT/preflight.err" ||
    fail 'synthetic optional-PMF preflight failed'
grep -Fxq 'PMF_AP_PREFLIGHT=PASS' "$TMP_ROOT/preflight.out" ||
    fail 'preflight did not report categorical success'

STATE_DIR="$(mktemp -d "$STATE_PREFIX"XXXXXX)"
"$HELPER" --activate --state-dir "$STATE_DIR" --lease-seconds 60 \
    >"$TMP_ROOT/activate.out" 2>"$TMP_ROOT/activate.err" ||
    fail 'synthetic required-PMF activation failed'
grep -Fxq 'PMF_AP_SWITCHOVER=REQUIRED_ACTIVE' "$TMP_ROOT/activate.out" ||
    fail 'activation did not report required-PMF active'
grep -Fxq 'state=required' "$STATE_DIR/state.txt" ||
    fail 'required AP was not promoted from rollback-armed state'
[ -r "$STATE_DIR/watchdog.pid" ] || fail 'activation did not retain a watchdog'
[ -r "$RUN_DIR/hostapd-5g-pmf-required.pid" ] ||
    fail 'required hostapd pid was not created'
[ ! -e "$RUN_DIR/hostapd-5g.pid" ] || fail 'optional hostapd pid survived activation'

# A host-side route/address/forwarding drift must stop before the sole group
# rekey stimulus reaches hostapd. The mock state is restored before rollback.
REKEY_CLI_LINES_BEFORE="$(wc -l <"$FAKE_CLI_LOG")"
if FAKE_NETWORK_MUTATED=1 "$HELPER" --rekey --state-dir "$STATE_DIR" \
        >"$TMP_ROOT/rekey-drift.out" 2>"$TMP_ROOT/rekey-drift.err"; then
    fail 'group rekey accepted a changed host-network signature'
fi
grep -Fq 'host network invariants changed before bounded group-rekey' \
    "$TMP_ROOT/rekey-drift.err" ||
    fail 'group rekey drift retained no categorical diagnostic'
[ "$(wc -l <"$FAKE_CLI_LOG")" = "$REKEY_CLI_LINES_BEFORE" ] ||
    fail 'group rekey drift reached the hostapd CLI'

# A signature change produced after hostapd accepts REKEY_GTK is also a
# failure. It must not create the rekey witness used by the runtime runner.
REKEY_CLI_LINES_BEFORE="$(wc -l <"$FAKE_CLI_LOG")"
if FAKE_MUTATE_DURING_REKEY=1 "$HELPER" --rekey --state-dir "$STATE_DIR" \
        >"$TMP_ROOT/rekey-post-drift.out" 2>"$TMP_ROOT/rekey-post-drift.err"; then
    fail 'group rekey accepted a post-command host-network signature change'
fi
grep -Fq 'host network invariants changed during bounded group-rekey' \
    "$TMP_ROOT/rekey-post-drift.err" ||
    fail 'post-command rekey drift retained no categorical diagnostic'
[ "$(wc -l <"$FAKE_CLI_LOG")" -eq $((REKEY_CLI_LINES_BEFORE + 1)) ] ||
    fail 'post-command drift did not reach exactly one hostapd CLI request'
[ ! -e "$STATE_DIR/rekey.status" ] ||
    fail 'post-command rekey drift wrote a success witness'
printf 'stable\n' >"$FAKE_NETWORK_STATE"

"$HELPER" --rekey --state-dir "$STATE_DIR" \
    >"$TMP_ROOT/rekey.out" 2>"$TMP_ROOT/rekey.err" ||
    fail 'synthetic bounded group rekey failed'
grep -Fxq 'PMF_AP_REKEY=REQUESTED' "$TMP_ROOT/rekey.out" ||
    fail 'group rekey did not report categorical acknowledgement'
grep -Fxq 'rekey_requested=true' "$STATE_DIR/rekey.status" ||
    fail 'group rekey state witness is missing'
grep -Fxq -- '-p /run/hostapd -i wlp0s20f3 raw REKEY_GTK' "$FAKE_CLI_LOG" ||
    fail 'fake hostapd CLI did not receive the canonical group-rekey command'

"$HELPER" --rollback --state-dir "$STATE_DIR" \
    >"$TMP_ROOT/rollback.out" 2>"$TMP_ROOT/rollback.err" ||
    fail 'synthetic optional-PMF rollback failed'
grep -Fxq 'PMF_AP_ROLLBACK=OPTIONAL_RESTORED' "$TMP_ROOT/rollback.out" ||
    fail 'rollback did not report optional-PMF restoration'
grep -Fxq 'rollback_verified=true' "$STATE_DIR/rollback.status" ||
    fail 'rollback verification witness is missing'
[ -r "$RUN_DIR/hostapd-5g.pid" ] || fail 'optional hostapd did not return'
[ ! -e "$RUN_DIR/hostapd-5g-pmf-required.pid" ] ||
    fail 'required hostapd pid survived rollback'
[ ! -e "$CONTROL_DIR/active.state" ] || fail 'rollback left the active marker behind'

FAIL_STATE_DIR="$(mktemp -d "$STATE_PREFIX"XXXXXX)"
if FAKE_FAIL_REQUIRED=1 "$HELPER" --activate --state-dir "$FAIL_STATE_DIR" \
    --lease-seconds 60 >"$TMP_ROOT/failed-activate.out" \
    2>"$TMP_ROOT/failed-activate.err"; then
    fail 'activation accepted a synthetic required-hostapd start failure'
fi
grep -Fq 'optional rollback verified' "$TMP_ROOT/failed-activate.err" ||
    fail 'failed activation did not report verified immediate optional rollback'
[ -r "$RUN_DIR/hostapd-5g.pid" ] ||
    fail 'failed activation did not restore optional hostapd'
[ ! -e "$RUN_DIR/hostapd-5g-pmf-required.pid" ] ||
    fail 'failed activation left required hostapd active'
[ ! -e "$CONTROL_DIR/active.state" ] ||
    fail 'failed activation left a live switchover marker'

# A background PID is not proof that the detached rollback owner successfully
# entered its state/marker-bound watchdog path.  The helper must reject a
# fixture watchdog that exits before its one-shot acknowledgement without
# stopping or replacing the optional-PMF process.
WATCHDOG_FAIL_STATE_DIR="$(mktemp -d "$STATE_PREFIX"XXXXXX)"
OPTIONAL_PID_BEFORE="$(tr -d '[:space:]' <"$RUN_DIR/hostapd-5g.pid")"
if AIAM_PMF_AP_TEST_WATCHDOG_EXIT_BEFORE_READY=1 "$HELPER" --activate \
    --state-dir "$WATCHDOG_FAIL_STATE_DIR" --lease-seconds 60 \
    >"$TMP_ROOT/watchdog-failed-activate.out" \
    2>"$TMP_ROOT/watchdog-failed-activate.err"; then
    fail 'activation accepted a watchdog that exited before readiness acknowledgement'
fi
grep -Fq 'rollback watchdog setup failed' \
    "$TMP_ROOT/watchdog-failed-activate.err" ||
    fail 'pre-ack watchdog exit did not retain its categorical diagnostic'
OPTIONAL_PID_AFTER="$(tr -d '[:space:]' <"$RUN_DIR/hostapd-5g.pid")"
[ "$OPTIONAL_PID_BEFORE" = "$OPTIONAL_PID_AFTER" ] ||
    fail 'pre-ack watchdog exit stopped or replaced optional hostapd'
/bin/kill -0 "$OPTIONAL_PID_AFTER" >/dev/null 2>&1 ||
    fail 'optional hostapd was not alive after pre-ack watchdog exit'
[ ! -e "$RUN_DIR/hostapd-5g-pmf-required.pid" ] ||
    fail 'pre-ack watchdog exit started required hostapd'
[ ! -e "$CONTROL_DIR/active.state" ] ||
    fail 'pre-ack watchdog exit left a live switchover marker'
[ ! -e "$WATCHDOG_FAIL_STATE_DIR/watchdog.pid" ] ||
    fail 'pre-ack watchdog exit persisted a false watchdog PID'

# A mismatched staged wireless identity is a read-only prerequisite failure;
# the helper must not start a switchover merely because the other PMF fields
# are valid.
write_config "$REQUIRED" 2 'WPA-PSK-SHA256' fixture-network-mismatch
if "$HELPER" --preflight >"$TMP_ROOT/mismatch-preflight.out" \
    2>"$TMP_ROOT/mismatch-preflight.err"; then
    fail 'preflight accepted mismatched optional/required identities'
fi
grep -Fq 'ssid-pair-mismatch' "$TMP_ROOT/mismatch-preflight.err" ||
    fail 'mismatched preflight did not retain its safe categorical diagnosis'
[ -r "$RUN_DIR/hostapd-5g.pid" ] ||
    fail 'mismatched preflight disturbed the optional hostapd process'

printf 'PASS: PMF-required AP helper local rollback fixture\n'
