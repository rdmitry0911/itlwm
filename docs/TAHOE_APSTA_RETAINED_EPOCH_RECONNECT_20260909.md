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

Source commit `98dc62ee` built successfully with all 1083 BootKC symbols
resolved. Private AuxKC preflight and transactional activation passed. The
disposable IWN/6235 guest loaded UUID
`6E20810C-101D-313E-AD15-56ED0BD8E709`; the frozen candidate's Mach-O SHA-256
is `948d79ae2f053b2d4315257d50f37d935f46577462fc40659f40f855ec28a159`.

The first credentialed public selection completed in 10 seconds without an
error and retained DHCP/data. A repeated selection still failed with `-3912`
after 47 seconds, followed by background recovery. Crucially, FBT on that
failed repeat showed the old handoff reservation being rejected after the
leave/epoch advance, the real SCAN preflight being admitted, and the first
`setWCL_ASSOCIATE` entering `setWCL_ASSOCIATEImpl` and the driver SAE owner.
It no longer acknowledged that first association without executing it.
Later authentication of a 5-GHz candidate timed out. Thus the stale-epoch
defect is closed, but the full public reconnect failure and the independent
idle-STA shared-channel roam restriction remain open.

## APSTA and sleep regression on the same candidate

The role-7 public create/up/POWER/CHANNEL/HOST_AP_MODE sequence started a
pure-SAE AP while the primary held its WPA3 link. A requested channel 9 was
correctly aligned to the live primary's channel 13. An external AX211
completed SAE group 19, required PMF and BIP. With an isolated static test
subnet, client-to-AP traffic passed 20/20, AP-to-client passed 5/5, and the
simultaneous primary STA-to-gateway path passed 5/5.

With both roles active, `pmset sleepnow` reached actual S3: the serial console
recorded `ACPI SLEEP` and the owned QEMU monitor reported `paused (suspended)`.
Only that monitor received `system_wakeup`; the guest recorded `ACPI S3 WAKE`
and retained its boot epoch and loaded UUID. The driver restored its primary
and replayed the AP PAN context. The external client's test profile was not
set to autoconnect, so it selected its ordinary network during sleep; explicit
reselection of the restored AP completed a fresh SAE/PMF handshake. Traffic
again passed 20/20 client-to-AP, 5/5 AP-to-client and 5/5 primary-to-gateway.
This proves AP service recovery, not uninterrupted or automatic client roam.

A normal AP stop reached the lower PAN terminal and restored the retained
primary link/RSN state; the primary passed 10/10 packets across that stop.
No driver panic or firmware fatal appeared in this interval. The temporary
client profile and static AP address were removed, and the host's ordinary
managed connection was restored. The separate USB Ethernet diagnostic path
also recovered after S3 and remains available for subsequent lab checks.

This regression is IWN hardware coverage with static addressing on the
role-7 AP. It does not re-prove the full Internet Sharing DHCP matrix or
IWM/IWX hardware behavior for this candidate.

Release archive SHA-256:
`5691d848ca125fba4c32b864f458347293777d8f23523d2da2fa9201cfb71e30`.
