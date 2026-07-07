# CR-479 BssManager rate/MCS writer seeding

Date: 2026-07-07

## Scope

This batch closes the framework-visible current-BSS rate cache writer gap.
The public `getRATE_SET` and `getMCS_INDEX_SET` paths were already aligned
with `IO80211BssManager::getCurrentRateSet()` and
`IO80211BssManager::getCurrentMCSSet()`. Tahoe framework code can also read
the BssManager cache directly, so the local WCLConfigManager/BssManager bridge
must seed the same cache that Apple updates after association.

## Reference evidence

BootKC symbol anchors from `10.7.6.112:~/Projects/ghidra_additional/kc_all_symbols.txt`:

- `0xffffff8001546772` `AppleBCMWLANNetAdapter::updateRateSetAsync()`
- `0xffffff80015475e8` `AppleBCMWLANNetAdapter::updateRateSetSync(apple80211_rate_set_data*)`
- `0xffffff8001547c38` `AppleBCMWLANNetAdapter::updateRateSetAsyncCallback(CommandID&, int, CommandRxPayload&, void*)`
- `0xffffff8002266c78` `IO80211BssManager::getCurrentRateSet(apple80211_rate_set_data*)`
- `0xffffff8002266cb2` `IO80211BssManager::setRateSet(apple80211_rate_set_data&)`
- `0xffffff8001547a0a` `AppleBCMWLANNetAdapter::updateMCSSet(unsigned char*, apple80211_mcs_index_set_data&, apple80211_vht_mcs_index_set_data&, apple80211_he_mcs_index_set_data&)`
- `0xffffff8002266b24` `IO80211BssManager::setMCSIndexSet(apple80211_mcs_index_set_data&)`
- `0xffffff8002266bc2` `IO80211BssManager::setVHTMCSIndexSet(apple80211_vht_mcs_index_set_data&)`
- `0xffffff8002266c4a` `IO80211BssManager::setHEMCSIndexSet(apple80211_he_mcs_index_set_data&)`

Static slices already present on the decompile host:

- `cr479_bootkc_memory_safe_checkpoint_smoke_20260516T1248/.../00131_ffffff8001547c38___ZN22AppleBCMWLANNetAdapter26updateRateSetAsyncCallbackER9CommandIDiR16CommandRxPayloadPv.asm.txt`
  builds an `apple80211_rate_set_data` carrier and calls
  `0xffffff8002266cb2`.
- `cr479_bootkc_memory_safe_checkpoint_smoke_20260516T1248/.../00130_ffffff8001547a0a___ZN22AppleBCMWLANNetAdapter12updateMCSSetEPhR29apple80211_mcs_index_set_dataR33apple80211_vht_mcs_index_set_dataR32apple80211_he_mcs_index_set_data.asm.txt`
  calls `setMCSIndexSet`, `setVHTMCSIndexSet`, and `setHEMCSIndexSet`.
- `04238_ffffff8002266b24_FUN_ffffff8002266b24.asm.txt` shows
  `setMCSIndexSet` copying the public MCS carrier into the BssManager cache
  object and, when present, the current beacon cache.

## Local mapping

`AirportItlwmSkywalkInterface::seedBssManagerRateAndMcs()` uses the same
verified WCLConfigManager route that the previous MCS seeding used to recover
the framework-owned `IO80211BssManager *`. It now seeds:

- `setRateSet(apple80211_rate_set_data&)` from the negotiated `ni_rates`
  carrier already used by the reference-aligned public `getRATE_SET`.
- `setMCSIndexSet(apple80211_mcs_index_set_data&)` from the existing MCS
  carrier.

VHT and HE writers are intentionally left out of this batch. The Apple
producer calls them, but local HE public carrier shape is not recovered here,
and the existing VHT public path is the cached `nrate` scalar path rather than
a proven BssManager VHT-index-set producer.
