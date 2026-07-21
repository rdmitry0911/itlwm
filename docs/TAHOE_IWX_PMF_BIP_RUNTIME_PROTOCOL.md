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
configuration shape, channel-width runtime state, and a hash-only host network
invariant. It then writes restricted temporary state, a one-at-a-time marker,
and a rollback watchdog before the first process transition.

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
6. Seal the trace, require two identical frozen reads and the sealed
   cross-slot rekey verdict, disable trace control, restore optional PMF, and
   re-check recovery/invariants and loaded candidate identity.

The runner treats its restricted AP state directory as rollback ownership even
if interruption occurs in the small interval after the external activation
returns and before a local success flag is stored. Cleanup first accepts a
watchdog-written rollback witness, otherwise requests the same marker-bound
rollback immediately. A final `PASS` requires both an explicit rollback-attempt
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
the initial prefix and traffic-before-rekey gate, one bounded group rekey, and
an intact sealed IWX cross-slot verdict. It otherwise accepts only an
inconclusive record with an explicit failure phase.
