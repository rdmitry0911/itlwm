# Tahoe PAE selected-BSS snapshot

## Scope

This layer stores a fixed-byte identity for the BSS that net80211 actually
selects and copies into `ic_bss`: association epoch, BSSID, length-delimited
SSID, and already-normalized SAE scan facts. It is writer-only. It does not
expose a UserClient selector, retain a node or raw IE pointer, alter BSS
selection, enable SAE Algorithm 3, change RSN output, or enable PMF/IGTK.

## Canonical producer and lifetime

`ieee80211_node_join_bss()` first obtains a replacement epoch and private
owner token, then copies the selected BSS into `ic_bss`, calculates fixed
profile facts, and captures only with that exact token. This covers targeted
selection, retry, watchdog alternate-BSS selection, direct legacy association,
and deferred background roam at their common actual-selection point. Request
carriers (`ASSOCIATE`, `WCL_ASSOCIATE`, TahoeOwnerRegistry) are deliberately
not authoritative: they express intent and can be rejected, deferred, or
differ from the final cached BSS.

The default `ic_node_copy` uses a private non-cancelling cleanup because its
caller already owns this controlled replacement. Every ordinary cleanup and
all other existing association fences instead advance the epoch, clear the
owner token, and invalidate the record. Capture holds the leaf writer lock and
requires both the current epoch and owner token to equal its expected epoch;
therefore a cancellation between replacement admission and post-copy capture
cannot resurrect an old BSS under a newer epoch. A future action may use a
snapshot only when its nonzero epoch equals the current association epoch.

## Concurrency boundary

The record is not yet read outside net80211. The leaf lock serializes only
epoch advancement and fixed-record publication; it is not a general lock for
copying a multi-byte record across the Skywalk IOCommandGate/PLTI boundary. A
future consumer must add one serialized copy-out contract and re-check the
epoch immediately before acting. That prevents node-teardown races and avoids
treating a request-side BSSID as the authenticated selected BSS.

The lock deliberately survives `ieee80211_ifdetach()`: closed driver queues
can still reject late cancellation work. Each HAL frees it only from its final
`free()` path after controller, interrupt, task, and higher lifecycle fences
have completed. It must not be moved to an earlier detach or queue-drain path,
which would reintroduce a load-to-lock versus free race.

## Verification

The portable identity test covers binary SSIDs, bounded length, zero/stale
epochs, and exact BSSID matching. The association-epoch contract checks the
post-copy ordering, all existing invalidation edges, and absence from scan or
WCL/public request ingress. The Tahoe isolated gate builds and resolves the
kext against BootKC without installing, loading, or rebooting it.
