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
paths, and repeated notification after leaving RUN. Both full payload
aggregates and the existing IWN and IWM or IWX beacon loss contracts pass.
Exact committed build, activation and loaded regression remain pending at
this checkpoint.

Successful on air recovery, GUI combinations, restored traffic after sleep
and IWX hardware remain separate qualification requirements. Current IWM
9260 hardware RFKILL prevents those service checks.
