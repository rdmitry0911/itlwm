# CR-571 — Skywalk public P2P NOA/OPP/CT GET/SET fixed-stub alignment

## Scope

This batch recovers only current 25C56 public Tahoe BSD GET and SET behavior
for three adjacent selectors:

- APPLE80211_IOC_P2P_NOA_LIST (IOC 99);
- APPLE80211_IOC_P2P_OPP_PS (IOC 100); and
- APPLE80211_IOC_P2P_CT_WINDOW (IOC 101).

All six public wrappers are direct 11-byte leaves returning raw 0xe082280e
without reading their public argument. Exact symbol records are retained in
artifacts/skywalk-p2p-noa-opp-ct-public-fixed-stubs-bootkc-current/ from the
read-only BootKC identity with outer SHA-256
eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d,
outer UUID F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5, and embedded AirportItlwm
UUID 8FB4B7F0-D656-3539-B8D6-C1327A50377C.

## Implementation boundary

One compile-time Tahoe-only switch group covers exactly IOC 99 through 101.
GET or SET returns 0xe082280e before carrier access; another command returns
kIOReturnUnsupported. The group is absent from the pre-26 source path.

There are no existing V1 or Virtual dispatcher routes or typed SET declarations
for these selectors, and this batch does not create them. The Tahoe
card-specific route remains outside all three selectors. No local carrier
contract or P2P behavior is inferred.

## Explicit nonclaims

This evidence does not claim outer-null dispatch behavior, a carrier contract,
P2P NOA, opportunistic power-save, or CT-window behavior, V1 or Virtual IOCTL
behavior, card-specific behavior, firmware, runtime-execution, radio,
association, traffic, or broader Tahoe behavior parity. No private carrier or
selector is constructed or invoked.

## Validation

scripts/skywalk_public_p2p_noa_opp_ct_fixed_stubs_alignment_report.py checks
all six raw records and manifest, the Tahoe-only terminal GET/SET group, absent
pre-26/V1/Virtual routes, inactive card-specific route, and generated state
report. Build and runtime evidence are captured separately; runtime control is
passive only.
