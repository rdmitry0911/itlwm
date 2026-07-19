# CR-612 — Tahoe BSD AP_IE_LIST carrier fence

## Scope

The Tahoe BSD GET `APPLE80211_IOC_AP_IE_LIST` (48) previously reached the
local `getAP_IE_LIST()` helper directly.  That helper initializes the carrier
header and can copy up to the embedded 1024-byte IE array through the nested
`apple80211req::req_data` pointer, but the BSD callback marshals only the
outer request.

The Tahoe-only BSD bridge now delegates this raw GET to
`IO80211InfraProtocol::processBSDCommand()` before the local dispatcher.  The
local helper remains available to kernel-owned callers; SET behavior is
unchanged.

## Reference evidence

The exact 25C56 recovery is recorded in
`artifacts/tahoe-ap-ie-list-bsd-carrier-bootkc-current/raw.txt`.

- private BSD/family `getAP_IE_LIST` at `0xffffff80021d691b` requires a
  non-null `req_data` and `req_len == 0x808`, otherwise returns `0x16`;
- it issues legacy GET `0xc02869c9`, selector `0x30`, with a `0x808` carrier,
  then falls back to the public leaf;
- public `apple80211getAP_IE_LIST` at `0xffffff80021becf0` gates selector
  `0x30` via virtual slot `+0xcc8`, resolves the interface owner, and
  tail-calls its `+0x408` route; absent owner returns `0xe082280e`.

The local Tahoe helper uses a distinct `apple80211_ap_ie_data` shape with a
`version`, `len`, and embedded 1024-byte IE output array.  Passing the raw BSD
nested address to that helper bypassed the reference family validation and
carrier conversion.

## Preserved paths and limits

Selector 48 is absent from `TahoeSkywalkIoctlRoutes`, so no card-specific
kernel-owned route is diverted.  This change does not invent AP IEs, alter
RSN, join, or association state, or claim that the family result payload is
equivalent to the former local one.  It restores the reference ownership
boundary at raw BSD ingress.

## Validation boundary

`scripts/test_tahoe_ap_ie_list_bsd_carrier_fence.sh` fixes the selector and
local embedded-array shape, records the recovered 25C56 transport constants,
and verifies a Tahoe-only GET fence precedes the direct local dispatcher
without inspecting the nested carrier.  Runtime validation uses only normal
Wi-Fi OFF/ON, keychain join, AP authorization, and laboratory traffic.
