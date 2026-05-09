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
