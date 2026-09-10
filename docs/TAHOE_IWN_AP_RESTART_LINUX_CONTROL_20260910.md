# Same-card native Linux APSTA restart control

## Question and scope

The loaded IWN candidate `cb6a7bc0` reproduces firmware assert `0x22CE`,
PC `0x26294`, after nonempty AP stop followed by client association on the
next AP start. Both SAE/required-PMF and WPA2/PMF-disabled APs reproduce it.
The latter still has a WPA3 primary STA: it excludes AP SAE/PMF as a
necessary condition, not every influence of the primary security context.
See `TAHOE_IWN_SCD_QUEUE_RETIREMENT_20260910.md` for those exact-image traces.

The next control runs the same physical 6235 under native Linux DVM instead
of changing another isolated firmware field in the guest driver. The guest
shut down normally at 03:42:38 UTC; its exact QEMU process terminated before
that one PCI function moved from vfio-pci to iwlwifi. No other VM or physical
user machine was changed.

Linux identifies kernel `6.8.0-137-generic`, Intel 6235 and firmware
`18.168.6.1 6000g2b-6.ucode`. Its actual interface-combination admission allows
one managed interface plus one AP on a common channel with matching beacon
intervals. This firmware/driver does not advertise MFP; the native control
therefore uses WPA2 in both roles. It is not a native Linux WPA3 qualification
or a security-identical comparison with the guest's WPA3 primary.

## Air-path isolation and measurement

The 6235 PHY and both of its interfaces run in a temporary network namespace.
The external AX211 stays in the host namespace: one temporary AP supplies
the 6235 primary, and its ordinary managed interface joins the 6235's AP.
Both radio contexts use channel 9 and a 100-TU beacon interval. Separating
network namespaces prevents host-local routing from delivering the supposed
AP/client packets without crossing the radio. Addresses are isolated static
test addresses; none of these native-control results proves DHCP.

The exact PCI-device-filtered `iwlwifi_dev_hcmd` tracepoint records command
bodies in private perf files. The installed perf Python scripting extension
segfaulted while reading the already completed first recording. A separate
offline reader instead validates the perf file/attribute/sample layout,
event IDs, dynamic-array boundaries and device filter before exporting each
complete command. Its counts match perf's independent counts: 294 samples
in the first control and 480 in the repeated control. No lost-record event
is present. No live kernel mutation or speculative private object offset is
used to decode these records.

## Results, including the initial failed gate

The first native run passed initial 20/20 client-to-AP, isolated cold-neighbor
10/10 reverse and primary 5/5 traffic. It disabled AP during AP-originated
UDP pressure, then received 9/10 primary packets after the dwell. The strict
zero-loss check terminated the scenario before restart; that run is not a
successful AP restart test. Its cleanup restored the device to vfio-pci and
the ordinary AX211 profile, leaving the host wired default route unchanged.

The repeated run preserves packet-loss results separately from the firmware
restart question. It also explicitly disconnects the AX211 managed client
after AP disable, preventing its automatic reconnect scans from competing
with the same AX211's temporary upstream AP during the dwell. That fixture
change is not a repair to either Intel driver and does not establish the
cause of the first run's one lost packet.

From 03:46:38 through 03:50:00 UTC the repeated native run completed:

- Initial AP and each of two subsequent starts: 20/20 client-to-AP, isolated
  cold-neighbor 10/10 reverse and primary 5/5 packets.
- Two AP-originated UDP-pressure stops, each followed by a 15-second dwell
  and primary 10/10 before the next ordinary hostapd enable.
- Separate primary UDP pressure: 35.1 MiB received without reported datagram
  loss. This establishes active primary transport, not a throughput benchmark.
- A bounded read-only queue/station observer records active AP aggregation,
  then primary station 0 owning aggregate queue 11 before the second AP
  restart. Queue 10 remains a fixed, nonaggregate transport queue.

There is no matched device firmware assert, firmware restart, queue timeout
or already-used-queue warning in the complete native kernel interval. The
unrelated perf userspace segfault remains in the retained log and is not
silently removed from the evidence.

All control/traffic/observer processes terminated. The temporary namespace
and source AP interface were removed, the exact card returned to vfio-pci,
the ordinary host Wi-Fi profile was restored and the wired default route
matched its pre-test value. The guest was restarted with the same overlay,
build disk and unchanged Intel kext; only its owned PID/monitor/serial file
names changed so that prior logs remain intact.

## What this changes next

The hardware/firmware can complete the tested pressure stop/restart sequence
under native DVM. This is evidence to compare complete transport lifetimes,
not proof that every relevant firmware field or radio/security condition is
identical between operating systems.

The recorded native sequence includes per-aggregate flushes, station update
and removal, unassociated PAN context transitions and final retirement of
the unused context. An absent standalone REMOVE_STA in the guest is not
automatically the cause: unassociated RXON also clears firmware stations.
Compare these transitions with actual guest submissions before selecting
the next correction.

One independently visible topology divergence deserves a complete producer/
consumer audit: native PAN reserves queue 10 for AUX and begins dynamic
aggregation at 11. The guest's PAN post-alive code likewise installs queue
10 as fixed AUX, but its STA start/stop methods still compute aggregate
queue `10 + tid`. Previous guest fatal dumps show primary traffic on queue
10, while this native observer shows primary aggregation on queue 11.
Changing a start constant alone would not be sufficient: TX submission,
completion, compressed BA, retirement, allocation and concurrent AP ownership
must all agree. This is a correction candidate, not a decoded explanation
of assert `0x22CE` or a qualified driver fix.

Public release remains the previously qualified `893a3114` image. No source
fix or replacement kext is claimed from this comparison alone.
