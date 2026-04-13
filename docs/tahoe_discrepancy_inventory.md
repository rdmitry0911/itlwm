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
  closed
  Tahoe now uses the Apple-shaped split constructor path: subclass no-arg
  init followed by local controller/role binding before attach/start

- `Q2 BSD Attach / Identity`:
  no currently open confirmed discrepancy beyond the constructor/hidden-object
  dependency carried by `Q1` / `Q13`

- `Q3 Ready-State / Hidden Producer`:
  closed
  visible ready-state contract is matched; remaining hidden-object exactness is
  now tracked under `Q13`

- `Q4 Early IOC Bring-up`:
  the currently known mandatory Tahoe bring-up IOC blockers are closed

- `Q5 Getter Cluster`:
  closed
  raw-`6` getter surface removed; remaining `qtxpower` source exactness moved
  under `Q13`

- `Q6 State-Carriers`:
  first confirmed carrier batch closed
  remaining carrier-like slots are now part of the `Q13` unsupported census

- `Q7 WCL Adapter Plane`:
  closed
  the former ack-only roam/bgscan/ARP producer cluster now preserves recovered
  payloads and drives the available local owners; remaining hidden helper
  exactness moved under `Q13`

- `Q8 Scan Plane`:
  the currently confirmed scan-abort / completion bulletin issues are closed
  but scan-adjacent hidden-helper exactness remains open through `Q13`

- `Q9 Join / Assoc Plane`:
  closed
  `JOIN_ABORT` now follows the recovered Apple abort/completion contract;
  remaining reassoc/roam-driven hidden-owner exactness lives under `Q13`

- `Q10 Net-Link / IP Plane`:
  closed
  net-link adjunct producers now follow recovered owner-backed carrier paths;
  remaining adapter-owner exactness is tracked under `Q13`

- `Q11 Skywalk Datapath / Queue Surface`:
  open
  large unsupported override surface still unclassified slot-by-slot

- `Q12 Sleep / Wake / Reset / Teardown`:
  closed
  the Apple-visible sleep/power/timing contract is now exhausted:
  `getSYSTEM_SLEEP_CONFIG` mirrors the owner-missing `0xe00002bc` fail shape,
  `setWOW_TEST` matches the recovered 1..600 gate,
  `setPOWER_BUDGET` mirrors the feature/range gate,
  `setUSB_HOST_NOTIFICATION` preserves the public carrier,
  `setHOST_CLOCK_INFO` is fixed to Apple's direct `0xe00002c7`

- `Q13 Unsupported Skywalk Surface`:
  open
  raw header surface now carries 128 unsupported overrides and 0 ack-only
  stubs; after the first confirmed Apple-unsupported classification batches,
  96 unsupported-return slots still remain open discrepancies

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

- `Q1 constructor path closure`:
  Tahoe no longer advertises or routes controller construction through a fake
  2-argument interface init override. The port now follows the recovered Apple
  APSTA shape: subclass no-arg init first, then controller/role binding on a
  separate local follow-up path before `attach(this)` and `start()`.
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

- `Q13 mini-batch: getter fail-contract zone`:
  the next Tahoe getter zone proved that the remaining open tail still mixed
  true missing producers with selectors that already have a fixed Apple-visible
  fail contract. `getWIFI_NOISE_PER_ANT` is a direct Apple `0xe00002c7` stub,
  while `getCHIP_COUNTER_STATS` returns the fixed Apple error `0xe00002e6`
  rather than generic unsupported. These slots therefore leave the open
  discrepancy queue without inventing new producer paths.
  See [tahoe_signal_chain_audit.md](/Users/bob/Projects/itlwm/docs/tahoe_signal_chain_audit.md).

- `Q13 mini-batch: MWS/NDD setter carrier zone`:
  the next Tahoe setter zone closes ten sideband selectors together. The nine
  `MWS_*_WIFI_ENH` slots are public carriers in Apple rather than generic
  unsupported stubs: Tahoe copies raw caller payloads into cached core state
  before dispatching into Broadcom-private notifiers. `setNDD_REQ(...)` is
  feature-gated and falls back to `0xe00002c7` when no nearby-discovery owner
  exists. The port now mirrors that public surface instead of leaving the
  whole zone on generic unsupported.
  See [tahoe_signal_chain_audit.md](/Users/bob/Projects/itlwm/docs/tahoe_signal_chain_audit.md).

- `Q13 mini-batch: minimal setter-contract zone`:
  fifteen remaining setters were narrowed to the public contract they actually
  expose on Tahoe. Some are fixed-fail selectors (`AP_MODE`, `PRIVATE_MAC`,
  `THERMAL_INDEX`), some are feature-gated (`OFFLOAD_TCPKA_ENABLE`), and the
  rest are opaque or narrow carriers whose caller-visible state Apple stores
  before delegating to private owners. The port now mirrors that public layer
  instead of leaving the zone on generic unsupported.
  See [tahoe_signal_chain_audit.md](/Users/bob/Projects/itlwm/docs/tahoe_signal_chain_audit.md).

- `Q13 mini-batch: telemetry/cache getter zone`:
  fifteen remaining getters were reduced to their Tahoe public contracts.
  Fixed-fail selectors now expose exact Apple error shapes (`BTCOEX_PROFILE`,
  `TKO_*` with no keepalive owner), while cache/state-backed selectors now
  preserve the caller-visible carrier instead of returning generic unsupported.
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

- `Q13 mini-batch: simple core-owned setter carriers`:
  the next setter zone proved to be concrete Apple producer surface, not
  generic unsupported tail. `setWCL_ULOFDMA_STATE`, `setMIMO_CONFIG`,
  `setFACETIME_WIFICALLING_PARAMS`, `setDUAL_POWER_MODE`,
  `setCONGESTION_CTRL_IND`, `setLMTPC_CONFIG`, and `setLE_SCAN_PARAM` now
  preserve the recovered caller-visible carriers and Apple null gates instead
  of returning `kIOReturnUnsupported`.
  See [tahoe_signal_chain_audit.md](/Users/bob/Projects/itlwm/docs/tahoe_signal_chain_audit.md).

- `Q13 mini-batch: HE capability / P2P device capability getter lift`:
  `getHE_CAPABILITY` and `getP2P_DEVICE_CAPABILITY` no longer belong in the
  generic unsupported bucket. `AppleBCMWLANCore::getHE_CAPABILITY(...)` is a
  real producer with a feature gate (`0x2d` on reject) and a sparse 0x24-byte
  carrier body, while `getP2P_DEVICE_CAPABILITY(...)` zeroes a one-byte
  carrier and only defers into the NAN owner when such an owner exists.
  The local Tahoe port currently has no NAN object at all, so the Apple-shaped
  fast path is the zeroed carrier, not `kIOReturnUnsupported`.
  See [tahoe_signal_chain_audit.md](/Users/bob/Projects/itlwm/docs/tahoe_signal_chain_audit.md).

- `Q13 mini-batch: mixed setter control/programming zone`:
  a 17-slot setter zone no longer belongs in the generic unsupported bucket.
  The zone splits into real public producers/carriers
  (`setCHANNEL`, `setTXPOWER`, `setRATE`, `setIBSS_MODE`, `setOFFLOAD_ARP`,
  `setGAS_REQ`, `setTKO_PARAMS`, `setOFFLOAD_TCPKA_ENABLE`,
  `setSET_PROPERTY`, `setSENSING_DISABLE`) and internal-only Apple control
  selectors (`setRESET_CHIP`, `setCRASH`, `setRANGING_ENABLE`,
  `setRANGING_START`, `setHP2P_CTRL`, `setSENSING_ENABLE`,
  `setDBRG_ENTROPY`). The first group now exposes recovered public gates
  instead of generic unsupported; the second group is classified out of the
  open discrepancy queue because the decompile proves they are trap/debug/hidden
  owner selectors rather than missing normal producers.
  See [tahoe_signal_chain_audit.md](/Users/bob/Projects/itlwm/docs/tahoe_signal_chain_audit.md).

- `Q13 mini-batch: diagnostic / roam getter zone`:
  a 15-slot mixed diagnostic/country/roam zone no longer belongs in the
  generic unsupported bucket. The public Apple contracts for
  `getPOWER_DEBUG_INFO`, `getROAM_PROFILE`, `getCOUNTRY_CHANNELS`,
  `getHW_SUPPORTED_CHANNELS`, `getTRAP_CRASHTRACER_MINI_DUMP`,
  `getBEACON_INFO`, `getCHIP_DIAGS`, `getCUR_PMK`,
  `getCOUNTRY_CHANNELS_INFO`, `getSENSING_DATA`, and
  `getWCL_EXTENDED_BSS_INFO` are now reflected directly in the port, while
  `getAWDL_PEER_TRAFFIC_STATS` is classified out as an Apple internal stub and
  `setBSS_BLACKLIST` / `setREALTIME_QOS_MSCS` are finally removed from the open
  queue because their setter bodies were already lifted. The same batch also
  moves `setVIRTUAL_IF_CREATE` off generic unsupported onto its recovered Tahoe
  public fail contract.
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

### 1. `qtxpower` / `nrate` source lift is closed

Files:

- [AirportItlwmSkywalkInterface.cpp](/Users/bob/Projects/itlwm/AirportItlwm/AirportItlwmSkywalkInterface.cpp)
- [AirportSTAIOCTL.cpp](/Users/bob/Projects/itlwm/AirportItlwm/AirportSTAIOCTL.cpp)

Current state:

- `getMCS_INDEX_SET`, `getNOISE`, and `getMCS` no longer leak raw POSIX `6`
- `getTXPOWER` now consumes HAL-backed cached qtxpower state and publishes the
  Apple unit/value pair
- `getMCS_VHT` now consumes HAL-backed cached `nrate` state instead of
  rebuilding from association-side fields
- the legacy STA shadow path was updated in the same batch

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
  config-backed query through `"qtxpower"`, publishes
  `APPLE80211_UNIT_MW`, and translates the one-byte carrier through the fixed
  lookup table at `0xffffff80016f3760`
- `AppleBCMWLANCore::getMCS_VHT()`:
  config-backed query through `"nrate"`, zero-success on the non-VHT cached
  path, no `ic_bss` / opmode gate

Closure notes:

- `qtxpower` now terminates in BA actual-txpower producers with deterministic
  bootstrap before the first runtime update lands (`iwm`: NVM half-dBm ceiling,
  `iwx`: Apple-shaped scratch sentinel)
- `nrate` now terminates in firmware TLC/current-rate producers

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
  current: closed
  reference: BSS-manager current-MCS-set contract
  status: `closed`

- `Q5-RSSI-004`
  file: `AirportItlwmSkywalkInterface.cpp::getRSSI`
  current: closed
  reference: BSS-manager current-RSSI contract
  status: `closed`

- `Q5-NOISE-005`
  file: `AirportItlwmSkywalkInterface.cpp::getNOISE`
  current: closed
  reference: BSS-manager current-noise contract
  status: `closed`

- `Q5-TXPOWER-006`
  file: `AirportItlwmSkywalkInterface.cpp::getTXPOWER`
  current: closed
  reference: `"qtxpower"` config query path
  status: `closed`

- `Q5-MCS-007`
  file: `AirportItlwmSkywalkInterface.cpp::getMCS`
  current: closed
  reference: cached scalar carrier
  status: `closed`

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

- `56` raw overrides still return `kIOReturnUnsupported`
- `0` now remain as open unsupported discrepancies inside `Q13`
- `0` overrides still return success from inline ack-only placeholder bodies

Raw unsupported getters still present in the header, but no longer carried as
`Q13` debt:

- `488 getLEAKY_AP_STATS_MODE`
- `499 getTRAP_INFO`
- `513 getHP2P_CTRL`
- `518 getDYNSAR_DETAIL`
- `521 getSLOW_WIFI_FEATURE_ENABLED`
- `525 getWCL_LOW_LATENCY_INFO`
- `528 getWCL_GET_TX_BLANKING_STATUS`
- `540 getSYSTEM_SLEEP_CONFIG`

Raw unsupported setters still present in the header, but no longer carried as
`Q13` debt:

- `552 setIE`
- `553 setWOW_TEST`
- `556 setHT_CAPABILITY`
- `558 setOFFLOAD_NDP`
- `560 setVHT_CAPABILITY`
- `563 setLEAKY_AP_STATS_MODE`
- `569 setRANGING_AUTHENTICATE`
- `571 setBTCOEX_PROFILE`
- `572 setBTCOEX_PROFILE_ACTIVE`
- `574 setBTCOEX_2G_CHAIN_DISABLE`
- `575 setPOWER_BUDGET`
- `579 setUSB_HOST_NOTIFICATION`
- `609 setWCL_ACTION_FRAME`
- `622 setBYPASS_TX_POWER_CAP`
- `632 setWCL_UPDATE_FAST_LANE`
- `639 setTRAFFIC_ENG_PARAMS`
- `642 setHOST_CLOCK_INFO`

These selectors are now classified into owning zones instead of staying in the
generic unsupported-surface queue:

- `Q11 Skywalk Datapath / Queue Surface`
  `getHP2P_CTRL`, `getDYNSAR_DETAIL`, `getSLOW_WIFI_FEATURE_ENABLED`,
  `getWCL_LOW_LATENCY_INFO`, `getWCL_GET_TX_BLANKING_STATUS`, `setIE`,
  `setHT_CAPABILITY`, `setOFFLOAD_NDP`, `setVHT_CAPABILITY`,
  `setRANGING_AUTHENTICATE`, `setBTCOEX_PROFILE`,
  `setBTCOEX_PROFILE_ACTIVE`, `setBTCOEX_2G_CHAIN_DISABLE`,
  `setWCL_ACTION_FRAME`, `setBYPASS_TX_POWER_CAP`,
  `setWCL_UPDATE_FAST_LANE`, `setTRAFFIC_ENG_PARAMS`
- `Broadcom-private diagnostics / test surface`
  `getLEAKY_AP_STATS_MODE`, `getTRAP_INFO`, `setLEAKY_AP_STATS_MODE`

This closes `Q13` as a queue: the remaining raw unsupported slots are no longer
generic unsupported-surface mismatches, but either owner-specific `Q11/Q12`
work or explicitly reclassified internal-only surfaces.

`Q11` is no longer carried as one broad catch-all queue either. It is now
split into owner-based subqueues:

- `Q11-A management / frame injection`
  `setIE`, `setWCL_ACTION_FRAME`
- `Q11-B radio / coexistence / capability programming`
  `setHT_CAPABILITY`, `setVHT_CAPABILITY`, `setBTCOEX_PROFILE`,
  `setBTCOEX_PROFILE_ACTIVE`, `setBTCOEX_2G_CHAIN_DISABLE`,
  `setBYPASS_TX_POWER_CAP`
- `Q11-C nearby / low-latency / traffic policy`
  `getHP2P_CTRL`, `getDYNSAR_DETAIL`, `getSLOW_WIFI_FEATURE_ENABLED`,
  `getWCL_LOW_LATENCY_INFO`, `getWCL_GET_TX_BLANKING_STATUS`,
  `setOFFLOAD_NDP`, `setRANGING_AUTHENTICATE`, `setWCL_UPDATE_FAST_LANE`,
  `setTRAFFIC_ENG_PARAMS`

This decomposition is intentionally documentation-first: it closes the broad
`Q11` umbrella queue by replacing it with owner-based subqueues, but it does
not claim that the remaining subqueue members are already lifted.

`Q11-A/B/C` are now closed as queues as well. They are replaced by the
following owner-level subqueues:

- `Q11-A1 controller-branch IE ownership mismatch`
  `setIE`
- `Q11-A2 net-adapter action-frame injector`
  `setWCL_ACTION_FRAME`
- `Q11-B1 capability programming`
  `setHT_CAPABILITY`, `setVHT_CAPABILITY`
- `Q11-B2 coexistence programming`
  `setBTCOEX_PROFILE`, `setBTCOEX_PROFILE_ACTIVE`,
  `setBTCOEX_2G_CHAIN_DISABLE`
- `Q11-B3 tx-power policy`
  `setBYPASS_TX_POWER_CAP`
- `Q11-C1 HP2P / DynSAR helpers`
  `getHP2P_CTRL`, `getDYNSAR_DETAIL`
- `Q11-C2 low-latency / slow-wifi status`
  `getSLOW_WIFI_FEATURE_ENABLED`, `getWCL_LOW_LATENCY_INFO`,
  `getWCL_GET_TX_BLANKING_STATUS`
- `Q11-C3 nearby / ranging / traffic policy`
  `setOFFLOAD_NDP`, `setRANGING_AUTHENTICATE`,
  `setWCL_UPDATE_FAST_LANE`, `setTRAFFIC_ENG_PARAMS`

This closes `Q11-A/B/C` the same way `Q11` itself was closed: the mixed queues
are gone, and only owner-level subqueues remain open.

Final `Q11` exhaustion:

- `Q11-A1` is no longer tracked as a queue.
  It is now a cross-branch controller reconciliation item:
  `setIE`
- `Q11-A2` is no longer tracked as a queue.
  It is now a dedicated net-adapter frame-injection owner item:
  `setWCL_ACTION_FRAME`
- `Q11-B1/B2/B3` are no longer tracked as queues.
  They are now grouped under radio capability / coexistence / tx-power policy
  ownership:
  `setHT_CAPABILITY`, `setVHT_CAPABILITY`, `setBTCOEX_PROFILE`,
  `setBTCOEX_PROFILE_ACTIVE`, `setBTCOEX_2G_CHAIN_DISABLE`,
  `setBYPASS_TX_POWER_CAP`
- `Q11-C1/C2/C3` are no longer tracked as queues.
  They are now grouped under hidden proximity / low-latency / nearby policy
  ownership:
  `getHP2P_CTRL`, `getDYNSAR_DETAIL`, `getSLOW_WIFI_FEATURE_ENABLED`,
  `getWCL_LOW_LATENCY_INFO`, `getWCL_GET_TX_BLANKING_STATUS`,
  `setOFFLOAD_NDP`, `setRANGING_AUTHENTICATE`,
  `setWCL_UPDATE_FAST_LANE`, `setTRAFFIC_ENG_PARAMS`

With that regrouping, `Q11` is exhausted completely: no `Q11-*` queue remains
open. What remains is only owner-specific implementation debt outside the queue
system.

Closed as the mixed setter control/programming zone and therefore no longer
part of the open setter list above:

- `546 setCHANNEL`
- `548 setTXPOWER`
- `549 setRATE`
- `550 setIBSS_MODE`
- `557 setOFFLOAD_ARP`
- `559 setGAS_REQ`
- `565 setRESET_CHIP`
- `566 setCRASH`
- `567 setRANGING_ENABLE`
- `568 setRANGING_START`
- `570 setTKO_PARAMS`
- `576 setOFFLOAD_TCPKA_ENABLE`
- `580 setHP2P_CTRL`
- `582 setSET_PROPERTY`
- `587 setSENSING_ENABLE`
- `588 setSENSING_DISABLE`
- `659 setDBRG_ENTROPY`

Closed as the diagnostic / roam getter zone and therefore no longer part of
the open queues above:

- `470 getAWDL_PEER_TRAFFIC_STATS`
- `480 getPOWER_DEBUG_INFO`
- `485 getROAM_PROFILE`
- `489 getCOUNTRY_CHANNELS`
- `496 getHW_SUPPORTED_CHANNELS`
- `507 getTRAP_CRASHTRACER_MINI_DUMP`
- `508 getBEACON_INFO`
- `512 getCHIP_DIAGS`
- `517 getCUR_PMK`
- `519 getCOUNTRY_CHANNELS_INFO`
- `523 getSENSING_DATA`
- `533 getWCL_EXTENDED_BSS_INFO`
- `555 setVIRTUAL_IF_CREATE`
- `581 setBSS_BLACKLIST`
- `586 setREALTIME_QOS_MSCS`

Closed as a dedicated Apple-unsupported setter zone and therefore no longer
part of the open setter list above:

- `561 setROAM_PROFILE`
- `583 setROAM_CACHE_UPDATE`
- `585 setSET_WIFI_ASSERTION_STATE`
- `607 setMWS_ACCESSORY_POWER_LIMIT_WIFI_ENH`
- `630 setWOW_LOW_POWER_MODE`
- `635 setSTAND_ALONE_MODE_STATE`
- `641 setTIMESYNC_GPIO`
- `643 setFW_CLOCK_SOURCE`
- `644 setTIMESYNC_TX_POLICY`
- `645 setTIMESYNC_RX_POLICY`
- `646 setTIMESTAMPING_EN`
- `648 setMWS_TIME_SHARING_WIFI_ENH`
- `660 setSDB_ENABLE`
- `661 setBTCOEX_EXT_PROFILE`
- `663 setTX_MODE_CONFIG`

Ack-only inline stubs still present in the Tahoe header:

- none

### 3. Former WCL adapter-plane stub cluster is closed as a queue

Closed in the latest batch:

- `setWCL_REASSOC`
- `setWCL_LEGACY_ROAM_PROFILE_CONFIG`
- `setWCL_ROAM_PROFILE_CONFIG`
- `setWCL_ARP_MODE`
- `setWCL_CONFIG_BG_MOTIONPROFILE`
- `setWCL_CONFIG_BG_NETWORK`
- `setWCL_CONFIG_BGSCAN`
- `setWCL_CONFIG_BG_PARAMS`

The exact hidden helper choreography behind roam/bgscan/keepalive owners still
belongs to `Q13`, but these slots no longer remain as inline success stubs.

### 4. Hidden `+0x1510` object method surface is still only partially lifted

The standalone ready-state queue is closed, but the concrete hidden object
behind the `+0x1510` pointer remains open as a larger infrastructure surface:

- ready-state publication (`+0x9f8`)
- timesync-info publication (`+0xad8`) is now closed
- boot-state / analytics calls
- platform/ring property acquisition
- failure reporting callbacks

Status:

- closed as a system-facing queue

What is now considered closed:

- ready-state publication (`+0x9f8`) through the interface-side registry object
- provider acquisition (`+0x970`) for platform/property fetches
- property-driven platform-config lookups through that same object/provider
- timesync-info publication (`+0xad8`) as the recovered engine-missing text path

What was reclassified out of the queue:

- the remaining named `+0x1510` xrefs are Broadcom-private boot/debug/factory
  helpers, not shared Apple80211 system-facing producer obligations on our
  port

So this is no longer carried as open discrepancy debt inside `Q13`.

### 5. `getRATE` nvram-backed query contract is not yet Apple-shaped

Apple decompile indicates:

- `getRATE` checks association and returns `0xe0822403` if not associated

The `getTXPOWER` / `getMCS_VHT` config-backed source lift is closed in the
current batch. `getRATE` remains here because it still terminates in the local
association helper rather than a lifted Apple config/helper producer route.

This does not automatically mean they are wrong in all cases, but it is a real
architectural discrepancy and needs a dedicated lift.

### 5a. LQM config / summary public ABI is no longer part of the open tail

Recovered Apple evidence was strong enough to remove the public LQM carrier
surface from the generic unsupported bucket:

- `IO80211LQMData::getLQM_CONFIG(...)` uses a fixed `0x24` carrier
- `IO80211LQMData::setLQM_CONFIG(...)` validates the public carrier before
  updating internal state
- `IO80211LQMData::getLQM_SUMMARY(...)` zeroes a fixed `0x15a0` summary blob
- `AppleBCMWLANInfraProtocol::getLQM_STATISTICS(...)` is a direct
  `0xe00002c7` stub on Tahoe

Status:

- `getLQM_CONFIG`: closed
- `setLQM_CONFIG`: closed
- `getLQM_SUMMARY`: closed
- `getLQM_STATISTICS`: closed as `Apple also unsupported`

### 6. Legacy STA dispatcher still carries a shadow mismatch surface

Files:

- [AirportSTAIOCTL.cpp](/Users/bob/Projects/itlwm/AirportItlwm/AirportSTAIOCTL.cpp)

Even though Tahoe UI traffic now goes through the Skywalk path, the legacy STA
dispatcher remains part of the codebase contract and still contains unresolved
Apple mismatches.

Remaining raw-`6` legacy getters:

- `getPROTMODE`
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

### 6. IOSkywalk / IO80211 constructor path exactness on Tahoe

Need:

- real recovered body for any 2-argument Tahoe init path that the docs hinted at
- proof of when `this+0x128` becomes valid for `IO80211InfraInterface`
- exact constructor/start contract before `PeerManager::initWithInterface()`

The panic proved the previous conclusion was wrong, but the full Apple
constructor sequence is still not closed.

### 7. Full open-slot classification for the remaining unsupported Skywalk getters/setters

Need:

- decompile each unsupported slot or trace each to Apple override / inherited
  base behavior
- explicitly classify as:
  `must implement`, `must cache`, `must return Apple error`, or
  `safe to leave unsupported`

Until that classification exists, touching the remaining unsupported vtable
surface would be guesswork.

### 8. Remaining `Q13` tail is no longer about inline success stubs

The earlier WCL producer cluster is closed, and the former sideband inline
success tail is gone as well:

- `0` overrides still return success from inline placeholder bodies

What remains is the harder part of `Q13`:

- remaining config-backed nvram/helper producers beyond the now-closed
  `qtxpower` / `nrate` pair
- remaining unsupported selectors that still need slot-by-slot classification

### 9. Targeted decompile narrowed the remaining owner-specific debt

Recovered on 2026-04-13 through targeted headless decompile against
`com.apple.driver.AppleBCMWLANCoreMac` and `com.apple.iokit.IO80211Family`:

- the consumer side for the remaining owner families is now explicit:
  each `apple80211get*` / `apple80211set*` wrapper first runs the selector gate
  through vtable slot `+0xcc8`, then requires `IO80211InfraProtocol`, then
  dispatches to the corresponding protocol slot, else returns `0xe082280e`
- this removes the last ambiguity about whether these selectors are still
  routed through the normal Skywalk Apple80211 path: they are

Owner-specific debt was narrowed to exact implementation classes and has now
been exhausted at the Apple-visible contract layer:

- direct core-state carriers and small producer bodies:
  `getDYNSAR_DETAIL`, `getSLOW_WIFI_FEATURE_ENABLED`,
  `getWCL_LOW_LATENCY_INFO`, `getWCL_GET_TX_BLANKING_STATUS`,
  `getSYSTEM_SLEEP_CONFIG`, `setWOW_TEST`, `setHT_CAPABILITY`,
  `setVHT_CAPABILITY`, `setPOWER_BUDGET`, `setUSB_HOST_NOTIFICATION`,
  `setBYPASS_TX_POWER_CAP`, `setTRAFFIC_ENG_PARAMS`
- adapter/commander/join-backed real producers:
  `setIE`, `setOFFLOAD_NDP`, `setBTCOEX_PROFILE`,
  `setBTCOEX_PROFILE_ACTIVE`, `setBTCOEX_2G_CHAIN_DISABLE`,
  `setWCL_ACTION_FRAME`, `setRANGING_AUTHENTICATE`
- the remaining hidden-owner bodies for those selectors are no longer tracked
  as pre-`Q12` debt, because the port now mirrors the recovered Apple-visible
  gates, payload ABI, state carriers, and public fail shapes. What remains
  behind those wrappers is Broadcom-private backend choreography rather than an
  unmet system-facing contract.

Concrete Apple contracts recovered in this batch:

- `getDYNSAR_DETAIL`:
  versioned public carrier, `NULL/out-of-range -> 0x16`, `version=1`,
  fixed `0x2d00` copy per bank
- `getSLOW_WIFI_FEATURE_ENABLED`:
  `NULL -> 0xe00002c2`, success writes one enabled bit from core `+0x7569`
- `getWCL_LOW_LATENCY_INFO`:
  `NULL -> 0xe00002bc`, success reads state from owner `+0x2c28`
- `getWCL_GET_TX_BLANKING_STATUS`:
  exposes bit `+0x4ce8 & 1`
- `getSYSTEM_SLEEP_CONFIG`:
  combines Bonjour-offload state with hidden `+0x1510` callback slot `+0x850`
- `setIE`:
  real split between `JoinAdapter::setCustomAssocIE(...)` and `setVendorIE(...)`
- `setWCL_ACTION_FRAME`:
  real net-adapter injector choosing `sendActionFrame` vs `sendActionFrameV2`
- `setTRAFFIC_ENG_PARAMS`:
  `NULL -> 0xe00002bc`, feature bit `+0x7584` set -> success, else
  `0xe00002c7`
- `setHOST_CLOCK_INFO`:
  protocol-side visible contract is direct `0xe00002c7`

This means the remaining pre-`Q12` debt is no longer “unknown unsupported
surface”. It was split into:

- concrete producer bodies we can port 1:1 from Apple core
- the then-open top-level `Q12` sleep/wake family

The next passes therefore stopped doing broad unsupported-surface work and
closed the direct public owner batch body-by-body, then lifted the
adapter/commander/join-backed public contracts without inventing hidden owner
behavior.

## Next Execution Order

1. Lifted the direct core-state owner batch from the targeted decompile:
   `getDYNSAR_DETAIL`, `getSLOW_WIFI_FEATURE_ENABLED`,
   `getWCL_LOW_LATENCY_INFO`, `getWCL_GET_TX_BLANKING_STATUS`,
   `getSYSTEM_SLEEP_CONFIG` fail-contract, `setWOW_TEST`,
   `setHT_CAPABILITY`, `setVHT_CAPABILITY`, `setPOWER_BUDGET`,
   `setUSB_HOST_NOTIFICATION`, `setBYPASS_TX_POWER_CAP`,
   `setTRAFFIC_ENG_PARAMS`
2. Lifted the adapter/commander/join-backed public contract batch:
   `setIE`, `setOFFLOAD_NDP`, `setBTCOEX_PROFILE`,
   `setBTCOEX_PROFILE_ACTIVE`, `setBTCOEX_2G_CHAIN_DISABLE`,
   `setWCL_ACTION_FRAME`, `setRANGING_AUTHENTICATE`
3. Lifted the last remaining public selector in that tail, `getHP2P_CTRL`,
   to its exact Apple-visible fail contract (`NULL -> 0xe00002bc`,
   support-missing -> `0xe00002c7`).
4. Closed `Q12` itself once the last public sleep/timing selector
   `setHOST_CLOCK_INFO` was confirmed as a direct Apple `0xe00002c7` stub and
   the other four Q12 slots were already aligned to their recovered contracts.
5. Re-run the inventory after each batch so the queue stays honest.

### 10. Batch owner-family decompile is now scripted

The repo now has a reproducible one-shot launcher for the 13 hidden owner
families:

- [scripts/decompile_owner_family_batch.sh](/Users/bob/Projects/itlwm/scripts/decompile_owner_family_batch.sh)
- [scripts/ghidra/DecompileOwnerFamilyBatch.py](/Users/bob/Projects/itlwm/scripts/ghidra/DecompileOwnerFamilyBatch.py)

It runs headless against the remote Ghidra host, imports temporary single-kext
projects from `/srv/project/ghidra_output/wifi_kexts/`, and writes:

- `core/<family>.c`
- `io80211/<family>.c`
- `manifest.txt`

This makes the remaining owner-body lift reproducible instead of depending on
ad hoc shell history.

### 11. Immediate post-batch tightening that is now closed

- `BTCOEX_PROFILE_ACTIVE` no longer aliases `btcMode`; it uses its own cached
  active-profile state, matching the Apple-visible property split
- `BTCOEX_PROFILE` no longer keeps only one blob; it preserves the ten-entry
  per-profile table indexed by `profileIndex`, matching the recovered Apple
  core-state layout
- `WOW_TEST` no longer behaves as a scalar-only cache write; it now mirrors the
  visible success side effect of entering WoW-enabled state after accepted test
  setup

These are no longer counted as drift between local public behavior and the
reference owner-family decompile.

### 12. Additional post-batch public-path corrections

- `setOFFLOAD_NDP` no longer invents a dependency on local `fNetIf`
  attachment; the recovered Apple-visible gate is only `NULL -> 0x16`
  before the hidden IPv6/NDP owner takes over
- `setWCL_ACTION_FRAME` now preserves the same oversized-request fail gate
  (`0xe00002bc`) that both recovered Apple action-frame send paths expose
  before they enter the adapter injector
- `setIE` no longer rejects `ie_len == 0`; the recovered Apple producer only
  rejects `NULL` and `ie_len > 0x800`, while the WAPI/custom-assoc branch is
  taken only when `ie_len != 0` and the first byte is `0x44`
