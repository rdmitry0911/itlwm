# IWN AP DVM retry ladder and runtime evidence (25C56, 2026-08-04)

## Scope

This layer replaces the synthetic IWN HostAP link-quality retry table with
the retry grouping and HT-to-legacy descent used by Intel DVM.  It does not
claim to implement DVM's sliding-window rate-selection feedback; that remains
the next rate-control layer.

The tested Tahoe 25C56 binary has Mach-O UUID
`60450DEE-7330-36A3-A832-80AF8886F95E` and SHA-256
`fde4cabbd08c889ea90f813ee35c33a73739d566ddd9caba3cc800eb65d9e1c3`.
Its embedded source identity is `ba8f1295-dvm-lq-wip57`.

## Reference contract

Intel's DVM implementation was read at Linux commit
`848acc8ffe1b7cd5f1bf427b93069becfebc2c9d`, file
`drivers/net/wireless/intel/iwlwifi/dvm/rs.c`:

- `rs_fill_link_cmd()` repeats the selected HT rate three times;
- it then repeats the next lower HT rate three times;
- only after those groups does it cross into the legacy ladder;
- `rs_ht_to_legacy` selects the matching legacy modulation and `prev_rs`
  walks the lower supported legacy rates.

The Tahoe reference corpus on `10.7.6.112`, including
`/home/dima/Projects/ghidra_output/`, was also checked read-only.  The Apple
AP/STA owner supplies the HostAP role, station and security lifecycle, but it
does not provide Intel firmware link-quality selection.  That responsibility
therefore remains in the IWN/DVM HAL rather than in the CoreWLAN bridge.

## Local change

For an HT client, `iwn_send_ap_client_link_quality()` now:

1. selects the highest negotiated MCS in the active SISO or MIMO family;
2. selects the next lower negotiated MCS in that family;
3. publishes three attempts for each HT group, retaining negotiated SGI;
4. places the MIMO delimiter after the HT groups;
5. maps the lower HT modulation into the exact DVM legacy ladder and fills
   any unused firmware retry slots with the lowest supported basic rate.

The old table published every MCS once and then filled as many as eight slots
with the minimum basic rate.  A single fade could consequently traverse the
entire MCS range and then spend most of the retry budget at 1 Mbit/s.

## Runtime evidence

The physical device is an Intel Centrino Advanced-N 6235 passed through to
the Tahoe guest.  The peer is a Linux AX211 on `sta0`.

### Open AP and load

`AIAM-DVM-LQ-WIP57` ran on channel 9 with BSSID
`ce:f7:33:f4:97:4b`.  The AX211 associated with `key_mgmt=NONE` and static
addresses `172.31.57.1/24` and `172.31.57.2/24` were used only for the lab
path.

- ICMP passed 20/20 in each direction;
- 60-second, four-stream AP-to-STA TCP measured 19.79 Mbit/s receiver rate;
- the immediately preceding same-fixture observation was 8.75 Mbit/s;
- 60-second, four-stream STA-to-AP TCP measured 12.66 Mbit/s receiver rate
  with two sender retransmissions;
- no new `IWN_TX_STATUS_FAIL_SHORT_LIMIT (0x82)`, TX-ring high-water,
  watchdog, reset, stale completion or firmware fatal was recorded.

### Real S3 and post-wake AP

Tahoe reached `PMRD: System Sleep` and QEMU ACPI S3.  A wall-clock owner was
armed before sleep so the test did not depend on the QEMU VirtIO management
NIC recovering after wake.  After exact QEMU `system_wakeup`:

- the owner created the fresh AP `AIAM-DVM-LQ-W57-WAKE` on channel 9;
- the AX211 scan observed it at 2452 MHz and -47 dBm;
- SAE/PMF-independent open association completed;
- post-wake ICMP passed 20/20 with zero loss;
- the post-wake interval added no `0x82`, ring/watchdog, stale-completion,
  panic, reset or firmware-fatal event.

This is a fresh AP after wake, matching Tahoe's explicit pre-S3
`HOST_AP_MODE(NULL)` policy rather than inventing driver-side AP retention.

### WPA2 and WPA3 regression

The same binary then completed:

- WPA2-PSK/CCMP association and 10/10 ICMP;
- WPA3-SAE group 19 with `pmf=2`, BIP and CCMP;
- WPA3 ICMP 10/10 in both directions;
- a clean rerun of 30-second, four-stream WPA3 AP-to-STA TCP at 7.03 Mbit/s
  receiver rate, with the VM still running and no `0x82`, watchdog, stale
  completion or firmware fatal.

The encrypted load reached the intentional AP backpressure watermark of 225
queued packets and rejected a bounded burst of 16 submissions.  TCP adapted
and completed.  That remaining pressure is evidence for the next
feedback-rate-control layer, not a reason to restore the old retry table.

## Infrastructure fault separated from driver evidence

The first WPA3 load attempt coincided with a host PCIe AER event at
2026-08-04 02:11:40.  Linux reported the Thunderbolt bridge `20:00.0` as
`Uncorrectable (Fatal), Inaccessible`; every device behind that bridge,
including the host xHCI and the VFIO 6235, returned PCI header `0x7f`.
QEMU consequently entered `paused (internal-error)` and required reset.

Host VFIO then failed both a 65-second FLR and a 65-second bus reset, proving
that the physical PCIe function was unavailable outside the guest.  Without
rebooting the host, the exact empty Thunderbolt subtree was removed and
rescanned.  The 6235 re-enumerated as `8086:088e rev 24`, was rebound to
`vfio-pci`, and the clean WPA3 load above completed without another AER.
The first run is therefore retained as infrastructure evidence and is not
misclassified as a driver crash.

## Result and remaining surface

The IWN AP firmware retry table now follows the Intel DVM grouping and legacy
descent, with real open/WPA2/WPA3, load and S3 recovery evidence.  The next
high-use discrepancy is DVM-style per-client rate adaptation from compressed
BA and TX completion statistics; the present layer deliberately does not
claim that feedback loop.
