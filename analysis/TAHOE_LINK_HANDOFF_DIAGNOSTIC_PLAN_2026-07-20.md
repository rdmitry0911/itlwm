# Tahoe link-handoff diagnostic layer — 2026-07-20

## Triggering observation

The exact `v2.4.0-alpha-ff7b960` candidate was identity-bound before its
saved-profile experiment.  Its redacted snapshots showed net80211 moving from
`IEEE80211_S_SCAN` to `IEEE80211_S_RUN`, while the pre-trigger controller
status was already `0x3` (valid + active).  The same capture contained no
successful EAPOL trace or accepted WCL link-state publication and later
observed a join-abort.

That rules out the simple claim that the WCL association carrier never reached
net80211.  It does not by itself prove why the userspace link handoff was
missing, so this layer instruments all adjacent non-secret decision points in
one candidate rather than testing them one at a time.

## Boundaries captured in one epoch

With the existing `sae-on` control mode, the candidate records:

1. Each controller `setLinkStatus` request: prior status, requested status,
   and whether it was applied, short-circuited as unchanged, or rejected by
   lifecycle admission.
2. The off-gate link publication path: source unavailable, queued, rejected
   by the work-loop ownership predicate, or returned by `setLinkState`.
3. Entry and exit of `setWCL_JOIN_ABORT`, including only the net80211 state
   number and whether completion was requested.
4. The existing pre/post snapshots, which already expose the redaction-safe
   controller state and current link-status word.

No event carries an SSID, BSSID, pointer, packet payload, PMK, passphrase, or
other credential material. `evaluate_tahoe_link_handoff.py` emits only counts,
status words, and one of these structural labels:

- `PREMATURE_ACTIVE_SHORT_CIRCUIT`
- `LINK_PUBLICATION_PROGRESS`
- `LINK_PUBLICATION_INCOMPLETE`
- `DIAGNOSTIC_INCOMPLETE`

None is an association, authentication, EAPOL, DHCP, ping, or traffic PASS.

## Candidate correction under test

The local port models Apple's hidden low-latency object as `fNetIf`. Applying
the recovered low-latency link-up tail directly to that alias during
`setInterfaceEnable(true)` sets the main interface active before association.
The real later `LINK_STATE_UP` request can then compare equal to
`currentStatus` and return before the off-gate WCL publication. The candidate
retains the required base enable but removes that aliased premature link-up;
only the real net80211 association-success path may publish active link state.

The laboratory result must distinguish this correction from a coincidental
change. A useful progress record requires either a non-active pre-trigger
status or an observed valid-to-active transition after the initial state was
cleared, plus an applied active `setLinkStatus`, a queued publication, and a
successful off-gate `setLinkState` return. A duplicate active request after a
real applied transition is not evidence that the real transition was
short-circuited. The result still requires separate EAPOL, link, address, and
traffic tests before any functional connection claim.

## Verification plan

The source contract runs a fixture matrix for all four labels and asserts the
status/queue/abort probes, the off-gate guard, and the absence of premature
`reportLinkStatus(3)` / `setLinkState(UP)` side effects in the aliased
`setInterfaceEnable` body. The normal Tahoe gate then performs the isolated
kext, BootKC, Agent, and RegDiag builds without installation, loading, or
reboot. Only after that candidate is released will the laboratory guest be
updated and rebooted for one controlled capture.

## Recorded pre-runtime result

On 2026-07-20 this exact source layer passed the link-handoff evaluator
fixture matrix, its static source contract, and the aggregate
`run_tahoe_sae_quarantine_layer.sh` gate. The latter also completed an
isolated Tahoe kext build, BootKC undefined-symbol resolution, Agent build,
and RegDiag client build. This is a successful build-and-contract scenario,
not a runtime association result: it installed, loaded, rebooted, and
published nothing. The later laboratory capture is deliberately a separate,
versioned result so that build validation cannot be mistaken for Wi-Fi
functionality.
