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

Source `2dc501b2` built with all 1085 external symbols resolved. Private
five-member AuxKC admission and transactional activation passed, preserving
the four companion members. The 00:49:49 UTC boot loaded UUID
`AE5962B6-6A4F-365A-AD7F-49961365F43A`, matching frozen and installed
Mach-O SHA-256
`282924e98a40a4e643983687fb79b7d818ddc5c9f3a5acca81a0412065fa2e99`.
The live owner observer uses fresh object DWARF from this exact build.

Native WPA3, WPA2 and open sharing completed sequentially on this boot.
Each external join obtained DHCP, passed 20/20 client-to-gateway packets,
isolated cold-neighbor 10/10 reverse packets and routed HTTP. A subsequent
three-round public-ioctl sequence submitted an old WPA2 start, stop and WPA3
replacement with 10, 100 and 300 ms caller spacing. All callers completed;
the final owner reached Terminal with AP-up, stop and replay flags clear.
The observed lower calls were sequential: these are real repeated-lifecycle
controls, not proof that the original yielding-call interleaving ran on air.

Concurrent WPA3 STA and role-7 SAE/required-PMF AP passed 20/20 client-to-AP,
isolated cold-neighbor 10/10 and primary 5/5. A newly built public Apple80211
probe supplied the existing role/channel/HostAP request sequence; it did not
call private lower-HAL entry points or inject firmware state.

The temporary USB upstream was removed while awake; no emulated Ethernet
device remained during the 01:03:06 UTC sleep request. Actual S3 was confirmed
by serial `ACPI SLEEP` and the owned VM's suspended state. Wake at 01:04:11
produced `ACPI S3 WAKE`, retaining the boot epoch and loaded UUID. The primary
address and AP were restored without another AP-start command. Fresh USB
management was attached only after wake and passed an independent HTTP check.
Explicit external-client reselection completed SAE with required PMF/BIP;
the recovered AP and primary passed the same 20/20, cold 10/10 and 5/5 checks.
This demonstrates service recovery, not automatic client continuity.

Normal role-7 stop retained primary traffic at 10/10. After removing only
the temporary AP address, native WPA3, WPA2 and open sharing completed in
sequence on the same post-S3 boot. Each used the normal system producer and
obtained real DHCP; each passed 20/20 client-to-gateway, isolated bridge-scoped
cold-neighbor 10/10 and routed HTTP. Client power save remained enabled.
There was no reboot, manual bridge-member rewrite, daemon restart or radio
toggle between these modes.

The frozen ZIP SHA-256 is
`08ed6022d21b430c987d0a39fd6930a4189c39749458e90aca2f19a8ef6e070a`;
its extracted Mach-O matches the loaded image above. These are IWN/6235
runtime results, not equivalent recent IWM/IWX hardware qualification, a
complete GUI/profile matrix or lossless roaming. Publication is still a
separate step at this checkpoint.

## Final console audit: release held on AP TX teardown

Both bounded owner observers completed without diagnostic errors. The final
normal sharing stop left Terminal with AP-up, lower-stop and replay flags
clear; the retired bridge reached I/O count zero and primary traffic passed
10/10. However, the full serial audit found a separate watchdog timeout after
WPA2 PAN teardown, both before and after S3. The post-S3 instance follows the
04:09:14 local sharing stop: PAN stop reports its terminal, then AP aggregate
queue 12 still owns three descriptors (`cur=41`, `read=38`). The management
queue named by the broad diagnostic prefix has zero queued descriptors.

The production watchdog emits these records only after the pending-TX timer
expires, then stops and reinitializes the device. Therefore the passing
short DHCP/traffic checks do not prove reset-free AP mode switching. The
CCA and upper-owner corrections are not established as the cause, and the
next boundary is real AP aggregate retirement before lower stop completion.
The prepared archive has not been uploaded; public release remains `893a3114`.
