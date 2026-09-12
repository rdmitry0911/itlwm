#!/bin/bash
# Read-only prerequisite, not a GUI association or traffic test.
set -euo pipefail

readonly preferences=/Library/Preferences/SystemConfiguration/com.apple.airport.preferences.plist
readonly agent=/System/Library/CoreServices/WiFiAgent.app/Contents/MacOS/WiFiAgent
readonly observation_seconds=${1:-12}
case "$observation_seconds" in ''|*[!0-9]*) exit 2;; esac
if (( observation_seconds < 12 || observation_seconds > 60 )); then
    printf 'Observation interval must be 12..60 seconds.\n' >&2
    exit 2
fi
if [[ "$(uname -s)" != Darwin ]]; then
    printf 'Run this check in the macOS GUI session.\n' >&2
    exit 2
fi
readonly console_uid=$(stat -f %u /dev/console)
if [[ "$console_uid" == 0 || "$(id -u)" != "$console_uid" ]]; then
    printf 'Run as the logged-in console user, not root or another SSH user.\n' >&2
    exit 3
fi

check_preferences() {
    if [[ ! -r "$preferences" ]] || ! plutil -lint -s "$preferences"; then
        printf 'GUI prerequisite failed: console user cannot read a valid airport preferences plist.\n' >&2
        return 1
    fi
}

agent_pid() {
    local service pid
    service=$(launchctl print "gui/$console_uid/com.apple.wifi.WiFiAgent") || return 1
    pid=$(printf '%s\n' "$service" | awk '$1 == "pid" && $2 == "=" { print $3; exit }')
    case "$pid" in ''|*[!0-9]*) return 1;; esac
    [[ "$(ps -p "$pid" -o comm=)" == "$agent" ]] || return 1
    printf '%s\n' "$pid"
}

check_preferences || exit 4
if ! initial_pid=$(agent_pid); then
    printf 'GUI prerequisite failed: native WiFiAgent is not running.\n' >&2
    exit 5
fi
for ((elapsed=0; elapsed<observation_seconds; elapsed++)); do
    sleep 1
    check_preferences || exit 4
    if ! current_pid=$(agent_pid) || [[ "$current_pid" != "$initial_pid" ]]; then
        printf 'GUI prerequisite failed: native WiFiAgent exited or restarted.\n' >&2
        exit 5
    fi
done
printf 'GUI service prerequisite PASS: console_uid=%s WiFiAgent_pid=%s stable_seconds=%s\n' \
    "$console_uid" "$initial_pid" "$observation_seconds"
printf 'No daemon restart, permissions/profile change, join, toggle or traffic test was performed.\n'
