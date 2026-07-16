# CR-565 — Skywalk public P2P_LISTEN GET fixed-stub alignment

## Scope

This change recovers only current 25C56 public Tahoe BSD GET behavior for
`APPLE80211_IOC_P2P_LISTEN` (IOC 92). The public wrapper is a direct 11-byte
leaf that returns raw `0xe082280e` without reading its public argument.

The recovered bytes are recorded in
`artifacts/skywalk-p2p-listen-get-public-fixed-stub-bootkc-current/` and come
from the read-only BootKC identity:

- outer SHA-256
  `eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d`;
- outer UUID `F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5`;
- embedded AirportItlwm UUID `8FB4B7F0-D656-3539-B8D6-C1327A50377C`;
- wrapper `0xffffff80021bf26f` / `0x20bf26f`, ending at
  `0xffffff80021bf27a` / `0x20bf27a`;
- raw fixed status `0xe082280e`.

## Implementation boundary

One compile-time Tahoe-only case is added after the existing SoftAP-params
case. For `SIOCGA80211`, it returns `0xe082280e` before carrier access.
Non-GET commands return `kIOReturnUnsupported`.

The Tahoe selector remains absent from the pre-26 Skywalk route. Legacy V1
and Virtual `P2P_LISTEN` SET routes remain separate and unmodified; the
card-specific route is untouched. No local GET carrier contract is inferred.
The public argument is neither inspected nor used to construct a carrier
object.

## Explicit nonclaims

This evidence does not claim outer-null dispatch behavior, a carrier contract,
P2P-listen behavior, SET behavior, P2P scan behavior, V1, Virtual IOCTL,
card-specific behavior, firmware, runtime-execution, radio, association,
traffic, or broader Tahoe behavior parity. No private carrier or selector is
constructed or invoked.

## Validation

`scripts/skywalk_public_p2p_listen_get_fixed_stub_alignment_report.py` checks
the raw manifest, selector identity, Tahoe-only source boundary, preserved V1
and Virtual SET text, inactive card route, and generated state report. Build
and runtime evidence are captured separately; runtime control is passive only.
