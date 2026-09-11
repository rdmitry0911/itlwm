# IWN AUTH: owned nonblocking preparation — 2026-09-11

## Qualification at the source checkpoint

This implements the correction motivated by the real 309.258 ms AUTH call
and two-endpoint roam loss in [the predecessor capture](TAHOE_IWN_AUTH_BEACON_WAIT_20260911.md).
The candidate builds but **has not yet been activated or RF-qualified**.
The public release is unchanged. Passing fixtures do not erase prior losses.

## Production behavior

STA AUTH and RUN-to-ASSOC preparation are queued on the existing main
workloop. Copied ownership contains monotonic serial, hardware generation,
join/association/reassociation identity, source state, channel, BSSID and
station MAC; no node, mbuf or credential survives ingress. Identical repeated
requests preserve the original deadline and do not repeat RXON.

The source RX/TX callback no longer runs the three-beacon-interval IODelay.
Generic AUTH/ASSOC waits for owned RXON, broadcast ADD_NODE and LINK_QUALITY
receipts. ADD_NODE must contain success status. Under the original
cold/switching/no-response-timer condition it also waits for a real matching
target beacon. Its RX_PHY must postdate the matching RXON receipt; cached
preceding PHY, probe response, wrong transmitter/BSSID/channel and expired or
superseded observations cannot admit AUTH.

Command enrollment is fenced at the real doorbell by selected-BSS and AUTH
leaves. Generic commit occurs once, without replaying hardware/epoch setup.
Primary management/data queues stay intact during preparation; after AUTH,
management drains while ordinary STA data waits for RUN. The direct SAE
producer starts below the same generic admission, so it cannot bypass it.

INIT/stop/reset revoke ownership; reopen advances hardware generation. The
five-second timer signals failure, never successful beacon readiness. Fresh
failure uses the common ledger, acks only the retired producer and requests
existing lower/SAE cleanup. GEN0 failure uses owned reassociation failure
when available, then INIT and a recovery scan guarded against a new owner
or intervening stop. Full GEN0 WCL 0x4a/0x50 event parity remains open.

## Real firmware receipt representation

The observer ran on predecessor boot
`E8E936A5-94B0-44A1-9C16-736EAB2470F5`, UUID
`7B77B2CB-D2B3-3B72-BCBF-5EFF73B5F53A`. One native roam to
`9a:fb:5d:97:a9:02` was accepted at 10:26:57 UTC, without resubmission.
The bounded observer ended with zero errors: RXON (16) and LINK_QUALITY (78)
had masked length 4 and flags 0; ADD_NODE (24) had length 8, flags 0 and
status 1. This validates the representation, **not** the new candidate's RF
behavior or each captured command's specific origin.

Evidence root: `/home/dima/Projects/itlwm/aiam-iwn-roam-datapath.JqVxWt`.

- `auth-preparation-receipt.d`: SHA-256 `44b296bc0f060129b5b0d2b8d99d4978f913b480522d569ee7f9dca27f4d5534`
- `auth-preparation-q1.log`: `e6209aa59940965d87f8e3720c1819c78bf4ff5bcfb242fb86d7ba5cb44d62ca`
- `auth-preparation-q1-request.log`: `2b55b94c2526efcc82e4c7fa9759bcf0fe08589b8e26c836ee61d060672dba82`

The ADD_NODE contract matches the pinned Intel DVM
[definition](https://github.com/torvalds/linux/blob/v6.18/drivers/net/wireless/intel/iwlwifi/dvm/commands.h#L819-L828)
and [response processing](https://github.com/torvalds/linux/blob/v6.18/drivers/net/wireless/intel/iwlwifi/dvm/sta.c#L75-L115).

## Executable checks and build

Linux/macOS ASan+UBSan: 35 continuation scenarios pass. They compile the
production helper plus complete `iwn_newstate_impl`, `iwn_cmd`,
`_iwn_start_task` and three common join helpers. IOKit, hardware leaf
operations, crypto and the generic callback are explicit test boundaries.
Coverage includes real ingress/queue retention, partial/duplicate/late/error
receipts, cached PHY, stale identity/timer, reentrant replacement, timeout
recovery and source allocation/attachment failure unwinding.

The complete `iwn_auth` test passes roam/cold/incoming/retry/command-failure
with zero busy-wait microseconds. Selecting predecessor `13997c74` still
fails roam with `busy_us=307200` (exit 134). The complete SAE worker suite
passes 21 cases, including failure before engine creation and pending
transport-terminal retirement. The ordinary Linux aggregate passes; known
red MVM/BSS-drain gates remain separate and open.

Production manifest (353 files):
`/tmp/aiam-iwn-auth-beacon-production-20260911-q2.sha256`, SHA-256
`03a8af4bc3c8d9290357afa61c3dbea8a940a2e21664e035333b4decfcc3e9fe`.
The complete predecessor was verified before copying exact changed files;
the complete candidate manifest was verified afterward. Source ID
`03a8af4bc3c8`. Ordinary AP-capable Tahoe build succeeds; all 1085 undefined
symbols resolve against guest BootKC, with no `thread_call_cancel_wait`.

Mach-O UUID `7BE91101-7829-3D70-8BDC-BD39C8850E3A`; SHA-256
`c193ecf9ece52f37eadd9a09ff52899982d4c1a7d363d0011e9fa9cfc2c706c7`.
Build log `/tmp/aiam-iwn-auth-beacon-build-20260911-q3.log`:
`ea9610c9d6e2c2d1f1cf5f92ccf3a13164480d6e7d919cb64fb1f0fb2fbcc91c`.
macOS 35-case log `/tmp/aiam-iwn-auth-beacon-macos-20260911-q4.log`:
`97498e2ae2b0db2bed5ad4e01551db7ee70f5df07dc571a32038a02afc18cca9`.

The first manifest attempt failed on a spaced filename before remote copy;
the first macOS fixture lacked the existing user-space `explicit_bzero`
support header. Both failed logs are retained and excluded from PASS claims.

## Required runtime continuation

Activate only on an offline copy of the owned working guest. Verify loaded
UUID, WPA3 auto-reconnect and bidirectional traffic; repeat the same roam
capture, open/WPA2/off-on, real S3 and timeout/replacement checks. Retain losses
and incomplete GUI/AP/IWM/IWX coverage. Do not touch physical host
10.90.10.22, unrelated QEMU or shared backing disks.

## Runtime follow-up (same candidate, source commit bb142d93)

The source commit was pushed before activation. An ordinary offline byte
copy of the working 17b guest and its OVMF variables was verified; the
working source remains local and unmodified. Runtime root:
`/home/dima/Projects/itlwm/aiam-iwn-auth-beacon-runtime.yIJOEP`.
Preserved disk SHA-256 `7c8c42cf2411df625f2dd75e54dbf422430cde52f274bb89965ae23baa16cb3e`,
variables `8ad9e6e16f7edb6d31b388b62d51cec4fc293d8733590111f134a099ab9a2686`.
Owned QEMU PID 1406911, name `aiam-iwn-after-scd-control`.

Private AuxKC preflight and transactional five-member activation passed.
The test artifact is unsigned (codesign verification exit 1); private
admission PASS is not a production signing claim. Reboot at 10:45:47 UTC
loaded the exact candidate UUID above. Current boot:
`C17BA719-545D-4127-B44A-8632DF5C52E0`. Native WPA3/SAE auto-connect to LabAP
(`82:c3:97:84:51:ca`, channel 9) received DHCP `172.16.66.219` at 10:46:22.
Both initial AUTH continuations reported zero error.

1400-byte ICMP observations, with host AX211 as the independent reverse end:

| Run | Guest → router | Host → guest | Max RTT, forward / reverse |
| --- | ---: | ---: | ---: |
| Boot auto-connect | 20/20 | 20/20 | 25.466 / 28.229 ms |
| Native roam ca:9 → 02:13, q1 | 239/250 | 237/250 | 135.493 / 206.568 ms |
| Native roam 02:13 → ca:9, q2 | 242/250 | 242/250 | 139.747 / 145.652 ms |

The roam counts are **not an improvement claim** over predecessor 247/250
and 246/250. They retain a real association outage. Neither run toggled the
radio or resubmitted its accepted native roam request. Both capture pairs
and DTrace observers finished naturally, with zero kernel capture drops and
zero DTrace errors. q1 captures: guest 979 / host 497 packets; q2: 988 / 504.

The complete `iwn_auth` calls now measured 6478 ns (q1) and 7571 ns (q2),
instead of the old 307200 us busy wait. Firmware receipts then arrived
asynchronously. q1's SAE-to-ASSOC continuation occurred at
1789123740114037398 ns and RUN at 1789123742070742847 ns: the remaining
1.9567-second interval is **after** SAE, not the removed AUTH wait.

q2 adds an exact typed return probe of `ieee80211_assoc_comeback_parse`:
the AP returned status 30 with 1000 TU, parsed as 2 integer seconds, at
1789124022000851173 ns. The real retry was sent at
1789124023051756987 ns (1.0509 seconds later), followed by Association
Response and RUN. This proves an AP-directed comeback in q2; q1 had no
such probe and its cause must not be retroactively asserted as proven.
q1 additionally retained two guest-BPF replies absent at host (reverse
sequence 82 and 248). q2 retained one `ieee80211_encap` rejection and a
separate missing host request at sequence 93. These remain open.

The existing watchdog rounds 1000 TU up to two ticks and depends on the
phase of an unrelated second tick. Exact monotonic comeback timing is the
next reconnect issue to test, not a reason to ignore the AP's interval.
Updated reference `AppleBCMWLANCore::handleAssocEvent` at
`ffffff800159f976` reports status/reason via 0x4e and delegates extended
data; this host-side function does not prove the firmware's comeback timing.
Pinned [mac80211 handling](https://github.com/torvalds/linux/blob/v6.18/net/mac80211/mlme.c#L6005-L6022)
uses the AP interval to set a future deadline, without a whole-second
countdown. No new timing correction is implemented at this checkpoint.

For space, the inactive, childless historical 20dd leaf was archived and
hash-verified on `10.7.6.112`, then removed locally at 10:49:40. Archive:
`/home/dima/Projects/itlwm-runtime-archive/auth-beacon-space-20260911.Ko6sy2/`.
Disk `tahoe-mgmt-queue.qcow2`, 1174077440 bytes, SHA-256
`fcc7a8ace06bd0146416d1b557e600b4f2232b3a54ac7384d7751668fc3eef1d`;
OVMF SHA-256 `281ec757b967ea88d53537d0fabf29842d9358b09ad758c55cd605cc825f0ed9`.
The 374-image census had no child using that leaf; fuser was empty. Its
shared backing and the latest working 17b recovery copy remain local.

## S3, WPA2 and a retained hard-loss failure

The same bb142d93 image entered real S3 at 10:58:53 UTC and woke at
10:59:39, with QEMU observed suspended and one `system_wakeup`. The boot UUID
did not change. WindowServer's 30-second sleep-ack timeout is recorded, not
attributed to Wi-Fi. WPA3/SAE and DHCP recovered; before restoring diagnostic
USB, 1400-byte ICMP passed 20/20 in each direction (maximum RTT 19.325 /
22.911 ms). The first sleep attempt stopped on an incompletely removed USB
interface and did not request sleep; it is not counted as an S3 test.

Post-S3 native selection of the controlled WPA2 AP and a subsequent radio
off/on both recovered DHCP 192.168.73.35 and passed 20/20 packets each way.
Maximum RTT was 77.344 / 90.600 ms after selection and 84.068 / 163.256 ms
after off/on. This does not qualify post-S3 GUI or active-AP-role sleep.

After that AP was removed at 11:03:38, automatic return to saved WPA3 LabAP
**failed**. The guest remained powered on but inactive with no Wi-Fi IPv4;
diagnostic USB remained healthy. No manual selection or on/off was used to
hide this failure. Exact-image read-only probes subsequently found logical
reassociation owner 6 still in SCAN_STARTED, common BGSCAN set, while IWN's
physical BGSCAN was clear (`sc_flags=0x1cf`). Its source epoch was 59 while
the current epoch reached 220. New physical scans completed, but their
common completion was rejected by the stranded owner. See
[the next correction](TAHOE_REASSOC_SCAN_SOURCE_CANCELLATION_20260911.md).

Consequently this candidate is not public-release qualified. Open/AP-role
regression on this image has not been completed; retained roaming loss,
precise association-comeback timing and IWM/IWX hardware coverage remain
open. The public release remains unchanged.
