#!/usr/bin/env bash
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
SOURCE="$ROOT/itlwm/hal_iwx/ItlIwx.cpp"

python3 - "$SOURCE" <<'PY'
from pathlib import Path
import re
import sys

source = Path(sys.argv[1]).read_text()


def body(name: str, following: str) -> str:
    start = source.index(name)
    end = source.index(following, start)
    return source[start:end]


schedule = body("iwx_schedule_protect_session(struct iwx_softc *sc,",
                "iwx_cancel_session_protection(struct iwx_softc *sc,")
cancel = body("iwx_cancel_session_protection(struct iwx_softc *sc,",
              "iwx_unprotect_session(struct iwx_softc *sc,")
mac_task = body("iwx_mac_ctxt_task(void *arg)",
                 "iwx_chan_ctxt_task(void *arg)")
auth = body("iwx_auth(struct iwx_softc *sc)",
            "iwx_deauth(struct iwx_softc *sc)")

assert ".conf_id = htole32(IWX_SESSION_PROTECT_CONF_ASSOC)" in schedule
assert ".duration_tu = htole32(duration_tu)" in schedule
assert "duration_tu * IEEE80211_DUR_TU" not in schedule
assert schedule.index("iwx_send_cmd_pdu") < schedule.index(
    "sc->sc_flags |= IWX_FLAG_TE_ACTIVE")
assert "if (err == 0)\n        sc->sc_flags |= IWX_FLAG_TE_ACTIVE;" in schedule

assert ".action = htole32(IWX_FW_CTXT_ACTION_REMOVE)" in cancel
assert ".conf_id = htole32(IWX_SESSION_PROTECT_CONF_ASSOC)" in cancel
assert ".duration_tu = 0" in cancel
guard = "if ((sc->sc_flags & IWX_FLAG_TE_ACTIVE) == 0)\n        return 0;"
assert guard in cancel
assert cancel.index(guard) < cancel.index("iwx_send_cmd_pdu")
assert cancel.index("iwx_send_cmd_pdu") < cancel.index(
    "sc->sc_flags &= ~IWX_FLAG_TE_ACTIVE")

assert "that->iwx_cancel_session_protection(sc, in);" in mac_task
assert "that->iwx_unprotect_session(sc, in);" in mac_task
assert mac_task.index("IWX_UCODE_TLV_CAPA_SESSION_PROT_CMD") < mac_task.index(
    "that->iwx_cancel_session_protection(sc, in);")

assert "duration = in->in_ni.ni_intval * 9;" in auth
assert re.search(r"else\s+duration = 900;", auth)

print("PASS: IWX session-protection TU and lifecycle contract")
PY
