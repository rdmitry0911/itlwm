#!/usr/bin/env bash
# Verify the one-shot Tahoe boot thread-call drain contract without relying on
# thread_call_cancel_wait, which the 25C56 AuxKC linker does not admit.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


root = Path(sys.argv[1])
cpp = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
hpp = (root / "AirportItlwm/AirportItlwmV2.hpp").read_text()
build_script = (root / "scripts/build_tahoe.sh").read_text()


def fail(message: str) -> None:
    raise SystemExit(f"Tahoe boot thread-call AuxKC contract: {message}")


def method(name: str, result: str = "void", params: str = "()") -> str:
    marker = f"{result} AirportItlwm::{name}{params}"
    start = cpp.find(marker)
    if start < 0:
        fail(f"missing {marker}")
    opening = cpp.find("{", start)
    depth = 0
    for pos in range(opening, len(cpp)):
        if cpp[pos] == "{":
            depth += 1
        elif cpp[pos] == "}":
            depth -= 1
            if depth == 0:
                return cpp[opening + 1:pos]
    fail(f"unterminated {marker}")


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        fail(f"missing {label}: {needle}")


def forbid(text: str, needle: str, label: str) -> None:
    if needle in text:
        fail(f"unexpected {label}: {needle}")


def ordered(text: str, label: str, *needles: str) -> None:
    cursor = 0
    for needle in needles:
        found = text.find(needle, cursor)
        if found < 0:
            fail(f"{label} missing ordered token: {needle}")
        cursor = found + len(needle)


for needle in (
    "bool fTahoeBootStopping;",
    "bool fTahoeBootScheduled;",
    "bool fTahoeBootCallActive;",
    "bool fTahoeBootCallRetained;",
    "thread_t fTahoeBootCallOwner;",
    "bool beginTahoeBootThreadCall();",
    "void completeTahoeBootThreadCall();",
    "void releaseTahoeBootThreadCallRetain();",
):
    require(hpp, needle, "one-shot callback state")

schedule = method("scheduleTahoeBootThreadCall")
begin = method("beginTahoeBootThreadCall", "bool")
live = method("tahoeBootThreadCallLive", "bool")
complete = method("completeTahoeBootThreadCall")
release_retain = method("releaseTahoeBootThreadCallRetain")
drain = method("stopTahoeBootThreadCallAndDrain")
init = method("init", "bool", "(OSDictionary *properties)")
free = method("free")

ordered(schedule, "one-shot schedule",
        "IOLockLock(fTahoeBootCallLock);",
        "!fTahoeBootStopping && !fTahoeBootScheduled &&",
        "!fTahoeBootCallActive", "fTahoeBootScheduled = true;",
        "fTahoeBootCallActive = true;", "fTahoeBootCallRetained = true;",
        "retain();", "thread_call_enter(tahoeBootThreadCall);")
ordered(begin, "callback owner admission",
        "IOLockLock(lock);",
        "if (fTahoeBootCallActive && tahoeBootThreadCall != nullptr)",
        "fTahoeBootCallOwner = current_thread();",
        "const bool live = !fTahoeBootStopping && fTahoeBootCallActive &&",
        "IOLockUnlock(lock);")
ordered(live, "callback liveness",
        "!fTahoeBootStopping && fTahoeBootCallActive &&",
        "tahoeBootThreadCall != nullptr")
ordered(complete, "callback completion",
        "IOLockLock(lock);", "fTahoeBootCallOwner = nullptr;",
        "fTahoeBootCallActive = false;",
        "IOLockWakeup(lock, &fTahoeBootCallActive, false);",
        "IOLockUnlock(lock);")
ordered(release_retain, "schedule-retain release",
        "IOLockLock(lock);", "fTahoeBootCallRetained = false;",
        "IOLockUnlock(lock);", "if (dropRetain)", "release();")
ordered(drain, "manual public-cancel drain",
        "if (fTahoeBootStopping)",
        "if (fTahoeBootCallOwner == current_thread())",
        "Tahoe boot callback attempted self-drain",
        "fTahoeBootStopping = true;",
        "const bool canceled = thread_call_cancel(call);",
        "if (canceled && tahoeBootThreadCall == call)",
        "fTahoeBootCallActive = false;",
        "while (tahoeBootThreadCall == call && fTahoeBootCallActive)",
        "IOLockSleep(lock, &fTahoeBootCallActive, THREAD_UNINT);",
        "thread_call_free(call)", "tahoeBootThreadCall = nullptr;")
ordered(drain, "cancelled-call terminal retain release",
        "tahoeBootThreadCall = nullptr;", "if (canceled)",
        "releaseTahoeBootThreadCallRetain();")
ordered(init, "boot callback state initialization",
        "fTahoeBootStopping = false;", "fTahoeBootScheduled = false;",
        "fTahoeBootCallActive = false;", "fTahoeBootCallRetained = false;",
        "fTahoeBootCallOwner = nullptr;")
require(free, "tahoeBootThreadCall == nullptr && !fTahoeBootCallActive &&",
        "free-time callback drain predicate")
require(free, "!fTahoeBootCallRetained && fTahoeBootCallOwner == nullptr",
        "free-time retained/owner predicate")
forbid(cpp, "thread_call_cancel_wait", "private cancel-wait dependency")
require(build_script, "nm -u \"$OUTPUT_BINARY\" | grep -qx '_thread_call_cancel_wait'",
        "Mach-O private-symbol rejection")
if cpp.count("thread_call_enter(tahoeBootThreadCall)") != 1:
    fail("only the locked one-shot scheduler may submit the boot thread call")

handler_start = cpp.find("handleTahoeBootChipImage(thread_call_param_t param0")
if handler_start < 0:
    fail("missing boot callback handler")
handler_end = cpp.find("\n}\n\nvoid AirportItlwm::performTahoeBootChipImage", handler_start)
if handler_end < 0:
    fail("cannot delimit boot callback handler")
handler = cpp[handler_start:handler_end]
ordered(handler, "unconditional callback completion",
        "if (self == nullptr)", "if (self->beginTahoeBootThreadCall())",
        "gate->runAction(performTahoeBootChipImageGated);",
        "self->completeTahoeBootThreadCall();",
        "self->releaseTahoeBootThreadCallRetain();")
if handler.count("self->completeTahoeBootThreadCall();") != 1:
    fail("boot callback must have exactly one completion edge")
if handler.count("self->releaseTahoeBootThreadCallRetain();") != 1:
    fail("boot callback must have exactly one terminal schedule-retain release")
if not handler.rstrip().endswith("self->releaseTahoeBootThreadCallRetain();"):
    fail("boot callback uses controller state after its terminal schedule-retain release")


class OneShotModel:
    """Small model of the explicit lock-protected lifetime state.

    Keep this model valid for the Tahoe guest's Python 3.9 runtime: do not use
    PEP 604 (`T | None`) annotations here.
    """

    def __init__(self) -> None:
        self.stopping = False
        self.scheduled = False
        self.active = False
        self.retained = False
        self.owner = None
        self.freed = False

    def schedule(self) -> bool:
        if self.stopping or self.scheduled or self.active:
            return False
        self.scheduled = True
        self.active = True
        self.retained = True
        return True

    def callback_start(self, owner: str) -> bool:
        if not self.active:
            return False
        self.owner = owner
        return not self.stopping

    def callback_finish(self) -> None:
        if self.owner is None:
            raise AssertionError("completion without callback")
        self.owner = None
        self.active = False

    def callback_release(self) -> None:
        if not self.retained:
            raise AssertionError("callback released no schedule retain")
        self.retained = False

    def stop(self, cancel_succeeds: bool, caller: str):
        if self.stopping:
            return "already-claimed"
        if self.owner == caller:
            raise AssertionError("callback attempted self-drain")
        self.stopping = True
        if cancel_succeeds:
            if not self.active or self.owner is not None:
                raise AssertionError("cancel succeeded after callback start")
            self.active = False
        return not self.active

    def cancelled_release(self) -> None:
        if not self.retained:
            raise AssertionError("cancelled call lost schedule retain")
        self.retained = False

    def free(self) -> None:
        if self.active or self.retained or self.owner is not None:
            raise AssertionError("free before callback lifetime is drained")
        self.freed = True


# Pending cancellation: no callback enters and free is immediately safe.
pending = OneShotModel()
assert pending.schedule()
assert pending.stop(cancel_succeeds=True, caller="stop")
pending.cancelled_release()
pending.free()
assert pending.freed

# A started callback survives a failed cancellation until its unconditional
# completion edge; stop may not free the call during that interval.
running = OneShotModel()
assert running.schedule()
assert running.callback_start("callback")
assert not running.stop(cancel_succeeds=False, caller="stop")
try:
    running.free()
except AssertionError:
    pass
else:
    raise AssertionError("running callback permitted premature free")
running.callback_finish()
running.callback_release()
running.free()

# A normal completion clears only active; the scheduled latch still prevents a
# second boot submission during this controller lifetime.
completed = OneShotModel()
assert completed.schedule()
assert completed.callback_start("callback")
completed.callback_finish()
completed.callback_release()
assert not completed.schedule()
assert completed.stop(cancel_succeeds=False, caller="stop")
completed.free()

# A self-drain cannot sleep on its own active latch, and a second lifecycle
# caller cannot free a call already claimed by the first drain owner.
self_drain = OneShotModel()
assert self_drain.schedule()
assert self_drain.callback_start("callback")
try:
    self_drain.stop(cancel_succeeds=False, caller="callback")
except AssertionError:
    pass
else:
    raise AssertionError("callback self-drain was not rejected")
self_drain.callback_finish()
self_drain.callback_release()

double_drain = OneShotModel()
assert double_drain.schedule()
assert not double_drain.stop(cancel_succeeds=False, caller="first-stop")
assert double_drain.stop(cancel_succeeds=False, caller="second-stop") == "already-claimed"
assert double_drain.callback_start("callback") is False
# The cancel-false dispatch gap still reaches callback completion; model it
# explicitly without allowing a second lifecycle owner to free the call.
double_drain.owner = "callback"
double_drain.callback_finish()
double_drain.callback_release()
double_drain.free()

# Stop before the nub submits a call leaves no active object to drain.
never_submitted = OneShotModel()
assert never_submitted.stop(cancel_succeeds=False, caller="stop")
assert not never_submitted.schedule()
never_submitted.free()

print("PASS: Tahoe boot thread-call AuxKC compatibility contract")
PY
