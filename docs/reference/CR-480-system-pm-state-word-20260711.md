# Tahoe system-PM state-word contract (25C56)

## Scope

This note closes the reference contract needed to align the local
`setPowerState` / `setPowerStateGated` producer. It does not claim that the PM
deviation is the sole cause of the previously observed WCL replay panic.

The complete AppleBCMWLANCore PM range was recovered from the 25C56
`BootKernelExtensions.kc`. The exhaustive Wi-Fi corpus used a 24-worker
parallel decompiler queue. The follow-up work here used that already-complete
corpus plus two bounded disassembly/symbol queries; no new serial multi-function
decompile batch was required. At the follow-up query time the Ghidra host had
48 CPUs, 121 GiB RAM, 111 GiB available memory, and load averages
12.34/10.90/9.34.

## Resolved symbols

| Address | Symbol |
| --- | --- |
| `0xffffff8001573712` | `AppleBCMWLANCore::bootChipImage(AppleBCMWLANChipImage const*)` |
| `0xffffff8001564846` | `AppleBCMWLANCore::reportInitFailure(...)` |
| `0xffffff8001575946` | `AppleBCMWLANCore::loadAndSetup(AppleBCMWLANChipImage const*)` |
| `0xffffff80015ebf6c` | `AppleBCMWLANCore::registerWithPolicyMaker(IOService*)` |
| `0xffffff80015ec10c` | `AppleBCMWLANCore::setPowerState(unsigned long, IOService*)` |
| `0xffffff80015ec31a` | `AppleBCMWLANCore::reportSystemPowerState(unsigned int, bool, bool)` |
| `0xffffff80015ec346` | `AppleBCMWLANCore::setPowerStateGated(unsigned long, IOService*)` |
| `0xffffff80015ec6f0` | `AppleBCMWLANCore::powerOffSystem()` |
| `0xffffff80015ecb08` | `AppleBCMWLANCore::powerOnSystem()` |
| `0xffffff80015ecfcc` | `AppleBCMWLANCore::watchdogFailed()` |
| `0xffffff800226c009` | `removePropertyHelper(IOService*, char const*)` |

The names above come from the live 25C56 BootKC symbol table. In particular,
the call immediately before the OFF bit clear is proven to be the property
removal helper; it is not inferred from the string alone.

## `setPowerState` contract

At `0xffffff80015ec1ad`, the method invokes the controller command gate with
`setPowerStateGated` and retains the returned `IOReturn`. It then reads the
private reporter owner at core expansion `+0x1580`. If that pointer is non-NULL,
`0xffffff80015ec1d6..0xffffff80015ec202` calls
`AppleBCMWLANIOReportingCore::reportSystemPowerState(0, ordinal == 2, 0,
state+0x7b40)`. The gated result is returned unchanged.

The reporting body at `0xffffff800161bd78..0xffffff800161be87` validates its
small state argument and updates reporter-owned timestamp/counter storage. It
does not publish a WCL or Apple80211 power message. The branch is conditional
on the private reporter object, not on the generic inherited IOReport entry
points.

`AirportItlwm` has no `AppleBCMWLANIOReportingCore`-equivalent object, member,
initializer, or system-power reporting method. The exact applicable reference
branch is therefore `owner == NULL`. Adding a generic reporter call or a fake
no-op owner would introduce an unproven object and is forbidden.

## `setPowerStateGated` state machine

The method takes one snapshot of the 32-bit word at core expansion `+0x2890`.
Its observable transition table is:

| Condition | State operation | Side effect | Return |
| --- | --- | --- | --- |
| `(word & 0x30) == 0x20` | none | none | `0` |
| ordinal `1`, bit `0` clear | `OSBitOrAtomic(1, &word)` | `powerOnSystem()` | `0` |
| ordinal `1`, bit `0` set | none | none | `0` |
| ordinal `0`, bit `0` clear | none | none | `0` |
| ordinal `0`, bit `0` set | `removePropertyHelper(this, "IO80211WokeSystem")`, then `OSBitAndAtomic(0xfffffffe, &word)` | `powerOffSystem()` | `0` |
| any other ordinal | none | none | `0` |

This ordering matters: ON records the powered state before entering the resume
helper; OFF removes the wake marker and records the powered-off state before
entering the quiesce helper. Both atomic operations preserve every unrelated
bit in the shared word.

## Relevant state-word owner inventory

The exhaustive `+0x2890` writer scan resolves every bit involved in the
`0x30` gate and its adjacent watchdog state:

| Mask | Owner and lifecycle |
| --- | --- |
| `0x01` | Set only by the real gated ON transition and cleared only by the real gated OFF transition. |
| `0x10` | `bootChipImage` atomically sets it before image setup. `bootChipImage` clears it on both success and failure, and `reportInitFailure` clears it on its error path. |
| `0x20` | `watchdogFailed` atomically sets it after power-on retry exhaustion. No atomic-AND writer in the complete corpus clears this bit, so it is the persistent transition-blocking state. |
| `0x40` | `watchdogFailed` sets it immediately before `0x20`; `loadAndSetup` clears it after a successful load. |

`watchdogFailed` first invokes the hidden interface advisory-disable slot,
atomically sets `0x40`, atomically sets `0x20`, calls `signalDriverReady`, calls
`halt`, and returns `0xe00002bc`. Its callers include the bounded failure tail
of `powerOnSystem`. This is why the PM gate tests `(word & 0x30) == 0x20`
instead of testing bit `0x20` alone: an active boot (`0x10`) is a distinct
lifecycle state even when other failure state exists.

The local Intel path already has an exact boot owner,
`AirportItlwm::performTahoeBootChipImage`, so it can own `0x10` across that
operation. It does not have the Broadcom `powerOnSystem` retry-exhaustion and
`watchdogFailed` lifecycle. The local alignment must therefore preserve the
full word and implement the gate without inventing a producer for `0x20` or
`0x40`.

## Power-message ownership

The system helpers remain distinct from radio `handlePowerStateChange`:

- radio OFF/ON publishes only the exact `APPLE80211_M_DRIVER_AVAILABLE`
  (`0x37`) carriers;
- `powerOffSystem` calls `powerOff(true)`, so system OFF also publishes the
  exact unavailable `0x37` carrier before quiesce, but has no selector `1`;
- `powerOnSystem` calls `powerOn()`, so system ON publishes the exact available
  `0x37` carrier before it posts one selector `1` with NULL payload, length
  zero, and asynchronous delivery.

Thus a normal framework IOPM cycle is:

1. gated OFF: remove `IO80211WokeSystem`, clear bit `0`, publish unavailable
   `0x37`, quiesce, no selector `1`;
2. gated ON: set bit `0`, resume, publish available `0x37`, then one selector
   `1`;
3. radio transitions remain separate invocations of the same normal `0x37`
   carrier methods and never publish selector `1`.

## Verification consequence

A radio-only toggle cannot validate this change because it must not enter the
IOPM methods. Final validation drove a real framework system sleep/wake on
UUID `8AFE24EC-4859-33BD-9E12-452F4DC24A90`: FBT captured the ordered gated
OFF/ON entries, property-removal helper, unavailable/available system `0x37`
carriers, selector `1` absent on OFF and once after ON, and synchronous zero
returns. Four post-wake radio cycles remained IOPM-free. Reassociation,
`240/240` concurrent ping, 240-second iperf3 (`572 MBytes` at `20.0 Mbits/sec`),
AP authorization with `tx failed: 0`, and the clean fixed-boot serial window
complete the runtime proof. See
`/home/dima/Projects/aiam/runtime-captures/itlwm-pm-state-word-20260711/`.

## Durable evidence anchors

- `~/Projects/ghidra_output/aiam_power_system_range_25C56_20260711.txt`
- `~/Projects/ghidra_output/aiam_power_ioreport_range2_25C56_20260711.txt`
- `~/Projects/ghidra_output/aiam_pm_remove_property_25C56_20260711.txt`
- `~/Projects/ghidra_output/aiam_pm_kernel_helpers_25C56_20260711.txt`
- `~/Projects/ghidra_output/aiam_wifi_surface_25C56_20260711/03_decomp/25C56_wifi_kernel_surface/functions/0xffffff80015ec346_FUN_ffffff80015ec346.c`
- `~/Projects/ghidra_output/aiam_wifi_surface_25C56_20260711/03_decomp/25C56_wifi_kernel_surface/functions/0xffffff80015ecfcc_FUN_ffffff80015ecfcc.c`
- `~/Projects/ghidra_output/aiam_wifi_surface_25C56_20260711/03_decomp/25C56_wifi_kernel_surface/functions/0xffffff8001573712_FUN_ffffff8001573712.c`
- live 25C56 BootKC `nm -n -g` symbol ranges around the addresses in the table.
