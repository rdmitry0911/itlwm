# CR-542 — Skywalk public AP_MODE GET fixed-stub alignment

Date: 2026-07-15

## Scope

This layer aligns only the current 25C56 public APPLE80211_IOC_AP_MODE
(IOC 26) Tahoe BSD GET direction in
AirportItlwmSkywalkInterface::processApple80211Ioctl.

The compile-time Tahoe-only GET branch returns the direct public-wrapper raw
status 0xe082280e before observing the existing AP-mode carrier. The public
leaf reads neither argument, so the branch applies to a non-null request object
without claiming outer-null dispatch behavior. The existing AP-mode SET helper,
its failure result, and pre-26 dispatch remain unchanged.

Historical V1 and Virtual IOCTL routes do not name IOC 26. The card-specific
route has no AP_MODE entry and continues to exclude it through
TahoeSkywalkIoctlRoutes::shouldRoute's false default.

## Direct current reference evidence

The read-only 25C56 BootKC container has SHA-256
eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d,
outer UUID F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5, and embedded Wi-Fi KEXT UUID
8FB4B7F0-D656-3539-B8D6-C1327A50377C.

Direct nested LC_SYMTAB recovery identifies the public getter
apple80211getAP_MODE at nlist 7088 in the half-open VM range
0xffffff80021be94d..0xffffff80021be958, file range
0x20be94d..0x20be958. It is an N_SECT|N_EXT record (type 0x0f, section 1,
desc 0x0000); the next symbol starts at the recorded end.

The exact 11-byte body is:

    55 48 89 e5 b8 0e 28 82 e0 5d c3

It reads neither public argument and performs no gate, owner lookup, call,
state, transport, or event operation before returning raw 0xe082280e.
The full file offsets, adjacent symbol boundary, raw bytes, and hash are
preserved under
docs/reference/artifacts/skywalk-ap-mode-get-public-fixed-stub-bootkc-current/.

## Boundary and non-claims

This is an exact, narrow public GET status alignment. It does not claim
outer-null dispatch behavior, the AP-mode carrier ABI, SET AP-mode behavior,
AP state, V1, Virtual IOCTL, card-specific behavior, firmware,
runtime-execution, radio, association, traffic, or broader Tahoe behavior
parity.

No private carrier or selector is constructed or invoked. No deployment,
radio change, association, traffic, or runtime selector execution is part of
this layer.

## Verification

scripts/skywalk_public_ap_mode_get_fixed_stub_alignment_report.py --check
checks the direct raw record and manifest, the exact Tahoe-only GET branch,
preserved SET/pre-26 boundaries, separated V1/Virtual/card seams, the existing
selector/carrier declarations without GET carrier access, and active Tahoe
source-phase markers.
