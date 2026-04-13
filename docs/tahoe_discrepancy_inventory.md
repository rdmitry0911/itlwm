# Tahoe Discrepancy Inventory

Date: 2026-04-13

## Purpose

Keep one durable list of every Tahoe architectural mismatch that has been
confirmed, superseded, or remains open, so fixes can be applied against a
tracked queue instead of as disconnected symptom patches.

This inventory is intentionally split into:

- `closed`:
  already fixed and documented elsewhere
- `superseded`:
  earlier conclusion disproven by panic/runtime evidence
- `open_confirmed`:
  live or source-confirmed mismatch with enough evidence to keep on the queue
- `open_needs_decompile`:
  clear stub/unsupported surface that must not be "filled in" before the Apple
  producer/consumer contract is lifted far enough

## Queue Coverage Snapshot

- `Q1 Interface Construction`:
  open
  constructor/start exactness still incomplete after the panic-backed rollback
  from the wrong 2-argument init path

- `Q2 BSD Attach / Identity`:
  no currently open confirmed discrepancy beyond the constructor/hidden-object
  dependency carried by `Q1` / `Q3`

- `Q3 Ready-State / Hidden Producer`:
  open
  visible key/value shape recovered, hidden interface-side producer object not
  yet modeled 1:1

- `Q4 Early IOC Bring-up`:
  the currently known mandatory Tahoe bring-up IOC blockers are closed

- `Q5 Getter Cluster`:
  partially closed
  `RATE / RATE_SET / RSSI` recovered, `MCS_INDEX_SET / NOISE / TXPOWER / MCS`
  still open

- `Q6 State-Carriers`:
  first confirmed carrier batch closed
  remaining carrier-like slots are now part of the `Q13` unsupported census

- `Q7 WCL Adapter Plane`:
  open
  adapter-owned roam/bgscan/join/net methods still sit on ack-only stubs

- `Q8 Scan Plane`:
  the currently confirmed scan-abort / completion bulletin issues are closed
  but scan-adjacent WCL config producers remain open through `Q7`

- `Q9 Join / Assoc Plane`:
  open
  join-adjacent WCL producers such as `JOIN_ABORT` and reassoc paths are still
  only ack-only placeholders

- `Q10 Net-Link / IP Plane`:
  early state-carrier portion closed
  link/QoS/ARP adjunct producers remain open through `Q7`

- `Q11 Skywalk Datapath / Queue Surface`:
  open
  large unsupported override surface still unclassified slot-by-slot

- `Q12 Sleep / Wake / Reset / Teardown`:
  open
  reset/crash/wow/system-sleep policy slots are still unsupported and require
  Apple-path classification before touching

- `Q13 Unsupported Skywalk Surface`:
  open
  raw header surface now carries 147 unsupported overrides and 17 ack-only
  stubs; after the first confirmed Apple-unsupported classification batches,
  121 unsupported-return slots still remain open discrepancies

## Closed

- `PLATFORM_CONFIG`:
  Tahoe 7-byte packed producer path recovered and implemented.
  See [tahoe_platform_config_root_cause.md](/Users/bob/Projects/itlwm/docs/tahoe_platform_config_root_cause.md).

- `POWERSAVE`:
  unsupported path replaced with cached level contract.
  See [tahoe_platform_config_root_cause.md](/Users/bob/Projects/itlwm/docs/tahoe_platform_config_root_cause.md).

- `payload-less VIRTUAL_IF_ROLE/PARENT`:
  no longer allowed to fall into raw POSIX `6`.
  See [tahoe_platform_config_root_cause.md](/Users/bob/Projects/itlwm/docs/tahoe_platform_config_root_cause.md).

- `WCL_TRIGGER_CC`:
  recovered request-shape contract implemented.
  See [tahoe_platform_config_root_cause.md](/Users/bob/Projects/itlwm/docs/tahoe_platform_config_root_cause.md).

- `scan abort / WCL scan complete bulletin`:
  corrected away from synthetic/non-Apple completion flow.
  See [tahoe_platform_config_root_cause.md](/Users/bob/Projects/itlwm/docs/tahoe_platform_config_root_cause.md).

- `Skywalk BSD bridge reachability`:
  existing handlers were unreachable on Tahoe until the BSD bridge was expanded.
  See [tahoe_signal_chain_audit.md](/Users/bob/Projects/itlwm/docs/tahoe_signal_chain_audit.md).

- `state-carrier IOC batch`:
  `OS_FEATURE_FLAGS`, `DHCP_RENEWAL_DATA`, `BATTERY_POWERSAVE_CONFIG`,
  `POWER_PROFILE`, `IPV4_PARAMS`, `IPV6_PARAMS`, `INFRA_ENUMERATED`.
  See [tahoe_signal_chain_audit.md](/Users/bob/Projects/itlwm/docs/tahoe_signal_chain_audit.md).

- `Q5 getter subset: RATE / RATE_SET / RSSI`:
  raw POSIX `6` removed from the getter subset whose Apple helper contracts
  were recovered strongly enough to implement 1:1.
  `RATE` now follows the Apple `0xe0822403` not-associated contract,
  `RATE_SET` follows the BSS-manager `0xe0822403` / `0xe00002f0` split, and
  `RSSI` follows the BSS-manager current-BSS contract instead of `if RUN else 6`.
  See [tahoe_signal_chain_audit.md](/Users/bob/Projects/itlwm/docs/tahoe_signal_chain_audit.md).

- `Q13 mini-batch: HW_ADDR ABI and producer path`:
  `getHW_ADDR` is no longer allowed to sit on generic `kIOReturnUnsupported`.
  Apple core populates `version + 6-byte hardware MAC`, and the family-side
  consumer `WCLDeviceConfiguration::setHwMacAddr(...)` confirms the same ABI.
  See [tahoe_signal_chain_audit.md](/Users/bob/Projects/itlwm/docs/tahoe_signal_chain_audit.md).

- `Q13 mini-batch: explicit Apple-unsupported getter classification`:
  a first Tahoe subset was proven to be "Apple also unsupported", not missing
  producers. These slots stay on `0xe00002c7` in the reference
  `AppleBCMWLANInfraProtocol` path itself and therefore should be removed from
  the open unsupported census rather than "implemented":
  `getRANGING_ENABLE`, `getRANGING_START`, `getRANGING_CAPS`,
  `getCOUNTRY_CHANNELS_INFO`, `getWCL_WNM_OFFLOAD`, `getFW_CLOCK_INFO`,
  `getTIMESYNC_STATS`, `getHE_COUNTERS`, `getSMARTCCA_OPMODE`,
  `getLQM_STATISTICS`.
  See [tahoe_signal_chain_audit.md](/Users/bob/Projects/itlwm/docs/tahoe_signal_chain_audit.md).

- `Q13 mini-batch: explicit Apple-unsupported setter classification`:
  a first setter subset was also proven to be "Apple also unsupported" in the
  vendor-side infra path itself. Those slots should not stay in the open
  discrepancy queue just because the local header still returns
  `kIOReturnUnsupported`:
  `setROAM_PROFILE`, `setROAM_CACHE_UPDATE`, `setSET_WIFI_ASSERTION_STATE`,
  `setMWS_ACCESSORY_POWER_LIMIT_WIFI_ENH`, `setWOW_LOW_POWER_MODE`,
  `setSTAND_ALONE_MODE_STATE`, `setTIMESYNC_GPIO`, `setFW_CLOCK_SOURCE`,
  `setTIMESYNC_TX_POLICY`, `setTIMESYNC_RX_POLICY`, `setTIMESTAMPING_EN`,
  `setMWS_TIME_SHARING_WIFI_ENH`, `setSDB_ENABLE`, `setBTCOEX_EXT_PROFILE`,
  `setTX_MODE_CONFIG`.
  `setVOICE_IND_STATE` was the one real mismatch in that cluster: Apple
  returns `0xe00002c7`, while the port had a false validate+ack success path.
  See [tahoe_signal_chain_audit.md](/Users/bob/Projects/itlwm/docs/tahoe_signal_chain_audit.md).

- `Q13 mini-batch: VHT capability producer path`:
  `getVHT_CAPABILITY` no longer belongs in the generic unsupported bucket.
  `AppleBCMWLANCore::getVHT_CAPABILITY(...)` is a real producer that returns
  `0x2d` only when the PHY-capability gate fails, and otherwise copies a
  14-byte VHT capability IE payload from core state into the
  `apple80211_vht_capability` body. This also corrects the local ABI: the
  payload after `version` is `0x0e` bytes, not `0x10`.
  See [tahoe_signal_chain_audit.md](/Users/bob/Projects/itlwm/docs/tahoe_signal_chain_audit.md).

- `Q13 mini-batch: thermal / power-budget carrier getters`:
  `getTHERMAL_INDEX` and `getPOWER_BUDGET` no longer belong in the generic
  unsupported bucket. The Apple vendor producers are direct core-state scalar
  carriers that write a 32-bit value at caller offset `+4`, sourced from
  offsets `+0x0` and `+0x4` inside the core-state block.
  See [tahoe_signal_chain_audit.md](/Users/bob/Projects/itlwm/docs/tahoe_signal_chain_audit.md).

- `Q13 mini-batch: guard-interval producer path`:
  `getGUARD_INTERVAL` no longer belongs in the generic unsupported bucket.
  `AppleBCMWLANCore::getGUARD_INTERVAL(...)` is a real producer: it rejects
  `NULL` with `0xe00002c2`, queries cached `"nrate"` state, and falls back to
  `800` ns when no recognized short-GI encoding is present.
  See [tahoe_signal_chain_audit.md](/Users/bob/Projects/itlwm/docs/tahoe_signal_chain_audit.md).

- `Q13 mini-batch: HT capability / private-mac / TCPKA getter-side ABI lift`:
  `getHT_CAPABILITY`, `getPRIVATE_MAC`, and `getOFFLOAD_TCPKA_ENABLE` no longer
  belong in the generic unsupported bucket. The remote AppleBCMWLANCoreMach-O
  recovered a real `getHT_CAPABILITY(...)` producer body, while
  `getPRIVATE_MAC(...)` and `getOFFLOAD_TCPKA_ENABLE(...)` proved that the
  local Tahoe headers were still missing their packed carrier ABIs.
  See [tahoe_signal_chain_audit.md](/Users/bob/Projects/itlwm/docs/tahoe_signal_chain_audit.md).

## Superseded

- `Tahoe must use IO80211InfraInterface::init(provider, addr)`:
  superseded by live panic `panic16.txt`.
  The rebuilt driver crashed in
  `IO80211InfraInterface::linkState() + 0xb` with `CR2=0x18`, and new
  decompile shows `linkState()` dereferences `*(this+0x128)+0x18`.
  The current port state therefore does not survive `start()` on that path.
  See [tahoe_signal_chain_audit.md](/Users/bob/Projects/itlwm/docs/tahoe_signal_chain_audit.md).

- `consumer-side ready-state replay hooks`:
  superseded once the init-path mistake was corrected. Apple producer evidence
  does not support readiness replay from `createEventPipe()`, `ether_ifattach()`
  or a duplicate post-`deferBSDAttach(false)` `registerService()`.
  See [tahoe_signal_chain_audit.md](/Users/bob/Projects/itlwm/docs/tahoe_signal_chain_audit.md).

## Open Confirmed

### 1. Raw `return 6` getter cluster still present in live-hit Tahoe paths

Files:

- [AirportItlwmSkywalkInterface.cpp](/Users/bob/Projects/itlwm/AirportItlwm/AirportItlwmSkywalkInterface.cpp)
- [AirportSTAIOCTL.cpp](/Users/bob/Projects/itlwm/AirportItlwm/AirportSTAIOCTL.cpp)

Current raw `6` return sites still exist in getter paths including:

- `getMCS_INDEX_SET`
- `getTXPOWER`
- `getNOISE`
- `getMCS`

Why this stays open:

- live Tahoe IOC logs previously showed raw `6` for several of these selectors
- the remaining open items still depend on helper/body recovery that has not
  yet been matched to trustworthy local carrier/state sources

Next required evidence:

- recover helper-level return semantics for the not-associated path of the
  BSS-manager-backed getters before touching them

Recovered Apple helper semantics now available:

- `AppleBCMWLANCore::getRATE()`:
  associated → success, not associated → `0xe0822403`
- `IO80211BssManager::getCurrentRateSet()`:
  no current BSS → `0xe0822403`, empty cached set → `0xe00002f0`, else success
- `IO80211BssManager::getCurrentMCSSet()`:
  no current BSS → `0xe0822403`, missing valid cached set → `0xe00002f0`,
  else success
- `IO80211BssManager::getCurrentRSSI(int&)`:
  no current BSS → `0xe0822403`, else success
- `IO80211BssManager::getCurrentNoise(short&)`:
  no current BSS → `0xe0822403`, missing valid noise sample → `0x66`, else
  success
- `AppleBCMWLANCore::getTXPOWER()`:
  config-backed query through `"qtxpower"`, returns underlying query status

So the next code batch for `Q5` must not replace raw `6` with ad hoc zeros.
It must align each getter with one of these Apple helper-level contracts.

Concrete sub-items:

- `Q5-RATE-001`
  file: `AirportItlwmSkywalkInterface.cpp::getRATE`
  current: closed
  reference: Apple returns `0xe0822403` when not associated
  status: `closed`

- `Q5-RATESET-002`
  file: `AirportItlwmSkywalkInterface.cpp::getRATE_SET`
  current: closed
  reference: BSS-manager current-rate-set contract
  status: `closed`

- `Q5-MCSSET-003`
  file: `AirportItlwmSkywalkInterface.cpp::getMCS_INDEX_SET`
  current: `if RUN else 6`
  reference: BSS-manager current-MCS-set contract
  status: `open_needs_decompile`

- `Q5-RSSI-004`
  file: `AirportItlwmSkywalkInterface.cpp::getRSSI`
  current: closed
  reference: BSS-manager current-RSSI contract
  status: `closed`

- `Q5-NOISE-005`
  file: `AirportItlwmSkywalkInterface.cpp::getNOISE`
  current: local `if RUN else 6` still diverges
  reference: BSS-manager current-noise contract
  status: `open_needs_decompile`

- `Q5-TXPOWER-006`
  file: `AirportItlwmSkywalkInterface.cpp::getTXPOWER`
  current: local ieee80211-state reconstruction, else `6`
  reference: `"qtxpower"` config query path
  status: `open_needs_decompile`

- `Q5-MCS-007`
  file: `AirportItlwmSkywalkInterface.cpp::getMCS`
  current: local reconstruction with raw `6` fallback
  reference: still needs exact Apple helper/body recovery
  status: `open_needs_decompile`

### 2. Large Tahoe Skywalk vtable surface still hardcoded to `kIOReturnUnsupported`

File:

- [AirportItlwmSkywalkInterface.hpp](/Users/bob/Projects/itlwm/AirportItlwm/AirportItlwmSkywalkInterface.hpp)

This is the largest remaining architectural gap. The header still contains a
wide unsupported surface across slots `[470]-[663]`.

High-risk because these are not all equivalent:

- some may be truly optional
- some are state carriers
- some are control-plane producers
- some may be safe to fail only with Apple-specific error codes, not generic
  `kIOReturnUnsupported`

Current examples:

- GET side:
  `getGUARD_INTERVAL`, `getHT_CAPABILITY`, `getVHT_CAPABILITY`,
  `getROAM_PROFILE`, `getHW_SUPPORTED_CHANNELS`, `getLQM_SUMMARY`,
  `getWCL_LOW_LATENCY_INFO`, `getRSN_XE`, `getHE_CAPABILITY`
- SET side:
  `setCHANNEL`, `setTXPOWER`, `setRATE`, `setROAM_PROFILE`,
  `setPRIVATE_MAC`, `setRESET_CHIP`, `setRANGING_*`, `setBTCOEX_*`,
  `setPM_MODE`, `setTIMESYNC_*`, `setWCL_WNM_*`, `setWOW_LOW_POWER_MODE`

Next required evidence:

- bucket each unsupported slot using
  `wifi_bundle_full_v3/34_skywalk_override_matrix.yaml`
- for each slot decide whether Apple behavior is:
  real producer, state carrier, safe-to-fail Apple error, or truly optional

Initial classification buckets for the next pass:

- `Q13-A Apple-specific fail likely`
  rationale: many family-side wrappers in `IO80211Family_decompiled.c` return
  `0xe082280e` directly for unsupported-but-known selectors, not generic
  `kIOReturnUnsupported`
  examples to classify first:
  `getGUARD_INTERVAL`, `getHT_CAPABILITY`, `getVHT_CAPABILITY`,
  `getROAM_PROFILE`, `getPRIVATE_MAC`, `getRSN_XE`

- `Q13-B likely real producer/state carrier`
  rationale: names imply runtime state/config ownership rather than optional
  decorative IOCTLs
  examples:
  `getLQM_SUMMARY`, `getWCL_LOW_LATENCY_INFO`, `setPM_MODE`,
  `setROAM_PROFILE`, `setTIMESYNC_*`, `setWCL_WNM_*`

- `Q13-C likely safe optional/feature-gated`
  rationale: feature families not yet present in our target path may legally
  fail, but still need Apple-specific failure shape confirmation
  examples:
  `getRANGING_*`, `setRANGING_*`, `getAWDL_*`, `getHE_CAPABILITY`,
  `setSENSING_*`

- `Q13-D explicit decompile required before touching`
  rationale: reset/crash/power-policy slots can alter global runtime behavior
  and must not be guessed
  examples:
  `setRESET_CHIP`, `setCRASH`, `setPOWER_BUDGET`, `setWOW_LOW_POWER_MODE`

Current census from the Tahoe header:

- `144` raw overrides still return `kIOReturnUnsupported`
- `114` of those still remain open unsupported discrepancies after the first
  confirmed Apple-unsupported classification batches and the lifted thermal /
  power-budget / guard-interval / HT-capability / private-mac / TCPKA getter
  batch
- `17` overrides still return success from inline ack-only placeholder bodies

Unsupported getter slots still present:

- `470 getAWDL_PEER_TRAFFIC_STATS`
- `480 getPOWER_DEBUG_INFO`
- `485 getROAM_PROFILE`
- `486 getCHIP_COUNTER_STATS`
- `487 getDBG_GUARD_TIME_PARAMS`
- `488 getLEAKY_AP_STATS_MODE`
- `489 getCOUNTRY_CHANNELS`
- `493 getAWDL_RSDB_CAPS`
- `494 getTKO_PARAMS`
- `495 getTKO_DUMP`
- `496 getHW_SUPPORTED_CHANNELS`
- `497 getBTCOEX_PROFILE`
- `498 getBTCOEX_PROFILE_ACTIVE`
- `499 getTRAP_INFO`
- `501 getMAX_NSS_FOR_AP`
- `502 getBTCOEX_2G_CHAIN_DISABLE`
- `506 getLQM_CONFIG`
- `507 getTRAP_CRASHTRACER_MINI_DUMP`
- `508 getBEACON_INFO`
- `509 getCHIP_POWER_RANGE`
- `512 getCHIP_DIAGS`
- `513 getHP2P_CTRL`
- `514 getBSS_BLACKLIST`
- `515 getTXRX_CHAIN_INFO`
- `516 getMIMO_STATUS`
- `517 getCUR_PMK`
- `518 getDYNSAR_DETAIL`
- `520 getLQM_SUMMARY`
- `521 getSLOW_WIFI_FEATURE_ENABLED`
- `522 getTIMESYNC_INFO`
- `523 getSENSING_DATA`
- `524 getWCL_FW_HOT_CHANNELS`
- `525 getWCL_LOW_LATENCY_INFO`
- `527 getWCL_TRAFFIC_COUNTERS`
- `528 getWCL_GET_TX_BLANKING_STATUS`
- `531 getRSN_XE`
- `532 getSIB_COEX_STATUS`
- `533 getWCL_EXTENDED_BSS_INFO`
- `534 getWCL_LOW_LATENCY_INFO_STATS`
- `537 getWIFI_NOISE_PER_ANT`
- `540 getSYSTEM_SLEEP_CONFIG`
- `543 getHE_CAPABILITY`
- `544 getP2P_DEVICE_CAPABILITY`

Unsupported setter slots still present:

- `546 setCHANNEL`
- `548 setTXPOWER`
- `549 setRATE`
- `550 setIBSS_MODE`
- `551 setAP_MODE`
- `552 setIE`
- `553 setWOW_TEST`
- `555 setVIRTUAL_IF_CREATE`
- `556 setHT_CAPABILITY`
- `557 setOFFLOAD_ARP`
- `558 setOFFLOAD_NDP`
- `559 setGAS_REQ`
- `560 setVHT_CAPABILITY`
- `562 setDBG_GUARD_TIME_PARAMS`
- `563 setLEAKY_AP_STATS_MODE`
- `564 setPRIVATE_MAC`
- `565 setRESET_CHIP`
- `566 setCRASH`
- `567 setRANGING_ENABLE`
- `568 setRANGING_START`
- `569 setRANGING_AUTHENTICATE`
- `570 setTKO_PARAMS`
- `571 setBTCOEX_PROFILE`
- `572 setBTCOEX_PROFILE_ACTIVE`
- `573 setTHERMAL_INDEX`
- `574 setBTCOEX_2G_CHAIN_DISABLE`
- `575 setPOWER_BUDGET`
- `576 setOFFLOAD_TCPKA_ENABLE`
- `577 setLQM_CONFIG`
- `578 setDYNAMIC_RSSI_WINDOW_CONFIG`
- `579 setUSB_HOST_NOTIFICATION`
- `580 setHP2P_CTRL`
- `581 setBSS_BLACKLIST`
- `582 setSET_PROPERTY`
- `584 setPM_MODE`
- `586 setREALTIME_QOS_MSCS`
- `587 setSENSING_ENABLE`
- `588 setSENSING_DISABLE`
- `606 setRSN_XE`
- `608 setWCL_ULOFDMA_STATE`
- `609 setWCL_ACTION_FRAME`
- `610 setGAS_ABORT`
- `614 setMIMO_CONFIG`
- `622 setBYPASS_TX_POWER_CAP`
- `623 setFACETIME_WIFICALLING_PARAMS`
- `625 setWCL_WNM_OPS`
- `626 setWCL_WNM_OFFLOAD`
- `627 setWCL_LIMITED_AGGREGATION`
- `628 setWCL_BCN_MUTE_CONFIG`
- `629 setEAP_FILTER_CONFIG`
- `631 setDUAL_POWER_MODE`
- `632 setWCL_UPDATE_FAST_LANE`
- `633 setWCL_ASSOCIATED_SLEEP`
- `634 setCONGESTION_CTRL_IND`
- `638 setLMTPC_CONFIG`
- `639 setTRAFFIC_ENG_PARAMS`
- `640 setLE_SCAN_PARAM`
- `642 setHOST_CLOCK_INFO`
- `647 setWCL_SOI_CONFIG`
- `649 setMWS_WIFI_TYPE_7_BITMAP_WIFI_ENH`
- `650 setMWS_COEX_BITMAP_WIFI_ENH`
- `651 setMWS_DISABLE_OCL_BITMAP_WIFI_ENH`
- `652 setMWS_RFEM_CONFIG_WIFI_ENH`
- `653 setMWS_ASSOC_PROTECTION_BITMAP_WIFI_ENH`
- `654 setMWS_SCAN_FREQ_WIFI_ENH`
- `655 setMWS_SCAN_FREQ_MODE_WIFI_ENH`
- `656 setMWS_CONDITION_ID_BITMAP_WIFI_ENH`
- `657 setMWS_ANTENNA_SELECTION_WIFI_ENH`
- `658 setNDD_REQ`
- `659 setDBRG_ENTROPY`
- `662 setOS_ELIGIBILITY`

Ack-only inline stubs still present in the Tahoe header:

- `590 setWCL_REASSOC`
- `591 setWCL_SET_ROAM_LOCK`
- `592 setWCL_LEGACY_ROAM_PROFILE_CONFIG`
- `593 setWCL_ROAM_PROFILE_CONFIG`
- `594 setWCL_ROAM_USER_CACHE`
- `596 setWCL_REAL_TIME_MODE`
- `597 setWCL_ARP_MODE`
- `598 setWCL_JOIN_ABORT`
- `602 setWCL_QOS_PARAMS`
- `603 setWCL_LINK_UP_DONE`
- `604 setWCL_SET_SCAN_HOME_AWAY_TIME`
- `615 setWCL_CONFIG_BG_MOTIONPROFILE`
- `616 setWCL_CONFIG_BG_NETWORK`
- `617 setWCL_CONFIG_BGSCAN`
- `618 setWCL_CONFIG_BG_PARAMS`
- `620 setHEARTBEAT`
- `621 setINTERFACE_SETTING`

### 3. Adapter-plane WCL cluster still not lifted from the reference producers

Still open from [tahoe_signal_chain_audit.md](/Users/bob/Projects/itlwm/docs/tahoe_signal_chain_audit.md):

- `setWCL_REASSOC`
- `setWCL_LEGACY_ROAM_PROFILE_CONFIG`
- `setWCL_ROAM_PROFILE_CONFIG`
- `setWCL_REAL_TIME_MODE`
- `setWCL_ARP_MODE`
- `setWCL_JOIN_ABORT`
- `setWCL_QOS_PARAMS`
- `setWCL_LINK_UP_DONE`
- `setWCL_CONFIG_BG_MOTIONPROFILE`
- `setWCL_CONFIG_BG_NETWORK`
- `setWCL_CONFIG_BGSCAN`
- `setWCL_CONFIG_BG_PARAMS`

These must not be converted into ack-only stubs. Apple delegates them into
roam/net/bgscan/join/power subsystems.

### 4. Ready-state producer target object is still only surface-matched

Current code publishes `CoreWiFiDriverReadyKey` through `fNetIf->setProperty`.

Apple evidence says the producer target is the hidden interface-side object at
core-state `+0x1510`, which services more than registry-like property writes:

- ready-state publication (`+0x9f8`)
- boot-state / analytics calls
- platform/ring property acquisition
- failure reporting callbacks

Why this remains open:

- we matched the visible key/value shape
- we have **not** yet recovered or recreated the full hidden object contract
- until that object is identified/modeled more precisely, ready-state remains
  only partially lifted

### 5. `getRATE`/`getTXPOWER` nvram-backed query contracts are not yet Apple-shaped

Apple decompile indicates:

- `getRATE` checks association and returns `0xe0822403` if not associated
- `getTXPOWER` queries `"qtxpower"` through the core config path and returns the
  underlying query status
- `getMCS_VHT` queries `"nrate"` through the same config path

Our current local implementations are local ieee80211-state reconstructions,
not the recovered Apple producer routes.

This does not automatically mean they are wrong in all cases, but it is a real
architectural discrepancy and needs a dedicated lift.

### 6. Legacy STA dispatcher still carries a shadow mismatch surface

Files:

- [AirportSTAIOCTL.cpp](/Users/bob/Projects/itlwm/AirportItlwm/AirportSTAIOCTL.cpp)

Even though Tahoe UI traffic now goes through the Skywalk path, the legacy STA
dispatcher remains part of the codebase contract and still contains unresolved
Apple mismatches.

Remaining raw-`6` legacy getters:

- `getPROTMODE`
- `getTXPOWER`
- `getMCS_INDEX_SET`
- `getNOISE`
- `getMCS`

Remaining legacy unsupported/gated surfaces needing classification:

- `getRSN_IE`
- `setRSN_IE`
- `getAP_IE_LIST`
- `setVIRTUAL_IF_CREATE`

These must stay in the inventory because they mirror producer semantics that
may still be reused, and they are a common source of regressions when Tahoe and
legacy paths drift apart.

## Open Needs Decompile

### 6. Full producer contract for the hidden interface-side object at `+0x1510`

Need:

- exact concrete class identity
- method map for the slots used by Apple core
- constructor/assignment site
- relationship between that object and our `fNetIf`

Without this, producer-side parity for ready-state, boot-state, analytics, and
several property/reporting flows remains incomplete.

### 7. IOSkywalk / IO80211 constructor path exactness on Tahoe

Need:

- real recovered body for any 2-argument Tahoe init path that the docs hinted at
- proof of when `this+0x128` becomes valid for `IO80211InfraInterface`
- exact constructor/start contract before `PeerManager::initWithInterface()`

The panic proved the previous conclusion was wrong, but the full Apple
constructor sequence is still not closed.

### 8. Full open-slot classification for the remaining unsupported Skywalk getters/setters

Need:

- decompile each unsupported slot or trace each to Apple override / inherited
  base behavior
- explicitly classify as:
  `must implement`, `must cache`, `must return Apple error`, or
  `safe to leave unsupported`

Until that classification exists, touching the remaining unsupported vtable
surface would be guesswork.

### 9. WCL ack-only stub cluster still needs producer lifting

The Tahoe header still contains `18` inline success stubs in the WCL and
adjacent control-plane cluster. They now have exact slot numbers and method
names captured above, but they still need one-by-one producer lifting before
they can be removed from the queue.

The highest-priority members remain:

- `setWCL_REASSOC`
- `setWCL_LEGACY_ROAM_PROFILE_CONFIG`
- `setWCL_ROAM_PROFILE_CONFIG`
- `setWCL_REAL_TIME_MODE`
- `setWCL_ARP_MODE`
- `setWCL_JOIN_ABORT`
- `setWCL_QOS_PARAMS`
- `setWCL_LINK_UP_DONE`
- `setWCL_CONFIG_BG_MOTIONPROFILE`
- `setWCL_CONFIG_BG_NETWORK`
- `setWCL_CONFIG_BGSCAN`
- `setWCL_CONFIG_BG_PARAMS`

## Next Execution Order

1. Finish the helper-level recovery for the raw-`6` getter cluster.
2. Bucket the unsupported Skywalk vtable surface into Apple contract classes.
3. Lift the next smallest confirmed batch from `open_confirmed`.
4. Re-run the same inventory after each batch so the queue stays honest.
