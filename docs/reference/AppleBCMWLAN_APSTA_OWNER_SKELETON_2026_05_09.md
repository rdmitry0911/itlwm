# AppleBCMWLAN APSTA Owner Skeleton — Local Implementation Foundation

## Recovered owner model

The reference Apple AP/APSTA layer is owned by
`AppleBCMWLANIO80211APSTAInterface`. That object owns:

- the role-7 virtual interface lifetime;
- the AP/SoftAP private state block (with the AP-up flag at private
  state offset `+0x26c`);
- SoftAP parameters, beacon/DTIM state, hidden-mode state, and
  channel/CSA state;
- the station table at private state `+0xb8` (5 entries with stride
  `0x30`);
- AP stats and monitor timers;
- AP datapath queues and packet pools;
- async completion cookies tied to lower firmware/Core operations.

Ownership is producer-side: lower AP firmware/Core events update the
APSTA owner, and the APSTA owner publishes IO80211/Skywalk/SAP
messages. A consumer cannot manufacture AP station records without the
owner and the lower producer.

## Local implementation route

Per the auditor verdict on AP/APSTA parity closure (status file
`AUDITOR_VERDICT_AP_LAYER_PARITY_CLOSURE_20260509T173840_0300.md`), the
route split for local AP/APSTA work is:

- `IMPLEMENT_LOCAL` for the host APSTA owner/lifetime, role-7
  carrier validation and create/delete glue, fail-closed lifecycle
  state, timers, queues, station-table storage, cleanup, and
  publication bridge. This first slice covers only the bounded
  skeleton (carrier validation, fail-closed gate, storage, cleanup);
  the persistent role-7 lifetime and create/delete edges are
  residual scope.
- `REUSE_LINUX_BSD` for the lower donor surfaces — OpenBSD net80211
  HostAP/admission/protocol, Linux iwlwifi AP/GO firmware MAC
  context/beacon/station/key/CSA/queue/event paths.
- `REUSE_REFERENCE_DECOMP` for the observable Apple SAP/IO80211
  contract — slots 505..531, selector carriers and return behaviour,
  AP-up state, station-table message ids and sizes, peer/cache/stat
  publication boundaries, and the WCL_REASSOC separation.

## Stage 1 first slice: bounded skeleton

This first slice lands the host APSTA owner skeleton at
`AirportItlwm/AirportItlwmAPSTAInterface.hpp`. It introduces the
class `AirportItlwmAPSTAInterface` with:

- `bool initWithCarrier(uint32_t role, const uint8_t *mac, const char *bsd_name)` —
  validates the recovered role-7 carrier (role must be `7`, MAC and
  bsd_name must be non-NULL) and initialises driver-private state.
- `static bool isLowerBackendReady()` — the lower-backend gate.
  Returns `false` until the AP/GO firmware MAC context, beacon and
  probe-response template upload, AP station add/remove, AP key
  install, CSA and beacon update, AP firmware event conversion,
  station-event producer bridge, and removal of the
  `IEEE80211_STA_ONLY` build flag are all in place.
- `void clear()` — deterministic reset to inactive state, called at
  init entry and on every teardown edge so the storage invariant
  remains "owner inactive" once the entry handler returns.
- read-only accessors `getActive`, `getRole`, `getMacAddr`,
  `getBsdName`, `getApUpState`.

The skeleton owns:

- a one-byte `fActive` flag,
- a `fRole` value,
- a 6-byte MAC,
- a 16-byte BSD name (matching the recovered carrier limit),
- a station-table buffer sized to
  `kAirportItlwmAPSTAStationTableEntryCount *
  kAirportItlwmAPSTAStationTableEntryStride` (5 × 0x30 bytes), and
- a one-byte AP-up mirror (`fApUpState`) corresponding to private
  state `+0x26c`.

The skeleton is deliberately not an `OSObject` and not an
`IOService`. Promoting it to a registered controller-side instance
requires the lower-backend support that does not exist yet; subsequent
CRs will add the registration glue when those backends are ready.

## Role-7 acquisition wiring

Only the recovered APSTA / SoftAP role is routed through the
skeleton:

- `AirportItlwmSkywalkInterface::setVIRTUAL_IF_CREATE` (Tahoe / V2
  path), `case 7` only — the role-7 / SoftAP carrier.
- `AirportItlwm::setVIRTUAL_IF_CREATE` (V1 path),
  `APPLE80211_VIF_SOFT_AP` only.

The Tahoe V2 `case 6` (proximity / AWDL) and the V1
`APPLE80211_VIF_AWDL` path are kept on the prior recovered public
failure `0xe00002bd` and explicitly do not enter the APSTA owner
skeleton, because that role is the AWDL public-failure path before
the hidden AWDL owner is consulted.

For the role-7 / SoftAP carrier, the handler:

1. Validates the recovered role-7 carrier via `initWithCarrier`. A
   bad carrier produces the recovered raw invalid-argument return
   `kAirportItlwmAPSTARawInvalidArgumentReturn = 0x16`.
2. Evaluates the lower-backend readiness gate.
3. Calls `clear()` on the stack-local owner so the storage invariant
   is restored before returning.
4. Returns the recovered Apple "create-failed" code
   `kAirportItlwmAPSTACreateFailedReturn = 0xe00002bd` while the gate
   is structurally false.

Observable behaviour is unchanged from the prior code: role-7
acquisition still fails closed with `0xe00002bd`, the V2 role-6 /
AWDL path still returns `0xe00002bd`, and the V1
`APPLE80211_VIF_AWDL` path still returns `0xe00002bd`. The role-7
failure now traverses the owner skeleton so subsequent CRs can flip
the gate or replace the stack-local owner with a registered instance
without restating the contract.

## Object lifetime in this slice

The owner is stack-local in this slice. It is constructed,
validated, cleared, and destroyed inside one handler invocation. The
slice does **not** implement:

- a persistent role-7 owner that outlives the entry handler;
- an existing-owner / duplicate-create check;
- an `already-exists` return edge;
- a delete path or a role-7 destroy edge;
- an IOService / IO80211 / SAP registration of the owner.

These edges are explicitly residual scope (see below) and depend on
lower-backend support that does not exist yet.

## Forbidden in this Stage 1

- No role-7 success: the gate is structurally false; the handler
  returns the recovered create-failed code for role 7 unconditionally.
- No change to the V2 role-6 / AWDL public failure (`0xe00002bd`)
  or to the V1 `APPLE80211_VIF_AWDL` public failure (`0xe00002bd`).
- No AP-up transition: `fApUpState` remains `0`; nothing writes to
  the recovered `+0x26c` mirror.
- No beacon emission, no AP firmware MAC-context command sequence,
  no AP key install, no CSA/beacon update, no AP client association,
  no DHCP, no traffic, no peer-cache publication, no station-table
  mutation.
- No removal of `IEEE80211_STA_ONLY`. No alteration of the iwx/iwm
  HOSTAP panic surfaces.
- No merging of AP station lifecycle events into the WCL_REASSOC
  publication path (the WCL_REASSOC owner contract from CR-446
  remains separate and unmodified).

## Residual scope (subsequent CRs)

1. AP/GO firmware HAL on Intel iwx/iwm: AP MAC context, beacon and
   probe-response template upload, AP station add/remove, AP key
   install, CSA and beacon update, AP firmware event conversion.
   Donor: Linux iwlwifi.
2. OpenBSD net80211 HostAP enablement scoped behind an AP-mode
   branch, `IEEE80211_STA_ONLY` retired only inside that branch,
   AID/TIM bitmap resize plumbing.
3. Station-event producer bridge: convert net80211/HAL AP events to
   the recovered APSTA station-table mutations and message
   publications (auth/assoc/reassoc/remove with the recovered IDs and
   sizes).
4. APSTA owner registration: promote the stack-local skeleton to a
   registered controller-side instance with lifetime tied to
   role-7 create/delete and to the controller's stop/free path.
5. AP-up transition wiring: only when the lower-backend gate becomes
   truthful and the producer bridge can update the station table.

## Self-check anchors

- Recovered Apple lifecycle, role-7 success state, station-table
  layout, and SAP slots 505..531 in the auditor-verified offline
  bundle (`commit-approval/offline_results/itlwm_ap_layer_remaining_debt_closure_result_2026_05_09.tar.zst`).
- Auditor verdict closure of Apple reference debt:
  `commit-approval/status/AUDITOR_VERDICT_AP_LAYER_PARITY_CLOSURE_20260509T173840_0300.md`.
- iwx/iwm HOSTAP panic surfaces at `itlwm/hal_iwx/ItlIwx.cpp:8428`
  and `itlwm/hal_iwm/mac80211.cpp:2019`.
- `IEEE80211_STA_ONLY` mask of `IEEE80211_M_HOSTAP` at
  `itl80211/openbsd/net80211/ieee80211_var.h:259-268`.

## Current-head fail-closed owner rework - 2026-06-23

The current-head APSTA owner implementation keeps the recovered owner/lower-backend split but corrects the public role-7 create result. Role 7 (`APPLE80211_VIF_SOFT_AP`) is still the only virtual-interface create carrier that enters the APSTA owner path. Role 6/AWDL remains on the recovered public create-failed path and does not allocate or register the APSTA owner.

The owner source is now named `AirportItlwmAPSTAOwner` rather than carrying review-stage wording in the type and file names. The Xcode Tahoe source graph references `AirportItlwmAPSTAOwner.cpp` directly, so the owner object, state block, station table, SoftAP selector mirror, and net80211 station-event callback binding are compiled with the Tahoe target.

Role-7 create now validates and allocates the owner, calls `startLowerIfReady()`, and treats any non-success lower HAL result as a failed create. On that path it calls `deleteAPSTAOwner()` before returning the HAL failure, so `ieee80211_apsta_event_unregister()` clears `ic_apsta_event_cb` and `ic_apsta_event_arg` before the owner reference is released. A successful role-7 create is therefore possible only after a later AP/GO backend advertises support and `startAPMode()` succeeds.

This rework does not enable AP/GO firmware operation, HostAP net80211 opt-out, beacon emission, AP key install, CSA, AP station association, peer-cache publication, DHCP, or AP traffic. It is fail-closed APSTA owner/lifetime/build-integration groundwork only.


## Role-7 delete/teardown dispatch - 2026-06-23

The current Tahoe/Skywalk selector graph now routes `APPLE80211_IOC_VIRTUAL_IF_DELETE` into the host APSTA owner teardown edge. This is a switch-only handler because `IO80211InfraProtocol` exposes a virtual slot for `setVIRTUAL_IF_CREATE` but not for delete on this local header surface. The handler rejects NULL carriers, requires a controller instance, and then delegates to `AirportItlwm::deleteAPSTAOwnerForBSDName()`.

The delete carrier contains a BSD name but no role field. The controller therefore treats the already allocated `AirportItlwmAPSTAOwner` as the only local role-7 owner and matches a non-empty carrier BSD name against that owner before teardown. On match, `deleteAPSTAOwner()` unregisters the net80211 APSTA event callback and releases the owner, which runs `AirportItlwmAPSTAOwner::free()` -> `teardown()` -> `stopLower()`. On a null, empty, absent, or non-matching owner/name, the path returns unsupported and performs no allocation, start, retry, publication, or AP-up transition.

This completes role-7 owner lifetime symmetry at the structural APSTA layer: create owns allocation and lower-start gating; delete owns matching teardown and unregister-before-release. It still does not enable AP/GO firmware operation, HostAP net80211 opt-out, beacon emission, AP key install, CSA, AP station association, peer-cache publication, DHCP, AP traffic, or any role-7 success claim while the lower AP/GO backend remains unsupported.


## AP/GO HAL method-surface expansion - 2026-06-23

After the role-7 create/delete owner-lifetime slice, the next bounded APSTA surface is the recovered SAP setter group for `setHOST_AP_MODE_HIDDEN`, `setSOFTAP_PARAMS`, `setSOFTAP_TRIGGER_CSA`, and `setSOFTAP_WIFI_NETWORK_INFO_IE`. The slot and byte-offset anchors remain `include/Airport/IO80211SapProtocol.h` slots 526-529 / byte offsets 0x1070-0x1088. The carrier and state anchors remain in `AirportItlwmAPSTAInterface.hpp`: hidden uses state +0x0d and AP-up state +0x26c, SoftAP params use state +0x18/+0x1c/+0x20/+0x24/+0x28 plus beacon interval +0x14 and applied interval +0x68, CSA uses the recovered channel/mode carrier and AP-up state +0x26c, and Wi-Fi network-info IE uses the bounded +0x2c region with the recovered maximum accepted length.

The local implementation exposes this method surface without claiming backend capability. `ItlHalService` default AP/GO hooks for hidden, SoftAP params, and Wi-Fi network-info IE return unsupported. Owner/controller routing only reaches lower HAL methods when the owner AP-up gate is true, which today requires a successful lower `startAPMode()` and therefore remains false on unsupported Intel backends. This preserves the fail-closed contract: no role-7 create success, AP-up state, beacon emission, key install, CSA, station publication, DHCP, or traffic is claimed by this structural slice.


## Scoped net80211 station-event opt-out admission - 2026-06-23

The APSTA owner station-event bridge remains a single-consumer callback tied to role-7 owner lifetime, but default Tahoe builds keep the local `IEEE80211_STA_ONLY` boundary. The admitted exploration surface is now named explicitly as `IEEE80211_APSTA_STATION_EVENT_OPT_OUT`: `scripts/build_tahoe.sh --opt-out` defines that gate together with the legacy `IEEE80211_OPT_OUT_STA_ONLY` spelling, and `ieee80211_var.h` maps the legacy spelling to the named gate before suppressing `IEEE80211_STA_ONLY`.

Controller-side binding follows the same gate. `AirportItlwm::ensureAPSTAOwner()` registers `AirportItlwmAPSTANet80211Event` with `ieee80211_apsta_event_register()` only when `IEEE80211_APSTA_STATION_EVENT_OPT_OUT` is compiled. `AirportItlwm::releaseAll()` and `AirportItlwm::deleteAPSTAOwner()` unregister only under that gate before releasing the owner. Default builds therefore do not bind a dormant APSTA callback while the `ieee80211_node_join()` / `ieee80211_node_leave()` producer call sites are absent. Opt-out builds admit only the station-event bridge surface; AP profile creation, beacon/key/station firmware producers, AP data path, AP runtime success, and role-7 create success remain gated by the lower AP/GO HAL backend and are not claimed by this admission layer.


## APSTA Profile, Key, And Station Producer Integration - 2026-06-23

The recovered APSTA SAP producer group is slots 517-523 in `include/Airport/IO80211SapProtocol.h`: `setSSID`, `setCIPHER_KEY`, `setCHANNEL`, `setHOST_AP_MODE`, `setSTA_AUTHORIZE`, `setSTA_DISASSOCIATE`, and `setSTA_DEAUTH`. This group sits before the already routed hidden/SoftAP params/CSA/network-info/ext-cap/MIS-max-STA selectors and uses the same AP-up ownership boundary.

Local structural routing now mirrors that boundary without enabling AP runtime. `AirportItlwmSkywalkInterface::processApple80211Ioctl` forwards SSID, CHANNEL, and CIPHER_KEY to the APSTA owner only when the role-7 owner exists; otherwise those selectors keep the existing STA behavior. APSTA-only station authorize/disassociate/deauth selectors route to owner methods and fail closed when the owner is absent. The owner stores SSID at the recovered state offsets +0x274/+0x278, caches channel into the lower start configuration, translates `apple80211_key` into `ItlHalApKey`, and translates station commands/events into `ItlHalApStationCommand` only after `isApRunning()` is true.

This does not close the AP/GO capability gap. Current Intel backends still report no AP/GO support, role-7 create deletes the owner after failed lower start, and HAL AP key/station/beacon commands remain default-unsupported until a later backend implementation. The observable contract of this slice is structural reachability and fail-closed producer ownership, not AP/client runtime success.

### STA_DISASSOCIATE / STA_DEAUTH carrier preservation rework - 2026-06-23

The station producer rework keeps the rejected Stage 1 package on the complete recovered carrier contract instead of narrowing to reason-only behavior. The recovered `AirportItlwmAPSTAStaDisassocInputLayout` carries `reason04`, `value08`, and `value0c`; the recovered firmware payload layout carries `reason00`, `value04`, `value08`, and sentinel `0xaaaa` at offset `0x0a`.

Local AP-up-gated station command translation now preserves those leaves at the APSTA owner-to-HAL boundary: `reason04` is copied to the command reason and payload `reason00`, carrier `value08` is copied to payload `value04`, carrier `value0c` is copied to payload `value08`, and payload `sentinel0a` is set to the recovered `0xaaaa` value. The default Intel HAL still returns unsupported, role-7 create remains fail-closed without AP/GO backend support, and this structural layer does not claim firmware AP operation or runtime AP client success.

## Intel iwn AP/GO firmware RXON backend boundary - 2026-06-23

After the APSTA profile/key/station producer routes reached the HAL surface, the next bounded backend layer is the first Intel iwn AP/GO firmware context owner. The local iwn firmware register surface already defines `IWN_MODE_HOSTAP`, `IWN_FILTER_BSS`, `IWN_FILTER_BEACON`, and `IWN_CMD_RXON`; `iwn_config()` already uses the same RXON field family for STA and monitor setup. The Stage 1 backend slice adds `ItlIwn::supportsAPMode()`, `ItlIwn::startAPMode()`, `ItlIwn::stopAPMode()`, and `ItlIwn::iwn_build_ap_rxon()` so the APSTA owner-to-HAL `startAPMode()` contract has a concrete Intel payload boundary.

The helper validates `ItlHalApConfig` channel and BSSID, finds the local net80211 channel, and composes an `IWN_MODE_HOSTAP` RXON payload with AP BSSID/WLAP, multicast/BSS/beacon filters, inherited 2 GHz protection flags, rate masks, and rxchain fields. This is only a firmware-context payload owner. It does not implement beacon template upload, AP key installation, station add/remove, CSA, AP datapath queues, HostAP runtime, client association, DHCP, AP traffic, or iwx/iwm AP support.

The current build deliberately keeps backend admission off. `IWN_APGO_FIRMWARE_BACKEND_OPT_IN` is not defined by the Tahoe build envelopes, so default and opt-out builds keep `ItlIwn::supportsAPMode() == false`, `startAPMode()` returns `kIOReturnUnsupported` before touching firmware state, and role-7 create still fails closed through the existing APSTA owner lower-start gate. `stopAPMode()` is still idempotent on that closed gate and returns success without touching firmware, so `AirportItlwmAPSTAOwner::stopLower()` can always issue the contractual lower-stop call while teardown has a live HAL pointer. A later backend admission slice must explicitly define the opt-in only after the remaining AP/GO firmware prerequisites are implemented and reviewed.

## V1 role-7 owner parity - 2026-07-10

The old V1 `AirportItlwm::setVIRTUAL_IF_CREATE` role-7 branch still used the
first-slice stack-local `AirportItlwmAPSTAInterface::isLowerBackendReady()`
gate after the Tahoe/Skywalk path had moved to the persistent
`AirportItlwmAPSTAOwner` lifetime. That was a stale local path: it validated
and cleared a temporary owner instead of allocating the controller-owned APSTA
owner, attempting `startLowerIfReady()`, and running the unregister/release
teardown edge on lower failure.

The V1 create/delete pair now mirrors the Skywalk APSTA lifetime contract:
role 7 allocates through `ensureAPSTAOwner()`, calls `startLowerIfReady()`,
deletes the owner on any lower failure, and can return success only after a
HAL backend truthfully starts AP/GO mode. V1 delete uses the same BSD-name
carrier matcher, `deleteAPSTAOwnerForBSDName()`, as the Skywalk switch-only
delete path. `AirportItlwmAPSTAInterface.hpp` no longer defines the obsolete
stack-local skeleton class or the always-false readiness gate; it remains the
recovered APSTA layout/ABI witness header consumed by `AirportItlwmAPSTAOwner`.

## APSTA SoftAP Wi-Fi Network-Info Feature Gate - 2026-07-10

The recovered `setSOFTAP_WIFI_NETWORK_INFO_IE(...)` body first reads the core
pointer stored at APSTA state `+0x218` and calls
`AppleBCMWLANCore::featureFlagIsBitSet(core, 0x46)`. That helper bounds the
feature index to a 16-byte bitmap (`0x80` bits) rooted in the core expansion
feature store at `+0x45a8`. Only when bit `0x46` is set does the setter read
input byte `+0x03`, require it to be below `0x21`, and copy exactly `0x24`
bytes into APSTA state `+0x2c`; invalid lengths on the enabled path return
`0xe00002c2`. When bit `0x46` is clear, the setter returns success without
reading the input carrier.

The local owner no longer uses the stale compile-time
`LocalFeatureGate46Enabled == 0` bypass. `AirportItlwm` now owns a 16-byte
APSTA core-feature bitmap and `AirportItlwmAPSTAOwner` routes the network-info
setter through `isAPSTACoreFeatureFlagSet(0x46)`. The bitmap is initialized
clear until a separately recovered producer sets individual bits, so this slice
does not guess feature `0x46`, enable AP/GO runtime, or synthesize SoftAP
vendor-IE programming.

## APSTA Simple Getter And STA-Disassociate Null-Guard Parity - 2026-07-10

The recovered APSTA simple bodies do not install local null guards on every
carrier. `getSOFTAP_PARAMS(...)` directly writes the fixed output fields from
APSTA state `+0x18/+0x1c/+0x20/+0x24/+0x68/+0x10/+0x0e/+0x28` and returns
success. `getSOFTAP_STATS(...)` copies exactly `0x58` bytes from state
`+0x1b0` and returns success. `setSTA_DISASSOCIATE(...)` reads input
`+0x04/+0x08/+0x0c`, builds the 0x0c-byte payload ending in sentinel
`0xaaaa`, and calls virtual IOCTL set selector `0xc9`; no local null guard is
present before those carrier reads.

The local owner/controller wrappers now preserve those direct-read contracts:
they no longer preempt `getSOFTAP_PARAMS`, `getSOFTAP_STATS`, or
`setSTA_DISASSOCIATE` with `nullptr -> kIOReturnBadArgument` before reaching
the APSTA owner. This does not remove the separate reference-proven null
returns for methods such as `getOP_MODE`, `getHOST_AP_MODE_HIDDEN`,
`setSTA_AUTHORIZE`, CSA, or station-list/stat getters.

## APSTA RSN_CONF Consumer And HAL Boundary - 2026-07-10

The recovered APSTA selector 77 body first checks APSTA state byte `+0x29b`
bit `0x10`; when set it returns `0xe00002d5`. The non-rejected path reads the
RSN_CONF carrier directly, with no recovered local null guard. The carrier
fields used by the APSTA consumer are pinned at input `+0x08/+0x0c`
(pairwise version count/list), `+0x2c/+0x30` (pairwise cipher count/list),
`+0x58/+0x5c` (group version count/list), `+0x7c/+0x80` (group cipher
count/list), and `+0xa0` (MFP word). Cipher/version loops clamp to eight
entries. The recovered mask rules are pairwise cipher `1 -> 0x02`, pairwise
cipher `2 -> 0x04`, group cipher `4 -> 0x40`, group cipher `8 -> 0x80`, and
group cipher `0x1000 -> 0x40000`; the recovered BootKC table for version
values `0..8` is `{0, 1, 1, 2, 4, 4, 0, 0, 0x100}`.

The local implementation adds the packed `apple80211_rsn_conf_data` carrier,
routes `APPLE80211_IOC_RSN_CONF` set requests through both V1 and Tahoe/Skywalk
controller surfaces, and centralizes the consumer body in
`AirportItlwmAPSTAOwner::setRsnConf(...)`. The owner preserves the APSTA gate,
direct carrier reads, count clamps, cipher masks, version-table mask, and MFP
publication into `ItlHalApRSNConfig`. The HAL method `setAPRSNConfig(...)`
defaults to `kIOReturnUnsupported`, so current Intel backends remain
fail-closed until a separately recovered AP/GO RSN firmware mapper is
implemented. This slice does not claim AP/GO runtime, beacon/key/station
firmware operation, or a producer/default RSN_CONF closure beyond the recovered
APSTA consumer and local HAL boundary.
