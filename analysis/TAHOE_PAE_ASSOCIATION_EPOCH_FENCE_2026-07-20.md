# Tahoe PAE association-epoch fence

Date: 2026-07-20

## Purpose

This layer adds a monotonic STA association epoch to net80211 before any
pure-SAE or PMF key lifecycle is enabled. It is a cancellation fence, not an
authentication feature: future relay events, q0 continuations, firmware-key
completions, and port/link transitions must carry the epoch captured for their
attempt and discard themselves if it has changed.

## Invalidation edges

`ieee80211_pae_assoc_epoch_begin()` atomically advances a nonzero epoch for
STA mode. `ieee80211_pae_assoc_epoch_begin_replacement()` does the same for
the one controlled current-BSS replacement and returns a private owner token.
Future asynchronous consumers must snapshot the epoch with
`ieee80211_pae_assoc_epoch_current()` (an acquire load), never by reading the
volatile field directly. The epoch advances before:

- selected-BSS replacement in `ieee80211_node_join_bss()`, before node-copy
  can release or overwrite the old BSS state;
- an accepted deferred background-roam handoff, after the old-BSS deauth was
  successfully queued and before its `ni_unref_cb` can switch BSS;
- an accepted OTA WCL reassociation request, before the lower owner sends its
  management frame. Its successful same-BSS response retains that request
  epoch; the response must not create a second attempt;
- terminal WCL reassociation failure, before the 0xcf callback is published
  (including synchronous send failure and RUN-state management timeout); the
  owner is closed even when no event handler is installed;
- rejected current-BSS auth/association responses, and received current-BSS
  deauth/disassoc frames, before terminal callbacks or roamscan/stay-auth
  early returns;
- `ieee80211_disable_rsn()` and direct `SIOCS80211WPAPSK` replacement/removal,
  before PSK/RSN configuration is changed;
- destructive external-PMK cancellation, including the no-controller and
  command-gate-unavailable fallback clear;
- cleanup of the current STA BSS, but not cleanup of arbitrary cache nodes.

## Controlled selected-BSS publication

The selected-BSS snapshot is writer-only. A replacement begins under a small
leaf lock, which advances the epoch, installs the matching owner token, and
invalidates the record. The default node-copy uses its internal non-cancelling
cleanup so it does not accidentally revoke the token it is meant to complete.
After the copy, capture takes the same lock and publishes only if its expected
epoch is still current and still owns the token. Every ordinary cancellation
uses the same lock, advances the epoch, clears the token, and invalidates the
record. Thus a cancellation that races post-copy capture wins fail-closed:
the stale capture cannot republish its BSS under the new epoch.

The leaf lock remains allocated through `ieee80211_ifdetach()` and is freed
only by the terminal HAL `free()` after controller, interrupt, task, and
higher lifecycle admission have been drained. It is deliberately not a
cross-context reader protocol; a future reader must add its own serialized
copy-out and re-check the epoch immediately before acting.

The `ieee80211_new_state` wrapper also advances the epoch before the driver
callback for every STA state request except one forward chain:

```
SCAN -> AUTH -> ASSOC -> RUN
```

Thus retry, scan, reset, stop, detach, and reassociation transitions
invalidate outstanding work before an asynchronous driver `newstate` task can
run. Direct IWX and IWM `sc_newstate(INIT)` stop paths, generic interface
detach, and STA-to-non-STA media changes fence explicitly because they bypass
or precede the wrapper. The counter is atomic acquire/release, skips zero on
wrap, and starts at zero on interface attach.

## Deliberate non-claims

This change does not enable or route any of the following:

- SAE Algorithm-3 authentication or the new UserClient selectors;
- `IEEE80211_C_MFP`;
- `ic_set_key_wait` or `ic_eapol_key_input`;
- q0 work, IGTK/key install, Msg4/GroupMsg2 ordering, port validity, or link
  publication.

The existing AX211 MFP path remains quarantined (`ic_set_key_wait == NULL`,
`ic_eapol_key_input == NULL`, and `iwx_set_key_wait()` returns
`EOPNOTSUPP`). The epoch is only the prerequisite needed to make a later
single-owner transaction reject stale completion safely.

## One-pass local proof

`scripts/test_net80211_pae_epoch_contract.sh` combines static wiring checks
with a deterministic state matrix. It validates selection, the three allowed
forward transitions, terminal receive failure, direct credential/reset,
driver stop/detach/mode exit, deferred roam, WCL request/failure ordering,
current-BSS-only cleanup, atomic zero-wrap behavior, and the unchanged MFP
quarantine. The script is called by the aggregate SAE quarantine contract.

The isolated test gate may compile and link a candidate kext, but never
installs, loads, reboots, or exercises it on a host.
