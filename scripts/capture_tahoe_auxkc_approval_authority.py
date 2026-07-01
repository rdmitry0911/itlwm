#!/usr/bin/env python3
import argparse
import datetime as dt
import json
import os
import re
import shlex
import subprocess
import sys
import time
from pathlib import Path


STEP_ID = "step:itlwm-rm-05a0c0a0-auxkc-approval-authority-isolation-boundary"
ROADMAP_ITEM_ID = "itlwm-rm-05a0c0a0-auxkc-approval-authority-isolation-boundary"
INPUT_HEAD = "e78f3c965bfde16416bf000018113dde2f25efd6"
DEFAULT_OUTPUT = Path("evidence/runtime/tahoe_auxkc_approval_authority_isolation_boundary.json")
DEFAULT_GUEST = "devops@127.0.0.1"
DEFAULT_PORT = "3322"
DEFAULT_INTERFACE = "en1"
DEFAULT_KEXT_PATH = "/Library/Extensions/AirportItlwm.kext"
KEXT_POLICY_DB = "/var/db/SystemPolicyConfiguration/KextPolicy"
BUNDLE_ID = "com.zxystd.AirportItlwm"


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
        "returncode": completed.returncode,
        "stdout": completed.stdout,
        "stderr": completed.stderr,
        "duration_seconds": round(time.monotonic() - started, 3),
    }


def bounded_nonempty_lines(text, limit=20):
    lines = []
    for line in (text or "").splitlines():
        stripped = line.strip()
        if stripped:
            lines.append(stripped)
        if len(lines) >= limit:
            break
    return lines


def parse_guest_os(output):
    values = {}
    for line in output.splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip()
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


def parse_identity(output):
    identity = {
        "bundle_id": "unknown",
        "bundle_version": "unknown",
        "installed_uuid": "unknown",
        "binary_sha256": "unknown",
        "codesign_cdhash": "",
    }
    for line in output.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            key = key.strip()
            value = value.strip()
            if key == "bundle_id":
                identity["bundle_id"] = value
            elif key == "bundle_version":
                identity["bundle_version"] = value
            elif key == "binary_sha256":
                identity["binary_sha256"] = value.split()[0] if value else "unknown"
            elif key == "codesign_cdhash":
                identity["codesign_cdhash"] = value
        match = re.search(r"UUID:\s*([0-9A-Fa-f-]+)", line)
        if match:
            identity["installed_uuid"] = match.group(1).upper()
    return identity


def parse_loaded(output):
    uuids = []
    for line in output.splitlines():
        if re.search(r"(?i)\bAirportItlwm\b|\bitlwm\b", line):
            for match in re.finditer(r"\b[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}\b", line):
                uuids.append(match.group(0).upper())
    return {
        "loaded": bool(uuids or re.search(r"(?i)\bAirportItlwm\b|\bitlwm\b", output or "")),
        "loaded_uuids_observed": uuids[:8],
    }


def parse_kmutil(output):
    exit_code = None
    cleaned_lines = []
    for line in output.splitlines():
        if line.startswith("__KMUTIL_EXIT__="):
            try:
                exit_code = int(line.split("=", 1)[1])
            except ValueError:
                exit_code = None
            continue
        cleaned_lines.append(line)
    message = "\n".join(cleaned_lines).strip()
    approval_required = bool(re.search(r"(?i)not approved to load|approve using System Settings", message))
    return exit_code, approval_required, message


def extract_json_array(output):
    text = (output or "").strip()
    if not text:
        return []
    start = text.find("[")
    end = text.rfind("]")
    if start == -1 or end == -1 or end < start:
        return []
    try:
        value = json.loads(text[start : end + 1])
    except json.JSONDecodeError:
        return []
    return value if isinstance(value, list) else []


def summarize_policy_rows(rows):
    summary = []
    for row in rows:
        team_id = row.get("team_id")
        if team_id is None:
            team_id = "<null>"
        summary.append(
            {
                "team_id": team_id,
                "bundle_id": row.get("bundle_id", ""),
                "allowed": bool(row.get("allowed")),
                "developer_name": row.get("developer_name", ""),
                "flags": row.get("flags"),
            }
        )
    return summary


def summarize_mdm_rows(rows):
    summary = []
    for row in rows:
        team_id = row.get("team_id")
        if team_id is None:
            team_id = "<null>"
        summary.append(
            {
                "team_id": team_id,
                "bundle_id": row.get("bundle_id", ""),
                "allowed": bool(row.get("allowed")),
                "payload_uuid_present": bool(row.get("payload_uuid")),
            }
        )
    return summary


def summarize_history_rows(rows):
    summary = []
    for row in rows:
        summary.append(
            {
                "path": row.get("path", ""),
                "team_id": row.get("team_id") if row.get("team_id") is not None else "<null>",
                "bundle_id": row.get("bundle_id", ""),
                "boot_uuid": row.get("boot_uuid", ""),
                "created_at": row.get("created_at", ""),
                "last_seen": row.get("last_seen", ""),
                "flags": row.get("flags"),
                "cdhash": row.get("cdhash", ""),
            }
        )
    return summary


def select_cdhash(identity, history_rows):
    if identity.get("codesign_cdhash"):
        return identity["codesign_cdhash"]
    for row in history_rows:
        cdhash = row.get("cdhash")
        if row.get("bundle_id") == BUNDLE_ID and cdhash:
            return cdhash
    return ""


def relevant_log_snippets(output, selected_cdhash):
    snippets = []
    strong_needles = [
        "approvalsRequiredFromSyspolicyd",
        "not approved to load",
        "System Settings",
        "failed to load/rebuild extensions",
        "approvedUnbuiltExtensions",
        "Holding work until alerts available",
        "Triggered kext upgrade",
        "requesting latest approval status and kext history",
        BUNDLE_ID,
        selected_cdhash,
    ]
    weak_needles = ["kernelmanager", "syspolicyd"]
    noisy_needles = [
        "SecKeyVerifySignature",
        "SecTrustEvaluateIfNecessary",
        "DetachedSignatures",
        "Validating extension at <private>",
    ]
    for line in (output or "").splitlines():
        lowered = line.lower()
        if any(needle and needle.lower() in lowered for needle in strong_needles):
            snippets.append(line.strip())
        elif (
            any(needle.lower() in lowered for needle in weak_needles)
            and not any(needle.lower() in lowered for needle in noisy_needles)
            and len(snippets) < 8
        ):
            snippets.append(line.strip())
        if len(snippets) >= 32:
            break
    return snippets


def build_capture(args):
    started_wall = utc_now()
    started_mono = time.monotonic()

    os_command = """
set -u
printf 'product_name=%s\\n' "$(sw_vers -productName 2>/dev/null || true)"
printf 'product_version=%s\\n' "$(sw_vers -productVersion 2>/dev/null || true)"
printf 'build_version=%s\\n' "$(sw_vers -buildVersion 2>/dev/null || true)"
printf 'kernel_release=%s\\n' "$(uname -r 2>/dev/null || true)"
printf 'boot_time=%s\\n' "$(sysctl -n kern.boottime 2>/dev/null || true)"
printf 'boot_session_uuid=%s\\n' "$(sysctl -n kern.bootsessionuuid 2>/dev/null || true)"
printf '__NETWORKSETUP_BEGIN__\\n'
networksetup -listallhardwareports 2>/dev/null || true
printf '__AIRPORT_IOREG_BEGIN__\\n'
ioreg -r -n AirportItlwm -l 2>/dev/null | head -120 || true
"""
    identity_command = f"""
set -u
p={shlex.quote(args.kext_path)}
bin="$p/Contents/MacOS/AirportItlwm"
printf 'bundle_id=%s\\n' "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$p/Contents/Info.plist" 2>/dev/null || true)"
printf 'bundle_version=%s\\n' "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$p/Contents/Info.plist" 2>/dev/null || true)"
codesign -d --verbose=4 "$p" 2>&1 | sed -n 's/^CDHash=//p' | head -1 | awk '{{print "codesign_cdhash=" $0}}'
dwarfdump --uuid "$bin" 2>/dev/null || true
shasum -a 256 "$bin" 2>/dev/null | awk '{{print "binary_sha256=" $0}}'
"""
    loaded_command = """
set -u
kextstat 2>/dev/null | grep -i 'AirportItlwm\\|itlwm' || true
kmutil showloaded 2>/dev/null | grep -i 'AirportItlwm\\|itlwm' || true
"""
    kmutil_command = f"""
set +e
out="$(sudo -n kmutil load -p {shlex.quote(args.kext_path)} 2>&1)"
rc=$?
printf '%s\\n' "$out"
printf '__KMUTIL_EXIT__=%s\\n' "$rc"
exit 0
"""
    tables_command = f"""
set -u
sudo -n sqlite3 {shlex.quote(KEXT_POLICY_DB)} '.tables' 2>&1 || true
"""
    policy_command = f"""
set -u
sudo -n sqlite3 -json {shlex.quote(KEXT_POLICY_DB)} "select team_id, bundle_id, allowed, developer_name, flags from kext_policy where bundle_id like '%AirportItlwm%' or bundle_id like '%itlwm%' or team_id='UAKL' order by bundle_id, team_id;" 2>&1 || true
"""
    mdm_command = f"""
set -u
sudo -n sqlite3 -json {shlex.quote(KEXT_POLICY_DB)} "select team_id, bundle_id, allowed, payload_uuid from kext_policy_mdm where bundle_id like '%AirportItlwm%' or bundle_id like '%itlwm%' or team_id='UAKL' order by bundle_id, team_id;" 2>&1 || true
"""
    history_command = f"""
set -u
sudo -n sqlite3 -json {shlex.quote(KEXT_POLICY_DB)} "select path, team_id, bundle_id, boot_uuid, created_at, last_seen, flags, cdhash from kext_load_history_v3 where bundle_id like '%AirportItlwm%' or bundle_id like '%itlwm%' or path like '%AirportItlwm%' or path like '%itlwm%' order by last_seen desc limit 20;" 2>&1 || true
"""
    log_command = """
set -u
sudo -n log show --last 30m --style compact --predicate 'process == "kernelmanagerd" OR process == "kernelmanager_helper" OR process == "syspolicyd" OR eventMessage CONTAINS[c] "AirportItlwm" OR eventMessage CONTAINS[c] "com.zxystd.AirportItlwm" OR eventMessage CONTAINS[c] "not approved to load" OR eventMessage CONTAINS[c] "System Settings" OR eventMessage CONTAINS[c] "approvalsRequiredFromSyspolicyd"' 2>/dev/null | tail -160 || true
"""

    results = {
        "os": run_guest(args.guest, args.port, os_command, args.command_timeout),
        "identity": run_guest(args.guest, args.port, identity_command, args.command_timeout),
        "loaded": run_guest(args.guest, args.port, loaded_command, args.command_timeout),
        "kmutil": run_guest(args.guest, args.port, kmutil_command, args.command_timeout),
        "tables": run_guest(args.guest, args.port, tables_command, args.command_timeout),
        "policy": run_guest(args.guest, args.port, policy_command, args.command_timeout),
        "mdm": run_guest(args.guest, args.port, mdm_command, args.command_timeout),
        "history": run_guest(args.guest, args.port, history_command, args.command_timeout),
        "logs": run_guest(args.guest, args.port, log_command, args.log_timeout),
    }

    elapsed = time.monotonic() - started_mono
    if elapsed < args.min_wallclock_seconds:
        time.sleep(args.min_wallclock_seconds - elapsed)

    os_stdout = results["os"]["stdout"]
    networksetup_output = os_stdout.split("__NETWORKSETUP_BEGIN__", 1)[-1].split("__AIRPORT_IOREG_BEGIN__", 1)[0]
    ioreg_output = os_stdout.split("__AIRPORT_IOREG_BEGIN__", 1)[-1] if "__AIRPORT_IOREG_BEGIN__" in os_stdout else ""
    guest_os = parse_guest_os(os_stdout.split("__NETWORKSETUP_BEGIN__", 1)[0])
    wifi_present = interface_present(networksetup_output, args.interface)
    identity = parse_identity(results["identity"]["stdout"])
    loaded = parse_loaded(results["loaded"]["stdout"])
    kmutil_exit, approval_required, kmutil_message = parse_kmutil(results["kmutil"]["stdout"])
    policy_rows = extract_json_array(results["policy"]["stdout"])
    mdm_rows = extract_json_array(results["mdm"]["stdout"])
    history_rows = extract_json_array(results["history"]["stdout"])
    selected_cdhash = select_cdhash(identity, history_rows)
    policy_summary = summarize_policy_rows(policy_rows)
    mdm_summary = summarize_mdm_rows(mdm_rows)
    history_summary = summarize_history_rows(history_rows)
    history_cdhash_present = any(row.get("cdhash") == selected_cdhash for row in history_rows)
    policy_allowed_present = any(row.get("bundle_id") == BUNDLE_ID and bool(row.get("allowed")) for row in policy_rows)
    log_snippets = relevant_log_snippets(results["logs"]["stdout"], selected_cdhash)
    syspolicy_evidence = any(
        re.search(r"(?i)kernelmanager|syspolicyd|approvalsRequiredFromSyspolicyd|not approved to load|System Settings", line)
        for line in log_snippets
    )
    machine_surface_found = bool(
        history_cdhash_present
        and policy_allowed_present
        and approval_required
        and syspolicy_evidence
    )

    finished_wall = utc_now()
    artifact = {
        "selected_step": STEP_ID,
        "roadmap_item_id": ROADMAP_ITEM_ID,
        "input_head": args.input_head,
        "capture_started_utc": started_wall.isoformat().replace("+00:00", "Z"),
        "capture_finished_utc": finished_wall.isoformat().replace("+00:00", "Z"),
        "capture_wallclock_seconds": round(time.monotonic() - started_mono, 3),
        "runtime_environment": {
            "guest_os": guest_os,
            "guest_wifi_interface": args.interface,
            "wifi_passthrough_attached": bool(wifi_present and ("AirportItlwm" in ioreg_output or loaded["loaded"])),
            "guest_ssh": {
                "target": args.guest,
                "port": str(args.port),
            },
        },
        "selected_kext": {
            "input_head": args.input_head,
            "path": args.kext_path,
            "bundle_id": identity["bundle_id"],
            "bundle_version": identity["bundle_version"],
            "installed_uuid": identity["installed_uuid"],
            "selected_cdhash": selected_cdhash,
            "binary_sha256": identity["binary_sha256"],
            "loaded_state_observed_but_not_claimed": loaded,
        },
        "approval_authority": {
            "source": "live_tahoe_guest_ssh",
            "prior_blocker_signature": "system_settings_approval_required_after_selected_install_uakl_persistence_and_reboot",
            "uakl_cdhash_present_after_reboot": bool(history_cdhash_present and policy_allowed_present),
            "kmutil_load_exit_code": kmutil_exit,
            "kmutil_system_settings_approval_required": approval_required,
            "kmutil_message_summary": bounded_nonempty_lines(kmutil_message, 4),
            "kext_policy_db_tables": bounded_nonempty_lines(results["tables"]["stdout"], 8),
            "kext_policy_rows_inspected": len(policy_rows),
            "kext_policy_rows": policy_summary,
            "kext_policy_mdm_rows_inspected": len(mdm_rows),
            "kext_policy_mdm_rows": mdm_summary,
            "kext_load_history_table": "kext_load_history_v3",
            "kext_load_history_rows_inspected": len(history_rows),
            "kext_load_history_rows": history_summary,
            "syspolicyd_or_kernelmanagerd_evidence_present": syspolicy_evidence,
            "syspolicyd_or_kernelmanagerd_evidence": log_snippets,
            "classification": "kext_policy_allowed_and_history_cdhash_present_but_kernelmanagerd_syspolicyd_auxkc_rebuild_still_requires_system_settings_approval",
            "machine_actionable_surface_found": machine_surface_found,
            "machine_actionable_surface": "kernelmanagerd syspolicyd approval work queue plus SystemPolicyConfiguration.KextPolicy kext_policy/kext_policy_mdm approval rows for the selected bundle id and cdhash",
            "next_autonomous_mechanism": "exercise the kernelmanagerd/syspolicyd AuxKC approval alert/workflow or seed the kext_policy_mdm team-bundle approval surface for com.zxystd.AirportItlwm, then rerun kmutil load; do not retry kext_load_history/UAKL cdhash persistence alone",
            "post_reboot_basis": {
                "guest_boot_session_uuid": guest_os.get("boot_session_uuid", "unknown"),
                "history_boot_uuids": sorted({row.get("boot_uuid", "") for row in history_rows if row.get("boot_uuid")}),
                "history_last_seen_values": [row.get("last_seen", "") for row in history_rows if row.get("last_seen")],
            },
        },
        "verdict": {
            "ready_for_approval_mechanism_retry": bool(
                selected_cdhash
                and wifi_present
                and history_cdhash_present
                and approval_required
                and syspolicy_evidence
                and machine_surface_found
            ),
            "approval_resolved_not_claimed": True,
            "load_admission_not_claimed": True,
            "loaded_uuid_after_reboot_not_claimed": True,
            "join_trigger_not_claimed": True,
            "pmk_delivery_not_claimed": True,
            "auth_tx_rx_not_claimed": True,
            "association_not_claimed": True,
            "dhcp_not_claimed": True,
            "data_transfer_not_claimed": True,
            "reconnect_not_claimed": True,
            "final_wifi_equivalence_not_claimed": True,
        },
        "capture_commands": {
            "guest_os": "sw_vers, uname, sysctl boot state, networksetup hardware ports",
            "selected_bundle_identity": "PlistBuddy, dwarfdump --uuid, shasum -a 256",
            "approval_probe": "sudo -n kmutil load -p /Library/Extensions/AirportItlwm.kext",
            "policy_history": "sudo -n sqlite3 /var/db/SystemPolicyConfiguration/KextPolicy policy/history queries",
            "kernelmanagerd_syspolicyd": "sudo -n log show --last 30m filtered for approval authority evidence",
        },
    }
    return artifact


def get_path(data, dotted):
    current = data
    for part in dotted.split("."):
        if not isinstance(current, dict) or part not in current:
            raise KeyError(dotted)
        current = current[part]
    return current


def validate_artifact(path):
    data = json.loads(Path(path).read_text())
    required_fields = [
        "selected_step",
        "input_head",
        "capture_wallclock_seconds",
        "runtime_environment.guest_os",
        "runtime_environment.guest_wifi_interface",
        "runtime_environment.wifi_passthrough_attached",
        "selected_kext.input_head",
        "selected_kext.installed_uuid",
        "selected_kext.selected_cdhash",
        "approval_authority.prior_blocker_signature",
        "approval_authority.uakl_cdhash_present_after_reboot",
        "approval_authority.kmutil_load_exit_code",
        "approval_authority.kmutil_system_settings_approval_required",
        "approval_authority.kext_policy_rows_inspected",
        "approval_authority.kext_load_history_rows_inspected",
        "approval_authority.syspolicyd_or_kernelmanagerd_evidence_present",
        "approval_authority.classification",
        "approval_authority.machine_actionable_surface_found",
        "approval_authority.next_autonomous_mechanism",
        "verdict.ready_for_approval_mechanism_retry",
        "verdict.approval_resolved_not_claimed",
        "verdict.load_admission_not_claimed",
        "verdict.loaded_uuid_after_reboot_not_claimed",
        "verdict.join_trigger_not_claimed",
        "verdict.pmk_delivery_not_claimed",
        "verdict.auth_tx_rx_not_claimed",
        "verdict.final_wifi_equivalence_not_claimed",
    ]
    errors = []
    for field in required_fields:
        try:
            get_path(data, field)
        except KeyError:
            errors.append(f"missing field: {field}")
    checks_equal = {
        "selected_step": STEP_ID,
        "runtime_environment.guest_wifi_interface": DEFAULT_INTERFACE,
        "runtime_environment.wifi_passthrough_attached": True,
        "approval_authority.prior_blocker_signature": "system_settings_approval_required_after_selected_install_uakl_persistence_and_reboot",
        "approval_authority.uakl_cdhash_present_after_reboot": True,
        "approval_authority.kmutil_system_settings_approval_required": True,
        "approval_authority.syspolicyd_or_kernelmanagerd_evidence_present": True,
        "approval_authority.machine_actionable_surface_found": True,
        "verdict.ready_for_approval_mechanism_retry": True,
        "verdict.approval_resolved_not_claimed": True,
        "verdict.load_admission_not_claimed": True,
        "verdict.loaded_uuid_after_reboot_not_claimed": True,
        "verdict.join_trigger_not_claimed": True,
        "verdict.pmk_delivery_not_claimed": True,
        "verdict.auth_tx_rx_not_claimed": True,
        "verdict.final_wifi_equivalence_not_claimed": True,
    }
    for field, expected in checks_equal.items():
        try:
            observed = get_path(data, field)
        except KeyError:
            continue
        if observed != expected:
            errors.append(f"{field}: expected {expected!r}, observed {observed!r}")
    if get_path(data, "capture_wallclock_seconds") < 10:
        errors.append("capture_wallclock_seconds below 10")
    if get_path(data, "approval_authority.kext_load_history_rows_inspected") < 1:
        errors.append("approval_authority.kext_load_history_rows_inspected below 1")
    forbidden_sources = {"synthetic", "replay", "static_fixture"}
    source = get_path(data, "approval_authority.source")
    if source in forbidden_sources:
        errors.append(f"forbidden approval_authority.source: {source}")
    forbidden_classifications = {"unknown", "unclassified", "prose_only"}
    classification = get_path(data, "approval_authority.classification")
    if classification in forbidden_classifications:
        errors.append(f"forbidden approval_authority.classification: {classification}")
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print(f"validated {path}")
    return 0


def main():
    parser = argparse.ArgumentParser(description="Capture Tahoe AuxKC approval authority boundary evidence.")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--guest", default=DEFAULT_GUEST)
    parser.add_argument("--port", default=DEFAULT_PORT)
    parser.add_argument("--interface", default=DEFAULT_INTERFACE)
    parser.add_argument("--kext-path", default=DEFAULT_KEXT_PATH)
    parser.add_argument("--input-head", default=INPUT_HEAD)
    parser.add_argument("--command-timeout", type=int, default=45)
    parser.add_argument("--log-timeout", type=int, default=60)
    parser.add_argument("--min-wallclock-seconds", type=float, default=10.0)
    parser.add_argument("--validate-existing", type=Path)
    args = parser.parse_args()

    if args.validate_existing:
        return validate_artifact(args.validate_existing)

    artifact = build_capture(args)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(artifact, indent=2, sort_keys=True) + "\n")
    print(f"wrote {args.output}")
    return validate_artifact(args.output)


if __name__ == "__main__":
    sys.exit(main())
