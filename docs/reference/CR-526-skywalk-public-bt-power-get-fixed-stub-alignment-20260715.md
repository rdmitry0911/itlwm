# CR-526 — Skywalk public BT_POWER GET fixed-stub alignment

Date: 2026-07-15

## Scope

This correction covers only the normal non-null public Tahoe BSD GET route for
`APPLE80211_IOC_BT_POWER` (IOC 104) in
`AirportItlwmSkywalkInterface::processApple80211Ioctl`. It retains the SET
branch, the outer/null fallback, all selector and carrier declarations, the
card-specific route table, all V1 code, and all pre-26 behavior.

The source uses a compile-time Tahoe-only guard
`__IO80211_TARGET >= __MAC_26_0`: the exact current-25C56 status applies only
to the Tahoe build, while the pre-26/Sonoma branch retains its prior generic
unsupported GET fallback. This evidence does not establish a current
public-wrapper contract for older target families, V1, private handlers, or
DEXT transport paths.

It does not modify Bluetooth state, association, radio, firmware, event,
traffic, APSTA, AWDL, P2P, scan, CCA, legacy V1 code, or user-client behavior.
It does not claim outer-null, carrier/ABI, SET, card-specific GET,
association, firmware, runtime-execution, or broader Tahoe behavior parity.

## Current public reference evidence

The read-only current macOS 26.2 / 25C56 `BootKC_guest_25C56.kc` container
has SHA-256 `eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d`,
outer UUID `F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5`, and embedded Wi-Fi
`MH_KEXT` UUID `8FB4B7F0-D656-3539-B8D6-C1327A50377C`.

Direct nested-`LC_SYMTAB` recovery identifies external section-1 nlist 7138,
`__Z21apple80211getBT_POWERP23IO80211SkywalkInterfacePv`, at half-open
VM/file ranges `[0xffffff80021bf391, 0xffffff80021bf39c)` and
`[0x20bf391, 0x20bf39c)`. The next sorted external section symbol is nlist
7366, `__Z25apple80211getAVAILABILITYP23IO80211SkywalkInterfacePv`, at
`0xffffff80021bf39c`, so the recovered body is exactly 11 bytes:

```text
55 48 89 e5 b8 0e 28 82 e0 5d c3
```

It is an unread fixed `mov eax, 0xe082280e` return: no selector load, gate,
metacast, dynamic tail, owner lookup, call, state mutation, transport, or
event operation occurs. The public SDK has no canonical local name for that
raw status, so it remains numeric and is not relabelled
`kIOReturnUnsupported`. The exact artifact and manifest are retained at
`docs/reference/artifacts/skywalk-bt-power-get-public-fixed-stub-bootkc-current/`.

## Local divergence and correction

Before this correction, the normal non-null Tahoe public BSD GET bridge
admitted IOC 104 but its Skywalk dispatcher fell through to generic
`kIOReturnUnsupported`. The current reference public wrapper instead reads no
carrier and returns `0xe082280e`.

The normal non-null public Tahoe GET branch now returns that exact numeric
status before reading a carrier, under the compile-time Tahoe-only guard. The
SET branch is unchanged, and the global null-carrier fallback remains before
this route and keeps its previous unsupported result. `processBSDCommand`
treats any result other than `kIOReturnUnsupported` as terminal, so the raw
current status terminates the BSD path. `TahoeSkywalkIoctlRoutes::kIocBtPower`
continues to admit only SET; card-specific GET remains excluded before the
shared dispatcher and is outside this claim.

`CR-479` contained older list-backed BT-power observations at a different
reference location. This current-reference supersession is limited to the
directly recovered public Skywalk GET wrapper; it does not turn that older
observation into a claim about SET, DEXT, virtual/private routes, or broader
Bluetooth coexistence behavior.

## Verification boundary

`scripts/skywalk_public_bt_power_get_fixed_stub_alignment_report.py --check`
verifies the raw artifact identity and manifest, exact unread public body,
guarded Tahoe GET branch, preserved SET/pre-26/null boundaries, BSD ingress
terminal behavior, unchanged card-specific GET exclusion, and all active
Skywalk source phases.

No private carrier or selector is constructed or invoked. This layer makes no
deployment, radio, Bluetooth, association, traffic, AWDL, P2P, scan,
firmware, event, CCA, or runtime-execution claim.
