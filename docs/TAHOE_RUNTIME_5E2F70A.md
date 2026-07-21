# Tahoe private-candidate runtime record: 5e2f70a

This record binds committed source `5e2f70a52eaa53b6c2633e95af76452dfe3e3774`
and its committed Tahoe source identity to a private candidate, private
five-member AuxKC admission, transactional activation, a guest-only reboot,
and post-boot installed/loaded identity equality.  No semantic release tag
or asset was created or changed.

Raw artifacts remain local-only.  This committed record contains no wireless
identity, credential, address, route, packet, or raw client output.

## Candidate admission and activation

Private AuxKC admission passed without canonical collection mutation.
Transactional activation preserved the canonical five-member member set and
performed no direct kext load or unload.  After the guest-only reboot, both
the installed and loaded driver matched the committed-candidate digest and
Mach-O UUID.

## Bounded runtime observations

The loaded candidate passed four bounded radio OFF/ON cycles.  Every cycle
met stable authorization and a fresh-association invariant; five permitted
local data-plane probes per cycle were all received.  Existing management,
default-route, direct-laboratory route, and address invariants were retained.
The runner issued no explicit join, address, route, or state-mutating DHCP
command.

## BSD SET fence scope and non-claims

The static no-backend BSD SET fence contract passed for the local IE and
WOW_TEST helper boundary.  This runtime experiment did not execute either raw
BSD selector, and does not establish a fix for a prior panic.  It only shows
that the normal saved-profile regression scenario remained healthy after the
fence.

This is only a saved-profile A2DF regression result.  It does not prove raw
BSD user-pointer rejection, Apple return-code parity for those selectors,
pure SAE/WPA3-SAE, PMF-required association, PMF q0 completion, EAPOL
success, IGTK installation, WCL link publication, generic reachability, or
physical/remote-host behavior.

The aggregate-only record is
`evidence/runtime/tahoe_a2df_5e2f70a_four_cycle.json`.
