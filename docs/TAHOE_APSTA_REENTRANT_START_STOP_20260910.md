# AP start/stop across a yielding lower call — 2026-09-10

## Live failure and successful controls

The IWN/6235 guest loaded `cf4b50ad`, UUID
`A476957E-AC9D-3712-B32E-F6C2BB870F81`. Native WPA3 and WPA2 sharing
passed DHCP, bidirectional traffic, cold-neighbor delivery and routed HTTP.
The following open start was not discoverable over the air. Matching-build
DWARF observation found lifecycle Terminal with AP-up still set, no pending
stop or replay, and no watchdog recovery. The primary STA recovered normally.
The contradictory AP/primary OP_MODE answers then made airportd reject the
standard stop before its driver call. Wi-Fi off/on did not repair that state.

A same-image reboot restored the owner. A ten-minute, read-only trace ending
at 00:27:37 UTC completed without diagnostic errors. The repeated native
WPA3, WPA2 and open sequence each passed real DHCP, 20/20 forward packets,
isolated cold-neighbor 10/10 reverse packets and routed HTTP. Another open
start after ordinary Wi-Fi off/on passed the same checks. These successful
controls do not erase the first failure: their lower stop/start calls were
sequential and did not reproduce its contradictory state. A separate attempt
to enable the sharing preference while Wi-Fi remained off never reached a
new driver HostAP call and did not create the bridge; it is not a lower-start
failure or a passed GUI test. Normal sharing disable and radio-on restored
the lab's ordinary STA configuration afterwards.

## Production control-flow counterexample

`ItlIwn::iwn_quiesce_scan_for_ap_transition()` uses `commandSleep()` while
waiting for the physical scan-abort terminal. That releases the command gate:
another public request or the watchdog can enter the AP owner before the
original HAL call returns. Upper workloop gating alone does not serialize
these complete transactions.

The regression compiles the complete production start, stop, lower-terminal,
reset, public HostAP setter and watchdog-replay methods. External HAL I/O,
packet construction and unrelated link reconciliation are substituted; this
is a control-flow test, not an on-air or beacon-format test. With unchanged
`f409abc1` methods, a stop admitted during the old start's HAL call first
clears AP-up and leaves teardown pending. The old start then returns success
and republishes AP-up/datapath. The subsequent successful stop terminal
leaves Terminal + AP-up + enabled datapath. This exactly reproduces the
observed invariant violation in production methods, but is not a captured
thread interleaving proving that this was the first live failure's cause.

## Correction

- A scoped lower-call owner prevents reentrant HAL AP start/stop calls while
  an earlier call has yielded its command gate. It does not hold a new lock
  over firmware completion or make the asynchronous IWX worker synchronous.
- Accepted public intents and radio resets advance a nonzero generation.
  Superseded start and caller tails cannot publish success or clear a newer
  pending request. A NULL request is admitted even before the first AP-up
  publication when a lower start is still in flight.
- A replacement profile received during that interval remains confirmed
  behind the old context's real stop terminal. The watchdog then starts it;
  the fix does not merely hide stale carrier or discard the replacement.
- SSID and credential pointers passed to the HAL refer to bounded snapshots,
  so replacing the durable profile during a gate sleep cannot combine the old
  beacon/security configuration with a successor's SSID or credential. The
  credential snapshot is cleared immediately after the HAL returns.

The reference's separately recovered AP success publication and reset/AP-up
contracts remain unchanged. The exact 25C56 raw success range calls
`reportLinkStatus(3, 0x80)` before enabling the interface. Available full
HostAP C exports contain bad-instruction/truncated paths; they are not used
to infer a complete Broadcom stop interleaving. The reentrancy mechanism and
generation fence here are Intel-specific lifetime ownership, not a claim
that Apple's private implementation uses these new fields.

## Verification and release boundary

ASan/UBSan regression passes cancellation before AP-up, cancellation during
datapath publication, immediate replacement during start and stop, immutable
borrowed profile data, asynchronous pending/error returns, watchdog successor
replay, unexpected lower-reset recovery, rejected input and generation wrap.
The pre-fix source independently compiles and fails the AP-up/datapath
invariant. The full payload/scan/LQM/PMF suite and existing APSTA carrier,
epoch, channel, sleep, IWM/IWX retry and stop-terminal contract checks pass.

The changed AP owner has not yet been built or loaded. Its new private fields
require fresh matching-build DWARF before any field-based live observer is
used. Native mode-switch, concurrent STA/AP, true S3 recovery and on-air
DHCP/traffic qualification remain mandatory. The public artifact remains
the qualified `893a3114` image; neither this fix nor `cf4b50ad` is promoted.
