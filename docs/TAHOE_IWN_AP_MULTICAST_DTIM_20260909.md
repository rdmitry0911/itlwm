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

## Loaded-image on-air qualification

Source `ad910bab` passed the tests above and existing AP watermark,
backpressure, A-MPDU, client-materialization and fresh-scan regressions. The
Tahoe build resolved all 1083 BootKC symbols. Private AuxKC admission passed
without canonical mutation, and transactional activation preserved the four
companion members. The guest loaded UUID
`5EBD9812-B43C-3720-8ADF-B3C85C68AE8B`, matching frozen Mach-O SHA-256
`a1b1d27ac7ed5b9b386fa63e14d09e9fd024bc68814bc4219e8eed7edf8ff92f`.

At 18:19:16 UTC, with the external SAE/PMF client in power-save mode, deleting
the guest's isolated AP-interface ARP entry was followed by 10/10 source-bound
1400-byte replies. No client-originated ping ran during this cold test. The
external capture contains the AP's ARP request, the client's reply and all
ten echo exchanges. The matching TX observer recorded queue 8, broadcast
station 14, non-QoS TID 8 and a successful completion. Subsequent independent
client-to-AP traffic passed 20/20, and the concurrent primary STA passed 5/5.

The 18:20:10 sleep request reached actual S3: the owned QEMU was suspended
and serial recorded `ACPI SLEEP`. Owned-monitor wake at 18:21:02 produced
`ACPI S3 WAKE`. After explicit external-client reselection, SAE group 19,
required PMF/BIP and power save remained enabled. A second isolated cold
ARP run at 18:21:35 passed 10/10. Queue-8 completions included a More Data
frame followed by a final frame without that bit. The primary passed 5/5;
boot epoch and loaded UUID were unchanged. AP stop retained primary traffic
at 10/10. This qualifies service recovery, not automatic client continuity.

The emulated USB management path initially did not return after S3. The
post-wake cold test therefore used the independent physical STA for SSH,
not the AP/client data path. USB management became available later; its
intermediate failure is not claimed fixed by this change.

Standard Internet Sharing subsequently started a WPA3 AP on the same boot.
The external client completed SAE/PMF, obtained a dynamic lease from system
bootpd and passed 20/20 client-to-gateway and 10/10 reverse packets. This
standard path uses `bridge100`: the initial diagnostic attempted ARP deletion
on `ap1` and returned `No such process`. Therefore its 10/10 result is ordinary
traffic, **not** an additional cold-neighbor proof.

Changing standard sharing from WPA3 to WPA2 exposed a separate incomplete
stop/start boundary: the daemon failed bridge creation with `EBUSY` before
DHCP setup. That failure is documented in
`TAHOE_INTERNET_SHARING_BRIDGE_RECYCLE_20260909.md` and remains under
investigation, including whether the new TX lifetime changes contribute.

With explicit isolated static addressing on the real AP interface, WPA2 and
open APs each passed 20/20 client-to-AP packets. Separate cold runs, without
concurrent client-originated traffic, passed 10/10 on open at 18:33:52 and
WPA2 at 18:37:59. Both used enabled client power save and observed successful
queue-8 completions; the open frame was unprotected and the WPA2 frame
protected. Earlier overlapping ping runs are not used as isolated cold proofs.
The first open attempt was invalidated by a host NetworkManager keyfile-writer
assertion while changing a temporary profile's security type. The service
automatically recovered; a separate open profile was then used for the valid
runs. That fixture failure is not attributed to the guest driver.

This closes the reproduced cold-neighbor power-save delivery defect on the
loaded IWN image. It does not close DHCP across standard sharing stop/start,
all AP sleep/client combinations, or equivalent IWM/IWX hardware coverage.
The candidate archive is prepared but held pending bridge-lifecycle work.
ZIP SHA-256:
`383403c4697840a536328bbf00c7800ade5379f05f90dfeb0476ec478c80896e`.
