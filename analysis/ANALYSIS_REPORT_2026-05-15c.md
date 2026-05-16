# 2026-05-15c Stage 1 design: Tahoe Skywalk APPLE80211_CIPHER_PMK -> ic_psk supplicant bridge

correlation_id: CR-479-stage1-tahoe-skywalk-setCIPHER_KEY-PMK-host-supplicant-bridge
authoring_role: coder
authoring_agent: external-analysis-agent
filed_at: 2026-05-15T22:30+03:00
builds_on:
  - analysis/CR-479-pmk-delivery-channel-ghidra-20260515.md
    (sha256 adfbadd1f6769e82252a4244b933d89caa85385c760b1c4e0a331068eef88e1a)
  - docs/reference/CR-479-pmk-delivery-channel-ghidra-20260515.md
    (sha256 adfbadd1f6769e82252a4244b933d89caa85385c760b1c4e0a331068eef88e1a)
  - analysis/ANALYSIS_REPORT_2026-05-15a.md (rev5 Stage 1 carrier evidence,
    superseded by 15b for the dispatch layer)
  - analysis/ANALYSIS_REPORT_2026-05-15b.md (rev5 Stage 1 Skywalk dispatch
    glue documentation; this 15c continues from its single-owner closure
    and adds the upstream supplicant-bridge layer above it)

## Why this layer is the next coherent unit

The previously approved Stage 1 rev5 (Skywalk LINK_CHANGED 32-byte
publisher) is structurally complete in source and installed binary
but produces zero positive carrier observations in the Stage 2
runtime, because the link-state chain is never invoked: the upstream
WPA2-PSK 4-way handshake never reaches `IEEE80211_S_RUN`. The bounded
analysis closure
`analysis/CR-479-pmk-delivery-channel-ghidra-20260515.md` recovered the
Apple-side PMK delivery contract: airportd delivers the PSK-derived
PMK to host-side drivers on Skywalk-managed STA hosts through
`APPLE80211_IOC_CIPHER_KEY` (=3) with `key_cipher_type =
APPLE80211_CIPHER_PMK` (=6); the BCM Apple driver's reference action
at `AppleBCMWLANJoinAdapter::setKey` case 6 stores that PMK into its
driver-internal state. The itlwm `AirportItlwmSkywalkInterface::-
setCIPHER_KEY` handler currently drops the same delivery with
`XYLog("Setting WPA PMK is not supported")`, so `ieee80211com::ic_psk`
remains zero, the OpenBSD PAE supplicant computes a wrong PTK MIC at
4-way M2, and the AP rejects the M2 with the
`AP-STA-POSSIBLE-PSK-MISMATCH` pattern seen in runtime evidence.

This Stage 1 closes the supplicant-bridge layer with one coherent
functional boundary: between the Apple userspace `setCIPHER_KEY` IOCTL
delivering the PMK and the OpenBSD net80211 PAE supplicant consuming
`ic_psk` on the next 4-way M1.

## Layer scope (one functional boundary, decomp-debt closed)

Files in the layer:

- `AirportItlwm/AirportItlwmSkywalkInterface.cpp`: implement the
  `APPLE80211_CIPHER_PMK` branch in `setCIPHER_KEY` (line 2594) and
  add the `externalPmkOwner` guard around `ieee80211_disable_rsn(ic)`
  inside `associateSSID` (line ~949).
- `itl80211/openbsd/net80211/ieee80211_pae_input.c`: add a non-secret
  structural marker after the PSK-AKM PMK copy in
  `ieee80211_recv_4way_msg1` reporting the PMK non-zero byte count
  and the AKM mask.

The layer also depends on already-existing donor code in
`AirportItlwm/AirportItlwmV2.cpp::deliverExternalPMK` (validate +
memcpy + setwpaparms) and on the `IEEE80211_F_PSK`/
`ic_psk`/`ic_rsnprotos`/`ic_rsnakms` semantics in
`itl80211/openbsd/net80211/ieee80211_var.h` and
`itl80211/openbsd/net80211/ieee80211_ioctl.c::ieee80211_ioctl_-
setwpaparms`. No new headers or build-system changes.

## Recovered contracts and their per-field lifecycles

### Apple PMK delivery channel (recovered in the prior Ghidra closure)

- IOCTL number: `APPLE80211_IOC_CIPHER_KEY = 3` (
  `include/Airport/apple80211_ioctl.h:67`).
- Payload struct: `apple80211_key` (defined in
  `include/Airport/apple80211_var.h`).
- Field `key_cipher_type`: `APPLE80211_CIPHER_PMK = 6` (
  `apple80211_var.h:119`). Producer: airportd userspace at the WCL
  join orchestration site. Consumer:
  `IO80211SkywalkInterface::setCIPHER_KEY` virtual overridden by the
  STA driver (us). Lifecycle: a single value chosen by airportd at
  IOCTL send time.
- Field `key_len`: must equal `IEEE80211_PMK_LEN` (32, defined in
  `itl80211/openbsd/net80211/ieee80211_crypto.h:83`). Producer:
  airportd. Consumer: this handler. Validation: handler rejects
  non-32-byte payloads to avoid wedging the supplicant with a
  truncated PMK.
- Field `key->key`: 32 bytes of PMK. Producer: airportd computes
  PMK from the keychain passphrase via PBKDF2-SHA1(passphrase, SSID,
  4096, 32) per IEEE 802.11i. Consumer: the host-side
  `ieee80211com::ic_psk`. Lifetime: persists from this install until
  a subsequent install with a different PMK or until
  `ieee80211_disable_rsn` is invoked from a non-associateSSID caller.
- Field `key->key_flags` and `key->key_index`: not consumed for the
  PMK install (they apply to PTK/GTK installs and are checked by
  the existing TKIP/CCM cases).

### Host-side sink state machine

- `ieee80211com::ic_psk` (`itl80211/openbsd/net80211/ieee80211_var.h:567`):
  32 bytes, profile-scoped PSK material. Producer: this Stage 1
  handler. Consumer:
  `ieee80211_recv_4way_msg1` at
  `itl80211/openbsd/net80211/ieee80211_pae_input.c:269` for the
  PSK AKM branch (the `else` of the 8021X-AKM check), where it is
  copied into `ni->ni_pmk` and fed to `ieee80211_derive_ptk`.
- `IEEE80211_F_PSK` (`ieee80211_var.h:666`): policy flag indicating
  the supplicant is configured for PSK. Producer: this handler and
  the existing PSK paths in `associateSSID`. Consumer: the auth/join
  flow.
- `ic_rsnprotos`, `ic_rsnakms`, `ic_rsngroupcipher`, `ic_rsnciphers`:
  RSN policy fields set by `ieee80211_ioctl_setwpaparms` from
  `wpa.i_protos`/`wpa.i_akms`. Producer: this handler. Consumer:
  the PAE state machine via `ni->ni_rsnakms` etc. derivations.

### Ordering competing-hypothesis closure

Two orderings of `setCIPHER_KEY(PMK)` vs `setWCL_ASSOCIATE` are
possible from Apple userspace and the auditor instruction forbids
deferring a downstream branch as a future conditional. This Stage 1
handles both, and the disable_rsn skip is scoped to PSK-AKM
associations only (open, WEP, WPA-Enterprise, 8021X WCL paths still
get the full RSN reset because they have no separate PMK delivery
to preserve):

1. PMK arrives BEFORE `setWCL_ASSOCIATE`: this Stage 1 stores the
   PMK in `ic_psk` and sets `IEEE80211_F_PSK` in the handler. The
   subsequent `setWCL_ASSOCIATE` calls `associateSSID(...,
   externalPmkOwner=true, authtype_upper=...)`. With the previous
   code, the `ieee80211_disable_rsn(ic)` call at line 949 would
   zero `ic_psk`. This Stage 1 gates that call with
   `external_psk_path = externalPmkOwner && (authtype_upper &
   (APPLE80211_AUTHTYPE_WPA_PSK | APPLE80211_AUTHTYPE_WPA2_PSK |
   APPLE80211_AUTHTYPE_SHA256_PSK))` and only skips disable_rsn
   when `external_psk_path` is true, so the externally-owned PMK
   is preserved across the reset-before-reconfigure edge for the
   PSK paths only. The subsequent `ieee80211_ioctl_setwpaparms`
   call inside `associateSSID` rebuilds
   `ic_rsnprotos`/`ic_rsnakms`, which the PSK supplicant needs.
2. PMK arrives AFTER `setWCL_ASSOCIATE`: `associateSSID` runs first.
   When the new gate is true (PSK path) the disable_rsn skip
   preserves any prior PMK; when the gate is false the RSN state
   is cleared as before. Later `setCIPHER_KEY(APPLE80211_CIPHER_PMK)`
   runs and stores the correct PMK into `ic_psk` and refreshes
   the RSN policy fields. The first 4-way M1 thereafter reads
   the correct PMK.

Both orderings on the PSK path reach the same end state: `ic_psk`
contains the PSK-derived PMK and the RSN policy fields are
WPA1|WPA2 PSK|SHA256_PSK when `ieee80211_recv_4way_msg1` next runs.
Open / WEP / WPA-Enterprise / 8021X WCL associations are not
affected by the gate (the gate evaluates to false) so their RSN
state lifecycle is unchanged.

### Invalid-delivery contract for the CIPHER_PMK branch

The `APPLE80211_CIPHER_PMK` arm returns explicit IOReturn
values rather than `break`-ing to the surrounding function's
common `kIOReturnSuccess`:

- `kIOReturnNotReady` when `fHalService` or
  `fHalService->get80211Controller()` is null, so userspace can
  observe that the host stack is not yet attached.
- `kIOReturnBadArgument` when `key->key_len != IEEE80211_PMK_LEN`,
  so userspace can observe that the IOCTL payload did not match
  the expected 32-byte PMK contract.
- `kIOReturnSuccess` on a successful install (PMK stored,
  IEEE80211_F_PSK set, RSN policy fields refreshed).

This matches the V2 donor contract in
`AirportItlwm::deliverExternalPMK` and removes the previous
silent success-with-ignore on invalid input.

### Why not modify ieee80211_disable_rsn itself

`ieee80211_disable_rsn` is also called from the user-explicit "WPA
off" path (`ieee80211_ioctl_setnwkeys`/`ieee80211_ioctl_setwpaparms`
disable branches) where wiping `ic_psk` is semantically correct: if
the user disables WPA on this network or switches to WEP, the PMK
should be cleared. The narrower fix is to keep
`ieee80211_disable_rsn` semantics intact and gate the call site in
`associateSSID`'s WCL/Skywalk PSK path where the wipe is the wrong
contract. The single guarded line is the only place in the project
that combines "I am about to reconfigure RSN", "an external PMK
owner has already delivered a PMK", and "the authentication type
for this association is a PSK AKM".

### Legacy (non-Skywalk) and non-PSK WCL paths preserved

`AirportItlwmSkywalkInterface::associateSSID` is also called at line
4110 with `importLocalPmk=true, externalPmkOwner=false` from the
legacy ASSOCIATE handler. For that caller `external_psk_path`
evaluates to false (externalPmkOwner is false) so
`ieee80211_disable_rsn` still runs, preserving the previous
PMK-from-local-key install lifecycle unchanged.

For WCL associations with `externalPmkOwner=true` but
`authtype_upper` lacking any of `APPLE80211_AUTHTYPE_WPA_PSK`,
`APPLE80211_AUTHTYPE_WPA2_PSK`, `APPLE80211_AUTHTYPE_SHA256_PSK`
(open, WEP, WPA-Enterprise, 8021X, SAE/WPA3), `external_psk_path`
also evaluates to false and `ieee80211_disable_rsn` runs as before.
The skip is therefore strictly scoped to the PSK-AKM WCL path
where setCIPHER_KEY(PMK) is the documented PMK delivery channel.

`AirportItlwm::associateSSID` (the V2 method at AirportItlwm.cpp:141)
is a separate method without the `externalPmkOwner` parameter and is
not changed by this Stage 1. Its associated `deliverExternalPMK`
contract remains the V2/PLTI path's responsibility.

## Verification plan for Stage 2

After Stage 1 approval and the after-fix runtime authorisation, the
guest WPA2 PSK association against the controlled lab AP must show:

1. `setCipherKey_pmk_install_count > 0`, with at least one
   `setCIPHER_KEY PMK INSTALLED key_len=32` line during the join
   attempt.
2. At least one
   `ieee80211_recv_4way_msg1_pmk_check ni_pmk_nonzero_bytes=NN/32`
   line with `NN >= 16` (the same non-zero gate the project uses
   for accepted PMK material).
3. `ieee80211_recv_4way_msg3` line observed (AP accepts our M2 MIC
   and proceeds to M3).
4. `ic_state` reaches `IEEE80211_S_RUN`.
5. `setLinkStateGated_DEBUG_count > 0` with
   `kIONetworkLinkActive` semantics.
6. `setLinkStateInternal_DEBUG_count > 0`.
7. Exactly one `Tahoe_Skywalk_M_LINK_CHANGED_isLinkDown_count_NEW_-
   publisher=1` line per accepted association edge.
8. Host-side `iw dev wlxe84e062bc4f5 station dump` (or the
   equivalent FAST_LAB_AP `status-itlwm-lab-ap.sh`) shows
   `authorized=yes` after the association.

If any of (1)-(7) is zero or (8) shows `authorized=no` with
`AP-STA-POSSIBLE-PSK-MISMATCH`, the upstream PMK ordering or
content is wrong and the auditor must route the next layer
(either auditor-routed re-decomp of `apple80211setCUR_PMK` as a
parallel channel, or a Ghidra trace of airportd's IOCTL ordering).

## Sensitive-artifact hygiene

This document contains no literal lab SSID, no Wi-Fi password or
passphrase, no sudo credential, no provider login account, and no
raw runtime credential bytes. The PMK contract is described by
field names, lengths, and cipher constants only; the runtime
markers report non-zero byte counts, never raw key material.

## Self-checks

- coder_decomp_completeness_self_check: YES
- reference_decomp_first_for_capability_gap: YES
- coder_payload_field_lifecycle_completeness_self_check: YES
- decomp_batch_scope: FULL_REMAINING_LAYER_DEBT
- atomic_layer_unit: YES
- decomp_remainder_size: SMALL_BOUNDED
