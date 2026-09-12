# Repeated GUI saved-security and recovery controls — 2026-09-12

P0.1 remains all repeated open/WPA2/WPA3 transitions, saved-profile reuse,
and recovery through the actual macOS GUI. This is live IWN/6235 runtime
coverage, not a new driver implementation or an IWM/IWX hardware pass.

## Identity and strict-security prerequisite

Loaded production remains `5e98d640`, UUID
`AF169180-05E8-33CF-960A-166E5DB901BB`, same guest boot
`7A5FAA36-5FAA-451F-A32B-70EB251B8660`. Native WiFiAgent remains PID 5894,
runs 576, after the separately documented preferences-readiness repair.
Physical 10.90.10.22 is untouched; guest USB and host wired management remain
independent. No reboot, API join, saved-profile rewrite or kext replacement
is used in these cells.

OpenWrt is not a uniform-security fixture: its BSSes can negotiate PSK or
SAE. This phase therefore reuses the previously saved, uniquely named
`AIAM-GUI-WPA2-0912`, with hostapd restricted to WPA2/PSK/CCMP and no PMF.
BSSID is `80:e4:ba:20:ef:f9`, channel 9, DHCP 192.168.73.0/24. Host AX211
STA is explicitly excluded from NetworkManager/supplicant for each bounded
AP fixture; cleanup restores the original managed profile. Hardware inventory
shows only one host AP-capable radio and supports one AP interface at a time.
Strict open and WPA2 host fixtures run in separate phases alongside LabAP/SAE;
changing the host fixture is not itself a direct GUI transition.

The first 08:58:29 saved-WPA2 selection fails: hostapd reports an invalid MIC
in message 2/4 and possible PSK mismatch. The laboratory AP was mistakenly
started with the OpenWrt credential instead of this fixture's original lab
credential. Original configuration from the successful 04:40 fixture proves
the discrepancy. First-failure logs and the native error dialog are retained.
macOS automatically falls back to an OpenWrt PSK BSS; that is not a successful
strict-fixture selection. The first AP controller terminates 09:03:59 with
successful host-profile restoration. A separately labeled AP starts 09:04:32
with the original fixture credential. No keychain access or client credential
change is needed. The error dialog is dismissed 09:05:04.

## Actual GUI controls

Each successful local cell requires WPA2/PSK or open readback, exact BSSID,
DHCP BOUND and the correct subnet, then 60 ICMP packets each way with a
1,400-byte payload. A separate HTTP GET must return 200 and exactly 82,944
bytes with SHA256
`1d4065ad48ea2444d90e735735e1a6cc7d315281c890d900641dbd64fbbe82db`.
Paired guest en1 and host AP captures are bounded to 75 seconds and stop
gracefully. These captures start after readiness, not before association;
they do not claim to capture the handshake or first DHCP exchange. Table
action times are VNC controller timestamps; precise native receipt latency
is stated separately below.

| GUI action (UTC) | Result |
| --- | --- |
| 09:05:31 System Settings selects saved strict WPA2 after fixture correction | DHCP .27 by 09:05:42; 60/60 each way; HTTP/hash PASS; no password dialog or off/on |
| 09:07:30 connected WPA2 menu row disconnects; 09:07:52 same row reselected | At 09:07:33 en1/DHCP inactive; DHCP .27 by 09:07:56; 60/60 each way and HTTP/hash PASS |
| 09:09:42 GUI Wi-Fi off; 09:09:58 on, no profile-selection click | Off readback inactive at 09:09:45; automatic strict-WPA2 DHCP .27 by 09:10:12; 60/60 each way and HTTP/hash PASS |
| 09:11:52 connected strict WPA2 -> saved LabAP menu row | Actual SAE/DHCP .219 by 09:11:59; 60/60 each way through the independent routed peer |
| 09:13:37 connected LabAP -> saved strict WPA2 menu row | WPA2/PSK DHCP .27 by 09:13:46; 60/60 each way and HTTP/hash PASS |
| 09:15:21 repeat strict WPA2 -> saved LabAP | SAE/DHCP .219 by 09:15:29; 60/60 each way |
| 09:17:04 repeat LabAP -> saved strict WPA2 | WPA2/PSK DHCP .27 by 09:17:12; 60/60 each way and HTTP/hash PASS |

The LabAP controls bind guest traffic to en1 and use routed peer 10.7.6.112
with 1,200-byte ICMP payload because of that path's MTU. No HTTP result is
claimed for these external WPA3 controls. Guest capture covers en1; the host
AP capture is not on the external LabAP path and is not a paired endpoint
capture for these cells.

All fourteen bounded captures report zero kernel drops.
The initial wrong-credential failure is not erased by these later passes and
does not establish a driver MIC defect. Latency and loss are measured from
each stated readiness/traffic window, not continuously from the GUI click.

Native evidence confirms the WPA2 menu unjoin at 09:07:30.452 reaches
airportd at .463 (11 ms), followed by link-off at .472. GUI power-on is
09:09:58.710 and native DHCP BOUND is 09:10:11.477. Native join entries also
match the four directed WPA2/SAE selections. This series does not reproduce
the old delayed ControlCenter disconnects. It does not erase their earlier
failure records or establish lossless performance in every scan/roam state.

The next eligible phase is open same-profile disconnect/reselect, off/on
starting from open, and repeated open/SAE combinations. The prepared open
configuration in this phase was not started. Full six-edge post-recovery
combinations and repeated same-security/different-profile coverage remain open.
Real S3 is a separate, still-open GUI prerequisite: previous actual wake
recovers Wi-Fi but leaves the framebuffer acknowledgment path stalled. These
awake results cannot qualify post-S3 UI behavior. AP/mixed AP and ad hoc remain
below the user-prioritized ordinary STA repetitions.

At 09:18:51 one final GUI LabAP selection restores the terminal configuration;
readback at 09:19:04 is SAE/DHCP .219 on channel13. This restoration action
has no additional traffic control and is not an eighth service pass. The
strict AP controller terminates 09:19:13 with successful host-profile
restoration, no AP/monitor interfaces, and unchanged wired default. At
09:20:16 the read-only 12-second prerequisite passes again with the same
WiFiAgent PID/runs, readable 0644 preferences, boot and loaded AF16 identity.

Terminal evidence: `/dev/shm/aiam-gui-strict-security-20260912.SMAg0O`.
Durable archive:
`/home/dima/Projects/itlwm/aiam-gui-strict-security-runtime-20260912.bXIHBe`.
All 187 files verify against `EVIDENCE.sha256`, manifest SHA256
`9612f68f5f498299f0e75aacd49f4f1e030426695c8af1ebd18737dc6fbcaca1`.
Archive and RAM source are terminal/immutable. The raw scratch narrative's
09:19:03 terminal estimate is superseded by the 09:19:04 state-log timestamp.
No production source change or new release identity is claimed.
