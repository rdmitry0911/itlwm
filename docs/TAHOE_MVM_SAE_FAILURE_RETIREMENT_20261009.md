# IWM/IWX failed SAE join retirement — 20261009

The production correction connects a real SAE peer rejection or local peer
protocol abort to the accepted fresh JoinAdapter attempt. It also enrolls
IWM/IWX in the existing NO_NETWORKS retirement path. This is a software-path
correction, not an on-air WPA3, GUI recovery, sleep/wake or AP qualification.
The laboratory 9260 still reports hardware RFKILL before radio initialization.

## Reference and ownership

The 25C56 reference `AppleBCMWLANJoinAdapter::handleAuth` preserves the
actual nonzero peer status in the 28-byte AUTH completion. Its
`sendConnectComplete` publishes the separate 164-byte join completion once
for the active, not-yet-completed attempt. These decompiles were read on
`10.7.6.112` in
`~/Projects/ghidra_output/aiam_roam_policy_25C56_20260910.Pthh3J/kernel-join-candidate-fsm-v2/`.
The existing [failure contract](TAHOE_REASSOC_FAILURE_LIFECYCLE_20260911.md)
and [candidate ownership](TAHOE_WCL_JOIN_FAILURE_CANDIDATES_20260910.md)
remain the design source; accepted roaming does not borrow a fresh-join ID.

AUTH admission copies the actual accepted generation matching BSSID, SSID,
epoch and AUTH phase. The peer worker claims only that generation and retains
the received status (or local EPROTO for a protocol abort). Three distinct
participants retire it:

- PRODUCER: after the worker scrubs its copied peer/PMK/terminal values, or
  after the exact final scan notification has retired its physical scan lease.
- LOWER: after the ordinary asynchronous station/BA/command cleanup and
  main-gate current-BSS retirement. This edge does not recursively start a scan.
- SAE: after cancellation, engine destruction and actual native descriptor
  and queued TX-event retirement. A late TX terminal wakes the waiting worker.

No synthetic generation or `cleanup_done(ALL)` is introduced. Current hardware,
join and association identities fence queued work; a same-BSS replacement or
cancellation cannot receive the old result. Watchdog/public SCAN is refused
before epoch mutation while a failed attempt is retiring, and raw backend
SCAN cannot replace its cleanup lease. Explicit INIT remains available.
Stop/detach discard obsolete retirement wait tokens without publishing a
false completion. With crypto unavailable, transport retirement waits for a
real TX_DONE/reset rather than self-requeueing continuously.

## Executed verification

The restored baseline at `85d73672` compiled on both Linux and macOS but
failed the required recovery assertions in both MVM families: accepted
attempt left in AUTH, generic scan started, no owned cleanup or failure.
That was the baseline, not a passing driver test.

Current fixtures execute complete extracted production AUTH admission and
SAE worker bodies, production queued-state worker/commit code and production
scan terminal handlers under ASan/UBSan. Hardware firmware operations,
crypto outcomes and IOKit scheduling are explicit fixture boundaries.

- IWM: 27 SAE peer failure/retirement scenarios.
- IWX: 27 SAE peer failure/retirement scenarios.
- IWN shared regression: 21 scenarios; production IWN is unchanged.
- Queued-state existing coverage: 254 groups; failed-join lower cleanup: 24.
- Physical scan terminal/replay: 68 groups, including NO_NETWORKS and
  replacement during upper or cleanup callbacks.
- Full `scripts/test_payload_builders.sh` aggregate is a required gate.

The MVM test can substitute only the historical complete worker through
`MVM_SAE_WORKER_NEGATIVE_REF=85d73672`; AUTH admission and retirement helpers
remain actual current code. This negative control must fail a behavioral
assertion, not merely fail compilation.

The first macOS build succeeded against the running 25C56 BootKC with all
1088 imports resolved and no `_thread_call_cancel_wait`. It was a pre-commit
diagnostic build: the build helper hashes the committed source tree, so its
embedded identity still named the old HEAD. It is not the deployment or
release candidate. A committed-source rebuild and loaded UUID/hash check
are required before runtime claims.

## Committed-image build and runtime checkpoint

Production commit `d2d8f1d8246c42a5d23a5e8286bacb4551e14510` was pushed to
`origin/tahoe-iwn-sae-bridge-runtime`. The isolated guest worktree was aligned
with that commit without changing the preexisting dirty main worktree.
MacOS reran both 27-case MVM tests, the 254+24 queued-state groups and all
68 scan-terminal groups, then built against its actual 25C56 BootKC.

- Committed source identity: `92be77103720`.
- Built, installed and loaded UUID: `1ABD7B76-3780-3690-A8AC-370A4A156866`.
- Mach-O SHA256: `26e1ac4bd91020ecbde9f57e47ad5cf596b8cd3baf50df09d81fc7a95cf2da25`.
- All 1088 imports resolve; no `_thread_call_cancel_wait`.
- Private AuxKC admission PASS; canonical hashes unchanged by preflight.
- Transactional activation preserved all four companion members and retained
  rollback copies of the previous canonical bundle and AuxKC.
- New boot UUID: `7E0F0479-E47A-47A1-A360-7A8C3E87E940`; SSH returned within
  the first bounded 55-second observation. No kext unload was attempted.
- Independent management remained on `en2`, `10.0.6.15`, default `10.0.6.2`.

The 9260 still logs `iwm_init_task SKIP iwm_init: fatal=2 IFF_UP=1 IFF_RUNNING=0`.
Boot/load success does not execute the new on-air failure path.

A repeated native `networksetup` power test is **FAIL**, not an off/on pass.
The first Off/On reads correctly. The second and third Off requests continue
to read On through six one-second polls each; the boot UUID and management
remain unchanged. Serial records the actual POWER setter entered
(`handler=1`) and returned `0xe00002d6`; this is not an omitted IOC dispatch.
No GUI result is inferred from these native controls.

Follow-up passive FBT on the exact loaded image narrows that observation:
the controller GET initially returns Off; SET On enters
`handlePowerStateChangeCore` and waits 15.005 seconds before returning
`0xe00002d6` (the configured 15000 ms lower-ready timeout). During the wait,
GET returns the tentative On state. The next requested Off does not enter
the traced setter during that pending call. After its timeout and rollback,
native readback returns Off, and a standalone Off remains Off. Thus this is
not evidence of a permanently stuck radio-off state. The short repeated
matrix fails while failed activation is pending; the next priority is prompt
real lower-init failure/cancellation propagation, not bypassing RFKILL or
fabricating a lower-ready success. Read the raw
`power-control-trace.log` and `power-control-trace-commands.log` together.

Evidence root:
`/home/dima/Projects/aiam/scratch/iwm-9260-runtime-20261009.mo5CXe/`.
Relevant logs are `sae-retirement-linux-final.log`, both
`sae-retirement-negative-{iwm,iwx}.log`, `sae-retirement-macos-committed.log`,
`sae-retirement-preflight.log`, `sae-retirement-activation.log`,
`sae-retirement-loaded.log` and `sae-retirement-power-stable.log`.

## Remaining scope

No on-air test can currently reach scan/auth on the assigned IWM 9260 because
both Linux and macOS observe hardware RFKILL. Do not bypass that guard.
Timeout/retry-exhaustion, local submission/command failure and reset-failure
terminal producers remain separate audit targets; these are not all closed
by preserving an explicit peer rejection. The repeated GUI matrix remains
the first radio-enabled qualification task: open/WPA2/WPA3 and saved-network
changes, failed-to-valid recovery without toggling, off/on, then real sleep
and wake, DHCP and bidirectional traffic. AP and IWX require their own runtime
qualification. Host `10.90.10.22` was not changed or rebooted.
