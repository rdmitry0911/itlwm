# Reassociation failure lifecycle — 2026-09-11

## Scope and current result

The fresh-AUTH SAE correction `97fe747c` is qualified and published; it must
not manufacture a JoinAdapter generation for an already-connected roaming
request. The next layer is the distinct accepted roam lifetime, including
failure, progress and actual completion. This checkpoint reconstructs that
contract and reproduces two production-helper defects. It does **not** claim
a new production fix, hardware qualification or replacement release.

The preceding user-status turn was read-only. This continuation produced new
raw-reference evidence and executable negative controls. Physical host
10.90.10.22 and all VM/radio state were left unchanged.

## Exact reference, not the historical selector shorthand

Source: unmodified `/home/dima/Projects/ghidra_input/BootKC_guest_25C56.kc`
on 10.7.6.112, SHA-256
`eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d`.
The old `kernel-roam-link[-memmove]` exports truncate ordinary callers because
allocation/logging helpers are marked no-return. The later roam-FSM export
corrects those annotations, but the Core completion builders needed the same
repair. A read-only symbol-bounded batch now covers 51 relevant Core,
NetAdapter, ScanAdapter and WCL producer/consumer functions. All 51 completed;
the manifest records 40 actual parallel decompiler interfaces, not only a CPU
limit. The reference project was not saved.

Remote evidence:
`/home/dima/Projects/ghidra_output/aiam_reassoc_terminal_25C56_20260911.jcC8d4/`.
Local evidence: `/tmp/aiam-roam-policy.Kj1vgE/reassoc-terminal-20260911/`.
Manifest SHA-256:
`52e0e6bb943a3e208a040ee6dcbe5d33d69e96826b4c3e6b34fa0a2aa0fcd249`.

A separate fileset parser reads the actual bytes, verifies overlapping segment
mappings agree, and decodes the level-zero chained pointers. It independently
recovers all 60 FSM cells and **31** subscriptions from this target, not the
six-entry historical 25D125 YAML excerpt. Init assembly selects configuration
`0xffffff80023e2ca0`, subscriptions `0xffffff80023dc7b0`, count `0x1f`.
The raw subscription key combines selector in the high 16 bits and message
type in the low 16 bits; e.g. `0xcf0002` is event selector `0xcf`.

| Reference event | Exact Skywalk payload | Meaning / consumer |
| --- | --- | --- |
| `0xcf` | 4 bytes | Nonzero asynchronous **command-start** error; `setReassocFail` only clears request bookkeeping |
| `0x89` | 12 bytes | Actual roam-scan start; `handleRoamStartEvent` sends FSM event 2 |
| `0x8a` | 216 bytes | Roam-scan end/results; `handleRoamScanEnd` sends FSM event 3 |
| `0x8b` | 12 bytes | Roam preparation, raw reason, target BSSID and RSSI; FSM event 4 |
| `0x49` | 8 bytes | Actual reassociation status/reason; only zero first dword advances FSM event 5 |
| `0x50` | 168 bytes | Overall roam completion/status; FSM event 6 and NetManager's deferred `tryReassoc` |
| `0xd8` | 16 bytes | Independent actual link indication, including lost replacement; not replaced by `0xcf` or `0x50` |
| `0x4a` | 104 bytes | Actual authentication status, independent of fresh JoinAdapter ownership |

Core `startRoamScanAsyncCallback` at `0xffffff8001591b14` publishes `0xcf`
only for a nonzero command result. `WCLRoamManager::setReassocFail` at
`0xffffff8002104928` clears IVars word `+0x3c` and qword `+0x40`, marks the
message consumed, and does not call the FSM or stop its protection timer.

Core `handleReassocEvent` at `0xffffff800159fd24` emits `0x49`, except for
firmware status 6. Its status mapping is distinct from the reason mapping:
nonzero status <=255 uses `0xe0820400 | status`; its event reason <=45 uses
`0xe0823000 | reason`. Out-of-range values use `0xe3ff8100`. This is not a
license to put an IEEE SAE status or an errno in the firmware-status field.
`handleAuthEvent` at `0xffffff800159f7f0` emits the separate `0x4a` before
calling JoinAdapter; the latter may have no active fresh request during roam.
A failed SAE Confirm is an AUTH observation, not a received reassociation
response. Merely changing its terminal selector to `0x49` would be wrong.

Core `postRoamCompletionStatus` at `0xffffff80015ba1be` emits `0x50` with
168 bytes to Skywalk (only 4 bytes to the legacy interface). It defers a
successful result until the target channel is populated. A later
`buildRoamCompletionStatus` at `0xffffff80015ba540` can finish that deferred
publication. The raw instruction stream independently verifies selector and
length; the older decomp's early no-return truncation hid the final cleanup.
Status originates at IVars `+0x36b0`; source/target BSSIDs are payload
`+0x58/+0x5e`. The AUTH/ASSOC observation flags share `+0x74` (bits 1/2),
AUTH status/reason are `+0x78/+0x7c`, ASSOC status/reason `+0x80/+0x84`.
These observations must survive until terminal publication, not be recreated
from whichever BSS happens to be current later. Remaining extended fields
must be decoded before representing the full carrier as implemented.

`WCLRoamManager::roamStart` arms a 10,000-ms protection timer. A real
`roamDone` stops it and clears bookkeeping/roam-lock state. The raw FSM
returns ROAM_SCAN, ROAM_REASSOC or WAIT_ROAM_DONE to LINK_UP on event 6,
executing that action; a timeout returns to LINK_UP via ignore instead.
Thus the timer is not an equivalent completion implementation. Independent
link-down still performs the already-recovered lost-association teardown.

Raw evidence hashes:

- FSM/subscriptions report `reassoc-fsm-raw-20260911.tsv`:
  `e84e803939ff967b01400f3df217aeb972deff6812f71d0d9c5b729c3984a318`.
- Raw 120-byte FSM table:
  `f4888eac65748eb62baccbe5aaf59dc4f5697819214740c41a7ae56897aff23f`.
- Raw 31 x 24-byte subscription table:
  `69d6515015a0e42c84633681665aea1df21e4ccda74072b44c01b5fdc8845978`.
- All 51 raw-byte function disassemblies, with per-function hashes:
  `reassoc-terminal-original-bytes-20260911.asm`, SHA-256
  `2bf9a46bae8597788f7cd999da5bf72448be10b2ee3171dc148ebd599124208b`.

## Local gap and executable negative controls

At `7e624176`, the common reassociation owner consists of mutable active/leaf,
request and source/target address fields, without a separate immutable serial.
The current controller emits only success `0x49` and generic failure `0xcf`
for this owner. The `0x89` constant exists without an actual event producer;
the corresponding accepted-roam `0x8b`/`0x50` producers are also absent.
The old comments describing `0xcf` as all response/timeout failures are not a
complete representation of the recovered contract. Neither is the historical
claim that the mere accepted scan must end via `0xcf` when no target is found.

The complete unchanged `ieee80211_wcl_reassoc_post_failure` calls
`ieee80211_pae_assoc_epoch_begin` **before** clearing its owner. The actual
epoch implementation releases its selected-BSS leaf, delivers credential/MFP
cancellation callbacks and WCL roam-link-loss notification, then returns.
The failure helper resumes by clearing the current mutable owner and posting
an unqualified event, without revalidating which request now owns the fields.

`scripts/test_wcl_reassoc_failure_retirement.sh` compiles that exact complete
helper and its production leaf predicate under ASan/UBSan. The callback inside
epoch cancellation is an explicit adversarial scheduling boundary; this is
not a claim that both schedules have already been observed on air.

- Ordinary inactive, setup, scan, switched-target and sequential duplicate
  controls pass.
- Replacement admitted during cancellation: the old helper returns with
  `active=0 request=0 target=0`, erasing the replacement. Required preservation
  assertion fails with exit 134.
- Reentrant retirement during cancellation: two epoch advances and two
  terminal events. Required one-shot assertion fails with exit 134.

The fixture's default mode intentionally fails on current production. It is
not yet part of the passing aggregate. `WCL_REASSOC_EXPECT_DEFECTS=1` verifies
the two expected assertion failures and explicitly prints that neither is
fixed; its zero wrapper exit is not a regression pass. Baseline log:
`/tmp/aiam-roam-policy.Kj1vgE/reassoc-retirement-baseline-20260911.log`, SHA-256
`fbc7625ea076959388e3cee6bc1821f3100518f6ff59d22fc14e37712c08f163`.

## FIX_CANDIDATE / implementation obligations

Retain a distinct accepted-roam serial and immutable source/target identity
through scan admission, target switching, AUTH/ASSOC/key results and actual
lower/SAE cleanup. Claim retirement under the selected-BSS leaf **before**
callbacks can reenter. No old continuation or delayed controller-gate delivery
may clear or publish against a later request, even on the same BSSID/SSID.
Detached terminal values must retain their result and generation independently
of future mutable owner fields. This must include IWN/IWM/IWX call sites,
not only a value-only helper disconnected from firmware completion.

Separate command rejection, scan progress, actual AUTH/ASSOC observations,
overall roam completion and actual source-link loss. Emit progress only from
real lower edges, and normal completion only when its corresponding operation
and result are actually known. Do not send a fabricated `0x49` for an SAE
rejection, create a fresh JoinAdapter request, shorten timeouts as a substitute,
or force a different BSSID outside native policy. Keep `97fe747c` fresh-AUTH
retirement and the independent lost-replacement `0xd8` correction intact.

Before qualification: finish the terminal/scan carrier fields and exact
status-domain mapping; execute production admission/retirement/dispatch with
replacement, nested cancellation, delayed lower completion, actual peer error
and timeout cases; fix both negative controls; run adjacent three-family
tests and macOS build. Then exercise accepted successful/failed/no-target and
superseded roaming on the loaded image, real candidate progression, DHCP and
bidirectional traffic, followed by the GUI/S3/AP regression gates and release
readback. Publishing `0x50` alone does not prove failed-BSS selection policy
or the whole roam outage has been corrected.
