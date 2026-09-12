# Native GUI service readiness and delayed disconnects — 2026-09-12

The laboratory guest had a broken native WiFiAgent initialization prerequisite.
Repairing only the airport preferences file's read permissions restores the
service and prompt ControlCenter disconnect delivery, without changing the
kext, rebooting, restarting a daemon, or toggling Wi-Fi. This is an environment
repair, not a new driver feature or a full GUI-matrix pass.

The new read-only prerequisite is:

```sh
bash scripts/check_tahoe_gui_service_readiness.sh 12
```

Run it as the logged-in console user. It checks that the airport preferences
plist is readable and valid, and that the actual native WiFiAgent retains the
same PID for at least 12 seconds. It neither repairs permissions nor restarts
services. A passing prerequisite is not an association/traffic test. Other
files, especially the root-only known-networks store and keychains, are not
permission-repair targets.

## Reference and live cause

The earlier delayed-disconnect failure is retained in
[`TAHOE_GUI_REPEATED_SECURITY_MATRIX_20260912.md`](TAHOE_GUI_REPEATED_SECURITY_MATRIX_20260912.md).
Its ControlCenter logs contain consecutive 80-second diagnostic timeouts before
the two delayed DISASSOC requests. The new 08:23:49 sample catches all 2,297
samples of the `WiFiImpl` serial queue waiting in
`CWWiFiUIProxyClient::queryDiagnosticsAndReturnError:`. The main UI event loop
is responsive. System Settings shows connected LabAP while ControlCenter's
menu has no network rows. No unjoin click is made in this first observation:
the intended row is absent.

The recovered chain is:

```text
airport preferences unreadable to the console user
  → CWWiFiAgent::init returns nil before creating its XPC listener
  → native WiFiAgent exits and launchd repeatedly starts it
  → ControlCenter WiFiImpl waits on diagnostic requests (80 seconds each)
  → queued UI updates/actions are delivered much later
```

The last link is supported by the retained earlier GUI-action/service receipts
and the observed queue wait. There was no pending-period stack sample at the
original 07:56 clicks; do not invent per-request identities for that interval.

Read-only `launchctl` reports 535 runs, last exit code 0, and a scheduled spawn
at 08:26:46. Native logs show initialization failures and orderly exits about
every 10 seconds, not process crashes. Bounded filesystem observation catches
`access(R_OK)` returning EACCES for the exact airport preferences plist twice
during initialization. It is `0600 root:wheel`; the process UID is 502.

The exact guest WiFiAgent universal binary has SHA256
`ed051b6efb8bcd1fbcd3d876ba7abd248ce2f6a9375c62dce74dd3ec589719f0`;
its x86_64 UUID is `74FEBCF1-3D42-357F-9626-FC9ED7777B0D`, thin SHA256
`42d4430ff134e1d203da4d114620a0a5fed8f1d6bf5ea438f02d2bc8ebb12d99`.
The bounded Ghidra batch uses the validated 5995e24caa tool overlay with
40 requested/created decompiler interfaces. All 963 submitted entries complete;
that count includes external stubs, not 963 independent behavioral contracts.
The retained complete init body at `0x100009f80` requires
`SCPreferencesCreate(..., "com.apple.airport.preferences.plist")` to succeed
before constructing its Mach-service listener. The application delegate at
`0x100001440` terminates when init returns nil.

Reference package on 10.7.6.112:
`/home/dima/Projects/ghidra_output/aiam-wifiagent-init-20260912.e6CX3r`.
The existing CoreWLAN package supplies the diagnostic wait and reply handlers.
Its input UUID `46EBC316-D89C-3CAC-B4BB-CCF84A87DA68` matches the loaded
framework. `CWInterface::disassociate` has a different ordinary 4-second wait;
that was not the sampled blocking operation.

## Minimal laboratory repair and controls

At 08:33:30, only
`/Library/Preferences/SystemConfiguration/com.apple.airport.preferences.plist`
changes from 0600 to 0644, matching the saved older file's read mode and the
reference's unprivileged reader. Contents retain SHA256
`9162e1f807e87b9c266768a28333ffb0b5ec0fdcfeb348981c595a92b9196632` before/after.
A root-only recoverable backup is
`/private/var/tmp/gui-unjoin-airport-preferences-20260912-before.plist`.
The origin of the earlier restrictive mode is not established; do not assign
it to the driver or a particular prior script without evidence.

WiFiAgent PID 5894 starts normally at 08:33:37.417–.421 on its next native
launch. ControlCenter PID 514 recovers its network rows without restarting.
Boot remains `7A5FAA36-5FAA-451F-A32B-70EB251B8660`, loaded kext UUID
`AF169180-05E8-33CF-960A-166E5DB901BB` (production 5e98d640).
Diagnostic USB management, host wired management and the neighboring QEMU are
untouched. Physical 10.90.10.22 is not accessed.

At 08:35:12.140 ControlCenter records the actual LabAP unjoin; airportd receives
it at .153 (13 ms), with link_off at .162. The guest becomes inactive without
an off/on. Actual menu reselection of the same saved LabAP at 08:36:26 obtains
WPA3/SAE and DHCP; the first completed service control passes 60/60 each way.
The routed peer is 10.7.6.112 with 1,200-byte ICMP payload and guest en1 binding.
This does not erase earlier packet losses or the original delayed disconnects.

## Mixed-security SSID must not qualify as a WPA2-only cell

At 08:38:24 the visible saved OpenWrt menu row is selected from LabAP. The
strict WPA2 checker stops without starting traffic because the actual result
is WPA3_SAE on BSSID `50:4f:3b:cd:dd:67`, channel 100, DHCP 172.16.66.212.
The native initial association targets channel 5 with PSK/SHA256-PSK/SAE
advertised; the later radio readback is channel 100. Host scanning independently
confirms OpenWrt BSSes `50:4f:3b:cd:dd:66`/channel 5 and `.67`/channel 100
advertise those three AKMs and MFPC, alongside other WPA2-only OpenWrt BSSes.
Do not relabel this as WPA2 success, a failed SAE association, or an IPv4
readiness failure. The earlier fixed-BSSID WPA2 results keep their original
scope; the SSID name alone does not guarantee the negotiated security.

A separately labeled actual same-OpenWrt reconnect follows: menu unjoin at
08:41:35.614 reaches airportd at .627 (13 ms), with link_off at .638.
Reselection at 08:42:16 obtains WPA3_SAE on the channel-100 transition BSS,
DHCP 172.16.66.212, and passes 60/60 packets each way. This qualifies a second
saved SAE profile's disconnect/reselect path, not the original WPA2-only cell.

Explicit menu off at 08:44:15 makes en1 inactive; menu on at 08:44:52
automatically returns to OpenWrt/SAE without a selection click. Native autojoin
reports 11.684 seconds from power-on trigger; IPv4 is configured at
08:45:04.825 and DHCP BOUND at 08:45:05.831. The nine-second screenshot is
still disconnected and is retained, not mistaken for a terminal failure.
The subsequent unchanged service window starts at 08:47:01 and passes 60/60
each way; it is delayed service coverage, not continuous first-packet coverage
from power-on. WiFiAgent remains PID 5894/runs 576, the preferences mode stays
0644 after the power-setting writes, and the 12-second readiness check passes
again. The guest stays on the same boot and exact AF16 image.

Script verification: Bash syntax, invalid interval rejection, non-Darwin
rejection, actual guest root-user rejection, and actual console-user passes
before and after off/on. The original unreadable-file and restart-loop
evidence predates the script; no deliberate reintroduction of that defect is
used as a test.

Remaining P0.1 work includes a separate unambiguous WPA2-only saved profile,
open same-profile disconnect/reselect, repeated direct pairs after the GUI
service repair, and the same cells after explicit off/on and real S3. The
previous post-S3 framebuffer/UI failure remains open. AP/mixed AP and ad hoc
remain below the user-prioritized ordinary STA repetitions.

Evidence archive:
`/home/dima/Projects/itlwm/aiam-gui-unjoin-runtime-20260912.zvNP4h`.
All 132 files verify against `EVIDENCE.sha256`, whose SHA256 is
`f4325686edd11bbeb1eb67a0c9ce37d185b0c025ee5b8512ef76ec146eb184c6`.
The archive and original RAM evidence are now terminal and immutable.
The release artifact stays at production 5e98d640; this cycle changes no
production driver source and must not publish identical bytes under a new
driver identity.
