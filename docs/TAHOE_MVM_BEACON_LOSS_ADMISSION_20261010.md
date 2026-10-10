# IWM and IWX missed beacon notification admission

IWM and IWX now reject incomplete API v3 missed beacon notifications and
notifications naming another firmware MAC context before arming SAE recovery,
publishing WCL beacon loss or entering SCAN. This protects the current STA
from another context's loss indication. It does not distinguish successive
associations that reuse the same MAC ID.

## Reproduced failures

The complete unmodified `c9f1d745` handlers execute with their actual packet
length functions and notification ABI in Linux and macOS ASan and UBSan
fixtures. A complete notification with MAC ID 1 while the STA owns ID 0 arms
recovery, publishes the event and changes RUN to SCAN. The required no effect
assertion fails with exit 134 for both families on both operating systems.

A notification with no payload causes a heap buffer overflow in each actual
handler. Linux ASan exits 1; macOS ASan aborts with exit 134. The initial macOS
wrapper expected Linux's exit code and stopped after IWM. Its unchanged rerun
checks the observed macOS ASan terminal and completes all four controls. This
wrapper correction is not a separate driver defect.

The frozen macOS baseline is
`/private/var/tmp/beacon-admission-baseline-c9f1d745.X1zdz4/source`. Its test
archive SHA256 is
`f39ba7cc0b3d9413ae5afd3619c6822522f73b4e3a92e9753a0273e216c88ac7`.
Firmware delivery, kernel object layouts, credential recovery and generic
callbacks are explicit fixture boundaries, not successful radio service.

## Firmware and reference contracts

The [Intel API v3 definition](https://raw.githubusercontent.com/torvalds/linux/v5.10/drivers/net/wireless/intel/iwlwifi/fw/api/mac.h)
defines a 20 byte notification whose `mac_id` is an interface ID. The
[matching Intel Linux handler](https://raw.githubusercontent.com/torvalds/linux/v5.10/drivers/net/wireless/intel/iwlwifi/mvm/mac-ctxt.c)
resolves that ID to its interface before declaring connection or beacon loss.
The local IWM and IWX definitions match this layout. MAC command color is not
part of this notification; comparing against ID and color would reject valid
loss notifications for a nonzero command color.

The saved 25C56 reference
[link loss contract](TAHOE_FAILED_ROAM_WCL_TEARDOWN_20260910.md) retains the
independent WCL link indication and its event BSSID. The correction leaves
the existing current MAC threshold, recovery arm, WCL indication and SCAN
ordering intact. No public Apple ABI, firmware token or RFKILL guard changes.

## Candidate software checks

Linux and macOS pass 65 cases for each complete handler. Cases include matching MAC
IDs with different command colors, foreign and malformed IDs, every short
payload, header length underflow, missing packet and BSS, non STA roles,
non RUN states, threshold and watchdog boundaries, extra payload and debug
paths, and repeated notification after leaving RUN. Both WIP and exact
committed full payload aggregates and the existing IWN and IWM or IWX beacon
loss contracts pass.

## Exact build and loaded sleep regression

Production `9f192fdded865f639bad6fd8647c68b3d542ab95` is committed, pushed
and independently matched to the remote. The ordinary Tahoe build succeeds
and all 1088 imports resolve against the running guest BootKC. The combined
macOS runner exits 2 after the successful build because its final comparison
omits Tahoe in the DerivedData path. An independent status zero reconciliation
uses the actual build script path and verifies both complete bundles without
rebuilding. The original failed runner log remains.

- Source identity: `ede874343e42`.
- Mach O UUID: `1F8DADE8-3852-364F-94C4-D4C261D12573`.
- Mach O SHA256: `c999925dd91edae30df9161afc84031f89ab304f656e94c110e34172ac9484d7`.
- Loaded boot: `76B5C299-FC5C-4F69-9121-738D0887060A`.

Private activation `activation-20261010T135424Z` reaches READY and preserves
four companion kexts and rollback. Installed and built complete bundles
compare equal. One guarded guest reboot loads the new UUID. The first SSH
observation times out during banner exchange; the next confirms identity,
bundle, en2 route and actual missed beacon and lower stop entry probes. The
owned QEMU unit and PID 517226 remain unchanged. Physical host 10.90.10.22 and
the original dirty guest checkout remain untouched; no base or overlay file
is replaced on the host.

Native cold Off and On return utility success and public power On, but the
actual init again refuses hardware RFKILL fatal 2 without IFF_RUNNING. Two
independent monitor observations show suspended state, with serial System
Sleep and ACPI SLEEP. One exact private monitor wake resumes the same boot.
Power history records a 46 second sleep interval and WakeTime 1.426 seconds;
that interval is not a measurement of the full physical paused duration.

The first postwake SSH observation times out before executing its controls.
The next confirms the same loaded image and whole bundle, independent en2
management, and four native Off and On controls. Actual On returns NotReady
at 1.792, 1.778, 1.971 and 2.059 milliseconds. The guest ends Wi Fi Off.

The guest local observer in `/private/var/tmp/iwm-sleep-stop-local.cnPrSE`
survives SSH detach and real S3, then ends with status zero and TRACE_COMPLETE.
The actual sleep disable takes 668.062 milliseconds; stop_device takes
667.959 milliseconds. Two failed NIC access calls take 324.988 and 328.685
milliseconds. Stop worker drain takes 4.181 microseconds and prepare_card_hw
17.764 microseconds. This localizes the dominant measured delay to NIC access,
not worker drain or card preparation. It does not prove DMA idle or authorize
descriptor reclaim after an access failure. The existing WindowServer power
acknowledgement limitation remains separate from Wi Fi service restoration.

No actual firmware missed beacon entry is observed under RFKILL. Successful
on air recovery, GUI combinations, restored traffic after sleep and IWX
hardware remain separate qualification requirements.

## Additional laboratory release

The additional unsigned Debug asset is
[AirportItlwm-Tahoe-BeaconAdmission-9f192fdd.kext.zip](https://github.com/rdmitry0911/itlwm/releases/download/v2.4.0-alpha/AirportItlwm-Tahoe-BeaconAdmission-9f192fdd.kext.zip).
Built, installed and extracted complete bundles compare equal without a
packaging rebuild.

- Asset ID: `628117447`; size: 15,719,217 bytes.
- ZIP SHA256: `15dc4add7e01c4a940b9e631cc3b479e6839080f5a5cd69afb7759742cfbae50`.

Fresh API checks verify 18 assets, all seventeen older asset records and the
default unchanged. The complete previous release notes remain the exact
suffix beneath the LAB entry. An independent release download compares byte
for byte with the local verified archive. Hardware RFKILL and absent on air
qualification remain explicit in its label and notes.

Successful on air recovery, GUI combinations, restored traffic after sleep
and IWX hardware remain separate qualification requirements. Current IWM
9260 hardware RFKILL prevents those service checks.
