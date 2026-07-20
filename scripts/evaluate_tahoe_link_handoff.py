#!/usr/bin/env python3
"""Classify Tahoe link-handoff evidence without exposing network identifiers.

This is a structural diagnostic companion to the SAE/PMK capture evaluator.
It reads only the redaction-safe link/status fields and trace kinds emitted by
RegDiag.  It never prints an SSID, BSSID, pointer, packet body, or key data.
The result is a diagnosis label, not an association, authentication, or
traffic-success verdict.
"""

from __future__ import annotations

import argparse
import re
import tempfile
from dataclasses import dataclass
from pathlib import Path


LINK_ACTIVE = 0x2


@dataclass(frozen=True)
class Snapshot:
    ic_state: int | None
    current_status: int | None


@dataclass(frozen=True)
class LinkStatusEvent:
    decision: str
    previous: int
    requested: int
    result: int


@dataclass(frozen=True)
class LinkPublishEvent:
    decision: str
    link_state: int
    raw_code: int
    result: int


@dataclass(frozen=True)
class JoinAbortEvent:
    phase: str
    ic_state: int
    request_completion: int
    result: int


def read(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except FileNotFoundError:
        return ""


def parse_snapshot(text: str) -> Snapshot:
    match = re.search(
        r"^state\s+ic=(\d+)\s+ic_flags=0x[0-9a-fA-F]+\s+"
        r"if_flags=0x[0-9a-fA-F]+\s+power=\d+\s+pm=\d+\s+"
        r"link=0x([0-9a-fA-F]+)",
        text,
        re.MULTILINE,
    )
    if not match:
        return Snapshot(None, None)
    return Snapshot(int(match.group(1)), int(match.group(2), 16))


def parse_result(line: str) -> int:
    match = re.search(r"\bresult=0x([0-9a-fA-F]+)", line)
    return int(match.group(1), 16) if match else -1


def parse_trace(text: str) -> tuple[list[LinkStatusEvent],
                                    list[LinkPublishEvent],
                                    list[JoinAbortEvent]]:
    statuses: list[LinkStatusEvent] = []
    publishes: list[LinkPublishEvent] = []
    aborts: list[JoinAbortEvent] = []
    for line in text.splitlines():
        if "kind=link-status" in line:
            match = re.search(
                r"\bdecision=([a-z-]+)\s+previous=0x([0-9a-fA-F]+)\s+"
                r"requested=0x([0-9a-fA-F]+)",
                line,
            )
            if match:
                decision, previous, requested = match.groups()
                statuses.append(LinkStatusEvent(
                    decision, int(previous, 16), int(requested, 16),
                    parse_result(line)))
        elif "kind=link-publish" in line:
            match = re.search(
                r"\bdecision=([a-z-]+)\s+link_state=(\d+)\s+"
                r"raw_code=(\d+)",
                line,
            )
            if match:
                decision, link_state, raw_code = match.groups()
                publishes.append(LinkPublishEvent(
                    decision, int(link_state), int(raw_code),
                    parse_result(line)))
        elif "kind=join-abort" in line:
            match = re.search(
                r"\bphase=([a-z-]+)\s+ic_state=(\d+)\s+"
                r"request_completion=(\d+)",
                line,
            )
            if match:
                phase, ic_state, request_completion = match.groups()
                aborts.append(JoinAbortEvent(
                    phase, int(ic_state), int(request_completion),
                    parse_result(line)))
    return statuses, publishes, aborts


def classify(pre: Snapshot, post: Snapshot,
             statuses: list[LinkStatusEvent],
             publishes: list[LinkPublishEvent],
             aborts: list[JoinAbortEvent]) -> tuple[str, list[str]]:
    if pre.ic_state is None or pre.current_status is None:
        return "DIAGNOSTIC_INCOMPLETE", [
            "pre-trigger snapshot lacks the redaction-safe controller link state"
        ]
    if not statuses:
        return "DIAGNOSTIC_INCOMPLETE", [
            "candidate did not emit link-status timeline records"
        ]

    active_same = any(
        event.decision == "same" and
        (event.previous & LINK_ACTIVE) != 0 and
        event.previous == event.requested
        for event in statuses
    )
    active_applied = any(
        event.decision == "applied" and
        (event.requested & LINK_ACTIVE) != 0 and event.result == 0
        for event in statuses
    )
    queued = any(event.decision == "queued" and event.result == 0
                 for event in publishes)
    published = any(event.decision == "published" and event.result == 0
                    for event in publishes)
    abort_enter = any(event.phase == "enter" for event in aborts)

    if ((pre.current_status & LINK_ACTIVE) != 0 and active_same and
            not published and abort_enter):
        return "PREMATURE_ACTIVE_SHORT_CIRCUIT", [
            "pre-trigger controller status was already active",
            "real active link request was short-circuited as unchanged",
            "no successful off-gate WCL link publication preceded join-abort",
        ]
    if active_applied and queued and published:
        return "LINK_PUBLICATION_PROGRESS", [
            "real active link status reached the controller",
            "off-gate link publication was queued and accepted",
            "this is not an association, EAPOL, authentication, or traffic success claim",
        ]

    reasons: list[str] = []
    if (pre.current_status & LINK_ACTIVE) != 0:
        reasons.append("pre-trigger controller status was already active")
    if active_same:
        reasons.append("an active link request was short-circuited as unchanged")
    if active_applied and not queued:
        reasons.append("active link status applied but no off-gate publication was queued")
    if queued and not published:
        reasons.append("off-gate link publication was queued but not accepted")
    if abort_enter:
        reasons.append("WCL join-abort reached the driver")
    if not reasons:
        reasons.append("link handoff did not match a classified structural path")
    return "LINK_PUBLICATION_INCOMPLETE", reasons


def evaluate(directory: Path) -> tuple[str, list[str], Snapshot, Snapshot,
                                        list[LinkStatusEvent],
                                        list[LinkPublishEvent],
                                        list[JoinAbortEvent]]:
    pre = parse_snapshot(read(directory / "pre-snapshot.txt"))
    post = parse_snapshot(read(directory / "post-snapshot.txt"))
    statuses, publishes, aborts = parse_trace(read(directory / "trace.txt"))
    verdict, reasons = classify(pre, post, statuses, publishes, aborts)
    return verdict, reasons, pre, post, statuses, publishes, aborts


def render(directory: Path) -> str:
    verdict, reasons, pre, post, statuses, publishes, aborts = evaluate(directory)
    lines = [
        "Tahoe link-handoff diagnostic",
        "pre: ic_state=%s current_status=%s" % (
            pre.ic_state if pre.ic_state is not None else "missing",
            "0x%x" % pre.current_status if pre.current_status is not None else "missing",
        ),
        "post: ic_state=%s current_status=%s" % (
            post.ic_state if post.ic_state is not None else "missing",
            "0x%x" % post.current_status if post.current_status is not None else "missing",
        ),
        "stages: link_status=%d link_publish=%d join_abort=%d" % (
            len(statuses), len(publishes), len(aborts)),
        "verdict=%s" % verdict,
    ]
    lines.extend("reason=%s" % reason for reason in reasons)
    return "\n".join(lines) + "\n"


def fixture(directory: Path, pre_status: int, trace: list[str]) -> None:
    snapshot = (
        "version=2 size=448 seq=1 control_seq=1\n"
        "state ic=1 ic_flags=0x1 if_flags=0x1 power=1 pm=1 "
        "link=0x%x speed=0\n" % pre_status
    )
    directory.joinpath("pre-snapshot.txt").write_text(snapshot)
    directory.joinpath("post-snapshot.txt").write_text(
        "version=2 size=448 seq=2 control_seq=1\n"
        "state ic=4 ic_flags=0x1 if_flags=0x1 power=1 pm=1 link=0x%x speed=0\n" %
        pre_status
    )
    directory.joinpath("trace.txt").write_text(
        "version=2 count=%d next=%d dropped=0\n%s\n" %
        (len(trace), len(trace), "\n".join(trace))
    )


def self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="aiam-link-handoff-") as temp:
        root = Path(temp)
        early = root / "early"
        early.mkdir()
        fixture(early, 0x3, [
            "#0 kind=link-status path=link result=0x0 decision=same previous=0x3 requested=0x3",
            "#1 kind=join-abort path=lifecycle result=0x0 phase=enter ic_state=4 request_completion=1",
        ])
        assert evaluate(early)[0] == "PREMATURE_ACTIVE_SHORT_CIRCUIT"

        progress = root / "progress"
        progress.mkdir()
        fixture(progress, 0x1, [
            "#0 kind=link-status path=link result=0x0 decision=applied previous=0x1 requested=0x3",
            "#1 kind=link-publish path=link result=0x0 decision=queued link_state=2 raw_code=0",
            "#2 kind=link-publish path=link result=0x0 decision=published link_state=2 raw_code=1",
        ])
        assert evaluate(progress)[0] == "LINK_PUBLICATION_PROGRESS"

        incomplete = root / "incomplete"
        incomplete.mkdir()
        fixture(incomplete, 0x1, [])
        assert evaluate(incomplete)[0] == "DIAGNOSTIC_INCOMPLETE"
    print("PASS: Tahoe link-handoff diagnostic fixture matrix")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture_dir", nargs="?", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if args.capture_dir is None:
        parser.error("capture_dir is required unless --self-test is used")
    print(render(args.capture_dir), end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
