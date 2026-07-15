# CR-540 — Skywalk public IBSS_MODE GET fixed-stub alignment

Date: 2026-07-15

## Scope

This layer aligns only the current 25C56 public APPLE80211_IOC_IBSS_MODE
(IOC 24) normal non-null Tahoe BSD GET direction in
AirportItlwmSkywalkInterface::processApple80211Ioctl.

The compile-time Tahoe-only GET branch returns the direct public-wrapper raw
status 0xe082280e before observing the carrier. The existing generic
req_data-null fallback precedes this second dispatch switch and remains
unchanged, so this layer intentionally does not claim null-carrier behavior.
The existing IBSS SET producer, non-GET unsupported outcome, and pre-26
dispatch remain unchanged.

No historical V1 route, Virtual IOCTL route, or card-specific route names IOC
24. TahoeSkywalkIoctlRoutes::shouldRoute admits no IBSS_MODE entry and
therefore continues to exclude it through its false default.

## Direct current reference evidence

The read-only 25C56 BootKC container has SHA-256
eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d,
outer UUID F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5, and embedded Wi-Fi KEXT UUID
8FB4B7F0-D656-3539-B8D6-C1327A50377C.

Direct nested LC_SYMTAB recovery identifies the public getter
apple80211getIBSS_MODE at nlist 7191 in the half-open VM range
0xffffff80021be937..0xffffff80021be942, file range
0x20be937..0x20be942. It is an N_SECT|N_EXT record (type 0x0f, section 1,
desc 0x0000); the next symbol starts at the recorded end.

The exact 11-byte body is:

    55 48 89 e5 b8 0e 28 82 e0 5d c3

It reads neither public argument and performs no gate, owner lookup, call,
state, transport, or event operation before returning raw 0xe082280e.
The full file offsets, adjacent symbol boundary, raw bytes, and hash are
preserved under
docs/reference/artifacts/skywalk-ibss-mode-get-public-fixed-stub-bootkc-current/.

## Boundary and non-claims

This is an exact, narrow public GET status alignment. It does not claim
outer-null or null-carrier behavior, the IBSS network carrier ABI, SET IBSS
behavior, V1, Virtual IOCTL, card-specific behavior, ad-hoc/proximity/NAN
lifecycle, firmware, runtime-execution, radio, association, traffic, or
broader Tahoe behavior parity.

No private carrier or selector is constructed or invoked. No deployment,
radio change, association, traffic, or runtime selector execution is part of
this layer.

## Verification

scripts/skywalk_public_ibss_mode_get_fixed_stub_alignment_report.py --check
checks the direct raw record and manifest, the exact Tahoe-only GET branch,
the preserved SET producer and generic null-carrier/pre-26 boundaries,
separated V1/Virtual/card seams, the existing selector and network carrier
declarations without carrier access in the GET branch, and active Tahoe
source-phase markers.
