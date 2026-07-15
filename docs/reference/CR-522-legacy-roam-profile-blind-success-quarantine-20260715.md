# CR-522 — legacy V1 ROAM_PROFILE blind-success quarantine

Date: 2026-07-15

## Scope

This correction covers only the historical V1 SET path of
APPLE80211_IOC_ROAM_PROFILE (IOC 216 / 0xd8) in AirportSTAIOCTL.cpp. It
retains the existing typed 76-byte apple80211_roam_profile_band_data carrier,
the bidirectional V1 dispatcher case, the separate V1 GET self-echo, the
roamProfile declaration and teardown, all neighboring selectors, and Tahoe
source unchanged.

It does not change a selector number, route, carrier declaration, V1 GET,
V2/Skywalk source, or implement or invoke radio, association, firmware,
event, traffic, AWDL, P2P, APSTA, scan, CCA, or roam behavior. It does not
claim null-input, valid-input return, carrier layout, ABI, user-client, GET,
roaming-policy, or Tahoe behavior parity.

## Current reference dispatch evidence

The read-only current macOS 26.2 / 25C56 BootKC_guest_25C56.kc container has
SHA-256 eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d,
outer UUID F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5, and embedded Wi-Fi MH_KEXT
UUID 8FB4B7F0-D656-3539-B8D6-C1327A50377C.

Direct nested-LC_SYMTAB recovery identifies external section-1 nlist 7410,
__Z25apple80211setROAM_PROFILEP23IO80211SkywalkInterfaceP33apple80211_roam_profile_all_bands,
at half-open VM/file ranges [0xffffff80021c4f19, 0xffffff80021c4f6e) and
[0x20c4f19, 0x20c4f6e). The next sorted external section symbol is nlist
7530, __Z27apple80211setAWDL_OPER_MODEP23IO80211SkywalkInterfacePv, at
0xffffff80021c4f6e, so the recovered wrapper is exactly 0x55 bytes.

The public wrapper calls its interface virtual `+0xcc8` gate with selector
0xd8 and propagates any nonzero gate result. It then calls
OSMetaClassBase::safeMetaCast; a failed cast returns raw 0xe082280e. On a
successful cast it loads the interface virtual `+0x1178` and tail-dispatches
the original carrier. Thus the wrapper establishes a current public gate and
dynamic Roam-owner topology, rather than a fixed success, fixed unsupported,
or known terminal return contract.

The wrapper's typed public signature names apple80211_roam_profile_all_bands.
The local legacy route instead uses a packed 76-byte
apple80211_roam_profile_band_data. The recovery proves neither an all-bands
carrier size/layout nor any null-input or successful-terminal behavior, so no
ABI conversion or status mapping is inferred. The exact identities,
nlist/range boundary, raw bytes, body digest, and disassembly are retained in
docs/reference/artifacts/legacy-roam-profile-public-dispatch-bootkc-current/raw.txt.

## Local divergence and correction

Before this correction, the historical V1 setter freed any existing
roamProfile, allocated a new 76-byte buffer, copied the carrier into it, and
returned kIOReturnSuccess. Source inspection confines that pointer to this
setter, the paired GET self-echo, declaration, and teardown; there is no local
Roam owner, `roam_prof` transport, callback, or firmware application.

The V1 setter now leaves both arguments unread and returns
kIOReturnUnsupported. This is a no-local-backend quarantine, not Apple
valid-input return-code parity: the recovered public wrapper can gate or
dynamically tail to an owner that does not exist in the Intel path. The local
setter had no pre-existing null guard; making it unread is deliberately not a
claim of reference null-input parity. The paired GET and pointer teardown are
retained because their lifecycle/ABI behavior has not been separately
recovered.

Tahoe is distinct and untouched. Its Skywalk source is compiled in the Tahoe
phase, while AirportSTAIOCTL.cpp is not. This V1-only correction makes no
Tahoe runtime claim.

## Verification boundary

scripts/legacy_roam_profile_blind_success_quarantine_report.py --check
verifies the raw-artifact identity/manifest, current selector-gate/metacast/
dynamic-slot topology, the local 216 route and 76-byte carrier, unread
fail-closed setter, preserved V1 GET/teardown boundary, and Tahoe source-phase
separation.

No private carrier or selector is invoked. This layer makes no deployment,
radio, association, APSTA, AWDL, P2P, scan, firmware, event, traffic, CCA, or
runtime-execution claim.
