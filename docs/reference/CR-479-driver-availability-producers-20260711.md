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
| `AppleBCMWLANCore::powerOff` | `0xffffff80015dd65c` | `0xffffff80015ddd20`, `0xffffff80015ddd54` | unavailable |
| `AppleBCMWLANCore::powerOn` | `0xffffff80015df67a` | `0xffffff80015e0245`, `0xffffff80015e0278`, `0xffffff80015e02ab` | available after power-on |

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
- Wi-Fi power transitions publish the reference power-off carrier before
  local teardown and the power-on carrier after successful local enablement.

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

## Runtime validation

On final clean-build UUID `09663B25-365D-3D90-BE59-D50490351847`, FBT on
`IO80211Controller::postMessage(IO80211SkywalkInterface*, unsigned int,
void*, unsigned long, bool)` captured one power-off and one power-on call:

- `selector=0x37 len=0xf8 async=1 prefix={3,0,0,0,0xe0821804,0}`
- `selector=0x37 len=0xf8 async=1 prefix={3,0,1,0,0xe0821803,0}`

No extra generic-bool carrier was emitted. The guest reassociated immediately,
completed 30/30 follow-up pings with zero loss, and the 5000 ms LQM producer
resumed with three consumer callbacks in the following 17-second trace.
