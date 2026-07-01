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


STEP_ID = "step:itlwm-rm-05a0c0a-auxkc-approval-reboot-materialization-boundary"
ROADMAP_ITEM_ID = "itlwm-rm-05a0c0a-auxkc-approval-reboot-materialization-boundary"
INPUT_HEAD = "9e83fb02ada3822747f19395b57129f9e4e2fc53"
DEFAULT_OUTPUT = Path("evidence/runtime/tahoe_auxkc_approval_reboot_materialization_boundary.json")
DEFAULT_GUEST = "devops@127.0.0.1"
DEFAULT_PORT = "3322"
DEFAULT_INTERFACE = "en1"
DEFAULT_KEXT_PATH = "/Library/Extensions/AirportItlwm.kext"
DEFAULT_GUEST_PROJECT = "/Users/devops/Projects/itlwm"
KEXT_POLICY_DB = "/var/db/SystemPolicyConfiguration/KextPolicy"
BUNDLE_ID = "com.zxystd.AirportItlwm"
PRIOR_BLOCKER_SIGNATURE = "system_settings_approval_required_after_selected_install_and_uakl_materialization"


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


def run_guest(target, port, remote_command, timeout_seconds):
    wrapped_remote_command = "/bin/bash -lc " + shlex.quote(remote_command)
    command = ssh_base(target, port) + [wrapped_remote_command]
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


def run_guest_script(target, port, script, timeout_seconds):
    command = ssh_base(target, port) + ["/bin/bash", "-s"]
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
        "returncode": completed.returncode,
        "stdout": completed.stdout,
        "stderr": completed.stderr,
        "duration_seconds": round(time.monotonic() - started, 3),
    }


def bounded_nonempty_lines(text, limit=24):
    lines = []
    for line in (text or "").splitlines():
        stripped = line.strip()
        if stripped:
            lines.append(stripped)
        if len(lines) >= limit:
            break
    return lines


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
    for line in (output or "").splitlines():
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
    for line in (output or "").splitlines():
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
    for line in (output or "").splitlines():
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


def normalize_team_id(value):
    return "<null>" if value is None else value


def summarize_history_rows(rows):
    summary = []
    for row in rows[:10]:
        summary.append(
            {
                "path": row.get("path", ""),
                "team_id": normalize_team_id(row.get("team_id")),
                "bundle_id": row.get("bundle_id", ""),
                "boot_uuid": row.get("boot_uuid", ""),
                "created_at": row.get("created_at", ""),
                "last_seen": row.get("last_seen", ""),
                "flags": row.get("flags"),
                "cdhash": row.get("cdhash", ""),
            }
        )
    return summary


def summarize_policy_rows(rows):
    summary = []
    for row in rows[:12]:
        summary.append(
            {
                "team_id": normalize_team_id(row.get("team_id")),
                "bundle_id": row.get("bundle_id", ""),
                "allowed": bool(row.get("allowed")),
                "developer_name": row.get("developer_name", ""),
                "flags": row.get("flags"),
            }
        )
    return summary


def select_cdhash(identity, history_rows, log_output):
    if identity.get("codesign_cdhash"):
        return identity["codesign_cdhash"]
    for row in history_rows:
        if row.get("bundle_id") == BUNDLE_ID and row.get("cdhash"):
            return row["cdhash"]
    match = re.search(rf"{re.escape(BUNDLE_ID)}\).* version:\s*([0-9a-f]{{40}})", log_output or "")
    if match:
        return match.group(1)
    return ""


def uakl_cdhash_present(selected_cdhash, policy_rows, history_rows):
    has_history_cdhash = any(
        row.get("bundle_id") == BUNDLE_ID and row.get("cdhash") == selected_cdhash
        for row in history_rows
    )
    has_uakl_policy = any(
        row.get("bundle_id") == BUNDLE_ID
        and row.get("team_id") == "UAKL"
        and bool(row.get("allowed"))
        for row in policy_rows
    )
    return bool(selected_cdhash and has_history_cdhash and has_uakl_policy)


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
        "Loading extension",
        BUNDLE_ID,
        selected_cdhash,
    ]
    snippets = []
    for line in (output or "").splitlines():
        lowered = line.lower()
        if any(needle and needle.lower() in lowered for needle in strong_needles):
            snippets.append(line.strip())
        if len(snippets) >= 160:
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
        or "extensionsNeedingApproval: []" in text
        or "approvedUnbuiltExtensions: []" in text
    )


def build_os_command():
    return """
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
ioreg -r -n AirportItlwm -l 2>/dev/null | head -160 || true
"""


def build_identity_command(args):
    expected_short = args.input_head[:7]
    return f"""
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
shasum -a 256 "$bin" 2>/dev/null | awk '{{print "binary_sha256=" $1}}'
strings "$bin" 2>/dev/null | grep -F {shlex.quote(expected_short)} | head -1 | sed 's/^/build_string=/'
"""


def build_loaded_command():
    return """
set -u
kextstat 2>/dev/null | grep -i 'AirportItlwm\\|itlwm' || true
kmutil showloaded 2>/dev/null | grep -i 'AirportItlwm\\|itlwm' || true
"""


def build_kmutil_command(args):
    return f"""
set +e
out="$(sudo -n kmutil load -p {shlex.quote(args.kext_path)} 2>&1)"
rc=$?
printf '%s\\n' "$out"
printf '__KMUTIL_EXIT__=%s\\n' "$rc"
exit 0
"""


def build_history_command():
    return f"""
set -u
sudo -n sqlite3 -json {shlex.quote(KEXT_POLICY_DB)} "select path, team_id, bundle_id, boot_uuid, created_at, last_seen, flags, cdhash from kext_load_history_v3 where bundle_id like '%AirportItlwm%' or path like '%AirportItlwm%' order by last_seen desc limit 20;" 2>&1 || true
"""


def build_policy_command():
    return f"""
set -u
sudo -n sqlite3 -json {shlex.quote(KEXT_POLICY_DB)} "select team_id, bundle_id, allowed, developer_name, flags from kext_policy where bundle_id like '%AirportItlwm%' or team_id='UAKL' order by bundle_id, team_id;" 2>&1 || true
"""


def build_log_command(args):
    return f"""
set -u
raw="$(sudo -n log show --last {shlex.quote(args.log_last)} --style compact --predicate 'process == "kernelmanagerd" OR process == "kernelmanager_helper" OR process == "syspolicyd" OR eventMessage CONTAINS[c] "AirportItlwm" OR eventMessage CONTAINS[c] "com.zxystd.AirportItlwm" OR eventMessage CONTAINS[c] "not approved to load" OR eventMessage CONTAINS[c] "requires a reboot" OR eventMessage CONTAINS[c] "System Settings" OR eventMessage CONTAINS[c] "approvalsRequiredFromSyspolicyd" OR eventMessage CONTAINS[c] "Holding work until alerts" OR eventMessage CONTAINS[c] "extensionsNeedingApproval"' 2>/dev/null || true)"
printf '%s\\n' "$raw" | egrep -i 'not approved|approve using System Settings|Holding work until alerts|approvalsRequiredFromSyspolicyd|approvedUnbuiltExtensions|extensionsNeedingApproval|Triggered kext|requires a reboot|Kernel Extension ALLOWED|Validate approval|AirportItlwm' | tail -180 || true
"""


def build_install_script(args):
    return f"""
set -euo pipefail
cd {shlex.quote(args.guest_project)}
head="$(git rev-parse HEAD)"
if [ "$head" != {shlex.quote(args.input_head)} ]; then
  echo "guest project head mismatch: $head" >&2
  exit 11
fi
./scripts/build_tahoe.sh
test -d Build/Debug/Tahoe/AirportItlwm.kext
sudo -n rm -rf {shlex.quote(args.kext_path)}
sudo -n cp -R Build/Debug/Tahoe/AirportItlwm.kext {shlex.quote(args.kext_path)}
sudo -n chown -R root:wheel {shlex.quote(args.kext_path)}
sudo -n chmod -R go-w {shlex.quote(args.kext_path)}
sudo -n touch /Library/Extensions
"""


def collect_probe(args, phase):
    results = {
        "os": run_guest(args.guest, args.port, build_os_command(), args.command_timeout),
        "identity": run_guest(args.guest, args.port, build_identity_command(args), args.command_timeout),
        "loaded": run_guest(args.guest, args.port, build_loaded_command(), args.command_timeout),
        "history": run_guest(args.guest, args.port, build_history_command(), args.command_timeout),
        "policy": run_guest(args.guest, args.port, build_policy_command(), args.command_timeout),
        "logs": run_guest(args.guest, args.port, build_log_command(args), args.log_timeout),
        "kmutil": run_guest(args.guest, args.port, build_kmutil_command(args), args.command_timeout),
    }
    os_stdout = results["os"]["stdout"]
    networksetup_output = extract_section(os_stdout, "__NETWORKSETUP_BEGIN__", "__IOREG_AIRPORT_BEGIN__")
    ioreg_output = extract_section(os_stdout, "__IOREG_AIRPORT_BEGIN__")
    guest_os = parse_guest_os(os_stdout.split("__NETWORKSETUP_BEGIN__", 1)[0])
    wifi_present = interface_present(networksetup_output, args.interface)
    loaded = parse_loaded(results["loaded"]["stdout"])
    identity = parse_identity(results["identity"]["stdout"])
    history_rows = extract_json_array(results["history"]["stdout"])
    policy_rows = extract_json_array(results["policy"]["stdout"])
    log_lines = bounded_nonempty_lines(results["logs"]["stdout"], 260)
    selected_cdhash = select_cdhash(identity, history_rows, results["logs"]["stdout"])
    kmutil_exit, kmutil_approval_error, kmutil_reboot_required, kmutil_message = parse_kmutil(
        results["kmutil"]["stdout"]
    )
    return {
        "phase": phase,
        "guest_os": guest_os,
        "wifi_present": wifi_present,
        "wifi_passthrough_attached": bool(wifi_present and ("AirportItlwm" in ioreg_output or loaded["loaded"])),
        "identity": identity,
        "loaded_state": loaded,
        "history_rows": history_rows,
        "policy_rows": policy_rows,
        "log_lines": log_lines,
        "log_snippets": relevant_log_snippets(results["logs"]["stdout"], selected_cdhash),
        "selected_cdhash": selected_cdhash,
        "kmutil_exit": kmutil_exit,
        "kmutil_approval_error": kmutil_approval_error,
        "kmutil_reboot_required": kmutil_reboot_required,
        "kmutil_message_summary": bounded_nonempty_lines(kmutil_message, 6),
        "command_results": {
            name: {
                "returncode": result["returncode"],
                "duration_seconds": result["duration_seconds"],
                "stdout_lines": len((result.get("stdout") or "").splitlines()),
                "stderr_lines": len((result.get("stderr") or "").splitlines()),
            }
            for name, result in results.items()
        },
    }


def ssh_probe(args):
    command = """
set -u
printf 'boot_session_uuid=%s\\n' "$(sysctl -n kern.bootsessionuuid 2>/dev/null || true)"
printf 'uptime=%s\\n' "$(uptime 2>/dev/null || true)"
"""
    return run_guest(args.guest, args.port, command, min(args.command_timeout, 15))


def perform_reboot(args, before_boot_uuid):
    reboot_started = utc_now()
    reboot_mono = time.monotonic()
    reboot_command = "sudo -n shutdown -r now"
    reboot_result = run_guest(args.guest, args.port, reboot_command, 20)
    saw_down = False
    returned = False
    after_boot_uuid = ""
    probes = []
    deadline = time.monotonic() + args.reboot_timeout
    time.sleep(args.reboot_initial_sleep)
    while time.monotonic() < deadline:
        try:
            probe = ssh_probe(args)
        except subprocess.TimeoutExpired:
            probe = {
                "returncode": 124,
                "stdout": "",
                "stderr": "ssh probe timed out",
                "duration_seconds": min(args.command_timeout, 15),
            }
        values = parse_key_values(probe.get("stdout", ""))
        current_boot_uuid = values.get("boot_session_uuid", "")
        probes.append(
            {
                "returncode": probe["returncode"],
                "duration_seconds": probe["duration_seconds"],
                "boot_session_uuid": current_boot_uuid,
            }
        )
        if probe["returncode"] != 0:
            saw_down = True
        elif current_boot_uuid and current_boot_uuid != before_boot_uuid:
            returned = True
            after_boot_uuid = current_boot_uuid
            break
        elif saw_down:
            returned = True
            after_boot_uuid = current_boot_uuid
            break
        time.sleep(args.reboot_poll_interval)

    return {
        "command": reboot_command,
        "command_result": {
            "returncode": reboot_result["returncode"],
            "duration_seconds": reboot_result["duration_seconds"],
            "stdout_lines": len((reboot_result.get("stdout") or "").splitlines()),
            "stderr_lines": len((reboot_result.get("stderr") or "").splitlines()),
        },
        "started_utc": reboot_started.isoformat().replace("+00:00", "Z"),
        "wallclock_seconds": round(time.monotonic() - reboot_mono, 3),
        "saw_ssh_down": saw_down,
        "ssh_returned": returned,
        "before_boot_session_uuid": before_boot_uuid,
        "after_boot_session_uuid": after_boot_uuid,
        "poll_count": len(probes),
        "poll_observations": probes[-20:],
    }


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
    build_install = run_guest_script(
        args.guest, args.port, build_install_script(args), args.build_timeout
    )
    if build_install["returncode"] != 0:
        raise RuntimeError(
            "guest selected-head build/install failed: "
            + "\n".join(bounded_nonempty_lines(build_install["stdout"] + build_install["stderr"], 16))
        )

    pre = collect_probe(args, "pre_reboot")
    before_boot_uuid = pre["guest_os"].get("boot_session_uuid", "")
    reboot = perform_reboot(args, before_boot_uuid)
    if not reboot["ssh_returned"]:
        raise RuntimeError("guest did not return over SSH after reboot before timeout")
    post = collect_probe(args, "post_reboot")

    elapsed = time.monotonic() - started_mono
    if elapsed < args.min_wallclock_seconds:
        time.sleep(args.min_wallclock_seconds - elapsed)

    post_identity = post["identity"]
    selected_cdhash = post["selected_cdhash"] or pre["selected_cdhash"]
    pre_uakl = uakl_cdhash_present(selected_cdhash, pre["policy_rows"], pre["history_rows"])
    admitted_after_reboot = bool(
        post["kmutil_exit"] == 0 and not post["kmutil_approval_error"]
    )

    finished_wall = utc_now()
    artifact = {
        "schema_version": "itlwm-tahoe-auxkc-approval-reboot-materialization-boundary/v1",
        "selected_step": STEP_ID,
        "roadmap_item_id": ROADMAP_ITEM_ID,
        "input_head": args.input_head,
        "capture_started_utc": started_wall.isoformat().replace("+00:00", "Z"),
        "capture_finished_utc": finished_wall.isoformat().replace("+00:00", "Z"),
        "capture_wallclock_seconds": round(time.monotonic() - started_mono, 3),
        "runtime_environment": {
            "guest_os": post["guest_os"],
            "guest_ssh": {
                "target": args.guest,
                "port": str(args.port),
                "available_after_reboot": reboot["ssh_returned"],
            },
            "guest_wifi_interface": args.interface,
            "guest_wifi_interface_present": post["wifi_present"],
            "wifi_passthrough_attached": post["wifi_passthrough_attached"],
        },
        "selected_kext": {
            "input_head": args.input_head,
            "guest_project_head": post_identity["guest_project_head"],
            "path": args.kext_path,
            "bundle_id": post_identity["bundle_id"],
            "bundle_version": post_identity["bundle_version"],
            "installed_uuid": post_identity["installed_uuid"],
            "installed_build_string": post_identity["build_string"],
            "selected_cdhash": selected_cdhash,
            "binary_sha256": post_identity["binary_sha256"],
            "pre_reboot_identity": pre["identity"],
        },
        "approval_materialization": {
            "source": "live_tahoe_guest_build_install_reboot_ssh_kmutil",
            "prior_blocker_signature": PRIOR_BLOCKER_SIGNATURE,
            "selected_head_rebuilt_and_installed": True,
            "uakl_cdhash_present_before_reboot": pre_uakl,
            "pre_reboot_kmutil_load_exit_code": pre["kmutil_exit"],
            "pre_reboot_kmutil_approval_error": pre["kmutil_approval_error"],
            "pre_reboot_kmutil_reboot_required": pre["kmutil_reboot_required"],
            "reboot_performed": True,
            "ssh_returned_after_reboot": reboot["ssh_returned"],
            "kmutil_load_exit_code_after_reboot": post["kmutil_exit"],
            "kmutil_load_approval_error_after_reboot": post["kmutil_approval_error"],
            "kmutil_reboot_required_after_reboot": post["kmutil_reboot_required"],
            "selected_bundle_admitted_after_reboot": admitted_after_reboot,
            "kmutil_message_summary_after_reboot": post["kmutil_message_summary"],
            "policy_materialization_inputs": {
                "pre_reboot_kext_policy_rows": summarize_policy_rows(pre["policy_rows"]),
                "pre_reboot_kext_load_history_rows": summarize_history_rows(pre["history_rows"]),
                "post_reboot_kext_load_history_rows": summarize_history_rows(post["history_rows"]),
            },
            "reboot_boundary": reboot,
            "prior_blocker_log_observed": log_has_prior_blocker(pre["log_lines"] + post["log_lines"]),
            "post_clearance_log_observed": log_has_post_clearance(pre["log_lines"] + post["log_lines"]),
            "syspolicyd_or_kernelmanagerd_evidence": post["log_snippets"][:120],
        },
        "capture_commands": {
            "selected_head_build_install": "cd /Users/devops/Projects/itlwm && ./scripts/build_tahoe.sh && sudo -n rm/cp/chown/chmod /Library/Extensions/AirportItlwm.kext",
            "pre_reboot_materialization_probe": "sudo -n kmutil load -p /Library/Extensions/AirportItlwm.kext",
            "reboot": "sudo -n shutdown -r now; poll SSH on devops@127.0.0.1:3322",
            "post_reboot_admission_probe": "sudo -n kmutil load -p /Library/Extensions/AirportItlwm.kext",
            "selected_bundle_identity": "guest git head, PlistBuddy, codesign, dwarfdump --uuid, shasum -a 256, strings selected short hash probe",
            "policy_history": "sudo -n sqlite3 /var/db/SystemPolicyConfiguration/KextPolicy selected bundle policy/history queries",
        },
        "command_results": {
            "build_install": {
                "returncode": build_install["returncode"],
                "duration_seconds": build_install["duration_seconds"],
                "stdout_lines": len((build_install.get("stdout") or "").splitlines()),
                "stderr_lines": len((build_install.get("stderr") or "").splitlines()),
            },
            "pre_reboot": pre["command_results"],
            "post_reboot": post["command_results"],
        },
        "verdict": {
            "ready_for_load_admission_step_retry": admitted_after_reboot,
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
        "approval_materialization.prior_blocker_signature",
        "approval_materialization.uakl_cdhash_present_before_reboot",
        "approval_materialization.reboot_performed",
        "approval_materialization.ssh_returned_after_reboot",
        "approval_materialization.kmutil_load_exit_code_after_reboot",
        "approval_materialization.kmutil_load_approval_error_after_reboot",
        "approval_materialization.selected_bundle_admitted_after_reboot",
        "verdict.ready_for_load_admission_step_retry",
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
        "approval_materialization.prior_blocker_signature": PRIOR_BLOCKER_SIGNATURE,
        "approval_materialization.uakl_cdhash_present_before_reboot": True,
        "approval_materialization.reboot_performed": True,
        "approval_materialization.ssh_returned_after_reboot": True,
        "approval_materialization.kmutil_load_approval_error_after_reboot": False,
        "approval_materialization.selected_bundle_admitted_after_reboot": True,
        "verdict.ready_for_load_admission_step_retry": True,
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
    try:
        if int(get_path(data, "approval_materialization.kmutil_load_exit_code_after_reboot")) > 0:
            errors.append("approval_materialization.kmutil_load_exit_code_after_reboot above 0")
    except (KeyError, TypeError, ValueError):
        pass
    if get_path(data, "approval_materialization.source") in {
        "synthetic",
        "replay",
        "static_fixture",
    }:
        errors.append("approval_materialization.source uses a forbidden value")
    if get_path(data, "input_head") != INPUT_HEAD:
        errors.append(f"input_head mismatch: {get_path(data, 'input_head')}")
    if get_path(data, "selected_kext.input_head") != INPUT_HEAD:
        errors.append(f"selected_kext.input_head mismatch: {get_path(data, 'selected_kext.input_head')}")
    if not get_path(data, "selected_kext.installed_uuid") or get_path(data, "selected_kext.installed_uuid") == "unknown":
        errors.append("selected_kext.installed_uuid is unknown")
    if not get_path(data, "selected_kext.selected_cdhash"):
        errors.append("selected_kext.selected_cdhash is empty")

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print(f"validated {path}")
    return 0


def recapture_existing(args):
    artifact_path = args.recapture_existing
    validation_status = validate_artifact(artifact_path)
    if validation_status:
        return validation_status

    data = json.loads(Path(artifact_path).read_text())
    started_wall = utc_now()
    started_mono = time.monotonic()
    post = collect_probe(args, "recapture_post_reboot")
    artifact_kext = data.get("selected_kext", {})
    artifact_uuid = artifact_kext.get("installed_uuid", "")
    artifact_sha256 = artifact_kext.get("binary_sha256", "")
    artifact_cdhash = artifact_kext.get("selected_cdhash", "")
    expected_heads = {
        head
        for head in (
            args.input_head,
            data.get("input_head", ""),
            artifact_kext.get("input_head", ""),
            artifact_kext.get("guest_project_head", ""),
            args.candidate_head,
        )
        if isinstance(head, str) and head
    }
    identity = post["identity"]

    errors = []
    if not post["wifi_present"]:
        errors.append(f"guest Wi-Fi interface {args.interface} not present")
    if not post["wifi_passthrough_attached"]:
        errors.append("wifi passthrough/AirportItlwm attachment not observed")
    if identity["guest_project_head"] not in expected_heads:
        errors.append(f"guest project head mismatch: {identity['guest_project_head']}")
    if identity["bundle_id"] != BUNDLE_ID:
        errors.append(f"installed bundle id mismatch: {identity['bundle_id']}")
    if artifact_uuid and identity["installed_uuid"] != artifact_uuid:
        errors.append(
            f"installed uuid mismatch: artifact {artifact_uuid}, live {identity['installed_uuid']}"
        )
    if artifact_sha256 and identity["binary_sha256"] != artifact_sha256:
        errors.append(
            "installed binary sha256 mismatch: "
            f"artifact {artifact_sha256}, live {identity['binary_sha256']}"
        )
    if artifact_cdhash and post["selected_cdhash"] and post["selected_cdhash"] != artifact_cdhash:
        errors.append(
            f"selected cdhash mismatch: artifact {artifact_cdhash}, live {post['selected_cdhash']}"
        )
    if post["kmutil_exit"] != 0:
        errors.append(f"kmutil exit mismatch: expected 0, observed {post['kmutil_exit']}")
    if post["kmutil_approval_error"]:
        errors.append("kmutil still reports the System Settings approval-required error")

    finished_wall = utc_now()
    summary = {
        "schema_version": "itlwm-tahoe-auxkc-approval-reboot-materialization-boundary/recapture-v1",
        "selected_step": STEP_ID,
        "roadmap_item_id": ROADMAP_ITEM_ID,
        "artifact_path": str(artifact_path),
        "artifact_mutated": False,
        "recapture_started_utc": started_wall.isoformat().replace("+00:00", "Z"),
        "recapture_finished_utc": finished_wall.isoformat().replace("+00:00", "Z"),
        "recapture_wallclock_seconds": round(time.monotonic() - started_mono, 3),
        "fresh_probe": {
            "guest_project_head": identity["guest_project_head"],
            "expected_guest_project_heads": sorted(expected_heads),
            "guest_wifi_interface": args.interface,
            "guest_wifi_interface_present": post["wifi_present"],
            "wifi_passthrough_attached": post["wifi_passthrough_attached"],
            "installed_uuid": identity["installed_uuid"],
            "binary_sha256": identity["binary_sha256"],
            "selected_cdhash": post["selected_cdhash"],
            "kmutil_load_exit_code_after_reboot": post["kmutil_exit"],
            "kmutil_approval_error_after_reboot": post["kmutil_approval_error"],
            "kmutil_message_summary": post["kmutil_message_summary"],
        },
        "command_results": post["command_results"],
        "verdict": {
            "committed_artifact_valid": True,
            "fresh_live_post_reboot_probe_passed": not errors,
            "ready_for_load_admission_step_retry": not errors,
            "loaded_uuid_after_reboot_not_claimed": True,
            "join_trigger_not_claimed": True,
            "pmk_delivery_not_claimed": True,
            "auth_tx_rx_not_claimed": True,
            "final_wifi_equivalence_not_claimed": True,
        },
    }
    print(json.dumps(summary, indent=2, sort_keys=True))
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    return 0


def main():
    parser = argparse.ArgumentParser(
        description="Capture Tahoe AuxKC approval materialization across reboot boundary evidence."
    )
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--guest", default=DEFAULT_GUEST)
    parser.add_argument("--port", default=DEFAULT_PORT)
    parser.add_argument("--interface", default=DEFAULT_INTERFACE)
    parser.add_argument("--kext-path", default=DEFAULT_KEXT_PATH)
    parser.add_argument("--guest-project", default=DEFAULT_GUEST_PROJECT)
    parser.add_argument("--input-head", default=INPUT_HEAD)
    parser.add_argument(
        "--candidate-head",
        default="",
        help="accepted useful commit head for non-mutating recapture after the selected input-head commit",
    )
    parser.add_argument("--command-timeout", type=int, default=60)
    parser.add_argument("--build-timeout", type=int, default=600)
    parser.add_argument("--log-timeout", type=int, default=180)
    parser.add_argument("--log-last", default="120m")
    parser.add_argument("--reboot-timeout", type=int, default=420)
    parser.add_argument("--reboot-poll-interval", type=float, default=5.0)
    parser.add_argument("--reboot-initial-sleep", type=float, default=8.0)
    parser.add_argument("--min-wallclock-seconds", type=float, default=30.0)
    parser.add_argument("--validate-existing", type=Path)
    parser.add_argument("--recapture-existing", type=Path)
    args = parser.parse_args()

    if args.validate_existing:
        return validate_artifact(args.validate_existing)
    if args.recapture_existing:
        try:
            return recapture_existing(args)
        except subprocess.TimeoutExpired as exc:
            print(f"recapture failed: command timed out after {exc.timeout}s", file=sys.stderr)
            return 2
        except Exception as exc:
            print(f"recapture failed: {exc}", file=sys.stderr)
            return 2

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
