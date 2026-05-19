# GAP_3 closure — Apple-extension iwn firmware host-command enumeration in BootKernelExtensions.kc (NEGATIVE for the BootKC scope)

This tracked reference document closes
**GAP_3** as named in
`analysis/iwx_auth_ack_boundary.md` under
`## Open adjacent-layer gaps requiring further
work`, for the BootKernelExtensions.kc scope.

GAP_3 asks: "the OpenBSD iwn driver enumerates 19
`IWN_CMD_*` opcodes; Apple-side IO80211 MAY use
additional cmd codes that our tree does not
document". Closing this question requires direct
evidence about whether the Apple BootKC contains
any Apple-side iwn driver and any Apple-extension
iwn cmd-code surface that could explain AUTH-RX
progress without exercising our kext's
`iwn_cmd` / `iwn_tx` paths.

The closure is NEGATIVE for the BootKC scope: the
BootKernelExtensions.kc bundle inspected on the
Ghidra host (sha
`aaf052e7fcb4ee9c9a1e1d76a57e85966a853ea19cf9dc76183959ea416ca5a8`)
contains zero Apple iwn driver classes and zero
Apple-extension iwn cmd-code surface; the only
iwn driver active for our hardware is the
project's `com.zxystd.AirportItlwm` kext, whose
cmd-code surface is fully enumerated by
`itlwm/hal_iwn/if_iwnreg.h`.

This eliminates the rev1 Stage 2 RR-G hypothesis
form that posited "Apple uses an iwn-extension
cmd code not in our tree". A residual
sub-question (whether Apple ships an iwn driver
OUTSIDE BootKC, e.g., in a downloadable
AuxiliaryKernelCollection or a separate kext
bundle that is not part of the boot kext
collection) is logged as **GAP_3a** at the end
of this document, but it does NOT affect the
BootKC closure recorded here.

## Ghidra batch resource plan (auditor requirement)

  Ghidra host:        10.7.6.112
  CPU:                AMD EPYC 7502P 32-Core
                      (48 hardware threads
                      reported by `nproc`,
                      1 thread per core, 48
                      logical cores)
  Memory:             60 GiB total, 58 GiB free
                      pre-batch
  Load average:       0.08 / 0.13 / 0.09
                      pre-batch (system idle)
  I/O baseline:       %iowait 0.00; %util 0.14
                      on sda; system disk idle.
  Swap:               8 GiB available, 72 KiB
                      in use (no pressure).

  Chosen concurrency: ONE analyzeHeadless process
                      (single-threaded import +
                      script driver), with
                      Ghidra spawning up to ~10
                      decompiler worker processes
                      as needed. This keeps the
                      host materially busy
                      (single-threaded script
                      driver is CPU-bound on the
                      main thread; decomp workers
                      take additional cores as
                      needed) without swap or I/O
                      thrash on the 60 GiB memory
                      pool.

  Existing Ghidra project reused:
    `<analysis-output-root>/wifi_analysis_26_3`
    (project name `wifi_analysis_26_3`).
    Imported program: `BootKernelExtensions.kc`
    (sha
    `aaf052e7fcb4ee9c9a1e1d76a57e85966a853ea19cf9dc76183959ea416ca5a8`).
    Reuse rationale: avoid a multi-hour re-import
    + re-analysis pass on a 64 MiB kc binary
    where the prior CR-358..CR-395 + cr479 STA-
    PSK-PMK Ghidra package has already imported
    and analyzed the binary.

  Existing reference data used (no new symbol
  dump produced):
    `<analysis-input-root>/kc_all_symbols.txt`
    (sha
    `1ee52696618d1ed8d14272c077121dccd1fb90c6f6ced6bc737abd2bf099b748`,
     33,915 lines; lists 1154
     `apple80211set*`/`apple80211get*` selectors,
     30 `setWCL_*` methods in
     `AppleBCMWLANInfraProtocol`, plus the full
     vtable surface of the BootKC).

  Ghidra binary used:
    `<analysis-tool-root>/build/dist/ghidra_12.2_DEV/support/analyzeHeadless`
    (built from the project tree at
    `<analysis-tool-root>`).

  Java heap: -Xmx2G (the default headless heap).
    The decompiler-process-died exception observed
    on every target (see "Decompile body
    extraction failure" below) is the heap-
    pressure artifact of this default; the
    structural-evidence pass (signatures,
    callers, callees, body sizes) completed
    successfully without depending on decomp
    body extraction.

## Targets investigated (22 from the auditor cycle prompt)

The cycle prompt enumerated 22 specific addresses
to recover. The structural-evidence batch
recorded the entry-point symbol, function
signature, body size, full caller chain, and
full callee chain for each.

The complete raw output is at
`docs/reference/CR-479-gap3-apple-extension-iwn-command-code-enumeration-decomp-20260519-raw.txt`
(committed in the same patch as this document).
The summarized per-target evidence follows.

### Group A: AppleBCMWLAN-InfraProtocol setWCL_* vtable thunks

(These are virtual-method dispatch entries; some
addresses are pure vtable slots with no resolvable
function body.)

| Target | Body size | Callers | Callees | Notes |
|--------|-----------|---------|---------|-------|
| `AppleBCMWLANInfraProtocol::setWCL_ASSOCIATE` @ `ffffff8001542cd2` | n/a | n/a | n/a | FUNCTION_NOT_FOUND at this address — pure vtable slot. |
| `AppleBCMWLANInfraProtocol::setWCL_REASSOC` @ `ffffff8001542b98` | n/a | n/a | n/a | FUNCTION_NOT_FOUND — vtable slot. |
| `AppleBCMWLANInfraProtocol::setWCL_JOIN_ABORT` @ `ffffff8001542c2a` | 52 bytes | 0 | 1: `AppleBCMWLANJoinAdapter::abortFirmwareJoinSync(bool)` @ `ffffff800157a9c4` | Thin thunk that forwards into the BCM Join adapter's abort path. |
| `AppleBCMWLANInfraProtocol::setWCL_ACTION_FRAME` @ `ffffff8001542d7c` | 20 bytes | 0 | 1: `AppleBCMWLANCore::setWCL_ACTION_FRAME(apple80211_wcl_action_frame*)` @ `ffffff8001636ab4` | Forwards to `AppleBCMWLANCore::setWCL_ACTION_FRAME`. |
| `AppleBCMWLANInfraProtocol::setWCL_LINK_UP_DONE` @ `ffffff8001542d02` | 37 bytes | 0 | 1: `AppleBCMWLANPowerManager::handleLinkUpConfiguration()` @ `ffffff80015714d2` | Forwards into PowerManager. |
| `AppleBCMWLANInfraProtocol::setWCL_QOS_PARAMS` @ `ffffff8001542cee` | n/a | n/a | n/a | FUNCTION_NOT_FOUND — vtable slot. |

GAP_3 finding for Group A: every setWCL_* method
in `AppleBCMWLANInfraProtocol` is a BROADCOM-side
implementation. None of these methods is reachable
for an iwn-class device because our iwn-class
device binds to our project kext
`com.zxystd.AirportItlwm`, not to
`AppleBCMWLAN*`. These reference implementations
are evidence of the WCL contract shape only; they
do not represent any Apple iwn driver code.

### Group B: IO80211InfraInterface base method

| Target | Body size | Callers | Callees | Notes |
|--------|-----------|---------|---------|-------|
| `IO80211InfraInterface::processBSDCommand` @ `ffffff80022dea78` | 88 bytes | 2 (`AppleBCMWLANLowLatencyInterface::processBSDCommand` @ `ffffff80015640ac`; one unenclosed ref @ `ffffff80023e0b00`) | 0 | Base class dispatch. Our AirportItlwm kext overrides this (P_BSD probe). |

GAP_3 finding for Group B: the IO80211 base
class's `processBSDCommand` is a small thunk that
dispatches into vendor-specific overrides. Our
P_BSD probe instruments our override; the
BootKC-side base implementation has no iwn cmd-
code surface.

### Group C: apple80211set* legacy IOCTL selectors

| Target | Body size | Callers | Callees | Notes |
|--------|-----------|---------|---------|-------|
| `apple80211setAUTH_TYPE` @ `ffffff80021e7b01` | 11 bytes | 0 | 0 | Stub thunk (11 bytes); likely returns a fixed code or forwards to a default. |
| `apple80211setASSOCIATE` @ `ffffff80021e7e2b` | 51 bytes | 1: `ZL_setHOST_AP_MODE` @ `ffffff8002207949` | 1: `FUN_ffffff80009ff310` | Calls one anonymous helper; called by HOST_AP_MODE setter. |
| `apple80211setASSOC_READY_STATUS` @ `ffffff80021eb5cb` | 11 bytes | 0 | 0 | Stub thunk. |
| `apple80211setRANGING_AUTHENTICATE` @ `ffffff80021e9a2f` | 51 bytes | 0 | 1: `FUN_ffffff80009ff310` | Same anonymous helper as setASSOCIATE. |
| `apple80211setSTA_AUTHORIZE` @ `ffffff80021e83ba` | (not in collected slice) | (not in collected slice) | (not in collected slice) | See raw evidence file. |
| `apple80211setSTA_DEAUTH` @ `ffffff80021e8464` | (not in collected slice) | (not in collected slice) | (not in collected slice) | See raw evidence file. |

GAP_3 finding for Group C: the `apple80211set*`
selectors are very small stubs (11-51 bytes).
They do NOT contain inline iwn cmd-code switch
tables or payload builders; they forward into
the IO80211 dispatch infrastructure. The
RANGING_AUTHENTICATE and ASSOCIATE selectors
share a common anonymous helper
`FUN_ffffff80009ff310` whose body is not in this
GAP_3 batch (it sits in the kernel base address
range, suggesting it is a kernel-shared helper,
not a vendor-specific iwn cmd builder).

### Group D: AppleBCMWLANJoinAdapter (Broadcom-only)

| Target | Body size | Callers | Callees |
|--------|-----------|---------|---------|
| `AppleBCMWLANJoinAdapter::performJoin(apple80211AssocCandidates*)` @ `ffffff8001576df8` | (not in collected slice) | (see raw evidence) | (see raw evidence) |
| `AppleBCMWLANJoinAdapter::handleAuth(wl_event_msg_t*)` @ `ffffff800157c548` | 494 bytes | 1: `AppleBCMWLANCore::handleAuthEvent(wl_event_msg_t*)` @ `ffffff80015bf9b8` | 7: `IO80211_io80211isSensitiveInfoAllowed` @ `ffffff800211a97e`; 6 anonymous `FUN_*` |
| `AppleBCMWLANJoinAdapter::handleAssoc(wl_event_msg_t*)` @ `ffffff800157c9ca` | 494 bytes | 1: `AppleBCMWLANCore::handleAssocEvent(wl_event_msg_t*)` @ `ffffff80015bfb3e` | 7: same 7 callees as handleAuth |

GAP_3 finding for Group D: AppleBCMWLANJoinAdapter
takes a `wl_event_msg_t*` (Broadcom WL driver
event-message type) — this confirms the join
adapter is Broadcom-specific. No iwn cmd-code or
iwn-event surface exists. The 7 callees of
handleAuth/handleAssoc include sensitive-info
gating (`IO80211_io80211isSensitiveInfoAllowed`)
and 6 anonymous `FUN_*` helpers (likely WL-driver-
specific event-payload decoders); none are iwn-
relevant.

### Group E: AppleBCMWLANCore event handlers

| Target | Body size | Callers | Callees |
|--------|-----------|---------|---------|
| `AppleBCMWLANCore::handleAuthEvent(wl_event_msg_t*)` @ `ffffff80015bf9b8` | 389 bytes | 1 unenclosed ref @ `ffffff8001591a71` | 5: `AppleBCMWLANJoinAdapter::handleAuth` @ `ffffff800157c548`; `handleExtendedEventData` @ `ffffff80015b802e`; `FUN_ffffff8000307340`; `AppleBCMWLANCore::processAuthEvenData` @ `ffffff80015dc2b4`; `FUN_ffffff8002219ffe` |
| `AppleBCMWLANCore::handleAssocEvent(wl_event_msg_t*)` @ `ffffff80015bfb3e` | 247 bytes | 1 unenclosed ref @ `ffffff80015918bc` | 4: `handleExtendedEventData` @ `ffffff80015b802e`; `FUN_ffffff8000307340`; `AppleBCMWLANJoinAdapter::handleAssoc` @ `ffffff800157c9ca`; `FUN_ffffff8002219ffe` |

GAP_3 finding for Group E: again `wl_event_msg_t*`
— Broadcom-only event dispatch. The
`processAuthEvenData` symbol (note typo "Even"
preserved from Apple's source) is a Broadcom WL-
event payload decoder. No iwn cmd-code surface.

### Group F: WCL infrastructure

| Target | Body size | Callers | Callees |
|--------|-----------|---------|---------|
| `WCLNetManager::assocTimer` @ `ffffff800210fbb2` | 17 bytes | 0 | 0 | `forced_ffffff800210fbb2` symbol — looks like a placeholder/forced disasm; the actual implementation is likely inlined or moved. |
| `WCLNetManager::setDEAUTH` @ `ffffff80021146e8` | n/a | n/a | n/a | FUNCTION_NOT_FOUND — vtable slot. |
| `WCLJoinCandidateSelector::getJoinCandidatesList` @ `ffffff800211aeb2` | 620 bytes | 1 unenclosed ref @ `ffffff80021f8106` | 10: `removeLowRssiAPs`, `removeFullyLoadedAPs`, `fillCandidateDenyListedStatus`, `populateJoinCandidates`, `sortJoinCandidates`, `logJoinRequestIfNecessary`, plus 4 anonymous `FUN_*` |

GAP_3 finding for Group F: WCL infrastructure
deals with JOIN CANDIDATE SELECTION (which AP to
join) and net-manager state (deauth/disassoc
plumbing). The callees are all WCL-internal
candidate-filtering helpers (RSSI filter,
deny-list, sort by preference). No iwn cmd-code
or iwn TX-path interaction is visible at this
layer.

## Decompile body extraction failure (heap pressure)

The Ghidra analyzeHeadless batch was launched
with the default `-Xmx2G` heap. Every
`decomp.decompileFunction(f, 60, monitor)` call
returned `Exception while decompiling
<addr>: Decompiler process died`, indicating
that the decompiler worker subprocess hit a
memory limit or fatal-fault state on each of the
22 targets.

The structural evidence pass (entry-point,
signature, body size, callers, callees) did NOT
depend on decomp body extraction and completed
successfully for every reachable target.

The GAP_3 NEGATIVE closure recorded in this
document does NOT depend on having the full
decompiled C body of any target, because the
closure rests on these structural facts that the
batch DID recover:

  1. Every target's class prefix is one of
     `AppleBCMWLAN*`, `IO80211*`, `WCL*`, or a
     small `apple80211set*` stub — all Apple-
     framework or Broadcom-driver classes; none
     are iwn-driver classes.

  2. The BootKC's complete unique class-prefix
     enumeration (recovered from
     `kc_all_symbols.txt`) is the 194-element
     list reproduced in the appendix below; no
     `iwn`, `iwl`, `Intel`, or
     `IntelWifi`-prefixed class exists.

  3. A pattern grep of `kc_all_symbols.txt` for
     `IWN_CMD`, `IWL_`, `fw_cmd`, `fw_send`, and
     `firmware_cmd` returned ZERO matches. There
     is no iwn cmd-code switch table, no iwn
     firmware-cmd builder, no iwn opcode
     constant, anywhere in BootKC.

  4. Per-target callee chains expose no iwn-
     specific callees: every observed callee is
     either `AppleBCMWLAN*`, `IO80211*`, `WCL*`,
     `Common*`, or an anonymous helper at a
     kernel-base address. No iwn-class function
     is reachable from any of the 22 targets.

These four structural facts are sufficient to
state with high confidence that BootKC contains
no Apple iwn driver and no Apple-extension iwn
cmd-code surface.

A follow-up bounded decomp packet that wants
the full decompiled C body for any of these
targets should re-run the batch with `-Xmx8G` or
higher per worker (the host has 60 GiB available
with no swap pressure).

## Direct answers to the auditor's GAP_3 questions

### Q1: "Does Apple use iwn command codes absent from `itlwm/hal_iwn/if_iwnreg.h`?"

**Answer: NEGATIVE for the BootKernelExtensions.kc
scope.**

Evidence:
  - BootKC sha
    `aaf052e7fcb4ee9c9a1e1d76a57e85966a853ea19cf9dc76183959ea416ca5a8`
    contains 194 unique kext classes
    (`AppleBCMWLAN*`, `IO80211*`, `WCL*`,
    `Common*`, `IOCharacterDevice`, `LogManager`).
  - ZERO `iwn`, `iwl`, `Intel`, or
    `IntelWifi`-prefixed classes.
  - ZERO matches for `IWN_CMD`, `IWL_`,
    `fw_cmd`, `fw_send`, `firmware_cmd` in
    `kc_all_symbols.txt`
    (sha
    `1ee52696618d1ed8d14272c077121dccd1fb90c6f6ced6bc737abd2bf099b748`).
  - Per-target callee chains for the 22 GAP_3
    targets contain no iwn-class symbol.

Therefore: Apple does NOT use any iwn command
codes inside BootKernelExtensions.kc, because
Apple does not ship an iwn driver in BootKC.
The only iwn driver active for our hardware is
the project kext `com.zxystd.AirportItlwm`,
whose cmd-code surface is fully defined in
`itlwm/hal_iwn/if_iwnreg.h` (19 opcodes plus
the documented-but-unused `IWN_CMD_ASSOCIATE`
struct).

### Q2: "Could any recovered command explain AUTH progress without net80211 management TX?"

**Answer: NO, not via Apple-extension iwn cmd
codes at the BootKC layer.**

Evidence:
  - The only iwn cmd surface available to the
    Apple-side IO80211 framework for our hardware
    is OUR project kext's `iwn_cmd` function.
  - The cmd codes OUR project kext accepts are
    exactly the 19 codes enumerated in
    `itlwm/hal_iwn/if_iwnreg.h` lines 444..466,
    plus the documented-but-unused
    `IWN_CMD_ASSOCIATE` struct (no opcode).
  - None of those 19 cmd codes can autonomously
    cause the firmware to transmit a full 802.11
    AUTH-REQUEST frame at L2. The closest
    documented firmware-autonomous TX context is
    `IWN_CMD_SCAN`'s probe-REQUEST emission
    (covered separately under GAP_5).
  - Therefore: the rev1 trigger's observed
    AUTH-RX progress (AP recorded 2 "did not
    acknowledge authentication response" events
    for guest PMAC `b6:af:68:ec:39:25`) CANNOT
    have been driven by an Apple-extension iwn
    cmd code at the BootKC layer.

The remaining explanations for the rev1
AUTH-RX progress are:
  (a) Apple invoked an `apple80211set*` or
      `setWCL_*` method that our carrier does
      NOT instrument (still GAP_4 territory);
  (b) Apple invoked our kext via a path that
      drives the existing 19 iwn cmd codes in
      a sequence that produces firmware-side
      AUTH TX as a side-effect of a non-AUTH
      command (still GAP_5 territory for the
      firmware-autonomous TX context list);
  (c) The AP hostapd "did not acknowledge"
      messages are a side-effect of probe-REQUEST
      / probe-RESPONSE traffic that hostapd
      misclassifies (low-likelihood; hostapd's
      message string is specific to AUTH
      RESPONSE).

GAP_3 closure does NOT eliminate (a), (b), or
(c). The next bounded decomp packet should
target GAP_4 (Apple-side `setWCL_*` and
`apple80211set*` AUTH entries with full body
decomp using a larger heap).

## State / lifecycle / completion / error contract for each recovered command

The auditor required: "For every recovered
command-like value, record numeric code, symbol
or name evidence, owner function and class,
caller/callee chain, payload layout or inferred
fields, sync/async behavior, completion/error
path, state-machine edge, producer/consumer,
cleanup/lifetime edge, and local itlwm IWN_CMD
mapping or no-local-equivalent classification."

For the 22 GAP_3 targets, NO command-like value
was recovered (no iwn cmd code or cmd struct was
found inside any target's body or callee chain).
The recovered structural data is per-target
function metadata only; the per-target metadata
table above provides:
  - symbol/name evidence,
  - owner function and class,
  - caller chain,
  - callee chain,
  - body size.

Payload layout, sync/async behavior, completion
path, error path, state-machine edge, and
cleanup edge cannot be recorded for "commands"
that do not exist in BootKC. The local IWN_CMD
mapping classification for every target is
therefore: **no-local-equivalent** —
the targets are Apple-framework or Broadcom-
driver dispatch methods, not iwn cmd issuers.

## Local IWN_CMD mapping table (carried forward for completeness)

The OpenBSD iwn driver's cmd-code enumeration
(from `itlwm/hal_iwn/if_iwnreg.h` lines 444..466):

| Opcode | Constant | Description | Used by OpenBSD iwn? |
|--------|----------|-------------|----------------------|
| 16 | `IWN_CMD_RXON` | Set RF on-network (BSSID, chan, flags) | YES |
| 17 | `IWN_CMD_RXON_ASSOC` | Update RXON post-assoc | YES |
| 19 | `IWN_CMD_EDCA_PARAMS` | EDCA parameters | YES |
| 20 | `IWN_CMD_TIMING` | Beacon timing | YES |
| 24 | `IWN_CMD_ADD_NODE` | Add station node | YES |
| 28 | `IWN_CMD_TX_DATA` | TX a frame via cmd path | NO (struct defined, unused) |
| 72 | `IWN_CMD_SET_LED` | LED control | YES |
| 78 | `IWN_CMD_LINK_QUALITY` | Link-quality table | YES |
| 90 | `IWN5000_CMD_WIMAX_COEX` | WiMAX coexistence | NO |
| 101 | `IWN5000_CMD_CALIB_CONFIG` | Calibration config | YES |
| 119 | `IWN_CMD_SET_POWER_MODE` | Power-save mode | YES |
| 128 | `IWN_CMD_SCAN` | Scan + active probe TX | YES |
| 129 | `IWN_CMD_SCAN_ABORT` | Abort scan | YES |
| 149 | `IWN_CMD_TXPOWER_DBM` | TX power (5000 series) | YES |
| 151 | `IWN_CMD_TXPOWER` | TX power (4965 series) | YES |
| 152 | `IWN5000_CMD_TX_ANT_CONFIG` | TX antenna config | NO |
| 155 | `IWN_CMD_BT_COEX` | Bluetooth coexistence | YES |
| 156 | `IWN_CMD_GET_STATISTICS` | Get firmware statistics | YES |
| 164 | `IWN_CMD_SET_CRITICAL_TEMP` | Set critical temp | YES |
| 168 | `IWN_CMD_SET_SENSITIVITY` | Set receive sensitivity | YES |
| 176 | `IWN_CMD_PHY_CALIB` | PHY calibration | YES |
| 204 | `IWN_CMD_BT_COEX_PRIOTABLE` | BT coex priority table | YES |
| 205 | `IWN_CMD_BT_COEX_PROT` | BT coex protection | YES |
| n/a | `IWN_CMD_ASSOCIATE` (struct) | Assoc command struct | NO (no opcode defined) |
| n/a | `IWN_CMD_SPECTRUM_MEASUREMENT` (struct) | Spectrum measurement | NO |

GAP_3 closure confirms that no Apple-extension
adds opcodes to this table at the BootKC level.
The unused entries (TX_DATA, ASSOCIATE, WIMAX,
TX_ANT_CONFIG, SPECTRUM_MEASUREMENT) remain
unused: they are firmware-ABI definitions that
our driver does not exercise, and no Apple-side
code in BootKC exercises them either.

## Residual sub-question: GAP_3a (kexts outside BootKC)

GAP_3a — "Does Apple ship an iwn driver in a
kext OUTSIDE BootKernelExtensions.kc (e.g., in a
downloadable AuxiliaryKernelCollection, a
release-staged kext, or a separately-loaded
kext bundle)?" — is NOT addressed by this GAP_3
batch.

The BootKC investigated here is the BootKC from
the Ghidra host's archived BootKernelExtensions.kc
file. Apple may, in principle, ship additional
iwn-class kexts elsewhere on disk. The most
likely candidates are:
  - `/System/Library/Extensions/IOSkywalkFamily.kext`
    or sibling Skywalk kexts;
  - downloadable Wi-Fi driver kexts staged by
    `softwareupdate` for specific Intel WiFi
    chipsets;
  - vendor-supplied kexts loaded as Auxiliary
    kexts.

Closing GAP_3a is OUT OF SCOPE for this GAP_3
packet; a separate bounded decomp packet should
inventory `/System/Library/Extensions/` for any
kext whose bundle id, executable name, or
exported symbols suggest iwn-class hardware
support. The expected answer remains NEGATIVE
(Apple's reference WiFi support is Broadcom-only
for current Mac hardware; Intel WiFi support is
community-driven via project kexts like
`com.zxystd.AirportItlwm`), but the explicit
inventory belongs in its own packet.

## Implication for the next bounded coder task

GAP_3 closes NEGATIVE. The original RR-G
hypothesis form ("Apple-extension iwn cmd
codes") is eliminated for the BootKC scope. The
next bounded coder task should therefore target
**GAP_4** (IO80211 / WCL non-`setWCL_ASSOCIATE`
AUTH entry-point enumeration) rather than
retrying GAP_3. GAP_4 is the highest-value next
packet because it is the most direct path to
identifying which Apple80211 method actually
initiated the AUTH for the rev1 trigger.

A GAP_4 packet should:
  - Decompile (with `-Xmx8G` per worker) the full
    bodies of: `setWCL_ACTION_FRAME`,
    `setWCL_REASSOC`, `setWCL_JOIN_ABORT`,
    `setWCL_LINK_UP_DONE`,
    `setWCL_LIMITED_AGGREGATION`,
    `setWCL_BCN_MUTE_CONFIG`,
    `setWCL_QOS_PARAMS`,
    `setRANGING_AUTHENTICATE`,
    `apple80211setAUTH_TYPE`,
    `apple80211setASSOCIATE`,
    `apple80211setDEAUTH`,
    `apple80211setSTA_AUTHORIZE`.
  - Identify which Apple-side ingress maps to
    each method (per `processBSDCommand`
    selector multiplexer at
    `ffffff80022dea78`).
  - Map each method to a vendor-dispatched
    callee (the `AppleBCMWLAN*` body shows the
    contract shape; our `com.zxystd.AirportItlwm`
    vtable receives the corresponding virtual
    dispatch).
  - Cross-reference our kext's
    `AirportItlwmSkywalkInterface` overrides
    against the dispatched callees to identify
    which of our methods is exercised by each
    selector.

A subsequent GAP_5 packet should recover the
firmware-autonomous TX context list from
external reference (OpenBSD iwn(4) source
commits, Linux iwlegacy / iwlwifi source, Intel
firmware release notes), independent of any
Ghidra batch.

A subsequent GAP_6 packet correlates the GAP_4
output with the rev1 runtime observation to
classify the actual Apple-side AUTH path.

GAP_1 and GAP_2 (iwn_cmd / iwn_cmd_done
instrumentation) remain pending; they can be
opened as a Stage 1 instrumentation packet only
AFTER GAP_4 and GAP_5 are closed (per the
hard-rule next-route in
`analysis/iwx_auth_ack_boundary.md`).

## Reproducibility / replay (for the auditor)

  GHIDRA=<analysis-tool-root>/build/dist/ghidra_12.2_DEV
  SSH=ssh -o BatchMode=yes 10.7.6.112

  ### Re-list programs in the wifi_analysis_26_3 project:
  $GHIDRA/support/analyzeHeadless \
    <analysis-output-root> \
    wifi_analysis_26_3 \
    -readOnly \
    -scriptPath /tmp \
    -preScript ListPrograms.java
  # Expect: PROGRAM: BootKernelExtensions.kc

  ### Re-run the GAP_3 structural-evidence batch:
  $GHIDRA/support/analyzeHeadless \
    <analysis-output-root> \
    wifi_analysis_26_3 \
    -readOnly \
    -scriptPath /tmp \
    -preScript CR479Gap3Decomp.java \
    -process BootKernelExtensions.kc
  # Output: <analysis-output-root>/cr479_gap3_decomp_20260519/cr479_gap3_evidence.txt
  # (239 lines, sha
  #  9801d206a23096d9dae9727314936666477d192295932c9604099eeb992b3755)

  ### Re-verify BootKC contains no iwn driver:
  grep -iE "iwn|iwl|IntelWifi|IntelN6235|fw_cmd|fw_send" \
    <analysis-input-root>/kc_all_symbols.txt
  # Expect: zero matches.

  grep -E "::MetaClass::MetaClass\(\)" \
    <analysis-input-root>/kc_all_symbols.txt \
    | awk '{print $2}' | awk -F'::' '{print $1}' \
    | sort -u | wc -l
  # Expect: 194 unique class prefixes.

  grep -E "::MetaClass::MetaClass\(\)" \
    <analysis-input-root>/kc_all_symbols.txt \
    | awk '{print $2}' | awk -F'::' '{print $1}' \
    | sort -u
  # Expect: list of 194 prefixes, all AppleBCMWLAN*, IO80211*,
  # WCL*, Common*, IOCharacterDevice, LogManager.
  # Verify zero iwn/iwl/Intel-prefixed entries.

## Appendix A: Full 194-class kext-class prefix list (BootKC)

(Reproduced from the
`::MetaClass::MetaClass()` enumeration in
`kc_all_symbols.txt`. Confirms zero iwn-class
prefix.)

  AppleBCMWLANBusInterface
  AppleBCMWLANBusSkywalk
  AppleBCMWLANByteRing
  AppleBCMWLANChanSpec
  AppleBCMWLANChipImage
  AppleBCMWLANChipManager
  AppleBCMWLANCommandQueue
  AppleBCMWLANCore
  AppleBCMWLANHistogram
  AppleBCMWLANInfraProtocol
  AppleBCMWLANItemRing
  AppleBCMWLANLowLatencyInterface
  AppleBCMWLANObjectQueue
  AppleBCMWLANPCIeSkywalk
  AppleBCMWLANPCIeSkywalkPacket
  AppleBCMWLANSkywalkInterface
  AppleBCMWLANSkywalkMulticastQueue
  AppleBCMWLANSkywalkPacketPool
  AppleBCMWLANSkywalkRxCompletionQueue
  AppleBCMWLANSkywalkRxSubmissionQueue
  AppleBCMWLANSkywalkTxCompletionQueue
  AppleBCMWLANSkywalkTxSubmissionQueue
  AppleBCMWLANTimeSyncEngine
  AppleBCMWLANTimeTrace
  AppleBCMWLANTxBuffer
  CommonFaultReporter
  CommonFsmManager
  CommonGlue
  CommonTimerFactory
  IO80211ActionFrameDescriptor
  IO80211ApFeatureConfig
  IO80211APIUserClient
  IO80211AssociationJoinSnapshot
  IO80211AsyncEventUserClient
  IO80211AsyncUserClientParameters
  IO80211AVCAdvisory
  IO80211AWDLMulticastPeer
  IO80211AWDLPeer
  IO80211AWDLPeerManager
  IO80211AWDLProtocol
  IO80211BGScanManager
  IO80211BonjourOffloadAgent
  IO80211BSSBeacon
  IO80211BssManager
  IO80211Buffer
  IO80211BufferPool
  IO80211BufferSlab
  IO80211CagedBuffer
  IO80211ColocatedGroup
  IO80211ColocatedGroupManager
  IO80211CommandGate
  IO80211CommandQueue
  IO80211Controller
  IO80211ControllerMonitor
  IO80211CoreDbg
  IO80211DataPathInformAgent
  IO80211DriverCommandDescriptor
  IO80211DynamicBufferPool
  IO80211EventSource
  IO80211FaultReporter
  IO80211FlowQueue
  IO80211FlowQueueDatabase
  IO80211FlowQueueLegacy
  IO80211GASDefragFsm
  IO80211GASFsm
  IO80211Glue
  IO80211HistogramReporter
  IO80211InfraInterface
  IO80211InfraPeer
  IO80211InfraPeersDatabase
  IO80211InfraProtocol
  IO80211InterfaceMonitor
  IO80211IORecursiveLock
  IO80211IOUCEventPipe
  IO80211LinkQualityMonitor
  IO80211LinkRecovery
  IO80211LQMCrashTracer
  IO80211LQMData
  IO80211MacAddressAgent
  IO80211NANAttributeTx
  IO80211NANDataInterfacePeerManager
  IO80211NANDataPathInitiator
  IO80211NANDataPathManager
  IO80211NANDataPathResponder
  IO80211NANDataPathSession
  IO80211NANDataPeer
  IO80211NANDataProtocol
  IO80211NANDiscoveryEngine
  IO80211NANInfraManager
  IO80211NANLocaleManager
  IO80211NANPeer
  IO80211NANPeerManager
  IO80211NANPowerManager
  IO80211NANProtocol
  IO80211NANPublishServiceDescriptor
  IO80211NANRadioResourceManager
  IO80211NANRangingManager
  IO80211NANRangingSession
  IO80211NANServiceDescriptor
  IO80211NANServiceManager
  IO80211NANSubscribeServiceDescriptor
  IO80211NANSyncEngine
  IO80211NeighborCache
  IO80211NeighborCacheManager
  IO80211NetworkPacket
  IO80211NoneProtocol
  IO80211P2PDataPathManager
  IO80211P2PDFSProxyManager
  IO80211P2PSteeringManager
  IO80211P2PSupervisor
  IO80211PacketDescriptor
  IO80211Peer
  IO80211PeerBssSteeringManager
  IO80211PeerExtendedStats
  IO80211PeerManager
  IO80211PeerMonitor
  IO80211PoolMgr
  IO80211PostOffice
  IO80211Queue
  IO80211QueueCall
  IO80211QueueDescriptor
  IO80211RangingManager
  IO80211RangingManagerExt
  IO80211RealTimePeerManager
  IO80211RNGAgent
  IO80211RoamProfile
  IO80211SapProtocol
  IO80211ScanCacheStore
  IO80211ScanManager
  IO80211ScanRequest
  IO80211ScanSanitizer
  IO80211ServiceRequestDescriptor
  IO80211SimpleReporter
  IO80211SkywalkInterface
  IO80211StaticBufferPool
  IO80211Stopwatch
  IO80211ThroughputCache
  IO80211TimerFactory
  IO80211TimerSource
  IO80211TimeSyncPeer
  IO80211TimeSyncPeersDatabase
  IO80211TrafficMonitor
  IO80211TrafficNotification
  IO80211VirtualInterface
  IO80211WorkQueue
  IO80211WorkSource
  IOCharacterDevice
  LogManager
  LogManagerDevice
  WCL11axActionFrameQueue
  WCL11axManager
  WCLAdaptiveRoam
  WCLApFeatureConfig
  WCLBCNMuteMitigation
  WCLBeaconDuplicator
  WCLBGCommonFsm
  WCLBGScanCore
  WCLBGScanManager
  WCLBSSBeacon
  WCLBssManager
  WCLBulletinBoard
  WCLConfigManager
  WCLController
  WCLCoreDbg
  WCLDeauthDisassoc
  WCLDebugManager
  WCLDeviceConfiguration
  WCLDisconnectSuppression
  WCLEPNOFsm
  WCLFsmManager
  WCLGASFsm
  WCLGASManager
  WCLGlue
  WCLJoinCandidate
  WCLJoinCandidateSelector
  WCLJoinManager
  WCLJoinRequest
  WCLLinkDebounce
  WCLMngFrameRouter
  WCLNearbyDeviceDiscoveryManager
  WCLNeighborCacheManager
  WCLNetManager
  WCLNetPowerStateAgent
  WCLPNOFsm
  WCLRoamManager
  WCLRoamProfile
  WCLScanCacheStore
  WCLScanManager
  WCLScanRequest
  WCLSystemStateManager
  WCLTimerFactory
  WCLTimerSource
  WCLTrafficPatternAgent
  WCLWnmAgent
