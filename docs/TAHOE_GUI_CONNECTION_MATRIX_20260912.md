# GUI-first connection matrix — 2026-09-12

User priority: this is the first functional layer, ahead of independent roam
carrier implementation/research. The latter remains an identified dependency
to revisit when a GUI control exercises it, not a reason to postpone the UI.

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

| UI path | Normal awake | After real S3 | After UI off/on |
| --- | --- | --- | --- |
| Select saved WPA3 LabAP / reconnect | NOT TESTED | NOT TESTED | NOT TESTED |
| Open network selection / reconnect | NOT TESTED | NOT TESTED | NOT TESTED |
| WPA2 selection / reconnect | NOT TESTED | NOT TESTED | NOT TESTED |
| Open → WPA2 → WPA3 and reverse | NOT TESTED | NOT TESTED | NOT TESTED |
| Multiple saved networks / return to prior network | NOT TESTED | NOT TESTED | NOT TESTED |
| AP UI enable/disable, external-client service | NOT TESTED | NOT TESTED | NOT TESTED |
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

## Preserved roam work

The completed scan-only overlap and reference/lifecycle requirements are in
`TAHOE_ROAM_SCAN_SUPERSESSION_20260912.md`. No production correction has been
made for that conflict. No reference batch or RF observer is being kept alive
as a substitute for starting this GUI matrix.
