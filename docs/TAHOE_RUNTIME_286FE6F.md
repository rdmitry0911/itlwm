# Tahoe runtime record: 286fe6f

This record binds the tested Tahoe candidate to source commit
`286fe6fe7537c267b2ddddec367fc83a1dcf85f3` and release tag
`v2.4.0-alpha-286fe6f`.  The archive, installed Mach-O, and loaded Mach-O
matched the digests and UUID in the committed runtime evidence after a
guest-only reboot.

The test used a fresh disposable QEMU overlay with one direct backing image.
It did not reboot a physical host or touch the remote validation host.  Raw
station, packet, network, and client output remains local-only; the committed
evidence contains aggregates and non-network identities only.

## Admission and materialization

Private AuxKC admission passed with exactly five required members and left the
canonical collection unchanged.  Transactional activation then reached
`READY_FOR_GUEST_REBOOT`; it made rollback copies, did not direct-load or
unload a kext, and was followed only by a QEMU guest reboot.  The post-reboot
read-only identity binding passed before either runtime scenario began.

## Bounded post-PLTI scenario

The trace runner performed one radio OFF/ON transition with saved-profile
autojoin only.  It issued no explicit join, route, address, or DHCP mutation.
The IWN trace backend acknowledged one synchronized generation; the two
reads were stable, there were zero dropped entries, and trace control was
disabled at completion.

The categorical trace contained one entry and one episode, not the required
closed kernel chain.  Its result is therefore `INTEGRITY_INCONCLUSIVE`, not a
functional failure and not a success claim.  This run does not prove pure SAE
or PMF functionality; it establishes only the bounded trace-control facts in
`evidence/runtime/tahoe_post_plti_trace_286fe6f_runtime.json`.

## A2DF baseline-control

The same exact loaded candidate completed four fresh radio OFF/ON cycles.  All
four reached stable authorization and a fresh association epoch.  Each cycle
passed its five-probe data-plane control, for twenty successful probes in
total.  Read-only DHCP observation completed, the management/default-route
invariant and pinned direct Wi-Fi route were preserved, and the final A2DF
four-cycle result was `PASS`.

This is a baseline regression result, not a generic Internet or pure-SAE
claim.  It is recorded with the candidate identity and local-only raw-artifact
digest in `evidence/runtime/tahoe_a2df_286fe6f_four_cycle.json`.
