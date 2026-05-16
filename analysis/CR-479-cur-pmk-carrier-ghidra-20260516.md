# CR-479 Tahoe alternate PMK carrier reference recovery (2026-05-16)

correlation_id: CR-479-stage1-tahoe-skywalk-CUR_PMK-CIPHER_KEY-external-pmk-ingestion-20260516
review_stage: REFERENCE_DECOMP_RECOVERY
provenance: Reference-decomp closure imported through the project web-AI
provider workflow and accepted by the auditor with status
`PROVIDER_RESULT_ACCEPTED_DECOMP_CLOSED`; full-target closure with no
remaining decomp targets.

## Scope

Recovered Apple reference contract for the Tahoe Skywalk host-supplicant
PSK PMK delivery and ownership layer. Closes the prior CR-479 same-root
gap that the CIPHER_KEY(PMK) carrier alone could not satisfy at runtime.

## Recovered dispatch

`apple80211setCUR_PMK` lives at BootKC label `0xffffff80021eb3b9` inside
the apple80211 set-stub cluster. The wrapper:

1. Loads selector `ESI = 0x168` (= `360` decimal). Distinct from
   `APPLE80211_IOC_CIPHER_KEY = 3`.
2. Passes a command/restricted-mode gate at vtable offset `+0xcc8` with
   the same selector.
3. Safe-casts to `IO80211SkywalkInterface`. On null/failed cast the
   wrapper returns `0xe082280e`.
4. On success, invokes the Skywalk virtual at vtable offset `+0x1770`
   with `(this, apple80211_pmk*)`.

The matching getter `apple80211getCUR_PMK` uses selector `0x16a`, the
same gate at vtable `+0xcc8`, and Skywalk virtual slot `+0xff0`. The
AppleBCMWLANInfraProtocol receiver
`AppleBCMWLANInfraProtocol::getCUR_PMK @ 0xffffff80015424b4` loads the
owner at `this + 0x130` and tail-calls AppleBCMWLANCore getter
`FUN_ffffff80016318c2`. The userclient wrappers
`getCUR_PMK @ 0xffffff80022003a1` and `getCurPmk @ 0xffffff80022a7149`
validate request size/state/pointer before receiver dispatch and return
request-style errors `0x16`, `0x2d`, `0x13`, or `0x10` on failed checks.

## Recovered `apple80211_pmk` carrier layout

| Offset | Size      | Field                  | Source       | Notes |
| ------ | --------- | ---------------------- | ------------ | ----- |
| +0x00  | 4         | header / reserved      | -            | not consumed by recovered core setter/getter |
| +0x04  | 4         | `key_len`              | caller       | setter accepts length `< 0x41`; getter writes owner length |
| +0x08  | up to 64  | getter output bytes    | getter       | getter copies owner store `core + 0xdf` to outgoing carrier |
| +0x10  | up to 64  | setter source bytes    | caller       | setter copies input material to owner `core + 0xdf` |
| +0x48  | 4         | status / version tag   | getter       | getter writes `0x10` |
| +0x4c  | 8         | metadata cookie 0      | getter / setter clears | matches owner `core + 0x124` |
| +0x54  | 8         | metadata cookie 1      | getter / setter clears | matches owner `core + 0x12c` |

The AppleBCMWLANCore PMK owner state is held outside the carrier:

- `core + 0xdf`: 64-byte PMK store.
- `core + 0x120`: owner length (0 when uninitialised, otherwise 1..64).
- `core + 0x124`, `core + 0x12c`: metadata cookies returned at
  carrier `+0x4c` and `+0x54`.

Apple PMK helpers:

- `FUN_ffffff8001631a1a`: setter helper. Validates gate/pointer, reads
  length at carrier `+0x04`, copies bytes from carrier `+0x10` into
  owner `core + 0xdf`, writes length to owner `core + 0x120`, clears
  metadata at owner `core + 0x124` and `core + 0x12c`.
- `FUN_ffffff80016318c2`: getter helper. Validates gate/owner length,
  copies owner bytes into carrier `+0x08`, writes carrier `+0x04`,
  `+0x48`, `+0x4c`, `+0x54`.
- `FUN_ffffff8001631d50`: zeroizer. Clears owner length at
  `core + 0x120` and zeros the full 64-byte PMK store at
  `core + 0xdf .. core + 0x11e`.

PMK byte regions are credential material; all static reporting and
runtime logging must name only offsets/sizes and zero/nonzero state. No
PMK bytes may appear in any committed artifact or log line.

## Carrier ordering on a PSK association edge

No strict ordering rule requires `apple80211setCUR_PMK` to precede
`apple80211setWCL_ASSOCIATE`. The recovered Apple semantics are:

- `apple80211setCIPHER_KEY` case 6 / case 9 is the normal public host
  PMK / MSK delivery path. `APPLE80211_IOC_CIPHER_KEY = 3` with
  `key_cipher_type == APPLE80211_CIPHER_PMK = 6` or
  `APPLE80211_CIPHER_MSK = 9` updates the AppleBCMWLANCore PMK owner.
- `apple80211setCUR_PMK` selector `0x168` is a separate current-PMK
  setter that targets the same owner.
- `apple80211setWCL_ASSOCIATE` selector gate `0x1b3` is the hidden
  association candidate carrier. It can legally carry
  `externalPmkOwner=true` with `key_len=0`; the candidate body holds no
  PMK bytes.

The ordered PSK association edge therefore looks like:

1. `WCLJoinRequest` stores and validates the private Apple80211 key
   pointer at body `+0x18`.
2. The host delivers the PMK through `apple80211setCIPHER_KEY` case 6/9
   or `apple80211setCUR_PMK` (or both) into the AppleBCMWLANCore owner.
3. `apple80211setWCL_ASSOCIATE` carries BSSID/SSID/RSN/candidate data
   and may contain no PMK bytes.
4. The supplicant/link path proceeds through `WCLNetManager` and the
   later key install / 4-way handshake. The host-supplicant 4-way M1
   must already have a valid PMK in the local PMK store.

## `WCLJoinRequest` and external PMK ownership

`WCLJoinRequest::checkValidationForApple80211Key @ 0xffffff8002221eb8`
returns the private key pointer stored at `*(this + 0x10) + 0x18`.
`WCLJoinRequest::checkValidationForApple80211AssociationData @ 0xffffff8002221c04`
validates the current join candidate at `*(this + 0x10) + 0x20`; a null
candidate logs "WCLJoinRequest NULL current join candidate".

A `WCLJoinRequest` body holds a valid private key owner at `body + 0x18`
while the WCL_ASSOCIATE candidate at `body + 0x20` carries no key bytes.
This is the recovered explanation for `externalPmkOwner=true` with
`key_len=0` on the observed Tahoe runtime: the local
`AirportItlwmSkywalkInterface::associateSSID` must accept the no-copy
external-owner association state and must not reject the edge solely
because candidate key bytes are absent.

## `WCLNetManager` / `WCLRoamManager` lifecycle boundaries

WCLNetManager timeline functions: `linkUp @ 0xffffff800211014a`,
`set4wayHsTimeout @ 0xffffff80021109ec`,
`leaveNetworkCommand @ 0xffffff8002111434`,
`connectComplete @ 0xffffff8002111594`,
`joinDone @ 0xffffff800211437e`,
`handleSupplicantEvent @ 0xffffff8002115788`,
`updateSupplicantEvent @ 0xffffff80021157bc`,
`handleRoamDoneEvent @ 0xffffff80021160f8`,
`handleFwDebounceFailure @ 0xffffff8002116ab0`. WCLRoamManager edges
appear around `0xffffff80021284f8`, `0xffffff8002128510`,
`0xffffff8002129baa`, `0xffffff800212a3f2`, `0xffffff800212a3fa`,
`0xffffff800212a5e4`, `0xffffff800212c2f0`, and `0xffffff800212c6a0`.

Neither manager is the PMK byte owner. They sequence
join/roam/leave/link/supplicant events while PMK byte ownership stays in
`WCLJoinRequest` private key state and the AppleBCMWLANCore owner.
External-PMK eligibility does not survive the leave/disassoc/reassoc
boundary: a fresh join/reassoc must reuse a valid private/external PMK
owner supplied for that edge or clear stale ownership. The local
mapping is `clearExternalPmkEligibilityLocked(...)` at `setDISASSOCIATE`
and `setCLEAR_PMKSA_CACHE`.

## `AppleBCMWLAN setKey` case 6 / case 9 vs CUR_PMK

`AppleBCMWLANJoinAdapter::setKey @ 0xffffff8001578cb0` case 6
(`APPLE80211_CIPHER_PMK`) consumes the PMK-bearing `apple80211_key`,
calls the PMK helper `FUN_ffffff8001631a1a`, copies length and material
into the AppleBCMWLANCore PMK owner, and marks success. Case 9
(`APPLE80211_CIPHER_MSK`) shares the same helper and owner-update
semantics. Case 7 (`APPLE80211_CIPHER_PMKSA`) logs "Ignoring PMKSA
request" and is not the PMK byte owner path. The absence of a separate
`apple80211setCUR_PMK` call is not a failure if case 6/9 already
populated the owner. If neither carrier populated the owner,
`apple80211getCUR_PMK` returns the documented `0xe00002c7` failure and
PMK-dependent supplicant / firmware progress fails. `programPMK` is
BCM-specific and not a portable itlwm requirement.

## Reference-to-local mapping (Apple owner -> itlwm sink)

- AppleBCMWLANCore PMK byte store `core + 0xdf`  →  `ieee80211com::ic_psk`.
- AppleBCMWLANCore PMK ready state  →  `IEEE80211_F_PSK` plus refreshed
  WPA/RSN auth state (`ic_rsnprotos`, `ic_rsnakms`).
- Apple CIPHER_KEY case 6/9  →  `AirportItlwmSkywalkInterface::setCIPHER_KEY`
  `APPLE80211_CIPHER_PMK` branch, refactored to call the shared
  `installExternalPmkLocked` helper.
- Apple CUR_PMK selector 0x168 / Skywalk virtual `+0x1770`  →
  `AirportItlwmSkywalkInterface::setCUR_PMK` is the planned local
  SET-side bridge. The active userspace SET delivery path on Tahoe is
  the trampoline `apple80211setCUR_PMK` at `0xffffff80021eb3b9`
  passing the command gate at vtable `+0xcc8` and calling the Skywalk
  virtual receiver at vtable `+0x1770` with `(this,
  apple80211_pmk *)`. The local IO80211SkywalkInterface vtable does
  not yet override that SET-side slot; closing the override is a
  bounded decomp-only follow-up scoped on the recovered SET-side
  dispatch evidence in
  `docs/reference/CR-479-cur-pmk-userclient-dispatch-evidence-20260516.md`.
  The GET direction reaches the local interface through the V2
  `handleCardSpecific` card-specific bridge because the BSDCommand
  `getCurPmk` static helper at `0xffffff80022a7149` exists; the
  set-side BSDCommand helper does not exist in IO80211Family.
- Apple WCLJoinRequest body `+0x18` / candidate body `+0x20`  →
  `associateSSID(... externalPmkOwner=true, key_len=0)`; no copy.
- Apple WCL leave / disassoc / PMKSA clear / association reset /
  reassoc start / RSN-disable  →
  `clearExternalPmkEligibilityLocked(...)` planned at
  `setDISASSOCIATE`, `setCLEAR_PMKSA_CACHE`, `setWCL_LEAVE_NETWORK`,
  `setWCL_REASSOC`, `setWCL_JOIN_ABORT`, and the `associateSSID`
  RSN-disable path.
- Apple first M1 consumer  →  `ieee80211_recv_4way_msg1` reading
  `ic_psk` into `ni_pmk`.
- Apple `getCUR_PMK`  →  `AirportItlwmSkywalkInterface::getCUR_PMK`
  unchanged; returns the credential-safe Apple failure `0xe00002c7` and
  does not snapshot `ic_psk` to userspace.
- Apple `programPMK`  →  not implemented; BCM firmware-only.

## Recovery completeness

All required carrier, selector, payload-field, consumer, owner
lifecycle, ordering, and mapping targets are closed by the imported
result and the auditor import review. No remaining decomp/material
targets are open at this layer.
