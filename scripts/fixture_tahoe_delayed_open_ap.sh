#!/bin/sh

set -eu

if [ "$#" -ne 7 ]; then
    echo "usage: $0 HELPER INTERFACE SSID CHANNEL START_EPOCH HOLD_SECONDS ADDRESS" >&2
    exit 64
fi

helper=$1
interface=$2
ssid=$3
channel=$4
start_epoch=$5
hold_seconds=$6
address=$7

while [ "$(date +%s)" -lt "$start_epoch" ]; do
    sleep 2 || true
done

"$helper" "$interface" --start "$ssid" open "$channel" "" "$hold_seconds" &
helper_pid=$!

cleanup()
{
    "$helper" "$interface" --stop >/dev/null 2>&1 || true
    kill "$helper_pid" 2>/dev/null || true
    wait "$helper_pid" 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM

attempt=0
while [ "$attempt" -lt 30 ]; do
    if ifconfig ap1 2>/dev/null | grep -q 'status: active'; then
        sudo ifconfig ap1 inet "$address" netmask 255.255.255.0 up
        echo "post-wake AP active: ssid=$ssid channel=$channel address=$address"
        wait "$helper_pid"
        trap - EXIT HUP INT TERM
        exit 0
    fi
    attempt=$((attempt + 1))
    sleep 1
done

echo "post-wake AP did not become active" >&2
exit 1
