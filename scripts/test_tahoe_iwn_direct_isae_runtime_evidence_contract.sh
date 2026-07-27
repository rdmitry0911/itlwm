#!/usr/bin/env bash
# Validate the aggregate-only direct-ISAE runtime attestation.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
runner="$root/scripts/run_tahoe_iwn_direct_isae_runtime.py"
mode=self-test
evidence=""

usage() {
    cat >&2 <<'EOF'
usage: test_tahoe_iwn_direct_isae_runtime_evidence_contract.sh [--self-test]
       test_tahoe_iwn_direct_isae_runtime_evidence_contract.sh --evidence SAFE.json
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --self-test)
            mode=self-test
            shift
            ;;
        --evidence)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            mode=validate
            evidence="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            usage
            exit 2
            ;;
    esac
done

if [ "$mode" = validate ]; then
    [ -f "$evidence" ] && [ ! -L "$evidence" ] || {
        printf 'FAIL: evidence file missing or unsafe\n' >&2
        exit 2
    }
fi

python3 - "$root/scripts" "$mode" "$evidence" <<'PY'
import json
import re
import sys
import tempfile
from pathlib import Path

scripts = Path(sys.argv[1])
mode = sys.argv[2]
evidence_path = sys.argv[3]
sys.path.insert(0, str(scripts))
import run_tahoe_iwn_direct_isae_runtime as runner


def reject_duplicate_keys(pairs):
    document = {}
    for key, value in pairs:
        if key in document:
            raise ValueError("duplicate JSON key")
        document[key] = value
    return document


def reject_nonfinite(value):
    raise ValueError(f"non-finite JSON constant: {value}")


def parse(text):
    return json.loads(
        text,
        object_pairs_hook=reject_duplicate_keys,
        parse_constant=reject_nonfinite,
    )


forbidden_label = re.compile(
    r"(ssid|bssid|passphrase|password|keychain|credential|networksetup|"
    r"corewlan|ioregistry|raw[-_ ]?capture)",
    re.IGNORECASE,
)
ipv4 = re.compile(r"\b(?:[0-9]{1,3}\.){3}[0-9]{1,3}\b")
ipv6 = re.compile(r"\b(?:[0-9A-Fa-f]{0,4}:){2,}[0-9A-Fa-f:]*\b")
mac = re.compile(r"\b(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}\b")


def inspect(value):
    if isinstance(value, dict):
        for key, item in value.items():
            if forbidden_label.search(key):
                raise ValueError("forbidden field label")
            inspect(item)
    elif isinstance(value, list):
        for item in value:
            inspect(item)
    elif isinstance(value, str):
        timestamp = re.fullmatch(
            r"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\+00:00",
            value,
        )
        if (forbidden_label.search(value) or ipv4.search(value) or
                (timestamp is None and ipv6.search(value)) or mac.search(value)):
            raise ValueError("identity-bearing value")


def validate(document):
    inspect(document)
    return runner.validate_evidence_document(document)


def passing_document():
    state = runner.State(
        direct_sha256="a" * 64,
        trace_sha256="b" * 64,
        host_dtrace_before=True,
        host_dtrace_after=True,
        guest_dtrace_before=True,
        guest_dtrace_after=True,
        artifacts_pre_bound=True,
        artifacts_post_bound=True,
        readiness_observed=True,
        submission_category="queued",
        dispatch_outcome="started",
        trace_reset_may_be_active=True,
        trace_reset_ack=True,
        initial_snapshot_synchronized=True,
        trace_seal_ack=True,
        trace_final_disabled=True,
        report_one_read=True,
        report_two_read=True,
        report_double_read_stable=True,
        capture_generation=1,
        trace_backend="IWN",
        trace_entry_count=1,
        trace_dropped=0,
        trace_integrity="ok",
        trace_episode_count=1,
        trace_active_episode=0,
        trace_verdict="DIRECT_SAE_4WAY_PORT_VALID",
        trace_first_missing_stage="none",
        result="PASS",
        failure_phase="none",
    )
    return runner.evidence_document(state)


if mode == "validate":
    try:
        validate(parse(Path(evidence_path).read_text(encoding="utf-8")))
    except Exception as error:
        raise SystemExit("FAIL: direct-ISAE runtime evidence contract")
    print("PASS: direct-ISAE runtime evidence contract")
    raise SystemExit(0)

document = passing_document()
validate(document)
for malformed in (
    {**document, "extra": True},
    {**document, "retention": {**document["retention"], "opaque_request_retained": True}},
    {**document, "input_handling": {**document["input_handling"], "submit_category": "untrusted"}},
    {**document, "input_handling": {**document["input_handling"], "dispatch_outcome": "untrusted"}},
    {**document, "input_handling": {**document["input_handling"], "dispatch_outcome": "pending"}},
    {**document, "input_handling": {**document["input_handling"], "readiness_observed": False}},
    {**document, "trace": {**document["trace"], "entry_count": 0}},
    {**document, "trace": {**document["trace"], "cleanup_fallback_attempted": True}},
    {**document, "trace": {**document["trace"], "cleanup_seal_confirmed": True}},
    {**document, "environment": {**document["environment"], "profile_or_route_changed": True}},
    {**document, "created_at_utc": "2026-02-30T00:00:00+00:00"},
):
    try:
        validate(malformed)
    except ValueError:
        pass
    else:
        raise SystemExit("FAIL: direct-ISAE evidence malformed fixture accepted")
for malformed in (
    '{"schema":"one","schema":"two"}',
    '{"schema":NaN}',
):
    try:
        parse(malformed)
    except ValueError:
        pass
    else:
        raise SystemExit("FAIL: direct-ISAE evidence duplicate/nonfinite JSON accepted")
with tempfile.TemporaryDirectory(prefix="aiam-isae-evidence-") as directory:
    path = Path(directory) / "evidence.json"
    path.write_text(json.dumps(document), encoding="utf-8")
    validate(parse(path.read_text(encoding="utf-8")))
print("PASS: direct-ISAE runtime evidence self-test")
PY
