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
