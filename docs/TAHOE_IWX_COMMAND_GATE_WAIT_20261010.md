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

## Exact committed build and loaded regression

Production `281f2d51c19c4a0db84010eb030b2e86e1b89c88` is committed, pushed
and independently matched to the remote branch. The complete payload aggregate
passes on Linux and on the exact committed macOS guest checkout. Kernel build
passes and all 1088 imports resolve against the running guest's
`/System/Library/KernelCollections/BootKernelExtensions.kc` on Tahoe 25C56.
The original dirty guest checkout and physical host 10.90.10.22 remain untouched.

An isolated AuxKC transaction preserves the four companion members and rollback,
returns zero and reaches READY. Independent full installed bundle equality and
the READY summary pass before one guarded guest reboot. The first postboot
observation returns SSH 255; the second has a banner timeout; the third verifies
the exact new loaded image and management. No installation or reboot is replayed.

- Boot session: `9A5DA19B-9BED-40E7-BE7D-07BEFEB5AD38`.
- Mach-O UUID: `C5D0499A-28E4-3725-869F-F63CBBE337D5`.
- Mach-O SHA256: `6d8452c868979898706a3f19f09809bd0cf75fd896a1a3e8c0fb0f6b491056c6`.

Real S3 is confirmed by two private monitor observations of paused suspended
state and serial ACPI SLEEP. Power history records 44 seconds of sleep,
WakeTime 1.331 seconds and the existing WindowServer 30 second notification
timeout. Private system_wakeup restores the same boot, exact loaded image,
full bundle equality and en2 management without VM restart or overlay change.

Four native postwake Off and On controls finish with exit zero and retain Off
readbacks and en2. The completed FBT trace has both its COMPLETE marker and
normal remote DTrace and SSH exit zero. Initial IWM disable succeeds in
669.220 milliseconds; two queue stops return in 11.261 and 15.939 microseconds.
Postwake native On returns `0xe00002d8` in 1.836, 1.892, 1.874 and 1.909
milliseconds. The utility process exit zero is not radio success. No actual IWX
sender or stop entry occurs on this IWM device; the regression does not qualify
the changed IWX firmware path or successful IWM on air association.

## Additional laboratory release

The archive contains the exact installed and loaded bundle without a rebuild.
Build, installed and extracted bundles compare equal. SCP completes with zero
before independent local size and SHA256 checks and upload.

- Asset: [AirportItlwm-Tahoe-Iwx-CommandGate-281f2d51.kext.zip](https://github.com/rdmitry0911/itlwm/releases/download/v2.4.0-alpha/AirportItlwm-Tahoe-Iwx-CommandGate-281f2d51.kext.zip).
- Asset ID: `627703422`; size: 15,716,748 bytes.
- ZIP SHA256: `1922eddf75555373de5fd7c7bf2c259d0ca5265e91962d907a4c69dacb38a07a`.

A fresh final API read verifies all twelve older assets and immutable metadata
unchanged, complete previous notes as an exact suffix, and the default asset
unchanged. This additional unsigned Debug LAB asset is explicitly labelled with
the IWM S3 regression scope and lack of current IWX on air qualification.
