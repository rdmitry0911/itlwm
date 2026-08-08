#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

python3 - "$PROJECT_DIR" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
var = (root / "itl80211/openbsd/net80211/ieee80211_var.h").read_text()
crypto = (root / "itl80211/openbsd/net80211/ieee80211_crypto.c").read_text()
pae = (root / "itl80211/openbsd/net80211/ieee80211_pae_input.c").read_text()
proto = (root / "itl80211/openbsd/net80211/ieee80211_proto.c").read_text()
iwx = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()


def section(text, start, end, label):
    try:
        first = text.index(start)
        last = text.index(end, first + len(start))
    except ValueError as exc:
        raise AssertionError(f"{label}: function boundary missing") from exc
    return text[first:last]


def ordered(text, *tokens):
    pos = -1
    for token in tokens:
        pos = text.index(token, pos + 1)


assert "int\t\t\tic_pae_data_key_txn;" in var
assert "ic->ic_pae_data_key_txn = 0;" in crypto

gate = section(pae, "ieee80211_pae_key_txn_enabled(",
               "ieee80211_pae_mfp_msg3_begin(", "generic data-key gate")
for token in (
    "ic->ic_pae_mfp_txn_submit == NULL",
    "ic->ic_pae_mfp_txn_cancel == NULL",
    "ic->ic_pae_mfp_txn_finish == NULL",
    "ni->ni_flags & IEEE80211_NODE_MFP",
    "ic->ic_pae_data_key_txn != 0",
    "ni->ni_rsncipher == IEEE80211_CIPHER_CCMP",
    "ni->ni_rsngroupcipher == IEEE80211_CIPHER_CCMP",
):
    assert token in gate, f"generic data-key gate missing {token}"

plan = section(pae, "ieee80211_pae_mfp_msg3_begin(",
               "ieee80211_pae_mfp_group_begin(", "Msg3 plan")
ordered(plan, "IEEE80211_NODE_RSN_NEW_PTK", "!have_ptk",
        "IEEE80211_NODE_MFP", "gtk == NULL || igtk == NULL")

msg3 = section(pae, "void\nieee80211_recv_4way_msg3(",
               "void\nieee80211_recv_4way_msg4(", "Msg3 ingress")
ordered(msg3, "ieee80211_pae_key_txn_enabled(ic, ni)",
        "ieee80211_pae_mfp_msg3_begin", "return;",
        "memcpy(&ni->ni_ptk", "ieee80211_send_4way_msg4")

group_plan = section(pae, "ieee80211_pae_mfp_group_begin(",
                     "ieee80211_eapol_key_input(", "group plan")
assert "} else if ((ni->ni_flags & IEEE80211_NODE_MFP) != 0)" in group_plan
group = section(pae, "void\nieee80211_recv_rsn_group_msg1(",
                "void\nieee80211_recv_wpa_group_msg1(", "group ingress")
ordered(group, "ieee80211_pae_key_txn_enabled(ic, ni)",
        "ieee80211_pae_mfp_group_begin", "return;",
        "check that key length matches")

base_runtime = section(iwx, "static bool\niwx_pae_key_runtime_enabled(",
                       "static bool\niwx_mfp_runtime_enabled(",
                       "IWX base runtime")
assert "iwx_api68_igtk_v2_ok" not in base_runtime
for token in ("sc_mfp_pae_lock", "sc_cmdq_lock", "sc_task_gate_lock",
              "sc_taskq_initialized", "sc_task_callbacks_ready", "sc_nswq",
              "ic_pae_selected_bss_lock"):
    assert token in base_runtime, f"IWX base runtime missing {token}"

mfp_runtime = section(iwx, "static bool\niwx_mfp_runtime_enabled(",
                      "static void\niwx_publish_mfp_capability(",
                      "IWX MFP runtime")
assert "iwx_pae_key_runtime_enabled(sc) && iwx_api68_igtk_v2_ok(sc)" in mfp_runtime

publish = section(iwx, "static void\niwx_publish_mfp_capability(",
                  "static int\niwx_set_sta_igtk_v2(",
                  "IWX hook publication")
ordered(publish, "if (!iwx_pae_key_runtime_enabled(sc))",
        "ic->ic_pae_data_key_txn = 0", "return;",
        "ic->ic_pae_data_key_txn = 1",
        "ItlIwx::iwx_pae_mfp_txn_submit",
        "if (!iwx_mfp_runtime_enabled(sc))", "return;",
        "ic->ic_caps |= IEEE80211_C_MFP")

rx = section(iwx, "iwx_security_rx_eapol_input(",
             "iwx_scan(", "IWX EAPOL ingress")
for token in ("iwx_pae_key_runtime_enabled(sc)",
              "ni->ni_rsncipher == IEEE80211_CIPHER_CCMP",
              "ni->ni_rsngroupcipher == IEEE80211_CIPHER_CCMP",
              "iwx_security_rx_enqueue"):
    assert token in rx, f"IWX EAPOL ingress missing {token}"

submit = section(iwx, "iwx_pae_mfp_txn_submit(",
                 "const struct iwl_cfg iwlax210_2ax_cfg_ty_gf_a0",
                 "IWX transaction submit")
ordered(submit, "!iwx_pae_key_runtime_enabled(sc)",
        "stage == IEEE80211_PAE_MFP_STAGE_IGTK",
        "!iwx_mfp_runtime_enabled(sc)",
        "(ni->ni_flags & IEEE80211_NODE_MFP) == 0")
ordered(submit, "sta_cmd.common.key_offset = 0",
        "if ((ni->ni_flags & IEEE80211_NODE_MFP) != 0)",
        "IWX_STA_KEY_MFP")

finish = section(proto,
                 "int\nieee80211_pae_mfp_txn_finish_publish_locked(",
                 "static int\nieee80211_pae_mfp_txn_terminal_current(",
                 "generic finish publication")
ordered(finish, "if (txn->have_ptk)", "ni->ni_pairwise_key",
        "if (txn->have_gtk)", "ic->ic_nw_keys",
        "ni->ni_port_valid = 1")


class AckGate:
    """Model only the externally visible invariant, without credentials."""

    def __init__(self):
        self.pending = ["PTK", "GTK"]
        self.port_valid = False

    def ack(self, stage):
        assert self.pending and self.pending[0] == stage
        self.pending.pop(0)
        if not self.pending:
            self.port_valid = True


model = AckGate()
assert not model.port_valid
model.ack("PTK")
assert not model.port_valid, "PTK ACK alone exposed protected data"
model.ack("GTK")
assert model.port_valid, "all data-key ACKs did not release the port"

print("PASS: IWX WPA2-CCMP data port is fenced by PTK/GTK firmware ACKs")
PY
