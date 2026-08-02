# Tahoe WCL open join retry epoch — 2026-08-01

## User-visible gap

An IWM station could authenticate and associate with an open AP on air while
Tahoe still rejected the join.  The host AP reported the station as
`AUTH/ASSOC/AUTHORIZED`, but the driver published neither a usable DHCP path
nor the terminal WCL connect completion.  The failing serial signature was:

```text
authentication timed out for 80:e4:ba:20:ef:f9
wcl_auth SUCCESS_LEDGER captured=0 result=0xe00002d8
wcl_assoc VALIDATED_COMPLETION captured=0 result=0xe00002d8
wcl_open RUN_COMPLETION result=0xe00002d8
```

## Reference boundary

The checked Tahoe 25C56 reference decompile is:

```text
10.7.6.112:/home/dima/Projects/ghidra_output/
cr357_wcl_join_lifecycle_boundary_repair_20260508_1005_decomp.txt
```

`AppleBCMWLANJoinAdapter::performJoin()` resets the per-join state before the
firmware attempt.  Successful `handleAuth()` and `handleAssoc()` events both
copy the event BSSID into the same persistent JoinAdapter field at `+0x28c`.
`getBSSInfoAsyncCallback()` later reaches `sendConnectComplete()`, whose gates
are fields in that same stable JoinAdapter attempt.  A retry therefore does
not discard the successful BSSID merely because the preceding response timer
expired; a later successful event still belongs to the live join attempt.

## Local root cause and repair

`ieee80211_node_join_bss()` already begins one controlled replacement epoch,
copies the selected node into `ic_bss`, and publishes its fixed-byte selected
BSS snapshot.  Two local actions violated that ownership:

1. a management timer from the superseded BSS remained armed while IWM
   asynchronously prepared the new candidate;
2. an `AUTH -> AUTH` retry then called the generic `ieee80211_new_state()`
   wrapper, which advanced the epoch a second time and invalidated the
   snapshot just published by the controlled replacement.

The selected-BSS owner now clears the superseded management timer before the
backend handoff.  The next real management TX arms its own response timer.  It
also preserves the backend preflight and passive state trace while submitting
the controlled replacement directly, so the generic non-forward cancellation
cannot erase the new epoch a second time.  Ordinary state callers retain the
generic epoch fence.

## Validation

All default, AP/STA opt-out, and IWN software-PMF Tahoe builds succeeded and
resolved all `1074/1074` undefined symbols against the target BootKC.  The
loaded disposable-VM candidate was UUID
`90333622-4CF0-3EFD-964B-D21260412340`, binary SHA-256
`043f47559772561c4c325a2c1ba7a605effa160a171dd2062685826fbe75cfba`.

With host AX211 BSS `AIAM-OPEN-09202d0` on channel 153, the same real timeout
and retry now produced:

```text
wcl_auth SUCCESS_LEDGER captured=1 result=0x00000000
wcl_assoc VALIDATED_COMPLETION captured=1 result=0x00000000 armed=1 published=1
wcl_open RUN_COMPLETION result=0x00000000
```

The guest obtained `10.77.0.176`, passed 10/10 ICMP packets and an HTTP 200
transfer through `en1`.  A real S3 sleep/wake repeated the exact completion,
retained the loaded UUID and DHCP address, and again passed 10/10 ICMP plus
HTTP 200.  The host AP was restored to its original `AIAMlab6235` WPA2/SAE
transition configuration after the test.
