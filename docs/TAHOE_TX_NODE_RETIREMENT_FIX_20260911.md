# Physical TX/node retirement correction — 2026-09-11

## Scope and implementation

This corrects the executed physical failures in
[the all-family investigation](TAHOE_REASSOC_TX_RETIREMENT_20260911.md),
not the complete accepted-roam lifecycle. The preceding user-status turn was
read-only. Production changes now cover IWN, IWM and IWX.

Completed mbufs retain their existing node reference in a temporary packet
list, without allocating under a TX leaf or adding a large kernel-stack
pointer array. The descriptor packet/node pointers and metadata detach first;
the actual reclaim loop finishes its counters/cursors before releasing node
references. IWX releases them after unlocking the TX leaf. IWN aggregate-stop
and failed-start unwind pass the batch through the hardware stop, release NIC
access and finish old BA/node updates before delivering the last release.
IWN single completion still releases the old reference before the independent
WNM callback, which can copy the next BSS and its reference counter.

Reset/free now release ordinary STA references too, not just SAE-marked ones.
Ring reset and counters precede release; orphan references with no remaining
packet are included. IWX partial allocation/invalid queue reset cannot return
before software references are retired. Free clears the old descriptor/command
aliases before callbacks can see already-freed DMA memory.

This does **not** make a zero node-reference count a whole-hardware drain
proof. SAE terminal reporting retains its existing position. Full source TX
admission closure, command/BA/reset ownership, immutable deferred attempt
identity and always-asynchronous main-workloop handoff remain required.

## Executed verification before build

- Complete Linux payload aggregate passes, including the newly enabled full
  physical-retirement fixture and adjacent primary-station/BA/TVQM suites.
- macOS ASan/UBSan executes the same complete retirement/reset/free methods:
  ordinary controls and all eight previously failing physical requirements
  pass. Additional orphan/partial reset/free cases check callback-visible
  state too. Minimal packet/DMA/lock/scheduler substitutions are explicit
  fixture boundaries, not hardware layouts or on-air qualification.
- Actual IWN node-copy/release/TX/reset/free execution passes ten positive
  scenarios. The three remaining deferred-BSS identity/liveness requirements
  still independently fail with exit 134 on both operating systems. The
  pre-copy one-reference case remains limited to that boundary, not a claim
  about established STA reference counts.
- Adjacent IWN queue topology, STA aggregate stop, AP stop and all three SAE
  transport contracts pass on macOS. Historical-source fixture signatures are
  retained through explicit baseline/current API selection.

Linux log `/tmp/aiam-tx-retire-linux-full-20260911-r2.log`, SHA-256
`f804f959e10e90fc9c8538b901c934c5e22cf25e144c4b269b66c53eeda82c44`.
macOS log `/tmp/aiam-tx-retire-macos-selected-20260911-r1.log`, SHA-256
`181894b4de15932caeb8f7ea01a34dea91746ce177d4b684e52f61ade7417d11`.

Production content digest:
`563246c2a3c2642f2094ff1403758b766e9d4ba597e55b10738b74655ba09598`.
At this checkpoint the correction is not built or loaded. The live lab still
runs `5aad4f68` / UUID `42D77FC7-BF18-3C78-A999-9276B718BF31`.
Public release remains the qualified `97fe747c` image. User host `.22` and
unrelated VMs are untouched.

## Required continuation

Build the matched source, activate in a fresh owned child with the working
parent preserved, then exercise ordinary STA queue retirement via on/off and
real sleep/wake, plus SAE recovery and open/WPA2 legacy BSS switching. Follow
with GUI/AP/security regressions before qualified publication. Continue the
full reference-owned roam start/preparation/AUTH/ASSOC/completion integration;
do not redefine it as only this packet-retirement correction.
