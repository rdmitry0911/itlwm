# Reassociation scan stranded after source loss — 2026-09-11

## Real failure and reference

On bb142d93, UUID `7BE91101-7829-3D70-8BDC-BD39C8850E3A`, boot
`C17BA719-545D-4127-B44A-8632DF5C52E0`, removing the controlled WPA2 AP
after real S3 and an off/on check stranded automatic return to saved WPA3
LabAP. Physical host 10.90.10.22 was not touched. Evidence root:
`/home/dima/Projects/itlwm/aiam-iwn-auth-beacon-runtime.yIJOEP`.

The existing serial log shows accepted WCL reassociation scan followed by
real firmware beacon loss. Subsequent joins repeatedly return
`REASSOC_SUPERSEDE_BUSY error=16`. Read-only 45-second DTrace runs establish:

- q1: owner serial 6, active 1, leaf 2 (SCAN_STARTED), common flags 0x5a0c0801.
- q2: actual `iwn_wnm_bgscan_abort` sees sc_flags 0x1cf, without its 0x200
  BGSCAN bit, and returns EBUSY before the abort reservation helper.
- q3: source association epoch 59, current epoch 220; a real physical
  terminal succeeds, then common WCL-foreground completion (mode 2, tag 0)
  is rejected while owner 6 remains active.

All three observers ended normally with zero DTrace errors. q1's lower-abort
wildcard did not match the actual `iwn_wnm_bgscan_abort` name; q2 corrects it.
q2 printed the bool physical-terminal return as a full integer, whose upper
bits are unspecified; q3 uses uint8_t. Only q3 is used for that bool result.
Offsets come from disassembly of the exact loaded UUID, not guessed layouts:
ic owner 0x628, source epoch 0x630, active 0x648, phase 0x64c, flags 0x14cc,
current epoch 0x14f0, softc pointer 0x28; softc flags 0x2530. No kernel writes,
forced state changes, manual reconnect or radio reset occurred during these
observations.

The updated Ghidra checkout on 10.7.6.112 was independently verified clean
at `5995e24caa83fa74d841c0cfc7c24f59773c3459`, branch
`feature/bounded-function-parallel-decompiler`. Its saved 25C56 export of
`WCLRoamManager::linkDown` at `ffffff8002105ae4` clears pending roam state
and invokes timer/policy cleanup. `WCLNetManager::linkDownInd` at
`ffffff80020edee8` handles the independent firmware beacon-loss indication.
These are logical lifecycle facts, not evidence that Intel firmware has
already completed a scan. No additional decompile was needed for this fix.

## Correction

Source-epoch cancellation now retires only that source's logical scan-phase
reassociation owner, under the existing selected-BSS leaf before any yielding
revocation callback. SETUP, SCAN_STARTED and SCAN_FAILED qualify; target
switching and on-air reassociation retain their own terminal lifetime. Exact
source epoch, active serial and monotonic sequence must match. The common
BGSCAN markers and request values are cleared, but the sequence and historical
accepted-scan receipt are preserved.

No physical scan lease, command, DMA ownership or completion is fabricated or
cleared. Existing lower abort/terminal handling remains authoritative. A late
tagged scan result cannot select for a new request. No additional 0xcf or
synthetic on-air result is published: the source cancellation/leave/link-loss
path already owns its notification. This common correction covers IWN, IWM
and IWX source cancellation, without claiming new MVM hardware evidence.

## Executable verification so far

The fixture compiles the complete actual epoch-cancellation function, scan
owner cleanup and scan-completion admission, alongside the existing real
carrier/BSS replacement functions. Kernel leaves, crypto, lower firmware and
controller transport remain explicit doubles. Linux ASan/UBSan passes 130
cases, including stale/missing identity, each pre-target phase, preservation
of post-scan phases, repeated cancellation, late physical tags, and a new
owner admitted by a revocation callback. The ordinary aggregate passes.

The unchanged bb142d93 epoch function, substituted at its original location,
fails the old-owner-retirement assertion (exit 134). Initial negative-harness
ordering and out-of-range byte fixture constants were corrected; their
failed logs remain, and are not counted as semantic regression evidence.

Logs (SHA-256):

- green q3: `06d26216ecf7d359495f84229b5d2c40ecba605bcdcbaad4d87c0649b9f9f26a`
- unchanged baseline q3: `b1dd8d5ceaa18b0d477d3e185b45705b31b904dc0d44d5a46164b187cf267928`
- aggregate q1: `e9de6ec1d3509fda8b9d517aaee57cdd69164e5c53f158341a2ccbddb916926d`
- live observer q1: `6eaec0b1a25440e477a939e95bebea722a66e25e5a55c428defe2520c585a68d`
- live observer q2: `cdb3edd2b41f1dc397a7b846d57e6e7176d102f65d9e54305127153e20eb0cc5`
- live observer q3: `dd80be10eecb87273d1c676b6091319a045ac7017c723c6feaeced131e04fb6d`

macOS ASan/UBSan also passes all 130 cases. The full 353-file predecessor
manifest was verified before the exact five changed source/test files were
copied to the build mirror, then the full candidate was verified. Ordinary
AP-capable Tahoe build succeeded; all 1085 undefined symbols resolve against
the guest BootKC and no `_thread_call_cancel_wait` dependency exists.

- source manifest SHA-256:
  `351889e0d098a0586b8423ece8ddd1206b9949e133422d1d6b51ac8aafb54520`
- embedded source ID: `351889e0d098`
- Mach-O UUID: `412FE602-291C-3683-877A-823470E766D1`
- Mach-O SHA-256:
  `883a37a204ec4039307d4f9a8a8e65da2d8d0fbc25ca5348e103e0f62ce7428c`
- build/macOS-test log SHA-256:
  `47f16bd99efbc3a7ae09fc70eedd924303a3a3e53952a0979f216544634fd7ad`

Real failure/recovery qualification remains required. Do not promote this
source/build correction to a public release before those checks.

## Exact-image runtime follow-up

Source commit `52951b81` was pushed before activation. The owned QEMU
PID/name and writable lab overlay are unchanged; the latest working 17b copy
and all backing images remain untouched. Runtime evidence for this candidate:
`/home/dima/Projects/itlwm/aiam-reassoc-source-runtime.sE0SPZ`.

Private preflight passed with five AuxKC members and no canonical mutation.
The first activation invocation incorrectly named the source snapshot instead
of the preflight snapshot; it failed its path check before mutation. The
corrected invocation returned READY at 11:31:19 UTC. Ordinary guest reboot
loaded UUID `412FE602-291C-3683-877A-823470E766D1`, boot
`D53B731D-A453-49CF-80A0-46E6DD3E0956`. Native boot auto-join reached
WPA3/SAE and DHCP. The unsigned private candidate is not a signing claim.

| 1400-byte ICMP observation | Forward | Reverse | Maximum RTT, ms |
| --- | ---: | ---: | ---: |
| Boot WPA3 | 20/20 | 19/20 | 59.412 / 34.271 |
| Native WPA2 selection | 20/20 | 20/20 | 64.946 / 118.914 |
| WPA2 after radio off/on | 20/20 | 20/20 | 114.588 / 18.309 |
| Automatic WPA3 return after WPA2 AP removal | 20/20 | 20/20 | 59.551 / 1077.238 |
| Native open-network selection | 20/20 | 20/20 | 119.742 / 172.847 |

Boot's lost reverse packet and the 1.077-second return-path outlier remain
recorded; this is not a claim that all data-path loss/latency is fixed.

The WPA2 fixture was removed and host restoration finished at 11:37:00.
At 1789126621503035113 ns the exact new helper saw scan owner 3,
SCAN_STARTED, source epoch 36 while cancellation advanced to 37. Its return
at 1789126621503040096 ns shows active=0, serial=0, BGSCAN cleared. The
following scans were admitted normally, and WPA3 DHCP's lease began at
11:37:08 UTC. No post-failure manual network selection or radio toggle was
issued. After the open fixture, WPA3 also returned automatically (DHCP lease
11:40:10). Fixture host restoration returned zero and the wired default route
was unchanged in both experiments.

The 300-second read-only observer ended with zero errors, SHA-256
`21407ecd788f567c585ba018ee75a02b8be0453a28e772228df7b754db6eb77b`.
No `SUPERSEDE_RESULT=16` was observed. Its field offsets were rechecked
against this exact image. One preliminary symbol spelling had length 44
instead of 46 and produced no helper disassembly; the corrected result is
retained separately as `cancel-layout-q2.log`.

The predecessor's frozen stuck-state evidence manifest is
`a2af19313a9e6d0cf2dfbb00dfc4a1fde42888699213b6ff1994f4196ddb2e6e`.
Its missing-permission disassembly attempt was empty; a separate sudo
read-only extraction was added afterward and is bound by this candidate's
`predecessor-evidence.sha256`. The original manifest was not overwritten.

## Same-image S3 and repeated source-loss recovery

The diagnostic USB Ethernet was removed while awake, and direct Wi-Fi SSH
independently verified its absence. A sleep request at 11:41:26 UTC reached
actual ACPI S3 at 11:41:56; the owned QEMU was observed `paused (suspended)`.
One monitor wake at 11:42:36 ended 40 seconds of actual sleep. The boot UUID
and loaded image were unchanged. Saved WPA3 DHCP started at 11:42:42, without
another network selection or radio toggle. Both 1400-byte traffic directions
completed 20/20 before diagnostic USB restoration; maximum RTTs were
68.856 / 32.120 ms. The first immediate post-wake SSH attempt timed out before
DHCP; the later successful readback is not substituted for that failed probe.

The same post-S3 boot then repeated native WPA2, off/on and controlled AP
removal. No reboot separates these observations:

| Post-S3 1400-byte ICMP observation | Forward | Reverse | Maximum RTT, ms |
| --- | ---: | ---: | ---: |
| Native WPA2 selection | 20/20 | 20/20 | 90.838 / 91.316 |
| WPA2 after radio off/on | 20/20 | 20/20 | 35.522 / 22.094 |
| Automatic WPA3 return after WPA2 AP removal | 20/20 | 20/20 | 82.224 / 57.723 |

At 1789127208437255861 ns the helper retired scan owner 9, SCAN_STARTED,
source epoch 132 after advancement to 133. Return at 1789127208437260053 ns
shows active=0, serial=0 and BGSCAN cleared. The host fixture finished restoring
at 11:46:48 UTC, and the guest's automatic WPA3 DHCP lease started at 11:46:54.
No post-failure manual join or radio toggle was issued. The 180-second observer
ended normally with zero errors and no `SUPERSEDE_RESULT=16`, SHA-256
`08528b2fc314a558bc818f73e8b69580538e21feb5e728e4c2016cb9494d40b9`.

This closes the reproduced stranded logical scan-owner failure on actual
IWN/6235 before and after S3. It does not close all candidate policy, timeout,
MVM physical-retirement or data-path loss/latency issues. The read-only VNC
capture after S3 reports that the guest has not initialized the display;
pmset also records WindowServer's 30-second sleep acknowledgement timeout.
Thus the Wi-Fi results do not qualify post-S3 GUI availability. No graphics
or daemon reset was used to relabel this limitation.

The terminal STA evidence, scripts and a frozen serial checkpoint are bound by
`evidence-sta-posts3.sha256` in the runtime root, manifest SHA-256
`298eccf4a8307ae21a85906cf050c2aa57ea672907a7a4d86bd3d665d6187f57`.
GUI and AP-role regression of this candidate remain required before public
release promotion. Physical host .22 and unrelated VMs remain untouched.
