# IWN scan dwell budget and missing 5-GHz candidates — 2026-09-09

## Reproduction on the released APSTA epoch candidate

The disposable IWN/6235 guest loaded source `98dc62ee`, UUID
`6E20810C-101D-313E-AD15-56ED0BD8E709`. Neither AP nor Internet Sharing was
active. A host AX211 monitor fixed on the target's 5-GHz channel received its
beacons continuously at about -60 dBm. Ordinary guest CoreWLAN directed scans
repeatedly returned only one or two 2.4-GHz members of the same ESS.

Credentialed public reconnects in this interval selected 2.4-GHz candidates
and succeeded in 10, 10 and 11 seconds. Two bounded FBT observations showed
both SAE transmissions completed successfully and both peer responses reached
the engine. These successes do not close the previously reproduced 5-GHz
authentication timeout. An explicitly channel-selected CoreWLAN association
probe could not try that target because its fresh scan did not contain it.

FBT then followed the scan request down to the actual firmware command:

- CoreWLAN submitted an active directed 2.4-GHz plan with 13 channels,
  followed by a 5-GHz plan with 24 channels, including the target channel.
- Both plans requested active 20, passive 110 and home 45.
- IWN submitted the two corresponding physical bands, with 13 and 24 channel
  entries. The 5-GHz entries retained their NVM-passive flags and used
  active=20 and passive=110; `max_out` was 112640 microseconds (110 TU).
- The firmware command replies had status 1. There was no observed
  target-BSS frame at `ieee80211_find_rxnode` during these scans. The matching
  external capture saw the target beacons but no guest probe on that channel.

This rules out a missing channel in the upper plan. It does not by itself
prove how firmware handled every channel or that dwell is the sole cause.

## Firmware contract and implementation

Intel's [DVM command ABI](https://github.com/torvalds/linux/blob/master/drivers/net/wireless/intel/iwlwifi/dvm/commands.h)
defines per-channel dwell in TU and `max_out_time` in microseconds, and
requires passive and active dwell to be strictly below nonzero max-out time.
The observed passive=110 TU equals, rather than precedes, the 112640-us
deadline. Intel's [DVM scan implementation](https://github.com/torvalds/linux/blob/master/drivers/net/wireless/intel/iwlwifi/dvm/scan.c)
also bounds channel dwell by the timing of the live firmware contexts.

The port already had STA/PAN timing limits, but exact WCL dwell overrides
bypassed the ordinary STA limit; the final reapplication covered PAN only.
Its subsequent active/passive repair could raise passive past that limit.

The final command builder now applies one production helper after every
policy/extension override. It bounds both dwell values by the existing
STA/PAN beacon ceiling and the strict max-out deadline, then bounds quiet
time by active dwell. An impossible budget fails before firmware submission.
The immutable WCL plan, channel list, passive/DFS restrictions, and upper
scan completion ownership are unchanged. This does not normalize all
historic millisecond/TU conversions or change the upper home/away policy.

## Verification boundary

The C++ regression calls the same helper used by the firmware command builder
under UBSan. It checks the observed equality case, retained STA and PAN
limits, an unrestricted foreground scan, zero/impossible budgets, integer
boundaries and active/passive ordering across a parameter matrix. The exact
WCL-plan and APSTA home/away integration contracts pass.

Source `35589ce0` built with all 1083 BootKC symbols resolved. Private AuxKC
preflight and transactional activation passed. The guest reboot loaded UUID
`990CB31C-373C-3CAB-B86F-F8C852342D58`, matching candidate Mach-O SHA-256
`66cbd52411a11ecd9c0284a64892c2674e3268eb20704ce435850890138d6024`.
Saved-profile WPA3/DHCP returned automatically and source-bound traffic
passed 5/5.

FBT confirmed the actual 24-channel 5-GHz command now uses passive=85 rather
than 110, with active=20 and max_out=112640 unchanged. A separate observer
captured the real `STOP_SCAN` descriptor at terminal claim: type 132,
scanned channels 24, status 1, last channel 165. The 2.4-GHz terminal likewise
reported all 13 requested channels with status 1.

Nevertheless, repeated CoreWLAN directed results still omitted the 5-GHz
target. The invalid firmware timing combination is corrected, but the
missing-BSS surface is not closed and dwell was not its sole cause. The
first terminal observer had a DTrace-inferred 32-bit pointer and generated
read faults; only its corrected pointer-width run is used for this claim.
Likewise a `tick-Nsec` probe is not a relative observation timeout: the
bounded observers now compare monotonic time against their BEGIN timestamp.

A time-aligned CoreWLAN/firmware observation also rules out the initially
suspected early public completion in that run: the directed call returned
at 13:24:45 UTC, the same second as the 24-channel firmware terminal. Its
results contained one 2.4-GHz ESS member. The broader CoreWLAN cache contained
other 5-GHz BSS on channels 36, 56, 100 and 161 before and for eight seconds
after the call, but not the target on 153. Thus this is not a blanket 5-GHz
receive failure. Reading an unfinished, buffered DTrace output had not been
sufficient evidence of early completion.

Further bounded RX observations at 13:38 UTC captured two real 13+24-channel
scans. Neither the target's MPDU at entry to `iwn_rx_done` (before RX_PHY/FCS
admission) nor its frame at net80211 appeared. Other 5-GHz RX_PHY/MPDU traffic
did appear. The simultaneous external channel-153 monitor captured 534
target/guest-filtered packets, with target beacons around -60 dBm, but no
guest probe. This localizes that reproduction below net80211 rather than to
the result serializer or SAE handling.

The actual per-channel firmware reports were subsequently decoded using
Intel's DVM `SCAN_START/RESULTS/COMPLETE_NOTIFICATION` layouts. Channel 153
reported a second START after about 72.8 ms, then a result about 10.4 ms later
with `probe_status=0x81`, `num_probe_not_sent=0` and `good_crc=0`. Bit 0 is
Intel's documented probe-TX-failed flag; bit 7 is not assigned a meaning by
that header and is not interpreted here. The complete terminal still reported
all 24 channels and status 1. This does not prove why TX failed.

A nearby AX211 control AP on the same channel was accepted only as a virtual
AP concurrent with its ordinary managed connection; standalone hostapd was
rejected by NO-IR and no regulatory override was attempted. Guest scans also
omitted that control SSID. Its hostapd ENABLED status alone does not prove
on-air beacon delivery, so this is not conclusive independent RF validation.
The temporary AP was stopped by its verified PID and its interface removed.
A guest radio off/on restored saved-profile WPA3/DHCP but did not eliminate
the subsequent associated-scan omission.

## APSTA/S3 regression and release

On the same loaded `35589ce0` image, a role-7 pure-SAE/required-PMF AP started
alongside the live primary WPA3 STA. An external AX211 completed SAE group 19
with BIP. Static-address traffic passed 20/20 client-to-AP, 5/5 AP-to-client
and 5/5 primary-to-gateway. During directed CoreWLAN scan requests, AP traffic
passed 100/100; those requests returned an empty set and the bounded observer
saw no physical scan command. That interval does not qualify actual PAN
off-channel scanning or its firmware dwell; the production-helper tests cover
the PAN numerical limits, separately from this traffic regression.

The guest entered actual S3 after `pmset sleepnow` at 13:31:55 UTC. The serial
console recorded `ACPI SLEEP` and the owned QEMU monitor reported
`paused (suspended)`. Only that monitor received `system_wakeup` at 13:32:46;
`ACPI S3 WAKE` followed. At the next management check, the original boot epoch
and kext UUID were unchanged and both roles were active. The client's
non-autoconnect test profile was explicitly reselected, completed a fresh
SAE/PMF handshake and again passed 20/20, 5/5 and 5/5 packets on the three
paths. A normal AP stop preserved primary traffic at 10/10. This proves
service recovery, not seamless client continuity or a new DHCP qualification.

The candidate archive is made from the same frozen kext verified above.
Release ZIP SHA-256:
`42d65d772c248d82c1e2154aecedbb1857afb2f66446ea0382b4e8aa7e1c1365`.
The release update retains the missing-BSS and repeated-public-join defects
as known limitations; this timing fix does not close them.

The temporary host monitor was removed and its original managed connection
restored. All experiments used the disposable guest and wired host management;
the physical user machine was not changed. The temporary AP address and
client profile were removed after the S3 regression; normal host management
and its ordinary Wi-Fi profile were retained/restored.

## Revalidation after SSID-cache correction

Loaded `96eaf2e9` (UUID `0FE73C63-480B-39C3-A43E-53DB3D225366`) still
reproduces the missing target. On 2026-09-09 at 15:31 UTC the physical command
again contained 13 and 24 channels, active/passive dwell 20/85, max-out
112640, and successful scan terminals. The target channel reported zero good
CRC and probe status `0x81`; no target frame reached net80211. A directed
CoreWLAN call returned only a 2.4-GHz ESS member.

The first concurrent host STA+monitor capture saw only one probe response,
not a continuous beacon stream. It is not used as proof of beacon delivery
during that scan. The host was then put into standalone passive monitor mode
on the target channel; wired host management and routed Wi-Fi SSH to the guest
remained available. No regulatory/channel restriction was changed.

A fully overlapping run at 15:35:17--21 UTC captured a real 13+24-channel
scan and successful terminals. The target channel's dwell lasted 87071 us,
with `good_crc=0`, no target at net80211, and this time probe status **zero**.
The simultaneous host capture received target beacons around -60 dBm at
24 Mbps, including a sequence across the target scan's wall-clock second.
No probe from the guest appeared in that capture. This reproduces omission
without the earlier probe-TX-failed bit, so that bit is not a sufficient
explanation of every occurrence.

A second immediate CoreWLAN call still returned the 2.4-GHz member but its
observed physical command covered only 13 channels; it is not counted as a
second full-band scan. Nor does an unidentified channel-153 entry in the
broader, redacted CoreWLAN cache prove that this target was received.

These results keep the defect below the result serializer and separate from
the corrected first-seen-only SSID cache policy. They do not establish whether
the remaining cause is scan scheduling, RF/rate/antenna behavior or another
firmware constraint. Host/guest wall clocks were not calibrated at sub-beacon
precision, so the capture does not prove that a particular beacon overlapped
the entire firmware listening interval. The temporary monitor was removed
and the original host managed profile restored after the run.
