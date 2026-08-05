# IWX WPA2 AP local authenticator, protected RX, and retry runtime

## Scope

This layer completes the common IWM/IWX driver-resident WPA2-PSK AP
authenticator and closes the two failures found in physical AX211/IWX runtime:

1. firmware reports the protected RX trailer length in
   `mac_flags1.MIC_CRC_LEN`; AX211 can report zero because firmware has already
   stripped the CCMP MIC;
2. the first EAPOL M1 transmission can exhaust its hardware attempts with
   status `0x83`, so an authenticator must retain the transaction and perform
   bounded EAPOL-Key retransmission.

The layer also shares the resulting state machine with WPA3/SAE.  SAE remains
a separate runtime gate; this report proves WPA2 only.

## Reference decisions

- Linux iwlwifi's RX API defines `MIC_CRC_LEN` as the number of trailing bytes
  retained in the delivered MPDU, and `rxmq.c` subtracts that reported value.
  The AP decapsulator therefore receives the descriptor value instead of
  assuming an eight-byte MIC.
- The in-tree OpenBSD authenticator already owns per-station EAPOL timeout and
  retry teardown in `ieee80211_pae_output.c`.
- hostapd's reference state machine uses a 100 ms first EAPOL-Key timeout,
  1000 ms subsequent timeouts, four pairwise attempts, a fresh replay counter
  for each authenticator transmission, and deauthentication after exhaustion.
  The local runtime mirrors those values and retains a four-entry replay
  acceptance window.

The timer callback never transmits or submits firmware commands.  It publishes
the timed-out handshake state to the existing serialized AP client task.  A
valid M2/M4 cancels the timer before the process-context GTK/PTK command and
M3/authorization edge.

## Candidate

- source parent: `493f0ca76ff74ca3b53a745aa08f158caa9ac810` plus this layer
- guest: disposable Tahoe 25C56 QEMU, physical AX211/IWX passthrough
- QEMU PID: `19869` (unchanged through activation, reboot, and S3)
- Mach-O SHA-256:
  `4390852ea2dc31bcd8b8671ac238db8c530a6d34ff0c830b923cd4ee714ede04`
- Mach-O UUID: `2E3FEEC7-A73F-39E5-9C11-63DCCE3C5173`
- SSID: `AIAM-IWX-WPA2-WIP106`, channel 6
- BSSID: `86:e4:ba:20:ef:f9`
- client: physical Intel 6235, `c8:f7:33:f4:97:4c`

The Tahoe build succeeded, imported no `_thread_call_cancel_wait`, and all
1074 undefined symbols resolved against the guest BootKC.  Private AuxKC
admission preserved the exact five-member set.  Transactional activation and
the next guest boot loaded the UUID above; the canonical binary SHA matched.

## Initial association result

The original failure reproduced exactly:

```text
IWX AP WPA2 EAPOL M1 queue=0 replay=1
IWX AP TX failure ... status=0x83 ... failure_frame=14
```

WIP106 recovered without interface on/off or an external reconnect:

```text
IWX AP WPA2 EAPOL M1 retry attempt=2 queue=0 replay=2
IWX AP WPA2 M2 accepted M3=0 replay=3
IWX AP WPA2 4-way complete authorized=1
```

The same connection then completed a real DHCP Discover/Offer/Request/ACK,
received `192.168.88.100/24`, passed ICMP 5/5 in both directions, and returned
HTTP 200 in both directions.

## Reconnect and sleep result

Three consecutive supplicant disconnect/reconnect cycles were run without
turning the host interface or guest radio off.  Every cycle reached
`COMPLETED`, reacquired `192.168.88.100/24`, passed ICMP 3/3, and returned
HTTP 200.  Each cycle reproduced the first-M1 `0x83` terminal and recovered on
attempt 2 inside the driver.

With the client connected, `pmset sleepnow` reached:

```text
PMRD: System Sleep
IOCPUSleepKernel enter
ACPI SLEEP
```

The exact QEMU monitor reported `paused (suspended)`.  After only
`system_wakeup`, the serial trace showed ACPI S3 wake, bounded primary-scan
handoff, complete AP PHY/beacon/MAC/binding/station replay, APSTA datapath
enable, a fresh WPA2 four-way completion, and no driver panic or firmware
fatal.  DHCP was reacquired and host-to-guest ICMP passed 5/5 with HTTP 200.
A further post-wake disconnect/reconnect also reached `COMPLETED`, DHCP,
ICMP 5/5, and HTTP 200.

As in earlier runs of this disposable image, the unrelated virtio management
SSH service did not return a banner after S3, although the wireless AP path
was fully operational.  An exact reset of this QEMU alone restored management.

## Contracts

The complete AP-focused static suite passed, including:

- `scripts/test_iwm_iwx_wpa2_ap_runtime_contract.sh`
- `scripts/test_iwm_iwx_wpa3_sae_ap_runtime_contract.sh`
- `scripts/test_iwm_iwx_ap_reassociation_contract.sh`
- `scripts/test_iwm_iwx_ap_sleep_rearm_contract.sh`
- `scripts/test_tahoe_apsta_sleep_wake_replay_order_contract.sh`
- `scripts/test_tahoe_apsta_wpa2_hostap_contract.sh`
- all IWN/IWM/IWX AP RX/TX A-MPDU, reset replay, rate, WMM, power-save,
  multi-client, hidden-AP, and CSA contracts

`git diff --check` also passed.

## Remaining runtime surface

This result closes physical WPA2 AP association/data/reconnect/S3 for the
tested IWX path.  It does not claim physical WPA3/SAE/PMF, open/WPA2/WPA3 UI
matrix completion, simultaneous STA+AP, ad-hoc, or multi-AP STA roam behavior.
