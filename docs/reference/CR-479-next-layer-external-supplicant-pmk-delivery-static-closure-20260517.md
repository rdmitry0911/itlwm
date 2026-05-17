# CR-479 next-layer external-supplicant / PMK-delivery / DHCP / AP-mode adjacent layer (project-owned closure)

## Status

- static_closure_status: FULL_DECOMP_CLOSED
- local_implementation_status: COMPLETE_IN_HEAD (no code change required by
  this iteration; documentation captures the closure anchors)
- coder_readiness: LAYER_CLOSED_PENDING_STAGE_1_DOC_WRAPUP
- runtime_allowed: NO (until a new Stage 1 decision approves after-fix runtime)
- commit_allowed: NO

## Why this layer note exists

The prior CR-479 WCL owner-aware first-M1 routing layer closed at commit
`d43f4d9d238c80c019ec2c3f31e30fa633c9ebfb` after auditor-accepted Stage 2
rev3 evidence (decision SHA-256
`238e27697c7257204657b7f8f998c7b3c91fd583d1c4bc0b52989f05b25366fc`). The
Stage 2 rev3 runtime evidence recorded 27 `owner=external
deferred_to_external_supplicant=1` events with zero `owner=local` and zero
`ieee80211_send_4way_msg2` events, but the external Apple supplicant did
not publish M2 on the live Tahoe Skywalk PSK association observed in the
project lab, leaving the 4-way handshake pending and DHCP/control-network
completion unreached. The Stage 2 rev3 decision explicitly left four
adjacent open roots: the external supplicant M2 publication path, an
explicit PMK side-channel ingress inventory, DHCP/control-network
completion, and the AP-mode role-7 lower-backend bring-up.

The auditor-accepted next-layer web-AI decomp task
`webai_20260517T143532_0300_fef57bd3` (result SHA-256
`f65de7c28ff3104dc073ce79a6c1317344d101622170162c955c9baabe895d0d`) closed
those four roots as one atomic layer under
`DECOMP_REFERENCE_CLOSURE_STATUS: FULL_DECOMP_CLOSED`,
`REMAINING_DECOMP_TARGETS: NONE`, and
`AUDITOR_ROUTE_RECOMMENDATION: REUSE_LINUX_BSD`. The next bounded coder
work is therefore not more decomp / material acquisition; it is the
local-layer integration audit and documentation closure captured by this
note.

## Recovered Apple reference contract for the adjacent layer

The accepted closure ledger records the following invariants for the
external supplicant / PMK-delivery / DHCP / AP-mode adjacent layer on the
live Tahoe Skywalk path.

### 1. Tahoe external supplicant EAPOL TX publisher (closure ledger Target A)

- Userland publisher: `CoreWLAN CWEAPOLClient_startSystemModeEAPOLForSSID`,
  `_startEAPOLWithClientItemID_authenticationInfo`, and `_stopEAPOL_`
  wrap `EAPOLControlStart` / `EAPOLControlStop` /
  `EAPOLControlCopyStateAndStatus`. The supplicant lifecycle is
  start/stop/state/status copy plus callback publication on the userland
  side; the kext does not own the supplicant.
- User-kernel callback path: `IO80211_user __eventRead` dispatches event
  payloads; `__dispatchSupplicant` validates the 0x28-byte payload and
  publishes a callback event id `0x4c`. The supplicant event carrier is
  status/publication only; it never transports raw PMK or M2 bytes.
- PMK source for M2 derivation: the userland supplicant draws PMK material
  from its own state (CoreWLAN keychain / EAPOLControl client context),
  not from a kernel selector. The kernel side accepts PMK bytes only via
  the explicit ingress carriers in Target B.
- Negative paths: null SSID / null client item, failed start / stop,
  malformed payload size, and missing callback state are explicit
  CoreWLAN failures, not silent successes.

### 2. IO80211 / CoreWiFi / Skywalk PMK delivery and ingress contracts (Target B)

- Valid PMK ingress carriers proven by the BootKC decomp closure:
  - `APPLE80211_IOC_CIPHER_KEY = 3` with `key_cipher_type = APPLE80211_CIPHER_PMK = 6`
    or `APPLE80211_CIPHER_MSK = 9`, payload `apple80211_key`, 32-byte PMK / MSK.
  - `APPLE80211_IOC_CUR_PMK = 360`, selector `0x168` SET / `0x16a` GET,
    payload `apple80211_pmk *` (0x5c bytes, `key_len` at `+0x04`, setter
    bytes at `+0x10`, getter window at `+0x08`, status at `+0x48`).
- Cleanup-only carrier: `CLEAR_PMKSA_CACHE` (no PMK bytes; cleanup edge).
- Negative carriers proven by NEGATIVE_CARRIER_PROOF:
  - `apple80211getPMK_CACHE` / `apple80211setPMK_CACHE`: body size 1 /
    immediate return. PMK_CACHE is not a PMK byte carrier.
  - `RSN_IE`, `RSN_XE`, `OFFLOAD_RSN`, AWDL / NAN key surfaces,
    `DHCP_RENEWAL_DATA`, `IPV4_PARAMS`, `IPV6_PARAMS`, `SoftAP`, and APSTA
    role surfaces: not PMK ingress.
  - No separate Skywalk channel, registry property, PMKSA hash / lookup /
    delete user method, or CoreWiFi private bypass supersedes the
    `CIPHER_KEY(PMK/MSK)` or `CUR_PMK` carriers.

### 3. Local AirportItlwm / net80211 external-supplicant parity (Target C)

The closure ledger classifies the local parity as CLOSED: the recovered
Apple contract maps to local carrier / lifecycle glue plus BSD / net80211
PAE reuse, not to a private CoreWLAN / EAPOLControl clone. Local code
already exposes the surfaces the external supplicant requires; the
implementation route is `REUSE_LINUX_BSD` for the M1 -> M2 / PTK path and
`IMPLEMENT_LOCAL` for the PMK ingress / owner-routing / cleanup glue that
the bounded layer already commits.

### 4. Adjacent state machines and object lifecycles (Target D)

- Userland supplicant state: CWEAPOLClient start / stop / state-status
  copy / callback publication / CF object release on stop / dealloc.
- IO80211 / Apple80211 user-kernel state: `__eventRead` /
  `__dispatchSupplicant` validate 0x28-byte payload, publish callback
  event `0x4c`, release CF objects.
- PMK / PMKSA lifetime: `CIPHER_KEY(PMK/MSK)` and `CUR_PMK` are ingress;
  `CLEAR_PMKSA_CACHE` plus disassociate / leave / reassociate / join
  abort / RSN disable are cleanup edges.
- DHCPv4 / DHCPv6 completion: `setDHCP_RENEWAL_DATA`, `setIPV4_PARAMS`,
  `setIPV6_PARAMS` are deferred / control-network state carriers (not PMK
  ingress).
- SoftAP / APSTA role-7: role-7 HostAP / APSTA owner state is recovered,
  but the lower Intel AP / GO HAL backend is local implementation, not
  recovered Apple decomp.

## Local implementation anchors at HEAD `340a844b5effd8a42ad570591f23244ba0ce8ed3`

A per-target source audit at the current HEAD shows the bounded layer is
already implemented from prior committed work. Each anchor below names the
file:line that satisfies the corresponding implementation target without
introducing PMK_CACHE as a synthetic carrier, without raw PMK logging,
without unproven PMKSA side channels, without a private
CoreWLAN / EAPOLControl clone, without fake SoftAP / AP success, and
without retry / poll / replay masking.

### Target 1: PMK ingress convergence (CIPHER_KEY + CUR_PMK -> installExternalPmkLocked)

- `AirportItlwm/AirportItlwmSkywalkInterface.cpp:2811` (`setCIPHER_KEY`)
  routes `APPLE80211_CIPHER_PMK` (case 6) and `APPLE80211_CIPHER_MSK`
  (case 9) through `installExternalPmkLocked(...)` with `source_tag` set
  to `CIPHER_KEY` and the validated 32-byte PMK / MSK payload.
- `AirportItlwm/AirportItlwmSkywalkInterface.cpp:3212` (`setCUR_PMK`)
  rejects `pmk == NULL`, then routes the `apple80211_pmk` payload through
  `installExternalPmkLocked(...)` with `source_tag = "CUR_PMK"`.
- `AirportItlwm/AirportItlwmSkywalkInterface.cpp:3239`
  (`installExternalPmkLocked`) validates `key_len == IEEE80211_PMK_LEN`,
  rejects `NULL` PMK bytes, copies the validated PMK into
  `ic->ic_psk`, sets `IEEE80211_F_PSK`, calls
  `ieee80211_ioctl_setwpaparms(...)` to refresh WPA / RSN PSK auth state,
  clears `ic_external_pmk_owner` (local PAE now owns first M1), and emits
  a credential-safe structural marker (`install_external_pmk INSTALLED
  source=... key_len=... ic_psk_nonzero_bytes=N/32 ic_flags=0x... ...`)
  carrying no raw key bytes.
- No `PMK_CACHE` / `setPMK_CACHE` / `getPMK_CACHE` code path exists in
  the local source tree (`grep -rn "PMK_CACHE\|pmk_cache" AirportItlwm/
  itl80211/` returns zero matches), consistent with the closure
  NEGATIVE_CARRIER_PROOF.

### Target 2: First-M1 external-owner deferral preserved in BSD / net80211 PAE

- `itl80211/openbsd/net80211/ieee80211_pae_input.c:279-325`
  (`ieee80211_recv_4way_msg1`) classifies the PSK AKM consumer by
  `ic_psk` non-zero bytes first and `ic_external_pmk_owner` second, with
  three mutually exclusive branches:
  - `ic_psk_nonzero > 0` -> `owner=local`, copies `ic_psk` into
    `ni_pmk`, sets `IEEE80211_NODE_PMK`, derives the PTK, and sends M2
    (the existing BSD PAE behaviour).
  - `ic_psk_nonzero == 0 && ic_external_pmk_owner != 0` ->
    `owner=external deferred_to_external_supplicant=1`, returns without
    mutating `ni_pmk` or `ni->ni_flags`; the external Apple supplicant
    owns the M1.
  - `ic_psk_nonzero == 0 && ic_external_pmk_owner == 0` ->
    `owner=none deferred_no_owner=1`, returns without mutating
    `ni_pmk` or sending M2; prevents zero-PMK PTK derivation and the
    zero-MIC M2 the authenticator would reject.
- This three-way classification was validated by the prior CR-479 owner-
  routing Stage 2 rev3 runtime evidence (27 deferred-external events, 0
  refuting `owner=local ic_psk_nonzero_bytes=0/32` events, 0
  `ieee80211_send_4way_msg2` events) and remains unchanged at HEAD
  `340a844b...`.

### Target 3: Cleanup coupling across six edges and five state fields

- `AirportItlwm/AirportItlwmSkywalkInterface.cpp:3313`
  (`clearExternalPmkEligibilityLocked`) zeroes the full cleanup payload
  every time it runs:
  - `ic->ic_psk` zeroed (`memset(..., 0, sizeof(ic->ic_psk))`).
  - `IEEE80211_F_PSK` cleared from `ic->ic_flags`.
  - `ic->ic_external_pmk_owner` cleared to 0.
  - `ic_bss->ni_pmk` zeroed when `ic_bss != NULL`
    (`memset(ic_bss->ni_pmk, 0, sizeof(ic_bss->ni_pmk))`).
  - `IEEE80211_NODE_PMK` cleared from `ic_bss->ni_flags` when bound.
  - Credential-safe `clear_external_pmk CLEARED reason=...
    ic_psk_nonzero_bytes=0/32 ic_flags=0x... ic_external_pmk_owner=0
    ni_pmk_cleared=...` structural marker emitted; no raw key bytes.
- Invocation sites (six edges, all covered):
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp:988`
    (`associateSSID` RSN-disable path: reason
    `associateSSID_disable_rsn`).
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp:4593`
    (`setDISASSOCIATE`: reason `setDISASSOCIATE`).
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp:4703`
    (`setCLEAR_PMKSA_CACHE`: reason `setCLEAR_PMKSA_CACHE`).
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp:5257`
    (`setWCL_LEAVE_NETWORK`: reason `setWCL_LEAVE_NETWORK`).
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp:6061`
    (`setWCL_REASSOC`: reason `setWCL_REASSOC`).
  - `AirportItlwm/AirportItlwmSkywalkInterface.cpp:6339`
    (`setWCL_JOIN_ABORT`: reason `setWCL_JOIN_ABORT`).
- All six edges are the cleanup edges named by the next-layer closure:
  `CLEAR_PMKSA_CACHE`, leave, disassociate, reassociate, join abort, RSN
  disable. The five state fields (`ic_psk`, `IEEE80211_F_PSK`,
  `ic_external_pmk_owner`, `ni_pmk`, `IEEE80211_NODE_PMK`) are all
  zeroed at every cleanup edge by the shared helper, so a fresh
  association / reassociation / re-attach cannot read stale PMK byte
  material.

### Target 4: DHCPv4 / DHCPv6 deferred / control-network state (no PMK ingress)

- `AirportItlwm/AirportItlwmSkywalkInterface.cpp:4891`
  (`setDHCP_RENEWAL_DATA`) rejects `data == NULL`, caches the first byte
  of the payload as a persistent `cachedDhcpRenewalData` bool (mirroring
  the Apple producer that retains this state across keepalive / PM work),
  and emits the credential-safe structural marker
  `WCL [612] setDHCP_RENEWAL_DATA enabled=N`. No PMK byte path.
- `AirportItlwm/AirportItlwmSkywalkInterface.cpp:4934` (`setIPV4_PARAMS`)
  rejects `data == NULL`, caches `cachedIPv4Address`,
  `cachedIPv4Netmask`, `cachedIPv4Gateway`, and `cachedIPv4GatewayTail`,
  and emits a structural marker reporting the cached values plus a
  keepalive readiness boolean derived from `addr != 0 && mask != 0`. No
  PMK byte path.
- `AirportItlwm/AirportItlwmSkywalkInterface.cpp:4960` (`setIPV6_PARAMS`)
  rejects `data == NULL` and caches the IPv6 header per the Apple
  producer contract. No PMK byte path.
- These three setters are control-network completion carriers in the
  recovered Apple contract; the local implementation preserves the same
  cached-state semantics rather than collapsing into blind success or
  treating any of them as PMK ingress. The closure ledger
  `NEGATIVE_CARRIER_PROOF` confirms DHCP / IP setters are not PMK
  ingress.

### Target 5: APSTA role-7 owner / lower-backend fail-closed gate

- `AirportItlwm/AirportItlwmSkywalkInterface.cpp:5321`
  (`setVIRTUAL_IF_CREATE`) preserves role-dependent public failure codes
  before any APSTA owner path: NAN / NAN-data roles 8-10 return
  `0xe00002c7`, role 6 (proximity / AWDL) returns the recovered
  create-failed code `0xe00002bd` because the hidden AWDL owner is not
  present on this driver, and only role 7 (`APPLE80211_VIF_SOFT_AP`) is
  routed into the APSTA owner skeleton.
- `AirportItlwm/AirportItlwmV2.cpp:6149` (`ensureAPSTAOwner`) is the
  factory entry point for the host APSTA owner. It returns the existing
  controller-stored `fAPSTAOwner` if a prior create call already
  allocated one, or allocates and initializes a new owner from the
  carrier descriptor when none exists. The owner lifetime spans role-7
  create through driver release; cleanup is performed in
  `AirportItlwm::releaseAll()` before `fHalService` is released.
- `AirportItlwm/AirportItlwmV2.cpp:6206` (`deleteAPSTAOwner`) is the
  explicit teardown entry point. The Tahoe Skywalk dispatch surface does
  not expose a per-role-7 delete entry point, so the handler in
  `setVIRTUAL_IF_CREATE` does not call `deleteAPSTAOwner` directly; the
  release path is bound to controller release.
- The lower iwx / iwm HAL backend is currently fail-closed: the HAL does
  not advertise AP / GO firmware support, so
  `AirportItlwmAPSTAStage1Owner::isApRunning` stays false after create.
  The recovered Apple contract treats that case as "owner present,
  AP-up false", and the host APSTA owner reports create success without
  asserting AP-up. `setMIS_MAX_STA`,
  `setSOFTAP_EXTENDED_CAPABILITIES_IE`, beacon / key / CSA, and
  station-event publication remain structurally inert until a HAL
  backend advertises AP / GO and `startLowerIfReady` succeeds.
- No fake SoftAP / AP success is claimed by the local implementation.
  The role-7 owner is structurally present so the producer / consumer
  contract is preserved, but no AP functional behaviour is exported
  before the Intel AP / GO HAL, station-event producer, beacon / key /
  CSA backend, and non-STA-only net80211 path exist. This is the
  layer's explicit residual implementation-only AP backend gap; it is
  not a decomp gap and does not require additional reference recovery.

## Local mapping table (recovered to itlwm)

| Apple / reference owner or field                                                 | Current itlwm source location                                                                     | First / primary consumer                                                                  | Reset / clear / copy points                                                                                                                                  |
|---|---|---|---|
| CIPHER_KEY(PMK) / CIPHER_KEY(MSK) (32-byte payload)                              | `AirportItlwm/AirportItlwmSkywalkInterface.cpp:2811` (`setCIPHER_KEY`) -> `installExternalPmkLocked` | local PAE first M1 (`ieee80211_recv_4way_msg1` `owner=local` branch)                       | `ic_psk`, `IEEE80211_F_PSK` set; cleared by `clearExternalPmkEligibilityLocked` at the six cleanup edges.                                                    |
| CUR_PMK selector `0x168` SET (apple80211_pmk * 0x5c)                             | `AirportItlwm/AirportItlwmSkywalkInterface.cpp:3212` (`setCUR_PMK`) -> `installExternalPmkLocked`   | local PAE first M1 (`owner=local`)                                                         | Same `ic_psk` + `IEEE80211_F_PSK` lifecycle; cleared by `clearExternalPmkEligibilityLocked`.                                                                  |
| CLEAR_PMKSA_CACHE                                                                | `AirportItlwm/AirportItlwmSkywalkInterface.cpp:4694` (`setCLEAR_PMKSA_CACHE`)                       | cleanup edge                                                                              | Calls `clearExternalPmkEligibilityLocked("setCLEAR_PMKSA_CACHE")` zeroing `ic_psk`, `IEEE80211_F_PSK`, `ic_external_pmk_owner`, `ni_pmk`, `IEEE80211_NODE_PMK`. |
| EAPOL TX publisher (CoreWLAN CWEAPOLClient + IO80211 __dispatchSupplicant)       | userland (not local kext)                                                                         | Apple userspace owns M2 publication when `owner=external`                                  | Local kext defers via `ieee80211_recv_4way_msg1 owner=external deferred_to_external_supplicant=1`.                                                             |
| BSD / net80211 local-owner M2 / PTK behaviour                                    | `itl80211/openbsd/net80211/ieee80211_pae_input.c:279-325`                                          | local first M1 owner classification                                                       | `owner=local` consumes M1; `owner=external` / `owner=none` defer without `ni_pmk` / `IEEE80211_NODE_PMK` mutation.                                            |
| DHCP_RENEWAL_DATA                                                                | `AirportItlwm/AirportItlwmSkywalkInterface.cpp:4891` (`setDHCP_RENEWAL_DATA`)                       | control-network completion (deferred / cached, not PMK ingress)                            | `cachedDhcpRenewalData` reset on driver release.                                                                                                              |
| IPV4_PARAMS                                                                      | `AirportItlwm/AirportItlwmSkywalkInterface.cpp:4934` (`setIPV4_PARAMS`)                             | control-network completion (deferred / cached, not PMK ingress)                            | `cachedIPv4Address`, `cachedIPv4Netmask`, `cachedIPv4Gateway`, `cachedIPv4GatewayTail` reset on driver release.                                               |
| IPV6_PARAMS                                                                      | `AirportItlwm/AirportItlwmSkywalkInterface.cpp:4960` (`setIPV6_PARAMS`)                             | control-network completion (deferred / cached, not PMK ingress)                            | IPv6 header cache reset on driver release.                                                                                                                    |
| VIRTUAL_IF_CREATE role 7 (APPLE80211_VIF_SOFT_AP)                                | `AirportItlwm/AirportItlwmSkywalkInterface.cpp:5321` (`setVIRTUAL_IF_CREATE`) -> `ensureAPSTAOwner` | host APSTA owner skeleton; lower HAL backend fail-closed                                  | Owner lifetime tied to controller; cleanup in `AirportItlwm::releaseAll()` before `fHalService` release.                                                       |
| AppleBCMWLANCore PMK byte store (`core + 0xdf`, len `+ 0x120`, metadata cookies) | local analogue `ieee80211com::ic_psk` + `IEEE80211_F_PSK` + `ic_external_pmk_owner`               | local PAE first M1 owner classification                                                   | Cleared by `clearExternalPmkEligibilityLocked` at the six cleanup edges.                                                                                       |

## Implementation route decision

`IMPLEMENTATION_ROUTE_DECISION: REUSE_LINUX_BSD` (per the accepted closure
result).

- The host supplicant M1 -> M2 / PTK path reuses the existing OpenBSD /
  net80211 PAE implementation in `itl80211/openbsd/net80211/`. No private
  CoreWLAN / EAPOLControl clone, no synthetic PMK byte side-channel, and
  no fake SoftAP / AP success path is introduced.
- The local kext glue (PMK ingress validation, owner classification,
  cleanup coupling, DHCP / IP control-network caching, APSTA role-7
  factory) is already in HEAD and committed under the prior CR-479
  layers. No code change is required by this layer wrap-up.
- The bounded next coder work after this iteration, if and when a new
  decomp surface is discovered or the AP backend gap is closed, is to
  file a new Stage 1 request for that specific delta and not to widen
  the present claim scope.

## Residual implementation-only AP backend gap

The Tahoe Skywalk APSTA role-7 owner skeleton is present and reports
create success, but the lower Intel iwx / iwm HAL backend does not
advertise AP / GO firmware support, so AP functional behaviour
(`isApRunning`, beacon / key / CSA TX, station event publication) stays
fail-closed. This is the only residual gap left by this layer; it is an
implementation gap rather than a decomp / reference gap, and closing it
requires a separate engineering effort (Intel AP / GO HAL, station-event
producer, beacon / key / CSA backend, non-STA-only net80211 path) that is
not in scope of CR-479. The host APSTA owner stays fail-closed so no AP
functional success is claimed without that backend.

## Non-claims

- This iteration does not claim 4-way completion, DHCP / IP success,
  CONTROL_STA_NETWORK success, sustained `authorized=yes`, sustained RUN
  steady state, AP-mode functionality, broader stability, or project
  completion.
- This iteration does not introduce timing, retry, fallback, forced
  state, masking, suppression, or any speculative "try and see" change.
  The five implementation targets are satisfied by the existing committed
  layer at HEAD; no behaviour change is invented.
- This iteration does not add `PMK_CACHE` as a synthetic carrier, raw
  PMK / PTK / MIC / EAPOL key logging, an unproven PMKSA side channel, a
  private CoreWLAN / EAPOLControl clone, a fake SoftAP / AP success path,
  or a retry / poll / replay masking shim.
- This iteration does not request runtime, kext install, reboot, unload,
  commit, an additional decomp / material batch, or another provider /
  web-AI task. The accepted FULL_DECOMP_CLOSED result is sufficient.

## Verification basis

- Decomp result: web-AI task `webai_20260517T143532_0300_fef57bd3`,
  result SHA-256 `f65de7c28ff3104dc073ce79a6c1317344d101622170162c955c9baabe895d0d`,
  reporting `DECOMP_EXECUTION_STATUS: COMPLETE`,
  `DECOMP_REFERENCE_CLOSURE_STATUS: FULL_DECOMP_CLOSED`,
  `REMAINING_DECOMP_TARGETS: NONE`,
  `REMAINING_DECOMP_TARGETS_REQUIRE_ADDITIONAL_DATA: NO`,
  `AUDITOR_ROUTE_RECOMMENDATION: REUSE_LINUX_BSD`,
  `IMPLEMENTATION_ROUTE_DECISION: CODER_LAYER_WORK`.
- Per-target local source audit at HEAD
  `340a844b5effd8a42ad570591f23244ba0ce8ed3`, with code anchors recorded
  in this document.
- Negative carrier check: `grep` over `AirportItlwm/` and `itl80211/`
  returns zero matches for `PMK_CACHE` / `pmk_cache` / `setPMK_CACHE` /
  `getPMK_CACHE`, consistent with the closure NEGATIVE_CARRIER_PROOF.
- Build evidence: `./scripts/build_tahoe.sh` (default STA-only build with
  Tahoe `BootKernelExtensions.kc` symbol check) recorded as part of the
  Stage 1 request that ships this document.

## Provenance

The recovered carrier identities, owner model, payload offsets, lifecycle
edges, negative carrier proofs, side-channel inventory, state-and-object
lifecycle ledger, local mapping table, and route recommendation come from
the auditor-accepted next-layer web-AI decomp synthesis result
`webai_20260517T143532_0300_fef57bd3` (SHA-256
`f65de7c28ff3104dc073ce79a6c1317344d101622170162c955c9baabe895d0d`), built
from the cleanbase Project Sources supplement
`itlwm-project-sources-cleanbase-20260516T194842Z`, the reference-decomp
supplement `itlwm-reference-decomp-supplement-20260516T202209Z`, the
prior executable-material supplement
`itlwm-cr479-wcl-assoc-pmk-executable-material-20260516T204847Z`, and the
new missing-data supplement
`itlwm-cr479-next-layer-external-supplicant-pmk-delivery-material-20260517T0330Z`
(canonical inner archive SHA-256
`4d9b5cc905d6aa7486d940c5657489ca2d35f170923b1256d1f344f6b8355d57`). The
closure conclusion is `REUSE_LINUX_BSD` / `CODER_LAYER_WORK` with the
bounded local layer already complete in HEAD. No new decompilation /
reference batch is requested by this iteration.
