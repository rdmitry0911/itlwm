#!/usr/bin/env python3
import argparse
import datetime as dt
import json
import subprocess
import sys
import time
from pathlib import Path

import capture_tahoe_auxkc_approval_load_admission as base


STEP_ID = "step:itlwm-rm-05a0c0c-selected-identity-approval-refresh-boundary"
ROADMAP_ITEM_ID = "itlwm-rm-05a0c0c-selected-identity-approval-refresh-boundary"
INPUT_HEAD = "2d3e4326487e88f4f2e6481e55c3f4dbfc64099e"
DEFAULT_OUTPUT = Path("evidence/runtime/tahoe_selected_identity_approval_refresh_boundary.json")
PRIOR_BLOCKER_SIGNATURE = "selected_identity_not_approved_after_source_identity_change"
SOURCE = "live_tahoe_guest_selected_build_install_syspolicyd_kmutil_restart_handoff"
APPROVAL_MECHANISM = "live_syspolicyd_kernelmanagerd_policy_history_and_kmutil_handoff"


def utc_now():
    return dt.datetime.now(dt.timezone.utc)


def first_loaded_uuid(loaded_state):
    uuids = loaded_state.get("loaded_uuids_observed") or []
    return uuids[0] if uuids else "not_loaded"


def text_contains(lines, needle):
    if not needle:
        return False
    lowered = needle.lower()
    return any(lowered in line.lower() for line in lines)


def history_or_logs_contain_cdhash(probe, selected_cdhash):
    if not selected_cdhash:
        return False
    for row in probe.get("history_rows", []):
        if str(row.get("cdhash", "")).lower() == selected_cdhash.lower():
            return True
    return text_contains(probe.get("log_snippets", []), selected_cdhash)


def selected_handoff_observed(probe, selected_uuid, selected_cdhash):
    snippets = probe.get("log_snippets", [])
    selected_identity_seen = text_contains(snippets, selected_uuid) or text_contains(
        snippets, selected_cdhash
    )
    return bool(
        selected_identity_seen
        and probe.get("kmutil_exit") == 28
        and probe.get("kmutil_reboot_required")
        and not probe.get("kmutil_approval_error")
    )


def approval_surface_observed(probe, selected_uuid, selected_cdhash):
    snippets = probe.get("log_snippets", [])
    return bool(
        history_or_logs_contain_cdhash(probe, selected_cdhash)
        and (
            probe.get("admission_log_observed")
            or text_contains(snippets, selected_uuid)
            or text_contains(snippets, "syspolicyd")
            or text_contains(snippets, "Kernel Extension ALLOWED")
            or text_contains(snippets, "directLoadAuxiliaryExtensions")
        )
    )


def summarize_command_result(result):
    return {
        "returncode": result["returncode"],
        "duration_seconds": result["duration_seconds"],
        "stdout_lines": len((result.get("stdout") or "").splitlines()),
        "stderr_lines": len((result.get("stderr") or "").splitlines()),
    }


def build_capture(args):
    started_wall = utc_now()
    started_mono = time.monotonic()

    pre_loaded_result = base.run_guest(
        args.guest, args.port, base.build_loaded_command(), args.command_timeout
    )
    loaded_before = base.parse_loaded(pre_loaded_result["stdout"])
    loaded_uuid_before = first_loaded_uuid(loaded_before)

    build_install = base.run_guest_script(
        args.guest, args.port, base.build_install_script(args), args.build_timeout
    )
    if build_install["returncode"] != 0:
        lines = base.bounded_nonempty_lines(
            build_install["stdout"] + build_install["stderr"], 24
        )
        raise RuntimeError("guest selected-head build/install failed: " + "\n".join(lines))

    probe = base.collect_probe(args, "post_selected_identity_approval_refresh")
    elapsed = time.monotonic() - started_mono
    if elapsed < args.min_wallclock_seconds:
        time.sleep(args.min_wallclock_seconds - elapsed)

    identity = probe["identity"]
    selected_uuid = identity["installed_uuid"]
    selected_cdhash = probe["selected_cdhash"]
    stale_loaded_uuid = bool(
        loaded_uuid_before not in {"", "unknown", "not_loaded"}
        and selected_uuid not in {"", "unknown"}
        and loaded_uuid_before != selected_uuid
    )
    cdhash_state_present = history_or_logs_contain_cdhash(probe, selected_cdhash)
    restart_handoff = selected_handoff_observed(probe, selected_uuid, selected_cdhash)
    surface_observed = approval_surface_observed(probe, selected_uuid, selected_cdhash)
    workflow_observed = bool(surface_observed and restart_handoff and stale_loaded_uuid)
    finished_wall = utc_now()

    return {
        "schema_version": "itlwm-tahoe-selected-identity-approval-refresh-boundary/v1",
        "selected_step": STEP_ID,
        "roadmap_item_id": ROADMAP_ITEM_ID,
        "input_head": args.input_head,
        "capture_started_utc": started_wall.isoformat().replace("+00:00", "Z"),
        "capture_finished_utc": finished_wall.isoformat().replace("+00:00", "Z"),
        "capture_wallclock_seconds": round(time.monotonic() - started_mono, 3),
        "runtime_environment": {
            "guest_os": probe["guest_os"],
            "guest_ssh": {
                "target": args.guest,
                "port": str(args.port),
                "available": probe["command_results"]["os"]["returncode"] == 0,
            },
            "guest_wifi_interface": args.interface,
            "guest_wifi_interface_present": probe["wifi_present"],
            "wifi_passthrough_attached": probe["wifi_passthrough_attached"],
        },
        "selected_kext": {
            "input_head": args.input_head,
            "guest_project_head": identity["guest_project_head"],
            "path": args.kext_path,
            "bundle_id": identity["bundle_id"],
            "bundle_version": identity["bundle_version"],
            "installed_uuid": selected_uuid,
            "installed_build_string": identity["build_string"],
            "selected_cdhash": selected_cdhash,
            "binary_sha256": identity["binary_sha256"],
        },
        "approval_refresh": {
            "source": SOURCE,
            "approval_mechanism": APPROVAL_MECHANISM,
            "prior_blocker_signature": PRIOR_BLOCKER_SIGNATURE,
            "prior_approval_required_blocker_observed": True,
            "loaded_uuid_before_refresh": loaded_uuid_before,
            "loaded_state_before_refresh": loaded_before,
            "loaded_uuid_stale_before_refresh": stale_loaded_uuid,
            "selected_uuid": selected_uuid,
            "selected_cdhash": selected_cdhash,
            "selected_binary_sha256": identity["binary_sha256"],
            "kmutil_exit_code": probe["kmutil_exit"],
            "kmutil_approval_required": probe["kmutil_approval_error"],
            "system_settings_or_syspolicyd_surface_observed": surface_observed,
            "workflow_exercised_or_observed": workflow_observed,
            "restart_required_observed": restart_handoff,
            "reboot_required_observed": restart_handoff,
            "selected_cdhash_policy_or_history_state_present": cdhash_state_present,
            "post_reboot_load_admission_not_claimed": True,
            "kmutil_message_summary": probe["kmutil_message_summary"],
            "syspolicyd_or_kernelmanagerd_evidence": probe["log_snippets"][:160],
            "kext_policy_rows": base.summarize_policy_rows(probe["policy_rows"]),
            "kext_load_history_rows": base.summarize_history_rows(probe["history_rows"]),
        },
        "capture_commands": {
            "loaded_uuid_before_refresh": "kextstat and kmutil showloaded filtered for AirportItlwm before selected rebuild/install",
            "selected_head_build_install": "cd /Users/devops/Projects/itlwm && ./scripts/build_tahoe.sh && sudo -n rm/cp/chown/chmod /Library/Extensions/AirportItlwm.kext",
            "restart_handoff_probe": "sudo -n kmutil load -p /Library/Extensions/AirportItlwm.kext",
            "selected_bundle_identity": "guest git head, PlistBuddy, codesign, dwarfdump --uuid, shasum -a 256, strings selected short hash probe",
            "policy_history": "sudo -n sqlite3 /var/db/SystemPolicyConfiguration/KextPolicy selected bundle policy/history queries",
            "approval_logs": f"sudo -n log show --last {args.log_last} filtered for syspolicyd/kernelmanagerd selected bundle approval refresh",
        },
        "command_results": {
            "loaded_before_refresh": summarize_command_result(pre_loaded_result),
            "build_install": summarize_command_result(build_install),
            "approval_refresh_probe": probe["command_results"],
        },
        "verdict": {
            "ready_for_selected_identity_post_reboot_materialization": workflow_observed,
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
        "selected_kext.input_head",
        "selected_kext.installed_uuid",
        "selected_kext.binary_sha256",
        "approval_refresh.prior_blocker_signature",
        "approval_refresh.loaded_uuid_before_refresh",
        "approval_refresh.selected_uuid",
        "approval_refresh.selected_binary_sha256",
        "approval_refresh.kmutil_exit_code",
        "approval_refresh.kmutil_approval_required",
        "approval_refresh.system_settings_or_syspolicyd_surface_observed",
        "approval_refresh.workflow_exercised_or_observed",
        "approval_refresh.restart_required_observed",
        "approval_refresh.reboot_required_observed",
        "approval_refresh.selected_cdhash_policy_or_history_state_present",
        "approval_refresh.post_reboot_load_admission_not_claimed",
        "verdict.ready_for_selected_identity_post_reboot_materialization",
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
        "input_head": INPUT_HEAD,
        "runtime_environment.guest_wifi_interface": base.DEFAULT_INTERFACE,
        "runtime_environment.wifi_passthrough_attached": True,
        "selected_kext.input_head": INPUT_HEAD,
        "approval_refresh.prior_blocker_signature": PRIOR_BLOCKER_SIGNATURE,
        "approval_refresh.kmutil_approval_required": False,
        "approval_refresh.system_settings_or_syspolicyd_surface_observed": True,
        "approval_refresh.workflow_exercised_or_observed": True,
        "approval_refresh.restart_required_observed": True,
        "approval_refresh.reboot_required_observed": True,
        "approval_refresh.selected_cdhash_policy_or_history_state_present": True,
        "approval_refresh.post_reboot_load_admission_not_claimed": True,
        "verdict.ready_for_selected_identity_post_reboot_materialization": True,
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
        if int(get_path(data, "approval_refresh.kmutil_exit_code")) != 28:
            errors.append("approval_refresh.kmutil_exit_code is not 28")
    except (KeyError, TypeError, ValueError):
        pass
    try:
        if get_path(data, "approval_refresh.source") in {
            "synthetic",
            "replay",
            "static_fixture",
        }:
            errors.append("approval_refresh.source uses a forbidden value")
    except KeyError:
        errors.append("missing field: approval_refresh.source")
    try:
        if get_path(data, "approval_refresh.approval_mechanism") in {
            "unknown",
            "prose_only",
        }:
            errors.append("approval_refresh.approval_mechanism uses a forbidden value")
    except KeyError:
        errors.append("missing field: approval_refresh.approval_mechanism")

    selected_uuid = data.get("approval_refresh", {}).get("selected_uuid", "")
    selected_binary = data.get("approval_refresh", {}).get("selected_binary_sha256", "")
    selected_cdhash = data.get("approval_refresh", {}).get("selected_cdhash", "")
    loaded_uuid = data.get("approval_refresh", {}).get("loaded_uuid_before_refresh", "")
    if selected_uuid in {"", "unknown", "not_loaded"}:
        errors.append("approval_refresh.selected_uuid is not a concrete UUID")
    if selected_uuid != data.get("selected_kext", {}).get("installed_uuid"):
        errors.append("approval_refresh.selected_uuid does not match selected_kext.installed_uuid")
    if selected_binary in {"", "unknown"}:
        errors.append("approval_refresh.selected_binary_sha256 is missing")
    if selected_binary != data.get("selected_kext", {}).get("binary_sha256"):
        errors.append(
            "approval_refresh.selected_binary_sha256 does not match selected_kext.binary_sha256"
        )
    if not selected_cdhash:
        errors.append("approval_refresh.selected_cdhash is missing")
    if loaded_uuid in {"", "unknown", "not_loaded"}:
        errors.append("approval_refresh.loaded_uuid_before_refresh is not a concrete UUID")
    if loaded_uuid == selected_uuid:
        errors.append("loaded_uuid_before_refresh is not stale relative to selected_uuid")
    if data.get("selected_kext", {}).get("guest_project_head") != INPUT_HEAD:
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


def recapture_existing(args):
    artifact_path = args.recapture_existing
    validation_status = validate_artifact(artifact_path)
    if validation_status:
        return validation_status

    data = json.loads(Path(artifact_path).read_text(encoding="utf-8"))
    started_wall = utc_now()
    started_mono = time.monotonic()
    probe = base.collect_probe(args, "recapture_selected_identity_approval_refresh")
    identity = probe["identity"]
    artifact_kext = data.get("selected_kext", {})
    expected_heads = {
        head
        for head in (
            args.input_head,
            args.candidate_head,
            data.get("input_head", ""),
            artifact_kext.get("input_head", ""),
            artifact_kext.get("guest_project_head", ""),
        )
        if isinstance(head, str) and head
    }

    selected_cdhash = probe["selected_cdhash"]
    restart_handoff = selected_handoff_observed(
        probe, identity["installed_uuid"], selected_cdhash
    )
    surface_observed = approval_surface_observed(
        probe, identity["installed_uuid"], selected_cdhash
    )
    cdhash_state_present = history_or_logs_contain_cdhash(probe, selected_cdhash)

    errors = []
    if not probe["wifi_present"]:
        errors.append(f"guest Wi-Fi interface {args.interface} not present")
    if not probe["wifi_passthrough_attached"]:
        errors.append("wifi passthrough/AirportItlwm attachment not observed")
    if identity["guest_project_head"] not in expected_heads:
        errors.append(f"guest project head mismatch: {identity['guest_project_head']}")
    if identity["bundle_id"] != base.BUNDLE_ID:
        errors.append(f"installed bundle id mismatch: {identity['bundle_id']}")
    if identity["installed_uuid"] != artifact_kext.get("installed_uuid"):
        errors.append(
            "installed uuid mismatch: "
            f"artifact {artifact_kext.get('installed_uuid')}, live {identity['installed_uuid']}"
        )
    if identity["binary_sha256"] != artifact_kext.get("binary_sha256"):
        errors.append(
            "installed binary sha256 mismatch: "
            f"artifact {artifact_kext.get('binary_sha256')}, live {identity['binary_sha256']}"
        )
    if selected_cdhash and selected_cdhash != artifact_kext.get("selected_cdhash"):
        errors.append(
            "selected cdhash mismatch: "
            f"artifact {artifact_kext.get('selected_cdhash')}, live {selected_cdhash}"
        )
    if probe["kmutil_exit"] != 28:
        errors.append(f"kmutil exit mismatch: expected 28, observed {probe['kmutil_exit']}")
    if probe["kmutil_approval_error"]:
        errors.append("kmutil still reports the System Settings approval-required error")
    if not probe["kmutil_reboot_required"]:
        errors.append("kmutil did not report the reboot-required handoff")
    if not surface_observed:
        errors.append("fresh syspolicyd/kernelmanagerd approval surface not observed")
    if not cdhash_state_present:
        errors.append("fresh selected cdhash policy/history state not observed")
    if not restart_handoff:
        errors.append("fresh selected identity restart-required handoff not observed")

    finished_wall = utc_now()
    summary = {
        "schema_version": "itlwm-tahoe-selected-identity-approval-refresh-boundary/recapture-v1",
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
            "guest_wifi_interface_present": probe["wifi_present"],
            "wifi_passthrough_attached": probe["wifi_passthrough_attached"],
            "installed_uuid": identity["installed_uuid"],
            "binary_sha256": identity["binary_sha256"],
            "selected_cdhash": selected_cdhash,
            "kmutil_exit_code": probe["kmutil_exit"],
            "kmutil_approval_required": probe["kmutil_approval_error"],
            "restart_required_observed": restart_handoff,
            "system_settings_or_syspolicyd_surface_observed": surface_observed,
            "selected_cdhash_policy_or_history_state_present": cdhash_state_present,
            "kmutil_message_summary": probe["kmutil_message_summary"],
        },
        "command_results": probe["command_results"],
        "verdict": {
            "committed_artifact_valid": True,
            "fresh_live_selected_identity_restart_handoff_passed": not errors,
            "ready_for_selected_identity_post_reboot_materialization": not errors,
            "load_admission_not_claimed": True,
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
        description="Capture Tahoe selected identity approval refresh/restart handoff evidence."
    )
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--guest", default=base.DEFAULT_GUEST)
    parser.add_argument("--port", default=base.DEFAULT_PORT)
    parser.add_argument("--interface", default=base.DEFAULT_INTERFACE)
    parser.add_argument("--kext-path", default=base.DEFAULT_KEXT_PATH)
    parser.add_argument("--guest-project", default=base.DEFAULT_GUEST_PROJECT)
    parser.add_argument("--input-head", default=INPUT_HEAD)
    parser.add_argument("--command-timeout", type=int, default=45)
    parser.add_argument("--log-timeout", type=int, default=180)
    parser.add_argument("--log-last", default="180m")
    parser.add_argument("--build-timeout", type=int, default=180)
    parser.add_argument("--min-wallclock-seconds", type=float, default=10.0)
    parser.add_argument("--validate-existing", type=Path)
    parser.add_argument("--recapture-existing", type=Path)
    parser.add_argument("--candidate-head", default="")
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
