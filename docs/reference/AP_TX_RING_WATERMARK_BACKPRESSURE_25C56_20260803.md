# AP TX ring watermark backpressure on Tahoe 25C56

## User-visible failure

During sustained guest-to-station traffic, the IWN AP aggregate queue could
reach all 255 usable descriptors and then trigger the TX watchdog.  The most
recent runtime witness was queue 12 with `queued=255`, producer index 90 and
firmware/SCD consumer index 91.  The queue had completed roughly 1600 frames
before the first pending MPDU stopped advancing, while Skywalk kept filling
the remaining descriptors.

## Reference contract

Intel DVM uses a 256-descriptor software/hardware TX ring independently of the
63-frame Block Ack window.  The producer is stopped when the number of queued
descriptors crosses the high watermark (224) and is reopened only after it
falls below the low watermark (192).  The existing STA paths already implement
that hysteresis through `qfullmsk`, `IWN_TX_RING_HIMARK`/
`IWN_TX_RING_LOMARK` (and the corresponding IWM/IWX values).

This matches the ownership rule of Tahoe's Skywalk output path: a temporary
driver-resource shortage must leave the packet staged for retry rather than
drop it or keep dequeuing into a full hardware ring.

Primary comparison points:

- Linux `drivers/net/wireless/intel/iwlwifi/dvm/tx.c` TX enqueue and reclaim.
- Linux `drivers/net/wireless/intel/iwlwifi/pcie/gen1_2/tx.c` queue stop/wake.
- Tahoe reference controller free-space/requeue semantics already recorded in
  `AirportItlwmV2::skywalkTxAction`.

## Implementation

- IWN AP PAN and aggregate admission returns no resources at the high
  watermark or while the queue-full bit remains set.
- IWM raw AP submission sets the same queue-full state as its STA path and
  rejects further AP frames while it is active.
- IWX dynamic AP queue IDs can exceed the width of its legacy 32-bit
  `qfullmsk`; each IWX ring therefore carries an equivalent AP-only full latch
  and clears it at the ring's low watermark without an out-of-range shift.
- AP free-space queries return zero during that high-to-low hysteresis interval.
- IWM/IWX AP completion paths update low-water state before asking Skywalk to
  resume dequeue.  IWN already had the equivalent AP-aware wake in
  `iwn_clear_oactive`.
- Ring capacity remains 256; no BA-window-sized artificial ring limit is used.

## Runtime acceptance

The IWN candidate completed a 180-second, four-stream WPA3 AP-to-station run
with 329 MiB received.  Admission stopped at `queued=225`, immediately above
the 224 high watermark, rather than filling the ring to 255.  The run produced
neither a TX watchdog nor a firmware reset, and bidirectional traffic remained
usable afterwards.

Tahoe sends an explicit `HOST_AP_MODE(NULL)` before S3.  The reference treats
that request as a normal terminal AP stop, so silently replaying it in the
driver would be an incompatibility rather than sleep recovery.  The accepted
post-wake test therefore created a fresh AP through the same public CoreWLAN
path.  SAE group 19 with required PMF/BIP completed again, followed by a
60-second four-stream AP-to-station run with 63.9 MiB received.  High-water
rejections again occurred at `queued=225`; there was no `queued=255`, firmware
fatal, watchdog, or lower reset.  Both directions then passed 20/20 ICMP.

Static contract coverage is provided by
`scripts/test_iwn_iwm_iwx_ap_tx_watermark_contract.sh`.
