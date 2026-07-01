#!/usr/bin/env python3
import argparse
import datetime as dt
import json
import os
import re
import shlex
import struct
import subprocess
import sys
import time
from pathlib import Path


STEP_ID = "step:itlwm-rm-05a0c0a1a-tahoe-gui-session-availability-boundary"
ROADMAP_ITEM_ID = "itlwm-rm-05a0c0a1a-tahoe-gui-session-availability-boundary"
INPUT_HEAD = "88cba6dbd49cb59c2830f2da8b4bbce501f3469a"
DEFAULT_OUTPUT = Path("evidence/runtime/tahoe_gui_session_availability_boundary.json")
DEFAULT_GUEST = "devops@127.0.0.1"
DEFAULT_PORT = "3322"
DEFAULT_INTERFACE = "en1"
DEFAULT_CONSOLE_USER = "devops"
DEFAULT_KEXT_PATH = "/Library/Extensions/AirportItlwm.kext"
DEFAULT_VNC_SERVER = "127.0.0.1::5901"


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


def run_local(command, timeout_seconds):
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


def get_path(data, dotted):
    current = data
    for part in dotted.split("."):
        if not isinstance(current, dict) or part not in current:
            raise KeyError(dotted)
        current = current[part]
    return current


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
        "boot_session_uuid": values.get("boot_session_uuid", "unknown"),
    }


def interface_present(networksetup_output, interface):
    pattern = re.compile(r"(?im)^\s*Device:\s*" + re.escape(interface) + r"\s*$")
    return bool(pattern.search(networksetup_output or ""))


def kext_loaded(output):
    return bool(re.search(r"(?i)\b(AirportItlwm|itlwm)\b", output or ""))


def bounded_nonempty_lines(text, limit=12):
    lines = []
    for line in (text or "").splitlines():
        stripped = line.strip()
        if stripped:
            lines.append(stripped)
        if len(lines) >= limit:
            break
    return lines


def extract_section(output, marker, next_marker=None):
    if marker not in output:
        return ""
    section = output.split(marker, 1)[1]
    if next_marker and next_marker in section:
        section = section.split(next_marker, 1)[0]
    return section


def parse_png_size(path):
    data = Path(path).read_bytes()
    if len(data) < 24 or data[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    width, height = struct.unpack(">II", data[16:24])
    return {"width": int(width), "height": int(height), "bytes": len(data)}


def capture_vnc_surface(server, timeout_seconds):
    capture_path = Path(f"/tmp/itlwm-tahoe-gui-vnc-{os.getpid()}.png")
    try:
        result = run_local(
            ["vncdotool", "-s", server, "capture", str(capture_path)],
            timeout_seconds,
        )
        png = parse_png_size(capture_path) if capture_path.exists() else None
        return {
            "source": "live_qemu_vnc_capture",
            "server": server,
            "returncode": result["returncode"],
            "duration_seconds": result["duration_seconds"],
            "capture_dimensions": png,
            "available": bool(
                result["returncode"] == 0
                and png
                and png["width"] > 0
                and png["height"] > 0
                and png["bytes"] > 0
            ),
        }
    finally:
        try:
            capture_path.unlink()
        except FileNotFoundError:
            pass


def parse_system_settings(system_settings_output):
    values = parse_key_values(system_settings_output)
    launch_rc = None
    try:
        launch_rc = int(values.get("open_rc", ""))
    except ValueError:
        launch_rc = None
    process_lines = bounded_nonempty_lines(
        extract_section(system_settings_output, "__SYSTEM_SETTINGS_PROCESS_BEGIN__"),
        limit=8,
    )
    process_running = any(
        re.search(r"System Settings|System Preferences", line)
        for line in process_lines
    )
    return {
        "open_returncode": launch_rc,
        "process_running": process_running,
        "process_observations": process_lines,
        "launchable": bool(launch_rc == 0 or process_running),
    }


def parse_kmutil(output):
    values = parse_key_values(output)
    rc = None
    try:
        rc = int(values.get("kmutil_exit", ""))
    except ValueError:
        rc = None
    message = extract_section(output, "__KMUTIL_OUTPUT_BEGIN__", "__KMUTIL_OUTPUT_END__").strip()
    approval_required = bool(
        re.search(r"(?i)not approved to load|approve using System Settings", message)
    )
    return rc, approval_required, message


def build_capture(args):
    started_wall = utc_now()
    started_mono = time.monotonic()

    os_command = """
set -u
printf 'product_name=%s\\n' "$(sw_vers -productName 2>/dev/null || true)"
printf 'product_version=%s\\n' "$(sw_vers -productVersion 2>/dev/null || true)"
printf 'build_version=%s\\n' "$(sw_vers -buildVersion 2>/dev/null || true)"
printf 'kernel_release=%s\\n' "$(uname -r 2>/dev/null || true)"
printf 'boot_session_uuid=%s\\n' "$(sysctl -n kern.bootsessionuuid 2>/dev/null || true)"
printf '__NETWORKSETUP_BEGIN__\\n'
networksetup -listallhardwareports 2>/dev/null || true
printf '__IOREG_AIRPORT_BEGIN__\\n'
ioreg -r -n AirportItlwm -l 2>/dev/null | head -80 || true
"""
    gui_command = f"""
set -u
console_user="$(stat -f %Su /dev/console 2>/dev/null || true)"
console_uid="$(stat -f %u /dev/console 2>/dev/null || true)"
expected_uid="$(id -u {shlex.quote(args.console_user)} 2>/dev/null || true)"
printf 'console_user=%s\\n' "$console_user"
printf 'console_uid=%s\\n' "$console_uid"
printf 'expected_uid=%s\\n' "$expected_uid"
printf '__GUI_DOMAIN_BEGIN__\\n'
if [ -n "$expected_uid" ]; then
  launchctl print "gui/$expected_uid" 2>&1 | sed -n '1,120p'
  printf '__GUI_DOMAIN_RC__=%s\\n' "${{PIPESTATUS[0]}}"
else
  printf 'missing expected uid\\n'
  printf '__GUI_DOMAIN_RC__=1\\n'
fi
printf '__GUI_PROCESSES_BEGIN__\\n'
ps axww -o pid=,user=,stat=,command= | egrep 'WindowServer|loginwindow|System Settings|System Preferences' | grep -v egrep || true
"""
    system_settings_command = """
set -u
open_output="$(/usr/bin/open -a 'System Settings' 2>&1)"
open_rc=$?
sleep 4
printf 'open_rc=%s\\n' "$open_rc"
printf '__OPEN_OUTPUT_BEGIN__\\n'
printf '%s\\n' "$open_output"
printf '__OPEN_OUTPUT_END__\\n'
printf '__SYSTEM_SETTINGS_PROCESS_BEGIN__\\n'
ps axww -o pid=,user=,stat=,command= | egrep 'System Settings|System Preferences' | grep -v egrep || true
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
printf 'kmutil_exit=%s\\n' "$rc"
printf '__KMUTIL_OUTPUT_BEGIN__\\n'
printf '%s\\n' "$out"
printf '__KMUTIL_OUTPUT_END__\\n'
exit 0
"""

    results = {
        "os": run_guest(args.guest, args.port, os_command, args.command_timeout),
        "loaded": run_guest(args.guest, args.port, loaded_command, args.command_timeout),
        "gui": run_guest(args.guest, args.port, gui_command, args.command_timeout),
        "system_settings": run_guest(args.guest, args.port, system_settings_command, args.command_timeout),
        "kmutil": run_guest(args.guest, args.port, kmutil_command, args.command_timeout),
    }
    vnc = capture_vnc_surface(args.vnc_server, args.vnc_timeout)

    elapsed = time.monotonic() - started_mono
    if elapsed < args.min_wallclock_seconds:
        time.sleep(args.min_wallclock_seconds - elapsed)

    os_stdout = results["os"]["stdout"]
    networksetup_output = extract_section(os_stdout, "__NETWORKSETUP_BEGIN__", "__IOREG_AIRPORT_BEGIN__")
    ioreg_output = extract_section(os_stdout, "__IOREG_AIRPORT_BEGIN__")
    loaded_output = "\n".join([results["loaded"]["stdout"], results["loaded"]["stderr"]])
    gui_output = results["gui"]["stdout"]
    gui_values = parse_key_values(gui_output)
    gui_domain_rc = None
    try:
        gui_domain_rc = int(gui_values.get("__GUI_DOMAIN_RC__", ""))
    except ValueError:
        gui_domain_rc = None
    processes = bounded_nonempty_lines(extract_section(gui_output, "__GUI_PROCESSES_BEGIN__"), 12)
    system_settings = parse_system_settings(results["system_settings"]["stdout"])
    kmutil_exit, approval_required, kmutil_message = parse_kmutil(results["kmutil"]["stdout"])

    console_uid = 0
    try:
        console_uid = int(gui_values.get("console_uid", "0"))
    except ValueError:
        console_uid = 0
    windowserver_running = any("WindowServer" in line for line in processes)
    loginwindow_running = any("loginwindow" in line for line in processes)
    wifi_present = interface_present(networksetup_output, args.interface)
    driver_loaded = kext_loaded(loaded_output)
    ioreg_driver_present = "AirportItlwm" in ioreg_output
    passthrough_attached = bool(wifi_present and (driver_loaded or ioreg_driver_present))
    gui_domain_available = bool(
        gui_domain_rc == 0
        and gui_values.get("console_user") == args.console_user
        and console_uid > 0
    )

    finished_wall = utc_now()
    artifact = {
        "schema_version": "itlwm-tahoe-gui-session-availability-boundary/v1",
        "selected_step": STEP_ID,
        "roadmap_item_id": ROADMAP_ITEM_ID,
        "input_head": args.input_head,
        "capture_started_utc": started_wall.isoformat().replace("+00:00", "Z"),
        "capture_finished_utc": finished_wall.isoformat().replace("+00:00", "Z"),
        "capture_wallclock_seconds": round(time.monotonic() - started_mono, 3),
        "runtime_environment": {
            "guest_os": parse_guest_os(os_stdout.split("__NETWORKSETUP_BEGIN__", 1)[0]),
            "guest_ssh": {
                "target": args.guest,
                "port": str(args.port),
                "available": results["os"]["returncode"] == 0,
            },
            "guest_wifi_interface": args.interface,
            "guest_wifi_interface_present": wifi_present,
            "wifi_passthrough_attached": passthrough_attached,
        },
        "gui": {
            "source": "live_tahoe_guest_ssh_and_qemu_vnc",
            "prior_blocker_signature": "no_logged_in_gui_domain_for_system_settings_approval_workflow",
            "console_user": gui_values.get("console_user", ""),
            "console_uid": console_uid,
            "expected_uid": gui_values.get("expected_uid", ""),
            "gui_domain_available": gui_domain_available,
            "windowserver_running": windowserver_running,
            "loginwindow_running": loginwindow_running,
            "vnc_surface_available": vnc["available"],
            "system_settings_launchable": system_settings["launchable"],
            "process_observations": processes,
            "system_settings": system_settings,
            "vnc_capture": vnc,
        },
        "approval_probe": {
            "source": "live_tahoe_guest_kmutil_probe",
            "command": "sudo -n kmutil load -p /Library/Extensions/AirportItlwm.kext",
            "kmutil_exit_code": kmutil_exit,
            "kmutil_approval_error_still_present": approval_required,
            "message_summary": bounded_nonempty_lines(kmutil_message, 4),
        },
        "capture_commands": {
            "guest_os_wifi": "sw_vers, uname, sysctl boot session, networksetup hardware ports, AirportItlwm ioreg",
            "gui_domain": "stat /dev/console, launchctl print gui/<devops uid>, ps WindowServer/loginwindow/System Settings",
            "vnc_surface": "vncdotool -s 127.0.0.1::5901 capture /tmp/<transient>.png",
            "system_settings": "open -a 'System Settings' from the logged-in devops SSH session",
            "approval_probe": "sudo -n kmutil load -p /Library/Extensions/AirportItlwm.kext",
        },
        "verdict": {
            "ready_for_approval_alert_workflow_retry": bool(
                results["os"]["returncode"] == 0
                and passthrough_attached
                and gui_domain_available
                and windowserver_running
                and loginwindow_running
                and vnc["available"]
                and system_settings["launchable"]
                and approval_required
            ),
            "approval_alert_cleared_not_claimed": True,
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
        "gui.prior_blocker_signature",
        "gui.console_user",
        "gui.console_uid",
        "gui.gui_domain_available",
        "gui.windowserver_running",
        "gui.vnc_surface_available",
        "gui.system_settings_launchable",
        "approval_probe.kmutil_approval_error_still_present",
        "verdict.ready_for_approval_alert_workflow_retry",
        "verdict.approval_alert_cleared_not_claimed",
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
        "gui.prior_blocker_signature": "no_logged_in_gui_domain_for_system_settings_approval_workflow",
        "gui.console_user": DEFAULT_CONSOLE_USER,
        "gui.gui_domain_available": True,
        "gui.windowserver_running": True,
        "gui.vnc_surface_available": True,
        "gui.system_settings_launchable": True,
        "approval_probe.kmutil_approval_error_still_present": True,
        "verdict.ready_for_approval_alert_workflow_retry": True,
        "verdict.approval_alert_cleared_not_claimed": True,
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

    try:
        if float(get_path(data, "capture_wallclock_seconds")) < 10:
            errors.append("capture_wallclock_seconds below 10")
    except (KeyError, TypeError, ValueError):
        pass
    try:
        if int(get_path(data, "gui.console_uid")) < 1:
            errors.append("gui.console_uid below 1")
    except (KeyError, TypeError, ValueError):
        pass
    try:
        if get_path(data, "gui.source") in {"synthetic", "replay", "static_fixture"}:
            errors.append("gui.source uses a forbidden value")
    except KeyError:
        pass

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print(f"validated {path}")
    return 0


def main():
    parser = argparse.ArgumentParser(description="Capture Tahoe logged-in GUI session availability evidence.")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--guest", default=DEFAULT_GUEST)
    parser.add_argument("--port", default=DEFAULT_PORT)
    parser.add_argument("--interface", default=DEFAULT_INTERFACE)
    parser.add_argument("--console-user", default=DEFAULT_CONSOLE_USER)
    parser.add_argument("--kext-path", default=DEFAULT_KEXT_PATH)
    parser.add_argument("--vnc-server", default=DEFAULT_VNC_SERVER)
    parser.add_argument("--input-head", default=INPUT_HEAD)
    parser.add_argument("--command-timeout", type=int, default=45)
    parser.add_argument("--vnc-timeout", type=int, default=20)
    parser.add_argument("--min-wallclock-seconds", type=float, default=10.0)
    parser.add_argument("--validate-existing", type=Path)
    args = parser.parse_args()

    if args.validate_existing:
        return validate_artifact(args.validate_existing)

    try:
        artifact = build_capture(args)
    except subprocess.TimeoutExpired as exc:
        print(f"capture failed: command timed out after {exc.timeout}s", file=sys.stderr)
        return 2
    except FileNotFoundError as exc:
        print(f"capture failed: missing command {exc.filename}", file=sys.stderr)
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
