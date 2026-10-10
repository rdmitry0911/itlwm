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

## Exact build and loaded regression

Production `d80d6fc3befc49c02cbb47e845c09a03bb45d43f` is pushed and independently
matched to the remote branch. Exact committed Linux and macOS payload gates
pass. The ordinary Tahoe build succeeds and all 1088 imports resolve against
the running guest BootKernelExtensions.kc. The build wrapper then returns 1
for a nonexistent guessed Info.plist key; independent source identity, clean
HEAD, UUID, hash and whole build bundle checks pass without rebuilding.

- Source identity: `946d41bce15d`.
- Mach-O UUID: `F11831F7-5BCE-3411-AEB5-90E6E5500E50`.
- Mach-O SHA256: `d791382f4e5247daa308f37b27adaf8941827195db4e77346c7f4031368d32f0`.
- Loaded boot: `7A93D037-E8BD-4E60-B64F-FD2A8E2E3178`.

Private activation `activation-20261010T122605Z` preserves four companions
and rollback, reaches READY and verifies complete installed bundle equality.
One guarded guest reboot loads the exact UUID. The first postboot observation
returns connection reset; the next verifies the new boot, full bundle and en2
management. The original dirty checkout and physical host 10.90.10.22 are
untouched. No host side rewrite or replacement of the VM base or overlay is
performed; the guest installation naturally writes its existing overlay.

Two exact private monitor observations show paused suspended state and serial
records ACPI SLEEP. Power history records 77 seconds of actual S3, power button
wake and WakeTime 1.269 seconds. A first wake banner times out; the next
verifies the same boot, image, full bundle and en2 default route. WindowServer's
30 second acknowledgement timeout remains recorded.

The corrected bounded observer finishes with COMPLETE and remote DTrace status
zero. Actual sleep cancellation records IWM disable 664.499 milliseconds.
Four native postwake controls retain Off and management; the observer captures
two On refusals at 1.850 and 1.901 milliseconds. A separate postwake observer
also finishes COMPLETE and status zero while four further native controls
retain Off and management. Those On calls return NotReady at 1.828, 1.814,
1.856 and 1.882 milliseconds. Utility exit zero is not successful radio
admission. Cold boot public On with lower fatal 2 is not admission either.

The two new handoff probes are registered, but no actual BTM handoff executes
under hardware RFKILL. This loaded regression does not qualify on air BTM,
SAE, restored Wi-Fi traffic, GUI combinations, AP or new IWN/IWX hardware.

## Additional laboratory release

The additional unsigned Debug asset is
[AirportItlwm-Tahoe-BtmHandoff-d80d6fc3.kext.zip](https://github.com/rdmitry0911/itlwm/releases/download/v2.4.0-alpha/AirportItlwm-Tahoe-BtmHandoff-d80d6fc3.kext.zip).
The package is made from the exact build without rebuilding; build, installed
and extracted bundles compare equal.

- Asset ID: `627932962`; size: 15,718,909 bytes.
- ZIP SHA256: `8b45d5cc7cce1cfe383c1801e6b0f8a69bf025fac40fbdb7fa5d9ef05261f1f7`.

All fifteen older assets, the default and complete previous notes remain
unchanged. A fresh API read verifies the addition and notes. An independent
release download has the same ZIP SHA256 and compares byte for byte to the
validated local archive. The asset remains LAB ONLY with its RFKILL limits.

## Remaining handoff scope

The later consume helper still matches public SSID and target BSSID rather
than a captured BTM generation. Its out of callback successor behavior is a
separate next audit, not closed by the source leave correction above.
