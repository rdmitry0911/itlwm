#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

python3 - "$ROOT" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
sky = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()
core = (root / "itl80211/openbsd/net80211/ieee80211.c").read_text()
node = (root / "itl80211/openbsd/net80211/ieee80211_node.c").read_text()
proto = (root / "itl80211/openbsd/net80211/ieee80211_proto.c").read_text()
pae = (root / "itl80211/openbsd/net80211/ieee80211_pae_input.c").read_text()
var = (root / "itl80211/openbsd/net80211/ieee80211_var.h").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
iwm = (root / "itlwm/hal_iwm/IwmSaeEngine.inc").read_text()
iwm_hal = (root / "itlwm/hal_iwm/ItlIwm.cpp").read_text()
iwm_mac = (root / "itlwm/hal_iwm/mac80211.cpp").read_text()
iwm_scan = (root / "itlwm/hal_iwm/scan.cpp").read_text()
iwm_rx = (root / "itlwm/hal_iwm/rx.cpp").read_text()
iwm_reg = (root / "itlwm/hal_iwm/if_iwmreg.h").read_text()
iwm_var = (root / "itlwm/hal_iwm/if_iwmvar.h").read_text()
iwx = (root / "itlwm/hal_iwx/IwxSaeEngine.inc").read_text()
iwx_hal = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()


def fail(message):
    raise SystemExit(f"Tahoe WCL reassoc roam-scan contract: {message}")


def require(text, needle, label):
    if needle not in text:
        fail(f"missing {label}: {needle}")


def forbid(text, needle, label):
    if needle in text:
        fail(f"unexpected {label}: {needle}")


def ordered(text, label, *needles):
    cursor = 0
    for needle in needles:
        pos = text.find(needle, cursor)
        if pos < 0:
            fail(f"{label} missing ordered token: {needle}")
        cursor = pos + len(needle)


def body(text, marker, label):
    start = text.find(marker)
    if start < 0:
        fail(f"missing {label}")
    opening = text.find("{", start)
    if opening < 0:
        fail(f"missing body for {label}")
    depth = 0
    for pos in range(opening, len(text)):
        if text[pos] == "{":
            depth += 1
        elif text[pos] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:pos]
    fail(f"unterminated {label}")


def section(text, marker, end_marker, label):
    start = text.find(marker)
    if start < 0:
        fail(f"missing {label}")
    end = text.find(end_marker, start + len(marker))
    if end < 0:
        fail(f"unterminated {label}")
    return text[start:end]


carrier = body(sky, "struct apple80211_reassoc\n", "Apple reassoc carrier")
for token in (
    "uint16_t channel_specs[50]",
    "apple80211_reassoc_candidate candidates[7]",
    "uint32_t candidate_count",
    "uint32_t channel_spec_count",
    "uint8_t feature_flags",
    "int8_t prune_rssi_dbm",
):
    require(carrier, token, "25C56 carrier layout")
for token in (
    "offsetof(apple80211_reassoc, candidates) == 0x64",
    "offsetof(apple80211_reassoc, candidate_count) == 0x90",
    "offsetof(apple80211_reassoc, channel_spec_count) == 0x94",
    "sizeof(apple80211_reassoc) == 0x9c",
):
    require(sky, token, "carrier ABI assertion")

producer = body(sky, "setWCL_REASSOC(apple80211_reassoc *data)",
                "WCL reassoc producer")
require(producer, "ieee80211_begin_wcl_reassoc_bgscan",
        "real lower roam-scan delegation")
require(producer, "data->channel_spec_count",
        "50-entry chanspec ingestion")
require(producer, "data->candidate_count", "candidate ingestion")
forbid(producer, "SAME_BSS_TRANSPARENT", "fabricated same-BSS success")
forbid(producer, "IEEE80211_FC0_SUBTYPE_REASSOC_REQ",
       "OTA request to the still-current BSS")
forbid(producer, "clearExternalPmkEligibilityLocked",
       "credential destruction before target selection")

scan = body(core, "ieee80211_begin_wcl_reassoc_bgscan(",
            "common WCL roam scan")
for token in (
    "(*ic->ic_bgscan_start)(ic, serial)",
    "ieee80211_free_allnodes(ic, 0)",
    "IEEE80211_F_BGSCAN",
    "IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_STARTED",
):
    require(scan, token, "real HAL scan ownership")
forbid(scan, "airportItlwmIsRoamLocked",
       "explicit request blocked by autonomous-roam preference")

supersede = body(core, "ieee80211_cancel_wcl_reassoc_bgscan(",
                 "foreground WCL supersession of reassoc scan")
for token in (
    "ic->ic_bgscan_abort",
    "IEEE80211_F_DISABLE_BG_AUTO_CONNECT",
    "IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_FAILED",
    "ieee80211_wcl_reassoc_post_failure",
):
    require(supersede, token, "paired lower/upper reassoc cancellation")

join = body(sky, "setWCL_ASSOCIATEImpl(apple80211AssocCandidates *candidates)",
            "WCL join producer")
require(join, "ieee80211_cancel_wcl_reassoc_bgscan",
        "JoinAdapter supersession before replacement AUTH")
public_scan = body(sky, "setWCL_SCAN_REQ(apple80211ScanRequest *req)",
                   "WCL scan producer")
ordered(public_scan, "ScanAdapter supersession before BGSCAN admission",
        "if (ic->ic_wcl_reassoc_owner_active)",
        "ieee80211_cancel_wcl_reassoc_bgscan(",
        "if ((ic->ic_flags & IEEE80211_F_BGSCAN) != 0")
cached = body(sky, "tahoeFindJoinableCachedWclCandidate(\n    struct ieee80211com *ic,",
              "cached WCL candidate predicate")
for token in ("IEEE80211_F_BGSCAN", "ic_wcl_reassoc_owner_active"):
    require(cached, token, "scan-owner fence on cached direct join")

iwx_abort = body(iwx_hal, "iwx_bgscan_abort(struct ieee80211com *ic, uint64_t reassocSerial)",
                 "IWX reassoc scan abort")
for token in ("return EINVAL", "iwx_scan_abort(sc, true, reassocSerial)"):
    require(iwx_abort, token, "IWX lower abort ownership")
require(iwx_hal, "ic->ic_bgscan_abort = iwx_bgscan_abort",
        "IWX lower abort hook publication")
iwx_replace = body(iwx_hal, "iwx_scan(struct iwx_softc *sc, const ItlStateTransitionRequest &request)",
                   "IWX foreground replacement")
for token in ("ic_wcl_reassoc_owner_active",
              "ieee80211_cancel_wcl_reassoc_bgscan(ic, ECANCELED)",
              "scanCommandBackgroundPending()",
              "iwx_umac_scan(sc, 0, scanSerial)"):
    require(iwx_replace, token, "IWX paired replacement owner")

iwx_stop = body(iwx_hal, "iwx_scan_abort(struct iwx_softc *sc, bool backgroundOnly, uint64_t reassocSerial)",
                "IWX native stopping UID wait")
for token in (
    "reserveScanCommandAbort(true, &serial, backgroundOnly, reassocSerial)",
    "iwx_umac_scan_abort_status",
    "IWX_UMAC_SCAN_ABORT_STATUS_NOT_FOUND",
    "waitScanCommandAbort(serial, generation)",
    "rejectScanCommand(serial)",
):
    require(iwx_stop, token, "IWX final scan-terminal serialization")

iwx_rx = body(iwx_hal, "iwx_rx_pkt(struct iwx_softc *sc,",
              "IWX notification reducer")
iteration = body(iwx_rx, "case IWX_SCAN_ITERATION_COMPLETE_UMAC:",
                 "IWX iteration notification")
for token in ("iwx_endscan(", "noteScanCommandTerminal("):
    forbid(iteration, token, "iteration progress treated as a final scan terminal")
complete = body(iwx_rx, "case IWX_SCAN_COMPLETE_UMAC:",
                "IWX final scan notification")
for token in ("noteScanCommandTerminal(true, le32toh(notif->uid)",
              "notif->status != IWX_SCAN_OFFLOAD_COMPLETED"):
    require(complete, token, "final UMAC identity and outcome own completion")

iwm_abort = body(iwm_scan, "iwm_bgscan_abort(struct ieee80211com *ic, uint64_t reassocSerial)",
                 "IWM reassoc scan abort")
for token in ("return EINVAL", "iwm_scan_abort(sc, true, reassocSerial)"):
    require(iwm_abort, token, "IWM lower abort ownership")
require(iwm_mac, "ic->ic_bgscan_abort = iwm_bgscan_abort",
        "IWM lower abort hook publication")
iwm_replace = body(iwm_scan, "iwm_scan(struct iwm_softc *sc, const ItlStateTransitionRequest &request)",
                   "IWM foreground replacement")
for token in ("ic_wcl_reassoc_owner_active",
              "ieee80211_cancel_wcl_reassoc_bgscan(ic, ECANCELED)",
              "scanCommandBackgroundPending()",
              "iwm_umac_scan(sc, 0, scanSerial)"):
    require(iwm_replace, token, "IWM paired replacement owner")

iwm_stop = body(iwm_scan, "iwm_scan_abort(struct iwm_softc *sc, bool backgroundOnly, uint64_t reassocSerial)",
                "IWM native stopping UID wait")
for token in (
    "reserveScanCommandAbort(true, &serial, backgroundOnly, reassocSerial)",
    "iwm_umac_scan_abort_status",
    "IWM_UMAC_SCAN_ABORT_STATUS_NOT_FOUND",
    "waitScanCommandAbort(serial, generation)",
    "rejectScanCommand(serial)",
):
    require(iwm_stop, token, "IWM final scan-terminal serialization")
require(iwm_var, "sc_scan_abort_pending", "IWM STOPPING owner storage")
for token in ("IWM_UMAC_SCAN_ABORT_STATUS_SUCCESS",
              "IWM_UMAC_SCAN_ABORT_STATUS_IN_PROGRESS",
              "IWM_UMAC_SCAN_ABORT_STATUS_NOT_FOUND"):
    require(iwm_reg, token, "IWM abort response status")
for family, hal, terminal_source, abort in (
    ("Iwm", iwm_hal, iwm_mac, iwm_abort),
    ("Iwx", iwx_hal, iwx_hal, iwx_abort),
):
    lower, upper = family.lower(), family.upper()
    forbid(abort, f"{upper}_FLAG_BGSCAN", "racy legacy-only abort precheck")
    reserve = body(hal, "reserveScanCommandAbort(bool wait, uint64_t *serial, bool backgroundOnly,",
                   f"{upper} exact abort admission")
    ordered(reserve, "kind/readiness admission before exact STOPPING owner",
            "IOSimpleLockLockDisableInterrupt(wclScanLock)",
            "backgroundOnly && !scanCommand.command.background",
            "reassocSerial != 0 && (!backgroundOnly ||",
            "scanCommand.command.reassocSerial != reassocSerial",
            "backgroundOnly && !scanCommand.upperReady",
            "scanCommand.beginAbort(current, com.sc_generation)",
            "scanCommandAbortSerial = current")
    waiter = body(hal, "waitScanCommandAbort(uint64_t serial, uint32_t generation)",
                  f"{upper} physical abort waiter")
    ordered(waiter, "matched sleep/leaf lock order", "lockTsleep()",
            "IOSimpleLockLockDisableInterrupt(wclScanLock)",
            "scanCommandAbortSerial == serial", "tsleep_nsec_locked",
            "SEC_TO_NSEC(1)", "scanCommandAbortSerial == serial",
            "unlockTsleep()", "rejectScanCommand(serial)")
    terminal = body(terminal_source,
                    f"{lower}_endscan(struct {lower}_softc *sc, uint64_t serial)",
                    f"{upper} final terminal reducer")
    ordered(terminal, "physical retirement before upper callback",
            "claimScanCommandTerminal(serial", "if (physical.stopping)",
            "ieee80211_end_scan")
    forbid(terminal, "sc_scan_abort_pending", "post-callback waiter mutation")
    claim = body(hal, "claimScanCommandTerminal(uint64_t serial,",
                 f"{upper} exact physical claim")
    for token in ("scanCommand.claimTerminal(serial, com.sc_generation, physical)",
                  "scanCommandAbortSerial == serial", "sc_scan_abort_pending",
                  f"{upper}_FLAG_SCANNING | {upper}_FLAG_BGSCAN",
                  "physical->stopping = true", "wakeupOn"):
        require(claim, token, "exact physical retirement and waiter delivery")

iwm_reduce = body(iwm_rx, "iwm_rx_pkt(struct iwm_softc *sc,",
                  "IWM notification reducer")
for marker in ("case IWM_SCAN_ITERATION_COMPLETE:",
               "case IWM_SCAN_ITERATION_COMPLETE_UMAC:"):
    iteration = body(iwm_reduce, marker, "IWM iteration notification")
    for token in ("iwm_endscan(", "noteScanCommandTerminal("):
        forbid(iteration, token, "IWM iteration progress treated as final")
for marker in ("case IWM_SCAN_OFFLOAD_COMPLETE:",
               "case IWM_SCAN_COMPLETE_UMAC:"):
    complete = body(iwm_reduce, marker, "IWM final scan notification")
    require(complete, "noteScanCommandTerminal(",
            "IWM final notification owns scan completion")
    require(complete, "notif->status != IWM_SCAN_OFFLOAD_COMPLETED",
            "IWM aborted census cannot become success")
    require(complete, "le32toh(notif->uid)" if "UMAC" in marker else "false, 0",
            "exact UMAC UID or serialized LMAC terminal")

selector = body(core, "ieee80211_wcl_reassoc_candidate_disposition(",
                "WCL candidate filter")
for token in (
    "ic_wcl_reassoc_source_bssid",
    "request->channel_count",
    "request->candidate_count",
    "request->prune_rssi_dbm",
):
    require(selector, token, "bounded Apple candidate policy")

matcher = body(node, "ieee80211_match_bss(", "BSS admission")
for token in (
    "wcl_target = bgscan && ic->ic_wcl_reassoc_owner_active",
    "ieee80211_wcl_reassoc_candidate_disposition(ic, ni) > 0",
    "wnm_target != 1 && !wcl_target",
):
    require(matcher, token, "firmware-roam DESBSSID bypass")

completion = body(node, "void\nieee80211_end_scan_owned(",
                  "scan completion")
for token in (
    "wcl_reassoc_scan",
    "IEEE80211_WCL_REASSOC_OWNER_LEAF_SCAN_FAILED",
    "ieee80211_wcl_reassoc_prepare(ic, reassoc_serial, selbs)",
    "ic_sae_wcl_roam_start",
    "ieee80211_node_defer_bss_switch(ic, source, selbs,",
):
    require(completion, token, "real target switch path")

# The target-stage transition moved from an inline assignment in scan
# completion into the shared common helper. It must still copy the real
# selected target and advance the owner to ROAM_STARTED under the lock.
prepare = body(core, "ieee80211_wcl_reassoc_prepare(",
               "common target-stage preparation")
for token in (
    "IEEE80211_ADDR_COPY(ic->ic_wcl_reassoc_target_bssid, target->ni_bssid)",
    "IEEE80211_WCL_REASSOC_STAGE_PREP",
    "IEEE80211_WCL_REASSOC_OWNER_LEAF_ROAM_STARTED",
):
    require(prepare, token, "target selection transitions the shared owner")

success = body(core, "ieee80211_wcl_reassoc_target_running(",
               "target completion gate")
for token in (
    "IEEE80211_WCL_REASSOC_OWNER_LEAF_ROAM_STARTED",
    "ic->ic_state != IEEE80211_S_RUN",
    "ic_wcl_reassoc_target_bssid",
    "ieee80211_wcl_reassoc_post_success(ic)",
):
    require(success, token, "post-roam target-RUN completion")
forbid(success, "ni->ni_port_valid",
       "reassociation terminal delayed behind RSN port validity")
newstate_target = section(proto, "int\nieee80211_newstate(",
                          "\nvoid\nieee80211_set_link_state(",
                          "generic net80211 state transition")
run_terminal = newstate_target.find(
    "ieee80211_wcl_reassoc_target_running(ic, ni);")
run_pae = newstate_target.find("sae_wcl_defer_link_up =", run_terminal)
if run_terminal < 0 or run_pae < 0 or run_terminal >= run_pae:
    fail("reassociation terminal must precede local PAE link policy")

failure = body(core, "u_int64_t\nieee80211_wcl_reassoc_post_failure_owned(",
               "WCL async failure")
require(failure, "LEAF_SCAN_FAILED", "source-preserving no-target failure")
require(failure, "ieee80211_pae_assoc_epoch_begin_reassoc(",
        "post-switch failure epoch fence")

admit = body(proto,
    "ieee80211_sae_wcl_request_admit_cached_roam_candidate(",
    "cached SAE roam admission")
for token in (
    "ic_wcl_reassoc_owner_active",
    "IEEE80211_WCL_REASSOC_OWNER_LEAF_ROAM_STARTED",
    "ic_wcl_reassoc_target_bssid",
    "IEEE80211_SAE_WCL_REQUEST_SCAN_ISSUED",
):
    require(admit, token, "fresh WCL-selected SAE target proof")

targeted = body(iwn, "iwn_sae_targeted_roam_start(",
                "IWN targeted SAE roam")
for token in (
    "sc_sae_wcl_credential_active",
    "ieee80211_sae_wcl_request_retarget_run",
    "ieee80211_sae_wcl_request_rollback_run_retarget",
    "stageSaeWclCredential",
    "LOWER_RETARGET_ACCEPTED",
    "ieee80211_node_join_bss",
    'consume_wnm ? "BTM" : "WCL"',
):
    require(targeted, token, "driver-resident SAE retarget")
forbid(targeted, "IEEE80211_NEWSTATE_ARG_WNM_RECONNECT_HOLD",
       "speculative IWN RUN-to-SCAN retarget")
require(iwn, "ic->ic_sae_wcl_roam_start = ItlIwn::iwn_sae_wcl_roam_start",
        "IWN hook publication")
require(iwn, "ic->ic_sae_wcl_roam_start = NULL",
        "IWN hook teardown")
targeted_iwm = body(iwm, "iwm_sae_targeted_roam_start(",
                    "IWM targeted SAE roam")
for token in (
    "sc_sae_wcl_credential_active",
    "ieee80211_sae_wcl_request_retarget_run",
    "ieee80211_sae_wcl_request_rollback_run_retarget",
    "stageSaeWclCredential",
    "LOWER_RETARGET_ACCEPTED",
    "ieee80211_node_join_bss",
    'consume_wnm ? "BTM" : "WCL"',
):
    require(targeted_iwm, token, "IWM driver-resident SAE retarget")
forbid(targeted_iwm, "IEEE80211_NEWSTATE_ARG_WNM_RECONNECT_HOLD",
       "speculative IWM RUN-to-SCAN retarget")
require(iwm, "ic->ic_sae_wcl_roam_start = ItlIwm::iwm_sae_wcl_roam_start",
        "IWM hook publication")
require(iwm, "ic->ic_sae_wcl_roam_start = NULL",
        "IWM hook teardown")
targeted_iwx = body(iwx, "iwx_sae_targeted_roam_start(",
                    "IWX targeted SAE roam")
for token in (
    "sc_sae_wcl_credential_active",
    "ieee80211_sae_wcl_request_retarget_run",
    "ieee80211_sae_wcl_request_rollback_run_retarget",
    "stageSaeWclCredential",
    "LOWER_RETARGET_ACCEPTED",
    "ieee80211_node_join_bss",
    'consume_wnm ? "BTM" : "WCL"',
):
    require(targeted_iwx, token, "IWX driver-resident SAE retarget")
forbid(targeted_iwx, "IEEE80211_NEWSTATE_ARG_WNM_RECONNECT_HOLD",
       "speculative asynchronous IWX RUN-to-SCAN retarget")
require(iwx, "ic->ic_sae_wcl_roam_start = ItlIwx::iwx_sae_wcl_roam_start",
        "IWX hook publication")
require(iwx, "ic->ic_sae_wcl_roam_start = NULL",
        "IWX hook teardown")
require(var, "ic_sae_wcl_roam_start", "common optional SAE roam hook")

run_retarget = body(proto,
    "ieee80211_sae_wcl_request_retarget_run(",
    "source-preserving SAE RUN retarget")
for token in (
    "ieee80211_sae_wcl_request_run_is_stable_locked",
    "request->generation != source_generation",
    "IEEE80211_WCL_REASSOC_OWNER_LEAF_ROAM_STARTED",
    "transition->candidate_confirmed",
    "IEEE80211_SAE_WCL_REQUEST_RUN_RETARGET_ISSUED",
    "ic->ic_sae_wcl_policy_generation = generation",
    "IEEE80211_ADDR_COPY(ic->ic_des_bssid, target_bssid)",
):
    require(run_retarget, token, "transactional RUN retarget")
forbid(run_retarget, "ieee80211_new_state(",
       "state transition before lower retarget acceptance")

rollback = body(proto,
    "ieee80211_sae_wcl_request_rollback_run_retarget(",
    "source-preserving SAE RUN rollback")
for token in (
    "source_generation >= generation",
    "ieee80211_sae_wcl_request_run_is_stable_locked",
    "request->phase = IEEE80211_SAE_WCL_REQUEST_BOUND",
    "ic->ic_sae_wcl_policy_generation = source_generation",
    "IEEE80211_ADDR_COPY(ic->ic_des_bssid, source->ni_bssid)",
):
    require(rollback, token, "failed lower retarget rollback")

bind = body(proto, "ieee80211_sae_wcl_request_bind_selected_bss(",
            "selected-BSS request bind")
for token in ("ic->ic_state == IEEE80211_S_RUN",
              "ieee80211_sae_wcl_request_run_retarget_issued_locked"):
    require(bind, token, "RUN retarget controlled replacement bind")

replacement = body(proto,
    "ieee80211_pae_assoc_epoch_begin_replacement(",
    "accepted reassociation BSS replacement")
require(replacement,
    "ieee80211_sae_wcl_pmk_claim_retire_replacement_locked(ic, prior_epoch)",
    "source PMK claim retirement after lower retarget acceptance")

newstate = section(proto, "int\nieee80211_newstate(",
                   "\nvoid\nieee80211_set_link_state(",
                   "generic net80211 state transition")
for token in (
    "sae_wcl_defer_link_up =",
    "ni->ni_port_valid == 0",
    "ieee80211_sae_wcl_request_bound_current(ic, ni)",
    "ieee80211_public_initial_bssid_pin_should_defer_link_up(",
    "!sae_wcl_defer_link_up",
    "sae_wcl LINK_UP_DEFERRED_UNTIL_PORT_VALID",
):
    require(newstate, token, "direct-SAE pre-port link-up fence")

msg3 = body(pae, "void\nieee80211_recv_4way_msg3(",
            "STA four-way Msg3 terminal")
for token in (
    "ni->ni_port_valid = 1;",
    "ieee80211_set_link_state(ic, LINK_STATE_UP);",
    "IEEE80211_EVT_STA_RSN_HANDSHAKE_DONE",
):
    require(msg3, token, "port-valid link release")

print("PASS: Tahoe WCL reassoc uses a real bounded roam scan, paired IWN/IWM/IWX SAE retarget, and target-RUN reassociation publication before RSN key completion")
PY
