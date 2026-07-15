# CR-528 — Skywalk public CIPHER_KEY GET fixed-stub alignment

Date: 2026-07-15

## Scope

This correction covers only the normal non-null public Tahoe BSD GET route for
`APPLE80211_IOC_CIPHER_KEY` (IOC 3) in
`AirportItlwmSkywalkInterface::processApple80211Ioctl`. It retains the SET
branch, the outer/null fallback, all selector and carrier declarations, the
card-specific route table, all V1 code, and all pre-26 behavior.

The source uses a compile-time Tahoe-only guard
`__IO80211_TARGET >= __MAC_26_0`: the exact current-25C56 status applies only
to the Tahoe build, while the pre-26/Sonoma branch retains its prior generic
unsupported GET fallback. The direct public symbol carries an unread
`apple80211_key *`; it establishes no carrier/ABI, key material, PMK, or
private-route contract.

It does not modify key material, PMK ownership, CIPHER_KEY SET, CUR_PMK,
association, radio, firmware, event, traffic, APSTA, AWDL, P2P, scan, legacy
V1 code, or user-client behavior. It does not claim outer-null, carrier/ABI,
SET, card-specific GET, private key route, PMK, association, firmware,
runtime-execution, or broader Tahoe behavior parity.

## Current public reference evidence

The read-only current macOS 26.2 / 25C56 `BootKC_guest_25C56.kc` container
has SHA-256 `eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d`,
outer UUID `F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5`, and embedded Wi-Fi
`MH_KEXT` UUID `8FB4B7F0-D656-3539-B8D6-C1327A50377C`.

Direct nested-`LC_SYMTAB` recovery identifies external section-1 nlist 7234,
`__Z23apple80211getCIPHER_KEYP23IO80211SkywalkInterfaceP14apple80211_key`,
at half-open VM/file ranges `[0xffffff80021be3c6, 0xffffff80021be3d1)` and
`[0x20be3c6, 0x20be3d1)`. The next sorted external section symbol is nlist
7090, `__Z20apple80211getCHANNELP23IO80211SkywalkInterfaceP23apple80211_channel_data`,
at `0xffffff80021be3d1`, so the recovered body is exactly 11 bytes:

```text
55 48 89 e5 b8 0e 28 82 e0 5d c3
```

It is an unread fixed `mov eax, 0xe082280e` return: no selector load, gate,
metacast, dynamic tail, owner lookup, call, state mutation, transport, event,
key, or PMK operation occurs. The public SDK has no canonical local name for
that raw status, so it remains numeric and is not relabelled
`kIOReturnUnsupported`. The exact artifact and manifest are retained at
`docs/reference/artifacts/skywalk-cipher-key-get-public-fixed-stub-bootkc-current/`.

## Local divergence and correction

Before this correction, the normal non-null Tahoe public BSD GET bridge
admitted IOC 3 but its Skywalk dispatcher fell through to generic
`kIOReturnUnsupported`. The current reference public wrapper instead reads no
carrier and returns `0xe082280e`.

The normal non-null public Tahoe GET branch now returns that exact numeric
status before reading a carrier, under the compile-time Tahoe-only guard. The
SET branch is unchanged: it still selects the APSTA owner when present or the
local `setCIPHER_KEY` helper otherwise. The global null-carrier fallback
remains before this route and keeps its previous unsupported result.
`processBSDCommand` treats any result other than `kIOReturnUnsupported` as
terminal, so the raw current status terminates the BSD path.

`TahoeSkywalkIoctlRoutes::kIocCipherKey` continues to admit only SET;
card-specific GET remains excluded before the shared dispatcher and is outside
this claim. The V1 dispatcher likewise retains its `IOCTL_SET` route. No key
carrier is constructed, read, logged, delivered, or invoked by this layer.

## Verification boundary

`scripts/skywalk_public_cipher_key_get_fixed_stub_alignment_report.py
--check` verifies the raw artifact identity and manifest, exact unread public
body, guarded Tahoe GET branch, preserved SET/APSTA/PMK and pre-26/null/V1
boundaries, BSD terminal behavior, unchanged card-specific GET exclusion, and
all active Skywalk source phases.

No private carrier or selector is constructed or invoked. This layer makes no
deployment, radio, association, key/PMK delivery, traffic, AWDL, P2P, scan,
firmware, event, CCA, or runtime-execution claim.
