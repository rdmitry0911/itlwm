# CR-518 — legacy V1 BTCOEX_MODE non-null fixed-stub alignment

Date: 2026-07-15

## Scope

This correction covers only the non-null historical V1 SET path of
APPLE80211_IOC_BTCOEX_MODE (IOC 87) in AirportSTAIOCTL.cpp. It retains the
typed packed apple80211_btc_mode_data carrier, the existing bidirectional V1
dispatcher case, the pre-existing null guard, the separate V1 GET readback,
all neighboring BTCOEX selectors, and Tahoe's existing direct BTCOEX
rejection boundary.

It does not change a selector number, route, carrier declaration, null guard,
V1 GET, V2/Skywalk source, or implement or invoke radio, association,
firmware, event, traffic, AWDL, P2P, APSTA, scan, or CCA behavior. It does
not claim null-input, carrier layout, ABI, user-client, GET, BTCOEX policy,
or Tahoe behavior parity.

## Current reference evidence and null boundary

The read-only current macOS 26.2 / 25C56 BootKC_guest_25C56.kc container has
SHA-256 eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d,
outer UUID F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5, and embedded Wi-Fi MH_KEXT
UUID 8FB4B7F0-D656-3539-B8D6-C1327A50377C.

Direct nested-LC_SYMTAB recovery identifies external section-1 nlist 7324,
__Z24apple80211setBTCOEX_MODEP23IO80211SkywalkInterfacePv, at half-open
VM/file ranges [0xffffff80021c3fda, 0xffffff80021c3fe5) and
[0x20c3fda, 0x20c3fe5). The next sorted external section symbol is nlist
7169, __Z21apple80211setWOW_TESTP23IO80211SkywalkInterfaceP24apple80211_wow_test_data,
at 0xffffff80021c3fe5, so the recovered body is exactly 11 bytes:

55 48 89 e5 b8 0e 28 82 e0 5d c3

It decodes as a fixed mov eax, 0xe082280e return. The public body reads
neither public argument and has no selector load, gate, metacast, dynamic
tail, owner lookup, call, state, transport, event, or dispatch to a static
handler or terminal. This is direct current public SET-wrapper evidence; it
does not establish a null-input contract or make a claim about handlers
elsewhere in the KEXT. The public SDK does not give this raw status a
canonical local symbolic name, and it is deliberately not relabelled
kIOReturnUnsupported.

The local packed carrier has a version and btc_mode word, but the public
reference stub is unread. No size or carrier-layout parity is inferred. The
exact identities, nlist/range boundary, raw bytes, body digest, and
disassembly command are retained in
docs/reference/artifacts/legacy-btcoex-mode-public-fixed-stub-bootkc-current/raw.txt.

## Local divergence and correction

Before this correction, the historical V1 setter returned kIOReturnError for
a null carrier; for non-null input it assigned data->btc_mode to the local
btcMode readback cache and returned kIOReturnSuccess. The V1 dispatcher
admits GET and SET through its typed IOCTL macro. The paired V1 GET remains
separate existing behavior: after its own null guard, it emits the project
version and btcMode.

The existing null guard remains deliberately unchanged because the recovered
public stub itself does not inspect the carrier and therefore does not
authorize a claim of end-to-end local null parity. For non-null input the V1
setter now leaves the carrier unread and returns the exact recovered numeric
0xe082280e. This reduces only the proven non-null blind-success capability
claim. It does not claim Apple historical behavior, caller population,
runtime reachability, a SET chain, or broader BTCOEX semantics are identical.

Tahoe remains separate and untouched. Skywalk groups BTCOEX_MODE with the
other BTCOEX selectors and returns kApple80211ClassOwnerAbsent, locally the
exact numeric 0xe082280e. That existing Tahoe direct-gate work has its own
CR-479 / 26.3 record and is not substituted for the independent current
25C56 V1 public-stub evidence. Tahoe's source phase includes
AirportItlwmSkywalkInterface.cpp, not AirportSTAIOCTL.cpp. This V1-only
correction therefore makes no Tahoe runtime claim.

## Verification boundary

scripts/legacy_btcoex_mode_non_null_fixed_stub_alignment_report.py --check
verifies the raw-artifact identity/manifest, exact public unread fixed-stub
status, the retained V1 selector/carrier/route, null guard and V1 GET, the
exact unread non-null V1 SET status, and the separate Tahoe direct-gate and
source-phase boundary.

No private carrier or selector is invoked. This layer makes no deployment,
radio, association, APSTA, AWDL, P2P, scan, firmware, event, traffic, CCA, or
runtime-execution claim.
