# GUI STA matrix: saved profiles and real S3 — 2026-09-12

P0 GUI work, following a9af94c0. No production code, installed kext or release
artifact changes in this cycle. Loaded candidate remains
`AF169180-05E8-33CF-960A-166E5DB901BB`, Mach-O SHA256
`ab06be973d3ae72aa6544b28e834285d2a6e1e02e8b8633c8fb78f14d2b1bf2b`.

Only the owned `aiam-iwn-after-scd-control` laboratory guest is exercised.
Physical 10.90.10.22 and the neighboring QEMU are untouched. Host management
keeps its wired default route. No network selection uses an API/CLI join;
commands read state, generate explicitly Wi-Fi-bound traffic, or control the
separately identified sleep/recovery fixture.

## Direct saved-network changes through System Settings

Boot `870616E6-4A67-4106-947C-F20DEDD08A2B`, devops interactive console.
Both target networks already existed in Known Networks. The controller
clicked each visible Connect button; no password prompt or off/on recovery.

| Actual action / event, UTC | Result |
| --- | --- |
| 06:43:45 initial LabAP/WPA3 baseline | DHCP .219; 20/20 ICMP1400 each direction |
| 06:44:53 GUI Connect OpenWrt/WPA2 | DHCP BOUND at 06:45:04.303, .212; 20/20 each direction |
| 06:46:16 GUI Connect saved LabAP/WPA3 | DHCP BOUND at 06:46:22.832, .219; 20/20 each direction |

The OpenWrt join first selected its 5 GHz BSS on channel161. A later
06:45:26 BEST CONNECTED ROAM selected its 2.4 GHz BSS on channel1. The initial
readback helper only allowed the previously observed channel1 BSSID, so its
traffic measurement started at 06:45:32. This delay is a test-selection
limitation, **not** evidence that DHCP took39 seconds. Both observed BSSes
are accepted for subsequent checks; the original per-second polls remain.

The one-second OpenWrt screenshot already says Connected with a spinner while
LabAP retains its checkmark and the first readback is inactive. This preserves
the earlier premature-target presentation observation; ownership and whether
the reference intentionally presents that transitional state remain unproven.

## Real S3: Wi-Fi recovers, GUI does not

The verified WPA3 baseline precedes sleep. The existing lab fixture temporarily
removes only its diagnostic USB network device and tablet, retains the keyboard,
requests `pmset sleepnow`, and verifies QEMU suspension before `system_wakeup`.
It restores the exact removed devices on every exit. This is a real-sleep
fixture, **not a GUI Sleep-menu test**.

The first controller invocation stopped before mutation because its pidfile
read lacked sudo. The corrected guarded read is retained with both logs.

- Sleep request:06:48:29; guest pmset records Entering Sleep at06:48:59.
- QEMU suspended observed06:49:01 and still suspended after a five-second hold.
- Wake requested06:49:06; guest records Normal Sleep wake06:49:07.
- Wi-Fi SSH returned with the same boot before diagnostic USB restoration.
- Restoration completed06:49:16; host default route unchanged.
- 06:49:29 traffic check: WPA3, DHCP .219,20/20 ICMP1400 each direction.
  USB diagnostics are present again during this traffic check; explicit
  source/interface binding proves the tested traffic path is Wi-Fi.

The System Settings image does not respond to ordinary VNC input. Before,
after input and later06:53:42 captures are byte-identical, SHA256
`88e96e0e135366fe742722067da57b46c968fb554686bcc2ec6ecb03152dc5b1`.
This reproduces the earlier D636 post-S3 GUI failure on the current AF16 image.
No post-S3 GUI network-selection cell is passed or relabeled as a Wi-Fi failure.

All100 samples of WindowServer218's main thread wait in:

```text
displayDidWake -> IOFBAcknowledgeNotification -> externalMethod
  -> IOFramebuffer::extAcknowledgeNotification -> _extEntry
  -> IOGraphicsControllerWorkLoop::sleepGate -> IOGraphicsWorkLoop::sleepGate
```

KDK symbolication matches loaded IOGraphicsFamily599 UUID
`C442ADC7-B277-32A6-B822-5C4D231E0DAC`. The public IOGraphics599 source
(`/home/dima/Projects/apple-oss-distributions/IOGraphics`, commit
`4d3fd008c80d1414c9da8f02ea67d534b47158c2`) shows this `_extEntry` wait guarded
by paging-state/system-power-ack conditions, with `handleEvent` wake paths
restoring paging state. That source is explanatory evidence, not a live read
of those private fields or proof of the missing transition's root cause.

The live framebuffer is AppleBochVGAFB, AppleVirtualGraphics248 UUID
`46A364AC-8493-351D-BE31-FE724517FEDC`. The completed start-method decompile
from `.112`'s `pvg_x86_exhaustive_20260812T160207` has that same input UUID,
verified against the local KDK binary and loaded image. No new heavy decompile
was needed. The extracted reference and KDK thin-file SHA256 values differ;
they are not claimed byte-identical. The framebuffer CurrentPowerState0 also
exists after a successful awake reboot, so that property alone is **not** a
discriminator or proof of the fault. No Apple power-path patch is made.

The airportd/configd observer stops delivering output when its USB transport
is removed. Its later controller exit255 is retained; it supplies no post-S3
event sequence. Post-S3 claims use the independent pmset receipt, QEMU state,
same-boot readback, traffic, screenshots and spindump. Observer PID2885 is
absent and its local session is terminal before recovery.

## Recovery and remaining work

After evidence preservation, a guarded graceful reboot at06:54:45 restores
the guest GUI. New boot `7A5FAA36-5FAA-451F-A32B-70EB251B8660` appears06:55:45;
the same AF16 kext remains loaded. GUI login and Wi-Fi-pane navigation work.
The new-boot LabAP baseline again passes DHCP and20/20 each direction.
This is explicitly reboot recovery, not a post-S3 GUI pass.

### Post-off/on saved-profile control

Actual GUI off06:59:00 is confirmed inactive with no IPv4; on06:59:29
automatically recovers LabAP/WPA3, DHCP .219 and20/20 each way. GUI Connect
saved OpenWrt at07:00:56 then gets DHCP BOUND at07:01:03.676. Its first
traffic check begins07:01:06 and yields20/20 forward but19/20 reverse:
reverse sequence20 is absent, while sequences1–19 succeed. This is not a
first-packet ARP failure. No packet capture was active to locate the loss.

The independent bounded log-show receipt has BEST CONNECTED SCAN beginning
07:01:21.168, BEST CONNECTED ROAM requested07:01:28.078, and ROAMED event
07:01:36.144. The missing final ping falls in the scan interval; temporal
overlap is not proof that the driver scan caused it. The initial20-packet
window does not measure the entire subsequent roam interruption.

Without GUI reselection, off/on, ARP clearing or any other recovery action,
a07:02:18 steady repeat passes20/20 each direction. The19/20 first attempt
is retained, not replaced by the later pass. Next evidence should capture
continuous bidirectional traffic across the full GUI join/background-scan/
roam sequence, with packet timestamps and actual BSS transitions.

The07:03:55 GUI return to saved LabAP gets DHCP BOUND07:04:01.616. Its
first07:04:03 check is19/20 forward (sequence17 times out),20/20 reverse.
BEST CONNECTED SCAN begins07:04:20.550 and roam is requested07:04:27.152.
This second loss is likewise close to scan entry, but has no packet-level
causal attribution. An unchanged07:04:50 steady repeat passes20/20 each way.
Final guest state is awake GUI LabAP/WPA3,.219; host remains on its original
managed LabAP profile,.226, with unchanged wired default route.

The full GUI matrix remains open: repeated open/WPA2/WPA3 profile combinations,
post-off/on cross-profile loss, post-S3 GUI availability, AP security
selection/service, AP across sleep and ad hoc. The mixed PSK/SAE AP dependency
remains preserved, not implemented or advertised by this cycle.

Durable evidence root:
`/home/dima/Projects/itlwm/aiam-gui-sta-sleep-runtime-20260912.aXf3CU`.
All212 files verify against `EVIDENCE.sha256`; manifest SHA256:
`326c516f2d3c20811b768a719c8a476c859aff6df5dd2933adeb182f8a38092a`.
All controls are terminal. `FINAL_HANDOFF.md` records the final state and next
continuous-traffic GUI cell. The archive is immutable; future controls use a
fresh scratch root. No release update is appropriate for this evidence-only
commit: the installed and public AF16 artifact is unchanged.
