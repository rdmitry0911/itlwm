# CR-479: WCL extended BSS info producer recovery

Date: 2026-07-16

## Scope

This correction covers only Tahoe V2/Skywalk slot `[533]`,
`getWCL_EXTENDED_BSS_INFO`. It restores the active WCL current-BSS request
path after the earlier quarantine proved too broad. It does not change V1 or
the ABI slot, and it is a partial local producer rather than a reconstruction
of every private Apple owner.

## Recovered reference contract

The reference is the 25C56 x86_64
`com.apple.DriverKit-AppleBCMWLAN` DEXT:

```text
path: /System/Library/DriverExtensions/com.apple.DriverKit-AppleBCMWLAN.dext/com.apple.DriverKit-AppleBCMWLAN
SHA-256: 4696795caefe738e849e5a4bb12077b7a3c2e68e9bb44fc99e8c91ef5f6463ab
UUID: 149C0AD1-A92F-35BC-AA69-5C8815C5421E
```

The active Infra wrapper at `0x100017c58` tail-dispatches through virtual
offset `+0x420` to `AppleBCMWLANCore::getWCL_EXTENDED_BSS_INFO` at
`0x100132df6`. Core returns `0xe00002bc` for a null carrier; otherwise it
loads its NetAdapter at Core state `+0x15e0` and tail-calls
`AppleBCMWLANNetAdapter::getExtendedBssInfo` at `0x10019de64`.

That adapter is a real output pipeline, not a no-op success: it invokes
`updateRateSetSync`, `updateMCSSetSyc`, and `getAssociatedWPARSNIESync`,
placing sub-carriers around caller offsets `+0xbc`, `+0xcc/+0xd4`, and `+0x113`
respectively. Rate/MCS failures are returned. Feature `0x73` with an 11be
adapter additionally obtains current BSSID and calls `getMloContext` at
caller `+0xdc`. The direct disassembly is captured in
`docs/reference/artifacts/wcl-extended-bss-info-25c56/raw.txt`.

## Why the quarantine was reverted

Before the 2026-07-14 quarantine, the local V2/Skywalk getter retained a null
guard but returned success for every non-null carrier without writing any
field or calling an Extended-BSS, NetAdapter, rate/MCS, RSN, or MLO producer.
The quarantine then made every non-null request return
`kIOReturnUnsupported`.

Runtime tracing on the local Tahoe path establishes that this is not a dead
public probe: `WCLNetManager::setCurrentBSS` sends GET selector `0x1cc` with a
`0x214` output carrier after BSS-info update. `0x1cc` dispatches to slot
`[533]`; returning `kIOReturnUnsupported` makes WCL leave the network and
deauthenticate locally. The following external operator-workspace captures
are not verified by this source-only script:

- `runtime-captures/itlwm-wcl-extended-bss-info-regression-20260716/`
  `main-full-producer-20260716T1838Z-linkstate.dtrace.log`, SHA-256
  `5555ade49a794456069867bea2906ff758a5c643fae3f0b0d101fa9a28b8863b`;
- `runtime-captures/itlwm-wcl-extended-bss-info-regression-20260716/`
  `full-producer-radio-cycles.log`, SHA-256
  `371e57bb9d3af57b372564fb987f4a2686919b91bfb0658ca4eafd31b5680d8f`.

## Local recovery

The local header now declares the recovered `0x214` carrier exactly enough to
preserve its field boundaries: rate set at `+0x000`, MCS at `+0x0bc`, VHT at
`+0x0cc`, HE at `+0x0d4`, opaque MLO context at `+0x0dc`, and the raw
associated-RSN IE range at `+0x113`.

For a non-null request the local getter clears that carrier, requires a RUN
state and BSS, and produces the locally owned data in the reference order:

- current rate set and MCS set are filled through their existing current-BSS
  producers, with their failures propagated;
- VHT starts from the local `version=1` / `mcs_map=0xffff` default and replaces
  it when the existing VHT current-BSS producer succeeds; HE starts from the
  same default and replaces its TX MCS-80 only under the local HEON + 11AX
  guard;
- the associated RSN range copies only the bounded canonical raw BSS TLV;
- no local 11be/MLO owner exists, so the zeroed MLO range is deliberately left
  empty rather than fabricated.

The existing null guard remains `0xe00002bc`-equivalent
(`kIOReturnBadArgumentTahoe`). A missing controller, non-RUN state, or missing
BSS returns the existing driver-not-available error after the deterministic
clear.

This is a **partial local producer**, not byte-identical Apple valid-input
parity. In particular, it does not reproduce the Apple NetAdapter's private
synchronization backend or its feature-`0x73` 11be MLO context branch. It does
restore the active local rate/MCS/RSN association surface that runtime WCL
requires instead of advertising a false unsupported result.

## Verification boundary

`scripts/wcl_extended_bss_info_quarantine_report.py` checks the reference
identity/raw anchors, active V2 slot declarations, exact local carrier
boundaries, null guard, producer ordering, propagated rate/MCS failures,
bounded raw RSN source, VHT/HE producer/default shape, absence of fabricated
MLO, and documentation of external runtime evidence. It deliberately records
that this is not byte-identical Apple parity; it does not validate those
external captures.

Runtime validation separately proves selector `0x1cc`, the WCL update return,
and the produced rate/MCS/VHT/HE/RSN fields under an actual association; build,
load, radio-cycle, DHCP, and gateway-ping gates cover the resulting data path.
