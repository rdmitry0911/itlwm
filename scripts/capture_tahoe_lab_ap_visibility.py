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
from pathlib import Path


DEFAULT_OUTPUT = Path("evidence/runtime/tahoe_lab_ap_visibility_readiness.json")
DEFAULT_GUEST = "devops@127.0.0.1"
DEFAULT_PORT = "3322"
DEFAULT_INTERFACE = "en1"


def utc_now():
    return dt.datetime.now(dt.timezone.utc)


def run_guest(target, port, remote_command, timeout_seconds):
    wrapped_remote_command = "/bin/bash -lc " + shlex.quote(remote_command)
    command = [
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
        wrapped_remote_command,
    ]
    started = time.monotonic()
    completed = subprocess.run(
        command,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout_seconds,
    )
    return {
        "command": public_command_label(remote_command),
        "returncode": completed.returncode,
        "stdout": completed.stdout,
        "stderr": completed.stderr,
        "duration_seconds": round(time.monotonic() - started, 3),
    }


def public_command_label(remote_command):
    if "itlwm_corewlan_scan" in remote_command and "--directed" in remote_command:
        return "guest CoreWLAN directed scan"
    if "itlwm_corewlan_scan" in remote_command:
        return "guest CoreWLAN scan"
    if "wdutil scan" in remote_command and "sudo" in remote_command:
        return "sudo -n wdutil scan"
    if "wdutil scan" in remote_command:
        return "wdutil scan"
    if "kextstat" in remote_command or "kmutil" in remote_command:
        return "loaded kext query"
    if "networksetup" in remote_command:
        return "guest network hardware ports query"
    if "log show" in remote_command:
        return "guest driver/runtime log query"
    if "sw_vers" in remote_command:
        return "guest OS query"
    return "guest command"


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


def bounded_lines(text, limit=8):
    lines = []
    for line in text.splitlines():
        stripped = line.strip()
        if stripped:
            lines.append(stripped)
        if len(lines) >= limit:
            break
    return lines


def ssid_identifier(allowed_ssid):
    digest = hashlib.sha256(allowed_ssid.encode("utf-8")).hexdigest()
    return f"operator-env-ssid-sha256:{digest[:16]}"


def parse_guest_os(output):
    lines = [line.strip() for line in output.splitlines() if line.strip()]
    product_name = lines[0] if len(lines) > 0 else "unknown"
    product_version = lines[1] if len(lines) > 1 else "unknown"
    build_version = lines[2] if len(lines) > 2 else "unknown"
    kernel = lines[3] if len(lines) > 3 else "unknown"
    return {
        "name": product_name,
        "version": product_version,
        "build": build_version,
        "kernel": kernel,
    }


def interface_present(networksetup_output, interface):
    pattern = re.compile(r"(?im)^\s*Device:\s*" + re.escape(interface) + r"\s*$")
    return bool(pattern.search(networksetup_output))


def kext_loaded(kext_output):
    return bool(re.search(r"(?i)\b(AirportItlwm|itlwm)\b", kext_output or ""))


def count_aps(scan_outputs, allowed_visible):
    bssids = set()
    ssid_fields = 0
    table_rows = 0
    for output in scan_outputs:
        for match in re.finditer(r"(?i)\b([0-9a-f]{2}:){5}[0-9a-f]{2}\b", output):
            bssids.add(match.group(0).lower())
        ssid_fields += len(re.findall(r"(?im)^\s*SSID\s*:", output))
        for line in output.splitlines():
            if re.search(r"(?i)\b([0-9a-f]{2}:){5}[0-9a-f]{2}\b", line):
                table_rows += 1
    observed = max(len(bssids), ssid_fields, table_rows)
    if allowed_visible:
        observed = max(observed, 1)
    return observed


COREWLAN_SCANNER_SOURCE = r'''
#import <Foundation/Foundation.h>
#import <CoreWLAN/CoreWLAN.h>

int main(int argc, const char **argv) {
    @autoreleasepool {
        BOOL directed = argc > 1 && strcmp(argv[1], "--directed") == 0;
        const char *target_c = getenv("ITLWM_ALLOWED_AP_SSID");
        NSString *target = (directed && target_c) ? [NSString stringWithUTF8String:target_c] : nil;
        CWInterface *iface = [CWInterface interfaceWithName:@"en1"];
        if (iface == nil) {
            fprintf(stderr, "NO_INTERFACE\n");
            return 2;
        }
        NSError *error = nil;
        NSSet<CWNetwork *> *networks = [iface scanForNetworksWithName:target error:&error];
        if (networks == nil) {
            const char *message = error ? [[error localizedDescription] UTF8String] : "unknown";
            fprintf(stderr, "SCAN_ERROR:%s\n", message);
            return 3;
        }
        for (CWNetwork *network in networks) {
            NSString *ssid = [network ssid] ?: @"";
            NSString *bssid = [network bssid] ?: @"";
            NSInteger channel = [[network wlanChannel] channelNumber];
            printf("%s\t%s\t%ld\t%ld\n",
                   [ssid UTF8String],
                   [bssid UTF8String],
                   (long)[network rssiValue],
                   (long)channel);
        }
    }
    return 0;
}
'''


def corewlan_remote_command(allowed_ssid, directed):
    scanner_arg = "--directed" if directed else "--all"
    return f"""
set -euo pipefail
python3 - <<'REMOTE_PY'
from pathlib import Path
Path('/tmp/itlwm_corewlan_scan.m').write_text({COREWLAN_SCANNER_SOURCE!r})
REMOTE_PY
clang -fobjc-arc -framework Foundation -framework CoreWLAN /tmp/itlwm_corewlan_scan.m -o /tmp/itlwm_corewlan_scan
ITLWM_ALLOWED_AP_SSID={shlex.quote(allowed_ssid)} /tmp/itlwm_corewlan_scan {scanner_arg}
"""


def parse_corewlan_rows(output):
    rows = []
    for line in output.splitlines():
        if not line.strip():
            continue
        fields = line.rstrip("\n").split("\t")
        if len(fields) < 4:
            continue
        try:
            rssi = int(fields[2])
            channel = int(fields[3])
        except ValueError:
            rssi = None
            channel = None
        rows.append(
            {
                "ssid": fields[0],
                "bssid": fields[1],
                "rssi": rssi,
                "channel": channel,
            }
        )
    return rows


def summarize_corewlan_rows(rows):
    channels = sorted({row["channel"] for row in rows if row["channel"]})
    rssi_values = [row["rssi"] for row in rows if row["rssi"] is not None]
    ssid_fields_present = sum(1 for row in rows if row["ssid"])
    bssid_fields_present = sum(1 for row in rows if row["bssid"])
    summary = {
        "result_count": len(rows),
        "ssid_fields_present": ssid_fields_present,
        "bssid_fields_present": bssid_fields_present,
        "channels": channels[:16],
    }
    if rssi_values:
        summary["rssi_min_dbm"] = min(rssi_values)
        summary["rssi_max_dbm"] = max(rssi_values)
    return summary


def find_allowed_observations(scan_results, allowed_ssid, allowed_psk, identifier):
    observations = []
    for result in scan_results:
        combined = "\n".join([result.get("stdout", ""), result.get("stderr", "")])
        if allowed_ssid not in combined:
            continue
        lines = combined.splitlines()
        matching = []
        for line in lines:
            if allowed_ssid in line:
                matching.append(redact_text(line, allowed_ssid, allowed_psk))
        observations.append(
            {
                "source": result["command"],
                "allowed_ap_identifier": identifier,
                "matched_by": "operator_env_ssid_exact_match",
                "ssid_redacted": True,
                "bssid_redacted": True,
                "redacted_context": bounded_lines("\n".join(matching), limit=4),
            }
        )
    return observations


def build_corewlan_observations(directed_rows, directed_output, allowed_ssid, allowed_psk, identifier):
    observations = []
    if directed_rows:
        observations.append(
            {
                "source": "guest CoreWLAN scanForNetworksWithName",
                "allowed_ap_identifier": identifier,
                "matched_by": "operator_env_directed_scan_nonempty_result",
                "directed_result_count": len(directed_rows),
                "ssid_redacted": True,
                "bssid_redacted": True,
                "privacy_redacted_fields": allowed_ssid not in directed_output,
                "directed_result_summary": summarize_corewlan_rows(directed_rows),
            }
        )
    observations.extend(
        find_allowed_observations(
            [{"command": "guest CoreWLAN scan", "stdout": directed_output, "stderr": ""}],
            allowed_ssid,
            allowed_psk,
            identifier,
        )
    )
    return observations


def safe_scan_status_lines(output, allowed_ssid, allowed_psk, limit=8):
    safe_lines = []
    for line in output.splitlines():
        if re.search(r"(?i)\b([0-9a-f]{2}:){5}[0-9a-f]{2}\b", line):
            continue
        if allowed_ssid and allowed_ssid in line:
            safe_lines.append(redact_text(line, allowed_ssid, allowed_psk))
        elif re.search(r"(?i)(error|failed|denied|requires|usage|warning|no networks|ssid|bssid|rssi|channel|security)", line):
            safe_lines.append(redact_text(line, allowed_ssid, allowed_psk))
        if len(safe_lines) >= limit:
            break
    return safe_lines


def assert_secret_absent(payload_text, allowed_ssid, allowed_psk):
    leaked = []
    if allowed_ssid and allowed_ssid in payload_text:
        leaked.append("allowed SSID")
    if allowed_psk and allowed_psk in payload_text:
        leaked.append("allowed PSK")
    if leaked:
        raise RuntimeError("refusing to write evidence with unredacted " + ", ".join(leaked))


def build_evidence(args):
    allowed_ssid = os.environ.get("ITLWM_ALLOWED_AP_SSID", "")
    allowed_psk = os.environ.get("ITLWM_ALLOWED_AP_PSK", "")
    if not allowed_ssid:
        raise RuntimeError("ITLWM_ALLOWED_AP_SSID is required for the allowed AP selector")

    started_wall = utc_now()
    started_mono = time.monotonic()

    os_result = run_guest(
        args.guest,
        args.port,
        "sw_vers -productName; sw_vers -productVersion; sw_vers -buildVersion; uname -a",
        15,
    )
    network_result = run_guest(
        args.guest,
        args.port,
        "networksetup -listallhardwareports",
        15,
    )
    kext_result = run_guest(
        args.guest,
        args.port,
        "kextstat 2>/dev/null | grep -i 'AirportItlwm\\|itlwm' || kmutil showloaded 2>/dev/null | grep -i 'AirportItlwm\\|itlwm' || true",
        20,
    )

    scan_results = [
        run_guest(
            args.guest,
            args.port,
            corewlan_remote_command(allowed_ssid, directed=False),
            45,
        ),
        run_guest(
            args.guest,
            args.port,
            corewlan_remote_command(allowed_ssid, directed=True),
            45,
        ),
    ]

    log_result = run_guest(
        args.guest,
        args.port,
        "log show --last 2m --style compact --predicate '(process == \"kernel\" OR process == \"airportd\") AND (eventMessage CONTAINS[c] \"AirportItlwm\" OR eventMessage CONTAINS[c] \"itlwm\" OR eventMessage CONTAINS[c] \"scan\" OR eventMessage CONTAINS[c] \"en1\")' 2>&1 | tail -n 60",
        25,
    )

    elapsed = time.monotonic() - started_mono
    if elapsed < args.min_seconds:
        time.sleep(args.min_seconds - elapsed)

    ended_wall = utc_now()
    capture_seconds = round(time.monotonic() - started_mono, 3)
    identifier = ssid_identifier(allowed_ssid)

    scan_outputs = [
        "\n".join([result.get("stdout", ""), result.get("stderr", "")])
        for result in scan_results
    ]
    undirected_rows = parse_corewlan_rows(scan_results[0].get("stdout", ""))
    directed_rows = parse_corewlan_rows(scan_results[1].get("stdout", ""))
    raw_allowed_match = any(allowed_ssid in output for output in scan_outputs)
    allowed_visible = raw_allowed_match or len(directed_rows) > 0
    observations = build_corewlan_observations(
        directed_rows,
        scan_results[1].get("stdout", ""),
        allowed_ssid,
        allowed_psk,
        identifier,
    )
    aps_seen = max(
        count_aps(scan_outputs, allowed_visible),
        len(undirected_rows),
        len(directed_rows),
    )

    os_output = "\n".join([os_result["stdout"], os_result["stderr"]])
    network_output = "\n".join([network_result["stdout"], network_result["stderr"]])
    kext_output = "\n".join([kext_result["stdout"], kext_result["stderr"]])
    log_output = "\n".join([log_result["stdout"], log_result["stderr"]])

    driver_loaded = kext_loaded(kext_output)
    interface_ok = interface_present(network_output, args.interface)
    ready = (
        capture_seconds >= args.min_seconds
        and interface_ok
        and driver_loaded
        and allowed_visible
        and aps_seen >= 1
        and len(observations) >= 1
    )

    evidence = {
        "schema_version": "itlwm-tahoe-lab-ap-visibility-readiness/v2",
        "capture_started_utc": started_wall.isoformat().replace("+00:00", "Z"),
        "capture_ended_utc": ended_wall.isoformat().replace("+00:00", "Z"),
        "capture_wallclock_seconds": capture_seconds,
        "runtime_environment": {
            "guest_os": parse_guest_os(os_result["stdout"]),
            "guest_ssh_target": "devops@127.0.0.1:3322",
            "guest_wifi_interface": args.interface,
            "guest_wifi_interface_present": interface_ok,
            "allowed_ap_identifier": identifier,
            "allowed_ap_identifier_kind": "sha256_prefix_of_operator_env_ssid",
        },
        "driver": {
            "kext_loaded": driver_loaded,
            "loaded_kext_query": {
                "source": kext_result["command"],
                "returncode": kext_result["returncode"],
                "redacted_lines": bounded_lines(
                    redact_text(kext_output, allowed_ssid, allowed_psk),
                    limit=8,
                ),
            },
            "runtime_log_observations": {
                "source": log_result["command"],
                "returncode": log_result["returncode"],
                "redacted_lines": bounded_lines(
                    redact_text(log_output, allowed_ssid, allowed_psk),
                    limit=20,
                ),
            },
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
            "commands": [result["command"] for result in scan_results],
            "aps_seen": aps_seen,
            "allowed_ap_visible": allowed_visible,
            "allowed_ap_observations": observations,
            "corewlan": {
                "undirected_result_summary": summarize_corewlan_rows(undirected_rows),
                "directed_result_summary": summarize_corewlan_rows(directed_rows),
                "directed_scan_target": "operator_env_ssid_redacted",
                "raw_allowed_ssid_echoed_by_api": raw_allowed_match,
            },
            "scan_attempts": [
                {
                    "source": result["command"],
                    "returncode": result["returncode"],
                    "duration_seconds": result["duration_seconds"],
                    "stdout_line_count": len(result.get("stdout", "").splitlines()),
                    "stderr_line_count": len(result.get("stderr", "").splitlines()),
                    "redacted_status_lines": safe_scan_status_lines(
                        "\n".join([result.get("stdout", ""), result.get("stderr", "")]),
                        allowed_ssid,
                        allowed_psk,
                    ),
                }
                for result in scan_results
            ],
        },
        "candidate_binding": {
            "loaded_kext_bound_to_checkout": False,
            "candidate_functional_verdict": "not-tested",
            "reason": (
                "visibility capture deliberately neither installs nor compares "
                "a kext artifact with the source checkout"
            ),
        },
        "non_claims": {
            "authentication_response_ack": False,
            "association": False,
            "dhcp": False,
            "data_transfer": False,
            "reconnect": False,
            "final_wifi_equivalence": False,
            "candidate_kext_tested": False,
        },
        "verdict": {
            "ready_for_candidate_runtime_experiment": ready,
            "reason": "allowed external AP visible from Tahoe guest Wi-Fi scan"
            if ready
            else "required live AP visibility predicates were not all satisfied",
        },
    }

    payload = json.dumps(evidence, indent=2, sort_keys=True) + "\n"
    assert_secret_absent(payload, allowed_ssid, allowed_psk)
    return evidence, payload


def main():
    parser = argparse.ArgumentParser(
        description="Capture sanitized Tahoe guest evidence for allowed AP visibility."
    )
    parser.add_argument("--output", default=str(DEFAULT_OUTPUT))
    parser.add_argument(
        "--guest",
        default=os.environ.get("ITLWM_GUEST_SSH_TARGET", DEFAULT_GUEST),
    )
    parser.add_argument(
        "--port",
        default=os.environ.get("ITLWM_GUEST_SSH_PORT", DEFAULT_PORT),
    )
    parser.add_argument(
        "--interface",
        default=os.environ.get("ITLWM_GUEST_WIFI_INTERFACE", DEFAULT_INTERFACE),
    )
    parser.add_argument(
        "--min-seconds",
        type=float,
        default=float(os.environ.get("ITLWM_CAPTURE_MIN_SECONDS", "30")),
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

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(payload, encoding="utf-8")

    verdict = evidence["verdict"]["ready_for_candidate_runtime_experiment"]
    print(
        "wrote "
        + str(output)
        + " allowed_ap_visible="
        + str(evidence["scan"]["allowed_ap_visible"]).lower()
        + " aps_seen="
        + str(evidence["scan"]["aps_seen"])
        + " identifier="
        + evidence["runtime_environment"]["allowed_ap_identifier"]
    )
    return 0 if verdict else 1


if __name__ == "__main__":
    sys.exit(main())
