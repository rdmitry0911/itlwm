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
