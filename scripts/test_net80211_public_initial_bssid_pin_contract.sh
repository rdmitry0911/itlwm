#!/usr/bin/env bash
# Source-level contract for public CoreWLAN's one-shot initial BSSID hint.
#
# It is deliberately narrower than raw ioctl/WCL BSSID policy: only an exact
# public WPA2 initial association may release its own DESBSSID after the
# selected BSS has reached RUN and opened its RSN port.  The test protects the
# provenance and lifecycle fences which let later same-ESS recovery use the
# ordinary net80211 candidate selector without weakening explicit pin users.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import sys


root = Path(sys.argv[1])
var_h = (root / "itl80211/openbsd/net80211/ieee80211_var.h").read_text()
proto_h = (root / "itl80211/openbsd/net80211/ieee80211_proto.h").read_text()
proto_c = (root / "itl80211/openbsd/net80211/ieee80211_proto.c").read_text()
node_c = (root / "itl80211/openbsd/net80211/ieee80211_node.c").read_text()
node_h = (root / "itl80211/openbsd/net80211/ieee80211_node.h").read_text()
pae_c = (root / "itl80211/openbsd/net80211/ieee80211_pae_input.c").read_text()
ioctl_c = (root / "itl80211/openbsd/net80211/ieee80211_ioctl.c").read_text()
sky = (root / "AirportItlwm/AirportItlwmSkywalkInterface.cpp").read_text()
legacy = (root / "AirportItlwm/AirportItlwm.cpp").read_text()
legacy_ioctl = (root / "AirportItlwm/AirportSTAIOCTL.cpp").read_text()
user_client = (root / "itlwm/ItlNetworkUserClient.cpp").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()
iwm = (root / "itlwm/hal_iwm/mac80211.cpp").read_text()
iwx = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()


def fail(message: str) -> None:
    raise SystemExit(f"public initial-BSSID pin contract: {message}")


def require(text: str, token: str, label: str) -> None:
    if token not in text:
        fail(f"missing {label}: {token}")


def forbid(text: str, token: str, label: str) -> None:
    if token in text:
        fail(f"unexpected {label}: {token}")


def ordered(text: str, label: str, *tokens: str) -> None:
    cursor = 0
    for token in tokens:
        found = text.find(token, cursor)
        if found < 0:
            fail(f"{label} missing ordered token: {token}")
        cursor = found + len(token)


def body(text: str, marker: str, label: str) -> str:
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


# The provenance value is fixed public identity/epoch state only; it cannot
# accidentally become an alternate owner for a node, key, credential, or WCL
# completion callback.
record_start = var_h.find("struct ieee80211_public_initial_bssid_pin")
record_end = var_h.find("};", record_start)
if record_start < 0 or record_end < record_start:
    fail("missing public initial-BSSID provenance record")
record = var_h[record_start:record_end]
for token in (
    "u_int64_t\t\tconfiguration_epoch;",
    "u_int64_t\t\tassociation_epoch;",
    "u_int8_t\t\tbssid[IEEE80211_ADDR_LEN];",
    "u_int8_t\t\tactive;",
    "u_int8_t\t\tbinding_pending;",
):
    require(record, token, "fixed public provenance field")
for forbidden in ("node", "key", "pmk", "password", "callback", "owner",
                  "scan_hop"):
    forbid(record.lower(), forbidden, "secret/owner field in provenance record")
require(var_h, "ic_public_initial_bssid_pin;", "controller-owned provenance")

for token in (
    "ieee80211_public_initial_bssid_pin_arm",
    "ieee80211_public_initial_bssid_pin_disarm",
    "ieee80211_public_initial_bssid_pin_port_valid",
    "ieee80211_public_initial_bssid_pin_should_defer_link_up",
):
    require(proto_h, token, "public pin helper declaration")
require(proto_h, "IEEE80211_NEWSTATE_ARG_SCAN_HOP",
        "private scanner channel-hop tag")
require(proto_h, "IEEE80211_NEWSTATE_BACKEND_ARG",
        "backend-local argument normalization")
require(node_h, "ieee80211_node_cleanup_scan_hop",
        "exact backend scan cleanup declaration")

arm = body(proto_c, "ieee80211_public_initial_bssid_pin_arm(", "pin arm")
ordered(arm, "arm is leaf-serialized and exact",
        "IOSimpleLockLockDisableInterrupt(lock);",
        "__atomic_load_n(&ic->ic_pae_assoc_epoch",
        "IEEE80211_F_DESBSSID",
        "IEEE80211_ADDR_EQ(ic->ic_des_bssid, bssid)",
        "configuration_epoch = epoch;",
        "IEEE80211_ADDR_COPY(ic->ic_public_initial_bssid_pin.bssid, bssid);",
        "active = 1;")

disarm = body(proto_c, "ieee80211_public_initial_bssid_pin_disarm(", "pin disarm")
require(disarm, "IOSimpleLockLockDisableInterrupt(lock);", "leaf-serialized disarm")
require(disarm, "ieee80211_public_initial_bssid_pin_clear_locked(ic);",
        "marker-only disarm")
forbidden_disarm = disarm.replace("ieee80211_public_initial_bssid_pin_clear_locked", "")
forbid(forbidden_disarm, "IEEE80211_F_DESBSSID", "actual pin mutation in disarm")
forbid(forbidden_disarm, "ic_des_bssid", "desired BSSID mutation in disarm")

bind = body(proto_c, "ieee80211_public_initial_bssid_pin_bind_selected_bss_locked(",
            "post-copy pin bind")
for token in (
    "pin->binding_pending == 0",
    "pin->association_epoch != 0",
    "ic->ic_pae_assoc_epoch",
    "ic->ic_pae_selected_bss.epoch",
    "IEEE80211_F_DESBSSID",
    "IEEE80211_ADDR_EQ(pin->bssid, ic->ic_des_bssid)",
    "IEEE80211_ADDR_EQ(pin->bssid, ni->ni_bssid)",
    "pin->association_epoch = expected_epoch;",
):
    require(bind, token, "exact post-copy bind fence")
forbid(bind, "ieee80211_new_state", "state transition in bind")
forbid(bind, "ic_set_key", "key work in bind")

capture = body(proto_c, "ieee80211_pae_selected_bss_capture(",
               "selected BSS capture")
ordered(capture, "capture publishes before pin bind",
        "ieee80211_pae_selected_bss_populate",
        "__atomic_store_n(&ic->ic_pae_selected_bss.epoch",
        "ieee80211_public_initial_bssid_pin_bind_selected_bss_locked")

release = body(proto_c, "void\nieee80211_public_initial_bssid_pin_port_valid(",
               "pin release")
for token in (
    "pin->active != 0",
    "pin->association_epoch != 0",
    "pin->association_epoch == epoch",
    "ni == ic->ic_bss",
    "ni->ni_port_valid != 0",
    "ic->ic_opmode == IEEE80211_M_STA",
    "ic->ic_state == IEEE80211_S_RUN",
    "ic->ic_pae_selected_bss.epoch",
    "IEEE80211_F_DESBSSID",
    "IEEE80211_ADDR_EQ(pin->bssid, ic->ic_des_bssid)",
    "IEEE80211_ADDR_EQ(pin->bssid, ni->ni_bssid)",
):
    require(release, token, "release identity/state fence")
ordered(release, "only release mutates the actual pin",
        "ic->ic_flags &= ~IEEE80211_F_DESBSSID;",
        "explicit_bzero(ic->ic_des_bssid",
        "ieee80211_public_initial_bssid_pin_clear_locked(ic);")

defer_link = body(proto_c,
                  "ieee80211_public_initial_bssid_pin_should_defer_link_up(",
                  "public pre-port LINK_UP guard")
for token in (
    "pin->active != 0",
    "pin->association_epoch != 0",
    "pin->association_epoch == epoch",
    "ic->ic_opmode == IEEE80211_M_STA",
    "ic->ic_state == IEEE80211_S_RUN",
    "ni == ic->ic_bss",
    "ni->ni_port_valid == 0",
    "ic->ic_pae_selected_bss.epoch",
    "IEEE80211_F_DESBSSID",
    "IEEE80211_ADDR_EQ(pin->bssid, ic->ic_des_bssid)",
    "IEEE80211_ADDR_EQ(pin->bssid, ni->ni_bssid)",
):
    require(defer_link, token, "exact pre-port LINK_UP fence")
forbid(defer_link, "ic_set_key", "key work in LINK_UP guard")
run_start = proto_c.find("case IEEE80211_S_RUN:\n\t\tswitch (ostate)",
                         proto_c.find("int\nieee80211_newstate("))
if run_start < 0:
    fail("missing generic RUN state")
run_state = proto_c[run_start:run_start + 3000]
ordered(run_state, "public marker defers Tahoe generic LINK_UP",
        "#ifdef USE_APPLE_SUPPLICANT",
        "IEEE80211_F_RSNON",
        "ieee80211_public_initial_bssid_pin_should_defer_link_up(",
        "ieee80211_set_link_state(ic, LINK_STATE_UP);")

# Source origin: only Skywalk's public association arms it after the shared
# policy setter succeeded and only for a local PSK-compatible request.
public_assoc = body(sky, "setASSOCIATE(struct apple80211_assoc_data *ad)",
                    "public association")
ordered(public_assoc, "public attempt clears stale marker before early exits",
        "struct ieee80211com *ic = fHalService->get80211Controller();",
        "ieee80211_public_initial_bssid_pin_disarm(ic);",
        "airportItlwmRegDiagShouldBlock")
ordered(public_assoc, "arm follows successful policy setup",
        "assocResult = associateSSID(",
        "if (assocResult == kIOReturnSuccess &&",
        "TahoeAssociationAuthContracts::mayUseLocalPskPmk",
        "ieee80211_public_initial_bssid_pin_arm(ic,")

shared_assoc = body(sky, "AirportItlwmSkywalkInterface::associateSSID(",
                    "shared Skywalk association")
require(shared_assoc, "ieee80211_public_initial_bssid_pin_disarm(ic);",
        "shared desired-BSSID writer disarms stale public provenance")
forbid(shared_assoc, "ieee80211_public_initial_bssid_pin_arm",
       "WCL-shared association arm")
forbid(shared_assoc, "ieee80211_public_initial_bssid_pin_port_valid",
       "WCL-shared association release")

# The actual selection is tied to node replacement rather than a request-side
# BSSID, so a stale scan candidate cannot acquire release authority.
join = body(node_c, "void\nieee80211_node_join_bss(", "node join BSS")
ordered(join, "selected BSS replaces before pin bind",
        "replacement_epoch = ieee80211_pae_assoc_epoch_begin_replacement(ic);",
        "(*ic->ic_node_copy)(ic, ic->ic_bss, selbs);",
        "ni = ic->ic_bss;",
        "ieee80211_pae_selected_bss_capture(ic, ni, sae_profile,",
        "ieee80211_sae_wcl_request_bind_selected_bss")
require(capture, "ieee80211_public_initial_bssid_pin_bind_selected_bss_locked",
        "post-copy public bind")

# The scanner may touch many channels before node_join_bss().  Its single
# internal tag crosses preflight/replay only to IWN's exact HAL callback,
# where that callback selects its already-fenced transient-BSS cleanup and
# then restores the historic -1 argument before forwarding anything lower.
# IWM/IWX queue state work, so their ingress normalizes the tag before it can
# become an unproven task snapshot.
next_scan = body(node_c, "ieee80211_next_scan(", "next scan")
ordered(next_scan, "only scanner channel hops carry the private tag",
        "ic->ic_bss->ni_chan = chan;",
        "ieee80211_new_state(ic, IEEE80211_S_SCAN,",
        "IEEE80211_NEWSTATE_ARG_SCAN_HOP);")
note_newstate = body(proto_c, "ieee80211_pae_assoc_epoch_note_newstate(",
                     "newstate epoch note")
ordered(note_newstate, "tagged scan hop preserves only through internal fence",
        "AirportItlwmPostPltiTraceNoteStateRequest",
        "ic->ic_state == IEEE80211_S_SCAN && nstate == IEEE80211_S_SCAN",
        "arg == IEEE80211_NEWSTATE_ARG_SCAN_HOP",
        "ieee80211_pae_assoc_epoch_begin_internal(ic, 1);")
require(note_newstate, "ieee80211_pae_assoc_epoch_begin(ic);",
        "untagged state requests retain ordinary epoch cancellation")
backend_macro_start = proto_h.find("#define IEEE80211_NEWSTATE_BACKEND_ARG")
backend_macro_end = proto_h.find("#define    ieee80211_new_state", backend_macro_start)
if backend_macro_start < 0 or backend_macro_end < backend_macro_start:
    fail("missing backend argument normalization macro")
backend_macro = proto_h[backend_macro_start:backend_macro_end]
for token in (
    "IEEE80211_NEWSTATE_ARG_SCAN_HOP",
    "? -1 : (_arg)",
):
    require(backend_macro, token, "scan-hop backend normalization")

node_cleanup = body(node_c, "ieee80211_node_cleanup_internal(",
                    "ordinary node cleanup")
require(node_cleanup, "ieee80211_pae_assoc_epoch_begin(ic);",
        "ordinary current-BSS cleanup cancellation")
hop_cleanup = body(node_c, "ieee80211_node_cleanup_scan_hop(",
                   "tagged node cleanup")
for token in (
    "ic->ic_opmode != IEEE80211_M_STA",
    "ic->ic_state != IEEE80211_S_SCAN",
    "ni != ic->ic_bss",
    "ieee80211_node_cleanup_internal(ic, ni, 1);",
    "ieee80211_node_cleanup_internal(ic, ni, 0);",
):
    require(hop_cleanup, token, "fail-closed tagged cleanup guard")

iwn_scan_state = body(iwn, "int ItlIwn::\niwn_newstate(", "IWN scan callback")
ordered(iwn_scan_state, "IWN captures then normalizes the hop tag",
        "scan_hop = nstate == IEEE80211_S_SCAN",
        "IEEE80211_NEWSTATE_ARG_SCAN_HOP",
        "arg = IEEE80211_NEWSTATE_BACKEND_ARG(nstate, arg);")
ordered(iwn_scan_state, "IWN uses exact tagged cleanup only in SCAN case",
        "if (scan_hop)",
        "ieee80211_node_cleanup_scan_hop(ic, ic->ic_bss);",
        "ieee80211_node_cleanup(ic, ic->ic_bss);")

# IWN is the only backend permitted to carry this tag across its deferred
# physical-scan lease.  The lease owns the copied argument until its exact
# terminal retires, then feeds it back through the generic macro so the
# ordinary state/epoch fence runs before IWN sees the special cleanup.
for marker, label in (
    ("static bool\niwn_scan_lease_defer_scan(", "IWN active-lease defer"),
    ("static bool\niwn_scan_lease_defer_terminal_replay(",
     "IWN terminal-lease defer"),
):
    defer = body(iwn, marker, label)
    ordered(defer, f"{label} retains the exact state argument under lease lock",
            "IOSimpleLockLock(sc->sc_scan_lease_lock);",
            "sc->sc_scan_lease_replay_pending = true;",
            "sc->sc_scan_lease_replay_nstate = nstate;",
            "sc->sc_scan_lease_replay_arg = arg;")

replay_task = body(iwn, "iwn_scan_lease_replay_task(void *arg)",
                   "IWN scan-lease replay task")
ordered(replay_task, "IWN replays the retained tag only through generic newstate",
        "nstate = sc->sc_scan_lease_replay_nstate;",
        "nstate_arg = sc->sc_scan_lease_replay_arg;",
        "sc->sc_scan_lease_replay_pending = false;",
        "sc->sc_scan_lease_replay_nstate = IEEE80211_S_INIT;",
        "sc->sc_scan_lease_replay_arg = -1;",
        "ieee80211_new_state(ic, nstate, nstate_arg);")
for marker, label in (
    ("static void\niwn_scan_lease_drop_replay(", "IWN replay drop"),
    ("static enum iwn_scan_lease_owner\niwn_scan_lease_begin_hardware_invalidation(",
     "IWN reset replay drop"),
):
    reset = body(iwn, marker, label)
    ordered(reset, f"{label} clears retained state argument",
            "sc->sc_scan_lease_replay_pending = false;",
            "sc->sc_scan_lease_replay_nstate = IEEE80211_S_INIT;",
            "sc->sc_scan_lease_replay_arg = -1;")

for text, marker, label in (
    (iwm, "int ItlIwm::\niwm_newstate(", "IWM newstate ingress"),
    (iwx, "int ItlIwx::\niwx_newstate(", "IWX newstate ingress"),
):
    ingress = body(text, marker, label)
    ordered(ingress, f"{label} normalizes before queued task capture",
            "arg = IEEE80211_NEWSTATE_BACKEND_ARG(nstate, arg);",
            "sc->ns_nstate = nstate;",
            "sc->ns_arg = arg;")
    forbid(ingress, "scan_hop =", f"{label} stale tag capture")
    forbid(text, "ieee80211_node_cleanup_scan_hop",
           f"{label} IWN-only cleanup bypass")

# WPA2's ordinary Msg3 transition is the primary physical route; PMF's async
# completion uses the same guard before it posts its terminal event.
msg3 = body(pae_c, "void\nieee80211_recv_4way_msg3(", "WPA2 Msg3")
ordered(msg3, "Msg3 releases only on 0->1 port-valid",
        "int was_port_valid = ni->ni_port_valid;",
        "ni->ni_port_valid = 1;",
        "if (!was_port_valid)",
        "ieee80211_public_initial_bssid_pin_port_valid(ic, ni);",
		"ieee80211_set_link_state(ic, LINK_STATE_UP);",
        "IEEE80211_EVT_STA_RSN_HANDSHAKE_DONE")

for marker, label in (
    ("void\nieee80211_recv_rsn_group_msg1(", "RSN group Msg1"),
    ("void\nieee80211_recv_wpa_group_msg1(", "WPA group Msg1"),
):
    group_msg1 = body(pae_c, marker, label)
    ordered(group_msg1, f"{label} releases before LINK_UP",
            "int was_port_valid = ni->ni_port_valid;",
            "ni->ni_port_valid = 1;",
            "if (!was_port_valid)",
            "ieee80211_public_initial_bssid_pin_port_valid(ic, ni);",
            "ieee80211_set_link_state(ic, LINK_STATE_UP);")

for text, marker, label in (
    (sky, "void AirportItlwmSkywalkInterface::setGTK(", "Skywalk setGTK"),
    (legacy, "void AirportItlwm::setGTK(", "legacy setGTK"),
):
    gtk = body(text, marker, label)
    ordered(gtk, f"{label} releases before LINK_UP",
            "was_port_valid = ni->ni_port_valid",
            "ni->ni_port_valid = 1;",
            "if (!was_port_valid)",
            "ieee80211_public_initial_bssid_pin_port_valid(ic, ni);",
            "ieee80211_set_link_state(ic, LINK_STATE_UP);")

mfp_complete = body(proto_c, "ieee80211_pae_mfp_txn_complete(",
                    "async PMF completion")
ordered(mfp_complete, "async PMF release before terminal event",
        "finish_error == 0 && published && port_became_valid",
		"ieee80211_public_initial_bssid_pin_port_valid(ic,",
        "ieee80211_set_link_state(ic, LINK_STATE_UP);",
        "IEEE80211_EVT_STA_RSN_HANDSHAKE_DONE")

# Every non-public explicit BSSID owner is marker-only disarmed before it can
# preserve or overwrite its own DESBSSID policy.
wcl = body(sky, "setWCL_ASSOCIATE(apple80211AssocCandidates *candidates)",
           "WCL association wrapper")
ordered(wcl, "WCL clears public provenance before parser",
        "AIRPORT_ITLWM_REQUIRE_LIVE_OPERATION();",
        "ieee80211_public_initial_bssid_pin_disarm",
        "return setWCL_ASSOCIATEImpl(candidates);")
forbid(wcl, "ieee80211_public_initial_bssid_pin_arm", "WCL arm")
forbid(wcl, "ieee80211_public_initial_bssid_pin_port_valid", "WCL release")

for marker, label in (
    ("setDISASSOCIATE(void *ad)", "public disassociate"),
    ("setWCL_LEAVE_NETWORK(apple80211_leave_network *data)", "WCL leave"),
    ("setWCL_REASSOC(apple80211_reassoc *data)", "WCL reassociation"),
    ("setWCL_JOIN_ABORT(apple80211_wcl_abort_join *data)", "WCL abort"),
    ("setWCL_SCAN_ABORT(void *data)", "WCL scan abort"),
):
    current = body(sky, marker, label)
    require(current, "ieee80211_public_initial_bssid_pin_disarm", label)
    forbid(current, "ieee80211_public_initial_bssid_pin_arm", f"{label} arm")
    forbid(current, "ieee80211_public_initial_bssid_pin_port_valid",
           f"{label} release")

legacy_assoc = body(legacy, "IOReturn AirportItlwm::associateSSID(",
                   "legacy association")
ordered(legacy_assoc, "legacy BSSID writer disarms marker",
        "struct ieee80211com *ic = fHalService->get80211Controller();",
        "ieee80211_public_initial_bssid_pin_disarm(ic);",
        "IEEE80211_ADDR_COPY(ic->ic_des_bssid, bssid.octet);")
forbid(legacy_assoc, "ieee80211_public_initial_bssid_pin_arm", "legacy arm")

legacy_public_assoc = body(legacy_ioctl, "IOReturn AirportItlwm::\nsetASSOCIATE(",
                           "legacy public association")
ordered(legacy_public_assoc, "legacy public carrier disarms before early exits",
        "struct ieee80211com *ic = fHalService->get80211Controller();",
        "ieee80211_public_initial_bssid_pin_disarm(ic);",
        "if (!ad)")

legacy_leave = body(legacy_ioctl, "IOReturn AirportItlwm::setDISASSOCIATE(",
                    "legacy disassociate")
require(legacy_leave, "ieee80211_public_initial_bssid_pin_disarm(ic);",
        "legacy disassociate teardown")

raw_start = ioctl_c.find("case SIOCS80211BSSID:")
raw_end = ioctl_c.find("case SIOCG80211BSSID:", raw_start)
if raw_start < 0 or raw_end < raw_start:
    fail("missing raw BSSID ioctl case")
raw = ioctl_c[raw_start:raw_end]
ordered(raw, "raw BSSID writer disarms marker",
        "bssid = (struct ieee80211_bssid *)data;",
        "ieee80211_public_initial_bssid_pin_disarm(ic);",
        "IEEE80211_F_DESBSSID")
user_bssid = body(user_client, "sNW_BSSID(OSObject* target, void* data, bool isSet)",
                  "private user-client BSSID")
ordered(user_bssid, "user-client BSSID writer disarms marker",
        "if (isSet) {",
        "ieee80211_public_initial_bssid_pin_disarm(ic);",
        "IEEE80211_F_DESBSSID")

epoch_begin_internal = body(proto_c, "ieee80211_pae_assoc_epoch_begin_internal(",
                            "ordinary epoch teardown")
for token in (
    "prior_epoch = __atomic_load_n(&ic->ic_pae_assoc_epoch",
    "preserve_unbound_public_initial_bssid_pin != 0",
    "ic->ic_public_initial_bssid_pin.association_epoch == 0",
    "ic->ic_public_initial_bssid_pin.binding_pending == 0",
    "ic->ic_public_initial_bssid_pin.configuration_epoch == prior_epoch",
    "IEEE80211_F_DESBSSID",
    "IEEE80211_ADDR_EQ(ic->ic_public_initial_bssid_pin.bssid",
    "configuration_epoch = epoch;",
    "ieee80211_public_initial_bssid_pin_clear_locked(ic);",
):
    require(epoch_begin_internal, token, "scan-hop preservation fence")
forbid(epoch_begin_internal, "ic->ic_flags &= ~IEEE80211_F_DESBSSID",
       "ordinary reset silently weakening explicit pin")
epoch_begin = body(proto_c, "ieee80211_pae_assoc_epoch_begin(",
                   "ordinary epoch wrapper")
require(epoch_begin, "ieee80211_pae_assoc_epoch_begin_internal(ic, 0);",
        "ordinary reset marker teardown")
replacement = body(proto_c, "ieee80211_pae_assoc_epoch_begin_replacement(",
                   "controlled replacement")
ordered(replacement, "only pending public marker crosses selected-BSS replacement",
        "prior_epoch = __atomic_load_n(&ic->ic_pae_assoc_epoch",
        "ieee80211_pae_assoc_epoch_advance_locked(ic);",
        "ic->ic_public_initial_bssid_pin.active != 0",
        "ic->ic_public_initial_bssid_pin.binding_pending = 1;")


# Small state model: source inspection above proves the wiring, while this
# matrix makes the functional semantics explicit and guards the key negative
# cases without needing a radio or an identity-bearing fixture.
class Pin:
    NONE, PENDING, BOUND = range(3)

    def __init__(self) -> None:
        self.phase = self.NONE
        self.des_set = False
        self.bssid = None
        self.epoch = 0

    def arm(self, bssid: bytes, epoch: int) -> None:
        self.phase, self.des_set, self.bssid, self.epoch = (
            self.PENDING, True, bssid, epoch)

    def disarm(self) -> None:
        self.phase, self.bssid, self.epoch = self.NONE, None, 0

    def scan_hop(self, epoch: int, exact_backend_cleanup: bool) -> None:
        # Tagged SCAN->SCAN advances the fence but keeps only an exact
        # unbound public marker.  Its matched backend cleanup is already
        # fenced and therefore cannot cancel it a second time.
        if self.phase != self.PENDING or not self.des_set:
            self.disarm()
            return
        self.epoch = epoch
        if not exact_backend_cleanup:
            self.disarm()

    def bind(self, bssid: bytes, epoch: int) -> None:
        if self.phase == self.PENDING and self.bssid == bssid:
            self.phase, self.epoch = self.BOUND, epoch
        else:
            self.disarm()

    def port_valid(self, bssid: bytes, epoch: int, run: bool) -> None:
        if self.phase != self.BOUND or self.epoch != epoch or self.bssid != bssid:
            return
        if run and self.des_set:
            self.des_set = False
        self.disarm()


a, b = b"A", b"B"
pin = Pin()
pin.arm(a, 10)
for epoch in (11, 12, 13):
    pin.scan_hop(epoch, True)
    assert pin.phase == Pin.PENDING and pin.des_set
pin.bind(a, 14)
pin.port_valid(a, 14, True)
assert not pin.des_set and pin.phase == Pin.NONE

pin = Pin()
pin.arm(a, 10)
pin.scan_hop(11, False)  # untagged SCAN->SCAN / abort cleanup
assert pin.des_set and pin.phase == Pin.NONE

pin = Pin()
pin.arm(a, 10)
pin.bind(a, 11)
pin.port_valid(b, 11, True)
assert pin.des_set and pin.phase == Pin.BOUND
pin.port_valid(a, 12, True)
assert pin.des_set and pin.phase == Pin.BOUND
pin.port_valid(a, 11, False)
assert pin.des_set and pin.phase == Pin.NONE

pin = Pin()
pin.arm(a, 10)
pin.scan_hop(11, True)
pin.bind(b, 12)  # another BSS cannot acquire release authority
pin.port_valid(b, 12, True)
assert pin.des_set and pin.phase == Pin.NONE

pin = Pin()
pin.arm(a, 10)
pin.bind(a, 11)
pin.disarm()  # WCL/raw/legacy takeover
pin.port_valid(a, 11, True)
assert pin.des_set and pin.phase == Pin.NONE

print("PASS: public initial BSSID pin releases only after exact WPA2 port-valid")
PY
