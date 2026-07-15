# CR-524 — Skywalk public VIRTUAL_IF_DELETE fixed-stub alignment

Date: 2026-07-15

## Scope

This correction covers only the normal non-null public Tahoe SET route for
`APPLE80211_IOC_VIRTUAL_IF_DELETE` (IOC 95) in
`AirportItlwmSkywalkInterface::processApple80211Ioctl`. It retains the GET
fallback, the outer/null fallback, all selector numbers and carrier
declarations, the Skywalk helper, the legacy V1/controller route, and
controller-owned APSTA cleanup used by release and failed-create paths.

The source uses a compile-time Tahoe-only guard
`__IO80211_TARGET >= __MAC_26_0`: the exact current-25C56 status applies only
to the Tahoe build, while the pre-26/Sonoma branch retains its existing call
to `setVIRTUAL_IF_DELETE`. This evidence does not establish a current
public-wrapper contract for those older target families.

It does not modify APSTA allocation, owner cleanup, release, failed-create
cleanup, association, radio, firmware, event, traffic, scan, AWDL, P2P,
carrier/ABI definitions, or user-client behavior. It does not claim
outer-null, GET, carrier/ABI, APSTA, owner-lifetime, firmware,
runtime-execution, or broader Tahoe behavior parity.

## Current public reference evidence

The read-only current macOS 26.2 / 25C56 `BootKC_guest_25C56.kc` container
has SHA-256 `eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d`,
outer UUID `F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5`, and embedded Wi-Fi
`MH_KEXT` UUID `8FB4B7F0-D656-3539-B8D6-C1327A50377C`.

Direct nested-`LC_SYMTAB` recovery identifies external section-1 nlist 7756,
`__Z30apple80211setVIRTUAL_IF_DELETEP23IO80211SkywalkInterfacePv`, at
half-open VM/file ranges `[0xffffff80021c415a, 0xffffff80021c4165)` and
`[0x20c415a, 0x20c4165)`. The next sorted external section symbol is nlist
7635, `__Z28apple80211setVIRTUAL_IF_ROLEP23IO80211SkywalkInterfacePv`, at
`0xffffff80021c4165`, so the recovered body is exactly 11 bytes:

```text
55 48 89 e5 b8 0e 28 82 e0 5d c3
```

It is an unread fixed `mov eax, 0xe082280e` return: no selector load, gate,
metacast, dynamic tail, owner lookup, call, state mutation, transport, or
event operation occurs. The public SDK has no canonical local name for that
raw status, so it remains numeric and is not relabelled
`kIOReturnUnsupported`. The exact artifact and manifest are retained at
`docs/reference/artifacts/skywalk-virtual-if-delete-public-fixed-stub-bootkc-current/`.

## Local divergence and correction

Before this correction, the Tahoe public BSD bridge admitted a non-null IOC
95 SET and called `setVIRTUAL_IF_DELETE`. That helper forwarded the carrier's
BSD name to `deleteAPSTAOwnerForBSDName`, which could release a matching
controller-owned role-7 APSTA owner and return success. The current reference
public wrapper instead performs neither read nor mutation and returns
`0xe082280e`.

The normal non-null public SET branch now returns that exact numeric status
without reading the carrier or calling the helper, under the compile-time
Tahoe-only guard. The pre-26 branch remains unchanged. The existing global
null-carrier fallback stays before the main dispatcher switch and keeps its
previous unsupported result. `processBSDCommand` treats any result other than
`kIOReturnUnsupported` as terminal, so the raw current status is terminal at
the public BSD Tahoe ingress.

IOC 95 is absent from `TahoeSkywalkIoctlRoutes::shouldRoute`, so the Tahoe
card-specific route does not reach this case. The helper, the legacy
controller path, and `deleteAPSTAOwner` / `deleteAPSTAOwnerForBSDName` remain
for their separate lifecycle responsibilities. This current-reference
supersession does not remove APSTA cleanup or infer that APSTA behavior is
otherwise identical.

## Verification boundary

`scripts/skywalk_public_virtual_if_delete_fixed_stub_alignment_report.py
--check` verifies the raw artifact identity and manifest, exact public body,
the guarded Tahoe public route, retained pre-26 helper route, unchanged null
fallback and BSD terminal boundary, absent card-specific IOC 95 route,
preserved helper/controller/V1 cleanup topology, and all three active
Skywalk source phases.

No private carrier or selector is constructed or invoked. This layer makes no
deployment, radio, association, APSTA, AWDL, P2P, scan, firmware, event,
traffic, CCA, or runtime-execution claim.
