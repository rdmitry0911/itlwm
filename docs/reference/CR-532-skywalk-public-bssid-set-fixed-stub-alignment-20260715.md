# CR-532 — Skywalk public BSSID SET fixed-stub alignment

Date: 2026-07-15

## Scope

This layer aligns only the current 25C56 public
APPLE80211_IOC_BSSID (IOC 9) normal non-null Tahoe BSD SET direction in
AirportItlwmSkywalkInterface::processApple80211Ioctl.

The compile-time Tahoe-only guard returns the direct public-wrapper raw status
0xe082280e before observing the carrier. The existing bootstrap-oriented GET
producer, global outer-null and inner null-carrier fallbacks, pre-26 behavior,
and unknown-command unsupported/super fallback remain unchanged.

It does not modify the historical V1 bidirectional route, the card-specific
GET-only route, the BSSID carrier ABI, bootstrap GET cache behavior,
association, radio, firmware, event, or traffic path. The card-specific SET
route remains excluded: its BSSID policy admits only GET.

## Direct current reference evidence

The read-only 25C56 BootKC container has SHA-256
eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d,
outer UUID F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5, and embedded Wi-Fi KEXT UUID
8FB4B7F0-D656-3539-B8D6-C1327A50377C.

Direct nested LC_SYMTAB recovery identifies the public setter
apple80211setBSSID at nlist 7057 in the half-open VM range
0xffffff80021c372e..0xffffff80021c3739, file range
0x20c372e..0x20c3739. It is an N_SECT|N_EXT record (type 0x0f, section 1,
desc 0x0000); the next symbol starts at the recorded end.

The exact 11-byte body is:

    55 48 89 e5 b8 0e 28 82 e0 5d c3

It reads neither public argument and performs no gate, owner lookup, call,
state, transport, or event operation before returning raw 0xe082280e.
The full file offsets, adjacent symbol boundary, raw bytes, and hash are
preserved under
docs/reference/artifacts/skywalk-bssid-set-public-fixed-stub-bootkc-current/.

## Boundary and non-claims

This is an exact, narrow public SET status alignment. It does not claim
outer-null, carrier/ABI, V1, card-specific GET, GET/bootstrap behavior,
association, firmware, runtime-execution, or broader Tahoe behavior parity.

No private carrier or selector is constructed or invoked. No deployment,
radio change, association, traffic, or runtime selector execution is part of
this layer.

## Verification

scripts/skywalk_public_bssid_set_fixed_stub_alignment_report.py --check
checks the direct raw record and manifest, the exact guarded SET branch, the
unchanged bootstrap GET and fallback behavior, preserved V1/card boundaries,
selector ABI declaration, and active Tahoe source-phase markers.
