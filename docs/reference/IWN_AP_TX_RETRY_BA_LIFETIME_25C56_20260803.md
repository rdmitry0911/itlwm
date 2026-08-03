# IWN AP aggregate retry and Block Ack lifetime on Tahoe 25C56

## User-visible failure

A real WPA3 AP run remained associated but lost guest-to-station traffic after
the firmware reported aggregate status `0x82`.  The driver reclaimed the
reported SSN, then treated that ordinary transmit retry exhaustion as a reason
to send DELBA, stop the PAN aggregate scheduler queue and mark the TID blocked
for the rest of the client association.  Subsequent traffic fell onto a path
whose queue state no longer matched the negotiated peer state.

## Reference contract

`0x82` is `IWN_TX_STATUS_FAIL_SHORT_LIMIT`: one MPDU exhausted its short retry
limit.  It is not a Block Ack agreement transition.

Linux DVM's `iwlagn_rx_reply_tx()` handles the corresponding single-frame
completion on an aggregation queue by reclaiming descriptors through the
firmware SSN and reporting the MPDU without an ACK.  mac80211 follows that
report with a compressed BAR so the peer advances its receive window.  It does
not stop the RA/TID scheduler queue or send DELBA.  The existing itlwm STA
completion has the same lifetime rule: it sends the compressed BAR, advances
the BA window and retains the agreement.  A peer DELBA or association teardown
is what ends that agreement.

`FAIL_PASSIVE_NO_RX` has a separate Linux DVM queue-blocking workaround only
for a station-mode interface.  Applying it as a generic AP-side terminal
status was therefore also contrary to the reference.

Primary comparison points:

- Linux `drivers/net/wireless/intel/iwlwifi/dvm/tx.c`,
  `iwlagn_rx_reply_tx()`.
- itlwm IWN STA `iwn_ampdu_tx_done()` completion immediately following the AP
  branch.
- Tahoe runtime firmware status and queue trace from the WPA3 AP test.

## Implementation contract

- AP aggregate completion retains the negotiated BA session for all ordinary
  per-MPDU TX statuses, including `SHORT_LIMIT`.
- A failed MPDU is reclaimed first.  Only a completion which actually releases
  at least one descriptor submits a compressed BAR; an empty reclaim, like an
  empty Linux `skbs` list, cannot generate a duplicate BAR.  Its BAR carries
  the failed MPDU header sequence plus one rather than the firmware reclaim
  SSN.
- The BAR uses the PAN best-effort queue and the exact DVM non-data command
  shape: `ACK | IMM_BA | STA_RATE`, PAN broadcast firmware station ownership,
  and the 60-attempt BAR retry limit.  Its receiver address remains the
  associated client; firmware station ownership and over-the-air RA are
  separate domains.
- Both compressed-BA and single-frame completion paths reject SSNs older than
  the shared AP transmit window.  A forward SSN is accepted only when its
  distance is no larger than the number of descriptors still owned by the
  ring; the window starts at the live SCD activation SSN rather than the
  earlier asynchronous ADDBA request SSN.
- The sequence-domain check is not sufficient by itself.  Candidate wip52
  accepted a forward SSN and later reached the impossible state
  `qid=11 queued=194 cur=227 read=227`.  Linux's transport-side
  `iwl_txq_reclaim()` has the missing second fence: the descriptor immediately
  before the exclusive reclaim target (`last_to_free`) must still be inside
  the physical `[read_ptr, write_ptr)` interval.  The IWN port now applies the
  same circular-ring ownership test before either a compressed-BA or a
  single-frame SSN may submit a BAR or move `read`.
- The shared logical BA window is committed only after physical reclamation
  succeeds.  A rejected low-byte SSN therefore cannot poison the next
  completion's monotonicity test.
- It reclaims the aggregate queue through the firmware-provided SSN, applies
  low-water wakeup and refreshes the watchdog timer.
- `DEST_PS` remains a distinct ownership transfer: the filtered mbuf moves to
  the AP power-save queue before descriptor reclamation.  The transfer sets
  the descriptor's `m` to null, but does not relinquish the physical TFD.
  Aggregate reclaim therefore keys descriptor ownership from `m`, `ap_data`
  and `ap_mgmt`, resets the scheduler slot and decrements `txq->queued` even
  when the mbuf is already owned by the PS queue.  The common descriptor
  cleanup is correspondingly null-safe and never frees the transferred mbuf.
- Aggregate completion contains no implicit DELBA, scheduler stop or
  per-association `Blocked` transition.

## Runtime root-cause evidence

Candidate wip53 carried both the logical SSN fence and Linux-style physical
`[read, cur)` ownership fence.  A real WPA3/SAE/PMF AP baseline passed SAE
group 19, required PMF/BIP, DHCP, bidirectional traffic and NAT.  Four-stream
AP-to-station load then failed with
`qid=11 queued=203 cur=19 read=19`.  Neither stale-SSN nor outside-owned-ring
diagnostics fired.  Equality of the physical indices proves that no submitted
TFDs remained, while the positive `queued` value proves accounting had leaked.

The only completion path which deliberately nulls an AP aggregate mbuf before
reclaim was `FAIL_DEST_PS`.  The old reclaim loop used `m != NULL` as both the
descriptor ownership predicate and the condition for `queued--`, so every
filtered frame transferred to the PS queue left one phantom queued descriptor.
The new contract separates those two ownership domains and emits a bounded
`IWN AP DEST_PS descriptor reclaimed` witness for the real-air acceptance run.

Candidate wip54 proved that fix under 180 seconds of four-stream real-air
AP-to-station traffic: all 388 MiB reached the receiver, the old qid 11
watchdog state did not recur, and bounded DEST_PS witnesses showed `queued`
falling with each transferred-mbuf descriptor.  A later independent firmware
assert (`0x1057`, PC `0xc17c`) exposed the next BAR transport mismatch.  At the
assert, aggregate qid 11 was already empty while PAN BE qid 5 alone held one
descriptor.  The preceding trace contained repeated failed BAR completions,
and the local command used client station 2, explicit legacy rate, 15 retries,
the firmware reclaim SSN, and BAR-before-reclaim ordering.

Linux DVM instead assigns every non-data frame to the PAN broadcast firmware
station, marks BAR with station-rate selection, uses 60 retries, and performs
transport reclaim before mac80211 can generate a BAR from the failed MPDU's
own sequence.  The corrected command and ordering above directly implement
those reference constraints.

Candidate wip55 then completed 180 seconds of four-stream real-air WPA3
AP-to-station traffic with 329 MiB received.  Retry exhaustion continued to
produce compressed BARs while retaining the negotiated BA session; the prior
firmware assert, stale/outside-ring reclaim and TX watchdog did not recur.
After a real S3, Tahoe's explicit upper AP-stop was honored, a fresh WPA3 AP
was configured through CoreWLAN, and the same aggregate path completed a
further 60-second four-stream run with 63.9 MiB received.  That run exercised
multiple `0x82` completions and successful BAR submissions, retained
association and BA ownership, and ended with 20/20 ICMP in both directions.
No firmware fatal, watchdog or lower reset occurred.

Static regression coverage is part of
`scripts/test_iwn_iwm_iwx_ap_tx_ampdu_contract.sh`.
