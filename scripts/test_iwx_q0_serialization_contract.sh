#!/usr/bin/env bash
# Static contract for the phase-1 iwx q0 serializer.  It deliberately checks
# ordering/lifetime properties that are easy to regress in a compile-clean KEXT.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import re
import sys

root = Path(sys.argv[1])
cpp = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()
hpp = (root / "itlwm/hal_iwx/ItlIwx.hpp").read_text()
var = (root / "itlwm/hal_iwx/if_iwxvar.h").read_text()
task_cpp = (root / "itl80211/openbsd/sys/_task.cpp").read_text()
v2_cpp = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
v2_hpp = (root / "AirportItlwm/AirportItlwmV2.hpp").read_text()
legacy_cpp = (root / "AirportItlwm/AirportItlwm.cpp").read_text()
generic_cpp = (root / "itlwm/itlwm.cpp").read_text()
skywalk_cpp = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()
legacy_ioctl_cpp = (root / "AirportItlwm/AirportSTAIOCTL.cpp").read_text()
iwx_apgo_hpp = (root / "itlwm/hal_iwx/IwxApGoCapability.hpp").read_text()
iwn_cpp = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
skywalk_runtime_contract = (
    root / "docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/"
    "90_skywalk_tx_rx_runtime_contracts.yaml").read_text()
skywalk_family_contract = (
    root / "docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/"
    "88_ioskywalkfamily_internals_checked.yaml").read_text()
bsd_attach_contract = (
    root / "docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/"
    "85_bsd_attach_chain_xref_checked.yaml").read_text()
skywalk_stop_contract = (
    root / "docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/"
    "87_teardown_reporters_lifecycle_checked.yaml").read_text()


def fail(message):
    raise SystemExit(f"iwx q0 serialization contract: {message}")


def method(name):
    pattern = re.compile(
        r"\n(?:int|void|bool)\s+ItlIwx::\s*\n" + re.escape(name) +
        r"\s*\([^)]*\)\s*\{",
        re.S,
    )
    match = pattern.search(cpp)
    if not match:
        fail(f"missing ItlIwx::{name}()")
    brace = cpp.find("{", match.start(), match.end())
    depth = 0
    for pos in range(brace, len(cpp)):
        if cpp[pos] == "{":
            depth += 1
        elif cpp[pos] == "}":
            depth -= 1
            if depth == 0:
                return cpp[brace + 1:pos]
    fail(f"unterminated ItlIwx::{name}()")


def airport_definition_pattern(name):
    # Match a real out-of-line definition, rather than a prose reference such
    # as "AirportItlwm::start()" in a comment. Some definitions put the
    # method name on the line after the scope qualifier, so permit that
    # whitespace while anchoring the return-type line.
    return re.compile(
        r"(?m)^[ \t]*(?:[A-Za-z_][A-Za-z0-9_:<>, \t*&]*)[ \t]+"
        r"AirportItlwm::[ \t\r\n]*" + name +
        r"[ \t\r\n]*\([^;{}]*\)[ \t\r\n]*(?:const[ \t\r\n]*)?\{"
    )


def airport_method(name):
    match = airport_definition_pattern(re.escape(name)).search(v2_cpp)
    if not match:
        fail(f"missing AirportItlwm::{name}()")
    # Raw source contains both sides of target-version #if branches, so plain
    # brace counting can see an intentionally unmatched inactive branch.
    # The next real AirportItlwm definition is a stable, syntax-level body
    # delimiter for the order-only checks in this contract.
    next_match = airport_definition_pattern(
        r"[A-Za-z_][A-Za-z0-9_]*").search(v2_cpp, match.end())
    end = next_match.start() if next_match else len(v2_cpp)
    return v2_cpp[match.end():end]


def body_from_marker(source, marker, what):
    start = source.find(marker)
    if start < 0:
        fail(f"missing {what}")
    brace = source.find("{", start)
    if brace < 0:
        fail(f"missing body for {what}")
    depth = 0
    for pos in range(brace, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1:pos]
    fail(f"unterminated {what}")


def require(text, needle, what):
    if needle not in text:
        fail(f"missing {what}: {needle}")


def order(text, *needles):
    positions = []
    cursor = 0
    for needle in needles:
        pos = text.find(needle, cursor)
        if pos < 0:
            fail(f"missing ordered token: {needle}")
        positions.append(pos)
        cursor = pos + len(needle)
    return positions


# The Skywalk and controller translation units deliberately use distinct
# local guard types: identical global class names with different definitions
# would violate the C++ ODR in a linked KEXT.
require(v2_cpp, "class AirportItlwmControllerLifecycleOperationGuard",
        "V2-local controller lifecycle guard")
if "class AirportItlwmLifecycleOperationGuard" in v2_cpp:
    fail("V2 reintroduced the Skywalk guard's global class name")


for needle in (
    "IOSimpleLock *sc_cmdq_lock;",
    "uint32_t sc_cmdq_senders;",
    "bool sc_cmdq_stopping;",
    "bool sc_cmdq_detaching;",
    "IWX_CMD_SLOT_TIMED_OUT",
    "IWX_CMD_SLOT_ABORTED",
):
    require(var, needle, "q0 state storage")

for needle in (
    "iwx_cmdq_enter",
    "iwx_cmdq_leave",
    "iwx_cmdq_start(struct iwx_softc *, int)",
    "iwx_cmdq_start_bootstrap",
    "iwx_cmdq_stop",
    "iwx_cmdq_detach_begin",
    "iwx_cmdq_snapshot",
):
    require(hpp, needle, "q0 helper declaration")

send = method("iwx_send_cmd")
done = method("iwx_cmd_done")
stop = method("iwx_cmdq_stop")
detach_begin = method("iwx_cmdq_detach_begin")
detach = method("detach")
free = body_from_marker(cpp, "void ItlIwx::free()", "ItlIwx::free")
destroy = method("iwx_cmdq_destroy")
stop_device = method("iwx_stop_device")
rx = method("iwx_rx_pkt")
snapshot = method("iwx_cmdq_snapshot")
is_narrow = method("iwx_cmdq_is_narrow")
store_response = method("iwx_cmdq_store_response")
start_locked = body_from_marker(cpp, "static bool\niwx_cmdq_start_locked",
                                "iwx_cmdq_start_locked")
send_status = method("iwx_send_cmd_status")
async_ack_error = body_from_marker(cpp, "static int\niwx_async_cmd_ack_error",
                                   "iwx_async_cmd_ack_error")
mfp_timeout = method("iwx_mfp_pae_timeout")
mfp_release = body_from_marker(cpp,
                                "static void\niwx_mfp_pae_release_transaction",
                                "iwx_mfp_pae_release_transaction")
gate_close = method("iwx_task_gate_close")
gate_begin = method("iwx_task_gate_begin_epoch")
gate_open = method("iwx_task_gate_open")
gate_rearm = method("iwx_task_gate_rearm")
gate_drain = method("iwx_task_gate_drain")
gate_bootstrap = method("iwx_bootstrap_init_task")
cmdq_start = method("iwx_cmdq_start")
cmdq_start_bootstrap = method("iwx_cmdq_start_bootstrap")
gate_live = method("iwx_task_gate_epoch_live")
init_internal = method("iwx_init_internal")
stop_internal = method("iwx_stop_internal")
init_task = method("iwx_init_task")
attach = method("iwx_attach")

# A sender owns the q0 lifetime before it can inspect q0-derived state.
order(send, "if (!iwx_cmdq_enter(sc))", "ring = &sc->txq[IWX_DQA_CMD_QUEUE];",
      "txq_size = getTxQueueSize();")

# Payload allocation/mapping must finish before the q0 fast critical section.
first_lock = send.find("IOSimpleLockLock(sc->sc_cmdq_lock);")
if first_lock < 0:
    fail("send path has no q0 lock")
for needle in (
    "mbuf_allocpacket(MBUF_WAITOK",
    "IOMbufNaturalMemoryCursor::withSpecification",
    "getPhysicalSegmentsWithCoalesce",
):
    pos = send.find(needle)
    if pos < 0 or pos > first_lock:
        fail(f"{needle} is not strictly before q0 lock")

unlock_label = send.find("unlock:", first_lock)
if unlock_label < 0:
    fail("send path lacks fast-lock unlock label")
fast = send[first_lock:unlock_label]
for forbidden in (
    "MBUF_WAITOK",
    "mbuf_allocpacket",
    "malloc(",
    "getPhysicalSegments",
    "tsleep_nsec",
    "wakeupOn(",
    "mbuf_freem",
    "::free",
    "ieee80211_",
):
    if forbidden in fast:
        fail(f"q0 fast section contains forbidden operation {forbidden}")

# The waiter and producers share sleep-mutex -> q0 ordering.
order(send, "lockTsleep();", "IOSimpleLockLock(sc->sc_cmdq_lock);")
require(send, "tsleep_nsec_locked(desc", "predicate-locked q0 sleep")
if "tsleep_nsec(desc" in send:
    fail("unlocked q0 tsleep remains")
order(send, "IOSimpleLockUnlock(sc->sc_cmdq_lock);\n        unlockTsleep();",
      "iwx_cmdq_leave(sc);")
if send.rfind("iwx_cmdq_leave(sc);") < send.rfind("if (resp_to_free != NULL)"):
    fail("sender lifetime ref drops before local cleanup")

order(done, "lockTsleep();", "IOSimpleLockLock(sc->sc_cmdq_lock);")
order(done, "IOSimpleLockUnlock(sc->sc_cmdq_lock);",
      "wakeupOn(wchan);", "unlockTsleep();")
for forbidden in ("mbuf_freem", "::free"):
    if done.find(forbidden) < done.find("unlockTsleep();"):
        fail(f"completion frees under a q0/sleep lock: {forbidden}")
require(done, "if (expected_code != (uint32_t)code)",
        "completion opcode validation")
require(done, "IWX_CMD_SLOT_TIMED_OUT", "late-timeout completion state")

# PMF is the only tokenized async q0 owner.  Its decoder may interpret a
# failed firmware header, short reply, opcode mismatch, or ADD_STA status,
# but the legacy response-buffer policy remains unchanged for every other
# command owner.
require(done, "iwx_async_cmd_ack_error", "PMF async completion decoder")
require(done, "that->iwx_mfp_pae_q0_done(sc, &async_result);",
        "post-unlock PMF owner continuation")
for needle in (
    "slot->code != (uint32_t)code",
    "pkt->hdr.group_id & IWX_CMD_FAILED_MSK",
    "IWX_CMD_ASYNC_ACK_ADD_STA_STATUS",
    "IWX_ADD_STA_STATUS_MASK",
):
    require(async_ack_error, needle, "PMF-only ACK failure decoder")
if "IWX_CMD_FAILED_MSK" in done:
    fail("q0 completion must delegate failed-mask semantics to PMF decoder")
require(rx, "Preserve the pre-existing response-buffer policy for this",
        "legacy failed-response disposition comment")
require(rx, "!!(pkt->hdr.group_id & IWX_CMD_FAILED_MSK)",
        "legacy failed-response disposition call")
order(mfp_timeout, "txn->result.error = ETIMEDOUT;",
      "that->iwx_mfp_pae_mark_q0_timeout(sc, cookie);")
order(mfp_release, "txn->q0_inflight = false;",
      "that->iwx_mfp_pae_mark_q0_timeout(sc, q0_cookie);")
if send_status.count("*status = le32toh(resp->status);") != 1:
    fail("iwx_send_cmd_status must assign firmware status exactly once")

# Runtime q0 start is not a bare ring reset: it is admitted by the active
# init epoch while holding the fixed task-gate -> q0 order.  This closes the
# old stop-vs-init race where a stale init could reopen q0 after close().
order(cmdq_start, "IOLockLock(sc->sc_task_gate_lock);",
      "IOSimpleLockLock(sc->sc_cmdq_lock);", "iwx_cmdq_start_locked(sc);")
for needle in (
    "sc->sc_flags & IWX_FLAG_SHUTDOWN",
    "sc->sc_task_gate_detaching",
    "!sc->sc_task_gate_closed",
    "sc->sc_task_gate_init_refs != 1",
    "sc->sc_task_gate_stop_refs != 0",
    "sc->sc_generation != generation",
):
    require(cmdq_start, needle, "runtime q0 epoch admission")
order(cmdq_start, "iwx_cmdq_start_locked(sc);",
      "IOSimpleLockUnlock(sc->sc_cmdq_lock);",
      "IOLockUnlock(sc->sc_task_gate_lock);")
require(cmdq_start_bootstrap, "Attach/preinit has no active task-gate init epoch.",
        "bootstrap-only q0 start scope")
if "sc_task_gate_lock" in cmdq_start_bootstrap:
    fail("bootstrap q0 start must not pretend to own a runtime init epoch")
for needle in (
    "sc->sc_flags & IWX_FLAG_SHUTDOWN",
    "!sc->sc_task_gate_detaching",
    "sc->sc_task_gate_closed",
    "sc->sc_task_gate_init_refs == 1",
    "sc->sc_task_gate_stop_refs == 0",
    "sc->sc_generation == generation",
):
    require(gate_live, needle, "epoch-live validation")

# Stop uses the same order, never sleeps under either lock, and drains refs
# before response/ring teardown can proceed.
order(stop, "lockTsleep();", "IOSimpleLockLock(sc->sc_cmdq_lock);")
order(stop, "IOSimpleLockUnlock(sc->sc_cmdq_lock);",
      "for (i = 0; i < wake_count; i++)", "unlockTsleep();")
sleep = stop.find("IOSleep(1);")
if sleep < 0 or sleep < stop.find("unlockTsleep();"):
    fail("cmdq_stop sleeps before releasing q0/sleep mutexes")
require(stop, "senders = sc->sc_cmdq_senders;", "active-sender drain")
require(stop, "if (senders == 0)", "active-sender termination")
require(stop, "sc->sc_cmdq_slots[i].state = IWX_CMD_SLOT_ABORTED;",
        "stop terminal slot state")
require(send, "sc->sc_cmdq_slots[idx].state = IWX_CMD_SLOT_TIMED_OUT;",
        "timeout non-reuse state")

# q0 index arithmetic is constrained to an actual power-of-two descriptor ring.
for needle in (
    "count <= nitems(sc->sc_cmdq_slots)",
    "(count & (count - 1)) == 0",
    "idx = ring->cur & (ring->ring_count - 1);",
):
    require(cpp, needle, "q0 ring bound")

# q0 reset/free occurs only after stop. detach closes q0 and drains every
# producer before rings/DMA are freed, but intentionally leaves the stopped
# lock allocated until OSObject final release: an IRQ-safe sender can load the
# lock pointer before it takes q0, so detach must never free that pointer.
order(stop_device, "iwx_cmdq_stop(sc);", "iwx_disable_interrupts(sc);",
      "iwx_reset_tx_ring(sc, &sc->txq[qid]);")
order(detach, "iwx_cmdq_detach_begin(sc);", "iwx_interrupt_teardown(sc);",
      "taskq_destroy", "iwx_free_tx_ring")
if "iwx_cmdq_destroy(sc);" in detach:
    fail("detach must retain the stopped q0 lock until final free")
if free.count("iwx_cmdq_destroy(&com);") != 1:
    fail("ItlIwx::free must destroy q0 exactly once")
order(free, "iwx_cmdq_destroy(&com);", "super::free();")
for needle in (
    "KASSERT(sc->sc_cmdq_detaching",
    "KASSERT(sc->sc_cmdq_stopping",
    "KASSERT(sc->sc_cmdq_senders == 0",
):
    require(destroy, needle, "final q0 destroy precondition")
require(detach_begin, "sc->sc_cmdq_detaching = true;", "permanent detach gate")

# The retained lock makes repeat stop safe after detach has already released
# ring/DMA storage. The repeat branch must return before it even constructs a
# q0-ring pointer or wake address from that storage.
order(stop, "IOSimpleLockLock(sc->sc_cmdq_lock);",
      "if (sc->sc_cmdq_stopping)",
      "IOSimpleLockUnlock(sc->sc_cmdq_lock);", "unlockTsleep();",
      "return;", "struct iwx_tx_ring *ring")
for helper, name in (
    (snapshot, "snapshot"),
    (is_narrow, "is_narrow"),
    (store_response, "store_response"),
):
    order(helper, "!sc->sc_cmdq_stopping", "!sc->sc_cmdq_detaching",
          "iwx_cmdq_ring_valid")
order(done, "sc->sc_cmdq_stopping || sc->sc_cmdq_detaching",
      "!iwx_cmdq_ring_valid", "data = &ring->data[idx]")
order(start_locked, "sc->sc_cmdq_detaching", "!sc->sc_cmdq_stopping",
      "iwx_cmdq_ring_valid")

# RX must normalize the notification bit before q0 helpers and must not fall
# back to an unlocked q0 txdata read.
order(rx, "raw_qid = pkt->hdr.qid;", "qid = raw_qid & ~(1 << 7);")
require(rx, "qid != IWX_DQA_CMD_QUEUE", "q0 raw-flag fallback exclusion")
require(rx, "iwx_cmd_done(sc, qid, idx, code, pkt,", "normalized q0 completion")

# Task callback lifetime: task_del() handles queued work only.  The serial
# marker fences copied work; the gate tracks callback bodies and has an
# init-only resume admission that cannot race teardown.
for needle in (
    "bool sc_taskq_initialized;",
    "bool sc_task_callbacks_ready;",
    "IOLock *sc_task_gate_lock;",
    "uint32_t sc_task_gate_init_refs;",
    "uint32_t sc_task_gate_stop_refs;",
    "bool sc_task_gate_bootstrap_init;",
    "bool sc_task_gate_detaching;",
):
    require(var, needle, "task callback lifetime storage")
require(task_cpp, "void\ntaskq_barrier(struct taskq *tq)",
        "taskq barrier implementation")
require(task_cpp, "taskq_barrier_task", "taskq barrier completion callback")
require(task_cpp, "The task queues used by the wireless HALs are serial.",
        "taskq barrier serial-queue scope")

# Every driver task enqueue must be through the normal gated helper or the
# explicitly scoped DVACT_WAKEUP bootstrap helper.
if len(re.findall(r"\btask_add\s*\(", cpp)) != 3:
    fail("unexpected direct iwx task_add() bypass")
require(cpp, "task_add(taskq, task);", "normal gated task enqueue")
require(cpp, "task_add(systq, &sc->init_task)",
        "init-only bootstrap task enqueue")
order(attach, "sc->sc_task_callbacks_ready = false;",
      "task_set(&sc->init_task, iwx_init_task_dispatch",
      "sc->sc_task_callbacks_ready = true;")

# Close atomically publishes shutdown and cancels bootstrap admission.  It
# also owns a normal stop ref (or marks permanent detach), while begin_epoch
# serializes exactly one init owner through its hardware/scan work.
order(gate_close, "IOLockLock(sc->sc_task_gate_lock);",
      "sc->sc_task_gate_stop_refs",
      "sc->sc_flags |= IWX_FLAG_SHUTDOWN;",
      "sc->sc_task_gate_closed = true;",
      "sc->sc_task_gate_bootstrap_init = false;")
require(gate_close, "sc_task_gate_detaching", "permanent detach gate state")
require(gate_close, "return false;", "normal-stop ownership rejection")
order(gate_begin, "sc->sc_task_gate_init_refs != 0",
      "*generation = ++sc->sc_generation;",
      "sc->sc_task_gate_init_refs++;")
order(gate_open, "IOLockLock(sc->sc_task_gate_lock);",
      "sc->sc_task_gate_init_refs != 1",
      "sc->sc_generation != generation",
      "sc->sc_task_gate_closed = false;")
require(gate_open, "return false;", "shutdown-aware gate open")
order(gate_rearm, "sc->sc_task_gate_stop_refs--",
      "!sc->sc_task_gate_detaching",
      "sc->sc_generation == generation")
require(gate_drain, "sc->sc_task_gate_init_refs > self_init_refs",
        "init-owner drain")
require(gate_drain, "sc->sc_task_gate_stop_refs > self_stop_refs",
        "stop-owner drain")
order(gate_bootstrap, "IOLockLock(sc->sc_task_gate_lock);",
      "sc->sc_task_gate_closed",
      "!sc->sc_task_gate_bootstrap_init",
      "sc->sc_task_gate_bootstrap_init = true;")

# Stop uses deletion + serial barriers, then drains callback/init/stop owners.
# The init task is the one documented self-barrier exception on systq.
order(stop_internal, "iwx_task_gate_close(sc, false, &stop_generation)",
      "task_del(systq, &sc->init_task);",
      "iwx_cmdq_stop(sc);", "taskq_barrier(sc->sc_nswq);")
require(stop_internal, "if (!caller_is_init_task)",
        "init-task self barrier exclusion")
require(stop_internal, "caller_is_init_epoch ? 1 : 0, 1",
        "init/stop owner self allowance")
order(stop_internal, "iwx_task_gate_drain", "iwx_stop_device(sc);",
      "iwx_task_gate_rearm(sc, stop_generation);")
order(init_task, "iwx_stop_internal(ifp, true, false);",
      "iwx_init_internal(ifp, true);")
order(init_internal, "iwx_task_gate_begin_epoch(sc, &generation)",
      "iwx_cmdq_start(sc, generation)",
      "iwx_task_gate_epoch_live(sc, generation)",
      "err = iwx_init_hw(sc);",
      "iwx_task_gate_open(sc, generation)",
      "iwx_task_gate_end_epoch(sc);")
pre_hw = init_internal[:init_internal.find("err = iwx_init_hw(sc);")]
require(pre_hw, "that->iwx_cmdq_stop(sc);",
        "stale q0 start rollback before init_hw")
open_failure = init_internal[init_internal.find("iwx_task_gate_open(sc, generation)"):]
require(open_failure, "that->iwx_cmdq_stop(sc);",
        "q0 rollback when gate open loses the epoch")
require(init_internal, "stale init epoch, returning ENXIO",
        "scan-wait stale epoch detection")
require(init_internal, "iwx_stop_internal(ifp, caller_is_init_task, true);",
        "self init-owner stop allowance")
order(attach, "iwx_cmdq_start_bootstrap(sc)", "iwx_preinit(sc)")
order(detach, "iwx_task_gate_close(sc, true, &detach_generation);",
      "iwx_cmdq_detach_begin(sc);", "taskq_barrier",
      "iwx_task_gate_drain(sc, 0, 0, 0);")
order(detach, "iwx_interrupt_teardown(sc);", "ieee80211_ifdetach(ifp);",
      "taskq_destroy", "iwx_task_gate_destroy(sc);")

# Tahoe controller lifecycle and raw Skywalk ingress contract.  This is kept
# beside q0 because the observed failure mode is the same: a delayed callback
# or external ioctl dereferences state after its owner has begun teardown.
for needle in (
    "enum AirportItlwmLifecyclePhase",
    "IOLock *fLifecycleLock;",
    "IOSimpleLock *fLifecycleAdmissionLock;",
    "bool fLifecycleTeardownInFlight;",
    "uint32_t fLifecycleDrainWaiters;",
    "bool fLifecycleFinalizing;",
    "uint32_t fLifecycleOperationUsers;",
    "bool fSkywalkInterfaceProviderAttached;",
    "bool fSkywalkInterfaceAttached;",
    "bool fSkywalkEthernetAttached;",
    "AirportItlwmLinkStatePublishLifecycle fLinkStatePublishLifecycle;",
    "AirportItlwmScanSourceLifecycle fScanSourceLifecycle;",
    "void detachSkywalkInterfaceAndFenceBorrowers();",
    "bool beginLifecycleInternalOperation();",
    "bool beginLifecycleOperation();",
    "void endLifecycleOperation();",
):
    require(v2_hpp, needle, "Tahoe lifecycle storage/declaration")

v2_init = airport_method("init")
v2_start = airport_method("start")
v2_stop = airport_method("stop")
v2_free = airport_method("free")
v2_release = airport_method("releaseAll")
v2_drain = airport_method("beginLifecycleDrain")
v2_prepare = airport_method("prepareLifecycleDrain")
v2_finish = airport_method("finishLifecycleDrain")
v2_finalizing = airport_method("beginLifecycleFinalization")
v2_waiters = airport_method("waitForLifecycleDrainWaiters")
v2_hal_claimed = airport_method("stopHalAndDrainClaimed")
v2_watchdog_drain = airport_method("stopWatchdogAndDrain")
v2_scan_drain = airport_method("stopScanSourceAndDrain")
v2_queue_drain = airport_method("stopSkywalkQueuesAndDrain")
v2_skywalk_fence = airport_method("detachSkywalkInterfaceAndFenceBorrowers")
v2_configure = airport_method("configureInterface")
v2_boot_schedule = airport_method("scheduleTahoeBootThreadCall")
v2_boot_drain = airport_method("stopTahoeBootThreadCallAndDrain")
v2_power = airport_method("setPowerState")
v2_new_user_client = airport_method("newUserClient")

for stale_global in ("sLinkStatePublish", "sScanSource"):
    if re.search(r"\\b" + stale_global + r"\\b", v2_cpp):
        fail(f"stale process-global Tahoe source state remains: {stale_global}")

order(v2_init, "fLifecycleLock = IOLockAlloc();",
      "fLifecycleAdmissionLock = IOSimpleLockAlloc();",
      "fLifecyclePhase = kAirportItlwmLifecycleStarting;",
      "fLinkStatePublishLifecycle.admissionLock = IOSimpleLockAlloc();",
      "fScanSourceLifecycle.admissionLock = IOSimpleLockAlloc();")
order(v2_start, "if (!fHalService->attach(pciNub))", "fHalAttached = true;")
order(v2_boot_schedule, "IOLockLock(fTahoeBootCallLock);",
      "!fTahoeBootStopping && !fTahoeBootScheduled",
      "!fTahoeBootCallActive", "fTahoeBootScheduled = true;",
      "fTahoeBootCallActive = true;", "fTahoeBootCallRetained = true;",
      "retain();", "thread_call_enter(tahoeBootThreadCall);")
order(v2_boot_drain, "if (fTahoeBootStopping)",
      "if (fTahoeBootCallOwner == current_thread())",
      "fTahoeBootStopping = true;",
      "const bool canceled = thread_call_cancel(call);", "if (canceled",
      "fTahoeBootCallActive = false;", "while (tahoeBootThreadCall == call && fTahoeBootCallActive)",
      "IOLockSleep(lock, &fTahoeBootCallActive, THREAD_UNINT);",
      "thread_call_free(call)",
      "tahoeBootThreadCall = nullptr;", "if (canceled)",
      "releaseTahoeBootThreadCallRetain();")
require(v2_hpp, "bool fTahoeBootCallActive;",
        "Tahoe boot callback active-latch storage")
require(v2_hpp, "bool fTahoeBootCallRetained;",
        "Tahoe boot callback schedule-retain storage")
require(v2_hpp, "thread_t fTahoeBootCallOwner;",
        "Tahoe boot callback owner storage")
require(v2_hpp, "bool beginTahoeBootThreadCall();",
        "Tahoe boot callback owner admission declaration")
require(v2_hpp, "void completeTahoeBootThreadCall();",
        "Tahoe boot callback completion declaration")
require(v2_hpp, "void releaseTahoeBootThreadCallRetain();",
        "Tahoe boot callback retain release declaration")
if "thread_call_cancel_wait" in v2_cpp:
    fail("Tahoe boot callback still depends on aux-kext-unavailable cancel_wait")

# External work is admitted only while Live.  Start-internal callbacks share
# the same counter but may also enter during Starting; once Draining is
# claimed, prepareLifecycleDrain wakes PLTI waiters before polling that count.
v2_external_op = airport_method("beginLifecycleOperation")
v2_internal_op = airport_method("beginLifecycleInternalOperation")
v2_end_op = airport_method("endLifecycleOperation")
order(v2_external_op, "fLifecyclePhase == kAirportItlwmLifecycleLive",
      "++fLifecycleOperationUsers")
order(v2_internal_op, "kAirportItlwmLifecycleStarting",
      "kAirportItlwmLifecycleLive", "++fLifecycleOperationUsers")
order(v2_end_op, "if (fLifecycleOperationUsers != 0)",
      "--fLifecycleOperationUsers")
order(v2_drain, "IOLockLock(controlLock);", "fLifecycleFinalizing",
      "while (fLifecycleTeardownInFlight)", "++fLifecycleDrainWaiters;",
      "IOLockSleep(controlLock, this, THREAD_UNINT);",
      "--fLifecycleDrainWaiters;", "fLifecyclePhase = kAirportItlwmLifecycleDraining;",
      "fLifecycleTeardownInFlight = true;")
order(v2_prepare, 'cancelPendingAssocTarget("prepareLifecycleDrain", true)',
      "fLifecycleOperationUsers == 0")
order(v2_stop, "if (!beginLifecycleDrain())", "prepareLifecycleDrain();",
      "disableAdapter(NULL);", "stopHalAndDrainClaimed();",
      "releaseAll(false);", "super::stop(provider);", "finishLifecycleDrain();")
order(v2_hal_claimed, "prepareLifecycleDrain();",
      "stopTahoeBootThreadCallAndDrain();",
      "teardownLinkStatePublishSource(this, _fWorkloop);",
      "stopWatchdogAndDrain();", "releaseAPSTAOwnerClaimed();",
      "fHalService->detach(pciNub);")
if v2_cpp.count("fHalService->detach(pciNub);") != 1:
    fail("V2 has a raw HAL detach outside its claimed lifecycle drain")
order(v2_finalizing, "fLifecycleFinalizing = true;")
order(v2_finish, "fLifecyclePhase = kAirportItlwmLifecycleStopped;",
      "fLifecycleDrainOwner = NULL;", "fLifecycleTeardownInFlight = false;",
      "IOLockWakeup(controlLock, this, false);")
order(v2_waiters, "while (fLifecycleDrainWaiters != 0)",
      "IOLockSleep(controlLock, &fLifecycleDrainWaiters, THREAD_UNINT);")
order(v2_free, "releaseAll(false);", "beginLifecycleFinalization();",
      "finishLifecycleDrain();", "waitForLifecycleDrainWaiters();",
      "IOSimpleLockFree(fLinkStatePublishLifecycle.admissionLock);",
      "IOSimpleLockFree(fScanSourceLifecycle.admissionLock);",
      "IOSimpleLockFree(fLifecycleAdmissionLock);", "IOLockFree(fLifecycleLock);")

# The reverse-engineered Skywalk contract makes detach a real borrower fence:
# callbacks stop first, then logical-link/queue detach, then queue/pool
# release.  Keep the evidence in the test so a local guard cannot silently
# replace the framework fence for returned raw queue pointers.
for needle in (
    "STOPPED -> DETACHED when logical-link/queue detach completes",
    "DETACHED -> RELEASED when pool/queue objects are finally released",
    "No pool release before DETACHED.",
):
    require(skywalk_runtime_contract, needle, "Skywalk teardown evidence")
require(skywalk_family_contract, "Recursive Skywalk channel teardown",
        "detachInterface recursive teardown evidence")
require(bsd_attach_contract, "IOService::attach(controller)",
        "Skywalk provider attachment evidence")
require(skywalk_stop_contract,
        "IOService::detach called by IOKit framework AFTER stop completes",
        "normal provider-detach ownership evidence")

# attach(), attachInterface(), and ether_ifattach() have distinct inverses.
# In particular, the attachInterface failure branch is already provider-bound,
# so releaseAll must be able to fence it without double-detaching the normal
# stop path.
order(v2_init, "fSkywalkInterfaceProviderAttached = false;",
      "fSkywalkInterfaceAttached = false;",
      "fSkywalkEthernetAttached = false;")
order(v2_start, "if (!fNetIf->attach(this))",
      "fSkywalkInterfaceProviderAttached = true;",
      "if (!attachInterface(fNetIf, this))",
      "releaseAll();",
      "fSkywalkInterfaceAttached = true;")
order(v2_configure, "ether_ifattach(ifp,",
      "fSkywalkEthernetAttached = true;")

# The RX trampoline is not contingent on BSD attachment: it is published
# before logical-link registration. It must be closed after queue source
# removal and before the lower HAL is detached.
order(v2_hal_claimed, "stopWatchdogAndDrain();",
      "ic->ic_ac.ac_if.if_skywalk_rx = NULL;",
      "fHalService->detach(pciNub);")
order(v2_watchdog_drain, "stopSkywalkQueuesAndDrain();")

# One shared helper owns every inverse. It performs optional BSD detach,
# framework detach, and provider detach in reverse order, exactly once; the
# generic release path invokes it before any queue/pool or interface release.
order(v2_skywalk_fence, "if (fSkywalkEthernetAttached)",
      "ether_ifdetach(ifp);",
      "const bool hadInterfaceAttachment = fSkywalkInterfaceAttached;",
      "if (hadInterfaceAttachment && fNetIf != nullptr)",
      "detachInterface(fNetIf, true);",
      "fSkywalkInterfaceAttached = false;",
      "if (!hadInterfaceAttachment && fSkywalkInterfaceProviderAttached &&",
      "fNetIf->detach(this);",
      "fSkywalkInterfaceProviderAttached = false;")
order(v2_release, "stopHalAndDrainClaimed();",
      "detachSkywalkInterfaceAndFenceBorrowers();",
      "skywalkTxDrainCompletionPackets(this);",
      "skywalkRxDrainPendingPackets(this);",
      "OSSafeReleaseNULL(fMultiCastQueue);",
      "OSSafeReleaseNULL(fTxCompQueue);",
      "OSSafeReleaseNULL(fTxQueue);",
      "OSSafeReleaseNULL(fRxQueue);",
      "OSSafeReleaseNULL(fTxPool);",
      "OSSafeReleaseNULL(fRxPool);",
      "OSSafeReleaseNULL(fNetIf);")
for token, label in (
    ("detachInterface(fNetIf, true);", "framework interface detach"),
    ("fNetIf->detach(this);", "provider detach"),
    ("ether_ifdetach(ifp);", "BSD ethernet detach"),
):
    if v2_cpp.count(token) != 1:
        fail(f"Skywalk teardown has not got exactly one {label}")

# Per-controller source records publish only after setup has claimed its state;
# teardown keeps tearingDown through removeEventSource, clears published
# pointers before dropping refs, and only then permits a second owner to return.
link_setup = body_from_marker(
    v2_cpp, "static bool setupLinkStatePublishSource", "link source setup")
link_queue = body_from_marker(
    v2_cpp, "static void queueOffGateLinkStatePublish", "link source producer")
link_action = body_from_marker(
    v2_cpp, "static void publishLinkStateInterruptAction", "link source action")
link_drain = body_from_marker(
    v2_cpp, "static void teardownLinkStatePublishSource", "link source drain")
scan_setup = body_from_marker(v2_cpp, "static bool setupScanSource", "scan source setup")
scan_acquire = body_from_marker(v2_cpp, "static bool acquireScanSource", "scan source admission")
scan_release = body_from_marker(v2_cpp, "static void releaseScanSource", "scan source release")
scan_schedule = airport_method("scheduleScanSource")
scan_cancel = airport_method("cancelScanSource")
scan_live = airport_method("scanSourceCallbackLive")
scan_done = airport_method("fakeScanDone")

for needle in (
    "struct AirportItlwmLinkStatePublishLifecycle",
    "IOSimpleLock *admissionLock;",
    "IOSimpleLock *payloadLock;",
    "bool settingUp;",
    "bool tearingDown;",
    "uint32_t users;",
    "struct AirportItlwmScanSourceLifecycle",
):
    require(v2_hpp, needle, "per-controller source lifetime storage")
order(link_setup, "state.settingUp = true;", "workloop->retain();",
      "workloop->addEventSource(source)", "source->enable();",
      "state.source = source;", "state.payloadLock = payloadLock;",
      "state.settingUp = false;")
order(link_queue, "++state.users;", "source->retain();",
      "source->interruptOccurred(0, 0, 0);", "source->release();",
      "--state.users;")
order(link_action, "state.tearingDown", "sender != state.source",
      "IOSimpleLockLockDisableInterrupt(payloadLock);",
      "state.pendingValid = false;")
order(link_drain, "state.stopping = true;", "state.tearingDown = true;",
      "const bool drained = !state.settingUp && state.users == 0;",
      "source->disable();", "workloop->removeEventSource(source);",
      "state.source = NULL;", "state.payloadLock = NULL;",
      "IOSimpleLockFree(payloadLock);", "source->release();",
      "state.tearingDown = false;")
order(scan_setup, "state.settingUp = true;", "workloop->retain();",
      "workloop->addEventSource(source)", "source->enable();",
      "state.source = source;", "that->scanSource = source;",
      "state.settingUp = false;")
order(scan_acquire, "++state.users;", "*out = state.source;",
      "(*out)->retain();")
order(scan_release, "source->release();", "--state.users;")
order(scan_schedule, "acquireScanSource(this, &source)",
      "source->setTimeoutMS(timeoutMs);", "releaseScanSource(this, source);")
order(scan_cancel, "acquireScanSource(this, &source)",
      "source->cancelTimeout();", "source->disable();",
      "releaseScanSource(this, source);")
order(scan_live, "!state.settingUp", "!state.stopping", "!state.tearingDown",
      "state.source == sender", "scanSource == sender")
order(v2_scan_drain, "state.stopping = true;", "state.tearingDown = true;",
      "const bool drained = !state.settingUp && state.users == 0;",
      "source->cancelTimeout();", "source->disable();",
      "workloop->removeEventSource(source);", "state.source = NULL;",
      "scanSource = NULL;", "source->release();", "state.tearingDown = false;")
order(scan_done, "!that->scanSourceCallbackLive(sender)",
      "that->getCommandGate()->runAction(postWclScanResultsGated")
if re.search(r"\\bscanSource\\s*->", skywalk_cpp):
    fail("Skywalk still invokes a raw cached scanSource")
if re.search(r"\\binstance->scanSource\\b", skywalk_cpp):
    fail("Skywalk bypasses controller scan admission")
for queue in ("fMultiCastQueue", "fTxCompQueue", "fTxQueue", "fRxQueue"):
    order(v2_queue_drain, f"{queue}->disable();",
          f"removeEventSource({queue});")

# The raw Skywalk dispatch remains leaf-oriented: there is no broad
# dispatcher admission (which would double-count nested leaves or reject
# bootstrap cache requests). Every direct external leaf that touches the
# controller/HAL takes exactly one Live admission; shared helpers use an
# unguarded Impl spelling only when invoked under an already-held guard.
def skywalk_method(name):
    pattern = re.compile(
        r"(?m)^\s*(?:IOReturn|bool|SInt32|SInt64|UInt64|int|void)\s*\*?\s*"
        r"AirportItlwmSkywalkInterface::[ \t]*\n" + re.escape(name) +
        r"\s*\([^;{}]*\)\s*\{")
    match = pattern.search(skywalk_cpp)
    if not match:
        fail(f"missing Skywalk method {name}")
    brace = skywalk_cpp.find("{", match.start(), match.end())
    depth = 0
    for pos in range(brace, len(skywalk_cpp)):
        if skywalk_cpp[pos] == "{":
            depth += 1
        elif skywalk_cpp[pos] == "}":
            depth -= 1
            if depth == 0:
                return skywalk_cpp[brace + 1:pos]
    fail(f"unterminated Skywalk method {name}")

dispatcher = skywalk_method("processApple80211Ioctl")
if "AIRPORT_ITLWM_REQUIRE_LIVE_OPERATION();" in dispatcher:
    fail("dispatcher has a broad nested Live admission")
if "instance->fAPSTAOwner" in dispatcher:
    fail("dispatcher reads fAPSTAOwner without a controller snapshot/guard")
order(dispatcher, "case APPLE80211_IOC_STATE: {",
      "AirportItlwmLifecycleOperationGuard lifecycle(", "fHalService ?")
if dispatcher.count("airportItlwmRunDispatchLive(") < 18:
    fail("direct controller/APSTA dispatcher routes lost their Live wrapper")

live_leaves = (
    "getCHANNELS_INFO", "getSSID", "getTahoeCompactSSID",
    "getAWDL_PEER_TRAFFIC_STATS", "setCIPHER_KEY", "setCUR_PMK",
    "setVHT_CAPABILITY", "setASSOCIATE", "setDISASSOCIATE",
    "setCLEAR_PMKSA_CACHE", "setSCAN_REQ", "setWCL_SCAN_REQ",
    "setWCL_ASSOCIATE", "setWCL_LEAVE_NETWORK", "setWCL_SCAN_ABORT",
    "setVIRTUAL_IF_CREATE", "setWCL_UPDATE_FAST_LANE", "setCHANNEL",
    "setWCL_REASSOC", "setWCL_JOIN_ABORT", "setWCL_QOS_PARAMS",
    "setWCL_LINK_UP_DONE", "setSET_MAC_ADDRESS", "getPHY_MODE",
    "getCHANNEL", "getSTATE", "getMCS_INDEX_SET", "getVHT_MCS_INDEX_SET",
    "getMCS_VHT", "getRATE_SET", "getOP_MODE", "getWCL_EXTENDED_BSS_INFO",
    "getTXPOWER", "getRATE", "getBSSID", "getTahoeCompactBSSID",
    "getHW_ADDR", "getVHT_CAPABILITY", "getHT_CAPABILITY",
    "getHE_CAPABILITY", "getGUARD_INTERVAL", "getMAX_NSS_FOR_AP",
    "getTXRX_CHAIN_INFO", "getRSSI", "getRSN_IE", "getAP_IE_LIST",
    "getNOISE", "getNSS", "getSUPPORTED_CHANNELS", "getDEAUTH",
    "getASSOCIATION_STATUS", "getMCS", "getLINK_CHANGED_EVENT_DATA",
    "getCURRENT_NETWORK", "getSCAN_RESULT", "getWCL_BGSCAN_CACHE_RESULT",
    "getWCL_CHANNELS_INFO", "getWCL_BSS_INFO", "getHW_SUPPORTED_CHANNELS",
    "getCOUNTRY_CHANNELS_INFO", "getBSS_BLACKLIST", "setBSS_BLACKLIST",
)
for name in live_leaves:
    body = skywalk_method(name).lstrip()
    if not body.startswith("AIRPORT_ITLWM_REQUIRE_LIVE_OPERATION();"):
        fail(f"Skywalk external leaf lacks exactly-one Live admission: {name}")
for name, macro in (
    ("setLinkStateInternal", "AIRPORT_ITLWM_REQUIRE_INTERNAL_BOOL_OPERATION();"),
    ("setWCL_LINK_STATE_UPDATE", "AIRPORT_ITLWM_REQUIRE_INTERNAL_OPERATION();"),
    ("setInterfaceEnable", "AIRPORT_ITLWM_REQUIRE_INTERNAL_SINT32_OPERATION();"),
):
    if not skywalk_method(name).lstrip().startswith(macro):
        fail(f"Skywalk bootstrap callback lost internal lifecycle admission: {name}")

# Raw queue/pool inventory is a different ABI class from controller/HAL
# selectors: the framework borrows these pointers while detachInterface()
# recursively tears down the logical link. Do not insert a short-lived
# operation guard that would return NULL in Draining; releaseAll's ordered
# detach is the lifetime fence. These readers must stay pure (no HAL, command
# gate, association, or queue state mutation).
require(skywalk_cpp,
        "Skywalk inventory accessors return framework-borrowed raw pointers.",
        "raw Skywalk borrower policy")
raw_inventory_readers = (
    "pendingPackets", "packetSpace", "getTxQueueDepth",
    "getRxQueueCapacity", "getMultiCastQueue", "getRxCompQueue",
    "getTxCompQueue", "getTxSubQueue", "getTxPacketPool",
    "getRxPacketPool", "getNumTxQueues",
)
for name in raw_inventory_readers:
    body = skywalk_method(name)
    require(body, "AirportItlwm *controller = instance;",
            f"raw Skywalk controller snapshot for {name}")
    if "AirportItlwmLifecycleOperationGuard" in body:
        fail(f"raw Skywalk borrower is incorrectly rejected in Draining: {name}")
    for forbidden in ("fHalService", "getCommandGate", "setLinkStatus",
                      "->enable(", "->disable("):
        if forbidden in body:
            fail(f"raw Skywalk reader mutates controller/HAL state: {name}")

enable_datapath = skywalk_method("enableDatapath")
order(enable_datapath,
      "AirportItlwmLifecycleOperationGuard lifecycle(",
      "AirportItlwmLifecycleAdmission::StartingOrLive);",
      "if (!lifecycle.admitted())",
      "controller->fTxCompQueue->enable()")
disable_datapath = skywalk_method("disableDatapath")
require(disable_datapath,
        "detachInterface() can synchronously request datapath shutdown",
        "Draining-safe datapath disable rationale")
if "AirportItlwmLifecycleOperationGuard" in disable_datapath:
    fail("disableDatapath rejects the framework teardown path")
for forbidden in ("fHalService", "getCommandGate", "->enable("):
    if forbidden in disable_datapath:
        fail("disableDatapath touches HAL or can reopen the data path")
for queue in ("fTxQueue", "fMultiCastQueue", "fRxQueue", "fTxCompQueue"):
    require(disable_datapath, f"controller->{queue}->disable()",
            f"Draining-safe disable for {queue}")

for public, impl in (
    ("getRATE_SET", "getRATE_SETImpl"),
    ("getMCS_INDEX_SET", "getMCS_INDEX_SETImpl"),
    ("getVHT_MCS_INDEX_SET", "getVHT_MCS_INDEX_SETImpl"),
    ("getCHANNELS_INFO", "getCHANNELS_INFOImpl"),
    ("getSUPPORTED_CHANNELS", "getSUPPORTED_CHANNELSImpl"),
    ("setWCL_ASSOCIATE", "setWCL_ASSOCIATEImpl"),
    ("setCHANNEL", "setCHANNELImpl"),
    ("setSET_MAC_ADDRESS", "setSET_MAC_ADDRESSImpl"),
):
    require(skywalk_method(public), f"return {impl}(",
            f"guarded-to-Impl split for {public}")
    if "AIRPORT_ITLWM_REQUIRE_" in skywalk_method(impl):
        fail(f"shared Impl accidentally reacquires lifecycle admission: {impl}")
v2_apsta_channel = airport_method("setAPSTA_CHANNEL")
require(v2_apsta_channel, "interface->setCHANNELImpl(in)",
        "APSTA CHANNEL fallback preserves its dispatcher-held Live admission")
if "interface->setCHANNEL(in)" in v2_apsta_channel:
    fail("APSTA CHANNEL fallback reacquires a nested Live admission")

# User-client and IOPM producers are outside the normal raw-vtable leaves.
# They retain the provider/operation across every use and pair null-gate exits.
for needle in (
    "IOLock       *fProviderLock;",
    "AirportItlwm *retainProvider();",
    "AirportItlwm *takeProvider();",
):
    require(v2_cpp, needle, "PLTI provider lifetime fence")
for marker in (
    "IOReturn AirportItlwmUserClient::\nsExtDeliverPMK",
    "IOReturn AirportItlwmUserClient::\nsExtWaitAssociationTarget",
):
    label = marker.rsplit("\n", 1)[1]
    body = body_from_marker(v2_cpp, marker, f"PLTI {label}")
    order(body, "target->retainProvider()", "provider->beginLifecycleOperation()")
    if body.count("provider->endLifecycleOperation();") < 2:
        fail(f"PLTI {label} lacks balanced lifecycle operation exits")
order(v2_new_user_client, "if (!beginLifecycleOperation())",
      "clientHasPrivilege", "client->attach(this)", "client->start(this)",
      "*handler = client;", "endLifecycleOperation();")
order(v2_power, "if (!beginLifecycleInternalOperation())",
      "IOCommandGate *gate = getCommandGate();", "if (gate == nullptr)",
      "endLifecycleOperation();", "gate->runAction(")
power_after_gate = v2_power[v2_power.find("gate->runAction("):]
require(power_after_gate, "endLifecycleOperation();",
        "IOPM lifecycle operation release after runAction")
rx_input = body_from_marker(v2_cpp, "static int\nskywalkRxInput", "Skywalk RX ingress")
order(rx_input, "AirportItlwmControllerLifecycleOperationGuard lifecycle(that, false);",
      "!lifecycle.admitted()", "that->fRxPool", "that->fRxQueue")
event_handler = body_from_marker(v2_cpp, "eventHandler(struct ieee80211com *ic", "HAL event ingress")
order(event_handler, "AirportItlwmControllerLifecycleOperationGuard lifecycle(that, true);",
      "!lifecycle.admitted()", "IOCommandGate *gate = that->getCommandGate();")
if "getCommandGate()->runAction" in event_handler:
    fail("HAL event ingress bypasses its null-checked command-gate snapshot")

# IOEthernetController virtuals bypass the raw Skywalk selector dispatcher.
# Their lifecycle admission must therefore bracket every direct HAL access.
output_packet = airport_method("outputPacket")
order(output_packet,
      "AirportItlwmControllerLifecycleOperationGuard lifecycle(this, false);",
      "!lifecycle.admitted() || fHalService == nullptr",
      "freePacket(m)",
      "struct _ifnet *ifp = &fHalService->get80211Controller()")
multicast_list = airport_method("setMulticastList")
order(multicast_list,
      "AirportItlwmControllerLifecycleOperationGuard lifecycle(this, false);",
      "!lifecycle.admitted() || fHalService == nullptr",
      "tahoeOwnerRegistry.controller.multicastCount = len;",
      "fHalService->getDriverController()->setMulticastList")
for name in ("setHardwareAddress", "getHardwareAddress", "configureInterface",
             "enableAdapter"):
    require(airport_method(name), "AirportItlwmControllerLifecycleOperationGuard lifecycle(",
            f"controller/HAL lifecycle admission for {name}")
link_status = airport_method("setLinkStatus")
if "struct _ifnet *ifq = &fHalService" in link_status:
    fail("setLinkStatus retains an unconditional HAL/ifnet dereference")
order(link_status,
      "const bool drainOwner = lifecycleDrainOwnedByCurrentThread();",
      "AirportItlwmControllerLifecycleOperationGuard lifecycle(this, true);",
      "if (!lifecycle.admitted() && !drainOwner)",
      "bool ret = super::setLinkStatus",
      "if (!lifecycle.admitted())",
      "if (fNetIf)",
      "fHalService->get80211Controller()")
if "if (lifecycle.admitted() && fHalService != nullptr)" in link_status:
    fail("setLinkStatus leaves interface publication outside lifecycle admission")

# APSTA is intentionally not parity-closed in the shipped STA-only runtime.
# iwx's current per-family gate is all-false and iwn's opt-in macro defaults
# to zero, so role-7 must return exactly the same NotReady/Unsupported result
# as the lower gate *before* allocating/publishing an owner.  The comments and
# checks deliberately keep this separate from a future AP-capable build, which
# must add selector admission and producer-bridge draining rather than treating
# this containment precheck as an AP implementation.
skywalk_vif_create = body_from_marker(
    skywalk_cpp, "setVIRTUAL_IF_CREATE(apple80211_virt_if_create_data *data)",
    "Tahoe role-7 create")
legacy_vif_create = body_from_marker(
    legacy_ioctl_cpp,
    "setVIRTUAL_IF_CREATE(OSObject *object, struct apple80211_virt_if_create_data* data)",
    "legacy role-7 create")

def role_case(body, marker, label):
    start = body.find(marker)
    if start < 0:
        fail(f"missing {label} role case")
    end = body.find("default:", start)
    if end < 0:
        fail(f"unterminated {label} role case")
    return body[start:end]


skywalk_role7 = role_case(skywalk_vif_create, "case 7: {", "Tahoe")
legacy_role7 = role_case(legacy_vif_create,
                         "case APPLE80211_VIF_SOFT_AP: {", "legacy")
for role, owner_call, label in (
    (skywalk_role7, "instance->ensureAPSTAOwner(data)", "Tahoe"),
    (legacy_role7, "ensureAPSTAOwner(data)", "legacy"),
):
    owner_pos = role.find(owner_call)
    if owner_pos < 0:
        fail(f"{label} role-7 does not retain its future opt-in owner path")
    pre_owner = role[:owner_pos]
    if "return kIOReturnSuccess" in pre_owner:
        fail(f"{label} STA-only quarantine acknowledges role-7 before owner admission")
    require(role, "not APSTA parity closure", f"{label} non-parity scope")
    require(role, "selector admission", f"{label} future AP admission scope")
    require(role, "producer-bridge", f"{label} future AP producer-drain scope")

order(skywalk_role7, "if (instance == nullptr)", "return kIOReturnNotReady;",
      "if (instance->fHalService == nullptr)", "return kIOReturnNotReady;",
      "if (!instance->fHalService->supportsAPMode())",
      "return kIOReturnUnsupported;", "instance->ensureAPSTAOwner(data)")
order(legacy_role7, "if (fHalService == nullptr)", "return kIOReturnNotReady;",
      "if (!fHalService->supportsAPMode())", "return kIOReturnUnsupported;",
      "ensureAPSTAOwner(data)")

iwx_family_gate = body_from_marker(
    iwx_apgo_hpp, "static inline bool iwx_firmware_family_supports_ap_go",
    "iwx shipped AP capability gate")
if "return true;" in iwx_family_gate:
    fail("iwx shipped AP capability gate no longer fails closed for every family")
require(iwx_family_gate, "case IWX_DEVICE_FAMILY_22000:",
        "iwx current-family fail-closed classification")
require(iwx_family_gate, "case IWX_DEVICE_FAMILY_AX210:",
        "iwx current-family fail-closed classification")
require(iwn_cpp, "#define IWN_APGO_FIRMWARE_BACKEND_OPT_IN 0",
        "iwn shipped AP opt-in default")
require(iwn_cpp, "#if IWN_APGO_FIRMWARE_BACKEND_OPT_IN",
        "iwn AP explicit opt-in boundary")

print("iwx q0 and outer lifecycle serialization contract: PASS")

PY
