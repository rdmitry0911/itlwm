#!/usr/bin/env bash
# Source-and-model contract for the selected AX210-family/API-68 PMF owner.
#
# This is deliberately a fail-closed admission test.  A PASS proves that the
# staged IWX PSK+PMF path retains its ownership, epoch, q0, and rollback
# fences.  The exact whitelist is AX211 GF, AX210 TY and AX411 GF4 only; this
# does not claim a functional SAE association.
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

python3 - "$root" <<'PY'
from pathlib import Path
import hashlib
import re
import struct
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
    "iwn_gate": "AirportItlwm/IwnDirectSaeLabGate.hpp",
    "auth": "AirportItlwm/TahoeAssociationAuthContracts.hpp",
    "cpp": "itlwm/hal_iwx/ItlIwx.cpp",
    "hpp": "itlwm/hal_iwx/ItlIwx.hpp",
    "iwxvar": "itlwm/hal_iwx/if_iwxvar.h",
    "iwxreg": "itlwm/hal_iwx/if_iwxreg.h",
    "mfp_contract": "itlwm/hal_iwx/IwxMfpIgtkContracts.hpp",
    "trace_abi": "include/ClientKit/AirportItlwmPostPltiTrace.h",
    "trace_bridge": "include/ClientKit/AirportItlwmPostPltiTraceBridge.h",
}
source = {name: (root / path).read_text() for name, path in paths.items()}


def fail(message):
    raise SystemExit(f"selected AX210-family/API-68 PMF owner contract: {message}")


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


def preprocessor_block(text, marker, label):
    start = text.find(marker)
    if start < 0:
        fail(f"missing {label}")
    depth = 0
    offset = start
    for line in text[start:].splitlines(keepends=True):
        directive = line.lstrip()
        if directive.startswith("#if"):
            depth += 1
        elif directive.startswith("#endif"):
            depth -= 1
            if depth == 0:
                return text[start:offset + len(line)]
        offset += len(line)
    fail(f"unterminated {label}")


def require_categorical_record(text, event, controller, label):
    match = re.search(
        r"AirportItlwmPostPltiTraceRecord\(\s*([^,]+),\s*" +
        re.escape(event) + r"\s*\)",
        text,
    )
    if match is None:
        fail(f"missing categorical trace record for {label}")
    if match.group(1).strip() != controller:
        fail(f"trace record carries non-controller data for {label}")


def macro_decimal(text, macro):
    match = re.search(r"^#define\s+" + re.escape(macro) + r"\s+(\d+)\s*$",
                      text, re.MULTILINE)
    if match is None:
        fail(f"missing decimal macro {macro}")
    return int(match.group(1))


# The selected images use the new TLV header format: the complete header
# value is 68, while the legacy API-byte extraction is zero.  Pin the three
# checked-in images and independently reproduce the loader's prerequisite
# walk so the runtime gate cannot regress to filename or family inference.
iwxreg = source["iwxreg"]
api_changes_tlv = macro_decimal(iwxreg, "IWX_UCODE_TLV_API_CHANGES_SET")
enabled_capa_tlv = macro_decimal(iwxreg, "IWX_UCODE_TLV_ENABLED_CAPABILITIES")
flags_tlv = macro_decimal(iwxreg, "IWX_UCODE_TLV_FLAGS")
new_version_bit = macro_decimal(iwxreg, "IWX_UCODE_TLV_API_NEW_VERSION")
if (api_changes_tlv, enabled_capa_tlv, flags_tlv, new_version_bit) != \
        (29, 30, 18, 20):
    fail("unexpected selected API-68 firmware TLV constants")


def verify_selected_firmware(label, filename, expected_sha256):
    firmware = (root / "itlwm/firmware" / filename).read_bytes()
    if hashlib.sha256(firmware).hexdigest() != expected_sha256:
        fail(f"{label} firmware hash differs from the audited asset")
    if len(firmware) < 0x58:
        fail(f"{label} firmware is shorter than its TLV header")
    header_version = struct.unpack_from("<I", firmware, 0x48)[0]
    if header_version != 68 or ((header_version & 0x0000ff00) >> 8) != 0:
        fail(f"{label} does not exercise the new-format API-68 gate")

    has_new_version = False
    firmware_flags = 0
    has_multi_queue_rx = False
    offset = 0x58
    while offset + 8 <= len(firmware):
        tlv_type, tlv_len = struct.unpack_from("<II", firmware, offset)
        payload = offset + 8
        next_offset = payload + ((tlv_len + 3) & ~3)
        if next_offset > len(firmware):
            fail(f"truncated {label} firmware TLV")
        if tlv_type == api_changes_tlv:
            if tlv_len != 8:
                fail(f"malformed {label} API_CHANGES_SET TLV")
            api_index, api_flags = struct.unpack_from("<II", firmware, payload)
            has_new_version |= (api_index == 0 and
                                (api_flags & (1 << new_version_bit)) != 0)
        elif tlv_type == flags_tlv:
            if tlv_len < 4:
                fail(f"short {label} FLAGS TLV")
            firmware_flags = struct.unpack_from("<I", firmware, payload)[0]
        elif tlv_type == enabled_capa_tlv:
            if tlv_len != 8:
                fail(f"malformed {label} ENABLED_CAPABILITIES TLV")
            capa_index, capa_flags = struct.unpack_from("<II", firmware, payload)
            has_multi_queue_rx |= (capa_index == 68 // 32 and
                                   (capa_flags & (1 << (68 % 32))) != 0)
        offset = next_offset
    if not has_new_version or (firmware_flags & (1 << 2)) == 0 or \
            not has_multi_queue_rx:
        fail(f"{label} lacks NEW_VERSION, MFP, or MQ-RX")


for firmware_args in (
    ("AX211 GF", "iwlwifi-so-a0-gf-a0-68.ucode",
     "08f78a57bd7052e07c2f597a0f22f5cd214eaf9f215912a467cdc2c1a1ae127d"),
    ("AX210 TY", "iwlwifi-ty-a0-gf-a0-68.ucode",
     "6cbc5c1d375e901c7cf57b2151cdb661b39157112884bb7d12ded69c210375eb"),
    ("AX411 GF4", "iwlwifi-so-a0-gf4-a0-68.ucode",
     "665483f7f4d79235dc0c7cb281a32e49c7a7b8154b30e7891f45965698ba875c"),
):
    verify_selected_firmware(*firmware_args)


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
    "int ieee80211_pae_mfp_txn_finish_publish_locked",
):
    require(proto_h, token, "generic PMF owner API")

# The AX211/IWX WCL carrier opts in only for the exact audited PSK PMK route.
# The IWN product compile gate may admit exact SAE password carriers, but it
# must neither reuse this PSK PMF assignment nor make IWX a SAE backend.
# Public/leave/disassociate ingress still clear stale state.
auth = source["auth"]
require(auth, "inline bool requiresUnsupportedWpa3Auth", "pure-SAE gate")
require(auth, "inline bool isAuditedPskPmkAuth", "audited PSK classifier")
sky = source["sky"]
iwn_gate = source["iwn_gate"]
hidden = body(sky, "IOReturn AirportItlwmSkywalkInterface::\nsetWCL_ASSOCIATEImpl",
              "hidden WCL association")
direct_marker = ("#if AIRPORT_ITLWM_IWN_SAE_WCL_INGRESS\n"
                 "    /*\n"
                 "     * The live ingress")
direct_lab = preprocessor_block(hidden, direct_marker,
                                "IWN lab exact-SAE WCL block")
shared_direct = body(sky, "IOReturn AirportItlwmSkywalkInterface::\nstartIwnDirectSaeCredential",
                     "shared IWN direct-SAE transaction")
for token in (
    "ITL_SAE_DRIVER_CRYPTO_AVAILABLE",
    "#define AIRPORT_ITLWM_IWN_SAE_WCL_INGRESS 1",
    "defined(IWN_SOFTWARE_PMF_LAB_BUILD)",
):
    require(iwn_gate, token, "IWN-only compile fence")
for token in (
    '#include "IwnDirectSaeLabGate.hpp"',
    "#define AIRPORT_ITLWM_IWN_SAE_WCL_INGRESS 1",
    "#define AIRPORT_ITLWM_IWN_DIRECT_SAE_LAB_STIMULUS 0",
):
    require(sky if token.startswith('#include') else iwn_gate, token,
            "IWN-only compile fence")
for token in (
    "if (directSaeWclPassword)",
    "startIwnDirectSaeCredential",
):
    require(direct_lab, token, "direct IWN exact-SAE handoff")
for token in (
    "ieee80211_sae_wcl_request_begin",
    "stageSaeWclCredential",
    "ieee80211_sae_wcl_request_resume_scan",
):
    require(shared_direct, token, "shared direct IWN SAE transaction")
if re.search(
        r"const bool directSaeWclPassword\s*=\s*"
        r"wcl_key_cipher\s*==\s*APPLE80211_CIPHER_PWD\s*&&\s*"
        r"TahoeAssociationAuthContracts::mayUseDirectSaeWclCredential\(\s*"
        r"auth_upper\s*\)\s*;", hidden, re.S) is None:
    fail("missing exact CIPHER_PWD-and-auth direct-SAE selector")
for token in (
    "TahoeAssociationAuthContracts::isAuditedPskPmkAuth",
    "TahoeAssociationContracts::pmfCapable(pmf_capability)",
    "publishPendingAssocTarget(",
    "assocResult = associateSSID",
):
    forbid(direct_lab, token, "AX211/PLTI PMF carrier reuse")
legacy_start = hidden.find(
    "if (TahoeAssociationAuthContracts::requiresUnsupportedWpa3Auth(")
if legacy_start < 0:
    fail("missing ordinary WCL PMF admission after IWN lab gate")
ordinary_hidden = hidden[legacy_start:]
order(ordinary_hidden, "ordinary hidden PMF admission",
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
firmware_reader = body(cpp, "int ItlIwx::\niwx_read_firmware",
                       "firmware reader")
order(firmware_reader, "new-format API-68 header retention",
      "sc->sc_fw_header_version = 0;",
      "sc->sc_fw_header_version = le32toh(uhdr->ver);",
      "sc->sc_fw_api = IWX_UCODE_API(sc->sc_fw_header_version);",
      "case IWX_UCODE_TLV_API_CHANGES_SET:")
abi_gate = body(cpp, "static bool\niwx_api68_igtk_v2_ok",
                "selected API-68 ABI gate")
for token in (
    "sc->sc_device_family != IWX_DEVICE_FAMILY_AX210",
    "sc->sc_cfg->device_family != IWX_DEVICE_FAMILY_AX210",
    "sc->sc_fw_header_version",
    "IWX_UCODE_TLV_API_NEW_VERSION",
    "IwxMfpIgtkContracts::hasExactAbiPrerequisites",
):
    require(abi_gate, token, "new-format API-68 ABI admission")
forbid(abi_gate, "sc->sc_fw_api", "legacy-byte API-68 ABI admission")
for config in (
    "iwlax211_2ax_cfg_so_gf_a0",
    "iwlax211_2ax_cfg_so_gf_a0_long",
    "iwlax210_2ax_cfg_ty_gf_a0",
    "iwlax411_2ax_cfg_so_gf4_a0",
    "iwlax411_2ax_cfg_so_gf4_a0_long",
):
    require(abi_gate, f"sc->sc_cfg != &{config}",
            "selected API-68 configuration whitelist")
if abi_gate.count("sc->sc_cfg != &") != 5:
    fail("selected API-68 whitelist is not exactly five configuration objects")
for excluded in (
    "iwlax210_2ax_cfg_so_jf_a0",
    "iwlax210_2ax_cfg_so_hr_a0",
    "iwlax411_2ax_cfg_sosnj_gf4_a0",
    "iwlax211_cfg_snj_gf_a0",
):
    forbid(abi_gate, f"&{excluded}",
           "unreviewed configuration in selected API-68 whitelist")
for token in (
    "kApi68FirmwareHeaderVersion",
    "bool has_new_version_format",
    "firmware_header_version == kApi68FirmwareHeaderVersion",
):
    require(source["mfp_contract"], token,
            "new-format API-68 carrier contract")
runtime = body(cpp, "static bool\niwx_mfp_runtime_enabled",
               "selected API-68 PMF runtime gate")
for token in (
    "iwx_api68_igtk_v2_ok(sc)",
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
# A Group Msg1 without a new IGTK may retain either RX slot across a 4<->5
# overlap.  The owner must ask the BIP core under its leaf lock, rather than
# dereference a table descriptor or k_priv in this ingress worker.
for token in (
    "ieee80211_bip_key_slot_live(ic, 4)",
    "ieee80211_bip_key_slot_live(ic, 5)",
):
    require(group_plan, token, "rekey-without-IGTK retained-slot requirement")
for token in ("old_igtk", "ic_nw_keys[", "->k_priv"):
    forbid(group_plan, token, "rekey-without-IGTK raw descriptor access")

# The generic owner serializes callbacks outside its leaf lock, verifies the
# identity tuple on every completion, and retains its value record through an
# atomic backend finish+software-publication handoff.  A prepared BIP context
# is local until that handoff and is extracted/disposed after any cancellation.
proto = source["proto"]
begin = body(proto, "int\nieee80211_pae_mfp_txn_begin", "PMF begin")
order(begin, "begin callback after lock", "IOSimpleLockUnlockEnableInterrupt(lock, irq);",
      "error = (*submit)")
for token in (
    "submit == NULL || cancel == NULL || finish == NULL",
    "ieee80211_pae_mfp_igtk_shape_valid(igtk_key)",
    "return EOPNOTSUPP;",
):
    require(begin, token, "complete backend owner contract")
abort = body(proto, "void\nieee80211_pae_mfp_txn_abort", "PMF abort")
order(abort, "abort extracts local BIP before backend cancellation",
      "ieee80211_pae_mfp_txn_cancel_locked(ic, &prepared)",
      "IOSimpleLockUnlockEnableInterrupt(lock, irq)",
      "ieee80211_pae_mfp_txn_dispose_prepared(ic, &prepared)",
      "(*cancel)(ic, id)")
complete = body(proto, "void\nieee80211_pae_mfp_txn_complete", "PMF complete")
for token in (
    "txn->id != id", "txn->phase != stage",
    "ieee80211_pae_mfp_txn_live_locked", "txn->assoc_epoch",
    "ieee80211_pae_mfp_txn_cancel_locked", "(*cancel)(ic, id)",
    "(*finish)(ic, id)",
    "final_commit = ieee80211_pae_mfp_txn_prepare_reply",
    "txn->prepared_bip_key = prepared.bip_key;",
    "txn->prepared_bip_installed = 1;",
    "finish_error = (*finish)(ic, id);",
    "if (finish_error == 0 && txn->finish_published)",
    "ieee80211_pae_mfp_txn_dispose_prepared(ic, &cancelled_prepared);",
):
    require(complete, token, "generic completion fence")
order(complete, "completion callback after lock", "IOSimpleLockUnlockEnableInterrupt(lock, irq);",
      "(*cancel)(ic, id)")
handoff = complete[complete.find("if (final_commit == 0) {"):]
for token in (
    "finish = ic->ic_pae_mfp_txn_finish;",
    "(*finish)(ic, id);",
    "finish_error = (*finish)(ic, id);",
    "txn->finish_published",
    "cancel_id = ieee80211_pae_mfp_txn_cancel_locked(ic,",
    "ieee80211_pae_mfp_txn_dispose_prepared(ic, &cancelled_prepared);",
    "if (finish_error != 0 && cancel_id != 0 && cancel != NULL)",
    "if (ieee80211_pae_mfp_txn_live_locked",
):
    require(handoff, token, "atomic generic-to-backend finish handoff")
order(complete, "finish handoff retains epoch cancellation authority",
      "final_commit = ieee80211_pae_mfp_txn_prepare_reply",
      "txn->prepared_bip_key = prepared.bip_key;",
      "finish = ic->ic_pae_mfp_txn_finish;", "(*finish)(ic, id);",
      "if (ieee80211_pae_mfp_txn_live_locked",
      "cancel_id = ieee80211_pae_mfp_txn_cancel_locked(ic,",
      "(*cancel)(ic, cancel_id);")
prepare = body(proto, "static int\nieee80211_pae_mfp_txn_prepare_reply",
               "PMF reply preparation")
forbid(prepare, "ieee80211_pae_mfp_txn_rollback_firmware",
       "generic firmware rollback")
forbid(prepare, "ieee80211_pae_mfp_txn_finish_publish_locked",
       "pre-finish software publication")
for forbidden, label in (
    ("ic->ic_nw_keys[", "pre-finish live-key-table publication"),
    ("ni->ni_port_valid = 1;", "pre-finish port publication"),
    ("IEEE80211_NODE_TXMGMTPROT", "pre-finish MFP flag publication"),
):
    forbid(prepare, forbidden, label)
for token in (
    "rollback_error:", "rollback_cancelled:",
    "ieee80211_pae_mfp_txn_stage_mic_state", "ieee80211_send_4way_msg4",
    "ieee80211_pae_mfp_txn_restore_mic_state",
    "ieee80211_pae_mfp_txn_dispose_prepared",
):
    require(prepare, token, "reply preparation rollback/local-BIP gate")
order(prepare, "reply MIC state is restored before finish",
      "ieee80211_pae_mfp_txn_stage_mic_state", "ieee80211_send_4way_msg4",
      "ieee80211_pae_mfp_txn_restore_mic_state")
forbid(prepare, "ni->ni_rsn_supp_state = RNSA_SUPP_PTKDONE;",
       "pre-handoff PTKDONE publication")
finish_publish = body(proto,
                      "int\nieee80211_pae_mfp_txn_finish_publish_locked",
                      "atomic PMF software publication")
forbid(finish_publish, "ieee80211_delete_key",
       "atomic publication copied-descriptor deletion")
for forbidden, label in (
    ("ieee80211_set_key", "atomic publication allocation"),
    ("ieee80211_bip_reap", "atomic publication retirement reap"),
    ("ic->ic_igtk_kid =", "out-of-helper IGTK-index publication"),
):
    forbid(finish_publish, forbidden, label)
for token in (
    "ieee80211_bip_key_publish_retire_locked",
    "txn->prepared_bip_installed = 0;",
    "ni->ni_ptk = txn->ptk;",
    "ni->ni_pairwise_key = txn->ptk_key;",
    "ic->ic_nw_keys[txn->gtk_key.k_id] = txn->gtk_key;",
    "ni->ni_port_valid = 1;",
    "if (txn->reply == IEEE80211_PAE_MFP_REPLY_4WAY_MSG4)",
    "ni->ni_rsn_supp_state = RNSA_SUPP_PTKDONE;",
    "txn->finish_published = 1;",
):
    require(finish_publish, token, "atomic PMF software publication")
order(finish_publish, "BIP transfer precedes all infallible value publication",
      "ieee80211_bip_key_publish_retire_locked",
      "txn->prepared_bip_installed = 0;",
      "ni->ni_ptk = txn->ptk;",
      "ni->ni_rsn_supp_state = RNSA_SUPP_PTKDONE;",
      "txn->finish_published = 1;")
if proto.count("ni->ni_rsn_supp_state = RNSA_SUPP_PTKDONE;") != 1:
    fail("PTKDONE must be published only by the accepted atomic finish")

# q0 contains value-only async metadata.  Errors are decoded after q0 is
# unlocked; cancellation and timeout preserve the token as TIMED_OUT so that
# a late response can free it without continuing PAE.
iwxvar = source["iwxvar"]
trace_abi = source["trace_abi"]
trace_bridge = source["trace_bridge"]
for token in (
    "kAirportItlwmPostPltiTraceEventIwxMfpPaeRxDelivered = 35",
    "kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0Doorbelled = 36",
    "kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0CompletionObserved = 37",
    "kAirportItlwmPostPltiTraceEventIwxIgtkSlot4Published = 38",
    "kAirportItlwmPostPltiTraceEventIwxIgtkSlot5Published = 39",
    "kAirportItlwmPostPltiTraceEventIwxIgtkSlot4TxSelected = 40",
    "kAirportItlwmPostPltiTraceEventIwxIgtkSlot5TxSelected = 41",
    "kAirportItlwmPostPltiTraceEventIwnMfpPaePtkSoftwarePrepared = 42",
    "kAirportItlwmPostPltiTraceEventIwnMfpPaeGtkSoftwarePrepared = 43",
    "kAirportItlwmPostPltiTraceEventIwnMfpPaeIgtkStageAcknowledged = 44",
    "kAirportItlwmPostPltiTraceEventIwnMfpPaeSoftwareCcmpBipPublished = 45",
    "kAirportItlwmPostPltiTraceEventIwnIgtkSlot4Published = 46",
    "kAirportItlwmPostPltiTraceEventIwnIgtkSlot5Published = 47",
    "kAirportItlwmPostPltiTraceEventIwnIgtkSlot4TxSelected = 48",
    "kAirportItlwmPostPltiTraceEventIwnIgtkSlot5TxSelected = 49",
    "kAirportItlwmPostPltiTraceEventWclPmfRequestRetained = 50",
    "kAirportItlwmPostPltiTraceEventNodeMfpNegotiated = 51",
    "kAirportItlwmPostPltiTraceEventIwnDirectSaeRequestAccepted = 52",
    "kAirportItlwmPostPltiTraceEventIwnDirectSaeCommitTxComplete = 53",
    "kAirportItlwmPostPltiTraceEventIwnDirectSaePeerCommitAccepted = 54",
    "kAirportItlwmPostPltiTraceEventIwnDirectSaeConfirmTxComplete = 55",
    "kAirportItlwmPostPltiTraceEventIwnDirectSaePeerConfirmValidated = 56",
    "kAirportItlwmPostPltiTraceEventIwnDirectSaePmkClaimed = 57",
    "kAirportItlwmPostPltiTraceEventIwnDirectSaeAssocDescriptorAccepted = 58",
    "kAirportItlwmPostPltiTraceEventWclPhysicalScanRequestAccepted = 59",
    "kAirportItlwmPostPltiTraceEventWclPhysicalScanLowerLeaseReserved = 60",
    "kAirportItlwmPostPltiTraceEventWclPhysicalScanTerminalComplete = 61",
    "kAirportItlwmPostPltiTraceEventWclPhysicalScanResultPublicationIssued = 62",
    "kAirportItlwmPostPltiTraceEventWclPhysicalScanTerminalAborted = 63",
    "kAirportItlwmPostPltiTraceEventWclPhysicalScanDonePublicationIssued = 64",
    "kAirportItlwmPostPltiTraceEventMax = 65",
):
    require(trace_abi, token, "append-only PMF observer ABI")
for token in (
    "neither allocate, log, publish",
    "nor inspect or retain frame contents",
):
    require(trace_bridge, token, "safe IWX PMF observer bridge")
for token in (
    "IWX_CMD_ASYNC_OWNER_MFP_PAE", "async_cookie", "async_ack_kind",
    "IWX_CMD_SLOT_TIMED_OUT", "bool task_active;", "bool release_pending;",
    "bool expected_identity_ready;", "bool result_identity_pending;",
    "uint64_t expected_slot_serial;", "uint32_t expected_slot_epoch;",
):
    require(iwxvar, token, "q0/owner state")
send = body(cpp, "int ItlIwx::\niwx_send_cmd", "q0 sender")
for token in (
    "hcmd->async_owner", "hcmd->async_ack_kind", "hcmd->async_cookie",
    "hcmd->async_identity", "hcmd->async_identity->slot_serial = slot_serial",
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
require(cpp, "#include <ClientKit/AirportItlwmPostPltiTraceBridge.h>",
        "IWX PMF observer bridge include")
security_task = body(cpp, "void ItlIwx::\niwx_security_rx_task(void *arg)",
                     "security RX worker")
order(security_task, "PMF RX observer after stale-epoch/BSS gate",
      "entry.assoc_epoch == 0",
      "ieee80211_pae_assoc_epoch_current(ic) != entry.assoc_epoch",
      "ic->ic_bss != entry.ni",
      "AirportItlwmPostPltiTraceRecord",
      "ieee80211_eapol_key_input(ic, entry.m, entry.ni)")
require_categorical_record(
    security_task, "kAirportItlwmPostPltiTraceEventIwxMfpPaeRxDelivered",
    "ic", "PMF RX delivery")
order(send, "PMF q0 observer records only a doorbelled MFP owner",
      "sc->sc_cmdq_slots[idx].state = IWX_CMD_SLOT_SUBMITTED;",
      "IWX_WRITE(sc, IWX_HBUS_TARG_WRPTR",
      "hcmd->async_owner == IWX_CMD_ASYNC_OWNER_MFP_PAE",
      "AirportItlwmPostPltiTraceRecord",
      "unlock:",
      "IOSimpleLockUnlock(sc->sc_cmdq_lock);")
require_categorical_record(
    send, "kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0Doorbelled",
    "&sc->sc_ic", "PMF q0 doorbell")
done = body(cpp, "void ItlIwx::\niwx_cmd_done", "q0 completion")
order(done, "completion observer unlock-before-owner",
      "IOSimpleLockUnlock(sc->sc_cmdq_lock);",
      "unlockTsleep();",
      "async_result_ready",
      "AirportItlwmPostPltiTraceRecord",
      "that->iwx_mfp_pae_q0_done")
require_categorical_record(
    done, "kAirportItlwmPostPltiTraceEventIwxMfpPaeQ0CompletionObserved",
    "&sc->sc_ic", "PMF q0 completion")
require(done, "iwx_async_cmd_ack_error", "q0 decoded PMF completion")
timeout = body(cpp, "void ItlIwx::\niwx_mfp_pae_timeout", "PMF timeout")
order(timeout, "timeout retains q0 token", "txn->result.error = ETIMEDOUT;",
      "that->iwx_mfp_pae_mark_q0_timeout(sc, cookie)")
release = body(cpp, "static void\niwx_mfp_pae_release_transaction", "PMF cancellation")
order(release, "cancellation retains q0 token", "txn->q0_inflight = false;",
      "that->iwx_mfp_pae_mark_q0_timeout(sc, q0_cookie)")
forbid(release, "txn->result_ready = false;",
       "cancellation discarding a terminal q0 result")
for token in (
    "} else if (txn->result_ready) {",
    "!txn->result_identity_pending && !txn->task_active",
    "Do not discard that terminal evidence",
):
    require(release, token, "terminal-result-before-cancel fence")
order(release, "terminal result precedes nonterminal cancellation",
      "} else if (txn->result_ready) {",
      "} else if (txn->operation == IWX_MFP_PAE_OP_DELETE",
      "} else if (txn->q0_inflight || txn->submit_call_active)")
task = body(cpp, "void ItlIwx::\niwx_mfp_pae_task(void *arg)", "PMF serial worker")
for token in (
    "txn->task_active = true;", "txn->cancelled || txn->release_pending",
    "ieee80211_pae_mfp_txn_complete", "} else if (!txn->result_ready) {",
):
    require(task, token, "node lifetime handoff")
order(task, "late ACK result drains before rollback submit",
      "} else if (!txn->result_ready) {", "start_delete = true;",
      "iwx_mfp_pae_submit_delete(sc, txn_id)")
finish_backend = body(cpp, "int ItlIwx::\niwx_pae_mfp_txn_finish",
                      "PMF backend finish")
for token in (
    "(bss_lock = ic->ic_pae_selected_bss_lock) == NULL",
    "IOSimpleLockLockDisableInterrupt(bss_lock)",
    "iwx_mfp_pae_finish_handoff_live_locked",
    "IOSimpleLockUnlockEnableInterrupt(bss_lock, irq)",
    "int handoff_error = ECANCELED;",
    "ieee80211_pae_mfp_txn_finish_publish_locked(ic, txn_id)",
    "txn->finish_requested = true;",
    "return handoff_error;",
):
    require(finish_backend, token, "atomic backend finish handoff")
order(finish_backend, "finish takes association fence before backend owner",
      "IOSimpleLockLockDisableInterrupt(bss_lock)",
      "IOSimpleLockLock(sc->sc_mfp_pae_lock)",
      "iwx_mfp_pae_finish_handoff_live_locked",
      "ieee80211_pae_mfp_txn_finish_publish_locked(ic, txn_id)",
      "txn->finish_requested = true;",
      "IOSimpleLockUnlock(sc->sc_mfp_pae_lock)",
      "IOSimpleLockUnlockEnableInterrupt(bss_lock, irq)")
require(cpp, "generic->phase == IEEE80211_PAE_MFP_STAGE_NONE",
        "finish handoff final generic phase fence")

stop = body(cpp, "void ItlIwx::\niwx_stop_internal", "AX211 stop")
detach = body(cpp, "void ItlIwx::\ndetach", "AX211 detach")
for text, label in ((stop, "stop"), (detach, "detach")):
    require(text, "iwx_mfp_pae_abort_all(sc, true)",
            f"{label} PMF cancellation")
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

    def begin(self, *, iwx_pure_sae=False, requested=True, initial=True,
              includes_igtk=True, group=False):
        assert not iwx_pure_sae, \
            "AX211/IWX PMF model has no pure-SAE ingress"
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


class FinishHandoffModel:
    """The selected-BSS fence owns an all-or-nothing BIP publication."""

    def __init__(self):
        self.generic_live = True
        self.backend_private_live = True
        self.local_bip = True
        self.software_published = False
        self.associated_owner = False
        self.reset_pending = False
        self.events = []

    def _dispose_local_bip(self):
        if self.local_bip:
            self.local_bip = False
            self.events.append("dispose-local-bip")

    def _backend_release(self):
        assert self.backend_private_live
        self.backend_private_live = False
        if self.software_published:
            # An epoch after atomic publication may release only the private
            # PAE owner.  It must not submit reverse key deletes for normal
            # association-owned firmware keys.
            self.events.append("backend-normal-scrub")
        elif self.reset_pending:
            self.events.append("backend-reset-owned")
        else:
            self.events.append("backend-cancel")

    def epoch_cancel(self):
        if self.generic_live:
            self.generic_live = False
            if not self.software_published:
                self._dispose_local_bip()
            if self.backend_private_live:
                self._backend_release()

    def backend_finish(self, accepts):
        # This method models the one selected-BSS-locked backend callback.
        if not self.generic_live:
            self.events.append("finish-observes-cancel")
            return "ECANCELED"
        if not accepts:
            self.events.append("finish-rejected")
            # The real driver requests its authoritative reset before
            # generic cancellation releases the still-local BIP context.
            self.reset_pending = True
            return "EIO"
        assert self.backend_private_live and self.local_bip
        # This is indivisible while selected-BSS is held: BIP leaves the
        # local record only when the normal software association state is
        # published.  No failure remains after this point.
        self.local_bip = False
        self.software_published = True
        self.associated_owner = True
        self.events.append("atomic-publish")
        return 0

    def generic_resume_after_finish(self, error):
        if not self.generic_live:
            self.events.append("generic-observes-epoch")
            return
        if error:
            self.generic_live = False
            self._dispose_local_bip()
            if self.backend_private_live:
                self._backend_release()
            return
        assert self.software_published and not self.local_bip
        # Generic now clears only its value record; the driver's serial owner
        # still holds a node reference until its task returns.
        self.generic_live = False
        self.events.append("generic-clears-record")

    def serial_owner_returns(self):
        assert self.software_published
        if self.backend_private_live:
            self._backend_release()


# Cancellation before the finish fence owns the still-local BIP and backend.
h = FinishHandoffModel()
h.epoch_cancel()
assert h.backend_finish(accepts=True) == "ECANCELED"
assert h.events == ["dispose-local-bip", "backend-cancel",
                    "finish-observes-cancel"]
assert not h.local_bip and not h.software_published
assert not h.backend_private_live and not h.associated_owner

# A rejecting finish cannot publish a partial software association.  Generic
# extracts the local BIP while it still owns its value record; reset remains
# the backend's authoritative firmware-key erase path.
h = FinishHandoffModel()
h_error = h.backend_finish(accepts=False)
assert h_error == "EIO" and not h.software_published
h.generic_resume_after_finish(h_error)
assert h.events == ["finish-rejected", "dispose-local-bip",
                    "backend-reset-owned"]
assert not h.generic_live and not h.local_bip and not h.software_published
assert not h.backend_private_live and not h.associated_owner

# On success, publication precedes generic-record clear and the serial owner
# subsequently scrubs only its private bookkeeping.
h = FinishHandoffModel()
assert h.backend_finish(accepts=True) == 0
assert h.software_published and not h.local_bip and h.associated_owner
h.generic_resume_after_finish(0); h.serial_owner_returns()
assert h.events == ["atomic-publish", "generic-clears-record",
                    "backend-normal-scrub"]
assert not h.generic_live and not h.backend_private_live
assert h.software_published and h.associated_owner

# The narrow race after backend atomic publication but before generic record
# clear is also safe: epoch cancellation releases only the private owner,
# never rolls back the newly normal-lifetime BIP/FW keys.
h = FinishHandoffModel()
assert h.backend_finish(accepts=True) == 0
h.epoch_cancel(); h.generic_resume_after_finish(0)
assert h.events == ["atomic-publish", "backend-normal-scrub",
                    "generic-observes-epoch"]
assert not h.local_bip and h.software_published and h.associated_owner
assert not h.backend_private_live and "backend-cancel" not in h.events


class TerminalResultCancelModel:
    """An ACK before sender return remains the one serial cleanup result."""

    def __init__(self):
        self.result_ready = False
        self.identity_pending = False
        self.late_wait = False
        self.task_queued = False
        self.cancelled = False
        self.delete_started = False

    def ack_before_sender_return(self):
        self.result_ready = True
        self.identity_pending = True

    def cancel(self):
        self.cancelled = True
        if self.result_ready:
            # Preserve terminal q0 evidence; it is not a second late wait.
            if not self.identity_pending:
                self.task_queued = True
            return
        self.late_wait = True

    def sender_returns(self):
        assert self.result_ready and self.identity_pending
        self.identity_pending = False
        self.task_queued = True

    def serial_cleanup(self):
        assert self.task_queued and self.result_ready
        self.task_queued = False
        self.result_ready = False
        if self.cancelled and not self.late_wait:
            self.delete_started = True


# A terminal ACK racing cancel does not get erased merely because its q0 slot
# identity is still being copied back to the sender.
q = TerminalResultCancelModel()
q.ack_before_sender_return(); q.cancel()
assert q.result_ready and q.identity_pending and not q.late_wait
q.sender_returns(); q.serial_cleanup()
assert q.delete_started and not q.result_ready


class LateAckDrainModel:
    """A late ACK queued during an active cleanup gets the next pass."""

    def __init__(self):
        self.task_active = True
        self.result_ready = False
        self.queued = False
        self.delete_started = False
        self.reset = False

    def late_ack(self):
        self.result_ready = True
        self.queued = True

    def first_cleanup_pass(self):
        self.task_active = False
        if self.result_ready:
            return  # The source gate must not submit delete yet.
        self.delete_started = True

    def queued_cleanup_pass(self):
        assert self.queued and self.result_ready
        self.queued = False
        self.result_ready = False
        self.delete_started = True


# No EBUSY→reset path is taken merely because the ACK's worker is queued behind
# the task that timed out/cancelled it.
l = LateAckDrainModel()
l.late_ack(); l.first_cleanup_pass()
assert not l.delete_started and l.queued and not l.reset
l.queued_cleanup_pass()
assert l.delete_started and not l.reset

try:
    PmfModel().begin(iwx_pure_sae=True)
    raise AssertionError("pure SAE unexpectedly admitted into AX211/IWX PMF")
except AssertionError:
    pass

print("PASS: selected AX210-family/API-68 PMF owner and deterministic failure matrix")
PY
