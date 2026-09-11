# IWN TX outcome versus two-BSS delivery

This follows the [exact failed-TX localization](TAHOE_IWN_STEADY_DATA_LOSS_20260912.md)
on the unchanged published f170870d image. It is diagnostic evidence, not a
new driver fix or a lossless-connectivity claim.

## Source/reference check

The ordinary IWN TX builder explicitly assigns RTS retry60 and data retry15,
infinite lifetime, timeout0 for data, initial LQ index0, and aggregation
protection. AP command builders clear the entire command buffer. Some unused
ordinary command fields are not reassigned, but no actual stale field has
been established. A buffer-hygiene hypothesis is not a proven packet-loss cause.

Pinned [Intel DVM TX construction](https://raw.githubusercontent.com/torvalds/linux/v6.18/drivers/net/wireless/intel/iwlwifi/dvm/tx.c)
and [retry defaults](https://raw.githubusercontent.com/torvalds/linux/v6.18/drivers/net/wireless/intel/iwlwifi/iwl-agn-hw.h)
use the same ordinary60/15 limits and station LQ selection for data. The
[status mapping](https://raw.githubusercontent.com/torvalds/linux/v6.18/drivers/net/wireless/intel/iwlwifi/dvm/agn.h)
does not turn SHORT_LIMIT into an ACK. The current evidence justifies neither
increasing retry limits nor reclassifying unsuccessful firmware results.

## Fixed1000-packet pair

The same guest IWN/6235 uses LabAP ca/channel9; external AX211 uses LabAP
c9/channel153, normal power save on. At21:56:50UTC one predefined pair of
1000 packets per direction,1400-byte payload and200-ms spacing started.
No scan, join, radio toggle, driver reload or peer-PS change was requested.

Each direction received999/1000. Guest capture has3999 ICMP identities and
host3997, without capture drops or duplicates. Two guest-egress packets are
absent at the host:

| Guest packet | IPv4 ID | Scheduler queue/index | Firmware outcome |
| --- | --- | --- | --- |
| request ICMP4878/seq256 |23986|11/147|status0x01, ACK/RTS/BT failures0|
| reply ICMP61389/seq764 |29102|11/118|status0x01, ACK/RTS/BT failures0|

Both are individually correlated by IPID, direction and a unique same-guest-
clock window. All2000 guest egress packets have complete identity -> scheduler
-> iwn_tx return -> single-frame response chains; all2000 firmware statuses
are successful. None are unresolved multi-frame aggregation responses.
Both missing packets therefore differ from the earlier exact0x82 loss.
Success at the IWN-to-first-AP firmware boundary does not establish end-to-end
delivery through the AP bridge and second wireless BSS.

Across the2000 receipts:345 RTS-retried frames (maximum47),224 ACK-retried
frames (maximum9), zero BT kills, rate7/rflags0x61 throughout. There are no
scan/newstate events, encapsulation drops or DTrace errors. The retry counts
show radio work, not a demonstrated programming defect. Per-packet TX command
fields were not sampled; source assignments are explicitly not live readbacks.

## Observer word-width correction

A constant-only DTrace experiment established that narrow byte expressions
do not automatically widen as assumed: uint8(198)|(uint8(5)<<8) prints198;
explicitly widening both operands prints1478. The first policy observer's
wireseq/xrflags/duration/length expressions therefore preserved only their low
byte. Earlier packet observers' narrow wireseq output has the same limitation.
Those values must not support full-sequence/length conclusions.

The decisive IPID is stored as uint32 and independently matched against pcap.
Queue/index, status, frame count, ACK/RTS/BT counters are single-byte fields,
so the per-packet outcome conclusions remain valid. No private object offset,
crypto key or packet payload was read. The fixed8-byte RX envelope and36-byte
TX response are asserted from the actual extracted production structures on
Linux and Tahoe.

A separate100-packet-per-direction calibration widens all combined words.
It passes100/100 each way,200 complete successful chains and400 matching
endpoint identities. Full raw response length1478, sequence50800 and following
values, and xrflags0xc000 are now visible. Undocumented high-rate flags are not
assigned an invented meaning. This clean small control does not erase q1 loss.

Q1 guest/host pcap SHA256:
`a00accd00c74cc953135add81a7a5772b302e8b8b6725a49711a354a0688b6ee`,
`566e929b7ff96e87d9753cfad0f51707c30da102e3ccb81e265b89c936a1c8e3`.
Q1/Q2 trace SHA256:
`c8b8a86a6eb05e8c23ea75566b6827088b86007beb3724090b05a6a80338e6a7`,
`4f9873080e002e02ae5ce44def731553c92b2e21e4e9ef70067559873781434e`.

Working evidence: `/dev/shm/aiam-tx-policy-20260912.soJbIJ`.
Durable evidence: `/home/dima/Projects/itlwm/aiam-tx-policy-runtime.nZE9KV`.
All73 entries in EVIDENCE.sha256 verify; manifest SHA256:
`291bfaf6a64411a8a99ff5b381623064432317cd4112cc62103677be087e3fbc`.
This root is frozen and must not receive later diagnostics or receipts.
The earlier frozen534-file and70-file evidence roots remain unchanged.

## Next functional boundary

Steady-state losses have more than one observed firmware-outcome class. Do not
continue speculative retry tuning to force this path green. The first-request
SAE/reconnect surface remains a higher-impact independent deficiency: the
current engine clears its prepared public Commit/Confirm after successful TX
and has no same-exchange peer-response retransmission. Pinned mac80211 retains
the current Authentication body and retries it with a bounded per-exchange
owner. A controlled single-Commit omission on the lab AP is the next runtime
test; it must establish exact TX completion, lack/presence of same-generation
retry, final native status and restoration before implementation.

That subsequent [controlled omission and positive control](TAHOE_SAE_PEER_RESPONSE_RETRY_20260912.md)
are now complete on the unchanged image. They establish the same-exchange
SAE recovery defect separately from steady-state data-path loss.
