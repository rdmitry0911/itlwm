# IWN AP multicast delivery to power-saving clients — 2026-09-09

## Reproduced runtime boundary

The disposable IWN/6235 guest loaded source `236f264e`, UUID
`779BA606-C79D-36AA-8C6A-27948BF9BAAE`. A role-7 SAE/required-PMF AP ran
alongside the primary WPA3 STA. The external AX211 client completed SAE
group 19 with PMF/BIP. Warm-neighbor bidirectional traffic had passed before
S3, and client-originated traffic passed after explicit reselection on wake.

A delayed AP-originated check instead failed 0/5 with an incomplete ARP
entry. Client-originated ARP allowed reverse traffic to resume. Deleting only
the isolated test client's neighbor entry reproduced the failure. Thus warm
traffic is not sufficient qualification of multicast or post-wake AP service.

The AP was normally stopped and restarted on the same loaded image, without
another S3 cycle. With client power save disabled, a fresh ARP lookup at
18:01:15 UTC and AP-to-client traffic passed 5/5. Re-enabling power save and
deleting the same entry at 18:02:02 reproduced 0/5 and incomplete ARP. Client
association remained complete. This is not restricted to the S3 replay path.

A bounded read-only TX observer recorded five successful submissions and
firmware completions on queue 5, broadcast station 14, frame control `0x4288`
(protected FromDS QoS data, no More Data), status `0x201` (success in the low
byte). A firmware submission/completion success alone does not establish
delivery to a sleeping client. No software GTK fallback is justified by this
evidence. The client power-save setting was restored to enabled.

## Reference and correction

The saved Apple CoreMac decompile was searched for the corresponding group
power-save path; no AP-specific queue implementation was identified there.
The Intel-specific scheduler contract is directly available in its DVM
reference implementation:

- [DVM commands.h](https://raw.githubusercontent.com/torvalds/linux/master/drivers/net/wireless/intel/iwlwifi/dvm/commands.h)
  reserves multicast queue 8 because firmware controls its scheduler state.
- [DVM mac80211.c](https://raw.githubusercontent.com/torvalds/linux/master/drivers/net/wireless/intel/iwlwifi/dvm/mac80211.c)
  assigns the AP's CAB queue from its context multicast queue.
- [mac80211 TX](https://raw.githubusercontent.com/torvalds/linux/master/net/mac80211/tx.c)
  selects the CAB queue for AP group traffic and marks delivery after DTIM
  when a station sleeps. DVM does not request host broadcast-PS buffering.
- [DVM TX](https://raw.githubusercontent.com/torvalds/linux/master/drivers/net/wireless/intel/iwlwifi/dvm/tx.c)
  uses the assigned queue and sets More Data for that DTIM burst; firmware
  clears it on the last frame. Non-QoS frames use firmware sequence control.

The IWN AP data path previously used BE queue 5 for multicast and borrowed
the selected peer's QoS state. It now uses dedicated queue 8 independently of
unicast BA state, sends group frames as non-QoS with firmware sequencing, and
marks the DTIM burst when any associated installed peer is sleeping.

Queue 8 receives the same first-transport-buffer and data-payload DMA storage
as the AP data queues. AP free-space accounting includes its capacity and
high-water state; low-water completion can reopen the Skywalk dequeue path.
Existing generic completion, reset, DMA-free and pending-work traversal cover
the queue. The existing PAN scheduler already maps queue 8 to FIFO 4, and
the existing beacon command supplies the TIM offset and size to firmware.

This is an IWN/DVM correction, not a queue-number change for IWM/IWX. Their
separate multicast queues and hardware behavior require their own audit and
runtime qualification.

## Verification boundary

`scripts/test_iwn_ap_multicast_dtim.sh` compiles the actual queue selector
and `getAPTxFreeSpace` under ASan/UBSan. It covers multicast independent of BA,
unicast BE/BA selection, multicast/BE/BA capacity and full masks, missing
queue capacity, absent clients and inactive AP. Source integration assertions
check DMA provisioning, non-QoS group frames, all-peer power-save aggregation,
More Data and the low-water wakeup path. The old production free-space method
from `236f264e` fails the multicast-full negative control.

The correction is not yet runtime-qualified. Required next checks are cold
neighbor establishment with client power save enabled, real firmware queue-8
completion, open/WPA2/WPA3 AP data and DHCP, then APSTA and true-S3 regression.
No successful release or elimination of the observed failure is claimed here.
