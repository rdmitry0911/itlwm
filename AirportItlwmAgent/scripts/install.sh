#!/bin/bash
#
# AirportItlwmAgent — install script (project-owned PLTI PMK producer).
#
# Builds the root LaunchDaemon, installs the binary + plist, creates
# the project-owned credential keychain, and loads the daemon. The
# helper is the userland half of the project-owned PLTI PMK producer
# pipeline: it stays resident under launchd, unlocks the project
# keychain before the first association target so Tahoe's cold
# securityd/keychain latency is paid outside the kext pre-M1 wait
# window, retries opening the 'PLTI' user client on AirportItlwm
# until the kext service appears, blocks in WaitAssociationTarget
# under the kext command gate until the kext's PSK association-start
# edge publishes a target, looks up the matching WPA2 passphrase in
# /Library/Keychains/AirportItlwm.keychain (service "AirportItlwm
# WiFi PSK", account = SSID) via SecKeychainFindGenericPassword,
# derives the 32-byte WPA2 PMK with PBKDF2-HMAC-SHA1 (4096
# iterations, ssid as salt) via CommonCrypto, and delivers it back
# through DeliverPMK with the kext-assigned generation echoed as the
# scalar argument (target-identity replay guard).
#
# Credential acquisition contract:
#
#   - The helper does NOT read airportd-populated entries in
#     /Library/Keychains/System.keychain. Those entries have an
#     ACL that authorizes decrypt only to /usr/libexec/airportd and
#     to apps in the Apple-anchored AirPort application group; an
#     ad-hoc-codesigned project helper is not on that list and would
#     receive errSecInteractionNotAllowed.
#
#   - Instead, this script creates a dedicated project-owned
#     system-domain keychain at /Library/Keychains/AirportItlwm.keychain
#     with a per-install random unlock password (see "Keychain
#     unlock-password mechanism" below for the password source) and
#     adds it to the system keychain search list. The script then explicitly enforces mode 0600 root:wheel
#     on the resulting keychain file (and on the -db sibling, if
#     present) so the filesystem trust boundary holds even if
#     the macOS system-domain default permissions on a freshly
#     created keychain file differ from 0600.
#
#   - The operator MUST populate this keychain with one
#     generic-password item per target SSID before the helper can
#     deliver a PMK. Because install.sh deliberately leaves the
#     project keychain at the macOS default auto-lock policy (see
#     "set-keychain-settings Tahoe 26.2 behavior" below), the
#     project keychain may be locked by the time the operator
#     runs the add command. The exact two-step operator workflow
#     is:
#
#       sudo security unlock-keychain \
#           -p "$(sudo /bin/cat /etc/airportitlwm/keychain-password)" \
#           /Library/Keychains/AirportItlwm.keychain
#       sudo security add-generic-password \
#           -s "AirportItlwm WiFi PSK" \
#           -a "<SSID>" \
#           -w "<WPA2 passphrase>" \
#           -A \
#           /Library/Keychains/AirportItlwm.keychain
#
#     Step 1 unlocks the project keychain using the per-install
#     random unlock password the install script wrote to
#     /etc/airportitlwm/keychain-password (mode 0600 root:wheel).
#     Without step 1, step 2 returns OSStatus -25293 /
#     CSSMERR_DL_OPERATION_AUTH_DENIED on macOS 26.2 Tahoe
#     because Tahoe 26.2 SecKeychainItemCreateFromContent on
#     a locked system-domain keychain returns "passphrase
#     incorrect" before consulting the per-item access list.
#     The -A flag on step 2 makes the per-item access list
#     permissive so the ad-hoc-codesigned helper can decrypt
#     the password without additional ACL setup. The script
#     does NOT add any entries automatically; PSK material must
#     come from the operator's own trust boundary (no parallel
#     non-canonical entries in System.keychain, no fallback to
#     airportd-created items, no fake / hard-coded passphrases).
#
# Keychain unlock-password mechanism:
#
#     On macOS 26 Tahoe, `security create-keychain -p ""` produces
#     a keychain whose actual unlock password does NOT equal the
#     literal empty byte string, so empty-password unlock fails
#     deterministically with OSStatus -25293 and
#     CSSMERR_DL_OPERATION_AUTH_DENIED. To work around this Tahoe
#     behavior without claiming any secret-protection guarantee
#     for the unlock password itself, the install script:
#
#       1. ensures /etc/airportitlwm/ exists with mode 0700,
#          root:wheel;
#       2. if /etc/airportitlwm/keychain-password does not exist,
#          generates a per-install random unlock password (32
#          bytes of /dev/urandom, base64-encoded) and writes it
#          atomically to that file with mode 0600, root:wheel;
#       3. reads the password from that file and passes it to
#          security create-keychain -p and unlock-keychain -p, then
#          explicitly chowns root:wheel + chmods 0600 on the
#          resulting keychain file (and the -db sibling) so the
#          filesystem trust boundary holds; no `security
#          set-keychain-settings` call is made (see "set-keychain-
#          settings Tahoe 26.2 behavior" below);
#       4. on reinstall, reuses the existing password file so the
#          existing project keychain (and any operator-supplied
#          PSK items in it) survives unchanged.
#
#   - set-keychain-settings Tahoe 26.2 behavior: on macOS 26.2
#     Tahoe, `security set-keychain-settings -lut 0` returns
#     "passphrase incorrect" / OSStatus -25293 / underlying
#     CSSMERR_DL_OPERATION_AUTH_DENIED immediately after a
#     successful `create-keychain` + `unlock-keychain` pair with
#     the same per-install password, independently of the password
#     value and independently of the flag set tried (`-lut 0`,
#     `-t 0`, no flags). Because the helper opens the project
#     keychain and calls SecKeychainUnlock with the per-install
#     password bytes on every relaunch (the helper does not
#     depend on the keychain staying unlocked between relaunches),
#     disabling auto-lock at install time is not actually required
#     for helper correctness. This script therefore intentionally
#     does NOT invoke `security set-keychain-settings`; the
#     keychain may auto-lock between relaunches and the helper
#     will unlock it defensively on every association edge.
#
#     The helper reads /etc/airportitlwm/keychain-password on
#     every relaunch, passes the bytes to SecKeychainUnlock, and
#     immediately scrubs the local buffer. The security trust
#     boundary is filesystem permissions on BOTH the keychain
#     file and the unlock-password file (both root-only); the
#     unlock password itself is not a separate secret -- anyone
#     who can read the password file can also read the keychain
#     file, so the only role of the unlock password is to drive
#     the Tahoe Security framework APIs that reject a literal
#     empty password.
#
# The helper runs as a root LaunchDaemon because /Library/Keychains/
# AirportItlwm.keychain and /etc/airportitlwm/keychain-password
# are root-only files; the helper never logs the operator PSK
# passphrase, the derived PMK, or the keychain unlock password.
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

PROJECT_KEYCHAIN="/Library/Keychains/AirportItlwm.keychain"
PASSWORD_DIR="/etc/airportitlwm"
PASSWORD_FILE="$PASSWORD_DIR/keychain-password"

echo "[1/5] Building PLTI PMK producer daemon..."
( cd "$REPO_DIR" && make )

echo "[2/5] Installing binary + plist..."
install -m 0755 -o root -g wheel "$REPO_DIR/$DAEMON_BIN"  "$DAEMON_DST"
install -m 0644 -o root -g wheel "$REPO_DIR/com.zxystd.airportitlwmagent.plist" "$PLIST_DST"

echo "[3/5] Ensuring project keychain unlock-password file exists..."
install -d -m 0700 -o root -g wheel "$PASSWORD_DIR"
if [[ ! -s "$PASSWORD_FILE" ]]; then
    umask 0177
    # 32 bytes of /dev/urandom, base64-encoded; 44 base64 chars
    # plus trailing newline (the helper strips the newline before
    # passing the bytes to SecKeychainUnlock).
    /bin/dd if=/dev/urandom of="$PASSWORD_FILE.new" bs=32 count=1 2>/dev/null
    /usr/bin/base64 < "$PASSWORD_FILE.new" > "$PASSWORD_FILE.b64"
    install -m 0600 -o root -g wheel "$PASSWORD_FILE.b64" "$PASSWORD_FILE"
    rm -f "$PASSWORD_FILE.new" "$PASSWORD_FILE.b64"
    echo "    wrote $PASSWORD_FILE (mode 0600, root:wheel,"
    echo "    per-install random base64-encoded 32-byte value)."
else
    echo "    keeping existing $PASSWORD_FILE (operator-managed)."
fi
KEYCHAIN_PW=$(cat "$PASSWORD_FILE")

echo "[4/5] Ensuring project-owned credential keychain exists..."
if [[ ! -e "$PROJECT_KEYCHAIN" && ! -e "${PROJECT_KEYCHAIN}-db" ]]; then
    # Create with the per-install unlock password. NOTE: the
    # `-p "$KEYCHAIN_PW"` argument is visible in argv during the
    # brief install-time `security` invocation window; the
    # mitigations are (i) the password is per-install random
    # (not reused across guests), (ii) the password file itself
    # is mode 0600 root:wheel and the keychain file is enforced
    # to the same mode/owner immediately after creation, so
    # anyone able to observe the argv on this host is already
    # privileged enough to read both files directly.
    /usr/bin/security create-keychain -p "$KEYCHAIN_PW" "$PROJECT_KEYCHAIN"
    # Enforce the filesystem trust boundary explicitly: chmod
    # 0600 and chown root:wheel on the resulting keychain file
    # (security may produce either /...AirportItlwm.keychain or
    # /...AirportItlwm.keychain-db depending on macOS version).
    for KC_FILE in "$PROJECT_KEYCHAIN" "${PROJECT_KEYCHAIN}-db"; do
        if [[ -e "$KC_FILE" ]]; then
            chown root:wheel "$KC_FILE"
            chmod 0600       "$KC_FILE"
        fi
    done
    /usr/bin/security unlock-keychain -p "$KEYCHAIN_PW" "$PROJECT_KEYCHAIN"
    # Intentionally NOT calling `security set-keychain-settings
    # -lut 0` here: that invocation returns "passphrase incorrect"
    # on macOS 26.2 Tahoe immediately after a successful
    # create+unlock pair with the same password, independently of
    # the password value or flag set. The helper unlocks the
    # project keychain defensively on every relaunch (via
    # SecKeychainUnlock with the per-install password bytes from
    # /etc/airportitlwm/keychain-password), so leaving auto-lock
    # at the macOS default is correct for helper behavior.
    # Add to the system keychain search list while preserving
    # the existing order.
    EXISTING=$(/usr/bin/security list-keychains -d system | tr -d '"' | xargs)
    /usr/bin/security list-keychains -d system -s "$PROJECT_KEYCHAIN" $EXISTING
    echo "    created $PROJECT_KEYCHAIN (mode 0600 root:wheel"
    echo "    enforced, added to system keychain search list;"
    echo "    auto-lock left at macOS default -- helper unlocks"
    echo "    defensively on every relaunch)."
else
    # Re-assert the enforced mode/owner on every install so a
    # historical file with a more permissive mode is corrected
    # without disturbing operator-added PSK items.
    for KC_FILE in "$PROJECT_KEYCHAIN" "${PROJECT_KEYCHAIN}-db"; do
        if [[ -e "$KC_FILE" ]]; then
            chown root:wheel "$KC_FILE"
            chmod 0600       "$KC_FILE"
        fi
    done
    echo "    keeping existing $PROJECT_KEYCHAIN (operator data;"
    echo "    mode 0600 root:wheel re-enforced)."
fi
unset KEYCHAIN_PW

echo "[5/5] Loading LaunchDaemon..."
# Do NOT swallow launchctl errors silently: the helper-side
# credential-acquisition contract is useless if the LaunchDaemon
# never loads. Order is enable -> bootstrap -> kickstart:
# (1) `launchctl enable` first because the matching `launchctl
#     disable` invocation in uninstall.sh records "disabled" in
#     launchd's persistent override store; on a subsequent
#     reinstall, `launchctl bootstrap` returns `Bootstrap failed:
#     5: Input/output error` for a label that is in the disabled
#     override state. `enable` clears the override and is
#     idempotent when the label is not currently disabled.
# (2) `launchctl bootstrap` loads the plist; if the service is
#     already bootstrapped (label active), `bootstrap` returns
#     `service already loaded` / `Bootstrap failed: 17`; we
#     bootout-then-rebootstrap in that case.
# (3) `launchctl kickstart -k` force-restarts the (now-loaded
#     and enabled) service so the helper picks up the freshly
#     installed binary even when launchd would otherwise wait
#     for the throttle interval.
LAUNCHCTL_ERR="$(mktemp -t airportitlwm_launchctl_err.XXXXXX)"
trap "rm -f \"$LAUNCHCTL_ERR\"" EXIT
# (1) Re-enable any persistent disable override left by
# `uninstall.sh launchctl disable ...`. `enable` succeeds (rc=0)
# whether the label was previously enabled or disabled.
if ! launchctl enable "system/$LABEL" 2>"$LAUNCHCTL_ERR"; then
    echo "    enable failed: $(cat "$LAUNCHCTL_ERR")" >&2
    exit 1
fi
# (2) Bootstrap the plist. On "already loaded" / "Bootstrap
# failed: 17", bootout and retry once.
if ! launchctl bootstrap system "$PLIST_DST" 2>"$LAUNCHCTL_ERR"; then
    BOOTSTRAP_ERR_TEXT="$(cat "$LAUNCHCTL_ERR")"
    if launchctl print "system/$LABEL" >/dev/null 2>&1 ||
       echo "$BOOTSTRAP_ERR_TEXT" | /usr/bin/grep -qE "service already loaded|already bootstrapped|Bootstrap failed: 17"; then
        echo "    LaunchDaemon already bootstrapped; rebootstrapping..."
        launchctl bootout "system/$LABEL" 2>/dev/null || true
        # Short wait for launchd to release the label.
        sleep 1
        if ! launchctl bootstrap system "$PLIST_DST" 2>"$LAUNCHCTL_ERR"; then
            echo "    bootstrap (after bootout) failed: $(cat "$LAUNCHCTL_ERR")" >&2
            exit 1
        fi
    else
        echo "    bootstrap failed: $BOOTSTRAP_ERR_TEXT" >&2
        exit 1
    fi
fi
# (3) Force-restart the service.
if ! launchctl kickstart -k "system/$LABEL" 2>"$LAUNCHCTL_ERR"; then
    echo "    kickstart failed: $(cat "$LAUNCHCTL_ERR")" >&2
    exit 1
fi
# Bounded verification poll: launchctl print may take a moment
# to reflect the new service. Fail loud if the service still
# isn't registered after the bound.
LOAD_VERIFIED=NO
for i in 1 2 3 4 5; do
    if launchctl print "system/$LABEL" >/dev/null 2>&1; then
        LOAD_VERIFIED=YES
        echo "    LaunchDaemon system/$LABEL is registered (verified after ${i}s)."
        break
    fi
    sleep 1
done
if [[ "$LOAD_VERIFIED" != "YES" ]]; then
    echo "    LaunchDaemon system/$LABEL is NOT registered after 5s of polling;" >&2
    echo "    install failed at step [5/5]. Inspect: launchctl print-disabled system | grep $LABEL" >&2
    exit 1
fi
rm -f "$LAUNCHCTL_ERR"
trap - EXIT

echo
echo "Done. The PLTI PMK producer daemon is now running as a"
echo "KeepAlive LaunchDaemon. It stays resident, pre-unlocks the"
echo "project keychain, and retries IOServiceOpen('PLTI') until the"
echo "AirportItlwm kext service is published."
echo
echo "Logs: sudo log show --last 1m --predicate 'process == \"AirportItlwmAgent\"'"
echo
echo "Operator action required before the helper can deliver a PMK:"
echo "the project keychain may be locked at the macOS default"
echo "auto-lock policy (install.sh does NOT call"
echo "set-keychain-settings on Tahoe 26.2; see the install.sh"
echo "header comment). Run BOTH steps below, in order, for each"
echo "target SSID:"
echo
echo "    sudo security unlock-keychain \\"
echo "        -p \"\$(sudo /bin/cat /etc/airportitlwm/keychain-password)\" \\"
echo "        $PROJECT_KEYCHAIN"
echo "    sudo security add-generic-password \\"
echo "        -s \"AirportItlwm WiFi PSK\" \\"
echo "        -a \"<SSID>\" \\"
echo "        -w \"<WPA2 passphrase>\" \\"
echo "        -A \\"
echo "        $PROJECT_KEYCHAIN"
echo
echo "Without the unlock step, add-generic-password returns"
echo "OSStatus -25293 / CSSMERR_DL_OPERATION_AUTH_DENIED on"
echo "Tahoe 26.2 because the locked system-domain keychain"
echo "rejects SecKeychainItemCreateFromContent before consulting"
echo "the per-item access list."
echo
echo "Operational contract: the daemon ONLY runs when a PSK"
echo "association-start edge is published by the kext. Each handled"
echo "target logs WaitAssociationTarget / DeliverPMK structural"
echo "markers (generation and length only — no passphrase, PMK,"
echo "or SSID bytes appear in any log line)."
