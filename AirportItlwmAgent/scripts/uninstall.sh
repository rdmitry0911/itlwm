#!/bin/bash
#
# AirportItlwmAgent — uninstall script (rev8 DIAGNOSTIC_INSTRUMENTATION).
#
# Run as: sudo bash scripts/uninstall.sh
#

set -euo pipefail

if [[ $EUID -ne 0 ]]; then
    echo "must run as root (use sudo)" >&2
    exit 1
fi

LABEL="com.zxystd.airportitlwmagent"
DAEMON_DST="/usr/local/libexec/AirportItlwmAgent"
PLIST_DST="/Library/LaunchDaemons/com.zxystd.airportitlwmagent.plist"

echo "[1/2] Stopping LaunchDaemon..."
launchctl bootout "system/$LABEL" 2>/dev/null || true
launchctl disable "system/$LABEL" 2>/dev/null || true

echo "[2/2] Removing binary + plist..."
rm -f "$DAEMON_DST"
rm -f "$PLIST_DST"

echo "Done."
