# CR-479 active Tahoe PSK PMK reference / static contract (project-owned)

## Status

The recovered reference / static contract for the active Tahoe PSK PMK
producer/carrier/order is closed. The remaining work is local
implementation / integration.

## Carriers

1. `APPLE80211_IOC_CIPHER_KEY = 3`
   - `key_cipher_type = APPLE80211_CIPHER_PMK = 6` carries a 32-byte PMK.
   - `key_cipher_type = APPLE80211_CIPHER_MSK = 9` carries a 32-byte
     PMK-equivalent.
   - Both converge on the same AppleBCMWLANCore PMK owner.

2. `APPLE80211_IOC_CUR_PMK = 360`
   - Selector `0x168` SET, `0x16a` GET.
   - Wrapper `apple80211setCUR_PMK @ 0xffffff80021eb3b9`.
   - Command gate vtable `+0xcc8`; Skywalk virtual `+0x1770`; slot `[750]`
     base name `_RESERVED_IO80211SkywalkInterface_11`; AppleBCMWLAN APSTA
     override `0xffffff8000b72960`.
   - Payload `apple80211_pmk *` 0x5c bytes packed: `key_len` at `+0x04`,
     setter bytes at `+0x10`, getter window at `+0x08`, status at `+0x48`,
     metadata at `+0x4c` and `+0x54`.

3. `apple80211setWCL_ASSOCIATE` (selector `0x1b3`)
   - Association candidate carrier; private key owner at body `+0x18`,
     current join candidate at body `+0x20`.
   - May legally carry `externalPmkOwner = true` with `key_len = 0`. This is
     not a PMK delivery failure; only the PMK byte owner is external.

## PMK byte owner

AppleBCMWLANCore PMK store at `core + 0xdf`, owner length at `core + 0x120`,
metadata cookies at `+0x124` / `+0x12c`. Zeroizer clears the PMK store and
length. AppleBCMWLANJoinAdapter `setKey` case 6 and case 9 share the owner;
case 7 (PMKSA) is not the PMK-byte owner.

Local analogue: `ieee80211com::ic_psk` plus `IEEE80211_F_PSK`.

## Skywalk vtable slot boundary

- Base vtable: `__ZTV23IO80211SkywalkInterface @ 0xffffff80023e8248`.
- Slot range closed: `[664]..[750]`.
- Slot `[750]` entry: `0xffffff80023e99b8`, offset `+0x1770`, base function
  `0xffffff8002277d2e` (`_RESERVED_IO80211SkywalkInterface_11`).
- AppleBCMWLANIO80211APSTAInterface override: `0xffffff8000b72960`.

## State and lifecycle rules

- The PMK must be installed in the local PMK owner before the host
  supplicant consumes its first 4-way M1. The first local consumer is
  `ieee80211_recv_4way_msg1`, which copies `ic_psk` into `ni_pmk`, sets
  `IEEE80211_NODE_PMK`, and derives the PTK.
- Disassociate, leave, reassociation start, join abort, PMKSA cache clear,
  and non-external RSN-disable paths must clear external PMK eligibility.
- Link-state publication is preconditioned on accepted state transitions and
  is not itself proof of PMK delivery.

## Routing requirements on the Tahoe Skywalk card-specific bridge

The card-specific Skywalk bridge `shouldRouteTahoeSkywalkIoctlReq` must
allow both `APPLE80211_IOC_CIPHER_KEY = 3` (SET) and `APPLE80211_IOC_CUR_PMK
= 360` (SET and GET) so the live Tahoe PSK PMK carriers reach the local
handlers. Without the routing entry the IOCTL falls back to the default
IO80211 path, the local handler is never invoked, and `ic_psk` stays empty
before the host supplicant first M1 consumer.

## Credential-safety constraints

All PMK / PTK / MIC / EAPOL key bytes, raw passphrases, private SSIDs, and
provider login accounts are credential material. Logging may include only
structural metadata: selector, length, nonzero-byte count, source tag,
state, and return code. No raw key bytes, raw passphrases, or private
network identifiers in any committed artifact.
