# CR-479 Tahoe Legacy BTCOEX Direct-Gate Alignment

Date: 2026-07-14

## Scope

This closes only the Tahoe V2/Skywalk public bridge for the four legacy
BTCOEX selector families:

- `APPLE80211_IOC_BTCOEX_MODE` (`87`)
- `APPLE80211_IOC_BTCOEX_PROFILES` (`221`)
- `APPLE80211_IOC_BTCOEX_CONFIG` (`222`)
- `APPLE80211_IOC_BTCOEX_OPTIONS` (`235`)

`AirportItlwmSkywalkInterface::processApple80211Ioctl()` is the active Tahoe
bridge. `AirportSTAIOCTL.cpp` is deliberately outside this correction: the
reference below is a Tahoe/26.3 IO80211Family image and does not establish
the legacy V1 target's behavior.

## Reference

Reference binary:

```text
IO80211Family.kext/Contents/MacOS/IO80211Family
bundle: com.apple.iokit.IO80211Family 1200.13.1
DTPlatformVersion: 26.3
SHA-256: 77fad8c22845b5ba2c5d808134f0dccd2cc5d42f006410aa89f7448360b72672
```

The x86_64 disassembly is captured in
`docs/reference/artifacts/legacy-btcoex-direct-gate-26.3/raw.txt`. Each
wrapper is a direct leaf: it does not read the carrier, dispatch a vtable, or
issue a firmware command; it returns raw `0xe082280e`.

| Direction | MODE | PROFILES | CONFIG | OPTIONS |
| --- | --- | --- | --- | --- |
| get | `0xdc5e6` | `0xdd502` | `0xdd50d` | `0xdd6c4` |
| set | `0xe1388` | `0xe23dc` | `0xe23e7` | `0xe2632` |

At every listed address the decisive instruction is
`mov eax, 0xe082280e`, immediately followed by function epilogue and `ret`.

## Local mismatch and correction

Before this correction, the active Tahoe bridge accepted both directions for
all four selectors, copied each request into controller-local
`btcProfile`/`btcConfig`/`btcOptions`/`btcMode` state, and returned success.
Those fields had no HAL consumer, IOVAR transport, or other producer; a later
getter merely replayed the synthetic cache.

The bridge now returns the exact raw class-owner-absent status
`0xe082280e` before inspecting `instance` or `req_data`, matching the direct
reference leaf. The four V2-only synthetic fields and their profile cleanup
are removed. The selectors remain in `TahoeSkywalkIoctlRoutes` so valid
card-specific requests still reach that exact gate rather than falling into
an unrelated default path.

## Validation and non-claims

`scripts/legacy_btcoex_direct_gate_alignment_report.py` verifies the eight
reference offsets, the direct local gate, continued Tahoe routing, removal of
the V2 cache fields, and the explicitly bounded V2 scope.

This does not claim a BT coexistence backend, firmware IOVAR, dynamic profile
state, or runtime invocation of these selectors. It does not alter the
separate modern `BTCOEX_PROFILE*` selector family, nor the older V1 target.
