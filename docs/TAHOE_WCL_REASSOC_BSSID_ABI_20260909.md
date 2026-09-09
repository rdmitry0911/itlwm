# WCL reassociation BSSID carrier — 2026-09-09

## Evidence and correction

The complete 25C56 NetAdapter `sendReassocCommandLegacy`, `V1` and `V3`
decompilations in `aiam_applebcm_reassoc_25C56_20260801/reassoc.txt` copy six
bytes per entry from public offset `0x64` into the firmware BSSID field/list.
They use the first BSSID even when the public count at `0x90` is zero; counts
of two or more select the bounded list. Channels are a separate array at
offset zero with their count at `0x94`. These are not score/channel tuples.
The older incomplete BootKC exports are not evidence for this conclusion.

On loaded `35589ce0`, a real `WCLNetManager::setROAMWithBssid` request supplied
one broadcast BSSID and no channel restriction. The old driver interpreted
the address as a score and channel 255, filtered it against an idle AP owner's
primary channel, armed an AP handoff and acknowledged a no-op. No AP operation
was active. The raw observation is recorded in the AP-intent note.

The public and common carriers now preserve BSSID bytes. Exact BSSIDs form an
allowlist; broadcast and unspecified addresses admit a wildcard within the
associated ESS, subject independently to channels and prune RSSI. Actual
target admission still validates rates, security and required PMF even when
the discovery-side desired SSID is empty. A valid WCL target may replace the
old desired BSSID/channel pin. Eligible targets use observed RSSI, never MAC
address bytes as a fabricated score.

The AP-intent gate from `3b14777c` is retained. A real single-channel AP
lifecycle constrains only the channel array, without rewriting BSSID entries.
An incompatible explicit channel request returns busy before disarming the
initial BSSID pin. Wildcard and addressed requests take the real bounded
background-scan path; the old empty-request/no-op successes and their false
AP-handoff producer are removed. A no-target result belongs to the existing
asynchronous failure path, not a synthetic successful reassociation.

## Verification boundary

The UBSan test compiles production carrier declarations, the full WCL
producer, AP intent/controller queries, candidate disposition and BSS matcher.
It covers zero/one/oversized counts, the observed broadcast carrier, explicit
and multiple BSSIDs, independent channel filtering, source/other-ESS exclusion,
RSSI pruning, lifecycle constraints, and real admission rejection of open,
wrong-AKM, missing-PMF, wrong-group-management-cipher and invalid-rate targets.
The ordinary unfiltered scan-export behavior remains separate.

This is an implementation candidate, not yet a runtime-qualified release.
The loaded and published image remains `35589ce0` until a new full build,
private AuxKC admission, guest activation and on-air regression succeed.
The common internal request layout changed; diagnostics must use matching
build DWARF, not offsets from an earlier kext. The public size remains `0x9c`.

This fix does not itself prove seamless roaming, forced same-BSSID
reassociation, every reference feature flag, reliable discovery of the
missing 5-GHz BSS, or the full repeated-public-join/GUI matrix.
