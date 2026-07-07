# CR-479 RATE / nrate normalization witness

Date: 2026-07-07

Reference sources:

- `AppleBCMWLANCore::getRATE(apple80211_rate_data*) @ 0xffffff80015e596c`
  in `~/Projects/ghidra_output/AppleBCMWLANCoreMac_decompiled.c`
- `AppleBCMWLANCore::getMCS_VHT(...)` nrate body at
  `0xffffff8001621608` in the same decompile

Recovered public facts:

- `getRATE` checks the current-network owner and returns `0xe0822403` when
  it is not associated.
- On success, `getRATE` writes only the dword at caller offset `+0x08`
  (`apple80211_rate_data::rate[0]`) as `internal_rate / 1000`; it does not
  initialize `version` or `num_radios`.
- The Apple nrate config carrier is a normalized word, not the Intel raw
  `rate_n_flags` layout. The already-recovered nrate consumers decode family
  bits `0x01000000`, `0x02000000`, and `0x03000000`, with VHT NSS in bits
  `+0x04`, bandwidth in `0x10000..0x40000`, and VHT SGI at bit 23.

Local closure:

- `TahoeNrateContracts` now normalizes iwm/iwx raw firmware rate words into
  the Apple nrate carrier before `getMCS`, `getMCS_VHT`, and
  `getGUARD_INTERVAL` decode them.
- Tahoe and legacy `getRATE` now preserve Apple's carrier shape: after the
  associated-current-network gate, they only update `rate[0]` and leave the
  caller's other carrier fields untouched.
- The local RATE value is sourced from the normalized transport-rate cache and
  decoded to integer Mbps, matching the reference `internal_rate / 1000`
  public unit instead of rebuilding from `ic_bss->ni_txmcs`, channel width,
  SGI, and NSS side fields.
