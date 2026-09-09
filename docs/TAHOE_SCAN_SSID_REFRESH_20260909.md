# Refresh a reconfigured BSS's scan identity — 2026-09-09

## Reproduction on `9040aa2b`

The disposable IWN guest moved from saved WPA3 to a controlled open AP through
ordinary `networksetup`, obtained DHCP and passed 20/20 packets in each
direction. Returning to WPA3 completed in nine seconds with 5/5 packets.
The first open selection had reported network-not-found; a fresh directed
scan then found it. That first omission is not assigned the cause below.

The controlled AP was then changed to a different WPA2 SSID while retaining
its BSSID. Two normal public selections reported network-not-found. The first
was observed entering WCL scan with real 13/24-channel firmware commands.
A subsequent directed CoreWLAN request returned zero matching results.

At 14:47:33 UTC, a bounded read-only probe on the matching `9040aa2b` build
captured fresh beacon/probe-response SSID IEs containing the new WPA2 name.
`ieee80211_find_node` returned the same BSSID's cached node with the previous
open name; the name was still unchanged on return from beacon processing.
Thus the new AP was received but its updated identity was not published.
The old `ni_essid[0] == '\0'` assignment condition admits only the first name,
while adjacent parsing refreshes other properties such as RSN and rates.

Offsets came from the matching `ieee80211_input.o` DWARF: MAC `0x35`, BSSID
`0x3b`, SSID length `0x4e`, SSID bytes `0x4f`. An initial observer used
unaligned multi-byte MAC comparisons and faulted; it is not evidence of an
absent frame. Only the corrected byte-comparison observation supports this
finding. No driver mutation was used to produce that result.

## Reference boundary and change

The complete 25C56 DriverKit `AppleBCMWLANScanAdapter::processScanResults`
at `0x10018a9a4` was decompiled from the saved project in read-only mode,
with `-max-cpu 40`. It validates each fresh firmware BSS record and its IEs,
assembles a beacon message through `getBeaconMsgFromWLBSSInfo`, and publishes
that message to the infrastructure consumer. It does not justify combining
new security IEs with a first-seen-only cached SSID in this port. This is
evidence for the fresh-result producer, not a claim to recover every
downstream Apple cache or hidden-SSID rule. The older full BootKC export's
broken no-return allocator/split body was not used as a complete function.

The net80211 helper now refreshes a cached node's SSID from a fresh non-hidden
advertisement. It does not rewrite `ic_bss`, BSS/authenticated/associated or
retiring peer owners. Empty and all-zero hidden advertisements preserve a
learned name. The SSID remains counted bytes, including embedded or leading
zeroes in a non-hidden value; replacing a longer name clears its old tail.

The ASan/UBSan regression compiles the production helper and checks initial
learning, repeated/shorter replacement, hidden advertisements, binary names,
maximum and malformed lengths, null inputs and association-owner isolation.
Existing hidden-AP, scan-result, WCL roaming and BTM contract tests pass.

## Runtime qualification of `96eaf2e9`

The full Tahoe build resolved all 1083 BootKC symbols. Private AuxKC admission
passed, then transactional activation retained the four companion members.
The disposable IWN/6235 guest loaded UUID
`0FE73C63-480B-39C3-A43E-53DB3D225366`, Mach-O SHA-256
`ee4da909d6d57b75b6e56f5801424ea106a5021f9a7392885b1f303120fba0c2`.

A reboot clears the cache, so discovery after activation alone was not the
qualification. At 15:02:36 UTC a directed scan first cached the controlled
open identity. The observer was armed before that AP was renamed and changed
to WPA2 while retaining its BSSID. At 15:03:13 the matching-build FBT trace
captured the cached old name, the incoming new SSID IE and the updated cache
name on return from beacon processing. The node remained a cache owner.

The ordinary first public WPA2 selection completed in 16 seconds, with no
network-not-found error, radio toggle or further reboot. The external AP
confirmed authentication, association and the RSN four-way handshake. DHCP
completed and traffic passed 20/20 packets in each direction. The production
helper ASan/UBSan regression and the real WCL/AP-intent regression passed again.

Two actual S3 cycles then distinguished profile choice from protocol recovery:

- In the first, the system returned to a different saved WPA3 profile. The
  airportd log shows both networks were discovered and matched, followed by
  a userspace request to join WPA3. The saved list had WPA3 ahead of WPA2.
  This is not counted as restoration of the selected WPA2 network.
- After the controlled WPA2 profile was first in the preferred list, another
  sleep reached QEMU `paused (suspended)` and serial `ACPI SLEEP`. Only the
  owned monitor received wake at 15:16:41 UTC. Airportd requested WPA2 at
  15:16:42; the external AP completed a fresh four-way handshake, and traffic
  returned on the fourth one-second probe (22/25 over the wake interval).
  Direct Wi-Fi SSH confirmed the same boot epoch and loaded UUID, the WPA2
  DHCP address and a subsequent 10/10 source-bound transfer. No radio toggle
  or explicit post-wake join was used.

The latter cycle lost the separate emulated USB Ethernet management interface
while physical Wi-Fi recovered. Replugging its exact QEMU USB device did not
restore that interface; subsequent checks used working physical Wi-Fi SSH.
Neither this profile-order comparison nor command-line selection proves the
full GUI last-selected-network policy. No physical user machine was changed.

### WPA3/APSTA regression on the same loaded candidate

A normal public WPA2-to-WPA3 selection after sleep succeeded and passed 5/5
source-bound packets. A role-7 pure-SAE/required-PMF AP then started alongside
the primary WPA3 STA. The external AX211 completed SAE group 19 with BIP.
Static-address traffic passed 20/20 client-to-AP, 5/5 AP-to-client and 5/5
primary-to-gateway packets.

Another `pmset sleepnow` at 15:22:21 UTC reached actual S3, with the QEMU
suspended and serial `ACPI SLEEP`. Owned-monitor wake at 15:24:23 produced
`ACPI S3 WAKE`. Explicit external-client reselection completed SAE/PMF again;
all three paths passed the same 20/20, 5/5 and 5/5 checks. Wi-Fi SSH confirmed
unchanged boot epoch and kext UUID with both roles active. This is service
recovery, not automatic client continuity or a fresh DHCP AP matrix.

Normal AP stop retained primary traffic at 10/10. The AP's temporary address,
the two test-only saved STA profiles, the host AP interface/services and the
two temporary host NetworkManager profiles were removed. The host's original
managed Wi-Fi profile was restored; its Ethernet default route was unchanged.

Frozen candidate ZIP SHA-256:
`c76806872f0ec00dd4f6c335cd381e27c19d956b08aeb3da172ccee39bd9ed1e`.

## GUI boundary

After the earlier APSTA S3 run, the laboratory VGA framebuffer remained asleep
despite user-activity assertions. A guest-only reboot restored the login screen.
There is no authenticated desktop session, and the login screen's Wi-Fi status
icon did not yield a usable selection menu. These command-path checks are not
labelled GUI coverage. A temporary USB tablet was added only to the owned QEMU
to make pointer input work; desktop-login credentials were requested separately
without blocking further driver testing.
