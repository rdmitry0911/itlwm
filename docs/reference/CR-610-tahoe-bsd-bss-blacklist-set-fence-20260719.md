# CR-610 — Tahoe BSD BSS-blacklist SET carrier fence

## Scope

`APPLE80211_IOC_BSS_BLACKLIST` (`0x174`) uses an exact 43-byte public carrier:
one count byte followed by up to seven BSSIDs.  The recovered Tahoe public
route returns `0x66` for an absent interface before returning `0x16` for a
null or wrong-sized carrier.

After that P0 gate, the previous local BSD SET reached the command-gate owner,
which copies all 43 bytes from the nested `req_data` pointer.  The BSD callback
marshals only the outer `apple80211req`, so that direct copy could consume a
user virtual address in kernel context.  Tahoe now preserves the existing P0
results and delegates only a valid BSD SET to `IO80211Family` before the local
dispatcher.

## Preserved paths and limits

GET is unchanged: the local GET owner does not dereference its public carrier
and launches its asynchronous result query.  The local SET owner is likewise
kept for kernel-owned callers outside the raw BSD ingress.

The selector remains outside `TahoeSkywalkIoctlRoutes`; this change neither
adds a card-specific path nor assumes a kernel carrier for one.  Local
blacklist state has no scan, join, or association caller, and the recovered
WCL policy only deprioritizes deny-listed candidates while retaining fallback.

The fence does not manufacture a SET status or payload.  It deliberately does
not alter the blacklist owner, its events, or the malformed/absent-interface
status order.

## Validation boundary

The accompanying static guard fixes the selector, 43-byte ABI and P0 ordering,
then verifies the Tahoe SET-only delegation occurs after preflight and before
the local dispatcher without reading the nested carrier.  Runtime validation
uses normal Wi-Fi OFF/ON, keychain join, AP authorization and protected
laboratory traffic only; it does not issue raw Apple80211 or BSD probes.
