# GUI-first connection matrix — 2026-09-12

**P0.1 — repeated open/WPA2/WPA3 combinations, saved networks and recovery
(latest explicit user priority, 2026-09-12).** Test all six directed security
transitions and same-security saved-profile reselection, repeatedly through
the actual GUI. Cover normal awake, explicit Wi-Fi off/on, and real sleep/wake
as separate contexts. Preserve an unavailable post-sleep GUI as an open cell.
Do not substitute automatic fallback or an intermediate network for a claimed
direct transition. Every attempt records actual target, DHCP, first traffic
loss and recovery actions. Individual scan/roam investigations serve failures
in this matrix; they do not displace the other eligible repeated combinations.
AP/mixed-security implementation remains below these ordinary STA combinations.

**P0 — first work item; user priority reaffirmed 2026-09-12.** This matrix
precedes independent protocol, capability, static-parity and roam-carrier
implementation/research. Those layers run first only for an observed GUI-cell
failure or a necessary safety prerequisite. Mixed PSK/SAE AP is a dependency
within this matrix, not a replacement for it. Choose the next eligible cell
by user-facing frequency, retaining ordinary STA connection/reconnection,
saved-network changes and recovery coverage alongside AP and ad hoc.

## Current starting point

Read-only04:12 UTC checks retain guest boot900A4367-43E5-42DC-8DC4-C0662A9E954E
and loaded c123d132/D636A28B-6A9B-3CCE-AF28-5779C5F980C8. en1 reports active
WPA3_SAE, DHCP172.16.66.219 and radio BSSID82:c3:97:84:51:ca/channel9.
The console owner is root/loginwindow, so previous helper-driven tests cannot
be relabelled as interactive GUI tests. A public networksetup query says not
associated while ipconfig and radio state report association; identities in
ipconfig are privacy-redacted. Do not treat that single query as an RF outage.

Evidence root: /dev/shm/aiam-gui-matrix-20260912.Vnadt4.
Physical10.90.10.22 is not accessed. No kext replacement or reboot is needed
to measure the current published image. Wired host management and guest USB
diagnostics remain independent of the tested radios. True S3 Wi-Fi-only
recovery controls temporarily detach diagnostic USB only under their exact
existing device/restore guards.

## Qualification ledger

Each entry requires actual UI action/screenshot, resulting SSID/BSSID/security,
DHCP and bidirectional traffic, with first-attempt latency/loss retained.
Opening the settings pane alone is not a connection test. Never insert a
hidden off/on or API-based join to turn a failed GUI cell green.

Latest P0.1 single-BSS/recovery controls:
[`TAHOE_GUI_SETTINGS_WPA2_SAE_RECOVERY_20260912.md`](TAHOE_GUI_SETTINGS_WPA2_SAE_RECOVERY_20260912.md).
System Settings different-saved WPA2 selection, two SAE-to-WPA2 returns,
one repeated WPA2-to-SAE and WPA2 off/on recovery pass60/60 each way;
all four WPA2 controls also pass HTTP/hash. Native power-on to DHCP BOUND
is3.150s. First WPA2-to-SAE and the later post-off/on WPA2-to-SAE each retain
59/60 forward during SAE ch13-to9 roaming. In the latter, marked endpoint
capture proves the server emitted the missing reply, absent in guest en1.
No exact lower-layer cause is claimed; fixture cleanup overlaps that run.
Seven controls, five PASS/two failures. Next: same-L2 endpoint GUI recovery
controls, keeping remaining cross-security and actual-S3 cells open.

Preceding P0.1 System Settings controls:
[`TAHOE_GUI_SETTINGS_SECURITY_PAIRS_20260912.md`](TAHOE_GUI_SETTINGS_SECURITY_PAIRS_20260912.md).
Actual SAE-to-open, open-to-WPA2 and WPA2-to-open service controls pass60/60
each way (HTTP/hash on open). Repeated open-to-WPA2 joins/gets DHCP but has
60/60 forward,59/60 reverse during autonomous same-SSID channel161-to1 roaming.
Missing reverse seq25 exists in source capture and not guest en1; native
RSN completion overlaps it, followed by ROAMED. Exact lower-layer cause is
not established. Four controls: three PASS, one retained loss; second return
to open unexecuted. Continue single-BSS WPA2/System Settings repetitions and
off/on cross-profile recovery, recording initial/final BSSID separately.

Preceding P0.1 open saved-security control:
[`TAHOE_GUI_OPEN_SAVED_RECOVERY_20260912.md`](TAHOE_GUI_OPEN_SAVED_RECOVERY_20260912.md).
Actual saved open selection, manual disconnect/reselect, and open-start GUI
off/on auto-recovery pass DHCP, 60/60 each way and HTTP/hash. Open unjoin
reaches airportd in 11 ms; power-on to native DHCP BOUND is about 3 seconds.
The first following open-to-SAE control is 60/60 forward but 59/60 reverse:
request17 is absent from guest capture, overlapping BEST CONNECTED SCAN,
without causal attribution. It remains failed. The separately labeled return
to open passes; the later open-to-SAE repeat passes with all 60 reverse
requests/replies matched between source and guest captures. Six controls,
five PASS and one retained loss; not two complete lossless round trips.
The original bounded fixture ends with host restored and guest on LabAP/SAE.
Next are the remaining return to open, actual-security-checked open/WPA2
repetitions and both GUI frontends, retaining the observed loss dependency.

Preceding P0.1 strict saved-security control:
[`TAHOE_GUI_STRICT_SAVED_SECURITY_20260912.md`](TAHOE_GUI_STRICT_SAVED_SECURITY_20260912.md).
After preserving and correcting a wrong-password laboratory AP configuration,
the existing WPA2-only saved profile passes real GUI selection, manual menu
disconnect/reselect and automatic recovery after explicit off/on. Each of
these controls passes DHCP, 60/60 packets each way and an HTTP file/hash
check. Two direct WPA2-only/LabAP-SAE round trips also pass 60/60 each way,
with HTTP/hash on both returns to WPA2, separately from OpenWrt's mixed SSID.
All seven service controls pass; open-start same-profile/off-on controls are
next, while the full six-edge recovery matrix remains open. No driver bytes
change in this runtime-verification cycle; earlier first failures remain in
their ledgers.

Preceding P0.1 GUI-service dependency:
[`TAHOE_GUI_WIFIAGENT_READINESS_20260912.md`](TAHOE_GUI_WIFIAGENT_READINESS_20260912.md).
The native WiFiAgent startup/read-permission defect in the laboratory guest is
repaired without a driver change, reboot or daemon restart. Two actual menu
disconnects now reach airportd in 13 ms; saved LabAP/SAE and OpenWrt/SAE
reconnections each pass DHCP and 60/60 packets both ways. OpenWrt's selected
BSS advertises transition PSK/SAE, so it cannot stand in for a WPA2-only
fixture. Use the new read-only GUI-service prerequisite before further cells.
Explicit off/on from the saved OpenWrt/SAE profile also auto-recovers it in
about 12 seconds; the delayed minute service control is 60/60 each way, with
native WiFiAgent and preferences read access still healthy. This does not
qualify WPA2-only off/on or post-S3 GUI recovery.

Preceding P0.1 control:
[`TAHOE_GUI_REPEATED_SECURITY_MATRIX_20260912.md`](TAHOE_GUI_REPEATED_SECURITY_MATRIX_20260912.md).
With all three saved SSIDs simultaneously available, all six direct GUI
security transitions associate and obtain DHCP on AF16. Four first-minute
controls pass60/60 both ways; both transitions into WPA3 retain59/60 each
way. The later explicit WPA3 off/on recovery passes60/60 both ways. A second
six-edge round has five60/60+60/60 controls; its last open→WPA3 join/DHCP is
interrupted by two delayed ControlCenter DISASSOC requests following earlier
menu unjoin actions. That GUI-action delay is the subsequently diagnosed dependency;
do not classify the interrupted cell as spontaneous SAE failure or pass it.
Same-security and full post-recovery combinations remain incomplete.

Current additional control:
[`TAHOE_GUI_STA_SLEEP_MATRIX_20260912.md`](TAHOE_GUI_STA_SLEEP_MATRIX_20260912.md).
AF16 direct saved WPA3↔WPA2 changes pass DHCP and20/20 each way. Actual S3
again recovers Wi-Fi but stalls the framebuffer/GUI on AF16, as on D636.
After a separately labeled reboot and GUI off/on, saved WPA2 selection has
a retained19/20 reverse first test during a background-scan interval; an
unchanged steady repeat passes20/20. The GUI return to WPA3 similarly has
a19/20 forward first test at scan entry and20/20 unchanged repeat. Do not
label those cells lossless.

| UI path | Normal awake | After real S3 | After UI off/on |
| --- | --- | --- | --- |
| Select saved WPA3 / reconnect | AF16 first fixture join, saved reselect and final LabAP service PASS; full matrix open | GUI unavailable on D636 and AF16; AF16 auto Wi-Fi20/20 each way | AF16 same WPA3 recovery PASS |
| Open network selection / reconnect | AF16 saved selection, manual disconnect/reselect and later SAE-to-open return: DHCP,60/60 each way,HTTP/hash PASS | GUI unavailable on D636 and AF16 | AF16 open-start auto-recovery: native DHCP BOUND about3s, readiness within5s,60/60 each way,HTTP/hash PASS |
| WPA2 selection / reconnect | AF16 strict WPA2-only saved selection and manual disconnect/reselect: DHCP,60/60 each way,HTTP/hash PASS after fixture credential correction | GUI unavailable on D636 and AF16 | AF16 strict WPA2-only auto-recovery within14s: DHCP,60/60 each way,HTTP/hash PASS; earlier reverse19/20 remains retained |
| Open → WPA2 → WPA3 and reverse | All six direct edges measured with simultaneous SSIDs on AF16: DHCP6/6; four60/60+60/60, both WPA3 targets59/60+59/60; repetitions remain open | GUI unavailable on D636 and AF16 | Second six-edge round: five60/60+60/60; open→WPA3 joins/gets DHCP then delayed ControlCenter disconnect interrupts service; not a clean six-edge pass |
| Multiple saved networks / return to prior network | WPA2 and WPA3 target reselect controls PASS; full matrix open | GUI unavailable on D636 and AF16 | AF16 same WPA3 recovery PASS; cross-profile first reverse19/20 retained |
| AP UI enable/disable, external-client service | AF16 GUI WPA2 DHCP/20+20/cold-ARP/NAT PASS after corrected credential entry; WPA3/mixed selector gap open | NOT TESTED | NOT TESTED |
| Ad hoc UI create/join | NOT TESTED | NOT TESTED | NOT TESTED |

An already-active AP across sleep is a separate cell, also NOT TESTED. Prior
native-helper STA and post-S3 AP passes remain useful regression evidence but
do not fill any of these GUI cells.

## First GUI control: post-S3 display is unresponsive

At 04:15–04:16 UTC, before any new reboot, the login screen remained at local
06:09, matching the previous S3 wake. One ordinary VNC click/type/Return
attempt produced a byte-identical screenshot. SSH remained responsive.
This is a failed GUI-availability prerequisite, not a failed password result
or a completed Wi-Fi selection cell. No repeated blind login attempts or
recovery toggle were used to disguise the first failure.

The bounded three-second WindowServer PID 203 sample captured all 274 main
thread samples in:

```text
displayNotification → displayDidWake → IOFBAcknowledgeNotification
  → IOConnectCallMethod → io_connect_method → mach_msg2_trap
```

The loginwindow sample shows an authorization XPC wait and a worker waiting
on WindowServer. A WindowServer UserIsActive assertion names the QEMU USB
Keyboard, so input reached the guest. Exact owned-QEMU monitor checks report
the VM running, with its keyboard and active absolute tablet present.
These userspace samples identify a framebuffer wake-acknowledgment wait;
they do not establish the underlying kernel lock owner or exclude driver
involvement. Do not attribute the fault to Wi-Fi or to graphics without that
additional evidence.

The bounded native screencapture attempt did not complete successfully
(controller exit 255, empty log); it supplies no independent screenshot.
The prior Wi-Fi-only S3 traffic pass remains valid only for that network
measurement. GUI recovery after S3 remains open.

Next work is to preserve/diagnose this exact display wait, then establish an
awake interactive GUI baseline. If the owned guest must be restarted to
restore the display, record a new boot and keep that baseline separate from
this failed post-S3 prerequisite. Physical 10.90.10.22 remains out of scope.

Durable evidence copy:
`/home/dima/Projects/itlwm/aiam-gui-matrix-20260912.oqa9bf`.
All seven files pass `sha256sum -c SHA256SUMS`; manifest SHA-256:
`eb30ba85496e3e9c71b83a5f7b724d642787f994bef4a77508852b6c9f919f3a`.
The before/after PNGs both hash to
`d97e9c4979ad88c4838402ed7aa1ff4cf49289fc730d25de5339c49c1c0dbd41`.
No production source, installed kext, or release artifact changed.

## Awake GUI controls, 04:27–04:57 UTC

After preserving the failure, one graceful reboot of the exact owned guest
restored the display. New boot: `702D27E1-61A2-415C-ACFC-EFB9DC9DCF50`.
Loaded production remained c123d132 / D636A28B-6A9B-3CCE-AF28-5779C5F980C8.
The devops console session was logged in through VNC; all selections below
used the real System Settings Wi-Fi pane. Command-line tools only observed
state/DHCP and generated traffic. No physical .22 access occurred.

Working evidence: `/dev/shm/aiam-gui-awake-baseline-20260912.ol9Hax`.
The controlled host AP used BSSID `80:e4:ba:20:ef:f9`, channel 9,
subnet 192.168.73.0/24. Traffic controls are 20 ICMP packets with 1400-byte
payload in each direction; they prove local data service, not Internet access.

| Actual GUI action (UTC) | First measured result |
| --- | --- |
| 04:32:55 Connect to new AIAM-GUI-OPEN-0912 | DHCP .34 at 04:33:03; 20/20 both ways; PASS |
| 04:34:48 Return to saved LabAP | WPA3, BSSID 9a:fb:5d:97:a9:02/ch13 and DHCP .219 before fixture stop; first peer test after host restoration 14/20 both ways; later unchanged steady test 20/20 both ways |
| 04:40:46 Connect to new AIAM-GUI-WPA2-0912; 04:41:22 enter password | WPA2/PSK, DHCP .27; 20/20 both ways; PASS |
| 04:42:59 Return to LabAP while WPA2 fixture stays up | WPA3/ch13, DHCP .219; guest-to-router 20/20; no independent reverse peer test in this substep |
| 04:44:07 Select saved WPA2 again | No password dialog; DHCP .27; 20/20 both ways; PASS |
| 04:45:01 UI off, 04:45:28 UI on | Off readback inactive/no IPv4; automatic WPA2 recovery, DHCP .27; 20/20 both ways; PASS |
| 04:46:06 Return to LabAP, then same fixture BSSID advertises AIAM-GUI-WPA3-0912 at 04:47:15 | GUI retains old WPA2 name and omits new WPA3 through fresh scans; FAIL before password/join |

The first LabAP loss is retained, not replaced by the later pass. Host profile
restoration/ARP remains a possible confound. The initial automatic LabAP
baseline also measured 20/20 forward, 19/20 reverse. Separately, airportd's
04:35:26 BEST CONNECTED ROAM selected a weaker ch9 BSS despite the connected
ch13 BSS; candidate-policy analysis remains open, not proven as the loss cause.

All three bounded fixtures ended and restored the original host managed
profile. All logstream and DTrace observers are terminal. No hidden power
toggle or API join was used to repair the WPA3 discovery failure.

The display spindump resolves the wake wait through
`IOFramebuffer::extAcknowledgeNotification` / `_extEntry` and
`IOGraphicsControllerWorkLoop::sleepGate` with the exact installed KDK UUID.
This narrows the outstanding GUI-after-S3 prerequisite; an awake reboot is
not a post-sleep GUI pass.

## Active GUI fix: stale former-BSS cache ownership

Live trace identifies the new-WPA3 discovery failure in the driver, not just
the GUI: fresh WPA3 SSID TLVs coexist with the old WPA2 cached SSID because
the former BSSID remains `IEEE80211_STA_BSS` after a transition to SAE.
See `TAHOE_GUI_STALE_BSS_CACHE_20260912.md` for exact evidence, production
correction5e98d640 and negative/positive tests. The AF16 candidate is now
built, installed and runtime-qualified after recreating the full GUI
precondition. WPA3/PMF first join, saved reconnect and UI off/on pass DHCP
and20/20 each way. WPA2/open GUI regressions and final LabAP service also
pass. These AF16 tests are awake tests, not a new S3/GUI qualification.
The existing alpha was updated to5e98d640 at05:41 UTC; public download is
byte-identical to the tested archive (asset558697358,15695095bytes).

Another GUI-observed discrepancy is premature Connected/target identity
behind the WPA3 password dialog while the live radio/DHCP remain on LabAP.
Its driver-versus-userspace ownership is not yet established. AP/ad hoc UI
and post-S3 GUI availability remain open alongside the full combination
matrix; do not let unrelated static work displace these GUI paths.
The actual system-UI WPA2 AP service cell is now qualified on AF16; initial
presentation and credential-entry failures remain in the ledger. The popup's
reference capability also exposes mixed WPA2/WPA3 AP, which the current
authenticator rejects. This is the next GUI-derived functional dependency,
not permission to advertise the bit alone. See
`TAHOE_GUI_AP_SECURITY_MATRIX_20260912.md` for exact controls and boundaries.

AF16 evidence: `/home/dima/Projects/itlwm/aiam-gui-cache-runtime-20260912.gHMjC3`,
251 verified files, manifest SHA256
`85114d00ecc731280dea44e9216e2c4f77814bafec15fc8aedc8dd5d2ff5b129`.

## Preserved roam work

The completed scan-only overlap and reference/lifecycle requirements are in
`TAHOE_ROAM_SCAN_SUPERSESSION_20260912.md`. No production correction has been
made for that conflict. No reference batch or RF observer is being kept alive
as a substitute for starting this GUI matrix.
