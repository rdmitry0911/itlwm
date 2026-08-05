# IWX APSTA bounded foreground-scan handoff runtime evidence (25C56, 2026-08-05)

## User-visible layer

An AX211 Tahoe guest could remain indefinitely in the primary STA's generic
foreground `SCAN` state while a valid HostAP request was retained above it.
The AP watchdog retried once per second, but every IWX lower start returned
`EBUSY`; no BSS was emitted and an external station had nothing to join.

This layer gives an untagged primary foreground scan a bounded interval to
finish.  At the timeout, IWM/IWX yield only that exact scan owner to AP.  A
firmware-active scan is retired only by its native scan-complete terminal; a
net80211 `SCAN` state with no firmware scan lease receives the equivalent
one-shot quiescent terminal.  Tagged WCL/background scans remain fail-closed.
After the AP transaction, the retained credential generation or the generic
foreground census is resumed.

## Reference boundary

The implementation follows two existing primary references:

- Tahoe's recovered `AppleBCMWLANIO80211APSTAInterface::setHostApModeInternal`
  disables the foreground/background scan private-MAC policy before it
  configures the independent FullMAC HostAP context.
- Linux MVM serializes AP start and scan retirement under the MVM mutex and
  does not treat command submission as a scan terminal.

The local Intel adaptation therefore keeps the policy in the common APSTA
owner and the physical lease/terminal ownership in IWM/IWX.  A C bridge is
used instead of a new virtual method so the early-attach HAL vtable ABI does
not move.

## Exact candidate

The clean WIP98 candidate installed transactionally in the disposable Tahoe
26.2 guest had:

- kernel collection build: `25C56`;
- kext SHA-256:
  `96f0014da4fdb86d263ac8679cb60011ba0a8021ef29a067af0a08066d7df8d1`;
- Mach-O UUID: `4436CB52-32AA-3F58-A7B7-852E75913DDF`;
- boot session UUID: `C9E096AE-6312-46E6-AEA5-9DD9729D132B`;
- source identity: `df5f6a8ac5e5`;
- all 1074 undefined symbols resolved against the exact BootKC;
- no `_thread_call_cancel_wait` dependency.

The owned QEMU process remained PID `19869` throughout activation and the
runtime gate.  The base disk remained read-only through `snapshot=on`.

## Physical runtime result

The cold HostAP request was intentionally admitted while the primary IWX
owner remained in an untagged generic foreground scan.  Serial evidence
showed the complete ownership transfer:

```text
APSTA radio-reset late primary foreground scan owns radio retained_recovery=0
IWX AP scan handoff retired prior retryable start terminal
IWX quiescent foreground scan yielded to AP generation=0 generic=1 yielded=1
APSTA bounded primary foreground scan handoff wait_ticks=30 result=0x0
IWX AP lower start queued outside upper command gate
IWX AP start complete multicast_queue=1 broadcast_queue=2
IWX resuming foreground scan after AP handoff generation=0 generic=1
IWX AP lower start worker complete error=0 result=0x0
APSTA datapath enabled link=1 RX=1 TX=1 TXC=1
```

The guest AX211 then emitted the real open BSS
`AIAM-IWX-OPEN-WIP98`, BSSID `86:e4:ba:20:ef:f9`, on channel 6.  A physical
Intel 6235 on the host observed and joined that BSS with
`wpa_state=COMPLETED` and no pairwise/group cipher.

The full data-path gate passed:

- DHCP completed DISCOVER/OFFER/REQUEST/ACK and assigned
  `192.168.88.100/24` from guest `192.168.88.1`;
- host-to-guest ICMP passed 5/5;
- guest-to-host ICMP passed 5/5;
- host-to-guest HTTP returned 200 and transferred 3078 bytes;
- guest-to-host HTTP returned 200 and transferred 3078 bytes;
- no panic or firmware-fatal marker occurred.

## Residual lifecycle layer

The first controlled `stopHostAPMode` test exposed the next independent
mismatch.  Tahoe issued `trigger=stop_apsta` and the driver disabled the APSTA
datapath, but the asynchronous IWX lower teardown did not reach a terminal:
the AX211 continued beaconing while ICMP/HTTP were correctly fenced.  This is
not part of the scan-to-AP admission fix recorded here.  It remains a
fail-closed stop/restart lifecycle task and must be fixed and physically
verified before sleep/wake and protected-AP publication.

## Contract verification

- `scripts/test_iwm_iwx_apsta_bounded_recovery_handoff_contract.sh`
- `scripts/test_iwn_iwm_iwx_apsta_reset_recovery_scan_order_contract.sh`
- `scripts/test_iwn_iwm_iwx_ap_unexpected_reset_replay_contract.sh`
- `scripts/test_iwm_iwx_ap_sleep_rearm_contract.sh`
- `scripts/test_iwm_iwx_ap_firmware_resource_contract.sh`
- `scripts/test_tahoe_iwx_hostap_lower_epoch_retry_contract.sh`
- `scripts/test_tahoe_standard_scan_lifecycle_contract.sh`
