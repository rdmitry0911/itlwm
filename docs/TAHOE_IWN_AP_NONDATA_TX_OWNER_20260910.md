# AP non-data TX uses the PAN broadcast firmware station

## Repeated firmware failure

The loaded `f3dc3b11` candidate completed a real WPA3 AP/client data check,
UDP-pressure stop and primary 10/10, then restarted the same AP normally.
A read-only observer recorded two SAE authentication replies through PAN
broadcast station 14, with actual TX completions. At 02:38:01 UTC the next
Association Response (frame control `10:00`, length 152) was submitted on
management queue 7/index 101 through client station 2. At 02:38:05 the fatal
interrupt found queue 7 with one descriptor and next cursor 102; the last
observed management submission was that Association Response. The driver
then performed its firmware-reset recovery. Subsequent short traffic passed,
which must not erase the earlier reset from qualification.

This follows the separately recorded 02:20:32 fatal on the same image after
another AP stop/restart. It is not the already corrected nonempty aggregate
retirement watchdog. The first raw observer reported 207 dynamic-variable
drops during the high-rate UDP phase; its complete-event negative claims and
data-result totals are therefore not used. The positive frame/submission,
fatal-entry and ring-count records above remain available, alongside the
independent firmware error log. A clean repeat is required for publication.
Both fatal dumps have the same type `0x22CE`, PC `0x26294`, source line
`0x5e` and error data `0x000000ff0000005e`.

## Reference contract and correction

Intel's pinned Linux v6.12
[DVM TX producer](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/net/wireless/intel/iwlwifi/dvm/tx.c)
selects `ctx->bcast_sta_id` for **all non-data frames**, explicitly to avoid
breaking aggregation. Its basic builder separately supplies receiver ACK,
frame type/sequence and timeout behavior; its crypto builder supplies the
inline key. Existing station materialization does not change that choice.
This is the same Intel DVM transport contract, not a station-number layout
inferred from Broadcom's different firmware.

The local AP RX BlockAck addition had changed every management submission
to use a materialized receiver's station ID. A later BAR correction restored
broadcast ownership only for control frames, leaving management frames on
the data station. The current correction makes the raw non-data method use
PAN broadcast ownership throughout, before and after client materialization.
Unicast receiver addresses, ACKs, BAR rate selection and PMF authorization,
inline key, CCMP PN and security flags are unchanged. Data frames still use
their own per-client station and aggregate context. IWM/IWX transports are
not assigned IWN station numbers.

## Verification gate

The regression compiles the complete production management/raw submission
methods with actual Intel wire structures and frame constants. Only packet
allocation, client lookup and external DMA/MMIO/scheduler I/O are substituted.
It exercises authentication, association, probe and Action responses before
and after client materialization, both bands, protected/unprotected frames,
ring wrap and BAR ownership. Rejections cover absent/unauthorized PMF peers,
PN exhaustion, stop-in-progress, full rings and allocation failure without
publishing DMA or consuming a PN. The unchanged predecessor compiles and
fails the non-data station assertion.
The corrected ASan/UBSan test passes; the unchanged `d5dc789a` method
independently compiles and exits 134 at that assertion. Existing full payload,
RX/TX A-MPDU, PMF/SAE, client-materialization, multicast and AP-stop retirement
checks also pass. The raw-method test is included in the full payload runner.

The change is not yet runtime-qualified. Required evidence includes actual
Association Response completion through broadcast ownership on the newly
loaded image, repeated WPA3 AP stop/restart after pending TX pressure,
native open/WPA2/WPA3 DHCP/traffic, real S3 and clean complete observers/serial
audit. The firmware's internal assert has not been decoded, so the reference
correction is not alone proof that every restart failure is repaired.
Primary missed-beacon continuity and the full GUI/profile matrix remain
separate required work. No new release is claimed at this checkpoint.
