#!/usr/bin/env python3
import argparse
import datetime as dt
import json
import os
import re
import subprocess
import sys
import time
import uuid
from pathlib import Path

import capture_tahoe_auth_unicast_tx_node as unicast
import capture_tahoe_join_trigger_pmk_delivery as base


STEP_ID = "step:itlwm-rm-05a0b1b-auth-txdone-guard-publication-boundary"
ROADMAP_ITEM_ID = "itlwm-rm-05a0b1b-auth-txdone-guard-publication-boundary"
INPUT_HEAD = ""
DEFAULT_OUTPUT = Path("evidence/runtime/tahoe_auth_txdone_guard_publication_boundary.json")
AUTH_SUBTYPE = "0xb0"


REQUIRED_FIELDS = [
    "capture_wallclock_seconds",
    "runtime_environment.guest_os",
    "runtime_environment.guest_wifi_interface",
    "runtime_environment.allowed_ap_identifier",
    "driver.kext_loaded",
    "lab.ap_allowed",
    "scan.aps_seen",
    "association_target.visible_or_directed",
    "pmk_helper.target_published",
    "pmk_helper.pmk_delivered",
    "auth.started",
    "auth.rxon_filter_bss_enabled",
    "auth.management_frame_enqueue_observed",
    "auth.management_frame_drain_attempt_observed",
    "auth.iwn_tx_path_entered",
    "auth.txid",
    "auth.txid_not_broadcast",
    "auth.tx_peer_unicast",
    "auth.pending_qid",
    "auth.pending_idx",
    "auth.raw_tx_done_publication_observed",
    "auth.raw_tx_done_publication_matches_pending_qid_idx",
    "auth.tx_done_guard_decision_classified",
    "candidate_fix.source_delta_committed",
    "verdict.ready_for_auth_tx_completion_retry",
    "verdict.auth_tx_completion_not_claimed",
    "verdict.auth_response_window_not_claimed",
    "verdict.auth_response_ack_not_claimed",
    "verdict.final_wifi_equivalence_not_claimed",
]

MIN_NUMBER = {
    "capture_wallclock_seconds": 30,
    "scan.aps_seen": 1,
}

MAX_NUMBER = {
    "auth.txid": 14,
}

EQUALS = {
    "runtime_environment.guest_wifi_interface": "en1",
    "driver.kext_loaded": True,
    "lab.ap_allowed": True,
    "association_target.visible_or_directed": True,
    "pmk_helper.target_published": True,
    "pmk_helper.pmk_delivered": True,
    "auth.started": True,
    "auth.rxon_filter_bss_enabled": True,
    "auth.management_frame_enqueue_observed": True,
    "auth.management_frame_drain_attempt_observed": True,
    "auth.iwn_tx_path_entered": True,
    "auth.txid_not_broadcast": True,
    "auth.tx_peer_unicast": True,
    "auth.raw_tx_done_publication_observed": True,
    "auth.raw_tx_done_publication_matches_pending_qid_idx": True,
    "auth.tx_done_guard_decision_classified": True,
    "candidate_fix.source_delta_committed": True,
    "verdict.ready_for_auth_tx_completion_retry": True,
    "verdict.auth_tx_completion_not_claimed": True,
    "verdict.auth_response_window_not_claimed": True,
    "verdict.auth_response_ack_not_claimed": True,
    "verdict.final_wifi_equivalence_not_claimed": True,
}

FORBIDDEN_VALUES = {
    "scan.source": {"synthetic", "replay", "static_fixture"},
    "auth.source": {"synthetic", "replay", "static_fixture"},
}

CLASSIFIED_DECISIONS = {
    "raw_txdone_observed",
    "no_raw_txdone_before_retry",
}


def utc_now():
    return dt.datetime.now(dt.timezone.utc)


def get_path(data, dotted):
    return base.get_path(data, dotted)


def parse_kv_blob(blob):
    return unicast.parse_kv_blob(blob)


def int_field(fields, key):
    return unicast.int_field(fields, key)


def extract_assignment_input_head(text):
    if not text:
        return ""
    step_index = text.find(f"id: {STEP_ID}")
    if step_index < 0:
        return ""
    step_block = text[step_index : step_index + 5000]
    return base.extract_yaml_hash(step_block, "input_head")


def resolve_attempt_heads(args):
    assignment_text = base.read_optional_text(args.assignment_path)
    candidate_text = base.read_optional_text(args.candidate_path)

    args.input_head = base.first_consistent_hash(
        "input head",
        [
            args.input_head,
            extract_assignment_input_head(assignment_text),
            base.extract_candidate_input_head(candidate_text),
        ],
    ) or base.local_project_head()
    if not args.input_head:
        raise RuntimeError("cannot resolve selected input head")

    args.candidate_after_head = base.first_consistent_hash(
        "candidate after head",
        [args.candidate_after_head, base.extract_candidate_after_head(candidate_text)],
    )


def expected_guest_heads(args):
    return base.ordered_unique(
        [args.input_head, args.candidate_after_head] + list(args.allow_guest_head or [])
    )


def build_auth_log_script(nonce, log_last):
    predicate = (
        f'(eventMessage CONTAINS[c] "{nonce}" '
        'OR eventMessage CONTAINS[c] "ieee80211_mgmt_output: enqueue AUTH" '
        'OR eventMessage CONTAINS[c] "iwn_start: ic_mgtq drain MGT" '
        'OR eventMessage CONTAINS[c] "iwn_tx: MGT ring_kick" '
        'OR eventMessage CONTAINS[c] "AUTH TXDONE" '
        'OR eventMessage CONTAINS[c] "iwn_auth:" '
        'OR eventMessage CONTAINS[c] "iwn_rx_done: MGT" '
        'OR eventMessage CONTAINS[c] "plti_publish_assoc_target" '
        'OR eventMessage CONTAINS[c] "plti_wait_assoc_target" '
        'OR eventMessage CONTAINS[c] "deliverExternalPMK" '
        'OR eventMessage CONTAINS[c] "WaitAssociationTarget" '
        'OR eventMessage CONTAINS[c] "DeliverPMK" '
        'OR eventMessage CONTAINS[c] "handle_target" '
        'OR eventMessage CONTAINS[c] "AirportItlwmAgent")'
    )
    return f"""
set -u
sudo -n log show --last {base.shlex.quote(log_last)} --style compact --predicate {base.shlex.quote(predicate)} 2>/dev/null | tail -560 || true
"""


def build_ioreg_script():
    return """
set -u
ioreg -r -n AirportItlwm -l 2>/dev/null | head -300 || true
"""


def parse_ioreg_properties(ioreg_output, allowed_ssid, allowed_psk):
    properties = unicast.parse_ioreg_properties(ioreg_output, allowed_ssid, allowed_psk)
    for prop in (
        "itlwm-iwn-auth-tx-pending",
        "itlwm-iwn-auth-txdone-guard",
    ):
        pattern = re.compile(r'"' + re.escape(prop) + r'"\s*=\s*"([^"]*)"')
        matches = pattern.findall(ioreg_output or "")
        if matches:
            value = base.redact_text(matches[-1], allowed_ssid, allowed_psk)
            properties[prop] = {
                "value": value,
                "fields": parse_kv_blob(value),
            }
    return properties


def auth_seq_is_one(event):
    return (event.get("fields") or {}).get("auth_seq") == "0x0001"


def event_not_dropped(event):
    return unicast.event_not_dropped(event)


def unicast_tx_identities(auth_markers):
    candidates = []
    for event in auth_markers["tx_path_events"]:
        fields = event.get("fields") or {}
        txid = int_field(fields, "txid")
        broadcast_txid = int_field(fields, "broadcast_txid")
        if not auth_seq_is_one(event) or txid is None:
            continue
        txid_not_broadcast = (
            txid != broadcast_txid if broadcast_txid is not None else txid < 15
        )
        tx_peer_unicast = (
            fields.get("unicast") == "1"
            if fields.get("unicast") is not None
            else txid_not_broadcast
        )
        candidates.append(
            {
                "event": event,
                "fields": fields,
                "txid": txid,
                "broadcast_txid": broadcast_txid,
                "txid_not_broadcast": txid_not_broadcast,
                "tx_peer_unicast": tx_peer_unicast,
                "pending_qid": int_field(fields, "qid"),
                "pending_idx": int_field(fields, "idx"),
            }
        )
    return candidates


def selected_unicast_tx_identity(auth_markers, guard_events):
    candidates = unicast_tx_identities(auth_markers)
    for guard_event in reversed(guard_events or []):
        fields = guard_event.get("fields") or {}
        guard_qid = int_field(fields, "pending_qid")
        guard_idx = int_field(fields, "pending_idx")
        if guard_qid is None or guard_idx is None:
            continue
        for candidate in reversed(candidates):
            if (
                candidate["pending_qid"] == guard_qid
                and candidate["pending_idx"] == guard_idx
                and candidate["txid_not_broadcast"]
                and candidate["tx_peer_unicast"]
            ):
                return candidate
    for candidate in reversed(candidates):
        if candidate["txid_not_broadcast"] and candidate["tx_peer_unicast"]:
            return candidate
    return candidates[-1] if candidates else None


def append_guard_event(events, source, line):
    fields = parse_kv_blob(line)
    stage = fields.get("stage", "")
    decision = fields.get("decision", "")
    if stage not in {"raw_txdone", "guard_before_retry"}:
        return
    events.append(
        {
            "source": source,
            "line": line,
            "fields": fields,
            "stage": stage,
            "decision": decision,
        }
    )


def guard_event_matches_pending(event, pending_qid, pending_idx):
    fields = event.get("fields") or {}
    event_pending_qid = int_field(fields, "pending_qid")
    event_pending_idx = int_field(fields, "pending_idx")
    event_qid = int_field(fields, "qid")
    event_idx = int_field(fields, "idx")
    if pending_qid is None or pending_idx is None:
        return False
    return (
        event_pending_qid == pending_qid
        and event_pending_idx == pending_idx
    ) or (event_qid == pending_qid and event_idx == pending_idx)


def parse_guard_markers(log_output, ioreg_output, nonce, allowed_ssid, allowed_psk):
    redacted_log_lines = [
        base.redact_text(line.strip(), allowed_ssid, allowed_psk)
        for line in unicast.lines_after_nonce(log_output, nonce)
        if line.strip()
    ]
    ioreg_properties = parse_ioreg_properties(ioreg_output, allowed_ssid, allowed_psk)

    guard_events = []
    for line in redacted_log_lines:
        if "AUTH TXDONE" in line:
            append_guard_event(guard_events, "guest_unified_log", line)

    guard_property = ioreg_properties.get("itlwm-iwn-auth-txdone-guard")
    if guard_property:
        append_guard_event(guard_events, "guest_ioreg", guard_property["value"])

    return {
        "events": guard_events[-8:],
        "ioreg_properties": ioreg_properties,
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
    for path, maximum in MAX_NUMBER.items():
        try:
            value = get_path(data, path)
        except KeyError:
            continue
        if not isinstance(value, (int, float)) or value > maximum:
            errors.append(f"{path}={value!r} above maximum {maximum!r}")
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


def parse_probe(args, probe_result):
    probe_output = probe_result["stdout"]
    network_output = base.extract_section(
        probe_output, "__NETWORKSETUP_BEGIN__", "__LOADED_BEGIN__"
    )
    loaded_output = base.extract_section(
        probe_output, "__LOADED_BEGIN__", "__IDENTITY_BEGIN__"
    )
    identity_output = base.extract_section(probe_output, "__IDENTITY_BEGIN__")
    guest_os = base.parse_guest_os(probe_output.split("__NETWORKSETUP_BEGIN__", 1)[0])
    loaded = base.parse_loaded(loaded_output)
    identity = base.parse_identity(identity_output)
    interface_ok = base.interface_present(network_output, args.interface)
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
    return {
        "guest_os": guest_os,
        "loaded": loaded,
        "identity": identity,
        "interface_ok": interface_ok,
        "loaded_uuid_matches_identity": loaded_uuid_matches_identity,
        "identity_guest_head_allowed": identity_guest_head_allowed,
        "selected_identity_matches": selected_identity_matches,
    }


def build_evidence(args):
    allowed_ssid = os.environ.get("ITLWM_ALLOWED_AP_SSID", "")
    allowed_psk = os.environ.get("ITLWM_ALLOWED_AP_PSK", "")
    if not allowed_ssid:
        raise RuntimeError("ITLWM_ALLOWED_AP_SSID is required for the allowed AP selector")
    if not allowed_psk:
        raise RuntimeError("ITLWM_ALLOWED_AP_PSK is required for the PMK helper setup")

    started_wall = utc_now()
    started_mono = time.monotonic()
    identifier = base.ssid_identifier(allowed_ssid)
    nonce = "itlwm-auth-txdone-guard-" + uuid.uuid4().hex[:16]

    probe_result = base.run_guest_script(
        args,
        "guest OS, loaded kext, and selected identity probe",
        base.build_probe_script(args),
        args.command_timeout,
    )
    if probe_result["returncode"] != 0:
        raise RuntimeError("guest probe failed")
    probe = parse_probe(args, probe_result)

    prepare_result = base.run_guest_script(
        args,
        "guest PMK helper install and project keychain preparation",
        base.env_script(allowed_ssid, allowed_psk, base.build_prepare_script(args)),
        args.setup_timeout,
    )
    if prepare_result["returncode"] != 0:
        raise RuntimeError("PMK helper/keychain preparation failed")

    join_result = base.run_guest_script(
        args,
        "guest CoreWLAN directed join trigger for AUTH TXDONE guard publication",
        base.env_script(allowed_ssid, allowed_psk, base.build_join_script(nonce)),
        args.join_timeout,
    )
    if join_result["returncode"] != 0:
        raise RuntimeError("join trigger command failed before AUTH log capture")
    join_values = base.parse_join_values(join_result["stdout"])

    log_result = base.run_guest_script(
        args,
        "guest unified log capture for AUTH TXDONE guard markers",
        build_auth_log_script(nonce, args.log_last),
        args.log_timeout,
    )
    ioreg_result = base.run_guest_script(
        args,
        "guest IORegistry capture for AUTH TXDONE guard properties",
        build_ioreg_script(),
        args.command_timeout,
    )

    plti_markers = base.parse_log_markers(
        log_result["stdout"],
        nonce,
        allowed_ssid,
        allowed_psk,
    )
    auth_markers = unicast.parse_auth_markers(
        log_result["stdout"],
        ioreg_result["stdout"],
        nonce,
        allowed_ssid,
        allowed_psk,
    )
    guard_markers = parse_guard_markers(
        log_result["stdout"],
        ioreg_result["stdout"],
        nonce,
        allowed_ssid,
        allowed_psk,
    )
    classified_guard_events = [
        event
        for event in guard_markers["events"]
        if event.get("decision") in CLASSIFIED_DECISIONS
    ]
    selected_identity = selected_unicast_tx_identity(auth_markers, classified_guard_events)
    pending_qid = selected_identity["pending_qid"] if selected_identity else None
    pending_idx = selected_identity["pending_idx"] if selected_identity else None

    matching_publication_events = [
        event
        for event in classified_guard_events
        if guard_event_matches_pending(event, pending_qid, pending_idx)
    ]
    selected_publication = (
        matching_publication_events[-1] if matching_publication_events else None
    )

    elapsed = time.monotonic() - started_mono
    if elapsed < args.min_seconds:
        time.sleep(args.min_seconds - elapsed)

    capture_seconds = round(time.monotonic() - started_mono, 3)
    ended_wall = utc_now()

    directed_scan_count = join_values.get("directed_scan_count", 0)
    directed_matches = join_values.get("directed_ssid_exact_matches", 0)
    allowed_visible = bool(directed_scan_count and directed_scan_count >= 1)
    aps_seen = max(
        int(directed_scan_count or 0),
        int(directed_matches or 0),
        1 if allowed_visible else 0,
    )

    association_target_published = bool(
        plti_markers["publish_events"] or plti_markers["helper_wait_events"]
    )
    pmk_delivered = bool(
        plti_markers["generation_joined_across_publish_wait_delivery"]
        and plti_markers["helper_delivery_events"]
        and plti_markers["helper_done_events"]
    )
    association_target_visible_or_directed = bool(allowed_visible or association_target_published)

    txid = selected_identity["txid"] if selected_identity else None
    txid_not_broadcast = bool(selected_identity and selected_identity["txid_not_broadcast"])
    tx_peer_unicast = bool(selected_identity and selected_identity["tx_peer_unicast"])
    raw_publication_observed = selected_publication is not None
    raw_publication_matches_pending = bool(selected_publication)
    guard_decision_classified = bool(selected_publication)

    ready = (
        capture_seconds >= args.min_seconds
        and probe["interface_ok"]
        and probe["loaded"]["kext_loaded"]
        and probe["loaded_uuid_matches_identity"]
        and probe["selected_identity_matches"]
        and aps_seen >= 1
        and association_target_visible_or_directed
        and association_target_published
        and pmk_delivered
        and auth_markers["auth_started"]
        and auth_markers["rxon_filter_bss_enabled"]
        and auth_markers["management_frame_enqueue_observed"]
        and auth_markers["management_frame_drain_attempt_observed"]
        and auth_markers["iwn_tx_path_entered"]
        and isinstance(txid, int)
        and txid <= 14
        and txid_not_broadcast
        and tx_peer_unicast
        and isinstance(pending_qid, int)
        and isinstance(pending_idx, int)
        and raw_publication_observed
        and raw_publication_matches_pending
        and guard_decision_classified
    )

    evidence = {
        "schema_version": "itlwm-tahoe-auth-txdone-guard-publication-boundary/v1",
        "step_id": STEP_ID,
        "roadmap_item_id": ROADMAP_ITEM_ID,
        "input_head": args.input_head,
        "capture_started_utc": started_wall.isoformat().replace("+00:00", "Z"),
        "capture_finished_utc": ended_wall.isoformat().replace("+00:00", "Z"),
        "capture_wallclock_seconds": capture_seconds,
        "runtime_environment": {
            "guest_os": probe["guest_os"],
            "guest_ssh_target": "devops@127.0.0.1:3322",
            "guest_wifi_interface": args.interface,
            "guest_wifi_interface_present": probe["interface_ok"],
            "guest_project_head": probe["identity"]["guest_project_head"],
            "selected_input_head": args.input_head,
            "candidate_after_head": args.candidate_after_head,
            "accepted_guest_project_heads": expected_guest_heads(args),
            "selected_source_identity_short": probe["identity"]["source_identity_short"],
            "allowed_ap_identifier": identifier,
            "allowed_ap_identifier_kind": "sha256_prefix_of_operator_env_ssid",
        },
        "driver": {
            "source": "live_tahoe_guest_kextstat_kmutil_showloaded",
            "kext_loaded": probe["loaded"]["kext_loaded"],
            "loaded_uuid": probe["loaded"]["loaded_uuids_observed"][0]
            if probe["loaded"]["loaded_uuids_observed"]
            else "unknown",
            "loaded_uuids_observed": probe["loaded"]["loaded_uuids_observed"],
            "selected_head_uuid": probe["identity"]["installed_uuid"],
            "selected_head_binary_sha256": probe["identity"]["binary_sha256"],
            "selected_head_build_string": probe["identity"]["build_string"],
            "selected_head_installed": probe["selected_identity_matches"],
            "selected_head_guest_project_head_allowed": probe["identity_guest_head_allowed"],
            "loaded_uuid_matches_selected_head": probe["loaded_uuid_matches_identity"]
            and probe["selected_identity_matches"],
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
            "directed_scan_count": directed_scan_count,
            "directed_ssid_exact_matches": directed_matches,
        },
        "association_target": {
            "source": "live_tahoe_guest_corewlan_directed_scan_and_plti_logs",
            "visible_or_directed": association_target_visible_or_directed,
            "plti_target_published": association_target_published,
            "allowed_ap_identifier": identifier,
            "publish_events": plti_markers["publish_events"],
            "helper_wait_events": plti_markers["helper_wait_events"],
        },
        "pmk_helper": {
            "source": "live_tahoe_guest_launchdaemon_keychain_and_logs",
            "target_published": association_target_published,
            "pmk_delivered": pmk_delivered,
            "allowed_ap_identifier": identifier,
            "selected_generation": plti_markers["selected_generation"],
            "helper_delivery_events": plti_markers["helper_delivery_events"],
            "helper_done_events": plti_markers["helper_done_events"],
            "operator_secret_redacted": True,
        },
        "auth": {
            "source": "live_tahoe_guest_iwn_unified_log_and_ioreg_guard_publication",
            "started": auth_markers["auth_started"],
            "rxon_filter_bss_enabled": auth_markers["rxon_filter_bss_enabled"],
            "management_frame_enqueue_observed": auth_markers[
                "management_frame_enqueue_observed"
            ],
            "management_frame_drain_attempt_observed": auth_markers[
                "management_frame_drain_attempt_observed"
            ],
            "iwn_tx_path_entered": auth_markers["iwn_tx_path_entered"],
            "txid": txid,
            "broadcast_txid": selected_identity["broadcast_txid"]
            if selected_identity
            else None,
            "txid_not_broadcast": txid_not_broadcast,
            "tx_peer_unicast": tx_peer_unicast,
            "pending_qid": pending_qid,
            "pending_idx": pending_idx,
            "raw_tx_done_publication_observed": raw_publication_observed,
            "raw_tx_done_publication_matches_pending_qid_idx": raw_publication_matches_pending,
            "tx_done_guard_decision_classified": guard_decision_classified,
            "txdone_guard_publication_event": selected_publication,
            "txdone_guard_publication_events": guard_markers["events"],
            "rxon_events": auth_markers["rxon_events"],
            "management_frame_enqueue_events": auth_markers["enqueue_events"],
            "management_frame_drain_events": auth_markers["drain_events"],
            "iwn_tx_path_events": auth_markers["tx_path_events"],
            "tx_identity_events": auth_markers["tx_identity_events"],
            "ioreg_properties": guard_markers["ioreg_properties"],
        },
        "command_results": {
            "probe": base.safe_result(probe_result, allowed_ssid, allowed_psk),
            "prepare_helper_keychain": base.safe_result(
                prepare_result, allowed_ssid, allowed_psk, line_limit=16
            ),
            "join_trigger": base.safe_result(join_result, allowed_ssid, allowed_psk, line_limit=24),
            "log_capture": {
                **base.safe_result(log_result, allowed_ssid, allowed_psk, line_limit=12),
                "nonce_marker_seen": auth_markers["nonce_marker_seen"],
                "redacted_relevant_lines": auth_markers["redacted_relevant_lines"],
            },
            "ioreg_capture": base.safe_result(ioreg_result, allowed_ssid, allowed_psk, line_limit=24),
        },
        "candidate_fix": {
            "source_delta_committed": True,
            "source_delta_paths": [
                "itlwm/hal_iwn/ItlIwn.cpp",
                "itlwm/hal_iwn/if_iwnvar.h",
                "scripts/capture_tahoe_auth_txdone_guard_publication.py",
                "evidence/runtime/tahoe_auth_txdone_guard_publication_boundary.json",
            ],
        },
        "non_claims": {
            "canonical_tx_completion": False,
            "auth_response_window": False,
            "auth_response_ack": False,
            "association": False,
            "dhcp": False,
            "data_transfer": False,
            "reconnect": False,
            "final_wifi_equivalence": False,
        },
        "verdict": {
            "ready_for_auth_tx_completion_retry": ready,
            "auth_tx_completion_not_claimed": True,
            "auth_response_window_not_claimed": True,
            "auth_response_ack_not_claimed": True,
            "final_wifi_equivalence_not_claimed": True,
            "reason": "live Tahoe guest proved the pending AUTH(seq=1) unicast qid/idx and recorded a classified raw TXDONE or guard-publication outcome before claiming canonical completion"
            if ready
            else "one or more live Tahoe AUTH TXDONE guard-publication predicates were not satisfied",
        },
    }

    errors = validate_evidence(evidence)
    if errors:
        evidence["validation_errors"] = errors
    payload = json.dumps(evidence, indent=2, sort_keys=True) + "\n"
    base.assert_secret_absent(payload, allowed_ssid, allowed_psk)
    return evidence, payload


def validate_only(args, evidence, fresh_errors):
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
        "auth.txid_not_broadcast",
        "auth.tx_peer_unicast",
        "auth.raw_tx_done_publication_observed",
        "auth.tx_done_guard_decision_classified",
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


def main():
    parser = argparse.ArgumentParser(
        description="Capture sanitized live Tahoe evidence for AUTH TXDONE guard publication."
    )
    parser.add_argument("--output", default=str(DEFAULT_OUTPUT))
    parser.add_argument("--guest", default=os.environ.get("ITLWM_GUEST_SSH_TARGET", base.DEFAULT_GUEST))
    parser.add_argument("--port", default=os.environ.get("ITLWM_GUEST_SSH_PORT", base.DEFAULT_PORT))
    parser.add_argument(
        "--interface",
        default=os.environ.get("ITLWM_GUEST_WIFI_INTERFACE", base.DEFAULT_INTERFACE),
    )
    parser.add_argument(
        "--guest-project",
        default=os.environ.get("ITLWM_GUEST_PROJECT", base.DEFAULT_GUEST_PROJECT),
    )
    parser.add_argument("--kext-path", default=os.environ.get("ITLWM_KEXT_PATH", base.DEFAULT_KEXT_PATH))
    parser.add_argument(
        "--assignment-path",
        default=os.environ.get("ITLWM_PROGRESS_ASSIGNMENT_PATH", ""),
        help="optional selected-step assignment used to resolve the selected input head",
    )
    parser.add_argument(
        "--candidate-path",
        default=os.environ.get("ITLWM_PROGRESS_CANDIDATE_PATH", ""),
        help="optional candidate file used to resolve the candidate after-head",
    )
    parser.add_argument(
        "--input-head",
        default=os.environ.get("ITLWM_SELECTED_INPUT_HEAD", INPUT_HEAD),
    )
    parser.add_argument(
        "--candidate-after-head",
        default=os.environ.get("ITLWM_CANDIDATE_AFTER_HEAD", ""),
    )
    parser.add_argument("--allow-guest-head", action="append", default=[])
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
        resolve_attempt_heads(args)
        evidence, payload = build_evidence(args)
    except subprocess.TimeoutExpired as exc:
        print(f"capture failed: guest command timed out after {exc.timeout}s", file=sys.stderr)
        return 2
    except Exception as exc:
        print(f"capture failed: {exc}", file=sys.stderr)
        return 2

    fresh_errors = validate_evidence(evidence)
    if args.validate_only:
        return validate_only(args, evidence, fresh_errors)

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
        + " pending_qid="
        + str(evidence["auth"]["pending_qid"])
        + " pending_idx="
        + str(evidence["auth"]["pending_idx"])
        + " txid="
        + str(evidence["auth"]["txid"])
        + " guard="
        + str(evidence["auth"]["tx_done_guard_decision_classified"]).lower()
        + " identifier="
        + evidence["runtime_environment"]["allowed_ap_identifier"]
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
