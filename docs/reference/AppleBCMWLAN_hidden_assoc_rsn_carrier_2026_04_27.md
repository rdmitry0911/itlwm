# AppleBCMWLAN hidden association / RSN carrier contracts

Date: 2026-04-27

Scope: hidden Tahoe WCL association carrier and the local RSN IE compatibility
handoff used by itlwm. This note records field offsets and bounded-copy
contracts only. It does not claim final RSN/EAPOL/key/DHCP completion.

## Hidden association selectors

The Tahoe family association path uses hidden WCL carriers:

- selector `0x45`
- selector `0x46`
- assoc-candidates payload length `0x3ad8`

The local slot-470 bridge must only route the exact `0x45`-shape
assoc-candidates payload into the WCL association owner. Other callers keep the
existing unsupported/set-MAC behavior.

## `AppleBCMWLANCore::setWCL_ASSOCIATE(...)`

Function:

- `AppleBCMWLANCore::setWCL_ASSOCIATE(apple80211AssocCandidates *)`
- Address: `0xffffff80015fbacc`

Observed carrier fields:

- `+0x10`: auth lower
- `+0x14`: auth upper
- `+0x18`: auth flags
- `+0x1c`: SSID length
- `+0x20`: SSID bytes
- `+0xd4`: RSN IE length
- `+0xd6`: RSN IE pointer
- `+0x1e0`: Instant Hotspot flags
- `+0x1e1`: companion flag byte
- `+0x1ec`: WSEC mode flags
- `+0x1f4`: context BSSID field
- `+0x214`: BSS info flags
- `+0x217`: PMF capability bit source
- `+0x218`: candidate count
- `+0x220`: first candidate BSSID
- `+0x226`: first candidate paired MAC
- `+0x22c`: first candidate channel
- candidate stride: `0x12`

`AppleBCMWLANCore::setWCL_ASSOCIATE(...)` stores the auth/SSID context and
delegates the full carrier to `AppleBCMWLANJoinAdapter::performJoin(...)`.

## `AppleBCMWLANJoinAdapter::setAssocRSNIE(...)`

Function:

- `AppleBCMWLANJoinAdapter::setAssocRSNIE(unsigned char const *, unsigned long long)`
- Address: `0xffffff80015795b8`

Observed behavior:

- accepts a pointer and exact length;
- when the length is nonzero, builds a TX payload from that bounded length;
- when the length is zero, sends no RSN IE pointer/length payload;
- does not copy a fixed 257-byte local stack buffer.

## `AppleBCMWLANCore::setRSN_IE(...)`

Function:

- `AppleBCMWLANCore::setRSN_IE(apple80211_rsn_ie_data *)`
- Address: `0xffffff800160433e`

Observed behavior:

- returns success immediately.

The direct public setter is therefore not the reference producer for hidden WCL
join RSN programming. The hidden join path uses JoinAdapter `setAssocRSNIE`
with explicit pointer+length semantics.

## Local alignment

- Add `TahoeAssociationContracts.hpp` with hidden selector, payload-size,
  carrier-offset, candidate-list, PMF, Instant Hotspot, and IOCTL selector
  constants.
- Add `TahoeOwnerRegistry::AssociationOwner` as a local witness for the latest
  parsed hidden carrier.
- Replace active WCL-association magic offsets with the recovered constants.
- Keep the previously fixed selected-BSSID source: candidate count `+0x218`
  and first candidate BSSID `+0x220`.
- Change local RSN override storage to zero the override and copy only the
  bounded caller-provided IE length.

## Non-claims

- This batch does not force EAPOL TX.
- This batch does not synthesize key installation.
- This batch does not force RSN done, link success, DHCP, or data reachability.
- This batch does not rewrite AKM/auth bits.
- This batch does not add retry, poll, delay, replay, or synthetic callbacks.
