# CR-479 active Tahoe PSK PMK static closure (project-owned analysis)

## Status

- static_closure_status: FULL_DECOMP_CLOSED
- coder_readiness: STATIC_CLOSURE_CODER_READY_FOR_LOCAL_LAYER_WORK
- runtime_allowed: NO (until a new Stage 1 decision approves after-fix runtime)
- commit_allowed: NO

## Why this layer note exists

Stage 2 of the prior rev12 candidate built, installed, loaded, and ran without
panic, but every local instrumentation marker the rev12 Stage 1 decision
required at Stage 2 stayed at zero on the live CONTROL_STA_NETWORK PSK
association edge: `install_external_pmk`, `clear_external_pmk`, `setCUR_PMK`,
`setCIPHER_KEY`, `setCipherKey`, `setCipherKey_pmk_install_count`,
`setCUR_PMK_pmk_install_count`, `ieee80211_recv_4way_msg1_pmk_check`,
`ni_pmk_nonzero_bytes`, `APPLE80211_M_LINK_CHANGED`, `APPLE80211_CIPHER_PMK`,
`APPLE80211_IOC_CUR_PMK`, and `external_pmk_eligibility_clear_count` were all
zero. That decisive negative result falsifies the rev12 hypothesis that the
narrow CUR_PMK / CIPHER_KEY ingestion wiring is by itself the active live
Tahoe PSK PMK delivery path, and it requires the next coder unit to be a
bounded local layer that fixes the routing/ingestion gaps the rev12 evidence
exposes, not another one-selector guess.

## Recovered Apple reference contract for the PSK PMK layer

The recovered Apple/Tahoe contract for the active PSK PMK producer/carrier
order is the union of three legal carriers, all of which converge on the same
AppleBCMWLANCore PMK owner state (locally mapped to `ieee80211com::ic_psk`):

1. `APPLE80211_IOC_CIPHER_KEY = 3`
   - `key_cipher_type = APPLE80211_CIPHER_PMK = 6` carries a 32-byte PMK.
   - `key_cipher_type = APPLE80211_CIPHER_MSK = 9` carries a 32-byte
     PMK-equivalent extracted from a longer MSK payload.
   - Both cases land on the same AppleBCMWLANCore PMK owner.

2. `APPLE80211_IOC_CUR_PMK = 360`
   - Wrapper `apple80211setCUR_PMK @ 0xffffff80021eb3b9`.
   - Selector `0x168`; SIOCGA80211 selector `0x16a`.
   - Command gate vtable slot `+0xcc8`; Skywalk receiver virtual `+0x1770`;
     `IO80211SkywalkInterface` absolute slot `[750]` (base reserved name
     `_RESERVED_IO80211SkywalkInterface_11`, AppleBCMWLAN APSTA override
     target `0xffffff8000b72960`).
   - Payload `apple80211_pmk *`: `key_len` at `+0x04`, setter source bytes at
     `+0x10`, getter window at `+0x08`, status at `+0x48`, metadata at `+0x4c`
     and `+0x54`. The carrier is 0x5c bytes packed.

3. `apple80211setWCL_ASSOCIATE` / WCLJoinRequest
   - Selector `0x1b3`. Association candidate carrier with private key owner
     state at body `+0x18` and current join candidate at body `+0x20`.
   - May legally carry `externalPmkOwner = true` with `key_len = 0`. This is
     not a PMK delivery failure: it only marks that the PMK byte owner is
     external (i.e. the PMK was, or will be, delivered through carriers 1 or
     2). Treating the absent candidate key bytes as a PSK failure destroys
     any prior PMK install before the host supplicant consumes its first
     4-way M1.

The PMK byte owner is AppleBCMWLANCore (`core + 0xdf` PMK store, `core +
0x120` length, metadata cookies at `core + 0x124` / `+0x12c`, zeroizer clears
length and PMK store). The local analogue is `ieee80211com::ic_psk` plus
`IEEE80211_F_PSK` for the PSK AKM eligibility.

`AppleBCMWLANJoinAdapter::setKey` case 6 and case 9 both update the same
owner; case 7 (`APPLE80211_CIPHER_PMKSA`) is not the PMK-byte owner. The
zeroizer is invoked on disassociate, leave, reassociation, join abort, PMKSA
clear, and non-external RSN disable edges.

## Local first M1 consumer

`net80211/ieee80211_pae_input.c::ieee80211_recv_4way_msg1` copies `ic->ic_psk`
into `ni->ni_pmk` on PSK AKM, sets `IEEE80211_NODE_PMK`, and derives the PTK
from the node PMK. The structural credential-safe marker
`ieee80211_recv_4way_msg1_pmk_check ni_pmk_nonzero_bytes=N/32 rsnakms=0xMASK`
is the first observable consumer of the local PMK install.

## Why rev12 failed at Stage 2

Two recovered routing/ingestion gaps explain why every rev12 marker stayed
at zero on the live Tahoe Skywalk PSK association edge:

- The Tahoe Skywalk card-specific bridge `shouldRouteTahoeSkywalkIoctlReq`
  listed `APPLE80211_IOC_CUR_PMK` but did not list `APPLE80211_IOC_CIPHER_KEY
  = 3`, so when Apple userspace delivers PMK / MSK through that IOCTL on the
  Skywalk path, the request fell back to the default IO80211 path and never
  reached the local `setCIPHER_KEY` handler. The rev12 case 6 / case 9
  convergence on `installExternalPmkLocked` was therefore present in the
  built kext but unreachable from the live edge.
- `AirportItlwmSkywalkInterface::associateSSID(...)` unconditionally
  `memcpy`-ed the caller-provided `key` into `ic->ic_psk` whenever
  `importLocalPmk = true`, regardless of `key_len`. When the live Tahoe
  IOC_ASSOCIATE / WCL_ASSOCIATE delivery arrives with `key_len = 0` because
  Apple is using the external-owner carrier order, that memcpy zeroed
  `ic_psk` even if a prior `setCIPHER_KEY` / `setCUR_PMK` install had
  populated it, destroying the PMK before the local first M1 consumer.

## Local mapping table (recovered to itlwm)

| Apple/reference owner or field | Current itlwm source location | Local consumer / action |
|---|---|---|
| `APPLE80211_IOC_CUR_PMK = 360`, selector `0x168` | `include/Airport/apple80211_ioctl.h`; `AirportItlwmSkywalkInterface::processApple80211Ioctl` | SIOCSA80211 dispatches to `setCUR_PMK`; SIOCGA80211 to credential-safe `getCUR_PMK` failure |
| `APPLE80211_IOC_CIPHER_KEY = 3`, cases 6 / 9 | `AirportItlwmSkywalkInterface::setCIPHER_KEY`; `AirportItlwmV2.cpp` `shouldRouteTahoeSkywalkIoctlReq` | Both cases converge on `installExternalPmkLocked(...)`; V2 card-specific bridge must allow SET-side routing |
| Skywalk virtual slot `+0x1770`, absolute slot `[750]` | `include/Airport/IO80211InfraProtocol.h`; `AirportItlwmSkywalkInterface.hpp/.cpp` | Preserve slots `[664]..[749]`, override `setCUR_PMK(apple80211_pmk *)` |
| `apple80211_pmk::key_len` / setter bytes | `include/Airport/apple80211_var.h`; `setCUR_PMK` | Validate 32-byte PMK and call shared local helper |
| AppleBCMWLANCore PMK store | `ieee80211com::ic_psk` in `net80211/ieee80211_var.h`; `installExternalPmkLocked` | Host-supplicant PMK source |
| WCLJoinRequest external owner with `key_len = 0` | `associateSSID(...)` PSK branch in `AirportItlwmSkywalkInterface.cpp` | Treat as external-owner case: do not overwrite `ic_psk` with zero bytes; mark `IEEE80211_F_PSK` |
| WCL disassoc / leave / reassoc / join abort / PMKSA clear / RSN-disable | `setDISASSOCIATE`, `setWCL_LEAVE_NETWORK`, `setWCL_REASSOC`, `setWCL_JOIN_ABORT`, `setCLEAR_PMKSA_CACHE`, `associateSSID_disable_rsn` | Call `clearExternalPmkEligibilityLocked(reason_tag)` |
| First M1 consumer | `net80211/ieee80211_pae_input.c::ieee80211_recv_4way_msg1` | Copy `ic_psk` into `ni_pmk`, set `IEEE80211_NODE_PMK`, derive PTK |

## Bounded layer change in this iteration

This iteration extends the rev12 bounded source/header/docs integration with
two minimum-scope additions that close the two routing/ingestion gaps the
rev12 Stage 2 evidence exposed:

1. `AirportItlwmV2.cpp` `shouldRouteTahoeSkywalkIoctlReq(...)` adds
   `APPLE80211_IOC_CIPHER_KEY` as a SET-side allowed selector so the live
   Tahoe Skywalk card-specific bridge reaches the local `setCIPHER_KEY`
   handler.
2. `AirportItlwmSkywalkInterface::associateSSID(...)` no longer overwrites
   `ic->ic_psk` when the caller signals `importLocalPmk = true` but provides
   no usable key bytes (`key == nullptr` or `key_len < sizeof(ic->ic_psk)`).
   A previously installed PMK from `setCIPHER_KEY` / `setCUR_PMK` survives
   into the host supplicant first M1 consumer; `IEEE80211_F_PSK` is still
   marked for the PSK AKM eligibility.

No other source/header/diagnostic change is added in this iteration. The
rev12 carrier/slot/lifecycle integration, packed `apple80211_pmk` ABI,
`APPLE80211_M_LINK_CHANGED` 32-byte publication, lifecycle clears at the
named reset points, and `ieee80211_recv_4way_msg1_pmk_check` marker are
unchanged.

## Non-claims

- This iteration does not claim 4-way completion, DHCP/IP, RUN steady state,
  AP-mode functionality, sustained `authorized=yes`, broader stability, or
  project completion.
- This iteration does not introduce timing, retry, fallback, forced state,
  masking, suppression, or any speculative "try and see" change.
- This iteration does not request runtime, kext install, reboot, unload, or
  commit before a new Stage 1 decision authorizes after-fix runtime.

## Residual uncertainty

If the runtime evidence after this iteration still shows zero
`install_external_pmk` markers, the next-step research must shift to whether
Tahoe airportd uses an alternative WCL-only PMK delivery selector that does
not enter `APPLE80211_IOC_CIPHER_KEY` or `APPLE80211_IOC_CUR_PMK`, and to the
`apple80211setWCL_ASSOCIATE` selector `0x1b3` carrier path.

## Provenance

The recovered carrier identities, vtable slot, payload offsets, owner
contracts, lifecycle edges, and route recommendation come from the
auditor-accepted static-closure synthesis result that combined the BootKC
p-code / disassembly / xref material with the userland CoreWiFi/airportd
static material and the Skywalk slot `[664]..[750]` closure. The conclusion
is `IMPLEMENT_LOCAL` for the bounded local layer above. No new
decompilation/reference batch is requested by this iteration.
