# Tahoe WCL single link-up ownership runtime (25C56, 2026-08-03)

## Reference ownership

The Tahoe 25C56 BootKC recovered on `10.7.6.112` separates the WCL join
producer from the parent IO80211 link-state publisher:

- `AppleBCMWLANNetAdapter::handleLink` is the single producer of the `0xd8`
  WCL link indication;
- `WCLNetManager::linkUp(void *)` at `0xffffff800211014a` consumes that mail
  and calls `updateBss(...)` before reporting link-up progress; and
- `WCLNetManager::connectComplete(void *)` at `0xffffff8002111594` calls
  `WCLNetManager::updateLinkState(true, false, true, 0, generation)` at
  `0xffffff8002111645`. That method returns IOC `0x1c6` to the interface.

The exact ready disassemblies are under
`/home/dima/Projects/ghidra_output/cr479_bootkc_memory_safe_checkpoint_smoke_20260516T1248/09_static_slices/BootKC_memory_safe/`
on the reference host. The xrefs for `updateLinkState` are in the matching
`07_xrefs/BootKC_memory_safe/02114_ffffff8002111768_*.xrefs.tsv` artifact.

The local protected/open join-completion actions already produced `0xd8` and
then `0xd5`. `AirportItlwm::setLinkStateGated` nevertheless produced another
`0xd8` before invoking the inherited parent transition. Once both producer
paths were reliably command-gated, a WPA2 four-way handshake completed on air
and the duplicate WCL join synchronously tore the new link back down.

The correction removes only the parent-side `0xd8`. Join completion remains
the single WCL producer; `setLinkStateGated` remains the inherited IO80211
link-state owner.

## Candidate identity and build

- source base: `4fe0cbbb03d2acf82d7a8239c5dd0764ddf16cc5` plus the single-owner
  patch recorded by this change;
- build root: `/Users/devops/runtime/itlwm-wcl-single-linkup-wip41`;
- target: `AirportItlwm-Tahoe`, Debug/Tahoe;
- loaded Mach-O UUID: `780AC3EF-5371-327E-922C-B00E315708BF`;
- loaded binary SHA-256:
  `6a0f7a279b2817449a676f53ecd44dcfc224c44dc425b46d7edba720586cb9d6`;
- release archive SHA-256:
  `ee491c8fac0b64752293084cdad46e145f4302f1163035b5ad109c70bd1a4090`;
- all 1075 undefined symbols resolved against the guest BootKC;
- private and canonical AuxKC inspections each contained exactly the five
  expected kexts.

The loaded UUID and installed binary hash were read back after a guest-only
reboot. No physical test host was rebooted.

## WPA2 on-air result

The physical guest Intel 6235 associated to host BSSID
`80:e4:ba:20:ef:f9`, SSID `AIAMUP9`, channel 9, WPA2-PSK/CCMP. Hostapd
recorded messages 1/4 through 4/4, `AP-STA-CONNECTED`, an authorized port and
`EAPOL-4WAY-HS-COMPLETED` for the guest MAC `a2:c0:e7:63:65:b9`.

The guest retained `IEEE80211_S_RUN`, active media and current BSS identity,
then received `192.168.4.25/24` from the controlled DHCP server. The
redaction-safe trace contained one WCL link-state update, not the two nested
down updates from the preceding candidate. Source-bound guest-to-host ICMP
passed 20/20 and host-to-guest ICMP passed 20/20.

## Open on-air result

The same radio and BSSID were changed to the open SSID
`AIAMOpenControl6235`. A direct ordinary `APPLE80211_IOC_ASSOCIATE` carrier
used `OPEN/NONE`, no key, and zero RSN length. The guest reached
`IEEE80211_S_RUN` with exact SSID/BSSID, active media and `192.168.4.25/24`.
Hostapd reported the real guest MAC authenticated, associated and authorized.

The trace again contained one successful WCL update followed by one accepted
parent link-up. Guest-to-host and host-to-guest ICMP both passed 20/20.

## Sleep boundary exposed by the same cycle

After a guest-only reset, CoreWLAN automatically rejoined the controlled open
BSSID using its private MAC `f2:9b:db:2f:58:cd`, obtained `192.168.4.56/24`,
and again passed 20/20 ICMP packets in both directions. A second real S3 then
recorded `ACPI SLEEP`, `acpi_sleep_kernel`, `ACPI S3 WAKE`, and the
system-wake event. Hostapd observed the controlled station disconnect during
sleep.

The wake path did not return to that open BSSID. It instead emitted
`wcl_assoc CACHED_CANDIDATE_DIRECT_JOIN`, moved from channel zero to channel
13, and entered `iwn_sae_roam ACTIVE_ESS_CREDENTIAL`. Channel 13 is the saved
pure-SAE `LabAP` profile used by the preceding roam cycle, while the live
pre-sleep BSSID was open on channel 9. The controlled AP saw no station return
within the bounded observation interval.

This narrows the next layer considerably: radio power-on and the cached WCL
join both ran, but the successful open join did not replace or retire the
driver-resident SAE ESS credential used by wake. This is not reported as S3
recovery success. The next change must compare the reference JoinAdapter/WCL
credential ownership at open connect completion before modifying the wake
policy.

## Contracts

- `scripts/test_tahoe_wcl_join_completion_gate_contract.sh`
- `scripts/test_tahoe_link_handoff_diagnostic_contract.sh`
- `scripts/test_tahoe_wcl_auth_assoc_completion_contract.sh`
- `scripts/test_net80211_public_initial_bssid_pin_contract.sh`
- `scripts/test_tahoe_post_plti_trace_contract.sh`
- `scripts/run_tahoe_sae_quarantine_layer.sh` (full five-stage gate)
- `git diff --check`
