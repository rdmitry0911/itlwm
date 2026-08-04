# Tahoe WCL power-terminal runtime evidence (25C56, 2026-08-04)

## Problem and reference contract

Tahoe could leave `WCLNetManager` in `WAITING_FOR_IP` when Intel radio-off
arrived without a public `DISASSOCIATE` request or a lower firmware link
event.  A later radio-on replay then panicked in
`WCLNetManager::ipStatusInWaitForIp`.

The 26.3 x86_64 KDK reference has a separate
`AppleBCMWLANNetAdapter::sendInternalLinkDownInd()` terminal.  It posts an
asynchronous selector `0xd8` payload of 16 bytes with:

- zero BSSID and link-state bytes;
- infrastructure interface type `1` at byte 7;
- literal reason `9` at offset 8.

The reference's direct owner is `AppleBCMWLANCore::setDISASSOCIATE`.  Its
`powerOff(bool)` path publishes driver unavailability and does not manufacture
a join-abort.  Runtime tracing showed that Tahoe's radio toggle reaches the
Intel port as `IOC_POWER` alone, while IWN disable supplies no later firmware
link event.  The Intel power-off adapter therefore emits the exact internal
terminal before `DRIVER_UNAVAILABLE`; it does not synthesize an authoritative
RUN-BSSID event or a `JOIN_ABORT`.

## Tested artifact

- macOS 26.2 build 25C56;
- physical Intel Centrino Advanced-N 6235 passed through to QEMU;
- pure WPA3-SAE/required-PMF test network, DHCP gateway `172.16.66.1`;
- AirportItlwm UUID `C656B132-D440-373D-8255-ABB7F5AF4451`;
- Mach-O SHA-256
  `7dcfbaef4e6511df58a4ab945ca48a0579c202ea48206eaf291c1d4b8b461268`;
- source base `b189352a` plus the power-terminal patch recorded by this
  document.

The disposable guest was `aiam-tahoe-disposable-pair-wip71`.  The baseline
disk remained unchanged; recovery used a direct copy of the last known-good
overlay.

## Failed-IP and repeated radio-cycle gate

The guest first reached an associated WPA3 state with no IPv4 lease, matching
the previously fatal WCL state.  Radio OFF/ON completed without a reboot and
recovered WPA3 plus DHCP address `172.16.66.214`.

Two additional OFF/ON cycles recovered in approximately 26 and 20 seconds.
After each cycle the boot epoch remained
`1785832819.171844`, HTTPS to `172.16.66.1` returned HTTP 200 with 664 bytes,
and a 1400-byte ICMP run completed without loss.

## Real S3 and post-wake radio cycle

The serial offset was fixed before `pmset sleepnow`.  New output then recorded:

```
PMRD: System Sleep
ACPI SLEEP
acpi_sleep_kernel hib=0, cpu=0
AppleACPIPlatformPower Wake reason: power-button (User)
PMRD: System Wake
```

Only the owned QEMU monitor socket received `system_wakeup`.  On wake, IWN
performed a fresh cached-candidate SAE join and recorded:

```
wcl_assoc CACHED_CANDIDATE_DIRECT_JOIN
wcl_assoc VALIDATED_COMPLETION captured=1 result=0x00000000 armed=1 published=1
iwn_sae_roam ACTIVE_ESS_CREDENTIAL
```

The QEMU VirtIO host-forward did not recover immediately, so the remaining
checks deliberately used SSH over the physical Wi-Fi address.  This also
proved the real data path independently of the management NIC:

- same boot epoch and loaded kext UUID;
- DHCP address `172.16.66.214` and Wi-Fi power on;
- 10/10 1400-byte ICMP packets to the AP;
- HTTP and HTTPS both returned 200 with 664 bytes.

A further post-wake OFF/ON cycle made the Wi-Fi SSH endpoint disappear and
recover in about six seconds.  The same boot epoch and kext remained loaded,
the DHCP address returned, another 10/10 1400-byte ICMP run passed, and HTTPS
again returned 200 with 664 bytes.

No `Debugger called`, kernel panic, WCL invalid-state report, firmware fatal,
command timeout, or driver watchdog appeared in the tested interval.  The
serial log contains the known virtual-SMC `SMCWDT::setWatchdogTimer` platform
warning; it is unrelated to the wireless driver and did not reset the guest.

## Regression gates

The focused contracts pass:

- `test_tahoe_wcl_disassociate_link_down_contract.sh`;
- `test_tahoe_wcl_power_off_link_down_contract.sh`;
- `test_tahoe_wcl_link_down_reconnect_contract.sh`.

The full historical quarantine runner still stops earlier at the pre-existing
IWN physical-scan lifecycle census assertion.  This result does not claim that
unrelated static census gate as fixed.
