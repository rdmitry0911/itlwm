# Tahoe IWM driver-resident SAE and software PMF — 2026-08-01

## Reference boundary

The Tahoe 25C56 reference keeps WPA3 capability, PMK delivery and reassociation
below WCL:

- `AppleBCMWLANCore::checkForWPA3SAESupport()` queries lower feature `0x41`;
- `AppleBCMWLANJoinAdapter::programPMK()` sends the lower BCM owner selector
  `0x10c`; and
- `AppleBCMWLANCore::setWCL_REASSOC()` applies the requested roam policy and
  submits a bounded V3, V1 or legacy firmware roam command. A successful
  submission is progress, not an immediate association success.

The local reference material is in
`ghidra_output/AppleBCMWLANCoreMac_decompiled.c`,
`ghidra_output/CR-479-pmk-delivery-channel-20260515/`, and
`ghidra_output/aiam_applebcm_reassoc_25C56_20260801/reassoc.txt` on the
reference host.

BCM selector `0x10c` is not an Intel firmware ABI and is not fabricated here.
For IWM, the equivalent lower owner is the driver-resident SAE engine plus the
selected-BSS PMK claim and the real management-ring doorbell.

## Implemented IWM owner

The IWM backend now owns the complete lower SAE association transaction:

1. WCL stages one bounded `CIPHER_PWD` record in a scrubbed private slot.
2. The exact selected-BSS `S_AUTH` event consumes that record into the
   in-kext HNP/H2E SAE engine.
3. Commit and Confirm use IWM's real management descriptor and existing
   TX-completion path. Private engine tickets occupy the independent high-bit
   ticket domain and never leak into the public diagnostic relay.
4. A validated peer Confirm publishes the PMK and PMKID only for the matching
   request generation, association epoch, SSID and BSSID.
5. The Association Request is checked before descriptor construction and
   again while the selected-BSS and engine leaves are held at the real IWM
   scheduler/WRPTR update. A stale owner cannot reach firmware.
6. Explicit WCL reassociation and confirmed 802.11v targets reuse the active
   ESS credential only after port-valid and exact ESS/security checks. The
   real background scan supplies the target BSSID; no same-BSS success is
   fabricated.
7. Stop, detach and hardware reinitialization close callback admission,
   cancel the owner, scrub transient credentials and fence deferred work.

## Software PMF mapping

IWM has no reliable firmware command for the complete PTK/GTK/IGTK transaction
required by a PMF association. The backend therefore publishes MFP capability
only together with its asynchronous software owner. PTK and GTK are prepared
as software CCMP keys and IGTK as a software BIP key; generic net80211 remains
the sole atomic publisher. Protected data and management traffic bypass IWM
hardware crypto so both share the same software key and packet-number
lifetime. Cancellation, reconnect successor and sleep/reset generation fences
prevent an old transaction from publishing into a new association.

## Verification boundary

The IWM source contracts and Tahoe builds verify ownership, exact doorbell
fencing, PMF publication, cancellation and symbol compatibility. The
laboratory VM contains physical IWN hardware, not IWM, so whole-kext IWN
open/WPA2/WPA3 and sleep/wake regression can establish that this cumulative
artifact did not regress the working backend. It cannot be reported as IWM
on-air proof; final IWM WPA3/PMF, multi-AP reconnect and sleep/wake validation
still requires a physical IWM adapter.
