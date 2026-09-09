# Restrict WCL shared-channel filtering to an accepted AP lifecycle

## Reproduced policy defect

The default Tahoe product publishes `ap1` during initialization. Its existence
is not a request to run an AP. Nevertheless, `setWCL_REASSOC` always queried
IWN's required STA+AP channel, which deliberately reports the current primary
channel even before PAN startup. An ordinary STA request could therefore lose
all of its off-channel candidates, acknowledge a no-op and arm an AP handoff
reservation while neither AP nor Internet Sharing was active.

The association-epoch fix prevents that reservation from surviving a real
leave, but does not make the filtered-out roam request execute. This is a
separate user-visible restriction on changing BSS/channel.

The reference `AppleBCMWLANNetAdapter::sendReassocCommandLegacy` in the
25C56 `aiam_applebcm_reassoc_25C56_20260801/reassoc.txt` export checks primary
association, converts the supplied channel list and forwards `WLC_REASSOC`.
It does not justify restricting an ordinary STA to its current channel.
This reference establishes the upper request behavior, not Intel's lower
single-channel concurrency implementation.

## Change

The WCL producer now queries a controller-level primary-roam constraint.
That query admits the HAL's required shared channel only when the lower AP
context exists or the AP owner has an accepted start, recovery or stop in
progress. A merely allocated owner, saved AP profile or old STA retention
token is not AP intent. A terminal owner cannot constrain a successor.

The HAL query itself is unchanged: AP startup/channel admission still needs
the associated primary channel before a PAN context exists. Running APs and
accepted asynchronous transitions retain that hardware constraint; backends
which return zero for it retain their prior behavior.

In particular, a WCL request preceding any accepted HOST_AP_MODE must keep its
real roam targets. A later userspace AP operation cannot retroactively make
that earlier request an AP handoff. This removes the former unsupported
inference from an off-channel candidate list alone.

## Verification boundary

The UBSan regression compiles the actual owner query, controller query and
complete production `setWCL_REASSOC` body, with the actual public and common
request structures. It verifies off-channel target/score/policy preservation
and real scan dispatch for an idle STA, retained-token non-admission, each AP
lifecycle flag, lower-only AP context, terminal owner, multi-channel backend,
AP-only busy/filtered-empty outcomes, literal-empty input and null input.
Existing APSTA shared-channel/start, association-epoch, WCL roam/terminal and
public BSSID-pin tests pass.

Source `3b14777c` built successfully with all 1083 BootKC symbols resolved.
Its UUID is `CE496F64-EC13-3E93-AEEE-70388EFAD7CC`, Mach-O SHA-256
`1153c53b389b283bae5728a3c885e43dd17518a537682aa79c5930819d8d57b1`.
Private AuxKC admission passed with five members and no canonical mutation.
It has deliberately not been activated or published because the following
additional carrier defect was discovered before the canonical swap.

## Live carrier contradicts the old candidate interpretation

At 14:02:55 UTC, on the still-loaded `35589ce0`, FBT captured a real request
with this call chain:

`WCLNetManager::setROAMWithBssid -> sendReassocCommand -> sendReassocToDriver
-> WCLGlue -> AirportItlwmSkywalkInterface::setWCL_REASSOC`.

The first 100 bytes (channel list) were zero, bytes `+0x64..+0x69` were all
`ff`, the count at `+0x90` was one, and the channel count at `+0x94` was zero.
The driver then armed `armPrimaryStaHandoffScan` and returned success. There
was no AP request active in that interval. This is a broadcast-BSSID roam
request, not an explicit channel-255 preference. The existing producer reads
those six bytes as `{score=0xffffffff, channel_spec=0xffff}`, filters it out
against the current STA channel and acknowledges a no-op.

The reference NetAdapter's legacy and V1/V3 builders copy those six-byte
entries into the firmware BSSID fields/list. The older reference note and
local tests' score/channel tuple interpretation at `+0x64` are therefore
incorrect. The intent regression above tests its intended lifecycle gate,
but its constructed candidate values do not validate the true carrier ABI.

The next implementation must correct public/common BSSID representation,
broadcast/unspecified-BSSID semantics and actual candidate matching, together
with the AP-intent gate. The existing literal-empty and filtered-empty
shortcuts must be reconsidered against that real contract; they cannot be
used as evidence of a functional roam. Only then should this candidate be
rebuilt, activated and tested through the normal user path. The published
and loaded artifact remains `35589ce0`; the physical user machine is unchanged.

The subsequent source correction and updated production-method tests are
documented in `TAHOE_WCL_REASSOC_BSSID_ABI_20260909.md`. They supersede the
score/channel candidate fixtures and empty-request outcomes in the initial
verification paragraph above; the initial candidate remains unactivated.
