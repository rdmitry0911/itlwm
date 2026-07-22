# Tahoe IWX PMF/BIP bounded runtime protocol

## Scope

`run_tahoe_iwx_pmf_bip_runtime.sh` is a future runtime gate for the IWX
PMF/BIP ownership layer. It is not runtime evidence by itself and does not prove pure SAE.
It does not publish a release. Its only permitted target is a
fresh disposable overlay of the pinned Tahoe QEMU guest after the exact
candidate has passed private AuxKC admission, transactional activation,
guest-only reboot, and loaded-identity binding.

The runner does not accept a password or a network name. It uses one existing
saved-profile autojoin path, a fixed host-key pin, and the restricted private
trace client path. It never changes a guest address, route, DHCP state,
default route, direct kext state, guest reboot state, or the physical
validation host.

The fresh-overlay precondition is prepared separately by
`tahoe_prepare_disposable_overlay.sh` and its local-only receipt. That helper
accepts only one direct qcow2 backing image, creates no guest activity, and is
not evidence of candidate activation or PMF/BIP behavior. The AP preflight
must still pass before any later candidate sequence begins.

The runner arms its trace cleanup before it submits reset, so a failed or
interrupted reset can only leave an attempted final-off operation, never an
unowned enabled capture. This remains a diagnostic safety boundary, not
runtime PMF/BIP evidence.

## AP transition boundary

The host helper is the sole component allowed to control the lab AP process.
Before it stops the optional-PMF process, it validates the exact process,
configuration shape, a hash-only configuration-pair invariant, channel-width
runtime state, and a hash-only host network invariant. It then writes
restricted temporary state and a one-at-a-time marker, and requires a separate
rollback watchdog to acknowledge that exact marker-bound state and its actual
PID before the first process transition. It then re-reads the same hash-only
network and configuration-pair signatures and re-attests that exact watchdog
receipt immediately before that transition; a changed, unreadable, exited, or
replaced predicate retains optional PMF and makes the run inconclusive without
starting required PMF.

The state directory itself must be canonical, owned by the invoking user, and
mode `0700`; the helper rejects a shared or writable-by-others namespace before
it writes state, launches a watchdog, or changes an AP process.

The same hash-only configuration-pair baseline is persisted in restricted AP
state and carried into required promotion.  A changed or unreadable pair after
the optional-PMF stop prevents required-active publication and is never used to
restart optional hostapd.  The helper quiesces an already required process,
retains marker/watchdog ownership, and permits a later normal rollback only
after the original staged pair is restored; bounded rekey is likewise blocked
while that pair differs.

Rollback repeats that state-bound pair check after optional-PMF restoration and
its final process checks, re-reads the original hash-only host-network
invariant, then repeats the pair check at the ownership/receipt edge before it
re-attests the exact optional PID/AP shape and releases marker/watchdog
ownership or writes `rollback_verified=true`.  Thus a post-restart file,
network, or optional-process change cannot be represented as verified recovery.
Before it begins any rollback process mutation, the helper also requires its
restricted `rollback.status` completion-witness path to be absent and
non-symlinked.  A blocked target retains the required transaction and its
marker/watchdog rather than failing only after ownership has been released.

After a required-PMF process launch succeeds, the helper re-attests that exact
PID and the pinned AP channel/width, re-reads the original hash-only host
network invariant, and re-attests the exact marker-bound rollback watchdog.
It then repeats the exact required-PID/AP-shape predicate immediately before
it promotes rollback state to `required`; an exited, replaced, unpinned,
unreadable, host-network-divergent, or ownerless process transition is restored
to optional PMF through the established transition recovery and never emits a
required-active result.

If any failure occurs after the first optional-PMF process stop, recovery also
compares the post-restore hash-only host-network signature with the state
baseline and repeats the state-bound configuration-pair comparison before it
cancels the watchdog or clears the marker.  A mismatch keeps that rollback
owner armed for a later verified rollback; pre-stop rejections, where no AP
process changed, retain their lighter cleanup path.

Neither the pinned AP shape nor that host-network signature proves hostapd
ownership.  Immediately before any rollback witness or marker/watchdog release,
the helper re-attests the exact optional-PMF PID/configuration and the pinned
AP shape; post-transition recovery repeats that attestation after its network
comparison.  A generated or real process loss at either edge withholds the
witness and retains recovery ownership for a later explicit rollback.

`rollback_verified=true` is a transaction-completion receipt, not an
intermediate AP-restoration observation.  The helper writes it only after the
applicable watchdog disposition and marker removal succeed.  Thus the runner
may safely consume a watchdog-written receipt during cleanup without treating
a failed ownership release as verified rollback.

The watchdog retries restoration to optional PMF if the caller disappears or
if an intermediate AP transition cannot be verified. The helper has no
address, route, NAT, forwarding, DHCP, firewall, service-manager, or reboot
operation. Its output is categorical; configuration values, credentials, and
wireless identifiers are never rendered.

The only post-initial stimulus is hostapd's canonical `REKEY_GTK` control
command sent through its raw control transport. For an IEEE 802.11w-enabled
group state machine, that standard rekey path rotates the alternate IGTK slot
as well as the GTK; a lower-case CLI alias is not used because packaged client
builds need not expose it. The helper compares its hash-only host-network
baseline immediately before and after that command; a changed signature blocks
the stimulus or makes it inconclusive before any success can be reported.
It also re-attests the exact marker-bound required hostapd process immediately
before the raw command and immediately after its `OK` acknowledgement, and
re-attests the independent marker-bound rollback watchdog before that raw
command.  It repeats the required-process/AP-shape check after the final
post-ack network comparison, after re-attesting the staged configuration pair,
and then re-attests the independent watchdog before success publication.  A
dead, replaced, configuration-divergent, or ownerless process state at any of
those edges is inconclusive and never receives a rekey success witness.
After that final pre-command attestation, the helper writes a restricted
one-shot request receipt immediately before the raw command.  The receipt
consumes the sole permitted stimulus even when acknowledgement or a later
postcondition is inconclusive; only a separately written success witness may
support the runtime result, and no retry is allowed for that AP transaction.
The sealed categorical evaluator admits exactly one opposite-slot transition
after the initial selected IGTK slot. A second cross-slot transition in that
same capture is ambiguous with respect to the one permitted request and is
therefore inconclusive rather than a PMF/BIP success.

The required configuration must represent the same saved-profile identity and
credential as the optional configuration. A mismatch is a precondition failure.
The current read-only preflight reports the categorical
`ssid-pair-mismatch`, so no PMF-required transition or candidate runtime run
is authorized at this checkpoint. Resolving that lab prerequisite requires a
separately reviewed configuration/profile plan; the runner must not bypass it.

## Ordered runtime gate

When all external prerequisites are satisfied, the gate has one bounded
sequence:

1. Bind the exact loaded candidate before the experiment and snapshot the
   existing management/default-route, direct-lab-route, and lab-address
   invariants.
2. Confirm the saved profile and optional-PMF AP preflight, reset the safe IWX
   trace, and require one acknowledged bound capture generation with an empty
   snapshot and buffer.
3. Turn the radio off, re-check every route/address invariant, activate the
   required-PMF AP through the helper, then turn the radio on for saved-profile
   autojoin only.
4. Require a live initial active prefix: PMF receive, q0 doorbell/completion,
   post-acknowledgement IGTK publication, matching selected slot, and
   port-valid in one active capture episode. The snapshot and progress reads
   must both name the same nonzero active episode and exactly one episode.
   This initial active prefix does not establish final success;
   it only authorizes the next bounded stimulus.
5. Require the bounded local traffic probe and all invariants before asking
   the helper for exactly one group rekey.
6. Seal the trace, require two identical frozen reads and exactly one sealed
   cross-slot rekey verdict, disable trace control, restore optional PMF, and
   re-check recovery/invariants and loaded candidate identity.

The runner treats its restricted AP state directory as rollback ownership even
if interruption occurs in the small interval after the external activation
returns and before a local success flag is stored. Cleanup first accepts a
watchdog-written rollback witness, otherwise requests the same marker-bound
rollback immediately; only after that attempt does it restore a still-pending
guest radio-on state. A final `PASS` requires both an explicit rollback-attempt
witness and verified optional-PMF restoration.

Every missing acknowledgement, changed binding, trace drop, unsealed capture,
failed rollback witness, or route/address deviation produces an inconclusive
result. No generic association, ping, or initial-only trace may be promoted to
a PMF/BIP success claim.

## Evidence boundary

The runner leaves raw trace, hostapd, interface, and route output local-only in
its fresh output directory. Its JSON attestation carries only exact candidate
digests/UUID, categorical trace counts/verdicts, booleans, and non-claims. It
must contain no wireless identity, address, route, hardware address,
credential, packet, or raw capture.

`test_tahoe_iwx_pmf_bip_runtime_evidence_contract.sh` accepts `PASS` only if
all of these are true: before/after identity binding, saved-profile preflight,
required-PMF activation and verified rollback, preserved network invariants,
the initial prefix and traffic-before-rekey gate, one bounded group rekey,
exactly one resulting sealed IWX cross-slot transition, and an intact trace
verdict. It otherwise accepts only an
inconclusive record with an explicit failure phase.
