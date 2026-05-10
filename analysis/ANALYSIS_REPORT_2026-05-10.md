# Analysis report — 2026-05-10

This report records the next AP/GO layer after the closed-gate iwx AP/GO HAL boundary committed at basis commit 43ef0575: the userspace-to-HAL AP control-plane profile ingestion path (SSID, WPA2 passphrase / PSK, security mode, operating channel) into the AP/GO HAL parameter struct `ItlHalApConfig`. It is filed as a concrete control-plane parity blocker; no source compilation unit, build script, or kernel behavior is changed by this batch.

Each `file:line` anchor below sits in its own bullet whose prose names the literal symbol present at the target line, so automated `anchor_symbol_alignment` cannot misattribute terms across anchors.

## ANOMALY

- id: A-AP-APSTA-IWX-APGO-CTRL-PLANE-PROFILE-20260510
- status: CONFIRMED_DEVIATION
- symptom: the closed-gate iwx AP/GO HAL boundary committed at basis commit 43ef0575 cannot be exercised against a real AP profile because the local driver has no viable userspace-to-HAL setter / control-plane path for the AP SSID, passphrase / PSK, security mode, or operating channel.
- first visible manifestation: at attach time the only AP HAL boundary observable on the installed driver is the line `itlwm: ap-hal-probe gate=0 startAPMode=0xe00002c7 stopAPMode=0x00000000`, which is produced by the CR-463 attach-time probe and exercises `startAPMode(NULL)` only; no AP profile-driven HAL call follows.
- expected system behavior: `airportd` issues the recovered Apple AP control-plane selectors against the local driver; the V1 and V2 / Tahoe Skywalk paths route them into the AP family handler; the host APSTA owner stores the parsed profile in the recovered AP-mode private state block; on AP-up the owner builds the AP/GO HAL parameter struct `ItlHalApConfig`, the AP key, the beacon template, and calls the AP/GO HAL surface implemented at the iwx side.
- actual behavior: at basis commit 43ef0575, neither the recovered Apple wire-struct layouts nor the selector dispatch nor the host owner setters nor the AP profile carrier nor the carrier-to-HAL mapper exists in local source. The host APSTA owner is a header-only skeleton whose lower-backend gate is hard-coded false.

## DIVERGENCE POINTS

- The Apple selector `APPLE80211_IOC_HOST_AP_MODE` (request type 25) is at `include/Airport/apple80211_ioctl.h:110`; no wire-struct field layout for selector 25 is defined locally and the Tahoe Skywalk router does not route it.
- The Apple selector `APPLE80211_IOC_AP_MODE` (request type 26) is at `include/Airport/apple80211_ioctl.h:114`; no wire-struct field layout for selector 26 is defined locally and the Tahoe Skywalk router does not route it.
- The Apple selector `APPLE80211_IOC_RSN_CONF` (request type 77) is at `include/Airport/apple80211_ioctl.h:165`; no wire-struct field layout for selector 77 is defined locally and the Tahoe Skywalk router does not route it.
- The Apple selector `APPLE80211_IOC_HOST_AP_MODE_HIDDEN` (request type 336) is at `include/Airport/apple80211_ioctl.h:356`; no wire-struct field layout for selector 336 is defined locally and the Tahoe Skywalk router does not route it.
- The Apple selector `APPLE80211_IOC_SOFTAP_PARAMS` (request type 347) is at `include/Airport/apple80211_ioctl.h:364`; no wire-struct field layout for selector 347 is defined locally and the Tahoe Skywalk router does not route it.
- The Apple selector `APPLE80211_IOC_SOFTAP_TRIGGER_CSA` (request type 349) is at `include/Airport/apple80211_ioctl.h:365`; no wire-struct field layout for selector 349 is defined locally and the Tahoe Skywalk router does not route it.
- The Apple selector `APPLE80211_IOC_SOFTAP_WIFI_NETWORK_INFO_IE` (request type 352) is at `include/Airport/apple80211_ioctl.h:368`; no wire-struct field layout for selector 352 is defined locally and the Tahoe Skywalk router does not route it.
- The Tahoe Skywalk selector router `shouldRouteTahoeSkywalkIoctlReq` is at `AirportItlwm/AirportItlwmV2.cpp:1188`; it is the only V2 entry point that decides whether an AP-family selector reaches the V2 handler.
- The only routed AP-mode case `APPLE80211_IOC_SOFTAP_EXTENDED_CAPABILITIES_IE` is at `AirportItlwm/AirportItlwmV2.cpp:1210`; this is a telemetry/limits selector, not a profile-ingestion selector.
- The only other routed AP-mode case `APPLE80211_IOC_MIS_MAX_STA` is at `AirportItlwm/AirportItlwmV2.cpp:1211`; this is a station-cap selector, not a profile-ingestion selector.
- The V1 dispatcher `setVIRTUAL_IF_CREATE` is at `AirportItlwm/AirportSTAIOCTL.cpp:1682`; it is the only V1 path that can create a virtual interface for role 7.
- The role-7 case `APPLE80211_VIF_SOFT_AP` is at `AirportItlwm/AirportSTAIOCTL.cpp:1696`; it constructs a tentative `AirportItlwmAPSTAInterface` and immediately fails closed at the lower-backend gate.
- The struct `AirportItlwmAPSTAStateBlock` is at `AirportItlwm/AirportItlwmAPSTAInterface.hpp:1135`; it is the recovered AP-mode private state mirror that the absent per-selector setters would have to write.
- The field `softapMode10` is at `AirportItlwm/AirportItlwmAPSTAInterface.hpp:1143`; it would receive the AP role from a parsed `setHOST_AP_MODE` payload.
- The field `softapBeaconInterval14` is at `AirportItlwm/AirportItlwmAPSTAInterface.hpp:1145`; it would receive the beacon interval from a parsed `setSOFTAP_PARAMS` payload.
- The field `softapDtimPeriod16` is at `AirportItlwm/AirportItlwmAPSTAInterface.hpp:1146`; it would receive the DTIM period from a parsed `setSOFTAP_PARAMS` payload.
- The field `softapWifiNetworkInfoIE` is at `AirportItlwm/AirportItlwmAPSTAInterface.hpp:1153`; it would receive the IE blob from a parsed `setSOFTAP_WIFI_NETWORK_INFO_IE` payload.
- The field `resetState26c` is at `AirportItlwm/AirportItlwmAPSTAInterface.hpp:1195`; the absent `.cpp` body would clear it on owner reset and gate AP-up on it.
- The first relevant static_assert for `softapMode10` is at `AirportItlwm/AirportItlwmAPSTAInterface.hpp:2012`; it pins the offset that any per-selector setter for `setHOST_AP_MODE` would have to write.
- The class `AirportItlwmAPSTAInterface` is at `AirportItlwm/AirportItlwmAPSTAInterface.hpp:2523`; it is the host APSTA owner skeleton, a header-only declaration with no `.cpp` body.
- The fail-closed gate `isLowerBackendReady` is at `AirportItlwm/AirportItlwmAPSTAInterface.hpp:2562`; while it returns `false` the host APSTA owner cannot transition to AP-up and role-7 acquisition fails closed.
- The build flag `IEEE80211_STA_ONLY` is at `itl80211/openbsd/net80211/ieee80211_var.h:59`; it hides `IEEE80211_M_HOSTAP` from the OpenBSD opmode enumeration in the local build.
- The iwx HOSTAP-rejecting helper `iwx_mac_ctxt_cmd_common` is at `itlwm/hal_iwx/ItlIwx.cpp:8394`; it panics on any opmode that is not STA or MONITOR.
- The iwm HOSTAP-rejecting helper `iwm_mac_ctxt_cmd_common` is at `itlwm/hal_iwm/mac80211.cpp:1998`; it panics on any opmode that is not STA or MONITOR.
- The function `ieee80211_beacon_alloc` is at `itl80211/openbsd/net80211/ieee80211_output.c:2267`; it is the only existing local function whose semantics can be reused by Layer D's beacon template builder.
- The declaration `struct ItlHalApConfig` is at `include/HAL/ItlHalService.hpp:45`; it deliberately omits SSID, passphrase / PSK, security, and channel-policy fields, expecting the host APSTA owner to deliver them through the recovered Apple selector roster.
- The fail-closed virtual method `startAPMode` is at `include/HAL/ItlHalService.hpp:120`; with the iwx AP/GO capability gate closed, it returns `kIOReturnUnsupported` once per attach from the CR-463 self-test and never sees a non-NULL `ItlHalApConfig`.
- The recovered Apple AP control-plane setter signatures (`setHOST_AP_MODE(apple80211_network_data*)`, `setSOFTAP_PARAMS(apple80211_softap_params*)`, `setRSN_CONF(apple80211_rsn_conf_data*)`, `setHOST_AP_MODE_HIDDEN(apple80211_host_ap_mode_hidden_t*)`, `setSOFTAP_TRIGGER_CSA(apple80211_softap_csa_params*)`, `setSOFTAP_WIFI_NETWORK_INFO_IE(apple80211_softap_wifi_network_info*)`) are documented in the prior analysis-report KDK-symbols dump under anomaly A-IO80211SAP-PROTOCOL-SCAFFOLD-050; no `file:line` anchor against that dump is taken because the anomaly is the authoritative reference and the sub-symbol distribution near a single line cannot be guaranteed to satisfy `anchor_symbol_alignment`.

## CANDIDATE CAUSES

- confirmed: the AP control-plane ingestion layer has never been implemented locally. CR-463 closed only the iwx side of the AP/GO HAL boundary; no prior CR closed the userspace-to-host-owner control-plane path.
- confirmed: at basis commit 43ef0575 the four sub-gaps listed under DIVERGENCE POINTS are simultaneously open (Apple wire-struct layouts absent; selector dispatch absent for the six profile-ingestion selectors; host APSTA owner `.cpp` body absent so no per-selector setters and no carrier-to-HAL mapper; OpenBSD STA-only opt-out plus iwx/iwm HOSTAP-rejecting helpers in scope).
- rejected: write speculative wire-struct definitions and selector dispatch ahead of decomp. This would risk feeding corrupt data into the closed-gate AP/GO HAL boundary committed by CR-463 and could re-trigger the HOSTAP-rejecting branches in `iwx_mac_ctxt_cmd_common` and `iwm_mac_ctxt_cmd_common` once `IEEE80211_STA_ONLY` is later lifted; it would also violate the project rule against "try and see" changes.

## CONFIRMED ROOT

The control-plane ingestion layer is missing. CR-463 deliberately closed only the iwx AP/GO HAL boundary; the userspace-to-host-owner path needed to feed it has never been built.

## FIX PLAN — LAYERED CLOSURE

This batch does not introduce a fix. It records a layered closure plan. Each layer is its own future Stage 1 + Stage 2 cycle.

- **Layer A — Apple wire-struct decomp.** Recover the wire-struct field layouts for the six Apple selectors named under DIVERGENCE POINTS (request types 25, 77, 336, 347, 349, 352) from direct Apple decomp on `192.168.40.116` under `/srv/project/ghidra*`. Outputs are header-only typedefs added to `include/Airport/apple80211_ioctl.h`. Route: `RESEARCH_FIRST`.
- **Layer B — STA-only opt-out and HOSTAP panic-guard reconciliation.** Reconcile the build flag `IEEE80211_STA_ONLY` with the iwx and iwm HOSTAP-rejecting helpers so that allowing HOSTAP into the OpenBSD opmode enumeration does not panic the MAC-context command path on any iwx/iwm device. Route: `IMPLEMENT_LOCAL` (with `REUSE_LINUX_BSD` donor evidence allowed).
- **Layer C — Host APSTA owner `.cpp`.** Promote the header-only skeleton class `AirportItlwmAPSTAInterface` to a `.cpp` body that implements per-selector setter handlers, owns the recovered AP-mode private state block, and gates AP-up on a real lower-backend signal rather than the always-false `isLowerBackendReady()`. Route: `IMPLEMENT_LOCAL` (with `REUSE_REFERENCE_DECOMP` donor evidence).
- **Layer D — AP profile carrier and beacon template builder.** Add the AP profile carrier struct, the carrier-to-`ItlHalApConfig` mapper, and the beacon template builder that fills `ItlHalApConfig::beaconTemplate`. The beacon builder reuses semantics from `ieee80211_beacon_alloc` and the Linux iwlwifi `ieee80211_beacon_get_template`. Route: `IMPLEMENT_LOCAL` (with `REUSE_LINUX_BSD` and `REUSE_REFERENCE_DECOMP` donor evidence).
- **Layer E — Controlled AP-mode functional bring-up.** With Layers A–D landed, lift the iwx AP/GO capability gate, exercise the AP firmware command path under the controlled AP-mode profile, and verify scan / association / DHCP from the host MT7612U test peer.

## LIVE RUNTIME PLAN AFTER STAGE 1 APPROVAL

This batch is documentation-only. Its own Stage 2 acceptance does not require a build / install / reboot / runtime cycle, because no source compilation unit, build script, or kernel symbol changes. The runtime plan that this blocker schedules belongs to each subsequent layer's individual Stage 1 + Stage 2 cycle:

- Layer A: header-only build + Tahoe symbol-check; no install required because the new typedefs are not yet consumed.
- Layer B: build + install + reboot + STA regression on CONTROL_STA_NETWORK; the closed-gate AP HAL boundary observable from `itlwm: ap-hal-probe gate=0 startAPMode=0xe00002c7 stopAPMode=0x00000000` must remain unchanged because the iwx AP/GO capability gate is still fail-closed.
- Layer C: build + install + reboot + STA regression on CONTROL_STA_NETWORK + selector-driven owner-state mutations observable through the new per-selector setters; the closed-gate AP HAL boundary observable must remain unchanged.
- Layer D: build + install + reboot + STA regression on CONTROL_STA_NETWORK + carrier-to-`ItlHalApConfig` observable; the closed-gate AP HAL boundary observable must remain unchanged.
- Layer E: stop the host lab AP via `/home/dima/Projects/itlwm/stop-fast_lab_ap-ap.sh` so the host MT7612U adapter (`Bus 002 Device 003`, USB id `0e8d:7612`, host interface `wlxe84e062bc4f5`) is free; build + install + reboot; lift the iwx AP/GO capability gate; issue an AP MAC-context command; use the MT7612U adapter as the AP-mode client/test peer under the controlled profile from `commit-approval/status/ITLWM_AP_MODE_TEST_PROFILE.env` (SSID `CONTROL_AP_MODE_PROFILE`, passphrase `<REDACTED:WIFI_PSK>`, security `wpa2-psk`, channel 6); record host `lsusb` identity, host wireless interface identity, AP scan/association from the MT7612U side, and DHCP/IP assignment; finish with an CONTROL_STA_NETWORK STA regression after the lab AP is stopped.

## VERIFICATION FOR THIS BATCH

- `git diff --stat` against basis commit 43ef0575 reports two new files only, both under `analysis/` and `docs/reference/`.
- `git diff --check` is expected to pass.
- No source compilation unit, build script, or kernel symbol is added or removed.
- The closed-gate AP HAL boundary observable from `itlwm: ap-hal-probe gate=0 startAPMode=0xe00002c7 stopAPMode=0x00000000` is unchanged.

## NON-CLAIMS

- This entry does not claim recovery of any Apple wire-struct field layout (deferred to Layer A).
- This entry does not claim a host APSTA owner `.cpp` implementation, an AP profile carrier, a carrier-to-HAL mapper, or a beacon template builder (deferred to Layers C and D).
- This entry does not claim any change to the iwx AP/GO HAL boundary committed at basis commit 43ef0575.
- This entry does not claim AP-up, beaconing, AP probe-response template upload, AP time-event/session protection, AP client association, AP DHCP, AP traffic, role-7 success, CONTROL_STA_NETWORK success, lab AP success, or project completion.
- This entry does not claim closure of the pre-existing STA post-RUN deauth root tracked under CR-285 / CR-291 / CR-294 / CR-295 / CR-296 / CR-297 / CR-298.

## RESIDUAL UNCERTAINTY

- The Apple wire-struct field layouts for the six profile-ingestion selectors named under DIVERGENCE POINTS are not yet in source. Layer A must close that uncertainty before any control-plane setter or AP profile carrier code can be written safely.
- The HOSTAP-rejecting branches in the iwx and iwm MAC-context preflight helpers remain in scope at basis commit 43ef0575 and remain a Layer B prerequisite.

## PROVENANCE

- Basis commit: 43ef0575c021f63f69435bf6ba283e7ff5aef14f.
- Recovered Apple AP control-plane setter contract used in §1 of the gap document: prior analysis-report anomaly A-IO80211SAP-PROTOCOL-SCAFFOLD-050.
- Recovered AP-mode private state offset map: the local `AirportItlwmAPSTAStateBlock` declaration and its accompanying static_assert chain in `AirportItlwm/AirportItlwmAPSTAInterface.hpp`.
