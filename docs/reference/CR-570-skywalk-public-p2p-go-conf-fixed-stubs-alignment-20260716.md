# CR-570 — Skywalk public P2P_GO_CONF GET/SET fixed-stub alignment

## Scope

This change recovers only current 25C56 public Tahoe BSD GET and SET behavior
for APPLE80211_IOC_P2P_GO_CONF (IOC 98). Both public wrappers are direct
11-byte leaves that return raw 0xe082280e without reading their public
argument.

The recovered GET bytes are at 0xffffff80021bf2b1 / 0x20bf2b1 and the SET
bytes are at 0xffffff80021c417b / 0x20c417b. Both exact records are retained
under artifacts/skywalk-p2p-go-conf-public-fixed-stubs-bootkc-current/ from
the read-only BootKC identity:

- outer SHA-256 eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d;
- outer UUID F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5;
- embedded AirportItlwm UUID 8FB4B7F0-D656-3539-B8D6-C1327A50377C.

## Implementation boundary

The Tahoe public switch gains one compile-time Tahoe-only case. GET or SET
returns 0xe082280e before carrier access; another command returns
kIOReturnUnsupported. The case is absent from the pre-26 source path.

The historical V1 and Virtual SET routes and their typed carriers remain
separate and unmodified. Their historical AirportAWDL fixed-stub setter is
also unchanged. The Tahoe card-specific route remains outside this selector.
No local carrier contract or P2P GO behavior is inferred.

## Explicit nonclaims

This evidence does not claim outer-null dispatch behavior, a carrier contract,
P2P GO behavior, V1 or Virtual IOCTL behavior, historical AWDL behavior,
card-specific behavior, firmware, runtime-execution, radio, association,
traffic, or broader Tahoe behavior parity. No private carrier or selector is
constructed or invoked.

## Validation

scripts/skywalk_public_p2p_go_conf_fixed_stubs_alignment_report.py checks the
two raw records and manifest, Tahoe-only terminal GET/SET source boundary,
preserved V1, Virtual, and historical setter text, inactive card-specific
route, and generated state report. Build and runtime evidence are captured
separately; runtime control is passive only.
