#!/usr/bin/env python3
"""Classify Tahoe parent-link acceptance without exposing network identifiers.

The evaluator consumes only RegDiag's decoded ``kind=link-state`` records.
It reports whether a Tahoe link-up edge was accepted by the inherited parent;
it is not an association, authentication, EAPOL, DHCP, route, ping, or
traffic-success verdict.
"""

from __future__ import annotations

import argparse
import re
import tempfile
from dataclasses import dataclass
from pathlib import Path


LINK_STATE_UP = 2


@dataclass(frozen=True)
class ParentEvent:
    sequence: int
    result: int
    link_state: int
    raw_code: int
    parent_accepted: str


def read(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except FileNotFoundError:
        return ""


def trace_dropped(text: str) -> int | None:
    match = re.search(r"^version=\d+\s+count=\d+\s+next=\d+\s+"
                      r"dropped=(\d+)$", text, re.MULTILINE)
    return int(match.group(1)) if match is not None else None


def parse_trace(text: str) -> list[ParentEvent]:
    events: list[ParentEvent] = []
    pattern = re.compile(
        r"^#(?P<sequence>\d+)\s+kind=link-state\s+path=link\s+"
        r"result=0x(?P<result>[0-9a-fA-F]+)\s+"
        r"link_state=(?P<link_state>\d+)\s+raw_code=(?P<raw_code>\d+)\s+"
        r"parent_accepted=(?P<parent_accepted>1|0|n/a)$"
    )
    for line in text.splitlines():
        match = pattern.match(line)
        if match is None:
            continue
        fields = match.groupdict()
        events.append(ParentEvent(
            sequence=int(fields["sequence"]),
            result=int(fields["result"], 16),
            link_state=int(fields["link_state"]),
            raw_code=int(fields["raw_code"]),
            parent_accepted=fields["parent_accepted"],
        ))
    return events


def verdict(events: list[ParentEvent], dropped: int | None = 0) -> str:
    if dropped is None:
        return "TAHOE_PARENT_LINK_UP_CAPTURE_INCOMPLETE"
    if dropped != 0:
        return "TAHOE_PARENT_LINK_UP_TRACE_TRUNCATED"
    link_up = [event for event in events if event.link_state == LINK_STATE_UP]
    if not link_up:
        return "TAHOE_PARENT_LINK_UP_NOT_OBSERVED"
    latest = max(link_up, key=lambda event: event.sequence)
    if latest.parent_accepted == "1" and latest.result == 0:
        return "TAHOE_PARENT_LINK_UP_ACCEPTED"
    if latest.parent_accepted == "0":
        return "TAHOE_PARENT_LINK_UP_REJECTED"
    return "TAHOE_PARENT_LINK_UP_UNATTESTED"


def render(events: list[ParentEvent], dropped: int | None = 0) -> str:
    link_up = [event for event in events if event.link_state == LINK_STATE_UP]
    accepted = sum(event.parent_accepted == "1" and event.result == 0
                   for event in link_up)
    rejected = sum(event.parent_accepted == "0" for event in link_up)
    unattested = sum(event.parent_accepted == "n/a" for event in link_up)
    return "\n".join((
        "Tahoe parent link-state attestation",
        "link_up_events=%d accepted=%d rejected=%d unattested=%d trace_dropped=%s" % (
            len(link_up), accepted, rejected, unattested,
            dropped if dropped is not None else "missing"),
        "verdict=%s" % verdict(events, dropped),
        "scope=parent bool only; no functional network claim",
        "",
    ))


def fixture(directory: Path, lines: list[str], *, dropped: int = 0) -> None:
    directory.joinpath("trace.txt").write_text(
        "version=2 count=%d next=%d dropped=%d\n%s\n" %
        (len(lines), len(lines), dropped, "\n".join(lines)), encoding="utf-8")


def line(sequence: int, *, result: int = 0, link_state: int = LINK_STATE_UP,
         parent_accepted: str = "1") -> str:
    return (
        "#%d kind=link-state path=link result=0x%x link_state=%d raw_code=1 "
        "parent_accepted=%s" %
        (sequence, result, link_state, parent_accepted)
    )


def self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="aiam-parent-attestation-") as temp:
        root = Path(temp)
        accepted = root / "accepted"
        accepted.mkdir()
        fixture(accepted, [line(0)])
        assert verdict(parse_trace(read(accepted / "trace.txt"))) == \
            "TAHOE_PARENT_LINK_UP_ACCEPTED"

        rejected = root / "rejected"
        rejected.mkdir()
        fixture(rejected, [line(0, result=0x2bc, parent_accepted="0")])
        assert verdict(parse_trace(read(rejected / "trace.txt"))) == \
            "TAHOE_PARENT_LINK_UP_REJECTED"

        unattested = root / "unattested"
        unattested.mkdir()
        fixture(unattested, [line(0, parent_accepted="n/a")])
        assert verdict(parse_trace(read(unattested / "trace.txt"))) == \
            "TAHOE_PARENT_LINK_UP_UNATTESTED"

        absent = root / "absent"
        absent.mkdir()
        fixture(absent, [line(0, link_state=1)])
        assert verdict(parse_trace(read(absent / "trace.txt"))) == \
            "TAHOE_PARENT_LINK_UP_NOT_OBSERVED"

        latest_rejected = root / "latest-rejected"
        latest_rejected.mkdir()
        fixture(latest_rejected, [line(0), line(1, parent_accepted="0")])
        assert verdict(parse_trace(read(latest_rejected / "trace.txt"))) == \
            "TAHOE_PARENT_LINK_UP_REJECTED"

        truncated = root / "truncated"
        truncated.mkdir()
        fixture(truncated, [line(0)], dropped=1)
        truncated_text = read(truncated / "trace.txt")
        assert verdict(parse_trace(truncated_text), trace_dropped(truncated_text)) == \
            "TAHOE_PARENT_LINK_UP_TRACE_TRUNCATED"
    print("PASS: Tahoe parent link-state attestation fixture matrix")
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
    capture = read(args.capture_dir / "trace.txt")
    print(render(parse_trace(capture), trace_dropped(capture)), end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
