# CR-527 — Skywalk public VIRTUAL_IF_ROLE/PARENT fixed-stub alignment

Date: 2026-07-15

## Scope

This correction covers only normal non-null public Tahoe GET/SET routes for
`APPLE80211_IOC_VIRTUAL_IF_ROLE` (IOC 96) and
`APPLE80211_IOC_VIRTUAL_IF_PARENT` (IOC 97) in
`AirportItlwmSkywalkInterface::processApple80211Ioctl`. It retains all
selector numbers and carrier declarations, `req == NULL` handling, each
existing null and unknown-command fallback, the card-specific route table,
all V1 code, and all pre-26 behavior.

The source uses a compile-time Tahoe-only guard
`__IO80211_TARGET >= __MAC_26_0`: the exact current-25C56 statuses apply only
to the Tahoe build, while the pre-26/Sonoma branch retains its current
role/parent logic. The recovered symbols carry only unread `void *` arguments;
they establish no carrier ABI or private-route contract.

It does not modify virtual-interface allocation, APSTA ownership, parent
topology, association, radio, firmware, event, traffic, AWDL, P2P, scan,
legacy V1 code, or user-client behavior. It does not claim null, carrier/ABI,
private virtual-route, card-specific contract, V1, pre-26, association,
firmware, runtime-execution, or broader Tahoe behavior parity.

## Current public reference evidence

The read-only current macOS 26.2 / 25C56 `BootKC_guest_25C56.kc` container
has SHA-256 `eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d`,
outer UUID `F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5`, and embedded Wi-Fi
`MH_KEXT` UUID `8FB4B7F0-D656-3539-B8D6-C1327A50377C`.

Direct nested-`LC_SYMTAB` recovery identifies four external section-1,
`n_type=0x0f`, `n_desc=0x0000` public wrappers. Each is bounded by its next
sorted external section symbol and has exactly the same 11-byte body:

| Wrapper | nlist | VM range | File range | Next nlist |
| --- | ---: | --- | --- | ---: |
| `getVIRTUAL_IF_ROLE` | 7598 | `[0xffffff80021bf29b, 0xffffff80021bf2a6)` | `[0x20bf29b, 0x20bf2a6)` | 7726 |
| `getVIRTUAL_IF_PARENT` | 7726 | `[0xffffff80021bf2a6, 0xffffff80021bf2b1)` | `[0x20bf2a6, 0x20bf2b1)` | 7309 |
| `setVIRTUAL_IF_ROLE` | 7635 | `[0xffffff80021c4165, 0xffffff80021c4170)` | `[0x20c4165, 0x20c4170)` | 7757 |
| `setVIRTUAL_IF_PARENT` | 7757 | `[0xffffff80021c4170, 0xffffff80021c417b)` | `[0x20c4170, 0x20c417b)` | 7344 |

```text
55 48 89 e5 b8 0e 28 82 e0 5d c3
```

Every body is an unread fixed `mov eax, 0xe082280e` return: no selector load,
gate, metacast, dynamic tail, owner lookup, call, state mutation, transport,
or event operation occurs. The public SDK has no canonical local name for that
raw status, so it remains numeric and is not relabelled
`kIOReturnUnsupported`. The exact artifact and manifest are retained at
`docs/reference/artifacts/skywalk-virtual-if-role-parent-public-fixed-stub-bootkc-current/`.

## Local divergence and correction

Before this correction, non-null Tahoe ROLE GET wrote `getInterfaceRole()` and
returned success; non-null PARENT GET read the primary interface/BSD name and
returned success. The SET directions returned generic unsupported before those
paths. All four current reference public wrappers instead read neither
argument and return `0xe082280e`.

The two normal non-null Tahoe switch branches now return that exact numeric
status for public GET or SET before any role, parent, name, length, or carrier
operation. Their existing null and unknown-command fallbacks remain after the
guard, as does all pre-26 code. `processBSDCommand` treats a result other than
`kIOReturnUnsupported` as terminal, so normal BSD requests receive the raw
status.

`TahoeSkywalkIoctlRoutes` is unchanged: it still admits IOC 96/97 only when
`!isSet`, so card-specific SET remains excluded before the shared dispatcher.
An already-admitted non-null card-specific GET mechanically shares the same
local dispatcher result, but this evidence makes no independent card-specific
contract claim.

## Verification boundary

`scripts/skywalk_public_virtual_if_role_parent_fixed_stub_alignment_report.py
--check` verifies the four raw wrapper identities and manifest, guarded Tahoe
GET/SET branches, retained null/unknown/pre-26 behavior, BSD terminal edge,
unchanged card-specific policy and canonical GET lengths, and all active
Skywalk source phases.

No private carrier or selector is constructed or invoked. This layer makes no
deployment, radio, association, APSTA, parent-topology, AWDL, P2P, scan,
firmware, event, traffic, CCA, or runtime-execution claim.
