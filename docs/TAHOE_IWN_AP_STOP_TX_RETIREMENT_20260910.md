# IWN AP stop owns TX through FIFO flush and scheduler retirement

## Reproduced lower failure

The loaded `2dc501b2` candidate passed short native WPA3/WPA2/open DHCP,
bidirectional and routed-HTTP checks before and after real S3. Full console
inspection nevertheless found a TX watchdog after normal WPA2 PAN stop in
both intervals. The post-wake case retained three AP aggregate descriptors
on queue 12 (`read=38`, `cur=41`) after the AP owner had reached its ordinary
stop terminal. The management queue itself was empty. The production watchdog
then stopped and reinitialized the device; the candidate release remains held.

Normal PAN stop previously erased all client RA/TID queue maps without retiring
those queues. The separate DELBA path also reclaimed descriptors before it
obtained NIC access and disabled the scheduler. Therefore neither a successful
PAN command reply nor a zeroed AP client table proved transport retirement.

## Reference and correction

Intel's pinned Linux v6.12
[DVM forced aggregation stop](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/net/wireless/intel/iwlwifi/dvm/tx.c)
flushes the selected queue before disabling and deallocating it. Its
[command contract](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/net/wireless/intel/iwlwifi/dvm/commands.h)
defines `REPLY_TXFIFO_FLUSH` as command/response `0x1e`: the full-flush response
follows DMA completion and an empty FIFO; individual discarded TX commands
receive flush-status responses. The
[DVM producer](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/net/wireless/intel/iwlwifi/dvm/lib.c)
uses the eight-byte v3 carrier for API > 2, including this PAN firmware.
[Transport disable](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/net/wireless/intel/iwlwifi/pcie/tx.c)
marks SCD inactive before unmapping the actual submitted descriptor interval.
These are Intel transport contracts; no Broadcom register layout is inferred.

Normal IWN AP stop now closes AP frame/ADDBA admission, asynchronously flushes
only fixed PAN queues 4..8 plus this AP's owned dynamic queues, and retains
every client while awaiting the exact submitted command-ring index. Primary
STA, auxiliary and command queues are excluded. Duplicate/foreign responses
cannot advance the new stop state; a rejected command retains all ownership.

After the real flush reply, fixed queues must be reclaimed by actual TX
responses. Dynamic RA/TID queues are disabled under NIC access, their physical
read-to-write interval is released exactly once, and only then are cursors and
client maps retired. A PS-transferred mbuf does not erase its TFD ownership.
Malformed maps, overlapping STA ownership and inconsistent descriptor counts
are rejected. NIC-access failure preserves the maps and packets for retry.
The normal RXON/deactivation/PAN-parameters stop chain begins only afterwards;
its existing final reply remains the upper lower-stop terminal.

The shared IWN aggregate stop backend also moves reclamation after scheduler
deactivation instead of allowing the DELBA caller to free DMA before NIC access.
This does not claim that every peer-removal/CSA error or ordinary DELBA drain
policy is now qualified. IWM/IWX use different transports and are unchanged.

## Verification boundary

The ASan/UBSan regression compiles complete production queue-mask, FIFO-stop
continuation, exact-reply, AP stop, scheduler-stop and ring-reclaim methods.
Only external hardware/command I/O, unrelated scan/security reset and final
packet freeing are substituted. It covers multiple peers/TIDs, ring wrap,
PS mbuf transfer, exact/foreign/duplicate replies, NIC failure, rejected flush,
RXON submission retry, fixed-queue completion delay, malformed ownership,
unchanged concurrent STA queues and idempotent repeated retirement.

Unchanged `bdc2941f` stop and scheduler methods compile in the same harness.
The protocol negative control fails when it submits RXON without a completed
flush; the independent backend control fails when it rebases a nonempty ring.
The corrected methods pass both, along with the existing STA aggregate-stop,
AP A-MPDU, watermark, materialization, reset/replay and sleep contracts.

At this source-test checkpoint the new image has not been built or loaded.
Required runtime evidence is the exact loaded image, real pending-queue flush
and retirement, repeated native open/WPA2/WPA3 DHCP/traffic, concurrent STA,
true S3 recovery and a complete serial audit with a watchdog-length dwell
after each stop. Passing short traffic alone cannot promote the release.

## Loaded-image first native stop matrix

Source `f3dc3b11` built with all 1085 external symbols resolved. Private
five-member AuxKC admission passed without canonical mutation; transactional
activation preserved the four companion members. The 01:42:46 UTC guest boot
loaded UUID `6E433F38-C96D-3BB0-B755-E2F19F028A2A`, matching frozen Mach-O
SHA-256 `66716ba3beb7224256dcc67408d7deec555e7c2ddae38b4d28464f2a4285dfcc`.
The independent management upstream and primary STA first passed HTTP and 5/5.

Native WPA2, WPA3 and open sharing then ran sequentially. Each external client
obtained DHCP, passed 20/20 client-to-AP, isolated cold-neighbor 10/10 reverse
traffic and routed HTTP. Each separate normal disable was followed by a
15-second dwell, bridge retirement and 10/10 primary packets. The complete
console interval from the first start through the final stop contains no
device-timeout or panic diagnostic. There was no reboot, radio toggle or
userspace daemon restart between modes.

The read-only observer recorded the real flush command/index reply, scheduler
stop, successful retirement and subsequent normal stop terminal. Its actions
use function arguments only, not older private-structure offsets. The first
WPA2 stop completed command 30/index 245 and retired dynamic queue 11; its
descriptor count was already zero. Thus this pass demonstrates the real new
protocol, but not yet retirement of a nonempty AP queue under load. A bounded
traffic-during-stop test, actual S3 and the full post-wake matrix remain required;
the release is still held.

## S3, post-wake native matrix and nonempty queue retirement

On the same `f3dc3b11` image, concurrent SAE/required-PMF AP and WPA3 STA
passed 20/20 external-client packets, isolated cold-neighbor 10/10 reverse
packets and primary 5/5. The temporary USB upstream was removed while awake.
The 02:01:49 UTC sleep request reached real ACPI S3 and the owned VM's
suspended state. After wake, ACPI S3 WAKE, the boot-session UUID and loaded
kext UUID confirmed resume rather than reboot. A fresh USB upstream then
passed HTTP. Explicit external-client reselection to the automatically
replayed AP passed 20/20 and cold-neighbor 10/10 without a new AP-start call
before either data test completed.

An operator diagnostic invocation without arguments subsequently started the
probe's default open AP rather than printing usage. This occurred after those
AP data tests and is recorded as a separate public stop/replacement, not as
part of automatic wake continuity. Primary 5/5 initially passed, but a later
check lost all packets despite a retained address. Normal AP stop did not
restore it; ordinary credentialed network selection did, in nine seconds,
followed by 10/10 without radio off/on or reboot. Attribution to AP security
replacement is not yet established; this adjacent case remains under review.

Native post-wake WPA2, WPA3 and open sharing each subsequently completed real
DHCP, 20/20 forward, cold-neighbor 10/10 reverse and routed HTTP. Each normal
disable was followed by a 15-second dwell and primary 10/10. A bounded UDP
load through WPA2 also stopped normally. Those system-sharing stops happened
with an already empty aggregate queue, so they alone do not prove nonempty
retirement.

The separate role-7 WPA3 UDP test supplied that missing witness at 02:18:28
UTC. Matching-build DWARF observation found queue 11 with 204 outstanding
descriptors (`read=161`, `cur=109`) both on lower stop entry and at actual
FIFO-flush submission. Firmware command 30/index 49 completed successfully.
Only afterwards did scheduler retirement reclaim all 204 descriptors; the
serial record reports `pending=204 remaining=0`. The next lower stop returned
success. The 15-second dwell and primary 10/10 passed without a device-timeout
or panic. The bounded UDP server's forced shutdown after AP removal is not a
throughput or lossless-transfer qualification.

After the native open stop's successful primary traffic, a separate background
WCL roam selected another BSS and timed out in association. The live observer
shows RUN/port-valid first, then ASSOC/closed-port, and only then watchdog
policy cleanup. Therefore that cleanup is not evidence that AP stop itself
cleared a working primary's RSN policy. The complete GUI/roam surface remains
open, and publication awaits the final replacement control and console audit.

## Final restart control: candidate remains held

The next role-7 WPA3 run exposed a firmware fatal at 02:20:32 UTC, after
the successful nonempty stop above. The complete serial error record gives
type `0x22CE`, PC `0x26294`, source line `0x5e` and error data
`0x000000ff0000005e`. Queue 7 held one descriptor; the retired AP aggregate
queue 11 held zero. The matching observer records `iwn_hw_stop` and ordinary
driver-reset recovery. This is a firmware reset, not a guest reboot, and is
not explained by the earlier three-descriptor stop watchdog. Its cause is
not yet established. The broad AP Ethernet TX rejection log around it does
not identify the rejected frame or prove a command-gate locking failure.

After automatic recovery, an explicit WPA3-to-open AP replacement began at
02:20:55. The primary initially carried traffic. At 02:21:37 the actual Apple
`WCLNetManager::handleMissedBeacons` path requested `LEAVE_NETWORK`, followed
by driver policy/RSN cleanup. Thus those cleanup calls were consequences of
the system's missed-beacon decision, not evidence of an unsolicited policy
clear by AP stop. The primary received 47 of 75 packets before its address
was withdrawn. Normal AP stop and ordinary saved-network selection then
restored 10/10 without radio off/on, daemon restart or reboot.

The ten-minute combined queue/policy observer completed at 02:24:46 UTC with
zero diagnostic errors. The nonempty retirement and native pre/post-S3
traffic results remain valid within their stated intervals; they do not
qualify repeated AP service as reset-free or close primary beacon continuity.
The `f3dc3b11` archive remains unpublished. The public Tahoe release is still
the previously qualified `893a3114` image.
