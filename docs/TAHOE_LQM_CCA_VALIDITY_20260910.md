# LQM channel occupancy is not RSSI — 2026-09-10

## Actual reference and live defect

The complete saved Apple 26.3 `AppleBCMWLANLQM::updateLQM` decompile at
`0xffffff800155362e`, in `aiam_lqm_lifecycle_exact_26_3_20260711.c`, sets
the event's byte `+0x12` and passes `+0x13` to a separate BSS-manager getter
at `0xffffff80022667b8`. It then logs this same byte as `CCA:%d`, separately
from the RSSI at `+0x04` and the extended CCA statistics at `+0x138`.
It posts the `0x1dc` carrier as event `0x27`. The original local names
`hasCurrentBssRssi/currentBssRssi` were wrong even for that cited reference;
this is not explained by assuming a later OS layout change.

The published source `893a3114`, loaded UUID
`5455B34A-AA04-34EE-8CC5-82B373798841`, copies the signed RSSI into this
CCA field with validity set. A bounded read-only observer from 23:33:27 to
23:33:45 UTC recorded four real controller submissions and four corresponding
`IO80211InfraInterface::postMessage` calls: RSSI -33, CCA validity 1,
CCA -33, and a successful producer return. It terminated with zero errors.
The actual airportd log independently reports `cca=-33.0%` along with the
correct -33 dBm RSSI. Earlier observations with another BSS report the same
false coupling at -69. No synthetic event or kernel-memory write was used.

An earlier observer of separate `IO80211LQMData` handlers saw no invocation.
That is not evidence that those handlers consumed this event; the proven
live path here is the Infra message endpoint and airportd publication.

## Correction and remaining scope

The carrier names and offset assertions now identify CCA explicitly. The
builder retains actual RSSI, noise/SNR, changing packet/error/beacon counters,
and event validity, but does not claim a CCA sample that the backend never
provided. Reused output buffers are fully cleared before publication.

This is shared production code for IWN/IWM/IWX, not three-family hardware
qualification. It does not add a true Intel occupancy producer, fill the
opaque extended-statistics carrier, enable feature bits or public LQM owner
configuration, replace event delivery, or disable the periodic signal event.
IWM/IWX RX-enable duration is not itself a busy-channel percentage and cannot
be substituted for one. Independent CCA measurement remains outstanding.

## Regression and runtime gate

The focused ASan/UBSan regression compiles the real production header and
checks exact wire bytes over 1212 RSSI/noise/counter combinations. It also
checks preservation of independent fields, repeated dirty-buffer use,
unchanged counter generations, invalid inputs and null output. The old
header is a separately compilable negative control, not a mock producer.
The corrected header passes all 1212 combinations; the unchanged `3860507a`
header compiles and fails the exact missing-CCA validity assertion. The full
payload-builder and existing WCL/scan/PMF trace regression suite also passes.

The change is not yet built, loaded or runtime-qualified. Qualification must
observe the corrected bytes at the real Infra endpoint while retaining actual
RSSI and counter events, then verify real WPA3/PMF, DHCP, traffic and recovery
after S3. Existing repeated pure-SAE joins on the old image succeeded but
still lost packets; a causal connection between false CCA and those losses
has not been established. Removing false CCA must not be relabeled as a
lossless-roaming or full GUI/reconnect fix.
