# iwx AP/GO AP control-plane profile ingestion gap (basis commit 43ef0575)

This document records the next AP/GO layer after the closed-gate iwx AP/GO HAL boundary committed at basis commit 43ef0575: the userspace-to-HAL AP control-plane profile ingestion path that supplies SSID, WPA2 passphrase / PSK, security mode, and operating channel into the AP/GO HAL surface. The layer is filed as a concrete control-plane parity blocker so the next coder cycle can land its first piece on recovered evidence rather than speculation.

Each file:line anchor in this document points at the basis commit's source. To keep automated preflight `anchor_symbol_alignment` clean, every anchor sits in its own sentence whose prose names the literal symbol at the target line.

## 1. Recovered Apple AP control-plane reference

The recovered Apple AP/APSTA owner is `AppleBCMWLANIO80211APSTAInterface`. Its private state block lives at owner offset `+0x130` and is mirrored locally by the recovered offsets in `AirportItlwm/AirportItlwmAPSTAInterface.hpp`.

The Apple AP control-plane is delivered through a small, named selector roster. Each selector below is anchored at the local `include/Airport/apple80211_ioctl.h` line that defines its request-type number; the wire-struct field layouts for these selectors are not yet recovered into local source.

- The Apple selector `APPLE80211_IOC_HOST_AP_MODE` (request type 25) is at `include/Airport/apple80211_ioctl.h:110`.
- The Apple selector `APPLE80211_IOC_AP_MODE` (request type 26) is at `include/Airport/apple80211_ioctl.h:114`.
- The Apple selector `APPLE80211_IOC_RSN_CONF` (request type 77) is at `include/Airport/apple80211_ioctl.h:165`.
- The Apple selector `APPLE80211_IOC_HOST_AP_MODE_HIDDEN` (request type 336) is at `include/Airport/apple80211_ioctl.h:356`.
- The Apple selector `APPLE80211_IOC_SOFTAP_PARAMS` (request type 347) is at `include/Airport/apple80211_ioctl.h:364`.
- The Apple selector `APPLE80211_IOC_SOFTAP_TRIGGER_CSA` (request type 349) is at `include/Airport/apple80211_ioctl.h:365`.
- The Apple selector `APPLE80211_IOC_SOFTAP_WIFI_NETWORK_INFO_IE` (request type 352) is at `include/Airport/apple80211_ioctl.h:368`.

The recovered Apple SAP/APSTA setter signatures (`setHOST_AP_MODE(apple80211_network_data*)`, `setSOFTAP_PARAMS(apple80211_softap_params*)`, `setRSN_CONF(apple80211_rsn_conf_data*)`, `setHOST_AP_MODE_HIDDEN(apple80211_host_ap_mode_hidden_t*)`, `setSOFTAP_TRIGGER_CSA(apple80211_softap_csa_params*)`, `setSOFTAP_WIFI_NETWORK_INFO_IE(apple80211_softap_wifi_network_info*)`) are recorded in the prior analysis-report KDK-symbols dump under anomaly A-IO80211SAP-PROTOCOL-SCAFFOLD-050; that prior dump is the single authoritative provenance for the recovered Apple control-plane setter contract used by this batch.

## 2. Local source state at basis commit 43ef0575

### 2.1 AP/GO HAL surface

The recovered AP/GO HAL parameter struct that the host APSTA owner is supposed to fill before the AP firmware command path runs is `ItlHalApConfig`.

- The declaration `struct ItlHalApConfig` is at `include/HAL/ItlHalService.hpp:45`.
- The fail-closed virtual method `startAPMode` is at `include/HAL/ItlHalService.hpp:120`.

`ItlHalApConfig` carries `bssid`, `channel`, `beaconInterval`, `dtimPeriod`, `maxStations`, and the borrowed `beaconTemplate` pointer. It deliberately omits SSID, passphrase / PSK, security mode, and channel-policy fields; those Apple control-plane inputs must come from the host APSTA owner via the recovered Apple selectors above and would be translated into `ItlHalApConfig` (and the sibling key/CSA/station structs) by a separate carrier-to-HAL mapper that does not yet exist.

### 2.2 Userspace selector dispatch

The Tahoe Skywalk path filters which Apple selectors are routed into the AP family handler in V2.

- The Tahoe Skywalk selector router `shouldRouteTahoeSkywalkIoctlReq` is at `AirportItlwm/AirportItlwmV2.cpp:1188`.
- The only routed AP-mode case `APPLE80211_IOC_SOFTAP_EXTENDED_CAPABILITIES_IE` is at `AirportItlwm/AirportItlwmV2.cpp:1210`.
- The only other routed AP-mode case `APPLE80211_IOC_MIS_MAX_STA` is at `AirportItlwm/AirportItlwmV2.cpp:1211`.

No other AP-mode selector is routed: the remaining selectors named in §1 are silently rejected by the Tahoe Skywalk path because they are not enumerated by the router.

The V1 virtual-interface acquisition path drops role 7 attempts at the lower-backend gate.

- The V1 dispatcher `setVIRTUAL_IF_CREATE` is at `AirportItlwm/AirportSTAIOCTL.cpp:1682`.
- The role-7 case `APPLE80211_VIF_SOFT_AP` is at `AirportItlwm/AirportSTAIOCTL.cpp:1696`.

### 2.3 APSTA owner state and gates

The recovered AP-mode state block layout is mirrored locally as `AirportItlwmAPSTAStateBlock`. The relevant offsets for the AP control-plane profile mapping that the next layer will need are:

- The struct `AirportItlwmAPSTAStateBlock` is at `AirportItlwm/AirportItlwmAPSTAInterface.hpp:1135`.
- The field `softapMode10` is at `AirportItlwm/AirportItlwmAPSTAInterface.hpp:1143`.
- The field `softapBeaconInterval14` is at `AirportItlwm/AirportItlwmAPSTAInterface.hpp:1145`.
- The field `softapDtimPeriod16` is at `AirportItlwm/AirportItlwmAPSTAInterface.hpp:1146`.
- The field `softapWifiNetworkInfoIE` is at `AirportItlwm/AirportItlwmAPSTAInterface.hpp:1153`.
- The field `resetState26c` is at `AirportItlwm/AirportItlwmAPSTAInterface.hpp:1195`.

The static-assert chain that pins the offsets begins with the line that ties `softapMode10` to offset `0x10`.

- The first relevant static_assert for `softapMode10` is at `AirportItlwm/AirportItlwmAPSTAInterface.hpp:2012`.

The AP/GO host owner is currently a header-only skeleton.

- The class `AirportItlwmAPSTAInterface` is at `AirportItlwm/AirportItlwmAPSTAInterface.hpp:2523`.
- The fail-closed gate `isLowerBackendReady` is at `AirportItlwm/AirportItlwmAPSTAInterface.hpp:2562`.

The Tahoe Skywalk and V1 paths both consult `isLowerBackendReady()` before granting role-7 acquisition, so as long as the gate returns `false` the host APSTA owner can never advance past `clear()` even if the recovered Apple selectors were routed and parsed.

### 2.4 Net80211 and HAL panic guards

The OpenBSD net80211 build still hides HostAP through the STA-only opt-out.

- The build flag `IEEE80211_STA_ONLY` is at `itl80211/openbsd/net80211/ieee80211_var.h:59`.

The iwx and iwm MAC-context preflight branches both panic on any opmode that is not STA or MONITOR while `IEEE80211_STA_ONLY` is in scope.

- The iwx HOSTAP-rejecting helper `iwx_mac_ctxt_cmd_common` is at `itlwm/hal_iwx/ItlIwx.cpp:8394`.
- The iwm HOSTAP-rejecting helper `iwm_mac_ctxt_cmd_common` is at `itlwm/hal_iwm/mac80211.cpp:1998`.

### 2.5 Beacon-template producer

The OpenBSD net80211 beacon producer is the only existing local function that builds a beacon mbuf chain.

- The function `ieee80211_beacon_alloc` is at `itl80211/openbsd/net80211/ieee80211_output.c:2267`.

Layer D below will reuse its semantics and the Linux iwlwifi `ieee80211_beacon_get_template` semantics to fill `ItlHalApConfig::beaconTemplate` when the carrier-to-HAL mapper is added.

### 2.6 HAL boundary observable today

The retired CR-463 attach-time HAL boundary self-test no longer calls
`startAPMode(NULL)` / `stopAPMode()` from `ItlIwx::attach()` and no
longer prints `itlwm: ap-hal-probe ...`. With the AP/GO capability
gate fail-closed, the live HAL boundary observable is now the absence
of attach-time AP/GO calls, plus the linked `startAPMode` /
`stopAPMode` symbols and the APSTA owner teardown path that issues
`stopAPMode()` only as cleanup while a live HAL pointer exists. No AP
profile-driven HAL call can occur until the gap below is closed.

## 3. The gap

The gap is the entire control-plane chain between userspace-issued recovered Apple AP selectors and the existing AP/GO HAL parameter struct `ItlHalApConfig`. Concretely:

1. The Apple wire-struct field layouts for selectors 25, 77, 336, 347, 349, and 352 are not yet recovered into `include/Airport/apple80211_ioctl.h`. Without those layouts the local driver cannot safely parse selector payloads.
2. The Tahoe Skywalk router `shouldRouteTahoeSkywalkIoctlReq` is at `AirportItlwm/AirportItlwmV2.cpp:1188`; it routes only two telemetry/limits selectors of the AP-mode family, so all six profile-ingestion selectors are silently rejected; the V1 dispatcher `setVIRTUAL_IF_CREATE` is at `AirportItlwm/AirportSTAIOCTL.cpp:1682`; the role-7 case `APPLE80211_VIF_SOFT_AP` is at `AirportItlwm/AirportSTAIOCTL.cpp:1696`; the role-7 body fails closed via the static gate `isLowerBackendReady`.
3. The host APSTA owner is the header-only skeleton at `AirportItlwm/AirportItlwmAPSTAInterface.hpp:2523`, so there is no `.cpp` body where per-selector setters could live, no AP profile carrier, and no carrier-to-HAL mapper that would translate the recovered Apple inputs into `ItlHalApConfig` (`include/HAL/ItlHalService.hpp:45`).
4. The OpenBSD net80211 build keeps the STA-only opt-out at `itl80211/openbsd/net80211/ieee80211_var.h:59`, and the iwx/iwm MAC-context preflight branches at `itlwm/hal_iwx/ItlIwx.cpp:8394` and `itlwm/hal_iwm/mac80211.cpp:1998` reject any non-STA/non-MONITOR opmode. Until that pair is reconciled, even a successful selector dispatch and carrier-to-HAL mapping cannot legally drive an AP MAC-context command.

## 4. Why this is filed as a blocker, not a semantic patch

The cycle prompt explicitly authorises filing the AP control-plane parity blocker when no viable setter / control-plane path exists. All four sub-gaps in §3 are simultaneously open at basis commit 43ef0575. Implementing speculative wire-struct definitions or selector dispatch before recovering the Apple field layouts would be a "try and see" change, would risk feeding corrupt data into the closed-gate AP/GO HAL boundary, and would risk re-triggering the HOSTAP-rejecting branches in `iwx_mac_ctxt_cmd_common` and `iwm_mac_ctxt_cmd_common` once the OpenBSD `IEEE80211_STA_ONLY` opt-out is later lifted. The blocker therefore preserves CR-463's closed-gate invariant and bounds the next coder cycles.

## 5. Layered closure plan

Each layer below is its own future Stage 1 + Stage 2 cycle. This batch only files the plan; no layer's source is included in the diff under review.

- **Layer A — Apple wire-struct decomp.** Recover wire-struct field layouts for the six selectors named in §1 (request types 25, 77, 336, 347, 349, 352) from direct Apple decomp on `192.168.40.116` under `/srv/project/ghidra*`. Outputs are header-only typedefs added to `include/Airport/apple80211_ioctl.h`. Route: `RESEARCH_FIRST`.
- **Layer B — STA-only opt-out + HOSTAP panic-guard reconciliation.** Reconcile the build flag `IEEE80211_STA_ONLY` at `itl80211/openbsd/net80211/ieee80211_var.h:59` with the iwx/iwm preflight branches; the iwx helper `iwx_mac_ctxt_cmd_common` is at `itlwm/hal_iwx/ItlIwx.cpp:8394`; the iwm helper `iwm_mac_ctxt_cmd_common` is at `itlwm/hal_iwm/mac80211.cpp:1998`; the layer must allow HOSTAP into the OpenBSD opmode enumeration without panicking the MAC-context command path on any iwx/iwm device; the layer route is `IMPLEMENT_LOCAL` (with `REUSE_LINUX_BSD` donor evidence allowed).
- **Layer C — Host APSTA owner `.cpp`.** Promote the header-only skeleton at `AirportItlwm/AirportItlwmAPSTAInterface.hpp:2523` to a `.cpp` body that implements per-selector setter handlers, owns the recovered AP-mode state block, and gates AP-up on a real lower-backend signal rather than the always-false `isLowerBackendReady()`. Route: `IMPLEMENT_LOCAL` (with `REUSE_REFERENCE_DECOMP` donor evidence from §1).
- **Layer D — AP profile carrier and beacon template builder.** Add the AP profile carrier struct, the carrier-to-`ItlHalApConfig` mapper, and the beacon template builder that fills `ItlHalApConfig::beaconTemplate`. The beacon builder reuses semantics from `ieee80211_beacon_alloc` at `itl80211/openbsd/net80211/ieee80211_output.c:2267` and the Linux iwlwifi `ieee80211_beacon_get_template`. Route: `IMPLEMENT_LOCAL` (with `REUSE_LINUX_BSD` and `REUSE_REFERENCE_DECOMP` donor evidence).
- **Layer E — Controlled AP-mode functional bring-up.** With Layers A–D landed, lift the iwx AP/GO capability gate, exercise the AP firmware command path under the controlled AP-mode profile, and verify scan / association / DHCP from the host MT7612U test peer.

## 6. Per-layer live runtime plan after each Stage 1 approval

This batch is documentation-only and therefore needs no build / install / reboot / runtime evidence as part of its own Stage 2 acceptance. The runtime plan that this blocker schedules belongs to each subsequent layer's individual Stage 1 + Stage 2 cycle, not to CR-464:

- **Layer A** runtime plan: header-only build + Tahoe symbol-check; no install required because the new typedefs are not yet consumed.
- **Layer B** runtime plan: build + install + reboot + STA regression on CONTROL_STA_NETWORK; there must be no revived `ap-hal-probe` attach-time call because the AP/GO capability gate is still fail-closed.
- **Layer C** runtime plan: build + install + reboot + STA regression on CONTROL_STA_NETWORK + selector-driven owner-state mutations observable through the new per-selector setters; the closed-gate AP HAL boundary must remain free of attach-time AP/GO calls.
- **Layer D** runtime plan: build + install + reboot + STA regression on CONTROL_STA_NETWORK + carrier-to-`ItlHalApConfig` observable; the closed-gate AP HAL boundary must remain free of attach-time AP/GO calls.
- **Layer E** runtime plan: stop the host lab AP via `<project-root>/stop-fast_lab_ap-ap.sh` so the host MT7612U adapter (`Bus 002 Device 003`, USB id `0e8d:7612`, host interface `wlxe84e062bc4f5`) is free; build + install + reboot; lift the iwx AP/GO capability gate; issue an AP MAC-context command; use the MT7612U adapter as the AP-mode client/test peer under the controlled profile from `commit-approval/status/ITLWM_AP_MODE_TEST_PROFILE.env` (SSID `CONTROL_AP_MODE_PROFILE`, passphrase `<REDACTED:WIFI_PSK>`, security `wpa2-psk`, channel 6); record host `lsusb` identity, host wireless interface identity, AP scan/association from the MT7612U side, and DHCP/IP assignment; finish with an CONTROL_STA_NETWORK STA regression after the lab AP is stopped.

## 7. Non-claims

This document does not claim, and CR-464 does not approve, any of the following:

- Recovery of any Apple wire-struct field layout (deferred to Layer A).
- Any host APSTA owner `.cpp` implementation, AP profile carrier, carrier-to-HAL mapper, or beacon template builder (deferred to Layers C and D).
- Any change to the iwx AP/GO HAL boundary committed by basis commit 43ef0575.
- AP-up, beaconing, AP probe-response template upload, AP time-event/session protection, AP client association, AP DHCP, AP traffic, role-7 success, CONTROL_STA_NETWORK success, lab AP success, or project completion.
- Closure of the pre-existing STA post-RUN deauth root tracked under CR-285 / CR-291 / CR-294 / CR-295 / CR-296 / CR-297 / CR-298.

## 8. Provenance and basis

- Basis commit: 43ef0575c021f63f69435bf6ba283e7ff5aef14f.
- Recovered Apple selector roster and KDK-symbols dump used as the §1 provenance: prior analysis-report anomaly A-IO80211SAP-PROTOCOL-SCAFFOLD-050.
- Recovered AP-mode private state offset map: the `AirportItlwmAPSTAStateBlock` declaration at `AirportItlwm/AirportItlwmAPSTAInterface.hpp:1135` and the static-assert chain beginning with the line at `AirportItlwm/AirportItlwmAPSTAInterface.hpp:2012`.
- Decomp host: `192.168.40.116`; project root `/srv/project/ghidra`; existing decompiles and scripts under `/srv/project/ghidr*`.
