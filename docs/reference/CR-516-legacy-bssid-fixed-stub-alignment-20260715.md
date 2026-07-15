# CR-516 — legacy V1 BSSID fixed-stub alignment

Date: 2026-07-15

## Scope

This correction covers only the historical V1 SET half of
`APPLE80211_IOC_BSSID` (IOC 9) in `AirportSTAIOCTL.cpp`. It retains the
typed `apple80211_bssid_data` carrier, the existing bidirectional V1
dispatcher case, the separate legacy V1 GET behavior, all neighboring
selectors, and Tahoe's distinct BSSID bridge.

It does not alter a selector number, route, carrier declaration, V1 GET,
V2/Skywalk source, association state, scan, radio, firmware, event, traffic,
AWDL, P2P, APSTA, or CCA behavior. In particular, this correction does not
claim carrier, null-input, ABI, user-client, GET, or Tahoe behavior parity.

## Current reference evidence

The read-only current macOS 26.2 / 25C56
`BootKC_guest_25C56.kc` container has SHA-256
`eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d`,
outer UUID `F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5`, and embedded Wi-Fi
`MH_KEXT` UUID `8FB4B7F0-D656-3539-B8D6-C1327A50377C`.

Direct nested-`LC_SYMTAB` recovery identifies external section-1 nlist 7057,
`__Z18apple80211setBSSIDP23IO80211SkywalkInterfaceP21apple80211_bssid_data`,
at half-open VM/file ranges
`[0xffffff80021c372e, 0xffffff80021c3739)` and
`[0x20c372e, 0x20c3739)`. The next sorted external section symbol is nlist
7167, `__Z21apple80211setSCAN_REQP23IO80211SkywalkInterfaceP20apple80211_scan_data`,
at `0xffffff80021c3739`, so the recovered body is exactly 11 bytes:

```text
55 48 89 e5 b8 0e 28 82 e0 5d c3
```

It decodes as a fixed `mov eax, 0xe082280e` return. The body reads neither
public argument and has no selector load, gate, metacast, dynamic tail, owner
lookup, call, state, transport, or event operation. This is direct current
public SET-wrapper evidence; it is not an inference from a distinct GET path.
The public SDK does not give this raw status a canonical local symbolic name,
and it is deliberately not relabelled `kIOReturnUnsupported`.

The exact identities, nlist/range boundary, raw bytes, body digest, and
disassembly command are retained in
`docs/reference/artifacts/legacy-bssid-public-fixed-stub-bootkc-current/raw.txt`.

## Local divergence and correction

Before this correction, the historical V1 setter ignored both arguments and
returned `kIOReturnSuccess`. The V1 dispatcher admits GET and SET through its
typed `IOCTL` macro. The paired V1 GET is separate existing behavior: it
zeroes the carrier, supplies the project version, and reads the live BSSID
only when the legacy controller is associated.

The V1 setter now explicitly leaves both arguments unread and returns the
exact recovered numeric `0xe082280e`. This aligns only the directly recovered
public V1 SET body and status, reducing its blind-success capability claim.
It does not claim Apple historical behavior, caller population, runtime
reachability, a SET chain, or broader BSSID semantics are identical.

Tahoe remains distinct and untouched. Its active Skywalk bridge has BSSID GET
handling and rejects SET with `kIOReturnUnsupported`; Tahoe's source phase
includes `AirportItlwmV2.cpp` and `AirportItlwmSkywalkInterface.cpp`, not
`AirportSTAIOCTL.cpp`. This V1-only correction therefore makes no Tahoe
runtime claim.

## Verification boundary

`scripts/legacy_bssid_fixed_stub_alignment_report.py --check` verifies the
raw-artifact identity/manifest, exact public unread fixed-stub status, the
retained V1 selector/carrier/route and separate V1 GET, the exact unread V1
SET body, and the separate Tahoe GET/SET-rejection/source-phase boundary.

No private carrier or selector is invoked. This layer makes no deployment,
radio, association, APSTA, AWDL, P2P, scan, firmware, event, traffic, CCA, or
runtime-execution claim.
