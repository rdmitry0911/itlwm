# CR-500 — legacy IOC 29 DEAUTH blind-success quarantine

Date: 2026-07-15

## Scope

This correction covers only the older AirportItlwm controller dispatcher in
AirportSTAIOCTL.cpp. Its APPLE80211_IOC_DEAUTH case uses the IOCTL macro, so
the SET half routes the typed apple80211_deauth_data carrier to
AirportItlwm::setDEAUTH(OSObject *, apple80211_deauth_data *).

It does not alter the Tahoe Skywalk bridge corrected by CR-499, the paired
legacy GET DEAUTH reader, APSTA DEAUTH, or the separate payload-less
APPLE80211_IOC_DISASSOCIATE (22) lifecycle.

## Evidence boundary

The selected current BootKernelExtensions.kc capture retained by CR-499 has
SHA-256 eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d
and UUID F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5. Its public
apple80211setDEAUTH wrapper gates selector 0x1d, type-checks the interface,
and tail-dispatches a typed terminal virtual. The exact bytes and manifest
are retained in
docs/reference/artifacts/deauth-selector-dispatch-bootkc-current/.

That is current Skywalk topology, not a recovered legacy AirportItlwm terminal.
It is therefore not used to claim legacy Apple terminal behavior, null-input
behavior, valid-input return code, carrier layout, state mutation, firmware
transaction, management frame, or runtime reachability.

The local source proof is narrower and sufficient for the correction:

- the legacy request switch routes IOC 29 through the bidirectional IOCTL
  macro;
- the getter remains a read of the local deauthentication reason;
- before this correction the SET handler ignored its controller, version,
  reason, and BSSID inputs and returned success unconditionally;
- the distinct legacy IOC 22 handler owns its own state and management-frame
  lifecycle and is not a substitute for IOC 29.

AirportSTAIOCTL.cpp remains present in historical AirportItlwm source build
phases, while the Tahoe source phase uses the Skywalk implementation instead.

## Local correction

The legacy SET handler now leaves both arguments unread and returns
kIOReturnUnsupported. This removes only the false acknowledgement. It does
not create a deauthentication implementation, inject a management frame,
change association state, add a selector, change ABI, or perform a runtime
request.

## Deterministic guard

scripts/legacy_deauth_blind_success_quarantine_report.py --check verifies the
legacy IOC route, fail-closed no-effect setter, preservation of paired GET and
distinct DISASSOCIATE behavior, the separate Skywalk quarantine, current raw
identity, project source-phase boundary, and this correction record.

## Historical build boundary

An observation-only Debug build of the historical AirportItlwm-Ventura target
was attempted with the current SDK and did compile AirportSTAIOCTL.cpp. The
target does not presently complete because pre-existing unresolved legacy
identifiers occur at lines 595, 1244, 1247, 1739, 1746, 1807, 1991, and 1994
of that translation unit. No diagnostic references this correction near the
legacy DEAUTH setter. This is retained as a failed historical-target build,
not reported as a passing legacy build. Tahoe validation remains separate
because Tahoe excludes AirportSTAIOCTL.cpp from its source phase.

## Non-claims

This is a local no-owner safety boundary, not Apple legacy semantic parity.
It does not claim Apple terminal behavior, null or valid-input status,
carrier-layout parity, state or firmware parity, management-frame parity,
legacy runtime coverage, Tahoe runtime coverage, deployment, radio activity,
association, or traffic.
