# System Settings single-BSS WPA2 and saved-SAE recovery — 2026-09-12

The user's P0.1 remains repeated open/WPA2/WPA3, saved networks and recovery.
This cycle uses real System Settings actions on unchanged production
`5e98d640` / UUID `AF169180-05E8-33CF-960A-166E5DB901BB`, guest boot
`7A5FAA36-5FAA-451F-A32B-70EB251B8660`, native WiFiAgent5894/runs576.
No driver install, reboot, daemon restart, CLI/API join or profile/keychain
rewrite occurs. Physical10.90.10.22 and neighboring QEMU are untouched.

## Executed controls

Controlled saved WPA2-only `AIAM-GUI-WPA2-0912` has one BSS,
`80:e4:ba:20:ef:f9`/channel9, DHCP192.168.73.0/24. Correct existing saved
credentials match the preceding strict fixture. The host AX211 STA is
excluded from NM/supplicant during the bounded900-second AP fixture, ready
10:18:02. Host wired default and guest USB management remain independent.
External saved LabAP advertises pure SAE; its actual BSSID/security, not
SSID alone, qualifies the target.

| Actual System Settings action, UTC | DHCP and service result |
| --- | --- |
| lp1 10:18:32 OpenWrt/WPA2 -> different saved WPA2-only profile | WPA2_PSK/.27 by10:18:43; 60/60 each way, HTTP/hash PASS |
| lp2 10:20:36 WPA2-only -> LabAP/SAE | SAE/.219 by10:20:46; forward59/60, reverse60/60; retained FAIL |
| lp3 10:24:12 SAE -> WPA2-only | WPA2_PSK/.27 by10:24:18; 60/60 each way, HTTP/hash PASS |
| lp4 10:25:53 repeated WPA2-only -> SAE | SAE/.219 by10:26:03; 60/60 each way; both complete endpoint streams match |
| lp5 10:28:04 repeated SAE -> WPA2-only | WPA2_PSK/.27 by10:28:11; 60/60 each way, HTTP/hash PASS |
| lp6 10:30:06 Wi-Fi off; 10:30:39 on, no selection click | Automatically restores WPA2_PSK/.27 by10:30:43; 60/60 each way, HTTP/hash PASS |
| lp7 10:32:30 recovered WPA2-only -> SAE | SAE/.219 by10:32:37; forward59/60, reverse60/60; retained FAIL |

All four WPA2 service controls use1400-byte ICMP and HTTP200 with the
82944-byte controlled payload/hash. Initial/final WPA2 radio readbacks retain
the sole BSS/channel9. Local captures cover guest en1 and host AP. External
SAE controls use1200-byte probes to10.7.6.112 with guest and source captures.
All14 captures report zero kernel drops. Captures start after target/DHCP
readiness; no continuous first-packet coverage from the GUI click is claimed.

The off readback10:30:10 has ActiveFALSE, DHCPINACTIVE and no IPv4. Its
last-run radio diagnostic is stale history, not an active association.
Native SET POWER starts10:30:39.730; DHCP BOUND10:30:42.880 is3.150s later.
This actual off/on recovery is not a sleep/wake test.

## First losses, preserved before any subsequent GUI action

lp2 starts on `9a:fb:5d:97:a9:02`/channel13 and ends on
`82:c3:97:84:51:ca`/channel9. Guest forward id55858 sequence23 is emitted
10:21:11.992471 without a reply in guest capture; neighbors22/24 succeed.
Native RSN_HANDSHAKE_DONE10:21:12.226 precedes ROAMED10:21:17.187 and
BSSID_CHANGED10:21:17.193. This is a failed first service interval including
an autonomous same-SSID roam, not a static-BSS failure.

lp2 source filtering only sees the reverse stream. The forward stream is
source-translated, so its absence under a guest-address filter supplies no
forward drop location. A new separately named helper marks subsequent
forward probes with `-p a5`; source BPF admits that marker beyond ping's
timestamp. Actual calibration observes the translated source10.7.6.2.
No production network or driver setting is changed by this observation aid.

lp4 is a separate successful repeat: initial/final BSSID stays on channel13.
Its marked forward id27956 sequences0..59 and reverse id12893 sequences1..60
each have exactly one request/reply in both endpoint captures:240 packets
per endpoint, zero missing/duplicate pairs. Source counters240/241/zero.
This does not retrospectively pass lp2.

lp7, after successful WPA2 off/on recovery, again changes SAE BSS13->9.
Marked forward id36662 sequence24 leaves guest10:33:03.925616, reaches
source-side peer ens18 through NAT10.7.6.2 at10:33:04.119270, and the server
emits its reply at10:33:04.119309. That reply is absent from guest en1.
Neighbors23/25 succeed at both endpoints. Complete matching finds240 source
packets versus239 guest packets, with exactly that missing reply; all60
reverse id12894 requests/replies are present in both captures.
Native RSN completion10:33:04.329 precedes ROAMED10:33:05.196 and
BSSID_CHANGED10:33:05.209. Source counters240/242/zero; guest338/755/zero.

This establishes an emitted server reply missing at observed guest ingress,
not its exact drop point between routed network, AP/RF and guest ingress.
There is no RF capture. The original host AP fixture also expires/restores
at10:33:08 while lp7's independent external-LabAP traffic is running; preserve
that environmental overlap, neither asserting nor excluding its causality.
No further GUI action or recovery occurs during lp7 evidence collection.

## Terminal status and next cycle

Seven service controls: five PASS, two retained forward59/60 failures during
observed SAE BSS changes. This is not two fully lossless round trips or a
complete post-off/on cross-security matrix. No return to WPA2 after lp7 is
executed before fixture expiry. Full six-edge recovery and actual post-S3
GUI remain open; AP mixed-security/ad hoc are not qualified here.

Fixture cleanup returns0 at10:33:08, restores host LabAP/ch153/.226 and leaves
no AP/monitor interface. All controllers/collectors finish. At10:34:57 the12s
native-service prerequisite passes on the same boot/AF16/WiFiAgent5894,
guest LabAP/SAE/ch9/.219. Wired/USB management remains intact.

Next eligible tests use the already restored host Wi-Fi station as a
same-L2 peer, first verifying interface-bound traffic without changing
association/routes/profiles. Then actual saved-SAE recovery/reselection
can retain both endpoints without routed/NAT ambiguity or AP teardown.
Remaining open/WPA2 combinations stay P0.1. Any driver correction must use
the exact reference contract and the observed GUI failure, not infer a
cause solely from RSN/ROAMED timing.

Immutable source: `/dev/shm/aiam-gui-local-pair-recovery-20260912.QqnCv6`.
Durable archive: `/home/dima/Projects/itlwm/aiam-gui-local-recovery-runtime-20260912.lMETmO`.
All171 files verify, `EVIDENCE.sha256` SHA256:
`61c7b1b95db2d2746905c3f0b3aad1b75519c742dccc6d91deb777cc489e6286`.
No production/release bytes change in this runtime-verification cycle.
