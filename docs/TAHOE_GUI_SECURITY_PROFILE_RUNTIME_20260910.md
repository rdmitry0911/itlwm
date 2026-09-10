# System Settings security/profile runtime — 2026-09-10

## Scope and exact image

These are real selections in the guest's System Settings Wi-Fi pane, driven
through its VNC console. Association is not substituted with networksetup,
CoreWLAN command-line selection, a private driver call or manual addressing.
The controlled external AP uses the host AX211 and a separate DHCP subnet;
the guest STA is the physical passed-through IWN/6235. Independent temporary
USB management does not carry the measured Wi-Fi packets.

The clean 06:20:02 UTC boot has session
`651027BF-A79C-488D-90E1-610BD01A5EFB` and loaded kext UUID
`946F461F-3B19-38AA-8923-7AEF04263ED8`, source `3e73f218`.
The published binary is unchanged. This note adds qualification evidence,
not another functional source correction or equivalent IWM/IWX coverage.

## Excluded fixture failures

The first open GUI attempt obtained DHCP and began passing traffic, but the
host ZFS pool filled and QEMU paused with `io-error` / `nospace`. Its remaining
traffic checks did not complete. That is not a clean driver qualification.
Space was recovered from independently checked, inactive failed disposable
overlays. The current overlay and base were preserved. A firmware fatal in
the I/O-stalled boot is retained as an observation; its causal chain has not
been established. The subsequent clean boot above is the qualification epoch.

The first WPA2 GUI attempt also failed and returned to the saved network.
The external AP reported possible PSK mismatch. A visible-input control
showed that the default VNC typing path changed capital letters to lowercase.
A first attempted input correction was not independently verified and also
failed. Neither is counted as successful WPA2 or as proof of a driver defect.
The subsequent control uses explicit shift handling, 100-ms key spacing and
inspection of the complete entered test credential before submission. The
credential, clipboard contents, network identities and screenshots remain
private; clipboard contents were cleared after the control.

## Open selection and return to saved WPA3

The controlled open AP became ready at 06:22:59 UTC. Its network appeared
in System Settings without toggling Wi-Fi. The 06:25:33 Connect click was
followed by external association at 06:25:38.782 and DHCPACK at 06:25:42.
Guest readback identified that real lease. Separately awaited 1400-byte
checks passed 20/20 guest-to-AP and 20/20 AP-to-guest. The GUI reported a
connected unsecured network. This is not a throughput or low-latency gate.

At 06:28:14 the saved WPA3 network was selected through the same GUI. The
real airportd request at 06:28:16.239 selected SAE with required PMF on the
strong 2.4-GHz candidate. IPConfiguration verified the prior DHCP lease's
router with ARP and published IPv4 success at 06:28:20.356; BOUND followed at
06:28:21.380 and another publication at 06:28:22.083. This is valid cached
DHCP-lease reuse, not a claim of a new over-the-air DHCP exchange on return.
A later independent source-bound check passed 10/10; the same boot and kext
UUID were rechecked. No off/on, guest reboot or command-line join intervened.
The external fixture terminated normally at 06:29:02 and restored the host's
ordinary managed connection without changing its wired management route.

## New WPA2 profile through GUI

The repeated controlled WPA2 fixture started at 06:41:48 UTC. The complete
test password was visibly verified before the 06:43:53 GUI submission. The
external AP recorded association at 06:43:58.145, successful RSN four-way
handshake, WPA2-PSK AKM and CCMP, with PMF disabled as configured. DHCPACK
followed at 06:44:02 and guest readback identified the real lease.

The separately awaited 1400-byte checks passed 20/20 guest-to-AP and 20/20
AP-to-guest, ending at 06:44:43. The first forward packet took approximately
310 ms; the result is service qualification, not a latency or throughput
claim. No radio toggle or guest reboot was required to recover from the
incorrect-input attempts and complete this new-profile join.

## GUI off/on and saved WPA2 reselection

The 06:45:24 GUI radio-off action was independently confirmed as power Off,
inactive carrier and absent STA IPv4. The 06:45:44 GUI radio-on action
automatically selected the other saved WPA3 network: airportd began SAE
association at 06:45:49.759 and IPConfiguration published the restored lease
at 06:45:55.293. The check waiting for the WPA2 subnet consequently failed;
this is not an automatic-return-to-WPA2 pass.

The Wi-Fi pane temporarily displayed no other networks while its real scan
requests continued completing. By 06:47:01 the controlled WPA2 AP was again
listed as a known network. This observation does not establish a permanent
scan failure or the cause of the temporary omission.

At 06:47:30 the saved WPA2 entry was selected through GUI, without entering
a password. The external AP associated the STA at 06:47:35.849, completed
the WPA2 handshake and acknowledged DHCP at 06:47:36. The separate forward
and reverse checks passed 20/20 each, ending at 06:48:14. This qualifies
explicit saved-profile reuse after off/on, not the system's preference for
the last user-selected network.

## Controlled AP shutdown

After the saved-profile traffic checks completed, the exact owned WPA2
fixture controller was stopped at 06:49:09 UTC. Its normal hostapd shutdown
and interface removal completed with restoration of the host's ordinary
managed connection at 06:49:12. This is a signaled AP shutdown, not a silent
RF blackhole or lossless roaming qualification.

Without any guest GUI selection, command-line join or radio toggle, the
guest regained the other saved network's address by 06:49:20 and passed a
separate source-bound 10/10 check. The same boot epoch was required by the
observer. The complete clean-boot serial interval through these GUI checks
contains no matched Intel firmware fatal, device-timeout watchdog or panic.

## New pure-WPA3 profile through GUI

The controlled SAE-only AP, with PMF required and group 19, started at
06:50:05 UTC. System Settings identified it as WPA3 Personal. The complete
test password was verified before the 06:52:21 GUI submission. The external
AP associated the STA at 06:52:27.127, reported SAE AKM, group 19 and MFP,
and completed the four-way key handshake. DHCPACK followed at 06:52:31;
the guest read back that lease.

The separately awaited 1400-byte forward and reverse checks passed 20/20
each, ending at 06:53:11. This was a new GUI profile, not a command-line
join or reuse of the previously tested ordinary WPA3 profile. The same
boot and released kext remained loaded.

## Actual S3 and the post-wake GUI boundary

The sleep guard verified the current boot/image and working controlled
WPA3 link. Temporary USB management and the VNC tablet were removed while
the VM was awake; both the monitor and guest USB inventory confirmed their
absence before the 06:54:22 UTC sleep request. Serial `ACPI SLEEP` and the
owned VM's `paused (suspended)` state independently confirmed actual S3.
Wake at 06:55:44 produced `ACPI S3 WAKE`. Fresh USB management and the tablet
were added only after wake. The boot session and kext UUID remained unchanged.

Without manual selection or off/on, airportd chose the other saved WPA3
network at 06:55:49.087, not the controlled test network. IPConfiguration
verified its cached lease's router and published IPv4 at 06:55:51.328;
BOUND followed at 06:55:54.621. A separate source-bound check passed 10/10.
This proves automatic network-service recovery, not restoration of the last
selected profile. No firmware-fatal, device-timeout or panic matched the
post-wake serial interval.

The display did not provide a usable post-wake GUI: VNC retained the 06:54
pre-sleep frame despite the guest clock and network processes advancing.
A one-second WindowServer sample at 06:58:56 found its main thread waiting
in `displayDidWake -> IOFBAcknowledgeNotification -> IOConnectCallMethod`.
The independent native screenshot process waited for the SkyLight session
port and produced no image. Its exact diagnostic PID was then terminated;
WindowServer was not restarted and no privacy permissions were changed.
These userspace stacks identify the blocked framebuffer-wake path, not the
kernel lock owner or a proven complete cause. No blind clicks or rebooted
GUI session are counted as a successful post-S3 profile-selection test.

The sleep-transition serial log also contains software-encryption attempts
with a cleared key descriptor. The production rejection branch frees the
packet and returns NULL. Their originating call path remains to be traced;
successful recovery does not erase this observation.

Thus post-sleep GUI qualification remains open. The passing selections do
not close the complete matrix, last-selected-profile policy, or the separate
weak-BSS reconnect delay.
