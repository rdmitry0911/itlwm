# IWX open-AP RX new-TX runtime boundary (Tahoe 25C56)

Date: 2026-08-05

## Reference boundary

The AX210/AX211 IWX backend always uses Intel's new TX API.  The primary
iwlwifi donor makes the same distinction in
`__iwl_mvm_mac_sta_notify()`: legacy devices send a station sleep/awake
update, while `iwl_mvm_has_new_tx_api(mvm)` returns without issuing that
command because firmware owns the transition.

Primary donor:

- <https://github.com/torvalds/linux/blob/v6.0/drivers/net/wireless/intel/iwlwifi/mvm/mac80211.c>

## Observed failure

The WIP93 open AP already completed firmware resource creation, beaconing,
client authentication/association, dynamic station creation, TXQ allocation,
and guest-to-client TX.  The IWX RX ring also received and classified client
frames.  They were discarded after classification with:

```text
IWX open AP RX action=4 error=35
IWX open AP RX action=5 error=35
```

Actions 4 and 5 are AP data and peer power-state observations.  Error 35 is
`EWOULDBLOCK`.  `iwx_ap_handle_rx()` was issuing a synchronous
`ADD_STA` wake modification from the firmware notification/RX path before
enqueuing the already accepted data frame.  The command could not wait on that
workloop, so the accepted frame never reached the AP Skywalk interface.

## Implemented boundary

For IWX only, the received PM bit is now the committed peer sleep/awake state.
The RX handler no longer issues an `ADD_STA` wake modification.  The
`sleep_tx_count` station modification remains reserved for an actual
PS-Poll service period.  IWM retains its legacy station-notify behavior.

`scripts/test_iwm_iwx_ap_power_save_contract.sh` fences this split so the
blocking wake command cannot return to the IWX RX handler.

## Candidate and admission

The Tahoe AP build completed with all 1074 undefined symbols resolved against
the 25C56 BootKC and no `_thread_call_cancel_wait` dependency.

- candidate SHA-256:
  `18a456ed74f7e970cee8f124566bc3ce9c4a8fb66402c90356c0a6a45d0a053c`
- candidate/loaded UUID:
  `243529E6-ED29-386B-92E1-47A98BFC37C9`
- private AuxKC admission: PASS
- private and installed AuxKC member count: exactly 5
- guest-only reboot: PASS
- host QEMU process and base disk: unchanged; the disk remained
  `snapshot=on`

An initial canonical rebuild reached a full guest filesystem and aborted.
The transactional helper restored the previous bundle and AuxKC byte-for-byte.
After removing only rebuildable cache and failed-transaction duplicates, the
same admitted candidate activated successfully.

## On-air result

The physical Intel 6235 client joined the guest AX211 AP
`AIAM-IWX-OPEN-WIP94` on channel 6.

- BSSID: `86:e4:ba:20:ef:f9`
- authentication/association: PASS
- guest `ap1` RX: increased from 0 packets to 59 packets / 9772 bytes
- ARP: 3/3 replies
- host-to-guest ICMP: 5/5 replies
- guest-to-host ICMP: 5/5 replies
- DHCP DISCOVER/OFFER/REQUEST/ACK: PASS
- assigned client address: `192.168.88.100/24`, 300-second lease
- HTTP over the AP path: HTTP 200
- post-candidate `action=4/5 error=35`: 0
- kernel panic/fatal PCI marker: none

This closes open-AP RX, DHCP, and bidirectional IP traffic for the loaded IWX
candidate.  It does not yet claim AP recovery across uplink loss or sleep, nor
WPA2/WPA3 HostAP traffic.
