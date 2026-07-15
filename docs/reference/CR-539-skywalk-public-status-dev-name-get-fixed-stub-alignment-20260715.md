# CR-539 — Skywalk public STATUS_DEV_NAME GET fixed-stub alignment

Date: 2026-07-15

## Scope

This layer aligns only the current 25C56 public
APPLE80211_IOC_STATUS_DEV_NAME (IOC 23) Tahoe BSD GET direction in
AirportItlwmSkywalkInterface::processApple80211Ioctl.

The compile-time Tahoe-only GET branch returns the direct public-wrapper raw
status 0xe082280e before observing req_data. The public leaf reads neither
argument, so the implementation intentionally does not invent a local
STATUS_DEV_NAME carrier ABI, does not consume the existing local
STATUS_DEV_NAME carrier declaration, and does not require a non-null carrier.
The outer request object remains required by the existing dispatcher in order
to read req_type. The Tahoe non-GET outcome remains unsupported; pre-26
dispatch remains unchanged.

No historical V1 route, Virtual IOCTL route, or card-specific route names IOC
23. TahoeSkywalkIoctlRoutes::shouldRoute admits no STATUS_DEV_NAME entry and
therefore continues to exclude it through its false default.

## Direct current reference evidence

The read-only 25C56 BootKC container has SHA-256
eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d,
outer UUID F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5, and embedded Wi-Fi KEXT UUID
8FB4B7F0-D656-3539-B8D6-C1327A50377C.

Direct nested LC_SYMTAB recovery identifies the public getter
apple80211getSTATUS_DEV_NAME at nlist 7595 in the half-open VM range
0xffffff80021be92c..0xffffff80021be937, file range
0x20be92c..0x20be937. It is an N_SECT|N_EXT record (type 0x0f, section 1,
desc 0x0000); the next symbol starts at the recorded end.

The exact 11-byte body is:

    55 48 89 e5 b8 0e 28 82 e0 5d c3

It reads neither public argument and performs no gate, owner lookup, call,
state, transport, or event operation before returning raw 0xe082280e.
The full file offsets, adjacent symbol boundary, raw bytes, and hash are
preserved under
docs/reference/artifacts/skywalk-status-dev-name-get-public-fixed-stub-bootkc-current/.

## Boundary and non-claims

This is an exact, narrow public GET status alignment. It does not claim
outer-null dispatch behavior, a STATUS_DEV_NAME carrier ABI, SET behavior,
V1, Virtual IOCTL, card-specific behavior, device-name discovery, firmware,
runtime-execution, radio, association, traffic, or broader Tahoe behavior
parity.

No private carrier or selector is constructed or invoked. No deployment,
radio change, association, traffic, or runtime selector execution is part of
this layer.

## Verification

scripts/skywalk_public_status_dev_name_get_fixed_stub_alignment_report.py
--check checks the direct raw record and manifest, the exact Tahoe-only GET
branch, preservation of the Tahoe non-GET and pre-26 boundaries, unchanged
outer-null dispatch, separated V1/Virtual/card seams, the selector declaration
and that its existing local carrier is not consumed, and active Tahoe
source-phase markers.
