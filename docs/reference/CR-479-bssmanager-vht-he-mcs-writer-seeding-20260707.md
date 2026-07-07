# CR-479 BssManager VHT/HE MCS writer seeding

Date: 2026-07-07

## Scope

This batch closes the deferred VHT/HE part of the current-BSS MCS cache
writer layer. The previous rate/MCS seeding batch proved the
WCLConfigManager route to the framework-owned `IO80211BssManager`; this
follow-up makes the local carrier ABI and writer calls match the full Apple
`updateMCSSet` writer set.

## Reference evidence

BootKC anchors from the decompile host:

- `0xffffff8001547a0a`
  `AppleBCMWLANNetAdapter::updateMCSSet(unsigned char*, apple80211_mcs_index_set_data&, apple80211_vht_mcs_index_set_data&, apple80211_he_mcs_index_set_data&)`
- `0xffffff8002266b24`
  `IO80211BssManager::setMCSIndexSet(apple80211_mcs_index_set_data&)`
- `0xffffff8002266bc2`
  `IO80211BssManager::setVHTMCSIndexSet(apple80211_vht_mcs_index_set_data&)`
- `0xffffff8002266c4a`
  `IO80211BssManager::setHEMCSIndexSet(apple80211_he_mcs_index_set_data&)`

Static slice:
`10.7.6.112:~/Projects/ghidra_output/cr479_bootkc_memory_safe_checkpoint_smoke_20260516T1248/09_static_slices/BootKC_memory_safe/00130_ffffff8001547a0a___ZN22AppleBCMWLANNetAdapter12updateMCSSetEPhR29apple80211_mcs_index_set_dataR33apple80211_vht_mcs_index_set_dataR32apple80211_he_mcs_index_set_data.asm.txt`.

Key recovered instructions:

- `0xffffff8001547afa` calls `setMCSIndexSet`.
- `0xffffff8001547aff` initializes the VHT carrier map at `+0x04` to
  `0xffff`; `0xffffff8001547b96` calls `setVHTMCSIndexSet`.
- `0xffffff8001547bfe` initializes the HE carrier map at `+0x04` to
  `0xffff` on the unsupported path; `0xffffff8001547c2d` tail-calls
  `setHEMCSIndexSet`.
- The VHT and HE BssManager writers both copy one qword from the carrier
  pointer into BssManager/current-beacon cache storage.

## Local mapping

`apple80211_vht_mcs_index_set_data` is now an 8-byte carrier
`{ version, mcs_map, reserved }`, matching the qword copy recovered from the
writer. `apple80211_he_mcs_index_set_data` is added with the same qword shape.

`AirportItlwmSkywalkInterface::seedBssManagerRateAndMcs()` now publishes:

- the existing MCS carrier through `setMCSIndexSet` when the local current-BSS
  MCS cache is valid;
- the VHT carrier through `setVHTMCSIndexSet`, defaulting to the Apple
  unsupported map `0xffff` and using the recovered local VHT index producer
  when it succeeds;
- the HE carrier through `setHEMCSIndexSet`, defaulting to `0xffff` and using
  the parsed current-BSS HE `tx_mcs_80` map only on a current HE association.

No packet path, authentication path, or forced association outcome is changed.
