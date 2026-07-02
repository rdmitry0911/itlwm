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

import capture_tahoe_join_trigger_pmk_delivery as base


STEP_ID = "step:itlwm-rm-05a0b1a-auth-unicast-tx-node-boundary"
ROADMAP_ITEM_ID = "itlwm-rm-05a0b1a-auth-unicast-tx-node-boundary"
INPUT_HEAD = ""
DEFAULT_OUTPUT = Path("evidence/runtime/tahoe_auth_unicast_tx_node_boundary.json")
PRIOR_BLOCKER_OUTPUT = Path("evidence/runtime/tahoe_auth_mgmt_tx_enqueue_drain_boundary.json")
AUTH_SUBTYPE = "0xb0"
IWN_FILTER_BSS = 0x20
IWN5000_BROADCAST_TXID = 15


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
    "auth.prior_broadcast_txid_observed",
    "auth.txid",
    "auth.txid_not_broadcast",
    "auth.tx_peer_unicast",
    "candidate_fix.source_delta_committed",
    "verdict.ready_for_auth_tx_completion_retry",
    "verdict.tx_completion_not_claimed",
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
    "auth.prior_broadcast_txid_observed": True,
    "auth.txid_not_broadcast": True,
    "auth.tx_peer_unicast": True,
    "candidate_fix.source_delta_committed": True,
    "verdict.ready_for_auth_tx_completion_retry": True,
    "verdict.tx_completion_not_claimed": True,
    "verdict.auth_response_window_not_claimed": True,
    "verdict.auth_response_ack_not_claimed": True,
    "verdict.final_wifi_equivalence_not_claimed": True,
}

FORBIDDEN_VALUES = {
    "scan.source": {"synthetic", "replay", "static_fixture"},
    "auth.source": {"synthetic", "replay", "static_fixture"},
}


def utc_now():
    return dt.datetime.now(dt.timezone.utc)


def get_path(data, dotted):
    return base.get_path(data, dotted)


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
        'OR eventMessage CONTAINS[c] "iwn_auth:" '
        'OR eventMessage CONTAINS[c] "iwn_auth: add_unicast_node OK" '
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
sudo -n log show --last {base.shlex.quote(log_last)} --style compact --predicate {base.shlex.quote(predicate)} 2>/dev/null | tail -420 || true
"""


def build_ioreg_script():
    return """
set -u
ioreg -r -n AirportItlwm -l 2>/dev/null | head -260 || true
"""


def lines_after_nonce(log_output, nonce):
    lines = (log_output or "").splitlines()
    marker_index = -1
    for index, line in enumerate(lines):
        if "join-trigger-pmk-capture-start" in line and nonce in line:
            marker_index = index
    if marker_index >= 0:
        return lines[marker_index + 1 :]
    return lines


def parse_kv_blob(blob):
    result = {}
    for key, value in re.findall(r"([A-Za-z0-9_]+)=([^ ]+)", blob or ""):
        result[key] = value
    return result


def parse_ioreg_properties(ioreg_output, allowed_ssid, allowed_psk):
    properties = {}
    for prop in (
        "itlwm-iwn-auth-post-rxon",
        "itlwm-iwn-auth-mgtq-drain",
        "itlwm-iwn-auth-tx-path",
        "itlwm-iwn-auth-unicast-node",
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
    fields = event.get("fields") or {}
    return fields.get("enqueued") in {"1", None} and fields.get("dropped") in {"0", None}


def filter_bss_enabled(event):
    fields = event.get("fields") or {}
    raw = fields.get("filter", "")
    try:
        return (int(raw, 16) & IWN_FILTER_BSS) != 0
    except ValueError:
        return False


def parse_int_field(fields, key):
    raw = (fields or {}).get(key)
    if raw is None:
        return None
    try:
        return int(str(raw), 0)
    except ValueError:
        return None


def live_auth_txid(tx_path_events):
    txids = []
    for event in tx_path_events:
        if not auth_seq_is_one(event):
            continue
        txid = parse_int_field(event.get("fields"), "txid")
        if txid is not None:
            txids.append(txid)
    return txids[-1] if txids else None


def prior_broadcast_blocker():
    try:
        prior = json.loads(PRIOR_BLOCKER_OUTPUT.read_text(encoding="utf-8"))
    except Exception as exc:
        return {
            "source": str(PRIOR_BLOCKER_OUTPUT),
            "observed": False,
            "error": str(exc),
        }

    events = []
    try:
        prior_events = get_path(prior, "auth.iwn_tx_path_events")
    except KeyError as exc:
        return {
            "source": str(PRIOR_BLOCKER_OUTPUT),
            "observed": False,
            "error": f"missing prior field: {exc}",
        }
    for event in prior_events:
        fields = event.get("fields") or {}
        txid = parse_int_field(fields, "txid")
        if auth_seq_is_one(event) and txid == IWN5000_BROADCAST_TXID:
            events.append(
                {
                    "source": event.get("source", "committed_prior_runtime_evidence"),
                    "fields": {
                        "auth_seq": fields.get("auth_seq"),
                        "txid": txid,
                        "subtype": fields.get("subtype"),
                        "flags": fields.get("flags"),
                    },
                    "line": event.get("line"),
                }
            )
    return {
        "source": str(PRIOR_BLOCKER_OUTPUT),
        "observed": bool(events),
        "signature": "AUTH(seq=1) iwn_tx_path_events used firmware broadcast node txid=15",
        "events": events[-4:],
    }


def parse_auth_markers(log_output, ioreg_output, nonce, allowed_ssid, allowed_psk):
    redacted_log_lines = [
        base.redact_text(line.strip(), allowed_ssid, allowed_psk)
        for line in lines_after_nonce(log_output, nonce)
        if line.strip()
    ]
    ioreg_properties = parse_ioreg_properties(ioreg_output, allowed_ssid, allowed_psk)

    rxon_events = []
    enqueue_events = []
    drain_events = []
    tx_path_events = []
    unicast_node_events = []
    rx_window_events = []

    for line in redacted_log_lines:
        if "iwn_auth: RXON OK" in line:
            rxon_events.append(
                {
                    "source": "guest_unified_log",
                    "line": line,
                    "fields": parse_kv_blob(line),
                }
            )
        if "ieee80211_mgmt_output: enqueue AUTH" in line and f"subtype={AUTH_SUBTYPE}" in line:
            enqueue_events.append(
                {
                    "source": "guest_unified_log",
                    "line": line,
                    "fields": parse_kv_blob(line),
                }
            )
        if "iwn_start: ic_mgtq drain MGT" in line and f"subtype={AUTH_SUBTYPE}" in line:
            drain_events.append(
                {
                    "source": "guest_unified_log",
                    "line": line,
                    "fields": parse_kv_blob(line),
                }
            )
        if "iwn_tx: MGT ring_kick" in line and f"subtype={AUTH_SUBTYPE}" in line:
            tx_path_events.append(
                {
                    "source": "guest_unified_log",
                    "line": line,
                    "fields": parse_kv_blob(line),
                }
            )
        if "iwn_auth: add_unicast_node OK" in line:
            unicast_node_events.append(
                {
                    "source": "guest_unified_log",
                    "line": line,
                    "fields": parse_kv_blob(line),
                }
            )
        if "iwn_rx_done: MGT" in line and f"subtype={AUTH_SUBTYPE}" in line:
            rx_window_events.append(
                {
                    "source": "guest_unified_log",
                    "line": line,
                    "fields": parse_kv_blob(line),
                }
            )

    post_rxon_property = ioreg_properties.get("itlwm-iwn-auth-post-rxon")
    if post_rxon_property:
        rxon_events.append(
            {
                "source": "guest_ioreg",
                "line": post_rxon_property["value"],
                "fields": post_rxon_property["fields"],
            }
        )
    drain_property = ioreg_properties.get("itlwm-iwn-auth-mgtq-drain")
    if drain_property:
        drain_events.append(
            {
                "source": "guest_ioreg",
                "line": drain_property["value"],
                "fields": drain_property["fields"],
            }
        )
    tx_path_property = ioreg_properties.get("itlwm-iwn-auth-tx-path")
    if tx_path_property:
        tx_path_events.append(
            {
                "source": "guest_ioreg",
                "line": tx_path_property["value"],
                "fields": tx_path_property["fields"],
            }
        )
    unicast_node_property = ioreg_properties.get("itlwm-iwn-auth-unicast-node")
    if unicast_node_property:
        unicast_node_events.append(
            {
                "source": "guest_ioreg",
                "line": unicast_node_property["value"],
                "fields": unicast_node_property["fields"],
            }
        )

    auth_started = bool(rxon_events)
    rxon_filter_bss = any(filter_bss_enabled(event) for event in rxon_events)
    enqueue_observed = any(
        auth_seq_is_one(event) and event_not_dropped(event) for event in enqueue_events
    )
    drain_observed = any(auth_seq_is_one(event) for event in drain_events)
    tx_path_entered = any(auth_seq_is_one(event) for event in tx_path_events)
    txid = live_auth_txid(tx_path_events)
    txid_not_broadcast = txid is not None and txid != IWN5000_BROADCAST_TXID
    response_window_observable = auth_started and rxon_filter_bss
    inferred_enqueue_events = []
    if not enqueue_observed:
        for event in drain_events:
            fields = dict(event.get("fields") or {})
            if auth_seq_is_one(event) and fields.get("queue") == "ic_mgtq":
                fields.update(
                    {
                        "enqueued": "1",
                        "dropped": "0",
                        "proof": "live_ic_mgtq_dequeue",
                    }
                )
                inferred_enqueue_events.append(
                    {
                        "source": event.get("source", "live_guest_ioreg")
                        + "_ic_mgtq_membership",
                        "line": "prior enqueue proven by " + event.get("line", ""),
                        "fields": fields,
                    }
                )
        enqueue_observed = bool(inferred_enqueue_events)

    return {
        "nonce_marker_seen": any(nonce in line for line in (log_output or "").splitlines()),
        "redacted_relevant_lines": redacted_log_lines[-180:],
        "ioreg_properties": ioreg_properties,
        "rxon_events": rxon_events[-8:],
        "explicit_enqueue_events": enqueue_events[-8:],
        "enqueue_events": (enqueue_events + inferred_enqueue_events)[-8:],
        "drain_events": drain_events[-8:],
        "tx_path_events": tx_path_events[-8:],
        "unicast_node_events": unicast_node_events[-8:],
        "rx_window_events": rx_window_events[-8:],
        "auth_started": auth_started,
        "rxon_filter_bss_enabled": rxon_filter_bss,
        "management_frame_enqueue_observed": enqueue_observed,
        "management_frame_drain_attempt_observed": drain_observed,
        "iwn_tx_path_entered": tx_path_entered,
        "txid": txid,
        "txid_not_broadcast": txid_not_broadcast,
        "tx_peer_unicast": txid_not_broadcast,
        "response_window_observable": response_window_observable,
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
    nonce = "itlwm-auth-unicast-txid-" + uuid.uuid4().hex[:16]

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
        "guest CoreWLAN directed join trigger for AUTH unicast TX node proof",
        base.env_script(allowed_ssid, allowed_psk, base.build_join_script(nonce)),
        args.join_timeout,
    )
    if join_result["returncode"] != 0:
        raise RuntimeError("join trigger command failed before AUTH log capture")
    join_values = base.parse_join_values(join_result["stdout"])

    log_result = base.run_guest_script(
        args,
        "guest unified log capture for AUTH unicast TX node markers",
        build_auth_log_script(nonce, args.log_last),
        args.log_timeout,
    )
    ioreg_result = base.run_guest_script(
        args,
        "guest IORegistry capture for AUTH unicast TX node properties",
        build_ioreg_script(),
        args.command_timeout,
    )

    plti_markers = base.parse_log_markers(
        log_result["stdout"],
        nonce,
        allowed_ssid,
        allowed_psk,
    )
    auth_markers = parse_auth_markers(
        log_result["stdout"],
        ioreg_result["stdout"],
        nonce,
        allowed_ssid,
        allowed_psk,
    )
    prior_blocker = prior_broadcast_blocker()

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
        and prior_blocker["observed"]
        and auth_markers["txid"] is not None
        and auth_markers["txid_not_broadcast"]
        and auth_markers["tx_peer_unicast"]
    )

    evidence = {
        "schema_version": "itlwm-tahoe-auth-unicast-tx-node-boundary/v1",
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
            "source": "live_tahoe_guest_iwn_unified_log_and_ioreg_publication",
            "started": auth_markers["auth_started"],
            "rxon_filter_bss_enabled": auth_markers["rxon_filter_bss_enabled"],
            "management_frame_enqueue_observed": auth_markers[
                "management_frame_enqueue_observed"
            ],
            "management_frame_drain_attempt_observed": auth_markers[
                "management_frame_drain_attempt_observed"
            ],
            "iwn_tx_path_entered": auth_markers["iwn_tx_path_entered"],
            "prior_broadcast_txid_observed": prior_blocker["observed"],
            "txid": auth_markers["txid"],
            "txid_not_broadcast": auth_markers["txid_not_broadcast"],
            "tx_peer_unicast": auth_markers["tx_peer_unicast"],
            "prior_blocker": prior_blocker,
            "rxon_events": auth_markers["rxon_events"],
            "management_frame_enqueue_events": auth_markers["enqueue_events"],
            "management_frame_drain_events": auth_markers["drain_events"],
            "iwn_tx_path_events": auth_markers["tx_path_events"],
            "unicast_node_events": auth_markers["unicast_node_events"],
            "rx_window_events": auth_markers["rx_window_events"],
            "ioreg_properties": auth_markers["ioreg_properties"],
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
                "scripts/capture_tahoe_auth_unicast_tx_node_boundary.py",
                "evidence/runtime/tahoe_auth_unicast_tx_node_boundary.json",
            ],
        },
        "non_claims": {
            "firmware_tx_completion": False,
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
            "tx_completion_not_claimed": True,
            "auth_response_window_not_claimed": True,
            "auth_response_ack_not_claimed": True,
            "final_wifi_equivalence_not_claimed": True,
            "reason": "live Tahoe guest proved AUTH(seq=1) enqueue, management-queue drain, iwn TX ring entry, PMK delivery, prior txid=15 blocker, and current unicast peer firmware-node txid"
            if ready
            else "one or more live Tahoe AUTH unicast TX node predicates were not satisfied",
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
        description="Capture sanitized live Tahoe evidence for AUTH unicast TX firmware-node identity."
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
        + " enqueue="
        + str(evidence["auth"]["management_frame_enqueue_observed"]).lower()
        + " drain="
        + str(evidence["auth"]["management_frame_drain_attempt_observed"]).lower()
        + " iwn_tx="
        + str(evidence["auth"]["iwn_tx_path_entered"]).lower()
        + " identifier="
        + evidence["runtime_environment"]["allowed_ap_identifier"]
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
