#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

python3 - "$ROOT" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
sky = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()
v2 = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
core = (root / "itl80211/openbsd/net80211/ieee80211.c").read_text()
var = (root / "itl80211/openbsd/net80211/ieee80211_var.h").read_text()
iwx = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()
iwm = (root / "itlwm/hal_iwm/scan.cpp").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
policy = (root / "include/HAL/ItlScanCommandPolicy.hpp").read_text()
iwm_hal = (root / "itlwm/hal_iwm/ItlIwm.cpp").read_text()
iwm_sender = (root / "itlwm/hal_iwm/phy.cpp").read_text()


def fail(message):
    raise SystemExit(f"Tahoe exact WCL scan-plan contract: {message}")


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


prefix = body(sky, "struct __attribute__((packed)) TahoeWclScanRequestPrefix",
              "25C56 WCL scan carrier prefix")
for token in (
        "uint8_t opaque00[0x1c]",
        "uint32_t ssidLength",
        "uint8_t ssid[IEEE80211_NWID_LEN]",
        "uint32_t scanType",
        "uint32_t activeTime",
        "uint32_t passiveTime",
        "uint32_t homeTime",
        "uint32_t channelCount",
):
    require(prefix, token, "reference-proven carrier field")
for token in (
        "sizeof(TahoeWclScanRequestPrefix) == 0x58",
        "ssidLength) == 0x1c",
        "ssid) == 0x20",
        "scanType) == 0x40",
        "activeTime) == 0x48",
        "passiveTime) == 0x4c",
        "homeTime) == 0x50",
        "channelCount) == 0x54",
        "sizeof(struct apple80211_channel) == 0x0c",
):
    require(sky, token, "carrier ABI assertion")

plan_decl = body(var, "struct ieee80211_wcl_scan_plan",
                 "common immutable scan plan")
for token in (
        "u_int64_t\tgeneration",
        "u_int32_t\trequested_channel_count",
        "u_int32_t\tactive_dwell_ms",
        "u_int32_t\tpassive_dwell_ms",
        "u_int32_t\thome_dwell_ms",
        "u_int8_t\tssid_len",
        "u_int8_t\tscan_type",
        "u_int8_t\tchannel_filter",
        "u_int8_t\tactive",
        "channel_any[IEEE80211_WCL_SCAN_BITMAP_BYTES]",
        "channel_2ghz[IEEE80211_WCL_SCAN_BITMAP_BYTES]",
        "channel_5ghz[IEEE80211_WCL_SCAN_BITMAP_BYTES]",
):
    require(plan_decl, token, "bounded shared plan field")
for token in (
        "IEEE80211_WCL_SCAN_REQUEST_MAX_CHANNELS 400",
        "IEEE80211_WCL_SCAN_TYPE_PASSIVE 2",
        "IEEE80211_WCL_SCAN_DWELL_MAX_MS 255",
        "IEEE80211_WCL_SCAN_HOME_MAX_MS 1000",
        "struct ieee80211_wcl_scan_plan ic_wcl_scan_plan",
):
    require(var, token, "shared WCL scan declaration")

builder = body(sky, "tahoeBuildWclScanPlan(", "carrier normalizer")
ordered(builder, "bounded carrier validation",
        "prefix.ssidLength > IEEE80211_NWID_LEN",
        "prefix.channelCount > IEEE80211_WCL_SCAN_REQUEST_MAX_CHANNELS",
        "prefix.activeTime > IEEE80211_WCL_SCAN_DWELL_MAX_MS",
        "prefix.passiveTime > IEEE80211_WCL_SCAN_DWELL_MAX_MS",
        "prefix.homeTime > IEEE80211_WCL_SCAN_HOME_MAX_MS")
ordered(builder, "exact channel-list publication",
        "if (prefix.channelCount == 0)",
        "plan->channel_filter = 1",
        "raw + index * sizeof(channel)",
        "channel.channel == 0 || channel.channel > IEEE80211_CHAN_MAX",
        "setbit(plan->channel_any, channel.channel)",
        "admittedChannels != 0 ? kIOReturnSuccess : kIOReturnBadArgument")
for token in (
        "setbit(plan->channel_2ghz, channel.channel)",
        "setbit(plan->channel_5ghz, channel.channel)",
        "plan->ssid_len = static_cast<uint8_t>(prefix.ssidLength)",
        "plan->scan_type = static_cast<uint8_t>(prefix.scanType)",
        "plan->active_dwell_ms = prefix.activeTime",
        "plan->passive_dwell_ms = prefix.passiveTime",
        "plan->home_dwell_ms = prefix.homeTime",
):
    require(builder, token, "normalized request fact")

producer = body(sky, "setWCL_SCAN_REQ(apple80211ScanRequest *req)",
                "WCL ScanAdapter producer")
ordered(producer, "plan precedes lower radio ownership",
        "tahoeBuildWclScanPlan(req, &scanPlan)",
        "instance->reserveWclPhysicalScan(",
        "scanPlan.generation = generation",
        "ieee80211_wcl_scan_plan_stage(ic, &scanPlan)",
        "airportItlwmBeginWclScanAfterRoam")
if producer.count("ieee80211_wcl_scan_plan_clear(ic, generation)") < 3:
    fail("producer does not clear the exact generation on every post-stage start failure")

stage = body(core, "ieee80211_wcl_scan_plan_stage(", "plan publisher")
ordered(stage, "publish-last protocol",
        "IOSimpleLockLockDisableInterrupt(ic->ic_pae_selected_bss_lock)",
        "__atomic_load_n(&plan->active, __ATOMIC_ACQUIRE)",
        "memcpy(plan, source, sizeof(*plan))",
        "plan->active = 0",
        "__atomic_thread_fence(__ATOMIC_RELEASE)",
        "__atomic_store_n(&plan->active, 1, __ATOMIC_RELEASE)",
        "IOSimpleLockUnlockEnableInterrupt(ic->ic_pae_selected_bss_lock, irq)")

snapshot = body(core, "ieee80211_wcl_scan_plan_snapshot(",
                "immutable plan reader")
ordered(snapshot, "leaf-serialized complete snapshot",
        "IOSimpleLockLockDisableInterrupt(ic->ic_pae_selected_bss_lock)",
        "__atomic_load_n(&plan->active, __ATOMIC_ACQUIRE)",
        "plan->generation != 0",
        "memcpy(snapshot, plan, sizeof(*snapshot))",
        "snapshot->active = 1",
        "IOSimpleLockUnlockEnableInterrupt(ic->ic_pae_selected_bss_lock, irq)")

allowed = body(core, "ieee80211_wcl_scan_plan_channel_allowed(",
               "common exact channel predicate")
for token in (
        "plan->channel_filter == 0",
        "isset(plan->channel_any, channel_number)",
        "IEEE80211_IS_CHAN_2GHZ(channel)",
        "isset(plan->channel_2ghz, channel_number)",
        "IEEE80211_IS_CHAN_5GHZ(channel)",
        "isset(plan->channel_5ghz, channel_number)",
):
    require(allowed, token, "channel admission rule")

clear = body(core, "ieee80211_wcl_scan_plan_clear(",
             "generation-scoped plan retirement")
ordered(clear, "generation-matched clear",
        "IOSimpleLockLockDisableInterrupt(ic->ic_pae_selected_bss_lock)",
        "generation != 0 && plan->generation != generation",
        "__atomic_store_n(&plan->active, 0, __ATOMIC_RELEASE)",
        "IOSimpleLockUnlockEnableInterrupt(ic->ic_pae_selected_bss_lock, irq)")
require(core, "memset(&ic->ic_wcl_scan_plan, 0, sizeof(ic->ic_wcl_scan_plan))",
        "attach initialization")
require(core, "ieee80211_wcl_scan_plan_clear(ic, 0)",
        "detach retirement")

invalidate_all = body(v2, "void AirportItlwm::invalidateWclPhysicalScan()",
                      "broad WCL invalidation")
require(invalidate_all, "ieee80211_wcl_scan_plan_clear(",
        "broad lifecycle plan retirement")
invalidate_exact = body(v2,
    "void AirportItlwm::invalidateWclPhysicalScan(uint64_t generation,",
    "exact WCL invalidation")
ordered(invalidate_exact, "successful exact invalidation retirement",
        "invalidated = true",
        "if (invalidated && fHalService != nullptr)",
        "ieee80211_wcl_scan_plan_clear(")
events = body(v2, "eventHandler(struct ieee80211com *ic, int msgCode, void *data)",
              "WCL event reducer")
for marker in ("IEEE80211_EVT_WCL_SCAN_START_REJECTED",
               "IEEE80211_EVT_WCL_SCAN_TERMINAL"):
    start = events.find(marker)
    if start < 0:
        fail(f"missing lifecycle event: {marker}")
    window = events[start:start + 1800]
    require(window, "ieee80211_wcl_scan_plan_clear(",
            f"plan retirement after {marker}")


def require_hal_plan(text, function_marker, label, zero_count_token):
    scan = body(text, function_marker, label)
    for token in (
            "const struct ieee80211_wcl_scan_plan &wclPlan = policy.plan",
            "wclPlan.ssid_len",
            "wclPlan.ssid",
            "wclPlan.scan_type != IEEE80211_WCL_SCAN_TYPE_PASSIVE",
            "wclPlan.active_dwell_ms",
            "wclPlan.passive_dwell_ms",
            "wclPlan.home_dwell_ms",
            "const uint32_t homeAwayMs = policy.homeAwayMs",
            "activeScan ? 1 : 0, bgscan, &wclPlan",
            zero_count_token,
            "return EINVAL",
    ):
        require(scan, token, f"{label} exact-plan consumption")
    forbid(scan, "ieee80211_wcl_scan_plan_snapshot", f"{label} policy reread")
    forbid(scan, "ic->ic_des_essid", f"{label} borrowed mutable SSID")


def require_active_wildcard(scan, label, passive_flag):
    ordered(scan, f"{label} active-wildcard mode",
            "const bool activeScan = exactWclPlan ?",
            "wclPlan.scan_type != IEEE80211_WCL_SCAN_TYPE_PASSIVE",
            "const bool directedSsid = activeScan && scanSsidLength != 0")
    require(scan, "if (!activeScan)",
            f"{label} passive mode independent of SSID")
    require(scan, passive_flag,
            f"{label} firmware passive flag")
    require(scan, "if (directedSsid)",
            f"{label} directed selector independent of active wildcard")


def require_intel_probe_selector(scan, label, *tokens):
    for token in (
            "if (activeScan)",
            "direct_scan[0].id = IEEE80211_ELEMID_SSID",
            "direct_scan[0].len = scanSsidLength",
            "activeScan ? 1 : 0",
            *tokens,
    ):
        require(scan, token, f"{label} active wildcard SSID-zero encoding")


iwx_channels = body(iwx, "iwx_umac_scan_fill_channels(",
                    "IWX channel builder")
for token in (
        "plan != NULL && plan->active != 0",
        "ieee80211_wcl_scan_plan_channel_allowed(ic, plan, c)",
        "const bool activeProbe = exactWclPlan ?",
        "plan->scan_type != IEEE80211_WCL_SCAN_TYPE_PASSIVE",
        "if (activeProbe)",
):
    require(iwx_channels, token, "IWX exact channel admission")
forbid(iwx_channels, "ieee80211_wcl_scan_plan_snapshot", "IWX nested policy reread")
for marker, label, zero_count, passive_flag, selector_tokens in (
        ("iwx_umac_scan(struct iwx_softc *sc, int bgscan, uint64_t scan_serial)",
         "IWX legacy UMAC scan", "chanparam->count == 0",
         "IWX_UMAC_SCAN_GEN_FLAGS_PASSIVE", ()),
        ("iwx_umac_scan_v12(struct iwx_softc *sc, int bgscan, uint64_t scan_serial,",
         "IWX v12 scan", "cp->count == 0",
         "IWX_UMAC_SCAN_GEN_FLAGS_V2_FORCE_PASSIVE",
         ("probe_params.ssid_num = 1", "probe_params.ssid_num = 0")),
        ("iwx_umac_scan_v14(struct iwx_softc *sc, int bgscan, uint64_t scan_serial,",
         "IWX v14 scan", "cp->count == 0",
         "IWX_UMAC_SCAN_GEN_FLAGS_V2_FORCE_PASSIVE", ()),
):
    require_hal_plan(iwx, marker, label, zero_count)
    scan = body(iwx, marker, label)
    require_active_wildcard(scan, label, passive_flag)
    require_intel_probe_selector(scan, label, *selector_tokens)

for marker, label in (
        ("iwm_lmac_scan_fill_channels(", "IWM LMAC channel builder"),
        ("iwm_umac_scan_fill_channels(", "IWM UMAC channel builder"),
):
    channels = body(iwm, marker, label)
    for token in (
            "plan != NULL && plan->active != 0",
            "ieee80211_wcl_scan_plan_channel_allowed(ic, plan, c)",
            "const bool activeProbe = exactWclPlan ?",
            "plan->scan_type != IEEE80211_WCL_SCAN_TYPE_PASSIVE",
            "if (activeProbe)",
    ):
        require(channels, token, "IWM exact channel admission")
    forbid(channels, "ieee80211_wcl_scan_plan_snapshot", "IWM nested policy reread")
for marker, label, zero_count, passive_flag in (
        ("iwm_lmac_scan(struct iwm_softc *sc, int bgscan, uint64_t scan_serial)",
         "IWM LMAC scan", "req->n_channels == 0",
         "IWM_LMAC_SCAN_FLAG_PASSIVE"),
        ("iwm_umac_scan(struct iwm_softc *sc, int bgscan, uint64_t scan_serial)",
         "IWM UMAC scan", "chanparam->count == 0",
         "IWM_UMAC_SCAN_GEN_FLAGS_PASSIVE"),
):
    require_hal_plan(iwm, marker, label, zero_count)
    scan = body(iwm, marker, label)
    require_active_wildcard(scan, label, passive_flag)
    require_intel_probe_selector(scan, label)

capture = body(policy, "static int captureOwnedLocked(", "physical-owner policy capture")
ordered(capture, "missing matching WCL policy rejects, never widens",
        "request->identity.equals(policy->identity)",
        "request->scanGeneration != wclGeneration",
        "if (wclGeneration != 0)",
        "plan.active == 0 || plan.generation != wclGeneration",
        "return ECANCELED", "policy->plan = plan")
require(capture, "memcpy(policy->plan.ssid, request->scanSsid, sizeof(policy->plan.ssid))",
        "foreground SSID from copied ingress")
require(capture, "policy->joinGeneration = request->scanJoinGeneration",
        "fresh-join role retained from ingress, not inferred at allocation")
for hal, sender, prefix in ((iwm_hal, iwm_sender, "iwm"), (iwx, iwx, "iwx")):
    reserve = body(hal, "reserveScanCommand(bool", "physical admission")
    ordered(reserve, "selected owner before scan leaf, then capture and reserve",
            "IOSimpleLockLockDisableInterrupt(ownerLock)",
            "IOSimpleLockLockDisableInterrupt(wclScanLock)",
            "stateTransition.current(*request, com.sc_generation)",
            "ItlScanCommandPolicy::captureOwnedLocked(ic, wclGeneration,",
            "scanCommand.reserve(", "scanCommandPolicy = policy")
    prepare = body(hal, "prepareStateTransition(int", "queued scan ingress")
    ordered(prepare, "ingress value under selected and scan leaves",
            "IOSimpleLockLockDisableInterrupt(ownerLock)",
            "IOSimpleLockLockDisableInterrupt(wclScanLock)",
            "ItlScanCommandPolicy::identityLocked(",
            "ItlScanCommandPolicy::captureIngressLocked(",
            "stateTransition.prepare(", "stateTransition.request = *request")
    submit = body(sender, f"\n{prefix}_send_cmd(struct", "real firmware sender")
    ordered(submit, "same owner checked before physical receipt and doorbell",
            "IOSimpleLockLockDisableInterrupt(owner_lock)",
            "IOSimpleLockLockDisableInterrupt(wclScanLock)",
            "scan_request && !scanCommandOwnerCurrentLocked(",
            "scanCommand.submitAbort(", "scanCommand.submit(",
            f"{prefix.upper()}_WRITE(sc, {prefix.upper()}_HBUS_TARG_WRPTR")
    defer = body(hal, "deferScanCommand(const", "exact deferred ingress")
    require(defer, "stateTransition.current(request, com.sc_generation)", "no successor resampling")
    forbid(defer, "ieee80211_wcl_join_copy_current", "newest join substituted for queued owner")
    ordered(defer, "defer exact request then level-check a concurrent release",
            "stateTransition.defer(request, com.sc_generation)",
            "IOSimpleLockUnlockEnableInterrupt(ownerLock",
            "resumeScanCommand()")
    replay = body(hal, "\nresumeScanCommand()\n", "exact deferred replay")
    pin = ("iwm_sae_tx_lifecycle_enter" if prefix == "iwm" else
           "iwx_task_gate_enter")
    unpin = ("iwm_sae_tx_lifecycle_leave" if prefix == "iwm" else
             "iwx_task_gate_leave")
    ordered(replay, "pin, validate and claim exact replay before scheduling",
            pin, "IOSimpleLockLockDisableInterrupt(ownerLock)",
            "IOSimpleLockLockDisableInterrupt(wclScanLock)",
            "ItlStateTransitionLease::Stage::Deferred",
            "ItlScanCommandPolicy::captureOwnedLocked(",
            "stateTransition.resume(com.sc_generation)",
            "IOSimpleLockUnlockEnableInterrupt(ownerLock",
            f"{prefix}_add_task(", unpin)
    for token in ("ieee80211_begin_scan", "stateTransition.prepare(",
                  "prepareStateTransition(", "ieee80211_new_state("):
        forbid(replay, token, "replay must not create another common/state request")
for text, marker in ((iwm, "iwm_lmac_scan(struct"),
                     (iwm, "iwm_umac_scan(struct"),
                     (iwx, "iwx_umac_scan(struct")):
    scan = body(text, marker, "top-level Intel scan")
    ordered(scan, "reserved policy precedes command allocation",
            "copyScanCommandPolicy(scan_serial, &policy)",
            "return ECANCELED", "malloc(req_len,")
    forbid(scan, "wclScanPhase", "mutable upper phase chosen by old builder")
dispatch = body(iwx, "iwx_umac_scan(struct", "IWX scan-version dispatcher")
for version in (12, 14):
    require(dispatch, f"iwx_umac_scan_v{version}(sc, bgscan, scan_serial, policy)",
            "immutable policy through firmware version dispatch")

iwn_scan = body(iwn, "iwn_scan_submit(struct iwn_softc *sc, uint16_t flags, int bgscan,",
                "IWN scan command")
for token in (
        "const bool exactWclPlan = wcl_scan &&",
        "ieee80211_wcl_scan_plan_snapshot(ic, &wclPlan)",
        "wclPlan.ssid_len",
        "wclPlan.ssid",
        "wclPlan.scan_type != IEEE80211_WCL_SCAN_TYPE_PASSIVE",
        "wclPlan.active_dwell_ms",
        "wclPlan.passive_dwell_ms",
        "wclPlan.home_dwell_ms",
        "ieee80211_wcl_scan_plan_channel_allowed(ic, &wclPlan, c)",
        "is_active = activeScan ? 1 : 0",
        "ieee80211_add_ssid(frm, NULL, 0)",
        "directedSsid && (flags & IEEE80211_CHAN_5GHZ)",
):
    require(iwn_scan, token, "IWN exact-plan consumption")
ordered(iwn_scan, "IWN active-wildcard mode",
        "const bool activeScan = exactWclPlan ?",
        "wclPlan.scan_type != IEEE80211_WCL_SCAN_TYPE_PASSIVE",
        "const bool directedSsid = activeScan && scanSsidLength != 0",
        "is_active = activeScan ? 1 : 0")

iwn_band_eligible = body(iwn, "iwn_wcl_scan_plan_has_eligible_band(",
                         "IWN exact-plan band selector")
for token in (
        "ieee80211_wcl_scan_plan_snapshot(ic, &plan)",
        "plan.channel_filter == 0",
        "(channel->ic_flags & flags) != flags",
        "ieee80211_wcl_scan_plan_channel_allowed(ic, &plan, channel)",
        "explicit_bzero(&plan, sizeof(plan))",
):
    require(iwn_band_eligible, token, "IWN exact-plan band eligibility")

iwn_initial_band = body(iwn, "static int\niwn_wcl_scan_initial_band(",
                        "IWN initial WCL band choice")
ordered(iwn_initial_band, "IWN 5-GHz-only initial scan",
        "iwn_wcl_scan_plan_has_eligible_band(sc, IEEE80211_CHAN_2GHZ)",
        "IWN_FLAG_HAS_5GHZ",
        "iwn_wcl_scan_plan_has_eligible_band(sc, IEEE80211_CHAN_5GHZ)",
        "return EINVAL")

for marker, label in (
        ("beginWclInitialScan(uint64_t generation, uint32_t *outBackendGeneration)",
         "IWN initial WCL scan"),
        ("beginWclBackgroundScan(uint64_t generation, uint32_t *outBackendGeneration)",
         "IWN background WCL scan"),
):
    scan = body(iwn, marker, label)
    require(scan, "iwn_wcl_scan_initial_band(&com, &scan_flags)",
            f"{label} exact initial-band choice")
    require(scan, "iwn_scan_start(&com, scan_flags",
            f"{label} chosen-band firmware submit")

iwn_replay = body(iwn, "iwn_scan_lease_replay_task(void *arg)",
                  "IWN queued initial WCL scan")
ordered(iwn_replay, "IWN queued WCL band selection",
        "if (launch_initial)",
        "iwn_wcl_scan_initial_band(sc, &scan_flags)",
        "if (error == 0)",
        "iwn_scan_start(sc, scan_flags, initial_background ? 1 : 0",
        "initial_background ? IWN_SCAN_LEASE_WCL_BACKGROUND",
        "IWN_SCAN_LEASE_WCL_INITIAL", "initial_handoff_serial",
        "reject_initial = error != 0 && !command_started")
forbid(iwn_replay, "iwn_scan_start(sc, IEEE80211_CHAN_2GHZ",
       "queued 5-GHz request must retain the exact band's admission")

iwn_stop = body(iwn, "case IWN_STOP_SCAN:", "IWN STOP_SCAN continuation")
require(iwn_stop,
        "iwn_wcl_scan_plan_has_eligible_band(\n                    sc, IEEE80211_CHAN_5GHZ)",
        "IWN exact 2-GHz request must not submit an empty 5-GHz continuation")

for text, label in ((iwx, "IWX"), (iwm, "IWM"), (iwn, "IWN")):
    forbid(text, "activeDirected",
           f"{label} conflating active scan with directed SSID")

for text, label in ((iwx, "IWX"), (iwm, "IWM"), (iwn, "IWN")):
    forbid(text, "ieee80211_wcl_scan_plan_clear(",
           f"{label} retiring an upper-owned plan before terminal")

print("Tahoe exact WCL scan-plan contract passed")
PY
