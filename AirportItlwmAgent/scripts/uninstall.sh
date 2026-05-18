#!/bin/bash
#
# AirportItlwmAgent — uninstall script (project-owned PLTI PMK producer).
#
# Removes the LaunchDaemon and binary. The project-owned credential
# keychain at /Library/Keychains/AirportItlwm.keychain and the
# install-time unlock-password file at
# /etc/airportitlwm/keychain-password are LEFT IN PLACE on
# purpose: the keychain holds operator-supplied WPA2 passphrases
# the maintainer may want to preserve across reinstalls, and the
# password file is required to unlock that keychain on next
# install. To remove them explicitly:
#
#     sudo /usr/bin/security delete-keychain \
#         /Library/Keychains/AirportItlwm.keychain
#     sudo /bin/rm -f /etc/airportitlwm/keychain-password
#     sudo /bin/rmdir /etc/airportitlwm 2>/dev/null || true
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

echo "Done. /Library/Keychains/AirportItlwm.keychain and"
echo "/etc/airportitlwm/keychain-password left in place by design"
echo "(remove explicitly with security delete-keychain + rm + rmdir"
echo "if desired; see this script's header for the exact commands)."
