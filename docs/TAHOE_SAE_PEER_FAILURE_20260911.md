# Real SAE peer failure and candidate retirement — 2026-09-11

## Proven pre-fix boundary

On real IWN/6235 with source `4d979f3b`, boot
`D946CBDB-621A-4524-9D0C-4E3FE52C1E88`, the controlled wrong-password SAE AP
produced real status-1 Confirm rejections. The read-only observer covered
03:54:16–03:55:47 local time (UTC+3), terminating normally with zero errors.
The in-kext crypto engine consumed empty-body Confirm rejections at association
epochs 32, 54 and 69 and returned PEER_AP_REJECT (4) each time. Retirement
requested generic SCAN and no AUTH failure entered the common join ledger.
A separate NO_NETWORKS producer and successful SAE epoch 53 were also observed.
Thus the observed rejection was not lost at 6235 firmware RX or short-frame
parsing. This does not prove every PMF or rejection path.

Evidence root: `/home/dima/Projects/itlwm/aiam-iwn-night-runtime.wCIvBU`.

- `night4d-auth5-observer.log`, SHA-256
  `f54a54eadcf8e431535e477c12ffca665ac54bc930135f19b73c7baa2d3c01eb`.
- `night4d-auth5-native.log`, SHA-256
  `ec0ec4af130f75c296058d493a9a15b65aa86f1ede362a98df55c6ac63582522`.
- External hostapd log:
  `/tmp/aiam-gui3e73-runtime.kiWiTP/night4d-auth5-hostapd.log`.

The auth3 request returned EBUSY and its fixture stopped before authentication;
auth4 was admitted but its real scan ended NO_ELIGIBLE_TARGET. Their empty
failure observations are not RX-negative controls. Auth5 kept the real AP
beaconing for 15 seconds before the accepted request. The fixture cleaned up
normally and restored host managed Wi-Fi without changing its wired route.

## Implementation and executable regressions

Production commit `97fe747c` and fixture portability commit `5e7a6735` are pushed.
The common JoinAdapter generation is captured at SAE producer admission and
kept separate from the credential request/relay generation. A consumed peer
failure claims only that exact current AUTH request under selected-BSS then
engine leaves. Peer statuses remain peer statuses; local crypto/method failures
use a local error with no invented received status. The cancelled owner retains
the generation and requests the existing owned lower cleanup instead of an
autonomous next scan. Producer acknowledgement follows local value scrubbing;
lower and SAE acknowledgements remain separate actual retirement edges.

The full production worker, claim and retirement methods pass 19 ASan/UBSan
scenarios on Linux and macOS, including same-BSS same-epoch replacement and
delayed TX terminal retirement. The pre-fix complete worker independently
compiles and fails the missing-producer assertion (exit 134). Crypto outcome,
task scheduling and lower firmware cleanup are explicit fixture boundaries.
Existing common join/physical scan/controller payload tests pass on both OSes;
the complete Linux payload aggregate passes. The first macOS fixture compile
failed on its SDK's absent explicit_bzero; the existing portable test support
fixed that test-only issue before the successful run.

Source and guest production fingerprint:
`a9b779d9482a218b801a40253e16ebf39fc17fa67659b458de1f9cf7adb87c10`.
Built UUID: `69D766FF-86F9-3337-ADE0-48361B68A59C`.
Built Mach-O SHA-256:
`560a3f5bb365a7f5c9a5e6f8d7418b0ff0db0931e392901f38aa658a4e8f5a63`.
All 1085 external symbols resolve against the running Tahoe BootKC; there is
no thread_call_cancel_wait dependency.

- Linux aggregate log: `/tmp/aiam-sae-peer-failure-linux-20260911.log`, SHA-256
  `7f9e497b8b852c182ec5e31ce82a4338d180d5c4eaf5c062ea921d5592a6d8c9`.
- macOS test/build log: `/tmp/aiam-sae-peer-failure-macos-20260911-r2.log`, SHA-256
  `b954b445fae179740ebae520b9ddfd6199acbccf11e449a9220e4aaa5f67ade7`.

## Loaded-image rejection recovery

Private AuxKC admission and transactional activation passed. Activation root:
`/private/var/tmp/aiam-iwn-activation-sae-peer-97fe747c-1`, transaction
`activation-20260911T012053Z`. After a normal guest reboot, boot UUID
`02DED0EA-E840-4DAE-BED2-1067761106C1` loaded the exact `69D766FF` image above
on the passed-through IWN/6235. Four companion kexts were preserved; no unload
was attempted. The first admission-helper invocation stopped before staging
because a pipefail/grep-early-exit check returned 141; the corrected read-to-EOF
check passed. The first completion-observer compilation rejected a signed
format argument; an explicit uint64_t cast passed compilation before any RF
fixture was admitted. Neither failed setup is counted as a radio test.

The `peer97-auth2` observer covered 04:23:43–04:25:14 local time (UTC+3), with
zero diagnostic errors and normal terminal exit. One native directed request
was accepted at 01:24:02 UTC, after 15 seconds of real fixture beaconing.
Its first reassociation rejection has no fresh JoinAdapter generation, as
expected: this patch does not invent an owner for the separate 0x49 path.
Subsequent fresh saved-profile attempts exercised the corrected producer:

| Local time | Fresh join generation | Actual observed sequence |
| --- | --- | --- |
| 04:24:20 | 2 | SAE Confirm status 1, empty body; engine rejects; producer/lower/SAE cleanup acknowledgements 1/2/4; one failure publication returning 0; successful SAE with the valid same-SSID BSS at epoch 33 |
| 04:25:04 | 4 | Same real rejection and three cleanup edges; one publication returning 0; successful SAE with the valid BSS at epoch 56 |

Native configd logs publish IPv4/DHCP at 04:24:22.286 and 04:25:06.501.
The independent watcher confirms restored active en1 with 172.16.66.219.
No extra join command, radio toggle or reboot caused either recovery. The
fixture then terminated normally and restored host managed Wi-Fi at 01:25:30
UTC while retaining its wired default route. Independent subsequent 1400-byte
checks passed 20/20 guest-to-router and 20/20 host-AX211-to-guest; the same boot
and loaded UUID and a real DHCPACK were revalidated. The captured new serial
interval has no matching panic, firmware fatal/error or device-timeout text.

Evidence in the private root:

- `peer97-auth2-observer.log`, SHA-256
  `1def008d253c44b777cc212316f6054f262aa4b0199c92336ee1830a8f44331c`.
- `peer97-auth2-watch.log`, SHA-256
  `4e9a94398b0a4189be9272291c637c5666dd8744aa77ebbf75612d10f324f51e`.
- Complete info/debug `peer97-auth2-native.log`, SHA-256
  `ca5b6506058936f6963aa1811a48835bf64d3d852e9c04fdbb9630f76bdc38d9`.

This confirms the fresh-AUTH received-SAE-rejection/cleanup/publication path,
not complete candidate policy. The system revisited the bad BSS during the
same observation. Reassociation's separate 0x49 completion, real timeouts and
remaining AUTH/ASSOC/key producers remain open. Exact-image GUI/sleep/AP
regressions and IWM/IWX hardware qualification are still required. The public
release remains `b5c6cfd8`; the full functional objective is unchanged.

## Verified space reclamation and recovery copy

Only the old offline July-24-d qcow2 was removed locally, after successful
byte-copy rsync, independent source/remote hash and length comparison, a
read-only owned-image backing census with no child reference, and final fuser
check with no open user. Its small companion files remain local.

- Original: `/home/dima/Projects/itlwm/pmf-live-20260724d/tahoe-pmf-runtime.qcow2`.
- Archive: `dima@10.7.6.112:/home/dima/Projects/itlwm-runtime-archive/sae-peer-space-20260911.58o3vp/`.
- Length: 1,186,856,960 bytes; SHA-256
  `964b02e833e6c2e2a0f1f75166c372218528f5468680527392a56e2bf011a41c`.
- Unchanged backing file: `/home/dima/Projects/itlwm/tahoe.qcow2`.
- All four archive files, including OVMF variables, attestation and serial log,
  independently match their source hashes. Restore the qcow2 to the original
  absent/unused path with rsync -aS, then recheck the hash before use.

The current guest was subsequently shut down normally. Serial confirms APFS
unmount and power-off; QEMU PID 1047654 is gone and its disk has no open user.
Its now-stopped current image still contains loaded/installed `4d979f3b`:

- Disk: `aiam-iwn-night-runtime.wCIvBU/tahoe-night-candidate.qcow2`, length
  1,580,531,712 bytes; SHA-256
  `b447ba844b37db50972a62582135884ea4353ced91465fb858284a9cb2b1e046`.
- OVMF variables: `aiam-iwn-night-runtime.wCIvBU/OVMF_VARS.candidate.fd`, SHA-256
  `492e589198a6fcf3d5b6cc1d56c96bfda31c4b152a121cd80508bce2ae1f8e8a`.
- Verified working-image archive:
  `10.7.6.112:/home/dima/Projects/itlwm-runtime-archive/working-4d979f3b-20260911.dycr30/`.

The byte-copy rsync completed successfully. Independent remote disk/OVMF hashes
and disk length match; local hashes were checked again while stopped and still
match. Only then was the same owned guest started with a new serial log,
`serial-sae-peer-candidate.log`, to stage the new candidate. Restore both disk
and variables from this archive only while this exact VM is stopped, preserving
the existing backing chain. The older local read-only parent and base are
unchanged. No physical user host,
unrelated VM, user-owned Build directory or shared filesystem setting changed.
