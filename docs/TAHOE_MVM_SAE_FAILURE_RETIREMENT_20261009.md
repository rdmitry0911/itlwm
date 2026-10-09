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
