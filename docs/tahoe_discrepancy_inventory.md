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

### 5. `getRATE` transport-rate query contract is Apple-shaped

Apple decompile indicates:

- `getRATE` checks association and returns `0xe0822403` if not associated
- associated success writes only `apple80211_rate_data::rate[0]` at caller
  offset `+0x08`, as an integer Mbps value derived from the current transport
  rate; it does not initialize `version` / `num_radios`

The `getTXPOWER` / `getMCS_VHT` config-backed source lift is closed, and
`getRATE` now uses the same normalized transport-rate cache instead of
rebuilding the value from `ic_bss->ni_txmcs`, channel width, SGI, and NSS side
fields. See `docs/reference/CR-479-rate-nrate-normalization-20260707.md`.

Current-lab follow-up: the active Tahoe hardware is on `ItlIwn` (`iwn-6030`),
while the original cache reader only covered `ItlIwm` and `ItlIwx`. The iwn
HAL now publishes the same Apple-shaped cached `nrate` from its TX command,
TX_DONE, and A-MPDU/BlockAck producer path, and both Tahoe Skywalk and legacy
Apple80211 getter paths consume it. See
`docs/reference/CR-479-iwn-nrate-cache-20260707.md`.

Status:

- closed

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
dispatcher remains part of the codebase contract. The CR-118..CR-120 cycle
closed the AP_IE_LIST, MCS, and VIRTUAL_IF_CREATE shadow drift, leaving only
surfaces that still need separate proof.

Remaining raw-`6` legacy getters:

- none

Remaining legacy unsupported/gated surfaces needing classification:

- none

Recently closed legacy shadow surfaces:

- `getPROTMODE`
- `getRSN_IE`
- `setRSN_IE`
- `getMCS_INDEX_SET`
- `getNOISE`
- `getMCS`
- `getAP_IE_LIST`
- `setVIRTUAL_IF_CREATE`
- `setBT_COEX_FLAGS`
- `setBT_POWER`

The remaining open items stay in the inventory because they mirror producer
semantics that may still be reused. The closed items stay listed only as
regression markers for future Tahoe/legacy drift audits.

Legacy BT setter note:

- `setBT_COEX_FLAGS` now preserves the recovered direct raw `6` return.
- `setBT_POWER` now preserves the recovered adjacent wrapper-stub
  `0xe082280e` return.
- `getBT_COEX_FLAGS` / `getBT_POWER` stay on the inherited IO80211/WCL path
  because the list-backed carrier producer behind helper `0xffffff80009ff310`
  is not recovered yet.

### 7. APSTA / SoftAP owner layer is a required reconstruction item

Reference role-7 `setVIRTUAL_IF_CREATE(...)` does not end at a generic failure
contract. It allocates an `AppleBCMWLANIO80211APSTAInterface` object, stores it
at core expansion `+0x2c30`, and uses the APSTA state block at `self+0x130` for
SoftAP carriers, feature bits, queues, pools, and datapath accessors.

The local primary STA bridge must not fake those selectors, but the missing
`IO80211SapProtocol` / APSTA owner object is now open implementation debt, not
a closed or excluded topic.

Required local reconstruction:

- `IO80211SapProtocol` base/vtable shape.
- APSTA owner object with `ap` prefix, BSD unit `1`, and Wi-Fi subfamily `3`.
- APSTA state block offsets used by SoftAP carriers and queue/datapath accessors.
- Role-7 creation/storage path matching Apple core expansion `+0x2c30`.
- HostAP capability publication only after the APSTA lifecycle exists.

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

### 31. Slot `[411]` still returns the aborting polarity for the proven public selector subset
- anomaly_id: `TAHOE-INTERFACE-REQUEST-GATE-POLARITY-024`
- symptom after reboot into `CR-051` runtime `B1AFF314-2935-3718-80F7-C440303D13D6`:
  - networks remain visible in UI
  - `sudo wdutil info` still shows `SSID: None`, `BSSID: None`,
    `Security: None`, `Scan Cache Count: 12`
  - `airportd` still emits unchanged failures:
    - `APPLE80211_IOC_SSID -> 0xe0822403`
    - `APPLE80211_IOC_BSSID -> 0xe0822403`
    - `APPLE80211_IOC_CURRENT_NETWORK -> 0xe0822403`
    - `APPLE80211_IOC_ROAM_PROFILE -> 0xe0822403`
    - `AUTO-JOIN ... driver not available`
- Apple/decomp contract that now matters:
  - the exact slot `[411]` helpers branch on non-zero as the surviving path:
    - `FUN_ffffff80021e28b2`: request `1`
    - `FUN_ffffff80021e2b46`: request `9`
    - `FUN_ffffff80021e3912`: request `0x67`
    - `FUN_ffffff80021e465f`: request `0xd8`
    - `FUN_ffffff80021e94fa`: request `0xd8`
  - each helper has the same shape:
    `iVar1 = slot411(...); if (iVar1 != 0) return; else abort`
- local mismatch before fix:
  - the selected selector set from `CR-051` was correct
  - but local `AirportItlwmSkywalkInterface::isCommandProhibited(int)` still
    delegated those commands to `AirportItlwm::isCommandProhibited(int)`
  - controller-side `AirportItlwm::isCommandProhibited(int)` returns `false`
    unconditionally
  - so the selected commands still reached slot `[411]` as zero and took the
    family abort path
- exact correction:
  - keep the exact selected subset from `CR-051`
  - return `true` directly from interface slot `[411]` for:
    - `0x45`, `0x46`
    - `1`, `4`, `9`, `0x67`, `0xd8`
  - leave all other commands on inherited family behavior
- expected effect:
  - the proven public current-link selectors should finally survive slot
    `[411]` and either enter the local helper plane or fail deeper than this
    gate

### 32. Active Skywalk queue layer must use exported Tahoe ABI and driver-owned queue metadata
- anomaly_id: `TAHOE-SKYWALK-QUEUE-ABI-SYMBOL-SURFACE-025`
- queue: active Skywalk inventory/lifecycle layer after `CR-115` rejection
- producer: `AirportItlwm::start()` Skywalk queue construction and
  `AirportItlwmSkywalkInterface` queue/lifecycle accessors
- symptom:
  - `CR-115` was rejected before runtime because reviewer build completed
    compilation but failed BootKC symbol verification
  - unresolved imports:
    - non-const `IOSkywalkPacketQueue::getCapacity()`
    - `IOSkywalkTxCompletionQueue::withPool(...)` with `IOReturn` callback ABI
- Apple/decomp contract now added to the inventory:
  - reference has a distinct TX completion queue at interface ivars `+0x60`
  - reference has RX completion at `+0x68`, TX subqueue vector at `+0x78`, and
    multicast work source at `+0x98`
  - TX queue depth/capacity comes from the Apple custom TX queue ivar `+0x28`
  - RX queue capacity comes from the Apple custom RX completion queue ivar
    `+0x10`
  - Apple custom TX queue `getRingFreeSpace()` returns `0`
  - Apple custom TX queue `getPendingPacketCount()` returns `0`
- BootKC ABI contract:
  - Tahoe exports `IOSkywalkTxCompletionQueue::withPool(...)` with `UInt32`
    callback ABI, not `IOReturn`
  - Tahoe exports `IOSkywalkPacketQueue::getCapacity()` as const only
- local mismatch before CR-116:
  - the CR-115 source relied on the wrong TX completion callback ABI
  - the queue metric accessors called generic queue capacity/free-space methods
    instead of returning local driver-owned queue metadata / Apple custom no-op
    results
- exact correction for the next batch:
  - patch the Tahoe build header path for `IOSkywalkTxCompletionQueueAction`
    to `UInt32`
  - make the local TX completion callback return `UInt32`
  - keep the distinct TX completion queue instead of dropping it
  - store local queue depth/capacity from the exact queue construction capacity
  - return those stored values from `getTxQueueDepth()` and
    `getRxQueueCapacity()`
  - return `0` from `pendingPackets()` and `packetSpace()` to match Apple
    custom queue methods
- non-claims:
  - this does not prove final RSN/data root cause
  - this does not add fake EAPOL, fake keys, fake RSN, fake DHCP, retry, delay,
    replay, or forced link success
  - this does not permit install/runtime until the resubmitted Stage 1 request
    is approved

### 33. APSTA state block must exist before role-7 owner creation can be restored
- anomaly_id: `A-APSTA-STATE-BLOCK-SCAFFOLD-049`
- layer: APSTA/SAP owner reconstruction after CR-121
- Apple contract:
  - role-7 `setVIRTUAL_IF_CREATE(...)` creates `AppleBCMWLANIO80211APSTAInterface`
    object size `0x138`
  - APSTA object offset `+0x130` stores the private APSTA state pointer
  - role-7 feature gates write APSTA state bytes `+0x32a` and `+0x32b`
  - SoftAP methods read state offsets `+0x0e`, `+0x10`, `+0x18`, `+0x1c`,
    `+0x20`, `+0x24`, `+0x28`, `+0x2c`, `+0x68`, and stats at `+0x1b0`
  - APSTA queue/datapath methods read state offsets `+0x2a4`, `+0x2b8`,
    `+0x2d8`, `+0x2e0`, `+0x2e8`, `+0x2f0`, `+0x300`, and `+0x320`
- local mismatch before CR-122:
  - APSTA/SoftAP offsets were only documented in YAML
  - there was no compilable local state carrier with offset checks
  - role-7 success could not be implemented without inventing storage
- exact correction:
  - add `AirportItlwmAPSTAStateBlock` as a local APSTA state offset witness
  - add `AirportItlwmAPSTAObjectStorageLayout` to assert APSTA object
    state-pointer offset `+0x130` and size witness `0x138`
  - compile these assertions through `AirportItlwmSkywalkInterface.hpp`
  - do not expose role-7 success yet; owner lifecycle and SAP vtable/header are
    the next reconstruction layer
- non-claims:
  - this is not a primary-STA data/connectivity fix
  - this does not fake HostAP/APSTA capability
  - this does not create queues, pool ownership, or APSTA BSD registration yet

### 34. SAP protocol vtable seam must not be modeled as primary STA inheritance
- anomaly_id: `A-IO80211SAP-PROTOCOL-SCAFFOLD-050`
- layer: APSTA/SAP owner-class reconstruction after CR-122
- Apple contract:
  - `IO80211SapProtocol` vtable is recovered at `0xffffff80023e8dc0`
  - `AppleBCMWLANIO80211APSTAInterface` vtable is recovered at
    `0xffffff8001777508`
  - SAP inherits the Tahoe Skywalk prefix through the `syncDPSStats` area, then
    exposes a SAP/virtual-interface extension seam at slots `481..519`
  - APSTA overrides typed station-management and SoftAP selectors at slots
    `505..531`
  - APSTA also overrides `forwardPacket(IO80211NetworkPacket*)` at recovered
    slot `465`, so a simple subclass of the current local primary-STA
    `IO80211SkywalkInterface` header is not a proven-safe APSTA base
- local mismatch before CR-123:
  - no local `IO80211SapProtocol` contract existed
  - the old local `IO80211VirtualInterface` header is IOService-based and does
    not model the Tahoe SAP/Skywalk seam
  - defining APSTA directly from the current primary STA classes would either
    put SoftAP selectors after the wrong slots or collide with unresolved
    Skywalk slot aliases
- exact correction:
  - add `include/Airport/IO80211SapProtocol.h` as a compiled contract header
  - record recovered slots and typed function-pointer carriers for APSTA/SAP
    methods without defining an ABI-wrong C++ base class yet
  - include the header through the V3/V2 Apple80211 umbrella so syntax and type
    declarations are compiled in the Tahoe build
- non-claims:
  - this does not instantiate APSTA
  - this does not return role-7 success or advertise HostAP
  - this does not resolve the remaining SAP slot aliases needed for the final
    C++ base class

### 35. APSTA lifecycle resources are part of the private state block
- anomaly_id: `A-APSTA-LIFECYCLE-STATE-RESOURCE-051`
- layer: APSTA owner lifecycle/state scaffold after CR-123
- Apple contract:
  - APSTA `free()` releases the private state block with exact size `0x338`
  - `freeResources()` releases timer/resource pointers at state offsets `+0x70`,
    `+0x78`, `+0x240`, `+0x248`, `+0x250`, `+0x258`, and `+0x260`
  - `reset()` clears state `+0x26c`, byte `+0x329`, and zeroes `+0xb8` for
    `0xf0` bytes
  - `initSoftAPParameters()` clears the same runtime block and scalar `+0x1a8`
  - HostAP/reset paths read owner/core pointer at state `+0x218`
- local mismatch before CR-124:
  - these lifecycle offsets were anonymous padding in
    `AirportItlwmAPSTAStateBlock`
  - APSTA state size was only asserted as minimum coverage
- exact correction:
  - name lifecycle/resource fields in the state scaffold
  - add static asserts for every recovered lifecycle/resource offset
  - assert exact APSTA state block size `0x338`
- non-claims:
  - this does not allocate or start APSTA
  - this does not attach timers/resources
  - this does not enable role-7 success or HostAP capability

### 36. Role-7 APSTA creation/storage contract must be compiled before enablement
- anomaly_id: `A-APSTA-ROLE7-CREATION-STORAGE-052`
- layer: APSTA owner creation/storage scaffold after CR-124
- Apple contract:
  - `setVIRTUAL_IF_CREATE` role is at carrier `data+0x0c`
  - APSTA role is `7`
  - APSTA factory receives `core`, `data+0x04` MAC carrier, role `7`, and
    `data+0x10` BSD-name carrier
  - created APSTA owner is stored at core expansion `+0x2c30`
  - proximity owner is the adjacent core expansion field `+0x2c28`
  - duplicate owner returns `0xe00002d2`
  - create failure returns `0xe00002bd`
  - unknown role returns `0xe0000001`
  - feature gates `0x0d` and `0x0c` write APSTA state bytes `+0x32a` and
    `+0x32b`
- local mismatch before CR-125:
  - role-7 carrier/storage facts were documented but not compiled
  - there was no local core expansion storage witness for APSTA owner `+0x2c30`
- exact correction:
  - add a packed `AirportItlwmAPSTAVirtualIfCreateCarrierLayout`
  - add `AirportItlwmAPSTACoreExpansionStorageLayout`
  - add APSTA role/feature-gate/return-code constants
  - add a typed factory argument contract
- non-claims:
  - this does not return role-7 success
  - this does not allocate APSTA
  - this does not publish HostAP capability

### 37. APSTA datapath activation uses a separate owner/resource pair
- anomaly_id: `A-APSTA-DATAPATH-ACTIVATION-RESOURCE-053`
- layer: APSTA datapath/resource scaffold after CR-125
- Apple contract:
  - APSTA helper `FUN_ffffff8001694064` returns `state+0x210`
  - APSTA event and SoftAP async callback paths use `state+0x210` as their
    bytestream/logger resource
  - APSTA `enableDatapath()` calls object `state+0x2d0` vtable `+0x120` before
    starting TX/RX completion queues
  - APSTA `disableDatapath()` calls object `state+0x2d0` vtable `+0x128`
    before stopping RX/TX completion queues
  - missing completion queues still return `0xe00002bc`
- local mismatch before CR-126:
  - APSTA state offsets `+0x210` and `+0x2d0` were hidden inside padding
  - APSTA datapath accessors were documented, but activation owner/resource
    operands were not compiled into the scaffold
- exact correction:
  - name `AirportItlwmAPSTAStateBlock::resource210`
  - name `AirportItlwmAPSTAStateBlock::datapathOwner2d0`
  - add static asserts for offsets `+0x210` and `+0x2d0`
- non-claims:
  - this does not allocate or start APSTA
  - this does not create the datapath owner object
  - this does not return role-7 success
  - this does not publish HostAP capability

### 38. APSTA init/start populates resource slots before role-7 success
- anomaly_id: `A-APSTA-INIT-START-RESOURCE-CONTRACT-054`
- layer: APSTA factory/init/start scaffold after CR-126
- Apple contract:
  - `withOptions(core, mac, role, bsdName)` allocates APSTA object size
    `0x138` and calls APSTA `init(core, mac, role, bsdName)`
  - `init(...)` allocates private APSTA state, stores owner/core at
    `state+0x218`, clears feature bytes `+0x32a/+0x32b`, creates timer sources
    at `+0x70/+0x78`, and fills resource references at `+0x210`, `+0x228`,
    `+0x240`, `+0x248`, `+0x250`, `+0x258`, and `+0x260`
  - `init(...)` sets defaults at `+0x14`, `+0x20c`, `+0x268`, `+0x2a4`, and
    four queue-map entries at `+0x2a8..+0x2b4`
  - `start(core, RegistrationInfo*)` stores work queue `+0x330`, bus/provider
    `+0x2c8`, datapath owner `+0x2d0`, and passes a stack queue config to
    datapath owner vtable `+0x118`
  - the queue config passes pointers to `+0x2a8`, `+0x300`, `+0x2f8`,
    `+0x2e8`, `+0x2f0`, `+0x320`, `+0x2d8`, and `+0x2e0`
- local mismatch before CR-127:
  - APSTA init/start fields were still anonymous padding
  - there was no compiled local witness for the queue config carrier passed to
    datapath owner `+0x118`
- exact correction:
  - name all recovered init/start state fields in `AirportItlwmAPSTAStateBlock`
  - add `AirportItlwmAPSTAStartQueueConfigLayout`
  - add static asserts for each recovered state offset and config offset
- non-claims:
  - this does not instantiate APSTA at runtime
  - this does not create queues locally
  - this does not register APSTA with BSD/Skywalk
  - this does not return role-7 success
  - this does not publish HostAP capability

### 39. AP/SoftAP control flags are fixed APSTA state fields
- anomaly_id: `A-APSTA-HOSTAP-STATE-MACHINE-FIELDS-055`
- layer: APSTA HostAP control-state scaffold after CR-127
- Apple contract:
  - `holdSoftAPPowerAssertion()` writes byte `state+0x0c`
  - `setHOST_AP_MODE_HIDDEN(...)` writes byte `state+0x0d`
  - AP shutdown/hidden-mode paths clear byte `state+0x0e`
  - `configureLowPowerModeExit()` checks 32-bit `state+0xb4`
  - `setHostApModeInternal(...)` tests/writes `state+0x26c` and
    `state+0x270`
  - successful AP enable sets `state+0x26c = 1`, clears `state+0x20c`, and
    updates `state+0x14`
  - `setHOST_AP_MODE(...)` coordinates proximity/NAN owners at core expansion
    `+0x2c28`, `+0x74f0`, and `+0x74f8` around `setHostApModeInternal`
- local mismatch before CR-128:
  - AP/SoftAP control bytes `+0x0c/+0x0d`, power field `+0xb4`, and
    transition field `+0x270` were not named or asserted
  - docs covered queue/init resources but not AP-mode control flags
- exact correction:
  - name `powerAssertionFlag0c`
  - name `hiddenNetworkFlag0d`
  - split `softapPowerStateB4` out of the `+0xb0` runtime area
  - name `hostApTransitionState270`
  - add static asserts and YAML/prose documentation
- non-claims:
  - this does not implement HostAP mode transitions
  - this does not send firmware IOVAR sequences
  - this does not create or publish APSTA
  - this does not return role-7 success
  - this does not publish HostAP capability

### 40. Role-7 APSTA registration uses a fixed RegistrationInfo carrier
- anomaly_id: `A-APSTA-REGISTRATION-INFO-CARRIER-056`
- layer: APSTA role-7 registration carrier scaffold after CR-128
- Apple contract:
  - after APSTA owner creation and feature gates, `setVIRTUAL_IF_CREATE`
    zeroes a `0x130`-byte `RegistrationInfo` stack block
  - it calls APSTA `initRegistrationInfo(info, 1, 0x130)`
  - it writes APSTA-specific fields at `info+0x0c`, `+0x14`, `+0x24`,
    `+0x30`, `+0x38`, `+0x40`, and `+0x108`
  - `info+0x30` carries BSD prefix `"ap"` and `info+0x38` carries BSD unit `1`
  - `info+0x108..+0x10d` receives the six-byte APSTA hardware address from
    APSTA vtable `+0xcf8`
  - the populated carrier is passed to
    `AppleBCMWLANIO80211APSTAInterface::start(core, info)`
- local mismatch before CR-129:
  - local `IOSkywalkEthernetInterface::RegistrationInfo` was only an opaque
    304-byte pad
  - APSTA-specific registration producer offsets were not asserted anywhere
- exact correction:
  - add packed `AirportItlwmAPSTARegistrationInfoLayout`
  - add constants for size, init type, APSTA fixed field values, and options
    qword
  - add static asserts for all recovered offsets and carrier size
- non-claims:
  - this does not call APSTA `start`
  - this does not instantiate APSTA at runtime
  - this does not return role-7 success
  - this does not publish HostAP capability

### 41. APSTA Ethernet registration uses four TX queues plus completion queues
- anomaly_id: `A-APSTA-REGISTER-ETHERNET-QUEUE-LIST-057`
- layer: APSTA start/registration queue-list scaffold after CR-129
- Apple contract:
  - APSTA `start(core, RegistrationInfo*)` reads `numTxQueues` from
    `state+0x2a4`
  - APSTA init sets `numTxQueues = 4`
  - start copies TX submission queues from `state+0x300..+0x318` into a stack
    queue list
  - start appends TX completion queue `state+0x2e8` and RX completion queue
    `state+0x2f0`
  - start calls `registerEthernetInterface(info, queueList, numTxQueues + 2,
    state+0x2d8, state+0x2e0, 0)`
  - failure cleanup removes work sources for TX queues, TX/RX completion
    queues, and multicast queue
- local mismatch before CR-130:
  - local scaffold had queue storage fields but no queue-list carrier witness
  - future APSTA start implementation could accidentally collapse APSTA to the
    primary STA one-queue topology
- exact correction:
  - add `AirportItlwmAPSTARegisterQueueListLayout`
  - add constants for two completion queues and six effective registration
    queue entries
  - add static asserts for TX/RX completion offsets and queue-list size
- non-claims:
  - this does not create queues
  - this does not register APSTA
  - this does not call APSTA `start`
  - this does not return role-7 success

### 42. APSTA public SoftAP/SAP carriers use fixed offsets and return codes
- anomaly_id: `A-APSTA-SOFTAP-PUBLIC-CARRIER-CONTRACT-058`
- layer: APSTA public SAP/SoftAP carrier scaffold after CR-130
- Apple contract:
  - `getSTATE(...)` writes state value `4` at output `+0x04`
  - `getPEER_CACHE_MAXIMUM_SIZE(...)` writes value `8` at output `+0x04`
  - `getHOST_AP_MODE_HIDDEN(...)` returns raw `0x16` for NULL and writes value
    `1` for non-NULL
  - `getSOFTAP_PARAMS(...)` copies APSTA state offsets
    `+0x18/+0x1c/+0x20/+0x24/+0x68/+0x10/+0x0e/+0x28` into output offsets
    `+0x04/+0x08/+0x0c/+0x10/+0x14/+0x16/+0x17/+0x18`
  - `getSOFTAP_STATS(...)` copies `0x58` bytes from `state+0x1b0`
  - `setSOFTAP_WIFI_NETWORK_INFO_IE(...)` accepts only input byte `+0x03`
    below `0x21`, copies exactly `0x24` bytes into `state+0x2c`, and otherwise
    returns `0xe00002c2`
  - `setSOFTAP_TRIGGER_CSA(...)` returns `6` when AP is not ready, returns raw
    `0x16` for NULL, accepts parsed channel specs below `0x10000` for IOVAR
    `csa`, and rejects parsed channel specs at or above `0x10000`
  - `setRSN_CONF(...)` rejects with `0xe00002d5` when `state+0x29b` bit `0x10`
    is set
  - `setSTA_AUTHORIZE(...)` rejects NULL with `0xe00002c2`
  - `setSTA_DEAUTH(...)` tailcalls vtable slot `+0x1040`
- local mismatch before CR-131:
  - SoftAP params output and Wi-Fi network-info input carrier shapes were
    prose-only
  - public constants and reject codes were not compiled into the APSTA witness
  - `state+0x29b` was still anonymous padding despite the RSN_CONF gate
- exact correction:
  - add `AirportItlwmAPSTASoftAPParamsOutputLayout`
  - add `AirportItlwmAPSTASoftAPWifiNetworkInfoCarrierLayout`
  - name `AirportItlwmAPSTAStateBlock::rsnConfGate29b`
  - add constants and static asserts for recovered carrier offsets, copy size,
    length threshold, and return codes
- non-claims:
  - this does not instantiate APSTA
  - this does not enable AP/SoftAP runtime paths
  - this does not force RSN success
  - this does not change primary STA association/data behavior

### 43. SAP/APSTA vtable aliases require base-vs-concrete guards
- anomaly_id: `A-APSTA-SAP-VTABLE-ALIAS-CONTRACT-059`
- layer: SAP/APSTA vtable ABI scaffold after CR-131
- Apple contract:
  - `IO80211SapProtocol` vtable spans recovered slots `280..519`
  - base SAP extension starts at slot `481`, byte offset `0x0f08`
  - base `IO80211SapProtocol` slot `483`, byte offset `0x0f18`, is
    `IO80211VirtualInterface::forwardPacket(IO80211NetworkPacket*)`
  - concrete `AppleBCMWLANIO80211APSTAInterface` slot `465`, byte offset
    `0x0e88`, is `AppleBCMWLANIO80211APSTAInterface::forwardPacket(...)`
  - concrete APSTA slot `488`, byte offset `0x0f40`, is
    `setMacAddress(ether_addr&)`
  - concrete APSTA public SAP/SoftAP surface continues through slot `531`,
    byte offset `0x1098`
  - APSTA destructor calls `IO80211SapProtocol` destructor
- local mismatch before CR-132:
  - the local SAP contract header had method slot constants but not explicit
    base-vs-concrete alias constants
  - there were no byte-offset asserts guarding the forwardPacket collision
- exact correction:
  - add base `IO80211SapProtocol` vtable range constants
  - add concrete `AppleBCMWLANAPSTAVtableSlot*` aliases
  - add byte-offset constants and static asserts for key dispatch targets
  - keep `IO80211SapProtocol` as a contract header only
- non-claims:
  - this does not define a C++ SAP base class
  - this does not instantiate APSTA
  - this does not change runtime vtable dispatch
  - this does not enable role-7 success

### 44. APSTA forwardPacket selects TX subqueue from packet metadata
- anomaly_id: `A-APSTA-FORWARD-PACKET-TX-QUEUE-SELECTION-060`
- layer: APSTA TX datapath scaffold after CR-132
- Apple contract:
  - concrete APSTA `forwardPacket(IO80211NetworkPacket*)` is slot `465`
  - the method calls a packet metadata helper
  - it selects a TX subqueue pointer from
    `state+0x300 + ((metadata >> 4) & 0xff8)`
  - it calls the selected queue vtable entry `+0x318` with the packet
- local mismatch before CR-133:
  - `txSubQueues` at `state+0x300` was named, but the forwardPacket selector
    shift, mask, and selected-queue submit vtable offset were not compiled
- exact correction:
  - add constants for selector shift `4`, selector mask `0xff8`, TX subqueue
    base `0x300`, and queue submit vtable offset `0x318`
  - assert that the TX subqueue base constant matches
    `AirportItlwmAPSTAStateBlock::txSubQueues`
- non-claims:
  - this does not transmit APSTA packets
  - this does not enable APSTA runtime forwarding
  - this does not clamp or fallback to queue zero
  - this does not change primary STA TX behavior

### 45. APSTA datapath metric accessors read queue-internal operands
- anomaly_id: `A-APSTA-DATAPATH-METRIC-ACCESSORS-061`
- layer: APSTA datapath accessor scaffold after CR-133
- Apple contract:
  - `getTxHeadroom()` returns `0`
  - `getTxQueueDepth()` reads first TX subqueue from `state+0x300`
  - missing TX subqueue returns `0`
  - existing TX subqueue reads nested object pointer at `queue+0x168` and
    returns dword `nested+0x28`
  - `getRxQueueCapacity()` reads RX completion queue from `state+0x2f0`
  - missing RX completion queue returns `0`
  - existing RX completion queue reads nested object pointer at `queue+0x138`
    and returns dword `nested+0x10`
- local mismatch before CR-134:
  - these offsets were present only in YAML/prose
  - compiled APSTA scaffold did not have constants for the nested metric
    operands
- exact correction:
  - add constants for zero headroom and missing-queue metric value
  - add constants for TX queue nested object/value offsets `0x168` and `0x28`
  - add constants for RX queue nested object/value offsets `0x138` and `0x10`
- non-claims:
  - this does not call generic queue capacity APIs
  - this does not synthesize nonzero metrics
  - this does not enable APSTA datapath
  - this does not change primary STA queue metrics

### 46. APSTA datapath lifecycle uses fixed owner/queue vtable offsets
- anomaly_id: `A-APSTA-DATAPATH-LIFECYCLE-VTABLE-OFFSETS-062`
- layer: APSTA datapath lifecycle scaffold after CR-134
- Apple contract:
  - `enableDatapath()` calls datapath owner `state+0x2d0` vtable `+0x120`
  - it starts TX completion queue `state+0x2e8` with queue vtable `+0x150`
  - it starts RX completion queue `state+0x2f0` with queue vtable `+0x150`
  - it arms RX completion queue with vtable `+0x298` and arguments `0,0`
  - missing completion queue returns `0xe00002bc`
  - `disableDatapath()` calls datapath owner `state+0x2d0` vtable `+0x128`
  - it stops RX completion queue first with vtable `+0x158`, then TX
    completion queue with vtable `+0x158`
- local mismatch before CR-135:
  - APSTA storage fields existed, but lifecycle vtable offsets were prose-only
- exact correction:
  - add constants for owner enable/disable offsets `0x120/0x128`
  - add constants for completion queue start/stop offsets `0x150/0x158`
  - add constant for RX completion arm offset `0x298`
  - add constants for RX arm args `0,0` and missing queue return
    `0xe00002bc`
- non-claims:
  - this does not start queues at runtime
  - this does not allocate the datapath owner
  - this does not treat missing queues as success
  - this does not change primary STA datapath lifecycle

### 47. APSTA start registers queue work sources before Ethernet registration
- anomaly_id: `A-APSTA-START-WORKSOURCE-REGISTRATION-063`
- layer: APSTA start/work-source scaffold after CR-135
- Apple contract:
  - APSTA `start` calls datapath owner `state+0x2d0` vtable `+0x118` with the
    queue config carrier
  - `numTxQueues >= 7` enters the reference invalid/trap path before queue-list
    registration
  - multicast queue `state+0x320`, when present, is added to work queue
    `state+0x330`
  - each TX submission queue from `state+0x300` is added through work queue
    vtable `+0x140`
  - TX completion `state+0x2e8` and RX completion `state+0x2f0` are added
    through the same vtable `+0x140`
  - `registerEthernetInterface` is called with queue count `numTxQueues + 2`,
    TX/RX pools `state+0x2d8/+0x2e0`, and flags `0`
  - registration failure removes TX queues, TX completion, RX completion, and
    multicast through work queue vtable `+0x148`
- local mismatch before CR-136:
  - these start-time work-source offsets and register flags were prose-only
- exact correction:
  - add constants for owner config offset `0x118`
  - add constants for work queue add/remove offsets `0x140/0x148`
  - add constants for TX queue trap threshold `7`, max accepted TX queues `6`,
    and register flags `0`
- non-claims:
  - this does not call APSTA `start`
  - this does not add work sources at runtime
  - this does not call `registerEthernetInterface`
  - this does not mask registration failure

### 48. APSTA teardown releases the exact timer/resource/queue fields it owns
- anomaly_id: `A-APSTA-TEARDOWN-RESOURCE-CLEANUP-064`
- layer: APSTA stop/freeResources scaffold after CR-136
- Apple contract:
  - `freeResources()` cancels timer sources `state+0x70` and `state+0x78`
    through vtable `+0x158`, releases them through vtable `+0x28`, and clears
    both fields
  - `freeResources()` releases retained resources at `state+0x240`,
    `+0x248`, `+0x250`, `+0x260`, and `+0x258` through vtable `+0x28` when
    present and clears each field
  - `stop(IOService*)` iterates TX queues from `state+0x300` while
    `i < state+0x2a4`
  - for every non-null TX queue, stop uses queue vtable `+0x158`, work queue
    remove vtable `+0x148`, release vtable `+0x28`, and NULL-clears the slot
  - TX completion `state+0x2e8` and RX completion `state+0x2f0` use the same
    stop/remove/release/clear sequence
  - multicast queue `state+0x320` is stopped, removed through work queue vtable
    `+0x148`, removed again through direct `IO80211WorkQueue::removeWorkSource`,
    released, and cleared
  - final tailcall goes to super stop vtable offset `+0x5d8`
- local mismatch before CR-137:
  - APSTA fields and start add/remove constants existed, but teardown release
    and clear operands were prose-only
- exact correction:
  - add constants for object release `0x28`, timer/queue stop `0x158`, work
    queue remove `0x148`, multicast direct-remove marker, super stop `0x5d8`,
    and teardown NULL
  - add constants/static asserts for the timer, retained resource, completion
    queue, TX queue base, and multicast queue offsets
  - add a local reference note for the recovered teardown sequence
- non-claims:
  - this does not enable APSTA runtime teardown
  - this does not allocate or release APSTA queues at runtime
  - this does not add fallback cleanup beyond reference
  - this does not change primary STA stop/free behavior

### 49. APSTA reset and initSoftAPParameters carry fixed SoftAP defaults
- anomaly_id: `A-APSTA-RESET-SOFTAP-DEFAULTS-065`
- layer: APSTA reset/default scaffold after CR-137
- Apple contract:
  - `reset()` clears `state+0x26c` and byte `state+0x329`
  - `reset()` calls `AppleBCMWLANCore::setConcurrencyState(4, false)`
  - `reset()` zeroes `state+0xb8` for `0xf0` bytes, clears `state+0x0` and
    `state+0xb0`, and calls `setPowerSaveState(0, 0xa)`
  - `reset()` invokes timer sources `state+0x70/+0x78` through vtable `+0x218`
  - `reset()` clears stats `state+0x1b0..+0x207` and runtime qwords
    `state+0x90/+0x98/+0xa0`
  - `initSoftAPParameters()` clears the same stats block, clears
    `state+0x1a8`, zeroes `state+0xb8` for `0xf0` bytes, and clears
    `state+0x0`
  - `initSoftAPParameters()` writes fixed defaults:
    `state+0x16 = 1`, `state+0x18 = 0x0f`, `state+0x1c = 0x1e`,
    `state+0x20 = 0x708`, `state+0x24 = 0x0a`, and `state+0x28 = 3`
  - it calls `setBeaconInterval(state+0x14)` and applies DTIM through IOCTL
    `0x4e` via commander `state+0x228` when `state+0x16 != state+0x6a`
- local mismatch before CR-138:
  - reset/default operands were only partially documented
  - fields `+0x0`, `+0x16`, `+0x6a`, `+0x90`, `+0x98`, and `+0xa0` were still
    anonymous in the local state block witness
- exact correction:
  - name recovered reset/default fields in `AirportItlwmAPSTAStateBlock`
  - add constants for reset clears, runtime/stat sizes, timer vtable `+0x218`,
    concurrency/power-save arguments, SoftAP default values, and IOCTL `0x4e`
  - add a local reference note for the recovered reset/initSoftAPParameters
    sequence
- non-claims:
  - this does not call APSTA `reset()` or `initSoftAPParameters()` at runtime
  - this does not force AP state or concurrency state
  - this does not synthesize AP defaults from primary STA code
  - this does not change primary STA reset behavior

### 50. APSTA beacon interval and DTIM use fixed IOCTL carriers
- anomaly_id: `A-APSTA-BEACON-DTIM-IOCTL-CARRIERS-066`
- layer: APSTA beacon/DTIM IOCTL scaffold after CR-138
- Apple contract:
  - `setBeaconInterval(uint16_t)` compares the request to applied interval
    `state+0x68`; equal values skip IOCTL
  - changed beacon interval uses commander `state+0x228`, payload length `4`,
    and IOCTL `0x4c`
  - async beacon path stores `handleSetBcnIntervalAsyncCallBack` at callback
    context `+0x8` and cookie `0` at `+0x10`
  - sync beacon path passes no callback
  - beacon success writes requested interval to `state+0x68`
  - DTIM apply uses source `state+0x16`, applied field `state+0x6a`, payload
    length `4`, commander `state+0x228`, and IOCTL `0x4e`
  - DTIM success writes `state+0x6a = state+0x16`
  - both async callbacks return on status `0`; for nonzero status they log and
    emit rxPayload bytestream telemetry using pointer `+0x0`, length `+0x8`,
    and telemetry resource `state+0x210`
- local mismatch before CR-139:
  - IOCTL numbers and payload/callback offsets were not compiled as APSTA
    contract constants
  - `state+0x68` remained a generic SoftAP params field instead of the applied
    beacon interval field
- exact correction:
  - add constants for IOCTL `0x4c`, IOCTL `0x4e`, payload size `4`, callback
    offsets, rxPayload offsets, and applied beacon/DTIM fields
  - add `AirportItlwmAPSTACommandPayloadHeadLayout`
  - rename local `state+0x68` field to applied beacon interval
  - add a local reference note for the recovered beacon/DTIM sequence
- non-claims:
  - this does not send beacon or DTIM IOCTLs at runtime
  - this does not write applied beacon/DTIM fields at runtime
  - this does not suppress callback errors
  - this does not enable APSTA AP mode

### 51. APSTA HostAP success tail resets state and schedules monitor timer
- anomaly_id: `A-APSTA-HOSTAP-SUCCESS-TAIL-067`
- layer: APSTA HostAP success-tail scaffold after CR-139
- Apple contract:
  - successful AP bring-up writes `state+0x26c = 1`
  - it clears `state+0x20c` and `state+0x88`
  - it calls `handleAPStatsUpdates(state+0x70)`
  - it schedules AP monitor timer `state+0x78` through vtable `+0x1d0` with
    interval `0x3e8`
  - it reads network-data flags at input `+0x4`
  - flags bit `8` selects beacon interval `0x64`; otherwise `0x12c` is written
    to `state+0x14`
  - flags bit `9` sends IOVAR `closednet` through commander `state+0x228` with
    4-byte payload value `1`
  - the common tail continues to `initSoftAPParameters()`
- local mismatch before CR-140:
  - HostAP success-tail state/timer/flag/closednet operands were prose-only
- exact correction:
  - add constants for state offsets/values, monitor timer vtable/interval,
    network-data flag offset, bits `8/9`, beacon interval values, and closednet
    payload
  - add static asserts tying HostAP offsets to local state layout
  - add a local reference note for the recovered success-tail sequence
- non-claims:
  - this does not set AP-up state at runtime
  - this does not schedule timers at runtime
  - this does not send `closednet`
  - this does not enable HostAP mode

### 52. APSTA HostAP max-assoc and vendor IE layer precedes AP-up
- anomaly_id: `A-APSTA-HOSTAP-ASSOC-VENDOR-IE-LAYER-068`
- layer: APSTA HostAP max-assoc/vendor-IE scaffold after CR-140
- Apple contract:
  - `setHostApModeInternal` reads max-assoc through the core expansion chain
    `state+0x218 -> +0x128 -> +0x1558 -> +0x10 -> +0xb4`
  - it stores that value at `state+0x8`, calls `setMaxAssoc`, then invokes
    APSTA vtable `+0xb18` with selector `0x57`, payload `state+0x8`, and
    payload size `4`
  - `setMaxAssoc` uses `state+0x0/+0x4/+0x8`, sends IOVAR `maxassoc`
    through commander `state+0x228`, and uses payload `state+0x0 +
    requested` with size `4`
  - network-data vendor IE length/data are at `+0x2dc/+0x2e0`
  - nonzero vendor IE length calls `programVendorIEList`; zero length calls
    `programAppleVendorIE`
  - `programVendorIEList` allocates an `0x814` byte carrier, writes fixed
    header qwords `0x1a00000001` and `0x400000001`, copies each IE, calls
    `AppleBCMWLANCore::setVendorIE`, and frees the carrier
  - `programAppleVendorIE` uses `vndr_ie`, deletes existing Apple OUI entries,
    sends a fixed `0x18` byte Apple capability IE, and can append extended
    capability data from APSTA state offsets `+0x2c/+0x2e/+0x2f/+0x30/+0x50/
    +0x51/+0x59`
- local mismatch before CR-141:
  - max-assoc state offsets, selector `0x57`, vendor IE list offsets, carrier
    size/header fields, and Apple `vndr_ie` operands were prose-only
- exact correction:
  - add constants for max-assoc source/fields, selector `0x57`, vtable
    `+0xb18`, network-data vendor IE offsets, vendor IE carrier layout, and
    Apple `vndr_ie` command buffers
  - add layout witnesses for the `0x814` byte vendor IE carrier and `0x52`
    byte Apple vendor IE set buffer
  - add static asserts tying APSTA state offsets `+0x0/+0x4/+0x8/+0x2c/+0x50/
    +0x51/+0x59` to the recovered layer
- non-claims:
  - this does not call `setMaxAssoc` at runtime
  - this does not send selector `0x57`
  - this does not call `setVendorIE` or `vndr_ie`
  - this does not enable HostAP mode

### 53. APSTA enableAPInterface carries RRM/WNM/MPDU/scb_probe side effects
- anomaly_id: `A-APSTA-ENABLE-AP-INTERFACE-LAYER-069`
- layer: APSTA AP-enable scaffold after CR-141
- Apple contract:
  - `enableAPInterface()` conditionally sends `rrm_bcn_req_thrtl_win` and
    `rrm_bcn_req_max_off_chan_time` using feature flag `0x15`, config byte
    `+0xe2`, 4-byte zero payloads, and commander `state+0x228`
  - it conditionally sends `wnm` using feature flag `0x19`, config byte
    `+0xe3`, a 4-byte zero payload, and commander `state+0x228`
  - it reads boot arg `wlan.ap.maxmpdu` size `4`; failed read maps to
    `0xffffffff`, nonzero success calls `configureMPDUSize`, and zero success
    skips the override
  - it ORs `0x10000` into core-private offset `+0x2890`
  - it calls APSTA vtable `+0xe70` with arguments `(2, 1)`
  - it sends `scb_probe` with payload qword `0xf0000001e`, dword `5`, and size
    `0x0c`, using async completion when supported and sync otherwise
  - async completion context stores owner at `+0x0`, callback at `+0x8`, and
    cookie `0` at `+0x10`
  - it notifies core event id `0x1e`, caps interface name length below `0x11`,
    calls APSTA vtable `+0xb18` selector `4`, adds event bit `5`, and tailcalls
    `writeEventBitField()`
- local mismatch before CR-142:
  - AP-enable RRM/WNM/MPDU/scb_probe/event operands were prose-only
- exact correction:
  - add constants for feature gates, config byte offsets, command names,
    payload sizes/defaults, core-private bit, vtable selectors, notification
    operands, and event bit
  - add layout witnesses for the `0x0c` byte `scb_probe` payload and `0x18`
    byte command completion context
- non-claims:
  - this does not call `enableAPInterface` at runtime
  - this does not send RRM, WNM, MPDU, or `scb_probe` commands
  - this does not publish AP link-up or write event bits
  - this does not enable HostAP mode

### 54. APSTA hidden mode and SoftAP power assertion have fixed operands
- anomaly_id: `A-APSTA-HIDDEN-POWER-ASSERTION-LAYER-070`
- layer: APSTA hidden/power scaffold after CR-142
- Apple contract:
  - `setHOST_AP_MODE_HIDDEN` requires AP-up state `state+0x26c != 0`, returns
    `6` when AP is not up, and returns raw invalid argument `0x16` for null
    input
  - hidden value is read from input `+0x4` and must be `0` or `1`
  - it sends IOVAR `closednet` through commander `state+0x228` with 4-byte
    payload carrying the requested hidden value
  - on success it writes `state+0x0d = (hidden != 0)`
  - when hidden is cleared and AP remains up, it calls
    `setPowerSaveState(0, 9)`, clears `state+0x0e`, and calls
    `holdSoftAPPowerAssertion`
  - `holdSoftAPPowerAssertion` writes `state+0x0c = 1` and notifies core event
    `0x8d` through resource `state+0x218 -> +0x128 -> +0x2c20` with payload
    value `1`, payload size `4`, and flag `1`
- local mismatch before CR-143:
  - hidden-mode validation, return, state write, power-save, and power
    assertion notification operands were prose-only
- exact correction:
  - add constants for hidden input offset/range, required AP-up state, return
    values, `closednet` payload size, post-success fields, power-save args, and
    hold-power notification operands
  - add static asserts tying `state+0x0c/+0x0d/+0x0e/+0x26c` to the recovered
    contract
- non-claims:
  - this does not call hidden-mode setters at runtime
  - this does not send `closednet`
  - this does not change power-save state
  - this does not hold power assertions at runtime

### 55. APSTA channel, CSA, and STA-control methods have fixed carriers
- anomaly_id: `A-APSTA-CHANNEL-CSA-STA-CONTROL-LAYER-071`
- layer: APSTA public channel/CSA/STA-control scaffold after CR-143
- Apple contract:
  - `getCHANNEL(...)` uses virtual IOCTL get selector `0x1d`, 0x0c-byte RX
    payload/range `0x0000000c000c000c`, writes channel number to output
    `+0x08`, and ORs flags `0x08` for channels below `0x0f` or `0x10`
    otherwise
  - `setCHANNEL(...)` rejects NULL and channels `>= 0x100` with raw `0x16`,
    maps flags `0x02/0x04/0x400` to bandwidth `2/3/4`, calls
    `getChanSpec`, returns `0xe00002c2` on zero chanspec, and sends 4-byte
    IOVAR `chanspec`
  - `setSOFTAP_TRIGGER_CSA(...)` requires `state+0x26c != 0` and
    `state+0x329 & 1`, rejects NULL with `0x16`, accepts parsed chanspec
    values below `0x10000`, rejects parsed chanspec values `>= 0x10000`, and
    sends a 6-byte IOVAR `csa`
  - `setSTA_AUTHORIZE(...)` rejects NULL with `0xe00002c2`, uses MAC at
    input `+0x08`, and selects virtual IOCTL `0x79` or `0x7a` from flag
    input `+0x04`
  - `setSTA_DISASSOCIATE(...)` occupies APSTA slot `522`/byte `0x1050`,
    builds a 0x0c-byte payload from input `+0x04/+0x08/+0x0c` with sentinel
    word `0xaaaa`, and calls virtual IOCTL set selector `0xc9`
  - `setSTA_DEAUTH(...)` occupies APSTA slot `523`/byte `0x1058` and tailcalls
    byte offset `+0x1040`
- local mismatch before CR-145:
  - channel, CSA, and STA-control carriers were partially prose-only
  - the CSA threshold was previously documented in reverse: the reference
    accepts `< 0x10000` and rejects `>= 0x10000`
  - `setSTA_DISASSOCIATE` slot and payload were not recorded in the local SAP
    contract header
- exact correction:
  - add constants and layout witnesses for channel data, RX payload range,
    CSA input/payload, STA authorize input, and STA disassociate input/payload
  - add SAP vtable slot/byte-offset constants for channel and STA-control
    methods
  - correct YAML/prose CSA threshold documentation
- non-claims:
  - this does not enable role-7 APSTA success
  - this does not send channel, CSA, authorize, disassociate, or deauth
    commands at runtime
  - this does not force AP state or STA state

### 56. APSTA HostAP control and power wrapper has fixed owner/gate operands
- anomaly_id: `A-APSTA-HOSTAP-CONTROL-POWER-LAYER-072`
- layer: APSTA HostAP control/power scaffold after CR-145
- Apple contract:
  - `setHOST_AP_MODE(...)` reads network-data mode at input `+0x1c`
  - neighbouring owners are read from core-private offsets `+0x2c28`,
    `+0x74f0`, and `+0x74f8`
  - feature gate `0x46` controls whether proximity/NAN owners are brought
    down before `setHostApModeInternal(input)` or brought up after the
    disable/zero-mode internal call
  - bring-up is additionally gated by core-private `+0x2890 & 1` and dword
    `+0x4d8c` being `4` or `1`
  - `hostAPPowerOff()` returns `0` when AP-up state `state+0x26c` is zero
  - with no associated stations (`state+0x00 == 0`), `hostAPPowerOff()` calls
    `setPowerSaveState(0, 0x0c)`, clears `state+0x0e`, calls
    `setHostApModeInternal(NULL)`, and notifies core event id `1` with null
    payload, size `0`, flag `1`
  - with associated stations and concurrency disabled, it calls
    `setPowerSaveState(3, 3)`
  - `isSoftAPConcurrencyEnabled()` requires feature `0x46` and core-private
    byte `+0x4d59 & 0x1b`
  - `configureLowPowerModeExit()` returns when `state+0xb4 == 0`; otherwise it
    dispatches low-power exit through work-queue vtable `+0x130` and successful
    exit clears `state+0xb4`
- local mismatch before CR-146:
  - hostap control/power offsets and neighbouring owner gates were only
    described in YAML/prose
  - network-data mode `+0x1c`, NAN owner offsets, power-off notify operands,
    concurrency mask, and low-power exit gate were not compiled witnesses
- exact correction:
  - add constants for network-data mode/vendor IE offsets, feature gates,
    neighbouring owners, bring-up private gates, power-off paths, concurrency
    mask, and low-power exit work-queue gate
  - add `AirportItlwmAPSTAHostApModeNetworkDataLayout`
  - extend core-expansion witness through proximity/APSTA/NAN/NAN-data owners
  - add static asserts for all recovered offsets
- non-claims:
  - this does not call `setHOST_AP_MODE`
  - this does not bring up/down proximity or NAN owners
  - this does not call `hostAPPowerOff`
  - this does not run low-power exit at runtime

### 57. APSTA public SAP slot surface must be complete before owner class
- anomaly_id: `A-APSTA-PUBLIC-SAP-SLOT-SURFACE-073`
- layer: APSTA public SAP ABI scaffold after CR-146
- Apple contract:
  - APSTA concrete public getters occupy slots `505..516`, byte offsets
    `0x0fc8..0x1020`
  - APSTA concrete public setters occupy slots `517..531`, byte offsets
    `0x1028..0x1098`
  - every public SAP getter/setter slot from `getSSID` through
    `setMIS_MAX_STA` has a fixed concrete slot and byte offset in the resolved
    AppleBCMWLAN APSTA vtable
- local mismatch before CR-147:
  - only a subset of concrete APSTA slot aliases had local AppleBCMWLAN
    constants and byte-offset asserts
  - a future local APSTA C++ class could still accidentally leave remaining
    public methods at base/reserved offsets
- exact correction:
  - add AppleBCMWLAN APSTA slot constants for all public getter/setter slots
    `505..531`
  - add byte-offset constants and static asserts for every slot in
    `include/Airport/IO80211SapProtocol.h`
  - add reference note and YAML/prose documentation for the complete surface
- non-claims:
  - this does not define the final APSTA C++ owner class
  - this does not route runtime calls through these slots
  - this does not implement remaining method bodies

### 58. APSTA simple public body contracts have fixed offsets and returns
- anomaly_id: `A-APSTA-PUBLIC-SIMPLE-BODY-CONTRACTS-074`
- layer: APSTA public simple body scaffold after CR-147
- Apple contract:
  - `getSSID(...)` reads length from `state+0x274`, rejects lengths greater
    than `0x20` with raw `0x16`, writes output length at `+0x04`, copies
    bytes from `state+0x278` to output `+0x08`, and returns `0`
  - `getSTATE(...)` writes value `4` at output `+0x04` and returns `0`
  - `getOP_MODE(...)` returns raw `0x16` for NULL, writes type `1` at output
    `+0x00`, writes mode `8` at output `+0x04` when `state+0x26c != 0`,
    otherwise writes `0`, and returns `0`
  - `getPEER_CACHE_MAXIMUM_SIZE(...)` writes value `8` at output `+0x04`
  - `getHOST_AP_MODE_HIDDEN(...)` returns raw `0x16` for NULL and writes value
    `1` at output base
  - `getSOFTAP_PARAMS(...)` copies fields from state
    `+0x18/+0x1c/+0x20/+0x24/+0x68/+0x10/+0x0e/+0x28` to fixed output
    offsets and returns `0`
  - `getSOFTAP_STATS(...)` copies `0x58` bytes from `state+0x1b0`
  - `setSSID(...)` is logging-only, does not mutate SSID state, and returns
    `0`
  - `setPEER_CACHE_CONTROL(...)` calls
    `AppleBCMWLANCore::completePeerCacheControl(input, self)` via
    `state+0x218`, ignores the helper result, and returns `0`
  - `setSOFTAP_PARAMS(...)` has no null guard, uses input
    `+0x04/+0x08/+0x0c/+0x10/+0x14/+0x17/+0x18`, state
    `+0x0e/+0x18/+0x1c/+0x20/+0x24/+0x28/+0x68/+0x26c`, sentinel
    `0xffff`, power-save calls `(0,0)` and `(1,0)`, and returns `0`
  - `setSOFTAP_EXTENDED_CAPABILITIES_IE(...)` clears state
    `+0x50/+0x58/+0x60`, copies input `+0x00/+0x01/+0x09` to state
    `+0x50/+0x51/+0x59`, and returns `0`
  - `setMIS_MAX_STA(...)` calls `setMaxAssoc(input+0x00)` only when
    `state+0x26c != 0`, ignores the helper result, and returns `0`
- local mismatch before CR-148:
  - SSID state fields `+0x274/+0x278` were hidden inside a reserved block
  - opmode/state/peer-cache/hidden/simple setter body contracts were YAML or
    prose-only, not compiled local witnesses
  - simple setters with no reference null guard were not explicitly protected
    against accidental local guard insertion
- exact correction:
  - add constants and layout witnesses for SSID, state, opmode, peer-cache
    maximum, hidden mode, SoftAP stats, SoftAP ext-cap input, and MIS max-STA
    input
  - split the APSTA state block around `state+0x274/+0x278` while preserving
    `rsnConfGate29b` at `0x29b` and total size `0x338`
  - add static asserts tying the simple body offsets, fixed values, copy sizes,
    and helper-result policies to compiled witnesses
  - add reference note and YAML documentation for the simple body layer
- non-claims:
  - this does not route runtime calls through APSTA public methods
  - this does not enable APSTA/HostAP runtime
  - this does not implement station/key datapath methods such as
    `setCIPHER_KEY`, `getSTA_IE_LIST`, `getSTA_STATS`, or `getKEY_RSC`

### 59. APSTA station/key public bodies have fixed command-buffer contracts
- anomaly_id: `A-APSTA-STATION-KEY-BODY-CONTRACTS-075`
- layer: APSTA station/key body scaffold after CR-148
- Apple contract:
  - `getSTATION_LIST(...)` rejects NULL with raw `0x16`, AP-down with `0x39`,
    allocates a `0x100` byte maclist initialized with dword `0x2a`, uses
    virtual IOCTL get selector `0x9f`, returns `0xe00002bd` on allocation
    failure, returns `0xe00002d8` on async submit failure, and converts the
    BCM assoc list on sync success
  - `setCIPHER_KEY(...)` rejects AP-down with `6`, has no null guard after
    AP-up passes, reads cipher type at input `+0x08`, accepts only cipher
    types `3` and `5` for programming, returns success for cipher `0` and
    unsupported nonzero ciphers, maps to a `0xa4` byte `wl_wsec_key`, and uses
    virtual IOCTL set selector `0x2d`
  - `getSTA_IE_LIST(...)` rejects NULL with raw `0x16`, scans station entries
    from `state+0xb9` to `state+0x1a9` with stride `0x30` and 6-byte MAC
    compares, returns `2` when not found, uses IOVAR `wpaie`, and updates
    output `+0x0c` from output `+0x11 + 2`
  - `getSTA_STATS(...)` rejects AP-down with `0x39`, NULL with raw `0x16`,
    derives allocation size from core-private `+0x30c` with thresholds `7` and
    `0x0f`, uses IOVAR `sta_info`, copies RX fields
    `+0x58/+0x68/+0x54/+0x60` to output `+0x0c/+0x10/+0x14/+0x18`, and frees
    the allocation
  - `getKEY_RSC(...)` has no null guard, reads key index at input `+0x0e`,
    uses virtual IOCTL get selector `0xb7`, 8-byte TX payload, RX range
    `0x0000000800040008`, and writes output length/value at `+0x50/+0x54`
- local mismatch before CR-149:
  - command selectors, payload sizes, allocation sizes, station-table bounds,
    IOVAR names, key/RSC offsets, and return values were not compiled local
    witnesses
  - no-null-guard bodies were not explicitly protected against accidental local
    guard insertion
- exact correction:
  - add constants for selectors, payload sizes, allocation sizes, station-table
    offset/stride/end, IOVAR names, return values, and output offsets
  - add layout witnesses for maclist, station-table entry, STA IE data, STA
    stats data, key RSC data, and `wl_wsec_key`
  - add static asserts tying state gates, resources, station table bounds,
    names, and carriers to recovered reference values
- non-claims:
  - this does not execute APSTA station/key command tails at runtime
  - this does not change primary STA key programming
  - this does not force AP-up state or command success

### 59A. APSTA station/key public bodies stop at local unsupported tails
- anomaly_id: `A-APSTA-STATION-KEY-RUNTIME-BOUNDARY-204`
- layer: APSTA station/key body runtime bridge after CR-149
- Apple contract:
  - `setCIPHER_KEY(...)` has no NULL guard after AP-up state passes
  - `getSTA_IE_LIST(...)` searches the APSTA station table, copies the first
    six bytes of the station entry into output `+0x10`, runs the `wpaie` IOVAR
    query, and derives output length from output byte `+0x11` plus `2`
  - `getSTA_STATS(...)` runs the `sta_info` IOVAR query and publishes the
    recovered valid bit plus four RX-derived output fields only after success
  - `getKEY_RSC(...)` has no NULL guard, sends the recovered key index through
    virtual IOCTL get selector `0xb7`, and publishes an 8-byte RSC only after
    success
- local mismatch before this batch:
  - `getSTA_IE_LIST`, `getSTA_STATS`, and `getKEY_RSC` reached
    `kIOReturnUnsupported` instead of crossing a backend command boundary
  - `setCIPHER_KEY` still had a local NULL guard not present in the recovered
    AppleBCMWLAN body
- exact correction:
  - add `ItlHalService` query contracts for AP station IE, station stats, and
    key RSC
  - route the three public APSTA bodies through those contracts after their
    recovered gates and station-table checks
  - keep HAL defaults fail-closed, so missing AP backend support returns
    unsupported rather than fabricated data
  - remove the post-AP-up `setCIPHER_KEY` NULL guard
- non-claims:
  - this does not implement the final Intel AP firmware backend
  - this does not synthesize AP station IE/stat/RSC data
  - this does not change primary STA key programming

### 60. APSTA event/station-table producer layer has fixed entry and message contracts
- anomaly_id: `A-APSTA-EVENT-STATION-TABLE-CONTRACTS-076`
- layer: APSTA event/station-table producer scaffold after CR-149
- Apple contract:
  - station table is five `0x30` byte entries at `state+0xb8`
  - each entry has active byte `+0x00`, MAC `+0x01`, sleep state `+0x10`,
    AIHS flag `+0x20`, sharing flag `+0x24`, and Apple-station flag `+0x28`
  - `handleEvent(...)` reads event type/status/reason/auth/data-length/address/data
    at event `+0x04/+0x08/+0x0c/+0x10/+0x14/+0x18/+0x30`
  - association/reassociation events `8/10` with status/reason `0/0` post STA
    message id `0x0c` with payload size `0x114`
  - removal events `5/6/11/12` notify APSTA TX subqueues via vtable `+0x358`,
    clear the entry, and post STA message id `0x0d` with payload size `0x0c`
  - `postMessageForSTA(...)` dispatches through APSTA vtable `+0xb18` and
    notifies core owner `state+0x218 -> +0x128 -> +0x2c20` with flag `1`
  - `removeStaFromStaTable(index)` rejects indexes `>= 5` with `0xe00002bc`
    and clears six qwords from the entry
- local mismatch before CR-150:
  - station-table entry witness used a MAC-relative offset instead of full
    active-byte entry layout
  - event header offsets, STA post-message carriers, Apple IE/RSNXE parse
    operands, action-frame low-power operands, station-list mismatch behavior,
    and removal clear policy were not compiled local witnesses
- exact correction:
  - type `state+0xb8` as five APSTA station entries
  - add event header, STA association/removal message, action-frame, Apple IE,
    RSNXE, station-list mismatch, and removal constants/layouts
  - add static asserts tying those carriers to recovered offsets and state
    aliases
- non-claims:
  - this does not execute APSTA event handling at runtime
  - this does not enable APSTA/HostAP ownership
  - this does not alter primary STA association or key paths

### 60A. APSTA net80211 station-event bridge misses recovered message carriers
- anomaly_id: `A-APSTA-EVENT-STATION-MESSAGE-RUNTIME-205`
- layer: APSTA event/station-table runtime producer after CR-150
- Apple contract:
  - association/reassociation posts message id `0x0c` with a `0x114` byte
    payload containing MAC, associated count, flags, and RSNXE at `+0x10`
  - removal posts message id `0x0d` with a `0x0c` byte payload containing MAC
    and the post-removal associated count
  - `updateSTAAssocInfo(...)` derives Apple station flags from association IE
    TLVs, including the recovered Apple, Apple BS, and Apple device-info OUIs
  - `parseRSNXE(...)` copies full RSNXE element `0xf4` into the association
    message output area
- local mismatch before this batch:
  - AP association IE TLVs were not preserved for the APSTA producer
  - the net80211 bridge updated the station table but did not build/post the
    recovered STA association/removal message carriers
  - `state+0x80/+0x84` event MAC shadow was not updated on association
- exact correction:
  - preserve AP association IE TLVs in `ni_rsnie_tlv`
  - pass those TLVs into `AirportItlwmAPSTAOwner`
  - add compiled OUI witnesses for `00:17:f2`, `00:03:93`, and `00:a0:40`
  - build and post the packed association/removal message carriers through the
    existing IO80211 `postMessage` boundary
- non-claims:
  - this does not enable AP firmware mode or remove the APSTA station-event
    opt-out gate
  - this does not synthesize the not-yet-recovered auth-ind payload body
  - this does not alter primary STA association or key paths

### 61. APSTA power/offload/datapath tail has fixed payload and return contracts
- anomaly_id: `A-APSTA-POWER-OFFLOAD-DATAPATH-TAIL-077`
- layer: APSTA power/offload/datapath tail scaffold after CR-150
- Apple contract:
  - `configureMPDUSize` sends `ampdu_mpdu` with 4-byte payload only when
    core-private `+0x3fc == 2` and `+0x30c <= 4`
  - low-power exit and enter paths send `lphs_mode` payload values `0` and
    `1`, size `4`, no RX expected
  - ARP offload success sends `arp_hostip_clear`, then reads `state+0xac` and
    sends `arp_hostip` payload size `4`
  - `setBeaconDutyCycle` sends `rpsnoa` payload size `0x10` with header
    `0x100100101`, mode word `2`, and enable word at `+0x0e`
  - `configureBeaconDutyCycleParams` sends `rpsnoa` payload size `0x18` with
    header `0x300180101`, mode word `2`, dynamic byte at `+0x0e`, and rotated
    params qword at `+0x10`
  - `releaseSoftAPPowerAssertion` clears `state+0x0c` and notifies event
    `0x8d`, payload value `0`, size `4`, flag `1`
  - power-state durations accumulate at `state+0x1d0 + state * 0x10`, while
    timestamp lives at `state+0x1a8`
  - APSTA `enableDatapath` first checks vtable `+0xcf0`; if not enabled, it
    returns `0xe00002bc`, not success
  - `setMacAddress` sends `cur_etheraddr` only when interface id is not `-1`
    and AP-up state `state+0x26c` is zero
  - `configureSoftAPPeerStats` uses feature gate `0x7a`, IOVAR
    `softap_stats`, payload size `0x0e`, and successful callback writes
    `state+0x328 = cookie & 1`
- local mismatch before CR-151:
  - these constants and payload layouts were absent from compiled local APSTA
    witnesses
  - YAML incorrectly described the APSTA `enableDatapath` not-enabled branch as
    success
- exact correction:
  - add MPDU, low-power/ARP, RPSNOA, release assertion, stats,
    enable/disable, datapath, MAC, and peer-stats constants/layout witnesses
  - split APSTA state fields `state+0xac` and `state+0x328`
  - correct the documented `enableDatapath` not-enabled return to
    `0xe00002bc`
- non-claims:
  - this does not send APSTA IOVARs at runtime
  - this does not enable role-7 APSTA creation
  - this does not alter primary STA association, key, or data paths

### 62. APSTA monitor/power/stats layer has fixed timer and conversion contracts
- anomaly_id: `A-APSTA-MONITOR-POWER-STATS-CONTRACTS-078`
- layer: APSTA monitor/power/stats scaffold after CR-151
- Apple contract:
  - `handleAPStatsUpdates(...)` validates timer `state+0x70`, allocates
    `0x808`, calls APSTA vtable `+0xfd8`, accepts async failure
    `0xe00002d8`, calls `checkForStationListMismatch`, tracks activity
    baseline `state+0x88`, posts inactivity STA message `0x0d` with payload
    `{ qword 0, dword 0xffffffff }`, and reschedules at `0x1388`
  - `monitorAPInterface(...)` validates timer `state+0x78`, mirrors
    core-private `+0x4d59` bit 0 to `state+0x208`, refreshes Apple vendor IE
    when `state+0x62` requires it, tracks low traffic at `state+0x64`,
    updates RX baselines `state+0x90/+0x98`, accumulates stats at
    `state+0x1b8/+0x1c0`, and reschedules at `0x3e8`
  - `setPowerSaveState(...)` is gated by `state+0x0e`, ignores reason `7`,
    writes current state at `state+0x10`, records transition counts at
    `state+0x1c8 + state * 0x10`, and aliases duration buckets at
    `state+0x1d0`
  - assoc-list callback/conversion uses BCM count `+0x00`, MACs from `+0x04`
    stride `6`, Apple output size `0x808`, version `1`, count `+0x04`,
    entries `+0x08`, stride `0x10`, max `0x80`, clamp threshold `0x81`
  - MFP uses feature gate `0x26`, IOVAR `mfp`, 4-byte payload, and unsupported
    return `0`
  - `printDataPath` uses userPrintCtx offsets `+0x18/+0x20/+0x24/+0x28` and
    vtable slots `+0x338/+0x320/+0x328/+0xc68`; `updateRxCounter` adds to
    `state+0xa0`
- local mismatch before CR-152:
  - these timer, conversion, MFP, print, and RX-counter contracts were absent
    from compiled APSTA witnesses
  - APSTA state fields `state+0x62`, `state+0x64`, and `state+0x208` were not
    explicitly typed
- exact correction:
  - add constants/layout witnesses for inactivity message, BCM/Apple assoc
    lists, power-state records, monitor counters, MFP, printDataPath, and
    updateRxCounter
  - split APSTA state fields `state+0x62`, `state+0x64`, and `state+0x208`
  - add static asserts tying these fields to recovered Apple offsets and
    aliases
- non-claims:
  - this does not execute APSTA timers or IOVARs at runtime
  - this does not enable APSTA/HostAP ownership
  - this does not alter primary STA association, key, or data paths

### 63. APSTA async callback telemetry has fixed filter/beacon operands
- anomaly_id: `A-APSTA-ASYNC-CALLBACK-TELEMETRY-CONTRACTS-079`
- layer: APSTA async callback telemetry scaffold after CR-152
- Apple contract:
  - HostAP startup sends virtual IOVAR `pkt_filter_delete` through
    `state+0x228` with 4-byte payload value `0x6c`, no RX expected, callback
    cookie `0`, and callback `deleteIPv4PktFiltersAsyncCallBack`
  - `deleteIPv4PktFiltersAsyncCallBack` returns on status `0`; nonzero status
    logs at level `2`, line `0x0ea0`, and decodes errors through
    `state+0x218` vtable `+0x780`
  - `setBeaconInterval` uses IOCTL `0x4c`, 4-byte payload, callback
    `handleSetBcnIntervalAsyncCallBack`, skip/apply target `state+0x68`, and
    sync error line `0x106b`
  - DTIM setup uses IOCTL `0x4e`, 4-byte payload, callback
    `handleSetBcnDTIMPeriodAsyncCallBack`, source `state+0x16`, apply target
    `state+0x6a`, and sync error line `0x1091`
  - beacon callbacks return on status `0`; nonzero status logs at level `1`
    and emits RX payload data `+0x00`, length `+0x08`, telemetry flag `1`,
    through `state+0x210`
- local mismatch before CR-153:
  - callback labels, log levels/lines, RX payload offsets, telemetry flag, and
    `pkt_filter_delete` payload were not compiled local witnesses
- exact correction:
  - add constants/string witnesses for `pkt_filter_delete`,
    `BCNPRD IOCTL rxPayload bytestream: `, and
    `DTIMPRD IOCTL rxPayload bytestream: `
  - add static asserts tying async RX payload offsets, telemetry flag, payload
    value/size, and callback log levels to recovered values
- non-claims:
  - this does not execute APSTA callbacks or IOVARs at runtime
  - this does not enable APSTA/HostAP ownership
  - this does not alter primary STA association, key, or data paths

### 64. APSTA action-frame LPHS has fixed sleep/awake state semantics
- anomaly_id: `A-APSTA-ACTION-FRAME-LPHS-CONTRACTS-080`
- layer: APSTA action-frame / LPHS scaffold after CR-153
- Apple contract:
  - `handleEvent` event type `0x4b` parses action-frame payload from
    `event+0x30` and accepts minimum payload length `0x12`
  - raw version `0x0100` reads category/action at payload `+0x10/+0x11`;
    raw version `0x0200` requires length `0x1a` and reads payload
    `+0x18/+0x19`
  - unknown category/action sentinel is `0xaa`; byte-swapped versions `>= 3`
    are rejected
  - LPHS category is `0x7f`; accepted actions are `1` and `2`
  - accepted action value is written directly to station-table sleep-state
    `entry+0x10`
  - station add initializes sleep-state to `2`; active entries with state `2`
    block all-STA LPM
  - when no active station remains in blocking state `2` and SoftAP
    concurrency is disabled, Apple calls `setPowerSaveState(3, 0x0b)`
- local mismatch before CR-154:
  - local constants treated action `1` as awake and action `2` as sleep
  - parse sentinel, event offsets, all-STA blocker, transition reason, and log
    line witnesses were absent from the compiled scaffold
- exact correction:
  - action `1` is low-power/sleep and action `2` is awake/default
  - add event-offset, reject-threshold, sentinel, blocker-state, transition,
    power-save reason, and log-line constants/static asserts
- non-claims:
  - this does not enable APSTA owner creation
  - this does not synthesize LPHS action frames
  - this does not force power-save transitions
  - this does not alter primary STA association, key, or data paths

### 65. WCL action-frame send path has fixed V1/V2 payload contracts
- anomaly_id: `A-WCL-ACTION-FRAME-SEND-CONTRACTS-081`
- layer: WCL action-frame sender after CR-154
- Apple contract:
  - `setWCL_ACTION_FRAME` rejects `NULL` with `0xe00002bc`
  - carrier fields are category `+0x00`, channel `+0x04`, peer address
    `+0x08`, frame length `+0x0e`, and frame bytes `+0x10`
  - V2 is selected when core-private firmware generation `+0x30c > 0x14`
  - V1 `sendActionFrame` uses a fixed IOVAR CommandTxPayload length `0x724`
    after zeroing a `0x718` buffer and accepting total bytes up to `0x707`
  - V2 `sendActionFrameV2` rejects total bytes `>= 0x708`, allocates
    `total + 0x34`, and uses issue-command dispatch
- local mismatch before CR-155:
  - local V1 dispatch used only `frameLen` as request length instead of the
    Apple fixed `0x724` payload size
  - local diagnostic cache truncated action frames to `0x200` even though the
    recovered sender capacity is `0x708`
  - the V2 threshold and capacity literals were split across call sites instead
    of being a single recovered contract
- exact correction:
  - add named action-frame capacity, maximum length, V1 payload size, and V2
    threshold constants
  - route local V1 dispatch with fixed request size `0x724`
  - expand local cached action-frame buffer to `0x708`
- non-claims:
  - this does not implement real Broadcom action-frame adapter injection
  - this does not synthesize action frames
  - this does not alter primary STA association or APSTA owner creation

### 66. WCL action-frame progress gates scan through overdue state
- anomaly_id: `A-WCL-ACTION-FRAME-PROGRESS-CONTRACTS-082`
- layer: WCL action-frame progress after CR-155
- Apple contract:
  - `setActionFrameProgress(bool)` stores the bool byte at core-private
    `+0x4478`
  - `getActionFrameProgress()` first calls
    `checkActionFrameCompleteOverdue()`, then returns bit 0 from
    core-private `+0x4478`
  - `checkActionFrameCompleteOverdue()` reads the start timestamp from
    core-private `+0x4480`, compares unsigned elapsed milliseconds against
    `0x12d`, clears `+0x4478` when overdue, logs line `0x3b1d`, and emits
    status `0xe3ff852b` through line `0x3b1e`
  - `setupDriver()` clears the progress byte during driver-state
    initialization
  - `AppleBCMWLANScanAdapter::startScan(...)` performs the overdue check and
    rejects scan with `0xe00002d5` / line `0x00a5` while progress remains set
- local mismatch before CR-156:
  - local Tahoe owner registry had no action-frame progress bit witness
  - local code had no progress start-ms witness or overdue helper semantics
  - local code had no named scan reject status/log-line constants for this
    Apple gate
- exact correction:
  - add named progress flag/start-ms offsets, overdue threshold, overdue
    status, and scan reject constants
  - add action-frame owner `progress` and `progressStartMs` witnesses
  - add pure owner helper semantics for set/get/overdue-check
- non-claims:
  - this does not enable local scan rejection while progress is set
  - this does not synthesize a timestamp or force completion
  - this does not alter primary STA association, DHCP, RSN/EAPOL, or data paths

### 67. Controller queue/depth/capacity and multicast owner contracts diverged
- anomaly_id: `A-TAHOE-CONTROLLER-QUEUE-MULTICAST-CAPACITY-083`
- layer: Tahoe controller queue/depth/capacity and multicast/promiscuous layer
  after CR-156
- Apple contract:
  - `requestQueueSizeAndTimeout` reads `wlan.coalesce.qsize` and
    `wlan.coalesce.timeout`; it returns `0xe00002c7` unless both low 16-bit
    values are nonzero, and writes both output pointers before returning `0`
  - `fetchAndUpdateRingParameters` initializes core-private `+0x1154` to
    `0x200`; `getDataQueueDepth(OSObject*)` returns this 16-bit field
  - `IO80211SkywalkInterface::getDataQueueDepth()` dispatches to the bound
    controller vtable slot for `getDataQueueDepth(OSObject*)`
  - IO80211 base `getDataQueueDepth` returns `0x400`, while
    `getActionFramePoolCapacity` returns `0x100`
  - `setPromiscuousMode(bool)` stores the bool at core-private `+0x4778`
  - multicast mode/list share reject gate `+0x2891` bit `0x80` with status
    `0xe0823804`
  - multicast list rejects count `> 0x20` with `0xe00002bc`, stores count at
    `+0x234`, stores 6-byte entries at `+0x238`, builds `4 + count * 6`
    payloads in a `0xca` stack buffer filled with `0xaa`, and uses IOVAR
    `mcast_list`
- local mismatch before CR-157:
  - `requestQueueSizeAndTimeout` returned success unconditionally and wrote no
    output values
  - Tahoe local controller did not override `getDataQueueDepth`, leaving the
    IO80211 default `0x400` instead of the AppleBCMWLANCore default `0x200`
  - action-frame pool capacity was not explicit locally
  - promiscuous/multicast requested state and multicast-list cache/limit had no
    Tahoe owner witnesses
- exact correction:
  - add Tahoe controller contract constants for queue, depth, capacity,
    promiscuous, multicast offsets/statuses/payload shape
  - return `0xe00002c7` from `requestQueueSizeAndTimeout` unless both local
    coalesce properties are nonzero, then write both output pointers and cache
    the values
  - override `getDataQueueDepth` with owner default `0x200`
  - override `getActionFramePoolCapacity` with `0x100`
  - cache promiscuous/multicast requested state and multicast-list caller data
  - reject multicast-list count above `0x20` with `0xe00002bc`
- non-claims:
  - this does not implement Broadcom multicast IOVAR dispatch locally
  - this does not enable AP/SoftAP runtime
  - this does not claim final primary STA association or data success
  - this does not add retry, poll, fallback, forced state, or synthetic queue
    parameters

### 68. Hidden +0x1510 flow/timestamp/virtual-interface surface was not recorded
- anomaly_id: `A-TAHOE-HIDDEN-INTERFACE-FLOW-TIMESTAMP-084`
- layer: hidden interface owner surface after CR-157
- Apple contract:
  - core-private `+0x1510` stores the hidden interface-side owner object
  - `flowIdSupported()` delegates to hidden slot `+0xa68`
  - `requestFlowQueue(...)` checks slot `+0xa68`, falls back to base slot
    `+0xd60` when unsupported, returns `NULL` while commands are rejected, and
    otherwise calls hidden slot `+0xa70` with metadata operands at
    `+0x06/+0x0c/+0x10`
  - `releaseFlowQueue(...)` delegates to hidden slot `+0xa78` when flow IDs are
    supported, otherwise falls back to base slot `+0xd68`
  - packet timestamp enable/disable use base slots `+0xd90/+0xd98`, command
    gate actions, and gated hidden slots `+0xaa8/+0xab0`
  - `getLogPipes(...)` reads hidden object `+0x88`, then event/log/snapshot
    pipes at `+0x218/+0x220/+0x230`
  - virtual-interface create/enable/disable delegate through base slots
    `+0xe10/+0xd40/+0xd48`; null enable/disable status is `0xe00002bc`; role
    `6` paths involve proximity owner `+0x2c28` and wake flag `0x10000`
- local mismatch before CR-158:
  - hidden `+0x1510` flow/timestamp/log-pipe/virtual-interface slots were not
    recorded as compiled local constants
  - local `flowIdSupported` was a literal false rather than an owner-state
    witness
  - local `releaseFlowQueue` emitted debug logs, adding a non-reference side
    effect on the flow-queue release path
- exact correction:
  - add hidden-interface constants and static asserts
  - add hidden-interface owner witnesses to `TahoeOwnerRegistry`
  - make `flowIdSupported` return the owner witness, defaulting to false
  - keep `requestFlowQueue` inherited from base while flow IDs are false
  - remove debug logging from local `releaseFlowQueue` and retain only an owner
    release witness
- non-claims:
  - this does not enable flow queues
  - this does not enable packet timestamping through a hidden backend
  - this does not enable APSTA/proximity virtual-interface runtime
  - this does not claim final association/data/AP success

### 69. QoS / DynSAR / congestion-control offsets were not represented locally
- anomaly_id: `A-TAHOE-QOS-DYNSAR-OFFSETS-085`
- layer: Q11-C1 QoS / DynSAR helper owner after CR-158
- Apple contract:
  - `wasDynSARInFailSafeMode()` reads start ticks at `+0x74e0`, computes
    `((now - start) >> 0x0a) < 0x9502f9`, and logs at line `0xdea9` when
    debug output is enabled
  - congestion-control configuration helpers test `+0x7584` bit `0`; return
    `0` when set and `0xe00002c7` when clear
  - AWDL AMPDU force flags live at `+0x3768` and `+0x3764`
  - hardware feature flags live at `+0x458c`
  - split-TX status is bit `0` at `+0x00dc`
  - TX address resolution counters live at `+0x2aa4/+0x2aa8`
- local mismatch before CR-159:
  - these helper offsets and return/status contracts were not compiled local
    witnesses
  - the local owner registry had no Q11-C1/QoS-DynSAR state container
- exact correction:
  - add `TahoeQosDynsarContracts.hpp`
  - add `TahoeOwnerRegistry::QosDynsarOwner`
  - add pure helper semantics for DynSAR fail-safe window and congestion
    feature gate
- non-claims:
  - this does not call QoS IOVARs
  - this does not enable DynSAR policy
  - this does not force congestion, AMPDU, split-TX, or address-resolution state
  - this does not claim final association/data/AP success
## 160. Hidden association / RSN carrier owner offsets recovered

- anomaly: `A-ASSOC-RSN-CARRIER-OWNER-160`
- status: `FIX_IMPLEMENTED` locally, pending structural review
- reference:
  - `AppleBCMWLANCore::setWCL_ASSOCIATE(...) @ 0xffffff80015fbacc`
  - `AppleBCMWLANJoinAdapter::performJoin(...) @ 0xffffff8001576df8`
  - `AppleBCMWLANJoinAdapter::setAssocRSNIE(...) @ 0xffffff80015795b8`
  - `AppleBCMWLANCore::setRSN_IE(...) @ 0xffffff800160433e`
- finding:
  - Tahoe hidden associate carriers are selectors `0x45/0x46` with exact
    assoc-candidates length `0x3ad8`.
  - The selected WCL candidate remains candidate count `+0x218` and first
    BSSID `+0x220`.
  - RSN IE is carried as explicit pointer `+0xd6` and length `+0xd4`; local
    fixed-size copy of a partially initialized `apple80211_rsn_ie_data` stack
    object is not reference-equivalent.
- local alignment:
  - added `TahoeAssociationContracts.hpp`.
  - added `TahoeOwnerRegistry::AssociationOwner`.
  - replaced active hidden-assoc magic offsets with recovered constants.
  - changed local RSN override storage to zero previous state and copy only the
    bounded caller-provided IE length.
- non-claims:
  - no forced EAPOL TX, key install, RSN done, DHCP, retry, delay, or replay.

## 161. Skywalk packet pools used generic packet type

- anomaly: `A-SKYWALK-PACKET-POOL-NETWORK-TYPE-161`
- status: `FIX_IMPLEMENTED` locally, pending structural review
- reference:
  - `AppleBCMWLANSkywalkPacketPool::initWithName(...) @ 0xffffff80016e033c`
  - parent `IOSkywalkPacketBufferPool::initWithName` is called with packet
    type `1`, i.e. `kIOSkywalkPacketTypeNetwork`
  - IO80211 downstream consumers use `IO80211NetworkPacket*` /
    `IOSkywalkNetworkPacket` contracts
- local mismatch before CR-161:
  - `AirportItlwm` created `AirportItlwm-TX` and `AirportItlwm-RX` pools with
    packet type `0`, i.e. generic Skywalk packet pools
  - runtime showed RX EAPOL enqueue success but no `ITLWM_IO80211_INPUT`
    marker afterward
- exact correction:
  - change both local pool factory calls to `kIOSkywalkPacketTypeNetwork`
- non-claims:
  - no manual `inputPacket(...)` callback
  - no forced EAPOL TX/key/RSN success
  - no retry, delay, replay, state masking, or guessed custom packet subclass

## 162. Skywalk network packet and RX tag ABI were not represented

- anomaly: `A-SKYWALK-NETWORK-PACKET-TAG-ABI-162`
- status: `FIX_IMPLEMENTED` locally, pending structural review
- reference:
  - `AppleBCMWLANPCIeSkywalkRxCompletionQueue::enqueuePackets(...) @
    0xffffff80014ca8e4`
  - `IO80211InterfaceMonitor::logRxCompletionPacket(...) @
    0xffffff80022f633e`
  - `MacKernelSDK/Headers/IOKit/skywalk/IOSkywalkNetworkPacket.h`
- finding:
  - RX completion producer passes the network packet, packet scratch/tag, and
    ethernet header to the interface input slot.
  - The producer reads scratch `+0x18` and writes mapped service class to
    scratch `+0x29`.
  - IO80211 monitor reads tag `+0x18` as TID and tag `+0x14` as its monitored
    completion gate.
  - Apple PCIe packet scratch lives at packet `+0x78`, has size `0x98`, and
    prepare clears the first `0x30` bytes.
- local mismatch before CR-162:
  - local `IOSkywalkNetworkPacket` inherited from `IOService` and declared
    generic packet methods in the wrong class.
  - local `packet_info_tag` was empty despite proven downstream dereferences.
- exact correction:
  - align `IOSkywalkNetworkPacket` declaration to the Tahoe Skywalk header
    shape.
  - add partial `packet_info_tag` layout for offsets `+0x14`, `+0x18`,
    `+0x29`, total size `0x98`, with static assertions.
- non-claims:
  - no manual `inputPacket(...)` callback
  - no forced EAPOL TX/key/RSN success
  - no retry, delay, replay, or guessed RX completion delivery

## 163. RX completion action did not perform IO80211 input handoff

- anomaly: `A-SKYWALK-RX-COMPLETION-INPUT-HANDOFF-163`
- status: `SUPERSEDED_BY_A-CR165-RX-COMPLETION-PRODUCER-STAGING`
- reference:
  - `AppleBCMWLANPCIeSkywalkRxCompletionQueue::enqueuePackets(...) @
    0xffffff80014ca8e4`
  - `IO80211InfraInterface::inputPacket(...) @ 0xffffff80022e3f20`
- finding:
  - Apple RX completion producer calls the interface input slot from the
    completion callback after deriving `ether_header` from packet data pointer
    plus data offset.
  - The call passes packet scratch/tag, a null accepted pointer, and `false`.
  - Local runtime reached RX enqueue but never reached the IO80211 input probe.
- local mismatch before CR-163:
  - local `skywalkRxAction(...)` only incremented `rxCbCnt` and returned
    `count`.
- exact correction:
  - local `skywalkRxAction(...)` now validates packet data, builds the ethernet
    header pointer, initializes recovered tag storage, and calls
    `AirportItlwmSkywalkInterface::inputPacket(...)` from the RX completion
    boundary.
- non-claims:
  - no direct input call from `skywalkRxInput(...)`
  - no packet replay/duplication
  - no forced accepted success
  - no forced EAPOL TX/key/RSN/DHCP/link success

## 165. RX completion producer action was bypassed by direct base enqueue

- anomaly: `A-CR165-RX-COMPLETION-PRODUCER-STAGING`
- status: `FIX_IMPLEMENTED` locally, pending structural review
- reference:
  - `IOSkywalkRxCompletionQueue::requestEnqueue(...) @ 0xffffff8002a59c4c`
  - `IOSkywalkRxCompletionQueue::enqueuePackets(...) @
    0xffffff8002a59cda / 0xffffff8002a59d84`
  - `AppleBCMWLANPCIeSkywalkRxCompletionQueue::enqueuePackets(...) @
    0xffffff80014ca8e4`
- finding:
  - IOSkywalk calls the registered RX producer action from
    `requestEnqueue(...)`, not from base `enqueuePackets(...)`.
  - AppleBCMWLAN's producer action drains an owner-side pending RX list, calls
    IO80211 input, fills the Skywalk-provided output packet array, and returns
    produced count.
  - CR-164 runtime showed local RX EAPOL enqueue success while
    `skywalkRxAction` and `ITLWM_IO80211_INPUT` remained absent.
- local mismatch before CR-165:
  - local `skywalkRxInput(...)` prepared a packet and called
    `fRxQueue->enqueuePackets(...)` directly.
  - no local pending producer queue existed, so the registered action was
    bypassed.
- exact correction:
  - add fixed-capacity local pending RX packet ring.
  - stage prepared RX packets in `skywalkRxInput(...)`.
  - ring `fRxQueue->requestEnqueue(nullptr, 0)`.
  - make `skywalkRxAction(...)` pop pending packets, call IO80211 input, fill
    the output packet array, and return produced count.
- non-claims:
  - no forced accepted success
  - no forced EAPOL TX/key/RSN/DHCP/link success
  - no retry, delay, poll loop, replay, duplicate notify, or deauth masking

## 166. TX submission consumed packets had no completion producer

- anomaly: `A-CR166-TX-COMPLETION-PRODUCER-OWNERSHIP`
- status: `FIX_IMPLEMENTED` locally, pending structural review
- reference:
  - `AppleBCMWLANPCIeSkywalkTxCompletionQueue::stagePacket(...) @
    0xffffff80014c91c0`
  - `AppleBCMWLANPCIeSkywalkTxCompletionQueue::enqueuePackets(...) @
    0xffffff80014c8d62`
  - `AppleBCMWLANPCIeSkywalkTxCompletionQueue::requestEnqueue(...) @
    0xffffff80014c920c`
  - IOSkywalk TX completion enqueue path near `0xffffff8002a3fa9e`
- finding:
  - Apple stages completed TX packets in an owner-side completion list, rings
    the completion producer boundary, drains produced packets into the
    Skywalk-provided array, and the IOSkywalk base completion path calls
    `completeWithQueue(queue, kIOSkywalkPacketDirectionTx, 0)`.
- local mismatch before CR-166:
  - local `skywalkTxAction(...)` copied data to an mbuf and returned the
    original `IOSkywalkPacket` as consumed.
  - no packet was staged on `fTxCompQueue`.
  - `skywalkTxCompletionAction(...)` returned `0` unconditionally.
- exact correction:
  - add a fixed-capacity TX completion pending producer ring.
  - stage each non-null packet consumed by `skywalkTxAction(...)`.
  - ring `fTxCompQueue->requestEnqueue(nullptr, 0)` after the TX batch.
  - make `skywalkTxCompletionAction(...)` pop staged packets, fill the
    provided array, and return produced count.
  - drain pending completion packets before queue/pool release.
- non-claims:
  - no forced TX success, key install, RSN, DHCP, link, or internet success
  - no retry, delay, poll loop, replay, packet synthesis, or deauth masking

## 167. RX producer missed packet tag carrier and post-batch accounting

- anomaly: `A-CR167-RX-PRODUCER-TAG-STATS-CLOSURE`
- status: `FIX_IMPLEMENTED` locally, pending structural review
- reference:
  - `AppleBCMWLANPCIeSkywalkRxCompletionQueue::enqueuePackets(...) @
    0xffffff80014ca8e4`
  - `IO80211SkywalkInterface::recordInputPacket(int, int) @
    0xffffff8002277c96`
- finding:
  - Apple's RX completion producer passes packet scratch/tag metadata into
    `inputPacket(...)`, fills the produced packet array, then records input
    packet/byte accounting and updates the RX counter.
  - Local generic `IOSkywalkNetworkPacket` is size `0x78`; the Apple PCIe
    scratch pointer at `packet+0x78` is subclass storage and cannot be
    synthesized by raw offset.
- local mismatch before CR-167:
  - local RX pending records carried only packet pointers.
  - local RX producer did not call `recordInputPacket(...)` or
    `updateRxCounter(...)` after producing a batch.
- exact correction:
  - add local RX pending tag and length metadata arrays.
  - pass the staged tag into `inputPacket(...)`.
  - call `recordInputPacket(produced, producedBytes)` and
    `updateRxCounter(produced)` after produced RX batches.
- non-claims:
  - no forced accepted success, EAPOL TX, key install, RSN, DHCP, or link
    success
  - no retry, delay, poll loop, replay, packet synthesis, or deauth masking

## 168. TX queue space and pending visibility returned unconditional zero

- anomaly: `A-CR168-TX-QUEUE-SPACE-PENDING-CLOSURE`
- status: `FIX_IMPLEMENTED` locally, pending structural review
- reference:
  - `AppleBCMWLANSkywalkInterface::getTxSubQueue(...) @
    0xffffff800155fb5a`
  - `AppleBCMWLANIO80211APSTAInterface::getTxSubQueue(...) @
    0xffffff80016940b4`
  - `IO80211SkywalkInterface::pendingPackets(...) @ 0xffffff80022780ac`
  - `IO80211SkywalkInterface::packetSpace(...) @ 0xffffff8002278134`
- finding:
  - Apple exposes queue-backed TX admission state through queue objects.
  - IO80211's base space/pending paths map queue objects and call their
    pending/free-space virtuals.
- local mismatch before CR-168:
  - local `getTxSubQueue(...)` returned `fTxQueue`.
  - local `pendingPackets(...)` and `packetSpace(...)` returned `0`.
- exact correction:
  - local single-queue mapping now returns `fTxQueue` consistently.
  - `pendingPackets(...)` returns `fTxQueue->getPacketCount()`.
  - `packetSpace(...)` returns `fTxQueue->getFreeSpace()`.
- non-claims:
  - no fabricated queue capacity
  - no forced TX success, EAPOL/key/RSN/DHCP/link/data success
  - no retry, delay, poll loop, replay, packet synthesis, or deauth masking

## 169. TX submission output accounting was skipped after accepted packets

- anomaly: `A-CR169-TX-OUTPUT-ACCOUNTING-CLOSURE`
- status: `FIX_IMPLEMENTED` locally, pending structural review
- reference:
  - `AppleBCMWLANPCIeSkywalkTxSubmissionQueue::dequeuePackets(...) @
    0xffffff80014c611c`
  - TX accounting call site near `0xffffff80014c7944`
  - `IO80211SkywalkInterface::recordOutputPacket(apple80211_wme_ac,int,int) @
    0xffffff8002277cc6`
- finding:
  - Apple TX submission dequeue accumulates packet and byte totals for the
    batch and calls the IO80211 output-accounting edge.
  - IO80211 output accounting delegates to interface monitor state and does
    not force link, key, or datapath success.
- local mismatch before CR-169:
  - local `skywalkTxAction(...)` counted delivered packets only in
    `sRT.txPktSent`.
  - no `recordOutputPacket(...)` call followed accepted `outputPacket(...)`
    frames.
- exact correction:
  - accumulate delivered packet bytes for accepted TX frames.
  - call `recordOutputPacket({ APPLE80211_WME_AC_BE }, delivered,
    deliveredBytes)` after the TX batch for the local single-queue mapping.
- non-claims:
  - no Apple packet scratch synthesis
  - no scratch-dependent TX log method calls
  - no forced TX/EAPOL/key/RSN/DHCP/link/data success
  - no retry, delay, poll loop, replay, packet synthesis, or deauth masking

## 170. Skywalk packet pool allocated the wrong IO80211 packet class

- anomaly: `A-CR170-IO80211-NETWORK-PACKET-POOL-CLASS`
- status: `FIX_IMPLEMENTED` locally, pending structural review
- reference:
  - `AppleBCMWLANPCIeSkywalkPacketPool::newPacketWithDescriptor(...) @
    0xffffff80014cb250`
  - `AppleBCMWLANPCIeSkywalkPacketPool::allocatePacket(...) @
    0xffffff80014cb8ae`
  - `IO80211NetworkPacket::getPacketType(...) @ 0xffffff80022cf000`
  - `IO80211InfraInterface::inputPacket(...) @ 0xffffff80022e3838`
  - `IO80211PeerManager::skywalkInputPacket(...) @ 0xffffff80021dd58c`
- finding:
  - AppleBCMWLAN packet pools allocate packet objects in the
    `IO80211NetworkPacket` family.
  - `IO80211NetworkPacket::getPacketType(...)` parses Ethernet payload and
    classifies EtherType `0x888e` as EAPOL packet type `2`.
  - The active runtime already reaches the IO80211 input boundary with RX
    EAPOL, but no EAPOL TX/key/RSN progression follows.
- local mismatch before CR-170:
  - local pools used base `IOSkywalkPacketBufferPool::withName(...,
    kIOSkywalkPacketTypeNetwork, ...)`.
  - local RX handoff passed the packet as
    `reinterpret_cast<IO80211NetworkPacket *>(pkt)`.
  - the allocation path did not prove that the object was a real
    `IO80211NetworkPacket`.
- exact correction:
  - add a local declaration for the system `IO80211NetworkPacket` class.
  - add `AirportItlwmIO80211PacketPool`, an `IOSkywalkPacketBufferPool`
    subclass.
  - override `newPacket(...)` to allocate the system `IO80211NetworkPacket`
    metaclass and initialize it with the pool descriptor.
  - use the new pool for both TX and RX.
- non-claims:
  - no Apple PCIe packet scratch synthesis
  - no raw `packet+0x78` writes
  - no scratch-dependent log method calls
  - no forced EAPOL TX, key install, RSN done, DHCP, link, or data success
  - no retry, delay, poll loop, replay, packet synthesis, or deauth masking

## 171. IOSkywalkNetworkPacket base size included non-reference storage

- anomaly: `A-CR171-IOSKYWALK-NETWORK-PACKET-SIZE`
- status: `FIX_IMPLEMENTED` locally, pending structural review
- reference:
  - `IOSkywalkNetworkPacket` metaclass construction in
    `IOSkywalkFamily_decompiled.c`, size `0x78`
  - `IO80211NetworkPacket` constructor/deallocation in
    `IO80211Family_decompiled.c`, size `0x78`
  - `AppleBCMWLANPCIeSkywalkPacket` metaclass construction in
    `AppleBCMWLANBusInterfacePCIeMac_decompiled.c`, size `0x80`
  - Apple packet scratch pointer uses at packet offset `+0x78`
- finding:
  - `+0x78` is subclass-owned scratch-pointer storage in the Apple PCIe
    packet object.
  - It is not a field of `IOSkywalkNetworkPacket` or `IO80211NetworkPacket`.
- local mismatch before CR-171:
  - the tracked local `IOSkywalkNetworkPacket` declaration added
    `void *mReserved`.
  - that shifted the local base size to `0x80`, consuming the reference
    subclass scratch-pointer slot.
- exact correction:
  - remove the non-reference member from the tracked local declaration.
  - add static assertions for `sizeof(IOSkywalkNetworkPacket) == 0x78`.
- non-claims:
  - no Apple PCIe packet subclass synthesis
  - no raw `packet+0x78` writes
  - no scratch-dependent method calls
  - no forced EAPOL/key/RSN/DHCP/link/data success
  - no retry, delay, poll loop, replay, packet synthesis, or deauth masking

## 172. IO80211NetworkPacket local declaration lacked the exported packet surface

- anomaly: `A-CR172-IO80211-NETWORK-PACKET-VIRTUAL-SURFACE`
- status: `FIX_IMPLEMENTED` locally, pending structural review
- reference:
  - `kdk_symbols.txt` exported `IO80211NetworkPacket` method list
  - `IO80211Family_decompiled.c` constructor/deallocation and packet-type
    implementation
  - `AppleBCMWLANPCIeSkywalkPacket` constructors in
    `AppleBCMWLANBusInterfacePCIeMac_decompiled.c`
- finding:
  - `IO80211NetworkPacket` is a real intermediate class layer, not just an
    empty type alias over `IOSkywalkNetworkPacket`.
  - Apple packet subclass construction enters this layer before installing the
    Apple packet vtable.
- local mismatch before CR-172:
  - local `IO80211NetworkPacket` header declared an empty subclass.
  - that was enough for CR-170 system-object allocation, but not enough to
    compile a future Apple packet subclass against the correct base ABI.
- exact correction:
  - add `OSDeclareDefaultStructors(IO80211NetworkPacket)`.
  - add the export/decompile-proven method declarations.
  - add opaque `IO80211NetworkTXStatus` enum declaration for ABI-correct
    signatures.
  - assert `sizeof(IO80211NetworkPacket) == 0x78`.
- non-claims:
  - no local IO80211 packet instantiation
  - no Apple PCIe packet subclass synthesis
  - no raw `packet+0x78` writes
  - no packet allocation/input/output behavior change
  - no forced EAPOL/key/RSN/DHCP/link/data success
  - no retry, delay, poll loop, replay, packet synthesis, or deauth masking

## 173. Packet scratch field map was incomplete for Apple PCIe packet offsets

- anomaly: `A-CR173-PACKET-SCRATCH-FIELD-MAP`
- status: `FIX_IMPLEMENTED` locally, pending structural review
- reference:
  - `AppleBCMWLANPCIeSkywalkPacket` scratch pointer at packet `+0x78`
  - scratch size `0x98`
  - scratch field uses at `+0x48`, `+0x50`, `+0x74`, `+0x80`, `+0x8a`, `+0x90`
- finding:
  - the local tag struct had the correct total size but only named the early
    IO80211 input/monitor fields.
  - later Apple packet scratch fields need compile-time names before any safe
    scratch owner restoration can proceed.
- local mismatch before CR-173:
  - `packet_info_tag` left the proven Apple packet fields anonymous.
- exact correction:
  - name bus and virtual address fields.
  - name packet signature, TX status, flow queue index, and AC/duplicate flags.
  - add static offset assertions for all named fields.
- rejected path:
  - direct local C++ subclass of `IO80211NetworkPacket` was tested and
    rejected before submission.
  - BootKC verification failed on non-exported `IOSkywalkPacket::*` virtuals
    generated into the subclass vtable.
- non-claims:
  - no Apple PCIe packet subclass synthesis
  - no raw `packet+0x78` writes
  - no packet allocation/input/output behavior change
  - no forced EAPOL/key/RSN/DHCP/link/data success
  - no retry, delay, poll loop, replay, packet synthesis, or deauth masking


## 174. Packet scratch RX/TX VLAN, RxDrop marker, and AC meta byte fields unnamed

- anomaly: `A-CR174-PACKET-SCRATCH-RX-TX-VLAN-FIELD-MAP`
- status: `FIX_IMPLEMENTED` locally; supersedes CR-173 structural review
- reference:
  - `IO80211InfraInterface::logTxPacket` writes `+0x1c` (TX VLAN, byteswapped) and `+0x28` (AC meta).
  - `IO80211InfraInterface::inputPacket` and `AppleBCMWLANLowLatencyInterface::inputPacket` write `+0x22` (RX VLAN, byteswapped).
  - `IO80211PeerManager::skywalkInputPacket` reads `+0x22` for RX log construction.
  - `IO80211PeerManager::inputPacket` and `IO80211PeerManager::skywalkInputPacket` clear `+0x24` (RxDrop marker).
- finding:
  - CR-173 left `+0x19..+0x28` as anonymous `reserved19[0x10]`. Cross-decompile audit confirms four offsets in that range carry distinct meanings used by exported IO80211 RX/TX path methods.
- local mismatch before CR-174:
  - `packet_info_tag` named no fields between `tid` (`+0x18`) and `service_class` (`+0x29`).
- exact correction:
  - promote `tx_vlan_tag` (`+0x1c`, uint32_t) out of `reserved19`.
  - promote `rx_vlan_tag` (`+0x22`, uint16_t).
  - promote `rx_drop_marker` (`+0x24`, uint32_t).
  - promote `ac_meta` (`+0x28`, uint8_t).
  - narrow padding bands to `reserved19[0x19..0x1b]` and `reserved20[0x20..0x21]`.
  - add static offset assertions for all four new fields.
  - struct size remains `0x98`.
- binary invariance:
  - generated kext sha256 unchanged: `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`.
  - UUID unchanged: `BA3D771F-F079-33FF-94E5-C792E66237D8`.
  - confirms purely additive structural rename — no live code references the new field names yet.
- non-claims:
  - no Apple PCIe packet subclass synthesis
  - no raw `packet+0x78` writes
  - no packet allocation/input/output behavior change
  - no forced EAPOL/key/RSN/DHCP/link/data success
  - no retry, delay, poll loop, replay, packet synthesis, or deauth masking

## 175. IO80211InfraInterface Tahoe public API helpers undeclared

- anomaly: `A-CR175-INFRAINTERFACE-TAHOE-API-HEADER-ALIGNMENT`
- status: `FIX_IMPLEMENTED` locally; supersedes CR-174 structural review
- reference:
  - `IO80211InfraInterface::getInfraPeer()` exported at `0xffffff80022e1148`.
  - `IO80211InfraInterface::getCurrentApAddress()` exported at `0xffffff80022e5ef8`.
  - `IO80211InfraInterface::handleKeyDone(bool, bool)` exported at `0xffffff80022e6f9c`.
  - `IO80211InfraInterface::bssidChange(void *, unsigned long)` exported at `0xffffff80022e116e`.
- finding:
  - All four are direct-call (non-vtable) BootKC exports on the live Tahoe IO80211Family. Until CR-175 the local `IO80211InfraInterface.h` did not declare them, so any future caller would need a per-call `extern "C"` shim with mangled signature instead of using the documented C++ surface.
- local mismatch before CR-175:
  - missing `IO80211Peer *getInfraPeer(void)` declaration.
  - missing `ether_addr *getCurrentApAddress(void)` declaration.
  - missing `void handleKeyDone(bool, bool)` declaration.
  - missing `void bssidChange(void *, unsigned long)` declaration.
- exact correction:
  - add four non-virtual declarations under `#if __IO80211_TARGET >= __MAC_26_0` block in `include/Airport/IO80211InfraInterface.h`.
  - anchor each to its BootKC address in a comment block.
  - vtable layout unchanged.
- binary invariance:
  - generated kext sha256 unchanged: `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`.
  - UUID unchanged: `BA3D771F-F079-33FF-94E5-C792E66237D8`.
  - regdiag sha256: `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`.
  - confirms purely additive structural rename — no live code references the new declarations yet.
- non-claims:
  - no Apple PCIe packet subclass synthesis
  - no raw `packet+0x78` writes
  - no packet allocation/input/output behavior change
  - no forced EAPOL/key/RSN/DHCP/link/data success
  - no retry, delay, poll loop, replay, packet synthesis, or deauth masking
  - no calls to `getInfraPeer`, `getCurrentApAddress`, `handleKeyDone`, or `bssidChange` from any local code path

## 176. IO80211PeerManager public peer API undeclared in local headers

- anomaly: `A-CR176-PEERMANAGER-PUBLIC-API-HEADER-ALIGNMENT`
- status: `FIX_IMPLEMENTED` locally; supersedes CR-175 structural review
- reference:
  - `IO80211PeerManager::addPeer(unsigned char *)` exported at `0xffffff80021d3f58`.
  - `IO80211PeerManager::addPeerOperation()` exported at `0xffffff80021d7ba0`.
  - `IO80211PeerManager::removePeer(IO80211Peer *)` exported at `0xffffff80021d4452`.
  - `IO80211PeerManager::removePeer(unsigned char *)` exported at `0xffffff80021d4806`.
  - `IO80211PeerManager::removePeerOperation()` exported at `0xffffff80021d7c7e`.
  - `IO80211PeerManager::getPeerList()` exported at `0xffffff80021df2fe`.
  - `IO80211PeerManager::getPeerStats(apple80211_peer_stats *)` exported at `0xffffff80021d298e`.
- finding:
  - the local include directory had no canonical header for the IO80211PeerManager public API. Existing files only forward-declared the class.
- local mismatch before CR-176:
  - no `IO80211PeerManager.h` header.
  - peer add/remove/list/stats methods undeclared anywhere in `include/Airport`.
- exact correction:
  - add new `include/Airport/IO80211PeerManager.h`.
  - declare `class IO80211PeerManager` as opaque, non-data, no vtable.
  - declare the seven non-virtual exported peer-management methods with BootKC-matching signatures.
  - forward-declare `IO80211Peer` and `apple80211_peer_stats`.
- binary invariance:
  - generated kext sha256 unchanged: `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`.
  - UUID unchanged: `BA3D771F-F079-33FF-94E5-C792E66237D8`.
  - regdiag sha256: `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`.
  - confirms purely additive header — no live code references the new declarations yet.
- non-claims:
  - no Apple PCIe packet subclass synthesis
  - no raw `packet+0x78` writes
  - no packet allocation/input/output behavior change
  - no forced EAPOL/key/RSN/DHCP/link/data success
  - no retry, delay, poll loop, replay, packet synthesis, or deauth masking
  - no calls from any local code path to the seven peer-management methods
  - no declared base class or data layout for `IO80211PeerManager`

## 177. IO80211Peer public peer API undeclared in local headers

- anomaly: `A-CR177-PEER-PUBLIC-API-HEADER-ALIGNMENT`
- status: `FIX_IMPLEMENTED` locally; supersedes CR-176 structural review
- reference:
  - `IO80211Peer::withAddressAndManager(unsigned char const *, IO80211PeerManager *)` exported at `0xffffff80021bf64a`.
  - `IO80211Peer::init()` exported at `0xffffff80021bf6c0`.
  - `IO80211Peer::getMacAddress()` exported at `0xffffff80021bff7a`.
  - `IO80211Peer::setMacAddress(ether_addr *)` exported at `0xffffff80021c5df4`.
  - `IO80211Peer::getManager()` exported at `0xffffff80021c3558`.
  - `IO80211Peer::getGeneration()` exported at `0xffffff80021c60dc`.
- finding:
  - the local include directory had no canonical header for the IO80211Peer public API. Existing files only forward-declared the class.
- local mismatch before CR-177:
  - no `IO80211Peer.h` header.
  - peer factory, init, MAC accessors, manager back-pointer and generation undeclared anywhere in `include/Airport`.
- exact correction:
  - add new `include/Airport/IO80211Peer.h`.
  - declare `class IO80211Peer` as opaque, non-data, no vtable.
  - declare the six non-virtual exported peer methods (one factory, one init, four accessors/mutators) with BootKC-matching signatures.
  - forward-declare `IO80211PeerManager` and `ether_addr`.
- binary invariance:
  - generated kext sha256 unchanged: `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`.
  - UUID unchanged: `BA3D771F-F079-33FF-94E5-C792E66237D8`.
  - regdiag sha256: `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`.
  - confirms purely additive header — no live code references the new declarations yet.
- non-claims:
  - no Apple PCIe packet subclass synthesis
  - no raw `packet+0x78` writes
  - no packet allocation/input/output behavior change
  - no forced EAPOL/key/RSN/DHCP/link/data success
  - no retry, delay, poll loop, replay, packet synthesis, or deauth masking
  - no calls from any local code path to the six peer methods
  - no declared base class or data layout for `IO80211Peer`
  - no inclusion of kernel-internal types like `peerState` or `peerMonitoringCtx`

## 178. IO80211PeerManager data-path + peer-lookup helper surface absent

- topic: structural reference alignment of IO80211PeerManager data-path and lookup surface
- date: 2026-04-28
- supersedes: CR-177
- reference evidence:
  - `IO80211PeerManager::findPeer(unsigned char *)` exported at `0xffffff80021d1388`.
  - `IO80211PeerManager::findCachedPeer(unsigned char *)` exported at `0xffffff80021d3f0c`.
  - `IO80211PeerManager::getUnicastPeer()` exported at `0xffffff80021df2a8`.
  - `IO80211PeerManager::getMulticastPeer()` exported at `0xffffff80021df296`.
  - `IO80211PeerManager::getEnabled()` exported at `0xffffff80021df672`.
  - `IO80211PeerManager::setEnableState(bool)` exported at `0xffffff80021cc798`.
  - `IO80211PeerManager::getDataPathOpen()` exported at `0xffffff80021df4f8`.
  - `IO80211PeerManager::setDataPathOpen(bool)` exported at `0xffffff80021df50a`.
  - `IO80211PeerManager::setDataPathState(bool)` exported at `0xffffff80021cde60`.
  - `IO80211PeerManager::lockDataPath()` exported at `0xffffff80021cded6`.
  - `IO80211PeerManager::unlockDataPath()` exported at `0xffffff80021cdfca`.
- finding:
  - the CR-176 IO80211PeerManager header only declared peer-membership helpers (addPeer/removePeer/getPeerList/getPeerStats). Lookup and data-path-control helpers were missing.
- local mismatch before CR-178:
  - `findPeer`, `findCachedPeer`, `getUnicastPeer`, `getMulticastPeer` undeclared.
  - `getEnabled`, `setEnableState` undeclared.
  - `getDataPathOpen`, `setDataPathOpen`, `setDataPathState`, `lockDataPath`, `unlockDataPath` undeclared.
- exact correction:
  - extend `include/Airport/IO80211PeerManager.h` with eleven non-virtual public method declarations matching the BootKC signatures listed above.
  - keep `class IO80211PeerManager` opaque (no base class, no data, no vtable).
  - anchor each new declaration to its BootKC address in the header preamble.
- binary invariance:
  - generated kext sha256 unchanged: `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`.
  - UUID unchanged: `BA3D771F-F079-33FF-94E5-C792E66237D8`.
  - regdiag sha256: `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`.
  - confirms purely additive header extension — no live code references the new declarations yet.
- non-claims:
  - no Apple PCIe packet subclass synthesis
  - no raw `packet+0x78` writes
  - no packet allocation/input/output behavior change
  - no forced EAPOL/key/RSN/DHCP/link/data success
  - no retry, delay, poll loop, replay, packet synthesis, or deauth masking
  - no calls from any local code path to the eleven new peer-manager methods
  - no declared base class or data layout for `IO80211PeerManager`
  - no inclusion of kernel-internal types whose definitions are not yet recovered

## 179. IO80211PeerManager infra BSSID/SSID/channel/RSSI helper surface absent

- topic: structural reference alignment of IO80211PeerManager infra-config helpers
- date: 2026-04-28
- supersedes: CR-178
- reference evidence:
  - `IO80211PeerManager::getInfraBssid()` exported at `0xffffff80021df07a`.
  - `IO80211PeerManager::getInfraSsidLen()` exported at `0xffffff80021df2de`.
  - `IO80211PeerManager::setInfraSsidLen(unsigned int)` exported at `0xffffff80021df2ee`.
  - `IO80211PeerManager::getInfraSsidBytes()` exported at `0xffffff80021df08a`.
  - `IO80211PeerManager::setInfraSsidBytes(unsigned char*, unsigned int)` exported at `0xffffff80021df09a`.
  - `IO80211PeerManager::setInfraTxState(bool)` exported at `0xffffff80021d4e36`.
  - `IO80211PeerManager::setInfraChannel(apple80211_channel*)` exported at `0xffffff80021d4eb0`.
  - `IO80211PeerManager::copyInfraChannel(apple80211_channel*)` exported at `0xffffff80021d4e72`.
  - `IO80211PeerManager::resetInfraChannel()` exported at `0xffffff80021d4e90`.
  - `IO80211PeerManager::setInfraChannelInfo(apple80211_channel*)` exported at `0xffffff80021df04c`.
  - `IO80211PeerManager::setInfraChannelFlags(unsigned int)` exported at `0xffffff80021df06a`.
  - `IO80211PeerManager::getInfraRSSI()` exported at `0xffffff80021df994`.
  - `IO80211PeerManager::setInfraRSSI(int)` exported at `0xffffff80021df984`.
- finding:
  - the CR-178 header had not yet declared the infra-config (BSSID/SSID/channel/RSSI/TX-state) helpers.
- local mismatch before CR-179:
  - thirteen public direct-call helpers undeclared anywhere in `include/Airport`.
- exact correction:
  - extend `include/Airport/IO80211PeerManager.h` with thirteen non-virtual public method declarations matching the BootKC signatures listed above.
  - keep `class IO80211PeerManager` opaque (no base class, no data, no vtable).
  - forward-declare `struct apple80211_channel` and `struct ether_addr`.
  - anchor each new declaration to its BootKC address in the header preamble.
- binary invariance:
  - generated kext sha256 unchanged: `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`.
  - UUID unchanged: `BA3D771F-F079-33FF-94E5-C792E66237D8`.
  - regdiag sha256: `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`.
  - confirms purely additive header extension — no live code references the new declarations yet.
- non-claims:
  - no Apple PCIe packet subclass synthesis
  - no raw `packet+0x78` writes
  - no packet allocation/input/output behavior change
  - no forced EAPOL/key/RSN/DHCP/link/data success
  - no retry, delay, poll loop, replay, packet synthesis, or deauth masking
  - no calls from any local code path to the thirteen new peer-manager methods
  - no declared base class or data layout for `IO80211PeerManager`
  - no inclusion of kernel-internal types whose definitions are not yet recovered

## 180. IO80211InterfaceMonitor public direct-call header surface absent

- topic: structural reference alignment of IO80211InterfaceMonitor public surface
- date: 2026-04-28
- supersedes: CR-179
- reference evidence:
  - 19 non-virtual exported helpers on `IO80211InterfaceMonitor` recovered
    from BootKC IO80211Family, addresses listed in the corresponding YAML
    `120_interface_monitor_public_api_2026_04_28.yaml`.
- finding:
  - the local include directory had no canonical header for the
    IO80211InterfaceMonitor public API. Existing files only forward-declared
    the class in IO80211Interface.h.
- local mismatch before CR-180:
  - no `IO80211InterfaceMonitor.h` header.
  - controller back-pointer, counters, RSSI/SNR/NF accessors, link-rate
    accessor, and channel modifier all undeclared anywhere in `include/Airport`.
- exact correction:
  - add new `include/Airport/IO80211InterfaceMonitor.h`.
  - declare `class IO80211InterfaceMonitor` as opaque, non-data, no vtable.
  - declare the nineteen non-virtual exported member functions with
    BootKC-matching signatures.
  - forward-declare `IO80211Controller`.
- binary invariance:
  - generated kext sha256 unchanged: `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`.
  - UUID unchanged: `BA3D771F-F079-33FF-94E5-C792E66237D8`.
  - regdiag sha256: `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`.
  - confirms purely additive header — no live code references the new declarations yet.
- non-claims:
  - no Apple PCIe packet subclass synthesis
  - no raw `packet+0x78` writes
  - no packet allocation/input/output behavior change
  - no forced EAPOL/key/RSN/DHCP/link/data success
  - no retry, delay, poll loop, replay, packet synthesis, or deauth masking
  - no calls from any local code path to the nineteen interface-monitor methods
  - no declared base class or data layout for `IO80211InterfaceMonitor`
  - no inclusion of kernel-internal types whose definitions are not yet recovered

## 181. IO80211InfraInterface IORegistry property updater helper surface absent

- topic: structural reference alignment of IO80211InfraInterface IORegistry property updater helpers
- date: 2026-04-28
- supersedes: CR-180
- reference evidence:
  - 11 non-virtual exported helpers on `IO80211InfraInterface` recovered
    from BootKC IO80211Family, addresses listed in the corresponding YAML
    `121_infrainterface_property_updaters_2026_04_28.yaml`.
- finding:
  - the CR-175 InfraInterface header had not yet declared the property
    updaters or runtime helpers that any future caller-wiring CR will
    need to keep IORegistry state in sync.
- local mismatch before CR-181:
  - `updateSSIDProperty`, `updateLocaleProperty`, `updateBSSIDProperty`,
    `updateChannelProperty`, `updateCountryCodeProperty`,
    `updateStaticProperties`, `updateLinkSpeed` undeclared.
  - `loadHwChannels`, `loadChannelInfo` undeclared.
  - `onDispatchQueue`, `cancelDebounceTimer` undeclared.
- exact correction:
  - extend `include/Airport/IO80211InfraInterface.h` under the
    `__IO80211_TARGET >= __MAC_26_0` block with eleven non-virtual
    public method declarations matching the BootKC signatures.
  - anchor each new declaration to its BootKC address in the header
    preamble next to the CR-175 entries.
- binary invariance:
  - generated kext sha256 unchanged: `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`.
  - UUID unchanged: `BA3D771F-F079-33FF-94E5-C792E66237D8`.
  - regdiag sha256: `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`.
  - confirms purely additive header extension — no live code references the new declarations yet.
- non-claims:
  - no Apple PCIe packet subclass synthesis
  - no raw `packet+0x78` writes
  - no packet allocation/input/output behavior change
  - no forced EAPOL/key/RSN/DHCP/link/data success
  - no retry, delay, poll loop, replay, packet synthesis, or deauth masking
  - no calls from any local code path to the eleven new InfraInterface methods
  - no declared base class or data layout for `IO80211InfraInterface` beyond what was already present
  - no inclusion of kernel-internal types whose definitions are not yet recovered

## 182. IO80211InterfaceMonitor leaky-AP, reporter, and packet-record helpers undeclared (REFERENCE_ALIGNMENT_FIX, supersedes 181)

- locus: `include/Airport/IO80211InterfaceMonitor.h`
- evidence: `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/122_interface_monitor_leaky_ap_reporters_2026_04_28.yaml`
- delta-from-reference:
  - CR-180 declared 19 InterfaceMonitor helpers (controller, counters,
    RSSI/SNR/NF, link/channel). The leaky-AP cache, IOReporter
    lifecycle, and per-packet record helpers were missing.
  - `getLeakyApSsid(apple80211_ssid*)`, `getLeakyApBssid(ether_addr*)`,
    `resetLeakyApStats()` undeclared.
  - `setInputPacketRSSI(long long)`, `recordInputPacket(int, int)`,
    `recordOutputPacket(apple80211_wme_ac, int, int)`, `initFrameStats()`
    undeclared.
  - `initHeFrameStats()`, `destroyReporters()`, `updateAllReports()`
    undeclared.
- exact correction:
  - extend `include/Airport/IO80211InterfaceMonitor.h` `class
    IO80211InterfaceMonitor` body with ten non-virtual public method
    declarations matching the BootKC signatures.
  - add forward declarations for `apple80211_ssid`, `ether_addr`, and
    `enum apple80211_wme_ac : unsigned int`.
  - anchor each new declaration to its BootKC address in the header
    preamble next to the CR-180 entries.
- binary invariance:
  - generated kext sha256 unchanged: `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`.
  - UUID unchanged: `BA3D771F-F079-33FF-94E5-C792E66237D8`.
  - regdiag sha256: `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`.
  - confirms purely additive header extension — no live code references the new declarations yet.
- non-claims:
  - no Apple PCIe packet subclass synthesis
  - no raw `packet+0x78` writes
  - no packet allocation/input/output behavior change
  - no forced EAPOL/key/RSN/DHCP/link/data success
  - no retry, delay, poll loop, replay, packet synthesis, or deauth masking
  - no calls from any local code path to the ten new InterfaceMonitor methods
  - no declared base class or data layout for `IO80211InterfaceMonitor` beyond what was already present
  - no inclusion of kernel-internal types whose definitions are not yet recovered

## 183. IO80211Peer capability/credit/counter accessor helpers undeclared (REFERENCE_ALIGNMENT_FIX, supersedes 182)

- locus: `include/Airport/IO80211Peer.h`
- evidence: `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/123_peer_cap_credit_counter_2026_04_28.yaml`
- delta-from-reference:
  - CR-177 declared six Peer helpers (`withAddressAndManager`, `init`,
    `getMacAddress`, `setMacAddress`, `getManager`, `getGeneration`).
    Capability/credit/counter helpers were missing.
  - HT/VHT/HE/6E capability getters/setters undeclared.
  - `hasHTorVHTCaps`, `canTransmit`, `canTransmitReason` undeclared.
  - `getOpenCredits`, `getCloseCredits`, `getNumTxPacket`,
    `getOutputSuccess` undeclared.
  - `getTxQuantum`, `setTxQuantum`, `getNextTxSeq`, `setTransmitOk`
    undeclared.
  - `getRxSequence`, `getRxSequenceMulticast` undeclared.
  - `isCachedInFw`, `setCachedInFw`, `isSoftAPPeer`, `setSoftAPPeer`
    undeclared.
- exact correction:
  - extend `include/Airport/IO80211Peer.h` `class IO80211Peer` body
    with twenty-five non-virtual public method declarations matching
    the BootKC signatures.
  - anchor each new declaration to its BootKC address in the header
    preamble next to the CR-177 entries.
- binary invariance:
  - generated kext sha256 unchanged: `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`.
  - UUID unchanged: `BA3D771F-F079-33FF-94E5-C792E66237D8`.
  - regdiag sha256: `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`.
  - confirms purely additive header extension — no live code references the new declarations yet.
- non-claims:
  - no Apple PCIe packet subclass synthesis
  - no raw `packet+0x78` writes
  - no packet allocation/input/output behavior change
  - no forced EAPOL/key/RSN/DHCP/link/data success
  - no retry, delay, poll loop, replay, packet synthesis, or deauth masking
  - no calls from any local code path to the twenty-five new Peer methods
  - no declared base class or data layout for `IO80211Peer` beyond what was already present
  - no inclusion of kernel-internal types whose definitions are not yet recovered

## 184. IO80211Peer RSSI/packet-stats/cache/queue helpers undeclared (REFERENCE_ALIGNMENT_FIX, supersedes 183)

- locus: `include/Airport/IO80211Peer.h`
- evidence: `docs/wifi_reverse_yaml_bundle_FULL_FIXED_v15/124_peer_rssi_stats_cache_2026_04_28.yaml`
- delta-from-reference:
  - CR-177/CR-183 declared 31 Peer helpers (identity, capability,
    credit, counter). Per-band RSSI accounting, packet-stats accessors,
    cache-state queries, and queue/lifetime helpers were missing.
  - `getStatsID`, `getStatsIDValid` undeclared.
  - `reportRssi`, `reportChainRssi`, `getAvgRssi24G`, `getAvgRssi5G`,
    `getAvgRssiAcrossBands`, `getAvgChainRssi5G`, `setPeerAvgRssi24G`,
    `setPeerAvgRssi5G` undeclared.
  - `simulateDPS`, `freeResources`, `unpauseQueues`, `reclaimPackets`,
    `clearCacheState` undeclared.
  - `getRxBitField`, `getRxBitFieldMulticast`, `incrementRxCount`
    undeclared.
  - `getPacketStats`, `getPacketStatsRealTimeRx`,
    `getPacketStatsRealTimeTx`, `getCumDataStats` undeclared.
  - `hasRealTimeData`, `hasLowLatencyData`, `hasQueuedPackets`
    undeclared.
  - `getDataLinkCount`, `logPeerTxLatency`, `updateQueueState`,
    `setPacketLifetime` undeclared.
  - `getCacheTimeStamp`, `setCacheTimeStamp` undeclared.
  - `isBssSteeringPeer`, `isBssSteeringPeerSyncState` undeclared.
- exact correction:
  - extend `include/Airport/IO80211Peer.h` `class IO80211Peer` body
    with thirty-three non-virtual public method declarations matching
    the BootKC signatures.
  - add forward declaration for `apple80211_channel`.
  - anchor each new declaration to its BootKC address in the header
    preamble next to the CR-183 entries.
- binary invariance:
  - generated kext sha256 unchanged: `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`.
  - UUID unchanged: `BA3D771F-F079-33FF-94E5-C792E66237D8`.
  - regdiag sha256: `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`.
  - confirms purely additive header extension — no live code references the new declarations yet.
- non-claims:
  - no Apple PCIe packet subclass synthesis
  - no raw `packet+0x78` writes
  - no packet allocation/input/output behavior change
  - no forced EAPOL/key/RSN/DHCP/link/data success
  - no retry, delay, poll loop, replay, packet synthesis, or deauth masking
  - no calls from any local code path to the thirty-three new Peer methods
  - no declared base class or data layout for `IO80211Peer` beyond what was already present
  - no inclusion of kernel-internal types whose definitions are not yet recovered

## 185. IO80211PeerManager parameterless accessor surface (CR-185, 2026-04-28)

- discrepancy: `IO80211PeerManager` exports thirty-three parameterless /
  single-primitive accessors that the local
  `include/Airport/IO80211PeerManager.h` did not declare.
- BootKC addresses (IO80211Family, 2026-04-28):
  - `getBSDName` `0xffffff80021c90aa`
  - `GetProvider` `0xffffff80021df576`
  - `getController` `0xffffff80021c6bd2`
  - `getInterfaceId` `0xffffff80021c8648`
  - `getCommandGate` `0xffffff80021df390`
  - `interfaceMonitor` `0xffffff80021cea00`
  - `getCountryCode` `0xffffff80021df8bc`
  - `getDTIMPeriod` `0xffffff80021df5ee`
  - `getBeaconPeriod` `0xffffff80021df5de`
  - `getEnabling` `0xffffff80021ccbba`
  - `failToEnable` `0xffffff80021c9d2c`
  - `getHeCapable` `0xffffff80021df650`
  - `getVhtCapable` `0xffffff80021df63e`
  - `getMyHeCap` `0xffffff80021df89c`
  - `getMyVhtCap` `0xffffff80021df88c`
  - `getRsdbCap` `0xffffff80021df8ac`
  - `getHtCapabilities` `0xffffff80021df87c`
  - `isRsdbSupported` `0xffffff80021df0ee`
  - `onDispatchQueue` `0xffffff80021dfb38`
  - `isPeerCacheFull` `0xffffff80021d4c0c`
  - `printHashTable` `0xffffff80021d4f0c`
  - `removeAllPeers` `0xffffff80021d46a0`
  - `freeResources` `0xffffff80021c93a4`
  - `awdlChipReset` `0xffffff80021ccf5c`
  - `flushFreeMbufs` `0xffffff80021cc734`
  - `enablemDNSTx` `0xffffff80021dba82`
  - `destroyReporters` `0xffffff80021c9a56`
  - `updateAllReports` `0xffffff80021d9338`
  - `getScanningState` `0xffffff80021df8cc`
  - `getOutputBEBytes` `0xffffff80021dfc24`
  - `getOutputBKBytes` `0xffffff80021dfc36`
  - `getOutputVIBytes` `0xffffff80021dfc48`
  - `getOutputVOBytes` `0xffffff80021dfc5a`
- justification class: `REFERENCE_ALIGNMENT_FIX` (header-only,
  no caller wiring)
- local change: extends `class IO80211PeerManager` body with the
  thirty-three new method declarations and anchors each declaration to
  its BootKC address in the header preamble next to the CR-179 entries.
- binary invariance:
  - generated kext sha256 unchanged: `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`.
  - UUID unchanged: `BA3D771F-F079-33FF-94E5-C792E66237D8`.
  - regdiag sha256: `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`.
  - confirms purely additive header extension — no live code references the new declarations yet.
- non-claims:
  - no Apple PCIe packet subclass synthesis
  - no raw `packet+0x78` writes
  - no packet allocation/input/output behavior change
  - no forced EAPOL/key/RSN/DHCP/link/data success
  - no retry, delay, poll loop, replay, packet synthesis, or deauth masking
  - no calls from any local code path to the thirty-three new PeerManager methods
  - no declared base class or data layout for `IO80211PeerManager` beyond what was already present
  - no inclusion of kernel-internal types whose definitions are not yet recovered

## 186. IO80211InfraInterface LQM/WMM/AVC/BT-coex/SIB/ULLA/AWDL/BPF/leaky-AP/supplicant/P2P helper surface (CR-186, 2026-04-28)

- discrepancy: `IO80211InfraInterface` exports twenty-one non-virtual
  helpers using only primitive argument types that the local
  `include/Airport/IO80211InfraInterface.h` did not declare.
- BootKC addresses (IO80211Family, 2026-04-28):
  - `getLQMData` `0xffffff80022e4446`
  - `setLQMGated` `0xffffff80022e451c`
  - `setLQMStatic` `0xffffff80022e44c4`
  - `getMonitorMode` `0xffffff80022e5d1e`
  - `getWMMBWReset` `0xffffff80022e5ca2`
  - `setWMMBWReset` `0xffffff80022e5cb8`
  - `getAVCAdvisory` `0xffffff80022e14cc`
  - `getBtCoexState` `0xffffff80022e66e0`
  - `resetInterface` `0xffffff80022e190e`
  - `getTrafficMonitor` `0xffffff80022e5dce`
  - `finishSIBCoexTimer` `0xffffff80022e1386`
  - `resetSIBTurnOnMetrics` `0xffffff80022e3aca`
  - `getCoPTxRTSFailCount` `0xffffff80022e3a8a`
  - `getULLALiteDuration` `0xffffff80022e3a9e`
  - `getAwdlMaxBandWidth` `0xffffff80022e39e6`
  - `notifyAWDLStateChange` `0xffffff80022e57fe`
  - `bpfTapInternal` `0xffffff80022e58d6`
  - `setLeakyAPStatsMode` `0xffffff80022e3784`
  - `UpdateULLADuration` `0xffffff80022e12f2`
  - `handleSupplicantEvent` `0xffffff80022e1e0c`
  - `routeToP2PInterface` `0xffffff80022e1e3a`
- justification class: `REFERENCE_ALIGNMENT_FIX` (header-only,
  no caller wiring)
- local change: extends `class IO80211InfraInterface` body with the
  twenty-one new method declarations and anchors each declaration to
  its BootKC address in the header preamble next to the CR-181
  entries.
- binary invariance:
  - generated kext sha256 unchanged: `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`.
  - UUID unchanged: `BA3D771F-F079-33FF-94E5-C792E66237D8`.
  - regdiag sha256: `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`.
  - confirms purely additive header extension — no live code references the new declarations yet.
- non-claims:
  - no Apple PCIe packet subclass synthesis
  - no raw `packet+0x78` writes
  - no packet allocation/input/output behavior change
  - no forced EAPOL/key/RSN/DHCP/link/data success
  - no retry, delay, poll loop, replay, packet synthesis, or deauth masking
  - no calls from any local code path to the twenty-one new InfraInterface methods
  - no declared base class or data layout for `IO80211InfraInterface` beyond what was already present
  - no inclusion of kernel-internal types whose definitions are not yet recovered

## 187. IO80211SkywalkInterface non-virtual helper surface (CR-187, 2026-04-28)

- discrepancy: `IO80211SkywalkInterface` exports twenty non-virtual
  helpers using only primitive / already-known opaque types that the
  local `include/Airport/IO80211SkywalkInterface.h` did not declare.
- BootKC addresses (IO80211Family, 2026-04-28):
  - `pidLockPid` `0xffffff800227849c`
  - `setPidLock` `0xffffff80022772e6`
  - `getWorkQueue` `0xffffff8002276fcc`
  - `getInterfaceId` `0xffffff8002274c8e`
  - `getPeerManager` `0xffffff8002274c7c`
  - `getPeerMonitor` `0xffffff8002276fde`
  - `setInitMacAddress` `0xffffff80022770fa`
  - `getMacAddressAgent` `0xffffff8002278916`
  - `getParentInterface` `0xffffff8002278466`
  - `getInterfaceMonitor` `0xffffff8002277cb4`
  - `getInterfaceRoleStr` `0xffffff8002274bbc`
  - `isLowLatencyEnabled` `0xffffff800227848a`
  - `postMessageInternal` `0xffffff80022772b2`
  - `postMessageSync` `0xffffff800227776e`
  - `routeIoctlToWcl` `0xffffff80022788a4`
  - `getDeviceType` `0xffffff8002278414`
  - `setDeviceType` `0xffffff8002278428`
  - `getMediumType` `0xffffff80022771f0`
  - `getPowerState` `0xffffff80022774f2`
  - `getPropertyTable` `0xffffff80022783cc`
  - `isCommandAllowed` `0xffffff8002276868`
- justification class: `REFERENCE_ALIGNMENT_FIX` (header-only,
  no caller wiring)
- local change: extends `class IO80211SkywalkInterface` body with the
  twenty new method declarations and anchors each declaration to its
  BootKC address in the header preamble; documents five deferred
  exports (`getBSDName`, `getHardwareAddress`, `setHardwareAddress`,
  `stringFromReturn`, `errnoFromReturn`) that would implicitly override
  a parent-class virtual and break bit-identity.
- binary invariance:
  - generated kext sha256 unchanged: `c1d6e7b134c70c8db158a6d270379684d992f1e67bc51bdfa220c7437929aaf8`.
  - UUID unchanged: `BA3D771F-F079-33FF-94E5-C792E66237D8`.
  - regdiag sha256: `6915020cdd70a07c4b77b2946dd5605bc378fc0677119506ae691a7968f01fad`.
  - confirms purely additive header extension — no live code references the new declarations yet.
- non-claims:
  - no Apple PCIe packet subclass synthesis
  - no raw `packet+0x78` writes
  - no packet allocation/input/output behavior change
  - no forced EAPOL/key/RSN/DHCP/link/data success
  - no retry, delay, poll loop, replay, packet synthesis, or deauth masking
  - no calls from any local code path to the twenty new SkywalkInterface methods
  - no declared base class or data layout for `IO80211SkywalkInterface` beyond what was already present
  - no inclusion of kernel-internal types whose definitions are not yet recovered

## 188. IO80211InterfaceMonitor extended primitive helpers (CR-188)

- date: 2026-04-28
- class: REFERENCE_ALIGNMENT_FIX
- supersedes: CR-187
- file: include/Airport/IO80211InterfaceMonitor.h

Adds twenty-seven non-virtual primitive-only public method
declarations recovered from BootKC IO80211Family on 2026-04-28:
- effective rate getters/setters: getEffectiveLinkRate / setEffectiveLinkRate /
  getEffectiveDataTransferRate / setEffectiveDataTransferRate / setDataTransferRates
- expected-peak latency: setExpectedPeakLatency
- CCA family: getInterfaceAverageCCA / hasInterfaceAverageCCA / setInterfaceAverageCCA
- traffic mix: setInterfaceOpenPercent / setInterfaceOFDMDesense
- DPS counters: incrementDPSDetected / incrementConsecutiveDPS
- leaky-AP validators / resetters: isBssidMetricsLoaded / isLeakyApSsidBssidValid /
  isLeakyApSsidMatchesSsidMetrics / resetLeakyApSsidMetrics / resetLeakyApBssidMetrics
- leaky-AP updaters: updateLeakyApStatus / updateLeakyApNetwork
- activity / LQM: setPreviousInterfaceActivity / setLQM
- range / BW queries: getLowRxRatePeriodRange / getEffectiveRxBWSinceLastRead /
  getEffectiveTxBWSinceLastRead
- aggregation / load: aggregatedPeersTxLatency / loadLeakyApBssidMetricsFromSsidMetrics

Local kext does not call them; declarations only. Verified against
kernel SDK (Kernel.framework) — no parent-class virtual collides
with any of these names. Bit-identical to CR-187.

Non-claims:
- no synthesis of `AppleBCMWLANPCIeSkywalkPacket`
- no `packet+0x78` write
- no change to packet allocation/queueing/input/output
- no forced EAPOL/key/RSN/DHCP/link/data success
- no retry, delay, poll loop, replay, packet synthesis, or deauth masking
- no calls from any local code path to the twenty-seven new InterfaceMonitor methods
- no declared base class or data layout for `IO80211InterfaceMonitor` beyond what was already present
- no inclusion of kernel-internal types whose definitions are not yet recovered

## 189. IO80211InfraInterface additional primitive helpers (CR-189)

- date: 2026-04-28
- class: REFERENCE_ALIGNMENT_FIX
- supersedes: CR-188
- file: include/Airport/IO80211InfraInterface.h

Adds eighteen additional non-virtual primitive-only public method
declarations recovered from BootKC IO80211Family on 2026-04-28:
- 5G band-switch counters: get5GLowHighBandSwitchCounter / SuccessPerc
- CoP SIB-coex turn-on metrics: getCoPSIBCoexTurnOnCount / Duration
- ULLA classic duration: getULLAClassicDuration
- counter resets: resetCoPTxRTSFailCount / resetTxPathHealthCheck
- infra-peers logging: setInfraPeersLoggingEnabled
- data-transfer-rate dispatchers: reportDataTransferRates / Static / Timer
- link-status timer: triggerLinkStatusUpdate
- leaky-AP / multicast / params timers: handleLeakyApStatsResetTimer /
  restoreMulticastStateTimer / updateLinkParametersStatic / updateLinkStatusStatic
- latency / publish: updateTxRxLatency / publishOffloadCapability

Forward declaration `class IO80211TimerSource;` added.

Local kext does not call them; declarations only. Bit-identical to
CR-188.

Non-claims:
- no synthesis of `AppleBCMWLANPCIeSkywalkPacket`
- no `packet+0x78` write
- no change to packet allocation/queueing/input/output
- no forced EAPOL/key/RSN/DHCP/link/data success
- no retry, delay, poll loop, replay, packet synthesis, or deauth masking
- no calls from any local code path to the eighteen new InfraInterface methods
- no declared base class or data layout for `IO80211InfraInterface` beyond what was already present
- no inclusion of kernel-internal types whose definitions are not yet recovered

## 190. IO80211SkywalkInterface companion-id / pid-lock / dispatch helpers (CR-190)

Anomaly: BootKC IO80211Family.kc exports eight additional non-virtual
direct-call methods of `IO80211SkywalkInterface` not declared by the
local header after CR-187:

- companion interface-id: getCompanionInterfaceId / setCompanionInterfaceId
- pid lock: pidLocked
- low-latency: setLowLatencyEnabled
- time-sync mac: updateTimeSyncMacAddress(ether_addr&)
- dispatch: validateDispatchQueue / getControllerWorkQueue
- ioctl trace: storeProcessNameAndIoctlInformation(unsigned long)

Excluded (already declared as virtuals in same class body): attachPeer,
detachPeer, cachePeer, findPeer, getSelfMacAddr, getFeatureFlags,
getDataQueueDepth, handleChosenMedia, flushPacketQueues,
isChipInterfaceReady, isCommandProhibited, isInterfaceEnabled,
setRunningState, getSupportedMediaArray, setPromiscuousModeEnable,
shouldLog, getLastQueuePacketTime, getLastRxUnicastLinkActivityTime,
logTxLatency, logRxLatency, getLastTxTimeStamp, getLastRxTimeStamp,
setDebugTrafficReport.

Local kext does not call the new helpers; declarations only.
Bit-identical to CR-189.

Non-claims:
- no synthesis of `AppleBCMWLANPCIeSkywalkPacket`
- no `packet+0x78` write
- no change to packet allocation/queueing/input/output
- no forced EAPOL/key/RSN/DHCP/link/data success
- no retry, delay, poll loop, replay, packet synthesis, or deauth masking
- no calls from any local code path to the eight new SkywalkInterface methods
- no declared base class or data layout for `IO80211SkywalkInterface` beyond what was already present
- no inclusion of kernel-internal types whose definitions are not yet recovered

## 191. IO80211PeerManager primitive-only helper batch (CR-191)

Anomaly: BootKC IO80211Family.kc exports twenty additional non-virtual
direct-call methods of `IO80211PeerManager` not declared by the local
header after CR-185:

- channel: modifyChID(unsigned long long)
- printers: printPeers(unsigned int, unsigned int)
- mDNS toggles: getBlockMdns / setBlockMdns / getBlockMdnsTx / setBlockMdnsTx
- toggles: setP2PLogging(bool) / setDisplayState(bool) / setScanOn2GOnly(bool)
- counters: setPeersCount, getTxQueueStamp / setTxQueueStamp,
  updateCtlCount, updateRxPackets
- timing: setBeaconPeriod, setDTIMPeriod
- queries: is24GOnlyScan, macAddressEqual
- save: saveCountryCode(unsigned char*)
- report: reportP2PCCA(unsigned char, unsigned int x4)

None of these names match a parent-class virtual or a virtual already
declared in the same class body.

Local kext does not call the new helpers; declarations only.
Bit-identical to CR-190.

Non-claims:
- no synthesis of `AppleBCMWLANPCIeSkywalkPacket`
- no `packet+0x78` write
- no change to packet allocation/queueing/input/output
- no forced EAPOL/key/RSN/DHCP/link/data success
- no retry, delay, poll loop, replay, packet synthesis, or deauth masking
- no calls from any local code path to the twenty new PeerManager methods
- no declared base class or data layout for `IO80211PeerManager` beyond what was already present
- no inclusion of kernel-internal types whose definitions are not yet recovered

## 192. IO80211Peer state-flag and counter accessor batch (CR-192)

Anomaly: BootKC IO80211Family.kc exports thirty additional non-virtual
direct-call methods of `IO80211Peer` not declared by the local header
after CR-184:

- HT/VHT IE-present flags: getHtOperationIEPresent / setHtOperationIEPresent /
  getVhtOperationIEPresent / setVhtOperationIEPresent
- peer add/delete request: getPeerAddRequestedState / setPeerAddRequestedState /
  getPeerDeleteRequestedState / setPeerDeleteRequestedState /
  isPeerAddRequestInProgress / setPeerAddRequestInProgress /
  isPeerDeleteRequesetInProgress / setPeerDeleteRequestInProgress
- BSS steering: setBssSteeringPeerSyncState
- Bonjour rx: getUnicastBonjourRx / setUnicastBonjourRx /
  getMulticastBonjourRx / setMulticastBonjourRx
- beacon: getBeaconReceivedCount / incrementBeaconReceivedCount
- data links: getTotalDataLinkCount / incrementTotalDataLinks /
  decrementTotalDataLinks / incrementDataLinks / decrementDataLinks
- realtime sessions: getRealTimeDataSessionCount /
  incrementRealTimeDataSession / decrementRealTimeDataSession
- low-latency sessions: getLowLatencyDataSessionCount /
  incrementLowLatencyDataSession / decrementLowLatencyDataSession

Local kext does not call the new helpers; declarations only.
Bit-identical to CR-191.

Non-claims:
- no synthesis of `AppleBCMWLANPCIeSkywalkPacket`
- no `packet+0x78` write
- no change to packet allocation/queueing/input/output
- no forced EAPOL/key/RSN/DHCP/link/data success
- no retry, delay, poll loop, replay, packet synthesis, or deauth masking
- no calls from any local code path to the thirty new Peer methods
- no declared base class or data layout for `IO80211Peer` beyond what was already present
- no inclusion of kernel-internal types whose definitions are not yet recovered

## 193. IO80211Peer timestamp / link-activity / cache-time batch (CR-193)

Anomaly: BootKC IO80211Family.kc exports twenty-four additional non-
virtual direct-call methods of `IO80211Peer` not declared by the
local header after CR-192:

- Rx link activity: getLastRxUnicastLinkActivity / setLastRxUnicastLinkActivity /
  getLastRxMulticastLinkActivity / setLastRxMulticastLinkActivity
- peer activity: getPeerLastDataActivityTimeMsec /
  getPeerDataInActivityExceededThreshold
- peer presence: getLastPeerPresencePosted / setLastPeerPresencePosted /
  setPeerDiscoveredTime
- caching: getCachingDeniedTimeStamp / setCachingDeniedTimeStamp /
  getLastCacheAddAttempt / getWaitingToBeUnCachedTimeStamp
- data log: getLastDataLogTimeStamp / setLastDataLogTimeStamp
- output: getLastOutputSuccess / setLastOutputSuccess
- chain RSSI: getTimeOfFirstChainRssiSample / setTimeOfFirstChainRssiSample
- queue: getLastQueuePacket / setLastQueuePacket
- tx-status log: getNumTransmitStatusLog / getNumTxStatusMismatch /
  setNumTxStatusMismatch

Local kext does not call the new helpers; declarations only.
Bit-identical to CR-192.

Non-claims:
- no synthesis of `AppleBCMWLANPCIeSkywalkPacket`
- no `packet+0x78` write
- no change to packet allocation/queueing/input/output
- no forced EAPOL/key/RSN/DHCP/link/data success
- no retry, delay, poll loop, replay, packet synthesis, or deauth masking
- no calls from any local code path to the twenty-four new Peer methods
- no declared base class or data layout for `IO80211Peer` beyond what was already present
- no inclusion of kernel-internal types whose definitions are not yet recovered

## 194. IO80211Peer caching-state and tx-counter batch (CR-194)

Anomaly: BootKC IO80211Family.kc exports thirty-three additional non-
virtual direct-call methods of `IO80211Peer` not declared by the
local header after CR-193:

- caching state: setStateForCachedPeer / setPreCachingStateForPeer /
  setPreUnCachingStateForPeer / clearPreUnCachingStateForPeer /
  getPreUnCachingStateForPeer / setReservationEnabled /
  clearReservationEnabled / ifCacheReservationEnabled /
  isPeerDeniedCachingForThisSession /
  setPeerDeniedCachingForThisSession /
  getReceivedSidecarRequest / setReceivedSidecarRequest
- low-latency idle: setLowLatencyLinkIdle / clearLowLatencyLinkIdle /
  isLowLatencyLinkIdle
- cache wait: isWaitingToBeCached / setWaitingToBeCached /
  isWaitingToBeUnCached
- updates: updateRequestBitField / updateNumHostPackets /
  updateAllReports / updateCumDataStats / updateIntervalDataStats /
  clearIntervalDataStats / updateTxPacketStats /
  allocTxLatencyStorage / freeTxLatencyStorage
- tx counters: incrementTxOkCount / incrementTxQueueCount /
  incrementTxFailNoAckCount / incrementTxFailOtherCount
- llw: llwLoadPacketLifetimeHistogram /
  llwComputeTxConsecutiveErrorCount

Local kext does not call the new helpers; declarations only.
Bit-identical to CR-193.

Non-claims:
- no synthesis of `AppleBCMWLANPCIeSkywalkPacket`
- no `packet+0x78` write
- no change to packet allocation/queueing/input/output
- no forced EAPOL/key/RSN/DHCP/link/data success
- no retry, delay, poll loop, replay, packet synthesis, or deauth masking
- no calls from any local code path to the thirty-three new Peer methods
- no declared base class or data layout for `IO80211Peer` beyond what was already present
- no inclusion of kernel-internal types whose definitions are not yet recovered

## item 196 — IO80211LinkQualityMonitor primitive-only batch (NEW header)

- class: IO80211LinkQualityMonitor (NEW local header)
- batch_size: 48
- header: include/Airport/IO80211LinkQualityMonitor.h
- supersedes: item 195 (IO80211Peer caching-state batch)
- justification: REFERENCE_ALIGNMENT_FIX

Helpers (BootKC, IO80211Family.kc, recovered 2026-04-28): induced
fault injectors (induceTxBadPhy/induceTxLatency/induceTxLowPhyRate/
induceRxBadPhy/induceRxHighOverflow/induceRxHighPNReplay/
induceRxHighDecryptError/induceRxHighMCDecryptError/
induceRxAmpduDupErrors/induceSlowWiFiIfDebugTriggered), measurement
reset/analyze (resetLatencyCoP/resetMeasurements/analyzeTxLatency/
analyzeMeasurements(bool)), input/output triggers (requestUserInput/
triggerUserInput/triggerLinkProbe(bool)/triggerIPFailRecovery), caps
and AWDL state updaters (updateCcaExtCaps(bool)/
updateRealTimeAWDLActiveState(bool)), event recorders
(recordPhyActivity(long long, bool)/recordPhyRate(unsigned int, bool)/
recordTxLatency(unsigned long long)/recordAMPDUDensity(unsigned int)/
recordSymptomsInput(unsigned long long)/recordRxPacket(unsigned long
long, unsigned long long)/recordLinkProbeResult(int)/
recordUserInputResult(int)/recordAWDLInfraDutyCycle(unsigned int)/
recordEscoTrafficIndication(unsigned int)/
recordInterfaceConcurrencyState(bool)), index lookups (getCCAIndex/
getRSSIIndex/getNSSIndex/getExpectedPhyRate), accessor getters
(getCcaExtCaps/getChannelWidth/getElapsedPeriodMS/
getWorstAvgLatencyCoP/getWorstMaxLatencyCop/getOffChannelDurationUS/
getTVPMActiveDurationMS/getMaxQueueFullDurationMS/
getRealTimeAWDLActiveState/getConcurrentInterfaceActiveDurationMS/
getPeerMacAddress) and timer arming/timeout
(armMeasurementTimer/checkMeasurementTimeout). All declarations use
only primitive types.

### Non-claims (item 196)

- no synthesis of `AppleBCMWLANPCIeSkywalkPacket`
- no `packet+0x78` write
- no change to packet allocation/queueing/input/output
- no forced EAPOL/key/RSN/DHCP/link/data success
- no retry, delay, poll loop, replay, packet synthesis, or deauth masking
- no calls from any local code path to the 48 LQM methods
- no declared base class or data layout for `IO80211LinkQualityMonitor`
- no inclusion of kernel-internal types whose definitions are not yet recovered

## item 198 — IO80211BSSBeacon primitive-only batch (NEW header)

- class: IO80211BSSBeacon (NEW local header)
- batch_size: 102
- header: include/Airport/IO80211BSSBeacon.h
- supersedes: item 196 (IO80211LinkQualityMonitor primitive-only batch)
- resubmission: CR-199 supersedes CR-198 (5 unrecovered void*-typed
  helpers deferred — getLogger / getSSID / getOWETransSSID /
  getRnRContext / getQueueChain)
- justification: REFERENCE_ALIGNMENT_FIX

Helpers (BootKC, IO80211Family.kc, recovered 2026-04-28): vendor /
network identification probes (isLikelyOrbiNetwork / isAppleNetwork /
isIOSDevice / isLikelyAlpineBMWNetwork / isNBTEvoBMWNetwork /
isCarPlayDongle / isWACNetwork), capability and timing setters
(setCapabilities / setBeaconPeriod / updateRSSI / updateSNR /
updateNoise / setNonTransmittedBssidIndex / setInterworkingIEPresent /
setInternetAccess), rate-set computation and AKM/dump helpers
(calculateRates / getAKMs / dumpBeacon), RSSI/SNR/probe/internet
accessibility const accessors (getRSSI / getSNR /
isDirectedProbeNetwork / isInternetAccessible), channel updater and
current-BSS mark/clear/predicate (updateChannel / setAsCurrent /
removeAsCurrent / isCurrent), SSID-cstr / address / ie-list-length
getters (getSSIDCStr / getOWETransSSIDLength / getSSIDLength /
isOWETrans / getAddress / getIeListLength), LQM / noise / noise-delta /
channel / band / chan-spec / DTIM-period / beacon-period /
ATIM-window / listen-interval / capabilities const accessors
(hasLQMResult / getNoise / getNoiseDeltaOverTwoCores /
updateNoiseDeltaOverTwoCores / getChannel / getBand / getChanSWSpec /
getChanPrimarySWSpec / getDTIMPeriod / getBeaconPeriod /
getATIMWindow / getListenInterval / getCapabilities), HT-rx-rates /
rx-rate / rx-rate-percent / rx-rate / CCA mutators (getHtRxRates /
getRxRate / getRxRatePercent / updateRxRate / updateCCA), short-SSID
matcher and age/timestamp accessors (shortSSIDMatches /
getAgeInSeconds / getAgeInMS / getTimestamp), encryption / WEP / WPA /
HT / VHT / HE / AP predicates (isPrivacyEnabled / isWEPEnabled /
isWPAEnabled / isHTEnabled / isVHTEnabled / isHEEnabled / isAP),
encryption-mode / AP-mode getters (getEncryptionMode / getAPMode),
blacklist / hidden mutators and predicates (setBlacklisted /
isBlacklisted / setHidden / isHidden), max/min rate getters
(getMaxRate / getMinRate), capability-tree predicates
(isProxyARPSupported / isTIMBroadcastSupported / isDMSSupported /
isBSSTransMgmtSupported / isBSSQoSMgmtMSCSSupported /
isBSSBeaconProtectionCapable / isBSSSAEPKCapable /
isBSSSAEPKPwdExclsivelyUsed / isBSSOCVCapable /
isFastBSSTransitionSupported / isNeighborReportSupported /
isWiFiNetworkFullyLoaded / isScoreComputed / isEhtEnabled /
isQosFastLaneEnabled / isNwAssuranceEnabledInCCXIE /
isSameSSIDCoLocatedAP / isSplitSSIDCoLocatedAP), FT / MFP / PMKSA /
interworking / wifi-info accessors (isFtEnabled / isBssMfpCapable /
getPMKSAExpiration / isInterworkingIEPresent /
isWiFiNetworkInfoAvailable / setPMKSAExpiration /
getNonTransmittedBssidIndex), FILS / beacon-at-HE-rate predicates
(isFILSDiscoveryFrame / isBeaconAtHeRate), current-BSS-AKMs /
multi-BSSID / MLD getters and predicates (getCurrentBSSAKMs /
getMultiBssidOffset / getMldAddress / isNonTransmittedBssid / isMld).
All declarations use only primitive parameter and primitive-pointer
return types.

Deferred from CR-198 (return types are unrecovered kernel-internal
pointers; will be re-introduced once decomp evidence documents the
actual reference return types): getLogger() const, getSSID() const,
getOWETransSSID() const, getRnRContext() const, getQueueChain().

### Non-claims (item 198)

- no synthesis of `AppleBCMWLANPCIeSkywalkPacket`
- no `packet+0x78` write
- no change to packet allocation/queueing/input/output
- no forced EAPOL/key/RSN/DHCP/link/data success
- no calls from any local code path to the 102 BSSBeacon methods
- no declared base class or data layout for `IO80211BSSBeacon`
- no inclusion of kernel-internal types whose definitions are not yet recovered

## item 200 — IO80211BssManager primitive-only batch (NEW header) [SUPERSEDED]

- class: IO80211BssManager (NEW local header)
- batch_size (CR-200): 41 — REJECTED (return types declared by
  naming-convention inference, not decompile evidence)
- supersedes: item 198 (IO80211BSSBeacon primitive-only batch)
- superseded_by: item 201

## item 201 — IO80211BssManager primitive-only batch (decomp-evidenced)

- class: IO80211BssManager (NEW local header)
- batch_size: 14
- header: include/Airport/IO80211BssManager.h
- supersedes: item 200 (CR-200 41-helper batch)
- justification: REFERENCE_ALIGNMENT_FIX

Helpers (BootKC, IO80211Family.kc, recovered 2026-04-28): the
fourteen helpers from CR-200's forty-one-symbol candidate set whose
return type is unambiguously recovered by Ghidra 12.2's C decompile
of `BootKernelExtensions.kc` (output captured in
`analysis/cr201_bssmgr_decomp.c`):

- void setters: resetRateAndIndexSet / setAdHocCreated(bool) /
  setSISOAssoc(bool) / setPrivateMacJoinStatus(bool) /
  setDeviceTypeInDhcpAllowStatus(bool) /
  setAssociateToHotspotInWoWMode(bool) / set6gStandAloneTopology(bool)
- bool predicate: isAssociatedToAdhoc
- byte/unsigned-char predicate: isAssociatedOnHighBand
- ulong/unsigned-long getters and predicates:
  isAssociatedToiOSDevice / getPrivateMacJoinStatus /
  getDeviceTypeInDhcpAllowStatus / isAssociateToHotspotInWoWMode /
  get6gStandAloneTopology

Deferred (twenty-seven from the CR-200 candidate set): twelve
helpers whose decompile produced an opaque placeholder return type
(`undefined4` / `undefined8`); nine helpers whose decompile did not
recover the BootKC mangled symbol name (Ghidra emitted
`FUN_<addr>`); six helpers whose decompile reported MISSING at the
recorded address. The kernel-internal-typed deferred set documented
under item 200 also remains deferred.

### Non-claims (item 201)

- no synthesis of `AppleBCMWLANPCIeSkywalkPacket`
- no `packet+0x78` write
- no change to packet allocation/queueing/input/output
- no forced EAPOL/key/RSN/DHCP/link/data success
- no calls from any local code path to the 14 BssManager methods
- no declared base class or data layout for `IO80211BssManager`
- no inclusion of kernel-internal types whose definitions are not yet recovered
- no `void *` substitution for any unrecovered kernel-internal return type
- no promotion of `undefined4`/`undefined8` decompile placeholders to
  concrete C++ return types

## item 202 — IO80211BssManager current-BSS rate/MCS writer seeding

- class: IO80211BssManager
- header: include/Airport/IO80211BssManager.h
- implementation: AirportItlwmSkywalkInterface.cpp::seedBssManagerRateAndMcs
- status: closed
- justification: REFERENCE_ALIGNMENT_FIX

Reference producer evidence:

- `AppleBCMWLANNetAdapter::updateRateSetAsyncCallback(...)` builds an
  `apple80211_rate_set_data` carrier and calls
  `IO80211BssManager::setRateSet(apple80211_rate_set_data&)`.
- `AppleBCMWLANNetAdapter::updateMCSSet(...)` calls
  `IO80211BssManager::setMCSIndexSet(...)`,
  `setVHTMCSIndexSet(...)`, and `setHEMCSIndexSet(...)`.
- symbol anchors are captured in
  `docs/reference/CR-479-bssmanager-rate-mcs-writer-seeding-20260707.md`.

Local closure:

- the previous verified WCLConfigManager/BssManager pointer route is kept;
- the seeding burst now publishes both the negotiated RATE_SET carrier and the
  existing MCS_INDEX_SET carrier into the framework-owned BssManager cache;
- VHT/HE writer calls remain deferred until their local carrier producers are
  recovered with the same certainty.

## item 203 — CARD_CAPABILITIES legacy shadow content alignment

- producer: `AirportItlwm::getCARD_CAPABILITIES(...)`
- files:
  - `AirportItlwm/AirportItlwmV2.cpp`
  - `AirportItlwm/AirportSTAIOCTL.cpp`
  - `AirportItlwm/TahoeCapabilityContracts.hpp`
- status: closed
- justification: REFERENCE_ALIGNMENT_FIX

The Tahoe controller producer was already sanitized to the Apple-consistent
capability cluster from item 15, but the legacy STA dispatcher shadow still
published the old `0xef / 0x2b / 0x8c` cluster. That left one public path able
to re-advertise Apple-impossible advanced capability bits.

This batch moves the deterministic cluster into
`TahoeCapabilityContracts::applyAppleConsistentCardCapabilityCluster()` and
uses it from both producers. The shared helper preserves:

- `cap[2] = 0x6f`
- `cap[3] = 0x27`
- `cap[5] = 0x40`
- `cap[6] = 0x0c`
- `cap[8..9] = 0x0201`

Reference note:
`docs/reference/CR-479-card-capabilities-shadow-cluster-20260707.md`.

## item 204 — IO80211BssManager VHT/HE MCS writer seeding

- class: IO80211BssManager
- header:
  - `include/Airport/apple80211_ioctl.h`
  - `include/Airport/IO80211BssManager.h`
- implementation: `AirportItlwmSkywalkInterface.cpp::seedBssManagerRateAndMcs`
- status: closed
- justification: REFERENCE_ALIGNMENT_FIX

Reference producer evidence:

- `AppleBCMWLANNetAdapter::updateMCSSet(...)` calls
  `IO80211BssManager::setMCSIndexSet(...)`,
  `setVHTMCSIndexSet(...)`, and `setHEMCSIndexSet(...)`.
- `setVHTMCSIndexSet(...)` and `setHEMCSIndexSet(...)` both copy one qword
  from their public carrier into the BssManager/current-beacon cache path.
- the Apple producer initializes unsupported VHT/HE maps as `0xffff` before
  capability-gated map updates.

Local closure:

- `apple80211_vht_mcs_index_set_data` now has the recovered 8-byte qword
  carrier shape;
- `apple80211_he_mcs_index_set_data` is added with the same qword shape;
- the BssManager header declares the recovered VHT/HE writer exports;
- the current-BSS seeding burst publishes MCS, VHT MCS, and HE MCS carriers
  through the framework-owned BssManager object recovered from WCLConfigManager.

Reference note:
`docs/reference/CR-479-bssmanager-vht-he-mcs-writer-seeding-20260707.md`.

## item 205 — LQM create prerequisite carriers

- class:
  - `AirportItlwm::getCARD_CAPABILITIES(...)`
  - `AirportItlwmSkywalkInterface::getSLOW_WIFI_FEATURE_ENABLED(...)`
- files:
  - `AirportItlwm/TahoeCapabilityContracts.hpp`
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
  - `tests/tahoe_payload_builders_test.cpp`
- status: superseded
- justification: RUNTIME_FALSIFIED

Runtime serial after item 204 still showed repeated
`IO80211QueueCall::handleEntry - called type 3, error 0xe00002c7`. The type-3
queue entry is `IO80211InfraInterface::createLinkQualityMonitor`.

The attempted prerequisite restoration was:

- `capabilities[10] = 0x08`, which loads card-capability index `0x53` into
  `cap+0xb36 bit3`
- `getSLOW_WIFI_FEATURE_ENABLED` returns the compact
  `version + enabled=1` carrier consumed by the LQM option builder

Runtime on 2026-07-07 falsified that layer for the current bridge. With those
prerequisites enabled, association repeatedly panicked in
`IO80211QueueCall::handleEntry` with `Kernel stack memory corruption detected`
after the framework's LQM queue path ran. Reverting both carriers restored a
stable association: 120/120 ping before stress, bidirectional TCP stress for
120 seconds, 150/150 ping during stress, and 10/10 post-stress ping with no new
panic entries.

Until the exact Apple LQM QueueCall/provider wiring is recovered, the local
CARD_CAPABILITIES cluster must stop at `cap[8..9] = 0x0201`, and
`getSLOW_WIFI_FEATURE_ENABLED` must continue reporting the local cached policy
state rather than forcing `enabled=1`.

Reference note:
`docs/reference/CR-479-lqm-create-prerequisites-20260707.md`.

## item 206 — IO80211BssManager current-BSS identity writer seeding

- class: IO80211BssManager
- header: `include/Airport/IO80211BssManager.h`
- implementation: `AirportItlwmSkywalkInterface.cpp::seedBssManagerRateAndMcs`
- status: closed
- justification: REFERENCE_ALIGNMENT_FIX

Reference producer evidence:

- BootKC exports
  `IO80211BssManager::setAssocSSID(unsigned char const*, unsigned long)`
  at `0xffffff800226713c` and
  `IO80211BssManager::setAssocRSNIE(unsigned char const*, unsigned long)`
  at `0xffffff8002267afa`.
- the `setAssocSSID` writer accepts at most 32 bytes, clears the 32-byte
  associated-SSID buffer, writes the length, and copies only on nonzero length.
- the `setAssocRSNIE` writer accepts at most `0x101` bytes, writes the length,
  copies nonzero input into the associated RSN IE cache, and has an explicit
  zero-length clear path.

Local closure:

- the BssManager direct-call header now declares the two recovered
  `IOReturn` writer signatures;
- the existing WCLConfigManager/BssManager seed burst now publishes the
  current node SSID and RSN IE into the framework-owned BssManager object;
- overlength local inputs are not truncated before the Apple writer boundary.
  SSID over 32 is skipped, and missing/empty/overlength RSN IE uses the
  writer's zero-length clear path.

Reference note:
`docs/reference/CR-479-bssmanager-current-bss-identity-writer-seeding-20260707.md`.

## item 207 — CURRENT_NETWORK not-associated status

- producer: `AirportItlwmSkywalkInterface::getCURRENT_NETWORK(...)`
- file: `AirportItlwm/AirportItlwmSkywalkInterface.cpp`
- status: closed
- justification: REFERENCE_ALIGNMENT_FIX

Reference evidence:

- `AppleBCMWLANCore::getCURRENT_NETWORK(apple80211_scan_result*)` at
  `0xffffff80015e6384` calls `IO80211BssManager::isAssociated()`.
- the false branch returns `0xe0822403`; the true branch tail-calls the
  BssManager current-network copier.

Local closure:

- the local not-associated/current-BSS-missing branch now returns
  `0xe0822403` instead of generic `kIOReturnError`;
- the associated success path remains the existing current-node
  `apple80211_scan_result` producer and is not used to fabricate association.

Reference note:
`docs/reference/CR-479-current-network-not-associated-status-20260707.md`.

## item 208 — STA SSID GET current-BSS byte carrier

- producers:
  - `AirportItlwmSkywalkInterface::getSSID(...)`
  - `AirportItlwm::getSSID(...)` in the Tahoe V2 source
  - legacy `AirportSTAIOCTL.cpp::getSSID(...)`
- status: closed
- justification: REFERENCE_ALIGNMENT_FIX

Reference evidence:

- `IO80211Controller::getSSIDData(apple80211_ssid_data*)` is classified at
  `0xffffff8002214f12` in the BootKC target list.
- the static slice at that address routes the SSID carrier through the primary
  Skywalk interface and controller cache owners.
- the recovered `IO80211BssManager::setAssocSSID(...)` associated-SSID writer
  is byte-length based, not C-string based.

Local closure:

- associated STA GET SSID now prefers the current BSS `ni_essid/ni_esslen`;
- desired `ic_des_essid/ic_des_esslen` is retained only as a bounded RUN-state
  fallback;
- all STA GET SSID copies continue returning success with a zeroed carrier
  before association, matching the bootstrap contract;
- all `strlen(ic_des_essid)` SSID carrier production was removed.

Reference note:
`docs/reference/CR-479-ssid-current-bss-byte-carrier-20260707.md`.

## item 209 — Primary OP_MODE associated STA/IBSS publication

- producers:
  - `AirportItlwmSkywalkInterface::getOP_MODE(...)`
  - legacy `AirportSTAIOCTL.cpp::getOP_MODE(...)`
- status: closed
- justification: REFERENCE_ALIGNMENT_FIX

Reference evidence:

- `AppleBCMWLANCore::getOP_MODE(apple80211_opmode_data*)` at
  `0xffffff80015e564a` initializes the output qword to `1`
  (`version=1`, `op_mode=0`).
- the same body calls `IO80211BssManager::isAssociated()` and, on true, calls
  the BssManager current-BSS mode helper at `0xffffff8002266a9c`.
- that helper tests current-BSS bit `0x2`, returning `1` for infrastructure
  STA and `2` for IBSS.

Local closure:

- primary OP_MODE still starts from `version=1, op_mode=0`;
- associated local primary STA now ORs `APPLE80211_M_STA` when the current BSS
  is infrastructure and `APPLE80211_M_IBSS` when `ni_capinfo` has bit `0x2`;
- the APSTA SoftAP mode and monitor bit remain separate owner paths and are not
  fabricated by this primary STA closure.

Reference note:
`docs/reference/CR-479-primary-opmode-carrier-20260707.md`.

## item 210 — public PHY_MODE hardware-supported vector and active BSS carrier

- producers:
  - `AirportItlwmSkywalkInterface::getPHY_MODE(...)`
  - legacy `AirportSTAIOCTL.cpp::getPHY_MODE(...)`
- status: closed
- justification: REFERENCE_ALIGNMENT_FIX

Reference evidence:

- `AppleBCMWLANCore::getPHY_MODE(apple80211_phymode_data*)` at
  `0xffffff80015f7cc6` initializes `version=1` and starts `phy_mode` with
  `APPLE80211_MODE_AUTO`.
- the same body calls `getSupportedBand(unsigned int*)` at
  `0xffffff80015ab73a` and `getSupportedPhyModeFromHW()` at
  `0xffffff80015ab880` before adding public supported PHY bits;
- `active_phy_mode` is written only through the associated BSS path, using
  `AppleBCMWLANCore::getBssPhyModde(AppleBCMWLANBSSBeacon*)` at
  `0xffffff80015dad80`.

Local closure:

- supported `PHY_MODE` now starts with AUTO and derives bands from the
  controller channel table;
- `11n`, `11ac`, and `11ax` are gated by real HT/VHT/HE capability and
  MCS/support carrier data rather than `IEEE80211_F_VHTON` /
  `IEEE80211_F_HEON` current-mode flags;
- the iwn attach path explicitly clears VHT/HE capability carriers before
  publishing its HT-only capability set;
- the active PHY carrier is left UNKNOWN before association and is derived from
  the current BSS after `IEEE80211_S_RUN`;
- direct bound Apple80211 key14 probes now return
  `PHYMODE_SUPPORTED=0x1f` and associated `PHYMODE_ACTIVE=0x10` on the
  active iwn-6030 HT-only link.

WCL/static path evidence:

- `system_profiler -detailLevel full SPAirPortDataType -xml` is handled by
  `IO80211Glue::sendIOUCToWcl(..., 0xe, payload, 0xc, handled)` and does not
  call the local Skywalk `getPHY_MODE` producer;
- `WCLConfigManager::getPHY_MODE(bulletinBoardMessage&)` at
  `0xffffff8002123cc2` writes `phy_mode=0x9f` as its baseline and only extends
  the vector from the internal device-configuration byte at `+0xcad`;
- live tracing on 2026-07-07 showed `cfg_cad=0x0` and WCL returned
  `supported=0x9f, active=0x10`, so `system_profiler` still prints
  `802.11 a/b/g/n/ac`;
- because the Apple WCL handler has no branch that removes the `11ac` baseline,
  this batch does not force WCL fallback or otherwise bypass the reference
  static path.

Reference note:
`docs/reference/CR-479-phy-mode-hw-supported-active-bss-20260707.md`.

## item 211 — association auth mapping stops rewriting pure WPA3 carriers

- producers:
  - `AirportItlwm::associateSSID(...)`
  - `AirportItlwmSkywalkInterface::associateSSID(...)`
- status: closed
- justification: REFERENCE_ALIGNMENT_FIX

Reference evidence:

- the recovered hidden Tahoe association carrier preserves auth policy through
  the assoc-candidates owner and RSN IE pointer/length fields instead of
  rewriting AKM bits in the local association backend;
- public `setRSN_IE` is a success no-op in the reference and is not an AKM
  rewrite point;
- the local `CARD_CAPABILITIES` cluster already stopped advertising
  Apple-impossible advanced AKM support, so pure WPA3 auth carriers must not be
  silently converted into WPA2 carrier bits downstream.

Local closure:

- `TahoeAssociationAuthContracts` now owns the explicit WPA/WPA2/SHA256 auth
  masks that are allowed to program local net80211 WPA state;
- both legacy and Tahoe Skywalk association paths derive the local mapping from
  those explicit supported bits only;
- mixed transition carriers that include a WPA2 PSK bit still enter the local
  WPA2 PSK path;
- pure WPA3 SAE / WPA3 enterprise carriers no longer mutate into WPA2 PSK or
  WPA2 enterprise inside `associateSSID(...)`.

Reference note:
`docs/reference/CR-479-association-auth-no-fallback-rewrite-20260707.md`.

## item 212 — APSTA simple setters no-null-guard contracts

- producer:
  - `AirportItlwmAPSTAOwner::setSoftAPParams(...)`
  - `AirportItlwmAPSTAOwner::setSoftAPExtCaps(...)`
  - `AirportItlwmAPSTAOwner::setMisMaxSta(...)`
- status: closed
- justification: REFERENCE_ALIGNMENT_FIX

Reference evidence:

- `AppleBCMWLANIO80211APSTAInterface::setSOFTAP_PARAMS(...)` at
  `0xffffff800168e536` reads the input fields directly and has no null guard;
- the recovered simple-body layer already records that this setter uses input
  offsets `+0x04/+0x08/+0x0c/+0x10/+0x14/+0x17/+0x18`, APSTA state offsets
  `+0x0e/+0x18/+0x1c/+0x20/+0x24/+0x28/+0x68/+0x26c`, sentinel `0xffff`,
  power-save calls `(0,0)` and `(1,0)`, and returns `0`.
- `AppleBCMWLANIO80211APSTAInterface::setSOFTAP_EXTENDED_CAPABILITIES_IE(...)`
  at `0xffffff800168e7b8` clears state `+0x50..+0x61`, then copies input
  `+0x00/+0x01/+0x09` directly into state `+0x50/+0x51/+0x59` and returns `0`;
- `AppleBCMWLANIO80211APSTAInterface::setMIS_MAX_STA(...)` at
  `0xffffff8001693a80` reads AP-up state `+0x26c`; when AP is up it reads
  input dword `+0x00`, calls `setMaxAssoc(value)`, ignores that helper result,
  and returns `0`.

Local closure:

- the local APSTA owner no longer inserts non-reference
  `nullptr -> kIOReturnBadArgument` branches before those simple-body field
  reads;
- `kAirportItlwmAPSTASetSoftAPParamsHasNullGuard == 0` is now a compiled
  regression witness.
- `kAirportItlwmAPSTASetSoftAPExtCapsHasNullGuard == 0` and
  `kAirportItlwmAPSTASetMisMaxStaHasNullGuardAfterAPUp == 0` are compiled
  regression witnesses for the adjacent simple setters.

Non-claims:

- this does not enable APSTA/HostAP runtime, force AP-up state, send SoftAP
  IOVARs, or change the primary STA datapath.
