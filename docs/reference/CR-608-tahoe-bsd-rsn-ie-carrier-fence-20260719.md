# CR-608 — Tahoe BSD RSN IE carrier fence

## Scope

The public `APPLE80211_IOC_RSN_IE` GET (46) carries a fixed `0x108`-byte
`apple80211_rsn_ie_data`: version, length, and up to 257 RSN IE bytes.  Tahoe's
local BSD bridge passed its nested `req_data` address directly to
`getRSN_IE`, which writes that carrier without consulting `req_len`.

The recovered 25C56 public GET instead enters an interface gate, resolves a
vendor owner, and calls that owner's virtual method; no owner returns
`0xe082280e`.  The BSD bridge now delegates only the GET direction to
`IO80211Family` before the local dispatcher, retaining the ownership boundary
for the raw outer ioctl.

## Preserved paths and limits

This change does not hard-code a GET return value or payload.  It does not
claim that the family transport's result is byte-for-byte equivalent to the
former local helper.  The local producer remains available to kernel-owned
callers.

`RSN_IE` SET remains the recovered public success no-op and is not fenced.
The Tahoe card-specific route admits this selector only for SET.  Hidden
association continues to use its separate bounded RSN pointer/length handoff
and `setAssocRSNIE` path; it is not a public BSD GET carrier.

## Laboratory validation

The source-equivalent candidate was built, signed, and loaded through an
explicit AuxKC only in the laboratory guest.  Its AuxKC was created with the
candidate at the final `/Library/Extensions/AirportItlwm.kext` path before
reboot.

It completed four separate normal Wi-Fi OFF/ON cycles on the 5 GHz
channel-153/VHT80 laboratory AP.  In every counted cycle the AP reported
`[AUTH][ASSOC][AUTHORIZED]`, and five of five ICMP packets travelled only from
`10.77.0.47` to `10.77.0.1`; the default route remained on `en0` and the
laboratory route on `en1`.  The textual SystemConfiguration network-state
query was not used as an association gate because it can lag the AP
authorization and protected-traffic state.

No raw BSD or raw Apple80211 probe was executed, and the external validation
host was not contacted or changed.
