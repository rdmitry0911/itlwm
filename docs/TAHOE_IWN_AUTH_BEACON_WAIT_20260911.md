# IWN roam packet loss and AUTH beacon admission — 2026-09-11

Implementation follow-up: [owned nonblocking AUTH preparation](TAHOE_IWN_AUTH_BEACON_CONTINUATION_20260911.md).
The evidence below remains the immutable predecessor result.

## Observed result, not a completed correction

The running `17b63574` candidate reconnects with native WPA3/SAE and DHCP,
but the controlled BSS-switch row is **not lossless**. No production code or
public release is changed by this checkpoint. It narrows the next real
user-facing correction to the blocking AUTH preparation path, without
claiming that all roam loss has the same cause.

Owned VM: `aiam-iwn-after-scd-control`, QEMU PID `1346908`, boot
`E8E936A5-94B0-44A1-9C16-736EAB2470F5`, loaded Mach-O UUID
`7B77B2CB-D2B3-3B72-BCBF-5EFF73B5F53A`. Production manifest remains
`61462712c76c1614681a01eeb7349d603751149e73f353a53bda7824e74256bd`.
The physical `.22` host, unrelated QEMU and base disks were not touched.

## Dual-endpoint RF capture

Evidence root:
`/home/dima/Projects/itlwm/aiam-iwn-roam-datapath.JqVxWt`.
All 23 input/evidence files in `evidence.sha256` pass strict SHA-256 checking.
Manifest SHA-256:
`ea5bd4b1276125cbfbd42cc321c06cd04475e6717580a88ce5bb38b447f42436`.
This includes the observer, exact guarded request harness, both pcaps,
packet decodes and the ICMP identifier/sequence comparison script/results.

The one accepted native `Apple80211Set(...,107,...)` request at
09:23:31.358 UTC selected `9a:fb:5d:97:a9:02`, channel 13, from the actual
source `82:c3:97:84:51:ca`, channel 9. It was not resubmitted. The final
firmware RUN RXON names the requested BSS; boot, loaded image, WPA3 and
guest address `172.16.66.219` remain unchanged. No fixture AP, network toggle,
profile rewrite or ARP flush was used in this row.

At 1400-byte ICMP payload, 250 requests at 0.2-second intervals:

| Direction | Received | Maximum RTT |
| --- | ---: | ---: |
| Guest Wi-Fi → router `172.16.66.1` | 247/250 | 176.315 ms |
| Host AX211 `172.16.66.226` → guest Wi-Fi | 246/250 | 210.732 ms |

The management USB channel carried only the observers and command. Traffic
used Wi-Fi in both directions. Both packet captures report zero kernel
capture drops. The guest capture has 1001 packets and the host capture 506;
their different filters/directions make those totals incomparable as loss
counters. Trace errors and `ieee80211_encap` NULL returns were zero. All 509
observed `iwn_tx` admissions returned zero. The 18 observed nonaggregate TX
completions had zero ACK retries; **aggregate final ACK ownership was not
instrumented**, so this is not proof that every admitted packet was ACKed.

ICMP matching, not cross-machine clock subtraction, establishes:

- Guest → router: requests 63, 64, 65 (ID 14852) are visible at guest BPF,
  with no matching replies there. Guest timestamps fall between
  09:23:36.454662 and 09:23:36.859235 UTC.
- Host → guest: requests 66, 67, 68 (ID 54194) are visible at host BPF,
  absent at guest BPF, around the same transition.
- Reverse request 113 is visible at both endpoints and a reply is visible
  at guest BPF, but absent at host BPF. This occurs roughly nine seconds
  later and is a **separate unresolved loss**, not explained by the AUTH wait.

Host/guest wall clocks differ by tens of milliseconds. Do not calculate
one-way latency or causal microsecond ordering by subtracting those clocks.
BPF output also precedes final firmware transmission; it is not an ACK.

## Exact blocking interval

The loaded source's static `iwn_newstate_impl` entry for AUTH has state 2,
argument 192 and cleanup generation zero. Its guest trace interval is
09:23:36.486126878–09:23:36.795385020 UTC: **309.258142 ms**.
Association and RUN then complete at approximately 09:23:36.869 and
09:23:36.877. The production `iwn_auth()` contains:

```cpp
if ((arg == -1 || bss_switch) && ic->ic_mgt_timer == 0)
    DELAY(ni->ni_intval * 3 * IEEE80211_DUR_TU);
```

`DELAY` maps to `IODelay`. With the observed 100-TU beacon interval this
is **307.2 ms of synchronous busy-wait** after RXON, PAN priority, TX power
and broadcast-station preparation. The duration explains almost all of
the measured AUTH call interval. It overlaps the principal loss window;
that does not yet prove it is the sole drop mechanism or establish the
improvement from a fix which has not run.

`scripts/test_iwn_auth_nonblocking.sh` extracts and executes the **complete
production function** under ASan/UBSan. Firmware calls, channel setup and
`DELAY` are explicitly doubled, not simulated hardware. Both Linux and
macOS produce:

- `incoming`, `retry`: four preparation steps, zero busy-wait, exit 0.
- `command-failure`: failure at the first command, zero wait, exit 0.
- `roam`, `cold`: four preparation steps and `busy_us=307200`; the required
  nonblocking assertion fails with exit 134.

These deliberately red cases are excluded from the passing aggregate. They
will not by themselves qualify a future asynchronous owner or RF behavior.
Source SHA-256:
`a605def341ab53881bdf2ad8db1911c49bf092a6da6312e78f3d4add5c31d2bf`.
Linux log `/tmp/aiam-iwn-auth-nonblocking-linux-20260911.log` SHA-256:
`b71b47a13a3e7259b44251df899c94204d8b7e90cdceb8a8345bcc052ac78382`.
macOS log `/tmp/aiam-iwn-auth-nonblocking-macos-20260911.log` SHA-256:
`74273a84e0ee61b4a732ab1831b4c4c7edc50acaf485a60d1927a5fce9eb02b4`.

## Reference and hardware constraints

The exact 25C56 Core/WCL AUTH, reassociation-response and roam-completion
exports were reread from the private updated Ghidra `5995e24caa` tool.
That prior export used 40 actual decompiler interfaces; its native and
JumpTable provenance is recorded in the reassociation lifecycle notes.
The reference publishes actual AUTH observations separately from the full
168-byte roam completion. It is not evidence that Intel DVM can transmit
without satisfying its own firmware admission rules.

Linux DVM stops station queues after a passive-channel TX rejection and
releases them on a beacon matching an active context's BSSID. Its source
explains that a new RXON may reset the firmware's permission to transmit.
See the pinned primary sources:
[Linux v6.18 DVM RX](https://github.com/torvalds/linux/blob/v6.18/drivers/net/wireless/intel/iwlwifi/dvm/rx.c#L605-L619)
and [DVM TX](https://github.com/torvalds/linux/blob/v6.18/drivers/net/wireless/intel/iwlwifi/dvm/tx.c#L1114-L1136).
This supports waiting for actual reception, not deleting the restriction or
using a cached scan result as post-RXON evidence.

## Next implementation and qualification gate

Preferred design under investigation: defer the **generic AUTH admission**,
not just `ic_mgtq`. Ordinary Open-System AUTH and driver-resident SAE diverge
after that boundary: the former uses the management queue, the latter has
its own gated direct TX producer. Blocking only management dequeue would
leave SAE uncovered. Returning NotReady to SAE as a substitute is also not
equivalent: its existing gate-contention retry window is under 100 ms.

Required ownership before removing the wait:

1. Copy source state, intended state/argument, target BSSID/channel, join
   sequence/generation and association epoch. Do not retain a mutable node
   as the operation identity. Return accepted internal deferral, not a
   public busy/error or a synthetic successful AUTH event.
2. Enroll the exact RXON command before its real doorbell, keep its receipt
   distinct from any preceding reset RXON, and require a subsequent valid
   target beacon from hardware RX. A cached probe/beacon, wrong BSSID/channel,
   stale command index, failed RXON or timer expiry must not authorize TX.
   Account for command/beacon terminal arrival before continuation is armed.
3. Resume `sc_newstate` exactly once on the main workloop after revalidating
   the copied operation. Do not repeat `iwn_auth`/RXON, advance the association
   epoch again, replay a superseded join, or allow old ordinary data into
   the replacement's unassociated firmware context while deferred.
4. A bounded timer is a failure/cancellation edge only. Capture the correct
   fresh-join or generation-zero roam failure owner; do not invent an IEEE
   peer status, borrow a fresh JoinAdapter generation, or acknowledge cleanup
   which has not retired. Stop, reset, replacement and detach must close the
   owner and drain any continuation producer before releasing its storage.
5. Test actual lower wiring plus receipt-before-arm, replacement with the
   same BSSID, stale RX, command failure, timeout, stop/detach and no duplicate
   Open/SAE submission. `commandSleep` is not a blanket substitute: an AUTH
   transition may itself execute inside the RX workloop callback.

Then commit/push, manifest/build/private admission, activate only on a
recoverable owned guest copy, verify loaded UUID, and repeat the captured
250-packet roam row in both directions. Re-run open/WPA2/WPA3 first join,
off/on and actual S3 with external DHCP/traffic. Preserve adverse rows and
compare old/new timings; a single subsequent clean control cannot erase
earlier losses. GUI-after-S3, active AP across S3, full IWM/IWX retirement,
generation-zero roam event payloads and physical source-drain remain open.
The autonomous objective is unchanged and active; this is a diagnostic/test
checkpoint, not a claimed functional closure or release qualification.
