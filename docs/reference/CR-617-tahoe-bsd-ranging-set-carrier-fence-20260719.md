# CR-617: Tahoe BSD ranging-authenticate SET carrier fence

Date: 2026-07-19

## Scope

This closure covers only the Tahoe raw BSD `SET` ingress for
`APPLE80211_IOC_RANGING_AUTHENTICATE` (selector 243). It does not implement
ranging authentication, alter the typed proximity path, submit an FTM/proxd
request, or change selectors 241 and 242.

## Fresh reference contract

The exact Tahoe 25C56 BootKC evidence is pinned in
`artifacts/tahoe-ranging-authenticate-bsd-set-route-bootkc-current/raw.txt`.
The external BSD SET router reads only the outer selector at request `+0x10`,
looks it up in its SET table, and returns `0x66` before any nested carrier
access when the table entry is zero.

For selector `0xf3`, table index `0xf2` resolves to exactly eight zero bytes.
There is therefore no raw BSD handler, `req_len` validation, `req_data`
copyin/copyout, WCL request, allocation, or owner gate to emulate.

The separately recovered typed leaf gates selector `0xf3`, safely casts its
protocol, and tail-calls slot `+0x11b8`. The deeper Core owner validates the
PMK and talks to proximity firmware. Neither is an external BSD carrier
contract and neither changes the zero table entry.

## Local divergence

The local raw BSD bridge directly dispatches selector 243 to
`setRANGING_AUTHENTICATE`, which reaches the local typed commander and reads
fields from the nested carrier while validating its PMK. That dereference is
not reachable in Apple's raw BSD route and can consume an un-marshalled caller
address.

Selector 243 has no Tahoe card-specific route in
`TahoeSkywalkIoctlRoutes.hpp`; this correction neither creates one nor removes
the typed helper for internal callers.

## Correction

On Tahoe only, raw BSD `SET` selector 243 delegates to
`IO80211InfraProtocol::processBSDCommand` before any local nested-carrier
access. The fence reads only the outer selector, leaves the typed helper and
all other ranging selectors untouched, and restores the family-owned `0x66`
raw-route rejection.

## Verification boundary

`scripts/test_tahoe_ranging_set_bsd_carrier_fence.sh` pins the 25C56 external
router and zero SET-table entry, SET-only early delegation, retained typed
helper, and absence of a fabricated card-specific route. Build/load and normal
association/traffic gates establish regression safety. No raw BSD or raw
Apple80211 selector is sent at runtime.
