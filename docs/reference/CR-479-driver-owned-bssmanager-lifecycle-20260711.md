# CR-479: driver-owned BssManager lifecycle

Date: 2026-07-11  
Reference: Tahoe 25C56 guest BootKC and KDK 26.3  
Projects: `wifi_analysis_25C56/BootKC_guest_25C56.kc`,
`kdk_26_3_25D125`

## Ownership correction

The Apple driver and WCL own different BssManager objects.

- `AppleBCMWLANCore::initAfterIORegUpdated()` creates
  `AppleBCMWLANBssManager::withOptions(core)` and stores it in the core ivars.
- `AppleBCMWLANCore::getBssManager()` returns that driver-owned object.
- `WCLController::initWithOptions()` independently creates a `WCLBssManager`.
- `WCLConfigManager::initWithOptions()` retains the controller's WCL object.

The previous local `tahoeRecoverWclBssManager(...)` pointer walk therefore
reached a real framework object, but the wrong owner. AppleBCMWLAN never walks
the WCL private object graph to obtain its driver BssManager. All historical
notes that describe this route as the Apple driver ownership path are
superseded by this note.

KDK 26.3 anchors:

- `AppleBCMWLANBssManager::withOptions` at `0xffffff800154f47c`;
- `AppleBCMWLANCore::initAfterIORegUpdated` factory call at
  `0xffffff8001584fa1`;
- `AppleBCMWLANCore::getBssManager` at `0xffffff800158fa0c`;
- base `IO80211BssManager` allocator, constructor, and initializer at
  `0xffffff80022660ac`, `0xffffff80022660c0`, and `0xffffff8002266120`.

The base manager and beacon metaclasses both register object size `0x18`.
Apple's private manager subclass has size `0x20`; its extra state is the core
pointer. It obtains the core logger and initializes the base manager with a
null scan-cache store.

## Current-BSS contract

The current 25C56 base methods are:

- `IO80211BssManager::setCurrentBSS` at `0xffffff80022418fc`;
- `IO80211BssManager::getCurrentBSS` at `0xffffff800224369a`;
- `IO80211BssManager::isAssociated` at `0xffffff8002241cd8`.

The base setter retains the new beacon, releases the old beacon, stores the
current pointer, and writes its bool argument to a separate ivar.
`isAssociated()` does not read that bool; it returns whether the current
pointer is non-null.

`AppleBCMWLANBssManager::setCurrentBSS` at current
`0xffffff800152f57c` performs these additional driver-owned effects:

1. query core feature bit `0x60` and pass it to the base setter;
2. keep the subclass current pointer;
3. post message `0x52` with the current `apple80211_channel`, or a zero
   channel when clearing;
4. update private state mask `0x01` according to current-pointer presence.

KDK `processChipCaps` clears both 64-bit feature words. The recovered static
feature population paths do not set bit `0x60`, so its reference value remains
false in the analyzed build. This bool is preserved as its own contract; it is
not replaced by an association fallback.

## Producer lifecycle

The only recovered calls to the Apple subclass current-BSS setter are inside
`AppleBCMWLANCore::setWCL_LINK_STATE_UPDATE`:

- link down clears the driver-owned manager;
- link up with payload byte `+0x08 == 0` leaves it unchanged;
- link up with payload byte `+0x08 != 0` obtains the current BSS info, creates
  a fresh `IO80211BSSBeacon`, publishes it, and then updates rate/MCS state.

The generic `setLinkState` path does not create, refresh, or clear this object.
The current BSS payload is the exact packed `0x44` metadata followed by an
`0x800` IE area, total `0x844`. Both the manager and beacon are genuine
IO80211Family objects created through their exported allocators, constructors,
and initializers.

## Local closure

- `AirportItlwm` owns one base `IO80211BssManager` for its full started
  lifetime and releases it before its logger.
- initialization uses the recovered `0x18` allocation, framework constructor,
  driver logger, and null scan-cache store.
- `setWCL_LINK_STATE_UPDATE` remains the sole current-BSS lifecycle producer.
- each refresh builds a fresh exact `0x844` carrier and a genuine framework
  beacon, then applies the recovered base-setter, channel-message, and private
  state effects.
- rate, MCS, auth, band, RSSI, and ad-hoc writers now target this driver-owned
  manager.
- all private WCL pointer offsets, retry seed bursts, synthetic LQM bulletin
  production, and WCL-manager mutation are removed.

The local driver uses composition because the private AppleBCMWLAN subclass is
not an IO80211Family export. Its externally observable setter effects are
implemented at the local core ownership boundary; no private object layout is
invented.

## Evidence

- `~/Projects/ghidra_output/aiam_io_bssmanager_string_xrefs_exact_26_3_20260711.txt`
- `~/Projects/ghidra_output/aiam_applebcm_bssmanager_setcurrent_xrefs_26_3_20260711.txt`
- `~/Projects/ghidra_output/aiam_feature_flag_getter_exact_25C56_20260711.txt`
- `~/Projects/ghidra_output/aiam_feature_flag_mutator_refs_26_3_20260711.txt`
- `~/Projects/ghidra_output/aiam_feature_flag_population_range_26_3_20260711.txt`
- `~/Projects/ghidra_output/aiam_process_chip_caps_exact_26_3_20260711.txt`
- `~/Projects/ghidra_output/aiam_dynamic_feature_set_update_report_26_3_20260711.txt`
- `~/Projects/ghidra_output/aiam_dynamic_feature_set_tcp_checksum_26_3_20260711.txt`
- `~/Projects/ghidra_output/aiam_configure_tcp_checksum_full_26_3_20260711.txt`

Static contract validation and fresh Tahoe runtime are complete. The clean
combined build loaded as UUID
`09663B25-365D-3D90-BE59-D50490351847`, joined `AIAMlab6235` at
`10.77.0.47`, and completed concurrent 240-second ping/iperf3 with 240/240
replies, zero loss, and 572 MB at 20.0 Mbit/s. The exact LQM callback used
this manager's `isAssociated()` result for 50 successful rearm cycles over a
250-second FBT window. Serial and host fault filters remained clean.
