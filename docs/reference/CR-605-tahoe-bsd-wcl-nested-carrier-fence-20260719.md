# CR-605 — Tahoe BSD WCL nested-carrier fence

## Scope

Tahoe `processBSDCommand()` receives an outer `apple80211req` from the BSD
ioctl layer.  The outer structure is marshalled, but `req_data` remains a
caller-owned nested address.  It must not be passed directly to a local
serializer that writes a complete result carrier in kernel context.

This change fences two GET selectors only at that BSD ingress and delegates
them to `IO80211InfraProtocol::processBSDCommand()`:

- `APPLE80211_IOC_BGSCAN_CACHE_RESULTS` (215), whose local producer clears a
  `0xb00`-byte carrier;
- WCL BSS info (`0x1b1`), whose local producer clears a `0x844`-byte carrier
  and copies the complete beacon payload.

The fence precedes the local dispatcher.  It deliberately does **not** alter
either getter or `TahoeSkywalkIoctlRoutes`: card-specific callers supply a
kernel-owned request carrier and continue to reach the existing producers.

## Evidence and decision

- `getWCL_BGSCAN_CACHE_RESULT()` begins with `bzero(data, sizeof(*data))`.
- `getWCL_BSS_INFO()` begins with `bzero(data, sizeof(*data))` and fills the
  complete local `BeaconPayload`.
- The shared BSD bridge had no nested copy-in/copy-out or length marshaling
  before dispatching either selector.
- The same outer/nested boundary caused the previously quarantined
  `SCAN_RESULT` SMAP hazard.  A direct raw probe is intentionally not repeated
  for these larger carriers.

This is a safety and ownership correction, not a claim that the family
transport's raw return code is identical to a local producer's return code.

## Laboratory validation

The signed Tahoe candidate was installed only in the laboratory guest.  On the
5 GHz channel-153/VHT80 AP it completed four normal Wi-Fi OFF/ON cycles.  Each
cycle reached AP authorization and delivered five ICMP packets from
`10.77.0.47` to laboratory-only `10.77.0.1`; the default route remained on
`en0` and the target route on `en1`.  No raw BSD or raw Apple80211 probe was
run, and the external validation host was not contacted or changed.
