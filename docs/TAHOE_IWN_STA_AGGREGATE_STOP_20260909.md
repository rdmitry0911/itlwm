# STA aggregation teardown and retained descriptors — 2026-09-09

## Runtime boundary and evidence limits

The USB-network-free IWN/6235 guest loaded `6433e0d3`, UUID
`29FD701D-9A8E-3D53-997F-3E71612750BF`. A repeated role-7 AP start overlapped
a real background WCL/SAE retarget. A later watchdog reset reported queue 10
with `queued=1`, `cur=29`, `read=29`. The nearby diagnostic label says PAN,
but the PAN management queue itself had zero pending descriptors. It is not
evidence that the PAN management queue hung.

Bounded, read-only FBT on the same loaded image followed ordinary credentialed
selection through WCL leave, net80211 DELBA and the actual STA stop callback.
At 20:07:51, 20:08:05, 20:08:18 and 20:08:31 UTC, the callback asked reclaim
to advance beyond the submitted interval. Reclaim returned false, after
which the backend still rebased the cursors. These particular observed stops
had zero pending descriptors; they do not independently reproduce the earlier
one-descriptor watchdog. The queue offsets came from this build's DWARF.
The corrected observer interprets the C++ bool return as its low byte; an
earlier observer printed undefined upper return-register bits as an integer.

## Cause of the demonstrated invariant failure

The inherited stop callback used `ba_winend` as its reclaim target. That is
the logical Block Ack window limit, not the exclusive end of submitted TFDs.
The later transport ownership guard correctly rejects such an out-of-range
target. The stop callback ignored rejection and the scheduler backend reset
both cursors, abandoning any remaining descriptor and its queued count.

The [OpenBSD IWN source](https://raw.githubusercontent.com/openbsd/src/master/sys/dev/pci/if_iwn.c)
contains the inherited logical-window teardown, without this port's bounded
reclaim guard. Intel's
[PCIe transport](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/net/wireless/intel/iwlwifi/pcie/tx.c)
instead deactivates a queue before unmapping its actual read-to-write
descriptor interval. Its normal
[DVM aggregation stop](https://raw.githubusercontent.com/torvalds/linux/master/drivers/net/wireless/intel/iwlwifi/dvm/tx.c)
also distinguishes an outstanding hardware drain from a completed DELBA.
These are Intel transport references, not a claim that Apple's different
hardware uses IWN queues or exposes the same ownership fields.

Both 4965 and 5000-family stop backends now deactivate scheduling, reclaim
through the current software write cursor, and only then rebase cursors.
The upper callback no longer frees packets before obtaining NIC access.
The existing completion ownership guard, aggregation capability and per-packet
cleanup remain intact. There is no watchdog suppression, forced queued-count
clear, fabricated completion or aggregation-disable workaround.

## Regression scope

`test_iwn_sta_aggregate_stop.sh` compiles the actual upper stop, both scheduler
backends, reclaim guard and reclaim loop under ASan/UBSan. Hardware access
and resource release are observed substitutes. Tests cover one pending MPDU
in a larger logical BA window, wrapped rings and sequence numbers, empty
queues, a logical end short of the physical exclusive end, NIC-lock failure,
and repeated teardown. They assert deactivation before resource release,
exactly-once packet/node release and zero abandoned descriptors before rebase.
The old `6433e0d3` methods fail the one-pending-descriptor assertion; the
corrected methods pass. Invalid completion targets remain rejected.

Existing STA DVM, three-family AP aggregation/watermark, AP multicast DTIM
and BSD carrier regressions pass. The STA DVM test's AP helper extraction
was corrected to select its function definition, not an earlier call site;
its previous false failure did not inspect the intended method.

Build, loaded-image validation and on-air repeated APSTA/reconnect/S3 checks
are still required. This source fix does not yet establish that the entire
earlier watchdog reproduction, public reconnect matrix or IWM/IWX hardware
surface is closed. Public release remains held.
