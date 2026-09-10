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

## Loaded image: two nonempty pressure stops and reset-free restarts

Source `3e73f218` built with all 1085 external symbols resolved. Private
five-member AuxKC admission and transactional activation preserved the four
companion members. The 05:24:10 UTC boot loaded UUID
`946F461F-3B19-38AA-8923-7AEF04263ED8`, matching Mach-O SHA-256
`94172899f0a0e9c522a54f05d8585303be826f2dca71136174a60c46cdd11f6f`.
The primary STA passed 5/5 and the independent management upstream passed HTTP.

Initial concurrent SAE group-19/required-PMF APSTA passed client-to-AP 20/20,
isolated cold-neighbor reverse 10/10 and primary 5/5. Normal public AP stop
during UDP pressure at 05:28:09 retired 193 pending aggregate descriptors to
zero. After a separate 15-second dwell the primary passed 10/10. The next
ordinary AP start and client join passed the same 20/20, cold 10/10 and primary
5/5 checks, without firmware recovery.

A second pressure stop at 05:30:41 retired 225 pending descriptors to zero;
its separate dwell and primary 10/10 passed. The second restart again passed
SAE/PMF, 20/20, cold 10/10 and primary 5/5. These role-7 checks use static
isolated addressing, not DHCP. The forced end of the UDP fixture is not a
throughput or lossless-transfer qualification.

The complete observer from 05:26:02 to 05:32:40 ended with zero diagnostic
errors, empty stderr, zero firmware fatals and zero hardware-stop entries.
It recorded 1998 wake requests and 7992 corresponding queue requests, all
with option 1. No AP data attempt occurred inside a wake request. All wake
durations were below 256 microseconds; all 11013 measured TX callbacks were
below 8192 microseconds. The longest observed RX duration was 45.892 ms,
not the previous 1.91-second dequeue pause. The independent complete serial
interval for this boot contains no firmware-fatal, device-timeout, panic or
AP TX gate-error storm.

The 3081 `EACCES` packet results now occurred outside RX and retained their
rejection semantics. Another 38 successful AP data calls occurred directly
inside RX through the existing `iwn_drain_ap_ps_queue` power-save path; those
are not nested Skywalk dequeues and must not be counted as a failed async
kick. This result closes the reproduced two-cycle pressure/restart failure
on this IWN image, not every AP lifecycle, full GUI matrix or IWM/IWX hardware.
Actual S3 and native sharing security/DHCP/traffic are recorded below as their
separate release gates complete.

## Same-image S3 and native system sharing

The 05:33:23 UTC sleep request reached actual ACPI S3, independently confirmed
by the serial sleep record and owned VM's suspended state. The temporary USB
management interface had been removed while awake; no emulated Ethernet was
present during sleep. Wake at 05:34:27 produced ACPI S3 WAKE and retained the
same boot-session and loaded-kext UUID. Fresh USB management was attached only
after wake and passed its independent HTTP upstream check.

The driver restored the concurrent AP without another AP-start command.
Explicit external-client reselection completed SAE group 19 and required PMF;
client-to-AP 20/20, isolated cold-neighbor reverse 10/10 and primary 5/5 passed.
This is service recovery, not automatic external-client continuity. Normal
public AP stop retained primary traffic at 10/10; the temporary static AP
address was removed before native sharing qualification.

The system sharing producer then enabled WPA3 on that same post-S3 boot.
The external client obtained a real DHCP lease and independently reported
SAE with required PMF. Forward 20/20, bridge-scoped cold-neighbor reverse
10/10 and routed HTTP through the guest upstream passed. Normal disable at
05:44:30 was followed by a separate 15-second dwell: the old bridge reached
I/O count zero and detached state, and primary traffic passed 10/10.

The first post-wake observer ended at 05:45:14 with zero diagnostic errors,
empty stderr, zero firmware fatals and zero hardware-stop entries. It recorded
27 AP wake requests, 108 queue calls with option 1 and no data call inside a
wake request. A 314.844-ms RX interval during subsequent primary activity is
retained in the evidence: the change does not claim to eliminate every long
RX handler. Native WPA2/open qualification and final console audit remain
in progress; the public release is still unchanged at this checkpoint.

WPA2 and open native sharing subsequently passed actual DHCP, independently
verified security mode, forward 20/20, bridge-scoped cold-neighbor 10/10 and
routed HTTP on this same post-S3 boot. There was no manual bridge rewrite,
daemon restart, radio toggle or guest reboot between the three modes. WPA2
disable at 05:47:35 also passed the separate 15-second dwell, detached bridge
with I/O count zero and primary 10/10.

The final open disable at 05:49:53 retired the bridge with I/O count zero,
but its 15-second primary check failed to bind because the STA address was
absent. This failed check is retained, not relabeled as a pass. Airportd
selected a weak 5-GHz candidate at 05:49:58.879; that association timed out
with `-3905` at 05:50:09.112. Its next automatic selection chose a stronger
2.4-GHz candidate at 05:50:18.993, associated at 05:50:21.114, and DHCP
published success at 05:50:21.137. No explicit selection or off/on intervened.
A later independent primary check passed 10/10. Thus automatic service
returned about 28 seconds after disable, not within the initial 15-second
check. This remains a real candidate-selection/reconnect delay, not an AP
firmware reset, and is not claimed fixed by the asynchronous TX correction.

The second post-wake observer ran from 05:45:34 to 05:52:19 and terminated
with zero errors, empty stderr, zero firmware fatals and zero hardware-stop
entries. It recorded 56 AP wake requests, 224 queue calls all with option 1,
471 TX callbacks and no data call nested inside a wake request. Its 21 direct
RX data calls used the existing power-save path. The complete serial interval
for this boot, including the final recovery and dwell, contains no matched
driver panic, firmware fatal, device timeout or AP TX gate-error storm.

The release archive SHA-256 is
`c0b26819fbe45072584cdbd5adf461d624813b7bbdacb9ca208897b771af95e8`.
Its extracted Mach-O matches the frozen and loaded image identified above.
The qualified change is reset-free IWN AP pressure/restart and tested
open/WPA2/WPA3 data service, including real S3 recovery. The failed weak-BSS
selection, complete GUI/profile matrix, automatic external-client continuity,
active-AP off-channel scanning, ad-hoc and equivalent IWM/IWX hardware
qualification remain open; they are not erased by these passing AP checks.

The exact Tahoe release asset was replaced at 05:54:35 UTC; the release notes
were updated one second later and explicitly retain the failed 15-second
return check and subsequent automatic recovery. A fresh GitHub download was
byte-compared with the qualified archive and independently rehashed; both ZIP
and extracted Mach-O match the identities above. The published notes were
also compared with their staged text. The previous archive remains privately
available for rollback. No physical user host was installed or rebooted.
