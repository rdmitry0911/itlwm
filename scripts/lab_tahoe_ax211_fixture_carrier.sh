#!/bin/bash
# Machine-specific disposable Tahoe lab fixture. Configurations/credentials
# remain in the existing private lab directory; no key is embedded here.
# The original external fixture and runtime comparison are documented in
# docs/TAHOE_MGMT_QUEUE_OWNERSHIP_20260911.md.
set -euo pipefail
test "$(id -u)" = 0
lab_dir=/tmp/aiam-gui3e73-runtime.kiWiTP
fixture_mode=${1:-open}
fixture_label=${2:?unique run label required}
fixture_monitor=${3:-1}
fixture_config=${4:-$lab_dir/hostapd-$fixture_mode.conf}
case "$fixture_config" in
    "$lab_dir/hostapd-$fixture_mode.conf"|/tmp/aiam-roam-policy.Kj1vgE/hostapd-failed-target.conf) ;;
    *) exit 2;;
esac
test -f "$fixture_config"
case "$fixture_monitor" in 0|1) ;; *) exit 2;; esac
case "$fixture_label" in ''|*[!a-zA-Z0-9_-]*) exit 2;; esac
test ! -e "$lab_dir/$fixture_label-hostapd.log"
df -PB1 /home | awk 'NR==2 {exit($4 < 1610612736)}'
case "$fixture_mode" in open|wpa2|wpa3) ;; *) exit 2 ;; esac
test ! -e /sys/class/net/uif3ap
test ! -e /sys/class/net/uif3mon
test -d /sys/class/net/wlp0s20f3
test "$(readlink -f /sys/bus/pci/devices/0000:25:00.0/driver)" = /sys/bus/pci/drivers/vfio-pci
ip -4 route show default | head -1 | grep -q 'dev enx1cbfce6c92ea'
test -z "$(ip -4 route show 192.168.73.0/24)"
fixture_ap_pid=
fixture_dhcp_pid=
fixture_ap_created=0
fixture_monitor_created=0
fixture_sta_unmanaged=0
cleanup() {
    set +e
    if [ -n "$fixture_ap_pid" ] && kill -0 "$fixture_ap_pid" 2>/dev/null; then
        kill -TERM "$fixture_ap_pid"
        wait "$fixture_ap_pid"
    fi
    if [ -n "$fixture_dhcp_pid" ] && kill -0 "$fixture_dhcp_pid" 2>/dev/null; then
        kill -TERM "$fixture_dhcp_pid"
        wait "$fixture_dhcp_pid"
    fi
    if [ "$fixture_monitor_created" = 1 ] && [ -d /sys/class/net/uif3mon ]; then
        ip link set uif3mon down
        iw dev uif3mon del
    fi
    if [ "$fixture_ap_created" = 1 ] && [ -d /sys/class/net/uif3ap ]; then
        ip link set uif3ap down
        iw dev uif3ap del
    fi
    if [ "$fixture_sta_unmanaged" = 1 ]; then
        nmcli device set wlp0s20f3 managed yes
        # Re-management is asynchronous; con up can otherwise race the
        # unavailable -> disconnected transition and fail immediately.
        for fixture_restore_try in $(seq 1 40); do
            fixture_restore_state=$(nmcli -g GENERAL.STATE device show wlp0s20f3)
            case "$fixture_restore_state" in
                '10 (unmanaged)'|'20 (unavailable)') sleep 0.25 ;;
                *) break ;;
            esac
        done
    fi
    fixture_restore_result=0
    nmcli -w 35 con up uuid bbed72a6-b9cb-4170-b207-b3a53998b32e ifname wlp0s20f3 || fixture_restore_result=$?
    printf 'FIXTURE_RESTORE_RESULT=%s\n' "$fixture_restore_result"
    date -u
    printf 'FIXTURE_TERMINAL\n'
    ip -4 route show default
}
trap cleanup EXIT
trap 'exit 130' INT TERM
fixture_initial_sta_state=$(nmcli -g GENERAL.STATE device show wlp0s20f3)
case "$fixture_initial_sta_state" in
    '100 (connected)') nmcli -w 10 device disconnect wlp0s20f3 ;;
    '30 (disconnected)') ;;
    *) printf 'Unexpected fixture STA state: %s\n' "$fixture_initial_sta_state"; exit 2 ;;
esac
# This STA shares the AX211 radio with uif3ap. Disconnected but managed
# NetworkManager still scans it, interrupting AP traffic/probe responses.
# Close that separate producer for the fixture, then restore it in cleanup.
fixture_sta_unmanaged=1
nmcli device set wlp0s20f3 managed no
fixture_sta_exclusive=0
for fixture_try in $(seq 1 30); do
    fixture_sta_state=$(nmcli -g GENERAL.STATE device show wlp0s20f3)
    fixture_sta_supplicant=$(busctl call fi.w1.wpa_supplicant1 \
        /fi/w1/wpa_supplicant1 fi.w1.wpa_supplicant1 \
        GetInterface s wlp0s20f3 2>&1) && fixture_sta_supplicant_result=0 || fixture_sta_supplicant_result=$?
    if [ "$fixture_sta_state" = "10 (unmanaged)" ] &&
       [ "$fixture_sta_supplicant_result" != 0 ] &&
       printf '%s' "$fixture_sta_supplicant" | grep -q 'knows nothing about this interface'; then
        fixture_sta_exclusive=1
        break
    fi
    sleep 0.25
done
test "$fixture_sta_exclusive" = 1
printf 'FIXTURE_STA_EXCLUSIVE_NM_STATE=%s\n' "$fixture_sta_state"
iw dev wlp0s20f3 interface add uif3ap type __ap
fixture_ap_created=1
# Let the newly announced device finish NetworkManager enumeration before
# requesting exclusive fixture ownership. The first attempt demonstrated an
# asynchronous supplicant acquisition after an early successful setter.
sleep 1
nmcli device set uif3ap managed no
fixture_unmanaged=0
for fixture_try in $(seq 1 30); do
    fixture_nm_state=$(nmcli -g GENERAL.STATE device show uif3ap)
    fixture_supplicant=$(busctl call fi.w1.wpa_supplicant1 \
        /fi/w1/wpa_supplicant1 fi.w1.wpa_supplicant1 \
        GetInterface s uif3ap 2>&1) && fixture_supplicant_result=0 || fixture_supplicant_result=$?
    if [ "$fixture_nm_state" = "10 (unmanaged)" ] &&
       [ "$fixture_supplicant_result" != 0 ] &&
       printf '%s' "$fixture_supplicant" | grep -q 'knows nothing about this interface'; then
        fixture_unmanaged=1
        break
    fi
    sleep 0.25
done
test "$fixture_unmanaged" = 1
printf 'FIXTURE_EXCLUSIVE_NM_STATE=%s\n' "$fixture_nm_state"
ip addr add 192.168.73.1/24 dev uif3ap
hostapd -dd "$fixture_config" >"$lab_dir/$fixture_label-hostapd.log" 2>&1 &
fixture_ap_pid=$!
fixture_ready=0
for fixture_try in $(seq 1 25); do
    kill -0 "$fixture_ap_pid"
    if hostapd_cli -p "$lab_dir/ctrl" -i uif3ap status 2>/dev/null | grep -q '^state=ENABLED$'; then
        fixture_ready=1
        break
    fi
    sleep 1
done
test "$fixture_ready" = 1
if [ "$fixture_monitor" = 1 ]; then
    iw dev uif3ap interface add uif3mon type monitor
    fixture_monitor_created=1
    ip link set uif3mon up
fi
dnsmasq --no-daemon --conf-file="$lab_dir/dnsmasq.conf" >"$lab_dir/$fixture_label-dnsmasq.log" 2>&1 &
fixture_dhcp_pid=$!
sleep 1
kill -0 "$fixture_dhcp_pid"
date -u
printf 'FIXTURE_READY controller=%s hostapd=%s dnsmasq=%s\n' "$$" "$fixture_ap_pid" "$fixture_dhcp_pid"
iw dev uif3ap info
for fixture_second in $(seq 1 720); do
    if [ "$((fixture_second % 10))" = 0 ]; then
        df -PB1 /home | awk 'NR==2 {exit($4 < 1073741824)}'
    fi
    kill -0 "$fixture_ap_pid"
    kill -0 "$fixture_dhcp_pid"
    sleep 1
done
