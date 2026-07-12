# CR-479: ranging authentication null-owner quarantine

Date: 2026-07-12

## Reference contract

The Tahoe 25C56 Core image is `BootKC_guest_25C56.kc`. Its fileset map puts
`AppleBCMWLANCoreMac` at VM `0xffffff800151d000` / file
`0x0141d000`. The current recovered preflight at
`FUN_ffffff80015eaf92` accepts a non-null carrier with a non-zero PMK length
at request `+0x70`, then checks the Core expansion's proximity owner at
`+0x2c28`. A missing or invalid owner leaves the exact public return
`0xe0000001`.

The continuation has real proximity firmware ownership. Current raw 25C56
instructions at `0xffffff80015ea1d5`, `0xffffff80015eaa82`, and
`0xffffff80015eb264` reference the Core diagnostic for failed `proxd` FTM
configuration; the same path loads the `+0x2c28` owner. The recovered Core
body sequences owner-targeted `proxd`, `wsec`, virtual ioctl `0xa5`, and
`ptk_start` requests before treating authentication as accepted. Success is
therefore a submitted proximity firmware operation, not a copied PMK carrier.

## Local divergence

AirportItlwm has no proximity owner and no `proxd`, `wsec`, or `ptk_start`
transport. Nevertheless, its Skywalk setter supplied a fabricated proximity
owner ID of `1`. Both Tahoe commander variants then cached PMK/role state,
completed selector 567, and the V2 route emitted several synthetic transport
successes. No Intel firmware operation occurred.

## Local correction

After retaining the public null and PMK-length validation,
`TahoeRangingOwner::apply` and the legacy `TahoeCommander` return
`TahoeErrorMap::kAppleRangingInvalid` (`0xe0000001`) before local state
mutation or completion. The Skywalk entry now passes no fictional owner ID;
the V2 commander only propagates the owner result and has no synthetic
selector-567 dispatch remaining.

This is the local equivalent of the observed Apple null-owner branch. It does
not claim that a Broadcom system with a constructed proximity owner rejects
ranging authentication, and it does not add an FTM/NAN implementation.

## Deterministic guard

`scripts/ranging_auth_quarantine_report.py --check` requires:

- the current 25C56 reference anchor and exact `0xe0000001` null-owner
  result in this note;
- validation followed by that result in both local owner/commander paths,
  with no ranging-cache mutation or completion;
- V2 propagation without selector-567 synthetic dispatch;
- Skywalk's absence of the fabricated owner ID; and
- absence of the required proximity firmware transport names from the Intel
  source tree.

The correction only removes a false user-visible success. It does not cover
ranging enable/start, NAN lifecycle, off-channel scheduling, ranging results,
or any Intel FTM capability.
