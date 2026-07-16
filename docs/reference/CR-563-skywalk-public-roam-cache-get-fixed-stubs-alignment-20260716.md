# CR-563 — Skywalk public ROAM/CACHE GET fixed-stub alignment

## Scope

This change recovers only the current 25C56 public Tahoe BSD GET behavior for
`APPLE80211_IOC_ROAM_THRESH` (IOC 80),
`APPLE80211_IOC_VENDOR_DBG_FLAGS` (IOC 81),
`APPLE80211_IOC_CACHE_AGE_THRESH` (IOC 82),
`APPLE80211_IOC_PMK_CACHE` (IOC 83), and
`APPLE80211_IOC_LINK_QUAL_EVENT_PARAMS` (IOC 84). Every public wrapper is a
direct 11-byte leaf that returns raw `0xe082280e` without reading its public
argument.

The recovered bytes are recorded in
`artifacts/skywalk-roam-cache-get-public-fixed-stubs-bootkc-current/`. They
come from the read-only current BootKC identity:

- outer SHA-256
  `eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d`;
- outer UUID `F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5`;
- embedded AirportItlwm UUID `8FB4B7F0-D656-3539-B8D6-C1327A50377C`;
- contiguous wrapper range
  `0xffffff80021bf1eb..0xffffff80021bf222` / `0x20bf1eb..0x20bf222`;
- raw fixed status `0xe082280e`.

## Implementation boundary

The implementation adds one compile-time Tahoe-only case group after the
existing VHT MCS case in the public Skywalk dispatcher. For `SIOCGA80211`, it
returns `0xe082280e` before any carrier access. Non-GET commands return
`kIOReturnUnsupported`.

The selectors remain absent from the pre-26 Skywalk route. No local carrier
contract is inferred for any member, and the card-specific route has no
ROAM/CACHE group entry. The legacy V1 `ROAM_THRESH` GET route remains separate
and unmodified. No public GET argument is inspected or used to construct a
carrier object.

## Explicit nonclaims

This evidence does not claim outer-null dispatch behavior, a carrier contract,
ROAM threshold behavior, vendor debug-flag behavior, cache-age behavior, PMK
cache behavior, link-quality event behavior, SET behavior, V1 ROAM behavior,
Virtual IOCTL, card-specific behavior, firmware, runtime-execution, radio,
association, traffic, or broader Tahoe behavior parity. No private carrier or
selector is constructed or invoked.

## Validation

`scripts/skywalk_public_roam_cache_get_fixed_stubs_alignment_report.py` checks
the checked-in raw manifest, selector identities, Tahoe-only source boundary,
preserved V1 ROAM GET text, inactive Virtual and card routes, and generated
state report. Build and runtime evidence are captured separately; runtime
control is passive only.
