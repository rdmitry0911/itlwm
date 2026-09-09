# APSTA retained-association epoch and user reconnect — 2026-09-09

## Reproduced user-facing failure

The IWN/6235 guest loaded source `5b4929e3`, UUID
`90A9F9AA-2E9A-302F-A4C7-4FA29A14730C`, with a working WPA3/PMF link after
S3. The idle role-7 interface existed, but neither an AP nor Internet Sharing
was active. A previous shared-channel-filtered WCL reassociation request had
armed the AP owner's primary handoff reservation.

A normal `networksetup` association with the correct WPA3 password failed:
airportd logged `hasPassword=1`, two pairs of ten-second association timeouts,
and the command reported `-3912`. The background saved-profile auto-join
eventually restored the link after the public command ended. The command's
shell exit status was zero despite its printed error; exit status alone is
therefore not an association test.

Bounded FBT observation showed `ieee80211_deselect_ess` followed by an APSTA
handoff scan veto. The first `setWCL_ASSOCIATE` returned success without
entering `setWCL_ASSOCIATEImpl`. WCL then issued `JOIN_ABORT` ten seconds later.
The serial trace confirmed retained-primary WCL/carrier suppression and
`ieee80211_encrypt` rejecting an unset software key. Later attempts reached
the actual SAE owner, but timed out refreshing the candidate before eventual
background recovery.

This is separate from a password-less command's `-3900`: airportd explicitly
logged `Apple80211Associate2: Missing password for upper auth 4096` before its
association reached the driver. That command is not equivalent to GUI
saved-profile selection. No driver-side fake success or password bypass is
introduced for that userland error.

## Cause and change

`ieee80211_deselect_ess` calls `ieee80211_disable_rsn`, which retires the
association epoch before the generic SCAN state transition. The old APSTA
reservation checked only RUN and port-valid. Those fields can still describe
the old BSS while its security owner has already been invalidated, so the
reservation vetoed the very SCAN transition needed to leave that state and
then swallowed the new WCL association.

The AP owner now captures the association epoch for both the handoff/carrier
reservation and the one post-stop WCL replay. All consumers require the same
nonzero epoch, in addition to their existing state, port and BSSID checks.
An actual unchanged-primary PAN transition still qualifies. Credential
replacement, leave, abort or reset cannot carry the exemption into a successor
association, even when the BSSID is unchanged.

The epoch is acquired atomically. This local AP-owner accessor also works
while DVM temporarily exposes HOSTAP with a retained STA RXON; the generic
STA-only epoch accessor's opmode admission remains unchanged.

The reference CoreWLAN association/reply path was inspected in
`aiam_corewlan_current_network_25C56_20260806/06_decomp/` on the reference
host. The recovered `AppleBCMWLANJoinAdapter::abortFirmwareJoinSync` in
`AppleBCMWLANCoreMac_decompiled.c` submits the lower disassociation and emits
the requested abort completion. This supports keeping a real leave/abort
distinct from a retained AP transition; it does not prove Intel-specific
epoch bookkeeping.

## Verification boundary

The regression compiles the production AP-owner helper and consumer methods.
It checks preserved same-epoch handoff, one-shot consumption, old-epoch
rejection with RUN/port-valid still set, same-BSSID successor rejection,
different-target rejection, temporary DVM HOSTAP opmode, and invalid owners.

Build, candidate activation and post-change on-air qualification are pending.
This document does not yet claim that the full public reconnect failure or
the independent idle-STA shared-channel roam restriction is closed.
