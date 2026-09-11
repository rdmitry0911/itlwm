# IWN passive roaming discovery: measured miss and bounded candidate

Status: production `ee501d78` is built and loaded; actual passive-channel
recovery is demonstrated twice in the radio, with off/on and real S3 recovery.
**This does not close general SAE roaming:** an authentication timeout in
depart-q5 remains a separate serious failure. All three post-S3 AP regression
modes have completed. This is a narrowly qualified passive-discovery fix,
not a declaration that WPA3/multi-AP reconnection is fully reliable.

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

- Production candidate is `ee501d78`. The dedicated retry and queued-band
  checks also pass on the Tahoe build guest. Their first macOS invocation
  exposed missing userspace endian/credential-scrub fixture definitions;
  Darwin test adapters were corrected without changing production code.
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

## Loaded-image receipts and remaining surface

Test-only portability commit `cdb2850c` does not change production identity.
All354 production files match manifest
`7c82cbd8d19c16c2e74784360bc74b07d6a51f5f69ac748ddcf4815fc2fbbe33`.
Tahoe build succeeds, all1085 imports resolve against BootKC. Mach-O UUID:
`DCCB5E44-25AE-30BE-930F-8975BFC10A6D`; SHA256:
`976c2addfe9dcc9d161f83502d999350da218f8f5ed699ba1fa4a0ed88d94f16`.
Private and installed AuxKC contain the exact five-member set. The sealed
bundle, installed bundle and extracted candidate ZIP are byte-identical.
The ZIP is15,685,533bytes, SHA256:
`0f17c3bf2d7a54942fa8677b9a46cc18c3a6e48a9792a601c75e77720f002040`.
Normal guest reboot gives boot`24A7472D-52A0-4F57-89F0-4EBFE68D38E6`;
observer direct-callsite calibration was repeated for this image/boot.

Three ca->02 returns were planned and completed; no repeat-until-green loop.
The initial depart-q1 source guard failed before any observer/request because
macOS had already moved from02 to ca after boot. Counts below are received
guest->gateway/external AX211->guest out of250 in each direction.

| Run | First channel13 / actual retries | Result | F/R | Encap drops |
| --- | --- | --- | --- | --- |
| return-q2 | target fresh/CRC15; retry12 | target02 RUN, ticket2 ACK |249/247|0|
| depart-q3 | retry12 | target ca RUN, ticket3 ACK |249/248|1|
| return-q4 | no target/CRC0; retry13 | fresh target/CRC11, ticket4 ACK,02 RUN |249/248|1|
| depart-q5 | no passive retry | first auth fails; macOS reconnects then roams to ca |203/208|0|
| return-q6 | no target/CRC0; retry12,13 | fresh target/CRC35, ticket7 ACK,02 RUN |248/248|2|

All five observers finish with zero DTrace errors. In q4 and q6, actual target
frames appear only in the standalone channel13 recovery command. Original
serials22/44 survive the continuations; normal5GHz follows and exactly one
upper terminal is claimed. Source departure, SAE/association and target RUN
then occur. This closes the **observed zero-reception fragmented passive
visit** mechanism, not every possible missed BSS and not lossless roaming.

Depart-q5 is deliberately retained as failed first-attempt roaming even
though the runner's final-target check returns0. Target ca was selected,
source-departure ticket5 was ACKed and the auth-beacon fence succeeded. Two
Algorithm3 TX descriptors were ACKed, but no association followed; about4.51s
later authentication timed out, IPv4 disappeared temporarily and native
recovery rejoined02 before a separate macOS-initiated roam(ticket6) reached ca.
No passive retry occurred in this failing request. The shared SAE engine
files are unchanged from986e030b and currently have no sent-Commit/Confirm
peer-response retransmission timer. This is a plausible recovery gap, **not
yet proof of which AP frame was lost**. Exact peer-phase observation and
bounded driver-owned retransmission are the next functional layer.

The final payload aggregate passes after all changes; SHA256:
`ef2e141e30ba8d52a0dd45f08d0a8a779c1d2d2ae96a616399692909cf3d498d`.

Native off/on rejoined WPA3/DHCP and passed20/20 packets each direction.
Real S3 was independently witnessed as QEMU suspended; guest pmset records
18:10:18->18:11:56UTC (98seconds). Same boot/UUID; WiFi DHCP starts18:12:02
(+6s). The premature initial SSH probe timed out before traffic. Once ready,
the WiFi-only check passed20/20 each direction while USB diagnostics were
absent; no off/on or reboot was needed. Exact USB devices were restored after
this receipt. GUI-after-sleep and AP-through-sleep are not claimed.

Post-S3 AP WPA3, WPA2 and open each pass external AX211 negotiation, DHCP,
20 forward/10 reverse packets, HTTP118bytes through guest USB/NAT backhaul,
normal stop/bridge retirement and10 STA gateway packets. WPA3 records
`key_mgmt=SAE, pmf=2`. All three normal-stop and exact host-profile restoration
receipts pass; open finishes at18:20:31UTC. This is not
simultaneous STA-radio backhaul. IWM/IWX are source/contract tested only;
their radio behavior is not covered by this6235 run.

A separate final STA health check after all AP modes retains a non-clean
traffic result:20/20 guest->AX211 and19/20 AX211->guest, with WPA3/DHCP intact.
Its strict runner returns1. It is not relabelled as a full bidirectional pass;
the earlier off/on and WiFi-only post-S3 checks remain distinct20/20 receipts.

Frozen evidence (including failed trials):
`/home/dima/Projects/itlwm/aiam-passive-roam-discovery-runtime.frmZ3s`.
`INVESTIGATION.md` records every retained run, observer revision and limitation;
all573 files verify against `EVIDENCE.sha256`, whose SHA256 is
`ccf4652625c5d2b608d3292590547f9193a81fab9c1995f961865f8365aedc40`.

## Published alpha receipt

The same [v2.4.0-alpha release](https://github.com/rdmitry0911/itlwm/releases/tag/v2.4.0-alpha)
was updated at18:30:27UTC, title`AirportItlwm Tahoe v2.4.0-alpha (ee501d78)`.
Asset557779468 has the15,685,533-byte/0f17c3bf... identity above. Independent
GitHub download at18:30:30UTC compares byte-for-byte with the frozen tested ZIP.
The release begins with the q5 SAE failure and final19/20 reverse-traffic
limitation; neither is promoted to a pass. Prior ADAB archive and metadata
are retained in`aiam-passive-discovery-release-20260911.ROevPG` for rollback.
