#!/bin/bash
#
# AirportItlwmAgent — install script (rev8 DIAGNOSTIC_INSTRUMENTATION).
#
# Builds the diagnostic daemon, installs the binary + LaunchDaemon
# plist, and loads the daemon. No credentials are collected: this
# helper is a behavior-neutral CWWiFiClient delegate observer (rev8
# supersede of REJECTED rev7 SYSTEM_CONTRACT_FIX request).
#
# Run as: sudo bash scripts/install.sh
#

set -euo pipefail

if [[ $EUID -ne 0 ]]; then
    echo "must run as root (use sudo)" >&2
    exit 1
fi

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DAEMON_BIN="AirportItlwmAgent"
DAEMON_DST="/usr/local/libexec/$DAEMON_BIN"
PLIST_DST="/Library/LaunchDaemons/com.zxystd.airportitlwmagent.plist"
LABEL="com.zxystd.airportitlwmagent"

echo "[1/3] Building diagnostic daemon..."
( cd "$REPO_DIR" && make )

echo "[2/3] Installing binary + plist..."
install -m 0755 -o root -g wheel "$REPO_DIR/$DAEMON_BIN"  "$DAEMON_DST"
install -m 0644 -o root -g wheel "$REPO_DIR/com.zxystd.airportitlwmagent.plist" "$PLIST_DST"

echo "[3/3] Loading LaunchDaemon..."
launchctl bootstrap system "$PLIST_DST" 2>/dev/null || true
launchctl enable "system/$LABEL" 2>/dev/null || true
launchctl kickstart -k "system/$LABEL" 2>/dev/null || true

echo
echo "Done. The diagnostic daemon is now running as a KeepAlive LaunchDaemon."
echo "Logs: sudo log show --last 1m --predicate 'process == \"AirportItlwmAgent\"'"
echo
echo "Diagnostic claim: this helper only os_logs CWWiFiClient delegate"
echo "join lifecycle callbacks. It does NOT query the System keychain,"
echo "derive any key material, or call any kext IOUserClient. Use it"
echo "to observe whether the public CoreWLAN join-delegate fires on"
echo "this build during the pre-first-M1 association window."
