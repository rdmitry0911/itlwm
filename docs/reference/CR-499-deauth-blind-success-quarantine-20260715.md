# CR-499 — public IOC 29 DEAUTH blind-success quarantine

Date: 2026-07-15

## Scope

This correction covers only the Skywalk BSD bridge SET half of
APPLE80211_IOC_DEAUTH (numeric 29):
AirportItlwmSkywalkInterface::setDEAUTH(apple80211_deauth_data *). It removes
its unconditional local success acknowledgement. It preserves the typed
carrier, the paired GET DEAUTH reader, the SIOCSA80211 route, APSTA DEAUTH,
the older AirportSTAIOCTL.cpp dispatcher, and the distinct void
APPLE80211_IOC_DISASSOCIATE (22) lifecycle. This correction does not
substitute one selector for the other.

## Recovered current BootKC selector topology

The selected current x86_64 BootKernelExtensions.kc identity is:

~~~text
path: /System/Library/KernelCollections/BootKernelExtensions.kc
SHA-256: eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d
UUID: F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5
~~~

apple80211setDEAUTH at 0xffffff80021c3a1f retains the original typed carrier
in RBX, invokes interface virtual +0xcc8 with selector 0x1d, and returns the
first handler's nonzero status immediately. For zero it performs
OSMetaClassBase::safeMetaCast; a successful cast tail-dispatches virtual
+0x2e0 with the original interface/carrier pair, while a failed cast returns
0xe082280e.

The selected capture proves gate/type/terminal-vtable topology. It is current
BootKC evidence, not canonical 25C56 AppleBCMWLAN DEXT evidence, and does not
establish the terminal owner's behavior, null-input behavior, complete carrier
layout, valid-input return code, firmware transaction, state mutation, or
management-frame transmission. The private symbols named setDEAUTH and
setDeauth are recorded only as unproven candidates; this correction does not
conflate them with the terminal virtual target. The capture is
docs/reference/artifacts/deauth-selector-dispatch-bootkc-current/raw.txt.

## Local correction

IOC 29 has a typed apple80211_deauth_data carrier with version, reason, and
BSSID, but the Skywalk local setter returned kIOReturnSuccess without reading
it, changing local deauthentication state, using firmware transport, or
publishing a management/event result. That was a blind successful
acknowledgement. The method now leaves the carrier unread and returns
kIOReturnUnsupported.

This is a no-owner safety boundary, not Apple null-input, valid-input
return-code, terminal-handler, carrier-layout, management-frame, state,
firmware, or runtime-selector parity. It invokes no private selector, IOVAR,
firmware command, scan, radio transition, deployment, association, or traffic
path.

## Deterministic guard

scripts/deauth_blind_success_quarantine_report.py --check verifies the current
BootKC identity/raw gate and terminal-vtable anchors, preserved typed IOC 29
route, local non-reading fail-closed behavior, preservation of the separate
DISASSOCIATE boundary, and the correction record in the signal-chain audit.
