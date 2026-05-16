> **Historical evidence (superseded).** This report describes the
> intermediate carrier-layer implementation that added the
> `buildTahoeStaLinkChangedEventPayload` helper to
> `AirportItlwm/AirportItlwmV2.cpp` and published
> `APPLE80211_M_LINK_CHANGED` from `AirportItlwm::setLinkStateGated`.
> The current driver removes that helper and that
> `setLinkStateGated` publication block; the
> `AirportItlwmSkywalkInterface::setLinkStateInternal` override is the
> single owner of the Tahoe userspace LINK_CHANGED carrier, gated on
> `ret == kIOReturnSuccess`. See
> `analysis/ANALYSIS_REPORT_2026-05-15b.md` for the current
> producer-ownership contract. Sections below remain accurate for the
> recovered byte-level reference semantics, the IO80211 carrier
> lifecycle, the userspace length gate, and the field-source mapping
> that the inline payload builder in the Skywalk override now uses.

# ANALYSIS REPORT 2026-05-15a

## CR-479 event-publication carrier 32-byte APPLE80211_M_LINK_CHANGED payload

### Anomaly

The Tahoe userspace event handler
`-[CWFXPCRequestProxy __setupEventHandlersWithInterfaceName:]_block_invoke`
length-checks every incoming `APPLE80211_M_LINK_CHANGED` event delivery
against the 32-byte `apple80211_link_changed_event_data` shape and rejects
zero-length payloads with `expected=32 actual=0` before the userspace
client ever invokes the `APPLE80211_IOC_LINK_CHANGED_EVENT_DATA` IOCTL.
The same handler length-checks `APPLE80211_M_BSSID_CHANGED` against 24
bytes and rejects the existing itlwm zero-length publication with
`expected=24 actual=0`.

The CR-479 rev4 Stage 1 IOCTL ABI diff at HEAD
`0f1124853c5d5383b2477c96454c7e0e21a009a2` re-shaped
`apple80211_link_changed_event_data` to the recovered 32-byte Tahoe ABI
and rewrote the V1 (`AirportItlwm::getLINK_CHANGED_EVENT_DATA`) and V2
(`AirportItlwmSkywalkInterface::getLINK_CHANGED_EVENT_DATA`) IOCTL
publishers to populate every recovered field. Stage 1 was approved on
that basis. Stage 2 of the rev4 diff was rejected because the live
userspace consumer rejected the upstream zero-length carrier before any
IOCTL was invoked, so the patched publisher path never ran. This
analysis covers the separate event-publication carrier layer that
attaches the correct 32-byte payload to the same on-wire event code,
which is the prerequisite for the existing rev4 Stage 1 diff to be
exercisable under any subsequent Stage 2 runtime.

### Reference closure used as input

The closure-grade decomp batch supplied to web-AI ChatGPT (task id
`cr479_closure_grade_missing_targets_project_sources_decomp_20260515T1604`,
result sha256
`c998baaeea9d4dc7cc80dd91a13f16d8c67c3c0f0b9e4d55350375b4a5e0bcdf`,
inner material
`itlwm-cr479-closure-grade-missing-targets-20260515T155512Z`,
material sha256
`aff07573a89a70429a91cd8bd94e0092d168ddc57ac290fa9490b5fe82def58f`)
returned `CR479_EVENT_PAYLOAD_CARRIER_CLOSURE_STATUS:
FULL_LAYER_CLOSED_CODER_READY` and the four previously open targets
closed as:

- The Apple reference does not contain a standalone payload-bearing
  writer for the 24-byte `APPLE80211_M_BSSID_CHANGED` shape. The event
  code is present in the event-name table only.
- The IO80211 carrier dispatch path is covered through
  `IO80211Controller::postMessage` / `postMessageSync`,
  `IO80211SkywalkInterface::postMessageInternal` / `postMessageSync`,
  the `IO80211PostOffice` send / sendSync / out-for-delivery ranges,
  and `IO80211Glue::sendIOUCToWcl` / `addEventToPendingQueue` /
  `sendEventAndFilter` / `routeEventToWcl` / `processPendingEventQueue`.
  The semantic is: copy the caller payload into a
  `postMessageQueueEntry` (0x20-byte header + payload bytes), preserve
  the event code and payload length, enqueue, deliver, filter, unlink,
  and free by `payload_length + 0x20`.
- `AppleBCMWLANCore` directly produces legacy state-machine events 3
  and 4 on link / roam paths, but the payload-bearing publication
  carrier on Tahoe is bridged / synthesised into the WCL 0xd8
  `setWCL_LINK_STATE_UPDATE` path rather than republished as zero-length
  userspace events.
- The 24-byte-vs-32-byte ambiguity resolves to the 32-byte shape:
  `WCLNetManager::getLINK_CHANGED_EVENT_DATA` (BootKC entry
  `0xffffff8002115416`) gates on length `0x20`, calls
  `WCLNetManager::getLinkChangeEventInternal` (BootKC entry
  `0xffffff8002111138`), and that function writes the recovered
  32-byte field / default / fallback layout.

The web-AI result explicitly classified the next step as
`IMPLEMENTATION_ROUTE_DECISION: SMALL_BOUNDED_CODER_REQUEUE` and the
target as "replace the rejected IOCTL / zero-payload publication route
with the in-kernel WCL / IO80211 carrier semantics; publish through the
WCL 0xd8 bridge with the 32-byte link-changed payload contract;
preserve `IO80211Glue` / `PostOffice` copy, queue, delivery, unlink,
filter, and free semantics; do not add a speculative standalone 24-byte
`APPLE80211_M_BSSID_CHANGED` writer." This analysis follows exactly that
scope and does not extend it.

### Recovered byte-level reference semantics

`WCLNetManager::getLinkChangeEventInternal` at BootKC entry
`0xffffff8002111138` writes into the caller-supplied
`apple80211_link_changed_event_data *` (RSI on entry) as follows. RDI is
the WCL net manager; `[RDI+0x20]` is the connection-owner struct; the
inner state pointer is loaded via `[[[[RDI+0x20]+0x8]+0x10]+0x0]`.

When the connection-owner exists (link-up branch):

- `byte[+0x00] = 0`                                          (`isLinkDown`)
- `dword[+0x04] = dword[ownerInner+0x280]`                    (`rssi`)
- If `byte[ownerInner+0x288] & 1`:
  `word[+0x0a] = word[ownerInner+0x284]`                      (`nf`),
  `word[+0x08] = word[ownerInner+0x286]`                      (`snr`)
- If `byte[ownerInner+0x295] & 1`:
  `byte[+0x0c] = byte[ownerInner+0x294]`                      (`cca`)
  else `byte[+0x0c] = 0`
- `byte[+0x1d] = byte[[RDI+0x20]+0xc8] & 1`                   (`voluntary_up`)

When the connection-owner is absent (link-down branch at
`LAB_ffffff8002111190`):

- Bytes 0x00..0x1f are first copied from
  `[[RDI+0x20]+0x100 .. +0x120]` as four contiguous qwords stored back
  to `[+0x18, +0x10, +0x08, +0x00]`, producing a 32-byte snapshot of the
  last-association connection state.
- `byte[+0x1c] = byte[[RDI+0x20]+0xc8] & 1`                   (`voluntary_down`)
- `byte[+0x00] = 1`                                           (`isLinkDown`)
- `dword[+0x04] = dword[[[RDI+0x20]+0x8]+0x10+0x14]`          (`reason`)

The 32-byte ABI of the struct is identical to the IOCTL response shape
already approved at Stage 1 rev4:

| offset | width | field          | producer on link-down                                      | producer on link-up                                                  |
|--------|-------|----------------|------------------------------------------------------------|----------------------------------------------------------------------|
| 0x00   | 1     | isLinkDown     | 1                                                          | 0                                                                    |
| 0x01   | 3     | _pad_01        | 0                                                          | 0                                                                    |
| 0x04   | 4     | reason / rssi  | apple80211_link_down_reason from owner inner +0x14         | RSSI from owner inner +0x280                                         |
| 0x08   | 2     | snr            | snapshot byte                                              | owner inner +0x286 (when flag at +0x288)                             |
| 0x0a   | 2     | nf             | snapshot byte                                              | owner inner +0x284 (when flag at +0x288)                             |
| 0x0c   | 1     | cca            | snapshot byte                                              | owner inner +0x294 (when flag at +0x295)                             |
| 0x0d   | 3     | _pad_0d        | snapshot bytes                                              | 0                                                                    |
| 0x10   | 12    | last_assoc[12] | snapshot of last-assoc bytes from owner +0x110..+0x11b      | 0                                                                    |
| 0x1c   | 1     | voluntary_down | owner +0xc8 & 1                                            | 0                                                                    |
| 0x1d   | 1     | voluntary_up   | snapshot byte                                              | owner +0xc8 & 1                                                      |
| 0x1e   | 2     | _pad_1e        | snapshot bytes                                              | 0                                                                    |

`WCLNetManager::getLINK_CHANGED_EVENT_DATA` at BootKC entry
`0xffffff8002115416` is the IOCTL surface wrapper:

- Reads the payload pointer at `[RBX+0x20]` from the
  `bulletinBoardMessage` request.
- Reads the requested length at `[RBX+0x18]` and rejects unless it
  equals `0x20`.
- Calls `getLinkChangeEventInternal` with the payload pointer when both
  conditions hold.
- Sets `byte[RBX+0x28] = 1` (response-written flag) and returns
  `0xe0000001` on the rejection paths or `0` on success.

The event carrier path (called via `IO80211Controller::postMessage`)
copies the caller payload into a `postMessageQueueEntry` whose layout is
0x20-byte header followed by `payload_length` payload bytes. The Glue
filter `IO80211Glue::isMsgNeeded` decides delivery; `addEventToPendingQueue`
allocates the entry and copies in the payload by length; `sendEventAndFilter`
delivers and unlinks; `routeEventToWcl` / `sendIOUCToWcl` are the WCL
bridge variants for events that need WCL-side post-processing; the free
routine computes the total entry size as `payload_length + 0x20`. The
length field stored in the queue entry is exactly the `dataLen` argument
passed to the controller's `postMessage`; the producer is responsible
for getting that length right, which is exactly the present anomaly when
the producer passes `dataLen = 0`.

### Local mapping

itlwm cannot read the WCL net-manager internal state because it is an
Apple kext-private object that itlwm does not own. The recovered field
semantics map to itlwm state that the same Stage 1 rev4 publishers
already used to produce the IOCTL response:

| recovered semantic                   | itlwm source                                          | notes                                                                                                     |
|--------------------------------------|-------------------------------------------------------|-----------------------------------------------------------------------------------------------------------|
| isLinkDown                           | `linkState != kIO80211NetworkLinkUp` in setLinkStateGated | exact correspondence                                                                                       |
| voluntary_down (link-down)           | `AirportItlwm::disassocIsVoluntary`                   | local member set by the same disassociation owner paths as the V1 / V2 IOCTL publishers                    |
| voluntary_up (link-up)               | 1 (constant)                                          | every itlwm STA RUN entry is driven through an explicit setASSOCIATE / setSCAN_REQ join; same as Stage 1   |
| reason (link-down)                   | `APPLE80211_LINK_DOWN_REASON_DEAUTH`                   | itlwm has no HAL-side beacon-loss teardown signal; matches the existing IOCTL publishers' static reason   |
| rssi (link-up)                       | `ic->ic_bss->ni_rssi` mapped through `IWM_MIN_DBM`     | matches the V1 / V2 IOCTL publishers exactly                                                              |
| snr, nf, cca                         | 0                                                     | itlwm does not expose per-beacon noise floor or channel CCA from the iwx / iwm HAL today                  |
| last_assoc[0..5] (link-down)         | `ic->ic_bss->ni_bssid`                                | matches the V1 / V2 IOCTL publishers exactly                                                              |
| last_assoc[6..11]                    | 0                                                     | itlwm has no recovered semantic for these six bytes                                                       |
| _pad                                 | 0                                                     | bzero entry contract                                                                                       |

`AirportItlwm` already owns `fHalService->get80211Controller()` and
`disassocIsVoluntary` (publicly declared in `AirportItlwm/AirportItlwmV2.hpp`
at lines 492 and 530), so a single static helper inside the anonymous
namespace of `AirportItlwm/AirportItlwmV2.cpp` can build the payload
without exposing new members or moving any state. The new helper is
placed at `AirportItlwm/AirportItlwmV2.cpp:1400` (adjacent to the
`postTahoeWclLinkUpInd` helper at
`AirportItlwm/AirportItlwmV2.cpp:1427`) and is invoked from the
Tahoe branch of `AirportItlwm::setLinkStateGated` at
`AirportItlwm/AirportItlwmV2.cpp:4679`.

### Implementation in this diff

The diff modifies one source file:

- `AirportItlwm/AirportItlwmV2.cpp`:
  - Add `buildTahoeStaLinkChangedEventPayload(AirportItlwm *, bool, apple80211_link_changed_event_data *)`
    as a static helper next to `postTahoeWclLinkUpInd`. The helper
    zeroes the struct, sets `isLinkDown` from the caller, and populates
    the per-direction fields exactly as the Stage 1 rev4 IOCTL
    publishers do.
  - In `AirportItlwm::setLinkStateGated`, for the Tahoe branch
    (`__IO80211_TARGET >= __MAC_26_0`), replace the unconditional
    zero-payload publication block (`APPLE80211_M_LINK_CHANGED`,
    `APPLE80211_M_BSSID_CHANGED`, `APPLE80211_M_SSID_CHANGED` all
    `NULL, 0`) with:
    1. A single `APPLE80211_M_LINK_CHANGED` publication that carries
       the 32-byte `apple80211_link_changed_event_data` built by the
       new helper for both link-up and link-down. The XYLog records
       the four populated fields so Stage 2 runtime can verify the
       payload contents.
    2. No `APPLE80211_M_BSSID_CHANGED` publication on Tahoe. The Apple
       reference does not have a standalone 24-byte payload-bearing
       writer for this event code; suppressing the local zero-length
       publication removes the `expected=24 actual=0` rejection
       without fabricating a 24-byte shape that the reference does
       not document.
    3. `APPLE80211_M_SSID_CHANGED` zero-length publication only on
       link-down, matching the pre-existing Tahoe behaviour. The
       closure-grade reference recovery did not surface a payload
       writer for this event code either, and userspace never
       length-rejected the zero-length itlwm publication; preserving
       the name-only carrier keeps the diff strictly bounded.
  - The pre-Tahoe `#else` branch is left exactly as it was: pre-Tahoe
    userspace does not enforce the same length contract, and changing
    its behaviour is out of scope.

The diff does not touch any of the four Stage 1 rev4 paths
(`include/Airport/apple80211_ioctl.h`,
`AirportItlwm/AirportItlwmSkywalkInterface.cpp`,
`AirportItlwm/AirportSTAIOCTL.cpp`, `analysis/ANALYSIS_REPORT_2026-05-14j.md`),
so the rev4 Stage 1 approval at HEAD `0f1124853c5d` remains valid:
`git diff --binary HEAD --` against the four rev4 paths continues to
hash to `76e397d8ebdba003794e6fc8afa798d92bd8f7c211be0f21a138bfc449f2af39`.

### Why this is the right next implementation unit

- The decomp / reference debt for the Tahoe event-publication carrier
  is recorded as `FULL_LAYER_CLOSED_CODER_READY` by the closure-grade
  web-AI batch above, with the prior coverage blockers
  (`commit-approval/status/AGENT_STATUS_CR-479-event-payload-layer-recovery-blocker_20260514T222500_0300.md`,
  sha256 `d48316fa4270a3830f19db7762a412b2df924129c27454bcf8007034cc5005de`,
  and `commit-approval/status/AGENT_STATUS_CR-479-event-payload-layer-recovery-blocker-addendum_20260514T223200_0300.md`,
  sha256 `f3b06aae9095bef1b094e8db485e9bf6b7ef5282aa62627f7d46ab0462235ebc`)
  superseded by the closure result.
- The layer has a single system-visible boundary: the on-wire
  `APPLE80211_M_LINK_CHANGED` event payload that
  `__setupEventHandlersWithInterfaceName:` validates. The new diff fixes
  exactly that boundary, with no side-effect on the WCL 0xd8 connect-
  complete or link-up-ind carriers, on the IOCTL response shape, or on
  the pre-Tahoe path.
- The diff is one coherent functional change. Smaller alternatives
  (helper only, link-down only, link-up only) would either be
  unbuildable on their own or would leave one transition's
  `expected=32 actual=0` rejection live, defeating the purpose.

### Live runtime after-fix plan

After Stage 1 approval the coder will:

1. Build with the standard 420 s timeout
   (`./scripts/build_tahoe.sh`). The build must complete with
   `BUILD SUCCEEDED` and the Tahoe symbol-check resolving all undefined
   symbols against BootKC.
2. Stop the controlled host lab AP if running, install the new kext
   over `/Library/Extensions/AirportItlwm.kext` with `root:wheel` ownership
   and `go-w` permissions, approve the kext through the VM UI on
   `127.0.0.1:5901` if macOS prompts, reboot the guest with the 120 s
   SSH-return budget, and inspect VNC immediately if SSH does not
   return.
3. Confirm boot survival: no panic, no AirportItlwm unload, kext
   identity hashed and recorded, `system_profiler SPAirPortDataType`
   shows the new HEAD short hash.
4. Lab-AP-scoped client diagnostic on the FAST_LAB_AP alias:
   - Start the host lab AP via
     `<project-root>/start-itlwm-lab-ap.sh`.
   - Run a join cycle; capture the new XYLog line
     (`DEBUG Tahoe M_LINK_CHANGED isLinkDown=... voluntary_down=...
     voluntary_up=... reason_or_rssi=0x...`) and the absence of
     `expected=32 actual=0` rejections for
     `APPLE80211_M_LINK_CHANGED` in the userspace handler.
   - Confirm absence of `expected=24 actual=0` for
     `APPLE80211_M_BSSID_CHANGED` (because the local publication for
     that event code is no longer emitted on Tahoe).
   - Stop the host lab AP via
     `<project-root>/stop-itlwm-lab-ap.sh`.
   - Stability window: 60 s minimum on the lab AP association.
5. Final client verification on the CONTROL_STA_NETWORK alias only
   after the lab AP step shows the carrier-layer claim. Capture
   association, DHCP/IP evidence, the same XYLog line, and a 60 s
   stability window. Wi-Fi credentials are redacted via the alias.

The Stage 1 rev4 IOCTL response diff at HEAD `0f1124853c5d` is the
necessary follow-on once the carrier layer ships: with the carrier
fixed, userspace will accept the upstream LINK_CHANGED event delivery,
proceed to invoke `APPLE80211_IOC_LINK_CHANGED_EVENT_DATA`, and exercise
the V1 / V2 IOCTL publisher XYLog that the rev4 Stage 2 evidence was
unable to capture. That follow-on Stage 2 is out of scope for this
analysis and remains an open future stage on the same correlation id.

### Explicit non-claims

- DHCP / IP success on the FAST_LAB_AP alias or on the
  CONTROL_STA_NETWORK alias is not claimed by this layer. The
  pre-existing post-association pre-DHCP-stable drop recorded in the
  CR-470..CR-479 status artifacts is unchanged.
- AP / GO functional runtime and the Skywalk role-7 explicit-delete
  capability gap remain explicit follow-up layers.
- SNR / NF / CCA producer parity, beacon-loss reason classification,
  BSSID-change-reason producer, IP-protect window publication, and
  missed-beacons FSM publication remain explicit follow-up layers and
  are not part of this carrier-layer scope.
- Final Stage 2 approval of the rev4 IOCTL response diff is not in
  scope for this Stage 1; this Stage 1 narrowly fixes the carrier so
  that a future runtime can exercise the rev4 publisher path.
- This analysis does not propose a 24-byte
  `APPLE80211_M_BSSID_CHANGED` writer. The decomp closure explicitly
  rules out a standalone writer for that event code and the diff
  suppresses the local zero-length publication accordingly.

### Sensitive-artifact hygiene

This analysis report, the patch artifact, the proposed commit message,
the source-code comment, and the XYLog string contain no literal lab
identifiers, no literal final/control STA network identifiers, no
Wi-Fi authentication values, no sudo credentials, no provider login
accounts, and no raw runtime credential logs. References to test
networks use the project aliases FAST_LAB_AP, CONTROL_STA_NETWORK, and
CONTROL_AP_MODE_PROFILE only. A sensitive-token scan over the report
and the patch artifact returns zero hits.
