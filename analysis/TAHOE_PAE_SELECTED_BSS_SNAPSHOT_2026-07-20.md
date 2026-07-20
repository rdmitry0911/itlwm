# Tahoe PAE selected-BSS snapshot

## Scope

This layer stores a fixed-byte identity for the BSS that net80211 actually
selects and copies into `ic_bss`: association epoch, BSSID, length-delimited
SSID, and already-normalized SAE scan facts. It is writer-only. It does not
expose a UserClient selector, retain a node or raw IE pointer, alter BSS
selection, enable SAE Algorithm 3, change RSN output, or enable PMF/IGTK.

## Canonical producer and lifetime

`ieee80211_node_join_bss()` captures only after `ic_node_copy()` has replaced
the current BSS and `ni` has been rebound to `ic_bss`. This covers targeted
selection, retry, watchdog alternate-BSS selection, direct legacy association,
and deferred background roam at their common actual-selection point. Request
carriers (`ASSOCIATE`, `WCL_ASSOCIATE`, TahoeOwnerRegistry) are deliberately
not authoritative: they express intent and can be rejected, deferred, or
differ from the final cached BSS.

Every `ieee80211_pae_assoc_epoch_begin()` atomically revokes the snapshot by
zeroing its epoch before the new epoch is returned. Its fixed payload is then
cleared and repopulated only by the one post-copy join producer. The join
producer reads the current epoch only after
`ic_node_copy()`, because default node cleanup itself begins another epoch.
Consequently a future action may use a snapshot only when its nonzero epoch
equals the current association epoch; retry, teardown, credential reset, stop,
deauth/disassoc, and OTA roam fail closed through the existing fence.

## Concurrency boundary

The record is not yet read outside net80211. The epoch has release publication
semantics, but it is not a general lock for copying a multi-byte record across
the Skywalk IOCommandGate/PLTI boundary. A future consumer must add one
serialized copy-out contract and re-check the epoch immediately before acting.
That prevents node-teardown races and avoids treating a request-side BSSID as
the authenticated selected BSS.

## Verification

The portable identity test covers binary SSIDs, bounded length, zero/stale
epochs, and exact BSSID matching. The association-epoch contract checks the
post-copy ordering, all existing invalidation edges, and absence from scan or
WCL/public request ingress. The Tahoe isolated gate builds and resolves the
kext against BootKC without installing, loading, or rebooting it.
