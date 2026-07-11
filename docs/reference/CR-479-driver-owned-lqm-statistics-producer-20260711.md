# CR-479 driver-owned LQM statistics producer, 2026-07-11

## Scope

This note closes the periodic driver-side statistics producer that feeds the
existing Tahoe IO80211Family LQM consumer. It replaces neither
`IO80211LinkQualityMonitor` nor `IO80211LQMData`; it implements the
AppleBCMWLAN-owned timer, real statistics snapshot, and Infra message endpoint
used below those framework owners.

## Reference evidence

Current AppleBCMWLAN/IO80211Family decompilation is stored on `10.7.6.112`:

- `~/Projects/ghidra_output/aiam_lqm_lifecycle_exact_26_3_20260711.c`
- `~/Projects/ghidra_output/aiam_lqm_withdriver_disasm_26_3_20260711.txt`
- `~/Projects/ghidra_output/aiam_lqm_stat_tail_disasm_26_3_20260711.txt`
- `~/Projects/ghidra_output/aiam_lqm_control_decomp_26_3_20260711.c`
- `~/Projects/ghidra_output/aiam_lqm_linkstate_disasm_26_3_20260711.txt`
- `~/Projects/ghidra_output/aiam_lqm_refs_26_3_20260711.txt`

The recovered lifecycle is:

- core initialization creates `AppleBCMWLANLQM` and retains it at core
  expansion `+0x15e8`;
- `AppleBCMWLANLQM::withDriver` creates a dedicated timer retained at LQM
  state `+0x2150`;
- the default interval at state `+0x11f4` is `0x1388` (5000 ms);
- `startAssocTimer()` cancels the prior timeout and schedules the configured
  interval;
- `setStatsTimerIntervalMS()` stores the new interval and rearms the same
  timer;
- the `handleStatUpdates` tail tests the driver-owned
  `IO80211BssManager::isAssociated()` and rearms only while that object still
  has a current BSS;
- `freeResources()` cancels and releases the timer before owner teardown.

The rearm is not conditional on every individual statistics read succeeding.
A transient failed sample is therefore not allowed to disable all later LQM
updates.

## Event contract

`AppleBCMWLANLQM::updateLQM(...)` builds a zero-initialized `0x1dc` carrier
and the driver posts message `0x27` asynchronously through its Infra endpoint.
The recovered fields used by this layer are:

| Offset | Size | Meaning |
| --- | ---: | --- |
| `+0x000` | 1 | RSSI valid |
| `+0x004` | 4 | RSSI |
| `+0x008` | 1 | per-antenna RSSI valid |
| `+0x009` | 2 | per-antenna RSSI |
| `+0x00b` | 1 | SNR valid |
| `+0x00c` | 2 | SNR |
| `+0x00e` | 1 | noise valid |
| `+0x010` | 2 | noise |
| `+0x012` | 1 | current-BSS RSSI valid |
| `+0x013` | 1 | current-BSS RSSI |
| `+0x014` | 4 | TX errors |
| `+0x018` | 4 | RX errors |
| `+0x01c` | 4 | TX frames |
| `+0x024` | 4 | RX frames |
| `+0x028` | 4 | received beacons |
| `+0x030` | 1 | counter snapshot valid |
| `+0x1d8` | 1 | event valid |
| `+0x1d9` | 1 | counter snapshot changed |

The counter fields and `+0x030/+0x1d9` are populated only when the current
hardware snapshot differs from the preceding snapshot. Signal validity comes
from actual RSSI/noise availability. No fixed counters, fixed one-second
cadence, or fabricated per-antenna values are admitted.

## Intel data sources

The local producer uses existing authoritative backend state:

- TX/RX packets and errors come from the live `IONetworkStats` attached to the
  net80211 interface;
- IWM reads firmware `sc_stats.rx.general.channel_beacons`;
- IWX retains `general.beacon_counter[0]` from both current and v11 firmware
  statistics notifications;
- IWN increments a driver-owned count on each real
  `IWN_BEACON_STATISTICS` notification rather than estimating beacons from a
  wall-clock interval;
- RSSI comes from the associated `ieee80211_node`; noise comes from the
  backend statistics owner.

## Local lifecycle

`AirportItlwm` owns a dedicated `IOTimerEventSource` on the existing watchdog
workloop. It is distinct from the one-second net80211 watchdog timer.

- `setWCL_LINK_STATE_UPDATE` starts or stops it on the same association edges
  that update the driver-owned BssManager;
- `setLQM_CONFIG` updates and rearms the owner interval after the recovered
  carrier validation;
- the timer callback enters the controller command gate before reading
  net80211/backend state;
- event `0x27` is posted through `fNetIf`, the real Infra protocol endpoint;
- rearm tests the genuine driver-owned BssManager association state;
- adapter disable and final release cancel, disable, detach, and release the
  timer before the HAL/workloop owners disappear.

There is no direct WCL manager call, private object-layout traversal,
ungated callback fallback, synthetic timestamp write, or direct framework
sink call.

## Validation

Final clean-build and runtime evidence is recorded in discrepancy inventory
item 244.

## Non-claims

This layer does not force CARD_CAPABILITIES slow-WiFi bits, fabricate the
framework monitor, alter public CoreWLAN or `networksetup`, implement AP/GO,
or add a userspace fallback.
