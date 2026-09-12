# Saved open-network GUI recovery and repeated SAE transitions — 2026-09-12

P0.1 follows the user's priority: repeated real GUI open/WPA2/WPA3 selections,
saved-profile reuse and recovery, before independent capability or AP work.
This phase follows the strict WPA2 controls documented in
[`TAHOE_GUI_STRICT_SAVED_SECURITY_20260912.md`](TAHOE_GUI_STRICT_SAVED_SECURITY_20260912.md).

## Unchanged production and safe fixture

Guest boot is `7A5FAA36-5FAA-451F-A32B-70EB251B8660`, loaded production
`5e98d640` / UUID `AF169180-05E8-33CF-960A-166E5DB901BB` on IWN/6235.
The 09:25:18 read-only GUI prerequisite passes for native WiFiAgent 5894,
runs 576, with readable 0644 airport preferences. No daemon restart, reboot,
driver install, keychain/profile rewrite or API/CLI join is used.
Physical 10.90.10.22 and neighboring QEMU are untouched.

The controlled host AX211 AP starts 09:27:27 for a nominal 900-second run, using
the previously saved `AIAM-GUI-OPEN-0912`, BSSID `80:e4:ba:20:ef:f9`, channel 9,
no WPA, DHCP 192.168.73.0/24. The same-radio host STA is excluded from
NetworkManager/supplicant for the fixture; cleanup restores its original
managed profile. Host wired default and guest USB diagnostics are independent.
The saved open row is visible in both the actual menu and System Settings
by 09:27:47 while the guest is still on external LabAP/SAE.

The local control requires actual NONE security, the exact BSSID, DHCP BOUND
and subnet, 60 ICMP packets each way with 1,400-byte payload, and HTTP 200
with an 82,944-byte controlled payload. Expected SHA256 is
`1d4065ad48ea2444d90e735735e1a6cc7d315281c890d900641dbd64fbbe82db`.
Guest en1 and host AP captures are bounded to 75 seconds; zero kernel drops
and final security/BSSID/IPv4/DHCP are mandatory. The captures start after
readiness, not before association/DHCP, so first-packet continuity from the
GUI click is not claimed.

For LabAP/SAE, traffic binds to guest en1 and routed peer 10.7.6.112 with
1,200-byte payload. The first SAE control has only guest capture because the
host AP is not on that external data path; the later repeat adds source-side
capture as described below. No HTTP result is claimed for those SAE controls.

## Actual GUI controls

Action times below are VNC controller timestamps. Native command-receipt
latencies, when available, use native log timestamps instead.

| GUI action (UTC) | Result |
| --- | --- |
| 09:28:00 saved open menu row selected directly from LabAP/SAE | NONE security, DHCP .34 by 09:28:10; 60/60 each way; HTTP/hash PASS |
| 09:29:49 connected open menu row disconnected; 09:30:18 same row reselected | Inactive/no DHCP at 09:29:53; NONE/DHCP .34 by 09:30:20; 60/60 each way and HTTP/hash PASS |
| 09:32:13 GUI Wi-Fi off; 09:32:33 on without a network-selection click | Off readback inactive at 09:32:16; automatic NONE/DHCP .34 by 09:32:38; 60/60 each way and HTTP/hash PASS |
| 09:34:28 open -> saved LabAP/SAE | SAE/DHCP .219 by 09:34:35; forward 60/60, reverse 59/60; FAIL lossless service gate, retained |
| 09:39:09 saved open selected from LabAP/SAE after first-loss evidence collection | NONE/DHCP .34 by 09:39:15; 60/60 each way and HTTP/hash PASS |
| 09:41:18 repeat open -> LabAP/SAE | SAE/DHCP .219 by 09:41:27; 60/60 each way; independent reverse-probe capture matches both endpoints |

### First open-to-SAE loss is preserved before any reselection

The reverse routed-peer test misses sequence 17. Guest en1 capture contains
59 echo requests and 59 replies for the peer's ICMP id 12888, with sequence 17
absent in both directions. The guest-initiated id 19498 stream contains all
60 requests and replies, including sequence 17 at 09:34:53.688106/.836851.
Neighboring reverse sequences 16 and 18 are received at
09:34:52.775307 and 09:34:54.841958 and each answered within 45 microseconds.
The capture reports 419 packets captured, 1340 received by filter, zero kernel
drops. These are the actual counters, not a claim that all ingress/air frames
were observed. No source-side or RF capture was present for this first loss.

Native BEST CONNECTED SCAN starts 09:34:52.905, with successive scan requests
and completions around the missing request's interval. This is temporal
overlap, not proof that the scan/driver caused the loss. The current evidence
places the missing request outside the observed guest ICMP request/reply
stream; routed network, AP/RF and driver ingress remain unresolved. Do not
blame the IPv4 responder or declare a driver RX cause from this alone.

The runner exits 1 at its reverse-loss gate. Final state still has SAE,
DHCP .219, BSSID 9a:fb:5d:97:a9:02/channel 13 and WiFiAgent 5894/runs 576.
No GUI action or recovery occurs while the capture and native logs are
inspected. A later neighboring matrix selection is separately labeled and
cannot turn this first attempt into a PASS.

### Subsequent source/guest packet control and terminal state

The next actual open-to-SAE selection adds a bounded 100-second capture on
10.7.6.112/ens18, restricted to ICMP and guest address 172.16.66.219. Its
launch is requested before the GUI click; the packet evidence establishes
coverage from the first probe, not full capture of the preceding join.
The same reverse-probe id 12889 has exactly one request and one reply for
every sequence 1..60 in both source and guest captures. Source sequence 1 is
09:41:29.489456; its final sequence 60 reply is 09:42:28.569539. Sequence 17
also traverses both endpoints. Source capture terminates normally through
the 100-second timeout (controller status 124), with 120 packets captured,
121 received by filter and zero kernel drops. Its filter covers this reverse
probe stream; no source-side coverage is claimed for the separate forward
probe. Guest traffic logs independently report 60/60 each direction.

This repeat does not reproduce or explain away the earlier missing packet.
Next loss attribution needs corresponding source/RF evidence for a failing
event, mapped to the existing scan/roam reference contracts. Keep that
GUI-observed dependency distinct from independent static-parity research.

Native open-network unjoin 09:29:49.886 reaches airportd at .897 (11 ms), then
link-off at .905. GUI power-on 09:32:33.642 reaches native DHCP BOUND at
09:32:36.661. This again exercises the previously repaired GUI-service path.

The original bounded AP controller terminates 09:42:33 with host-profile
restoration result 0, no remaining AP/monitor interface and unchanged wired
default. At 09:44:06 the 12-second read-only prerequisite passes again:
same boot/AF16 image, WiFiAgent 5894/runs 576, readable 0644 preferences, and
LabAP/SAE/DHCP .219 on channel 13. All capture and traffic controllers are terminal.

This phase has six service controls: five PASS and one retained reverse 59/60
failure. It does not complete two full lossless open/SAE round trips; the
second return to open is still unexecuted. The first-loss investigation used
part of the AP's unchanged 900-second window. Remaining ordinary STA work
includes that return, actual-security-checked open/WPA2 repetitions and
repeats through both System Settings and ControlCenter.
Real S3 GUI recovery, the full six-edge recovery matrix and other still-open
cells are not qualified by these awake controls. Earlier failures stay in
their original ledgers; a later pass does not erase them.

Terminal evidence: `/dev/shm/aiam-gui-open-recovery-20260912.KyswIx`.
Durable archive:
`/home/dima/Projects/itlwm/aiam-gui-open-recovery-runtime-20260912.KEtvcn`.
All 120 files verify against `EVIDENCE.sha256`, manifest SHA256
`49331994688c2339c2f61f906fa7dc6c32ea596df2d72c5b3e0534143544067d`.
The archive and original RAM root are terminal and immutable.
No production driver source or release identity changes in this phase.
