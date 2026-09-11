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
