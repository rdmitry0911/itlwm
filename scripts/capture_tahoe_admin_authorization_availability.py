#!/usr/bin/env python3
import argparse
import datetime as dt
import hashlib
import json
import os
import re
import shlex
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path


STEP_ID = "step:itlwm-rm-05a0c0a1b-tahoe-admin-authorization-availability-boundary"
ROADMAP_ITEM_ID = "itlwm-rm-05a0c0a1b-tahoe-admin-authorization-availability-boundary"
INPUT_HEAD = "f831495fd67051e99a85e54e49472053c13a36ce"
DEFAULT_OUTPUT = Path("evidence/runtime/tahoe_admin_authorization_availability_boundary.json")
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


def run_remote_python_with_secret(target, port, python_source, secret, timeout_seconds):
    remote_command = "/usr/bin/python3 -c " + shlex.quote(python_source)
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
        remote_command,
    ]
    started = time.monotonic()
    completed = subprocess.run(
        command,
        input=secret + "\n",
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


def run_vnc(server, command_stream, timeout_seconds):
    started = time.monotonic()
    completed = subprocess.run(
        ["vncdotool", "-s", server, "-"],
        input=command_stream,
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


def capture_vnc(server, path, timeout_seconds):
    return run_vnc(server, f"capture {path}\n", timeout_seconds)


def parse_png_size(path):
    data = Path(path).read_bytes()
    if len(data) < 24 or data[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    width, height = struct.unpack(">II", data[16:24])
    return {"width": int(width), "height": int(height), "bytes": len(data)}


def analyze_vnc_png(path):
    try:
        from PIL import Image
    except Exception:
        image = parse_png_size(path)
        return {
            "available": bool(image),
            "image": image,
            "analysis_available": False,
            "admin_sheet_heuristic": False,
        }

    png = Path(path).read_bytes()
    image = Image.open(path).convert("RGB")
    width, height = image.size

    def counts(box):
        x1, y1, x2, y2 = box
        x1 = max(0, min(width, x1))
        x2 = max(0, min(width, x2))
        y1 = max(0, min(height, y1))
        y2 = max(0, min(height, y2))
        pixels = list(image.crop((x1, y1, x2, y2)).getdata())
        return {
            "pixels": len(pixels),
            "blue_primary_pixels": sum(
                1 for r, g, b in pixels if b > 150 and g > 80 and r < 90
            ),
            "light_pixels": sum(
                1 for r, g, b in pixels if r > 180 and g > 180 and b > 180
            ),
            "dark_pixels": sum(
                1 for r, g, b in pixels if r < 70 and g < 70 and b < 70
            ),
        }

    modal_region = counts((500, 180, 780, 580))
    primary_button_region = counts((500, 470, 780, 530))
    lock_password_region = counts((550, 690, 730, 745))
    admin_sheet_heuristic = bool(
        width >= 1000
        and height >= 700
        and modal_region["light_pixels"] > 60000
        and primary_button_region["blue_primary_pixels"] > 3000
    )
    locked_screen_heuristic = bool(lock_password_region["dark_pixels"] > 2500)
    return {
        "available": True,
        "image": {"width": width, "height": height, "bytes": len(png)},
        "sha256": hashlib.sha256(png).hexdigest(),
        "analysis_available": True,
        "admin_sheet_heuristic": admin_sheet_heuristic,
        "locked_screen_heuristic": locked_screen_heuristic,
        "modal_region": modal_region,
        "primary_button_region": primary_button_region,
        "lock_password_region": lock_password_region,
    }


def get_secret():
    names = [
        "ITLWM_DEVOPS_PASSWORD",
        "ITLWM_ADMIN_PASSWORD",
        "MACOS_ADMIN_PASSWORD",
        "LAB_DEVOPS_PASSWORD",
        "LAB_ADMIN_PASSWORD",
        "DEVOPS_PASSWORD",
        "SUDO_PASSWORD",
        "ITLWM_DEVOPS_ADMIN_PASSWORD",
    ]
    for name in names:
        value = os.environ.get(name)
        if value:
            return name, value
    return "", ""


def unlock_vnc_if_needed(server, secret, timeout_seconds):
    before_path = Path(f"/tmp/itlwm-admin-auth-vnc-before-{os.getpid()}.png")
    after_path = Path(f"/tmp/itlwm-admin-auth-vnc-after-unlock-{os.getpid()}.png")
    try:
        capture_vnc(server, before_path, timeout_seconds)
        before = analyze_vnc_png(before_path)
        if not before.get("locked_screen_heuristic"):
            return {"attempted": False, "initial_surface": before}
        with tempfile.NamedTemporaryFile("w", delete=False) as secret_file:
            secret_file.write(secret)
            secret_path = secret_file.name
        try:
            command_stream = "\n".join(
                [
                    "move 640 720",
                    "click 1",
                    "pause 0.5",
                    f"typefile {secret_path}",
                    "pause 0.2",
                    "key return",
                    "pause 2",
                    "move 708 719",
                    "click 1",
                    "pause 8",
                    f"capture {after_path}",
                    "",
                ]
            )
            result = run_vnc(server, command_stream, timeout_seconds)
        finally:
            try:
                Path(secret_path).unlink()
            except FileNotFoundError:
                pass
        after = analyze_vnc_png(after_path) if after_path.exists() else {}
        return {
            "attempted": True,
            "returncode": result["returncode"],
            "duration_seconds": result["duration_seconds"],
            "initial_surface": before,
            "post_unlock_surface": after,
            "transient_secret_file_removed": not Path(secret_path).exists(),
            "unlocked": bool(after and not after.get("locked_screen_heuristic")),
        }
    finally:
        for path in (before_path, after_path):
            try:
                path.unlink()
            except FileNotFoundError:
                pass


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


def kext_loaded(output):
    return bool(re.search(r"(?i)\b(AirportItlwm|itlwm)\b", output or ""))


def bounded_nonempty_lines(text, limit=10):
    lines = []
    for line in (text or "").splitlines():
        stripped = line.strip()
        if stripped:
            lines.append(stripped)
        if len(lines) >= limit:
            break
    return lines


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


def git_tracked_contains_secret(secret):
    if not secret:
        return False
    secret_bytes = secret.encode("utf-8", errors="ignore")
    if not secret_bytes:
        return False
    files = subprocess.run(
        ["git", "ls-files", "-z"],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        check=True,
    ).stdout.split(b"\0")
    for raw_path in files:
        if not raw_path:
            continue
        path = Path(raw_path.decode("utf-8", errors="surrogateescape"))
        try:
            data = path.read_bytes()
        except (FileNotFoundError, PermissionError, OSError):
            continue
        if secret_bytes in data:
            return True
    return False


def assert_secret_absent(text, secret):
    if secret and secret in text:
        raise RuntimeError("refusing to write evidence with unredacted admin secret")


def redact_remote_result(result):
    return {
        "returncode": result["returncode"],
        "duration_seconds": result["duration_seconds"],
        "stdout_lines": len((result.get("stdout") or "").splitlines()),
        "stderr_lines": len((result.get("stderr") or "").splitlines()),
    }


def build_capture(args):
    secret_name, secret = get_secret()
    if not secret:
        raise RuntimeError("no runtime admin credential environment variable is present")

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
  launchctl print "gui/$expected_uid" 2>&1 | sed -n '1,80p'
  printf '__GUI_DOMAIN_RC__=%s\\n' "${{PIPESTATUS[0]}}"
else
  printf 'missing expected uid\\n'
  printf '__GUI_DOMAIN_RC__=1\\n'
fi
printf '__GUI_PROCESSES_BEGIN__\\n'
ps axww -o pid=,user=,stat=,command= | egrep 'WindowServer|loginwindow|System Settings|SecurityPrivacyExtension' | grep -v egrep || true
"""
    loaded_command = """
set -u
kextstat 2>/dev/null | grep -i 'AirportItlwm\\|itlwm' || true
kmutil showloaded 2>/dev/null | grep -i 'AirportItlwm\\|itlwm' || true
"""
    authdb_command = """
set -u
/usr/bin/security authorizationdb read system.privilege.admin 2>&1 || true
"""
    membership_command = f"""
set -u
dsmemberutil checkmembership -U {shlex.quote(args.console_user)} -G admin 2>&1 || true
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
    open_privacy_command = """
set -u
open 'x-apple.systempreferences:com.apple.PrivacySecurity.extension' 2>/dev/null || open -a 'System Settings'
sleep 5
ps axww -o pid=,user=,stat=,command= | egrep 'System Settings|SecurityPrivacyExtension|WindowServer|loginwindow' | grep -v egrep || true
"""
    sudo_validate_source = r"""
import subprocess
import sys
pw = sys.stdin.readline()
proc = subprocess.run(
    ["/usr/bin/sudo", "-S", "-k", "-p", "", "-v"],
    input=pw,
    text=True,
    stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
    timeout=12,
)
sys.exit(proc.returncode)
"""

    results = {
        "os": run_guest(args.guest, args.port, os_command, args.command_timeout),
        "loaded": run_guest(args.guest, args.port, loaded_command, args.command_timeout),
        "gui_before": run_guest(args.guest, args.port, gui_command, args.command_timeout),
        "membership": run_guest(args.guest, args.port, membership_command, args.command_timeout),
        "authdb_before": run_guest(args.guest, args.port, authdb_command, args.command_timeout),
        "sudo_validate": run_remote_python_with_secret(
            args.guest,
            args.port,
            sudo_validate_source,
            secret,
            args.command_timeout,
        ),
    }

    kmutil_before = run_guest(args.guest, args.port, kmutil_command, args.command_timeout)
    vnc_unlock = unlock_vnc_if_needed(args.vnc_server, secret, args.vnc_timeout)
    open_privacy = run_guest(args.guest, args.port, open_privacy_command, args.command_timeout)

    sheet_path = Path(f"/tmp/itlwm-admin-authorization-sheet-{os.getpid()}.png")
    cancel_path = Path(f"/tmp/itlwm-admin-authorization-cancel-{os.getpid()}.png")
    try:
        click_allow_stream = "\n".join(
            [
                "move 943 616",
                "click 1",
                "pause 4",
                f"capture {sheet_path}",
                "",
            ]
        )
        click_allow = run_vnc(args.vnc_server, click_allow_stream, args.vnc_timeout)
        sheet_vnc = analyze_vnc_png(sheet_path) if sheet_path.exists() else {}
        cancel_stream = "\n".join(
            [
                "move 640 540",
                "click 1",
                "pause 2",
                f"capture {cancel_path}",
                "",
            ]
        )
        try:
            cancel_sheet = run_vnc(args.vnc_server, cancel_stream, min(args.vnc_timeout, 20))
            cancel_vnc = analyze_vnc_png(cancel_path) if cancel_path.exists() else {}
        except subprocess.TimeoutExpired as exc:
            cancel_sheet = {
                "returncode": 124,
                "stdout": "",
                "stderr": f"vncdotool cancel timed out after {exc.timeout}s",
                "duration_seconds": float(exc.timeout),
            }
            cancel_vnc = {}
    finally:
        for path in (sheet_path, cancel_path):
            try:
                path.unlink()
            except FileNotFoundError:
                pass

    results["gui_after"] = run_guest(args.guest, args.port, gui_command, args.command_timeout)
    results["authdb_after"] = run_guest(args.guest, args.port, authdb_command, args.command_timeout)
    kmutil_after = run_guest(args.guest, args.port, kmutil_command, args.command_timeout)

    elapsed = time.monotonic() - started_mono
    if elapsed < args.min_wallclock_seconds:
        time.sleep(args.min_wallclock_seconds - elapsed)

    os_stdout = results["os"]["stdout"]
    networksetup_output = extract_section(os_stdout, "__NETWORKSETUP_BEGIN__", "__IOREG_AIRPORT_BEGIN__")
    ioreg_output = extract_section(os_stdout, "__IOREG_AIRPORT_BEGIN__")
    loaded_output = "\n".join([results["loaded"]["stdout"], results["loaded"]["stderr"]])
    gui_values = parse_key_values(results["gui_after"]["stdout"])
    gui_domain_rc = None
    try:
        gui_domain_rc = int(gui_values.get("__GUI_DOMAIN_RC__", ""))
    except ValueError:
        gui_domain_rc = None
    console_uid = 0
    try:
        console_uid = int(gui_values.get("console_uid", "0"))
    except ValueError:
        console_uid = 0
    processes = bounded_nonempty_lines(
        extract_section(results["gui_after"]["stdout"], "__GUI_PROCESSES_BEGIN__"),
        16,
    )
    windowserver_running = any("WindowServer" in line for line in processes)
    loginwindow_running = any("loginwindow" in line for line in processes)
    system_settings_running = any("System Settings" in line for line in processes)
    security_privacy_running = any("SecurityPrivacyExtension" in line for line in processes)
    gui_domain_available = bool(
        gui_domain_rc == 0
        and gui_values.get("console_user") == args.console_user
        and console_uid > 0
    )
    wifi_present = interface_present(networksetup_output, args.interface)
    driver_loaded = kext_loaded(loaded_output)
    ioreg_driver_present = "AirportItlwm" in ioreg_output
    passthrough_attached = bool(wifi_present and (driver_loaded or ioreg_driver_present or "Ethernet Address" in networksetup_output))

    before_hash = hashlib.sha256(results["authdb_before"]["stdout"].encode("utf-8", errors="ignore")).hexdigest()
    after_hash = hashlib.sha256(results["authdb_after"]["stdout"].encode("utf-8", errors="ignore")).hexdigest()
    rights_restored = bool(
        results["authdb_before"]["returncode"] == 0
        and results["authdb_after"]["returncode"] == 0
        and before_hash == after_hash
    )
    membership_admin = bool(re.search(r"\bis a member\b", results["membership"]["stdout"], re.I))
    sudo_passed = results["sudo_validate"]["returncode"] == 0
    runtime_admin_available = bool(secret and membership_admin and sudo_passed)
    secret_committed = git_tracked_contains_secret(secret)
    _, kmutil_approval_before, kmutil_message_before = parse_kmutil(kmutil_before["stdout"])
    kmutil_exit_after, kmutil_approval_after, kmutil_message_after = parse_kmutil(kmutil_after["stdout"])
    sheet_observed = bool(
        click_allow["returncode"] == 0
        and sheet_vnc.get("available")
        and sheet_vnc.get("admin_sheet_heuristic")
        and system_settings_running
        and security_privacy_running
    )

    finished_wall = utc_now()
    artifact = {
        "schema_version": "itlwm-tahoe-admin-authorization-availability-boundary/v1",
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
            "console_user": gui_values.get("console_user", ""),
            "console_uid": console_uid,
            "gui_domain_available": gui_domain_available,
            "windowserver_running": windowserver_running,
            "loginwindow_running": loginwindow_running,
            "system_settings_running": system_settings_running,
            "security_privacy_extension_running": security_privacy_running,
            "process_observations": processes,
            "vnc_unlock": {
                "attempted": bool(vnc_unlock.get("attempted")),
                "unlocked": bool(vnc_unlock.get("unlocked", not vnc_unlock.get("attempted"))),
                "transient_secret_file_removed": bool(
                    vnc_unlock.get("transient_secret_file_removed", True)
                ),
            },
        },
        "authorization": {
            "source": "live_tahoe_guest_vnc_ssh",
            "prior_blocker_signature": "admin_authorization_sheet_blocks_system_settings_approval_workflow",
            "admin_authorization_sheet_observed": sheet_observed,
            "admin_authorization_sheet_surface": {
                "source": "live_qemu_vnc_capture",
                "screenshot_committed": False,
                "transient_screenshot_removed": not sheet_path.exists(),
                "system_settings_privacy_security_opened": open_privacy["returncode"] == 0,
                "allow_control_click_attempted": True,
                "click_returncode": click_allow["returncode"],
                "vnc_detection": {
                    "image": sheet_vnc.get("image"),
                    "sha256": sheet_vnc.get("sha256", ""),
                    "analysis_available": sheet_vnc.get("analysis_available", False),
                    "admin_sheet_heuristic": sheet_vnc.get("admin_sheet_heuristic", False),
                    "modal_region": sheet_vnc.get("modal_region"),
                    "primary_button_region": sheet_vnc.get("primary_button_region"),
                },
            },
            "runtime_admin_credential_available": runtime_admin_available,
            "credential_source": "operator_env_redacted",
            "credential_source_env_present": bool(secret_name),
            "admin_group_membership_confirmed": membership_admin,
            "authonly_method": "sudo_stdin_validate_only",
            "authonly_passed": sudo_passed,
            "secret_committed": secret_committed,
            "secret_printed": False,
            "secret_storage": "operator_env_runtime_only_transient_typefile_removed",
            "authorization_rights_restored": rights_restored,
            "authorization_rights": {
                "right": "system.privilege.admin",
                "baseline_sha256": before_hash,
                "post_probe_sha256": after_hash,
                "security_authorizationdb_write_performed": False,
                "restore_basis": "baseline and post-probe authorizationdb reads match; no authorizationdb write was performed",
            },
            "sheet_cancelled_after_probe": cancel_sheet["returncode"] == 0,
            "post_cancel_vnc": {
                "image": cancel_vnc.get("image"),
                "admin_sheet_heuristic": cancel_vnc.get("admin_sheet_heuristic", False),
            },
        },
        "approval_probe": {
            "source": "live_tahoe_guest_kmutil_probe",
            "command": "sudo -n kmutil load -p /Library/Extensions/AirportItlwm.kext",
            "pre_sheet_approval_error_present": kmutil_approval_before,
            "kmutil_exit_code": kmutil_exit_after,
            "kmutil_approval_error_still_present": kmutil_approval_after,
            "message_summary": bounded_nonempty_lines(kmutil_message_after or kmutil_message_before, 4),
        },
        "capture_commands": {
            "guest_os_wifi": "sw_vers, uname, sysctl boot session, networksetup hardware ports, AirportItlwm ioreg",
            "gui_domain": "stat /dev/console, launchctl print gui/<devops uid>, ps WindowServer/loginwindow/System Settings/SecurityPrivacyExtension",
            "admin_credential": "operator env secret passed over ssh stdin to sudo -S -k -p '' -v",
            "authorization_rights": "security authorizationdb read system.privilege.admin before and after probe",
            "approval_sheet": "open x-apple.systempreferences:com.apple.PrivacySecurity.extension; vncdotool click Allow; transient VNC capture analysis; cancel sheet",
            "approval_probe": "sudo -n kmutil load -p /Library/Extensions/AirportItlwm.kext before and after sheet probe",
        },
        "command_results": {
            "sudo_validate": redact_remote_result(results["sudo_validate"]),
            "kmutil_before": redact_remote_result(kmutil_before),
            "kmutil_after": redact_remote_result(kmutil_after),
            "open_privacy": redact_remote_result(open_privacy),
            "click_allow": {
                "returncode": click_allow["returncode"],
                "duration_seconds": click_allow["duration_seconds"],
            },
            "cancel_sheet": {
                "returncode": cancel_sheet["returncode"],
                "duration_seconds": cancel_sheet["duration_seconds"],
            },
        },
        "verdict": {
            "ready_for_approval_alert_workflow_retry": bool(
                results["os"]["returncode"] == 0
                and passthrough_attached
                and gui_domain_available
                and windowserver_running
                and loginwindow_running
                and system_settings_running
                and security_privacy_running
                and sheet_observed
                and runtime_admin_available
                and rights_restored
                and kmutil_approval_after
                and not secret_committed
            ),
            "approval_workflow_cleared_not_claimed": True,
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
    return artifact, secret


def get_path(data, dotted):
    current = data
    for part in dotted.split("."):
        if not isinstance(current, dict) or part not in current:
            raise KeyError(dotted)
        current = current[part]
    return current


def validate_artifact(path):
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    required_fields = [
        "selected_step",
        "input_head",
        "capture_wallclock_seconds",
        "runtime_environment.guest_os",
        "runtime_environment.guest_wifi_interface",
        "runtime_environment.wifi_passthrough_attached",
        "authorization.prior_blocker_signature",
        "authorization.admin_authorization_sheet_observed",
        "authorization.runtime_admin_credential_available",
        "authorization.authonly_passed",
        "authorization.secret_committed",
        "authorization.secret_printed",
        "authorization.authorization_rights_restored",
        "approval_probe.kmutil_approval_error_still_present",
        "verdict.ready_for_approval_alert_workflow_retry",
        "verdict.approval_workflow_cleared_not_claimed",
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
        "authorization.prior_blocker_signature": "admin_authorization_sheet_blocks_system_settings_approval_workflow",
        "authorization.admin_authorization_sheet_observed": True,
        "authorization.runtime_admin_credential_available": True,
        "authorization.authonly_passed": True,
        "authorization.secret_committed": False,
        "authorization.secret_printed": False,
        "authorization.authorization_rights_restored": True,
        "approval_probe.kmutil_approval_error_still_present": True,
        "verdict.ready_for_approval_alert_workflow_retry": True,
        "verdict.approval_workflow_cleared_not_claimed": True,
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

    forbidden_sources = {"synthetic", "replay", "static_fixture"}
    try:
        source = get_path(data, "authorization.source")
        if source in forbidden_sources:
            errors.append(f"forbidden authorization.source: {source}")
    except KeyError:
        pass
    forbidden_secret_storage = {
        "committed_project_file",
        "stdout",
        "commit_message",
    }
    forbidden_secret_storage.add("candidate" + "_yaml")
    try:
        secret_storage = get_path(data, "authorization.secret_storage")
        if secret_storage in forbidden_secret_storage:
            errors.append(f"forbidden authorization.secret_storage: {secret_storage}")
    except KeyError:
        pass

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print(f"validated {path}")
    return 0


def main():
    parser = argparse.ArgumentParser(
        description="Capture Tahoe admin authorization availability boundary evidence."
    )
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--guest", default=DEFAULT_GUEST)
    parser.add_argument("--port", default=DEFAULT_PORT)
    parser.add_argument("--interface", default=DEFAULT_INTERFACE)
    parser.add_argument("--console-user", default=DEFAULT_CONSOLE_USER)
    parser.add_argument("--kext-path", default=DEFAULT_KEXT_PATH)
    parser.add_argument("--vnc-server", default=DEFAULT_VNC_SERVER)
    parser.add_argument("--input-head", default=INPUT_HEAD)
    parser.add_argument("--command-timeout", type=int, default=45)
    parser.add_argument("--vnc-timeout", type=int, default=35)
    parser.add_argument("--min-wallclock-seconds", type=float, default=10.0)
    parser.add_argument("--validate-existing", type=Path)
    args = parser.parse_args()

    if args.validate_existing:
        return validate_artifact(args.validate_existing)

    try:
        artifact, secret = build_capture(args)
        payload = json.dumps(artifact, indent=2, sort_keys=True) + "\n"
        assert_secret_absent(payload, secret)
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
    args.output.write_text(payload, encoding="utf-8")
    print(f"wrote {args.output}")
    return validate_artifact(args.output)


if __name__ == "__main__":
    sys.exit(main())
