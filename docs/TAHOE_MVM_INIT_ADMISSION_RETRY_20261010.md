# IWM and IWX recovery attempts require actual init admission

IWM and IWX now charge the recovery budget only after a lower firmware init
epoch has been admitted. A duplicate worker refused while another init owns
the device no longer exhausts recovery or clears the live power request.
This repairs a software race in repeated activation and recovery. Successful
GUI joins and on air traffic remain separate hardware qualification.

## Reproduced duplicate init failure

The complete historical init and worker bodies reproduce the same failure
in both families. One actual init retains ownership while its firmware double
is paused. Duplicate workers consume five recovery attempts despite starting
no additional firmware initialization, emit one exhaustion event, requeue
four times and clear request epoch 42. The original init then succeeds but
publishes ready with request epoch zero.

The IWX case executes the real init task dispatcher, bootstrap token,
task gate, init epoch and q0 sender lifecycle. Historical requeue rearms the
bootstrap token while the original init still owns its epoch; this is not a
fixture that bypasses dispatcher admission. IWM uses its actual radio init
owner and worker. Both cases release and join the original init before
checking the assertion.

Historical Linux source `07259bd8` and historical macOS source `a1c778a3`
compile, then fail the intended collision assertion with exit 134. Four
historical controls pass for each family on both platforms: normal init,
five genuine EIO failures, five genuine ENXIO failures and two genuine
failures followed by success. These controls distinguish duplicate admission
refusal from an actual lower failure with the same errno.

## Lower admission witness

`iwm_init` and `iwx_init_internal` accept an optional synchronous output
that is false on entry and becomes true only after their actual lower init
owner is acquired. The worker passes its local `attempted` flag through this
boundary rather than setting it before the call. Existing nonworker callers
retain the default null output and unchanged return semantics.

Refused ownership still returns ENXIO. An admitted init that fails with
ENXIO still consumes one recovery attempt. The five attempt policy, one shot
failure mailbox, successor request CAS and successful request completion are
unchanged. There is no errno filter, additional global owner, retained stack
pointer, fabricated ready event or RFKILL bypass. Admission does not imply
that firmware or MMIO has successfully run.

The 25C56 reference powerOn listing separates an early Busy return from its
recovery counter increment and lower call; the counter compares against four
before incrementing. The existing reference boundary is described in
`TAHOE_MVM_RADIO_ACTIVATION_FAILURE_20261009.md`. This change preserves bounded
real lower attempts in the Intel owner model rather than importing private
BCM flag meanings or selectors.

## Executable verification

All eleven complete IWM lifecycle controls and fifteen complete IWX lifecycle
controls pass ASan and UBSan on Linux and macOS. The collision now starts
firmware once, leaves retry and failure counts at zero, performs no self
requeue, retains request epoch 42 and publishes ready for that same request.
Both genuine five failure controls retain their exact exhaustion behavior;
eventual success resets the budget and completes only its own request.

The extracted production functions include complete init, stop, worker,
request and retry ownership. IWX additionally executes its actual dispatcher,
task gate and q0 lifecycle. Kernel lock services, firmware, task queue
scheduling, net80211 state and ready production are explicit doubles. This
checks production control flow and ownership, not packet transmission or
hardware firmware behavior.

Thirteen full init and ready controls for each family and both complete
activation failure suites pass on both platforms. The latter retain explicit
firmware admission doubles; real init admission belongs to the lifecycle
fixtures above. The Linux payload aggregate and physical scan, standard scan,
power off link down, resident SAE, AP reset replay, software PMF and IWX init
epoch contracts also pass. The broader raw task producer q0 contract remains
outside this qualification.

## Candidate qualification

The source and fixture checks above are complete. The candidate has not yet
been built, installed or loaded. The running IWM 9260 guest still uses
`a1c778a3` and retains management through en2. Its hardware RFKILL prevents
successful open, WPA2, WPA3, DHCP, traffic and AP qualification. Loading this
fix and checking real sleep and bounded native controls is the next release
step, not proof that the collision executes on hardware.

Evidence is retained in the current laboratory root under
`init-retry-owner-*` and `init-owner-receipt-*`, including current Linux and
macOS controls, historical intended failures and passing historical controls.
