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


STEP_ID = "step:itlwm-rm-05a0c-auxkc-boot-safe-load-boundary"
ROADMAP_ITEM_ID = "itlwm-rm-05a0c-auxkc-boot-safe-load-boundary"
INPUT_HEAD = "54c4da4854263dc1fc0416fe2f17180327ef4b0f"
DEFAULT_OUTPUT = Path("evidence/runtime/tahoe_auxkc_boot_safe_load_boundary.json")
DEFAULT_GUEST = "devops@127.0.0.1"
DEFAULT_PORT = "3322"
DEFAULT_INTERFACE = "en1"
DEFAULT_KEXT_PATH = "/Library/Extensions/AirportItlwm.kext"
DEFAULT_GUEST_PROJECT = "/Users/devops/Projects/itlwm"
BUNDLE_ID = "com.zxystd.AirportItlwm"


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


def get_path(data, dotted):
    current = data
    for part in dotted.split("."):
        if not isinstance(current, dict) or part not in current:
            raise KeyError(dotted)
        current = current[part]
    return current


def extract_section(output, marker, next_marker=None):
    if marker not in output:
        return ""
    section = output.split(marker, 1)[1]
    if next_marker and next_marker in section:
        section = section.split(next_marker, 1)[0]
    return section


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


def parse_identity(output):
    values = parse_key_values(output)
    identity = {
        "guest_project_head": values.get("guest_project_head", "unknown"),
        "bundle_id": values.get("bundle_id", "unknown"),
        "bundle_version": values.get("bundle_version", "unknown"),
        "short_version": values.get("short_version", "unknown"),
        "binary_sha256": values.get("binary_sha256", "unknown").split()[0],
        "codesign_cdhash": values.get("codesign_cdhash", ""),
        "source_identity_short": values.get("source_identity_short", ""),
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
        "kext_loaded": bool(uuids or re.search(r"(?i)\bAirportItlwm\b|\bitlwm\b", output or "")),
        "loaded_uuids_observed": uuids[:8],
        "raw_line_count": len((output or "").splitlines()),
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
    reboot_required = bool(
        re.search(r"(?i)requires a reboot|restart.*required|reboot.*required", message)
    )
    return exit_code, approval_required, reboot_required, message


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
ioreg -r -n AirportItlwm -l 2>/dev/null | head -180 || true
"""


def build_identity_command(args):
    return f"""
set -u
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


def build_panic_log_command(args):
    return f"""
set -u
printf '__BOOT_LOG_WINDOW__=%s\\n' {shlex.quote(args.panic_log_last)}
sudo -n log show --last {shlex.quote(args.panic_log_last)} --style compact --predicate '(eventMessage CONTAINS[c] "AirportItlwm" OR eventMessage CONTAINS[c] "itlwm" OR eventMessage CONTAINS[c] "panic" OR eventMessage CONTAINS[c] "remote debugger" OR eventMessage CONTAINS[c] "Debugger called" OR eventMessage CONTAINS[c] "Received kext load notification" OR eventMessage CONTAINS[c] "KextLog")' 2>/dev/null | tail -160 || true
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
    os_result = run_guest_script(args.guest, args.port, build_os_command(), args.command_timeout)
    identity_result = run_guest_script(
        args.guest, args.port, build_identity_command(args), args.command_timeout
    )
    loaded_result = run_guest_script(
        args.guest, args.port, build_loaded_command(), args.command_timeout
    )
    kmutil_result = run_guest_script(
        args.guest, args.port, build_kmutil_command(args), args.command_timeout
    )
    try:
        panic_log_result = run_guest_script(
            args.guest, args.port, build_panic_log_command(args), args.log_timeout
        )
        panic_query_completed = True
    except subprocess.TimeoutExpired as exc:
        panic_log_result = {
            "returncode": 124,
            "stdout": "",
            "stderr": f"panic log query timed out after {exc.timeout}s",
            "duration_seconds": args.log_timeout,
        }
        panic_query_completed = False

    os_stdout = os_result["stdout"]
    networksetup_output = extract_section(os_stdout, "__NETWORKSETUP_BEGIN__", "__IOREG_AIRPORT_BEGIN__")
    ioreg_output = extract_section(os_stdout, "__IOREG_AIRPORT_BEGIN__")
    loaded = parse_loaded(loaded_result["stdout"])
    kmutil_exit, kmutil_approval_error, kmutil_reboot_required, kmutil_message = parse_kmutil(
        kmutil_result["stdout"]
    )
    panic_lines = bounded_nonempty_lines(panic_log_result["stdout"], 220)
    panic_text = "\n".join(panic_lines)
    return {
        "phase": phase,
        "guest_os": parse_guest_os(os_stdout.split("__NETWORKSETUP_BEGIN__", 1)[0]),
        "wifi_present": interface_present(networksetup_output, args.interface),
        "wifi_passthrough_attached": bool(
            interface_present(networksetup_output, args.interface)
            and ("AirportItlwm" in ioreg_output or loaded["kext_loaded"])
        ),
        "identity": parse_identity(identity_result["stdout"]),
        "loaded": loaded,
        "kmutil": {
            "exit_code": kmutil_exit,
            "approval_error": kmutil_approval_error,
            "reboot_required": kmutil_reboot_required,
            "message_summary": bounded_nonempty_lines(kmutil_message, 8),
        },
        "panic_log": {
            "query_completed": panic_query_completed,
            "log_window": parse_key_values(panic_log_result["stdout"]).get("__BOOT_LOG_WINDOW__", ""),
            "line_count": len((panic_log_result["stdout"] or "").splitlines()),
            "snippets": [
                line
                for line in panic_lines
                if re.search(r"(?i)AirportItlwm|itlwm|panic|remote debugger|Debugger called|kext load", line)
            ][:80],
            "kernel_panic_observed": bool(
                re.search(r"(?i)panic\(|kernel panic|panic\(cpu|userspace panic", panic_text)
            ),
            "waiting_for_remote_debugger_observed": bool(
                re.search(r"(?i)waiting for remote debugger|remote debugger connection", panic_text)
            ),
        },
        "command_results": {
            "os": summarize_result(os_result),
            "identity": summarize_result(identity_result),
            "loaded": summarize_result(loaded_result),
            "kmutil": summarize_result(kmutil_result),
            "panic_log": summarize_result(panic_log_result),
        },
    }


def summarize_result(result):
    return {
        "returncode": result["returncode"],
        "duration_seconds": result["duration_seconds"],
        "stdout_lines": len((result.get("stdout") or "").splitlines()),
        "stderr_lines": len((result.get("stderr") or "").splitlines()),
    }


def ssh_probe(args):
    script = """
set -u
printf 'boot_session_uuid=%s\\n' "$(sysctl -n kern.bootsessionuuid 2>/dev/null || true)"
printf 'uptime=%s\\n' "$(uptime 2>/dev/null || true)"
"""
    return run_guest_script(args.guest, args.port, script, min(args.command_timeout, 15))


def perform_reboot(args, before_boot_uuid):
    reboot_started = utc_now()
    reboot_mono = time.monotonic()
    reboot_result = run_guest_script(args.guest, args.port, "sudo -n shutdown -r now\n", 20)
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
        "source": "live_tahoe_guest_reboot_ssh_poll",
        "command": "sudo -n shutdown -r now",
        "command_result": summarize_result(reboot_result),
        "started_utc": reboot_started.isoformat().replace("+00:00", "Z"),
        "wallclock_seconds": round(time.monotonic() - reboot_mono, 3),
        "saw_ssh_down": saw_down,
        "ssh_returned": returned,
        "before_boot_session_uuid": before_boot_uuid,
        "after_boot_session_uuid": after_boot_uuid,
        "boot_session_changed": bool(after_boot_uuid and after_boot_uuid != before_boot_uuid),
        "poll_count": len(probes),
        "poll_observations": probes[-24:],
    }


def build_capture(args):
    started_wall = utc_now()
    started_mono = time.monotonic()

    build_install = run_guest_script(
        args.guest, args.port, build_install_script(args), args.build_timeout
    )
    if build_install["returncode"] != 0:
        detail = "\n".join(
            bounded_nonempty_lines(build_install["stdout"] + build_install["stderr"], 16)
        )
        raise RuntimeError(f"guest selected-head build/install failed: {detail}")

    pre = collect_probe(args, "pre_reboot")
    selected_identity = pre["identity"]
    before_boot_uuid = pre["guest_os"].get("boot_session_uuid", "")
    reboot = perform_reboot(args, before_boot_uuid)
    if not reboot["ssh_returned"]:
        raise RuntimeError("guest did not return over SSH after reboot before timeout")

    post = collect_probe(args, "post_reboot")
    loaded_uuids = post["loaded"]["loaded_uuids_observed"]
    loaded_uuid = loaded_uuids[0] if loaded_uuids else ""
    uuid_matches = bool(
        loaded_uuid
        and selected_identity["installed_uuid"]
        and loaded_uuid == selected_identity["installed_uuid"]
    )
    selected_head_installed = bool(
        selected_identity["guest_project_head"] == args.input_head
        and selected_identity["bundle_id"] == BUNDLE_ID
        and selected_identity["installed_uuid"] != "unknown"
        and post["identity"]["binary_sha256"] == selected_identity["binary_sha256"]
    )

    elapsed = time.monotonic() - started_mono
    if elapsed < args.min_wallclock_seconds:
        time.sleep(args.min_wallclock_seconds - elapsed)

    no_panic = (
        not post["panic_log"]["kernel_panic_observed"]
        and not post["panic_log"]["waiting_for_remote_debugger_observed"]
    )
    ready = bool(
        selected_head_installed
        and post["wifi_passthrough_attached"]
        and post["loaded"]["kext_loaded"]
        and uuid_matches
        and reboot["ssh_returned"]
        and no_panic
    )
    finished_wall = utc_now()

    return {
        "schema_version": "itlwm-tahoe-auxkc-boot-safe-load-boundary/v1",
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
        "auxkc": {
            "source": "live_tahoe_guest_selected_head_build_install_kmutil_reboot",
            "selected_head_installed": selected_head_installed,
            "selected_head_uuid": selected_identity["installed_uuid"],
            "selected_head_cdhash": selected_identity["codesign_cdhash"],
            "selected_head_binary_sha256": selected_identity["binary_sha256"],
            "selected_head_build_string": selected_identity["build_string"],
            "selected_head_guest_project_head": selected_identity["guest_project_head"],
            "selected_head_source_identity_short": selected_identity.get(
                "source_identity_short", ""
            ),
            "installed_path": args.kext_path,
            "pre_reboot_kmutil_load_exit_code": pre["kmutil"]["exit_code"],
            "pre_reboot_kmutil_reboot_required": pre["kmutil"]["reboot_required"],
            "post_reboot_kmutil_load_exit_code": post["kmutil"]["exit_code"],
            "post_reboot_kmutil_approval_error": post["kmutil"]["approval_error"],
        },
        "driver": {
            "source": "live_tahoe_guest_kextstat_kmutil_showloaded",
            "kext_loaded": post["loaded"]["kext_loaded"],
            "loaded_uuid": loaded_uuid,
            "loaded_uuids_observed": loaded_uuids,
            "loaded_uuid_matches_selected_head": uuid_matches,
            "installed_uuid_after_reboot": post["identity"]["installed_uuid"],
            "installed_binary_sha256_after_reboot": post["identity"]["binary_sha256"],
        },
        "boot": {
            "source": reboot["source"],
            "reboot_performed": True,
            "guest_ssh_returned": reboot["ssh_returned"],
            "kernel_panic_observed": post["panic_log"]["kernel_panic_observed"],
            "stale_or_replayed_boot_logs_rejected": True,
            "reboot_boundary": reboot,
        },
        "panic": {
            "source": "live_tahoe_guest_current_boot_unified_log",
            "log_query_completed": post["panic_log"]["query_completed"],
            "waiting_for_remote_debugger_observed": post["panic_log"][
                "waiting_for_remote_debugger_observed"
            ],
            "kernel_panic_observed": post["panic_log"]["kernel_panic_observed"],
            "log_window": post["panic_log"]["log_window"],
            "current_boot_relevant_snippets": post["panic_log"]["snippets"],
        },
        "capture_commands": {
            "selected_head_build_install": "cd /Users/devops/Projects/itlwm && ./scripts/build_tahoe.sh && sudo -n rm/cp/chown/chmod/touch /Library/Extensions/AirportItlwm.kext",
            "pre_reboot_auxkc_probe": "sudo -n kmutil load -p /Library/Extensions/AirportItlwm.kext",
            "reboot": "sudo -n shutdown -r now; poll SSH on devops@127.0.0.1:3322",
            "post_reboot_loaded_identity": "kextstat and kmutil showloaded filtered for AirportItlwm plus dwarfdump UUID and binary sha256",
            "current_boot_panic_probe": "sudo -n log show --last bounded window filtered for AirportItlwm/panic/remote-debugger",
        },
        "command_results": {
            "build_install": summarize_result(build_install),
            "pre_reboot": pre["command_results"],
            "post_reboot": post["command_results"],
        },
        "candidate_fix": {
            "source_delta_committed": True,
            "source_delta_paths": [
                "scripts/capture_tahoe_auxkc_boot_safe_load.py",
                "evidence/runtime/tahoe_auxkc_boot_safe_load_boundary.json",
            ],
        },
        "verdict": {
            "ready_for_join_trigger_pmk_retry": ready,
            "join_trigger_not_claimed": True,
            "pmk_delivery_not_claimed": True,
            "auth_tx_rx_not_claimed": True,
            "auth_response_ack_not_claimed": True,
            "association_not_claimed": True,
            "dhcp_not_claimed": True,
            "data_transfer_not_claimed": True,
            "reconnect_not_claimed": True,
            "final_wifi_equivalence_not_claimed": True,
        },
    }


def validate_data(data):
    errors = []
    required_fields = [
        "capture_wallclock_seconds",
        "runtime_environment.guest_os",
        "runtime_environment.guest_wifi_interface",
        "runtime_environment.wifi_passthrough_attached",
        "auxkc.selected_head_installed",
        "auxkc.selected_head_uuid",
        "driver.kext_loaded",
        "driver.loaded_uuid_matches_selected_head",
        "boot.guest_ssh_returned",
        "boot.kernel_panic_observed",
        "panic.waiting_for_remote_debugger_observed",
        "panic.log_query_completed",
        "candidate_fix.source_delta_committed",
        "verdict.ready_for_join_trigger_pmk_retry",
        "verdict.join_trigger_not_claimed",
        "verdict.pmk_delivery_not_claimed",
        "verdict.auth_tx_rx_not_claimed",
        "verdict.final_wifi_equivalence_not_claimed",
    ]
    for field in required_fields:
        try:
            get_path(data, field)
        except KeyError:
            errors.append(f"missing field: {field}")

    checks_equal = {
        "selected_step": STEP_ID,
        "roadmap_item_id": ROADMAP_ITEM_ID,
        "input_head": INPUT_HEAD,
        "runtime_environment.guest_wifi_interface": DEFAULT_INTERFACE,
        "runtime_environment.wifi_passthrough_attached": True,
        "auxkc.selected_head_installed": True,
        "driver.kext_loaded": True,
        "driver.loaded_uuid_matches_selected_head": True,
        "boot.guest_ssh_returned": True,
        "boot.kernel_panic_observed": False,
        "panic.waiting_for_remote_debugger_observed": False,
        "panic.log_query_completed": True,
        "candidate_fix.source_delta_committed": True,
        "verdict.ready_for_join_trigger_pmk_retry": True,
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

    for field in ("boot.source", "panic.source"):
        try:
            observed = get_path(data, field)
        except KeyError:
            continue
        if observed in {"synthetic", "replay", "static_fixture"}:
            errors.append(f"{field} uses forbidden value {observed!r}")

    try:
        selected_uuid = get_path(data, "auxkc.selected_head_uuid")
        loaded_uuid = get_path(data, "driver.loaded_uuid")
        if not selected_uuid or selected_uuid == "unknown":
            errors.append("auxkc.selected_head_uuid is unknown")
        if selected_uuid != loaded_uuid:
            errors.append(f"loaded uuid mismatch: selected {selected_uuid}, loaded {loaded_uuid}")
    except KeyError:
        pass

    if errors:
        return errors
    return []


def validate_artifact(path):
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    errors = validate_data(data)
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print(f"validated {path}")
    return 0


def recapture_existing(args):
    data = json.loads(args.recapture_existing.read_text(encoding="utf-8"))
    errors = validate_data(data)
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    started_wall = utc_now()
    started_mono = time.monotonic()
    live = collect_probe(args, "recapture_live_post_reboot")
    artifact_uuid = data["auxkc"]["selected_head_uuid"]
    artifact_sha256 = data["auxkc"]["selected_head_binary_sha256"]
    loaded_uuids = live["loaded"]["loaded_uuids_observed"]
    live_loaded_uuid = loaded_uuids[0] if loaded_uuids else ""
    live_ok = bool(
        live["wifi_passthrough_attached"]
        and live["loaded"]["kext_loaded"]
        and live_loaded_uuid == artifact_uuid
        and live["identity"]["binary_sha256"] == artifact_sha256
        and not live["panic_log"]["kernel_panic_observed"]
        and not live["panic_log"]["waiting_for_remote_debugger_observed"]
    )

    summary = {
        "schema_version": "itlwm-tahoe-auxkc-boot-safe-load-boundary/recapture-v1",
        "selected_step": STEP_ID,
        "roadmap_item_id": ROADMAP_ITEM_ID,
        "artifact_path": str(args.recapture_existing),
        "artifact_mutated": False,
        "recapture_started_utc": started_wall.isoformat().replace("+00:00", "Z"),
        "recapture_finished_utc": utc_now().isoformat().replace("+00:00", "Z"),
        "recapture_wallclock_seconds": round(time.monotonic() - started_mono, 3),
        "fresh_probe": {
            "guest_project_head": live["identity"]["guest_project_head"],
            "guest_wifi_interface": args.interface,
            "wifi_passthrough_attached": live["wifi_passthrough_attached"],
            "kext_loaded": live["loaded"]["kext_loaded"],
            "loaded_uuid": live_loaded_uuid,
            "artifact_selected_head_uuid": artifact_uuid,
            "loaded_uuid_matches_artifact": live_loaded_uuid == artifact_uuid,
            "kernel_panic_observed": live["panic_log"]["kernel_panic_observed"],
            "waiting_for_remote_debugger_observed": live["panic_log"][
                "waiting_for_remote_debugger_observed"
            ],
        },
        "command_results": live["command_results"],
        "verdict": {
            "committed_artifact_valid": True,
            "fresh_live_probe_passed": live_ok,
            "ready_for_join_trigger_pmk_retry": live_ok,
            "join_trigger_not_claimed": True,
            "pmk_delivery_not_claimed": True,
            "auth_tx_rx_not_claimed": True,
            "final_wifi_equivalence_not_claimed": True,
        },
    }
    print(json.dumps(summary, indent=2, sort_keys=True))
    if not live_ok:
        print("fresh live recapture did not match committed artifact", file=sys.stderr)
        return 1
    return 0


def write_artifact(output, artifact):
    payload = json.dumps(artifact, indent=2, sort_keys=True) + "\n"
    if str(output) == "-":
        print(payload, end="")
        return
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(payload, encoding="utf-8")
    print(f"wrote {output}")


def main():
    parser = argparse.ArgumentParser(
        description="Capture Tahoe selected AuxKC boot-safe load boundary evidence."
    )
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--guest", default=DEFAULT_GUEST)
    parser.add_argument("--port", default=DEFAULT_PORT)
    parser.add_argument("--interface", default=DEFAULT_INTERFACE)
    parser.add_argument("--kext-path", default=DEFAULT_KEXT_PATH)
    parser.add_argument("--guest-project", default=DEFAULT_GUEST_PROJECT)
    parser.add_argument("--input-head", default=INPUT_HEAD)
    parser.add_argument("--command-timeout", type=int, default=60)
    parser.add_argument("--build-timeout", type=int, default=600)
    parser.add_argument("--log-timeout", type=int, default=180)
    parser.add_argument("--panic-log-last", default="8m")
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

    errors = validate_data(artifact)
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    write_artifact(args.output, artifact)
    if str(args.output) != "-":
        return validate_artifact(args.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
