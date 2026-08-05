# IWX AX211 init, TX completion and association-session runtime evidence

Date: 2026-08-05
Target: macOS Tahoe 25C56, `AirportItlwm` 2.4.0, passed-through Intel AX211

## Scope

This layer corrects four IWX firmware-facing contracts which are prerequisites
for ordinary station operation on AX210-family devices:

- fail-closed PNVM selection and predicate-checked PNVM/init completion;
- init-epoch validity after the task-admission gate opens;
- non-inclusive SCD SSN reclamation for single-frame TX completions;
- association session-protection duration and add/remove ownership.

The runtime candidate was built from byte-identical copies of
`ItlIwx.cpp`, `ItlIwx.hpp`, and `if_iwxvar.h` in this worktree. Its loaded and
installed identity was:

```text
UUID: E6E5B6B1-06E0-3D92-8A12-948A58A965D0
AirportItlwm executable SHA-256:
4fc357f7942792af1c1f5604e17c06aaaa5e50a91db1ce44756b41302e0f23b6
```

## Reference contracts

OpenBSD-current `if_iwx.c` provides the direct device-family reference:

- `iwx_load_pnvm()` waits until `IWX_PNVM_COMPLETE` is present, rather than
  treating any wakeup as completion;
- `iwx_run_init_mvm_ucode()` similarly loops on `IWX_INIT_COMPLETE`;
- `iwx_rx_tx_cmd()` defines the SCD SSN as the first descriptor not completed
  and frees descriptors before it, non-inclusive;
- `iwx_schedule_session_protection()` places an already-TU duration directly
  in `duration_tu`, records `IWX_FLAG_TE_ACTIVE` after a successful ADD, and
  removes only a live event;
- association protection uses nine beacon intervals, or 900 TU when no beacon
  interval is available.

Primary source:
<https://github.com/openbsd/src/blob/master/sys/dev/pci/if_iwx.c>

Linux iwlwifi independently states the same TX completion rule in
`iwl_mvm_rx_tx_cmd_single()`: reclaim up to the SCD SSN non-inclusive, apply the
reported status to the first reclaimed frame, and mark subsequently reclaimed
frames acknowledged. It also treats `TX_STATUS_DIRECT_DONE` as successful.

Primary source:
<https://github.com/torvalds/linux/blob/master/drivers/net/wireless/intel/iwlwifi/mvm/tx.c>

The Linux session-protection implementation also serializes `conf_id` and
`duration_tu` as little-endian firmware fields.

Primary source:
<https://github.com/torvalds/linux/blob/master/drivers/net/wireless/intel/iwlwifi/mvm/time-event.c>

## Corrected local behaviour

The local port previously derived an invalid AX211 PNVM filename, allowed a
required PNVM failure to fall through, and used one shared wakeup without
checking the completion predicate. The corrected path strips only the firmware
API suffix, fails closed when a required PNVM cannot be loaded, owns a distinct
PNVM completion bit/status, and loops on the expected completion bits.

The init-epoch predicate previously required the task gate to remain closed.
Opening the gate for the first SCAN therefore invalidated the same successful
init epoch. The predicate now keeps the shutdown, detach, generation, init-owner
and stop-owner fences without contradicting the deliberate closed-to-open
admission transition.

The old single-TX completion path looked at `ring->data[SSN]`, even though SSN
is the first uncompleted descriptor, and then reclaimed the preceding range in
a separate pass. The corrected path walks `[tail, SSN)`, publishes any SAE TX
terminal result before reclaiming its exact descriptor, treats DIRECT_DONE as
success, preserves the AP-frame completion indication, and cannot reclaim the
same range twice.

The old session-protection port multiplied an already-TU duration by 1024,
failed to record a successful ADD, sent REMOVE without a live-event guard, and
left the new command active from the RUN MAC-context task. The corrected path
matches the reference duration, endian, active-flag, guarded-remove and
RUN-cancellation lifecycle.

## Runtime results

The exact candidate booted and remained reachable with the AX211 attached.
There has been no `NMI_INTERRUPT_UMAC_FATAL`, `ADVANCED_SYSASSERT`, firmware
fatal error, or kernel panic after the candidate's last boot boundary.

### WPA3/SAE and open station paths

The candidate completed real WPA3/SAE plus PMF against `LabAP`, reached
AUTH -> ASSOC -> RUN, obtained DHCP, passed traffic, survived Wi-Fi off/on and
real ACPI S3/wake, and selected another same-SSID BSS after wake. A controlled
open AP on channel 6 also completed join, DHCP and bidirectional traffic.

The pre-fix open-network run had produced `NMI_INTERRUPT_UMAC_FATAL` with UMAC
`ADVANCED_SYSASSERT`; the last firmware command was MAC_CONF_GROUP command
`0x05`, `SESSION_PROTECTION_CMD`. The corrected candidate did not reproduce
that assert.

### WPA2 Personal UI join

A controlled hostapd AP was used:

```text
SSID: AIAM-IWX-WPA2-0805
channel: 2g6/20
security: WPA2-PSK / CCMP
host gateway: 192.168.87.1/24
```

After logging a real console user into the disposable guest, Tahoe System
Settings displayed the BSS and accepted its password. Hostapd recorded:

```text
authenticated
associated
AP-STA-CONNECTED
WPA: pairwise key handshake completed (RSN)
EAPOL-4WAY-HS-COMPLETED
```

The guest obtained `192.168.87.226` from `192.168.87.1`; `wdutil` reported
`WPA2 Personal` on `2g6/20`. Guest-to-host and host-to-guest ICMP both passed
10/10. An HTTP transfer of `README.md` produced the same SHA-256 on both ends:

```text
f04c97524f66e990ee70cbcc7b06feb464b3abca29a8840b707c2afa235cf3a4
```

This is a full manual UI-join result, not a scan-only or firmware-only claim.

## Remaining reconnect gap

The saved WPA2 profile exposed the next independent layer. After toggling Wi-Fi
off and on in System Settings, the radio repeatedly authenticated, associated,
and completed the WPA2 four-way handshake, but Tahoe did not retain the link as
committed:

- System Settings returned to `Not connected`;
- SSID/BSSID/security getters returned `None` and
  `AirportItlwm::getBSSIDData()` returned `0xe0822403`;
- the prior DHCP address was removed and no `192.168.87.0/24` route was
  installed;
- hostapd observed repeated connect/disconnect cycles.

Therefore this layer proves the corrected firmware contracts and full manual
open/WPA2/WPA3 station joins, but it does **not** claim saved-profile reconnect
closure. The next high-frequency user layer is the reference-derived RUN/link
identity publication and network-configuration commit across autojoin, radio
toggle, profile switch, and wake.
