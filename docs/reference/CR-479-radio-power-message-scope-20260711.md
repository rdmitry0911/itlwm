# CR-479: radio power message scope

Date: 2026-07-11
Reference: Tahoe 25C56 guest BootKC
Ghidra project: `wifi_analysis_25C56/BootKC_guest_25C56.kc`

## Reference chain

The exact current entries are:

| Function | Entry | Observable role |
| --- | --- | --- |
| `AppleBCMWLANCore::handlePowerStateChange` | `0xffffff800157af02` | selects radio state and calls power off/on |
| `AppleBCMWLANCore::powerOff(bool)` | `0xffffff80015dd6ac` | publishes unavailable `0x37`, then tears down radio owners |
| `AppleBCMWLANCore::powerOn()` | `0xffffff80015df6ca` | restores radio owners, then publishes available `0x37` |
| `AppleBCMWLANCore::setPowerState` | `0xffffff80015ec10c` | synchronously enters the controller command gate |
| `AppleBCMWLANCore::setPowerStateGated` | `0xffffff80015ec346` | owns the independent system-power state bit |
| `AppleBCMWLANCore::powerOffSystem()` | `0xffffff80015ec6f0` | calls `powerOff(true)`, then quiesces without selector `1` |
| `AppleBCMWLANCore::powerOnSystem()` | `0xffffff80015ecb08` | calls `powerOn()`, then publishes selector `1` once |

`handlePowerStateChange` has no call to
`IO80211Controller::postMessage` and no selector `1`. The direct posts in
`powerOff` and `powerOn` use selector `0x37`, length `0xf8`, and the distinct
normal power carriers recorded in the driver-availability reference note.

`APPLE80211_M_POWER_CHANGED` is a system sleep/wake event, but it is not
symmetric in the current reference. `setPowerState` obtains the controller
command gate and synchronously calls `setPowerStateGated`. The gated action
owns bit `0` in a shared 32-bit word, applies the recovered
`(word & 0x30) == 0x20` transition gate, removes `IO80211WokeSystem` before a
real OFF, and uses atomic preserved-bit updates before the system helper.
There are no power `thread_call`s, deferred five-second acknowledgement, or
manual `acknowledgeSetPowerState()` calls. The complete word-owner map and the
conditional IOReporting branch are recorded in
`CR-480-system-pm-state-word-20260711.md`.

`powerOffSystem` calls `powerOff(true)`, therefore emitting the normal
unavailable `0x37`, and has no direct selector `1` post. `powerOnSystem` calls
`powerOn()`, therefore emitting the normal available `0x37`, and then posts
selector `1`, NULL/0, async 1 at `0xffffff80015ecc65`-`0xffffff80015ecc78`.

## Consumer proof

The baseline radio off/on sequence panicked at uptime 795 seconds:

```text
WCLNetManager::ipStatusInWaitForIp
WCLNetManager::systemAwakeNotificationHandler
WCLSystemStateManager::handleReplayNotification
WCLSystemStateManager::driverAvailableEventHandler
```

The first correction removed selector `1` from the radio handler. FBT then
proved that a radio-off transition emitted only the unavailable `0x37`, with
no simultaneous `setPowerState` or system-power handler invocation. The next
radio-on nevertheless reproduced the same panic. That result disproved the
earlier claim that removal from the radio handler alone closed the mechanism.

An additional confirmed producer mismatch exists in the independent IOPM
lifecycle:

- radio power-off `DRIVER_UNAVAILABLE` puts SSM into deferred state;
- radio power-on `DRIVER_AVAILABLE` invokes `handleReplayNotification`;
- replay routes system WAKE as NetManager event `7`;
- WAKE is invalid in the `WAITING_FOR_IP` state and reaches the fatal
  `unhandledEvent` transition.
- the local IOPM owner initialized its private state to ON, scheduled system
  OFF/ON asynchronously, acknowledged them manually, and published selector
  `1` on both edges, while the reference owns a zero-initialized synchronous
  state and publishes selector `1` only after system ON.

This does not prove that IOPM is the sole producer of the replayed state. The
alignment is justified at `CONFIRMED_DEVIATION` scope: the non-reference radio
producer remains removed, and the genuine IOPM producer is matched to the
reference command-gate state machine and asymmetric selector scope without a
delay or replay guard.

## Artifacts

- `~/Projects/ghidra_output/aiam_power_lifecycle_exact2_25C56_20260711.c`
- `~/Projects/ghidra_output/aiam_wcl_power_panic_chain_25C56_20260711.c`
- `~/Projects/ghidra_output/aiam_poweroff_driver_available_listing_25C56_20260711.txt`
- `~/Projects/ghidra_output/aiam_poweron_driver_available_listing_25C56_20260711.txt`
- `~/Projects/ghidra_output/aiam_power_system_range_25C56_20260711.txt`
- host serial capture:
  `~/Projects/itlwm/vm-boot/20260711T-lqm-owner/serial.log`

## Local alignment candidate

`AirportItlwm::handlePowerStateChange` now matches the reference message
scope: radio state, radio power method, rollback, return.
`AirportItlwm::setPowerState` now runs `setPowerStateGated` synchronously on
the existing command gate. The gated action preserves the shared word, owns
bit `0` atomically, implements the `0x30` gate, and removes the wake property
before OFF. The existing local boot-image owner holds bit `0x10` across its
operation. System OFF quiesces without selector `1`; system ON resumes and
then emits the single `APPLE80211_M_POWER_CHANGED` bulletin. Before those
domain-specific selector rules, the system helpers publish their ordinary
unavailable/available `0x37` carriers through the same recovered power
methods as radio transitions.

## Pre-Stage-1 corroboration (not PM validation)

Clean-build candidate `66639ECC-6348-3910-8752-2462A991FDA1` loaded through a
clean AuxKC reboot. Its signed binary SHA-256 is
`61b2141458b1a8d6b40f552c2e5cbca7e8a0e9f3a800336ca79e9f6cda848fa3`.

Four consecutive radio OFF/ON/rejoin cycles all restored DHCP
`10.77.0.47` and passed the gateway check. FBT captured exactly:

- `RADIO[0]=4`, `RADIO[1]=4`;
- unavailable transition `1` four times and available transition `2` four
  times;
- four OFF `0x37` carriers `{3,0,0,0,0xe0821804,0}`;
- four ON `0x37` carriers `{3,0,1,0,0xe0821803,0}`;
- zero `setPowerState`, `setPowerStateGated`, system-power handler, or selector
  `1` entries during the radio-only transitions.

The prior candidate panicked on the second ON; this preliminary candidate completed all
four cycles. The following concurrent 240-second ping/iperf3 run passed with
`240/240`, zero loss, and `572 MBytes` at `20.0 Mbits/sec` sender/receiver.
The WCL LQM consumer received 50 updates. The stress serial window contains no
panic, debugger, kernel trap, replayed-WAKE stack, invalid state, CoreCapture,
or NoCTL marker.

Runtime artifacts:
`/home/dima/Projects/aiam/runtime-captures/itlwm-pm-gated-20260711T153712Z`.

This run predates the system-PM candidate and deliberately entered none of the
modified IOPM functions. It proves radio/system domain separation only; it is
not the PM validation recorded below.

## Final system-PM validation

Clean-build UUID `8AFE24EC-4859-33BD-9E12-452F4DC24A90` completed a real
framework sleep/wake. The FBT sequence is exact: system OFF removes the wake
property, posts unavailable `0x37`, and emits no selector `1`; system ON posts
available `0x37` and only then emits one selector `1`. Four post-wake radio
OFF/ON cycles each retained only their own `0x37` lifecycle and no system
entries. The guest then completed `240/240` ping and 240 seconds of concurrent
iperf3 (`572 MBytes`, `20.0 Mbits/sec`) without a panic; the AP remained
associated/authenticated/authorized with `tx failed: 0`. The FBT and stress
evidence is retained under
`/home/dima/Projects/aiam/runtime-captures/itlwm-pm-state-word-20260711/`.
