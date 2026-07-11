# CR-479: Tahoe driver-availability producers

Date: 2026-07-11  
Reference: Tahoe 25C56 guest BootKC  
Ghidra project: `wifi_analysis_25C56/BootKC_guest_25C56.kc`

## Reference ownership

`AppleBCMWLANCore::signalDriverReady()` at `0xffffff8001567dda` is not the
`APPLE80211_M_DRIVER_AVAILABLE` producer. It publishes
`CoreWiFiDriverReadyKey` as the string `"true"` or `"false"` through the
hidden interface object at core state `+0x1510`.

The normal `0x37` message producers are separate lifecycle methods:

| Producer | Current entry | Message calls | Transition |
| --- | --- | --- | --- |
| `AppleBCMWLANCore::bootChipImage` | `0xffffff8001573712` | `0xffffff8001573a63`, `0xffffff8001573abc`, `0xffffff8001573af0` | initial ready |
| `AppleBCMWLANCore::powerOff` | `0xffffff80015dd6ac` | `0xffffff80015ddd20`, `0xffffff80015ddd54` | unavailable |
| `AppleBCMWLANCore::powerOn` | `0xffffff80015df6ca` | `0xffffff80015e0245`, `0xffffff80015e0278`, `0xffffff80015e02ab` | available after power-on |

Every call uses `IO80211Controller::postMessage` at
`0xffffff80021f58f0`, message `0x37`, carrier length `0xf8`, and async flag
`1`. The repeated calls target the non-null Infra interface instances owned at
core state `+0x2c20`, `+0x2c28`, and `+0x74f0`; the local controller owns one
corresponding `fNetIf` instance.

`AppleBCMWLANCore::watchdog` and
`AppleBCMWLANCore::completeFaultReportCallback` are additional, fault-owned
producers. Their payload prefixes and trailing diagnostics are dynamic. They
do not justify reusing a normal lifecycle payload from a periodic watchdog.

## Exact normal carriers

The 25C56 carriers have six independent prefix dwords followed by `0xe0`
bytes of zeroed fault detail for normal transitions:

| Offset | Boot ready | Power off | Power on |
| --- | ---: | ---: | ---: |
| `+0x00` | `3` | `3` | `3` |
| `+0x04` | `0x20` | `0` | `0` |
| `+0x08` | `1` | `0` | `1` |
| `+0x0c` | `0` | `0` | `0` |
| `+0x10` | `0xe0822803` | `0xe0821804` | `0xe0821803` |
| `+0x14` | `0` | `0` | `0` |

The message selector is not stored at payload `+0x00`; it is the separate
`postMessage` argument. The previous local `uint64_t event` field therefore
misdescribed the carrier even though the family consumer's availability check
only exposed the dword at `+0x08`.

## Local closure

- `apple80211_driver_available_data` now preserves the six-dword prefix and
  exact `0xf8` size.
- `TahoeDriverAvailabilityContracts::build(...)` has distinct `BootReady`,
  `PowerOff`, and `PowerOn` variants with the recovered constants.
- boot success publishes only after `enableAdapter()` succeeds and preserves
  the reference order: interface enable, `CoreWiFiDriverReadyKey`, then the
  boot-ready `0x37` carrier.
- boot failure drives the advisory/property path but does not fabricate a
  normal power-off carrier.
- Direct radio transitions publish the reference power-off carrier before local
  teardown and the power-on carrier after successful local enablement. System
  helpers traverse the same carrier producers through `powerOff(true)` and
  `powerOn()` before their system-specific completion work.

This does not synthesize dynamic fault reports, add a fallback availability
gate, alter public CoreWLAN/networksetup behavior, or claim AP/GO/LQM closure.

## Artifacts

- `~/Projects/ghidra_output/aiam_signal_driver_ready_exact_25C56_20260711.txt`
- `~/Projects/ghidra_output/aiam_bootchip_driver_available_exact2_25C56_20260711.txt`
- `~/Projects/ghidra_output/aiam_poweroff_driver_available_listing_25C56_20260711.txt`
- `~/Projects/ghidra_output/aiam_poweron_driver_available_listing_25C56_20260711.txt`
- `~/Projects/ghidra_output/aiam_watchdog_driver_available_listing_25C56_20260711.txt`
- `~/Projects/ghidra_output/aiam_faultcomplete_driver_available_listing_25C56_20260711.txt`
- `~/Projects/ghidra_output/aiam_driver_available_setter_exact_25C56_20260711.txt`
- `~/Projects/ghidra_output/aiam_power_lifecycle_exact2_25C56_20260711.c`

## Runtime validation

On final clean-build UUID `09663B25-365D-3D90-BE59-D50490351847`, FBT on
`IO80211Controller::postMessage(IO80211SkywalkInterface*, unsigned int,
void*, unsigned long, bool)` captured one power-off and one power-on call:

- `selector=0x37 len=0xf8 async=1 prefix={3,0,0,0,0xe0821804,0}`
- `selector=0x37 len=0xf8 async=1 prefix={3,0,1,0,0xe0821803,0}`

No extra generic-bool carrier was emitted. The guest reassociated immediately,
completed 30/30 follow-up pings with zero loss, and the 5000 ms LQM producer
resumed with three consumer callbacks in the following 17-second trace.

## Radio versus system power correction

The exact entry of `AppleBCMWLANCore::handlePowerStateChange` is
`0xffffff800157af02`. Its complete body only selects `powerOff(bool)` or
`powerOn()`, rolls the logical radio state back on error, and returns. It does
not publish `APPLE80211_M_POWER_CHANGED`.

That selector belongs to the separate IOPM system sleep/wake lifecycle. The
local radio path had retained a `POWER_CHANGED` post after each real radio
transition. Removing it was necessary but not sufficient: FBT captured only
the exact unavailable `0x37` on radio-off, yet the next radio-on still
reproduced the replayed-WAKE panic.

The independent IOPM producer was also non-reference. Current 25C56
`setPowerState` synchronously calls `setPowerStateGated` through the controller
command gate; the gated action owns bit `0` in a shared state word, applies the
`0x30 == 0x20` gate, and removes `IO80211WokeSystem` before OFF. System OFF has
no selector `1` post, and system ON posts it once after resume. The local
asynchronous thread-call/manual-ack path and its system-OFF post are therefore
removed. The 25C56 helpers also prove that system OFF enters `powerOff(true)`
and system ON enters `powerOn()`: the exact unavailable/available `0x37`
carriers bracket the system cycle as well. The domain distinction is selector
scope, not exclusive ownership of `0x37`: only system ON adds selector `1`
after the available carrier. The complete PM contract is in
`CR-480-system-pm-state-word-20260711.md`; no sole-root-cause claim is made.

Pre-Stage-1 candidate `66639ECC-6348-3910-8752-2462A991FDA1` completed four
consecutive radio OFF/ON/rejoin cycles. FBT counted four exact unavailable and
four exact available `0x37` carriers, with no IOPM handler or selector `1`
publication during any radio transition. All four cycles restored DHCP and
gateway traffic; the subsequent 240-second concurrent ping/iperf3 run passed
with zero packet loss and no replayed-WAKE panic in serial.

That run recorded zero IOPM entries and cannot validate the PM implementation
or serve as Stage 2 evidence. It remains durable radio-domain corroboration.

## Final system-PM validation

The corrected clean build, UUID `8AFE24EC-4859-33BD-9E12-452F4DC24A90`, was
then driven through a real framework sleep/wake. FBT captured system OFF as
the exact ordered property removal, unavailable `0x37`, and no selector `1`;
system ON captured available `0x37` followed by exactly one selector `1`.
Four subsequent radio OFF/ON/rejoin cycles remained separate from IOPM and
used their own exact carriers. The post-wake stress completed `240/240` ping
with zero loss and 240 seconds of iperf3 at `572 MBytes`/`20.0 Mbits/sec`; the
AP station remained authorized with `tx failed: 0`, and the fixed-boot serial
window contains no panic, invalid transition, kernel trap, CoreCapture, or
NoCTL marker. The captured trace is
`/home/dima/Projects/aiam/runtime-captures/itlwm-pm-state-word-20260711/fixed-system-and-radio-trace.raw`.
