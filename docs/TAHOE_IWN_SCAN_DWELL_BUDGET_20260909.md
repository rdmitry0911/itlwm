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

AP/S3 regression and publication of this newer candidate are pending. The
published release remains the runtime-qualified APSTA epoch fix `98dc62ee`.

The temporary host monitor was removed and its original managed connection
restored. All experiments used the disposable guest and wired host management;
the physical user machine was not changed.
