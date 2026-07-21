# Tahoe LinkContext bridge recorder argument order — 2026-07-21

## Observed discrepancy

The bounded, redacted LinkContext trace recorded the net80211 bridge with a
zero epoch and an invalid result/predicate combination even though downstream
owner-context records carried non-zero association epochs. This was a telemetry
serialization defect, not evidence that the bridge failed to execute.

`AirportItlwmRegDiagNet80211LinkContext` called the raw recorder using the
wrapper's parameter order. The raw recorder instead takes the sampled epoch,
three predicate values, and then the result. Consequently the sampled epoch
was encoded as a predicate and the result slot received a sentinel value.

## Safe correction and regression coverage

The bridge now passes exactly:

```text
assocEpoch, -1, -1, -1, kIOReturnSuccess
```

after its lifecycle marker. It remains a passive self-contained bridge: it
does not publish link state, enter a gate, retain or cast an object, inspect a
work loop, touch HAL state, retry, or replay an event. The static LinkContext
contract now asserts this exact tail and the epoch-before-result ordering; both
owner-context and parent-attestation evaluator fixture suites pass.

The full isolated Tahoe build-admission gate passed for this correction: the
Debug/Tahoe kext, Agent, and RegDiag client built cleanly, all 959 undefined
kext symbols resolved against the pinned BootKC, and the candidate had no
`_thread_call_cancel_wait` dependency. No kext was installed, loaded,
published, released, or rebooted during that build gate.

## Explicit limit

This correction makes future passive traces correlate the bridge epoch
correctly. It does not change the independently observed `OWNER_CONTEXT_GATE_HELD`
path, does not synthesize a parent acceptance, and does not relax the Tahoe
off-gate publication precondition. A subsequent runtime trace must still
confirm the corrected encoding before it is claimed as observed runtime
behavior.
