# Restrict WCL shared-channel filtering to an accepted AP lifecycle

## Reproduced policy defect

The default Tahoe product publishes `ap1` during initialization. Its existence
is not a request to run an AP. Nevertheless, `setWCL_REASSOC` always queried
IWN's required STA+AP channel, which deliberately reports the current primary
channel even before PAN startup. An ordinary STA request could therefore lose
all of its off-channel candidates, acknowledge a no-op and arm an AP handoff
reservation while neither AP nor Internet Sharing was active.

The association-epoch fix prevents that reservation from surviving a real
leave, but does not make the filtered-out roam request execute. This is a
separate user-visible restriction on changing BSS/channel.

The reference `AppleBCMWLANNetAdapter::sendReassocCommandLegacy` in the
25C56 `aiam_applebcm_reassoc_25C56_20260801/reassoc.txt` export checks primary
association, converts the supplied channel list and forwards `WLC_REASSOC`.
It does not justify restricting an ordinary STA to its current channel.
This reference establishes the upper request behavior, not Intel's lower
single-channel concurrency implementation.

## Change

The WCL producer now queries a controller-level primary-roam constraint.
That query admits the HAL's required shared channel only when the lower AP
context exists or the AP owner has an accepted start, recovery or stop in
progress. A merely allocated owner, saved AP profile or old STA retention
token is not AP intent. A terminal owner cannot constrain a successor.

The HAL query itself is unchanged: AP startup/channel admission still needs
the associated primary channel before a PAN context exists. Running APs and
accepted asynchronous transitions retain that hardware constraint; backends
which return zero for it retain their prior behavior.

In particular, a WCL request preceding any accepted HOST_AP_MODE must keep its
real roam targets. A later userspace AP operation cannot retroactively make
that earlier request an AP handoff. This removes the former unsupported
inference from an off-channel candidate list alone.

## Verification boundary

The UBSan regression compiles the actual owner query, controller query and
complete production `setWCL_REASSOC` body, with the actual public and common
request structures. It verifies off-channel target/score/policy preservation
and real scan dispatch for an idle STA, retained-token non-admission, each AP
lifecycle flag, lower-only AP context, terminal owner, multi-channel backend,
AP-only busy/filtered-empty outcomes, literal-empty input and null input.
Existing APSTA shared-channel/start, association-epoch, WCL roam/terminal and
public BSSID-pin tests pass.

This change has not yet been built, activated or runtime-qualified. In
particular, the standard AP producer and repeated public selection must be
retested on the new artifact. It does not address the separate missing-BSS
receive/scan issue or claim full GUI/IWM/IWX hardware parity.
