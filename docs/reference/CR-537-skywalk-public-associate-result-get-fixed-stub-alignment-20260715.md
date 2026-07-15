# CR-537 — Skywalk public ASSOCIATE_RESULT GET fixed-stub alignment

Date: 2026-07-15

## Scope

This layer aligns only the current 25C56 public
APPLE80211_IOC_ASSOCIATE_RESULT (IOC 21) normal non-null Tahoe BSD GET
direction in AirportItlwmSkywalkInterface::processApple80211Ioctl.

The compile-time Tahoe-only GET case returns the direct public-wrapper raw
status 0xe082280e before observing the carrier. The existing outer-null and
inner null-carrier fallbacks, non-GET unsupported outcome, and pre-26 dispatch
remain unchanged.

It does not modify the historical V1 GET-only association-result producer, the
Virtual IOCTL surface, the card-specific route, the association-result carrier
ABI, association execution, radio, firmware, event, or traffic path. The
card-specific route remains excluded: IOC 21 is not admitted by
TahoeSkywalkIoctlRoutes::shouldRoute.

## Direct current reference evidence

The read-only 25C56 BootKC container has SHA-256
eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d,
outer UUID F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5, and embedded Wi-Fi KEXT UUID
8FB4B7F0-D656-3539-B8D6-C1327A50377C.

Direct nested LC_SYMTAB recovery identifies the public getter
apple80211getASSOCIATE_RESULT at nlist 7643 in the half-open VM range
0xffffff80021be916..0xffffff80021be921, file range
0x20be916..0x20be921. It is an N_SECT|N_EXT record (type 0x0f, section 1,
desc 0x0000); the next symbol starts at the recorded end.

The exact 11-byte body is:

    55 48 89 e5 b8 0e 28 82 e0 5d c3

It reads neither public argument and performs no gate, owner lookup, call,
state, transport, or event operation before returning raw 0xe082280e.
The full file offsets, adjacent symbol boundary, raw bytes, and hash are
preserved under
docs/reference/artifacts/skywalk-associate-result-get-public-fixed-stub-bootkc-current/.

## Boundary and non-claims

This is an exact, narrow public GET status alignment. It does not claim
outer-null, carrier/ABI, V1 association-result producer, Virtual IOCTL,
card-specific, association execution, firmware, runtime-execution, or broader
Tahoe behavior parity.

No private carrier or selector is constructed or invoked. No deployment,
radio change, association, traffic, or runtime selector execution is part of
this layer.

## Verification

scripts/skywalk_public_associate_result_get_fixed_stub_alignment_report.py
--check checks the direct raw record and manifest, the exact Tahoe-only GET
case, unchanged null/pre-26 boundaries, preserved V1/Virtual/card seams,
selector ABI declaration, and active Tahoe source-phase markers.
