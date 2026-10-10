# IWX SAE terminal callback keeps its lifecycle owner

The October 10 bounded audit resolves two stale source-contract failures
without changing the production dispatcher. The existing `d2d8f1d8` lease
retention is necessary: the dispatcher still accesses its owner after
delivering a terminal event. Moving the leave earlier would restore a
lifetime race, not fix a workloop deadlock.

## Executable evidence

`scripts/test_iwx_sae_terminal_lifetime.sh` extracts the complete current
`iwx_sae_tx_task_dispatch`, task-gate close/enter/leave/drain/enqueue bodies,
ticket-domain cancellation, native callback CAS admission/close/drain/open,
fallback join retirement and retirement wake. It also executes the complete
controller transport callback helper and value-copy mailbox producer.

The explicit host boundaries are IOKit locks and interrupt delivery, taskq
enqueue, the event router, native engine terminal queueing and the generic
join cleanup callback. The test does not execute controller detach, firmware,
descriptor DMA, PMF crypto or an on-air SAE exchange.

Each run uses 160 controlled concurrent schedules: 32 repetitions of stop
during mailbox notification, fallback retirement, retirement wake and task
enqueue, plus native callback close/drain/reopen. The stop side calls the
actual task lease close/drain or actual native callback close/drain. It must
not reach its modeled reclaim boundary until the admitted producer returns.
At enqueue, the shared admission lock blocks close itself; the other task
tail cases require an actual active-owner drain wait. No timing-only sleep
is used to decide whether reclaim was prevented.

Additional cases cover legacy mailbox value equality, direct private tickets,
native admission already closed, canceled and malformed records, a missing
event handler, engine-consumed legacy tickets, zeroed FIFO slots, fallback
join-generation retirement, remaining FIFO requeue and closed task admission.

The current source passes on Linux and macOS with ASan/UBSan, including the
final complete macOS aggregate at `5f351162`. Replacing only the full
dispatcher with its historical `d2d8f1d8^` body compiles, then fails exit 134
at `stop reclaimed the owner before terminal tail returned` on both systems.
That control
runs the callback race first, before checking the later-added retirement
helper, so it does not fail merely because that helper was absent historically.

## Contract correction

The old transport and resident-owner source checks demanded
`iwx_task_gate_leave` before `ic_event_handler`. The task gate is an active
lifecycle-owner counter: its IOLock is already released by enter. It is not
AirportItlwm's command gate. The corrected checks require the owner to cover
callback, payload scrub, retirement and requeue/wake through the final leave.
Both invoke the executable fixture and are now in `test_payload_builders.sh`.

The controller producer must still avoid synchronous command-gate entry.
The source checks enforce the SAE event-handler fast return before main-gate
handling and forbid `getCommandGate` or `runAction` in the mailbox producer.
The actual producer copies a bounded event and signals its private source;
the later workloop action owns semantic processing. Retaining the lower
owner does not authorize a callback to synchronously wait on the main gate.

## Qualification limit

These are software lifetime checks, not a new functional driver feature.
The 9260 remains hardware RFKILL-blocked, and it is an IWM rather than an IWX
device. Physical IWX execution, repeated GUI open/WPA2/WPA3 joins, saved
networks, DHCP/traffic, AP and post-sleep service remain separately unqualified.
