# Tahoe runtime record: ba4a2f0

This record binds the tested Tahoe candidate to source commit
`ba4a2f0833da4ca02d654f929bac8e5d4e8a6412` and the single mutable semantic
prerelease `v2.4.0-alpha`.  The archive, installed Mach-O, and loaded Mach-O
matched the candidate digest and UUID after a guest-only reboot.  Commit
markers remain Git provenance, not release names or adjacent release assets.

Both scenarios used fresh disposable QEMU overlays with one direct backing
image.  They did not reboot a physical host.  Raw station, packet, network,
and client artifacts remain local-only; the committed evidence is aggregate
only and contains no wireless identity, credential, address, or route.

## Admission and activation

Private AuxKC admission passed before each scenario and reported no canonical
collection mutation.  Transactional activation reached `ACTIVATION_READY` and
was followed only by a QEMU guest reboot.  No scenario directly loaded or
unloaded a kext.  A fresh read-only identity binding passed before each runtime
experiment.

## Bounded post-PLTI scenario

The runner performed one radio OFF/ON transition using saved-profile autojoin
only.  It issued no
explicit join, route, address, or DHCP mutation.  The IWN trace backend
acknowledged one synchronized generation; the frozen double-read was stable,
had zero dropped entries, and ended with trace control disabled.

The ordered categorical trace contained 33 entries in one episode and reached
`KERNEL_CHAIN_OBSERVED` with no missing stage.  This is a successful trace
instrumentation result, not proof of a generic association, reachability, pure
SAE, PMF, or physical-host behavior.  It does not prove pure SAE or PMF
functionality.  Its sanitized aggregate is
`evidence/runtime/tahoe_post_plti_trace_ba4a2f0_runtime.json`.

## A2DF baseline-control

A separate fresh guest with the same exact loaded candidate completed four
radio OFF/ON cycles.  Each cycle reached the runner's stable-authorization,
fresh-association, and five-probe pre-existing data-plane controls.  The
read-only DHCP observation completed, the existing management/default-route
and pinned direct Wi-Fi route invariants were preserved, and the final
four-cycle result was `PASS` with the radio On.

This is a bounded pinned-lab regression result, not a generic Internet or
pure-SAE/PMF claim.  Its sanitized aggregate is
`evidence/runtime/tahoe_a2df_ba4a2f0_four_cycle.json`.

## Release boundary

These records do not publish a new release or mutate `v2.4.0-alpha`.  That
semantic version remains one replaceable prerelease asset; it may be updated
only after a completed version-level layer meets its release gates.
