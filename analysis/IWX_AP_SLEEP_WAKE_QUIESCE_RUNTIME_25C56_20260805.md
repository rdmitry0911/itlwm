# IWX AP sleep/wake quiesce runtime (Tahoe 25C56, 2026-08-05)

## Scope

This layer removes the power-off deadlock between asynchronous IWX AP
teardown and the immediately following generic device reset.  It preserves
the separate, authoritative asynchronous worker used by an explicit user
HostAP NULL command.

AppleBCMWLAN's recovered `hostAPPowerOff` makes a no-client HostAP profile
terminal, while retaining an AP with a live client in its power-save state.
Intel firmware cannot retain the lower context through device reset.  The
upper owner therefore preserves the same public decision, but delegates the
physical no-client erasure to the imminent generic reset instead of starting
a competing q0 transaction from the power-command workloop.

## Candidate

- Parent source commit at build time:
  `f8e0a41062d1207c5587bb61579e7b5c89732fdf`
- Embedded source identity: `60451de463c4`
- Frozen signed Mach-O SHA-256:
  `87bcaf7e4abb3ee05838596be2583908d792176141a3f9524470dabfe24fc174`
- Mach-O UUID: `DF962399-90EA-32F0-A8E9-7E429C43F841`
- Guest: disposable QEMU Tahoe 25C56, physical AX211/IWX passthrough
- Initial loaded boot session:
  `E53BFE93-29A4-49B0-B4A9-E58C5F2E7706`

Private AuxKC admission passed with the exact five-member collection.  All
1074 undefined symbols resolved against the 25C56 BootKC, and the candidate
did not import `_thread_call_cancel_wait`.

## Pre-fix failure

With a physical client on the open AP, `pmset sleepnow` disabled the role-7
datapath, queued an IWX lower stop, and then stalled indefinitely:

```text
IWX AP lower stop queued outside upper command gate
PM waiting on pmDriverCallout(0x2) to AirportItlwm (2000 ms)
```

The guest never reached `ACPI SLEEP`.  `ItlIwx::disable()` was submitting an
AP firmware worker and then entering `DVACT_QUIESCE` from the workloop needed
to deliver that worker's command completions.

## Runtime result

WIP101 ran open AP `AIAM-IWX-OPEN-WIP101` on channel 6 with BSSID
`86:e4:ba:20:ef:f9`:

1. The physical Intel 6235 associated and obtained
   `192.168.88.100/24` from guest `192.168.88.1`.
2. ICMP passed 5/5 in both directions before sleep.
3. `pmset sleepnow` disabled the APSTA datapath and completed the system PM
   transition.  The serial log reached:

   ```text
   Time ... Message PMRD: System Sleep
   IOCPUSleepKernel enter
   ACPI SLEEP
   ```

4. The QEMU monitor reported `VM status: paused (suspended)`.  There was no
   `PM waiting on AirportItlwm` message.
5. After the exact monitor `system_wakeup`, the driver completed the bounded
   primary-scan handoff, recreated PHY/beacon/MAC/binding/multicast/broadcast
   AP resources, enabled the role-7 datapath, and admitted the physical
   client station again.
6. The host supplicant returned to `COMPLETED` on the same BSS, retained or
   reacquired `192.168.88.100/24`, and ICMP to the guest passed 5/5 after
   wake.  Each echo reply traverses guest AP RX and TX, proving both data
   directions after the firmware epoch was rebuilt.

The retained replay also proves that the connected lower station was counted
for the sleep decision even though no separate APSTA census diagnostic is
currently printed.

The unrelated virtio management SSH service did not return a banner after
this S3 cycle, although QEMU was running and AP traffic had fully recovered.
An exact `system_reset` of only this disposable guest restored management;
QEMU PID 19869 remained unchanged and the WIP101 canonical candidate remained
installed and loaded in the next boot.  There was no AirportItlwm panic or
IWX firmware fatal.

## Contracts

- `scripts/test_iwm_iwx_ap_sleep_rearm_contract.sh`
- `scripts/test_tahoe_apsta_sleep_wake_replay_order_contract.sh`
- `scripts/test_tahoe_apsta_wpa2_hostap_contract.sh`
- `scripts/test_iwx_ap_authoritative_stop_terminal_contract.sh`
- `scripts/test_tahoe_iwx_hostap_lower_epoch_retry_contract.sh`
- `scripts/test_iwm_iwx_ap_firmware_resource_contract.sh`
- `scripts/test_iwn_iwm_iwx_ap_unexpected_reset_replay_contract.sh`
- `scripts/test_iwm_iwx_apsta_bounded_recovery_handoff_contract.sh`

All passed together with `git diff --check` before candidate activation.
