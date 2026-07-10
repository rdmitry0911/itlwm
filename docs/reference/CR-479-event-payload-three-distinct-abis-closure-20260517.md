# CR-479 Tahoe event-payload three distinct ABIs closure (project-owned analysis)

## Status

- static_closure_status: FULL_DECOMP_CLOSED
- ap_control_plane_closure_status: FULL_LAYER_CLOSED_CODER_READY
- coder_implementation_status: BOUNDED_LOCAL_PARITY_IMPLEMENTED (rev2)
- runtime_allowed: NO (until a new Stage 1 decision approves after-fix runtime)
- commit_allowed: NO

## Why this layer note exists

The auditor-verified static-analysis closure for the four open CR-479 event-payload carrier targets is recorded in the imported web-AI result archive at SHA-256 `ba8d80e34eab6a9b55622b245592710d52ba5f0b0463bfde0bdcc669008b271c` (corroborated by the authoritative result `928321c63415943ee4e0947420b3adf54a41e026d3077e4df187bea5fbdbc7f3`). The closure proves the three distinct event payload ABIs and the legacy event 3 / event 4 IO80211Glue routing semantics. The auditor accepted the closure with `coder_requeue_decision: YES` and `next_coder_task.route: CODER_LAYER_WORK`, listing four implementation targets. This note records the recovered invariants and the local implementation that closes the bounded coder requeue.

## The three distinct Tahoe event payload ABIs

### 1. 32-byte link-changed snapshot/getter (APPLE80211_M_LINK_CHANGED = 4)

The 32-byte `apple80211_link_changed_event_data` payload is both the length-checked inline payload for `APPLE80211_M_LINK_CHANGED` publication and the on-demand response for the `SIOCGA80211` ioctl path `APPLE80211_IOC_LINK_CHANGED_EVENT_DATA = 156`. The recovered Apple writers / consumers are `WCLNetManager::getLINK_CHANGED_EVENT_DATA`, `WCLNetManager::getLinkChangeEventInternal`, `WCLNetManager::setLinkChangedEventData`, and helper `0xffffff8002113404`; the local mirror is `AirportItlwmSkywalkInterface::getLINK_CHANGED_EVENT_DATA` (IOCTL responder) plus the once-per-accepted-transition inline publisher in `AirportItlwmSkywalkInterface::setLinkStateInternal`.

Static enforcement: `static_assert(sizeof(apple80211_link_changed_event_data) == 0x20, ...)` plus per-field offset asserts at `+0x1c` (`voluntary_down`) and `+0x1d` (`voluntary_up`).

### 2. 24-byte BSSID-changed compact carrier (APPLE80211_M_BSSID_CHANGED = 3)

The 24-byte compact carrier is a distinct ABI from the 32-byte link-changed payload. The recovered Apple writer is `FUN_ffffff8002122c90` (BootKC entry, exact-equivalent of `getBssidChangeEventInternal`), reached through the wrapper at `0xffffff8002122eb2` and the `WCLNetManager::updateBss` / `setCurrentBSS` / `getBssidChnageReason` paths. The writer validates the current BSS state, allocates and frees a 0x844-byte IOUC buffer, issues WCL selector `0x1b1`, rejects zero BSSID, builds and consumes a `WCLBSSBeacon` object, and writes the BSSID material through the payload / output pointer paths. Observed default and failure statuses: `0xe0822403`, pass-through IOUC failure, and wrapper bad-envelope `0xe00002bc`.

Recovered field layout: total length `0x18` (24 bytes), BSSID at offset `0x00` (6 bytes), two reserved bytes at `+0x06..+0x07`, `apple80211_channel` at `+0x08..+0x13`, and reason at offset `0x14` (4 bytes). The 2026-07-08 25C56 `IO80211InfraInterface::bssidChange(void*, unsigned long)` decompile names the embedded channel by calling `IO80211Controller::setInfraChannel(controller, payload + 0x08)` before updating channel properties.

Recovered producer-side gates (mirrored by the local publisher):

- Zero-BSSID rejection: a proposed BSSID whose six octets are all zero is rejected before publication. The local publisher checks the six octets of `ic->ic_bss->ni_bssid` and emits a `zero_bssid_rejected=1` marker without publishing or updating the tracker when the rejection fires.
- Same-BSS reason-1 suppression: the publisher classifies the proposed BSSID against the last-published-BSSID tracker; if the tracker is valid and the proposed BSSID matches the last published BSSID, the reason is `APPLE80211_BSSID_CHANGE_REASON_SAME_BSS = 1` and the publication is suppressed (the tracker is not updated). Otherwise the reason is `APPLE80211_BSSID_CHANGE_REASON_INITIAL = 0` and the populated 24-byte payload is published exactly once through the standard `IO80211Controller::postMessage` / IO80211Glue pending-queue routing, with the tracker updated to the published BSSID afterwards.

Tahoe userspace length-checks this carrier (prior runtime evidence: `APPLE80211_M_BSSID_CHANGED expected=24 actual=0` rejection in CoreWiFi); the populated 24-byte payload is the only valid Tahoe shape.

Static enforcement: `static_assert(sizeof(apple80211_bssid_changed_event_data) == 0x18, ...)`, `__offsetof(bssid) == 0x00`, `__offsetof(channel) == 0x08`, `__offsetof(reason) == 0x14`.

Local status: the previously carried Tahoe `syncTahoeCurrentApAddress()` route forced `IO80211InfraInterface::setCurrentApAddress(NULL/BSSID)` from the local link-state path. Earlier review rejected that as guessed current-AP cache seeding, and the 2026-07-08 cleanup removes the forced route and its local cache state. The accepted identity BSSID publisher now zero-initializes the 24-byte carrier, fills the BSSID and reason fields through the existing same-BSS / zero-BSSID gates, and populates the recovered embedded `apple80211_channel` only from the current associated net80211 BSS when the proposed BSSID matches `ic->ic_bss->ni_bssid`. This closes the local carrier field shape without reviving the rejected current-AP cache seed or the later rejected passive `setCurrentApAddress(...)` event-3 hook.

The parent-success publisher inside `AirportItlwmSkywalkInterface::setLinkStateInternal` is active on the live Tahoe Skywalk runtime: FBT shows the parent bool return's low bit set on the accepted link-up transition. The earlier note that treated the `0x...001` return as a closed gate was incorrect. The accepted BSSID/SSID identity owner now lives on that parent-success link-up edge, after the inherited IO80211 transition accepts; `WCL_CONNECT_COMPLETE` remains on the earlier RSN key-done edge because the WCL join FSM times out without it. Fresh 25C56 BootKC evidence shows `AppleBCMWLANSkywalkInterface::setCurrentApAddress(...)` does not call `bssidChange(...)` or `postMessageInfra(...)`, and live FBT after removing the local passive hook shows `setCurrentApAddress(...)` entry/return without a BSSID helper. The first BSSID helper call is now inside the accepted identity owner's `ACCEPT_BSSID` path. The shared `fLastPublishedBssid` / `fLastPublishedBssidValid` tracker classifies repeated accepted-identity publication for the same BSS as `APPLE80211_BSSID_CHANGE_REASON_SAME_BSS` and suppresses duplicates. The credential-safe marker `Tahoe Skywalk M_BSSID_CHANGED bssid=... reason=<0|1> same_bss_reason_1_suppressed=<0|1> zero_bssid_rejected=<0|1>` reports the classification on every publisher entry: a normal first call with a non-zero address emits `reason=0 same_bss_reason_1_suppressed=0 zero_bssid_rejected=0` plus a published payload; a repeated call with the same address without an intervening link-down or null clear emits `reason=1 same_bss_reason_1_suppressed=1 zero_bssid_rejected=0` and no payload; a null or all-zero address emits `zero_bssid_rejected=1` and no payload. The embedded channel writer is conditional on the same current-BSS match and leaves the zeroed channel sub-structure intact when the proposed address is not the associated BSS.

### 2a. SSID-changed join event (APPLE80211_M_SSID_CHANGED = 2)

The follow-up BootKC read for `AppleBCMWLANJoinAdapter::handleSetSSID(wl_event_msg_t*)` shows that Apple's SSID-changed event is not owned by `IO80211InfraInterface::setLinkStateInternal`. The reference body copies six BSSID bytes from the SET_SSID firmware event (`wl_event_msg + 0x18`) into the join-adapter state, then calls the two-argument framework event producer with code `2` (`APPLE80211_M_SSID_CHANGED`). The recovered airportd `CWXPCInterfaceContext::ssidChanged` block does not consume payload bytes; it schedules a fresh `__associatedNetwork` read and forwards the returned object through `setAssociatedNetwork:`.

The local Tahoe bridge has no AppleBCMWLAN `JoinAdapter`, so the equivalent accepted identity producer is the parent-success Skywalk link-up path after `IO80211InfraInterface::setLinkStateInternal(...)` accepts. `AirportItlwmSkywalkInterface::setLinkStateInternal(...)` now calls the BSSID/SSID identity sequence there: populated BSSID_CHANGED, then the 8-byte SSID_CHANGED status carrier. The earlier `setLinkStateGated(...)` and RSN key-done BSSID/SSID producers were removed to keep one owner for the identity publication edge.

Runtime validation on 2026-07-08 loaded CDHash
`c14ddbe0ab5e77d5e8ee257769b72d48b3b11002` and confirmed airportd
delivery of `Driver Event: ... SSID_CHANGED/2 (en1)`. The following
`airportdProcessDpsEvent` log carried the joined network name, BSSID, and
channel tuple, proving the code-2 event now wakes the internal DPS associated
network refresh. The same build held a concurrent 240-second validation run:
ping to `10.77.0.1` reported `240 packets transmitted, 240 packets received,
0.0% packet loss`, and TCP iperf3 reported `838 MBytes` at
`29.3 Mbits/sec` sender / `29.2 Mbits/sec` receiver. Public
CoreWLAN/networksetup still returned nil / "not associated"; that is a
separate current-network/profile authorization gate, not the code-2 event
producer layer.

### 3. 16-byte WCL link-state update carrier (WCL event code 0xd8)

The 16-byte WCL link-state update payload is carried by `struct TahoeWclLinkChangedPayload` defined in `AirportItlwm/AirportItlwmV2.cpp`. The recovered Apple producers are `AppleBCMWLANNetAdapter::handleLink`, `sendInternalLinkDownInd`, `AppleBCMWLANCore::handleLinkEvent`, and `AppleBCMWLANCore::postMessageInfra`. The local mirror is `postTahoeWclLinkUpInd` in `AirportItlwmV2.cpp` which posts the 0x10-byte payload through `postMessage(controller->fNetIf, kTahoeWclLinkChanged = 0xd8, &payload, sizeof(payload), true)`.

The 16-byte WCL link-state update is NOT byte-synthesised from the 32-byte link-changed payload, and the 24-byte BSSID-changed and 32-byte link-changed payloads must NOT be byte-synthesised from the 16-byte 0xd8 payload either. The three ABIs are independent; each has its own producer and its own consumer contract.

Static enforcement: `static_assert(sizeof(TahoeWclLinkChangedPayload) == 0x10, ...)`.

## Legacy event 3 / event 4 IO80211Glue routing semantics

The closure proves the legacy event-3 / event-4 carrier path runs through:

- `IO80211Controller::postMessage` / `IO80211Controller::postMessageSync` dispatch.
- `IO80211SkywalkInterface::postMessageInternal` / `IO80211SkywalkInterface::postMessageSync` tail.
- `IO80211PostOffice::sendMail` / `IO80211PostOffice::sendMailSync` (the symbol `0xffffff80022bcd8e` is an in-function instruction / label inside `sendMailSync`'s store sequence, not a separate independent function).
- `IO80211Glue` operations: `addEventToPendingQueue`, `sendEventAndFilter`, `routeEventToWcl`, `sendIOUCToWcl` (including the block-invoke at `0xffffff8002118291`), `processPendingEventQueue`, `processPendingEventQueueSource`, `isMsgNeeded`, and the per-event filter / copy / free helpers.

The framework owns the lifecycle inside this pipeline: queued copy on enqueue, filter decision via `isMsgNeeded`, async/sync routing through PostOffice, pending-list drain through `processPendingEventQueue`, and free-after-delivery ownership in the postMessage layer. The local kext passes the populated payload and the asynchronous-delivery flag through `postMessage`; the framework handles copy / filter / route / drain / free.

The local kext's diagnostic instrumentation in `AirportItlwm::postWclScanDoneGated` enumerates the IO80211Glue object pointer at `*(fNetIf->expansion[0x120] + 0xd8)` as part of the `postMessage` dispatch debug log; that instrumentation remains the system-visible probe for IO80211Glue presence under this closure.

## Local mapping table

| Event ABI | Local source | Producer in local kext | Consumer in local kext |
| --- | --- | --- | --- |
| 32-byte link-changed (event 4 / IOC 156) | `include/Airport/apple80211_ioctl.h` `struct apple80211_link_changed_event_data` | `AirportItlwmSkywalkInterface::setLinkStateInternal` (inline 32-byte publication on parent transition success; same struct is the SIOCGA80211 IOC 156 response) | `AirportItlwmSkywalkInterface::getLINK_CHANGED_EVENT_DATA` (returns 32 bytes on SIOCGA80211) |
| 8-byte SSID-changed (event 2) | `apple80211_ssid_changed_event_data` status/reason carrier | `AirportItlwmSkywalkInterface::setLinkStateInternal` on the accepted parent-success link-up edge, after WCL connect-complete remains on RSN key-done | airportd `CWXPCInterfaceContext::ssidChanged` schedules `__associatedNetwork` and `setAssociatedNetwork:` |
| 24-byte BSSID-changed (event 3 compact) | `include/Airport/apple80211_ioctl.h` `struct apple80211_bssid_changed_event_data` (named fields `bssid +0x00`, `_pad_06 +0x06..+0x07`, `channel +0x08`, `reason +0x14`) | The rejected forced `syncTahoeCurrentApAddress()` seeding path and the later passive `setCurrentApAddress` event-3 hook have been removed. `AirportItlwmSkywalkInterface::setLinkStateInternal` publishes the accepted identity BSSID carrier on the parent-success link-up edge before SSID_CHANGED. | `IO80211InfraInterface::bssidChange(data, 0x18)` consumes the embedded channel before normal PostOffice delivery |
| 16-byte WCL 0xd8 link-state | `AirportItlwm/AirportItlwmV2.cpp` `struct TahoeWclLinkChangedPayload` | `postTahoeWclLinkUpInd` (posts 0x10-byte payload with `postMessage(... kTahoeWclLinkChanged, &payload, 0x10, true)`) | n/a (consumer is Apple userland event handler for WCL event 0xd8) |

## Non-claims

- This iteration does not change the 32-byte link-changed inline publication or the SIOCGA80211 IOC 156 getter; the existing committed behavior at `d43f4d9d238c80c019ec2c3f31e30fa633c9ebfb` for `APPLE80211_M_LINK_CHANGED` is preserved byte-for-byte.
- This iteration does not change the 16-byte WCL 0xd8 publisher or the `TahoeWclLinkChangedPayload` definition; the existing committed behavior is preserved byte-for-byte and the WCL payload is never byte-synthesised into a legacy 3/4 payload.
- This iteration writes only the recovered `apple80211_channel` at
  `+0x08..+0x13` when the accepted BSSID matches the current associated BSS;
  the two bytes at `+0x06..+0x07` remain reserved padding.
- This iteration does not claim the final WCL/current-BSS ownership route; it
  only makes the already-active local compact event-3 carrier match the
  recovered field layout before the framework `bssidChange(data, 0x18)`
  side-effect and PostOffice delivery path consume it.
- This iteration does not modify the IO80211Glue / PostOffice framework code; it relies on the documented `postMessage` entry to perform copy / filter / route / drain / free.

## Residual uncertainty (does not block Stage 1 approval)

- The two bytes at offset `+0x06..+0x07` of the 24-byte BSSID-changed payload
  remain reserved opaque pad. The local publisher zeroes them and populates only
  the named BSSID, embedded channel, and reason fields.
- `setCurrentApAddress(...)` is no longer a local event-3 producer. Fresh
  BootKC evidence for `AppleBCMWLANSkywalkInterface::setCurrentApAddress`
  shows current-address/cache side effects, not `bssidChange(...)` or
  `postMessageInfra(...)`. The broader WCL current-BSS ownership route remains
  a separate layer.

## Provenance

The recovered Apple contract above derives from the auditor-accepted web-AI result for task `cr479_event_payload_remaining_repair_full_project_sources_decomp_20260517T0604` (result archive SHA-256 `ba8d80e34eab6a9b55622b245592710d52ba5f0b0463bfde0bdcc669008b271c`, corroborated by authoritative task SHA-256 `928321c63415943ee4e0947420b3adf54a41e026d3077e4df187bea5fbdbc7f3`; `DECOMP_REFERENCE_CLOSURE_STATUS: FULL_DECOMP_CLOSED`, `AP_CONTROL_PLANE_CLOSURE_STATUS: FULL_LAYER_CLOSED_CODER_READY`, `REMAINING_DECOMP_TARGETS: NONE`, `IMPLEMENTATION_ROUTE_DECISION: SMALL_BOUNDED_CODER_REQUEUE`, `coder_requeue_decision: YES`, `next_coder_task.route: CODER_LAYER_WORK`). The targets covered: 24-byte BSSID-changed compact writer at length 0x18 with reason at offset 0x14 and same-BSS reason-1 suppression (TARGET 1), legacy event 3/4 IO80211Glue pipeline (TARGET 2), AppleBCMWLAN WCL 0xd8 link-state path (TARGET 3), and three-ABI distinction (TARGET 4). No new decomp / reference batch is requested by this iteration.
