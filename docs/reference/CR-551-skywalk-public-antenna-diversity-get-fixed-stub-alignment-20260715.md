# CR-551 — Skywalk public ANTENNA_DIVERSITY GET fixed-stub alignment

Date: 2026-07-15

## Scope

This layer aligns only the current 25C56 public
APPLE80211_IOC_ANTENNA_DIVERSITY (IOC 39) Tahoe BSD GET direction in
AirportItlwmSkywalkInterface::processApple80211Ioctl.

The compile-time Tahoe-only case returns the direct public-wrapper raw status
0xe082280e before observing the carrier. The direct leaf reads neither public
argument, so the branch applies to a non-null request object without claiming
outer-null dispatch behavior. The selector remains absent from the pre-26
switch. The separately evidenced Tahoe SET direction is confined to CR-597;
this GET record does not independently establish SET behavior.

Legacy V1 has a separate ANTENNA_DIVERSITY route using an existing
apple80211_antenna_data carrier. It is deliberately preserved and this layer
does not claim V1 behavior parity. The new Skywalk branch neither constructs
nor consumes a carrier.

Virtual IOCTL does not name IOC 39. The card-specific route has no
ANTENNA_DIVERSITY entry and continues to exclude it through
TahoeSkywalkIoctlRoutes::shouldRoute's false default.

## Direct current reference evidence

The read-only 25C56 BootKC container has SHA-256
eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d,
outer UUID F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5, and embedded Wi-Fi KEXT UUID
8FB4B7F0-D656-3539-B8D6-C1327A50377C.

Direct nested LC_SYMTAB recovery identifies the public getter
apple80211getANTENNA_DIVERSITY at nlist 7702 in the half-open VM range
0xffffff80021beafd..0xffffff80021beb08, file range
0x20beafd..0x20beb08. It is an N_SECT|N_EXT record (type 0x0f, section 1,
desc 0x0000); the next symbol starts at the recorded end.

The exact 11-byte body is:

    55 48 89 e5 b8 0e 28 82 e0 5d c3

It reads neither public argument and performs no gate, owner lookup, call,
state, transport, or event operation before returning raw 0xe082280e.
The full file offsets, adjacent symbol boundary, raw bytes, and hash are
preserved under
docs/reference/artifacts/skywalk-antenna-diversity-get-public-fixed-stub-bootkc-current/.

## Boundary and non-claims

This is an exact, narrow Skywalk public GET status alignment. It does not
claim outer-null dispatch behavior, an ANTENNA_DIVERSITY Skywalk carrier
contract, SET behavior, antenna-diversity behavior, legacy V1 behavior,
Virtual IOCTL, card-specific behavior, firmware, runtime-execution, radio,
association, traffic, or broader Tahoe behavior parity.

SET behavior is separately aligned and documented by CR-597; this GET evidence
does not independently prove SET behavior.

No private carrier or selector is constructed or invoked. No deployment,
radio change, association, traffic, or runtime selector execution is part of
this layer.

## Verification

scripts/skywalk_public_antenna_diversity_get_fixed_stub_alignment_report.py
--check checks the direct raw record and manifest, the exact Tahoe-only GET
case, absent pre-26 source case, preserved separate legacy V1 route, separated
Virtual/card seams, the ANTENNA_DIVERSITY-case boundary before ROM, and active
Tahoe source-phase markers.
