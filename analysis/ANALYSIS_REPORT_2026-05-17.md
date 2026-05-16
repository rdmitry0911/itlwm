# 2026-05-17 Stage 1 design: Tahoe WCL owner-aware first-M1 routing layer

correlation_id: CR-479-stage1-tahoe-wcl-owner-aware-first-m1-routing-20260517
anomaly_id: STA_TAHOE_SKYWALK_WPA2_PSK_4WAY_INCOMPLETE_NO_PMK_DELIVERY
change_class: REFERENCE_ALIGNMENT_FIX
implementation_route_decision: IMPLEMENT_LOCAL
reference_decomp_first_for_capability_gap: YES

## Why this layer exists

The CR-479 active-PSK-PMK-layer rev4 Stage 2 (rev3 submission) was
rejected for commit on a decisive negative runtime: the rev4 candidate
built, installed, loaded, and reached transient ic_state=4 (RUN) on the
live Tahoe Skywalk PSK association edge, but every credential-safe
acceptance marker stayed at zero across a 48055-line redacted log. The
auditor decision recorded `RUNTIME_EVIDENCE_REFUTES_STAGE1_CLAIM_REFERENCE_COVERAGE_GAP`
and routed the next work as RESEARCH_FIRST on the WCL_ASSOCIATE /
WCL-only PMK delivery path.

The follow-up auditor-accepted static-closure synthesis result for the
WCL_ASSOCIATE / WCL-only PMK delivery path (provider task
`cr479-wcl-assoc-pmk-executable-material-remaining-layer-decomp-20260516T2353`,
result archive SHA-256 `012dad0ca5e7230c4abd2621da38e3d403337739cc35d5ca1eb039586d40741c`,
`DECOMP_REFERENCE_CLOSURE_STATUS: FULL_DECOMP_CLOSED`,
`REMAINING_DECOMP_TARGETS: NONE`) records the corrected owner model:

- The hidden WCL_ASSOCIATE selector `0x1b3` carries WCL candidate /
  private owner state, not PMK bytes.
- The public ASSOCIATE / ad_key path is the normal local PSK PMK byte
  carrier.
- `setCIPHER_KEY(CIPHER_PMK)`, `CUR_PMK`, `PMK_CACHE`, and the hidden
  WCL candidate body are negative carriers on the active Tahoe path.
- The first 4-way M1 belongs to the active PMK owner: local PAE only
  when local PMK ingress has happened, external Apple/user supplicant
  otherwise.

`AUDITOR_ROUTE_RECOMMENDATION: IMPLEMENT_LOCAL`
`REMAINING_DECOMP_TARGETS: NONE`
`coder_authorization_scope: IMPLEMENTATION_ONLY_STAGE1_REQUEST_REQUIRED`

This Stage 1 implements the recovered local WCL owner-aware first-M1
routing boundary inside the existing rev4 layer; no new decomp/material
batch is requested.

## What is new in this Stage 1 relative to the prior rev4 layer

This Stage 1 adds owner-aware first-M1 routing on top of the rev4
source/header/docs integration:

1. `itl80211/openbsd/net80211/ieee80211_var.h` introduces
   `u_int8_t ic_external_pmk_owner` immediately after `ic_psk`. The new
   field is non-zero when an Apple external supplicant owns the active
   PSK PMK and zero whenever the local driver becomes the PMK owner.
2. `AirportItlwmSkywalkInterface::associateSSID(...)` PSK branch now
   selects the active owner explicitly and emits a structural
   `associateSSID_owner SELECTED owner=<local|external|none>` marker.
3. `installExternalPmkLocked(...)` clears `ic_external_pmk_owner` after
   a successful explicit PMK install and the `install_external_pmk
   INSTALLED ...` marker reports `ic_external_pmk_owner=0`.
4. `clearExternalPmkEligibilityLocked(...)` clears
   `ic_external_pmk_owner` alongside `ic_psk` / `IEEE80211_F_PSK`, and
   additionally clears the per-node `ni_pmk` and `IEEE80211_NODE_PMK`
   on `ic_bss` when bound. The `clear_external_pmk CLEARED ...` marker
   reports `ic_external_pmk_owner=0` and `ni_pmk_cleared=<0|1>`.
5. `net80211/ieee80211_pae_input.c::ieee80211_recv_4way_msg1` honours
   the owner contract on PSK AKM by classifying the active owner from
   `ic_psk` non-zero bytes first and the external owner state second:
   - `ic_psk` has non-zero bytes: emit
     `ieee80211_recv_4way_msg1_owner_route owner=local
     ic_psk_nonzero_bytes=N/32 ...` and proceed with the existing PSK
     PMK consumption path (copy `ic_psk` into `ni_pmk`, set
     `IEEE80211_NODE_PMK`, derive PTK, send M2).
   - `ic_psk` is all zero and `ic_external_pmk_owner` is set: emit
     `owner=external ... deferred_to_external_supplicant=1` and
     return without mutating `ni_pmk` or `ni->ni_flags`.
   - `ic_psk` is all zero and `ic_external_pmk_owner` is also zero:
     emit `owner=none ... deferred_no_owner=1` and return without
     mutating `ni_pmk` or `ni->ni_flags`. This prevents the local
     PAE from deriving a zero-PTK and sending a zero-MIC M2 that
     the authenticator would reject as a PSK mismatch.

The rev4 routing of `APPLE80211_IOC_CIPHER_KEY` through the Tahoe
Skywalk card-specific bridge, the packed `apple80211_pmk` carrier ABI,
the 32-byte `apple80211_link_changed_event_data` ABI, the single-owner
`APPLE80211_M_LINK_CHANGED` publisher on the parent-success edge, the
duplicate zero-length link/BSSID publication suppression on Tahoe, the
lifecycle clears at the recovered Apple owner reset edges, the credential
-safe `getCUR_PMK` Apple failure `0xe00002c7`, and the slot 664-750
closure are unchanged byte-for-byte.

## Failed same-root hypotheses to date

1. `setCIPHER_KEY(CIPHER_PMK)` host-supplicant bridge (rev2..rev9
   ingestion variants): synthetic pre-association PMK bridge; refuted by
   rev4 setCipherKey rev4 Stage 2 negative.
2. `APPLE80211_IOC_CUR_PMK` / `setCUR_PMK` SET-side external PMK ingest
   plus packed carrier and slot `[750]` override (rev10..rev12): the
   wired-up code is reachable but Tahoe airportd never invokes
   `APPLE80211_IOC_CUR_PMK` on the active Skywalk path.
3. `APPLE80211_IOC_CIPHER_KEY` Skywalk bridge routing plus
   `associateSSID` `ic_psk` preservation on `externalPmkOwner` /
   `key_len = 0` carrier (rev1..rev4 of the active-psk-pmk-layer):
   reachable but unused by live airportd.

One-step-back reframing: the failed unit was always "find the SET-side
selector that delivers PSK PMK bytes to the local kext". The static
closure proves no such SET-side selector exists for the WCL_ASSOCIATE
path; the Apple/user external supplicant is the PMK owner. The next
semantic unit is therefore "make the local handshake consumer
owner-aware" rather than "find a fourth PMK delivery selector".

## Stage 2 acceptance markers planned for this candidate

The credential-safe markers planned for Stage 2 after-fix runtime
collection (no raw PMK bytes are logged at any point):

- `associateSSID_owner SELECTED owner=<local|external|none> source=<public_ad_key|wcl_associate|no_carrier> key_len=N ic_flags=0xMASK ...`
  on every `associateSSID(...)` PSK branch entry.
- `ieee80211_recv_4way_msg1_owner_route owner=<local|external|none>
  ic_psk_nonzero_bytes=N/32 rsnakms=0xMASK ic_external_pmk_owner=<0|1>
  deferred_to_external_supplicant=<0|1> deferred_no_owner=<0|1>` on
  every PSK 4-way M1 reception. Only the local branch may copy into
  `ni_pmk` and send M2; external and no-owner branches return early.
- `install_external_pmk INSTALLED source=<CIPHER_KEY|CIPHER_KEY_MSK|CUR_PMK>
  key_len=32 nonzero_bytes=N ... ic_external_pmk_owner=0` for any
  explicit PMK ingress that does fire.
- `clear_external_pmk CLEARED reason=<setDISASSOCIATE|setCLEAR_PMKSA_CACHE|setWCL_LEAVE_NETWORK|setWCL_REASSOC|setWCL_JOIN_ABORT|associateSSID_disable_rsn>
  ic_psk_nonzero_bytes=0/32 ic_flags=0xMASK ic_external_pmk_owner=0
  ni_pmk_cleared=<0|1>` at every lifecycle reset edge that fires during
  the test window.
- Existing structural counters `setCipherKey_pmk_install_count`,
  `setCUR_PMK_pmk_install_count`, `external_pmk_eligibility_clear_count`,
  and `cr237_associateSSID_count` continue to monotonically reflect
  setter / clear activity.

The first M1 owner-route marker is decisive for the same-root
hypothesis. A CONTROL_STA_NETWORK PSK association that publishes a
live `owner=external deferred_to_external_supplicant=1` confirms the
recovered WCL owner model. A `owner=local` event with non-zero
`ic_psk_nonzero_bytes` on the same edge confirms the explicit local
PMK ingress path. A `owner=none deferred_no_owner=1` event on the
same edge confirms the no-owner safety branch: the local PAE has no
PMK material and correctly does not send a zero-PMK M2. All three
outcomes are valid per the recovered Apple contract; only the
local-owner branch may copy `ic_psk` into `ni_pmk`, set
`IEEE80211_NODE_PMK`, derive a PTK, and send M2.

## Residual uncertainty

- If the runtime evidence after this Stage 1 still shows zero
  `install_external_pmk` markers and the M1 owner-route marker reports
  only `owner=external deferred_to_external_supplicant=1` and/or
  `owner=none deferred_no_owner=1`, the deferred behaviour is by
  design: the external Apple/user supplicant is the legitimate first
  M1 consumer in the external-owner case, and the local PAE correctly
  refuses to send a zero-PMK M2 in the no-owner case. A separate
  Stage 2 may then claim the deferred observation is the expected
  Tahoe contract and that the local kext correctly does not pre-empt
  the external supplicant or fabricate a zero PMK; the live
  association/DHCP outcome belongs to the external supplicant on the
  external-owner path, and the no-owner path requires either a fresh
  external owner re-assertion or an explicit local PMK ingress to
  resume.
- If a future trace shows an out-of-band Apple producer publishing
  PSK PMK bytes through a side-channel not yet enumerated, that
  producer must be added to the recovered carrier order and the
  matching local ingress path wired through `installExternalPmkLocked`.
  Until then, the owner-route marker correctly classifies the active
  owner.
- The placeholder slots `[664]..[749]` and the packed `apple80211_pmk`
  carrier ABI remain reachable as compiled code; this Stage 1 does not
  widen the claim scope to those slots.
- `programPMK` is BCM firmware-only and is intentionally not implemented.
