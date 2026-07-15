# CR-523 — Skywalk public AUTH_TYPE fixed-stub alignment

Date: 2026-07-15

## Scope

This correction covers only the normal non-null public Tahoe SET route for
`APPLE80211_IOC_AUTH_TYPE` (IOC 2) in
`AirportItlwmSkywalkInterface::processApple80211Ioctl`. It retains the
existing GET route, the outer/null fallback, all selector numbers and carrier
declarations, the internal `setAUTH_TYPE` helper, and both association paths
which call that helper directly.

The source uses a compile-time Tahoe-only guard
`__IO80211_TARGET >= __MAC_26_0`: the exact current-25C56 status applies only
to the Tahoe build, while the pre-26/Sonoma branch retains its existing call
to `setAUTH_TYPE`. This evidence does not establish a current public-wrapper
contract for those older target families.

It does not modify association, radio, firmware, event, traffic, APSTA,
AWDL, P2P, scan, CCA, BSS-manager seeding, legacy V1 code, or user-client
behavior. It does not claim outer-null, carrier/ABI, GET, association,
firmware, runtime-execution, or broader Tahoe behavior parity.

## Current public reference evidence

The read-only current macOS 26.2 / 25C56 `BootKC_guest_25C56.kc` container
has SHA-256 `eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d`,
outer UUID `F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5`, and embedded Wi-Fi
`MH_KEXT` UUID `8FB4B7F0-D656-3539-B8D6-C1327A50377C`.

Direct nested-`LC_SYMTAB` recovery identifies external section-1 nlist 7204,
`__Z22apple80211setAUTH_TYPEP23IO80211SkywalkInterfaceP24apple80211_authtype_data`,
at half-open VM/file ranges `[0xffffff80021c3520, 0xffffff80021c352b)` and
`[0x20c3520, 0x20c352b)`. The next sorted external section symbol is nlist
7261, `__Z23apple80211setCIPHER_KEYP23IO80211SkywalkInterfaceP14apple80211_key`,
at `0xffffff80021c352b`, so the recovered body is exactly 11 bytes:

```text
55 48 89 e5 b8 0e 28 82 e0 5d c3
```

It is an unread fixed `mov eax, 0xe082280e` return: no selector load, gate,
metacast, dynamic tail, owner lookup, call, state mutation, transport, or
event operation occurs. The public SDK has no canonical local name for that
raw status, so it remains numeric and is not relabelled
`kIOReturnUnsupported`. The existing exact artifact and manifest are retained
at
`docs/reference/artifacts/legacy-auth-type-public-fixed-stub-bootkc-current/`.

## Local divergence and correction

Before this correction, the Tahoe public BSD/card-specific bridge admitted a
non-null IOC 2 SET and called `setAUTH_TYPE`, which updated the Skywalk
auth-type cache and returned success. The reference public wrapper instead
performs neither read nor mutation and returns `0xe082280e`.

The normal non-null public SET branch now returns that exact numeric status
without reading the carrier or calling the helper, under the compile-time
Tahoe-only guard. The pre-26 branch is unchanged. The existing null-carrier
guard remains before the dispatch switch and keeps its previous unsupported
fallback. `processBSDCommand` and `handleCardSpecific` both treat any result
other than `kIOReturnUnsupported` as terminal, so this branch is terminal at
both public Tahoe ingress paths.

The internal helper remains deliberately unchanged. `setASSOCIATE` and
`setWCL_ASSOCIATE` call it directly to seed local association/BSS-manager
context; they bypass this public dispatcher branch. This is direct
non-null-public fixed-stub alignment, not an inference that the helper or
association paths should return the public wrapper status.

## Verification boundary

`scripts/skywalk_public_auth_type_fixed_stub_alignment_report.py --check`
verifies the reused raw artifact identity and manifest, fixed public body,
normal public route terminal status, unchanged null fallback, both ingress
terminal boundaries, preserved GET/helper/internal association calls, and the
Tahoe source phase.

No private carrier or selector is constructed or invoked. This layer makes no
deployment, radio, association, APSTA, AWDL, P2P, scan, firmware, event,
traffic, CCA, or runtime-execution claim.
