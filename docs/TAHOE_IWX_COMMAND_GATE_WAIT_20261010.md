# IWX command waits release the recursive controller gate

IWX synchronous firmware commands now release the entire recursive main
workloop gate while waiting for an actual retained ACK. This repairs a command
timeout when the same workloop cannot deliver the IRQ. Queue stop also releases
that gate while draining senders, and every repeated stopper waits for the
same lifetime boundary before response or descriptor reclamation. Association,
activation and recovery use this command path; successful radio joins remain
separate hardware qualification.

## Reproduced command and stop failures

The complete sender and ACK bodies from production baseline `eb91aee6` return
ETIMEDOUT under a controller gate held twice. The same workloop IRQ scheduling
double cannot enter, so queue ownership and DMA remain live. Linux reports
110 and macOS reports 60; inline and DMA commands compile and fail the intended
result assertion with exit 134. The off gate control executes the actual ACK
body and succeeds on both systems.

Replacing only the complete stop body with that historical baseline reproduces
two further failures with the current full sender. Repeated stop returns before
the actual sender leaves. A gated stopper can also hold the controller gate
while a gated sender needs it to return, preventing the sender from finishing.
Both historical controls compile and fail their intended assertions with exit
134 on Linux and macOS. No sender reference is decremented by a test substitute.

## Command completion and deadline

The waiter pairs the wait mutex with q0 state inspection. A gated caller drops
the wait mutex before public `IOCommandGate::commandSleep`, then reacquires it
only after the recursive controller gate has been restored. The IRQ and stop
publish retained terminal slot state before ordinary and command gate wakeup.

Success requires the exact serial and epoch in COMPLETED state. An early ACK,
or actual completion racing the timeout, transfers the response and frees the
slot. A wake alone is not completion. Spurious wakes recheck the predicate
against one absolute one second deadline rather than starting another timeout.
Gate wait slices are capped at ten milliseconds and at that command deadline;
a missed gate wake never authorizes completion or descriptor reclamation.
Timeout retains DMA and response until actual late ACK or device reset.

The public API and recursive lock behavior follow Apple's
[IOCommandGate implementation](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/iokit/Kernel/IOCommandGate.cpp)
and [IOWorkLoop implementation](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/iokit/Kernel/IOWorkLoop.cpp).
This is an IOKit scheduling fix, not a guessed private BCM firmware algorithm.
The exact reference lower TX contract still requires actual device completion
and reset fencing, as recorded in
`docs/reference/AppleBCMWLAN_IWM_SAE_TX_COMPLETION_25C56_20260801.md`.

## Stop lifetime and reopening

Each stop owns a q0 stopper reference through sender drain and response cleanup.
The first stopper closes admission and aborts active slots; subsequent stoppers
do not inspect already reclaimed ring storage but still drain actual senders.
Gate aware drain releases full recursion, uses public command sleep and rechecks
the count. Sender leave wakes that drain outside q0. Reopening rejects any live
stopper, including the interval after sender count reaches zero but response
cleanup remains active. Final destruction requires both counts to be zero.

## Software qualification

The fixture extracts the complete production sender, ACK, queue admission,
leave, stop and start predicate. It uses ASan and UBSan on Linux and macOS.

- Three ordinary and gated ACK controls cover inline and DMA commands.
- Twenty six full sender and ACK scenarios cover early ACK, response ownership,
  spurious wake, absolute deadline, timeout racing ACK, late ACK, invalid queue
  and index, stale generation and asynchronous commands without an MFP owner.
- Sixteen threaded stop scenarios pause a full sender during preparation or
  after actual stop wake; ordinary and gated stoppers, including repeated stop,
  cannot reclaim storage or return before the sender leaves.
- Eight threaded scenarios exercise a full gated sender returning through its
  recursive controller gate while stop drains it.
- Four threaded cleanup scenarios retain actual stop ownership after sender
  exit and reject reopening until response cleanup completes.

IRQ delivery, DMA, firmware and kernel gate primitives remain explicit doubles.
Threaded controls use actual host mutexes, condition variables and full sender
and stop bodies. The existing 62 sender scenarios and lifecycle fixtures retain
their narrower adapters; MFP firmware response classification is not synthesized
by the new controls.

The static q0 contract reaches an existing unrelated enqueue count failure:
it expects three direct task_add call sites, while `eb91aee6` and this candidate
both contain seven. New command and stop assertions precede that failure. This
is not recast as a passing complete static contract.

## Laboratory qualification boundary

The current passthrough device is IWM 9260, not IWX, and its hardware RFKILL
prevents on air association. A whole kext build, load and IWM sleep regression
cannot qualify the changed IWX firmware command path on a physical IWX device.
The native repeated open, WPA2, WPA3, saved network, DHCP, traffic and AP matrix
therefore remains open for IWX hardware qualification.

Evidence is retained under
`scratch/iwm-9260-runtime-20261009.mo5CXe` in the aiam workspace with the
`iwx-gate-*` prefix.
