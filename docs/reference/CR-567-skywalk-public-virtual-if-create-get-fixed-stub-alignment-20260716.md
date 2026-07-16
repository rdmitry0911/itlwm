# CR-567 — Skywalk public VIRTUAL_IF_CREATE GET fixed-stub alignment

## Scope

This change recovers only current 25C56 public Tahoe BSD GET behavior for
APPLE80211_IOC_VIRTUAL_IF_CREATE (IOC 94). The public wrapper is a direct
11-byte leaf that returns raw 0xe082280e without reading its public argument.

The recovered bytes are recorded in
artifacts/skywalk-virtual-if-create-get-public-fixed-stub-bootkc-current/
and come from the read-only BootKC identity:

- outer SHA-256
  eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d;
- outer UUID F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5;
- embedded AirportItlwm UUID 8FB4B7F0-D656-3539-B8D6-C1327A50377C;
- wrapper 0xffffff80021bf285 / 0x20bf285, ending at
  0xffffff80021bf290 / 0x20bf290;
- raw fixed status 0xe082280e.

## Implementation boundary

The existing public switch case gains a compile-time Tahoe-only guard. For
SIOCGA80211 it returns 0xe082280e before carrier access. Its existing
SIOCSA80211 call to setVIRTUAL_IF_CREATE remains after the guard and is
unchanged.

The pre-26 source path retains that existing SET-only expression and its GET
unsupported fallback. The legacy V1 SET route remains separate and
unmodified. AirportVirtualIOCTL.cpp has no VIRTUAL_IF_CREATE dispatcher, so
this layer neither creates nor infers a Virtual IOCTL route. The card-specific
Tahoe route remains outside this selector. No local GET carrier contract is
inferred; the public argument is neither inspected nor used to construct a
carrier object.

## Explicit nonclaims

This evidence does not claim outer-null dispatch behavior, a carrier contract,
virtual-interface creation behavior, SET behavior, V1, Virtual IOCTL,
card-specific behavior, firmware, runtime-execution, radio, association,
traffic, or broader Tahoe behavior parity. No private carrier or selector is
constructed or invoked.

## Validation

scripts/skywalk_public_virtual_if_create_get_fixed_stub_alignment_report.py
checks the raw manifest, selector identity, Tahoe-only GET boundary, preserved
pre-26 and V1 SET text, absence of a local Virtual dispatcher, inactive
card-specific route, and generated state report. Build and runtime evidence
are captured separately; runtime control is passive only.
