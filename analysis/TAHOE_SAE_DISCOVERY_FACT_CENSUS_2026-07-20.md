# Tahoe SAE discovery-fact census

## Scope

This layer extends the passive BSS census introduced by the SAE discovery
foundation. It does not select a BSS, enable Algorithm 3, wire a UserClient
selector, derive or accept a PMK, change an RSN output IE, or enable PMF/IGTK.
All new values are normalized scan facts in `ni_sae_scan_flags`; no raw ExtCap,
RSNXE, Rates/XRates, or credential material is retained for a future carrier.

## Facts captured at the scan boundary

- Extended Capabilities EID 127 is read byte-wise, LSB-first, for bit 81
  (SAE Password Identifier), bit 82 (Password Identifier Exclusively), and
  bit 88 (SAE-PK Exclusively). Existing 64-bit ExtCap macros are intentionally
  not used above bit 63.
- RSNXE bit 5 remains the H2E fact. RSNXE bit 6 is SAE-PK, not an AKM; it is
  recorded and classified `UNSUPPORTED` pending a complete SAE-PK owner.
- Membership selector 123 is recognized only as a basic rate octet `0xfb`,
  in either Rates or XRates before `ieee80211_setup_rates()` can sort it.
- RSN `00:0f:ac:24` (`SAE_EXT_KEY`) and `00:0f:ac:25`
  (`FT-SAE-EXT-KEY`) are counted as extended-SAE; neither is aliased to
  SAE-PK. A profile carrying either is `UNSUPPORTED` for future pure SAE
  admission until an exact owner exists.
- Duplicate RSN/RSNXE/ExtCap/Rates/XRates, malformed TLV boundaries, and
  cross-IE contradictions are retained as fail-closed flags. In particular,
  password-ID-exclusive without password-ID, SAE-PK-exclusive without RSNXE
  SAE-PK, and H2E-only selector without RSNXE H2E are inconsistent.

## Compatibility boundary

The existing scanner still handles its legacy IEs exactly as before; the
additional duplicate detection affects only SAE fact flags. Current Tahoe
association ingress continues to reject pure WPA3 before PSK/PMK handling,
and net80211 continues to emit and accept only Open-System authentication.

## Verification

`tests/net80211_sae_policy_test.c` exercises short ExtCap payloads, each
relevant bit, SAE-PK RSNXE, H2E-only membership-selector variants, and all
cross-IE inconsistency flags. The product contract additionally proves that
the scanner normalizes at the bounded probe/beacon path and that no active
SAE configuration, raw ioctl, auth algorithm, or RSN output was added.
