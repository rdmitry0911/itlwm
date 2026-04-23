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
  closed
  queue hierarchy exhausted earlier; only owner/runtime parity remained after
  that and is no longer tracked as queue debt

- `Q12 Sleep / Wake / Reset / Teardown`:
  closed
  the Apple-visible sleep/power/timing contract is now exhausted:
  `getSYSTEM_SLEEP_CONFIG` mirrors the owner-missing `0xe00002bc` fail shape,
  `setWOW_TEST` matches the recovered 1..600 gate,
  `setPOWER_BUDGET` mirrors the feature/range gate,
  `setUSB_HOST_NOTIFICATION` preserves the public carrier,
  `setHOST_CLOCK_INFO` is fixed to Apple's direct `0xe00002c7`

- `Q13 Unsupported Skywalk Surface`:
  closed
  remaining pre-`Q12` owner-family selectors now enter a centralized
  owner-targeted commander layer rather than per-selector ad hoc shims

## Closed

- `Tahoe primary interface exposure gap`:
  live build `0707196` proves the active external bootstrap getters do not
  consume the already-fixed helper bodies through the expected controller /
  peer-manager primary-interface sources. The earlier
  "only `getPrimarySkywalkInterface()` matters" conclusion was incomplete.
  The family decompile does route one common bootstrap getter plane through
  controller slot `+0xc80` (`getPrimarySkywalkInterface()`), then reads
  controller cache `+0x188` and peer-manager refs `+0x550/+0x558`. That is
  one real Tahoe bootstrap exposure gap for
  `SSID/BSSID/CHANNEL/VIRTUAL_IF_ROLE/VIRTUAL_IF_PARENT`.
  Later decomp/runtime work proved Tahoe public selector fallback also still
  uses interface slot `[411] isCommandProhibited(int)` with the same request
  numbers after IOUC/WCL miss, so both the primary-interface seam and the
  public request gate matter on 26.x.

- `Tahoe driver-available producer contract`:
  live build `2820901` showed that `CoreWiFiDriverReadyKey = "true"` in
  `ioreg` is not enough to make Tahoe external consumers treat the interface as
  available. The new decompile of
  `WCLSystemStateManager::driverAvailableEventHandler(...)` proves the family
  accepts `APPLE80211_M_DRIVER_AVAILABLE (0x37)` only with the exact `0xf8`
  consumer ABI, but `AppleBCMWLANCoreMac` also disproves the earlier producer
  reconstruction: Apple does not fabricate that bulletin directly from
  `signalDriverReady()`. The recovered caller order is hidden
  `setInterfaceEnable(true)` first, then `signalDriverReady()` on the up path,
  and hidden `interfaceAdvisoryEnable(...)` first, then `signalDriverReady()`
  on the down/error path. The local drift was therefore the missing hidden
  interface lifecycle edge, not merely "missing bulletin". Live reboot on
  `d2953c9` then narrowed this further: the local port did call
  `setInterfaceEnable(true)`, but still left `isDriverAvailable=0`, which
  proves the missing piece is the recovered subclass body behind the hidden
  slot: base `setInterfaceEnable(bool)`, then `reportLinkStatus(3, 0x80)`, then
  `setLinkState(kIO80211NetworkLinkUp, 1, false, 0, 0)`. Live build `43bf34f`
  proves one final producer-side gap remains after that subclass lift:
  current-boot logs contain no `APPLE80211_M_DRIVER_AVAILABLE` posts at all.
  The family-side consumer still requires the exact `0x37` / `0xf8` bulletin,
  so the local ready path must reproduce that PostOffice-visible edge in
  addition to the hidden interface-enable body and `CoreWiFiDriverReadyKey`.

- `Tahoe bootstrap POWER contract`:
  live build `5da9d59` showed early Tahoe `APPLE80211_IOC_POWER` traffic
  issuing a transient `req=0`, then returning to `1`. The local port executed
  that request immediately through `handlePowerStateChange(...)`, which drove
  `disableAdapter(...)`, emitted `DRIVER_UNAVAILABLE`, and forced
  `WCLSystemStateManager` back into deferred state. Recovered Apple
  `setPOWER(...)` does not do that: it caches the request into `+0x289c` and
  sets sticky bit `0x1000`, while `setupDriver()` later consumes the cached
  request. The Tahoe local path must therefore defer bootstrap `setPOWER(...)`
  instead of treating it as an immediate OFF edge.

- `Tahoe POWER_CHANGED producer scope`:
  live build `36e4cc3` proved that fixing bootstrap `setPOWER(...)` alone is
  not enough. The local port still posted `APPLE80211_M_POWER_CHANGED` from
  `start()` and from no-op `handlePowerStateChange(cur=1, req=1)`, which fed
  false `SSM_EVENT_SYSTEM_POWER_OFF/ON` edges into WCL before availability
  settled. Reverse event maps mark `POWER_CHANGED` as mandatory only for real
  `setPowerState transitions`. The Tahoe local path must therefore stop posting
  it on bootstrap and on no-op requests.

- `Tahoe driver-available payload polarity`:
  live build `eea599b` proves the restored `APPLE80211_M_DRIVER_AVAILABLE`
  bulletin still feeds the wrong SSM edge. The local port currently logs
  `postTahoeDriverAvailableBulletin ready=1 ... available=0`, but recovered
  `WCLSystemStateManager::driverAvailableEventHandler(...)` sends
  `*(payload + 8) == 0` into `processEvent(..., 4, ...)`, and the recovered SSM
  matrix defines `4 = DRIVER_UNAVAILABLE`, `5 = DRIVER_AVAILABLE`. So the local
  producer inverted the Tahoe availability polarity. The ready edge must carry
  a non-zero dword at payload `+0x8`; zero is the unavailable edge.

- `pre-Q12 owner-family backend batch`:
  `setIE`, `setOFFLOAD_NDP`, `setBTCOEX_PROFILE`,
  `setWCL_ACTION_FRAME`, and `setRANGING_AUTHENTICATE` no longer preserve
  Apple-visible state through one-off local branches. They now route through
  `TahoeCommander` with `TahoeOwnerRegistry` state blocks and
  `TahoePayloadBuilders`, which matches the recovered Apple owner-family shape
  much more closely and removes the remaining pre-`Q12` implementation debt
  outside the queue system.

- `Commander V2 first engineering batch`:
  the internal layer is now split into router/base/owner components, and the
  first four owner families (`USB_HOST_NOTIFICATION`,
  `BTCOEX_PROFILE_ACTIVE`, `BTCOEX_2G_CHAIN_DISABLE`,
  `BYPASS_TX_POWER_CAP`) no longer live as monolithic commander branches.
  Remaining families still need to move into dedicated owners, but the
  implementation boundary is now stable.

- `Commander V2 transport/completion batch`:
  the remaining pre-`Q12` families now also live behind owner classes and
  `TahoeCommanderV2` carries explicit transport/completion state rather than
  only state-carrier shims. What remains after this batch is runtime proof on a
  live system, not another missing architectural split in the local backend.

- `Tahoe external bootstrap query cache contract`:
  live build `db546d2` proves that the still-active blocker is the external
  bootstrap query surface used by `airportd _initInterface`: after
  `ifCount[1]` and `Apple80211BindToInterfaceWithService ... useIOUCWhenPossible TRUE`,
  the next external `APPLE80211_IOC_SSID` still returns `0xe0822403` and
  aborts `_initInterface` with `Failed to query current SSID`. The reverse docs
  already require third-party drivers to expose success+zeroed data for
  `SSID/BSSID/CHANNEL` pre-association, because Apple's framework cache layer
  absorbs the low-level `0xe0822403` internally. The local Tahoe BSD bridge
  must therefore route those three selectors to the local zero-success helpers
  instead of leaking the low-level failure to `airportd`.

- `PLATFORM_CONFIG`:
  Tahoe 7-byte packed producer path recovered and implemented.
  See [tahoe_platform_config_root_cause.md](/Users/bob/Projects/itlwm/docs/tahoe_platform_config_root_cause.md).

- `POWERSAVE`:
  unsupported path replaced with cached level contract.
  See [tahoe_platform_config_root_cause.md](/Users/bob/Projects/itlwm/docs/tahoe_platform_config_root_cause.md).

- `payload-less VIRTUAL_IF_ROLE/PARENT`:
  no longer allowed to fall into raw POSIX `6`.
  Live build `5cb2a53` proved the real remaining source was the controller
  dispatcher `AirportItlwm::apple80211Request(...)`: Tahoe external requests
  still missed explicit cases for IOCTLs `96/97`, so the already-fixed
  interface-side bridge never saw the request.
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

## New Open Runtime Root Cause After `471f6f1`

- queue: runtime post-closeout
- status: open_confirmed
- producer: `IO80211SkywalkInterface::processBSDCommand`
- consumer: external `airportd` / Apple80211 BSD IOCTL callers
- reference behavior:
  Apple routes external `SSID`, `CHANNEL`, `BSSID`, and `SCAN_RESULT` through
  `sendIOUCToWcl(...)` first, then falls back to the protocol path only when
  the WCL route reports "not implemented"
- current behavior:
  local Tahoe bridge handles those selectors directly inside
  `AirportItlwmSkywalkInterface::processApple80211Ioctl()`, bypassing the
  Skywalk/WCL route
- evidence:
  `IO80211Family_decompiled.c` shows selectors `1`, `4`, `9`, `0x16`
  dispatched via `sendIOUCToWcl`
  live `471f6f1` still shows external `SSID/BSSID -> 0xe0822403` and
  external `SCAN_RESULT -> 5`
- files:
  `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
- next action:
  stop intercepting those selectors locally and let `super::processBSDCommand()`
  keep the Apple IOUC-first route alive
- close condition:
  local Tahoe BSD bridge no longer short-circuits selectors `SSID`, `CHANNEL`,
  `BSSID`, `SCAN_RESULT`
  before the hidden IPv6/NDP owner takes over
- later exact-runtime note:
  this root stayed valid for the external getter surface, but the exact
  seven-file probe runtime narrowed the deeper live association blocker
  further: the active family `setASSOCIATE` plane does not enter the local
  Tahoe bridge or local `setWCL_ASSOCIATE(...)` path at all
- `setWCL_ACTION_FRAME` now preserves the same oversized-request fail gate
  (`0xe00002bc`) that both recovered Apple action-frame send paths expose
  before they enter the adapter injector
- `setIE` no longer rejects `ie_len == 0`; the recovered Apple producer only
  rejects `NULL` and `ie_len > 0x800`, while the WAPI/custom-assoc branch is
  taken only when `ie_len != 0` and the first byte is `0x44`

## New Open Runtime Root Cause After Exact Seven-File Probe Runtime

- queue: runtime post-closeout
- status: open_confirmed
- narrowed anomaly: `TAHOE-HIDDEN-ASSOC-CARRIER-004`
- producer: hidden Tahoe family `setASSOCIATE` plane
- consumer: `airportd` / CoreWiFi association lifecycle
- reference behavior:
  Apple Tahoe association is owned by a controller/core path:
  `AppleBCMWLANCore::setWCL_ASSOCIATE(...)` prepares auth context and SSID,
  then delegates into `AppleBCMWLANJoinAdapter::performJoin(...)`; the same
  join owner later controls abort and connect-complete publication
- current behavior:
  the local port implements association only on the interface/net80211 side in
  `AirportItlwmSkywalkInterface::setASSOCIATE(...)` and
  `AirportItlwmSkywalkInterface::setWCL_ASSOCIATE(...)`, while the live family
  `setASSOCIATE` path exits hidden with `-536870201` before either local path
  is entered; primary-source family decomp now narrows that active carrier to
  hidden selector `0x45` with payload `0x3ad8`
- evidence:
  live `2026-04-19 15:20:18` shows only:
  - `(IO80211Family) Exit-setASSOCIATE:153 ret:-536870201`
  - `leaveNetworkCommand@2345 ... <setDISASSOCIATE> ... reason=<8>`
  - `leaveNetworkCommand@2345 ... <setDISASSOCIATE> ... reason=<10>`
  - no local hits for `processBSDCommand(...)`,
    `processApple80211Ioctl(...)`, `setASSOCIATE(...)`,
    `setWCL_ASSOCIATE(...)`, `setAUTH_TYPE(...)`, `setRSN_IE(...)`,
    `associateSSID(...)`
  remote primary-source decomp confirms:
  - `AppleBCMWLANCore::setWCL_ASSOCIATE(apple80211AssocCandidates*) @ 0xffffff80015fbacc`
  - `AppleBCMWLANJoinAdapter::performJoin(...) @ 0xffffff8001576df8`
  - `AppleBCMWLANJoinAdapter::sendConnectComplete() @ 0xffffff800157d472`
  - `IO80211Family::FUN_ffffff80022080ef @ 0xffffff80022080ef`
    validates `req_len == 0x3ad8` and sends hidden selector `0x45` through
    `sendIOUCToWcl(...)`
  - fallback helper `FUN_ffffff80021e82ef @ 0xffffff80021e82ef` does not call
    public `setWCL_ASSOCIATE(...)`; it only consults interface slot `+0xcc8`
  - `IO80211SkywalkInterface` slot `+0xcc8` / `[411]` is
    `isCommandProhibited(int)`
- files:
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  - `AirportItlwm/AirportItlwmV2.cpp`
  - `docs/tahoe_signal_chain_audit.md`
  - `analysis/ANALYSIS_REPORT_2026-04-19.md`
- next action:
  `TAHOE-GETCONTROLLER-BINDING-003` is now runtime-confirmed enough to cross
  the visibility gate: the exact `CR-029` runtime boots, networks are visible,
  and `wdutil` shows non-zero scan cache on `en0`. The next action is therefore
  no longer proving slot `[375]`; it is recovering the exact hidden Tahoe
  association carrier `0x45/0x3ad8` and its completion/cache bridge above the
  controller/core join owner, because manual association with `hasPassword=1`
  still fails at `2026-04-19 17:40:24 EEST` with `-536870201`, then
  disassociates with reasons `8` and `10`, ends in policy `INVALID_AKMS`, and
  still never enters the public local `setWCL_ASSOCIATE(...)` owner.
- close condition:
  the live `ASSOC` window no longer exits only through hidden
  `Exit-setASSOCIATE:153 ret:-536870201`, and the active association plane
  reaches either:
  - an equivalent local owner attached to the family-visible path, or
  - the recovered Apple controller/core join-owner semantics 1:1

### 13. First internal Tahoe commander batch

The repo now has the minimal internal compatibility stack needed to stop
spreading hidden-owner state through ad hoc Skywalk caches:

- [AirportItlwm/TahoeErrorMap.hpp](/Users/bob/Projects/itlwm/AirportItlwm/TahoeErrorMap.hpp)
- [AirportItlwm/TahoeOwnerRegistry.hpp](/Users/bob/Projects/itlwm/AirportItlwm/TahoeOwnerRegistry.hpp)
- [AirportItlwm/TahoePayloadBuilders.hpp](/Users/bob/Projects/itlwm/AirportItlwm/TahoePayloadBuilders.hpp)
- [AirportItlwm/TahoeAsyncCommandContext.hpp](/Users/bob/Projects/itlwm/AirportItlwm/TahoeAsyncCommandContext.hpp)
- [AirportItlwm/TahoeCommander.hpp](/Users/bob/Projects/itlwm/AirportItlwm/TahoeCommander.hpp)

The first selectors moved onto that layer are:

- `setUSB_HOST_NOTIFICATION`
- `setBTCOEX_PROFILE_ACTIVE`
- `setBTCOEX_2G_CHAIN_DISABLE`
- `setBYPASS_TX_POWER_CAP`

`setDUAL_POWER_MODE` now also synchronizes the new owner registry so the
tx-power-cap bypass path can use the same Apple-visible dual-power gate as the
reference producer.

### 14. Tahoe capability carrier ABI mismatch inside the hidden assoc queue

- queue: runtime post-closeout
- status: open_confirmed_with_fix_candidate
- narrowed anomaly: `TAHOE-CARD-CAP-ABI-MISMATCH-005`
- producer: `AirportItlwm::getCARD_CAPABILITIES(...)`
- consumer: hidden Tahoe association / WCL capability readers
- reference behavior:
  Apple Tahoe `AppleBCMWLANCore::getCARD_CAPABILITIES(...)` writes the public
  capability blob through byte offset `+0x17`; the same producer stores
  advanced capability bits in that tail, including WPA3-related state
- current behavior before fix:
  the local Tahoe headers still exposed only
  `apple80211_capability_data.capabilities[14]`, so the local struct size was
  `0x12` bytes total. `getCARD_CAPABILITIES(...)` therefore zeroed only the
  short prefix and left Apple-visible tail bytes uninitialized
- why it matters:
  the post-`CR-029` runtime fails association on a `wpa3-transition` network
  with policy `INVALID_AKMS`, while the active hidden join path never re-enters
  the local WPA3-downgrade shim in `setAUTH_TYPE(...)`. A truncated capability
  carrier can therefore leak arbitrary advanced AKM support into that exact
  hidden path
- fix candidate:
  - lift Tahoe `apple80211_capability_data` to the Apple-sized `0x1c` public
    carrier (`version + 24 capability bytes`)
  - add Tahoe `static_assert(sizeof(...) == 0x1c)`
  - keep the current producer conservative but zero the full Tahoe carrier
- evidence:
  - Apple decomp writes through offsets `+0x0d`, `+0x11`, `+0x12`, `+0x13`,
    `+0x15`, `+0x16`, and `+0x17`
  - Apple feature flag `0x41` maps to WPA3 SAE support inside that tail
  - local build with the ABI fix succeeds and still resolves all BootKC symbols
- files:
  - `include/Airport/apple80211_ioctl.h`
  - `AirportItlwm/AirportItlwmV2.cpp`
  - `docs/tahoe_signal_chain_audit.md`
  - `analysis/ANALYSIS_REPORT_2026-04-19.md`
- next action:
  runtime-test the fixed kext on the same transition network and check whether
  `INVALID_AKMS` disappears or the hidden `0x45/0x3ad8` queue narrows further

### 15. Tahoe capability content still over-advertises impossible AKM state

- queue: runtime post-`CR-031` reboot
- status: open_confirmed_with_fix_candidate
- narrowed anomaly: `TAHOE-CARD-CAP-CONTENT-OVERADVERTISE-006`
- producer: `AirportItlwm::getCARD_CAPABILITIES(...)`
- consumer: hidden Tahoe association / WCL capability readers
- reference behavior:
  Apple Tahoe `AppleBCMWLANCore::getCARD_CAPABILITIES(...)` never sets:
  - `cap[2] bit 0x80`
  - `cap[3] bit 0x08`
  - `cap[6] bit 0x80`
  The Apple producer only drives:
  - `cap[2]` within `0x7f`
  - `cap[3]` bits `{0x01, 0x02, 0x04, 0x20, 0x80}`
  - `cap[6]` bits `{0x04, 0x08, 0x10, 0x20, 0x40}`
- current behavior before fix:
  the local Tahoe producer still hard-codes:
  - `cap[2] = 0xEF`
  - `cap[3] = 0x2B`
  - `cap[6] = 0x8C`
  which sets exactly those Apple-impossible bits
- why it matters:
  after `CR-031` the capability tail is no longer uninitialized, but the
  post-reboot runtime still fails association on the same `wpa3-transition`
  network with downstream policy `INVALID_AKMS`. The hidden join path remains
  active and still bypasses the local WPA3 downgrade shim, so impossible card
  capability bits are now the next narrower way to over-advertise unsupported
  AKM state into that queue
- fix candidate:
  sanitize the hard-coded Tahoe capability cluster to Apple-consistent values:
  - `cap[2] = 0x6F`
  - `cap[3] = 0x27`
  - `cap[6] = 0x0C`
- evidence:
  - primary-source Apple decomp:
    `AppleBCMWLANCore::getCARD_CAPABILITIES(...) @ 0xffffff80015e4c66`
  - Apple never sets the three bits listed above, while the local producer
    still sets all three before the hidden association queue consumes the blob
  - the live post-`CR-031` runtime still fails with `-536870201`, then reasons
    `8` / `10`, then policy `INVALID_AKMS`
- files:
  - `AirportItlwm/AirportItlwmV2.cpp`
  - `docs/tahoe_signal_chain_audit.md`
  - `analysis/ANALYSIS_REPORT_2026-04-19.md`
  - `docs/tahoe_discrepancy_inventory.md`
- next action:
  build and stage the sanitized capability producer, then re-test the same
  transition-network association path to see whether the hidden queue stops
  asking for invalid AKM state

### 16. Tahoe hidden assoc carrier is still gated by the interface command filter

- queue: runtime post-`CR-032` reboot
- status: open_confirmed_with_fix_candidate
- narrowed anomaly: `TAHOE-HIDDEN-ASSOC-CMD-GATE-007`
- producer: `AirportItlwmSkywalkInterface::isCommandProhibited(int)`
- consumer: hidden Tahoe family association carrier `0x45` / `0x46`
- reference behavior:
  Apple Tahoe explicitly enables hidden commands `0x45` and `0x46` during
  core bring-up. Primary-source decomp shows repeated
  `FUN_ffffff800160190a(param_1, 0x45)` and `FUN_ffffff800160190a(param_1, 0x46)`
  inside the Apple Broadcom command-registration batch
- current behavior before fix:
  the local Tahoe port only implemented `AirportItlwm::isCommandProhibited(int)`
  on the controller side. The active hidden association fallback does not hit
  that slot at all: `IO80211Family::FUN_ffffff80021e82ef` consults interface
  slot `+0xcc8`, which maps to `[411] IO80211SkywalkInterface::isCommandProhibited(int)`,
  and `AirportItlwmSkywalkInterface` did not override it
- why it matters:
  the post-`CR-032` runtime still reaches `APPLE80211_IOC_ASSOCIATE ->
  -536870201 -> INVALID_AKMS`, while there are still no live hits for local
  `setASSOCIATE(...)`, `setWCL_ASSOCIATE(...)`, `setAUTH_TYPE(...)`,
  `setRSN_IE(...)`, or `associateSSID(...)`. The active family association
  carrier is therefore still dying before the local auth/RSN owner, and the
  interface command gate is the next narrower Apple-visible seam on that path
- fix candidate:
  add an explicit `AirportItlwmSkywalkInterface::isCommandProhibited(int)`
  override and open only hidden Tahoe commands `0x45` / `0x46`, while leaving
  all other commands on the inherited family behavior
- evidence:
  - `IO80211Family::FUN_ffffff80022080ef` validates the large assoc payload
    (`len == 0x3ad8`) and routes it through `sendIOUCToWcl(..., 0x45, ..., 0x3ad8)`
  - if that carrier is not absorbed, fallback
    `IO80211Family::FUN_ffffff80021e82ef` reaches only interface slot `+0xcc8`
  - `IO80211_vtables_BootKC_26.2_25C56.txt` maps that slot to
    `[411] IO80211SkywalkInterface::isCommandProhibited(int)`
  - Apple Broadcom bring-up explicitly enables `0x45` and `0x46`, but the
    local Tahoe interface had no corresponding override before this fix
- files:
  - `AirportItlwm/AirportItlwmSkywalkInterface.hpp`
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  - `docs/tahoe_signal_chain_audit.md`
  - `analysis/ANALYSIS_REPORT_2026-04-19.md`
- next action:
  boot the runtime with the new interface override and verify that the hidden
  carrier now logs local `isCommandProhibited(0x45/0x46)` hits, then check
  whether association moves deeper than the current pre-auth failure point

### 17. Tahoe interface command gate must mirror controller ownership, not the inherited family filter

- queue: runtime post-`CR-033` reboot
- status: open_confirmed_with_fix_candidate
- narrowed anomaly: `TAHOE-INTERFACE-CMD-GATE-OWNER-MISMATCH-008`
- producer: `AirportItlwmSkywalkInterface::isCommandProhibited(int)`
- consumer:
  - Tahoe startup IOCTL path (`APPLE80211_IOC_CARD_CAPABILITIES`)
  - hidden association carrier `0x45` / `0x46`
- reference behavior:
  on the local Tahoe port, command policy already lives on the controller side
  in `AirportItlwm::isCommandProhibited(int)`. The interface-side slot is only
  a family-visible seam above that owner and must preserve the same policy
  source instead of inventing a second non-hidden filter.
- current behavior after `CR-033`:
  `AirportItlwmSkywalkInterface::isCommandProhibited(int)` opened hidden
  commands `0x45` / `0x46`, but delegated every other command to inherited
  `IO80211InfraProtocol::isCommandProhibited(int)`. The first reboot into that
  runtime regressed startup:
  - `wdutil info` reported `No Wi-Fi hardware installed`
  - `airportd` repeatedly cached `CWFApple80211 ... name=(null)` for `en0`
  - `Apple80211BindToInterfaceWithIOCTL` kept falling back with `err -1`
  - `dmesg` showed local `isCommandProhibited(0xc)=0`, immediately followed by
    `Failed ioctl ret[26279936] 'APPLE80211_IOC_CARD_CAPABILITIES'`
- why it matters:
  this proves the `CR-033` regression is not "hidden commands still blocked".
  The deeper mismatch is owner selection: ordinary startup IOCTLs and hidden
  association carriers cross the same interface-side seam, and the non-hidden
  side cannot be delegated back to the inherited family filter without breaking
  bootstrap and CWFApple80211 binding.
- fix candidate:
  keep the explicit interface override, but proxy all commands through the
  already-bound controller owner:
  `instance->isCommandProhibited(command)`
  This preserves the local Tahoe controller policy for startup IOCTLs while
  still leaving hidden `0x45` / `0x46` open on the same seam.
- evidence:
  - live post-`CR-033` runtime loaded the new UUID
    `33A464B2-FF46-314C-8CC5-4B116DEA3D38`, so the regression is from the new
    gate code, not from a stale binary
  - `dmesg` showed interface-side hits for ordinary startup commands:
    `0x2b` and `0xc`
  - the failure happened after `isCommandProhibited(0xc)=0`, so the remaining
    divergence is the owner behind the gate, not the boolean on that one call
  - the pre-`CR-033` runtime booted correctly enough to show networks in UI,
    which means the local controller-owned policy was already compatible with
    startup IOCTLs before the interface override changed owner selection
- files:
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  - `docs/tahoe_signal_chain_audit.md`
  - `analysis/ANALYSIS_REPORT_2026-04-19.md`
- next action:
  boot with the controller-proxy gate and verify that
  `APPLE80211_IOC_CARD_CAPABILITIES` succeeds again, `airportd` stops caching
  `name=(null)`, and the association path returns to the deeper post-`CR-032`
  failure point instead of dying at bootstrap

### 18. Tahoe startup regression after `CR-033` comes from gate instrumentation, not command policy

- queue: structural review after `CR-034` rejection
- status: open_confirmed_with_fix_candidate
- narrowed anomaly: `TAHOE-INTERFACE-CMD-GATE-INSTRUMENTATION-SIDE-EFFECT-009`
- producer: `AirportItlwmSkywalkInterface::isCommandProhibited(int)`
- consumer:
  - Tahoe startup IOCTL path (`APPLE80211_IOC_CARD_CAPABILITIES`)
  - `airportd` / CoreWiFi bootstrap bind path
- reference behavior:
  hidden Tahoe commands `0x45` / `0x46` still need the explicit interface-side
  allow from `CR-033`, but ordinary startup commands must remain on the plain
  inherited policy path with no extra side effects inside this seam.
- current behavior after `CR-033`:
  the regressed startup path already showed the relevant commands as allowed:
  `isCommandProhibited(0x2b)=0` and `isCommandProhibited(0xc)=0`.
  That means the failing bootstrap path was not blocked by the policy bit.
  The only new runtime-bearing behavior on that path was the per-command
  `XYLog(...)` instrumentation added inside the interface gate.
- why it matters:
  `CR-034` was rejected specifically because changing the owner to
  `instance->isCommandProhibited(command)` did not prove any behavioral delta;
  the controller stub also returns `false`. So the next exact fix must target
  the only remaining changed behavior on the failing startup path:
  instrumentation inside the gate itself.
- fix candidate:
  keep `CR-033`'s explicit hidden `0x45` / `0x46` allow, but remove all
  interface-gate logging and return ordinary commands directly to inherited
  `super::isCommandProhibited(command)`.
- evidence:
  - reviewer finding in `COMMIT_DECISION_CR-034.md`:
    both the current interface path and the proposed controller proxy already
    yielded `prohibited=0` for the observed failing startup commands
  - live `dmesg` / telemetry interleave:
    `Failed to send CoreAnalytics for event com.apple.wifi.ioctlPathUsedByClientitlwm: DEBUG isCommandProhibited command=0xc prohibited=0`
  - immediate downstream failure after the same line:
    `Failed ioctl ret[26279936] 'APPLE80211_IOC_CARD_CAPABILITIES'`
- files:
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  - `docs/tahoe_signal_chain_audit.md`
  - `analysis/ANALYSIS_REPORT_2026-04-19.md`
- next action:
  boot with hidden `0x45` / `0x46` still opened but without gate
  instrumentation, then verify whether Tahoe startup returns from
  `No Wi-Fi hardware installed` / `name=(null)` back to the deeper
  post-`CR-032` runtime state

### 19. Visible Tahoe scan-result RSN IEs still over-advertise unsupported advanced AKMs

- queue: structural review after post-`CR-035` runtime verification
- status: rejected_not_proven
- narrowed anomaly: `TAHOE-VISIBLE-RSN-AKM-OVERADVERTISE-010`
- producer:
  - `buildTahoeWclScanResultPayload(...)`
  - `convertNodeToScanResult(...)`
  - `getWCL_BSS_INFO(...)`
- consumer:
  - CoreWiFi visible candidate selection before hidden Tahoe join ownership
  - `airportd` association planning for mixed / transition networks
- reference behavior:
  visible scan-result carriers must not advertise an advanced AKM set that the
  active hidden Tahoe join path cannot complete. Mixed / transition networks
  should be exposed through the legacy-visible AKM subset unless the BSS is
  truly WPA3-only.
- current behavior after `CR-035`:
  startup is healthy and scan visibility is restored, but the same manual join
  still fails with `-536870201`, reasons `8` / `10`, and policy
  `INVALID_AKMS`. `airportd` already classifies the candidate as
  `auths={ psk psk_sha256 sae }`, and scan-cache telemetry shows advanced-AKM
  masks such as `enc=0x8 akm=0x00048080`.
- why it matters:
  the local public join/auth path is still bypassed on this runtime, so the
  existing `associateSSID(...)` downgrade shim is not the active owner. The
  next earlier locally-controlled seam is the visible scan/beacon IE payload
  that determines which auth set CoreWiFi chooses before hidden association
  begins.
- fix candidate:
  add shared RSN sanitization for client-visible IEs:
  keep only AKM suites `00:0f:ac:01` and `00:0f:ac:02` when an RSN IE is
  mixed / transition, but preserve the original IE unchanged when no legacy
  AKM remains so WPA3-only BSSes are not misrepresented.
- exact delivery fix required:
  `MacKernelSDK` still models `apple80211_scan_result::asr_ie_data` as a
  pointer, so `convertNodeToScanResult(...)` needs an interface-owned scratch
  buffer for the pointer-based ABI while still supporting the inline-buffer
  Tahoe local header variant.
- review outcome:
  `CR-036` was rejected at Stage 1. The reviewer did not accept visible-AKM
  suppression as an Apple-backed contract and classified it as masking unless
  deeper hidden-owner recovery or direct Apple reference evidence proves that
  visible mixed-network RSN IEs must be rewritten.
- evidence:
  - post-`CR-035` runtime:
    networks visible in UI, `wdutil` shows `en0` and non-zero scan cache
  - failing join:
    `APPLE80211_IOC_ASSOCIATE -> -536870201`, disassociate reasons `8` / `10`,
    policy `INVALID_AKMS`
  - visible auth set before hidden join:
    `airportd` logs `{ psk psk_sha256 sae }`
  - local public path still inactive:
    no same-cycle hits for `setASSOCIATE(...)`, `setAUTH_TYPE(...)`,
    `setRSN_IE(...)`, or `associateSSID(...)`
- files:
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  - `AirportItlwm/AirportItlwmSkywalkInterface.hpp`
  - `AirportItlwm/AirportItlwmV2.cpp`
  - `AirportItlwm/AirportItlwmV2.hpp`
  - `docs/tahoe_signal_chain_audit.md`
  - `analysis/ANALYSIS_REPORT_2026-04-19.md`
- next action:
  stop pursuing visible-AKM suppression as the leading exact fix and recover
  a deeper Apple-backed contract seam instead.

### 20. Tahoe target still omits the Apple supplicant build contract

- queue: structural resubmission after `CR-036` rejection
- status: open_confirmed_with_fix_candidate
- narrowed anomaly: `TAHOE-APPLE-SUPPLICANT-BUILD-CONTRACT-011`
- producer:
  - Tahoe target `GCC_PREPROCESSOR_DEFINITIONS`
  - `AirportItlwm::useAppleRSNSupplicant(...)`
  - Tahoe `getASSOCIATION_STATUS(...)`
  - `net80211` RSN/EAPOL branches gated by `USE_APPLE_SUPPLICANT`
- consumer:
  - CoreWiFi / `CWEAPOLClient`
  - hidden Tahoe association path that consumes Apple-managed RSN/EAPOL state
- reference behavior:
  Tahoe must build with the same Apple supplicant contract already enabled on
  the other Airport targets. When that contract is active, the driver exposes
  Apple-managed RSN IE override, EAPOL handoff, and association-status state
  instead of keeping the local non-Apple supplicant path underneath a
  CoreWiFi-facing Tahoe UI/runtime.
- current behavior before fix:
  post-`CR-035` runtime already reaches the Apple-facing association flow:
  networks are visible, `airportd` performs real association attempts, logs
  mention `CWEAPOLClient`, and failures end as `INVALID_AKMS`. But Tahoe was
  still built without `USE_APPLE_SUPPLICANT`, unlike the other Airport targets.
  `xcodebuild -showBuildSettings` confirmed that Tahoe effective
  `GCC_PREPROCESSOR_DEFINITIONS` lacked the macro before this fix.
- why it matters:
  `USE_APPLE_SUPPLICANT` is not cosmetic. It flips the exact RSN/auth seams the
  current Tahoe failure still crosses:
  - `AirportItlwm::useAppleRSNSupplicant(...)`
  - `AirportItlwmSkywalkInterface::getRSN_IE(...)` /
    `setRSN_IE(...)`
  - `ieee80211_output.c` association-request RSN IE override
  - `ieee80211_input.c` EAPOL delivery to Apple userspace
  - `ieee80211_crypto_tkip.c` Apple-side MIC direction branch
  - `ic_assoc_status` propagation that Tahoe should surface back through
    `getASSOCIATION_STATUS(...)`
- fix candidate:
  - add `USE_APPLE_SUPPLICANT` to Tahoe Debug and Release target build settings
  - make `scripts/build_tahoe.sh` fail if the effective Tahoe build settings do
    not contain the macro
  - on Tahoe, return `ic_assoc_status` from
    `AirportItlwmSkywalkInterface::getASSOCIATION_STATUS(...)` whenever the
    link is not yet in `IEEE80211_S_RUN`
  - drop the rejected visible-RSN sanitizer and restore raw scan/beacon IE copy
- evidence:
  - project source audit:
    all other Airport targets already define `USE_APPLE_SUPPLICANT`, Tahoe was
    the outlier
  - effective-build audit:
    `xcodebuild -showBuildSettings` for `AirportItlwm-Tahoe` previously omitted
    the macro; after this fix it reports
    `... AIRPORT USE_APPLE_SUPPLICANT __IO80211_TARGET=__MAC_26_0 ...`
  - runtime:
    post-`CR-035` Tahoe still fails in an Apple-facing auth path
    (`CWEAPOLClient`, `INVALID_AKMS`), which is inconsistent with building the
    target without Apple supplicant support
  - verification:
    `./scripts/build_tahoe.sh /System/Library/KernelCollections/BootKernelExtensions.kc`
    passed with `BUILD SUCCEEDED`, and symbol verification passed
- files:
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  - `docs/tahoe_discrepancy_inventory.md`
  - `docs/tahoe_signal_chain_audit.md`
  - `analysis/ANALYSIS_REPORT_2026-04-19.md`
  - `itlwm.xcodeproj/project.pbxproj`
  - `scripts/build_tahoe.sh`
- next action:
  boot with the Apple-supplicant-enabled Tahoe runtime and verify whether
  association now traverses the Apple RSN/EAPOL contract instead of failing at
  the pre-supplicant mismatch point.

### 21. Fresh Tahoe WCL scan results still publish a non-Apple chanSpec

- queue: structural review after post-`CR-037` runtime verification
- status: open_confirmed_with_fix_candidate
- narrowed anomaly: `TAHOE-WCL-SCAN-CHANSPEC-MISMATCH-012`
- producer:
  - `buildTahoePrimaryChanSpec(...)`
  - `buildTahoeWclScanResultPayload(...)`
- consumer:
  - `APPLE80211_M_WCL_SCAN_RESULT` (`0xC9`) ingestion in WCL / CoreWiFi
  - user-visible scan cache and UI-visible network list
- reference behavior:
  Apple `processScanResults` does not invent a private scan-result chanspec.
  It converts the firmware chanspec through
  `AppleBCMWLANChanSpec::getAppleChannelSpec(...)` and writes that Apple-visible
  `AppleChannelSpec_t` value into the `0xC9` metadata carrier.
- current behavior before fix:
  rescans are live and kernel telemetry repeatedly posts fifteen fresh WCL scan
  results, but `airportd` often ingests only eight in the same window and
  `system_profiler` exposes only eight visible networks, all on 2.4 GHz
  channels. The local Tahoe scan-result metadata still used
  `0x1000 | (band << 14) | channel`, which is not Apple's primary-20
  `AppleChannelSpec_t` encoding.
- why it matters:
  this is not a "scan never happens" bug. The scan completes, but the fresh
  user-visible candidate plane drops part of the BSS set after local posting.
  The band skew in `system_profiler` makes the malformed `chanSpec` carrier the
  narrowest causal seam.
- fix candidate:
  emit Apple-compatible primary-20 `AppleChannelSpec_t` values in
  `buildTahoePrimaryChanSpec(...)`:
  - 2.4 GHz -> `channel`
  - 5 GHz -> `0xc000 | channel`
- evidence:
  - runtime:
    repeated `setWCL_SCAN_REQ -> scan triggered OK`,
    `posted scanResults=15`, `Updated scan cache with live scan results`, and
    same-window `scanResultsCount=8`
  - visible-surface skew:
    `system_profiler SPAirPortDataType` lists only eight visible networks and
    all are 2.4 GHz
  - Apple reference:
    `AppleBCMWLANScanAdapter::processScanResults` routes the scan-result
    metadata through `AppleBCMWLANChanSpec::getAppleChannelSpec(...)`
  - Apple chanspec contract:
    on the FW<2 primary-20 path, Apple 2.4 GHz channels map to `channel`, and
    Apple 5 GHz channels map to `0xc000 | channel`
- files:
  - `AirportItlwm/AirportItlwmV2.cpp`
  - `docs/tahoe_signal_chain_audit.md`
  - `analysis/ANALYSIS_REPORT_2026-04-19.md`
- next action:
  boot with Apple-compatible scan-result chanspec metadata and verify that the
  user-visible candidate set no longer collapses to the smaller 2.4 GHz-only
  subset while the deeper association root is investigated separately.

### 22. Visible `APPLE80211_IOC_ASSOCIATE` still leaks controller-side unsupported before local association code

- queue: structural review after live connect failure on `2026-04-20`
- status: open_confirmed_with_fix_candidate
- narrowed anomaly: `TAHOE-ASSOC-IOCTL-CARDSPECIFIC-ROUTING-013`
- producer:
  - controller-side Tahoe generic card-specific seam
    `handleCardSpecific(IO80211SkywalkInterface *, unsigned long, void *, bool)`
- consumer:
  - `Apple80211IOCTLSetWrapper`
  - `airportd` manual connect path for `APPLE80211_IOC_ASSOCIATE`
- reference behavior:
  a Tahoe connect attempt must not die at controller-side generic unsupported
  before the driver's local association handler runs. The interface already
  carries `processApple80211Ioctl(...) -> setASSOCIATE(...) -> associateSSID(...)`
  for selector `20`; the remaining missing contract is the controller-to-
  interface routing seam that hands this payload over.
- current behavior before fix:
  the live connect window shows:
  - `2026-04-20 17:44:58.510`
    `Apple80211IOCTLSetWrapper ... APPLE80211_IOC_ASSOCIATE ... return -536870201/0xe00002c7`
  - `2026-04-20 17:44:58.720`
    same second failure on the same selector
  There are still no same-cycle local hits for `processApple80211Ioctl(...)`,
  `setASSOCIATE(...)`, or `associateSSID(...)`.
- why it matters:
  this is a tighter connect root than the earlier broad hidden-owner theory.
  The current runtime proves that the visible/manual association request is
  already dying at the controller generic plane, so the local auth/RSN path is
  unreachable regardless of how correct it is internally.
- rejected alternative narrowed out:
  `apple80211SkywalkRequest(...)` is not a legal Tahoe fix path. V3 ABI does
  not declare those overrides, and the target fails to compile if they are
  added with `override`.
- fix candidate:
  implement Tahoe `handleCardSpecific(...)` as a narrow route for
  `APPLE80211_IOC_ASSOCIATE`:
  - synthesize `apple80211req`
  - set `req_type = APPLE80211_IOC_ASSOCIATE`
  - route to
    `AirportItlwmSkywalkInterface::processApple80211Ioctl(SIOCSA80211, ...)`
  - leave all other selectors on `kIOReturnUnsupported`
- evidence:
  - runtime:
    `Apple80211IOCTLSetWrapper ... APPLE80211_IOC_ASSOCIATE ... 0xe00002c7`
    twice in the same connect attempt, followed by downstream `INVALID_AKMS`
  - ABI:
    `include/Airport/IO80211ControllerV3.h` exposes
    `handleCardSpecific(...)` but not `apple80211SkywalkRequest(...)`
  - local source:
    `AirportItlwmSkywalkInterface::processApple80211Ioctl(...)` already owns
    selector `20` and routes it to `setASSOCIATE(...)`
- files:
  - `AirportItlwm/AirportItlwmV2.cpp`
  - `AirportItlwm/AirportItlwmV2.hpp`
  - `docs/tahoe_signal_chain_audit.md`
  - `analysis/ANALYSIS_REPORT_2026-04-19.md`
- next action:
  boot with controller-side `APPLE80211_IOC_ASSOCIATE` routed into the local
  interface helper and verify that the first manual connect attempt finally
  enters `setASSOCIATE(...)` instead of failing at `Apple80211IOCTLSetWrapper`.

### 23. `ROAM_PROFILE` still leaks hidden `0xe0822403` instead of Apple unsupported and aborts auto-join

- queue: structural review after post-`CR-039` runtime verification
- status: open_confirmed_with_fix_candidate
- narrowed anomaly: `TAHOE-ROAM-PROFILE-FAIL-SHAPE-014`
- producer:
  - visible selector `APPLE80211_IOC_ROAM_PROFILE` (`216`)
  - active Tahoe set-wrapper path before association
- consumer:
  - `airportd` auto-join / startup preparation
- reference behavior:
  `AppleBCMWLANInfraProtocol::setROAM_PROFILE(...)` is an Apple-unsupported
  setter and returns `0xe00002c7`. It is not a low-level "not associated"
  carrier.
- current behavior before fix:
  the post-`CR-039` runtime logs:
  - `2026-04-20 19:15:40.549`
    `Apple80211IOCTLSetWrapper ... APPLE80211_IOC_ROAM_PROFILE ... return -528342013/0xe0822403`
  - the same startup window immediately emits repeated
    `AUTO-JOIN: Auto-join aborted ... error=(37 'driver not available')`
- why it matters:
  this selector fires before manual association even starts. Even with scan
  visibility restored and selector `20` routing narrowed separately, startup
  still aborts if selector `216` leaks hidden not-associated status outward.
- local contract already available:
  the Tahoe Skywalk target already exposes
  `setROAM_PROFILE(...) -> kIOReturnUnsupported`, which matches the Apple
  unsupported contract. The active path is bypassing that local owner.
- fix candidate:
  route `SIOCSA80211` `APPLE80211_IOC_ROAM_PROFILE` through the local Tahoe
  dispatcher:
  - `processApple80211Ioctl(...) -> setROAM_PROFILE(...)`
  - extend Tahoe `handleCardSpecific(...)` to pass selector `216` into that
    local path, next to selector `20`
- evidence:
  - runtime:
    `ROAM_PROFILE -> 0xe0822403` followed by immediate auto-join
    `driver not available`
  - docs/decomp:
    Apple infra setter contract is direct `0xe00002c7`
  - local source:
    the unsupported local `setROAM_PROFILE(...)` override already exists
- files:
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  - `AirportItlwm/AirportItlwmV2.cpp`
  - `analysis/ANALYSIS_REPORT_2026-04-19.md`
  - `docs/tahoe_signal_chain_audit.md`
- next action:
  boot with selector `216` forced back onto the local unsupported contract and
  verify that startup no longer aborts on `ROAM_PROFILE -> driver not
  available` before connect preparation proceeds.

### 24. Hidden `0x45/0x3ad8` association fallback dies in slot `[470]` instead of local `setWCL_ASSOCIATE(...)`

- queue: structural review after post-`CR-040` manual connect verification
- status: open_confirmed_with_fix_candidate
- narrowed anomaly: `TAHOE-HIDDEN-ASSOC-SLOT470-BRIDGE-015`
- producer:
  - visible `APPLE80211_IOC_ASSOCIATE` selector `20`
  - family `getSetHandler(20)` hidden carrier path
- consumer:
  - `airportd` manual connect retry loop
  - local interface slot `[470]`
- reference behavior:
  when hidden selector `0x45` with the `0x3ad8` assoc-candidates blob is not
  absorbed by WCL, the local Tahoe port must still hand that payload into the
  recovered association owner instead of failing in a generic unsupported
  stub.
- current behavior before fix:
  the live connect window now shows:
  - `2026-04-20 22:52:19.129`
    `Apple80211IOCTLSetWrapper ... APPLE80211_IOC_ASSOCIATE ... return -536870201/0xe00002c7`
  - `2026-04-20 22:52:19.347`
    same second retry failure
  - same-cycle kernel evidence:
    - no `handleCardSpecific(...)`
    - no `processApple80211Ioctl(...)`
    - no `setASSOCIATE(...)`
    - no `setWCL_ASSOCIATE(...)`
    - no `associateSSID(...)`
    - only `DEBUG VTABLE [470] getAWDL_PEER_TRAFFIC_STATS`
- why it matters:
  this proves the live selector-`20` path is no longer dying in the previously
  claimed controller card-specific seam. It is now a narrower hidden-carrier
  fallback mismatch: the active payload reaches slot `[470]`, and that slot
  currently returns generic unsupported.
- fix candidate:
  replace the inline slot `[470]` stub with a real bridge:
  - keep `kIOReturnUnsupported` for unrelated callers
  - but when `(data != NULL && len == 0x3ad8)`, route the payload into
    `setWCL_ASSOCIATE((apple80211AssocCandidates *)data)`
- evidence:
  - family decomp:
    - `getSetHandler(20)` resolves to `FUN_ffffff80022080ef`
    - it validates `req_len == 0x3ad8`
    - it sends hidden selector `0x45`
    - fallback `FUN_ffffff80021e82ef` only checks `isCommandProhibited(0x45)`
      before re-entering the none-protocol side
  - runtime:
    the only same-cycle local hit is slot `[470]`
  - local source:
    `setWCL_ASSOCIATE(...)` already parses the same assoc-candidates blob and
    drives local auth / RSN / `associateSSID(...)`
- files:
  - `AirportItlwm/AirportItlwmSkywalkInterface.hpp`
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  - `docs/tahoe_signal_chain_audit.md`
  - `analysis/ANALYSIS_REPORT_2026-04-19.md`
- next action:
  boot with the slot `[470]` bridge in place and verify that the first hidden
  `0x45/0x3ad8` fallback enters `setWCL_ASSOCIATE(...)` instead of returning
  public `0xe00002c7`.

### 25. Inline slot `[509]` `getCHIP_POWER_RANGE(...)` was still classified as unsupported even though Apple exports a real carrier

- queue: structural audit after the targeted Tahoe inline-stub sweep
- status: open_confirmed_with_fix_candidate
- narrowed anomaly: `TAHOE-CHIP-POWER-RANGE-STUB-MISMATCH-016`
- producer:
  - slot `[509]`
  - `getCHIP_POWER_RANGE(apple80211_chip_power_limit *)`
- consumer:
  - any family/client path that asks Tahoe for the caller-visible chip-power
    duty-cycle table
- reference behavior:
  Apple does not leave this getter on a generic unsupported stub. The
  recovered path is:
  - `AppleBCMWLANCore::getCHIP_POWER_RANGE(...)`
  - `version=1`
  - copy packed 6x-`u64` duty-cycle entries from config-manager state
- current behavior before fix:
  the local Tahoe header still exposed slot `[509]` as:
  - inline `kIOReturnUnsupported`
- why it matters:
  this is exactly the class of mismatch that the "fix our stubs that are not
  stubs in the reference" audit is supposed to remove. It is not the current
  association blocker, but it is still the wrong system contract.
- fix candidate:
  - add the missing packed public carrier
    `apple80211_chip_power_limit` (`0x34` bytes)
  - replace the inline stub with a real `getCHIP_POWER_RANGE(...)`
    implementation
  - recover the six `u64` entries from the same Tahoe source contract Apple
    uses:
    - read `wlan.chip.power.dutycycle` (`0x30` bytes) from the
      interface/provider IOService path
    - if absent, copy the exact built-in fallback table from the Apple
      config-manager bootstrap path
- evidence:
  - decomp:
    - `AppleBCMWLANCore::getCHIP_POWER_RANGE(...) @ 0xffffff80015e63f0`
    - `AppleBCMWLANConfigManager::copyWlanPwrDutyCycleTable(...) @ 0xffffff8001673eb4`
    - property bootstrap / fallback path for
      `wlan.chip.power.dutycycle` at `0xffffff8001671920`
  - local source before fix:
    - `AirportItlwmSkywalkInterface.hpp` slot `[509]` was inline unsupported
  - audit exclusions:
    nearby inline stubs like `getWIFI_NOISE_PER_ANT(...)`,
    `getHE_COUNTERS(...)`, `getWCL_WNM_OFFLOAD(...)`,
    `getFW_CLOCK_INFO(...)`, `getTIMESYNC_STATS(...)`,
    `getSMARTCCA_OPMODE(...)`, and `getLQM_STATISTICS(...)` remain correctly
    classified as Apple-unsupported and were intentionally not changed
- files:
  - `include/Airport/apple80211_ioctl.h`
  - `AirportItlwm/AirportItlwmSkywalkInterface.hpp`
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  - `docs/tahoe_signal_chain_audit.md`
  - `analysis/ANALYSIS_REPORT_2026-04-19.md`
- next action:
  carry the slot `[509]` producer fix through review together with the current
  slot `[470]` bridge diff; after runtime approval, verify that Tahoe no
  longer reports `getCHIP_POWER_RANGE(...)` as unsupported or returns a
  synthetic empty payload if that getter is exercised by the family or
  userland.

### 26. Remaining inline Tahoe `unsupported` tail is now closed as a header-surface mismatch

- queue: structural audit close-out
- status: closed_with_explicit_reference_bodies
- narrowed anomaly: `TAHOE-INLINE-STUB-CLOSEOUT-4-BATCHES-017`
- previous state:
  after slot `[509]`, the Tahoe header still carried exactly `34` inline
  `return kIOReturnUnsupported;` slot bodies
- exact 4-batch split used for closure:
  - batch 1, direct Apple-unsupported getters:
    `[491, 492, 505, 529, 536, 537, 538, 539, 541, 542]`
  - batch 2, direct Apple-unsupported setters:
    `[561, 583, 585, 605, 607, 630, 635, 641, 642, 643, 644, 645, 646, 648,
    660, 661, 663]`
  - batch 3, internal-only / no-producer-recovered explicit-unsupported
    selectors:
    `[488, 499, 563, 591, 620, 621]`
  - batch 4, the one remaining non-stub visible contract:
    `[632] setWCL_UPDATE_FAST_LANE`
- final state:
  - `AirportItlwmSkywalkInterface.hpp` now has
    `inline_unsupported = 0`
  - all former inline bodies are now explicit `.cpp` methods
  - `[632]` now follows the recovered visible contract
    `NULL -> 0xe00002bc`, else success
- why this is closed:
  the mismatch was no longer "missing producers everywhere"; it was that the
  remaining slot policy still lived in anonymous header stubs. That surface is
  now explicit and reference-classed per slot.

### 27. Bootstrap current-AP seed theory was rejected and removed from the live diff

- queue: connect bootstrap after `CR-044`
- status: rejected_removed_from_diff
- anomaly: `TAHOE-CURRENT-AP-CACHE-SEED-019`
- reason:
  reviewer rejected the zero-BSSID `setCurrentApAddress(...)` repair as guessed
  state correction without an Apple-backed empty-cache encoding contract
- current disposition:
  - no `setCurrentApAddress(...)` seeding remains in the exact runtime diff
  - the hypothesis is preserved only as an audit trail, not as the current fix

### 28. Tahoe BSD attach uniqueid theory was rejected and removed from the live diff

- anomaly_id: `TAHOE-BSD-UNIQUEID-CONTRACT-020`
- class: `SYSTEM_CONTRACT_FIX`
- status: rejected_removed_from_diff
- reason:
  Apple/xnu proved that the BSD `uniqueid` seam exists, but review correctly
  found no causal proof tying interface identity/reuse to the active
  `SSID/BSSID/CURRENT_NETWORK -> 0xe0822403` failures on this boot
- current disposition:
  - no Tahoe `initBSDInterfaceParameters(...)` override remains in the exact
    runtime diff
  - the `uniqueid` seam is no longer claimed as the active connect blocker

### 29. External current-link/bootstrap IOCTLs still bypass the live Skywalk bridge and leak raw failure

- anomaly_id: `TAHOE-CONTROLLER-CARDSPECIFIC-GETSET-ROUTING-021`
- class: `SYSTEM_CONTRACT_FIX`
- current symptom on loaded runtime `377695CB-9BA7-3BB2-ADA2-34A997EA95E8`:
  - `2026-04-21 11:00:03.117` `ifname['en0'] APPLE80211_IOC_SSID -> 0xe0822403`
  - `2026-04-21 11:00:03.118` `ifname['en0'] APPLE80211_IOC_BSSID -> 0xe0822403`
  - `2026-04-21 11:00:03.125` `ifname['en0'] APPLE80211_IOC_ROAM_PROFILE -> 0xe0822403`
  - `2026-04-21 11:00:04.962` `AUTO-JOIN ... error=(37 'driver not available')`
- decisive narrowing:
  - the same boot returns the already-fixed local contract
    `APPLE80211_IOC_VIRTUAL_IF_ROLE -> -3903`, so the external plane itself is
    alive
  - the loaded `CR-044` runtime already contains `XYLog(...)` probes in:
    - `processBSDCommand(...)`
    - `processApple80211Ioctl(...)`
    - `getSSID(...)`
    - `getBSSID(...)`
    - `getCURRENT_NETWORK(...)`
    - `setROAM_PROFILE(...)`
  - current `log show` / `dmesg` windows for the failing boot show no hits on
    those local probes, even while `airportd` is actively logging the external
    `SSID/BSSID/ROAM_PROFILE` failures
- local mismatch before fix:
  - Tahoe V3 still exposes controller seam
    `handleCardSpecific(IO80211SkywalkInterface *, unsigned long, void *, bool isSet)`
  - local helper `shouldRouteTahoeSkywalkIoctlReq(...)` already classifies the
    failing bootstrap/current-link GET cluster:
    `SSID`, `BSSID`, `CHANNEL`, `CURRENT_NETWORK`
  - `ROAM_PROFILE` is special here: the local interface owner already supports
    both `getROAM_PROFILE(...)` and `setROAM_PROFILE(...)`, but the helper
    still classified it as GET-only
  - local `handleCardSpecific(...)` also left `isSet == false` requests on
    inherited fallback
- exact correction:
  - keep the controller-side route narrow and whitelist-driven
  - make `APPLE80211_IOC_ROAM_PROFILE` explicit bidirectional in the helper
    whitelist
  - in `handleCardSpecific(...)`, construct one `apple80211req` and route:
    - `isSet ? SIOCSA80211 : SIOCGA80211`
  - let `routeTahoeSkywalkIoctl(...)` and `shouldRouteTahoeSkywalkIoctlReq(...)`
    decide the allowed selector set
  - all non-whitelisted selectors still return inherited unsupported/fallback
- why this is the current root:
  the failing boot proves two facts together:
  - external bootstrap IOCTLs are still dying before the local Skywalk helper
    plane
  - Tahoe V3's remaining controller entry for selector+direction dispatch is
    `handleCardSpecific(..., isSet)`
  so the narrowest live mismatch is incomplete controller-side GET/SET routing,
  not BSD attach identity and not guessed current-AP cache seeding.

### 30. Active bootstrap/current-link failures now prove a public interface request-gate mismatch

- anomaly_id: `TAHOE-INTERFACE-PUBLIC-REQUEST-GATE-023`
- class: `SYSTEM_CONTRACT_FIX`
- current symptom on loaded runtime `82508171-BB7E-3B30-89D7-4B3D1D625879`:
  - `2026-04-21 11:00:03.117`
    `ifname['en0'] APPLE80211_IOC_SSID -> 0xe0822403`
  - `2026-04-21 11:00:03.118`
    `ifname['en0'] APPLE80211_IOC_BSSID -> 0xe0822403`
  - `2026-04-21 11:00:03.125`
    `ifname['en0'] APPLE80211_IOC_ROAM_PROFILE -> 0xe0822403`
  - `2026-04-21 11:00:04.962`
    `AUTO-JOIN ... error=(37 'driver not available')`
- decisive narrowing:
  - current `system_profiler SPAirPortDataType` still shows `en0` with visible
    2.4 GHz / 5 GHz candidates, so this is not another startup-visibility
    regression
  - `CR-049` was rejected because the proposed hidden `0x103/0x104/0x15e`
    mapping is contradicted by Apple-side command naming already present in the
    repo
  - the stronger decomp path is the public request-number fallback itself:
    - `FUN_ffffff80022a2910` sends `sendIOUCToWcl(..., 1, ..., 0x28)` then
      falls back to `FUN_ffffff80021e28b2`
    - `FUN_ffffff8002215524` sends `sendIOUCToWcl(..., 9, ..., 0xc)` then
      falls back to `FUN_ffffff80021e2b46`
    - `FUN_ffffff80021e29a7` falls back with request number `4`
    - `FUN_ffffff80021e3912` falls back with request number `0x67`
    - `FUN_ffffff80021e465f` falls back directly with request number `0xd8`
    - `FUN_ffffff80021e94fa` falls back directly with request number `0xd8`
    - the visible wrapper-side producer for the same selector also uses
      `sendIOUCToWcl(..., 0xd8, ..., 0x180)` before those fallback helpers
- Apple/decomp contract that now matters:
  - `IO80211Family` public fallback helpers call interface slot
    `*param_1 + 0xcc8` with the public request numbers
    `1`, `4`, `9`, `0x67`, and `0xd8`
  - `IO80211_vtables_BootKC_26.2_25C56.txt` maps that slot to
    `[411] IO80211SkywalkInterface::isCommandProhibited(int)`
- local mismatch before fix:
  - the local Skywalk helper owners already existed for
    `SSID/BSSID/CHANNEL/CURRENT_NETWORK/ROAM_PROFILE`
  - but `AirportItlwmSkywalkInterface::isCommandProhibited(int)` still admitted
    only the carried hidden association commands `0x45/0x46`
  - the public fallback request numbers were therefore still left on inherited
    family filtering before the helper plane
- exact correction:
  - keep `isCommandProhibited(...)` narrow to proven selectors only
  - retain the already approved hidden association commands `0x45/0x46`
  - additionally admit only the proven public fallback request numbers:
    `1`, `4`, `9`, `0x67`, `0xd8`
  - continue delegating those admitted selectors to the already permissive
    controller policy
- why this is narrower and more provable than `CR-049`:
  - it follows the exact public request-number seam already shown in family
    decomp
  - it removes the contradicted hidden `0x103/0x104/0x15e` classification from
    the runtime diff
  - it restores the exact interface gate Apple already uses for that fallback
