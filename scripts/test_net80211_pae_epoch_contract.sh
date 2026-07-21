#!/usr/bin/env bash
# Source-level lifecycle gate for the inactive association-epoch fence.
#
# This is intentionally broader than a single reassociation assertion: it
# verifies every current invalidation edge (selection, state retry, RSN
# replacement, node cleanup, and same-BSSID WCL reassociation) in one run and
# proves that no MFP/key callback has been enabled as a side effect.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


root = Path(sys.argv[1])
var_h = (root / "itl80211/openbsd/net80211/ieee80211_var.h").read_text()
snapshot_h = (root / "itl80211/openbsd/net80211/ieee80211_pae_selected_bss.h").read_text()
proto_h = (root / "itl80211/openbsd/net80211/ieee80211_proto.h").read_text()
proto_c = (root / "itl80211/openbsd/net80211/ieee80211_proto.c").read_text()
node_c = (root / "itl80211/openbsd/net80211/ieee80211_node.c").read_text()
input_c = (root / "itl80211/openbsd/net80211/ieee80211_input.c").read_text()
ioctl_c = (root / "itl80211/openbsd/net80211/ieee80211_ioctl.c").read_text()
crypto_c = (root / "itl80211/openbsd/net80211/ieee80211_crypto.c").read_text()
core_c = (root / "itl80211/openbsd/net80211/ieee80211.c").read_text()
iwx_cpp = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()
iwm_cpp = (root / "itlwm/hal_iwm/mac80211.cpp").read_text()
iwm_lifecycle_cpp = (root / "itlwm/hal_iwm/ItlIwm.cpp").read_text()
iwn_lifecycle_cpp = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
v2_cpp = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
skywalk_cpp = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()


def fail(message):
    raise SystemExit(f"PAE association-epoch contract: {message}")


def require(text, needle, label):
    if needle not in text:
        fail(f"missing {label}: {needle}")


def ordered(text, label, *needles):
    pos = 0
    for needle in needles:
        found = text.find(needle, pos)
        if found < 0:
            fail(f"{label} missing ordered token: {needle}")
        pos = found + len(needle)


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


# The field is isolated from current key handling but has an explicit atomic
# lifetime for future q0/firmware-completion consumers.
require(var_h, "volatile u_int64_t\tic_pae_assoc_epoch;",
        "association epoch field")
require(var_h, "volatile u_int64_t\tic_pae_assoc_replace_epoch;",
        "controlled replacement owner token")
require(var_h, "IOSimpleLock\t\t*ic_pae_selected_bss_lock;",
        "selected-BSS leaf writer lock")
require(var_h, "struct ieee80211_pae_selected_bss ic_pae_selected_bss;",
        "writer-only selected BSS snapshot")
for token in (
    "uint64_t epoch;",
    "uint32_t sae_scan_flags;",
    "uint8_t bssid[IEEE80211_PAE_SELECTED_BSS_BSSID_LEN];",
    "uint8_t ssid_len;",
    "uint8_t ssid[IEEE80211_PAE_SELECTED_BSS_MAX_SSID_LEN];",
    "uint8_t strict_pure_sae_profile;",
	"ieee80211_pae_selected_bss_clear_payload",
    "ieee80211_pae_selected_bss_populate",
    "ieee80211_pae_selected_bss_identity_matches",
):
    require(snapshot_h, token, "fixed-byte selected BSS identity")
require(proto_h, "u_int64_t ieee80211_pae_assoc_epoch_current",
        "epoch current declaration")
require(proto_h, "u_int64_t ieee80211_pae_assoc_epoch_begin",
        "epoch begin declaration")
require(proto_h, "u_int64_t ieee80211_pae_assoc_epoch_begin_replacement",
        "controlled replacement declaration")
require(proto_h, "void ieee80211_pae_selected_bss_lock_destroy",
        "terminal selected-BSS lock destruction declaration")
require(proto_h, "void ieee80211_pae_selected_bss_capture",
        "selected BSS capture declaration")
require(proto_h, "struct ieee80211_pae_selected_bss;",
	"selected BSS forward declaration")
require(proto_h, "int ieee80211_pae_selected_bss_copyout_current",
	"selected BSS serialized copyout declaration")
require(proto_h, "void ieee80211_pae_assoc_epoch_note_newstate",
        "epoch newstate declaration")
ordered(proto_h, "newstate macro pre-driver fence",
        "#define    ieee80211_new_state", "ieee80211_pae_assoc_epoch_note_newstate",
        "(((_ic)->ic_newstate)")

current = body(proto_c, "u_int64_t\nieee80211_pae_assoc_epoch_current",
               "epoch current")
for token in ("ic->ic_opmode != IEEE80211_M_STA", "__atomic_load_n",
              "__ATOMIC_ACQUIRE"):
    require(current, token, "epoch current semantics")

begin = body(proto_c, "u_int64_t\nieee80211_pae_assoc_epoch_begin",
             "epoch begin")
advance = body(proto_c, "static u_int64_t\nieee80211_pae_assoc_epoch_advance_locked",
               "locked epoch advance")
for token in ("__atomic_add_fetch", "__ATOMIC_ACQ_REL", "while (epoch == 0)"):
    require(advance, token, "locked epoch advance semantics")
for token in ("ic->ic_opmode != IEEE80211_M_STA",
              "IOSimpleLockLockDisableInterrupt",
              "ieee80211_pae_assoc_epoch_advance_locked(ic)",
              "ic->ic_pae_assoc_replace_epoch, 0",
              "ieee80211_pae_selected_bss_invalidate(ic)",
              "IOSimpleLockUnlockEnableInterrupt"):
	    require(begin, token, "epoch begin semantics")

invalidate = body(proto_c, "static void\nieee80211_pae_selected_bss_invalidate",
			  "selected BSS invalidation")
ordered(invalidate, "selected BSS scrub after invalid publication",
	"__atomic_store_n(&ic->ic_pae_selected_bss.epoch, 0",
	"__ATOMIC_RELEASE", "ieee80211_pae_selected_bss_clear_payload")

replacement = body(proto_c,
                   "u_int64_t\nieee80211_pae_assoc_epoch_begin_replacement",
                   "controlled replacement")
for token in ("ic->ic_pae_selected_bss_lock", "lock == NULL",
              "ieee80211_pae_assoc_epoch_advance_locked(ic)",
              "ic->ic_pae_assoc_replace_epoch, epoch",
              "ieee80211_pae_selected_bss_invalidate(ic)",
              "IOSimpleLockUnlockEnableInterrupt"):
    require(replacement, token, "controlled replacement semantics")

destroy = body(proto_c, "void\nieee80211_pae_selected_bss_lock_destroy",
               "terminal selected-BSS lock destroy")
for token in ("IOSimpleLockLockDisableInterrupt",
              "ieee80211_pae_selected_bss_invalidate(ic)",
              "ic->ic_pae_selected_bss_lock = NULL",
              "IOSimpleLockUnlockEnableInterrupt", "IOSimpleLockFree(lock)"):
    require(destroy, token, "terminal selected-BSS lock destruction")

snapshot_capture = body(proto_c,
                        "void\nieee80211_pae_selected_bss_capture",
                        "selected BSS capture")
for token in (
    "ni != ic->ic_bss", "expected_epoch",
    "IOSimpleLockLockDisableInterrupt",
    "ic->ic_pae_assoc_epoch", "ic->ic_pae_assoc_replace_epoch",
    "ni->ni_bssid", "ni->ni_essid", "ni->ni_esslen",
    "ni->ni_sae_scan_flags", "strict_pure_sae_profile", "__ATOMIC_RELEASE",
    "IOSimpleLockUnlockEnableInterrupt",
):
    require(snapshot_capture, token, "selected BSS capture semantics")
for token in ("ieee80211_node_copy", "ieee80211_new_state",
              "IEEE80211_AKM_SAE", "IEEE80211_AUTH_ALG_SAE"):
    if token in snapshot_capture:
        fail(f"selected BSS capture must not activate association: {token}")

copyout = body(proto_c,
		       "int\nieee80211_pae_selected_bss_copyout_current",
		       "selected BSS serialized copyout")
for token in (
	"out == NULL", "out == &ic->ic_pae_selected_bss",
	"memset(out, 0, sizeof(*out))", "expected_epoch == 0",
	"IOSimpleLockLockDisableInterrupt", "IOSimpleLockUnlockEnableInterrupt",
	"ic->ic_opmode == IEEE80211_M_STA",
	"ic->ic_pae_assoc_epoch", "ic->ic_pae_assoc_replace_epoch",
	"ic->ic_pae_selected_bss.epoch",
	"ieee80211_pae_selected_bss_populate(out,",
	"ic->ic_pae_selected_bss.bssid", "ic->ic_pae_selected_bss.ssid",
	"ic->ic_pae_selected_bss.ssid_len",
	"ic->ic_pae_selected_bss.sae_scan_flags",
	"ic->ic_pae_selected_bss.strict_pure_sae_profile",
	"out->epoch = expected_epoch", "return copied",
):
	require(copyout, token, "selected BSS serialized copyout semantics")
ordered(copyout, "copyout non-alias scrub before fallible checks",
	"out == &ic->ic_pae_selected_bss", "memset(out, 0, sizeof(*out))",
	"expected_epoch == 0")
ordered(copyout, "copyout exact live epoch before fixed-value publication",
	"ic->ic_pae_assoc_epoch", "ic->ic_pae_assoc_replace_epoch",
	"ic->ic_pae_selected_bss.epoch",
	"ieee80211_pae_selected_bss_populate(out,", "out->epoch = expected_epoch")
for token in (
	"*out =", "memcpy", "IOUserClient", "AirportItlwmSaeRelayV1",
	"ic_psk", "IEEE80211_AUTH_ALG_SAE", "IEEE80211_C_MFP",
	"ic_set_key_wait", "ieee80211_send_mgmt", "kIOReturn",
):
	if token in copyout:
		fail(f"selected BSS copyout must remain fixed-value and inactive: {token}")

# The API is a future-owner foundation, not a new production handoff.  The
# declaration and definition are its only source references until an owner can
# prove a HAL lifecycle claim through terminal destruction.
copyout_references = []
for directory in (root / "itl80211", root / "AirportItlwm"):
	for suffix in ("*.c", "*.h", "*.cpp", "*.hpp"):
		for source in directory.rglob(suffix):
			if "ieee80211_pae_selected_bss_copyout_current(" in source.read_text():
				copyout_references.append(source.relative_to(root).as_posix())
if sorted(copyout_references) != [
	"itl80211/openbsd/net80211/ieee80211_proto.c",
	"itl80211/openbsd/net80211/ieee80211_proto.h",
]:
	fail("selected BSS copyout must have no production caller")

require(var_h, "#include <IOKit/IOLocks.h>",
        "selected-BSS spin-lock API")
attach = body(core_c, "void\nieee80211_ifattach", "interface attach")
require(attach, "IOSimpleLockAlloc()", "selected-BSS lock allocation")
detach = body(core_c, "void\nieee80211_ifdetach", "interface detach")
if "IOSimpleLockFree" in detach:
    fail("ifdetach must retain the selected-BSS lock for late rejected work")
for source, marker, label, order in (
    (iwx_cpp, "void ItlIwx::free()", "IWX terminal free",
     ("iwx_cmdq_destroy(&com)",
      "ieee80211_pae_selected_bss_lock_destroy(&com.sc_ic)", "super::free")),
    (iwm_lifecycle_cpp, "void ItlIwm::\nfree()", "IWM terminal free",
     ("ieee80211_pae_selected_bss_lock_destroy(&com.sc_ic)", "super::free")),
    (iwn_lifecycle_cpp, "void ItlIwn::free()", "IWN terminal free",
     ("ieee80211_pae_selected_bss_lock_destroy(&com.sc_ic)", "super::free")),
):
    ordered(body(source, marker, label), label, *order)

note = body(proto_c, "void\nieee80211_pae_assoc_epoch_note_newstate",
            "epoch state-note")
for transition in (
    "ic->ic_state == IEEE80211_S_SCAN &&\n\t     nstate == IEEE80211_S_AUTH",
    "ic->ic_state == IEEE80211_S_AUTH &&\n\t     nstate == IEEE80211_S_ASSOC",
    "ic->ic_state == IEEE80211_S_ASSOC &&\n\t     nstate == IEEE80211_S_RUN",
):
    require(note, transition, "preserved forward association transition")
require(note, "(void)ieee80211_pae_assoc_epoch_begin(ic);",
        "non-forward state invalidation")

detach = body(core_c, "void\nieee80211_ifdetach", "interface detach")
ordered(detach, "detach pre-teardown fence",
        "ieee80211_pae_assoc_epoch_begin(ic)", "timeout_del")
media_change = body(core_c, "int\nieee80211_media_change", "media change")
ordered(media_change, "STA mode-exit fence",
        "ic->ic_opmode == IEEE80211_M_STA", "newopmode != IEEE80211_M_STA",
        "ieee80211_pae_assoc_epoch_begin(ic)", "ic->ic_opmode = newopmode")
watchdog = body(core_c, "void\nieee80211_watchdog", "management watchdog")
ordered(watchdog, "timeout terminal fence before WCL callback",
        "ieee80211_pae_assoc_epoch_begin(ic)",
        "ieee80211_wcl_reassoc_post_failure")
require(watchdog, "ic->ic_wcl_reassoc_owner_active",
        "RUN-state WCL timeout fence")
wcl_failure = body(core_c, "void\nieee80211_wcl_reassoc_post_failure",
                   "WCL reassociation failure")
ordered(wcl_failure, "WCL terminal failure fence before publication",
        "ieee80211_wcl_reassoc_leaf_is_post_send",
        "ieee80211_pae_assoc_epoch_begin(ic)",
        "ic->ic_wcl_reassoc_owner_active = 0", "ic->ic_event_handler")

join = body(node_c, "void\nieee80211_node_join_bss", "BSS selection")
ordered(join, "controlled BSS replacement fence",
        "ieee80211_pae_assoc_epoch_begin_replacement(ic)",
        "(*ic->ic_node_copy)")
ordered(join, "post-copy selected BSS snapshot",
        "(*ic->ic_node_copy)(ic, ic->ic_bss, selbs);", "ni = ic->ic_bss;",
        "strict_pure_sae_profile = ieee80211_sae_selected_bss_profile_is_strict(ni);",
        "ieee80211_pae_selected_bss_capture(ic, ni, strict_pure_sae_profile,",
        "replacement_epoch);",
        "ieee80211_fix_rate")
for source, label in ((input_c, "scan parser"),
                      (skywalk_cpp, "Tahoe request ingress")):
    if "ieee80211_pae_selected_bss_capture" in source:
        fail(f"selected BSS snapshot must not be captured at {label}")
cleanup = body(node_c, "static void\nieee80211_node_cleanup_internal",
               "node cleanup owner")
ordered(cleanup, "ordinary current STA BSS cleanup fence",
        "cancel_current_bss",
        "ic->ic_opmode == IEEE80211_M_STA && ni == ic->ic_bss",
        "ieee80211_pae_assoc_epoch_begin(ic)", "if (ni->ni_rsnie != NULL)")
node_copy = body(node_c, "void\nieee80211_node_copy", "default node copy")
require(node_copy, "ieee80211_node_cleanup_internal(ic, dst, 0);",
        "controlled node-copy cleanup without a second epoch")
if "ieee80211_pae_assoc_epoch_begin" in node_copy:
    fail("default node copy must leave replacement-token ownership intact")
disable_rsn = body(ioctl_c, "void\nieee80211_disable_rsn", "RSN disable")
ordered(disable_rsn, "credential replacement fence",
        "ieee80211_pae_assoc_epoch_begin(ic)", "ic->ic_flags &=")
wpapsk = ioctl_c[ioctl_c.find("case SIOCS80211WPAPSK:"):]
wpapsk = wpapsk[:wpapsk.find("case SIOCG80211WPAPSK:")]
ordered(wpapsk, "direct PSK replacement fence",
        "psk = (struct ieee80211_wpapsk *)data;",
        "ieee80211_pae_assoc_epoch_begin(ic)", "if (psk->i_enabled)")

roam = body(node_c, "void\nieee80211_end_scan", "background roam")
ordered(roam, "deferred roaming fence",
        "IEEE80211_SEND_MGMT(ic, ic->ic_bss,",
        "ieee80211_pae_assoc_epoch_begin(ic)",
        "ic->ic_bss->ni_unref_cb = ieee80211_node_switch_bss")

assoc_resp = body(input_c, "void\nieee80211_recv_assoc_resp",
                  "association response")
assoc_failure = assoc_resp[assoc_resp.find("if (status != IEEE80211_STATUS_SUCCESS)"):]
ordered(assoc_failure, "association rejection fence before callback",
        "ni == ic->ic_bss", "ieee80211_pae_assoc_epoch_begin(ic)",
        "ieee80211_wcl_reassoc_post_failure")
same_bss = assoc_resp[assoc_resp.find("if (same_bss_wcl_reassoc)"):]
same_bss = same_bss[:same_bss.find("rates = xrates")]
require(same_bss, "request boundary already fenced", "same-BSSID request-boundary comment")
if "ieee80211_pae_assoc_epoch_begin" in same_bss:
    fail("same-BSSID response must retain the request epoch")

auth_open = body(proto_c, "void\nieee80211_auth_open(", "open authentication response")
ordered(auth_open, "authentication rejection fence",
        "if (status != 0 && ni == ic->ic_bss)",
        "ieee80211_pae_assoc_epoch_begin(ic)",
        "if (ic->ic_flags & IEEE80211_F_RSNON)")
auth_rx = body(input_c, "void\nieee80211_recv_auth", "authentication input")
ordered(auth_rx, "unsupported authentication rejection fence",
        "if (algo != IEEE80211_AUTH_ALG_OPEN) {",
        "if (ieee80211_record_sta_auth_failure(ic, wh, ni, algo, seq,",
        "status))",
        "ieee80211_pae_assoc_epoch_begin(ic)",
        "ic->ic_stats.is_rx_auth_unsupported++")
deauth = body(input_c, "void\nieee80211_recv_deauth", "deauthentication")
ordered(deauth, "deauth fence before event callback",
        "ic->ic_deauth_reason = reason;", "ni == ic->ic_bss",
        "ieee80211_pae_assoc_epoch_begin(ic)", "ic->ic_event_handler")
disassoc = body(input_c, "void\nieee80211_recv_disassoc", "disassociation")
ordered(disassoc, "disassoc fence before roamscan branch",
        "ni == ic->ic_bss", "ieee80211_pae_assoc_epoch_begin(ic)",
        "ic->ic_stats.is_rx_disassoc")

iwx_stop = body(iwx_cpp, "void ItlIwx::\niwx_stop_internal", "AX211 stop")
ordered(iwx_stop, "direct AX211 stop fence before gate close",
        "ieee80211_pae_assoc_epoch_begin(ic)", "iwx_task_gate_close")
ordered(iwx_stop, "direct AX211 stop fence before direct newstate",
        "ieee80211_pae_assoc_epoch_begin(ic)",
        "sc->sc_newstate(ic, IEEE80211_S_INIT, -1)")
iwm_stop = body(iwm_cpp, "void ItlIwm::\niwm_stop", "legacy Intel stop")
ordered(iwm_stop, "direct IWM stop fence before direct newstate",
        "ieee80211_pae_assoc_epoch_begin(ic)",
        "sc->sc_newstate(ic, IEEE80211_S_INIT, -1)")

cancel = body(v2_cpp, "static IOReturn\nairportItlwmCancelAssocAction",
              "destructive PLTI cancellation")
ordered(cancel, "PLTI destructive cancel fence",
        "if (ic != nullptr)", "ieee80211_pae_assoc_epoch_begin(ic)",
        "memset(ic->ic_psk, 0")
require((root / "AirportItlwm/AirportItlwmV2.hpp").read_text(),
        "bool     cancelPendingAssocTarget", "PLTI cancel result contract")
clear_external_pmk = body(skywalk_cpp,
                          "void AirportItlwmSkywalkInterface::\nclearExternalPmkEligibilityLocked",
                          "external PMK clear")
ordered(clear_external_pmk, "external PMK fallback fence",
        "ieee80211_pae_assoc_epoch_begin(ic)",
        "instance == nullptr || !instance->cancelPendingAssocTarget",
        "memset(ic->ic_psk, 0")
wcl_reassoc = body(skywalk_cpp,
                   "IOReturn AirportItlwmSkywalkInterface::\nsetWCL_REASSOC",
                   "WCL reassociation")
transparent_end = wcl_reassoc.find("return kIOReturnSuccess;",
                                   wcl_reassoc.find("ni_port_valid"))
if transparent_end < 0:
    fail("missing transparent WCL reassociation return")
if "ieee80211_pae_assoc_epoch_begin" in wcl_reassoc[:transparent_end]:
    fail("transparent WCL reassociation must not fabricate an OTA epoch")
wcl_ota = wcl_reassoc[transparent_end:]
ordered(wcl_ota, "OTA WCL reassociation request fence",
        "ieee80211_pae_assoc_epoch_begin(ic)",
        "ic->ic_wcl_reassoc_owner_active = 1", "ieee80211_send_mgmt")

# Keep the exact quarantine intact: this spine alone must not make an MFP
# session partially live.
require(crypto_c, "ic->ic_set_key_wait = NULL;", "default wait-key quarantine")
require(crypto_c, "ic->ic_eapol_key_input = NULL;", "default EAPOL quarantine")
require(iwx_cpp, "return EOPNOTSUPP;", "AX211 wait-key unsupported result")


class EpochModel:
    INIT, SCAN, AUTH, ASSOC, RUN = range(5)

    def __init__(self):
        self.epoch = 0
        self.state = self.INIT

    def begin(self):
        self.epoch = (self.epoch + 1) & ((1 << 64) - 1)
        if self.epoch == 0:
            self.epoch = 1
        return self.epoch

    def note(self, next_state):
        preserved = ((self.state, next_state) in {
            (self.SCAN, self.AUTH),
            (self.AUTH, self.ASSOC),
            (self.ASSOC, self.RUN),
        })
        if not preserved:
            self.begin()
        self.state = next_state


class SnapshotModel:
    def __init__(self):
        self.association_epoch = 0
        self.replacement_epoch = 0
        self.snapshot_epoch = 0
        self.bssid = bytes(6)
        self.ssid = b""
        self.sae_scan_flags = 0
        self.strict_pure_sae_profile = 0

    @staticmethod
    def zeroed_value():
        return {
            "epoch": 0,
            "sae_scan_flags": 0,
            "bssid": bytes(6),
            "ssid": b"",
            "strict_pure_sae_profile": 0,
        }

    def invalidate(self):
        self.snapshot_epoch = 0
        self.bssid = bytes(6)
        self.ssid = b""
        self.sae_scan_flags = 0
        self.strict_pure_sae_profile = 0

    def begin(self):
        self.association_epoch = (self.association_epoch + 1) & ((1 << 64) - 1)
        if self.association_epoch == 0:
            self.association_epoch = 1
        self.replacement_epoch = 0
        self.invalidate()
        return self.association_epoch

    def begin_replacement(self):
        token = self.begin()
        self.replacement_epoch = token
        return token

    def nested_node_copy_cleanup(self):
        # The default node copy owns no independent cancellation edge.
        return self.association_epoch

    def capture(self, expected_epoch, bssid, ssid, sae_scan_flags=0,
                strict_pure_sae_profile=0):
        if expected_epoch == 0 or expected_epoch != self.association_epoch or \
                expected_epoch != self.replacement_epoch:
            return False
        self.invalidate()
        if len(bssid) != 6 or len(ssid) > 32:
            self.replacement_epoch = 0
            return False
        self.bssid = bssid
        self.ssid = ssid
        self.sae_scan_flags = sae_scan_flags
        self.strict_pure_sae_profile = 1 if strict_pure_sae_profile else 0
        self.snapshot_epoch = expected_epoch
        self.replacement_epoch = 0
        return True

    def matches(self, epoch, bssid, ssid):
        return epoch != 0 and self.snapshot_epoch == epoch and \
            self.bssid == bssid and self.ssid == ssid

    def copyout_current(self, expected_epoch):
        out = self.zeroed_value()
        if expected_epoch == 0 or expected_epoch != self.association_epoch or \
                self.replacement_epoch != 0 or \
                self.snapshot_epoch != expected_epoch:
            return out
        out.update({
            "epoch": expected_epoch,
            "sae_scan_flags": self.sae_scan_flags,
            "bssid": self.bssid,
            "ssid": self.ssid,
            "strict_pure_sae_profile": self.strict_pure_sae_profile,
        })
        return out


# One deterministic matrix covers accepted forward phases, retries/teardown,
# terminal RX, credentials, direct stop, deferred roam, request-boundary
# reassociation, and 64-bit zero-wrap without a guest reboot.
model = EpochModel()
model.begin()                         # selected BSS
selected = model.epoch
model.state = model.SCAN
model.note(model.AUTH)
model.note(model.ASSOC)
model.note(model.RUN)
assert model.epoch == selected
model.note(model.SCAN)                # RUN -> SCAN cancellation
assert model.epoch != selected
after_teardown = model.epoch
model.begin()                         # rejected auth/assoc/deauth/disassoc
assert model.epoch != after_teardown
after_terminal = model.epoch
model.begin()                         # credential reset, stop/detach/mode exit
assert model.epoch != after_terminal
after_cancel = model.epoch
model.begin()                         # accepted deferred background roam
assert model.epoch != after_cancel
roam_epoch = model.epoch
model.begin()                         # accepted OTA WCL reassoc request
assert model.epoch != roam_epoch
request_epoch = model.epoch
# Successful reassoc response keeps the request epoch; failure begins again.
assert model.epoch == request_epoch
model.begin()
assert model.epoch != request_epoch
model.epoch = (1 << 64) - 1
assert model.begin() == 1

# The snapshot is post-copy identity, not a raw scan node or request carrier.
snapshot = SnapshotModel()
bssid = bytes.fromhex("021122334455")
ssid = b"s\x00ae"
token = snapshot.begin_replacement()
snapshot.nested_node_copy_cleanup()
assert snapshot.capture(token, bssid, ssid, 0x20, 1)
assert snapshot.matches(token, bssid, ssid)
assert not snapshot.matches(token + 1, bssid, ssid)
assert snapshot.copyout_current(token) == {
	"epoch": token,
	"sae_scan_flags": 0x20,
	"bssid": bssid,
	"ssid": ssid,
	"strict_pure_sae_profile": 1,
}
assert snapshot.copyout_current(token + 1) == SnapshotModel.zeroed_value()

# A replacement token means post-copy publication may still be changing; even
# an otherwise matching old snapshot cannot be observed by a future owner.
replacement_token = snapshot.begin_replacement()
assert snapshot.copyout_current(replacement_token) == SnapshotModel.zeroed_value()
assert snapshot.copyout_current(token) == SnapshotModel.zeroed_value()
snapshot.nested_node_copy_cleanup()
assert snapshot.capture(replacement_token, bssid, ssid)
assert snapshot.copyout_current(replacement_token)["epoch"] == replacement_token

# A cancel after replacement admission but before its post-copy publication
# clears the owner token. The stale producer cannot resurrect its BSS under
# the newer epoch.
stale_token = snapshot.begin_replacement()
snapshot.nested_node_copy_cleanup()
cancel_epoch = snapshot.begin()
assert cancel_epoch != stale_token
assert not snapshot.capture(stale_token, bssid, ssid)
assert snapshot.snapshot_epoch == 0
assert snapshot.copyout_current(stale_token) == SnapshotModel.zeroed_value()
assert snapshot.copyout_current(cancel_epoch) == SnapshotModel.zeroed_value()

# Capture can publish only while it owns the token; a later cancellation
# serializes after publication and revokes the record again.
token = snapshot.begin_replacement()
snapshot.nested_node_copy_cleanup()
assert snapshot.capture(token, bssid, ssid)
assert snapshot.matches(token, bssid, ssid)
snapshot.begin()
assert snapshot.snapshot_epoch == 0
assert snapshot.copyout_current(token) == SnapshotModel.zeroed_value()

print("PASS: association epoch fences, selected-BSS scrub/copyout, terminal RX, credential/reset, roam, and WCL reassociation")
PY
