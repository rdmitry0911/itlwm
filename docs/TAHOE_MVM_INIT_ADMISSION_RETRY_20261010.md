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

Production `685f388029af42404c6b4531fa9609349013e65e` is pushed, built,
transactionally installed and loaded in the existing IWM 9260 guest. Source
identity is `80ecde86eee8`; all 1088 BootKC imports resolve. The exact committed
macOS tree repeats the lifecycle, ready and failure fixture results above.
Private admission and the canonical and AuxKC member sets pass. Four companion
kexts and rollback material remain under
`/private/var/tmp/aiam-iwn-activation-iwm9260-initowner685f3880-20261010`.
Activation is `activation-20261010T081527Z`.

- Loaded UUID: `D50DEB81-94E7-3FB9-81F4-CC8BCEE424EC`.
- Mach-O SHA256: `1f1fd888bb55102d6025045e3bb49778fbb60193cf54807218532c284f1b61aa`.
- Boot: `DD112E64-7417-4D93-AD3A-F3F3F55854E5`.

The build, installed bundle and extracted archive match completely. The
guarded guest reboot returns SSH exit 255; the same QEMU is observed through
temporary banner timeouts until the new boot and exact image are verified.
The private monitor, overlay and management default through en2 remain
unchanged. The original dirty checkout and physical host `.22` are untouched.

The first bundle fetch requested a branch name although this bundle exports
HEAD; it failed before changing the checkout. The corrected fetch uses the
actual exported ref. The activation wrapper returns one despite its final
success marker; an independent read verifies the READY transaction, both
member sets and complete installed equality before the guarded reboot. The
existing activation is not replayed.

## Real sleep and native controls

Actual S3 is confirmed independently by `paused (suspended)` and serial
`ACPI SLEEP`. The initial observation window ends before the guest suspends;
the same VM is then reobserved and awakened only after suspension is proved.
Current boot power history records one 32 second sleep, WakeTime 1.222 seconds
and a 30 second WindowServer sleep notification timeout. Older history belongs
to earlier boots. The same boot, loaded image, installed equality and en2
management survive private `system_wakeup`.

The first FBT trace observes actual Off, stop ownership, drain, device erase
and successful Off in 665.36 ms, but its wrapper ends with one and does not
cover the subsequent controls. That result remains distinct from the separate
native trace, whose explicit remote and local process results are zero and
whose ready and complete markers bracket four repeated Off and On controls.
It records real inner POWER On refusal `0xe00002d8` in 1.899, 6.941, 2.197 and
2.138 ms. Off readbacks and management remain intact in both sets of four
native controls. One control wrapper also ends with one despite its final
assertion marker; it is not counted as a clean process exit.

At boot the native logical power getter initially says On while the interface
is inactive and the lower worker reports `fatal=2`. This is not a successful
radio start. After native Off, repeated On remains refused. RFKILL prevents
actual full init and retry claim entries in the completed hardware trace, so
the software collision result is not represented as hardware execution.

## Separate LAB release artifact

Release `v2.4.0-alpha` has additional asset `627421593`,
`AirportItlwm-Tahoe-IwmIwx-InitOwner-685f3880.kext.zip`, 15,713,687 bytes,
SHA256 `36f5ea1fc716a1cd33706f5ae5924d24e30891746cd80cee45d7e5d812c3cdaa`.
It is the exact installed unsigned Debug bundle. SCP reaches terminal zero
before local size and hash checks and upload.

Independent API reads verify the new asset and all nine older assets with
immutable metadata unchanged. The complete preceding notes remain byte equal
as the suffix and the default archive is unchanged. The new label and notes
retain LAB ONLY, IWM RFKILL and software only IWX limits.

## Operational qualification boundary

Successful GUI, saved network recovery, open, WPA2, WPA3, DHCP, traffic and
AP remain unqualified on this RFKILL blocked 9260. IWX has executable software
coverage, not hardware runtime coverage in this cycle. Dequeued IWM workers,
raw task producers and deferred SAE hook retirement remain separate lifetime
work; this change does not establish full driver equivalence.

Evidence is retained in the current laboratory root under
`init-retry-owner-*` and `init-owner-receipt-*`, including current Linux and
macOS controls, historical intended failures and passing historical controls.
`init-owner-*` contains the exact committed build, activation reconciliation,
loaded image, sleep and wake, native traces, controls and archive verification.
