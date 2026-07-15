# CR-517 — legacy V1 AUTH_TYPE fixed-stub alignment

Date: 2026-07-15

## Scope

This correction covers only the historical V1 SET half of
APPLE80211_IOC_AUTH_TYPE (IOC 2) in AirportSTAIOCTL.cpp. It retains the typed
apple80211_authtype_data carrier, the existing bidirectional V1 dispatcher
case, the separate V1 GET cache readback, all neighboring selectors, and
Tahoe's distinct AUTH_TYPE route and state.

It does not change a selector number, route, carrier declaration, V1 GET,
V2/Skywalk source, or implement or invoke association, radio, firmware,
event, traffic, AWDL, P2P, APSTA, scan, or CCA behavior. It does not claim
carrier, null-input, ABI, user-client, GET, association, or Tahoe behavior
parity.

## Current reference evidence

The read-only current macOS 26.2 / 25C56 BootKC_guest_25C56.kc container has
SHA-256 eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d,
outer UUID F0ACEF59-61D0-DEDC-C1D2-BECE30DD94E5, and embedded Wi-Fi MH_KEXT
UUID 8FB4B7F0-D656-3539-B8D6-C1327A50377C.

Direct nested-LC_SYMTAB recovery identifies external section-1 nlist 7204,
__Z22apple80211setAUTH_TYPEP23IO80211SkywalkInterfaceP24apple80211_authtype_data,
at half-open VM/file ranges [0xffffff80021c3520, 0xffffff80021c352b) and
[0x20c3520, 0x20c352b). The next sorted external section symbol is nlist
7261, __Z23apple80211setCIPHER_KEYP23IO80211SkywalkInterfaceP14apple80211_key,
at 0xffffff80021c352b, so the recovered body is exactly 11 bytes:

55 48 89 e5 b8 0e 28 82 e0 5d c3

It decodes as a fixed mov eax, 0xe082280e return. The body reads neither
public argument and has no selector load, gate, metacast, dynamic tail,
static-handler or terminal dispatch, owner lookup, call, state, transport,
or event operation. This is direct current public SET-wrapper evidence; it is
not an inference from a distinct GET route or a claim about handlers elsewhere
in the KEXT. The public SDK does not give this raw status a canonical local
symbolic name, and it is deliberately not relabelled kIOReturnUnsupported.

The exact identities, nlist/range boundary, raw bytes, body digest, and
disassembly command are retained in
docs/reference/artifacts/legacy-auth-type-public-fixed-stub-bootkc-current/raw.txt.

## Local divergence and correction

Before this correction, the historical V1 setter copied authtype_lower and
authtype_upper into the two V1 AirportItlwm fields and returned
kIOReturnSuccess. The V1 dispatcher admits GET and SET through its typed
IOCTL macro. The paired V1 GET remains separate existing behavior: it emits
the project version and those V1 readback fields.

The V1 setter now explicitly leaves both arguments unread and returns the
exact recovered numeric 0xe082280e. This aligns only the directly recovered
public V1 SET body and status, reducing its blind-success capability claim.
It does not claim Apple historical behavior, caller population, runtime
reachability, a SET chain, or broader authentication semantics are identical.

Tahoe remains distinct and untouched by this V1 layer. Its Skywalk route has
separate AUTH_TYPE GET and auth-context helper state, and uses those fields
when seeding BssManager auth context. Tahoe's source phase includes
AirportItlwmV2.cpp and AirportItlwmSkywalkInterface.cpp, not
AirportSTAIOCTL.cpp. This V1-only correction therefore makes no Tahoe runtime
claim.

Subsequent CR-523 aligns the normal non-null public Tahoe SET route itself to
the same fixed current status while retaining the Skywalk helper and its
direct association callers for local auth-context seeding. That later
Skywalk-only correction does not alter this CR-517 V1 scope.

## Verification boundary

scripts/legacy_auth_type_fixed_stub_alignment_report.py --check verifies the
raw-artifact identity/manifest, exact public unread fixed-stub status, the
retained V1 selector/carrier/route and separate V1 GET, the exact unread V1
SET body, and the separate Tahoe GET/SET/state/source-phase boundary.

No private carrier or selector is invoked. This layer makes no deployment,
radio, association, APSTA, AWDL, P2P, scan, firmware, event, traffic, CCA, or
runtime-execution claim.
