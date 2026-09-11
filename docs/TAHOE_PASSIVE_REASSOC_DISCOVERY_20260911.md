# IWN passive roaming discovery: measured miss and bounded candidate

Status: candidate implemented and locally tested; **not yet built, loaded or
RF-qualified**. The published artifact is still the qualified source-departure
image from [the preceding cycle](TAHOE_SAE_ROAM_SOURCE_DEPARTURE_20260911.md),
production `986e030b`, UUID `ADABFCB9-0EE6-3FE0-AD81-01CDF616226B`.

## Reproduction and reference

On the same post-S3 guest boot `4CBD3236-33BE-48B8-8051-05DC34A550BB`, explicit
native WCL roaming from LabAP `82:c3:97:84:51:ca`/channel9 to
`9a:fb:5d:97:a9:02`/channel13 can complete without an eligible target. A clean
`passive-return-q5` observes no target beacon/probe response and no target node
in the fresh census. No source departure or target SAE is attempted. The
original WPA3 link survives:250/250 guest-to-gateway packets,249/250 external
AX211-to-guest packets, zero observer errors and encapsulation drops.

The actual command includes channel13 as EEPROM-passive, active36/passive85,
max-out112640us, packed home pause0x402800. Channel13 has two START receipts
and a final RESULT with good_crc0/probe_status0x81. The last start-to-result
span is about10ms; this is **not the total listening time**, because the
earlier fragment and firmware home-service boundary must be distinguished.
Unknown high probe-status bits are not assigned undocumented meanings.

The updated Ghidra `5995e24caa` was used for a fresh, read-only, exact-nm-bounded
40-function export with40 workers/decompiler interfaces. All40 completed.
BootKC25C56 SHA256:
`eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d`.
Export-manifest SHA256:
`af749a80958902d73653e6c2f9b6206c7f1b8fbb7ec3f6136aee66084d19dd6f`.

`configureScans` at`ffffff80015844d4` programs passive110. `startScan`
at`ffffff80016aca5a` programs home-away policy before the firmware scan.
The low-power/high-accuracy retry configuration is a distinct path; its
presence is **not proof of an automatic ordinary WCL_REASSOC retry**. The
candidate below is an Intel-specific adaptation to the measured DVM limit,
not a claim to have recovered Broadcom's whole firmware scanning algorithm.

Intel's [DVM scan code](https://raw.githubusercontent.com/torvalds/linux/master/drivers/net/wireless/intel/iwlwifi/dvm/scan.c)
limits dwell by associated-context beacon budgets; its
[notification ABI](https://raw.githubusercontent.com/torvalds/linux/master/drivers/net/wireless/intel/iwlwifi/dvm/commands.h)
provides per-channel scan results and a physical command completion. We retain
the existing dwell/home budget and wait for that completion before another
command. We do not turn a passive or DFS channel into an active one.

## Controls and limits

One-channel and13-channel native CoreWLAN controls can receive the target.
Their actual active20/home45 policy differs from the roaming path. Extended
CoreWLAN restTime/dwellTime arguments were normalized away by this macOS path;
they did not create a controlled timing comparison. A planned fourth such
parameter trial was cancelled as uninformative, not counted as a pass.

A returned CoreWLAN record alone is not fresh-RF proof. Likewise a final
good_crc0 after resumption does not exclude reception in an earlier fragment.
The corrected observer reads actual beacon/probe-response headers at the
parser entry and decodes firmware notifications only at the calibrated direct
RX-dispatcher callsite. The earlier four DTrace invalid-address faults during
nested management-frame allocation are retained as failed instrumentation,
not kernel crashes or qualified runs. Two early native controls also had
overlapping observer tails; their traffic windows did not overlap.

Two bounded monitor-mode trials used only the laboratory AX211 while keeping
wired management and guest USB diagnosis. The first successful trial,
`air-return-q3`, captured693 target beacons, median interval102.404ms, and real
guest probes/target responses.509 exact frame identities matched the guest RX
observer. It roamed successfully (source-departure ticket16), with249/250
guest-to-gateway packets. This topology did not exercise reverse host traffic.

The final trial,`air-return-q4`, reproduced the miss while AX211 still captured
target beacons (688 total, median102.397ms). The guest received no target frame,
made no target selection and kept source WPA3/250-of-250 gateway traffic.
The last resumed visit again ended after about10ms. Packet clocks are not
assumed identical: matching received frames and a separate eight-sample SSH
clock-bracket receipt retain their measured offsets and uncertainty. Missing
individual packets in the monitor capture are not called AP silence.

Both successful monitor fixtures deleted only their own monitor interface and
restored host profile`bbed72a6-b9cb-4170-b207-b3a53998b32e` on`wlp0s20f3`.
Two earlier fixture setup failures occurred before any guest scan and are
retained. No physical`.22`, QEMU process/lifecycle or disk image was changed.

## Candidate contract

Only a tagged explicit associated IWN roam can seed recovery, at its actual
first command doorbell. Candidates are the original admitted2GHz channels
that are EEPROM-passive and non-DFS. AP/PAN, BTM, ordinary public/controller
scans, foreground joins and IWM/IWX keep their existing policies.

Actual successful-channel CRC receipts remove candidates. A zero final count
leaves a channel eligible for one extra visit; it does not certify AP absence.
At STOP_SCAN the exact lease atomically consumes one remaining channel bit and
enters ARMING. The continuation contains just that channel, uses unchanged
dwell/home constraints, and cannot reseed its bitmap. Then normal5GHz scanning
and the original single upper completion proceed. Every original bit can be
used at most once, so there is no repeat-until-success loop.

The doorbell revalidates physical serial, WCL owner/source epoch and the exact
retry channel. Cancel/reset/terminal fences reject new work. A no-doorbell
failure restores only its still-owned old terminal as aborted; if a successor
has replaced it, the obsolete caller cannot consume the successor's terminal.
The notification decoder requires the exact16-byte payload/four-byte firmware
header and notification queue before packed-field access. No credential copy
or cached BSS is used as a replacement for fresh reception.

## Local checks / remaining gate

- 65,563 scenarios compile the actual retry, receipt-switch, doorbell and
  terminal-claim bodies, including all65,536 candidate masks, one visit per
  admitted bit, no reseeding, early/stale/cancelled/zero-serial cases, malformed
  receipts and failed/replaced submissions. ASan/UBSan and warnings-as-errors.
- The reused19 command/abort scenarios also pass; ordinary queued-band and
  full join-attempt regressions pass. The queued-band fixture needed missing
  declarations for an already-existing join-cleanup branch; that production
  worker is byte-identical to HEAD before this candidate.
- Full payload aggregate passes, including real IWM/IWX command builders and
  owner/retirement tests. Log SHA256:
  `16cc5af11eebd020ba65596784638c503460d2d963b54238648471f3793b1423`.
  The later zero-serial guard and additional terminal assertions pass the
  dedicated test again. No IWM/IWX radio qualification is claimed.

Next required gate: build/activate this exact candidate in the disposable
guest; recalibrate the observer after the image/boot changes; reproduce real
ca->02 discovery with an actual recovery command, source departure, SAE/PMF,
DHCP and bidirectional traffic. Then exercise off/on, real S3 recovery and AP
regressions before updating the public kext. This document does not close that
runtime gate or claim the intermittent discovery surface is already fixed.

Evidence working copy:
`/home/dima/Projects/itlwm/aiam-passive-roam-discovery-runtime.frmZ3s`.
`INVESTIGATION.md` records every retained run, observer revision and limitation;
this cycle's evidence is not yet frozen as a qualified release manifest.
