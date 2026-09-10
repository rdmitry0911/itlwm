#!/usr/bin/env bash
# Contract for Tahoe normal scans: FAST is cache-only, while every ordinary
# IWN request owns a generation-tagged physical lease through its terminal.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
sky = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()
v2 = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
v2h = (root / "AirportItlwm/AirportItlwmV2.hpp").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
iwnh = (root / "itlwm/hal_iwn/ItlIwn.hpp").read_text()
iwnvar = (root / "itlwm/hal_iwn/if_iwnvar.h").read_text()
iwm = (root / "itlwm/hal_iwm/mac80211.cpp").read_text()
iwmvar = (root / "itlwm/hal_iwm/if_iwmvar.h").read_text()
iwx = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()
iwxvar = (root / "itlwm/hal_iwx/if_iwxvar.h").read_text()
hal = (root / "include/HAL/ItlHalService.hpp").read_text()
node = (root / "itl80211/openbsd/net80211/ieee80211_node.c").read_text()
nodeh = (root / "itl80211/openbsd/net80211/ieee80211_node.h").read_text()
var = (root / "itl80211/openbsd/net80211/ieee80211_var.h").read_text()
fsm = (root / "AirportItlwm/TahoeStandardScanContracts.hpp").read_text()

def fail(message):
    raise SystemExit(f"Tahoe standard-scan lifecycle contract: {message}")

def require(text, token, label):
    if token not in text:
        fail(f"missing {label}: {token}")

def forbid(text, token, label):
    if token in text:
        fail(f"unexpected {label}: {token}")

def ordered(text, label, *tokens):
    cursor = 0
    for token in tokens:
        found = text.find(token, cursor)
        if found < 0:
            fail(f"{label} missing ordered token: {token}")
        cursor = found + len(token)

def body(text, marker, label):
    start = text.find(marker)
    if start < 0:
        fail(f"missing {label}")
    opening = text.find("{", start)
    if opening < 0:
        fail(f"missing body for {label}")
    depth = 0
    for position in range(opening, len(text)):
        if text[position] == "{":
            depth += 1
        elif text[position] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:position]
    fail(f"unterminated {label}")

setter = body(sky, "IOReturn AirportItlwmSkywalkInterface::\nsetSCAN_REQ",
              "standard SCAN_REQ setter")
for token in (
        "APPLE80211_SCAN_TYPE_FAST",
        "prepareTahoeWclAssociationBackend()",
        "scheduleScanSource(100)",
        "reserveStandardPhysicalScan",
        "fHalService->beginStandardScan",
        "if (backendGeneration != 0)",
        "activateStandardPhysicalScan",
        "failStandardPhysicalScanStart",
        "finishPendingStandardPhysicalScanTerminal",
        "ieee80211_begin_cache_bgscan(&ic->ic_ac.ac_if);",
        "ieee80211_begin_scan(&ic->ic_ac.ac_if);",
):
    require(setter, token, "normal scan setter")
forbid(setter, "if (fScanResultWrapping)",
       "completed result iterator blocking the next scan")
ordered(setter, "PowerOn readiness precedes every scan side effect",
        "prepareTahoeWclAssociationBackend()",
        "if (backendResult != kIOReturnSuccess)",
        "return backendResult;",
        "fNextNodeToSend = NULL;",
        "fScanResultWrapping = false;",
        "if (sd->scan_type == APPLE80211_SCAN_TYPE_FAST)")
ordered(setter, "new request resets the prior result iterator",
        "fNextNodeToSend = NULL;",
        "fScanResultWrapping = false;",
        "if (sd->scan_type == APPLE80211_SCAN_TYPE_FAST)")
if setter.count("scheduleScanSource(100)") != 1:
    fail("FAST cache timer must be armed exactly once")
fast = setter.find("if (sd->scan_type == APPLE80211_SCAN_TYPE_FAST)")
physical = setter.find("reserveStandardPhysicalScan")
if fast < 0 or physical < 0 or fast >= physical:
    fail("FAST and physical requests are not split")
forbid(setter, "cancelScanSource", "unsafe timer cancellation")
forbid(setter, "postMessage(", "synthetic physical completion")
forbid(setter, "postMessageGated", "gated synthetic physical completion")
ordered(setter, "exact lower normal-scan path",
        "reserveStandardPhysicalScan", "fHalService->beginStandardScan",
        "if (backendGeneration != 0)", "activateStandardPhysicalScan")
nonzero_backend = setter[setter.find("if (backendGeneration != 0)"):]
ordered(nonzero_backend, "submitted lower lease stays fenced on error",
        "activateStandardPhysicalScan", "return beginResult;")
fallback = setter[setter.find("beginResult == kIOReturnUnsupported"):]
ordered(fallback, "fallback releases exact reservation first",
        "failStandardPhysicalScanStart(generation)",
        "ieee80211_begin_cache_bgscan(&ic->ic_ac.ac_if);")

for token in (
        "struct AirportItlwmStandardScanLifecycle",
        "cachedTerminalPending",
        "cachedTerminalPublishing",
        "TahoeStandardScanContracts::State physicalState",
        "fStandardScanLifecycle",
):
    require(v2h, token, "controller normal-scan state")
for token in (
        "bool AirportItlwm::scheduleScanSource",
        "bool AirportItlwm::beginCachedScanTerminal",
        "IOReturn AirportItlwm::reserveStandardPhysicalScan",
        "TahoeStandardScanContracts::reserve",
        "TahoeStandardScanContracts::claimTerminal",
        "TahoeStandardScanContracts::beginDraining",
        "TahoeStandardScanContracts::reopenAfterRadioReset",
        "fWclPhysicalScanLifecycle.admissionLock",
):
    require(v2, token, "shared physical admission")
schedule = body(v2, "bool AirportItlwm::scheduleScanSource", "FAST timer fence")
for token in (
        "cachedTerminalPending",
        "TahoeStandardScanContracts::idle(&standard.physicalState)",
        "wcl.state.phase == TahoeWclPhysicalScanContracts::Phase::Idle",
        "source->setTimeoutMS(timeoutMs)",
):
    require(schedule, token, "FAST timer fence")
reserve = body(v2, "IOReturn AirportItlwm::reserveStandardPhysicalScan",
               "normal physical reservation")
for token in (
        "!standard.cachedTerminalPending",
        "!standard.cachedTerminalPublishing",
        "TahoeStandardScanContracts::idle(&standard.physicalState)",
        "wcl.state.phase == TahoeWclPhysicalScanContracts::Phase::Idle",
        "TahoeStandardScanContracts::reserve(&standard.physicalState",
):
    require(reserve, token, "normal/WCL exclusion")
wcl_reserve = body(v2, "IOReturn AirportItlwm::reserveWclPhysicalScan",
                   "WCL reservation")
for token in (
        "standard.cachedTerminalPending",
        "standard.cachedTerminalPublishing",
        "!TahoeStandardScanContracts::idle(&standard.physicalState)",
):
    require(wcl_reserve, token, "WCL/normal exclusion")
radio_invalidation = body(v2, "void AirportItlwm::invalidateWclPhysicalScan()",
                          "radio invalidation")
ordered(radio_invalidation, "FAST revoke precedes radio drain",
        "cancelCachedTerminal = standard.cachedTerminalPending;",
        "standard.cachedTerminalPending = false;",
        "TahoeStandardScanContracts::beginDraining",
        "source->cancelTimeout();")
forbid(radio_invalidation, "standard.cachedTerminalPublishing = false;",
       "publishing FAST terminal bypassing its release fence")
handler = body(v2, "void AirportItlwm::\neventHandler", "controller event handler")
ordered(handler, "normal terminal handling",
        "IEEE80211_EVT_STANDARD_SCAN_INVALIDATED",
        "IEEE80211_EVT_STANDARD_SCAN_TERMINAL",
        "claimStandardPhysicalScanTerminal")
fake = body(v2, "void AirportItlwm::fakeScanDone", "FAST terminal")
ordered(fake, "FAST publishing fence release",
        "beginCachedScanTerminal(sender)", "postMessageGated",
        "finishCachedScanTerminal()")
forbid(v2, "cancelScanSource", "removed timer cancellation helper")

for token in (
        "enum class Phase",
        "Starting",
        "Active",
        "Completing",
        "Draining",
        "activeGeneration",
        "activeBackendGeneration",
        "claimTerminal",
        "finishPendingTerminal",
        "reopenAfterRadioReset",
):
    require(fsm, token, "generation-tagged normal-scan reducer")
forbid(fsm, "postMessage", "framework publication in reducer")

for token in (
        "virtual IOReturn beginStandardScan(uint64_t generation, bool background,",
        "return kIOReturnUnsupported;",
):
    require(hal, token, "optional exact lower API")
for token in (
        "beginStandardScan(uint64_t generation, bool background,",
):
    require(iwnh, token, "IWN normal-scan override")
require(iwnvar, "IWN_SCAN_LEASE_STANDARD_CONTROLLER", "IWN lease owner")
for token in (
        "bool            publication_invalidated;",
        "bool            hardware_invalidated;",
):
    require(iwnvar, token, "separate lower invalidation domains")
begin = body(iwn, "IOReturn ItlIwn::\nbeginStandardScan", "IWN normal begin")
ordered(begin, "IWN lower scan submission",
        "iwn_scan_start(&com", "IWN_SCAN_LEASE_STANDARD_CONTROLLER",
        "&backend_generation")
scan_start = body(iwn, "int ItlIwn::\niwn_scan_start", "IWN scan start")
for token in (
        "bool wcl_background = owner == IWN_SCAN_LEASE_WCL_BACKGROUND;",
        "bool wcl_foreground = owner == IWN_SCAN_LEASE_WCL_INITIAL;",
        "bool wcl = iwn_scan_lease_owner_is_wcl(owner);",
        "bool standard = owner == IWN_SCAN_LEASE_STANDARD_CONTROLLER;",
        "bool prearm_background = wcl_background || (standard && bgscan != 0);",
        "bool controller_foreground = wcl_foreground ||\n        (standard && bgscan == 0);",
        "iwn_scan_lease_reserve(sc, owner, upper_generation,",
        "required_initial_handoff_serial,",
        "controller_foreground, wcl_foreground,",
        "&foreground_prepared);",
        "foreground_prepared || (bgscan == 0 && !wcl_foreground)",
        "iwn_scan_schedule_fatal_recovery(sc);",
):
    require(scan_start, token, "IWN standard owner")
ordered(scan_start, "normal scan builds after durable lower arm",
        "iwn_scan_lease_arm_submission(sc, serial, &abort_requested)",
        "if (abort_requested)",
        "error = iwn_scan_submit(sc, flags, bgscan, serial,",
        "controller_foreground, wcl_foreground,")
arm = body(iwn, "static bool\niwn_scan_lease_arm_submission",
           "lower submission arm")
require(arm, "const bool abort_requested = sc->sc_scan_lease.abort_requested;",
        "pre-submit abort observation")
forbid(arm, "command_submitted = true;",
       "pre-doorbell terminal admission")
doorbell_prepare = body(iwn, "static bool\niwn_scan_lease_prepare_doorbell",
                       "lower doorbell ownership")
for token in (
        "sc->sc_scan_lease.phase == IWN_SCAN_LEASE_ARMING",
        "!sc->sc_scan_lease.abort_requested",
        "!sc->sc_scan_lease.publication_invalidated",
        "sc->sc_scan_lease.command_submitted = true;",
        "sc->sc_flags |= IWN_FLAG_SCANNING;",
        "if (context->background)",
        "sc->sc_flags |= IWN_FLAG_BGSCAN;",
        "context->lock_held = true;",
):
    require(doorbell_prepare, token, "atomic lower doorbell ownership")
doorbell_finish = body(iwn, "static void\niwn_scan_lease_finish_doorbell",
                      "lower doorbell release")
require(doorbell_finish, "IOSimpleLockUnlock(context->lock);",
        "doorbell fence release")
submit = body(iwn, "int ItlIwn::\niwn_scan_submit", "IWN command submission")
ordered(submit, "build precedes foreground preparation",
        "buf = (uint8_t *)malloc(IWN_SCAN_MAXSZ",
        "hdr->len = htole16(buflen);",
        "if (prepare_controller_foreground)",
        "iwn_prepare_controller_foreground_scan(ic);")
ordered(submit, "prepared scan reaches exact doorbell hook",
        "iwn_prepare_controller_foreground_scan(ic);",
        "iwn_cmd_with_doorbell_hook(sc, IWN_CMD_SCAN, buf, buflen, 1,",
        "iwn_scan_lease_prepare_doorbell",
        "iwn_scan_lease_finish_doorbell")
require(iwn, "bool publish_wcl_initial_started,",
        "expanded WCL foreground submit argument")
require(iwn, "bool wcl_scan,",
        "WCL-owned foreground dwell submit argument")
for token in (
        "if (publish_wcl_initial_started)",
        "ieee80211_free_allnodes(ic, 1 /* fresh initial census */);",
):
    require(submit, token, "WCL foreground census remains explicit")
require(submit, "*out_command_attempted = doorbell.committed;",
        "doorbell commit result")
forbid(submit, "sc->sc_flags |= IWN_FLAG_SCANNING;",
       "late scan-flag publication after doorbell")
cmd = body(iwn, "int ItlIwn::\niwn_cmd_with_doorbell_hook",
           "IWN hooked command submission")
ordered(cmd, "transport readiness precedes the IRQ-safe doorbell fence",
        "error = iwn_set_cmd_in_flight(sc);",
        "if (error != 0)",
        "(*pre_doorbell)(sc, doorbell_context)",
        "iwn_clear_cmd_in_flight(sc);",
        "ops->update_sched(sc, ring->qid, submittedIndex, 0, 0);",
        "IWN_WRITE(sc, IWN_HBUS_TARG_WRPTR",
        "(*post_doorbell)(sc, doorbell_context)")
for token in (
        "mbuf_freem(m);",
        "data->m = NULL;",
        "data->map->dm_nsegs = 0;",
        "explicit_bzero(desc, sizeof(*desc));",
):
    require(cmd, token, "rejected doorbell cleanup")
terminal_claim = body(iwn, "static bool\niwn_scan_lease_claim_terminal",
                      "lower terminal claim")
terminal_admission = terminal_claim[:terminal_claim.find("terminal->valid = true;")]
for token in (
        "!sc->sc_scan_lease.hardware_invalidated",
        "IWN_SCAN_LEASE_ACTIVE",
        "IWN_SCAN_LEASE_ABORTING",
):
    require(terminal_claim, token, "reset-safe terminal claim")
forbid(terminal_admission, "publication_invalidated",
       "upper-ticket cancellation blocking lower terminal cleanup")
continuation = body(iwn, "static bool\niwn_scan_lease_begin_continuation",
                    "multi-band continuation arm")
for token in (
        "sc->sc_scan_lease.command_submitted = false;",
        "sc->sc_scan_lease.phase = IWN_SCAN_LEASE_ARMING;",
):
    require(continuation, token, "continuation terminal fence")
continue_submit = body(iwn, "int ItlIwn::\niwn_scan_continue",
                       "multi-band continuation submit")
ordered(continue_submit, "failed continuation restores current terminal",
        "iwn_scan_lease_begin_continuation(sc, &serial, &wcl_scan)",
        "iwn_scan_submit(sc, flags, bgscan, serial, false, false,",
        "wcl_scan, 0, 0,",
        "iwn_scan_lease_restore_continuation(sc, serial, true)")
continuation_restore = body(iwn,
                            "static bool\niwn_scan_lease_restore_continuation",
                            "failed continuation terminal status")
ordered(continuation_restore, "failed continuation is aborted",
        "if (abort_terminal)",
        "sc->sc_scan_lease.abort_requested = true;",
        "IWN_SCAN_LEASE_ABORTING")
require(continuation_restore, "!sc->sc_scan_lease.hardware_invalidated",
        "upper-ticket cancellation restores native terminal")
for marker, label in (
        ("static bool\niwn_scan_lease_defer_scan", "deferred scan"),
        ("static bool\niwn_scan_lease_defer_terminal_replay", "terminal replay"),
        ("static bool\niwn_scan_lease_finish_terminal", "terminal finish"),
):
    require(body(iwn, marker, label), "!sc->sc_scan_lease.hardware_invalidated",
            f"{label} reset fence")
hardware_invalidation = body(iwn,
    "static enum iwn_scan_lease_owner\niwn_scan_lease_begin_hardware_invalidation",
    "hardware invalidation")
require(hardware_invalidation, "sc->sc_scan_lease.hardware_invalidated = true;",
        "hardware reset invalidation")
abort_start = scan_start[scan_start.find("if (abort_requested)"):]
ordered(abort_start, "pre-submit abort rolls back before replay",
        "iwn_scan_lease_rollback(sc, serial)",
        "*out_backend_generation = 0;",
        "iwn_scan_lease_schedule_replay_task(sc);")
for token in (
        "iwn_scan_lease_defer_terminal_replay",
        "sc->sc_scan_lease.terminal_claimed",
        "return iwn_scan_lease_defer_terminal_replay(sc, nstate, arg) ? 1 : 0;",
):
    require(iwn, token, "post-terminal foreground scan replay")
stop = body(iwn, "case IWN_STOP_SCAN", "IWN scan terminal")
ordered(stop, "normal terminal remains on the generic completion route",
        "iwn_scan_lease_claim_terminal",
        "initial_handoff =",
        "IEEE80211_SCAN_COMPLETION_WCL_HANDOFF",
        "IEEE80211_SCAN_COMPLETION_WCL_FOREGROUND",
        "ieee80211_end_scan_owned(ifp,",
        "IEEE80211_SCAN_COMPLETION_GENERIC",
        "IEEE80211_EVT_STANDARD_SCAN_TERMINAL")
for token in (
        "ieee80211_end_scan_controlled(ifp,",
        "if (initial_handoff)",
        "else if (terminal.wcl_foreground)",
        "else\n                ieee80211_end_scan_owned(ifp,",
        "terminal.standard",
        "terminal.publish_standard_terminal",
        "standard_terminal.generation = terminal.upper_generation",
        "standard_terminal.backend_generation",
        "IEEE80211_EVT_STANDARD_SCAN_INVALIDATED",
):
    require(iwn, token, "exact IWN normal terminal/reset")

for token in (
        "void ieee80211_prepare_scan(struct _ifnet *);",
        "void ieee80211_begin_scan(struct _ifnet *);",
        "int ieee80211_begin_scan_with_result(struct _ifnet *);",
        "enum ieee80211_scan_completion_mode",
        "IEEE80211_SCAN_COMPLETION_GENERIC",
        "IEEE80211_SCAN_COMPLETION_WCL_HANDOFF",
        "IEEE80211_SCAN_COMPLETION_WCL_FOREGROUND",
        "void ieee80211_end_scan_controlled(struct _ifnet *,",
):
    require(nodeh, token, "foreground preparation split")
prepare = body(node, "void\nieee80211_prepare_scan", "scan preparation")
forbid(prepare, "ieee80211_next_scan", "second lower scan from preparation")
generic = body(node, "void\nieee80211_begin_scan", "generic scan begin")
ordered(generic, "generic scan still submits after preparation",
        "ieee80211_prepare_scan(ifp);",
        "(void)ieee80211_next_scan_result(ifp);")
checked = body(node, "int\nieee80211_begin_scan_with_result",
               "checked power-on scan begin")
ordered(checked, "checked power-on scan returns the lower result",
        "ieee80211_prepare_scan(ifp);",
        "return ieee80211_next_scan_result(ifp);")
next_result = body(node, "static int\nieee80211_next_scan_result",
                   "result-preserving scan hop")
ordered(next_result, "scan hop keeps preflight and epoch semantics",
        "ic->ic_newstate_preflight(ic, IEEE80211_S_SCAN,",
        "return EBUSY;",
        "ieee80211_pae_assoc_epoch_note_newstate(ic, IEEE80211_S_SCAN,",
        "return ic->ic_newstate(ic, IEEE80211_S_SCAN,")

init_task = body(iwn, "iwn_init_task(void *arg1)",
                 "IWN bounded power-on recovery")
for token in (
        "error = that->iwn_init(ifp);",
        "&sc->init_retry_count, 1, __ATOMIC_ACQ_REL",
        "if (attempt < 5)",
        "sc->sc_flags |= IWN_FLAG_FATAL_RECOVERY;",
        "(void)task_add(systq, &sc->init_task);",
        "power-on recovery exhausted after %u attempts",
):
    require(init_task, token, "IWN bounded power-on recovery")
ordered(init_task, "IWN retries full hardware epochs before terminal failure",
        "error = that->iwn_init(ifp);",
        "if (error == 0)",
        "&sc->init_retry_count, 1, __ATOMIC_ACQ_REL",
        "if (attempt < 5)",
        "sc->sc_flags |= IWN_FLAG_FATAL_RECOVERY;",
        "(void)task_add(systq, &sc->init_task);",
        "sc->sc_flags &= ~IWN_FLAG_FATAL_RECOVERY;",
        "power-on recovery exhausted after %u attempts")
iwn_init = body(iwn, "iwn_init(struct _ifnet *ifp)",
                "IWN power-on first scan")
for token in (
        "error = ieee80211_begin_scan_with_result(ifp);",
        "if (error != 0)",
        "goto fail;",
        "IEEE80211_EVT_WCL_SCAN_REOPENED",
):
    require(iwn_init, token, "IWN checked power-on scan")
ordered(iwn_init, "lower scan acceptance precedes availability",
        "error = ieee80211_begin_scan_with_result(ifp);",
        "if (error != 0)",
        "goto fail;",
        "IEEE80211_EVT_WCL_SCAN_REOPENED")

for source, var_source, prefix, flags in (
        (iwm, iwmvar, "IWM", "IWM_FLAG_SHUTDOWN | IWM_FLAG_RFKILL"),
        (iwx, iwxvar, "IWX", "IWX_FLAG_SHUTDOWN | IWX_FLAG_RFKILL")):
    require(var_source, "u_int8_t\t\tinit_retry_count;",
            f"{prefix} bounded power-on counter")
    task = body(source, f"{prefix.lower()}_init_task(void *arg1)",
                f"{prefix} bounded power-on recovery")
    for token in (
            "attempted = true;",
            "&sc->init_retry_count, 1, __ATOMIC_ACQ_REL",
            f"sc->sc_flags & ({flags})",
            "if (attempt < 5)",
            "power-on recovery exhausted after %u attempts"):
        require(task, token, f"{prefix} bounded power-on recovery")

iwm_task = body(iwm, "iwm_init_task(void *arg1)",
                "IWM bounded power-on recovery")
ordered(iwm_task, "IWM retries a complete firmware/first-scan epoch",
        "error = that->iwm_init(ifp);",
        "&sc->init_retry_count, 1, __ATOMIC_ACQ_REL",
        "if (attempt < 5)",
        "(void)task_add(systq, &sc->init_task);")
iwm_wake = body(iwm, "iwm_activate(struct iwm_softc *sc, int act)",
                "IWM wake recovery admission")
ordered(iwm_wake, "IWM wake never strands a transient ready failure",
        "case DVACT_WAKEUP:",
        "&sc->init_retry_count, 0, __ATOMIC_RELEASE",
        "if (!iwm_set_hw_ready(sc))",
        "(void)task_add(systq, &sc->init_task);")
forbid(iwm_wake, "init_task NOT scheduled",
       "IWM transient ready failure stranding power-on")

iwx_task = body(iwx, "iwx_init_task(void *arg1)",
                "IWX bounded power-on recovery")
ordered(iwx_task, "IWX retries through its bootstrap lifecycle token",
        "error = that->iwx_init_internal(ifp, true);",
        "&sc->init_retry_count, 1, __ATOMIC_ACQ_REL",
        "if (attempt < 5)",
        "that->iwx_bootstrap_init_task(sc);")
iwx_wake = body(iwx, "iwx_activate(struct iwx_softc *sc, int act)",
                "IWX wake recovery admission")
ordered(iwx_wake, "IWX wake never strands a transient ready failure",
        "case DVACT_WAKEUP:",
        "&sc->init_retry_count, 0, __ATOMIC_RELEASE",
        "err = iwx_prepare_card_hw(sc);",
        "that->iwx_bootstrap_init_task(sc);")
forbid(iwx_wake, "init_task NOT scheduled",
       "IWX transient ready failure stranding power-on")
iwx_enable = body(iwx, "IOReturn ItlIwx::enable(IONetworkInterface *netif)",
                  "IWX accepted asynchronous power-on")
ordered(iwx_enable, "IWX resume preflight hands off to bounded recovery",
        "ifp->if_flags |= IFF_UP;",
        "iwx_activate(&com, DVACT_RESUME)",
        "continuing with bounded power-on recovery",
        "iwx_activate(&com, DVACT_WAKEUP)",
        "return kIOReturnSuccess;")
forbid(iwx_enable, "ifp->if_flags &= ~IFF_UP;",
       "IWX transient resume failure revoking accepted power-on")
for token in (
        "IEEE80211_EVT_STANDARD_SCAN_TERMINAL",
        "IEEE80211_EVT_STANDARD_SCAN_INVALIDATED",
        "struct ieee80211_standard_scan_terminal",
):
    require(var, token, "normal terminal ABI")
controlled_end_scan = body(node, "void\nieee80211_end_scan_owned",
                          "controlled scan completion")
for token in (
        "const int generic_terminal = mode == IEEE80211_SCAN_COMPLETION_GENERIC;",
        "if (!generic_terminal)",
        "ieee80211_reset_scan(ifp);",
        "IEEE80211_EVT_SCAN_DONE",
):
    require(controlled_end_scan, token, "controlled generic/WCL completion split")
end_scan = body(node, "void\nieee80211_end_scan(struct _ifnet *ifp)",
                "generic scan completion wrapper")
ordered(end_scan, "generic completion wrapper",
        "ieee80211_end_scan_controlled(ifp,",
        "IEEE80211_SCAN_COMPLETION_GENERIC")
if controlled_end_scan.count("IEEE80211_F_BGSCAN |\n                              IEEE80211_F_DISABLE_BG_AUTO_CONNECT") < 2:
    fail("early background-scan exits do not restore scan flags")

done_start = v2.find("case IEEE80211_EVT_SCAN_DONE:")
done_end = v2.find("case IEEE80211_EVT_WCL_REASSOC_DONE:", done_start)
if done_start < 0 or done_end < 0:
    fail("generic scan-done consumer is missing")
done = v2[done_start:done_end]
for token in (
        "sRT.scanDoneCount++",
        "apple80211Msg = APPLE80211_M_SCAN_DONE",
        "msgData = &scanStatus",
        "msgDataLen = sizeof(scanStatus)",
):
    require(done, token, "normal generic terminal publication")

print("Tahoe standard-scan lifecycle contract: PASS")
PY
