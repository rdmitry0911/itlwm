# CR-533 — Skywalk public OP_MODE SET fixed-stub alignment

Date: 2026-07-15

## Scope

This layer aligns only the current 25C56 public
APPLE80211_IOC_OP_MODE (IOC 15) normal non-null Tahoe BSD SET direction in
AirportItlwmSkywalkInterface::processApple80211Ioctl.

The compile-time Tahoe-only guard returns the direct public-wrapper raw status
0xe082280e before observing the carrier. The existing dynamic GET producer,
global outer-null and inner null-carrier fallbacks, pre-26 behavior, and
unknown-command unsupported/super fallback remain unchanged.

It does not modify historical V1 or Virtual IOCTL GET-only routes, the
card-specific route, the OP_MODE carrier ABI, APSTA/core GET ownership,
association, radio, firmware, event, or traffic path. The card-specific route
remains excluded: IOC 15 is not admitted by TahoeSkywalkIoctlRoutes::shouldRoute.

## Direct current reference evidence

The read-only 25C56 BootKC container has SHA-256
eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d,
outer UUID F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5, and embedded Wi-Fi KEXT UUID
8FB4B7F0-D656-3539-B8D6-C1327A50377C.

Direct nested LC_SYMTAB recovery identifies the public setter
apple80211setOP_MODE at nlist 7122 in the half-open VM range
0xffffff80021c37ba..0xffffff80021c37c5, file range
0x20c37ba..0x20c37c5. It is an N_SECT|N_EXT record (type 0x0f, section 1,
desc 0x0000); the next symbol starts at the recorded end.

The exact 11-byte body is:

    55 48 89 e5 b8 0e 28 82 e0 5d c3

It reads neither public argument and performs no gate, owner lookup, call,
state, transport, or event operation before returning raw 0xe082280e.
The full file offsets, adjacent symbol boundary, raw bytes, and hash are
preserved under
docs/reference/artifacts/skywalk-op-mode-set-public-fixed-stub-bootkc-current/.

The current public GET wrapper is a separate 112-byte gated/vtable producer,
so it is intentionally not aligned by this layer.

## Boundary and non-claims

This is an exact, narrow public SET status alignment. It does not claim
outer-null, carrier/ABI, V1, Virtual IOCTL, card-specific, GET/APSTA/core
behavior, association, firmware, runtime-execution, or broader Tahoe behavior
parity.

No private carrier or selector is constructed or invoked. No deployment,
radio change, association, traffic, or runtime selector execution is part of
this layer.

## Verification

scripts/skywalk_public_op_mode_set_fixed_stub_alignment_report.py --check
checks the direct raw record and manifest, the exact guarded SET branch, the
unchanged dynamic GET and fallback behavior, preserved V1/Virtual/card
boundaries, selector ABI declaration, and active Tahoe source-phase markers.
