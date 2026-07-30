#!/usr/bin/env bash
# Static contract for the private Tahoe product WCL CIPHER_PWD staging slot
# owned by IWN.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import re
import sys


root = Path(sys.argv[1])
credential_h = (root / "include/HAL/ItlSaeWclCredentialV1.h").read_text()
hal_hpp = (root / "include/HAL/ItlHalService.hpp").read_text()
iwn_hpp = (root / "itlwm/hal_iwn/ItlIwn.hpp").read_text()
iwn_cpp = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
iwn_var = (root / "itlwm/hal_iwn/if_iwnvar.h").read_text()
build = (root / "scripts/build_tahoe.sh").read_text()


def fail(message: str) -> None:
    raise SystemExit(f"IWN SAE WCL credential contract: {message}")


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
    match = pattern.search(iwn_cpp)
    if match is None:
        fail(f"missing ItlIwn::{name}()")
    return block_after(iwn_cpp, iwn_cpp.rfind("{", match.start(), match.end()),
                       f"ItlIwn::{name}")


def ordered(text: str, label: str, *needles: str) -> None:
    cursor = 0
    for needle in needles:
        position = text.find(needle, cursor)
        if position < 0:
            fail(f"{label} missing ordered token: {needle}")
        cursor = position + len(needle)


def struct_body(text: str, name: str) -> str:
    marker = f"struct {name} {{"
    start = text.find(marker)
    if start < 0:
        fail(f"missing record {name}")
    end = text.find("\n};", start)
    if end < 0:
        fail(f"unterminated record {name}")
    return text[start:end]


# The ingress record is a fixed, self-validating private copy.  The C/C++
# unit test covers individual rejection cases; pin the ABI and the absence of
# caller-owned pointer fields here so a later source change cannot turn the
# stage into a borrowed WCL buffer.
record = struct_body(credential_h, "ItlSaeWclCredentialV1")
for token in (
        "uint32_t version;", "uint32_t size;", "uint64_t request_generation;",
        "uint32_t password_len;", "uint8_t ssid_len;",
        "uint8_t bssid[kItlSaeAuthTransportV1MacLength];",
        "uint8_t ssid[kItlSaeWclCredentialV1SsidMaxLength];",
        "uint8_t password[kItlSaeWclCredentialV1PasswordStorageLength];",
        "uint8_t reserved[8];"):
    require(record, token, "fixed credential record field")
for token in ("*", "void ", "OSObject", "mbuf"):
    forbid(record, token, "borrowed/runtime credential field")
for token in (
        "sizeof(struct ItlSaeWclCredentialV1) == 136",
        "request_generation) == 8", "password_len) == 16",
        "bssid) == 24", "ssid) == 32", "password) == 64",
        "itl_sae_wcl_credential_is_well_formed",
        "credential->request_generation != 0",
        "itl_sae_wcl_credential_bssid_is_unicast_nonzero",
        "credential->ssid +\n            credential->ssid_len",
        "credential->password +\n            credential->password_len"):
    require(credential_h, token, "credential ABI/validator fence")

# Every ordinary backend remains fail-closed.  IWN alone overrides a private
# preselection staging interface; this is not an Apple80211 or UserClient ABI.
require(hal_hpp, "#include <HAL/ItlSaeWclCredentialV1.h>",
        "HAL credential type inclusion")
stage_default = body(hal_hpp, "stageSaeWclCredential",
                     "default HAL credential stage")
require(stage_default, "return kIOReturnUnsupported;", "fail-closed HAL stage")
cancel_default = body(hal_hpp, "cancelSaeWclCredential",
                      "default HAL credential cancellation")
require(cancel_default, "(void)request_generation;",
        "fail-closed HAL cancellation")
require(hal_hpp, "virtual void purgeSaeWclCredentialStage() {}",
        "HAL overflow scrub ABI")
for token in ("stageSaeWclCredential(", "cancelSaeWclCredential(",
              "purgeSaeWclCredentialStage()", "iwn_sae_wcl_stop_begin",
              "iwn_sae_wcl_detach_begin"):
    require(iwn_hpp, token, "IWN credential declaration")
for token in ("sc_sae_wcl_credential_lock", "sc_sae_wcl_credential_staged",
              "sc_sae_wcl_credential_cancel_valid",
              "sc_sae_wcl_credential_cancel_through_generation",
              "struct ItlSaeWclCredentialV1 sc_sae_wcl_credential"):
    require(iwn_var, token, "IWN one-slot credential ownership")

# Direct WCL password ingress follows the Tahoe driver-crypto target and does
# not depend on a hardware MFP bit.  The lab build switch is retained only for
# the separate diagnostic UserClient.
require(build, "IWN_SOFTWARE_PMF_LAB_BUILD=1",
        "diagnostic compiler opt-in")
runtime_gate = body(iwn_cpp, "iwn_sae_wcl_credential_runtime_opted_in",
                    "IWN WCL credential runtime gate")
ordered(runtime_gate, "credential runtime gate",
        "#if ITL_SAE_DRIVER_CRYPTO_AVAILABLE",
        "return true;", "#else", "return false;")
forbid(runtime_gate, "IEEE80211_C_MFP",
       "hardware-MFP prerequisite in runtime gate")

clear = body(iwn_cpp, "iwn_sae_wcl_credential_clear_locked",
             "IWN credential clear leaf")
ordered(clear, "credential scrub before publication", "explicit_bzero(&sc->sc_sae_wcl_credential",
        "sc->sc_sae_wcl_credential_staged = false;")

# A retry can repeat the identical canonical record, but comparison must not
# disclose where two credential records differ.  Cancellation is a monotonic
# high-water mark, so a late newer cancellation also scrubs an older staged
# password rather than leaving it available for resurrection.
equal = body(iwn_cpp, "iwn_sae_wcl_credential_equal",
             "constant-time credential equality")
for token in ("volatile uint8_t difference = 0;", "sizeof(*left)",
              "difference |= left_bytes[index] ^ right_bytes[index]",
              "return difference == 0;"):
    require(equal, token, "constant-time canonical credential comparison")
forbid(equal, "memcmp(", "early-exit credential comparison")
cancelled = body(iwn_cpp, "iwn_sae_wcl_credential_cancelled_locked",
                 "credential cancellation high-water predicate")
ordered(cancelled, "cancelled-generation predicate", "request_generation != 0",
        "sc->sc_sae_wcl_credential_cancel_valid", "request_generation <=",
        "sc->sc_sae_wcl_credential_cancel_through_generation")
cancel_through = body(iwn_cpp, "iwn_sae_wcl_credential_cancel_through_locked",
                      "credential cancellation high-water update")
ordered(cancel_through, "monotonic cancellation high-water", "request_generation == 0",
        "!sc->sc_sae_wcl_credential_cancel_valid", "request_generation >",
        "sc->sc_sae_wcl_credential_cancel_through_generation",
        "sc->sc_sae_wcl_credential_cancel_through_generation =",
        "sc->sc_sae_wcl_credential_cancel_valid = true;")
ordered(cancel_through, "late newer cancellation scrubs older staged record",
        "sc->sc_sae_wcl_credential_staged", "iwn_sae_wcl_credential_cancelled_locked(sc,",
        "sc->sc_sae_wcl_credential.request_generation",
        "iwn_sae_wcl_credential_clear_locked(sc)")
state_permitted = body(iwn_cpp, "iwn_sae_wcl_credential_stage_state_permitted",
                       "credential stage-state predicate")
for token in ("(ifp->if_flags & IFF_RUNNING) == 0",
              "ic->ic_opmode != IEEE80211_M_STA",
              "if (ic->ic_state == IEEE80211_S_SCAN)",
              "ic->ic_state == IEEE80211_S_RUN && ic->ic_bss != NULL"):
    require(state_permitted, token, "STA scan/reconnect stage fence")

require(hal_hpp, "virtual bool isSaeWclCredentialAdmissionReady() { return false; }",
        "fail-closed HAL credential admission readiness")
require(hal_hpp,
        "virtual bool reserveSaeWclCredentialAdmission() { return false; }",
        "fail-closed HAL credential admission reservation")
require(hal_hpp, "virtual void releaseSaeWclCredentialAdmission() {}",
        "fail-closed HAL credential admission release")
require(iwn_hpp, "bool isSaeWclCredentialAdmissionReady() override;",
        "IWN credential admission readiness override")
for token in ("bool reserveSaeWclCredentialAdmission() override;",
              "void releaseSaeWclCredentialAdmission() override;"):
    require(iwn_hpp, token, "IWN credential admission reservation override")
admission = body(iwn_cpp, "isSaeWclCredentialAdmissionReady",
                 "credential admission readiness")
ordered(admission, "readiness lifecycle/leaf order",
        "iwn_sae_tx_lifecycle_enter(sc, false)",
        "IOLockLock(sc->sc_sae_tx_lifecycle_lock)",
        "IOSimpleLockLock(sc->sc_scan_lease_lock)",
        "IOSimpleLockUnlock(sc->sc_scan_lease_lock)",
        "IOSimpleLockLock(sc->sc_sae_engine_lock)",
        "IOSimpleLockUnlock(sc->sc_sae_engine_lock)",
        "IOSimpleLockLock(sc->sc_sae_wcl_credential_lock)",
        "IOSimpleLockUnlock(sc->sc_sae_wcl_credential_lock)",
        "IOLockUnlock(sc->sc_sae_tx_lifecycle_lock)",
        "iwn_sae_tx_lifecycle_leave(sc)")
for token in ("iwn_sae_wcl_credential_stage_state_permitted(ic, ifp)",
              "!iwn_scan_lease_live_locked(sc)",
              "!sc->sc_wcl_initial_scan_pending.queued",
              "!sc->sc_sae_wcl_admission_reserved",
              "(sc->sc_flags & IWN_FLAG_SCANNING) == 0",
              "!sc->sc_sae_engine_owner.active",
              "sc->sc_sae_engine == NULL",
              "!sc->sc_sae_wcl_credential_staged"):
    require(admission, token, "secret-free credential admission fence")

stage = iwn_method("stageSaeWclCredential")
ordered(stage, "stage input/copy/lifecycle order",
        "if (credential == NULL)",
        "iwn_sae_wcl_credential_runtime_opted_in()",
        "explicit_bzero(&copy", "memcpy(&copy, credential, sizeof(copy))",
        "itl_sae_wcl_credential_is_well_formed(&copy)",
        "iwn_sae_tx_lifecycle_enter(sc, false)",
        "IOLockLock(sc->sc_sae_tx_lifecycle_lock)",
        "IOSimpleLockLock(sc->sc_sae_wcl_credential_lock)")
require(stage, "iwn_sae_wcl_credential_stage_state_permitted(ic, ifp)",
        "STA scan/reconnect stage admission")
ordered(stage, "cancellation-before-stage fence",
        "iwn_sae_wcl_credential_cancelled_locked(sc,\n                copy.request_generation)",
        "rc = kIOReturnAborted;", "!sc->sc_sae_wcl_credential_staged",
        "sc->sc_sae_wcl_credential = copy;",
        "sc->sc_sae_wcl_credential_staged = true;")
ordered(stage, "idempotent fixed-record restage", "else if (sc->sc_sae_wcl_credential.request_generation ==",
        "copy.request_generation)", "iwn_sae_wcl_credential_equal(",
        "&sc->sc_sae_wcl_credential, &copy)", "rc = kIOReturnSuccess;")
ordered(stage, "conflicting same-generation retry retirement",
        "iwn_sae_wcl_credential_cancel_through_locked(sc,",
        "copy.request_generation)", "rc = kIOReturnAborted;")
ordered(stage, "newer-generation atomic replacement", "copy.request_generation >",
        "sc->sc_sae_wcl_credential.request_generation",
        "iwn_sae_wcl_credential_cancel_through_locked(sc,",
        "sc->sc_sae_wcl_credential.request_generation);",
        "!sc->sc_sae_wcl_credential_staged",
        "!iwn_sae_wcl_credential_cancelled_locked(sc,",
        "copy.request_generation)", "sc->sc_sae_wcl_credential = copy;",
        "sc->sc_sae_wcl_credential_staged = true;", "rc = kIOReturnSuccess;")
if stage.count("sc->sc_sae_wcl_credential = copy;") != 2:
    fail("staging lacks exactly the initial and newer-generation replacement writes")
ordered(stage, "stage release and local scrub", "IOSimpleLockUnlock(sc->sc_sae_wcl_credential_lock)",
        "IOLockUnlock(sc->sc_sae_tx_lifecycle_lock)",
        "iwn_sae_tx_lifecycle_leave(sc)", "out:", "explicit_bzero(&copy",
        "return rc;")
for token in ("submitSaeAuthFrame", "cancelSaeAuthFrame", "iwn_tx(",
              "ieee80211_sae_auth_frame_build", "ic_event_handler",
              "IOCommandGate", "fSaeTxGate", "IEEE80211_C_MFP"):
    forbid(stage, token, "TX/controller/MFP reentry from credential stage")
forbid(stage, "sc->sc_sae_wcl_credential = credential",
       "borrowed credential-pointer retention")

cancel = iwn_method("cancelSaeWclCredential")
ordered(cancel, "cancel validation/lifecycle order", "request_generation == 0",
        "iwn_sae_wcl_credential_runtime_opted_in()",
        "iwn_sae_tx_lifecycle_enter(sc, true)",
        "IOSimpleLockLock(sc->sc_sae_wcl_credential_lock)",
        "iwn_sae_wcl_credential_cancel_through_locked(sc,",
        "request_generation)")
for token in ("credential->", "memcpy(", "submitSaeAuthFrame", "iwn_tx(",
              "ic_event_handler"):
    forbid(cancel, token, "secret/TX route in cancellation")

purge = iwn_method("purgeSaeWclCredentialStage")
ordered(purge, "overflow scrub order", "iwn_sae_tx_lifecycle_enter(sc, true)",
        "IOSimpleLockLock(sc->sc_sae_wcl_credential_lock)",
        "iwn_sae_wcl_credential_clear_locked(sc)",
        "IOSimpleLockUnlock(sc->sc_sae_wcl_credential_lock)",
        "iwn_sae_tx_lifecycle_leave(sc)")
for token in ("sc_sae_wcl_credential_cancel_through_generation", "request_generation"):
    forbid(purge, token, "untrusted-generation retention on overflow")

stop = iwn_method("iwn_sae_wcl_stop_begin")
ordered(stop, "stop closes then scrubs credential", "iwn_sae_tx_lifecycle_close(sc, false)",
        "IOSimpleLockLock(sc->sc_scan_lease_lock)",
        "sc->sc_sae_wcl_admission_reserved = false;",
        "IOSimpleLockUnlock(sc->sc_scan_lease_lock)",
        "IOSimpleLockLock(sc->sc_sae_wcl_credential_lock)",
        "sc->sc_sae_wcl_credential_staged", "generation = sc->sc_sae_wcl_credential.request_generation",
        "if (generation != 0)", "iwn_sae_wcl_credential_cancel_through_locked(sc, generation)",
        "iwn_sae_wcl_credential_clear_locked(sc)")
detach_begin = iwn_method("iwn_sae_wcl_detach_begin")
ordered(detach_begin, "detach closes then final-scrubs credential",
        "iwn_sae_tx_lifecycle_close(sc, true)",
        "IOSimpleLockLock(sc->sc_scan_lease_lock)",
        "sc->sc_sae_wcl_admission_reserved = false;",
        "IOSimpleLockUnlock(sc->sc_scan_lease_lock)",
        "IOSimpleLockLock(sc->sc_sae_wcl_credential_lock)",
        "iwn_sae_wcl_credential_clear_locked(sc)",
        "IOSimpleLockUnlock(sc->sc_sae_wcl_credential_lock)")

attach = iwn_method("iwn_attach")
ordered(attach, "one-slot setup",
        "sc->sc_sae_wcl_admission_reserved = false;",
        "sc->sc_sae_wcl_credential_lock = IOSimpleLockAlloc()",
        "sc->sc_sae_wcl_credential_staged = false;",
        "sc->sc_sae_wcl_credential_cancel_valid = false;",
        "sc->sc_sae_wcl_credential_cancel_through_generation = 0;",
        "explicit_bzero(&sc->sc_sae_wcl_credential")
detach = iwn_method("detach")
ordered(detach, "outer detach before credential-lock free",
        "iwn_sae_tx_detach_begin(sc);", "iwn_sae_wcl_detach_begin(sc);",
        "IOSimpleLockFree(sc->sc_sae_wcl_credential_lock)")
hw_stop = iwn_method("iwn_hw_stop")
ordered(hw_stop, "hardware stop credential sequencing", "iwn_sae_tx_stop_begin(sc);",
        "iwn_sae_wcl_stop_begin(sc);", "iwn_sae_tx_snapshot_reset(sc, &reset_event)",
        "iwn_sae_tx_cancel_all(sc)")
if_stop = iwn_method("iwn_stop")
ordered(if_stop, "interface stop credential sequencing", "iwn_sae_tx_stop_begin(sc);",
        "iwn_sae_wcl_stop_begin(sc);", "iwn_mfp_pae_abort_all(sc)")


class StageModel:
    """Small semantic model of the static one-slot/high-water contract."""

    def __init__(self, lab: bool, state: str = "scan", running: bool = True,
                 sta: bool = True, has_bss: bool = False) -> None:
        self.lab = lab
        self.state = state
        self.running = running
        self.sta = sta
        self.has_bss = has_bss
        self.closed = False
        self.detaching = False
        self.slot = None
        self.cancel_through = 0

    def stage(self, generation: int, record: str = "canonical",
              well_formed: bool = True) -> str:
        if not self.lab:
            return "unsupported"
        if not well_formed:
            return "bad-argument"
        if self.closed or self.detaching or not self.running or not self.sta:
            return "not-ready"
        if self.state not in ("scan", "run"):
            return "aborted"
        if self.state == "run" and not self.has_bss:
            return "not-ready"
        if generation <= self.cancel_through:
            return "aborted"
        if self.slot is not None:
            if self.slot == (generation, record):
                return "success"
            if self.slot[0] == generation:
                self.cancel(generation)
                return "aborted"
            if generation > self.slot[0]:
                self.cancel(self.slot[0])
                if generation <= self.cancel_through:
                    return "aborted"
                self.slot = (generation, record)
                return "success"
            return "aborted"
        self.slot = (generation, record)
        return "success"

    def cancel(self, generation: int) -> None:
        self.cancel_through = max(self.cancel_through, generation)
        if self.slot is not None and self.slot[0] <= self.cancel_through:
            self.slot = None

    def stop(self) -> None:
        self.closed = True
        if self.slot is not None:
            self.cancel(self.slot[0])
        self.slot = None

    def detach(self) -> None:
        self.closed = True
        self.detaching = True
        self.slot = None


normal = StageModel(lab=False)
assert normal.stage(1) == "unsupported"
lab = StageModel(lab=True)
assert lab.stage(1, well_formed=False) == "bad-argument"
assert lab.stage(1, record="first") == "success"
assert lab.stage(1, record="first") == "success"
assert lab.stage(1, record="conflicting") == "aborted"
assert lab.slot is None and lab.cancel_through == 1
assert lab.stage(1) == "aborted"
assert lab.stage(2, record="old") == "success"
# A newer valid request atomically retires A and stages B.  A later cancel of
# B still wins, and its high-water keeps either request from being revived.
assert lab.stage(3, record="replacement") == "success"
assert lab.slot == (3, "replacement") and lab.cancel_through == 2
assert lab.stage(2) == "aborted"
lab.cancel(3)
lab.cancel(3)
assert lab.slot is None and lab.cancel_through == 3
assert lab.stage(3) == "aborted"
assert lab.stage(4, record="old") == "success"
# A late cancellation for a newer generation scrubs the old staged record as
# well as fencing both generations; an out-of-order stage remains fail-closed.
lab.cancel(5)
assert lab.slot is None and lab.cancel_through == 5
assert lab.stage(4) == "aborted"
assert lab.stage(5) == "aborted"
assert lab.stage(6, record="new") == "success"
assert lab.stage(5, record="out-of-order") == "aborted"
lab.stop()
assert lab.slot is None and lab.cancel_through == 6
assert lab.stage(7) == "not-ready"
lab.detach()
assert lab.slot is None and lab.detaching
reconnect = StageModel(lab=True, state="run", has_bss=True)
assert reconnect.stage(10) == "success"
no_bss_reconnect = StageModel(lab=True, state="run", has_bss=False)
assert no_bss_reconnect.stage(10) == "not-ready"

print("PASS: product IWN WCL credential staging has a fixed copy, cancellation high-water, idempotent restage, and stop/detach scrub order")
PY
