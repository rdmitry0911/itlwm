# CR-525 — Skywalk public CUR_PMK fixed-stub alignment

Date: 2026-07-15

## Scope

This correction covers only the normal non-null public Tahoe SET route for
`APPLE80211_IOC_CUR_PMK` (IOC 360) in
`AirportItlwmSkywalkInterface::processApple80211Ioctl`. It retains the GET
route, the outer/null fallback, the carrier declaration/layout, the virtual
`setCUR_PMK` helper, the `CIPHER_KEY` PMK path, and the PLTI
`DeliverPMK` user-client path.

The source uses a compile-time Tahoe-only guard
`__IO80211_TARGET >= __MAC_26_0`: the exact current-25C56 status applies only
to the Tahoe build, while the pre-26/Sonoma branch retains its existing call
to `setCUR_PMK`. This evidence does not establish a current public-wrapper
contract for those older target families.

It does not modify key material, the CIPHER_KEY PMK path, PLTI
`DeliverPMK`, association, radio, firmware, event, traffic, scan, AWDL, P2P,
carrier/ABI definitions, or user-client behavior. It does not claim
outer-null, GET, carrier/ABI, private virtual-route, PMK-owner, association,
firmware, runtime-execution, or broader Tahoe behavior parity.

## Current public reference evidence

The read-only current macOS 26.2 / 25C56 `BootKC_guest_25C56.kc` container
has SHA-256 `eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d`,
outer UUID `F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5`, and embedded Wi-Fi
`MH_KEXT` UUID `8FB4B7F0-D656-3539-B8D6-C1327A50377C`.

Direct nested-`LC_SYMTAB` recovery identifies external section-1 nlist 7112,
`__Z20apple80211setCUR_PMKP23IO80211SkywalkInterfaceP14apple80211_pmk`, at
half-open VM/file ranges `[0xffffff80021c700b, 0xffffff80021c7016)` and
`[0x20c700b, 0x20c7016)`. The next sorted external section symbol is nlist
7467,
`__Z26apple80211setDYNSAR_DETAILP23IO80211SkywalkInterfaceP24apple80211_dynsar_detail`,
at `0xffffff80021c7016`, so the recovered body is exactly 11 bytes:

```text
55 48 89 e5 b8 0e 28 82 e0 5d c3
```

It is an unread fixed `mov eax, 0xe082280e` return: no selector load, gate,
metacast, dynamic tail, virtual call, owner lookup, call, state mutation,
transport, or event operation occurs. The public SDK has no canonical local
name for that raw status, so it remains numeric and is not relabelled
`kIOReturnUnsupported`. The exact artifact and manifest are retained at
`docs/reference/artifacts/skywalk-cur-pmk-public-fixed-stub-bootkc-current/`.

## Local divergence and correction

Before this correction, the Tahoe public BSD/card-specific bridge admitted a
non-null IOC 360 SET and called `setCUR_PMK`, which read the carrier and
passed its PMK window to local PMK-install logic. The current reference public
wrapper instead performs neither read nor mutation and returns `0xe082280e`.

The normal non-null public SET branch now returns that exact numeric status
without reading the carrier or calling the helper, under the compile-time
Tahoe-only guard. The pre-26 branch remains unchanged. The existing global
null-carrier fallback stays before the main dispatcher switch and keeps its
previous unsupported result. `processBSDCommand` and
`AirportItlwm::handleCardSpecific` both treat a result other than
`kIOReturnUnsupported` as terminal, so this current raw status is terminal at
both public Tahoe ingress paths. The Tahoe card-specific route deliberately
continues to admit IOC 360; it now receives the same terminal public result.

The retained virtual helper is not deleted or redirected. It remains ABI and
private-route context only. `setCIPHER_KEY` still owns its separate local PMK
path, while PLTI `DeliverPMK` sends an `apple80211_key` directly to
`AirportItlwm::deliverExternalPMK` and does not call `setCUR_PMK`. This
current-reference supersession is limited to the directly recovered public
wrapper; it does not invalidate older version-specific virtual/private-route
evidence or assert that PMK behavior elsewhere is identical.

## Verification boundary

`scripts/skywalk_public_cur_pmk_fixed_stub_alignment_report.py --check`
verifies the raw artifact identity and manifest, exact public body, the
guarded Tahoe public route, retained pre-26 helper and GET route, unchanged
null fallback, both terminal ingress boundaries, retained Tahoe IOC 360
routing, separate CIPHER_KEY/PLTI paths, and all three active Skywalk source
phases.

No private carrier or selector is constructed or invoked. This layer makes no
deployment, radio, association, PMK traffic, AWDL, P2P, scan, firmware,
event, CCA, or runtime-execution claim.
