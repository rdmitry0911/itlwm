# Retained IGTK and PMF after reconnect — 2026-09-09

## Reproduced behavior

The default IWN image from source `6eb51401`, loaded UUID
`9B5CED54-D384-3460-B90B-B1B5C4C7C8B9`, joined a temporary same-ESS WPA3 AP
on 5 GHz with PMF required. A protected BSS Transition request targeting a
2.4 GHz BSS received status 0, followed by protected source deauthentication
and driver-resident SAE retarget. The target obtained DHCP and passed 7/8
source-bound ICMP packets. This is a successful transition, not lossless roam.

After saved-profile selection returned to the temporary AP, its authenticator
completed another four-way exchange using its retained PMKSA entry. DHCP and
data resumed, but protected BTM and SA Query requests were ignored despite
successful radio ACKs. The public selection command also reported a join
error before the eventual successful association; that timing remains a
separate UI result to investigate.

A read-only FBT observation of that loaded image confirmed protected Action
frames at both `ieee80211_find_rxnode` and `ieee80211_inputm`, with RUN state
and node flags `0x2079a`. MFP and data protection were present; RXMGMTPROT and
TXMGMTPROT were absent. No Action handler ran. Data CCMP snapshot, decrypt and
commit remained successful. Field offsets were obtained from the matching
clean-build DWARF, not assumed from a different kext.

## Cause and change

The authenticated Msg3 carried an unchanged live IGTK. The existing
anti-reinstallation guard correctly skipped another key installation, but
PAE's final publication enabled management protection only when a new IGTK
was installed. The new association node therefore retained no PMF direction
flags when its PTK changed and its IGTK did not.

The PAE plan now carries an explicit retained-IGTK value witness. At the
selected-BSS/association-epoch commit, the BIP owner verifies the exact live
slot and key material and restores both management protection directions.
The key context, key material, and RX/TX IPNs remain unchanged. A replaced,
retired, missing or unequal key fails the handoff. The synchronous Msg3 and
group-message acknowledgement paths use the same guarded rearm helper.

The shared net80211 change covers IWN, IWM and IWX callers; hardware coverage
must still be qualified separately. The local hostap 2.11
`wpa_supplicant_install_igtk` confirms the anti-reinstallation requirement:
an equal in-use IGTK is treated as successfully available without another
driver key installation. Existing Apple reference BSS-transition event
assembly was inspected for event ownership, but does not expose this Intel
software-key bookkeeping and is not claimed to prove the fix.

## Verification state

- The regression test compiles the actual production BIP rearm functions and
  checks both IGTK slots, unchanged replay state, repeat calls, stale nodes,
  mismatched keys, invalid witnesses and unpublished/retired contexts.
- Existing PAE epoch, reconnect, BIP/CCMP lifetime, anti-reinstallation and
  IWN BTM contract checks pass.
- Candidate build, activation and repeated on-air PMF verification are pending.

An initial BTM request for the visible 5 GHz target returned status 7 with no
fresh candidate. A normal public directed scan subsequently saw that BSS.
This scan reliability issue remains open and is independent of the confirmed
missing-PMF-flags defect after reconnect.
