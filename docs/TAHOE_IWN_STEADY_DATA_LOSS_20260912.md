# IWN steady-state packet-loss localization

This is a diagnostic continuation on the already-published f170870d image,
not another production fix or proof of lossless reconnect. The previous exact
qualification and publication are in
[the owned-AUTH report](TAHOE_IWN_SINGLE_AUTH_RXON_20260911.md).
Loaded UUID68F5B0D9-4863-3627-B30C-CC0CC111BB58 and boot
B4FEB27A-EDDA-4A70-AA35-29B68EF3AE1A remain unchanged.

## Topology and fixed experiment

Guest IWN/6235 uses LabAP ca/channel9,172.16.66.219. Host AX211 uses LabAP
c9/channel153,172.16.66.226, with its normal power-save setting on. Traffic
therefore crosses two wireless BSSes. Wired host management and guest USB
diagnostics remain separate. No radio toggle, network selection, AP mutation,
sleep or driver reload is used in these probes.

After the earlier20/18,19/20 and249/250 failures, a safe per-packet observer
passed a250-packet positive control. It reads only the driver's existing
successful20-byte IPv4-header copy in ieee80211_classify, keeps scalar packet
identity through its immediate encapsulation/TX owner, and joins scheduler
queue/index to the actual firmware response. It never reads beyond an mbuf
segment, walks a private kernel list, retains a dereferenced packet/node
pointer, or samples crypto keys/payload. The initial unsuccessful observer
and the calibration that found header-only first segments remain in the prior
frozen evidence; errors0 alone is not accepted as observer coverage.

The first new run was declared as1000 packets each way,1400-byte payload,
200-ms spacing, with220-second endpoint captures and230-second driver
observation. Its controller ended normally at21:35:40UTC.

## First long run: loss is on guest egress, not missing incoming requests

Guest-to-AX211 requests passed1000/1000. AX211-to-guest echo passed997/1000.
The guest capture contains4000 ICMP identities; the host has3997. All1000
incoming echo requests reached the guest and generated outgoing replies.
Exactly three guest replies are absent at the host: ICMP ID48680,
sequences379,433,705. Both captures report zero kernel drops and no duplicate
identities. Thus these three losses occur after guest Ethernet egress and
before host Ethernet ingress; they are not DHCP loss or unhandled inbound
echo requests.

All1000 guest-originated requests have exactly one classified identity,
scheduler submission, successful iwn_tx return and single-frame firmware
success (status0x01). No scan, newstate, encapsulation drop or observer error
was recorded. The three lost echo replies had IPv4 ID0 and were outside that
identity method. It would be wrong to assign the successful request receipts
to those unobserved reply transmissions.

Guest/host pcap SHA256:
`77eb8996579ec48e1f8e5beb6804959c68dafb5ef1bd2ffa9024dcc4da655e2c`,
`138cd097ddc489ce18603bb78806b9b16d0bcaf51dc1dd991b6f1f12e828d301`.
Driver trace SHA256:
`3bdd5f75093ae2300b2bfc958daa19741e1f05d920aa042ce47771e36cca1a02`.

## Reference boundary and next fixed control

The pinned [Intel DVM TX implementation](https://raw.githubusercontent.com/torvalds/linux/v6.18/drivers/net/wireless/intel/iwlwifi/dvm/tx.c)
and [firmware command ABI](https://raw.githubusercontent.com/torvalds/linux/v6.18/drivers/net/wireless/intel/iwlwifi/dvm/commands.h)
separate a single-frame result from a multi-frame aggregate notification,
whose Block Ack arrives separately. The local observer reports multi-frame
results as unresolved rather than treating a transmit-status word as an ACK.
The ABI names0x82 as TX_STATUS_FAIL_SHORT_LIMIT; observing it localizes a
firmware TX failure, but does not by itself establish why retries exhausted
or prove a driver programming defect. Rates, protection, coexistence and RF
conditions must be checked before changing a retry policy.

The second fixed1000-packet pair changes only host ping to `-M dont`.
Its initial endpoint capture confirms nonzero IPv4 IDs on guest echo replies,
and those IDs also appear at the real firmware receipt. The existing observer
can now cover both outgoing guest flows. This is a separately labelled
diagnostic, not a retry that replaces the first failure. Correlation uses IP ID
plus timestamps from the same guest clock, and refuses ambiguous matches.
It ended normally at21:40:51UTC with1000/1000 forward and999/1000 reverse.

## Second long run: an exact lost packet has a firmware TX failure

The guest capture contains4000 ICMP identities and the host3999, both with
zero capture drops and no duplicates. The only missing packet is the guest
reply to host ICMP ID51454/sequence193. Its IPv4 ID31068 is unique in the
same-clock matching window. The complete observed chain is:

- Guest Ethernet egress:1789162658.776305s.
- Driver classification:1789162658776360105ns, cookie4084461981996.
- Actual scheduler submission:1789162658776694941ns, queue11/index26,
  station0, length1478; iwn_tx then returns0.
- Firmware response:1789162658794610153ns, one frame, status0x82,
  ACK-failure count1. The peer never captures this packet.

All2000 guest-egress packets have structurally complete per-cookie chains.
There are1997 single-frame successes and three0x82 failures, with no multi-
frame result left unresolved. The other two failed-status frames are guest
echo requests ID41228/sequences65 and593 (IPv4 IDs38316 and16559); both
appear at the peer and receive their echo responses. Firmware failure is
therefore not equated with certain absence at the receiver. Conversely, the
one actually missing reply is not an upper enqueue, DHCP or SAE failure: it
was submitted to hardware and its matching firmware result is unsuccessful.
There are no scan/newstate events, observer errors or encapsulation drops.

This isolates one steady-state lost packet to the firmware transmission/
acknowledgment outcome. It does **not** establish why the short limit was
exhausted, explain the first run's unobserved reply statuses, or prove a new
driver/reference discrepancy. The next lower check must retain RTS failures,
rate/antenna/protection and retry-command settings, compare those with DVM,
and distinguish RF loss from incorrect programming before a semantic change.
Increasing retries or relabelling the packet as delivered is not a justified
fix. Independently, the larger first-request target-discovery/reconnect gap
remains a user-facing functional priority.

Q2 guest/host pcap SHA256:
`7ff1044f2a47aa28791ea8f59d0843ead919810724cbc28b49bf14d32bf5d111`,
`da6a9a8a8204bcf59426f521cb25b8efd7a1dc63247e180dcef6b69d2d7c267c`.
Driver trace SHA256:
`803366fbe6185e8d1a0cb7a8645204bcf49908392d51e2d7fddef33ad8b63909`.
Pinned DVM TX/command-source SHA256:
`3d09edaff6490ee886288388260773717e182e64c693c11eca210a65f2db50e3`,
`e1ae709aca5047b8542e7f6dddee3e84bc94f622df83339cc3267f74b017ffb4`.

Frozen evidence:
`/home/dima/Projects/itlwm/aiam-data-loss-runtime.8ugYN4`.
All70 entries of EVIDENCE.sha256 verify; manifest SHA256:
`9f0804d46afa9a4251f82756d325070d506e035aa3f6d6afb621e3da5351ff13`.
Do not append later diagnostics to this root. Final cleanup at21:42:59UTC
confirms the same boot/image, WPA3 address and channel9, no remaining observer
or capture, host profile/PS restored and unchanged wired management route.
No new production change or replacement kext is justified by these diagnostic
runs alone. Intermittent first-request BSS discovery, full GUI/profile/sleep
combinations and IWM/IWX RF parity remain independent open functional layers.
