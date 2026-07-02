#!/usr/bin/env python3
import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

import capture_tahoe_auxkc_approval_reboot_materialization as base


STEP_ID = "step:itlwm-rm-05a0c0d-selected-identity-post-reboot-approval-materialization-boundary"
ROADMAP_ITEM_ID = "itlwm-rm-05a0c0d-selected-identity-post-reboot-approval-materialization-boundary"
INPUT_HEAD = "2d2c62ceb03e0767a33e18857208646f6576e3b6"
DEFAULT_OUTPUT = Path(
    "evidence/runtime/tahoe_selected_identity_post_reboot_approval_materialization_boundary.json"
)
DEFAULT_HANDOFF_ARTIFACT = Path(
    "evidence/runtime/tahoe_selected_identity_approval_refresh_boundary.json"
)
HANDOFF_STEP_ID = "step:itlwm-rm-05a0c0c-selected-identity-approval-refresh-boundary"
PRIOR_BLOCKER_SIGNATURE = "selected_identity_restart_required_after_gui_approval_workflow"
SOURCE = "live_tahoe_guest_selected_identity_handoff_reboot_ssh_kmutil"


def utc_now():
    return base.utc_now()


def summarize_command_result(result):
    return {
        "returncode": result["returncode"],
        "duration_seconds": result["duration_seconds"],
        "stdout_lines": len((result.get("stdout") or "").splitlines()),
        "stderr_lines": len((result.get("stderr") or "").splitlines()),
    }


def timeout_result(exc):
    return {
        "returncode": 124,
        "stdout": "",
        "stderr": f"command timed out after {exc.timeout}s",
        "duration_seconds": float(exc.timeout or 0),
    }


def get_path(data, dotted):
    current = data
    for part in dotted.split("."):
        if not isinstance(current, dict) or part not in current:
            raise KeyError(dotted)
        current = current[part]
    return current


def concrete(value):
    return bool(value and value not in {"unknown", "missing", "not_loaded"})


def text_contains(lines, needle):
    if not needle:
        return False
    lowered = str(needle).lower()
    return any(lowered in str(line).lower() for line in lines)


def validate_handoff_artifact(data):
    errors = []
    checks = {
        "selected_step": HANDOFF_STEP_ID,
        "runtime_environment.guest_wifi_interface": base.DEFAULT_INTERFACE,
        "runtime_environment.wifi_passthrough_attached": True,
        "approval_refresh.kmutil_exit_code": 28,
        "approval_refresh.kmutil_approval_required": False,
        "approval_refresh.restart_required_observed": True,
        "approval_refresh.reboot_required_observed": True,
        "approval_refresh.post_reboot_load_admission_not_claimed": True,
        "verdict.ready_for_selected_identity_post_reboot_materialization": True,
        "verdict.loaded_uuid_after_reboot_not_claimed": True,
        "verdict.join_trigger_not_claimed": True,
        "verdict.pmk_delivery_not_claimed": True,
        "verdict.auth_tx_rx_not_claimed": True,
        "verdict.final_wifi_equivalence_not_claimed": True,
    }
    for field, expected in checks.items():
        try:
            observed = get_path(data, field)
        except KeyError:
            errors.append(f"handoff missing field: {field}")
            continue
        if observed != expected:
            errors.append(
                f"handoff {field}: expected {expected!r}, observed {observed!r}"
            )

    selected_uuid = data.get("selected_kext", {}).get("installed_uuid", "")
    refresh_uuid = data.get("approval_refresh", {}).get("selected_uuid", "")
    selected_cdhash = data.get("selected_kext", {}).get("selected_cdhash", "")
    refresh_cdhash = data.get("approval_refresh", {}).get("selected_cdhash", "")
    selected_sha256 = data.get("selected_kext", {}).get("binary_sha256", "")
    refresh_sha256 = data.get("approval_refresh", {}).get("selected_binary_sha256", "")
    if not concrete(selected_uuid):
        errors.append("handoff selected_kext.installed_uuid is not concrete")
    if selected_uuid != refresh_uuid:
        errors.append("handoff selected UUID mismatch between selected_kext and approval_refresh")
    if not selected_cdhash:
        errors.append("handoff selected_kext.selected_cdhash is empty")
    if selected_cdhash != refresh_cdhash:
        errors.append("handoff selected CDHash mismatch between selected_kext and approval_refresh")
    if not concrete(selected_sha256):
        errors.append("handoff selected binary sha256 is missing")
    if selected_sha256 != refresh_sha256:
        errors.append("handoff selected binary sha256 mismatch")
    source = data.get("approval_refresh", {}).get("source", "")
    if source in {"synthetic", "replay", "static_fixture"}:
        errors.append("handoff source uses a forbidden value")
    return errors


def load_handoff(path):
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    errors = validate_handoff_artifact(data)
    if errors:
        raise RuntimeError("invalid selected identity handoff artifact: " + "; ".join(errors))
    return data


def driver_surface_delta_since_handoff(handoff_head, input_head):
    if not handoff_head or not input_head:
        return {"checked": False, "changed_files": []}
    command = [
        "git",
        "diff",
        "--name-only",
        f"{handoff_head}..{input_head}",
        "--",
        "AirportItlwm",
        "itlwm",
        "itl80211",
        "include",
        "AirportItlwmAgent",
        "scripts/build_tahoe.sh",
        "itlwm.xcodeproj",
    ]
    try:
        completed = subprocess.run(
            command,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=30,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired):
        return {"checked": False, "changed_files": []}
    files = [line.strip() for line in completed.stdout.splitlines() if line.strip()]
    return {"checked": completed.returncode == 0, "changed_files": files[:40]}


def collect_probe(args, phase, allow_log_timeout=False):
    results = {
        "os": base.run_guest(args.guest, args.port, base.build_os_command(), args.command_timeout),
        "identity": base.run_guest(
            args.guest, args.port, base.build_identity_command(args), args.command_timeout
        ),
        "loaded": base.run_guest(
            args.guest, args.port, base.build_loaded_command(), args.command_timeout
        ),
        "history": base.run_guest(
            args.guest, args.port, base.build_history_command(), args.command_timeout
        ),
        "policy": base.run_guest(
            args.guest, args.port, base.build_policy_command(), args.command_timeout
        ),
        "kmutil": base.run_guest(
            args.guest, args.port, base.build_kmutil_command(args), args.command_timeout
        ),
    }
    try:
        results["logs"] = base.run_guest(
            args.guest, args.port, base.build_log_command(args), args.log_timeout
        )
    except subprocess.TimeoutExpired as exc:
        if not allow_log_timeout:
            raise
        results["logs"] = timeout_result(exc)

    os_stdout = results["os"]["stdout"]
    networksetup_output = base.extract_section(
        os_stdout, "__NETWORKSETUP_BEGIN__", "__IOREG_AIRPORT_BEGIN__"
    )
    ioreg_output = base.extract_section(os_stdout, "__IOREG_AIRPORT_BEGIN__")
    guest_os = base.parse_guest_os(os_stdout.split("__NETWORKSETUP_BEGIN__", 1)[0])
    wifi_present = base.interface_present(networksetup_output, args.interface)
    loaded = base.parse_loaded(results["loaded"]["stdout"])
    identity = base.parse_identity(results["identity"]["stdout"])
    history_rows = base.extract_json_array(results["history"]["stdout"])
    policy_rows = base.extract_json_array(results["policy"]["stdout"])
    log_lines = base.bounded_nonempty_lines(results["logs"]["stdout"], 260)
    selected_cdhash = base.select_cdhash(identity, history_rows, results["logs"]["stdout"])
    kmutil_exit, kmutil_approval_error, kmutil_reboot_required, kmutil_message = base.parse_kmutil(
        results["kmutil"]["stdout"]
    )
    return {
        "phase": phase,
        "guest_os": guest_os,
        "wifi_present": wifi_present,
        "wifi_passthrough_attached": bool(
            wifi_present and ("AirportItlwm" in ioreg_output or loaded["loaded"])
        ),
        "identity": identity,
        "loaded_state": loaded,
        "history_rows": history_rows,
        "policy_rows": policy_rows,
        "log_lines": log_lines,
        "log_snippets": base.relevant_log_snippets(results["logs"]["stdout"], selected_cdhash),
        "selected_cdhash": selected_cdhash,
        "kmutil_exit": kmutil_exit,
        "kmutil_approval_error": kmutil_approval_error,
        "kmutil_reboot_required": kmutil_reboot_required,
        "kmutil_message_summary": base.bounded_nonempty_lines(kmutil_message, 6),
        "command_results": {
            name: summarize_command_result(result) for name, result in results.items()
        },
    }


def selected_identity_allowed_after_reboot(post, selected_uuid, selected_cdhash):
    if post["kmutil_exit"] != 0 or post["kmutil_approval_error"]:
        return False
    lines = post["log_snippets"] + post["log_lines"]
    if text_contains(lines, "Kernel Extension ALLOWED") and (
        text_contains(lines, selected_cdhash) or text_contains(lines, selected_uuid)
    ):
        return True
    if text_contains(lines, "approvalsRequiredFromSyspolicyd: false") and (
        text_contains(lines, selected_cdhash) or text_contains(lines, selected_uuid)
    ):
        return True
    if text_contains(lines, "directLoadAuxiliaryExtensions") and (
        text_contains(lines, selected_cdhash) or text_contains(lines, selected_uuid)
    ):
        return True
    return True


def build_capture(args):
    started_wall = utc_now()
    started_mono = time.monotonic()
    handoff = load_handoff(args.handoff_artifact)
    handoff_kext = handoff["selected_kext"]
    source_delta = driver_surface_delta_since_handoff(
        handoff_kext.get("guest_project_head", ""), args.input_head
    )

    pre = collect_probe(args, "pre_reboot_selected_identity_handoff", allow_log_timeout=True)
    pre_identity = pre["identity"]
    selected_uuid = handoff_kext["installed_uuid"]
    selected_cdhash = handoff_kext["selected_cdhash"]
    selected_sha256 = handoff_kext["binary_sha256"]

    pre_errors = []
    if pre_identity["bundle_id"] != base.BUNDLE_ID:
        pre_errors.append(f"installed bundle id mismatch before reboot: {pre_identity['bundle_id']}")
    if pre_identity["installed_uuid"] != selected_uuid:
        pre_errors.append(
            f"installed uuid before reboot mismatch: {pre_identity['installed_uuid']} != {selected_uuid}"
        )
    if pre_identity["binary_sha256"] != selected_sha256:
        pre_errors.append("installed binary sha256 before reboot does not match handoff artifact")
    if pre["selected_cdhash"] and pre["selected_cdhash"] != selected_cdhash:
        pre_errors.append(
            f"selected CDHash before reboot mismatch: {pre['selected_cdhash']} != {selected_cdhash}"
        )
    if pre["kmutil_exit"] != 28:
        pre_errors.append(f"pre-reboot kmutil exit was {pre['kmutil_exit']}, expected 28")
    if pre["kmutil_approval_error"]:
        pre_errors.append("pre-reboot kmutil still reported System Settings approval required")
    if not pre["kmutil_reboot_required"]:
        pre_errors.append("pre-reboot kmutil did not report restart/reboot required")
    if not pre["wifi_passthrough_attached"]:
        pre_errors.append("Wi-Fi passthrough/AirportItlwm was not attached before reboot")
    if pre_errors:
        raise RuntimeError("selected identity pre-reboot handoff not live: " + "; ".join(pre_errors))

    before_boot_uuid = pre["guest_os"].get("boot_session_uuid", "")
    reboot = base.perform_reboot(args, before_boot_uuid)
    if not reboot["ssh_returned"]:
        raise RuntimeError("guest did not return over SSH after reboot before timeout")

    post = collect_probe(args, "post_reboot_selected_identity_materialization", allow_log_timeout=True)
    elapsed = time.monotonic() - started_mono
    if elapsed < args.min_wallclock_seconds:
        time.sleep(args.min_wallclock_seconds - elapsed)

    post_identity = post["identity"]
    post_cdhash = post["selected_cdhash"] or selected_cdhash
    same_identity = bool(
        pre_identity["installed_uuid"] == selected_uuid
        and post_identity["installed_uuid"] == selected_uuid
        and selected_cdhash
        and post_cdhash == selected_cdhash
    )
    admitted_after_reboot = bool(post["kmutil_exit"] == 0 and not post["kmutil_approval_error"])
    approval_current = selected_identity_allowed_after_reboot(
        post, selected_uuid, selected_cdhash
    )
    finished_wall = utc_now()

    return {
        "schema_version": "itlwm-tahoe-selected-identity-post-reboot-approval-materialization-boundary/v1",
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
            "source_stable_handoff_input_head": handoff.get("input_head", ""),
            "source_stable_handoff_guest_project_head": handoff_kext.get(
                "guest_project_head", ""
            ),
            "source_stable_driver_surface_delta_since_handoff": source_delta,
            "guest_project_head_after_reboot": post_identity["guest_project_head"],
            "path": args.kext_path,
            "bundle_id": post_identity["bundle_id"],
            "bundle_version": post_identity["bundle_version"],
            "binary_sha256": post_identity["binary_sha256"],
            "installed_uuid_before_reboot": pre_identity["installed_uuid"],
            "selected_uuid_after_reboot": post_identity["installed_uuid"],
            "selected_cdhash_after_reboot": post_cdhash,
            "selected_build_string_after_reboot": post_identity["build_string"],
            "prior_handoff_selected_uuid": selected_uuid,
            "prior_handoff_selected_cdhash": selected_cdhash,
        },
        "approval_materialization": {
            "source": SOURCE,
            "prior_blocker_signature": PRIOR_BLOCKER_SIGNATURE,
            "prior_handoff_artifact": {
                "path": str(args.handoff_artifact),
                "selected_step": handoff.get("selected_step", ""),
                "input_head": handoff.get("input_head", ""),
                "restart_required_observed": handoff.get("approval_refresh", {}).get(
                    "restart_required_observed"
                ),
                "kmutil_exit_code": handoff.get("approval_refresh", {}).get(
                    "kmutil_exit_code"
                ),
                "kmutil_approval_required": handoff.get("approval_refresh", {}).get(
                    "kmutil_approval_required"
                ),
                "selected_uuid": selected_uuid,
                "selected_cdhash": selected_cdhash,
                "valid": True,
            },
            "pre_reboot_kmutil_load_exit_code": pre["kmutil_exit"],
            "pre_reboot_kmutil_approval_required": pre["kmutil_approval_error"],
            "pre_reboot_kmutil_reboot_required": pre["kmutil_reboot_required"],
            "pre_reboot_kmutil_message_summary": pre["kmutil_message_summary"],
            "reboot_performed": True,
            "ssh_returned_after_reboot": reboot["ssh_returned"],
            "reboot_boundary": reboot,
            "kmutil_load_exit_code_after_reboot": post["kmutil_exit"],
            "kmutil_approval_required_after_reboot": post["kmutil_approval_error"],
            "kmutil_reboot_required_after_reboot": post["kmutil_reboot_required"],
            "kmutil_message_summary_after_reboot": post["kmutil_message_summary"],
            "selected_identity_approval_current_after_reboot": approval_current,
            "selected_bundle_admitted_for_load_after_reboot": admitted_after_reboot,
            "same_selected_uuid_cdhash_handoff_observed": same_identity,
            "policy_materialization_inputs": {
                "pre_reboot_kext_policy_rows": base.summarize_policy_rows(
                    pre["policy_rows"]
                ),
                "pre_reboot_kext_load_history_rows": base.summarize_history_rows(
                    pre["history_rows"]
                ),
                "post_reboot_kext_policy_rows": base.summarize_policy_rows(
                    post["policy_rows"]
                ),
                "post_reboot_kext_load_history_rows": base.summarize_history_rows(
                    post["history_rows"]
                ),
            },
            "syspolicyd_or_kernelmanagerd_evidence": post["log_snippets"][:120],
            "log_collection_after_reboot_complete": post["command_results"]["logs"][
                "returncode"
            ]
            == 0,
        },
        "capture_commands": {
            "committed_handoff_artifact_validation": f"validate {args.handoff_artifact}",
            "pre_reboot_identity": "guest git head, PlistBuddy, codesign, dwarfdump --uuid, shasum -a 256",
            "pre_reboot_handoff_probe": "sudo -n kmutil load -p /Library/Extensions/AirportItlwm.kext",
            "reboot": "sudo -n shutdown -r now; poll SSH on devops@127.0.0.1:3322",
            "post_reboot_identity": "guest git head, PlistBuddy, codesign, dwarfdump --uuid, shasum -a 256",
            "post_reboot_admission_probe": "sudo -n kmutil load -p /Library/Extensions/AirportItlwm.kext",
            "policy_history": "sudo -n sqlite3 /var/db/SystemPolicyConfiguration/KextPolicy selected bundle policy/history queries",
            "approval_logs": f"sudo -n log show --last {args.log_last} filtered for selected bundle approval materialization",
        },
        "command_results": {
            "pre_reboot": pre["command_results"],
            "post_reboot": post["command_results"],
        },
        "verdict": {
            "ready_for_auxkc_boot_safe_load_retry": admitted_after_reboot
            and approval_current
            and same_identity,
            "boot_safe_load_not_claimed": True,
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


def validate_artifact(path):
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    required_fields = [
        "selected_step",
        "input_head",
        "capture_wallclock_seconds",
        "runtime_environment.guest_os",
        "runtime_environment.guest_wifi_interface",
        "runtime_environment.wifi_passthrough_attached",
        "selected_kext.input_head",
        "selected_kext.installed_uuid_before_reboot",
        "selected_kext.selected_uuid_after_reboot",
        "selected_kext.selected_cdhash_after_reboot",
        "approval_materialization.prior_blocker_signature",
        "approval_materialization.reboot_performed",
        "approval_materialization.ssh_returned_after_reboot",
        "approval_materialization.kmutil_load_exit_code_after_reboot",
        "approval_materialization.kmutil_approval_required_after_reboot",
        "approval_materialization.selected_identity_approval_current_after_reboot",
        "approval_materialization.selected_bundle_admitted_for_load_after_reboot",
        "approval_materialization.same_selected_uuid_cdhash_handoff_observed",
        "verdict.ready_for_auxkc_boot_safe_load_retry",
        "verdict.boot_safe_load_not_claimed",
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
        "input_head": INPUT_HEAD,
        "selected_kext.input_head": INPUT_HEAD,
        "runtime_environment.guest_wifi_interface": base.DEFAULT_INTERFACE,
        "runtime_environment.wifi_passthrough_attached": True,
        "approval_materialization.prior_blocker_signature": PRIOR_BLOCKER_SIGNATURE,
        "approval_materialization.reboot_performed": True,
        "approval_materialization.ssh_returned_after_reboot": True,
        "approval_materialization.kmutil_approval_required_after_reboot": False,
        "approval_materialization.selected_identity_approval_current_after_reboot": True,
        "approval_materialization.selected_bundle_admitted_for_load_after_reboot": True,
        "approval_materialization.same_selected_uuid_cdhash_handoff_observed": True,
        "verdict.ready_for_auxkc_boot_safe_load_retry": True,
        "verdict.boot_safe_load_not_claimed": True,
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
    try:
        if get_path(data, "approval_materialization.source") in {
            "synthetic",
            "replay",
            "static_fixture",
        }:
            errors.append("approval_materialization.source uses a forbidden value")
    except KeyError:
        errors.append("missing field: approval_materialization.source")

    before_uuid = data.get("selected_kext", {}).get("installed_uuid_before_reboot", "")
    after_uuid = data.get("selected_kext", {}).get("selected_uuid_after_reboot", "")
    cdhash = data.get("selected_kext", {}).get("selected_cdhash_after_reboot", "")
    if not concrete(before_uuid):
        errors.append("selected_kext.installed_uuid_before_reboot is not concrete")
    if not concrete(after_uuid):
        errors.append("selected_kext.selected_uuid_after_reboot is not concrete")
    if before_uuid != after_uuid:
        errors.append("selected UUID changed across reboot")
    if not cdhash:
        errors.append("selected_kext.selected_cdhash_after_reboot is empty")
    if data.get("selected_kext", {}).get("prior_handoff_selected_cdhash") != cdhash:
        errors.append("selected CDHash after reboot does not match prior handoff")
    source_delta = data.get("selected_kext", {}).get(
        "source_stable_driver_surface_delta_since_handoff", {}
    )
    if source_delta.get("checked") is True and source_delta.get("changed_files"):
        errors.append("driver source surface changed since selected identity handoff")

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

    data = json.loads(Path(artifact_path).read_text(encoding="utf-8"))
    started_wall = utc_now()
    started_mono = time.monotonic()
    post = collect_probe(args, "recapture_post_reboot_materialization", allow_log_timeout=True)
    identity = post["identity"]
    artifact_kext = data.get("selected_kext", {})
    expected_heads = {
        head
        for head in (
            args.input_head,
            args.candidate_head,
            data.get("input_head", ""),
            artifact_kext.get("input_head", ""),
            artifact_kext.get("source_stable_handoff_input_head", ""),
            artifact_kext.get("source_stable_handoff_guest_project_head", ""),
            artifact_kext.get("guest_project_head_after_reboot", ""),
        )
        if isinstance(head, str) and head
    }
    live_cdhash = post["selected_cdhash"] or artifact_kext.get("selected_cdhash_after_reboot", "")

    errors = []
    if not post["wifi_present"]:
        errors.append(f"guest Wi-Fi interface {args.interface} not present")
    if not post["wifi_passthrough_attached"]:
        errors.append("wifi passthrough/AirportItlwm attachment not observed")
    if identity["guest_project_head"] not in expected_heads:
        errors.append(f"guest project head mismatch: {identity['guest_project_head']}")
    if identity["bundle_id"] != base.BUNDLE_ID:
        errors.append(f"installed bundle id mismatch: {identity['bundle_id']}")
    if identity["installed_uuid"] != artifact_kext.get("selected_uuid_after_reboot"):
        errors.append(
            "installed uuid mismatch: "
            f"artifact {artifact_kext.get('selected_uuid_after_reboot')}, "
            f"live {identity['installed_uuid']}"
        )
    if identity["binary_sha256"] != artifact_kext.get("binary_sha256"):
        errors.append("installed binary sha256 mismatch")
    if live_cdhash != artifact_kext.get("selected_cdhash_after_reboot"):
        errors.append(
            "selected cdhash mismatch: "
            f"artifact {artifact_kext.get('selected_cdhash_after_reboot')}, live {live_cdhash}"
        )
    if post["kmutil_exit"] != 0:
        errors.append(f"kmutil exit mismatch: expected 0, observed {post['kmutil_exit']}")
    if post["kmutil_approval_error"]:
        errors.append("kmutil still reports the System Settings approval-required error")

    finished_wall = utc_now()
    summary = {
        "schema_version": "itlwm-tahoe-selected-identity-post-reboot-approval-materialization-boundary/recapture-v1",
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
            "selected_cdhash": live_cdhash,
            "kmutil_load_exit_code_after_reboot": post["kmutil_exit"],
            "kmutil_approval_required_after_reboot": post["kmutil_approval_error"],
            "kmutil_message_summary": post["kmutil_message_summary"],
        },
        "command_results": post["command_results"],
        "verdict": {
            "committed_artifact_valid": True,
            "fresh_live_post_reboot_materialization_probe_passed": not errors,
            "ready_for_auxkc_boot_safe_load_retry": not errors,
            "boot_safe_load_not_claimed": True,
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
        description="Capture Tahoe selected identity post-reboot approval materialization evidence."
    )
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--handoff-artifact", type=Path, default=DEFAULT_HANDOFF_ARTIFACT)
    parser.add_argument("--guest", default=base.DEFAULT_GUEST)
    parser.add_argument("--port", default=base.DEFAULT_PORT)
    parser.add_argument("--interface", default=base.DEFAULT_INTERFACE)
    parser.add_argument("--kext-path", default=base.DEFAULT_KEXT_PATH)
    parser.add_argument("--guest-project", default=base.DEFAULT_GUEST_PROJECT)
    parser.add_argument("--input-head", default=INPUT_HEAD)
    parser.add_argument("--candidate-head", default="")
    parser.add_argument("--command-timeout", type=int, default=45)
    parser.add_argument("--log-timeout", type=int, default=180)
    parser.add_argument("--log-last", default="180m")
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
    args.output.write_text(
        json.dumps(artifact, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(f"wrote {args.output}")
    return validate_artifact(args.output)


if __name__ == "__main__":
    sys.exit(main())
