# CR-559 — Skywalk public MCS-adjacent GET fixed-stub alignment

Date: 2026-07-16

## Scope

This layer aligns only the current 25C56 public Tahoe BSD GET directions for
APPLE80211_IOC_RIFS (IOC 58), APPLE80211_IOC_LDPC (IOC 59),
APPLE80211_IOC_MSDU (IOC 60), APPLE80211_IOC_MPDU (IOC 61),
APPLE80211_IOC_BLOCK_ACK (IOC 62), APPLE80211_IOC_PLS (IOC 63),
APPLE80211_IOC_PSMP (IOC 64), and APPLE80211_IOC_PHY_SUB_MODE (IOC 65)
in AirportItlwmSkywalkInterface::processApple80211Ioctl.

Each compile-time Tahoe-only case returns its direct public-wrapper raw status
0xe082280e before observing the carrier. Every direct leaf reads neither public
argument, so the branches apply to a non-null request object without claiming
outer-null dispatch behavior. The selectors remain absent from the pre-26
switch; Tahoe non-GET remains unsupported.

No local carrier contract is inferred for any member of this MCS-adjacent
group. This layer neither constructs nor consumes a carrier. Historical V1 and
Virtual IOCTL routes do not name these selectors. The card-specific route has
no MCS-adjacent group entry and continues to exclude them through
TahoeSkywalkIoctlRoutes::shouldRoute's false default.

## Direct current reference evidence

The read-only 25C56 BootKC container has SHA-256
eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d,
outer UUID F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5, and embedded Wi-Fi KEXT UUID
8FB4B7F0-D656-3539-B8D6-C1327A50377C.

Direct nested LC_SYMTAB recovery identifies eight adjacent N_SECT|N_EXT
records (type 0x0f, section 1, desc 0x0000) covering the half-open VM range
0xffffff80021bef22..0xffffff80021bef7a and file range
0x20bef22..0x20bef7a. Their exact boundaries, nlist indices, adjacent symbols,
raw bytes, and hashes are preserved under
docs/reference/artifacts/skywalk-mcs-adjacent-get-public-fixed-stubs-bootkc-current/.

Every record has the same exact 11-byte body:

    55 48 89 e5 b8 0e 28 82 e0 5d c3

It reads neither public argument and performs no gate, owner lookup, call,
state, transport, or event operation before returning raw 0xe082280e.

## Boundary and non-claims

This is an exact, narrow public GET status alignment. It does not claim
outer-null dispatch behavior, a carrier contract, SET behavior, RIFS behavior,
LDPC behavior, MSDU behavior, MPDU behavior, block-ack behavior, PLS behavior,
PSMP behavior, PHY-sub-mode behavior, V1, Virtual IOCTL, card-specific
behavior, firmware, runtime-execution, radio, association, traffic, or broader
Tahoe behavior parity.

No private carrier or selector is constructed or invoked. No deployment,
radio change, association, traffic, or runtime selector execution is part of
this layer.

## Verification

scripts/skywalk_public_mcs_adjacent_get_fixed_stubs_alignment_report.py
--check checks all eight direct raw records and the manifest, the exact
Tahoe-only GET group, absent pre-26 source cases, separated V1/Virtual/card
seams, selector declarations without local carrier access, and active Tahoe
source-phase markers.
