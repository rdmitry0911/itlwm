# CR-529 — Skywalk public PROTMODE fixed-stub alignment

Date: 2026-07-15

## Scope

This layer aligns only the current 25C56 public
APPLE80211_IOC_PROTMODE (IOC 6) normal non-null Tahoe BSD GET and SET
directions in AirportItlwmSkywalkInterface::processApple80211Ioctl.

The compile-time Tahoe-only guard returns the direct public-wrapper raw status
0xe082280e for GET and SET without reading the carrier. The global outer-null
and inner null-carrier fallbacks remain before this branch. A pre-26 or unknown
command uses the existing unsupported/super fallback.

It does not modify the historical V1 getPROTMODE/setPROTMODE helpers,
the card-specific route, any carrier layout, CHANNEL or POWERSAVE behavior,
AWDL, APSTA, association, radio, firmware, event, or traffic path. The
card-specific route remains excluded: IOC 6 is not admitted by
TahoeSkywalkIoctlRoutes::shouldRoute.

## Direct current reference evidence

The read-only 25C56 BootKC container has SHA-256
eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d,
outer UUID F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5, and embedded Wi-Fi KEXT UUID
8FB4B7F0-D656-3539-B8D6-C1327A50377C.

Direct nested LC_SYMTAB recovery gives two public wrappers:

| direction | symbol | nlist | half-open VM range | exact body |
| --- | --- | ---: | --- | --- |
| GET | apple80211getPROTMODE | 7146 | 0xffffff80021be4bb..0xffffff80021be4c6 | fixed 0xe082280e |
| SET | apple80211setPROTMODE | 7164 | 0xffffff80021c3679..0xffffff80021c3684 | fixed 0xe082280e |

Both are N_SECT|N_EXT records (type 0x0f, section 1, desc 0x0000) and have
the same 11-byte body:

    55 48 89 e5 b8 0e 28 82 e0 5d c3

That body reads neither public argument and performs no gate, owner lookup,
call, state, transport, or event operation before returning the raw status.
The full addresses, file offsets, adjacent symbol boundaries, bytes, and
hashes are preserved in
docs/reference/artifacts/skywalk-protmode-public-fixed-stub-bootkc-current/.

## Boundary and non-claims

This is an exact, narrow public-wrapper status alignment, not a claim that
the historical V1 helper, carrier ABI, card-specific route, pre-26 behavior,
or any broader PROTMODE semantics match the reference. It does not claim
outer-null, carrier/ABI, V1, card-specific, CHANNEL, POWERSAVE, association,
firmware, runtime-execution, or broader Tahoe behavior parity.

No private carrier or selector is constructed or invoked. No deployment,
radio change, association, traffic, or runtime selector execution is part of
this layer.

## Verification

scripts/skywalk_public_protmode_fixed_stub_alignment_report.py --check
checks the direct raw records and manifest, both exact guarded directions,
the preserved null/pre-26 fallback, V1 helpers, card-specific exclusion,
selector ABI declaration, and active Tahoe source-phase markers.
