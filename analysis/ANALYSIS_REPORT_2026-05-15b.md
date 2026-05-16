# ANALYSIS REPORT 2026-05-15b

## CR-479 Tahoe Skywalk link-state-transition publisher dispatch glue

### Anomaly addressed

The CR-479 carrier-layer Stage 1 (analysis report 2026-05-15a) wired the
32-byte `APPLE80211_M_LINK_CHANGED` payload publication onto
`AirportItlwm::setLinkStateGated` for the Tahoe target. The carrier-layer
Stage 2 evidence (open-AP diagnostic on the installed approved kext, kext
sha256 `62455a4ff1fca788f4d954f8fdc82d21bf357af7611cf60e230a6ab6aed1d62b`,
boot at `2026-05-15T18:07:16+03:00`) proved that on a successful Tahoe
Skywalk client association the OpenBSD newstate → `setLinkStatus` →
`setLinkStateGated` chain is not invoked at all (counted invocations are
`0` across 108069 WiFi-related kernel/userspace lines), while the
upstream 802.11 association, 4-way handshake skip on open security, and
DHCP all complete. The Apple Tahoe Skywalk client path drives link
transitions through the IO80211 framework's
`IO80211InfraInterface::setLinkStateInternal` virtual rather than through
the legacy BSD newstate carrier. This analysis closes the remaining
dispatch glue gap by publishing the same recovered 32-byte payload from
the Tahoe Skywalk side override so that the upstream userspace event
handler receives a length-compliant `APPLE80211_M_LINK_CHANGED` delivery
on the actually-traversed Tahoe Skywalk path.

### Reference closure used as input

This analysis reuses the already-imported and auditor-verified
closure-grade decomp batch:

- web-AI task id
  `cr479_closure_grade_missing_targets_project_sources_decomp_20260515T1604`,
  result wrapper sha256
  `c998baaeea9d4dc7cc80dd91a13f16d8c67c3c0f0b9e4d55350375b4a5e0bcdf`,
  status `DECOMP_EXECUTION_STATUS: COMPLETE`,
  `CR479_EVENT_PAYLOAD_CARRIER_CLOSURE_STATUS: FULL_LAYER_CLOSED_CODER_READY`,
  `REMAINING_DECOMP_TARGETS: NONE`. The closure recovered the exact
  field-level semantics of the `apple80211_link_changed_event_data`
  publisher in `WCLNetManager::getLinkChangeEventInternal` at BootKC
  entry `0xffffff8002111138`, the 32-byte length gate on the Tahoe
  userspace event handler
  `-[CWFXPCRequestProxy __setupEventHandlersWithInterfaceName:]_block_invoke`,
  the complete IO80211 carrier dispatch path
  (`IO80211Controller::postMessage` /
  `IO80211SkywalkInterface::postMessageInternal` /
  `IO80211PostOffice` send / `IO80211Glue::sendIOUCToWcl` /
  `addEventToPendingQueue` / `sendEventAndFilter` / `routeEventToWcl` /
  `processPendingEventQueue`), the AppleBCMWLAN producer/bridge mapping
  for legacy events 3 / 4 via the WCL 0xd8 path, and the resolution
  that no standalone payload-bearing writer exists for the 24-byte
  `APPLE80211_M_BSSID_CHANGED` shape.

The orchestrator route decision `CODER_RESUME_AFTER_FULL_DECOMP_CLOSURE`
explicitly records this closure as full and coder-ready
(`decomp_closure_verified_by_auditor: true`,
`ap_control_plane_closure_status: FULL_LAYER_CLOSED_CODER_READY`,
`remaining_decomp_targets: []`,
`must_not_stage_web_ai: true`,
`stale_routes_superseded: [AUDITOR_REMOTE_DECOMP_ARTIFACT_ACQUISITION,
CHATGPT_WORKPACK_PREPARATION, AUDITOR_REPACKAGING_OF_PRIOR_RESULT]`),
and the carrier-layer Stage 2 rejection requires `IMPLEMENT_LOCAL for
local Skywalk-to-carrier dispatch glue, using the already recovered
32-byte Apple LINK_CHANGED payload contract and the existing
project-owned payload builder as inputs`.

### Recovered Apple-side dispatch contract

The Tahoe IO80211 framework drives Skywalk-managed STA interfaces
through the `IO80211InfraInterface::setLinkStateInternal(IO80211LinkState,
debounceTimeout, debounce, code, connectionId)` virtual. The override
point exists in the AirportItlwm Skywalk subclass
(`AirportItlwm/AirportItlwmSkywalkInterface.cpp:2143` pre-patch) and is
the canonical hook the framework reaches on a Tahoe Skywalk link
transition independent of the BSD newstate task. The terminal link
states `kIO80211NetworkLinkUp` and `kIO80211NetworkLinkDown` (defined
at `include/Airport/apple80211_var.h:679-680`) correspond exactly to
the userspace `APPLE80211_M_LINK_CHANGED` event semantics: on transition
to `kIO80211NetworkLinkUp` the Tahoe consumer expects an
`isLinkDown=0` payload with `voluntary_up=1` and `rssi` populated; on
transition to `kIO80211NetworkLinkDown` the consumer expects an
`isLinkDown=1` payload with `voluntary_down`, `reason`, and the last
BSSID populated.

### Local mapping

The recovered field-source mapping is now used by the inline payload
builder inside the Skywalk override
`AirportItlwmSkywalkInterface::setLinkStateInternal` and continues to
be used by the V1 and V2 `APPLE80211_IOC_LINK_CHANGED_EVENT_DATA`
publishers (`AirportItlwm::getLINK_CHANGED_EVENT_DATA` at V1 and V2
sites). The mapping was previously used by the superseded carrier-layer
helper `buildTahoeStaLinkChangedEventPayload` (now removed from
`AirportItlwm/AirportItlwmV2.cpp` along with the Tahoe-branch
`setLinkStateGated` `APPLE80211_M_LINK_CHANGED` publication block).

| recovered semantic            | itlwm source                                                                  |
|-------------------------------|-------------------------------------------------------------------------------|
| `isLinkDown`                  | `state != kIO80211NetworkLinkUp`                                              |
| `voluntary_down` (link-down)  | `instance->disassocIsVoluntary`                                               |
| `voluntary_up` (link-up)      | 1 (constant; every itlwm STA RUN entry runs through an upper-layer join)     |
| `reason` (link-down)          | `APPLE80211_LINK_DOWN_REASON_DEAUTH` (no HAL-side beacon-loss teardown)       |
| `rssi` (link-up)              | `ic->ic_bss->ni_rssi` via the `IWM_MIN_DBM` projection                        |
| `last_assoc[0..5]` (link-down)| `ic->ic_bss->ni_bssid`                                                        |
| `snr` / `nf` / `cca`          | 0 (itlwm does not expose these from the iwx / iwm HAL)                        |
| `last_assoc[6..11]`           | 0 (no recovered semantic for these six bytes)                                 |
| pad                           | 0 (bzero entry contract)                                                      |

`instance->fNetIf`, `instance->disassocIsVoluntary`, and
`instance->postMessage` are public members of `AirportItlwm` declared at
`AirportItlwm/AirportItlwmV2.hpp:490, 530` and inherited from
`IO80211Controller`. The `AirportItlwmSkywalkInterface` translation unit
already uses `instance->postMessage` and `instance->fNetIf` at multiple
sites (e.g. `AirportItlwmSkywalkInterface.cpp:4760, 5803, 6089`).

### Single-owner producer contract across the combined worktree

The rev2 submission was rejected because the combined worktree had two
APPLE80211_M_LINK_CHANGED publishers reachable on the same accepted
Tahoe link transition. `AirportItlwm::setLinkStateGated` (BSD-newstate
driven path) calls `((IO80211InfraInterface *)fNetIf)->setLinkState(...)`
before its own publication step. That `setLinkState` call enters the
`AirportItlwmSkywalkInterface::setLinkStateInternal` override; the
override now emits the recovered 32-byte payload on a successful parent
return, so re-publishing immediately afterwards from
`setLinkStateGated` would deliver the same userspace event twice for
a single accepted transition.

The rev3 diff resolves this by making the Skywalk override the single
owner of the APPLE80211_M_LINK_CHANGED carrier on Tahoe. The
Tahoe-branch publication block previously added by the carrier-layer
Stage 1 inside `AirportItlwm::setLinkStateGated` is removed; the
helper `buildTahoeStaLinkChangedEventPayload` it called is removed
with it (no remaining callers); and the same Tahoe branch keeps only
the link-down zero-length APPLE80211_M_SSID_CHANGED carrier, which has
no payload-bearing producer in the recovered reference and is not
length-rejected by Tahoe userspace. `APPLE80211_M_BSSID_CHANGED`
remains suppressed on Tahoe; the closure-grade reference recovery does
not contain a standalone payload-bearing writer for that event code.

The producer-ownership invariant after rev3 is therefore:

| accepted transition path                                             | publisher                                                |
|----------------------------------------------------------------------|----------------------------------------------------------|
| BSD newstate -> setLinkStatus -> setLinkStateGated -> fNetIf->setLinkState -> setLinkStateInternal override | `AirportItlwmSkywalkInterface::setLinkStateInternal` (once, on parent success) |
| Apple framework -> setLinkStateInternal override                     | `AirportItlwmSkywalkInterface::setLinkStateInternal` (once, on parent success) |
| pre-Tahoe `#else` branch in `setLinkStateGated`                  | unchanged legacy 3-event sequence                        |

The pre-Tahoe `#else` branch in `setLinkStateGated` is unchanged:
pre-Tahoe userspace does not enforce the Tahoe length contract and does
not provide the Skywalk override, so the legacy 3-event sequence remains
the single owner for that path.

No retry, fallback, masking, forced state, or duplicate-notify pattern
is introduced. The accepted-transition gate on `ret == kIOReturnSuccess`
inside the Skywalk override remains the only authorization for the
userspace carrier publication.

### Accepted-transition contract for the Skywalk producer hook

The Skywalk-side publication is gated on
`IO80211InfraInterface::setLinkStateInternal` returning
`kIOReturnSuccess` for the current call. The IO80211 framework returns
a nonzero `IOReturn` from `setLinkStateInternal` when an internal
step inside the parent transition aborts the link-state change before
it is committed. The earlier project-recorded behaviour at
`analysis/ANALYSIS_REPORT_2026-04-23.md` (lines 962-967) documents an
active Tahoe `ASSOC -> RUN` edge reaching
`setLinkStateInternal(state=2)`, then `AirportItlwm::getBSSIDData()`
failing the internal current-BSSID acquisition, then the parent
returning `0x1` without committing the link-up. That recorded edge is
not an accepted state change for the userspace
`APPLE80211_M_LINK_CHANGED` carrier, so the patched hook must
suppress the publication on the failed branch.

The recovered Apple producer
`WCLNetManager::getLinkChangeEventInternal` is invoked only from the
`bulletinBoardMessage` IOCTL surface and from accepted in-kernel link
transitions; the closure-grade reference recovery does not document a
publication on a failed link-state transition. Gating the Skywalk
producer hook on `ret == kIOReturnSuccess` therefore matches the
recovered contract and keeps the on-wire event tied to the Apple
framework view of an accepted transition. The gate adds no fallback or
masking semantics: when the parent transition fails, no Tahoe
`APPLE80211_M_LINK_CHANGED` carrier is emitted from the Skywalk
override or from `AirportItlwm::setLinkStateGated` (the Tahoe-branch
`setLinkStateGated` `APPLE80211_M_LINK_CHANGED` publication has been
removed by this diff, so there is no legacy Tahoe `LINK_CHANGED`
fallback publisher); the framework or the upstream join sequence may
re-drive the transition, and the next accepted transition will publish
exactly one carrier through the Skywalk override.

This gate also addresses the structural workaround-hunt concern that
an unconditional post-parent publication could fabricate a
successful-looking notification after the parent rejected the
transition. The Stage 1 patch publishes the recovered 32-byte payload
only on accepted terminal transitions; no forced callback or forced
success is introduced.

### Implementation in this diff

The diff modifies two source files and updates two analysis reports:

- `AirportItlwm/AirportItlwmSkywalkInterface.cpp`:
  Inside the Tahoe branch of
  `AirportItlwmSkywalkInterface::setLinkStateInternal`, after the parent
  `IO80211InfraInterface::setLinkStateInternal` call returns, when
  `ret == kIOReturnSuccess` and the caller-supplied `state` is
  `kIO80211NetworkLinkUp` or `kIO80211NetworkLinkDown`, build the
  recovered 32-byte `apple80211_link_changed_event_data` payload inline
  from project-owned state (`instance->disassocIsVoluntary`,
  `ic->ic_bss->ni_bssid`, `ic->ic_bss->ni_rssi`,
  `APPLE80211_LINK_DOWN_REASON_DEAUTH`, `IWM_MIN_DBM`) and publish it via
  `instance->postMessage(instance->fNetIf, APPLE80211_M_LINK_CHANGED,
   &ed, sizeof(ed), true)`. An XYLog records the four populated fields.
  This override is the single owner of the Tahoe userspace
  APPLE80211_M_LINK_CHANGED carrier.

- `AirportItlwm/AirportItlwmV2.cpp`:
  Inside the Tahoe branch of `AirportItlwm::setLinkStateGated`, remove
  the previously-added APPLE80211_M_LINK_CHANGED publication block (and
  its `Tahoe M_LINK_CHANGED isLinkDown=` XYLog), because that branch
  now reaches the Skywalk override above through
  `((IO80211InfraInterface *)fNetIf)->setLinkState(...)` and a second
  emission would deliver the same userspace event twice. The Tahoe
  branch keeps only the link-down zero-length
  APPLE80211_M_SSID_CHANGED carrier (no payload-bearing reference
  writer, not length-rejected by Tahoe userspace) and the
  APPLE80211_M_BSSID_CHANGED suppression (no standalone payload-bearing
  reference writer). The now-unused static helper
  `buildTahoeStaLinkChangedEventPayload` and its preceding documentation
  comment are removed; no remaining callers exist. The pre-Tahoe
  `#else` branch is unchanged.

- `analysis/ANALYSIS_REPORT_2026-05-15a.md`:
  Included in this patch as superseded historical evidence. Its
  implementation description (the helper added to V2.cpp and the
  setLinkStateGated APPLE80211_M_LINK_CHANGED publication block) is no
  longer accurate for the current diff; the recovered byte-level
  reference semantics and field-source mapping it documents remain
  authoritative and feed the inline payload builder in the Skywalk
  override. The historical-evidence header at the top of that file
  marks it as superseded.

- `analysis/ANALYSIS_REPORT_2026-05-15b.md`:
  This document. Carries the current single-owner producer contract,
  the accepted-transition return-value gate, and the live runtime
  after-fix plan.

The pre-Tahoe `#else` branch in `setLinkStateGated` continues to use the
legacy three-event sequence (`APPLE80211_M_LINK_CHANGED`,
`APPLE80211_M_BSSID_CHANGED`, `APPLE80211_M_SSID_CHANGED` all with NULL
payload). Pre-Tahoe userspace does not enforce the Tahoe 32-byte length
contract and does not provide the Skywalk override; that branch remains
the single owner of pre-Tahoe link-state event publication and is not
modified by this diff.

### Why this is the right next implementation unit

- The decomp / reference debt for the carrier layer is recorded as
  `FULL_LAYER_CLOSED_CODER_READY` by the closure-grade web-AI batch
  (sha256 `c998baaeea9d4dc7cc80dd91a13f16d8c67c3c0f0b9e4d55350375b4a5e0bcdf`).
  No new decomp gap is opened or required by this diff.
- The carrier-layer Stage 2 rejection decision pins
  `AirportItlwmSkywalkInterface.cpp` Tahoe Skywalk link-state transition
  as the unresolved touchpoint, and the auditor's preferred capability-gap
  route for this gap is `IMPLEMENT_LOCAL for local Skywalk-to-carrier
  dispatch glue, using the already recovered 32-byte Apple LINK_CHANGED
  payload contract and the existing project-owned payload builder as
  inputs`. This diff is exactly that dispatch glue.
- The layer has a single system-visible boundary: the on-wire
  `APPLE80211_M_LINK_CHANGED` carrier delivery on the Tahoe Skywalk
  link-up / link-down transition. The diff is one coherent functional
  change at that one boundary.
- Smaller alternatives (publication only on link-up or only on
  link-down) would leave the opposite transition without a carrier
  and either re-introduce the `expected=32 actual=0` userspace
  rejection or omit the link-down notification entirely. Larger
  alternatives (also wiring `setWCL_LINK_STATE_UPDATE` or
  `setCurrentApAddress` overrides) are not justified by the recovered
  contract because those framework hooks observe driver-side state
  rather than driving the userspace LINK_CHANGED carrier directly;
  publishing from multiple hooks would duplicate the on-wire event.

### Live runtime after-fix plan

After Stage 1 approval the coder will:

1. Build with the standard 420 s timeout
   (`./scripts/build_tahoe.sh`). Build must complete with
   `BUILD SUCCEEDED` and the Tahoe symbol check resolving all
   undefined symbols against BootKC.
2. Stop the controlled host lab AP if running, install the new kext
   over `/Library/Extensions/AirportItlwm.kext` with `root:wheel`
   ownership and `go-w` permissions, approve the kext through the VM
   UI on `127.0.0.1:5901` if macOS prompts, reboot the guest with the
   120 s SSH-return budget, and inspect VNC immediately if SSH does
   not return.
3. Confirm boot survival: no AirportItlwm panic, no AirportItlwm
   unload, kext identity hashed and recorded, `system_profiler
   SPAirPortDataType` shows the new HEAD short hash.
4. Lab-AP-scoped client diagnostic on the FAST_LAB_AP alias and the
   moderator-authorized OPEN_FAST_LAB_AP_DIAGNOSTIC alias:
   - Run a join cycle; capture the new XYLog line
     (`DEBUG ... Tahoe Skywalk M_LINK_CHANGED isLinkDown=...
     voluntary_down=... voluntary_up=... reason_or_rssi=0x...`) on the
     Tahoe Skywalk association success path.
   - Confirm absence of `expected=32 actual=0` rejections for
     `APPLE80211_M_LINK_CHANGED` in the userspace handler.
   - Confirm absence of `expected=24 actual=0` rejections (the
     carrier-layer suppression remains in place).
   - Stop the host lab AP after the recorded window.
   - Stability window: 60 s minimum on the lab AP association.
5. Final client verification on the CONTROL_STA_NETWORK alias only
   after the lab AP step shows the new Skywalk carrier publication.
   Capture association, DHCP/IP evidence, the same XYLog line, and
   a 60 s stability window. Wi-Fi credentials are redacted via the
   alias.

If post-install runtime shows
`AirportItlwmSkywalkInterface::setLinkStateInternal` invocation count
remains zero on the Tahoe Skywalk association success path (consistent
with the carrier-layer Stage 2 runtime evidence), the carrier still
will not fire and a separate follow-up Stage 1 will be required to add
a driver-side link-state trigger upstream of this override. The
current diff is the necessary and minimal closure of the Skywalk-side
publication gap regardless of whether the upstream trigger also
needs work.

### Explicit non-claims

- DHCP / IP success on the FAST_LAB_AP alias or on the
  CONTROL_STA_NETWORK alias is not claimed by this layer. The
  pre-existing post-association pre-DHCP-stable drop recorded in
  prior status artifacts is unchanged.
- AP / GO functional runtime and the Skywalk role-7 explicit-delete
  capability gap remain explicit follow-up layers.
- SNR / NF / CCA producer parity, beacon-loss reason classification,
  BSSID-change-reason producer, IP-protect window publication, and
  missed-beacons FSM publication remain explicit follow-up layers.
- Final Stage 2 approval of the preserved rev4 IOCTL response diff
  is not in scope for this Stage 1.
- This Stage 1 does not claim that the Apple framework's invocation
  rate of `setLinkStateInternal` on Tahoe Skywalk client association
  is changed by this diff; it only ensures the recovered 32-byte
  carrier is published when that hook is reached.
- This Stage 1 does not propose a 24-byte
  `APPLE80211_M_BSSID_CHANGED` writer. The closure-grade reference
  recovery rules out a standalone writer for that event code; the
  carrier-layer suppression of the Tahoe zero-length publication
  remains the project-owned behaviour.

### Sensitive-artifact hygiene

This analysis report, the patch artifact, the proposed commit
message, the source-code comment, and the XYLog string contain no
literal lab identifiers, no literal final / control STA network
identifiers, no Wi-Fi authentication values, no sudo credentials, no
provider login accounts, and no raw runtime credential logs.
References to test networks use the project aliases FAST_LAB_AP,
OPEN_FAST_LAB_AP_DIAGNOSTIC, CONTROL_STA_NETWORK, and
CONTROL_AP_MODE_PROFILE only. A sensitive-token scan over the report
and the patch artifact returns zero hits.
