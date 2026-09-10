# AP TX dequeue must not run inside an RX notification

## Exact loaded failure

The `b91bb5a5` image, UUID `67773F1E-126D-3368-B486-5DBB7D4F1DF5`,
still fails AP recreation after the independently verified nonempty TX stop
and PAN AUX correction. The same firmware assertion is reproducible with
WPA2, so absence of SAE or PMF does not remove this failure.

The first bounded RX observer recorded a 2,154,947 microsecond notification
handler, including 2,154,675 microseconds in the AP firmware-event callback.
The RX write-pointer register was not updated during that interval. There
was no observed synchronous host command inside RX and no comparably slow
`if_input` phase. These results reject the initial hypothesis that the
measured pause was in the upper input-delivery gate.

A second function-only observer ran from 04:59:33 to 05:06:04 UTC, ending
with zero diagnostic errors, empty stderr and one firmware fatal. Initial
SAE/required-PMF admission passed client-to-AP 20/20, isolated cold-neighbor
10/10 and concurrent primary 5/5. Ordinary public AP stop during UDP pressure
completed, followed by a separate 15-second dwell and primary 10/10.

The next client's LINK_QUALITY reply invoked this exact stack:

`iwn_notif_intr -> iwn_note_ap_firmware_event -> iwn_send_ap_assoc_success ->
airportItlwmRequestAPTxDequeue -> AirportItlwm::requestAPTxDequeue ->
IOSkywalkTxSubmissionQueue::requestDequeue -> packetSubmission ->
legacyDequeue -> skywalkTxAction -> iwn_ap_data_tx_action`.

That dequeue processed 1020 packets, all rejected, in 1,905,803 microseconds;
the complete RX handler took 1,907,621 microseconds. The return-code census
records 1020 `EACCES` results inside these nested kicks. Firmware asserted
at 05:02:33, followed by device recovery. Passing traffic after that recovery
is not a reset-free restart pass. Initial cleanup also required a retry;
normal public stop and ordinary saved-network selection finally restored
primary traffic at 10/10, without a radio toggle or guest reboot.

Separate ARCompact decoding of the byte-identical 6235 runtime firmware
locates assertion `0x22ce`, PC `0x26294`, in the producer's bounded wait for
RX FIFO space. Three valid callers cover command replies, notifications and
TX status. Exact instructions, including the shared register-saving prologue,
are authoritative over incorrect inferred C signatures. The timeout compares
a hardware counter; its conversion to seconds has not been established.
This connects the failure to RX service starvation, but the candidate still
needs a reset-free live pressure/restart test to establish closure.

## Exact Skywalk scheduling contract

The guest is 25C56. Its own BootKC symbol table locates
`IOSkywalkTxSubmissionQueue::requestDequeue(void *, unsigned int)` at
`0xffffff8002a32dc2`. Read-only 40-CPU analysis of the matching saved 25C56
project shows:

- With option bit 0 clear, it closes the event-source gate and directly calls
  virtual `packetSubmission(false)` before returning.
- With option bit 0 set, it increments the pending-work counter at `+0x148`,
  calls `IOEventSource::signalWorkAvailable` at `0xffffff8000ab1f00`, and
  returns without invoking the packet action.
- `checkForWork` at `0xffffff8002a32a8e` compares that counter with the serviced
  counter at `+0x14c`. Packet submission and legacy dequeue retain their
  enabled/ready admission checks.

The PCIe Broadcom override at `0xffffff80014a8900` and core override at
`0xffffff800153b022` are parent-dispatch tails, not separate async workers.
Older 26.3 exported addresses were not reused as 25C56 function addresses.
The local old header's `requestAsyncRefill` declaration is not a verified
25C56 export and is not used. No private structure offset is written by the
driver or required by the implementation.

## Correction and source verification

The shared IWN/IWM/IWX AP wake helper now requests the native asynchronous
dequeue option. It retains the existing running/enabled checks and queue
event-source lifetime; no additional timer, borrowed controller callback or
new teardown owner is introduced. The RX handler can finish and return its
descriptors before that workloop performs AP TX. Existing packet completion,
security rejection, free-space admission and low-water retry are unchanged.

IWN also stops logging ordinary `OutputDropped` and `NoResources` packet
results as command-gate failures. Their return values and upper completion/drop
accounting are retained. Actual gate failures remain distinguishable. This
prevents a rejected backlog from producing an unbounded console line per
packet while the same controller workloop is held; merely deferring that
console storm would not establish adequate RX service.

The ASan/UBSan test compiles the complete production wake, IWN gated transmit
and error-mapping methods. It covers all 16 enabled-queue combinations, 2000
coalesced requests, counter wrap, stop/removal before dispatch, null queues,
normal packet rejection/backpressure, actual gate rejection and unavailable
owners. The queue fixture models only the recovered option dispatch; it is
not substituted for runtime evidence of Skywalk packet ownership or timing.
Unchanged `8aa8bf06` methods independently compile and fail both the nested
callback assertion and the ordinary-result console-storm assertion. Corrected
methods, full payload tests and adjacent three-family watermark/backpressure
contracts pass without sanitizer diagnostics.

Build, exact-image activation, RX/TX timing, repeated nonempty pressure stops,
native open/WPA2/WPA3 DHCP/traffic and actual S3 remain the runtime gates.
The public `893a3114` release is unchanged at this source checkpoint.
