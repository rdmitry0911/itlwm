# Physical WCL scan observation census — 2026-09-09

## Reproduced user-visible failure

The released `150d7e73` IWN/6235 guest loaded UUID
`431DFEA8-4B59-3A52-B4B9-91677A7619B1`. A separate controlled open AP was
advertised by the external AX211 on 2.4 GHz, concurrent with its managed
connection. Wired management and the guest's saved WPA3 STA were unchanged.

At 17:26:04 UTC a native CoreWLAN single-channel scan issued a real physical
command and received that AP. The production metadata builder recorded sample
1155123983, no prior publication receipt, RSSI -33 dBm and flags `0x4046`.
CoreWLAN returned one result with measured RSSI. This proves on-air discovery,
not merely the host AP's ENABLED status.

The controlled AP was disabled at 17:27:38 UTC. At 17:27:41 another native
request issued another physical scan. Its terminal nevertheless published
the same node with sample=issued=1155123983 and flags `0x46`: no fresh
observation had occurred. Apple created a new BSS object with RSSI/time zero,
and CoreWLAN returned the disabled AP with `AGE=0`, RSSI 0. Both bounded
observers completed without diagnostic errors. Intel node offsets came from
the loaded candidate's DWARF; Apple inner offsets came from matching 25C56
BootKC disassembly, not the different 26.3 layout.

## Reference and correction

The complete saved 25C56 DriverKit
`AppleBCMWLANScanAdapter::processScanResults` at `0x10018a9a4` validates
the records in its incoming firmware buffer, builds beacon metadata for
each accepted record and calls `postMessageInfra`. It does not walk all
historical driver cache identities for each physical completion. This is
producer evidence, not a claim that the reference's separate system cache
instantly evicts a vanished BSS.

The Intel terminal collector previously walked the entire node tree. The
RSSI receipt correctly avoided refreshing an old measured value, but could
not prevent the enclosing old identity/IE message from creating a new Apple
cache entry. The physical terminal now restricts its value-only snapshot to
beacon/probe observations received in this request's window and channels.
The exact active channel plan must match the terminal generation.

The observation stamp is independent of RSSI validity: an accepted real
frame without usable signal metadata remains a discovery result. Starting a
local IBSS/AP clears received-observation state. Ordinary immediately admitted
requests capture their window before lower submission, including a terminal
that arrives before begin() returns. IWN's queued initial request moves that
floor to its existing post-WRPTR STARTED edge, excluding the foreground
predecessor's observations. Duplicate STARTED or the later setter acknowledgement
cannot move the floor again. Cancellation, terminal ownership, multi-band
continuation and the separately requested legacy cache route are unchanged.

Old and out-of-plan nodes are rejected before capacity accounting. This does
not flush the node tree, invent RSSI, widen channel permissions, change dwell,
or force the system's own cache retention policy.

## Verification boundary

The ASan/UBSan production-function regression now executes the actual
observation recorder, channel-plan predicate, terminal collector and
controller's activation/window method. Cases cover a queued predecessor,
exact-boundary receipt, wrong generation/backend, duplicate acknowledgement,
already completed/cancelled ownership, both bands, absent plans, invalid
channels, unavailable RSSI, and stale/out-of-plan entries at capacity. The
old collector fails the first stale-census assertion. Existing physical-scan
lifecycle, exact-plan, queued-band, SSID-refresh, APSTA epoch/roam and firmware
dwell regressions pass.

The correction is not yet built or runtime-qualified. Missing physical
5-GHz reception, all UI/saved-profile reconnect combinations and equivalent
recent IWM/IWX hardware qualification remain separate work.
