# Tahoe passive link-context census build — 2026-07-21

## Purpose

Commits `b3f18178f870ed765202b226bb431a074ef1067b` and
`ad3646ab09316b2c6d26f8f63d956705aefe0bb3` add an opt-in, numeric
owner-context census for the Tahoe link-publication chain.  It observes the
actual net80211 edge, controller status, off-gate queue/action, guarded
publication, and two Skywalk contrast points.  It does not add a parent call,
alternative route, retry, replay, gate entry, retain, or lifecycle admission.

The net80211 bridge records only its already-sampled atomic association epoch.
It does not cast or dereference the controller, read a work loop, or reach HAL
state.  The evaluator requires one non-zero epoch, ordered route/stage markers,
consistent transition direction, and a trace with `dropped=0`; otherwise it
fails closed.  The BSD bridge's local link-state encoding and the downstream
IO80211 encoding are deliberately normalized only by that evaluator.

## Successful, bounded scenario

At `2026-07-21T00:17:49Z`, the complete Tahoe SAE/PMF build-admission gate
passed for exact source commit
`ad3646ab09316b2c6d26f8f63d956705aefe0bb3` on the provenance-pinned QEMU
guest (OS build `25C56`, BootKC SHA-256
`eb5691e94b750df8316f8474245966e02d1badd696f78aa27f003766c9bff06d`).

The full static matrix passed, including the new multi-fixture owner-context
and parent-result evaluators, historical SAE/PMF contracts, link-handoff
contracts, App Store controller-personality contract, and AuxKC contracts.  An
isolated guest build then produced the Tahoe debug `AirportItlwm.kext`, clean
built the Agent and RegDiag client, confirmed no
`_thread_call_cancel_wait` dependency, and resolved all 959 undefined symbols
against the pinned BootKC.

This is a successful compile/ABI-admission scenario for the passive layer.  It
does not claim that context tracing was enabled at runtime, that a parent
transition occurred, or that any wireless connection functioned.

## Explicit limits

No candidate kext was installed, loaded, unloaded, published, released, or
activated.  No guest or host reboot occurred.  No association, authentication,
radio transition, DHCP, address or route change, ping, or traffic test was
attempted.  The physical host and remote physical validation host were
untouched.

The machine-checkable, credential-free evidence is
`evidence/runtime/tahoe_link_context_census_ad3646a.json`.  It records only
sanitized build and static-contract facts; raw trace, network identifiers,
credentials, packets, and route snapshots are not committed.
