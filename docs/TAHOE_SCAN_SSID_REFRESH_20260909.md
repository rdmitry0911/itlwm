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

Runtime qualification is pending. A guest reboot clears the old cache, so
merely discovering WPA2 after activation is insufficient: the test must again
advertise open, cache it, change the same BSSID to the WPA2 identity and then
exercise fresh discovery, association, DHCP and traffic without a radio toggle
or another reboot. The published artifact remains `9040aa2b` until that and
the relevant regressions pass. No physical user machine was changed.

## GUI boundary

After the earlier APSTA S3 run, the laboratory VGA framebuffer remained asleep
despite user-activity assertions. A guest-only reboot restored the login screen.
There is no authenticated desktop session, and the login screen's Wi-Fi status
icon did not yield a usable selection menu. These command-path checks are not
labelled GUI coverage. A temporary USB tablet was added only to the owned QEMU
to make pointer input work; desktop-login credentials were requested separately
without blocking further driver testing.
