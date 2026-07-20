# Tahoe 8fefad9 release AuxKC preflight — 2026-07-20

## Successful, bounded scenario

Release `v2.4.0-alpha-8fefad9` was downloaded and inspected before the
laboratory check.  The asset contains a real `AirportItlwm.kext`, including
both `Contents/Info.plist` and `Contents/MacOS/AirportItlwm`; it is not a
loose Mach-O.  The archive SHA-256 is
`1d810192b993d289416102f8243056fdb965e4187276efd15e45ab3c124c4801`.
The exact kext binary SHA-256 is
`dd7acfc45a534fc22333f1811a359c8e0f6ec2bf7b583bc349c0fbd6617543b9`,
and its LC_UUID is `EC0C3E24-FA7A-354C-BC28-B4A005073309`.

The release bundle was copied only to a fresh private path on the pinned
laboratory guest.  The project-owned preflight helper (SHA-256
`238794cf475a13bfeba0fcc8caf4af48581228086efd7d2ade798da4ca783520`)
then built a private, explicit five-member AuxKC.  `kmutil create`, private
inspection, and private-plus-BootKC inspection each returned zero.  The
result contained the exact candidate UUID, retained the four non-AirportItlwm
members unchanged, and canonical bundle/AuxKC witnesses remained unchanged.
This is a successful exact-release **private AuxKC admission** scenario.

## Explicit limits

The copied debug release bundle had `codesign --verify` exit status `1` both
before and after its private copy.  This record makes no code-signing success
claim; that status did not prevent the observed private `kmutil` admission.

No canonical kext or collection was changed.  No kext was installed, loaded,
unloaded, released, or activated; the lab guest was not rebooted.  The
physical host and `10.90.10.22` were untouched.  No association, DHCP, ping,
or traffic scenario was attempted, so this is not a Wi-Fi functionality,
authentication, or data-path claim.

The machine-checkable, credential-free record is
`evidence/runtime/tahoe_lab_8fefad9_auxkc_preflight.json`.  The adjacent
contract binds the document to the exact release hashes and current helper,
and rejects a mutation, membership, scope, or functional-claim regression.
