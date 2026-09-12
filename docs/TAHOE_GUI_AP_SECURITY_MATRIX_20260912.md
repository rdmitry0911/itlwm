# GUI AP service and security selection — 2026-09-12

## Result and scope

Actual System Settings Internet Sharing now has a qualified awake WPA2 AP
service control on the published AF16 image. The SSID, channel and final
password were entered in the real GUI; enable, confirmation and stop were
also GUI actions. No command-line AP configuration/start/stop filled this
cell. The external AX211 completed WPA2/CCMP, DHCP, bidirectional traffic,
cold ARP and HTTP through standard Internet Sharing NAT.

This does **not** close the full GUI AP matrix. The security chooser exposes
only disabled WPA2 Personal. The reference path for enabling its WPA3 and
WPA2/WPA3 choices has been identified, and the mixed authenticator remains a
real implementation prerequisite. AP after real S3 and the complete repeated
role/security matrix remain open. No production source, installed kext or
release artifact changed in this qualification cycle.

Guest boot: `870616E6-4A67-4106-947C-F20DEDD08A2B`.
Loaded kext UUID: `AF169180-05E8-33CF-960A-166E5DB901BB`, production5e98d640.
Only the owned IWN/6235 guest was exercised. Physical10.90.10.22 was not
accessed. Guest USB diagnostic en2 and host wired management/default route
were retained independently of both tested Wi-Fi roles.

## First attempts are retained

Working evidence: `/dev/shm/aiam-gui-ap-20260912.R58iwT`.
Durable copy: `/home/dima/Projects/itlwm/aiam-gui-ap-runtime-20260912.lwbAwG`.
All117 files pass the archived`EVIDENCE.sha256` verification. Manifest SHA256:
`12bddd30037527483b47e1f0c525dae294cfbd188c0fb2e0688132a99cd54164`.

- At05:48:58, the first Wi-Fi Options click queued an AppKit sheet behind
  the parent Internet Sharing sheet. A bounded Sharing sample showed its
  normal AppKit event loop, not a driver wait. The log explicitly records
  `queue sheet`. Clicking the parent's Done button exposed the pending
  options at05:52:27. This initial presentation problem is not erased by
  later ordinary openings.
- The inherited helper-created configuration was Open/channel9. The first
  GUI dialog showed disabled WPA2 Personal, initially no password fields,
  and offered channels1/6/11/36/40/44/48. Saving through the GUI changed the
  stored security to WPA2 Personal. The initial attempted command-key SSID
  replacement appended text; that fixture-entry mistake was corrected in
  the GUI before the first RF test. Final test SSID: `aiam-gui-ap-0912`.
- At06:04:45, GUI confirmation started WPA2 AP/channel11. The external scan
  found BSSID`ce:f7:33:f4:97:4b`, CCMP/PSK, and standard bridge100 appeared
  at192.168.2.1. Clientq1 associated but timed out in the four-way handshake.
  Its known test password had10bytes; the native driver carrier had13bytes,
  and the authenticator rejected M2 with a MIC mismatch. This is not a
  qualified service pass or proof of a broken WPA2 authenticator.
- The password field subsequently became visible in the same Sharing
  process, without quitting System Settings or a Wi-Fi recovery toggle.
  There is no continuous measurement proving its precise appearance time
  or cause. A second password-edit attempt using control-key deletion left
  part of the old secure-field contents: carrier17bytes, client10bytes,
  another MIC mismatch. Clientq2 is an invalid positive fixture, not a
  driver-failure claim. Both failed attempts and screenshots remain.
- The final edit first erased the secure field with ordinary Backspace;
  a screenshot proves it empty and OK disabled. Typing the known10-byte
  password then enabled OK. The next native carrier independently reports
  credential length10, and its M2/M3/M4 completion agrees with the external
  client's successful WPA2 negotiation. No key bytes were read from kernel
  memory or replaced through a diagnostic API.

## Qualified awake WPA2 GUI control

GUI password entry/save:06:23:00 UTC. GUI AP confirmation:06:23:30 UTC.
External clientq3 began06:23:37 and its bounded fixture ended06:24:51.

| Check | Observed result |
| --- | --- |
| External negotiation | `WPA2-PSK`, pairwise/group CCMP, COMPLETED,2462MHz |
| DHCP |192.168.2.3/24 from the guest's standard sharing service |
| Client → AP |20/20 ICMP,1400-byte payload, no duplicates |
| AP → client after deleting its exact ARP entry |20/20 ICMP,1400-byte payload |
| Independent cold-ARP capture | Guest request, AX211 reply, then first ICMP exchange |
| NAT | HTTP200,118-byte body byte-identical to the independent upstream fixture |
| Host restoration | Original managed LabAP profile restored; wired default unchanged |

The fixture derives the actual client DHCP address rather than assuming
the previous192.168.2.2 lease. The HTTP route was a bounded host-only
10.0.6.2/32 route via the guest AP and was removed on exit. The scoped HTTP
server and packet captures are terminal. The new host test profile
`69bb2b0e-440e-4dcc-8c85-2a47a5ec8096` remains saved with autoconnect disabled.

GUI stop at06:25:46 removed bridge100 and resumed STA. The first diagnostic
incorrectly assumed the old LabAP IPv4 address would persist; its failed
bind and probes to that stale address are retained, not called a traffic
failure of the new association. Readback shows automatic selection of a
different saved WPA2 BSS, `e4:3a:65:44:e4:dc`, with private MAC
`2a:e1:c4:d9:2e:f4`; DHCP ACK for172.16.66.212 was at06:25:55. This proves
STA/DHCP recovery, not restoration of the exact original LabAP profile.
The subsequent UI readback names this saved network OpenWrt. Using its
actual172.16.66.212 address, independent traffic passes20/20 in both
directions without a recovery toggle.
At06:30:54, actual GUI selection of saved LabAP restored WPA3_SAE and
172.16.66.219 (DHCP ACK06:31:00). Fresh independent tests again pass20/20
both ways. The final baseline is LabAP STA, sharing off, same boot/AF16.

## Reference-derived next implementation layer

The live CoreWLANKit image UUID is
`7B0E6C15-F217-3801-B5E9-44E5B87E945D`. Its system `dyld_info` disassembly
and bounded read-only runtime constant resolver are retained in the evidence.
Existing CoreWLAN decompile on10.7.6.112 agrees with the exact system
disassembly of `-[CWWiFiClient(Private) platformCapabilities]` at
`0x7ff8115a7652`.

`CWHostAPDialog_SL` obtains `_caps` from
`+[CWWiFiClient platformCapabilities]`, not directly from a guessed hardware
generation. `populateSecurityTypes` tests platform bit21 (`0x200000`):

- Without it, the dialog supplies WPA2 Personal/tag5 and disables the popup.
- With it, the dialog also supplies WPA2/WPA3/tag15 and WPA3/tag14; its
  default selection becomes the mixed mode. An additional platform-policy
  check controls whether the WPA2-only item is included in that branch.
- The corresponding CoreWLAN security values are0x80,0x1080 and0x1000.
  No Open item is added by these reference branches.

The independent same-getter query returns`0x148000`, bit21 clear. CoreWLAN
sets bit21 when its underlying capability enumeration contains`0x50`.
The retained25C56 raw Broadcom producer at`0xffffff80015c4e75` checks feature
flag0x4b and sets carrier`+0x0e` bit0x01, i.e. public capability byte10 bit0x01.
Do not confuse this with byte10 bit0x08, the separate hazardous LQM gate.

The local common capability cluster currently leaves byte10 clear. More
importantly, `AirportItlwmAPSTAOwner::setHostAPMode` accepts only Open,
WPA2-PSK and pure SAE auth masks, and the shared HAL runtime likewise
recognizes only those three configurations. Advertising the GUI capability
alone would expose a mixed-mode default that the driver still rejects.

The next P0 implementation must therefore handle the actual mixed PSK/SAE
AP path, including its RSN/PMF policy and per-peer authentication, before
publishing the matching capability. It must cover the shared IWN/IWM/IWX
paths and negative inputs, then build/install the exact candidate and repeat
the actual GUI choices with external WPA2 and WPA3 clients. Pure-SAE helper
passes must not be substituted for this mixed-mode or GUI qualification.

An airportd log also refuses a Sharing request lacking`com.apple.wifi.hostap`;
the signed system Sharing extension indeed lacks that entitlement. This is
separate from the now-resolved reference condition for the disabled popup.
Its role in delayed configuration/password presentation is not established.
Do not patch platform entitlements or weaken a security boundary to hide it.
The25-second UI trace recorded zero matching calls and zero errors; it is
not evidence of which presentation branch ran. No heavy decompile was run
in this cycle; any new heavy batch must use the requested40 workers.
