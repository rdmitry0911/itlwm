# CR-541 — Skywalk public HOST_AP_MODE GET fixed-stub alignment

Date: 2026-07-15

## Scope

This layer aligns only the current 25C56 public APPLE80211_IOC_HOST_AP_MODE
(IOC 25) Tahoe BSD GET direction in
AirportItlwmSkywalkInterface::processApple80211Ioctl.

The compile-time Tahoe-only GET branch returns the direct public-wrapper raw
status 0xe082280e before observing req_data or the APSTA instance. The public
leaf reads neither argument, so the branch applies to a non-null request object
without claiming outer-null dispatch behavior. The existing APSTA instance
guard and setHOST_AP_MODE producer remain below the GET branch for SET and
pre-26 behavior.

The card-specific route continues to admit IOC 25 only when isSet; its GET
direction remains excluded by TahoeSkywalkIoctlRoutes::shouldRoute. Historical
V1 and Virtual IOCTL routes do not name IOC 25.

## Direct current reference evidence

The read-only 25C56 BootKC container has SHA-256
eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d,
outer UUID F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5, and embedded Wi-Fi KEXT UUID
8FB4B7F0-D656-3539-B8D6-C1327A50377C.

Direct nested LC_SYMTAB recovery identifies the public getter
apple80211getHOST_AP_MODE at nlist 7374 in the half-open VM range
0xffffff80021be942..0xffffff80021be94d, file range
0x20be942..0x20be94d. It is an N_SECT|N_EXT record (type 0x0f, section 1,
desc 0x0000); the next symbol starts at the recorded end.

The exact 11-byte body is:

    55 48 89 e5 b8 0e 28 82 e0 5d c3

It reads neither public argument and performs no gate, owner lookup, call,
state, transport, or event operation before returning raw 0xe082280e.
The full file offsets, adjacent symbol boundary, raw bytes, and hash are
preserved under
docs/reference/artifacts/skywalk-host-ap-mode-get-public-fixed-stub-bootkc-current/.

## Boundary and non-claims

This is an exact, narrow public GET status alignment. It does not claim
outer-null dispatch behavior, a HOST_AP_MODE carrier ABI, SET HostAP behavior,
APSTA-owner lifecycle, V1, Virtual IOCTL, card-specific behavior, AP startup,
firmware, runtime-execution, radio, association, traffic, or broader Tahoe
behavior parity.

No private carrier or selector is constructed or invoked. No deployment,
radio change, association, traffic, or runtime selector execution is part of
this layer.

## Verification

scripts/skywalk_public_host_ap_mode_get_fixed_stub_alignment_report.py
--check checks the direct raw record and manifest, the exact Tahoe-only GET
branch, preserved instance/SET and pre-26 boundaries, the card SET-only seam,
separated V1/Virtual paths, existing selector/carrier declarations without GET
carrier access, and active Tahoe source-phase markers.
