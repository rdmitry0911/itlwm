#!/usr/bin/env python3
"""Summarize Tahoe's passive, redaction-safe link owner-context census.

Input is only RegDiag's decoded ``kind=link-context`` trace lines.  The
evaluator never reads or prints SSIDs, BSSIDs, pointers, packets, or keys.
Its result describes execution ownership only; it is not an association,
authentication, EAPOL, DHCP, ping, or traffic verdict.
"""

from __future__ import annotations

import argparse
import re
import tempfile
from dataclasses import dataclass
from pathlib import Path


MAIN_CHAIN = (
    ("net80211-bridge", "enter"),
    ("controller-status", "base-applied"),
    ("publish-queue", "source-ready"),
    ("publish-action", "action-ready"),
    ("link-gate", "gate-ready"),
)

# The bridge observes BSD LINK_STATE_* while the lower Tahoe stages observe
# IO80211LinkState.  These route-local encodings deliberately differ.
BSD_LINK_STATE_DOWN = 2
BSD_LINK_STATE_UP = 4
IO80211_LINK_STATE_DOWN = 1
IO80211_LINK_STATE_UP = 2


@dataclass(frozen=True)
class ContextEvent:
    sequence: int
    route: str
    stage: str
    epoch: int
    link_state: int
    raw_code: int
    controller_status: str
    lifecycle: str
    on_thread: str
    in_gate: str
    on_dispatch: str
    result: int


def read(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except FileNotFoundError:
        return ""


def trace_dropped(text: str) -> int | None:
    match = re.search(r"^version=\d+\s+count=\d+\s+next=\d+\s+"
                      r"dropped=(\d+)$", text, re.MULTILINE)
    return int(match.group(1)) if match is not None else None


def parse_trace(text: str) -> list[ContextEvent]:
    events: list[ContextEvent] = []
    pattern = re.compile(
        r"^#(?P<sequence>\d+)\s+kind=link-context\s+path=link\s+"
        r"result=0x(?P<result>[0-9a-fA-F]+)\s+"
        r"route=(?P<route>[a-z0-9-]+)\s+stage=(?P<stage>[a-z0-9-]+)\s+"
        r"epoch=(?P<epoch>\d+)\s+link_state=(?P<link_state>\d+)\s+"
        r"raw_code=(?P<raw_code>\d+)\s+"
        r"controller_status=(?P<controller_status>n/a|0x[0-9a-fA-F]+)\s+"
        r"lifecycle=(?P<lifecycle>[a-z0-9-]+)\s+"
        r"on_thread=(?P<on_thread>yes|no|unknown)\s+"
        r"in_gate=(?P<in_gate>yes|no|unknown)\s+"
        r"on_dispatch=(?P<on_dispatch>yes|no|unknown)$"
    )
    for line in text.splitlines():
        match = pattern.match(line)
        if not match:
            continue
        fields = match.groupdict()
        events.append(ContextEvent(
            sequence=int(fields["sequence"]),
            route=fields["route"],
            stage=fields["stage"],
            epoch=int(fields["epoch"]),
            link_state=int(fields["link_state"]),
            raw_code=int(fields["raw_code"]),
            controller_status=fields["controller_status"],
            lifecycle=fields["lifecycle"],
            on_thread=fields["on_thread"],
            in_gate=fields["in_gate"],
            on_dispatch=fields["on_dispatch"],
            result=int(fields["result"], 16),
        ))
    return events


def gate_terminal(events: list[ContextEvent]) -> ContextEvent | None:
    terminals = [event for event in events if event.route == "link-gate" and
                 event.stage in {"gate-ready", "gate-rejected"}]
    return max(terminals, key=lambda event: event.sequence, default=None)


def chain_before_terminal(events: list[ContextEvent],
                          terminal: ContextEvent) -> list[ContextEvent] | None:
    """Find one ordered mandatory chain ending at this exact gate event.

    The association epoch is the correlation key.  Epoch zero is intentionally
    not eligible: it means this passive point could not safely sample the
    net80211 epoch, so a complete execution-context claim would be dishonest.
    """
    if terminal.epoch == 0 or terminal.stage != "gate-ready":
        return None
    same_epoch = sorted((event for event in events
                         if event.epoch == terminal.epoch and
                         event.sequence < terminal.sequence),
                        key=lambda event: event.sequence)

    def find_prefix(index: int, after: int,
                    chosen: list[ContextEvent]) -> list[ContextEvent] | None:
        if index == len(MAIN_CHAIN) - 1:
            return chosen + [terminal]
        route, stage = MAIN_CHAIN[index]
        for event in same_epoch:
            if event.sequence <= after or event.route != route or event.stage != stage:
                continue
            found = find_prefix(index + 1, event.sequence, chosen + [event])
            if found is not None:
                return found
        return None

    chain = find_prefix(0, -1, [])
    if chain is None:
        return None

    bridge, _controller, queue, action, gate = chain
    canonical_bridge_state = {
        BSD_LINK_STATE_DOWN: IO80211_LINK_STATE_DOWN,
        BSD_LINK_STATE_UP: IO80211_LINK_STATE_UP,
    }.get(bridge.link_state)
    if canonical_bridge_state is None:
        return None
    if queue.link_state != canonical_bridge_state or \
            action.link_state != canonical_bridge_state or \
            gate.link_state != canonical_bridge_state:
        return None
    return chain


def complete_chain(events: list[ContextEvent]) -> list[ContextEvent] | None:
    terminal = gate_terminal(events)
    return chain_before_terminal(events, terminal) if terminal is not None else None


def main_chain_complete(events: list[ContextEvent]) -> bool:
    return complete_chain(events) is not None


def verdict(events: list[ContextEvent], dropped: int | None = 0) -> str:
    if dropped is None:
        return "OWNER_CONTEXT_CENSUS_INCOMPLETE"
    if dropped != 0:
        return "OWNER_CONTEXT_CENSUS_TRACE_TRUNCATED"
    if not events:
        return "OWNER_CONTEXT_CENSUS_INCOMPLETE"
    terminal = gate_terminal(events)
    if terminal is not None and terminal.stage == "gate-rejected" and \
            terminal.on_thread == "yes" and terminal.in_gate == "yes":
        return "OWNER_CONTEXT_GATE_HELD"
    chain = complete_chain(events)
    if chain is not None and all(event.on_thread == "yes" and
                                 event.in_gate == "no"
                                 for event in chain[-2:]):
        return "OWNER_CONTEXT_MAIN_CHAIN_SAFE"
    if terminal is not None and terminal.on_thread == "yes" and \
            terminal.in_gate == "yes":
        return "OWNER_CONTEXT_GATE_HELD"
    return "OWNER_CONTEXT_PARTIAL"


def render(events: list[ContextEvent], dropped: int | None = 0) -> str:
    route_counts = {route: sum(event.route == route for event in events)
                    for route in tuple(route for route, _ in MAIN_CHAIN) +
                    ("skywalk-parent", "wcl-update")}
    owner_off_gate = sum(event.on_thread == "yes" and event.in_gate == "no"
                         for event in events)
    owner_gate_held = sum(event.on_thread == "yes" and event.in_gate == "yes"
                          for event in events)
    non_owner = sum(event.on_thread == "no" for event in events)
    dispatch_owner = sum(event.on_dispatch == "yes" for event in events)
    epoch_values = sorted({event.epoch for event in events if event.epoch != 0})
    chain = complete_chain(events)
    lines = [
        "Tahoe passive link owner-context census",
        "events=%d epochs=%s trace_dropped=%s" % (
            len(events), ",".join(str(value) for value in epoch_values) or "none",
            dropped if dropped is not None else "missing"),
        "routes: " + " ".join("%s=%d" % (route, route_counts[route])
                              for route in route_counts),
        "owners: off_gate=%d gate_held=%d non_owner=%d dispatch_owner=%d" % (
            owner_off_gate, owner_gate_held, non_owner, dispatch_owner),
        "main_chain_complete=%s matched_epoch=%s" % (
            "yes" if chain is not None and dropped == 0 else "no",
            chain[-1].epoch if chain is not None else "none"),
        "verdict=%s" % verdict(events, dropped),
        "scope=execution-context only; no functional network claim",
    ]
    return "\n".join(lines) + "\n"


def fixture(directory: Path, lines: list[str], *, dropped: int = 0) -> None:
    directory.joinpath("trace.txt").write_text(
        "version=2 count=%d next=%d dropped=%d\n%s\n" %
        (len(lines), len(lines), dropped, "\n".join(lines)),
        encoding="utf-8",
    )


def line(sequence: int, route: str, stage: str, *, epoch: int = 41,
         link_state: int = 2, on_thread: str = "unknown", in_gate: str = "unknown",
         on_dispatch: str = "unknown") -> str:
    return (
        "#%d kind=link-context path=link result=0x0 route=%s stage=%s "
        "epoch=%d link_state=%d raw_code=1 controller_status=n/a "
        "lifecycle=publication-ready on_thread=%s in_gate=%s on_dispatch=%s"
        % (sequence, route, stage, epoch, link_state, on_thread, in_gate,
           on_dispatch)
    )


def self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="aiam-link-context-") as temp:
        root = Path(temp)
        full = root / "full"
        full.mkdir()
        fixture(full, [
            line(0, "net80211-bridge", "enter", link_state=4,
                 on_thread="no", in_gate="no"),
            line(1, "controller-status", "base-applied", on_thread="no", in_gate="no"),
            line(2, "publish-queue", "source-ready", on_thread="no", in_gate="no"),
            line(3, "publish-action", "action-ready", on_thread="yes", in_gate="no"),
            line(4, "link-gate", "gate-ready", on_thread="yes", in_gate="no"),
            line(5, "skywalk-parent", "parent-accepted", on_thread="yes", in_gate="no", on_dispatch="yes"),
            line(6, "wcl-update", "wcl-return", on_thread="yes", in_gate="yes", on_dispatch="yes"),
        ])
        full_events = parse_trace(read(full / "trace.txt"))
        assert verdict(full_events) == "OWNER_CONTEXT_MAIN_CHAIN_SAFE"
        assert "off_gate=3" in render(full_events)
        assert "scope=execution-context only" in render(full_events)

        held = root / "held"
        held.mkdir()
        fixture(held, [
            line(0, "link-gate", "gate-rejected", on_thread="yes", in_gate="yes"),
        ])
        assert verdict(parse_trace(read(held / "trace.txt"))) == "OWNER_CONTEXT_GATE_HELD"

        split = root / "split"
        split.mkdir()
        fixture(split, [
            line(0, "net80211-bridge", "enter", epoch=41,
                 on_thread="no", in_gate="no"),
            line(1, "controller-status", "base-applied", epoch=42,
                 on_thread="no", in_gate="no"),
            line(2, "publish-queue", "source-ready", epoch=42,
                 on_thread="no", in_gate="no"),
            line(3, "publish-action", "action-ready", epoch=42,
                 on_thread="yes", in_gate="no"),
            line(4, "link-gate", "gate-ready", epoch=42,
                 on_thread="yes", in_gate="no"),
        ])
        assert verdict(parse_trace(read(split / "trace.txt"))) == "OWNER_CONTEXT_PARTIAL"

        rejected = root / "rejected"
        rejected.mkdir()
        fixture(rejected, [
            line(0, "net80211-bridge", "enter", on_thread="no", in_gate="no"),
            line(1, "controller-status", "base-applied", on_thread="no", in_gate="no"),
            line(2, "publish-queue", "source-ready", on_thread="no", in_gate="no"),
            line(3, "publish-action", "action-ready", on_thread="yes", in_gate="no"),
            line(4, "link-gate", "gate-rejected", on_thread="yes", in_gate="yes"),
        ])
        assert verdict(parse_trace(read(rejected / "trace.txt"))) == "OWNER_CONTEXT_GATE_HELD"

        direction_mismatch = root / "direction-mismatch"
        direction_mismatch.mkdir()
        fixture(direction_mismatch, [
            line(0, "net80211-bridge", "enter", link_state=BSD_LINK_STATE_UP,
                 on_thread="no", in_gate="no"),
            line(1, "controller-status", "base-applied", on_thread="no", in_gate="no"),
            line(2, "publish-queue", "source-ready",
                 link_state=IO80211_LINK_STATE_DOWN, on_thread="no", in_gate="no"),
            line(3, "publish-action", "action-ready",
                 link_state=IO80211_LINK_STATE_DOWN, on_thread="yes", in_gate="no"),
            line(4, "link-gate", "gate-ready",
                 link_state=IO80211_LINK_STATE_DOWN, on_thread="yes", in_gate="no"),
        ])
        assert verdict(parse_trace(read(direction_mismatch / "trace.txt"))) == \
            "OWNER_CONTEXT_PARTIAL"

        truncated = root / "truncated"
        truncated.mkdir()
        fixture(truncated, [
            line(0, "link-gate", "gate-ready", on_thread="yes", in_gate="no"),
        ], dropped=1)
        truncated_text = read(truncated / "trace.txt")
        assert verdict(parse_trace(truncated_text), trace_dropped(truncated_text)) == \
            "OWNER_CONTEXT_CENSUS_TRACE_TRUNCATED"

        missing = root / "missing"
        missing.mkdir()
        fixture(missing, [])
        assert verdict(parse_trace(read(missing / "trace.txt"))) == "OWNER_CONTEXT_CENSUS_INCOMPLETE"
    print("PASS: Tahoe link owner-context census fixture matrix")
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
