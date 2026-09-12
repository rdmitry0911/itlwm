# Repeated GUI security transitions — 2026-09-12

P0.1 actual GUI work after8cdf553d. This cycle measures the current driver;
it does not add production functionality. Loaded UUID remains
`AF169180-05E8-33CF-960A-166E5DB901BB`, installed Mach-O SHA256
`ab06be973d3ae72aa6544b28e834285d2a6e1e02e8b8633c8fb78f14d2b1bf2b`
(production5e98d640). Boot remains
`7A5FAA36-5FAA-451F-A32B-70EB251B8660` throughout the awake round.

## Three simultaneously available saved networks

- Open: AIAM-GUI-OPEN-0912, host AX211 AP BSSID80:e4:ba:20:ef:f9/ch9,
  DHCP192.168.73.34. The host managed STA/supplicant is excluded while the
  fixture owns this radio. Local traffic uses192.168.73.1,1400-byte payload.
- WPA2: OpenWrt, BSSes e4:3a:65:44:e4:dc/ch1 and
  e4:3a:65:44:e4:dd/ch161, DHCP172.16.66.212.
- WPA3: LabAP, BSSes82:c3:97:84:51:ca/ch9,
  9a:fb:5d:97:a9:02/ch13 and82:c3:97:84:51:c9/ch153,
  DHCP172.16.66.219, readback WPA3_SAE.

The protected networks use the independent peer10.7.6.112, since the host
AX211 is serving the open AP. Guest traffic is bound with Darwin ping
`-b en1 -S <actual Wi-Fi IPv4>`; the default management path remains USB en2.
The host wired default is unchanged. Physical10.90.10.22 and the neighboring
QEMU are untouched. No route, MTU, ARP or saved-profile alteration is made.

All transitions use visible Connect buttons in System Settings Known Networks,
with screenshots and native Wi-Fi-settings-extension ASSOC receipts. The three
SSIDs stay available during the q2 round; no intervening anchor network,
password reentry, API/CLI join, hidden off/on or reboot is used.

## First complete six-direction awake round

Each traffic control sends60 ICMP packets in each direction after the actual
target security/BSSID, DHCP and IPv4 address are observed. Protected-network
payload is1200 bytes, below the independently observed routed-path MTU1280.
These are one-minute service controls, not long-duration reliability claims.

| GUI click UTC | Direct transition | DHCP BOUND UTC | Guest→peer / peer→guest |
| --- | --- | --- | --- |
| 07:44:00 | WPA3→WPA2 | 07:44:06.002 | 60/60,60/60 |
| 07:45:40 | WPA2→WPA3 | 07:45:50.871 | 59/60,59/60 |
| 07:47:22 | WPA3→open | 07:47:28.121 | 60/60,60/60 |
| 07:48:56 | open→WPA2 | 07:49:07.559 | 60/60,60/60 |
| 07:50:30 | WPA2→open | 07:50:35.749 | 60/60,60/60 |
| 07:51:52 | open→WPA3 | 07:51:58.232 | 59/60,59/60 |

All six associations and DHCP completions succeed, but the two WPA3-target
cells are not lossless. Their original failures are retained. Both end with
working WPA3 service; a later successful packet is not a recovery action or a
reason to replace the first failed test.

For WPA2→WPA3, forward reply19 and reverse reply51 are absent. The first
target is LabAP/ch13 and the final target LabAP/ch9. Native events show
BEST CONNECTED SCAN07:46:08.017, roam request07:46:15.573 and
ROAMED07:46:22.981. The reverse loss is later, around07:46:44, inside an
ordinary scan07:46:43.308–07:46:46.313. Do not attribute both losses to the
association or to the same roam interval. q2 has no packet capture locating
the drop within the routed path or radio/driver.

The open→WPA3 repeat also loses one packet each way (forward25, reverse25).
Its events show
BEST CONNECTED SCAN07:52:17.063, roam request07:52:20.710 and
ROAMED07:52:27.032. Temporal overlap alone is not root-cause proof.
WPA3→open and open→WPA2 controls pass despite their own scan/roam activity.

## Earlier attempts and checker limitations are preserved

At07:40:23 GUI WPA2→open completes DHCP at07:40:31.682. The initial
checker mistakes transient DHCP BOUND/IsPublished FALSE for address readiness
and stops before traffic. The corrected checker also requires an actual
IPv4 address in the target subnet. Without touching the connection, the
delayed data check passes20/20 each way; it is not continuous first-packet
coverage from the click. q2 repeats this edge with the corrected checker.

At07:41:48 direct open→WPA3 succeeds. Its1400-byte routed traffic measures
20/20 forward and19/20 reverse. The missing reverse packet1 is explicitly
rejected by10.7.6.2 with `Frag needed and DF set (mtu = 1280)`, followed by
19 replies. This particular missing packet is identified PMTU behavior before
the Wi-Fi target, not evidence of driver loss. The original result and old
checker are retained; q2 uses1200 bytes on this route and independently
reproduces different losses. Local open controls remain1400 bytes.

The idle-display gray screen before the round returns to normal after one
ordinary Shift. WindowServer samples a normal event loop; there is no new
system sleep. This is not the separately retained real-S3 framebuffer wake-ack
failure and does not qualify any post-S3 GUI cell.

## Explicit off/on and same-network GUI action

GUI off07:53:34 is confirmed inactive/no IPv4; GUI on07:53:53 automatically
recovers LabAP/WPA3, DHCP BOUND07:54:02.092, with traffic starting07:54:03.
The first1200-byte check
passes60/60 each way on the same boot and loaded image. This does not erase
the earlier cross-profile losses.

The current saved-network ellipsis offers auto-join, copy password, network
settings and forget, but no disconnect. Two separately recorded menu-bar
clicks on the current LabAP row/icon at07:56:08 and07:56:33 initially retain
the connection; the immediate airportd interval has no corresponding
ASSOC/DISASSOC request. Option-click
changes the settings-menu label but does not expose a disconnect action in
this guest. No profile is forgotten or auto-join changed. Therefore these
menu interactions do not qualify as a completed same-network reconnect.
**Later evidence below supersedes any interpretation that the clicks were
harmless no-ops:** two ControlCenter DISASSOC requests arrive much later.

## Repeated six-direction round after explicit off/on

Same saved profiles, same fixture, same boot and payloads; no further power
toggle or reboot. This round includes the intervening menu-bar interaction
above and is not a clean isolation of off/on as the only changed condition.

| GUI click UTC | Direct transition | DHCP BOUND UTC | First service result |
| --- | --- | --- | --- |
| 07:58:13 | WPA3→WPA2 | 07:58:24.957 | 60/60 each way |
| 08:00:00 | WPA2→WPA3 | 08:00:07.560 | 60/60 each way |
| 08:01:29 | WPA3→open | 08:01:37.646 | 60/60 each way |
| 08:03:15 | open→WPA2 | 08:03:22.596 | 60/60 each way |
| 08:04:49 | WPA2→open | 08:04:56.618 | 60/60 each way |
| 08:06:20 | open→WPA3 | 08:06:28.079, then removed | Interrupted by ControlCenter DISASSOC; no traffic window |

The final ASSOC returns success08:06:27.027. DHCP configures172.16.66.219
at08:06:29.681, but ControlCenter PID514 sends DISASSOC at08:06:29.688 and
08:06:29.744. The first request is explicitly user-requested, reason8;
link_off follows08:06:29.698 and IPv4 removal08:06:29.815. The GUI shows
Not connected. Both the initial and unchanged late observer terminate without
a traffic window; no repair click, API join or off/on follows this failure.

ControlCenter's own log records `unjoin: TGFiQVA=` (LabAP) at07:56:09.271
and07:56:33.987, matching the two menu actions. Airportd receives the two
DISASSOC requests only at08:06:29.688 and08:06:29.744. These receipts locate
the delay between the GUI unjoin entry and the service disconnect receipt;
they do not yet establish the queue/owner responsible for the roughly10-minute
delay. A post-drain ControlCenter sample08:10:10 shows
a normal main event loop, not the earlier pending owner. Therefore this cell
does not prove a spontaneous SAE failure. It exposes a GUI-action/lifecycle
dependency and leaves the clean post-off/on open→WPA3 service cell open.
Do not erase it by replacing it with a later successful retry.

The serial tail also contains repeated `ieee80211_encrypt: BUG! key unset
for sw crypto` messages (cipher/flags0). Those lines lack timestamps; their
position does not establish whether they cause this disconnect, result from
key retirement, or belong to another transition. Preserve them as an adjacent
key/TX-lifecycle question, not a proven cause. No speculative key-state fix.

The bounded AP fixture stops08:10:17 and restores the original host managed
LabAP profile successfully, with the wired default unchanged. Guest Wi-Fi
remains disconnected; diagnostic USB management remains reachable.

## Remaining highest-priority coverage

First resolve the observed delayed ControlCenter disconnect path against
existing logs and exact reference/decompilation evidence, then retest the
interrupted direct pair. Continue same-security disconnect/reselect, multiple
profiles of each security and off/on beginning on open/WPA2, followed by
real-S3 GUI recovery. The already reproduced S3 GUI failure stays open;
an awake reboot cannot pass it. The two WPA3-target losses are observed
dependencies for subsequent reference-guided scan/roam/data-path work, not a
reason to skip other eligible repeat-matrix cells. AP/mixed security and ad hoc
remain below this user-prioritized ordinary STA matrix.

Durable evidence:
`/home/dima/Projects/itlwm/aiam-gui-repeat-security-runtime-20260912.59gN8F`.
All336 files verify against `EVIDENCE.sha256`; manifest SHA256
`4100c668cf447955b424714a3a1666be23717f0d7892890412bb8bb896b92f38`.
The archive and original RAM root are immutable. `FINAL_HANDOFF.md` records
the terminal fixture, disconnected guest and next reference-guided GUI task.
No production source or release artifact changes in this evidence cycle.
