# Tahoe link-state parent-result normalization — 2026-07-20

## Narrow correction

On Tahoe, `IO80211InfraInterface::setLinkState` has a five-argument `bool`
ABI.  The recovered accepted-transition reference records a low-bit-one return
as accepted.  The old callback assigned that bool directly to `IOReturn`,
therefore reporting accepted `true` as non-zero failure and rejected `false`
as zero success.

Commit `eb5fdb530ef6493ecb2e0fa35efd5f7d88ecbd96` keeps the raw parent result
in `linkTransitionAccepted`, maps `true` to `kIOReturnSuccess` and `false` to
`kIOReturnError`, and bases RT14 on the raw bool.  The existing RegDiag ABI is
unchanged: `result` and `lastLinkStateResult` now carry the normalized
`IOReturn`, while trace `arg2` carries Tahoe's raw `parent_accepted` value
`0` or `1`.  Pre-Tahoe records use an explicit `n/a` sentinel rather than
pretending their legacy `arg2` is a Tahoe parent result.

The change does not modify the off-gate owner predicate, WCL indication order,
`setRunningState`, or any legacy target's parent call.  In particular, it
does not add a direct `setLinkState` probe or bypass the inherited pipeline.

## Successful, bounded scenario

At `2026-07-20T23:22:59Z`, the complete Tahoe SAE/PMF build-admission gate
passed for the exact source commit above on the transport- and
BootKC-provenance-pinned QEMU guest.  The static link-handoff contract checked
the Tahoe bool declaration, both normalization outcomes, RT14's bool edge,
the pre-Tahoe `n/a` sentinel, and RegDiag rendering.  The isolated guest built
the Tahoe debug `AirportItlwm.kext`, resolved all 958 undefined symbols
against the exact BootKC, clean-built the Agent, and built RegDiag.

This is a successful compile/ABI-admission scenario for the correction.  It
is not a parent-false runtime observation: neither parent outcome was forced,
and no association was attempted merely to synthesize one.

## Explicit limits

No kext was installed, loaded, unloaded, published, released, activated, or
rebooted.  No network association, radio transition, DHCP, address/route
change, ping, or Wi-Fi traffic was attempted.  The physical host and
`10.90.10.22` were untouched.  This record is not a Wi-Fi, SAE, PMF,
association, or data-path pass claim.

The machine-checkable, credential-free record is
`evidence/runtime/tahoe_link_state_parent_result_eb5fdb5.json`; its contract
binds the source identity, successful build checks, unobserved runtime branch,
and non-claims.
