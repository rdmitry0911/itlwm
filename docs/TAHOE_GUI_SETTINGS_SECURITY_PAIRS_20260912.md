# System Settings saved-security repetitions — 2026-09-12

P0.1 remains repeated GUI open/WPA2/WPA3, saved networks and recovery.
This phase uses the actual System Settings Connect buttons, independently
of the preceding ControlCenter menu controls. Production remains
`5e98d640`, UUID `AF169180-05E8-33CF-960A-166E5DB901BB`, guest boot
`7A5FAA36-5FAA-451F-A32B-70EB251B8660`. Native WiFiAgent remains 5894/runs576.
No CLI/API join, recovery toggle, daemon restart, reboot or driver install
occurs. Physical 10.90.10.22 and neighboring QEMU remain untouched.

## Fixture and actual controls

The host's active pre-fixture scan sees ten OpenWrt BSSes: eight PSK-only
and two PSK/PSK-SHA256/SAE transition BSSes. Actual negotiated security and
an observed BSSID allowlist, not SSID alone, qualify every WPA2 result.
The controlled saved open AP is `AIAM-GUI-OPEN-0912`,
`80:e4:ba:20:ef:f9`/channel9, DHCP192.168.73.0/24. It is ready09:54:34.
Same-radio host STA is excluded from NM/supplicant while the fixture runs;
wired host default and guest USB management remain independent.

| System Settings action, UTC | Observed result |
| --- | --- |
| sp1 09:55:41 LabAP/SAE -> saved open | NONE/DHCP.34 by09:55:52; 60/60 each way, HTTP200/82944-byte/hash PASS |
| sp2 09:59:05 open -> saved OpenWrt | WPA2_PSK, DHCP.212, e4:3a:65:44:e4:dc/ch1; delayed minute control60/60 each way PASS |
| sp3 10:02:18 WPA2 -> saved open | NONE/DHCP.34 by10:02:23; 60/60 each way, HTTP/hash PASS |
| sp4 10:04:14 repeated open -> OpenWrt | WPA2_PSK/DHCP.212 by10:04:22; forward60/60, reverse59/60; retained FAIL at lossless service gate |

sp2's first readback09:59:12 is still inactive; the next coarse read09:59:51
is connected. Native logs resolve the observation gap: link_on09:59:13.398,
ASSOC completion09:59:13.512 (7.774242234s, origin Wi-Fi/pid881, err0),
DHCP BOUND09:59:16.209, published IPv4/DNS/DHCP success09:59:17.891.
Do not call the coarse46-second gap DHCP latency. Traffic starts after
10:00:09, so this is delayed service coverage, not continuous first-packet
measurement from the GUI click.

Local controls use 1400-byte ICMP and a controlled HTTP payload/hash.
External WPA2 controls use 1200-byte ICMP over routed peer10.7.6.112.
All guest captures are75s; external controls add90s source capture with a
listening-header prerequisite before the probes. Source filtering covers
the reverse probe stream only, not the separate forward stream, RF or the
initial association. Captures and final state are retained before verdict.

## First failed repeat includes an autonomous cross-band roam

sp4 initially selects PSK-only `e4:3a:65:44:e4:dd`/channel161. Its final
radio is `e4:3a:65:44:e4:dc`/channel1, still WPA2_PSK and DHCP.212.
Do not describe this as a static-BSS minute or confuse the same SSID with
an unchanged radio association.

For reverse ICMP id12891, source capture has60 requests and59 replies;
guest en1 has59 requests and59 replies. Missing sequence25 is emitted at
10:04:50.387922 on source ens18 but is absent from guest en1. Guest neighboring
sequence24 is received/answered10:04:49.461803/.461863, sequence26 at
10:04:51.449492/.449531. The independent guest-originated id16944 stream
is60/60; its sequence25 succeeds10:04:50.695984/.763151.

Native RSN_HANDSHAKE_DONE occurs10:04:50.554, then ROAMED10:04:54.408 and
BSSID_CHANGED10:04:54.417. The next GUI live SCAN request starts10:04:51.467.
The missing request overlaps the observed roam/RSN interval. This is not
proof of a specific scan, AP, RF, driver or routed-network drop. The evidence
places it after observed source emission and before observed guest ingress;
there is no RF capture for this event. Source counters are119captured,
121received-by-filter, zero kernel drops; guest372/1058/zero. These counters
do not imply complete lower-layer visibility.

The runner exits1 only after capture and final-state collection. No GUI
selection or reset follows while the first loss is inspected. This adds a
WPA2 failure with source-side evidence; it does not erase the preceding
SAE first loss or establish that both have the same cause. The reference
roam-lifecycle gap remains an observed-GUI dependency, not a substitute for
the other eligible saved-security/recovery cells.

## Terminal state and next eligible cells

The original900-second fixture terminates10:09:40, host restoration result0,
no remaining AP/monitor interface and unchanged wired default. At10:13:26
the12s read-only native-service prerequisite passes on the same boot/AF16,
OpenWrt/WPA2/ch1/.212, WiFiAgent5894. All collectors are terminal.

Four service controls: three PASS, one retained reverse59/60. The planned
second WPA2-to-open return did not execute before fixture expiry; this is
not two complete lossless System Settings round trips. Next are controlled
single-BSS WPA2-only/System Settings repetitions and explicit off/on
cross-profile recovery, with initial/final BSSID changes recorded explicitly.
Full six-edge recovery, post-real-S3 GUI, AP mixed-security and ad hoc remain
open; these awake controls do not qualify them.

Immutable source: `/dev/shm/aiam-gui-ui-pair-repeats-20260912.PSNy6T`.
Durable archive: `/home/dima/Projects/itlwm/aiam-gui-settings-pair-runtime-20260912.tdpaOJ`.
All92 files verify against `EVIDENCE.sha256`, manifest SHA256
`1278927e67e9e524048254a06b6d6477064978020b51fbb9af3ac2bf5978a414`.
No production/release bytes changed.
