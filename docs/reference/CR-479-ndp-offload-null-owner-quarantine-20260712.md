# CR-479: NDP offload null-owner quarantine

Date: 2026-07-12

## Reference contract

The Tahoe 25C56 `BootKC_guest_25C56.kc` fileset map places
`AppleBCMWLANCoreMac` at VM `0xffffff800151d000` / file `0x0141d000`.
Current raw code identifies `AppleBCMWLANCore::setOFFLOAD_NDP` at
`0xffffff80015d9bbe`. It initializes the public result to raw `0x16`, then
checks both the non-null NDP carrier and the Core expansion's NDP owner at
`+0x2c20` before it writes the IPv6 table.

With that owner present, the body clamps the input to four addresses, refreshes
the Core IPv6 and link-local state, invokes the owner at
`0xffffff80022c0f14`, and schedules `handleIPv6AddressNotificationGated`.
The latter is recovered at `0xffffff80015960ca`. The nearby raw diagnostic
reference at `0xffffff80015d9e24` resolves to the current
`setOFFLOAD_NDP` string. Thus a success is an accepted private owner operation
followed by gated Core notification, not merely an IPv6 cache update.

## Local divergence

AirportItlwm has no equivalent private NDP owner or gated notification backend.
Its generic and V2 Tahoe commanders nevertheless copied the carrier into
`TahoeOwnerRegistry::ndp`, marked a hidden notification, completed selector
554, and returned synthetic hidden-callback and virtual-ioctl successes. The
Skywalk setter then copied that fabricated state into its IPv6 cache. None of
those operations submitted NDP offload work to Intel firmware.

`setIPV6_PARAMS(...)` is now independently quarantined with IPV4_PARAMS: its
old local cache did not implement the Infra or notification lifecycle. It does
not make `setOFFLOAD_NDP(...)` a valid firmware-offload operation.

## Local correction

After the existing null checks, both local NDP owner paths return the reference
raw invalid-argument value `0x16` before NDP registry mutation or completion.
The V2 path only propagates that owner result, and the Skywalk entry no longer
copies NDP state into the IPv6 cache. This is the observed Apple missing-owner
branch for the locally absent backend.

This does not claim that an Apple system with a constructed NDP owner rejects a
valid request, and does not implement neighbour discovery, IPv6 wake offload,
or the private Core notification lifecycle.

## Deterministic guard

`scripts/ndp_offload_quarantine_report.py --check` requires the current 25C56
reference anchors in this note, raw `0x16` results before local NDP mutation or
completion in both commander paths, V2 owner-result propagation without
selector-554 synthetic dispatch, no Skywalk NDP cache copy, and no remaining
consumer of the fabricated registry state.
