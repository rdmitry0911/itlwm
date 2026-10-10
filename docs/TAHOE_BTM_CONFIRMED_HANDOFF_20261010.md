# Confirmed BTM cancellation and target handoff

A confirmed BSS Transition target now survives only its exact completed
source leave. Ordinary cancellation or an independent association replacement
retires the old logical BTM request. A late management TX callback cannot start
reconnect for whichever association happens to occupy the same BSS node.
The shared correction applies to IWN, IWM and IWX. Current IWM 9260 hardware
RFKILL still prevents qualification of these paths in the air.

## Reproduced cancellation failures

The complete production epoch cancellation and BTM retarget functions from
`f17c88b7` reproduce a stale target after an ordinary epoch change from 7 to 8.
The old record remains active and copy_retarget returns its target. A second
composition executes the real last TX terminal, cancels through the real INIT
state note after its leaf unlock, then enters the real node reconnect callback.
The old callback changes INIT back to SCAN and advances the epoch again to 9.
Both controls compile and fail their intended assertions with exit 134 on
Linux and macOS ASan and UBSan.

The frozen macOS baseline is
`/private/var/tmp/btm-confirmed-handoff-baseline.zbcMwP`.
Its source and fixture archive SHA256 is
`113827d562f6e2cb84ae690d4d2cf1a51d8c3ddf989b46d86f340eb620e96f62`.
The first ordinary wrapper stops on missing static HAL input files after its
145 carrier and 30 cancellation cases pass. Adding only those unchanged
static inputs makes that ordinary baseline wrapper pass. The original failed
log and both semantic negatives are retained separately.

Three adjacent complete production copy_retarget controls also reproduce
source BSSID, SSID and epoch replacement on both operating systems. Kernel,
firmware results and callback scheduling are explicit fixture boundaries;
these are not observed GUI or physical TX events.

## Exact completed leave

The real management TX owner records LEAVE_DONE only after both submitted
response and deauthentication descriptors have delivered their existing
terminal callbacks. It captures the BTM request generation and source epoch
under the selected BSS leaf and passes those values to node reconnect.
The callback revalidates them before dispatch and after the SAE hook.

The legacy WCL fallback performs ordinary preflight, then advances the epoch
only for that exact confirmed completed leave. It records SCAN_HELD with the
new epoch. Out of leaf revocation callbacks cannot replace the association
and still authorize the old SCAN request. Preflight and lower errors clear
only the captured request generation, preserving an admitted successor.

Copying the target requires either the current RUN source in LEAVE_DONE or
the exact SCAN_HELD continuation. Pending descriptor ownership does not yet
authorize retarget and is not erased merely because copying returns false.
The two direct SAE admission functions independently require the matching
phase and association identity. Controlled selected BSS replacement retains
the admitted SAE request, but no longer carries the obsolete BTM record.

All phase values and epochs are local host ownership facts. No firmware token,
capability, successful TX receipt or physical descriptor retirement is added.
The saved 25C56 WCLRoamManager::linkDown export at `ffffff8002105ae4` clears
logical roam state and invokes timer and policy cleanup. The existing
[lower TX reference contract](reference/AppleBCMWLAN_IWM_SAE_TX_COMPLETION_25C56_20260801.md)
keeps physical completion distinct from command acceptance.

## Software qualification

Linux and macOS full payload aggregates and the WNM and direct SAE source
contracts pass. The carrier fixture executes 60 new BTM TX, cancellation,
handoff and SAE admission cases alongside the existing 145 carrier cases and
30 post target cancellation edges. Both TX terminal orders, repeated and
foreign receipts, four cancellation stages, callback replacement, refusal,
malformed handoff identity and both SAE admission paths are covered.
The RX and timer fixture passes 92 cases, including the three confirmed
source replacement controls in its ordinary aggregate run.

The SAE policy predicates and two admission bodies are complete production
functions. Hook registration, credential revocation, lower state callbacks
and kernel scheduling remain explicit doubles. This does not qualify complete
SAE authentication, an actual GUI transition, DHCP or traffic.

## Remaining qualification

The currently loaded image remains `f17c88b7` until the new committed build
passes its ordinary Tahoe build, import check and private activation cycle.
Runtime installation, real S3 regression and an additional LAB release are
the next gates. The current 9260 RFKILL is not bypassed.

The later consume helper still matches public SSID and target BSSID rather
than a captured BTM generation. Its out of callback successor behavior is a
separate next audit, not closed by the source leave correction above.
