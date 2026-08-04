# IWN AP DVM rate-feedback runtime evidence (25C56, 2026-08-04)

## Scope

This layer replaces the fixed initial IWN HostAP MCS with per-client rate
selection driven by real firmware TX and compressed-BA results.  It extends
the already-correct DVM retry ladder; it does not replace firmware retries or
treat a final retry exhaustion as a Block Ack lifetime transition.

The physically tested Tahoe 25C56 candidate has Mach-O UUID
`E103EBD1-8CF0-3B99-969B-7FC694776839`, binary SHA-256
`06331ec87244c5b85f919f29b24c17e9fb788a8eb97b25a6e0f37d5c30b6e386`,
and embedded source identity `e36f8c09-dvm-feedback-wip58c`.

## Reference contract

Intel DVM was compared at Linux commit
`848acc8ffe1b7cd5f1bf427b93069becfebc2c9d`, principally
`drivers/net/wireless/intel/iwlwifi/dvm/rs.c` and `tx.c`:

- each rate owns a 62-result sliding window;
- average throughput becomes usable after six failures or eight successes;
- the decrease, increase, and high-success thresholds are 15%, 50%, and 85%;
- expected throughput comes from the exact HT20 Normal, SGI, AGG, and
  AGG+SGI tables;
- aggregate feedback pairs the TX response's rate with the subsequent
  compressed BA `txed/txed_2_done` result;
- a non-aggregate result walks `failure_frame + 1` entries in the Link
  Quality retry table;
- a completion whose initial rate no longer matches table entry zero is
  ignored, and more than 15 consecutive mismatches republishes the LQ table;
- a subframe which first failed in an A-MPDU and later completes as a single
  frame contributes a failed sample to its descriptor-retained aggregate
  rate as well as a result for the final retry-table rate.

The Tahoe reference corpus on `10.7.6.112`, including the recovered
AppleBCMWLAN AP/STA lifecycle, was checked read-only.  It owns HostAP role,
station, security, and power transitions but does not implement Intel firmware
rate selection.  The correct implementation boundary is therefore the IWN
DVM HAL, not a synthetic CoreWLAN policy.

## Implementation contract

Each AP client now owns independent rate state:

- negotiated HT MCS mask and stream family constrain every candidate;
- a new lifetime begins at the lowest negotiated HT MCS and probes adjacent
  rates only after the DVM sample threshold;
- rate windows, selected MCS, aggregate mode, mismatch count, and command
  generation are reset on authentication/association replacement;
- only one Link Quality command may be outstanding, and old completions cannot
  alter a newer generation;
- TX response rate/rflags/generation are retained per RA/TID until the matching
  compressed BA arrives;
- aggregate descriptors retain their original MCS/rflags/generation.  AP
  descriptors have no synthetic STA node, so the aggregate completion path now
  explicitly admits AP-owned descriptors instead of silently skipping them;
- a later single-frame fallback result charges one failed sample to that
  descriptor-fenced aggregate rate, matching the existing IWN STA/DVM path;
- the selected HT rate remains repeated three times in the firmware retry
  table, followed by three lower-HT attempts and the DVM legacy descent.

IWM and IWX use their separate MVM rate-control implementation and therefore
received no speculative IWN-shaped mutation.  Their shared AP aggregation and
watermark contracts remain covered by the cross-family regression tests.

## Physical runtime evidence

The device under test is an Intel Centrino Advanced-N 6235 passed through to
Tahoe.  A Linux AX211 on host interface `sta0` is the on-air client.  All APs
used channel 9, BSSID `ce:f7:33:f4:97:4b`, and the isolated
`172.31.58.0/24` lab path.

### WPA2

The AX211 completed WPA2-PSK/CCMP and bidirectional ICMP passed 10/10.  A
30-second, four-stream AP-to-STA run delivered 48.06 Mbit/s to the receiver.
The rate owner climbed through MCS8..14, used descriptor-retained failures to
fall from MCS14 to MCS13 after a 1/8 window, then settled at MCS12.

Thirteen physical MPDUs exhausted the firmware short-retry limit among about
128 thousand output frames.  Each was reclaimed and followed by a successful
compressed BAR while the BA session and TCP transfer remained live.  There
was no stale SSN/ring completion, watchdog, reset, firmware fatal, or panic.

### WPA3/SAE with required PMF

The same candidate completed SAE group 19 with `key_mgmt=SAE`, `pmf=2`, BIP,
and CCMP.  Bidirectional ICMP passed 10/10.  The corresponding 30-second P4
AP-to-STA run delivered 49.24 Mbit/s.  Ten physical short-limit completions
were handled by BAR without ending BA ownership or traffic; no stale
completion, watchdog, reset, firmware fatal, or panic occurred.

### Real S3 and post-wake data path

Before sleep a wall-clock guest owner was armed so validation did not depend
on the VirtIO management NIC.  The exact power sequence included:

```text
AP stop requested
AP PAN context stopped; STA context preserved
PMRD: System Sleep
ACPI SLEEP
acpi_sleep_kernel
ACPI S3 WAKE
system wake events: power-button
AP PAN context running after DVM RXON/beacon/EDCA transition
IWN AP DVM rate initialized id=2 MCS8 generation=1
```

After the owned QEMU monitor received `system_wakeup`, the guest created the
fresh open AP `AIAM-DVM-FB-W58C-WAKE`.  The AX211 associated, host-to-guest
and guest-to-host ICMP each passed 20/20, and SSH itself worked over the Wi-Fi
path.  A further 20-second P4 AP-to-STA run delivered 63.25 Mbit/s while the
new rate lifetime climbed and descended normally.  The post-wake interval had
no short-limit completion, stale BA/ring result, IWN watchdog, reset, firmware
fatal, panic, or new host PCIe AER event.

## Regression coverage

The focused contract is
`scripts/test_iwn_ap_dvm_rate_feedback_contract.sh`.  It is run together with:

- `scripts/test_iwn_ap_dvm_rate_ladder_contract.sh`;
- `scripts/test_iwn_iwm_iwx_ap_tx_ampdu_contract.sh`;
- `scripts/test_iwn_iwm_iwx_ap_tx_watermark_contract.sh`;
- `scripts/test_tahoe_apsta_tx_backpressure_contract.sh`.

## Result and remaining boundary

IWN HostAP now selects its HT rate from real, generation-fenced TX/BA feedback
instead of pinning a synthetic initial MCS.  Open, WPA2, WPA3/SAE+PMF,
bidirectional traffic, sustained load, and real S3 recovery are physically
proven on 6235.

This result does not claim dynamic legacy-only rate selection, DVM's full
SISO/MIMO modulation-table search, simultaneous AP+STA reconnect/roam, or
physical IWM/IWX parity.  Those remain separate functional layers.
