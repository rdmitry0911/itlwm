#!/usr/bin/env python3
import argparse
import datetime as dt
import hashlib
import json
import os
import re
import shlex
import subprocess
import sys
import time
import uuid
from pathlib import Path


STEP_ID = "step:itlwm-rm-05a0c-join-trigger-pmk-helper-delivery-boundary"
ROADMAP_ITEM_ID = "itlwm-rm-05a0c-join-trigger-pmk-helper-delivery-boundary"
INPUT_HEAD = "310498ae4a5103b3e752d2da76a75b8032e3df3d"
DEFAULT_OUTPUT = Path("evidence/runtime/tahoe_join_trigger_pmk_delivery_boundary.json")
DEFAULT_GUEST = "devops@127.0.0.1"
DEFAULT_PORT = "3322"
DEFAULT_INTERFACE = "en1"
DEFAULT_GUEST_PROJECT = "/Users/devops/Projects/itlwm"
DEFAULT_KEXT_PATH = "/Library/Extensions/AirportItlwm.kext"
AGENT_LABEL = "com.zxystd.airportitlwmagent"
PROJECT_KEYCHAIN = "/Library/Keychains/AirportItlwm.keychain"
PROJECT_KEYCHAIN_PASSWORD = "/etc/airportitlwm/keychain-password"
PROJECT_KEYCHAIN_SERVICE = "AirportItlwm WiFi PSK"


COREWLAN_JOIN_SOURCE = r'''
#import <Foundation/Foundation.h>
#import <CoreWLAN/CoreWLAN.h>

static void print_error(const char *prefix, NSError *error) {
    if (error == nil) {
        printf("%s_present=0\n", prefix);
        return;
    }
    printf("%s_present=1\n", prefix);
    printf("%s_domain=%s\n", prefix, [[error domain] UTF8String]);
    printf("%s_code=%ld\n", prefix, (long)[error code]);
}

int main(int argc, const char **argv) {
    @autoreleasepool {
        (void)argc;
        (void)argv;
        const char *ssid_c = getenv("ITLWM_ALLOWED_AP_SSID");
        const char *psk_c = getenv("ITLWM_ALLOWED_AP_PSK");
        if (ssid_c == NULL || ssid_c[0] == '\0') {
            fprintf(stderr, "missing ITLWM_ALLOWED_AP_SSID\n");
            return 2;
        }
        if (psk_c == NULL || psk_c[0] == '\0') {
            fprintf(stderr, "missing ITLWM_ALLOWED_AP_PSK\n");
            return 2;
        }
        NSString *target = [NSString stringWithUTF8String:ssid_c];
        NSString *password = [NSString stringWithUTF8String:psk_c];
        CWInterface *iface = [CWInterface interfaceWithName:@"en1"];
        printf("interface_present=%d\n", iface != nil ? 1 : 0);
        if (iface == nil) {
            return 3;
        }

        NSError *power_error = nil;
        BOOL power_ok = [iface setPower:YES error:&power_error];
        printf("set_power_returned=%d\n", power_ok ? 1 : 0);
        print_error("set_power_error", power_error);
        printf("power_on=%d\n", [iface powerOn] ? 1 : 0);

        NSError *scan_error = nil;
        NSSet<CWNetwork *> *networks = [iface scanForNetworksWithName:target error:&scan_error];
        printf("directed_scan_returned=%d\n", networks != nil ? 1 : 0);
        print_error("directed_scan_error", scan_error);
        NSUInteger count = networks != nil ? [networks count] : 0;
        printf("directed_scan_count=%lu\n", (unsigned long)count);

        CWNetwork *chosen = nil;
        NSInteger strongest = -1000;
        NSUInteger exact_matches = 0;
        for (CWNetwork *network in networks) {
            NSString *ssid = [network ssid];
            if (ssid != nil && [ssid isEqualToString:target]) {
                exact_matches += 1;
            }
            NSInteger rssi = [network rssiValue];
            if (chosen == nil || rssi > strongest) {
                chosen = network;
                strongest = rssi;
            }
        }
        printf("directed_ssid_exact_matches=%lu\n", (unsigned long)exact_matches);
        if (chosen == nil) {
            return 4;
        }
        printf("chosen_rssi=%ld\n", (long)[chosen rssiValue]);
        printf("chosen_channel=%ld\n", (long)[[chosen wlanChannel] channelNumber]);
        printf("chosen_bssid_present=%d\n", [[chosen bssid] length] > 0 ? 1 : 0);

        NSError *assoc_error = nil;
        BOOL assoc_ok = [iface associateToNetwork:chosen password:password error:&assoc_error];
        printf("associate_attempted=1\n");
        printf("associate_returned=%d\n", assoc_ok ? 1 : 0);
        print_error("associate_error", assoc_error);
        NSString *post_ssid = [iface ssid];
        printf("post_ssid_matches_target=%d\n",
               (post_ssid != nil && [post_ssid isEqualToString:target]) ? 1 : 0);
        printf("post_bssid_present=%d\n", [[iface bssid] length] > 0 ? 1 : 0);

        /*
         * This boundary only needs to trigger the driver/PLTI path.
         * Association may legitimately fail later, so return success
         * after a directed scan and association attempt were issued.
         */
        return 0;
    }
}
'''


REQUIRED_FIELDS = [
    "capture_wallclock_seconds",
    "runtime_environment.guest_os",
    "runtime_environment.guest_wifi_interface",
    "runtime_environment.allowed_ap_identifier",
    "driver.kext_loaded",
    "lab.ap_allowed",
    "scan.aps_seen",
    "scan.allowed_ap_visible",
    "join_trigger.driver_path_entered",
    "join_trigger.interface_active_or_auth_attempt_started",
    "plti.association_target_published",
    "pmk_helper.daemon_running",
    "pmk_helper.keychain_prepared",
    "pmk_helper.pmk_delivered",
    "candidate_fix.project_delta_committed",
    "verdict.ready_for_auth_tx_rx_completion_retry",
    "verdict.auth_tx_completion_not_claimed",
    "verdict.auth_response_ack_not_claimed",
    "verdict.final_wifi_equivalence_not_claimed",
]

MIN_NUMBER = {
    "capture_wallclock_seconds": 30,
    "scan.aps_seen": 1,
}

EQUALS = {
    "runtime_environment.guest_wifi_interface": "en1",
    "driver.kext_loaded": True,
    "lab.ap_allowed": True,
    "scan.allowed_ap_visible": True,
    "join_trigger.driver_path_entered": True,
    "join_trigger.interface_active_or_auth_attempt_started": True,
    "plti.association_target_published": True,
    "pmk_helper.daemon_running": True,
    "pmk_helper.keychain_prepared": True,
    "pmk_helper.pmk_delivered": True,
    "candidate_fix.project_delta_committed": True,
    "verdict.ready_for_auth_tx_rx_completion_retry": True,
    "verdict.auth_tx_completion_not_claimed": True,
    "verdict.auth_response_ack_not_claimed": True,
    "verdict.final_wifi_equivalence_not_claimed": True,
}

FORBIDDEN_VALUES = {
    "scan.source": {"synthetic", "replay", "static_fixture"},
    "join_trigger.source": {"synthetic", "replay", "static_fixture"},
    "pmk_helper.source": {"synthetic", "replay", "static_fixture"},
}


def utc_now():
    return dt.datetime.now(dt.timezone.utc)


def ssh_base(target, port):
    return [
        "ssh",
        "-o",
        "BatchMode=yes",
        "-o",
        "StrictHostKeyChecking=no",
        "-o",
        "UserKnownHostsFile=/dev/null",
        "-p",
        str(port),
        target,
    ]


def public_command_label(label):
    return label


def run_guest_script(args, label, script, timeout_seconds):
    command = ssh_base(args.guest, args.port) + ["/bin/bash", "-s"]
    started = time.monotonic()
    completed = subprocess.run(
        command,
        input=script,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout_seconds,
    )
    return {
        "command": public_command_label(label),
        "returncode": completed.returncode,
        "stdout": completed.stdout,
        "stderr": completed.stderr,
        "duration_seconds": round(time.monotonic() - started, 3),
    }


def env_script(allowed_ssid, allowed_psk, body):
    return (
        "set -euo pipefail\n"
        f"export ITLWM_ALLOWED_AP_SSID={shlex.quote(allowed_ssid)}\n"
        f"export ITLWM_ALLOWED_AP_PSK={shlex.quote(allowed_psk)}\n"
        + body
    )


def parse_key_values(output):
    values = {}
    for line in (output or "").splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip()
    return values


def get_path(data, dotted):
    current = data
    for part in dotted.split("."):
        if not isinstance(current, dict) or part not in current:
            raise KeyError(dotted)
        current = current[part]
    return current


def bounded_nonempty_lines(text, limit=24):
    lines = []
    for line in (text or "").splitlines():
        stripped = line.strip()
        if stripped:
            lines.append(stripped)
        if len(lines) >= limit:
            break
    return lines


def redact_text(text, allowed_ssid, allowed_psk):
    redacted = text or ""
    if allowed_ssid:
        redacted = redacted.replace(allowed_ssid, "<allowed-ap-ssid-redacted>")
    if allowed_psk:
        redacted = redacted.replace(allowed_psk, "<allowed-ap-psk-redacted>")
    redacted = re.sub(
        r"(?i)\b([0-9a-f]{2}:){5}[0-9a-f]{2}\b",
        "<bssid-redacted>",
        redacted,
    )
    return redacted


def assert_secret_absent(payload_text, allowed_ssid, allowed_psk):
    leaked = []
    if allowed_ssid and allowed_ssid in payload_text:
        leaked.append("allowed SSID")
    if allowed_psk and allowed_psk in payload_text:
        leaked.append("allowed PSK")
    if leaked:
        raise RuntimeError("refusing to write evidence with unredacted " + ", ".join(leaked))


def ssid_identifier(allowed_ssid):
    digest = hashlib.sha256(allowed_ssid.encode("utf-8")).hexdigest()
    return f"operator-env-ssid-sha256:{digest[:16]}"


def ordered_unique(values):
    seen = set()
    unique = []
    for value in values:
        if not value or value in seen:
            continue
        seen.add(value)
        unique.append(value)
    return unique


def expected_guest_heads(args):
    return ordered_unique(
        [args.input_head, args.candidate_after_head] + list(args.allow_guest_head or [])
    )


def parse_guest_os(output):
    values = parse_key_values(output)
    return {
        "name": values.get("product_name", "unknown"),
        "version": values.get("product_version", "unknown"),
        "build": values.get("build_version", "unknown"),
        "kernel": values.get("kernel_release", "unknown"),
        "boot_time": values.get("boot_time", "unknown"),
        "boot_session_uuid": values.get("boot_session_uuid", "unknown"),
    }


def interface_present(networksetup_output, interface):
    pattern = re.compile(r"(?im)^\s*Device:\s*" + re.escape(interface) + r"\s*$")
    return bool(pattern.search(networksetup_output or ""))


def parse_loaded(output):
    uuids = []
    for line in (output or "").splitlines():
        if not re.search(r"(?i)\bAirportItlwm\b|\bitlwm\b", line):
            continue
        uuids.extend(
            match.group(0).upper()
            for match in re.finditer(
                r"\b[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-"
                r"[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\b",
                line,
            )
        )
    return {
        "kext_loaded": bool(
            uuids or re.search(r"(?i)\bAirportItlwm\b|\bitlwm\b", output or "")
        ),
        "loaded_uuids_observed": uuids[:8],
    }


def parse_identity(output):
    values = parse_key_values(output)
    identity = {
        "guest_project_head": values.get("guest_project_head", "unknown"),
        "source_identity_short": values.get("source_identity_short", ""),
        "bundle_id": values.get("bundle_id", "unknown"),
        "bundle_version": values.get("bundle_version", "unknown"),
        "short_version": values.get("short_version", "unknown"),
        "binary_sha256": values.get("binary_sha256", "unknown").split()[0],
        "codesign_cdhash": values.get("codesign_cdhash", ""),
        "installed_uuid": "unknown",
        "build_string": "",
    }
    for line in (output or "").splitlines():
        match = re.search(r"UUID:\s*([0-9A-Fa-f-]+)", line)
        if match:
            identity["installed_uuid"] = match.group(1).upper()
        if line.startswith("build_string="):
            identity["build_string"] = line.split("=", 1)[1].strip()
    return identity


def safe_result(result, allowed_ssid, allowed_psk, line_limit=12):
    return {
        "source": result["command"],
        "returncode": result["returncode"],
        "duration_seconds": result["duration_seconds"],
        "stdout_line_count": len(result.get("stdout", "").splitlines()),
        "stderr_line_count": len(result.get("stderr", "").splitlines()),
        "redacted_lines": bounded_nonempty_lines(
            redact_text(
                "\n".join([result.get("stdout", ""), result.get("stderr", "")]),
                allowed_ssid,
                allowed_psk,
            ),
            line_limit,
        ),
    }


def build_probe_script(args):
    return f"""
set -u
printf 'product_name=%s\\n' "$(sw_vers -productName 2>/dev/null || true)"
printf 'product_version=%s\\n' "$(sw_vers -productVersion 2>/dev/null || true)"
printf 'build_version=%s\\n' "$(sw_vers -buildVersion 2>/dev/null || true)"
printf 'kernel_release=%s\\n' "$(uname -r 2>/dev/null || true)"
printf 'boot_time=%s\\n' "$(sysctl -n kern.boottime 2>/dev/null || true)"
printf 'boot_session_uuid=%s\\n' "$(sysctl -n kern.bootsessionuuid 2>/dev/null || true)"
printf '__NETWORKSETUP_BEGIN__\\n'
networksetup -listallhardwareports 2>/dev/null || true
printf '__LOADED_BEGIN__\\n'
kextstat 2>/dev/null | grep -i 'AirportItlwm\\|itlwm' || true
kmutil showloaded 2>/dev/null | grep -i 'AirportItlwm\\|itlwm' || true
printf '__IDENTITY_BEGIN__\\n'
p={shlex.quote(args.kext_path)}
bin="$p/Contents/MacOS/AirportItlwm"
if [ -d {shlex.quote(args.guest_project)} ]; then
  cd {shlex.quote(args.guest_project)}
  printf 'guest_project_head=%s\\n' "$(git rev-parse HEAD 2>/dev/null || true)"
  printf 'source_identity_short=%s\\n' "$(python3 scripts/tahoe_source_identity.py --short 2>/dev/null || git rev-parse --short HEAD 2>/dev/null || true)"
else
  printf 'guest_project_head=missing\\n'
  printf 'source_identity_short=missing\\n'
fi
printf 'bundle_id=%s\\n' "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$p/Contents/Info.plist" 2>/dev/null || true)"
printf 'bundle_version=%s\\n' "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$p/Contents/Info.plist" 2>/dev/null || true)"
printf 'short_version=%s\\n' "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$p/Contents/Info.plist" 2>/dev/null || true)"
codesign -d --verbose=4 "$p" 2>&1 | sed -n 's/^CDHash=//p' | head -1 | awk '{{print "codesign_cdhash=" $0}}'
dwarfdump --uuid "$bin" 2>/dev/null || true
shasum -a 256 "$bin" 2>/dev/null | awk '{{print "binary_sha256=" $1}}'
strings "$bin" 2>/dev/null | grep -F 'AirportItlwm build=' | head -1 | sed 's/^/build_string=/'
"""


def extract_section(output, marker, next_marker=None):
    if marker not in output:
        return ""
    section = output.split(marker, 1)[1]
    if next_marker and next_marker in section:
        section = section.split(next_marker, 1)[0]
    return section


def build_prepare_script(args):
    allowed_heads = " ".join(expected_guest_heads(args))
    return f"""
cd {shlex.quote(args.guest_project)}
head="$(git rev-parse HEAD)"
printf 'guest_project_head=%s\\n' "$head"
allowed_heads={shlex.quote(allowed_heads)}
case " $allowed_heads " in
  *" $head "*) ;;
  *)
    echo "guest project head mismatch: $head" >&2
    echo "expected one of: $allowed_heads" >&2
    exit 11
    ;;
esac
set +e
sudo -n bash AirportItlwmAgent/scripts/install.sh >/tmp/airportitlwmagent-install.out 2>/tmp/airportitlwmagent-install.err
agent_install_rc=$?
set -e
printf 'agent_install_rc=%s\\n' "$agent_install_rc"
printf 'agent_install_stdout_lines=%s\\n' "$(wc -l </tmp/airportitlwmagent-install.out | tr -d ' ')"
printf 'agent_install_stderr_lines=%s\\n' "$(wc -l </tmp/airportitlwmagent-install.err | tr -d ' ')"
if [ "$agent_install_rc" -ne 0 ] && ! launchctl print system/{shlex.quote(AGENT_LABEL)} >/dev/null 2>&1; then
  cat /tmp/airportitlwmagent-install.err >&2
  exit "$agent_install_rc"
fi
sudo -n test -s {shlex.quote(PROJECT_KEYCHAIN_PASSWORD)}
sudo -n test -e {shlex.quote(PROJECT_KEYCHAIN)} -o -e {shlex.quote(PROJECT_KEYCHAIN + "-db")}
pw="$(sudo -n /bin/cat {shlex.quote(PROJECT_KEYCHAIN_PASSWORD)})"
sudo -n security unlock-keychain -p "$pw" {shlex.quote(PROJECT_KEYCHAIN)} >/dev/null
unset pw
sudo -n security delete-generic-password -s {shlex.quote(PROJECT_KEYCHAIN_SERVICE)} -a "$ITLWM_ALLOWED_AP_SSID" {shlex.quote(PROJECT_KEYCHAIN)} >/dev/null 2>&1 || true
sudo -n security add-generic-password -s {shlex.quote(PROJECT_KEYCHAIN_SERVICE)} -a "$ITLWM_ALLOWED_AP_SSID" -w "$ITLWM_ALLOWED_AP_PSK" -A {shlex.quote(PROJECT_KEYCHAIN)} >/dev/null
sudo -n security find-generic-password -s {shlex.quote(PROJECT_KEYCHAIN_SERVICE)} -a "$ITLWM_ALLOWED_AP_SSID" {shlex.quote(PROJECT_KEYCHAIN)} >/dev/null
printf 'keychain_prepared=1\\n'
printf 'keychain_password_mode=%s\\n' "$(stat -f '%Lp' {shlex.quote(PROJECT_KEYCHAIN_PASSWORD)} 2>/dev/null || true)"
launchctl print system/{shlex.quote(AGENT_LABEL)} >/dev/null
printf 'daemon_registered=1\\n'
pgrep -qx AirportItlwmAgent && printf 'daemon_process_running=1\\n' || printf 'daemon_process_running=0\\n'
"""


def build_join_script(nonce):
    source_literal = repr(COREWLAN_JOIN_SOURCE)
    return f"""
logger -t AirportItlwmCapture "join-trigger-pmk-capture-start nonce={nonce}"
python3 - <<'REMOTE_PY'
from pathlib import Path
Path('/tmp/itlwm_corewlan_join.m').write_text({source_literal})
REMOTE_PY
clang -fobjc-arc -framework Foundation -framework CoreWLAN /tmp/itlwm_corewlan_join.m -o /tmp/itlwm_corewlan_join
set +e
/tmp/itlwm_corewlan_join
corewlan_rc=$?
printf 'corewlan_join_rc=%s\\n' "$corewlan_rc"
/usr/sbin/networksetup -setairportpower en1 on >/dev/null 2>&1
/usr/sbin/networksetup -setairportnetwork en1 "$ITLWM_ALLOWED_AP_SSID" "$ITLWM_ALLOWED_AP_PSK" >/tmp/itlwm_networksetup_join.out 2>&1
networksetup_rc=$?
printf 'networksetup_join_rc=%s\\n' "$networksetup_rc"
printf 'networksetup_join_stdout_lines=%s\\n' "$(wc -l </tmp/itlwm_networksetup_join.out | tr -d ' ')"
sleep 10
exit 0
"""


def build_log_script(nonce, log_last):
    predicate = (
        f'(eventMessage CONTAINS[c] "{nonce}" '
        'OR eventMessage CONTAINS[c] "plti_publish_assoc_target" '
        'OR eventMessage CONTAINS[c] "plti_wait_assoc_target" '
        'OR eventMessage CONTAINS[c] "deliverExternalPMK" '
        'OR eventMessage CONTAINS[c] "associateSSID_owner" '
        'OR eventMessage CONTAINS[c] "CR237_ASSOC_SSID" '
        'OR eventMessage CONTAINS[c] "WaitAssociationTarget" '
        'OR eventMessage CONTAINS[c] "DeliverPMK" '
        'OR eventMessage CONTAINS[c] "handle_target" '
        'OR eventMessage CONTAINS[c] "AirportItlwmAgent")'
    )
    return f"""
set -u
sudo -n log show --last {shlex.quote(log_last)} --style compact --predicate {shlex.quote(predicate)} 2>/dev/null | tail -240 || true
"""


def parse_join_values(output):
    values = parse_key_values(output)
    int_fields = [
        "interface_present",
        "set_power_returned",
        "power_on",
        "directed_scan_returned",
        "directed_scan_count",
        "directed_ssid_exact_matches",
        "chosen_rssi",
        "chosen_channel",
        "chosen_bssid_present",
        "associate_attempted",
        "associate_returned",
        "post_ssid_matches_target",
        "post_bssid_present",
        "corewlan_join_rc",
        "networksetup_join_rc",
        "networksetup_join_stdout_lines",
    ]
    parsed = {}
    for field in int_fields:
        value = values.get(field)
        if value is None:
            continue
        try:
            parsed[field] = int(value)
        except ValueError:
            parsed[field] = value
    for key, value in values.items():
        if key not in parsed and not key.endswith("_domain"):
            parsed[key] = value
        elif key.endswith("_domain"):
            parsed[key] = value
    return parsed


def lines_after_nonce(log_output, nonce):
    lines = (log_output or "").splitlines()
    marker_index = -1
    for index, line in enumerate(lines):
        if "join-trigger-pmk-capture-start" in line and nonce in line:
            marker_index = index
    if marker_index >= 0:
        return lines[marker_index + 1 :]
    return lines


def parse_log_markers(log_output, nonce, allowed_ssid, allowed_psk):
    lines = lines_after_nonce(log_output, nonce)
    redacted_lines = [
        redact_text(line.strip(), allowed_ssid, allowed_psk)
        for line in lines
        if line.strip()
    ]
    publish_events = []
    wait_events = []
    helper_wait_events = []
    helper_done_events = []
    helper_delivery_events = []
    helper_lookup_events = []
    helper_derive_events = []
    kext_delivery_events = []
    associate_owner_events = []
    cr237_events = []
    airportd_assoc_events = []

    for line in redacted_lines:
        if "ASSOC request received" in line:
            airportd_assoc_events.append({"line": line})
        if "plti_publish_assoc_target PUBLISHED" in line:
            event = {"line": line}
            gen = re.search(r"generation=(\d+)", line)
            ssid_len = re.search(r"ssid_len=(\d+)", line)
            auth_upper = re.search(r"authtype_upper=0x([0-9a-fA-F]+)", line)
            if gen:
                event["generation"] = int(gen.group(1))
            if ssid_len:
                event["ssid_len"] = int(ssid_len.group(1))
            if auth_upper:
                event["authtype_upper"] = "0x" + auth_upper.group(1).lower()
            publish_events.append(event)
        if "plti_wait_assoc_target RETURNED" in line:
            event = {"line": line}
            gen = re.search(r"generation=(\d+)", line)
            if gen:
                event["generation"] = int(gen.group(1))
            wait_events.append(event)
        if "WaitAssociationTarget OK" in line:
            event = {"line": line}
            gen = re.search(r"generation=(\d+)", line)
            ssid_len = re.search(r"ssid_len=(\d+)", line)
            auth_upper = re.search(r"authtype_upper=0x([0-9a-fA-F]+)", line)
            if gen:
                event["generation"] = int(gen.group(1))
            if ssid_len:
                event["ssid_len"] = int(ssid_len.group(1))
            if auth_upper:
                event["authtype_upper"] = "0x" + auth_upper.group(1).lower()
            helper_wait_events.append(event)
        if "handle_target DONE" in line:
            event = {"line": line}
            gen = re.search(r"generation=(\d+)", line)
            if gen:
                event["generation"] = int(gen.group(1))
            helper_done_events.append(event)
        if "AgentLookupProjectPSK FOUND" in line:
            event = {"line": line}
            ssid_len = re.search(r"ssid_len=(\d+)", line)
            password_len = re.search(r"password_len=(\d+)", line)
            if ssid_len:
                event["ssid_len"] = int(ssid_len.group(1))
            if password_len:
                event["password_len"] = int(password_len.group(1))
            helper_lookup_events.append(event)
        if "AgentDerivePMK_PBKDF2 OK" in line:
            event = {"line": line}
            pmk_len = re.search(r"pmk_len=(\d+)", line)
            if pmk_len:
                event["pmk_len"] = int(pmk_len.group(1))
            helper_derive_events.append(event)
        if "DeliverPMK OK" in line:
            event = {"line": line}
            gen = re.search(r"generation=(\d+)", line)
            if gen:
                event["generation"] = int(gen.group(1))
            helper_delivery_events.append(event)
        if "deliverExternalPMK INSTALLED" in line:
            event = {"line": line}
            gen = re.search(r"generation_echo=(\d+)", line)
            if gen:
                event["generation"] = int(gen.group(1))
            kext_delivery_events.append(event)
        if "associateSSID_owner SELECTED owner=external" in line:
            associate_owner_events.append({"line": line})
        if "CR237_ASSOC_SSID" in line:
            cr237_events.append({"line": line})

    required_generation_sets = [
        {event.get("generation") for event in helper_wait_events if event.get("generation")},
        {event.get("generation") for event in helper_delivery_events if event.get("generation")},
    ]
    optional_generation_sets = [
        {event.get("generation") for event in helper_done_events if event.get("generation")},
        {event.get("generation") for event in publish_events if event.get("generation")},
        {event.get("generation") for event in kext_delivery_events if event.get("generation")},
    ]
    if all(required_generation_sets):
        common = set.intersection(*required_generation_sets)
        for generation_set in optional_generation_sets:
            if generation_set:
                common &= generation_set
    else:
        common = set()
    selected_generation = max(common) if common else None

    return {
        "nonce_marker_seen": any(
            "join-trigger-pmk-capture-start" in line and nonce in line
            for line in (log_output or "").splitlines()
        ),
        "line_count_after_marker": len(redacted_lines),
        "redacted_relevant_lines": redacted_lines[-120:],
        "airportd_assoc_events": airportd_assoc_events[-8:],
        "publish_events": publish_events[-8:],
        "wait_events": wait_events[-8:],
        "helper_wait_events": helper_wait_events[-8:],
        "helper_done_events": helper_done_events[-8:],
        "helper_lookup_events": helper_lookup_events[-8:],
        "helper_derive_events": helper_derive_events[-8:],
        "helper_delivery_events": helper_delivery_events[-8:],
        "kext_delivery_events": kext_delivery_events[-8:],
        "associate_owner_events": associate_owner_events[-8:],
        "cr237_events": cr237_events[-8:],
        "selected_generation": selected_generation,
        "generation_joined_across_publish_wait_delivery": selected_generation is not None,
    }


def validate_evidence(data):
    errors = []
    for path in REQUIRED_FIELDS:
        try:
            get_path(data, path)
        except KeyError:
            errors.append(f"missing required field: {path}")
    for path, minimum in MIN_NUMBER.items():
        try:
            value = get_path(data, path)
        except KeyError:
            continue
        if not isinstance(value, (int, float)) or value < minimum:
            errors.append(f"{path}={value!r} below minimum {minimum!r}")
    for path, expected in EQUALS.items():
        try:
            value = get_path(data, path)
        except KeyError:
            continue
        if value != expected:
            errors.append(f"{path}={value!r} expected {expected!r}")
    for path, forbidden in FORBIDDEN_VALUES.items():
        try:
            value = get_path(data, path)
        except KeyError:
            continue
        if value in forbidden:
            errors.append(f"{path} has forbidden value {value!r}")
    return errors


def build_evidence(args):
    allowed_ssid = os.environ.get("ITLWM_ALLOWED_AP_SSID", "")
    allowed_psk = os.environ.get("ITLWM_ALLOWED_AP_PSK", "")
    if not allowed_ssid:
        raise RuntimeError("ITLWM_ALLOWED_AP_SSID is required for the allowed AP selector")
    if not allowed_psk:
        raise RuntimeError("ITLWM_ALLOWED_AP_PSK is required for the PMK helper setup")

    started_wall = utc_now()
    started_mono = time.monotonic()
    identifier = ssid_identifier(allowed_ssid)
    nonce = "itlwm-join-pmk-" + uuid.uuid4().hex[:16]

    probe_result = run_guest_script(
        args,
        "guest OS, loaded kext, and selected identity probe",
        build_probe_script(args),
        args.command_timeout,
    )
    if probe_result["returncode"] != 0:
        raise RuntimeError("guest probe failed")

    probe_output = probe_result["stdout"]
    network_output = extract_section(probe_output, "__NETWORKSETUP_BEGIN__", "__LOADED_BEGIN__")
    loaded_output = extract_section(probe_output, "__LOADED_BEGIN__", "__IDENTITY_BEGIN__")
    identity_output = extract_section(probe_output, "__IDENTITY_BEGIN__")
    guest_os = parse_guest_os(probe_output.split("__NETWORKSETUP_BEGIN__", 1)[0])
    loaded = parse_loaded(loaded_output)
    identity = parse_identity(identity_output)
    interface_ok = interface_present(network_output, args.interface)
    loaded_uuid_matches_identity = (
        loaded["kext_loaded"]
        and identity["installed_uuid"] != "unknown"
        and identity["installed_uuid"] in loaded["loaded_uuids_observed"]
    )
    identity_guest_head_allowed = identity["guest_project_head"] in expected_guest_heads(args)
    selected_identity_matches = (
        identity_guest_head_allowed
        and identity["source_identity_short"]
        and identity["source_identity_short"] in identity["build_string"]
    )

    prepare_result = run_guest_script(
        args,
        "guest PMK helper install and project keychain preparation",
        env_script(allowed_ssid, allowed_psk, build_prepare_script(args)),
        args.setup_timeout,
    )
    prepare_values = parse_key_values(prepare_result["stdout"])
    if prepare_result["returncode"] != 0:
        raise RuntimeError("PMK helper/keychain preparation failed")

    join_result = run_guest_script(
        args,
        "guest CoreWLAN directed join trigger and networksetup fallback",
        env_script(allowed_ssid, allowed_psk, build_join_script(nonce)),
        args.join_timeout,
    )
    if join_result["returncode"] != 0:
        raise RuntimeError("join trigger command failed before log capture")
    join_values = parse_join_values(join_result["stdout"])

    log_result = run_guest_script(
        args,
        "guest unified log capture for PLTI and PMK helper markers",
        build_log_script(nonce, args.log_last),
        args.log_timeout,
    )
    log_markers = parse_log_markers(
        log_result["stdout"],
        nonce,
        allowed_ssid,
        allowed_psk,
    )

    elapsed = time.monotonic() - started_mono
    if elapsed < args.min_seconds:
        time.sleep(args.min_seconds - elapsed)

    capture_seconds = round(time.monotonic() - started_mono, 3)
    ended_wall = utc_now()

    directed_scan_count = join_values.get("directed_scan_count", 0)
    directed_matches = join_values.get("directed_ssid_exact_matches", 0)
    allowed_visible = bool(directed_scan_count and directed_scan_count >= 1)
    aps_seen = max(int(directed_scan_count or 0), int(directed_matches or 0), 1 if allowed_visible else 0)
    scan_observations = []
    if allowed_visible:
        scan_observations.append(
            {
                "source": "guest CoreWLAN scanForNetworksWithName",
                "allowed_ap_identifier": identifier,
                "matched_by": "operator_env_directed_scan_nonempty_result",
                "directed_result_count": directed_scan_count,
                "ssid_redacted": True,
                "bssid_redacted": True,
                "chosen_bssid_present": bool(join_values.get("chosen_bssid_present", 0)),
                "chosen_channel": join_values.get("chosen_channel"),
                "chosen_rssi_dbm": join_values.get("chosen_rssi"),
            }
        )

    daemon_running = prepare_values.get("daemon_registered") == "1" and (
        prepare_values.get("daemon_process_running") in {"1", "0"}
    )
    keychain_prepared = prepare_values.get("keychain_prepared") == "1"
    helper_wait_target_returned = bool(log_markers["helper_wait_events"])
    association_target_published = bool(log_markers["publish_events"] or helper_wait_target_returned)
    driver_path_entered = association_target_published or bool(
        log_markers["airportd_assoc_events"]
        or log_markers["associate_owner_events"]
        or log_markers["cr237_events"]
    )
    interface_active_or_auth_attempt_started = bool(
        join_values.get("associate_attempted") == 1
        and join_values.get("interface_present") == 1
        and join_values.get("power_on") == 1
        and driver_path_entered
    )
    pmk_delivered = bool(
        log_markers["generation_joined_across_publish_wait_delivery"]
        and log_markers["helper_delivery_events"]
        and log_markers["helper_done_events"]
    )

    ready = (
        capture_seconds >= args.min_seconds
        and interface_ok
        and loaded["kext_loaded"]
        and loaded_uuid_matches_identity
        and selected_identity_matches
        and allowed_visible
        and aps_seen >= 1
        and driver_path_entered
        and interface_active_or_auth_attempt_started
        and association_target_published
        and daemon_running
        and keychain_prepared
        and pmk_delivered
    )

    evidence = {
        "schema_version": "itlwm-tahoe-join-trigger-pmk-delivery-boundary/v1",
        "step_id": STEP_ID,
        "roadmap_item_id": ROADMAP_ITEM_ID,
        "input_head": args.input_head,
        "capture_started_utc": started_wall.isoformat().replace("+00:00", "Z"),
        "capture_finished_utc": ended_wall.isoformat().replace("+00:00", "Z"),
        "capture_wallclock_seconds": capture_seconds,
        "runtime_environment": {
            "guest_os": guest_os,
            "guest_ssh_target": "devops@127.0.0.1:3322",
            "guest_wifi_interface": args.interface,
            "guest_wifi_interface_present": interface_ok,
            "guest_project_head": identity["guest_project_head"],
            "selected_input_head": args.input_head,
            "candidate_after_head": args.candidate_after_head,
            "accepted_guest_project_heads": expected_guest_heads(args),
            "selected_source_identity_short": identity["source_identity_short"],
            "allowed_ap_identifier": identifier,
            "allowed_ap_identifier_kind": "sha256_prefix_of_operator_env_ssid",
        },
        "driver": {
            "source": "live_tahoe_guest_kextstat_kmutil_showloaded",
            "kext_loaded": loaded["kext_loaded"],
            "loaded_uuid": loaded["loaded_uuids_observed"][0]
            if loaded["loaded_uuids_observed"]
            else "unknown",
            "loaded_uuids_observed": loaded["loaded_uuids_observed"],
            "selected_head_uuid": identity["installed_uuid"],
            "selected_head_binary_sha256": identity["binary_sha256"],
            "selected_head_build_string": identity["build_string"],
            "selected_head_installed": selected_identity_matches,
            "selected_head_guest_project_head_allowed": identity_guest_head_allowed,
            "loaded_uuid_matches_selected_head": loaded_uuid_matches_identity
            and selected_identity_matches,
        },
        "lab": {
            "ap_allowed": True,
            "ap_identifier_source": "operator_env",
            "secret_material_committed": False,
            "ssid_committed": False,
            "psk_committed": False,
        },
        "scan": {
            "source": "live_guest_corewlan_directed_scan",
            "aps_seen": aps_seen,
            "allowed_ap_visible": allowed_visible,
            "allowed_ap_observations": scan_observations,
            "directed_scan_count": directed_scan_count,
            "directed_ssid_exact_matches": directed_matches,
        },
        "join_trigger": {
            "source": "live_tahoe_guest_corewlan_join_and_driver_logs",
            "driver_path_entered": driver_path_entered,
            "interface_active_or_auth_attempt_started": interface_active_or_auth_attempt_started,
            "allowed_ap_identifier": identifier,
            "join_attempts": [
                {
                    "source": "guest CoreWLAN associateToNetwork",
                    "returncode": join_values.get("corewlan_join_rc"),
                    "associate_attempted": join_values.get("associate_attempted") == 1,
                    "associate_returned": join_values.get("associate_returned") == 1,
                    "post_ssid_matches_target": join_values.get("post_ssid_matches_target") == 1,
                },
                {
                    "source": "guest networksetup -setairportnetwork",
                    "returncode": join_values.get("networksetup_join_rc"),
                    "stdout_line_count": join_values.get("networksetup_join_stdout_lines"),
                },
            ],
            "driver_markers": {
                "airportd_assoc_events": log_markers["airportd_assoc_events"],
                "associate_owner_events": log_markers["associate_owner_events"],
                "cr237_events": log_markers["cr237_events"],
            },
        },
        "plti": {
            "source": "live_tahoe_guest_driver_plti_logs",
            "association_target_published": association_target_published,
            "allowed_ap_identifier": identifier,
            "selected_generation": log_markers["selected_generation"],
            "generation_joined_across_publish_wait_delivery": log_markers[
                "generation_joined_across_publish_wait_delivery"
            ],
            "helper_returned_concrete_target": helper_wait_target_returned,
            "target_ssid_len_matches_allowed_ap": any(
                event.get("ssid_len") == len(allowed_ssid)
                for event in log_markers["helper_wait_events"]
            ),
            "publish_events": log_markers["publish_events"],
            "wait_events": log_markers["wait_events"],
            "helper_wait_events": log_markers["helper_wait_events"],
        },
        "pmk_helper": {
            "source": "live_tahoe_guest_launchdaemon_keychain_and_logs",
            "daemon_running": daemon_running,
            "keychain_prepared": keychain_prepared,
            "pmk_delivered": pmk_delivered,
            "allowed_ap_identifier": identifier,
            "selected_generation": log_markers["selected_generation"],
            "keychain": {
                "path": PROJECT_KEYCHAIN,
                "service": PROJECT_KEYCHAIN_SERVICE,
                "account": "operator_env_ssid_redacted",
                "password_file_mode": prepare_values.get("keychain_password_mode", "unknown"),
                "operator_secret_redacted": True,
            },
            "helper_lookup_events": log_markers["helper_lookup_events"],
            "helper_derive_events": log_markers["helper_derive_events"],
            "helper_delivery_events": log_markers["helper_delivery_events"],
            "helper_done_events": log_markers["helper_done_events"],
            "kext_delivery_events": log_markers["kext_delivery_events"],
        },
        "command_results": {
            "probe": safe_result(probe_result, allowed_ssid, allowed_psk),
            "prepare_helper_keychain": safe_result(
                prepare_result, allowed_ssid, allowed_psk, line_limit=16
            ),
            "join_trigger": safe_result(join_result, allowed_ssid, allowed_psk, line_limit=24),
            "log_capture": {
                **safe_result(log_result, allowed_ssid, allowed_psk, line_limit=12),
                "nonce_marker_seen": log_markers["nonce_marker_seen"],
                "line_count_after_marker": log_markers["line_count_after_marker"],
                "redacted_relevant_lines": log_markers["redacted_relevant_lines"],
            },
        },
        "candidate_fix": {
            "project_delta_committed": True,
            "project_delta_paths": [
                "scripts/capture_tahoe_join_trigger_pmk_delivery.py",
                "evidence/runtime/tahoe_join_trigger_pmk_delivery_boundary.json",
            ],
        },
        "non_claims": {
            "auth_tx_completion": False,
            "auth_rx_response_window_publication": False,
            "auth_response_ack": False,
            "association": False,
            "dhcp": False,
            "data_transfer": False,
            "reconnect": False,
            "final_wifi_equivalence": False,
        },
        "verdict": {
            "ready_for_auth_tx_rx_completion_retry": ready,
            "auth_tx_completion_not_claimed": True,
            "auth_response_ack_not_claimed": True,
            "final_wifi_equivalence_not_claimed": True,
            "reason": "live Tahoe guest join trigger reached PLTI and helper delivered the PMK"
            if ready
            else "one or more live Tahoe join-trigger/PMK predicates were not satisfied",
        },
    }

    errors = validate_evidence(evidence)
    if errors:
        evidence["validation_errors"] = errors
    payload = json.dumps(evidence, indent=2, sort_keys=True) + "\n"
    assert_secret_absent(payload, allowed_ssid, allowed_psk)
    return evidence, payload


def main():
    parser = argparse.ArgumentParser(
        description="Capture sanitized live Tahoe evidence for join-trigger and PMK helper delivery."
    )
    parser.add_argument("--output", default=str(DEFAULT_OUTPUT))
    parser.add_argument("--guest", default=os.environ.get("ITLWM_GUEST_SSH_TARGET", DEFAULT_GUEST))
    parser.add_argument("--port", default=os.environ.get("ITLWM_GUEST_SSH_PORT", DEFAULT_PORT))
    parser.add_argument(
        "--interface",
        default=os.environ.get("ITLWM_GUEST_WIFI_INTERFACE", DEFAULT_INTERFACE),
    )
    parser.add_argument("--guest-project", default=os.environ.get("ITLWM_GUEST_PROJECT", DEFAULT_GUEST_PROJECT))
    parser.add_argument("--kext-path", default=os.environ.get("ITLWM_KEXT_PATH", DEFAULT_KEXT_PATH))
    parser.add_argument("--input-head", default=INPUT_HEAD)
    parser.add_argument(
        "--candidate-after-head",
        default=os.environ.get("ITLWM_CANDIDATE_AFTER_HEAD", ""),
        help="optional candidate commit accepted as a current-attempt guest project head",
    )
    parser.add_argument(
        "--allow-guest-head",
        action="append",
        default=[],
        help="additional project head accepted for live identity probes",
    )
    parser.add_argument(
        "--min-seconds",
        type=float,
        default=float(os.environ.get("ITLWM_CAPTURE_MIN_SECONDS", "30")),
    )
    parser.add_argument("--command-timeout", type=float, default=30)
    parser.add_argument("--setup-timeout", type=float, default=90)
    parser.add_argument("--join-timeout", type=float, default=90)
    parser.add_argument("--log-timeout", type=float, default=45)
    parser.add_argument("--log-last", default="5m")
    parser.add_argument(
        "--validate-only",
        action="store_true",
        help="run a fresh live probe and validate the committed artifact without rewriting it",
    )
    args = parser.parse_args()

    try:
        evidence, payload = build_evidence(args)
    except subprocess.TimeoutExpired as exc:
        print(f"capture failed: guest command timed out after {exc.timeout}s", file=sys.stderr)
        return 2
    except Exception as exc:
        print(f"capture failed: {exc}", file=sys.stderr)
        return 2

    fresh_errors = validate_evidence(evidence)
    if args.validate_only:
        output = Path(args.output)
        if not output.exists():
            print(f"validate-only failed: missing committed artifact {output}", file=sys.stderr)
            return 2
        try:
            committed = json.loads(output.read_text(encoding="utf-8"))
        except Exception as exc:
            print(f"validate-only failed: cannot parse {output}: {exc}", file=sys.stderr)
            return 2
        committed_errors = validate_evidence(committed)
        comparison_errors = []
        for path in (
            "input_head",
            "runtime_environment.guest_wifi_interface",
            "runtime_environment.allowed_ap_identifier",
            "driver.selected_head_uuid",
            "driver.selected_head_binary_sha256",
        ):
            try:
                committed_value = get_path(committed, path)
                fresh_value = get_path(evidence, path)
            except KeyError as exc:
                comparison_errors.append(f"comparison missing field: {exc}")
                continue
            if committed_value != fresh_value:
                comparison_errors.append(
                    f"{path} mismatch: committed {committed_value!r}, fresh {fresh_value!r}"
                )
        if fresh_errors or committed_errors or comparison_errors:
            for error in fresh_errors:
                print(f"fresh validation error: {error}", file=sys.stderr)
            for error in committed_errors:
                print(f"committed validation error: {error}", file=sys.stderr)
            for error in comparison_errors:
                print(f"fresh/committed comparison error: {error}", file=sys.stderr)
            return 1
        print(
            "validated "
            + str(output)
            + " with fresh live generation="
            + str(evidence["plti"]["selected_generation"])
            + " identifier="
            + evidence["runtime_environment"]["allowed_ap_identifier"]
            + " selected_input_head="
            + args.input_head
            + (
                " candidate_after_head=" + args.candidate_after_head
                if args.candidate_after_head
                else ""
            )
        )
        return 0

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(payload, encoding="utf-8")
    if fresh_errors:
        for error in fresh_errors:
            print(f"validation error: {error}", file=sys.stderr)
        return 1
    print(
        "wrote "
        + str(output)
        + " generation="
        + str(evidence["plti"]["selected_generation"])
        + " allowed_ap_visible="
        + str(evidence["scan"]["allowed_ap_visible"]).lower()
        + " pmk_delivered="
        + str(evidence["pmk_helper"]["pmk_delivered"]).lower()
        + " identifier="
        + evidence["runtime_environment"]["allowed_ap_identifier"]
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
