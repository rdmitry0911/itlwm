# Deferred BSS origin identity — 2026-09-11

## Implemented checkpoint; full roaming remains open

The preceding user-status turn was read-only. This continuation changes the
common path used by IWN/IWM/IWX, following the exact 25C56 Core/WCL lifecycle
recovered in `TAHOE_REASSOC_FAILURE_LIFECYCLE_20260911.md`. The reference keeps
an admitted source/target operation across preparation and actual terminal
events. Its internal implementation is not our node-reference scheme.

Both explicit WCL and legacy same-ESS background switches now capture source
and target MAC/BSSID, original association epoch, continuation epoch, fresh
join sequence, reassociation census sequence and admitted reassociation serial
before allocation or AMPDU/management callbacks. No deferred callback borrows
the current owner's serial. Captured identity is checked again after callbacks
and before arming or acting on the selected cache entry.

Source-leave epoch advancement tests that same identity inside the actual
selected-BSS leaf which advances the epoch. It returns its captured result,
not a new owner's epoch observed after revocation callbacks. The fresh-join
sequence detects replacements even when BSSID and association epoch coincide.
The serial-zero legacy path cannot acquire a subsequently admitted WCL roam.

The data gate is set before stopping AMPDU and submitting DEAUTH. Failure
before accepted source leave preserves the source epoch/keys and does not
request SCAN. A later missing-target failure retires only its captured serial.
The failure helper returns the retired epoch by value; after public failure
delivery, the old continuation must still match its sequences, epoch and
source before requesting SCAN. A reentrant successor's state is left alone.

This is a reentrancy/identity correction, **not** an always-asynchronous
main-workloop handoff or full physical source drain. The final check-to-action
boundaries still need that serialization. The generic selected-node join has
its own further callback/epoch transitions; full immutable ownership must be
carried through those, state workers, AUTH/ASSOC/key results and overall roam
completion. Pure-SAE's separate retarget path is not relabeled as this legacy
switch and its generation-zero failure path remains open.

## Executed verification before build/load

- Complete Linux payload aggregate: terminal exit 0, including adjacent
  physical TX retirement, station/BA and IWX TVQM suites.
- Linux and macOS ASan/UBSan execute the complete common capture/current,
  deferred producer, failure and switch methods together with the existing
  actual node-copy/ref/release and IWN TX/reset/free methods: 25 positive cases.
  The two former obsolete-owner/failure-reentry requirements now pass.
- Complete actual epoch implementation plus actual identity predicate:
  118 carrier/epoch cases pass on both operating systems. Fourteen additions
  include same-peer fresh-join replacement, serial/census/epoch/source/target
  mismatch, absent lock, legacy admission, and replacement during revocation.
- Actual reassociation admission/abort/retirement/controller-gate suite:
  22 cases pass on both systems. macOS also repeats full physical IWM/IWX
  TX/reset/free extraction and the adjacent common contracts successfully.

Hardware notification scheduling, inner node join, allocation and management
submission are explicit boundaries in the deferred fixture. The epoch fixture
executes the real guarded epoch implementation separately. These are not RF
claims. No IWM/IWX hardware qualification follows from this IWN laboratory.

The full deferred gate deliberately retains two red requirements (exit 134):

1. Historical pre-copy one-reference liveness, restricted to that boundary.
2. **New established-source terminal-before-arm reproduction.** Actual
   node_copy leaves zero references. The explicit management-submission
   boundary completes the frame using the real ref/release functions before
   returning to the complete producer. That producer later installs its
   callback at zero references: no join occurs. This is not fixed by changing
   a reference-count constant or by immutable identity alone.

`BSS_SWITCH_EXPECT_DEFECTS=1` verifies these failures; its wrapper exit zero
does not mean complete roaming passes. The full aggregate includes only the
positive subgroup, with this red gate separate and explicitly reported.

Evidence:

- `/tmp/aiam-bss-identity-linux-full-20260911-r2.log`, SHA-256
  `442f22879f722c572cb145ff6179e376849501b4e2b9838d8148e109bb93a002`.
- `/tmp/aiam-bss-identity-macos-selected-20260911-r2.log`, SHA-256
  `cbf0ec9a49b890b7a117082534a1bcb18865c8eb994f9290f5b0687049a656f3`.
- `/tmp/aiam-bss-identity-linux-red-audit-20260911.log`, SHA-256
  `1802550c91dcf72a27274487e2c9fcf7adc7288f20271b78547cfd119ae1fd06`.
- Production content-manifest digest:
  `cd1422640631f3ec06a9e897e9943a1d457925bcaf81db75731957493ad78178`.

At this checkpoint the source is not built/loaded. The lab still runs
`1896beb2`, boot `F31CDD13-6EBC-470A-A76E-9E231395AB84`, image UUID
`46BA6759-1E6A-35B3-A9B8-A90C66B09A69`. Public release remains `97fe747c`.
Build, recoverable activation and actual runtime are the next steps, not
optional substitutions for the outstanding end-to-end gates. Physical `.22`,
unrelated QEMU instances and the user-owned local `Build/` are untouched.

## Updated reference tooling supplied by the user

Read-only inspection of `10.7.6.112:/home/dima/Projects/ghidra` confirms clean
branch `feature/bounded-function-parallel-decompiler`, HEAD `5995e24caa`, with
the four supplied commits `69d6405859`, `eb9a8c5ff9`, `27fed11060`,
`5995e24caa`. They cover block-structure recovery, exact jump-table bounds,
partial-symbol alias recovery and artificial-PHI storage respectively.

The checkout's `src/decompile/cpp/decomp_opt` is present but dated May 18;
the usual `os/linux_x86_64/decompile` path is absent. Neither is attested as
the updated build. Before using these fixes on a disputed reference function,
build/identify an exact-HEAD executable in a separate scratch directory and
record its hash. Heavy decompilation must use **40 actual cores/interfaces**.
The user's agent's successful scratch build/replays are reported evidence,
not a claim that this session repeated them or installed a new binary.
