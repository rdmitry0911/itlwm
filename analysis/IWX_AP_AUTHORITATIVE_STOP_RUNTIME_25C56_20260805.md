# IWX HostAP authoritative stop runtime (Tahoe 25C56, 2026-08-05)

## Scope

This layer makes an accepted `setHostApModeInternal(NULL)` remain privately
owned until IWX has actually removed the lower AP context.  Queue admission is
not treated as a completed stop, and a replacement profile cannot overtake a
pending lower teardown.

The reference boundary is AppleBCMWLAN's recovered HostAP NULL path: it calls
the lower stop and returns that lower result.  The IWX adaptation submits the
firmware transaction on `sc_nswq`, because synchronous q0 commands cannot be
waited from AirportItlwm's upper command gate.

## Candidate

- Base commit: `f8e0a41062d1207c5587bb61579e7b5c89732fdf`
- Source identity: `60451de463c4`
- Frozen signed Mach-O SHA-256:
  `9e73aa94baed65004dcaa4884e05629b92b68a2d95fb2a9c6515b4948b789d78`
- Mach-O UUID: `8D26AF40-BA38-3BC7-AC65-3D50047D112D`
- Guest: disposable QEMU Tahoe 25C56, physical AX211/IWX passthrough
- Loaded boot session after recovery:
  `DDE53FC5-EBDC-4009-A16D-7C0544F5A530`

Private AuxKC admission passed with the exact five-member collection.  All
1074 undefined symbols resolved against the 25C56 BootKC, and the candidate
did not import `_thread_call_cancel_wait`.

## Failure isolated

The first retained-stop candidate disabled the upper datapath but left the BSS
on air indefinitely.  A guest kernel spindump symbolized the lower worker as:

```text
taskq_thread
iwx_ap_stop_task_dispatch
iwx_ap_stop_task
ItlIwx::iwx_stop_ap_mode
taskq_barrier
```

`iwx_ap_stop_task` itself runs on the single-threaded `sc_nswq`; calling
`taskq_barrier(sc_nswq)` from that worker therefore waited for the current
worker to finish.  Removing the self-barrier is safe because tasks submitted
before the stop have already retired on the serial queue, while
`iwx_del_task(ap_client_task)` removes a client task still queued behind it.

## Runtime result

Open AP `AIAM-IWX-OPEN-WIP100` on channel 6 completed the following physical
cycle with the host Intel 6235 client:

1. BSS visible and client associated.
2. DHCP lease `192.168.88.100/24` from guest `192.168.88.1`.
3. ICMP 5/5 in both directions and HTTP 200 in both directions.
4. HostAP NULL produced the ordered terminal sequence:

   ```text
   IWX AP lower stop queued outside upper command gate
   APSTA lower stop pending result=...
   IWX AP lower stop worker complete error=0
   APSTA lower stop reached terminal
   ```

5. The physical client disconnected and lost DHCP.
6. After BSS cache flush, three independent scans observed 33, 35, and 31
   unrelated BSS entries and zero copies of the stopped target BSS.
7. A second start/stop followed by an immediate replacement start preserved
   terminal-stop-before-new-start ordering.  The replacement AP associated,
   leased DHCP, and passed ICMP 5/5.

## Contracts

- `scripts/test_iwx_ap_authoritative_stop_terminal_contract.sh`
- `scripts/test_tahoe_iwx_hostap_lower_epoch_retry_contract.sh`
- `scripts/test_iwm_iwx_ap_firmware_resource_contract.sh`
- `scripts/test_iwm_iwx_ap_sleep_rearm_contract.sh`
- `scripts/test_iwn_iwm_iwx_ap_unexpected_reset_replay_contract.sh`
- `scripts/test_iwm_iwx_apsta_bounded_recovery_handoff_contract.sh`

All passed together with `git diff --check` before the runtime candidate was
frozen.
