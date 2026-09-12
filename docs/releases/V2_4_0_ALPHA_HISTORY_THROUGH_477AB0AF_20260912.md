# Archived alpha release: 477ab0af / UUID842B

This is the preceding release body captured while preparing the combined
deferred-WCL handoff and post-target cancellation release. The intermediate
9B5B handoff-only image was not published. Artifact and current-status
statements in the archived body below are historical. Text is preserved;
the final blank line is normalized.
The previous ZIP is retained separately for rollback; older history remains
linked at the end of the archived body.

---

## Driver-resident SAE peer-response retry — 2026-09-12

**Limited alpha, not full reference parity or zero-loss roaming certification.**
The new image still loses one of20 guest-to-host packets in the strict Wi-Fi-only
post-S3 test (reverse20/20). The same boot, WPA3 and DHCP recover without off/on,
but that failed data gate is not promoted to PASS. Intermittent discovery,
saved-profile selection, other authentication failures and steady-state losses
remain open. The first native WPA2 STA selection also fails before
authentication: the driver rejects its2GHz scan with Device Busy, then the
framework scans only5GHz and cannot find the channel9 AP. This is a retained
failure, not a successful WPA2 STA gate. Physical hardware qualification here
is IWN/Intel6235, not IWM/IWX.

### Change

Source `477ab0af139c96ff485d5f2a3c69152ee82aacb6` keeps the current public
SAE Commit/Confirm in the driver and repeats it within the same exchange when
an ACKed transmission receives no peer response. It uses a receipt-anchored
two-second deadline, at most three actual successful transmissions per body,
and new transport tickets. Password-derived state, BSSID and exchange ownership
are retained. Nonzero TX results are not reclassified as successful delivery.
The shared core and IWN/IWM/IWX worker/timer paths are included.

### Exact-image evidence

- Separately omit one Commit and one Confirm: the same-exchange retry completes
  SAE, DHCP and20/20 packets each direction. Forward-all control also passes.
- Omit every Commit: each observed target exchange stops at three transmissions,
  retires through native timeout and never reports target success. macOS later
  reconnects to saved LabAP; this is fallback, not same-profile recovery.
- Linux and Tahoe execute the complete crypto core (30 scenarios), each of the
  three production workers (26 per family), the timer helper (12), existing
  failure checks and both complete payload aggregates. Firmware/IOKit boundary
  doubles in source tests are not radio evidence. Existing IWM/IWX peer-rejection
  cleanup tests still fail their separate semantic requirement.
- Actual S3/wake retains the same image/boot and restores WPA3/DHCP, with the
  strict packet-loss failure above. Subsequent native AP open/WPA2/WPA3 tests
  pass external AX211 negotiation/DHCP,20/20 forward and10/10 cold-ARP reverse,
  exact118-byte HTTP through USB/NAT, normal stop and10/10 restored STA traffic.
- Saved WPA3 off/on and subsequent20/20 bidirectional traffic pass. Native open
  and WPA3 STA selection, DHCP and20/20 each direction also pass. The WPA2
  discovery failure above remains visible separately.

Two native LabAP transitions after S3 reach their requested BSS through
one SAE exchange each, retaining248/250 and244/250 packets in the first
pair,247/250 each way in the reverse pair, and one/two encapsulation drops.
These are target-admission successes, not seamless or zero-loss roaming.

AP tests start the AP after waking; they do not certify an already active AP
across sleep, concurrent Wi-Fi STA uplink plus AP, the full mouse-driven GUI
matrix, all SAE/H2E interoperability variants, ad hoc or IWM/IWX radio parity.

### Artifact

The archive packages the already installed/tested bundle without rebuilding.
Full source manifest and installed/build/extracted bundle comparisons pass.

- Mach-O UUID: `842B08A6-AB1B-394A-9490-C10EB1F1D9D2`.
- Mach-O SHA256: `0e0aa25caca2f9ce9b630ad37aa9d9c53730ee939128d4fe30031b86927fcf80`.
- ZIP: `AirportItlwm-Tahoe-v2.4.0-alpha.kext.zip`,15,693,832bytes.
- ZIP SHA256: `f1fa87afb14519112deccb45e7db8122b1b3eefa99f707f2d4c101b4e51f749c`.

[Implementation, reference boundaries and detailed positive/negative runtime evidence](https://github.com/rdmitry0911/itlwm/blob/tahoe-iwn-sae-bridge-runtime/docs/TAHOE_SAE_PEER_RESPONSE_RETRY_20260912.md).

### Earlier history

[All prior release notes through f170870d, including their limitations and historical artifact identities](https://github.com/rdmitry0911/itlwm/blob/tahoe-iwn-sae-bridge-runtime/docs/releases/V2_4_0_ALPHA_HISTORY_THROUGH_F170870D_20260911.md).
History was moved out of this page because accumulated notes approached the
GitHub size limit. Only the artifact identity above describes this update.
