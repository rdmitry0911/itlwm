# CR-479 WCL owner-aware first-M1 routing (project-owned analysis)

## Status

- static_closure_status: FULL_DECOMP_CLOSED
- coder_readiness: STATIC_CLOSURE_CODER_READY_FOR_LOCAL_LAYER_WORK
- runtime_allowed: NO (until a new Stage 1 decision approves after-fix runtime)
- commit_allowed: NO

## Why this layer note exists

Stage 2 of the prior rev4 candidate built, installed, loaded, and reached
transient ic_state=4 (RUN) on the live Tahoe Skywalk PSK association edge,
but every credential-safe acceptance marker the rev4 Stage 1 decision
required at Stage 2 stayed at zero across the 48055-line redacted log:
`install_external_pmk`, `clear_external_pmk`, `setCUR_PMK`,
`setCIPHER_KEY`, `setCipherKey`, `setCipherKey_pmk_install_count`,
`setCUR_PMK_pmk_install_count`, `ieee80211_recv_4way_msg1_pmk_check`,
`ni_pmk_nonzero_bytes`, `APPLE80211_M_LINK_CHANGED`,
`APPLE80211_CIPHER_PMK`, `APPLE80211_IOC_CUR_PMK`,
`APPLE80211_IOC_CIPHER_KEY`, `external_pmk_eligibility_clear_count`,
`associateSSID`, and `localImportHasKey`. The 4-way handshake on the AP
side looped through `authenticated`/`associated (aid 1)` without an EAPOL
completion. The decisive negative result refutes the rev4 hypothesis that
the local kext is the active PSK PMK owner on the hidden WCL_ASSOCIATE
path and selects a layer-sized owner-aware first-M1 routing fix instead
of another one-selector guess.

## Recovered Apple reference contract for the PSK PMK layer

The auditor-accepted static-closure synthesis result for the WCL_ASSOCIATE
/ WCL-only PMK delivery path records the active-owner contract:

1. `APPLE80211_IOC_ASSOCIATE` / public ASSOCIATE
   - Public carrier delivers a usable PSK PMK byte window in
     `apple80211_assoc_data::ad_key`. The local driver consumes the bytes,
     copies them into `ieee80211com::ic_psk`, sets `IEEE80211_F_PSK`, and
     marks the local PAE as the active owner for the first 4-way M1.

2. `apple80211setWCL_ASSOCIATE` (selector `0x1b3`)
   - Hidden carrier with `externalPmkOwner = true` and `key_len = 0`.
     Apple internally retains the PSK PMK owner state (private key owner
     state at body `+0x18`, current join candidate at body `+0x20`); the
     selector publishes WCL association/admission state only, not PMK
     bytes. The local driver must mark PSK eligibility (`IEEE80211_F_PSK`)
     so RSN-policy and AKM negotiation remain consistent, but it must
     not copy any PMK bytes into `ic_psk` and must not let the local PAE
     consume the first 4-way M1; that consumption belongs to the external
     Apple/user supplicant.

3. Explicit local PMK ingress
   - `APPLE80211_IOC_CIPHER_KEY` cases `APPLE80211_CIPHER_PMK` and
     `APPLE80211_CIPHER_MSK`, `APPLE80211_IOC_CUR_PMK`, and the PMKSA /
     ESS-cache path: when an Apple producer does deliver PMK bytes
     out-of-band, `installExternalPmkLocked(...)` copies them into
     `ic_psk` and clears the external-owner flag, restoring local PAE
     ownership of the first M1.

The PMK byte owner inside Apple is `AppleBCMWLANCore::setKey` /
`saveCUR_PMK` / `programPMK` (PMK store at `core + 0xdf`, length at
`core + 0x120`, metadata at `core + 0x124` / `core + 0x12c`). The local
analogue is `ieee80211com::ic_psk` plus `IEEE80211_F_PSK` for the PSK
AKM eligibility and a new `ic_external_pmk_owner` boolean for the
active-owner classification. The zeroizer is invoked at disassociate,
leave, reassociation start, join abort, PMKSA clear, and non-external
RSN disable edges; the local `clearExternalPmkEligibilityLocked` is wired
to the same edges and now also clears the per-node `ni_pmk` and
`IEEE80211_NODE_PMK` so a fresh `ieee80211_recv_4way_msg1` on a
re-association cannot read stale node PMK material.

Negative carriers proven in the static closure:
- `setCIPHER_KEY(CIPHER_PMK)` as a synthetic pre-association PMK bridge.
- `CUR_PMK` / `getCUR_PMK` as a SET-side external PMK ingestion on the
  active Tahoe Skywalk path.
- `PMK_CACHE` as the active WCL PMK byte carrier.
- Hidden WCL_ASSOCIATE candidate body as a PMK byte source.
- Skywalk slots `[664]..[749]` and adjacent APSTA / IO80211ControllerWiFi
  consumers as active PMK byte owners.

## Local first M1 consumer

`net80211/ieee80211_pae_input.c::ieee80211_recv_4way_msg1` copies
`ic_psk` into `ni_pmk` on PSK AKM, sets `IEEE80211_NODE_PMK`, and derives
the PTK from the node PMK. The recovered Apple/Tahoe owner contract
classifies the first M1 by actual local PMK material first, then by the
external owner state; the local PAE may only consume the M1 when the
local driver actually has PMK bytes:

- Local PAE owns the PMK when `ic_psk` has non-zero bytes: copy into
  `ni_pmk`, set `IEEE80211_NODE_PMK`, derive PTK, send M2.
- External Apple/user supplicant owns the PMK when `ic_psk` is all
  zero and `ic_external_pmk_owner` is set: defer (no PTK derivation,
  no M2 send) so the external supplicant can consume the same M1
  unchanged.
- No active PMK owner when `ic_psk` is all zero and
  `ic_external_pmk_owner` is also zero: the local PAE has no key
  material to consume M1 with and must not derive a zero-PTK M2.
  The handshake stays pending for a future legitimate consumer
  (either a later explicit local PMK ingress or a fresh external
  owner re-assertion).

The credential-safe structural marker
`ieee80211_recv_4way_msg1_owner_route owner=<local|external|none>
ic_psk_nonzero_bytes=N/32 rsnakms=0xMASK
ic_external_pmk_owner=<0|1> deferred_to_external_supplicant=<0|1>
deferred_no_owner=<0|1>` is the first observable consumer of the
local owner classification. The external-owner and no-owner branches
return without mutating `ni_pmk` or `ni->ni_flags`; only the local
branch sends M2.

## Local mapping table (recovered to itlwm)

| Apple / reference owner or field | Current itlwm source location | Local consumer / action |
|---|---|---|
| Public ASSOCIATE / ad_key PMK bytes | `setASSOCIATE` -> `associateSSID(... importLocalPmk=true, externalPmkOwner=false)` | Copies into `ic_psk`, sets `IEEE80211_F_PSK`, clears `ic_external_pmk_owner`; local PAE owns first M1. |
| Hidden WCL_ASSOCIATE private owner state | `setWCL_ASSOCIATE` -> `associateSSID(... importLocalPmk=false, externalPmkOwner=true)` | No PMK byte copy. Sets `IEEE80211_F_PSK` for AKM eligibility and sets `ic_external_pmk_owner`; local PAE defers first M1 to external supplicant. |
| Explicit local PMK ingress (CIPHER_KEY case 6/9, CUR_PMK, PMKSA cache) | `setCIPHER_KEY` case `APPLE80211_CIPHER_PMK` / `APPLE80211_CIPHER_MSK`, `setCUR_PMK`, PMKSA helpers -> `installExternalPmkLocked(...)` | Validates `IEEE80211_PMK_LEN`, copies into `ic_psk`, sets `IEEE80211_F_PSK`, clears `ic_external_pmk_owner`; local PAE owns first M1. |
| PTK / GTK install | local `setCIPHER_KEY` case `APPLE80211_CIPHER_PTK` / `APPLE80211_CIPHER_GTK` | Downstream handshake key install; not the PMK source. |
| Cleanup / failure edges | `setDISASSOCIATE`, `setCLEAR_PMKSA_CACHE`, `setWCL_LEAVE_NETWORK`, `setWCL_REASSOC`, `setWCL_JOIN_ABORT`, `associateSSID` RSN-disable path | `clearExternalPmkEligibilityLocked(reason_tag)` clears `ic_psk`, drops `IEEE80211_F_PSK`, clears `ic_external_pmk_owner`, and clears `ni_pmk` / `IEEE80211_NODE_PMK` on the bound bss node. |
| First M1 consumer | `net80211/ieee80211_pae_input.c::ieee80211_recv_4way_msg1` | Classifies by `ic_psk` non-zero bytes first: owner=local consumes M1 only when local PMK ingress has happened; owner=external defers to the external supplicant when `ic_psk` is zero and `ic_external_pmk_owner` is set; owner=none returns without M2 send when neither has the PMK. Only the local branch copies into `ni_pmk` or sets `IEEE80211_NODE_PMK`. |

## Bounded layer change in this iteration

This iteration adds the owner-aware first-M1 routing layer on top of the
rev4 source/header/docs integration:

1. `itl80211/openbsd/net80211/ieee80211_var.h` introduces
   `u_int8_t ic_external_pmk_owner` immediately after `ic_psk`. The new
   field is non-zero when an Apple external supplicant owns the active
   PSK PMK and zero whenever the local driver becomes the PMK owner.
2. `AirportItlwmSkywalkInterface::associateSSID(...)` PSK branch now
   selects the active owner explicitly:
   - `localImportHasKey` -> local owner (copy into `ic_psk`, set
     `IEEE80211_F_PSK`, clear `ic_external_pmk_owner`).
   - `externalPmkOwner || importLocalPmk` with no usable key -> external
     owner (set `IEEE80211_F_PSK`, set `ic_external_pmk_owner`).
   - Neither -> no owner (clear `ic_external_pmk_owner`).
   Each branch emits a credential-safe `associateSSID_owner SELECTED ...`
   marker naming the chosen owner and source.
3. `installExternalPmkLocked(...)` clears `ic_external_pmk_owner` after a
   successful PMK install, and the `install_external_pmk INSTALLED ...`
   marker reports `ic_external_pmk_owner=0`. Explicit PMK ingress thus
   always restores local PAE ownership.
4. `clearExternalPmkEligibilityLocked(...)` clears `ic_external_pmk_owner`
   alongside `ic_psk` and `IEEE80211_F_PSK`, and additionally clears
   `ni_pmk` and `IEEE80211_NODE_PMK` on `ic_bss` when bound. The
   `clear_external_pmk CLEARED ...` marker reports
   `ic_external_pmk_owner=0` and `ni_pmk_cleared=<0|1>`.
5. `ieee80211_recv_4way_msg1` honours the owner contract on PSK AKM by
   classifying the active owner from `ic_psk` non-zero bytes first and
   the external owner state second:
   - `ic_psk` has non-zero bytes: emit
     `ieee80211_recv_4way_msg1_owner_route owner=local ic_psk_nonzero_bytes=N/32 ...`
     and proceed with the existing PSK PMK consumption path (copy into
     `ni_pmk`, set `IEEE80211_NODE_PMK`, derive PTK, send M2).
   - `ic_psk` is all zero and `ic_external_pmk_owner` is set: emit
     `ieee80211_recv_4way_msg1_owner_route owner=external ... deferred_to_external_supplicant=1`
     and return without mutating `ni_pmk` or `ni->ni_flags`.
   - `ic_psk` is all zero and `ic_external_pmk_owner` is also zero:
     emit `ieee80211_recv_4way_msg1_owner_route owner=none ... deferred_no_owner=1`
     and return without mutating `ni_pmk` or `ni->ni_flags`. This
     prevents zero-PMK PTK derivation and the resulting zero-MIC M2
     send that the authenticator would reject as a PSK mismatch.

No source change is introduced outside this owner-routing layer; the
rev4 routing of `APPLE80211_IOC_CIPHER_KEY` through the Tahoe Skywalk
card-specific bridge, the packed `apple80211_pmk` carrier, the 32-byte
`apple80211_link_changed_event_data` ABI, the single-owner
`APPLE80211_M_LINK_CHANGED` publisher, the duplicate zero-length
link/BSSID publication suppression, the lifecycle clears, the
credential-safe `getCUR_PMK` Apple failure `0xe00002c7`, the slot 664-750
closure, and the existing structural counters are unchanged byte-for-byte.

## Non-claims

- This iteration does not claim 4-way completion, DHCP/IP, RUN steady
  state, sustained `authorized=yes`, AP-mode functionality, broader
  stability, or project completion.
- This iteration does not introduce timing, retry, fallback, forced
  state, masking, suppression, or any speculative "try and see" change.
- This iteration does not request runtime, kext install, reboot, unload,
  or commit before a new Stage 1 decision authorizes after-fix runtime.

## Residual uncertainty

If the runtime evidence after this iteration still shows zero
`install_external_pmk` markers and the M1 owner-route marker reports
`owner=external` and/or `owner=none` only, the next-step research must
shift to whether an out-of-band Apple producer not yet enumerated in
the static closure publishes PMK bytes through a side-channel not
covered by CIPHER_KEY, CUR_PMK, or PMKSA, and to whether a producer
edge for `ic_external_pmk_owner` (a fresh WCL_ASSOCIATE re-assertion,
an alternative selector that sets the external owner state) is missing
or untracked. The deferred-to-external-supplicant behaviour and the
deferred-no-owner behaviour are deliberate: they preserve the external
supplicant's right to consume M1 and prevent the local PAE from
sending a zero-PMK M2 that the authenticator would reject. The
absence of local 4-way completion in either case is by design and not
a regression of the local handshake.

## Provenance

The recovered carrier identities, owner model, payload offsets,
lifecycle edges, and route recommendation come from the auditor-accepted
static-closure synthesis result `cr479-wcl-assoc-pmk-executable-material-remaining-layer-decomp-20260516T2353`
that combined the BootKC p-code / disassembly / xref material with the
CoreWiFi / airportd static material, the Skywalk slot `[664]..[750]`
closure, and the WCL_ASSOCIATE selector inventory. The conclusion is
`IMPLEMENT_LOCAL` for the bounded local layer above. No new
decompilation / reference batch is requested by this iteration.
