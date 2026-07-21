#!/usr/bin/env bash
# Static contract for the BIP publication/retirement boundary.  This is
# intentionally source-level: it runs on a host without the macOS kernel SDK.
set -euo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
fail() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }
need() { rg -Fq -- "$1" "$2" || fail "missing $1 in $2"; }
forbid() { ! rg -Fq -- "$1" "$2" || fail "forbidden $1 in $2"; }

crypto="$root/itl80211/openbsd/net80211/ieee80211_crypto.c"
bip="$root/itl80211/openbsd/net80211/ieee80211_crypto_bip.c"
input="$root/itl80211/openbsd/net80211/ieee80211_input.c"
paein="$root/itl80211/openbsd/net80211/ieee80211_pae_input.c"
paeout="$root/itl80211/openbsd/net80211/ieee80211_pae_output.c"
proto="$root/itl80211/openbsd/net80211/ieee80211_proto.c"
node="$root/itl80211/openbsd/net80211/ieee80211_node.c"
iwx="$root/itlwm/hal_iwx/ItlIwx.cpp"
iwm="$root/itlwm/hal_iwm/mac80211.cpp"
iwn="$root/itlwm/hal_iwn/ItlIwn.cpp"
sae_gate="$root/scripts/test_tahoe_sae_quarantine_contract.sh"
airport_legacy="$root/AirportItlwm/AirportItlwm.cpp"
airport_skywalk="$root/AirportItlwm/AirportItlwmSkywalkInterface.cpp"
airport_ioctl="$root/AirportItlwm/AirportSTAIOCTL.cpp"

for f in "$crypto" "$bip" "$input" "$paein" "$paeout" "$proto" "$node" \
    "$iwx" "$iwm" "$iwn" "$airport_legacy" "$airport_skywalk" \
    "$airport_ioctl"; do
    test -f "$f" || fail "missing source $f"
done
test -f "$sae_gate" || fail "missing SAE/PMF aggregate gate"
need 'bash "$root/scripts/test_net80211_bip_lifetime_contract.sh"' "$sae_gate"

# Context ownership and reaper: no reader-side free, deferred timeout retry,
# early-null-lock terminal unwind, and a terminal scan for live slots.
need 'IEEE80211_KEY_BIP_LOCAL' "$root/itl80211/openbsd/net80211/ieee80211_crypto.h"
need 'ieee80211_bip_key_publish_retire_locked' "$bip"
need 'timeout_add_msec' "$bip"
need 'ieee80211_bip_reap_timeout' "$bip"
need 'ieee80211_bip_lifetime_drain' "$bip"
need 'slot4->k_cipher' "$bip"
need 'slot5->k_cipher' "$bip"
need 'ic_bip_pending_valid' "$bip"
need 'ic->ic_igtk_kid == kid' "$bip"
need 'ic->ic_igtk_kid == k->k_id' "$bip"
need 'A preserved RX-only slot is never a TX claim.' "$bip"
need 'ieee80211_bip_rx_commit' "$bip"
need 'ieee80211_bip_key_is_slot(ic, k)' "$crypto"
need 'panic("ieee80211_crypto_clear_groupkeys BIP %d"' "$crypto"

# The active selector is TX-only.  RX identifies either preserved slot from
# the MMIE; no writer outside the BIP core may race-read/write ic_igtk_kid.
forbid 'ic_igtk_kid' "$paein"
forbid 'ic_igtk_kid' "$paeout"
forbid 'ic_igtk_kid' "$proto"
forbid 'ic_igtk_kid' "$node"
need 'ieee80211_bip_key_slot_live(ic, 4)' "$paein"
need 'ieee80211_bip_key_slot_live(ic, 5)' "$paein"
need 'ieee80211_bip_pending_stage' "$proto"
need 'ieee80211_bip_pending_take' "$proto"
need 'ieee80211_bip_key_install_publish' "$node"
need 'ieee80211_pae_install_igtk' "$paein"
need 'ieee80211_bip_active_key_snapshot' "$paeout"
need 'ieee80211_bip_pending_snapshot' "$paeout"

# Selector domain: IGTK is only for MFP multicast management; all remaining
# group traffic follows GTK.  BIP itself must reject a bad caller, not panic.
need 'IEEE80211_IS_MULTICAST(wh->i_addr1)' "$crypto"
need 'IEEE80211_FC0_TYPE_MGT' "$crypto"
need 'return ieee80211_bip_active_slot(ic)' "$crypto"
need 'kid = ic->ic_def_txkey' "$crypto"
need 'BIP is exclusively a multicast management protection primitive' "$bip"

# No HAL or hardware-decrypt helper may read BIP descriptor fields before its
# pointer identity is routed to generic software BIP.
need 'ieee80211_bip_key_is_slot(ic, k)' "$input"
need 'ieee80211_bip_key_is_slot(ic, k)' "$iwx"
need 'ieee80211_bip_key_is_slot(ic, k)' "$iwm"
need 'ieee80211_bip_key_is_slot(ic, k)' "$iwn"
need 'k = NULL; /* skip hardware crypto below */' "$iwm"
need 'k = NULL;' "$iwn"

# Preserve the Tahoe PMF finish handoff and require final owner drains.
need 'finish_requested || txn->state == IWX_MFP_PAE_COMMITTED' "$iwx"
need 'ieee80211_bip_lifetime_drain(&com.sc_ic)' "$iwx"
need 'ieee80211_bip_lifetime_drain(&com.sc_ic)' \
    "$root/itlwm/hal_iwm/ItlIwm.cpp"
need 'ieee80211_bip_lifetime_drain(&com.sc_ic)' "$iwn"

# Direct-key census.  Every historical route is either provably GTK/WEP-only,
# carries an IGTK only as a local value, or reaches the BIP publisher.  Keep
# these narrow body checks so a later broad refactor cannot silently reopen a
# raw table-4/5 write outside the selected-BSS publication boundary.
python3 - "$node" "$paein" "$paeout" "$proto" "$airport_legacy" \
    "$airport_skywalk" "$airport_ioctl" "$iwx" "$iwm" "$iwn" <<'PY'
from pathlib import Path
import sys


(node, paein, paeout, proto, airport_legacy, airport_skywalk, airport_ioctl,
 iwx, iwm, iwn) = (Path(path).read_text() for path in sys.argv[1:])


def fail(message):
    raise SystemExit(f"BIP direct-key census: {message}")


def require(text, needle, label):
    if needle not in text:
        fail(f"{label}: missing {needle!r}")


def forbid(text, needle, label):
    if needle in text:
        fail(f"{label}: unsafe {needle!r}")


def ordered(text, label, *needles):
    pos = 0
    for needle in needles:
        pos = text.find(needle, pos)
        if pos < 0:
            fail(f"{label}: missing ordered {needle!r}")
        pos += len(needle)


def body(text, marker, label):
    search = 0
    while True:
        start = text.find(marker, search)
        if start < 0:
            fail(f"missing {label}")
        opening = text.find("{", start)
        if opening < 0:
            fail(f"missing opening brace for {label}")
        # Ignore calls and forward declarations: both terminate before their
        # next brace, while the definition's argument list does not.
        if text.find(";", start, opening) < 0:
            break
        search = start + len(marker)
    depth = 0
    for pos in range(opening, len(text)):
        if text[pos] == "{":
            depth += 1
        elif text[pos] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:pos]
    fail(f"unterminated {label}")


def gtk_case(text, label):
    start = text.find("case 0: // GTK")
    if start < 0:
        fail(f"missing {label} GTK case")
    end = text.find("break;", start)
    if end < 0:
        fail(f"unterminated {label} GTK case")
    return text[start:end]


# node.c historical 656/659 WEP loop is bounded below the IGTK key range;
# 964/975 creates GTK slot 1 separately and sends IGTK only as a local key.
set_ess = body(node, "void\nieee80211_set_ess(", "ieee80211_set_ess")
require(set_ess, "i < IEEE80211_WEP_NKID", "set_ess WEP-only loop")
forbid(set_ess, "IEEE80211_KEY_IGTK", "set_ess WEP-only loop")
ibss = body(node, "void\nieee80211_create_ibss(", "ieee80211_create_ibss")
ordered(ibss, "IBSS GTK then local IGTK",
        "ic->ic_def_txkey = 1;", "k = &ic->ic_nw_keys[ic->ic_def_txkey];",
        "(*ic->ic_set_key)(ic, ni, k);", "ieee80211_bip_key_install_publish")
require(ibss, "explicit_bzero(&bip_key, sizeof(bip_key));",
        "IBSS local IGTK wipe")
forbid(ibss, "ic_nw_keys[4]", "IBSS raw IGTK slot")

# pae_input.c 224 is a value-only MFP plan; 831/PTK, 870/1179/1192 GTK, and
# 1281/WPA all remain outside IGTK slots.  The two IGTK fallback paths use
# need-update plus local install/publish rather than raw table mutation.
mfp_group = body(paein, "static int\nieee80211_pae_mfp_group_begin(",
                 "ieee80211_pae_mfp_group_begin")
require(mfp_group, "struct ieee80211_key gtk_key, igtk_key;",
        "MFP group value carriers")
require(mfp_group, "ieee80211_pae_mfp_txn_begin", "MFP group transaction")
forbid(mfp_group, "ic_nw_keys[", "MFP group raw table access")
fourway = body(paein, "void\nieee80211_recv_4way_msg3(",
               "ieee80211_recv_4way_msg3")
ordered(fourway, "4-way GTK route",
        "kid = gtk[6] & 3;", "k = &ic->ic_nw_keys[kid];",
        "ieee80211_pae_set_key(ic, ni, k)")
ordered(fourway, "4-way IGTK publisher",
        "ieee80211_bip_key_needs_update", "ieee80211_pae_install_igtk",
        "explicit_bzero(&bip_key, sizeof(bip_key));")
forbid(fourway, "ic_igtk_kid", "4-way selector bypass")
forbid(fourway, "ic_nw_keys[4]", "4-way raw IGTK slot")
rsn_group = body(paein, "void\nieee80211_recv_rsn_group_msg1(",
                 "ieee80211_recv_rsn_group_msg1")
require(rsn_group, "case IEEE80211_KDE_IGTK:", "RSN IGTK parser")
require(rsn_group, "ieee80211_pae_mfp_group_begin", "RSN async value plan")
ordered(rsn_group, "RSN GTK fallback",
        "kid = gtk[6] & 3;", "k = &ic->ic_nw_keys[kid];",
        "ieee80211_pae_set_key(ic, ni, k)")
ordered(rsn_group, "RSN IGTK fallback publisher",
        "ieee80211_bip_key_needs_update", "ieee80211_pae_install_igtk",
        "explicit_bzero(&bip_key, sizeof(bip_key));")
forbid(rsn_group, "ic_igtk_kid", "RSN group selector bypass")
wpa_group = body(paein, "void\nieee80211_recv_wpa_group_msg1(",
                 "ieee80211_recv_wpa_group_msg1")
ordered(wpa_group, "WPA GTK-only route",
        "kid = (info >> EAPOL_KEY_WPA_KID_SHIFT) & 3;",
        "k = &ic->ic_nw_keys[kid];", "ieee80211_pae_set_key(ic, ni, k)")
forbid(wpa_group, "IGTK", "WPA group IGTK route")

# proto.c 1302--1353 stages a future IGTK off-table, then publishes only
# after all Group Msg2 acknowledgements; GTK's existing 0--3 route is intact.
setkeys = body(proto, "void\nieee80211_setkeys(", "ieee80211_setkeys")
ordered(setkeys, "HostAP off-table IGTK stage",
        "ieee80211_bip_next_kid", "ieee80211_bip_pending_stage")
forbid(setkeys, "ic_nw_keys[bip_kid]", "HostAP raw pending IGTK slot")
setkeysdone = body(proto, "void\nieee80211_setkeysdone(",
                   "ieee80211_setkeysdone")
ordered(setkeysdone, "HostAP IGTK publication",
        "ieee80211_bip_pending_take", "ieee80211_bip_key_install_publish",
        "ieee80211_bip_pending_restore")
forbid(setkeysdone, "ic_igtk_kid", "HostAP selector bypass")

# pae_output.c 422/459/542/573/577 keep GTK as a normal group-key value,
# while each IGTK KDE gets a lock-protected value snapshot and explicit wipe.
msg3 = body(paeout, "int\nieee80211_send_4way_msg3(",
            "ieee80211_send_4way_msg3")
ordered(msg3, "Msg3 GTK and IGTK values",
        "k = &ic->ic_nw_keys[ic->ic_def_txkey];",
        "ieee80211_bip_active_key_snapshot", "ieee80211_add_igtk_kde")
require(msg3, "explicit_bzero(&igtk_key, sizeof(igtk_key));",
        "Msg3 IGTK wipe")
forbid(msg3, "ic_igtk_kid", "Msg3 selector bypass")
group1 = body(paeout, "int\nieee80211_send_group_msg1(",
              "ieee80211_send_group_msg1")
ordered(group1, "Group Msg1 IGTK snapshot",
        "ieee80211_bip_pending_snapshot", "ieee80211_bip_active_key_snapshot",
        "ieee80211_add_igtk_kde")
require(group1, "explicit_bzero(&igtk_key, sizeof(igtk_key));",
        "Group Msg1 IGTK wipe")
forbid(group1, "ic_igtk_kid", "Group Msg1 selector bypass")

# Preserve all three Apple GTK ingress fences before their u8/table paths:
# legacy, Tahoe Skywalk helper, and the old ioctl carrier.
legacy_gtk = body(airport_legacy, "AirportItlwm::setGTK(",
                  "legacy Airport setGTK")
ordered(legacy_gtk, "legacy GTK index fence",
        "kid >= IEEE80211_WEP_NKID", "k = &ic->ic_nw_keys[kid]")
skywalk_gtk = body(airport_skywalk,
                   "AirportItlwmSkywalkInterface::setGTK(",
                   "Skywalk Airport setGTK")
ordered(skywalk_gtk, "Skywalk GTK index fence",
        "kid >= IEEE80211_WEP_NKID", "k = &ic->ic_nw_keys[kid]")
ordered(gtk_case(airport_ioctl, "legacy ioctl"), "legacy ioctl GTK fence",
        "key->key_index >= IEEE80211_WEP_NKID", "setGTK(")
ordered(gtk_case(airport_skywalk, "Skywalk ioctl"), "Skywalk ioctl GTK fence",
        "key->key_index >= IEEE80211_WEP_NKID", "setGTK(")

# A live table descriptor is opaque to every HAL key callback.  The only
# accepted BIP callback carriers are local install values or value-only
# post-unpublish copies; reject pointer identity before the first key field.
for source, marker, label, first_field in (
    (iwx, "int ItlIwx::\niwx_set_key_impl(", "IWX set-key", "k->k_cipher"),
    (iwx, "void ItlIwx::\niwx_delete_key(", "IWX delete-key", "k->k_cipher"),
    (iwm, "int ItlIwm::\niwm_set_key_v1(", "IWM v1 set-key", "k->k_id"),
    (iwm, "int ItlIwm::\niwm_set_key(", "IWM set-key", "k->k_flags"),
    (iwm, "void ItlIwm::\niwm_delete_key_v1(", "IWM v1 delete-key", "k->k_id"),
    (iwm, "void ItlIwm::\niwm_delete_key(", "IWM delete-key", "k->k_flags"),
    (iwn, "int ItlIwn::\niwn_set_key(", "IWN set-key", "k->k_flags"),
    (iwn, "void ItlIwn::\niwn_delete_key(", "IWN delete-key", "k->k_flags"),
):
    callback = body(source, marker, label)
    ordered(callback, f"{label} live-slot fence",
            "ieee80211_bip_key_is_slot(ic, k)", first_field)
PY

model="$root/tests/net80211_bip_retirement_model_test.c"
test -f "$model" || fail "missing BIP retirement model"
need 'test_tx_rx_only_slot_rejected_before_ipn_reservation' "$model"
bin=$(mktemp "${TMPDIR:-/tmp}/aiam-bip-model.XXXXXX")
trap 'rm -f "$bin"' EXIT
cc -std=c11 -Wall -Wextra -Werror "$model" -o "$bin"
"$bin"

printf 'PASS: BIP publication, dual-slot RX, TX selector, reaper, and reader contracts\n'
