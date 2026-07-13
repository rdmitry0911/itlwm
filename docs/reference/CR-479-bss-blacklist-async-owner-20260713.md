# BSS blacklist async owner contract (25C56)

## Scope

This note records the exact Tahoe 25C56 contract behind Apple80211 selector `0x174`.
It replaces the earlier interpretation of the local setter/getter as
a synchronous 43-byte cache pair. The source change covered here reconstructs
the controller-owned request/applied state, the bounded public-route P0
semantics, and a local result publication model. It does not claim Intel firmware MACLIST support,
callback-equivalent lower async behavior, or
complete lower selection policy parity.

## Public route and wrappers

The current BootKC user-client routes are:

- GET `0xffffff80021dbe61`, ioctl `0xc02869c9`;
- SET `0xffffff80021e8899`, ioctl `0x802869c8`.

Both routes require non-null data and a request length of exactly `0x2b`.
They return raw `0x16` for a null/incorrect-size carrier and raw `0x66` when
the interface is absent. The public carrier is:

```text
u8 count
u8 bssid[7][6]
```

The public wrappers are GET `0xffffff80021c1cfb` and SET
`0xffffff80021c6f95`. Each first checks selector admission through interface
vtable `+0xcc8(this, 0x174)`, returns any non-zero status unchanged, and
returns `0xe082280e` when the required interface cast fails. GET dispatches
through vtable `+0x1000`; SET dispatches through `+0x1218`.

The local selector-bearing BSD bridge now preserves the same public ordering:
`processBSDCommand` evaluates the BSS-only preflight before forwarding, so a
missing interface returns raw `0x66` ahead of an inner null/incorrect carrier
(`0x16`). `processApple80211Ioctl` validates the exact carrier, runs its
existing ABI-stable boolean `isCommandProhibited(0x174)` gate (raw `0` or
`1` locally), then maps only a missing `instance` owner to `0xe082280e`.
The generic outer `data == NULL` bridge remains inherited because it cannot
read a selector from a nonexistent `apple80211req`; this note claims the
recovered selector-bearing route rather than changing unrelated ioctl paths.

## Core owner

`AppleBCMWLANCore::setBSS_BLACKLIST` is at `0xffffff800161008a`.

- Null input returns `0xe00002bc`.
- All 43 request bytes are copied into Core-private state before lower work.
- Count zero selects MACMODE 0; a non-zero count selects MACMODE 3 on the
  supported path.
- The top-level setter launches mode/list programming and their async queries,
  ignores each helper status, and returns success.
- The lower MACLIST builder at `0xffffff8001610696` rejects when count is at least `8`.
  Because the top-level setter ignores that result, the requested
  43-byte Core state changes while the effective lower list remains the prior
  valid list.

`AppleBCMWLANCore::getBSS_BLACKLIST` is at `0xffffff8001610fd2`. It never
reads or writes the synchronous caller buffer. It obtains the command gate and
runs action `0xffffff8001610b8e`, which launches an async MACLIST query and
returns the launch status.

## Async result

The GET-list callback is `0xffffff8001610db0`. On non-zero status, null
payload, or count zero it emits no event. For a successful non-empty result it
posts message `0xa3` asynchronously with this variable carrier:

```text
u32 count
u8  bssid[count][6]
u8  trailing[2]
```

The posted length is `6 * count + 6`, from 12 bytes for one BSSID through 48
bytes for seven. The two bytes follow the last actual BSSID; they are not at a
fixed offset except in the seven-entry case. Their semantic values are not
recovered. The local owner initializes them to zero and makes no stronger
claim.

SET also launches the GET-list query after programming. The reference only
publishes `0xa3` when that asynchronous query reaches its callback with status
zero, a non-null payload, and a non-zero count. Static recovery therefore does
not guarantee an event for every SET/GET or a particular lower-query result
after a zero/count-eight SET. A callback payload with count zero suppresses
publication.

The local controller models the prior applied list explicitly. Its valid
non-empty SET and a following explicit GET each enqueue their own `0xa3`
snapshot; count zero emits none, and count eight returns success while
preserving the prior applied snapshot. This is a constrained local publication
model, not a claim that a lower query/callback has been recovered: it cannot
represent a lower non-zero status or null callback payload. The targeted
runtime probe gates those local observations separately from the conditional
reference callback.

## Candidate policy evidence

`WCLJoinCandidateSelector::fillCandidateDenyListedStatus` is at
`0xffffff80020f776c`, with its per-candidate block at
`0xffffff80020f7f76`. It marks a candidate deny byte and does not remove the
candidate. Comparator block `0xffffff80020f7d9c` sorts a clear deny byte before
a set deny byte, then preserves preference/RSSI ordering. WCL therefore
deprioritizes deny-listed candidates and retains fallback candidates.

The Core membership helper `0xffffff8001610cb0` has no code/data/vtable
callers in the authoritative 25C56 image. This proves that the helper itself is
not a scan-hiding path. Firmware MACMODE 3 enforcement remains unlabeled, so
this owner layer does not add scan filtering, association failure flags, or a
hard BSSID exclusion.

## Local implementation boundary

The Tahoe port now:

- routes selector-bearing BSD GET/SET `0x174` with `0x66` interface precedence,
  an exact 43-byte carrier gate (`0x16`), admission before the owner cast, and
  raw `0xe082280e` for the proven absent-owner branch;
- stores requested and applied state in the controller-owned
  `ieee80211com` lifetime domain;
- mutates/snapshots that state under the controller command gate;
- preserves prior applied state for count eight or larger while SET succeeds;
- leaves the GET caller buffer untouched;
- posts controller-level `0xa3` only for a non-empty applied list;
- retains state across radio OFF/ON and system sleep/wake transitions.

The next separate layer may map the proven WCL deprioritization/fallback policy
onto local autonomous net80211 selection. Lower MACMODE/MACLIST callback
semantics and that behavior are deliberately not part of this ABI/event owner
change.

## Exact artifacts

The authoritative Ghidra project is
`wifi_analysis_25C56/BootKC_guest_25C56.kc`. Recovery artifacts are named:

- `aiam_bss_blacklist_disasm_25C56_20260712.txt.gz`;
- `aiam_bss_blacklist_helper_listing_25C56_20260712.txt.gz`;
- `aiam_bss_blacklist_get_wrapper_raw_current_25C56_20260713.txt.gz`;
- `aiam_bss_blacklist_set_wrapper_raw_current_25C56_20260713.txt.gz`;
- `aiam_bss_blacklist_get_route_raw_current_25C56_20260713.txt.gz`;
- `aiam_bss_blacklist_set_route_raw_current_25C56_20260713.txt.gz`;
- `aiam_bss_blacklist_wcl_fill_block_raw_current_25C56_20260713.txt.gz`;
- `aiam_bss_blacklist_wcl_sort_block_raw_current_25C56_20260713.txt.gz`;
- `aiam_bss_blacklist_wcl_joinrequest_flag_strings_current_25C56_20260713.txt.gz`;
- `aiam_bss_blacklist_core_helper_raw_exact_current_25C56_20260713.txt.gz`;
- `aiam_bss_blacklist_isbssid_decoded_pointer_scan_current_25C56_20260713.txt.gz`.

Deterministic gzip copies are checked in under
`docs/reference/artifacts/bss-blacklist-25C56/`; their hashes and the source
contract tokens are verified by `scripts/bss_blacklist_async_owner_report.py`.

No address from an older 26.3/KDK layout is used as a current BootKC address.
