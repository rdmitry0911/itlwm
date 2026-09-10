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

At the source-test checkpoint the change was not yet built or loaded. Qualification must
observe the corrected bytes at the real Infra endpoint while retaining actual
RSSI and counter events, then verify real WPA3/PMF, DHCP, traffic and recovery
after S3. Existing repeated pure-SAE joins on the old image succeeded but
still lost packets; a causal connection between false CCA and those losses
has not been established. Removing false CCA must not be relabeled as a
lossless-roaming or full GUI/reconnect fix.

## Loaded-image producer and actual userspace consumer

Source `cf4b50ad` built with all 1085 external symbols resolved. Private
five-member AuxKC admission and transactional activation passed, preserving
the four companion members. The 23:49:49 UTC guest boot loaded UUID
`A476957E-AC9D-3712-B32E-F6C2BB870F81`, matching the frozen and installed
Mach-O SHA-256
`9aad4a139c73a658f171e2ae04ba7fcb2dfae6500ab20ef9f02d518047843201`.

At 23:50:59 UTC the same real controller and Infra endpoints emitted
RSSI -69, CCA validity zero, CCA byte zero and event validity one. Independent
noise -93, SNR 24, TX 148, RX 409 and beacon 462 values remained present;
the producer returned success. The observer ended with zero errors. The
new airportd process independently logged seven periodic events between
23:50:14 and 23:50:59, preserving RSSI/noise/SNR and changing frame/beacon
counters, while omitting the unavailable CCA field. It did not report a
fabricated zero-percent measurement instead of the previous negative value.

That first observer overlapped the start of the native-sharing regression,
so it covers one driver event before STA stopped, not an uninterrupted
multi-sample STA traffic qualification. Likewise, a concurrent ten-packet
STA check received nine replies across that role change; it is not a
steady-state comparison or a demonstrated regression caused by this change.
The full AP, STA and S3 qualification remains in progress; no new release
has been promoted at this point.

## Native AP regression failure: release remains held

On this same loaded candidate, native WPA3 and then WPA2 Internet Sharing
each passed real client DHCP, 20/20 client-to-gateway packets, independently
isolated cold-neighbor 10/10 reverse packets, and routed HTTP. External client
state confirmed SAE with required PMF for WPA3 and WPA2-PSK for WPA2.

The following open sharing start failed: the external client could not find
its SSID. Fresh full and directed scans did not discover it. Separate bounded
standalone monitor captures on both relevant 2.4-GHz channels received 238
and 196 other beacon/probe-response frames without kernel drops, but no test
AP. The primary STA subsequently recovered its ordinary saved network. An
active-looking BSD interface/bridge was therefore not proof of an on-air AP;
later both interfaces reported inactive.

A read-only observer using the exact candidate's AP-owner object DWARF
recorded lifecycle 5 (Terminal) together with AP-up 1 from 00:01:15 through
00:01:33 UTC. Stop, resume, initial-start and confirmed-start flags were all
clear. The observer completed without errors. The watchdog did not query the
lower AP channel because its `isApRunning()` predicate rejects Terminal.

A subsequent standard open retry never reached the instrumented HostAP
start/stop methods. airportd instead rejected stop because the primary did
not report SWAP mode. The bounded observer ended at 00:06:45 UTC without
errors. Normal sharing disable followed by ordinary Wi-Fi off/on also left
the contradictory owner state unchanged; another native WPA2 attempt failed
external discovery and again encountered the userspace stop rejection.

This identifies a persistent AP lifecycle/public-mode disagreement. A stale
or reentrant lower-stop completion is a hypothesis, not yet a captured causal
interleaving. No AP source correction has been made, and the evidence does not
establish that the independent CCA change introduced this failure. The public
release remains the previously qualified `893a3114` image. A fresh same-image
boot and instrumented native mode sequence are required before any promotion.
