# CR-479 Association Auth Without Fallback AKM Rewrite

Date: 2026-07-07

Scope: public and hidden local association mapping from Apple80211 auth carriers
to the net80211 WPA parameter block.

## Reference evidence

- The recovered hidden Tahoe association carrier keeps auth policy in the
  assoc-candidates owner and RSN IE pointer/length fields; it does not rewrite
  auth bits while moving the carrier into the local owner path.
- The public `setRSN_IE` reference body returns success immediately, while the
  hidden join path uses the associated RSN IE owner; public RSN handling is not
  a fallback AKM rewrite point.
- The local card-capability cluster was already corrected to avoid advertising
  Apple-impossible advanced AKM bits. A later pure WPA3 auth carrier therefore
  must not be silently converted into a WPA2 PSK/enterprise carrier by the
  association backend.

## Local closure

- `TahoeAssociationAuthContracts` centralizes the Apple auth masks that map to
  local net80211 WPA protocol, PSK AKM, and enterprise AKM programming.
- `AirportItlwm::associateSSID(...)` and
  `AirportItlwmSkywalkInterface::associateSSID(...)` now derive a local auth
  mask from the caller's explicit WPA/WPA2/SHA256 bits only.
- Mixed transition carriers that already contain a WPA2 PSK bit still program
  the local WPA2 PSK path.
- Pure WPA3 SAE / WPA3 enterprise carriers are no longer rewritten into WPA2
  PSK or WPA2 enterprise. They remain outside the local net80211 WPA mapping
  unless a caller supplies an explicit supported WPA/WPA2/SHA256 bit.

## Non-claims

- No SAE, WPA3 Enterprise, PMKSA, retry, delay, fallback AKM rewrite, deauth
  masking, EAPOL success, key install, DHCP, link, or data success is
  synthesized.
- This does not change the external PMK owner path for explicit PSK AKMs.
