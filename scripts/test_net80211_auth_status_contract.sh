#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source="$root/itl80211/openbsd/net80211/ieee80211_input.c"

/usr/bin/python3 - "$source" <<'PY'
import pathlib
import re
import sys


def function_body(text, name):
    match = re.search(
        r"\n(?:static\s+)?(?:void|int)\n" + re.escape(name) + r"\([^)]*\)\n\{",
        text,
        re.DOTALL,
    )
    assert match, name
    start = match.end() - 1
    depth = 0
    for index in range(start, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start + 1:index]
    raise AssertionError(f"unterminated {name}")


source = pathlib.Path(sys.argv[1]).read_text()
helper = function_body(source, "ieee80211_record_sta_auth_failure")

guard = (
    "if (ic == NULL || wh == NULL || ni == NULL ||\n"
    "        ic->ic_opmode != IEEE80211_M_STA ||\n"
    "        ic->ic_state != IEEE80211_S_AUTH || ni != ic->ic_bss ||\n"
    "        !IEEE80211_ADDR_EQ(wh->i_addr1, ic->ic_myaddr) ||\n"
    "        !IEEE80211_ADDR_EQ(wh->i_addr2, ic->ic_bss->ni_bssid) ||\n"
    "        !IEEE80211_ADDR_EQ(wh->i_addr3, ic->ic_bss->ni_bssid) ||\n"
    "        seq != IEEE80211_AUTH_OPEN_RESPONSE)\n"
    "        return 0;"
)
assert guard in helper, "status record must be limited to the selected STA AUTH response"
assert "return 1;" in helper, "selected STA AUTH failure record must acknowledge the caller"

actual_failure = "if (status != IEEE80211_STATUS_SUCCESS)\n        ic->ic_assoc_status = status;"
unsupported_algorithm = (
    "else if (algo != IEEE80211_AUTH_ALG_OPEN)\n"
    "        ic->ic_assoc_status = IEEE80211_STATUS_ALG;"
)
assert actual_failure in helper, "real nonzero auth status must remain observable"
assert unsupported_algorithm in helper, "non-open auth with status zero must be truthful"
assert helper.index(actual_failure) < helper.index(unsupported_algorithm)

for forbidden in (
    "ieee80211_new_state",
    "ieee80211_auth_open",
    "IEEE80211_SEND_MGMT",
    "ic_set_key",
    "IEEE80211_AUTH_ALG_SAE",
    "ieee80211_pae_assoc_epoch_begin",
):
    assert forbidden not in helper, f"status-only helper gained {forbidden}"

recv_auth = function_body(source, "ieee80211_recv_auth")
call = "ieee80211_record_sta_auth_failure("
assert recv_auth.count(call) == 2, "both auth rejection paths must record once"

nonopen = recv_auth.index("if (algo != IEEE80211_AUTH_ALG_OPEN) {")
deauth_reset = recv_auth.index("ic->ic_deauth_reason = IEEE80211_REASON_UNSPECIFIED;")
epoch_revoke = "(void)ieee80211_pae_assoc_epoch_begin(ic);"
assert nonopen < recv_auth.index(call) < recv_auth.index(epoch_revoke) < deauth_reset, \
    "unsupported selected-STA algorithm must record and revoke before return"

assoc_reset = recv_auth.index("ic->ic_assoc_status = 0xffff;")
second_call = recv_auth.rindex(call)
auth_open = recv_auth.index("ieee80211_auth_open(ic, wh, ni, rxi, seq, status);")
assert assoc_reset < second_call < auth_open, "open auth failure must replace only the reset sentinel"

print("PASS: net80211 STA auth-status fail-closed contract")


def record_model(*, sta=True, auth=True, selected=True, destination=True,
                 transmitter=True, bssid=True, response=True,
                 algo_open=False, status=0, prior=0xFFFF):
    if not (sta and auth and selected and destination and transmitter and
            bssid and response):
        return False, prior
    if status != 0:
        return True, status
    if not algo_open:
        return True, "ALG"
    return False, prior


# Only the exact selected AP's response may alter the visible result.  Foreign
# BSSID/source, wrong destination, and a non-response sequence remain silent.
for rejected_guard in ("destination", "transmitter", "bssid", "response"):
    kwargs = {rejected_guard: False}
    recorded, result = record_model(**kwargs)
    assert not recorded and result == 0xFFFF, rejected_guard

recorded, result = record_model(algo_open=False, status=0)
assert recorded and result == "ALG"
recorded, result = record_model(algo_open=False, status=77)
assert recorded and result == 77
recorded, result = record_model(algo_open=True, status=0)
assert not recorded and result == 0xFFFF

print("PASS: selected-AP auth-status provenance matrix")
PY
