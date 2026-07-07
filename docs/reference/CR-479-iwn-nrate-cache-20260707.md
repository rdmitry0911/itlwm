# CR-479 iwn nrate producer closure

Date: 2026-07-07

Runtime trigger:

- Tahoe guest hardware is on the `ItlIwn` HAL path (`iwn-6030`).
- After the current-BSS/public association fixes, `wdutil info` reported
  `Tx Rate 0.0 Mbps` even while the link stayed associated and traffic passed.

Reference facts:

- `AppleBCMWLANCore::getRATE(apple80211_rate_data*) @ 0xffffff80015e596c`
  gates on current association and then reads the cached transport-rate
  carrier; the public result is only `apple80211_rate_data::rate[0]`.
- `AppleBCMWLANCore::getMCS_VHT(...) @ 0xffffff8001621608` and
  `getGUARD_INTERVAL` consume the same normalized `nrate` carrier. The carrier
  shape is recorded in `CR-479-rate-nrate-normalization-20260707.md`.

Local gap:

- Tahoe `getTahoeCachedNrate()` handled `ItlIwm` and `ItlIwx` only.
- The active `ItlIwn` path had real rate-control state (`iwn_tx()` selected
  `ridx`, `tx->plcp`, and `tx->rflags`; TX_DONE/A-MPDU callbacks reported
  firmware rate status), but no Apple-shaped cached `nrate` producer.

Closure:

- `iwn_softc` now owns a last Apple `nrate` cache, cleared on association
  reset/new-association edges.
- Each unicast data TX slot stores the Apple `nrate` derived from the same
  selected rate command that is submitted to firmware.
- TX_DONE and A-MPDU/BlockAck paths confirm the cache from the per-slot carrier
  and, for HT completions, from firmware `rate/rflags` status.
- Both Tahoe Skywalk and legacy Apple80211 getter paths now read the same
  `ItlIwn` cache instead of returning Apple's config-no-value status on iwn
  hardware.
