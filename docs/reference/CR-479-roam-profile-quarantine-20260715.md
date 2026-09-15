# CR-479: ROAM_PROFILE — from false-success quarantine to real marshalling

Date: 2026-07-15 (quarantine) / superseded 2026-09-15 (marshalling)

## Status: SUPERSEDED

The original CR-479 quarantine made Tahoe V2/Skywalk slot `[485]`
`getROAM_PROFILE` fail closed (`kIOReturnUnsupported` on every non-null
carrier) because no real backend existed. That fail-closed behavior is now
**superseded**: `getROAM_PROFILE` marshals the authoritative host roam policy
(`ic->ic_roam_profile`) into the public carrier, matching the reference layout.
No test may assert the old unsupported behavior.

## Recovered reference contract

The reference is the 25C56 x86_64
`com.apple.DriverKit-AppleBCMWLAN` DEXT:

```text
path: /System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN
SHA-256: 4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab
UUID: 149C0AD1-A92F-35BC-AA69-5C8815C5421E
```

The active Infra wrapper at `0x1000171c4` dispatches through virtual offset
`+0x768` to `AppleBCMWLANCore::getROAM_PROFILE` at `0x100140c0c` (the same Core
producer is exposed in the guest kernelcache as `getROAM_PROFILE @16356de`).
Core loads its RoamAdapter at `+0x15c0` and tail-jumps to
`AppleBCMWLANRoamAdapter::getROAM_PROFILE` at `0x10001d198`. The adapter returns
raw `0x16` only if the carrier or primary interface is absent; otherwise it
writes the three band-id headers and queries all three bands through
`getRoamProfilePerBand` at `0x10001d216` (`getRoamProfilePerBand @154d7a6` in the
guest kernelcache). Only a zero per-band result sets that band's valid flag.

### Pinned carrier layout

`apple80211_roam_profile_all_bands` is three `0x80`-stride band slots (total
`0x180`, confirmed by the adapter loop `subq $-0x80,%r15 / cmpq $0x180,%r15`).
The adapter writes `slot+0x4 = {2G=4, 5G=2, 6G=0x400}` for all three bands and
`slot+0xc = 1` only on a zero per-band return. `getRoamProfilePerBand` writes
`profile_cnt` at `slot+0x8` and each bracket into a `0x1c`-stride entry starting
at `slot+0x10` (four entries fill the slot: `0x10 + 4*0x1c = 0x80`). The per-band
os_log format string pins the field names:

```text
Roam profile[%d]: Band:%d, RSSI:[%d,%d], Flag:0x%x,
    ScanParams:(%d,%d,%d,%d,%d), Candidate:(%d,%d,%d), LPCore:[%d,%d]
```

Cross-referencing the DEST writes (`getRoamProfilePerBand` @0x10001d8b3 loop)
with the source reads named by that format yields the per-entry (`0x1c`) layout:

| slot offset | size | field                     |
|-------------|------|---------------------------|
| +0x00       | u32  | version (left 0)          |
| +0x04       | u32  | band id (4 / 2 / 0x400)   |
| +0x08       | u32  | profile_cnt               |
| +0x0c       | u32  | valid flag                |
| +0x10       | ...  | profiles[4], 0x1c stride  |

| profile offset | size | field                |
|----------------|------|----------------------|
| +0x00          | u32  | flags                |
| +0x04          | s8   | trigger_dbm          |
| +0x05          | s8   | rssi_lower           |
| +0x06          | s8   | roam_delta_db        |
| +0x07          | s8   | rssi_boost_delta     |
| +0x08          | s8   | rssi_boost_thresh    |
| +0x10          | u16  | backoff_multiplier   |
| +0x12          | u16  | full_scan_period     |
| +0x14          | u16  | init_scan_period     |
| +0x16          | u16  | nfscan               |
| +0x18          | u16  | max_scan_period      |

The captured DEXT disassembly is in
`docs/reference/artifacts/roam-profile-25c56/raw.txt`.

## Local marshalling (supersedes the quarantine)

itlwm has no Broadcom firmware and no separate Intel roam engine. It keeps the
authoritative policy in host state: `ic->ic_roam_profile`
(`struct ieee80211_roam_profile_policy`), written by
`setWCL_ROAM_PROFILE_CONFIG -> tahoeBuildIntelRoamProfile ->
ieee80211_set_roam_profile_policy` and read back through
`ieee80211_roam_profile_snapshot(ic, &policy)` (now non-static).

`getROAM_PROFILE` keeps the null guard, bzeros the `0x180` carrier, publishes
the three band-id headers, and — for each band whose `valid_mask` bit is set —
writes `profile_cnt`, sets the valid flag, and marshals each stored bracket into
the pinned `0x1c` entry. When a band (or the whole policy) has no data it stays
invalid, and the call still returns `kIOReturnSuccess` with the band-id headers,
exactly as the reference does when the firmware `roam_prof` IOVAR is empty.

Returning the marshalled `ic_roam_profile` is **functionally equivalent** to the
reference: the round-trip
`setWCL_ROAM_PROFILE_CONFIG(policy) -> getROAM_PROFILE()` reproduces the same
per-band brackets that were stored, at the pinned byte offsets. This is not the
forbidden CR-479 blind success (marking bands valid without data); a band is
marked valid only when it carries real bracket data.

## Verification boundary

`scripts/roam_profile_quarantine_report.py` verifies reference identity and raw
anchors, the active V2 slot, the preserved null guard, that the getter marshals
the host snapshot into the pinned carrier layout, and that no band is marked
valid without a `valid_mask` gate. The get-returns-configured-policy round trip
is exercised in the lab (dtrace of `ieee80211_set_roam_profile_policy` plus a
sel-216 `SIOCGA80211` read-back).
