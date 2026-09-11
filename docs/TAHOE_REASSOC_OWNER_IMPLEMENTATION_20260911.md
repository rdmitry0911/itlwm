# Accepted-roam identity implementation — 2026-09-11

## Current result, not a completed roaming release

This follows the exact-reference FIX_CANDIDATE in
[the lifecycle reconstruction](TAHOE_REASSOC_FAILURE_LIFECYCLE_20260911.md).
The preceding user-status turn was read-only. This continuation changes the
common producer, all three physical scan paths and the controller gate; the
new source builds on Tahoe. It is a WIP implementation checkpoint, not a claim
that the whole accepted-roam lifecycle or candidate-selection policy is fixed.
No new image was installed and no RF test was conducted in this continuation.

Admission now assigns a non-reused 64-bit serial under the selected-BSS leaf.
The scan callback receives that copied serial as an argument, rather than
inferring it from a possibly newer owner. The old census is cleared before the
new command can receive fresh candidates. Admission rechecks serial/epoch after
node-release callbacks and the lower return. A matched early physical terminal
can consume or advance the request before that return without being rearmed.
Serial exhaustion refuses admission instead of reusing a previous identity.

IWN retains the serial in its actual physical lease through both scan bands,
STOP_SCAN and the SAE scan-to-credential handoff. IWM/IWX retain it in command
policy and the physical terminal. Their sender-side policy validation checks
the admitted serial, and an already-scanning backend no longer returns false
success for a new explicit reassociation scan. The physical terminal supplies
the serial to common end_scan; stale and untagged predecessor censuses cannot
select for a newly admitted reassociation. There is another check after the
potentially yielding public SCAN_DONE event.

Completion claims and clears the active common owner under the leaf before
credential/SAE/MFP/WCL callbacks can reenter. Its detached result retains serial,
epoch, source/target BSSID and status. The conditional epoch helper checks the
retired serial and expected epoch inside the actual epoch-advancing leaf.
Controller dispatch copies that result across synchronous gate acquisition,
claims it once, and uses per-call wire storage instead of shared static words.
A successor admitted during cancellation is preserved and rejects the obsolete
publication. A nested retirement cannot advance the epoch or publish twice.
This does not create a fresh JoinAdapter generation for a roam request.

## Verification and exact build

- 20 executions of the complete common admission/abort/scan-completion claim,
  retirement and actual controller-gate functions pass ASan/UBSan on Linux and
  macOS. They cover the original two schedules, lower success/error after
  replacement, node-release replacement, delayed gate delivery, same-epoch
  successor, serial exhaustion and terminal-before-sender-return orderings.
  The physical firmware and epoch-cancellation scheduling are explicit fixture
  boundaries, not synthetic on-air evidence.
- The complete production epoch implementation is separately executed by the
  104-case roam-carrier suite, including six new conditional epoch and
  callback-replacement cases. It passes on both operating systems.
- The unchanged `72a042cf` failure helper independently compiles and still
  fails both required-behavior assertions, each exit 134. The expected-defect
  wrapper's zero exit is negative evidence, not a passing old implementation.
- Physical IWM/IWX admission: 74 groups; physical terminal/replay: 60 groups.
  New cases retain full-width roam identity despite replacement. The 25-group
  IWN receipt suite also verifies that identity across both bands and the
  detached terminal. Other firmware/IRQ effects remain fixture boundaries.
- The complete Linux payload aggregate passes. macOS additionally passes the
  new owner/epoch/physical suites and all 19 complete SAE failure-worker cases.
- Tahoe build succeeds; all 1085 external symbols resolve against the guest's
  BootKC, without a `thread_call_cancel_wait` dependency.

Source and guest mirror have identical content-manifest digest
`b1b71c8dc7d270199ef9d57de3bbad2f81abad7a52a4e63bcb6c204d781e4827`.
This digest is SHA-256 over the ordered `shasum -a 256` records for the
canonical tracked v2 production source roots, streamed unchanged to the guest;
it is not the guest's older `.git` HEAD. Build source ID is `b1b71c8dc7d2`.

Built UUID: `72E678CD-2AB3-3F64-BF6B-12790E86CCFC`.
Mach-O SHA-256:
`70bf1ac67f742501f6f1ba53d6994f42f77240cede13afe82456b6c4b5e7f1fb`.
Guest output: `/Volumes/AIAMBuild/itlwm-88c0d3f9/Build/Debug/Tahoe/AirportItlwm.kext`.

Logs:

- `/tmp/aiam-reassoc-owner-linux-20260911-r2.log`, SHA-256
  `28ca35e8452107c3b62279cec69a2dc0d9058fa3a162ed0f9e640b446e670267`.
- `/tmp/aiam-reassoc-owner-macos-20260911.log`, SHA-256
  `6c017139936367fa0dcaba29870ee846f0357604e6480ee16ea25100724bdd48`.
- `/tmp/aiam-reassoc-owner-baseline-20260911.log`, SHA-256
  `590754d6eff496b0c996dd7b07c2f10ec2a91ef3090226e45fb47e924c45d755`.

The separate source-only epoch contract had stale assertions for the already
checked AUTH return and for the old end_scan wrapper name. Those were aligned
with the existing actual function bodies. Its old assertion requiring owner
clearing *after* epoch callbacks enforced the reproduced defect; the new
atomic-claim assertion and executable regression replace that ordering.

## Required continuation, with no narrower completion claim

The accepted-roam identity is not yet carried through every physical lifetime:

1. Pass the expected serial into actual background-abort reservation, not only
   the common recheck after it returns. IWN also needs the final exact-owner
   validation at the physical doorbell, not only its reservation-time check.
2. Retain serial, source/target identity and epoch through deferred node switch,
   ordinary reset/replacement, state workers, AUTH/ASSOC/key observations and
   real lower/SAE retirement. Existing legacy callers can still acquire the
   current owner after an unrelated callback; they must receive their original
   identity instead. Clear the scan-accepted receipt on detach too.
3. Complete the reference's separate start/progress/preparation, actual AUTH /
   reassociation status and overall `0x50` completion producers and carriers.
   This checkpoint deliberately does not mislabel an SAE AUTH rejection as
   `0x49`. The old broad `0xcf` mapping and success's early local owner closure
   remain incomplete; protecting their identity is not wire-contract parity.
4. Exercise successful/failed/no-target/superseded roaming and real native
   next-candidate progression on the loaded final image, then DHCP, both
   traffic directions, GUI/security/S3/AP regression and qualified release.
   Do not count a selected helper, physical fixture or a build as this result.

The live lab remains boot `02DED0EA-E840-4DAE-BED2-1067761106C1` and the
previous qualified `97fe747c` image. The verified public asset remains the
same `c2dd6a08...` ZIP. Host 10.90.10.22, radio/VM state and unrelated QEMU
processes were not changed. User-owned local `Build/` remains untouched.
Local pool free space is about 1.2 GiB; recheck and use only a verified,
recoverable offline archive before a later RF/admission cycle needs more.
