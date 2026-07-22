#!/usr/bin/env bash
# Local fixture for the PMF-required hostapd helper.
#
# Every command path is replaced under explicit test mode.  This never reads
# the live AP configuration, starts a real hostapd, or changes host networking.
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
HELPER="$ROOT/scripts/tahoe_pmf_required_ap_switchover.sh"
TMP_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/aiam-pmf-ap-helper-fixture.XXXXXX")"
WATCHDOG_BACKUP_PIDS="$TMP_ROOT/original-watchdog-pids"
FOREIGN_WATCHDOG_PIDS="$TMP_ROOT/foreign-watchdog-pids"
umask 077

cleanup() {
    local pidfile pid
    set +e
    for pidfile in "$TMP_ROOT/run/hostapd-5g.pid" \
                   "$TMP_ROOT/run/hostapd-5g-pmf-required.pid" \
                   "$WATCHDOG_BACKUP_PIDS" \
                   "$FOREIGN_WATCHDOG_PIDS" \
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
FAKE_OPTIONAL_PID="$RUN_DIR/hostapd-5g.pid"
FAKE_REQUIRED_PID="$RUN_DIR/hostapd-5g-pmf-required.pid"
FAKE_HOSTAPD="$TMP_ROOT/hostapd"
FAKE_CLI="$TMP_ROOT/hostapd_cli"
FAKE_IW="$TMP_ROOT/iw"
FAKE_IP="$TMP_ROOT/ip"
FAKE_SYSCTL="$TMP_ROOT/sysctl"
FAKE_SUDO="$TMP_ROOT/sudo"
FAKE_HOSTAPD_LOG="$TMP_ROOT/fake-hostapd.log"
FAKE_CLI_LOG="$TMP_ROOT/fake-cli.log"
FAKE_NETWORK_STATE="$TMP_ROOT/fake-network-state"
FAKE_ROUTE_CALL_COUNT="$TMP_ROOT/fake-route-call-count"
FAKE_IW_CALL_COUNT="$TMP_ROOT/fake-iw-call-count"
FAKE_REQUIRED_CONFIG="$LAB_ROOT/hostapd-5g-wpa2-pmf.conf"
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
    'if [ "${FAKE_MUTATE_NETWORK_ON_REQUIRED_START:-0}" = 1 ] && [[ "$config" = *hostapd-5g-wpa2-pmf.conf ]]; then printf "drift\n" >"$FAKE_NETWORK_STATE"; fi' \
    'if [ "${FAKE_MUTATE_REQUIRED_CONFIG_ON_START:-0}" = 1 ] && [[ "$config" = *hostapd-5g-wpa2-pmf.conf ]]; then printf "wpa_group_rekey=1\n" >>"$FAKE_REQUIRED_CONFIG"; fi' \
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
    'if [ "${FAKE_TERMINATE_REQUIRED_DURING_REKEY:-0}" = 1 ] && [ -r "$FAKE_REQUIRED_PID" ]; then' \
    '    pid="$(tr -d "[:space:]" <"$FAKE_REQUIRED_PID")"' \
    '    case "$pid" in ""|*[!0-9]*) exit 65;; esac' \
    '    /bin/kill -KILL "$pid" >/dev/null 2>&1 || true' \
    '    /bin/rm -f -- "$FAKE_REQUIRED_PID"' \
    'fi' \
    'if [ "${FAKE_MUTATE_DURING_REKEY:-0}" = 1 ]; then printf "drift\n" >"$FAKE_NETWORK_STATE"; fi' \
    'printf "OK\n"' \
    >"$FAKE_CLI"

printf '%s\n' \
    '#!/usr/bin/env bash' \
    'set -euo pipefail' \
    'calls=0' \
    'if [ -r "$FAKE_IW_CALL_COUNT" ]; then calls="$(cat "$FAKE_IW_CALL_COUNT")"; fi' \
    'case "$calls" in ""|*[!0-9]*) exit 65;; esac' \
    'calls=$((calls + 1))' \
    'printf "%s\n" "$calls" >"$FAKE_IW_CALL_COUNT"' \
    'if [ "${FAKE_MUTATE_NETWORK_ON_IW_CALL:-}" = "$calls" ]; then printf "drift\n" >"$FAKE_NETWORK_STATE"; fi' \
    'if [ "${FAKE_TERMINATE_REQUIRED_ON_IW:-0}" = 1 ] && [ -r "$FAKE_REQUIRED_PID" ]; then' \
    '    pid="$(tr -d "[:space:]" <"$FAKE_REQUIRED_PID")"' \
    '    case "$pid" in ""|*[!0-9]*) exit 65;; esac' \
    '    /bin/kill -KILL "$pid" >/dev/null 2>&1 || true' \
    '    /bin/rm -f -- "$FAKE_REQUIRED_PID"' \
    'fi' \
    'if { [ "${FAKE_TERMINATE_OPTIONAL_ON_IW:-0}" = 1 ] || [ "${FAKE_TERMINATE_OPTIONAL_ON_IW_CALL:-}" = "$calls" ]; } && [ -r "$FAKE_OPTIONAL_PID" ]; then' \
    '    pid="$(tr -d "[:space:]" <"$FAKE_OPTIONAL_PID")"' \
    '    case "$pid" in ""|*[!0-9]*) exit 65;; esac' \
    '    /bin/kill -KILL "$pid" >/dev/null 2>&1 || true' \
    '    /bin/rm -f -- "$FAKE_OPTIONAL_PID"' \
    'fi' \
    'printf "Interface wlp0s20f3\n"' \
    'printf "\ttype AP\n"' \
    'printf "\tchannel 153 (5775 MHz), width: 80 MHz, center1: 5775 MHz\n"' \
    >"$FAKE_IW"

printf '%s\n' \
    '#!/usr/bin/env bash' \
    'set -euo pipefail' \
    'case "$*" in' \
    '  "-4 route show default")' \
    '      calls=0' \
    '      if [ -r "$FAKE_ROUTE_CALL_COUNT" ]; then calls="$(cat "$FAKE_ROUTE_CALL_COUNT")"; fi' \
    '      case "$calls" in ""|*[!0-9]*) exit 65;; esac' \
    '      calls=$((calls + 1))' \
    '      printf "%s\n" "$calls" >"$FAKE_ROUTE_CALL_COUNT"' \
    '      if [ "${FAKE_TERMINATE_WATCHDOG_ON_ROUTE_CALL:-}" = "$calls" ] && [ -r "${FAKE_WATCHDOG_PID_FILE:-}" ]; then pid="$(tr -d "[:space:]" <"$FAKE_WATCHDOG_PID_FILE")"; case "$pid" in ""|*[!0-9]*) exit 65;; esac; /bin/kill -KILL "$pid" >/dev/null 2>&1 || true; /bin/rm -f -- "$FAKE_WATCHDOG_PID_FILE"; fi' \
    '      if [ "${FAKE_TERMINATE_REQUIRED_ON_ROUTE_CALL:-}" = "$calls" ] && [ -r "$FAKE_REQUIRED_PID" ]; then pid="$(tr -d "[:space:]" <"$FAKE_REQUIRED_PID")"; case "$pid" in ""|*[!0-9]*) exit 65;; esac; /bin/kill -KILL "$pid" >/dev/null 2>&1 || true; /bin/rm -f -- "$FAKE_REQUIRED_PID"; fi' \
    '      if [ "${FAKE_MUTATE_REQUIRED_CONFIG_ON_ROUTE_CALL:-}" = "$calls" ]; then printf "wpa_group_rekey=1\n" >>"$FAKE_REQUIRED_CONFIG"; fi' \
    '      state="$(cat "$FAKE_NETWORK_STATE" 2>/dev/null || true)"' \
    '      if [ "${FAKE_NETWORK_MUTATED:-0}" = 1 ] || [ "$state" = drift ] || [ "${FAKE_DRIFT_ON_ROUTE_CALL:-}" = "$calls" ]; then printf "default fixture-route-mutated\n"; else printf "default fixture-route\n"; fi ;;' \
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
export FAKE_HOSTAPD_LOG FAKE_CLI_LOG FAKE_NETWORK_STATE FAKE_ROUTE_CALL_COUNT FAKE_IW_CALL_COUNT \
    FAKE_REQUIRED_CONFIG FAKE_OPTIONAL_PID FAKE_REQUIRED_PID
: >"$FAKE_CLI_LOG"
printf 'stable\n' >"$FAKE_NETWORK_STATE"
printf '0\n' >"$FAKE_ROUTE_CALL_COUNT"
printf '0\n' >"$FAKE_IW_CALL_COUNT"

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
grep -Eq '^config_pair_signature_before=[0-9a-f]{64}$' "$STATE_DIR/state.txt" ||
    fail 'required AP state lacks its configuration-pair baseline'
[ -r "$STATE_DIR/watchdog.pid" ] || fail 'activation did not retain a watchdog'
[ -r "$RUN_DIR/hostapd-5g-pmf-required.pid" ] ||
    fail 'required hostapd pid was not created'
[ ! -e "$RUN_DIR/hostapd-5g.pid" ] || fail 'optional hostapd pid survived activation'

# A file-pair drift while required PMF is active must block the later rekey
# micro-stimulus before it reaches hostapd.  Reset the generated staged file
# before the remaining baseline rekey/rollback cases.
REKEY_CLI_LINES_BEFORE="$(wc -l <"$FAKE_CLI_LOG")"
printf 'wpa_group_rekey=1\n' >>"$REQUIRED"
if "$HELPER" --rekey --state-dir "$STATE_DIR" \
        >"$TMP_ROOT/rekey-config-drift.out" \
        2>"$TMP_ROOT/rekey-config-drift.err"; then
    fail 'group rekey accepted a changed staged configuration pair'
fi
grep -Fq 'staged PMF configuration pair changed before bounded group-rekey' \
    "$TMP_ROOT/rekey-config-drift.err" ||
    fail 'group rekey config drift retained no categorical diagnostic'
[ "$(wc -l <"$FAKE_CLI_LOG")" = "$REKEY_CLI_LINES_BEFORE" ] ||
    fail 'group rekey config drift reached the hostapd CLI'
write_config "$REQUIRED" 2 'WPA-PSK-SHA256' fixture-network

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

# An acknowledged raw command that later becomes inconclusive still consumed
# the protocol's only permitted stimulus.  A second helper invocation must
# not turn that local failure into a second fake control request.
REKEY_CLI_LINES_BEFORE="$(wc -l <"$FAKE_CLI_LOG")"
if "$HELPER" --rekey --state-dir "$STATE_DIR" >"$TMP_ROOT/rekey-post-drift-retry.out" 2>"$TMP_ROOT/rekey-post-drift-retry.err"; then
    fail 'group rekey accepted a retry after an acknowledged inconclusive request'
fi
grep -Fq 'bounded group-rekey request was already recorded for this PMF-required transaction' "$TMP_ROOT/rekey-post-drift-retry.err" ||
    fail 'post-command rekey retry retained no categorical diagnostic'
[ "$(wc -l <"$FAKE_CLI_LOG")" = "$REKEY_CLI_LINES_BEFORE" ] ||
    fail 'post-command rekey retry reached the hostapd CLI'
[ ! -e "$STATE_DIR/rekey.status" ] ||
    fail 'post-command rekey retry wrote a success witness'
grep -Fxq 'rekey_attempted=true' "$STATE_DIR/rekey.requested" ||
    fail 'post-command rekey attempt receipt is missing'
"$HELPER" --rollback --state-dir "$STATE_DIR" >"$TMP_ROOT/rekey-post-drift-rollback.out" 2>"$TMP_ROOT/rekey-post-drift-rollback.err" ||
    fail 'post-command rekey drift did not restore optional PMF'
[ -r "$RUN_DIR/hostapd-5g.pid" ] ||
    fail 'post-command rekey drift did not restore optional hostapd'

# Re-arm a fresh generated transaction: the preceding state carries a raw
# command attempt and must never be reused for another rekey stimulus.
STATE_DIR="$(mktemp -d "$STATE_PREFIX"XXXXXX)"
"$HELPER" --activate --state-dir "$STATE_DIR" --lease-seconds 60 >"$TMP_ROOT/rekey-pre-command-activate.out" 2>"$TMP_ROOT/rekey-pre-command-activate.err" ||
    fail 'pre-command rekey fixture could not activate required PMF'

# A required-state receipt alone does not prove its independent recovery owner
# survived until the raw rekey edge.  Fake ip kills only the generated
# watchdog on the first rekey route probe while preserving the fake baseline.
REKEY_CLI_LINES_BEFORE="$(wc -l <"$FAKE_CLI_LOG")"
ROUTE_CALLS_BEFORE="$(tr -d '[:space:]' <"$FAKE_ROUTE_CALL_COUNT")"
case "$ROUTE_CALLS_BEFORE" in ''|*[!0-9]*) fail 'fake route call counter is invalid';; esac
REKEY_WATCHDOG_ROUTE_CALL=$((ROUTE_CALLS_BEFORE + 1))
if FAKE_WATCHDOG_PID_FILE="$STATE_DIR/watchdog.pid" FAKE_TERMINATE_WATCHDOG_ON_ROUTE_CALL="$REKEY_WATCHDOG_ROUTE_CALL" "$HELPER" --rekey --state-dir "$STATE_DIR" >"$TMP_ROOT/rekey-watchdog-death.out" 2>"$TMP_ROOT/rekey-watchdog-death.err"; then
    fail 'group rekey accepted a watchdog that died before the raw command edge'
fi
grep -Fq 'rollback watchdog is not exact before bounded group-rekey' "$TMP_ROOT/rekey-watchdog-death.err" ||
    fail 'rekey watchdog death did not retain its categorical diagnostic'
[ "$(wc -l <"$FAKE_CLI_LOG")" = "$REKEY_CLI_LINES_BEFORE" ] ||
    fail 'rekey watchdog death reached the hostapd CLI'
[ ! -e "$STATE_DIR/rekey.requested" ] ||
    fail 'rekey watchdog death consumed the one-shot request receipt'
[ ! -e "$STATE_DIR/rekey.status" ] ||
    fail 'rekey watchdog death wrote a success witness'
[ -r "$RUN_DIR/hostapd-5g-pmf-required.pid" ] ||
    fail 'rekey watchdog death disturbed required hostapd before rollback'
[ -e "$CONTROL_DIR/active.state" ] ||
    fail 'rekey watchdog death cleared the required-state marker'
[ ! -e "$STATE_DIR/watchdog.pid" ] ||
    fail 'rekey watchdog death retained a stale watchdog receipt'
"$HELPER" --rollback --state-dir "$STATE_DIR" \
    >"$TMP_ROOT/rekey-watchdog-death-rollback.out" \
    2>"$TMP_ROOT/rekey-watchdog-death-rollback.err" ||
    fail 'rekey watchdog death did not permit explicit optional rollback'
grep -Fxq 'PMF_AP_ROLLBACK=OPTIONAL_RESTORED' \
    "$TMP_ROOT/rekey-watchdog-death-rollback.out" ||
    fail 'rekey watchdog death rollback did not report optional restoration'
[ -r "$RUN_DIR/hostapd-5g.pid" ] ||
    fail 'rekey watchdog death rollback did not restore optional hostapd'
[ ! -e "$CONTROL_DIR/active.state" ] ||
    fail 'rekey watchdog death rollback left the active marker'

# Re-arm a fresh generated transaction after the owner-loss rejection so the
# following required-process command-edge case has its own one-shot receipt.
STATE_DIR="$(mktemp -d "$STATE_PREFIX"XXXXXX)"
"$HELPER" --activate --state-dir "$STATE_DIR" --lease-seconds 60 \
    >"$TMP_ROOT/rekey-pre-command-after-watchdog-activate.out" \
    2>"$TMP_ROOT/rekey-pre-command-after-watchdog-activate.err" ||
    fail 'post-watchdog rekey fixture could not activate required PMF'

# The required process may disappear after its first exact-PID observation
# but before the raw control command.  The fake AP-shape response remains
# pinned, so a process fence at the actual command edge is the only way to
# reject this stale ownership snapshot before the fake CLI is reached.
REKEY_CLI_LINES_BEFORE="$(wc -l <"$FAKE_CLI_LOG")"
if FAKE_TERMINATE_REQUIRED_ON_IW=1 "$HELPER" --rekey --state-dir "$STATE_DIR" \
        >"$TMP_ROOT/rekey-pre-command-death.out" \
        2>"$TMP_ROOT/rekey-pre-command-death.err"; then
    fail 'group rekey accepted a required hostapd that died before the command edge'
fi
grep -Fq 'required-PMF hostapd process is not exact before bounded group-rekey' \
    "$TMP_ROOT/rekey-pre-command-death.err" ||
    fail 'pre-command required-child death retained no categorical diagnostic'
[ "$(wc -l <"$FAKE_CLI_LOG")" = "$REKEY_CLI_LINES_BEFORE" ] ||
    fail 'pre-command required-child death reached the hostapd CLI'
[ ! -e "$STATE_DIR/rekey.status" ] ||
    fail 'pre-command required-child death wrote a success witness'
"$HELPER" --rollback --state-dir "$STATE_DIR" \
    >"$TMP_ROOT/rekey-pre-command-death-rollback.out" \
    2>"$TMP_ROOT/rekey-pre-command-death-rollback.err" ||
    fail 'pre-command required-child death did not restore optional PMF'
[ -r "$RUN_DIR/hostapd-5g.pid" ] ||
    fail 'pre-command required-child death did not restore optional hostapd'

# Re-arm an isolated generated transaction.  This injection lets the fake
# control client acknowledge exactly one canonical command and then removes
# its required child.  The helper must withhold the rekey witness until its
# post-ack exact-process fence succeeds.
STATE_DIR="$(mktemp -d "$STATE_PREFIX"XXXXXX)"
"$HELPER" --activate --state-dir "$STATE_DIR" --lease-seconds 60 \
    >"$TMP_ROOT/rekey-post-ack-activate.out" \
    2>"$TMP_ROOT/rekey-post-ack-activate.err" ||
    fail 'post-ack rekey fixture could not activate required PMF'
REKEY_CLI_LINES_BEFORE="$(wc -l <"$FAKE_CLI_LOG")"
if FAKE_TERMINATE_REQUIRED_DURING_REKEY=1 "$HELPER" --rekey --state-dir "$STATE_DIR" \
        >"$TMP_ROOT/rekey-post-ack-death.out" \
        2>"$TMP_ROOT/rekey-post-ack-death.err"; then
    fail 'group rekey accepted a required hostapd that died after the command acknowledgement'
fi
grep -Fq 'required-PMF hostapd process is not exact after bounded group-rekey' \
    "$TMP_ROOT/rekey-post-ack-death.err" ||
    fail 'post-ack required-child death retained no categorical diagnostic'
[ "$(wc -l <"$FAKE_CLI_LOG")" -eq $((REKEY_CLI_LINES_BEFORE + 1)) ] ||
    fail 'post-ack required-child death did not reach exactly one hostapd CLI request'
[ ! -e "$STATE_DIR/rekey.status" ] ||
    fail 'post-ack required-child death wrote a success witness'
"$HELPER" --rollback --state-dir "$STATE_DIR" \
    >"$TMP_ROOT/rekey-post-ack-death-rollback.out" \
    2>"$TMP_ROOT/rekey-post-ack-death-rollback.err" ||
    fail 'post-ack required-child death did not restore optional PMF'
[ -r "$RUN_DIR/hostapd-5g.pid" ] ||
    fail 'post-ack required-child death did not restore optional hostapd'

# Re-arm a generated transaction to prove that the post-ack process proof must
# remain current through the final host-network comparison and success witness.
STATE_DIR="$(mktemp -d "$STATE_PREFIX"XXXXXX)"
"$HELPER" --activate --state-dir "$STATE_DIR" --lease-seconds 60 \
    >"$TMP_ROOT/rekey-final-required-activate.out" \
    2>"$TMP_ROOT/rekey-final-required-activate.err" ||
    fail 'final-required rekey fixture could not activate required PMF'
REKEY_CLI_LINES_BEFORE="$(wc -l <"$FAKE_CLI_LOG")"
ROUTE_CALLS_BEFORE="$(tr -d '[:space:]' <"$FAKE_ROUTE_CALL_COUNT")"
case "$ROUTE_CALLS_BEFORE" in ''|*[!0-9]*) fail 'fake route call counter is invalid';; esac
FINAL_REKEY_REQUIRED_ROUTE_CALL=$((ROUTE_CALLS_BEFORE + 2))
if FAKE_TERMINATE_REQUIRED_ON_ROUTE_CALL="$FINAL_REKEY_REQUIRED_ROUTE_CALL" "$HELPER" --rekey --state-dir "$STATE_DIR" >"$TMP_ROOT/rekey-final-required-death.out" 2>"$TMP_ROOT/rekey-final-required-death.err"; then
    fail 'group rekey accepted a required hostapd that died before final success publication'
fi
grep -Fq 'required-PMF hostapd process is not exact before rekey success publication' "$TMP_ROOT/rekey-final-required-death.err" ||
    fail 'final rekey required-child death did not retain its categorical diagnostic'
[ "$(wc -l <"$FAKE_CLI_LOG")" -eq $((REKEY_CLI_LINES_BEFORE + 1)) ] ||
    fail 'final rekey required-child death did not reach exactly one hostapd CLI request'
[ ! -e "$STATE_DIR/rekey.status" ] ||
    fail 'final rekey required-child death wrote a success witness'
grep -Fxq 'rekey_attempted=true' "$STATE_DIR/rekey.requested" ||
    fail 'final rekey required-child death did not retain its one-shot receipt'
[ ! -e "$RUN_DIR/hostapd-5g-pmf-required.pid" ] ||
    fail 'final rekey required-child death left required hostapd active'
[ -e "$CONTROL_DIR/active.state" ] ||
    fail 'final rekey required-child death cleared the required-state marker'
[ -r "$STATE_DIR/watchdog.pid" ] ||
    fail 'final rekey required-child death did not retain its watchdog receipt'
FINAL_REKEY_REQUIRED_WATCHDOG_PID="$(tr -d '[:space:]' <"$STATE_DIR/watchdog.pid")"
/bin/kill -0 "$FINAL_REKEY_REQUIRED_WATCHDOG_PID" >/dev/null 2>&1 ||
    fail 'final rekey required-child death did not retain a live watchdog'
"$HELPER" --rollback --state-dir "$STATE_DIR" \
    >"$TMP_ROOT/rekey-final-required-rollback.out" \
    2>"$TMP_ROOT/rekey-final-required-rollback.err" ||
    fail 'final rekey required-child death did not permit explicit optional rollback'
grep -Fxq 'PMF_AP_ROLLBACK=OPTIONAL_RESTORED' \
    "$TMP_ROOT/rekey-final-required-rollback.out" ||
    fail 'final rekey required-child death rollback did not report optional restoration'
[ -r "$RUN_DIR/hostapd-5g.pid" ] ||
    fail 'final rekey required-child death rollback did not restore optional hostapd'
[ ! -e "$CONTROL_DIR/active.state" ] ||
    fail 'final rekey required-child death rollback left the active marker'
if /bin/kill -0 "$FINAL_REKEY_REQUIRED_WATCHDOG_PID" >/dev/null 2>&1; then
    fail 'final rekey required-child death rollback left the watchdog live'
fi

# The command-edge owner proof also must survive the raw command and final
# network read.  Fake ip removes only this generated watchdog at rekey's
# second route probe, immediately before success publication in the old path.
STATE_DIR="$(mktemp -d "$STATE_PREFIX"XXXXXX)"
"$HELPER" --activate --state-dir "$STATE_DIR" --lease-seconds 60 \
    >"$TMP_ROOT/rekey-final-watchdog-activate.out" \
    2>"$TMP_ROOT/rekey-final-watchdog-activate.err" ||
    fail 'final-watchdog rekey fixture could not activate required PMF'
REKEY_CLI_LINES_BEFORE="$(wc -l <"$FAKE_CLI_LOG")"
ROUTE_CALLS_BEFORE="$(tr -d '[:space:]' <"$FAKE_ROUTE_CALL_COUNT")"
case "$ROUTE_CALLS_BEFORE" in ''|*[!0-9]*) fail 'fake route call counter is invalid';; esac
FINAL_REKEY_WATCHDOG_ROUTE_CALL=$((ROUTE_CALLS_BEFORE + 2))
if FAKE_WATCHDOG_PID_FILE="$STATE_DIR/watchdog.pid" FAKE_TERMINATE_WATCHDOG_ON_ROUTE_CALL="$FINAL_REKEY_WATCHDOG_ROUTE_CALL" "$HELPER" --rekey --state-dir "$STATE_DIR" >"$TMP_ROOT/rekey-final-watchdog-death.out" 2>"$TMP_ROOT/rekey-final-watchdog-death.err"; then
    fail 'group rekey accepted a watchdog that died before final success publication'
fi
grep -Fq 'rollback watchdog is not exact before rekey success publication' "$TMP_ROOT/rekey-final-watchdog-death.err" ||
    fail 'final rekey watchdog death did not retain its categorical diagnostic'
[ "$(wc -l <"$FAKE_CLI_LOG")" -eq $((REKEY_CLI_LINES_BEFORE + 1)) ] ||
    fail 'final rekey watchdog death did not reach exactly one hostapd CLI request'
[ ! -e "$STATE_DIR/rekey.status" ] ||
    fail 'final rekey watchdog death wrote a success witness'
grep -Fxq 'rekey_attempted=true' "$STATE_DIR/rekey.requested" ||
    fail 'final rekey watchdog death did not retain its one-shot receipt'
[ -r "$RUN_DIR/hostapd-5g-pmf-required.pid" ] ||
    fail 'final rekey watchdog death disturbed required hostapd before rollback'
[ -e "$CONTROL_DIR/active.state" ] ||
    fail 'final rekey watchdog death cleared the required-state marker'
[ ! -e "$STATE_DIR/watchdog.pid" ] ||
    fail 'final rekey watchdog death retained a stale watchdog receipt'
"$HELPER" --rollback --state-dir "$STATE_DIR" \
    >"$TMP_ROOT/rekey-final-watchdog-rollback.out" \
    2>"$TMP_ROOT/rekey-final-watchdog-rollback.err" ||
    fail 'final rekey watchdog death did not permit explicit optional rollback'
grep -Fxq 'PMF_AP_ROLLBACK=OPTIONAL_RESTORED' \
    "$TMP_ROOT/rekey-final-watchdog-rollback.out" ||
    fail 'final rekey watchdog death rollback did not report optional restoration'
[ -r "$RUN_DIR/hostapd-5g.pid" ] ||
    fail 'final rekey watchdog death rollback did not restore optional hostapd'
[ ! -e "$CONTROL_DIR/active.state" ] ||
    fail 'final rekey watchdog death rollback left the active marker'

# A config pair that changes during the raw command's post-ack probes cannot
# support a categorical rekey witness or an immediate optional restart.  Fake
# ip mutates only the generated required config on the second rekey route read.
STATE_DIR="$(mktemp -d "$STATE_PREFIX"XXXXXX)"
"$HELPER" --activate --state-dir "$STATE_DIR" --lease-seconds 60 \
    >"$TMP_ROOT/rekey-final-config-activate.out" \
    2>"$TMP_ROOT/rekey-final-config-activate.err" ||
    fail 'final-config rekey fixture could not activate required PMF'
REKEY_CLI_LINES_BEFORE="$(wc -l <"$FAKE_CLI_LOG")"
ROUTE_CALLS_BEFORE="$(tr -d '[:space:]' <"$FAKE_ROUTE_CALL_COUNT")"
case "$ROUTE_CALLS_BEFORE" in ''|*[!0-9]*) fail 'fake route call counter is invalid';; esac
FINAL_REKEY_CONFIG_ROUTE_CALL=$((ROUTE_CALLS_BEFORE + 2))
if FAKE_MUTATE_REQUIRED_CONFIG_ON_ROUTE_CALL="$FINAL_REKEY_CONFIG_ROUTE_CALL" "$HELPER" --rekey --state-dir "$STATE_DIR" >"$TMP_ROOT/rekey-final-config-drift.out" 2>"$TMP_ROOT/rekey-final-config-drift.err"; then
    fail 'group rekey accepted a configuration changed before final success publication'
fi
grep -Fq 'staged PMF configuration pair changed before rekey success publication' "$TMP_ROOT/rekey-final-config-drift.err" ||
    fail 'final rekey configuration drift did not retain its categorical diagnostic'
[ "$(wc -l <"$FAKE_CLI_LOG")" -eq $((REKEY_CLI_LINES_BEFORE + 1)) ] ||
    fail 'final rekey configuration drift did not reach exactly one hostapd CLI request'
[ ! -e "$STATE_DIR/rekey.status" ] ||
    fail 'final rekey configuration drift wrote a success witness'
grep -Fxq 'rekey_attempted=true' "$STATE_DIR/rekey.requested" ||
    fail 'final rekey configuration drift did not retain its one-shot receipt'
[ -r "$RUN_DIR/hostapd-5g-pmf-required.pid" ] ||
    fail 'final rekey configuration drift disturbed required hostapd before rollback'
[ -e "$CONTROL_DIR/active.state" ] ||
    fail 'final rekey configuration drift cleared the required-state marker'
if "$HELPER" --rollback --state-dir "$STATE_DIR" \
        >"$TMP_ROOT/rekey-final-config-rollback-drift.out" \
        2>"$TMP_ROOT/rekey-final-config-rollback-drift.err"; then
    fail 'final rekey configuration drift rollback accepted the changed pair'
fi
grep -Fq 'staged PMF configuration pair changed before optional-PMF restart' \
    "$TMP_ROOT/rekey-final-config-rollback-drift.err" ||
    fail 'final rekey configuration drift rollback did not retain its categorical diagnostic'
[ ! -e "$STATE_DIR/rollback.status" ] ||
    fail 'final rekey configuration drift rollback wrote a success witness'
[ -e "$CONTROL_DIR/active.state" ] ||
    fail 'final rekey configuration drift rollback cleared the active marker'
write_config "$REQUIRED" 2 'WPA-PSK-SHA256' fixture-network
"$HELPER" --rollback --state-dir "$STATE_DIR" \
    >"$TMP_ROOT/rekey-final-config-rollback.out" \
    2>"$TMP_ROOT/rekey-final-config-rollback.err" ||
    fail 'final rekey configuration restoration did not permit explicit rollback'
grep -Fxq 'PMF_AP_ROLLBACK=OPTIONAL_RESTORED' \
    "$TMP_ROOT/rekey-final-config-rollback.out" ||
    fail 'final rekey configuration restoration did not report optional restoration'
[ -r "$RUN_DIR/hostapd-5g.pid" ] ||
    fail 'final rekey configuration restoration did not restore optional hostapd'
[ ! -e "$CONTROL_DIR/active.state" ] ||
    fail 'final rekey configuration restoration left the active marker'

# Restore a fresh required transaction for the existing stable positive rekey
# and rollback checks below.
STATE_DIR="$(mktemp -d "$STATE_PREFIX"XXXXXX)"
"$HELPER" --activate --state-dir "$STATE_DIR" --lease-seconds 60 \
    >"$TMP_ROOT/rekey-stable-activate.out" \
    2>"$TMP_ROOT/rekey-stable-activate.err" ||
    fail 'stable rekey fixture could not activate required PMF'

"$HELPER" --rekey --state-dir "$STATE_DIR" \
    >"$TMP_ROOT/rekey.out" 2>"$TMP_ROOT/rekey.err" ||
    fail 'synthetic bounded group rekey failed'
grep -Fxq 'PMF_AP_REKEY=REQUESTED' "$TMP_ROOT/rekey.out" ||
    fail 'group rekey did not report categorical acknowledgement'
grep -Fxq 'rekey_requested=true' "$STATE_DIR/rekey.status" ||
    fail 'group rekey state witness is missing'
grep -Fxq -- '-p /run/hostapd -i wlp0s20f3 raw REKEY_GTK' "$FAKE_CLI_LOG" ||
    fail 'fake hostapd CLI did not receive the canonical group-rekey command'
grep -Fxq 'rekey_attempted=true' "$STATE_DIR/rekey.requested" ||
    fail 'stable group rekey attempt receipt is missing'

# A successful one-request transaction remains one-request only.  The second
# local helper call must not reach the fake raw control endpoint or overwrite
# the original success witness.
REKEY_CLI_LINES_BEFORE="$(wc -l <"$FAKE_CLI_LOG")"
if "$HELPER" --rekey --state-dir "$STATE_DIR" >"$TMP_ROOT/rekey-duplicate.out" 2>"$TMP_ROOT/rekey-duplicate.err"; then
    fail 'group rekey accepted a second request for one PMF-required transaction'
fi
grep -Fq 'bounded group-rekey request was already recorded for this PMF-required transaction' "$TMP_ROOT/rekey-duplicate.err" ||
    fail 'duplicate rekey retained no categorical diagnostic'
[ "$(wc -l <"$FAKE_CLI_LOG")" = "$REKEY_CLI_LINES_BEFORE" ] ||
    fail 'duplicate rekey reached the hostapd CLI'
grep -Fxq 'rekey_requested=true' "$STATE_DIR/rekey.status" ||
    fail 'duplicate rekey altered the original success witness'

# The optional child can disappear after the start helper observes its exact
# PID but before rollback publishes its witness.  Fake iw continues to report
# the pinned AP shape, so only a final optional-process attestation can keep
# this generated loss from releasing marker/watchdog ownership.
if FAKE_TERMINATE_OPTIONAL_ON_IW=1 "$HELPER" --rollback --state-dir "$STATE_DIR" \
        >"$TMP_ROOT/rollback-optional-death.out" \
        2>"$TMP_ROOT/rollback-optional-death.err"; then
    fail 'rollback accepted an optional hostapd that died before verification'
fi
grep -Fq 'optional-PMF hostapd process or AP shape is not exact before rollback verification' \
    "$TMP_ROOT/rollback-optional-death.err" ||
    fail 'optional-child rollback death retained no categorical diagnostic'
[ ! -e "$STATE_DIR/rollback.status" ] ||
    fail 'optional-child rollback death wrote a verified rollback witness'
[ -e "$CONTROL_DIR/active.state" ] ||
    fail 'optional-child rollback death cleared the active marker'
[ -r "$STATE_DIR/watchdog.pid" ] ||
    fail 'optional-child rollback death did not retain its watchdog receipt'
[ ! -e "$RUN_DIR/hostapd-5g.pid" ] ||
    fail 'optional-child rollback death left a stale optional pid receipt'

"$HELPER" --rollback --state-dir "$STATE_DIR" \
    >"$TMP_ROOT/rollback.out" 2>"$TMP_ROOT/rollback.err" ||
    fail 'stable optional-PMF rollback failed after optional-child death'
grep -Fxq 'PMF_AP_ROLLBACK=OPTIONAL_RESTORED' "$TMP_ROOT/rollback.out" ||
    fail 'rollback did not report optional-PMF restoration'
grep -Fxq 'rollback_verified=true' "$STATE_DIR/rollback.status" ||
    fail 'rollback verification witness is missing'
[ -r "$RUN_DIR/hostapd-5g.pid" ] || fail 'optional hostapd did not return'
[ ! -e "$RUN_DIR/hostapd-5g-pmf-required.pid" ] ||
    fail 'required hostapd pid survived rollback'
[ ! -e "$CONTROL_DIR/active.state" ] || fail 'rollback left the active marker behind'

# The staged pair must remain state-bound until the rollback receipt is
# committed, not merely until optional hostapd restarts.  Fake ip mutates only
# the generated required config during rollback's one host-network probe.
ROLLBACK_FINAL_CONFIG_STATE_DIR="$(mktemp -d "$STATE_PREFIX"XXXXXX)"
"$HELPER" --activate --state-dir "$ROLLBACK_FINAL_CONFIG_STATE_DIR" --lease-seconds 60 \
    >"$TMP_ROOT/rollback-final-config-activate.out" \
    2>"$TMP_ROOT/rollback-final-config-activate.err" ||
    fail 'rollback-final-config fixture could not activate required PMF'
ROUTE_CALLS_BEFORE="$(tr -d '[:space:]' <"$FAKE_ROUTE_CALL_COUNT")"
case "$ROUTE_CALLS_BEFORE" in ''|*[!0-9]*) fail 'fake route call counter is invalid';; esac
ROLLBACK_FINAL_CONFIG_ROUTE_CALL=$((ROUTE_CALLS_BEFORE + 1))
if FAKE_MUTATE_REQUIRED_CONFIG_ON_ROUTE_CALL="$ROLLBACK_FINAL_CONFIG_ROUTE_CALL" "$HELPER" --rollback --state-dir "$ROLLBACK_FINAL_CONFIG_STATE_DIR" >"$TMP_ROOT/rollback-final-config-drift.out" 2>"$TMP_ROOT/rollback-final-config-drift.err"; then
    fail 'rollback accepted a configuration changed before verification'
fi
grep -Fq 'staged PMF configuration pair changed before rollback verification' "$TMP_ROOT/rollback-final-config-drift.err" ||
    fail 'rollback final configuration drift did not retain its categorical diagnostic'
[ ! -e "$ROLLBACK_FINAL_CONFIG_STATE_DIR/rollback.status" ] ||
    fail 'rollback final configuration drift wrote a verification receipt'
[ -r "$RUN_DIR/hostapd-5g.pid" ] ||
    fail 'rollback final configuration drift did not restore optional hostapd'
ROLLBACK_FINAL_CONFIG_OPTIONAL_PID="$(tr -d '[:space:]' <"$RUN_DIR/hostapd-5g.pid")"
/bin/kill -0 "$ROLLBACK_FINAL_CONFIG_OPTIONAL_PID" >/dev/null 2>&1 ||
    fail 'rollback final configuration drift restored no live optional hostapd'
[ ! -e "$RUN_DIR/hostapd-5g-pmf-required.pid" ] ||
    fail 'rollback final configuration drift left required hostapd active'
[ -e "$CONTROL_DIR/active.state" ] ||
    fail 'rollback final configuration drift cleared the active marker'
[ -r "$ROLLBACK_FINAL_CONFIG_STATE_DIR/watchdog.pid" ] ||
    fail 'rollback final configuration drift did not retain its watchdog receipt'
ROLLBACK_FINAL_CONFIG_WATCHDOG_PID="$(tr -d '[:space:]' <"$ROLLBACK_FINAL_CONFIG_STATE_DIR/watchdog.pid")"
/bin/kill -0 "$ROLLBACK_FINAL_CONFIG_WATCHDOG_PID" >/dev/null 2>&1 ||
    fail 'rollback final configuration drift did not retain a live watchdog'
write_config "$REQUIRED" 2 'WPA-PSK-SHA256' fixture-network
"$HELPER" --rollback --state-dir "$ROLLBACK_FINAL_CONFIG_STATE_DIR" \
    >"$TMP_ROOT/rollback-final-config-recovery.out" \
    2>"$TMP_ROOT/rollback-final-config-recovery.err" ||
    fail 'rollback final configuration restoration did not permit cleanup'
grep -Fxq 'PMF_AP_ROLLBACK=OPTIONAL_RESTORED' \
    "$TMP_ROOT/rollback-final-config-recovery.out" ||
    fail 'rollback final configuration recovery did not report optional restoration'
grep -Fxq 'rollback_verified=true' "$ROLLBACK_FINAL_CONFIG_STATE_DIR/rollback.status" ||
    fail 'rollback final configuration recovery did not commit its witness'
[ ! -e "$CONTROL_DIR/active.state" ] ||
    fail 'rollback final configuration recovery left the active marker'
if /bin/kill -0 "$ROLLBACK_FINAL_CONFIG_WATCHDOG_PID" >/dev/null 2>&1; then
    fail 'rollback final configuration recovery left the watchdog live'
fi

# Host-network proof must remain current through final optional/AP attestation.
# Fake iw changes only its generated network source at that third rollback
# shape probe, after the old single network comparison has already passed.
ROLLBACK_FINAL_NETWORK_STATE_DIR="$(mktemp -d "$STATE_PREFIX"XXXXXX)"
"$HELPER" --activate --state-dir "$ROLLBACK_FINAL_NETWORK_STATE_DIR" --lease-seconds 60 \
    >"$TMP_ROOT/rollback-final-network-activate.out" \
    2>"$TMP_ROOT/rollback-final-network-activate.err" ||
    fail 'rollback-final-network fixture could not activate required PMF'
IW_CALLS_BEFORE="$(tr -d '[:space:]' <"$FAKE_IW_CALL_COUNT")"
case "$IW_CALLS_BEFORE" in ''|*[!0-9]*) fail 'fake iw call counter is invalid';; esac
ROLLBACK_FINAL_NETWORK_IW_CALL=$((IW_CALLS_BEFORE + 3))
if FAKE_MUTATE_NETWORK_ON_IW_CALL="$ROLLBACK_FINAL_NETWORK_IW_CALL" "$HELPER" --rollback --state-dir "$ROLLBACK_FINAL_NETWORK_STATE_DIR" >"$TMP_ROOT/rollback-final-network-drift.out" 2>"$TMP_ROOT/rollback-final-network-drift.err"; then
    fail 'rollback accepted a host-network drift before verification'
fi
grep -Fq 'host network invariants changed before rollback verification' "$TMP_ROOT/rollback-final-network-drift.err" ||
    fail 'rollback final network drift did not retain its categorical diagnostic'
[ ! -e "$ROLLBACK_FINAL_NETWORK_STATE_DIR/rollback.status" ] ||
    fail 'rollback final network drift wrote a verification receipt'
[ -r "$RUN_DIR/hostapd-5g.pid" ] ||
    fail 'rollback final network drift did not restore optional hostapd'
ROLLBACK_FINAL_NETWORK_OPTIONAL_PID="$(tr -d '[:space:]' <"$RUN_DIR/hostapd-5g.pid")"
/bin/kill -0 "$ROLLBACK_FINAL_NETWORK_OPTIONAL_PID" >/dev/null 2>&1 ||
    fail 'rollback final network drift restored no live optional hostapd'
[ ! -e "$RUN_DIR/hostapd-5g-pmf-required.pid" ] ||
    fail 'rollback final network drift left required hostapd active'
[ -e "$CONTROL_DIR/active.state" ] ||
    fail 'rollback final network drift cleared the active marker'
[ -r "$ROLLBACK_FINAL_NETWORK_STATE_DIR/watchdog.pid" ] ||
    fail 'rollback final network drift did not retain its watchdog receipt'
ROLLBACK_FINAL_NETWORK_WATCHDOG_PID="$(tr -d '[:space:]' <"$ROLLBACK_FINAL_NETWORK_STATE_DIR/watchdog.pid")"
/bin/kill -0 "$ROLLBACK_FINAL_NETWORK_WATCHDOG_PID" >/dev/null 2>&1 ||
    fail 'rollback final network drift did not retain a live watchdog'
printf 'stable\n' >"$FAKE_NETWORK_STATE"
"$HELPER" --rollback --state-dir "$ROLLBACK_FINAL_NETWORK_STATE_DIR" \
    >"$TMP_ROOT/rollback-final-network-recovery.out" \
    2>"$TMP_ROOT/rollback-final-network-recovery.err" ||
    fail 'rollback final network restoration did not permit cleanup'
grep -Fxq 'PMF_AP_ROLLBACK=OPTIONAL_RESTORED' \
    "$TMP_ROOT/rollback-final-network-recovery.out" ||
    fail 'rollback final network recovery did not report optional restoration'
grep -Fxq 'rollback_verified=true' "$ROLLBACK_FINAL_NETWORK_STATE_DIR/rollback.status" ||
    fail 'rollback final network recovery did not commit its witness'
[ ! -e "$CONTROL_DIR/active.state" ] ||
    fail 'rollback final network recovery left the active marker'
if /bin/kill -0 "$ROLLBACK_FINAL_NETWORK_WATCHDOG_PID" >/dev/null 2>&1; then
    fail 'rollback final network recovery left the watchdog live'
fi

# `rollback_verified=true` is a transaction-completion receipt, not a record
# that optional hostapd was merely restored.  Replace only this fixture's
# private watchdog receipt with an unrelated generated process: cancellation
# must fail without publishing success, and restoring the original receipt
# must still permit one normal cleanup.
WITNESS_ORDER_STATE_DIR="$(mktemp -d "$STATE_PREFIX"XXXXXX)"
"$HELPER" --activate --state-dir "$WITNESS_ORDER_STATE_DIR" --lease-seconds 60 \
    >"$TMP_ROOT/witness-order-activate.out" \
    2>"$TMP_ROOT/witness-order-activate.err" ||
    fail 'witness-order fixture could not activate required PMF'
WITNESS_ORDER_WATCHDOG_PID="$(tr -d '[:space:]' <"$WITNESS_ORDER_STATE_DIR/watchdog.pid")"
case "$WITNESS_ORDER_WATCHDOG_PID" in ''|*[!0-9]*) fail 'witness-order watchdog PID is invalid';; esac
/bin/kill -0 "$WITNESS_ORDER_WATCHDOG_PID" >/dev/null 2>&1 ||
    fail 'witness-order watchdog was not live before receipt substitution'
printf '%s\n' "$WITNESS_ORDER_WATCHDOG_PID" >"$WATCHDOG_BACKUP_PIDS"
/bin/sleep 300 &
FOREIGN_WATCHDOG_PID=$!
printf '%s\n' "$FOREIGN_WATCHDOG_PID" >"$FOREIGN_WATCHDOG_PIDS"
printf '%s\n' "$FOREIGN_WATCHDOG_PID" >"$WITNESS_ORDER_STATE_DIR/watchdog.pid"
chmod 600 "$WITNESS_ORDER_STATE_DIR/watchdog.pid"
if "$HELPER" --rollback --state-dir "$WITNESS_ORDER_STATE_DIR" \
        >"$TMP_ROOT/witness-order-rollback.out" \
        2>"$TMP_ROOT/witness-order-rollback.err"; then
    fail 'rollback accepted a foreign watchdog receipt'
fi
grep -Fq 'rollback could not safely cancel its watchdog' \
    "$TMP_ROOT/witness-order-rollback.err" ||
    fail 'foreign watchdog receipt retained no categorical diagnostic'
[ ! -e "$WITNESS_ORDER_STATE_DIR/rollback.status" ] ||
    fail 'rollback verification was published before watchdog ownership released'
[ -e "$CONTROL_DIR/active.state" ] ||
    fail 'foreign watchdog receipt cleared the active marker'
/bin/kill -0 "$WITNESS_ORDER_WATCHDOG_PID" >/dev/null 2>&1 ||
    fail 'foreign watchdog receipt stopped the original watchdog'
printf '%s\n' "$WITNESS_ORDER_WATCHDOG_PID" >"$WITNESS_ORDER_STATE_DIR/watchdog.pid"
chmod 600 "$WITNESS_ORDER_STATE_DIR/watchdog.pid"
"$HELPER" --rollback --state-dir "$WITNESS_ORDER_STATE_DIR" \
    >"$TMP_ROOT/witness-order-recovery.out" \
    2>"$TMP_ROOT/witness-order-recovery.err" ||
    fail 'witness-order fixture could not complete a normal rollback'
grep -Fxq 'rollback_verified=true' "$WITNESS_ORDER_STATE_DIR/rollback.status" ||
    fail 'witness-order fixture did not commit the final rollback receipt'
[ ! -e "$CONTROL_DIR/active.state" ] ||
    fail 'witness-order fixture left the active marker behind'
if /bin/kill -0 "$WITNESS_ORDER_WATCHDOG_PID" >/dev/null 2>&1; then
    fail 'witness-order fixture left the original watchdog live'
fi
/bin/rm -f -- "$WATCHDOG_BACKUP_PIDS"
/bin/kill "$FOREIGN_WATCHDOG_PID" >/dev/null 2>&1 || true
wait "$FOREIGN_WATCHDOG_PID" 2>/dev/null || true
/bin/rm -f -- "$FOREIGN_WATCHDOG_PIDS"

# The transaction state directory itself is rollback authority and must not be
# writable by another local principal.  A generated mode-0777 directory must
# be rejected before the helper writes state, starts a watchdog, or touches
# optional hostapd.
UNSAFE_STATE_DIR="$(mktemp -d "$STATE_PREFIX"XXXXXX)"
chmod 777 "$UNSAFE_STATE_DIR"
OPTIONAL_PID_BEFORE="$(tr -d '[:space:]' <"$RUN_DIR/hostapd-5g.pid")"
if "$HELPER" --activate --state-dir "$UNSAFE_STATE_DIR" --lease-seconds 60 \
    >"$TMP_ROOT/unsafe-state-activate.out" \
    2>"$TMP_ROOT/unsafe-state-activate.err"; then
    fail 'activation accepted an other-writable rollback state directory'
fi
grep -Fq 'state directory permissions are not restricted' \
    "$TMP_ROOT/unsafe-state-activate.err" ||
    fail 'unsafe state directory did not retain its categorical diagnostic'
OPTIONAL_PID_AFTER="$(tr -d '[:space:]' <"$RUN_DIR/hostapd-5g.pid")"
[ "$OPTIONAL_PID_BEFORE" = "$OPTIONAL_PID_AFTER" ] ||
    fail 'unsafe state directory stopped or replaced optional hostapd'
/bin/kill -0 "$OPTIONAL_PID_AFTER" >/dev/null 2>&1 ||
    fail 'optional hostapd was not alive after unsafe-state rejection'
[ ! -e "$RUN_DIR/hostapd-5g-pmf-required.pid" ] ||
    fail 'unsafe state directory started required hostapd'
[ ! -e "$CONTROL_DIR/active.state" ] ||
    fail 'unsafe state directory left a live switchover marker'
[ ! -e "$UNSAFE_STATE_DIR/state.txt" ] ||
    fail 'unsafe state directory received transaction state'
[ ! -e "$UNSAFE_STATE_DIR/watchdog.pid" ] ||
    fail 'unsafe state directory received a watchdog receipt'
chmod 700 "$UNSAFE_STATE_DIR"

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

# The post-transition recovery path has the same final ownership obligation.
# This local injection kills only the generated optional child while fake iw
# preserves the pinned shape.  A failed required start must retain the marker
# and watchdog instead of claiming that optional rollback was verified.
OPTIONAL_DEATH_STATE_DIR="$(mktemp -d "$STATE_PREFIX"XXXXXX)"
OPTIONAL_DEATH_IW_CALLS_BEFORE="$(tr -d '[:space:]' <"$FAKE_IW_CALL_COUNT")"
case "$OPTIONAL_DEATH_IW_CALLS_BEFORE" in ''|*[!0-9]*) fail 'fake iw call counter is invalid';; esac
OPTIONAL_DEATH_IW_CALL=$((OPTIONAL_DEATH_IW_CALLS_BEFORE + 2))
if FAKE_TERMINATE_OPTIONAL_ON_IW_CALL="$OPTIONAL_DEATH_IW_CALL" FAKE_FAIL_REQUIRED=1 "$HELPER" --activate \
    --state-dir "$OPTIONAL_DEATH_STATE_DIR" --lease-seconds 60 \
    >"$TMP_ROOT/optional-death-activate.out" \
    2>"$TMP_ROOT/optional-death-activate.err"; then
    fail 'activation accepted an optional hostapd that died during recovery'
fi
grep -Fq 'required-PMF hostapd activation failed; rollback watchdog remains armed' \
    "$TMP_ROOT/optional-death-activate.err" ||
    fail 'optional-child recovery death did not retain its armed-watchdog diagnostic'
! grep -Fq 'optional rollback verified' "$TMP_ROOT/optional-death-activate.err" ||
    fail 'optional-child recovery death claimed verified optional rollback'
[ ! -e "$OPTIONAL_DEATH_STATE_DIR/rollback.status" ] ||
    fail 'optional-child recovery death wrote a verified rollback witness'
[ ! -e "$RUN_DIR/hostapd-5g.pid" ] ||
    fail 'optional-child recovery death left a stale optional pid receipt'
[ ! -e "$RUN_DIR/hostapd-5g-pmf-required.pid" ] ||
    fail 'optional-child recovery death left required hostapd active'
[ -e "$CONTROL_DIR/active.state" ] ||
    fail 'optional-child recovery death cleared the rollback marker'
[ -r "$OPTIONAL_DEATH_STATE_DIR/watchdog.pid" ] ||
    fail 'optional-child recovery death did not retain its watchdog receipt'
OPTIONAL_DEATH_WATCHDOG_PID="$(tr -d '[:space:]' <"$OPTIONAL_DEATH_STATE_DIR/watchdog.pid")"
/bin/kill -0 "$OPTIONAL_DEATH_WATCHDOG_PID" >/dev/null 2>&1 ||
    fail 'optional-child recovery death did not retain a live watchdog process'
"$HELPER" --rollback --state-dir "$OPTIONAL_DEATH_STATE_DIR" \
    >"$TMP_ROOT/optional-death-rollback.out" \
    2>"$TMP_ROOT/optional-death-rollback.err" ||
    fail 'stable explicit rollback did not recover optional-child death'
grep -Fxq 'PMF_AP_ROLLBACK=OPTIONAL_RESTORED' \
    "$TMP_ROOT/optional-death-rollback.out" ||
    fail 'optional-child recovery rollback did not report optional restoration'
[ ! -e "$CONTROL_DIR/active.state" ] ||
    fail 'optional-child recovery rollback left the active marker'
if /bin/kill -0 "$OPTIONAL_DEATH_WATCHDOG_PID" >/dev/null 2>&1; then
    fail 'optional-child recovery rollback left the watchdog live'
fi

# If the host network drifts during a required start that then fails, optional
# PMF may be restored but recovery is not verified.  The marker-bound watchdog
# must remain armed until a stable explicit rollback can prove the baseline.
TRANSITION_DRIFT_STATE_DIR="$(mktemp -d "$STATE_PREFIX"XXXXXX)"
if FAKE_MUTATE_NETWORK_ON_REQUIRED_START=1 FAKE_FAIL_REQUIRED=1 "$HELPER" --activate \
    --state-dir "$TRANSITION_DRIFT_STATE_DIR" --lease-seconds 60 \
    >"$TMP_ROOT/transition-drift-activate.out" \
    2>"$TMP_ROOT/transition-drift-activate.err"; then
    fail 'activation accepted a required-start failure with network drift'
fi
grep -Fq 'required-PMF hostapd activation failed; rollback watchdog remains armed' \
    "$TMP_ROOT/transition-drift-activate.err" ||
    fail 'post-transition drift did not retain its armed-watchdog diagnostic'
[ -r "$RUN_DIR/hostapd-5g.pid" ] ||
    fail 'post-transition drift did not restore optional hostapd'
TRANSITION_DRIFT_OPTIONAL_PID="$(tr -d '[:space:]' <"$RUN_DIR/hostapd-5g.pid")"
/bin/kill -0 "$TRANSITION_DRIFT_OPTIONAL_PID" >/dev/null 2>&1 ||
    fail 'post-transition drift restored no live optional hostapd'
[ ! -e "$RUN_DIR/hostapd-5g-pmf-required.pid" ] ||
    fail 'post-transition drift left required hostapd active'
[ -e "$CONTROL_DIR/active.state" ] ||
    fail 'post-transition drift cleared the rollback marker'
[ -r "$TRANSITION_DRIFT_STATE_DIR/watchdog.pid" ] ||
    fail 'post-transition drift did not retain its watchdog receipt'
TRANSITION_DRIFT_WATCHDOG_PID="$(tr -d '[:space:]' <"$TRANSITION_DRIFT_STATE_DIR/watchdog.pid")"
/bin/kill -0 "$TRANSITION_DRIFT_WATCHDOG_PID" >/dev/null 2>&1 ||
    fail 'post-transition drift did not retain a live watchdog process'
printf 'stable\n' >"$FAKE_NETWORK_STATE"
"$HELPER" --rollback --state-dir "$TRANSITION_DRIFT_STATE_DIR" \
    >"$TMP_ROOT/transition-drift-rollback.out" \
    2>"$TMP_ROOT/transition-drift-rollback.err" ||
    fail 'stable explicit rollback did not recover post-transition drift'
grep -Fxq 'PMF_AP_ROLLBACK=OPTIONAL_RESTORED' \
    "$TMP_ROOT/transition-drift-rollback.out" ||
    fail 'stable explicit rollback did not report optional restoration'
[ ! -e "$CONTROL_DIR/active.state" ] ||
    fail 'stable explicit rollback left the transition-drift marker'
if /bin/kill -0 "$TRANSITION_DRIFT_WATCHDOG_PID" >/dev/null 2>&1; then
    fail 'stable explicit rollback left the transition-drift watchdog live'
fi

# A required daemon can succeed while its launch changes the host-network
# signature.  Required-active publication must still be refused: the
# post-transition recovery restores optional PMF but retains its marker-bound
# watchdog until a stable explicit rollback can prove the original baseline.
POSTSTART_NETWORK_STATE_DIR="$(mktemp -d "$STATE_PREFIX"XXXXXX)"
if FAKE_MUTATE_NETWORK_ON_REQUIRED_START=1 "$HELPER" --activate \
    --state-dir "$POSTSTART_NETWORK_STATE_DIR" --lease-seconds 60 \
    >"$TMP_ROOT/poststart-network-activate.out" \
    2>"$TMP_ROOT/poststart-network-activate.err"; then
    fail 'activation accepted host-network drift during successful required start'
fi
grep -Fq 'required-PMF host-network invariants changed before state promotion; rollback watchdog remains armed' \
    "$TMP_ROOT/poststart-network-activate.err" ||
    fail 'successful required-start drift did not retain its armed-watchdog diagnostic'
! grep -Fxq 'PMF_AP_SWITCHOVER=REQUIRED_ACTIVE' \
    "$TMP_ROOT/poststart-network-activate.out" ||
    fail 'successful required-start drift published required-active success'
[ -r "$RUN_DIR/hostapd-5g.pid" ] ||
    fail 'successful required-start drift did not restore optional hostapd'
POSTSTART_NETWORK_OPTIONAL_PID="$(tr -d '[:space:]' <"$RUN_DIR/hostapd-5g.pid")"
/bin/kill -0 "$POSTSTART_NETWORK_OPTIONAL_PID" >/dev/null 2>&1 ||
    fail 'successful required-start drift restored no live optional hostapd'
[ ! -e "$RUN_DIR/hostapd-5g-pmf-required.pid" ] ||
    fail 'successful required-start drift left required hostapd active'
[ -e "$CONTROL_DIR/active.state" ] ||
    fail 'successful required-start drift cleared the rollback marker'
[ -r "$POSTSTART_NETWORK_STATE_DIR/watchdog.pid" ] ||
    fail 'successful required-start drift did not retain its watchdog receipt'
POSTSTART_NETWORK_WATCHDOG_PID="$(tr -d '[:space:]' <"$POSTSTART_NETWORK_STATE_DIR/watchdog.pid")"
/bin/kill -0 "$POSTSTART_NETWORK_WATCHDOG_PID" >/dev/null 2>&1 ||
    fail 'successful required-start drift did not retain a live watchdog process'
printf 'stable\n' >"$FAKE_NETWORK_STATE"
"$HELPER" --rollback --state-dir "$POSTSTART_NETWORK_STATE_DIR" \
    >"$TMP_ROOT/poststart-network-rollback.out" \
    2>"$TMP_ROOT/poststart-network-rollback.err" ||
    fail 'stable explicit rollback did not recover successful required-start drift'
grep -Fxq 'PMF_AP_ROLLBACK=OPTIONAL_RESTORED' \
    "$TMP_ROOT/poststart-network-rollback.out" ||
    fail 'successful required-start drift rollback did not report optional restoration'
[ ! -e "$CONTROL_DIR/active.state" ] ||
    fail 'successful required-start drift rollback left the active marker'
if /bin/kill -0 "$POSTSTART_NETWORK_WATCHDOG_PID" >/dev/null 2>&1; then
    fail 'successful required-start drift rollback left the watchdog live'
fi

# A pre-stop watchdog proof does not authorize required-active publication
# after the AP process transition.  Fake ip kills only the generated watchdog
# at activation's third route probe, after required startup and before the
# helper writes its required state.
POSTPROMOTION_WATCHDOG_STATE_DIR="$(mktemp -d "$STATE_PREFIX"XXXXXX)"
ROUTE_CALLS_BEFORE="$(tr -d '[:space:]' <"$FAKE_ROUTE_CALL_COUNT")"
case "$ROUTE_CALLS_BEFORE" in ''|*[!0-9]*) fail 'fake route call counter is invalid';; esac
POSTPROMOTION_WATCHDOG_ROUTE_CALL=$((ROUTE_CALLS_BEFORE + 3))
if FAKE_WATCHDOG_PID_FILE="$POSTPROMOTION_WATCHDOG_STATE_DIR/watchdog.pid" FAKE_TERMINATE_WATCHDOG_ON_ROUTE_CALL="$POSTPROMOTION_WATCHDOG_ROUTE_CALL" "$HELPER" --activate --state-dir "$POSTPROMOTION_WATCHDOG_STATE_DIR" --lease-seconds 60 >"$TMP_ROOT/postpromotion-watchdog-activate.out" 2>"$TMP_ROOT/postpromotion-watchdog-activate.err"; then
    fail 'activation accepted a watchdog that died during required-PMF startup'
fi
grep -Fq 'rollback watchdog is not exact before required-PMF state promotion; optional rollback verified' "$TMP_ROOT/postpromotion-watchdog-activate.err" ||
    fail 'post-promotion watchdog death did not retain its categorical diagnostic'
! grep -Fxq 'PMF_AP_SWITCHOVER=REQUIRED_ACTIVE' "$TMP_ROOT/postpromotion-watchdog-activate.out" ||
    fail 'post-promotion watchdog death published required-active success'
[ -r "$RUN_DIR/hostapd-5g.pid" ] ||
    fail 'post-promotion watchdog death did not restore optional hostapd'
POSTPROMOTION_WATCHDOG_OPTIONAL_PID="$(tr -d '[:space:]' <"$RUN_DIR/hostapd-5g.pid")"
/bin/kill -0 "$POSTPROMOTION_WATCHDOG_OPTIONAL_PID" >/dev/null 2>&1 ||
    fail 'post-promotion watchdog death restored no live optional hostapd'
[ ! -e "$RUN_DIR/hostapd-5g-pmf-required.pid" ] ||
    fail 'post-promotion watchdog death left required hostapd active'
[ ! -e "$CONTROL_DIR/active.state" ] ||
    fail 'post-promotion watchdog death left a live switchover marker'
[ ! -e "$POSTPROMOTION_WATCHDOG_STATE_DIR/watchdog.pid" ] ||
    fail 'post-promotion watchdog death retained a stale watchdog receipt'

# Required-hostapd startup evidence can become stale while later promotion
# gates run.  Fake ip removes only the generated required child at the third
# route probe, after the first process/AP attestation and before state commit.
FINAL_REQUIRED_PROMOTION_STATE_DIR="$(mktemp -d "$STATE_PREFIX"XXXXXX)"
ROUTE_CALLS_BEFORE="$(tr -d '[:space:]' <"$FAKE_ROUTE_CALL_COUNT")"
case "$ROUTE_CALLS_BEFORE" in ''|*[!0-9]*) fail 'fake route call counter is invalid';; esac
FINAL_REQUIRED_PROMOTION_ROUTE_CALL=$((ROUTE_CALLS_BEFORE + 3))
if FAKE_TERMINATE_REQUIRED_ON_ROUTE_CALL="$FINAL_REQUIRED_PROMOTION_ROUTE_CALL" "$HELPER" --activate --state-dir "$FINAL_REQUIRED_PROMOTION_STATE_DIR" --lease-seconds 60 >"$TMP_ROOT/final-required-promotion-activate.out" 2>"$TMP_ROOT/final-required-promotion-activate.err"; then
    fail 'activation accepted a required hostapd that died before final state promotion'
fi
grep -Fq 'required-PMF hostapd is not exact before final state promotion; optional rollback verified' "$TMP_ROOT/final-required-promotion-activate.err" ||
    fail 'final required-process death did not retain its categorical diagnostic'
! grep -Fxq 'PMF_AP_SWITCHOVER=REQUIRED_ACTIVE' "$TMP_ROOT/final-required-promotion-activate.out" ||
    fail 'final required-process death published required-active success'
[ -r "$RUN_DIR/hostapd-5g.pid" ] ||
    fail 'final required-process death did not restore optional hostapd'
FINAL_REQUIRED_PROMOTION_OPTIONAL_PID="$(tr -d '[:space:]' <"$RUN_DIR/hostapd-5g.pid")"
/bin/kill -0 "$FINAL_REQUIRED_PROMOTION_OPTIONAL_PID" >/dev/null 2>&1 ||
    fail 'final required-process death restored no live optional hostapd'
[ ! -e "$RUN_DIR/hostapd-5g-pmf-required.pid" ] ||
    fail 'final required-process death left required hostapd active'
[ ! -e "$CONTROL_DIR/active.state" ] ||
    fail 'final required-process death left a live switchover marker'
[ -r "$FINAL_REQUIRED_PROMOTION_STATE_DIR/watchdog.pid" ] ||
    fail 'final required-process death did not retain its watchdog receipt for liveness check'
FINAL_REQUIRED_PROMOTION_WATCHDOG_PID="$(tr -d '[:space:]' <"$FINAL_REQUIRED_PROMOTION_STATE_DIR/watchdog.pid")"
if /bin/kill -0 "$FINAL_REQUIRED_PROMOTION_WATCHDOG_PID" >/dev/null 2>&1; then
    fail 'final required-process death left the watchdog live'
fi

# A staged required config can change after the pre-stop fence but while the
# required daemon consumes it.  The changed file must not be promoted, and the
# helper must not restart optional hostapd from an unresolved pair.  Restoring
# the generated baseline later permits the normal marker-bound rollback.
TRANSITION_CONFIG_STATE_DIR="$(mktemp -d "$STATE_PREFIX"XXXXXX)"
if FAKE_MUTATE_REQUIRED_CONFIG_ON_START=1 "$HELPER" --activate \
    --state-dir "$TRANSITION_CONFIG_STATE_DIR" --lease-seconds 60 \
    >"$TMP_ROOT/transition-config-activate.out" \
    2>"$TMP_ROOT/transition-config-activate.err"; then
    fail 'activation accepted a required config changed during daemon start'
fi
grep -Fq 'required-PMF configuration changed before state promotion; rollback watchdog remains armed' \
    "$TMP_ROOT/transition-config-activate.err" ||
    fail 'transition config drift did not retain its armed-watchdog diagnostic'
! grep -Fxq 'PMF_AP_SWITCHOVER=REQUIRED_ACTIVE' \
    "$TMP_ROOT/transition-config-activate.out" ||
    fail 'transition config drift published required-active success'
[ ! -e "$RUN_DIR/hostapd-5g-pmf-required.pid" ] ||
    fail 'transition config drift did not quiesce required hostapd'
[ ! -e "$RUN_DIR/hostapd-5g.pid" ] ||
    fail 'transition config drift restarted optional hostapd from changed files'
[ -e "$CONTROL_DIR/active.state" ] ||
    fail 'transition config drift cleared the rollback marker'
[ -r "$TRANSITION_CONFIG_STATE_DIR/watchdog.pid" ] ||
    fail 'transition config drift did not retain its watchdog receipt'
TRANSITION_CONFIG_WATCHDOG_PID="$(tr -d '[:space:]' <"$TRANSITION_CONFIG_STATE_DIR/watchdog.pid")"
/bin/kill -0 "$TRANSITION_CONFIG_WATCHDOG_PID" >/dev/null 2>&1 ||
    fail 'transition config drift did not retain a live watchdog process'
write_config "$REQUIRED" 2 'WPA-PSK-SHA256' fixture-network
"$HELPER" --rollback --state-dir "$TRANSITION_CONFIG_STATE_DIR" \
    >"$TMP_ROOT/transition-config-rollback.out" \
    2>"$TMP_ROOT/transition-config-rollback.err" ||
    fail 'baseline restoration did not permit transition-config rollback'
grep -Fxq 'PMF_AP_ROLLBACK=OPTIONAL_RESTORED' \
    "$TMP_ROOT/transition-config-rollback.out" ||
    fail 'baseline restoration did not report optional restoration'
[ -r "$RUN_DIR/hostapd-5g.pid" ] ||
    fail 'baseline restoration did not restart optional hostapd'
[ ! -e "$CONTROL_DIR/active.state" ] ||
    fail 'baseline restoration left the transition-config marker'
if /bin/kill -0 "$TRANSITION_CONFIG_WATCHDOG_PID" >/dev/null 2>&1; then
    fail 'baseline restoration left the transition-config watchdog live'
fi

# A changed route/address/forwarding signature after the independent watchdog
# is ready must be rejected *before* optional hostapd is stopped.  The fake
# route source drifts exactly on activation's second signature read: baseline
# capture is the first read and the new pre-stop fence is the second.
PRESTOP_DRIFT_STATE_DIR="$(mktemp -d "$STATE_PREFIX"XXXXXX)"
OPTIONAL_PID_BEFORE="$(tr -d '[:space:]' <"$RUN_DIR/hostapd-5g.pid")"
ROUTE_CALLS_BEFORE="$(tr -d '[:space:]' <"$FAKE_ROUTE_CALL_COUNT")"
case "$ROUTE_CALLS_BEFORE" in ''|*[!0-9]*) fail 'fake route call counter is invalid';; esac
PRESTOP_DRIFT_CALL=$((ROUTE_CALLS_BEFORE + 2))
if FAKE_DRIFT_ON_ROUTE_CALL="$PRESTOP_DRIFT_CALL" "$HELPER" --activate \
    --state-dir "$PRESTOP_DRIFT_STATE_DIR" --lease-seconds 60 \
    >"$TMP_ROOT/prestop-drift-activate.out" \
    2>"$TMP_ROOT/prestop-drift-activate.err"; then
    fail 'activation accepted host-network drift before optional-PMF stop'
fi
grep -Fq 'host network invariants changed before optional-PMF stop' \
    "$TMP_ROOT/prestop-drift-activate.err" ||
    fail 'pre-stop drift did not retain its categorical diagnostic'
OPTIONAL_PID_AFTER="$(tr -d '[:space:]' <"$RUN_DIR/hostapd-5g.pid")"
[ "$OPTIONAL_PID_BEFORE" = "$OPTIONAL_PID_AFTER" ] ||
    fail 'pre-stop drift stopped or replaced optional hostapd'
/bin/kill -0 "$OPTIONAL_PID_AFTER" >/dev/null 2>&1 ||
    fail 'optional hostapd was not alive after pre-stop drift'
[ ! -e "$RUN_DIR/hostapd-5g-pmf-required.pid" ] ||
    fail 'pre-stop drift started required hostapd'
[ ! -e "$CONTROL_DIR/active.state" ] ||
    fail 'pre-stop drift left a live switchover marker'
[ -r "$PRESTOP_DRIFT_STATE_DIR/watchdog.pid" ] ||
    fail 'pre-stop drift did not retain its watchdog receipt for liveness check'
PRESTOP_WATCHDOG_PID="$(tr -d '[:space:]' <"$PRESTOP_DRIFT_STATE_DIR/watchdog.pid")"
if /bin/kill -0 "$PRESTOP_WATCHDOG_PID" >/dev/null 2>&1; then
    fail 'pre-stop drift left a live watchdog process'
fi

# A staged required configuration can be modified after its initial semantic
# validation yet before hostapd consumes it.  The mutation is a valid but
# previously unadmitted group-rekey directive and occurs on activation's
# second route probe: after watchdog readiness and before the required launch.
# The helper must reject that stale configuration before it stops optional PMF.
PRESTOP_CONFIG_DRIFT_STATE_DIR="$(mktemp -d "$STATE_PREFIX"XXXXXX)"
OPTIONAL_PID_BEFORE="$(tr -d '[:space:]' <"$RUN_DIR/hostapd-5g.pid")"
ROUTE_CALLS_BEFORE="$(tr -d '[:space:]' <"$FAKE_ROUTE_CALL_COUNT")"
case "$ROUTE_CALLS_BEFORE" in ''|*[!0-9]*) fail 'fake route call counter is invalid';; esac
PRESTOP_CONFIG_DRIFT_CALL=$((ROUTE_CALLS_BEFORE + 2))
if FAKE_MUTATE_REQUIRED_CONFIG_ON_ROUTE_CALL="$PRESTOP_CONFIG_DRIFT_CALL" "$HELPER" --activate \
    --state-dir "$PRESTOP_CONFIG_DRIFT_STATE_DIR" --lease-seconds 60 \
    >"$TMP_ROOT/prestop-config-drift-activate.out" \
    2>"$TMP_ROOT/prestop-config-drift-activate.err"; then
    fail 'activation accepted a staged configuration changed before optional-PMF stop'
fi
grep -Fq 'PMF configurations changed before optional-PMF stop' \
    "$TMP_ROOT/prestop-config-drift-activate.err" ||
    fail 'pre-stop configuration drift did not retain its categorical diagnostic'
OPTIONAL_PID_AFTER="$(tr -d '[:space:]' <"$RUN_DIR/hostapd-5g.pid")"
[ "$OPTIONAL_PID_BEFORE" = "$OPTIONAL_PID_AFTER" ] ||
    fail 'pre-stop configuration drift stopped or replaced optional hostapd'
/bin/kill -0 "$OPTIONAL_PID_AFTER" >/dev/null 2>&1 ||
    fail 'optional hostapd was not alive after pre-stop configuration drift'
[ ! -e "$RUN_DIR/hostapd-5g-pmf-required.pid" ] ||
    fail 'pre-stop configuration drift started required hostapd'
[ ! -e "$CONTROL_DIR/active.state" ] ||
    fail 'pre-stop configuration drift left a live switchover marker'
[ -r "$PRESTOP_CONFIG_DRIFT_STATE_DIR/watchdog.pid" ] ||
    fail 'pre-stop configuration drift did not retain its watchdog receipt for liveness check'
PRESTOP_CONFIG_WATCHDOG_PID="$(tr -d '[:space:]' <"$PRESTOP_CONFIG_DRIFT_STATE_DIR/watchdog.pid")"
if /bin/kill -0 "$PRESTOP_CONFIG_WATCHDOG_PID" >/dev/null 2>&1; then
    fail 'pre-stop configuration drift left a live watchdog process'
fi
write_config "$REQUIRED" 2 'WPA-PSK-SHA256' fixture-network

# A readiness acknowledgement does not prove the detached recovery owner
# survived until the first AP mutation.  Fake ip removes only the generated
# watchdog on activation's final route probe while keeping the route output
# stable; optional PMF must remain untouched rather than entering required PMF.
WATCHDOG_EDGE_STATE_DIR="$(mktemp -d "$STATE_PREFIX"XXXXXX)"
OPTIONAL_PID_BEFORE="$(tr -d '[:space:]' <"$RUN_DIR/hostapd-5g.pid")"
ROUTE_CALLS_BEFORE="$(tr -d '[:space:]' <"$FAKE_ROUTE_CALL_COUNT")"
case "$ROUTE_CALLS_BEFORE" in ''|*[!0-9]*) fail 'fake route call counter is invalid';; esac
WATCHDOG_EDGE_ROUTE_CALL=$((ROUTE_CALLS_BEFORE + 2))
if FAKE_WATCHDOG_PID_FILE="$WATCHDOG_EDGE_STATE_DIR/watchdog.pid" FAKE_TERMINATE_WATCHDOG_ON_ROUTE_CALL="$WATCHDOG_EDGE_ROUTE_CALL" "$HELPER" --activate --state-dir "$WATCHDOG_EDGE_STATE_DIR" --lease-seconds 60 >"$TMP_ROOT/watchdog-edge-activate.out" 2>"$TMP_ROOT/watchdog-edge-activate.err"; then
    fail 'activation accepted a watchdog that died before optional-PMF stop'
fi
grep -Fq 'rollback watchdog is not exact before optional-PMF stop; optional-PMF state retained' "$TMP_ROOT/watchdog-edge-activate.err" ||
    fail 'pre-transition watchdog death retained no categorical diagnostic'
OPTIONAL_PID_AFTER="$(tr -d '[:space:]' <"$RUN_DIR/hostapd-5g.pid")"
[ "$OPTIONAL_PID_BEFORE" = "$OPTIONAL_PID_AFTER" ] ||
    fail 'pre-transition watchdog death stopped or replaced optional hostapd'
/bin/kill -0 "$OPTIONAL_PID_AFTER" >/dev/null 2>&1 ||
    fail 'optional hostapd was not alive after pre-transition watchdog death'
[ ! -e "$RUN_DIR/hostapd-5g-pmf-required.pid" ] ||
    fail 'pre-transition watchdog death started required hostapd'
[ ! -e "$CONTROL_DIR/active.state" ] ||
    fail 'pre-transition watchdog death left a live switchover marker'
[ ! -e "$WATCHDOG_EDGE_STATE_DIR/watchdog.pid" ] ||
    fail 'pre-transition watchdog death retained a stale watchdog receipt'

# The start helper's own liveness observation is not a state-promotion proof.
# Here fake iw terminates the generated required child after wait_hostapd_active
# has observed it and immediately before that helper returns.  Activation must
# not publish required state from that stale observation.
PREPROMOTION_DEATH_STATE_DIR="$(mktemp -d "$STATE_PREFIX"XXXXXX)"
if FAKE_TERMINATE_REQUIRED_ON_IW=1 "$HELPER" --activate \
    --state-dir "$PREPROMOTION_DEATH_STATE_DIR" --lease-seconds 60 \
    >"$TMP_ROOT/prepromotion-death-activate.out" \
    2>"$TMP_ROOT/prepromotion-death-activate.err"; then
    fail 'activation accepted a required hostapd that died before state promotion'
fi
grep -Fq 'required-PMF hostapd post-start attestation failed' \
    "$TMP_ROOT/prepromotion-death-activate.err" ||
    fail 'pre-promotion child death did not retain its categorical diagnostic'
! grep -Fxq 'PMF_AP_SWITCHOVER=REQUIRED_ACTIVE' \
    "$TMP_ROOT/prepromotion-death-activate.out" ||
    fail 'pre-promotion child death published required-active success'
[ -r "$RUN_DIR/hostapd-5g.pid" ] ||
    fail 'pre-promotion child death did not restore optional hostapd'
PREPROMOTION_OPTIONAL_PID="$(tr -d '[:space:]' <"$RUN_DIR/hostapd-5g.pid")"
/bin/kill -0 "$PREPROMOTION_OPTIONAL_PID" >/dev/null 2>&1 ||
    fail 'pre-promotion child death restored no live optional hostapd'
[ ! -e "$RUN_DIR/hostapd-5g-pmf-required.pid" ] ||
    fail 'pre-promotion child death left required hostapd active'
[ ! -e "$CONTROL_DIR/active.state" ] ||
    fail 'pre-promotion child death left a live switchover marker'
[ -r "$PREPROMOTION_DEATH_STATE_DIR/watchdog.pid" ] ||
    fail 'pre-promotion child death did not retain its watchdog receipt for liveness check'
PREPROMOTION_WATCHDOG_PID="$(tr -d '[:space:]' <"$PREPROMOTION_DEATH_STATE_DIR/watchdog.pid")"
if /bin/kill -0 "$PREPROMOTION_WATCHDOG_PID" >/dev/null 2>&1; then
    fail 'pre-promotion child death left a live watchdog process'
fi

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
