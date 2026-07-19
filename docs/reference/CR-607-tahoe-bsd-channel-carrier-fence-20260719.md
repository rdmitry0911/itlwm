# CR-607 — Tahoe BSD channel-list carrier fence

## Scope

`APPLE80211_IOC_SUPPORTED_CHANNELS` (27) and
`APPLE80211_IOC_HW_SUPPORTED_CHANNELS` (254) share the Tahoe local
`getSUPPORTED_CHANNELS` producer.  The public
`apple80211_sup_channel_data` carrier has 128 entries, while that producer
walks all 255 net80211 channel slots and increments `num_channels` without an
entry-capacity or `req_len` check.

The Tahoe BSD bridge owns only the outer `apple80211req`; it does not marshal
the nested `req_data` address before reaching the producer.  Both GET selectors
therefore take a BSD-only family-transport fence before the local dispatcher.
The local producer is otherwise unchanged for kernel-owned callers.

## Evidence and limits

This is an ownership and fixed-array safety fence, not a reconstructed
channel-list implementation.  The change removes the local raw nested-carrier
write and its possible overrun from BSD ingress.  It deliberately does not
invent a truncation policy, result payload, or return code for the family path;
none is claimed to match the reference.

Tahoe's card-specific routing table does not admit either selector, so the
fence cannot divert an existing card route.  `HW_SUPPORTED_CHANNELS` is
included because its local helper delegates to the same unbounded producer.
SET direction is unchanged.

## Laboratory validation

The source-equivalent candidate was built, signed, and loaded through an
explicit AuxKC only in the laboratory guest.  The AuxKC was created with the
candidate at its final `/Library/Extensions/AirportItlwm.kext` path; building a
collection from a staging path produced a collection that booted but did not
autoload the bundle, so that materialization result was discarded rather than
treated as a driver regression.

The restored `150E333F` control and the candidate each completed four separate
normal Wi-Fi OFF/ON cycles on the 5 GHz channel-153/VHT80 laboratory AP.  In
every counted cycle the AP reported `[AUTH][ASSOC][AUTHORIZED]` and five of
five ICMP packets travelled only from `10.77.0.47` to `10.77.0.1`; the default
route stayed on `en0` and the laboratory route on `en1`.  The textual
SystemConfiguration network-state query was not used as an association gate:
it can report no associated network while the AP authorization and protected
traffic gates both show an active link.

No raw BSD or raw Apple80211 probe was executed, and the external validation
host was not contacted or changed.
