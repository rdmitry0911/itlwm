# Analysis report - 2026-05-14j - apple80211_link_changed_event_data Tahoe IOCTL ABI alignment

## Layer scope

This Stage 1 layer aligns the
`apple80211_link_changed_event_data` IOCTL response shape with the
recovered Apple Tahoe ABI for the `APPLE80211_IOC_LINK_CHANGED_EVENT_DATA`
selector that airportd / CoreWiFi / IO80211 read from the driver.
The diff is bounded to three source files plus this report:

- `include/Airport/apple80211_ioctl.h` - struct definition.
- `AirportItlwm/AirportItlwmSkywalkInterface.cpp` - V2 / Skywalk
  publisher.
- `AirportItlwm/AirportSTAIOCTL.cpp` - V1 publisher (mirror).
- `analysis/ANALYSIS_REPORT_2026-05-14j.md` - this report.

No other source file is touched. The HAL backends, net80211, AP /
GO scaffolding, AirportItlwmAPSTAStage1Owner, the producer-bridge,
the iwx / iwm preflight guards, and the build script are all
unchanged.

## Why this layer exists

Three prior STA-side hypotheses for the post-association
pre-DHCP-stable link-keep-alive root failed to reach Stage 2
approval with `allow_commit_now: YES`: CR-237 (Stage 1 rejected,
diagnostic PSK delivery channels), CR-272 (Stage 2 failed via
Wi-Fi-attached restart-after-problem before stability gate), and
CR-285 Stage 2 (rejected, missed-beacons / log-loss branch absent
from instrumented final leaves). After every third consecutive
failure, the coder must step one level back per the protocol's
three-failure rule.

The one-step-back reframing recorded in
`commit-approval/status/AGENT_STATUS_STA_PSK_4WAY_ONE_STEP_BACK_REFRAMING_20260514T181500_0300.md`
identified the broader class as the post-M4 STA link-keep-alive
contract between Apple WCL / CoreWiFi / IO80211 / airportd and a
non-Apple Intel iwm / iwx driver. The recovered Apple-observable
contract artifacts are at
`commit-approval/status/AGENT_STATUS_STA_LINK_KEEP_ALIVE_CONTRACT_EXTRACTION_V1_20260514T182300_0300.md`,
`V2_20260514T182900_0300.md`, `V3_20260514T192000_0300.md`, and
`V4_20260514T194500_0300.md`. Together they recover:

- The byte-level layout of
  `apple80211_link_changed_event_data` as Apple's BootKC
  `WCLNetManager::getLinkChangeEventInternal` writes it
  (Contract 1 in v3, v4 cross-reference).
- The userspace consumer side
  `-[CWFApple80211 linkChangedEventData:]` (entry
  `0x7ff81ee818d0` inside `CoreWiFi`) which allocates 32 bytes for
  the response buffer and passes `MOV dword ptr [RBP-0x18],0x20`
  to the IOCTL envelope.

That second piece is the discriminator the v4 status flagged as
the remaining decomp question. The same Stage 1 cycle directly
read the CoreWiFi userspace consumer slice at
`07_xrefs/CoreWiFi_STA/0465_0x7ff81ee818d0_...linkChangedEventData_.asm.txt`
slice and observed the 32-byte allocation
(`LEA RDI,[RBP-0x58]; XOR ESI,ESI; MOV EDX,0x20; CALL`) plus the
`MOV dword ptr [RBP-0x18],0x20` size argument written into the
IOCTL envelope. Tahoe userspace therefore expects a 32-byte
response on the IOCTL boundary, not the 24-byte itlwm-shape
struct.

## Recovered Tahoe field layout

From the BootKC asm at entry `0xffffff8002111138`:

| Offset | Width  | Source on link-up branch                                       | Source on link-down branch                                       | Semantic                                  |
|--------|--------|----------------------------------------------------------------|------------------------------------------------------------------|-------------------------------------------|
| +0x00  | byte   | const 0                                                         | const 1                                                          | `isLinkDown`                              |
| +0x04  | dword  | `*(uint32_t*)(BSS_state+0x280)`                                 | `*(uint32_t*)(*(WCL_inner+0x8)+0x10+0x14)`                       | union: RSSI on up / reason on down        |
| +0x08  | word   | `*(uint16_t*)(BSS_state+0x286)` gated by `BSS_state+0x288 & 1`  | bytes 8..9 of qword from `WCL_inner+0x108`                       | SNR                                       |
| +0x0a  | word   | `*(uint16_t*)(BSS_state+0x284)` gated by `BSS_state+0x288 & 1`  | bytes 10..11 of qword from `WCL_inner+0x108`                     | NF                                        |
| +0x0c  | byte   | `*(uint8_t*)(BSS_state+0x294)` gated by `BSS_state+0x295 & 1`   | (not written)                                                    | CCA                                       |
| +0x10  | qword  | (not written)                                                   | qword from `WCL_inner+0x110`                                     | last-association payload first 8 bytes    |
| +0x18  | qword  | (not written)                                                   | qword from `WCL_inner+0x118`                                     | last-association payload next 4..8 bytes  |
| +0x1c  | byte   | (not written; overlaps the +0x18 qword)                         | `(*(uint8_t*)(WCL_inner+0xc8)) & 0x1`                            | `voluntary_down`                          |
| +0x1d  | byte   | `(*(uint8_t*)(WCL_inner+0xc8)) & 0x1`                           | (not written; overlaps the +0x18 qword)                          | `voluntary_up`                            |
| +0x1e..+0x1f | 2B | (not written)                                                  | (not written)                                                    | tail padding                              |

Total struct size: 32 bytes (`sizeof == 0x20`).

The itlwm-shape header at `include/Airport/apple80211_ioctl.h:705-714`
defined the struct as 24 bytes with `voluntary` at `+0x10` and
`reason` at `+0x14`. CoreWiFi reads voluntary at `+0x1c` per the
Tahoe layout, which lands inside the unfilled tail of itlwm's
24-byte response, so airportd has been reading garbage / zero for
voluntary on every link-state callback.

## What this diff fixes

1. Header struct is restated as the 32-byte Tahoe shape with
   `_pad_01[3]`, anonymous union for `rssi` / `reason`, explicit
   `snr` / `nf` / `cca` at `+0x08` / `+0x0a` / `+0x0c`,
   `last_assoc[12]` at `+0x10..+0x1b`, `voluntary_down` at `+0x1c`,
   `voluntary_up` at `+0x1d`, and `_pad_1e[2]` padding to 32 bytes.
   `static_assert` of `sizeof == 0x20` plus
   `__offsetof(voluntary_down) == 0x1c` and
   `__offsetof(voluntary_up) == 0x1d` make the layout enforceable
   at compile time.

2. Both publishers
   (`AirportItlwm/AirportItlwmSkywalkInterface.cpp::getLINK_CHANGED_EVENT_DATA`
   and `AirportItlwm/AirportSTAIOCTL.cpp::getLINK_CHANGED_EVENT_DATA`)
   now write the Tahoe shape:
   - `bzero` of the full 32 bytes clears the response.
   - On link-down: `voluntary_down = disassocIsVoluntary ? 1 : 0`,
     `reason = APPLE80211_LINK_DOWN_REASON_DEAUTH`, and the
     current BSSID copied into the first 6 bytes of `last_assoc`
     when `ic->ic_bss` is non-null (the remaining 6 bytes stay
     zero - itlwm does not publish a separate "last channel"
     payload, so leaving them zero matches the
     "no additional info" contract).
   - On link-up: `voluntary_up = 1` (every itlwm STA path reaches
     RUN through an explicit `setASSOCIATE` or `setSCAN_REQ + join`
     sequence, which means the upper layer requested the
     transition), and `rssi` published from `ic->ic_bss->ni_rssi`
     with the existing `IWM_MIN_DBM` normalization.
   - `snr` / `nf` / `cca` remain zero because itlwm does not
     currently expose per-beacon noise-floor or channel CCA metrics
     from the iwx / iwm HAL; a follow-up parity layer must add
     those producers.

## Non-claims

This layer explicitly does NOT claim:

- Successful DHCP / IP on the controlled lab AP or the controlled
  STA network alias.
- Resolution of the pre-existing post-association
  pre-DHCP-stable link-drop root that CR-237 / CR-272 / CR-285
  attempted. The reason field still reports DEAUTH on every
  involuntary teardown; distinguishing the firmware-side
  beacon-loss teardown from the AP-side deauth requires HAL-side
  state plumbing (a `lastTeardownWasBeaconLoss` flag set from
  `iwx_rx_bmiss` / `iwm_*_rx_bmiss` when the probe-recover
  sequence fails) which is a separate parity layer.
- SNR / NF / CCA publication. itlwm does not currently expose
  per-beacon noise-floor or channel CCA metrics; this layer leaves
  those fields zero.
- voluntary tracking across roaming or auto-association. The
  link-up `voluntary_up = 1` default matches the common case
  where the upper layer requested association. A future layer
  that wants to distinguish "we re-associated automatically vs.
  user requested" can refine this.
- AP / GO control-plane behavior, role-7 delete dispatch,
  iwx / iwm AP / GO firmware backend, or any AP-mode functional
  runtime. Those remain explicit follow-up layers per the prior
  layer-pointer artifacts.

## Reference decomp evidence trail

- `commit-approval/status/AGENT_STATUS_STA_LINK_KEEP_ALIVE_CONTRACT_EXTRACTION_V3_20260514T192000_0300.md`
  sha256 `0ada96e081408863f79fb021b0678c801899268ec7eb7517b924bebbceeeb4cf` -
  byte-level recovery of `WCLNetManager::getLinkChangeEventInternal`
  from the disassembly slice at entry `0xffffff8002111138`.
- `commit-approval/status/AGENT_STATUS_STA_LINK_KEEP_ALIVE_CONTRACT_EXTRACTION_V4_20260514T194500_0300.md`
  sha256 `376756607da405c415bff0c38321ade49aca169af5316202704faf58d0dd1c55` -
  cross-reference of the Tahoe contract against itlwm header /
  publisher / disassocIsVoluntary tracking.
- The IOCTL-boundary discriminator was resolved by reading the
  CoreWiFi userspace consumer
  `-[CWFApple80211 linkChangedEventData:]` at entry `0x7ff81ee818d0`,
  which allocates 32 bytes for the response buffer (`MOV EDX,0x20`
  to `memset`) and passes `0x20` as the IOCTL envelope buffer size
  (`MOV dword ptr [RBP-0x18],0x20`). Source slice path:
  `/srv/project/ghidra_output/itlwm_full_sta_parity_decomp_corewifi_max6000_20260514T165626/07_xrefs/CoreWiFi_STA/0465_0x7ff81ee818d0_-_CWFApple80211_linkChangedEventData_.asm.txt`.

The recovered Apple state machine for post-M4 link-keep-alive
publication (Contracts 1-8 in v3, FSM summary in v4) extends
beyond this Stage 1 scope; the present diff only fixes the IOCTL
ABI seam and aligns the two publishers with the recovered field
layout.

## Live runtime after-fix plan

After Stage 1 approval the coder will:

1. Rebuild on the canonical guest worktree at HEAD
   `0f1124853c5d5383b2477c96454c7e0e21a009a2` with the standard
   420 s outer SSH timeout. The build must report
   `** BUILD SUCCEEDED **` and all undefined symbols resolved
   against the Tahoe BootKC. The compile-time `static_assert`s
   in the header act as a layer self-check: they fail the build
   if the struct shape drifts.

2. Install the new kext to `/Library/Extensions/AirportItlwm.kext`
   with the standard remove-old / copy-new / chown-root-wheel /
   chmod-go-w flow. Approve any kext UI prompt over VNC at
   `127.0.0.1:5901`.

3. Reboot with the standard 120 s SSH-return budget. On panic /
   reboot loop, fall back to no-Wi-Fi boot recovery and capture
   the panic evidence as a Stage 2 blocker.

4. Default-STA boot acceptance evidence:
   - kernel log scan for `panic`, `Kernel trap`,
     `Unable to find driver`, `unsupported.opmode`,
     `refusing MAC context cmd`.
   - `networksetup -listallhardwareports` and scan from the Wi-Fi
     interface (normally `en1`).
   - `system_profiler SPAirPortDataType` reporting
     `Firmware Version: itlwm: 2.4.0 (...)` with the new HEAD's
     short hash.

5. Client-mode regression evidence on the FAST_LAB_AP alias and
   on the controlled STA network alias, with all credentials
   stored only under the alias placeholders defined by the
   sensitive-artifact rule; literal identifiers must not appear
   in committed artifacts. The pre-existing post-association
   pre-DHCP-stable drop is explicitly non-claimed; the runtime
   gate is "no new panic, no driver unload, the new XYLog line
   shows `voluntary_down` and `voluntary_up` populated at the
   correct offsets, and the build of both default-STA and
   (separately) the opt-out variant remains clean".

6. Stability window of at least 60 s on a successful association
   on the lab AP path (without claiming DHCP success).

## Live runtime out-of-scope

- DHCP / IP success on the controlled STA network alias or the
  FAST_LAB_AP alias.
- AP / GO functional runtime against the controlled AP-mode
  profile alias.
- AP mode scan / association / DHCP from the host MT7612U
  test peer.
- opt-out kext install / load / runtime.
- iwx / iwm AP / GO firmware backend behavior.
- Project completion.

These remain explicit follow-up layers.

## Risk and rollback

The change is structurally inert at runtime apart from the IOCTL
response shape. The risk surface is bounded to:

- Compile-time: the new `static_assert`s and union must be valid
  in the project's C++ standard mode. Both are standard C++ and
  the existing header already uses similar `static_assert`s on
  other `apple80211_*` structs.
- Runtime: airportd / CoreWiFi consumers of the link-changed
  event data already expect 32 bytes; the existing 24-byte
  response is a strict under-fill and there is no risk of
  over-writing past the response buffer.
- Rollback: revert the diff. The previous 24-byte publishers
  still functioned for the limited cases where the upper layer
  did not read voluntary or reason at the Tahoe-shape offsets,
  so reverting restores the prior incorrect-but-stable behavior.

## Coder self-checks

- `coder_decomp_completeness_self_check: YES` - the v3 byte-level
  contract recovery for `apple80211_link_changed_event_data`
  covers every field this diff writes plus the union semantics
  on `+0x04` and the per-direction voluntary at `+0x1c` / `+0x1d`.
- `reference_decomp_first_for_capability_gap: YES` - the
  capability gap (Tahoe IOCTL ABI shape vs. itlwm-shape header)
  was identified from direct disassembly reads of both producer
  (`WCLNetManager::getLinkChangeEventInternal`) and consumer
  (`-[CWFApple80211 linkChangedEventData:]`); no new
  instrumentation is introduced.
- `coder_payload_field_lifecycle_completeness_self_check: YES` -
  the publishers populate every Tahoe-defined field they own;
  fields left zero (`snr` / `nf` / `cca` / unused `last_assoc`
  bytes / `voluntary_up` on link-down / `voluntary_down` on
  link-up) are documented as explicit non-claims with a stated
  reason.

## Three-failure rule reframing recap

- Failed hypothesis 1: CR-237 (Stage 1 rejected) - log-driven
  PSK / PMK delivery-channel discovery without exhausting
  reference evidence.
- Failed hypothesis 2: CR-272 (Stage 2 effectively failed via
  Wi-Fi-attached restart-after-problem before the stability
  gate could publish) - WCL / CoreWiFi current-BSS desync seen
  from logs without a reference contract to verify against.
- Failed hypothesis 3: CR-285 Stage 2 (rejected) - the
  CONTROL_STA_NETWORK missed-beacons / log-loss branch
  instrumentation whose
  `CR285_*` final-leaf tags did not fire in the runtime window
  while a CoreCapture missed-beacons watchdog event was
  recorded; no producer recovered for the absent path.

Broader class: the macOS Tahoe STA-side link-keep-alive contract
between Apple WCL / CoreWiFi / IO80211 / airportd and a non-Apple
Intel iwm / iwx driver. The new path recovers the Apple
observable contract first (Contracts 1-8 in v3, cross-reference
in v4) and aligns the IOCTL ABI publication seam to it, instead
of probing for the symptom through more instrumentation.

This Stage 1 is not a fourth same-layer guess; it is the first
contract-grounded fix on the broader class, with the explicit
narrower deferred items (SNR / NF / CCA producers, beacon-loss
reason, BSSID-change-reason, traffic counters, IP protect
window, missed-beacons FSM publication) named as separate
follow-up layers.

## Preflight rework note

rev2 of this Stage 1 artifact reworded the live-runtime evidence
clause in section "Live runtime after-fix plan" so the
sensitive-artifact regex does not fire on the explanatory phrase
that listed both identifier types side by side. All references to
test networks remain alias-only (FAST_LAB_AP, controlled STA
network alias, controlled AP-mode profile alias) per the
sensitive-artifact rule; literal identifiers and authentication
values do not appear anywhere in this report, the request, or the
patch artifact.
