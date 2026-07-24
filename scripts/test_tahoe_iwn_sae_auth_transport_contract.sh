#!/usr/bin/env bash
# Static contract for the lab-gated, one-ticket IWN SAE Authentication TX
# transport.  It proves only the physical TX/completion spine: it is not an
# SAE or WPA3 association claim.  The selected-BSS state owner, Open-System
# suppression, AKM/PMK activation and on-air association proof remain
# separate work.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import re
import sys


root = Path(sys.argv[1])
hal = (root / "include/HAL/ItlHalService.hpp").read_text()
hpp = (root / "itlwm/hal_iwn/ItlIwn.hpp").read_text()
cpp = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
var = (root / "itlwm/hal_iwn/if_iwnvar.h").read_text()
build = (root / "scripts/build_tahoe.sh").read_text()


def fail(message: str) -> None:
    raise SystemExit(f"IWN SAE auth transport contract: {message}")


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        fail(f"missing {label}: {needle}")


def forbid(text: str, needle: str, label: str) -> None:
    if needle in text:
        fail(f"unexpected {label}: {needle}")


def block_after(source: str, opening: int, label: str) -> str:
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    fail(f"unterminated {label}")


def body(source: str, name: str, label: str) -> str:
    pattern = re.compile(r"\b" + re.escape(name) +
                         r"\s*\([^;{}]*\)\s*\{", re.S)
    match = pattern.search(source)
    if match is None:
        fail(f"missing {label}")
    return block_after(source, source.rfind("{", match.start(), match.end()),
                       label)


def iwn_method(name: str) -> str:
    pattern = re.compile(r"\bItlIwn\s*::\s*" + re.escape(name) +
                         r"\s*\([^;{}]*\)\s*\{", re.S)
    match = pattern.search(cpp)
    if match is None:
        fail(f"missing ItlIwn::{name}()")
    return block_after(cpp, cpp.rfind("{", match.start(), match.end()),
                       f"ItlIwn::{name}")


def ordered(text: str, label: str, *needles: str) -> None:
    cursor = 0
    for needle in needles:
        position = text.find(needle, cursor)
        if position < 0:
            fail(f"{label} missing ordered token: {needle}")
        cursor = position + len(needle)


# The ordinary HAL is fail-closed.  IWN owns an explicit private workloop
# gate and only public descriptor identity; no credential material crosses or
# remains in firmware-completion storage.
require(hal, "virtual IOReturn submitSaeAuthFrame(", "HAL SAE submission ABI")
require(hal, "return kIOReturnUnsupported;", "fail-closed default HAL")
for token in (
        "submitSaeAuthFrame(", "cancelSaeAuthFrame(",
        "IOCommandGate *fSaeTxGate", "iwn_sae_tx_gate_action",
        "iwn_sae_tx_commit_doorbell", "iwn_sae_tx_task"):
    require(hpp, token, "IWN SAE declaration")
for token in (
        "struct task        sae_tx_task;", "sc_sae_tx_lifecycle_lock",
        "sc_sae_tx_lifecycle_active", "sc_sae_tx_lifecycle_closed",
        "sc_sae_tx_lock", "sc_sae_tx_active", "sc_sae_tx_doorbelled",
        "sc_sae_tx_stopping", "sc_sae_tx_active_ticket",
        "sc_sae_tx_cancel_through", "sc_sae_tx_generation",
        "sc_sae_tx_active_generation", "sc_sae_tx_eventq",
        "IWN_SAE_TX_EVENTQ_LEN", "sae_lifecycle_generation",
        "sae_association_epoch", "sae_relay_generation", "sae_bssid",
        "sae_sta"):
    require(var, token, "IWN bounded SAE owner storage")
for secret in ("sae_body", "sae_password", "sae_pmk", "sae_pwe", "sae_kck"):
    forbid(var, secret, "secret descriptor storage")

# Direct physical Algorithm-3 admission is an opt-in laboratory feature,
# never a surprise in the ordinary production IWN artifact.
require(build, "IWN_SOFTWARE_PMF_LAB_BUILD=1", "lab compiler opt-in")
lab_gate = body(cpp, "iwn_sae_auth_transport_lab_opted_in",
                "IWN SAE laboratory gate")
ordered(lab_gate, "laboratory gate", "#if IWN_SOFTWARE_PMF_LAB_BUILD",
        "return true;", "#else", "return false;")
submit = iwn_method("submitSaeAuthFrame")
for token in (
        "itl_sae_auth_transport_request_is_well_formed",
        "iwn_sae_auth_transport_lab_opted_in", "kIOReturnUnsupported",
        "iwn_sae_tx_lifecycle_enter(sc, false)", "sc_sae_tx_active",
        "sc_sae_tx_event_count != 0", "gate = fSaeTxGate", "gate->retain",
        "gate->attemptAction(&ItlIwn::iwn_sae_tx_gate_action, &args)",
        "kIOReturnCannotLock", "iwn_sae_tx_retire_unsubmitted",
        "iwn_sae_tx_lifecycle_leave(sc)"):
    require(submit, token, "one-ticket IWN submission")
ordered(submit, "IWN gate lifetime", "gate = fSaeTxGate", "gate->retain",
        "gate->attemptAction(&ItlIwn::iwn_sae_tx_gate_action, &args)",
        "gate->release", "iwn_sae_tx_lifecycle_leave(sc)")
for token in ("getMainCommandGate", "runAction", "commandSleep", "iwn_start(",
              "IEEE80211_SEND_MGMT"):
    forbid(submit, token, "controller/generic TX reentry in submission")
cancel = iwn_method("cancelSaeAuthFrame")
for token in ("iwn_sae_tx_lifecycle_enter(sc, true)",
              "iwn_sae_tx_lifecycle_leave(sc);", "sc_sae_tx_cancel_through",
              "!sc->sc_sae_tx_doorbelled"):
    require(cancel, token, "IWN cancellation fence")
forbid(cancel, "ic_event_handler", "terminal callback from cancellation")

# The private action makes a real preassociation frame only after it owns a
# live selected BSS.  It never uses the generic Open-System sender.
action = iwn_method("iwn_sae_tx_gate_action")
require(action, "iwn_sae_tx_submit_on_gate", "private gate action")
leaf = iwn_method("iwn_sae_tx_submit_on_gate")
for token in (
        "iwn_sae_tx_request_is_live", "ieee80211_ref_node(ic->ic_bss)",
        "ieee80211_sae_auth_frame_build", "IEEE80211_S_AUTH",
        "ieee80211_pae_assoc_epoch_current(ic)", "ic->ic_bss != ni",
        "memcmp(ni->ni_macaddr, request->bssid",
        "memcmp(ic->ic_myaddr, request->sta", "iwn_tx(sc, m, ni, request)",
        "iwn_sae_tx_retire_unsubmitted"):
    require(leaf, token, "selected-BSS IWN TX leaf")
if leaf.count("iwn_sae_tx_request_is_live") < 2:
    fail("IWN TX leaf lacks pre- and post-builder liveness fences")
forbid(leaf, "IEEE80211_SEND_MGMT", "generic management sender bypass")

# Gate and task lifetime must cover a retained submitter, a copied taskq
# callback, and the final event-source withdrawal before DMA/softc teardown.
attach = iwn_method("iwn_attach")
for token in (
        "sc->sc_sae_tx_lifecycle_lock = IOLockAlloc()",
        "sc->sc_sae_tx_lock = IOSimpleLockAlloc()",
        "sc->sc_sae_tx_lifecycle_closed = true",
        "sc->sc_sae_tx_stopping = true",
        "fSaeTxGate = IOCommandGate::commandGate(this)",
        "pa->workloop->addEventSource(fSaeTxGate)",
        "task_set(&sc->sae_tx_task, iwn_sae_tx_task"):
    require(attach, token, "IWN private-gate setup")
detach_begin = iwn_method("iwn_sae_tx_detach_begin")
ordered(detach_begin, "IWN SAE teardown",
        "iwn_sae_tx_lifecycle_close(sc, true)", "iwn_sae_tx_cancel_all(sc)",
        "sc->sc_sae_tx_task_ready = false", "task_del(systq, &sc->sae_tx_task)",
        "taskq_barrier(systq)", "iwn_sae_tx_lifecycle_drain(sc)",
        "gate = fSaeTxGate", "fSaeTxGate = NULL",
        "removeEventSource(gate)", "gate->release")
detach = iwn_method("detach")
ordered(detach, "IWN outer teardown", "iwn_sae_tx_detach_begin(sc)",
        "iwn_interrupt_teardown(sc)", "iwn_free_tx_ring")

# Validate every on-air byte before the legacy TX routine trims the 802.11
# header.  Only then may normal DMA machinery gain descriptor ownership; the
# scheduler mutation and final WRPTR write are one cancellation linearization
# point.
tx = iwn_method("iwn_tx")
for token in (
        "itl_sae_auth_transport_request_is_well_formed(sae_request)",
        "type != IEEE80211_FC0_TYPE_MGT",
        "subtype != IEEE80211_FC0_SUBTYPE_AUTH",
        "hdrlen != sizeof(struct ieee80211_frame)",
        "mbuf_len(m) < hdrlen + 6 + sae_request->body_len",
        "mbuf_pkthdr_len(m) != hdrlen + 6 + sae_request->body_len",
        "memcmp(wh->i_addr1, sae_request->bssid",
        "memcmp(wh->i_addr2, sae_request->sta",
        "memcmp(wh->i_addr3, sae_request->bssid",
        "LE_READ_2(auth) != IEEE80211_AUTH_ALG_SAE",
        "LE_READ_2(auth + 2) != sae_request->wire_transaction",
        "LE_READ_2(auth + 4) != sae_request->auth_status",
        "memcmp(auth + 6, sae_request->body",
        "data->m = m", "data->ni = ni", "data->sae_active = true",
        "data->sae_lifecycle_generation", "iwn_sae_tx_commit_doorbell"):
    require(tx, token, "raw Algorithm-3 TX fence")
if tx.find("if (sae_request != NULL)") > tx.find("mbuf_adj(m"):
    fail("IWN validates SAE bytes only after header trim")
dma_tail = tx[tx.find("nsegs = data->map->cursor"):]
ordered(dma_tail, "descriptor-before-doorbell", "data->m = m", "data->ni = ni",
        "iwn_sae_tx_data_clear(data)", "data->sae_active = true",
        "iwn_sae_tx_commit_doorbell")
forbid(tx, "ops->reset_sched(sc, ring->qid, saved_cur)",
       "unsafe 4965 pre-doorbell scheduler rollback")
commit = iwn_method("iwn_sae_tx_commit_doorbell")
ordered(commit, "doorbell cancellation fence", "IOLockLock(sc->sc_sae_tx_lifecycle_lock)",
        "IOSimpleLockLock(sc->sc_sae_tx_lock)", "sc->sc_sae_tx_active_ticket == ticket",
        "sc->sc_sae_tx_active_generation == sc->sc_sae_tx_generation",
        "ticket > sc->sc_sae_tx_cancel_through",
        "sc->ops.update_sched(sc, qid, descriptor_idx, station_id, length)",
        "sc->sc_sae_tx_doorbelled = true",
        "IWN_WRITE(sc, IWN_HBUS_TARG_WRPTR", "IOSimpleLockUnlock",
        "IOLockUnlock")

# Both real firmware status formats use the common terminal path.  ACK retry
# count alone is not converted to failure: only neither SUCCESS nor
# DIRECT_DONE is terminal EIO.
for name in ("iwn4965_tx_done", "iwn5000_tx_done"):
    native = iwn_method(name)
    for token in ("IWN_TX_STATUS_SUCCESS", "IWN_TX_STATUS_DIRECT_DONE",
                  "that->iwn_tx_done(sc, desc"):
        require(native, token, f"{name} common TX_DONE funnel")
    forbid(native, "iwn_sae_tx_report_terminal", f"{name} duplicate SAE terminal")
common_done = iwn_method("iwn_tx_done")
ordered(common_done, "common SAE terminal before release",
        "iwn_sae_tx_report_terminal(sc, data, txfail ? EIO : 0)",
        "iwn_tx_done_free_txdata(sc, data)")
for name in ("iwn_ampdu_txq_advance", "iwn_reset_tx_ring", "iwn_free_tx_ring"):
    reclaim = iwn_method(name)
    require(reclaim, "iwn_sae_tx_report_terminal(sc", f"{name} reset/reclaim terminal")
queue = iwn_method("iwn_sae_tx_queue_terminal")
for token in ("sc->sc_sae_tx_active", "sc->sc_sae_tx_doorbelled",
              "sc->sc_sae_tx_active_ticket != event->ticket",
              "sc->sc_sae_tx_active_generation != lifecycle_generation",
              "KASSERT(sc->sc_sae_tx_event_count < IWN_SAE_TX_EVENTQ_LEN",
              "entry->is_reset = true", "iwn_sae_tx_schedule_task"):
    require(queue, token, "terminal FIFO ownership")
forbid(queue, "ic_event_handler", "IRQ-context controller callback")

# Completion callback is deferred.  The requeue remains inside its lifecycle
# lease and schedule admission is serialized with close; reset alone may be
# scheduled after close, while detach is always rejected.
schedule = body(cpp, "iwn_sae_tx_schedule_task", "IWN task scheduler")
for token in ("IOLockLock(sc->sc_sae_tx_lifecycle_lock)",
              "!sc->sc_sae_tx_lifecycle_closed || allow_closed",
              "!sc->sc_sae_tx_detaching", "sc->sc_sae_tx_task_ready",
              "task_add(systq, &sc->sae_tx_task)"):
    require(schedule, token, "lifecycle-safe task scheduling")
task = iwn_method("iwn_sae_tx_task")
for token in ("IEEE80211_EVT_SAE_AUTH_TRANSPORT_RESET",
              "IEEE80211_EVT_SAE_AUTH_TRANSPORT", "ic->ic_event_handler",
              "iwn_sae_tx_schedule_task(sc, false)",
              "iwn_sae_tx_lifecycle_leave(sc)"):
    require(task, token, "deferred terminal delivery")
ordered(task, "task requeue lifetime", "iwn_sae_tx_schedule_task(sc, false)",
        "iwn_sae_tx_lifecycle_leave(sc)")
forbid(task, "getMainCommandGate", "controller gate from completion worker")
snapshot = iwn_method("iwn_sae_tx_snapshot_reset")
require(snapshot, "snapshot->result = EIO", "reset terminal result")
emit_reset = iwn_method("iwn_sae_tx_emit_reset_event")
for token in ("iwn_sae_tx_lifecycle_enter(sc, true)",
              "entry->is_reset = true", "iwn_sae_tx_schedule_task(sc, true)",
              "iwn_sae_tx_lifecycle_leave(sc)"):
    require(emit_reset, token, "post-close reset delivery")
hw_stop = iwn_method("iwn_hw_stop")
ordered(hw_stop, "hardware-reset ownership", "iwn_sae_tx_stop_begin(sc)",
        "iwn_sae_tx_snapshot_reset(sc, &reset_event)",
        "iwn_sae_tx_cancel_all(sc)", "iwn_reset_tx_ring(sc",
        "iwn_sae_tx_purge(sc)", "iwn_sae_tx_emit_reset_event(sc, &reset_event)")
forbid(hw_stop, "taskq_barrier", "blocking barrier in calibration reset")


class TicketModel:
    """Small model for the source's cancel/reset terminal ownership rule."""

    def __init__(self) -> None:
        self.ticket = None
        self.doorbelled = False
        self.cancel_through = 0
        self.events = []

    def reserve(self, ticket: int) -> bool:
        if self.ticket is not None or ticket <= self.cancel_through:
            return False
        self.ticket = ticket
        return True

    def cancel(self, ticket: int) -> None:
        self.cancel_through = max(self.cancel_through, ticket)
        if self.ticket == ticket and not self.doorbelled:
            self.ticket = None

    def complete(self, result: int) -> None:
        ticket = self.ticket
        self.ticket = None
        self.doorbelled = False
        if ticket is not None and ticket > self.cancel_through:
            self.events.append(("complete", ticket, result))

    def reset(self) -> None:
        ticket = self.ticket
        self.ticket = None
        self.doorbelled = False
        if ticket is not None and ticket > self.cancel_through:
            self.events.append(("reset", ticket, "EIO"))


model = TicketModel()
assert model.reserve(10)
model.cancel(10)
assert model.ticket is None and not model.events
assert model.reserve(11)
model.doorbelled = True
model.cancel(11)
model.complete(0)
assert not model.events
assert model.reserve(12)
model.doorbelled = True
model.reset()
assert model.events == [("reset", 12, "EIO")]

print("PASS: IWN lab-gated SAE transport owns one real TX descriptor and deferred terminal completion")
PY
