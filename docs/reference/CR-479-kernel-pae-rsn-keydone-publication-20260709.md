# CR-479 kernel PAE RSN key-done publication

## Reference evidence

- `IO80211InfraInterface::handleKeyDone(bool, bool)` is exported by Tahoe
  `IO80211Family` at `0xffffff80022e6f9c`.
- Raw recovery in `analysis/handlekeydone_disasm_2026_04_28.txt` proves the
  direct-call C++ signature is `void handleKeyDone(bool, bool)`.
- Forced decompile on the Ghidra host
  `~/Projects/ghidra_output/cr312_io80211_lsu_body_20260507_1331_forced.c`
  shows `handleKeyDone` selects the `IO80211RSNDone` property value from the
  first bool and logs the second bool as the rekey argument.
- The same forced decompile shows
  `IO80211InfraInterface::setWCL_LINK_STATE_UPDATE(...)` calls
  `handleKeyDone(false, false)` on the link-state reset/update branch.
- Exhaustive disassembly in
  `~/Projects/ghidra_output/pvg_x86_retry_bootkc_24/03_decomp/guest_bootkc_BootKernelExtensions/functions/0xffffff80022e1db0_FUN_ffffff80022e1db0.c`
  identifies `IO80211InfraInterface::handleSupplicantEvent(void*, unsigned long)`:
  for a 0x28-byte successful supplicant event it calls
  `handleKeyDone(true, status == 0xe0822c15)`.

## Local divergence

The local Tahoe kernel-PAE path completes the WPA 4-way handshake in
`ieee80211_pae_input.c`, installs PTK/GTK, opens the 802.1X port, and emits
`IEEE80211_EVT_STA_RSN_HANDSHAKE_DONE` on the `ni_port_valid` 0->1 transition.
Before this batch, the event handler posted `APPLE80211_M_RSN_HANDSHAKE_DONE`
and WCL link/connect-complete events, but it did not invoke the IO80211
key-done property updater. Runtime therefore could show real air-side
`EAPOL-4WAY-HS-COMPLETED` while `IO80211RSNDone` remained false.

## Local correction

`AirportItlwm::postRsnHandshakeDoneGated(...)` now calls
`IO80211InfraInterface::handleKeyDone(true, false)` before the existing
`APPLE80211_M_RSN_HANDSHAKE_DONE` publication. The call is gated and is reached
only from the real kernel-PAE completion event. It does not fabricate keys,
change PAE state, replay EAPOL, retry association, or mask deauth; it connects
the completed lower handshake to the recovered IO80211 key-done consumer.

The reset side remains owned by the inherited
`IO80211InfraInterface::setWCL_LINK_STATE_UPDATE(...)`, which the local
`AirportItlwmSkywalkInterface::setWCL_LINK_STATE_UPDATE(...)` already delegates
to directly.

## Runtime validation

- Built on Tahoe against
  `/System/Library/KernelCollections/BootKernelExtensions.kc`; all 946
  undefined symbols resolved against BootKC.
- Loaded kext UUID:
  `9AD9C19E-131D-3C3C-B7FD-0695645BA626`.
- Controlled AP join showed hostapd
  `EAPOL-4WAY-HS-COMPLETED c6:ef:66:c0:c0:ca` and DHCP
  `10.77.0.157`.
- IORegistry after join and after stress showed:
  `IO80211SSID = "ITLWM-Lab-3c95c7"`,
  `IO80211BSSID = <80e4ba20eff9>`,
  `IO80211Channel = 1`, and `IO80211RSNDone = Yes`.
- 240-second stress window with concurrent ping and iperf3:
  `240/240` ping replies, `0.0%` loss, and `718 MBytes` over `240.59`
  seconds at about `25.0 Mbits/sec` receiver throughput.
- hostapd retained the station as authenticated, associated, and authorized
  after the stress window.

Remaining divergence: `networksetup -getairportnetwork en1` still reports
`You are not associated with an AirPort network.` even while IORegistry,
hostapd, DHCP, ICMP, and iperf3 all prove the link is up. That is the next
public-status/CoreWLAN layer, not part of this kernel PAE key-done closure.
