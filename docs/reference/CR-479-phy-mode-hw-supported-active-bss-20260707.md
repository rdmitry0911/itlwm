# CR-479 PHY_MODE hardware-supported vector and active BSS carrier

Date: 2026-07-07

## Reference evidence

- `AppleBCMWLANCore::getPHY_MODE(apple80211_phymode_data*)` is present in
  the BootKC static slice at `0xffffff80015f7cc6`.
- The function initializes the public carrier with `version=1` and
  `phy_mode=APPLE80211_MODE_AUTO`.
- It calls `AppleBCMWLANCore::getSupportedBand(unsigned int*)` at
  `0xffffff80015ab73a`; successful band data ORs in `11a` for the 5 GHz bit
  and `11b|11g` for the 2 GHz bit.
- It calls `AppleBCMWLANCore::getSupportedPhyModeFromHW()` at
  `0xffffff80015ab880`; return values in the recovered range add `11n`, and
  higher recovered hardware modes add `11ac` in addition to `11n`.
- It gates `11ax` through feature/capability checks separate from the current
  configured net80211 mode.
- It writes `active_phy_mode` only after the BssManager associated path is
  true, using `AppleBCMWLANCore::getBssPhyModde(AppleBCMWLANBSSBeacon*)` at
  `0xffffff80015dad80`.

## Local discrepancy

The previous local Skywalk and legacy `getPHY_MODE` implementations built the
supported vector as fixed `11a|11b|11g|11n`, then added `11ac` from
`IEEE80211_F_VHTON` and `11ax` from `IEEE80211_F_HEON`. Those flags are
configuration/current-mode flags. In this tree, `ieee80211_channel_init()`
also derives `ic_modecaps` from channel flags and adds both `11ac` and `11ax`
when a VHT channel exists, so AUTO mode can set VHT/HE flags even on an
HT-only HAL.

Runtime on the active iwn-6030 path confirmed the public producer mismatch:
the direct bound Apple80211 key14 path used the local Skywalk producer and
reported `PHYMODE_SUPPORTED=0x1f`, while the HAL has no VHT capability and the
current BSS was correctly 802.11n.

## Local closure

- Added `TahoePhyModeContracts` for the public carrier constants and supported
  / active PHY builder rules.
- Skywalk and legacy `getPHY_MODE` now initialize `version=1`,
  `active_phy_mode=UNKNOWN`, and a supported vector that starts with AUTO.
- Supported bands are derived from the controller channel table.
- `11n` is added from real HT capability, `11ac` from complete VHT
  capability plus MCS/support carriers, and `11ax` from complete HE
  capability plus MCS carriers.
- The iwn attach path explicitly clears VHT/HE capability and MCS carrier
  fields before publishing its HT capability set.
- `active_phy_mode` is published only for `IEEE80211_S_RUN` with a current BSS;
  it is derived from current BSS HE/VHT/HT capability and legacy channel/rate
  evidence instead of `ic_curmode`.
- Standalone Tahoe contract tests now cover the iwn-class HT-only vector, VHT
  and HE additions, pre-association UNKNOWN active mode, and active BSS
  priority order.

## WCL static path note

`system_profiler -detailLevel full SPAirPortDataType -xml` does not consume
the local Skywalk `getPHY_MODE` producer on Tahoe. Live DTrace shows
`IO80211Glue::sendIOUCToWcl(..., 0xe, payload, 0xc, handled)` routes key14 to
`WCLConfigManager::getPHY_MODE(bulletinBoardMessage&)`, and that WCL handler
marks the message handled before the public fallback can run.

The BootKC static slice at `0xffffff8002123cc2` writes
`version=1, phy_mode=0x9f, active=0` as its baseline carrier. It then reads
the internal device-configuration byte at `+0xcad` and can only extend the
supported vector to `0x19f`, `0x29f`, or `0x39f`; there is no branch that
removes `APPLE80211_MODE_11AC` from the baseline. Runtime on 2026-07-07 had
`cfg_cad=0x0`, and WCL returned `supported=0x9f, active=0x10`.

Therefore the local fix is intentionally limited to the driver-owned public
producer and its legacy shadow. The `system_profiler` WCL baseline remains
`802.11 a/b/g/n/ac` unless a future, reference-backed WCL/device-configuration
layer is recovered; disabling WCL handling or forcing a fallback would not
match the observed Apple handler.
