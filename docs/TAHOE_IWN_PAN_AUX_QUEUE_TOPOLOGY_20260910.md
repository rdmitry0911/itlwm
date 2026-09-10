# PAN AUX versus primary aggregation queue topology

## Evidence and correction

The same-card native Linux pressure/restart control is recorded in
`TAHOE_IWN_AP_RESTART_LINUX_CONTROL_20260910.md`. Its queue observer retains
queue 10 as fixed AUX and uses queue 11 for primary aggregation. The guest's
existing PAN post-alive initialization also assigns queue 10 to AUX/FIFO 5,
but the family attach default leaves `first_agg_txq` at 10. Primary 5000
aggregation start and stop additionally hardcode `10 + tid`. Earlier guest
fatal dumps independently show primary traffic using queue 10.

After each successful firmware read, initialization now selects command and
first-aggregate queues together from the same hardware, EEPROM and firmware
PAN admission. PAN uses command 9 and first aggregate 11; non-PAN 5000 uses
4 and 10; 4965 retains 4 and 7. Selecting a later non-PAN image clears a
previous PAN topology. The existing BA-owner and aggregate-mask reset occurs
before this selection, and hardware initialization occurs afterwards.

The complete primary 5000 start and stop methods now use that shared dynamic
boundary. Data submission, TX completion, compressed BA, generic BA ownership,
retirement and AP/STA stop isolation already consume the same field. This
avoids changing a hardware queue without changing its completion/TID owner.
Attach provisions DMA for the complete original family range, so the runtime
boundary does not leave newly selected queues unallocated. Reset/free retain
ownership of the provisioned rings, including the now nonaggregate AUX ring.
The distinct 4965 and non-PAN layouts and the IWM/IWX backends are unchanged.

This does not claim to fix a separate possible asynchronous AP queue-reservation
race. Primary start already rejects an occupied aggregate queue; it is not
accurate to describe that path as unconditionally overwriting an active AP.

## Production-method regression

The new topology test compiles the complete production selector, generic STA
start and 5000 scheduler start under ASan/UBSan with nonrecovering diagnostics.
It covers all hardware codes 0..31, both capability inputs and stale-topology
replacement, then PAN/non-PAN starts for all eight TIDs, sequence wrap, five
window sizes, occupied-queue rejection, unchanged neighboring SRAM/rings and
queue ownership before link-quality publication. Actual register definitions
and the firmware PAN enum value are extracted from production headers.

The existing complete production STA stop/drain test now covers aggregate
bases 7, 10 and 11, nonempty and wrapped physical intervals, NIC-admission
failure/retry, four-word SCD status retirement and preservation of neighbors.
It retains the different 4965 stop behavior. Both tests run in the full
payload suite. Adjacent AP RX/TX A-MPDU, client-materialization, backpressure
and STA DVM scheduler regressions pass as well.

Unchanged `a5fe4673` start and stop consumers compile independently and each
fail the wrong-queue assertion (exit 134). The start negative control uses
the current topology producer as its fixture; it proves that the old complete
consumer cannot honor PAN layout, not that old initialization called the new
producer. The test harness's initial out-of-range intermediate pointer
arithmetic was corrected before qualification; clean reruns have no sanitizer
diagnostics. Earlier diagnostic-contaminated runs are not passing evidence.

## Runtime and release gate

This is an implemented source correction, not yet a loaded-image result or
a decoded explanation of firmware assert `0x22CE`. The next gate is the exact
built/loaded candidate's nonempty AP pressure stop and repeated client joins,
with observed primary/AP queue identities and no hidden firmware reset.
Native open/WPA2/WPA3 DHCP/traffic and actual APSTA S3 recovery must follow
before release promotion. The previously qualified `893a3114` public kext
remains unchanged until that runtime gate passes.
