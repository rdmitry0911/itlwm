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

## Exact-image runtime: topology corrected, restart still fails

Source `b91bb5a5` built successfully with all 1085 external symbols resolved.
Private five-member AuxKC admission and transactional activation passed,
preserving the four companion members. The 04:15:43 UTC boot loaded UUID
`67773F1E-126D-3368-B486-5DBB7D4F1DF5`, matching frozen/installed Mach-O
SHA-256 `cbb6dfb433f11c5f68983673e728069f8ce0fe2288fbc9d59027afaa83066fcb`.
The saved primary recovered its DHCP address, passed 5/5 packets, and the
independent management upstream passed HTTP.

The first concurrent role-7 AP admitted the external AX211 with SAE group
19 and required PMF/BIP. Client-to-AP 20/20, isolated cold-neighbor reverse
10/10 and primary 5/5 passed. These role-7 checks use static test addresses,
not a new DHCP qualification. The AP's real ADD_STA/BA terminal selected
queue 12, not the primary queue or fixed AUX.

At 04:18:34 UTC, ordinary public AP stop during AP-originated UDP pressure
retired 224 submitted descriptors from queue 12 to zero. The read-only
observer independently recorded its four-word SCD clear at `0x80d4e0`.
After a separate 15-second dwell, primary traffic passed 10/10.

The next AP start reached the lower at 04:19:29, but firmware asserted at
04:19:46: type `0x22CE`, PC `0x26294`, line `0x5E`, data
`0x000000FF0000005E`. The last management submission was Association
Response, queue 7/index 57/station 14; its TX_DONE is present before the
fatal. The independent serial dump instead has one pending AP data descriptor
on queue 5, zero on queue 7, zero on fixed AUX queue 10, and zero on both
primary queue 11 and retired AP queue 12. Hardware stop and automatic AP
replay followed. Subsequent 20/20, cold 10/10 and primary 5/5 demonstrate
recovery after a failure, not successful reset-free AP restart.

The complete observer was stopped by its exact owned PID after cleanup and
ended at 04:22:49 UTC with zero diagnostic errors, one fatal and empty stderr.
Its aggregate submission census records 107 on primary queue 11/station 0
and 16619 on AP queue 12/station 2, with none on queue 10. Actual primary
scheduler-start register writes independently select queue 11 after recovery.
Normal final AP stop retained primary traffic at 10/10; the temporary AP
address was removed and the host's ordinary Wi-Fi profile restored, with
wired management unchanged.

Thus the shared topology defect is corrected on the loaded image, but it is
not a sufficient cause of the persistent firmware lifecycle failure. The
candidate is not released or labeled as a completed WPA3/AP restart fix.
The next comparison must cover the full recorded PAN station/context teardown
and reuse sequence, rather than repeating the now-disproved AUX explanation.
