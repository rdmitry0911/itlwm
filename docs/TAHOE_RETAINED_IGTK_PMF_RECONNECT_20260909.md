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
- Source commit `5b4929e3` built successfully; all 1083 BootKC symbols resolved.
- Private AuxKC admission and transactional activation both passed. The guest
  reboot loaded UUID `90A9F9AA-2E9A-302F-A4C7-4FA29A14730C`, matching the
  candidate Mach-O SHA-256
  `0602876224834e19ce16c6a220b6ab2fa47a9ad034ad3477714af1992ad4bca0`.
- The exact on-air sequence was repeated: temporary 5 GHz WPA3 source,
  protected BTM to the 2.4 GHz target, then saved-profile return to the
  original source. The live rearm helper returned 0; observed PMF node flags
  included both directions (`0x207fa`).
- After return, the source AP received a protected SA Query response and a
  protected BTM response. Another request to the 2.4 GHz target received
  status 0 and completed SAE, DHCP and 8/8 source-bound ICMP exchanges.
- The candidate entered true S3 (`ACPI SLEEP`, QEMU suspended) and resumed
  through `ACPI S3 WAKE`. Wi-Fi became reachable on the seventh one-second
  probe; subsequent source-bound runs passed 8/8 packets in each direction.
  WPA3, the DHCP address and the loaded candidate UUID were retained.
- The temporary host AP and DHCP processes were stopped by their recorded
  PIDs, its virtual interface was removed, and the host's ordinary managed
  connection remained active.

The fix closes the reproduced retained-IGTK PMF loss after reconnect. It does
not close the public selection error preceding eventual connection, the
5 GHz targeted-scan issue below, or all IWM/IWX hardware coverage.

Release archive SHA-256:
`2659b255ca40407d7622566ccde701a92184cfcd11ab659e70c95b3694eb6d9f`.
The archive's Mach-O matches the runtime-qualified candidate exactly.

An initial BTM request for the visible 5 GHz target returned status 7 with no
fresh candidate. A normal public directed scan subsequently saw that BSS.
This scan reliability issue remains open and is independent of the confirmed
missing-PMF-flags defect after reconnect.
