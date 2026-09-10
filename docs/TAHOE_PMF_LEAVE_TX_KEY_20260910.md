# PMF deauthentication after WCL policy cancellation — 2026-09-10

## Reproduced functional boundary

The released `3e73f218` image, UUID
`946F461F-3B19-38AA-8923-7AEF04263ED8`, booted with session
`682F05B7-41A4-4093-818B-0A329D29F4D1`. A saved controlled WPA3 profile
survived reboot and appeared in System Settings after scanning. Selecting it
at 07:24:20 UTC required no password, command-line join or radio toggle.

At 07:24:21 the production encryption dispatcher rejected a protected
deauthentication frame (`fc0=0xc0`, `fc1=0x40`). A read-only function probe
captured this origin:

`WCLNetManager::leaveNetwork -> setWCL_LEAVE_NETWORK -> ieee80211_send_mgmt ->
ieee80211_mgmt_output -> iwn_start -> iwn_tx -> ieee80211_encrypt`.

The selected key pointer equaled the pointer passed to encryption. The serial
message reported a cleared descriptor. This happened **before S3**, while
leaving the old network, not during SAE authentication to the new AP. The
observer read frame-control bytes and object identities, not key material.
It terminated at 07:30:57 with zero diagnostic errors and one encryption
failure; duplicate console lines are not counted as two failed frames.

The new WPA3 connection nevertheless obtained real DHCP and passed separate
1400-byte 20/20 forward and reverse checks. Actual S3 was independently
confirmed after the 07:25:57 request, with temporary USB management/tablet
removed while awake. Wake at 07:27:14 retained the boot and loaded image.
The guest automatically rejoined the same controlled SAE/required-PMF AP,
obtained a new DHCPACK at 07:27:20 and passed another 20/20 in each direction.
The VNC framebuffer remained stale, so no post-wake GUI selection is claimed.
The checks include high individual RTTs and are not latency/throughput gates.

Normal shutdown of only the controlled AP at 07:29:20 restored the ordinary
host connection by 07:29:23. The guest automatically regained its other saved
network by 07:29:26 and passed 10/10. This is a signaled AP shutdown, not a
silent RF-loss or seamless-roaming test. All fixture/observer processes ended.

## Cause and reference boundary

`setWCL_LEAVE_NETWORK` cancels external-PMK eligibility before sending deauth.
That cancellation advances the association epoch and clears the direct SAE
request's configuration, including `IEEE80211_F_RSNON`. It does not itself
retire the installed node PTK or clear its negotiated PMF protection flags.
The old `ieee80211_get_txkey` required the interface's RSN configuration bit
to select the pairwise key. After cancellation it selected the default group
slot instead. The frame was still correctly marked protected by the old
node's PMF state, so encryption rejected the wrong, empty descriptor.

The saved Apple `AppleBCMWLANNetAdapter::leaveNetworkSync` decompile delegates
the actual leave to its firmware command owner and handles the command result
before post-leave configuration. It does not provide a host net80211 key
selector that can simply be copied into this Intel path. The corresponding
software-MAC [Linux TX key selection](https://raw.githubusercontent.com/torvalds/linux/master/net/mac80211/tx.c)
uses the installed peer PTK, independently of a new association's policy;
group-management and group-data key selection are separate.

## Correction and qualification gates

The shared net80211 TX selector now uses the installed peer PTK for already
protected unicast deauthentication, disassociation and action frames when
both negotiated-MFP and TX-management-protection flags are present. Normal
data/WEP selection and multicast GTK/BIP selection remain unchanged. Epoch
cancellation, PMK scrubbing, future-owner rejection and actual key retirement
are unchanged. An absent/retired PTK is still rejected, never replaced by a
GTK or an unprotected frame.

`test_pmf_leave_tx_key.sh` compiles the complete production SAE-policy-clear,
key-selector and encryption-dispatch functions under ASan/UBSan. It verifies
credential-policy scrubbing, the retained PTK, rejection after real descriptor
clearing, and 16,384 selector combinations. Header constants come from the
production headers. Fixture structs model fields, not a kernel ABI; cipher
sinks test dispatch/rejection, not the cryptographic algorithms themselves.
Unchanged `d145c1f5` code compiles and fails the retained-PTK assertion; the
corrected code passes. The regression is included in the full payload suite.

At this source checkpoint, build and loaded-image/on-air qualification were
still pending. The subsequent evidence below supersedes that checkpoint;
IWM/IWX do not gain hardware qualification from the shared selector alone.

## Loaded candidate and independently accepted protected leave

Source `964a90b3` built with all 1085 external symbols resolved. Private
five-member AuxKC preflight and transactional activation preserved the four
companion members. The 07:45 UTC reboot loaded UUID
`EDEEE35F-92DE-3EE3-8F25-3EBA042D1275`, boot session
`666B42A6-249F-4E62-9363-FAF0CE391AD3`, matching Mach-O SHA-256
`a5a808d8b7e644401b56a44554f2c52ca2232c5de33fbd5c4b2c1c2b4a685752`.

The real GUI selected the controlled saved WPA3 profile at 07:49:18 UTC.
The observer recorded request-policy cancellation followed by successful
protected-deauth encryption at 07:49:19. The new AP completed SAE/PMF and
acknowledged DHCP at 07:49:28. Two separate forward checks each lost one of
20 packets; neither reached its reverse check. These are retained failures,
not a complete traffic pass.

At 07:51:32 the GUI selected the ordinary saved network. The observer recorded
another successful protected leave at 07:51:33. The host monitor captured one
protected deauthentication frame to the controlled AP, with zero kernel capture
drops. Independently, hostapd reported `fc=0x40c0`, successfully processed
`reason_code=3`, emitted the deauthentication indication and removed the STA.
This establishes accepted on-air protection, not merely local cipher dispatch.
The ordinary STA then passed a separate source-bound 10/10 check.

Only the monitor interface was subsequently removed; the controlled AP stayed
enabled. GUI reselection at 07:52:59 produced a third successful protected leave
at 07:53:00 and a new DHCPACK at 07:53:05. Separate 1400-byte forward and reverse
checks passed 20/20 each at 07:53:45. Because both monitor presence and association
epoch differ, this comparison does not establish the cause of the earlier loss.
The complete observer ended at 07:55:36 with zero diagnostic errors, zero
encryption failures and empty stderr. Its final trace was copied only after
the process had exited. The bounded AP fixture ended normally at 08:00:04,
restoring the host's ordinary connection and preserving wired management.
The guest automatically regained its ordinary address and passed 10/10.

## Remaining candidate gates

The first post-boot ordinary-network check failed to bind its previously
observed address. Current-boot JSON log correlation subsequently established
the exact sequence: initial association on channel 13 at about -44 dBm,
successful DHCP publication at 07:46:21, `BEST CONNECTED ROAM` at 07:46:42,
address withdrawal at 07:46:48, then `ROAMED` and channel 9 at about -69 dBm
at 07:46:49. Router ARP and DHCP retries thereafter received no response and
the guest published a link-local address. This is a failed post-association
BSS transition, not failure of the initial DHCP exchange on the strong BSS.
Its cause is not established by passing GUI rejoins or by the earlier
weak-5-GHz observation. Log selection uses the exact boot UUID, because
wall-clock-only queries included records from earlier VM boots.

At this checkpoint, open/WPA2 GUI regressions, candidate-specific actual S3
and AP regressions remained required. Subsequent client checks are below.
A backup of an inactive old overlay is incomplete and is not counted as
recovered space.

The reproduced protected-leave defect is now corrected and accepted on air,
but release qualification is incomplete. The public release remains
`3e73f218`; no physical user host was installed or rebooted.

## Candidate GUI WPA2/open and actual S3 service recovery

The real System Settings pane requested a password for a new WPA2 Personal
profile on the external OpenWrt control network. Submission at 08:05:53 UTC
completed the GUI association request successfully at 08:05:57 on channel
161, about -33 dBm. A fresh DHCP exchange reached BOUND at 08:06:02 and IPv4
publication at 08:06:04. Separate source-bound 1400-byte checks passed 20/20
guest-to-gateway and 20/20 host-Wi-Fi-to-guest. No command-line join, radio
toggle or reboot replaced the GUI operation. The legacy command-line network
name query reported not associated despite GUI, DHCP and traffic success;
that query's privacy/API behavior is not classified as a driver defect here.

After three further byte-identical redundant release ZIPs were removed while
retaining their rollback copies, the unchanged storage guard admitted a
bounded open-AP fixture at 08:08:36. Two earlier start attempts had stopped
at the free-space guard before any wireless mutation; they are not driver
failures. The saved open profile was selected through GUI. External DHCPACK
at 08:09:49 and independently awaited 20/20 forward and 20/20 reverse checks
completed at 08:10:33. The external AP reported open security with no PMF.

The candidate's power configuration independently reported `hibernatemode=0`
and no sleep image. With host free space stable at about 1.54 GiB, the guarded
S3 helper verified the exact boot/image, open-network address and two packets.
USB management and tablet were removed while awake at 08:11:04. The 08:11:25
request reached actual ACPI SLEEP and QEMU suspended state. Wake at 08:12:29
produced ACPI S3 WAKE without another boot. Fresh USB devices were attached
only after that wake record was verified.

The guest automatically selected its other saved WPA3/required-PMF network,
not the open test AP. Association completed about 08:12:36, DHCP reached
BOUND at 08:12:37 and IPv4 was published at 08:12:39. Later independent
forward and reverse runs passed 20/20 each on the same boot and kext UUID.
Individual RTTs include approximately one second; this is not a latency gate.
The first freshly attached USB SSH probe timed out during enumeration; a
subsequent probe succeeded. Neither that transport timeout nor the other
profile's recovery is counted as a passed post-S3 open-network check.

The framebuffer retained the pre-sleep 08:11 GUI image, so no post-wake GUI
selection is claimed. The open fixture ended normally at 08:15:20 and restored
the host's managed connection and wired route. The complete candidate serial
interval through these client checks has no matched driver panic, firmware
fatal, device-timeout or unset-software-key diagnostic. AP regressions and
the full post-S3 GUI/profile matrix remain open; publication is still held.

## Same-candidate post-S3 native AP regressions

On the same boot/image after S3, native system Internet Sharing ran WPA3,
WPA2 and open sequentially. Each mode obtained a real external-client DHCP
lease, independently verified negotiated security, passed 20/20 forward and
bridge-scoped cold-neighbor 10/10 reverse packets, and routed HTTP through the
guest's independent upstream. WPA3 reported SAE group 19, required PMF and BIP.
No bridge rewrite, daemon restart, Wi-Fi toggle or reboot separated the modes.
These operations used the native sharing producer, not a claim of post-sleep
GUI operation on the stalled framebuffer.

WPA3 stop at 08:19:50 and WPA2 stop at 08:22:37 each passed a separate
15-second dwell, showed the retired bridge detached with zero I/O references,
and retained the ordinary STA at 10/10. Open stop at 08:25:29 also retired its
bridge to zero I/O references. Its hard-coded old-address check failed to bind;
that is not recorded as a passed old-profile return. Exact current-boot logs
instead show automatic selection of the other saved WPA2 profile at 08:25:34,
association at 08:25:38 and a new DHCP address published at 08:25:43, before
that failed old-address probe. Independent checks on the actual current STA
address then passed 20/20 in both directions. The host's normal managed
connection and wired route were restored. The bounded HTTP fixture terminated.

The complete 13,177-line candidate serial interval audited after all three
AP modes has no matched driver panic, firmware fatal, device timeout,
unset-software-key diagnostic or AP TX gate failure. The only production-code
change from the previous release is the shared 17-line TX key selection fix;
these runs qualify its reproduced protected-leave correction with client,
sleep and AP service regressions, not full roaming/profile equivalence.

The prepared release ZIP SHA-256 is
`eaa6ef1c077dfb7fc309f0781722f8ed3aa6594ad3aa01b127192a021974f9ab`.
Its extracted Mach-O and Info.plist were byte-compared with the frozen image;
the Mach-O hash and UUID match the loaded candidate above. Host download and
ZIP integrity verification also passed. Publication and fresh public-download
verification remain a separate next operation. The post-roam DHCP failure,
earlier monitored traffic losses, full post-S3 GUI/profile matrix and IWM/IWX
hardware qualification remain explicitly open.
