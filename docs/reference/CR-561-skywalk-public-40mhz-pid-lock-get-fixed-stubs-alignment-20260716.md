# CR-561 — Skywalk public 40MHz/PID-lock GET fixed-stub alignment

## Scope

This change recovers only the current 25C56 public Tahoe BSD GET behavior for
`APPLE80211_IOC_40MHZ_INTOLERANT` (IOC 71) and
`APPLE80211_IOC_PID_LOCK` (IOC 72).  Both public wrappers are direct 11-byte
leaves that return the raw numeric `0xe082280e` without reading their public
argument.

The recovered bytes are recorded in
`artifacts/skywalk-40mhz-pid-lock-get-public-fixed-stubs-bootkc-current/`.
They come from the read-only current BootKC identity:

- outer SHA-256
  `eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d`;
- outer UUID `F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5`;
- embedded AirportItlwm UUID `8FB4B7F0-D656-3539-B8D6-C1327A50377C`;
- contiguous wrapper range
  `0xffffff80021bf08f..0xffffff80021bf0a5` / `0x20bf08f..0x20bf0a5`;
- raw fixed status `0xe082280e`.

## Implementation boundary

The implementation adds one compile-time Tahoe-only case group after the
existing `POWER` case in the public Skywalk dispatcher.  For `SIOCGA80211`, it
returns `0xe082280e` before any carrier access.  Non-GET commands return
`kIOReturnUnsupported`.

The selectors remain absent from the pre-26 switch.  No local carrier contract
is inferred for either member, and the card-specific route has no
40MHz/PID-lock entry.  The public argument is neither inspected nor used to
construct a carrier object.

## Explicit nonclaims

This evidence does not claim outer-null dispatch behavior, a carrier contract,
SET behavior, 40MHz-intolerant behavior, PID-lock behavior, V1, Virtual IOCTL,
card-specific behavior, firmware, runtime-execution, radio, association,
traffic, or broader Tahoe behavior parity.  No private carrier or selector is
constructed or invoked.

## Validation

`scripts/skywalk_public_40mhz_pid_lock_get_fixed_stubs_alignment_report.py`
checks the checked-in raw manifest, selector identities, Tahoe-only source
boundary, inactive legacy routes, and generated state report.  Build and
runtime evidence are captured separately; runtime control is passive only.
