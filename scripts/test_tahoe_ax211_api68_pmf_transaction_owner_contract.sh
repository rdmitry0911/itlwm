#!/usr/bin/env bash
# Source-and-model contract for the AX211/API-68 PMF transaction owner.
#
# This is deliberately a fail-closed admission test.  A PASS proves that the
# staged PSK+PMF path retains its ownership, epoch, q0, and rollback fences;
# it does not claim functional pure SAE support.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import re
import sys


root = Path(sys.argv[1])
paths = {
    "var": "itl80211/openbsd/net80211/ieee80211_var.h",
    "proto_h": "itl80211/openbsd/net80211/ieee80211_proto.h",
    "proto": "itl80211/openbsd/net80211/ieee80211_proto.c",
    "pae": "itl80211/openbsd/net80211/ieee80211_pae_input.c",
    "node": "itl80211/openbsd/net80211/ieee80211_node.c",
    "output": "itl80211/openbsd/net80211/ieee80211_output.c",
    "sky": "AirportItlwm/AirportItlwmSkywalkInterface.cpp",
    "auth": "AirportItlwm/TahoeAssociationAuthContracts.hpp",
    "cpp": "itlwm/hal_iwx/ItlIwx.cpp",
    "hpp": "itlwm/hal_iwx/ItlIwx.hpp",
    "iwxvar": "itlwm/hal_iwx/if_iwxvar.h",
}
source = {name: (root / path).read_text() for name, path in paths.items()}


def fail(message):
    raise SystemExit(f"AX211/API-68 PMF owner contract: {message}")


def require(text, token, label):
    if token not in text:
        fail(f"missing {label}: {token}")


def forbid(text, token, label):
    if token in text:
        fail(f"unexpected {label}: {token}")


def order(text, label, *tokens):
    cursor = 0
    for token in tokens:
        pos = text.find(token, cursor)
        if pos < 0:
            fail(f"{label} missing ordered token: {token}")
        cursor = pos + len(token)


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


# Value-only generic transaction state.  It owns no raw packet, userspace
# material, completion closure, or backend-private pointer.
var = source["var"]
txn = var[var.find("struct ieee80211_pae_mfp_txn {"):]
txn = txn[:txn.find("};") + 2]
for token in (
    "IEEE80211_PAE_MFP_STAGE_PTK",
    "IEEE80211_PAE_MFP_STAGE_GTK",
    "IEEE80211_PAE_MFP_STAGE_IGTK",
    "u_int64_t\t\tid;",
    "u_int64_t\t\tassoc_epoch;",
    "struct ieee80211_node\t*ni;",
    "struct ieee80211_key\tptk_key;",
    "struct ieee80211_key\tgtk_key;",
    "struct ieee80211_key\tigtk_key;",
):
    require(var, token, "staged PMF transaction field")
for forbidden in ("mbuf_t", "struct mbuf", "void (*", "OSObject"):
    forbid(txn, forbidden, "non-value PMF transaction state")
for token in (
    "ic_pae_mfp_txn_submit",
    "ic_pae_mfp_txn_cancel",
    "ic_pae_mfp_txn_finish",
    "u_int8_t\t\tic_pae_mfp_requested;",
):
    require(var, token, "PMF owner interface")

proto_h = source["proto_h"]
for token in (
    "int ieee80211_pae_mfp_txn_begin",
    "void ieee80211_pae_mfp_txn_complete",
    "void ieee80211_pae_mfp_txn_abort",
):
    require(proto_h, token, "generic PMF owner API")

# WCL opts in only for the exact audited PSK PMK carrier, after pure SAE has
# already been rejected.  Public/leave/disassociate ingress clear stale state.
auth = source["auth"]
require(auth, "inline bool requiresUnsupportedWpa3Auth", "pure-SAE gate")
require(auth, "inline bool isAuditedPskPmkAuth", "audited PSK classifier")
sky = source["sky"]
hidden = body(sky, "IOReturn AirportItlwmSkywalkInterface::\nsetWCL_ASSOCIATEImpl",
              "hidden WCL association")
order(hidden, "hidden PMF admission",
      "requiresUnsupportedWpa3Auth", "return kIOReturnUnsupported;",
      "ic->ic_pae_mfp_requested =",
      "TahoeAssociationContracts::pmfCapable(pmf_capability)",
      "TahoeAssociationAuthContracts::isAuditedPskPmkAuth(auth_upper)")
for marker, label in (
    ("IOReturn AirportItlwmSkywalkInterface::\nsetASSOCIATE", "public association"),
    ("IOReturn AirportItlwmSkywalkInterface::\nsetDISASSOCIATE", "disassociate"),
    ("IOReturn AirportItlwmSkywalkInterface::\nsetWCL_LEAVE_NETWORK", "WCL leave"),
):
    require(body(sky, marker, label), "ic->ic_pae_mfp_requested = 0;",
            f"{label} PMF reset")

# Exact hardware/runtime gate, then per-association negotiation.  Publishing
# C_MFP must bring the owned deferred ingress and three transaction callbacks
# together; the disabled branch must clear all of them.
cpp = source["cpp"]
runtime = body(cpp, "static bool\niwx_mfp_runtime_enabled",
               "AX211 PMF runtime gate")
for token in (
    "iwx_ax211_api68_igtk_v2_ok(sc)",
    "sc->sc_mfp_pae_lock != NULL",
    "sc->sc_cmdq_lock != NULL",
    "sc->sc_task_gate_lock != NULL",
    "sc->sc_taskq_initialized",
    "sc->sc_task_callbacks_ready",
    "sc->sc_nswq != NULL",
    "sc->sc_ic.ic_pae_selected_bss_lock != NULL",
):
    require(runtime, token, "runtime prerequisite")
publish = body(cpp, "static void\niwx_publish_mfp_capability",
               "PMF capability publication")
for token in (
    "ic->ic_caps &= ~IEEE80211_C_MFP;",
    "ic->ic_eapol_key_input = NULL;",
    "ic->ic_pae_mfp_txn_submit = NULL;",
    "ic->ic_pae_mfp_txn_cancel = NULL;",
    "ic->ic_pae_mfp_txn_finish = NULL;",
    "ic->ic_caps |= IEEE80211_C_MFP;",
    "ItlIwx::iwx_security_rx_eapol_input",
    "ItlIwx::iwx_pae_mfp_txn_submit",
    "ItlIwx::iwx_pae_mfp_txn_cancel",
    "ItlIwx::iwx_pae_mfp_txn_finish",
):
    require(publish, token, "PMF publication branch")

node = source["node"]
for token in (
    "!ic->ic_pae_mfp_requested",
    "(ic->ic_caps & IEEE80211_C_MFP) && ic->ic_pae_mfp_requested",
    "ni->ni_flags |= IEEE80211_NODE_MFP;",
):
    require(node, token, "per-association PMF negotiation")
ibss = body(node, "void\nieee80211_create_ibss", "IBSS RSN initialization")
if "if (ic->ic_caps & IEEE80211_C_MFP)" in ibss:
    fail("IBSS initialization must not advertise or stage MFP globally")
if ibss.count("ic->ic_pae_mfp_requested") != 2:
    fail("both IBSS MFPC and initial IGTK setup must require the WCL PMF request")
output = source["output"]
require(output, "ni->ni_flags & IEEE80211_NODE_MFP", "per-node protected MMPDU gate")

# RX ingress holds node ownership and association epoch before it queues a
# key frame.  The worker drops stale records before the generic PAE parser.
enqueue = body(cpp, "bool ItlIwx::\niwx_security_rx_enqueue",
               "security RX enqueue")
order(enqueue, "security RX capture", "held_ni = ieee80211_ref_node(ni);",
      "assoc_epoch = ieee80211_pae_assoc_epoch_current", "entry->assoc_epoch = assoc_epoch")
security_task = body(cpp, "void ItlIwx::\niwx_security_rx_task(void *arg)",
                     "security RX worker")
order(security_task, "security RX stale drop", "entry.assoc_epoch == 0",
      "ieee80211_pae_assoc_epoch_current(ic) != entry.assoc_epoch",
      "ic->ic_bss != entry.ni", "mbuf_freem(entry.m)")

# Initial Msg3 enters the async owner before every historical publish point.
pae = source["pae"]
msg3 = body(pae, "void\nieee80211_recv_4way_msg3", "4-way Msg3")
order(msg3, "initial PMF IGTK requirement",
      "IEEE80211_NODE_MFP | IEEE80211_NODE_RSN_NEW_PTK", "igtk == NULL",
      "goto deauth")
order(msg3, "Msg3 owner-before-commit",
      "ieee80211_pae_mfp_msg3_begin", "if (mfp_error == 0 || mfp_error == EBUSY)",
      "return;", "Historical non-MFP/retransmit path commits TPTK synchronously.",
      "memcpy(&ni->ni_ptk", "ieee80211_send_4way_msg4")
group = body(pae, "void\nieee80211_recv_rsn_group_msg1", "group Msg1")
order(group, "group owner-before-commit", "ieee80211_pae_mfp_group_begin",
      "if (mfp_error == 0 || mfp_error == EBUSY)", "return;",
      "check that key length matches")
group_plan = body(pae, "static int\nieee80211_pae_mfp_group_begin",
                  "group PMF plan")
for token in (
    "old_igtk = &ic->ic_nw_keys[old_kid];",
    "old_igtk->k_cipher != IEEE80211_CIPHER_BIP",
    "old_igtk->k_priv == NULL",
):
    require(group_plan, token, "rekey-without-IGTK live-key requirement")

# The generic owner serializes callbacks outside its leaf lock, verifies the
# identity tuple on every completion, and rolls firmware state back if BIP
# setup cannot commit after the key ACKs.
proto = source["proto"]
begin = body(proto, "int\nieee80211_pae_mfp_txn_begin", "PMF begin")
order(begin, "begin callback after lock", "IOSimpleLockUnlockEnableInterrupt(lock, irq);",
      "error = (*submit)")
complete = body(proto, "void\nieee80211_pae_mfp_txn_complete", "PMF complete")
for token in (
    "txn->id != id", "txn->phase != stage",
    "ieee80211_pae_mfp_txn_live_locked", "txn->assoc_epoch",
    "ieee80211_pae_mfp_txn_cancel_locked", "(*cancel)(ic, id)",
    "(*finish)(ic, id)",
):
    require(complete, token, "generic completion fence")
order(complete, "completion callback after lock", "IOSimpleLockUnlockEnableInterrupt(lock, irq);",
      "(*cancel)(ic, id)")
commit = body(proto, "static int\nieee80211_pae_mfp_txn_commit", "PMF commit")
order(commit, "BIP failure rollback", "ieee80211_set_key(ic, ni, &bip_key)",
      "ieee80211_pae_mfp_txn_rollback_firmware", "return EIO;")
for token in (
    "rollback_error:", "rollback_cancelled:",
    "ieee80211_pae_mfp_txn_rollback_firmware(ic, txn)",
    "ieee80211_send_4way_msg4", "ni->ni_port_valid = 1",
):
    require(commit, token, "commit rollback/port gate")

# q0 contains value-only async metadata.  Errors are decoded after q0 is
# unlocked; cancellation and timeout preserve the token as TIMED_OUT so that
# a late response can free it without continuing PAE.
iwxvar = source["iwxvar"]
for token in (
    "IWX_CMD_ASYNC_OWNER_MFP_PAE", "async_cookie", "async_ack_kind",
    "IWX_CMD_SLOT_TIMED_OUT", "bool task_active;", "bool release_pending;",
):
    require(iwxvar, token, "q0/owner state")
send = body(cpp, "int ItlIwx::\niwx_send_cmd", "q0 sender")
for token in (
    "hcmd->async_owner", "hcmd->async_ack_kind", "hcmd->async_cookie",
    "sc->sc_cmdq_slots[idx].async_cookie = hcmd->async_cookie",
):
    require(send, token, "q0 value metadata")
ack = body(cpp, "static int\niwx_async_cmd_ack_error", "async ACK decoder")
for token in (
    "pkt_len < sizeof(pkt->len_n_flags)",
    "slot->code != (uint32_t)code",
    "pkt->hdr.group_id & IWX_CMD_FAILED_MSK",
    "IWX_CMD_ASYNC_ACK_ADD_STA_STATUS",
    "IWX_ADD_STA_STATUS_MASK",
):
    require(ack, token, "strict async ACK failure")
done = body(cpp, "void ItlIwx::\niwx_cmd_done", "q0 completion")
order(done, "completion unlock-before-owner", "IOSimpleLockUnlock(sc->sc_cmdq_lock);",
      "unlockTsleep();", "that->iwx_mfp_pae_q0_done")
require(done, "iwx_async_cmd_ack_error", "q0 decoded PMF completion")
timeout = body(cpp, "void ItlIwx::\niwx_mfp_pae_timeout", "PMF timeout")
order(timeout, "timeout retains q0 token", "txn->result.error = ETIMEDOUT;",
      "that->iwx_mfp_pae_mark_q0_timeout(sc, cookie)")
release = body(cpp, "static void\niwx_mfp_pae_release_transaction", "PMF cancellation")
order(release, "cancellation retains q0 token", "txn->q0_inflight = false;",
      "that->iwx_mfp_pae_mark_q0_timeout(sc, q0_cookie)")
task = body(cpp, "void ItlIwx::\niwx_mfp_pae_task(void *arg)", "PMF serial worker")
for token in (
    "txn->task_active = true;", "txn->release_pending || txn->cancelled",
    "ieee80211_pae_mfp_txn_complete",
):
    require(task, token, "node lifetime handoff")

stop = body(cpp, "void ItlIwx::\niwx_stop_internal", "AX211 stop")
detach = body(cpp, "void ItlIwx::\ndetach", "AX211 detach")
for text, label in ((stop, "stop"), (detach, "detach")):
    require(text, "iwx_mfp_pae_abort_all(sc)", f"{label} PMF cancellation")
    order(text, f"{label} PMF task drain",
          "iwx_del_task(sc, sc->sc_nswq, &sc->mfp_pae_task)",
          "taskq_barrier(sc->sc_nswq)")
for token in (
    "ic_pae_mfp_txn_submit = NULL;", "ic_pae_mfp_txn_cancel = NULL;",
    "ic_pae_mfp_txn_finish = NULL;", "timeout_del(&sc->sc_mfp_pae_timeout)",
):
    require(detach, token, "detach PMF teardown")


class PmfModel:
    """Deterministic ownership model: no real credentials or packets."""

    STAGES = ("PTK", "GTK", "IGTK")

    def __init__(self):
        self.epoch = 1
        self.txn = None
        self.q0 = "FREE"
        self.cookie = 0
        self.events = []
        self.live_igtk = False

    def begin(self, *, pure_sae=False, requested=True, initial=True,
              includes_igtk=True, group=False):
        assert not pure_sae, "pure SAE must be rejected before PMF admission"
        assert requested, "PMF requires exact WCL request"
        if initial:
            assert includes_igtk, "initial Msg3 requires IGTK"
            stages = list(self.STAGES)
        else:
            assert group
            assert includes_igtk or self.live_igtk, \
                "rekey may omit IGTK only with a live old BIP key"
            stages = ["GTK"] + (["IGTK"] if includes_igtk else [])
        assert self.txn is None
        self.txn = {"epoch": self.epoch, "stages": stages, "index": 0,
                    "accepted": [], "group": group}
        self._submit()

    def _submit(self):
        self.cookie += 1
        self.q0 = "SUBMITTED"
        self.events.append("submit:" + self.txn["stages"][self.txn["index"]])

    def ack(self, *, ok=True, malformed=False, duplicate=False):
        if duplicate or self.q0 == "FREE":
            return
        if self.q0 == "TIMED_OUT":
            self.q0 = "FREE"
            self.events.append("late-ack-free")
            return
        assert self.q0 == "SUBMITTED"
        self.q0 = "FREE"
        if not ok or malformed or self.txn is None:
            self.abort("nack-or-malformed")
            return
        stage = self.txn["stages"][self.txn["index"]]
        self.txn["accepted"].append(stage)
        self.txn["index"] += 1
        if self.txn["index"] < len(self.txn["stages"]):
            self._submit()
            return
        if "IGTK" in self.txn["accepted"]:
            self.live_igtk = True
        self.events.append("group-msg2" if self.txn["group"] else "msg4")
        if not self.txn["group"]:
            self.events.extend(("port-valid", "link-up"))
        self.txn = None

    def timeout(self):
        assert self.q0 == "SUBMITTED"
        self.q0 = "TIMED_OUT"
        self.abort("timeout", retain_q0=True)

    def cancel(self, reason):
        if self.q0 == "SUBMITTED":
            self.q0 = "TIMED_OUT"
        self.abort(reason, retain_q0=self.q0 == "TIMED_OUT")

    def abort(self, reason, retain_q0=False):
        if self.txn is not None:
            self.events.append("rollback:" + reason)
        self.txn = None
        if not retain_q0:
            self.q0 = "FREE"

    def replace_epoch(self):
        self.epoch += 1
        self.cancel("stale-epoch")


# Initial key ordering and no externally visible success before final ACK.
m = PmfModel()
m.begin(initial=True, includes_igtk=True)
assert m.events == ["submit:PTK"]
m.ack(); assert m.events == ["submit:PTK", "submit:GTK"]
m.ack(); assert m.events == ["submit:PTK", "submit:GTK", "submit:IGTK"]
assert "msg4" not in m.events and "port-valid" not in m.events
m.ack()
assert m.events[-3:] == ["msg4", "port-valid", "link-up"]
assert m.live_igtk and m.q0 == "FREE"

# Group rekey can reuse a live IGTK but cannot invent one on an initial run.
m.begin(initial=False, group=True, includes_igtk=False)
m.ack()
assert m.events[-1] == "group-msg2"
try:
    PmfModel().begin(initial=False, group=True, includes_igtk=False)
    raise AssertionError("missing live IGTK accepted")
except AssertionError:
    pass

# Firmware NACKs, failed-mask/short/mismatched/unknown direct replies and
# queue-send failure all terminate before Msg4/port/link publication.
for failure in ("queue-full", "send-failure", "add-sta-nack", "failed-mask",
                "short-response", "opcode-mismatch", "unknown-direct"):
    m = PmfModel(); m.begin(initial=True, includes_igtk=True)
    if failure in ("add-sta-nack", "failed-mask", "short-response",
                   "opcode-mismatch", "unknown-direct"):
        m.ack(ok=False, malformed=failure != "add-sta-nack")
    else:
        m.abort(failure)
    assert m.txn is None and "msg4" not in m.events
    assert "port-valid" not in m.events and "link-up" not in m.events

# Timeout and every cancellation edge retain a late q0 response only long
# enough to free its descriptor; no old transaction can continue a new BSS.
m = PmfModel(); m.begin(initial=True, includes_igtk=True); m.timeout()
assert m.q0 == "TIMED_OUT" and m.txn is None
m.ack(); m.ack(duplicate=True)
assert m.q0 == "FREE" and m.events.count("late-ack-free") == 1
for reason in ("deauth", "disassoc", "roam", "replacement", "reassoc",
               "stop", "detach"):
    m = PmfModel(); m.begin(initial=True, includes_igtk=True); m.cancel(reason)
    assert m.txn is None and m.q0 == "TIMED_OUT"
    m.ack()
    assert m.q0 == "FREE" and "msg4" not in m.events
m = PmfModel(); m.begin(initial=True, includes_igtk=True); old_epoch = m.epoch
m.replace_epoch()
assert m.epoch != old_epoch and m.txn is None and "msg4" not in m.events

try:
    PmfModel().begin(pure_sae=True)
    raise AssertionError("pure SAE unexpectedly admitted")
except AssertionError:
    pass

print("PASS: AX211/API-68 PMF transaction owner static fences and deterministic failure matrix")
PY
