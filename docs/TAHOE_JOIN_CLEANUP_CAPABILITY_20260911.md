# No-candidate cleanup enrollment — 2026-09-11

## Production correction

IWM/IWX now carry the exact fresh-join generation through physical scan
admission and terminal handling. Both advertise `IEEE80211_C_SCANALLBAND`.
Their `endscan` therefore reaches common `ieee80211_end_scan_owned` with a
nonzero generation, including a real empty candidate census. However neither
HAL yet installs `ic_wcl_join_failure_scan` or acknowledges the complete
PRODUCER/LOWER/SAE failed-join retirement sequence. IWN does.

The common NO_NETWORKS producer previously entered FAILING on all three
families, then made `end_scan_owned` return before its existing next-scan
path. For an unenrolled backend no participant could finish that attempt.
This is a source-connected regression with an executable negative control,
not an assertion that an IWM/IWX radio reproduced it in this laboratory.

`ieee80211_wcl_join_scan_failed` now requires the backend cleanup callback
before changing the ledger. Without enrollment it returns false, leaves the
same DISCOVERY attempt intact and permits the existing next-scan path and a
later actual candidate. It does not acknowledge nonexistent cleanup, invent
an upper success/failure event or discard scan-generation ownership. IWN
continues to wait for its real three-part retirement instead of rescanning.

The callback's presence is a lifecycle enrollment prerequisite, not a claim
that the callback has completed. Physical receipt identity alone is not such
an enrollment. Full IWM/IWX fresh-AUTH SAE failure handling remains next:
capture the accepted join at engine admission, claim only its real peer
result, retain the generation through asynchronous lower cleanup, drain the
actual engine/transport and acknowledge each real participant. The separate
generation-zero accepted-roam AUTH observation/completion must not borrow a
fresh JoinAdapter generation.

Reference contract remains the exact 25C56 JoinAdapter and Core/WCL lifecycle
in `TAHOE_WCL_JOIN_FAILURE_CANDIDATES_20260910.md` and
`TAHOE_REASSOC_FAILURE_LIFECYCLE_20260911.md`; the latter was re-exported
with the updated Ghidra `5995e24caa` native/JumpTable tooling and 40 actual
interfaces. This patch is a safe compatibility correction until the missing
backend completion ownership is implemented, not full reference parity.

## Executed tests

- The complete production join bridge, under ASan/UBSan, fails on previous
  source `9a830ac6`: with no cleanup callback, `scan_failed` returns true
  instead of preserving DISCOVERY (exit 134). This also reproduces with a
  callback removed after physical-generation admission but before terminal.
- Corrected complete helpers pass. The **actual extracted no-candidate
  branch** additionally executes its next-scan call for unenrolled backends,
  preserves the same ledger and permits subsequent AUTH binding. The enrolled
  control enters FAILING without an autonomous replacement scan. Candidate
  discovery and actual physical rescan are explicit fixture boundaries; this
  is not the whole `end_scan` function or an RF test.
- Linux full payload aggregate: exit 0, including adjacent three-family
  physical TX/reset, primary station/BA, scan and join suites.
- macOS exact bridge/branch, IWN SAE worker failure and all 22 shared
  management-queue ownership scenarios: exit 0 under ASan/UBSan.

Evidence SHA-256:

- Previous-source negative log
  `/tmp/aiam-join-cleanup-capability-negative-20260911.log`:
  `582776d71b5ebe3fa283fddf5fc01bd98690e9c022ba58150c8f40ed2d5e150c`.
- Linux aggregate `/tmp/aiam-join-cleanup-capability-linux-full-20260911.log`:
  `82afb717ac3b4d89056af832e7efb7bfd99018cfae28f4f3ac05a57449047e5b`.
- macOS selected `/tmp/aiam-join-cleanup-capability-macos-selected-20260911.log`:
  `7eb892e31dfcf018617c0a1f825cbba450c9c4b5d3a9fb1ee467db96812dbfae`.
- Full production manifest
  `/tmp/aiam-join-cleanup-capability-production-20260911.sha256`:
  `61462712c76c1614681a01eeb7349d603751149e73f353a53bda7824e74256bd`.

## Build/runtime checkpoint

At this source checkpoint the owned lab still runs `20dd3d8a`, boot
`C7693AA2-C5CD-4515-B7AA-508BAB9344F6`, loaded UUID
`4B2B526E-9D1D-3C51-9509-0C11C7BE30E9`. The new guard is not yet loaded.
The exact guest source mirror received only the changed production file and
test files after its prior production hash was verified. Its old Git HEAD
is not used as build identity; the manifest above is the build input gate.
Build, recoverable guest activation and on-air IWN regression follow.

No new IWM/IWX on-air qualification, all-mode UI/sleep/AP completion or public
release is claimed. Physical host `.22`, unrelated QEMU, base disks and the
user's local `Build/` were not changed. The original autonomous goal remains
active; this bounded guard does not replace the missing full cleanup layer.
