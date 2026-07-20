# 2026-07-20 — laboratory runtime-harness integrity

## ANOMALY

- id: `LAB-A2DF-HARNESS-SCOPE-20260720`
- status: `CONFIRMED_DEVIATION`
- symptom: the first no-route-mutation A2DF harness could accurately state that it issued no explicit state-mutating DHCP, address, or route command, but its evidence labels asserted the stronger fact that no such mutation occurred.
- first visible manifestation: independent static review of the uncommitted runner after its first four-cycle baseline run.
- expected system behavior: a safety harness must claim only properties it observes or enforces, identify the guest before it changes radio state, avoid arbitrary privileged guest code, preserve a fresh evidence directory, and restore radio power if a run fails while it is off.
- actual behavior: the runner permitted an arbitrary `JOIN_HELPER`, trusted a localhost forwarding endpoint without an SSH host-key/guest-identity pin, permitted an existing output directory, lacked an EXIT recovery path, and used overly broad `routing_mutation=none` / `ip_address_mutation=none` labels.  Radio power transitions can themselves trigger OS-managed association, DHCP, address, or route activity.
- divergence point: `scripts/run_tahoe_lab_radio_gates_no_route_mutation.sh` input, transport, evidence-label, and failure-cleanup paths.
- evidence:
  - runtime: `/home/dima/Projects/aiam/runtime-captures/itlwm-3cd3-a2df-no-route-mutation-20260720T061821Z/summary.log` passed four baseline cycles but establishes only gate observations;
  - independent review: `/root/runner_review` identified the five scope gaps above;
  - lab identity observed read-only: host-key fingerprint `SHA256:4Q/9OkSwSE09YhXRdAbdbPl7WTqRNJHyn+vAM6p8QiY`, build `25C56`, model `MacPro7,1`, management MAC `52:54:00:c9:18:28`.
- candidate causes:
  - confirmed: evidence scope exceeded the direct commands and persistent values actually checked;
  - confirmed: arbitrary helper could violate the scope despite shell-path sanitization;
  - confirmed: no restoration path exists after an error while Wi-Fi is off.
- rejected causes:
  - no driver functional regression is inferred;
  - no interaction with `10.90.10.22` occurred in the executed baseline run.
- confirmed deviation: this is a harness/evidence integrity deviation, not a claimed AirportItlwm reference-behavior deviation.
- root cause: the runner was designed around command prohibition rather than explicit guest identity and observed network-invariant contracts.

## FIX_CANDIDATE

- anomaly_id: `LAB-A2DF-HARNESS-SCOPE-20260720`
- symptom: the runner can overstate its network-safety evidence and leave the laboratory radio off after an abnormal exit.
- expected system behavior: the runner must only use the pinned QEMU guest, use saved-profile autojoin only, make no explicit state-mutating DHCP/address/route command, capture the observable IPv4/route/DHCP-lease state, and restore Wi-Fi after a failure while off.
- actual behavior: see anomaly above.
- exact divergence point: runner initialization, `JOIN_HELPER`, output setup, evidence labels, and OFF/ON lifecycle.
- evidence from runtime: the initial baseline run used a saved profile and retained `10.77.0.47`, default `en0`, and target `en1`, but this does not prove all OS-managed intermediate activity absent.
- evidence from decomp: not applicable; this is host-side diagnostic instrumentation, not a driver semantic change.
- exact semantic mismatch: an unobserved global-negative claim (`no mutation`) was emitted where the runner only proves a no-explicit-command policy plus selected postconditions.
- diagnostic class: `DIAGNOSTIC_INSTRUMENTATION`
- exact hypotheses being disambiguated:
  - whether an A2DF failure is a driver reconnect/data-plane failure rather than a changed persistent guest network state;
  - whether the command endpoint is the intended QEMU guest before radio control is sent.
- exact probe points: pinned SSH transport; guest build/model/management-MAC identity; complete IPv4 route/address/DHCP lease snapshots before, after each completed cycle, and final; default/target/IPv4/lease invariant assertions.
- why these probe points are sufficient: they bound the claimed persistent network state at every completed cycle while leaving unobservable transient OS work explicitly outside the claim.
- why instrumentation is behavior-neutral: no AirportItlwm, Agent, AP, route, address, or DHCP state is written by the new probes; the only existing state transition remains public `networksetup -setairportpower en1 off/on` on the pinned lab guest.
- what exact runtime evidence must be collected: four completed cycles with AP authorization/fresh-association/data-plane gates, full snapshots, preserved invariant signatures, and final radio-on state.
- files/functions to modify:
  - `scripts/run_tahoe_lab_radio_gates_no_route_mutation.sh`: pin transport/identity, remove helper, make output fresh, add cleanup and invariant snapshots/assertions, narrow labels;
  - `scripts/test_tahoe_lab_radio_gates_no_route_mutation_contract.sh`: enforce those boundaries.
- forbidden alternative fixes considered and rejected:
  - trusting a caller-supplied arbitrary helper;
  - claiming that absence of a command proves absence of all OS-managed transient behavior;
  - accepting an unpinned localhost SSH target;
  - adding a route/address/DHCP command to force recovery.
- verification plan:
  1. shell parse and static contract;
  2. independent structural review and `APPROVED_FOR_AFTER_FIX_RUNTIME` decision;
  3. one pinned-QEMU, saved-profile four-cycle run;
  4. inspect evidence for final radio-on state, unchanged observed IPv4/route/lease invariants, and 20/20 source-bound pings.

## CR-233 STAGE-1 REJECTION AND REVISED FIX_CANDIDATE

- decision: `commit-approval/decisions/COMMIT_DECISION_CR-233.md` rejected the first revised harness before runtime.  The rejection is accepted; no guest was run after that decision.
- confirmed residual deviations:
  - the runner cleared `RADIO_OFF` immediately after an `on` command returned, before observing `Wi-Fi Power (en1): On`, and lacked a final explicit power-on assertion;
  - `AP_IF` was syntax-validated but caller-overridable, so the AP-side authorization observer was not bound to the 5 GHz laboratory AP;
  - the target route was asserted only by interface rather than by a stable gateway/interface signature.

### FIX_CANDIDATE — `LAB-A2DF-HARNESS-SCOPE-20260720-R2`

- anomaly_id: `LAB-A2DF-HARNESS-SCOPE-20260720`
- symptom: a post-OFF failure can skip recovery if `networksetup ... on` returns before the radio is observably on; AP and target-route claims are wider than their pins.
- expected system behavior: on every path after a runner-issued OFF, keep recovery pending until a read-only `networksetup -getairportpower en1` observation says `On`; bind the AP observer to the exact VHT80 laboratory AP; compare the target route's gateway/interface signature at every invariant gate.
- actual behavior: as recorded in the CR-233 decision.
- exact divergence point: `RADIO_OFF` transition, `AP_IF` input, and `guest_target_route_interface()`/assertion paths in the external runner.
- evidence from runtime: the previous four-cycle baseline establishes the lab topology and endpoint values; no post-CR-233 runtime was performed.
- evidence from decomp: not applicable — this remains a host-side evidence-tool correction with no kext source or binary change.
- exact semantic mismatch: the evidence tool asserted completion/identity/configuration more strongly than it observed.
- diagnostic class: `DIAGNOSTIC_INSTRUMENTATION`
- exact hypotheses being disambiguated:
  - whether each completed radio cycle preserves the exact persistent target route, rather than merely selecting an interface;
  - whether AP authorization was observed on the intended channel-153, 80 MHz lab AP;
  - whether unsuccessful runner paths leave the Wi-Fi radio observably on after cleanup.
- exact probe points:
  - bounded power-on observation after every `on` request, inside cleanup, and before PASS;
  - `iw dev wlp0s20f3 info` identity fields: AP mode, expected MAC, SSID, channel 153, width 80 MHz;
  - `route -n get 10.77.0.1` gateway/interface signature baseline and equality gate.
- why these probe points are sufficient: they close each Stage-1 finding at its direct observable boundary without adding an association, route, address, DHCP, kext, or Agent action.
- why instrumentation is behavior-neutral: all new operations are read-only probes except an already-declared cleanup retry that only restores the radio state the runner itself changed; no driver-visible packet, event, callback, or configuration is added.
- what exact runtime evidence must be collected: a fresh four-cycle directory that records AP identity before and after, each requested/observed ON transition, cleanup status if exercised, target-route signature equality, and existing authorization/fresh-epoch/20-ping gates.
- files/functions to modify:
  - `scripts/run_tahoe_lab_radio_gates_no_route_mutation.sh`: add AP pin, target-route signature, bounded radio-on observation, and final assertion;
  - `scripts/test_tahoe_lab_radio_gates_no_route_mutation_contract.sh`: require the new pins and assertions.
- forbidden alternative fixes considered and rejected:
  - clearing recovery state based on command return alone;
  - treating arbitrary AP interface output as laboratory proof;
  - weakening the target-route claim rather than measuring its explicit signature;
  - forcing IP/route/DHCP recovery.
- verification plan:
  1. shell parse, static contract, artifact/hash review;
  2. fresh independent Stage-1 approval on the revised immutable diff;
  3. one pinned-QEMU, saved-profile four-cycle run;
  4. independent Stage-2 review before any commit.

## R2 WITHDRAWN BEFORE RUNTIME; REVISED FIX_CANDIDATE R3

- decision: R2/CR-234 is withdrawn before reviewer approval and before any
  guest mutation.  An independent pre-review found three still-unbounded
  observations in the unrun R2 source.
- evidence:
  - the known-good baseline's `route -n get 10.77.0.1` output has no
    `gateway:` field for the directly connected host route; it contains
    `destination: 10.77.0.1` and `interface: en1`;
  - R2 accepted `ifconfig en1` `status: inactive` as proof of a requested
    radio OFF, although that status can arise with radio power still On;
  - R2 set its cleanup-pending bit only after the OFF SSH invocation returned,
    leaving an execution/transport ambiguity in which the remote OFF could
    apply while the caller sees an error and skips restoration;
  - raw `route`/`ifconfig`/`netstat`/`ipconfig`/`scutil` captures are deliberately
    best-effort evidence (`|| true`), so they cannot be called complete
    assertions.

### FIX_CANDIDATE — `LAB-A2DF-HARNESS-SCOPE-20260720-R3`

- anomaly_id: `LAB-A2DF-HARNESS-SCOPE-20260720`
- symptom: the unrun R2 harness could prove neither a true radio OFF boundary
  nor recovery after an ambiguous failed OFF transport; its direct target route
  signature emitted an empty gateway value while describing a gateway/interface
  contract.
- expected system behavior: after a test-issued OFF attempt, recovery remains
  pending until `networksetup -getairportpower en1` explicitly reads `Off` for
  the OFF gate and later `On` for recovery/completion.  The direct lab route is
  represented as its observable destination, an explicit `nexthop=direct`
  marker when macOS reports no gateway, and interface; raw state snapshots are
  labelled best-effort supplemental evidence only.
- actual behavior: see the R2 pre-review evidence above.
- exact divergence point: `wait_guest_radio_inactive()`, the ordering around
  the OFF transport invocation, `guest_target_route_signature()`, and wording
  that elevated tolerant raw snapshots into complete assertions.
- evidence from runtime: the prior baseline capture provides the actual
  direct-route shape (`destination: 10.77.0.1`, `interface: en1`, no gateway)
  and stable lab topology.  No R2 or R3 after-fix guest run has occurred.
- evidence from decomp: not applicable — this is external host-side diagnostic
  instrumentation, not a driver semantic change.
- exact semantic mismatch: a status-derived/empty-field approximation was
  described as a direct power/route observation, and a possible remote state
  change was not covered by cleanup.
- diagnostic class: `DIAGNOSTIC_INSTRUMENTATION`
- exact hypotheses being disambiguated:
  - whether all four test cycles traverse explicit `Off` then explicit `On`,
    rather than merely a link-inactive interval;
  - whether the direct route to the lab AP remains a direct route on `en1`,
    rather than an accidentally empty gateway field;
  - whether any test-owned OFF attempt has a bounded observed-On restoration
    path even if its SSH response is ambiguous.
- exact probe points:
  - `networksetup -getairportpower en1` must match `: Off` before the AP
    station-absence gate and `: On` before recovery state clears/PASS;
  - `route -n get 10.77.0.1` parses `destination`, optional `gateway`, and
    `interface`, emitting `nexthop=direct` only when no gateway is present;
  - raw state capture headings explicitly say `best_effort` and remain
    supplemental to exact invariant probes.
- why these probe points are sufficient: each value corresponds exactly to the
  boundary now asserted; no unstated full-routing or absence-of-transient claim
  remains.
- why instrumentation is behavior-neutral: all added checks are read-only.
  Marking recovery pending immediately before the existing OFF stimulus adds no
  remote operation; it merely ensures that cleanup can issue the already
  declared idempotent On request if transport outcome is ambiguous.  No kext,
  Agent, AP, route, address, DHCP, credential, packet, or external-host state
  is changed beyond the test's existing lab radio OFF/ON stimulus.
- what exact runtime evidence must be collected: four fresh recorded explicit
  Off/On observations, pinned AP identity, direct-route signature equality,
  existing authorization/fresh-epoch/source-bound traffic gates, and final
  observed On.  If cleanup is entered, record request and observed result.
- files/functions to modify:
  - `scripts/run_tahoe_lab_radio_gates_no_route_mutation.sh`: use strict Off
    observation, set recovery pending before OFF, normalize the direct route,
    and label tolerant raw snapshots best-effort;
  - `scripts/test_tahoe_lab_radio_gates_no_route_mutation_contract.sh`: make
    each correction static-reviewable.
- forbidden alternative fixes considered and rejected:
  - accept link status as a substitute for radio power;
  - infer a gateway that macOS did not report;
  - send any DHCP/address/route/association action after OFF;
  - exercise a deliberately failing live run merely to test cleanup.
- verification plan:
 1. shell parse, static contract, immutable artifact/hash review;
 2. fresh independent Stage-1 approval;
 3. one pinned-QEMU saved-profile four-cycle run;
 4. Stage-2 review before commit.

### R3 ADDENDUM — PRE-OFF BASELINE AND OBSERVER-ERROR BOUNDS

- new evidence: independent static review found that a stale en1 MAC/IP/route
  can satisfy the old preflight while Wi-Fi is already Off.  In that state, an
  ambiguous OFF transport followed by cleanup could turn a radio On that the
  runner had not first established as its active test baseline.  It also found
  that a failing host `iw station get` could be interpreted as station absence.
- expected system behavior: immediately before any recovery-pending bit or
  OFF request, require a read-only `Wi-Fi Power (en1): On` observation and
  AP authorization for the current guest en1 MAC on the already pinned AP.
  A host station-probe execution failure must fail the run, never prove station
  absence or lack of authorization.  DHCP evidence labels must say selected
  signature, not a complete DHCP configuration.
- exact divergence points: runner preflight after `wait_active_client_mac`,
  `station_present`/authorization observers, and selected-DHCP labels.
- added hypotheses: (1) the first OFF is a real reconnect edge from a known
  On/authorized baseline; (2) an apparent AP absence is an actual station
  observation, not loss of host observer authority.
- exact probe points: read-only `networksetup -getairportpower en1`, pinned
  AP station-dump parsing with explicit probe-error propagation, and the same
  selected DHCP field-signature equality used by the invariant gate.
- behavior neutrality: all added probes and local fingerprint checks are
  read-only.  They add no guest radio, association, credential, packet, route,
  address, DHCP, kext, Agent, AP, or external-host action.
- files/functions to modify: the same R3 runner/contract only; no driver code.
- verification: static parse/contract plus an independent Stage-1 review must
 complete before any R3 guest run.
- R3 packet note: CR-235 was withdrawn before a decision and before guest
 runtime after self-review identified that the known On/authorized baseline
 must be asserted before every runner-issued OFF, not only before cycle one.
 The runner now checks the pinned AP and current-MAC authorization immediately
 before each recovery-pending OFF edge; a new immutable request is required.
- R3 packet note 2: CR-236 was likewise withdrawn before a decision and before
  guest runtime when the independent final precheck found that the per-cycle
  On assertion itself must be immediately adjacent to that authorization and
  before `RADIO_OFF=1`.  The runner now requires that explicit per-cycle On
  observation; a fresh immutable request is required.
## R3 AFTER-FIX RUNTIME — DHCP GATE FALSE-POSITIVE CANDIDATE

- anomaly_id: LAB-A2DF-DHCP-SIGNATURE-SCOPE-20260720
- status: CONFIRMED_DEVIATION
- runtime result: the Stage-1-approved R3 run
  /home/dima/Projects/aiam/runtime-captures/itlwm-3cd3-a2df-pinned-observed-invariants-20260720T072714Z
  stopped at cycle 1 before the first ping with:
  Wi-Fi selected DHCP signature changed at cycle_1_before_ping.
- gates that passed before that stop: pinned guest/host key/AP identity, initial
  On/authorization, strict Off, strict On, station absence, fresh authorized
  AP association epoch, management IPv4/default route, Wi-Fi IPv4
  10.77.0.47, and direct target route
  destination=10.77.0.1 nexthop=direct interface=en1.
- safety result: post-failure pinned read-only observation recorded radio On,
  en1 active with 10.77.0.47, default en0 route, and direct en1 lab route.
  No panic, reboot, raw scan, state-mutating route/address/DHCP command, or external-host
  action occurred.
- observed evidence: preflight DHCP includes a 7200-second lease and
  renewal/rebinding values 0xd29/0x17b5. A later read-only post-failure sample
  has the same address/server/lease/mask/broadcast/DNS/router values and
  renewal/rebinding values 0xe10/0x189c. It is supporting evidence, not proof
  of the exact values at the failing gate because R3 did not capture a raw
  post-authorization DHCP snapshot before it asserted.
- confirmed deviation: R3's one compared selected-DHCP signature conflates
  identity/configuration fields with lease-transaction/timing fields
  renewal_t1_time_value and rebinding_t2_time_value. The latter may change with
  elapsed lease time or a reconnect lease epoch, so equality does not express
  the claimed persistent-configuration invariant.
- rejected interpretation: this run does not establish a driver, association,
  route, address, or data-plane regression. No ping was attempted, and a
  material DHCP-option difference at the exact failure point remains
  unobserved.
- root cause: test evidence scope, not AirportItlwm behavior.

### FIX_CANDIDATE — LAB-A2DF-DHCP-SIGNATURE-SCOPE-20260720-R4

- anomaly_id: LAB-A2DF-DHCP-SIGNATURE-SCOPE-20260720
- symptom: a DHCP gate can fail despite preserved observed address and routes,
  but the evidence does not reveal whether its difference is volatile lease
  timing or a material DHCP identity/configuration option.
- expected system behavior: compare only a named stable DHCP
  identity/configuration signature; capture the transaction/timing signature
  separately and never claim it is persistent. Before any DHCP equality gate
  after reauthorization, save a best-effort raw state snapshot and log both
  named signatures so a failure is self-describing.
- actual behavior: a single signature includes yiaddr, siaddr, server,
  lease_time, T1, T2, mask, broadcast, DNS, and router, then fails without
  logging the actual compared value or saving a post-authorization snapshot.
- exact divergence point: guest_dhcp_config_signature(),
  assert_observed_network_invariants(), and the post-authorization path
  before verify_preexisting_data_plane().
- evidence from runtime: the R3 failure and pre/post read-only evidence above;
  no driver source, decomp, or reference behavior is implicated.
- evidence from decomp: not applicable — external laboratory diagnostic
  instrumentation only.
- exact semantic mismatch: volatile DHCP transaction/timer values are treated
  as a persistent configuration invariant, while their transition is not
  captured at the assertion boundary.
- diagnostic class: DIAGNOSTIC_INSTRUMENTATION
- exact hypotheses being disambiguated:
  - only DHCP transaction/timing values change across a normal reconnect;
  - a material DHCP identity/configuration field changes;
  - a separately observed stable DHCP difference correlates with a later
    route/address/data-plane failure.
- exact probe points:
  - stable signature: yiaddr, siaddr, server_identifier, lease_time,
    subnet_mask, broadcast_address, domain_name_server, router;
  - transaction/timing observation: xid, ciaddr, renewal_t1_time_value,
    rebinding_t2_time_value;
  - best-effort full ipconfig getpacket snapshot immediately after authorized
    fresh association and before the equality/ping path, in addition to existing
    preflight/Off/post/final snapshots.
- why these probe points are sufficient: they turn the former opaque
  false-positive candidate into a single-run discriminator without weakening
  material address/server/mask/DNS/router checks or claiming all DHCP values are
  immutable.
- why instrumentation is behavior-neutral: every added operation is a
  read-only ipconfig parse or existing best-effort snapshot. It changes no
  Wi-Fi state, DHCP client state, route, address, AP, credential, packet,
  kext, Agent, or external host.
- what exact runtime evidence must be collected: four cycles that record
  preflight and post-authorization textual DHCP state, both named signatures at
  every invariant gate, existing strict radio/AP/route/address gates, and
  source-bound ping outcomes. A stable-signature difference must fail with
  values logged; a timing-only difference is recorded but not judged
  persistent-state loss.
- files/functions to modify:
  - scripts/run_tahoe_lab_radio_gates_no_route_mutation.sh: split stable and
    transaction/timing DHCP observations, log actual values, and capture
    post-authorization state before the invariant/ping gate;
  - scripts/test_tahoe_lab_radio_gates_no_route_mutation_contract.sh: require
    split scope and capture ordering.
- forbidden alternative fixes considered and rejected:
  - ignore all DHCP evidence;
  - silently suppress any DHCP mismatch;
  - force DHCP renew/release, route/address restoration, or a new association;
  - infer that T1/T2 alone caused the R3 failure without the missing boundary
    snapshot.
- verification plan:
  1. shell parse/static contract and immutable Stage-1 review;
  2. exactly one fresh pinned-QEMU four-cycle R4 run;
  3. Stage-2 review only after evidence distinguishes the named signatures.

### R4 PRE-REVIEW ADDENDUM — COMPLETE ASSERTION-BOUNDARY EVIDENCE

- new evidence: independent static review confirmed that the first online
  after-authorization snapshot precedes the pre-ping DHCP gate, but no textual
  snapshot precedes the after-ping or final DHCP equality gates. It also found
  that the radio-Off invariant observes only default route/management IPv4,
  despite R4 wording that could be read as named DHCP signatures at every gate.
- expected system behavior: every online Wi-Fi DHCP stable-signature comparison
  must have an `ipconfig getpacket` textual snapshot immediately before it: after authorization
  before ping, after ping before its comparison, and final before its
  comparison. The radio-Off gate cannot require a Wi-Fi DHCP textual rendering because
  Wi-Fi is intentionally Off; it must instead explicitly compare/log the
  management DHCP stable signature and transaction/timing observation alongside
  its existing management route/IP checks.
- exact divergence point: verify_preexisting_data_plane() after ping, final
  ordering, and assert_management_invariants().
- exact probe points:
  - preserve the existing after-authorization textual snapshot immediately before
    the pre-ping equality gate;
  - add an after-ping-before-invariant textual snapshot;
  - add a final-before-invariant textual snapshot;
  - have assert_management_invariants() observe/compare named management DHCP
    stable/timing values at the radio-Off gate and expose that observed stable
    value to the encompassing online invariant gate.
- claim correction: named Wi-Fi DHCP signatures are compared only while Wi-Fi
  is online. The radio-Off gate claims only management DHCP signature equality
  plus a best-effort Wi-Fi packet result, not a Wi-Fi DHCP equality.
- behavior neutrality: added work remains read-only ipconfig parsing and textual
  snapshot capture; it adds no packet, DHCP, route, address, association, AP,
  kext, Agent, or external-host action.
- verification: extend the static contract to require all three online
  assertion-boundary snapshots and the management DHCP observation at Off,
  then obtain a fresh Stage-1 decision before R4 runtime.

## R4 WITHDRAWN BEFORE STAGE-1; REVISED FIX_CANDIDATE R5

- decision: R4 is withdrawn before artifact submission, reviewer decision, or guest
  runtime. Independent pre-review found that it still treated separate saved
  textual and parsed ipconfig calls as though they described the same client state, and its
  planned stable signature had no proven external contract for byte-identical
  preservation across a reconnect.
- confirmed residual deviations:
  - R4's textual post-authorization capture and later parsed equality values came
    from separate ipconfig calls and could observe different DHCP transactions;
  - later after-ping/final comparison boundaries lacked an immediately preceding
    saved textual rendering in the same evidence relation;
  - DHCP fields such as siaddr and lease_time were over-labelled as stable
    configuration, while the observed evidence establishes only parser shape;
  - a stable mismatch would stop before ping, so R4 could not test its stated
    data-plane-correlation hypothesis.
- rejected alternative: do not change the harness to send ping after an
  unproven DHCP-option mismatch. That would alter its gate/control-flow
  semantics without a proven need.

### FIX_CANDIDATE — LAB-A2DF-DHCP-SAMPLE-BINDING-20260720-R5

- anomaly_id: LAB-A2DF-DHCP-SIGNATURE-SCOPE-20260720
- symptom: the harness needs DHCP evidence around reconnect without claiming
  byte-identical DHCP configuration or associating a parsed signature with a
  different textual rendering.
- expected system behavior: at each declared online observation point, obtain
  exactly one read-only `ipconfig getpacket` stdout sample per interface; persist
  that exact stdout text in the fresh evidence directory and derive all named
  network-option and transaction/timing observations locally from that same
  saved text. These DHCP observations are diagnostic evidence, not PASS/FAIL
  configuration-equivalence gates. Functional gates remain address, route,
  radio, authorization, fresh association, and source-bound traffic.
- actual behavior: R3 did not preserve the matching `ipconfig getpacket` stdout
  rendering. The unrun R4 design could compare separately fetched data and
  would overstate a persistent DHCP contract.
- exact divergence point: R3's independent parse/capture calls and R4's
  proposed separate stable/timing signature calls.
- evidence from runtime: the R3 failed directory, post-failure read-only
  snapshot, and pre-review source analysis above. No R4 runtime exists.
- evidence from decomp: not applicable — host-side diagnostic instrumentation
  only.
- exact semantic mismatch: a temporal/contract relationship that was not
  observed was described as an invariant.
- fix justification path: SYSTEM_CONTRACT_FIX (external A2DF harness)
- exact hypotheses being disambiguated:
  - whether the textual rendering's named network options change across a
    reconnect;
  - whether only transaction/timing fields change;
  - whether functional address/route/association/data-plane gates remain
    healthy while either observation relation is recorded.
- exact probe points:
  - one exact `ipconfig getpacket` stdout rendering for en0 and en1 at preflight, online
    post-authorization-before-ping, online after-ping-before-invariant, and
    final-before-invariant points;
  - local derivation from each saved stdout rendering of network-option observation
    fields: server_identifier, subnet_mask, broadcast_address,
    domain_name_server, router;
  - local derivation from that same rendering of transaction/timing observation
    fields: xid, ciaddr, yiaddr, siaddr, lease_time,
    renewal_t1_time_value, rebinding_t2_time_value;
  - an explicit sample-unavailable record rather than an empty string
    interpreted as a changed option.
- why these probe points are sufficient: every later analysis can compare a
  printed observation with the exact saved stdout rendering that produced it. The batch
  distinguishes timing/transaction variation from option variation without
  inventing an immutable DHCP contract.
- system-visible control scope: R5 deliberately removes an unproven DHCP
  equality gate, so the already-declared source-bound ping can run after a
  textual-field delta when all real functional gates pass. It adds no ping
  command, destination, source, count, DHCP renew/release, route/address,
  association, kext, Agent, AP, or external-host action; it restores the
  declared four-cycle functional scenario to its existing 20-request maximum.
- what exact runtime evidence must be collected: one four-cycle run with all
  existing functional gates plus named stdout-bound DHCP observations and sample
  availability at each online phase. DHCP option/timing changes are reported as
  evidence, not converted into a driver failure verdict by this runner.
- why this is root cause and not just correlation: direct source control flow
  proves the old mixed/independent evidence relation; runtime proves the first
  opaque false-positive candidate. No driver causality is asserted.
- why proposed path is 1:1 with reference architecture and semantics: N/A by
  exact non-driver scope. It does not model or replace an Apple driver path.
- files/functions to modify:
  - scripts/run_tahoe_lab_radio_gates_no_route_mutation.sh: replace independent
    DHCP parse/equality calls with stdout-bound sample capture and local
    observations; remove DHCP equality gating;
  - scripts/test_tahoe_lab_radio_gates_no_route_mutation_contract.sh: require
    stdout/sample binding and forbid DHCP configuration-equivalence claims.
- forbidden alternative fixes considered and rejected:
  - silently discard DHCP evidence;
  - continue/fail pings based on an unproven DHCP option contract;
  - force DHCP renew/release or route/address repair;
  - fetch a second `ipconfig getpacket` rendering and claim it is the first capture.
- verification plan:
  1. static parse/contract and exact stdout-sample parser replay on saved evidence;
  2. immutable Stage-1 review;
  3. exactly one fresh pinned-QEMU four-cycle R5 run;
  4. Stage-2 review before any commit.


### R5 SCOPE AMENDMENT BEFORE IMPLEMENTATION

- classifier correction: R5 records named DHCP fields from textual stdout for later comparison; it
  does not classify any delta as timing-only, material, semantically harmless,
  or causal. No bytewise DHCP field is a PASS/FAIL functional invariant.
- sample binding: each named phase/interface sample must persist the exact stdout
  text, command exit status, and SHA-256. All display observations are parsed
  locally from that same saved text. The broad capture_read_only_state output
  is supplemental and cannot stand in for that bound sample.
- unavailable sample rule: a command failure/empty stdout rendering records
  `sample_availability=INCONCLUSIVE`, and the final aggregate reports
  `ipconfig_getpacket_stdout_observation_result=INCONCLUSIVE`, never a DHCP
  change. Functional gates may still demonstrate their own result, but overall
  evidence must not emit the unqualified four_cycle_result=PASS while DHCP
  observation is incomplete.
- verdict scope correction: R5 removes DHCP equality gating. This changes the
  harness verdict/control flow relative to R3/R4: existing source-bound pings
  can continue after an old DHCP-equality false positive, but no new
  state-mutating guest operation or runner-issued IP data-plane destination,
  count, source, or packet command is introduced. Stage-1 must review that exact
  scope correction.
- functional scope: hard gates remain radio/AP/current en1 IPv4/direct route
  and permit at most four times five source-bound pings only to 10.77.0.1. All
  four functional cycles may complete even if later DHCP textual stdout evidence
  is INCONCLUSIVE; that diagnostic result separately prevents aggregate PASS.
- protocol completeness: the direct source/evidence relation above establishes
  the evidence-tool root cause rather than driver correlation; reference 1:1 is
  N/A because no driver path is changed or claimed.

### R5 SYSTEM-CONTRACT COVERAGE BEFORE STAGE-1

- reclassification: the aggregate R5/R5B patch is a
  `SYSTEM_CONTRACT_FIX` for the external A2DF evidence harness, not pure
  `DIAGNOSTIC_INSTRUMENTATION`. Removing the unproven DHCP equality gate changes
  whether an already-existing ping is reached, so describing the full patch as
  behavior-neutral instrumentation would be false.
- contract touchpoint 1 — radio ownership: the only runner-issued state changes
  are the original public `networksetup -setairportpower en1 off/on` calls on
  the pinned QEMU guest, at most four normal cycles. All four can complete when
  functional gates pass even if later DHCP evidence is INCONCLUSIVE; abnormal
  cleanup may only compensate an owned Off with observed On.
- contract touchpoint 2 — reconnect ownership: reconnect is saved-profile
  autojoin only. The runner has no password-bearing join command, arbitrary
  helper, scan, or credential input.
- contract touchpoint 3 — identity and observer authority: the guest host key,
  guest build/model/MAC, AP interface/BSSID/SSID/mode/channel/width, and station
  observer are pinned. Observer failure fails the run; these probes are
  read-only.
- contract touchpoint 4 — functional admission: radio power, AP authorization,
  fresh association, current en1 IPv4, management IPv4/default route, and the
  direct selected route to 10.77.0.1 remain hard gates before traffic.
- contract touchpoint 5 — DHCP evidence: `ipconfig getpacket` is an explicit
  read-only textual client-state query, not a wire capture or DHCP state change.
  Its values are never equality gates. A failed, empty, malformed, or locally
  unpersisted sample is `INCONCLUSIVE` and prevents unqualified aggregate PASS.
- contract touchpoint 6 — data plane: the sole runner-issued IP data-plane
  action is the pre-existing `ping -S <preexisting-en1-ip> -c 5 -W 1000
  10.77.0.1`, once per completed functional cycle: at most 20 requests. All 20
  can occur when the four functional cycles complete even if later DHCP evidence
  is INCONCLUSIVE; only complete evidence permits aggregate PASS. R5 adds no
  ping destination, source, command type, count, `-W` option, artificial delay,
  or retry. It does insert/read local textual evidence before the existing ping,
  so no unchanged wall-clock timing is claimed.
- contract touchpoint 7 — local evidence: stdout/stderr/status files are local
  host evidence only; a persistence failure is explicitly incomplete rather
  than silently treated as a complete DHCP observation.
- contract touchpoint 8 — excluded systems: the patch has no kext, firmware,
  AuxKC, staging, release, reboot, AP configuration, or `10.90.10.22` action.
- exact control-flow correction: the R3 runtime passed all functional admission
  gates then stopped at an opaque selected-DHCP equality comparison before its
  first pre-existing ping. R5 removes only that unsupported diagnostic gate;
  it restores the user-authorized A2DF scenario's already-declared ping after
  functional admission, rather than adding a new system-visible probe.
- supersession of R4's rejected alternative: R4 rejected sending a ping after an
  unproven *classified DHCP mismatch*. R5 has no DHCP-delta classifier or
  mismatch branch at all; pings are admitted only by the address, route, radio,
  authorization, and fresh-association hard gates above.
- lifecycle completeness: preflight, all four Off/On/reassociation cycles,
  pre-ping and post-ping evidence points, final gates, abnormal radio recovery,
  and diagnostic-inconclusive termination are covered by explicit assertions or
  fixtures. No relevant harness touchpoint remains outside this scope.
- control-flow differences: the only difference relevant to data-plane
  reachability is the pre-existing, already-bounded ping after the invalid
  equality stop. Separately, the final evidence-completeness branch can block
  aggregate PASS after otherwise functional completion. The corrected contract
  requires traffic only after the same hard functional conditions; R5 introduces
  no new IP data-plane target, packet type, command, network write, or lifecycle
  owner.

### R5 IMPLEMENTATION-SAFETY AMENDMENT BEFORE REVISION

- terminology correction: `ipconfig getpacket` emits a textual rendering of
  client-held DHCP state. It is not a wire-level DHCP packet and its byte count
  and SHA-256 bind only that exact stdout rendering. R5 evidence, labels, and
  paths must say `ipconfig_getpacket_stdout`, never imply captured packet bytes.
- evidence-persistence defect: the initial R5 implementation invoked each
  sample function through `|| true`. In Bash, that conditional context disables
  `errexit` for commands within the function. A failed local evidence step after
  a successful SSH response (hashing, status-file write, or parser) could
  therefore leave a misleading `COMPLETE` result.
- expected behavior: any failed local persistence, hash, parser, or required
  textual-format check must mark that sample `INCONCLUSIVE`, set the aggregate
  diagnostic-incomplete flag, and prevent an unqualified `four_cycle_result=PASS`.
  This remains diagnostic only; it must not turn a textual DHCP-field delta into
  a functional-driver verdict.
- minimal sample completeness: a complete textual rendering requires command
  exit status zero, non-empty stdout, successful local evidence persistence and
  parsing, and the expected BOOTP-reply markers `op = BOOTREPLY` and `yiaddr =`.
  It does not require any named option to be present or equal across samples.
- verdict-label correction: a completed loop prints `cycle=N
  functional_result=PASS`; aggregate functional and textual-observation results
  remain separately named. This prevents a per-cycle functional result from
  being read as a complete DHCP-evidence verdict.
- behavior scope: this R5 safety sub-revision adds no guest state mutation or
  runner-issued IP data-plane command. It only makes the existing R5 local
  evidence handling honest and changes labels to match the observed data type.
- verification: extend the static contract to require explicit local-evidence
  error handling and forbid conditional `capture_dhcp_sample ... || true`; add
  a local mocked-fixture test for successful capture plus capture, hash, parser,
  and status-write failures; then obtain a fresh immutable Stage-1 review before
  any R5 guest runtime.

### R5 STATUS-PERSISTENCE AMENDMENT BEFORE REVISION

- newly found persistence edge case: a brace group containing several `printf`
  calls was itself the condition of `if ! ...`. Bash disables `errexit` in that
  conditional context, so a non-final failed `printf` could be followed by a
  successful one and make the group appear successful; the redirection attached
  to that compound group also did not reliably enter the intended failure branch.
- expected behavior: serialize the complete status record into one in-memory
  payload, then perform one checked write. A failed checked write must set the
  aggregate diagnostic-incomplete flag and return normally only after emitting
  an explicit `status_write_failed` evidence line; it must never retain a
  `COMPLETE` verdict.
- scope: this is local evidence-file correctness only. It adds no guest state
  mutation, runner-issued IP traffic, network state mutation, kext action,
  staging, or reboot.
- verification addition: fixture coverage must separately force a failing
  status-payload construction and a failing status-path open, alongside the
  existing capture/hash/parser/text-format cases. The revised writer has no
  independent per-field output writes to mask. The static contract must also
  prove one `ipconfig getpacket` invocation site per captured interface sample
  and exactly the management/Wi-Fi pair at each named phase.

### FIX_CANDIDATE — LAB-A2DF-IPCONFIG-EVIDENCE-SCOPE-20260720-R5B

- anomaly_id: LAB-A2DF-DHCP-SIGNATURE-SCOPE-20260720
- symptom: R5's status persistence could conceal a local write failure, and the
  runner simultaneously called read-only `ipconfig getpacket` while declaring
  `explicit_dhcp_command=none`.
- expected system behavior: every textual-sample persistence failure must become
  diagnostic `INCONCLUSIVE`, and emitted scope labels must distinguish an
  explicit read-only query from prohibited DHCP state mutation.
- actual behavior: the previous multi-`printf` conditional status writer could
  mask a non-final write failure; a redirection failure on its compound group
  did not reliably take the intended failure branch. Separately, the blanket
  DHCP-command label contradicted the explicit read-only query in the same
  runner.
- exact divergence point:
  `capture_dhcp_sample()` status-write branch and the preflight scope-label
  lines in `run_tahoe_lab_radio_gates_no_route_mutation.sh`.
- evidence from runtime: no new guest runtime is needed or claimed for this
  local shell/evidence defect. R3's failed evidence directory established why a
  complete, correctly bound textual sample is required at each named point.
- evidence from source/fixture: direct Bash control-flow review reproduced the
  compound-group persistence failure; deterministic local fixtures now cover
  success plus capture, hash, parser, unexpected-text, status-payload, and
  status-path failures without contacting the guest.
- evidence from decomp: N/A — this changes no driver, kext, firmware, or Apple
  semantic path.
- exact semantic mismatch: the evidence tool could emit a complete diagnostic
  status without verified local persistence and made an internally contradictory
  claim about its own read-only DHCP query.
- fix justification path: SYSTEM_CONTRACT_FIX (inherits aggregate R5 contract)
- exact hypotheses being disambiguated:
  - whether each `ipconfig getpacket` stdout sample is locally persisted and
    structurally complete;
  - whether a local evidence failure is isolated as `INCONCLUSIVE` rather than
    reinterpreted as a DHCP or driver state change;
  - whether the command-scope record names only prohibited state-changing DHCP
    actions while explicitly recording the read-only observation.
- exact probe points:
  - one checked in-memory status payload and one checked status-path write per
    textual sample;
  - fixture-induced failure of payload construction and status-path opening;
  - static proof of one `ipconfig getpacket` invocation site and exactly one
    management/Wi-Fi pair for every named capture phase.
- why these probe points are sufficient: they cover every local persistence
  boundary after the single guest query and prove the label matches the exact
  command class. They add no new runtime traffic or guest behavior.
- system-contract scope: this R5B subset only writes local evidence more
  accurately and renames local claims. It adds no guest state mutation,
  runner-issued IP data-plane packet, route/address/DHCP state change,
  association, AP change, kext action, staging, reboot, or external-host action;
  the aggregate control-flow change is stated in the R5 system-contract coverage
  above.
- what exact runtime evidence must be collected: after a fresh immutable Stage-1
  approval, exactly one existing four-cycle lab run must emit either complete
  textual-sample evidence at each point or an explicit `INCONCLUSIVE` failure;
  it retains the existing radio, authorization, route, address, and 20-ping
  functional gates.
- why this is root cause and not just correlation: the old outcome follows
  directly from Bash conditional/group semantics and its source labels; the
  fixture reproduces the error paths deterministically without inferring driver
  causality.
- why proposed path is 1:1 with reference architecture and semantics: N/A by
  exact non-driver scope. It corrects only the harness's own evidence contract.
- files/functions to modify:
  - `scripts/run_tahoe_lab_radio_gates_no_route_mutation.sh`:
    `capture_dhcp_sample()` status serialization and explicit state-mutation vs
    read-only-query labels;
  - `scripts/test_tahoe_lab_radio_gates_no_route_mutation_contract.sh`: static
    call-shape/scope-label checks and deterministic local fixtures;
  - `analysis/ANALYSIS_REPORT_2026-07-20.md`: exact diagnostic scope and
    persistence evidence.
- forbidden alternative fixes considered and rejected:
  - treat a failed evidence write as `COMPLETE` or silently discard it;
  - remove `ipconfig getpacket` while claiming equivalent DHCP evidence;
  - issue DHCP renew/release/set, route/address repair, a scan, or a join to
    compensate for missing evidence;
  - alter the candidate kext, stage it, reboot, or touch `10.90.10.22`.
- verification plan:
  1. `bash -n`, static call-shape/scope contract, and all local fixtures;
  2. exact artifact/hash verification and fresh Stage-1 structural review;
  3. one approved four-cycle pinned-QEMU R5 run only;
  4. Stage-2 review before commit or push.
