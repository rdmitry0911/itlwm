# Missing scan candidate: same-card Linux control — 2026-09-10

## Scope and loaded image

The disposable Tahoe guest used source `893a3114`, loaded UUID
`5455B34A-AA04-34EE-8CC5-82B373798841`. This is the currently published,
runtime-qualified AP discovery/DTIM/BSS-teardown image. No functional source
change or replacement release is claimed in this investigation.

The user-visible remaining case is intermittent discovery of one 5-GHz BSS.
Two 2.4-GHz BSSs of the same saved network are discoverable. The external
AX211 receives the missing BSS at approximately -59 to -60 dBm. Network
identities, addresses and credentials are retained only in private evidence.

## Actual guest scan versus received radio frames

At 22:32 UTC, five single-channel CoreWLAN requests while the STA was associated
each reached a real channel-153 firmware command. Each completed successfully
with passive dwell 85 TU, approximately 87.1 ms elapsed and zero good-CRC
frames. The PHY observer recorded no channel-153 receipt in that interval.
A channel-161 control used the same command shape, received nine good-CRC
frames and returned a BSS. Thus this is not a failure to submit the requested
5-GHz channel, nor a total loss of the 5-GHz receive path.

After ordinary CoreWLAN disassociation at 22:44:25, the firmware's RXON had
association ID zero and the net80211 state was SCAN. Longer 110-TU scans ran
with no associated home-time budget. A target beacon reached net80211 at
22:44:34 and a CoreWLAN result contained measured RSSI -78 dBm. Subsequent
unassociated scans still missed the same target. Association state alone is
therefore not a sufficient explanation.

A separate standalone AX211 monitor then observed regular target beacons at
24 Mbit/s and a 100-TU interval. A persistent SSH clock exchange bounded the
guest/host realtime offset before and after the capture; the conservative
envelope was approximately -27.9 to -24.8 ms. Several completed guest scan
notification intervals longer than a beacon interval contained an externally
captured beacon well inside that envelope, but reported zero good-CRC frames.
These are notification-bracket observations, not a direct RF switch probe or
proof of the precise firmware listen start. They do not support attributing
all omissions solely to the earlier sub-beacon dwell length.

Diagnostic limits are explicit. An early multi-channel command observer
attempted an unaligned read of a packed channel record; DTrace rejected the
read without writing kernel memory. Those command actions are excluded.
The firmware channel notifications and positive net80211 target receipt are
independent observations. An early cross-clause target-MPDU diagnostic is not
used as negative evidence; its replacement performs the checks and reporting
in one clause. Current Intel field offsets were read from this loaded build's
DWARF, not reused from an older image.

## Same physical 6235 under Linux DVM

The guest shut down normally before the exact passed-through 6235 PCI function
was temporarily bound to the host's native `iwlwifi`/`iwldvm` driver. The same
card, antennas and physical location were retained. The PCI bridge and the
AX211's driver were not unbound; the host's wired management route was unchanged.

The native driver identified a Centrino Advanced-N 6235 AGN and loaded
firmware `18.168.6.1 6000g2b-6.ucode` on kernel `6.8.0-137-generic`.
An initial control was inadmissible: the pre-existing global regulatory domain
disabled the target and control upper-5-GHz channels, so Linux returned EINVAL
before scanning. That is not a failed RF-reception test. A bounded repeat used
the conservative world domain and passive-only targeted scans, with no
forced-active or channel-table override. The previous regulatory domain was
restored after each such run. The self-managed AX211 retained its ordinary link.

Two completed repetitions each performed five passive scans of channel 153.
Neither repetition discovered the target BSS. Other channel-153 records in
the first repetition were initially fresh and then aged rather than being
counted as new receptions on every request. The channel-161 control received
fresh records, approximately -26 to -27 dBm, and a channel-13 control received
fresh neighboring BSSs. Historical cache entries are not counted as fresh
control results.

The second repetition then placed the same 6235 in standalone monitor mode
on channel 153. A 15-second capture received only three target beacons, all
at 24 Mbit/s and approximately -77 dBm, with long gaps between them. The
external AX211 remained associated with that target at approximately -59 dBm.
This continuous-monitor result also avoids relying exclusively on a short
firmware scan window to establish intermittent reception on the 6235.

The low-level scan and receive contracts were compared with Intel's primary
[DVM scan implementation](https://raw.githubusercontent.com/torvalds/linux/master/drivers/net/wireless/intel/iwlwifi/dvm/scan.c)
and [DVM receive implementation](https://raw.githubusercontent.com/torvalds/linux/master/drivers/net/wireless/intel/iwlwifi/dvm/rx.c).
This is an Intel same-hardware control, not a claim of equal Broadcom hardware
behavior or a proof that a specific antenna, RF component or firmware defect
is responsible. It establishes that the omission is not unique to this port
or to macOS metadata publication. Removing PMF, inventing a fresh scan result,
or bypassing timing/regulatory admission is not justified by this evidence.

## Reconnect observation and next work

The explicit CoreWLAN disassociate also caused macOS to mark the saved network
temporarily disabled. Airportd continued receiving fresh matching 2.4-GHz
results but excluded that known profile from automatic selection. That is not
a demonstrated driver candidate-rejection defect and is not equivalent to
the controlled involuntary BSS-loss test.

Explicit selections nevertheless reproduced a separate empty-scan/no-network
result before a later credentialed selection recovered without radio off/on.
The next instrumented ordinary selection at 22:54:39 completed in ten seconds,
obtained its normal address and passed 5/5 packets. A matching-build observer
followed current observation stamps through the physical collector, successful
metadata building and the actual Apple consumer; its terminal reported zero
diagnostic errors. A final independent primary check passed 5/5 before shutdown.
This does not close the full repeated selection or GUI/profile matrix.

The next priority remains repeatable user-facing reconnect behavior with a
controlled, reliably received AP, including open/WPA2/WPA3 and post-sleep
sequences. The intermittent weak-BSS case remains open, but its present evidence
must not be mislabeled as a macOS-only functional regression.

## Restoration after the control

Both completed native-Linux runs reached their terminal and restored the exact
PCI function to `vfio-pci`, including its driver override. The original global
regulatory domain and the AX211's ordinary managed connection were restored;
the wired management route was unchanged. No monitor interface was retained.

Only the owned disposable guest was started again, using the same overlay and
unchanged kext. Its 23:03:30 UTC boot loaded the matching UUID above, obtained
its ordinary STA DHCP address and passed 5/5 source-bound packets. The previous
guest had terminated before the new process opened that disk. Neither the
physical user machine, another VM nor a base disk was modified. This control
does not add a functional driver change or require a replacement release.
