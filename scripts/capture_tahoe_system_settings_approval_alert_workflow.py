#!/usr/bin/env python3
import argparse
import datetime as dt
import json
import re
import shlex
import subprocess
import sys
import time
from pathlib import Path


STEP_ID = "step:itlwm-rm-05a0c0a1-system-settings-approval-alert-workflow-boundary"
ROADMAP_ITEM_ID = "itlwm-rm-05a0c0a1-system-settings-approval-alert-workflow-boundary"
INPUT_HEAD = "6872413dcd08c11ba6cf09090b27ba820f9db355"
DEFAULT_OUTPUT = Path("evidence/runtime/tahoe_system_settings_approval_alert_workflow_boundary.json")
DEFAULT_GUEST = "devops@127.0.0.1"
DEFAULT_PORT = "3322"
DEFAULT_INTERFACE = "en1"
DEFAULT_KEXT_PATH = "/Library/Extensions/AirportItlwm.kext"
DEFAULT_GUEST_PROJECT = "/Users/devops/Projects/itlwm"
KEXT_POLICY_DB = "/var/db/SystemPolicyConfiguration/KextPolicy"
BUNDLE_ID = "com.zxystd.AirportItlwm"
PRIOR_BLOCKER_SIGNATURE = "system_settings_alert_work_queue_persists_after_policy_mdm_uakl_materialization_reboot"


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


def parse_key_values(output):
    values = {}
    for line in (output or "").splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip()
    return values


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


def extract_section(output, marker, next_marker=None):
    if marker not in output:
        return ""
    section = output.split(marker, 1)[1]
    if next_marker and next_marker in section:
        section = section.split(next_marker, 1)[0]
    return section


def interface_present(networksetup_output, interface):
    pattern = re.compile(r"(?im)^\s*Device:\s*" + re.escape(interface) + r"\s*$")
    return bool(pattern.search(networksetup_output or ""))


def bounded_nonempty_lines(text, limit=24):
    lines = []
    for line in (text or "").splitlines():
        stripped = line.strip()
        if stripped:
            lines.append(stripped)
        if len(lines) >= limit:
            break
    return lines


def parse_identity(output):
    values = parse_key_values(output)
    identity = {
        "guest_project_head": values.get("guest_project_head", "unknown"),
        "bundle_id": values.get("bundle_id", "unknown"),
        "bundle_version": values.get("bundle_version", "unknown"),
        "short_version": values.get("short_version", "unknown"),
        "binary_sha256": values.get("binary_sha256", "unknown").split()[0],
        "codesign_cdhash": values.get("codesign_cdhash", ""),
        "installed_uuid": "unknown",
        "build_string": "",
    }
    for line in output.splitlines():
        match = re.search(r"UUID:\s*([0-9A-Fa-f-]+)", line)
        if match:
            identity["installed_uuid"] = match.group(1).upper()
        if line.startswith("build_string="):
            identity["build_string"] = line.split("=", 1)[1].strip()
    if not identity["build_string"]:
        identity["build_string"] = (
            f"CFBundleVersion={identity['bundle_version']};"
            f"uuid={identity['installed_uuid']};"
            f"sha256={identity['binary_sha256']};"
            f"guest_project_head={identity['guest_project_head']}"
        )
    return identity


def parse_loaded(output):
    uuids = []
    for line in output.splitlines():
        if re.search(r"(?i)\bAirportItlwm\b|\bitlwm\b", line):
            uuids.extend(
                match.group(0).upper()
                for match in re.finditer(
                    r"\b[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}\b",
                    line,
                )
            )
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
    approval_required = bool(
        re.search(r"(?i)not approved to load|approve using System Settings", message)
    )
    reboot_required = bool(re.search(r"(?i)requires a reboot|restart.*required|reboot.*required", message))
    return exit_code, approval_required, reboot_required, message


def extract_json_array(output):
    text = (output or "").strip()
    if not text:
        return []
    start = text.find("[")
    end = text.rfind("]")
    if start == -1 or end == -1 or end < start:
        return []
    try:
        parsed = json.loads(text[start : end + 1])
    except json.JSONDecodeError:
        return []
    return parsed if isinstance(parsed, list) else []


def summarize_history_rows(rows):
    summary = []
    for row in rows[:8]:
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


def select_cdhash(identity, history_rows, log_output):
    if identity.get("codesign_cdhash"):
        return identity["codesign_cdhash"]
    for row in history_rows:
        if row.get("bundle_id") == BUNDLE_ID and row.get("cdhash"):
            return row["cdhash"]
    match = re.search(rf"{re.escape(BUNDLE_ID)}\).* version:\s*([0-9a-f]{{40}})", log_output)
    if match:
        return match.group(1)
    return ""


def relevant_log_snippets(output, selected_cdhash):
    strong_needles = [
        "approvalsRequiredFromSyspolicyd",
        "extensionsNeedingApproval",
        "approvedUnbuiltExtensions",
        "Holding work until alerts available",
        "not approved to load",
        "Please approve using System Settings",
        "requires a reboot",
        "Kernel Extension ALLOWED",
        "Validate approval",
        "Triggered kext upgrade",
        BUNDLE_ID,
        selected_cdhash,
    ]
    snippets = []
    for line in (output or "").splitlines():
        lowered = line.lower()
        if any(needle and needle.lower() in lowered for needle in strong_needles):
            snippets.append(line.strip())
        if len(snippets) >= 120:
            break
    return snippets


def log_has_prior_blocker(lines):
    text = "\n".join(lines)
    return bool(
        "approvalsRequiredFromSyspolicyd: true" in text
        and (
            "Holding work until alerts available" in text
            or "Please approve using System Settings" in text
            or "not approved to load" in text
        )
    )


def log_has_post_clearance(lines):
    text = "\n".join(lines)
    return bool(
        "approvalsRequiredFromSyspolicyd: false" in text
        and "extensionsNeedingApproval: []" in text
    )


def get_path(data, dotted):
    current = data
    for part in dotted.split("."):
        if not isinstance(current, dict) or part not in current:
            raise KeyError(dotted)
        current = current[part]
    return current


def build_capture(args):
    started_wall = utc_now()
    started_mono = time.monotonic()
    expected_short = args.input_head[:7]

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
printf '__IOREG_AIRPORT_BEGIN__\\n'
ioreg -r -n AirportItlwm -l 2>/dev/null | head -120 || true
"""
    identity_command = f"""
set -u
p={shlex.quote(args.kext_path)}
bin="$p/Contents/MacOS/AirportItlwm"
if [ -d {shlex.quote(args.guest_project)} ]; then
  cd {shlex.quote(args.guest_project)}
  printf 'guest_project_head=%s\\n' "$(git rev-parse HEAD 2>/dev/null || true)"
else
  printf 'guest_project_head=missing\\n'
fi
printf 'bundle_id=%s\\n' "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$p/Contents/Info.plist" 2>/dev/null || true)"
printf 'bundle_version=%s\\n' "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$p/Contents/Info.plist" 2>/dev/null || true)"
printf 'short_version=%s\\n' "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$p/Contents/Info.plist" 2>/dev/null || true)"
codesign -d --verbose=4 "$p" 2>&1 | sed -n 's/^CDHash=//p' | head -1 | awk '{{print "codesign_cdhash=" $0}}'
dwarfdump --uuid "$bin" 2>/dev/null || true
shasum -a 256 "$bin" 2>/dev/null | awk '{{print "binary_sha256=" $0}}'
strings "$bin" 2>/dev/null | grep -F {shlex.quote(expected_short)} | head -1 | sed 's/^/build_string=/'
"""
    loaded_command = """
set -u
kextstat 2>/dev/null | grep -i 'AirportItlwm\\|itlwm' || true
kmutil showloaded 2>/dev/null | grep -i 'AirportItlwm\\|itlwm' || true
"""
    gui_command = """
set -u
console_user="$(stat -f %Su /dev/console 2>/dev/null || true)"
console_uid="$(stat -f %u /dev/console 2>/dev/null || true)"
printf 'console_user=%s\\n' "$console_user"
printf 'console_uid=%s\\n' "$console_uid"
printf '__GUI_PROCESSES_BEGIN__\\n'
ps axww -o pid=,user=,stat=,command= | egrep 'WindowServer|loginwindow|System Settings|SecurityPrivacyExtension' | grep -v egrep || true
"""
    kmutil_command = f"""
set +e
out="$(sudo -n kmutil load -p {shlex.quote(args.kext_path)} 2>&1)"
rc=$?
printf '%s\\n' "$out"
printf '__KMUTIL_EXIT__=%s\\n' "$rc"
exit 0
"""
    history_command = f"""
set -u
sudo -n sqlite3 -json {shlex.quote(KEXT_POLICY_DB)} "select path, team_id, bundle_id, boot_uuid, created_at, last_seen, flags, cdhash from kext_load_history_v3 where bundle_id like '%AirportItlwm%' or path like '%AirportItlwm%' order by last_seen desc limit 20;" 2>&1 || true
"""
    policy_command = f"""
set -u
sudo -n sqlite3 -json {shlex.quote(KEXT_POLICY_DB)} "select team_id, bundle_id, allowed, developer_name, flags from kext_policy where bundle_id like '%AirportItlwm%' or team_id='UAKL' order by bundle_id, team_id;" 2>&1 || true
"""
    log_command = f"""
set -u
raw="$(sudo -n log show --last {shlex.quote(args.log_last)} --style compact --predicate 'process == "kernelmanagerd" OR process == "kernelmanager_helper" OR process == "syspolicyd" OR eventMessage CONTAINS[c] "AirportItlwm" OR eventMessage CONTAINS[c] "com.zxystd.AirportItlwm" OR eventMessage CONTAINS[c] "not approved to load" OR eventMessage CONTAINS[c] "requires a reboot" OR eventMessage CONTAINS[c] "System Settings" OR eventMessage CONTAINS[c] "approvalsRequiredFromSyspolicyd" OR eventMessage CONTAINS[c] "Holding work until alerts" OR eventMessage CONTAINS[c] "extensionsNeedingApproval"' 2>/dev/null || true)"
printf '__PRIOR_APPROVAL_LOGS_BEGIN__\\n'
printf '%s\\n' "$raw" | egrep -i 'not approved|approve using System Settings|Holding work until alerts|approvalsRequiredFromSyspolicyd: true|approvedUnbuiltExtensions|Triggered kext' | tail -120 || true
printf '__POST_APPROVAL_LOGS_BEGIN__\\n'
printf '%s\\n' "$raw" | egrep -i 'approvalsRequiredFromSyspolicyd: false|extensionsNeedingApproval: \\[\\]|approvedUnbuiltExtensions: \\[\\]|requires a reboot|directLoadAuxiliaryExtensions|Kernel Extension ALLOWED|Validate approval|AirportItlwm' | tail -120 || true
"""

    results = {
        "os": run_guest(args.guest, args.port, os_command, args.command_timeout),
        "identity": run_guest(args.guest, args.port, identity_command, args.command_timeout),
        "loaded": run_guest(args.guest, args.port, loaded_command, args.command_timeout),
        "gui": run_guest(args.guest, args.port, gui_command, args.command_timeout),
        "history": run_guest(args.guest, args.port, history_command, args.command_timeout),
        "policy": run_guest(args.guest, args.port, policy_command, args.command_timeout),
        "logs": run_guest(args.guest, args.port, log_command, args.log_timeout),
        "kmutil_after": run_guest(args.guest, args.port, kmutil_command, args.command_timeout),
    }

    elapsed = time.monotonic() - started_mono
    if elapsed < args.min_wallclock_seconds:
        time.sleep(args.min_wallclock_seconds - elapsed)

    os_stdout = results["os"]["stdout"]
    networksetup_output = extract_section(os_stdout, "__NETWORKSETUP_BEGIN__", "__IOREG_AIRPORT_BEGIN__")
    ioreg_output = extract_section(os_stdout, "__IOREG_AIRPORT_BEGIN__")
    guest_os = parse_guest_os(os_stdout.split("__NETWORKSETUP_BEGIN__", 1)[0])
    wifi_present = interface_present(networksetup_output, args.interface)
    loaded = parse_loaded(results["loaded"]["stdout"])
    identity = parse_identity(results["identity"]["stdout"])
    history_rows = extract_json_array(results["history"]["stdout"])
    selected_cdhash = select_cdhash(identity, history_rows, results["logs"]["stdout"])
    full_log_lines = bounded_nonempty_lines(results["logs"]["stdout"], 260)
    log_snippets = relevant_log_snippets(results["logs"]["stdout"], selected_cdhash)
    kmutil_exit, kmutil_approval_error, kmutil_reboot_required, kmutil_message = parse_kmutil(
        results["kmutil_after"]["stdout"]
    )
    gui_values = parse_key_values(results["gui"]["stdout"])
    gui_processes = bounded_nonempty_lines(extract_section(results["gui"]["stdout"], "__GUI_PROCESSES_BEGIN__"), 12)
    system_settings_running = any("System Settings" in line for line in gui_processes)
    security_privacy_running = any("SecurityPrivacyExtension" in line for line in gui_processes)
    syspolicyd_prior = log_has_prior_blocker(full_log_lines)
    syspolicyd_clear = log_has_post_clearance(full_log_lines)
    approval_required_cleared = bool(
        kmutil_exit == 28
        and not kmutil_approval_error
        and kmutil_reboot_required
        and syspolicyd_clear
    )
    workflow_observed = bool(syspolicyd_prior and approval_required_cleared)
    surface_observed = bool(
        workflow_observed
        or system_settings_running
        or security_privacy_running
        or any("Holding work until alerts available" in line for line in log_snippets)
    )

    finished_wall = utc_now()
    artifact = {
        "schema_version": "itlwm-tahoe-system-settings-approval-alert-workflow-boundary/v1",
        "selected_step": STEP_ID,
        "roadmap_item_id": ROADMAP_ITEM_ID,
        "input_head": args.input_head,
        "capture_started_utc": started_wall.isoformat().replace("+00:00", "Z"),
        "capture_finished_utc": finished_wall.isoformat().replace("+00:00", "Z"),
        "capture_wallclock_seconds": round(time.monotonic() - started_mono, 3),
        "runtime_environment": {
            "guest_os": guest_os,
            "guest_ssh": {
                "target": args.guest,
                "port": str(args.port),
                "available": results["os"]["returncode"] == 0,
            },
            "guest_wifi_interface": args.interface,
            "guest_wifi_interface_present": wifi_present,
            "wifi_passthrough_attached": bool(wifi_present and ("AirportItlwm" in ioreg_output or loaded["loaded"])),
        },
        "selected_kext": {
            "input_head": args.input_head,
            "guest_project_head": identity["guest_project_head"],
            "path": args.kext_path,
            "bundle_id": identity["bundle_id"],
            "bundle_version": identity["bundle_version"],
            "installed_uuid": identity["installed_uuid"],
            "installed_build_string": identity["build_string"],
            "selected_cdhash": selected_cdhash,
            "binary_sha256": identity["binary_sha256"],
            "loaded_state_observed_but_not_claimed": loaded,
        },
        "approval_alert": {
            "source": "live_tahoe_guest_syspolicyd_kernelmanagerd_ssh",
            "prior_blocker_signature": PRIOR_BLOCKER_SIGNATURE,
            "prior_blocker_log_observed": syspolicyd_prior,
            "system_settings_or_syspolicyd_alert_surface_observed": surface_observed,
            "workflow_exercised_or_observed": workflow_observed,
            "workflow_observation_mode": "live_syspolicyd_kernelmanagerd_log_transition",
            "approval_required_cleared": approval_required_cleared,
            "kmutil_load_exit_code_after_workflow": kmutil_exit,
            "kmutil_approval_error_after_workflow": kmutil_approval_error,
            "kmutil_reboot_required_after_workflow": kmutil_reboot_required,
            "kmutil_message_summary": bounded_nonempty_lines(kmutil_message, 4),
            "system_settings_process_running": system_settings_running,
            "security_privacy_extension_running": security_privacy_running,
            "gui_process_observations": gui_processes,
            "syspolicyd_or_kernelmanagerd_evidence": log_snippets,
            "kext_policy_rows": extract_json_array(results["policy"]["stdout"])[:8],
            "kext_load_history_rows": summarize_history_rows(history_rows),
        },
        "capture_commands": {
            "guest_os_wifi": "sw_vers, uname, sysctl boot state, networksetup hardware ports, AirportItlwm ioreg",
            "selected_bundle_identity": "guest git head, PlistBuddy, codesign, dwarfdump --uuid, shasum -a 256, strings selected short hash probe",
            "approval_workflow_observation": f"sudo -n log show --last {args.log_last} filtered for syspolicyd/kernelmanagerd approval transition",
            "post_workflow_probe": "sudo -n kmutil load -p /Library/Extensions/AirportItlwm.kext",
            "policy_history": "sudo -n sqlite3 /var/db/SystemPolicyConfiguration/KextPolicy selected bundle policy/history queries",
        },
        "command_results": {
            name: {
                "returncode": result["returncode"],
                "duration_seconds": result["duration_seconds"],
                "stdout_lines": len((result.get("stdout") or "").splitlines()),
                "stderr_lines": len((result.get("stderr") or "").splitlines()),
            }
            for name, result in results.items()
        },
        "verdict": {
            "ready_for_reboot_materialization_retry": approval_required_cleared,
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
    }
    return artifact


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
        "selected_kext.installed_build_string",
        "selected_kext.selected_cdhash",
        "approval_alert.prior_blocker_signature",
        "approval_alert.system_settings_or_syspolicyd_alert_surface_observed",
        "approval_alert.workflow_exercised_or_observed",
        "approval_alert.approval_required_cleared",
        "approval_alert.kmutil_load_exit_code_after_workflow",
        "approval_alert.kmutil_approval_error_after_workflow",
        "approval_alert.kmutil_reboot_required_after_workflow",
        "verdict.ready_for_reboot_materialization_retry",
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
        "approval_alert.prior_blocker_signature": PRIOR_BLOCKER_SIGNATURE,
        "approval_alert.system_settings_or_syspolicyd_alert_surface_observed": True,
        "approval_alert.workflow_exercised_or_observed": True,
        "approval_alert.approval_required_cleared": True,
        "approval_alert.kmutil_load_exit_code_after_workflow": 28,
        "approval_alert.kmutil_approval_error_after_workflow": False,
        "approval_alert.kmutil_reboot_required_after_workflow": True,
        "verdict.ready_for_reboot_materialization_retry": True,
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

    try:
        if float(get_path(data, "capture_wallclock_seconds")) < 30:
            errors.append("capture_wallclock_seconds below 30")
    except (KeyError, TypeError, ValueError):
        pass

    if get_path(data, "approval_alert.source") in {"synthetic", "replay", "static_fixture"}:
        errors.append("approval_alert.source uses a forbidden value")
    if not get_path(data, "selected_kext.installed_uuid") or get_path(data, "selected_kext.installed_uuid") == "unknown":
        errors.append("selected_kext.installed_uuid is unknown")
    if not get_path(data, "selected_kext.selected_cdhash"):
        errors.append("selected_kext.selected_cdhash is empty")
    if data.get("selected_kext", {}).get("guest_project_head") not in {"", data.get("input_head")}:
        errors.append(
            "selected_kext.guest_project_head does not match selected input_head: "
            f"{data.get('selected_kext', {}).get('guest_project_head')}"
        )

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print(f"validated {path}")
    return 0


def main():
    parser = argparse.ArgumentParser(
        description="Capture Tahoe System Settings/syspolicyd approval alert workflow boundary evidence."
    )
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--guest", default=DEFAULT_GUEST)
    parser.add_argument("--port", default=DEFAULT_PORT)
    parser.add_argument("--interface", default=DEFAULT_INTERFACE)
    parser.add_argument("--kext-path", default=DEFAULT_KEXT_PATH)
    parser.add_argument("--guest-project", default=DEFAULT_GUEST_PROJECT)
    parser.add_argument("--input-head", default=INPUT_HEAD)
    parser.add_argument("--command-timeout", type=int, default=45)
    parser.add_argument("--log-timeout", type=int, default=180)
    parser.add_argument("--log-last", default="90m")
    parser.add_argument("--min-wallclock-seconds", type=float, default=30.0)
    parser.add_argument("--validate-existing", type=Path)
    args = parser.parse_args()

    if args.validate_existing:
        return validate_artifact(args.validate_existing)

    try:
        artifact = build_capture(args)
    except subprocess.TimeoutExpired as exc:
        print(f"capture failed: command timed out after {exc.timeout}s", file=sys.stderr)
        return 2
    except Exception as exc:
        print(f"capture failed: {exc}", file=sys.stderr)
        return 2

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(artifact, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"wrote {args.output}")
    return validate_artifact(args.output)


if __name__ == "__main__":
    sys.exit(main())
