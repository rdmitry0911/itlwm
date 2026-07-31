#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

python3 - "$PROJECT_DIR" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
ifnet = (root / "itl80211/openbsd/sys/_if_ether.h").read_text()
mbuf_h = (root / "itl80211/openbsd/sys/_mbuf.h").read_text()
mbuf = (root / "itl80211/openbsd/sys/_mbuf.cpp").read_text()
airport = (root / "AirportItlwm/AirportItlwmV2.cpp").read_text()
iwn = (root / "itlwm/hal_iwn/ItlIwn.cpp").read_text()

assert "if_skywalk_rx)(struct _ifnet *, mbuf_t)" in ifnet
assert "if_skywalk_rx_ap)(struct _ifnet *, mbuf_t)" in ifnet
assert "int if_input_ap(struct _ifnet *ifq, struct mbuf_list *ml);" in mbuf_h

dispatch = mbuf[
    mbuf.index("static IOReturn _if_input("):
    mbuf.index("int if_input(struct _ifnet *ifq")
]
assert "apsta ? ifq->if_skywalk_rx_ap : ifq->if_skywalk_rx" in dispatch
assert "else if (!apsta && ifq->iface != NULL)" in dispatch

ap_entry = mbuf[
    mbuf.index("int if_input_ap(struct _ifnet *ifq"):
]
assert "reinterpret_cast<void *>(1)" in ap_entry

rx_bridge = airport[
    airport.index("skywalkRxInputForRole("):
    airport.index("#endif /* __IO80211_TARGET >= __MAC_26_0 */",
                  airport.index("skywalkRxInputForRole("))
]
assert "skywalkRxInputForRole(ifp, m, false)" in rx_bridge
assert "skywalkRxInputForRole(ifp, m, true)" in rx_bridge
assert "apsta ? that->fAPSTARxPool : that->fRxPool" in rx_bridge
assert "apsta ? that->fAPSTARxQueue : that->fRxQueue" in rx_bridge

start = airport[
    airport.index("// Wire up Skywalk RX input handler"):
    airport.index("// Register the interface through the proper Skywalk path.")
]
assert "ifp->if_skywalk_rx = skywalkRxInput;" in start
assert "ifp->if_skywalk_rx_ap = skywalkRxInputAPSTA;" in start

rx_done = iwn[
    iwn.index("iwn_rx_done(struct iwn_softc *sc"):
    iwn.index("iwn_cmd_done(struct iwn_softc *sc")
]
assert "struct mbuf_list *apMl" in rx_done
assert "iwn_handle_ap_data(m, len, apMl, flags, desc->type)" in rx_done

interrupt = iwn[
    iwn.index("iwn_notif_intr(struct iwn_softc *sc)"):
    iwn.index("iwn_wakeup_intr(struct iwn_softc *sc)")
]
assert "struct mbuf_list ml = MBUF_LIST_INITIALIZER();" in interrupt
assert "struct mbuf_list apMl = MBUF_LIST_INITIALIZER();" in interrupt
assert "iwn_rx_done(sc, desc, data, &ml, &apMl);" in interrupt
assert interrupt.index("if_input(&sc->sc_ic.ic_if, &ml);") < \
       interrupt.index("if_input_ap(&sc->sc_ic.ic_if, &apMl);")

print("PASS: Tahoe APSTA RX packets retain their STA/AP queue role")
PY
