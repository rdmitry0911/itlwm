#!/usr/bin/env python3
"""Evaluate one isolated, credential-safe Tahoe SAE/PMK capture epoch.

Canonical input is the control acknowledgement, post-capture snapshot, and
trace.  ``report.txt`` deliberately is not parsed because it duplicates the
other two records.  The evaluator never prints SSIDs, BSSIDs, pointers, or
key material; it turns the one captured association into a structural verdict.
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


MODE_SAE_PMK = 0x35  # enabled + assoc + control + PMK; no data/intervention
POLICY_REJECT_WPA3 = 0x01
POLICY_PSK_PMK_ELIGIBLE = 0x02
POLICY_LOCAL_PSK = 0x04
PURE_SAE = 0x1000
TRACE_CAPACITY = 128


@dataclass(frozen=True)
class Event:
    sequence: int
    kind: str
    path: str
    result: int
    auth_upper: int | None = None
    policy: int | None = None
    decision: str | None = None
    generation: int | None = None
    eapol: bool | None = None
    link_state: int | None = None


@dataclass(frozen=True)
class Capture:
    trigger_exit: str
    abi: int | None
    snapshot_mode: int | None
    snapshot_block: int | None
    snapshot_control_sequence: int | None
    control_sequence: int | None
    control_mode: int | None
    control_block: int | None
    trace_version: int | None
    trace_count: int | None
    trace_next: int | None
    trace_dropped: int | None
    events: tuple[Event, ...]
    snapshot_counts: dict[str, int]
    routes_changed: str
    interface_changed: str


def read_optional(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except FileNotFoundError:
        return ""


def parse_manifest(text: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in text.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            result[key] = value
    return result


def changed(before: Path, after: Path) -> str:
    old, new = read_optional(before), read_optional(after)
    if not old or not new:
        return "unavailable"
    return "yes" if old != new else "no"


def field(line: str, key: str, base: int = 10) -> int | None:
    match = re.search(rf"\b{re.escape(key)}=([0-9a-fA-Fx-]+)", line)
    if not match:
        return None
    try:
        return int(match.group(1), base)
    except ValueError:
        return None


def parse_event(line: str) -> Event | None:
    common = re.match(
        r"#(\d+)\s+kind=([a-z0-9-]+)\s+path=([a-z0-9-]+)\s+result=0x([0-9a-fA-F]+)",
        line,
    )
    if not common:
        return None
    sequence, kind, path, result_hex = common.groups()
    auth_upper = policy = generation = link_state = None
    decision: str | None = None
    eapol: bool | None = None
    if kind == "auth-policy":
        auth = re.search(r"\bauth=0x[0-9a-fA-F]+/0x([0-9a-fA-F]+)", line)
        auth_upper = int(auth.group(1), 16) if auth else None
        policy = field(line, "policy", 16)
    elif kind == "pmk-ingress":
        match = re.search(r"\bdecision=([a-z0-9-]+)", line)
        decision = match.group(1) if match else None
    elif kind in {"plti-publish", "plti-deliver"}:
        match = re.search(r"\bdecision=([a-z0-9-]+)\s+generation=(\d+)", line)
        if match:
            decision, generation_text = match.groups()
            generation = int(generation_text)
    elif kind in {"tx", "rx"}:
        value = field(line, "eapol")
        eapol = None if value is None else value != 0
    elif kind == "link-state":
        link_state = field(line, "link_state")
    return Event(int(sequence), kind, path, int(result_hex, 16), auth_upper,
                 policy, decision, generation, eapol, link_state)


def parse_capture(directory: Path) -> Capture:
    manifest = parse_manifest(read_optional(directory / "manifest.txt"))
    control = read_optional(directory / "regdiag-control.txt")
    snapshot = read_optional(directory / "post-snapshot.txt")
    trace = read_optional(directory / "trace.txt")

    control_match = re.search(
        r"\bseq=(\d+)\s+applied=1\s+mode=0x([0-9a-fA-F]+)\s+block=0x([0-9a-fA-F]+)",
        control,
    )
    control_sequence = int(control_match.group(1)) if control_match else None
    control_mode = int(control_match.group(2), 16) if control_match else None
    control_block = int(control_match.group(3), 16) if control_match else None

    snapshot_header = re.search(
        r"^version=(\d+)\s+size=\d+\s+seq=\d+\s+control_seq=(\d+)",
        snapshot,
        re.MULTILINE,
    )
    abi = int(snapshot_header.group(1)) if snapshot_header else None
    snapshot_control_sequence = int(snapshot_header.group(2)) if snapshot_header else None
    mode_line = next((line for line in snapshot.splitlines()
                      if line.startswith("mode=")), "")
    snapshot_mode = field(mode_line, "mode", 16)
    snapshot_block = field(mode_line, "block", 16)

    trace_header = re.search(
        r"^version=(\d+)\s+count=(\d+)\s+next=(\d+)\s+dropped=(\d+)",
        trace,
        re.MULTILINE,
    )
    trace_version = int(trace_header.group(1)) if trace_header else None
    trace_count = int(trace_header.group(2)) if trace_header else None
    trace_next = int(trace_header.group(3)) if trace_header else None
    trace_dropped = int(trace_header.group(4)) if trace_header else None
    events = tuple(event for event in
                   (parse_event(line) for line in trace.splitlines())
                   if event is not None)

    count_line = next((line for line in snapshot.splitlines()
                       if line.startswith("counts ")), "")
    snapshot_counts = {
        name: field(count_line, name) or 0
        for name in ("tx", "rx", "eapol_tx", "eapol_rx")
    }
    return Capture(
        trigger_exit=manifest.get("trigger_exit", "missing"),
        abi=abi,
        snapshot_mode=snapshot_mode,
        snapshot_block=snapshot_block,
        snapshot_control_sequence=snapshot_control_sequence,
        control_sequence=control_sequence,
        control_mode=control_mode,
        control_block=control_block,
        trace_version=trace_version,
        trace_count=trace_count,
        trace_next=trace_next,
        trace_dropped=trace_dropped,
        events=events,
        snapshot_counts=snapshot_counts,
        routes_changed=changed(directory / "routes-before.txt",
                               directory / "routes-after.txt"),
        interface_changed=changed(directory / "interface-before.txt",
                                  directory / "interface-after.txt"),
    )


def integrity_errors(capture: Capture) -> list[str]:
    errors: list[str] = []
    if capture.abi != 2:
        errors.append("loaded kext did not expose RegDiag ABI v2")
    if (capture.control_mode != MODE_SAE_PMK or capture.control_block != 0 or
            capture.control_sequence is None):
        errors.append("diagnostic control ACK is not applied mode=0x35 block=0")
    if (capture.snapshot_mode != MODE_SAE_PMK or capture.snapshot_block != 0 or
            capture.snapshot_control_sequence != capture.control_sequence):
        errors.append("post snapshot does not match the SAE/PMK control epoch")
    if (capture.trace_version != 2 or capture.trace_count is None or
            capture.trace_next is None or capture.trace_dropped is None):
        errors.append("trace header is absent or not ABI v2")
        return errors
    if capture.trace_count <= 0 or capture.trace_count > TRACE_CAPACITY:
        errors.append("trace count is outside the fixed ABI ring capacity")
    if capture.trace_count != len(capture.events):
        errors.append("trace header count does not match decoded records")
    if capture.trace_dropped != 0:
        errors.append("trace overflowed; ordering evidence is incomplete")
    expected_start = capture.trace_next - capture.trace_count
    sequences = [event.sequence for event in capture.events]
    if sequences != list(range(expected_start, capture.trace_next)):
        errors.append("trace sequence is not a contiguous, unique ring window")
    packet_events = [event for event in capture.events if event.kind in {"tx", "rx"}]
    if any(event.eapol is not True for event in packet_events):
        errors.append("PMK-mode trace contains non-EAPOL or undecoded packet data")
    tx_events = [event for event in packet_events if event.kind == "tx"]
    rx_events = [event for event in packet_events if event.kind == "rx"]
    counts = capture.snapshot_counts
    if (counts["tx"] != counts["eapol_tx"] or counts["rx"] != counts["eapol_rx"] or
            counts["tx"] != len(tx_events) or counts["rx"] != len(rx_events)):
        errors.append("snapshot packet counters do not reconcile with the EAPOL trace")
    return errors


def events_after(events: Iterable[Event], sequence: int) -> list[Event]:
    return [event for event in events if event.sequence > sequence]


def has_psk_pmk_progress(events: Iterable[Event]) -> bool:
    if any(event.kind == "pmk-ingress" and event.decision == "accepted"
           for event in events):
        return True
    published = {
        event.generation for event in events
        if event.kind == "plti-publish" and event.decision == "accepted" and
        event.generation not in {None, 0}
    }
    return any(event.kind == "plti-deliver" and event.decision == "accepted" and
               event.generation in published for event in events)


def has_eapol_pair(events: Iterable[Event]) -> bool:
    event_list = list(events)
    return (any(event.kind == "tx" and event.eapol and event.result == 0
                for event in event_list) and
            any(event.kind == "rx" and event.eapol and event.result == 0
                for event in event_list))


def has_link_up(events: Iterable[Event]) -> bool:
    return any(event.kind == "link-state" and event.result == 0 and
               event.link_state == 2 for event in events)


def evaluate_psk(capture: Capture) -> list[str]:
    reasons: list[str] = []
    candidates = [
        event for event in capture.events
        if event.kind == "auth-policy" and event.policy is not None and
        (event.policy & POLICY_REJECT_WPA3) == 0 and
        (event.policy & (POLICY_PSK_PMK_ELIGIBLE | POLICY_LOCAL_PSK)) ==
        (POLICY_PSK_PMK_ELIGIBLE | POLICY_LOCAL_PSK)
    ]
    if not candidates:
        reasons.append("no PSK-eligible non-WPA3 association policy")
        return reasons
    if not any(any(event.kind in {"public-assoc", "hidden-assoc"} and
                       event.result == 0 for event in events_after(capture.events,
                                                                    candidate.sequence))
               for candidate in candidates):
        reasons.append("PSK association did not reach a successful driver ingress")
    if not has_psk_pmk_progress(capture.events):
        reasons.append("no accepted direct PMK or matched PLTI publish/deliver pair")
    if not has_eapol_pair(capture.events):
        reasons.append("missing successful EAPOL TX/RX pair")
    if not has_link_up(capture.events):
        reasons.append("no successful link-up publication")
    return reasons


def evaluate_sae_reject(capture: Capture) -> list[str]:
    reasons: list[str] = []
    candidates = [
        event for event in capture.events
        if event.kind == "auth-policy" and event.auth_upper == PURE_SAE and
        event.policy is not None and (event.policy & POLICY_REJECT_WPA3) != 0
    ]
    if not candidates:
        return ["no pure-SAE reject policy carrier"]
    for candidate in candidates:
        tail = events_after(capture.events, candidate.sequence)
        if not any(event.kind in {"public-assoc", "hidden-assoc"} and
                   event.result != 0 for event in tail):
            continue
        if has_psk_pmk_progress(tail):
            continue
        if any(event.kind in {"tx", "rx"} and event.eapol for event in tail):
            continue
        if has_link_up(tail):
            continue
        return []
    reasons.append("pure SAE did not terminate before PMK/PLTI/EAPOL/link")
    return reasons


def evaluate(capture: Capture, expected: str) -> tuple[bool, list[str]]:
    reasons = integrity_errors(capture)
    if not any(event.kind == "auth-policy" for event in capture.events):
        reasons.append("no association auth-policy carrier reached the driver")
    if expected == "wpa2-psk":
        reasons.extend(evaluate_psk(capture))
    elif expected == "sae-reject":
        reasons.extend(evaluate_sae_reject(capture))
    return not reasons, reasons


def render(capture: Capture, expected: str, passed: bool,
           reasons: Iterable[str]) -> str:
    events = capture.events
    auth = sum(event.kind == "auth-policy" for event in events)
    pmk = sum(event.kind == "pmk-ingress" and event.decision == "accepted"
              for event in events)
    publish = sum(event.kind == "plti-publish" and event.decision == "accepted"
                  for event in events)
    deliver = sum(event.kind == "plti-deliver" and event.decision == "accepted"
                  for event in events)
    eapol_tx = sum(event.kind == "tx" and event.eapol for event in events)
    eapol_rx = sum(event.kind == "rx" and event.eapol for event in events)
    link_up = sum(event.kind == "link-state" and event.link_state == 2 and
                  event.result == 0 for event in events)
    lines = [
        "SAE/PMK layer capture verdict",
        f"expect={expected} verdict={'PASS' if passed else 'INCONCLUSIVE/FAIL'} trigger_exit={capture.trigger_exit}",
        "diagnostics: abi=%s control_mode=%s control_block=%s trace=%s/%s dropped=%s" % (
            capture.abi if capture.abi is not None else "missing",
            "0x%x" % capture.control_mode if capture.control_mode is not None else "missing",
            "0x%x" % capture.control_block if capture.control_block is not None else "missing",
            capture.trace_count if capture.trace_count is not None else "missing",
            capture.trace_next if capture.trace_next is not None else "missing",
            capture.trace_dropped if capture.trace_dropped is not None else "missing"),
        "stages: auth=%d pmk_direct=%d plti_publish=%d plti_deliver=%d eapol_tx=%d eapol_rx=%d link_up=%d" % (
            auth, pmk, publish, deliver, eapol_tx, eapol_rx, link_up),
        "environment: routes_changed=%s interface_changed=%s" % (
            capture.routes_changed, capture.interface_changed),
    ]
    lines.extend(f"reason={reason}" for reason in reasons)
    return "\n".join(lines) + "\n"


def fixture(directory: Path, lines: list[str], *, control: str | None = None,
            dropped: int = 0, mode: int = MODE_SAE_PMK,
            counts: tuple[int, int] = (0, 0), link_state: int = 1) -> None:
    control = control or "seq=7 applied=1 mode=0x35 block=0x0\n"
    directory.joinpath("manifest.txt").write_text("trigger_exit=0\n")
    directory.joinpath("regdiag-control.txt").write_text(control)
    tx, rx = counts
    directory.joinpath("post-snapshot.txt").write_text(
        "version=2 size=512 seq=9 control_seq=7\n"
        f"mode=0x{mode:x} block=0x0 rt=0x0/0x0/0x0\n"
        f"counts public_assoc=0 hidden_assoc=0 link=0 tx={tx} rx={rx} eapol_tx={tx} eapol_rx={rx} tx_drop=0 rx_drop=0 block=0\n"
        f"last_result public_assoc=0x0 hidden_assoc=0x0 link=0x0 tx=0x0 rx=0x0 link_state={link_state} last_block=0x0 tx_len=0 rx_len=0\n")
    directory.joinpath("trace.txt").write_text(
        f"version=2 count={len(lines)} next={len(lines)} dropped={dropped}\n" +
        "\n".join(lines) + "\n")


def self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="aiam-sae-evaluator-") as temp:
        root = Path(temp)
        psk_lines = [
            "#0 kind=control path=unknown result=0x0 arg0=0 arg1=0x35 arg2=0x0",
            "#1 kind=auth-policy path=hidden-assoc result=0x0 pmf=0 auth=0x0/0x8 rsn_len=22 policy=0x6",
            "#2 kind=hidden-assoc path=hidden-assoc result=0x0 arg0=0 arg1=0x800000000 arg2=0x16",
            "#3 kind=pmk-ingress path=pmk result=0x0 source=cipher-key auth=0x8 key_len=32 decision=accepted",
            "#4 kind=tx path=tx result=0x0 eapol=1 length=120",
            "#5 kind=rx path=rx result=0x0 eapol=1 length=120",
            "#6 kind=link-state path=link result=0x0 link_state=2 raw_code=1",
        ]
        sae_lines = [
            "#0 kind=control path=unknown result=0x0 arg0=0 arg1=0x35 arg2=0x0",
            "#1 kind=auth-policy path=hidden-assoc result=0xe00002c7 pmf=1 auth=0x0/0x1000 rsn_len=22 policy=0x1",
            "#2 kind=hidden-assoc path=hidden-assoc result=0xe00002c7 arg0=0 arg1=0x100000000000 arg2=0x16",
        ]
        for directory, expected in ((root / "psk", "wpa2-psk"),
                                    (root / "sae", "sae-reject")):
            directory.mkdir()
            fixture(directory, psk_lines if expected == "wpa2-psk" else sae_lines,
                    counts=(1, 1) if expected == "wpa2-psk" else (0, 0),
                    link_state=2 if expected == "wpa2-psk" else 1)
            assert evaluate(parse_capture(directory), expected)[0], expected
        for name, expected, kwargs in (
            ("drop", "wpa2-psk", {"dropped": 1, "counts": (1, 1), "link_state": 2}),
            ("control", "wpa2-psk", {"control": "seq=7 applied=1 mode=0x15 block=0x0\n", "counts": (1, 1), "link_state": 2}),
            ("contamination", "sae-reject", {"counts": (1, 0)}),
            ("eapol", "wpa2-psk", {"counts": (1, 1), "link_state": 2}),
            ("link", "wpa2-psk", {"counts": (1, 1), "link_state": 1}),
        ):
            directory = root / name
            directory.mkdir()
            lines = list(sae_lines if expected == "sae-reject" else psk_lines)
            if name == "contamination":
                lines.append("#3 kind=pmk-ingress path=pmk result=0x0 source=cipher-key auth=0x1000 key_len=32 decision=accepted")
            if name == "eapol":
                lines[4] = "#4 kind=tx path=tx result=0xe00002c0 eapol=1 length=120"
            if name == "link":
                lines[-1] = "#6 kind=link-state path=link result=0x0 link_state=1 raw_code=1"
            fixture(directory, lines, **kwargs)
            assert not evaluate(parse_capture(directory), expected)[0], name
    print("PASS: SAE/PMK capture evaluator fixture matrix")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture_dir", nargs="?", type=Path)
    parser.add_argument("--expect", choices=("auto", "wpa2-psk", "sae-reject"),
                        default="auto")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if args.capture_dir is None:
        parser.error("capture_dir is required unless --self-test is used")
    capture = parse_capture(args.capture_dir)
    passed, reasons = evaluate(capture, args.expect)
    sys.stdout.write(render(capture, args.expect, passed, reasons))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
