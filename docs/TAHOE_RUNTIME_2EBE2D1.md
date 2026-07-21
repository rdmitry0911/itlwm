# Tahoe private-candidate runtime record: 2ebe2d1

This record binds a private candidate built from source commit
`2ebe2d1657936209a465cf3ff09c8e77db918210` to private five-member AuxKC
admission, transactional activation, a guest-only reboot, and post-boot
installed/loaded identity equality.  No semantic release tag or asset was
created or changed.

Raw artifacts remain local-only.  This committed record contains no wireless
identity, credential, address, route, packet, or raw client output.

## Candidate admission and activation

Private AuxKC admission passed without canonical collection mutation.
Transactional activation preserved the canonical five-member member set and
reached `READY_FOR_GUEST_REBOOT`; it performed no direct kext load or unload.
After the guest-only reboot, both the installed and loaded driver matched the
candidate digest and Mach-O UUID.

## Bounded runtime observations

The loaded candidate passed four bounded radio OFF/ON cycles.  Each met stable
authorization and fresh-association invariants; five permitted local data-plane
probes per cycle were all received.  Existing management/default and
direct-laboratory route/address invariants were preserved.  The runner issued
no explicit join, address, route, or state-mutating DHCP command.

The separate safe trace window completed on the IWN backend with a sealed
ordered diagnostic verdict.  The execution backend was IWN, so this run did
not exercise the IWX q0 observer added by this candidate.

## Scope and non-claims

This is only a saved-profile A2DF regression result.  It does not prove pure
SAE/WPA3-SAE, PMF-required association, PMF q0 completion, EAPOL success,
IGTK installation, WCL link publication, generic reachability, or any
physical/remote-host behavior.  The IWX observer remains a separate
hardware-specific runtime evidence requirement.

The aggregate-only record is
`evidence/runtime/tahoe_a2df_2ebe2d1_four_cycle.json`.
